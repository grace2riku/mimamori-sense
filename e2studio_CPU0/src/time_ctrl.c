/**
 * @file time_ctrl.c
 * @brief 時刻管理モジュール実装（S-012-2 / Issue #212）
 * @details
 * 設計メモ: `doc/design/issue-212.md`
 *
 * ## 排他方針（設計メモ §5）
 *
 * RTC アクセス区間を `TA_INHERIT` 属性の**資源所有ミューテックス**で囲む。保護対象は
 * 「RTC レジスタ列 ＋ `g_rtc_ctrl`（`carry_isr_triggered` と carry IRQ の一時有効化・復元）」
 * という**資源**であり、要求の順序付けではない。したがって CLAUDE.md が
 * 「保持したまま呼ぶのが正しい」と定める資源所有ロックに該当し、
 * `i2c_bus0_lock()` / `i2c_bus0_unlock()`（`src/port/i2c_bus0.c:78-137`）と同じ類型・同じ属性とする。
 *
 * - 書き手・読み手とも**タスクのみ**（`ntshell_task`、#213 以降は `lvgl_task` が読み手に加わる）。
 *   ISR から `time_ctrl_*` を呼ぶ経路は作らない（ミューテックスは ISR から取れない）
 * - ミューテックスはディスパッチを止めないため、`R_RTC_CalendarTimeGet()` が依存する
 *   carry ISR（`r_rtc.c:1687`）はもちろん、他タスクも区間中に走り続ける
 * - ロック取得は `TIME_CTRL_LOCK_TIMEOUT_MS` でタイムアウトさせ、
 *   保持タスクがハングしても待ち側は `TIME_CTRL_ERR_BUSY` で復帰できるようにする
 *
 * ### なぜ `tk_dis_dsp()` ではないか（PR #216 レビュー指摘 P1）
 *
 * FSP の RTC API は `FSP_HARDWARE_REGISTER_WAIT`（タイムアウト無し、`bsp_common.h:122`）で
 * カウントソース同期の完了を待つ。サブクロックが発振していなければ**戻らない**:
 *
 * | API | 無制限待ちの箇所 |
 * |---|---|
 * | `R_RTC_ClockSourceSet()` | `r_rtc.c:1043,1057,1091,1100,1109`（START/RESET/CNTMD/RCR1/HR24） |
 * | `R_RTC_CalendarTimeSet()` | `r_rtc.c:1043`（START ×2）、`:1589,1593,1607,1625`（誤差調整） |
 * | `R_RTC_CalendarTimeGet()` | `r_rtc.c:1169,1176`（RCR1 の CIE 更新） |
 *
 * これを `tk_dis_dsp()` で囲むと、ハング時にディスパッチが戻らず
 * **CPU0 の全タスク（camera / lvgl / audio / blink）が巻き添えで停止する**。
 * ミューテックスなら被害は呼び出しタスク 1 本に閉じ込められ、他タスクは
 * タイムアウトで `TIME_CTRL_ERR_BUSY` を受け取って動作を継続できる。
 * なお、どちらにしてもサブクロック喪失時に RTC が使えないことは変わらない
 * （FSP にタイムアウトが無い以上、モジュール側では回避できない）。
 *
 * `tk_dis_dsp()` を使うのはミューテックス生成の二重生成防止のみで、
 * その区間に RTC アクセスは含めない（`time_lock_init()`）。
 *
 * `tk_set_utc()` はロックの**外**で呼ぶ。同期元の値はローカル変数なので競合しない。
 *
 * ## FSP のパラメータチェックは無効（設計メモ §2）
 *
 * `BSP_CFG_PARAM_CHECKING_ENABLE = (0)`（`ra_cfg/fsp_cfg/bsp/bsp_cfg.h:33`）のため
 * `r_rtc.c` の `#if RTC_CFG_PARAM_CHECKING_ENABLE` ブロックは全てコンパイル時に消える。
 * NULL チェック・オープン済みチェック・日時の妥当性検証は本モジュールの責務。
 *
 * ### 帰結: 本ビルドでは RTC API は失敗しない
 *
 * `R_RTC_Open()` / `R_RTC_CalendarTimeSet()` / `R_RTC_CalendarTimeGet()` はいずれも
 * `fsp_err_t err = FSP_SUCCESS;` から `return err;` までの間に `err` を書き換える文が
 * `#if RTC_CFG_PARAM_CHECKING_ENABLE` の外に存在しない（`r_rtc.c:196-253`, `:362-407`,
 * `:421-470`）。つまり**本ビルドでは常に `FSP_SUCCESS` を返す**。
 *
 * よって本ファイルの `FSP_SUCCESS != err` 分岐は、FSP 設定が将来変わった場合に備えた
 * **防御的コードであり、現状は到達しない**。実際に `TIME_CTRL_ERR_HW` を返しうるのは、
 * 読み出した RTC の値が壊れていた場合（`FSP_ERR_INVALID_DATA` を `s_last_err` に
 * 記録する 2 経路）だけである:
 *
 * 1. `rtc_raw_bcd_is_valid()` — 生レジスタのニブルが 0-9 に収まっていない
 * 2. `time_is_valid()` — BCD としては妥当だが日時として成立しない（2 月 30 日など）
 *
 * 1 が必要なのは、FSP の `rtc_bcd_to_dec()` がニブルを検査しないため、
 * `RSECCNT = 0x1A` のような壊れ方が「20 秒」として 2 の範囲検査を通り抜けるからである
 * （`r_rtc.c:1725-1729`。PR #216 レビュー指摘 P2）。
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <string.h>

#include "hal_data.h"
#include "time_ctrl.h"

#include <tk/tkernel.h>

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** `struct tm` の `tm_year` は 1900 起点 */
#define TIME_CTRL_TM_YEAR_BASE      (1900)

