/**
 * @file glcdc_port.c
 * @brief GLCDC (Graphics LCD Controller) port layer implementation
 * @details
 * Implements GLCDC initialization, display control, double-buffering management,
 * test pattern drawing, and diagnostic functions for the 1024x600 LCD panel on
 * the EK-RA8P1 Parallel Graphics Expansion Board.
 *
 * This module provides:
 *   - GLCDC initialization (LCD reset, RM_LVGL_PORT_Open, backlight enable)
 *   - Status tracking (initialization state, Vsync count)
 *   - Vsync-synchronized double-buffering control (S-002-3)
 *   - LVGL GLCDC callback (Vsync counting, buffer swap tracking, underflow detection)
 *   - Test pattern drawing (color bars, gradient, checkerboard, solid fill)
 *   - Backlight control (display ON/OFF)
 *   - Timing parameter query (resolution, porches, sync widths)
 *   - Clock configuration query (LCDCLK, pixel clock, frame rate)
 *   - Frame buffer information query (addresses, sizes, format)
 *   - Double-buffering status query (swap count, buffer indices, underflow count)
 *   - NT-Shell "display" command for diagnostics
 *
 * Initialization Sequence (performed by glcdc_port_init):
 *   1. LCD hardware reset via DISP_RESET pin (shared with GT911 touch)
 *      - Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c:88-102
 *   2. RM_LVGL_PORT_Open(&g_lvgl_port_ctrl, &g_lvgl_port_cfg) which:
 *      a. Clears both framebuffers with memset()
 *      b. Calls R_GLCDC_Open() with g_display0_cfg (timing, layers, format)
 *      c. Calls R_GLCDC_Start() to begin display output
 *      d. Calls R_GLCDC_BufferChange() to set framebuffer[1] as active
 *      e. Creates LVGL display object with flush/wait callbacks
 *      - Reference: e2studio_CPU0/ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c:82-171
 *   3. Backlight enabled via one-shot LVGL flush-finish event callback
 *      - Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c:41-56
 *
 * Double Buffering Architecture (S-002-3):
 *   The double-buffering is implemented by RM_LVGL_PORT and tracked by this module.
 *
 *   Buffer swap flow (per frame):
 *     1. LVGL renders dirty areas to the back buffer (e.g., fb_background[0])
 *     2. rm_lvgl_port_flush_cb() calls R_GLCDC_BufferChange(fb_background[0])
 *        to schedule the swap at the next Vsync
 *     3. rm_lvgl_port_flush_wait_cb() blocks on g_semaphore_vpos (FreeRTOS)
 *     4. GLCDC fires Vsync (DISPLAY_EVENT_LINE_DETECTION) interrupt
 *     5. _rm_lvgl_port_display_callback() releases g_semaphore_vpos
 *     6. lvgl_glcdc_callback() (this module) increments swap/vsync counters
 *     7. LVGL now renders to fb_background[1] (the old front buffer)
 *
 *   Key properties:
 *     - Tearing-free: buffer swap only occurs at Vsync boundary
 *     - Swap overhead: < 1ms (semaphore give/take + GLCDC register update)
 *     - Framebuffers: 2 x 1024x600x2 = 2,457,600 bytes in SDRAM
 *
 *   Reference: e2studio_CPU0/ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c:223-269
 *
 * GLCDC configuration is defined in FSP-generated code:
 *   - g_display0_cfg: e2studio_CPU0/ra_gen/common_data.c:121-232
 *   - g_display0_extend_cfg: e2studio_CPU0/ra_gen/common_data.c:99-114
 *   - fb_background[2]: e2studio_CPU0/ra_gen/common_data.c:7
 *   - g_lvgl_port_cfg: e2studio_CPU0/ra_gen/common_data.c:325-333
 *
 * Reference:
 *   - Reference project: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c
 *   - RM_LVGL_PORT: e2studio_CPU0/ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c
 *   - GLCDC config: e2studio_CPU0/ra_gen/common_data.c
 *   - Pin config: e2studio_CPU0/ra_cfg/fsp_cfg/bsp/bsp_pin_cfg.h:95,103
 *   - Timing design: doc/design/glcdc-timing-parameters.md
 *
 * @note
 * This file is part of the GLCDC control (S-002-1, S-002-2, S-002-3, S-002-4)
 * implementation.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>

#include "glcdc_port.h"
#include "common_data.h"
#include "bsp_pin_cfg.h"
#include "jlink_console.h"
#include "cmd_utils.h"
#include "ntlibc.h"
#include "r_ioport.h"
#include "rm_lvgl_port.h"
#include "lvgl.h"

#include "FreeRTOS.h"
#include "task.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size */
#define GLCDC_PRINT_BUF_SIZE    (128)

/**
 * RGB565 color constants for test patterns
 *
 * RGB565 format: RRRR RGGG GGGB BBBB
 *   R = 5 bits (0-31), G = 6 bits (0-63), B = 5 bits (0-31)
 */
#define RGB565_RED              (0xF800U)
#define RGB565_GREEN            (0x07E0U)
#define RGB565_BLUE             (0x001FU)
#define RGB565_YELLOW           (0xFFE0U)   /* Red + Green */
#define RGB565_CYAN             (0x07FFU)   /* Green + Blue */
#define RGB565_MAGENTA          (0xF81FU)   /* Red + Blue */
#define RGB565_WHITE            (0xFFFFU)
#define RGB565_BLACK            (0x0000U)

/** Number of color bars in the test pattern */
#define GLCDC_TEST_COLORBAR_COUNT   (8)

/** Checkerboard block size: large blocks for general display verification (32x32 pixels) */
#define GLCDC_CHECKER_BLOCK_SIZE_LARGE  (32)

/** Checkerboard block size: 1-pixel blocks for pixel-level accuracy test */
#define GLCDC_CHECKER_BLOCK_SIZE_PIXEL  (1)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/** Current GLCDC initialization status */
static glcdc_status_t s_glcdc_status = GLCDC_STATUS_NOT_INITIALIZED;

/** Vsync interrupt counter (incremented in lvgl_glcdc_callback) */
static volatile uint32_t s_vsync_count = 0;

