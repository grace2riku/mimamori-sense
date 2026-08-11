/**
 * @file camera_display.c
 * @brief Camera frame to LVGL display - zero-copy (F-001-8)
 * @details
 * Displays camera frames on the LVGL screen using a zero-copy approach:
 * the lv_image_dsc_t descriptor's data pointer is updated to point directly
 * at the VIN capture buffer. No CPU memcpy is performed. Dave2D's DMA
 * handles the transfer from SDRAM to the GLCDC framebuffer.
 *
 * Performance history:
 *   Method B original:  4 FPS - lv_image_set_src() overhead every frame
 *   Method C (direct):  2 FPS - CPU memcpy to non-cached SDRAM (175ms/691KB)
 *   Method B optimized: 4 FPS - CPU memcpy source read from SDRAM (175ms/691KB)
 *   Zero-copy:         target 15-30 FPS - no CPU copy, Dave2D DMA only
 *
 * Key insight:
 *   ALL CPU memory copy involving SDRAM is ~4 MB/s on this platform, whether
 *   the destination is cached or non-cached. The bottleneck is SDRAM read
 *   throughput under bus contention (GLCDC DMA + VIN DMA + CPU all sharing
 *   the 16-bit SDRAM bus). Dave2D's DMA engine uses burst transfers that
 *   are far more efficient on the shared SDRAM bus.
 *
 * Data flow (zero-copy):
 *   VIN DMA -> capture buffer (SDRAM .sdram_noinit)
 *     -> Update lv_image_dsc_t.data pointer (just a pointer write)
 *       -> lv_obj_invalidate() -> Dave2D DMA blit to GLCDC FB
 *
 * The lv_image widget is resized from 1024x560 to 768x450 (matching the
 * camera resolution) and centered in the camera area. This eliminates the
 * need for letterbox copy - the screen background (black) provides the
 * natural letterbox borders.
 *
 * Reference:
 *   - Issue #9 (F-001-8)
 *
 * @note This file is part of the camera display implementation (F-001-8).
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>

#include "camera_display.h"
#include "camera_framebuffer.h"
#include "ui/ui_main_screen.h"
#include "jlink_console.h"

#include "camera_layer/camera_utils.h"
#include "common_util.h"
#include "ai_application/ai_application_config.h"
#include "ai_inference_thread_api.h"
#include "ui/fall_detection_screen.h"

#include "lvgl.h"

/* R-006 (Issue #156): FreeRTOS headers removed.
 *   xTaskGetTickCount()                 -> tk_get_otm (operating time, ms)
 *   g_ai_app_event (FreeRTOS EventGroup) -> g_ai_app_flgid (uT-Kernel event
 *     flag ID; R-007 で ai_inference_task が tk_cre_flg で生成・定義も同タスク側へ
 *     移動。0 = not created (AI タスク起動前) の間は AI パイプラインのブロックを
 *     スキップする - 旧 NULL ガード相当)。 */
#include <tk/tkernel.h>

#include "bsp_api.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size for diagnostic messages */
#define PRINT_BUF_SIZE          (128)

/** FPS measurement interval in milliseconds (1 second).
 *  R-006: FreeRTOS tick (configTICK_RATE_HZ) -> ms (tk_get_otm is ms-based,
 *  same change as camera_framebuffer.c in R-005). */
#define FPS_INTERVAL_MS         (1000)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/** LVGL timer handle for periodic frame polling */
static lv_timer_t *s_timer = NULL;

/** True after at least one camera frame has been transferred to the display */
static bool s_active = false;

/** True after AI inference thread initialization is confirmed */
static bool s_ai_init_done = false;

/**
 * LVGL image descriptor for zero-copy camera display.
 *
 * Dimensions: 768x450 RGB565 (matching camera capture resolution).
 * The data pointer is updated on each new frame to point directly
 * at the VIN capture buffer - no CPU memcpy needed.
 */
static lv_image_dsc_t s_cam_dsc;

/** True after lv_image_set_src() has been called with the zero-copy descriptor */
static bool s_source_set = false;

/** Total number of frames transferred to the display */
static uint32_t s_update_count = 0;

/** Display-side FPS: frames transferred in the last 1-second window */
static uint32_t s_fps = 0;

/** FPS counter: frames in the current measurement window */
static uint32_t s_fps_frame_count = 0;

