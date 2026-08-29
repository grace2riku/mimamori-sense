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
 *   2. lvgl_port_mtk3_open(&g_lvgl_port_cfg) (R-006: uT-Kernel replacement
 *      of RM_LVGL_PORT_Open - src/port/lvgl_port_mtk3.c) which:
 *      a. Clears both framebuffers with memset()
 *      b. Calls R_GLCDC_Open() with a copy of g_display0_cfg (callback swapped)
 *      c. Calls R_GLCDC_Start() to begin display output
 *      d. Calls R_GLCDC_BufferChange() to set framebuffer[1] as active
 *      e. Creates LVGL display object with flush/wait callbacks
 *      - Reference: e2studio_CPU0/src/port/lvgl_port_mtk3.c (1:1 with rm_lvgl_port.c:82-171)
 *   3. Backlight enabled via one-shot LVGL flush-finish event callback
 *      - Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c:41-56
 *
 * Double Buffering Architecture (S-002-3):
 *   The double-buffering is implemented by lvgl_port_mtk3 (R-006) and tracked
 *   by this module.
 *
 *   Buffer swap flow (per frame):
 *     1. LVGL renders dirty areas to the back buffer (e.g., fb_background[0])
 *     2. lvgl_port_mtk3_flush_cb() calls R_GLCDC_BufferChange(fb_background[0])
 *        to schedule the swap at the next Vsync
 *     3. lvgl_port_mtk3_flush_wait_cb() blocks on the Vsync semaphore
 *        (uT-Kernel tk_wai_sem)
 *     4. GLCDC fires Vsync (DISPLAY_EVENT_LINE_DETECTION) interrupt
 *     5. lvgl_port_mtk3_display_callback() releases the semaphore (tk_sig_sem)
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
#include "rm_lvgl_port.h"      /* types only (rm_lvgl_port_cfg_t / callback args) */
#include "lvgl_port_mtk3.h"    /* R-006: uT-Kernel display port (replaces RM_LVGL_PORT_Open) */
#include "lvgl.h"

/* R-006 (Issue #156): FreeRTOS.h / task.h removed. vTaskDelay -> tk_dly_tsk. */
#include <tk/tkernel.h>

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

/**
 * Framebuffer census sampling grid ("display fbstat", Issue #218)
 *
 * 1024/16 x 600/8 = 64 x 75 = 4,800 samples per buffer. Dense enough that a
 * uniformly white screen and a screen with any real content are never
 * confused, cheap enough to finish in a few milliseconds on the shared
 * SDRAM bus.
 */
#define GLCDC_FBSTAT_X_STEP     (16)
#define GLCDC_FBSTAT_Y_STEP     (8)

/**
 * "display blank" reconcile wait (Issue #218)
 *
 * The blank request is applied by the LVGL task on its next flush, so the
 * shell polls for the acknowledgement. At the 20 fps this project renders,
 * one frame is 50 ms; 500 ms is ten frames of slack, and reaching the
 * timeout is itself the answer ("LVGL is not flushing").
 */
#define GLCDC_BLANK_POLL_MS             (10)
#define GLCDC_BLANK_APPLY_TIMEOUT_MS    (500)

/**
 * PmnPFS fields checked by "display pins" (Issue #218)
 *
 * The IOPORT configuration word FSP writes IS the PmnPFS register value
 * (ra/fsp/src/bsp/mcu/all/bsp_io.h:386), so the constants below are the
 * register bit positions:
 *   PSEL[28:24] = 0x19 -> IOPORT_PERIPHERAL_LCD_GRAPHICS
 *                 (r_ioport.h:428, IOPORT_PRV_PFS_PSEL_OFFSET = 24)
 *   PMR[16]     = 1    -> IOPORT_CFG_PERIPHERAL_PIN (r_ioport.h:481)
 */
#define GLCDC_PFS_PSEL_MASK             (0x1F000000UL)
#define GLCDC_PFS_PSEL_LCD_GRAPHICS     (0x19000000UL)
#define GLCDC_PFS_PMR_BIT               (0x00010000UL)

/**
 * PmnPFS fields checked for the GPIO control pins (Issue #222)
 *
 * DISP_RESET and DISP_BLEN are plain outputs, not peripheral pins, so their
 * pass condition is the inverse of the 30 above: PSEL = 0 and PMR = 0.
 * Bit positions from the MCU header (R7KA8P1KF_core0.h:66216-66220):
 *   PODR[0] - the level software drives
 *   PIDR[1] - the level actually present on the pad
 *   PDR[2]  - direction, 1 = output
 */
#define GLCDC_PFS_PODR_BIT              (0x00000001UL)
#define GLCDC_PFS_PIDR_BIT              (0x00000002UL)
#define GLCDC_PFS_PDR_BIT               (0x00000004UL)

/**
 * "display signals" sampling window (Issue #218)
 *
 * GLCDC_SIGNALS_ROUNDS bursts of GLCDC_SIGNALS_BURST samples per pin,
 * GLCDC_SIGNALS_GAP_MS apart. The gap is not a divisor of the ~14.08 ms frame
 * period (71 Hz), so bursts land at different phases of the frame rather than
 * resampling the same lines. Total: 2,000 samples per pin over ~100 ms, about
 * seven frames.
 */
#define GLCDC_SIGNALS_ROUNDS            (10)
#define GLCDC_SIGNALS_BURST             (200)
#define GLCDC_SIGNALS_GAP_MS            (10)

/** Number of pins the GLCDC drives (see g_glcdc_pins) */
#define GLCDC_PIN_COUNT                 (30)

/** Number of LCD control pins driven as GPIO (see g_glcdc_gpio_pins) */
#define GLCDC_GPIO_PIN_COUNT            (2)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/** Current GLCDC initialization status */
static glcdc_status_t s_glcdc_status = GLCDC_STATUS_NOT_INITIALIZED;

/** Vsync interrupt counter (incremented in lvgl_glcdc_callback) */
static volatile uint32_t s_vsync_count = 0;

/**
 * Double-buffering state tracking (S-002-3, revised by Issue #218)
 *
 * Issue #218: the previous implementation incremented a "swap count" and
 * toggled a "front buffer index" inside the Vsync branch of
 * lvgl_glcdc_callback(). Both advanced on EVERY Vsync whether or not LVGL had
 * flushed anything, which made "Swap Count" a duplicate of "Vsync Count" and
 * the reported front buffer a free-running toggle. While triaging the
 * all-white screen those two numbers looked like proof that LVGL was still
 * rendering, but they only proved that the GLCDC line-detect interrupt was
 * still firing. They are replaced by the counters below, which are written
 * from the flush callback (LVGL task) instead.
 *
 * Writer  : lvgl_task, via glcdc_port_notify_flush() from
 *           lvgl_port_mtk3_flush_cb(). Single writer, no ISR path
 *           (lv_refr.c:1348,1373,1409 -> lv_timer_handler).
 * Readers : ntshell_task ("display dbuf" / "display reg").
 * Each value is an independently meaningful aligned 32-bit volatile store, so
 * no critical section is required (CLAUDE.md concurrency rules apply to
 * multi-word publish/sample pairs, which this is not).
 */

/** Completed LVGL flushes (one per R_GLCDC_BufferChange() request) */
static volatile uint32_t s_flush_count = 0;

/** Buffer address passed to the last R_GLCDC_BufferChange() (0 = none yet) */
static volatile uint32_t s_last_flush_addr = 0;

/**
 * Number of R_GLCDC_BufferChange() calls that returned an error.
 *
 * The flush callback retries only FSP_ERR_INVALID_UPDATE_TIMING and discards
 * every other return value (lvgl_port_mtk3.c). A persistent failure would
 * therefore freeze the displayed image with nothing reported; this counter
 * makes it visible.
 */
static volatile uint32_t s_bufchange_err_count = 0;

/** fsp_err_t returned by the last failing R_GLCDC_BufferChange() */
static volatile int32_t s_bufchange_last_err = 0;

/** GLCDC underflow error counter (written from the Vsync/underflow ISR) */
static volatile uint32_t s_underflow_count = 0;

/**
 * Diagnostic blank request ("display blank", Issue #218)
 *
 * Declaration / reconcile pair, per the concurrency rules in CLAUDE.md:
 *   s_blank_desired : written by ntshell_task, read by lvgl_task in the flush
 *                     callback, which then hands NULL to
 *                     R_GLCDC_BufferChange() instead of the framebuffer.
 *   s_blank_applied : written by lvgl_task (glcdc_port_notify_flush), read by
 *                     ntshell_task while it waits for the acknowledgement.
 *
 * The flush callback stays the only caller of R_GLCDC_BufferChange() that
 * can run concurrently with anything. (lvgl_port_mtk3_open() also calls it,
 * once, during initialisation - but glcdc_cmd_blank() refuses while
 * s_glcdc_status is not GLCDC_STATUS_INITIALIZED, and that is set only after
 * lvgl_port_mtk3_open() has returned, so the two can never overlap.) A
 * concurrent second caller would interleave with its AB1 -> FLMRD -> FLM2 ->
 * VEN writes (r_glcdc.c:677-685) and could latch "layer visible + FLM2 = 0".
 *
 * Each is a single aligned 32-bit volatile with one writer, and they are
 * never read as a pair, so no critical section is needed. The reconcile is
 * idempotent - the last declaration wins - so no sequence number or ticket is
 * needed either.
 */
static volatile uint32_t s_blank_desired = 0;
static volatile uint32_t s_blank_applied = 0;

/**********************************************************************************************************************
 Private (static) functions prototypes
 *********************************************************************************************************************/
