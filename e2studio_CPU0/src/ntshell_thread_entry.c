/**
 * @file ntshell_thread_entry.c
 * @brief NT-Shell FreeRTOSスレッドエントリとUARTコールバック
 * @details
 * NT-Shell（Natural Tiny Shell）のFreeRTOSスレッドエントリ関数。
 * SCI UART経由で双方向コンソールを提供する。
 * UART I/Oコールバック（読み込み・書き込み）とコマンドディスパッチを実装する。
 */

#include <string.h>

#include "ntshell_thread.h"
#include "jlink_console.h"
#include "ntshell.h"
#include "usrcmd.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** NT-Shell書き込みバッファサイズ（NULL終端分を含む） */
#define NTSHELL_WRITE_BUF_SIZE  (256)

/** NT-Shellプロンプト文字列 */
#define NTSHELL_PROMPT_STR      "mimamori>"

/** 起動バナー */
#define NTSHELL_BANNER \
    "\r\n" \
    "======================================\r\n" \
    " Mimamori-Sense Console\r\n" \
    " Type 'help' for available commands.\r\n" \
    "======================================\r\n"

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

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
 * NT-Shell Thread entry function
 * @details FreeRTOSスレッドのエントリ関数。
 *          SCI UARTを初期化し、NT-Shellを起動する。
 *          ntshell_execute()は内部で無限ループするため、この関数は戻らない。
 * @param pvParameters FreeRTOSタスクパラメータ（未使用）
 */
void ntshell_thread_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    /* NT-Shellインスタンス（staticでスタック消費を削減） */
    static ntshell_t ntshell;

    /* SCI UARTドライバの初期化 */
    jlink_console_init();

    /* 起動バナーの表示 */
    print_to_console(NTSHELL_BANNER);

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
