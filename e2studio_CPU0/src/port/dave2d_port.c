/**
 * @file dave2d_port.c
 * @brief Dave2D (D/AVE 2D) graphics accelerator port layer implementation
 * @details
 * Implements Dave2D initialization tracking, status query, and diagnostic
 * functions for the Renesas D/AVE 2D hardware graphics engine.
 *
 * This module acts as a diagnostic and tracking layer over the Dave2D
 * initialization performed by LVGL internally. It does NOT duplicate
 * the Dave2D initialization sequence.
 *
 * Dave2D Initialization Flow:
 *   1. lv_init() is called from lvgl_thread_entry.c
 *   2. LVGL internally calls lv_draw_dave2d_init() because LV_USE_DRAW_DAVE2D=1
 *   3. lv_draw_dave2d_init() calls lv_dave2d_init() which:
 *      a. d2_opendevice(0) - creates the device handle (_d2_handle)
 *      b. d2_inithw(_d2_handle, 0) - initializes Dave2D hardware
 *      c. Sets blend mode, alpha, antialiasing, line cap/join parameters
 *      d. Sets display list block size (25)
 *      e. Creates two render buffers (_blit_renderbuffer, _renderbuffer)
 *      f. Selects _renderbuffer as the active render buffer
 *   4. dave2d_port_init() (this module) verifies the handle is valid
 *
 *   Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:541-604
 *
 * The LVGL Dave2D draw unit (lv_draw_dave2d_unit_t) stores the handle
 * in its d2_handle member and uses it for all hardware-accelerated
 * drawing operations.
 *
 * Note on d2_handle0 vs _d2_handle:
 *   - d2_handle0: Declared in FSP-generated common_data.c:4 as a global
 *     pointer. This is an FSP convention but is NOT used by LVGL's Dave2D
 *     driver. It remains NULL unless user code assigns it.
 *   - _d2_handle: The actual device handle created and used by LVGL's
 *     lv_draw_dave2d.c:58. This is the handle we verify in this module.
 *
 * Reference:
 *   - LVGL Dave2D driver: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c
 *   - Dave2D API: e2studio_CPU0/ra/tes/dave2d/inc/dave_driver.h
 *   - FSP d2_handle0: e2studio_CPU0/ra_gen/common_data.c:4
 *
 * @note
 * This file is part of the Dave2D control (S-004-1) implementation.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>

#include "dave2d_port.h"
#include "dave_driver.h"
#include "common_data.h"
#include "jlink_console.h"
#include "cmd_utils.h"
#include "ntlibc.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size */
#define DAVE2D_PRINT_BUF_SIZE   (128)

/**********************************************************************************************************************
 External declarations
 *********************************************************************************************************************/

/**
 * LVGL internal Dave2D device handle
 *
 * This is the handle created by LVGL's lv_dave2d_init() via d2_opendevice(0).
 * It is defined as a global variable in:
 *   e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:58
 *
 * We declare it extern here to verify that LVGL has initialized Dave2D
 * without duplicating the initialization.
 *
 * Note: This handle is separate from the FSP-generated d2_handle0
 * (common_data.c:4) which is NOT used by LVGL.
 */
#if LV_USE_DRAW_DAVE2D
extern d2_device *_d2_handle;
#endif

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/** Current Dave2D initialization status */
static dave2d_status_t s_dave2d_status = DAVE2D_STATUS_NOT_INITIALIZED;

/**********************************************************************************************************************
 Private (static) functions prototypes
 *********************************************************************************************************************/
static void dave2d_cmd_status(void);

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * "dave2d status" sub-command handler
 *
 * @details Displays Dave2D initialization status, driver version,
 *          hardware revision, and LVGL integration state.
 */
