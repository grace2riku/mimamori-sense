/**
 * @file cmd_utils.h
 * @brief NT-Shell command utility functions
 * @details
 * Provides common utility functions for NT-Shell command implementations:
 *  - Numeric string parsing (hex/decimal)
 *  - Address range validation
 *  - Common error message output
 *  - Command definition macro
 *
 * @note
 * This file is part of the NT-Shell command registration framework (S-007-4).
 */

#ifndef CMD_UTILS_H
#define CMD_UTILS_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size for command implementations */
#define CMD_PRINT_BUF_SIZE      (128)

/**
 * Command return codes
 * Unified error codes returned by command handlers.
 */
#define CMD_OK                  (0)     /**< Command executed successfully */
#define CMD_ERR_USAGE           (-1)    /**< Invalid usage (wrong arguments) */
#define CMD_ERR_INVALID_ARG     (-2)    /**< Invalid argument value */
#define CMD_ERR_INVALID_ADDR    (-3)    /**< Invalid or inaccessible address */
#define CMD_ERR_ALIGN           (-4)    /**< Address alignment error */
#define CMD_ERR_EXECUTE         (-5)    /**< Command execution error */

/**
 * RA8P1 memory map regions (Cortex-M85 CPU0)
 *
 * Address ranges based on the RA8P1 linker script (memory_regions.ld).
 * This is a conservative set; additional regions may be accessible depending on MPU configuration.
 */

/** ITCM start address */
#define CMD_ADDR_ITCM_START         (0x00000000UL)
/** ITCM end address (exclusive) - 128KB */
#define CMD_ADDR_ITCM_END           (0x00020000UL)

/** Code Flash & system area start (includes Flash, OFS, OTP, Unique ID) */
#define CMD_ADDR_FLASH_START        (0x02000000UL)
/** Code Flash & system area end (exclusive) */
#define CMD_ADDR_FLASH_END          (0x03000000UL)

/** DTCM start address */
#define CMD_ADDR_DTCM_START         (0x20000000UL)
/** DTCM end address (exclusive) - 128KB */
#define CMD_ADDR_DTCM_END           (0x20020000UL)

/** Internal SRAM start address */
#define CMD_ADDR_SRAM_START         (0x22000000UL)
/** Internal SRAM end address (exclusive) - ~1.8MB */
#define CMD_ADDR_SRAM_END           (0x221D4000UL)

/** Peripheral register space start */
#define CMD_ADDR_PERIPH_START       (0x40000000UL)
/** Peripheral register space end (exclusive) */
#define CMD_ADDR_PERIPH_END         (0x50000000UL)

/** SDRAM start address */
#define CMD_ADDR_SDRAM_START        (0x68000000UL)
/** SDRAM end address (exclusive) - 128MB */
#define CMD_ADDR_SDRAM_END          (0x70000000UL)

/** OSPI Flash start address (OSPI1 CS0) */
#define CMD_ADDR_OSPI_START         (0x70000000UL)
/** OSPI Flash end address (exclusive) - covers OSPI1 + OSPI0 */
#define CMD_ADDR_OSPI_END           (0xA0000000UL)

/** System register space start (SCB, NVIC, etc.) */
#define CMD_ADDR_SYSTEM_START       (0xE0000000UL)
/** System register space end (exclusive) */
#define CMD_ADDR_SYSTEM_END         (0xF0000000UL)

/** Minimum access size for memory commands */
#define CMD_ACCESS_SIZE_BYTE        (1)
/** 16-bit access size */
#define CMD_ACCESS_SIZE_HALF        (2)
/** 32-bit access size */
#define CMD_ACCESS_SIZE_WORD        (4)

/** Maximum count for memory fill operations (mw command with count parameter) */
#define CMD_MW_MAX_COUNT            (0x10000UL)

/**********************************************************************************************************************
 Global Typedef definitions
 *********************************************************************************************************************/

/**
 * Result of numeric parsing
 */
