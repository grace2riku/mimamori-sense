/**
 * @file da7212.c
 * @brief DA7212 audio CODEC driver (Issue #46)
 * @details
 * See da7212.h for the register map and the full list of sources. Every
 * register value written below carries an inline reference to the datasheet
 * section it came from.
 *
 * Clocking derivation (all board/FSP facts verified in the repository):
 *
 *   MCLK : GPT2 GTIOC2A -> PD06. g_timer_audio_mclk_cfg.period_counts = 0x14
 *          (= 20) and source_div = 0 (ra_gen/hal_data.c:257-260). The GPT
 *          count clock is PCLKD = 250 MHz (ra_gen/bsp_clock_cfg.h:13,38,56 +
 *          r_gpt.c:1735-1746), and in TIMER_MODE_PERIODIC the driver toggles
 *          GTIOCA once per timer period (r_gpt.c:1760-1790, GTIO setting
 *          "low at compare match / high at cycle end" with duty = period/2-1)
 *          => MCLK = 250 MHz / 20 = 12.5 MHz.
 *   BCLK : GPT1 GTIOC1A -> SSIE internal AUDIO_CLK, bit clock divider /1
 *          (ra_gen/hal_data.c:323-325 SSI_AUDIO_CLOCK_INTERNAL / SSI_CLOCK_DIV_1).
 *          g_timer_audio_clk_cfg.period_counts = 0x1e8 (= 488)
 *          (ra_gen/hal_data.c:131-134) => BCLK = 250 MHz / 488 = 512,295 Hz.
 *   WCLK : 16-bit PCM x 2 channels = 32 BCLK per frame
 *          (ra_gen/hal_data.c:328-331, I2S_PCM_WIDTH_16_BITS /
 *          I2S_WORD_LENGTH_16_BITS) => WCLK = 512,295 / 32 = 16,009.2 Hz.
 *
 * Clock mode choice (datasheet table 33 p50, and 13.38.1 p63):
 *   The MCU is the DAI master, so the DA7212 is a DAI SLAVE. PLL bypass mode
 *   is not usable because it requires MCLK to be exactly 12.288 MHz (or
 *   11.2896 MHz) or a multiple thereof (Note 20), and 250 MHz cannot be
 *   integer-divided to 12.288 MHz (250/12.288 = 20.35).
 *   "Slave + PLL enabled" (normal PLL mode) is allowed by table 33 with
 *   Note 21 "MCLK must be synchronous with BCLK and WCLK". That condition
 *   holds here: MCLK (GPT2) and BCLK (GPT1) are both integer divisions of the
 *   same PCLKD, so they are frequency locked with no drift.
 *   Datasheet 13.38.1 states the SRM PLL mode is required only "if the WCLK
 *   input is not from the same clock source as the MCLK input" - not our case.
 *
 * KNOWN RESIDUAL ERROR (reported as an open item for this Issue):
 *   With the PLL locked to MCLK the codec's internal system clock is exactly
 *   12.288 MHz, from which SR = 16 kHz derives exactly 16,000.0 Hz, while the
 *   arriving WCLK is 16,009.2 Hz (+0.058 %). The DA7212 DAI is slave-clocked,
 *   so the audio is consumed at the WCLK rate; the mismatch against the
 *   internal rate is ~9 samples per second. Alternatives if this proves
 *   audible: (a) enable SRM PLL mode (PLL_SRM_EN) so the PLL tracks WCLK -
 *   this needs WCLK to be running before the lock check, i.e. it cannot be
 *   done in the "codec before R_SSI_Open" order required by this Issue;
 *   (b) trim the PLL feedback divider by +0.058 %.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <tk/tkernel.h>

#include "da7212.h"
#include "i2c_bus0.h"
#include "jlink_console.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size */
#define DA7212_PRINT_BUF_SIZE           (128)

/** Event flag bits used to signal I2C completion from the FSP callback */
#define DA7212_I2C_COMPLETE             (1U << 0)
#define DA7212_I2C_ABORT                (1U << 1)

/** Timeout (ms) for a single 1..2 byte I2C transfer at 400 kHz */
#define DA7212_I2C_TIMEOUT_MS           (100)

/** MCLK applied to the codec, in Hz (see file header for the derivation). */
#define DA7212_MCLK_HZ                  (12500000UL)

