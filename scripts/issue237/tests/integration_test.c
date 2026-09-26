/* Issue #237: real detection + alarm owner + PCM generator integration.
 * Deterministic task/SSI-fill simulation; no real timing or audibility claim. */
#include "../../../e2studio_CPU0/src/fall_detection_logic.c"
#include "../../../e2studio_CPU0/src/audio_alarm.c"

fall_detection_result_t g_fall_detection_results[AI_MAX_DETECTION_NUM];
volatile uint32_t g_fall_detection_count;
static audio_state_t device_state = AUDIO_STATE_READY;
static audio_fill_cb_t producer;
static void *producer_context;
static uint32_t now_ms, starts, stops, prefill_count;
static int locked, errors;
static int16_t pcm[AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS];

ER tk_dis_dsp(void) { if (locked) errors++; locked = 1; return E_OK; }
ER tk_ena_dsp(void) { if (!locked) errors++; locked = 0; return E_OK; }
ER tk_get_otm(SYSTIM *now) { now->lo = now_ms; now->hi = 0; return E_OK; }
ID tk_cre_flg(const T_CFLG *cfg) { (void)cfg; return 1; }
ER tk_set_flg(ID id, UINT bits)
{ (void)id; (void)bits; if (locked) errors++; return E_OK; }
ER tk_dly_tsk(UINT ms) { if (locked) errors++; now_ms += ms; return E_OK; }
ER tk_wai_flg(ID id, UINT bits, UINT mode, UINT *pattern, TMO timeout)
{
    (void)id; (void)bits; (void)mode; (void)pattern;
    if (locked) errors++;
    now_ms += (uint32_t)timeout;
    return E_TMOUT;
}
audio_state_t audio_get_state(void) { return device_state; }
audio_fill_cb_t audio_get_fill_cb(void) { return producer; }
fsp_err_t audio_start(audio_fill_cb_t cb, void *ctx)
{
    if (locked) errors++;
    starts++;
    producer = cb;
    producer_context = ctx;
    /* Match the real start prefill: both ping-pong buffers before PLAYING. */
    cb(pcm, AUDIO_BUFFER_FRAMES, ctx);
    cb(pcm, AUDIO_BUFFER_FRAMES, ctx);
    prefill_count += 2;
    device_state = AUDIO_STATE_PLAYING;
    return FSP_SUCCESS;
}
fsp_err_t audio_stop(void)
{
    if (locked) errors++;
    stops++;
    device_state = AUDIO_STATE_READY;
    return FSP_SUCCESS;
}
static bool silent(void)
{
    for (uint32_t i = 0; i < AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS; ++i)
        if (pcm[i]) return false;
    return true;
}
static void fill(void) { producer(pcm, AUDIO_BUFFER_FRAMES, producer_context); }
static void person(bool fallen)
{
    g_fall_detection_count = 1;
    g_fall_detection_results[0] = (fall_detection_result_t){
        .x = 0, .y = 10, .width = fallen ? 150 : 40,
        .height = 100, .score = 0.9f, .class_id = 0
    };
}
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int run_tests(void)
{
    fall_detection_init();
    CHECK(alarm_sound_init() == FSP_SUCCESS);
    person(false);
    fall_detection_update();
    alarm_reconcile();
    CHECK(starts == 0);
    person(true);
    for (int i = 0; i < 4; ++i)
    {
        CHECK(fall_detection_update() == FALL_STATE_SUSPECTED);
        alarm_reconcile();
        CHECK(starts == 0);
    }
    CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
    alarm_reconcile();
    CHECK(starts == 1 && prefill_count == 2 && alarm_owns_stream());
    CHECK(!silent());
    uint32_t original_seq = s_request_seq;
    for (int i = 0; i < 5; ++i)
    {
        CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
        alarm_reconcile();
        fill();
        CHECK(!silent());
    }
    CHECK(starts == 1 && s_request_seq == original_seq);
    /* Exercise the looping pattern across its step boundary and repetitions. */
    uint32_t audible_buffers = 0;
    for (int i = 0; i < 150; ++i)
    {
        fill();
        if (!silent()) audible_buffers++;
    }
    CHECK(audible_buffers > 100 && s_step_changes > 4);
    CHECK(s_gen == ALARM_GEN_RUN && s_render_pattern == ALARM_PATTERN_EMERGENCY);
    g_fall_detection_count = 0;
    for (int i = 0; i < 6; ++i)
    {
        CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
        alarm_reconcile();
        fill();
    }
    CHECK(starts == 1 && stops == 0 && alarm_owns_stream());
    person(false);
    for (int i = 0; i < 4; ++i)
    {
        CHECK(fall_detection_update() == FALL_STATE_CONFIRMED);
        alarm_reconcile();
        fill();
        CHECK(stops == 0);
    }
    CHECK(fall_detection_update() == FALL_STATE_NORMAL);
    CHECK((s_gen_request & ALARM_GEN_PATTERN_MASK) == ALARM_PATTERN_NONE);
    /* Fill consumes the immediate silence request before the owner can stop. */
    fill();
    CHECK(silent());
    fill();
    CHECK(silent());
    alarm_reconcile();
    CHECK(stops == 1 && !alarm_owns_stream() && !s_fall_cleanup);
    person(true);
    for (int i = 0; i < 5; ++i) fall_detection_update();
    alarm_reconcile();
    CHECK(starts == 2 && prefill_count == 4 && alarm_owns_stream() && !silent());
    fall_detection_reset();
    CHECK(fall_detection_get_state() == FALL_STATE_NORMAL);
    fill();
    CHECK(silent());
    alarm_reconcile();
    CHECK(stops == 2 && !s_fall_cleanup && !alarm_owns_stream());
    CHECK(errors == 0 && locked == 0);
    return 0;
}