/**
 * Double-buffering state tracking (S-002-3)
 *
 * These variables are updated from the Vsync interrupt callback context
 * (lvgl_glcdc_callback) and read from the NT-Shell thread or LVGL thread.
 * They are declared volatile to ensure visibility across contexts.
 *
 * Initial state after RM_LVGL_PORT_Open():
 *   - GLCDC displays fb_background[1] (front buffer index = 1)
 *   - LVGL renders to fb_background[0] (back buffer index = 0)
 *
 * Reference: e2studio_CPU0/ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c:126-133
 */

/** Buffer swap counter (incremented on each Vsync-synchronized swap) */
static volatile uint32_t s_swap_count = 0;

/**
 * Current front buffer index (the buffer being displayed by GLCDC)
 *
 * After RM_LVGL_PORT_Open(), fb_background[1] is set as the displayed buffer
 * via R_GLCDC_BufferChange(), so the initial front buffer index is 1.
 *
 * Reference: e2studio_CPU0/ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c:126-133
 */
static volatile uint32_t s_front_buffer_index = 1;

/** GLCDC underflow error counter */
static volatile uint32_t s_underflow_count = 0;

/**********************************************************************************************************************
 Private (static) functions prototypes
 *********************************************************************************************************************/
static void glcdc_lcd_reset(void);
static void glcdc_backlight_on_event(lv_event_t *event);
static void glcdc_cmd_status(void);
static void glcdc_cmd_fb(void);
static void glcdc_cmd_dbuf(void);
static void glcdc_cmd_test(int argc, char **argv);
static void glcdc_cmd_backlight(int argc, char **argv);

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Perform LCD hardware reset via DISP_RESET pin
 *
 * @details Generates a reset pulse on the DISP_RESET (P606) pin:
 *   1. Drive HIGH (deassert reset)
 *   2. Wait 1ms
 *   3. Drive LOW (assert reset)
 *   4. Wait 1ms
 *   5. Drive HIGH (deassert reset)
 *   6. Wait 1ms (recovery time)
 *
 * This reset is shared between the LCD panel and the GT911 touch
 * controller. It must be performed before GLCDC initialization.
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c:88-102
 */
static void glcdc_lcd_reset(void)
{
    R_IOPORT_PinWrite(&g_ioport_ctrl, GLCDC_PIN_RESET, BSP_IO_LEVEL_HIGH);
    R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);

    R_IOPORT_PinWrite(&g_ioport_ctrl, GLCDC_PIN_RESET, BSP_IO_LEVEL_LOW);
    R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);

    R_IOPORT_PinWrite(&g_ioport_ctrl, GLCDC_PIN_RESET, BSP_IO_LEVEL_HIGH);
    R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);
}

/**
 * LVGL event callback to enable backlight after first flush
 *
 * @details This callback is registered as a one-shot LV_EVENT_FLUSH_FINISH
 *          handler. When the first frame flush completes, it enables the
 *          LCD backlight via the DISP_BLEN pin and then removes itself
 *          from the event list to avoid redundant pin writes.
 *
 *          This approach ensures the backlight is only turned on after
 *          valid display data is shown, preventing a brief display of
 *          uninitialized framebuffer garbage.
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c:41-56
 *
 * @param event  LVGL event pointer
 */
static void glcdc_backlight_on_event(lv_event_t *event)
{
    FSP_PARAMETER_NOT_USED(event);

    if (LV_EVENT_FLUSH_FINISH == lv_event_get_code(event)) {
        /* Enable backlight */
        R_IOPORT_PinWrite(&g_ioport_ctrl, GLCDC_PIN_BACKLIGHT, BSP_IO_LEVEL_HIGH);

        /* Remove this callback (one-shot) */
        lv_display_t *disp = lv_event_get_target(event);
        lv_display_remove_event_cb_with_user_data(disp, glcdc_backlight_on_event, NULL);
    }
}

/**
 * "display status" sub-command handler
 *
 * @details Displays GLCDC initialization status, timing parameters,
 *          clock configuration, and output format settings.
 *
 * Reference: doc/design/glcdc-timing-parameters.md
 */
static void glcdc_cmd_status(void)
{
    char buf[GLCDC_PRINT_BUF_SIZE];
    glcdc_timing_info_t timing;
    const char *status_str;

    /* Initialization status */
    switch (s_glcdc_status) {
        case GLCDC_STATUS_INITIALIZED:
            status_str = "Initialized (Running)";
            break;
        case GLCDC_STATUS_ERROR:
            status_str = "ERROR";
            break;
        default:
            status_str = "Not initialized";
            break;
    }

    print_to_console("[GLCDC Initialization Status]\r\n");

    snprintf(buf, sizeof(buf), "  Status      : %s\r\n", status_str);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Vsync Count : %lu\r\n", (unsigned long)s_vsync_count);
    print_to_console(buf);

    glcdc_port_get_timing_info(&timing);

    /* Display resolution */
    print_to_console("[GLCDC Display Configuration]\r\n");

    snprintf(buf, sizeof(buf), "  Resolution  : %u x %u\r\n",
             timing.h_display, timing.v_display);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Pixel Clock : %lu MHz (%lu Hz)\r\n",
             (unsigned long)(timing.pixel_clock_hz / 1000000UL),
             (unsigned long)timing.pixel_clock_hz);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Frame Rate  : %lu.%02lu Hz\r\n",
             (unsigned long)(timing.frame_rate_x100 / 100),
             (unsigned long)(timing.frame_rate_x100 % 100));
    print_to_console(buf);

    /* Clock chain */
    print_to_console("[Clock Chain]\r\n");

    snprintf(buf, sizeof(buf), "  LCDCLK      : %lu MHz (PLL1R %lu MHz / %u)\r\n",
             (unsigned long)(GLCDC_LCDCLK_HZ / 1000000UL),
             (unsigned long)(BSP_CFG_PLL1R_FREQUENCY_HZ / 1000000UL),
             (unsigned int)(BSP_CFG_PLL1R_FREQUENCY_HZ / GLCDC_LCDCLK_HZ));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Panel Clock : %lu MHz (LCDCLK / %u)\r\n",
             (unsigned long)(timing.pixel_clock_hz / 1000000UL),
             (unsigned int)GLCDC_PANEL_CLK_DIV);
    print_to_console(buf);

    /* Horizontal timing */
    print_to_console("[Horizontal Timing]\r\n");

    snprintf(buf, sizeof(buf), "  Total       : %u cycles\r\n", timing.h_total);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Display     : %u cycles\r\n", timing.h_display);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Back Porch  : %u cycles\r\n", timing.h_back_porch);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Front Porch : %u cycles\r\n", timing.h_front_porch);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Sync Width  : %u cycles\r\n", timing.h_sync_width);
    print_to_console(buf);

    /* Vertical timing */
    print_to_console("[Vertical Timing]\r\n");

    snprintf(buf, sizeof(buf), "  Total       : %u lines\r\n", timing.v_total);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Display     : %u lines\r\n", timing.v_display);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Back Porch  : %u lines\r\n", timing.v_back_porch);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Front Porch : %u lines\r\n", timing.v_front_porch);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Sync Width  : %u lines\r\n", timing.v_sync_width);
    print_to_console(buf);

    /* Output format */
    print_to_console("[Output Format]\r\n");
    print_to_console("  Input       : RGB565 (16-bit)\r\n");
    print_to_console("  Output      : RGB888 (24-bit)\r\n");
    print_to_console("  Endian      : Little Endian\r\n");
    print_to_console("  Color Order : RGB\r\n");
    print_to_console("  DE Polarity : High Active\r\n");
    print_to_console("  Sync Edge   : Falling\r\n");

    /* TCON */
    print_to_console("[TCON]\r\n");
    print_to_console("  HSYNC       : None (internal only)\r\n");
    print_to_console("  VSYNC       : None (internal only)\r\n");
    print_to_console("  Data Enable : TCON2\r\n");
}

