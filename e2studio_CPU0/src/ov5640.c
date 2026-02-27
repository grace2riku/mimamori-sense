/**
 * @file ov5640.c
 * @brief OV5640 camera sensor I2C register control driver
 * @details
 * Implementation of the OV5640 camera sensor driver for the EK-RA8P1.
 * Provides I2C register access (16-bit address, 8-bit data), power/reset
 * sequences, full sensor initialization with 100+ register settings,
 * PLL clock configuration, and MIPI CSI-2 stream control.
 *
 * FSP Instance Mapping (current project):
 *   I2C Master:  g_i2c_master_camera_ctrl (IIC1, 7-bit addr 0x3C)
 *   GPT Timer:   g_timer_camera_xclk_ctrl (GPT12, 24 MHz XCLK output)
 *   I2C Event:   g_i2c_event_group (shared FreeRTOS event group)
 *   Callback:    i2c_camera_callback (FSP-generated declaration)
 *
 * Reference (ov5640.c):
 *   reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640.c
 * Reference (init registers):
 *   reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:456-896
 * Reference (clock config):
 *   reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:1056-1227
 *
 * Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <string.h>

#include "bsp_api.h"
#include "hal_data.h"
#include "FreeRTOS.h"
#include "event_groups.h"

#include "ov5640.h"
#include "ov5640_cfg.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * GPIO control macros for OV5640 reset and power-down pins
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640.c:37-48
 */

/* OV5640 Reset pin control (active LOW) */
#define OV5640_RST_PIN_SET(x) do { \
    R_BSP_PinAccessEnable();       \
    R_BSP_PinWrite(OV5640_CAM_RESET, (x)); \
    R_BSP_PinAccessDisable();      \
} while(0)

/* OV5640 Power-down pin control (active HIGH) */
#define OV5640_PWDN_PIN_SET(x) do { \
    R_BSP_PinAccessEnable();        \
    R_BSP_PinWrite(OV5640_CAM_PWR_ON, (x)); \
    R_BSP_PinAccessDisable();       \
} while(0)

/* Millisecond delay using BSP software delay */
#define delay_ms(x)  (R_BSP_SoftwareDelay((x), BSP_DELAY_UNITS_MILLISECONDS))

/* I2C transfer event bits for FreeRTOS event group */
#define I2C_TRANSFER_COMPLETE   (1 << 0)
#define I2C_TRANSFER_ABORT      (1 << 1)
#define I2C_TIMEOUT_MS          (1000 / portTICK_PERIOD_MS)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/* Driver status tracking */
static ov5640_status_t s_ov5640_status = {
    .initialized      = false,
    .streaming         = false,
    .chip_id_verified  = false,
    .chip_id           = 0x0000,
    .init_error_count  = 0,
};

/**
 * PLL clock divider settings (pre-computed for 24 MHz XCLK, target ~185 MHz MIPI clock)
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:252-266
 */
static uint8_t s_sys_clock_div   = OV5640_PLL_SYS_CLK_DIV;
static uint8_t s_mipi_clock_div  = OV5640_PLL_MIPI_CLK_DIV;
static uint8_t s_pll_multiplier  = OV5640_PLL_MULTIPLIER;
static uint8_t s_pll_root_div    = OV5640_PLL_ROOT_DIV;
static uint8_t s_pll_pre_div     = OV5640_PLL_PRE_DIV;
static uint8_t s_pclk_root_div   = OV5640_PCLK_ROOT_DIV;
static uint8_t s_sclk2x_root_div = OV5640_SCLK2X_ROOT_DIV;
static uint8_t s_sclk_root_div   = OV5640_SCLK_ROOT_DIV;

/**********************************************************************************************************************
 Private (static) function prototypes
 *********************************************************************************************************************/

static fsp_err_t ov5640_i2c_wait_complete(void);
static fsp_err_t ov5640_i2c_write_reg16_8(uint16_t reg, uint8_t data);
static fsp_err_t ov5640_i2c_read_reg16_8(uint16_t reg, uint8_t *data);
static void ov5640_init_base_registers(void);
static void ov5640_init_awb_registers(void);
static void ov5640_init_color_matrix(void);
static void ov5640_init_cip_registers(void);
static void ov5640_init_gamma_registers(void);
static void ov5640_init_uv_adjust(void);
static void ov5640_init_lens_correction(void);
static void ov5640_init_aec_registers(void);
static void ov5640_init_timing_registers(void);
static void ov5640_init_output_format(void);

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * I2C callback for camera I2C master (IIC1)
 * Called from ISR context when I2C transfer completes or aborts.
 * Sets event group bits to signal waiting task.
 *
 * Note: This callback name (i2c_camera_callback) is defined in FSP configuration
 * and declared in ra_gen/hal_data.h. Must be implemented in user code.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/board_i2c_master.c:219-252
 */
void i2c_camera_callback(i2c_master_callback_args_t *p_args)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t xResult = pdFAIL;

    if ((I2C_MASTER_EVENT_TX_COMPLETE == p_args->event) ||
        (I2C_MASTER_EVENT_RX_COMPLETE == p_args->event))
    {
        xResult = xEventGroupSetBitsFromISR(g_i2c_event_group,
                                            I2C_TRANSFER_COMPLETE,
                                            &xHigherPriorityTaskWoken);
    }
    else if (I2C_MASTER_EVENT_ABORTED == p_args->event)
    {
        xResult = xEventGroupSetBitsFromISR(g_i2c_event_group,
                                            I2C_TRANSFER_ABORT,
                                            &xHigherPriorityTaskWoken);
    }
    else
    {
        /* Unexpected event - do nothing */
        ;
    }

    if (pdFAIL != xResult)
    {
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    R_BSP_IrqStatusClear(R_FSP_CurrentIrqGet());
}

/**********************************************************************************************************************
 * Function Name: ov5640_write_reg
 * Description  : Write a single register on the OV5640 (16-bit address, 8-bit data)
 * Arguments    : reg  - 16-bit register address
 *              : data - 8-bit data to write
 * Return Value : FSP_SUCCESS on success, error code on failure
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640.c:66-81
 *********************************************************************************************************************/
