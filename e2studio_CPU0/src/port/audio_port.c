/**
 * @file audio_port.c
 * @brief Audio output device layer: SSIE0 (I2S master) -> DA7212 -> J33 speaker
 * @details See audio_port.h for the module contract.
 *
 * Ping-pong (double) buffering
 * ----------------------------
 * R_SSI_Write() hands one whole buffer to the DTC: it calls
 * transfer_api_t::reset with num_blocks = samples/2
 * (ra/fsp/src/r_ssi/r_ssi.c:822-850). The DTC transfer info is configured
 * with TRANSFER_IRQ_END (ra_gen/hal_data.c:286), which the transfer API
 * documents as "DTC triggers the interrupt of the activation source. Choosing
 * TRANSFER_IRQ_END with DTC will prevent activation source interrupts until
 * the transfer is complete" (ra/fsp/inc/api/r_transfer_api.h:159-163). The
 * SSI0 TXI request is therefore consumed by the DTC without reaching the CPU
 * until the LAST block has been moved, so ssi_txi_isr() runs once per buffer
 * (r_ssi.c:1170-1195). At that point p_tx_src is NULL and tx_src_samples is 0
 * (set by r_ssi_tx_load_fifo, r_ssi.c:843-848), so the ISR reports
 * I2S_EVENT_TX_EMPTY and the buffer that was just drained is free for reuse.
 *
 * The callback therefore:
 *   I2S_EVENT_TX_EMPTY -> submit the other buffer, then refill the freed one
 *   I2S_EVENT_IDLE     -> playback/stop is complete (r_i2s_api.h:172,186-187)
 *
 * Cache
 * -----
 * The DTC reads the PCM buffers from SRAM. No cache maintenance is emitted
 * because BSP_CFG_DCACHE_ENABLED is 0 for CPU0 - a COMPILE-time constant
 * (ra_cfg/fsp_cfg/bsp/bsp_mcu_family_cfg.h:427-433) - which is also why the
 * rest of this project only does cache maintenance behind the same guard
 * (e.g. src/port/lvgl_port_mtk3.c:259-261,
 * src/port/dave2d_cache_management.c:61-62,83-84). The guard below keeps the
 * code correct if the D-cache is ever enabled.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <tk/tkernel.h>

#include "audio_port.h"
#include "da7212.h"
#include "i2c_bus0.h"
#include "jlink_console.h"
#include "cmd_utils.h"
#include "ntlibc.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size */
#define AUDIO_PRINT_BUF_SIZE        (144)

/** Event flag bit set from the SSI callback when I2S_EVENT_IDLE arrives */
#define AUDIO_EVT_IDLE              (1U << 0)

/** Timeout for audio_stop(): the SSI stops on the next frame boundary and the
 *  DTC still has at most one queued buffer, so 2 buffers + margin. */
#define AUDIO_STOP_TIMEOUT_MS       ((AUDIO_BUFFER_MS * 2U) + 50U)

/** Attempts (not retries) at unmuting the codec in audio_start(), and the gap
 *  between them. One transient I2C hiccup must not abort playback, but a
 *  persistent failure must not be reported as success either. */
#define AUDIO_UNMUTE_ATTEMPTS       (3U)
#define AUDIO_UNMUTE_RETRY_MS       (5)

/** Default built-in test tone (Hz) */
#define AUDIO_TEST_TONE_DEFAULT_HZ  (1000U)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/**
 * Ping-pong PCM buffers, interleaved L,R int16.
 *
 * 4-byte aligned because R_SSI_Write() documents "p_src ... Must be 4 byte
 * aligned" (ra/fsp/inc/api/r_i2s_api.h:190) and the DTC source address is
 * incremented from it.
 */
static int16_t s_pcm_buf[AUDIO_BUFFER_COUNT][AUDIO_BUFFER_SAMPLES] BSP_ALIGN_VARIABLE(4);

/** Index of the buffer currently handed to the DTC. */
static volatile uint32_t s_play_idx = 0;

/** Set by audio_stop(), consumed by the ISR on the next I2S_EVENT_TX_EMPTY. */
static volatile bool s_stop_request = false;

/** Device state. Written by task context and by the ISR (single word). */
static volatile audio_state_t s_state = AUDIO_STATE_UNINITIALIZED;

/** Last error recorded outside of a function return path. */
static volatile fsp_err_t s_last_error = FSP_SUCCESS;

/** Diagnostics */
static volatile uint32_t s_tx_empty_count = 0;
static volatile uint32_t s_idle_count     = 0;
static volatile uint32_t s_error_count    = 0;

/**
 * Number of times the stream was restarted from an UNSOLICITED I2S_EVENT_IDLE.
 *
 * An idle event that arrives while the state is still AUDIO_STATE_PLAYING and
 * no stop was requested means the SSI ran dry: ssi_int_isr() takes the error
 * branch on a transmit underflow, calls r_ssi_stop_sub() and returns WITHOUT
 * invoking any callback (ra/fsp/src/r_ssi/r_ssi.c:1263-1275); the peripheral
 * then goes idle and only that idle reaches us. The same ISR clears every
 * SSISR flag on entry (r_ssi.c:1252), so TUIRQ can no longer be read back -
 * this counter is the only underflow evidence available to the application.
 *
 * Root cause of the dry-out: after the DTC finishes a buffer the only slack
 * left is the 32-stage transmit FIFO (~1 ms at fs 16 kHz), and the SSI TX
 * interrupt is IPL 2 while uT-Kernel's disint() raises BASEPRI to
 * INTPRI_VAL(INTPRI_MAX_EXTINT_PRI) with INTPRI_MAX_EXTINT_PRI == 1
 * (mtk3_bsp2/include/sys/sysdepend/ra_fsp/cpu/ra8p1/sysdef.h:76,
 * mtk3_bsp2/mtkernel/lib/libtk/sysdepend/cpu/core/armv7m/int_armv7m.c:56-66),
 * i.e. every kernel critical section masks this ISR.
 */
