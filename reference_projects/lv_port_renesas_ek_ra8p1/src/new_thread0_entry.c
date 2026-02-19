#include "new_thread0.h"
#include "lvgl.h"
#include "port/lv_port_disp.h"
#include "port/lv_port_indev.h"
#include "lv_demos.h"
#include "../ui/ui.h"

#if (0 == LV_USE_DEMO_BENCHMARK) && (0 == LV_USE_DEMO_MUSIC) && (0 == LV_USE_DEMO_KEYPAD_AND_ENCODER) && (0 == LV_USE_DEMO_WIDGETS)
#define USE_LVGL_EDITOR 1
#endif

lv_obj_t * settings;

/* New Thread entry function */
/* pvParameters contains TaskHandle_t */
void new_thread0_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED (pvParameters);
    fsp_err_t err;

    err = R_SCI_B_UART_Open(&g_uart0_ctrl, &g_uart0_cfg);
    assert(FSP_SUCCESS == err);

    lv_init();

    lv_port_disp_init();

    lv_port_indev_init();

#if (1 == LV_USE_DEMO_BENCHMARK)
    lv_demo_benchmark();
#elif (1 == LV_USE_DEMO_MUSIC)
    lv_demo_music();
#elif (1 == LV_USE_DEMO_KEYPAD_AND_ENCODER)
    lv_demo_keypad_encoder();
#elif (1 == LV_USE_DEMO_STRESS)
    lv_demo_stress();
#elif (1 == LV_USE_DEMO_WIDGETS && 0 == LV_USE_DEMO_BENCHMARK)
    lv_demo_widgets();
#elif (1 == USE_LVGL_EDITOR)

    ui_init(NULL);

    LV_IMAGE_DECLARE(img_bell_data);
    LV_IMAGE_DECLARE(img_bluetooth_data);
    LV_IMAGE_DECLARE(img_wifi_data);

    img_bell      = &img_bell_data;
    img_bluetooth = &img_bluetooth_data;
    img_wifi      = &img_wifi_data;

    settings = settings_create();
    lv_screen_load(settings);

#endif

    /* TODO: add your own code here */
    while (1)
    {
      lv_timer_handler();
#if (1 == INDEV_EVENT_DRIVEN)
        BaseType_t status;

        status = xSemaphoreTake( g_irq_binary_semaphore, 0);
        if(pdTRUE == status)
        {
            lv_indev_t * indev_touchpad = NULL;
            indev_touchpad = lv_indev_get_next(NULL);
            if(NULL == indev_touchpad)
            {
                LV_ASSERT(0);
            }
            /* Call this anywhere you want to read the input device */
            lv_indev_read(indev_touchpad);
        }
#endif
      vTaskDelay (1);
    }
}
