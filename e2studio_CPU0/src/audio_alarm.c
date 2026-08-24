/**
 * @file audio_alarm.c
 * @brief Alarm tone generator: waveform synthesis on top of the audio device
 * @details See audio_alarm.h for the module contract.
 *
 * Where the code runs
 * -------------------
 * audio_port.c calls the producer installed by audio_start() from two places:
 *   - twice from TASK context, to pre-fill both ping-pong buffers before the
 *     stream starts (src/port/audio_port.c:587-588)
 *   - once per drained buffer from the SSI transmit ISR at priority 2
 *     (src/port/audio_port.c:328, and 347-348 on an unsolicited restart)
 * so alarm_fill_cb() must satisfy the stricter of the two contexts, i.e. the
 * ISR one (src/port/audio_port.h:157-159): short, non-blocking, no I2C.
 *
 * It therefore uses integer arithmetic only:
 *   - no floating point, so the ISR never touches the FPU and cannot force a
 *     lazy FPU context save into the exception frame
 *   - no runtime division: the per-step phase increment and duration are
 *     folded into the pattern tables by ALARM_STEP() at compile time, and the
 *     envelope ramp slope ALARM_ENV_STEP is a preprocessor constant
 *   - no library call except memset() for the silence case
 * Cost per frame is 2 multiplies, 2 shifts and a table lookup; at
 * AUDIO_SAMPLE_RATE_HZ = 16276 Hz one AUDIO_BUFFER_FRAMES buffer is 162
 * frames, produced once every AUDIO_BUFFER_MS = 10 ms on a 1 GHz Cortex-M85.
 *
 * How playback is timed
 * ---------------------
 * Samples written during a fill call are NOT the samples being played: at the
 * moment the ISR refills the buffer that was just drained, the OTHER buffer
 * has just been handed to the DTC (src/port/audio_port.c:312-328). So a fill
 * at time T is audible during [T + AUDIO_BUFFER_MS, T + 2 x AUDIO_BUFFER_MS].
 * Both the one-shot drain (ALARM_DRAIN_FILLS) and the stop-latency budget
 * (alarm_sound_stop()) are derived from that one fact.
 *
 * Why a dedicated task
 * --------------------
 * A one-shot pattern has to stop the device when it ends, and audio_stop()
 * mutes the codec over I2C and then waits on an event flag
 * (src/port/audio_port.c:661,675-679) - neither is allowed from an ISR. The
 * generator therefore only switches itself to silence and sets an event flag;
 * alarm_task() wakes up and calls audio_stop() from task context.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <tk/tkernel.h>

#include "audio_alarm.h"
#include "port/audio_port.h"
#include "jlink_console.h"
#include "cmd_utils.h"
#include "ntlibc.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size (matches AUDIO_PRINT_BUF_SIZE in audio_port.c) */
#define ALARM_PRINT_BUF_SIZE        (144)

/** Event flag bit set by the generator when a one-shot pattern has finished
 *  AND its tail has been played out. Consumed by alarm_task(). */
#define ALARM_EVT_FINISHED          (1U << 0)

/** Timeout when taking the module mutex.
 *  Must exceed the worst case of the longest critical section, which is
 *  alarm_stop_locked() -> audio_stop(): da7212_mute() can wait up to
 *  I2C_BUS0_LOCK_TIMEOUT_MS = 1000 ms for the shared IIC1 bus
 *  (src/port/i2c_bus0.h:81, src/port/i2c_bus0.c:114) and the idle wait is
 *  AUDIO_STOP_TIMEOUT_MS = 70 ms (src/port/audio_port.c:65). */
#define ALARM_LOCK_TIMEOUT_MS       (2000)

/** Retry delay used by alarm_task() if tk_wai_flg() ever fails. */
#define ALARM_TASK_ERR_DELAY_MS     (100)

/**
 * Buffer fills of silence emitted after a one-shot pattern ends, before
 * alarm_task() is told to stop the device.
 *
 * The fill that produces the last tone sample is F0; that buffer is submitted
 * to the DTC at F1 and has been fully transferred by F2 (one fill per
 * AUDIO_BUFFER_MS - see "How playback is timed" above). Signalling before F2
 * would let alarm_task() mute the codec while the tail is still being played,
 * truncating the beep. Signalling at F2 leaves at most the 32-stage SSI
 * transmit FIFO (~1 ms at this sample rate) unplayed, which is shorter than
 * the task wake-up plus the I2C mute that follows.
 */
#define ALARM_DRAIN_FILLS           (2U)

/** Entries in the quarter-wave sine table (excluding the endpoint), and the
 *  matching log2 so that the index split below cannot drift from it. */
#define ALARM_SINE_QUARTER_BITS     (8U)
#define ALARM_SINE_QUARTER          (1U << ALARM_SINE_QUARTER_BITS)

/** Phase bits used to index the reconstructed full period.
 *  4 quarters x ALARM_SINE_QUARTER = 1024 points per period, so the top 10
 *  bits of the Q32 phase accumulator select the point (truncation, no
 *  interpolation). The usual "6 dB of spur-free range per phase bit" rule
 *  puts the phase-truncation spurs around -60 dBc (peak time-domain error
 *  2*pi/1024 = 0.6 % of the amplitude), which is far below the distortion of
 *  a small speaker - and truncation costs no multiply in the ISR. */
#define ALARM_SINE_PHASE_BITS       (10U)

/** Full-scale value stored in the sine table and returned for a square wave. */
#define ALARM_FULL_SCALE            (32767)

/** Envelope gain unity in Q15 (32768 == 1.0; the (x * g) >> 15 below then
 *  reproduces x exactly at unity). */
#define ALARM_ENV_UNITY             (32768)

/** Fade length in frames. ALARM_FADE_MS x 16276 / 1000 = 48 frames.
 *  Written without a cast so that the #if below can evaluate it (a cast is
 *  not a valid token in a preprocessor expression). */
#define ALARM_FADE_FRAMES           ((ALARM_FADE_MS * AUDIO_SAMPLE_RATE_HZ) / 1000U)

