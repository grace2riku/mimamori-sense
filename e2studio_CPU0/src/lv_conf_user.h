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
    /** LVGL heap size: 1MB
     *
     * Rationale:
     *   - The mimamori-sense UI requires multiple screens (home, camera view,
     *     alert display, settings) with image assets and dynamic widgets
     *   - 1024x600 RGB565 display: a single full-screen image buffer alone
     *     would consume ~1.2MB (1024 * 600 * 2 bytes), but LVGL manages
     *     rendering via partial buffers, not full-screen image allocation
     *   - 1MB provides sufficient headroom for:
     *     * Multiple screen objects and widget trees
     *     * Style and theme data
     *     * Temporary draw buffers for layer compositing
     *     * Animation state tracking
     *     * Future expansion (additional screens, more complex widgets)
     *   - The EK-RA8P1 has 128MB SDRAM available for framebuffers and large
     *     assets, so 1MB from internal SRAM for the LVGL heap is acceptable
     *   - Matches the reference project setting
     *
     * FSP default: 0x20000 (128KB) -- too small for rich UI with multiple screens
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:35
     */
    #define LV_MEM_SIZE (1024 * 1024U)          /**< [bytes] 1MB */
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
     *   - printf output is captured by the J-Link RTT console, which is
     *     already configured in this project for NT-Shell
     *   - Simpler than registering a custom callback via lv_log_register_print_cb()
     *   - Set to 0 if a custom log callback is needed (e.g., to route logs
     *     to a file or network)
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:64
     */
    #define LV_LOG_PRINTF 1

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
    /** Idle percentage callback
     *
     * Uses LVGL's built-in timer idle tracking. Returns the percentage of
     * time the system was idle (not processing LVGL tasks) during the last
     * refresh period.
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:114
     */
    #define LV_SYSMON_GET_IDLE lv_timer_get_idle

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
