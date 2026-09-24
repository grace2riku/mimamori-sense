/* Executes production UI callbacks with a deterministic LVGL/cache double.
 * Rendering and actual touch hit-testing still require target verification. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "ui_stubs/lvgl.h"
#include "../../../e2studio_CPU0/src/time_cache.h"

static lv_obj_t objects[64], origin;
static lv_obj_t *active = &origin;
static lv_timer_t timer;
static unsigned allocation_count, fail_allocation, live_count;
static bool fail_timer, cache_valid, accept_request, result_ready;
static unsigned requests, refresh_count;
static uint32_t tick, request_seq, result_seq;
static time_ctrl_time_t cached, submitted;
static time_ctrl_err_t write_result, read_result;
static char datetime_text[40];

char *strcpy(char *dst, const char *src)
{
    char *start = dst;
    while ((*dst++ = *src++) != 0) {}
    return start;
}
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}
char *strstr(const char *s, const char *needle)
{
    for (; *s; ++s) {
        const char *a = s, *b = needle;
        while (*a && *b && *a == *b) { ++a; ++b; }
        if (!*b) return (char *)s;
    }
    return NULL;
}

/* Only the printf conversions used by the two production files. */
int snprintf(char *out, size_t size, const char *fmt, ...)
{
    va_list ap;
    size_t n = 0;
    va_start(ap, fmt);
#define PUT(c) do { if (n + 1 < size) out[n] = (c); ++n; } while (0)
    while (*fmt) {
        if (*fmt != '%') { PUT(*fmt++); continue; }
        ++fmt;
        if (*fmt == 's') {
            const char *s = va_arg(ap, const char *);
            while (*s) { PUT(*s++); }
            ++fmt;
        } else {
            unsigned width = 0, count = 0;
            char digits[12];
            if (*fmt == '0') ++fmt;
            while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
            unsigned v = va_arg(ap, unsigned);
            do { digits[count++] = (char)('0' + v % 10); v /= 10; } while (v);
            while (width > count) { PUT('0'); --width; }
            while (count) { PUT(digits[--count]); }
            ++fmt;
        }
    }
    if (size) out[n < size ? n : size - 1] = 0;
    va_end(ap);
    return (int)n;
#undef PUT
}

static lv_obj_t *lv_obj_create(lv_obj_t *parent)
{
    ++allocation_count;
    if (allocation_count == fail_allocation) return NULL;
    for (unsigned i = 0; i < 64; ++i) {
        if (!objects[i].live) {
            lv_obj_t *o = &objects[i];
            memset(o, 0, sizeof(*o));
            o->live = true; o->parent = parent; ++live_count;
            if (parent && !parent->child) parent->child = o;
            return o;
        }
    }
    return NULL;
}
static void lv_obj_delete(lv_obj_t *o)
{
    for (unsigned i = 0; i < 64; ++i)
        if (objects[i].live && objects[i].parent == o) lv_obj_delete(&objects[i]);
    o->live = false; --live_count;
}
static void lv_label_set_text(lv_obj_t *o, const char *s) { strcpy(o->text, s); }
static lv_obj_t *lv_screen_active(void) { return active; }
static void lv_screen_load(lv_obj_t *o) { active = o; }
static lv_timer_t *lv_timer_create(void (*cb)(lv_timer_t *), uint32_t p, void *d)
{ (void)cb; (void)p; (void)d; return fail_timer ? NULL : &timer; }
static uint32_t lv_tick_get(void) { return tick; }
static uint32_t lv_tick_elaps(uint32_t start) { return tick - start; }
static void lv_roller_set_options(lv_obj_t *o, const char *s, int mode)
{
    (void)mode;
    o->count = 1; o->selected = 0;
    while (*s) if (*s++ == '\n') ++o->count;
}
bool time_cache_init(void) { return true; }
bool time_cache_get(time_ctrl_time_t *out) { if (cache_valid) *out = cached; return cache_valid; }
bool time_cache_set_request(const time_ctrl_time_t *in, uint32_t *seq)
{
    if (!accept_request) return false;
    submitted = *in; *seq = ++request_seq; ++requests; return true;
}
bool time_cache_set_result(uint32_t seq, time_ctrl_err_t *err, time_ctrl_err_t *read_err)
{
    if (!result_ready || result_seq != seq) return false;
    *err = write_result; *read_err = read_result; return true;
}
void ui_main_screen_set_datetime(const char *text) { strcpy(datetime_text, text); ++refresh_count; }