static void glcdc_lcd_reset(void);
static void glcdc_backlight_on_event(lv_event_t *event);
static void glcdc_cmd_status(void);
static void glcdc_cmd_fb(void);
static void glcdc_cmd_dbuf(void);
static void glcdc_cmd_backlight(int argc, char **argv);
static void glcdc_cmd_test(int argc, char **argv);
static void glcdc_cmd_reg(void);
static void glcdc_cmd_fbstat(void);
static int glcdc_cmd_blank(int argc, char **argv);
static const char *glcdc_tcon_sel_name(uint32_t sel);
static void glcdc_cmd_pins(void);
static void glcdc_pins_report_gpio(void);
static void glcdc_cmd_signals(void);

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

    /*
     * Issue #218: the "(front/display)" annotation used to come from a
     * software index that toggled on every Vsync, so it never named the
     * buffer the GLCDC was really reading. The displayed buffer is a hardware
     * register; "display reg" reads it back. Here we only report which buffer
     * LVGL last handed to R_GLCDC_BufferChange().
     *
     * Issue #219: sample it ONCE. The two lines below are separated by a
     * print_to_console() call that takes milliseconds, and s_last_flush_addr
     * alternates between the two buffers every ~50 ms at the 20 fps this
     * project renders. Reading the variable per line would let both lines
     * claim the flush, or neither - which is the exact defect Issue #219
     * reports against the old s_front_buffer_index.
     */
    {
        const uint32_t last_flush = s_last_flush_addr;

        snprintf(buf, sizeof(buf), "  FB[0]       : 0x%08lX%s\r\n",
                 (unsigned long)fb.fb0_addr,
                 (last_flush == fb.fb0_addr) ? " (last flushed by LVGL)" : "");
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  FB[1]       : 0x%08lX%s\r\n",
                 (unsigned long)fb.fb1_addr,
                 (last_flush == fb.fb1_addr) ? " (last flushed by LVGL)" : "");
        print_to_console(buf);
    }

    print_to_console("  (displayed buffer: see 'display reg' GR0.FLM2)\r\n");

    print_to_console("[Placement]\r\n");
    print_to_console("  Section     : .sdram_noinit_nocache\r\n");
    print_to_console("  Cache       : Non-cacheable (MPU configured by BSP)\r\n");
    print_to_console("  Alignment   : 64-byte aligned\r\n");
    print_to_console("  Buffering   : Double (Vsync-synchronized swap)\r\n");
}

/**
 * "display dbuf" sub-command handler (S-002-3, revised by Issue #218)
 *
 * @details Displays the current double-buffering status including:
 *   - Whether double buffering is enabled
 *   - The buffer address LVGL last requested
 *   - LVGL flush count, Vsync count, BufferChange failures, underflow count
 *   - Flush rate and Vsync rate measured over one second
 *
 * Issue #218: the two rates are the point of this command. They come from
 * independent sources - the flush rate is written by the LVGL task, the Vsync
 * rate by the GLCDC line-detect ISR - so "Vsync 70 Hz / Flush 0 fps" says
 * LVGL stopped rendering while the panel keeps being scanned out, and
 * "Vsync 0 Hz" says the GLCDC itself stopped. Before Issue #218 both numbers
 * were derived from the Vsync interrupt and were therefore always equal.
 *
 * The buffer swap mechanism (src/port/lvgl_port_mtk3.c):
 *   - flush_cb: calls R_GLCDC_BufferChange() to schedule swap at next Vsync
 *   - flush_wait_cb: blocks on semaphore until Vsync fires
 *   - lvgl_port_mtk3_display_callback: releases semaphore on Vsync interrupt
 *
 * Limitation: Flush Count is the number of buffer changes LVGL REQUESTED, not
 * the number the GLCDC has latched. The flush callback waits for the next
 * Vsync immediately afterwards, so over a one-second window the two differ by
 * at most one frame - fine for "is LVGL still rendering?" and for a rough
 * frame-rate figure, not a presented-frame count.
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

    snprintf(buf, sizeof(buf), "  Sync        : Vsync (uT-Kernel semaphore)\r\n");
    print_to_console(buf);

    print_to_console("[Buffer State]\r\n");

    snprintf(buf, sizeof(buf), "  Last flushed by LVGL : 0x%08lX%s\r\n",
             (unsigned long)dbuf.last_flush_addr,
             (dbuf.last_flush_addr != 0) ? "" :
             ((dbuf.flush_count == 0) ? " (no flush yet)"
                                      : " (NULL - 'display blank' is on)"));
    print_to_console(buf);

    print_to_console("  Displayed by GLCDC   : see 'display reg' (GR0.FLM2)\r\n");

    print_to_console("[Counters]\r\n");

    snprintf(buf, sizeof(buf), "  Vsync Count      : %lu\r\n",
             (unsigned long)dbuf.vsync_count);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Flush Count      : %lu\r\n",
             (unsigned long)dbuf.flush_count);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  BufChange Errors : %lu (last: %ld)\r\n",
             (unsigned long)dbuf.bufchange_err_count,
             (long)dbuf.bufchange_last_err);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Underflow Count  : %lu%s\r\n",
             (unsigned long)dbuf.underflow_count,
             dbuf.underflow_count > 0 ? " (WARNING: SDRAM bandwidth issue)" : "");
    print_to_console(buf);

    /*
     * Measure the flush rate and the Vsync rate over one second.
     *
     * Issue #218: these two now come from different writers (LVGL task vs
     * GLCDC ISR), so comparing them tells which side stopped.
     */
    if (s_glcdc_status == GLCDC_STATUS_INITIALIZED) {
        print_to_console("[Rate Measurement]\r\n");
        print_to_console("  Measuring over 1 second...\r\n");

        uint32_t flush_start = s_flush_count;
        uint32_t vsync_start = s_vsync_count;
        tk_dly_tsk(1000);   /* R-006: vTaskDelay(pdMS_TO_TICKS(1000)) -> tk_dly_tsk(1000) ms */
        uint32_t flush_end = s_flush_count;
        uint32_t vsync_end = s_vsync_count;

        uint32_t fps = flush_end - flush_start;
        uint32_t vsync_rate = vsync_end - vsync_start;

        snprintf(buf, sizeof(buf), "  Vsync Rate  : %lu Hz\r\n",
                 (unsigned long)vsync_rate);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  Flush Rate  : %lu fps\r\n",
                 (unsigned long)fps);
        print_to_console(buf);

        if ((fps == 0) && (vsync_rate > 0)) {
            print_to_console("  Note: GLCDC scans out but LVGL is not flushing.\r\n");
        } else if (vsync_rate == 0) {
            print_to_console("  Note: No Vsync. GLCDC output has stopped.\r\n");
        } else {
            /* Both sides alive - nothing to flag. */
        }
    }

    print_to_console("[Architecture]\r\n");
    print_to_console("  lvgl_port_mtk3 handles buffer swap (uT-Kernel):\r\n");
    print_to_console("    flush_cb      -> R_GLCDC_BufferChange(back_buf)\r\n");
    print_to_console("    flush_wait_cb -> tk_wai_sem(vsync_sem)\r\n");
    print_to_console("    Vsync ISR     -> tk_sig_sem(vsync_sem)\r\n");
    print_to_console("  Tearing prevention: guaranteed by Vsync sync\r\n");
}

/**
 * Name the signal a TCON output register is routing (Issue #218)
 *
 * @details Decodes the SEL[2:0] field of TCON.STVA2 / STVB2 / STHA2 / STHB2.
 *          Values are glcdc_tcon_signal_select_t
 *          (ra/fsp/inc/instances/r_glcdc.h:95-99).
 *
 * @param sel SEL field value (0-7)
 * @return Human-readable signal name
 */
static const char *glcdc_tcon_sel_name(uint32_t sel)
{
    const char *name;

    switch (sel) {
        case 0: name = "STVA/VS"; break;
        case 1: name = "STVB/VE"; break;
        case 2: name = "STHA/HS"; break;
        case 3: name = "STHB/HE"; break;
        case 7: name = "DE";      break;
        default: name = "(reserved)"; break;
    }

    return name;
}

/**
 * "display reg" sub-command handler (Issue #218)
 *
 * @details Reads back the GLCDC hardware registers that decide what reaches
 *          the panel, instead of the software-tracked values printed by
 *          "display status" / "display fb" / "display dbuf".
 *
 *          Issue #218 (all-white screen after minutes of runtime) needs this
 *          because every software-side number kept looking healthy while the
 *          panel was white. The registers answer three questions the tracked
 *          values cannot:
 *            - GR0.FLM2  : which address is the GLCDC really fetching?
 *                          Compared here against fb_background[0] / [1].
 *            - GR0.AB1   : is the graphics plane still blended in
 *                          (DISPSEL == 3)? If it went transparent
 *                          (DISPSEL == 1) the panel would show the lower
 *                          layer, and since graphics layer 2 is disabled
 *                          (GLCDC_CFG_LAYER_2_ENABLE false) that is BG.BGC,
 *                          configured black - so a WHITE panel is not
 *                          explained by a transparent plane.
 *            - OUT.BRIGHT1/2, OUT.CONTRAST : has the colour correction
 *                          saturated the output to full scale (= white)?
 *            - TCON.*    : is the DE (data enable) pin still driven? This
 *                          project routes DE to LCD_TCON2
 *                          (g_display0_extend_cfg.tcon_de =
 *                          GLCDC_TCON_PIN_2), and FSP writes the per-pin
 *                          signal selection through
 *                          g_tcon_lut[] = {STVA2, STVB2, STHA2, STHB2}
 *                          (r_glcdc.c:283-286), so DE lands in
 *                          TCON.STHA2.SEL. If DE stopped, the panel loses its
 *                          valid-data window (many TFTs go white) while the
 *                          line-detect interrupt keeps firing from the
 *                          GLCDC's internal counter - which is exactly the
 *                          state Issue #218 is in.
 *            - SYSCNT.PANEL_CLK : is the panel clock output still enabled
 *                          (CLKEN)?
 *
 *          Register block layout: R_GLCDC->GR[0] is frame layer 1
 *          (DISPLAY_FRAME_LAYER_1 == 0, ra/fsp/inc/api/r_display_api.h:50),
 *          which is the layer this project uses (g_lvgl_port_cfg
 *          .inherit_frame_layer, ra_gen/common_data.c).
 *
 * All accesses are reads. This runs on ntshell_task concurrently with LVGL
 * rendering, but reading peripheral registers cannot disturb it.
 */
