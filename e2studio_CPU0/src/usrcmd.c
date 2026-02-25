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
 *  - Debug commands: mr (memory read), md (memory dump), mw (memory write)
 *  - Hardware commands: led (LED control)
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
#include "led_ctrl.h"
#include "port/sdram_port.h"
#include "port/glcdc_port.h"
#include "port/dave2d_port.h"
#include "port/mipi_port.h"
#include "port/lv_port_indev.h"
#include "ui/ui_main_screen.h"

#include "lvgl.h"

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

/** Number of bytes displayed per line in hexdump output */
#define MD_BYTES_PER_LINE   (16)

/** Default dump length in bytes for the md command */
#define MD_DEFAULT_LENGTH   (256)

/** Maximum dump length in bytes (64KB) to prevent excessive output */
#define MD_MAX_LENGTH       (0x10000UL)

/** Maximum count for memory write fill operations */
#define MW_MAX_COUNT        CMD_MW_MAX_COUNT

/**********************************************************************************************************************
 Private (static) functions prototypes
 *********************************************************************************************************************/
static int usrcmd_ntopt_callback(int argc, char **argv, void *extobj);
static int usrcmd_help(int argc, char **argv);
static int usrcmd_info(int argc, char **argv);
static int usrcmd_version(int argc, char **argv);
static int usrcmd_reset(int argc, char **argv);
static int usrcmd_led(int argc, char **argv);
static int usrcmd_lvgl(int argc, char **argv);
static int usrcmd_md(int argc, char **argv);
static int usrcmd_mr(int argc, char **argv);
static int usrcmd_mw(int argc, char **argv);

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
 */
