/**
 * @file ai_cmd.c
 * @brief NT-Shell "ai" command implementation (F-003-5, F-003-6, F-003-7, F-003-8)
 * @details
 * Implements the "ai" command for NT-Shell, providing subcommands
 * to inspect AI model configuration and runtime status.
 *
 * Supported subcommands:
 *   ai model   - Display model information (input/output sizes, arena sizes)
 *   ai config  - Display AI application configuration constants
 *   ai preproc - Preprocessing status/timing (F-003-6)
 *   ai status  - Inference thread state (F-003-7)
 *   ai time    - Timing breakdown (F-003-7)
 *   ai detect  - Latest detection results (F-003-8)
 *   ai nms     - NMS parameter display/configuration (F-003-8)
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <math.h>
#include "ntlibc.h"
#include "jlink_console.h"
#include "ai_application_config.h"
#include "ai_cmd.h"
#include "camera_layer/camera_utils.h"
#include "camera_framebuffer.h"
#include "ai_inference_thread_api.h"
#include "common_util.h"
#include "common_data.h"
#include "bsp_mcu_ofs_cfg.h"
#include "fall_detection/fall_detection_postprocess.h"
#include "fall_detection/wrapper.h"
#include "fall_detection/mera/sub_0000_net1_tensors.h"
#include "fall_detection/mera/sub_0000_net1_invoke.h"
#if !YOLO_FASTEST_MODEL
#include "fall_detection/mera/sub_0002_net1_tensors.h"
#include "fall_detection/mera/sub_0002_net1_invoke.h"
#endif

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/
#define AI_CMD_BUF_SIZE     (128)

/**********************************************************************************************************************
 Private (static) functions prototypes
 *********************************************************************************************************************/
static void ai_cmd_model(void);
static void ai_cmd_config(void);
static void ai_cmd_preproc(void);
static void ai_cmd_status(void);
static void ai_cmd_time(void);
static void ai_cmd_detect(void);
static void ai_cmd_nms(int argc, char **argv);
static void ai_cmd_tensor(int argc, char **argv);
static void ai_cmd_diag(void);
static void ai_cmd_help(void);

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * NT-Shell "ai" command handler
 */
int usrcmd_ai(int argc, char **argv)
{
    if (argc < 2) {
        ai_cmd_help();
        return 0;
    }

    if (ntlibc_strcmp(argv[1], "model") == 0) {
        ai_cmd_model();
    } else if (ntlibc_strcmp(argv[1], "config") == 0) {
        ai_cmd_config();
    } else if (ntlibc_strcmp(argv[1], "preproc") == 0) {
        ai_cmd_preproc();
    } else if (ntlibc_strcmp(argv[1], "status") == 0) {
        ai_cmd_status();
    } else if (ntlibc_strcmp(argv[1], "time") == 0) {
        ai_cmd_time();
    } else if (ntlibc_strcmp(argv[1], "detect") == 0) {
        ai_cmd_detect();
    } else if (ntlibc_strcmp(argv[1], "nms") == 0) {
        ai_cmd_nms(argc, argv);
    } else if (ntlibc_strcmp(argv[1], "tensor") == 0) {
        ai_cmd_tensor(argc, argv);
    } else if (ntlibc_strcmp(argv[1], "diag") == 0) {
        ai_cmd_diag();
    } else if (ntlibc_strcmp(argv[1], "help") == 0) {
        ai_cmd_help();
    } else {
        print_to_console("Unknown subcommand: ");
        print_to_console(argv[1]);
        print_to_console("\r\n");
        ai_cmd_help();
    }

    return 0;
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Display model information
 */
static void ai_cmd_model(void)
{
    char buf[AI_CMD_BUF_SIZE];

    print_to_console("=== AI Model Information ===\r\n");

#if AI_DEMO == (FALL_DETECTION)
    print_to_console("Demo type      : FALL_DETECTION (YOLO-Fastest V1, YOLOv3-based)\r\n");
#elif AI_DEMO == (FACE_DETECTION)
    print_to_console("Demo type      : FACE_DETECTION\r\n");
#else
    print_to_console("Demo type      : IMAGE_CLASSIFICATION\r\n");
#endif

    snprintf(buf, sizeof(buf), "Input size     : %d x %d x %d (%d bytes)\r\n",
             AI_INPUT_IMAGE_WIDTH, AI_INPUT_IMAGE_HEIGHT,
             AI_INPUT_IMAGE_BYTE_PER_PIXEL,
             AI_INPUT_IMAGE_WIDTH * AI_INPUT_IMAGE_HEIGHT * AI_INPUT_IMAGE_BYTE_PER_PIXEL);
    print_to_console(buf);

#if AI_DEMO == (FALL_DETECTION)
    print_to_console("Architecture   : Anchor-based, 2 branches\r\n");

    snprintf(buf, sizeof(buf), "Branch 0       : %dx%d grid, stride %d (%d bytes)\r\n",
             AI_OUTPUT_BRANCH0_GRID_W, AI_OUTPUT_BRANCH0_GRID_H,
             AI_OUTPUT_BRANCH0_STRIDE, AI_OUTPUT_BRANCH0_SIZE);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "Branch 1       : %dx%d grid, stride %d (%d bytes)\r\n",
             AI_OUTPUT_BRANCH1_GRID_W, AI_OUTPUT_BRANCH1_GRID_H,
             AI_OUTPUT_BRANCH1_STRIDE, AI_OUTPUT_BRANCH1_SIZE);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "Total output   : %d bytes (%d + %d)\r\n",
             AI_OUTPUT_TENSOR_SIZE, AI_OUTPUT_BRANCH0_SIZE, AI_OUTPUT_BRANCH1_SIZE);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "Anchors/cell   : %d\r\n", AI_OUTPUT_NUM_ANCHORS);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "Values/anchor  : %d (x,y,w,h,obj,cls)\r\n", AI_OUTPUT_VALUES_PER_ANCHOR);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "Num classes    : %d (person)\r\n", AI_OUTPUT_NUM_CLASSES);
    print_to_console(buf);