fsp_err_t ov5640_write_reg(uint16_t reg, uint8_t data)
{
    fsp_err_t err;

    err = ov5640_i2c_write_reg16_8(reg, data);
    if (FSP_SUCCESS != err)
    {
        s_ov5640_status.init_error_count++;
    }

    return err;
}

/**********************************************************************************************************************
 * Function Name: ov5640_read_reg
 * Description  : Read a single register from the OV5640 (16-bit address, 8-bit data)
 * Arguments    : reg - 16-bit register address
 * Return Value : 8-bit register value, or 0xFF on error
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640.c:92-99
 *********************************************************************************************************************/
uint8_t ov5640_read_reg(uint16_t reg)
{
    uint8_t data = 0xFF;

    ov5640_i2c_read_reg16_8(reg, &data);

    return data;
}

/**********************************************************************************************************************
 * Function Name: ov5640_write_reg_verified
 * Description  : Write a register with read-back verification
 * Arguments    : reg  - 16-bit register address
 *              : data - 8-bit data to write
 * Return Value : FSP_SUCCESS if write and verify succeeded
 *              : FSP_ERR_WRITE_FAILED if I2C write failed
 *              : FSP_ERR_ASSERTION if read-back mismatch
 *********************************************************************************************************************/
fsp_err_t ov5640_write_reg_verified(uint16_t reg, uint8_t data)
{
    fsp_err_t err;
    uint8_t read_back;

    err = ov5640_write_reg(reg, data);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    read_back = ov5640_read_reg(reg);
    if (read_back != data)
    {
        s_ov5640_status.init_error_count++;
        return FSP_ERR_ASSERTION;
    }

    return FSP_SUCCESS;
}

/**********************************************************************************************************************
 * Function Name: ov5640_hw_init
 * Description  : Set initial GPIO state: RESET=LOW (assert reset), PWDN=HIGH (power down)
 * Arguments    : None
 * Return Value : None
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640.c:110-119
 *********************************************************************************************************************/
void ov5640_hw_init(void)
{
    /* Reset pin is Active LOW -> assert reset */
    OV5640_RST_PIN_SET(BSP_IO_LEVEL_LOW);

    /* Power-down pin is Active HIGH -> enter power-down */
    OV5640_PWDN_PIN_SET(BSP_IO_LEVEL_HIGH);
}

/**********************************************************************************************************************
 * Function Name: ov5640_exit_power_down
 * Description  : Exit the power-down state with correct timing sequence
 *              : Sequence: RESET=LOW -> 20ms -> PWDN=LOW -> 50ms -> RESET=HIGH -> 20ms
 * Arguments    : None
 * Return Value : None
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640.c:130-143
 *********************************************************************************************************************/
void ov5640_exit_power_down(void)
{
    /* Ensure reset is asserted */
    OV5640_RST_PIN_SET(BSP_IO_LEVEL_LOW);
    delay_ms(20);

    /* Exit power-down: PWDN=LOW */
    OV5640_PWDN_PIN_SET(BSP_IO_LEVEL_LOW);
    delay_ms(50);

    /* Release reset: RESET=HIGH */
    OV5640_RST_PIN_SET(BSP_IO_LEVEL_HIGH);
    delay_ms(20);
}

/**********************************************************************************************************************
 * Function Name: ov5640_hw_reset
 * Description  : Toggle the hardware reset pin with proper timing
 *              : Sequence: RESET=LOW -> 20ms -> RESET=HIGH -> 20ms
 * Arguments    : None
 * Return Value : None
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640.c:154-163
 *********************************************************************************************************************/
void ov5640_hw_reset(void)
{
    /* Assert reset */
    OV5640_RST_PIN_SET(BSP_IO_LEVEL_LOW);
    delay_ms(20);

    /* Release reset */
    OV5640_RST_PIN_SET(BSP_IO_LEVEL_HIGH);
    delay_ms(20);
}

/**********************************************************************************************************************
 * Function Name: ov5640_sw_reset
 * Description  : Issue software reset via I2C and wait for completion
 *              : Clears SCCB system control bit, then writes 0x82 to system control register
 * Arguments    : None
 * Return Value : None
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640.c:180-189
 *********************************************************************************************************************/
void ov5640_sw_reset(void)
{
    uint8_t reg3103;

    /* Clear SCCB system control bit[1] */
    reg3103 = ov5640_read_reg(OV5640_REG_SCCB_SYS_CTRL);
    reg3103 &= (uint8_t)~(0x01U << 1);
    ov5640_write_reg(OV5640_REG_SCCB_SYS_CTRL, reg3103);

    /* Issue software reset: bit[7] = 1 */
    ov5640_write_reg(OV5640_REG_SYS_CTRL, 0x82);

    /* Wait for reset to complete */
    delay_ms(300);
}

/**********************************************************************************************************************
 * Function Name: ov5640_verify_chip_id
 * Description  : Read and verify the OV5640 chip ID
 *              : Reads registers 0x300A (high byte) and 0x300B (low byte)
 *              : Valid chip IDs: 0x5640 (rev1a), 0x5641 (rev2a), 0x564C (rev2c)
 * Arguments    : None
 * Return Value : FSP_SUCCESS if valid chip ID detected
 *              : FSP_ERR_ASSERTION if invalid chip ID
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:478-499
 *********************************************************************************************************************/
fsp_err_t ov5640_verify_chip_id(void)
{
    uint8_t id_h;
    uint8_t id_l;

    /* Read chip ID high byte (expect 0x56) */
    id_h = ov5640_read_reg(OV5640_REG_CHIP_ID_H);

    /* Read chip ID low byte (expect 0x40, 0x41, or 0x4C) */
    id_l = ov5640_read_reg(OV5640_REG_CHIP_ID_L);

    s_ov5640_status.chip_id = (uint16_t)((uint16_t)id_h << 8) | (uint16_t)id_l;

    /* Verify high byte */
    if (id_h != OV5640_CHIP_ID_H_EXPECTED)
    {
        s_ov5640_status.chip_id_verified = false;
        return FSP_ERR_ASSERTION;
    }

    /* Verify low byte matches one of the known revisions */
    if ((id_l == OV5640_CHIP_ID_L_REV1A) ||
        (id_l == OV5640_CHIP_ID_L_REV2A) ||
        (id_l == OV5640_CHIP_ID_L_REV2C))
    {
        s_ov5640_status.chip_id_verified = true;
        return FSP_SUCCESS;
    }

    s_ov5640_status.chip_id_verified = false;
    return FSP_ERR_ASSERTION;
}

