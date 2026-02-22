/**
 * @file sdram_port.c
 * @brief SDRAM control abstraction layer implementation
 * @details
 * Implements SDRAM initialization status tracking, configuration query,
 * and diagnostic functions for the IS42S16160J-6BLI SDRAM on the EK-RA8P1 board.
 *
 * The actual SDRAM initialization sequence is performed by the FSP BSP layer:
 *   - R_BSP_SdramInit(true) in hal_warmstart.c (BSP_WARM_START_POST_C event)
 *
 * This initialization sequence follows the IS42S16160J datasheet requirements:
 *   1. Power-on wait (200us minimum) - handled by BSP startup delay
 *   2. PRECHARGE ALL command
 *   3. AUTO REFRESH command x8 (BSP_CFG_SDRAM_INIT_ARFC = 8)
 *   4. MODE REGISTER SET (CAS latency = 3, burst length = 1, sequential)
 *   5. Auto-refresh enabled (refresh interval: 64ms / 8192 rows)
 *   6. SDRAM access enabled
 *
 * All of the above steps are handled by R_BSP_SdramInit() in:
 *   ra/fsp/src/bsp/mcu/all/bsp_sdram.c (lines 59-147)
 *
 * This port module provides:
 *   - Status tracking (sdram_port_init records the result)
 *   - Configuration query (BSP_CFG_SDRAM_* values)
 *   - Sanity check (write/readback verification at key addresses)
 *   - NT-Shell "sdram" command for diagnostics
 *
 * Reference:
 *   - FSP bsp_sdram.c: ra/fsp/src/bsp/mcu/all/bsp_sdram.c
 *   - Reference hal_entry.c: reference_projects/lv_port_renesas_ek_ra8p1/src/hal_entry.c:50
 *   - BSP config: ra_cfg/fsp_cfg/bsp/bsp_mcu_family_cfg.h:452-510
 *
 * @note
 * This file is part of the SDRAM control (S-001-2) implementation.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>

#include "sdram_port.h"
#include "hal_data.h"
#include "jlink_console.h"
#include "cmd_utils.h"
#include "ntlibc.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size */
#define SDRAM_PRINT_BUF_SIZE    (128)

/** Test pattern for sanity check: walking 1s seed */
#define SDRAM_TEST_PATTERN_A    (0xA5A5A5A5UL)

/** Complementary test pattern */
#define SDRAM_TEST_PATTERN_B    (0x5A5A5A5AUL)

/** Address-as-data test pattern */
#define SDRAM_TEST_PATTERN_ADDR (0x12345678UL)

/** Number of test locations for sanity check */
#define SDRAM_SANITY_CHECK_COUNT    (8)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/** Current SDRAM initialization status */
static sdram_status_t s_sdram_status = SDRAM_STATUS_NOT_INITIALIZED;

/** SDRAM configuration information (populated from BSP_CFG_SDRAM_* defines) */
static const sdram_config_info_t s_sdram_config = {
    .base_address       = SDRAM_BASE_ADDRESS,
    .size_bytes         = SDRAM_SIZE_BYTES,
    .bus_width_bits     = SDRAM_BUS_WIDTH_BITS,
    .cas_latency        = SDRAM_CAS_LATENCY,
    .refresh_rows       = SDRAM_REFRESH_ROWS,
    .refresh_period_ms  = SDRAM_REFRESH_PERIOD_MS,
    .sdram_enabled      = (BSP_CFG_SDRAM_ENABLED != 0),
};

/**********************************************************************************************************************
 Private (static) functions prototypes
 *********************************************************************************************************************/
static bool sdram_write_read_verify(volatile uint32_t *addr, uint32_t pattern);
static void sdram_cmd_status(void);
static void sdram_cmd_check(void);

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Write a 32-bit pattern to an address and verify by readback
 *
 * @param addr    Pointer to the SDRAM location to test
 * @param pattern 32-bit test pattern to write
 * @retval true   Readback matches the written pattern
 * @retval false  Readback mismatch (SDRAM error)
 */
static bool sdram_write_read_verify(volatile uint32_t *addr, uint32_t pattern)
{
    *addr = pattern;

    /* Data synchronization barrier to ensure the write completes */
    __DSB();

    uint32_t readback = *addr;

    return (readback == pattern);
}

/**
 * "sdram status" sub-command handler
 *
 * @details Displays SDRAM initialization status, configuration parameters,
 *          and hardware register values.
 */
