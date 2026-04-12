/**
 * @file fall_detection_postprocess.h
 * @brief YOLOv3 anchor-based post-processing for fall detection (F-003-8)
 * @details
 * Implements YOLO post-processing for the YOLO-Fastest V1 (YOLOv3-based,
 * anchor-based) fall detection model with 2 output branches.
 *
 * Model output tensor layout:
 *   Branch 0 (6x6 grid, stride 32):   648 bytes INT8
 *   Branch 1 (12x12 grid, stride 16): 2592 bytes INT8
 *   Each: [grid_h, grid_w, num_anchors, values_per_anchor]
 *   values_per_anchor = 6: (tx, ty, tw, th, objectness, class_score)
 *
 * Post-processing pipeline:
 *   1. Dequantize INT8 -> float: float_val = (int8_val - zero_point) * scale
 *   2. objectness = sigmoid(raw_obj), threshold check
 *   3. bbox decode: x = (sigmoid(tx) + cx) * stride,
 *                   y = (sigmoid(ty) + cy) * stride,
 *                   w = exp(tw) * anchor_w,
 *                   h = exp(th) * anchor_h
 *   4. class_score = sigmoid(raw_cls) * objectness
 *   5. Filter by confidence threshold
 *   6. Apply NMS (Non-Maximum Suppression)
 *   7. Convert model coordinates (192x192) -> camera coordinates (320x240)
 *
 * Reference: reference_projects/ruhmi-framework-mcu/application_examples/
 *            face_detection/src/ai_application/face_detection/DetectorPostProcessing.hpp
 *
 * Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FALL_DETECTION_POSTPROCESS_H
#define FALL_DETECTION_POSTPROCESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "ai_application/ai_application_config.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * Maximum number of candidate detections before NMS.
 * This limits the working buffer size to avoid excessive stack/heap usage.
 * With 2 branches: 6x6x3 + 12x12x3 = 108 + 432 = 540 total anchor boxes,
 * but only a small fraction will pass the confidence threshold.
 */
#define POSTPROC_MAX_CANDIDATES     (50)

/**
 * Number of output branches in the YOLO-Fastest V1 model.
 */
#define POSTPROC_NUM_BRANCHES       (2)

/**
 * Per-branch quantization parameters.
 *
 * From TFLite INT8 model (yolo_fastest_person_darknet_int8.tflite, Issue #131):
 *   Branch 0 (6x6, StatefulPartitionedCall:0): scale=0.10909886, zp=34
 *   Branch 1 (12x12, StatefulPartitionedCall:1): scale=0.10983318, zp=23
 *
 * Dequantization formula: float_val = (int8_val - zero_point) * scale
 */
#define POSTPROC_BRANCH0_SCALE       (0.10909886f)  /* StatefulPartitionedCall:0 (6x6) */
#define POSTPROC_BRANCH0_ZERO_POINT  (34)           /* StatefulPartitionedCall:0 (6x6) */
#define POSTPROC_BRANCH1_SCALE       (0.10983318f)  /* StatefulPartitionedCall:1 (12x12) */
#define POSTPROC_BRANCH1_ZERO_POINT  (23)           /* StatefulPartitionedCall:1 (12x12) */

/**
 * Default detection confidence threshold.
 * Detections with (sigmoid(objectness) * sigmoid(class_score)) below
 * this value are discarded.
 */
#define POSTPROC_CONFIDENCE_THRESHOLD   (0.5f)

/**
 * Default NMS IoU (Intersection over Union) threshold.
 * Overlapping boxes with IoU above this value are suppressed.
 */
#define POSTPROC_NMS_IOU_THRESHOLD      (0.45f)

/**********************************************************************************************************************
 Typedef definitions
 *********************************************************************************************************************/

/**
 * Fall detection result structure (F-003-8)
 *
 * Represents a single detected object with bounding box coordinates
 * in the camera image coordinate space (320x240).
 *
 * Reference: Issue #25 specification
 */
typedef struct fall_detection_result_t {
    uint16_t x;         /**< Bounding box top-left X (camera coords) */
    uint16_t y;         /**< Bounding box top-left Y (camera coords) */
    uint16_t width;     /**< Bounding box width (camera coords) */
    uint16_t height;    /**< Bounding box height (camera coords) */
    float    score;     /**< Detection confidence score (0.0 - 1.0) */
    uint8_t  class_id;  /**< Class ID (0: person) */
} fall_detection_result_t;

/**
 * Per-branch configuration for YOLOv3 anchor-based decoding.
 *
 * Each branch corresponds to a different feature map scale:
 *   Branch 0: 6x6 grid, stride 32 (large objects)
 *   Branch 1: 12x12 grid, stride 16 (small objects)
 *
 * Reference: DetectorPostProcessing.cc GetNetworkBoxes() - net.branches[i]
 */
