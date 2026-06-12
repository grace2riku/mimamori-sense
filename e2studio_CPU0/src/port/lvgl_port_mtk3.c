/**
 * @file lvgl_port_mtk3.c
 * @brief uT-Kernel 3.0 replacement for the FSP RM_LVGL_PORT module (R-006 / Issue #156)
 * @details
 * 1:1 re-implementation of ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c with only
 * the OS-dependent parts replaced (FreeRTOS -> uT-Kernel 3.0). See
 * lvgl_port_mtk3.h for the rationale (BSP_CFG_RTOS==2 + boot method A).
 *
 * Correspondence with the original (rm_lvgl_port.c):
 *
 *   | original (FreeRTOS)                              | this file (uT-Kernel)            |
 *   |--------------------------------------------------|----------------------------------|
 *   | g_semaphore_vpos = xSemaphoreCreateBinaryStatic  | tk_cre_sem (maxsem=1)            |
 *   | RM_LVGL_PORT_Open (lines 82-171)                 | lvgl_port_mtk3_open              |
 *   | _rm_lvgl_port_display_callback (184-214)         | lvgl_port_mtk3_display_callback  |
 *   |   xSemaphoreGiveFromISR + portYIELD_FROM_ISR     |   tk_sig_sem (delayed dispatch)  |
 *   | rm_lvgl_port_flush_cb (223-244)                  | lvgl_port_mtk3_flush_cb          |
 *   | rm_lvgl_port_flush_wait_cb (250-269)             | lvgl_port_mtk3_flush_wait_cb     |
 *   |   xSemaphoreTake(0); xSemaphoreTake(portMAX_DELAY)| tk_wai_sem(TMO_POL); tk_wai_sem(TMO_FEVR) |
 *   | rm_lvgl_port_tick_get_cb (276-285)               | lvgl_port_mtk3_tick_get_cb       |
 *   |   pdTICKS_TO_MS(xTaskGetTickCount())             |   tk_get_otm(SYSTIM*).lo (ms)    |
 *
 * IMPORTANT (FSP version updates): if rm_lvgl_port.c changes in a future
 * FSP release, diff it against this file and re-apply the differences
 * (registered in migration guide 6.2 re-apply checklist).
 *
 * Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <string.h>

#include "lvgl_port_mtk3.h"

#include "common_data.h"            /* g_lvgl_port_cfg (read-only) / display instance */
#include "rm_lvgl_port_cfg.h"       /* RM_LVGL_PORT_CFG_PROVIDE_TICK_CALLBACK + LVGL_DISPLAY_* via common_data.h */
#include "r_glcdc.h"

#include <tk/tkernel.h>

/**********************************************************************************************************************
 Private (static) function prototypes
 *********************************************************************************************************************/
static void lvgl_port_mtk3_display_callback(display_callback_args_t *p_args);
static void lvgl_port_mtk3_flush_cb(lv_display_t *p_lv_display, const lv_area_t *p_lv_area,
                                    uint8_t *p_px_map);
static void lvgl_port_mtk3_flush_wait_cb(lv_display_t *p_lv_display);
#if RM_LVGL_PORT_CFG_PROVIDE_TICK_CALLBACK
static uint32_t lvgl_port_mtk3_tick_get_cb(void);
#endif

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/** Vsync (DISPLAY_EVENT_LINE_DETECTION) semaphore ID. 0 = not created.
 *  Replaces g_semaphore_vpos (binary semaphore) of rm_lvgl_port.c. */
static ID s_vsync_semid = 0;

/** Copy of the FSP display cfg with p_callback swapped to this module.
 *  g_display0_cfg (ra_gen) itself is NOT modified. */
static display_cfg_t s_display_cfg;

/** LVGL display object (replaces g_lvgl_port_ctrl.p_lv_display) */
static lv_display_t *s_lv_display = NULL;

/** LVGL port cfg in use (= &g_lvgl_port_cfg). Provides display instance,
 *  inherit_frame_layer and the user callback (lvgl_glcdc_callback). */
