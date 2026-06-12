/**
 * @file lv_os_mtkernel.c
 * @brief LVGL custom OSAL implementation for uT-Kernel 3.0 (R-006 / Issue #156)
 * @details
 * Implements the LVGL OSAL API (lv_os.h, `LV_USE_OS != LV_OS_NONE`) on top of
 * uT-Kernel 3.0. This replaces the FreeRTOS OSAL (ra/lvgl/lvgl/src/osal/
 * lv_freertos.c), which compiles to nothing when `LV_USE_OS == LV_OS_CUSTOM`
 * because of its `#if LV_USE_OS == LV_OS_FREERTOS` guard - no build exclusion
 * of LVGL sources is needed.
 *
 * API mapping (FreeRTOS OSAL -> uT-Kernel 3.0, spike report 5.2):
 *   lv_thread_init        xTaskCreate                  -> tk_cre_tsk + tk_sta_tsk
 *   lv_thread_delete      vTaskDelete                  -> tk_ter_tsk + tk_del_tsk
 *   lv_mutex_*            recursive mutex              -> tk_cre_mtx(TA_INHERIT) + owner/count wrapper
 *   lv_thread_sync_*      counting semaphore (sticky)  -> tk_cre_sem(TA_CNT, maxsem=32767)
 *   lv_thread_sync_signal_isr  xSemaphoreGiveFromISR   -> tk_sig_sem (notification call,
 *                              + portYIELD_FROM_ISR       allowed from ISR; uT-Kernel uses
 *                                                         delayed dispatch, no explicit yield)
 *   lv_os_get_idle_percent     trace-hook measurement  -> lv_timer_get_idle() delegation
 *
 * Users of this OSAL inside LVGL (ra/lvgl/ - not edited):
 *   - lv_os.c           : lv_general_mutex (lv_lock/lv_unlock)
 *   - lv_draw.c         : _draw_info.sync (draw dispatch synchronization)
 *   - lv_draw_dave2d.c  : "dave2d" render thread + xd2Semaphore HW mutex
 *   - lv_draw_sw.c      : "swdraw" render thread
 *   - lv_mem_core_builtin.c / lv_cache.c : allocator / cache mutexes
 *
 * Constraints (documented in spike report 2.5 / 6):
 *   - lv_mutex_lock_isr / lv_lock_isr are NOT supported (uT-Kernel mutexes
 *     cannot be operated from an interrupt handler). Verified unused in this
 *     project (grep: no callers in src/ nor in the dave2d/sw draw paths).
 *   - lv_thread_init MUST succeed: lv_draw_dave2d.c:104 / lv_draw_sw.c:98
 *     ignore the return value and the dispatch path keeps signaling the
 *     render-thread sync (`#if LV_USE_OS` compile-time branch), so a failed
 *     thread creation results in a rendering hang, not a graceful fallback.
 *     Errors are therefore logged loudly via LV_LOG_ERROR.
 *
 * Design reference: doc/migration/r006a-lvgl-osal-spike.md 5.2 / 5.3
 */

#include "lvgl.h"               /* lv_result_t / LV_LOG_* / lv_timer_get_idle */

/* lv_os.h is NOT pulled in by the public lvgl.h (only private headers such
 * as lv_global.h / lv_draw_private.h include it), so include it explicitly
 * for the OSAL prototypes. The path resolves against the LVGL root include
 * directory (ra/lvgl/lvgl), the same base as "lvgl.h". */
#include "src/osal/lv_os.h"

#if LV_USE_OS == LV_OS_CUSTOM

#include <tk/tkernel.h>

/*--------------------------------------------------------------------------
 * Thread priority mapping (LVGL -> uT-Kernel itskpri)
 *
 * Must stay consistent with the task priority table in usermain.c /
 * spike report 5.7:
 *   blink=10, camera=11, ntshell=12, dave2d/swdraw=13 (PRIO_HIGH),
 *   lvgl_task=14 (= PRIO_MID equivalent)
 * The render threads sit one level above lvgl_task by LVGL design
 * (they sleep on their sync object most of the time).
 *--------------------------------------------------------------------------*/
