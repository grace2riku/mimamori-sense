/**
 * @file lv_conf_user.h
 * @brief LVGL user configuration overrides for mimamori-sense (EK-RA8P1)
 *
 * @details
 * This file overrides the FSP-generated default LVGL configuration
 * (ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h) with project-specific settings.
 *
 * Configuration priority:
 *   lv_conf_user.h (this file) > lv_conf.h (FSP-generated defaults)
 *   The FSP lv_conf.h includes this file first, and uses #ifndef guards,
 *   so any #define set here takes precedence over the FSP defaults.
 *
 * Hardware target:
 *   - Board:   EK-RA8P1 (Renesas RA8P1 Cortex-M85 @ 1GHz)
 *   - Display: 1024x600, RGB565, GLCDC output
 *   - Touch:   GT911 (FT5X06 compatible), I2C, multi-touch
 *   - GPU:     D/AVE 2D (Dave2D) hardware accelerator
 *   - Memory:  1MB internal SRAM + 128MB SDRAM
 *   - LVGL:    v9.3.0+renesas.0.fsp.6.x.0
 *
 * Dave2D GPU acceleration (S-004-3):
 *   LV_USE_DRAW_DAVE2D is set to 1 in the FSP-generated lv_conf.h:
 *     e2studio_CPU0/ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h:107
 *   This enables the LVGL Dave2D draw unit which accelerates:
 *     - Rectangle fill  (lv_draw_dave2d_fill)
 *     - Border drawing  (lv_draw_dave2d_border)
 *     - Line drawing    (lv_draw_dave2d_line)
 *     - Arc drawing     (lv_draw_dave2d_arc)
 *     - Image/BLIT      (lv_draw_dave2d_image)
 *     - Label/Text      (lv_draw_dave2d_label)
 *     - Triangle        (lv_draw_dave2d_triangle)
 *   Operations not supported by Dave2D (e.g., gradients, box shadows,
 *   mask bitmaps) automatically fall back to the software renderer.
 *
 *   The Dave2D draw unit is initialized by lv_init() via lv_draw_dave2d_init().
 *   No user-side initialization is required for LVGL-Dave2D integration.
 *
 * Reference:
 *   - FSP LV_USE_DRAW_DAVE2D: e2studio_CPU0/ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h:106-108
 *   - LVGL Dave2D init: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:76-108
 *   - LVGL Dave2D eval: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:238-367
 *   - Reference project: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h
 *
 * Issue: #4 (F-001-3)
 */

#ifndef LV_CONF_USER_H_
#define LV_CONF_USER_H_

/*=============================================================
 * Operating System Abstraction (OSAL)
 *============================================================*/

/** R-006 (Issue #156): switch the LVGL OSAL from FreeRTOS to the custom
 * uT-Kernel 3.0 backend (R-006a spike decision: plan A).
 *
 * The FSP-generated lv_conf.h defines `LV_USE_OS (LV_OS_FREERTOS)` inside an
 * `#ifndef LV_USE_OS` guard (ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h:36-38) and
 * includes this file first, so defining it here overrides the FSP default
 * WITHOUT editing ra/lvgl/ or ra_cfg/. With LV_OS_CUSTOM:
 *   - ra/lvgl/lvgl/src/osal/lv_freertos.c compiles to nothing
 *     (`#if LV_USE_OS == LV_OS_FREERTOS` guard) - no build exclusion needed
 *   - lv_os.h includes LV_OS_CUSTOM_INCLUDE for the OSAL types
 *     (src/ is on the compiler include path)
 *   - the OSAL functions are provided by src/lv_os_mtkernel.c
 *
 * Fallback: setting `LV_USE_OS LV_OS_NONE` here degrades to synchronous
 * (non-threaded) rendering at COMPILE time (plan C). Note that lv_lock()
 * becomes a no-op in that case, so the NT-Shell `lvgl` commands (usrcmd.c)
 * must be disabled or given their own locking (spike report 4).
 *
 * Reference: doc/migration/r006a-lvgl-osal-spike.md 2.2 / 5.3
 */
#define LV_USE_OS            LV_OS_CUSTOM
#define LV_OS_CUSTOM_INCLUDE "lv_os_mtkernel.h"

/*=============================================================
 * Standard Library Selection
 *============================================================*/

