/**
 * @file dave2d_port.c
 * @brief Dave2D (D/AVE 2D) graphics accelerator port layer implementation
 * @details
 * Implements Dave2D initialization tracking, status query, basic drawing
 * wrapper functions, color conversion utilities, LVGL-Dave2D integration
 * query, and diagnostic functions for the Renesas D/AVE 2D hardware
 * graphics engine.
 *
 * This module acts as a diagnostic, tracking, integration query,
 * drawing wrapper, and performance benchmarking layer over the Dave2D
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
 * Drawing Wrapper Architecture (S-004-2):
 *   The wrapper functions use the LVGL-managed Dave2D resources:
 *     - _d2_handle: The Dave2D device handle (created by LVGL)
 *     - _blit_renderbuffer: Separate render buffer for non-LVGL operations
 *     - xd2Semaphore: LVGL's Dave2D mutex for thread safety
 *
 *   Each drawing wrapper follows this pattern:
 *     1. Check dave2d is available (dave2d_port_is_available)
 *     2. Lock the LVGL Dave2D mutex (lv_mutex_lock)
 *     3. Select _blit_renderbuffer (separate from LVGL's main buffer)
 *     4. Set up framebuffer, clip rect, color, alpha
 *     5. Issue the rendering command (d2_renderbox, d2_renderline, etc.)
 *     6. Execute and flush (d2_executerenderbuffer + d2_flushframe)
 *     7. Restore _renderbuffer selection
 *     8. Unlock the mutex
 *
 *   Reference for blit pattern:
 *     e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:156-213
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
 * Performance Benchmarking (S-004-4):
 *   The benchmark functions use the DWT (Data Watchpoint and Trace) cycle
 *   counter on the Cortex-M85 to measure drawing operation times with
 *   sub-microsecond precision. The DWT CYCCNT register counts CPU clock
 *   cycles (1 GHz = 1ns per cycle).
 *
 *   Benchmark tests measure:
 *     - Rectangle fill (200x100): Dave2D vs CPU software
 *     - Line drawing (400px diagonal)
 *     - BLIT (64x64 source to framebuffer)
 *     - Alpha blending (200x100 with 50% alpha)
 *     - Full-screen fill (1024x600): Dave2D vs CPU software
 *
 *   Results include per-operation timing (microseconds), cycle counts,
 *   and speedup ratios (Dave2D vs CPU).
 *
 *   Reference: ARM Cortex-M85 TRM, DWT registers (0xE0001000)
 *
 * @note
 * This file is part of the Dave2D control (S-004-1, S-004-2, S-004-3, S-004-4)
 * implementation.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>

#include "dave2d_port.h"
#include "glcdc_port.h"
#include "dave_driver.h"
#include "lvgl.h"
#include "common_data.h"
#include "jlink_console.h"
#include "cmd_utils.h"
#include "ntlibc.h"

#include "FreeRTOS.h"
#include "task.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size */
#define DAVE2D_PRINT_BUF_SIZE   (128)

/**
 * D2_FIX4 macro for converting integer to 12:4 fixed-point
 *
 * Reference: e2studio_CPU0/ra/tes/dave2d/inc/dave_driver.h
 *   "if you want to pass an integer value of 42 to a function that expects
 *    an argument of e.g. type <d2_point> you would have to write :
 *    > function( 42 << 4 );    // conversion from integer to fixedpoint"
 */
#ifndef D2_FIX4
#define D2_FIX4(x)  ((d2_s32)((x) << 4))
#endif

/**
 * CPU clock frequency for DWT cycle counter to microsecond conversion
 *
 * CPUCLK = PLL1P (1GHz) / 1 = 1,000,000,000 Hz
 *
 * Reference: e2studio_CPU0/ra_gen/bsp_clock_cfg.h:13,28-29
 *   BSP_CFG_PLL1P_FREQUENCY_HZ = 1,000,000,000
 *   BSP_CFG_CPUCLK_DIV = BSP_CLOCKS_SYS_CLOCK_DIV_1
 */
#define DAVE2D_CPUCLK_HZ       (1000000000UL)

/** Number of benchmark iterations for averaging */
#define DAVE2D_BENCH_ITERATIONS (100)

/**
 * DWT (Data Watchpoint and Trace) register addresses for Cortex-M85
 *
 * The DWT provides a 32-bit cycle counter (CYCCNT) that counts CPU clock
 * cycles. On Cortex-M85, DWT is accessible at the standard ARM address.
 *
 * Reference: ARM Cortex-M85 Technical Reference Manual
 */
#define DWT_CTRL_ADDR           (0xE0001000UL)
#define DWT_CYCCNT_ADDR         (0xE0001004UL)
#define DWT_CTRL_CYCCNTENA_Msk  (1UL << 0)

/** Benchmark source buffer size for BLIT tests (64x64 pixels, RGB565) */
#define BENCH_BLIT_SRC_SIZE     (64)

/** Full-screen BLIT test width (matches display) */
#define BENCH_FULLSCREEN_W      GLCDC_DISPLAY_HSIZE
/** Full-screen BLIT test height (matches display) */
#define BENCH_FULLSCREEN_H      GLCDC_DISPLAY_VSIZE

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

/**
 * LVGL internal blit render buffer
 *
 * Used by the wrapper functions to avoid interfering with LVGL's main
 * render buffer (_renderbuffer). The blit render buffer is created by
 * lv_dave2d_init() at:
 *   e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:580-585
 *
 * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:60
 */
extern d2_renderbuffer *_blit_renderbuffer;

/**
 * LVGL internal main render buffer
 *
 * The primary render buffer used by LVGL for draw operations. The wrapper
 * functions restore selection to this buffer after using _blit_renderbuffer.
 *
 * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:59
 */
extern d2_renderbuffer *_renderbuffer;

/**
 * LVGL Dave2D mutex for thread safety
 *
 * All Dave2D operations must be serialized via this mutex because the
 * Dave2D hardware is a shared resource between the LVGL draw thread and
 * any other thread (e.g., NT-Shell) that uses the wrapper functions.
 *
 * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:65
 */
extern lv_mutex_t xd2Semaphore;
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
static void dave2d_cmd_integration(void);
static void dave2d_cmd_test(int argc, char **argv);
static void dave2d_cmd_bench(int argc, char **argv);

#if LV_USE_DRAW_DAVE2D
static void dave2d_cmd_test_rect(void);
static void dave2d_cmd_test_line(void);
static void dave2d_cmd_test_blit(void);
static void dave2d_cmd_test_alpha(void);

/* Benchmark helper functions (S-004-4) */
static void dave2d_dwt_init(void);
static uint32_t dave2d_dwt_get_cycles(void);
static uint32_t dave2d_cycles_to_us(uint32_t cycles);
static void dave2d_bench_fill_rect(void);
static void dave2d_bench_draw_line(void);
static void dave2d_bench_blit(void);
static void dave2d_bench_blit_fullscreen(void);
static void dave2d_bench_alpha_blend(void);
static void dave2d_bench_cpu_fill_rect(void);
static void dave2d_bench_cpu_fill_fullscreen(void);
static void dave2d_bench_summary(void);
#endif

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

    print_to_console("[Wrapper Functions (S-004-2)]\r\n");
    print_to_console("  dave2d_port_fill_rect()     - Filled rectangle\r\n");
    print_to_console("  dave2d_port_draw_rect()     - Rectangle outline\r\n");
    print_to_console("  dave2d_port_draw_line()     - Line drawing\r\n");
    print_to_console("  dave2d_port_blit()          - Texture BLIT (1:1 copy)\r\n");
    print_to_console("  dave2d_port_blit_scaled()   - Texture BLIT (scaled)\r\n");
    print_to_console("  dave2d_port_set_blend_mode()- Alpha blend mode\r\n");

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

/**
 * "dave2d integration" sub-command handler (S-004-3)
 *
 * @details Displays the LVGL-Dave2D integration status, including
 *          which draw operations are GPU-accelerated and which fall
 *          back to the software renderer.
 *
 * Reference:
 *   - _dave2d_evaluate: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:238-367
 *   - lv_draw_dave2d_init: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:76-108
 */
static void dave2d_cmd_integration(void)
{
    dave2d_port_print_lvgl_integration();
}

/**
 * "dave2d test" sub-command dispatcher (S-004-2)
 *
 * @details Dispatches test sub-commands for drawing wrapper verification.
 *          Test patterns are drawn directly to the GLCDC framebuffer
 *          using the Dave2D wrapper functions.
 *
 *          Note: These tests bypass LVGL and write directly to the
 *          framebuffer. LVGL rendering will overwrite the test patterns.
 *
 * @param argc  Argument count (from the original command)
 * @param argv  Argument vector
 */
static void dave2d_cmd_test(int argc, char **argv)
{
#if LV_USE_DRAW_DAVE2D
    if (!dave2d_port_is_available()) {
        print_to_console("  Error: Dave2D is not available.\r\n");
        return;
    }

    if (argc < 3) {
        print_to_console("Usage: dave2d test <pattern>\r\n");
        print_to_console("  rect   - Draw filled and outlined rectangles\r\n");
        print_to_console("  line   - Draw lines with various widths\r\n");
        print_to_console("  blit   - Draw BLIT test (source buffer copy)\r\n");
        print_to_console("  alpha  - Draw alpha blending test\r\n");
        print_to_console("  all    - Run all drawing tests\r\n");
        return;
    }

    const char *pattern = argv[2];

    if (ntlibc_strcmp(pattern, "rect") == 0) {
        dave2d_cmd_test_rect();
    } else if (ntlibc_strcmp(pattern, "line") == 0) {
        dave2d_cmd_test_line();
    } else if (ntlibc_strcmp(pattern, "blit") == 0) {
        dave2d_cmd_test_blit();
    } else if (ntlibc_strcmp(pattern, "alpha") == 0) {
        dave2d_cmd_test_alpha();
    } else if (ntlibc_strcmp(pattern, "all") == 0) {
        print_to_console("  Running all Dave2D drawing tests...\r\n");
        dave2d_cmd_test_rect();
        dave2d_cmd_test_line();
        dave2d_cmd_test_blit();
        dave2d_cmd_test_alpha();
        print_to_console("  All tests completed.\r\n");
    } else {
        char buf[DAVE2D_PRINT_BUF_SIZE];
        snprintf(buf, sizeof(buf), "  Error: Unknown test '%s'.\r\n", pattern);
        print_to_console(buf);
        print_to_console("  Available: rect, line, blit, alpha, all\r\n");
    }
#else
    (void)argc;
    (void)argv;
    print_to_console("  Error: LV_USE_DRAW_DAVE2D is disabled.\r\n");
#endif
}

