/**
 * @file ai_inference_thread_entry.c
 * @brief Fall detection AI inference task (F-003-7)
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
 * Event flow (uT-Kernel event flag g_ai_app_flgid):
 *   camera_display (LVGL timer cb) -> tk_set_flg(IMAGE_READY)
 *     -> ai_inference_task -> memcpy -> mera_invoke() -> post-process
 *       -> tk_set_flg(RESULT_UPDATED) -> camera_display (tk_ref_flg/tk_clr_flg)
 *
 * R-007 / Issue #157（FreeRTOS -> uT-Kernel 3.0 移行）:
 *   本タスクは src/usermain.c の usermain() から tk_cre_tsk + tk_sta_tsk で
 *   生成・起動される（方式A: ra_gen/main.c の FreeRTOS スレッド生成経路は未到達）。
 *   - エントリ関数は uT-Kernel タスク形式 ai_inference_task(INT stacd, void *exinf) へ移植。
 *   - 同期オブジェクトを FreeRTOS イベントグループ g_ai_app_event
 *     （xEventGroupCreateStatic / WaitBits / SetBits）から uT-Kernel イベントフラグ
 *     g_ai_app_flgid（tk_cre_flg / tk_wai_flg / tk_set_flg）へ置換。
 *     ビット割り当ては common_util.h のまま（HARDWARE_ETHOSU_INIT_DONE 等）。
 *   - vTaskDelay -> tk_dly_tsk（configTICK_RATE_HZ=1000 のため値は ms のまま等価）。
 *   - Ethos-U コアドライバの RTOS フック（ethosu_mutex_* / ethosu_semaphore_*）は
 *     本プロジェクトでは weak のベアメタル実装（__WFE/__SEV）のまま使用しており、
 *     FreeRTOS 実装での上書きは元々存在しない（ethosu_driver.c:165-232）。NPU 完了
 *     割り込み（rm_ethosu_isr -> ethosu_irq_handler -> ethosu_semaphore_give = __SEV）
 *     はカーネル API を呼ばないため uT-Kernel 下でも無変更で動作する。
 *   旧 FreeRTOS エントリ ai_inference_thread_entry() は対応関係の追跡用に末尾へ残置
 *   （方式A では未使用だが ra_gen/ai_inference_thread.c のリンク解決に必要）。
 *   詳細は doc/migration/mtk3-migration-guide.md 7.5。
 *
 * uT-Kernel タスク設定（usermain.c で生成）:
 *   Stack size: 16384 bytes（FreeRTOS 版 ra_gen/ai_inference_thread.c の 0x4000 と同値）
 *   Priority  : itskpri=15（描画スレッド 13 / lvgl 14 より低い最下位グループ）
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
#include "fall_detection_logic.h"

/* R-007 (Issue #157): FreeRTOS headers (FreeRTOS.h / task.h / event_groups.h)
 * removed. 対応関係:
 *   xEventGroupCreateStatic -> tk_cre_flg（TA_TFIFO|TA_WMUL ― ov5640.c と同一属性）
 *   xEventGroupWaitBits     -> tk_wai_flg（TWF_ANDW|TWF_BITCLR, TMO_FEVR）
 *   xEventGroupSetBits      -> tk_set_flg
 *   vTaskDelay(n)           -> tk_dly_tsk(n)（n は ms。tick=1ms のため値は不変） */
#include <tk/tkernel.h>