/** Memory allocator backend
 *
 * Use LVGL's built-in memory allocator (lv_malloc / lv_free) rather than
 * the C standard library malloc. The built-in allocator provides:
 *   - Deterministic allocation time (important for real-time GUI rendering)
 *   - Memory usage monitoring via lv_mem_monitor()
 *   - Fragmentation tracking for long-running embedded systems
 *
 * Possible values:
 *   - LV_STDLIB_BUILTIN:     LVGL's built-in implementation
 *   - LV_STDLIB_CLIB:        Standard C functions (malloc, strlen, etc.)
 *   - LV_STDLIB_MICROPYTHON: MicroPython implementation
 *   - LV_STDLIB_RTTHREAD:    RT-Thread implementation
 *   - LV_STDLIB_CUSTOM:      Implement the functions externally
 */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    /** LVGL heap size: 256KB
     *
     * Rationale:
     *   - Increased from FSP default (128KB) to support multiple screens
     *     (home, camera view, alert display, settings) with dynamic widgets
     *   - Limited to 256KB because the LVGL heap is allocated in internal
     *     SRAM, which has limited capacity (~1MB total shared with FreeRTOS
     *     stacks, BSS, and other allocations)
     *   - 1MB (as originally specified in Issue #4) causes a linker overflow
     *     (~370KB over the RAM region limit)
     *   - To use a larger LVGL heap (e.g., 1MB), the heap must be placed
     *     in SDRAM via linker script modifications (future enhancement)
     *   - 256KB is sufficient for the initial UI implementation with
     *     moderate widget complexity
     *
     * FSP default: 0x20000 (128KB)
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:35
     */
    #define LV_MEM_SIZE (256 * 1024U)           /**< [bytes] 256KB */
#endif  /*LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN*/

/*=============================================================
 * Display Refresh Configuration
 *============================================================*/

/** Display refresh period: 16ms (~60 FPS)
 *
 * Rationale:
 *   - 16ms period yields ~62.5 FPS, matching common LCD panel refresh rates
 *   - The EK-RA8P1's 1GHz Cortex-M85 + Dave2D GPU can sustain 60 FPS
 *     for typical UI content (buttons, labels, simple animations)
 *   - Provides smooth touch response for the mimamori-sense home monitoring UI
 *   - Lower values (e.g., 33ms = ~30 FPS) would be acceptable for static
 *     content but may feel sluggish during screen transitions
 *   - Matches the reference project setting
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:38
 */
#define LV_DEF_REFR_PERIOD  16      /**< [ms] */

/*=============================================================
 * Drawing Layer Configuration
 *============================================================*/

/** Target buffer size for simple layer chunks: 256KB
 *
 * Rationale:
 *   - Larger buffers allow Dave2D to process bigger areas per draw call,
 *     reducing the number of GPU submissions and improving throughput
 *   - 256KB can hold a 256x256 pixel area in ARGB8888 (256*256*4 = 256KB)
 *     or a 512x256 area in RGB565 (512*256*2 = 256KB)
 *   - For the 1024x600 display, this allows efficient partial rendering
 *     of screen regions without excessive memory usage
 *   - Matches the reference project setting
 *
 * FSP default: 0x6000 (24KB) -- too small for efficient Dave2D batch rendering
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:41
 */
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE    (256 * 1024)    /**< [bytes] */

/** Maximum allocated memory for layers: 256KB
 *
 * Rationale:
 *   - This limit should be >= LV_DRAW_LAYER_SIMPLE_BUF_SIZE
 *   - For Dave2D GPU acceleration, larger layers allow more efficient
 *     batch rendering by reducing the number of GPU context switches
 *   - Set equal to LV_DRAW_LAYER_SIMPLE_BUF_SIZE since the mimamori-sense
 *     UI does not use heavy transformed layers (rotation, scaling)
 *   - If transformed layers are needed in the future, increase this to
 *     accommodate the largest transformed widget (width * height * 4 bytes)
 *   - Matches the reference project setting
 *
 * FSP default: 0 (no limit) -- explicit limit is safer for embedded systems
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:47
 */
#define LV_DRAW_LAYER_MAX_MEMORY (256 * 1024)  /**< [bytes] */

