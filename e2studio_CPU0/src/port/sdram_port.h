/**
 * @file sdram_port.h
 * @brief SDRAM control abstraction layer for EK-RA8P1 board
 * @details
 * Provides status query, diagnostic functions, and section placement macros
 * for the IS42S16160J-6BLI SDRAM (32MB) on the EK-RA8P1 evaluation board.
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
 * SDRAM Section Layout (defined in FSP-generated linker script: Debug/fsp_gen.lld):
 *   The FSP linker script defines the following SDRAM sections, all placed in
 *   the SDRAM memory region (0x68000000, 64MB for CPU0):
 *
 *   +-------------------------------+ 0x68000000 (SDRAM_START)
 *   | .sdram_from_flash             |   Initialized data (copied from Flash at startup)
 *   | .sdram_from_ospi0_cs1         |   Initialized data (copied from OSPI0 CS1)
 *   | .sdram_from_ospi1_cs1         |   Initialized data (copied from OSPI1 CS1)
 *   | .sdram_from_data_flash        |   Initialized data (copied from Data Flash)
 *   +-------------------------------+
 *   | .sdram_noinit_nocache         |   Non-initialized, non-cached (GLCDC framebuffers)
 *   +-------------------------------+
 *   | .sdram_nocache                |   Zero-initialized, non-cached (BSP zeroes at startup)
 *   +-------------------------------+
 *   | .sdram_noinit                 |   Non-initialized (no startup processing)
 *   +-------------------------------+
 *   | .sdram                        |   Zero-initialized (BSP zeroes at startup)
 *   +-------------------------------+
 *   |           (free)              |
 *   +-------------------------------+ 0x6BFFFFFF (CPU0 partition end)
 *
 *   BSP Startup Behavior:
 *     - .sdram_from_*  : Data is copied from the source memory at startup.
 *     - .sdram_nocache : Zeroed by BSP startup (in zero_list of bsp_linker_info.h).
 *     - .sdram         : Zeroed by BSP startup (in zero_list of bsp_linker_info.h).
 *     - .sdram_noinit_nocache : NOT initialized at startup (content is undefined).
 *     - .sdram_noinit  : NOT initialized at startup (content is undefined).
 *
 *   Cache Behavior:
 *     - Sections with "nocache" in their name are configured as non-cacheable
 *       by the BSP MPU setup (via nocache_list in bsp_linker_info.h).
 *     - GLCDC framebuffers MUST be placed in a nocache section to avoid
 *       coherency issues between CPU cache and GLCDC DMA.
 *
 * Section Placement Macros (for variable declarations):
 *   Use BSP_PLACE_IN_SECTION() with the section name to place variables.
 *   Convenience macros are defined below for common use cases.
 *
 * Reference:
 *   - FSP bsp_sdram.c: ra/fsp/src/bsp/mcu/all/bsp_sdram.c (R_BSP_SdramInit)
 *   - FSP linker script: Debug/fsp_gen.lld (SDRAM section definitions, lines 403-451)
 *   - FSP BSP startup: Debug/bsp_linker_info.h (zero_list, copy_list, nocache_list)
 *   - FSP compiler support: ra/fsp/src/bsp/mcu/all/bsp_compiler_support.h:51
 *   - Reference project: reference_projects/lv_port_renesas_ek_ra8p1/src/hal_entry.c:50
 *   - Reference framebuffer: reference_projects/lv_port_renesas_ek_ra8p1/ra_gen/common_data.c:7
 *   - BSP config: ra_cfg/fsp_cfg/bsp/bsp_mcu_family_cfg.h (BSP_CFG_SDRAM_*)
 *
 * @note
 * This file is part of the SDRAM control (S-001-2, S-001-3) implementation.
 */

#ifndef SDRAM_PORT_H
#define SDRAM_PORT_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include "bsp_api.h"

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

/*
 * SDRAM Section Placement Macros
 *
 * These macros provide convenient wrappers around BSP_PLACE_IN_SECTION()
 * for placing variables into the SDRAM linker sections defined by FSP.
 *
 * The section names correspond to sections in the FSP-generated linker script
 * (Debug/fsp_gen.lld). BSP startup code handles initialization (zeroing or
 * copying) automatically based on the section type.
 *
 * Usage Examples:
 *   // Framebuffer: no init needed, must be non-cacheable for GLCDC DMA
 *   static uint8_t fb[2][1024*600*2] SDRAM_SECTION_NOINIT_NOCACHE;
 *
 *   // Zero-initialized work buffer
 *   static uint32_t work_buf[1024] SDRAM_SECTION_ZERO;
 *
 *   // Large buffer without initialization (content is undefined at startup)
 *   static uint8_t capture_buf[640*480*2] SDRAM_SECTION_NOINIT;
 *
 * Reference:
 *   - BSP_PLACE_IN_SECTION: ra/fsp/src/bsp/mcu/all/bsp_compiler_support.h:51
 *   - BSP_ALIGN_VARIABLE: ra/fsp/src/bsp/mcu/all/bsp_compiler_support.h:53
 *   - Reference framebuffer placement:
 *     reference_projects/lv_port_renesas_ek_ra8p1/ra_gen/common_data.c:7
 *       uint8_t fb_background[2][...] BSP_ALIGN_VARIABLE(64)
 *           BSP_PLACE_IN_SECTION(".sdram_noinit_nocache");
 */

