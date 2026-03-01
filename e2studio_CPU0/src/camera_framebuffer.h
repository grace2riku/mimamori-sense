/**
 * @file camera_framebuffer.h
 * @brief Camera frame buffer management API (F-002-6)
 * @details
 * Provides triple-buffer management for VIN camera capture data and
 * thread-safe buffer hand-off between the camera thread (producer, ISR context)
 * and the display/LVGL thread (consumer).
 *
 * The frame buffers themselves are allocated by the FSP code generator as
 * vin_image_buffer_1/2/3 in SDRAM .sdram_noinit section (ra_gen/common_data.c).
 * This module does NOT define its own buffers; it wraps the FSP-generated
 * buffers with management state, FPS tracking, and a thread-safe latest-frame
 * pointer.
 *
 * Buffer flow:
 *   VIN HW (triple buffer round-robin)
 *     -> vin0_callback (ISR) -> vin_port_callback
 *       -> camera_framebuffer_set_latest(p_buffer)  [ISR-safe]
 *   Display/LVGL thread
 *     -> camera_framebuffer_get_latest()             [returns latest or NULL]
 *
 * Reference:
 *   - reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/display_thread_entry.c:157-160
 *     (display_next_buffer_set: volatile pointer hand-off from VIN callback to display)
 *   - reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:921
 *     (vin0_callback calling display_next_buffer_set)
 *   - e2studio_CPU0/ra_gen/common_data.c:224-226 (vin_image_buffer_1/2/3 allocation)
 *   - e2studio_CPU0/ra_gen/common_data.c:264-265 (VIN config referencing the buffers)
 */

#ifndef CAMERA_FRAMEBUFFER_H
#define CAMERA_FRAMEBUFFER_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * Frame buffer dimensions
 *
 * These match the VIN preclip / stride settings configured in FSP
 * (configuration.xml -> VIN module properties).
 *
 * Reference: e2studio_CPU0/ra_gen/common_data.h:57-65
 */
#define CAMERA_FRAME_WIDTH      (768)
#define CAMERA_FRAME_HEIGHT     (450)
#define CAMERA_FRAME_BPP        (2)     /* RGB565 / YUV422: 2 bytes per pixel */
#define CAMERA_FRAME_SIZE       (CAMERA_FRAME_WIDTH * CAMERA_FRAME_HEIGHT * CAMERA_FRAME_BPP)

/** Number of capture buffers (triple buffering) */
#define CAMERA_FRAME_NUM_BUFFERS (3)

/**********************************************************************************************************************
 Global Typedef definitions
 *********************************************************************************************************************/

/**
 * Frame buffer status information (for diagnostics / NT-Shell)
 */
typedef struct {
    bool        initialized;            /**< true after camera_framebuffer_init() */
    uint32_t    frame_count;            /**< Total frame_complete count */
    uint32_t    fps;                    /**< Measured frames per second (updated every 1s) */
    void       *p_latest;              /**< Pointer to the latest completed frame (or NULL) */
    bool        new_frame_available;    /**< true if a new frame has not been consumed */
    uint32_t    buffer_addr[CAMERA_FRAME_NUM_BUFFERS]; /**< FSP-generated buffer addresses */
} camera_framebuffer_info_t;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the frame buffer management state
 *
 * @details Resets all internal state (frame counter, FPS tracker, latest pointer).
 *          Must be called once before capture starts, typically from camera_thread_entry
 *          before R_VIN_CaptureStart().
 *
 *          The actual frame buffer memory (vin_image_buffer_1/2/3) is allocated by FSP
 *          and does not need explicit allocation here.
 */
void camera_framebuffer_init(void);

/**
 * Set the latest completed frame buffer (called from ISR context)
 *
 * @details Called from vin_port_callback() on frame_complete interrupt.
 *          Atomically updates the latest-frame pointer and increments the
 *          frame counter. Must be safe to call from ISR context (no blocking).
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/display_thread_entry.c:157-160
 *            reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:921
 *
 * @param p_buffer Pointer to the completed frame buffer (from VIN callback p_args->p_buffer)
 */
void camera_framebuffer_set_latest(void *p_buffer);

/**
 * Get the latest completed frame buffer (called from display thread)
 *
 * @details Returns the most recently completed frame buffer pointer and
 *          clears the new-frame flag. Returns NULL if no new frame is
 *          available since the last call.
 *
 *          Thread-safe: uses volatile pointer and flag for lock-free
 *          single-producer (ISR) / single-consumer (display thread) access.
 *
 * @return Pointer to the latest frame buffer, or NULL if no new frame
 */
void *camera_framebuffer_get_latest(void);

/**
 * Get the three frame buffer addresses (for VIN initialization / diagnostics)
 *
 * @details Returns the addresses of the three FSP-generated VIN capture buffers
 *          (vin_image_buffer_1/2/3). These are the buffers registered in the
 *          VIN extended configuration for triple-buffer round-robin capture.
 *
 * @param pp_buf1 Output: pointer to buffer 1
 * @param pp_buf2 Output: pointer to buffer 2
 * @param pp_buf3 Output: pointer to buffer 3
 */
void camera_framebuffer_get_buffers(void **pp_buf1, void **pp_buf2, void **pp_buf3);

/**
 * Get the total frame complete count since initialization
 *
 * @return Frame count (monotonically increasing)
 */
uint32_t camera_framebuffer_get_frame_count(void);

/**
 * Get the measured frames per second
 *
 * @details Returns the FPS value computed over the last 1-second interval.
 *          Updated internally by camera_framebuffer_set_latest() using
 *          a sliding window approach.
 *
 * @return Frames per second (0 if no frames have been captured)
 */
uint32_t camera_framebuffer_get_fps(void);

/**
 * Get comprehensive frame buffer status information (for diagnostics)
 *
 * @param info Pointer to camera_framebuffer_info_t structure to fill
 */
void camera_framebuffer_get_info(camera_framebuffer_info_t *info);

/**
 * NT-Shell "camera fb" sub-command handler
 *
 * @details Displays frame buffer status, addresses, frame count, FPS,
 *          and latest frame pointer information.
 */
void camera_framebuffer_cmd_status(void);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_FRAMEBUFFER_H */
