/* generated thread header file - do not edit */
#ifndef LVGL_THREAD_H_
#define LVGL_THREAD_H_
#include "bsp_api.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hal_data.h"
#ifdef __cplusplus
                extern "C" void lvgl_thread_entry(void * pvParameters);
                #else
extern void lvgl_thread_entry(void *pvParameters);
#endif
#include "rm_comms_i2c.h"
#include "rm_comms_api.h"
FSP_HEADER
/* I2C Communication Device */
extern const rm_comms_instance_t g_comms_i2c_device0;
extern rm_comms_i2c_instance_ctrl_t g_comms_i2c_device0_ctrl;
extern const rm_comms_cfg_t g_comms_i2c_device0_cfg;
#ifndef comms_i2c_callback
void comms_i2c_callback(rm_comms_callback_args_t *p_args);
#endif
FSP_FOOTER
#endif /* LVGL_THREAD_H_ */
