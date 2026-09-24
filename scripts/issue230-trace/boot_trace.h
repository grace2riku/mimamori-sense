/* Issue #230 diagnostic overlay only; never installed into generated FSP code. */
#ifndef ISSUE230_BOOT_TRACE_H
#define ISSUE230_BOOT_TRACE_H
#include "hal_data.h"
#include <stdint.h>

enum boot_trace_point {
    BT_POST_C, BT_SDRAM_BEFORE, BT_SDRAM_AFTER, BT_OS_BEFORE,
    BT_DISPLAY_BEFORE, BT_RESET_BEFORE, BT_RESET_AFTER,
    BT_GLCDC_OPEN, BT_GLCDC_START, BT_DISPLAY_AFTER, BT_COUNT
};

/* POST_CLOCK is before C initialization: registers only, no RAM/OS calls.
 * Existing camera/AI code uses the same DWT enable sequence. */
static inline void boot_trace_clock_start(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL &= ~DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void boot_trace_post_c(uint32_t cycles);
void boot_trace_cycles(enum boot_trace_point point, int32_t result);
void boot_trace_os(enum boot_trace_point point, int32_t result);
int usrcmd_boottrace(int argc, char **argv);
#endif