/** FPS window start time (ms, tk_get_otm). R-006: TickType_t -> uint32_t ms */
static uint32_t s_fps_last_ms = 0;

/** Timing: total callback processing time (ms) */
static uint32_t s_time_total_ms = 0;

/**********************************************************************************************************************
 Exported global variables
 *********************************************************************************************************************/

/*
 * g_ai_app_flgid（AI アプリ用 uT-Kernel イベントフラグ ID）の定義は、R-007 で
 * 本来の所有者である src/ai_inference_thread_entry.c へ移動した（R-006 時点は
 * AI タスク未移行のため暫定的に本ファイルで定義していた）。
 * extern 宣言は ai_inference_thread_api.h。フラグは ai_inference_task() の先頭で
 * tk_cre_flg() により生成され、本ファイルは tk_ref_flg / tk_set_flg / tk_clr_flg で
 * 参照のみ行う（ID が 0 の間＝AI タスク起動前は各ブロックをスキップ）。
 */

/**********************************************************************************************************************
 Private (static) function prototypes
 *********************************************************************************************************************/

static void camera_display_timer_cb(lv_timer_t *timer);

/**
 * Get the current operating time in milliseconds (R-006).
 *
 * tk_get_otm() is a reference-type system call (task and ISR safe). SYSTIM
 * is a 64-bit hi/lo pair; the lower 32 bits suffice for the within-second
 * deltas used here (same technique as camera_framebuffer.c, R-005).
 */
