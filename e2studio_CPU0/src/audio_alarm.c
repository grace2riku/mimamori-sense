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

/** Event flag bit set by alarm_post_request() when a caller has declared a
 *  new desired state. Consumed by alarm_task(). */
#define ALARM_EVT_REQUEST           (1U << 1)

/** Every bit alarm_task() waits on. */
#define ALARM_EVT_ANY               (ALARM_EVT_FINISHED | ALARM_EVT_REQUEST)

/** Layout of the packed generator request word (see s_gen_request). */
#define ALARM_GEN_PATTERN_MASK      (0xFFU)
#define ALARM_GEN_SEQ_SHIFT         (8U)

/** Reserved word meaning "no request". alarm_gen_publish_locked() never
 *  produces it, so it is safe as the "nothing consumed / nothing finished"
 *  marker for s_gen_applied and s_finished_req. */
#define ALARM_GEN_REQ_NONE          (0U)

/**
 * How long alarm_task waits for OUR OWN previous stop to reach
 * I2S_EVENT_IDLE before starting the device again, and how often it looks.
 *
 * audio_stop() deliberately leaves the device in AUDIO_STATE_STOPPING when its
 * own wait expires (src/port/audio_port.c:691-706), and audio_start() rejects
 * that state with FSP_ERR_IN_USE (src/port/audio_port.c:562-575). Observed on
 * hardware: the SSI interrupt starvation of Issue #206 delayed the idle well
 * past AUDIO_STOP_TIMEOUT_MS (70 ms), so "alarm stop" reported a timeout and
 * left STOPPING behind. Sized above the ~572 ms per-underrun recovery measured
 * for that issue.
 */
#define ALARM_STOPPING_WAIT_MS      (1000U)
#define ALARM_STOPPING_POLL_MS      (10U)

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

/** The pattern last asked of the generator. REPORTING ONLY - the generator
 *  itself is driven by s_gen_request, which carries the same pattern packed
 *  with a sequence; this mirror exists so that "alarm status" and
 *  alarm_sound_get_pattern() have something to show. Published in the same
 *  task-atomic step as s_gen_request, so the two never disagree. */
static volatile alarm_pattern_t s_pattern = ALARM_PATTERN_NONE;

/** true while this module owns the audio stream (audio_start() succeeded and
 *  audio_stop() has not run yet). Written by alarm_task ONLY, which is the
 *  only task that calls audio_start() / audio_stop(); everybody else reads. */
static volatile bool s_active = false;

/**
 * The generator request whose one-shot has finished and been played out, or 0
 * when there is no completion outstanding.
 *
 * A TAG rather than a flag, because "discard the stale completion" cannot be
 * ordered correctly against the ISR:
 *   - clearing BEFORE publishing a new request lets a completion of the OLD
 *     pattern that lands in between survive, and the new pattern is then
 *     stopped for no reason;
 *   - clearing AFTER publishing erases the completion of the NEW pattern when
 *     the ISR gets there first - which is reachable, since re-enabling
 *     dispatch can hand the CPU to another task for as long as it likes - and
 *     the stream is then left PLAYING silence with nothing to end it.
 * The ISR cannot be locked out either: tk_dis_dsp() does not mask it.
 *
 * Tagging removes the choice. The generator records WHICH request it finished,
 * and alarm_reconcile() acts on it only while that is still the request in
 * force, so publishing a new one invalidates an old completion without anybody
 * having to clear anything.
 *
 * The tag is still retired when the generator moves on to another request -
 * alarm_fill_cb() does it, being the owner of both this and s_gen_applied.
 * That is not needed for correctness of the comparison, but it stops a fossil
 * tag from surviving until the 24-bit sequence in the packed request word
 * wraps round to it again.
 */
static volatile uint32_t s_finished_req = ALARM_GEN_REQ_NONE;

