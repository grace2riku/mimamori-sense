/**
 * @file fall_detection_logic.c
 * @brief Fall detection state machine and temporal filter (F-003-9)
 * @details
 * Implements the fall detection judgment logic described in Issue #26.
 *
 * Algorithm overview:
 *   1. For each frame, examine all detection results from F-003-8 post-processing
 *   2. Evaluate fall candidacy based on bounding box aspect ratio (width/height)
 *      and detection confidence score
 *   3. Track consecutive frames with fall candidates (temporal filter)
 *   4. Transition through NORMAL -> SUSPECTED -> CONFIRMED -> COOLDOWN states
 *   5. Issue event notification on fall confirmation (F-004 integration)
 *
 * The state machine runs in the ai_inference_thread context, called after
 * each inference cycle's post-processing completes.
 *
 * Reference: Issue #26 (F-003-9) specification
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include "fall_detection_logic.h"

#include <string.h>

#include "ai_application/fall_detection/fall_detection_postprocess.h"
#include "ai_application/ai_application_config.h"
#include "common_util.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Minimum floating-point value to avoid division by zero in aspect ratio */
#define EPSILON     (1e-7f)

/**********************************************************************************************************************
 Private global variables
 *********************************************************************************************************************/

/** Current fall detection state */
static volatile fall_state_t s_state = FALL_STATE_NORMAL;

/** Runtime-adjustable parameters */
static fall_detection_params_t s_params;

/** Runtime statistics */
static fall_detection_stats_t s_stats;

/** Consecutive fall candidate counter (frames) */
static uint32_t s_consecutive_count = 0;

/** Cooldown frame counter (counts down to 0) */
static uint32_t s_cooldown_counter = 0;

/** Total frame counter (monotonically increasing) */
static uint32_t s_frame_count = 0;

/** Event callback for fall confirmation (F-004 integration point) */
static fall_detection_event_callback_t s_event_callback = NULL;

/** State transition log ring buffer */
static fall_detection_log_entry_t s_log_buffer[FALL_DETECT_LOG_SIZE];
static uint32_t s_log_write_index = 0;
static uint32_t s_log_count = 0;

/**********************************************************************************************************************
 Private function prototypes
 *********************************************************************************************************************/

static bool evaluate_fall_candidate(float *out_aspect_ratio, float *out_score, bool *out_position_hint);
static void log_state_transition(fall_state_t from, fall_state_t to,
                                  float aspect_ratio, float score, uint32_t consecutive);

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Initialize the fall detection logic module.
 */
void fall_detection_init(void)
{
    /* Set default parameters from macro constants */
    s_params.aspect_ratio_threshold = FALL_DETECT_ASPECT_RATIO_THRESHOLD;
    s_params.score_threshold        = FALL_DETECT_SCORE_THRESHOLD;
    s_params.consecutive_threshold  = FALL_DETECT_CONSECUTIVE_COUNT;
    s_params.cooldown_frames        = FALL_DETECT_COOLDOWN_FRAMES;
    s_params.lower_position_ratio   = FALL_DETECT_LOWER_POSITION_RATIO;

    /* Reset state machine */
    s_state = FALL_STATE_NORMAL;
    s_consecutive_count = 0;
    s_cooldown_counter = 0;
    s_frame_count = 0;

    /* Clear statistics */
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.current_state = FALL_STATE_NORMAL;

    /* Clear log buffer */
    memset(s_log_buffer, 0, sizeof(s_log_buffer));
    s_log_write_index = 0;
    s_log_count = 0;

    /* Clear callback */
    s_event_callback = NULL;
}

/**
 * Update the fall detection state machine.
 *
 * Called once per inference cycle after post-processing.
 * Reads g_fall_detection_results[] and g_fall_detection_count
 * (populated by fall_detection_postprocess() in F-003-8).
 */