/** 1 日の秒数 */
#define TIME_CTRL_SECS_PER_DAY      (86400LL)

/** 1970-01-01 を 0 とする通日から曜日を求めるための補正（1970-01-01 は木曜 = 4） */
#define TIME_CTRL_EPOCH_WDAY        (4)

/** RHRCNT の時フィールドマスク。FSP の RTC_RHRCNT_HOUR_MASK（`r_rtc.c:62`）と同値。
 * FSP がデコード時に掛けるマスクと検査対象を一致させるために使う。 */
#define TIME_CTRL_RHRCNT_HOUR_MASK  (0x3FU)

/** RTC ロックの取得タイムアウト [ms]
 * @details 正常時の最長保持は `time_ctrl_init()` の約 0.5 ms なので、通常は待たずに取れる。
 * サブクロック喪失で保持タスクがハングした場合に、待ち側を確実に解放するための値。 */
#define TIME_CTRL_LOCK_TIMEOUT_MS   (1000)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/** `time_ctrl_init()` が成功したか。書き手・読み手とも `ntshell_task`（将来 `lvgl_task` が読み手） */
static bool      s_initialized   = false;

/** `time_ctrl_init()` が `R_RTC_ClockSourceSet()` を実行したか（`time status` 表示用） */
static bool      s_did_provision = false;

/** 直近の RTC API の戻り値（`time status` 表示用） */
static fsp_err_t s_last_err      = FSP_SUCCESS;

/** RTC 資源所有ミューテックス。0 以下なら未生成 */
static ID        s_mtxid         = 0;

/**********************************************************************************************************************
 Private (static) functions prototypes
 *********************************************************************************************************************/

static bool     time_lock_init(void);
static bool     time_lock(void);
static void     time_unlock(void);
static bool     rtc_is_provisioned(void);
static bool     rtc_is_running(void);
static bool     rtc_raw_bcd_is_valid(void);
static bool     bcd_byte_is_valid(uint8_t value, uint8_t max_high);
static bool     time_is_valid(const time_ctrl_time_t *p_time);
static uint8_t  days_in_month(uint16_t year, uint8_t mon);
static bool     is_leap_year(uint16_t year);
static int64_t  days_from_civil(uint16_t year, uint8_t mon, uint8_t mday);
static uint8_t  wday_from_civil(uint16_t year, uint8_t mon, uint8_t mday);
static void     sync_system_time(const time_ctrl_time_t *p_time);
static bool     parse_fixed_uint(const char *p_str, int digits, uint32_t *p_out);

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * 時刻管理モジュールを初期化する
 */
