/**
 * @file camera_test.c
 * @brief MIPI CSI-2 camera integration test and verification (S-003-4)
 * @details
 * Implements end-to-end camera pipeline test functions for verifying the
 * MIPI D-PHY -> CSI-2 -> VIN capture chain with the OV5640 camera module.
 *
 * This module provides:
 *   - Single frame capture test with data validation
 *   - Continuous capture FPS measurement
 *   - Camera-to-GLCDC direct display (bypass LVGL)
 *   - Streaming test with continuous LCD update
 *   - NT-Shell "camera test" command for interactive testing
 *
 * Test Strategy:
 *   1. Capture Test (camera_test_capture):
 *      - Start VIN capture, wait for frame completion callback
 *      - Sample pixels across the frame to verify data is non-zero
 *      - Count unique pixel values to detect stuck/uniform patterns
 *
 *   2. FPS Test (camera_test_fps):
 *      - Run capture for a measured duration
 *      - Count frame_complete callbacks from VIN
 *      - Compute FPS = frames / time
 *      - Detect frame drops by comparing end_of_frame vs frame_complete
 *
 *   3. Display Test (camera_test_display_frame):
 *      - Copy VIN buffer (768x450) to GLCDC framebuffer (1024x600)
 *      - Line-by-line copy with stride conversion
 *      - Fill remaining LCD area with black
 *
 *   4. Stream Test (camera_test_stream):
 *      - Continuously copy new frames to GLCDC for live preview
 *      - Track performance metrics during streaming
 *
 * Reference:
 *   - vin0_callback: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:907-941
 *   - display_next_buffer_set: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/display_thread_entry.c:157-160
 *   - GLCDC display: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/display_thread_entry.c:264-268
 *
 * @note
 * This file is part of the MIPI CSI-2 integration test (S-003-4) implementation.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>

#include "camera_test.h"
#include "vin_port.h"
#include "csi2_port.h"
#include "mipi_port.h"
#include "glcdc_port.h"
#include "bsp_api.h"
#include "jlink_console.h"
#include "cmd_utils.h"
#include "ntlibc.h"
#include "common_data.h"

/* R-006 (Issue #156): FreeRTOS.h / task.h removed.
 *   vTaskDelay(ticks)    -> tk_dly_tsk(ms)
 *   xTaskGetTickCount()  -> tk_get_otm (operating time, ms)
 * Tick-based macros were renamed to *_MS (1 tick == 1 ms in the former
 * configuration, so the values are unchanged). */
#include <tk/tkernel.h>

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size */
#define TEST_PRINT_BUF_SIZE         (128)

/** Timeout for waiting for a single frame capture (ms) */
#define TEST_CAPTURE_TIMEOUT_MS     (2000)

/** Polling interval while waiting for frame (ms) */
#define TEST_POLL_INTERVAL_MS       (1)

/** Streaming display update interval (ms, limit CPU usage) */
#define TEST_STREAM_UPDATE_MS       (10)

/**********************************************************************************************************************
 Private (static) functions prototypes
 *********************************************************************************************************************/
static bool test_ensure_capture_running(void);
static bool test_validate_frame(const uint8_t *p_frame, uint32_t frame_size,
                                uint32_t *nonzero_count, uint32_t *sample_count,
                                uint32_t *unique_values);
static void test_cmd_capture(int argc, char **argv);
static void test_cmd_fps(int argc, char **argv);
static void test_cmd_stream(int argc, char **argv);
static void test_cmd_validate(void);
static void test_cmd_print_help(void);

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Get the current operating time in milliseconds (R-006).
 *
 * tk_get_otm() is a reference-type system call (ms-based). SYSTIM is a
 * 64-bit hi/lo pair; the lower 32 bits suffice for the second-order deltas
 * measured here (same technique as camera_framebuffer.c, R-005).
 */
static uint32_t test_now_ms(void)
{
    SYSTIM now = {0, 0};
    (void)tk_get_otm(&now);
    return (uint32_t)now.lo;
}

/**
 * Ensure VIN capture is initialized and running
 *
 * @details Auto-initializes VIN if needed and starts capture.
 * @retval true   VIN capture is running
 * @retval false  Failed to start VIN capture
 */