fall_state_t fall_detection_update(void)
{
    s_frame_count++;
    s_stats.total_frames = s_frame_count;

    /* Evaluate whether current frame contains a fall candidate */
    float aspect_ratio = 0.0f;
    float score = 0.0f;
    bool position_hint = false;
    bool is_candidate = evaluate_fall_candidate(&aspect_ratio, &score, &position_hint);

    /* Update diagnostic stats */
    s_stats.last_aspect_ratio = aspect_ratio;
    s_stats.last_score = score;
    s_stats.last_position_hint = position_hint;

    if (is_candidate)
    {
        s_stats.candidate_frames++;
    }

    /* State machine transition logic */
    fall_state_t prev_state = s_state;

    switch (s_state)
    {
        case FALL_STATE_NORMAL:
            if (is_candidate)
            {
                /* Fall candidate detected - start counting */
                s_consecutive_count = 1;
                s_state = FALL_STATE_SUSPECTED;
                log_state_transition(prev_state, s_state, aspect_ratio, score, s_consecutive_count);
            }
            break;

        case FALL_STATE_SUSPECTED:
            if (is_candidate)
            {
                s_consecutive_count++;

                if (s_consecutive_count >= s_params.consecutive_threshold)
                {
                    /* Consecutive threshold reached - fall confirmed */
                    s_state = FALL_STATE_CONFIRMED;
                    s_stats.confirmed_count++;

                    log_state_transition(prev_state, s_state, aspect_ratio, score, s_consecutive_count);

                    /* Issue notification event (F-004 integration) */
                    if (s_event_callback != NULL)
                    {
                        s_event_callback(s_frame_count);
                    }

                    /* Immediately transition to cooldown */
                    fall_state_t confirmed_state = s_state;
                    s_state = FALL_STATE_COOLDOWN;
                    s_cooldown_counter = s_params.cooldown_frames;

                    log_state_transition(confirmed_state, s_state, aspect_ratio, score, s_consecutive_count);
                }
            }
            else
            {
                /* Candidate lost - return to normal */
                s_consecutive_count = 0;
                s_state = FALL_STATE_NORMAL;
                log_state_transition(prev_state, s_state, aspect_ratio, score, s_consecutive_count);
            }
            break;

        case FALL_STATE_CONFIRMED:
            /* This state is transient; fall_detection_update() transitions
             * to COOLDOWN immediately after confirmation (see SUSPECTED case above).
             * If we somehow end up here, move to cooldown. */
            s_state = FALL_STATE_COOLDOWN;
            s_cooldown_counter = s_params.cooldown_frames;
            log_state_transition(prev_state, s_state, aspect_ratio, score, s_consecutive_count);
            break;

        case FALL_STATE_COOLDOWN:
            if (s_cooldown_counter > 0)
            {
                s_cooldown_counter--;
            }

            if (s_cooldown_counter == 0)
            {
                /* Cooldown period elapsed - return to normal */
                s_consecutive_count = 0;
                s_state = FALL_STATE_NORMAL;
                log_state_transition(prev_state, s_state, aspect_ratio, score, s_consecutive_count);
            }
            break;

        default:
            /* Invalid state - reset to normal */
            s_state = FALL_STATE_NORMAL;
            s_consecutive_count = 0;
            break;
    }

    /* Update stats with current state */
    s_stats.current_state = s_state;
    s_stats.consecutive_count = s_consecutive_count;
    s_stats.cooldown_remaining = s_cooldown_counter;

    return s_state;
}

/**
 * Get the current fall detection state.
 */
fall_state_t fall_detection_get_state(void)
{
    return s_state;
}

/**
 * Get the state name as a string.
 */
const char *fall_detection_get_state_name(void)
{
    switch (s_state)
    {
        case FALL_STATE_NORMAL:     return "NORMAL";
        case FALL_STATE_SUSPECTED:  return "SUSPECTED";
        case FALL_STATE_CONFIRMED:  return "CONFIRMED";
        case FALL_STATE_COOLDOWN:   return "COOLDOWN";
        default:                    return "UNKNOWN";
    }
}

/**
 * Get runtime statistics.
 */
void fall_detection_get_stats(fall_detection_stats_t *stats)
{
    if (stats != NULL)
    {
        *stats = s_stats;
    }
}

/**
 * Get current tunable parameters.
 */
const fall_detection_params_t *fall_detection_get_params(void)
{
    return &s_params;
}

/**
 * Set the aspect ratio threshold.
 */
void fall_detection_set_aspect_ratio(float threshold)
{
    if (threshold > 0.0f)
    {
        s_params.aspect_ratio_threshold = threshold;
    }
}

/**
 * Set the detection score threshold.
 */
void fall_detection_set_score_threshold(float threshold)
{
    if (threshold >= 0.0f && threshold <= 1.0f)
    {
        s_params.score_threshold = threshold;
    }
}

/**
 * Set the consecutive frame count threshold.
 */
void fall_detection_set_consecutive_count(uint32_t count)
{
    if (count >= 1)
    {
        s_params.consecutive_threshold = count;
    }
}

/**
 * Set the cooldown period.
 */
void fall_detection_set_cooldown_frames(uint32_t frames)
{
    s_params.cooldown_frames = frames;
}

/**
 * Reset the state machine to NORMAL.
 */
void fall_detection_reset(void)
{
    s_state = FALL_STATE_NORMAL;
    s_consecutive_count = 0;
    s_cooldown_counter = 0;

    /* Update stats but keep cumulative counters */
    s_stats.current_state = FALL_STATE_NORMAL;
    s_stats.consecutive_count = 0;
    s_stats.cooldown_remaining = 0;
}

/**
 * Register the fall confirmed event callback.
 */
void fall_detection_set_event_callback(fall_detection_event_callback_t callback)
{
    s_event_callback = callback;
}

/**
 * Get state transition log entries.
 */