/** Q15 gain increment per frame during a fade. 32768 / 48 = 682 (truncated).
 *
 *  alarm_env_gain() ramps over elapsed = 0 .. ALARM_FADE_FRAMES - 1 and takes
 *  the >= branch (unity) from elapsed == ALARM_FADE_FRAMES on, so the last
 *  ramped value is 47 x 682 = 32054 (97.8 %) and the jump to unity is 714 /
 *  32768 = 2.2 % of the gain. At the maximum amplitude used here (16000) that
 *  is a worst-case sample discontinuity of ~350 LSB, i.e. about -39 dBFS, and
 *  it happens at the END of a fade-in / start of a fade-out where the click it
 *  could cause is 33 dB below the one the fade exists to remove (a hard start
 *  is a full-amplitude step). Left as a truncating ramp on purpose: making the
 *  endpoint exact needs a runtime division, which this ISR must not do. */
#define ALARM_ENV_STEP              (ALARM_ENV_UNITY / ALARM_FADE_FRAMES)

#if (ALARM_FADE_FRAMES < 2U)
#error "ALARM_FADE_MS is too short for AUDIO_SAMPLE_RATE_HZ (fade would be 0/1 frame)."
#endif

/** Q32 phase increment for @p hz at the actual sample rate: hz * 2^32 / fs.
 *  Constant-folded by the compiler - the ISR never divides. */
#define ALARM_PHASE_STEP(hz)        ((uint32_t)((((uint64_t)(hz)) << 32) / \
                                                (uint64_t)AUDIO_SAMPLE_RATE_HZ))

/** Duration in frames for @p ms at the actual sample rate. Constant-folded. */
#define ALARM_FRAMES_MS(ms)         (((uint32_t)(ms) * AUDIO_SAMPLE_RATE_HZ) / 1000U)

/** Pattern step helpers: a tone burst and a gap. */
#define ALARM_STEP(hz, ms)          { ALARM_PHASE_STEP(hz), ALARM_FRAMES_MS(ms), (uint16_t)(hz), (uint16_t)(ms) }
#define ALARM_GAP(ms)               { 0U,                   ALARM_FRAMES_MS(ms), 0U,             (uint16_t)(ms) }

/**********************************************************************************************************************
 Private (static) typedef definitions
 *********************************************************************************************************************/

/** One step of a pattern. phase_step == 0 means "silence for frames". */
typedef struct st_alarm_step
{
    uint32_t phase_step;        /**< Q32 phase increment (0 = silence) */
    uint32_t frames;            /**< duration in L+R frames */
    uint16_t freq_hz;           /**< reporting only ("alarm status") */
    uint16_t duration_ms;       /**< reporting only ("alarm status") */
} alarm_step_t;

/** A pattern: a step list plus how it ends. */
typedef struct st_alarm_pattern_def
{
    const char         *p_name;         /**< name used by the shell */
    const alarm_step_t *p_steps;        /**< step list (in ROM) */
    uint8_t             step_count;     /**< entries in p_steps */
    bool                loop;           /**< true = repeat, false = one shot */
} alarm_pattern_def_t;

/** Generator state machine (ISR side). */
typedef enum e_alarm_gen_state
{
    ALARM_GEN_IDLE = 0,         /**< emit silence, nothing pending */
    ALARM_GEN_RUN,              /**< emit the current pattern */
    ALARM_GEN_DRAIN,            /**< one shot ended: emit silence, then signal */
} alarm_gen_state_t;

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/**
 * Quarter-wave sine table, ALARM_SINE_QUARTER + 1 entries covering [0, pi/2]
 * INCLUSIVE:
 *
 *     s_sine_lut[k] = round(32767 * sin(k * pi / 512)),  k = 0 .. 256
 *
 * The endpoint (k = 256, value 32767) is what makes the mirrored quadrants
 * exact: quadrant 1 needs sin(pi/2 - x) = s_sine_lut[256 - sub], which for
 * sub = 0 must be the peak. Cost: 514 bytes of .rodata for a 1024-point
 * period. See alarm_wave_sample() for the reconstruction.
 */
static const int16_t s_sine_lut[ALARM_SINE_QUARTER + 1U] = {
         0,    201,    402,    603,    804,   1005,   1206,   1407,
      1608,   1809,   2009,   2210,   2410,   2611,   2811,   3012,
      3212,   3412,   3612,   3811,   4011,   4210,   4410,   4609,
      4808,   5007,   5205,   5404,   5602,   5800,   5998,   6195,
      6393,   6590,   6786,   6983,   7179,   7375,   7571,   7767,
      7962,   8157,   8351,   8545,   8739,   8933,   9126,   9319,
      9512,   9704,   9896,  10087,  10278,  10469,  10659,  10849,
     11039,  11228,  11417,  11605,  11793,  11980,  12167,  12353,
     12539,  12725,  12910,  13094,  13279,  13462,  13645,  13828,
     14010,  14191,  14372,  14553,  14732,  14912,  15090,  15269,
     15446,  15623,  15800,  15976,  16151,  16325,  16499,  16673,
     16846,  17018,  17189,  17360,  17530,  17700,  17869,  18037,
     18204,  18371,  18537,  18703,  18868,  19032,  19195,  19357,
     19519,  19680,  19841,  20000,  20159,  20317,  20475,  20631,
     20787,  20942,  21096,  21250,  21403,  21554,  21705,  21856,
     22005,  22154,  22301,  22448,  22594,  22739,  22884,  23027,
     23170,  23311,  23452,  23592,  23731,  23870,  24007,  24143,
     24279,  24413,  24547,  24680,  24811,  24942,  25072,  25201,
     25329,  25456,  25582,  25708,  25832,  25955,  26077,  26198,
     26319,  26438,  26556,  26674,  26790,  26905,  27019,  27133,
     27245,  27356,  27466,  27575,  27683,  27790,  27896,  28001,
     28105,  28208,  28310,  28411,  28510,  28609,  28706,  28803,
     28898,  28992,  29085,  29177,  29268,  29358,  29447,  29534,
     29621,  29706,  29791,  29874,  29956,  30037,  30117,  30195,
     30273,  30349,  30424,  30498,  30571,  30643,  30714,  30783,
     30852,  30919,  30985,  31050,  31113,  31176,  31237,  31297,
     31356,  31414,  31470,  31526,  31580,  31633,  31685,  31736,
     31785,  31833,  31880,  31926,  31971,  32014,  32057,  32098,
     32137,  32176,  32213,  32250,  32285,  32318,  32351,  32382,
     32412,  32441,  32469,  32495,  32521,  32545,  32567,  32589,
     32609,  32628,  32646,  32663,  32678,  32692,  32705,  32717,
     32728,  32737,  32745,  32752,  32757,  32761,  32765,  32766,
     32767,
};

