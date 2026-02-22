/**
 * @file sdram_port.h
 * @brief SDRAM control abstraction layer for EK-RA8P1 board
 * @details
 * Provides status query and diagnostic functions for the IS42S16160J-6BLI
 * SDRAM (32MB) on the EK-RA8P1 evaluation board.
 *
 * SDRAM initialization is performed by the FSP BSP layer (R_BSP_SdramInit)
 * during the warm-start sequence in hal_warmstart.c, before the FreeRTOS
 * scheduler is started. This module wraps and tracks that initialization,
 * and provides status query and basic verification functions.
 *
 * SDRAM Memory Map:
 *   Base address : 0x68000000
 *   Size         : 32MB (IS42S16160J-6BLI: 16Mbit x 16bit = 256Mbit = 32MB)
 *   Bus width    : 16-bit
 *   SDCLK        : ~133.333 MHz (ICLK/8 or BCLK derived)
 *
 * Reference:
 *   - FSP bsp_sdram.c: ra/fsp/src/bsp/mcu/all/bsp_sdram.c (R_BSP_SdramInit)
 *   - Reference project: reference_projects/lv_port_renesas_ek_ra8p1/src/hal_entry.c:50
 *   - BSP config: ra_cfg/fsp_cfg/bsp/bsp_mcu_family_cfg.h (BSP_CFG_SDRAM_*)
 *
 * @note
 * This file is part of the SDRAM control (S-001-2) implementation.
 */

#ifndef SDRAM_PORT_H
#define SDRAM_PORT_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** SDRAM base address (from BSP_FEATURE_SDRAM_START_ADDRESS) */
#define SDRAM_BASE_ADDRESS          (0x68000000UL)

/** SDRAM total size in bytes: IS42S16160J-6BLI = 32MB */
#define SDRAM_SIZE_BYTES            (32UL * 1024UL * 1024UL)

/** SDRAM end address (exclusive) */
#define SDRAM_END_ADDRESS           (SDRAM_BASE_ADDRESS + SDRAM_SIZE_BYTES)

/** SDRAM bus width in bits */
#define SDRAM_BUS_WIDTH_BITS        (16)

/** SDRAM CAS latency (from BSP_CFG_SDRAM_TCL) */
#define SDRAM_CAS_LATENCY           (3)

/** SDRAM auto-refresh row count */
#define SDRAM_REFRESH_ROWS          (8192)

/** SDRAM auto-refresh period in milliseconds */
#define SDRAM_REFRESH_PERIOD_MS     (64)

/**********************************************************************************************************************
 Global Typedef definitions
 *********************************************************************************************************************/

/** SDRAM initialization status */
typedef enum {
    SDRAM_STATUS_NOT_INITIALIZED = 0,   /**< Not yet initialized */
    SDRAM_STATUS_INITIALIZED,           /**< Successfully initialized */
    SDRAM_STATUS_ERROR,                 /**< Initialization failed or error detected */
} sdram_status_t;

/** SDRAM configuration information (read-only) */
typedef struct {
    uint32_t    base_address;       /**< SDRAM base address */
    uint32_t    size_bytes;         /**< SDRAM total size in bytes */
    uint32_t    bus_width_bits;     /**< Bus width in bits */
    uint32_t    cas_latency;        /**< CAS latency setting */
    uint32_t    refresh_rows;       /**< Number of refresh rows */
    uint32_t    refresh_period_ms;  /**< Auto-refresh period in milliseconds */
    bool        sdram_enabled;      /**< BSP_CFG_SDRAM_ENABLED flag */
} sdram_config_info_t;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Record that SDRAM initialization has been performed by the BSP
 *
 * @details This function is called from hal_warmstart.c immediately after
 *          R_BSP_SdramInit(true) completes. It performs a quick sanity check
 *          (write/read a known pattern at the base address) and records
 *          the initialization status.
 *
 *          This must be called in the BSP_WARM_START_POST_C context, before
 *          the FreeRTOS scheduler starts.
 *
 * @retval true   SDRAM initialization verified successfully
 * @retval false  SDRAM sanity check failed
 */
bool sdram_port_init(void);

/**
 * Get the current SDRAM initialization status
 *
 * @return Current SDRAM status
 */
sdram_status_t sdram_port_get_status(void);

/**
 * Get SDRAM configuration information
 *
 * @return Pointer to the SDRAM configuration info structure (static, always valid)
 */
const sdram_config_info_t *sdram_port_get_config(void);

/**
 * Check if SDRAM is available for use
 *
 * @retval true   SDRAM is initialized and accessible
 * @retval false  SDRAM is not available
 */
bool sdram_port_is_available(void);

/**
 * Perform a quick sanity check on SDRAM
 *
 * @details Writes and reads back a test pattern at a few locations to verify
 *          basic SDRAM operation. This is a fast check (does not test all memory).
 *
 * @retval true   Sanity check passed
 * @retval false  Sanity check failed
 */
bool sdram_port_sanity_check(void);

/**
 * NT-Shell command handler for SDRAM control
 *
 * @details Registered as the "sdram" command in usrcmd.c.
 *          Sub-commands:
 *            sdram status  - Show SDRAM initialization status and configuration
 *            sdram check   - Perform a quick sanity check (write/read pattern)
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return CMD_OK on success, CMD_ERR_* on error
 */
int usrcmd_sdram(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* SDRAM_PORT_H */