/**********************************************************************************************************************
 * Function Name: ov5640_stream_on
 * Description  : Start camera MIPI CSI-2 stream output
 * Arguments    : None
 * Return Value : None
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:426-429
 *********************************************************************************************************************/
void ov5640_stream_on(void)
{
    ov5640_write_reg(OV5640_REG_STREAM_CTRL, 0x00);
    s_ov5640_status.streaming = true;
}

/**********************************************************************************************************************
 * Function Name: ov5640_stream_off
 * Description  : Stop camera MIPI CSI-2 stream output
 * Arguments    : None
 * Return Value : None
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:440-443
 *********************************************************************************************************************/
void ov5640_stream_off(void)
{
    ov5640_write_reg(OV5640_REG_STREAM_CTRL, 0x0F);
    s_ov5640_status.streaming = false;
}

/**********************************************************************************************************************
 * Function Name: ov5640_set_mipi_virtual_channel
 * Description  : Set MIPI virtual channel number
 * Arguments    : vchannel - Virtual channel number (0-3)
 * Return Value : None
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:405-415
 *********************************************************************************************************************/
void ov5640_set_mipi_virtual_channel(uint32_t vchannel)
{
    uint8_t tmp;

    tmp = ov5640_read_reg(0x4814);
    tmp = tmp & (uint8_t)~(3U << 6);
    tmp = tmp | (uint8_t)(vchannel << 6);
    ov5640_write_reg(0x4814, tmp);
}

/**********************************************************************************************************************
 * Function Name: ov5640_configure_clocks
 * Description  : Configure PLL clock dividers for target MIPI clock frequency
 *              : Uses pre-computed settings (not runtime calculation) for 24 MHz XCLK
 *              : Clock tree:
 *              :   XCLK -> pre_div -> PLL multiplier -> root_div -> bit_div -> dividers
 * Arguments    : None
 * Return Value : Calculated MIPI clock frequency in Hz
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:1056-1227
 *********************************************************************************************************************/
uint32_t ov5640_configure_clocks(void)
{
    uint32_t base_pll_hz;
    uint32_t mipi_clk_hz;

    /* Calculate PLL base frequency */
    base_pll_hz = (uint32_t)(s_pll_multiplier * (OV5640_XCLK_HZ / (s_pll_root_div * s_pll_pre_div)));

    /* Calculate MIPI clock */
    mipi_clk_hz = base_pll_hz / (uint32_t)(s_mipi_clock_div * s_pclk_root_div);

    /*
     * 0x3035 SC PLL CTRL1 (Default: 0x11)
     * Bit[7:4]: System clock divider
     * Bit[3:0]: MIPI clock divider
     */
    uint8_t reg_3035 = (uint8_t)((s_sys_clock_div << 4) | (s_mipi_clock_div << 0));
    ov5640_write_reg(OV5640_REG_PLL_CTRL1, reg_3035);

    /*
     * 0x3036 SC PLL CTRL2 (Default: 0x38)
     * Bit[7:0]: PLL multiplier (4~252, even integer from 128+)
     */
    ov5640_write_reg(OV5640_REG_PLL_CTRL2, s_pll_multiplier);

    /*
     * 0x3037 SC PLL CTRL3 (Default: 0x13)
     * Bit[4]: PLL root divider (0: bypass, 1: divide by 2)
     * Bit[3:0]: PLL pre-divider
     */
    uint8_t reg_3037 = (uint8_t)(((s_pll_root_div == 2) ? 1U : 0U) << 4) | (s_pll_pre_div & 0x0F);
    ov5640_write_reg(OV5640_REG_PLL_CTRL3, reg_3037);

    /*
     * 0x3108 System Root Divider (Default: 0x01)
     * Bit[5:4]: PCLK root divider  (00:1, 01:2, 10:4, 11:8)
     * Bit[3:2]: sclk2x root divider
     * Bit[1:0]: SCLK root divider
     */
    uint8_t reg_3108_div_lut[] = {0x0F, 0, 1, 0x0F, 2, 0x0F, 0x0F, 0x0F, 3};
    uint8_t reg_3108 = (uint8_t)((reg_3108_div_lut[s_pclk_root_div] << 4) |
                                  (reg_3108_div_lut[s_sclk2x_root_div] << 2) |
                                  (reg_3108_div_lut[s_sclk_root_div] << 0));
    ov5640_write_reg(OV5640_REG_SYS_ROOT_DIV, reg_3108);

    return mipi_clk_hz;
}

/**********************************************************************************************************************
 * Function Name: ov5640_init
 * Description  : Full OV5640 sensor initialization
 *              : Performs GPT timer start, power-up sequence, chip ID check, and
 *              : 100+ register writes for PLL, timing, ISP, AWB, gamma, etc.
 * Arguments    : None
 * Return Value : FSP_SUCCESS on success, FSP_ERR_ASSERTION if chip ID fails
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:456-896
 *********************************************************************************************************************/
