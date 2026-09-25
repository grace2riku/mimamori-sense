#ifndef RTC_BOOT_DIAG_H
#define RTC_BOOT_DIAG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RTC_BOOT_RESET,
    RTC_BOOT_POST_CLOCK,
    RTC_BOOT_POST_C,
    RTC_BOOT_BEFORE_OPEN,
    RTC_BOOT_BEFORE_DECISION,
    RTC_BOOT_STAGE_COUNT
} rtc_boot_stage_t;

typedef struct {
    uint32_t rcr1, rcr2, rcr4, sosccr, somcr;
} rtc_boot_snapshot_t;

/* CPU0 only. Begin at RESET, capture the next two slots before OS startup.
 * The last two slots are written by ntshell_task under the RTC resource lock.
 * No ISR access; read only from ntshell_task after time_ctrl_init returns.
 * No RTC writes, waits, or calendar-counter reads (UM 27.6.5). */
void rtc_boot_diag_begin(void);
void rtc_boot_diag_capture(rtc_boot_stage_t stage);
bool rtc_boot_diag_get(rtc_boot_stage_t stage, rtc_boot_snapshot_t *out);

#endif