#include "ai_inference_thread_api.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * AI task yield delay (ms) after each inference cycle.
 *
 * This prevents the AI task from starving lower-priority tasks.
 * The value should not be too low; otherwise, the display thread
 * performance is negatively impacted.
 *
 * R-007: 単位を tick -> ms へ読み替え（FreeRTOS 版は configTICK_RATE_HZ=1000 の
 * tick 数 25 = 25ms。tk_dly_tsk は ms 指定のため値は 25 のまま等価）。
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
 * AI application uT-Kernel event flag ID for inter-task synchronization (R-007).
 *
 * R-007: FreeRTOS イベントグループ g_ai_app_event（+ StaticEventGroup_t）を
 * uT-Kernel イベントフラグ ID へ置換した。ビット割り当ては common_util.h のまま。
 * フラグ本体は ai_inference_task() の先頭で tk_cre_flg() により生成する
 * （FSP 構成に AI 同期用イベントグループが無く、従来も本ファイルで生成していた）。
 * 0 のままなら「未生成」を意味し、参照側（camera_display.c / ai_cmd.c）は
 * `g_ai_app_flgid > 0` ガードでスキップする（旧 `g_ai_app_event != NULL` ガード相当）。
 *
 * Used by:
 *   - ai_inference_task: waits on IMAGE_READY (tk_wai_flg), sets
 *     ETHOSU_INIT_DONE / AI_INFERENCE_INIT_DONE / RESULT_UPDATED (tk_set_flg)
 *   - camera_display (LVGL timer cb): sets IMAGE_READY (tk_set_flg), checks
 *     AI_INFERENCE_INIT_DONE / RESULT_UPDATED (tk_ref_flg) and clears
 *     RESULT_UPDATED (tk_clr_flg)
 *   - ai_cmd.c "ai status": reads flag pattern (tk_ref_flg)
 *
 * extern 宣言は ai_inference_thread_api.h（R-006 時点の暫定定義は
 * camera_display.c に置かれていたが、R-007 で本来の所有者である本ファイルへ移動）。
 *
 * Reference: face_detection common_data.h (FSP-generated EventGroupHandle_t)
 */
ID g_ai_app_flgid = 0;

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

/**
 * NPU inference time statistics in DWT cycles (Issue #148).
 *
 * ms 単位の s_time_invoke_ms では 3ms と 5.5ms を区別できず、
 * 解像度アップ (案H) の可否判定に使えない。サイクル単位で
 * min / max / 累計を貯め、`ai bench` が us へ換算して表示する。
 *
 * 計測点は mera_invoke() の前後 (wrapper.h の注記どおり同期実行)。
 * DWT->CYCCNT は CPU サイクルの自由走行カウンタなので、NPU 待ちの間に
 * 他タスクへ切り替わるとその時間も含まれる。**min が純粋な推論時間に
 * 最も近い値**であり、avg/max との差はプリエンプションの影響とみなせる。
 */
