/**
 * @file audio_alarm.h
 * @brief Alarm tone generator: waveform synthesis on top of the audio device
 * @details
 * Issue #47 (S-005-3). This is the WAVEFORM layer; the DEVICE layer is
 * src/port/audio_port.c (Issue #46). The split follows the contract stated in
 * src/port/audio_port.h:12-15 - audio_port owns SSIE0 / DA7212 / ping-pong
 * buffering and asks a caller-supplied fill callback for PCM, and this module
 * is that callback.
 *
 * What it produces
 * ----------------
 *   - tones from a quarter-wave sine look-up table or from a square wave,
 *     driven by a Q32 phase accumulator (same scheme as the built-in test
 *     tone, src/port/audio_port.c:202-210)
 *   - alarm PATTERNS described as a table of (frequency, duration) steps that
 *     either loop forever or play once (see alarm_pattern_t)
 *   - a short linear fade-in / fade-out on every tone burst so that a burst
 *     never starts or ends on a step from / to zero (click suppression)
 *
 * Execution context (IMPORTANT)
 * -----------------------------
 *   - The generator itself runs inside the SSI transmit ISR at priority 2
 *     (ra_gen/hal_data.c:222, txi_ipl / idle_err_ipl). It is integer-only -
 *     no float, no division, no blocking call - see the notes in
 *     audio_alarm.c.
 *   - alarm_sound_start() / alarm_sound_stop() / alarm_sound_set_pattern()
 *     are TASK CONTEXT ONLY. They do not touch the audio device themselves:
 *     each one DECLARES the pattern that should be playing and wakes
 *     alarm_task(), which is the only task that calls audio_start() /
 *     audio_stop(). That is what makes the module free of locks - "the newest
 *     request wins" is just the last write to a single word - and it is why a
 *     one-shot can stop itself even though the ISR may not call audio_stop().
 *   - The wrappers then wait for alarm_task() to report the outcome, so they
 *     still read as synchronous calls. The wait is REPORTING ONLY: the device
 *     converges on the request whether or not it succeeds.
 *
 * Volume
 * ------
 * Loudness is the DA7212 LINE amplifier gain, i.e. audio_set_volume() /
 * audio_get_volume(). This module additionally has a digital amplitude
 * (alarm_sound_set_amplitude()) that scales the generated PCM; it defaults to
 * AUDIO_TEST_AMPLITUDE and normally needs no change.
 */

#ifndef AUDIO_ALARM_H
#define AUDIO_ALARM_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

#include <tk/tkernel.h>

#include "hal_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Linear fade-in / fade-out length applied to every tone burst, in ms.
 *  3 ms is long enough to remove the click of a hard start / stop and far too
 *  short to soften the attack of an alarm (it is ~1.5 % of the 200 ms
 *  emergency burst). Every pattern step in this module is >= 100 ms, i.e.
 *  more than 2 x this value, so fade-in and fade-out never overlap - and if a
 *  shorter step is ever added, alarm_env_gain() takes the smaller of the two
 *  ramps and degrades gracefully instead of clipping. */
#define ALARM_FADE_MS               (3U)

/**********************************************************************************************************************
 Global Typedef definitions
 *********************************************************************************************************************/

/**
 * Alarm patterns.
 *
 * The step tables that implement these live in audio_alarm.c
 * (s_steps_emergency / s_steps_caution / s_steps_beep).
 */
typedef enum e_alarm_pattern
{
    ALARM_PATTERN_NONE = 0,     /**< silence / not playing */
    ALARM_PATTERN_EMERGENCY,    /**< 2 kHz <-> 3 kHz, 200 ms each, looping
                                 *   ("pi-po pi-po"). Fall CONFIRMED. */
    ALARM_PATTERN_CAUTION,      /**< 1 kHz, 500 ms on / 500 ms off, looping.
                                 *   Fall SUSPECTED. */
    ALARM_PATTERN_BEEP,         /**< 2 kHz, 100 ms, ONE SHOT (stops itself).
                                 *   Operation acknowledge. */
    ALARM_PATTERN_COUNT         /**< number of entries (not a pattern) */
} alarm_pattern_t;

/** Waveform used for every tone step. */
typedef enum e_alarm_wave
{
    ALARM_WAVE_SINE = 0,        /**< quarter-wave LUT sine (default) */
    ALARM_WAVE_SQUARE,          /**< square wave: louder / harsher on a small
                                 *   speaker because of its odd harmonics */
    ALARM_WAVE_COUNT
} alarm_wave_t;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Create the module's uT-Kernel synchronisation objects (event flag + mutex).
 *
 * Idempotent, task context only (uses tk_dis_dsp()). Called by alarm_task()
 * before anything else, and lazily by the API functions below so that the
 * order in which usermain() starts the tasks cannot matter.
 *
 * @retval FSP_SUCCESS      Objects available.
 * @retval FSP_ERR_INTERNAL tk_cre_flg() / tk_cre_mtx() failed.
 */
fsp_err_t alarm_sound_init(void);