/*=============================================================
 * Dave2D GPU Acceleration Settings (S-004-3)
 *
 * The Dave2D draw engine is enabled by LV_USE_DRAW_DAVE2D=1 in the
 * FSP-generated lv_conf.h. The following settings tune LVGL's behavior
 * to work optimally with the Dave2D hardware accelerator.
 *
 * Dave2D accelerated operations (claimed by _dave2d_evaluate):
 *   LV_DRAW_TASK_TYPE_FILL      -> d2_renderbox (solid fill only, no gradients)
 *   LV_DRAW_TASK_TYPE_BORDER    -> d2_renderbox (border edges)
 *   LV_DRAW_TASK_TYPE_LINE      -> d2_renderline
 *   LV_DRAW_TASK_TYPE_ARC       -> d2_renderwedge
 *   LV_DRAW_TASK_TYPE_IMAGE     -> d2_setblitsrc + d2_blitcopy
 *   LV_DRAW_TASK_TYPE_LABEL     -> d2_setblitsrc (glyph blit)
 *   LV_DRAW_TASK_TYPE_TRIANGLE  -> d2_rendertri (solid fill only)
 *
 * CPU fallback operations (not claimed by Dave2D evaluate):
 *   LV_DRAW_TASK_TYPE_LAYER          -> SW renderer
 *   LV_DRAW_TASK_TYPE_BOX_SHADOW     -> SW renderer
 *   LV_DRAW_TASK_TYPE_MASK_RECTANGLE -> SW renderer (disabled in Dave2D)
 *   LV_DRAW_TASK_TYPE_MASK_BITMAP    -> SW renderer
 *   Gradient fills                    -> SW renderer (when stops differ)
 *   Gradient triangles                -> SW renderer (when stops differ)
 *
 * Reference:
 *   - _dave2d_evaluate: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:238-367
 *   - lv_dave2d_init: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:541-603
 *   - Reference lv_conf_user.h: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h
 *============================================================*/

/** LVGL demos (benchmark, widget showcase)
 *
 * Disabled for the mimamori-sense production application.
 * Enable temporarily to run Dave2D benchmark:
 *   1. Set LV_BUILD_DEMOS to 1
 *   2. Set LV_USE_DEMO_BENCHMARK to 1
 *   3. Call lv_demo_benchmark() from lvgl_thread_entry.c
 *   4. Compare FPS/CPU% with Dave2D enabled vs disabled
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:9
 */
#define LV_BUILD_DEMOS 0

#if LV_BUILD_DEMOS
    /** Show some widgets. */
    #define LV_USE_DEMO_WIDGETS 0

    /** Benchmark your system - compare Dave2D vs SW rendering performance. */
    #define LV_USE_DEMO_BENCHMARK 0

    #if LV_USE_DEMO_BENCHMARK
        /** Use fonts where bitmaps are aligned 16 byte and has Nx16 byte stride.
         *  This improves Dave2D BLIT performance for text rendering because
         *  d2_setblitsrc() works more efficiently with aligned source data.
         *
         *  Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:19-20 */
        #define LV_DEMO_BENCHMARK_ALIGNED_FONTS 1
    #endif
#endif

/*=============================================================
 * TinyTTF Font Engine
 *============================================================*/

/** Enable Tiny TTF font rendering engine
 *
 * Rationale:
 *   - Tiny TTF allows loading TrueType fonts at runtime from file or memory
 *   - Essential for future Japanese font support: the built-in LVGL bitmap
 *     fonts (Montserrat, Source Han Sans SC) cover limited CJK code points
 *   - With Tiny TTF, a Japanese TrueType font (e.g., Noto Sans JP) can be
 *     loaded from external storage (SD card or SPI flash) without increasing
 *     the firmware binary size
 *   - The font is rasterized at the requested size on demand, trading
 *     some CPU time for significant flash/ROM savings
 *   - Matches the reference project setting
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:49
 */
#define LV_USE_TINY_TTF 1

/*=============================================================
 * End Dave2D GPU Acceleration Settings
 *============================================================*/

/*=============================================================
 * Logging Configuration
 *============================================================*/

/** Enable LVGL logging
 *
 * Rationale:
 *   - Logging is critical during development to diagnose rendering issues,
 *     memory allocation failures, and Dave2D fallback behavior
 *   - TODO: Disable (set to 0) for production release to reduce code size
 *     and eliminate logging overhead
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:51
 */
#define LV_USE_LOG 1

