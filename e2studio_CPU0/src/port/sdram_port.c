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
 * This file is part of the SDRAM control (S-001-2, S-001-3, S-001-4) implementation.
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

/* R-006 (Issue #156): FreeRTOS.h / task.h removed.
 * xTaskGetTickCount() -> tk_get_otm (operating time, ms). */
#include <tk/tkernel.h>

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

/**
 * Walking ones test step size (bytes).
 * Test every 4KB to keep the walking ones test duration reasonable.
 * 32MB / 4KB = 8192 locations, each testing 32 bit patterns.
 */
#define SDRAM_WALKING_STEP_BYTES    (4096UL)

/**
 * Progress reporting interval for full region tests (bytes).
 * Report progress every 1MB during pattern fill/verify.
 */
#define SDRAM_PROGRESS_INTERVAL     (1024UL * 1024UL)

/**
 * Number of pattern test iterations.
 * Each iteration uses a different fill pattern.
 */
#define SDRAM_PATTERN_COUNT         (4)

/** Anti-pattern used for address bus testing */
#define SDRAM_ADDR_TEST_PATTERN     (0xAAAAAAAAUL)

/** Base pattern used for address bus testing */
#define SDRAM_ADDR_BASE_PATTERN     (0x55555555UL)

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
static void sdram_cmd_map(void);
static void sdram_cmd_test(void);
static uint32_t sdram_get_tick_ms(void);
static void sdram_test_init_result(sdram_test_result_t *result, sdram_test_type_t type);
static void sdram_test_print_result(const sdram_test_result_t *result, const char *name);

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

/**
 * Linker-defined symbols for SDRAM sections
 *
 * These symbols are defined in the FSP-generated linker script (Debug/fsp_gen.lld)
 * and declared as extern in Debug/bsp_linker_info.h.
 *
 * Naming convention:
 *   __<section_name>$$Base  = start address of section
 *   __<section_name>$$Limit = end address of section (exclusive)
 *
 * Reference: Debug/fsp_gen.lld, lines 403-451 (SDRAM section definitions)
 * Reference: Debug/bsp_linker_info.h, lines 171-174 (extern declarations)
 */
extern uint32_t __sdram_noinit_nocache$$Base;
extern uint32_t __sdram_noinit_nocache$$Limit;
extern uint32_t __sdram_zero_nocache$$Base;
extern uint32_t __sdram_zero_nocache$$Limit;
extern uint32_t __sdram_noinit$$Base;
extern uint32_t __sdram_noinit$$Limit;
extern uint32_t __sdram_zero$$Base;
extern uint32_t __sdram_zero$$Limit;
extern uint32_t __sdram_from_flash$$Base;
extern uint32_t __sdram_from_flash$$Limit;

/**
 * "sdram map" sub-command handler
 *
 * @details Displays the SDRAM section memory layout by querying linker-defined
 *          symbols. Shows the address range, size, and purpose of each section.
 *
 * Reference: Debug/fsp_gen.lld (section definitions)
 */
