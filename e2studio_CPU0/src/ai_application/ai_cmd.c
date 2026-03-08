/**
 * @file ai_cmd.c
 * @brief NT-Shell "ai" command implementation (F-003-5, F-003-6, F-003-7)
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
 *
 * Future subcommands (F-003-8):
 *   ai detect  - Latest detection results
 *   ai nms     - NMS parameter display
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include "ntlibc.h"
#include "jlink_console.h"
#include "ai_application_config.h"
#include "ai_cmd.h"
#include "camera_layer/camera_utils.h"
#include "camera_framebuffer.h"
#include "ai_inference_thread_api.h"
#include "common_util.h"

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
    print_to_console("Demo type      : FALL_DETECTION (YOLOv8 pico INT8)\r\n");
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
    snprintf(buf, sizeof(buf), "Output tensor  : [%d, %d] = %d bytes\r\n",
             AI_OUTPUT_TENSOR_ROWS, AI_OUTPUT_NUM_CANDIDATES, AI_OUTPUT_TENSOR_SIZE);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "Num classes    : %d (person)\r\n", AI_OUTPUT_NUM_CLASSES);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "Candidates     : %d (P3:%d + P4:%d + P5:%d)\r\n",
             AI_OUTPUT_NUM_CANDIDATES, 24*24, 12*12, 6*6);
    print_to_console(buf);
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
 * Display latest detection results (F-003-8, stub for now)
 *
 * Shows the contents of g_ai_detection[] array.
 */
static void ai_cmd_detect(void)
{
    char buf[AI_CMD_BUF_SIZE];
    int count = 0;

    print_to_console("=== AI Detection Results ===\r\n");

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
            count++;
        }
    }

    if (count == 0)
    {
        print_to_console("  (no detections)\r\n");
    }

    snprintf(buf, sizeof(buf), "Total detections: %d / %d max\r\n",
             count, AI_MAX_DETECTION_NUM);
    print_to_console(buf);

    print_to_console("Note: Post-processing (F-003-8) not yet implemented.\r\n");
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
    print_to_console("  detect  - Latest detection results\r\n");
    print_to_console("  help    - Show this help\r\n");
    print_to_console("\r\n");
    print_to_console("Future subcommands (F-003-8):\r\n");
    print_to_console("  nms     - NMS parameter display\r\n");
}