static rm_lvgl_port_cfg_t const *s_p_cfg = NULL;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Open the display port (uT-Kernel version of RM_LVGL_PORT_Open).
 *
 * Same sequence as rm_lvgl_port.c:82-171. See lvgl_port_mtk3.h.
 */
fsp_err_t lvgl_port_mtk3_open(rm_lvgl_port_cfg_t const *p_cfg)
{
    fsp_err_t error;

    if (NULL == p_cfg) {
        return FSP_ERR_ASSERTION;
    }
    s_p_cfg = p_cfg;

    /* --- Vsync semaphore (rm_lvgl_port.c:93-95 xSemaphoreCreateBinaryStatic) ---
     * Binary semaphore equivalent: counting semaphore with maxsem=1.
     * tk_sig_sem on a full semaphore returns E_QOVR which is ignored
     * (same saturation behavior as a FreeRTOS binary semaphore Give). */
    if (s_vsync_semid <= 0) {       /* idempotent (re-open after error) */
        T_CSEM csem = {
            .exinf   = NULL,
            .sematr  = TA_TFIFO | TA_FIRST,
            .isemcnt = 0,
            .maxsem  = 1,
        };
        ID semid = tk_cre_sem(&csem);
        if (semid <= E_OK) {
            return FSP_ERR_OUT_OF_MEMORY;
        }
        s_vsync_semid = semid;
    }

    /* --- Clear both framebuffers (rm_lvgl_port.c:103-113) --- */
    if (NULL != p_cfg->p_framebuffer_0) {
        memset(p_cfg->p_framebuffer_0, 0,
               (LVGL_DISPLAY_BUFFER_STRIDE_BYTES_INPUT * LVGL_DISPLAY_VSIZE_INPUT));
    }

    if (NULL != p_cfg->p_framebuffer_1) {
        memset(p_cfg->p_framebuffer_1, 0,
               (LVGL_DISPLAY_BUFFER_STRIDE_BYTES_INPUT * LVGL_DISPLAY_VSIZE_INPUT));
    }

    /* --- GLCDC open (rm_lvgl_port.c:115-116) ---
     * Difference from the original: open with a COPY of the display cfg in
     * which only p_callback is swapped from _rm_lvgl_port_display_callback
     * (FreeRTOS) to this module's callback. ra_gen/common_data.c is not
     * edited; the FreeRTOS callback stays linked but is never invoked. */
    s_display_cfg            = *(p_cfg->p_display_instance->p_cfg);
    s_display_cfg.p_callback = lvgl_port_mtk3_display_callback;

    error = R_GLCDC_Open(p_cfg->p_display_instance->p_ctrl, &s_display_cfg);
    FSP_ERROR_RETURN(error == FSP_SUCCESS, error);

    /* --- GLCDC start (rm_lvgl_port.c:118-123) --- */
    error = R_GLCDC_Start(p_cfg->p_display_instance->p_ctrl);
    if (error != FSP_SUCCESS) {
        R_GLCDC_Close(p_cfg->p_display_instance->p_ctrl);
        FSP_ERROR_RETURN(error == FSP_SUCCESS, error);
    }

    /* --- Display the inactive buffer if double-buffering is used
     *     (rm_lvgl_port.c:126-140) --- */
    if (NULL != p_cfg->p_framebuffer_1) {
        do {
            error = R_GLCDC_BufferChange(p_cfg->p_display_instance->p_ctrl,
                                         p_cfg->p_framebuffer_1,
                                         p_cfg->inherit_frame_layer);
        } while (FSP_ERR_INVALID_UPDATE_TIMING == error);

        if (error != FSP_SUCCESS) {
            R_GLCDC_Close(p_cfg->p_display_instance->p_ctrl);
            FSP_ERROR_RETURN(error == FSP_SUCCESS, error);
        }
    }

    /* --- LVGL display creation (rm_lvgl_port.c:142-149) --- */
    s_lv_display = lv_display_create(LVGL_DISPLAY_HSIZE_INPUT, LVGL_DISPLAY_VSIZE_INPUT);
    if (NULL == s_lv_display) {
        R_GLCDC_Close(p_cfg->p_display_instance->p_ctrl);
        return FSP_ERR_OUT_OF_MEMORY;
    }

    /* --- Flush pipeline + double buffering (rm_lvgl_port.c:151-158) --- */
    lv_display_set_flush_cb(s_lv_display, lvgl_port_mtk3_flush_cb);
    lv_display_set_flush_wait_cb(s_lv_display, lvgl_port_mtk3_flush_wait_cb);
    lv_display_set_buffers_with_stride(s_lv_display,
                                       p_cfg->p_framebuffer_0,
                                       p_cfg->p_framebuffer_1,
                                       (LVGL_DISPLAY_BUFFER_STRIDE_BYTES_INPUT * LVGL_DISPLAY_VSIZE_INPUT),
                                       LVGL_DISPLAY_BUFFER_STRIDE_BYTES_INPUT,
                                       LV_DISPLAY_RENDER_MODE_DIRECT);

    /* --- LVGL ms tick (rm_lvgl_port.c:160-166) ---
     * Original uses SysTick when BSP_CFG_RTOS==0; this project is
     * BSP_CFG_RTOS==2 so only the lv_tick_set_cb path applies. */
#if RM_LVGL_PORT_CFG_PROVIDE_TICK_CALLBACK
    lv_tick_set_cb(lvgl_port_mtk3_tick_get_cb);
#endif

    return FSP_SUCCESS;
}