static volatile uint32_t s_restart_count = 0;

/**
 * Wall-clock stamp (ms, tk_get_otm) taken by audio_start(), plus the counter
 * values at that moment. audio_cmd_status() turns the deltas into a MEASURED
 * buffer rate, which is the only way to check the real bit clock from software:
 * every value printed under "Clocks" is computed from the configured GPT period,
 * not observed. One tx_empty is one AUDIO_BUFFER_FRAMES buffer, so a healthy
 * stream must show 1000 / AUDIO_BUFFER_MS = 100 buffers per second.
 */
static volatile uint32_t s_start_ms       = 0;
static volatile uint32_t s_start_tx_empty = 0;

/** uT-Kernel event flag used to wait for I2S_EVENT_IDLE. 0 = not created. */
static ID s_audio_flgid = 0;

/** Buffer producer installed by audio_start(). */
static audio_fill_cb_t s_fill_cb      = NULL;
static void           *s_fill_context = NULL;

/** Built-in test tone state (used when audio_start() is called with NULL). */
static uint32_t s_tone_freq_hz  = AUDIO_TEST_TONE_DEFAULT_HZ;
static uint16_t s_tone_amplitude = AUDIO_TEST_AMPLITUDE;
static volatile uint32_t s_tone_phase = 0;
static uint32_t s_tone_step  = 0;

/** true once R_SSI_Open()/R_GPT_Open() succeeded (so re-init is idempotent). */
static bool s_ssi_open  = false;
static bool s_gpt_open  = false;

/**********************************************************************************************************************
 Private (static) function prototypes
 *********************************************************************************************************************/
static fsp_err_t audio_sync_init(void);
static void      audio_fill_buffer(uint32_t index);
static void      audio_tone_fill(int16_t *p_frames, uint32_t frame_count, void *p_context);
static void      audio_tone_update_step(void);
static void      audio_log(const char *msg);
static void      audio_cmd_status(void);
static void      audio_cmd_usage(void);

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Create the uT-Kernel event flag used to wait for I2S_EVENT_IDLE.
 *
 * Idempotent. Called at the very beginning of audio_init(), i.e. before
 * R_SSI_Open() makes audio_i2s_callback() reachable, so the ISR can never
 * observe an uncreated flag. audio_i2s_callback() additionally checks the ID.
 */
static fsp_err_t audio_sync_init(void)
{
    if (s_audio_flgid > 0)
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

    s_audio_flgid = flgid;

    return FSP_SUCCESS;
}

/**
 * Recompute the phase increment of the built-in test tone.
 *
 * Q32 phase accumulator: step = freq * 2^32 / fs.
 */
static void audio_tone_update_step(void)
{
    uint64_t step = ((uint64_t)s_tone_freq_hz << 32) / (uint64_t)AUDIO_SAMPLE_RATE_HZ;

    s_tone_step = (uint32_t)step;
}

/**
 * Built-in test tone producer (square wave).
 *
 * Deliberately minimal: the sine LUT, the alarm patterns and the envelope
 * belong to Issue #47 (S-005-3). This exists only so that Issue #46 can be
 * verified on hardware.
 */
static void audio_tone_fill(int16_t *p_frames, uint32_t frame_count, void *p_context)
{
    FSP_PARAMETER_NOT_USED(p_context);

    uint32_t phase = s_tone_phase;
    int16_t  hi    = (int16_t)s_tone_amplitude;
    int16_t  lo    = (int16_t)(-(int32_t)s_tone_amplitude);

    for (uint32_t i = 0; i < frame_count; i++)
    {
        int16_t sample = (0U != (phase & 0x80000000U)) ? hi : lo;

        p_frames[(i * AUDIO_CHANNELS) + 0] = sample;    /* left  */
        p_frames[(i * AUDIO_CHANNELS) + 1] = sample;    /* right -> MIXOUT_R -> LINE amp */

        phase += s_tone_step;
    }

    s_tone_phase = phase;
}

/**
 * Fill one ping-pong buffer through the installed producer.
 *
 * Called from the SSI ISR (and from audio_start() before playback begins).
 */
static void audio_fill_buffer(uint32_t index)
{
    audio_fill_cb_t cb = s_fill_cb;

    if (NULL != cb)
    {
        cb(&s_pcm_buf[index][0], AUDIO_BUFFER_FRAMES, s_fill_context);
    }
    else
    {
        memset(&s_pcm_buf[index][0], 0, AUDIO_BUFFER_BYTES);
    }

#if BSP_CFG_DCACHE_ENABLED
    /* Make the freshly produced samples visible to the DTC. Compiled out in
     * this build (BSP_CFG_DCACHE_ENABLED == 0, see the file header). */
    SCB_CleanDCache_by_Addr((uint32_t *)&s_pcm_buf[index][0], (int32_t)AUDIO_BUFFER_BYTES);
#endif
}