static bool test_ensure_capture_running(void)
{
    vin_port_status_t status = vin_port_get_status();

    /* Auto-initialize if needed */
    if (status == VIN_PORT_STATUS_NOT_INITIALIZED ||
        status == VIN_PORT_STATUS_NO_FSP_MODULE) {
        if (!vin_port_init()) {
            return false;
        }
        status = vin_port_get_status();
    }

    /* Start capture if not already running */
    if (status != VIN_PORT_STATUS_CAPTURING) {
        if (!vin_port_capture_start()) {
            return false;
        }
    }

    return true;
}

/**
 * Validate captured frame data
 *
 * @details Samples pixels across the frame and checks:
 *   1. Non-zero pixel count (buffer was actually written)
 *   2. Unique value count (not a stuck/uniform pattern)
 *
 * @param p_frame       Pointer to frame buffer
 * @param frame_size    Frame size in bytes
 * @param nonzero_count Output: number of non-zero sample pixels
 * @param sample_count  Output: total number of sampled pixels
 * @param unique_values Output: number of unique pixel values in samples
 * @retval true         Frame data is valid
 * @retval false        Frame data is invalid (all zeros or no variation)
 */
static bool test_validate_frame(const uint8_t *p_frame, uint32_t frame_size,
                                uint32_t *nonzero_count, uint32_t *sample_count,
                                uint32_t *unique_values)
{
    if (p_frame == NULL || frame_size < 2) {
        return false;
    }

    uint32_t total_pixels = frame_size / 2;  /* RGB565 = 2 bytes per pixel */
    uint32_t step = total_pixels / CAMERA_TEST_SAMPLE_COUNT;
    if (step == 0) {
        step = 1;
    }

    uint32_t nz_count = 0;
    uint32_t s_count = 0;

    /*
     * Track unique values using a simple hash table approach.
     * We use 256 buckets and count how many are populated.
     * This is not exact but gives a reasonable estimate of diversity.
     */
    uint8_t hash_bucket[256];
    memset(hash_bucket, 0, sizeof(hash_bucket));

    const uint16_t *p_pixels = (const uint16_t *)p_frame;

    for (uint32_t i = 0; i < total_pixels && s_count < CAMERA_TEST_SAMPLE_COUNT; i += step) {
        uint16_t pixel = p_pixels[i];
        s_count++;

        if (pixel != 0) {
            nz_count++;
        }

        /* Hash the pixel value to a bucket */
        uint8_t hash = (uint8_t)((pixel ^ (pixel >> 8)) & 0xFF);
        hash_bucket[hash] = 1;
    }

    /* Count populated hash buckets as a proxy for unique values */
    uint32_t unique = 0;
    for (uint32_t i = 0; i < 256; i++) {
        if (hash_bucket[i]) {
            unique++;
        }
    }

    if (nonzero_count) *nonzero_count = nz_count;
    if (sample_count)  *sample_count  = s_count;
    if (unique_values) *unique_values = unique;

    /* Validation criteria:
     * 1. At least CAMERA_TEST_NONZERO_MIN_PERCENT of samples are non-zero
     * 2. At least 2 unique hash buckets (not all same value)
     */
    if (s_count == 0) {
        return false;
    }

    uint32_t nonzero_percent = (nz_count * 100) / s_count;

    return (nonzero_percent >= CAMERA_TEST_NONZERO_MIN_PERCENT) && (unique >= 2);
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Perform a single frame capture test (S-003-4)
 *
 * @details
 *   1. Ensure VIN is initialized and capture is running
 *   2. Record initial frame count
 *   3. Wait for a new frame completion (with timeout)
 *   4. Validate captured data
 *   5. Fill result structure
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:359
 */
bool camera_test_capture(camera_test_capture_result_t *result)
{
    camera_test_capture_result_t local_result;
    memset(&local_result, 0, sizeof(local_result));
    local_result.frame_size = VIN_FRAME_SIZE_BYTES;

    /* Ensure capture is running */
    if (!test_ensure_capture_running()) {
        local_result.success = false;
        if (result) {
            *result = local_result;
        }
        return false;
    }

    /* Record initial frame count */
    vin_port_info_t info;
    vin_port_get_info(&info);
    uint32_t initial_frame_count = info.stats.frame_complete;

    /* Record start time (R-006: tick -> ms) */
    uint32_t start_ms = test_now_ms();

    /* Wait for a new frame */
    uint8_t *frame_ptr = NULL;
    bool frame_received = false;

    while ((test_now_ms() - start_ms) < TEST_CAPTURE_TIMEOUT_MS) {
        frame_ptr = vin_port_get_last_frame();
        vin_port_get_info(&info);

        if (info.stats.frame_complete > initial_frame_count && frame_ptr != NULL) {
            frame_received = true;
            break;
        }

        tk_dly_tsk(TEST_POLL_INTERVAL_MS);
    }

    local_result.capture_time_ms = test_now_ms() - start_ms;
    local_result.p_frame = frame_ptr;

    if (!frame_received || frame_ptr == NULL) {
        local_result.success = false;
        local_result.data_valid = false;
        if (result) {
            *result = local_result;
        }
        return false;
    }

    /* Validate frame data */
    local_result.data_valid = test_validate_frame(
        frame_ptr, VIN_FRAME_SIZE_BYTES,
        &local_result.nonzero_count,
        &local_result.sample_count,
        &local_result.unique_values
    );

    local_result.success = local_result.data_valid;

    if (result) {
        *result = local_result;
    }

    return local_result.success;
}

/**
 * Measure camera capture frame rate (S-003-4)
 *
 * @details
 *   1. Ensure VIN capture is running
 *   2. Reset statistics
 *   3. Measure frame count over the specified duration
 *   4. Compute FPS with x100 precision
 *   5. Detect frame drops and errors
 */
bool camera_test_fps(uint32_t duration_ms, camera_test_fps_result_t *result)
{
    camera_test_fps_result_t local_result;
    memset(&local_result, 0, sizeof(local_result));

    /* Clamp duration */
    if (duration_ms < CAMERA_TEST_FPS_MIN_DURATION_MS) {
        duration_ms = CAMERA_TEST_FPS_MIN_DURATION_MS;
    }
    if (duration_ms > CAMERA_TEST_FPS_MAX_DURATION_MS) {
        duration_ms = CAMERA_TEST_FPS_MAX_DURATION_MS;
    }

    /* Ensure capture is running */
    if (!test_ensure_capture_running()) {
        local_result.success = false;
        if (result) {
            *result = local_result;
        }
        return false;
    }

    /* Reset statistics for clean measurement */
    vin_port_reset_stats();

    /* Wait a small amount to let the reset take effect */
    tk_dly_tsk(10);     /* R-006: vTaskDelay(pdMS_TO_TICKS(10)) -> tk_dly_tsk(10) ms */

    /* Record starting point */
    vin_port_info_t start_info;
    vin_port_get_info(&start_info);
    uint32_t start_frames = start_info.stats.frame_complete;
    uint32_t start_eof    = start_info.stats.end_of_frame;
    uint32_t start_errors = start_info.stats.error_event;
    uint32_t start_fifo   = start_info.stats.fifo_overflow;

    uint32_t start_ms = test_now_ms();      /* R-006: tick -> ms */

    /* Wait for the measurement duration */
    tk_dly_tsk((RELTIM)duration_ms);        /* R-006: vTaskDelay -> tk_dly_tsk (ms) */

    uint32_t end_ms = test_now_ms();

    /* Record ending point */
    vin_port_info_t end_info;
    vin_port_get_info(&end_info);
    uint32_t end_frames = end_info.stats.frame_complete;
    uint32_t end_eof    = end_info.stats.end_of_frame;
    uint32_t end_errors = end_info.stats.error_event;
    uint32_t end_fifo   = end_info.stats.fifo_overflow;

    /* Compute results */
    uint32_t actual_duration_ms = end_ms - start_ms;
    uint32_t frame_count = end_frames - start_frames;
    uint32_t eof_count   = end_eof - start_eof;

    local_result.success = true;
    local_result.duration_ms = actual_duration_ms;
    local_result.frame_count = frame_count;
    local_result.error_count = end_errors - start_errors;
    local_result.fifo_overflow = end_fifo - start_fifo;

    /* Compute FPS x 100 */
    if (actual_duration_ms > 0) {
        local_result.fps_x100 = (frame_count * 100000UL) / actual_duration_ms;
    } else {
        local_result.fps_x100 = 0;
    }

    /*
     * Detect frame drops:
     * end_of_frame events should match or slightly exceed frame_complete events.
     * A significant difference indicates dropped frames (VIN received data but
     * could not write it to memory in time).
     */
    if (eof_count > frame_count) {
        local_result.frame_drop_count = eof_count - frame_count;
    } else {
        local_result.frame_drop_count = 0;
    }

    if (result) {
        *result = local_result;
    }

    return true;
}

/**
 * Copy a camera capture frame to the GLCDC framebuffer for display (S-003-4)
 *
 * @details
 *   The VIN capture buffer is 768x450 pixels (VIN_IMAGE_WIDTH x VIN_IMAGE_HEIGHT)
 *   with VIN_BYTES_PER_LINE stride. The GLCDC framebuffer is 1024x600 pixels
 *   with DISPLAY_BUFFER_STRIDE_BYTES_INPUT0 stride.
 *
 *   This function performs a line-by-line copy from the VIN buffer to the
 *   GLCDC framebuffer, handling the stride difference.
 *
 *   The camera image is placed at the top-left (0,0). Pixels outside the
 *   camera image area (right margin and bottom margin) are filled with black.
 *
 * Reference:
 *   - display_next_buffer_set: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/display_thread_entry.c:157-160
 *   - R_GLCDC_BufferChange: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/display_thread_entry.c:264-268
 */
bool camera_test_display_frame(const uint8_t *p_camera_frame)
{
    if (p_camera_frame == NULL) {
        return false;
    }

    /* Check if GLCDC is available */
    if (!glcdc_port_is_available()) {
        return false;
    }

#if GLCDC_CFG_LAYER_1_ENABLE
    /*
     * Write to both framebuffers to ensure display regardless of
     * which buffer is currently front/back in the double-buffering scheme.
     *
     * Reference: e2studio_CPU0/src/port/glcdc_port.c:571-577
     */
    uint8_t *p_fb[2] = {
        &fb_background[0][0],
        &fb_background[1][0]
    };

    uint32_t cam_width_bytes  = VIN_IMAGE_WIDTH * VIN_BYTES_PER_PIXEL;
    uint32_t cam_stride_bytes = VIN_BYTES_PER_LINE;
    uint32_t lcd_stride_bytes = DISPLAY_BUFFER_STRIDE_BYTES_INPUT0;

    for (int fb_idx = 0; fb_idx < 2; fb_idx++) {
        uint8_t *p_dst = p_fb[fb_idx];

        /*
         * Copy the camera image line by line.
         * Camera image occupies the top-left VIN_IMAGE_WIDTH x VIN_IMAGE_HEIGHT area.
         */
        for (uint32_t y = 0; y < VIN_IMAGE_HEIGHT && y < GLCDC_DISPLAY_VSIZE; y++) {
            const uint8_t *p_src_line = p_camera_frame + (y * cam_stride_bytes);
            uint8_t       *p_dst_line = p_dst + (y * lcd_stride_bytes);

            /* Copy camera pixels for this line */
            uint32_t copy_width = cam_width_bytes;
            if (copy_width > (GLCDC_DISPLAY_HSIZE * 2)) {
                copy_width = GLCDC_DISPLAY_HSIZE * 2;
            }
            memcpy(p_dst_line, p_src_line, copy_width);

            /* Fill the right margin with black (0x0000 for RGB565) */
            if (VIN_IMAGE_WIDTH < GLCDC_DISPLAY_HSIZE) {
                uint32_t margin_bytes = (GLCDC_DISPLAY_HSIZE - VIN_IMAGE_WIDTH) * 2;
                memset(p_dst_line + copy_width, 0, margin_bytes);
            }
        }

        /* Fill the bottom margin with black */
        if (VIN_IMAGE_HEIGHT < GLCDC_DISPLAY_VSIZE) {
            for (uint32_t y = VIN_IMAGE_HEIGHT; y < GLCDC_DISPLAY_VSIZE; y++) {
                uint8_t *p_dst_line = p_dst + (y * lcd_stride_bytes);
                memset(p_dst_line, 0, GLCDC_DISPLAY_HSIZE * 2);
            }
        }
    }

    return true;
#else
    /* GLCDC Layer 1 not enabled */
    (void)p_camera_frame;
    return false;
#endif
}

/**
 * Run streaming test: continuous capture with LCD display (S-003-4)
 *
 * @details
 *   Continuously copies new camera frames to the GLCDC framebuffer for
 *   a live camera preview on the LCD. Tracks performance metrics.
 *
 *   The function runs for the specified duration and then stops,
 *   printing performance statistics.
 *
 * Reference:
 *   - camera_thread_entry main loop: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:367-376
 */
bool camera_test_stream(uint32_t duration_ms)
{
    char buf[TEST_PRINT_BUF_SIZE];

    /* Clamp duration */
    if (duration_ms > CAMERA_TEST_STREAM_MAX_DURATION_MS) {
        duration_ms = CAMERA_TEST_STREAM_MAX_DURATION_MS;
    }

    /* Ensure capture is running */
    if (!test_ensure_capture_running()) {
        print_to_console("  Error: Failed to start VIN capture.\r\n");
        return false;
    }

    /* Check GLCDC availability */
    if (!glcdc_port_is_available()) {
        print_to_console("  Warning: GLCDC not available. Streaming without display.\r\n");
    }

    /* Reset statistics for clean measurement */
    vin_port_reset_stats();
    tk_dly_tsk(10);     /* R-006: vTaskDelay(pdMS_TO_TICKS(10)) -> tk_dly_tsk(10) ms */

    snprintf(buf, sizeof(buf), "  Streaming for %lu ms...\r\n", (unsigned long)duration_ms);
    print_to_console(buf);

    vin_port_info_t start_info;
    vin_port_get_info(&start_info);
    uint32_t start_frames = start_info.stats.frame_complete;

    uint32_t start_ms = test_now_ms();      /* R-006: tick -> ms */

    uint32_t display_count = 0;
    uint32_t last_frame_count = start_frames;

    /* Streaming loop */
    while ((test_now_ms() - start_ms) < duration_ms) {
        vin_port_info_t info;
        vin_port_get_info(&info);

        /* Check if a new frame has arrived */
        if (info.stats.frame_complete > last_frame_count) {
            last_frame_count = info.stats.frame_complete;

            /* Display the frame on LCD */
            uint8_t *p_frame = vin_port_get_last_frame();
            if (p_frame != NULL && glcdc_port_is_available()) {
                camera_test_display_frame(p_frame);
                display_count++;
            }
        }

        tk_dly_tsk(TEST_STREAM_UPDATE_MS);
    }

    /* Compute results */
    uint32_t actual_duration_ms = test_now_ms() - start_ms;

    vin_port_info_t end_info;
    vin_port_get_info(&end_info);
    uint32_t total_frames = end_info.stats.frame_complete - start_frames;
    uint32_t total_eof    = end_info.stats.end_of_frame;
    uint32_t total_errors = end_info.stats.error_event;
    uint32_t total_fifo   = end_info.stats.fifo_overflow;

    uint32_t fps_x100 = 0;
    if (actual_duration_ms > 0) {
        fps_x100 = (total_frames * 100000UL) / actual_duration_ms;
    }

    /* Print streaming results */
    print_to_console("[Streaming Test Results (S-003-4)]\r\n");

    snprintf(buf, sizeof(buf), "  Duration       : %lu ms\r\n", (unsigned long)actual_duration_ms);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Total Frames   : %lu\r\n", (unsigned long)total_frames);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Displayed      : %lu frames\r\n", (unsigned long)display_count);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Capture FPS    : %lu.%02lu\r\n",
             (unsigned long)(fps_x100 / 100), (unsigned long)(fps_x100 % 100));
    print_to_console(buf);

    uint32_t display_fps_x100 = 0;
    if (actual_duration_ms > 0) {
        display_fps_x100 = (display_count * 100000UL) / actual_duration_ms;
    }
    snprintf(buf, sizeof(buf), "  Display FPS    : %lu.%02lu\r\n",
             (unsigned long)(display_fps_x100 / 100), (unsigned long)(display_fps_x100 % 100));
    print_to_console(buf);

    if (total_eof > total_frames) {
        snprintf(buf, sizeof(buf), "  Frame Drops    : %lu\r\n",
                 (unsigned long)(total_eof - total_frames));
    } else {
        snprintf(buf, sizeof(buf), "  Frame Drops    : 0\r\n");
    }
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  FIFO Overflow  : %lu\r\n", (unsigned long)total_fifo);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Error Events   : %lu\r\n", (unsigned long)total_errors);
    print_to_console(buf);

    /* Pass/fail assessment */
    bool pass = (total_frames > 0) && (fps_x100 >= 3000);  /* At least 30 fps */

    if (total_eof > total_frames) {
        uint32_t drop_count = total_eof - total_frames;
        uint32_t drop_pct = (total_frames > 0) ? ((drop_count * 100) / total_frames) : 100;
        if (drop_pct > 1) {
            pass = false;  /* Frame drop rate > 1% */
        }
    }

    if (pass) {
        print_to_console("  Result         : PASS\r\n");
    } else {
        print_to_console("  Result         : FAIL\r\n");
        if (fps_x100 < 3000) {
            snprintf(buf, sizeof(buf), "  Reason         : FPS below 30 (target: >= 30.00)\r\n");
            print_to_console(buf);
        }
    }

    return pass;
}

