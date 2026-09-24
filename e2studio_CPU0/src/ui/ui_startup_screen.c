/* Startup UI. All mutable state is owned by lvgl_task, including callbacks.
 * No ISR/other task entry points. The image is immutable after decode.
 */
#include "ui_startup_screen.h"
#include "ui_startup_image.h"
#include "port/sdram_port.h"

#define STARTUP_DURATION_MS 5000U

static uint8_t s_pixels[UI_STARTUP_IMAGE_BYTES] SDRAM_SECTION_NOINIT
    __attribute__((aligned(64)));
static const lv_image_dsc_t s_image = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = UI_STARTUP_IMAGE_WIDTH,
        .h = UI_STARTUP_IMAGE_HEIGHT,
        .stride = UI_STARTUP_IMAGE_WIDTH * 2U,
    },
    .data_size = sizeof(s_pixels),
    .data = s_pixels,
};

static lv_obj_t *s_screen;
static lv_obj_t *s_return_screen;
static lv_timer_t *s_timer;
static bool s_waiting_first_frame;

static void first_frame_cb(lv_event_t *event)
{
    (void)event;
    if (s_waiting_first_frame && lv_screen_active() == s_screen) {
        s_waiting_first_frame = false;
        /* Start only after the splash has been rendered, not during decode. */
        lv_timer_reset(s_timer);
        lv_timer_resume(s_timer);
    }
}

static void finish_cb(lv_timer_t *timer)
{
    lv_display_t *display = lv_obj_get_display(s_screen);
    (void)lv_display_remove_event_cb_with_user_data(display, first_frame_cb, NULL);
    lv_screen_load(s_return_screen);
#if LV_USE_PERF_MONITOR
    lv_sysmon_show_performance(display);
#endif
    lv_obj_delete(s_screen); /* Deletes child widgets and their animations. */
    s_screen = NULL;
    s_return_screen = NULL;
    s_timer = NULL;
    lv_timer_delete(timer);
}

bool ui_startup_screen_show(lv_obj_t *return_screen)
{
    if (return_screen == NULL || s_screen != NULL) return false;
    lv_display_t *display = lv_obj_get_display(return_screen);
    if (display == NULL || display != lv_display_get_default() ||
        lv_screen_active() != return_screen) return false;
    if (!ui_startup_image_decode(s_pixels, sizeof(s_pixels))) return false;

#if BSP_CFG_DCACHE_ENABLED
    /* Dave2D reads this buffer by DMA. Publish the completed decode once. */
    SCB_CleanDCache_by_Addr(s_pixels, (int32_t)sizeof(s_pixels));
#endif

    s_screen = lv_obj_create(NULL);
    if (s_screen == NULL) return false;
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0xfffbf7), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    lv_obj_t *image = lv_image_create(s_screen);
    if (image == NULL) goto fail;
    lv_image_set_src(image, &s_image);
    lv_obj_center(image);

    lv_obj_t *backing = lv_obj_create(s_screen);
    if (backing == NULL) goto fail;
    lv_obj_remove_style_all(backing);
    lv_obj_set_size(backing, 84, 84);
    lv_obj_center(backing);
    lv_obj_set_style_radius(backing, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(backing, lv_color_hex(0xfffbf7), 0);
    lv_obj_set_style_bg_opa(backing, LV_OPA_90, 0);
    lv_obj_remove_flag(backing, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *spinner = lv_spinner_create(backing);
    if (spinner == NULL) goto fail;
    lv_obj_set_size(spinner, 64, 64);
    lv_obj_center(spinner);
    lv_obj_set_style_arc_width(spinner, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0xdce9ed), LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x5085a3), LV_PART_INDICATOR);
    lv_spinner_set_anim_params(spinner, 2000, 90);

    s_timer = lv_timer_create(finish_cb, STARTUP_DURATION_MS, NULL);
    if (s_timer == NULL) goto fail;
    lv_timer_pause(s_timer);

    uint32_t event_count = lv_display_get_event_count(display);
    lv_display_add_event_cb(display, first_frame_cb, LV_EVENT_REFR_READY, NULL);
    if (lv_display_get_event_count(display) != event_count + 1U) goto fail;

    s_return_screen = return_screen;
    s_waiting_first_frame = true;
#if LV_USE_PERF_MONITOR
    /* The monitor lives on the display's system layer, above all screens. */
    lv_sysmon_hide_performance(display);
#endif
    lv_screen_load(s_screen);
    return true;

fail:
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    lv_obj_delete(s_screen);
    s_screen = NULL;
    return false;
}
