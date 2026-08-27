/**
 * @file time_cmd.c
 * @brief NT-Shell "time" コマンド実装（S-012-2 / Issue #212）
 * @details
 * サポートするサブコマンド:
 *   time                            現在時刻を表示
 *   time set YYYY-MM-DD hh:mm:ss    時刻を設定
 *   time status                     時刻源・各フラグ・RTC 生値・システム時刻を表示
 *
 * 時刻は **JST 固定のローカル時刻**として扱う（設計メモ `doc/design/issue-212.md` §7）。
 *
 * 実行コンテキストは `ntshell_task`。`time_ctrl` 側が `tk_dis_dsp()` で RTC アクセスを
 * 直列化するため、本ファイルでの排他は不要。
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>

#include "ntlibc.h"
#include "jlink_console.h"
#include "cmd_utils.h"
#include "time_ctrl.h"
#include "time_cmd.h"

#include <tk/tkernel.h>

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** コンソール出力バッファサイズ */
#define TIME_CMD_BUF_SIZE   (128)

/** `time set` が必要とする引数の個数（"time" "set" "<date>" "<time>"） */
#define TIME_CMD_SET_ARGC   (4)

/**********************************************************************************************************************
 Private (static) functions prototypes
 *********************************************************************************************************************/

static int  time_cmd_show(void);
static int  time_cmd_set(int argc, char **argv);
static int  time_cmd_status(void);
static void time_cmd_usage(void);
static void time_cmd_print_err(time_ctrl_err_t err);

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * NT-Shell "time" コマンドハンドラ
 */
int usrcmd_time(int argc, char **argv)
{
    if (argc < 2) {
        return time_cmd_show();
    }

    if (ntlibc_strcmp(argv[1], "set") == 0) {
        return time_cmd_set(argc, argv);
    }
    if (ntlibc_strcmp(argv[1], "status") == 0) {
        return time_cmd_status();
    }

    time_cmd_usage();

    return CMD_ERR_USAGE;
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * 現在時刻を表示する
 * @return `CMD_OK` または `CMD_ERR_EXECUTE`
 */
static int time_cmd_show(void)
{
    char             buf[TIME_CMD_BUF_SIZE];
    time_ctrl_time_t now;
    time_ctrl_err_t  err = time_ctrl_get(&now);

    if (TIME_CTRL_OK != err) {
        time_cmd_print_err(err);
        return CMD_ERR_EXECUTE;
    }

    snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u (%s)\r\n",
             (unsigned int)now.year, (unsigned int)now.mon, (unsigned int)now.mday,
             (unsigned int)now.hour, (unsigned int)now.min, (unsigned int)now.sec,
             time_ctrl_wday_str(now.wday));
    print_to_console(buf);

    return CMD_OK;
}

/**
 * 時刻を設定する
 * @param argc 引数の個数
 * @param argv 引数文字列
 * @return `CMD_OK` / `CMD_ERR_USAGE` / `CMD_ERR_INVALID_ARG` / `CMD_ERR_EXECUTE`
 */
static int time_cmd_set(int argc, char **argv)
{
    char             buf[TIME_CMD_BUF_SIZE];
    time_ctrl_time_t req;
    time_ctrl_err_t  err;

    if (argc < TIME_CMD_SET_ARGC) {
        time_cmd_usage();
        return CMD_ERR_USAGE;
    }

    /* 書式・値の検証。ここで弾いた場合 RTC は一切変化しない。 */
    err = time_ctrl_parse(argv[2], argv[3], &req);
    if (TIME_CTRL_OK != err) {
        cmd_print_error("Invalid date/time. Expected: time set YYYY-MM-DD hh:mm:ss");
        snprintf(buf, sizeof(buf), "  (year must be %u-%u; leap years are checked)\r\n",
                 (unsigned int)TIME_CTRL_YEAR_MIN, (unsigned int)TIME_CTRL_YEAR_MAX);
        print_to_console(buf);
        return CMD_ERR_INVALID_ARG;
    }

    err = time_ctrl_set(&req);
    if (TIME_CTRL_OK != err) {
        time_cmd_print_err(err);
        return CMD_ERR_EXECUTE;
    }

    /* 書き込みが RTC に反映されたことを読み戻して確認・表示する */
    print_to_console("Time set. Now: ");

    return time_cmd_show();
}

/**
 * 時刻源・各フラグ・RTC 生値・システム時刻を表示する
 * @return `CMD_OK`
 */
