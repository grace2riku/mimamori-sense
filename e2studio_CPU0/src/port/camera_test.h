/**
 * @file camera_test.h
 * @brief MIPI CSI-2 camera integration test and verification (S-003-4)
 * @details
 * Provides end-to-end camera pipeline test functions for verifying the
 * MIPI D-PHY -> CSI-2 -> VIN capture chain with the OV5640 camera module.
 *
 * Test Functions:
 *   - Single frame capture with data validation
 *   - Continuous capture with FPS measurement and frame drop detection
 *   - Camera image direct display on GLCDC (bypass LVGL)
 *   - Streaming mode with continuous LCD update
 *
 * Data Flow Under Test:
 *   OV5640 (MIPI CSI-2) -> D-PHY (S-003-1) -> CSI-2 Rx (S-003-2)
 *   -> VIN (S-003-3) -> SDRAM Capture Buffer -> GLCDC Framebuffer (S-002)
 *
 * Camera-to-Display Mapping:
 *   The VIN capture resolution is 768x450 (YUV422/RGB565, 2 bytes/pixel).
 *   The GLCDC display resolution is 1024x600 (RGB565, 2 bytes/pixel).
 *   The camera image is placed at the top-left of the LCD framebuffer,
 *   with the remaining area filled with black.
 *
 * Reference:
 *   - vin0_callback: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:907-941
 *   - display_next_buffer_set: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/display_thread_entry.c:157-160
 *   - GLCDC BufferChange: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/display_thread_entry.c:264-268
 *
 * @note
 * This file is part of the MIPI CSI-2 integration test (S-003-4) implementation.
 */

#ifndef CAMERA_TEST_H
#define CAMERA_TEST_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

#include "diag_config.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * Default FPS measurement duration in milliseconds
 *
 * FPS is measured by counting frames over this duration.
 * Longer durations give more accurate results.
 */
#define CAMERA_TEST_FPS_DURATION_MS         (3000)

/**
 * Minimum FPS measurement duration in milliseconds
 */
#define CAMERA_TEST_FPS_MIN_DURATION_MS     (1000)

/**
 * Maximum FPS measurement duration in milliseconds
 */
#define CAMERA_TEST_FPS_MAX_DURATION_MS     (30000)

/**
 * Default streaming duration in milliseconds
 *
 * How long the "camera test stream" command runs before stopping.
 */
#define CAMERA_TEST_STREAM_DURATION_MS      (10000)

/**
 * Maximum streaming duration in milliseconds (10 minutes)
 */
#define CAMERA_TEST_STREAM_MAX_DURATION_MS  (600000)

/**
 * Data validation: minimum percentage of non-zero pixels required
 *
 * A captured frame is considered valid if at least this percentage
 * of its 16-bit pixel values are non-zero. This catches cases where
 * the capture buffer was never written (all zeros).
 */
#define CAMERA_TEST_NONZERO_MIN_PERCENT     (5)

/**
 * Data validation: Number of sample points for quick validation
 *
 * Rather than scanning the entire frame, we sample this many evenly
 * spaced pixels across the frame for a quick validity check.
 */
#define CAMERA_TEST_SAMPLE_COUNT            (256)

/**********************************************************************************************************************
 Global Typedef definitions
 *********************************************************************************************************************/

/**
 * Camera test result for single frame capture
 */
typedef struct {
    bool        success;            /**< true if capture and validation succeeded */
    uint8_t    *p_frame;            /**< Pointer to the captured frame buffer */
    uint32_t    frame_size;         /**< Frame size in bytes */
    uint32_t    capture_time_ms;    /**< Time taken to capture the frame (ms) */
    uint32_t    nonzero_count;      /**< Number of non-zero sample pixels */
    uint32_t    sample_count;       /**< Total number of sampled pixels */
    uint32_t    unique_values;      /**< Number of unique pixel values found in samples */
    bool        data_valid;         /**< true if captured data passes validation */
} camera_test_capture_result_t;

/**
 * Camera test result for FPS measurement
 */
typedef struct {
    bool        success;            /**< true if measurement completed successfully */
    uint32_t    duration_ms;        /**< Actual measurement duration (ms) */
    uint32_t    frame_count;        /**< Number of frames captured during measurement */
    uint32_t    fps_x100;           /**< Measured FPS x 100 (e.g., 3012 = 30.12 fps) */
    uint32_t    frame_drop_count;   /**< Number of frame drops detected */
    uint32_t    error_count;        /**< Number of VIN errors during measurement */
    uint32_t    fifo_overflow;      /**< Number of FIFO overflows during measurement */
} camera_test_fps_result_t;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Issue #183: the test suite below is bring-up-only and is excluded from the
 * default build to reclaim CPU0 internal flash (its console strings are the
 * bulk of its footprint). The declarations are guarded as well so that a new
 * caller added outside this module fails to compile rather than to link.
 * See src/diag_config.h. Only camera_test_cmd() stays available in both
 * configurations; with the switch off it reports that the command was
 * excluded from this build.
 */