fsp_err_t ov5640_init(void)
{
    fsp_err_t err;
    uint8_t reg_val;

    /* Reset error counter */
    s_ov5640_status.init_error_count = 0;
    s_ov5640_status.initialized = false;
    s_ov5640_status.streaming = false;

    /*
     * Step 1: Open and start GPT timer for 24 MHz XCLK output
     * Reference: camera_thread_entry.c:461-464
     */
    R_GPT_Open(&g_timer_camera_xclk_ctrl, &g_timer_camera_xclk_cfg);
    R_GPT_Start(&g_timer_camera_xclk_ctrl);

    /*
     * Step 2: Open I2C master for camera communication
     */
    if (0 == g_i2c_master_camera_ctrl.open)
    {
        R_IIC_MASTER_Open(&g_i2c_master_camera_ctrl, &g_i2c_master_camera_cfg);
    }

    /*
     * Step 3: Enter power-down and reset state, then exit
     * Reference: camera_thread_entry.c:466-476
     */
    ov5640_hw_init();
    delay_ms(300);

    ov5640_exit_power_down();

    ov5640_hw_reset();
    ov5640_sw_reset();
    delay_ms(20);  /* Register access permitted after 20 ms */

    /*
     * Step 4: Verify chip ID
     * Reference: camera_thread_entry.c:478-499
     */
    /* First read may return stale data - read high byte first */
    reg_val = ov5640_read_reg(OV5640_REG_CHIP_ID_H);
    (void)reg_val;

    err = ov5640_verify_chip_id();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /*
     * Step 5: Second software reset before register programming
     * Reference: camera_thread_entry.c:501-512
     */
    {
        uint8_t reg3103 = ov5640_read_reg(OV5640_REG_SCCB_SYS_CTRL);
        reg3103 = reg3103 & (uint8_t)~(0x01 << 1);
        ov5640_write_reg(OV5640_REG_SCCB_SYS_CTRL, reg3103);

        uint8_t reg3008 = ov5640_read_reg(OV5640_REG_SYS_CTRL);
        reg3008 |= (0x01 << 7);  /* Software reset */
        ov5640_write_reg(OV5640_REG_SYS_CTRL, reg3008);
        delay_ms(300);
    }

    /*
     * Step 6: OV5640 register initialization sequence
     * Based on OmniVision settings v A09 (dated 05/19/2011)
     * Reference: camera_thread_entry.c:532-896
     */

    /* SCCB system control and initial software reset */
    ov5640_write_reg(0x3103, 0x11);     /* SCCB system control */
    ov5640_write_reg(0x3008, 0x82);     /* Software reset */
    delay_ms(5);
    ov5640_write_reg(0x3008, 0x42);     /* Software power down */
    ov5640_write_reg(0x3103, 0x03);     /* SCCB system control */

    /* I/O direction: set all as input */
    ov5640_write_reg(0x3017, 0x00);     /* Frex, Vsync, Href, PCLK, D[9:6] input */
    ov5640_write_reg(0x3018, 0x00);     /* D[5:0], GPIO[1:0] input */

    /* Initial PLL settings (overridden later by ov5640_configure_clocks) */
    ov5640_write_reg(0x3034, 0x18);     /* MIPI 8-bit mode */
    ov5640_write_reg(0x3037, 0x13);     /* PLL initial */
    ov5640_write_reg(0x3108, 0x01);     /* System divider */

    /* Analog settings */
    ov5640_init_base_registers();

    /* 50Hz/60Hz detection */
    ov5640_write_reg(0x3c01, 0x34);     /* 50/60Hz detection */
    ov5640_write_reg(0x3c04, 0x28);     /* Threshold for low sum */
    ov5640_write_reg(0x3c05, 0x98);     /* Threshold for high sum */
    ov5640_write_reg(0x3c06, 0x00);     /* Light meter 1 threshold high */
    ov5640_write_reg(0x3c08, 0x00);     /* Light meter 2 threshold high */
    ov5640_write_reg(0x3c09, 0x1c);     /* Light meter 2 threshold low */
    ov5640_write_reg(0x3c0a, 0x9c);     /* Sample number high */
    ov5640_write_reg(0x3c0b, 0x40);     /* Sample number low */

    /* Initial timing (full resolution crop window) */
    ov5640_write_reg(0x3800, 0x00);     /* HS start high */
    ov5640_write_reg(0x3801, 0x00);     /* HS start low */
    ov5640_write_reg(0x3802, 0x00);     /* VS start high */
    ov5640_write_reg(0x3803, 0x04);     /* VS start low */
    ov5640_write_reg(0x3804, 0x0a);     /* HW end high */
    ov5640_write_reg(0x3805, 0x3f);     /* HW end low (full resolution) */
    ov5640_write_reg(0x3806, 0x07);     /* VW end high */
    ov5640_write_reg(0x3807, 0x9f);     /* VW end low (full resolution) */
    ov5640_write_reg(0x3810, 0x00);     /* H offset high */
    ov5640_write_reg(0x3811, 0x10);     /* H offset low */
    ov5640_write_reg(0x3812, 0x00);     /* V offset high */
    ov5640_write_reg(0x3813, 0x00);     /* V offset low */

    /* Misc timing and BLC */
    ov5640_write_reg(0x3708, 0x64);     /* Analog setting */
    ov5640_write_reg(0x3a08, 0x01);     /* B50 step */
    ov5640_write_reg(0x4001, 0x02);     /* BLC start line */
    ov5640_write_reg(0x4005, 0x1a);     /* BLC always update */

    /* System reset and clock enable */
    ov5640_write_reg(0x3000, 0x00);     /* System reset 0 */
    ov5640_write_reg(0x3002, 0x1c);     /* System reset 2 */
    ov5640_write_reg(0x3004, 0xff);     /* Clock enable 0 */
    ov5640_write_reg(0x3006, 0xc3);     /* Clock enable 2 */

    /*
     * MIPI control: 2-lane MIPI enable
     * Reference: camera_thread_entry.c:601-608
     */
#if (OV5640_MIPI_NUM_LANES == 2)
    ov5640_write_reg(0x300e, 0x45);     /* MIPI control: 2 lane, MIPI enable */
#else
    ov5640_write_reg(0x300e, 0x25);     /* MIPI control: 1 lane, MIPI enable */
#endif

    ov5640_write_reg(0x302e, 0x08);

    /* Output format and ISP settings */
    ov5640_init_output_format();

    /* ISP control */
    ov5640_write_reg(0x501f, 0x00);     /* ISP YUV 422 */
    ov5640_write_reg(0x4407, 0x04);     /* JPEG QS */
    ov5640_write_reg(0x440e, 0x00);
    ov5640_write_reg(0x5000, 0xa7);     /* ISP: Lenc on, gamma on, BPC on, WPC on, CIP on */

    /* AWB (Auto White Balance) */
    ov5640_init_awb_registers();

    /* Color matrix */
    ov5640_init_color_matrix();

    /* CIP (Color Interpolation) */
    ov5640_init_cip_registers();

    /* Gamma */
    ov5640_init_gamma_registers();

    /* UV adjust */
    ov5640_init_uv_adjust();

    /* Lens correction */
    ov5640_init_lens_correction();

    /* AEC (Auto Exposure Control) */
    ov5640_init_aec_registers();

    /*
     * Test image and MIPI settings
     * Reference: camera_thread_entry.c:792-803
     */
    ov5640_write_reg(0x503d, (uint8_t)((OV5640_TEST_IMAGE_ENABLE << 7) |
                                        (OV5640_TEST_IMAGE_ROLLING << 6) |
                                        (OV5640_TEST_IMAGE_TRANSPARENT << 5) |
                                        (OV5640_TEST_IMAGE_SQUARE << 4) |
                                        (OV5640_TEST_IMAGE_BAR_STYLE << 2) |
                                        (OV5640_TEST_IMAGE_TYPE << 0)));

    ov5640_write_reg(0x4800, (uint8_t)(0x04 | (!OV5640_CONTINUOUS_CLOCK << 5)));

    /* Disable DVP output, enable MIPI only */
    ov5640_write_reg(0x3007, 0xfb);     /* Disable DVP PCLK output */
    ov5640_write_reg(0x3017, 0x00);     /* Disable DVP PCLK output */
    ov5640_write_reg(0x301d, 0xff);
    ov5640_write_reg(0x5001, 0xa3);     /* SDE on, scale on, UV avg off, CMX on, AWB on */

    /* Configure PLL clocks */
    ov5640_configure_clocks();

    /* Set MIPI virtual channel 0 */
    ov5640_set_mipi_virtual_channel(0x00);

    /* Wake up from software power down */
    ov5640_write_reg(0x3008, 0x02);

    /* Final timing and resolution settings */
    ov5640_init_timing_registers();

    s_ov5640_status.initialized = true;

    return FSP_SUCCESS;
}

