/**
 * @file camera_display.h
 * @brief Camera frame buffer to GLCDC framebuffer direct transfer (F-001-8)
 * @details
 * Bridges the camera capture subsystem (camera_framebuffer) and the GLCDC
 * display by writing camera frames directly to the GLCDC framebuffers,
 * bypassing the LVGL rendering pipeline entirely (Method C: Direct mode).
 *
 * Why Method C:
 *   Method B (lv_image) achieved only 4 FPS at 99% CPU because LVGL's
 *   rendering pipeline (Dave2D image blit of 1024x560 pixels) was the
 *   bottleneck. Method C eliminates this by writing directly to the GLCDC
 *   framebuffers, reducing the work to a single row-by-row memcpy.
 *
 * Architecture:
 *   An LVGL timer polls camera_framebuffer_get_latest() every 16ms.
 *   When a new frame is available:
 *     1. D-cache invalidation on the VIN DMA source buffer
 *     2. Letterbox copy to BOTH GLCDC framebuffers (double-buffered)
 *        Camera 768x450 -> centered in display 1024x560 area (below 40px status bar)
 *     3. No LVGL invalidation needed - the GLCDC displays the data directly
 *
 * GLCDC framebuffer layout:
 *   +--------------------------------------------------+  Y=0
 *   |  Status Bar (40px, managed by LVGL)               |
 *   +--------------------------------------------------+  Y=40
 *   |  Black border (55 rows)                            |
 *   +---+------------------------------------------+---+  Y=95
 *   |128|  Camera Image (768 x 450, RGB565)         |128|
 *   | px|  written directly by this module           | px|
 *   +---+------------------------------------------+---+  Y=545
 *   |  Black border (55 rows)                            |
 *   +--------------------------------------------------+  Y=600
 *
 * Double-buffering:
 *   The GLCDC uses Vsync-synchronized double buffering (fb_background[0/1]).
 *   This module writes to BOTH buffers on each frame to ensure both always
 *   contain the latest camera image. LVGL manages only the status bar area
 *   and does not touch the camera region.
 *
 * Thread safety:
 *   The LVGL timer callback runs in the LVGL thread context. The GLCDC
 *   framebuffers are in non-cached SDRAM (.sdram_noinit_nocache), so no
 *   cache maintenance is needed on the write side.
 *
 * Reference:
 *   - Issue #9 (F-001-8)
 *   - camera_framebuffer.h (producer: VIN ISR -> set_latest)
 *   - port/glcdc_port.h (GLCDC framebuffer info)
 *   - reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/display_thread_entry.c
 *
 * @note This file is part of the camera display implementation (F-001-8).
 */

#ifndef CAMERA_DISPLAY_H
#define CAMERA_DISPLAY_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * LVGL timer period for camera display update polling (milliseconds)
 *
 * 16ms corresponds to ~60Hz polling rate, matching the GLCDC refresh rate
 * (~59Hz). Since the camera captures at ~30fps, approximately every other
 * poll cycle will find a new frame to transfer.
 */
#define CAMERA_DISPLAY_TIMER_PERIOD_MS  (16)

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize camera display transfer (Method C: Direct framebuffer write)
 *
 * @details Retrieves GLCDC framebuffer addresses and creates an LVGL timer
 *          for periodic camera frame polling. On each new frame, the camera
 *          data is written directly to both GLCDC framebuffers.
 *
 *          Must be called after glcdc_port_init() (framebuffers available)
 *          and after ui_main_screen_create() (screen layout established).
 *          Must be called from the LVGL thread.
 */
void camera_display_init(void);

/**
 * Transfer a camera frame directly to the GLCDC framebuffers
 *
 * @details Writes the camera frame data directly to both GLCDC double
 *          buffers with letterbox centering, bypassing LVGL completely.
 *
 *          The source frame (768x450) is centered within the camera area
 *          (1024x560, starting at Y=40 below the status bar).
 *
 *          This function is called automatically by the LVGL timer.
 *          It can also be called manually for testing or direct injection.
 *
 *          Must be called from the LVGL thread or within lv_lock()/lv_unlock().
 *
 * @param frame_buf Camera frame buffer pointer (RGB565 format)
 * @param width     Frame width in pixels
 * @param height    Frame height in pixels
 */
void camera_display_update(const uint16_t *frame_buf,
                           uint16_t width, uint16_t height);

/**
 * Get the display-side frames per second
 *
 * @return Display FPS (0 if no frames have been transferred)
 */
uint32_t camera_display_get_fps(void);

/**
 * Get the total number of frames transferred to the display
 *
 * @return Update count (monotonically increasing since init)
 */
uint32_t camera_display_get_update_count(void);

/**
 * Check if camera display transfer is active
 *
 * @return true if camera display is active (receiving frames)
 */
bool camera_display_is_active(void);

/**
 * NT-Shell "camera display" diagnostic command handler
 */
void camera_display_cmd_status(void);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_DISPLAY_H */