#if LV_USE_DRAW_DAVE2D

/**
 * "dave2d test rect" - Draw test rectangles on both framebuffers
 *
 * @details Draws filled rectangles and outlined rectangles on the display
 *          to verify dave2d_port_fill_rect() and dave2d_port_draw_rect().
 *          Draws to both framebuffers for visibility with double buffering.
 */
static void dave2d_cmd_test_rect(void)
{
    char buf[DAVE2D_PRINT_BUF_SIZE];
    int result;
    dave2d_fb_t fb;

    print_to_console("  [Dave2D Test: Rectangle]\r\n");

    /* Draw to both framebuffers for visibility */
    for (uint32_t i = 0; i < 2; i++) {
        if (!dave2d_port_fb_from_glcdc(&fb, i)) {
            snprintf(buf, sizeof(buf), "  Error: Cannot get FB[%lu].\r\n",
                     (unsigned long)i);
            print_to_console(buf);
            return;
        }

        /* Red filled rectangle at (50, 50), 200x100 */
        result = dave2d_port_fill_rect(&fb, 50, 50, 200, 100,
                                       dave2d_port_rgb888_to_d2color(255, 0, 0), 255);
        if (result != DAVE2D_OK) {
            snprintf(buf, sizeof(buf), "  Error: fill_rect failed (%d) on FB[%lu].\r\n",
                     result, (unsigned long)i);
            print_to_console(buf);
            return;
        }

        /* Green filled rectangle at (300, 50), 200x100 */
        dave2d_port_fill_rect(&fb, 300, 50, 200, 100,
                              dave2d_port_rgb888_to_d2color(0, 255, 0), 255);

        /* Blue filled rectangle at (550, 50), 200x100 */
        dave2d_port_fill_rect(&fb, 550, 50, 200, 100,
                              dave2d_port_rgb888_to_d2color(0, 0, 255), 255);

        /* White outlined rectangle at (50, 200), 300x150, border 3px */
        dave2d_port_draw_rect(&fb, 50, 200, 300, 150,
                              dave2d_port_rgb888_to_d2color(255, 255, 255), 255, 3);

        /* Yellow outlined rectangle at (400, 200), 300x150, border 5px */
        dave2d_port_draw_rect(&fb, 400, 200, 300, 150,
                              dave2d_port_rgb888_to_d2color(255, 255, 0), 255, 5);
    }

    print_to_console("  Done. 3 filled rects (R,G,B) + 2 outlined rects (W,Y).\r\n");
}

/**
 * "dave2d test line" - Draw test lines on both framebuffers
 *
 * @details Draws lines with various colors and widths to verify
 *          dave2d_port_draw_line().
 */
static void dave2d_cmd_test_line(void)
{
    char buf[DAVE2D_PRINT_BUF_SIZE];
    int result;
    dave2d_fb_t fb;

    print_to_console("  [Dave2D Test: Line]\r\n");

    for (uint32_t i = 0; i < 2; i++) {
        if (!dave2d_port_fb_from_glcdc(&fb, i)) {
            snprintf(buf, sizeof(buf), "  Error: Cannot get FB[%lu].\r\n",
                     (unsigned long)i);
            print_to_console(buf);
            return;
        }

        /* Red horizontal line, width 2 */
        result = dave2d_port_draw_line(&fb, 50, 400, 400, 400,
                                       dave2d_port_rgb888_to_d2color(255, 0, 0), 255, 2);
        if (result != DAVE2D_OK) {
            snprintf(buf, sizeof(buf), "  Error: draw_line failed (%d) on FB[%lu].\r\n",
                     result, (unsigned long)i);
            print_to_console(buf);
            return;
        }

        /* Green vertical line, width 3 */
        dave2d_port_draw_line(&fb, 500, 370, 500, 550,
                              dave2d_port_rgb888_to_d2color(0, 255, 0), 255, 3);

        /* Blue diagonal line, width 4 */
        dave2d_port_draw_line(&fb, 550, 370, 900, 550,
                              dave2d_port_rgb888_to_d2color(0, 0, 255), 255, 4);

        /* White cross pattern, width 1 */
        dave2d_port_draw_line(&fb, 50, 450, 400, 550,
                              dave2d_port_rgb888_to_d2color(255, 255, 255), 255, 1);
        dave2d_port_draw_line(&fb, 50, 550, 400, 450,
                              dave2d_port_rgb888_to_d2color(255, 255, 255), 255, 1);
    }

    print_to_console("  Done. Horizontal, vertical, diagonal, and cross lines.\r\n");
}

/**
 * "dave2d test blit" - Draw a test BLIT pattern on both framebuffers
 *
 * @details Creates a small RGB565 source buffer with a checkerboard pattern
 *          and blits it to the framebuffer at multiple positions, including
 *          a scaled copy, to verify dave2d_port_blit() and
 *          dave2d_port_blit_scaled().
 */
static void dave2d_cmd_test_blit(void)
{
    char buf[DAVE2D_PRINT_BUF_SIZE];
    int result;
    dave2d_fb_t fb;

    print_to_console("  [Dave2D Test: BLIT]\r\n");

    /*
     * Create a small 32x32 checkerboard source buffer in RGB565 format.
     * The buffer is allocated on the stack. For larger images, SDRAM
     * allocation would be needed.
     */
    #define BLIT_TEST_SIZE  (32)
    static uint16_t src_buf[BLIT_TEST_SIZE * BLIT_TEST_SIZE];

    /* Fill source buffer with 4x4 pixel checkerboard (cyan/magenta) */
    for (int32_t y = 0; y < BLIT_TEST_SIZE; y++) {
        for (int32_t x = 0; x < BLIT_TEST_SIZE; x++) {
            uint32_t block_x = (uint32_t)x / 4;
            uint32_t block_y = (uint32_t)y / 4;
            if ((block_x ^ block_y) & 1U) {
                src_buf[y * BLIT_TEST_SIZE + x] = 0x07FF;  /* Cyan (RGB565) */
            } else {
                src_buf[y * BLIT_TEST_SIZE + x] = 0xF81F;  /* Magenta (RGB565) */
            }
        }
    }

    for (uint32_t i = 0; i < 2; i++) {
        if (!dave2d_port_fb_from_glcdc(&fb, i)) {
            snprintf(buf, sizeof(buf), "  Error: Cannot get FB[%lu].\r\n",
                     (unsigned long)i);
            print_to_console(buf);
            return;
        }

        /* 1:1 BLIT at position (800, 50) */
        result = dave2d_port_blit(&fb,
                                  src_buf, BLIT_TEST_SIZE,
                                  BLIT_TEST_SIZE, BLIT_TEST_SIZE,
                                  d2_mode_rgb565,
                                  800, 50);
        if (result != DAVE2D_OK) {
            snprintf(buf, sizeof(buf), "  Error: blit failed (%d) on FB[%lu].\r\n",
                     result, (unsigned long)i);
            print_to_console(buf);
            return;
        }

        /* Scaled BLIT (32x32 -> 96x96) at position (840, 50) */
        dave2d_port_blit_scaled(&fb,
                                src_buf, BLIT_TEST_SIZE,
                                BLIT_TEST_SIZE, BLIT_TEST_SIZE,
                                d2_mode_rgb565,
                                840, 100, 96, 96);

        /* 1:1 BLIT at another position (800, 200) */
        dave2d_port_blit(&fb,
                         src_buf, BLIT_TEST_SIZE,
                         BLIT_TEST_SIZE, BLIT_TEST_SIZE,
                         d2_mode_rgb565,
                         800, 200);
    }

    print_to_console("  Done. 32x32 checkerboard blitted at 1:1 and 3x scaled.\r\n");
    #undef BLIT_TEST_SIZE
}

/**
 * "dave2d test alpha" - Draw alpha blending test on both framebuffers
 *
 * @details Draws overlapping rectangles with different alpha values to
 *          verify dave2d_port_fill_rect() alpha parameter and
 *          dave2d_port_set_blend_mode().
 */
static void dave2d_cmd_test_alpha(void)
{
    char buf[DAVE2D_PRINT_BUF_SIZE];
    dave2d_fb_t fb;

    print_to_console("  [Dave2D Test: Alpha Blending]\r\n");

    for (uint32_t i = 0; i < 2; i++) {
        if (!dave2d_port_fb_from_glcdc(&fb, i)) {
            snprintf(buf, sizeof(buf), "  Error: Cannot get FB[%lu].\r\n",
                     (unsigned long)i);
            print_to_console(buf);
            return;
        }

        /* Draw a white background rectangle */
        dave2d_port_fill_rect(&fb, 50, 370, 400, 200,
                              dave2d_port_rgb888_to_d2color(255, 255, 255), 255);

        /* Red with alpha 255 (fully opaque) */
        dave2d_port_fill_rect(&fb, 60, 380, 80, 80,
                              dave2d_port_rgb888_to_d2color(255, 0, 0), 255);

        /* Red with alpha 192 (75% opaque) */
        dave2d_port_fill_rect(&fb, 150, 380, 80, 80,
                              dave2d_port_rgb888_to_d2color(255, 0, 0), 192);

        /* Red with alpha 128 (50% opaque) */
        dave2d_port_fill_rect(&fb, 240, 380, 80, 80,
                              dave2d_port_rgb888_to_d2color(255, 0, 0), 128);

        /* Red with alpha 64 (25% opaque) */
        dave2d_port_fill_rect(&fb, 330, 380, 80, 80,
                              dave2d_port_rgb888_to_d2color(255, 0, 0), 64);

        /* Overlapping: Green semi-transparent over previous */
        dave2d_port_fill_rect(&fb, 100, 420, 300, 80,
                              dave2d_port_rgb888_to_d2color(0, 255, 0), 128);

        /* Overlapping: Blue semi-transparent over previous */
        dave2d_port_fill_rect(&fb, 150, 460, 250, 80,
                              dave2d_port_rgb888_to_d2color(0, 0, 255), 128);
    }

    print_to_console("  Done. Red rects at 100%%/75%%/50%%/25%% alpha,\r\n");
    print_to_console("        overlapping green (50%%) and blue (50%%).\r\n");
}

