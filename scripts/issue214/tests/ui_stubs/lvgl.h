#ifndef TEST_LVGL_H
#define TEST_LVGL_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct lv_obj_t {
    struct lv_obj_t *parent, *child;
    uint32_t flags, state, selected, count;
    bool live;
    char text[512];
} lv_obj_t;
typedef struct { bool paused; } lv_timer_t;
typedef struct { int unused; } lv_event_t;
typedef void (*lv_event_cb_t)(lv_event_t *);
typedef uint32_t lv_color_t;
#define LV_PART_MAIN 0
#define LV_PART_SELECTED 1
#define LV_OPA_COVER 255
#define LV_OPA_50 128
#define LV_OBJ_FLAG_SCROLLABLE 1
#define LV_OBJ_FLAG_CLICKABLE 2
#define LV_OBJ_FLAG_HIDDEN 4
#define LV_STATE_DISABLED 1
#define LV_ALIGN_TOP_MID 0
#define LV_TEXT_ALIGN_CENTER 0
#define LV_EVENT_VALUE_CHANGED 1
#define LV_EVENT_CLICKED 2
#define LV_ROLLER_MODE_NORMAL 0
#define LV_ANIM_OFF 0
static const int lv_font_montserrat_16 = 16, lv_font_montserrat_20 = 20;
static lv_obj_t *lv_obj_create(lv_obj_t *parent);
#define lv_label_create lv_obj_create
#define lv_roller_create lv_obj_create
#define lv_button_create lv_obj_create
static void lv_obj_delete(lv_obj_t *obj);
static void lv_label_set_text(lv_obj_t *obj, const char *text);
static lv_obj_t *lv_screen_active(void);
static void lv_screen_load(lv_obj_t *obj);
static lv_timer_t *lv_timer_create(void (*cb)(lv_timer_t *), uint32_t period, void *data);
static uint32_t lv_tick_get(void);
static uint32_t lv_tick_elaps(uint32_t start);
static void lv_roller_set_options(lv_obj_t *obj, const char *options, int mode);
static inline void lv_roller_set_selected(lv_obj_t *o, uint32_t i, int anim) { (void)anim; o->selected = i; }
static inline uint32_t lv_roller_get_selected(lv_obj_t *o) { return o->selected; }
static inline void lv_obj_add_flag(lv_obj_t *o, uint32_t f) { o->flags |= f; }
static inline void lv_obj_remove_flag(lv_obj_t *o, uint32_t f) { o->flags &= ~f; }
static inline void lv_obj_add_state(lv_obj_t *o, uint32_t s) { o->state |= s; }
static inline void lv_obj_remove_state(lv_obj_t *o, uint32_t s) { o->state &= ~s; }
static inline lv_obj_t *lv_obj_get_child(lv_obj_t *o, int i) { (void)i; return o->child; }
static inline void lv_timer_pause(lv_timer_t *t) { t->paused = true; }
static inline void lv_timer_resume(lv_timer_t *t) { t->paused = false; }
#define lv_color_make(r,g,b) ((lv_color_t)(r))
#define lv_color_white() ((lv_color_t)0xffffff)
#define lv_obj_set_style_bg_color(o,v,p) ((void)(o),(void)(v),(void)(p))
#define lv_obj_set_style_bg_opa(o,v,p) ((void)(o),(void)(v),(void)(p))
#define lv_obj_set_style_border_width(o,v,p) ((void)(o),(void)(v),(void)(p))
#define lv_obj_set_style_radius(o,v,p) ((void)(o),(void)(v),(void)(p))
#define lv_obj_set_style_shadow_width(o,v,p) ((void)(o),(void)(v),(void)(p))
#define lv_obj_set_style_text_align(o,v,p) ((void)(o),(void)(v),(void)(p))
#define lv_obj_set_style_text_color(o,v,p) ((void)(o),(void)(v),(void)(p))
#define lv_obj_set_style_text_font(o,v,p) ((void)(o),(void)(v),(void)(p))
#define lv_obj_align(o,a,x,y) ((void)(o),(void)(a),(void)(x),(void)(y))
#define lv_obj_set_pos(o,x,y) ((void)(o),(void)(x),(void)(y))
#define lv_obj_set_size(o,w,h) ((void)(o),(void)(w),(void)(h))
#define lv_obj_set_width(o,w) ((void)(o),(void)(w))
#define lv_obj_center(o) ((void)(o))
#define lv_obj_add_event_cb(o,cb,event,data) ((void)(o),(void)(cb),(void)(event),(void)(data))
#define lv_roller_set_visible_row_count(o,n) ((void)(o),(void)(n))
#endif
