/* RAM-only boot diagnostics. Writers: pre-OS startup, then LVGL task.
 * Reader: NT-Shell task. No ISR writers; no extra device operations/barriers
 * in the recorded startup path. See doc/design/issue-230.md section 21. */
#include "boot_trace.h"
#include "jlink_console.h"
#include <tk/tkernel.h>
#include <stdio.h>

typedef struct {
    uint32_t lo, hi;
    int32_t result;
    uint32_t present;
} trace_slot_t;
typedef struct {
    uint32_t version, hz, dwt_ctrl, probe_delta;
    trace_slot_t slot[BT_COUNT];
} trace_record_t;

/* Ordinary internal .bss; map validation is mandatory. UART reads via CPU,
 * so no debugger/cache publication protocol is needed for this build. */
static volatile trace_record_t s_boot_trace __attribute__((aligned(32)));

void boot_trace_post_c(uint32_t cycles)
{
    /* BSS has already been zeroed. Do not clear it before sampling cycles. */
    s_boot_trace.version = 1U;
    s_boot_trace.hz = SystemCoreClock;
    s_boot_trace.dwt_ctrl = DWT->CTRL;
    uint32_t a = DWT->CYCCNT;
    __asm volatile("nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop" ::: "memory");
    s_boot_trace.probe_delta = DWT->CYCCNT - a;
    s_boot_trace.slot[BT_POST_C].lo = cycles;
    s_boot_trace.slot[BT_POST_C].present = 1U;
}

void boot_trace_cycles(enum boot_trace_point point, int32_t result)
{
    uint32_t cycles = DWT->CYCCNT;
    s_boot_trace.slot[point].lo = cycles;
    s_boot_trace.slot[point].hi = 0U;
    s_boot_trace.slot[point].result = result;
    __asm volatile("" ::: "memory");
    s_boot_trace.slot[point].present = 1U;
}

void boot_trace_os(enum boot_trace_point point, int32_t result)
{
    SYSTIM now;
    ER err = tk_get_otm(&now);
    if (err != E_OK) { return; }
    s_boot_trace.slot[point].lo = now.lo;
    s_boot_trace.slot[point].hi = (uint32_t) now.hi;
    s_boot_trace.slot[point].result = result;
    __asm volatile("" ::: "memory");
    s_boot_trace.slot[point].present = 1U;
}

int usrcmd_boottrace(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        (void)print_to_console("Usage: boottrace\r\n");
        return -1;
    }
    trace_record_t copy;
    /* All runtime writers are tasks. Copy under dispatch exclusion, but do
     * not hold exclusion across the UART calls below. Reading never resets. */
    ER err = tk_dis_dsp();
    if (err != E_OK) { return -1; }
    copy = s_boot_trace;
    (void)tk_ena_dsp();

    /* Same code/data in both images; identify the actual flash contents.
     * The build verifier computes these exact 4096 bytes for the manifest. */
    uintptr_t base = ((uintptr_t)&SystemInit) & ~(uintptr_t)1U;
    const volatile uint8_t *bytes = (const volatile uint8_t *)base;
    uint32_t fingerprint = 2166136261U;
    for (uint32_t i = 0; i < 4096U; ++i) {
        fingerprint = (fingerprint ^ bytes[i]) * 16777619U;
    }
    char line[160];
    (void)snprintf(line, sizeof(line),
        "BOOTTRACE v=%lu fnv4096=%08lX hz=%lu ctrl=%08lX probe=%lu\r\n",
        (unsigned long)copy.version, (unsigned long)fingerprint,
        (unsigned long)copy.hz, (unsigned long)copy.dwt_ctrl,
        (unsigned long)copy.probe_delta);
    (void)print_to_console(line);
    static const char * const names[BT_COUNT] = {
        "POST_C", "SDRAM_BEFORE", "SDRAM_AFTER", "OS_BEFORE",
        "DISPLAY_BEFORE", "RESET_BEFORE", "RESET_AFTER",
        "GLCDC_OPEN", "GLCDC_START", "DISPLAY_AFTER"
    };
    for (unsigned int i = 0; i < BT_COUNT; ++i) {
        const trace_slot_t *s = &copy.slot[i];
        (void)snprintf(line, sizeof(line),
            "BT %s clock=%s valid=%lu hi=%lu lo=%lu result=%ld\r\n",
            names[i], i <= BT_OS_BEFORE ? "DWT_MOD32" : "OS_MS10",
            (unsigned long)s->present, (unsigned long)s->hi,
            (unsigned long)s->lo, (long)s->result);
        (void)print_to_console(line);
    }
    (void)print_to_console("BOOTTRACE END (DWT wrap bound must be checked separately)\r\n");
    return 0;
}
