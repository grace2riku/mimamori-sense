/**
 * @file mipi_port.c
 * @brief MIPI D-PHY port layer implementation (S-003-1)
 * @details
 * Implements MIPI D-PHY initialization, status monitoring, and diagnostic
 * functions for the OV5640 camera module on the EK-RA8P1 Camera Expansion Board.
 *
 * This module provides:
 *   - MIPI PHY initialization via R_MIPI_PHY_Open() (FSP driver)
 *   - PHY status tracking (initialization state, PLL frequency)
 *   - D-PHY timing parameter query
 *   - NT-Shell "camera" command for diagnostics
 *
 * Initialization flow (called from camera thread):
 *   1. mipi_phy_port_init()
 *      a. Verify FSP module availability
 *      b. Compute PCLKA clock for timing reference
 *      c. R_MIPI_PHY_Open(&g_mipi_phy0_ctrl, &g_mipi_phy0_cfg)
 *      d. Update status tracking
 *
 * Note: The FSP MIPI PHY module (r_mipi_phy) must be added to configuration.xml
 * before this code can execute. If the module is not configured, all functions
 * return a "no FSP module" status, allowing the system to boot without camera
 * support.
 *
 * Reference:
 *   - camera_thread_entry.c: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c
 *   - R_MIPI_PHY_Open: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra/fsp/inc/instances/r_mipi_phy.h:123
 *   - FSP config: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:3-48
 *
 * @note
 * This file is part of the MIPI PHY initialization (S-003-1) implementation.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>

#include "diag_config.h"
#include "mipi_port.h"
#include "csi2_port.h"
#include "vin_port.h"
#include "camera_test.h"
#include "camera_framebuffer.h"
#include "camera_display.h"
#include "bsp_api.h"
#include "jlink_console.h"
#include "cmd_utils.h"
#include "ntlibc.h"
#include "ov5640.h"
/* Issue #46: IIC1 is now shared by the touch panel AND the DA7212 codec, so
 * the camera diagnostic commands below must hand the channel over explicitly
 * instead of just opening their own master on it. */
#include "i2c_bus0.h"
#include "ov5640_cfg.h"
#include "camera_thread_api.h"

/*
 * FSP MIPI PHY header include
 *
 * The MIPI PHY driver header provides the type definitions and API declarations
 * needed for R_MIPI_PHY_Open() / R_MIPI_PHY_Close(). The FSP-generated instances
 * (g_mipi_phy0_ctrl, g_mipi_phy0_cfg) are declared in common_data.h when the
 * MIPI PHY module is configured in configuration.xml.
 *
 * Until the FSP module is added, we use the MIPI_PORT_FSP_AVAILABLE macro to
 * guard FSP API calls and provide stub implementations.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra/fsp/inc/instances/r_mipi_phy.h
 */

/*
 * Check if the FSP MIPI PHY module is available.
 *
 * When the MIPI PHY module is configured in configuration.xml, the FSP code
 * generator creates g_mipi_phy0 instance in common_data.c/h and includes
 * the r_mipi_phy.h header. We detect this by checking if common_data.h
 * declares the MIPI PHY instance.
 *
 * For now, we use a compile-time feature flag. When the FSP module is added,
 * this flag should be set to 1 by the build system or defined before this include.
 */
#ifndef MIPI_PORT_FSP_AVAILABLE
#define MIPI_PORT_FSP_AVAILABLE     (1)
#endif

#if MIPI_PORT_FSP_AVAILABLE
#include "r_mipi_phy.h"
#include "common_data.h"
#endif

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size (increased for HW register diagnostic output) */
#define MIPI_PRINT_BUF_SIZE         (160)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/** Current MIPI PHY initialization status */
static mipi_phy_status_t s_mipi_phy_status = MIPI_PHY_STATUS_NOT_INITIALIZED;

/** PCLKA clock frequency (computed at init time) */
static uint32_t s_pclka_hz = 0;

/**********************************************************************************************************************
 Private (static) functions prototypes
 *********************************************************************************************************************/
static void mipi_cmd_phy(void);
static void mipi_cmd_init(void);
static void mipi_cmd_sensor(int argc, char **argv);
static void mipi_cmd_thread(void);
#if MIMAMORI_VERBOSE_DIAG
/*
 * Issue #183: "camera timing" / "camera diag" are bring-up-only register dumps.
 * Their string literals are the largest single consumer of CPU0 internal flash
 * in this file, so they are excluded unless MIMAMORI_VERBOSE_DIAG is 1
 * (see src/diag_config.h).
 */
static void mipi_cmd_timing(void);
static void mipi_cmd_diag(void);
#endif

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * "camera phy" sub-command handler
 *
 * @details Displays MIPI PHY initialization status, lane configuration,
 *          PLL settings, and clock frequencies.
 */
static void mipi_cmd_phy(void)
{
    char buf[MIPI_PRINT_BUF_SIZE];
    mipi_phy_info_t info;

    mipi_phy_port_get_info(&info);

    /* Initialization status */
    const char *status_str;
    switch (info.status) {
        case MIPI_PHY_STATUS_INITIALIZED:
            status_str = "Initialized";
            break;
        case MIPI_PHY_STATUS_ERROR:
            status_str = "ERROR";
            break;
        case MIPI_PHY_STATUS_NO_FSP_MODULE:
            status_str = "FSP module not configured";
            break;
        default:
            status_str = "Not initialized";
            break;
    }

    print_to_console("[MIPI D-PHY Status (S-003-1)]\r\n");

    snprintf(buf, sizeof(buf), "  Status      : %s\r\n", status_str);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  PHY Open    : %s\r\n", info.phy_open ? "Yes" : "No");
    print_to_console(buf);

    /* Lane configuration */
    print_to_console("[Lane Configuration]\r\n");

    snprintf(buf, sizeof(buf), "  Data Lanes  : %lu\r\n", (unsigned long)info.num_lanes);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  HS Clock    : %lu MHz\r\n", (unsigned long)info.hsclk_mhz);
    print_to_console(buf);

    /* PLL configuration */
    print_to_console("[PLL Configuration]\r\n");

    snprintf(buf, sizeof(buf), "  PLL Freq    : %lu MHz\r\n",
             (unsigned long)(info.pll_freq_hz / 1000000UL));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Input Clock : %lu MHz (XTAL)\r\n",
             (unsigned long)(MIPI_PHY_XCLK_HZ / 1000000UL));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  PLL Div     : %u\r\n", MIPI_PHY_PLL_DIV);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  PLL Mul     : %u\r\n", MIPI_PHY_PLL_MUL_INT);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  LP Divisor  : %lu\r\n", (unsigned long)info.lp_divisor);
    print_to_console(buf);

    /* PCLKA reference clock */
    print_to_console("[Reference Clock]\r\n");

    snprintf(buf, sizeof(buf), "  PCLKA       : %lu MHz (%lu Hz)\r\n",
             (unsigned long)(info.pclka_hz / 1000000UL),
             (unsigned long)info.pclka_hz);
    print_to_console(buf);

    /* Camera module info */
    print_to_console("[Camera Module]\r\n");
    print_to_console("  Sensor      : OmniVision OV5640\r\n");
    print_to_console("  Interface   : MIPI CSI-2 (2-lane)\r\n");

    snprintf(buf, sizeof(buf), "  I2C Address : 0x%02X (7-bit)\r\n", MIPI_OV5640_I2C_ADDR);
    print_to_console(buf);

    print_to_console("  Max Res     : 2592 x 1944 (5MP)\r\n");

    /* FSP module status */
    print_to_console("[FSP Module]\r\n");
#if MIPI_PORT_FSP_AVAILABLE
    print_to_console("  r_mipi_phy  : Available\r\n");
#else
    print_to_console("  r_mipi_phy  : NOT CONFIGURED\r\n");
    print_to_console("  Action      : Add MIPI PHY module in configuration.xml\r\n");
    print_to_console("                (Stacks > New Stack > MIPI PHY)\r\n");
#endif
}

#if MIMAMORI_VERBOSE_DIAG
/**
 * "camera timing" sub-command handler
 *
 * @details Displays all D-PHY timing parameters in PCLKA cycles.
 *          These values are derived from the MIPI D-PHY specification
 *          and the OV5640 HS clock frequency.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:9-22
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:147-163
 */
static void mipi_cmd_timing(void)
{
    char buf[MIPI_PRINT_BUF_SIZE];
    mipi_phy_timing_info_t timing;

    mipi_phy_port_get_timing_info(&timing);

    print_to_console("[D-PHY Timing Parameters (PCLKA cycles)]\r\n");

    /* Initialization timing */
    print_to_console("[Init Timing]\r\n");

    snprintf(buf, sizeof(buf), "  T_INIT       : %lu\r\n", (unsigned long)timing.t_init);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  T_LP_EXIT    : %lu\r\n", (unsigned long)timing.t_lp_exit);
    print_to_console(buf);

    /* Clock lane timing */
    print_to_console("[Clock Lane Timing]\r\n");

    snprintf(buf, sizeof(buf), "  T_CLK_PREP   : %lu\r\n", (unsigned long)timing.t_clk_prep);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  T_CLK_SETTLE : %lu\r\n", (unsigned long)timing.t_clk_settle);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  T_CLK_MISS   : %lu\r\n", (unsigned long)timing.t_clk_miss);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  T_CLK_ZERO   : %lu\r\n", (unsigned long)timing.t_clk_zero);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  T_CLK_PRE    : %lu\r\n", (unsigned long)timing.t_clk_pre);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  T_CLK_POST   : %lu\r\n", (unsigned long)timing.t_clk_post);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  T_CLK_TRAIL  : %lu\r\n", (unsigned long)timing.t_clk_trail);
    print_to_console(buf);

    /* Data lane timing */
    print_to_console("[Data Lane Timing]\r\n");

    snprintf(buf, sizeof(buf), "  T_HS_PREP    : %lu\r\n", (unsigned long)timing.t_hs_prep);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  T_HS_SETTLE  : %lu\r\n", (unsigned long)timing.t_hs_settle);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  T_HS_ZERO    : %lu\r\n", (unsigned long)timing.t_hs_zero);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  T_HS_TRAIL   : %lu\r\n", (unsigned long)timing.t_hs_trail);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  T_HS_EXIT    : %lu\r\n", (unsigned long)timing.t_hs_exit);
    print_to_console(buf);

    /* Timing derivation note */
    print_to_console("[Notes]\r\n");
    print_to_console("  Timing values are in PCLKA cycles.\r\n");

    if (s_pclka_hz > 0) {
        /* Issue #183: fixed-point formatting instead of %f.
         * period_ns x100 = 1e11 / f_hz; the 64-bit intermediate keeps this
         * exact for the whole PCLKA range without needing a lower bound on
         * s_pclka_hz beyond the > 0 test above.
         * Kept float-free even though this block is excluded from the default
         * build, so that no translation unit in the project requires the
         * floating-point printf variant from libc (see Issue #184). */
        const uint32_t period_ns_x100 = (uint32_t)(100000000000ULL / (uint64_t)s_pclka_hz);

        snprintf(buf, sizeof(buf), "  PCLKA period : %lu.%02lu ns (%lu MHz)\r\n",
                 (unsigned long)(period_ns_x100 / 100),
                 (unsigned long)(period_ns_x100 % 100),
                 (unsigned long)(s_pclka_hz / 1000000UL));
        print_to_console(buf);
    }

    print_to_console("  Source: FSP configuration (configuration.xml)\r\n");
    print_to_console("  Ref: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:9-22\r\n");
}
#endif /* MIMAMORI_VERBOSE_DIAG (Issue #183: mipi_cmd_timing) */

