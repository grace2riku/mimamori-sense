/**
 * @file d1_heap_mtkernel.h
 * @brief D/AVE 2D (d1 driver) custom heap on a uT-Kernel 3.0 variable size
 *        memory pool (R-xxx / Issue #186, Issue #178)
 *
 * See src/d1_heap_mtkernel.c for the design rationale, the pool sizing
 * evidence and the initialization ordering requirements.
 */

#ifndef D1_HEAP_MTKERNEL_H
#define D1_HEAP_MTKERNEL_H

#include <stdint.h>
#include <tk/tkernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Runtime statistics of the d1 heap (reported by the NT-Shell
 * "dave2d status" command so that D1_HEAP_SIZE can be right-sized on
 * real hardware).
 */
typedef struct
{
    uint32_t pool_size;                /**< Total pool size in bytes (0 = pool not created) */
    uint32_t free_now;                 /**< Current total free bytes (tk_ref_mpl frsz) */
    uint32_t max_contiguous_free;      /**< Largest contiguous free block (tk_ref_mpl maxsz) */
    uint32_t peak_used;                /**< Peak of (pool_size - frsz), incl. per-block headers */
    uint32_t alloc_calls;              /**< d1_malloc() call count */
    uint32_t free_calls;               /**< d1_free() call count */
    uint32_t alloc_fail;               /**< d1_malloc() calls that returned NULL */
} d1_heap_stats_t;

/**
 * Creates the d1 heap memory pool.
 *
 * MUST be called from task context (tk_cre_mpl performs CHECK_DISPATCH())
 * and BEFORE any code that can reach d1_allocmem() - i.e. before the LVGL
 * task is started, because lv_init() opens the Dave2D device.
 *
 * @retval E_OK      Pool created (or custom malloc disabled -> nothing to do,
 *                   or the pool already exists: the call is idempotent).
 * @retval < E_OK    tk_cre_mpl() error code.
 */
ER d1_heap_init(void);

/**
 * Copies the current d1 heap statistics into @p p_stats.
 *
 * @retval E_OK      Statistics stored.
 * @retval E_PAR     @p p_stats is NULL.
 * @retval E_OBJ     The pool has not been created (custom malloc disabled or
 *                   d1_heap_init() not called / failed).
 * @retval < E_OK    tk_ref_mpl() error code.
 */
ER d1_heap_get_stats(d1_heap_stats_t * p_stats);

#ifdef __cplusplus
}
#endif

#endif /* D1_HEAP_MTKERNEL_H */
