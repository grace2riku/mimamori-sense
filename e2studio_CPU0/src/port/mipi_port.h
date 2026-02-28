/**
 * @file mipi_port.h
 * @brief MIPI D-PHY port layer for EK-RA8P1 camera interface (S-003-1)
 * @details
 * Provides MIPI D-PHY (physical layer) initialization, status monitoring,
 * and diagnostic functions for the OV5640 camera module connected via
 * the MIPI CSI-2 interface on the EK-RA8P1 Camera Expansion Board.
 *
 * MIPI D-PHY Configuration Summary:
 *   Data Lanes       : 2 (MIPI CSI-2 2-lane)
 *   HS Clock         : 185 MHz (OV5640 default)
 *   PLL Frequency    : 1000 MHz (24 MHz / 3 * 125)
 *   LP Divisor       : 5
 *   Mode             : CSI (not DSI)
 *
 * D-PHY Timing Parameters (from FSP configuration):
 *   T_INIT           : 74999 PCLKA cycles (~750 us at 100 MHz PCLKA)
 *   T_CLK_SETTLE     : 62 PCLKA cycles
 *   T_CLK_MISS       : 37 PCLKA cycles
 *   T_HS_SETTLE      : 24 PCLKA cycles
 *
 * Power-on Sequence (per MIPI D-PHY spec):
 *   1. Camera power-down exit (GPIO)
 *   2. Camera hardware reset (GPIO)
 *   3. Camera software reset (I2C)
 *   4. OV5640 register initialization (I2C)
 *   5. R_MIPI_PHY_Open() - PHY PLL lock and timing configuration
 *   6. R_MIPI_CSI2_Open() - CSI-2 link initialization (S-003-2)
 *   7. R_VIN_Open() - Video input capture initialization (S-003-3)
 *
 * Dependencies:
 *   - FSP MIPI PHY driver (r_mipi_phy) must be added via configuration.xml
 *   - FSP-generated instances: g_mipi_phy0, g_mipi_phy0_ctrl, g_mipi_phy0_cfg
 *   - PCLKA clock must be running (BSP initialization)
 *
 * Reference:
 *   - Reference project: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c
 *   - MIPI PHY API: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra/fsp/inc/instances/r_mipi_phy.h
 *   - FSP config: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:3-48
 *   - OV5640 config: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640_cfg.h
 *
 * @note
 * This file is part of the MIPI PHY initialization (S-003-1) implementation.
 */

#ifndef MIPI_PORT_H
#define MIPI_PORT_H

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
 * MIPI D-PHY lane configuration
 *
 * OV5640 camera module supports 1-lane or 2-lane MIPI CSI-2.
 * The EK-RA8P1 Camera Expansion Board uses 2-lane configuration.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:67
 */
#define MIPI_PHY_NUM_LANES              (2)

/**
 * OV5640 HS clock frequency configuration
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:146
 */
#define MIPI_PHY_HSCLK_MHZ             (185)
#define MIPI_PHY_HSCLK_HZ              (MIPI_PHY_HSCLK_MHZ * 1000000UL)

/**
 * MIPI PHY PLL configuration values (from FSP-generated code)
 *
 * PLL frequency = (XTAL / div) * (mul_int + mul_frac) / pll_div
 *               = (24 MHz / 3) * 125 / 1
 *               = 1000 MHz
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:43-44
 */
#define MIPI_PHY_PLL_DIV                (3)     /* Input divisor */
#define MIPI_PHY_PLL_MUL_INT           (125)   /* Integer multiplier */
#define MIPI_PHY_PLL_MUL_FRAC          (0)     /* Fractional multiplier */
#define MIPI_PHY_PLL_OUTPUT_DIV        (0)     /* Output divisor (0 = /1) */
#define MIPI_PHY_PLL_FREQ_HZ           (1000000000UL)

/**
 * MIPI PHY LP speed divisor
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:45
 */
#define MIPI_PHY_LP_DIVISOR            (5)

/**
 * Camera external clock frequency (XCLK provided by GPT timer)
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:68
 */
#define MIPI_PHY_XCLK_HZ              (24000000UL)

/**
 * D-PHY timing parameter macros (in PCLKA cycles)
 *
 * These values are programmed into the MIPI PHY timing registers.
 * They are derived from the MIPI D-PHY specification timing requirements
 * and the OV5640 HS clock frequency.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.c:9-22
 */
#define MIPI_PHY_TINIT                  (74999)     /* T_INIT duration (PCLKA cycles) */
#define MIPI_PHY_TCLKPREP              (9)          /* Clock lane LP-00 duration */
#define MIPI_PHY_TCLKSETT              (62)         /* Clock lane settle period */
#define MIPI_PHY_TCLKMISS              (37)         /* Clock absence detection period */
#define MIPI_PHY_THSPREP               (6)          /* Data lane LP-00 duration */
#define MIPI_PHY_THSETT                (24)         /* Data lane HS settle period */
#define MIPI_PHY_TCLKTRAIL             (7)          /* Clock trail period */
#define MIPI_PHY_TCLKPOST              (20)         /* Clock post period */
#define MIPI_PHY_TCLKPRE               (1)          /* Clock pre period */
#define MIPI_PHY_TCLKZERO              (28)         /* Clock zero period */
#define MIPI_PHY_THSEXIT               (12)         /* HS exit period */
#define MIPI_PHY_THSTRAIL              (8)          /* HS trail period */
#define MIPI_PHY_THSZERO               (19)         /* HS zero period */
#define MIPI_PHY_TLPEXIT               (7)          /* LP exit period */

