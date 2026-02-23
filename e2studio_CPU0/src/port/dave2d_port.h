/**
 * @file dave2d_port.h
 * @brief Dave2D (D/AVE 2D) graphics accelerator port layer for EK-RA8P1 board
 * @details
 * Provides Dave2D initialization tracking, status query, basic drawing
 * wrapper functions, and diagnostic functions for the Renesas D/AVE 2D
 * hardware graphics engine on the RA8P1 MCU.
 *
 * Dave2D Initialization Architecture:
 *   The Dave2D hardware is initialized by LVGL's internal Dave2D draw unit
 *   (lv_draw_dave2d.c) when LV_USE_DRAW_DAVE2D is enabled (set to 1 in
 *   ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h). The initialization is triggered
 *   by lv_init() -> lv_draw_dave2d_init() -> lv_dave2d_init().
 *
 *   LVGL's internal initialization sequence (lv_dave2d_init):
 *     1. d2_opendevice(0)              - Create Dave2D device context
 *     2. d2_inithw(handle, 0)          - Initialize Dave2D hardware
 *     3. d2_setblendmode()             - Set alpha blending mode
 *     4. d2_setalphamode()             - Set constant alpha mode
 *     5. d2_setalpha(UINT8_MAX)        - Set full opacity
 *     6. d2_setantialiasing(1)         - Enable antialiasing
 *     7. d2_setlinecap(d2_lc_butt)     - Set line cap style
 *     8. d2_setlinejoin(d2_lj_miter)   - Set line join style
 *     9. d2_setdlistblocksize(25)      - Set display list block size
 *    10. d2_newrenderbuffer() x2       - Create blit and main render buffers
 *    11. d2_selectrenderbuffer()       - Select main render buffer
 *
 *   Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:541-604
 *
 * This Port Layer's Role:
 *   Since LVGL already performs the Dave2D initialization, this port layer
 *   does NOT duplicate the init sequence. Instead, it provides:
 *     - Post-init validation: verifies Dave2D was initialized by LVGL
 *     - Status tracking: records whether Dave2D is available
 *     - Hardware info query: HW revision, driver version
 *     - Basic drawing wrappers: rect fill, rect outline, line, blit, alpha (S-004-2)
 *     - Color conversion utilities: RGB565<->d2_color, YUV->RGB (S-004-2)
 *     - Rendering pipeline control: execute + flush wrappers (S-004-2)
 *     - Diagnostic output: NT-Shell "dave2d status/test" commands
 *     - Clean shutdown support: dave2d_port_deinit() for orderly shutdown
 *
 *   The LVGL internal handle (_d2_handle) is accessed via the extern
 *   declaration from lv_draw_dave2d.c. The FSP-generated d2_handle0
 *   (common_data.c) is a separate variable not used by this module.
 *
 * Dave2D Capabilities (RA8P1 D/AVE 2D v3.17):
 *   - Hardware-accelerated rectangle fill (d2_renderbox)
 *   - Hardware-accelerated line drawing (d2_renderline)
 *   - Hardware-accelerated arc/circle drawing (d2_renderwedge)
 *   - Texture mapping with bilinear filtering
 *   - BLIT with scaling (d2_setblitsrc + d2_blitcopy)
 *   - Alpha blending (per-pixel and per-primitive)
 *   - Antialiasing for all primitives
 *   - Triangle rendering
 *   - Display list based command processing
 *
 * Drawing Wrapper Functions (S-004-2):
 *   The wrapper functions provide a simplified interface to Dave2D operations
 *   for use by application code (outside of LVGL). Each wrapper:
 *     - Validates that Dave2D is initialized before proceeding
 *     - Uses the LVGL-managed render buffer (_blit_renderbuffer)
 *     - Acquires the LVGL Dave2D mutex for thread safety
 *     - Sets up framebuffer, clip rect, color, alpha
 *     - Issues the rendering command
 *     - Executes the render buffer and flushes (immediate mode)
 *     - Restores previous state and releases the mutex
 *
 *   Note: These wrappers are for non-LVGL use (e.g., camera image DMA,
 *   test patterns, NT-Shell commands). LVGL uses its own draw pipeline
 *   (lv_draw_dave2d_*) which manages the render buffer differently.
 *
 * LVGL Integration:
 *   When LV_USE_DRAW_DAVE2D = 1, LVGL registers a Dave2D draw unit that
 *   evaluates each draw task. If Dave2D can accelerate the operation
 *   (rectangle fill, line, image blit, label, arc, triangle), it claims
 *   the task and uses the hardware engine. Otherwise, the software renderer
 *   handles it.
 *
 * Reference:
 *   - LVGL Dave2D init: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:76-108
 *   - LVGL Dave2D hw init: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:541-604
 *   - LVGL Dave2D fill: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d_fill.c:6-311
 *   - LVGL Dave2D line: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d_line.c:6-94
 *   - LVGL Dave2D image: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d_image.c:42-328
 *   - LVGL Dave2D blit(buf_copy): e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:156-213
 *   - Dave2D driver API: e2studio_CPU0/ra/tes/dave2d/inc/dave_driver.h
 *   - FSP d2_handle0: e2studio_CPU0/ra_gen/common_data.c:4
 *   - LV_USE_DRAW_DAVE2D: e2studio_CPU0/ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h:107
 *
 * LVGL-Dave2D Integration (S-004-3):
 *   The integration between LVGL and Dave2D is configured and managed as follows:
 *
 *   Configuration:
 *     - LV_USE_DRAW_DAVE2D=1 in FSP lv_conf.h enables the Dave2D draw unit
 *     - lv_conf_user.h contains Dave2D-optimized settings (layer sizes, etc.)
 *     - No additional user-side initialization is needed for LVGL-Dave2D
 *
 *   Runtime flow:
 *     1. lv_init() calls lv_draw_dave2d_init() which:
 *        a. Creates the Dave2D device handle (_d2_handle)
 *        b. Initializes the D/AVE 2D hardware
 *        c. Registers lv_draw_dave2d_unit_t as draw unit ID=4
 *        d. Creates a render thread for asynchronous draw task execution
 *     2. For each draw task, LVGL calls _dave2d_evaluate() to check if
 *        Dave2D can handle it. If claimed, the Dave2D draw unit processes
 *        it via hardware acceleration. Otherwise, the SW renderer handles it.
 *     3. dave2d_port_init() verifies the initialization succeeded.
 *
 *   Accelerated operations (from _dave2d_evaluate):
 *     - Fill (solid color only, no gradients)
 *     - Border
 *     - Line
 *     - Arc
 *     - Image (supported color formats: A8, RGB565, ARGB1555, ARGB4444, ARGB8888, XRGB8888)
 *     - Label (glyph BLIT)
 *     - Triangle (solid color only, no gradients)
 *
 *   CPU fallback operations:
 *     - Layer compositing
 *     - Box shadow
 *     - Mask rectangle (disabled in current Dave2D driver)
 *     - Mask bitmap
 *     - Gradient fills / gradient triangles
 *
 *   Reference:
 *     - _dave2d_evaluate: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:238-367
 *     - lv_draw_dave2d_init: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:76-108
 *     - lv_dave2d_init: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:541-603
 *
 * @note
 * This file is part of the Dave2D control (S-004-1, S-004-2, S-004-3) implementation.
 */