static PRI prio_map(lv_thread_prio_t p)
{
    switch (p) {
        case LV_THREAD_PRIO_HIGHEST: return 12;
        case LV_THREAD_PRIO_HIGH:    return 13;   /* dave2d / swdraw render threads */
        case LV_THREAD_PRIO_MID:     return 14;   /* same level as lvgl_task */
        case LV_THREAD_PRIO_LOW:     return 15;
        case LV_THREAD_PRIO_LOWEST:
        default:                     return 16;
    }
}

/*--------------------------------------------------------------------------
 * Threads
 *--------------------------------------------------------------------------*/

/* uT-Kernel task entry trampoline: calls the LVGL thread function and
 * terminates/deletes itself if the function ever returns (LVGL render
 * threads normally loop forever). */
static void thread_entry(INT stacd, void *exinf)
{
    (void)stacd;
    lv_thread_t *t = (lv_thread_t *)exinf;
    t->pfn(t->arg);
    tk_exd_tsk();               /* exit & delete own task, releasing resources */
}

lv_result_t lv_thread_init(lv_thread_t *thread, const char *const name,
                           lv_thread_prio_t prio, void (*callback)(void *),
                           size_t stack_size, void *user_data)
{
    (void)name;                 /* USE_OBJECT_NAME=0: T_CTSK has no dsname member */

    thread->pfn = callback;
    thread->arg = user_data;

    T_CTSK ctsk = {
        .exinf   = thread,
        .tskatr  = TA_HLNG | TA_RNG3,
        .task    = thread_entry,
        .itskpri = prio_map(prio),
        .stksz   = (SZ)stack_size,   /* dave2d/swdraw: LV_DRAW_THREAD_STACK_SIZE = 0x2000 */
        .bufptr  = NULL,             /* USE_IMALLOC=1: stack auto-allocated */
    };

    ID tskid = tk_cre_tsk(&ctsk);
    if (tskid <= E_OK) {
        /* Must not fail silently: see file header (rendering would hang). */
        LV_LOG_ERROR("tk_cre_tsk failed: %d", (int)tskid);
        return LV_RESULT_INVALID;
    }

    ER ercd = tk_sta_tsk(tskid, 0);
    if (ercd != E_OK) {
        LV_LOG_ERROR("tk_sta_tsk failed: %d", (int)ercd);
        (void)tk_del_tsk(tskid);
        return LV_RESULT_INVALID;
    }

    thread->tskid = tskid;
    return LV_RESULT_OK;
}

lv_result_t lv_thread_delete(lv_thread_t *thread)
{
    /* Not used in steady state by this project (render threads run forever).
     * Provided for completeness: force-terminate then delete. */
    if (thread->tskid <= 0) {
        return LV_RESULT_INVALID;
    }

    ER ercd = tk_ter_tsk(thread->tskid);
    if ((ercd != E_OK) && (ercd != E_OBJ)) {   /* E_OBJ: already dormant */
        LV_LOG_ERROR("tk_ter_tsk failed: %d", (int)ercd);
        return LV_RESULT_INVALID;
    }

    ercd = tk_del_tsk(thread->tskid);
    if (ercd != E_OK) {
        LV_LOG_ERROR("tk_del_tsk failed: %d", (int)ercd);
        return LV_RESULT_INVALID;
    }

    thread->tskid = 0;
    return LV_RESULT_OK;
}

/*--------------------------------------------------------------------------
 * Mutexes (recursive wrapper around non-recursive uT-Kernel mutex)
 *
 * TA_INHERIT (priority inheritance) avoids priority inversion between the
 * render threads (itskpri=13) / lvgl_task (14) and the NT-Shell task (12)
 * which takes lv_lock() in the `lvgl` shell commands (usrcmd.c).
 *
 * Lazy creation: LVGL calls lv_mutex_init() for some mutexes (lv_os_init,
 * cache) but other code paths may lock a zero-initialized mutex first, so
 * check_mutex_init() is also called from lv_mutex_lock(). Creation is
 * task-context only (LVGL never creates mutexes from ISRs), so dispatch
 * disable is sufficient to serialize concurrent creation between tasks.
 *--------------------------------------------------------------------------*/