/**
 * Emergency pattern - fall CONFIRMED.
 *
 * Two alternating tones of 200 ms, looping: the classic two-tone siren.
 * A frequency SWEEP or alternation is far harder to ignore than a steady
 * tone, and 2 kHz / 3 kHz sits in the most sensitive region of human hearing.
 * Both fundamentals stay well below the Nyquist limit of this stream
 * (AUDIO_SAMPLE_RATE_HZ / 2 = 8138 Hz), so the default SINE waveform is clean.
 * The square-wave variant is NOT band-limited - it is generated by testing the
 * phase MSB - so every harmonic above 8138 Hz folds back: for the 3 kHz burst
 * the 3rd harmonic (9 kHz) aliases to 16276 - 9000 = 7276 Hz. That is
 * deliberate and harmless here: aliased partials of a square wave only make an
 * alarm buzzier, which is why the option exists on a small speaker. Use the
 * sine if a clean tone is ever needed.
 * No gap between the two bursts: each burst has its own 3 ms fade, which is
 * enough to separate them audibly without breaking the siren rhythm.
 */
static const alarm_step_t s_steps_emergency[] = {
    ALARM_STEP(2000U, 200U),
    ALARM_STEP(3000U, 200U),
};

/**
 * Caution pattern - fall SUSPECTED.
 *
 * A slow 1 kHz beep, 500 ms on / 500 ms off. Deliberately lower, slower and
 * with 50 % duty so that it cannot be confused with the emergency siren.
 */
static const alarm_step_t s_steps_caution[] = {
    ALARM_STEP(1000U, 500U),
    ALARM_GAP(500U),
};

/**
 * Acknowledge beep - one shot.
 *
 * A single 2 kHz / 100 ms burst. 100 ms is ~6 buffers, i.e. long enough that
 * the 3 ms fades stay a small fraction of it (see ALARM_FADE_MS).
 */
static const alarm_step_t s_steps_beep[] = {
    ALARM_STEP(2000U, 100U),
};

/** Pattern table, indexed by alarm_pattern_t. Entry 0 (NONE) is a valid but
 *  empty descriptor so that a bad index can never dereference NULL. */
static const alarm_pattern_def_t s_pattern_defs[ALARM_PATTERN_COUNT] = {
    [ALARM_PATTERN_NONE]      = { "none",      NULL,               0U, false },
    [ALARM_PATTERN_EMERGENCY] = { "emergency", s_steps_emergency,  2U, true  },
    [ALARM_PATTERN_CAUTION]   = { "caution",   s_steps_caution,    2U, true  },
    [ALARM_PATTERN_BEEP]      = { "beep",      s_steps_beep,       1U, false },
};

/* --- Shared between task context and the generator ------------------------ */

/** Pattern requested by task context. The ONLY variable the ISR reads to
 *  learn about a change; a single aligned word, so no tearing on Cortex-M85
 *  and no lock is needed on the ISR side. */
static volatile alarm_pattern_t s_pattern = ALARM_PATTERN_NONE;

/** true while this module owns the audio stream (audio_start() succeeded and
 *  audio_stop() has not run yet). Task context only, guarded by the mutex. */
static volatile bool s_active = false;

/** Set by the generator when a one-shot pattern has finished and its tail has
 *  been played out. Cleared by every start / stop under the mutex, which is
 *  what stops a stale event from tearing down a freshly restarted alarm. */
static volatile bool s_finished = false;

/** Waveform and digital amplitude. Single words, read by the ISR once per
 *  buffer, written from task context - a change simply takes effect on the
 *  next buffer. */
static volatile alarm_wave_t s_wave      = ALARM_WAVE_SINE;
static volatile uint16_t     s_amplitude = AUDIO_TEST_AMPLITUDE;

/* --- Generator (ISR) state ----------------------------------------------- */
/* Written by the generator only, plus alarm_generator_reset() from task
 * context while the stream is stopped (i.e. while no fill can run). Declared
 * volatile because "alarm status" reads them from ntshell_task. */

static volatile alarm_pattern_t   s_render_pattern = ALARM_PATTERN_NONE;
static volatile alarm_gen_state_t s_gen            = ALARM_GEN_IDLE;
static volatile uint32_t          s_step_idx       = 0;
static volatile uint32_t          s_step_left      = 0;
static volatile uint32_t          s_step_total     = 0;
static volatile uint32_t          s_drain_fills    = 0;
static volatile uint32_t          s_phase          = 0;
static volatile uint32_t          s_phase_step     = 0;

/** Diagnostics ("alarm status"). */
static volatile uint32_t s_fill_count   = 0;
static volatile uint32_t s_step_changes = 0;
static volatile uint32_t s_finish_count = 0;

/* --- Synchronisation ------------------------------------------------------ */

/** Event flag signalled by the generator, waited on by alarm_task(). */
static ID s_alarm_flgid = 0;

/** Serialises alarm_sound_start() / alarm_sound_stop() /
 *  alarm_sound_set_pattern() and alarm_task()'s auto-stop, so that the shell
 *  and the auto-stop can never both be inside audio_stop(). */
static ID s_alarm_mtxid = 0;

/**********************************************************************************************************************
 Private (static) function prototypes
 *********************************************************************************************************************/
static fsp_err_t alarm_lock(void);
static void      alarm_unlock(void);
static bool      alarm_owns_stream(void);
static fsp_err_t alarm_stop_locked(void);
static void      alarm_generator_reset(void);
static void      alarm_load_step(uint32_t idx);
static void      alarm_next_step(void);
static void      alarm_apply_pattern(alarm_pattern_t pattern);
static int32_t   alarm_wave_sample(uint32_t phase, alarm_wave_t wave);
static int32_t   alarm_env_gain(uint32_t elapsed, uint32_t remaining);
static void      alarm_fill_cb(int16_t *p_frames, uint32_t frame_count, void *p_context);
static void      alarm_cmd_status(void);
static void      alarm_cmd_usage(void);