/**
 * Console logging helper.
 */
static void audio_log(const char *msg)
{
    (void)print_to_console((char_t *)msg);
}

/**********************************************************************************************************************
 FSP callback (declared by ra_gen/hal_data.h:61 and referenced by
 ra_gen/hal_data.c:331 - it MUST exist or the image does not link)
 *********************************************************************************************************************/

/**
 * SSI transmit / idle callback.
 *
 * @details ISR context (SSI0 TXI and SSI0 INT, both at priority 2 -
 *          ra_gen/hal_data.c:348). Keeps the stream running by submitting the
 *          other ping-pong buffer, then regenerating the buffer that was just
 *          drained.
 */
void audio_i2s_callback(i2s_callback_args_t *p_args)
{
    fsp_err_t err;

    switch (p_args->event)
    {
        case I2S_EVENT_TX_EMPTY:
        {
            s_tx_empty_count++;

            if (s_stop_request)
            {
                /* Stop completes asynchronously with I2S_EVENT_IDLE
                 * (ra/fsp/inc/api/r_i2s_api.h:172). */
                err = R_SSI_Stop(&g_i2s_audio_ctrl);
                if (FSP_SUCCESS != err)
                {
                    s_last_error = err;
                    s_error_count++;
                }
                break;
            }

            uint32_t next = s_play_idx ^ 1U;

            err = R_SSI_Write(&g_i2s_audio_ctrl, &s_pcm_buf[next][0], AUDIO_BUFFER_BYTES);
            if (FSP_SUCCESS != err)
            {
                /* FSP_ERR_UNDERFLOW means the FIFO ran dry before we got here
                 * (r_ssi.c:354); the SSI must go idle before it can be
                 * restarted, so stop and report. */
                s_last_error = err;
                s_error_count++;
                s_stop_request = true;
                (void)R_SSI_Stop(&g_i2s_audio_ctrl);
                break;
            }

            uint32_t freed = s_play_idx;
            s_play_idx = next;

            audio_fill_buffer(freed);
            break;
        }

        case I2S_EVENT_IDLE:
        {
            s_idle_count++;

            /*
             * UNSOLICITED idle: still PLAYING and nobody asked to stop, so the
             * SSI ran dry (see s_restart_count). Re-prime both buffers and
             * resubmit instead of falling silent - a dropped alarm tone is
             * worse than the click this causes. R_SSI_Write() from the
             * callback is the documented pattern (r_ssi.c:321).
             */
            if ((AUDIO_STATE_PLAYING == s_state) && (!s_stop_request))
            {
                s_restart_count++;

                audio_fill_buffer(0);
                audio_fill_buffer(1);
                s_play_idx = 0;

                err = R_SSI_Write(&g_i2s_audio_ctrl, &s_pcm_buf[0][0], AUDIO_BUFFER_BYTES);
                if (FSP_SUCCESS == err)
                {
                    break;              /* stream is running again */
                }

                /* Could not restart - fall through to the stop handling so the
                 * device does not stay wedged in PLAYING. */
                s_last_error = err;
                s_error_count++;
            }

            s_stop_request = false;

            /* The stop is now complete, so the device really is idle.
             *
             * PLAYING  : unsolicited idle that could not be restarted.
             * STOPPING : audio_stop() is either still waiting (it sets READY
             *            itself when the wait returns - harmless duplicate) or
             *            it already TIMED OUT and deliberately left the state
             *            at STOPPING. Clearing it here is what recovers the
             *            device in that case; r_ssi_stop_sub() re-enables the
             *            idle interrupt, so this callback is guaranteed to
             *            run. */
            if ((AUDIO_STATE_PLAYING == s_state) || (AUDIO_STATE_STOPPING == s_state))
            {
                s_state = AUDIO_STATE_READY;
            }

            if (s_audio_flgid > 0)
            {
                (void)tk_set_flg(s_audio_flgid, AUDIO_EVT_IDLE);
            }
            break;
        }

        default:
        {
            /* I2S_EVENT_RX_FULL cannot occur: the receive interrupt is
             * disabled (ra_gen/hal_data.c:338-341, rxi_irq =
             * FSP_INVALID_VECTOR). */
            break;
        }
    }
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Bring up the audio output device.
 */
fsp_err_t audio_init(void)
{
    fsp_err_t err;
    char      buf[AUDIO_PRINT_BUF_SIZE];

    /* ---------------------------------------------------------------------
     * Claim the initialisation atomically.
     *
     * audio_task runs audio_init() at boot and the shell can run it again via
     * `audio init`, so two TASKS can enter here. Testing only for READY /
     * PLAYING is not enough: the state stays UNINITIALIZED for the whole
     * roughly 0.4 s power-up sequence, so both callers would pass the check
     * and interleave the DA7212 soft reset, LDO/PLL bring-up and register
     * writes. The IIC1 bus mutex does NOT prevent that - it serialises single
     * transfers, not this multi-step sequence - and one caller could return
     * success and start playback while the other is still muting the codec.
     *
     * tk_dis_dsp() is enough to make the test-and-set atomic: the only other
     * writer of s_state is the SSI ISR, and it only ever moves PLAYING ->
     * READY (audio_i2s_callback), never into or out of INITIALIZING.
     * ------------------------------------------------------------------ */
    if (E_OK != tk_dis_dsp())
    {
        return FSP_ERR_INTERNAL;
    }

    if ((AUDIO_STATE_READY == s_state) ||
        (AUDIO_STATE_PLAYING == s_state) ||
        (AUDIO_STATE_STOPPING == s_state))
    {
        (void)tk_ena_dsp();
        return FSP_SUCCESS;
    }

    if (AUDIO_STATE_INITIALIZING == s_state)
    {
        (void)tk_ena_dsp();
        return FSP_ERR_IN_USE;
    }

    s_state = AUDIO_STATE_INITIALIZING;
    (void)tk_ena_dsp();

    /* Every exit below is terminal: the success path sets READY and
     * init_failed: sets ERROR, so INITIALIZING is never left behind. */

    /* Event flag before anything can call audio_i2s_callback(). */
    err = audio_sync_init();
    if (FSP_SUCCESS != err)
    {
        s_last_error = err;
        s_state      = AUDIO_STATE_ERROR;
        return err;
    }

    audio_tone_update_step();

    if (!s_gpt_open)
    {
        /* -----------------------------------------------------------------
         * Step 1: GPT2. One timer, two consumers:
         *   - GTIOC2A -> PD06 = MCLK for the DA7212
         *   - internal connection = SSIE0 AUDIO_CLK (see audio_port.h)
         *
         * FIRST, because the codec's PLL locks to MCLK and step 2 writes the
         * PLL registers, and because r_ssi only sets SSICR.CKS for
         * SSI_AUDIO_CLOCK_INTERNAL (ra/fsp/src/r_ssi/r_ssi.c:259-266) and
         * contains no GPT code at all, so nothing else starts the audio clock.
         *
         * The pin is already configured as the GPT peripheral output
         * (ra_gen/pin_data.c:629-630) and the driver enables the output
         * because gtioca.output_enabled is true (ra_gen/hal_data.c:176-177)
         * which makes r_gpt.c:1412-1421 compute a GTIOR with OAE set
         * (r_gpt.c:1760-1764).
         * -------------------------------------------------------------- */
        err = R_GPT_Open(&g_timer_audio_mclk_ctrl, &g_timer_audio_mclk_cfg);
        if ((FSP_SUCCESS != err) && (FSP_ERR_ALREADY_OPEN != err))
        {
            goto init_failed;
        }

        err = R_GPT_Start(&g_timer_audio_mclk_ctrl);
        if (FSP_SUCCESS != err)
        {
            goto init_failed;
        }

        s_gpt_open = true;
    }

    /* ---------------------------------------------------------------------
     * Step 2: DA7212 over I2C. Uses the IIC1 shared bus lock internally and
     * waits until the camera has released IIC1 (i2c_bus0_open_once()).
     * ------------------------------------------------------------------ */
    err = da7212_init();
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    /* ---------------------------------------------------------------------
     * Step 3: SSIE0. R_SSI_Open() also opens the DTC transmit instance
     * (r_ssi.c:713-720), so g_transfer_i2s_tx must never be opened here.
     *
     * SSICR.CKDV comes straight from the generated configuration since Issue
     * #202 set "Bit Clock Divider" to Audio Clock / 24 (SSI_CLOCK_DIV_24 in
     * ra_gen/hal_data.c, applied by r_ssi.c:259-266). The stop-gap that used
     * to overwrite CKDV here is gone; `audio status` prints the live SSICR so
     * the field can still be checked on hardware.
     * ------------------------------------------------------------------ */
    if (!s_ssi_open)
    {
        err = R_SSI_Open(&g_i2s_audio_ctrl, &g_i2s_audio_cfg);
        if ((FSP_SUCCESS != err) && (FSP_ERR_ALREADY_OPEN != err))
        {
            goto init_failed;
        }

        s_ssi_open = true;
    }

    s_stop_request = false;
    s_play_idx     = 0;
    s_state        = AUDIO_STATE_READY;
    s_last_error   = FSP_SUCCESS;

    /* Apply the volume that may have been requested before the codec was up. */
    (void)da7212_set_volume(da7212_get_volume());

    snprintf(buf, sizeof(buf),
             "  audio: ready. fs=%lu Hz BCLK=%lu Hz MCLK=%lu Hz buf=%lu frames (%u ms) x2\r\n",
             (unsigned long)AUDIO_SAMPLE_RATE_HZ,
             (unsigned long)AUDIO_BCLK_HZ,
             (unsigned long)AUDIO_MCLK_HZ,
             (unsigned long)AUDIO_BUFFER_FRAMES,
             (unsigned)AUDIO_BUFFER_MS);
    audio_log(buf);

    return FSP_SUCCESS;

init_failed:
    s_last_error = err;
    s_state      = AUDIO_STATE_ERROR;

    snprintf(buf, sizeof(buf), "  audio: init failed (err=0x%lX).\r\n",
             (unsigned long)err);
    audio_log(buf);

    return err;
}

/**
 * Start continuous playback.
 */
fsp_err_t audio_start(audio_fill_cb_t p_fill, void *p_context)
{
    fsp_err_t err;

    if (AUDIO_STATE_READY != s_state)
    {
        /* STOPPING and INITIALIZING are "busy", not "not opened": the device
         * exists and will become usable shortly. STOPPING in particular means
         * the SSI has not reached idle yet, and r_ssi_start() would reject the
         * restart with FSP_ERR_IN_USE anyway (it requires SSISR.IIRQ == 1). */
        if ((AUDIO_STATE_PLAYING == s_state) ||
            (AUDIO_STATE_STOPPING == s_state) ||
            (AUDIO_STATE_INITIALIZING == s_state))
        {
            return FSP_ERR_IN_USE;
        }
        return FSP_ERR_NOT_OPEN;
    }

    s_fill_cb      = (NULL != p_fill) ? p_fill : audio_tone_fill;
    s_fill_context = p_context;

    if (audio_tone_fill == s_fill_cb)
    {
        s_tone_phase = 0;
        audio_tone_update_step();
    }

    /* Pre-fill both buffers before any of them is handed to the DTC. */
    audio_fill_buffer(0);
    audio_fill_buffer(1);

    s_play_idx     = 0;
    s_stop_request = false;
    s_state        = AUDIO_STATE_PLAYING;

    /* Baseline for the measured buffer rate reported by audio_cmd_status(). */
    {
        SYSTIM now = {0, 0};
        (void)tk_get_otm(&now);
        s_start_ms       = (uint32_t)now.lo;
        s_start_tx_empty = s_tx_empty_count;
    }

    err = R_SSI_Write(&g_i2s_audio_ctrl, &s_pcm_buf[0][0], AUDIO_BUFFER_BYTES);
    if (FSP_SUCCESS != err)
    {
        s_state      = AUDIO_STATE_READY;
        s_last_error = err;
        return err;
    }

    /* Unmute only after the stream is running so the first samples are not a
     * step from silence. LINE_AMP_RAMP_EN / DAC_R_RAMP_EN make the unmute
     * ramp instead of click (DA7212 datasheet 13.14, p38). */
    /*
     * The unmute must NOT be best-effort.
     *
     * da7212_apply_mute() writes DAC_R_CTRL and LINE_CTRL in two SEPARATE I2C
     * transactions, so a failure can leave the codec half-unmuted. Returning
     * FSP_SUCCESS then leaves the stream in AUDIO_STATE_PLAYING and tells the
     * shell "tone playing" while the speaker stays silent, with nothing
     * retrying - exactly the "reports success but no sound" failure mode this
     * driver already cost a long bring-up to diagnose.
     *
     * Retry a few times for a transient bus hiccup, then roll the stream back
     * so the device is not left PLAYING while inaudible.
     */
    for (uint32_t attempt = 0; attempt < AUDIO_UNMUTE_ATTEMPTS; attempt++)
    {
        err = da7212_mute(false);
        if (FSP_SUCCESS == err)
        {
            return FSP_SUCCESS;
        }

        if ((attempt + 1U) < AUDIO_UNMUTE_ATTEMPTS)
        {
            tk_dly_tsk(AUDIO_UNMUTE_RETRY_MS);
        }
    }

    s_last_error = err;
    (void)audio_stop();

    return err;
}

/**
 * Stop playback.
 */
fsp_err_t audio_stop(void)
{
    fsp_err_t err;

    if ((AUDIO_STATE_PLAYING != s_state) && (AUDIO_STATE_STOPPING != s_state))
    {
        return FSP_SUCCESS;
    }

    /* Immediate silence: the codec mute is applied within a few hundred
     * microseconds over I2C, long before the SSI reaches its next frame
     * boundary. */
    (void)da7212_mute(true);

    /* Drop any AUDIO_EVT_IDLE left over from an earlier unsolicited idle so
     * the wait below cannot be satisfied by a stale bit.
     * tk_clr_flg() ANDs the pattern with the current bits. */
    (void)tk_clr_flg(s_audio_flgid, (UINT)~AUDIO_EVT_IDLE);

    /* State first, then the request: the ISR's I2S_EVENT_IDLE handler only
     * forces READY while the state is still PLAYING, so this ordering
     * guarantees the ISR never races the assignment below. */
    s_state        = AUDIO_STATE_STOPPING;
    s_stop_request = true;

    UINT flgptn = 0;
    ER   ercd   = tk_wai_flg(s_audio_flgid,
                             (UINT)AUDIO_EVT_IDLE,
                             TWF_ORW | TWF_BITCLR,
                             &flgptn,
                             (TMO)AUDIO_STOP_TIMEOUT_MS);

    if (E_OK != ercd)
    {
        /* No I2S_EVENT_IDLE: force the stop from task context so the device
         * does not stay half-running. */
        err = R_SSI_Stop(&g_i2s_audio_ctrl);
        if (FSP_SUCCESS != err)
        {
            s_last_error = err;
        }

        /* Stay STOPPING - do NOT advertise READY here.
         *
         * R_SSI_Stop() only REQUESTS the stop; it completes later through
         * I2S_EVENT_IDLE ("Stop is complete after an I2S_EVENT_IDLE
         * interrupt", r_ssi.c R_SSI_Stop). Reporting READY would let a caller
         * run audio_start() while the peripheral is still stopping, and
         * r_ssi_start() rejects exactly that with FSP_ERR_IN_USE because it
         * requires SSISR.IIRQ == 1 before setting TEN.
         *
         * This does not wedge the device: r_ssi_stop_sub() explicitly
         * re-enables the idle interrupt (`ssicr |= 1 << SSI_PRV_SSICR_IIEN_BIT`),
         * so the idle WILL arrive, and audio_i2s_callback() moves
         * STOPPING -> READY when it does. Until then `audio status` honestly
         * shows STOPPING. s_stop_request stays true so the ISR treats the
         * late idle as a requested stop and does not auto-restart the stream. */
        return FSP_ERR_TIMEOUT;
    }

    s_state = AUDIO_STATE_READY;

    return FSP_SUCCESS;
}

/**
 * Set the hardware volume.
 */
fsp_err_t audio_set_volume(uint8_t percent)
{
    return da7212_set_volume(percent);
}

uint8_t audio_get_volume(void)
{
    return da7212_get_volume();
}

/**
 * Configure the built-in test tone.
 */
fsp_err_t audio_set_test_tone(uint32_t freq_hz, uint16_t amplitude)
{
    /* Nyquist: refuse anything the 16 kHz stream cannot represent. */
    if ((0U == freq_hz) || (freq_hz >= (AUDIO_SAMPLE_RATE_HZ / 2U)))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    s_tone_freq_hz   = freq_hz;
    s_tone_amplitude = amplitude;
    audio_tone_update_step();

    return FSP_SUCCESS;
}

audio_state_t audio_get_state(void)
{
    return s_state;
}

uint32_t audio_get_sample_rate(void)
{
    return (uint32_t)AUDIO_SAMPLE_RATE_HZ;
}

/**
 * uT-Kernel task entry.
 *
 * Runs audio_init() once and then sleeps forever. A dedicated task is used
 * (rather than initialising from ntshell_task) because audio_init() blocks
 * for ~0.4 s in the mandatory DA7212 power-up delays and may additionally
 * wait for the camera to release IIC1.
 */
void audio_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    audio_log("[audio_task] initialising audio output (SSIE0 -> DA7212 -> J33).\r\n");

    (void)audio_init();

    tk_slp_tsk(TMO_FEVR);
}