static void dave2d_cmd_status(void)
{
    char buf[DAVE2D_PRINT_BUF_SIZE];
    dave2d_info_t info;
    const char *status_str;

    /* Initialization status */
    switch (s_dave2d_status) {
        case DAVE2D_STATUS_INITIALIZED:
            status_str = "Initialized (Available)";
            break;
        case DAVE2D_STATUS_NOT_AVAILABLE:
            status_str = "Not available (LV_USE_DRAW_DAVE2D disabled)";
            break;
        case DAVE2D_STATUS_ERROR:
            status_str = "ERROR (initialization failed)";
            break;
        default:
            status_str = "Not initialized";
            break;
    }

    print_to_console("[Dave2D Initialization Status]\r\n");

    snprintf(buf, sizeof(buf), "  Status          : %s\r\n", status_str);
    print_to_console(buf);

    dave2d_port_get_info(&info);

    snprintf(buf, sizeof(buf), "  LVGL Dave2D     : %s\r\n",
             info.lvgl_dave2d_enabled ? "Enabled (LV_USE_DRAW_DAVE2D=1)" : "Disabled");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Device Handle   : %s\r\n",
             info.handle_valid ? "Valid" : "NULL (not initialized)");
    print_to_console(buf);

    /* Driver version info */
    print_to_console("[Dave2D Driver Information]\r\n");

    if (info.driver_version_str != NULL) {
        snprintf(buf, sizeof(buf), "  Driver Version  : %s (0x%08lX)\r\n",
                 info.driver_version_str, (unsigned long)info.driver_version);
    } else {
        snprintf(buf, sizeof(buf), "  Driver Version  : 0x%08lX\r\n",
                 (unsigned long)info.driver_version);
    }
    print_to_console(buf);

    /* Hardware revision (only available if handle is valid) */
    print_to_console("[Dave2D Hardware Information]\r\n");

    if (info.handle_valid) {
        if (info.hw_revision_str != NULL) {
            snprintf(buf, sizeof(buf), "  HW Revision     : %s (0x%08lX)\r\n",
                     info.hw_revision_str, (unsigned long)info.hw_revision);
        } else {
            snprintf(buf, sizeof(buf), "  HW Revision     : 0x%08lX\r\n",
                     (unsigned long)info.hw_revision);
        }
        print_to_console(buf);
    } else {
        print_to_console("  HW Revision     : N/A (device not open)\r\n");
    }

    /* LVGL integration details */
    print_to_console("[LVGL Integration]\r\n");
    print_to_console("  Draw Unit       : lv_draw_dave2d_unit_t (ID=4)\r\n");
    print_to_console("  Render Mode     : Display list based (deferred execution)\r\n");

    print_to_console("[Supported Accelerated Operations]\r\n");
    print_to_console("  Rectangle Fill  : d2_renderbox (lv_draw_dave2d_fill)\r\n");
    print_to_console("  Border          : d2_renderbox (lv_draw_dave2d_border)\r\n");
    print_to_console("  Line            : d2_renderline (lv_draw_dave2d_line)\r\n");
    print_to_console("  Arc             : d2_renderwedge (lv_draw_dave2d_arc)\r\n");
    print_to_console("  Image/BLIT      : d2_setblitsrc + d2_blitcopy (lv_draw_dave2d_image)\r\n");
    print_to_console("  Label/Text      : d2_setblitsrc (lv_draw_dave2d_label)\r\n");
    print_to_console("  Triangle        : d2_rendertri (lv_draw_dave2d_triangle)\r\n");

    print_to_console("[Initialization Architecture]\r\n");
    print_to_console("  lv_init()\r\n");
    print_to_console("    -> lv_draw_dave2d_init()\r\n");
    print_to_console("      -> lv_dave2d_init()\r\n");
    print_to_console("        -> d2_opendevice(0)\r\n");
    print_to_console("        -> d2_inithw(handle, 0)\r\n");
    print_to_console("        -> d2_setblendmode/alphamode/antialiasing\r\n");
    print_to_console("        -> d2_newrenderbuffer() x2\r\n");
    print_to_console("  dave2d_port_init() [this module]\r\n");
    print_to_console("    -> Verifies _d2_handle != NULL\r\n");
    print_to_console("    -> Records status for diagnostics\r\n");
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Verify and record Dave2D initialization status
 *
 * @details Checks the LVGL internal Dave2D handle (_d2_handle) to verify
 *          that Dave2D was successfully initialized by lv_init().
 *
 *          This function MUST be called after lv_init() has completed.
 *          It does NOT perform the Dave2D initialization itself.
 *
 * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:58
 */
