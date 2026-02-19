/**
 * @file ui_gen.c
 */

/*********************
 *      INCLUDES
 *********************/
#include "ui_gen.h"

#if LV_USE_XML
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *  GLOBAL VARIABLES
 **********************/

/* Global Styles */

/* Fonts */
lv_font_t * font_title;
extern uint8_t Inter_SemiBold_ttf_data[];
extern size_t Inter_SemiBold_ttf_data_size;
lv_font_t * font_subtitle;

/* Images */
const void * img_wifi;
const void * img_bluetooth;
const void * img_bell;

/*Subjects*/
lv_subject_t subject_hours;
lv_subject_t subject_mins;
lv_subject_t subject_bluetooth_on;
lv_subject_t subject_wifi_on;
lv_subject_t subject_notification_on;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ui_init_gen(const char * asset_path)
{
    char buf[256];

    /* Global Styles */

    /* Subjects */
    lv_subject_init_int(&subject_hours, 17);
    lv_subject_init_int(&subject_mins, 45);
    lv_subject_init_int(&subject_bluetooth_on, 0);
    lv_subject_init_int(&subject_wifi_on, 0);
    lv_subject_init_int(&subject_notification_on, 0);

    /* Fonts */
    /* create tiny ttf font 'font_title' from C array */
    font_title = lv_tiny_ttf_create_data(Inter_SemiBold_ttf_data, Inter_SemiBold_ttf_data_size, 20);
    /* create tiny ttf font 'font_subtitle' from C array */
    font_subtitle = lv_tiny_ttf_create_data(Inter_SemiBold_ttf_data, Inter_SemiBold_ttf_data_size, 14);

    /* Images */
    lv_snprintf(buf, 256, "%s%s", asset_path, "images/wifi-solid.png");
    img_wifi = lv_strdup(buf);
    lv_snprintf(buf, 256, "%s%s", asset_path, "images/bluetooth-brands.png");
    img_bluetooth = lv_strdup(buf);
    lv_snprintf(buf, 256, "%s%s", asset_path, "images/bell-solid.png");
    img_bell = lv_strdup(buf);

    #if LV_USE_XML

        lv_xml_register_font(NULL, "font_title", font_title);
        lv_xml_register_font(NULL, "font_subtitle", font_subtitle);

        lv_xml_register_image(NULL, "img_wifi", img_wifi);
        lv_xml_register_image(NULL, "img_bluetooth", img_bluetooth);
        lv_xml_register_image(NULL, "img_bell", img_bell);

        lv_xml_register_subject(NULL, "hours", &subject_hours);
        lv_xml_register_subject(NULL, "mins", &subject_mins);
        lv_xml_register_subject(NULL, "bluetooth_on", &subject_bluetooth_on);
        lv_xml_register_subject(NULL, "wifi_on", &subject_wifi_on);
        lv_xml_register_subject(NULL, "notification_on", &subject_notification_on);

    #endif
}

/* callbacks */

/**********************
 *   STATIC FUNCTIONS
 **********************/