/**
 * Requested state, and the change counter that goes with it.
 *
 * This module used to let every caller drive the device directly under a
 * mutex. That could not be made correct: alarm_sound_start() holds the mutex
 * across audio_start(), which blocks for seconds retrying the codec unmute
 * over a contended IIC1 bus, so the order in which callers ACQUIRED the mutex
 * had nothing to do with the order in which they were ISSUED. Six successive
 * attempts to recover that order with tickets, generations and abandonment
 * watermarks each left a hole (see the PR history for Issue #47).
 *
 * The concurrency is gone instead of being managed. A request now only
 * DECLARES what should be playing:
 *
 *   caller  : s_desired_pattern = X; s_request_seq++;  (one task-atomic step)
 *   alarm_task : drives audio_start() / audio_stop() until the device matches
 *
 * alarm_task is the only task that touches audio_port, so there is no mutex,
 * no lock timeout, and nothing to order: "the newest request wins" is simply
 * the last write to a single word. A request that arrives while alarm_task is
 * inside a multi-second audio_start() is picked up by the reconcile loop as
 * soon as that call returns.
 *
 * s_request_seq exists only to answer "has anything been asked for since I
 * last applied?" - it distinguishes a repeated request for the SAME pattern
 * (alarm start beep, twice) from no request at all. It is NOT an ordering
 * ticket; nothing compares one caller's value against another's.
 */
static volatile alarm_pattern_t s_desired_pattern = ALARM_PATTERN_NONE;
static volatile uint32_t        s_request_seq     = 0;

/* --- Written by alarm_task only ------------------------------------------- */

/** s_request_seq / s_desired_pattern as of the last completed reconcile. */
static volatile uint32_t        s_applied_seq     = 0;
static volatile alarm_pattern_t s_applied_pattern = ALARM_PATTERN_NONE;

/** Result of the last reconcile, reported by the API wrappers and by
 *  "alarm status". */
static volatile fsp_err_t s_last_error = FSP_SUCCESS;

/** Number of completed reconcile passes ("alarm status" diagnostics). */
static volatile uint32_t s_settle_count = 0;

/** Waveform and digital amplitude. Single words, read by the ISR once per
 *  buffer, written from task context - a change simply takes effect on the
 *  next buffer. */
static volatile alarm_wave_t s_wave      = ALARM_WAVE_SINE;
static volatile uint16_t     s_amplitude = AUDIO_TEST_AMPLITUDE;

/* --- Generator (ISR) state ----------------------------------------------- */
/* Written by the generator only, plus alarm_generator_reset() from task
 * context while the stream is stopped (i.e. while no fill can run). Declared
 * volatile because "alarm status" reads them from ntshell_task. */

/**
 * The generator request, published as ONE aligned 32-bit word:
 *
 *     (sequence << ALARM_GEN_SEQ_SHIFT) | pattern
 *
 * Why a single word rather than "write the pattern, then raise a restart
 * flag": the SSI ISR can preempt task context between any two stores. With a
 * separate pattern and flag it could consume the pattern change on one fill
 * and the flag on the next, reloading the SAME request twice - which cuts the
 * first step of the new pattern short after one AUDIO_BUFFER_MS and restarts
 * it, distorting a one-shot and the cadence of a looping pattern. Packing
 * both into one store makes a request indivisible: the ISR either sees the
 * whole thing or none of it, and consumes it exactly once.
 *
 * The sequence is what makes a REPEATED request distinct. Asking again for
 * the pattern that is already loaded must still take effect - otherwise a
 * one-shot whose generator has finished (ALARM_GEN_IDLE) or is draining
 * (ALARM_GEN_DRAIN) would never start again, which is reachable whenever the
 * stream is still PLAYING while the one-shot on it has ended.
 *
 * Written by task context only, and always with dispatching disabled, so the
 * fast stop path in alarm_post_request() and alarm_task cannot interleave
 * their sequence increments.
 */
static volatile uint32_t s_gen_request = 0;

/** The request alarm_fill_cb() last consumed. ISR only. */
static volatile uint32_t s_gen_applied = ALARM_GEN_REQ_NONE;

/** Sequence source for s_gen_request. Task context under tk_dis_dsp() only. */
static uint32_t s_gen_seq = 0;

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

/** Event flag: ALARM_EVT_REQUEST from the API wrappers, ALARM_EVT_FINISHED
 *  from the generator ISR. Both wake alarm_task's reconcile loop. */
static ID s_alarm_flgid = 0;

/**********************************************************************************************************************
 Private (static) function prototypes
 *********************************************************************************************************************/
