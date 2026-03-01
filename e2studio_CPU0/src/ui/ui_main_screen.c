/**
 * @file ui_main_screen.c
 * @brief Main screen UI implementation for camera image display
 * @details
 * Implements the primary screen layout for the mimamori-sense device.
 *
 * Screen Layout (ASCII Art):
 * +------------------------------------------------------------------+
 * | Status Bar (40px)                                                 |
 * | [status_label]        [datetime_label]          [settings_btn]    |
 * +------------------------------------------------------------------+
 * |                                                                    |
 * |                                                                    |
 * |              Camera Image Area (1024 x 560)                        |
 * |              lv_image backed by SDRAM buffer                       |
 * |              RGB565 format, test pattern on init                   |
 * |                                                                    |
 * |                                                                    |
 * +------------------------------------------------------------------+
 *
 * Widget Hierarchy:
 *   screen (lv_obj, 1024x600)
 *     +-- status_bar (lv_obj, 1024x40, top)
 *     |     +-- status_label (lv_label, left-aligned)
 *     |     +-- datetime_label (lv_label, center-aligned)
 *     |     +-- settings_btn (lv_button, right-aligned)
 *     |           +-- settings_label (lv_label, gear icon text)
 *     +-- camera_image (lv_image, 1024x560, below status bar)
 *
 * Camera Buffer Architecture:
 *   The camera pixel buffer is a static array in SDRAM (.sdram section),
 *   1024 * 560 * 2 = 1,146,880 bytes. An lv_image_dsc_t descriptor
 *   references this buffer with RGB565 color format. The lv_image widget
 *   displays this descriptor.
 *
 *   Frame update sequence:
 *     1. Camera driver writes RGB565 pixels to camera_buf[]
 *     2. Caller invokes ui_main_screen_invalidate_camera()
 *     3. LVGL redraws the image region on next lv_timer_handler()
 *
 * Reference:
 *   - Issue #8 (F-001-7)
 *   - LVGL lv_image: e2studio_CPU0/ra/lvgl/lvgl/src/widgets/image/lv_image.h
 *   - LVGL lv_image_dsc_t: e2studio_CPU0/ra/lvgl/lvgl/src/draw/lv_image_dsc.h
 *   - SDRAM sections: e2studio_CPU0/src/port/sdram_port.h
 *
 * @note This file is part of the LVGL UI implementation (F-001-7).
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include "ui_main_screen.h"
#include "port/sdram_port.h"    /* SDRAM_SECTION_ZERO for buffer placement */
#include <string.h>             /* memset */

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Number of color bars in the test pattern */
#define TEST_PATTERN_BAR_COUNT      (8)

/** Width of each color bar in pixels */
#define TEST_PATTERN_BAR_WIDTH      (UI_CAMERA_WIDTH / TEST_PATTERN_BAR_COUNT)

/** Status bar padding (horizontal) */
#define STATUS_BAR_PAD_H            (12)

/** Status bar padding (vertical) */
#define STATUS_BAR_PAD_V            (4)

/** Settings button width */
#define SETTINGS_BTN_WIDTH          (80)

/** Settings button height */
#define SETTINGS_BTN_HEIGHT         (32)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/**
 * Camera image pixel buffer in SDRAM
 *
 * Placed in the .sdram section (zero-initialized at startup by BSP).
 * Size: 1024 * 560 * 2 = 1,146,880 bytes (~1.1MB)
 *
 * The .sdram section is cached (unlike .sdram_noinit_nocache used for
 * framebuffers). This is acceptable because:
 *   - LVGL reads from this buffer through the CPU cache, which is fast
 *   - Dave2D also accesses this buffer, but Dave2D flushes the data cache
 *     before DMA operations (handled internally by the LVGL Dave2D driver)
 *   - The buffer is written by the CPU (camera ISR or DMA callback),
 *     and SCB_CleanDCache_by_Addr() should be called after writes if
 *     Dave2D is used for rendering
 *
 * Note: Using .sdram (zero-init, cached) rather than .sdram_noinit_nocache
 * (non-cached) to benefit from cache performance for CPU-side pixel
 * manipulation (test pattern drawing, frame updates).
 */
static uint8_t camera_buf[UI_CAMERA_BUF_SIZE]
    SDRAM_SECTION_ZERO
    __attribute__((aligned(64)));

