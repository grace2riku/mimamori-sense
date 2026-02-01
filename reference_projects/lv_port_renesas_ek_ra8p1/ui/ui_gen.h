/**
 * @file ui_gen.h
 */

#ifndef UI_GEN_H
#define UI_GEN_H

#ifndef UI_SUBJECT_STRING_LENGTH
#define UI_SUBJECT_STRING_LENGTH 256
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl/lvgl.h"

/*Include all the widget and components of this library*/
#include "components/checkbox/checkbox_gen.h"
#include "components/column/column_gen.h"
#include "components/header/header_gen.h"
#include "components/icon/icon_gen.h"
#include "components/row/row_gen.h"
#include "components/setclock/setclock_gen.h"
#include "components/subtiltle_no_bind/subtitle_no_bind_gen.h"
#include "components/subtitle/subtitle_gen.h"
#include "components/title/title_gen.h"
#include "screens/settings/settings_gen.h"

/*********************
 *      DEFINES
 *********************/

// TODO: should these definitions be prefixed with the project name?


/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL VARIABLES
 **********************/

/* Global Styles */

/* Fonts */
extern lv_font_t * font_title;
extern lv_font_t * font_subtitle;

/* Images */
extern const void * img_wifi;
extern const void * img_bluetooth;
extern const void * img_bell;

/* Subjects */
extern lv_subject_t subject_hours;
extern lv_subject_t subject_mins;
extern lv_subject_t subject_bluetooth_on;
extern lv_subject_t subject_wifi_on;
extern lv_subject_t subject_notification_on;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/* callbacks */

/**
 * Initialize the component library
 */

void ui_init_gen(const char * asset_path);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_GEN_H*/