static bool      alarm_owns_stream(void);
static void      alarm_gen_publish_locked(alarm_pattern_t pattern);
static void      alarm_gen_publish(alarm_pattern_t pattern);
static void      alarm_post_request(alarm_pattern_t pattern, uint32_t *p_seq);
static void      alarm_reconcile(void);
static fsp_err_t alarm_apply_stop(uint32_t req_seq);
static fsp_err_t alarm_apply_start(alarm_pattern_t pattern, uint32_t req_seq);
static void      alarm_generator_reset(void);
static void      alarm_load_step(uint32_t idx, uint32_t start_elapsed);
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
 * Is the audio stream still OURS?
 *
 * audio_start() overwrites the installed producer unconditionally
 * (src/port/audio_port.c:577) and audio_stop() does not clear it, so another
 * caller can take the device over behind our back while s_active stays true:
 *
 *   alarm start emergency   -> producer = alarm_fill_cb, s_active = true
 *   audio stop              -> device READY, s_active STILL true
 *   audio start             -> producer = audio_tone_fill, device PLAYING
 *
 * At that point the device really is AUDIO_STATE_PLAYING, but it is playing
 * the built-in test tone. Comparing the installed producer against our own
 * callback is what distinguishes the two cases.
 *
 * The device state has to be part of the test as well, for the case where
 * nobody installs a replacement:
 *
 *   alarm start emergency   -> producer = alarm_fill_cb, s_active = true
 *   audio stop              -> device READY, producer STILL alarm_fill_cb
 *
 * Here the producer really is ours and s_active is still set, so callback
 * equality alone would report an alarm that is not sounding:
 * alarm_sound_is_active() would say true, alarm_sound_set_pattern() would
 * restart playback instead of returning the documented FSP_ERR_NOT_OPEN, and
 * alarm_sound_get_pattern() would keep naming a pattern while silent.
 */
static bool alarm_owns_stream(void)
{
    return (s_active &&
            (alarm_fill_cb == audio_get_fill_cb()) &&
            (AUDIO_STATE_PLAYING == audio_get_state()));
}

/**
 * Publish a generator request. Dispatching must ALREADY be disabled.
 *
 * s_pattern is kept in step purely so that "alarm status" and
 * alarm_sound_get_pattern() can report what was last asked of the generator;
 * the ISR drives itself from s_gen_request alone.
 */
static void alarm_gen_publish_locked(alarm_pattern_t pattern)
{
    uint32_t request;

    do
    {
        s_gen_seq++;

        request = (s_gen_seq << ALARM_GEN_SEQ_SHIFT) |
                  ((uint32_t)pattern & ALARM_GEN_PATTERN_MASK);
    } while (ALARM_GEN_REQ_NONE == request);

    s_gen_request = request;
    s_pattern     = pattern;
}

/**
 * Publish a generator request from a context that does not already hold the
 * dispatch lock.
 */
static void alarm_gen_publish(alarm_pattern_t pattern)
{
    const bool dispatch_off = (E_OK == tk_dis_dsp());

    alarm_gen_publish_locked(pattern);

    if (dispatch_off)
    {
        (void)tk_ena_dsp();
    }
}

/**
 * Declare what should be playing and wake alarm_task.
 *
 * The pattern and the change counter are published in one task-atomic step so
 * that alarm_task can never sample a counter that does not belong to the
 * pattern beside it. tk_dis_dsp() is the right scope: the only writers of
 * these variables are TASKS.
 *
 * A stop additionally publishes a generator request right here, because going
 * quiet is the part with a deadline (Issue #47: within 100 ms) and it must not
 * wait for alarm_task to be scheduled or for the I2C mute inside audio_stop().
 * alarm_task publishes the same silence again when it reconciles; a request
 * that changes nothing is consumed without a reload.
 *
 * @param[in]  pattern  desired pattern, ALARM_PATTERN_NONE to stop.
 * @param[out] p_seq    the change counter published for this request. May be
 *                      NULL; kept because "alarm status" and future
 *                      diagnostics may want to name a specific request.
 */
static void alarm_post_request(alarm_pattern_t pattern, uint32_t *p_seq)
{
    const bool dispatch_off = (E_OK == tk_dis_dsp());

    s_desired_pattern = pattern;
    s_request_seq++;

    if (ALARM_PATTERN_NONE == pattern)
    {
        /* Silence the generator here, without waiting for alarm_task to be
         * scheduled: this is the part of a stop that has a deadline. Already
         * inside this function's tk_dis_dsp(). */
        alarm_gen_publish_locked(ALARM_PATTERN_NONE);
    }

    if (NULL != p_seq)
    {
        *p_seq = s_request_seq;
    }

    if (dispatch_off)
    {
        (void)tk_ena_dsp();
    }

    if (s_alarm_flgid > 0)
    {
        (void)tk_set_flg(s_alarm_flgid, ALARM_EVT_REQUEST);
    }
}