/** Codec internal system clock required for the 8/12/16/24/32/48/96 kHz
 *  family (datasheet table 34, p51). */
#define DA7212_SYSCLK_HZ                (12288000UL)

/** PLL input divider actually used. 12.5 MHz falls in the "10 - 20 MHz" row
 *  of datasheet table 35 (p52) => PLL_INDIV = 01 => divide by 4, giving
 *  FREF = 3.125 MHz which is inside the required 2 - 5 MHz window (Note 23).
 *  The divide-by-4 reading of PLL_INDIV = 01 is confirmed by reproducing all
 *  six example rows of datasheet table 36 (p53) with the code below. */
#define DA7212_PLL_INDIV_SEL            DA7212_PLL_INDIV_10_20MHZ
#define DA7212_PLL_INDIV_DIV            (4UL)

/** FREF = MCLK / input divider */
#define DA7212_PLL_FREF_HZ              (DA7212_MCLK_HZ / DA7212_PLL_INDIV_DIV)

/** FVCO = 8 x system clock (datasheet 13.26.1, p52) */
#define DA7212_PLL_FVCO_HZ              (8ULL * DA7212_SYSCLK_HZ)

/** Feedback divider in 7.13 unsigned fixed point (7 integer bits +
 *  13 fractional bits split over PLL_INTEGER / PLL_FRAC_TOP / PLL_FRAC_BOT;
 *  datasheet 13.26.1 p52).
 *
 *  VALIDATION: this exact expression reproduces all six example rows of
 *  datasheet table 36 (p53) bit-for-bit -
 *    13.0 MHz -> 11.2896 MHz : 0x1B / 0x19 / 0x45
 *    13.0 MHz -> 12.288  MHz : 0x1E / 0x07 / 0xEA
 *    15.0 MHz -> 11.2896 MHz : 0x18 / 0x02 / 0xB4
 *    15.0 MHz -> 12.288  MHz : 0x1A / 0x06 / 0xDC
 *    19.2 MHz -> 11.2896 MHz : 0x12 / 0x1A / 0x1C
 *    19.2 MHz -> 12.288  MHz : 0x14 / 0x0F / 0x5C
 *  which also confirms that PLL_INDIV = 01 divides by 4 (the prose formula in
 *  table 35 is garbled in the PDF text layer, the divider column is not).
 *
 *  For MCLK = 12.5 MHz -> 12.288 MHz the result is
 *    PLL_INTEGER = 0x1F, PLL_FRAC_TOP = 0x0E, PLL_FRAC_BOT = 0xA2
 *  and PLL_CTRL = 0x84, the same PLL_CTRL value every row of table 36 uses. */
#define DA7212_PLL_FBDIV_Q13            ((uint32_t)((DA7212_PLL_FVCO_HZ * 8192ULL) / \
                                                    (uint64_t)DA7212_PLL_FREF_HZ))
#define DA7212_PLL_FBDIV_INTEGER        ((uint8_t)(DA7212_PLL_FBDIV_Q13 >> 13))
#define DA7212_PLL_FBDIV_FRAC_TOP       ((uint8_t)((DA7212_PLL_FBDIV_Q13 >> 8) & 0x1FU))
#define DA7212_PLL_FBDIV_FRAC_BOT       ((uint8_t)(DA7212_PLL_FBDIV_Q13 & 0xFFU))

/** Mandatory wait after the first I2C access before enabling the LDO
 *  (datasheet 14.9, LDO_CTRL description, p107: "wait for a minimum of
 *  40 ms after the first I2C access before enabling the LDO"). */
#define DA7212_LDO_WAIT_MS              (50)

/** VMID settling: datasheet table 24 (p26) gives 25 ms typical for
 *  "VMID > 90 % of final value" with the 1 uF capacitor fitted. */
#define DA7212_VMID_WAIT_MS             (30)

/** DAC -> SP_P/SP_N start-up time in PLL bypass or PLL normal mode
 *  (datasheet table 24, p27: 250 ms). */
#define DA7212_OUTPUT_WAIT_MS           (250)

/** Retries for the very first I2C access, which the datasheet warns "may fail
 *  because of the time taken to restart the reference oscillator"
 *  (13.40.2, p64). */
#define DA7212_WAKE_RETRIES             (5)
#define DA7212_WAKE_RETRY_MS            (5)

