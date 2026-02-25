/**
 * @file ui_main_screen.h
 * @brief Main screen UI for camera image display
 * @details
 * Implements the primary screen layout for the mimamori-sense home monitoring
 * device. The screen is divided into two areas:
 *
 *   1. Status Bar (top 40px):  System status, date/time, settings button
 *   2. Camera Area (remaining 1024x560): Camera image display area
 *
 * Screen Layout:
 * +--------------------------------------------------+
 * |  Status Bar (height: 40px)                        |
 * |  [Status Label]  [DateTime]         [Settings Btn]|
 * +--------------------------------------------------+
 * |                                                    |
 * |                                                    |
 * |           Camera Image Display Area                |
 * |           (1024 x 560, RGB565)                     |
 * |                                                    |
 * |                                                    |
 * |                                                    |
 * +--------------------------------------------------+
 *
 * Camera Image Update Architecture:
 *   The camera area uses an lv_image widget backed by a static
 *   lv_image_dsc_t descriptor. The descriptor points to a pixel buffer
 *   in SDRAM (.sdram section). To update the camera frame:
 *     1. Write new pixel data into the SDRAM buffer
 *     2. Call ui_main_screen_invalidate_camera() to trigger LVGL redraw
 *   This approach avoids the high RAM cost of lv_canvas (1024*560*2 = 1.1MB
 *   in internal SRAM) by using SDRAM for the pixel buffer.
 *
 * Test Pattern:
 *   When the camera subsystem is not yet connected (F-002), a color bar
 *   test pattern is drawn into the camera buffer at initialization time.
 *   This validates the full LVGL rendering pipeline including:
 *     - lv_image widget display
 *     - RGB565 color format handling
 *     - SDRAM buffer access from LVGL/Dave2D
 *     - Screen layout correctness
 *
 * Thread Safety:
 *   All functions in this module must be called from the LVGL thread
 *   (lvgl_thread) or within an lv_lock()/lv_unlock() critical section.
 *
 * Reference:
 *   - Issue #8 (F-001-7): Camera image display main screen
 *   - GLCDC display: e2studio_CPU0/src/port/glcdc_port.h
 *   - SDRAM sections: e2studio_CPU0/src/port/sdram_port.h
 *
 * @note This file is part of the LVGL UI implementation (F-001-7).
 */

#ifndef UI_MAIN_SCREEN_H
#define UI_MAIN_SCREEN_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include "lvgl.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Display resolution */
#define UI_DISPLAY_WIDTH        (1024)
#define UI_DISPLAY_HEIGHT       (600)

/** Status bar dimensions */
#define UI_STATUS_BAR_HEIGHT    (40)

/** Camera area dimensions (display height minus status bar) */
#define UI_CAMERA_WIDTH         (UI_DISPLAY_WIDTH)
#define UI_CAMERA_HEIGHT        (UI_DISPLAY_HEIGHT - UI_STATUS_BAR_HEIGHT)

/** Camera image color format: RGB565 (16 bits per pixel) */
#define UI_CAMERA_BPP           (16)

/**
 * Camera image buffer size in bytes
 *
 * Calculation: 1024 * 560 * 2 = 1,146,880 bytes (~1.1MB)
 * This buffer is placed in SDRAM (.sdram section) to avoid exhausting
 * internal SRAM (~1MB total shared with FreeRTOS stacks and LVGL heap).
 */
#define UI_CAMERA_BUF_SIZE      (UI_CAMERA_WIDTH * UI_CAMERA_HEIGHT * (UI_CAMERA_BPP / 8))

/**
 * Camera image stride in bytes (bytes per row)
 *
 * For RGB565: 1024 pixels * 2 bytes/pixel = 2048 bytes per row.
 * This matches the GLCDC stride (DISPLAY_BUFFER_STRIDE_BYTES_INPUT0 = 2048).
 */
#define UI_CAMERA_STRIDE        (UI_CAMERA_WIDTH * (UI_CAMERA_BPP / 8))

/** Status bar background color (dark blue-gray) */
#define UI_STATUS_BAR_COLOR     lv_color_make(0x1A, 0x23, 0x2F)