static uint32_t camera_display_now_ms(void)
{
    SYSTIM now = {0, 0};
    (void)tk_get_otm(&now);
    return (uint32_t)now.lo;
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Initialize camera display transfer (zero-copy)
 */
void camera_display_init(void)
{
    /*
     * Initialize the zero-copy image descriptor.
     * Dimensions match the camera capture: 768x450 RGB565.
     * The data pointer starts as NULL and is updated on each frame.
     */
    memset(&s_cam_dsc, 0, sizeof(s_cam_dsc));
    s_cam_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    s_cam_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    s_cam_dsc.header.w      = CAMERA_FRAME_WIDTH;
    s_cam_dsc.header.h      = CAMERA_FRAME_HEIGHT;
    s_cam_dsc.header.stride = CAMERA_FRAME_WIDTH * 2u;
    s_cam_dsc.header.flags  = 0;
    s_cam_dsc.data_size     = CAMERA_FRAME_SIZE;
    s_cam_dsc.data          = NULL;

    s_source_set = false;

    /*
     * Reconfigure the lv_image widget: resize to 768x450 and center
     * within the camera area (below the 40px status bar).
     *
     * Position: X = (1024 - 768) / 2 = 128
     *           Y = 40 + (560 - 450) / 2 = 95
     *
     * The screen background (black) provides natural letterbox borders.
     * This eliminates the need for any CPU-side letterbox copy.
     */
    lv_obj_t *cam_img = ui_main_screen_get_camera_image();
    if (cam_img != NULL) {
        const uint16_t x_pos = (UI_CAMERA_WIDTH - CAMERA_FRAME_WIDTH) / 2u;
        const uint16_t y_pos = UI_STATUS_BAR_HEIGHT
                             + (UI_CAMERA_HEIGHT - CAMERA_FRAME_HEIGHT) / 2u;

        lv_obj_set_size(cam_img,
                        (int32_t)CAMERA_FRAME_WIDTH,
                        (int32_t)CAMERA_FRAME_HEIGHT);
        lv_obj_set_pos(cam_img, (int32_t)x_pos, (int32_t)y_pos);
    }

    /* Reset state */
    s_active = false;
    s_update_count = 0;
    s_fps = 0;
    s_fps_frame_count = 0;
    s_fps_last_ms = camera_display_now_ms();    /* R-006: xTaskGetTickCount() -> tk_get_otm (ms) */
    s_time_total_ms = 0;

    /*
     * Create LVGL timer for periodic camera frame polling.
     */
    s_timer = lv_timer_create(camera_display_timer_cb,
                              CAMERA_DISPLAY_TIMER_PERIOD_MS,
                              NULL);
}

/**
 * Transfer a camera frame to the LVGL display (zero-copy)
 *
 * @details Updates the image descriptor's data pointer to reference
 *          the frame buffer directly. No CPU memcpy is performed.
 *          Dave2D's DMA handles the actual pixel transfer during
 *          LVGL's render phase.
 */
void camera_display_update(const uint16_t *frame_buf,
                           uint16_t width, uint16_t height)
{
    if (frame_buf == NULL) {
        return;
    }

    (void)width;
    (void)height;

    lv_obj_t *cam_img = ui_main_screen_get_camera_image();
    if (cam_img == NULL) {
        return;
    }

    /* Zero-copy: update the data pointer in the descriptor */
    s_cam_dsc.data = (const uint8_t *)frame_buf;

    if (!s_source_set) {
        /*
         * First frame: call lv_image_set_src() once to bind the descriptor
         * to the widget. This replaces the original 1024x560 test-pattern
         * descriptor with our 768x450 zero-copy descriptor.
         */
        lv_image_set_src(cam_img, &s_cam_dsc);
        s_source_set = true;
    } else {
        /*
         * Subsequent frames: drop LVGL's image cache entry for this
         * descriptor so LVGL re-reads the (updated) data pointer.
         * This is much faster than lv_image_set_src() which triggers
         * full image source processing.
         */
        lv_image_cache_drop(&s_cam_dsc);
    }

    /* Trigger LVGL to redraw the camera image */
    lv_obj_invalidate(cam_img);

    /* Update statistics */
    s_active = true;
    s_update_count++;

    /* Display-side FPS measurement (R-006: tick -> ms, same formula change
     * as camera_framebuffer.c in R-005: frames * 1000 / elapsed_ms) */
    s_fps_frame_count++;
    uint32_t now_ms = camera_display_now_ms();
    uint32_t elapsed_ms = now_ms - s_fps_last_ms;

    if (elapsed_ms >= FPS_INTERVAL_MS) {
        s_fps = (s_fps_frame_count * 1000u) / elapsed_ms;
        s_fps_frame_count = 0;
        s_fps_last_ms = now_ms;
    }
}

uint32_t camera_display_get_fps(void)
{
    return s_fps;
}

uint32_t camera_display_get_update_count(void)
{
    return s_update_count;
}

bool camera_display_is_active(void)
{
    return s_active;
}

/**
 * NT-Shell "camera display" diagnostic command handler
 */
void camera_display_cmd_status(void)
{
    char buf[PRINT_BUF_SIZE];

    print_to_console("[Camera Display Transfer (F-001-8, Zero-copy)]\r\n");

    snprintf(buf, sizeof(buf), "  Active        : %s\r\n",
             s_active ? "Yes" : "No");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Method        : Zero-copy (pointer update + Dave2D DMA)\r\n");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Source        : %u x %u (RGB565)\r\n",
             (unsigned int)CAMERA_FRAME_WIDTH,
             (unsigned int)CAMERA_FRAME_HEIGHT);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Widget size   : %u x %u (centered at %u, %u)\r\n",
             (unsigned int)CAMERA_FRAME_WIDTH,
             (unsigned int)CAMERA_FRAME_HEIGHT,
             (unsigned int)((UI_CAMERA_WIDTH - CAMERA_FRAME_WIDTH) / 2u),
             (unsigned int)(UI_STATUS_BAR_HEIGHT
                          + (UI_CAMERA_HEIGHT - CAMERA_FRAME_HEIGHT) / 2u));
    print_to_console(buf);

    if (s_cam_dsc.data != NULL) {
        snprintf(buf, sizeof(buf), "  Data pointer  : 0x%08lX (VIN buffer, zero-copy)\r\n",
                 (unsigned long)(uintptr_t)s_cam_dsc.data);
    } else {
        snprintf(buf, sizeof(buf), "  Data pointer  : (none)\r\n");
    }
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Update Count  : %lu\r\n",
             (unsigned long)s_update_count);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Display FPS   : %lu\r\n",
             (unsigned long)s_fps);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Callback time : %lu ms\r\n",
             (unsigned long)s_time_total_ms);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Timer Period  : %u ms\r\n",
             (unsigned int)CAMERA_DISPLAY_TIMER_PERIOD_MS);
    print_to_console(buf);
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * LVGL timer callback for camera display update
 *
 * @details Polls for new camera frames and updates the image descriptor's
 *          data pointer (zero-copy). The actual pixel transfer to the GLCDC
 *          framebuffer is handled by Dave2D DMA during LVGL's render phase.
 */
