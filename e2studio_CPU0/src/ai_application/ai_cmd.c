/**
 * @file ai_cmd.c
 * @brief NT-Shell "ai" command implementation (F-003-5)
 * @details
 * Implements the "ai" command for NT-Shell, providing subcommands
 * to inspect AI model configuration and runtime status.
 *
 * Currently supports (F-003-5, F-003-6):
 *   ai model   - Display model information (input/output sizes, arena sizes)
 *   ai config  - Display AI application configuration constants
 *   ai preproc - Preprocessing status/timing (F-003-6)
 *
 * Future subcommands (F-003-7 ~ F-003-8):
 *   ai status  - Inference thread state
 *   ai infer   - Manual inference trigger
 *   ai time    - Timing breakdown
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
 * Display help for the ai command
 */
static void ai_cmd_help(void)
{
    print_to_console("Usage: ai <subcommand>\r\n");
    print_to_console("Subcommands:\r\n");
    print_to_console("  model   - Display model information (input/output sizes)\r\n");
    print_to_console("  config  - Display AI application configuration\r\n");
    print_to_console("  preproc - Preprocessing status/timing (F-003-6)\r\n");
    print_to_console("  help    - Show this help\r\n");
    print_to_console("\r\n");
    print_to_console("Future subcommands (F-003-7 ~ F-003-8):\r\n");
    print_to_console("  status  - Inference thread state\r\n");
    print_to_console("  infer   - Manual inference trigger\r\n");
    print_to_console("  time    - Timing breakdown\r\n");
    print_to_console("  detect  - Latest detection results\r\n");
    print_to_console("  nms     - NMS parameter display\r\n");
}
