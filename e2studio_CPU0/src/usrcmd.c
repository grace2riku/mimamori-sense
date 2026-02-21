/**
 * @file usrcmd.c
 * @brief NT-Shell ユーザーコマンド実装
 * @details
 * NT-Shellのコマンドテーブルとコマンドディスパッチャを実装する。
 * 基本コマンド（help, info, version, reset）を提供する。
 *
 * @author CuBeatSystems
 * @author Shinichiro Nakamura
 * @copyright
 * ===============================================================
 * Natural Tiny Shell (NT-Shell) Version 0.3.1
 * ===============================================================
 * Copyright (c) 2010-2016 Shinichiro Nakamura
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>

#include "ntopt.h"
#include "ntlibc.h"
#include "ntshell.h"
#include "jlink_console.h"
#include "usrcmd.h"

#include "FreeRTOS.h"
#include "task.h"
#include "fsp_version.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** ファームウェアバージョン */
#define FW_VERSION_MAJOR    (0)
#define FW_VERSION_MINOR    (1)
#define FW_VERSION_PATCH    (0)

/** コンソール出力用書式付き文字列バッファサイズ */
#define PRINT_BUF_SIZE      (128)

/** コマンドテーブルのエントリ数を計算するマクロ */
#define CMD_TABLE_SIZE(tbl) (sizeof(tbl) / sizeof(tbl[0]))

/**********************************************************************************************************************
 Local Typedef definitions
 *********************************************************************************************************************/

/** コマンド関数の型定義 */
typedef int (*USRCMDFUNC)(int argc, char **argv);

/** コマンドテーブルエントリ */
typedef struct {
    const char *cmd;    /**< コマンド名 */
    const char *desc;   /**< ヘルプ説明文 */
    USRCMDFUNC func;    /**< コマンド実行関数 */
} cmd_table_t;

/**********************************************************************************************************************
 Private (static) functions prototypes
 *********************************************************************************************************************/
static int usrcmd_ntopt_callback(int argc, char **argv, void *extobj);
static int usrcmd_help(int argc, char **argv);
static int usrcmd_info(int argc, char **argv);
static int usrcmd_version(int argc, char **argv);
static int usrcmd_reset(int argc, char **argv);

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/**
 * コマンドテーブル
 * @note 新しいコマンドを追加する場合はこのテーブルにエントリを追加する。
 *       helpコマンドでこのテーブルの内容が一覧表示される。
 */
static const cmd_table_t cmdlist[] = {
    { "help",    "Show available commands",                   usrcmd_help    },
    { "info",    "Show system information (info sys|ver)",    usrcmd_info    },
    { "version", "Show firmware version",                     usrcmd_version },
    { "reset",   "Reset the system",                          usrcmd_reset   },
};

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * コマンド文字列を解析して実行する
 * @param text NT-Shellから渡されるコマンド文字列
 * @return コマンドの戻り値
 */
int usrcmd_execute(const char *text)
{
    return ntopt_parse(text, usrcmd_ntopt_callback, 0);
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * ntoptコールバック（コマンドディスパッチャ）
 * @details パース済みのargc/argvからコマンドテーブルを検索し、該当するコマンド関数を実行する。
 * @param argc 引数の数
 * @param argv 引数の配列
 * @param extobj 拡張オブジェクト（未使用）
 * @return コマンドの戻り値。コマンドが見つからない場合は0。
 */
static int usrcmd_ntopt_callback(int argc, char **argv, void *extobj)
{
    (void)extobj;

    if (argc == 0) {
        return 0;
    }

    const cmd_table_t *p = &cmdlist[0];
    for (int i = 0; i < (int)CMD_TABLE_SIZE(cmdlist); i++) {
        if (ntlibc_strcmp((const char *)argv[0], p->cmd) == 0) {
            return p->func(argc, argv);
        }
        p++;
    }

    /* 未登録コマンドのエラー表示 */
    {
        char buf[PRINT_BUF_SIZE];
        snprintf(buf, sizeof(buf), "Unknown command: '%s'. Type 'help' for available commands.\r\n", argv[0]);
        print_to_console(buf);
    }

    return 0;
}

/**
 * helpコマンド - 登録コマンド一覧表示
 * @param argc 引数の数
 * @param argv 引数の配列
 * @return 0: 成功
 */
static int usrcmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char buf[PRINT_BUF_SIZE];
    const cmd_table_t *p = &cmdlist[0];

    print_to_console("Available commands:\r\n");
    print_to_console("-------------------------------------------\r\n");

    for (int i = 0; i < (int)CMD_TABLE_SIZE(cmdlist); i++) {
        snprintf(buf, sizeof(buf), "  %-12s %s\r\n", p->cmd, p->desc);
        print_to_console(buf);
        p++;
    }

    return 0;
}