static void glcdc_cmd_reg(void)
{
    char buf[GLCDC_PRINT_BUF_SIZE];

    if (s_glcdc_status != GLCDC_STATUS_INITIALIZED) {
        print_to_console("[GLCDC Registers]\r\n");
        print_to_console("  GLCDC is not initialized; registers are not meaningful.\r\n");
        return;
    }

    /*
     * Read all registers up front. Printing is UART-paced (tens of ms for this
     * many lines), so reading as we print would spread the sample over that
     * window. This is not an atomic snapshot - the 19 reads are still
     * sequential - but it keeps them within a few microseconds of each other.
     */
    const uint32_t gr_ven    = R_GLCDC->GR[0].VEN;
    const uint32_t gr_flmrd  = R_GLCDC->GR[0].FLMRD;
    const uint32_t gr_flm2   = R_GLCDC->GR[0].FLM2;
    const uint32_t gr_flm3   = R_GLCDC->GR[0].FLM3;
    const uint32_t gr_flm5   = R_GLCDC->GR[0].FLM5;
    const uint32_t gr_flm6   = R_GLCDC->GR[0].FLM6;
    const uint32_t gr_ab1    = R_GLCDC->GR[0].AB1;
    const uint32_t gr_mon    = R_GLCDC->GR[0].MON;
    const uint32_t bg_en     = R_GLCDC->BG.EN;
    const uint32_t bg_bgc    = R_GLCDC->BG.BGC;
    const uint32_t bg_mon    = R_GLCDC->BG.MON;
    const uint32_t out_vlatch  = R_GLCDC->OUT.VLATCH;
    const uint32_t out_set     = R_GLCDC->OUT.SET;
    const uint32_t out_bright1 = R_GLCDC->OUT.BRIGHT1;
    const uint32_t out_bright2 = R_GLCDC->OUT.BRIGHT2;
    const uint32_t out_contrast = R_GLCDC->OUT.CONTRAST;
    const uint32_t out_clkphase = R_GLCDC->OUT.CLKPHASE;
    const uint32_t out_pdtha    = R_GLCDC->OUT.PDTHA;
    const uint32_t tcon_tim   = R_GLCDC->TCON.TIM;
    const uint32_t tcon_stva1 = R_GLCDC->TCON.STVA1;
    const uint32_t tcon_stva2 = R_GLCDC->TCON.STVA2;
    const uint32_t tcon_stvb1 = R_GLCDC->TCON.STVB1;
    const uint32_t tcon_stvb2 = R_GLCDC->TCON.STVB2;
    const uint32_t tcon_stha1 = R_GLCDC->TCON.STHA1;
    const uint32_t tcon_stha2 = R_GLCDC->TCON.STHA2;
    const uint32_t tcon_sthb1 = R_GLCDC->TCON.STHB1;
    const uint32_t tcon_sthb2 = R_GLCDC->TCON.STHB2;
    const uint32_t tcon_de    = R_GLCDC->TCON.DE;
    const uint32_t sys_inten = R_GLCDC->SYSCNT.INTEN;
    const uint32_t sys_stmon = R_GLCDC->SYSCNT.STMON;
    const uint32_t sys_panel_clk = R_GLCDC->SYSCNT.PANEL_CLK;

    print_to_console("[GLCDC Graphics Layer 1 (GR[0]) Registers]\r\n");

    snprintf(buf, sizeof(buf), "  VEN        : 0x%08lX\r\n", (unsigned long)gr_ven);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  FLMRD      : 0x%08lX (read enable: %s)\r\n",
             (unsigned long)gr_flmrd,
             (gr_flmrd & 1UL) ? "on" : "OFF");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  FLM2 (base): 0x%08lX\r\n", (unsigned long)gr_flm2);
    print_to_console(buf);

    /*
     * Compare the address the hardware is fetching against the two LVGL
     * framebuffers. "neither" means the GLCDC is scanning out memory that
     * LVGL never renders into, which on its own explains a frozen or garbage
     * screen.
     */
#if GLCDC_CFG_LAYER_1_ENABLE
    {
        const uint32_t fb0 = (uint32_t)(uintptr_t)&fb_background[0];
        const uint32_t fb1 = (uint32_t)(uintptr_t)&fb_background[1];
        const char *which;

        if (gr_flm2 == fb0) {
            which = "fb_background[0]";
        } else if (gr_flm2 == fb1) {
            which = "fb_background[1]";
        } else {
            which = "NEITHER framebuffer (unexpected)";
        }

        snprintf(buf, sizeof(buf), "    -> %s\r\n", which);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "    fb[0]=0x%08lX fb[1]=0x%08lX last flush=0x%08lX\r\n",
                 (unsigned long)fb0, (unsigned long)fb1,
                 (unsigned long)s_last_flush_addr);
        print_to_console(buf);
    }
#endif

    snprintf(buf, sizeof(buf), "  FLM3 (ofs) : 0x%08lX\r\n", (unsigned long)gr_flm3);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  FLM5 (line): 0x%08lX\r\n", (unsigned long)gr_flm5);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  FLM6 (fmt) : 0x%08lX\r\n", (unsigned long)gr_flm6);
    print_to_console(buf);

    {
        /*
         * DISPSEL encoding (ra/fsp/src/r_glcdc/r_glcdc.c:158-162):
         *   1 = transparent (the lower layer is shown; graphics layer 2 is
         *       disabled here, so that means the background plane = BG.BGC)
         *   2 = this plane displayed opaque
         *   3 = this plane blended onto the lower layer - what
         *       R_GLCDC_BufferChange() writes for a non-NULL framebuffer
         *       (r_glcdc.c:666-678)
         */
        const uint32_t dispsel = gr_ab1 & 3UL;
        const char *dispsel_txt;

        if (dispsel == 1UL) {
            dispsel_txt = "transparent - panel shows BG.BGC";
        } else if (dispsel == 2UL) {
            dispsel_txt = "plane displayed (opaque)";
        } else if (dispsel == 3UL) {
            dispsel_txt = "plane blended on lower layer (expected)";
        } else {
            dispsel_txt = "0 - never written by the driver (unexpected)";
        }

        snprintf(buf, sizeof(buf), "  AB1        : 0x%08lX (DISPSEL=%lu: %s)\r\n",
                 (unsigned long)gr_ab1, (unsigned long)dispsel, dispsel_txt);
        print_to_console(buf);
    }

    snprintf(buf, sizeof(buf), "  MON        : 0x%08lX (underflow flag: %s)\r\n",
             (unsigned long)gr_mon,
             (gr_mon & 0x00010000UL) ? "SET" : "clear");
    print_to_console(buf);

    print_to_console("[GLCDC Background Plane (BG) Registers]\r\n");

    snprintf(buf, sizeof(buf), "  EN         : 0x%08lX\r\n", (unsigned long)bg_en);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  BGC        : 0x%08lX (R=%lu G=%lu B=%lu)\r\n",
             (unsigned long)bg_bgc,
             (unsigned long)((bg_bgc >> 16) & 0xFFUL),
             (unsigned long)((bg_bgc >> 8) & 0xFFUL),
             (unsigned long)(bg_bgc & 0xFFUL));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  MON        : 0x%08lX (module enabled: %s)\r\n",
             (unsigned long)bg_mon,
             (bg_mon & 1UL) ? "yes" : "NO");
    print_to_console(buf);

    print_to_console("[GLCDC Output Block (OUT) Registers]\r\n");

    snprintf(buf, sizeof(buf), "  VLATCH     : 0x%08lX\r\n", (unsigned long)out_vlatch);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  SET        : 0x%08lX\r\n", (unsigned long)out_set);
    print_to_console(buf);

    /*
     * Colour correction is compiled out in this project
     * (GLCDC_CFG_COLOR_CORRECTION_ENABLE is false, ra_cfg/fsp_cfg/r_glcdc_cfg.h),
     * so R_GLCDC_Open() writes fixed neutral values once (r_glcdc.c:407-414)
     * and nothing calls R_GLCDC_ColorCorrection() afterwards. Anything other
     * than the expected words below means the correction stage drifted, which
     * would tint or saturate every pixel regardless of the framebuffer
     * contents - one of the ways a panel can go white with healthy pixel data.
     */
    snprintf(buf, sizeof(buf), "  BRIGHT1    : 0x%08lX (expect 0x00000200)%s\r\n",
             (unsigned long)out_bright1,
             (out_bright1 == 0x00000200UL) ? "" : "  <<< UNEXPECTED");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  BRIGHT2    : 0x%08lX (expect 0x02000200)%s\r\n",
             (unsigned long)out_bright2,
             (out_bright2 == 0x02000200UL) ? "" : "  <<< UNEXPECTED");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  CONTRAST   : 0x%08lX (expect 0x00808080)%s\r\n",
             (unsigned long)out_contrast,
             (out_contrast == 0x00808080UL) ? "" : "  <<< UNEXPECTED");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  CLKPHASE   : 0x%08lX\r\n", (unsigned long)out_clkphase);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  PDTHA      : 0x%08lX (dithering off in this project)\r\n",
             (unsigned long)out_pdtha);
    print_to_console(buf);

    /*
     * TCON: the pin timing block. Only the DE path is configured here -
     * tcon_hsync and tcon_vsync are GLCDC_TCON_PIN_NONE, so r_glcdc.c:1286-1302
     * calls r_glcdc_data_enable_set() alone. That writes:
     *   STHA2 = GLCDC_TCON_SIGNAL_SELECT_DE (7)          [pin LCD_TCON2]
     *   STHB1 = (h back porch 160 << 16) | h display 1024 = 0x00A00400
     *   STVB1 = (v back porch  23 << 16) | v display  600 = 0x00170258
     *   TCON.DE = 0, because data_enable_polarity is HIACTIVE
     * (r_glcdc.c:1368-1379 and ra_gen/common_data.c g_display0_cfg.output).
     * STVA1/STVA2/STVB2/STHA1/STHB2 are never written and keep their reset
     * values, so no expectation is asserted for them.
     */
    print_to_console("[GLCDC Timing Control (TCON) Registers]\r\n");

    snprintf(buf, sizeof(buf), "  TIM        : 0x%08lX\r\n", (unsigned long)tcon_tim);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  STVA1/STVA2: 0x%08lX / 0x%08lX (TCON0 <- %s)\r\n",
             (unsigned long)tcon_stva1, (unsigned long)tcon_stva2,
             glcdc_tcon_sel_name(tcon_stva2 & 7UL));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  STVB1/STVB2: 0x%08lX / 0x%08lX (TCON1 <- %s)\r\n",
             (unsigned long)tcon_stvb1, (unsigned long)tcon_stvb2,
             glcdc_tcon_sel_name(tcon_stvb2 & 7UL));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  STHA1/STHA2: 0x%08lX / 0x%08lX (TCON2 <- %s)%s\r\n",
             (unsigned long)tcon_stha1, (unsigned long)tcon_stha2,
             glcdc_tcon_sel_name(tcon_stha2 & 7UL),
             ((tcon_stha2 & 7UL) == 7UL) ? "" : "  <<< DE LOST");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  STHB1/STHB2: 0x%08lX / 0x%08lX (TCON3 <- %s)\r\n",
             (unsigned long)tcon_sthb1, (unsigned long)tcon_sthb2,
             glcdc_tcon_sel_name(tcon_sthb2 & 7UL));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  DE window  : STHB1 0x%08lX (expect 0x00A00400)%s\r\n",
             (unsigned long)tcon_sthb1,
             (tcon_sthb1 == 0x00A00400UL) ? "" : "  <<< UNEXPECTED");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "               STVB1 0x%08lX (expect 0x00170258)%s\r\n",
             (unsigned long)tcon_stvb1,
             (tcon_stvb1 == 0x00170258UL) ? "" : "  <<< UNEXPECTED");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  DE polarity: 0x%08lX (expect 0x00000000 = high active)%s\r\n",
             (unsigned long)tcon_de,
             (tcon_de == 0UL) ? "" : "  <<< UNEXPECTED");
    print_to_console(buf);

    print_to_console("[GLCDC System Control (SYSCNT) Registers]\r\n");

    snprintf(buf, sizeof(buf), "  INTEN      : 0x%08lX\r\n", (unsigned long)sys_inten);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  STMON      : 0x%08lX\r\n", (unsigned long)sys_stmon);
    print_to_console(buf);

    /*
     * PANEL_CLK.CLKEN gates the panel clock output. If it were cleared the
     * panel would lose everything at once - the failure mode this command is
     * looking for. DCDR / CLKSEL / PIXSEL are printed raw; they come from
     * g_display0_extend_cfg (clksrc INTERNAL, clock_div_ratio
     * GLCDC_PANEL_CLK_DIVISOR_4).
     */
    snprintf(buf, sizeof(buf), "  PANEL_CLK  : 0x%08lX\r\n", (unsigned long)sys_panel_clk);
    print_to_console(buf);

    snprintf(buf, sizeof(buf),
             "    CLKEN=%lu%s DCDR=%lu CLKSEL=%lu PIXSEL=%lu VER=0x%04lX\r\n",
             (unsigned long)((sys_panel_clk >> 6) & 1UL),
             (((sys_panel_clk >> 6) & 1UL) != 0UL) ? "" : " <<< PANEL CLOCK OFF",
             (unsigned long)(sys_panel_clk & 0x3FUL),
             (unsigned long)((sys_panel_clk >> 8) & 1UL),
             (unsigned long)((sys_panel_clk >> 12) & 1UL),
             (unsigned long)((sys_panel_clk >> 16) & 0xFFFFUL));
    print_to_console(buf);
}