static const cmd_table_t cmdlist[] = {
    NTSHELL_CMD("camera",  "Camera: camera phy|csi|status|start|stop|capture|info|test", usrcmd_camera),
    NTSHELL_CMD("dave2d",  "Dave2D GPU: dave2d status|integration|test|bench", usrcmd_dave2d),
    NTSHELL_CMD("display", "GLCDC display: display status|fb|dbuf|test|backlight", usrcmd_display),
    NTSHELL_CMD("help",    "Show available commands",                   usrcmd_help),
    NTSHELL_CMD("info",    "Show system information (info sys|ver)",    usrcmd_info),
    NTSHELL_CMD("led",     "LED control: led list | led <id> <on|off|toggle|blink>", usrcmd_led),
    NTSHELL_CMD("lvgl",    "LVGL control: lvgl status|mem|screen|conf|testpat", usrcmd_lvgl),
    NTSHELL_CMD("md",      "Dump memory: md <addr> [length]",          usrcmd_md),
    NTSHELL_CMD("mr",      "Read memory: mr <addr> [size(1|2|4)]",     usrcmd_mr),
    NTSHELL_CMD("mw",      "Write memory: mw <addr> <val> [size] [count]", usrcmd_mw),
    NTSHELL_CMD("reset",   "Reset the system",                          usrcmd_reset),
    NTSHELL_CMD("sdram",   "SDRAM control: sdram status|check|map|test", usrcmd_sdram),
    NTSHELL_CMD("touch",   "Touch panel: touch status|info|read|mon",   usrcmd_touch),
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
 * lvgl command - LVGL status, memory, and configuration diagnostics
 *
 * @details Provides diagnostic information about the LVGL library state.
 *          This command is called from ntshell_thread, so all LVGL API
 *          calls must be protected with lv_lock() / lv_unlock() for
 *          thread safety.
 *
 * Usage:
 *   lvgl status  - Show LVGL initialization state and version
 *   lvgl mem     - Show LVGL heap memory usage (lv_mem_monitor)
 *   lvgl screen  - Show current screen information
 *   lvgl conf    - Show key lv_conf_user.h settings
 *
 * Issue: #4 (F-001-3)
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return CMD_OK on success, CMD_ERR_* on error
 */
static int usrcmd_lvgl(int argc, char **argv)
{
    char buf[PRINT_BUF_SIZE];

    if (argc < 2) {
        cmd_print_usage("lvgl", "<subcommand>");
        print_to_console("  status  - LVGL initialization state and version\r\n");
        print_to_console("  mem     - LVGL heap memory usage\r\n");
        print_to_console("  screen  - Current screen information\r\n");
        print_to_console("  conf    - Key lv_conf_user.h settings\r\n");
        print_to_console("  testpat - Redraw camera area test pattern\r\n");
        return CMD_ERR_USAGE;
    }

    /* --- "lvgl status" sub-command --- */
    if (ntlibc_strcmp(argv[1], "status") == 0) {
        print_to_console("[LVGL Status]\r\n");

        /* LVGL version */
        snprintf(buf, sizeof(buf), "  Version   : %d.%d.%d %s\r\n",
                 LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
                 LVGL_VERSION_INFO);
        print_to_console(buf);

        /* lv_is_initialized() check - requires lv_lock for thread safety */
        lv_lock();
        {
            bool initialized = lv_is_initialized();
            snprintf(buf, sizeof(buf), "  Initialized: %s\r\n",
                     initialized ? "Yes" : "No");
            print_to_console(buf);

            /* Display info */
            lv_display_t *disp = lv_display_get_default();
            if (disp != NULL) {
                int32_t hor = lv_display_get_horizontal_resolution(disp);
                int32_t ver = lv_display_get_vertical_resolution(disp);
                snprintf(buf, sizeof(buf), "  Display   : %ldx%ld\r\n",
                         (long)hor, (long)ver);
                print_to_console(buf);
            } else {
                print_to_console("  Display   : Not initialized\r\n");
            }

            /* Color depth */
            snprintf(buf, sizeof(buf), "  Color depth: %d bits\r\n", LV_COLOR_DEPTH);
            print_to_console(buf);

            /* Dave2D status */
#if LV_USE_DRAW_DAVE2D
            print_to_console("  Dave2D GPU : Enabled\r\n");
#else
            print_to_console("  Dave2D GPU : Disabled\r\n");
#endif

            /* Refresh period */
            snprintf(buf, sizeof(buf), "  Refresh   : %d ms (~%d FPS)\r\n",
                     LV_DEF_REFR_PERIOD, 1000 / LV_DEF_REFR_PERIOD);
            print_to_console(buf);
        }
        lv_unlock();

        return CMD_OK;
    }

    /* --- "lvgl mem" sub-command --- */
    if (ntlibc_strcmp(argv[1], "mem") == 0) {
        print_to_console("[LVGL Memory]\r\n");

        lv_lock();
        {
            lv_mem_monitor_t mon;
            lv_mem_monitor(&mon);

            snprintf(buf, sizeof(buf), "  Total size    : %lu bytes (%lu KB)\r\n",
                     (unsigned long)mon.total_size,
                     (unsigned long)(mon.total_size / 1024));
            print_to_console(buf);

            snprintf(buf, sizeof(buf), "  Free          : %lu bytes (%lu KB)\r\n",
                     (unsigned long)mon.free_size,
                     (unsigned long)(mon.free_size / 1024));
            print_to_console(buf);

            snprintf(buf, sizeof(buf), "  Used          : %lu bytes (%lu KB)\r\n",
                     (unsigned long)(mon.total_size - mon.free_size),
                     (unsigned long)((mon.total_size - mon.free_size) / 1024));
            print_to_console(buf);

            snprintf(buf, sizeof(buf), "  Max used      : %lu bytes (%lu KB)\r\n",
                     (unsigned long)mon.max_used,
                     (unsigned long)(mon.max_used / 1024));
            print_to_console(buf);

            snprintf(buf, sizeof(buf), "  Biggest free  : %lu bytes (%lu KB)\r\n",
                     (unsigned long)mon.free_biggest_size,
                     (unsigned long)(mon.free_biggest_size / 1024));
            print_to_console(buf);

            snprintf(buf, sizeof(buf), "  Used blocks   : %lu\r\n",
                     (unsigned long)mon.used_cnt);
            print_to_console(buf);

            snprintf(buf, sizeof(buf), "  Free blocks   : %lu\r\n",
                     (unsigned long)mon.free_cnt);
            print_to_console(buf);

            snprintf(buf, sizeof(buf), "  Used %%        : %u%%\r\n", mon.used_pct);
            print_to_console(buf);

            snprintf(buf, sizeof(buf), "  Fragmentation : %u%%\r\n", mon.frag_pct);
            print_to_console(buf);
        }
        lv_unlock();

        return CMD_OK;
    }

    /* --- "lvgl screen" sub-command --- */
    if (ntlibc_strcmp(argv[1], "screen") == 0) {
        print_to_console("[LVGL Screen]\r\n");

        lv_lock();
        {
            lv_display_t *disp = lv_display_get_default();
            if (disp == NULL) {
                print_to_console("  No display initialized\r\n");
                lv_unlock();
                return CMD_OK;
            }

            lv_obj_t *scr = lv_display_get_screen_active(disp);
            if (scr == NULL) {
                print_to_console("  No active screen\r\n");
                lv_unlock();
                return CMD_OK;
            }

            uint32_t child_count = lv_obj_get_child_count(scr);
            snprintf(buf, sizeof(buf), "  Active screen : %p\r\n", (void *)scr);
            print_to_console(buf);

            snprintf(buf, sizeof(buf), "  Child widgets : %lu\r\n",
                     (unsigned long)child_count);
            print_to_console(buf);

            int32_t w = lv_obj_get_width(scr);
            int32_t h = lv_obj_get_height(scr);
            snprintf(buf, sizeof(buf), "  Screen size   : %ldx%ld\r\n",
                     (long)w, (long)h);
            print_to_console(buf);

            /* List top-level children */
            if (child_count > 0) {
                uint32_t max_show = (child_count > 10) ? 10 : child_count;
                print_to_console("  Top-level children:\r\n");
                for (uint32_t i = 0; i < max_show; i++) {
                    lv_obj_t *child = lv_obj_get_child(scr, (int32_t)i);
                    const lv_obj_class_t *cls = lv_obj_get_class(child);
                    int32_t cw = lv_obj_get_width(child);
                    int32_t ch = lv_obj_get_height(child);
                    snprintf(buf, sizeof(buf), "    [%lu] class=%p size=%ldx%ld\r\n",
                             (unsigned long)i, (void *)cls, (long)cw, (long)ch);
                    print_to_console(buf);
                }
                if (child_count > max_show) {
                    snprintf(buf, sizeof(buf), "    ... and %lu more\r\n",
                             (unsigned long)(child_count - max_show));
                    print_to_console(buf);
                }
            }

            /* Main screen (F-001-7) details */
            print_to_console("\r\n  [Main Screen (F-001-7)]\r\n");
            if (ui_main_screen_is_created()) {
                print_to_console("  Created       : Yes\r\n");

                const char *status_text = ui_main_screen_get_status_text();
                if (status_text != NULL) {
                    snprintf(buf, sizeof(buf), "  Status text   : \"%s\"\r\n", status_text);
                    print_to_console(buf);
                }

                uint8_t *cam_buf = ui_main_screen_get_camera_buffer();
                if (cam_buf != NULL) {
                    snprintf(buf, sizeof(buf), "  Camera buffer : %p (SDRAM)\r\n", (void *)cam_buf);
                    print_to_console(buf);
                    snprintf(buf, sizeof(buf), "  Camera size   : %dx%d (RGB565, %lu bytes)\r\n",
                             UI_CAMERA_WIDTH, UI_CAMERA_HEIGHT,
                             (unsigned long)UI_CAMERA_BUF_SIZE);
                    print_to_console(buf);
                }

                lv_obj_t *cam_img = ui_main_screen_get_camera_image();
                if (cam_img != NULL) {
                    int32_t iw = lv_obj_get_width(cam_img);
                    int32_t ih = lv_obj_get_height(cam_img);
                    snprintf(buf, sizeof(buf), "  Camera widget : %p (%ldx%ld)\r\n",
                             (void *)cam_img, (long)iw, (long)ih);
                    print_to_console(buf);
                }
            } else {
                print_to_console("  Created       : No\r\n");
            }
        }
        lv_unlock();

        return CMD_OK;
    }

    /* --- "lvgl conf" sub-command --- */
    if (ntlibc_strcmp(argv[1], "conf") == 0) {
        print_to_console("[LVGL Configuration (lv_conf_user.h)]\r\n");

        snprintf(buf, sizeof(buf), "  LV_MEM_SIZE              : %lu bytes (%lu KB)\r\n",
                 (unsigned long)LV_MEM_SIZE,
                 (unsigned long)(LV_MEM_SIZE / 1024));
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  LV_DEF_REFR_PERIOD       : %d ms\r\n",
                 LV_DEF_REFR_PERIOD);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  LV_COLOR_DEPTH            : %d bits\r\n",
                 LV_COLOR_DEPTH);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  LV_DRAW_LAYER_SIMPLE_BUF  : %lu bytes (%lu KB)\r\n",
                 (unsigned long)LV_DRAW_LAYER_SIMPLE_BUF_SIZE,
                 (unsigned long)(LV_DRAW_LAYER_SIMPLE_BUF_SIZE / 1024));
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  LV_DRAW_LAYER_MAX_MEMORY  : %lu bytes (%lu KB)\r\n",
                 (unsigned long)LV_DRAW_LAYER_MAX_MEMORY,
                 (unsigned long)(LV_DRAW_LAYER_MAX_MEMORY / 1024));
        print_to_console(buf);

