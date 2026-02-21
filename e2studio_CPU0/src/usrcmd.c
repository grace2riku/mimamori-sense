/**
 * @file usrcmd.c
 * @brief NT-Shell command table and command dispatcher
 * @details
 * NT-Shell command registration framework implementation.
 *
 * This file contains:
 *  - The central command table (cmdlist[])
 *  - The command dispatcher (ntopt callback)
 *  - Built-in commands: help, info, version, reset
 *
 * To add a new command:
 *   1. Write a handler: static int usrcmd_foo(int argc, char **argv);
 *   2. Add to cmdlist[]:  NTSHELL_CMD("foo", "Description", usrcmd_foo),
 *   3. Done. The command appears in 'help' automatically.
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
#include "cmd_utils.h"
#include "fw_version.h"

#include "FreeRTOS.h"
#include "task.h"
#include "fsp_version.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output format buffer size */
#define PRINT_BUF_SIZE      (128)

/** Compute the number of entries in a static array */
#define CMD_TABLE_SIZE(tbl) (sizeof(tbl) / sizeof(tbl[0]))

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
 * Central command table
 *
 * All NT-Shell commands are registered here. The 'help' command iterates
 * this table to display the full list. Commands are matched by exact name
 * using ntlibc_strcmp().
 *
 * To add a new command, insert a NTSHELL_CMD() entry below.
 * Keep entries in alphabetical order for readability.
 *
 * @note Future debug commands (mr, md, mw, led, etc.) will be added here
 *       as they are implemented in subsequent Issues (S-008 through S-011).
 */
static const cmd_table_t cmdlist[] = {
    NTSHELL_CMD("help",    "Show available commands",                   usrcmd_help),
    NTSHELL_CMD("info",    "Show system information (info sys|ver)",    usrcmd_info),
    NTSHELL_CMD("reset",   "Reset the system",                          usrcmd_reset),
    NTSHELL_CMD("version", "Show firmware version",                     usrcmd_version),
};

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Execute a command string from NT-Shell
 */
int usrcmd_execute(const char *text)
{
    return ntopt_parse(text, usrcmd_ntopt_callback, 0);
}

/**
 * Get the command table and its size
 */
const cmd_table_t *usrcmd_get_cmdlist(int *count)
{
    if (count != NULL) {
        *count = (int)CMD_TABLE_SIZE(cmdlist);
    }
    return &cmdlist[0];
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * ntopt callback - Command dispatcher
 * @details
 * Called by ntopt_parse() after splitting the input text into argc/argv.
 * Searches the command table for a matching command name and invokes it.
 *
 * Error handling:
 *  - Empty input (argc == 0): silently returns 0.
 *  - Unknown command: prints "Unknown command" message with hint.
 *  - Command returns error: prints result via cmd_print_result() if needed.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @param extobj External object (unused)
 * @return Command handler return value, or 0 for empty/unknown input
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
            int retval = p->func(argc, argv);

            /* Unified error reporting for non-zero returns */
            if (retval != CMD_OK) {
                cmd_print_result(p->cmd, retval);
            }

            return retval;
        }
        p++;
    }

    /* Unknown command */
    {
        char buf[PRINT_BUF_SIZE];
        snprintf(buf, sizeof(buf), "Unknown command: '%s'. Type 'help' for available commands.\r\n", argv[0]);
        print_to_console(buf);
    }

    return 0;
}

/**
 * help command - Display all registered commands
 * @details
 * Lists all commands in the command table with their descriptions.
 * If a command name is given as argument (e.g., "help info"), shows
 * only that command's description.
 */
static int usrcmd_help(int argc, char **argv)
{
    char buf[PRINT_BUF_SIZE];
    const cmd_table_t *p = &cmdlist[0];
    const int count = (int)CMD_TABLE_SIZE(cmdlist);

    if (argc >= 2) {
        /* Show help for a specific command */
        for (int i = 0; i < count; i++) {
            if (ntlibc_strcmp((const char *)argv[1], cmdlist[i].cmd) == 0) {
                snprintf(buf, sizeof(buf), "  %-12s %s\r\n", cmdlist[i].cmd, cmdlist[i].desc);
                print_to_console(buf);
                return CMD_OK;
            }
        }

        snprintf(buf, sizeof(buf), "Unknown command: '%s'\r\n", argv[1]);
        print_to_console(buf);
        return CMD_ERR_USAGE;
    }

    /* Show all commands */
    print_to_console("Available commands:\r\n");
    print_to_console("-------------------------------------------\r\n");

    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "  %-12s %s\r\n", p->cmd, p->desc);
        print_to_console(buf);
        p++;
    }

    return CMD_OK;
}

/**
 * info command - Display system information
 * @details Sub-commands:
 *  - info sys : System name and build info
 *  - info ver : FSP, FreeRTOS, NT-Shell version info
 *  - (no args): Show all information
 */
static int usrcmd_info(int argc, char **argv)
{
    char buf[PRINT_BUF_SIZE];

    if (argc == 1) {
        /* No arguments: show all information */
        print_to_console("[System Information]\r\n");

        snprintf(buf, sizeof(buf), "  System    : %s (%s)\r\n", FW_PROJECT_NAME, FW_BOARD_NAME);
        print_to_console(buf);

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

        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "sys") == 0) {
        snprintf(buf, sizeof(buf), "  System    : %s (%s)\r\n", FW_PROJECT_NAME, FW_BOARD_NAME);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  CPU       : Cortex-M85 (Core %d)\r\n", _RA_CORE);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  Built     : %s %s\r\n", __DATE__, __TIME__);
        print_to_console(buf);

        return CMD_OK;
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

        return CMD_OK;
    }

    print_to_console("Usage: info [sys|ver]\r\n");
    print_to_console("  sys  - System name and build info\r\n");
    print_to_console("  ver  - Version information\r\n");
    print_to_console("  (no args) - Show all information\r\n");
    return CMD_ERR_USAGE;
}

/**
 * version command - Display firmware version
 */
static int usrcmd_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char buf[PRINT_BUF_SIZE];

    snprintf(buf, sizeof(buf), "%s Firmware v%d.%d.%d\r\n", FW_PROJECT_NAME,
             FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "Built: %s %s\r\n", __DATE__, __TIME__);
    print_to_console(buf);

    return CMD_OK;
}

/**
 * reset command - Perform system reset
 * @details Calls CMSIS NVIC_SystemReset(). This function does not return.
 */
static int usrcmd_reset(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    print_to_console("System resetting...\r\n");

    /* Wait for transmit to complete */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* CMSIS standard system reset */
    NVIC_SystemReset();

    /* Should not reach here */
    return CMD_OK;
}