/**********************************************************************************************************************
 Private (static) functions - synchronisation
 *********************************************************************************************************************/

/**
 * Take the module mutex, creating the synchronisation objects if needed.
 */
static fsp_err_t alarm_lock(void)
{
    fsp_err_t err = alarm_sound_init();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    ER ercd = tk_loc_mtx(s_alarm_mtxid, (TMO)ALARM_LOCK_TIMEOUT_MS);
    if (E_TMOUT == ercd)
    {
        return FSP_ERR_TIMEOUT;
    }
    if (E_OK != ercd)
    {
        return FSP_ERR_INTERNAL;
    }

    return FSP_SUCCESS;
}

static void alarm_unlock(void)
{
    (void)tk_unl_mtx(s_alarm_mtxid);
}

/**
 * Is the audio stream still OURS?
 *
 * s_active alone is not enough. audio_start() overwrites the installed
 * producer unconditionally (src/port/audio_port.c:577) and audio_stop() does
 * not clear it, so another caller can take the device over behind our back
 * while s_active stays true:
 *
 *   alarm start emergency   -> producer = alarm_fill_cb, s_active = true
 *   audio stop              -> device READY, s_active STILL true
 *   audio start             -> producer = audio_tone_fill, device PLAYING
 *
 * At that point the device really is AUDIO_STATE_PLAYING, but it is playing
 * the built-in test tone. Testing the device state only would make
 * alarm_sound_start() report success for a pattern that is never rendered,
 * and would make alarm_sound_stop() tear down a stream belonging to somebody
 * else. Comparing the installed producer against our own callback is what
 * distinguishes the two cases.
 *
 * @return true when this module installed the producer that audio_port is
 *         currently using.
 */
static bool alarm_owns_stream(void)
{
    return (s_active && (alarm_fill_cb == audio_get_fill_cb()));
}

/**
 * Stop playback. The mutex must already be held.
 *
 * Order matters: s_pattern is cleared BEFORE audio_stop() is called, because
 * that is the part of the stop that has a hard deadline. The generator sees
 * the change on its next fill (<= AUDIO_BUFFER_MS) and emits zeros, so the
 * output is silent within 3 x AUDIO_BUFFER_MS at the latest even if the codec
 * mute inside audio_stop() has to wait for the shared IIC1 bus (up to
 * I2C_BUS0_LOCK_TIMEOUT_MS = 1000 ms, src/port/i2c_bus0.c:114). In practice
 * the mute wins by a wide margin and silence is immediate.
 */
static fsp_err_t alarm_stop_locked(void)
{
    fsp_err_t err = FSP_SUCCESS;

    s_pattern = ALARM_PATTERN_NONE;

    if (s_active)
    {
        /* Stop the DEVICE only while the stream is still ours; otherwise just
         * drop our own bookkeeping, because audio_stop() here would silence a
         * stream another caller started (see alarm_owns_stream()). */
        if (alarm_owns_stream())
        {
            err = audio_stop();
        }

        s_active = false;
    }

    /* Drop a pending "one shot finished" event so that alarm_task() cannot
     * stop a later alarm because of it (it re-checks s_finished under the
     * mutex as well). */
    s_finished = false;
    if (s_alarm_flgid > 0)
    {
        (void)tk_clr_flg(s_alarm_flgid, (UINT)~ALARM_EVT_FINISHED);
    }

    return err;
}

/**********************************************************************************************************************
 Private (static) functions - waveform generator
 *********************************************************************************************************************/

/**
 * Reset the generator to "silent, nothing loaded".
 *
 * Task context, and only while the stream is stopped, so it cannot race the
 * ISR. Clearing s_render_pattern is what makes a repeated
 * alarm_sound_start(SAME_PATTERN) restart the pattern instead of being
 * swallowed by the "pattern unchanged" test in alarm_fill_cb().
 */
static void alarm_generator_reset(void)
{
    s_render_pattern = ALARM_PATTERN_NONE;
    s_gen            = ALARM_GEN_IDLE;
    s_step_idx       = 0;
    s_step_left      = 0;
    s_step_total     = 0;
    s_drain_fills    = 0;
    s_phase          = 0;
    s_phase_step     = 0;
    s_finished       = false;
}

/**
 * Load step @p idx of the pattern currently being rendered.
 *
 * The phase accumulator is deliberately NOT reset: keeping it continuous
 * across a frequency change removes the step discontinuity that would
 * otherwise click.
 */
static void alarm_load_step(uint32_t idx)
{
    const alarm_pattern_def_t *p_def = &s_pattern_defs[s_render_pattern];
    const alarm_step_t        *p_step = &p_def->p_steps[idx];

    s_step_idx   = idx;
    s_step_left  = p_step->frames;
    s_step_total = p_step->frames;
    s_phase_step = p_step->phase_step;

    s_step_changes++;
}

/**
 * Advance to the next step, looping or finishing the pattern.
 */
static void alarm_next_step(void)
{
    const alarm_pattern_def_t *p_def = &s_pattern_defs[s_render_pattern];
    uint32_t                   next  = s_step_idx + 1U;

    if (next >= (uint32_t)p_def->step_count)
    {
        if (!p_def->loop)
        {
            /* One shot done. Emit silence for ALARM_DRAIN_FILLS buffers so
             * the tail is really played, then signal alarm_task(). */
            s_gen         = ALARM_GEN_DRAIN;
            s_drain_fills = ALARM_DRAIN_FILLS;
            return;
        }

        next = 0U;
    }

    alarm_load_step(next);
}

/**
 * Switch the generator to @p pattern (called from the fill callback only).
 */
static void alarm_apply_pattern(alarm_pattern_t pattern)
{
    s_render_pattern = pattern;

    if ((ALARM_PATTERN_NONE == pattern) ||
        (pattern >= ALARM_PATTERN_COUNT) ||
        (NULL == s_pattern_defs[pattern].p_steps))
    {
        s_gen         = ALARM_GEN_IDLE;
        s_drain_fills = 0U;
        return;
    }

    alarm_load_step(0U);
    s_gen = ALARM_GEN_RUN;
}