bool dave2d_port_init(void)
{
#if LV_USE_DRAW_DAVE2D
    /*
     * Verify that LVGL has initialized the Dave2D device handle.
     *
     * _d2_handle is set by lv_dave2d_init() which is called from
     * lv_draw_dave2d_init() during lv_init(). If LVGL initialization
     * succeeded and Dave2D was available, _d2_handle will be non-NULL.
     *
     * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:549
     */
    if (_d2_handle != NULL) {
        s_dave2d_status = DAVE2D_STATUS_INITIALIZED;
        return true;
    } else {
        /*
         * _d2_handle is NULL. This means either:
         *   1. lv_init() has not been called yet
         *   2. d2_opendevice() failed (out of memory)
         *   3. d2_inithw() failed (hardware not present or not responding)
         *
         * Check was performed after lv_init(), so this is an error.
         */
        s_dave2d_status = DAVE2D_STATUS_ERROR;
        return false;
    }
#else
    /*
     * LV_USE_DRAW_DAVE2D is disabled in the LVGL configuration.
     * Dave2D hardware acceleration is not available.
     *
     * Reference: e2studio_CPU0/ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h:107
     */
    s_dave2d_status = DAVE2D_STATUS_NOT_AVAILABLE;
    return false;
#endif
}

/**
 * Deinitialize the Dave2D port layer
 *
 * @details Resets the tracking state. The actual Dave2D hardware
 *          deinitialization (d2_deinithw, d2_closedevice) would be
 *          handled by LVGL's cleanup path if needed. This function
 *          provides a clean lifecycle boundary for the port layer.
 */
void dave2d_port_deinit(void)
{
    s_dave2d_status = DAVE2D_STATUS_NOT_INITIALIZED;
}

/**
 * Get the current Dave2D initialization status
 */
dave2d_status_t dave2d_port_get_status(void)
{
    return s_dave2d_status;
}

/**
 * Check if Dave2D hardware accelerator is available for use
 */
bool dave2d_port_is_available(void)
{
    return (s_dave2d_status == DAVE2D_STATUS_INITIALIZED);
}

/**
 * Get Dave2D hardware and driver information
 *
 * @details Fills the info structure with Dave2D version and hardware
 *          revision information. The driver version is always available
 *          (static library info). The hardware revision requires a
 *          valid device handle.
 *
 * Reference:
 *   - d2_getversion(): e2studio_CPU0/ra/tes/dave2d/inc/dave_driver.h:468
 *   - d2_getversionstring(): e2studio_CPU0/ra/tes/dave2d/inc/dave_driver.h:469
 *   - d2_getrevisionhw(): e2studio_CPU0/ra/tes/dave2d/inc/dave_driver.h:478
 *   - d2_getrevisionstringhw(): e2studio_CPU0/ra/tes/dave2d/inc/dave_driver.h:479
 */
void dave2d_port_get_info(dave2d_info_t *info)
{
    if (info == NULL) {
        return;
    }

    /* Driver version (always available - static library info) */
    info->driver_version     = (uint32_t)d2_getversion();
    info->driver_version_str = d2_getversionstring();

    /* LVGL Dave2D enabled flag */
#if LV_USE_DRAW_DAVE2D
    info->lvgl_dave2d_enabled = true;
    info->handle_valid        = (_d2_handle != NULL);
#else
    info->lvgl_dave2d_enabled = false;
    info->handle_valid        = false;
#endif

    /* Hardware revision (requires a valid device handle) */
#if LV_USE_DRAW_DAVE2D
    if (_d2_handle != NULL) {
        info->hw_revision     = d2_getrevisionhw(_d2_handle);
        info->hw_revision_str = d2_getrevisionstringhw(_d2_handle);
    } else {
        info->hw_revision     = 0;
        info->hw_revision_str = NULL;
    }
#else
    info->hw_revision     = 0;
    info->hw_revision_str = NULL;
#endif
}

/**
 * NT-Shell "dave2d" command handler
 *
 * @details Provides Dave2D diagnostic sub-commands:
 *   dave2d status  - Show Dave2D initialization state, HW/driver info, LVGL integration
 */
int usrcmd_dave2d(int argc, char **argv)
{
    if (argc < 2) {
        cmd_print_usage("dave2d", "<subcommand>");
        print_to_console("  status  - Show Dave2D initialization state and hardware info\r\n");
        return CMD_ERR_USAGE;
    }

    if (ntlibc_strcmp(argv[1], "status") == 0) {
        dave2d_cmd_status();
        return CMD_OK;
    }

    /* Unknown sub-command */
    {
        char buf[DAVE2D_PRINT_BUF_SIZE];
        snprintf(buf, sizeof(buf), "Error: Unknown sub-command '%s'.\r\n", argv[1]);
        print_to_console(buf);
    }

    cmd_print_usage("dave2d", "<subcommand>");
    print_to_console("  status  - Show Dave2D initialization state and hardware info\r\n");

    return CMD_ERR_INVALID_ARG;
}