#ifndef DAVE2D_PORT_H
#define DAVE2D_PORT_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * Dave2D error codes for wrapper functions
 */
#define DAVE2D_OK               (0)     /**< Operation completed successfully */
#define DAVE2D_ERR_NOT_INIT     (-1)    /**< Dave2D is not initialized */
#define DAVE2D_ERR_PARAM        (-2)    /**< Invalid parameter */
#define DAVE2D_ERR_HW           (-3)    /**< Dave2D hardware error */

/**********************************************************************************************************************
 Global Typedef definitions
 *********************************************************************************************************************/

/** Dave2D initialization status */
typedef enum {
    DAVE2D_STATUS_NOT_INITIALIZED = 0,  /**< Not yet verified / LVGL not initialized */
    DAVE2D_STATUS_INITIALIZED,          /**< LVGL has initialized Dave2D successfully */
    DAVE2D_STATUS_NOT_AVAILABLE,        /**< LV_USE_DRAW_DAVE2D is disabled */
    DAVE2D_STATUS_ERROR,                /**< LVGL Dave2D initialization failed */
} dave2d_status_t;

/**
 * Dave2D hardware/driver information structure
 *
 * Contains version information and hardware capabilities for diagnostics.
 */
typedef struct {
    uint32_t    driver_version;         /**< Dave2D driver library version (d2_getversion) */
    const char *driver_version_str;     /**< Dave2D driver version string (d2_getversionstring) */
    uint32_t    hw_revision;            /**< Dave2D hardware revision (d2_getrevisionhw) */
    const char *hw_revision_str;        /**< Dave2D hardware revision string (d2_getrevisionstringhw) */
    bool        handle_valid;           /**< true if the LVGL Dave2D device handle is valid */
    bool        lvgl_dave2d_enabled;    /**< true if LV_USE_DRAW_DAVE2D is enabled */
} dave2d_info_t;