/**********************************************************************************************************************
 NT-Shell command implementation
 *********************************************************************************************************************/

static void audio_cmd_usage(void)
{
    cmd_print_usage("audio", "<subcommand>");
    print_to_console("  status              - Show audio device / codec state\r\n");
    print_to_console("  init                - Retry device initialisation\r\n");
    print_to_console("  start               - Start the test tone\r\n");
    print_to_console("  stop                - Stop playback\r\n");
    print_to_console("  tone <hz> [ampl]    - Set test tone frequency / amplitude\r\n");
    print_to_console("  volume [0-100]      - Show or set the speaker volume\r\n");
    print_to_console("  mute <on|off>       - Mute or unmute the codec\r\n");
    print_to_console("  reg <addr> [value]  - Read or write a DA7212 register\r\n");
}

static void audio_cmd_status(void)
{
    char        buf[AUDIO_PRINT_BUF_SIZE];
    const char *state_str;

    switch (s_state)
    {
        case AUDIO_STATE_INITIALIZING: state_str = "INITIALIZING"; break;
        case AUDIO_STATE_READY:    state_str = "READY";    break;
        case AUDIO_STATE_PLAYING:  state_str = "PLAYING";  break;
        case AUDIO_STATE_STOPPING: state_str = "STOPPING"; break;
        case AUDIO_STATE_ERROR:    state_str = "ERROR";    break;
        default:                   state_str = "UNINITIALIZED"; break;
    }

    print_to_console("Audio output (SSIE0 I2S master -> DA7212 -> J33)\r\n");

    snprintf(buf, sizeof(buf), "  State        : %s (last err=0x%lX)\r\n",
             state_str, (unsigned long)s_last_error);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Format       : %lu Hz, %u bit, %u ch (I2S frame)\r\n",
             (unsigned long)AUDIO_SAMPLE_RATE_HZ,
             (unsigned)AUDIO_SAMPLE_BITS,
             (unsigned)AUDIO_CHANNELS);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Clocks       : MCLK %lu Hz, BCLK %lu Hz, WCLK %lu Hz\r\n",
             (unsigned long)AUDIO_MCLK_HZ,
             (unsigned long)AUDIO_BCLK_HZ,
             (unsigned long)AUDIO_SAMPLE_RATE_HZ);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Buffers      : 2 x %lu frames (%u ms, %lu bytes)\r\n",
             (unsigned long)AUDIO_BUFFER_FRAMES,
             (unsigned)AUDIO_BUFFER_MS,
             (unsigned long)AUDIO_BUFFER_BYTES);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  ISR counters : tx_empty=%lu idle=%lu err=%lu restart=%lu\r\n",
             (unsigned long)s_tx_empty_count,
             (unsigned long)s_idle_count,
             (unsigned long)s_error_count,
             (unsigned long)s_restart_count);
    print_to_console(buf);

    /* Live SSI hardware state - TEN tells whether the transmitter is actually
     * running right now, independently of the software state above.
     * SSICR bit 1 = TEN, bit 3 = MUEN (R7KA8P1KF_core0.h SSICR_b). */
    {
        uint32_t ssicr = R_SSI0->SSICR;
        uint32_t ssisr = R_SSI0->SSISR;

        snprintf(buf, sizeof(buf),
                 "  SSI regs     : SSICR=0x%08lX (TEN=%lu MUEN=%lu) SSISR=0x%08lX\r\n",
                 (unsigned long)ssicr,
                 (unsigned long)((ssicr >> 1) & 1U),
                 (unsigned long)((ssicr >> 3) & 1U),
                 (unsigned long)ssisr);
        print_to_console(buf);
    }

    /*
     * MEASURED buffer rate since audio_start(). Everything under "Clocks"
     * above is derived from the configured GPT period; this line is the only
     * observed figure. Expected: 100 buf/s (AUDIO_BUFFER_MS = 10) and a
     * measured fs close to AUDIO_SAMPLE_RATE_HZ. A large discrepancy means the
     * real bit clock is not what the configuration says.
     */
    if ((0U != s_start_ms) && (AUDIO_STATE_PLAYING == s_state))
    {
        SYSTIM now = {0, 0};
        (void)tk_get_otm(&now);

        uint32_t elapsed_ms = (uint32_t)now.lo - s_start_ms;
        uint32_t bufs       = s_tx_empty_count - s_start_tx_empty;

        if (elapsed_ms >= 100U)
        {
            uint32_t bufs_per_s = (uint32_t)(((uint64_t)bufs * 1000U) / elapsed_ms);

            snprintf(buf, sizeof(buf),
                     "  Measured     : %lu buf in %lu ms = %lu buf/s (expect %lu), fs~%lu Hz\r\n",
                     (unsigned long)bufs,
                     (unsigned long)elapsed_ms,
                     (unsigned long)bufs_per_s,
                     (unsigned long)(1000U / AUDIO_BUFFER_MS),
                     (unsigned long)(bufs_per_s * AUDIO_BUFFER_FRAMES));
            print_to_console(buf);
        }
    }

    snprintf(buf, sizeof(buf), "  Test tone    : %lu Hz, amplitude %u (square)\r\n",
             (unsigned long)s_tone_freq_hz, (unsigned)s_tone_amplitude);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  I2C bus0     : %s\r\n",
             i2c_bus0_is_ready() ? "open" : "not open");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  DA7212       : %s, PLL %s, volume %u%% (LINE_AMP_GAIN=0x%02X), %s\r\n",
             da7212_is_ready() ? "ready" : "not ready",
             da7212_pll_is_locked() ? "locked" : "UNLOCKED",
             (unsigned)da7212_get_volume(),
             (unsigned)da7212_get_volume_code(),
             da7212_is_muted() ? "muted" : "unmuted");
    print_to_console(buf);
}

