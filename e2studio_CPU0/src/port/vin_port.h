/**
 * @file vin_port.h
 * @brief VIN (Video Input) capture control port layer (S-003-3)
 * @details
 * Provides VIN (Video Input) module capture control for the OV5640 camera
 * module on the EK-RA8P1 Camera Expansion Board.
 *
 * The VIN module captures image data received via the CSI-2 interface and
 * writes it to SDRAM buffers via DMA transfer.
 *
 * VIN Configuration Summary (from reference project):
 *   Input Format      : YCbCr422 8-bit (from OV5640 via CSI-2)
 *   Input Resolution  : 768 x 450 (reference project, preclip settings)
 *   Output Resolution : 768 x 450 (no scaling, conversion mode NONE)
 *   Bytes per Pixel   : 2 (YUV422 / RGB565)
 *   Image Stride      : 768 pixels
 *   Capture Buffers   : 3 (triple buffering in SDRAM .sdram_noinit section)
 *   Buffer Size       : 768 * 450 * 2 = 691200 bytes per frame
 *
 * Capture Mode:
 *   - Continuous capture (R_VIN_CaptureStart with NULL buffer uses
 *     the pre-configured triple buffer in round-robin)
 *   - Frame complete interrupt (VIN_EVENT_NOTIFY with frame_complete flag)
 *     notifies the application of each completed frame
 *
 * Data Flow:
 *   OV5640 (MIPI CSI-2) -> D-PHY (S-003-1) -> CSI-2 Rx (S-003-2)
 *   -> VIN (this module) -> SDRAM Capture Buffer
 *   -> Display / AI Processing (downstream)
 *
 * Initialization Sequence:
 *   1. SDRAM init (S-001)
 *   2. MIPI D-PHY init (S-003-1: mipi_port.c)
 *   3. CSI-2 init (S-003-2: csi2_port.c) [called internally by R_VIN_Open]
 *   4. VIN Open via R_VIN_Open() [this module]
 *   5. OV5640 camera init (I2C register programming)
 *   6. VIN Capture Start via R_VIN_CaptureStart()
 *   7. OV5640 stream on
 *
 * Note: In the reference project, R_VIN_Open() internally calls
 * R_MIPI_CSI_Open() which internally calls R_MIPI_PHY_Open(). Thus a single
 * R_VIN_Open() call initializes the entire capture pipeline.
 *
 * Dependencies:
 *   - FSP VIN driver (r_vin) must be added via configuration.xml
 *   - FSP-generated instances: g_vin0, g_vin0_ctrl, g_vin0_cfg
 *   - SDRAM must be initialized (for capture buffers)
 *   - MIPI D-PHY and CSI-2 are initialized by R_VIN_Open() internally
 *
 * Reference:
 *   - camera_thread_entry.c: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:309-377
 *   - VIN config: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:171-309
 *   - VIN API: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra/fsp/inc/instances/r_vin.h
 *   - VIN types: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra/fsp/src/r_vin/r_vin_device_types.h
 *   - Capture API: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra/fsp/inc/api/r_capture_api.h
 *
 * @note
 * This file is part of the VIN capture control (S-003-3) implementation.
 */

#ifndef VIN_PORT_H
#define VIN_PORT_H

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
 * VIN capture buffer configuration
 *
 * The reference project uses triple buffering in SDRAM. Image buffers are
 * placed in the .sdram_noinit linker section with 128-byte alignment.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:171-173
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.h:50-62
 */
#define VIN_NUM_BUFFERS                 (3)         /* Triple buffering */

/**
 * VIN image configuration (from reference project FSP settings)
 *
 * These values match the reference project's preclip and stride settings.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:191-194
 *            reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.h:50-58
 */
#define VIN_IMAGE_WIDTH                 (768)       /* Horizontal pixels (preclip pixel_end + 1) */
#define VIN_IMAGE_HEIGHT                (450)       /* Vertical lines (preclip line_end + 1) */
#define VIN_BYTES_PER_PIXEL             (2)         /* YUV422 / RGB565 = 2 bytes per pixel */

/**
 * VIN image stride (in pixels)
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.h:50-51
 */
#define VIN_IMAGE_STRIDE                (768)       /* Image stride in pixels */

/**
 * VIN bytes per line and bytes per frame
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.h:54-58
 */
#define VIN_BYTES_PER_LINE              (VIN_IMAGE_STRIDE * VIN_BYTES_PER_PIXEL)
#define VIN_FRAME_SIZE_BYTES            (VIN_BYTES_PER_LINE * VIN_IMAGE_HEIGHT)