time_ctrl_err_t time_ctrl_init(void)
{
    fsp_err_t        err;
    bool             time_available;
    rtc_time_t       raw;
    time_ctrl_time_t now;

    if (s_initialized) {
        /* 冪等: 2 回目以降は何もしない */
        return TIME_CTRL_OK;
    }

    memset(&raw, 0, sizeof(raw));

    if (!time_lock_init()) {
        s_last_err = FSP_ERR_INTERNAL;
        return TIME_CTRL_ERR_HW;
    }
    if (!time_lock()) {
        return TIME_CTRL_ERR_BUSY;
    }
    {
        /* RTC_CFG_OPEN_SET_CLOCK_SOURCE = (0)（ra_cfg/fsp_cfg/r_rtc_cfg.h:9）のため、
         * R_RTC_Open() は r_rtc_set_clock_source() を呼ばない（r_rtc.c:240-245）。
         * 実体は R_BSP_IrqCfg() のみでレジスタ待ちが無く、電池バックアップ中の計時も壊さない。 */
        err        = R_RTC_Open(&g_rtc_ctrl, &g_rtc_cfg);
        s_last_err = err;

        if (FSP_SUCCESS == err) {
            /* 未プロビジョニングのときだけクロック源を設定する。
             * R_RTC_ClockSourceSet() は無条件に START をクリアしてソフトウェアリセットを
             * 実行する（r_rtc.c:1075,1094）ため、プロビジョニング済みで呼ぶと
             * 電池でバックアップされた計時を壊す。 */
            if (!rtc_is_provisioned()) {
                /* 戻り値は捨てる: パラメータチェック無効時は常に FSP_SUCCESS
                 * （r_rtc.c:331-350 に err を書き換える文が #if の外に無い）。 */
                (void)R_RTC_ClockSourceSet(&g_rtc_ctrl);
                s_did_provision = true;
            }

            s_initialized = true;
        }

        /* 計時中なら現在時刻を読み出す（同期は区間の外で行う） */
        time_available = s_initialized && rtc_is_running();
        if (time_available) {
            err        = R_RTC_CalendarTimeGet(&g_rtc_ctrl, &raw);
            s_last_err = err;
            if (FSP_SUCCESS != err) {
                time_available = false;
            } else if (!rtc_raw_bcd_is_valid()) {
                /* 生レジスタが BCD として壊れている。デコード後の値は範囲内に
                 * 見えることがあるので、ここで弾かないと誤った時刻を
                 * tk_set_utc() に流し込むことになる。 */
                s_last_err     = FSP_ERR_INVALID_DATA;
                time_available = false;
            } else {
                /* 妥当。区間の外で tk_set_utc() へ同期する */
            }
        }
    }
    time_unlock();

    if (!s_initialized) {
        return TIME_CTRL_ERR_HW;
    }

    if (time_available) {
        now.year = (uint16_t)(raw.tm_year + TIME_CTRL_TM_YEAR_BASE);
        now.mon  = (uint8_t)(raw.tm_mon + 1);
        now.mday = (uint8_t)raw.tm_mday;
        now.hour = (uint8_t)raw.tm_hour;
        now.min  = (uint8_t)raw.tm_min;
        now.sec  = (uint8_t)raw.tm_sec;
        now.wday = (uint8_t)raw.tm_wday;

        if (time_is_valid(&now)) {
            sync_system_time(&now);
        } else {
            /* 計時中なのに日時として不正 = レジスタ内容が壊れている */
            s_last_err = FSP_ERR_INVALID_DATA;
        }
    }

    return TIME_CTRL_OK;
}

/**
 * 現在時刻を取得する（RTC 直読）
 */