#if LV_USE_DRAW_DAVE2D
        print_to_console("  LV_USE_DRAW_DAVE2D        : 1 (Enabled)\r\n");
#else
        print_to_console("  LV_USE_DRAW_DAVE2D        : 0 (Disabled)\r\n");
#endif

#if LV_USE_LOG
        print_to_console("  LV_USE_LOG                : 1 (Enabled)\r\n");
    #if LV_LOG_LEVEL == LV_LOG_LEVEL_TRACE
        print_to_console("  LV_LOG_LEVEL              : TRACE\r\n");
    #elif LV_LOG_LEVEL == LV_LOG_LEVEL_INFO
        print_to_console("  LV_LOG_LEVEL              : INFO\r\n");
    #elif LV_LOG_LEVEL == LV_LOG_LEVEL_WARN
        print_to_console("  LV_LOG_LEVEL              : WARN\r\n");
    #elif LV_LOG_LEVEL == LV_LOG_LEVEL_ERROR
        print_to_console("  LV_LOG_LEVEL              : ERROR\r\n");
    #else
        print_to_console("  LV_LOG_LEVEL              : OTHER\r\n");
    #endif
        snprintf(buf, sizeof(buf), "  LV_LOG_PRINTF             : %d\r\n", LV_LOG_PRINTF);
        print_to_console(buf);
#else
        print_to_console("  LV_USE_LOG                : 0 (Disabled)\r\n");
#endif

#if LV_USE_SYSMON
        print_to_console("  LV_USE_SYSMON             : 1 (Enabled)\r\n");
    #if LV_USE_PERF_MONITOR
        print_to_console("  LV_USE_PERF_MONITOR       : 1 (Enabled)\r\n");
    #else
        print_to_console("  LV_USE_PERF_MONITOR       : 0 (Disabled)\r\n");
    #endif
    #if LV_USE_MEM_MONITOR
        print_to_console("  LV_USE_MEM_MONITOR        : 1 (Enabled)\r\n");
    #else
        print_to_console("  LV_USE_MEM_MONITOR        : 0 (Disabled)\r\n");
    #endif
#else
        print_to_console("  LV_USE_SYSMON             : 0 (Disabled)\r\n");
#endif

#if LV_USE_TINY_TTF
        print_to_console("  LV_USE_TINY_TTF           : 1 (Enabled)\r\n");
#else
        print_to_console("  LV_USE_TINY_TTF           : 0 (Disabled)\r\n");