/**********************************************************************************************************************
 NT-Shell command handlers
 *********************************************************************************************************************/

/**
 * "camera test capture" sub-command handler
 *
 * @details Performs a single frame capture, validates the data, and optionally
 *          displays it on the LCD if "display" argument is given.
 */
static void test_cmd_capture(int argc, char **argv)
{
    char buf[TEST_PRINT_BUF_SIZE];
    bool display_on_lcd = false;

    /* Check for "display" argument: "camera test capture display" */
    if (argc >= 4 && ntlibc_strcmp(argv[3], "display") == 0) {
        display_on_lcd = true;
    }

    print_to_console("  Performing single frame capture test...\r\n");

    camera_test_capture_result_t result;
    bool ok = camera_test_capture(&result);

    /* Print results */
    print_to_console("[Capture Test Results (S-003-4)]\r\n");

    if (result.p_frame != NULL) {
        snprintf(buf, sizeof(buf), "  Frame Address  : 0x%08lX\r\n",
                 (unsigned long)(uintptr_t)result.p_frame);
        print_to_console(buf);
    } else {
        print_to_console("  Frame Address  : (none - capture failed)\r\n");
    }

    snprintf(buf, sizeof(buf), "  Frame Size     : %lu bytes (%lu KB)\r\n",
             (unsigned long)result.frame_size,
             (unsigned long)(result.frame_size / 1024));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Capture Time   : %lu ms\r\n",
             (unsigned long)result.capture_time_ms);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Sampled Pixels : %lu / %lu non-zero (%lu%%)\r\n",
             (unsigned long)result.nonzero_count,
             (unsigned long)result.sample_count,
             (unsigned long)(result.sample_count > 0 ?
                            (result.nonzero_count * 100 / result.sample_count) : 0));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Unique Hashes  : %lu (pixel value diversity)\r\n",
             (unsigned long)result.unique_values);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Data Valid     : %s\r\n", result.data_valid ? "Yes" : "No");
    print_to_console(buf);

    if (ok) {
        print_to_console("  Result         : PASS\r\n");

        /* Show first 16 bytes */
        if (result.p_frame != NULL) {
            print_to_console("  First 16 bytes : ");
            for (int i = 0; i < 16; i++) {
                snprintf(buf, sizeof(buf), "%02X ", result.p_frame[i]);
                print_to_console(buf);
            }
            print_to_console("\r\n");
        }

        /* Display on LCD if requested */
        if (display_on_lcd && result.p_frame != NULL) {
            print_to_console("  Displaying on LCD...\r\n");
            if (camera_test_display_frame(result.p_frame)) {
                print_to_console("  Frame displayed on LCD.\r\n");
            } else {
                print_to_console("  Warning: Failed to display on LCD (GLCDC not available).\r\n");
            }
        }
    } else {
        print_to_console("  Result         : FAIL\r\n");

        if (result.p_frame == NULL) {
            print_to_console("  Reason         : Capture timeout or no frame received.\r\n");

            vin_port_status_t vin_status = vin_port_get_status();
            if (vin_status == VIN_PORT_STATUS_NO_FSP_MODULE) {
                print_to_console("  Hint           : FSP VIN module not configured.\r\n");
            } else if (vin_status == VIN_PORT_STATUS_NOT_INITIALIZED) {
                print_to_console("  Hint           : VIN not initialized.\r\n");
            }
        } else if (!result.data_valid) {
            print_to_console("  Reason         : Captured data validation failed.\r\n");
            if (result.nonzero_count == 0) {
                print_to_console("  Hint           : All sampled pixels are zero.\r\n");
                print_to_console("                   Camera may not be connected or streaming.\r\n");
            }
        }
    }
}

