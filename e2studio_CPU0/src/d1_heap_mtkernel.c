/**
 * @file d1_heap_mtkernel.c
 * @brief D/AVE 2D (d1 driver) custom heap on a uT-Kernel 3.0 variable size
 *        memory pool (Issue #186 Step 1 / Issue #178)
 * @details
 * ## Why this file exists
 *
 * `ra/fsp/src/r_drw/r_drw_memory.c` picks the D2 heap backend at COMPILE time
 * (it is an `#if` chain, not a runtime branch):
 *
 * ```
 * r_drw_memory.c:59  #if DRW_CFG_CUSTOM_MALLOC        -> d1_malloc()  / d1_free()
 * r_drw_memory.c:63  #elif (BSP_CFG_RTOS == 2)        -> pvPortMalloc()/vPortFree()
 * r_drw_memory.c:73  #else                            -> malloc()     / free()
 * ```
 *
 * With `BSP_CFG_RTOS == 2` (the FreeRTOS setting is intentionally kept, see
 * doc/migration/mtk3-migration-guide.md ch.1) the FreeRTOS branch is selected,
 * which is the ONLY remaining user of the FreeRTOS heap: `ucHeap` costs
 * 262,144 B of RAM (`ra_cfg/aws/FreeRTOSConfig.h`, `configTOTAL_HEAP_SIZE`).
 * Worse, under boot method A the FreeRTOS scheduler never runs
 * (`src/hal_warmstart.c:157` calls `knl_start_mtkernel()` and does not return),
 * so `pvPortMalloc()`'s mutual exclusion - `vTaskSuspendAll()` /
 * `xTaskResumeAll()` - is a no-op with respect to the uT-Kernel scheduler:
 * two uT-Kernel tasks (LVGL `lvgl_task` and the LVGL draw threads
 * `dave2d`/`swdraw`, see src/lv_os_mtkernel.c) can corrupt the heap 4 free
 * list. That is Issue #178.
 *
 * Selecting `DRW_CFG_CUSTOM_MALLOC` and backing it with a uT-Kernel variable
 * size memory pool fixes both: the FreeRTOS heap is no longer referenced, and
 * `tk_get_mpl()` / `tk_rel_mpl()` serialize through the kernel's own critical
 * section (`mtk3_bsp2/mtkernel/kernel/tkernel/mempool.c:423,468`).
 *
 * `DRW_CFG_CUSTOM_MALLOC` is FSP-generated (`ra_cfg/fsp_cfg/r_drw_cfg.h`,
 * "generated configuration header file - do not edit") and therefore cannot be
 * flipped from user code or with a `-D` command line define (the header would
 * redefine it). It is switched in the FSP configurator; see
 * doc/migration/mtk3-migration-guide.md ch.10.2 for the GUI steps.
 * Until it is switched, this whole file compiles to a pair of stubs so that the
 * build keeps working unchanged.
 *
 * ## Symbols the FSP expects when DRW_CFG_CUSTOM_MALLOC is enabled
 *
 * Exactly two, declared at `ra/fsp/src/r_drw/r_drw_memory.c:34-35`:
 *   - `void * d1_malloc(size_t size)`   (used at r_drw_memory.c:62)
 *   - `void   d1_free(void * ptr)`      (used at r_drw_memory.c:90)
 * No other symbol in r_drw_memory.c is guarded by `DRW_CFG_CUSTOM_MALLOC`
 * (verified by reading the whole file), so this file covers the full contract.
 *
 * ## Pool sizing evidence (D1_HEAP_SIZE)
 *
 * Allocation ledger of the Dave2D bring-up path
 * (`lvgl_task` -> `lv_init()` -> `lv_draw_dave2d_init()` -> `lv_dave2d_init()`,
 * ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:541-602).
 * Struct sizes were measured by compiling a `sizeof` probe with the project's
 * exact ATfE flags (`-mcpu=cortex-m85 -fshort-enums`):
 * `d2_devicedata`=468, `d2_contextdata`=380, `d2_contextdata_backup`=112,
 * `d2_rbuffer`=88, `d2_dlist_block`=20, `d2_dlist_entry`=20,
 * `d2_dlist_scratch_entry`=8, `D2_DLISTBLOCKSIZE`=51, `D2_DLISTSCRATCH`=64,
 * `DLIST_ADRESSES_NUMBER`=16.
 * Pool overhead per block: the request is rounded up to `ROUNDSZ`=8 and one
 * 8-byte `QUEUE` area header is inserted in front of it
 * (mtk3_bsp2/mtkernel/kernel/tkernel/memory.h:53-54,91).
 *
 * | # | call site                                            | request | pool cost |
 * |---|------------------------------------------------------|--------:|----------:|
 * | 1 | d2_opendevice: device       (dave_driver.c:237)      |     468 |       480 |
 * | 2 | d2_opendevice: ctxdef       (dave_driver.c:251)      |     380 |       392 |
 * | 3 | d2_opendevice: blitctx_b    (dave_driver.c:258)      |     112 |       120 |
 * | 4 | d2_inithw: renderbuffer[0]  (dave_driver.c:500)      |         |     1,752 |
 * | 5 | d2_inithw: renderbuffer[1]  (dave_driver.c:501)      |         |     1,752 |
 * | 6 | d2_inithw: dlscratch_base   (dave_driver.c:516)      |     512 |       520 |
 * | 7 | lv_dave2d_init: _blit_renderbuffer(20,20)            |         |     1,128 |
 * | 8 | lv_dave2d_init: _renderbuffer(20,20)                 |         |     1,128 |
 * |   | **total**                                            |         | **7,272** |
 *
 * (A renderbuffer costs `d2_rbuffer` 88 + `d2_dlist_block` 20 +
 *  `initialsize * 20` dlist bytes + `DLIST_ADRESSES_NUMBER * 4` = 64 +
 *  `D2_DLISTSCRATCH * 8` = 512, each padded/headered as above:
 *  initialsize 51 -> 1,752 B, initialsize 20 -> 1,128 B.)
 *
 * On top of that the display lists grow on demand and shrink slowly
 * (`d2_growdlist_intern`, dave_dlist.c:154; a page is released only after 60
 * `d2_executerenderbuffer()` calls without use). LVGL executes and re-selects
 * the render buffer once per draw task (e.g. lv_draw_dave2d_fill.c:41,296), so
 * the depth of one list is bounded by a single primitive and only a handful of
 * 20-entry pages (20*20+20 -> 448 B of pool each) are ever live. The scratch
 * layer doubles on overflow (`d2_resizerblayer_intern`, dave_rbuffer.c:1201).
 *
 * 32,768 B therefore gives ~4.5x headroom over the measured 7,272 B bring-up
 * footprint (~25.5 KB, i.e. ~57 additional 448 B dlist pages) while returning
 * 262,144 - 32,768 = 229,376 B of RAM compared to `ucHeap`.
 * `dave2d status` prints the peak usage so the value can be re-tuned against a
 * real measurement - it is a single `#define`.
 *
 * ## Initialization order
 *
 * `d1_heap_init()` is called from `usermain()` (src/usermain.c) BEFORE any
 * task is created. The first (and only) path that can reach `d1_allocmem()` is
 * `lvgl_task` -> `lv_init()` (src/lvgl_thread_entry.c:127) -> Dave2D device
 * open, and `lvgl_task` is created later in the same function, so the pool
 * always exists before the first allocation.
 * The Dave2D interrupt handler does not allocate: `drw_int_isr()`
 * (src/port/r_drw_irq_mtk3.c) only touches R_DRW registers and `tk_sig_sem()`,
 * so `tk_get_mpl()` / `tk_rel_mpl()` (both do `CHECK_DISPATCH()`,
 * mempool.c:418,464) are never reached from interrupt context.
 *
 * ## Timeout policy
 *
 * `tk_get_mpl()` is called with `TMO_POL` (no wait). The FreeRTOS backend it
 * replaces (`pvPortMalloc`) never blocks either, and the d1 contract is
 * "return NULL on failure" (r_drw_memory.c:54-55). Blocking would deadlock
 * whenever the only task that could free memory is the caller itself.
 *
 * Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stddef.h>
#include <stdint.h>

#include <tk/tkernel.h>

/* FSP generated D/AVE 2D configuration - provides DRW_CFG_CUSTOM_MALLOC. */
#include "r_drw_cfg.h"

