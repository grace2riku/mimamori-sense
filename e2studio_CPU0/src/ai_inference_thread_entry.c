/**
 * @file ai_inference_thread_entry.c
 * @brief Fall detection AI inference FreeRTOS thread (F-003-7)
 * @details
 * Implements the AI inference pipeline on the Ethos-U55 NPU:
 *   1. NPU initialization via RM_ETHOSU_Open()
 *   2. Event-driven inference loop:
 *      - Wait for AI_INFERENCE_INPUT_IMAGE_READY from camera preprocessing
 *      - Copy preprocessed image to MERA input arena
 *      - Execute NPU inference via mera_invoke()
 *      - Measure inference time via DWT cycle counter
 *      - Signal AI_INFERENCE_RESULT_UPDATED to display thread
 *
 * Event flow:
 *   camera_display_thread -> SetBits(IMAGE_READY)
 *     -> ai_inference_thread -> memcpy -> mera_invoke() -> post-process
 *       -> SetBits(RESULT_UPDATED) -> camera_display_thread
 *
 * Reference: reference_projects/ruhmi-framework-mcu/application_examples/
 *            face_detection/src/ai_inference_thread_entry.c
 *
 * Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include "ai_inference_thread.h"

#include <stdio.h>
#include <string.h>

#include "common_util.h"
#include "common_data.h"

#include "ai_application/application_config.h"
#include "ai_application/ai_application_config.h"

/**
 * MERA_INFERENCE_ENABLED controls whether MERA model functions are linked.
 *
 * Requires CPU0 FLASH partition >= 1MB (see Issue #115).
 */
#define MERA_INFERENCE_ENABLED  (1)

#if MERA_INFERENCE_ENABLED
#include "ai_application/fall_detection/wrapper.h"
#endif

#include "jlink_console.h"
#include "camera_framebuffer.h"
#include "camera_thread_api.h"
#include "camera_layer/camera_utils.h"
#include "ai_application/fall_detection/fall_detection_postprocess.h"

#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * AI thread yield delay (ticks) after each inference cycle.
 *
 * This prevents the AI thread from starving lower-priority threads.
 * The value should not be too low; otherwise, the display thread
 * performance is negatively impacted.
 *
 * Reference: face_detection/src/ai_inference_thread_entry.c line 28
 */
#define AI_THREAD_YIELD                     (25)

/**
 * AI model input buffer size in bytes.
 * 192 x 192 x 3 (RGB INT8) = 110592 bytes
 *
 * Reference: face_detection/src/ai_inference_thread_entry.c line 39
 */
#define AI_INPUT_BUFFER_SIZE    (AI_INPUT_IMAGE_WIDTH * AI_INPUT_IMAGE_HEIGHT * AI_INPUT_IMAGE_BYTE_PER_PIXEL)

/**
 * DWT cycle counter clock frequency.
 * Cortex-M85 runs at 1 GHz (= SystemCoreClock).
 */
#define DWT_CYCLES_PER_MS       (SystemCoreClock / 1000U)

/**
 * Console output buffer size for logging
 */
#define AI_PRINT_BUF_SIZE       (128)

/**********************************************************************************************************************
 Imported global variables and functions (from other files)
 *********************************************************************************************************************/

/**********************************************************************************************************************
 Exported global variables and functions (to be accessed by other files)
 *********************************************************************************************************************/

/**
 * AI application event group for inter-thread synchronization.
 *
 * This event group is created in ai_inference_thread_entry() because the
 * FSP-generated code does not include an event group for AI synchronization.
 * The reference project (face_detection) uses g_ai_app_event which was
 * FSP-generated in that project's configuration.
 *
 * Used by:
 *   - ai_inference_thread: waits on IMAGE_READY, sets ETHOSU_INIT_DONE,
 *     AI_INFERENCE_INIT_DONE, RESULT_UPDATED
 *   - camera_display_thread: sets IMAGE_READY, waits on RESULT_UPDATED
 *   - Other threads: wait on ETHOSU_INIT_DONE / AI_INFERENCE_INIT_DONE
 *
 * Reference: face_detection common_data.h (FSP-generated EventGroupHandle_t)
 */