/**
 * One full-scale sample of the selected waveform for the given Q32 phase.
 *
 * Sine: the top ALARM_SINE_PHASE_BITS bits index a 1024-point period, which
 * is reconstructed from the quarter-wave table by mirroring:
 *
 *   quadrant 0 (idx   0..255): +lut[sub]        sin(x)
 *   quadrant 1 (idx 256..511): +lut[256 - sub]  sin(pi/2 + x) = cos(x)
 *   quadrant 2 (idx 512..767): -lut[sub]
 *   quadrant 3 (idx 768..1023):-lut[256 - sub]
 *
 * @return -32767 .. +32767
 */
static int32_t alarm_wave_sample(uint32_t phase, alarm_wave_t wave)
{
    if (ALARM_WAVE_SQUARE == wave)
    {
        /* Same convention as the built-in test tone in
         * src/port/audio_port.c:226-232: MSB of the phase selects the half. */
        return (0U != (phase & 0x80000000U)) ? ALARM_FULL_SCALE : -ALARM_FULL_SCALE;
    }

    uint32_t idx = phase >> (32U - ALARM_SINE_PHASE_BITS);      /* 0..1023 */
    uint32_t sub = idx & (ALARM_SINE_QUARTER - 1U);             /* 0..255  */

    switch (idx >> ALARM_SINE_QUARTER_BITS)
    {
        case 0U:  return (int32_t)s_sine_lut[sub];
        case 1U:  return (int32_t)s_sine_lut[ALARM_SINE_QUARTER - sub];
        case 2U:  return -(int32_t)s_sine_lut[sub];
        default:  return -(int32_t)s_sine_lut[ALARM_SINE_QUARTER - sub];
    }
}

/**
 * Linear fade-in / fade-out gain in Q15 (0 .. ALARM_ENV_UNITY).
 *
 * Taking the MINIMUM of the two ramps means the function is still correct if
 * a step is shorter than 2 x ALARM_FADE_FRAMES: the burst then simply never
 * reaches unity instead of clipping or wrapping.
 *
 * @param[in] elapsed    frames already produced in this step
 * @param[in] remaining  frames still to produce in this step
 */
static int32_t alarm_env_gain(uint32_t elapsed, uint32_t remaining)
{
    int32_t g_in  = (elapsed   >= ALARM_FADE_FRAMES) ? (int32_t)ALARM_ENV_UNITY
                                                     : (int32_t)(elapsed * ALARM_ENV_STEP);
    int32_t g_out = (remaining >= ALARM_FADE_FRAMES) ? (int32_t)ALARM_ENV_UNITY
                                                     : (int32_t)(remaining * ALARM_ENV_STEP);

    return (g_in < g_out) ? g_in : g_out;
}

/**
 * Buffer producer handed to audio_start().
 *
 * @warning SSI transmit ISR context (priority 2). See the file header for the
 *          rules this obeys. The only kernel service used is tk_set_flg(),
 *          which is legal here for the same reason audio_i2s_callback() may
 *          call it (src/port/audio_port.c:382): the SSI interrupt runs at
 *          IPL 2 and uT-Kernel's critical sections mask every interrupt at
 *          IPL >= INTPRI_MAX_EXTINT_PRI (== 1), so this ISR can never
 *          interrupt the kernel itself.
 */