/**
 * "display fbstat" sub-command handler (Issue #218)
 *
 * @details Samples the contents of both framebuffers and reports how many of
 *          the sampled pixels are white (0xFFFF), black (0x0000) or something
 *          else, plus the raw value at four fixed screen positions.
 *
 *          This is the measurement that splits Issue #218 in half:
 *            - all-white census  -> the white comes from what is WRITTEN into
 *                                   the framebuffer (LVGL / Dave2D / SDRAM)
 *            - normal census     -> the framebuffer is fine and the white
 *                                   comes from the GLCDC output stage or the
 *                                   panel; continue with "display test
 *                                   colorbar", which bypasses LVGL entirely
 *
 *          "display backlight off" turning the panel black proves only that
 *          the backlight pin works; it says nothing about the pixel data.
 *          This command is what says something about the pixel data.
 *
 *          Sampling: every GLCDC_FBSTAT_X_STEP-th pixel of every
 *          GLCDC_FBSTAT_Y_STEP-th line = 4,800 samples per buffer. At the
 *          ~4 MB/s effective SDRAM read rate measured for this platform
 *          (see the performance notes in src/camera_display.c) the two
 *          buffers together take well under 10 ms, and no lock is held.
 *
 *          The buffers are live: LVGL keeps rendering into the back buffer
 *          while this runs, so a census taken mid-frame can mix old and new
 *          content. That is fine for "is the whole screen white?" and is
 *          noted in the output.
 */
static void glcdc_cmd_fbstat(void)
{
#if GLCDC_CFG_LAYER_1_ENABLE
    char buf[GLCDC_PRINT_BUF_SIZE];

    /* Probe points, chosen to cover regions with different update rates. */
    static const struct {
        uint16_t    x;
        uint16_t    y;
        const char *name;
    } probes[] = {
        {  20u,  20u, "status bar left"       },
        { 900u,  20u, "status bar right/clock"},
        { 512u, 320u, "camera image centre"   },
        {  40u, 300u, "letterbox margin"      },
    };

    print_to_console("[Frame Buffer Content Census (Issue #218)]\r\n");

    snprintf(buf, sizeof(buf), "  Grid      : every %u px x %u lines (%lu samples/buffer)\r\n",
             (unsigned int)GLCDC_FBSTAT_X_STEP,
             (unsigned int)GLCDC_FBSTAT_Y_STEP,
             (unsigned long)(((uint32_t)DISPLAY_HSIZE_INPUT0 / GLCDC_FBSTAT_X_STEP) *
                             ((uint32_t)DISPLAY_VSIZE_INPUT0 / GLCDC_FBSTAT_Y_STEP)));
    print_to_console(buf);

    print_to_console("  Note      : buffers are live; a census may mix frames.\r\n");

    for (uint32_t fb_index = 0; fb_index < 2u; fb_index++) {
        const uint8_t *p_fb = &fb_background[fb_index][0];

        uint32_t white = 0;
        uint32_t black = 0;
        uint32_t other = 0;
        uint16_t vmin  = 0xFFFFu;
        uint16_t vmax  = 0x0000u;

        for (uint32_t y = 0; y < (uint32_t)DISPLAY_VSIZE_INPUT0; y += GLCDC_FBSTAT_Y_STEP) {
            const volatile uint16_t *p_line =
                (const volatile uint16_t *)(const void *)
                (p_fb + (y * (uint32_t)DISPLAY_BUFFER_STRIDE_BYTES_INPUT0));

            for (uint32_t x = 0; x < (uint32_t)DISPLAY_HSIZE_INPUT0; x += GLCDC_FBSTAT_X_STEP) {
                const uint16_t px = p_line[x];

                if (px == 0xFFFFu) {
                    white++;
                } else if (px == 0x0000u) {
                    black++;
                } else {
                    other++;
                }

                if (px < vmin) {
                    vmin = px;
                }
                if (px > vmax) {
                    vmax = px;
                }
            }
        }

        const uint32_t total = white + black + other;

        snprintf(buf, sizeof(buf), "[fb_background[%lu] @ 0x%08lX]\r\n",
                 (unsigned long)fb_index,
                 (unsigned long)(uintptr_t)p_fb);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  White(FFFF): %lu (%lu%%)\r\n",
                 (unsigned long)white,
                 (unsigned long)((total > 0u) ? ((white * 100u) / total) : 0u));
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  Black(0000): %lu (%lu%%)\r\n",
                 (unsigned long)black,
                 (unsigned long)((total > 0u) ? ((black * 100u) / total) : 0u));
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  Other      : %lu (%lu%%)\r\n",
                 (unsigned long)other,
                 (unsigned long)((total > 0u) ? ((other * 100u) / total) : 0u));
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  Min / Max  : 0x%04X / 0x%04X\r\n",
                 (unsigned int)vmin, (unsigned int)vmax);
        print_to_console(buf);

        /*
         * The single most useful line for Issue #218: if every sampled pixel
         * has the same value, the buffer holds one flat colour and there is
         * no image in it at all.
         */
        if (vmin == vmax) {
            snprintf(buf, sizeof(buf), "  ** UNIFORM : every sample is 0x%04X (no image) **\r\n",
                     (unsigned int)vmin);
            print_to_console(buf);
        }

        for (uint32_t i = 0; i < (sizeof(probes) / sizeof(probes[0])); i++) {
            const volatile uint16_t *p_line =
                (const volatile uint16_t *)(const void *)
                (p_fb + ((uint32_t)probes[i].y * (uint32_t)DISPLAY_BUFFER_STRIDE_BYTES_INPUT0));

            snprintf(buf, sizeof(buf), "  (%4u,%4u) : 0x%04X  %s\r\n",
                     (unsigned int)probes[i].x,
                     (unsigned int)probes[i].y,
                     (unsigned int)p_line[probes[i].x],
                     probes[i].name);
            print_to_console(buf);
        }
    }