/**
 * LVGL-Dave2D integration information structure (S-004-3)
 *
 * Contains information about the LVGL-Dave2D draw engine integration,
 * including which draw operations are GPU-accelerated and which fall
 * back to the software renderer.
 *
 * Reference:
 *   - _dave2d_evaluate: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:238-367
 *   - lv_draw_dave2d_init: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:76-108
 */
typedef struct {
    /* Draw unit registration */
    bool        draw_unit_registered;   /**< true if Dave2D draw unit was registered with LVGL */
    uint32_t    draw_unit_id;           /**< Dave2D draw unit ID (expected: 4) */
    const char *draw_unit_name;         /**< Draw unit name string ("DAVE2D") */

    /* GPU-accelerated operations (claimed by _dave2d_evaluate) */
    bool        accel_fill;             /**< true: solid rectangle fill (no gradient) */
    bool        accel_border;           /**< true: border drawing */
    bool        accel_line;             /**< true: line drawing */
    bool        accel_arc;              /**< true: arc/circle drawing */
    bool        accel_image;            /**< true: image/BLIT (supported CFs only) */
    bool        accel_label;            /**< true: label/text glyph blit */
    bool        accel_triangle;         /**< true: solid triangle (no gradient) */

    /* CPU fallback operations (not claimed by Dave2D) */
    bool        fallback_layer;         /**< true: layer compositing -> SW */
    bool        fallback_box_shadow;    /**< true: box shadow -> SW */
    bool        fallback_mask_rect;     /**< true: mask rectangle -> SW */
    bool        fallback_mask_bitmap;   /**< true: mask bitmap -> SW */
    bool        fallback_gradient;      /**< true: gradient fills/triangles -> SW */

    /* Render buffer info */
    bool        renderbuffer_valid;     /**< true if _renderbuffer is non-NULL */
    bool        blit_renderbuffer_valid;/**< true if _blit_renderbuffer is non-NULL */

    /* Thread info */
    bool        render_thread_active;   /**< true if Dave2D render thread was created */
    bool        mutex_initialized;      /**< true if xd2Semaphore was initialized */
} dave2d_lvgl_info_t;

/**
 * Dave2D framebuffer descriptor for wrapper functions (S-004-2)
 *
 * Describes the target framebuffer for Dave2D rendering operations.
 * All wrapper functions require this descriptor to set up the Dave2D
 * framebuffer before issuing rendering commands.
 *
 * Reference: e2studio_CPU0/ra/tes/dave2d/inc/dave_driver.h:499
 *   d2_framebuffer(handle, ptr, pitch, width, height, format)
 */
typedef struct {
    void     *p_base;       /**< Pointer to framebuffer base address */
    int32_t   pitch;        /**< Line pitch in pixels (NOT bytes) */
    uint32_t  width;        /**< Framebuffer width in pixels */
    uint32_t  height;       /**< Framebuffer height in pixels */
    uint32_t  format;       /**< Dave2D pixel format (d2_mode_rgb565, d2_mode_argb8888, etc.) */
} dave2d_fb_t;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 *  Initialization / Status (S-004-1)
 * ============================================================ */

/**
 * Verify and record Dave2D initialization status
 *
 * @details This function checks whether LVGL has successfully initialized
 *          the Dave2D hardware by examining the LVGL internal handle
 *          (_d2_handle). It does NOT perform the Dave2D initialization
 *          itself -- that is done by LVGL's lv_draw_dave2d_init().
 *
 *          Call this function AFTER lv_init() has completed.
 *
 * Preconditions:
 *   - lv_init() must have been called (which triggers lv_draw_dave2d_init())
 *
 * @retval true   Dave2D is available (LVGL initialized it successfully)
 * @retval false  Dave2D is not available (disabled or init failed)
 */
bool dave2d_port_init(void);

/**
 * Deinitialize the Dave2D port layer
 *
 * @details Performs orderly shutdown of the Dave2D tracking. In the current
 *          architecture, the actual Dave2D hardware deinitialization is
 *          handled by LVGL's cleanup path. This function resets the
 *          tracking state.
 *
 *          This is provided for completeness of the init/deinit lifecycle.
 */
void dave2d_port_deinit(void);

