/**
 * @file lv_port_disp_templ.c
 *
 */


/*********************
 *      INCLUDES
 *********************/
#include "new_thread0.h"
#include "lv_port_disp.h"

/*********************
 *      DEFINES
 *********************/


/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

static void glcdc_flush_finish_event(lv_event_t * event);

static void glcdc_flush_finish_event(lv_event_t * event)
{
    FSP_PARAMETER_NOT_USED(event);
    lv_display_t * disp;

    if (LV_EVENT_FLUSH_FINISH == lv_event_get_code(event))
    {
        /* Enable Backlight */
        R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_BLEN, BSP_IO_LEVEL_HIGH);

        disp = lv_event_get_target(event);

        /* now the backlight in enabled, remove the event callback */
        lv_display_remove_event_cb_with_user_data(disp, glcdc_flush_finish_event, NULL);
    }
}

void lvgl_glcdc_callback(rm_lvgl_port_callback_args_t *p_arg)
{
    if (RM_LVGL_PORT_EVENT_UNDERFLOW == p_arg->event)
    {
       assert(0);
    }
}

void lv_port_disp_init(void)
{
    /*-------------------------
     * Initialize your display
     * -----------------------*/

    /* Shared RESET between the GT911 touch controller and the LCD, only reset once, here */
    disp_init();

    /*------------------------------------
     * Create a display and set a flush_cb
     * -----------------------------------*/
    fsp_err_t err;
    err = RM_LVGL_PORT_Open(&g_lvgl_port_ctrl, &g_lvgl_port_cfg);
    if (FSP_SUCCESS != err)
    {
        __BKPT(0);
    }

    lv_display_add_event_cb(g_lvgl_port_ctrl.p_lv_display, glcdc_flush_finish_event, LV_EVENT_FLUSH_FINISH, NULL);
}

static void disp_init(void)
{

    R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_RESET, BSP_IO_LEVEL_HIGH);

    R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);

    R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_RESET, BSP_IO_LEVEL_LOW);

    R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);

    R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_RESET, BSP_IO_LEVEL_HIGH);

    R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);
}