uint32_t fall_detection_get_log(fall_detection_log_entry_t *entries, uint32_t max_entries)
{
    if (entries == NULL || max_entries == 0)
    {
        return 0;
    }

    uint32_t available = (s_log_count < FALL_DETECT_LOG_SIZE) ? s_log_count : FALL_DETECT_LOG_SIZE;
    uint32_t to_return = (available < max_entries) ? available : max_entries;

    if (to_return == 0)
    {
        return 0;
    }

    /* Calculate start index for chronological order (oldest first) */
    uint32_t start_idx;
    if (s_log_count <= FALL_DETECT_LOG_SIZE)
    {
        /* Buffer hasn't wrapped yet */
        start_idx = 0;
        /* If we want the most recent entries and there are more than requested */
        if (available > to_return)
        {
            start_idx = available - to_return;
        }
    }
    else
    {
        /* Buffer has wrapped - oldest entry is at s_log_write_index */
        /* Return the most recent to_return entries */
        if (to_return >= FALL_DETECT_LOG_SIZE)
        {
            start_idx = s_log_write_index;
        }
        else
        {
            start_idx = (s_log_write_index + FALL_DETECT_LOG_SIZE - to_return) % FALL_DETECT_LOG_SIZE;
        }
    }

    for (uint32_t i = 0; i < to_return; i++)
    {
        uint32_t idx = (start_idx + i) % FALL_DETECT_LOG_SIZE;
        entries[i] = s_log_buffer[idx];
    }

    return to_return;
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Evaluate whether the current frame contains a fall candidate.
 *
 * Examines all detection results from F-003-8 post-processing and checks:
 *   1. Detection score >= score threshold
 *   2. Bounding box aspect ratio (width/height) >= aspect ratio threshold
 *      (Approach A/C: horizontal posture indicates fall)
 *
 * The "best" candidate (highest aspect ratio among qualifying detections)
 * is used for the output parameters.
 *
 * @param[out] out_aspect_ratio  Aspect ratio of best candidate (0 if none)
 * @param[out] out_score         Score of best candidate (0 if none)
 * @param[out] out_position_hint true if candidate is in lower frame portion
 * @return true if at least one detection qualifies as fall candidate
 */
static bool evaluate_fall_candidate(float *out_aspect_ratio, float *out_score, bool *out_position_hint)
{
    uint32_t count = g_fall_detection_count;
    bool found = false;
    float best_aspect_ratio = 0.0f;
    float best_score = 0.0f;
    bool best_position_hint = false;

    for (uint32_t i = 0; i < count && i < AI_MAX_DETECTION_NUM; i++)
    {
        const fall_detection_result_t *det = &g_fall_detection_results[i];

        /* Skip detections below score threshold */
        if (det->score < s_params.score_threshold)
        {
            continue;
        }

        /* Skip detections with zero-sized bounding box */
        if (det->height == 0 || det->width == 0)
        {
            continue;
        }

        /* Calculate aspect ratio (width / height)
         *
         * Approach A/C (person detection + posture judgment):
         * A person who has fallen will produce a bounding box that is
         * wider than it is tall (aspect ratio > 1.0).
         * A standing person produces a taller-than-wide box (aspect ratio < 1.0).
         */
        float aspect_ratio = (float)det->width / ((float)det->height + EPSILON);

        /* Check position hint: is detection in the lower portion of frame? */
        float center_y = (float)det->y + (float)det->height / 2.0f;
        float frame_height = (float)CAM_QVGA_HEIGHT;
        bool is_lower = (center_y / frame_height) >= s_params.lower_position_ratio;

        /* Fall candidate: aspect ratio exceeds threshold (horizontal posture) */
        if (aspect_ratio >= s_params.aspect_ratio_threshold)
        {
            if (!found || aspect_ratio > best_aspect_ratio)
            {
                best_aspect_ratio = aspect_ratio;
                best_score = det->score;
                best_position_hint = is_lower;
            }
            found = true;
        }
    }

    *out_aspect_ratio = best_aspect_ratio;
    *out_score = best_score;
    *out_position_hint = best_position_hint;

    return found;
}

/**
 * Record a state transition in the ring buffer log.
 *
 * @param from        Previous state
 * @param to          New state
 * @param aspect_ratio Aspect ratio at transition
 * @param score       Detection score at transition
 * @param consecutive Consecutive count at transition
 */
static void log_state_transition(fall_state_t from, fall_state_t to,
                                  float aspect_ratio, float score, uint32_t consecutive)
{
    fall_detection_log_entry_t *entry = &s_log_buffer[s_log_write_index];

    entry->frame_number = s_frame_count;
    entry->from_state   = from;
    entry->to_state     = to;
    entry->aspect_ratio = aspect_ratio;
    entry->score        = score;
    entry->consecutive  = consecutive;

    s_log_write_index = (s_log_write_index + 1) % FALL_DETECT_LOG_SIZE;
    s_log_count++;
}
