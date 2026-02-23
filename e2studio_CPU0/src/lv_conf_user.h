/* See ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h
 * to see default overrides
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
 */

#ifndef LV_CONF_USER_H_
#define LV_CONF_USER_H_

/** Possible values
 * - LV_STDLIB_BUILTIN:     LVGL's built in implementation
 * - LV_STDLIB_CLIB:        Standard C functions, like malloc, strlen, etc
 * - LV_STDLIB_MICROPYTHON: MicroPython implementation
 * - LV_STDLIB_RTTHREAD:    RT-Thread implementation
 * - LV_STDLIB_CUSTOM:      Implement the functions externally
 */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    /** Size of memory available for `lv_malloc()` in bytes (>= 2kB) */
    #define LV_MEM_SIZE (0x20000)               /**< [bytes] 128KB (matches FSP default) */
#endif  /*LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN*/

#define LV_DEF_REFR_PERIOD  16      /**< [ms] */

/** The target buffer size for simple layer chunks.
 *  Larger buffers allow Dave2D to process bigger areas per draw call,
 *  reducing the number of GPU submissions and improving throughput. */
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE    (256 * 1024)    /**< [bytes]*/

/* Limit the max allocated memory for simple and transformed layers.
 * This limit should be >= LV_DRAW_LAYER_SIMPLE_BUF_SIZE. For Dave2D
 * GPU acceleration, larger layers allow more efficient batch rendering. */
#define LV_DRAW_LAYER_MAX_MEMORY (256 * 1024)  /**< [bytes]*/

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

/** Enable LVGL benchmark demo for Dave2D performance measurement (S-004-4).
 *  Set to 0 to disable benchmark demo and reduce code size.
 *
 *  Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h:9 */
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
 * End Dave2D GPU Acceleration Settings
 *============================================================*/

#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 0
    #define LV_LOG_USE_TIMESTAMP 1
    #define LV_LOG_USE_FILE_LINE 1
#endif  /*LV_USE_LOG*/

#define LV_USE_ASSERT_NULL          0
#define LV_USE_ASSERT_MALLOC        0
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

#define LV_USE_XML 0

/** Disable complex gradients in software renderer.
 *  Dave2D does not accelerate gradient fills, so gradients fall back to
 *  the software renderer. Disabling complex gradients reduces SW renderer
 *  overhead for operations that Dave2D cannot handle. */
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS    0

/** 1: Enable system monitor component */
#define LV_USE_SYSMON   1
#if LV_USE_SYSMON
    #define LV_SYSMON_GET_IDLE lv_timer_get_idle

    /** 1: Show CPU usage and FPS count.
     *  With Dave2D acceleration enabled, the CPU usage should be noticeably
     *  lower than with SW-only rendering. The FPS counter shows how many
     *  frames per second LVGL can render.
     *  Comparing CPU% with Dave2D enabled vs disabled confirms GPU offloading. */
    #define LV_USE_PERF_MONITOR 1
    #if LV_USE_PERF_MONITOR
        #define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT
        #define LV_USE_PERF_MONITOR_LOG_MODE 0
    #endif

    #define LV_USE_MEM_MONITOR 0
#endif /*LV_USE_SYSMON*/

#endif /* LV_CONF_USER_H_ */
