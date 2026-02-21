/**
 * @file freertos_hooks.c
 * @brief FreeRTOS application hook functions
 * @details
 * Implements callback hooks required by the FreeRTOS kernel configuration.
 * These hooks are called by the kernel when specific events occur.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include "FreeRTOS.h"

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Called by FreeRTOS when a memory allocation fails (pvPortMalloc returns NULL).
 * Requires configUSE_MALLOC_FAILED_HOOK == 1 in FreeRTOSConfig.h.
 */
void vApplicationMallocFailedHook(void)
{
    /* Halt in debug builds; in production, consider a system reset */
    configASSERT(0);
}
