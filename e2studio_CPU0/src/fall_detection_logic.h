/**
 * @file fall_detection_logic.h
 * @brief Fall detection state machine and temporal filter (F-003-9)
 * @details
 * Implements the fall detection judgment logic that processes AI inference
 * results (bounding boxes and scores from F-003-8 post-processing) and
 * determines whether a fall has occurred.
 *
 * The logic uses a state machine with temporal filtering to reduce
 * false positives:
 *
 *   NORMAL --> SUSPECTED --> CONFIRMED --> COOLDOWN --> NORMAL
 *
 * State transitions:
 *   NORMAL    -> SUSPECTED:  Fall candidate detected (aspect ratio or score)
 *   SUSPECTED -> CONFIRMED:  Consecutive detections >= threshold
 *   SUSPECTED -> NORMAL:     Candidate lost (no detection in current frame)
 *   CONFIRMED -> COOLDOWN:   Notification event issued to F-004
 *   COOLDOWN  -> NORMAL:     Cooldown period elapsed
 *
 * Fall candidate detection criteria:
 *   - Approach A/C: Bounding box aspect ratio (width/height) > threshold
 *     indicates a horizontal (fallen) posture
 *   - Approach B: "Fall" class detection score > threshold
 *   - Position hint: Detection in the lower portion of the frame increases
 *     fall likelihood
 *
 * Thread safety:
 *   - The state machine is updated from ai_inference_thread context
 *   - Query functions (get_state, get_stats) may be called from
 *     ntshell_thread; reads of 32-bit aligned values are atomic on
 *     Cortex-M85
 *   - Runtime parameter changes via set functions are safe as they
 *     only modify single aligned values
 *
 * Reference: Issue #26 (F-003-9) specification
 */