#if YOLO_FASTEST_MODEL
    print_to_console("MERA model     : YOLO-Fastest V1 (single subgraph)\r\n");
#else
    print_to_console("MERA model     : YOLOv8 legacy (YOLO_FASTEST_MODEL=0)\r\n");
#endif
#endif

    snprintf(buf, sizeof(buf), "Max detections : %d\r\n", AI_MAX_DETECTION_NUM);
    print_to_console(buf);
}

/**
 * Display AI application configuration
 */
static void ai_cmd_config(void)
{
    char buf[AI_CMD_BUF_SIZE];

    print_to_console("=== AI Application Config ===\r\n");

    snprintf(buf, sizeof(buf), "AI_DEMO                    : %d", AI_DEMO);
    print_to_console(buf);
#if AI_DEMO == (FALL_DETECTION)
    print_to_console(" (FALL_DETECTION)\r\n");
#elif AI_DEMO == (FACE_DETECTION)
    print_to_console(" (FACE_DETECTION)\r\n");
#else
    print_to_console(" (IMAGE_CLASSIFICATION)\r\n");
#endif

    snprintf(buf, sizeof(buf), "AI_INPUT_IMAGE_WIDTH       : %d\r\n", AI_INPUT_IMAGE_WIDTH);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "AI_INPUT_IMAGE_HEIGHT      : %d\r\n", AI_INPUT_IMAGE_HEIGHT);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "AI_INPUT_IMAGE_BYTE_PER_PX : %d\r\n", AI_INPUT_IMAGE_BYTE_PER_PIXEL);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "AI_MAX_DETECTION_NUM       : %d\r\n", AI_MAX_DETECTION_NUM);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "AI_INPUT_IMAGE_ALLOCATION  : %d\r\n", AI_INPUT_IMAGE_ALLOCATION);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "AI_MODEL_ALLOCATION        : %d\r\n", AI_MODEL_ALLOCATION);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "TENSOR_ARENA_ALLOCATION    : %d\r\n", TENSOR_ARENA_ALLOCATION);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "ENABLE_CAMERA_INPUT        : %d\r\n", ENABLE_CAMERA_INPUT);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "ENABLE_LCD_DISPLAY_OUTPUT  : %d\r\n", ENABLE_LCD_DISPLAY_OUTPUT);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "YOLO_FASTEST_MODEL         : %d\r\n", YOLO_FASTEST_MODEL);
    print_to_console(buf);
}

/**
 * Display preprocessing status and timing (F-003-6)
 */
static void ai_cmd_preproc(void)
{
    char buf[AI_CMD_BUF_SIZE];

    print_to_console("=== AI Preprocessing Status (F-003-6) ===\r\n");

    print_to_console("Function       : image_rgb565_to_rgb_int8()\r\n");

    snprintf(buf, sizeof(buf), "Input          : %u x %u RGB565 (%u bytes)\r\n",
             (unsigned int)CAMERA_FRAME_WIDTH,
             (unsigned int)CAMERA_FRAME_HEIGHT,
             (unsigned int)(CAMERA_FRAME_WIDTH * CAMERA_FRAME_HEIGHT * 2u));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "Output         : %d x %d x %d RGB INT8 (%d bytes)\r\n",
             AI_INPUT_IMAGE_WIDTH, AI_INPUT_IMAGE_HEIGHT,
             AI_INPUT_IMAGE_BYTE_PER_PIXEL,
             AI_INPUT_IMAGE_WIDTH * AI_INPUT_IMAGE_HEIGHT * AI_INPUT_IMAGE_BYTE_PER_PIXEL);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "Crop offset    : %u px (center crop %u x %u -> %u x %u)\r\n",
             (unsigned int)((CAMERA_FRAME_WIDTH - CAMERA_FRAME_HEIGHT) / 2u),
             (unsigned int)CAMERA_FRAME_WIDTH,
             (unsigned int)CAMERA_FRAME_HEIGHT,
             (unsigned int)CAMERA_FRAME_HEIGHT,
             (unsigned int)CAMERA_FRAME_HEIGHT);
    print_to_console(buf);

    print_to_console("Resize         : Nearest neighbor\r\n");
    print_to_console("Normalization  : uint8 [0,255] -> int8 [-128,+127]\r\n");

    snprintf(buf, sizeof(buf), "Call count     : %lu\r\n",
             (unsigned long)camera_utils_get_preproc_count());
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "Last time      : %lu ms\r\n",
             (unsigned long)camera_utils_get_preproc_time_ms());
    print_to_console(buf);
}

/**
 * Display AI inference thread status (F-003-7)
 *
 * Shows the current inference thread state, event group bits,
 * and inference count.
 */
