/**
 * @file fall_detection_screen.c
 * @brief Fall detection overlay UI implementation (F-003-10)
 * @details
 * Implements LVGL overlay widgets for displaying fall detection results
 * on top of the camera image displayed on the LCD (1024x600).
 *
 * Architecture:
 *   Rather than using raw D/AVE 2D calls (as in the reference
 *   face_detection_screen_mipi.c), this module uses LVGL widgets placed
 *   on top of the camera image widget. This approach:
 *     - Integrates cleanly with the existing LVGL display pipeline
 *     - Avoids conflicts between LVGL and raw D/AVE 2D rendering
 *     - Leverages LVGL's built-in Dave2D acceleration for fills and labels
 *     - Supports automatic VSYNC sync via LVGL's render cycle
 *
 *   The reference face_detection_screen_mipi.c uses D/AVE 2D directly
 *   because that project does not use LVGL. This project uses LVGL for
 *   all display management, so the overlay approach is more appropriate.
 *
 * Widget hierarchy (added to main screen):
 *   screen (from ui_main_screen)
 *     +-- ... (existing status bar + camera image)
 *     +-- bbox_obj[0..N]  (LVGL objects with colored borders, transparent fill)
 *     +-- status_label    (fall state text, positioned below camera image)
 *     +-- info_panel      (container with inference time, count, state, FPS)
 *
 * Coordinate transformation:
 *   Detection coordinates (320x240 camera space) are transformed to
 *   absolute LCD coordinates:
 *     lcd_x = STATUS_BAR_OFFSET + CAM_OFFSET_X + (det_x / 320) * 768
 *     lcd_y = STATUS_BAR_HEIGHT + CAM_OFFSET_Y + (det_y / 240) * 450
 *
 * Reference:
 *   - Issue #27 (F-003-10)
 *   - face_detection_screen_mipi.c lines 96-131 (bounding box drawing)
 *   - face_detection_screen_mipi.c lines 139-186 (text overlay)
 *   - camera_display.c lines 146-165 (camera positioning)
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>

#include "fall_detection_screen.h"
#include "ui_main_screen.h"

#include "lvgl.h"

#include "fall_detection_logic.h"
#include "ai_application/fall_detection/fall_detection_postprocess.h"
#include "ai_application/ai_application_config.h"
#include "common_util.h"
#include "camera_display.h"
#include "ai_inference_thread_api.h"
#include "jlink_console.h"

#include "FreeRTOS.h"
#include "event_groups.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size */
#define PRINT_BUF_SIZE          (80)

/**
 * Info panel position: bottom of camera area
 * Camera area: Y=40 to Y=600 (560px height)
 * Info panel placed at the bottom of the screen
 */
#define INFO_PANEL_X            (10)
#define INFO_PANEL_Y            (UI_DISPLAY_HEIGHT - 50)
#define INFO_PANEL_WIDTH        (UI_DISPLAY_WIDTH - 20)
#define INFO_PANEL_HEIGHT       (45)

/**
 * Status text position: above info panel, centered
 */
#define STATUS_TEXT_X           (10)
#define STATUS_TEXT_Y           (UI_DISPLAY_HEIGHT - 95)

/**
 * Scale factor from camera coords (320x240) to display coords (768x450).
 * Using fixed-point * 1000 for integer arithmetic, then divide.
 */
#define SCALE_X_NUM             (FALL_SCREEN_CAM_DST_W)
#define SCALE_X_DEN             (FALL_SCREEN_CAM_SRC_W)
#define SCALE_Y_NUM             (FALL_SCREEN_CAM_DST_H)
#define SCALE_Y_DEN             (FALL_SCREEN_CAM_SRC_H)

/** Absolute display offset for camera image origin */
#define CAM_DISPLAY_X           (FALL_SCREEN_CAM_OFFSET_X)
#define CAM_DISPLAY_Y           (UI_STATUS_BAR_HEIGHT + FALL_SCREEN_CAM_OFFSET_Y)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/** Bounding box overlay objects */
static lv_obj_t *s_bbox_obj[FALL_SCREEN_MAX_BBOX];

/** Status text label (shows fall state message) */
static lv_obj_t *s_status_label = NULL;

/** Info panel container */
static lv_obj_t *s_info_panel = NULL;

/** Info panel labels */
static lv_obj_t *s_info_inference_label = NULL;
static lv_obj_t *s_info_detect_label = NULL;
static lv_obj_t *s_info_state_label = NULL;
static lv_obj_t *s_info_fps_label = NULL;

/** Initialization flag */
static bool s_initialized = false;

