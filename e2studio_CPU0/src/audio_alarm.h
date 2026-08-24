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
 *     are TASK CONTEXT ONLY, because audio_start() / audio_stop() use I2C and
 *     uT-Kernel wait services (src/port/audio_port.c:661,675-679).
 *   - A one-shot pattern therefore cannot stop the device from the ISR. The
 *     ISR only switches to silence and signals alarm_task(), which performs
 *     the real audio_stop() from task context.
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
 * If this module is not playing yet, the generator is reset to the first step
 * of @p pattern and audio_start() is called with the internal fill callback.
 *
 * If it IS already playing (and the device really still is PLAYING, and the
 * pattern in progress has not already finished), only the pattern is replaced
 * - exactly as alarm_sound_set_pattern() does - and FSP_SUCCESS is returned;
 * the stream is not restarted, so there is no gap or click at the switch.
 * In the two remaining cases - a one-shot pattern that has just ended, or a
 * stream that was stopped behind our back by "audio stop" - the stream is torn
 * down first and then started again, so a repeated
 * alarm_sound_start(ALARM_PATTERN_BEEP) always beeps.
 *
 * Non-blocking with respect to the sound: it returns as soon as the stream is
 * running and the ISR keeps producing samples. Task context only.
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
 *                                  stopping.
 * @retval FSP_ERR_INTERNAL         Synchronisation objects unavailable.
 */
fsp_err_t alarm_sound_start(alarm_pattern_t pattern);

/**
 * Stop alarm playback.
 *
 * The generator is switched to silence FIRST - before the module mutex is
 * even taken - and the device is stopped afterwards, so the output is
 * guaranteed to go quiet within 3 x AUDIO_BUFFER_MS (30 ms) whatever the
 * codec mute inside audio_stop() and the lock contention do.
 * Task context only.
 *
 * @retval FSP_SUCCESS      Stopped (or was not playing).
 * @retval FSP_ERR_TIMEOUT  audio_stop() did not observe I2S_EVENT_IDLE; the
 *                          output is silent but the device is still STOPPING.
 * @retval FSP_ERR_INTERNAL Synchronisation objects unavailable.
 */
fsp_err_t alarm_sound_stop(void);

/**
 * Change the pattern of a running alarm.
 *
 * The change takes effect on the next buffer the ISR produces, i.e. within
 * AUDIO_BUFFER_MS, and becomes audible one further buffer later. The phase
 * accumulator is NOT reset, which is what keeps the transition click-free.
 *
 * @param[in] pattern       New pattern. ALARM_PATTERN_NONE is rejected.
 *
 * @retval FSP_SUCCESS              Pattern changed.
 * @retval FSP_ERR_INVALID_ARGUMENT @p pattern out of range or NONE.
 * @retval FSP_ERR_NOT_OPEN         Not currently playing (either this module
 *                                  never started, or the device is no longer
 *                                  in AUDIO_STATE_PLAYING) - use
 *                                  alarm_sound_start().
 */
fsp_err_t alarm_sound_set_pattern(alarm_pattern_t pattern);

/** @return true while this module owns the audio stream. */
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
 * uT-Kernel task entry: waits for the "one-shot pattern finished" event from
 * the generator ISR and performs the audio_stop() that the ISR cannot do.
 * Created and started from usermain().
 */
void alarm_task(INT stacd, void *exinf);

/** NT-Shell "alarm" command handler. */
int usrcmd_alarm(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_ALARM_H */