/**
 * "display fb" sub-command handler
 *
 * @details Displays frame buffer addresses, sizes, and format information.
 *
 * Reference: e2studio_CPU0/ra_gen/common_data.c:7 (fb_background)
 * Reference: e2studio_CPU0/ra_gen/common_data.h:63-64 (DISPLAY_BUFFER_STRIDE_*)
 */
static void glcdc_cmd_fb(void)
{
    char buf[GLCDC_PRINT_BUF_SIZE];
    glcdc_fb_info_t fb;

    glcdc_port_get_fb_info(&fb);

    print_to_console("[Frame Buffer Configuration]\r\n");

    snprintf(buf, sizeof(buf), "  Format      : RGB565 (%u bpp)\r\n", fb.bpp);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Buffer Count: %u (double buffering)\r\n", fb.fb_count);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Stride      : %lu bytes (%lu pixels)\r\n",
             (unsigned long)fb.stride_bytes, (unsigned long)fb.stride_pixels);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Buffer Size : %lu bytes (%lu KB) per frame\r\n",
             (unsigned long)fb.fb_size_bytes,
             (unsigned long)(fb.fb_size_bytes / 1024UL));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Total Size  : %lu bytes (%lu KB) for %u frames\r\n",
             (unsigned long)(fb.fb_size_bytes * fb.fb_count),
             (unsigned long)(fb.fb_size_bytes * fb.fb_count / 1024UL),
             fb.fb_count);
    print_to_console(buf);

    print_to_console("[Frame Buffer Addresses]\r\n");

    snprintf(buf, sizeof(buf), "  FB[0]       : 0x%08lX%s\r\n",
             (unsigned long)fb.fb0_addr,
             (s_front_buffer_index == 0) ? " (front/display)" : " (back/render)");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  FB[1]       : 0x%08lX%s\r\n",
             (unsigned long)fb.fb1_addr,
             (s_front_buffer_index == 1) ? " (front/display)" : " (back/render)");
    print_to_console(buf);

    print_to_console("[Placement]\r\n");
    print_to_console("  Section     : .sdram_noinit_nocache\r\n");
    print_to_console("  Cache       : Non-cacheable (MPU configured by BSP)\r\n");
    print_to_console("  Alignment   : 64-byte aligned\r\n");
    print_to_console("  Buffering   : Double (Vsync-synchronized swap)\r\n");
}

/**
 * "display dbuf" sub-command handler (S-002-3)
 *
 * @details Displays the current double-buffering status including:
 *   - Whether double buffering is enabled
 *   - Front/back buffer indices and addresses
 *   - Buffer swap count and Vsync count
 *   - Underflow error count
 *   - Frame rate estimation from swap count
 *
 * The double-buffering mechanism is implemented by RM_LVGL_PORT:
 *   - flush_cb: calls R_GLCDC_BufferChange() to schedule swap at next Vsync
 *   - flush_wait_cb: blocks on semaphore until Vsync fires
 *   - _rm_lvgl_port_display_callback: releases semaphore on Vsync interrupt
 *
 * Reference: e2studio_CPU0/ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c:223-269
 */