/**
 * Get the LVGL display object (replaces g_lvgl_port_ctrl.p_lv_display).
 */
lv_display_t *lvgl_port_mtk3_get_display(void)
{
    return s_lv_display;
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * GLCDC display callback (ISR context).
 *
 * Replaces _rm_lvgl_port_display_callback (rm_lvgl_port.c:184-214):
 *   - DISPLAY_EVENT_LINE_DETECTION (Vsync): release the Vsync semaphore.
 *     Original: xSemaphoreGiveFromISR + portYIELD_FROM_ISR.
 *     uT-Kernel: tk_sig_sem is a notification call allowed from interrupt
 *     handlers (migration guide 5.1); dispatch is deferred to interrupt
 *     exit, so no explicit yield is needed. E_QOVR (semaphore already full,
 *     maxsem=1) is ignored - same saturation as a binary semaphore Give.
 *   - Performs the same event conversion (VPOS / UNDERFLOW) and forwards to
 *     the user callback registered in g_lvgl_port_cfg.p_callback
 *     (= lvgl_glcdc_callback in glcdc_port.c, which counts Vsync/underflow
 *     for the NT-Shell "display" command). Difference from the original:
 *     cb_arg.event is pre-initialized to 0 so an unexpected display event
 *     never forwards an uninitialized value (the original left it
 *     uninitialized on the empty `else` branch).
 *
 * @param p_args Display interface callback arguments
 */
static void lvgl_port_mtk3_display_callback(display_callback_args_t *p_args)
{
    rm_lvgl_port_callback_args_t cb_arg;
    cb_arg.event = (rm_lvgl_port_event_t)0;

    if (DISPLAY_EVENT_LINE_DETECTION == p_args->event) {
        ER ercd = tk_sig_sem(s_vsync_semid, 1);
        (void)ercd;                          /* E_QOVR: already signaled (maxsem=1) */

        cb_arg.event = RM_LVGL_PORT_EVENT_DISPLAY_VPOS;
    }
    else if ((DISPLAY_EVENT_GR1_UNDERFLOW == p_args->event) ||
             (DISPLAY_EVENT_GR2_UNDERFLOW == p_args->event)) {
        cb_arg.event = RM_LVGL_PORT_EVENT_UNDERFLOW;
    }
    else {
        /* Other events: no conversion (same as original) */
    }

    if ((NULL != s_p_cfg) && (NULL != s_p_cfg->p_callback)) {
        cb_arg.device = RM_LVGL_PORT_DEVICE_DISPLAY;
        cb_arg.error  = FSP_SUCCESS;
        s_p_cfg->p_callback(&cb_arg);
    }
}

/**
 * LVGL flush callback.
 *
 * Replaces rm_lvgl_port_flush_cb (rm_lvgl_port.c:223-244): on the last
 * flush of a frame, clean+invalidate the D-cache for the rendered buffer
 * and schedule the Vsync-synchronized buffer swap via R_GLCDC_BufferChange
 * (retrying while the GLCDC reports FSP_ERR_INVALID_UPDATE_TIMING).
 * This callback contains no OS dependency; it is duplicated here only
 * because the module is bypassed as a whole.
 */
static void lvgl_port_mtk3_flush_cb(lv_display_t *p_lv_display, const lv_area_t *p_lv_area,
                                    uint8_t *p_px_map)
{
    fsp_err_t error;
    FSP_PARAMETER_NOT_USED(p_lv_area);

    if (lv_display_flush_is_last(p_lv_display)) {
#if BSP_CFG_DCACHE_ENABLED
        SCB_CleanInvalidateDCache_by_Addr(p_px_map,
                                          (LVGL_DISPLAY_BUFFER_STRIDE_BYTES_INPUT * LVGL_DISPLAY_VSIZE_INPUT));
#endif
        do {
            error = R_GLCDC_BufferChange(s_p_cfg->p_display_instance->p_ctrl,
                                         p_px_map,
                                         s_p_cfg->inherit_frame_layer);
        } while (FSP_ERR_INVALID_UPDATE_TIMING == error);
    }
    else {
        /* Not the last area: nothing to do (same as original) */
    }
}

/**
 * LVGL flush wait callback.
 *
 * Replaces rm_lvgl_port_flush_wait_cb (rm_lvgl_port.c:250-269). Same
 * two-step take as the original:
 *   1. non-blocking take (TMO_POL) to clear a Vsync that already fired
 *      (stale signal from a previous frame)
 *   2. blocking take (TMO_FEVR) to wait for the next Vsync, at which point
 *      the GLCDC has latched the new buffer address (tearing-free swap)
 * The Vsync wake-up is event-driven (tk_sig_sem from the ISR), so it is NOT
 * quantized by the uT-Kernel timer period (CNF_TIMER_PERIOD).
 */
static void lvgl_port_mtk3_flush_wait_cb(lv_display_t *p_lv_display)
{
    if (lv_display_flush_is_last(p_lv_display)) {
        (void)tk_wai_sem(s_vsync_semid, 1, TMO_POL);    /* clear if already set (E_TMOUT ok) */

        ER ercd = tk_wai_sem(s_vsync_semid, 1, TMO_FEVR);
        if (ercd != E_OK) {
            LV_LOG_ERROR("vsync tk_wai_sem failed: %d", (int)ercd);
        }
    }
    else {
        /* Not the last area: nothing to do (same as original) */
    }
}

#if RM_LVGL_PORT_CFG_PROVIDE_TICK_CALLBACK

/**
 * LVGL millisecond tick callback.
 *
 * Replaces rm_lvgl_port_tick_get_cb (rm_lvgl_port.c:276-285).
 * Original: pdTICKS_TO_MS(xTaskGetTickCount()).
 * uT-Kernel: tk_get_otm() returns the operating time in ms (SYSTIM is a
 * 64-bit hi/lo pair; the lower 32 bits are sufficient for LVGL's wrapping
 * uint32 tick). Same technique as camera_framebuffer.c (R-005).
 */
static uint32_t lvgl_port_mtk3_tick_get_cb(void)
{
    SYSTIM tim = {0, 0};
    (void)tk_get_otm(&tim);
    return (uint32_t)tim.lo;
}

#endif /* RM_LVGL_PORT_CFG_PROVIDE_TICK_CALLBACK */