/**
 * "camera test fps" sub-command handler
 *
 * @details Measures FPS by counting frames over a configurable duration.
 *          Default: 3 seconds. Pass duration as argument in milliseconds.
 */
static void test_cmd_fps(int argc, char **argv)
{
    char buf[TEST_PRINT_BUF_SIZE];
    uint32_t duration_ms = CAMERA_TEST_FPS_DURATION_MS;

    /* Parse optional duration argument: "camera test fps [duration_ms]" */
    if (argc >= 4) {
        cmd_parse_result_t parse = cmd_parse_uint32(argv[3]);
        if (parse.valid && parse.value > 0) {
            duration_ms = parse.value;
        } else {
            print_to_console("  Warning: Invalid duration, using default.\r\n");
        }
    }

    snprintf(buf, sizeof(buf), "  Measuring FPS over %lu ms...\r\n", (unsigned long)duration_ms);
    print_to_console(buf);

    camera_test_fps_result_t result;
    bool ok = camera_test_fps(duration_ms, &result);

    /* Print results */
    print_to_console("[FPS Measurement Results (S-003-4)]\r\n");

    if (!ok) {
        print_to_console("  Result         : FAIL (measurement failed)\r\n");

        vin_port_status_t vin_status = vin_port_get_status();
        if (vin_status == VIN_PORT_STATUS_NO_FSP_MODULE) {
            print_to_console("  Hint           : FSP VIN module not configured.\r\n");
        }
        return;
    }

    snprintf(buf, sizeof(buf), "  Duration       : %lu ms\r\n", (unsigned long)result.duration_ms);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Frame Count    : %lu\r\n", (unsigned long)result.frame_count);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  FPS            : %lu.%02lu\r\n",
             (unsigned long)(result.fps_x100 / 100),
             (unsigned long)(result.fps_x100 % 100));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Frame Drops    : %lu\r\n", (unsigned long)result.frame_drop_count);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  FIFO Overflow  : %lu\r\n", (unsigned long)result.fifo_overflow);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Error Events   : %lu\r\n", (unsigned long)result.error_count);
    print_to_console(buf);

    /* Pass/fail assessment */
    bool pass = true;
    if (result.fps_x100 < 3000) {
        pass = false;
        snprintf(buf, sizeof(buf), "  FPS Check      : FAIL (%.2f < 30.00 target)\r\n",
                 (float)result.fps_x100 / 100.0f);
        print_to_console(buf);
    } else {
        print_to_console("  FPS Check      : PASS (>= 30.00)\r\n");
    }

    if (result.frame_count > 0 && result.frame_drop_count > 0) {
        uint32_t drop_pct = (result.frame_drop_count * 100) / result.frame_count;
        if (drop_pct > 1) {
            pass = false;
            snprintf(buf, sizeof(buf), "  Drop Rate      : FAIL (%lu%% > 1%% target)\r\n",
                     (unsigned long)drop_pct);
            print_to_console(buf);
        } else {
            snprintf(buf, sizeof(buf), "  Drop Rate      : PASS (%lu%% <= 1%%)\r\n",
                     (unsigned long)drop_pct);
            print_to_console(buf);
        }
    } else {
        print_to_console("  Drop Rate      : PASS (0%%)\r\n");
    }

    snprintf(buf, sizeof(buf), "  Overall        : %s\r\n", pass ? "PASS" : "FAIL");
    print_to_console(buf);
}