/**
 * Get the current Dave2D initialization status
 *
 * @return Current Dave2D status
 */
dave2d_status_t dave2d_port_get_status(void);

/**
 * Check if Dave2D hardware accelerator is available for use
 *
 * @retval true   Dave2D is initialized and available
 * @retval false  Dave2D is not available
 */
bool dave2d_port_is_available(void);

/**
 * Get Dave2D hardware and driver information
 *
 * @details Fills the info structure with Dave2D version information,
 *          hardware revision, and handle validity. This information is
 *          queried from the Dave2D driver library and hardware registers.
 *
 * @param info Pointer to dave2d_info_t structure to fill
 */
void dave2d_port_get_info(dave2d_info_t *info);

/* ============================================================
 *  LVGL-Dave2D Integration (S-004-3)
 * ============================================================ */

/**
 * Get LVGL-Dave2D integration information
 *
 * @details Fills the info structure with details about the LVGL-Dave2D
 *          draw engine integration, including:
 *            - Draw unit registration status (ID, name)
 *            - GPU-accelerated operations list
 *            - CPU fallback operations list
 *            - Render buffer and thread status
 *            - Mutex status
 *
 *          This function queries the LVGL internal Dave2D draw unit state
 *          to provide a comprehensive view of the integration.
 *
 *          Call this function AFTER lv_init() and dave2d_port_init().
 *
 * Reference:
 *   - lv_draw_dave2d_init: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:76-108
 *   - _dave2d_evaluate: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:238-367
 *
 * @param info  Pointer to dave2d_lvgl_info_t structure to fill
 */
void dave2d_port_get_lvgl_info(dave2d_lvgl_info_t *info);

/**
 * Print a summary of the LVGL-Dave2D integration to the console
 *
 * @details Outputs a human-readable summary of the Dave2D-LVGL integration
 *          status, including which draw operations use GPU acceleration and
 *          which fall back to the software renderer. Useful for runtime
 *          verification that the GPU is properly connected to the LVGL
 *          rendering pipeline.
 *
 *          This is called from lvgl_thread_entry.c after initialization
 *          and from the NT-Shell "dave2d integration" sub-command.
 */
void dave2d_port_print_lvgl_integration(void);

/* ============================================================
 *  Drawing Wrapper Functions (S-004-2)
 * ============================================================ */

/**
 * Fill a rectangle with a solid color using Dave2D hardware acceleration
 *
 * @details Draws a filled rectangle at the specified position in the target
 *          framebuffer. Uses d2_renderbox() internally.
 *
 *          Thread safety: Acquires the LVGL Dave2D mutex internally.
 *          Rendering: Immediate mode (execute + flush before return).
 *
 * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d_fill.c:136-139
 *
 * @param fb       Target framebuffer descriptor
 * @param x        X coordinate of the top-left corner
 * @param y        Y coordinate of the top-left corner
 * @param w        Width of the rectangle in pixels
 * @param h        Height of the rectangle in pixels
 * @param color    Fill color in d2_color format (0x00RRGGBB)
 * @param alpha    Opacity (0=transparent, 255=opaque)
 * @return DAVE2D_OK on success, DAVE2D_ERR_* on error
 */
int dave2d_port_fill_rect(const dave2d_fb_t *fb,
                          int32_t x, int32_t y,
                          int32_t w, int32_t h,
                          uint32_t color, uint8_t alpha);

/**
 * Draw a rectangle outline (border) using Dave2D hardware acceleration
 *
 * @details Draws a rectangular outline (non-filled) at the specified position
 *          by rendering four thin rectangles (top, bottom, left, right edges)
 *          using d2_renderbox(). This approach avoids the overhead of four
 *          separate execute/flush cycles by batching the four boxes into one
 *          render buffer.
 *
 * @param fb          Target framebuffer descriptor
 * @param x           X coordinate of the top-left corner
 * @param y           Y coordinate of the top-left corner
 * @param w           Width of the rectangle in pixels
 * @param h           Height of the rectangle in pixels
 * @param color       Border color in d2_color format (0x00RRGGBB)
 * @param alpha       Opacity (0=transparent, 255=opaque)
 * @param border_w    Border width in pixels
 * @return DAVE2D_OK on success, DAVE2D_ERR_* on error
 */
int dave2d_port_draw_rect(const dave2d_fb_t *fb,
                          int32_t x, int32_t y,
                          int32_t w, int32_t h,
                          uint32_t color, uint8_t alpha,
                          int32_t border_w);

