/**
 * @file vin_port.c
 * @brief VIN (Video Input) capture control port layer implementation (S-003-3)
 * @details
 * Implements VIN module initialization, capture control, callback handler,
 * and diagnostic functions for the OV5640 camera module on the EK-RA8P1
 * Camera Expansion Board.
 *
 * This module provides:
 *   - VIN module initialization via R_VIN_Open() (FSP driver)
 *   - Capture start/stop control via R_VIN_CaptureStart()
 *   - VIN callback handler (vin0_callback) for:
 *     - Frame complete detection
 *     - FIFO overflow error tracking
 *     - AXI error tracking
 *   - Capture buffer management (triple buffering in SDRAM)
 *   - Frame and error counter management
 *   - NT-Shell "camera status/info/capture" diagnostic commands
 *
 * Initialization flow:
 *   1. vin_port_init()
 *      a. Check FSP module availability (VIN_PORT_FSP_AVAILABLE)
 *      b. Call R_VIN_Open() with FSP-generated configuration
 *         (internally opens MIPI CSI and MIPI PHY)
 *      c. Reset capture statistics
 *      d. Update status tracking
 *
 * Note: The FSP VIN module (r_vin) must be added to configuration.xml before
 * this code can execute. If the module is not configured, all functions
 * return a "no FSP module" status, allowing the system to boot without
 * camera support.
 *
 * Reference:
 *   - camera_thread_entry.c: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:309-377
 *   - vin0_callback: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:907-941
 *   - VIN config: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:171-309
 *   - VIN API: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra/fsp/inc/instances/r_vin.h
 *   - VIN types: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra/fsp/src/r_vin/r_vin_device_types.h
 *
 * @note
 * This file is part of the VIN capture control (S-003-3) implementation.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>

#include "vin_port.h"
#include "bsp_api.h"
#include "jlink_console.h"
#include "cmd_utils.h"
#include "camera_framebuffer.h"

/*
 * Check if the FSP VIN module is available.
 *
 * When the VIN module is configured in configuration.xml, the FSP code
 * generator creates g_vin0 instance in common_data.c/h and includes
 * the r_vin.h header.
 *
 * For now, we use a compile-time feature flag. When the FSP module is added,
 * this flag should be set to 1 by the build system or defined before this include.
 */
#ifndef VIN_PORT_FSP_AVAILABLE
#define VIN_PORT_FSP_AVAILABLE      (1)
#endif

#if VIN_PORT_FSP_AVAILABLE
#include "r_vin.h"
#include "r_capture_api.h"
#include "common_data.h"
#endif

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size */
#define VIN_PRINT_BUF_SIZE          (128)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/** Current VIN port status */
static vin_port_status_t s_vin_status = VIN_PORT_STATUS_NOT_INITIALIZED;

/** VIN capture statistics (updated from ISR context) */
static vin_capture_stats_t s_capture_stats;

/** Pointer to the most recently completed frame buffer */
static volatile uint8_t *s_last_frame_buffer = NULL;

/** Index of the latest completed buffer (0-2 for triple buffering) */
static volatile uint32_t s_active_buffer_index = 0;

/**********************************************************************************************************************
 Private (static) functions prototypes
 *********************************************************************************************************************/
static const char *vin_status_to_str(vin_port_status_t status);

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Convert VIN port status to human-readable string
 */