/* ============================================================
 *  Benchmark Functions (S-004-4)
 * ============================================================ */

/**
 * Initialize the DWT cycle counter for performance measurement
 *
 * @details Enables the DWT CYCCNT register on the Cortex-M85 processor.
 *          The cycle counter runs at the CPU core clock frequency (1 GHz)
 *          and provides sub-microsecond timing resolution.
 *
 *          DWT_CTRL bit 0 (CYCCNTENA) must be set to enable the counter.
 *          The counter wraps around after ~4.295 seconds at 1 GHz.
 *
 * Reference: ARM Cortex-M85 Technical Reference Manual, DWT registers
 * Reference: e2studio_CPU0/ra_gen/bsp_clock_cfg.h:13 (CPUCLK = 1GHz)
 */
static void dave2d_dwt_init(void)
{
    volatile uint32_t *dwt_ctrl   = (volatile uint32_t *)DWT_CTRL_ADDR;
    volatile uint32_t *dwt_cyccnt = (volatile uint32_t *)DWT_CYCCNT_ADDR;

    /* Reset the cycle counter */
    *dwt_cyccnt = 0;

    /* Enable the cycle counter */
    *dwt_ctrl |= DWT_CTRL_CYCCNTENA_Msk;
}

/**
 * Read the current DWT cycle count
 *
 * @return Current value of the DWT CYCCNT register
 */
static uint32_t dave2d_dwt_get_cycles(void)
{
    volatile uint32_t *dwt_cyccnt = (volatile uint32_t *)DWT_CYCCNT_ADDR;
    return *dwt_cyccnt;
}

/**
 * Convert DWT cycle count to microseconds
 *
 * @details At 1 GHz CPUCLK, 1000 cycles = 1 microsecond.
 *
 * @param cycles  Number of CPU clock cycles
 * @return Elapsed time in microseconds
 */
static uint32_t dave2d_cycles_to_us(uint32_t cycles)
{
    return cycles / (DAVE2D_CPUCLK_HZ / 1000000UL);
}

/**
 * Benchmark: Rectangle fill (Dave2D accelerated)
 *
 * @details Measures the time to fill a 200x100 rectangle using
 *          dave2d_port_fill_rect(). Averages over DAVE2D_BENCH_ITERATIONS
 *          iterations.
 */
static void dave2d_bench_fill_rect(void)
{
    char buf[DAVE2D_PRINT_BUF_SIZE];
    dave2d_fb_t fb;
    uint32_t start, end, total_cycles;

    if (!dave2d_port_fb_from_glcdc(&fb, 0)) {
        print_to_console("  Error: Cannot get framebuffer.\r\n");
        return;
    }

    print_to_console("  [Fill Rect 200x100]\r\n");

    total_cycles = 0;
    for (uint32_t i = 0; i < DAVE2D_BENCH_ITERATIONS; i++) {
        start = dave2d_dwt_get_cycles();

        dave2d_port_fill_rect(&fb, 50, 50, 200, 100,
                              dave2d_port_rgb888_to_d2color(255, 0, 0), 255);

        end = dave2d_dwt_get_cycles();
        total_cycles += (end - start);
    }

    uint32_t avg_cycles = total_cycles / DAVE2D_BENCH_ITERATIONS;
    uint32_t avg_us = dave2d_cycles_to_us(avg_cycles);

    snprintf(buf, sizeof(buf),
             "    Dave2D: %lu us/op (%lu cycles, %lu iterations)\r\n",
             (unsigned long)avg_us,
             (unsigned long)avg_cycles,
             (unsigned long)DAVE2D_BENCH_ITERATIONS);
    print_to_console(buf);
}

/**
 * Benchmark: Line drawing (Dave2D accelerated)
 *
 * @details Measures the time to draw a diagonal line (400 pixels long,
 *          2 pixels wide) using dave2d_port_draw_line(). Averages over
 *          DAVE2D_BENCH_ITERATIONS iterations.
 */
static void dave2d_bench_draw_line(void)
{
    char buf[DAVE2D_PRINT_BUF_SIZE];
    dave2d_fb_t fb;
    uint32_t start, end, total_cycles;

    if (!dave2d_port_fb_from_glcdc(&fb, 0)) {
        print_to_console("  Error: Cannot get framebuffer.\r\n");
        return;
    }

    print_to_console("  [Line 400px diagonal, width 2]\r\n");

    total_cycles = 0;
    for (uint32_t i = 0; i < DAVE2D_BENCH_ITERATIONS; i++) {
        start = dave2d_dwt_get_cycles();

        dave2d_port_draw_line(&fb, 50, 50, 450, 350,
                              dave2d_port_rgb888_to_d2color(0, 255, 0), 255, 2);

        end = dave2d_dwt_get_cycles();
        total_cycles += (end - start);
    }

    uint32_t avg_cycles = total_cycles / DAVE2D_BENCH_ITERATIONS;
    uint32_t avg_us = dave2d_cycles_to_us(avg_cycles);

    snprintf(buf, sizeof(buf),
             "    Dave2D: %lu us/op (%lu cycles, %lu iterations)\r\n",
             (unsigned long)avg_us,
             (unsigned long)avg_cycles,
             (unsigned long)DAVE2D_BENCH_ITERATIONS);
    print_to_console(buf);
}

/**
 * Benchmark: BLIT 64x64 (Dave2D accelerated)
 *
 * @details Measures the time to blit a 64x64 RGB565 source buffer to
 *          the framebuffer using dave2d_port_blit(). Averages over
 *          DAVE2D_BENCH_ITERATIONS iterations.
 */
static void dave2d_bench_blit(void)
{
    char buf[DAVE2D_PRINT_BUF_SIZE];
    dave2d_fb_t fb;
    uint32_t start, end, total_cycles;

    if (!dave2d_port_fb_from_glcdc(&fb, 0)) {
        print_to_console("  Error: Cannot get framebuffer.\r\n");
        return;
    }

    /* Create a 64x64 test source buffer (checkerboard pattern) */
    static uint16_t bench_src[BENCH_BLIT_SRC_SIZE * BENCH_BLIT_SRC_SIZE];
    for (int32_t y = 0; y < BENCH_BLIT_SRC_SIZE; y++) {
        for (int32_t x = 0; x < BENCH_BLIT_SRC_SIZE; x++) {
            bench_src[y * BENCH_BLIT_SRC_SIZE + x] =
                ((x ^ y) & 1U) ? 0xFFFFU : 0x0000U;
        }
    }

    print_to_console("  [BLIT 64x64 RGB565]\r\n");

    total_cycles = 0;
    for (uint32_t i = 0; i < DAVE2D_BENCH_ITERATIONS; i++) {
        start = dave2d_dwt_get_cycles();

        dave2d_port_blit(&fb,
                         bench_src, BENCH_BLIT_SRC_SIZE,
                         BENCH_BLIT_SRC_SIZE, BENCH_BLIT_SRC_SIZE,
                         d2_mode_rgb565,
                         100, 100);

        end = dave2d_dwt_get_cycles();
        total_cycles += (end - start);
    }

    uint32_t avg_cycles = total_cycles / DAVE2D_BENCH_ITERATIONS;
    uint32_t avg_us = dave2d_cycles_to_us(avg_cycles);

    snprintf(buf, sizeof(buf),
             "    Dave2D: %lu us/op (%lu cycles, %lu iterations)\r\n",
             (unsigned long)avg_us,
             (unsigned long)avg_cycles,
             (unsigned long)DAVE2D_BENCH_ITERATIONS);
    print_to_console(buf);
}

/**
 * Benchmark: Full-screen fill (1024x600, Dave2D accelerated)
 *
 * @details Measures the time to fill the entire 1024x600 framebuffer
 *          using dave2d_port_fill_rect(). This simulates a full-screen
 *          background update.
 *
 *          Target: < 16.7ms (60fps frame budget)
 *
 * Reference: Issue #44 acceptance criterion:
 *   "Full-screen drawing completes within 16.7ms (60fps)"
 */
static void dave2d_bench_blit_fullscreen(void)
{
    char buf[DAVE2D_PRINT_BUF_SIZE];
    dave2d_fb_t fb;
    uint32_t start, end, total_cycles;
    uint32_t iterations = 10;  /* Fewer iterations for full-screen (expensive) */

    if (!dave2d_port_fb_from_glcdc(&fb, 0)) {
        print_to_console("  Error: Cannot get framebuffer.\r\n");
        return;
    }

    snprintf(buf, sizeof(buf), "  [Full-screen fill %ux%u]\r\n",
             (unsigned int)BENCH_FULLSCREEN_W,
             (unsigned int)BENCH_FULLSCREEN_H);
    print_to_console(buf);

    total_cycles = 0;
    for (uint32_t i = 0; i < iterations; i++) {
        start = dave2d_dwt_get_cycles();

        dave2d_port_fill_rect(&fb, 0, 0,
                              (int32_t)BENCH_FULLSCREEN_W,
                              (int32_t)BENCH_FULLSCREEN_H,
                              dave2d_port_rgb888_to_d2color(0, 0, (uint8_t)(i * 25)), 255);

        end = dave2d_dwt_get_cycles();
        total_cycles += (end - start);
    }

    uint32_t avg_cycles = total_cycles / iterations;
    uint32_t avg_us = dave2d_cycles_to_us(avg_cycles);
    uint32_t avg_ms_x10 = avg_us / 100;  /* ms * 10 for one decimal place */

    snprintf(buf, sizeof(buf),
             "    Dave2D: %lu us (%lu.%lu ms, %lu cycles, %lu iterations)\r\n",
             (unsigned long)avg_us,
             (unsigned long)(avg_ms_x10 / 10),
             (unsigned long)(avg_ms_x10 % 10),
             (unsigned long)avg_cycles,
             (unsigned long)iterations);
    print_to_console(buf);

    /* Check against 60fps frame budget */
    if (avg_us <= 16700) {
        print_to_console("    Result: PASS (within 16.7ms 60fps budget)\r\n");
    } else {
        print_to_console("    Result: FAIL (exceeds 16.7ms 60fps budget)\r\n");
    }
}

