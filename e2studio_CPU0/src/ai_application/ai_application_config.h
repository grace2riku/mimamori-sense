/**
 * @file ai_application_config.h
 * @brief AI inference model configuration for the active demo
 * @details
 * Defines AI model input image dimensions, channel count, and maximum
 * detection number based on the selected AI_DEMO type.
 *
 * Fall detection model (YOLO-Fastest V1, YOLOv3-based, anchor-based):
 *   - Input:  224x224x3 RGB INT8 (150528 bytes)   ... Issue #148 で 192 から変更
 *   - Output: 2 branches (anchor-based)
 *     - Branch 0 (7x7, stride 32): 7x7x3x6 = 882 bytes INT8
 *     - Branch 1 (14x14, stride 16): 14x14x3x6 = 3528 bytes INT8
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
     * Input: 224x224x3 RGB INT8
     * Output: 2 branches (7x7 + 14x14, each with 3 anchors x 6 values)
     * Classes: 1 (person)
     *
     * Issue #148 (2026-08): 入力解像度を 192 -> 224 に変更した。
     *   小物体の Recall が 6.1% で頭打ちだったため解像度を上げた (方針書 6.1.6/6.1.7)。
     *   mAP 49.1% -> 54.97%、Precision 51% -> 62% に改善。
     *   グリッドは stride 32/16 に追随して 6x6/12x12 から 7x7/14x14 になる。
     *   推論時間は 5.78ms -> 7.86ms (改定後 KPI 10ms 以内。product-requirements 3.1.1)。
     *   入力バッファは 110,592 -> 150,528 バイト。
     */
    #define AI_INPUT_IMAGE_WIDTH              (224)
    #define AI_INPUT_IMAGE_HEIGHT             (224)
    #define AI_INPUT_IMAGE_BYTE_PER_PIXEL     (3)     /* RGB: 3 bytes per pixel */

    #define AI_MAX_DETECTION_NUM              (10)

    /* YOLOv3 anchor-based output configuration */
    #define AI_OUTPUT_NUM_CLASSES             (1)     /* person only */
    #define AI_OUTPUT_NUM_ANCHORS             (3)     /* anchors per grid cell */
    #define AI_OUTPUT_BBOX_ATTRS              (4)     /* x, y, w, h */
    #define AI_OUTPUT_VALUES_PER_ANCHOR       (AI_OUTPUT_BBOX_ATTRS + 1 + AI_OUTPUT_NUM_CLASSES) /* 6: x,y,w,h,obj,cls */

    /* Branch 0: 7x7 grid (stride 32, large objects)
     * Issue #148: 224 / 32 = 7 (192px 時は 6) */
    #define AI_OUTPUT_BRANCH0_STRIDE          (32)
    #define AI_OUTPUT_BRANCH0_GRID_W          (AI_INPUT_IMAGE_WIDTH / AI_OUTPUT_BRANCH0_STRIDE)   /* 7 */
    #define AI_OUTPUT_BRANCH0_GRID_H          (AI_INPUT_IMAGE_HEIGHT / AI_OUTPUT_BRANCH0_STRIDE)  /* 7 */
    #define AI_OUTPUT_BRANCH0_SIZE            (AI_OUTPUT_BRANCH0_GRID_W * AI_OUTPUT_BRANCH0_GRID_H * AI_OUTPUT_NUM_ANCHORS * AI_OUTPUT_VALUES_PER_ANCHOR) /* 882 */

    /* Branch 1: 14x14 grid (stride 16, small objects)
     * Issue #148: 224 / 16 = 14 (192px 時は 12) */
    #define AI_OUTPUT_BRANCH1_STRIDE          (16)
    #define AI_OUTPUT_BRANCH1_GRID_W          (AI_INPUT_IMAGE_WIDTH / AI_OUTPUT_BRANCH1_STRIDE)   /* 14 */
    #define AI_OUTPUT_BRANCH1_GRID_H          (AI_INPUT_IMAGE_HEIGHT / AI_OUTPUT_BRANCH1_STRIDE)  /* 14 */
    #define AI_OUTPUT_BRANCH1_SIZE            (AI_OUTPUT_BRANCH1_GRID_W * AI_OUTPUT_BRANCH1_GRID_H * AI_OUTPUT_NUM_ANCHORS * AI_OUTPUT_VALUES_PER_ANCHOR) /* 3528 */

    /* Total output size */
    #define AI_OUTPUT_TENSOR_SIZE             (AI_OUTPUT_BRANCH0_SIZE + AI_OUTPUT_BRANCH1_SIZE) /* 4410 */

    /* Model input buffer size */
    #define AI_INPUT_IMAGE_SIZE               (AI_INPUT_IMAGE_WIDTH * AI_INPUT_IMAGE_HEIGHT * AI_INPUT_IMAGE_BYTE_PER_PIXEL) /* 150528 */

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