static lv_result_t check_mutex_init(lv_mutex_t *m)
{
    if (m->mtxid > 0) {
        return LV_RESULT_OK;
    }

    ER ercd = tk_dis_dsp();
    if (ercd != E_OK) {
        LV_LOG_ERROR("tk_dis_dsp failed: %d", (int)ercd);
        return LV_RESULT_INVALID;
    }

    if (m->mtxid <= 0) {        /* re-check under dispatch disable */
        T_CMTX cmtx = {
            .exinf  = NULL,
            .mtxatr = TA_INHERIT,
        };
        ID mtxid = tk_cre_mtx(&cmtx);
        if (mtxid > 0) {
            m->owner = 0;
            m->count = 0;
            m->mtxid = mtxid;
        } else {
            LV_LOG_ERROR("tk_cre_mtx failed: %d", (int)mtxid);
        }
    }

    (void)tk_ena_dsp();

    return (m->mtxid > 0) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

lv_result_t lv_mutex_init(lv_mutex_t *mutex)
{
    return check_mutex_init(mutex);
}

lv_result_t lv_mutex_lock(lv_mutex_t *mutex)
{
    if (check_mutex_init(mutex) != LV_RESULT_OK) {
        return LV_RESULT_INVALID;
    }

    ID self = tk_get_tid();
    if (mutex->owner == self) {
        /* Recursive lock by the owner: just bump the depth. Safe without
         * extra locking because only the owner task reaches this branch. */
        mutex->count++;
        return LV_RESULT_OK;
    }

    ER ercd = tk_loc_mtx(mutex->mtxid, TMO_FEVR);
    if (ercd != E_OK) {
        LV_LOG_ERROR("tk_loc_mtx failed: %d", (int)ercd);
        return LV_RESULT_INVALID;
    }

    mutex->owner = self;
    mutex->count = 1;
    return LV_RESULT_OK;
}

lv_result_t lv_mutex_lock_isr(lv_mutex_t *mutex)
{
    (void)mutex;
    /* uT-Kernel mutexes cannot be operated from an interrupt handler.
     * Verified unused in this project (spike report 2.5). */
    LV_LOG_ERROR("lv_mutex_lock_isr is not supported on uT-Kernel");
    return LV_RESULT_INVALID;
}

lv_result_t lv_mutex_unlock(lv_mutex_t *mutex)
{
    if ((mutex->mtxid <= 0) || (mutex->owner != tk_get_tid())) {
        LV_LOG_ERROR("lv_mutex_unlock by non-owner");
        return LV_RESULT_INVALID;
    }

    if (mutex->count > 1) {
        mutex->count--;         /* unwinding a recursive lock */
        return LV_RESULT_OK;
    }

    /* Clear ownership BEFORE unlocking: after tk_unl_mtx another task may
     * immediately acquire the kernel mutex and write owner/count. */
    mutex->owner = 0;
    mutex->count = 0;

    ER ercd = tk_unl_mtx(mutex->mtxid);
    if (ercd != E_OK) {
        LV_LOG_ERROR("tk_unl_mtx failed: %d", (int)ercd);
        return LV_RESULT_INVALID;
    }
    return LV_RESULT_OK;
}

lv_result_t lv_mutex_delete(lv_mutex_t *mutex)
{
    if (mutex->mtxid <= 0) {
        return LV_RESULT_INVALID;
    }

    ER ercd = tk_del_mtx(mutex->mtxid);
    if (ercd != E_OK) {
        LV_LOG_ERROR("tk_del_mtx failed: %d", (int)ercd);
        return LV_RESULT_INVALID;
    }

    mutex->mtxid = 0;
    mutex->owner = 0;
    mutex->count = 0;
    return LV_RESULT_OK;
}

/*--------------------------------------------------------------------------
 * Thread synchronization (counting semaphore)
 *
 * The FreeRTOS OSAL implements this with a counting-semaphore-like
 * "sticky signal" (a signal issued before the wait is remembered). A plain
 * uT-Kernel counting semaphore has exactly this property, so the mapping
 * is direct: signal = tk_sig_sem(+1), wait = tk_wai_sem(-1).
 *--------------------------------------------------------------------------*/
lv_result_t lv_thread_sync_init(lv_thread_sync_t *sync)
{
    if (sync->semid > 0) {
        return LV_RESULT_OK;
    }

    T_CSEM csem = {
        .exinf   = NULL,
        .sematr  = TA_TFIFO | TA_CNT,
        .isemcnt = 0,
        .maxsem  = 32767,
    };

    ID semid = tk_cre_sem(&csem);
    if (semid <= E_OK) {
        LV_LOG_ERROR("tk_cre_sem failed: %d", (int)semid);
        return LV_RESULT_INVALID;
    }

    sync->semid = semid;
    return LV_RESULT_OK;
}

lv_result_t lv_thread_sync_wait(lv_thread_sync_t *sync)
{
    ER ercd = tk_wai_sem(sync->semid, 1, TMO_FEVR);
    if (ercd != E_OK) {
        LV_LOG_ERROR("tk_wai_sem failed: %d", (int)ercd);
        return LV_RESULT_INVALID;
    }
    return LV_RESULT_OK;
}

lv_result_t lv_thread_sync_signal(lv_thread_sync_t *sync)
{
    ER ercd = tk_sig_sem(sync->semid, 1);
    /* E_QOVR (count saturated at maxsem) means the signal is already
     * remembered - treat as success, same as the binary-semaphore Give
     * semantics in the FreeRTOS implementation. */
    if ((ercd != E_OK) && (ercd != E_QOVR)) {
        LV_LOG_ERROR("tk_sig_sem failed: %d", (int)ercd);
        return LV_RESULT_INVALID;
    }
    return LV_RESULT_OK;
}

lv_result_t lv_thread_sync_signal_isr(lv_thread_sync_t *sync)
{
    /* tk_sig_sem is a notification call and may be issued from an interrupt
     * handler (migration guide 5.1). uT-Kernel performs delayed dispatch at
     * interrupt exit, so no portYIELD_FROM_ISR equivalent is needed. */
    ER ercd = tk_sig_sem(sync->semid, 1);
    if ((ercd != E_OK) && (ercd != E_QOVR)) {
        return LV_RESULT_INVALID;   /* no logging in ISR context */
    }
    return LV_RESULT_OK;
}

lv_result_t lv_thread_sync_delete(lv_thread_sync_t *sync)
{
    if (sync->semid <= 0) {
        return LV_RESULT_INVALID;
    }

    ER ercd = tk_del_sem(sync->semid);
    if (ercd != E_OK) {
        LV_LOG_ERROR("tk_del_sem failed: %d", (int)ercd);
        return LV_RESULT_INVALID;
    }

    sync->semid = 0;
    return LV_RESULT_OK;
}

/*--------------------------------------------------------------------------
 * SYSMON idle percentage (default resolution target of LV_SYSMON_GET_IDLE)
 *
 * The FreeRTOS OSAL measured idle time with task-switch trace hooks
 * (traceTASK_SWITCHED_IN/OUT in User_FreeRTOSConfig.h - removed in R-006).
 * Here we delegate to LVGL's own timer-based idle tracking, which is
 * coarser ("idle inside the LVGL task" rather than whole-CPU idle) but
 * sufficient for the on-screen PERF_MONITOR. KPI measurements use the FPS
 * counter (PERF_MONITOR) and the inference timer (R-007), so this
 * precision loss does not affect KPI verification (spike report 6).
 *--------------------------------------------------------------------------*/
uint32_t lv_os_get_idle_percent(void)
{
    return lv_timer_get_idle();
}

#endif /* LV_USE_OS == LV_OS_CUSTOM */