/**
 * Benchmark: Alpha blending fill (Dave2D accelerated)
 *
 * @details Measures the time to fill a 200x100 rectangle with 50% alpha
 *          blending using dave2d_port_fill_rect(). Alpha blending requires
 *          the GPU to read-modify-write the destination, which is more
 *          expensive than an opaque fill.
 */
static void dave2d_bench_alpha_blend(void)
{
    char buf[DAVE2D_PRINT_BUF_SIZE];
    dave2d_fb_t fb;
    uint32_t start, end, total_cycles;

    if (!dave2d_port_fb_from_glcdc(&fb, 0)) {
        print_to_console("  Error: Cannot get framebuffer.\r\n");
        return;
    }

    print_to_console("  [Alpha blend fill 200x100, alpha=128]\r\n");

    total_cycles = 0;
    for (uint32_t i = 0; i < DAVE2D_BENCH_ITERATIONS; i++) {
        start = dave2d_dwt_get_cycles();

        dave2d_port_fill_rect(&fb, 50, 50, 200, 100,
                              dave2d_port_rgb888_to_d2color(0, 0, 255), 128);

        end = dave2d_dwt_get_cycles();
        total_cycles += (end - start);
    }

    uint32_t avg_cycles = total_cycles / DAVE2D_BENCH_ITERATIONS;
    uint32_t avg_us = dave2d_cycles_to_us(avg_cycles);

    snprintf(buf, sizeof(buf),
             "    Dave2D: %lu us/op (%lu cycles, %lu iterations)\r\n",
             (unsigned long)avg_us,
             (unsigned long)avg_cycles,
             (unsigned long)DAVE2D_BENCH_ITERATIONS);
    print_to_console(buf);
}

/**
 * Benchmark: CPU software fill (comparison baseline)
 *
 * @details Measures the time to fill a 200x100 area using CPU memset/loop
 *          for comparison with Dave2D accelerated fill. This provides a
 *          baseline to quantify the GPU acceleration benefit.
 *
 *          Uses direct memory writes (volatile pointer) to the framebuffer.
 *          No Dave2D hardware is involved.
 */
static void dave2d_bench_cpu_fill_rect(void)
{
    char buf[DAVE2D_PRINT_BUF_SIZE];
    dave2d_fb_t fb;
    uint32_t start, end, total_cycles;

    if (!dave2d_port_fb_from_glcdc(&fb, 0)) {
        print_to_console("  Error: Cannot get framebuffer.\r\n");
        return;
    }

    print_to_console("  [CPU Fill Rect 200x100 (SW baseline)]\r\n");

    uint16_t color565 = 0xF800U;  /* Red in RGB565 */
    int32_t x0 = 50, y0 = 50, w = 200, h = 100;

    total_cycles = 0;
    for (uint32_t iter = 0; iter < DAVE2D_BENCH_ITERATIONS; iter++) {
        start = dave2d_dwt_get_cycles();

        /*
         * CPU software fill: write each pixel individually.
         * This uses the stride from the framebuffer descriptor.
         */
        for (int32_t y = y0; y < y0 + h; y++) {
            uint16_t *p_line = (uint16_t *)((uint8_t *)fb.p_base +
                               (uint32_t)y * (uint32_t)fb.pitch * sizeof(uint16_t));
            for (int32_t x = x0; x < x0 + w; x++) {
                p_line[x] = color565;
            }
        }

        end = dave2d_dwt_get_cycles();
        total_cycles += (end - start);
    }

    uint32_t avg_cycles = total_cycles / DAVE2D_BENCH_ITERATIONS;
    uint32_t avg_us = dave2d_cycles_to_us(avg_cycles);

    snprintf(buf, sizeof(buf),
             "    CPU SW: %lu us/op (%lu cycles, %lu iterations)\r\n",
             (unsigned long)avg_us,
             (unsigned long)avg_cycles,
             (unsigned long)DAVE2D_BENCH_ITERATIONS);
    print_to_console(buf);
}

/**
 * Benchmark: Full-screen CPU software fill (comparison baseline)
 *
 * @details Measures the time to fill the entire 1024x600 framebuffer
 *          using CPU software writes for comparison with Dave2D.
 */
static void dave2d_bench_cpu_fill_fullscreen(void)
{
    char buf[DAVE2D_PRINT_BUF_SIZE];
    dave2d_fb_t fb;
    uint32_t start, end, total_cycles;
    uint32_t iterations = 10;

    if (!dave2d_port_fb_from_glcdc(&fb, 0)) {
        print_to_console("  Error: Cannot get framebuffer.\r\n");
        return;
    }

    snprintf(buf, sizeof(buf), "  [CPU Full-screen fill %ux%u (SW baseline)]\r\n",
             (unsigned int)BENCH_FULLSCREEN_W,
             (unsigned int)BENCH_FULLSCREEN_H);
    print_to_console(buf);

    total_cycles = 0;
    for (uint32_t iter = 0; iter < iterations; iter++) {
        uint16_t color565 = (uint16_t)(0x001FU + iter * 0x0200U);

        start = dave2d_dwt_get_cycles();

        for (uint32_t y = 0; y < BENCH_FULLSCREEN_H; y++) {
            uint16_t *p_line = (uint16_t *)((uint8_t *)fb.p_base +
                               y * (uint32_t)fb.pitch * sizeof(uint16_t));
            for (uint32_t x = 0; x < BENCH_FULLSCREEN_W; x++) {
                p_line[x] = color565;
            }
        }

        end = dave2d_dwt_get_cycles();
        total_cycles += (end - start);
    }

    uint32_t avg_cycles = total_cycles / iterations;
    uint32_t avg_us = dave2d_cycles_to_us(avg_cycles);
    uint32_t avg_ms_x10 = avg_us / 100;

    snprintf(buf, sizeof(buf),
             "    CPU SW: %lu us (%lu.%lu ms, %lu cycles, %lu iterations)\r\n",
             (unsigned long)avg_us,
             (unsigned long)(avg_ms_x10 / 10),
             (unsigned long)(avg_ms_x10 % 10),
             (unsigned long)avg_cycles,
             (unsigned long)iterations);
    print_to_console(buf);

    if (avg_us <= 16700) {
        print_to_console("    Result: PASS (within 16.7ms 60fps budget)\r\n");
    } else {
        print_to_console("    Result: FAIL (exceeds 16.7ms 60fps budget)\r\n");
    }
}

/**
 * Print benchmark summary with Dave2D vs CPU comparison
 *
 * @details Runs both Dave2D and CPU fill benchmarks side by side and
 *          calculates the speedup ratio. Also reports LVGL performance
 *          monitor data (FPS and CPU usage) if available.
 */
static void dave2d_bench_summary(void)
{
    char buf[DAVE2D_PRINT_BUF_SIZE];
    dave2d_fb_t fb;
    uint32_t start, end;
    uint32_t dave2d_cycles, cpu_cycles;

    if (!dave2d_port_fb_from_glcdc(&fb, 0)) {
        print_to_console("  Error: Cannot get framebuffer.\r\n");
        return;
    }

    print_to_console("[Performance Comparison: Dave2D vs CPU]\r\n");

    /* --- Fill rect 200x100 comparison --- */
    print_to_console("  [Fill Rect 200x100]\r\n");

    /* Dave2D fill */
    dave2d_cycles = 0;
    for (uint32_t i = 0; i < DAVE2D_BENCH_ITERATIONS; i++) {
        start = dave2d_dwt_get_cycles();
        dave2d_port_fill_rect(&fb, 50, 50, 200, 100,
                              dave2d_port_rgb888_to_d2color(255, 0, 0), 255);
        end = dave2d_dwt_get_cycles();
        dave2d_cycles += (end - start);
    }
    dave2d_cycles /= DAVE2D_BENCH_ITERATIONS;

    /* CPU fill */
    cpu_cycles = 0;
    {
        uint16_t color565 = 0xF800U;
        for (uint32_t iter = 0; iter < DAVE2D_BENCH_ITERATIONS; iter++) {
            start = dave2d_dwt_get_cycles();
            for (int32_t y = 50; y < 150; y++) {
                uint16_t *p_line = (uint16_t *)((uint8_t *)fb.p_base +
                                   (uint32_t)y * (uint32_t)fb.pitch * sizeof(uint16_t));
                for (int32_t x = 50; x < 250; x++) {
                    p_line[x] = color565;
                }
            }
            end = dave2d_dwt_get_cycles();
            cpu_cycles += (end - start);
        }
        cpu_cycles /= DAVE2D_BENCH_ITERATIONS;
    }

    {
        uint32_t dave2d_us = dave2d_cycles_to_us(dave2d_cycles);
        uint32_t cpu_us = dave2d_cycles_to_us(cpu_cycles);
        uint32_t speedup_x10 = (cpu_us > 0) ? (cpu_us * 10 / dave2d_us) : 0;

        snprintf(buf, sizeof(buf),
                 "    Dave2D : %lu us  |  CPU : %lu us  |  Speedup : %lu.%lux\r\n",
                 (unsigned long)dave2d_us,
                 (unsigned long)cpu_us,
                 (unsigned long)(speedup_x10 / 10),
                 (unsigned long)(speedup_x10 % 10));
        print_to_console(buf);
    }

    /* --- Full-screen fill comparison --- */
    snprintf(buf, sizeof(buf), "  [Full-screen fill %ux%u]\r\n",
             (unsigned int)BENCH_FULLSCREEN_W,
             (unsigned int)BENCH_FULLSCREEN_H);
    print_to_console(buf);

    /* Dave2D full-screen */
    dave2d_cycles = 0;
    {
        uint32_t iterations = 10;
        for (uint32_t i = 0; i < iterations; i++) {
            start = dave2d_dwt_get_cycles();
            dave2d_port_fill_rect(&fb, 0, 0,
                                  (int32_t)BENCH_FULLSCREEN_W,
                                  (int32_t)BENCH_FULLSCREEN_H,
                                  dave2d_port_rgb888_to_d2color(0, 0, 128), 255);
            end = dave2d_dwt_get_cycles();
            dave2d_cycles += (end - start);
        }
        dave2d_cycles /= iterations;
    }

    /* CPU full-screen */
    cpu_cycles = 0;
    {
        uint32_t iterations = 10;
        uint16_t color565 = 0x0010U;
        for (uint32_t iter = 0; iter < iterations; iter++) {
            start = dave2d_dwt_get_cycles();
            for (uint32_t y = 0; y < BENCH_FULLSCREEN_H; y++) {
                uint16_t *p_line = (uint16_t *)((uint8_t *)fb.p_base +
                                   y * (uint32_t)fb.pitch * sizeof(uint16_t));
                for (uint32_t x = 0; x < BENCH_FULLSCREEN_W; x++) {
                    p_line[x] = color565;
                }
            }
            end = dave2d_dwt_get_cycles();
            cpu_cycles += (end - start);
        }
        cpu_cycles /= iterations;
    }

    {
        uint32_t dave2d_us = dave2d_cycles_to_us(dave2d_cycles);
        uint32_t cpu_us = dave2d_cycles_to_us(cpu_cycles);
        uint32_t dave2d_ms_x10 = dave2d_us / 100;
        uint32_t cpu_ms_x10 = cpu_us / 100;
        uint32_t speedup_x10 = (dave2d_us > 0) ? (cpu_us * 10 / dave2d_us) : 0;

        snprintf(buf, sizeof(buf),
                 "    Dave2D : %lu.%lu ms  |  CPU : %lu.%lu ms  |  Speedup : %lu.%lux\r\n",
                 (unsigned long)(dave2d_ms_x10 / 10),
                 (unsigned long)(dave2d_ms_x10 % 10),
                 (unsigned long)(cpu_ms_x10 / 10),
                 (unsigned long)(cpu_ms_x10 % 10),
                 (unsigned long)(speedup_x10 / 10),
                 (unsigned long)(speedup_x10 % 10));
        print_to_console(buf);

        /* 60fps budget check */
        if (dave2d_us <= 16700) {
            print_to_console("    Dave2D: PASS (within 16.7ms 60fps budget)\r\n");
        } else {
            print_to_console("    Dave2D: FAIL (exceeds 16.7ms 60fps budget)\r\n");
        }
    }

    /* --- LVGL Performance Monitor Data --- */
    print_to_console("[LVGL Performance Monitor]\r\n");
    print_to_console("  Check the on-screen LVGL perf monitor for live FPS/CPU data.\r\n");
    print_to_console("  LV_USE_PERF_MONITOR is enabled in lv_conf_user.h.\r\n");
    print_to_console("  Location: bottom-right corner of the display.\r\n");
}