/** Settling delay after CIF_REG_SOFT_RESET.
 *  TODO: 要データシート確認 - Rev 3.5 does not specify a soft-reset recovery
 *  time. 10 ms is a conservative application choice, not a datasheet value. */
#define DA7212_SOFT_RESET_WAIT_MS       (10)

/** PLL lock polling budget.
 *  TODO: 要データシート確認 - Rev 3.5 does not specify a PLL lock time.
 *  200 ms is a conservative application choice. A timeout here is reported
 *  but is NOT treated as an init failure, because the codec falls back to
 *  the MCLK / internal oscillator and the rest of the path is still valid. */
#define DA7212_PLL_LOCK_TIMEOUT_MS      (200)
#define DA7212_PLL_LOCK_POLL_MS         (5)

/** Volume mapping window, expressed as LINE_AMP_GAIN register codes.
 *  This is an APPLICATION choice (not a datasheet requirement): percent 1
 *  maps to -24 dB and percent 100 to +6 dB, both inside the register's
 *  -48 dB .. +15 dB range (datasheet 14.5 LINE_GAIN, p92). Percent 0 mutes. */
#define DA7212_VOL_CODE_MIN             (0x18U)     /* 0x18 - 0x30 = -24 dB */
#define DA7212_VOL_CODE_MAX             (0x36U)     /* 0x36 - 0x30 = +6 dB  */

/** Volume applied by da7212_init() */
#define DA7212_VOL_DEFAULT_PERCENT      (60U)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/** uT-Kernel event flag signalling I2C completion (set from ISR context by
 *  audio_codec_i2c_callback, waited on by da7212_i2c_wait). 0 = not created. */
static ID s_codec_i2c_flgid = 0;

/** true once RM_COMMS_I2C_Open() succeeded for g_comms_i2c_codec. */
static bool s_codec_comms_open = false;

static da7212_state_t s_state       = DA7212_STATE_UNINITIALIZED;
static fsp_err_t      s_last_error  = FSP_SUCCESS;
static uint8_t        s_volume_pct  = DA7212_VOL_DEFAULT_PERCENT;
static uint8_t        s_volume_code = DA7212_VOL_CODE_MIN;
static bool           s_pll_locked  = false;

/** Mute state requested by the application (audio_port / NT-Shell). The
 *  EFFECTIVE mute is (s_mute_req || s_volume_pct == 0); keeping the request
 *  separate means "volume 0 then volume 50" restores audio. */
static bool           s_mute_req    = true;

/**********************************************************************************************************************
 Private (static) function prototypes
 *********************************************************************************************************************/
static fsp_err_t da7212_sync_init(void);
static fsp_err_t da7212_comms_open(void);
static fsp_err_t da7212_i2c_wait(void);
static fsp_err_t da7212_update_reg(uint8_t reg, uint8_t mask, uint8_t value);
static fsp_err_t da7212_apply_mute(void);
static uint8_t   da7212_volume_to_code(uint8_t percent);
static void      da7212_log(const char *msg);

/**********************************************************************************************************************
 FSP callback (declared by ra_gen/hal_data.h:25 and referenced by
 ra_gen/hal_data.c:14-15 - it MUST exist or the image does not link)
 *********************************************************************************************************************/

/**
 * RM_COMMS_I2C completion callback for the DA7212 device.
 *
 * @details Runs in ISR context (IIC1 TXI/RXI/TEI -> rm_comms_i2c_callback ->
 *          this function). Only sets an event flag; all decisions are made by
 *          the waiting task in da7212_i2c_wait(). Same pattern as
 *          comms_i2c_callback() for the touch panel
 *          (src/port/lv_port_indev.c) and i2c_camera_callback() for the
 *          camera (src/ov5640.c).
 */