/**
 * Stop the device. alarm_task context only.
 */
static fsp_err_t alarm_apply_stop(uint32_t req_seq)
{
    fsp_err_t err = FSP_SUCCESS;
    bool      stale;

    /* Same reasoning as the live-retarget path in alarm_apply_start(): the
     * staleness test and the publication share one tk_dis_dsp() section, so a
     * newer start published in between cannot be silenced by this stale stop.
     * Doing so would not break "the newest request wins" - the next reconcile
     * iteration still starts it - but it would turn a live retarget into a
     * full teardown and restart, with an audible gap and possibly the
     * ALARM_STOPPING_WAIT_MS wait on top. */
    {
        const bool dispatch_off = (E_OK == tk_dis_dsp());

        stale = (s_request_seq != req_seq);

        if (!stale)
        {
            alarm_gen_publish_locked(ALARM_PATTERN_NONE);
        }

        if (dispatch_off)
        {
            (void)tk_ena_dsp();
        }
    }

    if (stale)
    {
        return FSP_ERR_ABORTED;
    }

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

    return err;
}

/**
 * Make @p pattern play. alarm_task context only.
 *
 * Retargets the running stream when we already own it, and brings the device
 * up otherwise. This is the only place audio_start() is called from.
 *
 * @param[in] pattern   pattern to play.
 * @param[in] req_seq   s_request_seq value this call is applying, used to tell
 *                      whether a newer request has arrived while audio_start()
 *                      was blocked.
 */