time_ctrl_err_t time_ctrl_get(time_ctrl_time_t *p_time)
{
    fsp_err_t  err = FSP_SUCCESS;
    rtc_time_t raw;
    bool       running;

    if (NULL == p_time) {
        return TIME_CTRL_ERR_INVALID_ARG;
    }

    memset(&raw, 0, sizeof(raw));

    /* ロックを取る前に見る。未初期化ならミューテックスも存在しない。 */
    if (!s_initialized) {
        return TIME_CTRL_ERR_NOT_INIT;
    }
    if (!time_lock()) {
        return TIME_CTRL_ERR_BUSY;
    }
    {
        running = rtc_is_running();

        if (running) {
            err        = R_RTC_CalendarTimeGet(&g_rtc_ctrl, &raw);
            s_last_err = err;

            if ((FSP_SUCCESS == err) && !rtc_raw_bcd_is_valid()) {
                /* 生レジスタが BCD として壊れている。デコード後の範囲検査
                 * （time_is_valid()）では捕まらない壊れ方があるため、ここで弾く。 */
                err        = FSP_ERR_INVALID_DATA;
                s_last_err = err;
            }
        }
    }
    time_unlock();

    if (!running) {
        /* RCR2.START == 0。R_RTC_CalendarTimeSet() 以外に START を 1 にする経路は無い
         * （r_rtc.c:404）ので、これは「時刻が一度も設定されていない」を意味する。 */
        return TIME_CTRL_ERR_NOT_SET;
    }
    if (FSP_SUCCESS != err) {
        return TIME_CTRL_ERR_HW;
    }

    p_time->year = (uint16_t)(raw.tm_year + TIME_CTRL_TM_YEAR_BASE);
    p_time->mon  = (uint8_t)(raw.tm_mon + 1);
    p_time->mday = (uint8_t)raw.tm_mday;
    p_time->hour = (uint8_t)raw.tm_hour;
    p_time->min  = (uint8_t)raw.tm_min;
    p_time->sec  = (uint8_t)raw.tm_sec;
    p_time->wday = (uint8_t)raw.tm_wday;

    if (!time_is_valid(p_time)) {
        /* 生値は BCD として妥当だが、日時として成立しない（2 月 30 日・19 月など
         * 桁の組み合わせの不正）。「未設定」ではなく異常として扱う。
         * BCD として壊れているケースは rtc_raw_bcd_is_valid() が上で弾いている。 */
        s_last_err = FSP_ERR_INVALID_DATA;
        return TIME_CTRL_ERR_HW;
    }

    return TIME_CTRL_OK;
}

/**
 * 時刻を設定する
 */
time_ctrl_err_t time_ctrl_set(const time_ctrl_time_t *p_time)
{
    fsp_err_t        err;
    rtc_time_t       raw;
    time_ctrl_time_t applied;

    if (NULL == p_time) {
        return TIME_CTRL_ERR_INVALID_ARG;
    }
    if (!time_is_valid(p_time)) {
        /* FSP の r_rtc_time_and_date_validate() はコンパイル時に消えているため、
         * ここで弾かないと 2 月 30 日がそのまま BCD レジスタに書かれる。 */
        return TIME_CTRL_ERR_INVALID_ARG;
    }

    applied      = *p_time;
    /* FSP は曜日を導出せず RWKCNT にそのまま書く（r_rtc.c:392）ので、年月日から算出する。 */
    applied.wday = wday_from_civil(applied.year, applied.mon, applied.mday);

    memset(&raw, 0, sizeof(raw));
    raw.tm_sec  = (int)applied.sec;
    raw.tm_min  = (int)applied.min;
    raw.tm_hour = (int)applied.hour;
    raw.tm_wday = (int)applied.wday;
    raw.tm_mday = (int)applied.mday;
    raw.tm_mon  = (int)applied.mon - 1;                                     /* tm_mon は 0 起点 */
    raw.tm_year = (int)applied.year - TIME_CTRL_TM_YEAR_BASE;               /* tm_year は 1900 起点 */

    /* ロックを取る前に見る。未初期化ならミューテックスも存在しない。 */
    if (!s_initialized) {
        return TIME_CTRL_ERR_NOT_INIT;
    }
    if (!time_lock()) {
        return TIME_CTRL_ERR_BUSY;
    }
    {
        err        = R_RTC_CalendarTimeSet(&g_rtc_ctrl, &raw);
        s_last_err = err;
    }
    time_unlock();

    if (FSP_SUCCESS != err) {
        return TIME_CTRL_ERR_HW;
    }

    sync_system_time(&applied);

    return TIME_CTRL_OK;
}

/**
 * "YYYY-MM-DD" と "hh:mm:ss" をカレンダー時刻へ変換する
 */
