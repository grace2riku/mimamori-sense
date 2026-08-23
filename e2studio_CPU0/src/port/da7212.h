/**
 * @file da7212.h
 * @brief DA7212 audio CODEC register map and driver API (Issue #46)
 * @details
 * The EK-RA8P1 carries a Renesas (ex Dialog) DA7212 ultra-low-power stereo
 * codec (U14). FSP 6.3.0 ships no DA7212/DA7xxx driver, so the register map
 * and the start-up sequence are implemented here from the datasheet.
 *
 * PRIMARY SOURCE for every register address, bit field and timing constant
 * in this file (unless a different source is named on the line itself):
 *
 *   "DA7212 Ultra-Low Power Stereo Codec Datasheet, Revision 3.5,
 *    05-Jan-2022", Renesas Electronics, document CFR0011-120-00.
 *   https://www.farnell.com/datasheets/3962888.pdf
 *   (Rev 3.6 is also published at
 *    https://www.renesas.com/en/document/dst/da7212-datasheet)
 *   Section 14 "Register definitions": 14.1 register map (p65-70),
 *   14.3 system initialisation registers (p76-81),
 *   14.5 output gain-filter registers (p87-94),
 *   14.6 system controller registers (p95-96),
 *   14.7 control registers (p97-105),
 *   14.9 configuration registers (p107-...).
 *
 * SECONDARY SOURCE, used only for cross-checking the register addresses and
 * for the "which mixer feeds the LINE amplifier" routing question:
 *
 *   Linux kernel `sound/soc/codecs/da7213.h` / `da7213.c` (the in-tree driver
 *   that also binds "dlg,da7212"). The DAPM route
 *   `{"Lineout PGA", NULL, "Mixout Right PGA"}` (da7213.c:1190) establishes
 *   that the mono LINE/speaker amplifier is fed from MIXOUT_R.
 *
 * Board wiring (EK-RA8P1 v1 schematic p18 "SSIE/I2S Audio Codec";
 * EK-RA8P1 UM R20UT5309JG0104 Rev.1.04, section 6.6 and table 32):
 *   BCLK  = P403, WCLK = P404, DATIN = P405 (J41-3/4), DATOUT = P406,
 *   MCLK  = PD06 (driven by GPT2 GTIOC2A),
 *   SDA/SCL = P511/P512 (IIC1, SYS_I2C), 7-bit slave address 0x1A,
 *   SP_P/SP_N = J33-1/J33-2 (speaker).
 *
 * Clocking used by this driver (see da7212.c for the derivation):
 *   MCLK  = 12.5 MHz     (PCLKD 250 MHz / 20, ra_gen/hal_data.c:257-260)
 *   BCLK  = 512,295 Hz   (PCLKD 250 MHz / 488, ra_gen/hal_data.c:131-134)
 *   WCLK  = BCLK / 32    = 16,009.2 Hz  (16-bit x 2ch frame)
 *   DA7212 runs as DAI SLAVE with the PLL in normal (MCLK locked) mode.
 */

#ifndef DA7212_H
#define DA7212_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

#include "hal_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/**********************************************************************************************************************
 Macro definitions - register addresses (datasheet 14.1 register map, p65-70)
 *********************************************************************************************************************/

#define DA7212_REG_STATUS1                  (0x02U)
#define DA7212_REG_PLL_STATUS               (0x03U)

#define DA7212_REG_CIF_CTRL                 (0x1DU)
#define DA7212_REG_DIG_ROUTING_DAI          (0x21U)
#define DA7212_REG_SR                       (0x22U)
#define DA7212_REG_REFERENCES               (0x23U)
#define DA7212_REG_PLL_FRAC_TOP             (0x24U)
#define DA7212_REG_PLL_FRAC_BOT             (0x25U)
#define DA7212_REG_PLL_INTEGER              (0x26U)
#define DA7212_REG_PLL_CTRL                 (0x27U)
#define DA7212_REG_DAI_CLK_MODE             (0x28U)
#define DA7212_REG_DAI_CTRL                 (0x29U)
#define DA7212_REG_DIG_ROUTING_DAC          (0x2AU)

#define DA7212_REG_DAC_FILTERS5             (0x40U)
#define DA7212_REG_DAC_FILTERS1             (0x44U)
#define DA7212_REG_DAC_L_GAIN               (0x45U)
#define DA7212_REG_DAC_R_GAIN               (0x46U)
#define DA7212_REG_LINE_GAIN                (0x4AU)
#define DA7212_REG_MIXOUT_L_SELECT          (0x4BU)
#define DA7212_REG_MIXOUT_R_SELECT          (0x4CU)