/** Overlay enabled flag */
static bool s_enabled = true;

/** Text buffers for info panel (static to avoid stack allocation) */
static char s_inference_buf[FALL_SCREEN_INFO_BUF_SIZE];
static char s_detect_buf[FALL_SCREEN_INFO_BUF_SIZE];
static char s_state_buf[FALL_SCREEN_INFO_BUF_SIZE];
static char s_fps_buf[FALL_SCREEN_INFO_BUF_SIZE];

/** Previous detection count (to know when to hide unused bboxes) */
static uint32_t s_prev_det_count = 0;

/**********************************************************************************************************************
 Private function prototypes
 *********************************************************************************************************************/

static void create_bbox_objects(lv_obj_t *parent);
static void create_status_label(lv_obj_t *parent);
static void create_info_panel(lv_obj_t *parent);
static lv_obj_t *create_info_label(lv_obj_t *parent, const char *text, lv_color_t color, int32_t x_ofs);
static void update_bounding_boxes(void);
static void update_status_text(void);
static void update_info_panel(void);
static void get_state_color(fall_state_t state, lv_color_t *color, int32_t *border_w);
static void transform_coords(uint16_t cam_x, uint16_t cam_y,
                              uint16_t cam_w, uint16_t cam_h,
                              int32_t *lcd_x, int32_t *lcd_y,
                              int32_t *lcd_w, int32_t *lcd_h);

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Initialize fall detection screen overlay widgets.
 *
 * Reference: Issue #27 (F-003-10)
 */
void fall_detection_screen_init(lv_obj_t *parent)
{
    if (s_initialized)
    {
        return;
    }

    if (parent == NULL)
    {
        return;
    }

    /* Create overlay widgets */
    create_bbox_objects(parent);
    create_status_label(parent);
    create_info_panel(parent);

    s_initialized = true;
    s_enabled = true;
}

/**
 * Update the fall detection overlay with current detection results.
 *
 * Called from the LVGL timer callback after new AI results are available.
 */
void fall_detection_screen_update(void)
{
    if (!s_initialized || !s_enabled)
    {
        return;
    }

    update_bounding_boxes();
    update_status_text();
    update_info_panel();
}

/**
 * Check if overlay has been initialized.
 */
bool fall_detection_screen_is_initialized(void)
{
    return s_initialized;
}

/**
 * Get overlay enabled state.
 */
bool fall_detection_screen_is_enabled(void)
{
    return s_enabled;
}

/**
 * Enable or disable the overlay.
 */