/**
 * LVGL image descriptor for the camera area
 *
 * Points to camera_buf[] with RGB565 format metadata.
 * This descriptor is passed to lv_image_set_src() and must remain
 * valid for the lifetime of the lv_image widget.
 *
 * The header fields are initialized at runtime in ui_main_screen_create()
 * because lv_image_header_t uses bitfields that cannot be portably
 * initialized with designated initializers across all compilers.
 */
static lv_image_dsc_t camera_img_dsc;

/** Pointer to the main screen object */
static lv_obj_t *s_screen = NULL;

/** Pointer to the status bar container */
static lv_obj_t *s_status_bar = NULL;

/** Pointer to the status text label */
static lv_obj_t *s_status_label = NULL;

/** Pointer to the date/time label */
static lv_obj_t *s_datetime_label = NULL;

/** Pointer to the settings button */
static lv_obj_t *s_settings_btn = NULL;

/** Pointer to the camera image widget */
static lv_obj_t *s_camera_image = NULL;

/** Flag indicating whether the main screen has been created */
static bool s_created = false;

/**********************************************************************************************************************
 Private (static) function prototypes
 *********************************************************************************************************************/

static void create_status_bar(lv_obj_t *parent);
static void create_camera_area(lv_obj_t *parent);
static void init_camera_image_descriptor(void);
static void settings_btn_event_cb(lv_event_t *e);
static uint16_t rgb565_pack(uint8_t r, uint8_t g, uint8_t b);

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

lv_obj_t* ui_main_screen_create(void)
{
    /* Prevent double creation */
    if (s_created) {
        return s_screen;
    }

    /*
     * Step 1: Create root screen
     *
     * lv_obj_create(NULL) creates a new screen (not attached to any parent).
     * The screen has no default padding, borders, or scrollbars.
     */
    s_screen = lv_obj_create(NULL);
    if (s_screen == NULL) {
        return NULL;
    }

    /* Set screen background to black (visible if any area is not covered) */
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN);

    /* Remove default padding and scrollbar from the screen */
    lv_obj_set_style_pad_all(s_screen, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * Step 2: Create status bar (top 40px)
     */
    create_status_bar(s_screen);

    /*
     * Step 3: Initialize camera image descriptor and draw test pattern
     */
    init_camera_image_descriptor();
    ui_main_screen_draw_test_pattern();

    /*
     * Step 4: Create camera image area (remaining 1024x560)
     */
    create_camera_area(s_screen);

    /*
     * Step 5: Load this screen as the active screen
     */
    lv_screen_load(s_screen);

    s_created = true;
    return s_screen;
}

lv_obj_t* ui_main_screen_get_camera_image(void)
{
    return s_camera_image;
}

uint8_t* ui_main_screen_get_camera_buffer(void)
{
    if (!s_created) {
        return NULL;
    }
    return camera_buf;
}

void ui_main_screen_invalidate_camera(void)
{
    if (s_camera_image != NULL) {
        /*
         * Only invalidate the widget area to trigger a redraw.
         *
         * Note: lv_image_set_src() is NOT called here. The original code
         * called lv_image_set_src() every frame to force LVGL to re-read
         * the buffer, but this triggers heavy image source processing
         * (cache lookup, header re-read, decode preparation) that caused
         * ~200ms overhead per frame. Since the lv_image_dsc_t descriptor
         * already points to the SDRAM pixel buffer, lv_obj_invalidate()
         * alone is sufficient - Dave2D reads the updated pixels directly
         * when rendering the dirty area.
         */
        lv_obj_invalidate(s_camera_image);
    }
}

void ui_main_screen_set_status(const char *text)
{
    if (s_status_label != NULL && text != NULL) {
        lv_label_set_text(s_status_label, text);
    }
}

void ui_main_screen_set_datetime(const char *text)
{
    if (s_datetime_label != NULL && text != NULL) {
        lv_label_set_text(s_datetime_label, text);
    }
}

bool ui_main_screen_is_created(void)
{
    return s_created;
}