/**
 * "camera init" sub-command handler
 *
 * @details Triggers MIPI PHY initialization manually from the NT-Shell.
 *          Useful for testing and debugging the initialization sequence.
 */
static void mipi_cmd_init(void)
{
    print_to_console("  Initializing MIPI D-PHY...\r\n");

    bool result = mipi_phy_port_init();

    if (result) {
        print_to_console("  MIPI D-PHY initialized successfully.\r\n");
    } else {
        mipi_phy_status_t status = mipi_phy_port_get_status();
        if (status == MIPI_PHY_STATUS_NO_FSP_MODULE) {
            print_to_console("  Error: FSP MIPI PHY module is not configured.\r\n");
            print_to_console("  Add MIPI PHY module in configuration.xml first.\r\n");
        } else {
            print_to_console("  Error: MIPI D-PHY initialization failed.\r\n");
        }
    }
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Initialize the MIPI D-PHY subsystem (S-003-1)
 *
 * @details Performs MIPI PHY initialization:
 *   1. Check if FSP MIPI PHY module is available
 *   2. Get PCLKA clock frequency for timing reference
 *   3. Call R_MIPI_PHY_Open() with FSP-generated configuration
 *
 * The R_MIPI_PHY_Open() function internally:
 *   a. Configures the PHY PLL with the specified frequency settings
 *   b. Programs the D-PHY timing registers (T_INIT, T_CLK_*, T_HS_*)
 *   c. Enables the PHY PLL and waits for lock
 *   d. Configures the LP speed divisor
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:330-331
 *            (VIN Open internally opens MIPI CSI which opens MIPI PHY)
 */
bool mipi_phy_port_init(void)
{
#if MIPI_PORT_FSP_AVAILABLE
    fsp_err_t err;

    /* Get PCLKA clock frequency for reference */
    s_pclka_hz = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKA);

    /*
     * Open the MIPI PHY driver
     *
     * R_MIPI_PHY_Open() configures:
     *   - PHY PLL frequency (1000 MHz for OV5640 at 185 MHz HS clock)
     *   - D-PHY timing parameters (T_INIT, T_CLK_*, T_HS_*)
     *   - LP speed divisor
     *   - CSI mode (not DSI)
     *
     * Note: In the reference project, R_MIPI_PHY_Open() is called internally
     * by R_MIPI_CSI2_Open() which is called by R_VIN_Open(). However, calling
     * it explicitly here allows us to:
     *   1. Verify PHY initialization independently
     *   2. Detect and report PHY-specific errors
     *   3. Query PHY status before proceeding to CSI-2/VIN initialization
     *
     * If R_VIN_Open() calls R_MIPI_PHY_Open() internally, this explicit call
     * will return FSP_ERR_ALREADY_OPEN, which we treat as success.
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra/fsp/inc/instances/r_mipi_phy.h:123
     */
    err = g_mipi_phy0.p_api->open(g_mipi_phy0.p_ctrl, g_mipi_phy0.p_cfg);

    if ((FSP_SUCCESS == err) || (FSP_ERR_ALREADY_OPEN == err)) {
        s_mipi_phy_status = MIPI_PHY_STATUS_INITIALIZED;
        return true;
    }

    s_mipi_phy_status = MIPI_PHY_STATUS_ERROR;
    return false;

#else
    /* FSP MIPI PHY module not configured yet */
    s_pclka_hz = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKA);
    s_mipi_phy_status = MIPI_PHY_STATUS_NO_FSP_MODULE;
    return false;
#endif
}

/**
 * Close the MIPI D-PHY subsystem
 */
bool mipi_phy_port_close(void)
{
#if MIPI_PORT_FSP_AVAILABLE
    fsp_err_t err;

    err = g_mipi_phy0.p_api->close(g_mipi_phy0.p_ctrl);

    if (FSP_SUCCESS == err) {
        s_mipi_phy_status = MIPI_PHY_STATUS_NOT_INITIALIZED;
        return true;
    }

    return false;
#else
    s_mipi_phy_status = MIPI_PHY_STATUS_NOT_INITIALIZED;
    return true;
#endif
}

/**
 * Get the current MIPI PHY initialization status
 */
mipi_phy_status_t mipi_phy_port_get_status(void)
{
    return s_mipi_phy_status;
}

/**
 * Check if MIPI PHY is available for use
 */
bool mipi_phy_port_is_available(void)
{
    return (s_mipi_phy_status == MIPI_PHY_STATUS_INITIALIZED);
}

/**
 * Get MIPI PHY configuration and status information
 *
 * @details Fills the info structure with compile-time and runtime values.
 *          Compile-time values come from the macro definitions (derived from
 *          the FSP-generated configuration in the reference project).
 *          Runtime values come from the BSP API.
 */
void mipi_phy_port_get_info(mipi_phy_info_t *info)
{
    if (info == NULL) {
        return;
    }

    info->status      = s_mipi_phy_status;
    info->num_lanes   = MIPI_PHY_NUM_LANES;
    info->hsclk_mhz   = MIPI_PHY_HSCLK_MHZ;
    info->pll_freq_hz  = MIPI_PHY_PLL_FREQ_HZ;
    info->lp_divisor  = MIPI_PHY_LP_DIVISOR;
    info->pclka_hz    = s_pclka_hz;

#if MIPI_PORT_FSP_AVAILABLE
    info->phy_open = (g_mipi_phy0_ctrl.open != 0);
#else
    info->phy_open = false;
#endif
}

/**
 * Get MIPI PHY D-PHY timing parameters
 *
 * @details Fills the timing info structure with the compile-time constant
 *          timing values derived from the FSP configuration of the reference
 *          project. These values represent PCLKA cycle counts.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:9-22
 */
void mipi_phy_port_get_timing_info(mipi_phy_timing_info_t *timing)
{
    if (timing == NULL) {
        return;
    }

    timing->t_init       = MIPI_PHY_TINIT;
    timing->t_clk_prep   = MIPI_PHY_TCLKPREP;
    timing->t_clk_settle = MIPI_PHY_TCLKSETT;
    timing->t_clk_miss   = MIPI_PHY_TCLKMISS;
    timing->t_hs_prep    = MIPI_PHY_THSPREP;
    timing->t_hs_settle  = MIPI_PHY_THSETT;
    timing->t_clk_zero   = MIPI_PHY_TCLKZERO;
    timing->t_clk_pre    = MIPI_PHY_TCLKPRE;
    timing->t_clk_post   = MIPI_PHY_TCLKPOST;
    timing->t_clk_trail  = MIPI_PHY_TCLKTRAIL;
    timing->t_hs_zero    = MIPI_PHY_THSZERO;
    timing->t_hs_trail   = MIPI_PHY_THSTRAIL;
    timing->t_hs_exit    = MIPI_PHY_THSEXIT;
    timing->t_lp_exit    = MIPI_PHY_TLPEXIT;
}

/**
 * Print camera command usage help
 */
static void mipi_cmd_print_help(void)
{
    cmd_print_usage("camera", "<subcommand>");
    print_to_console("  phy      - Show MIPI D-PHY configuration and status (S-003-1)\r\n");
#if MIMAMORI_VERBOSE_DIAG
    /* Issue #183: only listed when the verbose diagnostics build is enabled. */
    print_to_console("  timing   - Show D-PHY timing parameters (S-003-1)\r\n");
#endif
    print_to_console("  init     - Initialize MIPI D-PHY (S-003-1)\r\n");
    print_to_console("  csi      - Show CSI-2 receiver status and error counters (S-003-2)\r\n");
    print_to_console("  csi reset  - Reset CSI-2 error/frame counters (S-003-2)\r\n");
    print_to_console("  csi init   - Initialize CSI-2 receiver (S-003-2)\r\n");
    print_to_console("  csi start  - Start CSI-2 reception (S-003-2)\r\n");
    print_to_console("  csi stop   - Stop CSI-2 reception (S-003-2)\r\n");
    print_to_console("  status   - Show VIN capture status and statistics (S-003-3)\r\n");
    print_to_console("  start    - Start VIN capture (S-003-3)\r\n");
    print_to_console("  stop     - Stop VIN capture (S-003-3)\r\n");
    print_to_console("  capture  - Capture 1 frame, show buffer address (S-003-3)\r\n");
    print_to_console("  info     - Show camera module and pipeline info (S-003-3)\r\n");
    print_to_console("  reset    - Reset VIN capture statistics (S-003-3)\r\n");
    print_to_console("  thread   - Show camera thread init status (F-002-5)\r\n");
    print_to_console("  sensor       - Show OV5640 sensor driver status (F-002-4)\r\n");
    print_to_console("  sensor init  - Initialize OV5640 sensor (F-002-4)\r\n");
    print_to_console("  sensor id    - Read and verify OV5640 chip ID (F-002-4)\r\n");
    print_to_console("  sensor reg <addr>        - Read OV5640 register (F-002-4)\r\n");
    print_to_console("  sensor reg <addr> <val>  - Write OV5640 register (F-002-4)\r\n");
    print_to_console("  sensor stream on|off     - Control MIPI stream (F-002-4)\r\n");
    print_to_console("  fb       - Show frame buffer status, addresses, FPS (F-002-6)\r\n");
    print_to_console("  display  - Show camera-to-LVGL display transfer status (F-001-8)\r\n");
#if MIMAMORI_VERBOSE_DIAG
    /* Issue #183: only listed when the verbose diagnostics build is enabled. */
    print_to_console("  diag     - MIPI data path diagnostics: OV5640 regs + FSP state (F-002-6)\r\n");
    print_to_console("  test     - Integration test commands (S-003-4)\r\n");
    print_to_console("    test capture         - Single frame capture with validation\r\n");
    print_to_console("    test capture display  - Capture and display on LCD\r\n");
    print_to_console("    test fps [ms]        - FPS measurement (default: 3000 ms)\r\n");
    print_to_console("    test stream [ms]     - Continuous capture + LCD (default: 10000 ms)\r\n");
    print_to_console("    test validate        - Validate last captured frame data\r\n");
#endif
}

/**
 * "camera csi init" sub-command handler
 */
static void mipi_cmd_csi_init(void)
{
    print_to_console("  Initializing CSI-2 receiver...\r\n");

    bool result = csi2_port_init();

    if (result) {
        print_to_console("  CSI-2 receiver initialized successfully.\r\n");
    } else {
        csi2_status_t status = csi2_port_get_status();
        if (status == CSI2_STATUS_NO_FSP_MODULE) {
            print_to_console("  Error: FSP MIPI CSI module is not configured.\r\n");
            print_to_console("  Add MIPI CSI module in configuration.xml first.\r\n");
        } else {
            print_to_console("  Error: CSI-2 receiver initialization failed.\r\n");
        }
    }
}

/**
 * "camera csi start" sub-command handler
 */