static void ai_cmd_status(void)
{
    char buf[AI_CMD_BUF_SIZE];

    print_to_console("=== AI Inference Status (F-003-7) ===\r\n");

    snprintf(buf, sizeof(buf), "Thread state   : %s (%lu)\r\n",
             ai_inference_get_state_name(),
             (unsigned long)ai_inference_get_state());
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "Inference count: %lu\r\n",
             (unsigned long)ai_inference_get_count());
    print_to_console(buf);

    /* Show event group bits if g_ai_app_event has been created */
    if (g_ai_app_event != NULL)
    {
        EventBits_t bits = xEventGroupGetBits(g_ai_app_event);
        snprintf(buf, sizeof(buf), "Event bits     : 0x%04lX\r\n",
                 (unsigned long)bits);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  ETHOSU_INIT  : %s (bit 2)\r\n",
                 (bits & HARDWARE_ETHOSU_INIT_DONE) ? "SET" : "clear");
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  AI_INIT      : %s (bit 3)\r\n",
                 (bits & SOFTWARE_AI_INFERENCE_INIT_DONE) ? "SET" : "clear");
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  IMAGE_READY  : %s (bit 13)\r\n",
                 (bits & AI_INFERENCE_INPUT_IMAGE_READY) ? "SET" : "clear");
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "  RESULT_UPD   : %s (bit 14)\r\n",
                 (bits & AI_INFERENCE_RESULT_UPDATED) ? "SET" : "clear");
        print_to_console(buf);
    }
    else
    {
        print_to_console("Event group    : (not yet created)\r\n");
    }

    snprintf(buf, sizeof(buf), "Input buffer   : 0x%08lX (%lu bytes)\r\n",
             (unsigned long)(uintptr_t)model_buffer_int8,
             (unsigned long)model_buffer_int8_size);
    print_to_console(buf);
}

/**
 * Display AI inference timing breakdown (F-003-7)
 *
 * Shows the DWT cycle counter based timing for each stage of
 * the inference pipeline.
 */
static void ai_cmd_time(void)
{
    char buf[AI_CMD_BUF_SIZE];

    print_to_console("=== AI Inference Timing (F-003-7) ===\r\n");

    uint32_t preproc_ms = camera_utils_get_preproc_time_ms();
    uint32_t memcpy_ms  = ai_inference_get_memcpy_time_ms();
    uint32_t invoke_ms  = ai_inference_get_invoke_time_ms();
    uint32_t total_ms   = ai_inference_get_total_time_ms();

    snprintf(buf, sizeof(buf), "Preprocessing  : %lu ms (image_rgb565_to_rgb_int8)\r\n",
             (unsigned long)preproc_ms);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "Input memcpy   : %lu ms (model_buffer -> MERA arena)\r\n",
             (unsigned long)memcpy_ms);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "NPU inference  : %lu ms (mera_invoke)\r\n",
             (unsigned long)invoke_ms);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "Total cycle    : %lu ms (memcpy + invoke + postproc)\r\n",
             (unsigned long)total_ms);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "End-to-end     : %lu ms (preproc + total cycle)\r\n",
             (unsigned long)(preproc_ms + total_ms));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "Inference count: %lu\r\n",
             (unsigned long)ai_inference_get_count());
    print_to_console(buf);

    /* Processing time from global structure */
    snprintf(buf, sizeof(buf), "g_processing_time.ai_inference_time_ms: %lu\r\n",
             (unsigned long)g_processing_time.ai_inference_time_ms);
    print_to_console(buf);
}

/**
 * Display latest detection results (F-003-8)
 *
 * Shows the contents of g_fall_detection_results[] array with
 * bounding box coordinates, confidence scores, and class IDs.
 * Also shows the legacy g_ai_detection[] for cross-check.
 */
static void ai_cmd_detect(void)
{
    char buf[AI_CMD_BUF_SIZE];
    uint32_t det_count = g_fall_detection_count;

    print_to_console("=== AI Detection Results (F-003-8) ===\r\n");

    if (det_count == 0)
    {
        print_to_console("  (no detections)\r\n");
    }
    else
    {
        for (uint32_t i = 0; i < det_count && i < AI_MAX_DETECTION_NUM; i++)
        {
            const fall_detection_result_t *r = &g_fall_detection_results[i];
            /* Print score as "X.YY" (integer part + 2 decimal places) without %f */
            int score_pct = (int)(r->score * 100.0f + 0.5f);
            if (score_pct > 100) score_pct = 100;
            snprintf(buf, sizeof(buf),
                     "  [%lu] x=%u y=%u w=%u h=%u score=%d.%02d class=%u\r\n",
                     (unsigned long)i,
                     (unsigned int)r->x,
                     (unsigned int)r->y,
                     (unsigned int)r->width,
                     (unsigned int)r->height,
                     score_pct / 100,
                     score_pct % 100,
                     (unsigned int)r->class_id);
            print_to_console(buf);
        }
    }

    snprintf(buf, sizeof(buf), "Total detections: %lu / %d max\r\n",
             (unsigned long)det_count, AI_MAX_DETECTION_NUM);
    print_to_console(buf);

    /* Post-processing statistics */
    print_to_console("--- Post-processing stats ---\r\n");
    snprintf(buf, sizeof(buf), "  Call count      : %lu\r\n",
             (unsigned long)g_postproc_stats.call_count);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Last candidates : %lu (before NMS)\r\n",
             (unsigned long)g_postproc_stats.last_candidates);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Last detections : %lu (after NMS)\r\n",
             (unsigned long)g_postproc_stats.last_detections);
    print_to_console(buf);

    /* Show legacy g_ai_detection[] for cross-check */
    print_to_console("--- Legacy g_ai_detection[] ---\r\n");
    int legacy_count = 0;
    for (int i = 0; i < AI_MAX_DETECTION_NUM; i++)
    {
        if (g_ai_detection[i].m_w != 0 || g_ai_detection[i].m_h != 0)
        {
            snprintf(buf, sizeof(buf), "  [%d] x=%d y=%d w=%d h=%d\r\n",
                     i,
                     (int)g_ai_detection[i].m_x,
                     (int)g_ai_detection[i].m_y,
                     (int)g_ai_detection[i].m_w,
                     (int)g_ai_detection[i].m_h);
            print_to_console(buf);
            legacy_count++;
        }
    }
    if (legacy_count == 0)
    {
        print_to_console("  (no detections)\r\n");
    }
}