#endif /* LV_USE_DRAW_DAVE2D */

/**
 * "dave2d bench" sub-command dispatcher (S-004-4)
 *
 * @details Dispatches benchmark sub-commands for Dave2D performance measurement.
 *          Measures individual drawing operations and compares Dave2D GPU
 *          performance against CPU software rendering.
 *
 *          Each benchmark uses the DWT cycle counter for precise timing at
 *          the CPU clock frequency (1 GHz).
 *
 *          Note: These benchmarks write directly to the GLCDC framebuffer,
 *          bypassing LVGL. The display content will be overwritten.
 *
 * @param argc  Argument count (from the original command)
 * @param argv  Argument vector
 */
static void dave2d_cmd_bench(int argc, char **argv)
{
#if LV_USE_DRAW_DAVE2D
    if (!dave2d_port_is_available()) {
        print_to_console("  Error: Dave2D is not available.\r\n");
        return;
    }

    /* Initialize the DWT cycle counter */
    dave2d_dwt_init();

    if (argc < 3) {
        /* No sub-sub-command: run all benchmarks */
        print_to_console("[Dave2D Performance Benchmark (S-004-4)]\r\n");
        print_to_console("  CPU Clock  : 1000 MHz (Cortex-M85)\r\n");
        print_to_console("  Display    : 1024x600 RGB565\r\n");
        print_to_console("  Timer      : DWT CYCCNT (1ns resolution)\r\n");

        char buf[DAVE2D_PRINT_BUF_SIZE];
        snprintf(buf, sizeof(buf), "  Iterations : %u (per test, except full-screen: 10)\r\n",
                 (unsigned int)DAVE2D_BENCH_ITERATIONS);
        print_to_console(buf);

        print_to_console("-------------------------------------------\r\n");

        dave2d_bench_fill_rect();
        dave2d_bench_draw_line();
        dave2d_bench_blit();
        dave2d_bench_alpha_blend();
        dave2d_bench_blit_fullscreen();

        print_to_console("-------------------------------------------\r\n");

        dave2d_bench_cpu_fill_rect();
        dave2d_bench_cpu_fill_fullscreen();

        print_to_console("-------------------------------------------\r\n");

        dave2d_bench_summary();

        print_to_console("-------------------------------------------\r\n");
        print_to_console("Benchmark complete.\r\n");
        return;
    }

    const char *sub = argv[2];

    if (ntlibc_strcmp(sub, "fill") == 0) {
        dave2d_dwt_init();
        dave2d_bench_fill_rect();
    } else if (ntlibc_strcmp(sub, "line") == 0) {
        dave2d_dwt_init();
        dave2d_bench_draw_line();
    } else if (ntlibc_strcmp(sub, "blit") == 0) {
        dave2d_dwt_init();
        dave2d_bench_blit();
    } else if (ntlibc_strcmp(sub, "alpha") == 0) {
        dave2d_dwt_init();
        dave2d_bench_alpha_blend();
    } else if (ntlibc_strcmp(sub, "fullscreen") == 0) {
        dave2d_dwt_init();
        dave2d_bench_blit_fullscreen();
    } else if (ntlibc_strcmp(sub, "cpu") == 0) {
        dave2d_dwt_init();
        dave2d_bench_cpu_fill_rect();
        dave2d_bench_cpu_fill_fullscreen();
    } else if (ntlibc_strcmp(sub, "compare") == 0) {
        dave2d_dwt_init();
        dave2d_bench_summary();
    } else {
        char buf[DAVE2D_PRINT_BUF_SIZE];
        snprintf(buf, sizeof(buf), "  Error: Unknown benchmark '%s'.\r\n", sub);
        print_to_console(buf);
        print_to_console("  Available: fill, line, blit, alpha, fullscreen, cpu, compare\r\n");
        print_to_console("  (no argument runs all benchmarks)\r\n");
    }
#else
    (void)argc;
    (void)argv;
    print_to_console("  Error: LV_USE_DRAW_DAVE2D is disabled.\r\n");
#endif
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/* ============================================================
 *  Initialization / Status (S-004-1)
 * ============================================================ */

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

/* ============================================================
 *  LVGL-Dave2D Integration (S-004-3)
 * ============================================================ */

/**
 * Get LVGL-Dave2D integration information
 *
 * @details Queries the LVGL internal Dave2D draw unit state to provide
 *          comprehensive integration information. The GPU-accelerated
 *          and CPU-fallback operation flags are determined by examining
 *          the _dave2d_evaluate() function logic in the LVGL source.
 *
 *          Since the evaluate function's behavior is coded at compile time
 *          (controlled by USE_D2 macro which is always 1), the accelerated
 *          operations are known statically. We verify them here for runtime
 *          diagnostics.
 *
 * Reference:
 *   - _dave2d_evaluate: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:238-367
 *   - lv_draw_dave2d_init: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:76-108
 */
void dave2d_port_get_lvgl_info(dave2d_lvgl_info_t *info)
{
    if (info == NULL) {
        return;
    }

    memset(info, 0, sizeof(*info));

#if LV_USE_DRAW_DAVE2D
    /*
     * Draw unit registration status.
     *
     * The Dave2D draw unit is created with ID=4 in lv_draw_dave2d_init():
     *   draw_dave2d_unit->idx = DRAW_UNIT_ID_DAVE2D;  // 4
     *   draw_dave2d_unit->base_unit.name = "DAVE2D";
     *
     * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:82-86
     */
    if (_d2_handle != NULL) {
        info->draw_unit_registered = true;
        info->draw_unit_id         = 4;   /* DRAW_UNIT_ID_DAVE2D */
        info->draw_unit_name       = "DAVE2D";
    } else {
        info->draw_unit_registered = false;
        info->draw_unit_id         = 0;
        info->draw_unit_name       = "N/A";
    }

    /*
     * GPU-accelerated operations.
     *
     * These are determined by the _dave2d_evaluate() function which sets
     * t->preferred_draw_unit_id = DRAW_UNIT_ID_DAVE2D for supported task types.
     * The USE_D2 macro is always 1 in the current LVGL Dave2D driver.
     *
     * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:248-365
     */
    info->accel_fill     = true;  /* LV_DRAW_TASK_TYPE_FILL (solid, no gradient) :249-268 */
    info->accel_border   = true;  /* LV_DRAW_TASK_TYPE_BORDER :288-295 */
    info->accel_line     = true;  /* LV_DRAW_TASK_TYPE_LINE :311-318 */
    info->accel_arc      = true;  /* LV_DRAW_TASK_TYPE_ARC :320-327 */
    info->accel_image    = true;  /* LV_DRAW_TASK_TYPE_IMAGE (supported CFs) :274-286 */
    info->accel_label    = true;  /* LV_DRAW_TASK_TYPE_LABEL :302-309 */
    info->accel_triangle = true;  /* LV_DRAW_TASK_TYPE_TRIANGLE (solid, no gradient) :329-345 */

    /*
     * CPU fallback operations.
     *
     * These task types are NOT claimed by _dave2d_evaluate(), so they fall
     * through to the software renderer (LV_USE_DRAW_SW=1).
     *
     * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:269-364
     */
    info->fallback_layer       = true;  /* LV_DRAW_TASK_TYPE_LAYER :269-272 */
    info->fallback_box_shadow  = true;  /* LV_DRAW_TASK_TYPE_BOX_SHADOW :297-300 */
    info->fallback_mask_rect   = true;  /* LV_DRAW_TASK_TYPE_MASK_RECTANGLE :347-354 (disabled) */
    info->fallback_mask_bitmap = true;  /* LV_DRAW_TASK_TYPE_MASK_BITMAP :356-359 */
    info->fallback_gradient    = true;  /* Gradient fills/triangles :252-256, 332-336 */

    /*
     * Render buffer status.
     *
     * LVGL creates two render buffers in lv_dave2d_init():
     *   _blit_renderbuffer: For non-LVGL operations (dave2d_port wrappers)
     *   _renderbuffer: For LVGL draw operations
     *
     * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:580-594
     */
    info->renderbuffer_valid      = (_renderbuffer != NULL);
    info->blit_renderbuffer_valid = (_blit_renderbuffer != NULL);

    /*
     * Thread and mutex status.
     *
     * The Dave2D render thread and mutex are created in lv_draw_dave2d_init():
     *   lv_mutex_init(&xd2Semaphore);
     *   lv_thread_init(&draw_dave2d_unit->thread, ...);
     *
     * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:91-106
     */
    info->render_thread_active = (_d2_handle != NULL);  /* Thread created if init succeeded */
    info->mutex_initialized    = (_d2_handle != NULL);  /* Mutex created if init succeeded */

#else
    /* LV_USE_DRAW_DAVE2D is disabled */
    info->draw_unit_registered = false;
    info->draw_unit_id         = 0;
    info->draw_unit_name       = "N/A (disabled)";
#endif
}

/**
 * Print LVGL-Dave2D integration summary to console
 *
 * @details Outputs a human-readable summary of the Dave2D-LVGL draw engine
 *          integration. This covers:
 *          - Draw unit registration (ID, name)
 *          - GPU-accelerated operations and their Dave2D primitives
 *          - CPU fallback operations
 *          - Render buffer and thread status
 *
 *          Called from lvgl_thread_entry.c after initialization and from
 *          the NT-Shell "dave2d integration" sub-command.
 *
 * Reference:
 *   - _dave2d_evaluate: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:238-367
 */
void dave2d_port_print_lvgl_integration(void)
{
    char buf[DAVE2D_PRINT_BUF_SIZE];
    dave2d_lvgl_info_t info;

    dave2d_port_get_lvgl_info(&info);

    print_to_console("[LVGL-Dave2D Integration Status (S-004-3)]\r\n");

    /* Draw unit registration */
    print_to_console("[Draw Unit Registration]\r\n");
    snprintf(buf, sizeof(buf), "  Registered  : %s\r\n",
             info.draw_unit_registered ? "Yes" : "No");
    print_to_console(buf);

    if (info.draw_unit_registered) {
        snprintf(buf, sizeof(buf), "  Unit ID     : %lu (DRAW_UNIT_ID_DAVE2D)\r\n",
                 (unsigned long)info.draw_unit_id);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  Unit Name   : %s\r\n", info.draw_unit_name);
        print_to_console(buf);
    }

    /* GPU-accelerated operations */
    print_to_console("[GPU-Accelerated Operations (Dave2D)]\r\n");
    snprintf(buf, sizeof(buf), "  Fill (solid)  : %s  -> d2_renderbox\r\n",
             info.accel_fill ? "GPU" : "---");
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Border        : %s  -> d2_renderbox\r\n",
             info.accel_border ? "GPU" : "---");
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Line          : %s  -> d2_renderline\r\n",
             info.accel_line ? "GPU" : "---");
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Arc           : %s  -> d2_renderwedge\r\n",
             info.accel_arc ? "GPU" : "---");
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Image/BLIT    : %s  -> d2_setblitsrc + d2_blitcopy\r\n",
             info.accel_image ? "GPU" : "---");
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Label/Text    : %s  -> d2_setblitsrc (glyph blit)\r\n",
             info.accel_label ? "GPU" : "---");
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Triangle      : %s  -> d2_rendertri\r\n",
             info.accel_triangle ? "GPU" : "---");
    print_to_console(buf);

    /* CPU fallback operations */
    print_to_console("[CPU Fallback Operations (SW Renderer)]\r\n");
    snprintf(buf, sizeof(buf), "  Layer         : %s\r\n",
             info.fallback_layer ? "CPU" : "---");
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Box Shadow    : %s\r\n",
             info.fallback_box_shadow ? "CPU" : "---");
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Mask Rect     : %s  (disabled in Dave2D driver)\r\n",
             info.fallback_mask_rect ? "CPU" : "---");
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Mask Bitmap   : %s\r\n",
             info.fallback_mask_bitmap ? "CPU" : "---");
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Gradient Fill : %s  (gradient stops differ)\r\n",
             info.fallback_gradient ? "CPU" : "---");
    print_to_console(buf);

    /* Render infrastructure */
    print_to_console("[Render Infrastructure]\r\n");
    snprintf(buf, sizeof(buf), "  Main RenderBuf: %s\r\n",
             info.renderbuffer_valid ? "Valid" : "NULL");
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Blit RenderBuf: %s\r\n",
             info.blit_renderbuffer_valid ? "Valid" : "NULL");
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Render Thread : %s\r\n",
             info.render_thread_active ? "Active" : "Not active");
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Mutex         : %s\r\n",
             info.mutex_initialized ? "Initialized" : "Not initialized");
    print_to_console(buf);

    /* Configuration summary */
    print_to_console("[Configuration (lv_conf_user.h)]\r\n");
    print_to_console("  LV_USE_DRAW_DAVE2D             = ");