static void sdram_cmd_status(void)
{
    char buf[SDRAM_PRINT_BUF_SIZE];
    const char *status_str;

    /* Status */
    switch (s_sdram_status) {
        case SDRAM_STATUS_INITIALIZED:
            status_str = "Initialized (OK)";
            break;
        case SDRAM_STATUS_ERROR:
            status_str = "ERROR";
            break;
        default:
            status_str = "Not initialized";
            break;
    }

    print_to_console("[SDRAM Status]\r\n");

    snprintf(buf, sizeof(buf), "  Status      : %s\r\n", status_str);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  BSP Enabled : %s\r\n",
             s_sdram_config.sdram_enabled ? "Yes" : "No");
    print_to_console(buf);

    /* Configuration */
    print_to_console("[SDRAM Configuration]\r\n");

    snprintf(buf, sizeof(buf), "  Base Address: 0x%08lX\r\n",
             (unsigned long)s_sdram_config.base_address);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Size        : %lu MB (%lu bytes)\r\n",
             (unsigned long)(s_sdram_config.size_bytes / (1024UL * 1024UL)),
             (unsigned long)s_sdram_config.size_bytes);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Bus Width   : %lu-bit\r\n",
             (unsigned long)s_sdram_config.bus_width_bits);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  CAS Latency : CL%lu\r\n",
             (unsigned long)s_sdram_config.cas_latency);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Refresh     : %lu rows / %lu ms\r\n",
             (unsigned long)s_sdram_config.refresh_rows,
             (unsigned long)s_sdram_config.refresh_period_ms);
    print_to_console(buf);

    /* Timing parameters from BSP config */
    print_to_console("[SDRAM Timing Parameters]\r\n");

    snprintf(buf, sizeof(buf), "  tRAS        : %d cycles\r\n", BSP_CFG_SDRAM_TRAS);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  tRCD        : %d cycles\r\n", BSP_CFG_SDRAM_TRCD);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  tRP         : %d cycles\r\n", BSP_CFG_SDRAM_TRP);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  tWR         : %d cycles\r\n", BSP_CFG_SDRAM_TWR);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  tRFC        : %d cycles\r\n", BSP_CFG_SDRAM_TRFC);
    print_to_console(buf);

    /* Hardware register status */
    print_to_console("[SDRAM Registers]\r\n");

    snprintf(buf, sizeof(buf), "  SDSR        : 0x%02lX\r\n",
             (unsigned long)R_BUS->SDRAM.SDSR);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  SDCCR       : 0x%02lX (EXENB=%lu, BSIZE=%lu)\r\n",
             (unsigned long)R_BUS->SDRAM.SDCCR,
             (unsigned long)R_BUS->SDRAM.SDCCR_b.EXENB,
             (unsigned long)R_BUS->SDRAM.SDCCR_b.BSIZE);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  SDRFEN      : %lu (Auto-refresh %s)\r\n",
             (unsigned long)R_BUS->SDRAM.SDRFEN,
             R_BUS->SDRAM.SDRFEN ? "Enabled" : "Disabled");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  SDSELF      : %lu (Self-refresh %s)\r\n",
             (unsigned long)R_BUS->SDRAM.SDSELF,
             R_BUS->SDRAM.SDSELF ? "Enabled" : "Disabled");
    print_to_console(buf);
}

/**
 * "sdram check" sub-command handler
 *
 * @details Performs a quick write/readback verification at several SDRAM
 *          locations to confirm basic SDRAM operation.
 *          Tests the following locations:
 *            - Base address (0x68000000)
 *            - End of each 4MB bank boundary
 *            - Near the end of memory
 */
