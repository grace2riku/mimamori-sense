/**
 * @file ai_inference_thread_api.h
 * @brief AI inference thread public API for inter-thread queries (F-003-7)
 * @details
 * Provides thread-safe query functions for AI inference thread state.
 * These functions can be called from other threads (e.g., ntshell_thread
 * for "ai status" / "ai time" diagnostics).
 *
 * Note: ra_gen/ai_inference_thread.h (which used to declare the FreeRTOS
 * entry ai_inference_thread_entry()) is no longer generated - Issue #186
 * Step 2 set the FSP RTOS selection to No RTOS. The uT-Kernel task entry is
 * ai_inference_task(), created in src/usermain.c.
 * This header provides additional public functions implemented in
 * ai_inference_thread_entry.c.
 */

#ifndef AI_INFERENCE_THREAD_API_H
#define AI_INFERENCE_THREAD_API_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>

/* R-007 (Issue #157): FreeRTOS.h / event_groups.h removed.
 * g_ai_app_event (FreeRTOS EventGroupHandle_t) -> g_ai_app_flgid
 * (uT-Kernel event flag ID). */
#include <tk/tkernel.h>

/**********************************************************************************************************************
 Exported global variables
 *********************************************************************************************************************/

/**
 * AI application uT-Kernel event flag ID for inter-task synchronization.
 *
 * Defined and created (tk_cre_flg) in ai_inference_thread_entry.c
 * (ai_inference_task). 0 means "not yet created" - readers must guard
 * with `g_ai_app_flgid > 0` before tk_ref_flg/tk_set_flg/tk_clr_flg
 * (replaces the former `g_ai_app_event != NULL` guard).
 *
 * Event bits defined in common_util.h:
 *   HARDWARE_ETHOSU_INIT_DONE      (bit 2)
 *   SOFTWARE_AI_INFERENCE_INIT_DONE (bit 3)
 *   AI_INFERENCE_INPUT_IMAGE_READY  (bit 13)
 *   AI_INFERENCE_RESULT_UPDATED     (bit 14)
 */
extern ID g_ai_app_flgid;

/**
 * AI model input buffer (preprocessed image, INT8).
 *
 * Written by camera preprocessing (F-003-6), read by inference thread.
 * Size: AI_INPUT_IMAGE_WIDTH * AI_INPUT_IMAGE_HEIGHT * AI_INPUT_IMAGE_BYTE_PER_PIXEL
 */
extern int8_t model_buffer_int8[];
extern uint32_t model_buffer_int8_size;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the current AI inference thread state (numeric)
 *
 * @return State value: 0=NOT_STARTED, 1=INITIALIZING, 2=IDLE,
 *         3=PREPROCESSING, 4=INFERRING, 5=POSTPROCESSING, 6=ERROR
 */
uint32_t ai_inference_get_state(void);

/**
 * Get the current AI inference thread state name as a string
 *
 * @return Null-terminated state name string
 */
const char *ai_inference_get_state_name(void);

/**
 * Get the total inference count since boot
 *
 * @return Inference cycle count (monotonically increasing)
 */
uint32_t ai_inference_get_count(void);

/**
 * Get the last input memcpy time in milliseconds
 *
 * @return Memcpy time (model_buffer_int8 -> MERA arena) in ms
 */
uint32_t ai_inference_get_memcpy_time_ms(void);

/**
 * Get the last NPU inference time in milliseconds
 *
 * @return mera_invoke() execution time in ms
 */
uint32_t ai_inference_get_invoke_time_ms(void);

/**
 * Get the last total inference cycle time in milliseconds
 *
 * @return Total time (memcpy + invoke + postprocess) in ms
 */
uint32_t ai_inference_get_total_time_ms(void);

/**
 * NPU inference time statistics in DWT cycles (Issue #148)
 *
 * ms 単位の getter では 3ms と 5.5ms を区別できないため、
 * 解像度アップ (案H) の可否判定用にサイクル単位で保持する。
 */
typedef struct {
    uint32_t count;     /**< 計測回数 */
    uint32_t last_cyc;  /**< 直近の推論サイクル数 */
    uint32_t min_cyc;   /**< 最小 (純粋な推論時間に最も近い) */
    uint32_t avg_cyc;   /**< 平均 */
    uint32_t max_cyc;   /**< 最大 */
    uint32_t cpu_hz;    /**< 換算に使う CPU クロック (SystemCoreClock) */
} ai_invoke_stats_t;

/**
 * Get NPU inference time statistics in DWT cycles (Issue #148)
 *
 * @param[out] stats 統計の格納先 (NULL 不可)
 */
void ai_inference_get_invoke_stats(ai_invoke_stats_t *stats);

/**
 * Reset NPU inference time statistics (Issue #148)
 */
void ai_inference_reset_invoke_stats(void);

/**
 * Update a detection result in g_ai_detection[]
 *
 * Called by the post-processing function (F-003-8) to store
 * bounding box coordinates for each detected object.
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
                              signed short w, signed short h);

#ifdef __cplusplus
}
#endif

#endif /* AI_INFERENCE_THREAD_API_H */