#if LV_USE_DRAW_DAVE2D
    print_to_console("1 (GPU enabled)\r\n");
#else
    print_to_console("0 (GPU disabled)\r\n");
#endif
    print_to_console("  LV_USE_DRAW_SW                 = ");
#if LV_USE_DRAW_SW
    print_to_console("1 (SW fallback enabled)\r\n");
#else
    print_to_console("0 (SW fallback disabled)\r\n");
#endif

    snprintf(buf, sizeof(buf), "  LV_DRAW_LAYER_SIMPLE_BUF_SIZE  = %lu bytes\r\n",
             (unsigned long)LV_DRAW_LAYER_SIMPLE_BUF_SIZE);
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  LV_DRAW_LAYER_MAX_MEMORY       = %lu bytes\r\n",
             (unsigned long)LV_DRAW_LAYER_MAX_MEMORY);
    print_to_console(buf);

    /* Image color format support */
    print_to_console("[Dave2D Supported Image Formats]\r\n");
    print_to_console("  A8       : Yes\r\n");
    print_to_console("  RGB565   : Yes\r\n");
    print_to_console("  ARGB1555 : Yes\r\n");
    print_to_console("  ARGB4444 : Yes\r\n");
    print_to_console("  ARGB8888 : Yes\r\n");
    print_to_console("  XRGB8888 : Yes\r\n");
    print_to_console("  Others   : No (fallback to SW renderer)\r\n");
}

/* ============================================================
 *  Drawing Wrapper Functions (S-004-2)
 * ============================================================ */

/**
 * Fill a rectangle with a solid color using Dave2D hardware acceleration
 *
 * @details Uses the LVGL-managed _blit_renderbuffer to avoid interfering
 *          with LVGL's main rendering pipeline. The pattern follows the
 *          reference _dave2d_buf_copy() implementation.
 *
 * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d_fill.c:126-139
 * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:156-213
 */
int dave2d_port_fill_rect(const dave2d_fb_t *fb,
                          int32_t x, int32_t y,
                          int32_t w, int32_t h,
                          uint32_t color, uint8_t alpha)
{
#if LV_USE_DRAW_DAVE2D
    if (!dave2d_port_is_available()) {
        return DAVE2D_ERR_NOT_INIT;
    }

    if (fb == NULL || fb->p_base == NULL || w <= 0 || h <= 0) {
        return DAVE2D_ERR_PARAM;
    }

    d2_s32 result;
    lv_result_t status;

    /* Acquire the LVGL Dave2D mutex for thread safety */
    status = lv_mutex_lock(&xd2Semaphore);
    if (LV_RESULT_OK != status) {
        return DAVE2D_ERR_HW;
    }

    /* Select the blit render buffer (separate from LVGL's main buffer) */
    result = d2_selectrenderbuffer(_d2_handle, _blit_renderbuffer);
    if (D2_OK != result) {
        lv_mutex_unlock(&xd2Semaphore);
        return DAVE2D_ERR_HW;
    }

    /* Set up the target framebuffer */
    result = d2_framebuffer(_d2_handle, fb->p_base,
                            (d2_s32)fb->pitch,
                            (d2_u32)fb->width,
                            (d2_u32)fb->height,
                            (d2_s32)fb->format);
    if (D2_OK != result) {
        d2_selectrenderbuffer(_d2_handle, _renderbuffer);
        lv_mutex_unlock(&xd2Semaphore);
        return DAVE2D_ERR_HW;
    }

    /* Set clip rectangle to the full framebuffer */
    d2_cliprect(_d2_handle, 0, 0,
                (d2_border)(fb->width - 1),
                (d2_border)(fb->height - 1));

    /* Set rendering parameters */
    d2_setfillmode(_d2_handle, d2_fm_color);
    d2_setcolor(_d2_handle, 0, (d2_color)color);
    d2_setalpha(_d2_handle, alpha);
    d2_setblendmode(_d2_handle, d2_bm_alpha, d2_bm_one_minus_alpha);

    /* Render the filled rectangle */
    result = d2_renderbox(_d2_handle,
                          (d2_point)D2_FIX4(x),
                          (d2_point)D2_FIX4(y),
                          (d2_width)D2_FIX4(w),
                          (d2_width)D2_FIX4(h));

    /* Execute and flush */
    d2_executerenderbuffer(_d2_handle, _blit_renderbuffer, 0);
    d2_flushframe(_d2_handle);

    /* Restore the main render buffer */
    d2_selectrenderbuffer(_d2_handle, _renderbuffer);

    /* Release the mutex */
    lv_mutex_unlock(&xd2Semaphore);

    return (D2_OK == result) ? DAVE2D_OK : DAVE2D_ERR_HW;
#else
    (void)fb; (void)x; (void)y; (void)w; (void)h;
    (void)color; (void)alpha;
    return DAVE2D_ERR_NOT_INIT;
#endif
}

/**
 * Draw a rectangle outline using Dave2D hardware acceleration
 *
 * @details Draws four thin filled rectangles to form a border.
 *          All four edges are batched into one render buffer execution
 *          for efficiency.
 */