static const char *vin_status_to_str(vin_port_status_t status)
{
    switch (status) {
        case VIN_PORT_STATUS_INITIALIZED:    return "Initialized";
        case VIN_PORT_STATUS_CAPTURING:      return "Capturing";
        case VIN_PORT_STATUS_STOPPED:        return "Stopped";
        case VIN_PORT_STATUS_ERROR:          return "ERROR";
        case VIN_PORT_STATUS_NO_FSP_MODULE:  return "FSP module not configured";
        default:                              return "Not initialized";
    }
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Initialize the VIN (Video Input) module (S-003-3)
 *
 * @details Performs VIN module initialization:
 *   1. Check if FSP VIN module is available
 *   2. Call R_VIN_Open() with FSP-generated configuration
 *   3. Reset capture statistics
 *
 * R_VIN_Open() internally calls R_MIPI_CSI_Open() which calls R_MIPI_PHY_Open(),
 * initializing the entire MIPI CSI-2 capture pipeline.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:331
 */
bool vin_port_init(void)
{
#if VIN_PORT_FSP_AVAILABLE
    fsp_err_t err;

    /* Reset statistics */
    memset((void *)&s_capture_stats, 0, sizeof(s_capture_stats));
    s_last_frame_buffer = NULL;
    s_active_buffer_index = 0;

    /*
     * Open the VIN driver
     *
     * R_VIN_Open() configures the VIN module with:
     *   - Input control: YCbCr422 8-bit, preclip (0,0)-(767,449), stride 768
     *   - CSI mode: Virtual Channel 0, Data Type YUV422 8-bit
     *   - Output control: Triple buffer in SDRAM (vin_image_buffer_1/2/3)
     *   - Conversion control: No conversion (pass-through), byte swap enabled
     *   - Interrupt: Frame memory write complete (FME)
     *   - Callback: vin0_callback
     *
     * Internally, R_VIN_Open() also calls:
     *   - R_MIPI_CSI_Open() (which calls R_MIPI_PHY_Open())
     *   - Configures CSI-2 receiver for 2-lane, YUV422 8-bit
     *   - Configures MIPI PHY PLL at 1000 MHz
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:171-309
     */
    err = R_VIN_Open(&g_vin0_ctrl, &g_vin0_cfg);

    if ((FSP_SUCCESS == err) || (FSP_ERR_ALREADY_OPEN == err)) {
        s_vin_status = VIN_PORT_STATUS_INITIALIZED;
        return true;
    }

    s_vin_status = VIN_PORT_STATUS_ERROR;
    return false;

#else
    /* FSP VIN module not configured yet */
    memset((void *)&s_capture_stats, 0, sizeof(s_capture_stats));
    s_last_frame_buffer = NULL;
    s_active_buffer_index = 0;
    s_vin_status = VIN_PORT_STATUS_NO_FSP_MODULE;
    return false;
#endif
}

/**
 * Close the VIN module
 */
bool vin_port_close(void)
{
#if VIN_PORT_FSP_AVAILABLE
    fsp_err_t err;

    err = R_VIN_Close(&g_vin0_ctrl);

    if (FSP_SUCCESS == err) {
        s_vin_status = VIN_PORT_STATUS_NOT_INITIALIZED;
        s_last_frame_buffer = NULL;
        return true;
    }

    return false;
#else
    s_vin_status = VIN_PORT_STATUS_NOT_INITIALIZED;
    s_last_frame_buffer = NULL;
    return true;
#endif
}

/**
 * Start continuous capture
 *
 * @details Calls R_VIN_CaptureStart() with NULL buffer to use the
 *          pre-configured triple buffer round-robin. The VIN module
 *          captures frames continuously and invokes vin0_callback
 *          on each frame complete.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:359
 */
bool vin_port_capture_start(void)
{
#if VIN_PORT_FSP_AVAILABLE
    fsp_err_t err;

    /*
     * Start capture with NULL buffer to use the pre-configured triple buffer.
     * The VIN module internally manages buffer rotation (buffer 0 -> 1 -> 2 -> 0 ...).
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:359
     */
    err = R_VIN_CaptureStart(&g_vin0_ctrl, NULL);

    if (FSP_SUCCESS == err) {
        s_vin_status = VIN_PORT_STATUS_CAPTURING;
        return true;
    }

    return false;
#else
    return false;
#endif
}

/**
 * Stop capture
 *
 * @details The VIN FSP driver does not have a dedicated stop API.
 *          To stop capture, we close and re-open the module.
 */
bool vin_port_capture_stop(void)
{
#if VIN_PORT_FSP_AVAILABLE
    /* Close the VIN module to stop capture */
    fsp_err_t err = R_VIN_Close(&g_vin0_ctrl);
    if (FSP_SUCCESS != err) {
        return false;
    }

    /* Re-open to restore the initialized state (ready for next capture) */
    err = R_VIN_Open(&g_vin0_ctrl, &g_vin0_cfg);
    if ((FSP_SUCCESS == err) || (FSP_ERR_ALREADY_OPEN == err)) {
        s_vin_status = VIN_PORT_STATUS_STOPPED;
        return true;
    }

    s_vin_status = VIN_PORT_STATUS_ERROR;
    return false;
#else
    return false;
#endif
}

/**
 * Get the current VIN status
 */
vin_port_status_t vin_port_get_status(void)
{
    return s_vin_status;
}

/**
 * Check if VIN is available for use
 */
bool vin_port_is_available(void)
{
    return (s_vin_status == VIN_PORT_STATUS_INITIALIZED) ||
           (s_vin_status == VIN_PORT_STATUS_CAPTURING) ||
           (s_vin_status == VIN_PORT_STATUS_STOPPED);
}

/**
 * Get VIN configuration, status, and statistics
 *
 * @details Fills the info structure with current VIN state.
 *          Statistics are copied atomically by temporarily disabling
 *          interrupts to ensure consistency.
 */
void vin_port_get_info(vin_port_info_t *info)
{
    if (info == NULL) {
        return;
    }

    info->status            = s_vin_status;
    info->image_width       = VIN_IMAGE_WIDTH;
    info->image_height      = VIN_IMAGE_HEIGHT;
    info->bytes_per_pixel   = VIN_BYTES_PER_PIXEL;
    info->image_stride      = VIN_IMAGE_STRIDE;
    info->frame_size_bytes  = VIN_FRAME_SIZE_BYTES;
    info->num_buffers       = VIN_NUM_BUFFERS;

#if VIN_PORT_FSP_AVAILABLE
    info->vin_open = (g_vin0_ctrl.open != 0);

    /*
     * Get buffer addresses from the FSP-generated extended configuration.
     * The buffers are allocated in SDRAM .sdram_noinit section.
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:206-207
     */
    {
        const vin_extended_cfg_t *p_extend =
            (const vin_extended_cfg_t *)g_vin0_cfg.p_extend;
        for (uint32_t i = 0; i < VIN_NUM_BUFFERS; i++) {
            info->buffer_addr[i] = (uint32_t)(uintptr_t)p_extend->output_ctrl.image_buffer[i];
        }
    }
#else
    info->vin_open = false;
    for (uint32_t i = 0; i < VIN_NUM_BUFFERS; i++) {
        info->buffer_addr[i] = 0;
    }
#endif

    /* Copy current active buffer index and last frame pointer */
    info->active_buffer = s_active_buffer_index;
    info->p_last_frame  = (uint8_t *)s_last_frame_buffer;

    /* Copy statistics (disable interrupts briefly for consistency) */
    __disable_irq();
    memcpy((void *)&info->stats, (const void *)&s_capture_stats, sizeof(vin_capture_stats_t));
    __enable_irq();
}

/**
 * Reset VIN capture statistics
 */
void vin_port_reset_stats(void)
{
    __disable_irq();
    memset((void *)&s_capture_stats, 0, sizeof(s_capture_stats));
    __enable_irq();
}

/**
 * Get pointer to the most recently completed frame buffer
 */
uint8_t *vin_port_get_last_frame(void)
{
    return (uint8_t *)s_last_frame_buffer;
}

/**
 * Get VIN hardware capture status
 *
 * @details Queries R_VIN_StatusGet() and maps the capture_state_t to a
 *          simplified uint32_t value:
 *            0 = CAPTURE_STATE_IDLE
 *            1 = CAPTURE_STATE_IN_PROGRESS
 *            2 = CAPTURE_STATE_BUSY
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:369-375
 */
bool vin_port_get_hw_status(uint32_t *p_state)
{
#if VIN_PORT_FSP_AVAILABLE
    if (p_state == NULL) {
        return false;
    }

    capture_status_t capture_status;
    fsp_err_t err = R_VIN_StatusGet(&g_vin0_ctrl, &capture_status);

    if (FSP_SUCCESS == err) {
        *p_state = (uint32_t)capture_status.state;
        return true;
    }

    return false;
#else
    (void)p_state;
    return false;
#endif
}

/**
 * VIN callback handler (called from ISR context)
 *
 * @details Processes VIN capture events. This function is registered as
 *          vin0_callback in the FSP-generated configuration.
 *
 * Events:
 *   - VIN_EVENT_NOTIFY: Normal capture event
 *     - frame_complete: A frame has been fully written to memory
 *       -> Updates the last frame buffer pointer
 *       -> Increments frame_complete counter
 *     - fifo_overflow: VIN FIFO overflow (data loss)
 *     - Other status bits tracked for diagnostics
 *
 *   - VIN_EVENT_ERROR: Error condition detected
 *     -> Increments error_event counter
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:907-941
 */
void vin_port_callback(void *p_args)
{
#if VIN_PORT_FSP_AVAILABLE
    capture_callback_args_t *p_cb = (capture_callback_args_t *)p_args;

    s_capture_stats.callback_count++;

    vin_event_t event = (vin_event_t)p_cb->event;
    vin_interrupt_status_t interrupt_status;
    interrupt_status.mask = p_cb->interrupt_status;

    switch (event)
    {
        /**
         * VIN_EVENT_NOTIFY: Normal capture events
         *
         * The interrupt_status field contains the VIN interrupt status register
         * value with individual bit fields for various events.
         *
         * Reference: r_vin_device_types.h:389-412 (vin_interrupt_status_t)
         * Reference: camera_thread_entry.c:916-927
         */
        case VIN_EVENT_NOTIFY:
        {
            s_capture_stats.notify_event++;

            if (interrupt_status.bits.frame_complete) {
                s_capture_stats.frame_complete++;

                /* Update the last frame buffer pointer.
                 * p_cb->p_buffer points to the buffer that was just completed.
                 *
                 * Reference: camera_thread_entry.c:921
                 */
                s_last_frame_buffer = p_cb->p_buffer;

                /* F-002-6: Notify frame buffer manager of the new frame.
                 * This updates the latest-frame pointer and FPS counter.
                 *
                 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:921
                 *            (display_next_buffer_set(p_args->p_buffer))
                 */
                camera_framebuffer_set_latest(p_cb->p_buffer);
            }

            if (interrupt_status.bits.end_of_frame) {
                s_capture_stats.end_of_frame++;
            }

            if (interrupt_status.bits.fifo_overfow) {
                s_capture_stats.fifo_overflow++;
            }

            if (interrupt_status.bits.preclip_h_err) {
                s_capture_stats.preclip_h_error++;
            }

            if (interrupt_status.bits.preclip_v_err) {
                s_capture_stats.preclip_v_error++;
            }

            if (interrupt_status.bits.axi_err) {
                s_capture_stats.axi_error++;
            }

            if (interrupt_status.bits.resp_overflow) {
                s_capture_stats.resp_overflow++;
            }

            break;
        }

        /**
         * VIN_EVENT_ERROR: Error condition
         *
         * Reference: camera_thread_entry.c:929-933
         */
        case VIN_EVENT_ERROR:
        {
            s_capture_stats.error_event++;
            break;
        }

        default:
        {
            /* Do nothing */
            break;
        }
    }

#else
    (void)p_args;
#endif
}

/**
 * NT-Shell "camera status" sub-command handler
 *
 * @details Displays VIN capture status, image configuration, buffer layout,
 *          and capture statistics.
 */
void vin_cmd_status(void)
{
    char buf[VIN_PRINT_BUF_SIZE];
    vin_port_info_t info;

    vin_port_get_info(&info);

    /* Status section */
    print_to_console("[VIN Capture Status (S-003-3)]\r\n");

    snprintf(buf, sizeof(buf), "  Status        : %s\r\n", vin_status_to_str(info.status));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  VIN Open      : %s\r\n", info.vin_open ? "Yes" : "No");
    print_to_console(buf);

    /* Hardware capture state */
#if VIN_PORT_FSP_AVAILABLE
    {
        uint32_t hw_state;
        if (vin_port_get_hw_status(&hw_state)) {
            const char *state_str;
            switch (hw_state) {
                case 0:  state_str = "IDLE";        break;
                case 1:  state_str = "IN_PROGRESS"; break;
                case 2:  state_str = "BUSY";        break;
                default: state_str = "UNKNOWN";     break;
            }
            snprintf(buf, sizeof(buf), "  HW State      : %s\r\n", state_str);
            print_to_console(buf);
        }
    }
#endif

    /* Image configuration */
    print_to_console("[Image Configuration]\r\n");

    snprintf(buf, sizeof(buf), "  Resolution    : %lu x %lu\r\n",
             (unsigned long)info.image_width, (unsigned long)info.image_height);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Bytes/Pixel   : %lu\r\n", (unsigned long)info.bytes_per_pixel);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Stride        : %lu pixels\r\n", (unsigned long)info.image_stride);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Frame Size    : %lu bytes (%lu KB)\r\n",
             (unsigned long)info.frame_size_bytes,
             (unsigned long)(info.frame_size_bytes / 1024));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Input Format  : YCbCr422 8-bit (0x%02X)\r\n",
             (unsigned int)VIN_DATA_TYPE);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Virtual Ch    : VC%u\r\n", (unsigned int)VIN_VIRTUAL_CHANNEL);
    print_to_console(buf);

    /* Buffer layout */
    print_to_console("[Capture Buffers (SDRAM)]\r\n");

    snprintf(buf, sizeof(buf), "  Buffer Count  : %lu (triple buffering)\r\n",
             (unsigned long)info.num_buffers);
    print_to_console(buf);

    for (uint32_t i = 0; i < info.num_buffers; i++) {
        if (info.buffer_addr[i] != 0) {
            snprintf(buf, sizeof(buf), "  Buffer[%lu]     : 0x%08lX - 0x%08lX\r\n",
                     (unsigned long)i,
                     (unsigned long)info.buffer_addr[i],
                     (unsigned long)(info.buffer_addr[i] + info.frame_size_bytes - 1));
        } else {
            snprintf(buf, sizeof(buf), "  Buffer[%lu]     : (not allocated)\r\n",
                     (unsigned long)i);
        }
        print_to_console(buf);
    }

    /* Last frame pointer */
    if (info.p_last_frame != NULL) {
        snprintf(buf, sizeof(buf), "  Last Frame    : 0x%08lX\r\n",
                 (unsigned long)(uintptr_t)info.p_last_frame);
    } else {
        snprintf(buf, sizeof(buf), "  Last Frame    : (none)\r\n");
    }
    print_to_console(buf);

    /* Capture statistics */
    print_to_console("[Capture Statistics]\r\n");

    snprintf(buf, sizeof(buf), "  Frame Complete: %lu\r\n",
             (unsigned long)info.stats.frame_complete);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  End of Frame  : %lu\r\n",
             (unsigned long)info.stats.end_of_frame);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Notify Events : %lu\r\n",
             (unsigned long)info.stats.notify_event);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Total Callback: %lu\r\n",
             (unsigned long)info.stats.callback_count);
    print_to_console(buf);

    /* Error statistics */
    print_to_console("[Error Statistics]\r\n");

    snprintf(buf, sizeof(buf), "  FIFO Overflow : %lu\r\n",
             (unsigned long)info.stats.fifo_overflow);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Preclip H Err : %lu\r\n",
             (unsigned long)info.stats.preclip_h_error);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Preclip V Err : %lu\r\n",
             (unsigned long)info.stats.preclip_v_error);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  AXI Error     : %lu\r\n",
             (unsigned long)info.stats.axi_error);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Resp Overflow : %lu\r\n",
             (unsigned long)info.stats.resp_overflow);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Error Events  : %lu\r\n",
             (unsigned long)info.stats.error_event);
    print_to_console(buf);

    /* FSP module status */
    print_to_console("[FSP Module]\r\n");
