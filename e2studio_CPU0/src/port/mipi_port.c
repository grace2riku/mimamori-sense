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

#include "mipi_port.h"
#include "csi2_port.h"
#include "vin_port.h"
#include "bsp_api.h"
#include "jlink_console.h"
#include "cmd_utils.h"
#include "ntlibc.h"

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
#define MIPI_PORT_FSP_AVAILABLE     (0)
#endif

#if MIPI_PORT_FSP_AVAILABLE
#include "r_mipi_phy.h"
#include "common_data.h"
#endif

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size */
#define MIPI_PRINT_BUF_SIZE         (128)

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
static void mipi_cmd_timing(void);
static void mipi_cmd_init(void);

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
        snprintf(buf, sizeof(buf), "  PCLKA period : %.2f ns (%lu MHz)\r\n",
                 1000000000.0f / (float)s_pclka_hz,
                 (unsigned long)(s_pclka_hz / 1000000UL));
        print_to_console(buf);
    }

    print_to_console("  Source: FSP configuration (configuration.xml)\r\n");
    print_to_console("  Ref: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:9-22\r\n");
}

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
    print_to_console("  timing   - Show D-PHY timing parameters (S-003-1)\r\n");
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
        mipi_cmd_timing();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "init") == 0) {
        mipi_cmd_init();
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

    /* Unknown sub-command */
    {
        char buf[MIPI_PRINT_BUF_SIZE];
        snprintf(buf, sizeof(buf), "Error: Unknown sub-command '%s'.\r\n", argv[1]);
        print_to_console(buf);
    }

    mipi_cmd_print_help();

    return CMD_ERR_INVALID_ARG;
}