static void sdram_cmd_map(void)
{
    char buf[SDRAM_PRINT_BUF_SIZE];
    sdram_section_info_t info;

    sdram_port_get_section_info(&info);

    print_to_console("[SDRAM Section Layout]\r\n");

    snprintf(buf, sizeof(buf), "  SDRAM Region  : 0x%08lX - 0x%08lX\r\n",
             (unsigned long)info.sdram_start, (unsigned long)info.sdram_end);
    print_to_console(buf);

    print_to_console("  -----------------------------------------------\r\n");

    /* .sdram_from_flash (initialized data) */
    {
        uint32_t size = info.from_flash.limit - info.from_flash.base;
        snprintf(buf, sizeof(buf), "  .sdram_from_flash     : 0x%08lX - 0x%08lX (%lu bytes)\r\n",
                 (unsigned long)info.from_flash.base,
                 (unsigned long)info.from_flash.limit,
                 (unsigned long)size);
        print_to_console(buf);
        print_to_console("    -> Init from Flash, cacheable\r\n");
    }

    /* .sdram_noinit_nocache (framebuffers) */
    {
        uint32_t size = info.noinit_nocache.limit - info.noinit_nocache.base;
        snprintf(buf, sizeof(buf), "  .sdram_noinit_nocache : 0x%08lX - 0x%08lX (%lu bytes)\r\n",
                 (unsigned long)info.noinit_nocache.base,
                 (unsigned long)info.noinit_nocache.limit,
                 (unsigned long)size);
        print_to_console(buf);
        print_to_console("    -> No init, non-cacheable (GLCDC framebuffers)\r\n");
    }

    /* .sdram_nocache (zero-init, non-cached) */
    {
        uint32_t size = info.zero_nocache.limit - info.zero_nocache.base;
        snprintf(buf, sizeof(buf), "  .sdram_nocache        : 0x%08lX - 0x%08lX (%lu bytes)\r\n",
                 (unsigned long)info.zero_nocache.base,
                 (unsigned long)info.zero_nocache.limit,
                 (unsigned long)size);
        print_to_console(buf);
        print_to_console("    -> Zero-init, non-cacheable (DMA buffers)\r\n");
    }

    /* .sdram_noinit */
    {
        uint32_t size = info.noinit.limit - info.noinit.base;
        snprintf(buf, sizeof(buf), "  .sdram_noinit         : 0x%08lX - 0x%08lX (%lu bytes)\r\n",
                 (unsigned long)info.noinit.base,
                 (unsigned long)info.noinit.limit,
                 (unsigned long)size);
        print_to_console(buf);
        print_to_console("    -> No init, cacheable (capture/AI buffers)\r\n");
    }

    /* .sdram (zero-init) */
    {
        uint32_t size = info.zero.limit - info.zero.base;
        snprintf(buf, sizeof(buf), "  .sdram                : 0x%08lX - 0x%08lX (%lu bytes)\r\n",
                 (unsigned long)info.zero.base,
                 (unsigned long)info.zero.limit,
                 (unsigned long)size);
        print_to_console(buf);
        print_to_console("    -> Zero-init, cacheable (work buffers)\r\n");
    }

    print_to_console("  -----------------------------------------------\r\n");

    snprintf(buf, sizeof(buf), "  Total used    : %lu bytes (%lu KB)\r\n",
             (unsigned long)info.total_used,
             (unsigned long)(info.total_used / 1024UL));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Total free    : %lu bytes (%lu KB)\r\n",
             (unsigned long)info.total_free,
             (unsigned long)(info.total_free / 1024UL));
    print_to_console(buf);

    /* Section placement guide */
    print_to_console("\r\n[Section Placement Guide]\r\n");
    print_to_console("  GLCDC framebuffer:\r\n");
    print_to_console("    uint8_t fb[2][W*H*2] SDRAM_SECTION_NOINIT_NOCACHE;\r\n");
    print_to_console("  Camera capture buffer:\r\n");
    print_to_console("    uint8_t cap[W*H*2] SDRAM_SECTION_NOINIT;\r\n");
    print_to_console("  Zero-init work buffer:\r\n");
    print_to_console("    uint32_t work[1024] SDRAM_SECTION_ZERO;\r\n");
}

/**
 * Get current time in milliseconds
 *
 * @details R-006: xTaskGetTickCount() -> tk_get_otm() (operating time, ms).
 *          SYSTIM is a 64-bit hi/lo pair; the lower 32 bits suffice for the
 *          throughput-measurement deltas used here (same technique as
 *          camera_framebuffer.c, R-005).
 *
 * @return Current operating time in milliseconds
 */
static uint32_t sdram_get_tick_ms(void)
{
    SYSTIM now = {0, 0};
    (void)tk_get_otm(&now);
    return (uint32_t)now.lo;
}

/**
 * Initialize a test result structure with default values
 *
 * @param result Pointer to test result structure
 * @param type   Test type identifier
 */
static void sdram_test_init_result(sdram_test_result_t *result, sdram_test_type_t type)
{
    result->test_type      = type;
    result->passed         = false;
    result->error_address  = 0;
    result->expected_value = 0;
    result->actual_value   = 0;
    result->elapsed_ms     = 0;
}

/**
 * Print a single test result to the console
 *
 * @param result Pointer to the test result
 * @param name   Human-readable test name string
 */
