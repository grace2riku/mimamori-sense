/**
 * @file camera_thread_entry.c
 * @brief Camera capture FreeRTOS thread entry and FSP callbacks (F-002-5)
 * @details
 * FreeRTOS thread that manages the entire camera capture pipeline:
 *   1. OV5640 sensor initialization (XCLK start, power-up, reset, register config)
 *   2. VIN driver open (which internally opens MIPI CSI and MIPI PHY)
 *   3. Continuous frame capture with triple buffering in SDRAM
 *   4. Capture state monitoring in the main loop
 *
 * This file also provides the FSP-required callback functions:
 *   - vin0_callback()       : VIN capture frame complete / error handler
 *   - mipi_csi0_callback()  : MIPI CSI-2 error event handler
 *
 * These callbacks are declared in ra_gen/common_data.h and registered in the
 * FSP-generated VIN/CSI configuration. They forward events to the port layer
 * functions (vin_port_callback, csi2_port_callback) which handle statistics
 * tracking and diagnostics.
 *
 * FSP Instance Mapping (current project):
 *   VIN:       g_vin0_ctrl, g_vin0_cfg           (common_data.h)
 *   MIPI CSI:  g_mipi_csi0_ctrl, g_mipi_csi0_cfg (common_data.h)
 *   MIPI PHY:  g_mipi_phy0_ctrl, g_mipi_phy0_cfg (common_data.h)
 *   GPT Timer: g_timer_camera_xclk_ctrl/cfg       (hal_data.h)
 *   I2C:       g_i2c_master_camera_ctrl/cfg        (hal_data.h)
 *
 * Thread configuration (from configuration.xml):
 *   Symbol    : camera_thread
 *   Name      : "Camera Thread"
 *   Stack size: 4096 bytes
 *   Priority  : 4
 *   Allocation: Static
 *
 * Reference:
 *   reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:309-377
 *   reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:907-1001
 *
 * Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>

#include "camera_thread.h"
#include "camera_thread_api.h"
#include "hal_data.h"
#include "common_data.h"
#include "bsp_api.h"

#include "ov5640.h"
#include "ov5640_cfg.h"
#include "jlink_console.h"
#include "r_typedefs.h"

#include "port/vin_port.h"
#include "port/csi2_port.h"
#include "port/mipi_port.h"

#include "FreeRTOS.h"
#include "task.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size for camera thread log messages */
#define CAMERA_PRINT_BUF_SIZE   (128)

/** Delay between VIN CaptureStart and stream_on (ms) */
#define CAMERA_CAPTURE_SETTLE_MS (1)

/** Main loop polling interval (ticks) */
#define CAMERA_POLL_INTERVAL    (1)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/** Camera thread initialization state */
static volatile bool s_camera_initialized = false;

/** Camera thread error flag (set if init fails, thread blocks) */
static volatile bool s_camera_init_error = false;

/**********************************************************************************************************************
 Private (static) function prototypes
 *********************************************************************************************************************/
static void camera_thread_log(const char *msg);
static void camera_thread_log_fsp_err(const char *context, fsp_err_t err);

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Camera Thread entry function
 *
 * @details
 * Performs the full camera capture pipeline initialization:
 *
 * 1. OV5640 sensor initialization:
 *    Calls ov5640_init() which handles XCLK start, I2C open, power-up,
 *    reset, chip ID verification, and register configuration (~500ms).
 *
 * 2. VIN driver open:
 *    R_VIN_Open() initializes VIN + MIPI CSI + MIPI PHY in one call.
 *    The FSP VIN driver internally chains the CSI and PHY initialization.
 *
 * 3. Capture start:
 *    Stops OV5640 stream, starts VIN continuous capture, then starts
 *    OV5640 stream. This ordering ensures VIN is ready before data arrives.
 *
 * 4. Main loop:
 *    Polls VIN capture status at 1-tick intervals for diagnostics.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:309-377
 *
 * @param pvParameters FreeRTOS task parameter (unused)
 */