#else
    print_to_console("  Error: GLCDC Layer 1 is not enabled.\r\n");
#endif
}

/**
 * "display blank on|off" sub-command handler (Issue #218)
 *
 * @details Asks the LVGL task to hand NULL to R_GLCDC_BufferChange() instead
 *          of the rendered framebuffer. The FSP driver then writes
 *          AB1.DISPSEL = 1 (transparent) and FLMRD = 0 (frame buffer read
 *          disabled) - see ra/fsp/src/r_glcdc/r_glcdc.c:666-685 - so what
 *          reaches the panel is the background plane colour BG.BGC, which is
 *          configured black and is generated inside the GLCDC WITHOUT reading
 *          SDRAM at all. Graphics layer 2 is disabled
 *          (GLCDC_CFG_LAYER_2_ENABLE false), so the background plane really
 *          is the lower layer here.
 *
 *          That makes this the measurement that splits what Issue #218 was
 *          left with after "display test red" failed to change an all-white
 *          panel:
 *            - panel turns BLACK -> the GLCDC output stage and the panel are
 *                                   healthy; the fault is in the graphics
 *                                   plane's SDRAM read path (a different bus
 *                                   master from the CPU, which is why
 *                                   "display fbstat" can still read a sane
 *                                   image out of the same memory)
 *            - panel stays WHITE -> the fault is in the GLCDC output stage or
 *                                   the panel/board; software cannot narrow
 *                                   it further
 *
 * Why this does not call R_GLCDC_BufferChange() itself:
 *   lvgl_port_mtk3_flush_cb() calls it ~20 times a second from the LVGL task.
 *   A second caller would interleave with that function's four register
 *   writes (AB1 -> FLMRD -> FLM2 -> VEN, r_glcdc.c:677-685) and could latch
 *   "layer visible + FLM2 = 0", pointing the GLCDC at address 0 for a 1.2 MB
 *   read. So this command only declares the desired state and the owning task
 *   reconciles it on its next frame, per the concurrency rules in CLAUDE.md.
 *
 * Execution context: ntshell_task. Blocks for at most
 * GLCDC_BLANK_APPLY_TIMEOUT_MS while waiting for the LVGL task to apply the
 * request; no lock is held.
 *
 * @param argc Argument count
 * @param argv Argument vector
 *
 * @return CMD_OK on success (including "declared but not applied yet"),
 *         CMD_ERR_USAGE / CMD_ERR_INVALID_ARG / CMD_ERR_EXECUTE otherwise
 */
static int glcdc_cmd_blank(int argc, char **argv)
{
    char buf[GLCDC_PRINT_BUF_SIZE];
    uint32_t want;

    if (s_glcdc_status != GLCDC_STATUS_INITIALIZED) {
        print_to_console("  Error: GLCDC is not initialized.\r\n");
        return CMD_ERR_EXECUTE;
    }

    if (argc < 3) {
        print_to_console("Usage: display blank <on|off>\r\n");
        print_to_console("  on  - Hide the graphics plane; show BG.BGC (black) instead.\r\n");
        print_to_console("        The panel then shows a colour the GLCDC generates\r\n");
        print_to_console("        internally, with no SDRAM read involved.\r\n");
        print_to_console("  off - Show the rendered framebuffer again.\r\n");
        print_to_console("  Diagnostic only (Issue #218). Remember to turn it off.\r\n");
        return CMD_ERR_USAGE;
    }

    if (ntlibc_strcmp(argv[2], "on") == 0) {
        want = 1u;
    } else if (ntlibc_strcmp(argv[2], "off") == 0) {
        want = 0u;
    } else {
        snprintf(buf, sizeof(buf), "  Error: expected 'on' or 'off', got '%s'.\r\n", argv[2]);
        print_to_console(buf);
        return CMD_ERR_INVALID_ARG;
    }

    /*
     * Declare the desired state. The LVGL task samples it in its flush
     * callback and drives the GLCDC; nothing is written to the hardware here.
     */
    s_blank_desired = want;

    /*
     * Wait for the LVGL task to apply it. This only happens when LVGL renders
     * a frame, so a timeout is itself a useful result: it means LVGL is not
     * flushing (nothing invalidated, or rendering has stalled).
     */
    {
        uint32_t waited_ms = 0;
        /* Sampled so a timeout can tell "LVGL never flushed" from "the flush
         * happened but R_GLCDC_BufferChange() rejected it". */
        const uint32_t err_before = s_bufchange_err_count;

        while ((s_blank_applied != want) && (waited_ms < GLCDC_BLANK_APPLY_TIMEOUT_MS)) {
            tk_dly_tsk(GLCDC_BLANK_POLL_MS);
            waited_ms += GLCDC_BLANK_POLL_MS;
        }

        if (s_blank_applied == want) {
            snprintf(buf, sizeof(buf), "  Blank %s, applied by LVGL after %lu ms.\r\n",
                     (want != 0u) ? "ON" : "OFF",
                     (unsigned long)waited_ms);
            print_to_console(buf);
        } else if (s_bufchange_err_count != err_before) {
            snprintf(buf, sizeof(buf),
                     "  Blank %s NOT applied: R_GLCDC_BufferChange() failed (%ld).\r\n",
                     (want != 0u) ? "ON" : "OFF",
                     (long)s_bufchange_last_err);
            print_to_console(buf);
            print_to_console("  LVGL flushed, but the GLCDC registers were not updated.\r\n");
            return CMD_ERR_EXECUTE;
        } else {
            snprintf(buf, sizeof(buf),
                     "  Blank %s requested, but LVGL has not flushed within %u ms.\r\n",
                     (want != 0u) ? "ON" : "OFF",
                     (unsigned int)GLCDC_BLANK_APPLY_TIMEOUT_MS);
            print_to_console(buf);
            print_to_console("  LVGL is not rendering (nothing invalidated, or stalled).\r\n");
            print_to_console("  Check 'display dbuf' Flush Rate. It will take effect on the\r\n");
            print_to_console("  next frame LVGL draws.\r\n");
            return CMD_OK;
        }
    }

    if (want != 0u) {
        print_to_console("  Panel now shows BG.BGC (black) with no SDRAM read.\r\n");
        print_to_console("    black -> GLCDC output stage and panel are healthy\r\n");
        print_to_console("    white -> fault is in the output stage or the panel\r\n");
        print_to_console("  Confirm with 'display reg': FLMRD=0, AB1 DISPSEL=1.\r\n");
        /*
         * Measured side effect: with FLMRD = 0 the graphics FIFO is starved
         * while the plane pipeline keeps running, so layer 1 underflows every
         * frame. That latches GR0.MON.UNDFLST and makes glcdc_underflow_1_isr
         * fire, which advances s_underflow_count. Say so here, or the inflated
         * "display dbuf" Underflow Count gets chased as a real fault later.
         */
        print_to_console("  NOTE: blanking starves the layer-1 FIFO on purpose, so it\r\n");
        print_to_console("  inflates 'display dbuf' Underflow Count and latches\r\n");
        print_to_console("  GR0.MON.UNDFLST. Neither is a real fault.\r\n");
        print_to_console("  Run 'display blank off' to restore the picture.\r\n");
    }

    return CMD_OK;
}

/**
 * "display pins" sub-command handler (Issue #218)
 *
 * @details Reads back the PmnPFS pin-function register of every pin the GLCDC
 *          drives and checks that it is still assigned to the LCD graphics
 *          peripheral.
 *
 *          This is the last layer software can inspect. By the time this was
 *          added, Issue #218 had established that during the all-white fault
 *          the framebuffer holds a correct image, every GLCDC register is at
 *          its expected value, and hiding the graphics plane entirely (so the
 *          GLCDC emits its internally generated black background with no
 *          SDRAM access) still leaves the panel white. If a pin had reverted
 *          from peripheral function to GPIO the GLCDC would look perfectly
 *          healthy while nothing reached the panel - which is exactly that
 *          state. One pin is enough when it is DISP_CLK, PARLCD_DE,
 *          PARLCD_HSYNC or PARLCD_VSYNC; a stray data pin would only corrupt
 *          colour, not blank the screen.
 *
 *          Expected PmnPFS bits: PSEL = 0x19 (IOPORT_PERIPHERAL_LCD_GRAPHICS,
 *          ra/fsp/inc/instances/r_ioport.h:428, shifted by
 *          IOPORT_PRV_PFS_PSEL_OFFSET = 24) and PMR = 1
 *          (IOPORT_CFG_PERIPHERAL_PIN = 0x00010000, r_ioport.h:481). The pin
 *          list and the expectation both come from ra_gen/pin_data.c, where
 *          all 30 entries are
 *          IOPORT_CFG_DRIVE_HIGH | IOPORT_CFG_PERIPHERAL_PIN |
 *          IOPORT_PERIPHERAL_LCD_GRAPHICS.
 *
 *          Other PmnPFS fields are printed but not judged: PIDR reflects the
 *          live input level and changes on its own, and the drive strength
 *          (DSCR, IOPORT_CFG_DRIVE_HIGH = 0x00000C00) affects signal quality
 *          rather than whether the pin is connected to the GLCDC at all.
 *
 *          Issue #222: these 30 pins are not the whole signal path. Two more
 *          lines reach the panel from outside the GLCDC, and holding either
 *          one low blanks the display without disturbing anything above.
 *          glcdc_pins_report_gpio() covers them in a second section.
 *
 * Execution context: ntshell_task. Reads only - PmnPFS reads need no PWPR
 * unlock, and nothing here writes a pin register. Does not block.
 */