typedef struct {
    uint32_t value;     /**< Parsed numeric value */
    bool     valid;     /**< true if parsing succeeded */
} cmd_parse_result_t;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse a numeric string (decimal or hexadecimal)
 * @details Supports the following formats:
 *  - "0x1234" or "0X1234" : Hexadecimal with prefix
 *  - "1234"               : Decimal
 *  - "0"                  : Zero
 *
 * @param str Input string to parse
 * @return Parse result containing the value and validity flag
 */
cmd_parse_result_t cmd_parse_uint32(const char *str);

/**
 * Validate whether an address is accessible for read/write
 * @details Checks the address against known memory regions of the RA8P1.
 *          Does NOT check MPU settings; this is a coarse-grained safety check.
 *
 * @param addr Address to validate
 * @param size Access size in bytes (1, 2, or 4)
 * @return true if address range [addr, addr+size) is within known accessible regions
 */
bool cmd_validate_address(uint32_t addr, uint32_t size);

/**
 * Validate address alignment for a given access size
 * @param addr Address to validate
 * @param access_size Access size in bytes (1, 2, or 4)
 * @return true if address is properly aligned
 */
bool cmd_validate_alignment(uint32_t addr, uint32_t access_size);

/**
 * Validate memory access size (must be 1, 2, or 4)
 * @param size Size to validate
 * @return true if size is 1, 2, or 4
 */
bool cmd_validate_access_size(int size);

/**
 * Print a usage error message for a command
 * @param cmd_name Command name
 * @param usage_str Usage string (e.g., "<address> [size]")
 */
void cmd_print_usage(const char *cmd_name, const char *usage_str);

/**
 * Print a generic error message
 * @param msg Error message string
 */
void cmd_print_error(const char *msg);

/**
 * Print a "diagnostic excluded from this build" message
 * @details Used by commands guarded by MIMAMORI_VERBOSE_DIAG (see
 *          src/diag_config.h). All such commands share this one message so
 *          the excluded set costs a single string in .rodata (Issue #183).
 * @param cmd_name Command name as typed by the user (e.g. "camera diag")
 */
void cmd_print_diag_disabled(const char *cmd_name);

/**
 * Print an address alignment error message
 * @param addr The misaligned address
 * @param access_size Required alignment size
 */
void cmd_print_align_error(uint32_t addr, int access_size);

/**
 * Print an invalid address error message
 * @param addr The invalid address
 */
void cmd_print_addr_error(uint32_t addr);

/**
 * Validate whether an address is in a writable memory region
 * @details Rejects read-only and protected areas:
 *  - ITCM  (0x00000000-0x00020000) : Code execution area
 *  - Flash (0x02000000-0x03000000) : Code Flash, OFS, OTP, Unique ID
 *  - System (0xE0000000-0xF0000000): SCB, NVIC, etc.
 *
 * Allows writes to:
 *  - DTCM  (0x20000000-0x20020000)
 *  - SRAM  (0x22000000-0x221D4000)
 *  - Peripheral registers (0x40000000-0x50000000)
 *  - SDRAM (0x68000000-0x70000000)
 *  - OSPI  (0x70000000-0xA0000000)
 *
 * @param addr Address to validate
 * @param size Access size in bytes (1, 2, or 4)
 * @return true if address range [addr, addr+size) is within a writable region
 */
bool cmd_validate_writable(uint32_t addr, uint32_t size);

/**
 * Print a write-protection error message
 * @param addr The write-protected address
 */
void cmd_print_write_protect_error(uint32_t addr);

/**
 * Print command result message based on return code
 * @details If retval indicates an error and is not CMD_ERR_USAGE
 *          (which already has its own message), prints a generic
 *          "command failed" message.
 * @param cmd_name Command name
 * @param retval Return code from the command handler
 */
void cmd_print_result(const char *cmd_name, int retval);

#ifdef __cplusplus
}
#endif

#endif /* CMD_UTILS_H */
