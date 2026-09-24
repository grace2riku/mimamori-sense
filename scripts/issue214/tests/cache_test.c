/* Execute the actual cache implementation with deterministic kernel/RTC mocks.
 * The worker helper lets the test control task scheduling without OS threads. */
#include "../../../e2studio_CPU0/src/time_cache.c"

static int failure;
#define CHECK(x) do { if (!(x) && !failure) failure = __LINE__; } while (0)
static int locked, cre_flg_calls, cre_tsk_calls, del_flg_calls, del_tsk_calls;
static int set_calls, get_calls, notify_calls, observe_completion;
static int flg_ret, tsk_ret, start_ret, notify_ret, dis_ret;
static uint32_t tick;
static time_ctrl_err_t set_ret, get_ret;
static time_ctrl_time_t rtc, written;

ER tk_dis_dsp(void) { if (dis_ret) return dis_ret; CHECK(!locked); locked = 1; return E_OK; }
ER tk_ena_dsp(void) {
    CHECK(locked); locked = 0;
    if (observe_completion && !s_set_pending) {
        CHECK(s_pub_valid == (get_ret == TIME_CTRL_OK));
        if (s_pub_valid) CHECK(s_pub_time.sec == rtc.sec);
        observe_completion = 0;
    }
    return E_OK;
}
ER tk_get_otm(SYSTIM *p) { CHECK(!locked); p->lo = tick; return E_OK; }
ID tk_cre_flg(const T_CFLG *p) { CHECK(p->iflgptn == 0); cre_flg_calls++; return flg_ret; }
ID tk_cre_tsk(const T_CTSK *p) { CHECK(p->task == time_cache_task); CHECK(s_flgid > 0); cre_tsk_calls++; return tsk_ret; }
ER tk_sta_tsk(ID id, INT code) { CHECK(id == tsk_ret && code == 0); CHECK(s_flgid > 0); return start_ret; }
ER tk_del_flg(ID id) { CHECK(id == flg_ret); del_flg_calls++; return E_OK; }
ER tk_del_tsk(ID id) { CHECK(id == tsk_ret); del_tsk_calls++; return E_OK; }
ER tk_set_flg(ID id, UINT bits) { CHECK(!locked); CHECK(id == s_flgid && bits == TIME_CACHE_FLG_SET); notify_calls++; return notify_ret; }
ER tk_wai_flg(ID id, UINT bits, UINT mode, UINT *p, TMO t) { (void)id; (void)bits; (void)mode; (void)p; CHECK(!locked && t == 1000); return -1; }

time_ctrl_err_t time_ctrl_set(const time_ctrl_time_t *p) {
    CHECK(!locked && s_set_pending); written = *p; set_calls++;
    return set_ret;
}
time_ctrl_err_t time_ctrl_get(time_ctrl_time_t *p) {
    CHECK(!locked); get_calls++;
    if (s_set_pending) {
        time_ctrl_err_t a = TIME_CTRL_ERR_BUSY, b = TIME_CTRL_ERR_BUSY;
        uint32_t token = 999;
        CHECK(!s_pub_valid); /* old snapshot invalidated before readback */
        CHECK(!time_cache_set_result(s_set_seq, &a, &b));
        CHECK(a == TIME_CTRL_ERR_BUSY && b == TIME_CTRL_ERR_BUSY);
        CHECK(!time_cache_set_request(&rtc, &token) && token == 999);
    }
    *p = rtc; return get_ret;
}

static void reset(void) {
    s_tskid = s_flgid = 0; s_pub_valid = false; s_pub_tick = 0;
    s_set_seq = 0; s_set_pending = false;
    locked = cre_flg_calls = cre_tsk_calls = del_flg_calls = del_tsk_calls = 0;
    set_calls = get_calls = notify_calls = observe_completion = 0;
    flg_ret = 10; tsk_ret = 20; start_ret = notify_ret = dis_ret = 0;
    tick = 100; set_ret = get_ret = TIME_CTRL_OK;
    rtc = (time_ctrl_time_t){2026, 9, 24, 12, 34, 57, 4};
}