/**
 * Display or set NMS parameters (F-003-8)
 *
 * Usage:
 *   ai nms              - Show current NMS parameters
 *   ai nms conf <value> - Set confidence threshold (0-100 as integer percent)
 *   ai nms iou <value>  - Set IoU threshold (0-100 as integer percent)
 */
static void ai_cmd_nms(int argc, char **argv)
{
    char buf[AI_CMD_BUF_SIZE];
    const fall_detection_postproc_config_t *cfg = fall_detection_postprocess_get_config();

    if (argc < 3)
    {
        /* Display current parameters */
        int conf_pct = (int)(cfg->confidence_threshold * 100.0f);
        int iou_pct  = (int)(cfg->nms_iou_threshold * 100.0f);

        print_to_console("=== NMS Parameters (F-003-8) ===\r\n");

        snprintf(buf, sizeof(buf), "Confidence threshold : 0.%02d\r\n", conf_pct);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "NMS IoU threshold    : 0.%02d\r\n", iou_pct);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "Num classes          : %d\r\n", cfg->num_classes);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "Model input          : %d x %d\r\n",
                 cfg->model_input_width, cfg->model_input_height);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "Camera output        : %d x %d\r\n",
                 cfg->camera_width, cfg->camera_height);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "Max detections       : %d\r\n", cfg->max_detections);
        print_to_console(buf);

        /* Per-branch quantization and anchor info */
        for (int b = 0; b < POSTPROC_NUM_BRANCHES; b++)
        {
            const fall_detection_branch_config_t *br = &cfg->branches[b];
            snprintf(buf, sizeof(buf), "\r\n--- Branch %d (%dx%d, stride %d) ---\r\n",
                     b, br->grid_w, br->grid_h, br->stride);
            print_to_console(buf);

            /* Print scale as integer + fractional parts to avoid %f */
            int scale_int = (int)br->scale;
            int scale_frac = (int)((br->scale - (float)scale_int) * 100000000.0f);
            if (scale_frac < 0) scale_frac = -scale_frac;
            snprintf(buf, sizeof(buf), "  Scale              : %d.%08d\r\n",
                     scale_int, scale_frac);
            print_to_console(buf);

            snprintf(buf, sizeof(buf), "  Zero point         : %d\r\n", br->zero_point);
            print_to_console(buf);

            for (int a = 0; a < AI_OUTPUT_NUM_ANCHORS; a++)
            {
                int aw = (int)br->anchors[a][0];
                int ah = (int)br->anchors[a][1];
                snprintf(buf, sizeof(buf), "  Anchor[%d]          : (%d, %d)\r\n",
                         a, aw, ah);
                print_to_console(buf);
            }
        }

        print_to_console("\r\nTo modify:\r\n");
        print_to_console("  ai nms conf <0-100>  Set confidence threshold (%%)\r\n");
        print_to_console("  ai nms iou  <0-100>  Set IoU threshold (%%)\r\n");
        return;
    }

    if (argc >= 4 && ntlibc_strcmp(argv[2], "conf") == 0)
    {
        int val = 0;
        for (const char *p = argv[3]; *p; p++)
        {
            if (*p >= '0' && *p <= '9')
                val = val * 10 + (*p - '0');
        }
        if (val >= 0 && val <= 100)
        {
            float new_conf = (float)val / 100.0f;
            fall_detection_postprocess_set_confidence(new_conf);
            snprintf(buf, sizeof(buf), "Confidence threshold set to 0.%02d\r\n", val);
            print_to_console(buf);
        }
        else
        {
            print_to_console("Error: value must be 0-100\r\n");
        }
        return;
    }

    if (argc >= 4 && ntlibc_strcmp(argv[2], "iou") == 0)
    {
        int val = 0;
        for (const char *p = argv[3]; *p; p++)
        {
            if (*p >= '0' && *p <= '9')
                val = val * 10 + (*p - '0');
        }
        if (val >= 0 && val <= 100)
        {
            float new_iou = (float)val / 100.0f;
            fall_detection_postprocess_set_nms_iou(new_iou);
            snprintf(buf, sizeof(buf), "NMS IoU threshold set to 0.%02d\r\n", val);
            print_to_console(buf);
        }
        else
        {
            print_to_console("Error: value must be 0-100\r\n");
        }
        return;
    }

    print_to_console("Unknown nms subcommand. Use: ai nms [conf|iou] <value>\r\n");
}