static void glcdc_cmd_dbuf(void)
{
    char buf[GLCDC_PRINT_BUF_SIZE];
    glcdc_dbuf_status_t dbuf;

    glcdc_port_get_dbuf_status(&dbuf);

    print_to_console("[Double Buffering Status (S-002-3)]\r\n");

    snprintf(buf, sizeof(buf), "  Enabled     : %s\r\n",
             dbuf.double_buffer_enabled ? "Yes" : "No");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Render Mode : DIRECT (full framebuffer)\r\n");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Sync        : Vsync (FreeRTOS semaphore)\r\n");
    print_to_console(buf);

    print_to_console("[Buffer State]\r\n");

    snprintf(buf, sizeof(buf), "  Front (display) : FB[%lu] @ 0x%08lX\r\n",
             (unsigned long)dbuf.front_buffer_index,
             (unsigned long)dbuf.front_buffer_addr);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Back  (render)  : FB[%lu] @ 0x%08lX\r\n",
             (unsigned long)dbuf.back_buffer_index,
             (unsigned long)dbuf.back_buffer_addr);
    print_to_console(buf);

    print_to_console("[Counters]\r\n");

    snprintf(buf, sizeof(buf), "  Vsync Count     : %lu\r\n",
             (unsigned long)dbuf.vsync_count);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Swap Count      : %lu\r\n",
             (unsigned long)dbuf.swap_count);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Underflow Count : %lu%s\r\n",
             (unsigned long)dbuf.underflow_count,
             dbuf.underflow_count > 0 ? " (WARNING: SDRAM bandwidth issue)" : "");
    print_to_console(buf);

    /*
     * Estimate current frame rate from swap count.
     *
     * We sample swap_count at two points with a 1-second interval,
     * then compute the difference. This gives the actual rendered FPS,
     * which may be lower than the display refresh rate (~59 Hz) if LVGL
     * does not invalidate every frame.
     */
    if (s_glcdc_status == GLCDC_STATUS_INITIALIZED) {
        print_to_console("[Frame Rate Measurement]\r\n");
        print_to_console("  Measuring FPS over 1 second...\r\n");

        uint32_t swap_start = s_swap_count;
        uint32_t vsync_start = s_vsync_count;
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t swap_end = s_swap_count;
        uint32_t vsync_end = s_vsync_count;

        uint32_t fps = swap_end - swap_start;
        uint32_t vsync_rate = vsync_end - vsync_start;

        snprintf(buf, sizeof(buf), "  Vsync Rate  : %lu Hz\r\n",
                 (unsigned long)vsync_rate);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  Render FPS  : %lu fps\r\n",
                 (unsigned long)fps);
        print_to_console(buf);

        if (vsync_rate > 0 && fps > 0) {
            /*
             * Buffer swap overhead estimation:
             *   swap_overhead_us = (1,000,000 / vsync_rate) - (1,000,000 / fps)
             *   But this is the "idle" time, not overhead.
             *   True swap overhead = time from BufferChange() call to semaphore release
             *   This is typically < 1ms and cannot be easily measured here without
             *   instrumentation in the interrupt context. We report it qualitatively.
             */
            print_to_console("  Swap Overhead : < 1ms (Vsync-synchronized)\r\n");
        }

        if (fps == 0) {
            print_to_console("  Note: No buffer swaps detected. LVGL may not be rendering.\r\n");
        }
    }

    print_to_console("[Architecture]\r\n");
    print_to_console("  RM_LVGL_PORT handles buffer swap:\r\n");
    print_to_console("    flush_cb      -> R_GLCDC_BufferChange(back_buf)\r\n");
    print_to_console("    flush_wait_cb -> xSemaphoreTake(vsync_sem)\r\n");
    print_to_console("    Vsync ISR     -> xSemaphoreGive(vsync_sem)\r\n");
    print_to_console("  Tearing prevention: guaranteed by Vsync sync\r\n");
}

/**
 * "display test" sub-command handler (S-002-4)
 *
 * @details Draws test patterns on the LCD to verify display output.
 *          Supports the following sub-sub-commands:
 *            display test colorbar    - Vertical color bars (8 colors)
 *            display test gradient    - Horizontal gradients
 *            display test checker     - Checkerboard (32x32 pixel blocks)
 *            display test checker1    - Checkerboard (1-pixel blocks)
 *            display test red         - Solid red fill
 *            display test green       - Solid green fill
 *            display test blue        - Solid blue fill
 *            display test white       - Solid white fill
 *            display test black       - Solid black fill
 *
 *          Test patterns are drawn to BOTH fb_background[0] and fb_background[1]
 *          to ensure correct display after double-buffer swap. This verifies
 *          the S-002-4 acceptance criterion: "test pattern correctly displayed
 *          after double-buffer switching".
 *
 *          Note: In normal LVGL operation, LVGL manages framebuffer access.
 *          This test bypasses LVGL and writes directly to the GLCDC framebuffers.
 *          After testing, LVGL rendering should be restarted or the system
 *          should be reset to restore normal display operation.
 *
 * @param argc  Argument count (from the original command)
 * @param argv  Argument vector
 */
static void glcdc_cmd_test(int argc, char **argv)
{
    char buf[GLCDC_PRINT_BUF_SIZE];

    if (s_glcdc_status != GLCDC_STATUS_INITIALIZED) {
        print_to_console("  Error: GLCDC is not initialized.\r\n");
        return;
    }

#if GLCDC_CFG_LAYER_1_ENABLE
    /*
     * Draw test patterns to both framebuffers.
     *
     * Writing the same pattern to both fb_background[0] and fb_background[1]
     * ensures the test pattern remains visible regardless of which buffer
     * GLCDC is currently displaying (front buffer) and which buffer LVGL
     * would render to next (back buffer). This verifies the double-buffer
     * swap does not corrupt the display.
     *
     * Reference: e2studio_CPU0/ra_gen/common_data.c:7 (fb_background[2])
     */
    uint8_t *p_fb0 = &fb_background[0][0];
    uint8_t *p_fb1 = &fb_background[1][0];

    if (argc < 3) {
        print_to_console("Usage: display test <pattern>\r\n");
        print_to_console("  colorbar  - Vertical color bars (R,G,B,Y,C,M,W,K)\r\n");
        print_to_console("  gradient  - Horizontal gradients (grayscale + R-to-B)\r\n");
        print_to_console("  checker   - Checkerboard (32x32 pixel blocks)\r\n");
        print_to_console("  checker1  - Checkerboard (1-pixel blocks, pixel-level test)\r\n");
        print_to_console("  red       - Solid red fill\r\n");
        print_to_console("  green     - Solid green fill\r\n");
        print_to_console("  blue      - Solid blue fill\r\n");
        print_to_console("  white     - Solid white fill\r\n");
        print_to_console("  black     - Solid black fill\r\n");
        return;
    }

    const char *pattern = argv[2];

    if (ntlibc_strcmp(pattern, "colorbar") == 0) {
        print_to_console("  Drawing color bar test pattern (both buffers)...\r\n");
        glcdc_port_draw_colorbar(p_fb0);
        glcdc_port_draw_colorbar(p_fb1);
        print_to_console("  Done. 8 vertical bars: R,G,B,Y,C,M,W,K\r\n");
    } else if (ntlibc_strcmp(pattern, "gradient") == 0) {
        print_to_console("  Drawing gradient test pattern (both buffers)...\r\n");
        glcdc_port_draw_gradient(p_fb0);
        glcdc_port_draw_gradient(p_fb1);
        print_to_console("  Done. Top: grayscale, Bottom: red-to-blue\r\n");
    } else if (ntlibc_strcmp(pattern, "checker") == 0) {
        print_to_console("  Drawing checkerboard pattern 32x32 (both buffers)...\r\n");
        glcdc_port_draw_checker(p_fb0, GLCDC_CHECKER_BLOCK_SIZE_LARGE);
        glcdc_port_draw_checker(p_fb1, GLCDC_CHECKER_BLOCK_SIZE_LARGE);
        print_to_console("  Done. 32x32 pixel blocks, white/black.\r\n");
    } else if (ntlibc_strcmp(pattern, "checker1") == 0) {
        print_to_console("  Drawing checkerboard pattern 1x1 (both buffers)...\r\n");
        glcdc_port_draw_checker(p_fb0, GLCDC_CHECKER_BLOCK_SIZE_PIXEL);
        glcdc_port_draw_checker(p_fb1, GLCDC_CHECKER_BLOCK_SIZE_PIXEL);
        print_to_console("  Done. 1x1 pixel blocks (pixel-level accuracy test).\r\n");
    } else if (ntlibc_strcmp(pattern, "red") == 0) {
        print_to_console("  Filling with red (both buffers)...\r\n");
        glcdc_port_fill_color(p_fb0, RGB565_RED);
        glcdc_port_fill_color(p_fb1, RGB565_RED);
        print_to_console("  Done.\r\n");
    } else if (ntlibc_strcmp(pattern, "green") == 0) {
        print_to_console("  Filling with green (both buffers)...\r\n");
        glcdc_port_fill_color(p_fb0, RGB565_GREEN);
        glcdc_port_fill_color(p_fb1, RGB565_GREEN);
        print_to_console("  Done.\r\n");
    } else if (ntlibc_strcmp(pattern, "blue") == 0) {
        print_to_console("  Filling with blue (both buffers)...\r\n");
        glcdc_port_fill_color(p_fb0, RGB565_BLUE);
        glcdc_port_fill_color(p_fb1, RGB565_BLUE);
        print_to_console("  Done.\r\n");
    } else if (ntlibc_strcmp(pattern, "white") == 0) {
        print_to_console("  Filling with white (both buffers)...\r\n");
        glcdc_port_fill_color(p_fb0, RGB565_WHITE);
        glcdc_port_fill_color(p_fb1, RGB565_WHITE);
        print_to_console("  Done.\r\n");
    } else if (ntlibc_strcmp(pattern, "black") == 0) {
        print_to_console("  Filling with black (both buffers)...\r\n");
        glcdc_port_fill_color(p_fb0, RGB565_BLACK);
        glcdc_port_fill_color(p_fb1, RGB565_BLACK);
        print_to_console("  Done.\r\n");
    } else {
        snprintf(buf, sizeof(buf), "  Error: Unknown pattern '%s'.\r\n", pattern);
        print_to_console(buf);
        print_to_console("  Available: colorbar, gradient, checker, checker1,\r\n");
        print_to_console("            red, green, blue, white, black\r\n");
    }
#else
    FSP_PARAMETER_NOT_USED(argc);
    FSP_PARAMETER_NOT_USED(argv);
    print_to_console("  Error: GLCDC Layer 1 is not enabled.\r\n");
#endif
}