#endif

        return CMD_OK;
    }

    /* --- "lvgl testpat" sub-command --- */
    if (ntlibc_strcmp(argv[1], "testpat") == 0) {
        if (!ui_main_screen_is_created()) {
            print_to_console("Error: Main screen not created yet.\r\n");
            return CMD_ERR_EXECUTE;
        }

        print_to_console("Drawing test pattern to camera area...\r\n");

        lv_lock();
        {
            ui_main_screen_draw_test_pattern();
            ui_main_screen_invalidate_camera();
        }
        lv_unlock();

        print_to_console("Done. Color bars should be visible in camera area.\r\n");
        return CMD_OK;
    }

    /* --- Unknown sub-command --- */
    snprintf(buf, sizeof(buf), "Error: Unknown sub-command '%s'.\r\n", argv[1]);
    print_to_console(buf);
    cmd_print_usage("lvgl", "status|mem|screen|conf|testpat");
    return CMD_ERR_INVALID_ARG;
}

/**
 * led command - Control user LEDs on the EK-RA8P1 board
 *
 * @details Provides on/off/toggle/blink control and status display for the
 *          user LEDs on the EK-RA8P1 evaluation board.
 *
 * Usage:
 *   led list                - Show all LEDs and their current state
 *   led <id> on             - Turn on the specified LED
 *   led <id> off            - Turn off the specified LED
 *   led <id> toggle         - Toggle the specified LED state
 *   led <id> blink [ms]     - Blink the LED (default: 500ms interval)
 *
 * Parameters:
 *   id    - LED index: 0 (Blue), 1 (Green), 2 (Red)
 *   ms    - Blink interval in milliseconds (optional, default: 500)
 *
 * EK-RA8P1 LED mapping:
 *   0: LED1 (Blue)  - P600 (BSP_IO_PORT_06_PIN_00)
 *   1: LED2 (Green) - P303 (BSP_IO_PORT_03_PIN_03)
 *   2: LED3 (Red)   - PA07 (BSP_IO_PORT_10_PIN_07)
 *
 * Examples:
 *   led list           -> Show all LED states
 *   led 0 on           -> Turn on LED1 (Blue)
 *   led 1 toggle       -> Toggle LED2 (Green)
 *   led 2 blink 200    -> Blink LED3 (Red) at 200ms interval
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return CMD_OK on success, CMD_ERR_* on error
 */