/**
 * OV5640 camera module I2C slave address
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640_cfg.h:27
 */
#define MIPI_OV5640_I2C_ADDR           (0x78 >> 1)

/**
 * OV5640 camera module GPIO pins
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640_cfg.h:28-29
 */
#define MIPI_OV5640_CAM_PWR_ON          (BSP_IO_PORT_07_PIN_04)
#define MIPI_OV5640_CAM_RESET           (BSP_IO_PORT_07_PIN_09)

/**
 * OV5640 Product ID registers
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:40-41
 */
#define MIPI_OV5640_REG_PIDH            (0x300A)
#define MIPI_OV5640_REG_PIDL            (0x300B)
#define MIPI_OV5640_PID_EXPECTED        (0x5640)

/**********************************************************************************************************************
 Global Typedef definitions
 *********************************************************************************************************************/

/** MIPI PHY initialization status */
typedef enum {
    MIPI_PHY_STATUS_NOT_INITIALIZED = 0,    /**< Not yet initialized */
    MIPI_PHY_STATUS_INITIALIZED,            /**< Successfully initialized */
    MIPI_PHY_STATUS_ERROR,                  /**< Initialization failed */
    MIPI_PHY_STATUS_NO_FSP_MODULE,          /**< FSP module not yet configured */
} mipi_phy_status_t;

/**
 * MIPI PHY information structure
 *
 * Contains the current state and configuration of the MIPI D-PHY subsystem.
 */
typedef struct {
    mipi_phy_status_t status;           /**< Current initialization status */
    uint32_t    num_lanes;              /**< Number of data lanes configured */
    uint32_t    hsclk_mhz;             /**< HS clock frequency in MHz */
    uint32_t    pll_freq_hz;            /**< PHY PLL frequency in Hz */
    uint32_t    lp_divisor;             /**< LP speed divisor */
    uint32_t    pclka_hz;               /**< PCLKA clock frequency in Hz */
    bool        phy_open;               /**< true if R_MIPI_PHY_Open() succeeded */
} mipi_phy_info_t;

/**
 * MIPI PHY timing information structure
 *
 * Contains the D-PHY timing parameters for diagnostic display.
 */
typedef struct {
    uint32_t t_init;                    /**< T_INIT duration (PCLKA cycles) */
    uint32_t t_clk_prep;               /**< Clock lane LP-00 duration */
    uint32_t t_clk_settle;             /**< Clock lane settle period */
    uint32_t t_clk_miss;               /**< Clock absence detection period */
    uint32_t t_hs_prep;                /**< Data lane LP-00 duration */
    uint32_t t_hs_settle;              /**< Data lane HS settle period */
    uint32_t t_clk_zero;               /**< Clock zero period */
    uint32_t t_clk_pre;                /**< Clock pre period */
    uint32_t t_clk_post;               /**< Clock post period */
    uint32_t t_clk_trail;              /**< Clock trail period */
    uint32_t t_hs_zero;                /**< HS zero period */
    uint32_t t_hs_trail;               /**< HS trail period */
    uint32_t t_hs_exit;                /**< HS exit period */
    uint32_t t_lp_exit;                /**< LP exit period */
} mipi_phy_timing_info_t;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the MIPI D-PHY subsystem (S-003-1)
 *
 * @details Performs the MIPI PHY initialization sequence:
 *   1. Verify FSP MIPI PHY module is available (g_mipi_phy0)
 *   2. Compute PCLKA clock frequency for timing calculations
 *   3. Call R_MIPI_PHY_Open() with the FSP-generated configuration
 *   4. Verify PHY PLL lock
 *
 * This function must be called before MIPI CSI-2 initialization (S-003-2)
 * and VIN capture initialization (S-003-3).
 *
 * Preconditions:
 *   - BSP initialization complete (clocks running)
 *   - SDRAM initialized (for capture buffers)
 *   - FSP MIPI PHY module configured in configuration.xml
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:309-377
 *
 * @retval true   MIPI PHY initialized successfully
 * @retval false  MIPI PHY initialization failed
 */
bool mipi_phy_port_init(void);

/**
 * Close the MIPI D-PHY subsystem
 *
 * @details Calls R_MIPI_PHY_Close() to shut down the PHY and release resources.
 *
 * @retval true   MIPI PHY closed successfully
 * @retval false  MIPI PHY close failed
 */
bool mipi_phy_port_close(void);

/**
 * Get the current MIPI PHY initialization status
 *
 * @return Current MIPI PHY status
 */
mipi_phy_status_t mipi_phy_port_get_status(void);

/**
 * Check if MIPI PHY is available for use
 *
 * @retval true   MIPI PHY is initialized and ready
 * @retval false  MIPI PHY is not available
 */
bool mipi_phy_port_is_available(void);

/**
 * Get MIPI PHY configuration and status information
 *
 * @details Fills the info structure with the current PHY configuration
 *          including lane count, clock frequencies, PLL settings,
 *          and initialization status.
 *
 * @param info Pointer to mipi_phy_info_t structure to fill
 */
void mipi_phy_port_get_info(mipi_phy_info_t *info);

/**
 * Get MIPI PHY D-PHY timing parameters
 *
 * @details Fills the timing info structure with the configured
 *          D-PHY timing parameters (in PCLKA cycles).
 *
 * @param timing Pointer to mipi_phy_timing_info_t structure to fill
 */
void mipi_phy_port_get_timing_info(mipi_phy_timing_info_t *timing);

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
 *   camera thread    - Show camera thread init status (F-002-5)
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
int usrcmd_camera(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* MIPI_PORT_H */