#if LV_USE_LOG
    /** Log level: WARN
     *
     * Rationale:
     *   - WARN level captures unexpected conditions (e.g., Dave2D fallback
     *     to SW renderer, memory allocation near capacity) without flooding
     *     the console with routine trace messages
     *   - For deep debugging, temporarily change to LV_LOG_LEVEL_INFO or
     *     LV_LOG_LEVEL_TRACE
     *   - Available levels:
     *     LV_LOG_LEVEL_TRACE  - Detailed information (very verbose)
     *     LV_LOG_LEVEL_INFO   - Important events
     *     LV_LOG_LEVEL_WARN   - Unwanted but non-critical situations
     *     LV_LOG_LEVEL_ERROR  - Critical issues, system may fail
     *     LV_LOG_LEVEL_USER   - Custom log messages only
     *     LV_LOG_LEVEL_NONE   - No logging
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:60
     */
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

    /** Route log output to printf
     *
     * Rationale:
     *   - Disabled because the embedded toolchain (LLVM Embedded) does not
     *     provide the `stdout` symbol required by vprintf/printf
     *   - LVGL log output can be captured by registering a custom callback
     *     via lv_log_register_print_cb() that routes to the J-Link UART
     *   - The reference project enables this (=1) but uses a different
     *     toolchain/libc configuration that provides stdout
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:64
     */
    #define LV_LOG_PRINTF 0

    /** Print timestamp with log messages
     *
     * Rationale:
     *   - Timestamps help correlate LVGL events with NT-Shell command
     *     output and FreeRTOS task timing
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:73
     */
    #define LV_LOG_USE_TIMESTAMP 1

    /** Print file name and line number with log messages
     *
     * Rationale:
     *   - File/line info makes it easy to locate the source of warnings
     *     and errors in the LVGL codebase
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:77
     */
    #define LV_LOG_USE_FILE_LINE 1

    /* Per-module trace log switches
     *
     * These are only effective when LV_LOG_LEVEL is set to LV_LOG_LEVEL_TRACE.
     * Enable selectively to debug specific subsystems without overwhelming
     * the console with unrelated trace output.
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:80-88
     */
    #define LV_LOG_TRACE_MEM        1   /**< Trace memory operations (lv_malloc/lv_free) */
    #define LV_LOG_TRACE_TIMER      1   /**< Trace timer operations (lv_timer_handler) */
    #define LV_LOG_TRACE_INDEV      1   /**< Trace input device events (touch panel) */
    #define LV_LOG_TRACE_DISP_REFR  1   /**< Trace display refresh cycles */
    #define LV_LOG_TRACE_EVENT      1   /**< Trace event dispatch logic */
    #define LV_LOG_TRACE_OBJ_CREATE 1   /**< Trace widget creation */
    #define LV_LOG_TRACE_LAYOUT     1   /**< Trace flex/grid layout calculations */
    #define LV_LOG_TRACE_ANIM       1   /**< Trace animation logic */
    #define LV_LOG_TRACE_CACHE      1   /**< Trace cache operations */
#endif  /*LV_USE_LOG*/

/*=============================================================
 * Assert Configuration
 *============================================================*/

/** Assertion checks
 *
 * Rationale:
 *   - Disabled for normal development to avoid performance overhead
 *   - Enable LV_USE_ASSERT_NULL and LV_USE_ASSERT_MALLOC temporarily
 *     when debugging crashes or memory issues (both are very fast)
 *   - LV_USE_ASSERT_MEM_INTEGRITY and LV_USE_ASSERT_OBJ are slow and
 *     should only be enabled for specific debugging sessions
 *   - TODO: Consider enabling NULL and MALLOC asserts in debug builds
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:97-101
 */
#define LV_USE_ASSERT_NULL          0   /**< Check NULL parameters (very fast) */
#define LV_USE_ASSERT_MALLOC        0   /**< Check malloc success (very fast) */
#define LV_USE_ASSERT_STYLE         0   /**< Check style initialization (very fast) */
#define LV_USE_ASSERT_MEM_INTEGRITY 0   /**< Check lv_mem integrity (slow) */
#define LV_USE_ASSERT_OBJ           0   /**< Check object type/existence (slow) */

/*=============================================================
 * Feature Toggles
 *============================================================*/

/** Disable XML parser
 *
 * Rationale:
 *   - XML is not used in the mimamori-sense UI definition
 *   - Disabling reduces code size
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:104
 */
#define LV_USE_XML 0

/** Disable complex software gradients
 *
 * Rationale:
 *   - Dave2D does not accelerate gradient fills, so gradients fall back
 *     to the software renderer
 *   - Disabling complex gradients (linear at angle, radial, conical)
 *     reduces SW renderer overhead for operations Dave2D cannot handle
 *   - Simple horizontal/vertical gradients are still available if needed
 *   - The mimamori-sense UI uses solid colors for clarity and readability,
 *     consistent with a home monitoring interface design
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:107
 */
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS    0

/*=============================================================
 * FLASH Reduction (Issue #182) - MEASURED RESULT
 *
 * Measured on a clean rebuild, from the `.flash.startof` .. `.flash.endof`
 * span in Debug/mimamori_sense_CPU0.map:
 *
 *   before  990,720 B used / 25,088 B free  (97.5% of 992 KiB)
 *   after   863,744 B used / 152,064 B free (85.0% of 992 KiB)
 *   saved   126,976 B (124.0 KiB)
 *
 * Per category (cumulative, measured one category at a time):
 *   category 1  SW blender color formats   -57,344 B
 *               (-64,512 B before restoring RGB888 as a gradient SOURCE
 *                format, +7,168 B to restore it - see that block below)
 *   category 2  unused widgets + layouts   -69,632 B
 *   category 3  fonts                        no change - see the note in
 *                                            "Font Configuration" below
 *
 * LVGL's own flash footprint went from 313,664 B to 190,200 B.
 *============================================================*/