static int time_cmd_status(void)
{
    char               buf[TIME_CMD_BUF_SIZE];
    time_ctrl_status_t st;
    time_ctrl_time_t   now;
    time_ctrl_err_t    err;
    SYSTIM             systim;
    uint32_t           utc_sec;
    uint32_t           utc_ms;

    time_ctrl_get_status(&st);

    print_to_console("--- time status ---\r\n");

    print_to_console(" Source        : RTC (r_rtc) / SUBCLK 32.768kHz\r\n");
    print_to_console(" Time zone     : JST fixed (local time, no UTC offset handling)\r\n");

    snprintf(buf, sizeof(buf), " Initialized   : %s\r\n", st.initialized ? "yes" : "no");
    print_to_console(buf);

    /* 「このブートでクロック源を設定したか」(did_provision) と
     * 「RTC が構成済みと判定されたか」(provisioned = START==1 && HR24==1) は別の事実。
     * 初回ブートで時刻を設定するまでは START==0 なので前者 true / 後者 false になり、
     * 1 行にまとめると "no (clock source set at this boot)" と矛盾して見える。
     * よって別々の行に分け、それぞれが単独で読めるようにする。 */
    if (st.initialized) {
        snprintf(buf, sizeof(buf), " Clock source  : %s\r\n",
                 st.did_provision ? "SET AT THIS BOOT (timekeeping was reset)"
                                  : "kept (already configured before this boot)");
    } else {
        /* R_RTC_Open() 失敗時。did_provision は false だが「維持した」わけではない。 */
        snprintf(buf, sizeof(buf), " Clock source  : (unknown - init failed)\r\n");
    }
    print_to_console(buf);

    snprintf(buf, sizeof(buf), " Provisioned   : %s\r\n",
             st.provisioned ? "yes (next boot keeps timekeeping)"
                            : "no  (next boot will set the clock source)");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), " Time set      : %s\r\n",
             st.running ? "yes (counting)"
                        : "no  -> run 'time set YYYY-MM-DD hh:mm:ss'");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), " Sub-clock     : %s (SOSCCR.SOSTP=%u)\r\n",
             st.subclock_stopped ? "STOPPED" : "running",
             (unsigned int)(st.subclock_stopped ? 1U : 0U));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), " Last FSP err  : %d\r\n", (int)st.last_err);
    print_to_console(buf);

    if (st.lock_busy) {
        /* 正常時は必ずロックが取れる。出た場合は保持タスクが RTC 内でハングしている。 */
        print_to_console(" RTC lock      : BUSY (snapshot taken without the lock)\r\n");
    }

    snprintf(buf, sizeof(buf), " RCR1/2/4      : 0x%02X / 0x%02X / 0x%02X"
                               "  (START=%u HR24=%u RCKSEL=%u)\r\n",
             (unsigned int)st.rcr1, (unsigned int)st.rcr2, (unsigned int)st.rcr4,
             (unsigned int)(st.rcr2 & 0x01U),
             (unsigned int)((st.rcr2 >> 6) & 0x01U),
             (unsigned int)(st.rcr4 & 0x01U));
    print_to_console(buf);

    snprintf(buf, sizeof(buf),
             " RTC raw (BCD) : year=%02X mon=%02X mday=%02X wday=%02X hour=%02X min=%02X sec=%02X\r\n",
             (unsigned int)st.bcd_year, (unsigned int)st.bcd_mon, (unsigned int)st.bcd_mday,
             (unsigned int)st.bcd_wday, (unsigned int)st.bcd_hour, (unsigned int)st.bcd_min,
             (unsigned int)st.bcd_sec);
    print_to_console(buf);

    err = time_ctrl_get(&now);
    if (TIME_CTRL_OK == err) {
        snprintf(buf, sizeof(buf), " Current time  : %04u-%02u-%02u %02u:%02u:%02u (%s)\r\n",
                 (unsigned int)now.year, (unsigned int)now.mon, (unsigned int)now.mday,
                 (unsigned int)now.hour, (unsigned int)now.min, (unsigned int)now.sec,
                 time_ctrl_wday_str(now.wday));
        print_to_console(buf);
    } else {
        print_to_console(" Current time  : (not available)\r\n");
    }

    /* システム時刻。tk_set_utc() で RTC に同期済みなら Current time とほぼ一致する。
     * 分解能は CNF_TIMER_PERIOD = 10 ms。 */
    if (E_OK == tk_get_utc(&systim)) {
        /* SYSTIM は 1970-01-01 起点の ms。2000-2099 の範囲では秒値は 32bit に収まる。 */
        uint64_t ms = ((uint64_t)(uint32_t)systim.hi << 32) | (uint64_t)systim.lo;

        utc_sec = (uint32_t)(ms / 1000ULL);
        utc_ms  = (uint32_t)(ms % 1000ULL);
        snprintf(buf, sizeof(buf), " System time   : %lu.%03lu s since 1970-01-01 (JST as UTC)\r\n",
                 (unsigned long)utc_sec, (unsigned long)utc_ms);
        print_to_console(buf);
    }

    return CMD_OK;
}

/**
 * 使い方を表示する
 */
static void time_cmd_usage(void)
{
    print_to_console("Usage:\r\n");
    print_to_console("  time                          Show current date/time\r\n");
    print_to_console("  time set YYYY-MM-DD hh:mm:ss  Set date/time (JST)\r\n");
    print_to_console("  time status                   Show clock source, flags and RTC raw values\r\n");
}

/**
 * `time_ctrl_err_t` に応じたエラーメッセージを表示する
 * @details 「未設定」と「HW 異常」はユーザの次の行動が違うため、別のメッセージにする。
 * @param err time_ctrl のエラーコード
 */
static void time_cmd_print_err(time_ctrl_err_t err)
{
    switch (err) {
        case TIME_CTRL_ERR_NOT_INIT:
            cmd_print_error("Time module is not initialized (RTC open failed).");
            break;
        case TIME_CTRL_ERR_NOT_SET:
            cmd_print_error("Time is not set. Run: time set YYYY-MM-DD hh:mm:ss");
            break;
        case TIME_CTRL_ERR_INVALID_ARG:
            cmd_print_error("Invalid argument.");
            break;
        case TIME_CTRL_ERR_HW:
            cmd_print_error("RTC access failed. Run 'time status' for details.");
            break;
        case TIME_CTRL_ERR_BUSY:
            /* 他タスクが RTC を保持したまま戻ってこない状態。サブクロック喪失が疑わしい。
             * 'time status' はロック無しでも状態を採取するので実行できる。 */
            cmd_print_error("RTC is busy (lock timeout). Run 'time status' for details.");
            break;
        default:
            break;
    }
}