/** Status bar text color (white) */
#define UI_STATUS_BAR_TEXT_COLOR lv_color_white()

/** Camera area default background color (dark gray, shown before first frame) */
#define UI_CAMERA_BG_COLOR      lv_color_make(0x20, 0x20, 0x20)

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create and initialize the main screen
 *
 * @details Creates the root screen object with:
 *   - Status bar (40px height) at the top with:
 *     - Status label (left-aligned, shows monitoring state)
 *     - Date/time label (center)
 *     - Settings button (right-aligned, gear icon placeholder)
 *   - Camera image area (1024x560) filling the remaining space
 *     - Backed by an SDRAM pixel buffer
 *     - Initialized with a color bar test pattern
 *
 * After creation, the screen is loaded as the active screen
 * via lv_screen_load().
 *
 * Must be called from the LVGL thread after lv_init() and
 * glcdc_port_init() have completed.
 *
 * @return Pointer to the created screen object, or NULL on failure
 */
lv_obj_t* ui_main_screen_create(void);

/**
 * Get the camera image widget
 *
 * @details Returns a pointer to the lv_image widget used for camera
 *          frame display. External modules can use this to:
 *          - Get the underlying pixel buffer pointer
 *          - Trigger invalidation after writing new frame data
 *
 * @return Pointer to the camera lv_image widget, or NULL if not created
 */
lv_obj_t* ui_main_screen_get_camera_image(void);

/**
 * Get the camera image pixel buffer
 *
 * @details Returns a pointer to the raw RGB565 pixel buffer in SDRAM
 *          that backs the camera image widget. Camera frame data should
 *          be written directly to this buffer.
 *
 * Buffer layout:
 *   - Format: RGB565 (16 bits per pixel, little-endian)
 *   - Width:  1024 pixels
 *   - Height: 560 pixels
 *   - Stride: 2048 bytes per row
 *   - Total:  1,146,880 bytes
 *
 * After writing new frame data, call ui_main_screen_invalidate_camera()
 * to trigger LVGL to redraw the image.
 *
 * @return Pointer to the SDRAM pixel buffer, or NULL if not initialized
 */
uint8_t* ui_main_screen_get_camera_buffer(void);

/**
 * Invalidate the camera image area to trigger LVGL redraw
 *
 * @details Marks the camera image widget as dirty so that LVGL will
 *          redraw it on the next lv_timer_handler() cycle. Call this
 *          after writing new pixel data to the camera buffer.
 *
 * Must be called from the LVGL thread or within lv_lock()/lv_unlock().
 */
void ui_main_screen_invalidate_camera(void);

/**
 * Update the status bar text
 *
 * @details Sets the status label text in the status bar.
 *          Typical values: "Monitoring", "Detecting", "Idle", "Error"
 *
 * Must be called from the LVGL thread or within lv_lock()/lv_unlock().
 *
 * @param text  Status text string (must remain valid; LVGL copies it)
 */
void ui_main_screen_set_status(const char *text);

/**
 * Update the date/time label text
 *
 * @details Sets the date/time label in the center of the status bar.
 *
 * Must be called from the LVGL thread or within lv_lock()/lv_unlock().
 *
 * @param text  Date/time text string (e.g., "2026-02-25 12:34")
 */
void ui_main_screen_set_datetime(const char *text);

/**
 * Check if the main screen has been created
 *
 * @return true if ui_main_screen_create() has been called successfully
 */
bool ui_main_screen_is_created(void);

/**
 * Draw a color bar test pattern into the camera buffer
 *
 * @details Fills the camera pixel buffer with 8 vertical color bars:
 *   Red, Green, Blue, Yellow, Cyan, Magenta, White, Black
 *
 * Each bar is (UI_CAMERA_WIDTH / 8) = 128 pixels wide.
 * This function is called automatically during ui_main_screen_create()
 * but can also be called explicitly for testing.
 *
 * Must be called from the LVGL thread or within lv_lock()/lv_unlock().
 */
void ui_main_screen_draw_test_pattern(void);

/**
 * Get the current status bar text
 *
 * @return Pointer to the current status text, or NULL if not created
 */
const char* ui_main_screen_get_status_text(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_MAIN_SCREEN_H */