void ui_main_screen_draw_test_pattern(void)
{
    /*
     * Draw 8 vertical color bars into the camera buffer:
     *   Red, Green, Blue, Yellow, Cyan, Magenta, White, Black
     *
     * Each bar is 128 pixels wide (1024 / 8 = 128).
     * The buffer is in RGB565 format (little-endian).
     */
    static const struct {
        uint8_t r, g, b;
    } bar_colors[TEST_PATTERN_BAR_COUNT] = {
        { 0xFF, 0x00, 0x00 },   /* Red */
        { 0x00, 0xFF, 0x00 },   /* Green */
        { 0x00, 0x00, 0xFF },   /* Blue */
        { 0xFF, 0xFF, 0x00 },   /* Yellow */
        { 0x00, 0xFF, 0xFF },   /* Cyan */
        { 0xFF, 0x00, 0xFF },   /* Magenta */
        { 0xFF, 0xFF, 0xFF },   /* White */
        { 0x00, 0x00, 0x00 },   /* Black */
    };

    uint16_t *pixel_buf = (uint16_t *)camera_buf;

    for (int32_t y = 0; y < UI_CAMERA_HEIGHT; y++) {
        for (int32_t bar = 0; bar < TEST_PATTERN_BAR_COUNT; bar++) {
            uint16_t color = rgb565_pack(
                bar_colors[bar].r,
                bar_colors[bar].g,
                bar_colors[bar].b
            );
            int32_t x_start = bar * TEST_PATTERN_BAR_WIDTH;
            int32_t x_end   = x_start + TEST_PATTERN_BAR_WIDTH;
            for (int32_t x = x_start; x < x_end; x++) {
                pixel_buf[y * UI_CAMERA_WIDTH + x] = color;
            }
        }
    }
}