/**
 * Dump raw output tensor bytes for layout diagnosis (F-003-8 debug)
 *
 * Usage:
 *   ai tensor             - Dump first entries of each branch (YOLOv3 2-branch layout)
 *   ai tensor branch <n>  - Dump details of branch n (0 or 1)
 *   ai tensor raw <off>   - Dump 40 raw INT8 bytes from offset in branch 0
 *
 * Each branch tensor layout: [grid_h, grid_w, num_anchors, values_per_anchor]
 * Shows grid_y, grid_x, anchor_id, and the 6 values (x,y,w,h,obj,cls).
 */
static void ai_cmd_tensor(int argc, char **argv)
{
    char buf[AI_CMD_BUF_SIZE];
    const int8_t *branch0 = NULL;
    const int8_t *branch1 = NULL;

    fall_detection_postprocess_get_output_tensors(&branch0, &branch1);

    if (branch0 == NULL && branch1 == NULL)
    {
        print_to_console("No output tensors available (inference not yet run)\r\n");
        return;
    }

    const fall_detection_postproc_config_t *cfg = fall_detection_postprocess_get_config();

    if (argc >= 3 && ntlibc_strcmp(argv[2], "raw") == 0)
    {
        /* Raw byte dump from branch 0 at specified offset */
        int offset = 0;
        if (argc >= 4)
        {
            for (const char *p = argv[3]; *p >= '0' && *p <= '9'; p++)
                offset = offset * 10 + (*p - '0');
        }
        const int8_t *tensor = branch0;
        int total_size = AI_OUTPUT_BRANCH0_SIZE;
        if (tensor == NULL)
        {
            tensor = branch1;
            total_size = AI_OUTPUT_BRANCH1_SIZE;
            print_to_console("(Using branch 1 - branch 0 is NULL)\r\n");
        }
        if (tensor == NULL)
        {
            print_to_console("No tensor data available\r\n");
            return;
        }
        if (offset >= total_size) offset = total_size - 40;
        if (offset < 0) offset = 0;
        int end = offset + 40;
        if (end > total_size) end = total_size;

        snprintf(buf, sizeof(buf), "=== Raw tensor[%d..%d] ===\r\n", offset, end - 1);
        print_to_console(buf);

        for (int i = offset; i < end; i += 10)
        {
            int line_end = i + 10;
            if (line_end > end) line_end = end;
            snprintf(buf, sizeof(buf), "  [%4d]:", i);
            print_to_console(buf);
            for (int k = i; k < line_end; k++)
            {
                snprintf(buf, sizeof(buf), " %4d", (int)tensor[k]);
                print_to_console(buf);
            }
            print_to_console("\r\n");
        }
        return;
    }

    if (argc >= 3 && ntlibc_strcmp(argv[2], "branch") == 0)
    {
        /* Detailed dump of a specific branch */
        int branch_id = 0;
        if (argc >= 4 && argv[3][0] == '1') branch_id = 1;

        const int8_t *tensor = (branch_id == 0) ? branch0 : branch1;
        if (tensor == NULL)
        {
            snprintf(buf, sizeof(buf), "Branch %d tensor is NULL\r\n", branch_id);
            print_to_console(buf);
            return;
        }

        const fall_detection_branch_config_t *br = &cfg->branches[branch_id];
        int grid_w = br->grid_w;
        int grid_h = br->grid_h;
        int na = AI_OUTPUT_NUM_ANCHORS;
        int vpa = AI_OUTPUT_VALUES_PER_ANCHOR;

        snprintf(buf, sizeof(buf), "=== Branch %d (%dx%d, stride %d) ===\r\n",
                 branch_id, grid_w, grid_h, br->stride);
        print_to_console(buf);
        print_to_console("  cy cx  a : [  tx   ty   tw   th  obj  cls] (INT8)\r\n");

        /* Show first 15 entries */
        int shown = 0;
        for (int cy = 0; cy < grid_h && shown < 15; cy++)
        {
            for (int cx = 0; cx < grid_w && shown < 15; cx++)
            {
                for (int a = 0; a < na && shown < 15; a++)
                {
                    int off = ((cy * grid_w + cx) * na + a) * vpa;
                    snprintf(buf, sizeof(buf),
                             "  %2d %2d %d : [%4d %4d %4d %4d %4d %4d]\r\n",
                             cy, cx, a,
                             (int)tensor[off + 0], (int)tensor[off + 1],
                             (int)tensor[off + 2], (int)tensor[off + 3],
                             (int)tensor[off + 4], (int)tensor[off + 5]);
                    print_to_console(buf);
                    shown++;
                }
            }
        }
        return;
    }

    /* Default: summary of both branches */
    print_to_console("=== Output Tensors (YOLOv3 2-branch) ===\r\n");
    print_to_console("  cy cx  a : [  tx   ty   tw   th  obj  cls] (INT8)\r\n");

    for (int b = 0; b < POSTPROC_NUM_BRANCHES; b++)
    {
        const int8_t *tensor = (b == 0) ? branch0 : branch1;
        const fall_detection_branch_config_t *br = &cfg->branches[b];

        snprintf(buf, sizeof(buf), "\r\n--- Branch %d (%dx%d, stride %d) ---\r\n",
                 b, br->grid_w, br->grid_h, br->stride);
        print_to_console(buf);

        if (tensor == NULL)
        {
            print_to_console("  (NULL - not available)\r\n");
            continue;
        }

        int na = AI_OUTPUT_NUM_ANCHORS;
        int vpa = AI_OUTPUT_VALUES_PER_ANCHOR;
        int total_anchors = br->grid_w * br->grid_h * na;

        /* Show first 5 entries */
        int shown = 0;
        for (int cy = 0; cy < br->grid_h && shown < 5; cy++)
        {
            for (int cx = 0; cx < br->grid_w && shown < 5; cx++)
            {
                for (int a = 0; a < na && shown < 5; a++)
                {
                    int off = ((cy * br->grid_w + cx) * na + a) * vpa;
                    snprintf(buf, sizeof(buf),
                             "  %2d %2d %d : [%4d %4d %4d %4d %4d %4d]\r\n",
                             cy, cx, a,
                             (int)tensor[off + 0], (int)tensor[off + 1],
                             (int)tensor[off + 2], (int)tensor[off + 3],
                             (int)tensor[off + 4], (int)tensor[off + 5]);
                    print_to_console(buf);
                    shown++;
                }
            }
        }

        /* Stats: scan for high objectness scores */
        int high_obj = 0;
        int8_t min_obj = 127;
        int8_t max_obj = -128;
        for (int i = 0; i < total_anchors; i++)
        {
            int off = i * vpa + 4;  /* objectness is at index 4 */
            int8_t v = tensor[off];
            if (v > max_obj) max_obj = v;
            if (v < min_obj) min_obj = v;
            float dq = ((float)v - (float)br->zero_point) * br->scale;
            float sig = 1.0f / (1.0f + expf(-dq));
            if (sig > 0.5f) high_obj++;
        }
        snprintf(buf, sizeof(buf),
                 "  Objectness: INT8 range [%d, %d], sigmoid>0.5: %d/%d\r\n",
                 (int)min_obj, (int)max_obj, high_obj, total_anchors);
        print_to_console(buf);
    }
}

