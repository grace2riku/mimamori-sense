/* generated HAL header file - do not edit */
#ifndef HAL_DATA_H_
#define HAL_DATA_H_
#include <stdint.h>
#include "bsp_api.h"
#include "common_data.h"
#include "r_rtc.h"
#include "r_rtc_api.h"
#include "rm_comms_i2c.h"
#include "rm_comms_api.h"
#include "r_gpt.h"
#include "r_timer_api.h"
#include "r_dtc.h"
#include "r_transfer_api.h"
#include "r_i2s_api.h"
#include "r_ssi.h"
#include "r_iic_master.h"
#include "r_i2c_master_api.h"
#include "r_sci_b_uart.h"
#include "r_uart_api.h"
FSP_HEADER
/* RTC Instance. */
extern const rtc_instance_t g_rtc;

/** Access the RTC instance using these structures when calling API functions directly (::p_api is not used). */
extern rtc_instance_ctrl_t g_rtc_ctrl;
extern const rtc_cfg_t g_rtc_cfg;

#ifndef NULL
void NULL(rtc_callback_args_t *p_args);
#endif
/* I2C Communication Device */
extern const rm_comms_instance_t g_comms_i2c_codec;
extern rm_comms_i2c_instance_ctrl_t g_comms_i2c_codec_ctrl;
extern const rm_comms_cfg_t g_comms_i2c_codec_cfg;
#ifndef audio_codec_i2c_callback
void audio_codec_i2c_callback(rm_comms_callback_args_t *p_args);
#endif
/** Timer on GPT Instance. */
extern const timer_instance_t g_timer_audio_mclk;

/** Access the GPT instance using these structures when calling API functions directly (::p_api is not used). */
extern gpt_instance_ctrl_t g_timer_audio_mclk_ctrl;
extern const timer_cfg_t g_timer_audio_mclk_cfg;

#ifndef NULL
void NULL(timer_callback_args_t *p_args);
#endif
/* Transfer on DTC Instance. */
extern const transfer_instance_t g_transfer_i2s_tx;

/** Access the DTC instance using these structures when calling API functions directly (::p_api is not used). */
extern dtc_instance_ctrl_t g_transfer_i2s_tx_ctrl;
extern const transfer_cfg_t g_transfer_i2s_tx_cfg;
/** SSI Instance. */
extern const i2s_instance_t g_i2s_audio;

/** Access the SSI instance using these structures when calling API functions directly (::p_api is not used). */
extern ssi_instance_ctrl_t g_i2s_audio_ctrl;
extern const i2s_cfg_t g_i2s_audio_cfg;

#ifndef audio_i2s_callback
void audio_i2s_callback(i2s_callback_args_t *p_args);
#endif
/* I2C Communication Device */
extern const rm_comms_instance_t g_comms_i2c_device0;
extern rm_comms_i2c_instance_ctrl_t g_comms_i2c_device0_ctrl;
extern const rm_comms_cfg_t g_comms_i2c_device0_cfg;
#ifndef comms_i2c_callback
void comms_i2c_callback(rm_comms_callback_args_t *p_args);
#endif
/** Timer on GPT Instance. */
extern const timer_instance_t g_timer_camera_xclk;

/** Access the GPT instance using these structures when calling API functions directly (::p_api is not used). */
extern gpt_instance_ctrl_t g_timer_camera_xclk_ctrl;
extern const timer_cfg_t g_timer_camera_xclk_cfg;

#ifndef NULL
void NULL(timer_callback_args_t *p_args);
#endif
/* I2C Master on IIC Instance. */
extern const i2c_master_instance_t g_i2c_master_camera;

/** Access the I2C Master instance using these structures when calling API functions directly (::p_api is not used). */
extern iic_master_instance_ctrl_t g_i2c_master_camera_ctrl;
extern const i2c_master_cfg_t g_i2c_master_camera_cfg;

#ifndef i2c_camera_callback
void i2c_camera_callback(i2c_master_callback_args_t *p_args);
#endif
/** UART on SCI Instance. */
extern const uart_instance_t g_jlink_console;

/** Access the UART instance using these structures when calling API functions directly (::p_api is not used). */
extern sci_b_uart_instance_ctrl_t g_jlink_console_ctrl;
extern const uart_cfg_t g_jlink_console_cfg;
extern const sci_b_uart_extended_cfg_t g_jlink_console_cfg_extend;

#ifndef jlink_console_callback
void jlink_console_callback(uart_callback_args_t *p_args);
#endif
void hal_entry(void);
void g_hal_init(void);
FSP_FOOTER
#endif /* HAL_DATA_H_ */