static void alarm_fill_cb(int16_t *p_frames, uint32_t frame_count, void *p_context)
{
    FSP_PARAMETER_NOT_USED(p_context);

    s_fill_count++;

    /* Pick up a pattern change requested by task context. One volatile read
     * of a single word - no lock, and at worst the change lands one buffer
     * (AUDIO_BUFFER_MS) later than requested. */
    alarm_pattern_t want = s_pattern;
    if (want != s_render_pattern)
    {
        alarm_apply_pattern(want);
    }

    if (ALARM_GEN_RUN != s_gen)
    {
        memset(p_frames, 0, (size_t)frame_count * AUDIO_CHANNELS * sizeof(int16_t));

        if (ALARM_GEN_DRAIN == s_gen)
        {
            if (s_drain_fills > 0U)
            {
                s_drain_fills--;
            }

            if (0U == s_drain_fills)
            {
                /* The tail of the one-shot pattern has now been transferred,
                 * so alarm_task() may stop the device. */
                s_gen      = ALARM_GEN_IDLE;
                s_finished = true;
                s_finish_count++;

                if (s_alarm_flgid > 0)
                {
                    (void)tk_set_flg(s_alarm_flgid, ALARM_EVT_FINISHED);
                }
            }
        }

        return;
    }

    uint32_t     phase = s_phase;
    uint32_t     step  = s_phase_step;
    int32_t      ampl  = (int32_t)s_amplitude;
    alarm_wave_t wave  = s_wave;

    for (uint32_t i = 0; i < frame_count; i++)
    {
        int16_t sample = 0;

        if ((ALARM_GEN_RUN == s_gen) && (0U == s_step_left))
        {
            alarm_next_step();
            step = s_phase_step;            /* reload: the step may have changed */
        }

        if (ALARM_GEN_RUN == s_gen)
        {
            if (0U != step)                 /* 0 = silent step (gap) */
            {
                int32_t raw  = alarm_wave_sample(phase, wave);
                int32_t gain = alarm_env_gain(s_step_total - s_step_left, s_step_left);

                /* Both products stay inside int32: 32767 * 32767 < 2^30 and
                 * 32767 * 32768 == 2^30. The >> of a negative value is an
                 * arithmetic shift on this toolchain (and on every Arm
                 * target), so it rounds towards -inf: at most 1 LSB of DC,
                 * i.e. -90 dBFS. */
                raw    = (raw * ampl) >> 15;
                raw    = (raw * gain) >> 15;
                sample = (int16_t)raw;

                phase += step;
            }

            s_step_left--;
        }

        p_frames[(i * AUDIO_CHANNELS) + 0] = sample;    /* left  */
        p_frames[(i * AUDIO_CHANNELS) + 1] = sample;    /* right -> MIXOUT_R -> LINE amp */
    }

    s_phase = phase;
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Create the module's synchronisation objects.
 */
fsp_err_t alarm_sound_init(void)
{
    if ((s_alarm_flgid > 0) && (s_alarm_mtxid > 0))
    {
        return FSP_SUCCESS;
    }

    /* Re-check with dispatching disabled so two tasks racing on the lazy path
     * cannot both create an object (same pattern as i2c_bus0_sync_init(),
     * src/port/i2c_bus0.c:72-100). Task context only. */
    if (E_OK != tk_dis_dsp())
    {
        return FSP_ERR_INTERNAL;
    }

    if (s_alarm_flgid <= 0)
    {
        T_CFLG cflg = {
            .exinf   = NULL,
            .flgatr  = TA_TFIFO | TA_WMUL,
            .iflgptn = 0,
        };

        ID flgid = tk_cre_flg(&cflg);
        if (flgid > E_OK)
        {
            s_alarm_flgid = flgid;
        }
    }

    if (s_alarm_mtxid <= 0)
    {
        T_CMTX cmtx = {
            .exinf  = NULL,
            .mtxatr = TA_INHERIT,
        };

        ID mtxid = tk_cre_mtx(&cmtx);
        if (mtxid > E_OK)
        {
            s_alarm_mtxid = mtxid;
        }
    }

    (void)tk_ena_dsp();

    return ((s_alarm_flgid > 0) && (s_alarm_mtxid > 0)) ? FSP_SUCCESS : FSP_ERR_INTERNAL;
}

/**
 * Start (or re-target) alarm playback.
 */
fsp_err_t alarm_sound_start(alarm_pattern_t pattern)
{
    fsp_err_t err;

    if ((ALARM_PATTERN_NONE == pattern) || (pattern >= ALARM_PATTERN_COUNT))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    err = alarm_lock();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    if (s_active)
    {
        if (alarm_owns_stream() &&
            (!s_finished) &&
            (AUDIO_STATE_PLAYING == audio_get_state()))
        {
            /* Live pattern switch - no restart, no click. */
            s_pattern = pattern;
            alarm_unlock();
            return FSP_SUCCESS;
        }

        /* We still think we own the stream, but it is not usable as-is:
         *   - !alarm_owns_stream(): another caller replaced the producer
         *     (e.g. "audio start"), so the device may well be PLAYING but it
         *     is not playing US,
         *   - s_finished: a one-shot pattern has already ended and
         *     alarm_task() has not torn the stream down yet (it is either not
         *     scheduled or waiting for this mutex), or
         *   - the device is no longer PLAYING because somebody stopped it
         *     behind our back (e.g. "audio stop").
         * Tearing it down here makes the restart below clean, and
         * alarm_stop_locked() clears the pending event so alarm_task() will
         * not stop the alarm we are about to start. In the first case
         * alarm_stop_locked() deliberately leaves the other stream running,
         * so the audio_start() below reports FSP_ERR_IN_USE instead of
         * silently hijacking it. */
        (void)alarm_stop_locked();
    }

    alarm_generator_reset();
    s_pattern = pattern;

    /* audio_start() validates the device state for us: FSP_ERR_NOT_OPEN when
     * audio_init() has not succeeded, FSP_ERR_IN_USE when the device is
     * already PLAYING (e.g. the "audio start" test tone) or still STOPPING
     * (src/port/audio_port.c:562-575). */
    err = audio_start(alarm_fill_cb, NULL);
    if (FSP_SUCCESS != err)
    {
        s_pattern = ALARM_PATTERN_NONE;
        alarm_unlock();
        return err;
    }

    s_active = true;

    alarm_unlock();

    return FSP_SUCCESS;
}

/**
 * Stop alarm playback.
 */
fsp_err_t alarm_sound_stop(void)
{
    /* Silence the generator BEFORE taking the mutex.
     *
     * Going quiet is the part of the stop that has a deadline (Issue #47:
     * within 100 ms), and the mutex can legitimately be held for much longer
     * by another task that is inside audio_start() / audio_stop(). The ISR
     * only ever READS s_pattern (alarm_fill_cb()), and this is a single
     * aligned word, so clearing it outside the lock is safe; the deadline
     * therefore does not depend on lock contention at all.
     * alarm_stop_locked() clears it again - the assignment is idempotent. */
    s_pattern = ALARM_PATTERN_NONE;

    fsp_err_t err = alarm_lock();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = alarm_stop_locked();

    alarm_unlock();

    return err;
}

/**
 * Change the pattern of a running alarm.
 */
fsp_err_t alarm_sound_set_pattern(alarm_pattern_t pattern)
{
    if ((ALARM_PATTERN_NONE == pattern) || (pattern >= ALARM_PATTERN_COUNT))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    fsp_err_t err = alarm_lock();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* Ownership AND device state are checked, so that a pattern change cannot
     * report success on a stream that "audio stop" has already killed, nor on
     * one that "audio start" has taken over (see alarm_owns_stream()). */
    if ((!alarm_owns_stream()) || (AUDIO_STATE_PLAYING != audio_get_state()))
    {
        alarm_unlock();
        return FSP_ERR_NOT_OPEN;
    }

    s_pattern = pattern;

    /* Drop a pending "one shot finished" event, exactly as the start and stop
     * paths do.
     *
     * Without this, switching away from a one-shot pattern in the window
     * between the generator finishing it and alarm_task() acting on the event
     * would install the new pattern and then have alarm_task() immediately
     * stop it: alarm_task() re-checks s_finished under this same mutex, and
     * nothing else would have cleared it. The window is tiny in practice
     * (alarm_task runs at priority 11 and preempts ntshell_task at 12 as soon
     * as the flag is set, see the priority table in src/usermain.c:277-278),
     * but the generator has already moved to ALARM_GEN_IDLE by then, so the
     * new pattern is genuinely live and must not be torn down. */
    s_finished = false;
    if (s_alarm_flgid > 0)
    {
        (void)tk_clr_flg(s_alarm_flgid, (UINT)~ALARM_EVT_FINISHED);
    }

    alarm_unlock();

    return FSP_SUCCESS;
}

bool alarm_sound_is_active(void)
{
    return alarm_owns_stream();
}

alarm_pattern_t alarm_sound_get_pattern(void)
{
    return s_pattern;
}

fsp_err_t alarm_sound_set_wave(alarm_wave_t wave)
{
    if (wave >= ALARM_WAVE_COUNT)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    s_wave = wave;

    return FSP_SUCCESS;
}

alarm_wave_t alarm_sound_get_wave(void)
{
    return s_wave;
}

fsp_err_t alarm_sound_set_amplitude(uint16_t amplitude)
{
    if (amplitude > (uint16_t)ALARM_FULL_SCALE)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    s_amplitude = amplitude;

    return FSP_SUCCESS;
}

uint16_t alarm_sound_get_amplitude(void)
{
    return s_amplitude;
}

const char *alarm_pattern_name(alarm_pattern_t pattern)
{
    if (pattern >= ALARM_PATTERN_COUNT)
    {
        return "?";
    }

    return s_pattern_defs[pattern].p_name;
}

alarm_pattern_t alarm_pattern_from_name(const char *name)
{
    if (NULL == name)
    {
        return ALARM_PATTERN_COUNT;
    }

    for (uint32_t i = (uint32_t)ALARM_PATTERN_EMERGENCY; i < (uint32_t)ALARM_PATTERN_COUNT; i++)
    {
        if (0 == ntlibc_strcmp(name, s_pattern_defs[i].p_name))
        {
            return (alarm_pattern_t)i;
        }
    }

    return ALARM_PATTERN_COUNT;
}

/**
 * uT-Kernel task entry.
 *
 * Sleeps on ALARM_EVT_FINISHED and performs the audio_stop() that the
 * generator ISR is not allowed to perform. Nothing else runs here, so the
 * task is idle except for the few milliseconds after a one-shot pattern.
 */
void alarm_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    (void)alarm_sound_init();

    for (;;)
    {
        UINT flgptn = 0;
        ER   ercd   = tk_wai_flg(s_alarm_flgid,
                                 (UINT)ALARM_EVT_FINISHED,
                                 TWF_ORW | TWF_BITCLR,
                                 &flgptn,
                                 TMO_FEVR);

        if (E_OK != ercd)
        {
            /* Should not happen (the flag exists and the wait has no timeout).
             * Back off instead of spinning if it ever does. */
            tk_dly_tsk(ALARM_TASK_ERR_DELAY_MS);
            continue;
        }

        if (FSP_SUCCESS != alarm_lock())
        {
            continue;
        }

        /* Re-check under the mutex: alarm_sound_start()/stop() clear
         * s_finished, so an event that was overtaken by a restart is ignored
         * here instead of killing the new alarm. */
        if (s_finished && s_active)
        {
            (void)alarm_stop_locked();
        }

        alarm_unlock();
    }
}