/**
 * Draw a line using Dave2D hardware acceleration
 *
 * @details Draws a line from (x1,y1) to (x2,y2) with the specified width
 *          using d2_renderline(). Line cap style is set to butt (flat ends).
 *
 * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d_line.c:77-78
 *
 * @param fb       Target framebuffer descriptor
 * @param x1       Start X coordinate
 * @param y1       Start Y coordinate
 * @param x2       End X coordinate
 * @param y2       End Y coordinate
 * @param color    Line color in d2_color format (0x00RRGGBB)
 * @param alpha    Opacity (0=transparent, 255=opaque)
 * @param width    Line width in pixels (minimum 1)
 * @return DAVE2D_OK on success, DAVE2D_ERR_* on error
 */
int dave2d_port_draw_line(const dave2d_fb_t *fb,
                          int32_t x1, int32_t y1,
                          int32_t x2, int32_t y2,
                          uint32_t color, uint8_t alpha,
                          int32_t width);

/**
 * Copy a rectangular region from a source buffer to the framebuffer (BLIT)
 *
 * @details Performs a hardware-accelerated block image transfer (BLIT) from
 *          a source buffer to the target framebuffer. Uses d2_setblitsrc()
 *          and d2_blitcopy() internally.
 *
 *          This is the basic 1:1 copy without scaling. For scaled copies,
 *          use dave2d_port_blit_scaled().
 *
 * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:186-193
 *
 * @param fb          Target framebuffer descriptor
 * @param p_src       Pointer to source image data
 * @param src_pitch   Source image pitch in pixels (NOT bytes)
 * @param src_w       Source image width in pixels
 * @param src_h       Source image height in pixels
 * @param src_format  Source pixel format (d2_mode_rgb565, d2_mode_argb8888, etc.)
 * @param dst_x       Destination X coordinate in the target framebuffer
 * @param dst_y       Destination Y coordinate in the target framebuffer
 * @return DAVE2D_OK on success, DAVE2D_ERR_* on error
 */
int dave2d_port_blit(const dave2d_fb_t *fb,
                     const void *p_src, int32_t src_pitch,
                     int32_t src_w, int32_t src_h, uint32_t src_format,
                     int32_t dst_x, int32_t dst_y);

/**
 * Copy and scale a rectangular region from source to framebuffer (scaled BLIT)
 *
 * @details Performs a hardware-accelerated block image transfer with scaling.
 *          The source image is scaled to fit the destination area specified by
 *          (dst_w, dst_h). Uses d2_setblitsrc() and d2_blitcopy() internally.
 *
 * @param fb          Target framebuffer descriptor
 * @param p_src       Pointer to source image data
 * @param src_pitch   Source image pitch in pixels (NOT bytes)
 * @param src_w       Source image width in pixels
 * @param src_h       Source image height in pixels
 * @param src_format  Source pixel format (d2_mode_rgb565, d2_mode_argb8888, etc.)
 * @param dst_x       Destination X coordinate in the target framebuffer
 * @param dst_y       Destination Y coordinate in the target framebuffer
 * @param dst_w       Destination width in pixels (scaled output)
 * @param dst_h       Destination height in pixels (scaled output)
 * @return DAVE2D_OK on success, DAVE2D_ERR_* on error
 */
int dave2d_port_blit_scaled(const dave2d_fb_t *fb,
                            const void *p_src, int32_t src_pitch,
                            int32_t src_w, int32_t src_h, uint32_t src_format,
                            int32_t dst_x, int32_t dst_y,
                            int32_t dst_w, int32_t dst_h);

/**
 * Set the alpha blending mode for subsequent Dave2D operations
 *
 * @details Configures the Dave2D hardware blending mode. This affects how
 *          rendered pixels are combined with existing framebuffer contents.
 *
 *          Common modes:
 *            - Normal blend: src=d2_bm_alpha, dst=d2_bm_one_minus_alpha
 *            - Opaque (no blend): src=d2_bm_one, dst=d2_bm_zero
 *            - Additive: src=d2_bm_alpha, dst=d2_bm_one
 *
 * Reference: e2studio_CPU0/ra/tes/dave2d/inc/dave_driver.h:537
 *
 * @param src_factor  Source blend factor (d2_bm_zero/one/alpha/one_minus_alpha)
 * @param dst_factor  Destination blend factor
 * @return DAVE2D_OK on success, DAVE2D_ERR_* on error
 */
int dave2d_port_set_blend_mode(uint32_t src_factor, uint32_t dst_factor);