/**
 * Every pin ra_gen/pin_data.c assigns to IOPORT_PERIPHERAL_LCD_GRAPHICS, with
 * the board net name from ra_cfg/fsp_cfg/bsp/bsp_pin_cfg.h. Shared by
 * "display pins" (function assignment) and "display signals" (live activity).
 *
 * "driven" marks the pins this configuration actually drives, which is what
 * "display signals" judges. Four of the thirty are deliberately idle and must
 * not be reported as faults:
 *   PARLCD_HSYNC (P805, LCD_TCON0) and PARLCD_VSYNC (P806, LCD_TCON1)
 *     - g_display0_extend_cfg.tcon_hsync / .tcon_vsync are
 *       GLCDC_TCON_PIN_NONE, so R_GLCDC_Open() never assigns them a signal
 *       (r_glcdc.c:1286-1294). This panel runs DE-only.
 *   DISP_TCON3 (P513, LCD_TCON3)
 *     - tcon_de is GLCDC_TCON_PIN_2, so TCON3 carries nothing.
 *   PARLCD_EXTCLK (P710)
 *     - g_display0_extend_cfg.clksrc is GLCDC_CLK_SRC_INTERNAL; the external
 *       clock input is unused.
 * The DE pin is PARLCD_DE (P807) = LCD_TCON2, confirmed on hardware: it
 * measured 74.4% high against the 72.0% duty the configured timing implies
 * ((1024/1344) * (600/635)).
 */
static const struct {
    uint16_t    pin;
    const char *name;
    bool        driven;
} g_glcdc_pins[GLCDC_PIN_COUNT] = {
        { (uint16_t)PARLCD_D9G1,                   "PARLCD_D9G1",     true  },
        { (uint16_t)DISP_TCON3,                    "DISP_TCON3",      false },
        { (uint16_t)DISP_CLK,                      "DISP_CLK",        true  },
        { (uint16_t)PARLCD_D18R2,                  "PARLCD_D18R2",    true  },
        { (uint16_t)PARLCD_EXTCLK,                 "PARLCD_EXTCLK",   false },
        { (uint16_t)PARLCD_D19R3,                  "PARLCD_D19R3",    true  },
        { (uint16_t)PARLCD_D20R4,                  "PARLCD_D20R4",    true  },
        { (uint16_t)PARLCD_D21R5,                  "PARLCD_D21R5",    true  },
        { (uint16_t)PARLCD_D22R6,                  "PARLCD_D22R6",    true  },
        { (uint16_t)PARLCD_D23R7,                  "PARLCD_D23R7",    true  },
        { (uint16_t)PARLCD_HSYNC,                  "PARLCD_HSYNC",    false },
        { (uint16_t)PARLCD_VSYNC,                  "PARLCD_VSYNC",    false },
        { (uint16_t)PARLCD_DE,                     "PARLCD_DE",       true  },
        { (uint16_t)PARLCD_D3B3_PARCAM_D1,         "PARLCD_D3B3",     true  },
        { (uint16_t)PARLCD_D2B2,                   "PARLCD_D2B2",     true  },
        { (uint16_t)PARLCD_D8G0,                   "PARLCD_D8G0",     true  },
        { (uint16_t)PARLCD_D4B4,                   "PARLCD_D4B4",     true  },
        { (uint16_t)PARLCD_D5B5,                   "PARLCD_D5B5",     true  },
        { (uint16_t)PARLCD_D6B6,                   "PARLCD_D6B6",     true  },
        { (uint16_t)PARLCD_D7B7,                   "PARLCD_D7B7",     true  },
        { (uint16_t)PARLCD_D0B0,                   "PARLCD_D0B0",     true  },
        { (uint16_t)PARLCD_D1B1,                   "PARLCD_D1B1",     true  },
        { (uint16_t)PARLCD_D17R1,                  "PARLCD_D17R1",    true  },
        { (uint16_t)PARLCD_D13G5,                  "PARLCD_D13G5",    true  },
        { (uint16_t)PARLCD_D16R0_PARCAM_VSYNC,     "PARLCD_D16R0",    true  },
        { (uint16_t)PARLCD_D15G7_PARCAM_HSYNC,     "PARLCD_D15G7",    true  },
        { (uint16_t)PARLCD_D14G6_PARCAM_PCLK,      "PARLCD_D14G6",    true  },
        { (uint16_t)PARLCD_D12G4,                  "PARLCD_D12G4",    true  },
        { (uint16_t)PARLCD_D11G3,                  "PARLCD_D11G3",    true  },
    { (uint16_t)PARLCD_D10G2,                  "PARLCD_D10G2",    true  },
};

/**
 * The two LCD control lines the GLCDC does not drive (Issue #222)
 *
 * "display pins" and "display signals" only ever looked at the 30 pins above,
 * so these two were invisible to every command. DISP_RESET is the dangerous
 * one: while it is held low the panel stays blank and the GLCDC registers,
 * the framebuffer and the pin waveforms all still look perfectly healthy -
 * indistinguishable from the Issue #218 fault by software alone. It also
 * resets the GT911 touch controller, which is why a working "touch read" is
 * independent evidence that the line is released.
 *
 * ra_gen/pin_data.c:255-257 (P514) and :267-269 (P606) configure both as
 * IOPORT_CFG_DRIVE_MID | PORT_DIRECTION_OUTPUT | PORT_OUTPUT_LOW, so reset
 * asserted and backlight off is the normal state at power-on. glcdc_lcd_reset()
 * releases DISP_RESET during glcdc_port_init(); DISP_BLEN is raised later, by
 * glcdc_backlight_on_event() after the first LVGL flush.
 *
 * "low_is_fault" is what separates the two. DISP_RESET low always breaks the
 * display. DISP_BLEN low does not: it is the state both after "display
 * backlight off" and before the first flush, so it is reported as information
 * rather than as a fault.
 *
 * PR #224 review: a low level also means different things before and after
 * glcdc_port_init() has run, so each pin carries two notes. usermain.c starts
 * ntshell_task (priority 12) before it even creates lvgl_task (priority 14),
 * which is what calls glcdc_port_init(), so "display pins" can genuinely run
 * while both pins are still at their pin_data.c reset level.
 *
 * The early note re-attributes the cause; it does not clear "low_is_fault".
 * A DISP_RESET still low because lvgl_task never reached glcdc_port_init()
 * (failed to start, hung, crashed) is exactly why the panel would be blank,
 * and that is worth reporting - naming the reason is more useful than
 * suppressing the verdict, which would recreate the Issue #222 blind spot in
 * a new place.
 */
static const struct {
    uint16_t    pin;
    const char *name;
    const char *role;
    bool        low_is_fault;
    const char *low_note;       /* PODR=0 once glcdc_port_init() has run */
    const char *low_note_early;  /* PODR=0 before it has run */
} g_glcdc_gpio_pins[GLCDC_GPIO_PIN_COUNT] = {
    { (uint16_t)GLCDC_PIN_RESET,     "DISP_RESET", "panel + touch reset", true,
      "<<< RESET ASSERTED (panel and touch held in reset)",
      "<<< RESET NOT RELEASED (display init has not run yet)" },
    { (uint16_t)GLCDC_PIN_BACKLIGHT, "DISP_BLEN",  "backlight enable",    false,
      "-- backlight off (normal after 'display backlight off')",
      "-- backlight off (raised after the first LVGL flush)" },
};

static void glcdc_cmd_pins(void)
{
    char buf[GLCDC_PRINT_BUF_SIZE];
    uint32_t ok_count = 0;

    print_to_console("[GLCDC Pin Function (PmnPFS) Readback]\r\n");
    print_to_console("  Expect PSEL=0x19 (LCD_GRAPHICS) and PMR=1 on every pin.\r\n");

    for (uint32_t i = 0; i < GLCDC_PIN_COUNT; i++) {
        const uint32_t pin = g_glcdc_pins[i].pin;
        const uint32_t pfs = R_PFS->PORT[pin >> 8].PIN[pin & 0xFFU].PmnPFS;

        /* PSEL[28:24] and PMR[16] are what decides whether the GLCDC drives
         * this pin at all. Everything else is left out of the verdict. */
        const bool ok = ((pfs & (GLCDC_PFS_PSEL_MASK | GLCDC_PFS_PMR_BIT)) ==
                         (GLCDC_PFS_PSEL_LCD_GRAPHICS | GLCDC_PFS_PMR_BIT));

        if (ok) {
            ok_count++;
        }

        snprintf(buf, sizeof(buf), "  %-13s P%u%02u 0x%08lX PSEL=0x%02lX PMR=%lu %s\r\n",
                 g_glcdc_pins[i].name,
                 (unsigned int)(pin >> 8),
                 (unsigned int)(pin & 0xFFU),
                 (unsigned long)pfs,
                 (unsigned long)((pfs >> 24) & 0x1FUL),
                 (unsigned long)((pfs >> 16) & 1UL),
                 ok ? "" : "<<< NOT LCD_GRAPHICS");
        print_to_console(buf);
    }

    snprintf(buf, sizeof(buf), "  %lu / %u pins assigned to the GLCDC.%s\r\n",
             (unsigned long)ok_count,
             (unsigned int)GLCDC_PIN_COUNT,
             (ok_count == GLCDC_PIN_COUNT) ?
             "" : "  <<< SIGNAL PATH BROKEN AT THE PIN");
    print_to_console(buf);

    glcdc_pins_report_gpio();
}

