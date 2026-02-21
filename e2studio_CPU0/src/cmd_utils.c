/**
 * @file cmd_utils.c
 * @brief NT-Shell command utility functions implementation
 * @details
 * Implements common utility functions for NT-Shell command implementations.
 * See cmd_utils.h for the public interface.
 *
 * @note
 * This file is part of the NT-Shell command registration framework (S-007-4).
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd_utils.h"
#include "jlink_console.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Internal print buffer size for error messages */
#define CMD_INTERNAL_BUF_SIZE   (128)

/**********************************************************************************************************************
 Private (static) variables and functions
 *********************************************************************************************************************/

/**
 * Memory region descriptor for address validation
 */
typedef struct {
    uint32_t start;     /**< Region start address (inclusive) */
    uint32_t end;       /**< Region end address (exclusive) */
} cmd_mem_region_t;

/**
 * Table of accessible memory regions on RA8P1 (Cortex-M85 CPU0)
 *
 * This table defines the coarse-grained address ranges that are
 * considered safe to access via memory read/write commands.
 */
static const cmd_mem_region_t s_valid_regions[] = {
    { CMD_ADDR_FLASH_START,   CMD_ADDR_FLASH_END   },  /* Code Flash (4MB) */
    { CMD_ADDR_SRAM_START,    CMD_ADDR_SRAM_END     },  /* Internal SRAM (1.5MB) */
    { CMD_ADDR_PERIPH_START,  CMD_ADDR_PERIPH_END   },  /* Peripheral registers */
    { CMD_ADDR_SDRAM_START,   CMD_ADDR_SDRAM_END    },  /* SDRAM (64MB) */
    { CMD_ADDR_OSPI_START,    CMD_ADDR_OSPI_END     },  /* OSPI Flash (256MB) */
    { CMD_ADDR_SYSTEM_START,  CMD_ADDR_SYSTEM_END   },  /* System registers (SCB, NVIC, etc.) */
};

/** Number of entries in the valid regions table */
#define CMD_NUM_VALID_REGIONS   (sizeof(s_valid_regions) / sizeof(s_valid_regions[0]))

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Parse a numeric string (decimal or hexadecimal)
 */
cmd_parse_result_t cmd_parse_uint32(const char *str)
{
    cmd_parse_result_t result;
    char *endptr = NULL;

    result.value = 0;
    result.valid = false;

    if (str == NULL || *str == '\0') {
        return result;
    }

    /* Use strtoul with base 0 to auto-detect hex (0x prefix) and decimal */
    result.value = (uint32_t)strtoul(str, &endptr, 0);

    /* Check if the entire string was consumed (ignoring trailing whitespace) */
    if (endptr != str) {
        /* Skip trailing whitespace */
        while (*endptr == ' ' || *endptr == '\t' || *endptr == '\r' || *endptr == '\n') {
            endptr++;
        }
        if (*endptr == '\0') {
            result.valid = true;
        }
    }

    return result;
}

/**
 * Validate whether an address range is accessible
 */
bool cmd_validate_address(uint32_t addr, uint32_t size)
{
    uint32_t end_addr;

    /* Reject NULL pointer area (first 256 bytes) */
    if (addr < 0x100UL && addr + size <= 0x100UL) {
        /* Allow only if address range extends beyond the null page */
        return false;
    }

    /* Overflow check */
    end_addr = addr + size;
    if (end_addr < addr) {
        return false;  /* Overflow */
    }

    /* Check against known valid regions */
    for (uint32_t i = 0; i < CMD_NUM_VALID_REGIONS; i++) {
        if (addr >= s_valid_regions[i].start && end_addr <= s_valid_regions[i].end) {
            return true;
        }
    }

    return false;
}

/**
 * Validate address alignment for a given access size
 */
bool cmd_validate_alignment(uint32_t addr, uint32_t access_size)
{
    if (access_size == 0) {
        return false;
    }

    return (addr % access_size) == 0;
}

/**
 * Validate memory access size
 */
bool cmd_validate_access_size(int size)
{
    return (size == CMD_ACCESS_SIZE_BYTE ||
            size == CMD_ACCESS_SIZE_HALF ||
            size == CMD_ACCESS_SIZE_WORD);
}

/**
 * Print a usage error message for a command
 */
void cmd_print_usage(const char *cmd_name, const char *usage_str)
{
    char buf[CMD_INTERNAL_BUF_SIZE];

    snprintf(buf, sizeof(buf), "Usage: %s %s\r\n", cmd_name, usage_str);
    print_to_console(buf);
}

/**
 * Print a generic error message
 */
void cmd_print_error(const char *msg)
{
    char buf[CMD_INTERNAL_BUF_SIZE];

    snprintf(buf, sizeof(buf), "Error: %s\r\n", msg);
    print_to_console(buf);
}

/**
 * Print an address alignment error message
 */
void cmd_print_align_error(uint32_t addr, int access_size)
{
    char buf[CMD_INTERNAL_BUF_SIZE];

    snprintf(buf, sizeof(buf),
             "Error: Address 0x%08lX is not aligned to %d-byte boundary.\r\n",
             (unsigned long)addr, access_size);
    print_to_console(buf);
}

/**
 * Print an invalid address error message
 */
void cmd_print_addr_error(uint32_t addr)
{
    char buf[CMD_INTERNAL_BUF_SIZE];

    snprintf(buf, sizeof(buf),
             "Error: Address 0x%08lX is not in an accessible memory region.\r\n",
             (unsigned long)addr);
    print_to_console(buf);
}

/**
 * Print command result message based on return code
 */
void cmd_print_result(const char *cmd_name, int retval)
{
    /* Only print for non-usage errors that don't have their own message */
    if (retval < CMD_ERR_USAGE) {
        char buf[CMD_INTERNAL_BUF_SIZE];

        snprintf(buf, sizeof(buf), "Error: '%s' failed (code: %d).\r\n", cmd_name, retval);
        print_to_console(buf);
    }
}
