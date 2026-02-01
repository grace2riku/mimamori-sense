/**
 * @file subtitle_gen.h
 */

#ifndef SUBTITLE_H
#define SUBTITLE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "lvgl/lvgl.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

lv_obj_t * subtitle_create(lv_obj_t * parent, const char * text, lv_subject_t * bind_text);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*SUBTITLE_H*/