/**
 * @file fall_detection_postprocess.c
 * @brief YOLOv3 anchor-based post-processing for fall detection (F-003-8)
 * @details
 * Implements the YOLO post-processing pipeline for the YOLO-Fastest V1
 * (YOLOv3-based, anchor-based) fall detection model with 2 output branches.
 *
 * The face detection reference sample uses the same YOLO-Fastest V1 architecture
 * with anchor-based detection and two output branches (6x6 and 12x12).
 * This implementation follows the same algorithm:
 *   - DetectorPostProcessing.cc GetNetworkBoxes(): anchor decoding per branch
 *   - ImageUtils.cc CalculateNMS(): non-maximum suppression
 *   - DetectorPostProcessing.cc DoPostProcess(): result assembly
 *
 * Implementation note:
 *   This is a C implementation (not C++) to avoid TFLite dependencies
 *   (TfLiteTensor, TfLiteAffineQuantization) that the reference uses.
 *   The MERA auto-generated code provides raw INT8 pointers without
 *   TFLite metadata structures.
 *
 * Reference: reference_projects/ruhmi-framework-mcu/application_examples/
 *            face_detection/src/ai_application/face_detection/DetectorPostProcessing.cc
 *
 * Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include "fall_detection_postprocess.h"

#include <string.h>
#include <math.h>

#include "common_util.h"
#include "ai_inference_thread_api.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * Minimum floating-point value to avoid division by zero
 */
#define EPSILON     (1e-7f)

/**
 * Clamp exp() input to avoid overflow.
 * exp(10) ~= 22026 which is large enough for anchor scaling.
 */
#define EXP_CLAMP_MAX   (10.0f)

/**********************************************************************************************************************
 Exported global variables
 *********************************************************************************************************************/

/**
 * Fall detection results array.
 * Sized to AI_MAX_DETECTION_NUM (10) as defined in ai_application_config.h.
 */
fall_detection_result_t g_fall_detection_results[AI_MAX_DETECTION_NUM];

/**
 * Number of valid detections in g_fall_detection_results[]
 */
volatile uint32_t g_fall_detection_count = 0;

/**
 * Post-processing runtime statistics
 */
fall_detection_postproc_stats_t g_postproc_stats = {0};

/**********************************************************************************************************************
 Private global variables
 *********************************************************************************************************************/

/** Post-processing configuration (initialized by fall_detection_postprocess_init) */
static fall_detection_postproc_config_t s_config;

/** Last output tensor pointers (for debug dump via ai_cmd) */
static const int8_t *s_last_branch0_tensor = NULL;
static const int8_t *s_last_branch1_tensor = NULL;

/**
 * Internal candidate structure used during post-processing.
 * Stores decoded bounding box in model input coordinate space and score.
 */
typedef struct {
    float x_center;     /**< Box center X in model coords (0-192) */
    float y_center;     /**< Box center Y in model coords (0-192) */
    float width;        /**< Box width in model coords */
    float height;       /**< Box height in model coords */
    float score;        /**< Confidence score (objectness * class_score, 0.0-1.0) */
    bool  suppressed;   /**< true if suppressed by NMS */
} postproc_candidate_t;

/** Working buffer for candidates that pass the confidence threshold */
static postproc_candidate_t s_candidates[POSTPROC_MAX_CANDIDATES];

/**********************************************************************************************************************
 Private function prototypes
 *********************************************************************************************************************/

static float sigmoid_f32(float x);
static float dequantize_int8(int8_t val, float scale, int zero_point);
static float calculate_iou(const postproc_candidate_t *a, const postproc_candidate_t *b);
static uint32_t decode_branch(const int8_t *tensor,
                               const fall_detection_branch_config_t *branch_cfg,
                               postproc_candidate_t *candidates,
                               uint32_t current_count,
                               uint32_t max_candidates);
static void apply_nms(postproc_candidate_t *candidates, uint32_t count, float iou_threshold);
static void sort_candidates_by_score(postproc_candidate_t *candidates, uint32_t count);

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Initialize the post-processing module with default configuration.
 *
 * Sets up per-branch anchor boxes, quantization parameters, and
 * coordinate conversion parameters.
 *
 * Reference: DetectorPostProcessing.cc constructor, GetNetworkBoxes() anchor setup
 */
