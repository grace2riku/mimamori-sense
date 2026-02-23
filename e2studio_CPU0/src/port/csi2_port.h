/**
 * @file csi2_port.h
 * @brief MIPI CSI-2 receive control port layer (S-003-2)
 * @details
 * Provides MIPI CSI-2 protocol layer receive control for the OV5640 camera
 * module on the EK-RA8P1 Camera Expansion Board.
 *
 * CSI-2 Configuration Summary (from reference project):
 *   Data Lanes        : 2
 *   Virtual Channel   : VC0
 *   Data Type          : YUV422 8-bit (OV5640 output format)
 *   ECC Check          : 24-bit mode
 *   Error Detection    : ECC, CRC, SOT, Frame Sync
 *
 * CSI-2 Interrupt Configuration:
 *   - Receive interrupt (RX): Receive start/active detection
 *   - Data Lane interrupt (DL): SOT error, sync error, control error, escape error
 *   - Virtual Channel interrupt (VC): Frame start/end, ECC error, CRC error
 *   - Power Management interrupt (PM): Stop state, ULPS state changes
 *   - Generic Short Packet interrupt (GST): FIFO overflow
 *
 * Initialization Sequence:
 *   1. MIPI D-PHY init (S-003-1: mipi_port.c)
 *   2. CSI-2 Open via R_MIPI_CSI_Open() [this module]
 *   3. VIN Open via R_VIN_Open() (S-003-3: vin_port.c)
 *   4. CSI-2 Start via R_MIPI_CSI_Start()
 *
 * Note: In the reference project, R_MIPI_CSI2_Open() is called internally
 * by R_VIN_Open(). This module provides explicit CSI-2 control and status
 * monitoring for diagnostics and fine-grained error reporting.
 *
 * Dependencies:
 *   - FSP MIPI CSI driver (r_mipi_csi) must be added via configuration.xml
 *   - FSP-generated instances: g_mipi_csi0, g_mipi_csi0_ctrl, g_mipi_csi0_cfg
 *   - MIPI D-PHY must be initialized first (S-003-1)
 *
 * Reference:
 *   - camera_thread_entry.c: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:948-1001
 *   - CSI-2 config: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:49-170
 *   - CSI-2 API: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra/fsp/inc/instances/r_mipi_csi.h
 *   - CSI-2 types: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra/fsp/src/r_mipi_csi/r_mipi_csi_device_types.h
 */

#ifndef CSI2_PORT_H
#define CSI2_PORT_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include "bsp_api.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * CSI-2 Virtual Channel configuration
 *
 * OV5640 is configured for Virtual Channel 0 (VC0).
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:196
 *            reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:806
 */
#define CSI2_VIRTUAL_CHANNEL            (0)

/**
 * CSI-2 Data Lane count
 *
 * OV5640 camera module uses 2-lane MIPI CSI-2 interface.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:54
 */
#define CSI2_NUM_LANES                  (2)

/**
 * CSI-2 Data Type: YUV422 8-bit
 *
 * The OV5640 is configured to output YUV422 8-bit format.
 * The CSI-2 receiver is configured to accept this data type.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:65
 *            reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:96
 */
#define CSI2_DATA_TYPE_YUV422_8BIT      (0x1E)  /* MIPI CSI-2 Data Type ID for YUV422 8-bit */
#define CSI2_DATA_TYPE_RGB565           (0x22)  /* MIPI CSI-2 Data Type ID for RGB565 */
#define CSI2_DATA_TYPE_RGB888           (0x24)  /* MIPI CSI-2 Data Type ID for RGB888 */
#define CSI2_DATA_TYPE_RAW8             (0x2A)  /* MIPI CSI-2 Data Type ID for RAW8 */
#define CSI2_DATA_TYPE_RAW10            (0x2B)  /* MIPI CSI-2 Data Type ID for RAW10 */

/**
 * CSI-2 ECC check mode
 *
 * 24-bit ECC check mode (CSI-2 specification compliant).
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:59
 */
#define CSI2_ECC_CHECK_24BIT            (1)

/**
 * CSI-2 Frequency Rate (FRR) settings
 *
 * These values determine packet reception end timing and data lane skew adjustment.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:62-63
 */
#define CSI2_FRRCLK                     (10)
#define CSI2_FRRSKW                     (10)

/**
 * CSI-2 interrupt priority level
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:67
 */
#define CSI2_INTERRUPT_PRIORITY         (12)

/**********************************************************************************************************************
 Global Typedef definitions
 *********************************************************************************************************************/

/** CSI-2 initialization status */
typedef enum {
    CSI2_STATUS_NOT_INITIALIZED = 0,    /**< Not yet initialized */
    CSI2_STATUS_INITIALIZED,            /**< Successfully initialized (Open done) */
    CSI2_STATUS_RECEIVING,              /**< Currently receiving data */
    CSI2_STATUS_STOPPED,                /**< Stopped (was receiving, now stopped) */
    CSI2_STATUS_ERROR,                  /**< Initialization or runtime error */
    CSI2_STATUS_NO_FSP_MODULE,          /**< FSP module not yet configured */
} csi2_status_t;

/**
 * CSI-2 error counter structure
 *
 * Tracks CSI-2 protocol errors detected by the interrupt handler.
 * All counters are updated from the ISR context (mipi_csi0_callback).
 */