void camera_thread_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    fsp_err_t err;
    char buf[CAMERA_PRINT_BUF_SIZE];

    s_camera_initialized = false;
    s_camera_init_error = false;

    /*
     * Wait for JLink console to be ready so we can log initialization progress.
     * The ntshell_thread initializes the UART console; we wait until it is
     * configured before attempting to print.
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:325-329
     */
    while (!jlink_configured())
    {
        vTaskDelay(100);
    }

    camera_thread_log("Camera thread started.\r\n");

    /* ======================================================================
     * Step 1: OV5640 sensor initialization
     *
     * ov5640_init() performs:
     *   - XCLK generation start (GPT timer -> 24 MHz)
     *   - I2C master open
     *   - GPIO power-down exit and hardware reset sequence (~400ms)
     *   - Chip ID verification (registers 0x300A/0x300B)
     *   - Software reset
     *   - Full register configuration (100+ registers)
     *   - PLL clock configuration for target MIPI clock
     *   - MIPI virtual channel setup
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:354-356
     * ====================================================================== */
    camera_thread_log("  Initializing OV5640 sensor...\r\n");

    err = ov5640_init();
    if (FSP_SUCCESS != err)
    {
        const ov5640_status_t *st = ov5640_get_status();
        snprintf(buf, sizeof(buf),
                 "  ERROR: OV5640 init failed. Chip ID: 0x%04X, Errors: %lu\r\n",
                 st->chip_id, (unsigned long)st->init_error_count);
        camera_thread_log(buf);
        s_camera_init_error = true;
        goto camera_init_failed;
    }

    {
        const ov5640_status_t *st = ov5640_get_status();
        snprintf(buf, sizeof(buf),
                 "  OV5640 initialized. Chip ID: 0x%04X\r\n", st->chip_id);
        camera_thread_log(buf);
    }

    /* ======================================================================
     * Step 2: VIN driver open
     *
     * R_VIN_Open() initializes the Video Input module and internally opens
     * the MIPI CSI-2 receiver and MIPI D-PHY via the FSP driver chain:
     *   R_VIN_Open -> R_MIPI_CSI2_Open -> R_MIPI_PHY_Open
     *
     * The VIN is configured for:
     *   - YCbCr422 8-bit input (from OV5640 via MIPI CSI-2)
     *   - Triple buffering in SDRAM (vin_image_buffer_1/2/3)
     *   - Frame complete interrupt (vin0_callback)
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:331
     * ====================================================================== */
    camera_thread_log("  Opening VIN driver...\r\n");

    err = R_VIN_Open(&g_vin0_ctrl, &g_vin0_cfg);
    if (FSP_SUCCESS != err)
    {
        camera_thread_log_fsp_err("R_VIN_Open", err);
        s_camera_init_error = true;
        goto camera_init_failed;
    }

    camera_thread_log("  VIN driver opened (VIN + MIPI CSI + MIPI PHY).\r\n");

    /* ======================================================================
     * Step 3: Start capture
     *
     * The capture start sequence is:
     *   1. Stop OV5640 stream output (ensure no data on MIPI lanes)
     *   2. Software power-down OV5640 (register 0x3008 = 0x42)
     *   3. Start VIN continuous capture (R_VIN_CaptureStart)
     *   4. Wake up OV5640 (register 0x3008 = 0x02)
     *   5. Start OV5640 stream output
     *
     * This ordering ensures the VIN is ready to receive data before
     * the OV5640 starts transmitting frames on the MIPI CSI-2 bus.
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:355-363
     * ====================================================================== */
    camera_thread_log("  Starting capture...\r\n");

    ov5640_stream_off();
    ov5640_write_reg(OV5640_REG_SYS_CTRL, 0x42);  /* Software power-down */

    R_BSP_SoftwareDelay(CAMERA_CAPTURE_SETTLE_MS, BSP_DELAY_UNITS_MILLISECONDS);

    err = R_VIN_CaptureStart(&g_vin0_ctrl, NULL);
    if (FSP_SUCCESS != err)
    {
        camera_thread_log_fsp_err("R_VIN_CaptureStart", err);
        s_camera_init_error = true;
        goto camera_init_failed;
    }

    R_BSP_SoftwareDelay(CAMERA_CAPTURE_SETTLE_MS, BSP_DELAY_UNITS_MILLISECONDS);

    ov5640_write_reg(OV5640_REG_SYS_CTRL, 0x02);  /* Wake up from power-down */
    ov5640_stream_on();

    s_camera_initialized = true;
    camera_thread_log("  Camera capture started successfully.\r\n");
    camera_thread_log("  Camera initialization complete.\r\n");

    /* ======================================================================
     * Step 4: Main loop - Capture state monitoring
     *
     * Continuously polls VIN capture status. In normal operation,
     * the state should remain CAPTURE_STATE_IN_PROGRESS.
     * Frame data arrives asynchronously via vin0_callback.
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:367-376
     * ====================================================================== */
    while (1)
    {
        capture_status_t capture_status;
        R_VIN_StatusGet(&g_vin0_ctrl, &capture_status);

        if (capture_status.state == CAPTURE_STATE_IN_PROGRESS)
        {
            /*
             * Capture is running normally.
             * Frame completion is handled by vin0_callback in ISR context.
             * Future: inter-frame processing can be added here.
             */
        }

        vTaskDelay(CAMERA_POLL_INTERVAL);
    }

    /* Not reached during normal operation */
    return;

