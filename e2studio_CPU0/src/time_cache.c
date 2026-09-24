/**
 * @file time_cache.c
 * @brief 現在時刻のノンブロッキング読み出し用キャッシュの実装（S-012-3 / Issue #213）
 * @details
 * 1 Hz のポーリングタスクが `time_ctrl_get()` を呼び、結果を `tk_dis_dsp()` 区間で
 * publish する。読み手は同じ区間で sample するだけで、ロックも待ちも伴わない。
 *
 * 位置づけと、なぜ RTC アクセスを専用タスクへ追い出すのかは `time_cache.h` を参照。
 * 設計メモ: `doc/design/issue-213.md`
 *
 * @note This file is part of the time display implementation (S-012-3).
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "time_cache.h"
#include "time_ctrl.h"

#include <tk/tkernel.h>

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** ポーリング周期 [ms] */
#define TIME_CACHE_POLL_PERIOD_MS   (1000)
#define TIME_CACHE_FLG_SET         (1U)

/**
 * スナップショットを陳腐化とみなすまでの経過時間 [ms]
 *
 * @note ポーリング周期の5倍。RTC停止だけでなく長いスケジューリング遅延でも
 *       到達しうるため、falseだけで故障原因を断定しない。
 */
#define TIME_CACHE_STALE_MS         (5000U)

/**
 * ポーリングタスクの優先度
 *
 * @note `ntshell_task`（`usermain.c:194`）と同値。どちらも RTC の利用者で、
 *       通常の1 Hz取得に加えてUI設定要求を実行する。
 *       最下位グループ（15-16）に置くと、`ai_inference` / `audio` に押されて
 *       publish が遅れ、陳腐化と誤判定されうる。
 */
#define TIME_CACHE_TASK_PRI         (12)

/**
 * ポーリングタスクのスタックサイズ [byte]
 *
 * @note `time_ctrl_get()` / `time_ctrl_set()` を呼ぶ。要求と読出しの暦構造体を
 *       ローカルに保持する。画面生成・文字列整形は本タスクでは行わない。
 */
#define TIME_CACHE_TASK_STKSZ       (1024)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/*
 * publish される 3 ワード。書き手は `time_cache_task` だけ、読み手は `time_cache_get()` の
 * 呼び出しタスク（現状 `lvgl_task`）。**ISR から書く経路は作らない**ので、
 * CLAUDE.md「並行性の既定形」に従い `tk_dis_dsp()` 区間で publish / sample する。
 * 3 つを別々の区間に分けると、読み手が「新しい時刻 ＋ 古いスタンプ」の組を採取して
 * 有効な値を陳腐化と誤判定するため、必ず同一区間に入れること。
 */

/** 直近に取得できたカレンダー時刻（`s_pub_valid` が true のときのみ意味を持つ） */
static time_ctrl_time_t s_pub_time;

/** 直近のポーリングが成功したか */
static bool             s_pub_valid = false;

/** 直近に publish した時刻（`tk_get_otm()` の下位 32bit, ms） */
static uint32_t         s_pub_tick = 0;

/** ポーリングタスクの ID。多重生成の判定に使う（書き手・読み手とも `time_cache_init()`） */
static ID               s_tskid = 0;
static ID               s_flgid = 0;

/* Single UI producer, one outstanding request. All shared fields below are
 * published/sampled together with dispatch disabled; no ISR uses this API.
 * The worker retains ownership through the post-write readback. A UI timeout
 * must never clear pending or allow a second request to overwrite this one. */
static time_ctrl_time_t s_set_time;
static uint32_t s_set_seq = 0;
static bool s_set_pending = false;
static time_ctrl_err_t s_set_err = TIME_CTRL_OK;
static time_ctrl_err_t s_read_err = TIME_CTRL_ERR_NOT_SET;

/**********************************************************************************************************************
 Private (static) function prototypes
 *********************************************************************************************************************/
static uint32_t time_cache_now_ms(void);
static void     time_cache_publish(bool valid, const time_ctrl_time_t *p_time);
static void     time_cache_task(INT stacd, void *exinf);
static bool     time_cache_apply_request(void);

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