#if VIN_PORT_FSP_AVAILABLE
    print_to_console("  r_vin         : Available\r\n");
#else
    print_to_console("  r_vin         : NOT CONFIGURED\r\n");
    print_to_console("  Action        : Add VIN module in configuration.xml\r\n");
    print_to_console("                  (Stacks > New Stack > VIN)\r\n");
#endif
}

/**
 * NT-Shell "camera info" sub-command handler
 *
 * @details Displays camera module information and VIN pipeline overview.
 */
void vin_cmd_info(void)
{
    char buf[VIN_PRINT_BUF_SIZE];

    print_to_console("[Camera Module Information]\r\n");
    print_to_console("  Sensor        : OmniVision OV5640\r\n");
    print_to_console("  Max Resolution: 2592 x 1944 (5MP)\r\n");

    snprintf(buf, sizeof(buf), "  Capture Res   : %u x %u\r\n",
             (unsigned int)VIN_IMAGE_WIDTH, (unsigned int)VIN_IMAGE_HEIGHT);
    print_to_console(buf);

    print_to_console("  Output Format : YUV422 8-bit\r\n");
    print_to_console("  Interface     : MIPI CSI-2 (2-lane)\r\n");
    print_to_console("  Control IF    : I2C (SCCB compatible)\r\n");
    print_to_console("  I2C Address   : 0x3C (7-bit)\r\n");

    print_to_console("[Capture Pipeline]\r\n");
    print_to_console("  OV5640 -> MIPI D-PHY (2-lane)\r\n");
    print_to_console("        -> CSI-2 Rx (VC0, YUV422)\r\n");
    print_to_console("        -> VIN (preclip, DMA)\r\n");
    print_to_console("        -> SDRAM Buffer (triple)\r\n");
    print_to_console("        -> Display / AI\r\n");

    print_to_console("[VIN Configuration]\r\n");

    snprintf(buf, sizeof(buf), "  Preclip       : (%u,%u) - (%u,%u)\r\n",
             (unsigned int)VIN_PRECLIP_PIXEL_START,
             (unsigned int)VIN_PRECLIP_LINE_START,
             (unsigned int)VIN_PRECLIP_PIXEL_END,
             (unsigned int)VIN_PRECLIP_LINE_END);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Stride        : %u pixels\r\n",
             (unsigned int)VIN_IMAGE_STRIDE);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Buffer Size   : %lu bytes (%lu KB)\r\n",
             (unsigned long)VIN_FRAME_SIZE_BYTES,
             (unsigned long)(VIN_FRAME_SIZE_BYTES / 1024));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Total SDRAM   : %lu bytes (%lu KB) for %u buffers\r\n",
             (unsigned long)(VIN_FRAME_SIZE_BYTES * VIN_NUM_BUFFERS),
             (unsigned long)((VIN_FRAME_SIZE_BYTES * VIN_NUM_BUFFERS) / 1024),
             (unsigned int)VIN_NUM_BUFFERS);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Alignment     : %u bytes\r\n",
             (unsigned int)VIN_BUFFER_ALIGNMENT);
    print_to_console(buf);

    print_to_console("  Conversion    : None (pass-through)\r\n");
    print_to_console("  Byte Swap     : Enabled\r\n");
    print_to_console("  IRQ Enable    : Frame Complete (FME)\r\n");
}