void audio_codec_i2c_callback(rm_comms_callback_args_t *p_args)
{
    if (s_codec_i2c_flgid <= 0)
    {
        return;
    }

    if (RM_COMMS_EVENT_OPERATION_COMPLETE == p_args->event)
    {
        (void)tk_set_flg(s_codec_i2c_flgid, DA7212_I2C_COMPLETE);
    }
    else
    {
        (void)tk_set_flg(s_codec_i2c_flgid, DA7212_I2C_ABORT);
    }
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Create the uT-Kernel event flag used to signal I2C completion.
 *
 * Idempotent. Must run before RM_COMMS_I2C_Open() registers the callback that
 * signals it, which is guaranteed because da7212_comms_open() calls this
 * first (same ordering rule as touch_sync_init() in lv_port_indev.c).
 */
static fsp_err_t da7212_sync_init(void)
{
    if (s_codec_i2c_flgid > 0)
    {
        return FSP_SUCCESS;
    }

    T_CFLG cflg = {
        .exinf   = NULL,
        .flgatr  = TA_TFIFO | TA_WMUL,
        .iflgptn = 0,
    };

    ID flgid = tk_cre_flg(&cflg);
    if (flgid <= E_OK)
    {
        return FSP_ERR_INTERNAL;
    }

    s_codec_i2c_flgid = flgid;

    return FSP_SUCCESS;
}

/**
 * Open the rm_comms_i2c device for the codec (idempotent).
 */
static fsp_err_t da7212_comms_open(void)
{
    fsp_err_t err;

    if (s_codec_comms_open)
    {
        return FSP_SUCCESS;
    }

    /* Event flag first: RM_COMMS_I2C_Open() makes the callback reachable. */
    err = da7212_sync_init();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* The lower level IIC1 master must already be open. */
    err = i2c_bus0_open_once();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = RM_COMMS_I2C_Open(&g_comms_i2c_codec_ctrl, &g_comms_i2c_codec_cfg);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    s_codec_comms_open = true;

    return FSP_SUCCESS;
}

/**
 * Wait for the I2C transfer started by the caller to complete.
 */
static fsp_err_t da7212_i2c_wait(void)
{
    UINT flgptn = 0;

    ER ercd = tk_wai_flg(s_codec_i2c_flgid,
                         (UINT)(DA7212_I2C_COMPLETE | DA7212_I2C_ABORT),
                         TWF_ORW | TWF_BITCLR,
                         &flgptn,
                         (TMO)DA7212_I2C_TIMEOUT_MS);

    if (E_OK != ercd)
    {
        return FSP_ERR_TIMEOUT;
    }

    if (DA7212_I2C_COMPLETE == (flgptn & DA7212_I2C_COMPLETE))
    {
        return FSP_SUCCESS;
    }

    return FSP_ERR_ABORTED;
}

/**
 * Read-modify-write a codec register.
 */
static fsp_err_t da7212_update_reg(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t   current = 0;
    fsp_err_t err     = da7212_read_reg(reg, &current);

    if (FSP_SUCCESS != err)
    {
        return err;
    }

    uint8_t updated = (uint8_t)((current & (uint8_t)~mask) | (uint8_t)(value & mask));

    if (updated == current)
    {
        return FSP_SUCCESS;
    }

    return da7212_write_reg(reg, updated);
}

/**
 * Map a 0..100 volume percentage onto a LINE_AMP_GAIN register code.
 */
static uint8_t da7212_volume_to_code(uint8_t percent)
{
    if (percent > 100U)
    {
        percent = 100U;
    }

    if (0U == percent)
    {
        return DA7212_VOL_CODE_MIN;
    }

    uint32_t span = (uint32_t)(DA7212_VOL_CODE_MAX - DA7212_VOL_CODE_MIN);
    uint32_t code = (uint32_t)DA7212_VOL_CODE_MIN +
                    (((uint32_t)(percent - 1U) * span) + 49U) / 99U;

    if (code > DA7212_VOL_CODE_MAX)
    {
        code = DA7212_VOL_CODE_MAX;
    }

    return (uint8_t)code;
}

/**
 * Console logging helper.
 */
static void da7212_log(const char *msg)
{
    (void)print_to_console((char_t *)msg);
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Write one codec register.
 *
 * @details Two-byte transfer [register address][data], MSB first - datasheet
 *          13.32 (p56): "A two byte serial protocol is used containing one
 *          byte for address and one byte for data."
 */
fsp_err_t da7212_write_reg(uint8_t reg, uint8_t value)
{
    fsp_err_t err;
    uint8_t   buf[2];

    if (!s_codec_comms_open)
    {
        return FSP_ERR_NOT_OPEN;
    }

    buf[0] = reg;
    buf[1] = value;

    err = i2c_bus0_lock();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = RM_COMMS_I2C_Write(&g_comms_i2c_codec_ctrl, buf, sizeof(buf));
    if (FSP_SUCCESS == err)
    {
        err = da7212_i2c_wait();
    }

    (void)i2c_bus0_unlock();

    return err;
}

/**
 * Read one codec register (write address, repeated start, read one byte).
 */
fsp_err_t da7212_read_reg(uint8_t reg, uint8_t *p_value)
{
    fsp_err_t                    err;
    rm_comms_write_read_params_t params;
    uint8_t                      addr = reg;
    uint8_t                      data = 0;

    if (NULL == p_value)
    {
        return FSP_ERR_ASSERTION;
    }

    if (!s_codec_comms_open)
    {
        return FSP_ERR_NOT_OPEN;
    }

    params.p_src      = &addr;
    params.src_bytes  = 1;
    params.p_dest     = &data;
    params.dest_bytes = 1;

    err = i2c_bus0_lock();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = RM_COMMS_I2C_WriteRead(&g_comms_i2c_codec_ctrl, params);
    if (FSP_SUCCESS == err)
    {
        err = da7212_i2c_wait();
    }

    (void)i2c_bus0_unlock();

    if (FSP_SUCCESS == err)
    {
        *p_value = data;
    }

    return err;
}

/**
 * Power up and configure the DA7212 for 16 kHz / 16-bit I2S playback.
 */
fsp_err_t da7212_init(void)
{
    fsp_err_t err;
    char      buf[DA7212_PRINT_BUF_SIZE];

    if (DA7212_STATE_READY == s_state)
    {
        return FSP_SUCCESS;
    }

    s_state      = DA7212_STATE_UNINITIALIZED;
    s_last_error = FSP_SUCCESS;
    s_pll_locked = false;

    err = da7212_comms_open();
    if (FSP_SUCCESS != err)
    {
        s_state      = DA7212_STATE_ERROR;
        s_last_error = err;

        /* Distinct from the "DA7212: init failed" message at init_failed:
         * reaching here means NO codec register access was attempted at all -
         * the IIC1 shared bus could not be acquired (err 0x14 = timeout on
         * i2c_bus0_lock) or RM_COMMS_I2C_Open() failed. */
        snprintf(buf, sizeof(buf),
                 "  DA7212: bus/comms open failed (err=0x%lX) - no register access attempted.\r\n",
                 (unsigned long)err);
        da7212_log(buf);

        return err;
    }

    /* ---------------------------------------------------------------------
     * Step 1: leave standby mode.
     *
     * Datasheet 13.40.2 (p64): "It is recommended that standby mode is exited
     * by writing to the SYSTEM_ACTIVE register rather than relying on the
     * automatic assertion of the register by a read or write access", and
     * "note that the first read or write access may fail because of the time
     * taken to restart the reference oscillator". The write is therefore
     * retried; only a persistent failure aborts the init.
     * ------------------------------------------------------------------ */
    for (uint32_t attempt = 0; attempt < DA7212_WAKE_RETRIES; attempt++)
    {
        err = da7212_write_reg(DA7212_REG_SYSTEM_ACTIVE, DA7212_SYSTEM_ACTIVE);
        if (FSP_SUCCESS == err)
        {
            break;
        }
        tk_dly_tsk(DA7212_WAKE_RETRY_MS);
    }
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    /* ---------------------------------------------------------------------
     * Step 2: software reset -> known register state.
     * CIF_CTRL[7] CIF_REG_SOFT_RESET, datasheet 14.3 p76:
     * "Writing to this bit causes all the registers to reset".
     * The reset also restores SYSTEM_ACTIVE to its default, so standby is
     * left again straight afterwards.
     * ------------------------------------------------------------------ */
    err = da7212_write_reg(DA7212_REG_CIF_CTRL, DA7212_CIF_REG_SOFT_RESET);
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }
    tk_dly_tsk(DA7212_SOFT_RESET_WAIT_MS);

    err = da7212_write_reg(DA7212_REG_SYSTEM_ACTIVE, DA7212_SYSTEM_ACTIVE);
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    /* ---------------------------------------------------------------------
     * Step 3: master bias on, then (after the mandatory 40 ms) the digital
     * LDO. Order matters: datasheet 14.9 LDO_CTRL (p107) states "The master
     * bias must be enabled for the LDO to operate", and "After powering up
     * from Off or from Powerdown Mode, you must wait for a minimum of 40 ms
     * after the first I2C access before enabling the LDO. Failure to wait
     * 40 ms can cause the chip to reset."
     *
     * REFERENCES (0x23) is updated read-modify-write: bit 7 is documented as
     * reserved with a reset value of 1 (datasheet 14.3 p78), so it must be
     * preserved rather than written blind.
     *
     * VDIG (the LDO output pin) carries only decoupling capacitors on
     * EK-RA8P1 - it is not fed from a board rail (schematic p18, net
     * AUDIO_VDIG) - so the internal LDO has to be enabled.
     * ------------------------------------------------------------------ */
    err = da7212_update_reg(DA7212_REG_REFERENCES, DA7212_BIAS_EN, DA7212_BIAS_EN);
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    tk_dly_tsk(DA7212_LDO_WAIT_MS);

    err = da7212_write_reg(DA7212_REG_LDO_CTRL,
                           (uint8_t)(DA7212_LDO_EN | DA7212_LDO_LEVEL_1V05));
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    tk_dly_tsk(DA7212_VMID_WAIT_MS);

    /* ---------------------------------------------------------------------
     * Step 4: digital I/O level. EK-RA8P1 supplies VDD_IO from +3.3 V
     * (UM R20UT5309JG0104 section 6.6), and IO_CTRL resets to 0 = "1.2 to
     * 2.8 V", so it must be switched to the 2.5 - 3.6 V range
     * (datasheet 14.9 IO_CTRL, p107).
     * ------------------------------------------------------------------ */
    err = da7212_write_reg(DA7212_REG_IO_CTRL, DA7212_IO_VOLTAGE_2V5_TO_3V6);
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    /* ---------------------------------------------------------------------
     * Step 5: PLL. Feedback divider registers first, then PLL_CTRL with
     * PLL_EN + PLL_INDIV. Datasheet 13.26.1 (p52) and table 36 (p53).
     * The three divider bytes are computed from DA7212_MCLK_HZ /
     * DA7212_SYSCLK_HZ at compile time (see the macros at the top of this
     * file), so changing the GPT2 period only requires updating one constant.
     * ------------------------------------------------------------------ */
    err = da7212_write_reg(DA7212_REG_PLL_FRAC_TOP, DA7212_PLL_FBDIV_FRAC_TOP);
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    err = da7212_write_reg(DA7212_REG_PLL_FRAC_BOT, DA7212_PLL_FBDIV_FRAC_BOT);
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    err = da7212_write_reg(DA7212_REG_PLL_INTEGER, DA7212_PLL_FBDIV_INTEGER);
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    err = da7212_write_reg(DA7212_REG_PLL_CTRL,
                           (uint8_t)(DA7212_PLL_EN | DA7212_PLL_INDIV_SEL));
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    /* Poll PLL_STATUS[0] PLL_LOCK. A timeout is reported but not fatal (see
     * DA7212_PLL_LOCK_TIMEOUT_MS). */
    {
        uint32_t waited = 0;
        uint8_t  status = 0;

        while (waited < DA7212_PLL_LOCK_TIMEOUT_MS)
        {
            err = da7212_read_reg(DA7212_REG_PLL_STATUS, &status);
            if (FSP_SUCCESS != err)
            {
                goto init_failed;
            }

            if (0U != (status & DA7212_PLL_LOCK))
            {
                s_pll_locked = true;
                break;
            }

            tk_dly_tsk(DA7212_PLL_LOCK_POLL_MS);
            waited += DA7212_PLL_LOCK_POLL_MS;
        }

        if (!s_pll_locked)
        {
            snprintf(buf, sizeof(buf),
                     "  DA7212: WARNING PLL not locked (PLL_STATUS=0x%02X).\r\n",
                     (unsigned)status);
            da7212_log(buf);
        }
    }

    /* ---------------------------------------------------------------------
     * Step 6: sample rate. SR = 0101 -> 16 kHz (datasheet 14.3 SR, p77).
     * ------------------------------------------------------------------ */
    err = da7212_write_reg(DA7212_REG_SR, DA7212_SR_16000);
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    /* ---------------------------------------------------------------------
     * Step 7: DAI format.
     *   DAI_CLK_MODE (0x28): DAI_CLK_EN = 0 -> "slave mode (BCLK/WCLK
     *     inputs)"; DAI_BCLKS_PER_WCLK = 00 -> 32 BCLK per WCLK, matching the
     *     SSIE 16-bit x 2ch frame. Normal clock polarities.
     *   DAI_CTRL (0x29): DAI_EN = 1, DAI_OE = 0 (DATOUT stays high impedance
     *     because playback does not use the codec's DAI output and P406 is
     *     shared), word length 00 = 16 bits per channel, format 00 = I2S.
     * Datasheet 14.3 p79-80.
     * ------------------------------------------------------------------ */
    err = da7212_write_reg(DA7212_REG_DAI_CLK_MODE, DA7212_DAI_BCLKS_PER_WCLK_32);
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    err = da7212_write_reg(DA7212_REG_DAI_CTRL,
                           (uint8_t)(DA7212_DAI_EN |
                                     DA7212_DAI_WORD_LENGTH_16 |
                                     DA7212_DAI_FORMAT_I2S));
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    /* ---------------------------------------------------------------------
     * Step 8: digital routing DAI -> DAC. Reset value (0x32) already routes
     * DAI-left to DAC_L and DAI-right to DAC_R (datasheet 14.3 p81); it is
     * written explicitly so the state does not depend on the reset value.
     * ------------------------------------------------------------------ */
    err = da7212_write_reg(DA7212_REG_DIG_ROUTING_DAC, DA7212_DIG_ROUTING_DAC_DEFAULT);
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    /* ---------------------------------------------------------------------
     * Step 9: analogue output path DAC_R -> MIXOUT_R -> LINE amp -> SP_P/SP_N.
     *
     * The mono LINE/speaker amplifier is fed from MIXOUT_R. Datasheet 13.14
     * (p38) describes the "differential lineout channel ... mini speakers"
     * driven by LINE_AMP_EN / LINE_AMP_GAIN, and the Linux DA7213 driver -
     * which also binds "dlg,da7212" - encodes the routing explicitly as
     * {"Lineout PGA", NULL, "Mixout Right PGA"} (sound/soc/codecs/da7213.c:1190).
     * EK-RA8P1 wires SP_P/SP_N to J33-1/J33-2 (UM table 32).
     *
     * Only the NON power related bits are written here; the amplifier enable
     * bits are left to the level 2 system controller in step 11 so that the
     * pop-free start-up sequence is used (datasheet 13.39.2, p63). This split
     * follows the same convention as the Linux driver (da7213.c:1997-2030).
     * ------------------------------------------------------------------ */
    err = da7212_write_reg(DA7212_REG_MIXOUT_R_SELECT, DA7212_MIXOUT_SELECT_DAC);
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    err = da7212_update_reg(DA7212_REG_MIXOUT_R_CTRL,
                            DA7212_MIXOUT_MIX_EN, DA7212_MIXOUT_MIX_EN);
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    /* Gain ramp rate: 01 = nominal/16, the fastest ramp (datasheet 14.9
     * GAIN_RAMP_CTRL, p107). Used by the DAC / LINE ramp enables below so
     * that mute/unmute and volume changes do not click. */
    err = da7212_write_reg(DA7212_REG_GAIN_RAMP_CTRL, DA7212_GAIN_RAMP_RATE_DIV16);
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    /* Volume before the amplifier is enabled. */
    s_volume_code = da7212_volume_to_code(DA7212_VOL_DEFAULT_PERCENT);
    s_volume_pct  = DA7212_VOL_DEFAULT_PERCENT;

    err = da7212_write_reg(DA7212_REG_LINE_GAIN,
                           (uint8_t)(s_volume_code & DA7212_LINE_AMP_GAIN_MASK));
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    /* Start muted: audio_port unmutes when playback actually begins. */
    err = da7212_write_reg(DA7212_REG_LINE_CTRL,
                           (uint8_t)(DA7212_LINE_AMP_MUTE_EN |
                                     DA7212_LINE_AMP_RAMP_EN |
                                     DA7212_LINE_AMP_OE));
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    err = da7212_write_reg(DA7212_REG_DAC_R_CTRL,
                           (uint8_t)(DA7212_DAC_MUTE_EN | DA7212_DAC_RAMP_EN));
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    /* PC_COUNT free-running, matching the Linux driver default
     * (da7213.c:1977-1979, DA7213_PC_FREERUN_MASK). */
    err = da7212_update_reg(DA7212_REG_PC_COUNT, DA7212_PC_FREERUN, DA7212_PC_FREERUN);
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    /* ---------------------------------------------------------------------
     * Step 10: level 2 system controller - one-touch, pop-free activation of
     * DAC_R and the LINE amplifier. Datasheet 13.39.2 (p63): select the
     * sub-systems in SYSTEM_MODES_OUTPUT bits 1..7, then write 1 to
     * MODE_SUBMIT (self-clearing) and SCL2 performs every register write the
     * selected sub-systems need, in the correct order.
     * ------------------------------------------------------------------ */
    err = da7212_write_reg(DA7212_REG_SYSTEM_MODES_OUTPUT,
                           (uint8_t)(DA7212_SYSMODE_OUT_DAC_R |
                                     DA7212_SYSMODE_OUT_LINE |
                                     DA7212_SYSMODE_MODE_SUBMIT));
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    /* Datasheet table 24 (p27): "Any analogue input or DAC_L/R" -> SP_P/SP_N
     * takes 250 ms in PLL bypass or PLL normal mode. */
    tk_dly_tsk(DA7212_OUTPUT_WAIT_MS);

    s_mute_req = true;
    s_state    = DA7212_STATE_READY;

    snprintf(buf, sizeof(buf),
             "  DA7212: ready (MCLK %lu Hz, PLL fbdiv %u+%u/8192, SR 16 kHz).\r\n",
             (unsigned long)DA7212_MCLK_HZ,
             (unsigned)DA7212_PLL_FBDIV_INTEGER,
             (unsigned)(DA7212_PLL_FBDIV_Q13 & 0x1FFFU));
    da7212_log(buf);

    return FSP_SUCCESS;

init_failed:
    s_state      = DA7212_STATE_ERROR;
    s_last_error = err;

    snprintf(buf, sizeof(buf), "  DA7212: init failed (err=0x%lX).\r\n",
             (unsigned long)err);
    da7212_log(buf);

    return err;
}

/**
 * Apply the effective mute state (application request OR volume == 0) to the
 * DAC and the LINE amplifier.
 */
static fsp_err_t da7212_apply_mute(void)
{
    fsp_err_t err;
    bool      mute = (s_mute_req || (0U == s_volume_pct));

    uint8_t dac_val  = mute ? DA7212_DAC_MUTE_EN : 0U;
    uint8_t line_val = mute ? DA7212_LINE_AMP_MUTE_EN : 0U;

    err = da7212_update_reg(DA7212_REG_DAC_R_CTRL, DA7212_DAC_MUTE_EN, dac_val);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    return da7212_update_reg(DA7212_REG_LINE_CTRL, DA7212_LINE_AMP_MUTE_EN, line_val);
}

/**
 * Set the speaker volume through LINE_AMP_GAIN.
 */
fsp_err_t da7212_set_volume(uint8_t percent)
{
    fsp_err_t err;

    if (percent > 100U)
    {
        percent = 100U;
    }

    if (DA7212_STATE_READY != s_state)
    {
        /* Remember the request so it is applied by the next successful init. */
        s_volume_pct  = percent;
        s_volume_code = da7212_volume_to_code(percent);
        return FSP_ERR_NOT_OPEN;
    }

    uint8_t code = da7212_volume_to_code(percent);

    err = da7212_write_reg(DA7212_REG_LINE_GAIN,
                           (uint8_t)(code & DA7212_LINE_AMP_GAIN_MASK));
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    s_volume_pct  = percent;
    s_volume_code = code;

    /* Percent 0 means silence: the lowest gain code is still audible, so the
     * amplifier mute has to follow the volume. */
    return da7212_apply_mute();
}

uint8_t da7212_get_volume(void)
{
    return s_volume_pct;
}

uint8_t da7212_get_volume_code(void)
{
    return s_volume_code;
}

/**
 * Mute / unmute the DAC and the LINE amplifier.
 */
fsp_err_t da7212_mute(bool mute)
{
    if (DA7212_STATE_READY != s_state)
    {
        s_mute_req = mute;
        return FSP_ERR_NOT_OPEN;
    }

    s_mute_req = mute;

    return da7212_apply_mute();
}

da7212_state_t da7212_get_state(void)
{
    return s_state;
}

fsp_err_t da7212_get_last_error(void)
{
    return s_last_error;
}

bool da7212_is_ready(void)
{
    return (DA7212_STATE_READY == s_state);
}

bool da7212_is_muted(void)
{
    return (s_mute_req || (0U == s_volume_pct));
}

bool da7212_pll_is_locked(void)
{
    return s_pll_locked;
}