int dave2d_port_draw_rect(const dave2d_fb_t *fb,
                          int32_t x, int32_t y,
                          int32_t w, int32_t h,
                          uint32_t color, uint8_t alpha,
                          int32_t border_w)
{
#if LV_USE_DRAW_DAVE2D
    if (!dave2d_port_is_available()) {
        return DAVE2D_ERR_NOT_INIT;
    }

    if (fb == NULL || fb->p_base == NULL || w <= 0 || h <= 0 || border_w <= 0) {
        return DAVE2D_ERR_PARAM;
    }

    /* Clamp border width to half the shorter dimension */
    int32_t max_border = (w < h ? w : h) / 2;
    if (border_w > max_border) {
        border_w = max_border;
    }

    d2_s32 result;
    lv_result_t status;

    status = lv_mutex_lock(&xd2Semaphore);
    if (LV_RESULT_OK != status) {
        return DAVE2D_ERR_HW;
    }

    result = d2_selectrenderbuffer(_d2_handle, _blit_renderbuffer);
    if (D2_OK != result) {
        lv_mutex_unlock(&xd2Semaphore);
        return DAVE2D_ERR_HW;
    }

    d2_framebuffer(_d2_handle, fb->p_base,
                   (d2_s32)fb->pitch,
                   (d2_u32)fb->width,
                   (d2_u32)fb->height,
                   (d2_s32)fb->format);

    d2_cliprect(_d2_handle, 0, 0,
                (d2_border)(fb->width - 1),
                (d2_border)(fb->height - 1));

    d2_setfillmode(_d2_handle, d2_fm_color);
    d2_setcolor(_d2_handle, 0, (d2_color)color);
    d2_setalpha(_d2_handle, alpha);
    d2_setblendmode(_d2_handle, d2_bm_alpha, d2_bm_one_minus_alpha);

    /*
     * Draw four edges of the rectangle:
     *   Top:    (x, y) -> width w, height border_w
     *   Bottom: (x, y+h-border_w) -> width w, height border_w
     *   Left:   (x, y+border_w) -> width border_w, height (h - 2*border_w)
     *   Right:  (x+w-border_w, y+border_w) -> width border_w, height (h - 2*border_w)
     */

    /* Top edge */
    d2_renderbox(_d2_handle,
                 (d2_point)D2_FIX4(x),
                 (d2_point)D2_FIX4(y),
                 (d2_width)D2_FIX4(w),
                 (d2_width)D2_FIX4(border_w));

    /* Bottom edge */
    d2_renderbox(_d2_handle,
                 (d2_point)D2_FIX4(x),
                 (d2_point)D2_FIX4(y + h - border_w),
                 (d2_width)D2_FIX4(w),
                 (d2_width)D2_FIX4(border_w));

    /* Left edge (excluding corners already drawn) */
    int32_t inner_h = h - 2 * border_w;
    if (inner_h > 0) {
        d2_renderbox(_d2_handle,
                     (d2_point)D2_FIX4(x),
                     (d2_point)D2_FIX4(y + border_w),
                     (d2_width)D2_FIX4(border_w),
                     (d2_width)D2_FIX4(inner_h));

        /* Right edge */
        d2_renderbox(_d2_handle,
                     (d2_point)D2_FIX4(x + w - border_w),
                     (d2_point)D2_FIX4(y + border_w),
                     (d2_width)D2_FIX4(border_w),
                     (d2_width)D2_FIX4(inner_h));
    }

    /* Execute all four edges at once */
    result = d2_executerenderbuffer(_d2_handle, _blit_renderbuffer, 0);
    d2_flushframe(_d2_handle);

    d2_selectrenderbuffer(_d2_handle, _renderbuffer);
    lv_mutex_unlock(&xd2Semaphore);

    return (D2_OK == result) ? DAVE2D_OK : DAVE2D_ERR_HW;
#else
    (void)fb; (void)x; (void)y; (void)w; (void)h;
    (void)color; (void)alpha; (void)border_w;
    return DAVE2D_ERR_NOT_INIT;
#endif
}

/**
 * Draw a line using Dave2D hardware acceleration
 *
 * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d_line.c:77-78
 */
int dave2d_port_draw_line(const dave2d_fb_t *fb,
                          int32_t x1, int32_t y1,
                          int32_t x2, int32_t y2,
                          uint32_t color, uint8_t alpha,
                          int32_t width)
{
#if LV_USE_DRAW_DAVE2D
    if (!dave2d_port_is_available()) {
        return DAVE2D_ERR_NOT_INIT;
    }

    if (fb == NULL || fb->p_base == NULL || width <= 0) {
        return DAVE2D_ERR_PARAM;
    }

    d2_s32 result;
    lv_result_t status;

    status = lv_mutex_lock(&xd2Semaphore);
    if (LV_RESULT_OK != status) {
        return DAVE2D_ERR_HW;
    }

    result = d2_selectrenderbuffer(_d2_handle, _blit_renderbuffer);
    if (D2_OK != result) {
        lv_mutex_unlock(&xd2Semaphore);
        return DAVE2D_ERR_HW;
    }

    d2_framebuffer(_d2_handle, fb->p_base,
                   (d2_s32)fb->pitch,
                   (d2_u32)fb->width,
                   (d2_u32)fb->height,
                   (d2_s32)fb->format);

    d2_cliprect(_d2_handle, 0, 0,
                (d2_border)(fb->width - 1),
                (d2_border)(fb->height - 1));

    d2_setcolor(_d2_handle, 0, (d2_color)color);
    d2_setalpha(_d2_handle, alpha);
    d2_setblendmode(_d2_handle, d2_bm_alpha, d2_bm_one_minus_alpha);
    d2_setlinecap(_d2_handle, d2_lc_butt);

    result = d2_renderline(_d2_handle,
                           (d2_point)D2_FIX4(x1),
                           (d2_point)D2_FIX4(y1),
                           (d2_point)D2_FIX4(x2),
                           (d2_point)D2_FIX4(y2),
                           (d2_width)D2_FIX4(width),
                           d2_le_exclude_none);

    d2_executerenderbuffer(_d2_handle, _blit_renderbuffer, 0);
    d2_flushframe(_d2_handle);

    d2_selectrenderbuffer(_d2_handle, _renderbuffer);
    lv_mutex_unlock(&xd2Semaphore);

    return (D2_OK == result) ? DAVE2D_OK : DAVE2D_ERR_HW;
#else
    (void)fb; (void)x1; (void)y1; (void)x2; (void)y2;
    (void)color; (void)alpha; (void)width;
    return DAVE2D_ERR_NOT_INIT;
#endif
}

/**
 * Copy a rectangular region from source to framebuffer (1:1 BLIT)
 *
 * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:186-193
 */
int dave2d_port_blit(const dave2d_fb_t *fb,
                     const void *p_src, int32_t src_pitch,
                     int32_t src_w, int32_t src_h, uint32_t src_format,
                     int32_t dst_x, int32_t dst_y)
{
    /* 1:1 copy: destination size equals source size */
    return dave2d_port_blit_scaled(fb, p_src, src_pitch,
                                  src_w, src_h, src_format,
                                  dst_x, dst_y,
                                  src_w, src_h);
}

/**
 * Copy and scale a rectangular region from source to framebuffer (scaled BLIT)
 *
 * @details Uses d2_setblitsrc() to configure the source texture and
 *          d2_blitcopy() to perform the hardware-accelerated copy with
 *          optional scaling. The blend mode is set to opaque (src=1, dst=0)
 *          so the source overwrites the destination completely.
 *
 * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:171-213
 */
int dave2d_port_blit_scaled(const dave2d_fb_t *fb,
                            const void *p_src, int32_t src_pitch,
                            int32_t src_w, int32_t src_h, uint32_t src_format,
                            int32_t dst_x, int32_t dst_y,
                            int32_t dst_w, int32_t dst_h)
{
#if LV_USE_DRAW_DAVE2D
    if (!dave2d_port_is_available()) {
        return DAVE2D_ERR_NOT_INIT;
    }

    if (fb == NULL || fb->p_base == NULL || p_src == NULL) {
        return DAVE2D_ERR_PARAM;
    }

    if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0 || src_pitch <= 0) {
        return DAVE2D_ERR_PARAM;
    }

    d2_s32 result;
    lv_result_t status;

    status = lv_mutex_lock(&xd2Semaphore);
    if (LV_RESULT_OK != status) {
        return DAVE2D_ERR_HW;
    }

    /* Save current blend mode to restore later */
    d2_u32 saved_src_blend = d2_getblendmodesrc(_d2_handle);
    d2_u32 saved_dst_blend = d2_getblendmodedst(_d2_handle);

    result = d2_selectrenderbuffer(_d2_handle, _blit_renderbuffer);
    if (D2_OK != result) {
        lv_mutex_unlock(&xd2Semaphore);
        return DAVE2D_ERR_HW;
    }

    /* Set destination framebuffer */
    d2_framebuffer(_d2_handle, fb->p_base,
                   (d2_s32)fb->pitch,
                   (d2_u32)fb->width,
                   (d2_u32)fb->height,
                   (d2_s32)fb->format);

    d2_cliprect(_d2_handle,
                (d2_border)dst_x,
                (d2_border)dst_y,
                (d2_border)(dst_x + dst_w - 1),
                (d2_border)(dst_y + dst_h - 1));

    /* Opaque blit: source overwrites destination completely */
    d2_setblendmode(_d2_handle, d2_bm_one, d2_bm_zero);

    /* Set the source bitmap for blitting */
    result = d2_setblitsrc(_d2_handle, (void *)p_src,
                           (d2_s32)src_pitch,
                           (d2_s32)src_w,
                           (d2_s32)src_h,
                           (d2_u32)src_format);
    if (D2_OK != result) {
        d2_setblendmode(_d2_handle, saved_src_blend, saved_dst_blend);
        d2_selectrenderbuffer(_d2_handle, _renderbuffer);
        lv_mutex_unlock(&xd2Semaphore);
        return DAVE2D_ERR_HW;
    }

    /*
     * Perform the blit copy.
     *
     * d2_blitcopy parameters:
     *   srcwidth, srcheight: Source dimensions (integer)
     *   srcx, srcy: Source offset (integer)
     *   dstwidth, dstheight: Destination dimensions (4:4 fixed-point)
     *   dstx, dsty: Destination position (4:4 fixed-point)
     *   flags: 0 for default
     *
     * Reference: e2studio_CPU0/ra/tes/dave2d/inc/dave_driver.h:622
     */
    result = d2_blitcopy(_d2_handle,
                         (d2_s32)src_w,
                         (d2_s32)src_h,
                         (d2_blitpos)0,       /* Source origin x */
                         (d2_blitpos)0,       /* Source origin y */
                         (d2_width)D2_FIX4(dst_w),
                         (d2_width)D2_FIX4(dst_h),
                         (d2_point)D2_FIX4(dst_x),
                         (d2_point)D2_FIX4(dst_y),
                         0);

    /* Execute and flush */
    d2_executerenderbuffer(_d2_handle, _blit_renderbuffer, 0);
    d2_flushframe(_d2_handle);

    /* Restore previous blend mode and render buffer */
    d2_setblendmode(_d2_handle, saved_src_blend, saved_dst_blend);
    d2_selectrenderbuffer(_d2_handle, _renderbuffer);

    lv_mutex_unlock(&xd2Semaphore);

    return (D2_OK == result) ? DAVE2D_OK : DAVE2D_ERR_HW;