/* ============================================================
 *  Rendering Pipeline Control (S-004-2)
 * ============================================================ */

/**
 * Execute the render buffer and wait for completion
 *
 * @details Executes all pending Dave2D render commands in the blit render
 *          buffer and blocks until the hardware finishes processing.
 *          This is equivalent to:
 *            d2_executerenderbuffer(handle, blit_rb, 0)
 *            d2_flushframe(handle)
 *
 *          The drawing wrapper functions call this internally, so this
 *          function is only needed when building custom render sequences.
 *
 * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:619-623
 *
 * @return DAVE2D_OK on success, DAVE2D_ERR_* on error
 */
int dave2d_port_execute_and_flush(void);

/* ============================================================
 *  Color Conversion Utilities (S-004-2)
 * ============================================================ */

/**
 * Convert RGB565 color to Dave2D color format (0x00RRGGBB)
 *
 * @details Unpacks a 16-bit RGB565 value into Dave2D's 32-bit color format.
 *          RGB565 format: RRRR RGGG GGGB BBBB
 *          Dave2D format: 0x00RRGGBB (8-bit per channel)
 *
 * @param rgb565  RGB565 color value
 * @return Dave2D color (0x00RRGGBB)
 */
uint32_t dave2d_port_rgb565_to_d2color(uint16_t rgb565);

/**
 * Convert Dave2D color format (0x00RRGGBB) to RGB565
 *
 * @details Packs Dave2D's 32-bit color into a 16-bit RGB565 value.
 *
 * @param d2color  Dave2D color (0x00RRGGBB)
 * @return RGB565 color value
 */
uint16_t dave2d_port_d2color_to_rgb565(uint32_t d2color);

/**
 * Convert an RGB888 triplet to Dave2D color format
 *
 * @param r  Red component (0-255)
 * @param g  Green component (0-255)
 * @param b  Blue component (0-255)
 * @return Dave2D color (0x00RRGGBB)
 */
uint32_t dave2d_port_rgb888_to_d2color(uint8_t r, uint8_t g, uint8_t b);

/**
 * Convert a YUV (YCbCr BT.601) pixel to RGB565
 *
 * @details Converts a single YUV pixel to RGB565 using the BT.601 standard
 *          conversion formula. This is useful for camera image format
 *          conversion (e.g., YUYV -> RGB565 for display).
 *
 *          BT.601 conversion:
 *            R = Y + 1.402 * (V - 128)
 *            G = Y - 0.344 * (U - 128) - 0.714 * (V - 128)
 *            B = Y + 1.772 * (U - 128)
 *
 * @param y_val  Y (luminance) component (0-255)
 * @param u_val  U (Cb) chrominance component (0-255)
 * @param v_val  V (Cr) chrominance component (0-255)
 * @return RGB565 color value
 */
uint16_t dave2d_port_yuv_to_rgb565(uint8_t y_val, uint8_t u_val, uint8_t v_val);

/* ============================================================
 *  Framebuffer Helper (S-004-2)
 * ============================================================ */

/**
 * Initialize a dave2d_fb_t descriptor for the GLCDC framebuffer
 *
 * @details Fills the framebuffer descriptor with the parameters of
 *          the specified GLCDC framebuffer (fb_background[index]).
 *          This provides a convenient way to target the display
 *          framebuffer with Dave2D wrapper functions.
 *
 * @param fb     Pointer to dave2d_fb_t structure to fill
 * @param index  Framebuffer index (0 or 1)
 * @retval true  Descriptor initialized successfully
 * @retval false Invalid index or GLCDC layer not enabled
 */
bool dave2d_port_fb_from_glcdc(dave2d_fb_t *fb, uint32_t index);

/* ============================================================
 *  NT-Shell Command (S-004-1, S-004-2, S-004-3)
 * ============================================================ */

/**
 * NT-Shell command handler for Dave2D diagnostics and testing
 *
 * @details Registered as the "dave2d" command in usrcmd.c.
 *          Sub-commands:
 *            dave2d status          - Show Dave2D initialization state and HW info
 *            dave2d integration     - Show LVGL-Dave2D integration status (S-004-3)
 *            dave2d test rect       - Draw test rectangles on the display
 *            dave2d test line       - Draw test lines on the display
 *            dave2d test blit       - Draw a test BLIT pattern
 *            dave2d test alpha      - Draw alpha blending test
 *            dave2d test all        - Run all drawing tests
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return CMD_OK on success, CMD_ERR_* on error
 */
int usrcmd_dave2d(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* DAVE2D_PORT_H */