camera_init_failed:
    /*
     * Initialization failed - block the thread.
     *
     * The camera thread remains alive but idle so that NT-Shell
     * "camera status" can still report the error state.
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:495-498
     */
    camera_thread_log("  ERROR: Camera initialization failed. Thread blocked.\r\n");
    while (1)
    {
        vTaskDelay(1000);
    }
}

/**********************************************************************************************************************
 * Function Name: vin0_callback
 * Description  : VIN capture callback handler (FSP-registered)
 *
 * @details
 * This function is declared in ra_gen/common_data.h and registered as the
 * VIN callback in the FSP-generated configuration (g_vin0_cfg.p_callback).
 * It is called from ISR context when a VIN event occurs.
 *
 * Events are forwarded to vin_port_callback() in the port layer which
 * handles frame complete tracking, error statistics, and buffer management.
 *
 * Events:
 *   - VIN_EVENT_NOTIFY
 *     - frame_complete: A complete frame has been written to SDRAM buffer
 *     - fifo_overflow: VIN FIFO overflow (data loss, typically bandwidth issue)
 *     - end_of_frame: End of frame marker detected
 *   - VIN_EVENT_ERROR
 *     - AXI bus error or other hardware error
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:907-941
 *
 * @param p_args Pointer to capture_callback_args_t from FSP VIN driver
 *********************************************************************************************************************/
void vin0_callback(capture_callback_args_t *p_args)
{
    /* Forward to port layer handler which tracks statistics and buffer state */
    vin_port_callback((void *)p_args);
}

/**********************************************************************************************************************
 * Function Name: mipi_csi0_callback
 * Description  : MIPI CSI-2 callback handler (FSP-registered)
 *
 * @details
 * This function is declared in ra_gen/common_data.h and registered as the
 * MIPI CSI callback in the FSP-generated configuration (g_mipi_csi0_cfg.p_callback).
 * It is called from ISR context when a MIPI CSI-2 event occurs.
 *
 * Events are forwarded to csi2_port_callback() in the port layer which
 * handles error counting, frame sync tracking, and diagnostics.
 *
 * Events:
 *   - MIPI_CSI_EVENT_DATA_LANE: Data lane errors (ECC, escape, control)
 *   - MIPI_CSI_EVENT_FRAME_DATA: Frame data errors
 *   - MIPI_CSI_EVENT_VIRTUAL_CHANNEL: Virtual channel status (frame start/end, errors)
 *   - MIPI_CSI_EVENT_POWER: Power management events
 *   - MIPI_CSI_EVENT_SHORT_PACKET_FIFO: Short packet FIFO overflow
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:948-1001
 *
 * @param p_args Pointer to mipi_csi_callback_args_t from FSP MIPI CSI driver
 *********************************************************************************************************************/
void mipi_csi0_callback(mipi_csi_callback_args_t *p_args)
{
    /* Forward to port layer handler which tracks error counters and frame events */
    csi2_port_callback((void *)p_args);
}

/**********************************************************************************************************************
 * Function Name: camera_thread_is_initialized
 * Description  : Check if camera thread has completed initialization
 *
 * @details Thread-safe query of camera initialization state.
 *          Can be called from other threads (e.g., ntshell_thread via
 *          "camera status" command, or lvgl_thread for display updates).
 *
 * @retval true  Camera thread has completed initialization and capture is running
 * @retval false Camera thread has not yet completed initialization or init failed
 *********************************************************************************************************************/
bool camera_thread_is_initialized(void)
{
    return s_camera_initialized;
}

/**********************************************************************************************************************
 * Function Name: camera_thread_has_error
 * Description  : Check if camera thread encountered an initialization error
 *
 * @details Thread-safe query of camera error state.
 *
 * @retval true  Camera initialization failed
 * @retval false No initialization error (may still be initializing)
 *********************************************************************************************************************/
bool camera_thread_has_error(void)
{
    return s_camera_init_error;
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Log a message to the console from camera thread context
 *
 * @param msg Null-terminated message string
 */
static void camera_thread_log(const char *msg)
{
    print_to_console((char_t *)msg);
}

/**
 * Log an FSP error with context description
 *
 * @param context Description of the operation that failed
 * @param err     FSP error code
 */
static void camera_thread_log_fsp_err(const char *context, fsp_err_t err)
{
    char buf[CAMERA_PRINT_BUF_SIZE];
    snprintf(buf, sizeof(buf), "  ERROR: %s failed (err=0x%lX).\r\n",
             context, (unsigned long)err);
    camera_thread_log(buf);
}
