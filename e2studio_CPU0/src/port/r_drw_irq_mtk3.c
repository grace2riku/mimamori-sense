/**
 * @file r_drw_irq_mtk3.c
 * @brief uT-Kernel 3.0 replacement for ra/fsp/src/r_drw/r_drw_irq.c (R-006 / Issue #156)
 * @details
 * The FSP D/AVE 2D low-level IRQ driver (r_drw_irq.c) uses FreeRTOS
 * semaphores under `BSP_CFG_RTOS == 2`: `d1_queryirq()` blocks the calling
 * task with `xSemaphoreTake` until the display-list (dlist) completion
 * interrupt fires, and `drw_int_isr()` releases it with
 * `xSemaphoreGiveFromISR`. `d1_queryirq()` is called from INSIDE the d2
 * library (binary), so it cannot be bypassed at the user-code level.
 * Under boot method A (FreeRTOS scheduler never started) the blocking call
 * would hang/fault, therefore:
 *
 *   - `ra/fsp/src/r_drw/r_drw_irq.c` is EXCLUDED FROM BUILD
 *     (e2 studio GUI, all configurations; stored in .cproject sourceEntries,
 *     surviving "Generate Project Content" - same mechanism as the R-002
 *     mtk3_bsp2 non-target exclusions), and
 *   - this file provides uT-Kernel implementations of ALL FOUR symbols the
 *     excluded file defined. Symbol coverage of r_drw_irq.c (verified by
 *     grep of its definitions and their external references):
 *       d1_initirq_intern      <- r_drw_base.c:74  (d1_opendevice)
 *       d1_shutdownirq_intern  <- r_drw_base.c:99  (d1_closedevice; missing
 *                                                   this symbol = link error)
 *       d1_queryirq            <- d2 library (dave_d0lib / d2 driver core)
 *       drw_int_isr            <- ra_gen/vector_data.c:17 (NVIC vector [10])
 *     (the only other definition in r_drw_irq.c is the static variable
 *      g_d1_queryirq_sem/_data, file-local, replaced by s_d1_semid here)
 *
 * Everything except the OS primitives is kept IDENTICAL in logic to the
 * original r_drw_irq.c (register writes, dlist-indirect continuation,
 * status handling). If a future FSP release changes r_drw_irq.c, diff it
 * against this file and re-apply (migration guide 6.2 re-apply checklist).
 *
 * OS mapping:
 *   xSemaphoreCreateBinaryStatic -> tk_cre_sem (maxsem=1)
 *   vSemaphoreDelete             -> tk_del_sem
 *   xSemaphoreTake(timeout)      -> tk_wai_sem(id, 1, (TMO)timeout)
 *                                   (d1_to_wait_forever == -1 == TMO_FEVR;
 *                                    0 == TMO_POL; >0 is ms - the FreeRTOS
 *                                    build also passed the raw value with a
 *                                    1ms tick, so semantics are unchanged)
 *   xSemaphoreGiveFromISR + portYIELD_FROM_ISR
 *                                -> tk_sig_sem (notification call, ISR-safe;
 *                                    uT-Kernel delayed dispatch, no yield)
 *
 * Note (interrupt priority): DRW_INT_IPL = 2 (ra_gen/common_data.c). The
 * BSP2 ARMv8-M port masks interrupts with BASEPRI = INTPRI_MAX_EXTINT_PRI(1)
 * in kernel critical sections, so a priority-2 interrupt IS maskable and may
 * legally call uT-Kernel notification APIs (tk_sig_sem) from its handler.
 *
 * Original: e2studio_CPU0/ra/fsp/src/r_drw/r_drw_irq.c
 * Design reference: doc/migration/r006a-lvgl-osal-spike.md 5.5
 *
 * Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
 * SPDX-License-Identifier: BSD-3-Clause
 */

/***********************************************************************************************************************
 * Includes
 **********************************************************************************************************************/
#include <stdlib.h>

/* FSP internal header (relative include: ra/fsp/src is not on the include
 * path). Provides d1_device_flex / d1_int_t and pulls in bsp_api.h and
 * dave_base.h (d1_device, d1_to_wait_forever). */
#include "../../ra/fsp/src/r_drw/r_drw_base.h"
#include "r_drw_cfg.h"

#include <tk/tkernel.h>

/**********************************************************************************************************************
 * Macro definitions (identical to r_drw_irq.c:29-38)
 **********************************************************************************************************************/