static void sdram_cmd_check(void)
{
    char buf[SDRAM_PRINT_BUF_SIZE];
    bool all_passed = true;

    print_to_console("[SDRAM Quick Check]\r\n");

    if (s_sdram_status != SDRAM_STATUS_INITIALIZED) {
        print_to_console("  Error: SDRAM is not initialized.\r\n");
        return;
    }

    /*
     * Test locations distributed across the SDRAM address space.
     * We test at the base, at several MB boundaries, and near the end.
     */
    const uint32_t test_offsets[SDRAM_SANITY_CHECK_COUNT] = {
        0x00000000UL,   /* Base address */
        0x00400000UL,   /* 4MB offset */
        0x00800000UL,   /* 8MB offset */
        0x00C00000UL,   /* 12MB offset */
        0x01000000UL,   /* 16MB offset */
        0x01400000UL,   /* 20MB offset */
        0x01800000UL,   /* 24MB offset */
        0x01FFFFFCUL,   /* Near end (32MB - 4 bytes) */
    };

    /* Test with pattern A (0xA5A5A5A5) and pattern B (0x5A5A5A5A) */
    for (uint32_t i = 0; i < SDRAM_SANITY_CHECK_COUNT; i++) {
        uint32_t addr = SDRAM_BASE_ADDRESS + test_offsets[i];
        volatile uint32_t *p = (volatile uint32_t *)addr;
        bool pass_a, pass_b;

        /* Save original value for restoration */
        uint32_t original = *p;

        /* Test pattern A */
        pass_a = sdram_write_read_verify(p, SDRAM_TEST_PATTERN_A);

        /* Test pattern B */
        pass_b = sdram_write_read_verify(p, SDRAM_TEST_PATTERN_B);

        /* Restore original value */
        *p = original;
        __DSB();

        bool pass = pass_a && pass_b;
        if (!pass) {
            all_passed = false;
        }

        snprintf(buf, sizeof(buf), "  0x%08lX : %s\r\n",
                 (unsigned long)addr, pass ? "PASS" : "FAIL");
        print_to_console(buf);
    }

    snprintf(buf, sizeof(buf), "  Result: %s\r\n",
             all_passed ? "All checks PASSED" : "Some checks FAILED");
    print_to_console(buf);
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Record that SDRAM initialization has been performed by the BSP
 *
 * @details Called from hal_warmstart.c after R_BSP_SdramInit(true).
 *          Performs a quick sanity check and records the result.
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/hal_entry.c:50
 */
bool sdram_port_init(void)
{
#if BSP_CFG_SDRAM_ENABLED

    /*
     * At this point, R_BSP_SdramInit(true) has already been called by
     * hal_warmstart.c. The SDRAM should be fully initialized with:
     *   - SDCLK output enabled
     *   - Init sequence completed (PRECHARGE ALL, AUTO REFRESH x8, MODE REGISTER SET)
     *   - Timing parameters configured (tRAS, tRCD, tRP, tWR, tCL)
     *   - Auto-refresh enabled
     *   - SDRAM access enabled (EXENB = 1)
     *
     * Reference: ra/fsp/src/bsp/mcu/all/bsp_sdram.c:59-147
     */

    /* Verify SDRAM is accessible by performing a quick write/read at the base */
    volatile uint32_t *p_base = (volatile uint32_t *)SDRAM_BASE_ADDRESS;

    /* Save the original value */
    uint32_t original = *p_base;

    /* Write test pattern and verify */
    bool check_a = sdram_write_read_verify(p_base, SDRAM_TEST_PATTERN_A);
    bool check_b = sdram_write_read_verify(p_base, SDRAM_TEST_PATTERN_B);

    /* Restore original value */
    *p_base = original;
    __DSB();

    if (check_a && check_b) {
        s_sdram_status = SDRAM_STATUS_INITIALIZED;
        return true;
    } else {
        s_sdram_status = SDRAM_STATUS_ERROR;
        return false;
    }

#else
    /* SDRAM is not enabled in BSP configuration */
    s_sdram_status = SDRAM_STATUS_NOT_INITIALIZED;
    return false;
#endif
}

/**
 * Get the current SDRAM initialization status
 */
sdram_status_t sdram_port_get_status(void)
{
    return s_sdram_status;
}

/**
 * Get SDRAM configuration information
 */
const sdram_config_info_t *sdram_port_get_config(void)
{
    return &s_sdram_config;
}

/**
 * Check if SDRAM is available for use
 */
bool sdram_port_is_available(void)
{
    return (s_sdram_status == SDRAM_STATUS_INITIALIZED);
}

/**
 * Perform a quick sanity check on SDRAM
 *
 * @details Tests several locations with alternating patterns.
 *          Does NOT test the full memory (use "sdram test" command from
 *          S-001-4 for full verification).
 */
bool sdram_port_sanity_check(void)
{
    if (s_sdram_status != SDRAM_STATUS_INITIALIZED) {
        return false;
    }

    /* Quick check at base and a few boundaries */
    const uint32_t offsets[] = {
        0x00000000UL,
        0x00800000UL,
        0x01000000UL,
        0x01FFFFFCUL,
    };

    for (uint32_t i = 0; i < (sizeof(offsets) / sizeof(offsets[0])); i++) {
        volatile uint32_t *p = (volatile uint32_t *)(SDRAM_BASE_ADDRESS + offsets[i]);
        uint32_t original = *p;

        if (!sdram_write_read_verify(p, SDRAM_TEST_PATTERN_A)) {
            *p = original;
            __DSB();
            return false;
        }

        if (!sdram_write_read_verify(p, SDRAM_TEST_PATTERN_B)) {
            *p = original;
            __DSB();
            return false;
        }

        *p = original;
        __DSB();
    }

    return true;
}

/**
 * NT-Shell "sdram" command handler
 *
 * @details Provides SDRAM diagnostic sub-commands:
 *   sdram status - Display SDRAM status and configuration
 *   sdram check  - Perform quick sanity check (write/read verification)
 */
int usrcmd_sdram(int argc, char **argv)
{
    if (argc < 2) {
        cmd_print_usage("sdram", "<subcommand>");
        print_to_console("  status  - Show SDRAM initialization status and configuration\r\n");
        print_to_console("  check   - Perform quick write/read verification\r\n");
        return CMD_ERR_USAGE;
    }

    if (ntlibc_strcmp(argv[1], "status") == 0) {
        sdram_cmd_status();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "check") == 0) {
        sdram_cmd_check();
        return CMD_OK;
    }

    /* Unknown sub-command */
    {
        char buf[SDRAM_PRINT_BUF_SIZE];
        snprintf(buf, sizeof(buf), "Error: Unknown sub-command '%s'.\r\n", argv[1]);
        print_to_console(buf);
    }

    cmd_print_usage("sdram", "<subcommand>");
    print_to_console("  status  - Show SDRAM initialization status and configuration\r\n");
    print_to_console("  check   - Perform quick write/read verification\r\n");

    return CMD_ERR_INVALID_ARG;
}
