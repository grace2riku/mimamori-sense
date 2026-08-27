/**
 * @file time_ctrl.h
 * @brief 時刻管理モジュール（S-012-2 / Issue #212）
 * @details
 * RTC（`g_rtc` / `r_rtc`, サブクロック駆動）を正の時刻源とする時刻管理モジュール。
 * カレンダー時刻の取得・設定と、μT-Kernel システム時刻（`tk_get_utc()`）への同期を行う。
 *
 * 設計メモ: `doc/design/issue-212.md`
 *
 * ## タイムゾーン
 *
 * **JST 固定のローカル時刻**として扱う。UTC オフセットは管理しない。
 * RTC には JST の壁時計時刻をそのまま格納する。
 *
 * `time_ctrl_init()` / `time_ctrl_set()` の成功時に `tk_set_utc()` でシステム時刻を同期するため、
 * 他モジュールは `tk_get_utc()` だけで時刻を得られる（#213 の画面表示など）。ただし
 * **`tk_get_utc()` が返すのは「JST を UTC とみなした 1970-01-01 起点のミリ秒」**であり、
 * 真の UTC ではない。分解能はシステムタイマ周期の 10 ms
 * （`mtk3_bsp2/config/config.h:39` の `CNF_TIMER_PERIOD`）。
 * SysTick 由来のドリフトがあるため、**正の時刻源は常に RTC**（= `time_ctrl_get()`）とする。
 *
 * ## 実行コンテキスト制約
 *
 * - **ISR から呼んではならない。** 内部で RTC の carry 割り込みに依存する
 *   `R_RTC_CalendarTimeGet()` を呼ぶため（`ra/fsp/src/r_rtc/r_rtc.c:421-470`）。
 * - **割り込み禁止区間から呼んではならない。** carry ISR が走れないと桁上がりを検出できない。
 *   `tk_dis_dsp()`（タスクディスパッチ禁止のみ）は割り込みを止めないため問題ない。
 * - 書き手・読み手ともタスクのみ。本モジュール内部で `tk_dis_dsp()` / `tk_ena_dsp()` により
 *   RTC アクセスを直列化するので、呼び出し側での排他は不要。
 *
 * ## 所要時間
 *
 * `time_ctrl_get()` ≒ 0.1 ms / `time_ctrl_set()` ≒ 0.2 ms / `time_ctrl_init()` ≒ 0.5 ms（最悪）。
 * いずれもビジー待ちのみでタスクは待ち状態に入らない。
 */

#ifndef TIME_CTRL_H
#define TIME_CTRL_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** 設定可能な西暦の下限（RYRCNT が BCD 2 桁 ＋ オフセット 100 のため 2000-2099） */
#define TIME_CTRL_YEAR_MIN      (2000U)
/** 設定可能な西暦の上限 */
#define TIME_CTRL_YEAR_MAX      (2099U)

/** `time_ctrl_parse()` が期待する日付文字列長（"YYYY-MM-DD"） */
#define TIME_CTRL_DATE_STR_LEN  (10)
/** `time_ctrl_parse()` が期待する時刻文字列長（"hh:mm:ss"） */
#define TIME_CTRL_TIME_STR_LEN  (8)

/**********************************************************************************************************************
 Global Typedef definitions
 *********************************************************************************************************************/

/**
 * time_ctrl API の戻り値
 *
 * @note `TIME_CTRL_ERR_NOT_SET` と `TIME_CTRL_ERR_HW` は**呼び出し元の次の行動が違う**ため
 *       別の値としている（前者は `time set` を促す／後者は異常として扱う）。
 * @note `TIME_CTRL_ERR_HW` のうち「RTC API がエラーを返した」経路は、
 *       `BSP_CFG_PARAM_CHECKING_ENABLE = (0)` の本ビルドでは到達しない
 *       （`R_RTC_*` は常に `FSP_SUCCESS` を返す。詳細は `time_ctrl.c` 冒頭）。
 *       実際に返りうるのは「読み出し値が日時として不正」の経路のみ。
 */
