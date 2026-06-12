/**
 * @file ntshell_thread_entry.c
 * @brief NT-Shell タスクエントリとUARTコールバック
 * @details
 * NT-Shell（Natural Tiny Shell）のタスクエントリ関数。
 * SCI UART経由で双方向コンソールを提供する。
 * UART I/Oコールバック（読み込み・書き込み）とコマンドディスパッチを実装する。
 *
 * R-004 / Issue #154（FreeRTOS -> uT-Kernel 3.0 移行）:
 *   本タスクは src/usermain.c の usermain() から tk_cre_tsk + tk_sta_tsk で
 *   生成・起動される（方式A: ra_gen/main.c の FreeRTOS スレッド生成経路は未到達）。
 *   - エントリ関数は uT-Kernel タスク形式 ntshell_task(INT stacd, void *exinf) へ移植。
 *   - FreeRTOS 依存（FreeRTOS.h/task.h, FSP_PARAMETER_NOT_USED, tskKERNEL_VERSION_NUMBER）を除去/置換。
 *   旧 FreeRTOS エントリ ntshell_thread_entry() は対応関係の追跡用に末尾へ残置（方式A では未使用）。
 *   詳細は doc/migration/mtk3-migration-guide.md 7.2。
 *
 * uT-Kernel タスク設定（usermain.c で生成）:
 *   Stack size: 4096 bytes（FreeRTOS 版と同等）
 *   Priority  : blink タスク(=10)より低優先度（数値大）
 */

#include <stdio.h>
#include <string.h>

#include "ntshell_thread.h"
#include "jlink_console.h"
#include "ntshell.h"
#include "usrcmd.h"
#include "fw_version.h"
#include "fsp_version.h"
#include "led_ctrl.h"
#include "port/dave2d_port.h"

#include <tk/tkernel.h>

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** NT-Shell書き込みバッファサイズ（NULL終端分を含む） */
#define NTSHELL_WRITE_BUF_SIZE  (256)

/** NT-Shellプロンプト文字列 */
#define NTSHELL_PROMPT_STR      "mimamori>"

/** 起動バナー書式バッファサイズ */
#define NTSHELL_BANNER_BUF_SIZE (256)

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * 起動バナーの表示
 * @details プロジェクト名、ファームウェアバージョン、ビルド日時、
 *          FSP・uT-Kernelバージョンをコンソールに表示する。
 */
static void ntshell_print_banner(void)
{
    char buf[NTSHELL_BANNER_BUF_SIZE];

    print_to_console("\r\n");
    print_to_console("======================================\r\n");

    snprintf(buf, sizeof(buf), " %s Console (%s)\r\n", FW_PROJECT_NAME, FW_BOARD_NAME);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), " FW Version : v%d.%d.%d\r\n",
             FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), " Built      : %s %s\r\n", __DATE__, __TIME__);
    print_to_console(buf);

    /* R-004: FreeRTOS バージョン表示を uT-Kernel 3.0 へ置換 */
    snprintf(buf, sizeof(buf), " FSP %s / uT-Kernel 3.0\r\n",
             FSP_VERSION_STRING);
    print_to_console(buf);

    print_to_console(" Type 'help' for available commands.\r\n");
    print_to_console("======================================\r\n");
}

/**
 * NT-Shell用シリアル読み込み関数
 * @param buf 読み込みバッファ
 * @param cnt 読み込みバイト数
 * @param extobj 拡張オブジェクト（未使用）
 * @return 実際に読み込んだバイト数
 */
static int ntshell_serial_read(char *buf, int cnt, void *extobj)
{
    (void)extobj;
    int i;

    for (i = 0; i < cnt; i++)
    {
        /* 1文字受信（ブロッキング） */
        buf[i] = input_from_console();
    }

    return i;
}

/**
 * NT-Shell用シリアル書き込み関数
 * @param buf 書き込みバッファ
 * @param cnt 書き込みバイト数
 * @param extobj 拡張オブジェクト（未使用）
 * @return 実際に書き込んだバイト数
 */
static int ntshell_serial_write(const char *buf, int cnt, void *extobj)
{
    (void)extobj;

    /*
     * print_to_consoleはNULL終端文字列を期待するため、
     * バッファをコピーしてNULL終端を追加する
     */
    char temp[NTSHELL_WRITE_BUF_SIZE];
    int len = (cnt < (NTSHELL_WRITE_BUF_SIZE - 1)) ? cnt : (NTSHELL_WRITE_BUF_SIZE - 1);
    memcpy(temp, buf, (size_t)len);
    temp[len] = '\0';

    print_to_console(temp);

    return len;
}