typedef struct {
    int   grid_w;                                  /**< Grid width (6 or 12) */
    int   grid_h;                                  /**< Grid height (6 or 12) */
    int   stride;                                  /**< Feature map stride (32 or 16) */
    float anchors[AI_OUTPUT_NUM_ANCHORS][2];       /**< Anchor (w, h) pairs in pixels */
    float scale;                                   /**< INT8 quantization scale */
    int   zero_point;                              /**< INT8 quantization zero point */
} fall_detection_branch_config_t;

/**
 * Post-processing configuration parameters
 */
typedef struct fall_detection_postproc_config_t {
    float    confidence_threshold;  /**< Minimum confidence to keep a detection */
    float    nms_iou_threshold;     /**< IoU threshold for NMS suppression */
    fall_detection_branch_config_t branches[POSTPROC_NUM_BRANCHES]; /**< branch0: 6x6, branch1: 12x12 */
    int      num_classes;           /**< Number of detection classes (1) */
    int      model_input_width;     /**< Model input width (192) */
    int      model_input_height;    /**< Model input height (192) */
    int      camera_width;          /**< Camera image width (320) */
    int      camera_height;         /**< Camera image height (240) */
    int      max_detections;        /**< Maximum detections to return */
} fall_detection_postproc_config_t;

/**
 * Post-processing runtime statistics (for diagnostics)
 */
typedef struct fall_detection_postproc_stats_t {
    uint32_t call_count;            /**< Total post-process invocations */
    uint32_t last_candidates;       /**< Candidates above threshold (before NMS) */
    uint32_t last_detections;       /**< Detections after NMS */
    uint32_t last_time_us;          /**< Last execution time (microseconds) */
} fall_detection_postproc_stats_t;

/**********************************************************************************************************************
 Exported global variables
 *********************************************************************************************************************/

/**
 * Fall detection results array.
 * Updated by fall_detection_postprocess() after each inference.
 * Read by display thread and "ai detect" command.
 */
extern fall_detection_result_t g_fall_detection_results[];

/**
 * Number of valid detections in g_fall_detection_results[]
 */
extern volatile uint32_t g_fall_detection_count;

/**
 * Post-processing runtime statistics
 */
extern fall_detection_postproc_stats_t g_postproc_stats;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Initialize the post-processing module with default configuration.
 *
 * Initializes the global config with default values including per-branch
 * anchor boxes, quantization parameters, and coordinate conversion params.
 * Must be called once before fall_detection_postprocess().
 */
void fall_detection_postprocess_init(void);

/**
 * Execute YOLOv3 anchor-based post-processing on the model output tensors.
 *
 * Processing pipeline:
 *   1. For each branch: dequantize INT8, decode anchors, filter by confidence
 *   2. Merge candidates from both branches
 *   3. Apply NMS to suppress overlapping boxes
 *   4. Convert model coordinates (192x192) to camera coordinates (320x240)
 *   5. Store results in g_fall_detection_results[] and update
 *      g_ai_detection[] via update_detection_result()
 *
 * @param[in] branch0_tensor  Pointer to INT8 output tensor for branch 0 (6x6, 648 bytes).
 *                             May be NULL if YOLO_FASTEST_MODEL=0 (stub mode).
 * @param[in] branch1_tensor  Pointer to INT8 output tensor for branch 1 (12x12, 2592 bytes).
 *                             May be NULL if YOLO_FASTEST_MODEL=0 (stub mode).
 *
 * @return Number of detections found (0 to max_detections)
 *
 * Reference: face_detection/src/ai_application/face_detection/DetectorPostProcessing.cc
 *            DoPostProcess() and GetNetworkBoxes()
 */
uint32_t fall_detection_postprocess(const int8_t *branch0_tensor, const int8_t *branch1_tensor);

/**
 * Get the current post-processing configuration (read-only).
 *
 * @return Pointer to the current configuration structure.
 */
const fall_detection_postproc_config_t *fall_detection_postprocess_get_config(void);

/**
 * Set the confidence threshold (for runtime tuning).
 *
 * @param[in] threshold New confidence threshold (0.0 - 1.0)
 */
void fall_detection_postprocess_set_confidence(float threshold);

/**
 * Set the NMS IoU threshold (for runtime tuning).
 *
 * @param[in] iou_threshold New IoU threshold (0.0 - 1.0)
 */
void fall_detection_postprocess_set_nms_iou(float iou_threshold);

/**
 * Get the last output tensor pointers (for debug raw dump).
 *
 * @param[out] branch0 Pointer to branch 0 tensor, or NULL if not yet run.
 * @param[out] branch1 Pointer to branch 1 tensor, or NULL if not yet run.
 */
void fall_detection_postprocess_get_output_tensors(const int8_t **branch0, const int8_t **branch1);

#ifdef __cplusplus
}
#endif

#endif /* FALL_DETECTION_POSTPROCESS_H */