bool time_cache_init(void)
{
    ID tskid;

    if (s_tskid > 0) {
        return true;
    }

    s_pub_valid = false;
    s_pub_tick  = time_cache_now_ms();

    /* The worker may run immediately in tk_sta_tsk(), so publish the flag ID
     * first. There are no requests until init returns successfully. */
    {
        T_CFLG cflg = { .exinf = NULL, .flgatr = TA_TFIFO, .iflgptn = 0U };
        s_flgid = tk_cre_flg(&cflg);
    }
    if (s_flgid <= E_OK) {
        s_flgid = 0;
        return false;
    }

    {
        T_CTSK ctsk = {
            .exinf   = NULL,
            .tskatr  = TA_HLNG | TA_RNG3,
            .task    = time_cache_task,
            .itskpri = TIME_CACHE_TASK_PRI,
            .stksz   = TIME_CACHE_TASK_STKSZ,
            .bufptr  = NULL,        /* USE_IMALLOC = 1 によりスタックは自動確保 */
        };

        tskid = tk_cre_tsk(&ctsk);
    }

    if (tskid <= E_OK) {
        (void)tk_del_flg(s_flgid);
        s_flgid = 0;
        return false;
    }

    if (E_OK != tk_sta_tsk(tskid, 0)) {
        (void)tk_del_tsk(tskid);
        (void)tk_del_flg(s_flgid);
        s_flgid = 0;
        return false;
    }

    /* 起動したタスクは `TIME_CACHE_TASK_PRI`(12) で、呼び出し元の `lvgl_task`(14) より
     * 高優先度なので、この代入より先に走って publish しうる。`s_pub_*` は
     * `tk_dis_dsp()` 区間で守られており、`s_tskid` は本関数しか触らないので問題ない。 */
    s_tskid = tskid;
    return true;
}

bool time_cache_set_request(const time_ctrl_time_t *p_time, uint32_t *p_seq)
{
    uint32_t seq;
    if ((NULL == p_time) || (NULL == p_seq) || (s_tskid <= 0) || (s_flgid <= 0)) {
        return false;
    }
    if (E_OK != tk_dis_dsp()) {
        return false;
    }
    if (s_set_pending) {
        (void)tk_ena_dsp();
        return false;
    }
    s_set_time = *p_time;
    s_set_seq++;
    if (0U == s_set_seq) {
        s_set_seq = 1U; /* zero is never a request token */
    }
    seq = s_set_seq;
    s_set_pending = true;
    (void)tk_ena_dsp();
    *p_seq = seq;
    /* Notification is outside the publication critical section. If it fails,
     * the periodic wake still consumes the request; do not report rejection
     * after publishing an operation which can take effect later. */
    (void)tk_set_flg(s_flgid, TIME_CACHE_FLG_SET);
    return true;
}

bool time_cache_set_result(uint32_t seq, time_ctrl_err_t *p_set_err,
                           time_ctrl_err_t *p_read_err)
{
    if ((0U == seq) || (NULL == p_set_err) || (NULL == p_read_err)) {
        return false;
    }
    if (E_OK != tk_dis_dsp()) {
        return false;
    }
    if (s_set_pending || (seq != s_set_seq)) {
        (void)tk_ena_dsp();
        return false;
    }
    *p_set_err = s_set_err;
    *p_read_err = s_read_err;
    (void)tk_ena_dsp();
    return true;
}

