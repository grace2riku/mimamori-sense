/**
 * @file ai_application_config.h
 * @brief AI inference model configuration for the active demo
 * @details
 * Defines AI model input image dimensions, channel count, and maximum
 * detection number based on the selected AI_DEMO type.
 *
 * Fall detection model (YOLO-Fastest V1, YOLOv3-based, anchor-based):
 *   - Input:  192x192x3 RGB INT8 (110592 bytes)
 *   - Output: 2 branches (anchor-based)
 *     - Branch 0 (6x6, stride 32): 6x6x3x6 = 648 bytes INT8
 *     - Branch 1 (12x12, stride 16): 12x12x3x6 = 2592 bytes INT8
 *     - Each anchor: (x, y, w, h, objectness, class_score)
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
    /* YOLO-Fastest V1 (YOLOv3-based, anchor-based) for fall detection
     * Input: 192x192x3 RGB INT8
     * Output: 2 branches (6x6 + 12x12, each with 3 anchors x 6 values)
     * Classes: 1 (person)
     */
    #define AI_INPUT_IMAGE_WIDTH              (192)
    #define AI_INPUT_IMAGE_HEIGHT             (192)
    #define AI_INPUT_IMAGE_BYTE_PER_PIXEL     (3)     /* RGB: 3 bytes per pixel */

    #define AI_MAX_DETECTION_NUM              (10)

    /* YOLOv3 anchor-based output configuration */
    #define AI_OUTPUT_NUM_CLASSES             (1)     /* person only */
    #define AI_OUTPUT_NUM_ANCHORS             (3)     /* anchors per grid cell */
    #define AI_OUTPUT_BBOX_ATTRS              (4)     /* x, y, w, h */
    #define AI_OUTPUT_VALUES_PER_ANCHOR       (AI_OUTPUT_BBOX_ATTRS + 1 + AI_OUTPUT_NUM_CLASSES) /* 6: x,y,w,h,obj,cls */

    /* Branch 0: 6x6 grid (stride 32, large objects) */
    #define AI_OUTPUT_BRANCH0_GRID_W          (6)
    #define AI_OUTPUT_BRANCH0_GRID_H          (6)
    #define AI_OUTPUT_BRANCH0_STRIDE          (32)
    #define AI_OUTPUT_BRANCH0_SIZE            (AI_OUTPUT_BRANCH0_GRID_W * AI_OUTPUT_BRANCH0_GRID_H * AI_OUTPUT_NUM_ANCHORS * AI_OUTPUT_VALUES_PER_ANCHOR) /* 648 */

    /* Branch 1: 12x12 grid (stride 16, small objects) */
    #define AI_OUTPUT_BRANCH1_GRID_W          (12)
    #define AI_OUTPUT_BRANCH1_GRID_H          (12)
    #define AI_OUTPUT_BRANCH1_STRIDE          (16)
    #define AI_OUTPUT_BRANCH1_SIZE            (AI_OUTPUT_BRANCH1_GRID_W * AI_OUTPUT_BRANCH1_GRID_H * AI_OUTPUT_NUM_ANCHORS * AI_OUTPUT_VALUES_PER_ANCHOR) /* 2592 */

    /* Total output size */
    #define AI_OUTPUT_TENSOR_SIZE             (AI_OUTPUT_BRANCH0_SIZE + AI_OUTPUT_BRANCH1_SIZE) /* 3240 */

    /* Model input buffer size */
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
