/**
 * @file subtitle_gen.c
 * @description Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/
#include "subtitle_gen.h"
#include "ui.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/***********************
 *  STATIC VARIABLES
 **********************/

/***********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * subtitle_create(lv_obj_t * parent, const char * text, lv_subject_t * bind_text)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t style_main;

    static bool style_inited = false;

    if (!style_inited) {
        
        lv_style_init(&style_main);
        lv_style_set_text_font(&style_main, font_subtitle);

        style_inited = true;
    }

    lv_obj_t * lv_label_1 = lv_label_create(parent);
        lv_obj_add_style(lv_label_1, &style_main, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lv_label_1, text);
    
    lv_label_bind_text(lv_label_1, bind_text, "%02d");


    LV_TRACE_OBJ_CREATE("finished");

    return lv_label_1;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
