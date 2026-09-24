#ifndef UI_STARTUP_IMAGE_H
#define UI_STARTUP_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UI_STARTUP_IMAGE_WIDTH  1024U
#define UI_STARTUP_IMAGE_HEIGHT 600U
#define UI_STARTUP_IMAGE_BYTES  (UI_STARTUP_IMAGE_WIDTH * UI_STARTUP_IMAGE_HEIGHT * 2U)

/* LVGL task only, before rendering. The buffer is unusable on false. */
bool ui_startup_image_decode(uint8_t *pixels, size_t size);

#endif
