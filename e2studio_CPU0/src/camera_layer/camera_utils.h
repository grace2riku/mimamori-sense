/**
 * @file camera_utils.h
 * @brief Camera image preprocessing for AI inference (F-003-6)
 * @details
 * Provides image conversion functions to transform camera-captured
 * RGB565 frames into the AI model's expected input format.
 *
 * For fall detection (YOLOv8 pico):
 *   Input:  768x450 RGB565 (camera VIN capture)
 *   Output: 192x192x3 RGB INT8
 *
 * The preprocessing pipeline:
 *   1. Center-crop to square (450x450 from 768x450)
 *   2. Nearest-neighbor downscale to 192x192
 *   3. RGB565 to RGB888 color space conversion
 *   4. Unsigned [0,255] to signed INT8 [-128,+127] normalization
 *
 * Reference: reference_projects/ruhmi-framework-mcu/application_examples/
 *            face_detection/src/camera_layer/camera_utils.h
 */
#ifndef CAMERA_UTILS_H
#define CAMERA_UTILS_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>
#include "common_util.h"

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

FSP_CPP_HEADER

/**
 * Convert RGB565 camera image to RGB INT8 for fall detection model
 *
 * @details
 * Performs center-crop, nearest-neighbor resize, and RGB565-to-INT8
 * color space conversion in a single pass. The output is a planar
 * interleaved RGB INT8 image suitable for the YOLOv8 pico model.
 *
 * Processing steps (per output pixel):
 *   1. Compute source (x,y) via nearest-neighbor mapping from cropped square
 *   2. Extract R5/G6/B5 from the 16-bit RGB565 pixel
 *   3. Expand to full 8-bit range: R=[0..255], G=[0..255], B=[0..255]
 *   4. Convert to signed INT8: value = (uint8_t)component - 128
 *
 * DCache note:
 *   The caller must call SCB_CleanDCache_by_Addr() on the output buffer
 *   after this function returns, before the NPU reads the data via DMA.
 *
 * Reference: face_detection/src/camera_layer/camera_utils.c
 *            image_rgb565_to_int8() lines 35-80
 *            image_rgb565_to_rgb888() lines 82-119
 *
 * @param[in]  p_input_image_buff  Input RGB565 image buffer (768x450)
 * @param[out] p_output_image_buff Output RGB INT8 image buffer (192x192x3)
 * @param[in]  in_width            Width of input image in pixels
 * @param[in]  in_height           Height of input image in pixels
 * @param[in]  out_width           Width of output image in pixels
 * @param[in]  out_height          Height of output image in pixels
 *
 * @retval VISION_AI_APP_SUCCESS        Conversion completed successfully
 * @retval VISION_AI_APP_ERR_NULL_POINTER  NULL input or output pointer
 * @retval VISION_AI_APP_ERR_IMG_PROCESS   Invalid dimensions (width < height)
 */
vision_ai_app_err_t image_rgb565_to_rgb_int8(const void *p_input_image_buff,
                                              void *p_output_image_buff,
                                              uint16_t in_width,
                                              uint16_t in_height,
                                              uint16_t out_width,
                                              uint16_t out_height);

/**
 * Get the last preprocessing execution time in milliseconds
 *
 * @details
 * Returns the DWT cycle-count based measurement of the most recent
 * call to image_rgb565_to_rgb_int8(). The value is updated on each
 * successful conversion.
 *
 * @return Preprocessing time in milliseconds (0 if never executed)
 */
uint32_t camera_utils_get_preproc_time_ms(void);

/**
 * Get the total number of preprocessing calls since boot
 *
 * @return Preprocessing call count
 */
uint32_t camera_utils_get_preproc_count(void);

FSP_CPP_FOOTER

#endif /* CAMERA_UTILS_H */