/**
 * Start (or re-target) alarm playback.
 *
 * Declares @p pattern as the pattern that should be playing and waits for
 * alarm_task() to act on it. If the alarm is already running this is a live
 * retarget - the stream is not restarted, so there is no gap or click; if it
 * is not, the device is brought up.
 *
 * The declaration cannot be lost: it is a single word that alarm_task() reads,
 * so a request issued while alarm_task() is inside a multi-second
 * audio_start() is applied as soon as that call returns. If another caller
 * declares something else in the meantime, the LAST declaration wins and this
 * call reports FSP_ERR_ABORTED.
 *
 * Task context only.
 *
 * @param[in] pattern       Pattern to play. ALARM_PATTERN_NONE is rejected;
 *                          use alarm_sound_stop().
 *
 * @retval FSP_SUCCESS              Playing.
 * @retval FSP_ERR_INVALID_ARGUMENT @p pattern out of range or NONE.
 * @retval FSP_ERR_NOT_OPEN         audio_init() has not succeeded yet.
 * @retval FSP_ERR_IN_USE           The audio device is busy with something
 *                                  that is not this module (e.g. the
 *                                  "audio start" test tone), or it is still
 *                                  stopping. The other stream keeps playing.
 * @retval FSP_ERR_ABORTED          A newer request was applied instead, so
 *                                  this one never took effect. Query
 *                                  alarm_sound_is_active() /
 *                                  alarm_sound_get_pattern() to see what is
 *                                  playing now - do not assume silence.
 * @retval FSP_ERR_TIMEOUT          The request is still queued and its outcome
 *                                  is not known yet. It has NOT been dropped;
 *                                  alarm_task() will still apply it.
 * @retval FSP_ERR_INTERNAL         Synchronisation objects unavailable.
 */
fsp_err_t alarm_sound_start(alarm_pattern_t pattern);

/**
 * Stop alarm playback.
 *
 * The generator is silenced immediately, before alarm_task() is even woken,
 * so the output is guaranteed to go quiet within 3 x AUDIO_BUFFER_MS (30 ms)
 * whatever the codec mute and the task scheduling do. Tearing the device down
 * happens afterwards, in alarm_task().
 *
 * If another caller has taken the stream over in the meantime, only this
 * module's own state is cleared; the other stream is left running.
 * Task context only.
 *
 * @retval FSP_SUCCESS      Stopped (or was not playing).
 * @retval FSP_ERR_TIMEOUT  audio_stop() did not observe I2S_EVENT_IDLE, or the
 *                          teardown has not been reported yet. The output is
 *                          silent either way.
 * @retval FSP_ERR_ABORTED  A newer request was applied instead: something is
 *                          deliberately playing again. Query
 *                          alarm_sound_is_active() / alarm_sound_get_pattern().
 * @retval FSP_ERR_INTERNAL Synchronisation objects unavailable.
 */
fsp_err_t alarm_sound_stop(void);

/**
 * Change the pattern of a running alarm.
 *
 * Identical to alarm_sound_start() except that it refuses when this module is
 * not currently playing. The phase accumulator is NOT reset, which is what
 * keeps the transition click-free.
 *
 * @param[in] pattern       New pattern. ALARM_PATTERN_NONE is rejected.
 *
 * @retval FSP_SUCCESS              Pattern changed.
 * @retval FSP_ERR_INVALID_ARGUMENT @p pattern out of range or NONE.
 * @retval FSP_ERR_NOT_OPEN         Not currently playing: this module never
 *                                  started, the device is no longer in
 *                                  AUDIO_STATE_PLAYING, or another caller has
 *                                  taken the stream over - use
 *                                  alarm_sound_start().
 * @retval FSP_ERR_ABORTED          A newer request was applied instead - see
 *                                  alarm_sound_start().
 * @retval FSP_ERR_TIMEOUT          Still queued; the outcome is not known yet.
 */
fsp_err_t alarm_sound_set_pattern(alarm_pattern_t pattern);

/** @return true while this module owns the audio stream, i.e. the producer
 *          audio_port currently uses is this module's. Goes false on its own
 *          if another caller takes the device over. */
bool alarm_sound_is_active(void);

/** @return the pattern requested by the last alarm_sound_start() /
 *          alarm_sound_set_pattern(), or ALARM_PATTERN_NONE when stopped. */
alarm_pattern_t alarm_sound_get_pattern(void);

/**
 * Select the waveform used for tone steps.
 *
 * May be called while playing; it takes effect on the next generated buffer.
 *
 * @retval FSP_SUCCESS              Applied.
 * @retval FSP_ERR_INVALID_ARGUMENT @p wave out of range.
 */
fsp_err_t alarm_sound_set_wave(alarm_wave_t wave);

/** @return the waveform currently selected. */
alarm_wave_t alarm_sound_get_wave(void);

/**
 * Set the digital amplitude of the generated PCM (0..32767).
 *
 * This is NOT the speaker volume - see audio_set_volume(). Lowering it below
 * AUDIO_TEST_AMPLITUDE only wastes dynamic range; it exists so that a test
 * can verify the envelope / scaling path.
 *
 * @retval FSP_SUCCESS              Applied.
 * @retval FSP_ERR_INVALID_ARGUMENT @p amplitude > 32767.
 */
fsp_err_t alarm_sound_set_amplitude(uint16_t amplitude);

/** @return the digital amplitude currently applied. */
uint16_t alarm_sound_get_amplitude(void);

/** @return a static, human readable name ("emergency", "caution", "beep",
 *          "none"). Never NULL, even for an out-of-range value. */
const char *alarm_pattern_name(alarm_pattern_t pattern);

/** @return the pattern whose alarm_pattern_name() equals @p name, or
 *          ALARM_PATTERN_COUNT when there is no match. */
alarm_pattern_t alarm_pattern_from_name(const char *name);

/**
 * uT-Kernel task entry: the single owner of the audio device for this module.
 *
 * Every audio_start() / audio_stop() call in audio_alarm.c happens here. It
 * waits for "a caller declared something" or "the generator finished a
 * one-shot", then drives the device until it matches the declared state,
 * re-reading that state after each operation so a request made during a
 * multi-second audio_start() is not missed.
 *
 * Created and started from usermain().
 */
void alarm_task(INT stacd, void *exinf);

/** NT-Shell "alarm" command handler. */
int usrcmd_alarm(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_ALARM_H */