#define DA7212_REG_SYSTEM_MODES_INPUT       (0x50U)
#define DA7212_REG_SYSTEM_MODES_OUTPUT      (0x51U)

#define DA7212_REG_DAC_L_CTRL               (0x69U)
#define DA7212_REG_DAC_R_CTRL               (0x6AU)
#define DA7212_REG_LINE_CTRL                (0x6DU)
#define DA7212_REG_MIXOUT_L_CTRL            (0x6EU)
#define DA7212_REG_MIXOUT_R_CTRL            (0x6FU)

#define DA7212_REG_LDO_CTRL                 (0x90U)
#define DA7212_REG_IO_CTRL                  (0x91U)
#define DA7212_REG_GAIN_RAMP_CTRL           (0x92U)
#define DA7212_REG_PC_COUNT                 (0x94U)

#define DA7212_REG_SYSTEM_STATUS            (0xE0U)
#define DA7212_REG_SYSTEM_ACTIVE            (0xFDU)

/**********************************************************************************************************************
 Macro definitions - bit fields (datasheet 14.3 / 14.5 / 14.6 / 14.7 / 14.9)
 *********************************************************************************************************************/

/* CIF_CTRL (0x1D) - datasheet 14.3 p76 */
#define DA7212_CIF_REG_SOFT_RESET           (1U << 7)   /**< write 1: all registers back to default */

/* SR (0x22) - datasheet 14.3 p77 (sample rate control) */
#define DA7212_SR_8000                      (0x1U)
#define DA7212_SR_11025                     (0x2U)
#define DA7212_SR_12000                     (0x3U)
#define DA7212_SR_16000                     (0x5U)
#define DA7212_SR_22050                     (0x6U)
#define DA7212_SR_24000                     (0x7U)
#define DA7212_SR_32000                     (0x9U)
#define DA7212_SR_44100                     (0xAU)
#define DA7212_SR_48000                     (0xBU)

/* REFERENCES (0x23) - datasheet 14.3 p78.
 * bit3 = BIAS_EN (master bias). bits 7/6 and 2:0 are reserved; bit 7 has a
 * reset value of 1, so the register is updated read-modify-write and never
 * written blind. (Cross-check: the Linux driver's DA7213 reset table has
 * REFERENCES = 0x80, da7213.c:1197.) */
#define DA7212_BIAS_EN                      (1U << 3)

/* PLL_CTRL (0x27) - datasheet 14.3 p79 */
#define DA7212_PLL_EN                       (1U << 7)
#define DA7212_PLL_SRM_EN                   (1U << 6)
#define DA7212_PLL_32K_MODE                 (1U << 5)
#define DA7212_PLL_MCLK_SQR_EN              (1U << 4)
#define DA7212_PLL_INDIV_2_10MHZ            (0x0U << 2) /**< input divider /2  */
#define DA7212_PLL_INDIV_10_20MHZ           (0x1U << 2) /**< input divider /4  */
#define DA7212_PLL_INDIV_20_40MHZ           (0x2U << 2) /**< input divider /8  */
#define DA7212_PLL_INDIV_40_80MHZ           (0x3U << 2) /**< input divider /16 */

/* PLL_STATUS (0x03) - datasheet 14.1 register map p65 / 14.2 */
#define DA7212_PLL_LOCK                     (1U << 0)
#define DA7212_PLL_SRM_LOCK                 (1U << 1)
#define DA7212_PLL_MCLK_STATUS              (1U << 2)
#define DA7212_PLL_BYPASS_ACTIVE            (1U << 3)

/* DAI_CLK_MODE (0x28) - datasheet 14.3 p79 */
#define DA7212_DAI_CLK_EN                   (1U << 7)   /**< 0 = slave (BCLK/WCLK inputs) */
#define DA7212_DAI_WCLK_POL_INV             (1U << 3)
#define DA7212_DAI_CLK_POL_INV              (1U << 2)
#define DA7212_DAI_BCLKS_PER_WCLK_32        (0x0U << 0)
#define DA7212_DAI_BCLKS_PER_WCLK_64        (0x1U << 0)
#define DA7212_DAI_BCLKS_PER_WCLK_128       (0x2U << 0)
#define DA7212_DAI_BCLKS_PER_WCLK_256       (0x3U << 0)