/**
 * コマンド処理コールバック
 * @details ユーザーがEnterキーを押した際にNT-Shellから呼ばれる。
 *          入力テキストをコマンドディスパッチャに渡す。
 * @param text 入力されたコマンド文字列
 * @param extobj 拡張オブジェクト（未使用）
 * @return 0: 成功
 */
static int ntshell_callback(const char *text, void *extobj)
{
    (void)extobj;

    usrcmd_execute(text);

    return 0;
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * NT-Shell タスク本体（uT-Kernel 3.0 / R-004）
 * @details usermain() から tk_cre_tsk + tk_sta_tsk で生成・起動される uT-Kernel タスク。
 *          SCI8（FSP UART）を初期化し、NT-Shell を起動する。
 *          ntshell_execute() は内部で無限ループするため、この関数は戻らない。
 *
 *  SCI8 一本化（migration guide 7.1/7.2）:
 *    起動バナー（usermain 序盤）までは T-Monitor（tm_printf, SCI8 直接レジスタ）を使う。
 *    本タスクが jlink_console_init()（R_SCI_B_UART_Open, channel=8）で SCI8 を FSP UART として
 *    開いた後は、SCI8 は jlink_console が専有する（以降 T-Monitor は使用しない）。
 *    UART 割り込み（TXI/RXI/TEI/ERI）の ELC->NVIC マッピング（IELSR）は usermain() 先頭で
 *    bsp_irq_cfg() を呼んで構成済み（方式A では SystemInit の bsp_irq_cfg() が未実行のため）。
 *
 * @param stacd タスク起動コード（未使用）
 * @param exinf 拡張情報（未使用）
 */
void ntshell_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    /* NT-Shellインスタンス（staticでスタック消費を削減） */
    static ntshell_t ntshell;

    /* SCI UARTドライバの初期化（以降 SCI8 は FSP UART = jlink_console が専有） */
    jlink_console_init();

    /* LED制御モジュールの初期化（uT-Kernel 周期ハンドラ方式。led_ctrl_init は冪等） */
    led_ctrl_init();

    /* 起動バナーの表示（プロジェクト名、バージョン、ビルド日時） */
    ntshell_print_banner();

    /* Dave2D GPU加速ステータスの表示 (S-004-3) */
    if (dave2d_port_is_available()) {
        print_to_console(" Dave2D GPU  : Enabled\r\n");
    } else {
        print_to_console(" Dave2D GPU  : NOT available\r\n");
    }

    /* NT-Shellの初期化 */
    ntshell_init(
        &ntshell,
        ntshell_serial_read,
        ntshell_serial_write,
        ntshell_callback,
        NULL
    );

    /* プロンプトの設定 */
    ntshell_set_prompt(&ntshell, NTSHELL_PROMPT_STR);

    /* NT-Shell実行（ntshell_executeは内部で無限ループする） */
    while (1)
    {
        ntshell_execute(&ntshell);
    }
}

/**
 * 旧 FreeRTOS スレッドエントリ（R-004 移行前の名残り。対応関係追跡用に残置）。
 *
 * 方式A（src/hal_warmstart.c の静的コンストラクタで uT-Kernel を起動し、
 * ra_gen/main.c の main()/vTaskStartScheduler() に到達しない）では、本関数は
 * 実行時には呼ばれない。しかし ra_gen/ntshell_thread.c（編集禁止）の
 * ntshell_thread_func() が本シンボルを参照し、その参照鎖は startup が参照する
 * main() から辿れるためリンク時には解決が必要。よって削除せず、実体を
 * uT-Kernel タスク ntshell_task() へ委譲する薄いラッパとして残す
 * （実行されないが、将来 FreeRTOS へ切り戻した場合も動作する）。
 *
 * @param pvParameters FreeRTOSタスクパラメータ（未使用）
 */
void ntshell_thread_entry(void *pvParameters)
{
    (void)pvParameters;
    /* uT-Kernel タスク本体へ委譲（stacd/exinf は未使用）。 */
    ntshell_task(0, NULL);
}
