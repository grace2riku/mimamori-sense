/**
 * @file csi2_port.c
 * @brief MIPI CSI-2 receive control port layer implementation (S-003-2)
 * @details
 * Implements MIPI CSI-2 protocol layer receive control for the OV5640 camera
 * module on the EK-RA8P1 Camera Expansion Board.
 *
 * This module provides:
 *   - CSI-2 receiver initialization via R_MIPI_CSI_Open() (FSP driver)
 *   - CSI-2 start/stop control via R_MIPI_CSI_Start()/R_MIPI_CSI_Stop()
 *   - CSI-2 callback handler (mipi_csi0_callback) for:
 *     - Frame Start (FS) / Frame End (FE) detection
 *     - Virtual Channel error tracking (ECC, CRC, ID, word count)
 *     - Data Lane error tracking (SOT, sync, control, escape)
 *     - Short packet FIFO overflow detection
 *   - Error and frame counter management
 *   - NT-Shell "camera csi" diagnostic command
 *
 * Initialization flow:
 *   1. csi2_port_init()
 *      a. Check FSP module availability (CSI2_PORT_FSP_AVAILABLE)
 *      b. Call R_MIPI_CSI_Open() with FSP-generated configuration
 *      c. Update status tracking
 *
 * Note: The FSP MIPI CSI module (r_mipi_csi) must be added to configuration.xml
 * before this code can execute. If the module is not configured, all functions
 * return a "no FSP module" status, allowing the system to boot without camera
 * support.
 *
 * Reference:
 *   - camera_thread_entry.c: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:948-1001
 *   - CSI-2 config: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:49-170
 *   - R_MIPI_CSI_Open: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra/fsp/inc/instances/r_mipi_csi.h:62
 *   - CSI-2 types: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra/fsp/src/r_mipi_csi/r_mipi_csi_device_types.h
 *
 * @note
 * This file is part of the CSI-2 receive control (S-003-2) implementation.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>

#include "csi2_port.h"
#include "bsp_api.h"
#include "jlink_console.h"
#include "cmd_utils.h"

/*
 * Check if the FSP MIPI CSI module is available.
 *
 * When the MIPI CSI module is configured in configuration.xml, the FSP code
 * generator creates g_mipi_csi0 instance in common_data.c/h and includes
 * the r_mipi_csi.h header.
 *
 * For now, we use a compile-time feature flag. When the FSP module is added,
 * this flag should be set to 1 by the build system or defined before this include.
 */
#ifndef CSI2_PORT_FSP_AVAILABLE
#define CSI2_PORT_FSP_AVAILABLE     (1)
#endif

#if CSI2_PORT_FSP_AVAILABLE
#include "r_mipi_csi.h"
#include "r_mipi_csi_api.h"
#include "common_data.h"
#endif

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size */
#define CSI2_PRINT_BUF_SIZE         (128)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/** Current CSI-2 initialization status */
static csi2_status_t s_csi2_status = CSI2_STATUS_NOT_INITIALIZED;

/** CSI-2 error counters (updated from ISR context) */
static csi2_error_counters_t s_error_counters;

/** CSI-2 frame counters (updated from ISR context) */
static csi2_frame_counters_t s_frame_counters;

/** Total callback invocation count */
static volatile uint32_t s_callback_count = 0;

/**********************************************************************************************************************
 Private (static) functions prototypes
 *********************************************************************************************************************/

static const char *csi2_data_type_to_str(uint32_t data_type);
static const char *csi2_status_to_str(csi2_status_t status);

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Convert CSI-2 data type ID to human-readable string
 */
static const char *csi2_data_type_to_str(uint32_t data_type)
{
    switch (data_type) {
        case CSI2_DATA_TYPE_YUV422_8BIT: return "YUV422 8-bit";
        case CSI2_DATA_TYPE_RGB565:      return "RGB565";
        case CSI2_DATA_TYPE_RGB888:      return "RGB888";
        case CSI2_DATA_TYPE_RAW8:        return "RAW8";
        case CSI2_DATA_TYPE_RAW10:       return "RAW10";
        default:                          return "Unknown";
    }
}