/**********************************************************************************************************************
 NT-Shell command implementation
 *********************************************************************************************************************/

static void alarm_cmd_usage(void)
{
    cmd_print_usage("alarm", "<subcommand>");
    print_to_console("  start <pattern>     - Start: emergency | caution | beep\r\n");
    print_to_console("  stop                - Stop playback\r\n");
    print_to_console("  status              - Show generator state\r\n");
    print_to_console("  pattern <pattern>   - Switch pattern while playing\r\n");
    print_to_console("  wave <sine|square>  - Select the tone waveform\r\n");
    print_to_console("  amp <0-32767>       - Digital amplitude of the PCM\r\n");
    print_to_console("  volume [0-100]      - Show or set the speaker volume\r\n");
}

static void alarm_cmd_status(void)
{
    char buf[ALARM_PRINT_BUF_SIZE];

    const char *gen_str;
    switch (s_gen)
    {
        case ALARM_GEN_RUN:   gen_str = "RUN";   break;
        case ALARM_GEN_DRAIN: gen_str = "DRAIN"; break;
        default:              gen_str = "IDLE";  break;
    }

    print_to_console("Alarm tone generator (Issue #47 / S-005-3)\r\n");

    snprintf(buf, sizeof(buf), "  Playback     : %s, pattern '%s', generator %s\r\n",
             alarm_owns_stream() ? "ACTIVE"
                                 : (s_active ? "LOST (device taken over)" : "stopped"),
             alarm_pattern_name(s_pattern),
             gen_str);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Waveform     : %s, amplitude %u / %d\r\n",
             (ALARM_WAVE_SQUARE == s_wave) ? "square" : "sine",
             (unsigned)s_amplitude, ALARM_FULL_SCALE);
    print_to_console(buf);

    /* Current step of the pattern the generator is really rendering (which
     * lags s_pattern by at most one buffer). */
    {
        alarm_pattern_t rendered = s_render_pattern;

        if ((rendered > ALARM_PATTERN_NONE) && (rendered < ALARM_PATTERN_COUNT))
        {
            const alarm_pattern_def_t *p_def = &s_pattern_defs[rendered];
            uint32_t                   idx   = s_step_idx;

            if (idx < (uint32_t)p_def->step_count)
            {
                const alarm_step_t *p_step = &p_def->p_steps[idx];

                snprintf(buf, sizeof(buf),
                         "  Step         : %lu/%u  %u Hz %u ms (%lu of %lu frames left), %s\r\n",
                         (unsigned long)(idx + 1U),
                         (unsigned)p_def->step_count,
                         (unsigned)p_step->freq_hz,
                         (unsigned)p_step->duration_ms,
                         (unsigned long)s_step_left,
                         (unsigned long)s_step_total,
                         p_def->loop ? "looping" : "one shot");
                print_to_console(buf);
            }
        }
    }

    snprintf(buf, sizeof(buf), "  Envelope     : %u ms fade (%lu frames, step %d/%d)\r\n",
             (unsigned)ALARM_FADE_MS,
             (unsigned long)ALARM_FADE_FRAMES,
             (int)ALARM_ENV_STEP,
             ALARM_ENV_UNITY);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Counters     : fills=%lu steps=%lu finished=%lu drain=%lu\r\n",
             (unsigned long)s_fill_count,
             (unsigned long)s_step_changes,
             (unsigned long)s_finish_count,
             (unsigned long)s_drain_fills);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Device       : audio state=%d, fs=%lu Hz, volume %u%%\r\n",
             (int)audio_get_state(),
             (unsigned long)audio_get_sample_rate(),
             (unsigned)audio_get_volume());
    print_to_console(buf);

    print_to_console("  ('audio status' shows the SSI / DA7212 side.)\r\n");
}