time_ctrl_err_t time_ctrl_parse(const char *p_date, const char *p_time_str, time_ctrl_time_t *p_out)
{
    uint32_t         year;
    uint32_t         mon;
    uint32_t         mday;
    uint32_t         hour;
    uint32_t         min;
    uint32_t         sec;
    time_ctrl_time_t work;

    if ((NULL == p_date) || (NULL == p_time_str) || (NULL == p_out)) {
        return TIME_CTRL_ERR_INVALID_ARG;
    }

    /* 書式を厳密に検証する: 長さ・区切り文字・数字であること */
    if ((strlen(p_date) != (size_t)TIME_CTRL_DATE_STR_LEN) ||
        (strlen(p_time_str) != (size_t)TIME_CTRL_TIME_STR_LEN)) {
        return TIME_CTRL_ERR_INVALID_ARG;
    }
    if ((p_date[4] != '-') || (p_date[7] != '-')) {
        return TIME_CTRL_ERR_INVALID_ARG;
    }
    if ((p_time_str[2] != ':') || (p_time_str[5] != ':')) {
        return TIME_CTRL_ERR_INVALID_ARG;
    }

    if (!parse_fixed_uint(&p_date[0], 4, &year) ||
        !parse_fixed_uint(&p_date[5], 2, &mon) ||
        !parse_fixed_uint(&p_date[8], 2, &mday) ||
        !parse_fixed_uint(&p_time_str[0], 2, &hour) ||
        !parse_fixed_uint(&p_time_str[3], 2, &min) ||
        !parse_fixed_uint(&p_time_str[6], 2, &sec)) {
        return TIME_CTRL_ERR_INVALID_ARG;
    }

    work.year = (uint16_t)year;
    work.mon  = (uint8_t)mon;
    work.mday = (uint8_t)mday;
    work.hour = (uint8_t)hour;
    work.min  = (uint8_t)min;
    work.sec  = (uint8_t)sec;
    work.wday = 0U;

    if (!time_is_valid(&work)) {
        return TIME_CTRL_ERR_INVALID_ARG;
    }

    work.wday = wday_from_civil(work.year, work.mon, work.mday);
    *p_out    = work;

    return TIME_CTRL_OK;
}

/**
 * 内部状態と RTC 生値のスナップショットを取得する
 */
void time_ctrl_get_status(time_ctrl_status_t *p_status)
{
    bool locked;

    if (NULL == p_status) {
        return;
    }

    /* 診断用なので、ロックが取れなくても採取して返す（ヘッダの @details 参照）。
     * ここでのレジスタ読みは 1 バイト単位でカウントソース同期の待ちを伴わないため、
     * ロック無しでもハングしない。 */
    locked = time_lock();
    {
        p_status->lock_busy     = !locked;
        p_status->initialized   = s_initialized;
        p_status->did_provision = s_did_provision;
        p_status->last_err      = (int32_t)s_last_err;

        p_status->rcr1 = R_RTC->RCR1;
        p_status->rcr2 = R_RTC->RCR2;
        p_status->rcr4 = R_RTC->RCR4;

        p_status->provisioned = rtc_is_provisioned();
        p_status->running     = rtc_is_running();

        /* サブクロック発振の停止ビット。SOSTP==1 なら SOSC は止まっている。 */
        p_status->subclock_stopped = (1U == R_SYSTEM->SOSCCR_b.SOSTP);

        /* BCD 生値。R_RTC_CalendarTimeGet() を通さない素のレジスタ内容を見せる。 */
        p_status->bcd_sec  = R_RTC->RSECCNT;
        p_status->bcd_min  = R_RTC->RMINCNT;
        p_status->bcd_hour = R_RTC->RHRCNT;
        p_status->bcd_wday = R_RTC->RWKCNT;
        p_status->bcd_mday = R_RTC->RDAYCNT;
        p_status->bcd_mon  = R_RTC->RMONCNT;
        p_status->bcd_year = (uint8_t)R_RTC->RYRCNT;
    }
    if (locked) {
        time_unlock();
    }
}

/**
 * 曜日番号を 3 文字の英略称に変換する
 */