void fall_detection_screen_set_enabled(bool enable)
{
    s_enabled = enable;

    if (!s_initialized)
    {
        return;
    }

    if (!enable)
    {
        /* Hide all overlay widgets */
        for (uint32_t i = 0; i < FALL_SCREEN_MAX_BBOX; i++)
        {
            if (s_bbox_obj[i] != NULL)
            {
                lv_obj_add_flag(s_bbox_obj[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (s_status_label != NULL)
        {
            lv_obj_add_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_info_panel != NULL)
        {
            lv_obj_add_flag(s_info_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else
    {
        /* Show status and info panel (bboxes shown by update) */
        if (s_status_label != NULL)
        {
            lv_obj_remove_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_info_panel != NULL)
        {
            lv_obj_remove_flag(s_info_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * NT-Shell diagnostic: print overlay status.
 */
void fall_detection_screen_cmd_status(void)
{
    char buf[PRINT_BUF_SIZE];

    print_to_console("[Fall Screen Overlay]\r\n");
    print_to_console(s_initialized ? "  Init: Yes\r\n" : "  Init: No\r\n");
    print_to_console(s_enabled ? "  Enabled: Yes\r\n" : "  Enabled: No\r\n");

    snprintf(buf, sizeof(buf), "  BBoxes: %lu  FPS: %lu\r\n",
             (unsigned long)s_prev_det_count,
             (unsigned long)camera_display_get_fps());
    print_to_console(buf);
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Create bounding box overlay objects.
 *
 * Each object is a transparent rectangle with a colored border.
 * Initially all are hidden. They are shown/positioned by update_bounding_boxes().
 *
 * Reference: face_detection_screen_mipi.c lines 96-104 (draw_bounding_box)
 *   The reference draws four lines per box with d2_renderline().
 *   Here we use LVGL objects with border styling for the same visual effect.
 */
static void create_bbox_objects(lv_obj_t *parent)
{
    for (uint32_t i = 0; i < FALL_SCREEN_MAX_BBOX; i++)
    {
        s_bbox_obj[i] = lv_obj_create(parent);

        /* Transparent background - only the border is visible */
        lv_obj_set_style_bg_opa(s_bbox_obj[i], LV_OPA_TRANSP, LV_PART_MAIN);

        /* Default border: green, 2px */
        lv_obj_set_style_border_color(s_bbox_obj[i],
            lv_color_make(FALL_SCREEN_COLOR_NORMAL_R,
                          FALL_SCREEN_COLOR_NORMAL_G,
                          FALL_SCREEN_COLOR_NORMAL_B),
            LV_PART_MAIN);
        lv_obj_set_style_border_width(s_bbox_obj[i], FALL_SCREEN_BORDER_NORMAL, LV_PART_MAIN);
        lv_obj_set_style_border_opa(s_bbox_obj[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(s_bbox_obj[i], 0, LV_PART_MAIN);

        /* No padding, no scrollbar, not clickable */
        lv_obj_set_style_pad_all(s_bbox_obj[i], 0, LV_PART_MAIN);
        lv_obj_remove_flag(s_bbox_obj[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(s_bbox_obj[i], LV_OBJ_FLAG_CLICKABLE);

        /* Start hidden */
        lv_obj_add_flag(s_bbox_obj[i], LV_OBJ_FLAG_HIDDEN);

        /* Default size - will be updated */
        lv_obj_set_size(s_bbox_obj[i], 50, 50);
    }
}

/**
 * Create the status text label.
 *
 * Shows the fall detection state as a large text overlay:
 *   - "Monitoring..."  (green, normal)
 *   - "Fall Suspected" (yellow, suspected)
 *   - "FALL DETECTED"  (red, large, confirmed)
 *
 * Reference: face_detection_screen_mipi.c lines 139-153 (print_static_text)
 */
static void create_status_label(lv_obj_t *parent)
{
    s_status_label = lv_label_create(parent);
    lv_label_set_text(s_status_label, "Monitoring...");

    /* Green text, large font */
    lv_obj_set_style_text_color(s_status_label,
        lv_color_make(FALL_SCREEN_COLOR_NORMAL_R,
                      FALL_SCREEN_COLOR_NORMAL_G,
                      FALL_SCREEN_COLOR_NORMAL_B),
        LV_PART_MAIN);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_20, LV_PART_MAIN);

    /* Semi-transparent dark background for readability */
    lv_obj_set_style_bg_color(s_status_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_status_label, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_pad_left(s_status_label, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_status_label, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_status_label, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_status_label, 4, LV_PART_MAIN);

    /* Position below camera image area */
    lv_obj_set_pos(s_status_label, STATUS_TEXT_X, STATUS_TEXT_Y);

    /* Not clickable/scrollable */
    lv_obj_remove_flag(s_status_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_status_label, LV_OBJ_FLAG_CLICKABLE);
}

/**
 * Create a single info label inside the info panel.
 */
static lv_obj_t *create_info_label(lv_obj_t *parent, const char *text, lv_color_t color, int32_t x_ofs)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, x_ofs, 0);
    return label;
}

/**
 * Create the info panel with inference time, detection count, state, and FPS.
 */
static void create_info_panel(lv_obj_t *parent)
{
    /* Info panel container */
    s_info_panel = lv_obj_create(parent);
    lv_obj_set_pos(s_info_panel, INFO_PANEL_X, INFO_PANEL_Y);
    lv_obj_set_size(s_info_panel, INFO_PANEL_WIDTH, INFO_PANEL_HEIGHT);

    /* Semi-transparent dark background */
    lv_obj_set_style_bg_color(s_info_panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_info_panel, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_info_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_info_panel, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_left(s_info_panel, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_info_panel, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_info_panel, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_info_panel, 4, LV_PART_MAIN);
    lv_obj_remove_flag(s_info_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_info_panel, LV_OBJ_FLAG_CLICKABLE);

    /* Common label style color */
    lv_color_t info_color = lv_color_make(0xCC, 0xCC, 0xCC);

    /*
     * Labels positioned manually in a horizontal row within the panel.
     * Using fixed positions rather than flex layout for maximum
     * compatibility and predictability on embedded targets.
     *
     * Layout: | Inference: XX ms | Detected: N | State: XX | FPS: XX |
     *         X=0               X=280         X=440       X=600
     */

    s_info_inference_label = create_info_label(s_info_panel, "AI: -- ms", info_color, 0);
    s_info_detect_label    = create_info_label(s_info_panel, "Det: 0", info_color, 280);
    s_info_state_label     = create_info_label(s_info_panel, "State: --", info_color, 460);
    s_info_fps_label       = create_info_label(s_info_panel, "FPS: --", info_color, 680);
}

/**
 * Update bounding box positions and colors based on detection results.
 *
 * Reads g_fall_detection_results[] and g_fall_detection_count (from F-003-8
 * post-processing), transforms coordinates from camera space (320x240) to
 * LCD display space, and updates LVGL bounding box objects.
 *
 * The border color is determined by the current fall detection state
 * (from F-003-9 logic), not per-detection. This is because the state
 * machine evaluates all detections collectively.
 *
 * Reference: face_detection_screen_mipi.c lines 113-131
 *   (calculate_and_draw_bounding_box)
 */
static void update_bounding_boxes(void)
{
    uint32_t det_count = g_fall_detection_count;
    fall_state_t state = fall_detection_get_state();

    /* Determine color and border width based on state */
    lv_color_t bbox_color;
    int32_t border_w;
    get_state_color(state, &bbox_color, &border_w);

    /* Clamp to maximum displayable */
    if (det_count > FALL_SCREEN_MAX_BBOX)
    {
        det_count = FALL_SCREEN_MAX_BBOX;
    }

    /* Update visible bounding boxes */
    for (uint32_t i = 0; i < det_count; i++)
    {
        const fall_detection_result_t *det = &g_fall_detection_results[i];

        /* Skip zero-sized detections */
        if (det->width == 0 || det->height == 0)
        {
            lv_obj_add_flag(s_bbox_obj[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        /* Transform coordinates from camera space to LCD display space */
        int32_t lcd_x, lcd_y, lcd_w, lcd_h;
        transform_coords(det->x, det->y, det->width, det->height,
                          &lcd_x, &lcd_y, &lcd_w, &lcd_h);

        /* Update position and size */
        lv_obj_set_pos(s_bbox_obj[i], lcd_x, lcd_y);
        lv_obj_set_size(s_bbox_obj[i], lcd_w, lcd_h);

        /* Update border color and width */
        lv_obj_set_style_border_color(s_bbox_obj[i], bbox_color, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_bbox_obj[i], border_w, LV_PART_MAIN);

        /* Show the box */
        lv_obj_remove_flag(s_bbox_obj[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* Hide unused bounding boxes */
    for (uint32_t i = det_count; i < FALL_SCREEN_MAX_BBOX; i++)
    {
        lv_obj_add_flag(s_bbox_obj[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_prev_det_count = det_count;
}

/**
 * Update the status text based on current fall detection state.
 *
 * Reference: Issue #27 specification - status text requirements
 */
static void update_status_text(void)
{
    if (s_status_label == NULL)
    {
        return;
    }

    fall_state_t state = fall_detection_get_state();
    lv_color_t text_color;
    const char *text;

    /* Use get_state_color to determine color (reuse existing function) */
    int32_t dummy_w;
    get_state_color(state, &text_color, &dummy_w);

    switch (state)
    {
        case FALL_STATE_SUSPECTED:
            text = "Fall Suspected";
            break;
        case FALL_STATE_CONFIRMED:
        case FALL_STATE_COOLDOWN:
            text = "!! FALL DETECTED !!";
            break;
        default:
            text = "Monitoring...";
            break;
    }

    lv_label_set_text(s_status_label, text);
    lv_obj_set_style_text_color(s_status_label, text_color, LV_PART_MAIN);
}

/**
 * Update the info panel labels with current statistics.
 *
 * Reference: face_detection_screen_mipi.c lines 161-186
 *   (print_inf_time_and_number_faces)
 */
static void update_info_panel(void)
{
    /* Inference time */
    if (s_info_inference_label != NULL)
    {
        snprintf(s_inference_buf, sizeof(s_inference_buf),
                 "AI: %lu ms",
                 (unsigned long)ai_inference_get_invoke_time_ms());
        lv_label_set_text(s_info_inference_label, s_inference_buf);
    }

    /* Detection count */
    if (s_info_detect_label != NULL)
    {
        snprintf(s_detect_buf, sizeof(s_detect_buf),
                 "Det: %lu", (unsigned long)g_fall_detection_count);
        lv_label_set_text(s_info_detect_label, s_detect_buf);
    }

    /* Fall detection state */
    if (s_info_state_label != NULL)
    {
        lv_color_t state_color;
        int32_t dummy_w;
        get_state_color(fall_detection_get_state(), &state_color, &dummy_w);

        snprintf(s_state_buf, sizeof(s_state_buf), "%s", fall_detection_get_state_name());
        lv_label_set_text(s_info_state_label, s_state_buf);
        lv_obj_set_style_text_color(s_info_state_label, state_color, LV_PART_MAIN);
    }

    /* Display FPS */
    if (s_info_fps_label != NULL)
    {
        uint32_t fps = camera_display_get_fps();
        snprintf(s_fps_buf, sizeof(s_fps_buf), "FPS: %lu", (unsigned long)fps);
        lv_label_set_text(s_info_fps_label, s_fps_buf);
    }
}

/**
 * Get the bounding box color and border width for a given fall state.
 *
 * @param state    Current fall detection state
 * @param color    Output: LVGL color for bounding box border
 * @param border_w Output: Border width in pixels
 */
static void get_state_color(fall_state_t state, lv_color_t *color, int32_t *border_w)
{
    switch (state)
    {
        case FALL_STATE_SUSPECTED:
            *color = lv_color_make(FALL_SCREEN_COLOR_SUSPECTED_R,
                                    FALL_SCREEN_COLOR_SUSPECTED_G,
                                    FALL_SCREEN_COLOR_SUSPECTED_B);
            *border_w = FALL_SCREEN_BORDER_SUSPECTED;
            break;

        case FALL_STATE_CONFIRMED:
        case FALL_STATE_COOLDOWN:
            *color = lv_color_make(FALL_SCREEN_COLOR_CONFIRMED_R,
                                    FALL_SCREEN_COLOR_CONFIRMED_G,
                                    FALL_SCREEN_COLOR_CONFIRMED_B);
            *border_w = FALL_SCREEN_BORDER_CONFIRMED;
            break;

        case FALL_STATE_NORMAL:
        default:
            *color = lv_color_make(FALL_SCREEN_COLOR_NORMAL_R,
                                    FALL_SCREEN_COLOR_NORMAL_G,
                                    FALL_SCREEN_COLOR_NORMAL_B);
            *border_w = FALL_SCREEN_BORDER_NORMAL;
            break;
    }
}

/**
 * Transform coordinates from camera space to LCD display space.
 *
 * Camera detection coordinates are in the post-processing output space (320x240).
 * The camera image is displayed at 768x450, centered in the 1024x560 camera area,
 * which starts at Y=40 (below the 40px status bar).
 *
 * Transform:
 *   lcd_x = CAM_DISPLAY_X + (cam_x * 768 / 320)
 *   lcd_y = CAM_DISPLAY_Y + (cam_y * 450 / 240)
 *   lcd_w = cam_w * 768 / 320
 *   lcd_h = cam_h * 450 / 240
 *
 * Reference: face_detection_screen_mipi.c lines 113-131
 *   (calculate_and_draw_bounding_box) - similar scaling logic
 *
 * @param cam_x   Camera X coordinate (0-320)
 * @param cam_y   Camera Y coordinate (0-240)
 * @param cam_w   Camera width
 * @param cam_h   Camera height
 * @param lcd_x   Output: LCD X coordinate
 * @param lcd_y   Output: LCD Y coordinate
 * @param lcd_w   Output: LCD width
 * @param lcd_h   Output: LCD height
 */
static void transform_coords(uint16_t cam_x, uint16_t cam_y,
                              uint16_t cam_w, uint16_t cam_h,
                              int32_t *lcd_x, int32_t *lcd_y,
                              int32_t *lcd_w, int32_t *lcd_h)
{
    /* Scale from camera coords to display coords */
    *lcd_x = CAM_DISPLAY_X + ((int32_t)cam_x * SCALE_X_NUM / SCALE_X_DEN);
    *lcd_y = CAM_DISPLAY_Y + ((int32_t)cam_y * SCALE_Y_NUM / SCALE_Y_DEN);
    *lcd_w = (int32_t)cam_w * SCALE_X_NUM / SCALE_X_DEN;
    *lcd_h = (int32_t)cam_h * SCALE_Y_NUM / SCALE_Y_DEN;

    /* Clamp minimum size for visibility */
    if (*lcd_w < 10)
    {
        *lcd_w = 10;
    }
    if (*lcd_h < 10)
    {
        *lcd_h = 10;
    }

    /* Clamp to display bounds */
    if (*lcd_x < 0)
    {
        *lcd_x = 0;
    }
    if (*lcd_y < 0)
    {
        *lcd_y = 0;
    }
    if (*lcd_x + *lcd_w > UI_DISPLAY_WIDTH)
    {
        *lcd_w = UI_DISPLAY_WIDTH - *lcd_x;
    }
    if (*lcd_y + *lcd_h > UI_DISPLAY_HEIGHT)
    {
        *lcd_h = UI_DISPLAY_HEIGHT - *lcd_y;
    }
}
