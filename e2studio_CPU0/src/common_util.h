/**
 * @file common_util.h
 * @brief Common utility definitions for AI vision application
 * @details
 * Defines event flags for inter-thread synchronization, AI detection
 * result structures, camera size enumerations, error codes, and
 * processing time measurement structures.
 *
 * Reference: reference_projects/ruhmi-framework-mcu/application_examples/
 *            face_detection/src/common_util.h
 */
#ifndef COMMON_UTIL_H__
#define COMMON_UTIL_H__

#include "hal_data.h"
#include "ai_application/application_config.h"
#include "ai_application/ai_application_config.h"

/* ---- LED pin definitions ---- */
/* Reference: common_util.h lines 18-31 */
#if defined(BOARD_RA8D1_EK)
#define LED1_PIN (BSP_IO_PORT_06_PIN_00)
#define LED2_PIN (BSP_IO_PORT_04_PIN_14)
#define LED3_PIN (BSP_IO_PORT_01_PIN_07)
#elif defined(BOARD_RA8P1_WSB_EK) || defined(BOARD_RA8P1_EK)
#define LED1_PIN (BSP_IO_PORT_06_PIN_00)
#define LED2_PIN (BSP_IO_PORT_03_PIN_03)
#define LED3_PIN (BSP_IO_PORT_10_PIN_07)
#else
/* Update pin number for your board */
#define LED1_PIN (BSP_IO_PORT_FF_PIN_FF)
#define LED2_PIN (BSP_IO_PORT_FF_PIN_FF)
#define LED3_PIN (BSP_IO_PORT_FF_PIN_FF)
#endif

#define LED1_ON  R_IOPORT_PinWrite(NULL, LED1_PIN, BSP_IO_LEVEL_HIGH)
#define LED1_OFF R_IOPORT_PinWrite(NULL, LED1_PIN, BSP_IO_LEVEL_LOW)
#define LED2_ON  R_IOPORT_PinWrite(NULL, LED2_PIN, BSP_IO_LEVEL_HIGH)
#define LED2_OFF R_IOPORT_PinWrite(NULL, LED2_PIN, BSP_IO_LEVEL_LOW)
#define LED3_ON  R_IOPORT_PinWrite(NULL, LED3_PIN, BSP_IO_LEVEL_HIGH)
#define LED3_OFF R_IOPORT_PinWrite(NULL, LED3_PIN, BSP_IO_LEVEL_LOW)

#if (ENABLE_INFERENCE_RUNNING_LED == 1)
#define INFERENCE_START_INDICATE_LED LED1_ON
#define INFERENCE_END_INDICATE_LED   LED1_OFF
#else
#define INFERENCE_START_INDICATE_LED
#define INFERENCE_END_INDICATE_LED
#endif

#if (ENABLE_CAMERA_CAPTURE_RUNNING_LED == 1)
#define CAMERA_CAPTURE_END_INDICATE_LED_ON  LED2_ON
#define CAMERA_CAPTURE_END_INDICATE_LED_OFF LED2_OFF
#else
#define CAMERA_CAPTURE_END_INDICATE_LED_ON
#define CAMERA_CAPTURE_END_INDICATE_LED_OFF
#endif

#define ERROR_INDICATE_LED_ON  LED3_ON
#define ERROR_INDICATE_LED_OFF LED3_OFF

#define ERROR_INDICATE ERROR_INDICATE_LED_ON; __BKPT(0)

/* ---- Sync events (FreeRTOS EventGroup bits) ---- */
/* Reference: common_util.h lines 62-72 */
#define HARDWARE_DISPLAY_INIT_DONE      (1 << 0)
#define HARDWARE_CAMERA_INIT_DONE       (1 << 1)
#define HARDWARE_ETHOSU_INIT_DONE       (1 << 2)
#define SOFTWARE_AI_INFERENCE_INIT_DONE (1 << 3)
#define GLCDC_VSYNC                     (1 << 10)
#define MIPI_MESSAGE_SENT               (1 << 11)
#define CAMERA_CAPTURE_COMPLETED        (1 << 12)
#define AI_INFERENCE_INPUT_IMAGE_READY  (1 << 13)
#define AI_INFERENCE_RESULT_UPDATED     (1 << 14)
#define DISPLAY_PAUSE                   (1 << 15)
#define CAMERA_AUTO_FOCUS_EXECUTE       (1 << 16)