/* DAI_CTRL (0x29) - datasheet 14.3 p80 */
#define DA7212_DAI_EN                       (1U << 7)
#define DA7212_DAI_OE                       (1U << 6)   /**< 1 = DATOUT driven */
#define DA7212_DAI_TDM_MODE_EN              (1U << 5)
#define DA7212_DAI_MONO_MODE_EN             (1U << 4)
#define DA7212_DAI_WORD_LENGTH_16           (0x0U << 2)
#define DA7212_DAI_WORD_LENGTH_20           (0x1U << 2)
#define DA7212_DAI_WORD_LENGTH_24           (0x2U << 2)
#define DA7212_DAI_WORD_LENGTH_32           (0x3U << 2)
#define DA7212_DAI_FORMAT_I2S               (0x0U << 0)
#define DA7212_DAI_FORMAT_LEFT_J            (0x1U << 0)
#define DA7212_DAI_FORMAT_RIGHT_J           (0x2U << 0)
#define DA7212_DAI_FORMAT_DSP               (0x3U << 0)

/* DIG_ROUTING_DAC (0x2A) - datasheet 14.3 p81.
 * DAC_x_SRC = 10/11 means "determined by DAC_L_MONO / DAC_R_MONO". With both
 * MONO bits cleared this routes DAI-left -> DAC_L and DAI-right -> DAC_R,
 * which is the reset value 0x32. */
#define DA7212_DAC_R_MONO                   (1U << 7)
#define DA7212_DAC_R_SRC_BY_DAC_R_MONO      (0x3U << 4)
#define DA7212_DAC_L_MONO                   (1U << 3)
#define DA7212_DAC_L_SRC_BY_DAC_L_MONO      (0x2U << 0)
#define DA7212_DIG_ROUTING_DAC_DEFAULT      (0x32U)

/* MIXOUT_L/R_SELECT (0x4B/0x4C) - datasheet 14.5 p92-93 */
#define DA7212_MIXOUT_SELECT_DAC            (1U << 3)   /**< DAC_L for 0x4B, DAC_R for 0x4C */
#define DA7212_MIXOUT_SELECT_MIXIN_A        (1U << 1)
#define DA7212_MIXOUT_SELECT_AUX            (1U << 0)

/* LINE_GAIN (0x4A) - datasheet 14.5 p92.
 * 6-bit field, 1 dB steps: 0x00 = -48 dB ... 0x30 = 0 dB (reset) ... 0x3F = +15 dB */
#define DA7212_LINE_AMP_GAIN_MASK           (0x3FU)
#define DA7212_LINE_AMP_GAIN_MIN_DB         (-48)
#define DA7212_LINE_AMP_GAIN_0DB_CODE       (0x30U)

/* DAC_L/R_CTRL (0x69/0x6A) - datasheet 14.7 p105 */
#define DA7212_DAC_EN                       (1U << 7)
#define DA7212_DAC_MUTE_EN                  (1U << 6)
#define DA7212_DAC_RAMP_EN                  (1U << 5)

/* LINE_CTRL (0x6D) - datasheet 14.7 p105 */
#define DA7212_LINE_AMP_EN                  (1U << 7)
#define DA7212_LINE_AMP_MUTE_EN             (1U << 6)
#define DA7212_LINE_AMP_RAMP_EN             (1U << 5)
#define DA7212_LINE_AMP_OE                  (1U << 3)   /**< 0 = high impedance */
#define DA7212_LINE_AMP_MIN_GAIN_EN         (1U << 2)

/* MIXOUT_L/R_CTRL (0x6E/0x6F) - datasheet 14.7 p105-106 */
#define DA7212_MIXOUT_AMP_EN                (1U << 7)
#define DA7212_MIXOUT_SOFTMIX_EN            (1U << 4)
#define DA7212_MIXOUT_MIX_EN                (1U << 3)

/* SYSTEM_MODES_OUTPUT (0x51) - datasheet 14.6 p96 */
#define DA7212_SYSMODE_OUT_DAC_R            (1U << 7)
#define DA7212_SYSMODE_OUT_DAC_L            (1U << 6)
#define DA7212_SYSMODE_OUT_HP_R             (1U << 5)
#define DA7212_SYSMODE_OUT_HP_L             (1U << 4)
#define DA7212_SYSMODE_OUT_LINE             (1U << 3)
#define DA7212_SYSMODE_OUT_AUX_R            (1U << 2)
#define DA7212_SYSMODE_OUT_AUX_L            (1U << 1)
#define DA7212_SYSMODE_MODE_SUBMIT          (1U << 0)   /**< self-clearing */