static int usrcmd_led(int argc, char **argv)
{
    char buf[PRINT_BUF_SIZE];

    /* --- No arguments: show usage --- */
    if (argc < 2) {
        cmd_print_usage("led", "list | <id> <on|off|toggle|blink> [interval_ms]");
        print_to_console("  list          - Show all LEDs and their current state\r\n");
        print_to_console("  id            - LED index: 0 (Blue), 1 (Green), 2 (Red)\r\n");
        print_to_console("  on/off/toggle - Control the LED\r\n");
        print_to_console("  blink [ms]    - Blink (default: 500ms interval)\r\n");
        return CMD_ERR_USAGE;
    }

    /* --- "led list" sub-command --- */
    if (ntlibc_strcmp(argv[1], "list") == 0) {
        print_to_console("Available LEDs:\r\n");

        for (uint32_t i = 0; i < LED_COUNT; i++) {
            const led_info_t *info = led_ctrl_get_info(i);
            led_state_t state = led_ctrl_get_state(i);
            const char *state_str;

            switch (state) {
                case LED_STATE_ON:       state_str = "ON";       break;
                case LED_STATE_BLINKING: state_str = "BLINKING"; break;
                default:                 state_str = "OFF";      break;
            }

            snprintf(buf, sizeof(buf), "  %lu: %s (%-5s) - %-4s [%s]\r\n",
                     (unsigned long)i, info->name, info->color,
                     info->pin_name, state_str);
            print_to_console(buf);
        }

        return CMD_OK;
    }

    /* --- Parse LED id --- */
    if (argc < 3) {
        cmd_print_usage("led", "<id> <on|off|toggle|blink> [interval_ms]");
        return CMD_ERR_USAGE;
    }

    {
        cmd_parse_result_t result = cmd_parse_uint32(argv[1]);
        if (!result.valid) {
            snprintf(buf, sizeof(buf), "Error: Invalid LED id '%s'.\r\n", argv[1]);
            print_to_console(buf);
            return CMD_ERR_INVALID_ARG;
        }

        uint32_t id = result.value;

        if (!led_ctrl_valid_id(id)) {
            snprintf(buf, sizeof(buf),
                     "Error: LED id %lu out of range. Valid: 0-%d.\r\n",
                     (unsigned long)id, LED_COUNT - 1);
            print_to_console(buf);
            return CMD_ERR_INVALID_ARG;
        }

        const led_info_t *info = led_ctrl_get_info(id);
        const char *action = argv[2];

        /* --- "on" action --- */
        if (ntlibc_strcmp(action, "on") == 0) {
            led_ctrl_on(id);
            snprintf(buf, sizeof(buf), "%s (%s): ON\r\n", info->name, info->color);
            print_to_console(buf);
            return CMD_OK;
        }

        /* --- "off" action --- */
        if (ntlibc_strcmp(action, "off") == 0) {
            led_ctrl_off(id);
            snprintf(buf, sizeof(buf), "%s (%s): OFF\r\n", info->name, info->color);
            print_to_console(buf);
            return CMD_OK;
        }

        /* --- "toggle" action --- */
        if (ntlibc_strcmp(action, "toggle") == 0) {
            led_state_t prev_state;
            led_ctrl_toggle(id, &prev_state);
            led_state_t new_state = led_ctrl_get_state(id);

            const char *prev_str = (prev_state == LED_STATE_ON) ? "ON" : "OFF";
            const char *new_str  = (new_state == LED_STATE_ON)  ? "ON" : "OFF";

            snprintf(buf, sizeof(buf), "%s (%s): %s -> %s\r\n",
                     info->name, info->color, prev_str, new_str);
            print_to_console(buf);
            return CMD_OK;
        }

        /* --- "blink" action --- */
        if (ntlibc_strcmp(action, "blink") == 0) {
            uint32_t interval_ms = LED_BLINK_DEFAULT_MS;

            /* Parse optional interval */
            if (argc >= 4) {
                cmd_parse_result_t interval_result = cmd_parse_uint32(argv[3]);
                if (!interval_result.valid || interval_result.value == 0) {
                    cmd_print_error("Blink interval must be a positive number (ms).");
                    return CMD_ERR_INVALID_ARG;
                }
                interval_ms = interval_result.value;
            }

            if (!led_ctrl_blink(id, interval_ms)) {
                cmd_print_error("Failed to start blink timer.");
                return CMD_ERR_EXECUTE;
            }

            snprintf(buf, sizeof(buf), "%s (%s): BLINKING (%lums interval)\r\n",
                     info->name, info->color, (unsigned long)interval_ms);
            print_to_console(buf);
            return CMD_OK;
        }

        /* --- Unknown action --- */
        snprintf(buf, sizeof(buf), "Error: Unknown action '%s'. Use on/off/toggle/blink.\r\n", action);
        print_to_console(buf);
        return CMD_ERR_INVALID_ARG;
    }
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

/**
 * md command - Memory dump (hexdump) at a specified address
 *
 * @details Reads a range of memory and displays it in a standard 16-byte-per-line
 *          hexdump format, including address column, hex values, and ASCII
 *          representation. An extra space is inserted after the 8th byte in
 *          each line for improved readability.
 *
 * Usage:
 *   md <address> [length]
 *
 * Parameters:
 *   address - Start address in hexadecimal (0x prefix) or decimal
 *   length  - Number of bytes to dump. Default: 256. Max: 65536 (64KB).
 *
 * Output format (one line per 16 bytes):
 *   XXXXXXXX: XX XX XX XX XX XX XX XX  XX XX XX XX XX XX XX XX  ................
 *
 * Examples:
 *   md 0x68000000         -> Dump 256 bytes starting at 0x68000000
 *   md 0x68000000 64      -> Dump 64 bytes starting at 0x68000000
 *   md 0x22000000 0x100   -> Dump 256 bytes from SRAM
 *
 * Validation:
 *   - Address must be within a known accessible memory region (see cmd_utils.h)
 *   - Length is clamped to MD_MAX_LENGTH (64KB) to prevent excessive output
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return CMD_OK on success, CMD_ERR_* on error
 */
static int usrcmd_md(int argc, char **argv)
{
    char buf[PRINT_BUF_SIZE];
    uint32_t addr;
    uint32_t length = MD_DEFAULT_LENGTH;

    /* --- Argument count check --- */
    if (argc < 2) {
        cmd_print_usage("md", "<address> [length]");
        print_to_console("  address - Start address (hex with 0x prefix, or decimal)\r\n");
        print_to_console("  length  - Number of bytes to dump (default: 256, max: 65536)\r\n");
        return CMD_ERR_USAGE;
    }

    /* --- Parse address --- */
    {
        cmd_parse_result_t result = cmd_parse_uint32(argv[1]);
        if (!result.valid) {
            snprintf(buf, sizeof(buf), "Error: Invalid address '%s'.\r\n", argv[1]);
            print_to_console(buf);
            return CMD_ERR_INVALID_ARG;
        }
        addr = result.value;
    }

    /* --- Parse optional length --- */
    if (argc >= 3) {
        cmd_parse_result_t result = cmd_parse_uint32(argv[2]);
        if (!result.valid) {
            snprintf(buf, sizeof(buf), "Error: Invalid length '%s'.\r\n", argv[2]);
            print_to_console(buf);
            return CMD_ERR_INVALID_ARG;
        }
        length = result.value;
    }

    /* --- Validate length --- */
    if (length == 0) {
        cmd_print_error("Length must be greater than 0.");
        return CMD_ERR_INVALID_ARG;
    }

    if (length > MD_MAX_LENGTH) {
        snprintf(buf, sizeof(buf),
                 "Warning: Length %lu exceeds maximum (%lu). Clamped to %lu.\r\n",
                 (unsigned long)length, (unsigned long)MD_MAX_LENGTH, (unsigned long)MD_MAX_LENGTH);
        print_to_console(buf);
        length = MD_MAX_LENGTH;
    }

    /* --- Validate start address is in an accessible memory region --- */
    if (!cmd_validate_address(addr, CMD_ACCESS_SIZE_BYTE)) {
        cmd_print_addr_error(addr);
        return CMD_ERR_INVALID_ADDR;
    }

    /* --- Validate end address is in an accessible memory region --- */
    {
        uint32_t end_addr = addr + length - 1;
        if (!cmd_validate_address(end_addr, CMD_ACCESS_SIZE_BYTE)) {
            snprintf(buf, sizeof(buf),
                     "Error: End address 0x%08lX is not in an accessible memory region.\r\n",
                     (unsigned long)end_addr);
            print_to_console(buf);
            return CMD_ERR_INVALID_ADDR;
        }
    }

    /* --- Perform hexdump output --- */
    {
        uint32_t offset;
        uint8_t line_data[MD_BYTES_PER_LINE];

        for (offset = 0; offset < length; offset += MD_BYTES_PER_LINE) {
            uint32_t line_addr = addr + offset;
            uint32_t remaining = length - offset;
            uint32_t line_len = (remaining < MD_BYTES_PER_LINE) ? remaining : MD_BYTES_PER_LINE;
            uint32_t i;
            int pos;

            /* Read memory for this line */
            for (i = 0; i < line_len; i++) {
                line_data[i] = *(volatile uint8_t *)(line_addr + i);
            }

            /* Address column */
            pos = snprintf(buf, sizeof(buf), "%08lX:", (unsigned long)line_addr);

            /* Hex dump column */
            for (i = 0; i < MD_BYTES_PER_LINE; i++) {
                /* Extra space separator after the 8th byte */
                if (i == 8) {
                    pos += snprintf(buf + pos, sizeof(buf) - (uint32_t)pos, " ");
                }

                if (i < line_len) {
                    pos += snprintf(buf + pos, sizeof(buf) - (uint32_t)pos, " %02X", line_data[i]);
                } else {
                    /* Pad with spaces for incomplete last line */
                    pos += snprintf(buf + pos, sizeof(buf) - (uint32_t)pos, "   ");
                }
            }

            /* Separator between hex and ASCII columns */
            pos += snprintf(buf + pos, sizeof(buf) - (uint32_t)pos, "  ");

            /* ASCII column */
            for (i = 0; i < line_len; i++) {
                char c = (char)line_data[i];
                /* Printable ASCII range: 0x20 (space) to 0x7E (tilde) */
                if (c >= 0x20 && c <= 0x7E) {
                    buf[pos++] = c;
                } else {
                    buf[pos++] = '.';
                }
            }

            /* Pad ASCII column for incomplete last line */
            for (i = line_len; i < MD_BYTES_PER_LINE; i++) {
                buf[pos++] = ' ';
            }

            /* Line termination */
            buf[pos++] = '\r';
            buf[pos++] = '\n';
            buf[pos] = '\0';

            print_to_console(buf);
        }
    }

    return CMD_OK;
}

/**
 * mr command - Read memory at a specified address
 *
 * @details Reads 1, 2, or 4 bytes from the given memory address using volatile
 *          access and prints the value in hexadecimal.
 *
 * Usage:
 *   mr <address> [size]
 *
 * Parameters:
 *   address - Memory address in hexadecimal (0x prefix) or decimal
 *   size    - Access size: 1 (byte), 2 (halfword), 4 (word). Default is 4.
 *
 * Examples:
 *   mr 0x40000000        -> 0x40000000: 0x12345678
 *   mr 0x40000000 1      -> 0x40000000: 0x78
 *   mr 0x40000000 2      -> 0x40000000: 0x5678
 *
 * Validation:
 *   - Address must be within a known accessible memory region (see cmd_utils.h)
 *   - Address must be aligned to the access size (2-byte or 4-byte boundary)
 *   - Access size must be 1, 2, or 4
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return CMD_OK on success, CMD_ERR_* on error
 */
static int usrcmd_mr(int argc, char **argv)
{
    char buf[PRINT_BUF_SIZE];
    uint32_t addr;
    int access_size = CMD_ACCESS_SIZE_WORD;  /* Default: 4-byte (word) access */

    /* --- Argument count check --- */
    if (argc < 2) {
        cmd_print_usage("mr", "<address> [size(1|2|4)]");
        print_to_console("  address - Memory address (hex with 0x prefix, or decimal)\r\n");
        print_to_console("  size    - Access size: 1=byte, 2=halfword, 4=word (default: 4)\r\n");
        return CMD_ERR_USAGE;
    }

    /* --- Parse address --- */
    {
        cmd_parse_result_t result = cmd_parse_uint32(argv[1]);
        if (!result.valid) {
            snprintf(buf, sizeof(buf), "Error: Invalid address '%s'.\r\n", argv[1]);
            print_to_console(buf);
            return CMD_ERR_INVALID_ARG;
        }
        addr = result.value;
    }

    /* --- Parse optional access size --- */
    if (argc >= 3) {
        cmd_parse_result_t result = cmd_parse_uint32(argv[2]);
        if (!result.valid) {
            snprintf(buf, sizeof(buf), "Error: Invalid size '%s'.\r\n", argv[2]);
            print_to_console(buf);
            return CMD_ERR_INVALID_ARG;
        }
        access_size = (int)result.value;
    }

    /* --- Validate access size (must be 1, 2, or 4) --- */
    if (!cmd_validate_access_size(access_size)) {
        cmd_print_error("Size must be 1 (byte), 2 (halfword), or 4 (word).");
        return CMD_ERR_INVALID_ARG;
    }

    /* --- Validate address alignment --- */
    if (!cmd_validate_alignment(addr, (uint32_t)access_size)) {
        cmd_print_align_error(addr, access_size);
        return CMD_ERR_ALIGN;
    }

    /* --- Validate address is in an accessible memory region --- */
    if (!cmd_validate_address(addr, (uint32_t)access_size)) {
        cmd_print_addr_error(addr);
        return CMD_ERR_INVALID_ADDR;
    }

    /* --- Perform memory read and display result --- */
    switch (access_size) {
        case CMD_ACCESS_SIZE_BYTE: {
            uint8_t val = *(volatile uint8_t *)addr;
            snprintf(buf, sizeof(buf), "0x%08lX: 0x%02X\r\n",
                     (unsigned long)addr, val);
            break;
        }
        case CMD_ACCESS_SIZE_HALF: {
            uint16_t val = *(volatile uint16_t *)addr;
            snprintf(buf, sizeof(buf), "0x%08lX: 0x%04X\r\n",
                     (unsigned long)addr, val);
            break;
        }
        case CMD_ACCESS_SIZE_WORD: {
            uint32_t val = *(volatile uint32_t *)addr;
            snprintf(buf, sizeof(buf), "0x%08lX: 0x%08lX\r\n",
                     (unsigned long)addr, (unsigned long)val);
            break;
        }
        default:
            /* Should not reach here after validation */
            cmd_print_error("Internal error: unexpected access size.");
            return CMD_ERR_EXECUTE;
    }

    print_to_console(buf);
    return CMD_OK;
}

/**
 * mw command - Write memory at a specified address
 *
 * @details Writes a value of 1, 2, or 4 bytes to the given memory address
 *          using volatile access. Optionally repeats the write for memory fill
 *          operations. Performs readback verification after the write.
 *
 * Usage:
 *   mw <address> <value> [size] [count]
 *
 * Parameters:
 *   address - Memory address in hexadecimal (0x prefix) or decimal
 *   value   - Value to write in hexadecimal (0x prefix) or decimal
 *   size    - Access size: 1 (byte), 2 (halfword), 4 (word). Default is 4.
 *   count   - Number of consecutive writes (fill). Default is 1. Max: 65536.
 *
 * Examples:
 *   mw 0x68000000 0xDEADBEEF         -> Write 0xDEADBEEF to 0x68000000 (4 bytes)
 *   mw 0x68000000 0xFF 1 256         -> Fill 256 bytes with 0xFF from 0x68000000
 *   mw 0x68000000 0x0000 2           -> Write 0x0000 to 0x68000000 (2 bytes)
 *
 * Safety:
 *   - Flash area (0x02000000-0x03000000) writes are blocked
 *   - ITCM area (0x00000000-0x00020000) writes are blocked
 *   - System registers (0xE0000000-0xF0000000) writes are blocked
 *   - Count is limited to MW_MAX_COUNT to prevent runaway fills
 *   - Address alignment is enforced for 2-byte and 4-byte accesses
 *   - Readback verification is performed for single writes (count == 1)
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return CMD_OK on success, CMD_ERR_* on error
 */
static int usrcmd_mw(int argc, char **argv)
{
    char buf[PRINT_BUF_SIZE];
    uint32_t addr;
    uint32_t value;
    int access_size = CMD_ACCESS_SIZE_WORD;  /* Default: 4-byte (word) access */
    uint32_t count = 1;                      /* Default: single write */

    /* --- Argument count check --- */
    if (argc < 3) {
        cmd_print_usage("mw", "<address> <value> [size(1|2|4)] [count]");
        print_to_console("  address - Memory address (hex with 0x prefix, or decimal)\r\n");
        print_to_console("  value   - Value to write (hex with 0x prefix, or decimal)\r\n");
        print_to_console("  size    - Access size: 1=byte, 2=halfword, 4=word (default: 4)\r\n");
        print_to_console("  count   - Repeat count for fill operations (default: 1, max: 65536)\r\n");
        return CMD_ERR_USAGE;
    }

    /* --- Parse address --- */
    {
        cmd_parse_result_t result = cmd_parse_uint32(argv[1]);
        if (!result.valid) {
            snprintf(buf, sizeof(buf), "Error: Invalid address '%s'.\r\n", argv[1]);
            print_to_console(buf);
            return CMD_ERR_INVALID_ARG;
        }
        addr = result.value;
    }

    /* --- Parse value --- */
    {
        cmd_parse_result_t result = cmd_parse_uint32(argv[2]);
        if (!result.valid) {
            snprintf(buf, sizeof(buf), "Error: Invalid value '%s'.\r\n", argv[2]);
            print_to_console(buf);
            return CMD_ERR_INVALID_ARG;
        }
        value = result.value;
    }

    /* --- Parse optional access size --- */
    if (argc >= 4) {
        cmd_parse_result_t result = cmd_parse_uint32(argv[3]);
        if (!result.valid) {
            snprintf(buf, sizeof(buf), "Error: Invalid size '%s'.\r\n", argv[3]);
            print_to_console(buf);
            return CMD_ERR_INVALID_ARG;
        }
        access_size = (int)result.value;
    }

    /* --- Parse optional count --- */
    if (argc >= 5) {
        cmd_parse_result_t result = cmd_parse_uint32(argv[4]);
        if (!result.valid) {
            snprintf(buf, sizeof(buf), "Error: Invalid count '%s'.\r\n", argv[4]);
            print_to_console(buf);
            return CMD_ERR_INVALID_ARG;
        }
        count = result.value;
    }

    /* --- Validate access size (must be 1, 2, or 4) --- */
    if (!cmd_validate_access_size(access_size)) {
        cmd_print_error("Size must be 1 (byte), 2 (halfword), or 4 (word).");
        return CMD_ERR_INVALID_ARG;
    }

    /* --- Validate value range for the access size --- */
    if (access_size == CMD_ACCESS_SIZE_BYTE && value > 0xFFUL) {
        snprintf(buf, sizeof(buf),
                 "Error: Value 0x%lX exceeds byte range (0x00-0xFF).\r\n",
                 (unsigned long)value);
        print_to_console(buf);
        return CMD_ERR_INVALID_ARG;
    }
    if (access_size == CMD_ACCESS_SIZE_HALF && value > 0xFFFFUL) {
        snprintf(buf, sizeof(buf),
                 "Error: Value 0x%lX exceeds halfword range (0x0000-0xFFFF).\r\n",
                 (unsigned long)value);
        print_to_console(buf);
        return CMD_ERR_INVALID_ARG;
    }

    /* --- Validate count --- */
    if (count == 0) {
        cmd_print_error("Count must be greater than 0.");
        return CMD_ERR_INVALID_ARG;
    }

    if (count > MW_MAX_COUNT) {
        snprintf(buf, sizeof(buf),
                 "Error: Count %lu exceeds maximum (%lu).\r\n",
                 (unsigned long)count, (unsigned long)MW_MAX_COUNT);
        print_to_console(buf);
        return CMD_ERR_INVALID_ARG;
    }

    /* --- Validate address alignment --- */
    if (!cmd_validate_alignment(addr, (uint32_t)access_size)) {
        cmd_print_align_error(addr, access_size);
        return CMD_ERR_ALIGN;
    }

    /* --- Validate start address is in a writable memory region --- */
    if (!cmd_validate_writable(addr, (uint32_t)access_size)) {
        /* Check if address is accessible but read-only */
        if (cmd_validate_address(addr, (uint32_t)access_size)) {
            cmd_print_write_protect_error(addr);
        } else {
            cmd_print_addr_error(addr);
        }
        return CMD_ERR_INVALID_ADDR;
    }

    /* --- Validate end address for fill operations --- */
    if (count > 1) {
        uint32_t total_size = (uint32_t)access_size * count;
        uint32_t end_addr = addr + total_size - 1;

        /* Overflow check */
        if (end_addr < addr) {
            cmd_print_error("Address range overflow.");
            return CMD_ERR_INVALID_ADDR;
        }

        if (!cmd_validate_writable(end_addr, (uint32_t)access_size)) {
            snprintf(buf, sizeof(buf),
                     "Error: End address 0x%08lX is not in a writable memory region.\r\n",
                     (unsigned long)end_addr);
            print_to_console(buf);
            return CMD_ERR_INVALID_ADDR;
        }
    }

    /* --- Perform memory write --- */
    {
        uint32_t i;
        uint32_t current_addr;

        for (i = 0; i < count; i++) {
            current_addr = addr + (i * (uint32_t)access_size);

            switch (access_size) {
                case CMD_ACCESS_SIZE_BYTE:
                    *(volatile uint8_t *)current_addr = (uint8_t)value;
                    break;
                case CMD_ACCESS_SIZE_HALF:
                    *(volatile uint16_t *)current_addr = (uint16_t)value;
                    break;
                case CMD_ACCESS_SIZE_WORD:
                    *(volatile uint32_t *)current_addr = value;
                    break;
                default:
                    /* Should not reach here after validation */
                    break;
            }
        }
    }

    /* --- Output result --- */
    if (count == 1) {
        /* Single write: show written value and readback verification */
        switch (access_size) {
            case CMD_ACCESS_SIZE_BYTE: {
                uint8_t readback = *(volatile uint8_t *)addr;
                snprintf(buf, sizeof(buf),
                         "Written 0x%02X to 0x%08lX (%d byte)\r\n",
                         (uint8_t)value, (unsigned long)addr, access_size);
                print_to_console(buf);

                if (readback != (uint8_t)value) {
                    snprintf(buf, sizeof(buf),
                             "Warning: Readback mismatch! Expected 0x%02X, got 0x%02X\r\n",
                             (uint8_t)value, readback);
                    print_to_console(buf);
                }
                break;
            }
            case CMD_ACCESS_SIZE_HALF: {
                uint16_t readback = *(volatile uint16_t *)addr;
                snprintf(buf, sizeof(buf),
                         "Written 0x%04X to 0x%08lX (%d bytes)\r\n",
                         (uint16_t)value, (unsigned long)addr, access_size);
                print_to_console(buf);

                if (readback != (uint16_t)value) {
                    snprintf(buf, sizeof(buf),
                             "Warning: Readback mismatch! Expected 0x%04X, got 0x%04X\r\n",
                             (uint16_t)value, readback);
                    print_to_console(buf);
                }
                break;
            }
            case CMD_ACCESS_SIZE_WORD: {
                uint32_t readback = *(volatile uint32_t *)addr;
                snprintf(buf, sizeof(buf),
                         "Written 0x%08lX to 0x%08lX (%d bytes)\r\n",
                         (unsigned long)value, (unsigned long)addr, access_size);
                print_to_console(buf);

                if (readback != value) {
                    snprintf(buf, sizeof(buf),
                             "Warning: Readback mismatch! Expected 0x%08lX, got 0x%08lX\r\n",
                             (unsigned long)value, (unsigned long)readback);
                    print_to_console(buf);
                }
                break;
            }
            default:
                break;
        }
    } else {
        /* Fill operation: show summary */
        uint32_t total_bytes = count * (uint32_t)access_size;
        snprintf(buf, sizeof(buf),
                 "Filled %lu bytes with 0x%lX from 0x%08lX (%lu x %d-byte writes)\r\n",
                 (unsigned long)total_bytes, (unsigned long)value,
                 (unsigned long)addr, (unsigned long)count, access_size);
        print_to_console(buf);
    }

    return CMD_OK;
}