/**
 * "camera test stream" sub-command handler
 *
 * @details Runs continuous capture with LCD display for a configurable duration.
 *          Default: 10 seconds. Pass duration as argument in milliseconds.
 */
static void test_cmd_stream(int argc, char **argv)
{
    char buf[TEST_PRINT_BUF_SIZE];
    uint32_t duration_ms = CAMERA_TEST_STREAM_DURATION_MS;

    /* Parse optional duration argument: "camera test stream [duration_ms]" */
    if (argc >= 4) {
        cmd_parse_result_t parse = cmd_parse_uint32(argv[3]);
        if (parse.valid && parse.value > 0) {
            duration_ms = parse.value;
        } else {
            print_to_console("  Warning: Invalid duration, using default.\r\n");
        }
    }

    snprintf(buf, sizeof(buf), "  Starting streaming test (%lu ms)...\r\n",
             (unsigned long)duration_ms);
    print_to_console(buf);

    camera_test_stream(duration_ms);
}

/**
 * "camera test validate" sub-command handler
 *
 * @details Validates the most recently captured frame data without
 *          performing a new capture. Useful for checking data after
 *          capture has been running for a while.
 */
static void test_cmd_validate(void)
{
    char buf[TEST_PRINT_BUF_SIZE];

    uint8_t *p_frame = vin_port_get_last_frame();

    if (p_frame == NULL) {
        print_to_console("  No captured frame available.\r\n");
        print_to_console("  Run 'camera start' or 'camera test capture' first.\r\n");
        return;
    }

    print_to_console("[Frame Validation (S-003-4)]\r\n");

    snprintf(buf, sizeof(buf), "  Frame Address  : 0x%08lX\r\n",
             (unsigned long)(uintptr_t)p_frame);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Frame Size     : %lu bytes\r\n",
             (unsigned long)VIN_FRAME_SIZE_BYTES);
    print_to_console(buf);

    uint32_t nonzero_count, sample_count, unique_values;
    bool valid = test_validate_frame(p_frame, VIN_FRAME_SIZE_BYTES,
                                     &nonzero_count, &sample_count, &unique_values);

    snprintf(buf, sizeof(buf), "  Sampled Pixels : %lu / %lu non-zero (%lu%%)\r\n",
             (unsigned long)nonzero_count,
             (unsigned long)sample_count,
             (unsigned long)(sample_count > 0 ?
                            (nonzero_count * 100 / sample_count) : 0));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Unique Hashes  : %lu\r\n", (unsigned long)unique_values);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Data Valid     : %s\r\n", valid ? "Yes" : "No");
    print_to_console(buf);

    /* Show first 32 bytes */
    print_to_console("  First 32 bytes:\r\n    ");
    for (int i = 0; i < 32; i++) {
        snprintf(buf, sizeof(buf), "%02X ", p_frame[i]);
        print_to_console(buf);
        if (i == 15) {
            print_to_console("\r\n    ");
        }
    }
    print_to_console("\r\n");

    /* Show pixel values at strategic positions */
    print_to_console("  Sample pixels (RGB565):\r\n");

    /* Top-left */
    uint16_t tl = *(uint16_t *)p_frame;
    snprintf(buf, sizeof(buf), "    [0,0]     : 0x%04X\r\n", tl);
    print_to_console(buf);

    /* Center */
    uint32_t center_offset = (VIN_IMAGE_HEIGHT / 2) * VIN_BYTES_PER_LINE +
                             (VIN_IMAGE_WIDTH / 2) * VIN_BYTES_PER_PIXEL;
    if (center_offset + 1 < VIN_FRAME_SIZE_BYTES) {
        uint16_t center = *(uint16_t *)(p_frame + center_offset);
        snprintf(buf, sizeof(buf), "    [%lu,%lu] : 0x%04X\r\n",
                 (unsigned long)(VIN_IMAGE_WIDTH / 2),
                 (unsigned long)(VIN_IMAGE_HEIGHT / 2),
                 center);
        print_to_console(buf);
    }

    /* Bottom-right */
    uint32_t br_offset = (VIN_IMAGE_HEIGHT - 1) * VIN_BYTES_PER_LINE +
                         (VIN_IMAGE_WIDTH - 1) * VIN_BYTES_PER_PIXEL;
    if (br_offset + 1 < VIN_FRAME_SIZE_BYTES) {
        uint16_t br = *(uint16_t *)(p_frame + br_offset);
        snprintf(buf, sizeof(buf), "    [%lu,%lu] : 0x%04X\r\n",
                 (unsigned long)(VIN_IMAGE_WIDTH - 1),
                 (unsigned long)(VIN_IMAGE_HEIGHT - 1),
                 br);
        print_to_console(buf);
    }
}

