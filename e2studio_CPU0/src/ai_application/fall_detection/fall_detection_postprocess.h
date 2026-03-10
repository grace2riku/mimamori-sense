/**
 * @file fall_detection_postprocess.h
 * @brief YOLOv8 post-processing for fall detection (F-003-8)
 * @details
 * Implements YOLO post-processing for the YOLOv8 pico INT8 fall detection
 * model. Unlike the reference face detection sample which uses an anchor-based
 * YOLOv3/v4 architecture with two output branches, the fall detection model
 * uses YOLOv8's anchor-free architecture with a single output tensor.
 *
 * Model output tensor layout:
 *   Shape:  [1, 5, 756]  INT8
 *   Rows:   [x_center, y_center, w, h, class_score]
 *   Cols:   756 candidates = P3(24x24=576) + P4(12x12=144) + P5(6x6=36)
 *   Scale:  0.89210844
 *   Zero point: -128
 *
 * Post-processing pipeline:
 *   1. Dequantize INT8 -> float: float_val = (int8_val - zero_point) * scale
 *   2. Bbox coordinates are direct predictions in model input space (0-192)
 *   3. Class score is already sigmoid-applied by MERA sub_0001 (no sigmoid here)
 *   4. Filter by confidence threshold
 *   5. Apply NMS (Non-Maximum Suppression)
 *   6. Convert model coordinates (192x192) -> camera coordinates (320x240)
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

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * Maximum number of candidate detections before NMS.
 * This limits the working buffer size to avoid excessive stack/heap usage.
 * The model produces 756 candidates but only a small fraction will pass
 * the confidence threshold.
 */
#define POSTPROC_MAX_CANDIDATES     (50)

/**
 * YOLOv8 output tensor quantization parameters.
 *
 * These values are extracted from the INT8 TFLite model via Netron or
 * the training/quantization report (doc/analysis_report/f003_03_training_quantization_report.md).
 *
 * Dequantization formula: float_val = (int8_val - zero_point) * scale
 */
#define POSTPROC_OUTPUT_SCALE       (0.89210844f)
#define POSTPROC_OUTPUT_ZERO_POINT  (-128)

/**
 * Default detection confidence threshold.
 * Detections with sigmoid(class_score) below this value are discarded.
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
 * Post-processing configuration parameters
 */
typedef struct fall_detection_postproc_config_t {
    float    confidence_threshold;  /**< Minimum confidence to keep a detection */
    float    nms_iou_threshold;     /**< IoU threshold for NMS suppression */
    float    output_scale;          /**< INT8 quantization scale */
    int      output_zero_point;     /**< INT8 quantization zero point */
    int      num_candidates;        /**< Total number of output candidates (756) */
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
 * Initializes the global config with default values from
 * ai_application_config.h and the quantization parameters from the model.
 * Must be called once before fall_detection_postprocess().
 */
void fall_detection_postprocess_init(void);

/**
 * Execute YOLOv8 post-processing on the model output tensor.
 *
 * Processing pipeline:
 *   1. Dequantize INT8 output to float
 *   2. Apply sigmoid to class scores
 *   3. Filter by confidence threshold
 *   4. Apply NMS to suppress overlapping boxes
 *   5. Convert model coordinates (192x192) to camera coordinates (320x240)
 *   6. Store results in g_fall_detection_results[] and update
 *      g_ai_detection[] via update_detection_result()
 *
 * @param[in] output_tensor  Pointer to INT8 output tensor from mera_output_ptr().
 *                           Layout: [5, 756] row-major (5 rows, 756 columns)
 *                           Row 0: x_center, Row 1: y_center,
 *                           Row 2: width, Row 3: height,
 *                           Row 4: class_score (person)
 *
 * @return Number of detections found (0 to max_detections)
 *
 * Reference: face_detection/src/ai_application/face_detection/MainLoop_obj.cc
 *            main_loop_face_detection() (lines 105-145)
 */
uint32_t fall_detection_postprocess(const int8_t *output_tensor);

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
 * Get the last output tensor pointer (for debug raw dump).
 *
 * @return Pointer to INT8 output tensor, or NULL if no inference has run yet.
 */
const int8_t *fall_detection_postprocess_get_output_tensor(void);

#ifdef __cplusplus
}
#endif

#endif /* FALL_DETECTION_POSTPROCESS_H */