static void mipi_cmd_csi_start(void)
{
    if (!csi2_port_is_available()) {
        print_to_console("  Error: CSI-2 receiver is not initialized.\r\n");
        print_to_console("  Run 'camera csi init' first.\r\n");
        return;
    }

    print_to_console("  Starting CSI-2 reception...\r\n");

    if (csi2_port_start()) {
        print_to_console("  CSI-2 reception started.\r\n");
    } else {
        print_to_console("  Error: Failed to start CSI-2 reception.\r\n");
    }
}

/**
 * "camera csi stop" sub-command handler
 */
static void mipi_cmd_csi_stop(void)
{
    print_to_console("  Stopping CSI-2 reception...\r\n");

    if (csi2_port_stop()) {
        print_to_console("  CSI-2 reception stopped.\r\n");
    } else {
        print_to_console("  Error: Failed to stop CSI-2 reception.\r\n");
    }
}

/**
 * "camera start" sub-command handler (S-003-3)
 *
 * @details Initializes VIN (if needed) and starts continuous capture.
 */
static void mipi_cmd_vin_start(void)
{
    /*
     * Guard: If camera_thread has already initialized VIN and started
     * capture, do not call R_VIN_CaptureStart() again. The camera_thread
     * manages VIN directly (bypassing vin_port status), so vin_port's
     * s_vin_status may still be NOT_INITIALIZED even though VIN HW is
     * actively capturing. Re-calling CaptureStart on a running VIN freezes.
     */
    if (camera_thread_is_initialized()) {
        print_to_console("  Camera capture is already running (managed by camera thread).\r\n");
        return;
    }

    vin_port_status_t status = vin_port_get_status();

    /* Auto-initialize if not yet initialized */
    if (status == VIN_PORT_STATUS_NOT_INITIALIZED ||
        status == VIN_PORT_STATUS_NO_FSP_MODULE) {
        print_to_console("  Initializing VIN module...\r\n");
        if (!vin_port_init()) {
            vin_port_status_t new_status = vin_port_get_status();
            if (new_status == VIN_PORT_STATUS_NO_FSP_MODULE) {
                print_to_console("  Error: FSP VIN module is not configured.\r\n");
                print_to_console("  Add VIN module in configuration.xml first.\r\n");
            } else {
                print_to_console("  Error: VIN initialization failed.\r\n");
            }
            return;
        }
        print_to_console("  VIN module initialized.\r\n");
    }

    if (vin_port_get_status() == VIN_PORT_STATUS_CAPTURING) {
        print_to_console("  VIN capture is already running.\r\n");
        return;
    }

    print_to_console("  Starting VIN capture...\r\n");
    if (vin_port_capture_start()) {
        print_to_console("  VIN capture started (continuous mode).\r\n");
    } else {
        print_to_console("  Error: Failed to start VIN capture.\r\n");
    }
}

/**
 * "camera stop" sub-command handler (S-003-3)
 *
 * @details Stops VIN continuous capture.
 */
static void mipi_cmd_vin_stop(void)
{
    /*
     * Guard: When camera_thread is managing VIN, vin_port status tracking
     * is out of sync. Stopping VIN while the camera thread's main loop
     * polls R_VIN_StatusGet() could cause undefined behavior. For now,
     * reject stop when camera_thread owns the capture pipeline.
     */
    if (camera_thread_is_initialized()) {
        print_to_console("  Camera capture is managed by camera thread.\r\n");
        print_to_console("  Stop is not supported while camera thread is running.\r\n");
        return;
    }

    if (!vin_port_is_available()) {
        print_to_console("  Error: VIN is not initialized.\r\n");
        return;
    }

    if (vin_port_get_status() != VIN_PORT_STATUS_CAPTURING) {
        print_to_console("  VIN capture is not running.\r\n");
        return;
    }

    print_to_console("  Stopping VIN capture...\r\n");
    if (vin_port_capture_stop()) {
        print_to_console("  VIN capture stopped.\r\n");
    } else {
        print_to_console("  Error: Failed to stop VIN capture.\r\n");
    }
}

/**
 * "camera capture" sub-command handler (S-003-3)
 *
 * @details Performs a single-shot capture by starting capture, waiting
 *          briefly, and displaying the buffer address and first bytes.
 */
static void mipi_cmd_vin_capture(void)
{
    char buf[MIPI_PRINT_BUF_SIZE];
    vin_port_status_t status = vin_port_get_status();

    /* Auto-initialize if needed */
    if (status == VIN_PORT_STATUS_NOT_INITIALIZED ||
        status == VIN_PORT_STATUS_NO_FSP_MODULE) {
        print_to_console("  Initializing VIN module...\r\n");
        if (!vin_port_init()) {
            vin_port_status_t new_status = vin_port_get_status();
            if (new_status == VIN_PORT_STATUS_NO_FSP_MODULE) {
                print_to_console("  Error: FSP VIN module is not configured.\r\n");
            } else {
                print_to_console("  Error: VIN initialization failed.\r\n");
            }
            return;
        }
    }

    /* Start capture if not already running */
    bool was_capturing = (vin_port_get_status() == VIN_PORT_STATUS_CAPTURING);
    if (!was_capturing) {
        print_to_console("  Starting capture...\r\n");
        if (!vin_port_capture_start()) {
            print_to_console("  Error: Failed to start capture.\r\n");
            return;
        }
    }

    /* Wait for at least one frame (with timeout) */
    print_to_console("  Waiting for frame...\r\n");
    uint32_t initial_count;
    vin_port_info_t info;
    vin_port_get_info(&info);  /* Internally uses __disable_irq for atomic copy */
    initial_count = info.stats.frame_complete;

    /* Poll for frame completion (simple busy-wait with iteration limit) */
    uint32_t timeout = 1000000;
    uint8_t *frame_ptr = NULL;
    while (timeout > 0) {
        frame_ptr = vin_port_get_last_frame();
        vin_port_get_info(&info);
        if (info.stats.frame_complete > initial_count && frame_ptr != NULL) {
            break;
        }
        timeout--;
    }

    if (timeout == 0 || frame_ptr == NULL) {
        print_to_console("  Timeout: No frame captured.\r\n");
        if (!was_capturing) {
            vin_port_capture_stop();
        }
        return;
    }

    /* Display capture result */
    snprintf(buf, sizeof(buf), "  Frame captured at: 0x%08lX\r\n",
             (unsigned long)(uintptr_t)frame_ptr);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Frame size: %lu bytes\r\n",
             (unsigned long)VIN_FRAME_SIZE_BYTES);
    print_to_console(buf);

    /* Show first 16 bytes of the captured frame */
    print_to_console("  First 16 bytes:\r\n    ");
    for (int i = 0; i < 16; i++) {
        snprintf(buf, sizeof(buf), "%02X ", frame_ptr[i]);
        print_to_console(buf);
    }
    print_to_console("\r\n");

    snprintf(buf, sizeof(buf), "  Use 'md 0x%08lX 64' to dump more data.\r\n",
             (unsigned long)(uintptr_t)frame_ptr);
    print_to_console(buf);

    /* Stop capture if we started it */
    if (!was_capturing) {
        vin_port_capture_stop();
        print_to_console("  Capture stopped.\r\n");
    }
}

/**
 * "camera reset" sub-command handler (S-003-3)
 *
 * @details Resets VIN capture statistics counters.
 */
static void mipi_cmd_vin_reset(void)
{
    vin_port_reset_stats();
    print_to_console("  VIN capture statistics reset.\r\n");
}

#if MIMAMORI_VERBOSE_DIAG
/**********************************************************************************************************************
 * Function Name: mipi_cmd_diag
 * Description  : "camera diag" sub-command handler (F-002-6)
 *
 * @details Reads key OV5640 registers related to MIPI output configuration
 *          and frame timing, then displays them alongside the FSP VIN/CSI/PHY
 *          state for diagnosing MIPI CSI-2 data path issues.
 *
 *          Key diagnostic checks:
 *            1. OV5640 system control state (streaming/power-down)
 *            2. OV5640 PLL clock registers (actual vs expected)
 *            3. OV5640 MIPI lane/format configuration
 *            4. OV5640 frame timing (HTS/VTS, output resolution)
 *            5. OV5640 MIPI control and virtual channel settings
 *            6. FSP pipeline state (PHY open, CSI open, VIN open)
 *            7. VIN callback and CSI callback activity summary
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:252-266
 *            reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:817-893
 *********************************************************************************************************************/