/**
 * Convert CSI-2 status to human-readable string
 */
static const char *csi2_status_to_str(csi2_status_t status)
{
    switch (status) {
        case CSI2_STATUS_INITIALIZED:    return "Initialized";
        case CSI2_STATUS_RECEIVING:      return "Receiving";
        case CSI2_STATUS_STOPPED:        return "Stopped";
        case CSI2_STATUS_ERROR:          return "ERROR";
        case CSI2_STATUS_NO_FSP_MODULE:  return "FSP module not configured";
        default:                          return "Not initialized";
    }
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Initialize the MIPI CSI-2 receiver (S-003-2)
 *
 * @details Performs CSI-2 receiver initialization:
 *   1. Check if FSP MIPI CSI module is available
 *   2. Call R_MIPI_CSI_Open() with FSP-generated configuration
 *   3. Reset error and frame counters
 *
 * The R_MIPI_CSI_Open() function internally:
 *   a. Configures the MIPI PHY (calls R_MIPI_PHY_Open if not already open)
 *   b. Sets the lane count (2 lanes for OV5640)
 *   c. Configures Virtual Channel interrupt masks
 *   d. Sets ECC check mode (24-bit)
 *   e. Configures data type enable (YUV422 8-bit)
 *   f. Enables interrupt sources (VC, DL, PM, GST)
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:49-170
 */
bool csi2_port_init(void)
{
#if CSI2_PORT_FSP_AVAILABLE
    fsp_err_t err;

    /* Reset counters */
    memset((void *)&s_error_counters, 0, sizeof(s_error_counters));
    memset((void *)&s_frame_counters, 0, sizeof(s_frame_counters));
    s_callback_count = 0;

    /*
     * Open the MIPI CSI-2 driver
     *
     * R_MIPI_CSI_Open() configures the CSI-2 receiver with:
     *   - Lane count: 2 (from g_mipi_csi0_cfg.ctrl_data.control_0_bits.lane_count)
     *   - Virtual Channel error mask: ECC, CRC, frame sync, frame data errors
     *   - Data Lane error mask: SOT, sync, control, escape errors
     *   - Data type enable: YUV422 8-bit
     *   - ECC check mode: 24-bit
     *   - Callback: mipi_csi0_callback
     *
     * Note: In the reference project, R_MIPI_CSI2_Open() is called internally
     * by R_VIN_Open() (see vin_extended_cfg_t.p_mipi_csi_instance). If R_VIN_Open()
     * is called first, this explicit call returns FSP_ERR_ALREADY_OPEN, which we
     * treat as success.
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra/fsp/inc/instances/r_mipi_csi.h:62
     */
    err = g_mipi_csi0.p_api->open(g_mipi_csi0.p_ctrl, g_mipi_csi0.p_cfg);

    if ((FSP_SUCCESS == err) || (FSP_ERR_ALREADY_OPEN == err)) {
        s_csi2_status = CSI2_STATUS_INITIALIZED;
        return true;
    }

    s_csi2_status = CSI2_STATUS_ERROR;
    return false;

#else
    /* FSP MIPI CSI module not configured yet */
    memset((void *)&s_error_counters, 0, sizeof(s_error_counters));
    memset((void *)&s_frame_counters, 0, sizeof(s_frame_counters));
    s_callback_count = 0;
    s_csi2_status = CSI2_STATUS_NO_FSP_MODULE;
    return false;
#endif
}

/**
 * Close the MIPI CSI-2 receiver
 */
bool csi2_port_close(void)
{
#if CSI2_PORT_FSP_AVAILABLE
    fsp_err_t err;

    err = g_mipi_csi0.p_api->close(g_mipi_csi0.p_ctrl);

    if (FSP_SUCCESS == err) {
        s_csi2_status = CSI2_STATUS_NOT_INITIALIZED;
        return true;
    }

    return false;
#else
    s_csi2_status = CSI2_STATUS_NOT_INITIALIZED;
    return true;
#endif
}

/**
 * Start CSI-2 data reception
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra/fsp/inc/instances/r_mipi_csi.h:64
 */
bool csi2_port_start(void)
{
#if CSI2_PORT_FSP_AVAILABLE
    fsp_err_t err;

    err = g_mipi_csi0.p_api->start(g_mipi_csi0.p_ctrl);

    if (FSP_SUCCESS == err) {
        s_csi2_status = CSI2_STATUS_RECEIVING;
        return true;
    }

    return false;
#else
    return false;
#endif
}

/**
 * Stop CSI-2 data reception
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra/fsp/inc/instances/r_mipi_csi.h:65
 */
bool csi2_port_stop(void)
{
#if CSI2_PORT_FSP_AVAILABLE
    fsp_err_t err;

    err = g_mipi_csi0.p_api->stop(g_mipi_csi0.p_ctrl);

    if (FSP_SUCCESS == err) {
        s_csi2_status = CSI2_STATUS_STOPPED;
        return true;
    }

    return false;
#else
    return false;
#endif
}

/**
 * Get the current CSI-2 status
 */
csi2_status_t csi2_port_get_status(void)
{
    return s_csi2_status;
}

/**
 * Check if CSI-2 receiver is available for use
 */
bool csi2_port_is_available(void)
{
    return (s_csi2_status == CSI2_STATUS_INITIALIZED) ||
           (s_csi2_status == CSI2_STATUS_RECEIVING) ||
           (s_csi2_status == CSI2_STATUS_STOPPED);
}

/**
 * Get CSI-2 configuration, status, and statistics
 *
 * @details Fills the info structure with compile-time and runtime values.
 *          Error and frame counters are copied atomically by temporarily
 *          disabling interrupts to ensure consistency.
 */
void csi2_port_get_info(csi2_info_t *info)
{
    if (info == NULL) {
        return;
    }

    info->status          = s_csi2_status;
    info->virtual_channel = CSI2_VIRTUAL_CHANNEL;
    info->num_lanes       = CSI2_NUM_LANES;
    info->data_type       = CSI2_DATA_TYPE_YUV422_8BIT;
    info->data_type_str   = csi2_data_type_to_str(CSI2_DATA_TYPE_YUV422_8BIT);
    info->ecc_24bit       = (CSI2_ECC_CHECK_24BIT != 0);
    info->callback_count  = s_callback_count;

    /* Copy counters (disable interrupts briefly for consistency) */
    __disable_irq();
    memcpy((void *)&info->errors, (const void *)&s_error_counters, sizeof(csi2_error_counters_t));
    memcpy((void *)&info->frames, (const void *)&s_frame_counters, sizeof(csi2_frame_counters_t));
    __enable_irq();

#if CSI2_PORT_FSP_AVAILABLE
    info->csi_open = (g_mipi_csi0_ctrl.open != 0);
#else
    info->csi_open = false;
#endif
}

/**
 * Reset CSI-2 error and frame counters
 */
void csi2_port_reset_counters(void)
{
    __disable_irq();
    memset((void *)&s_error_counters, 0, sizeof(s_error_counters));
    memset((void *)&s_frame_counters, 0, sizeof(s_frame_counters));
    s_callback_count = 0;
    __enable_irq();
}

/**
 * CSI-2 callback handler (called from ISR context)
 *
 * @details This is the application-level callback registered with the FSP
 *          MIPI CSI driver. It is called from the ISR when CSI-2 events occur.
 *
 *          The callback processes five event types:
 *            1. MIPI_CSI_EVENT_VIRTUAL_CHANNEL
 *               - Frame Start/End detection (FS/FE packets)
 *               - ECC errors (1-bit corrected, 2-bit uncorrectable)
 *               - CRC errors, ID errors, word count errors
 *               - Frame sync/data errors, malformed packets
 *
 *            2. MIPI_CSI_EVENT_DATA_LANE
 *               - SOT (Start of Transmission) errors
 *               - SOT synchronization errors
 *               - Control errors, escape errors
 *
 *            3. MIPI_CSI_EVENT_FRAME_DATA
 *               - Frame data events (status tracking)
 *
 *            4. MIPI_CSI_EVENT_POWER
 *               - Power management state changes
 *
 *            5. MIPI_CSI_EVENT_SHORT_PACKET_FIFO
 *               - Short packet FIFO overflow
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:948-1001
 *
 * @param p_args Pointer to mipi_csi_callback_args_t from FSP driver
 */
void csi2_port_callback(void *p_args)
{
#if CSI2_PORT_FSP_AVAILABLE
    mipi_csi_callback_args_t *p_cb = (mipi_csi_callback_args_t *)p_args;

    s_callback_count++;

    switch (p_cb->event)
    {
        /**
         * Virtual Channel event
         *
         * Contains per-VC status including frame start/end detection and
         * packet error flags (ECC, CRC, ID, word count, frame sync).
         *
         * Reference: r_mipi_csi_device_types.h:150-175 (mipi_csi_virtual_channel_status_t)
         */
        case MIPI_CSI_EVENT_VIRTUAL_CHANNEL:
        {
            mipi_csi_virtual_channel_status_t vc_status;
            vc_status.mask = p_cb->event_data.data_lane_status.mask;

            /* Frame Start/End detection */
            if (vc_status.bits.frame_start) {
                s_frame_counters.frame_start++;
            }
            if (vc_status.bits.frame_end) {
                s_frame_counters.frame_end++;
            }
            if (vc_status.bits.line_start) {
                s_frame_counters.line_start++;
            }
            if (vc_status.bits.line_end) {
                s_frame_counters.line_end++;
            }

            /* ECC error tracking */
            if (vc_status.bits.err_ecc_1_bit) {
                s_error_counters.ecc_corrected++;
            }
            if (vc_status.bits.err_ecc_2_bit) {
                s_error_counters.ecc_uncorrected++;
            }

            /* CRC error */
            if (vc_status.bits.err_crc) {
                s_error_counters.crc_error++;
            }

            /* ID error */
            if (vc_status.bits.err_id) {
                s_error_counters.id_error++;
            }

            /* Word count error */
            if (vc_status.bits.err_word_count) {
                s_error_counters.word_count_error++;
            }

            /* Malformed packet */
            if (vc_status.bits.malformed) {
                s_error_counters.malformed_packet++;
            }

            /* Frame sync error */
            if (vc_status.bits.err_frame_sync) {
                s_error_counters.frame_sync_error++;
            }

            /* Frame data error */
            if (vc_status.bits.err_frame_data) {
                s_error_counters.frame_data_error++;
            }

            /* Short packet FIFO overflow */
            if (vc_status.bits.short_packet_overflow) {
                s_error_counters.fifo_overflow++;
            }

            break;
        }

        /**
         * Data Lane event
         *
         * Contains per-lane error status (SOT, sync, control, escape).
         *
         * Reference: r_mipi_csi_device_types.h:131-148 (mipi_csi_data_lane_status_t)
         */
        case MIPI_CSI_EVENT_DATA_LANE:
        {
            mipi_csi_data_lane_status_t dl_status = p_cb->event_data.data_lane_status;

            if (dl_status.bits.err_sot_hs) {
                s_error_counters.sot_error++;
            }
            if (dl_status.bits.err_sot_sync) {
                s_error_counters.sot_sync_error++;
            }
            if (dl_status.bits.err_control) {
                s_error_counters.control_error++;
            }
            if (dl_status.bits.err_escape) {
                s_error_counters.escape_error++;
            }

            break;
        }

        /**
         * Frame Data event
         *
         * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:962-967
         */
        case MIPI_CSI_EVENT_FRAME_DATA:
        {
            /* Frame data event: currently tracked by VC frame start/end */
            break;
        }

        /**
         * Power Management event
         *
         * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:969-974
         */
        case MIPI_CSI_EVENT_POWER:
        {
            /* Power state change: no action required for basic operation */
            break;
        }

        /**
         * Short Packet FIFO event
         *
         * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:976-981
         */
        case MIPI_CSI_EVENT_SHORT_PACKET_FIFO:
        {
            mipi_csi_short_packet_fifo_status_t fifo_status;
            fifo_status.mask = p_cb->event_data.data_lane_status.mask;

            if (fifo_status.bits.overflow) {
                s_error_counters.fifo_overflow++;
            }

            break;
        }

        default:
            break;
    }

#else
    (void)p_args;
#endif
}

/**
 * NT-Shell "camera csi" sub-command handler
 *
 * @details Displays CSI-2 receiver status, configuration, error counters,
 *          and frame counters.
 */
void csi2_cmd_status(void)
{
    char buf[CSI2_PRINT_BUF_SIZE];
    csi2_info_t info;

    csi2_port_get_info(&info);

    /* Status section */
    print_to_console("[MIPI CSI-2 Receiver Status (S-003-2)]\r\n");

    snprintf(buf, sizeof(buf), "  Status       : %s\r\n", csi2_status_to_str(info.status));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  CSI Open     : %s\r\n", info.csi_open ? "Yes" : "No");
    print_to_console(buf);

    /* Configuration section */
    print_to_console("[Configuration]\r\n");

    snprintf(buf, sizeof(buf), "  Data Lanes   : %lu\r\n", (unsigned long)info.num_lanes);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Virtual Ch   : VC%lu\r\n", (unsigned long)info.virtual_channel);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Data Type    : 0x%02lX (%s)\r\n",
             (unsigned long)info.data_type, info.data_type_str);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  ECC Check    : %s\r\n", info.ecc_24bit ? "24-bit" : "26-bit");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  FRR Clock    : %u\r\n", CSI2_FRRCLK);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  FRR Skew     : %u\r\n", CSI2_FRRSKW);
    print_to_console(buf);

    /* Frame counters section */
    print_to_console("[Frame Counters]\r\n");

    snprintf(buf, sizeof(buf), "  Frame Start  : %lu\r\n", (unsigned long)info.frames.frame_start);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Frame End    : %lu\r\n", (unsigned long)info.frames.frame_end);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Line Start   : %lu\r\n", (unsigned long)info.frames.line_start);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Line End     : %lu\r\n", (unsigned long)info.frames.line_end);
    print_to_console(buf);

    /* Error counters section */
    print_to_console("[Error Counters]\r\n");

    snprintf(buf, sizeof(buf), "  ECC 1-bit    : %lu (corrected)\r\n",
             (unsigned long)info.errors.ecc_corrected);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  ECC 2-bit    : %lu (uncorrectable)\r\n",
             (unsigned long)info.errors.ecc_uncorrected);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  CRC Error    : %lu\r\n", (unsigned long)info.errors.crc_error);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  SOT Error    : %lu\r\n", (unsigned long)info.errors.sot_error);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  SOT Sync Err : %lu\r\n", (unsigned long)info.errors.sot_sync_error);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  ID Error     : %lu\r\n", (unsigned long)info.errors.id_error);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  WC Error     : %lu\r\n", (unsigned long)info.errors.word_count_error);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Malformed    : %lu\r\n", (unsigned long)info.errors.malformed_packet);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Frame Sync   : %lu\r\n", (unsigned long)info.errors.frame_sync_error);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Frame Data   : %lu\r\n", (unsigned long)info.errors.frame_data_error);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Control Err  : %lu\r\n", (unsigned long)info.errors.control_error);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Escape Err   : %lu\r\n", (unsigned long)info.errors.escape_error);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  FIFO Overflow: %lu\r\n", (unsigned long)info.errors.fifo_overflow);
    print_to_console(buf);

    /* Callback statistics */
    print_to_console("[Callback Statistics]\r\n");

    snprintf(buf, sizeof(buf), "  Total calls  : %lu\r\n", (unsigned long)info.callback_count);
    print_to_console(buf);

    /* FSP module status */
    print_to_console("[FSP Module]\r\n");
#if CSI2_PORT_FSP_AVAILABLE
    print_to_console("  r_mipi_csi   : Available\r\n");
#else
    print_to_console("  r_mipi_csi   : NOT CONFIGURED\r\n");
    print_to_console("  Action       : Add MIPI CSI module in configuration.xml\r\n");
    print_to_console("                 (Stacks > New Stack > MIPI CSI)\r\n");
#endif
}

/**
 * NT-Shell "camera csi reset" sub-command handler
 */
void csi2_cmd_reset(void)
{
    csi2_port_reset_counters();
    print_to_console("  CSI-2 error and frame counters reset.\r\n");
}