static fsp_err_t alarm_apply_start(alarm_pattern_t pattern, uint32_t req_seq)
{
    fsp_err_t err;

    if (alarm_owns_stream())
    {
        /* Live pattern switch - the stream keeps running, so there is no gap.
         *
         * The request carries a fresh sequence, so it reaches the generator
         * even when it names the pattern that is already loaded; that is what
         * lets a finished or draining one-shot be started again. Deciding
         * whether the generator actually has to reload is left to
         * alarm_fill_cb(), which owns s_gen / s_render_pattern and can read
         * them without racing anybody.
         *
         * The staleness test and the publication share one tk_dis_dsp()
         * section. Testing first and publishing afterwards would leave a
         * window in which a higher-priority task runs alarm_sound_stop():
         * that call publishes silence and RETURNS, having promised the caller
         * quiet within 3 x AUDIO_BUFFER_MS - and then this republication would
         * put the superseded pattern straight back into a stream that is still
         * running, audibly, until the reconcile loop reaches the stop. Sharing
         * the section closes it, because alarm_post_request() takes the same
         * lock to publish. */
        bool stale;

        {
            const bool dispatch_off = (E_OK == tk_dis_dsp());

            stale = (s_request_seq != req_seq);

            if (!stale)
            {
                alarm_gen_publish_locked(pattern);
            }

            if (dispatch_off)
            {
                (void)tk_ena_dsp();
            }
        }

        if (stale)
        {
            /* Leave the stream exactly as the newer request left it;
             * alarm_reconcile() applies that request next. */
            return FSP_ERR_ABORTED;
        }

        /* No completion to clear: publishing above changed s_gen_request, so
         * any completion tagged with the previous request no longer matches
         * (see s_finished_req). A completion of the pattern just published is
         * kept, which is exactly what has to end this one-shot. */
        return FSP_SUCCESS;
    }

    /* The stream is not ours, or not running: drop whatever we still think we
     * own, then start fresh. alarm_apply_stop() deliberately leaves another
     * caller's stream running, so audio_start() below reports FSP_ERR_IN_USE
     * instead of silently hijacking it. */
    (void)alarm_apply_stop(req_seq);

    alarm_generator_reset();
    alarm_gen_publish(pattern);

    /* audio_start() validates the device state for us: FSP_ERR_NOT_OPEN when
     * audio_init() has not succeeded, FSP_ERR_IN_USE when the device is
     * already PLAYING (e.g. the "audio start" test tone) or still STOPPING
     * (src/port/audio_port.c:562-575). */
    /*
     * Let our own previous stop finish first.
     *
     * A stop whose I2S_EVENT_IDLE has not arrived yet leaves the device in
     * AUDIO_STATE_STOPPING, and audio_start() turns that into FSP_ERR_IN_USE.
     * Nothing would retry: the late idle only flips the device to READY, it
     * does not wake alarm_task, so the newest alarm would stay silent until
     * somebody issued another request by hand. That is not hypothetical - a
     * timed-out stop was observed on hardware (Issue #206).
     *
     * Blocking here is harmless: the API is asynchronous, so no caller is
     * waiting on this task. The loop gives up early when a newer request has
     * arrived - alarm_reconcile() applies that one instead of this stale
     * pattern - and when the device is stuck it simply falls through and lets
     * audio_start() report the failure honestly.
     */
    for (uint32_t waited = 0; waited < ALARM_STOPPING_WAIT_MS;
         waited += ALARM_STOPPING_POLL_MS)
    {
        if (AUDIO_STATE_STOPPING != audio_get_state())
        {
            break;
        }

        if (s_request_seq != req_seq)
        {
            break;
        }

        tk_dly_tsk(ALARM_STOPPING_POLL_MS);
    }

    /*
     * Whatever ended that wait, a newer request makes this one stale and it
     * must NOT reach audio_start().
     *
     * Breaking out of the loop is not enough - it only stops waiting, and
     * execution would carry straight on and bring the device up with the
     * superseded pattern. audio_start() pre-fills both buffers and starts the
     * stream, so those buffers can be heard before alarm_reconcile() gets its
     * next iteration and applies the newer request. The state test above also
     * runs first, so a newer request that arrives just as the device reaches
     * READY would skip the sequence test inside the loop entirely.
     *
     * Returning here leaves the device alone; alarm_reconcile() re-samples and
     * applies the newer request instead. It records this call as applied under
     * the stale sequence, which is what lets that next iteration see the newer
     * one as a change.
     */
    if (s_request_seq != req_seq)
    {
        return FSP_ERR_ABORTED;
    }

    /*
     * Snapshot the completion counter: audio_start() starts the SSI stream
     * BEFORE it retries the codec unmute (src/port/audio_port.c:602-638), and
     * those retries can take seconds on a contended IIC1 bus. Everything the
     * generator produces in that window is rendered into a MUTED codec, so a
     * short one-shot can play out entirely without ever being heard.
     */
    const uint32_t finish_before = s_finish_count;

    err = audio_start(alarm_fill_cb, NULL);
    if (FSP_SUCCESS != err)
    {
        /*
         * The generator may have played the pattern out into the still-muted
         * codec while audio_start() was retrying the unmute, leaving a
         * completion raised. It refers to a start that has just FAILED and to
         * a device that audio_start() has already rolled back, so there is
         * nothing for it to stop.
         *
         * Publishing silence here is what invalidates it: the completion is
         * tagged with the request that was in force while the pattern played
         * (see s_finished_req), and that is no longer the current one. Left
         * matching, alarm_reconcile()'s very next iteration would take the
         * automatic-stop path under the SAME applied sequence and overwrite
         * s_last_error with the stop's FSP_SUCCESS, losing the only record
         * that this start failed and that the alarm was never audible.
         */
        alarm_gen_publish(ALARM_PATTERN_NONE);

        return err;
    }

    s_active = true;

    if (s_finish_count != finish_before)
    {
        /*
         * The pattern finished while the codec was still muted, i.e. nothing
         * was audible. Honour the request rather than that stale completion:
         * drop it and re-arm the generator, so the caller actually gets the
         * sound it asked for. audio_start() only returns FSP_SUCCESS once the
         * unmute has succeeded, so the replay IS audible.
         *
         * ONLY while this is still the current request. audio_start() can sit
         * in its unmute retries for seconds, and a stop posted during that
         * window has already published silence; replaying here would put an
         * audible buffer of the old pattern out seconds after the caller asked
         * for quiet, breaking the 30 ms silence guarantee. The test and the
         * publish share one tk_dis_dsp() so a request cannot slip in between
         * them - alarm_post_request() takes the same lock.
         */
        const bool dispatch_off = (E_OK == tk_dis_dsp());

        if (s_request_seq == req_seq)
        {
            /* Publishing re-tags the generator, so the completion recorded
             * while the codec was muted stops matching (see s_finished_req). */
            alarm_gen_publish_locked(pattern);
        }

        if (dispatch_off)
        {
            (void)tk_ena_dsp();
        }
    }

    return FSP_SUCCESS;
}