void fall_detection_postprocess_init(void)
{
    memset(&s_config, 0, sizeof(s_config));

    s_config.confidence_threshold = POSTPROC_CONFIDENCE_THRESHOLD;
    s_config.nms_iou_threshold    = POSTPROC_NMS_IOU_THRESHOLD;
    s_config.num_classes          = AI_OUTPUT_NUM_CLASSES;
    s_config.model_input_width    = AI_INPUT_IMAGE_WIDTH;
    s_config.model_input_height   = AI_INPUT_IMAGE_HEIGHT;
    s_config.camera_width         = (int)CAM_QVGA_WIDTH;
    s_config.camera_height        = (int)CAM_QVGA_HEIGHT;
    s_config.max_detections       = AI_MAX_DETECTION_NUM;

    /* Branch 0: 6x6 grid, stride 32, large objects
     * Placeholder anchors from face detection reference - update after darknet anchor calculation
     * Reference: DetectorPostProcessing.cc lines 165-170 (face detection anchors)
     */
    s_config.branches[0].grid_w     = AI_OUTPUT_BRANCH0_GRID_W;
    s_config.branches[0].grid_h     = AI_OUTPUT_BRANCH0_GRID_H;
    s_config.branches[0].stride     = AI_OUTPUT_BRANCH0_STRIDE;
    s_config.branches[0].anchors[0][0] = 38.0f;  s_config.branches[0].anchors[0][1] = 77.0f;
    s_config.branches[0].anchors[1][0] = 47.0f;  s_config.branches[0].anchors[1][1] = 97.0f;
    s_config.branches[0].anchors[2][0] = 61.0f;  s_config.branches[0].anchors[2][1] = 126.0f;
    s_config.branches[0].scale      = POSTPROC_BRANCH0_SCALE;
    s_config.branches[0].zero_point = POSTPROC_BRANCH0_ZERO_POINT;

    /* Branch 1: 12x12 grid, stride 16, small objects
     * Placeholder anchors from face detection reference - update after darknet anchor calculation
     */
    s_config.branches[1].grid_w     = AI_OUTPUT_BRANCH1_GRID_W;
    s_config.branches[1].grid_h     = AI_OUTPUT_BRANCH1_GRID_H;
    s_config.branches[1].stride     = AI_OUTPUT_BRANCH1_STRIDE;
    s_config.branches[1].anchors[0][0] = 14.0f;  s_config.branches[1].anchors[0][1] = 26.0f;
    s_config.branches[1].anchors[1][0] = 19.0f;  s_config.branches[1].anchors[1][1] = 37.0f;
    s_config.branches[1].anchors[2][0] = 28.0f;  s_config.branches[1].anchors[2][1] = 55.0f;
    s_config.branches[1].scale      = POSTPROC_BRANCH1_SCALE;
    s_config.branches[1].zero_point = POSTPROC_BRANCH1_ZERO_POINT;

    memset(g_fall_detection_results, 0, sizeof(g_fall_detection_results));
    g_fall_detection_count = 0;
    memset(&g_postproc_stats, 0, sizeof(g_postproc_stats));
}

/**
 * Execute YOLOv3 anchor-based post-processing on the model output tensors.
 *
 * The overall flow follows the same pattern as the face detection reference:
 *   1. Decode each branch's output tensor -> candidate list
 *      (Reference: DetectorPostProcessing.cc GetNetworkBoxes() lines 155-241)
 *   2. Apply NMS
 *      (Reference: ImageUtils.cc CalculateNMS() lines 74-96)
 *   3. Convert to result format
 *      (Reference: DetectorPostProcessing.cc DoPostProcess() lines 73-123)
 *   4. Update g_ai_detection[] via update_detection_result()
 *      (Reference: MainLoop_obj.cc PresentInferenceResult() lines 83-94)
 *
 * @param[in] branch0_tensor  Pointer to INT8 branch 0 tensor (6x6, 648 bytes)
 * @param[in] branch1_tensor  Pointer to INT8 branch 1 tensor (12x12, 2592 bytes)
 * @return Number of detections found
 */