/**
 * Print camera test command usage help
 */
static void test_cmd_print_help(void)
{
    print_to_console("Usage: camera test <subcommand>\r\n");
    print_to_console("  capture           - Single frame capture with data validation\r\n");
    print_to_console("  capture display   - Single frame capture and display on LCD\r\n");
    print_to_console("  fps [duration_ms] - Measure FPS (default: 3000 ms)\r\n");
    print_to_console("  stream [dur_ms]   - Continuous capture + LCD display (default: 10000 ms)\r\n");
    print_to_console("  validate          - Validate last captured frame data\r\n");
}

/**
 * NT-Shell "camera test" sub-command handler (S-003-4)
 *
 * @details Dispatches camera integration test sub-commands.
 *
 * @param argc  Argument count (from the original camera command)
 * @param argv  Argument vector
 */
void camera_test_cmd(int argc, char **argv)
{
    char buf[TEST_PRINT_BUF_SIZE];

    if (argc < 3) {
        test_cmd_print_help();
        return;
    }

    if (ntlibc_strcmp(argv[2], "capture") == 0) {
        test_cmd_capture(argc, argv);
        return;
    }

    if (ntlibc_strcmp(argv[2], "fps") == 0) {
        test_cmd_fps(argc, argv);
        return;
    }

    if (ntlibc_strcmp(argv[2], "stream") == 0) {
        test_cmd_stream(argc, argv);
        return;
    }

    if (ntlibc_strcmp(argv[2], "validate") == 0) {
        test_cmd_validate();
        return;
    }

    /* Unknown test sub-command */
    snprintf(buf, sizeof(buf), "  Error: Unknown test sub-command '%s'.\r\n", argv[2]);
    print_to_console(buf);
    test_cmd_print_help();
}