const char *time_ctrl_wday_str(uint8_t wday)
{
    static const char *const names[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

    if (wday >= (uint8_t)(sizeof(names) / sizeof(names[0]))) {
        return "???";
    }

    return names[wday];
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * RTC 資源所有ミューテックスを生成する（冪等）
 * @details
 * 生成そのものは `tk_dis_dsp()` で囲って二重生成を防ぐ。この区間に RTC アクセスは
 * 含めない（`tk_cre_mtx()` はビジー待ちのみでブロックしない）ため、
 * ファイル冒頭で述べたディスパッチ禁止の問題は生じない。
 * 生成手順・属性は `i2c_bus0_sync_init()`（`src/port/i2c_bus0.c:78-100`）に合わせる。
 *
 * @return true なら利用可能なミューテックスがある
 */
static bool time_lock_init(void)
{
    if (s_mtxid > 0) {
        return true;
    }

    if (E_OK != tk_dis_dsp()) {
        return false;
    }

    if (s_mtxid <= 0) {                 /* ディスパッチ禁止下で再確認する */
        T_CMTX cmtx = {
            .exinf  = NULL,
            .mtxatr = TA_INHERIT,       /* 優先度継承。i2c_bus0 / lv_os_mtkernel と同じ */
        };

        ID mtxid = tk_cre_mtx(&cmtx);
        if (mtxid > E_OK) {
            s_mtxid = mtxid;
        }
    }

    (void)tk_ena_dsp();

    return (s_mtxid > 0);
}

/**
 * RTC 資源をロックする
 * @details タイムアウト付きで待つ。保持タスクがサブクロック喪失でハングしても、
 * 待ち側は `TIME_CTRL_LOCK_TIMEOUT_MS` 後に復帰して自タスクの処理を続けられる。
 *
 * @return true ならロックを取得した（`time_unlock()` が必要）
 */
static bool time_lock(void)
{
    if (s_mtxid <= 0) {
        return false;
    }

    return (E_OK == tk_loc_mtx(s_mtxid, (TMO)TIME_CTRL_LOCK_TIMEOUT_MS));
}

/**
 * RTC 資源のロックを解放する
 */
static void time_unlock(void)
{
    if (s_mtxid > 0) {
        (void)tk_unl_mtx(s_mtxid);
    }
}

/**
 * RTC がプロビジョニング済みかを判定する
 * @details
 * `RCR2.START == 1 && RCR2.HR24 == 1` をもって「本 FW がクロック源を設定し、
 * かつ時刻を設定して計時中」と判定する。根拠:
 *  - `START` を 1 にするのは `r_rtc.c` 全体で `R_RTC_CalendarTimeSet()` の `:404` の 1 箇所のみ
 *    （他 3 箇所 `:282` `:382` `:1075` はすべて `0U`）
 *  - `HR24` を 1 にするのは `r_rtc_set_clock_source()` の `:1104` のみ
 *  - VBATT 喪失後は RCR2 = 0x00 となり両ビットが 0 になる
 *
 * `RCR4`(RCKSEL) は判定に使えない。`RTC_CLOCK_SOURCE_SUBCLK == 0`（`r_rtc_api.h:72`）で
 * リセット値と同値であるうえ、`R_BSP_Init_RTC()` が起動のたびに `R_RTC->RCR4 = 0` を書く
 * （`bsp_clocks.c:3381`。本プロジェクトは `BSP_PRV_LOCO_USED = 0`）。
 * なお同関数の RCR2 リセット部は `#if !BSP_CFG_RTC_USED` の内側にあり、
 * `BSP_CFG_RTC_USED = (1)`（`ra_cfg/fsp_cfg/bsp/bsp_cfg.h:24`）のため
 * コンパイル時に消える。つまり **BSP は RCR2 を触らない**。
 *
 * @return true ならプロビジョニング済み
 */
static bool rtc_is_provisioned(void)
{
    return (1U == R_RTC->RCR2_b.START) && (1U == R_RTC->RCR2_b.HR24);
}

/**
 * RTC が計時中か（＝時刻が設定済みか）を判定する
 * @return true なら計時中
 */
static bool rtc_is_running(void)
{
    return (1U == R_RTC->RCR2_b.START);
}

/**
 * RTC カウンタの生値が BCD として妥当かを判定する（PR #216 レビュー指摘 P2）
 * @details
 * FSP の `rtc_bcd_to_dec()` は `(上位ニブル) * 10 + (下位ニブル)` を計算するだけで、
 * ニブルが 0-9 に収まっているかを検査しない（`r_rtc.c:1725-1729`）。
 * このため下位ニブルが 0xA-0xF の壊れた値が、範囲内の 10 進値として通ってしまう:
 *
 * | 生値 | `rtc_bcd_to_dec()` | `time_is_valid()` |
 * |---|---|---|
 * | `RSECCNT = 0x1A` | 1×10 + 10 = **20** | 20 ≤ 59 なので**通る** |
 * | `RSECCNT = 0x20` | 2×10 + 0 = 20 | 通る（正当な値） |
 *
 * デコード後の 20 からは両者を区別できないので、生レジスタを直接見る必要がある。
 * `time_is_valid()`（デコード後の意味的な妥当性）とは相補的な検査であり、両方を行う。
 *
 * 検査対象は **FSP が実際にデコードに使うバイトそのもの**とする
 * （`r_rtc.c:449-459`。`RHRCNT` だけは FSP が `RTC_RHRCNT_HOUR_MASK`(0x3F) を掛けるので
 * こちらも同じマスクを掛ける）。ビットフィールドアクセサを使うと予約ビットが落ちてしまい、
 * FSP が見ている値と検査対象がずれる。例えば `RWKCNT = 0x08` は
 * `DAYW`(bit2:0) では 0 に見えるが、FSP は 8 としてデコードする。
 *
 * @note 本関数の読みは `R_RTC_CalendarTimeGet()` の読みとは別の瞬間になるが、
 *       健全な RTC はどの瞬間でも各カウンタが妥当な BCD なので誤検出しない。
 *       桁上がりによるフィールド間の不整合は `R_RTC_CalendarTimeGet()` 側の
 *       carry 再読み込みループが担当する（`r_rtc.c:445-460`）。
 *
 * @return true なら全カウンタが BCD として妥当
 */
static bool rtc_raw_bcd_is_valid(void)
{
    /* 秒 0-59: 上位 0-5 / 下位 0-9。予約ビット bit7 が立てば上位は 8 以上になり弾かれる */
    if (!bcd_byte_is_valid(R_RTC->RSECCNT, 5U)) {
        return false;
    }
    /* 分 0-59 */
    if (!bcd_byte_is_valid(R_RTC->RMINCNT, 5U)) {
        return false;
    }
    /* 時 0-23（24 時間モード）。FSP と同じく 0x3F でマスクしてから見る */
    if (!bcd_byte_is_valid((uint8_t)(R_RTC->RHRCNT & TIME_CTRL_RHRCNT_HOUR_MASK), 2U)) {
        return false;
    }
    /* 曜日 0-6。BCD ではなく 3bit のバイナリなので、バイト全体を直接比較する */
    if (R_RTC->RWKCNT > 6U) {
        return false;
    }
    /* 日 1-31: 上位 0-3 / 下位 0-9 */
    if (!bcd_byte_is_valid(R_RTC->RDAYCNT, 3U)) {
        return false;
    }
    /* 月 1-12: 上位 0-1 / 下位 0-9 */
    if (!bcd_byte_is_valid(R_RTC->RMONCNT, 1U)) {
        return false;
    }
    /* 年 00-99: 上位 0-9 / 下位 0-9 */
    if (!bcd_byte_is_valid((uint8_t)R_RTC->RYRCNT, 9U)) {
        return false;
    }

    /* 「39 日」「19 月」のような桁の組み合わせの不正は time_is_valid() が弾く */
    return true;
}

/**
 * BCD 1 バイトが妥当かを判定する
 * @param value    検査するレジスタ生値
 * @param max_high 上位ニブルの許容上限（下位ニブルの上限は常に 9）
 * @return true なら妥当な BCD
 */
static bool bcd_byte_is_valid(uint8_t value, uint8_t max_high)
{
    return (((value >> 4) <= max_high) && ((value & 0x0FU) <= 9U));
}

/**
 * カレンダー時刻の妥当性を検証する（うるう年を含む）
 * @param p_time 検証対象
 * @return true なら妥当
 */
static bool time_is_valid(const time_ctrl_time_t *p_time)
{
    if ((p_time->year < TIME_CTRL_YEAR_MIN) || (p_time->year > TIME_CTRL_YEAR_MAX)) {
        return false;
    }
    if ((p_time->mon < 1U) || (p_time->mon > 12U)) {
        return false;
    }
    if ((p_time->mday < 1U) || (p_time->mday > days_in_month(p_time->year, p_time->mon))) {
        return false;
    }
    if (p_time->hour > 23U) {
        return false;
    }
    if (p_time->min > 59U) {
        return false;
    }
    if (p_time->sec > 59U) {
        /* うるう秒は RTC が扱わないので 60 は不正とする */
        return false;
    }

    return true;
}

/**
 * うるう年判定
 * @param year 西暦
 * @return true ならうるう年
 */
static bool is_leap_year(uint16_t year)
{
    return ((0U == (year % 4U)) && (0U != (year % 100U))) || (0U == (year % 400U));
}

/**
 * 指定した年月の日数を返す
 * @param year 西暦
 * @param mon 月（1-12）
 * @return 日数。`mon` が範囲外なら 0
 */
static uint8_t days_in_month(uint16_t year, uint8_t mon)
{
    static const uint8_t days[] = { 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U };

    if ((mon < 1U) || (mon > 12U)) {
        return 0U;
    }
    if ((2U == mon) && is_leap_year(year)) {
        return 29U;
    }

    return days[mon - 1U];
}

/**
 * 1970-01-01 を 0 とする通日を求める
 * @details Howard Hinnant の `days_from_civil` アルゴリズム（proleptic Gregorian）。
 *          呼び出し前に `time_is_valid()` で検証済みであることを前提とする。
 * @param year 西暦
 * @param mon 月（1-12）
 * @param mday 日（1-31）
 * @return 1970-01-01 からの経過日数
 */
static int64_t days_from_civil(uint16_t year, uint8_t mon, uint8_t mday)
{
    int32_t  y   = (int32_t)year - ((mon <= 2U) ? 1 : 0);
    int32_t  era = ((y >= 0) ? y : (y - 399)) / 400;
    uint32_t yoe = (uint32_t)(y - (era * 400));                             /* [0, 399] */
    uint32_t doy = (uint32_t)((153 * ((int32_t)mon + ((mon > 2U) ? -3 : 9)) + 2) / 5) + mday - 1U;
    uint32_t doe = (yoe * 365U) + (yoe / 4U) - (yoe / 100U) + doy;          /* [0, 146096] */

    return ((int64_t)era * 146097LL) + (int64_t)doe - 719468LL;
}

/**
 * 年月日から曜日を求める
 * @param year 西暦
 * @param mon 月（1-12）
 * @param mday 日（1-31）
 * @return 曜日（0=日曜 … 6=土曜）
 */
static uint8_t wday_from_civil(uint16_t year, uint8_t mon, uint8_t mday)
{
    /* 対応範囲は 2000-2099 なので通日は必ず正。1970-01-01 は木曜（=4）。 */
    int64_t days = days_from_civil(year, mon, mday);

    return (uint8_t)((days + TIME_CTRL_EPOCH_WDAY) % 7LL);
}

/**
 * カレンダー時刻を μT-Kernel システム時刻へ同期する
 * @details
 * `tk_set_utc()` は 1970-01-01 起点のミリ秒（`SYSTIM`）を受け取る
 * （`mtk3_bsp2/mtkernel/kernel/tkernel/time_calls.c:35-43`）。
 * 本プロジェクトは JST 固定のローカル時刻を扱うため、ここで渡すのは
 * **「JST を UTC とみなした」ミリ秒**であり、真の UTC ではない（`time_ctrl.h` 参照）。
 *
 * @param p_time 同期する時刻（検証済みであること）
 */
static void sync_system_time(const time_ctrl_time_t *p_time)
{
    int64_t days = days_from_civil(p_time->year, p_time->mon, p_time->mday);
    int64_t secs = (days * TIME_CTRL_SECS_PER_DAY) +
                   ((int64_t)p_time->hour * 3600LL) +
                   ((int64_t)p_time->min * 60LL) +
                   (int64_t)p_time->sec;
    int64_t msec = secs * 1000LL;
    SYSTIM  systim;

    systim.hi = (W)((uint64_t)msec >> 32);
    systim.lo = (UW)((uint64_t)msec & 0xFFFFFFFFULL);

    (void)tk_set_utc(&systim);
}

/**
 * 固定桁数の 10 進数文字列を解析する
 * @details `digits` 桁ちょうどが全て '0'-'9' であることを要求する。
 *          符号・空白・前置記号は一切許可しない。
 *
 * @param[in]  p_str 解析対象の先頭
 * @param[in]  digits 桁数
 * @param[out] p_out 解析結果
 * @return true なら解析成功
 */
static bool parse_fixed_uint(const char *p_str, int digits, uint32_t *p_out)
{
    uint32_t value = 0U;

    for (int i = 0; i < digits; i++) {
        char c = p_str[i];

        if ((c < '0') || (c > '9')) {
            return false;
        }
        value = (value * 10U) + (uint32_t)(c - '0');
    }

    *p_out = value;

    return true;
}