/*=============================================================
 * FLASH Reduction: SW Blender Color Formats (Issue #182, category 1)
 *
 * Each `LV_DRAW_SW_SUPPORT_<fmt>` gates TWO things - both must be checked
 * before disabling one (this cost us a P2 review finding on PR #190):
 *
 *   (a) DESTINATION: whether `lv_draw_sw_blend_to_<fmt>.o` (~6-12 KB) is
 *       compiled at all, i.e. whether the blender can render INTO that format.
 *   (b) SOURCE: the `case LV_COLOR_FORMAT_<fmt>:` arm inside every OTHER
 *       blender, i.e. whether the blender can read FROM that format
 *       (e.g. ra/lvgl/lvgl/src/draw/sw/blend/lv_draw_sw_blend_to_rgb565.c
 *       :385-435 switches on `dsc->src_color_format`).
 *
 * The FSP-generated lv_conf.h enables all of them
 * (ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h:60-104).
 *
 * (a) Destination formats reachable in this project - exactly two:
 *
 *   RGB565    - the display buffer (LV_COLOR_DEPTH 16, lv_conf.h:22-23) and
 *               every layer created with LV_COLOR_FORMAT_NATIVE
 *               (ra/lvgl/lvgl/src/core/lv_refr.c:1254, false branch)
 *   ARGB8888  - layers that need an alpha channel. Created at
 *               lv_refr.c:195 and :216 (clip-corner path: an object with a
 *               radius that clips its children) and at lv_refr.c:1253-1254
 *               (transform / opacity path, when `area_need_alpha` is true).
 *               These are RUNTIME decisions driven by widget styles, so this
 *               format must stay enabled even though the current screens may
 *               not hit it on every frame.
 *
 * (b) Source formats: every producer of `blend_dsc.src_color_format` in
 *     draw/sw/ was enumerated. Only two kinds exist:
 *
 *     - DECODED IMAGES - the format comes from the image header `cf`
 *       (lv_draw_sw_img.c:267-515, lv_draw_sw_arc.c:147). The only image this
 *       project draws is the camera framebuffer, declared RGB565
 *       (src/camera_display.c:169, src/ui/ui_main_screen.c:430).
 *     - GRADIENTS - hardcoded to RGB888, NOT derived from any image:
 *         lv_draw_sw_fill.c:123      gradient fills     (dir >= LV_GRAD_DIR_HOR)
 *         lv_draw_sw_triangle.c:147  gradient triangles
 *       This is why RGB888 stays enabled below even though nothing renders
 *       INTO RGB888.
 *
 * Text/labels are NOT affected by any of this: glyphs are blended through the
 * `mask_buf` path and never set `src_color_format` (no producer in draw/sw/).
 *
 * IMPORTANT - failure mode if one of these is wrong:
 *   The blender dispatch is a `switch(layer_cf)` whose arms are `#if`-guarded,
 *   ending in `default: break;`
 *   (ra/lvgl/lvgl/src/draw/sw/blend/lv_draw_sw_blend.c:174-281).
 *   Disabling a format that IS needed therefore produces NO link error and NO
 *   assert - the blend is silently skipped and the affected area renders
 *   blank/stale. Any visual regression after touching this block should be
 *   investigated here first.
 *============================================================*/

/* Kept: the display / native-layer format. */
#define LV_DRAW_SW_SUPPORT_RGB565               1

/* Kept: required by the alpha-layer paths in lv_refr.c cited above. */
#define LV_DRAW_SW_SUPPORT_ARGB8888             1

/* Disabled: byte-swapped RGB565 is only used when the display is explicitly
 * switched to it via lv_display_set_color_format(). This project never calls
 * that function, so the display keeps LV_COLOR_FORMAT_NATIVE (= RGB565 at
 * LV_COLOR_DEPTH 16); see src/port/lvgl_port_mtk3.c:164, which only calls
 * lv_display_set_buffers_with_stride(). */
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED       0