/**
 * Second half of "display pins": the GPIO control lines (Issue #222)
 *
 * @details Reports DISP_RESET (P606) and DISP_BLEN (P514). These get their own
 *          section because their pass condition is the inverse of the 30 pins
 *          above - here PSEL=0 and PMR=0 mean "still a GPIO", which is correct,
 *          whereas on a GLCDC pin that would mean the peripheral had lost it.
 *
 *          PIDR is judged here, unlike in the GLCDC section where it is only
 *          printed. On a peripheral pin PIDR changes on its own every frame; on
 *          a static push-pull output it must equal PODR, so any disagreement
 *          means something off-chip is fighting the driver.
 *
 *          A low level is judged against s_glcdc_status, because both pins
 *          power up low and are only raised during display bring-up. See the
 *          note on g_glcdc_gpio_pins for why that re-words the verdict instead
 *          of suppressing it.
 *
 * Execution context: ntshell_task, via glcdc_cmd_pins(). Reads only - PmnPFS
 * reads need no PWPR unlock (see R_BSP_PinRead(), bsp_io.h:347-351), and
 * nothing here writes a pin register, so this cannot disturb a running panel.
 * Does not block. s_glcdc_status is written only by lvgl_task and read here
 * the same way the other "display" sub-commands already read it (:747, :1210,
 * :1635, :1757); a stale read can only mis-word one line of diagnostics.
 */
static void glcdc_pins_report_gpio(void)
{
    char buf[GLCDC_PRINT_BUF_SIZE];
    uint32_t fault_count = 0;

    /* Both pins power up low (ra_gen/pin_data.c) and are raised during display
     * bring-up, so a low level only means "wrong" once that has run. Sampled
     * once here rather than per pin so both lines describe the same instant. */
    const bool early = (GLCDC_STATUS_NOT_INITIALIZED == s_glcdc_status);

    print_to_console("[LCD GPIO Control Pins (PmnPFS) Readback]\r\n");
    print_to_console("  Not GLCDC pins. Expect PSEL=0x00, PMR=0, PDR=1, PODR=PIDR.\r\n");

    snprintf(buf, sizeof(buf), "  Display init: %s\r\n",
             early ? "NOT RUN YET - a low level here is the power-on state" :
             ((GLCDC_STATUS_ERROR == s_glcdc_status) ? "FAILED" : "done"));
    print_to_console(buf);

    for (uint32_t i = 0; i < GLCDC_GPIO_PIN_COUNT; i++) {
        const uint32_t pin  = g_glcdc_gpio_pins[i].pin;
        const uint32_t pfs  = R_PFS->PORT[pin >> 8].PIN[pin & 0xFFU].PmnPFS;
        const uint32_t pmr  = (pfs >> 16) & 1UL;
        const uint32_t psel = (pfs >> 24) & 0x1FUL;
        const bool podr = (0U != (pfs & GLCDC_PFS_PODR_BIT));
        const bool pidr = (0U != (pfs & GLCDC_PFS_PIDR_BIT));
        const bool pdr  = (0U != (pfs & GLCDC_PFS_PDR_BIT));

        /* First match wins - the checks run most to least fundamental, so a pin
         * that is not even a GPIO any more is not also reported as "not an
         * output". */
        const char *verdict = "";
        bool fault = true;

        if ((0U != pmr) || (0U != psel)) {
            verdict = "<<< NOT GPIO (taken by a peripheral)";
        } else if (!pdr) {
            verdict = "<<< NOT OUTPUT";
        } else if (podr && !pidr) {
            verdict = "<<< DRIVEN HIGH BUT PAD IS LOW (pulled down off-chip)";
        } else if (!podr && pidr) {
            verdict = "<<< DRIVEN LOW BUT PAD IS HIGH (driven off-chip)";
        } else if (!podr) {
            /* Software really is holding the line low. What that means depends
             * on which line it is and on whether display bring-up has run. */
            verdict = early ? g_glcdc_gpio_pins[i].low_note_early
                            : g_glcdc_gpio_pins[i].low_note;
            fault   = g_glcdc_gpio_pins[i].low_is_fault;
        } else {
            fault = false;
        }

        if (fault) {
            fault_count++;
        }

        snprintf(buf, sizeof(buf),
                 "  %-10s P%u%02u 0x%08lX PODR=%lu PIDR=%lu PDR=%lu PMR=%lu PSEL=0x%02lX\r\n",
                 g_glcdc_gpio_pins[i].name,
                 (unsigned int)(pin >> 8),
                 (unsigned int)(pin & 0xFFU),
                 (unsigned long)pfs,
                 (unsigned long)podr,
                 (unsigned long)pidr,
                 (unsigned long)pdr,
                 (unsigned long)pmr,
                 (unsigned long)psel);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "    %s%s%s\r\n",
                 g_glcdc_gpio_pins[i].role,
                 ('\0' == verdict[0]) ? "" : "  ",
                 verdict);
        print_to_console(buf);
    }

    snprintf(buf, sizeof(buf), "  %lu / %u GPIO control pins healthy.%s\r\n",
             (unsigned long)(GLCDC_GPIO_PIN_COUNT - fault_count),
             (unsigned int)GLCDC_GPIO_PIN_COUNT,
             (0U == fault_count) ? "" : "  <<< PANEL CANNOT DISPLAY");
    print_to_console(buf);
}

/**
 * "display signals" sub-command handler (Issue #218)
 *
 * @details Samples the live input level (PmnPFS.PIDR) of every GLCDC pin over
 *          a window spanning several frames and reports which pins ever
 *          changed state. A pin that never changes while the GLCDC is running
 *          is not being driven.
 *
 *          This is the last thing software can say about the all-white fault.
 *          By this point everything the software configures has been verified
 *          correct - registers, TCON/DE routing, panel clock, all 30 pin
 *          function assignments - and hiding the graphics plane entirely
 *          still leaves the panel white. What is left is whether the signals
 *          actually leave the MCU:
 *            - DISP_CLK stuck    -> no pixel clock reaches the panel
 *            - PARLCD_DE stuck   -> no data-enable window
 *            - data lines stuck  -> no pixel data
 *            - everything toggles -> the MCU drives the panel correctly and
 *                                    the fault is past the pins (connector,
 *                                    flex, expansion board, or the panel)
 *          Either way it decides which trace to put a probe on first.
 *
 *          Sampling: GLCDC_SIGNALS_ROUNDS bursts of GLCDC_SIGNALS_BURST reads
 *          per pin, separated by GLCDC_SIGNALS_GAP_MS. The gap is deliberately
 *          not a divisor of the ~14.08 ms frame period (71 Hz), so successive
 *          bursts land at different phases of the frame instead of resampling
 *          the same lines.
 *
 *          Reading a level says nothing about frequency - the pixel clock is
 *          far faster than this loop, so the samples are effectively random
 *          phases. That is enough to answer "does it ever change?", which is
 *          the only question being asked.
 *
 *          Caveat worth knowing before reading the output: a data line is
 *          legitimately constant if the picture being scanned out happens to
 *          hold that bit constant. Run "display test checker1" first - a
 *          one-pixel white/black checkerboard forces every data line to
 *          toggle at the pixel clock - so that "stuck" means something.
 *
 *          PARLCD_HSYNC / PARLCD_VSYNC carry no expectation here: this project
 *          sets tcon_hsync and tcon_vsync to GLCDC_TCON_PIN_NONE and drives
 *          only DE (see "display reg"), so those pins keep whatever their
 *          TCON registers default to.
 *
 * Execution context: ntshell_task. Reads only. Sleeps in tk_dly_tsk between
 * bursts for a total of about GLCDC_SIGNALS_ROUNDS * GLCDC_SIGNALS_GAP_MS.
 */
static void glcdc_cmd_signals(void)
{
    char buf[GLCDC_PRINT_BUF_SIZE];

    /*
     * uint16_t, not uint32_t: the maximum count is
     * GLCDC_SIGNALS_ROUNDS * GLCDC_SIGNALS_BURST = 2,000, and ntshell_task
     * runs on a 4 KB stack (usermain.c: stksz = 4096). Two 30-entry arrays
     * cost 120 bytes this way instead of 240.
     */
    uint16_t seen_low[GLCDC_PIN_COUNT];
    uint16_t seen_high[GLCDC_PIN_COUNT];
    uint32_t toggling = 0;

    for (uint32_t i = 0; i < GLCDC_PIN_COUNT; i++) {
        seen_low[i]  = 0;
        seen_high[i] = 0;
    }

    if (s_glcdc_status != GLCDC_STATUS_INITIALIZED) {
        print_to_console("  Error: GLCDC is not initialized.\r\n");
        return;
    }

    print_to_console("[GLCDC Pin Signal Activity]\r\n");
    print_to_console("  Hint: run 'display test checker1' first so every data\r\n");
    print_to_console("  line is forced to toggle at the pixel clock.\r\n");

    for (uint32_t round = 0; round < GLCDC_SIGNALS_ROUNDS; round++) {
        for (uint32_t burst = 0; burst < GLCDC_SIGNALS_BURST; burst++) {
            for (uint32_t i = 0; i < GLCDC_PIN_COUNT; i++) {
                const uint32_t pin = g_glcdc_pins[i].pin;
                const uint32_t pidr =
                    (R_PFS->PORT[pin >> 8].PIN[pin & 0xFFU].PmnPFS >> 1) & 1UL;

                if (pidr != 0u) {
                    seen_high[i] = (uint16_t)(seen_high[i] + 1u);
                } else {
                    seen_low[i] = (uint16_t)(seen_low[i] + 1u);
                }
            }
        }

        /* Let the scan-out advance to a different phase before the next burst. */
        tk_dly_tsk(GLCDC_SIGNALS_GAP_MS);
    }

    for (uint32_t i = 0; i < GLCDC_PIN_COUNT; i++) {
        const uint32_t pin = g_glcdc_pins[i].pin;
        const bool changed = ((seen_low[i] != 0u) && (seen_high[i] != 0u));
        const char *verdict;

        if (!g_glcdc_pins[i].driven) {
            /* Idle by configuration - see the note on g_glcdc_pins. */
            verdict = changed ? "toggling (not driven in this config)"
                              : "idle (not driven in this config)";
        } else if (changed) {
            verdict = "toggling";
            toggling++;
        } else if (seen_high[i] != 0u) {
            verdict = "STUCK HIGH  <<<";
        } else {
            verdict = "STUCK LOW   <<<";
        }

        snprintf(buf, sizeof(buf), "  %-13s P%u%02u lo=%-5u hi=%-5u %s\r\n",
                 g_glcdc_pins[i].name,
                 (unsigned int)(pin >> 8),
                 (unsigned int)(pin & 0xFFU),
                 (unsigned int)seen_low[i],
                 (unsigned int)seen_high[i],
                 verdict);
        print_to_console(buf);
    }

    {
        uint32_t driven_count = 0;

        for (uint32_t i = 0; i < GLCDC_PIN_COUNT; i++) {
            if (g_glcdc_pins[i].driven) {
                driven_count++;
            }
        }

        snprintf(buf, sizeof(buf), "  %lu / %lu driven pins toggling over ~%u ms.\r\n",
                 (unsigned long)toggling,
                 (unsigned long)driven_count,
                 (unsigned int)(GLCDC_SIGNALS_ROUNDS * GLCDC_SIGNALS_GAP_MS));
        print_to_console(buf);

        if (toggling == driven_count) {
            print_to_console("  Every signal the GLCDC drives is present at the pin:\r\n");
            print_to_console("  the MCU side is working. Look past the pins - connector,\r\n");
            print_to_console("  flex, expansion board, panel.\r\n");
        } else {
            print_to_console("  A pin marked <<< is where to put the probe first.\r\n");
        }
    }
}