/**
 * Place a variable in the SDRAM non-initialized, non-cached section.
 *
 * Use for: GLCDC framebuffers, DMA buffers that must not be cached.
 * BSP startup: No initialization (content is undefined after reset).
 * Cache: Non-cacheable (MPU configured by BSP).
 *
 * Linker section: .sdram_noinit_nocache (in Debug/fsp_gen.lld, line 415-421)
 */
#define SDRAM_SECTION_NOINIT_NOCACHE    BSP_PLACE_IN_SECTION(".sdram_noinit_nocache")

/**
 * Place a variable in the SDRAM zero-initialized, non-cached section.
 *
 * Use for: DMA work buffers that need zeroing, shared memory with peripherals.
 * BSP startup: Zeroed by BSP startup code (bsp_linker_info.h zero_list).
 * Cache: Non-cacheable (MPU configured by BSP).
 *
 * Linker section: .sdram_nocache (in Debug/fsp_gen.lld, line 422-430)
 */
#define SDRAM_SECTION_ZERO_NOCACHE      BSP_PLACE_IN_SECTION(".sdram_nocache")

/**
 * Place a variable in the SDRAM non-initialized section.
 *
 * Use for: Large buffers where initial content does not matter
 *          (camera capture buffers, AI inference scratch memory).
 * BSP startup: No initialization (content is undefined after reset).
 * Cache: Cacheable (normal cache policy).
 *
 * Linker section: .sdram_noinit (in Debug/fsp_gen.lld, line 432-438)
 */
#define SDRAM_SECTION_NOINIT            BSP_PLACE_IN_SECTION(".sdram_noinit")

/**
 * Place a variable in the SDRAM zero-initialized section.
 *
 * Use for: Work buffers that must start at zero.
 * BSP startup: Zeroed by BSP startup code (bsp_linker_info.h zero_list).
 * Cache: Cacheable (normal cache policy).
 *
 * Linker section: .sdram (in Debug/fsp_gen.lld, line 440-446)
 */
#define SDRAM_SECTION_ZERO              BSP_PLACE_IN_SECTION(".sdram")

/**
 * Place a variable in the SDRAM initialized-from-flash section.
 *
 * Use for: Data that must be initialized with specific values from Flash.
 * BSP startup: Copied from Flash by BSP startup code (bsp_linker_info.h copy_list).
 * Cache: Cacheable (normal cache policy).
 *
 * Note: This increases Flash usage by the size of the data.
 *
 * Linker section: .sdram_from_flash (in Debug/fsp_gen.lld, line 404-413)
 */
#define SDRAM_SECTION_DATA              BSP_PLACE_IN_SECTION(".sdram_from_flash")

/**
 * Required alignment for GLCDC framebuffers (64-byte aligned).
 *
 * The GLCDC requires framebuffer addresses to be 64-byte aligned.
 * Use in combination with SDRAM_SECTION_NOINIT_NOCACHE for framebuffers.
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/ra_gen/common_data.c:7
 *   BSP_ALIGN_VARIABLE(64)
 */
#define SDRAM_ALIGN_FRAMEBUFFER         BSP_ALIGN_VARIABLE(64)

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

/**
 * SDRAM section address range (base and limit from linker symbols)
 *
 * Linker symbols use the FSP naming convention:
 *   __<section_name>$$Base  = start address
 *   __<section_name>$$Limit = end address (exclusive)
 */
typedef struct {
    uint32_t    base;       /**< Section start address */
    uint32_t    limit;      /**< Section end address (exclusive) */
} sdram_section_range_t;

/**
 * SDRAM section layout information
 *
 * Contains the address ranges of all SDRAM linker sections.
 * Populated from linker-defined symbols in Debug/fsp_gen.lld.
 *
 * Reference: Debug/bsp_linker_info.h (extern declarations for linker symbols)
 */
typedef struct {
    sdram_section_range_t   noinit_nocache;     /**< .sdram_noinit_nocache section */
    sdram_section_range_t   zero_nocache;       /**< .sdram_nocache section (zero-init) */
    sdram_section_range_t   noinit;             /**< .sdram_noinit section */
    sdram_section_range_t   zero;               /**< .sdram section (zero-init) */
    sdram_section_range_t   from_flash;         /**< .sdram_from_flash section (init data) */
    uint32_t                sdram_start;        /**< SDRAM region start (from __ddsc_SDRAM_START) */
    uint32_t                sdram_end;          /**< SDRAM region end (from __ddsc_SDRAM_END) */
    uint32_t                total_used;         /**< Total bytes used by all sections */
    uint32_t                total_free;         /**< Remaining free bytes */
} sdram_section_info_t;

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
 * Get SDRAM section layout information
 *
 * @details Queries the linker-defined symbols for each SDRAM section to
 *          determine the actual memory layout. Uses extern references to
 *          the linker symbols defined in Debug/fsp_gen.lld and
 *          Debug/bsp_linker_info.h.
 *
 * @param info Pointer to sdram_section_info_t structure to fill
 */
void sdram_port_get_section_info(sdram_section_info_t *info);

/**
 * NT-Shell command handler for SDRAM control
 *
 * @details Registered as the "sdram" command in usrcmd.c.
 *          Sub-commands:
 *            sdram status  - Show SDRAM initialization status and configuration
 *            sdram check   - Perform a quick sanity check (write/read pattern)
 *            sdram map     - Show SDRAM section memory layout
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