typedef struct {
    volatile uint32_t ecc_corrected;    /**< ECC 1-bit errors (corrected) */
    volatile uint32_t ecc_uncorrected;  /**< ECC 2-bit errors (uncorrectable) */
    volatile uint32_t crc_error;        /**< CRC errors */
    volatile uint32_t sot_error;        /**< SOT (Start of Transmission) errors */
    volatile uint32_t sot_sync_error;   /**< SOT synchronization errors */
    volatile uint32_t id_error;         /**< Packet ID errors */
    volatile uint32_t word_count_error; /**< Word count errors */
    volatile uint32_t malformed_packet; /**< Malformed packet errors */
    volatile uint32_t frame_sync_error; /**< Frame synchronization errors */
    volatile uint32_t frame_data_error; /**< Frame data errors */
    volatile uint32_t control_error;    /**< Data lane control errors */
    volatile uint32_t escape_error;     /**< Data lane escape errors */
    volatile uint32_t fifo_overflow;    /**< Short packet FIFO overflow */
} csi2_error_counters_t;

/**
 * CSI-2 frame counter structure
 *
 * Tracks frame start/end events for monitoring camera streaming.
 */
typedef struct {
    volatile uint32_t frame_start;      /**< Frame Start (FS) packet count */
    volatile uint32_t frame_end;        /**< Frame End (FE) packet count */
    volatile uint32_t line_start;       /**< Line Start (LS) packet count */
    volatile uint32_t line_end;         /**< Line End (LE) packet count */
} csi2_frame_counters_t;

/**
 * CSI-2 information structure
 *
 * Contains the current state, configuration, and statistics of the CSI-2 receiver.
 */
typedef struct {
    csi2_status_t          status;          /**< Current initialization/runtime status */
    uint32_t               virtual_channel; /**< Configured virtual channel ID */
    uint32_t               num_lanes;       /**< Number of data lanes */
    uint32_t               data_type;       /**< Configured data type (YUV422/RGB565 etc.) */
    const char            *data_type_str;   /**< Data type name string */
    bool                   ecc_24bit;       /**< true if 24-bit ECC check mode */
    bool                   csi_open;        /**< true if R_MIPI_CSI_Open() succeeded */
    csi2_error_counters_t  errors;          /**< Error counters (copy, safe to read) */
    csi2_frame_counters_t  frames;          /**< Frame counters (copy, safe to read) */
    uint32_t               callback_count;  /**< Total callback invocation count */
} csi2_info_t;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the MIPI CSI-2 receiver (S-003-2)
 *
 * @details Calls R_MIPI_CSI_Open() with the FSP-generated configuration.
 *          The configuration includes:
 *            - Virtual Channel: VC0
 *            - Data Type: YUV422 8-bit
 *            - ECC check: 24-bit mode
 *            - Interrupt masks for error detection (ECC, CRC, SOT, frame sync)
 *
 *          Note: In the reference project, R_MIPI_CSI2_Open() is called
 *          internally by R_VIN_Open(). If R_VIN_Open() is called first,
 *          this function will return success with FSP_ERR_ALREADY_OPEN.
 *
 * Preconditions:
 *   - MIPI D-PHY must be initialized (S-003-1)
 *   - FSP MIPI CSI module configured in configuration.xml
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:49-170
 *
 * @retval true   CSI-2 receiver initialized successfully
 * @retval false  CSI-2 initialization failed
 */
bool csi2_port_init(void);

/**
 * Close the MIPI CSI-2 receiver
 *
 * @retval true   CSI-2 closed successfully
 * @retval false  CSI-2 close failed
 */
bool csi2_port_close(void);

/**
 * Start CSI-2 data reception
 *
 * @details Calls R_MIPI_CSI_Start() to begin receiving CSI-2 packets.
 *
 * @retval true   CSI-2 reception started
 * @retval false  Failed to start reception
 */
bool csi2_port_start(void);

/**
 * Stop CSI-2 data reception
 *
 * @details Calls R_MIPI_CSI_Stop() to stop receiving CSI-2 packets.
 *
 * @retval true   CSI-2 reception stopped
 * @retval false  Failed to stop reception
 */
bool csi2_port_stop(void);

/**
 * Get the current CSI-2 status
 *
 * @return Current CSI-2 status
 */
csi2_status_t csi2_port_get_status(void);

/**
 * Check if CSI-2 receiver is available for use
 *
 * @retval true   CSI-2 is initialized and ready (or already receiving)
 * @retval false  CSI-2 is not available
 */
bool csi2_port_is_available(void);

/**
 * Get CSI-2 configuration, status, and statistics
 *
 * @details Fills the info structure with current CSI-2 state including
 *          configuration, error counters, and frame counters.
 *
 * @param info Pointer to csi2_info_t structure to fill
 */
void csi2_port_get_info(csi2_info_t *info);

/**
 * Reset CSI-2 error and frame counters
 *
 * @details Clears all error and frame counters to zero.
 *          Useful for starting fresh monitoring after a known-good state.
 */
void csi2_port_reset_counters(void);

/**
 * CSI-2 callback handler (called from ISR context)
 *
 * @details This function is registered as the mipi_csi0_callback in the
 *          FSP-generated configuration. It processes CSI-2 events:
 *            - MIPI_CSI_EVENT_VIRTUAL_CHANNEL: Frame start/end, ECC/CRC errors
 *            - MIPI_CSI_EVENT_DATA_LANE: SOT errors, control errors
 *            - MIPI_CSI_EVENT_FRAME_DATA: Frame data events
 *            - MIPI_CSI_EVENT_POWER: Power management events
 *            - MIPI_CSI_EVENT_SHORT_PACKET_FIFO: FIFO overflow
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:948-1001
 *
 * @param p_args Pointer to callback arguments from FSP driver
 */
void csi2_port_callback(void *p_args);

/**
 * NT-Shell "camera csi" sub-command handler
 *
 * @details Displays CSI-2 receive status, configuration, error counters,
 *          and frame counters.
 */
void csi2_cmd_status(void);

/**
 * NT-Shell "camera csi reset" sub-command handler
 *
 * @details Resets all CSI-2 error and frame counters.
 */
void csi2_cmd_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* CSI2_PORT_H */