/*
 * Issue #183 / #218: "display test" used to be excluded from the default
 * build. Issue #218 needs it in every build: it is the only way to put known
 * pixels into the GLCDC framebuffer WITHOUT going through LVGL, which
 * separates "LVGL renders white" from "the GLCDC/panel output path is
 * broken". Measured cost of building it (and the pattern generators below)
 * in: 3,072 bytes of internal flash. See src/diag_config.h.
 */

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
     * R-006 (Issue #156): RM_LVGL_PORT_Open (FSP, FreeRTOS-dependent Vsync
     * semaphore / tick) is BYPASSED. lvgl_port_mtk3_open() performs the same
     * sequence with uT-Kernel primitives, reusing g_lvgl_port_cfg (ra_gen)
     * read-only. The Vsync/underflow statistics callback (lvgl_glcdc_callback
     * below) keeps being invoked through g_lvgl_port_cfg.p_callback.
     *
     * Reference: e2studio_CPU0/src/port/lvgl_port_mtk3.c (1:1 with rm_lvgl_port.c:82-171)
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c:78-83
     */
    err = lvgl_port_mtk3_open(&g_lvgl_port_cfg);
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
    /* R-006: g_lvgl_port_ctrl.p_lv_display -> lvgl_port_mtk3_get_display()
     * (the control block is no longer populated since RM_LVGL_PORT_Open is
     * bypassed). */
    lv_display_add_event_cb(lvgl_port_mtk3_get_display(),
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
 * @details Called from the display port's internal display callback
 *          (R-006: lvgl_port_mtk3_display_callback in src/port/lvgl_port_mtk3.c,
 *          which performs the same event conversion as the original
 *          _rm_lvgl_port_display_callback) after processing the Vsync
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
         * Issue #218: buffer-swap tracking used to live here. It counted
         * Vsyncs, not swaps, because this callback fires on every line-detect
         * interrupt regardless of whether LVGL flushed. The real flush count
         * is now recorded by glcdc_port_notify_flush() from the LVGL task.
         */
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

/*
 * Issue #183: test pattern generators (S-002-4). Verified with a tree-wide grep
 * of e2studio_CPU0/src that glcdc_port_draw_colorbar / _draw_gradient /
 * _draw_checker / _fill_color are referenced only by glcdc_cmd_test() above.
 * Issue #218 removed the build gate from both (see the comment on
 * glcdc_cmd_test); they are now always built. See src/diag_config.h.
 */

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
 * Get double-buffering status information (S-002-3, revised by Issue #218)
 *
 * @details Fills the double-buffering status structure with the counters
 *          maintained by glcdc_port_notify_flush() (LVGL task) and by the
 *          Vsync/underflow callback (ISR).
 *
 *          The buffer the GLCDC hardware is actually scanning out is NOT
 *          derived here. It lives in R_GLCDC->GR[0].FLM2 and is reported by
 *          the "display reg" sub-command (Issue #218); the value tracked in
 *          software is only "what LVGL last asked for".
 */
void glcdc_port_get_dbuf_status(glcdc_dbuf_status_t *status)
{
    if (status == NULL) {
        return;
    }

    status->flush_count         = s_flush_count;
    status->vsync_count         = s_vsync_count;
    status->last_flush_addr     = s_last_flush_addr;
    status->bufchange_err_count = s_bufchange_err_count;
    status->bufchange_last_err  = s_bufchange_last_err;
    status->underflow_count     = s_underflow_count;

#if GLCDC_CFG_LAYER_1_ENABLE
    status->double_buffer_enabled = true;
#else
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
 * Get the LVGL flush count (S-002-3, revised by Issue #218)
 */
uint32_t glcdc_port_get_flush_count(void)
{
    return s_flush_count;
}

/**
 * Query the diagnostic blank request (Issue #218)
 *
 * @details See glcdc_port.h. Called from lvgl_port_mtk3_flush_cb() on the
 *          LVGL task, once per rendered frame; a single volatile read.
 */
bool glcdc_port_blank_requested(void)
{
    return (s_blank_desired != 0u);
}

/**
 * Report the result of one LVGL flush (Issue #218)
 *
 * @details See glcdc_port.h for the execution-context contract. Only plain
 *          volatile stores here - this runs on every rendered frame, right
 *          before the Vsync wait, and must not block.
 */
void glcdc_port_notify_flush(const void *p_framebuffer, int32_t err)
{
    s_last_flush_addr = (uint32_t)(uintptr_t)p_framebuffer;
    s_flush_count++;

    if (err != (int32_t)FSP_SUCCESS) {
        s_bufchange_last_err = err;
        s_bufchange_err_count++;
    } else {
        /*
         * Issue #218: a NULL buffer means the flush callback honoured the
         * diagnostic blank request. Acknowledging it here (rather than in the
         * callback) keeps the whole reconcile visible in one place.
         *
         * Only a successful R_GLCDC_BufferChange() reached the registers, so
         * only that may acknowledge the request - otherwise "display blank"
         * would report a state the hardware never entered, during the very
         * investigation the command exists for (PR #220 review, P2).
         *
         * As configured this cannot currently happen: with
         * GLCDC_CFG_PARAM_CHECKING_ENABLE = 0 (it follows
         * BSP_CFG_PARAM_CHECKING_ENABLE, ra_cfg/fsp_cfg/bsp/bsp_cfg.h:33) the
         * only error R_GLCDC_BufferChange() can return is
         * FSP_ERR_INVALID_UPDATE_TIMING (r_glcdc.c:650-660), which the flush
         * callback retries. The guard is here because turning parameter
         * checking on is exactly what someone debugging this would do.
         */
        s_blank_applied = (NULL == p_framebuffer) ? 1u : 0u;
    }
}

/**
 * NT-Shell "display" command handler
 *
 * @details Provides GLCDC display diagnostic sub-commands:
 *   display status    - Show GLCDC initialization state, timing, clock, output config
 *   display fb        - Show frame buffer addresses, sizes, and format
 *   display dbuf      - Show double-buffering status (S-002-3)
 *   display reg       - Read back GLCDC hardware registers (#218)
 *   display fbstat    - Sample framebuffer contents (#218)
 *   display blank     - Hide graphics plane, show BG.BGC (#218)
 *   display pins      - Read back GLCDC and LCD control pin registers (#218, #222)
 *   display signals   - Detect GLCDC pins that are not toggling (#218)
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
        print_to_console("  reg       - Read back GLCDC hardware registers\r\n");
        print_to_console("  fbstat    - Sample framebuffer contents (white/black)\r\n");
        print_to_console("  blank     - Hide graphics plane; show background colour\r\n");
        print_to_console("  pins      - Read back GLCDC and LCD control pin registers\r\n");
        print_to_console("  signals   - Detect GLCDC pins that are not toggling\r\n");
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

    if (ntlibc_strcmp(argv[1], "reg") == 0) {
        glcdc_cmd_reg();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "fbstat") == 0) {
        glcdc_cmd_fbstat();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "blank") == 0) {
        return glcdc_cmd_blank(argc, argv);
    }

    if (ntlibc_strcmp(argv[1], "pins") == 0) {
        glcdc_cmd_pins();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "signals") == 0) {
        glcdc_cmd_signals();
        return CMD_OK;
    }

    /* Issue #218: no longer gated by MIMAMORI_VERBOSE_DIAG. */
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
    print_to_console("  reg       - Read back GLCDC hardware registers\r\n");
    print_to_console("  fbstat    - Sample framebuffer contents (white/black)\r\n");
    print_to_console("  blank     - Hide graphics plane; show background colour\r\n");
    print_to_console("  pins      - Read back GLCDC and LCD control pin registers\r\n");
    print_to_console("  signals   - Detect GLCDC pins that are not toggling\r\n");
    print_to_console("  test      - Draw test patterns on the LCD\r\n");
    print_to_console("  backlight - Control LCD backlight on/off\r\n");

    return CMD_ERR_INVALID_ARG;
}
