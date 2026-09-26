/* Real alarm owner/generator; deterministic device and task-boundary mocks. */
#include "../../../e2studio_CPU0/src/audio_alarm.c"

static fall_state_t fall_state;
static audio_state_t device_state;
static audio_fill_cb_t producer;
static uint32_t now_ms, starts, stops;
static int locked, errors;
static bool fail_start, timeout_stop, fail_event, fail_init;
static void (*start_hook)(void);
static void (*stop_hook)(void);
static void (*unlock_hook)(void);

ER tk_dis_dsp(void) { if (locked) errors++; locked = 1; return E_OK; }
ER tk_ena_dsp(void)
{
    if (!locked) errors++;
    locked = 0;
    if (unlock_hook) { void (*hook)(void) = unlock_hook; unlock_hook = 0; hook(); }
    return E_OK;
}
ER tk_get_otm(SYSTIM *now) { now->lo = now_ms; now->hi = 0; return E_OK; }
ID tk_cre_flg(const T_CFLG *cfg) { (void)cfg; return fail_init ? E_SYS : 1; }
ER tk_set_flg(ID id, UINT bits)
{
    (void)id; (void)bits;
    if (locked) errors++;
    return fail_event ? E_SYS : E_OK;
}
ER tk_dly_tsk(UINT ms) { if (locked) errors++; now_ms += ms; return E_OK; }
ER tk_wai_flg(ID id, UINT bits, UINT mode, UINT *pattern, TMO timeout)
{
    (void)id; (void)bits; (void)mode; (void)pattern;
    if (locked) errors++;
    now_ms += (uint32_t)timeout;
    return E_TMOUT;
}
fall_state_t fall_detection_get_state(void) { return fall_state; }
audio_state_t audio_get_state(void) { return device_state; }
audio_fill_cb_t audio_get_fill_cb(void) { return producer; }
fsp_err_t audio_start(audio_fill_cb_t cb, void *ctx)
{
    (void)ctx;
    if (locked) errors++;
    starts++;
    producer = cb;
    device_state = AUDIO_STATE_PLAYING;
    if (start_hook) { void (*hook)(void) = start_hook; start_hook = 0; hook(); }
    if (fail_start) { now_ms += 1000; device_state = AUDIO_STATE_READY; return FSP_ERR_INTERNAL; }
    return FSP_SUCCESS;
}
fsp_err_t audio_stop(void)
{
    if (locked) errors++;
    stops++;
    device_state = timeout_stop ? AUDIO_STATE_STOPPING : AUDIO_STATE_READY;
    if (stop_hook) { void (*hook)(void) = stop_hook; stop_hook = 0; hook(); }
    return timeout_stop ? FSP_ERR_TIMEOUT : FSP_SUCCESS;
}
static void other_fill(int16_t *frames, uint32_t count, void *ctx)
{ (void)frames; (void)count; (void)ctx; }
static void confirm(void)
{ fall_state = FALL_STATE_CONFIRMED; alarm_sound_sync_fall_state(); }
static void release(void)
{ fall_state = FALL_STATE_NORMAL; alarm_sound_sync_fall_state(); }
static void external_stop(void) { device_state = AUDIO_STATE_READY; }
static void reset_test(void)
{
    fall_state = FALL_STATE_NORMAL;
    device_state = AUDIO_STATE_READY;
    producer = 0;
    starts = stops = now_ms = 0;
    locked = errors = 0;
    fail_start = timeout_stop = fail_event = fail_init = false;
    start_hook = stop_hook = unlock_hook = 0;
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
    reset_test();
    /* Request survives missing event objects and device initialization. */
    device_state = AUDIO_STATE_UNINITIALIZED;
    confirm();
    CHECK(s_desired_pattern == ALARM_PATTERN_EMERGENCY && s_alarm_flgid == 0);
    alarm_reconcile();
    CHECK(starts == 0 && s_retry_pending);
    device_state = AUDIO_STATE_READY;
    now_ms = 499; alarm_reconcile(); CHECK(starts == 0);
    now_ms = 500; alarm_reconcile(); CHECK(starts == 1 && alarm_owns_stream());
    uint32_t seq = s_request_seq;
    confirm(); alarm_reconcile(); CHECK(starts == 1 && seq == s_request_seq);
    CHECK(alarm_sound_start(ALARM_PATTERN_BEEP) == FSP_ERR_IN_USE);
    CHECK(alarm_sound_stop() == FSP_ERR_IN_USE);
    CHECK(alarm_sound_set_pattern(ALARM_PATTERN_CAUTION) == FSP_ERR_IN_USE);
    release(); CHECK((s_gen_request & ALARM_GEN_PATTERN_MASK) == ALARM_PATTERN_NONE);
    alarm_reconcile(); CHECK(stops == 1 && !s_fall_cleanup && !errors);

    reset_test(); confirm(); start_hook = release; alarm_reconcile();
    CHECK(starts == 1 && stops == 1 && !s_fall_cleanup && !alarm_owns_stream());
    CHECK(!errors);

    reset_test(); confirm(); alarm_reconcile();
    release(); stop_hook = confirm; alarm_reconcile();
    CHECK(starts == 2 && stops == 1 && alarm_owns_stream() && !errors);

    reset_test(); confirm(); fail_start = true; alarm_reconcile();
    CHECK(starts == 1 && s_failure_history == FSP_ERR_INTERNAL);
    now_ms = 1499; alarm_reconcile(); CHECK(starts == 1);
    now_ms = 1500; fail_start = false; alarm_reconcile(); CHECK(starts == 2);
    CHECK(s_failure_history == FSP_ERR_INTERNAL && !errors);

    reset_test(); confirm(); fail_start = true; alarm_reconcile();
    release(); alarm_reconcile();
    CHECK(!s_fall_cleanup && starts == 1 && !errors);

    reset_test(); fail_init = true;
    CHECK(alarm_sound_init() == FSP_ERR_INTERNAL);
    confirm(); alarm_reconcile(); CHECK(alarm_owns_stream());
    fail_init = false; CHECK(alarm_sound_init() == FSP_SUCCESS);
    release(); alarm_reconcile(); CHECK(!s_fall_cleanup && !errors);

    /* #208: lower start returns success after an external stop. */
    reset_test(); confirm(); start_hook = external_stop; alarm_reconcile();
    CHECK(!alarm_owns_stream() && s_retry_pending && starts == 1);
    now_ms = 499; alarm_reconcile(); CHECK(starts == 1);
    now_ms = 500; alarm_reconcile(); CHECK(starts == 2 && alarm_owns_stream());
    external_stop(); alarm_reconcile(); CHECK(alarm_owns_stream());
    CHECK(!errors);

    /* Never reset/stop another producer, then recover after it releases. */
    reset_test(); producer = other_fill; device_state = AUDIO_STATE_PLAYING;
    s_gen_applied = 123; confirm(); alarm_reconcile();
    CHECK(starts == 0 && stops == 0 && s_gen_applied == 123);
    now_ms = 500; device_state = AUDIO_STATE_READY; alarm_reconcile();
    CHECK(starts == 1 && alarm_owns_stream() && !errors);

    reset_test(); confirm(); alarm_reconcile(); timeout_stop = true;
    release(); alarm_reconcile();
    CHECK(s_fall_cleanup && !s_active && device_state == AUDIO_STATE_STOPPING);
    CHECK(alarm_sound_start(ALARM_PATTERN_BEEP) == FSP_ERR_IN_USE);
    now_ms = 499; alarm_reconcile(); CHECK(s_fall_cleanup && stops == 1);
    now_ms = 500; alarm_reconcile(); CHECK(s_fall_cleanup && stops == 2);
    now_ms = 1000; device_state = AUDIO_STATE_READY; alarm_reconcile();
    CHECK(!s_fall_cleanup && !errors);

    reset_test(); confirm(); alarm_reconcile(); timeout_stop = true;
    release(); alarm_reconcile(); CHECK(s_fall_cleanup);
    timeout_stop = false;
    now_ms = 500; alarm_reconcile();
    CHECK(!s_fall_cleanup && stops == 2 && device_state == AUDIO_STATE_READY);
    CHECK(s_failure_history == FSP_ERR_TIMEOUT && !errors);

    reset_test(); confirm(); alarm_reconcile(); device_state = AUDIO_STATE_ERROR;
    release(); alarm_reconcile(); CHECK(s_fall_cleanup && s_retry_pending);
    now_ms = 500; alarm_reconcile(); CHECK(s_fall_cleanup && stops == 0);
    device_state = AUDIO_STATE_READY; now_ms = 1000; alarm_reconcile();
    CHECK(!s_fall_cleanup && !errors);

    /* Lost wakeup does not erase durable request. */
    reset_test(); CHECK(alarm_sound_init() == FSP_SUCCESS);
    fail_event = true; confirm(); CHECK(s_event_failures == 1);
    alarm_reconcile(); CHECK(alarm_owns_stream());
    release(); alarm_reconcile(); CHECK(!s_fall_cleanup && !errors);

    /* Release before a delayed sync re-reads state cannot resurrect alarm. */
    reset_test(); fall_state = FALL_STATE_CONFIRMED; release();
    alarm_sound_sync_fall_state(); alarm_reconcile(); CHECK(starts == 0);
    /* Manual publish guard and publication share dispatch lock. */
    CHECK(alarm_sound_init() == FSP_SUCCESS);
    unlock_hook = confirm;
    CHECK(alarm_sound_start(ALARM_PATTERN_BEEP) == FSP_SUCCESS);
    alarm_reconcile(); CHECK(s_desired_pattern == ALARM_PATTERN_EMERGENCY);
    release(); alarm_reconcile();
    CHECK(alarm_sound_start(ALARM_PATTERN_BEEP) == FSP_SUCCESS);
    alarm_reconcile();
    s_finished_req = s_gen_request;
    alarm_reconcile(); CHECK(!alarm_owns_stream());
    uint32_t old_starts = starts;
    alarm_reconcile(); CHECK(starts == old_starts && !errors);
    return 0;
}
