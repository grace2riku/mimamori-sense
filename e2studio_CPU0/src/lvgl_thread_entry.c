/**
 * @file lvgl_thread_entry.c
 * @brief LVGL thread entry point - display initialization and LVGL main loop
 * @details
 * This thread performs the LVGL and GLCDC display initialization sequence,
 * then runs the LVGL timer handler in a loop to process rendering and
 * input events.
 *
 * Initialization sequence:
 *   1. lv_init() - Initialize the LVGL library
 *   2. glcdc_port_init() - Initialize GLCDC display subsystem:
 *      a. LCD hardware reset (DISP_RESET pin)
 *      b. RM_LVGL_PORT_Open() -> R_GLCDC_Open() + R_GLCDC_Start()
 *      c. Register backlight enable callback (after first frame flush)
 *   3. lv_timer_handler() loop - Process LVGL rendering at 1ms intervals
 *
 * Preconditions (satisfied before this thread starts):
 *   - SDRAM initialized by R_BSP_SdramInit() in hal_warmstart.c
 *   - IOPORT opened by R_IOPORT_Open() in hal_warmstart.c
 *   - FreeRTOS scheduler running
 *
 * Reference:
 *   - Reference project: reference_projects/lv_port_renesas_ek_ra8p1/src/new_thread0_entry.c
 *   - GLCDC init: e2studio_CPU0/src/port/glcdc_port.c (glcdc_port_init)
 *   - RM_LVGL_PORT: e2studio_CPU0/ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c
 *
 * @note
 * This file is part of the GLCDC control (S-002-2) implementation.
 */

#include "lvgl_thread.h"
#include "lvgl.h"
#include "port/glcdc_port.h"

/**
 * LVGL thread entry function
 *
 * @details Initializes the LVGL library and GLCDC display, then enters
 *          the LVGL main loop. The loop calls lv_timer_handler() to
 *          process pending LVGL tasks (rendering, animations, timers)
 *          and yields to other FreeRTOS tasks with a 1ms delay.
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/new_thread0_entry.c:16-78
 *
 * @param pvParameters  FreeRTOS task parameter (unused)
 */
void lvgl_thread_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    /*
     * Step 1: Initialize LVGL library
     *
     * This must be called before any other LVGL function.
     * Initializes LVGL's internal data structures, memory management,
     * timer system, and default theme.
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/new_thread0_entry.c:24
     */
    lv_init();

    /*
     * Step 2: Initialize GLCDC display subsystem
     *
     * This performs:
     *   a. LCD hardware reset (DISP_RESET pin pulse)
     *   b. RM_LVGL_PORT_Open():
     *      - R_GLCDC_Open() with g_display0_cfg (timing, layers, format)
     *      - R_GLCDC_Start() to begin display output
     *      - R_GLCDC_BufferChange() for double buffering setup
     *      - lv_display_create() and flush callback registration
     *   c. Backlight enable callback registration (fires after first flush)
     *
     * If initialization fails, the thread continues running but no
     * display output will be produced. The error can be checked via
     * the NT-Shell "display status" command.
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/new_thread0_entry.c:26
     */
    glcdc_port_init();

    /*
     * Step 3: LVGL main loop
     *
     * lv_timer_handler() processes all pending LVGL tasks:
     *   - Rendering invalidated areas to the active framebuffer
     *   - Running animation timers
     *   - Processing input device events
     *   - Executing user-defined timers
     *
     * The 1ms vTaskDelay matches the LVGL tick resolution (LV_DEF_REFR_PERIOD = 16ms)
     * and allows other FreeRTOS tasks to run.
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/new_thread0_entry.c:60-77
     */
    while (1) {
        lv_timer_handler();
        vTaskDelay(1);
    }
}