/**
 * Diagnose MERA pipeline: dump intermediate buffers at each stage
 *
 * With YOLO_FASTEST_MODEL=1: single subgraph (sub_0000 only)
 * With YOLO_FASTEST_MODEL=0: legacy YOLOv8 with sub_0000 + sub_0002
 */
static void ai_cmd_diag(void)
{
    char buf[AI_CMD_BUF_SIZE];

    print_to_console("=== MERA Pipeline Diagnosis ===\r\n");

#if YOLO_FASTEST_MODEL
    print_to_console("Model: YOLO-Fastest V1 (single NPU subgraph)\r\n");
#else
    print_to_console("Model: YOLOv8 legacy (YOLO_FASTEST_MODEL=0)\r\n");
#endif

    /* NPU OFS2 configuration (security/privilege derived at init) */
    {
        uint32_t ofs2 = (uint32_t)BSP_CFG_OPTION_SETTING_OFS2;
        uint32_t npusa = (ofs2 >> 2) & 1;  /* bit 2: 1=secure, 0=non-secure */
        uint32_t npupa = (ofs2 >> 3) & 1;  /* bit 3: 1=unprivileged, 0=privileged */
        snprintf(buf, sizeof(buf), "OFS2=0x%08lX NPUSA=%lu(%s) NPUPA=%lu(%s)\r\n",
                 (unsigned long)ofs2,
                 (unsigned long)npusa, npusa ? "Secure" : "NonSecure",
                 (unsigned long)npupa, npupa ? "Unpriv" : "Priv");
        print_to_console(buf);
    }

    /* sub_0000 arena info */
    snprintf(buf, sizeof(buf), "sub_0000_arena addr   : 0x%08lX (size %lu, SRAM)\r\n",
             (unsigned long)(uintptr_t)sub_0000_net1_arena,
             (unsigned long)kArenaSize_sub_0000_net1);
    print_to_console(buf);

#if !YOLO_FASTEST_MODEL
    /* Legacy YOLOv8: also has sub_0002 */
    snprintf(buf, sizeof(buf), "sub_0002_arena addr   : 0x%08lX (size %lu, SRAM)\r\n",
             (unsigned long)(uintptr_t)sub_0002_net1_arena,
             (unsigned long)kArenaSize_sub_0002_net1);
    print_to_console(buf);

    {
        const int8_t *out_ptr = NULL;
        const int8_t *dummy = NULL;
        fall_detection_postprocess_get_output_tensors(&dummy, &dummy);
        /* For legacy, show the mera_output_ptr */
        out_ptr = mera_output_ptr();
        snprintf(buf, sizeof(buf), "output ptr (legacy)   : 0x%08lX\r\n",
                 (unsigned long)(uintptr_t)out_ptr);
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "expected output addr  : 0x%08lX (arena+0x%lX)\r\n",
                 (unsigned long)(uintptr_t)(sub_0002_net1_arena + sub_0002_net1_address_PartitionedCall_0_70452),
                 (unsigned long)sub_0002_net1_address_PartitionedCall_0_70452);
        print_to_console(buf);
    }

    /* NPU invoke return codes (legacy) */
    {
        extern volatile int g_sub0000_last_rc;
        extern volatile int g_sub0002_last_rc;
        snprintf(buf, sizeof(buf), "sub_0000 invoke rc    : %d (%s)\r\n",
                 (int)g_sub0000_last_rc,
                 g_sub0000_last_rc == 0 ? "OK" : g_sub0000_last_rc == -1 ? "FAIL" : "not yet run");
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "sub_0002 invoke rc    : %d (%s)\r\n",
                 (int)g_sub0002_last_rc,
                 g_sub0002_last_rc == 0 ? "OK" : g_sub0002_last_rc == -1 ? "FAIL" : "not yet run");
        print_to_console(buf);
    }

    /* Invalidate D-cache before reading */
    SCB_InvalidateDCache_by_Addr(
        (uint32_t *)sub_0000_net1_arena, (int32_t)kArenaSize_sub_0000_net1);

    /* Check input area */
    print_to_console("\r\n--- sub_0000 NPU input ---\r\n");
    {
        const int8_t *p = (const int8_t *)(sub_0000_net1_arena + sub_0000_net1_address_serving_default_image_input_0);
        int nonzero = 0;
        for (int i = 0; i < 1000; i++) { if (p[i] != 0) nonzero++; }
        snprintf(buf, sizeof(buf), "Input    [0x%lX] (%d/1000 nonzero): ",
                 (unsigned long)sub_0000_net1_address_serving_default_image_input_0, nonzero);
        print_to_console(buf);
        for (int i = 0; i < 10; i++) {
            snprintf(buf, sizeof(buf), "%4d ", (int)p[i]);
            print_to_console(buf);
        }
        print_to_console("\r\n");
    }

    /* sub_0000 outputs (NPU stage 1) */
    print_to_console("\r\n--- sub_0000 NPU outputs ---\r\n");

    /* Sigmoid1 at offset */
    {
        const int8_t *p = (const int8_t *)(sub_0000_net1_arena + sub_0000_net1_address_model_78_tf_math_sigmoid_56_Sigmoid1_70442);
        int nonzero = 0;
        for (int i = 0; i < 756; i++) { if (p[i] != 0) nonzero++; }
        snprintf(buf, sizeof(buf), "Sigmoid1 [0x%lX] (%d/756 nonzero): ",
                 (unsigned long)sub_0000_net1_address_model_78_tf_math_sigmoid_56_Sigmoid1_70442, nonzero);
        print_to_console(buf);
        for (int i = 0; i < 10; i++) {
            snprintf(buf, sizeof(buf), "%4d ", (int)p[i]);
            print_to_console(buf);
        }
        print_to_console("\r\n");
    }

    /* Reshape at offset */
    {
        const int8_t *p = (const int8_t *)(sub_0000_net1_arena + sub_0000_net1_address_model_78_tf_reshape_23_Reshape_70431);
        int nonzero = 0;
        for (int i = 0; i < 3024; i++) { if (p[i] != 0) nonzero++; }
        snprintf(buf, sizeof(buf), "Reshape  [0x%lX] (%d/3024 nonzero): ",
                 (unsigned long)sub_0000_net1_address_model_78_tf_reshape_23_Reshape_70431, nonzero);
        print_to_console(buf);
        for (int i = 0; i < 10; i++) {
            snprintf(buf, sizeof(buf), "%4d ", (int)p[i]);
            print_to_console(buf);
        }
        print_to_console("\r\n");
    }

    /* Scan sub_0000 arena for any non-zero data */
    {
        int nonzero_total = 0;
        int first_nonzero = -1;
        for (int i = 0; i < (int)kArenaSize_sub_0000_net1; i++) {
            if (sub_0000_net1_arena[i] != 0) {
                nonzero_total++;
                if (first_nonzero < 0) first_nonzero = i;
            }
        }
        snprintf(buf, sizeof(buf), "\r\nArena scan: %d/%lu nonzero, first at offset 0x%X\r\n",
                 nonzero_total, (unsigned long)kArenaSize_sub_0000_net1,
                 (first_nonzero >= 0) ? first_nonzero : 0);
        print_to_console(buf);
        if (first_nonzero >= 0) {
            const uint8_t *p = sub_0000_net1_arena + first_nonzero;
            snprintf(buf, sizeof(buf), "  [0x%X]: ", first_nonzero);
            print_to_console(buf);
            int show = 10;
            if (first_nonzero + show > (int)kArenaSize_sub_0000_net1)
                show = (int)kArenaSize_sub_0000_net1 - first_nonzero;
            for (int i = 0; i < show; i++) {
                snprintf(buf, sizeof(buf), "%02X ", (unsigned int)p[i]);
                print_to_console(buf);
            }
            print_to_console("\r\n");
        }
    }

    /* sub_0002 arena contents */
    print_to_console("\r\n--- sub_0002 arena contents ---\r\n");

    /* Sigmoid1 copy */
    {
        const int8_t *p = (const int8_t *)(sub_0002_net1_arena + sub_0002_net1_address_model_78_tf_math_sigmoid_56_Sigmoid1_70442);
        int nonzero = 0;
        for (int i = 0; i < 756; i++) { if (p[i] != 0) nonzero++; }
        snprintf(buf, sizeof(buf), "Sigmoid1 [0x%lX] (%d/756 nonzero): ",
                 (unsigned long)sub_0002_net1_address_model_78_tf_math_sigmoid_56_Sigmoid1_70442, nonzero);
        print_to_console(buf);
        for (int i = 0; i < 10; i++) {
            snprintf(buf, sizeof(buf), "%4d ", (int)p[i]);
            print_to_console(buf);
        }
        print_to_console("\r\n");
    }

    /* StridedSlice */
    {
        const int8_t *p = (const int8_t *)(sub_0002_net1_arena + sub_0002_net1_address_model_78_tf_strided_slice_StridedSlice_70445);
        int nonzero = 0;
        for (int i = 0; i < 1512; i++) { if (p[i] != 0) nonzero++; }
        snprintf(buf, sizeof(buf), "Slice    [0x%lX] (%d/1512 nonzero): ",
                 (unsigned long)sub_0002_net1_address_model_78_tf_strided_slice_StridedSlice_70445, nonzero);
        print_to_console(buf);
        for (int i = 0; i < 10; i++) {
            snprintf(buf, sizeof(buf), "%4d ", (int)p[i]);
            print_to_console(buf);
        }
        print_to_console("\r\n");
    }

    /* StridedSlice_1 */
    {
        const int8_t *p = (const int8_t *)(sub_0002_net1_arena + sub_0002_net1_address_model_78_tf_strided_slice_1_StridedSlice_70443);
        int nonzero = 0;
        for (int i = 0; i < 1512; i++) { if (p[i] != 0) nonzero++; }
        snprintf(buf, sizeof(buf), "Slice_1  [0x%lX] (%d/1512 nonzero): ",
                 (unsigned long)sub_0002_net1_address_model_78_tf_strided_slice_1_StridedSlice_70443, nonzero);
        print_to_console(buf);
        for (int i = 0; i < 10; i++) {
            snprintf(buf, sizeof(buf), "%4d ", (int)p[i]);
            print_to_console(buf);
        }
        print_to_console("\r\n");
    }

    /* Output */
    {
        const int8_t *p = (const int8_t *)(sub_0002_net1_arena + sub_0002_net1_address_PartitionedCall_0_70452);
        int nonzero = 0;
        for (int i = 0; i < 3780; i++) { if (p[i] != 0) nonzero++; }
        snprintf(buf, sizeof(buf), "Output   [0x%lX] (%d/3780 nonzero): ",
                 (unsigned long)sub_0002_net1_address_PartitionedCall_0_70452, nonzero);
        print_to_console(buf);
        for (int i = 0; i < 10; i++) {
            snprintf(buf, sizeof(buf), "%4d ", (int)p[i]);
            print_to_console(buf);
        }
        print_to_console("\r\n");
    }
