/**
 * @file ov5640_cfg.h
 * @brief OV5640 camera sensor configuration definitions
 * @details
 * GPIO pin definitions, I2C address, and sensor configuration constants
 * for the OV5640 camera module on the EK-RA8P1 Camera Expansion Board.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640_cfg.h
 *
 * Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef OV5640_CFG_H
#define OV5640_CFG_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include "hal_data.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * OV5640 I2C slave address (7-bit)
 * Datasheet: 0x78 write / 0x79 read -> 7-bit = 0x3C
 * Matches FSP configuration: g_i2c_master_camera_cfg.slave = 0x3C
 */
#define OV5640_I2C_ADDR             (0x3C)

/**
 * GPIO pin assignments for OV5640 camera module
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640_cfg.h:28-29
 */
#define OV5640_CAM_PWR_ON           (BSP_IO_PORT_07_PIN_04)   /* P704: Power-down control (active HIGH) */
#define OV5640_CAM_RESET            (BSP_IO_PORT_07_PIN_09)   /* P709: Hardware reset (active LOW)      */

/**
 * OV5640 chip ID register addresses
 */
#define OV5640_REG_CHIP_ID_H        (0x300A)    /* Chip ID high byte (expect 0x56) */
#define OV5640_REG_CHIP_ID_L        (0x300B)    /* Chip ID low byte (expect 0x40/0x41/0x4C) */

/**
 * OV5640 system control registers
 */
#define OV5640_REG_SYS_CTRL         (0x3008)    /* System control (reset, power-down) */
#define OV5640_REG_SCCB_SYS_CTRL    (0x3103)    /* SCCB system control */
#define OV5640_REG_STREAM_CTRL      (0x4202)    /* Stream ON/OFF control */

/**
 * OV5640 PLL registers
 */
#define OV5640_REG_PLL_CTRL0        (0x3034)    /* PLL control 0 (MIPI bit mode) */
#define OV5640_REG_PLL_CTRL1        (0x3035)    /* PLL control 1 (sys/MIPI divider) */
#define OV5640_REG_PLL_CTRL2        (0x3036)    /* PLL control 2 (multiplier) */
#define OV5640_REG_PLL_CTRL3        (0x3037)    /* PLL control 3 (root/pre divider) */
#define OV5640_REG_SYS_ROOT_DIV     (0x3108)    /* System root divider */

/**
 * MIPI configuration
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:67
 */
#define OV5640_MIPI_NUM_LANES       (2)         /* Number of MIPI CSI-2 data lanes */
#define OV5640_XCLK_HZ             (24000000)   /* External clock frequency: 24 MHz from GPT */

/**
 * Image format configuration
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:96-100
 */
#define OV5640_INPUT_FORMAT_YUV422_8BIT  (1)    /* YUV422 8-bit mode */
#define OV5640_INPUT_FORMAT_RAW8         (0)    /* RAW8 mode (not used) */
#define OV5640_INPUT_FORMAT_RGB888       (0)    /* RGB888 mode (not used) */

/**
 * Image orientation
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:64-65
 */
#define OV5640_FLIP_IMAGE           (0)         /* 0: No vertical flip */
#define OV5640_MIRROR_IMAGE         (0)         /* 0: No horizontal mirror */

/**
 * Test image configuration
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:85-90
 */
#define OV5640_TEST_IMAGE_ENABLE        (0)     /* 0: Disabled */
#define OV5640_TEST_IMAGE_ROLLING       (0)     /* 0: Static */
#define OV5640_TEST_IMAGE_TRANSPARENT   (0)     /* 0: Opaque */
#define OV5640_TEST_IMAGE_SQUARE        (0)     /* 0: No square overlay */
#define OV5640_TEST_IMAGE_BAR_STYLE     (1)     /* 1: Gradual Vertical Mode 1 */
#define OV5640_TEST_IMAGE_TYPE          (0)     /* 0: Color Bar */

/**
 * MIPI clock configuration
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:79
 */
#define OV5640_CONTINUOUS_CLOCK     (1)         /* 1: MIPI continuous clock enabled */

/**
 * MIPI HS clock and timing
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:146-169
 */
#define OV5640_HSCLK_MHZ           (185)        /* HS clock in MHz */
#define OV5640_MIPI_GLOBAL_TIMING  (0x0A)       /* Global timing value for HS clock >= 70 MHz */

/**
 * Default image resolution
 * Uses display resolution from FSP configuration (GLCDC Layer 2 or VIN settings)
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:62-63
 * Note: Actual values come from VIN/GLCDC FSP configuration at runtime
 */
#define OV5640_DEFAULT_IMAGE_WIDTH  (768)       /* Default: VIN_CFG_IMAGE_STRIDE from common_data.h */
#define OV5640_DEFAULT_IMAGE_HEIGHT (450)       /* Default: from VIN buffer size calculation */

/**
 * PLL clock settings (pre-computed for 24 MHz XCLK)
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:252-266
 *
 * Clock tree:
 *   XCLK (24 MHz) -> pre_div -> PLL multiplier -> root_div -> MIPI bit div -> dividers
 *   Target: MIPI clock ~185 MHz (OV5640_HSCLK_MHZ)
 *
 * Computed settings:
 *   sys_clock_div  = 1, mipi_clock_div = 2
 *   pll_multiplier = 246
 *   pll_root_div   = 1 (bypass=0) or 2 (divide)
 *   pll_pre_div    = 8
 *   pclk_root_div  = 2, sclk2x_root_div = 1, sclk_root_div = 4
 */
#define OV5640_PLL_SYS_CLK_DIV     (1)
#define OV5640_PLL_MIPI_CLK_DIV    (2)
#define OV5640_PLL_MULTIPLIER      (246)
#define OV5640_PLL_ROOT_DIV        (1)         /* 1: bypass, 2: divide by 2 */
#define OV5640_PLL_PRE_DIV         (8)
#define OV5640_PCLK_ROOT_DIV       (2)
#define OV5640_SCLK2X_ROOT_DIV     (1)
#define OV5640_SCLK_ROOT_DIV       (4)

#endif /* OV5640_CFG_H */
