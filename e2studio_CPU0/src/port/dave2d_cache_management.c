/**
 * @file dave2d_cache_management.c
 * @brief Dave2D (D/AVE 2D) data cache maintenance overrides
 *
 * @details
 * Overrides the FSP weak functions d1_cacheflush() / d1_cacheblockflush()
 * defined in ra/fsp/src/r_drw/r_drw_memory.c.
 *
 * The FSP default implementations are no-ops ("RA devices do not have
 * cache memory"), but the RA8P1 Cortex-M85 core HAS a data cache.
 * When the data cache is enabled (BSP_CFG_DCACHE_ENABLED=1), the CPU may
 * hold rendered pixel data or Dave2D display lists in the dcache, while
 * the Dave2D hardware engine reads/writes physical memory directly.
 * Without explicit cache cleaning, this causes rendering corruption
 * (stale or missing pixels on screen).
 *
 * These overrides clean and invalidate the dcache before Dave2D hardware
 * access, ensuring memory coherency between the CPU and the GPU.
 *
 * Current project status:
 *   - BSP_CFG_DCACHE_ENABLED is 0 (dcache disabled), so these functions
 *     currently compile to the same no-op behavior as the FSP defaults.
 *   - This file is ported proactively so that enabling the dcache in the
 *     future (a known performance improvement, see reference project
 *     commit 795273d "enable dcache, improve performance") does not
 *     introduce Dave2D rendering corruption.
 *
 * Reference:
 *   - reference_projects/lv_port_renesas_ek_ra8p1/src/dave2d_cache_management.c
 *     (added in upstream commit 4a26c79 / merge 6ab5aa5)
 *   - FSP weak defaults: e2studio_CPU0/ra/fsp/src/r_drw/r_drw_memory.c
 *
 * Issue: #138
 */

#include "hal_data.h"
#include "r_drw_base.h"

#include "r_drw_cfg.h"

#if (BSP_CFG_RTOS == 2)                // FreeRTOS
 #include "FreeRTOS.h"
#endif

/* Override the default weak functions in ra/fsp/src/r_drw/r_drw_memory.c */

/*******************************************************************************************************************//**
 * Flush (clean + invalidate) the entire CPU data cache so that the Dave2D
 * hardware engine sees coherent memory contents.
 *
 * @param[in] handle    Pointer to the d1_device object (not used).
 * @param[in] memtype   Memory pools to flush (not used).
 * @retval    1         The function always returns 1.
 **********************************************************************************************************************/
d1_int_t d1_cacheflush (d1_device * handle, d1_int_t memtype)
{
    FSP_PARAMETER_NOT_USED(handle);
    FSP_PARAMETER_NOT_USED(memtype);

#if defined(RENESAS_CORTEX_M85) && (BSP_CFG_DCACHE_ENABLED)
    SCB_CleanInvalidateDCache();
#endif

    return 1;
}

/*******************************************************************************************************************//**
 * Flush (clean + invalidate) the CPU data cache for a specific address range
 * so that the Dave2D hardware engine sees coherent memory contents.
 *
 * @param[in] handle    Pointer to the d1_device object (not used).
 * @param[in] memtype   Memory pools to flush (not used).
 * @param[in] ptr       Start address of memory to be flushed.
 * @param[in] size      Size of memory to be flushed.
 * @retval    1         The function always returns 1.
 **********************************************************************************************************************/
d1_int_t d1_cacheblockflush (d1_device * handle, d1_int_t memtype, const void * ptr, d1_uint_t size)
{
    FSP_PARAMETER_NOT_USED(handle);
    FSP_PARAMETER_NOT_USED(memtype);

#if defined(RENESAS_CORTEX_M85) && (BSP_CFG_DCACHE_ENABLED)
    SCB_CleanInvalidateDCache_by_Addr((void *)ptr, (int32_t)size);
#else
    FSP_PARAMETER_NOT_USED(ptr);
    FSP_PARAMETER_NOT_USED(size);
#endif

    return 1;
}
