/**
 * @file lv_os_mtkernel.h
 * @brief LVGL custom OSAL type definitions for uT-Kernel 3.0 (R-006 / Issue #156)
 * @details
 * This header is pulled into every LVGL compilation unit via
 * `LV_OS_CUSTOM_INCLUDE` (lv_os.h:40-41) when `LV_USE_OS == LV_OS_CUSTOM`.
 * Both macros are set in src/lv_conf_user.h, taking advantage of the
 * `#ifndef LV_USE_OS` guard in the FSP-generated lv_conf.h
 * (ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h:36-38) so that no FSP-managed file
 * (ra/lvgl/, ra_cfg/) needs to be edited.
 *
 * Only the three OSAL types are required here (lv_thread_t / lv_mutex_t /
 * lv_thread_sync_t); the OSAL functions are resolved at link time by
 * src/lv_os_mtkernel.c.
 *
 * LVGL zero-initializes these structures before use, and uT-Kernel object
 * IDs are always >= 1, so an ID value of 0 reliably means "not yet created"
 * (used for lazy/idempotent creation in lv_os_mtkernel.c).
 *
 * Design reference: doc/migration/r006a-lvgl-osal-spike.md 5.3
 */

#ifndef LV_OS_MTKERNEL_H
#define LV_OS_MTKERNEL_H

#include <tk/tkernel.h>

/** LVGL thread handle (backed by a uT-Kernel task) */
typedef struct {
    void (*pfn)(void *);    /* LVGL thread function (lv_thread_init callback) */
    void *arg;              /* user_data passed to the callback */
    ID   tskid;             /* uT-Kernel task ID (0 = not created) */
} lv_thread_t;

/** LVGL mutex (backed by a uT-Kernel mutex + recursion wrapper).
 *  uT-Kernel mutexes are non-recursive (double lock returns E_ILUSE), but
 *  LVGL's FreeRTOS OSAL uses recursive mutexes, so recursion is emulated
 *  with an owner/count pair (see lv_os_mtkernel.c). */
typedef struct {
    ID   mtxid;             /* uT-Kernel mutex ID (0 = not created) */
    ID   owner;             /* owning task ID for recursion (0 = not held) */
    UINT count;             /* recursive lock depth */
} lv_mutex_t;

/** LVGL thread synchronization object (backed by a uT-Kernel counting
 *  semaphore). A counting semaphore remembers signals issued before the
 *  wait, which covers the "sticky signal" semantics of the FreeRTOS OSAL
 *  implementation (lv_freertos.c). */
typedef struct {
    ID   semid;             /* uT-Kernel semaphore ID (0 = not created) */
} lv_thread_sync_t;

#endif /* LV_OS_MTKERNEL_H */