static void mipi_cmd_diag(void)
{
    char buf[MIPI_PRINT_BUF_SIZE];

    print_to_console("[MIPI CSI-2 Data Path Diagnostics (F-002-6)]\r\n");

    /*
     * Temporarily open camera I2C to read OV5640 registers.
     *
     * IMPORTANT: After camera init, g_i2c_master_camera_ctrl is closed to
     * release IIC1 for the touch panel (Issue #93 fix). For diagnostics,
     * we temporarily re-open it, read registers, then close it again to
     * avoid the IRQ context conflict with g_i2c_master0 (touch panel).
     *
     * Issue #46: the previous note here said the touch panel's callbacks are
     * only disrupted "while the camera I2C is open" and that this is
     * acceptable. That was wrong - R_IIC_MASTER_Close() calls R_BSP_IrqDisable()
     * and does NOT restore the previous owner's ISR context, so IIC1's
     * interrupts stay DISABLED after this command and both the touch panel and
     * the DA7212 codec time out until reboot. Hand the channel over properly.
     */
    bool diag_i2c_opened = false;
    if (0 == g_i2c_master_camera_ctrl.open)
    {
        /* If the hand-over fails the shared master is STILL open, so opening
         * the camera master anyway would steal its IIC1 IRQ context with no
         * way back (resume() would find nothing to undo). Skip instead. */
        fsp_err_t bus_err = i2c_bus0_suspend();
        if (FSP_SUCCESS != bus_err)
        {
            print_to_console("  WARNING: Could not take IIC1 from the shared bus; skipping register reads.\r\n");
        }
        else
        {
            fsp_err_t i2c_err = R_IIC_MASTER_Open(&g_i2c_master_camera_ctrl, &g_i2c_master_camera_cfg);
            if (FSP_SUCCESS == i2c_err)
            {
                diag_i2c_opened = true;
            }
            else
            {
                (void)i2c_bus0_resume();
                print_to_console("  WARNING: Could not open camera I2C for register reads.\r\n");
            }
        }
    }

    /*
     * Whether the OV5640 sections below can actually talk to the sensor.
     *
     * Without this every ov5640_read_reg() returns the 0xFF error sentinel and
     * the command would print fabricated register values, PLL dividers and
     * frame timings as if they were real - far worse than omitting the
     * section. Note this is NOT `diag_i2c_opened`: the camera master may have
     * been open already, in which case we did not open it and must not close
     * it, but the reads are still valid.
     */
    const bool cam_i2c_usable = (0 != g_i2c_master_camera_ctrl.open);

    if (!cam_i2c_usable)
    {
        print_to_console("[OV5640 Registers]\r\n");
        print_to_console("  SKIPPED: camera I2C unavailable.\r\n\r\n");
    }

    /* ---- OV5640 System State ---- */
    if (cam_i2c_usable)
    {
        print_to_console("[OV5640 System State]\r\n");
        uint8_t reg_3008 = ov5640_read_reg(0x3008);   /* System control */
        uint8_t reg_4202 = ov5640_read_reg(0x4202);   /* Stream control */

        snprintf(buf, sizeof(buf), "  reg[0x3008] = 0x%02X  (sys ctrl: %s)\r\n",
                 reg_3008,
                 (reg_3008 & 0x40) ? "POWER-DOWN" : ((reg_3008 & 0x80) ? "RESET" : "RUNNING"));
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  reg[0x4202] = 0x%02X  (stream: %s)\r\n",
                 reg_4202,
                 (reg_4202 == 0x00) ? "ON" : "OFF");
        print_to_console(buf);
    }

    /* ---- OV5640 PLL Configuration ---- */
    if (cam_i2c_usable)
    {
        print_to_console("[OV5640 PLL Registers]\r\n");
        uint8_t reg_3034 = ov5640_read_reg(0x3034);   /* PLL ctrl0: MIPI bit mode */
        uint8_t reg_3035 = ov5640_read_reg(0x3035);   /* PLL ctrl1: sys/MIPI div */
        uint8_t reg_3036 = ov5640_read_reg(0x3036);   /* PLL ctrl2: multiplier */
        uint8_t reg_3037 = ov5640_read_reg(0x3037);   /* PLL ctrl3: root/pre div */
        uint8_t reg_3108 = ov5640_read_reg(0x3108);   /* System root divider */

        snprintf(buf, sizeof(buf), "  reg[0x3034] = 0x%02X  (MIPI bit mode: %u)\r\n",
                 reg_3034, (unsigned)(reg_3034 & 0x0F));
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  reg[0x3035] = 0x%02X  (sys_div=%u, mipi_div=%u)\r\n",
                 reg_3035, (unsigned)((reg_3035 >> 4) & 0x0F), (unsigned)(reg_3035 & 0x0F));
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  reg[0x3036] = 0x%02X  (PLL mul=%u)\r\n",
                 reg_3036, (unsigned)reg_3036);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  reg[0x3037] = 0x%02X  (root_div=%s, pre_div=%u)\r\n",
                 reg_3037,
                 (reg_3037 & 0x10) ? "/2" : "bypass",
                 (unsigned)(reg_3037 & 0x0F));
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  reg[0x3108] = 0x%02X  (pclk/sclk2x/sclk root div)\r\n",
                 reg_3108);
        print_to_console(buf);
    }

    /* ---- OV5640 MIPI Configuration ---- */
    if (cam_i2c_usable)
    {
        print_to_console("[OV5640 MIPI Configuration]\r\n");
        uint8_t reg_300e = ov5640_read_reg(0x300e);   /* MIPI control 00 */
        uint8_t reg_4800 = ov5640_read_reg(0x4800);   /* MIPI CTRL 00 */
        uint8_t reg_4814 = ov5640_read_reg(0x4814);   /* Virtual channel */
        uint8_t reg_4837 = ov5640_read_reg(0x4837);   /* MIPI global timing */

        snprintf(buf, sizeof(buf), "  reg[0x300e] = 0x%02X  (MIPI %s, %s lane)\r\n",
                 reg_300e,
                 (reg_300e & 0x40) ? "enable" : "DISABLE",
                 (reg_300e & 0x20) ? "1" : "2");
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  reg[0x4800] = 0x%02X  (clk: %s, gate: %s)\r\n",
                 reg_4800,
                 (reg_4800 & 0x20) ? "non-continuous" : "continuous",
                 (reg_4800 & 0x01) ? "on" : "off");
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  reg[0x4814] = 0x%02X  (VC=%u)\r\n",
                 reg_4814, (unsigned)((reg_4814 >> 6) & 0x03));
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  reg[0x4837] = 0x%02X  (MIPI global timing)\r\n",
                 reg_4837);
        print_to_console(buf);
    }

    /* ---- OV5640 Frame Timing ---- */
    if (cam_i2c_usable)
    {
        print_to_console("[OV5640 Frame Timing]\r\n");
        /* Output resolution (DVPHO/DVPVO) */
        uint16_t dvpho = ((uint16_t)ov5640_read_reg(0x3808) << 8) |
                           (uint16_t)ov5640_read_reg(0x3809);
        uint16_t dvpvo = ((uint16_t)ov5640_read_reg(0x380a) << 8) |
                           (uint16_t)ov5640_read_reg(0x380b);

        snprintf(buf, sizeof(buf), "  Output size : %u x %u (DVPHO x DVPVO)\r\n",
                 (unsigned)dvpho, (unsigned)dvpvo);
        print_to_console(buf);

        /* HTS and VTS (Horizontal/Vertical Total Size) */
        uint16_t hts = ((uint16_t)ov5640_read_reg(0x380c) << 8) |
                        (uint16_t)ov5640_read_reg(0x380d);
        uint16_t vts = ((uint16_t)ov5640_read_reg(0x380e) << 8) |
                        (uint16_t)ov5640_read_reg(0x380f);

        snprintf(buf, sizeof(buf), "  HTS         : %u  (Horizontal Total Size)\r\n",
                 (unsigned)hts);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  VTS         : %u  (Vertical Total Size)\r\n",
                 (unsigned)vts);
        print_to_console(buf);

        if (hts == 0 || vts == 0)
        {
            print_to_console("  *** WARNING: HTS/VTS = 0 means no valid frame timing! ***\r\n");
        }

        /* Sensor window (crop area) */
        uint16_t x_start = ((uint16_t)ov5640_read_reg(0x3800) << 8) |
                             (uint16_t)ov5640_read_reg(0x3801);
        uint16_t y_start = ((uint16_t)ov5640_read_reg(0x3802) << 8) |
                             (uint16_t)ov5640_read_reg(0x3803);
        uint16_t x_end   = ((uint16_t)ov5640_read_reg(0x3804) << 8) |
                             (uint16_t)ov5640_read_reg(0x3805);
        uint16_t y_end   = ((uint16_t)ov5640_read_reg(0x3806) << 8) |
                             (uint16_t)ov5640_read_reg(0x3807);

        snprintf(buf, sizeof(buf), "  Crop window : (%u,%u)-(%u,%u)\r\n",
                 (unsigned)x_start, (unsigned)y_start,
                 (unsigned)x_end, (unsigned)y_end);
        print_to_console(buf);

        /* X/Y increment (sub-sampling) */
        uint8_t x_inc = ov5640_read_reg(0x3814);
        uint8_t y_inc = ov5640_read_reg(0x3815);

        snprintf(buf, sizeof(buf), "  X/Y inc     : 0x%02X / 0x%02X\r\n",
                 x_inc, y_inc);
        print_to_console(buf);
    }

    /* ---- OV5640 ISP/Output Format ---- */
    if (cam_i2c_usable)
    {
        print_to_console("[OV5640 Output Format]\r\n");
        uint8_t reg_4300 = ov5640_read_reg(0x4300);   /* Format control */
        uint8_t reg_501f = ov5640_read_reg(0x501f);   /* ISP format select */
        uint8_t reg_5001 = ov5640_read_reg(0x5001);   /* ISP control */

        snprintf(buf, sizeof(buf), "  reg[0x4300] = 0x%02X  (format ctrl)\r\n", reg_4300);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  reg[0x501f] = 0x%02X  (ISP format: %s)\r\n",
                 reg_501f,
                 (reg_501f == 0x00) ? "YUV422" : ((reg_501f == 0x01) ? "RGB" : "RAW"));
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  reg[0x5001] = 0x%02X  (SDE=%u, scale=%u, AWB=%u)\r\n",
                 reg_5001,
                 (unsigned)((reg_5001 >> 7) & 1),
                 (unsigned)((reg_5001 >> 5) & 1),
                 (unsigned)(reg_5001 & 1));
        print_to_console(buf);
    }

    /* ---- MIPI_IF_EN (P108) Pin State ---- */
    print_to_console("[MIPI_IF_EN (P108)]\r\n");
    {
        /*
         * Read P108 pin level. This pin controls the Camera Expansion Board's
         * MIPI data path. Must be LOW for MIPI data to reach the receiver.
         */
        uint32_t pin_level = R_BSP_PinRead(BSP_IO_PORT_01_PIN_08);

        snprintf(buf, sizeof(buf), "  P108 level  : %s (%s)\r\n",
                 (pin_level == 0) ? "LOW" : "HIGH",
                 (pin_level == 0) ? "MIPI enabled" : "MIPI DISABLED");
        print_to_console(buf);

        if (pin_level != 0)
        {
            print_to_console("  *** WARNING: MIPI_IF_EN is HIGH - no MIPI data can reach CSI-2! ***\r\n");
        }
    }

    /* ---- FSP Pipeline State ---- */
    print_to_console("[FSP Pipeline State]\r\n");
    {
        mipi_phy_info_t phy_info;
        mipi_phy_port_get_info(&phy_info);

        csi2_info_t csi_info;
        csi2_port_get_info(&csi_info);

        vin_port_info_t vin_info;
        vin_port_get_info(&vin_info);

        snprintf(buf, sizeof(buf), "  MIPI PHY    : %s\r\n",
                 phy_info.phy_open ? "Open" : "CLOSED");
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  MIPI CSI-2  : %s\r\n",
                 csi_info.csi_open ? "Open" : "CLOSED");
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  VIN         : %s\r\n",
                 vin_info.vin_open ? "Open" : "CLOSED");
        print_to_console(buf);

        /* Callback activity summary */
        snprintf(buf, sizeof(buf), "  VIN callbacks     : %lu (frames: %lu, errors: %lu)\r\n",
                 (unsigned long)vin_info.stats.callback_count,
                 (unsigned long)vin_info.stats.frame_complete,
                 (unsigned long)vin_info.stats.error_event);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  CSI-2 callbacks   : %lu (FS: %lu, FE: %lu)\r\n",
                 (unsigned long)csi_info.callback_count,
                 (unsigned long)csi_info.frames.frame_start,
                 (unsigned long)csi_info.frames.frame_end);
        print_to_console(buf);

        /* CSI-2 error summary (only if errors exist) */
        uint32_t total_csi_errors =
            csi_info.errors.ecc_corrected + csi_info.errors.ecc_uncorrected +
            csi_info.errors.crc_error + csi_info.errors.sot_error +
            csi_info.errors.sot_sync_error + csi_info.errors.id_error +
            csi_info.errors.word_count_error + csi_info.errors.frame_sync_error;

        if (total_csi_errors > 0)
        {
            snprintf(buf, sizeof(buf), "  CSI-2 errors      : %lu total\r\n",
                     (unsigned long)total_csi_errors);
            print_to_console(buf);
        }
        else
        {
            print_to_console("  CSI-2 errors      : 0\r\n");
        }

        /* VIN hardware state */
        uint32_t hw_state;
        if (vin_port_get_hw_status(&hw_state))
        {
            const char *state_str;
            switch (hw_state) {
                case 0:  state_str = "IDLE";        break;
                case 1:  state_str = "IN_PROGRESS"; break;
                case 2:  state_str = "BUSY";        break;
                default: state_str = "UNKNOWN";     break;
            }
            snprintf(buf, sizeof(buf), "  VIN HW state      : %s\r\n", state_str);
            print_to_console(buf);
        }
    }

    /* ---- MIPI D-PHY / CSI-2 / VIN Hardware Registers ---- */
    /*
     * Direct hardware register reads for low-level debugging.
     * These registers are read from the CMSIS-style peripheral pointers
     * (R_MIPI_PHY, R_MIPI_CSI, R_VIN) defined in R7KA8P1KF_core0.h.
     *
     * Base addresses:
     *   R_MIPI_PHY: 0x40346C00
     *   R_MIPI_CSI: 0x40347000
     *   R_VIN:      0x40347400
     */
    print_to_console("[MIPI D-PHY Hardware Registers]\r\n");
    {
        /* DPHYREFCR (offset 0x00): Reference clock frequency */
        uint32_t dphyrefcr = R_MIPI_PHY->DPHYREFCR;
        snprintf(buf, sizeof(buf), "  DPHYREFCR   = 0x%08lX  (RFREQ=%lu -> %lu MHz ref clk)\r\n",
                 (unsigned long)dphyrefcr,
                 (unsigned long)(dphyrefcr & 0xFF),
                 (unsigned long)((dphyrefcr & 0xFF) + 1));
        print_to_console(buf);

        /* DPHYPLFCR (offset 0x04): PLL frequency control */
        uint32_t dphyplfcr = R_MIPI_PHY->DPHYPLFCR;
        snprintf(buf, sizeof(buf), "  DPHYPLFCR   = 0x%08lX  (IDIV=%lu, NFMUL=%lu, PMUL=%lu, NMUL=%lu)\r\n",
                 (unsigned long)dphyplfcr,
                 (unsigned long)(dphyplfcr & 0x3),
                 (unsigned long)((dphyplfcr >> 8) & 0x3),
                 (unsigned long)((dphyplfcr >> 12) & 0x3),
                 (unsigned long)((dphyplfcr >> 16) & 0x1FF));
        print_to_console(buf);

        /* DPHYPLOCR (offset 0x08): PLL operation control */
        uint32_t dphyplocr = R_MIPI_PHY->DPHYPLOCR;
        snprintf(buf, sizeof(buf), "  DPHYPLOCR   = 0x%08lX  (PLLSTP=%lu: %s)\r\n",
                 (unsigned long)dphyplocr,
                 (unsigned long)(dphyplocr & 0x1),
                 (dphyplocr & 0x1) ? "PLL STOPPED" : "PLL running");
        print_to_console(buf);

        /* DPHYESCCR (offset 0x0C): Escape mode clock control */
        uint32_t dphyesccr = R_MIPI_PHY->DPHYESCCR;
        snprintf(buf, sizeof(buf), "  DPHYESCCR   = 0x%08lX  (ESCDIV=%lu)\r\n",
                 (unsigned long)dphyesccr,
                 (unsigned long)(dphyesccr & 0x1F));
        print_to_console(buf);

        /* DPHYPWRCR (offset 0x10): Power supplying control */
        uint32_t dphypwrcr = R_MIPI_PHY->DPHYPWRCR;
        snprintf(buf, sizeof(buf), "  DPHYPWRCR   = 0x%08lX  (PWRSEN=%lu: LDO %s)\r\n",
                 (unsigned long)dphypwrcr,
                 (unsigned long)(dphypwrcr & 0x1),
                 (dphypwrcr & 0x1) ? "enabled" : "DISABLED");
        print_to_console(buf);

        /* DPHYSFR (offset 0x1C): Status flag register - KEY DIAGNOSTIC */
        uint32_t dphysfr = R_MIPI_PHY->DPHYSFR;
        snprintf(buf, sizeof(buf), "  DPHYSFR     = 0x%08lX  (PWRSF=%lu: LDO %s, PLLSF=%lu: PLL %s)\r\n",
                 (unsigned long)dphysfr,
                 (unsigned long)(dphysfr & 0x1),
                 (dphysfr & 0x1) ? "OK" : "NOT READY",
                 (unsigned long)((dphysfr >> 8) & 0x1),
                 ((dphysfr >> 8) & 0x1) ? "LOCKED" : "NOT LOCKED");
        print_to_console(buf);

        if (!(dphysfr & 0x1))
        {
            print_to_console("  *** CRITICAL: D-PHY LDO not powered on! ***\r\n");
        }
        if (!((dphysfr >> 8) & 0x1))
        {
            print_to_console("  *** NOTE: D-PHY PLL not locked (expected for CSI RX slave mode) ***\r\n");
        }

        /* DPHYOCR (offset 0x20): Operation control */
        uint32_t dphyocr = R_MIPI_PHY->DPHYOCR;
        snprintf(buf, sizeof(buf), "  DPHYOCR     = 0x%08lX  (DPHYEN=%lu: D-PHY %s)\r\n",
                 (unsigned long)dphyocr,
                 (unsigned long)(dphyocr & 0x1),
                 (dphyocr & 0x1) ? "ENABLED" : "DISABLED");
        print_to_console(buf);

        if (!(dphyocr & 0x1))
        {
            print_to_console("  *** CRITICAL: D-PHY is disabled! ***\r\n");
        }

        /* DPHYMDC (offset 0x48): Mode control */
        uint32_t dphymdc = R_MIPI_PHY->DPHYMDC;
        snprintf(buf, sizeof(buf), "  DPHYMDC     = 0x%08lX  (MASTEREN=%lu: %s mode)\r\n",
                 (unsigned long)dphymdc,
                 (unsigned long)(dphymdc & 0x1),
                 (dphymdc & 0x1) ? "DSI Master" : "CSI Slave");
        print_to_console(buf);

        if (dphymdc & 0x1)
        {
            print_to_console("  *** WARNING: D-PHY is in DSI Master mode, should be CSI Slave! ***\r\n");
        }

        /* Timing registers (summary) */
        snprintf(buf, sizeof(buf), "  DPHYTIM1    = 0x%08lX  (T_INIT)\r\n",
                 (unsigned long)R_MIPI_PHY->DPHYTIM1);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  DPHYTIM2    = 0x%08lX  (CLK_PREP=%lu, CLK_SETTLE=%lu, CLK_MISS=%lu)\r\n",
                 (unsigned long)R_MIPI_PHY->DPHYTIM2,
                 (unsigned long)(R_MIPI_PHY->DPHYTIM2 & 0xFF),
                 (unsigned long)((R_MIPI_PHY->DPHYTIM2 >> 8) & 0xFF),
                 (unsigned long)((R_MIPI_PHY->DPHYTIM2 >> 16) & 0xFF));
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  DPHYTIM3    = 0x%08lX  (HS_PREP=%lu, HS_SETTLE=%lu)\r\n",
                 (unsigned long)R_MIPI_PHY->DPHYTIM3,
                 (unsigned long)(R_MIPI_PHY->DPHYTIM3 & 0xFF),
                 (unsigned long)((R_MIPI_PHY->DPHYTIM3 >> 8) & 0xFF));
        print_to_console(buf);
    }

    print_to_console("[MIPI CSI-2 Hardware Registers]\r\n");
    {
        /* MCG (offset 0x00): Module configuration (read-only) */
        uint32_t mcg = R_MIPI_CSI->MCG;
        snprintf(buf, sizeof(buf), "  MCG         = 0x%08lX  (VER=%lu, SDLN=%lu lanes, GSNM=%lu)\r\n",
                 (unsigned long)mcg,
                 (unsigned long)(mcg & 0xF),
                 (unsigned long)((mcg >> 8) & 0xF),
                 (unsigned long)((mcg >> 16) & 0xFF));
        print_to_console(buf);

        /* MCT0 (offset 0x10): Module control 0 */
        uint32_t mct0 = R_MIPI_CSI->MCT0;
        snprintf(buf, sizeof(buf), "  MCT0        = 0x%08lX  (VDLN=%lu lanes, ECCV13=%lu, LFSREN=%lu)\r\n",
                 (unsigned long)mct0,
                 (unsigned long)(mct0 & 0xF),
                 (unsigned long)((mct0 >> 24) & 0x1),
                 (unsigned long)((mct0 >> 25) & 0x1));
        print_to_console(buf);

        /* MCT2 (offset 0x18): Module control 2 */
        uint32_t mct2 = R_MIPI_CSI->MCT2;
        snprintf(buf, sizeof(buf), "  MCT2        = 0x%08lX  (FRRCLK=%lu, FRRSKW=%lu)\r\n",
                 (unsigned long)mct2,
                 (unsigned long)(mct2 & 0x1FF),
                 (unsigned long)((mct2 >> 16) & 0x1FF));
        print_to_console(buf);

        /* MCT3 (offset 0x1C): Module control 3 - RX Enable - KEY DIAGNOSTIC */
        uint32_t mct3 = R_MIPI_CSI->MCT3;
        snprintf(buf, sizeof(buf), "  MCT3        = 0x%08lX  (RXEN=%lu: CSI-2 RX %s)\r\n",
                 (unsigned long)mct3,
                 (unsigned long)(mct3 & 0x1),
                 (mct3 & 0x1) ? "ENABLED" : "DISABLED");
        print_to_console(buf);

        if (!(mct3 & 0x1))
        {
            print_to_console("  *** CRITICAL: CSI-2 reception is DISABLED! ***\r\n");
        }

        /* RTST (offset 0x2C): Reset status */
        uint32_t rtst = R_MIPI_CSI->RTST;
        snprintf(buf, sizeof(buf), "  RTST        = 0x%08lX  (VSRSTS=%lu: %s)\r\n",
                 (unsigned long)rtst,
                 (unsigned long)(rtst & 0x1),
                 (rtst & 0x1) ? "RESET IN PROGRESS" : "normal");
        print_to_console(buf);

        /* MIST (offset 0x50): Module interrupt status - shows pending interrupts */
        uint32_t mist = R_MIPI_CSI->MIST;
        snprintf(buf, sizeof(buf), "  MIST        = 0x%08lX  (DL0=%lu, DL1=%lu, PM=%lu, GST=%lu, RX=%lu, VC0=%lu)\r\n",
                 (unsigned long)mist,
                 (unsigned long)(mist & 0x1),
                 (unsigned long)((mist >> 1) & 0x1),
                 (unsigned long)((mist >> 8) & 0x1),
                 (unsigned long)((mist >> 9) & 0x1),
                 (unsigned long)((mist >> 10) & 0x1),
                 (unsigned long)((mist >> 16) & 0x1));
        print_to_console(buf);

        /* DTEL/DTEH (offset 0x60/0x64): Data type enable */
        uint32_t dtel = R_MIPI_CSI->DTEL;
        uint32_t dteh = R_MIPI_CSI->DTEH;
        snprintf(buf, sizeof(buf), "  DTEL        = 0x%08lX  (data types 0x00-0x1F enable)\r\n",
                 (unsigned long)dtel);
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "  DTEH        = 0x%08lX  (data types 0x20-0x3F enable)\r\n",
                 (unsigned long)dteh);
        print_to_console(buf);

        /* RXST (offset 0x70): Receive status - KEY DIAGNOSTIC */
        uint32_t rxst = R_MIPI_CSI->RXST;
        snprintf(buf, sizeof(buf), "  RXST        = 0x%08lX  (FRM0=%lu, RACT=%lu, RACTDET=%lu)\r\n",
                 (unsigned long)rxst,
                 (unsigned long)(rxst & 0x1),
                 (unsigned long)((rxst >> 16) & 0x1),
                 (unsigned long)((rxst >> 17) & 0x1));
        print_to_console(buf);

        if ((rxst >> 17) & 0x1)
        {
            print_to_console("  ** RX activity detected on CSI-2 **\r\n");
        }
        else
        {
            print_to_console("  *** NO RX activity detected - no MIPI data arriving ***\r\n");
        }

        /* RXIE (offset 0x78): Receive interrupt enable */
        uint32_t rxie = R_MIPI_CSI->RXIE;
        snprintf(buf, sizeof(buf), "  RXIE        = 0x%08lX  (RACTDETE=%lu)\r\n",
                 (unsigned long)rxie,
                 (unsigned long)((rxie >> 17) & 0x1));
        print_to_console(buf);

        /* DLST0/DLST1 (offset 0x80/0x90): Data lane status - KEY DIAGNOSTIC */
        uint32_t dlst0 = R_MIPI_CSI->DLST0;
        uint32_t dlst1 = R_MIPI_CSI->DLST1;
        snprintf(buf, sizeof(buf), "  DLST0       = 0x%08lX  (ESH=%lu, ESS=%lu, ECT=%lu, EES=%lu, ULP=%lu)\r\n",
                 (unsigned long)dlst0,
                 (unsigned long)(dlst0 & 0x1),
                 (unsigned long)((dlst0 >> 1) & 0x1),
                 (unsigned long)((dlst0 >> 2) & 0x1),
                 (unsigned long)((dlst0 >> 3) & 0x1),
                 (unsigned long)((dlst0 >> 24) & 0x1));
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  DLST1       = 0x%08lX  (ESH=%lu, ESS=%lu, ECT=%lu, EES=%lu, ULP=%lu)\r\n",
                 (unsigned long)dlst1,
                 (unsigned long)(dlst1 & 0x1),
                 (unsigned long)((dlst1 >> 1) & 0x1),
                 (unsigned long)((dlst1 >> 2) & 0x1),
                 (unsigned long)((dlst1 >> 3) & 0x1),
                 (unsigned long)((dlst1 >> 24) & 0x1));
        print_to_console(buf);

        if ((dlst0 & 0x3) || (dlst1 & 0x3))
        {
            print_to_console("  ** Data lane SoT errors detected - check signal integrity **\r\n");
        }

        /* DLIE0/DLIE1 (offset 0x88/0x98): Data lane interrupt enable */
        uint32_t dlie0 = R_MIPI_CSI->DLIE0;
        uint32_t dlie1 = R_MIPI_CSI->DLIE1;
        snprintf(buf, sizeof(buf), "  DLIE0       = 0x%08lX  (lane 0 int enable)\r\n",
                 (unsigned long)dlie0);
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "  DLIE1       = 0x%08lX  (lane 1 int enable)\r\n",
                 (unsigned long)dlie1);
        print_to_console(buf);

        /* VCST0 (offset 0x100): Virtual Channel 0 status - KEY DIAGNOSTIC */
        uint32_t vcst0 = R_MIPI_CSI->VCST0;
        snprintf(buf, sizeof(buf), "  VCST0       = 0x%08lX\r\n", (unsigned long)vcst0);
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "    Errors: MLF=%lu, ECD=%lu, CRC=%lu, IDE=%lu, WCE=%lu\r\n",
                 (unsigned long)(vcst0 & 0x1),
                 (unsigned long)((vcst0 >> 1) & 0x1),
                 (unsigned long)((vcst0 >> 2) & 0x1),
                 (unsigned long)((vcst0 >> 3) & 0x1),
                 (unsigned long)((vcst0 >> 4) & 0x1));
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "    ECC: corrected=%lu, no_error=%lu\r\n",
                 (unsigned long)((vcst0 >> 5) & 0x1),
                 (unsigned long)((vcst0 >> 6) & 0x1));
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "    Frame: FRS=%lu, FRD=%lu, FSR=%lu, FER=%lu, LSR=%lu, LER=%lu\r\n",
                 (unsigned long)((vcst0 >> 8) & 0x1),
                 (unsigned long)((vcst0 >> 9) & 0x1),
                 (unsigned long)((vcst0 >> 24) & 0x1),
                 (unsigned long)((vcst0 >> 25) & 0x1),
                 (unsigned long)((vcst0 >> 26) & 0x1),
                 (unsigned long)((vcst0 >> 27) & 0x1));
        print_to_console(buf);

        if ((vcst0 >> 24) & 0x1)
        {
            print_to_console("  ** VC0 Frame Start received! **\r\n");
        }

        /* VCIE0 (offset 0x108): Virtual Channel 0 interrupt enable */
        uint32_t vcie0 = R_MIPI_CSI->VCIE0;
        snprintf(buf, sizeof(buf), "  VCIE0       = 0x%08lX  (VC0 int enable: FSR=%lu, FER=%lu)\r\n",
                 (unsigned long)vcie0,
                 (unsigned long)((vcie0 >> 24) & 0x1),
                 (unsigned long)((vcie0 >> 25) & 0x1));
        print_to_console(buf);

        /* PMST (offset 0x200): Power Management status - KEY DIAGNOSTIC */
        uint32_t pmst = R_MIPI_CSI->PMST;
        snprintf(buf, sizeof(buf), "  PMST        = 0x%08lX\r\n", (unsigned long)pmst);
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "    Data lanes: stop_exit=%lu, stop_entry=%lu\r\n",
                 (unsigned long)(pmst & 0x1),
                 (unsigned long)((pmst >> 1) & 0x1));
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "    Clk  lane : stop_exit=%lu, stop_entry=%lu\r\n",
                 (unsigned long)((pmst >> 2) & 0x1),
                 (unsigned long)((pmst >> 3) & 0x1));
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "    ULPS: data_exit=%lu, data_entry=%lu, clk_exit=%lu, clk_entry=%lu\r\n",
                 (unsigned long)((pmst >> 4) & 0x1),
                 (unsigned long)((pmst >> 5) & 0x1),
                 (unsigned long)((pmst >> 6) & 0x1),
                 (unsigned long)((pmst >> 7) & 0x1));
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "    CLSS=%lu (clk stop), CLUL=%lu (clk ULPS), DLSS=0x%lX (data stop), DLUL=0x%lX (data ULPS)\r\n",
                 (unsigned long)((pmst >> 14) & 0x1),
                 (unsigned long)((pmst >> 15) & 0x1),
                 (unsigned long)((pmst >> 16) & 0x3),
                 (unsigned long)((pmst >> 24) & 0x3));
        print_to_console(buf);

        /* PMIE (offset 0x208): Power Management interrupt enable */
        uint32_t pmie = R_MIPI_CSI->PMIE;
        snprintf(buf, sizeof(buf), "  PMIE        = 0x%08lX  (PM interrupt enable)\r\n",
                 (unsigned long)pmie);
        print_to_console(buf);
    }

    print_to_console("[VIN Hardware Registers]\r\n");
    {
        /* MC (offset 0x00): Main control - KEY DIAGNOSTIC */
        uint32_t mc = R_VIN->MC;
        snprintf(buf, sizeof(buf), "  VnMC        = 0x%08lX  (ME=%lu, BPS=%lu, INF=%lu, SCLE=%lu)\r\n",
                 (unsigned long)mc,
                 (unsigned long)(mc & 0x1),
                 (unsigned long)((mc >> 1) & 0x1),
                 (unsigned long)((mc >> 16) & 0x7),
                 (unsigned long)((mc >> 26) & 0x1));
        print_to_console(buf);

        if (!(mc & 0x1))
        {
            print_to_console("  *** VIN Module Enable is OFF ***\r\n");
        }

        /* MS (offset 0x04): Module status - KEY DIAGNOSTIC */
        uint32_t ms = R_VIN->MS;
        snprintf(buf, sizeof(buf), "  VnMS        = 0x%08lX  (CA=%lu, AV=%lu, FS=%lu, FBS=%lu, MA=%lu, FMS=%lu)\r\n",
                 (unsigned long)ms,
                 (unsigned long)(ms & 0x1),
                 (unsigned long)((ms >> 1) & 0x1),
                 (unsigned long)((ms >> 2) & 0x1),
                 (unsigned long)((ms >> 3) & 0x3),
                 (unsigned long)((ms >> 16) & 0x1),
                 (unsigned long)((ms >> 19) & 0x3));
        print_to_console(buf);

        /* FC (offset 0x08): Frame capture */
        uint32_t fc = R_VIN->FC;
        snprintf(buf, sizeof(buf), "  VnFC        = 0x%08lX  (CC=%lu: continuous capture %s)\r\n",
                 (unsigned long)fc,
                 (unsigned long)((fc >> 1) & 0x1),
                 ((fc >> 1) & 0x1) ? "ENABLED" : "DISABLED");
        print_to_console(buf);

        /* IE (offset 0x40): Interrupt enable - KEY DIAGNOSTIC */
        uint32_t ie = R_VIN->IE;
        snprintf(buf, sizeof(buf), "  VnIE        = 0x%08lX  (FOE=%lu, EFE=%lu, FIE=%lu, FME=%lu, VRE=%lu, VFE=%lu)\r\n",
                 (unsigned long)ie,
                 (unsigned long)(ie & 0x1),
                 (unsigned long)((ie >> 1) & 0x1),
                 (unsigned long)((ie >> 4) & 0x1),
                 (unsigned long)((ie >> 5) & 0x1),
                 (unsigned long)((ie >> 16) & 0x1),
                 (unsigned long)((ie >> 17) & 0x1));
        print_to_console(buf);

        if (!((ie >> 5) & 0x1))
        {
            print_to_console("  *** WARNING: FME (Frame Memory write complete) interrupt is DISABLED ***\r\n");
        }

        /* INTS (offset 0x44): Interrupt status */
        uint32_t ints = R_VIN->INTS;
        snprintf(buf, sizeof(buf), "  VnINTS      = 0x%08lX  (FOS=%lu, EFS=%lu, FIS=%lu, FMS=%lu, VRS=%lu, VFS=%lu)\r\n",
                 (unsigned long)ints,
                 (unsigned long)(ints & 0x1),
                 (unsigned long)((ints >> 1) & 0x1),
                 (unsigned long)((ints >> 4) & 0x1),
                 (unsigned long)((ints >> 5) & 0x1),
                 (unsigned long)((ints >> 16) & 0x1),
                 (unsigned long)((ints >> 17) & 0x1));
        print_to_console(buf);

        /* CSI_IFMD (offset 0x20): CSI2 interface mode */
        uint32_t csi_ifmd = R_VIN->CSI_IFMD;
        snprintf(buf, sizeof(buf), "  VnCSI_IFMD  = 0x%08lX  (VC_SEL=%lu, DT=0x%02lX)\r\n",
                 (unsigned long)csi_ifmd,
                 (unsigned long)(csi_ifmd & 0xF),
                 (unsigned long)((csi_ifmd >> 8) & 0x3F));
        print_to_console(buf);

        /* DMR (offset 0x58): Data mode register */
        uint32_t dmr = R_VIN->DMR;
        snprintf(buf, sizeof(buf), "  VnDMR       = 0x%08lX  (DTMD=%lu, BPSM=%lu, EXRGB=%lu)\r\n",
                 (unsigned long)dmr,
                 (unsigned long)(dmr & 0x3),
                 (unsigned long)((dmr >> 4) & 0x1),
                 (unsigned long)((dmr >> 8) & 0x1));
        print_to_console(buf);

        /* Pre-clip registers */
        snprintf(buf, sizeof(buf), "  VnSLPRC     = 0x%08lX  (start line: %lu)\r\n",
                 (unsigned long)R_VIN->SLPRC,
                 (unsigned long)(R_VIN->SLPRC & 0xFFF));
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "  VnELPRC     = 0x%08lX  (end line: %lu)\r\n",
                 (unsigned long)R_VIN->ELPRC,
                 (unsigned long)(R_VIN->ELPRC & 0xFFF));
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "  VnSPPRC     = 0x%08lX  (start pixel: %lu)\r\n",
                 (unsigned long)R_VIN->SPPRC,
                 (unsigned long)(R_VIN->SPPRC & 0xFFF));
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "  VnEPPRC     = 0x%08lX  (end pixel: %lu)\r\n",
                 (unsigned long)R_VIN->EPPRC,
                 (unsigned long)(R_VIN->EPPRC & 0xFFF));
        print_to_console(buf);

        /* IS (offset 0x2C): Image stride */
        snprintf(buf, sizeof(buf), "  VnIS        = 0x%08lX  (stride: %lu pixels)\r\n",
                 (unsigned long)R_VIN->IS,
                 (unsigned long)(R_VIN->IS & 0x1FFF));
        print_to_console(buf);

        /* MB1/MB2/MB3 (offset 0x30/0x34/0x38): Memory base addresses */
        snprintf(buf, sizeof(buf), "  VnMB1       = 0x%08lX\r\n", (unsigned long)R_VIN->MB1);
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "  VnMB2       = 0x%08lX\r\n", (unsigned long)R_VIN->MB2);
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "  VnMB3       = 0x%08lX\r\n", (unsigned long)R_VIN->MB3);
        print_to_console(buf);

        /* LC (offset 0x3C): Line count */
        uint32_t lc = R_VIN->LC;
        snprintf(buf, sizeof(buf), "  VnLC        = 0x%08lX  (line count: %lu)\r\n",
                 (unsigned long)lc,
                 (unsigned long)(lc & 0xFFF));
        print_to_console(buf);
    }

    /* ---- Diagnosis ---- */
    print_to_console("[Diagnosis]\r\n");
    {
        vin_port_info_t vin_info;
        vin_port_get_info(&vin_info);

        csi2_info_t csi_info;
        csi2_port_get_info(&csi_info);

        if (vin_info.stats.frame_complete > 0)
        {
            print_to_console("  PASS: VIN frame complete events received.\r\n");
        }
        else if (csi_info.frames.frame_start > 0)
        {
            print_to_console("  PARTIAL: CSI-2 FS received but VIN not completing.\r\n");
            print_to_console("  Check: VIN preclip/format vs OV5640 output size.\r\n");
        }
        else if (csi_info.callback_count > 0)
        {
            print_to_console("  PARTIAL: CSI-2 callbacks but no FS packets.\r\n");
            print_to_console("  Check: OV5640 stream/power state, MIPI lane config.\r\n");
        }
        else
        {
            print_to_console("  FAIL: No callbacks from CSI-2 or VIN.\r\n");
            print_to_console("  Check: OV5640 MIPI output (0x300e), PLL lock,\r\n");
            print_to_console("         stream ctrl (0x4202), HTS/VTS registers.\r\n");
            /*
             * Provide targeted diagnosis based on HW register state:
             */
            uint32_t hw_dphyocr = R_MIPI_PHY->DPHYOCR;
            uint32_t hw_dphysfr = R_MIPI_PHY->DPHYSFR;
            uint32_t hw_mct3    = R_MIPI_CSI->MCT3;
            uint32_t hw_mc      = R_VIN->MC;
            uint32_t hw_fc      = R_VIN->FC;
            uint32_t hw_ie      = R_VIN->IE;

            if (!(hw_dphysfr & 0x1))
            {
                print_to_console("  -> D-PHY LDO not powered: MIPI PHY not initialized.\r\n");
            }
            else if (!(hw_dphyocr & 0x1))
            {
                print_to_console("  -> D-PHY disabled: PHY_Open may not have been called.\r\n");
            }
            else if (!(hw_mct3 & 0x1))
            {
                print_to_console("  -> CSI-2 RX disabled: CSI_Start may not have been called.\r\n");
            }
            else if (!(hw_mc & 0x1))
            {
                print_to_console("  -> VIN ME=0: VIN_CaptureStart not called or VIN reset.\r\n");
            }
            else if (!((hw_fc >> 1) & 0x1))
            {
                print_to_console("  -> VIN CC=0: Continuous capture not enabled.\r\n");
            }
            else if (!((hw_ie >> 5) & 0x1))
            {
                print_to_console("  -> VIN FME=0: Frame complete interrupt not enabled.\r\n");
            }
            else
            {
                print_to_console("  -> All HW control bits look correct.\r\n");
                print_to_console("     Possible causes:\r\n");
                print_to_console("       - OV5640 not streaming (check 0x4202, 0x3008)\r\n");
                print_to_console("       - MIPI clock not reaching D-PHY (check XCLK, board HW)\r\n");
                print_to_console("       - Signal integrity issue (check camera cable)\r\n");
                print_to_console("       - CSI-2 timing mismatch (THS_SETTLE/TCLK_SETTLE)\r\n");
            }
        }
    }

    /*
     * Close camera I2C if we opened it for diagnostics.
     * This releases IIC1 back for the touch panel's exclusive use.
     *
     * Reference: Issue #93 fix - IIC1 channel conflict prevention
     */
    if (diag_i2c_opened)
    {
        R_IIC_MASTER_Close(&g_i2c_master_camera_ctrl);
        (void)i2c_bus0_resume();        /* Issue #46: restores IIC1's ISR context */
    }
}
#endif /* MIMAMORI_VERBOSE_DIAG (Issue #183: mipi_cmd_diag) */