#define DRW_PRV_IRQCTL_DLISTIRQ_ENABLE             (1U << 1)
#define DRW_PRV_IRQCTL_ENUMIRQ_CLEAR               (1U << 2)
#define DRW_PRV_IRQCTL_DLISTIRQ_CLEAR              (1U << 3)
#define DRW_PRV_IRQCTL_BUSIRQ_CLEAR                (1U << 5)
#define DRW_PRV_IRQCTL_ALLIRQ_DISABLE_AND_CLEAR    (DRW_PRV_IRQCTL_BUSIRQ_CLEAR | DRW_PRV_IRQCTL_DLISTIRQ_CLEAR | \
                                                    DRW_PRV_IRQCTL_ENUMIRQ_CLEAR)
#define DRW_PRV_IRQCTL_ALLIRQ_CLEAR_AND_DLISTIRQ_ENABLE                                           \
    (DRW_PRV_IRQCTL_BUSIRQ_CLEAR | DRW_PRV_IRQCTL_DLISTIRQ_CLEAR | DRW_PRV_IRQCTL_ENUMIRQ_CLEAR | \
     DRW_PRV_IRQCTL_DLISTIRQ_ENABLE)
#define DRW_PRV_STATUS_DLISTIRQ_TRIGGERED          (1U << 5)

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

/** dlist completion semaphore ID (replaces g_d1_queryirq_sem). 0 = not created. */
static ID s_d1_semid = 0;

/***********************************************************************************************************************
 * Extern variables (identical to r_drw_irq.c:55)
 **********************************************************************************************************************/
extern const uint8_t DRW_INT_IPL;

/***********************************************************************************************************************
 * Functions
 **********************************************************************************************************************/

void drw_int_isr(void);

/*******************************************************************************************************************//**
 * Initializes DRW_INT (replaces r_drw_irq.c d1_initirq_intern, lines 77-108).
 * Called from d1_opendevice() (r_drw_base.c:74) during d2_inithw().
 *
 * @param[in] handle    Pointer to the d1_device object.
 * @retval    0         The IRQ number is invalid or the semaphore could not be created.
 * @retval    1         DRW_INT successfully initialized.
 **********************************************************************************************************************/
d1_int_t d1_initirq_intern (d1_device_flex * handle)
{
    d1_int_t ret = 0;

    if (DRW_CFG_INT_IRQ >= 0)
    {
        /* Clear all the D/AVE 2D IRQs and enable Display list IRQ. (same as original) */
        R_DRW->IRQCTL = DRW_PRV_IRQCTL_ALLIRQ_CLEAR_AND_DLISTIRQ_ENABLE;

        /* Enable D/AVE 2D interrupt in NVIC. (same as original) */
        R_BSP_IrqCfgEnable((IRQn_Type) DRW_CFG_INT_IRQ, DRW_INT_IPL, handle);

        /* Initialize semaphore for use in d1_queryirq().
         * Original: g_d1_queryirq_sem = xSemaphoreCreateBinaryStatic(...).
         * uT-Kernel: counting semaphore with maxsem=1 (binary equivalent).
         * Idempotent: reuse the semaphore if open/close cycles re-enter. */
        if (s_d1_semid <= 0)
        {
            T_CSEM csem = {
                .exinf   = NULL,
                .sematr  = TA_TFIFO | TA_FIRST,
                .isemcnt = 0,
                .maxsem  = 1,
            };
            ID semid = tk_cre_sem(&csem);
            if (semid > 0)
            {
                s_d1_semid = semid;
            }
        }

        if (s_d1_semid > 0)
        {
            ret = 1;
        }
    }

    return ret;
}

/*******************************************************************************************************************//**
 * De-initializes DRW_INT (replaces r_drw_irq.c d1_shutdownirq_intern, lines 116-137).
 * Called from d1_closedevice() (r_drw_base.c:99) during d2_deinithw().
 * NOTE: this symbol MUST exist even though the project never closes the
 * Dave2D device at runtime - r_drw_base.c references it unconditionally and
 * the link fails without it.
 *
 * @param[in] handle    Pointer to the d1_device object.
 * @retval    1         The function returns 1.
 **********************************************************************************************************************/
d1_int_t d1_shutdownirq_intern (d1_device_flex * handle)
{
    FSP_PARAMETER_NOT_USED(handle);

    /* Disable D/AVE 2D interrupt in NVIC. (same as original) */
    NVIC_DisableIRQ((IRQn_Type) DRW_CFG_INT_IRQ);

    /* Clear all the D/AVE 2D IRQs and disable Display list IRQ. (same as original) */
    R_DRW->IRQCTL = DRW_PRV_IRQCTL_ALLIRQ_DISABLE_AND_CLEAR;

    /* Delete semaphore (original: vSemaphoreDelete) */
    if (s_d1_semid > 0)
    {
        ER ercd = tk_del_sem(s_d1_semid);
        FSP_PARAMETER_NOT_USED(ercd);   /* original ignores delete errors as well */
        s_d1_semid = 0;
    }

    return 1;
}

