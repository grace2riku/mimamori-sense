#ifndef UI_STARTUP_SCREEN_H
#define UI_STARTUP_SCREEN_H

#include "lvgl.h"

/* Call once from lvgl_task after main-screen initialization, before its first
 * timer_handler. Returns false with the existing screen still active on failure.
 * return_screen must remain alive throughout the startup screen's lifetime.
 */
bool ui_startup_screen_show(lv_obj_t *return_screen);

#endif