typedef enum {
    TIME_CTRL_OK = 0,              /**< 成功 */
    TIME_CTRL_ERR_NOT_INIT,        /**< `time_ctrl_init()` が未実行、または失敗している */
    TIME_CTRL_ERR_NOT_SET,         /**< 時刻が未設定（RTC が計時していない）。`time_ctrl_set()` が必要 */
    TIME_CTRL_ERR_INVALID_ARG,     /**< 引数が NULL、または日時として不正 */
    TIME_CTRL_ERR_HW               /**< RTC アクセスに失敗、または読み出し値が日時として不正 */
} time_ctrl_err_t;

/**
 * カレンダー時刻
 *
 * @note FSP の `rtc_time_t`（= `struct tm`）と違い、`year` は西暦そのもの、`mon` は 1 起点。
 *       変換は本モジュール内部で行う。
 */
typedef struct {
    uint16_t year;      /**< 西暦（2000-2099） */
    uint8_t  mon;       /**< 月（1-12） */
    uint8_t  mday;      /**< 日（1-31、月とうるう年に応じた上限） */
    uint8_t  hour;      /**< 時（0-23） */
    uint8_t  min;       /**< 分（0-59） */
    uint8_t  sec;       /**< 秒（0-59） */
    uint8_t  wday;      /**< 曜日（0=日曜 … 6=土曜）。設定時は年月日から内部で算出する */
} time_ctrl_time_t;

/**
 * `time status` 表示用の内部状態スナップショット
 */
typedef struct {
    bool     initialized;       /**< `time_ctrl_init()` が成功している */
    bool     provisioned;       /**< RCR2.START==1 かつ RCR2.HR24==1（クロック源設定済み＋計時中） */
    bool     running;           /**< RCR2.START==1（計時中＝時刻設定済み） */
    bool     did_provision;     /**< `time_ctrl_init()` が `R_RTC_ClockSourceSet()` を実行した */
    bool     subclock_stopped;  /**< SOSCCR.SOSTP==1（サブクロック発振停止） */
    uint8_t  rcr1;              /**< RTC 生値: RCR1 */
    uint8_t  rcr2;              /**< RTC 生値: RCR2 */
    uint8_t  rcr4;              /**< RTC 生値: RCR4 */
    uint8_t  bcd_sec;           /**< RTC 生値: RSECCNT（BCD） */
    uint8_t  bcd_min;           /**< RTC 生値: RMINCNT（BCD） */
    uint8_t  bcd_hour;          /**< RTC 生値: RHRCNT（BCD） */
    uint8_t  bcd_wday;          /**< RTC 生値: RWKCNT */
    uint8_t  bcd_mday;          /**< RTC 生値: RDAYCNT（BCD） */
    uint8_t  bcd_mon;           /**< RTC 生値: RMONCNT（BCD） */
    uint8_t  bcd_year;          /**< RTC 生値: RYRCNT（BCD） */
    int32_t  last_err;          /**< 直近の RTC API の `fsp_err_t` */
} time_ctrl_status_t;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * 時刻管理モジュールを初期化する
 * @details
 * 1. `R_RTC_Open()` を実行する（`RTC_CFG_OPEN_SET_CLOCK_SOURCE = (0)` のためクロック源は触らない）
 * 2. **未プロビジョニングの場合のみ** `R_RTC_ClockSourceSet()` を実行する。
 *    判定は RCR2.START==1 && RCR2.HR24==1。プロビジョニング済みなら何もしないことで、
 *    VBATT 電池でバックアップされた計時を壊さない
 * 3. 既に時刻が設定されていれば RTC から読み出して `tk_set_utc()` へ同期する
 *
 * @warning `R_RTC_ClockSourceSet()` の内部の `FSP_HARDWARE_REGISTER_WAIT` には
 *          タイムアウトが無いため、**サブクロックが発振していない場合この関数は戻らない**
 *          （`ra/fsp/src/bsp/mcu/all/bsp_common.h:122`）。呼び出し位置は
 *          `ntshell_task` の `jlink_console_init()` 直後に固定している（設計メモ §6）。
 *
 * @note `ntshell_task` から 1 回だけ呼ぶこと。2 回目以降は `TIME_CTRL_OK` を返して何もしない。
 *
 * @retval TIME_CTRL_OK        初期化に成功した（時刻が未設定でも OK を返す）
 * @retval TIME_CTRL_ERR_HW    `R_RTC_Open()` が失敗した。
 *                             **本ビルドでは到達しない**（`R_RTC_Open()` は常に `FSP_SUCCESS`）
 */
