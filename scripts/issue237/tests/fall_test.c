/* Issue #237: real state machine, deterministic task-boundary interleavings. */
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "../../../e2studio_CPU0/src/fall_detection_logic.c"

fall_detection_result_t g_fall_detection_results[AI_MAX_DETECTION_NUM];
volatile uint32_t g_fall_detection_count;
static int locked;
static int errors;
static bool alarm_requested;
static uint32_t callbacks;
static uint32_t callback_frame;
static void (*on_unlock)(void);

ER tk_dis_dsp(void)
{
    if (locked) errors++;
    locked = 1;
    return E_OK;
}
ER tk_ena_dsp(void)
{
    if (!locked) errors++;
    locked = 0;
    if (on_unlock)
    {
        void (*hook)(void) = on_unlock;
        on_unlock = 0;
        hook();
    }
    return E_OK;
}
void alarm_sound_sync_fall_state(void)
{
    if (locked) errors++;
    tk_dis_dsp();
    alarm_requested = fall_detection_get_state() == FALL_STATE_CONFIRMED;
    tk_ena_dsp();
}
static void event(uint32_t frame)
{
    if (locked) errors++;
    callbacks++;
    callback_frame = frame;
}
static void person(bool fallen)
{
    g_fall_detection_count = 1;
    g_fall_detection_results[0] = (fall_detection_result_t){
        .x = 0, .y = 10, .width = fallen ? 150 : 40,
        .height = 100, .score = 0.9f, .class_id = 0
    };
}
static void confirm_during_unlock(void)
{
    person(true);
    fall_detection_update();
}
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int run_tests(void)
{
    fall_detection_stats_t stats;
    fall_detection_log_entry_t log[FALL_DETECT_LOG_SIZE];
    fall_detection_params_t params;
    fall_detection_init();
    CHECK(!alarm_requested);
    fall_detection_set_event_callback(event);
    person(true);
    CHECK(fall_detection_update() == FALL_STATE_SUSPECTED);
    person(false);
    CHECK(fall_detection_update() == FALL_STATE_NORMAL);
    fall_detection_get_stats(&stats);
    CHECK(stats.consecutive_count == 0);
    fall_detection_init();
    fall_detection_set_event_callback(event);
    person(false);
    CHECK(fall_detection_update() == FALL_STATE_NORMAL);
    person(true);
    for (int i = 1; i < 5; ++i)
    {
        CHECK(fall_detection_update() == FALL_STATE_SUSPECTED);
        CHECK(!alarm_requested);
    }
    CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
    CHECK(alarm_requested && callbacks == 1 && callback_frame == 6);
    for (int i = 0; i < 1100; ++i) fall_detection_update();
    CHECK(fall_detection_get_state() == FALL_STATE_CONFIRMED && callbacks == 1);
    person(false);
    for (int i = 0; i < 4; ++i) CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
    fall_detection_get_stats(&stats);
    CHECK(stats.recovery_count == 4 && stats.current_state == FALL_STATE_CONFIRMED);
    g_fall_detection_count = 0;
    CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
    fall_detection_get_stats(&stats);
    CHECK(stats.recovery_count == 0 && alarm_requested);
    person(false);
    g_fall_detection_results[0].score = 0.1f;
    for (int i = 0; i < 6; ++i) CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
    person(false);
    g_fall_detection_results[0].score = NAN;
    for (int i = 0; i < 6; ++i) CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
    g_fall_detection_results[0].score = INFINITY;
    CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
    person(false);
    g_fall_detection_results[0].score = 1.1f;
    CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
    person(false);
    g_fall_detection_results[0].width = 0;
    CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
    person(false);
    g_fall_detection_count = 2;
    g_fall_detection_results[1] = g_fall_detection_results[0];
    g_fall_detection_results[1].score = NAN;
    for (int i = 0; i < 6; ++i) CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
    g_fall_detection_results[1].score = 0.9f;
    g_fall_detection_results[1].width = 150;
    for (int i = 0; i < 6; ++i) CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
    person(false);
    for (int i = 0; i < 4; ++i) fall_detection_update();
    person(true);
    CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
    fall_detection_get_stats(&stats);
    CHECK(stats.recovery_count == 0);
    person(false);
    for (int i = 0; i < 4; ++i) CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
    CHECK(fall_detection_update() == FALL_STATE_NORMAL && !alarm_requested);
    fall_detection_set_consecutive_count(1);
    person(true);
    CHECK(fall_detection_update() == FALL_STATE_CONFIRMED && alarm_requested && callbacks == 2);
    fall_detection_reset();
    fall_detection_get_stats(&stats);
    CHECK(stats.confirmed_count == 2 && stats.current_state == FALL_STATE_NORMAL);
    CHECK(stats.consecutive_count == 0 && stats.recovery_count == 0 && !alarm_requested);

    /* Publication confirms; shell reset runs before alarm synchronization. */
    on_unlock = fall_detection_reset;
    fall_detection_update();
    CHECK(fall_detection_get_state() == FALL_STATE_NORMAL && !alarm_requested);
    /* Reset publishes NORMAL; inference confirms before reset synchronization. */
    on_unlock = confirm_during_unlock;
    fall_detection_reset();
    CHECK(fall_detection_get_state() == FALL_STATE_CONFIRMED && alarm_requested);
    /* Multiword snapshots are protected and setters reject non-finite values. */
    fall_detection_set_aspect_ratio(INFINITY);
    fall_detection_set_aspect_ratio(NAN);
    fall_detection_set_score_threshold(NAN);
    fall_detection_get_params(&params);
    CHECK(params.aspect_ratio_threshold == FALL_DETECT_ASPECT_RATIO_THRESHOLD);
    CHECK(params.score_threshold == FALL_DETECT_SCORE_THRESHOLD);
    for (int i = 0; i < 40; ++i)
    {
        fall_detection_reset();
        fall_detection_update();
    }
    CHECK(fall_detection_get_log(log, FALL_DETECT_LOG_SIZE) == FALL_DETECT_LOG_SIZE);
    for (int i = 1; i < FALL_DETECT_LOG_SIZE; ++i)
        CHECK(log[i].frame_number >= log[i-1].frame_number);
    CHECK(log[FALL_DETECT_LOG_SIZE - 1].to_state == FALL_STATE_CONFIRMED);
    /* Exact aspect threshold remains a fall candidate (>=, not >). */
    fall_detection_reset();
    person(true);
    g_fall_detection_results[0].width = 130;
    CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
    person(false);
    g_fall_detection_count = AI_MAX_DETECTION_NUM + 1U;
    CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
    fall_detection_get_stats(&stats);
    CHECK(stats.recovery_count == 0);
    CHECK(errors == 0 && locked == 0);
    fall_detection_init();
    CHECK(!alarm_requested);
    fall_detection_get_stats(&stats);
    CHECK(stats.total_frames == 0 && stats.confirmed_count == 0);
    return 0;
}