#ifndef FALL_DETECTION_LOGIC_H
#define FALL_DETECTION_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * @name Fall Detection Threshold Parameters
 * @brief Tunable parameters for fall detection algorithm.
 *
 * These defaults can be overridden at runtime via fall_detection_set_param()
 * or the "fall set" NT-Shell command for on-device tuning without rebuild.
 * @{
 */

/**
 * Bounding box aspect ratio threshold for fall candidate detection.
 * A person lying down produces a wider-than-tall bounding box.
 * Fall candidate if: width / height > this threshold.
 *
 * Typical range: 1.2 to 1.5
 */
#define FALL_DETECT_ASPECT_RATIO_THRESHOLD      (1.3f)

/**
 * Detection confidence score threshold.
 * Only detections with score >= this value are considered for fall judgment.
 * This is separate from the post-processing confidence threshold (F-003-8).
 */
#define FALL_DETECT_SCORE_THRESHOLD             (0.5f)

/**
 * Number of consecutive frames with fall candidate required to confirm fall.
 * Temporal filter to eliminate single-frame false positives.
 * At ~30ms per inference cycle, 5 frames = ~150ms.
 */
#define FALL_DETECT_CONSECUTIVE_COUNT           (5)

/**
 * Cooldown period after fall confirmation, in inference frames.
 * Prevents duplicate notifications for the same fall event.
 * At ~30ms per inference cycle, 1000 frames = ~30 seconds.
 */
#define FALL_DETECT_COOLDOWN_FRAMES             (1000)

/**
 * Position-based fall likelihood boost threshold.
 * If the bounding box center Y coordinate is below this fraction of the
 * camera frame height (0.0 = top, 1.0 = bottom), the detection is
 * considered more likely to be a fall. Currently used for logging/display
 * hint only; does not change the core judgment logic.
 */
#define FALL_DETECT_LOWER_POSITION_RATIO        (0.6f)

/**
 * Maximum number of state transition log entries (ring buffer).
 * Used by the "fall log" NT-Shell command for debugging.
 */
#define FALL_DETECT_LOG_SIZE                    (32)

/** @} */

/**********************************************************************************************************************
 Typedef definitions
 *********************************************************************************************************************/

/**
 * Fall detection state machine states.
 *
 * Reference: Issue #26 specification - State transitions
 */
typedef enum e_fall_state
{
    FALL_STATE_NORMAL = 0,      /**< Normal monitoring, no fall detected */
    FALL_STATE_SUSPECTED,       /**< Fall candidate detected, counting frames */
    FALL_STATE_CONFIRMED,       /**< Fall confirmed, notification pending */
    FALL_STATE_COOLDOWN,        /**< Post-notification cooldown period */
} fall_state_t;

/**
 * Fall detection runtime statistics.
 * Queried by NT-Shell "fall" command for diagnostics.
 */
typedef struct st_fall_detection_stats
{
    uint32_t    total_frames;           /**< Total frames processed */
    uint32_t    candidate_frames;       /**< Frames with fall candidate detected */
    uint32_t    confirmed_count;        /**< Total fall confirmations since boot */
    uint32_t    consecutive_count;      /**< Current consecutive detection count */
    uint32_t    cooldown_remaining;     /**< Remaining cooldown frames */
    fall_state_t current_state;         /**< Current state machine state */
    float       last_aspect_ratio;      /**< Last detected aspect ratio */
    float       last_score;             /**< Last detection score used */
    bool        last_position_hint;     /**< true if last detection was in lower frame */
} fall_detection_stats_t;

/**
 * Fall detection tunable parameters (runtime adjustable).
 */
typedef struct st_fall_detection_params
{
    float       aspect_ratio_threshold;     /**< Width/height ratio for fall candidate */
    float       score_threshold;            /**< Minimum detection score */
    uint32_t    consecutive_threshold;      /**< Consecutive frames to confirm */
    uint32_t    cooldown_frames;            /**< Cooldown period in frames */
    float       lower_position_ratio;       /**< Y-position hint threshold */
} fall_detection_params_t;

/**
 * Fall detection state transition log entry.
 * Stored in a ring buffer for "fall log" command.
 */
typedef struct st_fall_detection_log_entry
{
    uint32_t    frame_number;       /**< Frame number when transition occurred */
    fall_state_t from_state;        /**< Previous state */
    fall_state_t to_state;          /**< New state */
    float       aspect_ratio;       /**< Aspect ratio at transition */
    float       score;              /**< Detection score at transition */
    uint32_t    consecutive;        /**< Consecutive count at transition */
} fall_detection_log_entry_t;

/**
 * Fall confirmed event callback type.
 *
 * Called when the state machine transitions to CONFIRMED state.
 * This is the integration point for F-004 (alarm notification).
 *
 * @param frame_number  Frame number when fall was confirmed
 */
typedef void (*fall_detection_event_callback_t)(uint32_t frame_number);

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Initialize the fall detection logic module.
 *
 * Sets all parameters to default values, resets the state machine to NORMAL,
 * and clears statistics and log buffer.
 * Must be called once before fall_detection_update().
 */
void fall_detection_init(void);

/**
 * Update the fall detection state machine with current frame's detection results.
 *
 * This function should be called once per inference cycle (after post-processing).
 * It examines the detection results in g_fall_detection_results[] and
 * g_fall_detection_count, evaluates fall candidates, and advances the state machine.
 *
 * When a fall is confirmed (SUSPECTED -> CONFIRMED transition):
 *   1. The registered callback is invoked (if set)
 *   2. The state automatically transitions to COOLDOWN
 *
 * @return Current fall state after update
 */
fall_state_t fall_detection_update(void);

/**
 * Get the current fall detection state.
 *
 * Thread-safe: reads a single aligned 32-bit value.
 *
 * @return Current state (NORMAL, SUSPECTED, CONFIRMED, COOLDOWN)
 */
fall_state_t fall_detection_get_state(void);

/**
 * Get the state name as a human-readable string.
 *
 * @return Null-terminated string ("NORMAL", "SUSPECTED", "CONFIRMED", "COOLDOWN")
 */
const char *fall_detection_get_state_name(void);

/**
 * Get the current runtime statistics.
 *
 * @param[out] stats Pointer to statistics structure to fill
 */
void fall_detection_get_stats(fall_detection_stats_t *stats);

/**
 * Get the current tunable parameters.
 *
 * @return Pointer to the current parameters (read-only)
 */
const fall_detection_params_t *fall_detection_get_params(void);

/**
 * Set the aspect ratio threshold at runtime.
 *
 * @param threshold New aspect ratio threshold (must be > 0)
 */
void fall_detection_set_aspect_ratio(float threshold);

/**
 * Set the detection score threshold at runtime.
 *
 * @param threshold New score threshold (0.0 to 1.0)
 */
void fall_detection_set_score_threshold(float threshold);

/**
 * Set the consecutive frame count threshold at runtime.
 *
 * @param count New consecutive count (must be >= 1)
 */
void fall_detection_set_consecutive_count(uint32_t count);

/**
 * Set the cooldown period at runtime.
 *
 * @param frames New cooldown period in inference frames
 */
void fall_detection_set_cooldown_frames(uint32_t frames);

/**
 * Reset the state machine to NORMAL state.
 *
 * Clears the consecutive counter and cooldown timer.
 * Does not reset cumulative statistics (total_frames, confirmed_count).
 * Useful for the "fall reset" NT-Shell command.
 */
void fall_detection_reset(void);

/**
 * Register a callback for fall confirmed events.
 *
 * The callback is invoked from the AI inference thread context when
 * a fall is confirmed (SUSPECTED -> CONFIRMED transition).
 * This is the integration point for F-004 (alarm notification).
 *
 * Pass NULL to unregister the callback.
 *
 * @param callback Function pointer, or NULL to clear
 */
void fall_detection_set_event_callback(fall_detection_event_callback_t callback);

/**
 * Get state transition log entries.
 *
 * Returns up to FALL_DETECT_LOG_SIZE recent state transitions from
 * the internal ring buffer. Entries are returned in chronological order
 * (oldest first).
 *
 * @param[out] entries  Buffer to receive log entries
 * @param[in]  max_entries  Maximum entries to return
 * @return Number of entries actually written to the buffer
 */
uint32_t fall_detection_get_log(fall_detection_log_entry_t *entries, uint32_t max_entries);

#ifdef __cplusplus
}
#endif

#endif /* FALL_DETECTION_LOGIC_H */