#else
    (void)fb; (void)p_src; (void)src_pitch;
    (void)src_w; (void)src_h; (void)src_format;
    (void)dst_x; (void)dst_y; (void)dst_w; (void)dst_h;
    return DAVE2D_ERR_NOT_INIT;
#endif
}

/**
 * Set the alpha blending mode
 *
 * Reference: e2studio_CPU0/ra/tes/dave2d/inc/dave_driver.h:537
 */
int dave2d_port_set_blend_mode(uint32_t src_factor, uint32_t dst_factor)
{
#if LV_USE_DRAW_DAVE2D
    if (!dave2d_port_is_available()) {
        return DAVE2D_ERR_NOT_INIT;
    }

    lv_result_t status;
    d2_s32 result;

    status = lv_mutex_lock(&xd2Semaphore);
    if (LV_RESULT_OK != status) {
        return DAVE2D_ERR_HW;
    }

    result = d2_setblendmode(_d2_handle, src_factor, dst_factor);

    lv_mutex_unlock(&xd2Semaphore);

    return (D2_OK == result) ? DAVE2D_OK : DAVE2D_ERR_HW;
#else
    (void)src_factor; (void)dst_factor;
    return DAVE2D_ERR_NOT_INIT;
#endif
}

/* ============================================================
 *  Rendering Pipeline Control (S-004-2)
 * ============================================================ */

/**
 * Execute the render buffer and wait for completion
 *
 * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:619-623
 */
int dave2d_port_execute_and_flush(void)
{
#if LV_USE_DRAW_DAVE2D
    if (!dave2d_port_is_available()) {
        return DAVE2D_ERR_NOT_INIT;
    }

    lv_result_t status;
    d2_s32 result;

    status = lv_mutex_lock(&xd2Semaphore);
    if (LV_RESULT_OK != status) {
        return DAVE2D_ERR_HW;
    }

    result = d2_executerenderbuffer(_d2_handle, _blit_renderbuffer, 0);
    if (D2_OK == result) {
        result = d2_flushframe(_d2_handle);
    }

    d2_selectrenderbuffer(_d2_handle, _renderbuffer);

    lv_mutex_unlock(&xd2Semaphore);

    return (D2_OK == result) ? DAVE2D_OK : DAVE2D_ERR_HW;
#else
    return DAVE2D_ERR_NOT_INIT;
#endif
}

/* ============================================================
 *  Color Conversion Utilities (S-004-2)
 * ============================================================ */

/**
 * Convert RGB565 color to Dave2D color format (0x00RRGGBB)
 *
 * @details Unpacks the 16-bit RGB565 into 8-bit per channel.
 *   RGB565: RRRR RGGG GGGB BBBB
 *   R5 (bits 15:11) -> R8 by: (R5 << 3) | (R5 >> 2)
 *   G6 (bits 10:5)  -> G8 by: (G6 << 2) | (G6 >> 4)
 *   B5 (bits 4:0)   -> B8 by: (B5 << 3) | (B5 >> 2)
 */
uint32_t dave2d_port_rgb565_to_d2color(uint16_t rgb565)
{
    uint32_t r5 = (rgb565 >> 11) & 0x1FU;
    uint32_t g6 = (rgb565 >> 5)  & 0x3FU;
    uint32_t b5 = rgb565 & 0x1FU;

    /* Expand to 8 bits with LSB replication for accurate color */
    uint32_t r8 = (r5 << 3) | (r5 >> 2);
    uint32_t g8 = (g6 << 2) | (g6 >> 4);
    uint32_t b8 = (b5 << 3) | (b5 >> 2);

    return (r8 << 16) | (g8 << 8) | b8;
}

/**
 * Convert Dave2D color format (0x00RRGGBB) to RGB565
 */
uint16_t dave2d_port_d2color_to_rgb565(uint32_t d2color)
{
    uint32_t r8 = (d2color >> 16) & 0xFFU;
    uint32_t g8 = (d2color >> 8)  & 0xFFU;
    uint32_t b8 = d2color & 0xFFU;

    uint16_t r5 = (uint16_t)(r8 >> 3);
    uint16_t g6 = (uint16_t)(g8 >> 2);
    uint16_t b5 = (uint16_t)(b8 >> 3);

    return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

/**
 * Convert an RGB888 triplet to Dave2D color format
 */
uint32_t dave2d_port_rgb888_to_d2color(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/**
 * Convert a YUV (YCbCr BT.601) pixel to RGB565
 *
 * @details Uses integer arithmetic with fixed-point scaling to avoid
 *          floating-point operations. The BT.601 conversion coefficients
 *          are scaled by 256 for fixed-point computation:
 *
 *          R = Y + 359 * (V - 128) / 256
 *          G = Y -  88 * (U - 128) / 256 - 183 * (V - 128) / 256
 *          B = Y + 454 * (U - 128) / 256
 *
 *          Results are clamped to [0, 255] before conversion to RGB565.
 */
uint16_t dave2d_port_yuv_to_rgb565(uint8_t y_val, uint8_t u_val, uint8_t v_val)
{
    int32_t y = (int32_t)y_val;
    int32_t u = (int32_t)u_val - 128;
    int32_t v = (int32_t)v_val - 128;

    /* BT.601 conversion with fixed-point coefficients (x256) */
    int32_t r = y + ((359 * v) >> 8);
    int32_t g = y - ((88 * u + 183 * v) >> 8);
    int32_t b = y + ((454 * u) >> 8);

    /* Clamp to [0, 255] */
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;

    /* Pack into RGB565 */
    return (uint16_t)(((uint32_t)r >> 3) << 11 |
                      ((uint32_t)g >> 2) << 5 |
                      ((uint32_t)b >> 3));
}

/* ============================================================
 *  Framebuffer Helper (S-004-2)
 * ============================================================ */

/**
 * Initialize a dave2d_fb_t descriptor for the GLCDC framebuffer
 *
 * @details Creates a framebuffer descriptor targeting one of the two
 *          GLCDC framebuffers (fb_background[0] or fb_background[1]).
 *
 * Reference: e2studio_CPU0/ra_gen/common_data.c:7 (fb_background)
 * Reference: e2studio_CPU0/ra_gen/common_data.h:63-64 (DISPLAY_BUFFER_STRIDE_*)
 */
bool dave2d_port_fb_from_glcdc(dave2d_fb_t *fb, uint32_t index)
{
#if GLCDC_CFG_LAYER_1_ENABLE
    if (fb == NULL || index > 1) {
        return false;
    }

    fb->p_base = &fb_background[index][0];
    fb->pitch  = (int32_t)DISPLAY_BUFFER_STRIDE_PIXELS_INPUT0;
    fb->width  = (uint32_t)DISPLAY_HSIZE_INPUT0;
    fb->height = (uint32_t)DISPLAY_VSIZE_INPUT0;
    fb->format = d2_mode_rgb565;

    return true;
#else
    (void)fb; (void)index;
    return false;
#endif
}

/* ============================================================
 *  NT-Shell Command (S-004-1, S-004-2, S-004-4)
 * ============================================================ */

/**
 * NT-Shell "dave2d" command handler
 *
 * @details Provides Dave2D diagnostic, integration, test, and benchmark
 *          sub-commands:
 *   dave2d status          - Show Dave2D initialization state, HW/driver info
 *   dave2d integration     - Show LVGL-Dave2D integration status (S-004-3)
 *   dave2d test <pattern>  - Draw test patterns using Dave2D wrappers (S-004-2)
 *   dave2d bench [test]    - Run performance benchmarks (S-004-4)
 */
int usrcmd_dave2d(int argc, char **argv)
{
    if (argc < 2) {
        cmd_print_usage("dave2d", "<subcommand>");
        print_to_console("  status       - Show Dave2D initialization state and hardware info\r\n");
        print_to_console("  integration  - Show LVGL-Dave2D integration status\r\n");
        print_to_console("  test         - Draw test patterns using Dave2D wrappers\r\n");
        print_to_console("  bench        - Run performance benchmarks (Dave2D vs CPU)\r\n");
        return CMD_ERR_USAGE;
    }

    if (ntlibc_strcmp(argv[1], "status") == 0) {
        dave2d_cmd_status();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "integration") == 0) {
        dave2d_cmd_integration();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "test") == 0) {
        dave2d_cmd_test(argc, argv);
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "bench") == 0) {
        dave2d_cmd_bench(argc, argv);
        return CMD_OK;
    }

    /* Unknown sub-command */
    {
        char buf[DAVE2D_PRINT_BUF_SIZE];
        snprintf(buf, sizeof(buf), "Error: Unknown sub-command '%s'.\r\n", argv[1]);
        print_to_console(buf);
    }

    cmd_print_usage("dave2d", "<subcommand>");
    print_to_console("  status       - Show Dave2D initialization state and hardware info\r\n");
    print_to_console("  integration  - Show LVGL-Dave2D integration status\r\n");
    print_to_console("  test         - Draw test patterns using Dave2D wrappers\r\n");
    print_to_console("  bench        - Run performance benchmarks (Dave2D vs CPU)\r\n");

    return CMD_ERR_INVALID_ARG;
}