/**
 * NT-Shell "audio" command handler.
 *
 * @note Runs in ntshell_task context. Playback is asynchronous (ISR driven),
 *       so "audio start" returns immediately and the shell stays responsive.
 */
int usrcmd_audio(int argc, char **argv)
{
    char      buf[AUDIO_PRINT_BUF_SIZE];
    fsp_err_t err;

    if (argc < 2)
    {
        audio_cmd_usage();
        return CMD_ERR_USAGE;
    }

    if (0 == ntlibc_strcmp(argv[1], "status"))
    {
        audio_cmd_status();
        return CMD_OK;
    }

    if (0 == ntlibc_strcmp(argv[1], "init"))
    {
        err = audio_init();
        snprintf(buf, sizeof(buf), "audio init: %s (err=0x%lX)\r\n",
                 (FSP_SUCCESS == err) ? "OK" : "FAILED", (unsigned long)err);
        print_to_console(buf);
        return (FSP_SUCCESS == err) ? CMD_OK : CMD_ERR_EXECUTE;
    }

    if (0 == ntlibc_strcmp(argv[1], "start"))
    {
        err = audio_start(NULL, NULL);
        if (FSP_SUCCESS != err)
        {
            snprintf(buf, sizeof(buf), "audio start failed (err=0x%lX)\r\n",
                     (unsigned long)err);
            print_to_console(buf);
            return CMD_ERR_EXECUTE;
        }

        snprintf(buf, sizeof(buf), "audio start: %lu Hz test tone playing.\r\n",
                 (unsigned long)s_tone_freq_hz);
        print_to_console(buf);
        return CMD_OK;
    }

    if (0 == ntlibc_strcmp(argv[1], "stop"))
    {
        err = audio_stop();
        snprintf(buf, sizeof(buf), "audio stop: %s (err=0x%lX)\r\n",
                 (FSP_SUCCESS == err) ? "OK" : "TIMEOUT", (unsigned long)err);
        print_to_console(buf);
        return (FSP_SUCCESS == err) ? CMD_OK : CMD_ERR_EXECUTE;
    }

    if (0 == ntlibc_strcmp(argv[1], "tone"))
    {
        if (argc < 3)
        {
            cmd_print_usage("audio tone", "<hz> [amplitude]");
            return CMD_ERR_USAGE;
        }

        cmd_parse_result_t freq = cmd_parse_uint32(argv[2]);
        if (!freq.valid)
        {
            cmd_print_error("Invalid frequency.");
            return CMD_ERR_INVALID_ARG;
        }

        uint16_t ampl = s_tone_amplitude;
        if (argc >= 4)
        {
            cmd_parse_result_t a = cmd_parse_uint32(argv[3]);
            if ((!a.valid) || (a.value > 32767U))
            {
                cmd_print_error("Invalid amplitude (0-32767).");
                return CMD_ERR_INVALID_ARG;
            }
            ampl = (uint16_t)a.value;
        }

        err = audio_set_test_tone(freq.value, ampl);
        if (FSP_SUCCESS != err)
        {
            snprintf(buf, sizeof(buf),
                     "Frequency must be 1..%lu Hz (below Nyquist).\r\n",
                     (unsigned long)((AUDIO_SAMPLE_RATE_HZ / 2U) - 1U));
            print_to_console(buf);
            return CMD_ERR_INVALID_ARG;
        }

        snprintf(buf, sizeof(buf), "audio tone: %lu Hz, amplitude %u.\r\n",
                 (unsigned long)s_tone_freq_hz, (unsigned)s_tone_amplitude);
        print_to_console(buf);
        return CMD_OK;
    }

    if (0 == ntlibc_strcmp(argv[1], "volume"))
    {
        if (argc < 3)
        {
            snprintf(buf, sizeof(buf), "audio volume: %u%% (LINE_AMP_GAIN=0x%02X)\r\n",
                     (unsigned)da7212_get_volume(), (unsigned)da7212_get_volume_code());
            print_to_console(buf);
            return CMD_OK;
        }

        cmd_parse_result_t vol = cmd_parse_uint32(argv[2]);
        if ((!vol.valid) || (vol.value > 100U))
        {
            cmd_print_error("Volume must be 0-100.");
            return CMD_ERR_INVALID_ARG;
        }

        err = audio_set_volume((uint8_t)vol.value);
        snprintf(buf, sizeof(buf), "audio volume: %u%% -> %s (err=0x%lX)\r\n",
                 (unsigned)vol.value,
                 (FSP_SUCCESS == err) ? "OK" : "FAILED",
                 (unsigned long)err);
        print_to_console(buf);
        return (FSP_SUCCESS == err) ? CMD_OK : CMD_ERR_EXECUTE;
    }

    if (0 == ntlibc_strcmp(argv[1], "mute"))
    {
        if (argc < 3)
        {
            cmd_print_usage("audio mute", "<on|off>");
            return CMD_ERR_USAGE;
        }

        bool mute;
        if (0 == ntlibc_strcmp(argv[2], "on"))
        {
            mute = true;
        }
        else if (0 == ntlibc_strcmp(argv[2], "off"))
        {
            mute = false;
        }
        else
        {
            cmd_print_usage("audio mute", "<on|off>");
            return CMD_ERR_INVALID_ARG;
        }

        err = da7212_mute(mute);
        snprintf(buf, sizeof(buf), "audio mute %s: %s (err=0x%lX)\r\n",
                 mute ? "on" : "off",
                 (FSP_SUCCESS == err) ? "OK" : "FAILED",
                 (unsigned long)err);
        print_to_console(buf);
        return (FSP_SUCCESS == err) ? CMD_OK : CMD_ERR_EXECUTE;
    }

    if (0 == ntlibc_strcmp(argv[1], "reg"))
    {
        if (argc < 3)
        {
            cmd_print_usage("audio reg", "<addr> [value]");
            return CMD_ERR_USAGE;
        }

        cmd_parse_result_t addr = cmd_parse_uint32(argv[2]);
        if ((!addr.valid) || (addr.value > 0xFFU))
        {
            cmd_print_error("Register address must be 0x00-0xFF.");
            return CMD_ERR_INVALID_ARG;
        }

        if (argc >= 4)
        {
            cmd_parse_result_t val = cmd_parse_uint32(argv[3]);
            if ((!val.valid) || (val.value > 0xFFU))
            {
                cmd_print_error("Register value must be 0x00-0xFF.");
                return CMD_ERR_INVALID_ARG;
            }

            err = da7212_write_reg((uint8_t)addr.value, (uint8_t)val.value);
            snprintf(buf, sizeof(buf), "DA7212[0x%02X] <- 0x%02X : %s (err=0x%lX)\r\n",
                     (unsigned)addr.value, (unsigned)val.value,
                     (FSP_SUCCESS == err) ? "OK" : "FAILED",
                     (unsigned long)err);
            print_to_console(buf);
            return (FSP_SUCCESS == err) ? CMD_OK : CMD_ERR_EXECUTE;
        }

        uint8_t value = 0;
        err = da7212_read_reg((uint8_t)addr.value, &value);
        if (FSP_SUCCESS != err)
        {
            snprintf(buf, sizeof(buf), "DA7212[0x%02X] read failed (err=0x%lX)\r\n",
                     (unsigned)addr.value, (unsigned long)err);
            print_to_console(buf);
            return CMD_ERR_EXECUTE;
        }

        snprintf(buf, sizeof(buf), "DA7212[0x%02X] = 0x%02X\r\n",
                 (unsigned)addr.value, (unsigned)value);
        print_to_console(buf);
        return CMD_OK;
    }

    snprintf(buf, sizeof(buf), "Error: Unknown sub-command '%s'.\r\n", argv[1]);
    print_to_console(buf);
    audio_cmd_usage();

    return CMD_ERR_INVALID_ARG;
}