/**
 * VIN buffer alignment requirement
 *
 * VIN requires 64-byte or 128-byte aligned buffers for DMA transfer.
 * The reference project uses 128-byte alignment.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:171-173
 */
#define VIN_BUFFER_ALIGNMENT            (128)

/**
 * VIN preclip settings (from reference project FSP configuration)
 *
 * The preclip defines the region of the CSI-2 input that is captured.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:191-194
 */
#define VIN_PRECLIP_LINE_START          (0)
#define VIN_PRECLIP_LINE_END            (449)
#define VIN_PRECLIP_PIXEL_START         (0)
#define VIN_PRECLIP_PIXEL_END           (767)

/**
 * VIN Virtual Channel and Data Type settings
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:196-198
 */
#define VIN_VIRTUAL_CHANNEL             (0)
#define VIN_DATA_TYPE                   (0x1E)      /* VIN_DATA_TYPE_YUV422_8_BIT */

/**
 * VIN interrupt priority
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:274-286
 */
#define VIN_INTERRUPT_PRIORITY          (12)

/**********************************************************************************************************************
 Global Typedef definitions
 *********************************************************************************************************************/

/** VIN capture status */
typedef enum {
    VIN_PORT_STATUS_NOT_INITIALIZED = 0,    /**< Not yet initialized (R_VIN_Open not called) */
    VIN_PORT_STATUS_INITIALIZED,            /**< Initialized (R_VIN_Open succeeded, capture not started) */
    VIN_PORT_STATUS_CAPTURING,              /**< Continuous capture in progress */
    VIN_PORT_STATUS_STOPPED,                /**< Capture stopped (was capturing) */
    VIN_PORT_STATUS_ERROR,                  /**< Initialization or runtime error */
    VIN_PORT_STATUS_NO_FSP_MODULE,          /**< FSP VIN module not yet configured */
} vin_port_status_t;

/**
 * VIN capture statistics
 *
 * Tracks frame capture events and errors detected by the VIN callback handler.
 * All counters are updated from the ISR context (vin0_callback).
 */
typedef struct {
    volatile uint32_t frame_complete;       /**< Frame memory write complete count */
    volatile uint32_t end_of_frame;         /**< End of frame detected count */
    volatile uint32_t fifo_overflow;        /**< FIFO overflow error count */
    volatile uint32_t preclip_h_error;      /**< Horizontal preclip error count */
    volatile uint32_t preclip_v_error;      /**< Vertical preclip error count */
    volatile uint32_t axi_error;            /**< AXI response error count */
    volatile uint32_t resp_overflow;        /**< Response overflow error count */
    volatile uint32_t error_event;          /**< VIN_EVENT_ERROR total count */
    volatile uint32_t notify_event;         /**< VIN_EVENT_NOTIFY total count */
    volatile uint32_t callback_count;       /**< Total callback invocation count */
} vin_capture_stats_t;

/**
 * VIN capture information structure
 *
 * Contains the current state, configuration, and statistics of the VIN module.
 */
typedef struct {
    vin_port_status_t   status;             /**< Current VIN port status */
    bool                vin_open;           /**< true if R_VIN_Open() succeeded */

    /* Image configuration */
    uint32_t            image_width;        /**< Capture image width in pixels */
    uint32_t            image_height;       /**< Capture image height in lines */
    uint32_t            bytes_per_pixel;    /**< Bytes per pixel */
    uint32_t            image_stride;       /**< Image stride in pixels */
    uint32_t            frame_size_bytes;   /**< Total frame size in bytes */

    /* Buffer configuration */
    uint32_t            num_buffers;        /**< Number of capture buffers */
    uint32_t            buffer_addr[VIN_NUM_BUFFERS]; /**< Buffer addresses (SDRAM) */

    /* Capture state */
    uint32_t            active_buffer;      /**< Index of the latest written buffer */
    uint8_t            *p_last_frame;       /**< Pointer to the most recently completed frame */

    /* Statistics */
    vin_capture_stats_t stats;              /**< Capture statistics (copy, safe to read) */
} vin_port_info_t;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the VIN (Video Input) module (S-003-3)
 *
 * @details Performs the VIN module initialization:
 *   1. Check if FSP VIN module is available
 *   2. Call R_VIN_Open() with the FSP-generated configuration
 *   3. Reset capture statistics
 *
 * R_VIN_Open() internally:
 *   a. Opens the MIPI CSI-2 driver (R_MIPI_CSI_Open)
 *      which in turn opens the MIPI PHY driver (R_MIPI_PHY_Open)
 *   b. Configures the VIN input control (preclip, stride, data type)
 *   c. Configures the VIN output control (buffer addresses)
 *   d. Configures the VIN conversion control (data format conversion)
 *   e. Configures the VIN interrupt control (frame complete interrupt)
 *
 * Preconditions:
 *   - SDRAM must be initialized (for capture buffer access)
 *   - FSP VIN module must be configured in configuration.xml
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:331
 *
 * @retval true   VIN module initialized successfully
 * @retval false  VIN initialization failed
 */