#include "../../../e2studio_CPU0/src/ui/ui_datetime.c"
#include "../../../e2studio_CPU0/src/ui/ui_time_setting_screen.c"

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static void finish(time_ctrl_err_t write_err, time_ctrl_err_t read_err)
{
    write_result = write_err; read_result = read_err;
    result_seq = request_seq; result_ready = true;
    cached = submitted; cache_valid = (read_err == TIME_CTRL_OK);
    ui_time_setting_ack_timer_cb(NULL);
    result_ready = false;
}
int run_tests(void)
{
    /* Every checked object allocation and timer allocation must clean up. */
    for (unsigned n = 1; n <= 18; ++n) {
        allocation_count = 0; fail_allocation = n;
        ui_time_setting_screen_open();
        CHECK(s_screen == NULL && active == &origin && live_count == 0);
    }
    fail_allocation = 0; fail_timer = true;
    ui_time_setting_screen_open();
    CHECK(!s_screen && live_count == 0);
    fail_timer = false;
    ui_time_setting_screen_open();
    CHECK(active == s_screen && strstr(s_msg_label->text, "unavailable"));
    CHECK(lv_roller_get_selected(s_roller_year) == 26);
    ui_time_setting_cancel_cb(NULL);
    CHECK(active == &origin && requests == 0);
    ui_time_setting_screen_open();
    ui_time_setting_screen_open();
    CHECK(s_return_screen == &origin);

    CHECK(ui_time_setting_days_in_month(2000,2) == 29);
    CHECK(ui_time_setting_days_in_month(2028,2) == 29);
    CHECK(ui_time_setting_days_in_month(2027,2) == 28);
    CHECK(ui_time_setting_days_in_month(2026,4) == 30);
    CHECK(ui_time_setting_days_in_month(2026,1) == 31);
    time_ctrl_time_t value = {2028, 1, 31, 23, 59, 59, 0};
    ui_time_setting_load_time(&value);
    lv_roller_set_selected(s_roller_mon, 1, 0);
    ui_time_setting_date_changed_cb(NULL);
    CHECK(s_mday_opt_cnt == 29 && s_roller_mday->selected == 28);
    lv_roller_set_selected(s_roller_year, 27, 0);
    ui_time_setting_date_changed_cb(NULL);
    CHECK(s_mday_opt_cnt == 28 && s_roller_mday->selected == 27);

    accept_request = false;
    ui_time_setting_ok_cb(NULL);
    CHECK(!s_pending && requests == 0 && strstr(s_msg_label->text, "unavailable"));
    accept_request = true;
    tick = 0xffffff00U; /* elapsed arithmetic must also work across wrap. */
    ui_time_setting_ok_cb(NULL);
    ui_time_setting_ok_cb(NULL);
    CHECK(s_pending && requests == 1 && submitted.sec == 0);
    CHECK(!(s_input_cover->flags & LV_OBJ_FLAG_HIDDEN));
    ui_time_setting_cancel_cb(NULL);
    CHECK(active == s_screen);
    tick += 2999;
    ui_time_setting_ack_timer_cb(NULL);
    CHECK(!s_unconfirmed);
    tick += 1;
    ui_time_setting_ack_timer_cb(NULL);
    CHECK(s_unconfirmed && !strcmp(s_cancel_label->text, "Back"));
    ui_time_setting_cancel_cb(NULL);
    CHECK(active == &origin && !timer.paused && s_pending);
    ui_time_setting_screen_open();
    ui_time_setting_ok_cb(NULL);
    CHECK(requests == 1 && s_pending);
    result_ready = true; result_seq = request_seq + 1;
    ui_time_setting_ack_timer_cb(NULL);
    CHECK(s_pending);
    result_ready = false;
    ui_time_setting_cancel_cb(NULL);
    finish(TIME_CTRL_OK, TIME_CTRL_OK);
    CHECK(active == &origin && !s_pending && timer.paused);
    CHECK(!strcmp(datetime_text, "2027-02-28 23:59") && refresh_count == 1);
    ui_time_setting_screen_open();
    CHECK(!strcmp(s_msg_label->text, "Time set"));

    const time_ctrl_err_t errors[] = {TIME_CTRL_ERR_INVALID_ARG, TIME_CTRL_ERR_BUSY,
                                    TIME_CTRL_ERR_HW, TIME_CTRL_ERR_NOT_INIT};
    for (unsigned i = 0; i < sizeof(errors)/sizeof(errors[0]); ++i) {
        ui_time_setting_ok_cb(NULL);
        finish(errors[i], TIME_CTRL_ERR_NOT_SET);
        CHECK(active == s_screen && !s_pending && !s_ok_btn->state);
        CHECK(!strcmp(s_msg_label->text, ui_time_setting_err_text(errors[i])));
    }
    ui_time_setting_ok_cb(NULL);
    finish(TIME_CTRL_OK, TIME_CTRL_ERR_HW);
    CHECK(active == s_screen && !s_pending && !s_ok_btn->state);
    CHECK(!strcmp(s_msg_label->text, "Time set; readback unavailable"));
    CHECK(!strcmp(datetime_text, "--:--"));
    ui_time_setting_ok_cb(NULL);
    finish(TIME_CTRL_OK, TIME_CTRL_OK);
    CHECK(active == &origin && !strcmp(datetime_text, "2027-02-28 23:59"));
    ui_time_setting_screen_open();
    ui_time_setting_ok_cb(NULL);
    tick += 3000;
    ui_time_setting_ack_timer_cb(NULL);
    ui_time_setting_cancel_cb(NULL);
    finish(TIME_CTRL_ERR_BUSY, TIME_CTRL_ERR_NOT_SET);
    CHECK(active == &origin);
    ui_time_setting_screen_open();
    CHECK(!strcmp(s_msg_label->text, "RTC busy - try again"));
    ui_time_setting_cancel_cb(NULL);
    ui_time_setting_screen_open();
    CHECK(strcmp(s_msg_label->text, "RTC busy - try again") != 0);
    return 0;
}