#include "d1_heap_mtkernel.h"

#if DRW_CFG_CUSTOM_MALLOC

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * Size of the D2 heap in bytes.
 *
 * Must be a multiple of sizeof(QUEUE) == 8 because TA_USERBUF rejects a size
 * that is changed by roundSize() (mtk3_bsp2/mtkernel/kernel/tkernel/mempool.c:269-274).
 * See the sizing evidence in the file header before changing this.
 */
#ifndef D1_HEAP_SIZE
 #define D1_HEAP_SIZE    (32768U)
#endif

/**********************************************************************************************************************
 Private global variables
 *********************************************************************************************************************/

/** Backing store of the memory pool (TA_USERBUF). Plain .bss, exactly like the
 * FreeRTOS `ucHeap` it replaces. 32-byte aligned (cache line) - the kernel only
 * requires 8. */
static uint8_t s_d1_heap[D1_HEAP_SIZE] __attribute__((aligned(32)));

/** Memory pool ID. 0 = not created. */
static ID s_d1_mplid = 0;

/** Statistics counters (see d1_heap_stats_t). */
static uint32_t s_peak_used   = 0;
static uint32_t s_alloc_calls = 0;
static uint32_t s_free_calls  = 0;
static uint32_t s_alloc_fail  = 0;

/**********************************************************************************************************************
 Private functions
 *********************************************************************************************************************/