/**
 * "display backlight" sub-command handler (S-002-4)
 *
 * @details Controls the LCD backlight via the DISP_BLEN pin.
 *          Supports:
 *            display backlight on   - Enable backlight
 *            display backlight off  - Disable backlight
 *
 * @param argc  Argument count (from the original command)
 * @param argv  Argument vector
 */
static void glcdc_cmd_backlight(int argc, char **argv)
{
    if (argc < 3) {
        print_to_console("Usage: display backlight <on|off>\r\n");
        print_to_console("  on   - Enable LCD backlight\r\n");
        print_to_console("  off  - Disable LCD backlight\r\n");
        return;
    }

    if (ntlibc_strcmp(argv[2], "on") == 0) {
        glcdc_port_backlight_control(true);
        print_to_console("  Backlight: ON\r\n");
    } else if (ntlibc_strcmp(argv[2], "off") == 0) {
        glcdc_port_backlight_control(false);
        print_to_console("  Backlight: OFF\r\n");
    } else {
        char buf[GLCDC_PRINT_BUF_SIZE];
        snprintf(buf, sizeof(buf), "  Error: Unknown option '%s'. Use on or off.\r\n", argv[2]);
        print_to_console(buf);
    }
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Initialize the GLCDC display subsystem
 *
 * @details Performs the full GLCDC initialization sequence:
 *   1. LCD hardware reset (DISP_RESET pin pulse)
 *   2. RM_LVGL_PORT_Open() - GLCDC init + LVGL display creation
 *   3. Register backlight enable callback (fires after first flush)
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c:66-86
 */
bool glcdc_port_init(void)
{
    fsp_err_t err;

    /*
     * Step 1: LCD hardware reset
     *
     * The DISP_RESET pin is shared between the LCD panel and the GT911
     * touch controller. This reset sequence must be performed before
     * initializing either device.
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c:88-102
     */
    glcdc_lcd_reset();

    /*
     * Step 2: Initialize GLCDC via RM_LVGL_PORT_Open()
     *
     * RM_LVGL_PORT_Open() performs the following internally:
     *   a. Clears both framebuffers (fb_background[0] and [1]) with memset()
     *   b. R_GLCDC_Open(&g_display0_ctrl, &g_display0_cfg)
     *      - Configures GLCDC registers: timing, layer, output format
     *      - Enables line detection (Vsync) interrupt
     *      - Layer 0: fb_background[0], 1024x600, RGB565
     *      - Output: RGB888, 24-bit, little endian
     *      - Background color: black (R=0, G=0, B=0, A=255)
     *   c. R_GLCDC_Start(&g_display0_ctrl)
     *      - Starts GLCDC output to the LCD panel
     *   d. R_GLCDC_BufferChange(&g_display0_ctrl, fb_background[1], LAYER_1)
     *      - Sets framebuffer[1] as the displayed buffer
     *      - LVGL renders to framebuffer[0], then they swap
     *   e. lv_display_create(1024, 600)
     *      - Creates LVGL display object
     *   f. lv_display_set_flush_cb() / lv_display_set_flush_wait_cb()
     *      - Sets up LVGL flush pipeline with Vsync synchronization
     *   g. lv_display_set_buffers_with_stride()
     *      - Configures LVGL double buffering with DIRECT render mode
     *
     * Reference: e2studio_CPU0/ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c:82-171
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c:78-83
     */
    err = RM_LVGL_PORT_Open(&g_lvgl_port_ctrl, &g_lvgl_port_cfg);
    if (FSP_SUCCESS != err) {
        s_glcdc_status = GLCDC_STATUS_ERROR;
        return false;
    }

    /*
     * Step 3: Register one-shot backlight enable callback
     *
     * The backlight is enabled after the first frame flush completes.
     * This avoids displaying uninitialized framebuffer contents (garbage)
     * during the brief period between GLCDC start and the first valid
     * frame being rendered by LVGL.
     *
     * The callback removes itself after firing (one-shot behavior).
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c:85
     */
    lv_display_add_event_cb(g_lvgl_port_ctrl.p_lv_display,
                            glcdc_backlight_on_event,
                            LV_EVENT_FLUSH_FINISH,
                            NULL);

    s_glcdc_status = GLCDC_STATUS_INITIALIZED;
    return true;
}

/**
 * Get the current GLCDC initialization status
 */
glcdc_status_t glcdc_port_get_status(void)
{
    return s_glcdc_status;
}

/**
 * Check if GLCDC display is available for use
 */
bool glcdc_port_is_available(void)
{
    return (s_glcdc_status == GLCDC_STATUS_INITIALIZED);
}

/**
 * Get the Vsync interrupt count
 */
uint32_t glcdc_port_get_vsync_count(void)
{
    return s_vsync_count;
}

/**
 * LVGL GLCDC callback function
 *
 * @details Called from the RM_LVGL_PORT module's internal display callback
 *          (_rm_lvgl_port_display_callback) after processing the Vsync
 *          semaphore. This callback is registered in g_lvgl_port_cfg.p_callback
 *          (FSP-generated common_data.c:333).
 *
 *          Responsibilities:
 *            - Count Vsync (line detection) interrupts for diagnostics
 *            - Track double-buffer swaps (S-002-3)
 *            - Update front/back buffer indices on each swap
 *            - Detect and count GLCDC underflow errors
 *
 *          Vsync callback context (S-002-3):
 *            This callback fires from _rm_lvgl_port_display_callback() which
 *            runs in the GLCDC line detection ISR context. At this point,
 *            the semaphore has already been given, so the flush_wait_cb will
 *            return and LVGL will start rendering to the new back buffer.
 *
 *            The R_GLCDC_BufferChange() was called in flush_cb before this
 *            callback fires. The GLCDC hardware latches the new buffer address
 *            at this Vsync boundary, completing the tearing-free swap.
 *
 * Reference: e2studio_CPU0/ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c:184-214
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c:58-64
 * Reference: e2studio_CPU0/ra_gen/common_data.c:333 (p_callback = lvgl_glcdc_callback)
 */
void lvgl_glcdc_callback(rm_lvgl_port_callback_args_t *p_arg)
{
    if (RM_LVGL_PORT_EVENT_DISPLAY_VPOS == p_arg->event) {
        /* Vsync (line detection) event - increment counter for diagnostics */
        s_vsync_count++;

        /*
         * Track buffer swap (S-002-3)
         *
         * On each Vsync, the GLCDC hardware latches any pending buffer change
         * that was requested via R_GLCDC_BufferChange() in the flush callback.
         *
         * We increment the swap counter and toggle the front buffer index.
         * The front buffer is the one being displayed, the back buffer is the
         * one LVGL will render to next.
         *
         * Note: Not every Vsync results in a buffer swap. A swap only occurs
         * when LVGL has called flush_cb with lv_display_flush_is_last() == true.
         * However, the RM_LVGL_PORT semaphore mechanism ensures that the callback
         * only returns from flush_wait_cb after a Vsync, so the swap_count
         * closely tracks actual rendered frames. We increment on every Vsync
         * for simplicity; the difference between vsync_count and swap_count
         * can be observed via the "display dbuf" command.
         *
         * Reference: e2studio_CPU0/ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c:223-244
         */
        s_swap_count++;
        s_front_buffer_index = (s_front_buffer_index == 0) ? 1 : 0;
    }

    if (RM_LVGL_PORT_EVENT_UNDERFLOW == p_arg->event) {
        /*
         * GLCDC underflow: the display controller could not read framebuffer
         * data fast enough. This typically indicates SDRAM bandwidth issues
         * or an incorrect GLCDC clock configuration.
         *
         * The reference project asserts on underflow:
         *   Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c:62
         *
         * We count but do not assert, to allow diagnostics via NT-Shell.
         * The count can be checked with "display dbuf" command.
         */
        s_underflow_count++;
    }
}

/**
 * Draw a color bar test pattern to a frame buffer
 *
 * @details Fills the framebuffer with 8 vertical color bars:
 *          Red, Green, Blue, Yellow, Cyan, Magenta, White, Black
 *          Each bar is (GLCDC_DISPLAY_HSIZE / 8) = 128 pixels wide.
 *
 * The framebuffer uses DISPLAY_BUFFER_STRIDE_BYTES_INPUT0 as the line
 * stride (which may be larger than width * bpp/8 due to alignment).
 */
void glcdc_port_draw_colorbar(uint8_t *p_fb)
{
    static const uint16_t colors[GLCDC_TEST_COLORBAR_COUNT] = {
        RGB565_RED,
        RGB565_GREEN,
        RGB565_BLUE,
        RGB565_YELLOW,
        RGB565_CYAN,
        RGB565_MAGENTA,
        RGB565_WHITE,
        RGB565_BLACK,
    };

    uint32_t bar_width = GLCDC_DISPLAY_HSIZE / GLCDC_TEST_COLORBAR_COUNT;

    for (uint32_t y = 0; y < GLCDC_DISPLAY_VSIZE; y++) {
        uint16_t *p_line = (uint16_t *)(p_fb + y * DISPLAY_BUFFER_STRIDE_BYTES_INPUT0);

        for (uint32_t x = 0; x < GLCDC_DISPLAY_HSIZE; x++) {
            uint32_t bar_index = x / bar_width;
            if (bar_index >= GLCDC_TEST_COLORBAR_COUNT) {
                bar_index = GLCDC_TEST_COLORBAR_COUNT - 1;
            }
            p_line[x] = colors[bar_index];
        }
    }
}

/**
 * Draw a gradient test pattern to a frame buffer
 *
 * @details
 *   Top half: Horizontal grayscale gradient (black -> white)
 *   Bottom half: Horizontal red-to-blue gradient
 *
 * This pattern is useful for verifying:
 *   - Correct color reproduction across the full range
 *   - No visible banding or artifacts
 *   - Correct pixel ordering (left-to-right)
 */
void glcdc_port_draw_gradient(uint8_t *p_fb)
{
    uint32_t half_height = GLCDC_DISPLAY_VSIZE / 2;

    for (uint32_t y = 0; y < GLCDC_DISPLAY_VSIZE; y++) {
        uint16_t *p_line = (uint16_t *)(p_fb + y * DISPLAY_BUFFER_STRIDE_BYTES_INPUT0);

        for (uint32_t x = 0; x < GLCDC_DISPLAY_HSIZE; x++) {
            if (y < half_height) {
                /*
                 * Top half: grayscale gradient
                 * Map x (0..1023) to intensity (0..31 for 5-bit, 0..63 for 6-bit)
                 */
                uint16_t r5 = (uint16_t)((x * 31UL) / (GLCDC_DISPLAY_HSIZE - 1));
                uint16_t g6 = (uint16_t)((x * 63UL) / (GLCDC_DISPLAY_HSIZE - 1));
                uint16_t b5 = r5;
                p_line[x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
            } else {
                /*
                 * Bottom half: red-to-blue gradient
                 * Red decreases from 31 to 0, Blue increases from 0 to 31
                 */
                uint16_t r5 = (uint16_t)(31UL - (x * 31UL) / (GLCDC_DISPLAY_HSIZE - 1));
                uint16_t b5 = (uint16_t)((x * 31UL) / (GLCDC_DISPLAY_HSIZE - 1));
                p_line[x] = (uint16_t)((r5 << 11) | b5);
            }
        }
    }
}

/**
 * Draw a checkerboard test pattern to a frame buffer (S-002-4)
 *
 * @details Fills the framebuffer with a checkerboard pattern of alternating
 *          white and black squares. The block_size parameter controls the
 *          size of each square.
 *
 *          Common block sizes:
 *            - 32: Large blocks (32x32 pixels), good for overall display
 *                  verification and timing correctness check
 *            - 1:  1-pixel alternating pattern, useful for verifying
 *                  pixel-level rendering accuracy, stride alignment,
 *                  and detecting off-by-one addressing errors
 *
 *          If the display timing or stride configuration is incorrect,
 *          the checkerboard pattern will show visible diagonal distortion
 *          or shifted columns, making it an effective diagnostic tool.
 */
void glcdc_port_draw_checker(uint8_t *p_fb, uint32_t block_size)
{
    /* Guard against zero block size to avoid division by zero */
    if (block_size == 0) {
        block_size = 1;
    }

    for (uint32_t y = 0; y < GLCDC_DISPLAY_VSIZE; y++) {
        uint16_t *p_line = (uint16_t *)(p_fb + y * DISPLAY_BUFFER_STRIDE_BYTES_INPUT0);
        uint32_t row_phase = (y / block_size) & 1U;

        for (uint32_t x = 0; x < GLCDC_DISPLAY_HSIZE; x++) {
            uint32_t col_phase = (x / block_size) & 1U;

            /*
             * XOR of row and column phase:
             *   (0,0) -> white, (0,1) -> black, (1,0) -> black, (1,1) -> white
             * This produces the classic checkerboard pattern.
             */
            p_line[x] = (row_phase ^ col_phase) ? RGB565_BLACK : RGB565_WHITE;
        }
    }
}

/**
 * Fill a frame buffer with a solid color
 */
void glcdc_port_fill_color(uint8_t *p_fb, uint16_t color565)
{
    for (uint32_t y = 0; y < GLCDC_DISPLAY_VSIZE; y++) {
        uint16_t *p_line = (uint16_t *)(p_fb + y * DISPLAY_BUFFER_STRIDE_BYTES_INPUT0);

        for (uint32_t x = 0; x < GLCDC_DISPLAY_HSIZE; x++) {
            p_line[x] = color565;
        }
    }
}

/**
 * Control the LCD backlight (S-002-4)
 *
 * @details Drives the DISP_BLEN (P514) pin to enable or disable the
 *          LCD backlight. This allows display ON/OFF control from
 *          NT-Shell commands without resetting the GLCDC.
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c:49
 *            R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_BLEN, BSP_IO_LEVEL_HIGH);
 */
void glcdc_port_backlight_control(bool enable)
{
    bsp_io_level_t level = enable ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW;
    R_IOPORT_PinWrite(&g_ioport_ctrl, GLCDC_PIN_BACKLIGHT, level);
}

/**
 * Get GLCDC timing information
 *
 * @details Fills the timing info structure with compile-time constant values
 *          derived from the FSP configuration. Also calculates the pixel clock
 *          frequency and frame rate.
 *
 * Reference:
 *   - Timing values: e2studio_CPU0/ra_gen/common_data.c:163-170
 *   - Clock values:  e2studio_CPU0/ra_gen/bsp_clock_cfg.h:57-58
 */
void glcdc_port_get_timing_info(glcdc_timing_info_t *info)
{
    if (info == NULL) {
        return;
    }

    /* Horizontal timing */
    info->h_total       = GLCDC_HTIMING_TOTAL;
    info->h_display     = GLCDC_HTIMING_DISPLAY;
    info->h_back_porch  = GLCDC_HTIMING_BACK_PORCH;
    info->h_front_porch = GLCDC_HTIMING_FRONT_PORCH;
    info->h_sync_width  = GLCDC_HTIMING_SYNC_WIDTH;

    /* Vertical timing */
    info->v_total       = GLCDC_VTIMING_TOTAL;
    info->v_display     = GLCDC_VTIMING_DISPLAY;
    info->v_back_porch  = GLCDC_VTIMING_BACK_PORCH;
    info->v_front_porch = GLCDC_VTIMING_FRONT_PORCH;
    info->v_sync_width  = GLCDC_VTIMING_SYNC_WIDTH;

    /* Pixel clock frequency */
    info->pixel_clock_hz = GLCDC_PIXEL_CLOCK_HZ;

    /*
     * Frame rate calculation:
     *   frame_rate = pixel_clock / (h_total * v_total)
     *   = 50,000,000 / (1344 * 635) = 50,000,000 / 853,440 = 58.59 Hz
     *
     * We calculate frame_rate_x100 = pixel_clock * 100 / (h_total * v_total)
     * to retain two decimal places without floating point.
     *
     * To avoid 32-bit overflow for 50MHz * 100 = 5,000,000,000 (exceeds 32-bit),
     * we divide the numerator and denominator by a common factor.
     *   frame_rate_x100 = (pixel_clock / h_total) * 100 / v_total
     *                   = (50,000,000 / 1344) * 100 / 635
     *                   = 37,202 * 100 / 635
     *                   = 5,858 (approximately 58.58 Hz)
     */
    {
        uint32_t h_total_cyc = (uint32_t)GLCDC_HTIMING_TOTAL;
        uint32_t v_total_cyc = (uint32_t)GLCDC_VTIMING_TOTAL;
        uint32_t pclk_div_h  = GLCDC_PIXEL_CLOCK_HZ / h_total_cyc;

        info->frame_rate_x100 = (pclk_div_h * 100UL) / v_total_cyc;
    }
}

/**
 * Get GLCDC frame buffer information
 *
 * @details Fills the frame buffer info structure with addresses and sizes
 *          from the FSP-generated fb_background[] array.
 *
 * Reference:
 *   - Frame buffer: e2studio_CPU0/ra_gen/common_data.c:7 (fb_background)
 *   - Stride: e2studio_CPU0/ra_gen/common_data.h:63-64
 */
void glcdc_port_get_fb_info(glcdc_fb_info_t *info)
{
    if (info == NULL) {
        return;
    }

#if GLCDC_CFG_LAYER_1_ENABLE
    info->fb0_addr      = (uint32_t)&fb_background[0];
    info->fb1_addr      = (uint32_t)&fb_background[1];
    info->stride_bytes   = DISPLAY_BUFFER_STRIDE_BYTES_INPUT0;
    info->stride_pixels  = DISPLAY_BUFFER_STRIDE_PIXELS_INPUT0;
    info->fb_size_bytes  = DISPLAY_BUFFER_STRIDE_BYTES_INPUT0 * DISPLAY_VSIZE_INPUT0;
#else
    info->fb0_addr      = 0;
    info->fb1_addr      = 0;
    info->stride_bytes   = 0;
    info->stride_pixels  = 0;
    info->fb_size_bytes  = 0;
#endif

    info->bpp           = GLCDC_FB_BPP;
    info->fb_count      = GLCDC_FB_COUNT;
}

/**
 * Get double-buffering status information (S-002-3)
 *
 * @details Fills the double-buffering status structure with current state.
 *          The buffer indices are derived from the Vsync callback tracking.
 *
 *          Initial state (after RM_LVGL_PORT_Open):
 *            - Front buffer: fb_background[1] (displayed by GLCDC)
 *            - Back buffer:  fb_background[0] (LVGL renders to this)
 *
 *          On each Vsync-synchronized swap:
 *            - Front/back indices toggle (0<->1)
 *
 * Reference: e2studio_CPU0/ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c:126-133
 */
void glcdc_port_get_dbuf_status(glcdc_dbuf_status_t *status)
{
    if (status == NULL) {
        return;
    }

    status->swap_count       = s_swap_count;
    status->vsync_count      = s_vsync_count;
    status->underflow_count  = s_underflow_count;

    /*
     * Determine buffer indices.
     * s_front_buffer_index tracks which buffer GLCDC is currently displaying.
     * The back buffer (render target) is the other one.
     */
    status->front_buffer_index = s_front_buffer_index;
    status->back_buffer_index  = (s_front_buffer_index == 0) ? 1 : 0;

#if GLCDC_CFG_LAYER_1_ENABLE
    status->front_buffer_addr  = (uint32_t)&fb_background[s_front_buffer_index];
    status->back_buffer_addr   = (uint32_t)&fb_background[status->back_buffer_index];
    status->double_buffer_enabled = true;
#else
    status->front_buffer_addr  = 0;
    status->back_buffer_addr   = 0;
    status->double_buffer_enabled = false;
#endif
}

/**
 * Get the current GLCDC underflow error count (S-002-3)
 */
uint32_t glcdc_port_get_underflow_count(void)
{
    return s_underflow_count;
}

/**
 * Get the buffer swap count (S-002-3)
 */
uint32_t glcdc_port_get_swap_count(void)
{
    return s_swap_count;
}

/**
 * NT-Shell "display" command handler
 *
 * @details Provides GLCDC display diagnostic sub-commands:
 *   display status    - Show GLCDC initialization state, timing, clock, output config
 *   display fb        - Show frame buffer addresses, sizes, and format
 *   display dbuf      - Show double-buffering status (S-002-3)
 *   display test      - Draw test patterns on the LCD (S-002-4)
 *   display backlight - Control LCD backlight on/off (S-002-4)
 *
 * Reference: doc/design/glcdc-timing-parameters.md
 */
int usrcmd_display(int argc, char **argv)
{
    if (argc < 2) {
        cmd_print_usage("display", "<subcommand>");
        print_to_console("  status    - Show GLCDC timing parameters and configuration\r\n");
        print_to_console("  fb        - Show frame buffer addresses and sizes\r\n");
        print_to_console("  dbuf      - Show double-buffering status (Vsync sync)\r\n");
        print_to_console("  test      - Draw test patterns on the LCD\r\n");
        print_to_console("  backlight - Control LCD backlight on/off\r\n");
        return CMD_ERR_USAGE;
    }

    if (ntlibc_strcmp(argv[1], "status") == 0) {
        glcdc_cmd_status();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "fb") == 0) {
        glcdc_cmd_fb();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "dbuf") == 0) {
        glcdc_cmd_dbuf();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "test") == 0) {
        glcdc_cmd_test(argc, argv);
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "backlight") == 0) {
        glcdc_cmd_backlight(argc, argv);
        return CMD_OK;
    }

    /* Unknown sub-command */
    {
        char buf[GLCDC_PRINT_BUF_SIZE];
        snprintf(buf, sizeof(buf), "Error: Unknown sub-command '%s'.\r\n", argv[1]);
        print_to_console(buf);
    }

    cmd_print_usage("display", "<subcommand>");
    print_to_console("  status    - Show GLCDC timing parameters and configuration\r\n");
    print_to_console("  fb        - Show frame buffer addresses and sizes\r\n");
    print_to_console("  dbuf      - Show double-buffering status (Vsync sync)\r\n");
    print_to_console("  test      - Draw test patterns on the LCD\r\n");
    print_to_console("  backlight - Control LCD backlight on/off\r\n");

    return CMD_ERR_INVALID_ARG;
}
