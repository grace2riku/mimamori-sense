/**
 * @file glcdc_port.c
 * @brief GLCDC (Graphics LCD Controller) port layer implementation
 * @details
 * Implements GLCDC initialization, display control, test pattern drawing,
 * and diagnostic functions for the 1024x600 LCD panel on the EK-RA8P1
 * Parallel Graphics Expansion Board.
 *
 * This module provides:
 *   - GLCDC initialization (LCD reset, RM_LVGL_PORT_Open, backlight enable)
 *   - Status tracking (initialization state, Vsync count)
 *   - LVGL GLCDC callback (Vsync counting, underflow detection)
 *   - Test pattern drawing (color bars, gradient, solid fill)
 *   - Timing parameter query (resolution, porches, sync widths)
 *   - Clock configuration query (LCDCLK, pixel clock, frame rate)
 *   - Frame buffer information query (addresses, sizes, format)
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

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/** Current GLCDC initialization status */
static glcdc_status_t s_glcdc_status = GLCDC_STATUS_NOT_INITIALIZED;

/** Vsync interrupt counter (incremented in lvgl_glcdc_callback) */
static volatile uint32_t s_vsync_count = 0;

/**********************************************************************************************************************
 Private (static) functions prototypes
 *********************************************************************************************************************/
static void glcdc_lcd_reset(void);
static void glcdc_backlight_on_event(lv_event_t *event);
static void glcdc_cmd_status(void);
static void glcdc_cmd_fb(void);
static void glcdc_cmd_test(int argc, char **argv);

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

    snprintf(buf, sizeof(buf), "  FB[0]       : 0x%08lX\r\n",
             (unsigned long)fb.fb0_addr);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  FB[1]       : 0x%08lX\r\n",
             (unsigned long)fb.fb1_addr);
    print_to_console(buf);

    print_to_console("[Placement]\r\n");
    print_to_console("  Section     : .sdram_noinit_nocache\r\n");
    print_to_console("  Cache       : Non-cacheable (MPU configured by BSP)\r\n");
    print_to_console("  Alignment   : 64-byte aligned\r\n");
}

/**
 * "display test" sub-command handler
 *
 * @details Draws test patterns on the LCD to verify display output.
 *          Supports the following sub-sub-commands:
 *            display test colorbar  - Vertical color bars (8 colors)
 *            display test gradient  - Horizontal gradients
 *            display test red       - Solid red fill
 *            display test green     - Solid green fill
 *            display test blue      - Solid blue fill
 *            display test white     - Solid white fill
 *            display test black     - Solid black fill
 *
 *          Test patterns are drawn directly to fb_background[0].
 *          The GLCDC hardware displays the buffer in real time.
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
     * Draw test patterns directly to fb_background[0].
     *
     * Note: In normal LVGL operation, LVGL manages framebuffer access.
     * This test bypasses LVGL and writes directly to the GLCDC framebuffer.
     * After testing, LVGL rendering should be restarted or the system
     * should be reset to restore normal display operation.
     */
    uint8_t *p_fb = &fb_background[0][0];

    if (argc < 3) {
        print_to_console("Usage: display test <pattern>\r\n");
        print_to_console("  colorbar  - Vertical color bars (R,G,B,Y,C,M,W,K)\r\n");
        print_to_console("  gradient  - Horizontal gradients\r\n");
        print_to_console("  red       - Solid red fill\r\n");
        print_to_console("  green     - Solid green fill\r\n");
        print_to_console("  blue      - Solid blue fill\r\n");
        print_to_console("  white     - Solid white fill\r\n");
        print_to_console("  black     - Solid black fill\r\n");
        return;
    }

    const char *pattern = argv[2];

    if (ntlibc_strcmp(pattern, "colorbar") == 0) {
        print_to_console("  Drawing color bar test pattern...\r\n");
        glcdc_port_draw_colorbar(p_fb);
        print_to_console("  Done.\r\n");
    } else if (ntlibc_strcmp(pattern, "gradient") == 0) {
        print_to_console("  Drawing gradient test pattern...\r\n");
        glcdc_port_draw_gradient(p_fb);
        print_to_console("  Done.\r\n");
    } else if (ntlibc_strcmp(pattern, "red") == 0) {
        print_to_console("  Filling with red...\r\n");
        glcdc_port_fill_color(p_fb, RGB565_RED);
        print_to_console("  Done.\r\n");
    } else if (ntlibc_strcmp(pattern, "green") == 0) {
        print_to_console("  Filling with green...\r\n");
        glcdc_port_fill_color(p_fb, RGB565_GREEN);
        print_to_console("  Done.\r\n");
    } else if (ntlibc_strcmp(pattern, "blue") == 0) {
        print_to_console("  Filling with blue...\r\n");
        glcdc_port_fill_color(p_fb, RGB565_BLUE);
        print_to_console("  Done.\r\n");
    } else if (ntlibc_strcmp(pattern, "white") == 0) {
        print_to_console("  Filling with white...\r\n");
        glcdc_port_fill_color(p_fb, RGB565_WHITE);
        print_to_console("  Done.\r\n");
    } else if (ntlibc_strcmp(pattern, "black") == 0) {
        print_to_console("  Filling with black...\r\n");
        glcdc_port_fill_color(p_fb, RGB565_BLACK);
        print_to_console("  Done.\r\n");
    } else {
        snprintf(buf, sizeof(buf), "  Error: Unknown pattern '%s'.\r\n", pattern);
        print_to_console(buf);
        print_to_console("  Available: colorbar, gradient, red, green, blue, white, black\r\n");
    }
#else
    FSP_PARAMETER_NOT_USED(argc);
    FSP_PARAMETER_NOT_USED(argv);
    print_to_console("  Error: GLCDC Layer 1 is not enabled.\r\n");
#endif
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
 *            - Detect and report GLCDC underflow errors
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
         * We log but do not assert, to allow diagnostics via NT-Shell.
         */
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
 * NT-Shell "display" command handler
 *
 * @details Provides GLCDC display diagnostic sub-commands:
 *   display status - Show GLCDC initialization state, timing, clock, output config
 *   display fb     - Show frame buffer addresses, sizes, and format
 *   display test   - Draw test patterns on the LCD (S-002-4)
 *
 * Reference: doc/design/glcdc-timing-parameters.md
 */
int usrcmd_display(int argc, char **argv)
{
    if (argc < 2) {
        cmd_print_usage("display", "<subcommand>");
        print_to_console("  status  - Show GLCDC timing parameters and configuration\r\n");
        print_to_console("  fb      - Show frame buffer addresses and sizes\r\n");
        print_to_console("  test    - Draw test patterns on the LCD\r\n");
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

    if (ntlibc_strcmp(argv[1], "test") == 0) {
        glcdc_cmd_test(argc, argv);
        return CMD_OK;
    }

    /* Unknown sub-command */
    {
        char buf[GLCDC_PRINT_BUF_SIZE];
        snprintf(buf, sizeof(buf), "Error: Unknown sub-command '%s'.\r\n", argv[1]);
        print_to_console(buf);
    }

    cmd_print_usage("display", "<subcommand>");
    print_to_console("  status  - Show GLCDC timing parameters and configuration\r\n");
    print_to_console("  fb      - Show frame buffer addresses and sizes\r\n");
    print_to_console("  test    - Draw test patterns on the LCD\r\n");

    return CMD_ERR_INVALID_ARG;
}