/* KEPT AT 1 - required as a SOURCE format, not as a destination.
 *
 * Nothing in this project renders INTO RGB888, but the SW renderer hardcodes
 * RGB888 as the source format for gradient scanlines:
 *   lv_draw_sw_fill.c:123      gradient fills, when dir >= LV_GRAD_DIR_HOR
 *                              (i.e. HOR and the complex dirs; VER = 1 sorts
 *                              below HOR = 2 in lv_grad.h:31-38 and takes the
 *                              per-row solid-colour path instead)
 *   lv_draw_sw_triangle.c:147  gradient triangles
 * and Dave2D declines any gradient whose first and last stop differ
 * (lv_draw_dave2d.c:250-265), so those tasks always land on the SW renderer.
 *
 * With this at 0, the RGB888 source arm of lv_draw_sw_blend_to_rgb565.c
 * (:396-400) is compiled out and a horizontal gradient fill would be SILENTLY
 * SKIPPED - no link error, no assert, just an unpainted area. That would also
 * contradict LV_USE_DRAW_SW_COMPLEX_GRADIENTS above, which deliberately keeps
 * simple horizontal/vertical gradients available.
 *
 * Costs 7,168 B (measured). No widget currently sets a gradient (the only one in
 * lv_theme_default.c is styles.led at :603, and LV_USE_LED is 0 below), so
 * this is purely insurance against a future style change failing silently.
 * Reported as a P2 finding on PR #190. */
#define LV_DRAW_SW_SUPPORT_RGB888               1

/* Disabled: XRGB8888 is reachable only as a decoded-image source format or an
 * XRGB8888 destination buffer. No gradient or other draw path produces it, and
 * the only image drawn is RGB565. */
#define LV_DRAW_SW_SUPPORT_XRGB8888             0

/* Disabled: premultiplied ARGB is only produced by lv_draw_buf_premultiply(),
 * which this project never calls. */
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0

/* Disabled: grayscale / indexed formats. As destinations they need an
 * lv_canvas of that format (LV_USE_CANVAS is 0 below); as sources they need a
 * decoded image whose header `cf` says L8/AL88/I1. The only image drawn is
 * RGB565, and no draw path synthesises these the way gradients synthesise
 * RGB888. */
#define LV_DRAW_SW_SUPPORT_L8                   0
#define LV_DRAW_SW_SUPPORT_AL88                 0
#define LV_DRAW_SW_SUPPORT_I1                   0

/* Disabled: A8 / RGB565A8 are source formats for the SW image transform path
 * (ra/lvgl/lvgl/src/draw/sw/lv_draw_sw_transform.c:72-84, 253-283) and, for
 * RGB565A8, an image source arm. Both come from a decoded image's header `cf`;
 * the only image drawn is the camera framebuffer, declared RGB565
 * (src/camera_display.c:169, src/ui/ui_main_screen.c:430) and never rotated or
 * scaled. NOTE: font glyphs do NOT go through A8 here - they use the mask_buf
 * path, which is independent of these switches (see the section header). */
#define LV_DRAW_SW_SUPPORT_A8                   0
#define LV_DRAW_SW_SUPPORT_RGB565A8             0

/*=============================================================
 * FLASH Reduction: Unused Widgets (Issue #182, category 2)
 *
 * A full grep of e2studio_CPU0/src/ for `lv_*_create(` shows that this
 * application instantiates only four widget classes:
 *
 *   lv_obj_create     (5x)  - core, has no LV_USE_ switch
 *   lv_label_create   (5x)  - src/ui/ui_main_screen.c, fall_detection_screen.c
 *   lv_image_create   (1x)  - src/ui/ui_main_screen.c (camera view)
 *   lv_button_create  (1x)  - src/ui/ui_main_screen.c (settings button)
 *
 * Everything else below is dead weight, but `--gc-sections` cannot drop it:
 * lv_theme_default.c dispatches on every widget class it was compiled with
 * (`lv_obj_check_type(obj, &lv_chart_class)` etc.), so each widget's class
 * object stays referenced from the theme and drags in the whole translation
 * unit. Compile-time exclusion via LV_USE_* is the only way to remove them.
 *
 * Re-enable the matching switch here before using a new widget - LVGL will
 * otherwise fail to compile the `lv_<widget>_create()` call rather than fail
 * silently.
 *============================================================*/

/* Kept: the four classes actually used (see grep above). */
#define LV_USE_LABEL        1
#define LV_USE_IMAGE        1
#define LV_USE_BUTTON       1