/**
 * NT-Shell "camera" command handler (S-003-1 / S-003-2 / S-003-3 / S-003-4)
 *
 * @details Provides camera/MIPI diagnostic sub-commands:
 *   camera phy       - Show MIPI PHY initialization state and configuration (S-003-1)
 *   camera timing    - Show D-PHY timing parameters (S-003-1)
 *   camera init      - Initialize MIPI PHY (S-003-1)
 *   camera csi       - Show CSI-2 receiver status and error counters (S-003-2)
 *   camera csi reset - Reset CSI-2 error/frame counters (S-003-2)
 *   camera csi init  - Initialize CSI-2 receiver (S-003-2)
 *   camera csi start - Start CSI-2 reception (S-003-2)
 *   camera csi stop  - Stop CSI-2 reception (S-003-2)
 *   camera status    - Show VIN capture status and statistics (S-003-3)
 *   camera start     - Start VIN capture (S-003-3)
 *   camera stop      - Stop VIN capture (S-003-3)
 *   camera capture   - Capture 1 frame, show buffer address (S-003-3)
 *   camera info      - Show camera module and pipeline info (S-003-3)
 *   camera reset     - Reset VIN capture statistics (S-003-3)
 *   camera test capture         - Single frame capture with validation (S-003-4)
 *   camera test capture display - Single frame capture and LCD display (S-003-4)
 *   camera test fps [ms]        - FPS measurement (S-003-4)
 *   camera test stream [ms]     - Continuous capture + LCD display (S-003-4)
 *   camera test validate        - Validate last captured frame data (S-003-4)
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return CMD_OK on success, CMD_ERR_* on error
 */