static void camera_display_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    void *frame = camera_framebuffer_get_latest();
    if (frame == NULL) {
        return;
    }

    uint32_t t_start_ms = camera_display_now_ms();  /* R-006: tick -> ms */

    /*
     * Cache maintenance for the VIN DMA buffer.
     *
     * The VIN hardware writes frame data to SDRAM via DMA. We invalidate
     * the CPU D-cache to ensure that:
     *   1. Any stale CPU cache lines for this address range are discarded
     *   2. Dave2D DMA reads the actual VIN-written data from SDRAM
     *
     * Note: Dave2D's DMA engine may bypass the CPU cache entirely (reads
     * directly from SDRAM), in which case this is a safety precaution.
     */
    SCB_InvalidateDCache_by_Addr(frame, (int32_t)CAMERA_FRAME_SIZE);

    /* Zero-copy update: just update the pointer + invalidate */
    camera_display_update((const uint16_t *)frame,
                          CAMERA_FRAME_WIDTH,
                          CAMERA_FRAME_HEIGHT);

    /* ---- AI inference pipeline: preprocess + notify (F-003-12) ----
     *
     * R-006: g_ai_app_event (FreeRTOS event group, NULL guard) ->
     * g_ai_app_flgid (uT-Kernel event flag ID, <=0 guard). R-007 で
     * ai_inference_task がフラグを生成するようになったため、AI タスク起動後は
     * 本ブロックが実行される（起動前は従来どおりスキップ）。
     *   xEventGroupGetBits  -> tk_ref_flg (reference call, returns flgptn)
     *   xEventGroupSetBits  -> tk_set_flg
     */
    if (!s_ai_init_done) {
        /* Check if AI inference task has completed initialization */
        if (g_ai_app_flgid > 0) {
            T_RFLG rflg;
            if (E_OK == tk_ref_flg(g_ai_app_flgid, &rflg)) {
                if (rflg.flgptn & SOFTWARE_AI_INFERENCE_INIT_DONE) {
                    s_ai_init_done = true;
                }
            }
        }
    }

    if (s_ai_init_done) {
        /* Preprocess: RGB565 (768x450) -> RGB INT8 (224x224x3) */
        image_rgb565_to_rgb_int8(frame, model_buffer_int8,
                                  CAMERA_FRAME_WIDTH, CAMERA_FRAME_HEIGHT,
                                  AI_INPUT_IMAGE_WIDTH, AI_INPUT_IMAGE_HEIGHT);

        /* D-Cache clean: flush model_buffer_int8 so NPU DMA reads correct data */
        SCB_CleanDCache_by_Addr((void *)model_buffer_int8,
                                (int32_t)AI_INPUT_IMAGE_SIZE);

        /* Notify AI inference task that preprocessed image is ready */
        (void)tk_set_flg(g_ai_app_flgid, AI_INFERENCE_INPUT_IMAGE_READY);
    }

    s_time_total_ms = camera_display_now_ms() - t_start_ms;

    /*
     * Update fall detection screen overlay (F-003-10)
     *
     * Check if new AI inference results are available and update the
     * overlay widgets (bounding boxes, status text, info panel).
     * The AI_INFERENCE_RESULT_UPDATED event is set by ai_inference_thread
     * after each inference + post-processing + fall_detection_update cycle.
     *
     * We check and clear the event bit here so the overlay is updated
     * in sync with the display refresh. Even if no new AI results are
     * available, we still call update to keep the info panel (FPS) current.
     */
    if (g_ai_app_flgid > 0)
    {
        T_RFLG rflg;
        if ((E_OK == tk_ref_flg(g_ai_app_flgid, &rflg)) &&
            (rflg.flgptn & AI_INFERENCE_RESULT_UPDATED))
        {
            /* Clear the event bit.
             * R-006: xEventGroupClearBits(grp, bit) -> tk_clr_flg(id, ~bit)
             * (tk_clr_flg performs flgptn &= clrptn - eventflag.c:204). */
            (void)tk_clr_flg(g_ai_app_flgid, (UINT)~AI_INFERENCE_RESULT_UPDATED);

            /* Update overlay with latest detection results */
            fall_detection_screen_update();
        }
    }
}
