/* Exercise the production screen with actual LVGL timers and widgets.
 * Display refresh is paused: the harness injects the first-frame event.
 * This does not test the physical display, touch controller, or Dave2D.
 */
#include "lvgl.h"
#include "src/display/lv_display_private.h"
#include "ui/ui_startup_screen.h"

static lv_obj_t *main_screen;
static lv_display_t *display;
static uint32_t base_events;
static size_t free_before;
static uint8_t framebuffer[1024 * 600 * 2] __attribute__((aligned(64)));

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(disp);
}

uint8_t *test_preview(void)
{
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, framebuffer, NULL, sizeof(framebuffer), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, flush_cb);
    lv_refr_now(display);
    return framebuffer;
}

void test_init(void)
{
    lv_init();
    display = lv_display_create(1024, 600);
    lv_timer_pause(lv_display_get_refr_timer(display));
    /* Keep FPS text allocations out of the screen-lifecycle heap comparison. */
    lv_timer_pause(display->perf_sysmon_backend.timer);
    main_screen = lv_obj_create(NULL);
    lv_screen_load(main_screen);
    base_events = lv_display_get_event_count(display);
}

int test_start(void)
{
    lv_mem_monitor_t monitor;
    lv_mem_monitor(&monitor);
    free_before = monitor.free_size;
    return ui_startup_screen_show(main_screen);
}

int test_duplicate(void) { return ui_startup_screen_show(main_screen); }
int test_null(void) { return ui_startup_screen_show(NULL); }
void test_frame(void) { lv_display_send_event(display, LV_EVENT_REFR_READY, NULL); }
void test_tick(uint32_t ms) { lv_tick_inc(ms); (void)lv_timer_handler(); }
uint32_t test_now(void) { return lv_tick_get(); }
int test_on_main(void) { return lv_screen_active() == main_screen; }
int test_perf_visible(void)
{
    return display->perf_label != NULL &&
           !lv_obj_has_flag(display->perf_label, LV_OBJ_FLAG_HIDDEN);
}
uint32_t test_animation_count(void) { return lv_anim_count_running(); }
uint32_t test_angle(void)
{
    lv_obj_t *backing = lv_obj_get_child(lv_screen_active(), 1);
    return (uint32_t)lv_arc_get_angle_end(lv_obj_get_child(backing, 0));
}
int test_cleanup(void)
{
    lv_mem_monitor_t monitor;
    lv_mem_monitor(&monitor);
    return (lv_display_get_event_count(display) == base_events ? 1 : 0) |
           (lv_anim_count_running() == 0 ? 2 : 0) |
           (monitor.free_size >= free_before ? 4 : 0);
}
int test_heap_delta(void)
{
    lv_mem_monitor_t monitor;
    lv_mem_monitor(&monitor);
    return (int)monitor.free_size - (int)free_before;
}