#define APP_ERROR_TRAP(err)  if(err) { __asm("BKPT #0\n"); }

/**********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/**
 * Bounding box detection result
 * Reference: common_util.h lines 81-86
 */
typedef struct ai_detection_point_t {
    signed short      m_x;
    signed short      m_y;
    signed short      m_w;
    signed short      m_h;
} st_ai_detection_point_t;

/**
 * Classification result
 * Reference: common_util.h lines 88-92
 */
typedef struct ai_classification_point_t {
    unsigned short    category;
    float             prob;
} st_ai_classification_point_t;

/**
 * Camera capture resolutions
 * Reference: common_util.h lines 94-101
 */
typedef enum
{
    CAM_VGA_WIDTH          = 640,
    CAM_VGA_HEIGHT         = 480,
    CAM_QVGA_WIDTH         = 320,
    CAM_QVGA_HEIGHT        = 240,
} camera_size_list_t;

#define CAM_BYTE_PER_PIXEL              (2)
#define RGB888_BYTE_PER_PIXEL           (3)

/**
 * Common error codes
 * Reference: common_util.h lines 111-137
 */
typedef enum e_vision_ai_app_err
{
    VISION_AI_APP_SUCCESS                = 0,

    VISION_AI_APP_ERR_AI_INIT            = 1,
    VISION_AI_APP_ERR_AI_INFERENCE       = 2,
    VISION_AI_APP_ERR_IMG_PROCESS        = 3,
    VISION_AI_APP_ERR_IMG_ROTATION       = 4,
    VISION_AI_APP_ERR_NULL_POINTER       = 5,
    VISION_AI_APP_ERR_GLCDC_OPEN         = 6,
    VISION_AI_APP_ERR_MIPI_CMD           = 7,
    VISION_AI_APP_ERR_GLCDC_START        = 8,
    VISION_AI_APP_ERR_GLCDC_LAYER_CHANGE = 9,
    VISION_AI_APP_ERR_GRAPHICS_INIT      = 10,
    VISION_AI_APP_ERR_GPT_OPEN           = 11,
    VISION_AI_APP_ERR_CEU_OPEN           = 12,
    VISION_AI_APP_ERR_WRITE_OV3640_REG   = 13,
    VISION_AI_APP_ERR_WRITE_SENSOR_ARRAY = 14,
    VISION_AI_APP_ERR_CAMERA_INIT        = 15,
    VISION_AI_APP_ERR_IIC_MASTER_OPEN    = 16,
    VISION_AI_APP_ERR_IIC_MASTER_WRITE   = 17,
    VISION_AI_APP_ERR_IIC_MASTER_READ    = 18,
    VISION_AI_APP_ERR_CONSOLE_OPEN       = 19,
    VISION_AI_APP_ERR_CONSOLE_WRITE      = 20,
    VISION_AI_APP_ERR_CONSOLE_READ       = 21,
    VISION_AI_APP_ERR_EXTERNAL_IRQ_INIT  = 22,
} vision_ai_app_err_t;

/**
 * Processing time measurement
 * Reference: common_util.h lines 140-147
 */
typedef struct st_processing_time_info_t
{
    uint32_t camera_image_capture_time_ms;
    uint32_t camera_post_processing_time_ms;
    uint32_t lcd_display_update_refresh_ms;
    uint32_t ai_inference_pre_processing_time_ms;
    uint32_t ai_inference_time_ms;
} processing_time_info_t;

/**********************************************************************************************************************
 * Exported global variables
 **********************************************************************************************************************/

extern st_ai_detection_point_t g_ai_detection[AI_MAX_DETECTION_NUM];
extern st_ai_classification_point_t g_ai_classification[AI_MAX_DETECTION_NUM];
extern processing_time_info_t g_processing_time;

/**********************************************************************************************************************
 * Exported global functions
 **********************************************************************************************************************/

FSP_CPP_HEADER
void handle_error(vision_ai_app_err_t err);
FSP_CPP_FOOTER

#endif /* COMMON_UTIL_H__ */