uint32_t fall_detection_postprocess(const int8_t *branch0_tensor, const int8_t *branch1_tensor)
{
    if (branch0_tensor == NULL && branch1_tensor == NULL)
    {
        g_fall_detection_count = 0;
        return 0;
    }

    /* Save pointers for debug dump (ai tensor command) */
    s_last_branch0_tensor = branch0_tensor;
    s_last_branch1_tensor = branch1_tensor;

    /* Step 1: Decode both branches and collect candidates above threshold
     *
     * YOLOv3 output tensor layout per branch: [grid_h, grid_w, num_anchors, values_per_anchor]
     *   tensor[cy][cx][a][v] where v = {tx, ty, tw, th, objectness, class_score}
     *
     * Reference: DetectorPostProcessing.cc GetNetworkBoxes() lines 155-241
     */
    uint32_t num_candidates = 0;

    if (branch0_tensor != NULL)
    {
        num_candidates = decode_branch(branch0_tensor, &s_config.branches[0],
                                        s_candidates, num_candidates,
                                        POSTPROC_MAX_CANDIDATES);
    }

    if (branch1_tensor != NULL)
    {
        num_candidates = decode_branch(branch1_tensor, &s_config.branches[1],
                                        s_candidates, num_candidates,
                                        POSTPROC_MAX_CANDIDATES);
    }

    g_postproc_stats.last_candidates = num_candidates;

    /* Step 2: Sort candidates by score (descending) for NMS */
    if (num_candidates > 1)
    {
        sort_candidates_by_score(s_candidates, num_candidates);
    }

    /* Step 3: Apply NMS
     *
     * Reference: ImageUtils.cc CalculateNMS() lines 74-96
     * The reference uses std::forward_list; this implementation uses a flat array
     * with a 'suppressed' flag for simplicity on the embedded target.
     */
    if (num_candidates > 1)
    {
        apply_nms(s_candidates, num_candidates, s_config.nms_iou_threshold);
    }

    /* Step 4: Convert surviving candidates to results
     *
     * Coordinate conversion: model input space (192x192) -> camera space (320x240)
     *
     * The preprocessing (F-003-6) performs center-crop from 320x240 to 240x240,
     * then resizes to 192x192. So we reverse:
     *   model_coord / 192 * 240 = crop_coord
     *   camera_x = crop_coord + crop_offset_x  (crop_offset_x = (320-240)/2 = 40)
     *   camera_y = crop_coord
     *
     * Reference: DetectorPostProcessing.cc DoPostProcess() lines 86-107
     */
    const float crop_size = (float)s_config.camera_height;  /* 240: square crop from camera */
    const float crop_offset_x = (float)(s_config.camera_width - s_config.camera_height) / 2.0f;  /* 40 */
    const float scale_x = crop_size / (float)s_config.model_input_width;   /* 240/192 = 1.25 */
    const float scale_y = crop_size / (float)s_config.model_input_height;  /* 240/192 = 1.25 */

    uint32_t detection_count = 0;

    /* Clear all detection results first
     * Reference: MainLoop_obj.cc PresentInferenceResult() lines 85-88
     */
    for (int i = 0; i < s_config.max_detections; i++)
    {
        update_detection_result((uint16_t)i, 0, 0, 0, 0);
        memset(&g_fall_detection_results[i], 0, sizeof(fall_detection_result_t));
    }

    for (uint32_t i = 0; i < num_candidates && detection_count < (uint32_t)s_config.max_detections; i++)
    {
        if (s_candidates[i].suppressed)
        {
            continue;
        }

        /* Convert center-format (x_center, y_center, w, h) to corner-format (x1, y1, w, h)
         * in model coordinate space first, then scale to camera space.
         *
         * Reference: DetectorPostProcessing.cc DoPostProcess() lines 86-101
         */
        float x_min_model = s_candidates[i].x_center - s_candidates[i].width / 2.0f;
        float y_min_model = s_candidates[i].y_center - s_candidates[i].height / 2.0f;
        float w_model = s_candidates[i].width;
        float h_model = s_candidates[i].height;

        /* Clamp to model input bounds */
        if (x_min_model < 0.0f) x_min_model = 0.0f;
        if (y_min_model < 0.0f) y_min_model = 0.0f;
        if (x_min_model + w_model > (float)s_config.model_input_width)
            w_model = (float)s_config.model_input_width - x_min_model;
        if (y_min_model + h_model > (float)s_config.model_input_height)
            h_model = (float)s_config.model_input_height - y_min_model;

        /* Scale to camera coordinate space */
        float cam_x = x_min_model * scale_x + crop_offset_x;
        float cam_y = y_min_model * scale_y;
        float cam_w = w_model * scale_x;
        float cam_h = h_model * scale_y;

        /* Clamp to camera bounds */
        if (cam_x < 0.0f) cam_x = 0.0f;
        if (cam_y < 0.0f) cam_y = 0.0f;
        if (cam_x + cam_w > (float)s_config.camera_width)
            cam_w = (float)s_config.camera_width - cam_x;
        if (cam_y + cam_h > (float)s_config.camera_height)
            cam_h = (float)s_config.camera_height - cam_y;

        /* Store in fall_detection_result_t */
        g_fall_detection_results[detection_count].x        = (uint16_t)cam_x;
        g_fall_detection_results[detection_count].y        = (uint16_t)cam_y;
        g_fall_detection_results[detection_count].width    = (uint16_t)cam_w;
        g_fall_detection_results[detection_count].height   = (uint16_t)cam_h;
        g_fall_detection_results[detection_count].score    = s_candidates[i].score;
        g_fall_detection_results[detection_count].class_id = 0;  /* person */

        /* Also update g_ai_detection[] for legacy display compatibility
         * Reference: MainLoop_obj.cc PresentInferenceResult() lines 90-91
         */
        update_detection_result((uint16_t)detection_count,
                                (signed short)cam_x,
                                (signed short)cam_y,
                                (signed short)cam_w,
                                (signed short)cam_h);

        detection_count++;
    }

    g_fall_detection_count = detection_count;

    /* Update statistics */
    g_postproc_stats.call_count++;
    g_postproc_stats.last_detections = detection_count;

    return detection_count;
}