EventGroupHandle_t g_ai_app_event;
static StaticEventGroup_t s_ai_app_event_memory;

/**
 * AI model input buffer (preprocessed image, INT8)
 *
 * Allocated in OnChip RAM for fast NPU DMA access.
 * The camera preprocessing function writes here, then the inference
 * thread copies to the MERA input arena.
 *
 * Reference: face_detection/src/ai_inference_thread_entry.c lines 38-44
 */
#if (AI_INPUT_IMAGE_ALLOCATION == ALLOCATE_TO_ONCHIP_RAM)
int8_t model_buffer_int8[AI_INPUT_BUFFER_SIZE] BSP_ALIGN_VARIABLE(8);
#elif (AI_INPUT_IMAGE_ALLOCATION == ALLOCATE_TO_SDRAM)
int8_t model_buffer_int8[AI_INPUT_BUFFER_SIZE] BSP_PLACE_IN_SECTION(".sdram") BSP_ALIGN_VARIABLE(8);
#else
#error "Unsupported AI_INPUT_IMAGE_ALLOCATION value"
#endif
uint32_t model_buffer_int8_size = sizeof(model_buffer_int8);

/**********************************************************************************************************************
 Private global variables and functions
 *********************************************************************************************************************/

/** AI inference thread state for diagnostics (ai status command) */
typedef enum e_ai_inference_state
{
    AI_STATE_NOT_STARTED = 0,       /**< Thread not yet entered */
    AI_STATE_INITIALIZING,          /**< NPU / model initialization in progress */
    AI_STATE_IDLE,                  /**< Waiting for input image */
    AI_STATE_PREPROCESSING,         /**< Copying input to MERA arena */
    AI_STATE_INFERRING,             /**< NPU inference in progress */
    AI_STATE_POSTPROCESSING,        /**< Post-processing results */
    AI_STATE_ERROR,                 /**< Initialization or inference error */
} ai_inference_state_t;

static volatile ai_inference_state_t s_ai_state = AI_STATE_NOT_STARTED;

/** Inference cycle count (total inferences since boot) */
static volatile uint32_t s_inference_count = 0;

/** Timing measurements (DWT cycle count based) */
static volatile uint32_t s_time_memcpy_ms   = 0;   /**< Input copy time (ms) */
static volatile uint32_t s_time_invoke_ms   = 0;   /**< NPU inference time (ms) */
static volatile uint32_t s_time_total_ms    = 0;   /**< Total cycle time (ms) */