/**
 * Drive the device until it matches what has been requested.
 *
 * alarm_task context only - which is what makes the whole module safe: no
 * other task ever calls audio_start() / audio_stop(), so there is nothing to
 * serialise and no lock that could time out.
 *
 * The loop re-samples the request after every device operation, because
 * audio_start() can take seconds and a caller may well have changed its mind
 * in the meantime.
 */
static void alarm_reconcile(void)
{
    for (;;)
    {
        alarm_pattern_t want;
        uint32_t        seq;

        /* Sample the pair atomically: alarm_post_request() publishes both
         * together, so they have to be read together. */
        {
            const bool dispatch_off = (E_OK == tk_dis_dsp());

            want = s_desired_pattern;
            seq  = s_request_seq;

            if (dispatch_off)
            {
                (void)tk_ena_dsp();
            }
        }

        if (seq == s_applied_seq)
        {
            /*
             * Nothing new has been asked for. A one-shot that has played out
             * now stops itself; comparing the counters is what distinguishes
             * that from "alarm start beep" issued a second time, which is a
             * NEW request for the same pattern and must replay it.
             */
            /* A completion counts only while it belongs to the request the
             * generator is currently under; publishing anything else retires
             * it without a race (see s_finished_req). */
            const uint32_t finished = s_finished_req;

            if ((ALARM_GEN_REQ_NONE == finished) || (finished != s_gen_request))
            {
                s_settle_count++;
                return;                 /* converged */
            }

            want = ALARM_PATTERN_NONE;
        }

        /* Nobody waits on this: the API is asynchronous, so these three are
         * read only by "alarm status" as diagnostics. */
        s_last_error      = (ALARM_PATTERN_NONE == want)
                                ? alarm_apply_stop(seq)
                                : alarm_apply_start(want, seq);
        s_applied_pattern = want;
        s_applied_seq     = seq;
        s_settle_count++;
    }
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

    /*
     * Nothing has been consumed or finished under a generator that is being
     * reset.
     *
     * s_gen_applied has to be invalidated too, not just s_finished_req.
     * Leaving the last consumed word behind is not harmless: the packed
     * request only carries a 24-bit sequence (ALARM_GEN_SEQ_SHIFT), so after
     * 2^24 publications a start of the SAME pattern can rebuild exactly that
     * word. alarm_fill_cb() would then find `req == s_gen_applied`, treat the
     * request as already consumed and never load the pattern - while
     * audio_start() has succeeded and s_active is true. The generator would
     * sit in ALARM_GEN_IDLE emitting silence, with no completion to ever wake
     * alarm_task: a real start suppressed indefinitely, not merely a spurious
     * stop.
     *
     * Safe to write from here: alarm_generator_reset() runs in task context
     * with the stream already stopped, so no fill can be in flight.
     */
    s_gen_applied  = ALARM_GEN_REQ_NONE;
    s_finished_req = ALARM_GEN_REQ_NONE;
}

/**
 * Load step @p idx of the pattern currently being rendered.
 *
 * The phase accumulator is deliberately NOT reset: keeping it continuous
 * across a frequency change removes the step discontinuity that would
 * otherwise click.
 */