int usrcmd_camera(int argc, char **argv)
{
    if (argc < 2) {
        mipi_cmd_print_help();
        return CMD_ERR_USAGE;
    }

    /* S-003-1: D-PHY sub-commands */
    if (ntlibc_strcmp(argv[1], "phy") == 0) {
        mipi_cmd_phy();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "timing") == 0) {
#if MIMAMORI_VERBOSE_DIAG
        mipi_cmd_timing();
        return CMD_OK;
#else
        /* Issue #183: excluded from the default build (see src/diag_config.h). */
        cmd_print_diag_disabled("camera timing");
        return CMD_ERR_EXECUTE;
#endif
    }

    if (ntlibc_strcmp(argv[1], "init") == 0) {
        mipi_cmd_init();
        return CMD_OK;
    }

    /* F-002-4: OV5640 sensor sub-commands */
    if (ntlibc_strcmp(argv[1], "sensor") == 0) {
        mipi_cmd_sensor(argc, argv);
        return CMD_OK;
    }

    /* F-002-5: Camera thread status sub-command */
    if (ntlibc_strcmp(argv[1], "thread") == 0) {
        mipi_cmd_thread();
        return CMD_OK;
    }

    /* S-003-2: CSI-2 sub-commands */
    if (ntlibc_strcmp(argv[1], "csi") == 0) {
        if (argc < 3) {
            /* "camera csi" with no further args: show status */
            csi2_cmd_status();
            return CMD_OK;
        }

        if (ntlibc_strcmp(argv[2], "reset") == 0) {
            csi2_cmd_reset();
            return CMD_OK;
        }

        if (ntlibc_strcmp(argv[2], "init") == 0) {
            mipi_cmd_csi_init();
            return CMD_OK;
        }

        if (ntlibc_strcmp(argv[2], "start") == 0) {
            mipi_cmd_csi_start();
            return CMD_OK;
        }

        if (ntlibc_strcmp(argv[2], "stop") == 0) {
            mipi_cmd_csi_stop();
            return CMD_OK;
        }

        /* Unknown CSI sub-command */
        {
            char buf[MIPI_PRINT_BUF_SIZE];
            snprintf(buf, sizeof(buf), "Error: Unknown CSI sub-command '%s'.\r\n", argv[2]);
            print_to_console(buf);
        }
        print_to_console("  Available: csi [reset|init|start|stop]\r\n");
        return CMD_ERR_INVALID_ARG;
    }

    /* S-003-3: VIN capture sub-commands */
    if (ntlibc_strcmp(argv[1], "status") == 0) {
        vin_cmd_status();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "start") == 0) {
        mipi_cmd_vin_start();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "stop") == 0) {
        mipi_cmd_vin_stop();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "capture") == 0) {
        mipi_cmd_vin_capture();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "info") == 0) {
        vin_cmd_info();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "reset") == 0) {
        mipi_cmd_vin_reset();
        return CMD_OK;
    }

    /* F-002-6: Frame buffer management sub-command */
    if (ntlibc_strcmp(argv[1], "fb") == 0) {
        camera_framebuffer_cmd_status();
        return CMD_OK;
    }

    /* F-001-8: Camera display transfer sub-command */
    if (ntlibc_strcmp(argv[1], "display") == 0) {
        camera_display_cmd_status();
        return CMD_OK;
    }

    /* F-002-6: MIPI data path diagnostics sub-command */
    if (ntlibc_strcmp(argv[1], "diag") == 0) {
#if MIMAMORI_VERBOSE_DIAG
        mipi_cmd_diag();
        return CMD_OK;
#else
        /* Issue #183: excluded from the default build (see src/diag_config.h). */
        cmd_print_diag_disabled("camera diag");
        return CMD_ERR_EXECUTE;
#endif
    }

    /* S-003-4: Integration test sub-commands */
    if (ntlibc_strcmp(argv[1], "test") == 0) {
        camera_test_cmd(argc, argv);
        return CMD_OK;
    }

    /* Unknown sub-command */
    {
        char buf[MIPI_PRINT_BUF_SIZE];
        snprintf(buf, sizeof(buf), "Error: Unknown sub-command '%s'.\r\n", argv[1]);
        print_to_console(buf);
    }

    mipi_cmd_print_help();

    return CMD_ERR_INVALID_ARG;
}