/*******************************************************************************************************************//**
 * Waits for DRW_INT with timeout (replaces r_drw_irq.c d1_queryirq, lines 148-195).
 * Called from inside the d2 library (dlist completion wait) - runs in the
 * LVGL "dave2d" render thread context (a uT-Kernel task).
 *
 * @param[in] handle    Pointer to the d1_device object (Not used).
 * @param[in] irqmask   Interrupt ID (Not used. FSP only uses Display list IRQ).
 * @param[in] timeout   Timeout value. d1_to_wait_forever(-1) == TMO_FEVR(-1);
 *                      0 == TMO_POL; positive values are milliseconds (the
 *                      FreeRTOS build passed the same raw value as 1ms ticks).
 * @retval    0         The wait timed out (or the semaphore is invalid).
 * @retval    1         DRW_INT was detected.
 **********************************************************************************************************************/
d1_int_t d1_queryirq (d1_device * handle, d1_int_t irqmask, d1_int_t timeout)
{
    d1_int_t ret = 1;

    FSP_PARAMETER_NOT_USED(handle);
    FSP_PARAMETER_NOT_USED(irqmask);

    /* Wait for dlist processing to complete.
     * Original: err = xSemaphoreTake(g_d1_queryirq_sem, (uint32_t) timeout); */
    ER ercd = tk_wai_sem(s_d1_semid, 1, (TMO) timeout);

    /* If the wait timed out (or failed) return 0. (same as original) */
    if (E_OK != ercd)
    {
        ret = 0;
    }

    return ret;
}

/*******************************************************************************************************************//**
 * D/AVE 2D interrupt handler (replaces r_drw_irq.c drw_int_isr, lines 204-270).
 * Registered in the NVIC vector table by ra_gen/vector_data.c:17 (same
 * symbol name, so the read-only ra_gen table resolves to this function once
 * the original file is excluded from build).
 *
 * Increments the dlist array pointer in indirect mode and indicates dlist
 * completion in both modes.
 **********************************************************************************************************************/
void drw_int_isr (void)
{
    /* Original starts with FSP_CONTEXT_SAVE / ends with FSP_CONTEXT_RESTORE.
     * Both expand to nothing unless BSP_CFG_RTOS==1 (ThreadX, bsp_common.h:57-64),
     * so they are intentionally omitted here (BSP_CFG_RTOS==2). */

    uint32_t int_status;

    /* Get current IRQ number (same as original) */
    IRQn_Type irq = R_FSP_CurrentIrqGet();

    /* Get D/AVE 2D interrupt status. (same as original) */
    int_status = R_DRW->STATUS;

    /* Clear all the D/AVE 2D interrupts (keep the Display list interrupt enable). (same as original) */
    R_DRW->IRQCTL = DRW_PRV_IRQCTL_ALLIRQ_CLEAR_AND_DLISTIRQ_ENABLE;

    /* Display list interrupt? (same as original) */
    if (int_status & DRW_PRV_STATUS_DLISTIRQ_TRIGGERED)
    {
#if DRW_CFG_USE_DLIST_INDIRECT

        /* Get handle from ISR context (same as original) */
        d1_device_flex * p_context = (d1_device_flex *) R_FSP_IsrContextGet(irq);

        /* If indirect mode is enabled and there are more display lists left then continue with the next list */
        if ((p_context->dlist_indirect_enable) && (NULL != (uint32_t *) *(p_context->pp_dlist_indirect_start)))
        {
            /* Start D/AVE 2D. (same as original) */
            R_DRW->DLISTSTART = *(p_context->pp_dlist_indirect_start);

            /* Get next display list start address. (same as original) */
            p_context->pp_dlist_indirect_start++;
        }
        else
#endif
        {
            /* Put semaphore.
             * Original: xSemaphoreGiveFromISR + portYIELD_FROM_ISR.
             * uT-Kernel: tk_sig_sem (notification call, allowed from ISR -
             * migration guide 5.1). Dispatch is deferred to interrupt exit;
             * E_QOVR (maxsem=1 saturation) is ignored. */
            (void) tk_sig_sem(s_d1_semid, 1);
        }
    }
    else
    {
        /* Do nothing. (same as original) */
    }

    /* Clear IRQ status. (same as original) */
    R_BSP_IrqStatusClear(irq);
}