bool time_cache_get(time_ctrl_time_t *p_out)
{
    time_ctrl_time_t snap;
    bool             valid;
    uint32_t         tick;

    if (NULL == p_out) {
        return false;
    }

    memset(&snap, 0, sizeof(snap));

    if (E_OK != tk_dis_dsp()) {
        /* ディスパッチ禁止にできない＝ISR から呼ばれている。契約違反なので何も返さない。 */
        return false;
    }
    snap  = s_pub_time;         /* 3 つを同一区間で sample する（分けてはならない） */
    valid = s_pub_valid;
    tick  = s_pub_tick;
    (void)tk_ena_dsp();

    if (!valid) {
        return false;
    }

    /* 符号なし減算なので `tk_get_otm()` 下位 32bit の巻き戻り（約 49.7 日）をまたいでも正しい。 */
    if ((time_cache_now_ms() - tick) > TIME_CACHE_STALE_MS) {
        /* 取得遅延やRTC待ちなどで古くなった値は「時刻不明」として扱う。 */
        return false;
    }

    *p_out = snap;
    return true;
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * 現在の動作時間を ms で得る
 *
 * @details `camera_display.c:146-151` と同じ作法（`tk_get_otm()` の下位 32bit）。
 *
 * @return 動作時間 [ms]（約 49.7 日で巻き戻る）
 */
static uint32_t time_cache_now_ms(void)
{
    SYSTIM now = {0, 0};

    (void)tk_get_otm(&now);
    return (uint32_t)now.lo;
}

/**
 * スナップショットを publish する
 *
 * @param valid  `p_time` が有効か（ポーリングが成功したか）
 * @param p_time 取得したカレンダー時刻。`valid` が false なら参照しない
 */
static void time_cache_publish(bool valid, const time_ctrl_time_t *p_time)
{
    /* `tk_get_otm()` はディスパッチ禁止区間の外で採る。区間内では構造体コピーだけを行う。 */
    uint32_t now_ms = time_cache_now_ms();

    if (E_OK != tk_dis_dsp()) {
        return;
    }
    if (valid) {
        s_pub_time = *p_time;   /* 3 つを同一区間で publish する（分けてはならない） */
    }
    s_pub_valid = valid;
    s_pub_tick  = now_ms;
    (void)tk_ena_dsp();
}

/**
 * 時刻キャッシュのポーリングタスク本体
 *
 * @details 設定要求を優先し、要求がないときは1 Hzで現在時刻を取得する。
 *
 * @warning **この関数は戻らないことがある。** サブクロックが停止すると
 *          `time_ctrl_get()` の中の `FSP_HARDWARE_REGISTER_WAIT`（タイムアウト無し）
 *          から抜けられなくなる（`time_cache.h` 参照）。本モジュールはそれを前提に、
 *          LVGLロック内でRTCを待たないために存在する。ただし高優先度の
 *          busy waitによる低優先度タスクの飢餓までは防げない。
 *
 * @param stacd タスク起動コード（未使用）
 * @param exinf 拡張情報（未使用）
 */
static void time_cache_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    while (1) {
        UINT pattern;
        if (!time_cache_apply_request()) {
            time_ctrl_time_t now = {0};
            bool ok = (TIME_CTRL_OK == time_ctrl_get(&now));
            time_cache_publish(ok, &now);
        }
        (void)tk_wai_flg(s_flgid, TIME_CACHE_FLG_SET, TWF_ORW | TWF_BITCLR,
                         &pattern, (TMO)TIME_CACHE_POLL_PERIOD_MS);
    }
}

/* Only the worker calls this function. No resource operation occurs inside a
 * dispatch-disabled section. Completion follows publication of readback, so
 * a UI that observes success can immediately refresh the status bar. */
static bool time_cache_apply_request(void)
{
    time_ctrl_time_t want;
    time_ctrl_time_t now = {0};
    time_ctrl_err_t set_err;
    time_ctrl_err_t read_err = TIME_CTRL_ERR_NOT_SET;
    if (E_OK != tk_dis_dsp()) {
        return false;
    }
    if (!s_set_pending) {
        (void)tk_ena_dsp();
        return false;
    }
    want = s_set_time;
    (void)tk_ena_dsp();

    set_err = time_ctrl_set(&want);
    if (TIME_CTRL_OK == set_err) {
        /* Invalidate the old snapshot before the possibly unbounded read.
         * Never present pre-write time as the confirmed post-write value. */
        time_cache_publish(false, NULL);
        read_err = time_ctrl_get(&now);
        time_cache_publish(TIME_CTRL_OK == read_err, &now);
    }
    if (E_OK != tk_dis_dsp()) {
        /* Contract violation: keep ownership rather than admit a retry. */
        return true;
    }
    s_set_err = set_err;
    s_read_err = read_err;
    s_set_pending = false;
    (void)tk_ena_dsp();
    return true;
}
