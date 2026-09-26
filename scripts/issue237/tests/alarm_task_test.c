/* Exercise the production task loop; waits advance deterministic mock time.
 * longjmp exits only at a mock blocking boundary with dispatch enabled. */
#include <setjmp.h>
#include "../../../e2studio_CPU0/src/audio_alarm.c"

static jmp_buf task_exit;
static fall_state_t fall_state;
static audio_state_t device_state;
static audio_fill_cb_t producer;
static uint32_t now_ms, starts, stops, waits, delays, creations;
static unsigned scenario;
static int locked, errors;

ER tk_dis_dsp(void) { if (locked) errors++; locked = 1; return E_OK; }
ER tk_ena_dsp(void) { if (!locked) errors++; locked = 0; return E_OK; }
ER tk_get_otm(SYSTIM *now) { now->lo = now_ms; now->hi = 0; return E_OK; }
ID tk_cre_flg(const T_CFLG *cfg)
{
    (void)cfg;
    creations++;
    return (scenario == 2 && creations == 1) ? E_SYS : 1;
}
ER tk_set_flg(ID id, UINT bits)
{
    (void)id; (void)bits;
    if (locked) errors++;
    /* Deliberately lose every notification. Only timed waits can recover. */
    return E_SYS;
}
ER tk_dly_tsk(UINT ms)
{
    if (locked || scenario != 2 || ms != ALARM_RETRY_MS) errors++;
    delays++;
    now_ms += ms;
    device_state = AUDIO_STATE_READY;
    if (delays > 2) longjmp(task_exit, 1); /* Bound a broken init retry loop. */
    return E_OK;
}
ER tk_wai_flg(ID id, UINT bits, UINT mode, UINT *pattern, TMO timeout)
{
    (void)pattern;
    if (locked || id != 1 || bits != ALARM_EVT_ANY ||
        mode != (TWF_ORW | TWF_BITCLR) || timeout != 100) errors++;
    waits++;
    if (scenario == 2) longjmp(task_exit, 1);
    if (waits == 1)
    {
        if (starts != 0) errors++;
        fall_state = FALL_STATE_CONFIRMED;
        alarm_sound_sync_fall_state();
    }
    else if (waits == 2)
    {
        if (starts != 1 || !alarm_owns_stream()) errors++;
        /* Shell-side stop does not notify this task. */
        device_state = AUDIO_STATE_READY;
    }
    else if (waits == 3)
    {
        if (starts != 2 || !alarm_owns_stream()) errors++;
        fall_state = FALL_STATE_NORMAL;
        alarm_sound_sync_fall_state();
    }
    else
    {
        if (waits != 4 || stops != 1 || alarm_owns_stream() || s_fall_cleanup) errors++;
        longjmp(task_exit, 1);
    }
    now_ms += (uint32_t)timeout;
    return E_TMOUT;
}
fall_state_t fall_detection_get_state(void) { return fall_state; }
audio_state_t audio_get_state(void) { return device_state; }
audio_fill_cb_t audio_get_fill_cb(void) { return producer; }
fsp_err_t audio_start(audio_fill_cb_t cb, void *ctx)
{
    (void)ctx;
    if (locked || device_state != AUDIO_STATE_READY) errors++;
    starts++;
    producer = cb;
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
static void reset_test(unsigned which)
{
    scenario = which;
    fall_state = FALL_STATE_NORMAL;
    device_state = AUDIO_STATE_READY;
    producer = 0;
    starts = stops = now_ms = waits = delays = creations = 0;
    locked = errors = 0;
    s_active = s_fall_confirmed = s_fall_cleanup = s_retry_pending = false;
    s_request_seq = s_applied_seq = s_retry_at = s_retry_count = s_event_failures = 0;
    s_desired_pattern = s_applied_pattern = s_pattern = ALARM_PATTERN_NONE;
    s_last_error = s_failure_history = FSP_SUCCESS;
    s_alarm_flgid = 0;
    alarm_generator_reset();
    s_gen_request = s_gen_seq = 0;
}
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int run_tests(void)
{
    reset_test(1);
    if (setjmp(task_exit) == 0) alarm_task(0, 0);
    CHECK(waits == 4 && starts == 2 && stops == 1 && now_ms == 300);
    CHECK(delays == 0 && creations == 1 && s_event_failures == 2);
    CHECK(!errors && !locked);

    reset_test(2);
    device_state = AUDIO_STATE_UNINITIALIZED;
    fall_state = FALL_STATE_CONFIRMED;
    alarm_sound_sync_fall_state();
    CHECK(s_alarm_flgid == 0 && s_desired_pattern == ALARM_PATTERN_EMERGENCY);
    if (setjmp(task_exit) == 0) alarm_task(0, 0);
    CHECK(delays == 1 && now_ms == 500 && creations == 2 && waits == 1);
    CHECK(starts == 1 && stops == 0 && alarm_owns_stream());
    CHECK(s_event_failures == 1 && s_failure_history == FSP_ERR_NOT_OPEN);
    CHECK(!s_retry_pending && !errors && !locked);
    return 0;
}