/**********************************************************************************************************************
 * Function Name: mipi_cmd_sensor
 * Description  : "camera sensor" sub-command handler for OV5640 diagnostics
 *              : Provides sensor status display, initialization, chip ID verification,
 *              : register read/write, and stream control via NT-Shell.
 * Arguments    : argc - argument count (from usrcmd_camera)
 *              : argv - argument vector
 * Return Value : None
 *********************************************************************************************************************/
static void mipi_cmd_sensor(int argc, char **argv)
{
    char buf[MIPI_PRINT_BUF_SIZE];

    /* "camera sensor" with no further args: show status */
    if (argc < 3) {
        const ov5640_status_t *st = ov5640_get_status();
        print_to_console("=== OV5640 Sensor Driver Status ===\r\n");

        snprintf(buf, sizeof(buf), "  Initialized    : %s\r\n", st->initialized ? "Yes" : "No");
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  Streaming      : %s\r\n", st->streaming ? "Yes" : "No");
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  Chip ID verified: %s\r\n", st->chip_id_verified ? "Yes" : "No");
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  Chip ID        : 0x%04X\r\n", st->chip_id);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  Init errors    : %lu\r\n", (unsigned long)st->init_error_count);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  I2C addr       : 0x%02X (7-bit)\r\n", OV5640_I2C_ADDR);
        print_to_console(buf);

        return;
    }

    /* "camera sensor init" - Full sensor initialization */
    if (ntlibc_strcmp(argv[2], "init") == 0) {
        print_to_console("  Initializing OV5640 sensor...\r\n");

        fsp_err_t err = ov5640_init();

        if (FSP_SUCCESS == err) {
            const ov5640_status_t *st = ov5640_get_status();
            snprintf(buf, sizeof(buf), "  OV5640 initialized successfully. Chip ID: 0x%04X\r\n", st->chip_id);
            print_to_console(buf);

            snprintf(buf, sizeof(buf), "  Init errors: %lu\r\n", (unsigned long)st->init_error_count);
            print_to_console(buf);
        } else {
            const ov5640_status_t *st = ov5640_get_status();
            snprintf(buf, sizeof(buf), "  Error: OV5640 init failed. Chip ID read: 0x%04X\r\n", st->chip_id);
            print_to_console(buf);
        }
        return;
    }

    /* "camera sensor id" - Read and verify chip ID */
    if (ntlibc_strcmp(argv[2], "id") == 0) {
        print_to_console("  Reading OV5640 chip ID...\r\n");

        /*
         * Temporarily open camera I2C for register reads.
         * Must close after to avoid IIC1 conflict with touch panel.
         * Reference: Issue #93 fix
         */
        bool id_i2c_opened = false;
        if (0 == g_i2c_master_camera_ctrl.open) {
            /* Issue #46: hand IIC1 over - see i2c_bus0_suspend(). A failed
             * hand-over leaves the shared master open, so opening ours anyway
             * would steal its IRQ context irrecoverably. */
            if (FSP_SUCCESS != i2c_bus0_suspend()) {
                print_to_console("  ERROR: Could not take IIC1 from the shared bus.\r\n");
                return;
            }
            if (FSP_SUCCESS == R_IIC_MASTER_Open(&g_i2c_master_camera_ctrl, &g_i2c_master_camera_cfg)) {
                id_i2c_opened = true;
            } else {
                (void)i2c_bus0_resume();
                print_to_console("  ERROR: Could not open camera I2C.\r\n");
                return;
            }
        }

        uint8_t id_h = ov5640_read_reg(OV5640_REG_CHIP_ID_H);
        uint8_t id_l = ov5640_read_reg(OV5640_REG_CHIP_ID_L);
        uint16_t chip_id = (uint16_t)((uint16_t)id_h << 8) | (uint16_t)id_l;

        snprintf(buf, sizeof(buf), "  Chip ID: 0x%04X (high=0x%02X, low=0x%02X)\r\n", chip_id, id_h, id_l);
        print_to_console(buf);

        fsp_err_t err = ov5640_verify_chip_id();
        if (FSP_SUCCESS == err) {
            print_to_console("  Chip ID verification: PASS\r\n");
        } else {
            print_to_console("  Chip ID verification: FAIL (expected 0x56xx where xx=40/41/4C)\r\n");
        }

        if (id_i2c_opened) {
            R_IIC_MASTER_Close(&g_i2c_master_camera_ctrl);
            (void)i2c_bus0_resume();
        }
        return;
    }

    /* "camera sensor reg <addr> [val]" - Register read/write */
    if (ntlibc_strcmp(argv[2], "reg") == 0) {
        if (argc < 4) {
            print_to_console("  Usage: camera sensor reg <addr> [value]\r\n");
            print_to_console("    Read:  camera sensor reg 0x300A\r\n");
            print_to_console("    Write: camera sensor reg 0x300A 0x56\r\n");
            return;
        }

        /*
         * Temporarily open camera I2C for register access.
         * Must close after to avoid IIC1 conflict with touch panel.
         * Reference: Issue #93 fix
         */
        bool reg_i2c_opened = false;
        if (0 == g_i2c_master_camera_ctrl.open) {
            /* Issue #46: hand IIC1 over - see i2c_bus0_suspend(). A failed
             * hand-over leaves the shared master open, so opening ours anyway
             * would steal its IRQ context irrecoverably. */
            if (FSP_SUCCESS != i2c_bus0_suspend()) {
                print_to_console("  ERROR: Could not take IIC1 from the shared bus.\r\n");
                return;
            }
            if (FSP_SUCCESS == R_IIC_MASTER_Open(&g_i2c_master_camera_ctrl, &g_i2c_master_camera_cfg)) {
                reg_i2c_opened = true;
            } else {
                (void)i2c_bus0_resume();
                print_to_console("  ERROR: Could not open camera I2C.\r\n");
                return;
            }
        }

        cmd_parse_result_t addr_result = cmd_parse_uint32(argv[3]);
        uint32_t addr = addr_result.value;

        if (argc >= 5) {
            /* Write register */
            cmd_parse_result_t val_result = cmd_parse_uint32(argv[4]);
            uint32_t val = val_result.value;
            fsp_err_t err = ov5640_write_reg((uint16_t)addr, (uint8_t)val);
            if (FSP_SUCCESS == err) {
                uint8_t readback = ov5640_read_reg((uint16_t)addr);
                snprintf(buf, sizeof(buf), "  Write: reg[0x%04X] = 0x%02X (readback: 0x%02X)\r\n",
                         (unsigned)addr, (unsigned)val, readback);
            } else {
                snprintf(buf, sizeof(buf), "  Error: Write to reg[0x%04X] failed (err=%d)\r\n",
                         (unsigned)addr, (int)err);
            }
        } else {
            /* Read register */
            uint8_t val = ov5640_read_reg((uint16_t)addr);
            snprintf(buf, sizeof(buf), "  Read: reg[0x%04X] = 0x%02X\r\n", (unsigned)addr, val);
        }
        print_to_console(buf);

        if (reg_i2c_opened) {
            R_IIC_MASTER_Close(&g_i2c_master_camera_ctrl);
            (void)i2c_bus0_resume();
        }
        return;
    }

    /* "camera sensor stream on|off" - Stream control */
    if (ntlibc_strcmp(argv[2], "stream") == 0) {
        if (argc < 4) {
            print_to_console("  Usage: camera sensor stream on|off\r\n");
            return;
        }

        if (ntlibc_strcmp(argv[3], "on") == 0) {
            ov5640_stream_on();
            print_to_console("  OV5640 stream ON\r\n");
        } else if (ntlibc_strcmp(argv[3], "off") == 0) {
            ov5640_stream_off();
            print_to_console("  OV5640 stream OFF\r\n");
        } else {
            print_to_console("  Usage: camera sensor stream on|off\r\n");
        }
        return;
    }

    /* Unknown sensor sub-command */
    snprintf(buf, sizeof(buf), "  Error: Unknown sensor sub-command '%s'.\r\n", argv[2]);
    print_to_console(buf);
    print_to_console("  Available: sensor [init|id|reg|stream]\r\n");
}