/**
 * Get the current post-processing configuration.
 */
const fall_detection_postproc_config_t *fall_detection_postprocess_get_config(void)
{
    return &s_config;
}

/**
 * Set the confidence threshold.
 */
void fall_detection_postprocess_set_confidence(float threshold)
{
    if (threshold >= 0.0f && threshold <= 1.0f)
    {
        s_config.confidence_threshold = threshold;
    }
}

/**
 * Get the last output tensor pointers (for debug dump).
 */
void fall_detection_postprocess_get_output_tensors(const int8_t **branch0, const int8_t **branch1)
{
    if (branch0 != NULL)
    {
        *branch0 = s_last_branch0_tensor;
    }
    if (branch1 != NULL)
    {
        *branch1 = s_last_branch1_tensor;
    }
}

/**
 * Set the NMS IoU threshold.
 */
void fall_detection_postprocess_set_nms_iou(float iou_threshold)
{
    if (iou_threshold >= 0.0f && iou_threshold <= 1.0f)
    {
        s_config.nms_iou_threshold = iou_threshold;
    }
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Sigmoid activation function.
 *
 * Reference: PlatformMath.cc SigmoidF32()
 *
 * @param[in] x Input value
 * @return Sigmoid output in range (0.0, 1.0)
 */
static float sigmoid_f32(float x)
{
    return 1.0f / (1.0f + expf(-x));
}

/**
 * Dequantize an INT8 value to float.
 *
 * Formula: float_val = (int8_val - zero_point) * scale
 *
 * Reference: DetectorPostProcessing.cc GetNetworkBoxes() lines 178-181
 *
 * @param[in] val        INT8 quantized value
 * @param[in] scale      Quantization scale factor
 * @param[in] zero_point Quantization zero point
 * @return Dequantized float value
 */
static float dequantize_int8(int8_t val, float scale, int zero_point)
{
    return ((float)val - (float)zero_point) * scale;
}

/**
 * Calculate IoU (Intersection over Union) between two candidates.
 *
 * Uses center-format (x_center, y_center, width, height) coordinates.
 *
 * Reference: ImageUtils.cc CalculateBoxIOU() lines 59-72
 *            ImageUtils.cc Calculate1DOverlap() lines 24-35
 *
 * @param[in] a First candidate
 * @param[in] b Second candidate
 * @return IoU value in range [0.0, 1.0]
 */
static float calculate_iou(const postproc_candidate_t *a, const postproc_candidate_t *b)
{
    /* Calculate 1D overlap for X axis
     * Reference: ImageUtils.cc Calculate1DOverlap() lines 24-35
     */
    float a_left  = a->x_center - a->width / 2.0f;
    float a_right = a->x_center + a->width / 2.0f;
    float b_left  = b->x_center - b->width / 2.0f;
    float b_right = b->x_center + b->width / 2.0f;

    float x_overlap = fminf(a_right, b_right) - fmaxf(a_left, b_left);
    if (x_overlap <= 0.0f)
    {
        return 0.0f;
    }

    /* Calculate 1D overlap for Y axis */
    float a_top    = a->y_center - a->height / 2.0f;
    float a_bottom = a->y_center + a->height / 2.0f;
    float b_top    = b->y_center - b->height / 2.0f;
    float b_bottom = b->y_center + b->height / 2.0f;

    float y_overlap = fminf(a_bottom, b_bottom) - fmaxf(a_top, b_top);
    if (y_overlap <= 0.0f)
    {
        return 0.0f;
    }

    /* Intersection and union
     * Reference: ImageUtils.cc CalculateBoxIntersect/Union lines 37-57
     */
    float intersection = x_overlap * y_overlap;
    float area_a = a->width * a->height;
    float area_b = b->width * b->height;
    float union_area = area_a + area_b - intersection;

    if (union_area <= EPSILON)
    {
        return 0.0f;
    }

    return intersection / union_area;
}

/**
 * Decode one branch of the YOLOv3 output tensor using anchor-based decoding.
 *
 * YOLOv3 output tensor layout per branch: [grid_h, grid_w, num_anchors, values_per_anchor]
 *   tensor[cy][cx][a][v]
 *   v=0: tx (raw x offset)
 *   v=1: ty (raw y offset)
 *   v=2: tw (raw width)
 *   v=3: th (raw height)
 *   v=4: objectness (raw)
 *   v=5: class_score (raw)
 *
 * Decoding:
 *   objectness = sigmoid(dequant(raw_obj))
 *   x_center = (sigmoid(dequant(tx)) + cx) * stride
 *   y_center = (sigmoid(dequant(ty)) + cy) * stride
 *   width    = exp(dequant(tw)) * anchor_w
 *   height   = exp(dequant(th)) * anchor_h
 *   class_score = sigmoid(dequant(raw_cls)) * objectness
 *
 * Reference: DetectorPostProcessing.cc GetNetworkBoxes() lines 155-241
 *
 * @param[in]     tensor          INT8 branch output tensor
 * @param[in]     branch_cfg      Branch configuration (grid, stride, anchors, quant params)
 * @param[in,out] candidates      Candidate buffer (appends to existing entries)
 * @param[in]     current_count   Number of existing candidates in buffer
 * @param[in]     max_candidates  Maximum buffer capacity
 * @return Updated candidate count (current_count + newly added)
 */
static uint32_t decode_branch(const int8_t *tensor,
                               const fall_detection_branch_config_t *branch_cfg,
                               postproc_candidate_t *candidates,
                               uint32_t current_count,
                               uint32_t max_candidates)
{
    const int grid_w = branch_cfg->grid_w;
    const int grid_h = branch_cfg->grid_h;
    const int stride = branch_cfg->stride;
    const float scale = branch_cfg->scale;
    const int zp = branch_cfg->zero_point;
    const float threshold = s_config.confidence_threshold;
    const int num_anchors = AI_OUTPUT_NUM_ANCHORS;
    const int values_per_anchor = AI_OUTPUT_VALUES_PER_ANCHOR;

    uint32_t count = current_count;

    for (int cy = 0; cy < grid_h; cy++)
    {
        for (int cx = 0; cx < grid_w; cx++)
        {
            for (int a = 0; a < num_anchors; a++)
            {
                if (count >= max_candidates)
                {
                    return count;
                }

                /* Calculate offset into the tensor
                 * Layout: [grid_h, grid_w, num_anchors, values_per_anchor]
                 * offset = ((cy * grid_w + cx) * num_anchors + a) * values_per_anchor
                 *
                 * Reference: DetectorPostProcessing.cc GetNetworkBoxes() line 176
                 */
                int offset = ((cy * grid_w + cx) * num_anchors + a) * values_per_anchor;

                /* Dequantize all 6 values */
                float tx_dq  = dequantize_int8(tensor[offset + 0], scale, zp);
                float ty_dq  = dequantize_int8(tensor[offset + 1], scale, zp);
                float tw_dq  = dequantize_int8(tensor[offset + 2], scale, zp);
                float th_dq  = dequantize_int8(tensor[offset + 3], scale, zp);
                float obj_dq = dequantize_int8(tensor[offset + 4], scale, zp);
                float cls_dq = dequantize_int8(tensor[offset + 5], scale, zp);

                /* Check objectness first (early exit for efficiency)
                 * Reference: DetectorPostProcessing.cc GetNetworkBoxes() lines 188-192
                 */
                float objectness = sigmoid_f32(obj_dq);
                if (objectness < threshold)
                {
                    continue;
                }

                /* Compute class score = sigmoid(cls) * objectness
                 * Reference: DetectorPostProcessing.cc GetNetworkBoxes() lines 193-200
                 */
                float class_score = sigmoid_f32(cls_dq) * objectness;
                if (class_score < threshold)
                {
                    continue;
                }

                /* Decode bounding box coordinates
                 * Reference: DetectorPostProcessing.cc GetNetworkBoxes() lines 202-210
                 *
                 * x_center = (sigmoid(tx) + cx) * stride
                 * y_center = (sigmoid(ty) + cy) * stride
                 * width    = exp(tw) * anchor_w
                 * height   = exp(th) * anchor_h
                 */
                float x_center = (sigmoid_f32(tx_dq) + (float)cx) * (float)stride;
                float y_center = (sigmoid_f32(ty_dq) + (float)cy) * (float)stride;

                /* Clamp exp input to avoid overflow */
                if (tw_dq > EXP_CLAMP_MAX) tw_dq = EXP_CLAMP_MAX;
                if (th_dq > EXP_CLAMP_MAX) th_dq = EXP_CLAMP_MAX;

                float w = expf(tw_dq) * branch_cfg->anchors[a][0];
                float h = expf(th_dq) * branch_cfg->anchors[a][1];

                /* Sanity check: skip invalid boxes */
                if (w <= 0.0f || h <= 0.0f)
                {
                    continue;
                }

                candidates[count].x_center   = x_center;
                candidates[count].y_center   = y_center;
                candidates[count].width      = w;
                candidates[count].height     = h;
                candidates[count].score      = class_score;
                candidates[count].suppressed = false;
                count++;
            }
        }
    }

    return count;
}

/**
 * Apply Non-Maximum Suppression (NMS) on the candidate list.
 *
 * Uses a simple greedy NMS approach: iterate through candidates sorted by
 * descending score, suppress any later candidate that overlaps above the
 * IoU threshold with an earlier non-suppressed candidate.
 *
 * Reference: ImageUtils.cc CalculateNMS() lines 74-96
 * The reference iterates per class; since we have only 1 class (person),
 * we can simplify to a single pass.
 *
 * @param[in,out] candidates Candidate array (suppressed flag is set)
 * @param[in]     count      Number of candidates
 * @param[in]     iou_threshold IoU threshold for suppression
 */
static void apply_nms(postproc_candidate_t *candidates, uint32_t count, float iou_threshold)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (candidates[i].suppressed)
        {
            continue;
        }

        for (uint32_t j = i + 1; j < count; j++)
        {
            if (candidates[j].suppressed)
            {
                continue;
            }

            float iou = calculate_iou(&candidates[i], &candidates[j]);
            if (iou > iou_threshold)
            {
                candidates[j].suppressed = true;
            }
        }
    }
}

/**
 * Sort candidates by score in descending order (insertion sort).
 *
 * Uses insertion sort which is efficient for small arrays (POSTPROC_MAX_CANDIDATES = 50)
 * and avoids dynamic memory allocation.
 *
 * Reference: DetectorPostProcessing.cc InsertTopNDetections() lines 130-143
 * The reference maintains a sorted forward_list during insertion.
 *
 * @param[in,out] candidates Candidate array to sort
 * @param[in]     count      Number of candidates
 */
static void sort_candidates_by_score(postproc_candidate_t *candidates, uint32_t count)
{
    for (uint32_t i = 1; i < count; i++)
    {
        postproc_candidate_t key = candidates[i];
        int j = (int)i - 1;

        while (j >= 0 && candidates[j].score < key.score)
        {
            candidates[j + 1] = candidates[j];
            j--;
        }
        candidates[j + 1] = key;
    }
}