static volatile uint32_t s_invoke_cyc_last  = 0;   /**< Last invoke (cycles) */
static volatile uint32_t s_invoke_cyc_min   = 0xFFFFFFFFU;
static volatile uint32_t s_invoke_cyc_max   = 0;
static volatile uint64_t s_invoke_cyc_sum   = 0;
static volatile uint32_t s_invoke_cyc_count = 0;

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
 * Get NPU inference time statistics in DWT cycles (Issue #148)
 *
 * @param[out] stats 統計の格納先。NULL 不可
 *
 * @details 未計測 (count==0) の場合は min に 0 を入れて返す。
 *          サイクル -> us の換算は呼び出し側で cpu_hz を使って行う。
 */
void ai_inference_get_invoke_stats(ai_invoke_stats_t *stats)
{
    if (NULL == stats) {
        return;
    }
    uint32_t count = s_invoke_cyc_count;
    stats->count      = count;
    stats->last_cyc   = s_invoke_cyc_last;
    stats->max_cyc    = s_invoke_cyc_max;
    stats->min_cyc    = (0U == count) ? 0U : s_invoke_cyc_min;
    stats->avg_cyc    = (0U == count) ? 0U
                        : (uint32_t)(s_invoke_cyc_sum / (uint64_t)count);
    stats->cpu_hz     = SystemCoreClock;
}

/**
 * Reset NPU inference time statistics (Issue #148)
 *
 * @details 条件を変えて測り直すとき (解像度変更・クロック変更など) に使う。
 */
void ai_inference_reset_invoke_stats(void)
{
    s_invoke_cyc_count = 0;
    s_invoke_cyc_sum   = 0;
    s_invoke_cyc_min   = 0xFFFFFFFFU;
    s_invoke_cyc_max   = 0;
    s_invoke_cyc_last  = 0;
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
 * AI inference task entry function (F-003-7)
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
 * R-007: uT-Kernel タスク本体（usermain() から tk_cre_tsk + tk_sta_tsk で生成・起動）。
 *
 * @param stacd タスク起動コード（未使用）
 * @param exinf 拡張情報（未使用）
 */
void ai_inference_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    char buf[AI_PRINT_BUF_SIZE];

    s_ai_state = AI_STATE_INITIALIZING;

    /*
     * Wait for JLink console to be ready so we can log initialization progress.
     * The ntshell task initializes the UART console (jlink_console_init);
     * waiting here avoids a concurrent double-open of SCI8 from
     * print_to_console()'s internal init loop (camera_task と同一パターン)。
     */
    while (!jlink_configured())
    {
        tk_dly_tsk(100);
    }

    /* ======================================================================
     * Step 0: Create the AI application event flag (uT-Kernel)
     *
     * The reference project (face_detection) has g_ai_app_event as an
     * FSP-generated FreeRTOS event group. This project creates the
     * synchronization object in user code instead.
     * R-007: xEventGroupCreateStatic -> tk_cre_flg。属性は ov5640.c の
     * I2C 完了フラグと同一（TA_TFIFO | TA_WMUL、初期パターン 0）。
     * USE_OBJECT_NAME = 0 のため dsname は初期化子に含めない。
     * ====================================================================== */
    if (g_ai_app_flgid <= 0)
    {
        T_CFLG cflg = {
            .exinf   = NULL,
            .flgatr  = TA_TFIFO | TA_WMUL,
            .iflgptn = 0,
        };

        ID flgid = tk_cre_flg(&cflg);
        if (flgid <= E_OK)
        {
            snprintf(buf, sizeof(buf),
                     "ERROR: tk_cre_flg(g_ai_app_flgid) failed (ercd=%d).\r\n", (int)flgid);
            ai_thread_log(buf);
            s_ai_state = AI_STATE_ERROR;
            while (1) { tk_dly_tsk(1000); }   /* R-007: vTaskDelay(1000) -> tk_dly_tsk(1000) */
        }

        /* ID を公開（camera_display.c / ai_cmd.c は >0 ガードで参照開始する） */
        g_ai_app_flgid = flgid;
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

    /* Poll for camera readiness (camera task does not use g_ai_app_flgid) */
    while (!camera_thread_is_initialized())
    {
        if (camera_thread_has_error())
        {
            ai_thread_log("  WARNING: Camera init failed. Continuing NPU init.\r\n");
            break;
        }
        tk_dly_tsk(100);    /* R-007: vTaskDelay(100) -> tk_dly_tsk(100) */
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

    /* R-007: xEventGroupSetBits -> tk_set_flg */
    {
        ER ercd = tk_set_flg(g_ai_app_flgid, HARDWARE_ETHOSU_INIT_DONE);
        if (E_OK != ercd)
        {
            snprintf(buf, sizeof(buf),
                     "  ERROR: tk_set_flg(ETHOSU_INIT_DONE) failed (ercd=%d)\r\n", (int)ercd);
            ai_thread_log(buf);
        }
    }

#if YOLO_FASTEST_MODEL
    snprintf(buf, sizeof(buf), "  NPU initialized. Arena sub0=%lu bytes (YOLO-Fastest V1)\r\n",
             (unsigned long)mera_arena_size_sub0());
    ai_thread_log(buf);
#else
    snprintf(buf, sizeof(buf), "  NPU initialized. Arena sub0=%lu, sub2=%lu bytes (YOLOv8 legacy)\r\n",
             (unsigned long)mera_arena_size_sub0(),
             (unsigned long)mera_arena_size_sub2());
    ai_thread_log(buf);
#endif
#else
    /* Stub: Skip NPU init to avoid linking MERA model data (~307KB) */
    /* R-007: xEventGroupSetBits -> tk_set_flg */
    {
        ER ercd = tk_set_flg(g_ai_app_flgid, HARDWARE_ETHOSU_INIT_DONE);
        if (E_OK != ercd)
        {
            snprintf(buf, sizeof(buf),
                     "  ERROR: tk_set_flg(ETHOSU_INIT_DONE) failed (ercd=%d)\r\n", (int)ercd);
            ai_thread_log(buf);
        }
    }
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
     * Step 3c: Initialize fall detection logic module (F-003-9)
     *
     * Sets default threshold parameters and resets the state machine
     * to NORMAL state.
     * ====================================================================== */
    fall_detection_init();
    ai_thread_log("  Fall detection logic initialized (F-003-9).\r\n");

    /* ======================================================================
     * Step 4: Signal AI inference initialization complete
     *
     * camera_display.c の LVGL タイマコールバックが本ビットを tk_ref_flg で
     * 確認後、前処理 + IMAGE_READY 通知を開始する（camera_display.c:399-422）。
     *
     * Reference: face_detection/src/ai_inference_thread_entry.c line 115
     * ====================================================================== */
    /* R-007: xEventGroupSetBits -> tk_set_flg */
    {
        ER ercd = tk_set_flg(g_ai_app_flgid, SOFTWARE_AI_INFERENCE_INIT_DONE);
        if (E_OK != ercd)
        {
            snprintf(buf, sizeof(buf),
                     "  ERROR: tk_set_flg(AI_INFERENCE_INIT_DONE) failed (ercd=%d)\r\n", (int)ercd);
            ai_thread_log(buf);
        }
    }

    ai_thread_log("  AI inference initialization complete. Entering inference loop.\r\n");

    s_ai_state = AI_STATE_IDLE;

    /* ======================================================================
     * Step 5: Inference loop
     *
     * Reference: face_detection/src/ai_inference_thread_entry.c lines 117-144
     * ====================================================================== */
    while (true)
    {
        /* Wait for preprocessed image to be ready.
         *
         * R-007: xEventGroupWaitBits(..., pdTRUE, pdTRUE, portMAX_DELAY)
         *        -> tk_wai_flg(..., TWF_ANDW | TWF_BITCLR, ..., TMO_FEVR)。
         *   - TWF_ANDW   : 待ちビット全成立で解除（xWaitForAllBits=pdTRUE 相当。
         *                  待ちビットは IMAGE_READY の 1 ビットのみ）
         *   - TWF_BITCLR : 解除時に「待ちビットのみ」クリア（xClearOnExit=pdTRUE 相当。
         *                  TWF_CLR は全ビットクリアのため不可 ― ETHOSU_INIT_DONE /
         *                  AI_INFERENCE_INIT_DONE が消えると camera_display.c の
         *                  init 確認や ai status 表示が壊れる）
         *   - TMO_FEVR   : 無限待ち（portMAX_DELAY 相当）
         */
        UINT flgptn = 0;
        ER ercd = tk_wai_flg(g_ai_app_flgid,
                             AI_INFERENCE_INPUT_IMAGE_READY,
                             TWF_ANDW | TWF_BITCLR,
                             &flgptn,
                             TMO_FEVR);
        if (E_OK != ercd)
        {
            snprintf(buf, sizeof(buf),
                     "ERROR: tk_wai_flg(IMAGE_READY) failed (ercd=%d)\r\n", (int)ercd);
            ai_thread_log(buf);
            s_ai_state = AI_STATE_ERROR;
            tk_dly_tsk(1000);
            continue;
        }

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

        /* Issue #148: サイクル単位の統計を蓄積する (us 分解能の計測用).
         * 符号なし減算なので CYCCNT の 1 周回 (1GHz で約 4.29 秒) は吸収される。
         * 1 回の推論は数 ms なので 2 周以上することはない。 */
        {
            uint32_t cyc = t_invoke_end - t_invoke_start;
            s_invoke_cyc_last = cyc;
            if (cyc < s_invoke_cyc_min) { s_invoke_cyc_min = cyc; }
            if (cyc > s_invoke_cyc_max) { s_invoke_cyc_max = cyc; }
            s_invoke_cyc_sum += (uint64_t)cyc;
            s_invoke_cyc_count++;
        }

        /* Update processing time for global diagnostics */
        g_processing_time.ai_inference_time_ms = s_time_invoke_ms;

        /* ---- Post-processing (F-003-8) ----
         *
         * YOLOv3 anchor-based post-processing pipeline (YOLO-Fastest V1):
         *   1. Get 2 output tensor pointers via mera_output_ptr_branch0/1()
         *   2. For each branch: dequantize INT8, decode anchors, filter
         *   3. Apply Non-Maximum Suppression (NMS)
         *   4. Store results in g_ai_detection[] via update_detection_result()
         *
         * Reference: face_detection/src/ai_application/face_detection/MainLoop_obj.cc
         *            main_loop_face_detection() lines 115-142
         */
        s_ai_state = AI_STATE_POSTPROCESSING;

        {
#if MERA_INFERENCE_ENABLED
            int8_t *branch0 = mera_output_ptr_branch0();
            int8_t *branch1 = mera_output_ptr_branch1();
            fall_detection_postprocess(branch0, branch1);
#endif
        }

        /* ---- Fall detection judgment (F-003-9) ----
         *
         * Evaluate detection results against fall criteria (aspect ratio,
         * score threshold) and advance the state machine.
         * This must run after post-processing populates g_fall_detection_results[].
         */
        fall_detection_update();

        INFERENCE_END_INDICATE_LED;

        uint32_t t_total_end = dwt_get_cycles();
        s_time_total_ms = dwt_cycles_to_ms(t_total_start, t_total_end);

        s_inference_count++;

        /* Signal that inference result is available.
         * R-007: xEventGroupSetBits -> tk_set_flg。camera_display.c の LVGL
         * タイマコールバックが tk_ref_flg で本ビットを確認し tk_clr_flg で
         * クリアして転倒検出オーバーレイを更新する（camera_display.c:439-451）。 */
        ercd = tk_set_flg(g_ai_app_flgid, AI_INFERENCE_RESULT_UPDATED);
        if (E_OK != ercd)
        {
            snprintf(buf, sizeof(buf),
                     "ERROR: tk_set_flg(RESULT_UPDATED) failed (ercd=%d)\r\n", (int)ercd);
            ai_thread_log(buf);
        }

        s_ai_state = AI_STATE_IDLE;

        /*
         * Yield to the display thread. The AI task does not need to run
         * faster than human reaction/response time, so a relatively larger
         * delay is used. This value should not be too low; otherwise, the
         * display thread performance is negatively influenced.
         *
         * R-007: vTaskDelay(AI_THREAD_YIELD) -> tk_dly_tsk(AI_THREAD_YIELD)
         * （25 tick @1000Hz = 25ms で値は等価）。
         *
         * Reference: face_detection/src/ai_inference_thread_entry.c lines 138-144
         */
        tk_dly_tsk(AI_THREAD_YIELD);
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

/**
 * 旧 FreeRTOS スレッドエントリ（R-007 移行前の名残り。対応関係追跡用に残置）。
 *
 * 方式A（src/hal_warmstart.c の静的コンストラクタで uT-Kernel を起動し、
 * ra_gen/main.c の main()/vTaskStartScheduler() に到達しない）では、本関数は
 * 実行時には呼ばれない。しかし ra_gen/ai_inference_thread.c（編集禁止）の
 * ai_inference_thread_func() が本シンボルを参照し、その参照鎖は startup が参照する
 * main() から辿れるためリンク時には解決が必要。よって削除せず、実体を
 * uT-Kernel タスク ai_inference_task() へ委譲する薄いラッパとして残す
 * （実行されない。FreeRTOS ブートへ切り戻した場合もリンクのみ通る ― タスク本体が
 *   tk_* の uT-Kernel API を直接呼ぶため、実行には本体 API の FreeRTOS への差し戻しが必要）。
 * ntshell_thread_entry.c / camera_thread_entry.c / lvgl_thread_entry.c 末尾の
 * ラッパと同一パターン。
 *
 * @param pvParameters FreeRTOSタスクパラメータ（未使用）
 */
void ai_inference_thread_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);
    /* uT-Kernel タスク本体へ委譲（stacd/exinf は未使用）。 */
    ai_inference_task(0, NULL);
}
