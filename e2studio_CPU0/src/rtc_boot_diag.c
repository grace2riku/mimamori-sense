#include "rtc_boot_diag.h"
#include "bsp_api.h"

#define RTC_BOOT_COMPLETE (0x234B0071U)

typedef struct {
    rtc_boot_snapshot_t value;
    uint32_t complete;
} rtc_boot_slot_t;

/* Internal SRAM, not TCM/SDRAM. fsp_gen.lld collects .ram_noinit outside
 * the C runtime's zero/copy regions. RESET writes every valid marker before
 * reading it; no assumption about SRAM contents across power loss.
 * Volatile accesses publish the marker last. There are no concurrent readers.
 * Keep callable code in flash: RESET runs before runtime data initialization. */
static volatile rtc_boot_slot_t s_rtc_boot[RTC_BOOT_STAGE_COUNT]
    __attribute__((section(".ram_noinit"), aligned(4)));

void rtc_boot_diag_begin(void)
{
    for (unsigned int i = 0; i < RTC_BOOT_STAGE_COUNT; ++i) {
        s_rtc_boot[i].complete = 0U;
    }
    rtc_boot_diag_capture(RTC_BOOT_RESET);
}

void rtc_boot_diag_capture(rtc_boot_stage_t stage)
{
    if ((unsigned int)stage >= RTC_BOOT_STAGE_COUNT) return;
    volatile rtc_boot_slot_t *slot = &s_rtc_boot[stage];
    if (slot->complete == RTC_BOOT_COMPLETE) return;
    slot->value.rcr1 = R_RTC->RCR1;
    slot->value.rcr2 = R_RTC->RCR2;
    slot->value.rcr4 = R_RTC->RCR4;
    slot->value.sosccr = R_SYSTEM->SOSCCR;
    slot->value.somcr = R_SYSTEM->SOMCR;
    slot->complete = RTC_BOOT_COMPLETE;
}

bool rtc_boot_diag_get(rtc_boot_stage_t stage, rtc_boot_snapshot_t *out)
{
    if ((unsigned int)stage >= RTC_BOOT_STAGE_COUNT || out == NULL) return false;
    const volatile rtc_boot_slot_t *slot = &s_rtc_boot[stage];
    if (slot->complete != RTC_BOOT_COMPLETE) return false;
    out->rcr1 = slot->value.rcr1;
    out->rcr2 = slot->value.rcr2;
    out->rcr4 = slot->value.rcr4;
    out->sosccr = slot->value.sosccr;
    out->somcr = slot->value.somcr;
    return true;
}
