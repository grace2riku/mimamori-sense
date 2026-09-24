/* Diagnostic overlay only. Task writers/readers; no ISR/CPU1 writes.
 * Sequence, timestamp, and publish share one dispatch exclusion interval.
 * No device API, UART, or hardware wait is called in that interval. */
#include "boot_trace.h"
#include "jlink_console.h"
#include <tk/tkernel.h>
#include <stdio.h>

typedef struct {
    uint32_t seq, lo, hi, cycles, epoch, ctrl;
    int32_t result;
    uint32_t aux, extra;
} trace_slot_t;
typedef struct {
    uint32_t version, hz, initial_cycles, initial_ctrl, probe;
    uint32_t next_seq, epoch, fault;
    trace_slot_t slot[BT_COUNT];
} trace_record_t;
static volatile trace_record_t s_boot_trace __attribute__((aligned(32)));
typedef char trace_size_check[(sizeof(trace_record_t) <= 1536U) ? 1 : -1];

void boot_trace_post_c(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk; /* Do not reset the running counter. */
    s_boot_trace.version = 23201U;
    s_boot_trace.hz = SystemCoreClock;
    s_boot_trace.initial_ctrl = DWT->CTRL;
    s_boot_trace.initial_cycles = DWT->CYCCNT;
    uint32_t a = DWT->CYCCNT;
    __asm volatile("nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop" ::: "memory");
    s_boot_trace.probe = DWT->CYCCNT - a;
}

/* Caller holds dispatch exclusion; only fixed stores and nonblocking clock read. */
static void trace_locked(enum boot_trace_point point, int32_t result,
                         uint32_t aux, uint32_t extra)
{
    volatile trace_slot_t *p = &s_boot_trace.slot[point];
    if (p->seq != 0U) { return; }
    SYSTIM now;
    if (tk_get_otm(&now) != E_OK) { s_boot_trace.fault = 1U; return; }
    p->cycles = DWT->CYCCNT;
    p->ctrl = DWT->CTRL;
    p->epoch = s_boot_trace.epoch;
    p->lo = now.lo;
    p->hi = (uint32_t)now.hi;
    p->result = result;
    p->aux = aux;
    p->extra = extra;
    uint32_t seq = s_boot_trace.next_seq + 1U;
    s_boot_trace.next_seq = seq;
    __asm volatile("" ::: "memory");
    p->seq = seq; /* publish last */
}

void boot_trace_mark(enum boot_trace_point point, int32_t result, uint32_t aux)
{
    if ((unsigned int)point >= BT_COUNT) { return; }
    if (s_boot_trace.slot[point].seq != 0U) { return; }
    if (tk_dis_dsp() != E_OK) { s_boot_trace.fault = 1U; return; }
    trace_locked(point, result, aux, 0U);
    (void)tk_ena_dsp();
}

void boot_trace_branch(bool provisioned)
{
    if (tk_dis_dsp() != E_OK) { s_boot_trace.fault = 1U; return; }
    /* Snapshot AFTER the actual branch predicate. Not a pre-BSP snapshot. */
    uint32_t rtc = (uint32_t)R_RTC->RCR1 | ((uint32_t)R_RTC->RCR2 << 8U) |
                   ((uint32_t)R_RTC->RCR4 << 16U);
    uint32_t clocks = (uint32_t)R_SYSTEM->SOSCCR | ((uint32_t)R_SYSTEM->SOMCR << 8U);
    trace_locked(BT_BRANCH, provisioned ? 1 : 0, rtc, clocks);
    (void)tk_ena_dsp();
}

void boot_trace_ai_dwt_reset(void)
{
    ER er = tk_dis_dsp();
    if (er != E_OK) { s_boot_trace.fault = 1U; }
    /* Preserve the original AI enable/reset behavior, tagging the discontinuity. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_boot_trace.epoch++;
    if (er == E_OK) { (void)tk_ena_dsp(); }
}

int usrcmd_boottrace(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) { (void)print_to_console("Usage: boottrace\r\n"); return -1; }
    trace_record_t copy;
    if (tk_dis_dsp() != E_OK) { return -1; }
    copy = s_boot_trace;
    (void)tk_ena_dsp();
    /* Fingerprint the actual CPU0 flash after snapshot; not in the boot path. */
    uintptr_t address = ((uintptr_t)&SystemInit) & ~(uintptr_t)1U;
    const volatile uint8_t *bytes = (const volatile uint8_t *)address;
    uint32_t fnv = 2166136261U;
    for (uint32_t i = 0; i < 4096U; ++i) { fnv = (fnv ^ bytes[i]) * 16777619U; }
    char line[240];
    (void)snprintf(line, sizeof(line),
        "BOOTTRACE232 v=%lu fnv=%08lX hz=%lu tick_ms=10 probe=%lu fault=%lu epoch=%lu count=%lu\r\n",
        (unsigned long)copy.version, (unsigned long)fnv, (unsigned long)copy.hz,
        (unsigned long)copy.probe, (unsigned long)copy.fault,
        (unsigned long)copy.epoch, (unsigned long)copy.next_seq);
    (void)print_to_console(line);
    (void)snprintf(line, sizeof(line), "POST_C cycles=%lu ctrl=%08lX\r\n",
                   (unsigned long)copy.initial_cycles, (unsigned long)copy.initial_ctrl);
    (void)print_to_console(line);
    static const char * const names[BT_COUNT] = {
        "INIT_BEGIN", "INIT_END", "OPEN_BEGIN", "OPEN_END", "BRANCH",
        "PROVISION_BEGIN", "PROVISION_END", "GET_BEGIN", "GET_END",
        "SYNC_BEGIN", "SYNC_END", "DISPLAY_BEGIN", "DISPLAY_END",
        "RESET_BEGIN", "RESET_END", "GLCDC_OPEN_BEGIN", "GLCDC_OPEN_END",
        "GLCDC_START_BEGIN", "GLCDC_START_END", "INITIAL_BUFFER_BEGIN",
        "INITIAL_BUFFER_END", "FLUSH_BEGIN", "FLUSH_END", "BACKLIGHT",
        "WAIT_BEGIN", "WAIT_END"
    };
    for (unsigned int i = 0; i < BT_COUNT; ++i) {
        const trace_slot_t *p = &copy.slot[i];
        (void)snprintf(line, sizeof(line),
            "BT %s seq=%lu hi=%lu lo=%lu cyc=%lu epoch=%lu ctrl=%08lX result=%ld aux=%08lX extra=%08lX\r\n",
            names[i], (unsigned long)p->seq, (unsigned long)p->hi, (unsigned long)p->lo,
            (unsigned long)p->cycles, (unsigned long)p->epoch, (unsigned long)p->ctrl,
            (long)p->result, (unsigned long)p->aux, (unsigned long)p->extra);
        (void)print_to_console(line);
    }
    (void)print_to_console("BOOTTRACE232 END\r\n");
    return 0;
}
