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
 * The following address ranges are considered accessible for memory read/write commands.
 * This is a conservative set; additional regions may be accessible depending on MPU configuration.
 */

/** Internal SRAM start address */
#define CMD_ADDR_SRAM_START         (0x20000000UL)
/** Internal SRAM end address (exclusive) - 1.5MB */
#define CMD_ADDR_SRAM_END           (0x20180000UL)

/** Code Flash start address */
#define CMD_ADDR_FLASH_START        (0x00000000UL)
/** Code Flash end address (exclusive) - 4MB */
#define CMD_ADDR_FLASH_END          (0x00400000UL)

/** Peripheral register space start */
#define CMD_ADDR_PERIPH_START       (0x40000000UL)
/** Peripheral register space end (exclusive) */
#define CMD_ADDR_PERIPH_END         (0x50000000UL)

/** System register space start */
#define CMD_ADDR_SYSTEM_START       (0xE0000000UL)
/** System register space end (exclusive) */
#define CMD_ADDR_SYSTEM_END         (0xF0000000UL)

/** SDRAM start address */
#define CMD_ADDR_SDRAM_START        (0x68000000UL)
/** SDRAM end address (exclusive) - 64MB */
#define CMD_ADDR_SDRAM_END          (0x6C000000UL)

/** OSPI Flash start address */
#define CMD_ADDR_OSPI_START         (0x80000000UL)
/** OSPI Flash end address (exclusive) - 256MB */
#define CMD_ADDR_OSPI_END           (0x90000000UL)

/** Minimum access size for memory commands */
#define CMD_ACCESS_SIZE_BYTE        (1)
/** 16-bit access size */
#define CMD_ACCESS_SIZE_HALF        (2)
/** 32-bit access size */
#define CMD_ACCESS_SIZE_WORD        (4)

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