#else
    /* YOLO-Fastest: single subgraph, show output tensor info */
    {
        const int8_t *branch0 = NULL;
        const int8_t *branch1 = NULL;
        fall_detection_postprocess_get_output_tensors(&branch0, &branch1);

        snprintf(buf, sizeof(buf), "Branch 0 ptr          : 0x%08lX (%s)\r\n",
                 (unsigned long)(uintptr_t)branch0,
                 branch0 ? "valid" : "NULL");
        print_to_console(buf);

        snprintf(buf, sizeof(buf), "Branch 1 ptr          : 0x%08lX (%s)\r\n",
                 (unsigned long)(uintptr_t)branch1,
                 branch1 ? "valid" : "NULL");
        print_to_console(buf);
    }

    /* Invalidate D-cache before reading */
    SCB_InvalidateDCache_by_Addr(
        (uint32_t *)sub_0000_net1_arena, (int32_t)kArenaSize_sub_0000_net1);

    /* Check input area */
    print_to_console("\r\n--- sub_0000 NPU input ---\r\n");
    {
        const int8_t *p = (const int8_t *)(sub_0000_net1_arena + sub_0000_net1_address_serving_default_image_input_0);
        int nonzero = 0;
        for (int i = 0; i < 1000; i++) { if (p[i] != 0) nonzero++; }
        snprintf(buf, sizeof(buf), "Input    [0x%lX] (%d/1000 nonzero): ",
                 (unsigned long)sub_0000_net1_address_serving_default_image_input_0, nonzero);
        print_to_console(buf);
        for (int i = 0; i < 10; i++) {
            snprintf(buf, sizeof(buf), "%4d ", (int)p[i]);
            print_to_console(buf);
        }
        print_to_console("\r\n");
    }

    /* Scan sub_0000 arena for any non-zero data */
    {
        int nonzero_total = 0;
        int first_nonzero = -1;
        for (int i = 0; i < (int)kArenaSize_sub_0000_net1; i++) {
            if (sub_0000_net1_arena[i] != 0) {
                nonzero_total++;
                if (first_nonzero < 0) first_nonzero = i;
            }
        }
        snprintf(buf, sizeof(buf), "\r\nArena scan: %d/%lu nonzero, first at offset 0x%X\r\n",
                 nonzero_total, (unsigned long)kArenaSize_sub_0000_net1,
                 (first_nonzero >= 0) ? first_nonzero : 0);
        print_to_console(buf);
    }
#endif
}

/**
 * Display help for the ai command
 */
static void ai_cmd_help(void)
{
    print_to_console("Usage: ai <subcommand>\r\n");
    print_to_console("Subcommands:\r\n");
    print_to_console("  model   - Display model information (input/output sizes)\r\n");
    print_to_console("  config  - Display AI application configuration\r\n");
    print_to_console("  preproc - Preprocessing status/timing (F-003-6)\r\n");
    print_to_console("  status  - Inference thread state (F-003-7)\r\n");
    print_to_console("  time    - Timing breakdown (F-003-7)\r\n");
    print_to_console("  detect  - Latest detection results (F-003-8)\r\n");
    print_to_console("  nms     - NMS parameters display/config (F-003-8)\r\n");
    print_to_console("  tensor  - Raw output tensor dump (debug)\r\n");
    print_to_console("  diag    - MERA pipeline diagnosis (debug)\r\n");
    print_to_console("  help    - Show this help\r\n");
}
