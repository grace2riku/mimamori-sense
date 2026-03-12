/**
 * @file fall_detection_screen.h
 * @brief Fall detection overlay UI for LCD display (F-003-10)
 * @details
 * Provides LVGL overlay widgets for displaying fall detection results
 * on top of the camera image. Uses LVGL objects (labels, rectangles)
 * rather than raw D/AVE 2D calls to integrate cleanly with the
 * existing LVGL-based display pipeline.
 *
 * Overlay elements:
 *   - Bounding boxes: LVGL objects with colored borders (green/yellow/red)
 *   - Status text: LVGL label showing fall detection state
 *   - Info panel: LVGL labels showing inference time, detections, FPS
 *
 * Coordinate transformation:
 *   Detection results are in camera coordinates (320x240).
 *   These are transformed to LCD display coordinates based on the
 *   camera image position and scale within the display area.
 *
 *   Camera image (768x450) is centered in camera area (1024x560):
 *     Display X = cam_offset_x + (det_x / 320.0) * 768
 *     Display Y = cam_offset_y + (det_y / 240.0) * 450
 *   where cam_offset_x = 128, cam_offset_y = 95 (within camera area)
 *   and camera area starts at Y=40 (below status bar)
 *
 * Thread safety:
 *   All functions must be called from the LVGL thread or within
 *   lv_lock()/lv_unlock(). The update function reads global detection
 *   results (g_fall_detection_results[], g_fall_detection_count) which
 *   are written by the AI inference thread. These reads are safe because:
 *     - g_fall_detection_count is volatile uint32_t (atomic read on CM85)
 *     - Results are only read after AI_INFERENCE_RESULT_UPDATED event
 *
 * Reference:
 *   - Issue #27 (F-003-10)
 *   - face_detection_screen_mipi.c (bounding box + text overlay pattern)
 *   - camera_display.c (camera image positioning: X=128, Y=95)
 */

#ifndef FALL_DETECTION_SCREEN_H
#define FALL_DETECTION_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

#include "lvgl.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * @name Bounding Box Colors (RGB888)
 * @{
 */
#define FALL_SCREEN_COLOR_NORMAL_R      (0x00)  /**< Normal: Green */
#define FALL_SCREEN_COLOR_NORMAL_G      (0xC8)
#define FALL_SCREEN_COLOR_NORMAL_B      (0x00)

#define FALL_SCREEN_COLOR_SUSPECTED_R   (0xFF)  /**< Suspected: Yellow */
#define FALL_SCREEN_COLOR_SUSPECTED_G   (0xD7)
#define FALL_SCREEN_COLOR_SUSPECTED_B   (0x00)

#define FALL_SCREEN_COLOR_CONFIRMED_R   (0xFF)  /**< Confirmed: Red */
#define FALL_SCREEN_COLOR_CONFIRMED_G   (0x00)
#define FALL_SCREEN_COLOR_CONFIRMED_B   (0x00)

#define FALL_SCREEN_COLOR_COOLDOWN_R    (0xFF)  /**< Cooldown: Orange */
#define FALL_SCREEN_COLOR_COOLDOWN_G    (0x80)
#define FALL_SCREEN_COLOR_COOLDOWN_B    (0x00)
/** @} */

/**
 * @name Bounding Box Border Width
 * @{
 */
#define FALL_SCREEN_BORDER_NORMAL       (2)     /**< Normal border width (px) */
#define FALL_SCREEN_BORDER_SUSPECTED    (3)     /**< Suspected border width (px) */
#define FALL_SCREEN_BORDER_CONFIRMED    (4)     /**< Confirmed border width - thick (px) */
/** @} */

/**
 * Maximum number of bounding box overlay objects.
 * Matches AI_MAX_DETECTION_NUM from ai_application_config.h.
 */
#define FALL_SCREEN_MAX_BBOX            (5)

/**
 * @name Camera-to-Display Coordinate Transform Constants
 *
 * Detection coordinates are in camera space (320x240).
 * Camera image (768x450) is centered in the display camera area (1024x560).
 *
 * Reference: camera_display.c camera_display_init() positioning
 * @{
 */
#define FALL_SCREEN_CAM_SRC_W           (320)   /**< Post-process output width */
#define FALL_SCREEN_CAM_SRC_H           (240)   /**< Post-process output height */
#define FALL_SCREEN_CAM_DST_W           (768)   /**< Camera display width */
#define FALL_SCREEN_CAM_DST_H           (450)   /**< Camera display height */
#define FALL_SCREEN_CAM_OFFSET_X        (128)   /**< Camera X offset in display */
#define FALL_SCREEN_CAM_OFFSET_Y        (95)    /**< Camera Y offset in camera area */
/** @} */

/**
 * Info panel text buffer size
 */
#define FALL_SCREEN_INFO_BUF_SIZE       (32)

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Initialize fall detection screen overlay widgets.
 *
 * Creates LVGL overlay objects (bounding boxes, status label, info panel)
 * on the main screen. All objects start hidden and are shown/updated
 * by fall_detection_screen_update().
 *
 * Must be called after ui_main_screen_create() and camera_display_init()
 * from the LVGL thread.
 *
 * @param parent  Parent LVGL object (typically the main screen object)
 */
void fall_detection_screen_init(lv_obj_t *parent);

/**
 * Update the fall detection overlay with current detection results.
 *
 * Reads the global detection results (g_fall_detection_results[],
 * g_fall_detection_count) and fall detection state, then updates:
 *   - Bounding box positions, sizes, and colors
 *   - Status text (Monitoring / Fall Suspected / FALL DETECTED)
 *   - Info panel (inference time, detected count, state, FPS)
 *
 * Should be called from the LVGL timer callback (camera_display_timer_cb)
 * after AI_INFERENCE_RESULT_UPDATED is detected, or periodically.
 */
void fall_detection_screen_update(void);

/**
 * Check if the fall detection screen overlay has been initialized.
 *
 * @return true if fall_detection_screen_init() has been called
 */
bool fall_detection_screen_is_initialized(void);

/**
 * Get the display-side overlay enabled/disabled state.
 *
 * @return true if overlay drawing is enabled
 */
bool fall_detection_screen_is_enabled(void);

/**
 * Enable or disable the fall detection overlay.
 *
 * When disabled, all overlay widgets are hidden but not destroyed.
 * When re-enabled, they become visible again on the next update.
 *
 * @param enable  true to enable overlay, false to disable
 */
void fall_detection_screen_set_enabled(bool enable);

/**
 * NT-Shell diagnostic: print overlay status to console.
 */
void fall_detection_screen_cmd_status(void);

#ifdef __cplusplus
}
#endif

#endif /* FALL_DETECTION_SCREEN_H */