/* Disabled: never instantiated by this application. */
#define LV_USE_ANIMIMG      0
#define LV_USE_ARC          0
#define LV_USE_BAR          0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR     0
#define LV_USE_CANVAS       0
#define LV_USE_CHART        0
#define LV_USE_CHECKBOX     0
#define LV_USE_DROPDOWN     0
#define LV_USE_IMAGEBUTTON  0
#define LV_USE_KEYBOARD     0
#define LV_USE_LED          0
#define LV_USE_LINE         0
#define LV_USE_LIST         0
#define LV_USE_MENU         0
#define LV_USE_MSGBOX       0
#define LV_USE_ROLLER       0
#define LV_USE_SCALE        0
#define LV_USE_SLIDER       0
#define LV_USE_SPAN         0
#define LV_USE_SPINBOX      0
#define LV_USE_SPINNER      0
#define LV_USE_SWITCH       0
#define LV_USE_TABLE        0
#define LV_USE_TABVIEW      0
#define LV_USE_TEXTAREA     0
#define LV_USE_TILEVIEW     0
#define LV_USE_WIN          0

/*=============================================================
 * FLASH Reduction: Unused Layouts and Helpers (Issue #182, category 4)
 *============================================================*/

/* Disabled: the UI positions every child explicitly with lv_obj_set_pos() /
 * lv_obj_align() / lv_obj_center(). Neither lv_obj_set_flex_* nor
 * lv_obj_set_grid_* appears anywhere in e2studio_CPU0/src/. The widgets that
 * use flex internally (menu, list, tabview) are all disabled above. */
#define LV_USE_FLEX 0
#define LV_USE_GRID 0

/* KEPT AT 1 (648 B) even though this application never calls the
 * observer/subject API directly: lv_sysmon.h:27 is a hard `#error` gate -
 * `LV_USE_SYSMON 1` (set below, and required for the LV_USE_PERF_MONITOR FPS
 * overlay) does not compile without it. Setting this to 0 breaks the build
 * rather than silently degrading. */
#define LV_USE_OBSERVER 1

/*=============================================================
 * Font Configuration
 *============================================================*/

/** Default font: Montserrat 14pt
 *
 * Rationale:
 *   - Montserrat 14pt provides good readability on the 1024x600 display
 *     at typical viewing distances for a home monitoring device
 *   - The FSP-generated lv_conf.h already enables Montserrat 8-48pt and
 *     sets the default to Montserrat 14, but we explicitly define it here
 *     to make the project's font choice visible and intentional
 *   - Additional font sizes (16, 20, 24, 32) are available from the FSP
 *     defaults for headings, buttons, and status displays
 *
 * FSP default: LV_FONT_MONTSERRAT_14 = 1 (already enabled)
 *
 * Reference: e2studio_CPU0/ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h:135-210
 */
#define LV_FONT_MONTSERRAT_14 1

/*
 * Fonts and FLASH size (Issue #182, category 3) - MEASURED FINDING
 *
 * The FSP lv_conf.h enables Montserrat 8..48, DejaVu Persian/Hebrew, both
 * Source Han Sans SC CJK fonts and UNSCII 8/16 (lv_conf.h:126-206), but
 * setting the unused ones to 0 saves NOTHING in flash: each font is a single
 * `const lv_font_t` in its own translation unit, so `--gc-sections` already
 * drops every font nothing references. The map confirms it - only the three
 * fonts actually referenced by src/ui/ occupy flash:
 *
 *   lv_font_montserrat_20  21,795 B  ui/fall_detection_screen.c:342
 *   lv_font_montserrat_16  15,899 B  ui/ui_main_screen.c:344
 *   lv_font_montserrat_14  13,641 B  ui/ui_main_screen.c:354,
 *                                    ui/fall_detection_screen.c:368
 *                                    (also LV_FONT_DEFAULT)
 *
 * So the ~51 KB spent on fonts can only be recovered by using FEWER SIZES or
 * by subsetting the glyph range - both change what is on screen, so neither
 * is done here. Left as a follow-up; note that ui_main_screen.c:375 renders
 * LV_SYMBOL_SETTINGS, so any replacement font must keep the FontAwesome
 * symbol range.
 */