/** DWT cycle counter helpers */
static inline void dwt_counter_enable(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline uint32_t dwt_get_cycles(void)
{
    return DWT->CYCCNT;
}

static inline uint32_t dwt_cycles_to_ms(uint32_t start, uint32_t end)
{
    uint32_t diff = end - start;    /* Handles wrap-around via unsigned arithmetic */
    return diff / DWT_CYCLES_PER_MS;
}

/** Forward declaration of update_detection_result (called from post-processing) */
void update_detection_result(uint16_t index, signed short x, signed short y,
                              signed short w, signed short h);

/**
 * Log a message to the console from AI inference thread context
 */
static void ai_thread_log(const char *msg)
{
    print_to_console((char_t *)msg);
}

/**********************************************************************************************************************
 Exported global functions (for ai_cmd.c diagnostics)
 *********************************************************************************************************************/

/**
 * Get the current AI inference thread state
 */
uint32_t ai_inference_get_state(void)
{
    return (uint32_t)s_ai_state;
}

/**
 * Get the state name string for display
 */
const char *ai_inference_get_state_name(void)
{
    switch (s_ai_state)
    {
        case AI_STATE_NOT_STARTED:    return "NOT_STARTED";
        case AI_STATE_INITIALIZING:   return "INITIALIZING";
        case AI_STATE_IDLE:           return "IDLE";
        case AI_STATE_PREPROCESSING:  return "PREPROCESSING";
        case AI_STATE_INFERRING:      return "INFERRING";
        case AI_STATE_POSTPROCESSING: return "POSTPROCESSING";
        case AI_STATE_ERROR:          return "ERROR";
        default:                      return "UNKNOWN";
    }
}

/**
 * Get the total inference count since boot
 */
uint32_t ai_inference_get_count(void)
{
    return s_inference_count;
}

/**
 * Get the last memcpy (input copy) time in milliseconds
 */
uint32_t ai_inference_get_memcpy_time_ms(void)
{
    return s_time_memcpy_ms;
}

/**
 * Get the last NPU inference time in milliseconds
 */
uint32_t ai_inference_get_invoke_time_ms(void)
{
    return s_time_invoke_ms;
}

/**
 * Get the last total cycle time in milliseconds
 */
uint32_t ai_inference_get_total_time_ms(void)
{
    return s_time_total_ms;
}

/**********************************************************************************************************************
 Thread entry function
 *********************************************************************************************************************/

/**
 * AI inference thread entry function (F-003-7)
 *
 * @details
 * Initializes Ethos-U55 NPU, then runs the inference loop:
 *   1. Wait for AI_INFERENCE_INPUT_IMAGE_READY event
 *   2. Clear previous detection results
 *   3. Copy preprocessed image (model_buffer_int8) to MERA input arena
 *   4. Invoke NPU inference via mera_invoke()
 *   5. Post-process results (F-003-8, stub for now)
 *   6. Signal AI_INFERENCE_RESULT_UPDATED event
 *   7. Yield to display thread
 *
 * Reference: face_detection/src/ai_inference_thread_entry.c lines 95-145
 *
 * @param pvParameters FreeRTOS task parameter (unused)
 */
void ai_inference_thread_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    char buf[AI_PRINT_BUF_SIZE];

    s_ai_state = AI_STATE_INITIALIZING;

    /* ======================================================================
     * Step 0: Create the AI application event group
     *
     * The reference project (face_detection) has g_ai_app_event as an
     * FSP-generated event group. This project does not have it in the FSP
     * configuration, so we create it here in user code.
     * ====================================================================== */
    g_ai_app_event = xEventGroupCreateStatic(&s_ai_app_event_memory);
    if (NULL == g_ai_app_event)
    {
        ai_thread_log("ERROR: Failed to create g_ai_app_event.\r\n");
        s_ai_state = AI_STATE_ERROR;
        while (1) { vTaskDelay(1000); }
    }

    ai_thread_log("AI inference thread started.\r\n");

    /* ======================================================================
     * Step 1: Wait for camera initialization to complete
     *
     * The reference project waits for both display and camera init.
     * In this project, we wait for the camera thread to finish initialization
     * before proceeding with NPU init. The camera thread signals readiness
     * via camera_thread_is_initialized().
     *
     * Reference: face_detection/src/ai_inference_thread_entry.c line 102
     * ====================================================================== */
    ai_thread_log("  Waiting for camera initialization...\r\n");

    /* Poll for camera readiness (camera thread does not use g_ai_app_event) */
    while (!camera_thread_is_initialized())
    {
        if (camera_thread_has_error())
        {
            ai_thread_log("  WARNING: Camera init failed. Continuing NPU init.\r\n");
            break;
        }
        vTaskDelay(100);
    }

    ai_thread_log("  Camera ready. Initializing Ethos-U55 NPU...\r\n");

    /* ======================================================================
     * Step 2: Initialize Ethos-U55 NPU
     *
     * RM_ETHOSU_Open() configures the NPU hardware, sets up the IRQ, and
     * initializes the Ethos-U driver. The FSP instances (g_rm_ethosu0_ctrl,
     * g_rm_ethosu0_cfg) are defined in ra_gen/common_data.c.
     *
     * Reference: face_detection/src/ai_inference_thread_entry.c line 104
     * ====================================================================== */
#if MERA_INFERENCE_ENABLED
    {
        fsp_err_t ethosu_err = RM_ETHOSU_Open(&g_rm_ethosu0_ctrl, &g_rm_ethosu0_cfg);
        if (FSP_SUCCESS != ethosu_err)
        {
            snprintf(buf, sizeof(buf), "  ERROR: RM_ETHOSU_Open() failed (err=%d)\r\n", (int)ethosu_err);
            ai_thread_log(buf);

            /* Read NPU registers for diagnosis (R_NPU_BASE = 0x40140000) */
            volatile uint32_t *npu = (volatile uint32_t *)0x40140000UL;
            snprintf(buf, sizeof(buf), "  NPU ID=0x%08lX CONFIG=0x%08lX\r\n",
                     (unsigned long)npu[0x0000/4],  /* ID register */
                     (unsigned long)npu[0x0010/4]);  /* CONFIG register */
            ai_thread_log(buf);
            snprintf(buf, sizeof(buf), "  NPU STATUS=0x%08lX PROT=0x%08lX\r\n",
                     (unsigned long)npu[0x0004/4],  /* STATUS register */
                     (unsigned long)npu[0x0024/4]);  /* PROT register */
            ai_thread_log(buf);
        }
    }

    xEventGroupSetBits(g_ai_app_event, HARDWARE_ETHOSU_INIT_DONE);

    snprintf(buf, sizeof(buf), "  NPU initialized. Arena sub0=%lu, sub2=%lu bytes\r\n",
             (unsigned long)mera_arena_size_sub0(),
             (unsigned long)mera_arena_size_sub2());
    ai_thread_log(buf);
#else
    /* Stub: Skip NPU init to avoid linking MERA model data (~307KB) */
    xEventGroupSetBits(g_ai_app_event, HARDWARE_ETHOSU_INIT_DONE);
    ai_thread_log("  NPU init SKIPPED (MERA_INFERENCE_ENABLED=0)\r\n");
#endif

    /* ======================================================================
     * Step 3: Enable DWT cycle counter for timing measurements
     * ====================================================================== */
    dwt_counter_enable();

    /* ======================================================================
     * Step 3b: Initialize post-processing module (F-003-8)
     *
     * Sets default confidence threshold, NMS IoU threshold, and
     * coordinate conversion parameters.
     * ====================================================================== */
    fall_detection_postprocess_init();
    ai_thread_log("  Post-processing initialized (F-003-8).\r\n");

    /* ======================================================================
     * Step 4: Signal AI inference initialization complete
     *
     * Reference: face_detection/src/ai_inference_thread_entry.c line 115
     * ====================================================================== */
    xEventGroupSetBits(g_ai_app_event, SOFTWARE_AI_INFERENCE_INIT_DONE);

    ai_thread_log("  AI inference initialization complete. Entering inference loop.\r\n");

    s_ai_state = AI_STATE_IDLE;

    /* ======================================================================
     * Step 5: Inference loop
     *
     * Reference: face_detection/src/ai_inference_thread_entry.c lines 117-144
     * ====================================================================== */
    while (true)
    {
        /* Wait for preprocessed image to be ready */
        xEventGroupWaitBits(g_ai_app_event,
                            AI_INFERENCE_INPUT_IMAGE_READY,
                            pdTRUE,         /* Clear on exit */
                            pdTRUE,         /* Wait for all */
                            portMAX_DELAY);

        uint32_t t_total_start = dwt_get_cycles();

        INFERENCE_START_INDICATE_LED;

        /* Clear previous detection results */
        for (int i = 0; i < AI_MAX_DETECTION_NUM; i++)
        {
            memset(&g_ai_detection[i], 0, sizeof(g_ai_detection[i]));
        }

        /* ---- Copy input data to MERA arena ---- */
        s_ai_state = AI_STATE_PREPROCESSING;

        uint32_t t_memcpy_start = dwt_get_cycles();

#if MERA_INFERENCE_ENABLED
        memcpy(mera_input_ptr(), model_buffer_int8, AI_INPUT_BUFFER_SIZE);
#else
        /* Stub: no MERA arena to copy to */
        (void)model_buffer_int8;
#endif

        uint32_t t_memcpy_end = dwt_get_cycles();
        s_time_memcpy_ms = dwt_cycles_to_ms(t_memcpy_start, t_memcpy_end);

        /* ---- Execute NPU inference ---- */
        s_ai_state = AI_STATE_INFERRING;

        uint32_t t_invoke_start = dwt_get_cycles();

#if MERA_INFERENCE_ENABLED
        mera_invoke();
#else
        /* Stub: skip NPU inference */
#endif

        uint32_t t_invoke_end = dwt_get_cycles();
        s_time_invoke_ms = dwt_cycles_to_ms(t_invoke_start, t_invoke_end);

        /* Update processing time for global diagnostics */
        g_processing_time.ai_inference_time_ms = s_time_invoke_ms;

        /* ---- Post-processing (F-003-8) ----
         *
         * YOLOv8 post-processing pipeline:
         *   1. Get output tensor pointer via mera_output_ptr()
         *   2. Dequantize INT8 output to float
         *   3. Decode YOLO bounding boxes (x_center, y_center, w, h)
         *   4. Apply confidence threshold filtering
         *   5. Apply Non-Maximum Suppression (NMS)
         *   6. Store results in g_ai_detection[] via update_detection_result()
         *
         * Reference: face_detection/src/ai_application/face_detection/MainLoop_obj.cc
         *            main_loop_face_detection() lines 115-142
         */
        s_ai_state = AI_STATE_POSTPROCESSING;

        {
#if MERA_INFERENCE_ENABLED
            int8_t *output = mera_output_ptr();
            fall_detection_postprocess(output);
#endif
        }

        INFERENCE_END_INDICATE_LED;

        uint32_t t_total_end = dwt_get_cycles();
        s_time_total_ms = dwt_cycles_to_ms(t_total_start, t_total_end);

        s_inference_count++;

        /* Signal that inference result is available */
        xEventGroupSetBits(g_ai_app_event, AI_INFERENCE_RESULT_UPDATED);

        s_ai_state = AI_STATE_IDLE;

        /*
         * Yield to the display thread. The AI thread does not need to run
         * faster than human reaction/response time, so a relatively larger
         * delay is used. This value should not be too low; otherwise, the
         * display thread performance is negatively influenced.
         *
         * Reference: face_detection/src/ai_inference_thread_entry.c lines 138-144
         */
        vTaskDelay(AI_THREAD_YIELD);
    }
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Update a detection result in the global detection array
 *
 * @details Called by the post-processing function (F-003-8) to store
 *          bounding box coordinates for each detected object.
 *
 * Reference: face_detection/src/ai_inference_thread_entry.c lines 71-80
 *
 * @param index Detection index (0 to AI_MAX_DETECTION_NUM - 1)
 * @param x     X coordinate of bounding box top-left corner
 * @param y     Y coordinate of bounding box top-left corner
 * @param w     Width of bounding box
 * @param h     Height of bounding box
 */
void update_detection_result(uint16_t index, signed short x, signed short y,
                              signed short w, signed short h)
{
    if (index < AI_MAX_DETECTION_NUM)
    {
        g_ai_detection[index].m_x = x;
        g_ai_detection[index].m_y = y;
        g_ai_detection[index].m_w = w;
        g_ai_detection[index].m_h = h;
    }
}