const char* ui_main_screen_get_status_text(void)
{
    if (s_status_label == NULL) {
        return NULL;
    }
    return lv_label_get_text(s_status_label);
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Create the status bar at the top of the screen
 *
 * Layout:
 *   [status_label]  ......  [datetime_label]  ......  [settings_btn]
 *   |<-- left pad ->|                                 |<-- right pad ->|
 *
 * @param parent  Parent screen object
 */
static void create_status_bar(lv_obj_t *parent)
{
    /*
     * Status bar container: full width, 40px height, positioned at top
     */
    s_status_bar = lv_obj_create(parent);
    lv_obj_set_size(s_status_bar, UI_DISPLAY_WIDTH, UI_STATUS_BAR_HEIGHT);
    lv_obj_set_pos(s_status_bar, 0, 0);

    /* Style: dark background, no border, no radius, no scrollbar */
    lv_obj_set_style_bg_color(s_status_bar, UI_STATUS_BAR_COLOR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_status_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_status_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_status_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(s_status_bar, STATUS_BAR_PAD_H, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_status_bar, STATUS_BAR_PAD_H, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_status_bar, STATUS_BAR_PAD_V, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_status_bar, STATUS_BAR_PAD_V, LV_PART_MAIN);
    lv_obj_remove_flag(s_status_bar, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * Status label: left-aligned, shows monitoring state
     * Default text: "Idle" (updated by ui_main_screen_set_status())
     */
    s_status_label = lv_label_create(s_status_bar);
    lv_label_set_text(s_status_label, "Idle");
    lv_obj_set_style_text_color(s_status_label, UI_STATUS_BAR_TEXT_COLOR, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(s_status_label, LV_ALIGN_LEFT_MID, 0, 0);

    /*
     * Date/time label: center-aligned
     * Default text: "--:--" (updated by ui_main_screen_set_datetime())
     */
    s_datetime_label = lv_label_create(s_status_bar);
    lv_label_set_text(s_datetime_label, "--:--");
    lv_obj_set_style_text_color(s_datetime_label, UI_STATUS_BAR_TEXT_COLOR, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_datetime_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_datetime_label, LV_ALIGN_CENTER, 0, 0);

    /*
     * Settings button: right-aligned
     * Uses a text label ("Settings") as placeholder for a gear icon.
     * A future enhancement can replace this with an LV_SYMBOL or custom icon.
     */
    s_settings_btn = lv_button_create(s_status_bar);
    lv_obj_set_size(s_settings_btn, SETTINGS_BTN_WIDTH, SETTINGS_BTN_HEIGHT);
    lv_obj_align(s_settings_btn, LV_ALIGN_RIGHT_MID, 0, 0);

    /* Button style: slightly lighter than status bar background */
    lv_obj_set_style_bg_color(s_settings_btn, lv_color_make(0x2C, 0x3E, 0x50), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_settings_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_settings_btn, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_settings_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_settings_btn, 0, LV_PART_MAIN);

    /* Button label */
    lv_obj_t *btn_label = lv_label_create(s_settings_btn);
    lv_label_set_text(btn_label, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(btn_label, UI_STATUS_BAR_TEXT_COLOR, LV_PART_MAIN);
    lv_obj_center(btn_label);

    /* Register event handler for touch feedback */
    lv_obj_add_event_cb(s_settings_btn, settings_btn_event_cb, LV_EVENT_CLICKED, NULL);
}

/**
 * Create the camera image display area below the status bar
 *
 * @param parent  Parent screen object
 */
static void create_camera_area(lv_obj_t *parent)
{
    /*
     * Create an lv_image widget for the camera display area.
     * The image is positioned directly below the status bar.
     */
    s_camera_image = lv_image_create(parent);
    lv_obj_set_pos(s_camera_image, 0, UI_STATUS_BAR_HEIGHT);
    lv_obj_set_size(s_camera_image, UI_CAMERA_WIDTH, UI_CAMERA_HEIGHT);

    /* Set the image source to our camera buffer descriptor */
    lv_image_set_src(s_camera_image, &camera_img_dsc);

    /* Align image content to top-left (no scaling or centering) */
    lv_image_set_inner_align(s_camera_image, LV_IMAGE_ALIGN_TOP_LEFT);

    /* Disable anti-aliasing (camera frames are pixel-perfect, no transforms) */
    lv_image_set_antialias(s_camera_image, false);

    /* Remove scrollbar and clickable flags from the image area */
    lv_obj_remove_flag(s_camera_image, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_camera_image, LV_OBJ_FLAG_CLICKABLE);
}

/**
 * Initialize the lv_image_dsc_t descriptor for the camera buffer
 *
 * @details Sets up the image header with:
 *   - magic:  LV_IMAGE_HEADER_MAGIC (0x19)
 *   - cf:     LV_COLOR_FORMAT_RGB565
 *   - w:      UI_CAMERA_WIDTH (1024)
 *   - h:      UI_CAMERA_HEIGHT (560)
 *   - stride: UI_CAMERA_STRIDE (2048)
 *   - flags:  0 (no special flags)
 *
 * The data pointer is set to camera_buf (SDRAM).
 */
static void init_camera_image_descriptor(void)
{
    memset(&camera_img_dsc, 0, sizeof(camera_img_dsc));

    camera_img_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    camera_img_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    camera_img_dsc.header.w      = UI_CAMERA_WIDTH;
    camera_img_dsc.header.h      = UI_CAMERA_HEIGHT;
    camera_img_dsc.header.stride = UI_CAMERA_STRIDE;
    camera_img_dsc.header.flags  = 0;

    camera_img_dsc.data_size = UI_CAMERA_BUF_SIZE;
    camera_img_dsc.data      = camera_buf;
}

/**
 * Settings button click event callback
 *
 * @details Called when the settings button is pressed/released.
 *          Currently logs the event for testing purposes. In the future,
 *          this will navigate to the settings screen.
 *
 * @param e  LVGL event object
 */
static void settings_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        /*
         * TODO: F-001-8+ Navigate to settings screen
         * For now, toggle the status text to demonstrate touch responsiveness.
         */
        const char *current = lv_label_get_text(s_status_label);
        if (current != NULL && strcmp(current, "Idle") == 0) {
            lv_label_set_text(s_status_label, "Monitoring");
        } else {
            lv_label_set_text(s_status_label, "Idle");
        }
    }
}

/**
 * Pack RGB888 components into an RGB565 value
 *
 * @details RGB565 bit layout (little-endian):
 *   Bits [15:11] = Red   (5 bits, 0-31)
 *   Bits [10:5]  = Green (6 bits, 0-63)
 *   Bits [4:0]   = Blue  (5 bits, 0-31)
 *
 * @param r  Red component (0-255)
 * @param g  Green component (0-255)
 * @param b  Blue component (0-255)
 * @return   Packed RGB565 value
 */
static uint16_t rgb565_pack(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}