/**
 * NT-Shell "alarm" command handler.
 *
 * @note Runs in ntshell_task context. Playback is produced by the SSI ISR, so
 *       every sub-command returns immediately and the shell stays responsive
 *       while the alarm sounds.
 */
int usrcmd_alarm(int argc, char **argv)
{
    char      buf[ALARM_PRINT_BUF_SIZE];
    fsp_err_t err;

    if (argc < 2)
    {
        alarm_cmd_usage();
        return CMD_ERR_USAGE;
    }

    if (0 == ntlibc_strcmp(argv[1], "status"))
    {
        alarm_cmd_status();
        return CMD_OK;
    }

    if ((0 == ntlibc_strcmp(argv[1], "start")) || (0 == ntlibc_strcmp(argv[1], "pattern")))
    {
        bool is_start = (0 == ntlibc_strcmp(argv[1], "start"));

        if (argc < 3)
        {
            cmd_print_usage(is_start ? "alarm start" : "alarm pattern",
                            "<emergency|caution|beep>");
            return CMD_ERR_USAGE;
        }

        alarm_pattern_t pattern = alarm_pattern_from_name(argv[2]);
        if (ALARM_PATTERN_COUNT == pattern)
        {
            cmd_print_error("Unknown pattern (emergency|caution|beep).");
            return CMD_ERR_INVALID_ARG;
        }

        err = is_start ? alarm_sound_start(pattern) : alarm_sound_set_pattern(pattern);
        if (FSP_SUCCESS != err)
        {
            snprintf(buf, sizeof(buf), "alarm %s failed (err=0x%lX)%s\r\n",
                     argv[1],
                     (unsigned long)err,
                     (FSP_ERR_NOT_OPEN == err)
                         ? " - run 'audio init' / 'alarm start' first"
                         : ((FSP_ERR_IN_USE == err) ? " - device busy, try 'audio stop'" : ""));
            print_to_console(buf);
            return CMD_ERR_EXECUTE;
        }

        snprintf(buf, sizeof(buf), "alarm %s: '%s' (%s).\r\n",
                 argv[1],
                 alarm_pattern_name(pattern),
                 (ALARM_PATTERN_BEEP == pattern) ? "one shot" : "looping");
        print_to_console(buf);
        return CMD_OK;
    }

    if (0 == ntlibc_strcmp(argv[1], "stop"))
    {
        err = alarm_sound_stop();
        snprintf(buf, sizeof(buf), "alarm stop: %s (err=0x%lX)\r\n",
                 (FSP_SUCCESS == err) ? "OK" : "FAILED", (unsigned long)err);
        print_to_console(buf);
        return (FSP_SUCCESS == err) ? CMD_OK : CMD_ERR_EXECUTE;
    }

    if (0 == ntlibc_strcmp(argv[1], "wave"))
    {
        if (argc < 3)
        {
            cmd_print_usage("alarm wave", "<sine|square>");
            return CMD_ERR_USAGE;
        }

        alarm_wave_t wave;
        if (0 == ntlibc_strcmp(argv[2], "sine"))
        {
            wave = ALARM_WAVE_SINE;
        }
        else if (0 == ntlibc_strcmp(argv[2], "square"))
        {
            wave = ALARM_WAVE_SQUARE;
        }
        else
        {
            cmd_print_usage("alarm wave", "<sine|square>");
            return CMD_ERR_INVALID_ARG;
        }

        (void)alarm_sound_set_wave(wave);

        snprintf(buf, sizeof(buf), "alarm wave: %s\r\n", argv[2]);
        print_to_console(buf);
        return CMD_OK;
    }

    if (0 == ntlibc_strcmp(argv[1], "amp"))
    {
        if (argc < 3)
        {
            snprintf(buf, sizeof(buf), "alarm amp: %u\r\n", (unsigned)alarm_sound_get_amplitude());
            print_to_console(buf);
            return CMD_OK;
        }

        cmd_parse_result_t amp = cmd_parse_uint32(argv[2]);
        if ((!amp.valid) || (amp.value > (uint32_t)ALARM_FULL_SCALE))
        {
            cmd_print_error("Amplitude must be 0-32767.");
            return CMD_ERR_INVALID_ARG;
        }

        (void)alarm_sound_set_amplitude((uint16_t)amp.value);

        snprintf(buf, sizeof(buf), "alarm amp: %lu\r\n", (unsigned long)amp.value);
        print_to_console(buf);
        return CMD_OK;
    }

    if (0 == ntlibc_strcmp(argv[1], "volume"))
    {
        if (argc < 3)
        {
            snprintf(buf, sizeof(buf), "alarm volume: %u%%\r\n", (unsigned)audio_get_volume());
            print_to_console(buf);
            return CMD_OK;
        }

        cmd_parse_result_t vol = cmd_parse_uint32(argv[2]);
        if ((!vol.valid) || (vol.value > 100U))
        {
            cmd_print_error("Volume must be 0-100.");
            return CMD_ERR_INVALID_ARG;
        }

        /* Loudness is the codec LINE amplifier gain, shared with the "audio"
         * command on purpose - one hardware volume, one place to set it. */
        err = audio_set_volume((uint8_t)vol.value);
        snprintf(buf, sizeof(buf), "alarm volume: %u%% -> %s (err=0x%lX)\r\n",
                 (unsigned)vol.value,
                 (FSP_SUCCESS == err) ? "OK" : "FAILED",
                 (unsigned long)err);
        print_to_console(buf);
        return (FSP_SUCCESS == err) ? CMD_OK : CMD_ERR_EXECUTE;
    }

    snprintf(buf, sizeof(buf), "Error: Unknown sub-command '%s'.\r\n", argv[1]);
    print_to_console(buf);
    alarm_cmd_usage();

    return CMD_ERR_INVALID_ARG;
}