#if MIMAMORI_VERBOSE_DIAG

/**
 * Perform a single frame capture test (S-003-4)
 *
 * @details
 *   1. Ensure VIN is initialized and start capture if not already running
 *   2. Wait for a new frame to complete (with timeout)
 *   3. Validate the captured data:
 *      a. Check that the buffer pointer is non-NULL
 *      b. Sample pixels across the frame and count non-zero values
 *      c. Count unique pixel values (detect single-color or stuck patterns)
 *   4. Report results including buffer address, capture time, and validation
 *
 * @param result  Pointer to result structure to fill (may be NULL)
 * @retval true   Capture and validation succeeded
 * @retval false  Capture failed or data validation failed
 */
bool camera_test_capture(camera_test_capture_result_t *result);

/**
 * Measure camera capture frame rate (S-003-4)
 *
 * @details
 *   1. Ensure VIN capture is running
 *   2. Reset VIN statistics counters
 *   3. Record initial frame count and start time
 *   4. Wait for the specified duration
 *   5. Record final frame count and elapsed time
 *   6. Compute FPS = (frame_count * 100000) / duration_ms (x100 precision)
 *   7. Detect frame drops (end_of_frame != frame_complete)
 *   8. Report FPS, frame drops, and error counts
 *
 * @param duration_ms  Measurement duration in milliseconds
 * @param result       Pointer to result structure to fill (may be NULL)
 * @retval true   Measurement completed successfully
 * @retval false  Measurement failed
 */
bool camera_test_fps(uint32_t duration_ms, camera_test_fps_result_t *result);

/**
 * Copy a camera capture frame to the GLCDC framebuffer for display (S-003-4)
 *
 * @details
 *   Copies a VIN capture buffer (768x450, RGB565/YUV422) to the GLCDC
 *   framebuffer (1024x600, RGB565) for direct display without LVGL.
 *
 *   The camera image is placed at the top-left corner of the LCD.
 *   Pixels outside the camera image area are filled with black.
 *
 *   Note: The VIN output format is YUV422 8-bit, but with the VIN
 *   byte-swap and conversion settings in the reference project, the
 *   output in the SDRAM buffer is RGB565-compatible.
 *
 *   This function writes to BOTH fb_background[0] and fb_background[1]
 *   to ensure correct display regardless of double-buffer state.
 *
 * Preconditions:
 *   - GLCDC must be initialized (S-002)
 *   - VIN capture must have produced at least one frame
 *
 * Reference:
 *   - display_next_buffer_set: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/display_thread_entry.c:157-160
 *   - R_GLCDC_BufferChange: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/display_thread_entry.c:264-268
 *
 * @param p_camera_frame  Pointer to the VIN capture buffer to display
 * @retval true   Frame copied to GLCDC framebuffer successfully
 * @retval false  Display not available or invalid frame pointer
 */
bool camera_test_display_frame(const uint8_t *p_camera_frame);

/**
 * Run streaming test: continuous capture with LCD display (S-003-4)
 *
 * @details
 *   1. Start VIN capture (if not already running)
 *   2. For the specified duration, on each new frame:
 *      a. Copy the frame to the GLCDC framebuffer
 *      b. Track frame count and timing
 *   3. Report FPS, frame drops, and error statistics
 *
 * @param duration_ms  Streaming duration in milliseconds
 * @retval true   Streaming test completed successfully
 * @retval false  Streaming test failed
 */
bool camera_test_stream(uint32_t duration_ms);

#endif /* MIMAMORI_VERBOSE_DIAG (Issue #183) */

/**
 * NT-Shell "camera test" sub-command handler (S-003-4)
 *
 * @details Provides camera integration test sub-commands:
 *   camera test capture           - Single frame capture with validation
 *   camera test capture display   - Single frame capture and display on LCD
 *   camera test fps [duration_ms] - FPS measurement
 *   camera test stream [duration_ms] - Continuous capture with LCD display
 *   camera test validate          - Validate last captured frame data
 *
 * @note Issue #183: when MIMAMORI_VERBOSE_DIAG is 0 the sub-commands are not
 *       built; this handler then only reports that the verbose build is
 *       required (cmd_print_diag_disabled()).
 *
 * @param argc  Argument count (from the original camera command)
 * @param argv  Argument vector
 */
void camera_test_cmd(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_TEST_H */