/**
 * infoコマンド - システム情報表示
 * @details サブコマンドにより表示する情報を選択する。
 *          - info sys : システム名とビルド日時
 *          - info ver : FSP・FreeRTOS・NTShellのバージョン
 *          - 引数なし : 全情報を表示
 * @param argc 引数の数
 * @param argv 引数の配列
 * @return 0: 成功, -1: 不明なサブコマンド
 */
static int usrcmd_info(int argc, char **argv)
{
    char buf[PRINT_BUF_SIZE];

    if (argc == 1) {
        /* 引数なしの場合は全情報を表示 */
        print_to_console("[System Information]\r\n");
        print_to_console("  System    : Mimamori-Sense (EK-RA8P1)\r\n");

        snprintf(buf, sizeof(buf), "  CPU       : Cortex-M85 (Core %d)\r\n", _RA_CORE);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  Built     : %s %s\r\n", __DATE__, __TIME__);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  FW Ver    : %d.%d.%d\r\n",
                 FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  FSP       : %s\r\n", FSP_VERSION_STRING);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  FreeRTOS  : %s\r\n", tskKERNEL_VERSION_NUMBER);
        print_to_console(buf);

        {
            int nt_major, nt_minor, nt_release;
            ntshell_version(&nt_major, &nt_minor, &nt_release);
            snprintf(buf, sizeof(buf), "  NT-Shell  : %d.%d.%d\r\n", nt_major, nt_minor, nt_release);
            print_to_console(buf);
        }

        return 0;
    }

    if (ntlibc_strcmp(argv[1], "sys") == 0) {
        print_to_console("  System    : Mimamori-Sense (EK-RA8P1)\r\n");

        snprintf(buf, sizeof(buf), "  CPU       : Cortex-M85 (Core %d)\r\n", _RA_CORE);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  Built     : %s %s\r\n", __DATE__, __TIME__);
        print_to_console(buf);

        return 0;
    }

    if (ntlibc_strcmp(argv[1], "ver") == 0) {
        snprintf(buf, sizeof(buf), "  FW Ver    : %d.%d.%d\r\n",
                 FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  FSP       : %s\r\n", FSP_VERSION_STRING);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  FreeRTOS  : %s\r\n", tskKERNEL_VERSION_NUMBER);
        print_to_console(buf);

        {
            int nt_major, nt_minor, nt_release;
            ntshell_version(&nt_major, &nt_minor, &nt_release);
            snprintf(buf, sizeof(buf), "  NT-Shell  : %d.%d.%d\r\n", nt_major, nt_minor, nt_release);
            print_to_console(buf);
        }

        return 0;
    }

    print_to_console("Usage: info [sys|ver]\r\n");
    print_to_console("  sys  - System name and build info\r\n");
    print_to_console("  ver  - Version information\r\n");
    print_to_console("  (no args) - Show all information\r\n");
    return -1;
}

/**
 * versionコマンド - ファームウェアバージョン表示
 * @param argc 引数の数
 * @param argv 引数の配列
 * @return 0: 成功
 */
static int usrcmd_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char buf[PRINT_BUF_SIZE];

    snprintf(buf, sizeof(buf), "Mimamori-Sense Firmware v%d.%d.%d\r\n",
             FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "Built: %s %s\r\n", __DATE__, __TIME__);
    print_to_console(buf);

    return 0;
}

/**
 * resetコマンド - システムリセット
 * @details CMSIS NVIC_SystemReset()を呼び出してシステムをリセットする。
 * @param argc 引数の数
 * @param argv 引数の配列
 * @return 通常は戻らない
 */
static int usrcmd_reset(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    print_to_console("System resetting...\r\n");

    /* 送信完了を待つ */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* CMSIS標準のシステムリセット */
    NVIC_SystemReset();

    /* ここには到達しない */
    return 0;
}