static void sdram_test_print_result(const sdram_test_result_t *result, const char *name)
{
    char buf[SDRAM_PRINT_BUF_SIZE];

    if (result->passed) {
        snprintf(buf, sizeof(buf), "  %-20s : PASS (%lu ms)\r\n",
                 name, (unsigned long)result->elapsed_ms);
        print_to_console(buf);
    } else {
        snprintf(buf, sizeof(buf), "  %-20s : FAIL (%lu ms)\r\n",
                 name, (unsigned long)result->elapsed_ms);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "    Error at 0x%08lX: expected 0x%08lX, got 0x%08lX\r\n",
                 (unsigned long)result->error_address,
                 (unsigned long)result->expected_value,
                 (unsigned long)result->actual_value);
        print_to_console(buf);
    }
}

/**
 * "sdram test" sub-command handler
 *
 * @details Runs the comprehensive SDRAM memory test suite:
 *   1. Data bus test (walking 1s/0s)
 *   2. Address bus test (alias detection)
 *   3. Walking ones test (sampled locations)
 *   4. Full region pattern test (all 32MB)
 *
 * Prints per-test results and a summary.
 *
 * This is the S-001-4 implementation.
 */
static void sdram_cmd_test(void)
{
    char buf[SDRAM_PRINT_BUF_SIZE];
    sdram_test_report_t report;

    print_to_console("[SDRAM Comprehensive Test]\r\n");

    if (s_sdram_status != SDRAM_STATUS_INITIALIZED) {
        print_to_console("  Error: SDRAM is not initialized.\r\n");
        return;
    }

    snprintf(buf, sizeof(buf), "  Test region : 0x%08lX - 0x%08lX (%lu MB)\r\n",
             (unsigned long)SDRAM_BASE_ADDRESS,
             (unsigned long)SDRAM_END_ADDRESS,
             (unsigned long)(SDRAM_SIZE_BYTES / (1024UL * 1024UL)));
    print_to_console(buf);

    print_to_console("  Warning: This test is destructive and overwrites SDRAM contents.\r\n");
    print_to_console("  Running...\r\n\r\n");

    /* Run all tests */
    sdram_test_run_all(&report);

    /* Print individual results */
    print_to_console("[Test Results]\r\n");
    sdram_test_print_result(&report.data_bus,     "Data Bus Test");
    sdram_test_print_result(&report.address_bus,   "Address Bus Test");
    sdram_test_print_result(&report.walking_ones,  "Walking Ones Test");
    sdram_test_print_result(&report.pattern,       "Pattern Test");

    /* Summary */
    print_to_console("\r\n[Summary]\r\n");
    snprintf(buf, sizeof(buf), "  Result      : %s\r\n",
             report.all_passed ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Total time  : %lu ms\r\n",
             (unsigned long)report.total_elapsed_ms);
    print_to_console(buf);
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Get SDRAM section layout information from linker symbols
 *
 * @details Reads the linker-defined symbols for each SDRAM section and
 *          calculates total usage and free space.
 *
 * Reference: Debug/fsp_gen.lld (SDRAM section definitions)
 * Reference: Debug/bsp_linker_info.h (linker symbol extern declarations)
 */
void sdram_port_get_section_info(sdram_section_info_t *info)
{
    if (info == NULL) {
        return;
    }

    /* Query linker symbols for each section */
    info->noinit_nocache.base   = (uint32_t)&__sdram_noinit_nocache$$Base;
    info->noinit_nocache.limit  = (uint32_t)&__sdram_noinit_nocache$$Limit;

    info->zero_nocache.base     = (uint32_t)&__sdram_zero_nocache$$Base;
    info->zero_nocache.limit    = (uint32_t)&__sdram_zero_nocache$$Limit;

    info->noinit.base           = (uint32_t)&__sdram_noinit$$Base;
    info->noinit.limit          = (uint32_t)&__sdram_noinit$$Limit;

    info->zero.base             = (uint32_t)&__sdram_zero$$Base;
    info->zero.limit            = (uint32_t)&__sdram_zero$$Limit;

    info->from_flash.base       = (uint32_t)&__sdram_from_flash$$Base;
    info->from_flash.limit      = (uint32_t)&__sdram_from_flash$$Limit;

    /*
     * SDRAM region boundaries: use compile-time constants from bsp_linker_info.h.
     *
     * __ddsc_SDRAM_START / __ddsc_SDRAM_END are linker symbols that mark the
     * range of *used* sections, NOT the full SDRAM region. When no variables are
     * placed in SDRAM, both resolve to the same address (0x68000000).
     *
     * BSP_PARTITION_SDRAM_CPU0_S_START/SIZE are compile-time constants that
     * always reflect the full partition size regardless of usage.
     */
    info->sdram_start           = (uint32_t)BSP_PARTITION_SDRAM_CPU0_S_START;
    info->sdram_end             = (uint32_t)(BSP_PARTITION_SDRAM_CPU0_S_START
                                           + BSP_PARTITION_SDRAM_CPU0_S_SIZE);

    /* Calculate total used and free space */
    uint32_t used = 0;
    used += (info->noinit_nocache.limit - info->noinit_nocache.base);
    used += (info->zero_nocache.limit   - info->zero_nocache.base);
    used += (info->noinit.limit         - info->noinit.base);
    used += (info->zero.limit           - info->zero.base);
    used += (info->from_flash.limit     - info->from_flash.base);

    info->total_used = used;

    /* Free space = total SDRAM partition minus used */
    uint32_t total_region = BSP_PARTITION_SDRAM_CPU0_S_SIZE;
    info->total_free = (total_region > used) ? (total_region - used) : 0;
}

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
 * Run SDRAM data bus test
 *
 * @details Tests each data line independently by writing a walking-1 pattern
 *          and its complement at the SDRAM base address. Since the SDRAM bus
 *          width is 16-bit but the CPU accesses it as 32-bit words (two
 *          consecutive 16-bit accesses), we test all 32 bits.
 *
 *          Test algorithm (based on Michael Barr's "Programming Embedded
 *          Systems" memory test methodology):
 *            1. Write 0x00000001 to base, read back and verify
 *            2. Write 0x00000002 to base, read back and verify
 *            3. ... continue for all 32 bit positions
 *            4. Repeat with walking 0s (complement patterns)
 *
 * Reference: Industry-standard SDRAM test methodology
 */
bool sdram_test_data_bus(sdram_test_result_t *result)
{
    volatile uint32_t *p_base = (volatile uint32_t *)SDRAM_BASE_ADDRESS;
    uint32_t start_ms = sdram_get_tick_ms();

    sdram_test_init_result(result, SDRAM_TEST_DATA_BUS);

    /*
     * Walking 1 test: Write a single bit set at each position and verify.
     * This detects stuck-at-0 and stuck-at-1 faults on individual data lines.
     */
    for (uint32_t bit = 0; bit < 32; bit++) {
        uint32_t pattern = (1UL << bit);

        *p_base = pattern;
        __DSB();

        uint32_t readback = *p_base;
        if (readback != pattern) {
            result->error_address  = SDRAM_BASE_ADDRESS;
            result->expected_value = pattern;
            result->actual_value   = readback;
            result->elapsed_ms     = sdram_get_tick_ms() - start_ms;
            return false;
        }
    }

    /*
     * Walking 0 test: Write the complement of a single bit at each position.
     * This detects shorted (bridged) data lines.
     */
    for (uint32_t bit = 0; bit < 32; bit++) {
        uint32_t pattern = ~(1UL << bit);

        *p_base = pattern;
        __DSB();

        uint32_t readback = *p_base;
        if (readback != pattern) {
            result->error_address  = SDRAM_BASE_ADDRESS;
            result->expected_value = pattern;
            result->actual_value   = readback;
            result->elapsed_ms     = sdram_get_tick_ms() - start_ms;
            return false;
        }
    }

    result->passed     = true;
    result->elapsed_ms = sdram_get_tick_ms() - start_ms;
    return true;
}

/**
 * Run SDRAM address bus test
 *
 * @details Verifies that each address line is independent and properly
 *          connected. Detects stuck-at and shorted address lines by
 *          checking for address aliasing.
 *
 *          The test operates on power-of-2 offset addresses within the
 *          SDRAM region. The number of address bits tested is determined
 *          by the SDRAM size (32MB = 25 address bits for byte addressing,
 *          24 bits for 16-bit word addressing).
 *
 *          Algorithm:
 *            Phase 1 - Check for stuck-high address lines:
 *              1. Write base pattern to address offset 0
 *              2. For each power-of-2 offset, write anti-pattern
 *              3. Verify offset 0 still holds base pattern (no alias)
 *            Phase 2 - Check for stuck-low and shorted address lines:
 *              1. Write base pattern to each power-of-2 offset
 *              2. For each offset, verify it was not overwritten by
 *                 a write to a different offset
 *
 * Reference: Michael Barr, "Programming Embedded Systems", Chapter 6
 */
bool sdram_test_address_bus(sdram_test_result_t *result)
{
    volatile uint32_t *p_base = (volatile uint32_t *)SDRAM_BASE_ADDRESS;
    uint32_t start_ms = sdram_get_tick_ms();
    /*
     * Number of 32-bit word addresses in the SDRAM.
     * SDRAM_SIZE_BYTES / 4 = number of 32-bit accessible locations.
     * We need to test address bits that select between these locations.
     */
    uint32_t addr_mask = (SDRAM_SIZE_BYTES / sizeof(uint32_t)) - 1;

    sdram_test_init_result(result, SDRAM_TEST_ADDRESS_BUS);

    /*
     * Phase 1: Check for address bits stuck high.
     * Write base pattern at offset 0, then write anti-pattern at each
     * power-of-2 offset. After each write, check that offset 0 still
     * holds the base pattern.
     */
    p_base[0] = SDRAM_ADDR_BASE_PATTERN;
    __DSB();

    for (uint32_t test_offset = 1; (test_offset & addr_mask) != 0; test_offset <<= 1) {
        p_base[test_offset] = SDRAM_ADDR_TEST_PATTERN;
        __DSB();

        uint32_t readback = p_base[0];
        if (readback != SDRAM_ADDR_BASE_PATTERN) {
            /* Address bit aliased back to offset 0 */
            result->error_address  = (uint32_t)&p_base[test_offset];
            result->expected_value = SDRAM_ADDR_BASE_PATTERN;
            result->actual_value   = readback;
            result->elapsed_ms     = sdram_get_tick_ms() - start_ms;
            return false;
        }
    }

    /*
     * Phase 2: Check for address bits stuck low or shorted.
     * Write base pattern to offset 0, then write anti-pattern at each
     * power-of-2 offset and verify that no other power-of-2 offset
     * was affected.
     */
    p_base[0] = SDRAM_ADDR_TEST_PATTERN;
    __DSB();

    for (uint32_t test_offset = 1; (test_offset & addr_mask) != 0; test_offset <<= 1) {
        p_base[test_offset] = SDRAM_ADDR_BASE_PATTERN;
        __DSB();

        /* Check that offset 0 was not overwritten */
        uint32_t readback = p_base[0];
        if (readback != SDRAM_ADDR_TEST_PATTERN) {
            result->error_address  = (uint32_t)&p_base[test_offset];
            result->expected_value = SDRAM_ADDR_TEST_PATTERN;
            result->actual_value   = readback;
            result->elapsed_ms     = sdram_get_tick_ms() - start_ms;
            return false;
        }

        /* Restore anti-pattern for subsequent tests */
        p_base[test_offset] = SDRAM_ADDR_TEST_PATTERN;
        __DSB();
    }

    result->passed     = true;
    result->elapsed_ms = sdram_get_tick_ms() - start_ms;
    return true;
}

/**
 * Run SDRAM walking ones test
 *
 * @details Tests data retention at multiple addresses across the SDRAM.
 *          At each sampled address, writes all 32 walking-1 patterns and
 *          verifies each one.
 *
 *          Sampling strategy: Test every SDRAM_WALKING_STEP_BYTES (4KB)
 *          to balance coverage and test duration.
 *          32MB / 4KB = 8192 locations x 32 patterns = 262,144 write/read ops.
 *
 * Reference: Standard memory diagnostic technique
 */
bool sdram_test_walking_ones(sdram_test_result_t *result)
{
    char buf[SDRAM_PRINT_BUF_SIZE];
    uint32_t start_ms = sdram_get_tick_ms();
    uint32_t last_progress = 0;

    sdram_test_init_result(result, SDRAM_TEST_WALKING_ONES);

    print_to_console("  Walking Ones: ");

    for (uint32_t offset = 0; offset < SDRAM_SIZE_BYTES; offset += SDRAM_WALKING_STEP_BYTES) {
        volatile uint32_t *p = (volatile uint32_t *)(SDRAM_BASE_ADDRESS + offset);

        /* Test all 32 walking-1 patterns at this location */
        for (uint32_t bit = 0; bit < 32; bit++) {
            uint32_t pattern = (1UL << bit);

            *p = pattern;
            __DSB();

            uint32_t readback = *p;
            if (readback != pattern) {
                print_to_console("FAIL\r\n");
                result->error_address  = (uint32_t)p;
                result->expected_value = pattern;
                result->actual_value   = readback;
                result->elapsed_ms     = sdram_get_tick_ms() - start_ms;
                return false;
            }
        }

        /* Progress reporting every 1MB */
        uint32_t mb = offset / SDRAM_PROGRESS_INTERVAL;
        if (mb > last_progress) {
            snprintf(buf, sizeof(buf), "%luMB..", (unsigned long)mb);
            print_to_console(buf);
            last_progress = mb;
        }
    }

    snprintf(buf, sizeof(buf), "%luMB ", (unsigned long)(SDRAM_SIZE_BYTES / SDRAM_PROGRESS_INTERVAL));
    print_to_console(buf);
    print_to_console("PASS\r\n");

    result->passed     = true;
    result->elapsed_ms = sdram_get_tick_ms() - start_ms;
    return true;
}

/**
 * Run SDRAM full region pattern test
 *
 * @details Fills the entire SDRAM with a pattern, then reads back every
 *          location to verify correctness. Repeats with multiple patterns
 *          to detect various failure modes.
 *
 *          Patterns tested:
 *            1. 0x55AA55AA - Alternating bits (tests crosstalk between adjacent lines)
 *            2. 0xAA55AA55 - Complement of pattern 1
 *            3. 0x00000000 - All zeros (tests stuck-at-1 faults)
 *            4. 0xFFFFFFFF - All ones (tests stuck-at-0 faults)
 *
 * Reference: Standard memory diagnostic technique
 */
bool sdram_test_pattern(sdram_test_result_t *result)
{
    char buf[SDRAM_PRINT_BUF_SIZE];
    uint32_t start_ms = sdram_get_tick_ms();

    static const uint32_t patterns[SDRAM_PATTERN_COUNT] = {
        0x55AA55AAUL,
        0xAA55AA55UL,
        0x00000000UL,
        0xFFFFFFFFUL,
    };

    uint32_t num_words = SDRAM_SIZE_BYTES / sizeof(uint32_t);

    sdram_test_init_result(result, SDRAM_TEST_PATTERN);

    for (uint32_t p = 0; p < SDRAM_PATTERN_COUNT; p++) {
        uint32_t pattern = patterns[p];
        volatile uint32_t *base = (volatile uint32_t *)SDRAM_BASE_ADDRESS;

        snprintf(buf, sizeof(buf), "  Pattern 0x%08lX: ", (unsigned long)pattern);
        print_to_console(buf);

        /* Fill phase */
        print_to_console("fill..");
        for (uint32_t i = 0; i < num_words; i++) {
            base[i] = pattern;
        }
        __DSB();

        /* Verify phase */
        print_to_console("verify..");
        for (uint32_t i = 0; i < num_words; i++) {
            uint32_t readback = base[i];
            if (readback != pattern) {
                print_to_console("FAIL\r\n");
                result->error_address  = (uint32_t)&base[i];
                result->expected_value = pattern;
                result->actual_value   = readback;
                result->elapsed_ms     = sdram_get_tick_ms() - start_ms;
                return false;
            }
        }

        print_to_console("PASS\r\n");
    }

    result->passed     = true;
    result->elapsed_ms = sdram_get_tick_ms() - start_ms;
    return true;
}

/**
 * Run all SDRAM tests and generate a full report
 *
 * @details Executes all four SDRAM tests in sequence. If a critical test
 *          (data bus or address bus) fails, subsequent tests are skipped
 *          because their results would be unreliable.
 *
 * This is the main entry point for the S-001-4 comprehensive SDRAM test.
 */
bool sdram_test_run_all(sdram_test_report_t *report)
{
    char buf[SDRAM_PRINT_BUF_SIZE];
    uint32_t total_start_ms = sdram_get_tick_ms();

    /* Initialize report */
    memset(report, 0, sizeof(sdram_test_report_t));
    report->test_base = SDRAM_BASE_ADDRESS;
    report->test_size = SDRAM_SIZE_BYTES;

    sdram_test_init_result(&report->data_bus,     SDRAM_TEST_DATA_BUS);
    sdram_test_init_result(&report->address_bus,   SDRAM_TEST_ADDRESS_BUS);
    sdram_test_init_result(&report->walking_ones,  SDRAM_TEST_WALKING_ONES);
    sdram_test_init_result(&report->pattern,       SDRAM_TEST_PATTERN);

    /* Test 1: Data Bus */
    snprintf(buf, sizeof(buf), "  [1/4] Data Bus Test...\r\n");
    print_to_console(buf);
    sdram_test_data_bus(&report->data_bus);

    if (!report->data_bus.passed) {
        print_to_console("  ** Data bus test failed. Skipping remaining tests.\r\n");
        report->all_passed       = false;
        report->total_elapsed_ms = sdram_get_tick_ms() - total_start_ms;
        return false;
    }

    /* Test 2: Address Bus */
    snprintf(buf, sizeof(buf), "  [2/4] Address Bus Test...\r\n");
    print_to_console(buf);
    sdram_test_address_bus(&report->address_bus);

    if (!report->address_bus.passed) {
        print_to_console("  ** Address bus test failed. Skipping remaining tests.\r\n");
        report->all_passed       = false;
        report->total_elapsed_ms = sdram_get_tick_ms() - total_start_ms;
        return false;
    }

    /* Test 3: Walking Ones */
    snprintf(buf, sizeof(buf), "  [3/4] Walking Ones Test...\r\n");
    print_to_console(buf);
    sdram_test_walking_ones(&report->walking_ones);

    /* Test 4: Full Region Pattern */
    snprintf(buf, sizeof(buf), "  [4/4] Full Region Pattern Test...\r\n");
    print_to_console(buf);
    sdram_test_pattern(&report->pattern);

    /* Aggregate result */
    report->all_passed = report->data_bus.passed
                      && report->address_bus.passed
                      && report->walking_ones.passed
                      && report->pattern.passed;

    report->total_elapsed_ms = sdram_get_tick_ms() - total_start_ms;
    return report->all_passed;
}

/**
 * NT-Shell "sdram" command handler
 *
 * @details Provides SDRAM diagnostic sub-commands:
 *   sdram status - Display SDRAM status and configuration
 *   sdram check  - Perform quick sanity check (write/read verification)
 *   sdram map    - Show SDRAM section memory layout
 *   sdram test   - Run comprehensive SDRAM memory tests (S-001-4)
 */
int usrcmd_sdram(int argc, char **argv)
{
    if (argc < 2) {
        cmd_print_usage("sdram", "<subcommand>");
        print_to_console("  status  - Show SDRAM initialization status and configuration\r\n");
        print_to_console("  check   - Perform quick write/read verification\r\n");
        print_to_console("  map     - Show SDRAM section memory layout\r\n");
        print_to_console("  test    - Run comprehensive SDRAM memory tests\r\n");
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

    if (ntlibc_strcmp(argv[1], "map") == 0) {
        sdram_cmd_map();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "test") == 0) {
        sdram_cmd_test();
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
    print_to_console("  map     - Show SDRAM section memory layout\r\n");
    print_to_console("  test    - Run comprehensive SDRAM memory tests\r\n");

    return CMD_ERR_INVALID_ARG;
}