int run_tests(void) {
    uint32_t token, next;
    time_ctrl_time_t input, output;
    time_ctrl_err_t a, b;
    reset(); token = 42;
    CHECK(!time_cache_set_request(&rtc, &token) && token == 42);
    flg_ret = -1; CHECK(!time_cache_init()); CHECK(cre_tsk_calls == 0 && s_flgid == 0);
    flg_ret = 10; CHECK(time_cache_init()); CHECK(time_cache_init()); CHECK(cre_tsk_calls == 1);
    reset(); tsk_ret = -1; CHECK(!time_cache_init()); CHECK(del_flg_calls == 1 && s_flgid == 0);
    tsk_ret = 20; CHECK(time_cache_init());
    reset(); start_ret = -1; CHECK(!time_cache_init()); CHECK(del_flg_calls == 1 && del_tsk_calls == 1 && s_tskid == 0);
    start_ret = 0; CHECK(time_cache_init());

    reset(); CHECK(time_cache_init()); input = rtc; input.sec = 56;
    CHECK(!time_cache_set_request(0, &token)); CHECK(!time_cache_set_request(&input, 0));
    dis_ret = -1; CHECK(!time_cache_set_request(&input, &token)); dis_ret = 0;
    CHECK(time_cache_set_request(&input, &token)); CHECK(token == 1 && set_calls == 0);
    input.sec = 1; next = 99;
    CHECK(!time_cache_set_request(&input, &next) && next == 99);
    a = b = TIME_CTRL_ERR_BUSY;
    CHECK(!time_cache_set_result(token, &a, &b)); CHECK(a == TIME_CTRL_ERR_BUSY);
    tick += 60000; /* UI timeout cannot release worker ownership. */
    CHECK(!time_cache_set_request(&input, &next));
    observe_completion = 1; CHECK(time_cache_apply_request());
    CHECK(written.sec == 56 && set_calls == 1 && get_calls == 1 && !observe_completion);
    CHECK(time_cache_set_result(token, &a, &b) && a == TIME_CTRL_OK && b == TIME_CTRL_OK);
    CHECK(time_cache_get(&output) && output.sec == 57);
    CHECK(!time_cache_apply_request() && set_calls == 1);
    CHECK(!time_cache_set_result(0, &a, &b)); CHECK(!time_cache_set_result(token + 1, &a, &b));
    CHECK(!time_cache_set_result(token, 0, &b));
    s_set_seq = UINT32_MAX;
    CHECK(time_cache_set_request(&input, &next) && next == 1);
    get_ret = TIME_CTRL_ERR_HW; observe_completion = 1;
    CHECK(time_cache_apply_request());
    CHECK(time_cache_set_result(next, &a, &b) && a == TIME_CTRL_OK && b == TIME_CTRL_ERR_HW);
    CHECK(!time_cache_get(&output) && !observe_completion);

    /* Rejected writes never trigger readback or overwrite the cached time. */
    const time_ctrl_err_t errors[] = {TIME_CTRL_ERR_INVALID_ARG, TIME_CTRL_ERR_BUSY, TIME_CTRL_ERR_HW, TIME_CTRL_ERR_NOT_INIT};
    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
        reset(); CHECK(time_cache_init()); time_cache_publish(true, &rtc);
        set_ret = errors[i]; CHECK(time_cache_set_request(&input, &token));
        CHECK(time_cache_apply_request()); CHECK(get_calls == 0 && set_calls == 1);
        CHECK(time_cache_set_result(token, &a, &b) && a == errors[i] && b == TIME_CTRL_ERR_NOT_SET);
        CHECK(time_cache_get(&output) && output.sec == rtc.sec);
    }
    reset(); CHECK(time_cache_init()); notify_ret = -1;
    CHECK(time_cache_set_request(&rtc, &token) && notify_calls == 1);
    tick += TIME_CACHE_POLL_PERIOD_MS; /* periodic worker wake after lost notification */
    CHECK(time_cache_apply_request()); CHECK(time_cache_set_result(token, &a, &b));
    CHECK(set_calls == 1 && a == TIME_CTRL_OK);
    return failure;
}