/* SYSTEM_STATUS (0xE0) - datasheet 14.1 register map p70 */
#define DA7212_SC2_BUSY                     (1U << 1)
#define DA7212_SC1_BUSY                     (1U << 0)

/* SYSTEM_ACTIVE (0xFD) - datasheet 13.40 / 14.1 register map p70 */
#define DA7212_SYSTEM_ACTIVE                (1U << 0)

/* LDO_CTRL (0x90) - datasheet 14.9 p107 */
#define DA7212_LDO_EN                       (1U << 7)
#define DA7212_LDO_LEVEL_1V05               (0x0U << 4)
#define DA7212_LDO_LEVEL_1V10               (0x1U << 4)
#define DA7212_LDO_LEVEL_1V20               (0x2U << 4)
#define DA7212_LDO_LEVEL_1V40               (0x3U << 4)

/* IO_CTRL (0x91) - datasheet 14.9 p107 */
#define DA7212_IO_VOLTAGE_1V2_TO_2V8        (0x0U)
#define DA7212_IO_VOLTAGE_2V5_TO_3V6        (0x1U)

/* GAIN_RAMP_CTRL (0x92) - datasheet 14.9 p107 */
#define DA7212_GAIN_RAMP_RATE_DIV8          (0x0U)
#define DA7212_GAIN_RAMP_RATE_DIV16         (0x1U)
#define DA7212_GAIN_RAMP_RATE_X16           (0x2U)
#define DA7212_GAIN_RAMP_RATE_X32           (0x3U)

/* PC_COUNT (0x94) - datasheet 14.9 (cross-check: da7213.h DA7213_PC_FREERUN_MASK) */
#define DA7212_PC_FREERUN                   (1U << 0)

/**********************************************************************************************************************
 Global Typedef definitions
 *********************************************************************************************************************/

/** DA7212 driver state */
typedef enum e_da7212_state
{
    DA7212_STATE_UNINITIALIZED = 0, /**< da7212_init() not run (or failed early) */
    DA7212_STATE_READY,             /**< configured, DAC->LINE path active */
    DA7212_STATE_ERROR,             /**< init failed; see da7212_get_last_error() */
} da7212_state_t;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Power up and configure the DA7212 for 16 kHz / 16-bit I2S playback to the
 * on-board speaker (SP_P/SP_N -> J33).
 *
 * Requires the IIC1 shared bus to be open (i2c_bus0_open_once()). Blocks for
 * roughly 0.4 s (mandatory datasheet delays). Task context only.
 *
 * @retval FSP_SUCCESS          Codec configured, DAC->LINE path enabled (muted).
 * @return                      First failing FSP error code otherwise.
 */
fsp_err_t da7212_init(void);

/**
 * Set the speaker volume.
 *
 * @param[in] percent   0 = mute, 1..100 mapped linearly in dB onto
 *                      LINE_AMP_GAIN. Values > 100 are clamped.
 * @retval FSP_SUCCESS  Volume applied.
 */
fsp_err_t da7212_set_volume(uint8_t percent);

/** @return the last value passed to da7212_set_volume() (0..100). */
uint8_t da7212_get_volume(void);

/** @return the LINE_AMP_GAIN register code currently programmed (0..63). */
uint8_t da7212_get_volume_code(void);

/**
 * Mute / unmute the DAC and the LINE (speaker) amplifier.
 *
 * @param[in] mute      true = mute, false = unmute.
 * @retval FSP_SUCCESS  Applied.
 */
fsp_err_t da7212_mute(bool mute);

/** Write one codec register (takes the IIC1 bus lock internally). */
fsp_err_t da7212_write_reg(uint8_t reg, uint8_t value);

/** Read one codec register (takes the IIC1 bus lock internally). */
fsp_err_t da7212_read_reg(uint8_t reg, uint8_t *p_value);

/** @return current driver state. */
da7212_state_t da7212_get_state(void);

/** @return the FSP error code recorded by the last failed da7212_init(). */
fsp_err_t da7212_get_last_error(void);

/** @return true when da7212_init() completed successfully. */
bool da7212_is_ready(void);

/** @return true when the DAC / LINE amplifier are currently muted. */
bool da7212_is_muted(void);

/** @return true when PLL_STATUS.PLL_LOCK was observed during da7212_init(). */
bool da7212_pll_is_locked(void);

#ifdef __cplusplus
}
#endif

#endif /* DA7212_H */