/**********************************************************************************************************************
 * Function Name: ov5640_get_status
 * Description  : Get current driver status for diagnostics
 * Arguments    : None
 * Return Value : Pointer to internal status structure (read-only)
 *********************************************************************************************************************/
const ov5640_status_t *ov5640_get_status(void)
{
    return &s_ov5640_status;
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**********************************************************************************************************************
 * Function Name: ov5640_i2c_wait_complete
 * Description  : Wait for I2C transfer to complete using FreeRTOS event group
 * Arguments    : None
 * Return Value : FSP_SUCCESS on complete, FSP_ERR_ABORTED on abort, FSP_ERR_TIMEOUT on timeout
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/board_i2c_master.c:52-78
 *********************************************************************************************************************/
static fsp_err_t ov5640_i2c_wait_complete(void)
{
    fsp_err_t ret;
    EventBits_t uxBits;

    uxBits = xEventGroupWaitBits(g_i2c_event_group,
                                 I2C_TRANSFER_COMPLETE | I2C_TRANSFER_ABORT,
                                 pdTRUE,      /* Clear bits before returning */
                                 pdFALSE,     /* Either bit will do */
                                 I2C_TIMEOUT_MS);

    if ((I2C_TRANSFER_COMPLETE & uxBits) == I2C_TRANSFER_COMPLETE)
    {
        ret = FSP_SUCCESS;
    }
    else if ((I2C_TRANSFER_ABORT & uxBits) == I2C_TRANSFER_ABORT)
    {
        ret = FSP_ERR_ABORTED;
    }
    else
    {
        /* Timeout */
        ret = FSP_ERR_TIMEOUT;
    }

    return ret;
}

/**********************************************************************************************************************
 * Function Name: ov5640_i2c_write_reg16_8
 * Description  : Write 8-bit data to 16-bit register address via I2C
 * Arguments    : reg  - 16-bit register address
 *              : data - 8-bit data to write
 * Return Value : FSP_SUCCESS on success, error code on failure
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/board_i2c_master.c:160-173
 *********************************************************************************************************************/
static fsp_err_t ov5640_i2c_write_reg16_8(uint16_t reg, uint8_t data)
{
    fsp_err_t err;
    uint8_t buf[3];

    buf[0] = (uint8_t)((reg & 0xFF00U) >> 8);   /* Register address high byte */
    buf[1] = (uint8_t)(reg & 0x00FFU);           /* Register address low byte */
    buf[2] = data;                                /* Data byte */

    err = R_IIC_MASTER_Write(&g_i2c_master_camera_ctrl, buf, 3, false);
    if (FSP_SUCCESS == err)
    {
        err = ov5640_i2c_wait_complete();
    }

    return err;
}

/**********************************************************************************************************************
 * Function Name: ov5640_i2c_read_reg16_8
 * Description  : Read 8-bit data from 16-bit register address via I2C
 *              : Performs I2C write (register address) then I2C read (data)
 * Arguments    : reg  - 16-bit register address
 *              : data - Pointer to store read data
 * Return Value : FSP_SUCCESS on success, error code on failure
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/board_i2c_master.c:186-207
 *********************************************************************************************************************/
static fsp_err_t ov5640_i2c_read_reg16_8(uint16_t reg, uint8_t *data)
{
    fsp_err_t err;
    uint8_t buf[2];

    buf[0] = (uint8_t)((reg & 0xFF00U) >> 8);   /* Register address high byte */
    buf[1] = (uint8_t)(reg & 0x00FFU);           /* Register address low byte */

    /* Write register address with restart (no stop) */
    err = R_IIC_MASTER_Write(&g_i2c_master_camera_ctrl, buf, 2, true);
    if (FSP_SUCCESS == err)
    {
        err = ov5640_i2c_wait_complete();
    }

    /* Read data byte */
    if (FSP_SUCCESS == err)
    {
        err = R_IIC_MASTER_Read(&g_i2c_master_camera_ctrl, data, 1, false);
        if (FSP_SUCCESS == err)
        {
            err = ov5640_i2c_wait_complete();
        }
    }

    return err;
}

/**********************************************************************************************************************
 * Function Name: ov5640_init_base_registers
 * Description  : Write analog and misc base registers
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:543-570
 *********************************************************************************************************************/
static void ov5640_init_base_registers(void)
{
    /* Analog settings */
    ov5640_write_reg(0x3630, 0x36);
    ov5640_write_reg(0x3631, 0x0e);
    ov5640_write_reg(0x3632, 0xe2);
    ov5640_write_reg(0x3633, 0x12);
    ov5640_write_reg(0x3621, 0xe0);
    ov5640_write_reg(0x3704, 0xa0);
    ov5640_write_reg(0x3703, 0x5a);
    ov5640_write_reg(0x3715, 0x78);
    ov5640_write_reg(0x3717, 0x01);
    ov5640_write_reg(0x370b, 0x60);
    ov5640_write_reg(0x3705, 0x1a);
    ov5640_write_reg(0x3905, 0x02);
    ov5640_write_reg(0x3906, 0x10);
    ov5640_write_reg(0x3901, 0x0a);
    ov5640_write_reg(0x3731, 0x12);

    /* VCM debug mode */
    ov5640_write_reg(0x3600, 0x08);
    ov5640_write_reg(0x3601, 0x33);

    /* System control */
    ov5640_write_reg(0x302d, 0x60);

    /* More analog settings */
    ov5640_write_reg(0x3620, 0x52);
    ov5640_write_reg(0x371b, 0x20);
    ov5640_write_reg(0x471c, 0x50);

    /* AGC pre-gain and gain ceiling */
    ov5640_write_reg(0x3a13, 0x43);     /* AGC pre-gain, 0x40 = 1x */
    ov5640_write_reg(0x3a18, 0x00);     /* Gain ceiling high */
    ov5640_write_reg(0x3a19, 0xf8);     /* Gain ceiling low */

    /* More analog */
    ov5640_write_reg(0x3635, 0x13);
    ov5640_write_reg(0x3636, 0x03);
    ov5640_write_reg(0x3634, 0x40);
    ov5640_write_reg(0x3622, 0x01);
}

/**********************************************************************************************************************
 * Function Name: ov5640_init_output_format
 * Description  : Configure output data format (YUV422 YUYV 8-bit) and PLL charge pump
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:611-630
 *********************************************************************************************************************/
static void ov5640_init_output_format(void)
{
    uint8_t mipi_bits = 8;  /* 8-bit MIPI mode */

#if OV5640_INPUT_FORMAT_YUV422_8BIT
    ov5640_write_reg(0x4300, 0x32);     /* YUV 422, YUYV */
#elif OV5640_INPUT_FORMAT_RAW8
    ov5640_write_reg(0x4300, 0xf8);     /* RAW */
#elif OV5640_INPUT_FORMAT_RGB888
    ov5640_write_reg(0x4300, 0x22);     /* YUV444/RGB888 */
#endif

    /*
     * 0x3034 SC PLL CTRL0 (Default: 0x18)
     * Bit[6:4]: PLL charge pump control
     * Bit[3:0]: MIPI bit mode (8 or 10)
     */
    uint8_t pll_charge_pump_mode = 1;
    uint8_t reg_3034 = (uint8_t)((pll_charge_pump_mode << 4) | (mipi_bits << 0));
    ov5640_write_reg(OV5640_REG_PLL_CTRL0, reg_3034);
}

/**********************************************************************************************************************
 * Function Name: ov5640_init_awb_registers
 * Description  : Configure Auto White Balance settings
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:636-665
 *********************************************************************************************************************/
static void ov5640_init_awb_registers(void)
{
    ov5640_write_reg(0x5180, 0xff);
    ov5640_write_reg(0x5181, 0xf2);
    ov5640_write_reg(0x5182, 0x00);
    ov5640_write_reg(0x5183, 0x14);
    ov5640_write_reg(0x5184, 0x25);
    ov5640_write_reg(0x5185, 0x24);
    ov5640_write_reg(0x5186, 0x09);
    ov5640_write_reg(0x5187, 0x09);
    ov5640_write_reg(0x5188, 0x09);
    ov5640_write_reg(0x5189, 0x75);
    ov5640_write_reg(0x518a, 0x54);
    ov5640_write_reg(0x518b, 0xe0);
    ov5640_write_reg(0x518c, 0xb2);
    ov5640_write_reg(0x518d, 0x42);
    ov5640_write_reg(0x518e, 0x3d);
    ov5640_write_reg(0x518f, 0x56);
    ov5640_write_reg(0x5190, 0x46);
    ov5640_write_reg(0x5191, 0xf8);
    ov5640_write_reg(0x5192, 0x04);
    ov5640_write_reg(0x5193, 0x70);
    ov5640_write_reg(0x5194, 0xf0);
    ov5640_write_reg(0x5195, 0xf0);
    ov5640_write_reg(0x5196, 0x03);
    ov5640_write_reg(0x5197, 0x01);
    ov5640_write_reg(0x5198, 0x04);
    ov5640_write_reg(0x5199, 0x12);
    ov5640_write_reg(0x519a, 0x04);
    ov5640_write_reg(0x519b, 0x00);
    ov5640_write_reg(0x519c, 0x06);
    ov5640_write_reg(0x519d, 0x82);
    ov5640_write_reg(0x519e, 0x38);
}

/**********************************************************************************************************************
 * Function Name: ov5640_init_color_matrix
 * Description  : Configure color matrix coefficients
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:668-679
 *********************************************************************************************************************/
static void ov5640_init_color_matrix(void)
{
    ov5640_write_reg(0x5381, 0x1e);
    ov5640_write_reg(0x5382, 0x5b);
    ov5640_write_reg(0x5383, 0x08);
    ov5640_write_reg(0x5384, 0x0a);
    ov5640_write_reg(0x5385, 0x7e);
    ov5640_write_reg(0x5386, 0x88);
    ov5640_write_reg(0x5387, 0x7c);
    ov5640_write_reg(0x5388, 0x6c);
    ov5640_write_reg(0x5389, 0x10);
    ov5640_write_reg(0x538a, 0x01);
    ov5640_write_reg(0x538b, 0x98);
}

/**********************************************************************************************************************
 * Function Name: ov5640_init_cip_registers
 * Description  : Configure Color Interpolation (CIP) sharpening and denoising
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:680-691
 *********************************************************************************************************************/
static void ov5640_init_cip_registers(void)
{
    ov5640_write_reg(0x5300, 0x08);     /* Sharpen MT th1 */
    ov5640_write_reg(0x5301, 0x30);     /* Sharpen MT th2 */
    ov5640_write_reg(0x5302, 0x10);     /* Sharpen MT offset 1 */
    ov5640_write_reg(0x5303, 0x00);     /* Sharpen MT offset 2 */
    ov5640_write_reg(0x5304, 0x08);     /* DNS threshold 1 */
    ov5640_write_reg(0x5305, 0x30);     /* DNS threshold 2 */
    ov5640_write_reg(0x5306, 0x08);     /* DNS offset 1 */
    ov5640_write_reg(0x5307, 0x16);     /* DNS offset 2 */
    ov5640_write_reg(0x5309, 0x08);     /* Sharpen TH th1 */
    ov5640_write_reg(0x530a, 0x30);     /* Sharpen TH th2 */
    ov5640_write_reg(0x530b, 0x04);     /* Sharpen TH offset 1 */
    ov5640_write_reg(0x530c, 0x06);     /* Sharpen TH offset 2 */
}

/**********************************************************************************************************************
 * Function Name: ov5640_init_gamma_registers
 * Description  : Configure gamma correction curve
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:693-709
 *********************************************************************************************************************/
static void ov5640_init_gamma_registers(void)
{
    ov5640_write_reg(0x5480, 0x01);
    ov5640_write_reg(0x5481, 0x08);
    ov5640_write_reg(0x5482, 0x14);
    ov5640_write_reg(0x5483, 0x28);
    ov5640_write_reg(0x5484, 0x51);
    ov5640_write_reg(0x5485, 0x65);
    ov5640_write_reg(0x5486, 0x71);
    ov5640_write_reg(0x5487, 0x7d);
    ov5640_write_reg(0x5488, 0x87);
    ov5640_write_reg(0x5489, 0x91);
    ov5640_write_reg(0x548a, 0x9a);
    ov5640_write_reg(0x548b, 0xaa);
    ov5640_write_reg(0x548c, 0xb8);
    ov5640_write_reg(0x548d, 0xcd);
    ov5640_write_reg(0x548e, 0xdd);
    ov5640_write_reg(0x548f, 0xea);
    ov5640_write_reg(0x5490, 0x1d);
}

/**********************************************************************************************************************
 * Function Name: ov5640_init_uv_adjust
 * Description  : Configure UV (saturation/contrast) adjustment
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:711-717
 *********************************************************************************************************************/
static void ov5640_init_uv_adjust(void)
{
    ov5640_write_reg(0x5580, 0x06);     /* Saturation on, contrast on */
    ov5640_write_reg(0x5583, 0x40);     /* Saturation U */
    ov5640_write_reg(0x5584, 0x10);     /* Saturation V */
    ov5640_write_reg(0x5589, 0x10);     /* UV adjust th1 */
    ov5640_write_reg(0x558a, 0x00);     /* UV adjust th2[8] */
    ov5640_write_reg(0x558b, 0xf8);     /* UV adjust th2[7:0] */
    ov5640_write_reg(0x501d, 0x04);     /* Enable manual offset of contrast */
}

/**********************************************************************************************************************
 * Function Name: ov5640_init_lens_correction
 * Description  : Configure lens shading correction parameters
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:719-781
 *********************************************************************************************************************/
static void ov5640_init_lens_correction(void)
{
    ov5640_write_reg(0x5800, 0x23);
    ov5640_write_reg(0x5801, 0x14);
    ov5640_write_reg(0x5802, 0x0f);
    ov5640_write_reg(0x5803, 0x0f);
    ov5640_write_reg(0x5804, 0x12);
    ov5640_write_reg(0x5805, 0x26);
    ov5640_write_reg(0x5806, 0x0c);
    ov5640_write_reg(0x5807, 0x08);
    ov5640_write_reg(0x5808, 0x05);
    ov5640_write_reg(0x5809, 0x05);
    ov5640_write_reg(0x580a, 0x08);
    ov5640_write_reg(0x580b, 0x0d);
    ov5640_write_reg(0x580c, 0x08);
    ov5640_write_reg(0x580d, 0x03);
    ov5640_write_reg(0x580e, 0x00);
    ov5640_write_reg(0x580f, 0x00);
    ov5640_write_reg(0x5810, 0x03);
    ov5640_write_reg(0x5811, 0x09);
    ov5640_write_reg(0x5812, 0x07);
    ov5640_write_reg(0x5813, 0x03);
    ov5640_write_reg(0x5814, 0x00);
    ov5640_write_reg(0x5815, 0x01);
    ov5640_write_reg(0x5816, 0x03);
    ov5640_write_reg(0x5817, 0x08);
    ov5640_write_reg(0x5818, 0x0d);
    ov5640_write_reg(0x5819, 0x08);
    ov5640_write_reg(0x581a, 0x05);
    ov5640_write_reg(0x581b, 0x06);
    ov5640_write_reg(0x581c, 0x08);
    ov5640_write_reg(0x581d, 0x0e);
    ov5640_write_reg(0x581e, 0x29);
    ov5640_write_reg(0x581f, 0x17);
    ov5640_write_reg(0x5820, 0x11);
    ov5640_write_reg(0x5821, 0x11);
    ov5640_write_reg(0x5822, 0x15);
    ov5640_write_reg(0x5823, 0x28);
    ov5640_write_reg(0x5824, 0x46);
    ov5640_write_reg(0x5825, 0x26);
    ov5640_write_reg(0x5826, 0x08);
    ov5640_write_reg(0x5827, 0x26);
    ov5640_write_reg(0x5828, 0x64);
    ov5640_write_reg(0x5829, 0x26);
    ov5640_write_reg(0x582a, 0x24);
    ov5640_write_reg(0x582b, 0x22);
    ov5640_write_reg(0x582c, 0x24);
    ov5640_write_reg(0x582d, 0x24);
    ov5640_write_reg(0x582e, 0x06);
    ov5640_write_reg(0x582f, 0x22);
    ov5640_write_reg(0x5830, 0x40);
    ov5640_write_reg(0x5831, 0x42);
    ov5640_write_reg(0x5832, 0x24);
    ov5640_write_reg(0x5833, 0x26);
    ov5640_write_reg(0x5834, 0x24);
    ov5640_write_reg(0x5835, 0x22);
    ov5640_write_reg(0x5836, 0x22);
    ov5640_write_reg(0x5837, 0x26);
    ov5640_write_reg(0x5838, 0x44);
    ov5640_write_reg(0x5839, 0x24);
    ov5640_write_reg(0x583a, 0x26);
    ov5640_write_reg(0x583b, 0x28);
    ov5640_write_reg(0x583c, 0x42);
    ov5640_write_reg(0x583d, 0xce);

    ov5640_write_reg(0x5025, 0x00);
}

/**********************************************************************************************************************
 * Function Name: ov5640_init_aec_registers
 * Description  : Configure AEC (Auto Exposure Control) parameters
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:782-787
 *********************************************************************************************************************/
static void ov5640_init_aec_registers(void)
{
    ov5640_write_reg(0x3a0f, 0x30);     /* Stable in high */
    ov5640_write_reg(0x3a10, 0x28);     /* Stable in low */
    ov5640_write_reg(0x3a1b, 0x30);     /* Stable out high */
    ov5640_write_reg(0x3a1e, 0x26);     /* Stable out low */
    ov5640_write_reg(0x3a11, 0x60);     /* Fast zone high */
    ov5640_write_reg(0x3a1f, 0x14);     /* Fast zone low */
}

/**********************************************************************************************************************
 * Function Name: ov5640_init_timing_registers
 * Description  : Configure final timing, resolution, and exposure settings
 *              : Uses VIN configuration image size from FSP (OV5640_DEFAULT_IMAGE_WIDTH/HEIGHT)
 * Reference    : reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:817-893
 *********************************************************************************************************************/
static void ov5640_init_timing_registers(void)
{
    uint16_t image_width  = OV5640_DEFAULT_IMAGE_WIDTH;
    uint16_t image_height = OV5640_DEFAULT_IMAGE_HEIGHT;

    /* Light meter */
    ov5640_write_reg(0x3c07, 0x08);

    /* Image flip/mirror */
#if OV5640_FLIP_IMAGE
    ov5640_write_reg(0x3820, 0x43);     /* ISP flip on */
#else
    ov5640_write_reg(0x3820, 0x40);     /* ISP flip off */
#endif

#if OV5640_MIRROR_IMAGE
    ov5640_write_reg(0x3821, 0x07);     /* ISP mirror on, binning enable */
#else
    ov5640_write_reg(0x3821, 0x01);     /* ISP mirror off, binning enable */
#endif

    /* X and Y increment (sub-sampling) */
    ov5640_write_reg(0x3814, 0x31);     /* X inc */
    ov5640_write_reg(0x3815, 0x31);     /* Y inc */

    /*
     * Sensor window settings for the target resolution
     * Using simplified fixed settings based on reference defaults.
     * Reference: camera_thread_entry.c:833-872
     *
     * For VGA-like output with 2:1 sub-sampling, the crop window
     * is set near full resolution and the output is scaled down.
     */
    ov5640_write_reg(0x3803, 0x04);     /* VS start */

    /* Output image size */
    ov5640_write_reg(0x3808, (uint8_t)(image_width >> 8));      /* DVPHO high */
    ov5640_write_reg(0x3809, (uint8_t)(image_width & 0xFF));    /* DVPHO low */
    ov5640_write_reg(0x380a, (uint8_t)(image_height >> 8));     /* DVPVO high */
    ov5640_write_reg(0x380b, (uint8_t)(image_height & 0xFF));   /* DVPVO low */

    /* V offset */
    ov5640_write_reg(0x3813, 0x06);

    /* Analog settings for sub-sampled mode */
    ov5640_write_reg(0x3618, 0x00);
    ov5640_write_reg(0x3612, 0x29);
    ov5640_write_reg(0x3709, 0x52);
    ov5640_write_reg(0x370c, 0x03);

    /* 60Hz exposure limits */
    ov5640_write_reg(0x3a02, 0x03);     /* 60Hz max exposure high */
    ov5640_write_reg(0x3a03, 0xd8);     /* 60Hz max exposure low */

    /* Banding filter */
    ov5640_write_reg(0x3a09, 0x27);     /* B50 low */
    ov5640_write_reg(0x3a0a, 0x00);     /* B60 high */
    ov5640_write_reg(0x3a0b, 0xf6);     /* B60 low */
    ov5640_write_reg(0x3a0e, 0x03);     /* B50 max */
    ov5640_write_reg(0x3a0d, 0x04);     /* B60 max */

    /* 50Hz exposure limits */
    ov5640_write_reg(0x3a14, 0x03);     /* 50Hz max exposure high */
    ov5640_write_reg(0x3a15, 0xd8);     /* 50Hz max exposure low */

    /* Misc */
    ov5640_write_reg(0x4004, 0x02);     /* BLC line number */
    ov5640_write_reg(0x4713, 0x03);     /* JPEG mode 3 */
    ov5640_write_reg(0x460b, 0x35);     /* Debug */
    ov5640_write_reg(0x460c, 0x22);     /* VFIFO, PCLK manual */

    /* MIPI global timing */
    ov5640_write_reg(0x4837, OV5640_MIPI_GLOBAL_TIMING);

    /* PCLK divider */
    ov5640_write_reg(0x3824, 0x02);

    /* SDE on, scale on, UV average off, CMX on, AWB on */
    ov5640_write_reg(0x5001, 0xa3);
}