/*
 * Japanese Font Support (Future)
 *
 * The FSP configuration includes CJK bitmap fonts:
 *   - LV_FONT_SOURCE_HAN_SANS_SC_14_CJK (lv_conf.h:194-196)
 *   - LV_FONT_SOURCE_HAN_SANS_SC_16_CJK (lv_conf.h:197-199)
 *
 * These Source Han Sans SC fonts cover Simplified Chinese CJK characters.
 * While they include many kanji shared with Japanese, they do NOT cover:
 *   - Japanese-specific kanji variants (JIS X 0208 / JIS X 0213)
 *   - Hiragana/Katakana (partially covered)
 *   - Japanese punctuation and symbols
 *
 * For full Japanese language support, the following approaches are recommended:
 *
 *   Option A: Runtime TTF loading (recommended)
 *     - Enable LV_USE_TINY_TTF (already set to 1 above)
 *     - Load a Japanese TrueType font (e.g., Noto Sans JP, M PLUS 1p)
 *       from SD card or SPI flash at runtime
 *     - Pros: No firmware size increase, flexible font selection
 *     - Cons: Slightly slower rendering (TTF rasterization), requires
 *       external storage
 *
 *   Option B: Custom bitmap font
 *     - Use the LVGL font converter tool to generate a bitmap font from
 *       a Japanese TrueType font with only the needed code points
 *     - Include the generated .c file in the project
 *     - Pros: Fast rendering (pre-rasterized), no external storage needed
 *     - Cons: Large firmware size increase (~500KB-2MB depending on
 *       character coverage), requires regeneration when adding characters
 *
 *   Option C: Use Source Han Sans SC (quick prototype)
 *     - Use the FSP-provided CJK fonts for basic Japanese text
 *     - Call lv_obj_set_style_text_font(obj, &lv_font_source_han_sans_sc_16_cjk, 0)
 *     - Pros: No additional setup needed, already in firmware
 *     - Cons: Incomplete Japanese coverage, SC (Simplified Chinese) glyph
 *       variants may look incorrect for some Japanese characters
 */

/*=============================================================
 * System Monitor and Performance
 *============================================================*/

/** Enable system monitor component
 *
 * Rationale:
 *   - Provides runtime CPU usage and FPS monitoring essential during
 *     development to validate Dave2D GPU offloading effectiveness
 *   - TODO: Disable for production release to reclaim screen area and
 *     reduce processing overhead
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:111
 */
#define LV_USE_SYSMON   1

#if LV_USE_SYSMON
    /* R-006 (Issue #156): the `LV_SYSMON_GET_IDLE lv_os_get_idle_percent`
     * override (Issue #138, FreeRTOS trace-hook based measurement) was
     * REMOVED. LVGL's default for LV_SYSMON_GET_IDLE is the very same
     * `lv_os_get_idle_percent` (lv_conf_internal.h:3206), which is now
     * implemented by the custom uT-Kernel OSAL (src/lv_os_mtkernel.c) as a
     * delegation to lv_timer_get_idle(). The FreeRTOS trace hooks in
     * User_FreeRTOSConfig.h were removed together (lv_freertos.c compiles
     * empty under LV_OS_CUSTOM, so the hook targets no longer exist).
     * Precision note: the value now reflects "idle inside the LVGL task"
     * rather than whole-CPU idle - acceptable for the on-screen monitor
     * (spike report 2.5 / 6). */

    /** Performance monitor (FPS + CPU usage overlay)
     *
     * Rationale:
     *   - Displays an on-screen overlay showing:
     *     * FPS: Frames per second (target: >= 30 FPS, ideally 60 FPS)
     *     * CPU: Percentage of time spent in LVGL processing
     *   - With Dave2D acceleration enabled, CPU usage should be noticeably
     *     lower than with SW-only rendering. The FPS counter shows how many
     *     frames per second LVGL can render.
     *   - Comparing CPU% with Dave2D enabled vs disabled confirms GPU offloading
     *   - Positioned at bottom-right to avoid overlapping the main UI content
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:118-124
     */
    #define LV_USE_PERF_MONITOR 1
    #if LV_USE_PERF_MONITOR
        /** Position of the performance monitor overlay */
        #define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT

        /** Output mode: 0 = on-screen overlay, 1 = log output
         *
         * On-screen mode (0) provides immediate visual feedback during development.
         * Log mode (1) is useful when the overlay interferes with UI testing.
         */
        #define LV_USE_PERF_MONITOR_LOG_MODE 0
    #endif

    /** Memory monitor (heap usage overlay)
     *
     * Rationale:
     *   - Disabled by default to keep the screen uncluttered
     *   - Can be enabled temporarily to monitor LVGL heap usage during
     *     development, especially when adding new screens or widgets
     *   - Memory usage can also be checked via the "lvgl mem" NT-Shell
     *     command without the on-screen overlay
     *   - Requires LV_USE_STDLIB_MALLOC = LV_STDLIB_BUILTIN
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:129-132
     */
    #define LV_USE_MEM_MONITOR 0
    #if LV_USE_MEM_MONITOR
        #define LV_USE_MEM_MONITOR_POS LV_ALIGN_BOTTOM_LEFT
    #endif
#endif /*LV_USE_SYSMON*/

#endif /* LV_CONF_USER_H_ */