/**********************************************************************************************************************
 * Function Name: mipi_cmd_thread
 * Description  : "camera thread" sub-command handler (F-002-5)
 *
 * @details Displays camera thread initialization state, OV5640 sensor status,
 *          and VIN capture statistics in a combined summary view.
 *          Queries thread state via camera_thread_api.h functions and
 *          aggregates status from the OV5640 driver and VIN port layer.
 *********************************************************************************************************************/
static void mipi_cmd_thread(void)
{
    char buf[MIPI_PRINT_BUF_SIZE];

    print_to_console("[Camera Thread Status (F-002-5)]\r\n");

    /* Thread init state */
    bool initialized = camera_thread_is_initialized();
    bool has_error = camera_thread_has_error();

    const char *state_str;
    if (has_error) {
        state_str = "ERROR (init failed)";
    } else if (initialized) {
        state_str = "Running (capture active)";
    } else {
        state_str = "Initializing...";
    }

    snprintf(buf, sizeof(buf), "  Thread state : %s\r\n", state_str);
    print_to_console(buf);

    /* OV5640 sensor status summary */
    const ov5640_status_t *sensor_st = ov5640_get_status();
    snprintf(buf, sizeof(buf), "  OV5640       : %s (Chip ID: 0x%04X)\r\n",
             sensor_st->initialized ? "Initialized" : "Not initialized",
             sensor_st->chip_id);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Streaming    : %s\r\n",
             sensor_st->streaming ? "Yes" : "No");
    print_to_console(buf);

    /*
     * VIN capture status summary
     *
     * camera_thread_entry.c calls R_VIN_Open / R_VIN_CaptureStart directly
     * (not through vin_port_init), so vin_port's internal s_vin_status is
     * not updated. Derive VIN status from camera thread state and the
     * actual FSP open flag (g_vin0_ctrl.open) instead.
     */
    vin_port_info_t vin_info;
    vin_port_get_info(&vin_info);

    const char *vin_status_str;
    if (initialized) {
        vin_status_str = "Capturing";
    } else if (has_error) {
        vin_status_str = vin_info.vin_open ? "Opened (error)" : "ERROR";
    } else if (vin_info.vin_open) {
        vin_status_str = "Opened (not yet capturing)";
    } else {
        vin_status_str = "Not initialized";
    }

    snprintf(buf, sizeof(buf), "  VIN status   : %s\r\n", vin_status_str);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Frames       : %lu complete, %lu errors\r\n",
             (unsigned long)vin_info.stats.frame_complete,
             (unsigned long)vin_info.stats.error_event);
    print_to_console(buf);

    if (vin_info.stats.fifo_overflow > 0) {
        snprintf(buf, sizeof(buf), "  FIFO overflow: %lu\r\n",
                 (unsigned long)vin_info.stats.fifo_overflow);
        print_to_console(buf);
    }
}