static void alarm_load_step(uint32_t idx, uint32_t start_elapsed)
{
    const alarm_pattern_def_t *p_def = &s_pattern_defs[s_render_pattern];
    const alarm_step_t        *p_step = &p_def->p_steps[idx];

    uint32_t elapsed = start_elapsed;

    if (elapsed > p_step->frames)
    {
        elapsed = p_step->frames;
    }

    s_step_idx   = idx;
    s_step_total = p_step->frames;

    /* alarm_env_gain() derives the envelope from (total - left, left), so
     * seeding s_step_left below s_step_total starts the step part-way up its
     * fade-in. That is how a mid-burst pattern switch keeps its amplitude
     * continuous - see alarm_apply_pattern(). */
    s_step_left  = p_step->frames - elapsed;
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

    /* A new step inside the SAME pattern always starts from silence: the
     * previous step has just faded out, so its own fade-in is what belongs
     * here. */
    alarm_load_step(next, 0U);
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

    /*
     * Carry the envelope across a mid-burst switch.
     *
     * Starting the new step at elapsed 0 would make alarm_env_gain() return 0
     * for the first frame, i.e. drop from whatever gain the outgoing burst had
     * straight to silence - a full-amplitude step, and exactly the click the
     * envelope exists to remove. Keeping s_phase continuous does not help,
     * because the discontinuity is in the gain, not the phase.
     *
     * alarm_env_gain() is min(elapsed, remaining) x ALARM_ENV_STEP, capped at
     * unity, so the gain the outgoing burst is at right now corresponds to an
     * elapsed of min(elapsed, remaining, ALARM_FADE_FRAMES). Seeding the new
     * step with that value makes the first frame of the new step carry exactly
     * the gain the last frame of the old one had: no step, no division, and
     * the frequency change that follows is a slope change rather than a jump.
     *
     * Only when a TONE is actually sounding. A gap emits zeros regardless of
     * the envelope (alarm_fill_cb() skips generation when s_phase_step is 0),
     * so carrying its notional gain would jump from silence to full instead.
     */
    uint32_t start_elapsed = 0U;

    if ((ALARM_GEN_RUN == s_gen) && (0U != s_phase_step))
    {
        const uint32_t elapsed   = s_step_total - s_step_left;
        const uint32_t remaining = s_step_left;

        start_elapsed = (elapsed < remaining) ? elapsed : remaining;

        if (start_elapsed > ALARM_FADE_FRAMES)
        {
            start_elapsed = ALARM_FADE_FRAMES;
        }
    }

    alarm_load_step(0U, start_elapsed);
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
    const uint32_t req = s_gen_request;
    if (req != s_gen_applied)
    {
        const alarm_pattern_t want =
            (alarm_pattern_t)(req & ALARM_GEN_PATTERN_MASK);

        /* Consume the request exactly once, whether or not it turns into a
         * reload: it is one indivisible word (see s_gen_request). */
        s_gen_applied = req;

        /*
         * Retire any completion of the request being replaced.
         *
         * Done HERE because this ISR owns both s_gen_applied and
         * s_finished_req, so the retirement cannot race the generator raising
         * a new completion - task context could not clear the tag safely, it
         * would have to read-modify-write against this very code.
         *
         * Without it a stale tag lives on indefinitely, and the packed request
         * word only carries a 24-bit sequence (ALARM_GEN_SEQ_SHIFT), so after
         * 2^24 publications the value repeats: a brand new request could match
         * that fossil and be treated as already finished the moment it is
         * applied. Retiring on every change keeps a tag alive only while the
         * generator is actually on that request.
         */
        s_finished_req = ALARM_GEN_REQ_NONE;

        /*
         * Reload when the pattern differs, and also when the generator is no
         * longer RUNNING - the latter is what restarts a one-shot that has
         * already finished or is draining.
         *
         * Deliberately NOT when the same pattern is already running: such a
         * request has nothing to change, and restarting it would throw away
         * the position of a looping pattern for no reason. (A switch to a
         * DIFFERENT pattern does reload, and alarm_apply_pattern() carries the
         * envelope across so that it does not click.)
         */
        if ((want != s_render_pattern) || (ALARM_GEN_RUN != s_gen))
        {
            alarm_apply_pattern(want);
        }
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
                s_gen          = ALARM_GEN_IDLE;
                s_finished_req = s_gen_applied;
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
    if (s_alarm_flgid > 0)
    {
        return FSP_SUCCESS;
    }

    /* Re-check with dispatching disabled so two tasks racing on the lazy path
     * cannot both create the flag (same pattern as i2c_bus0_sync_init(),
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

    (void)tk_ena_dsp();

    return (s_alarm_flgid > 0) ? FSP_SUCCESS : FSP_ERR_INTERNAL;
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

    err = alarm_sound_init();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    alarm_post_request(pattern, NULL);

    return FSP_SUCCESS;
}

/**
 * Declare that nothing should be playing.
 */
fsp_err_t alarm_sound_stop(void)
{
    fsp_err_t err = alarm_sound_init();

    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* alarm_post_request() silences the generator before it returns, so the
     * output is quiet within 3 x AUDIO_BUFFER_MS whatever alarm_task and the
     * codec mute are busy with afterwards. */
    alarm_post_request(ALARM_PATTERN_NONE, NULL);

    return FSP_SUCCESS;
}

/**
 * Change the pattern of a running alarm.
 */
fsp_err_t alarm_sound_set_pattern(alarm_pattern_t pattern)
{
    fsp_err_t err;

    if ((ALARM_PATTERN_NONE == pattern) || (pattern >= ALARM_PATTERN_COUNT))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    /* Retargeting only makes sense while we own a running stream. This is a
     * snapshot: alarm_task may tear the stream down a moment later, in which
     * case the request simply brings it back up. */
    if (!alarm_sound_is_active())
    {
        return FSP_ERR_NOT_OPEN;
    }

    err = alarm_sound_init();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    alarm_post_request(pattern, NULL);

    return FSP_SUCCESS;
}

bool alarm_sound_is_active(void)
{
    return alarm_owns_stream();
}


alarm_pattern_t alarm_sound_get_pattern(void)
{
    /* Converge to NONE whenever nothing of ours is sounding, so that the
     * documented contract holds even after somebody else stopped the device
     * behind our back. s_pattern on its own would keep naming the pattern
     * that was last asked of the generator. */
    return alarm_owns_stream() ? s_pattern : ALARM_PATTERN_NONE;
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
 * The single owner of the audio device for this module: every audio_start() /
 * audio_stop() call in audio_alarm.c happens here. Callers only declare what
 * should be playing (alarm_post_request), which is why the module needs no
 * mutex and has no lock that could time out.
 *
 * Woken by ALARM_EVT_REQUEST (a caller changed the desired state) and by
 * ALARM_EVT_FINISHED (the generator finished a one-shot); both simply run the
 * reconcile loop, which drives the device until it matches the request.
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
                                 (UINT)ALARM_EVT_ANY,
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

        /*
         * The event is only a hint that something MIGHT have changed; the
         * durable state is s_desired_pattern / s_request_seq / s_finished_req,
         * and alarm_reconcile() reads those. A wake-up that turns out to have
         * nothing to do simply converges immediately, and a request that
         * arrives while we are inside a multi-second audio_start() is picked
         * up by the loop without needing its own wake-up to survive.
         */
        alarm_reconcile();
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
             alarm_owns_stream()
                 ? "ACTIVE"
                 : (!s_active
                        ? "stopped"
                        : ((alarm_fill_cb == audio_get_fill_cb())
                               ? "idle (device stopped elsewhere)"
                               : "LOST (device taken over)")),
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

    snprintf(buf, sizeof(buf), "  Requests     : desired '%s' seq=%lu / applied '%s' seq=%lu\r\n",
             alarm_pattern_name(s_desired_pattern),
             (unsigned long)s_request_seq,
             alarm_pattern_name(s_applied_pattern),
             (unsigned long)s_applied_seq);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Reconcile    : %lu passes, last err=0x%lX%s\r\n",
             (unsigned long)s_settle_count,
             (unsigned long)s_last_error,
             (s_request_seq == s_applied_seq) ? "" : " (pending)");
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

        err = is_start ? alarm_sound_start(pattern)
                       : alarm_sound_set_pattern(pattern);
        if (FSP_SUCCESS != err)
        {
            snprintf(buf, sizeof(buf), "alarm %s rejected (err=0x%lX)%s\r\n",
                     argv[1],
                     (unsigned long)err,
                     (FSP_ERR_NOT_OPEN == err)
                         ? " - not playing; use 'alarm start'"
                         : "");
            print_to_console(buf);
            return CMD_ERR_EXECUTE;
        }

        /* Accepted, not necessarily sounding yet: alarm_task brings the
         * device up asynchronously and that can take seconds when the codec
         * unmute has to fight for the IIC1 bus. "alarm status" is where the
         * outcome shows up. */
        snprintf(buf, sizeof(buf), "alarm %s: '%s' (%s) requested.\r\n",
                 argv[1],
                 alarm_pattern_name(pattern),
                 (ALARM_PATTERN_BEEP == pattern) ? "one shot" : "looping");
        print_to_console(buf);
        return CMD_OK;
    }

    if (0 == ntlibc_strcmp(argv[1], "stop"))
    {
        err = alarm_sound_stop();

        /* The generator is silenced inside the call, so this really does
         * report silence; only the device teardown is left to alarm_task. */
        snprintf(buf, sizeof(buf), "alarm stop: %s (err=0x%lX)\r\n",
                 (FSP_SUCCESS == err) ? "silenced" : "FAILED",
                 (unsigned long)err);
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