bool vin_port_init(void);

/**
 * Close the VIN module
 *
 * @details Calls R_VIN_Close() to shut down capture and release resources.
 *
 * @retval true   VIN module closed successfully
 * @retval false  VIN close failed
 */
bool vin_port_close(void);

/**
 * Start continuous capture
 *
 * @details Calls R_VIN_CaptureStart() with NULL buffer to use the
 *          pre-configured triple buffer in round-robin mode.
 *
 * The VIN module captures frames continuously and writes them to the
 * SDRAM buffers in sequence. On each frame complete, the vin0_callback
 * is invoked with a pointer to the completed buffer.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:359
 *
 * @retval true   Capture started successfully
 * @retval false  Failed to start capture
 */
bool vin_port_capture_start(void);

/**
 * Stop capture
 *
 * @details Stops the VIN capture by closing and re-opening the module.
 *          The VIN FSP driver does not provide a direct stop API, so
 *          the module must be closed and re-opened to stop capture.
 *
 * @retval true   Capture stopped successfully
 * @retval false  Failed to stop capture
 */
bool vin_port_capture_stop(void);

/**
 * Get the current VIN status
 *
 * @return Current VIN port status
 */
vin_port_status_t vin_port_get_status(void);

/**
 * Check if VIN is available for use
 *
 * @retval true   VIN is initialized and ready (or capturing)
 * @retval false  VIN is not available
 */
bool vin_port_is_available(void);

/**
 * Get VIN configuration, status, and statistics
 *
 * @details Fills the info structure with current VIN state including
 *          image configuration, buffer addresses, capture state, and
 *          frame/error statistics.
 *
 * @param info Pointer to vin_port_info_t structure to fill
 */
void vin_port_get_info(vin_port_info_t *info);

/**
 * Reset VIN capture statistics
 *
 * @details Clears all frame and error counters to zero.
 */
void vin_port_reset_stats(void);

/**
 * Get pointer to the most recently completed frame buffer
 *
 * @details Returns a pointer to the buffer containing the most recently
 *          completed frame capture. This pointer is updated by the VIN
 *          callback handler on each frame_complete interrupt.
 *
 * @return Pointer to the last completed frame buffer, or NULL if no frame
 *         has been captured yet
 */
uint8_t *vin_port_get_last_frame(void);

/**
 * Get VIN hardware capture status
 *
 * @details Queries the VIN hardware status register via R_VIN_StatusGet().
 *          Returns the capture state (IDLE, IN_PROGRESS, BUSY).
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:369-375
 *
 * @param p_state Pointer to store the current capture state
 * @retval true   Status retrieved successfully
 * @retval false  Failed to get status (VIN not open)
 */
bool vin_port_get_hw_status(uint32_t *p_state);

/**
 * VIN callback handler (called from ISR context)
 *
 * @details This function is registered as the vin0_callback in the
 *          FSP-generated configuration. It processes VIN capture events:
 *            - VIN_EVENT_NOTIFY with frame_complete: Frame write complete
 *            - VIN_EVENT_NOTIFY with fifo_overflow: FIFO overflow error
 *            - VIN_EVENT_ERROR: General error event
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:907-941
 *
 * @param p_args Pointer to capture_callback_args_t from FSP driver
 */
void vin_port_callback(void *p_args);

/**
 * NT-Shell "camera status" sub-command handler
 *
 * @details Displays VIN capture status, image configuration, buffer layout,
 *          and capture statistics.
 */
void vin_cmd_status(void);

/**
 * NT-Shell "camera info" sub-command handler
 *
 * @details Displays camera module information (sensor ID, resolution, format).
 */
void vin_cmd_info(void);

#ifdef __cplusplus
}
#endif

#endif /* VIN_PORT_H */
