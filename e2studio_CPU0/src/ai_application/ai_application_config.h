/**
 * @file ai_application_config.h
 * @brief AI inference model configuration for the active demo
 * @details
 * Defines AI model input image dimensions, channel count, and maximum
 * detection number based on the selected AI_DEMO type.
 *
 * Fall detection model (YOLOv8 pico INT8):
 *   - Input:  192x192x3 RGB INT8 (110592 bytes)
 *   - Output: [1, 5, 756] INT8 (3780 bytes)
 *     - 5 = 4 bbox (x_center, y_center, w, h) + 1 class score
 *     - 756 = P3(24x24) + P4(12x12) + P5(6x6) detection candidates
 *   - Classes: 1 (person)
 *
 * Reference: reference_projects/ruhmi-framework-mcu/application_examples/
 *            face_detection/src/ai_application/ai_application_config.h
 */

#ifndef AI_APPLICATION_CONFIG_H_
#define AI_APPLICATION_CONFIG_H_

#include "application_config.h"

// ###################### AI INFERENCE SETTING ######################
/* AI input image */

#if AI_DEMO == (FALL_DETECTION)
    /* YOLOv8 pico INT8 model for fall detection
     * Trained with 192x192 RGB input, 1 class (person)
     * Reference: dataset/scripts/train_yolov8_pico_colab.ipynb
     */
    #define AI_INPUT_IMAGE_WIDTH              (192)
    #define AI_INPUT_IMAGE_HEIGHT             (192)
    #define AI_INPUT_IMAGE_BYTE_PER_PIXEL     (3)     /* RGB: 3 bytes per pixel */

    /* Maximum number of detected objects presented in the result */
    #define AI_MAX_DETECTION_NUM              (10)

    /* YOLOv8 output tensor dimensions */
    #define AI_OUTPUT_NUM_CANDIDATES          (756)   /* P3(576) + P4(144) + P5(36) */
    #define AI_OUTPUT_NUM_CLASSES             (1)     /* person only */
    #define AI_OUTPUT_BBOX_ATTRS              (4)     /* x_center, y_center, w, h */
    #define AI_OUTPUT_TENSOR_ROWS             (AI_OUTPUT_BBOX_ATTRS + AI_OUTPUT_NUM_CLASSES) /* 5 */
    #define AI_OUTPUT_TENSOR_SIZE             (AI_OUTPUT_TENSOR_ROWS * AI_OUTPUT_NUM_CANDIDATES) /* 3780 */

    /* Model input buffer size in bytes */
    #define AI_INPUT_IMAGE_SIZE               (AI_INPUT_IMAGE_WIDTH * AI_INPUT_IMAGE_HEIGHT * AI_INPUT_IMAGE_BYTE_PER_PIXEL) /* 110592 */

#elif AI_DEMO == (FACE_DETECTION)
    #define AI_INPUT_IMAGE_WIDTH              (192)
    #define AI_INPUT_IMAGE_HEIGHT             (192)
    #define AI_INPUT_IMAGE_BYTE_PER_PIXEL     (1)
    /* This definition selects how many faces will be presented in the detection result */
    #define AI_MAX_DETECTION_NUM              (20)

#else
    #define AI_INPUT_IMAGE_WIDTH              (224)
    #define AI_INPUT_IMAGE_HEIGHT             (224)
    #define AI_INPUT_IMAGE_BYTE_PER_PIXEL     (3)
    /* This definition selects how many objects will be presented in the detection result */
    #define AI_MAX_DETECTION_NUM              (5)

#endif

#endif /* AI_APPLICATION_CONFIG_H_ */