/**
 * Samples the pool occupancy and updates the peak.
 *
 * Called only after a SUCCESSFUL allocation: the maximum of "used bytes" can
 * only be reached immediately after a block has been handed out, so this is
 * sufficient to record the true peak while keeping tk_ref_mpl() (which walks
 * the free queue inside a kernel critical section) off the free path.
 */
static void d1_heap_update_peak(void)
{
    T_RMPL rmpl;

    if (E_OK == tk_ref_mpl(s_d1_mplid, &rmpl))
    {
        uint32_t used = (uint32_t) D1_HEAP_SIZE - (uint32_t) rmpl.frsz;

        if (used > s_peak_used)
        {
            s_peak_used = used;
        }
    }
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Allocates a block from the D2 heap.
 * Replaces pvPortMalloc() for r_drw_memory.c:62.
 *
 * @param[in] size  Requested size in bytes.
 * @retval Non-NULL Pointer to at least @p size bytes (8-byte aligned).
 * @retval NULL     Allocation failed (pool not created, pool exhausted, or the
 *                  caller is not in a dispatchable task context).
 */
void * d1_malloc(size_t size)
{
    void * p_blk = NULL;
    ER     ercd;

    s_alloc_calls++;

    if (0 == s_d1_mplid)
    {
        /* d1_heap_init() has not run (or failed). Fail like pvPortMalloc()
         * would on an exhausted heap; the d1 driver checks for NULL. */
        s_alloc_fail++;

        return NULL;
    }

    if (0U == size)
    {
        /* tk_get_mpl() rejects blksz == 0 with E_PAR (mempool.c:416). malloc(0)
         * may legally return NULL, and the d1 driver never requests 0 bytes. */
        s_alloc_fail++;

        return NULL;
    }

    ercd = tk_get_mpl(s_d1_mplid, (SZ) size, &p_blk, TMO_POL);
    if (E_OK != ercd)
    {
        s_alloc_fail++;

        return NULL;
    }

    d1_heap_update_peak();

    return p_blk;
}

/**
 * Returns a block to the D2 heap.
 * Replaces vPortFree() for r_drw_memory.c:90.
 *
 * @param[in] ptr  Pointer previously returned by d1_malloc(). NULL is ignored
 *                 (free(NULL) semantics, same as the vPortFree() it replaces).
 *                 d2_freemem_p() can be reached with a NULL pointer, e.g.
 *                 d2_reallocmem_p(newsize == 0, oldadr == NULL) at
 *                 dave_memory.c:51.
 */
void d1_free(void * ptr)
{
    ER ercd;

    if ((NULL == ptr) || (0 == s_d1_mplid))
    {
        return;
    }

    ercd = tk_rel_mpl(s_d1_mplid, ptr);
    if (E_OK == ercd)
    {
        s_free_calls++;
    }

    /* On error nothing sensible can be done here: the d1 free API returns void
     * (r_drw_memory.c:85). The block is simply not counted as freed, which
     * shows up as a growing gap between alloc_calls and free_calls in the
     * "dave2d status" output. */
}

/**
 * Creates the D2 heap memory pool. See d1_heap_mtkernel.h.
 */
ER d1_heap_init(void)
{
    T_CMPL cmpl;
    ER     ercd;

    if (s_d1_mplid > 0)
    {
        /* Idempotent. */
        return E_OK;
    }

    cmpl.exinf  = NULL;

    /* TA_TFIFO : the pool is never waited on (TMO_POL only), so the queueing
     *            order is irrelevant; TFIFO is the cheaper of the two.
     * TA_RNG3  : same protection level as every other object created by this
     *            project (see the T_CTSK initializers in src/usermain.c).
     * TA_USERBUF: use s_d1_heap[] instead of the kernel Imalloc area, so the
     *            D2 heap budget is explicit and does not compete with task
     *            stacks. */
    cmpl.mplatr = TA_TFIFO | TA_RNG3 | TA_USERBUF;
    cmpl.mplsz  = (SZ) D1_HEAP_SIZE;
    cmpl.bufptr = (void *) s_d1_heap;

    ercd = (ER) tk_cre_mpl(&cmpl);
    if (ercd <= E_OK)
    {
        /* tk_cre_mpl() returns the ID (> 0) on success. */
        return (ercd == E_OK) ? (ER) E_SYS : ercd;
    }

    s_d1_mplid = (ID) ercd;

    return E_OK;
}

/**
 * Reads back the D2 heap statistics. See d1_heap_mtkernel.h.
 */
ER d1_heap_get_stats(d1_heap_stats_t * p_stats)
{
    T_RMPL rmpl;
    ER     ercd;

    if (NULL == p_stats)
    {
        return E_PAR;
    }

    if (0 == s_d1_mplid)
    {
        return E_OBJ;
    }

    ercd = tk_ref_mpl(s_d1_mplid, &rmpl);
    if (E_OK != ercd)
    {
        return ercd;
    }

    p_stats->pool_size           = (uint32_t) D1_HEAP_SIZE;
    p_stats->free_now            = (uint32_t) rmpl.frsz;
    p_stats->max_contiguous_free = (uint32_t) rmpl.maxsz;
    p_stats->peak_used           = s_peak_used;
    p_stats->alloc_calls         = s_alloc_calls;
    p_stats->free_calls          = s_free_calls;
    p_stats->alloc_fail          = s_alloc_fail;

    return E_OK;
}

#else /* DRW_CFG_CUSTOM_MALLOC */

/*
 * FSP "Memory Allocation" property is still "Default": r_drw_memory.c uses the
 * FreeRTOS heap (BSP_CFG_RTOS == 2) and never references d1_malloc/d1_free.
 * Provide the two API entry points as stubs so that src/usermain.c and
 * src/port/dave2d_port.c compile and link unchanged before/after the FSP
 * property is switched. No RAM is reserved in this configuration.
 */

ER d1_heap_init(void)
{
    return E_OK;
}

ER d1_heap_get_stats(d1_heap_stats_t * p_stats)
{
    (void) p_stats;

    return E_OBJ;
}

#endif /* DRW_CFG_CUSTOM_MALLOC */