time_ctrl_err_t time_ctrl_init(void);

/**
 * 現在時刻を取得する（RTC 直読）
 *
 * @param[out] p_time 取得したカレンダー時刻
 *
 * @retval TIME_CTRL_OK              成功
 * @retval TIME_CTRL_ERR_INVALID_ARG `p_time` が NULL
 * @retval TIME_CTRL_ERR_NOT_INIT    `time_ctrl_init()` が未実行／失敗
 * @retval TIME_CTRL_ERR_NOT_SET     時刻が未設定（RTC が計時していない）
 * @retval TIME_CTRL_ERR_HW          読み出し値が日時として不正。
 *                                   （RTC アクセス失敗の経路は本ビルドでは到達しない）
 */
time_ctrl_err_t time_ctrl_get(time_ctrl_time_t *p_time);

/**
 * 時刻を設定する
 * @details
 * 妥当性検証（西暦範囲・月ごとの日数・うるう年・時分秒範囲）に通ってから RTC に書き込む。
 * FSP のパラメータチェックは `BSP_CFG_PARAM_CHECKING_ENABLE = (0)` で無効なため
 * （`ra_cfg/fsp_cfg/bsp/bsp_cfg.h:33`）、検証は本関数の責務。
 * 成功時は `tk_set_utc()` でシステム時刻も同期する。
 *
 * @param[in] p_time 設定するカレンダー時刻。`wday` は**無視され**、年月日から内部で算出した
 *                   曜日が RTC に書き込まれる（FSP は曜日を導出しない: `r_rtc.c:392`）
 *
 * @retval TIME_CTRL_OK              成功
 * @retval TIME_CTRL_ERR_INVALID_ARG `p_time` が NULL、または日時として不正（RTC は変化しない）
 * @retval TIME_CTRL_ERR_NOT_INIT    `time_ctrl_init()` が未実行／失敗（RTC は変化しない）
 * @retval TIME_CTRL_ERR_HW          RTC アクセスに失敗した。**本ビルドでは到達しない**
 */
time_ctrl_err_t time_ctrl_set(const time_ctrl_time_t *p_time);

/**
 * "YYYY-MM-DD" と "hh:mm:ss" の 2 文字列をカレンダー時刻へ変換する
 * @details 書式・桁数・区切り文字を厳密に検証し、値の範囲（うるう年を含む）も検証する。
 *
 * @param[in]  p_date "YYYY-MM-DD" 形式の日付文字列
 * @param[in]  p_time_str "hh:mm:ss" 形式の時刻文字列
 * @param[out] p_out 変換結果（`wday` は算出して格納する）
 *
 * @retval TIME_CTRL_OK              成功
 * @retval TIME_CTRL_ERR_INVALID_ARG いずれかが NULL、書式不正、または値が範囲外
 */
time_ctrl_err_t time_ctrl_parse(const char *p_date, const char *p_time_str, time_ctrl_time_t *p_out);

/**
 * 内部状態と RTC 生値のスナップショットを取得する（`time status` 用）
 *
 * @param[out] p_status スナップショット格納先。NULL の場合は何もしない
 */
void time_ctrl_get_status(time_ctrl_status_t *p_status);

/**
 * 曜日番号を 3 文字の英略称に変換する
 *
 * @param wday 曜日（0=日曜 … 6=土曜）
 * @return "Sun".."Sat"。範囲外なら "???"
 */
const char *time_ctrl_wday_str(uint8_t wday);

#ifdef __cplusplus
}
#endif

#endif /* TIME_CTRL_H */
