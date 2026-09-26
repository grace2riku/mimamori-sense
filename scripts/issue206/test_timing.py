"""Execute production audio timing paths on ARM with deterministic SSI/DWT mocks.

Requires ARM LLVM and Python unicorn. Override ARM_CLANG / ISSUE206_TEST_DEPS
if needed. Extracts complete production functions, not copies of their logic.
Does not model hardware latency, IRQ scheduling, DTC, or producer internals.
Unicorn's MMIO hooks can leave stale multi-instruction Thumb IT state with this
harness. Keep -O1, but use -mrestrict-it (single-instruction IT blocks) for these
extracted-code tests only. Firmware build flags and CPU flags are not modified.
"""
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "e2studio_CPU0/src/port/audio_port.c"
CLANG = os.environ.get("ARM_CLANG", "C:/Renesas/RA/e2studio_v2025-12_fsp_v6.3.0/toolchains/llvm_arm/ATfE-21.1.1-Windows-x86_64/bin/clang.exe")


def function(source, name):
    match = re.search(r"^(?:static )?(?:void|bool|int|fsp_err_t) " + name + r"\([^;{]*\)\s*\{.*?^\}", source, re.M | re.S)
    if not match:
        raise RuntimeError(f"Cannot extract production function {name}")
    return match.group(0)


MOCKS = r"""
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
void __aeabi_memclr4(void *dest, size_t count) {
    volatile unsigned char *p = dest;
    while (count--) *p++ = 0;
}
void __aeabi_memcpy4(void *dest, const void *source, size_t count) {
    volatile unsigned char *d = dest;
    const volatile unsigned char *s = source;
    while (count--) *d++ = *s++;
}
typedef int fsp_err_t;
typedef int ER;
#define E_OK 0
typedef unsigned UINT;
typedef struct { uint32_t hi, lo; } SYSTIM;
typedef void (*audio_fill_cb_t)(int16_t *, uint32_t, void *);
typedef struct { int event; } i2s_callback_args_t;
enum { FSP_SUCCESS, FSP_ERR_IN_USE, FSP_ERR_NOT_OPEN, MOCK_WRITE_ERROR, FSP_ERR_UNDERFLOW, FSP_ERR_ABORTED, FSP_ERR_INTERNAL };
enum { AUDIO_STATE_READY, AUDIO_STATE_PLAYING, AUDIO_STATE_STOPPING,
       AUDIO_STATE_INITIALIZING, AUDIO_STATE_ERROR };
enum { I2S_EVENT_TX_EMPTY, I2S_EVENT_IDLE };
#define AUDIO_BUFFER_BYTES 8
#define AUDIO_BUFFER_FRAMES 2
#define AUDIO_EVT_IDLE 1
#define AUDIO_UNMUTE_ATTEMPTS 3
#define AUDIO_UNMUTE_RETRY_MS 10
#define CoreDebug_DEMCR_TRCENA_Msk 1U
#define DWT_CTRL_CYCCNTENA_Msk 1U
static struct { volatile uint32_t CYCCNT, CTRL; } mock_dwt;
static struct { volatile uint32_t DEMCR; } mock_debug;
#define DWT (&mock_dwt)
#define CoreDebug (&mock_debug)
static uint32_t irq_disabled, write_cycles, fill_cycles, fills, writes, stops, flags;
#define DI(saved) do { (saved) = irq_disabled; irq_disabled = 1; } while (0)
#define EI(saved) do { irq_disabled = (saved); } while (0)
static fsp_err_t write_result;
static uint32_t s_tx_empty_count, s_idle_count, s_error_count, s_restart_count;
static uint32_t s_start_ms, s_start_tx_empty, s_timing_session, s_timing_clock;
static uint32_t s_play_idx, s_tone_phase, SystemCoreClock = 1000000000U;
static bool s_stop_request, s_ssi_open;
static int s_state, s_audio_flgid = 1;
typedef struct { volatile uint32_t tx_src_samples; const void *p_tx_src; } ssi_instance_ctrl_t;
#define g_i2s_audio_ctrl (*(ssi_instance_ctrl_t *)0x900300)
enum {TRANSFER_MODE_BLOCK = 2, TRANSFER_IRQ_END = 1, TRANSFER_CHAIN_MODE_DISABLED = 0};
typedef struct {
    union {
        uint32_t transfer_settings_word;
        struct { uint32_t reserved0:21, irq:1, chain_mode:2, reserved1:6, mode:2; } transfer_settings_word_b;
    };
    const void * volatile p_src;
    void * volatile p_dest;
    volatile uint16_t num_blocks, length;
} transfer_info_t;
#define mock_descriptor (*(transfer_info_t *)0x900200)
typedef struct { transfer_info_t *p_info; } mock_transfer_cfg_t;
static mock_transfer_cfg_t g_transfer_i2s_tx_cfg = {&mock_descriptor};
static struct { mock_transfer_cfg_t *p_cfg; } g_transfer_i2s_tx = {&g_transfer_i2s_tx_cfg};
typedef struct {
    union { volatile uint32_t SSIFSR; struct { volatile uint32_t reserved:24, TDC:6, reserved2:2; } SSIFSR_b; };
    union { volatile uint32_t SSICR; struct { volatile uint32_t reserved:1, TEN:1, reserved2:30; } SSICR_b; };
    union {
        volatile uint32_t SSIFCR;
        struct { volatile uint32_t reserved0:3, TIE:1, reserved1:28; } SSIFCR_b;
    };
    union { volatile uint32_t SSISR; struct { volatile uint32_t reserved:29, TUIRQ:1, reserved2:2; } SSISR_b; };
} mock_ssi_t;
typedef struct { volatile uint16_t DTCSTS; } mock_dtc_t;
#define R_DTC ((mock_dtc_t *)0x900500)
#define FSP_STYPE3_REG16_READ(reg, security) (reg)
#define AUDIO_DTC_SECURITY_ATTRIBUTE 0
typedef union {
    volatile uint32_t IELSR[1];
    struct { volatile uint32_t reserved:24, DTCE:1, reserved2:7; } IELSR_b[1];
} mock_icu_t;
#define R_SSI0 ((mock_ssi_t *)0x900000)
#define R_ICU ((mock_icu_t *)0x900100)
#define VECTOR_NUMBER_SSI0_TXI 0
#define mock_reads ((volatile uint32_t *)0x900400)
#define R_SSI0_SSIFCR_TIE_Msk 0x8U
#define MOCK_OTHER_FCR_BITS 0x80000810U /* AUCKE, BSW, RIE */
/* Hook-owned event log; marker writes distinguish API/producer boundaries. */
#define mock_events ((volatile uint32_t *)0x900600)
#define mock_monitor (*(volatile uint32_t *)0x900700)
#define mock_marker (*(volatile uint32_t *)0x900704)
#define mock_readback (*(volatile uint32_t *)0x900708)
#define mock_late_fifo_change (*(volatile uint32_t *)0x90070C)
#define mock_prov_monitor (*(volatile uint32_t *)0x900730)
#define mock_prov_change (*(volatile uint32_t *)0x900734)
#define mock_prov_observations ((volatile uint32_t *)0x900800)
#define AUDIO_PRINT_BUF_SIZE 144
static char output[16000];
static unsigned output_len, output_calls, change_session_on_print, print_while_masked, print_truncated;
static int snprintf(char *dst, size_t cap, const char *fmt, ...) {
    va_list args; va_start(args, fmt); unsigned n = 0;
    while (*fmt) {
        if (*fmt != '%') { if (n + 1 < cap) dst[n] = *fmt; n++; fmt++; continue; }
        fmt++; bool wide = false; unsigned width = 0; char pad = *fmt == '0' ? '0' : ' ';
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + *fmt - '0'; fmt++; }
        if (*fmt == 'l') { wide = true; fmt++; }
        if (*fmt == 's') {
            const char *text = va_arg(args, const char *);
            while (*text) { if (n + 1 < cap) dst[n] = *text; n++; text++; }
        } else {
            unsigned long value = wide ? va_arg(args, unsigned long) : va_arg(args, unsigned);
            unsigned base = (*fmt == 'X' || *fmt == 'x') ? 16 : 10;
            char digits[16]; unsigned k = 0;
            do { unsigned digit = value % base; digits[k++] = digit < 10 ? '0' + digit : 'A' + digit - 10; value /= base; } while (value);
            while (width > k) { if (n + 1 < cap) dst[n] = pad; n++; width--; }
            while (k) { if (n + 1 < cap) dst[n] = digits[k - 1]; n++; k--; }
        }
        fmt++;
    }
    if (cap) dst[n < cap ? n : cap - 1] = 0;
    if (n >= cap) print_truncated++;
    va_end(args); return (int)n;
}
static void print_to_console(const char *text) {
    if (irq_disabled) print_while_masked++;
    while (*text && output_len + 1 < sizeof(output)) output[output_len++] = *text++;
    output[output_len] = 0; output_calls++;
    if (change_session_on_print == output_calls) s_timing_session++;
}
static bool output_has(const char *text) {
    for (unsigned i = 0; i < output_len; i++) {
        unsigned j = 0; while (text[j] && output[i+j] == text[j]) j++;
        if (!text[j]) return true;
    }
    return false;
}
static fsp_err_t s_last_error;
static int16_t s_pcm_buf[2][4];
static audio_fill_cb_t s_fill_cb;
static void *s_fill_context;
static bool mock_transfer_on_write, mock_during_write_underflow, mock_tde_during_write;
static unsigned mock_stop_fail_mask;
static fsp_err_t mock_stop_result;
static unsigned last_written, last_filled;
static uint32_t write_fcr, fill_fcr, stop_fcr, write_readback;
static fsp_err_t R_SSI_Write(ssi_instance_ctrl_t *ctrl, int16_t *buf, uint32_t size) {
    mock_marker = 2; write_fcr = R_SSI0->SSIFCR; write_readback = mock_readback;
    (void)ctrl; (void)size; writes++; last_written = (buf == &s_pcm_buf[0][0]) ? 0 : 1;
    if (mock_tde_during_write) R_SSI0->SSIFSR |= 0x10000U;
    if (mock_transfer_on_write) {
        R_ICU->IELSR_b[0].DTCE = 1; mock_descriptor.num_blocks = 81;
        mock_descriptor.p_src = buf; mock_descriptor.length = 2;
        /* FSP starts SSI only when TEN is zero, preserving TIE while running. */
        if (!R_SSI0->SSICR_b.TEN) {
            R_SSI0->SSICR_b.TEN = 1; R_SSI0->SSIFCR_b.TIE = 1;
        }
    }
    if (mock_during_write_underflow) {
        R_SSI0->SSISR_b.TUIRQ = 1;
        DWT->CYCCNT += write_cycles; mock_marker = 3; return FSP_ERR_UNDERFLOW;
    }
    DWT->CYCCNT += write_cycles; mock_marker = 3; return write_result;
}
static fsp_err_t R_SSI_Stop(ssi_instance_ctrl_t *ctrl) {
    mock_marker = 6; stop_fcr = R_SSI0->SSIFCR;
    (void)ctrl; stops++;
    if (mock_transfer_on_write) {
        if (!(mock_stop_fail_mask & 1)) R_SSI0->SSICR_b.TEN = 0;
        /* Inject a stuck-high TIE readback, even if caller cleared it earlier. */
        R_SSI0->SSIFCR_b.TIE = (mock_stop_fail_mask & 2) ? 1 : 0;
        if (!(mock_stop_fail_mask & 4)) R_ICU->IELSR_b[0].DTCE = 0;
        R_DTC->DTCSTS = (mock_stop_fail_mask & 8) ? 0x8000 : 0;
    }
    return mock_stop_result;
}
static void audio_fill_buffer(uint32_t index) {
    mock_marker = 5; fill_fcr = R_SSI0->SSIFCR;
    last_filled = index; fills++; DWT->CYCCNT += fill_cycles;
}
static int tk_set_flg(int id, int value) { (void)id; (void)value; flags++; return 0; }
static uint32_t mock_time_ms = 1234, mock_time_calls;
static int mock_time_result;
static int tk_get_otm(SYSTIM *now) {
    mock_time_calls++; now->hi = 0; now->lo = mock_time_ms; return mock_time_result;
}
static void audio_tone_fill(int16_t *p, uint32_t n, void *ctx) { (void)p; (void)n; (void)ctx; }
static void audio_tone_update_step(void) {}
static fsp_err_t da7212_mute(bool muted) { (void)muted; return FSP_SUCCESS; }
static void tk_dly_tsk(int ms) { (void)ms; }
static fsp_err_t audio_stop(void) { return FSP_SUCCESS; }
enum { CMD_OK, CMD_ERR_USAGE, CMD_ERR_EXECUTE, CMD_ERR_INVALID_ARG };
typedef struct { bool valid; uint32_t value; } cmd_parse_result_t;
static uint32_t s_tone_freq_hz = 1000;
static uint16_t s_tone_amplitude = 16000;
#define AUDIO_SAMPLE_RATE_HZ 16276U
static int ntlibc_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
/* Unrelated shell branches are linked so the real dispatcher is exercised. */
static void audio_cmd_usage(void) {}
static void audio_cmd_status(void) {}
static fsp_err_t audio_init(void) { return FSP_SUCCESS; }
static void cmd_print_usage(const char *a, const char *b) { (void)a; (void)b; }
static void cmd_print_error(const char *s) { (void)s; }
static cmd_parse_result_t cmd_parse_uint32(const char *s) { (void)s; return (cmd_parse_result_t){false, 0}; }
static fsp_err_t audio_set_test_tone(uint32_t hz, uint16_t amplitude) { (void)hz; (void)amplitude; return FSP_SUCCESS; }
static unsigned da7212_get_volume(void) { return 60; }
static unsigned da7212_get_volume_code(void) { return 42; }
static fsp_err_t audio_set_volume(uint8_t v) { (void)v; return FSP_SUCCESS; }
static fsp_err_t da7212_write_reg(uint8_t a, uint8_t v) { (void)a; (void)v; return FSP_SUCCESS; }
static fsp_err_t da7212_read_reg(uint8_t a, uint8_t *v) { (void)a; *v = 0; return FSP_SUCCESS; }
"""

TESTS = r"""
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
/* Mock the FSP entry, retaining real application callback behavior. */
static uint32_t real_tx_calls, real_int_calls;
static bool mock_tx_notify, mock_int_notify, mock_change_state, mock_override_tdc;
static bool mock_int_clear_status;
static uint32_t mock_irq_tdc;
static void mock_hardware_change(void) {
    if (mock_override_tdc) R_SSI0->SSIFSR_b.TDC = mock_irq_tdc;
    if (mock_change_state) {
        R_SSI0->SSIFSR_b.TDC = 29; R_ICU->IELSR_b[0].DTCE = 0;
        mock_descriptor.num_blocks = 7; g_i2s_audio_ctrl.tx_src_samples = 11;
    }
}
void __real_ssi_txi_isr(void) {
    real_tx_calls++;
    mock_hardware_change();
    if (mock_tx_notify) {
        i2s_callback_args_t args = {I2S_EVENT_TX_EMPTY};
        audio_i2s_callback(&args);
    }
}
void __real_ssi_int_isr(void) {
    real_int_calls++;
    if (mock_int_clear_status) R_SSI0->SSISR = 0;
    mock_hardware_change();
    if (mock_int_notify) {
        i2s_callback_args_t args = {I2S_EVENT_IDLE};
        audio_i2s_callback(&args);
    }
}

static int test_irq_wrappers(void) {
    /* These helpers never substitute for actual production wrapper bodies. */
    s_timing = (audio_timing_t){0};
    s_state = AUDIO_STATE_PLAYING; s_stop_request = false;
    write_result = FSP_SUCCESS;
    __wrap_ssi_txi_isr();
    __wrap_ssi_txi_isr();
    CHECK(real_tx_calls == 2 && real_int_calls == 0);
    CHECK(s_timing.tx_irq_count == 2 && s_timing.tx_no_callback == 2);
    CHECK(s_timing.tx_no_callback_run == 2 && s_timing.tx_no_callback_run_max == 2);
    mock_tx_notify = true;
    /* Callback counter wrap must still count as a callback. */
    s_tx_empty_count = UINT32_MAX;
    __wrap_ssi_txi_isr();
    CHECK(real_tx_calls == 3 && s_tx_empty_count == 0);
    CHECK(s_timing.tx_irq_count == 3 && s_timing.tx_no_callback == 2);
    CHECK(s_timing.tx_no_callback_run == 0 && s_timing.tx_no_callback_run_max == 2);
    mock_tx_notify = false;
    __wrap_ssi_txi_isr();
    CHECK(real_tx_calls == 4 && s_timing.tx_no_callback_run == 1);
    CHECK(s_timing.tx_no_callback_run_max == 2 && s_timing.tx_no_callback == 3);

    __wrap_ssi_int_isr();
    CHECK(real_int_calls == 1 && s_timing.int_irq_count == 1 && s_timing.int_no_idle == 1);
    mock_int_notify = true; s_idle_count = UINT32_MAX;
    __wrap_ssi_int_isr();
    CHECK(real_int_calls == 2 && s_idle_count == 0);
    CHECK(s_timing.int_irq_count == 2 && s_timing.int_no_idle == 1);
    CHECK(real_tx_calls == 4 && s_timing.tx_irq_count == 4);

    /* Each independently incremented counter invalidates at the first limit,
     * and stays at UINT32_MAX on the following entry (never wraps to zero). */
    volatile uint32_t *tx_counters[] = {
        &s_timing.tx_irq_count, &s_timing.tx_no_callback,
        &s_timing.tx_no_callback_run
    };
    for (unsigned i = 0; i < 3; i++) {
        s_timing = (audio_timing_t){0};
        *tx_counters[i] = UINT32_MAX - 1;
        uint32_t calls = real_tx_calls;
        __wrap_ssi_txi_isr();
        CHECK(real_tx_calls == calls + 1);
        CHECK(*tx_counters[i] == UINT32_MAX && s_timing.irq_saturated == 1);
        __wrap_ssi_txi_isr();
        CHECK(real_tx_calls == calls + 2);
        CHECK(*tx_counters[i] == UINT32_MAX && s_timing.irq_saturated == 1);
        if (i == 2) CHECK(s_timing.tx_no_callback_run_max == UINT32_MAX);
        mock_tx_notify = true;
        __wrap_ssi_txi_isr();
        CHECK(real_tx_calls == calls + 3);
        CHECK(s_timing.tx_no_callback_run == 0 && s_timing.irq_saturated == 1);
        if (i == 2) CHECK(s_timing.tx_no_callback_run_max == UINT32_MAX);
        mock_tx_notify = false;
    }
    volatile uint32_t *int_counters[] = {&s_timing.int_irq_count, &s_timing.int_no_idle};
    for (unsigned i = 0; i < 2; i++) {
        s_timing = (audio_timing_t){0}; mock_int_notify = false;
        *int_counters[i] = UINT32_MAX - 1;
        uint32_t calls = real_int_calls;
        __wrap_ssi_int_isr();
        CHECK(real_int_calls == calls + 1);
        CHECK(*int_counters[i] == UINT32_MAX && s_timing.irq_saturated == 1);
        __wrap_ssi_int_isr();
        CHECK(real_int_calls == calls + 2);
        CHECK(*int_counters[i] == UINT32_MAX && s_timing.irq_saturated == 1);
        mock_int_notify = true;
        __wrap_ssi_int_isr();
        CHECK(real_int_calls == calls + 3 && s_timing.irq_saturated == 1);
    }

    /* Exercise actual session start with every stage-two field dirty. */
    s_timing.tx_irq_count = s_timing.tx_no_callback = 99;
    s_timing.tx_no_callback_run = s_timing.tx_no_callback_run_max = 99;
    s_timing.int_irq_count = s_timing.int_no_idle = 99;
    s_timing.irq_saturated = 1;
    s_state = AUDIO_STATE_READY;
    uint32_t session = s_timing_session;
    CHECK(audio_start(NULL, NULL) == FSP_SUCCESS);
    CHECK(s_timing_session == session + 1);
    CHECK(s_timing.tx_irq_count == 0 && s_timing.tx_no_callback == 0);
    CHECK(s_timing.tx_no_callback_run == 0 && s_timing.tx_no_callback_run_max == 0);
    CHECK(s_timing.int_irq_count == 0 && s_timing.int_no_idle == 0);
    CHECK(s_timing.irq_saturated == 0 && irq_disabled == 0);
    return 0;
}


static void reset_trace_test(void) {
    s_state = AUDIO_STATE_READY; write_result = FSP_SUCCESS;
    (void) audio_start(NULL, NULL);
    mock_tx_notify = false; mock_int_notify = false; mock_change_state = false;
    mock_override_tdc = false; mock_int_clear_status = false; s_ssi_open = false;
    real_tx_calls = real_int_calls = 0;
    output_len = output_calls = change_session_on_print = 0; output[0] = 0;
}
static int test_trace(void) {
    CHECK(sizeof(audio_trace_record_t) == 56);
    reset_trace_test();
    uint32_t values[] = {0, 1, 16, 17, 32, 33};
    for (unsigned i = 0; i < 6; i++) {
        R_SSI0->SSIFSR_b.TDC = values[i]; __wrap_ssi_txi_isr();
    }
    CHECK(s_timing.fifo_no_cb[0] == 1 && s_timing.fifo_no_cb[1] == 2);
    CHECK(s_timing.fifo_no_cb[2] == 2 && s_timing.fifo_no_cb[3] == 1);
    CHECK(s_timing.tx_no_callback == 6 && real_tx_calls == 6);

    reset_trace_test();
    R_SSI0->SSIFSR_b.TDC = 3; R_ICU->IELSR_b[0].DTCE = 1;
    mock_descriptor.num_blocks = 81; g_i2s_audio_ctrl.tx_src_samples = 22;
    mock_change_state = true; uint32_t callback = s_tx_empty_count;
    __wrap_ssi_txi_isr();
    CHECK(s_timing.trace_count == 1 && real_tx_calls == 1);
    CHECK(s_trace[0].kind == AUDIO_TRACE_TX_NO_CB && s_trace[0].ordinal == 1);
    CHECK(s_trace[0].pre_tdc == 3 && s_trace[0].post_tdc == 29);
    CHECK(s_trace[0].pre_dtce == 1 && s_trace[0].post_dtce == 0);
    CHECK(s_trace[0].pre_descriptor_blocks == 81 && s_trace[0].post_descriptor_blocks == 7);
    CHECK(s_trace[0].pre_sw_samples == 22 && s_trace[0].post_sw_samples == 11);
    CHECK(s_trace[0].callback_before == callback && s_trace[0].callback_after == callback);
    CHECK(s_trace[0].flags & AUDIO_TRACE_POST_VALID);
    mock_change_state = false; mock_tx_notify = true;
    __wrap_ssi_txi_isr(); /* Successful TX consumes ordinal, not a trace slot. */
    CHECK(s_timing.trace_count == 1 && s_timing.trace_ordinal == 2);
    __wrap_ssi_int_isr();
    CHECK(s_trace[1].kind == AUDIO_TRACE_INT_NO_IDLE && s_trace[1].ordinal == 3);
    CHECK(!(s_trace[1].flags & AUDIO_TRACE_POST_VALID));
    CHECK(s_trace[1].post_tdc == 0 && s_trace[1].post_dtce == 0);
    CHECK(s_trace[1].post_descriptor_blocks == 0 && s_trace[1].post_sw_samples == 0);
    mock_int_notify = true; __wrap_ssi_int_isr();
    CHECK(s_trace[2].kind == AUDIO_TRACE_INT_IDLE && s_trace[2].ordinal == 4);
    CHECK(s_trace[2].callback_after == s_trace[2].callback_before + 1);
    CHECK(real_tx_calls == 2 && real_int_calls == 2);

    audio_trace_record_t copy = {0};
    CHECK(audio_trace_copy(s_timing_session, 0, &copy));
    CHECK(copy.ordinal == 1 && copy.pre_descriptor_blocks == 81);
    CHECK(!audio_trace_copy(s_timing_session + 1, 0, &copy));
    CHECK(!audio_trace_copy(s_timing_session, s_timing.trace_count, &copy));
    audio_timing_t snapshot = s_timing;
    audio_trace_print(s_timing_session, &snapshot);
    CHECK(output_has("n/a") && output_has("trace_valid=1"));
    uint32_t final_record_output = output_calls - 1;
    output_len = output_calls = 0; output[0] = 0;
    change_session_on_print = final_record_output;
    audio_trace_print(s_timing_session, &snapshot);
    CHECK(output_has("trace_valid=0")); /* Reset during last record output. */
    output_len = output_calls = 0; output[0] = 0; change_session_on_print = 1;
    audio_trace_print(s_timing_session, &snapshot);
    CHECK(output_has("trace_valid=0"));
    CHECK(irq_disabled == 0);

    reset_trace_test();
    for (unsigned i = 0; i < AUDIO_TRACE_CAPACITY; i++) {
        R_SSI0->SSIFSR_b.TDC = 16; __wrap_ssi_txi_isr();
    }
    CHECK(s_timing.trace_count == 16 && s_timing.trace_truncated == 0);
    snapshot = s_timing; audio_trace_print(s_timing_session, &snapshot);
    CHECK(output_has("full=1 truncated=0"));
    CHECK(s_trace[15].ordinal == 16);
    /* SSI closed isolates trace reads from the independent fallback guards. */
    s_timing.delay_count = AUDIO_DELAY_CAPACITY; /* All newer TX diagnostics full. */
    for (unsigned i = 0; i < 4; i++) mock_reads[i] = 0;
    __wrap_ssi_txi_isr();
    CHECK(mock_reads[0] == 1 && mock_reads[1] == 0);
    CHECK(mock_reads[2] == 0 && mock_reads[3] == 0);
    CHECK(s_timing.trace_count == 16 && s_timing.trace_truncated == 1);
    CHECK(s_timing.fifo_no_cb[1] == 17 && s_timing.trace_ordinal == 17);
    CHECK(s_trace[15].ordinal == 16 && real_tx_calls == 17);
    s_timing.int_trace_count = AUDIO_INT_TRACE_CAPACITY;
    for (unsigned i = 0; i < 4; i++) mock_reads[i] = 0;
    __wrap_ssi_int_isr();
    CHECK(mock_reads[0] == 0 && mock_reads[1] == 0);
    CHECK(mock_reads[2] == 0 && mock_reads[3] == 0);
    CHECK(s_timing.trace_ordinal == 18 && real_int_calls == 1);
    uint32_t old_ordinal = s_trace[0].ordinal;
    reset_trace_test();
    CHECK(s_timing.trace_count == 0 && s_timing.trace_ordinal == 0);
    CHECK(s_timing.trace_truncated == 0 && s_timing.fifo_no_cb[1] == 0);
    CHECK(s_trace[0].ordinal == old_ordinal); /* Reset need not erase 896 bytes. */
    CHECK(!audio_trace_copy(s_timing_session, 0, &copy));
    snapshot = s_timing; audio_trace_print(s_timing_session, &snapshot);
    CHECK(output_has("trace_valid=1"));
    CHECK(!output_has("TX-no-cb")); /* No stale slot exposed by status. */
    output_len = output_calls = 0; output[0] = 0; change_session_on_print = 1;
    audio_trace_print(s_timing_session, &snapshot);
    CHECK(output_has("trace_valid=0")); /* Empty snapshot must still check session. */
    CHECK(print_while_masked == 0);

    for (unsigned i = 0; i < 5; i++) {
        reset_trace_test();
        volatile uint32_t *counter = i == 4 ? &s_timing.trace_ordinal : &s_timing.fifo_no_cb[i];
        *counter = UINT32_MAX - 1;
        R_SSI0->SSIFSR_b.TDC = i == 0 ? 0 : i == 1 ? 16 : i == 2 ? 32 : 33;
        __wrap_ssi_txi_isr();
        CHECK(*counter == UINT32_MAX && s_timing.irq_saturated == 1);
        __wrap_ssi_txi_isr();
        CHECK(*counter == UINT32_MAX && s_timing.irq_saturated == 1);
        snapshot = s_timing; audio_trace_print(s_timing_session, &snapshot);
        CHECK(output_has("trace_valid=0"));
    }
    CHECK(print_while_masked == 0);
    return 0;
}


/* DTC mocks expose reconfiguration even on Write failure, as FSP does. */
static void reset_fallback_test(void) {
    mock_prov_monitor = 0; mock_prov_change = 0;
    mock_monitor = 0; mock_late_fifo_change = 0;
    reset_trace_test();
    s_ssi_open = true; s_state = AUDIO_STATE_PLAYING; s_stop_request = false;
    s_play_idx = 0; s_last_error = FSP_SUCCESS; s_error_count = 0;
    mock_transfer_on_write = true; mock_during_write_underflow = false;
    mock_tde_during_write = false;
    mock_stop_fail_mask = 0; mock_stop_result = FSP_SUCCESS;
    fills = writes = stops = 0; fill_cycles = 3; write_cycles = 2;
    R_SSI0->SSIFSR = 16U << 24; R_SSI0->SSICR_b.TEN = 1;
    R_SSI0->SSIFCR_b.TIE = 1; R_SSI0->SSISR_b.TUIRQ = 0;
    R_ICU->IELSR_b[0].DTCE = 0; R_DTC->DTCSTS = 0;
    g_transfer_i2s_tx_cfg.p_info = &mock_descriptor;
    mock_descriptor.num_blocks = 0;
    mock_descriptor.transfer_settings_word_b.mode = TRANSFER_MODE_BLOCK;
    mock_descriptor.transfer_settings_word_b.irq = TRANSFER_IRQ_END;
    mock_descriptor.transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_DISABLED;
    g_i2s_audio_ctrl.p_tx_src = NULL; g_i2s_audio_ctrl.tx_src_samples = 0;
    for (unsigned i = 0; i < 5; i++) mock_reads[i] = 0;
}
static void begin_tie_monitor(void) {
    R_SSI0->SSIFCR = MOCK_OTHER_FCR_BITS | R_SSI0_SSIFCR_TIE_Msk;
    mock_events[0] = 0; mock_readback = 0; mock_monitor = 1;
}
static int test_int_trace(void) {
    CHECK(sizeof(audio_tx_summary_t) == 32 && sizeof(audio_int_record_t) == 96);
    reset_trace_test();
    __wrap_ssi_int_isr(); /* No completed TX in this session. */
    CHECK(s_timing.int_trace_count == 1 && s_int_trace[0].last_valid == 0);
    CHECK(real_int_calls == 1 && real_tx_calls == 0);
    audio_timing_t first_snapshot = s_timing;
    audio_int_trace_print(s_timing_session, &first_snapshot);
    CHECK(output_has("last_valid=0 TX=n/a"));

    reset_fallback_test();
    mock_tx_notify = true; DWT->CYCCNT = 100;
    __wrap_ssi_txi_isr();
    CHECK(real_tx_calls == 1 && s_timing.last_tx_valid == 1);
    CHECK(s_last_tx.ordinal == 1 && s_last_tx.entry_cycles == 100 && s_last_tx.exit_cycles == 105);
    CHECK(s_last_tx.entry_tdc == 16 && s_last_tx.native_delta == 1);
    CHECK(s_last_tx.fallback_attempt_delta == 0 && s_last_tx.fallback_ok_delta == 0 && s_last_tx.refill_ok_delta == 1);
    mock_tx_notify = false; R_ICU->IELSR_b[0].DTCE = 0; mock_descriptor.num_blocks = 0;
    DWT->CYCCNT = 200; __wrap_ssi_txi_isr();
    CHECK(real_tx_calls == 2 && s_last_tx.ordinal == 2 && s_last_tx.entry_cycles == 200 && s_last_tx.exit_cycles == 205);
    CHECK(s_last_tx.native_delta == 0 && s_last_tx.fallback_attempt_delta == 1);
    CHECK(s_last_tx.fallback_ok_delta == 1 && s_last_tx.refill_ok_delta == 1);
    __wrap_ssi_txi_isr(); /* Newly armed DTC: skip rather than submit twice. */
    CHECK(real_tx_calls == 3 && writes == 2 && fills == 2);
    CHECK(s_last_tx.native_delta == 0 && s_last_tx.fallback_attempt_delta == 0);
    CHECK(s_last_tx.fallback_ok_delta == 0 && s_last_tx.refill_ok_delta == 0);

    reset_trace_test();
    for (unsigned i = 0; i < AUDIO_TRACE_CAPACITY; i++) __wrap_ssi_txi_isr();
    CHECK(s_timing.trace_count == AUDIO_TRACE_CAPACITY && s_timing.int_trace_count == 0);
    R_SSI0->SSICR = 0x600940A2; R_SSI0->SSISR = 0x20000001;
    R_SSI0->SSIFCR = 0x80000818; R_SSI0->SSIFSR = 0x12000000;
    R_ICU->IELSR[0] = 0x01000123; R_DTC->DTCSTS = 0x8014;
    mock_descriptor.num_blocks = 61; g_i2s_audio_ctrl.tx_src_samples = 7;
    s_state = AUDIO_STATE_STOPPING; s_stop_request = true;
    mock_int_clear_status = true; mock_int_notify = true;
    s_idle_count = UINT32_MAX; DWT->CYCCNT = 900;
    *(volatile uint32_t *)0x900720 = 1;
    __wrap_ssi_int_isr();
    CHECK(*(volatile uint32_t *)0x900724 == 0x90000C); /* SSISR is first MMIO. */
    CHECK(real_tx_calls == 16 && real_int_calls == 1 && s_timing.int_trace_count == 1);
    CHECK(R_SSI0->SSISR == 0 && s_int_trace[0].ssisr == 0x20000001);
    CHECK(s_int_trace[0].ssicr == 0x600940A2 && s_int_trace[0].ssifcr == 0x80000818);
    CHECK(s_int_trace[0].ssifsr == 0x12000000 && s_int_trace[0].ielsr == 0x01000123);
    CHECK(s_int_trace[0].dtcsts == 0x8014 && s_int_trace[0].descriptor_blocks == 61);
    CHECK(s_int_trace[0].sw_samples == 7 && s_int_trace[0].cycles == 900);
    CHECK(s_int_trace[0].state == AUDIO_STATE_STOPPING && s_int_trace[0].stop == 1);
    CHECK(s_int_trace[0].idle_before == UINT32_MAX && s_int_trace[0].idle_after == 0);
    CHECK(s_int_trace[0].kind == AUDIO_TRACE_INT_IDLE && s_int_trace[0].last_valid == 1);
    CHECK(s_int_trace[0].last_tx.ordinal == 16);
    mock_int_clear_status = false; mock_int_notify = false;
    for (unsigned i = 1; i < AUDIO_INT_TRACE_CAPACITY; i++) __wrap_ssi_int_isr();
    CHECK(real_int_calls == 8 && s_timing.int_trace_count == 8 && s_timing.int_trace_omitted == 0);
    CHECK(s_int_trace[7].kind == AUDIO_TRACE_INT_NO_IDLE);
    audio_int_record_t saved[8];
    for (unsigned i = 0; i < 8; i++) saved[i] = s_int_trace[i];
    audio_tx_summary_t last = s_last_tx;
    s_timing.delay_count = AUDIO_DELAY_CAPACITY;
    for (unsigned i = 0; i < 5; i++) mock_reads[i] = 0;
    __wrap_ssi_int_isr();
    CHECK(real_int_calls == 9 && s_timing.int_trace_count == 8 && s_timing.int_trace_omitted == 1);
    for (unsigned i = 0; i < 5; i++) CHECK(mock_reads[i] == 0);
    for (unsigned i = 0; i < 8; i++) {
        const unsigned char *old = (const unsigned char *)&saved[i];
        const volatile unsigned char *now = (const volatile unsigned char *)&s_int_trace[i];
        for (unsigned j = 0; j < sizeof(saved[i]); j++) CHECK(old[j] == now[j]);
    }
    __wrap_ssi_txi_isr();
    CHECK(real_tx_calls == 17 && mock_reads[0] == 1);
    for (unsigned i = 1; i < 5; i++) CHECK(mock_reads[i] == 0);
    for (unsigned i = 0; i < sizeof(last); i++)
        CHECK(((const unsigned char *)&last)[i] == ((const volatile unsigned char *)&s_last_tx)[i]);

    audio_int_record_t copy = {0}; audio_timing_t snapshot = s_timing;
    CHECK(audio_int_trace_copy(s_timing_session, 0, &copy) && copy.ssisr == 0x20000001);
    CHECK(!audio_int_trace_copy(s_timing_session + 1, 0, &copy));
    CHECK(!audio_int_trace_copy(s_timing_session, 8, &copy));
    audio_int_trace_print(s_timing_session, &snapshot);
    CHECK(output_has("int_trace_valid=1"));
    uint32_t last_output = output_calls - 1;
    output_len = output_calls = 0; output[0] = 0; change_session_on_print = last_output;
    audio_int_trace_print(s_timing_session, &snapshot);
    CHECK(output_has("int_trace_valid=0"));
    output_len = output_calls = 0; output[0] = 0; change_session_on_print = 1;
    audio_int_trace_print(s_timing_session, &snapshot);
    CHECK(output_has("int_trace_valid=0") && irq_disabled == 0);
    change_session_on_print = 0;
    s_timing.int_trace_omitted = UINT32_MAX - 1;
    __wrap_ssi_int_isr(); __wrap_ssi_int_isr();
    CHECK(s_timing.int_trace_omitted == UINT32_MAX && s_timing.irq_saturated == 1);
    snapshot = s_timing; output_len = output_calls = 0; output[0] = 0;
    audio_int_trace_print(s_timing_session, &snapshot);
    CHECK(output_has("int_trace_valid=0"));

    /* Maximal numeric fields must fit the actual production display buffer. */
    for (unsigned i = 0; i < sizeof(s_int_trace[0]); i++) ((volatile unsigned char *)&s_int_trace[0])[i] = 0xFF;
    s_timing_session = UINT32_MAX;
    snapshot.int_trace_count = 1; snapshot.int_trace_omitted = UINT32_MAX;
    snapshot.tx_summary_max = UINT32_MAX; print_truncated = 0;
    audio_int_trace_print(s_timing_session, &snapshot);
    CHECK(print_truncated == 0);
    uint32_t stored = s_int_trace[0].ordinal;
    reset_trace_test();
    CHECK(s_timing.int_trace_count == 0 && s_timing.int_trace_omitted == 0 && s_timing.last_tx_valid == 0);
    CHECK(s_timing.tx_summary_max == 0 && s_int_trace[0].ordinal == stored);
    CHECK(!audio_int_trace_copy(s_timing_session, 0, &copy));
    snapshot = s_timing; audio_int_trace_print(s_timing_session, &snapshot);
    CHECK(output_has("int_trace_valid=1") && print_while_masked == 0);
    output_len = output_calls = 0; output[0] = 0; change_session_on_print = 1;
    audio_int_trace_print(s_timing_session, &snapshot);
    CHECK(output_has("int_trace_valid=0"));
    reset_fallback_test();
    return 0;
}
static int test_tie_reissue(void) {
    reset_fallback_test(); begin_tie_monitor();
    audio_fallback_refill();
    mock_monitor = 0;
    CHECK(writes == 1 && fills == 1);
    CHECK(write_fcr == MOCK_OTHER_FCR_BITS && write_readback == 1);
    CHECK(fill_fcr == (MOCK_OTHER_FCR_BITS | R_SSI0_SSIFCR_TIE_Msk));
    CHECK(R_SSI0->SSIFCR == fill_fcr);
    /* Clear -> Write entry -> Write return -> set -> producer. */
    CHECK(mock_events[0] == 5);
    for (unsigned i = 1; i <= 5; i++) CHECK(mock_events[i] == i);

    /* Native notifications use the same order whether TDE remains low or
     * rises during Write. This models register state, not request pulses. */
    for (unsigned tde = 0; tde < 2; tde++) {
        reset_fallback_test(); begin_tie_monitor();
        mock_tde_during_write = tde != 0;
        mock_tx_notify = true; R_SSI0->SSIFSR_b.TDC = 24;
        uint32_t native_before = s_tx_empty_count;
        __wrap_ssi_txi_isr();
        mock_monitor = 0;
        CHECK(real_tx_calls == 1 && s_tx_empty_count == native_before + 1);
        CHECK(writes == 1 && fills == 1 && s_play_idx == 1);
        CHECK(write_fcr == MOCK_OTHER_FCR_BITS && write_readback == 1);
        CHECK(fill_fcr == (MOCK_OTHER_FCR_BITS | R_SSI0_SSIFCR_TIE_Msk));
        CHECK(mock_events[0] == 5);
        for (unsigned i = 1; i <= 5; i++) CHECK(mock_events[i] == i);
        CHECK(((R_SSI0->SSIFSR & 0x10000U) != 0) == (tde != 0));
        CHECK(s_timing.refill_ok == 1 && s_timing.fallback_attempt == 0 && s_timing.fallback_ok == 0);
        __wrap_ssi_txi_isr(); /* Native pending IRQ while DTC owns the buffer. */
        mock_tx_notify = false; __wrap_ssi_txi_isr();
        CHECK(writes == 1 && fills == 1 && s_play_idx == 1);
        CHECK(s_timing.fallback_attempt == 0 && s_timing.refill_ok == 1);
    }

    for (unsigned error = 0; error < 2; error++) {
        reset_fallback_test(); begin_tie_monitor(); mock_tx_notify = true;
        R_SSI0->SSIFSR_b.TDC = 24;
        if (error == 0) write_result = MOCK_WRITE_ERROR;
        else mock_during_write_underflow = true;
        __wrap_ssi_txi_isr();
        mock_monitor = 0;
        CHECK(write_fcr == MOCK_OTHER_FCR_BITS && write_readback == 1);
        CHECK(stop_fcr == MOCK_OTHER_FCR_BITS && s_stop_request);
        CHECK(writes == 1 && stops == 1 && fills == 0 && s_play_idx == 0);
        CHECK(s_timing.refill_ok == 0 && s_timing.fallback_attempt == 0);
        CHECK(s_timing.fallback_fail == 0 && s_timing.fallback_underflow == 0);
        CHECK(mock_events[1] == 1 && mock_events[2] == 2 && mock_events[3] == 3 && mock_events[4] == 6);
        CHECK(R_SSI0->SSIFCR == MOCK_OTHER_FCR_BITS);
        mock_int_notify = true; mock_during_write_underflow = false;
        __wrap_ssi_int_isr(); /* Native errors retain requested-stop policy. */
        CHECK(writes == 1 && fills == 0 && s_state == AUDIO_STATE_READY);
        CHECK(s_timing.recover_ok_count == 0 && s_timing.recover_fail_count == 0);
    }

    /* Read current FIFO/TUIRQ again inside shared helper, not only its caller. */
    for (unsigned kind = 1; kind <= 3; kind++) {
        reset_fallback_test(); begin_tie_monitor();
        mock_late_fifo_change = kind;
        audio_fallback_refill();
        mock_monitor = 0; mock_late_fifo_change = 0;
        CHECK(writes == 0 && fills == 0 && stops == 0);
        CHECK(s_timing.fallback_attempt == 0 && mock_events[0] == 0);
        CHECK(R_SSI0->SSIFCR == (MOCK_OTHER_FCR_BITS | R_SSI0_SSIFCR_TIE_Msk));
    }
    /* Write failure must enter Stop with TIE still clear, without generation. */
    for (unsigned kind = 0; kind < 2; kind++) {
        reset_fallback_test(); begin_tie_monitor();
        if (kind == 0) write_result = MOCK_WRITE_ERROR;
        else mock_during_write_underflow = true;
        audio_fallback_refill();
        mock_monitor = 0;
        CHECK(write_fcr == MOCK_OTHER_FCR_BITS && write_readback == 1);
        CHECK(stop_fcr == MOCK_OTHER_FCR_BITS && fills == 0 && s_play_idx == 0);
        CHECK(mock_events[1] == 1 && mock_events[2] == 2 && mock_events[3] == 3);
        CHECK(mock_events[4] == 6); /* No TIE enable between Write and Stop. */
        CHECK(R_SSI0->SSIFCR == MOCK_OTHER_FCR_BITS);
    }
    reset_fallback_test();
    return 0;
}
static void delay_tx(uint32_t cycles, uint32_t ms) {
    DWT->CYCCNT = cycles; mock_time_ms = ms; __wrap_ssi_txi_isr();
}
static void reset_delay_test(void) {
    mock_time_result = 0; mock_time_ms = 1234;
    reset_trace_test(); mock_time_calls = 0;
}
static int test_delay_trace(void) {
    CHECK(sizeof(audio_delay_record_t) <= 160 && AUDIO_DELAY_CAPACITY == 4);
    reset_delay_test();
    CHECK(s_timing.delay_threshold_cycles == 30000000U);
    delay_tx(0, 0);
    CHECK(real_tx_calls == 1 && mock_time_calls == 1 && s_timing.delay_count == 0);
    CHECK(s_timing.delay_prev_valid && !s_timing.delay_success_valid);
    delay_tx(29999999, 29);
    CHECK(s_timing.delay_count == 0);
    delay_tx(59999999, 59);
    CHECK(s_timing.delay_count == 1 && s_timing.delay_latched);
    CHECK(s_delay_trace[0].reason == 3 && s_delay_trace[0].prev_valid && !s_delay_trace[0].success_valid);
    CHECK(s_delay_trace[0].current.cycles == 59999999 && s_delay_trace[0].previous.cycles == 29999999);
    CHECK(s_delay_trace[0].current.ms == 59 && s_delay_trace[0].previous.ms == 29);

    /* Independent comparisons, unsigned wraps, and failed clock API calls. */
    reset_delay_test(); delay_tx(100, 0); delay_tx(200, 30);
    CHECK(s_timing.delay_count == 1 && s_delay_trace[0].reason == 2);
    reset_delay_test(); delay_tx(0, 7); delay_tx(30000000, 7);
    CHECK(s_timing.delay_count == 1 && s_delay_trace[0].reason == 1);
    reset_delay_test(); delay_tx(UINT32_MAX - 14999999U, 10); delay_tx(15000000, 10);
    CHECK(s_timing.delay_count == 1 && s_delay_trace[0].reason == 1);
    reset_delay_test(); delay_tx(0, UINT32_MAX - 14U); delay_tx(1, 15);
    CHECK(s_timing.delay_count == 1 && s_delay_trace[0].reason == 2);
    reset_delay_test(); delay_tx(0, 0); mock_time_result = -5; delay_tx(10, UINT32_MAX);
    CHECK(s_timing.delay_count == 0);
    mock_time_result = 0; delay_tx(20, 100);
    CHECK(s_timing.delay_count == 0); /* Previous failed OS sample is invalid too. */
    mock_time_result = -5; delay_tx(30000020, 1000);
    CHECK(s_timing.delay_count == 1 && s_delay_trace[0].reason == 1);
    CHECK(s_delay_trace[0].current.time_error == (uint32_t)-5);
    SystemCoreClock = 0; reset_delay_test();
    CHECK(s_timing.delay_threshold_cycles == 0);
    delay_tx(0, 0); delay_tx(1000000000, 0);
    CHECK(s_timing.delay_count == 0); /* Unknown clock disables DWT comparison. */
    delay_tx(1000000010, 30);
    CHECK(s_timing.delay_count == 1 && s_delay_trace[0].reason == 2 && s_delay_trace[0].clock == 0);
    SystemCoreClock = 1000000000U;

    /* A prolonged episode is recorded once, until a normal interval returns. */
    reset_delay_test(); delay_tx(0, 0); delay_tx(30000000, 30); delay_tx(60000000, 60);
    CHECK(s_timing.delay_count == 1);
    delay_tx(61000000, 61);
    CHECK(!s_timing.delay_latched);
    delay_tx(91000000, 91);
    CHECK(s_timing.delay_count == 2);
    delay_tx(92000000, 92); delay_tx(122000000, 122);
    delay_tx(123000000, 123); delay_tx(153000000, 153);
    CHECK(s_timing.delay_count == 4);
    /* Fill the other diagnostic arrays, isolating this path's extra MMIO. */
    s_timing.trace_count = AUDIO_TRACE_CAPACITY;
    s_timing.int_trace_count = AUDIO_INT_TRACE_CAPACITY;
    audio_delay_record_t saved[4];
    for (unsigned i = 0; i < 4; i++) saved[i] = s_delay_trace[i];
    uint32_t time_calls = mock_time_calls, tx_calls = real_tx_calls;
    for (unsigned i = 0; i < 5; i++) mock_reads[i] = 0;
    delay_tx(200000000, 200);
    CHECK(s_timing.delay_count == 4 && real_tx_calls == tx_calls + 1 && mock_time_calls == time_calls);
    CHECK(mock_reads[0] == 1); /* Existing TX FIFO observation remains. */
    for (unsigned i = 1; i < 5; i++) CHECK(mock_reads[i] == 0);
    for (unsigned i = 0; i < 4; i++) {
        const unsigned char *old = (const unsigned char *)&saved[i];
        const volatile unsigned char *now = (const volatile unsigned char *)&s_delay_trace[i];
        for (unsigned j = 0; j < sizeof(saved[i]); j++) CHECK(old[j] == now[j]);
    }

    /* Same-entry success cannot retract a delay record or replace its history. */
    reset_fallback_test(); mock_time_result = 0; mock_tx_notify = true;
    delay_tx(0, 0);
    R_ICU->IELSR_b[0].DTCE = 0; mock_descriptor.num_blocks = 0;
    delay_tx(30000000, 30);
    CHECK(s_timing.delay_count == 1 && s_delay_trace[0].success_ordinal == 1);
    CHECK(s_delay_success_ordinal == 2 && s_delay_success.cycles == 30000000);
    mock_int_notify = true; __wrap_ssi_int_isr(); /* Unexpected IDLE regenerates prefill. */
    CHECK(s_timing.recover_ok_count == 1 && !s_timing.delay_success_valid);

    /* Use actual native/fallback Write success paths to publish metadata. */
    reset_fallback_test(); mock_time_result = 0; mock_tx_notify = true;
    delay_tx(100, 100);
    CHECK(s_timing.delay_success_valid && s_delay_success_ordinal == 1);
    CHECK(s_delay_success.cycles == 100 && s_delay_success.ms == 100);
    CHECK(s_delay_success_index == 1 && s_delay_success_address == (uint32_t)(uintptr_t)&s_pcm_buf[1][0]);
    uint32_t native_kind = s_delay_success_kind;
    mock_tx_notify = false;
    delay_tx(10000100, 110); delay_tx(20000100, 120);
    CHECK(s_timing.delay_count == 0);
    R_SSI0->SSISR = 0x20000001; R_ICU->IELSR[0] = 0x01000123;
    R_DTC->DTCSTS = 0x8014; mock_descriptor.p_src = &s_pcm_buf[1][0];
    *(volatile uint32_t *)0x900720 = 1;
    delay_tx(30000100, 130); /* Short TX gaps, but no successful refill for 30ms. */
    CHECK(s_timing.delay_count == 1 && s_delay_trace[0].reason == 12);
    CHECK(s_delay_trace[0].success_ordinal == 1 && s_delay_trace[0].success_kind == native_kind);
    CHECK(s_delay_trace[0].success_index == 1 && s_delay_trace[0].success_address == (uint32_t)(uintptr_t)&s_pcm_buf[1][0]);
    CHECK(s_delay_trace[0].ssisr == 0x20000001 && s_delay_trace[0].ielsr == 0x01000123);
    CHECK(s_delay_trace[0].dtcsts == 0x8014 && s_delay_trace[0].descriptor_source == (uint32_t)(uintptr_t)&s_pcm_buf[1][0]);
    CHECK(*(volatile uint32_t *)0x900724 == 0x90000C); /* SSISR before other entry MMIO. */
    CHECK(s_delay_trace[0].current.cycles == 30000100 && s_delay_trace[0].success.cycles == 100);
    R_ICU->IELSR_b[0].DTCE = 0; R_DTC->DTCSTS = 0; mock_descriptor.num_blocks = 0;
    R_SSI0->SSISR = 0;
    delay_tx(40000100, 140); /* Success cannot retract the existing episode. */
    CHECK(s_timing.delay_count == 1 && s_timing.delay_success_valid);
    CHECK(s_delay_success_kind != native_kind && s_delay_success_index == 0);
    CHECK(s_delay_success_ordinal == 5 && s_delay_success.cycles == 40000100);
    delay_tx(50000100, 150);
    CHECK(!s_timing.delay_latched);
    /* Failed submission does not replace the last successful TX metadata. */
    R_ICU->IELSR_b[0].DTCE = 0; mock_descriptor.num_blocks = 0;
    write_result = MOCK_WRITE_ERROR; mock_tx_notify = true;
    delay_tx(60000100, 160);
    CHECK(s_delay_success_ordinal == 5 && s_delay_success_index == 0);
    /* IDLE clears success info even when it follows a requested stop. */
    mock_int_notify = true; __wrap_ssi_int_isr();
    CHECK(!s_timing.delay_success_valid);

    audio_delay_record_t copy = {0}; audio_timing_t snapshot = s_timing;
    CHECK(audio_delay_copy(s_timing_session, 0, &copy));
    CHECK(!audio_delay_copy(s_timing_session + 1, 0, &copy));
    CHECK(!audio_delay_copy(s_timing_session, snapshot.delay_count, &copy));
    output_len = output_calls = 0; output[0] = 0; change_session_on_print = 0;
    audio_delay_print(s_timing_session, &snapshot);
    CHECK(output_has("delay_valid=1"));
    uint32_t last_output = output_calls - 1;
    output_len = output_calls = 0; output[0] = 0; change_session_on_print = last_output;
    audio_delay_print(s_timing_session, &snapshot);
    CHECK(output_has("delay_valid=0"));
    output_len = output_calls = 0; output[0] = 0; change_session_on_print = 1;
    audio_delay_print(s_timing_session, &snapshot);
    CHECK(output_has("delay_valid=0"));
    change_session_on_print = 0; snapshot.irq_saturated = 1;
    output_len = output_calls = 0; output[0] = 0;
    audio_delay_print(s_timing_session, &snapshot);
    CHECK(output_has("delay_valid=0"));
    for (unsigned i = 0; i < sizeof(s_delay_trace[0]); i++) ((volatile unsigned char *)&s_delay_trace[0])[i] = 0xFF;
    s_timing_session = UINT32_MAX; snapshot.delay_count = 1;
    snapshot.delay_threshold_cycles = UINT32_MAX;
    print_truncated = 0; audio_delay_print(s_timing_session, &snapshot);
    CHECK(print_truncated == 0 && print_while_masked == 0);
    uint32_t saved_ordinal = s_delay_trace[0].ordinal;
    reset_delay_test();
    CHECK(s_timing.delay_count == 0 && !s_timing.delay_latched && !s_timing.delay_prev_valid);
    CHECK(!s_timing.delay_current_valid && !s_timing.delay_success_valid);
    CHECK(s_delay_trace[0].ordinal == saved_ordinal && !audio_delay_copy(s_timing_session, 0, &copy));
    snapshot = s_timing; output_len = output_calls = 0; output[0] = 0;
    audio_delay_print(s_timing_session, &snapshot); CHECK(output_has("delay_valid=1"));
    output_len = output_calls = 0; output[0] = 0; change_session_on_print = 1;
    audio_delay_print(s_timing_session, &snapshot); CHECK(output_has("delay_valid=0"));
    reset_fallback_test(); mock_time_ms = 1234; mock_time_result = 0;
    return 0;
}
static int test_provenance(void) {
    CHECK(sizeof(audio_prov_snapshot_t) == 48 && sizeof(audio_prov_header_t) <= 48);
    CHECK(sizeof(audio_provenance_t) <= 192);
    CHECK(sizeof(audio_prov_snapshot_t) + 6 * sizeof(audio_provenance_t) + 9 * sizeof(uint32_t) <= 1280);
    for (unsigned native = 0; native < 2; native++) {
        reset_fallback_test(); mock_time_result = 0; mock_time_ms = 0;
        mock_tx_notify = native != 0;
        mock_descriptor.p_src = (void *)0x12340000; mock_descriptor.length = 0x0102;
        mock_marker = 0; mock_prov_observations[0] = 0; mock_prov_monitor = 1;
        begin_tie_monitor(); delay_tx(100, 0);
        mock_monitor = mock_prov_monitor = 0;
        CHECK(real_tx_calls == 1 && writes == 1 && fills == 1 && s_timing.prov_last_valid);
        CHECK(s_prov_last.header.valid && s_prov_last.header.ordinal == 1);
        CHECK(s_prov_last.header.kind == (native ? 1U : 2U));
        CHECK(s_prov_last.header.index == 1 && s_prov_last.header.address == (uint32_t)(uintptr_t)&s_pcm_buf[1][0]);
        CHECK(s_prov_last.header.bytes == AUDIO_BUFFER_BYTES && s_prov_last.header.blocks == AUDIO_BUFFER_FRAMES);
        CHECK(s_prov_last.header.write_error == FSP_SUCCESS);
        CHECK(s_prov_last.entry.sar == 0x12340000 && s_prov_last.pre.sar == 0x12340000);
        CHECK(s_prov_last.entry.crb_begin == 0 && s_prov_last.pre.crb_end == 0);
        CHECK(s_prov_last.post.sar == (uint32_t)(uintptr_t)&s_pcm_buf[1][0]);
        CHECK(s_prov_last.post.crb_begin == 81 && s_prov_last.post.crb_end == 81);
        CHECK(s_prov_last.pre.cra == 0x0102 && s_prov_last.post.cra == 2);
        CHECK(s_prov_last.pre.settings == mock_descriptor.transfer_settings_word);
        CHECK(s_prov_last.entry.cycles_begin == 100 && s_prov_last.post.cycles_begin == 102);
        CHECK(mock_prov_observations[0] == 3);
        /* SAR read identifies each sample; ENTRY TIE1, PRE and POST TIE0.
         * POST follows Write return marker 3, before reenable/producer. */
        CHECK(mock_prov_observations[1] == (MOCK_OTHER_FCR_BITS | 8U) && mock_prov_observations[2] == 0);
        CHECK(mock_prov_observations[3] == MOCK_OTHER_FCR_BITS && mock_prov_observations[4] == 0);
        CHECK(mock_prov_observations[5] == MOCK_OTHER_FCR_BITS && mock_prov_observations[6] == 3);
        CHECK(fill_fcr == (MOCK_OTHER_FCR_BITS | 8U));
        CHECK(s_timing.fallback_attempt == (native ? 0U : 1U));

        audio_provenance_t last = s_prov_last;
        R_ICU->IELSR_b[0].DTCE = 0; mock_descriptor.num_blocks = 0;
        write_result = MOCK_WRITE_ERROR; delay_tx(10000100, 10);
        CHECK(writes == 2 && fills == 1 && s_timing.prov_last_valid);
        CHECK(s_prov_candidate.header.write_error == MOCK_WRITE_ERROR);
        for (unsigned i = 0; i < sizeof(last); i++)
            CHECK(((const unsigned char *)&last)[i] == ((const volatile unsigned char *)&s_prov_last)[i]);
    }

    /* A hardware change inside a sequential sample remains visible as a
     * differing begin/end pair; never coerce it into an atomic snapshot. */
    reset_fallback_test(); mock_descriptor.num_blocks = 17;
    mock_descriptor.p_src = (void *)0x12340000;
    mock_prov_monitor = 1; mock_prov_change = 1; mock_prov_observations[0] = 0;
    audio_prov_snapshot_t moving;
    audio_prov_sample(&moving);
    mock_prov_monitor = mock_prov_change = 0;
    CHECK(moving.crb_begin == 17 && moving.crb_end == 16 && moving.sar == 0x12340004);

    /* Delay detection freezes OLD success before current ENTRY and success. */
    reset_fallback_test(); mock_tx_notify = true; mock_time_result = 0;
    delay_tx(0, 0);
    R_ICU->IELSR_b[0].DTCE = 0; mock_descriptor.num_blocks = 0;
    delay_tx(30000000, 30);
    CHECK(s_timing.delay_count == 1 && s_prov_delay[0].header.valid);
    CHECK(s_prov_delay[0].header.ordinal == 1 && s_delay_trace[0].success_ordinal == 1);
    CHECK(s_prov_last.header.ordinal == 2 && s_prov_delay[0].entry.cycles_begin == 0);
    CHECK(s_prov_delay[0].header.index == 1 && s_prov_last.header.index == 0);
    audio_provenance_t frozen = s_prov_delay[0];
    R_ICU->IELSR_b[0].DTCE = 0; mock_descriptor.num_blocks = 0;
    delay_tx(40000000, 40);
    for (unsigned i = 0; i < sizeof(frozen); i++)
        CHECK(((const unsigned char *)&frozen)[i] == ((const volatile unsigned char *)&s_prov_delay[0])[i]);

    audio_prov_header_t header; audio_prov_snapshot_t piece;
    CHECK(audio_prov_header_copy(s_timing_session, 0, 1, &header) && header.valid);
    for (unsigned i = 0; i < 3; i++) CHECK(audio_prov_snapshot_copy(s_timing_session, 0, 1, i, &piece));
    CHECK(!audio_prov_header_copy(s_timing_session, 0, 2, &header));
    CHECK(!audio_prov_snapshot_copy(s_timing_session, 0, 2, 0, &piece));
    CHECK(!audio_prov_snapshot_copy(s_timing_session, 0, 1, 3, &piece));
    CHECK(!audio_prov_header_copy(s_timing_session, 1, 1, &header));
    uint32_t session = s_timing_session;
    CHECK(audio_prov_header_copy(session, 0, 1, &header));
    CHECK(audio_prov_snapshot_copy(session, 0, 1, 0, &piece));
    s_timing_session++;
    CHECK(!audio_prov_snapshot_copy(session, 0, 1, 1, &piece));
    CHECK(!audio_prov_header_copy(session, 0, 1, &header));
    s_timing_session = session;
    audio_timing_t snapshot = s_timing;
    output_len = output_calls = 0; output[0] = 0; change_session_on_print = 0;
    audio_prov_print(session, &snapshot);
    CHECK(output_has("provenance_valid=1"));
    uint32_t final_output = output_calls - 1;
    for (unsigned change = 1; change <= final_output; change++) {
        output_len = output_calls = 0; output[0] = 0;
        change_session_on_print = change;
        audio_prov_print(s_timing_session, &snapshot);
        CHECK(output_has("provenance_valid=0") && irq_disabled == 0);
    }
    change_session_on_print = 0; snapshot.irq_saturated = 1;
    output_len = output_calls = 0; output[0] = 0;
    audio_prov_print(s_timing_session, &snapshot); CHECK(output_has("provenance_valid=0"));

    /* Full diagnostics stop sampling but never suppress a safe native Write. */
    s_timing.delay_count = AUDIO_DELAY_CAPACITY; s_timing.trace_count = AUDIO_TRACE_CAPACITY;
    s_timing.int_trace_count = AUDIO_INT_TRACE_CAPACITY;
    uint32_t before_writes = writes, before_fills = fills;
    R_ICU->IELSR_b[0].DTCE = 0; mock_descriptor.num_blocks = 0;
    mock_prov_observations[0] = 0; mock_prov_monitor = 1;
    delay_tx(50000000, 50);
    mock_prov_monitor = 0;
    CHECK(writes == before_writes + 1 && fills == before_fills + 1);
    CHECK(mock_prov_observations[0] == 0 && !s_timing.prov_entry_valid);
    CHECK(s_prov_last.header.ordinal == 3); /* Full freezes rolling success too. */
    for (unsigned i = 0; i < 5; i++) mock_reads[i] = 0;
    audio_prov_entry(); audio_prov_prepare(false, 0); audio_prov_post(FSP_SUCCESS); audio_prov_commit();
    for (unsigned i = 0; i < 5; i++) CHECK(mock_reads[i] == 0);

    /* No TX ENTRY means no new provenance, without changing refill control. */
    reset_fallback_test(); audio_refill(false);
    CHECK(writes == 1 && fills == 1 && !s_timing.prov_last_valid);
    CHECK(!s_prov_candidate.header.valid);
    reset_fallback_test(); mock_tx_notify = true; delay_tx(0, 0);
    mock_int_notify = true; __wrap_ssi_int_isr();
    CHECK(!s_timing.prov_last_valid && !s_timing.prov_entry_valid);
    reset_fallback_test();
    CHECK(!s_timing.prov_last_valid && !s_timing.prov_entry_valid);
    CHECK(s_timing.prov_entry_max == 0 && s_timing.prov_pre_max == 0 && s_timing.prov_post_max == 0 && s_timing.prov_aux_max == 0);

    /* Maximal fields, including session/ordinals, must fit actual buffers. */
    for (unsigned i = 0; i < sizeof(s_prov_delay[0]); i++) ((volatile unsigned char *)&s_prov_delay[0])[i] = 0xFF;
    s_delay_trace[0].success_ordinal = UINT32_MAX; s_delay_trace[0].success_valid = 1;
    s_timing.delay_count = 1; s_timing_session = UINT32_MAX;
    snapshot = s_timing; print_truncated = 0;
    audio_prov_print(s_timing_session, &snapshot);
    CHECK(print_truncated == 0 && print_while_masked == 0);
    /* A real start between bounded copies hides the prior session's chunks. */
    session = s_timing_session;
    CHECK(audio_prov_header_copy(session, 0, UINT32_MAX, &header));
    CHECK(audio_prov_snapshot_copy(session, 0, UINT32_MAX, 0, &piece));
    s_state = AUDIO_STATE_READY;
    CHECK(audio_start(NULL, NULL) == FSP_SUCCESS);
    CHECK(!audio_prov_header_copy(session, 0, UINT32_MAX, &header));
    CHECK(!audio_prov_snapshot_copy(session, 0, UINT32_MAX, 1, &piece));
    CHECK(!s_timing.prov_last_valid && !s_timing.prov_entry_valid && s_timing.delay_count == 0);
    output_len = output_calls = 0; output[0] = 0;
    audio_prov_print(session, &snapshot); CHECK(output_has("provenance_valid=0"));
    reset_fallback_test(); mock_time_ms = 1234;
    return 0;
}
static int test_provenance_modes(void) {
    char *full[] = {"audio", "prov", "full"};
    char *nowindow[] = {"audio", "prov", "nowindow"};
    char *invalid[] = {"audio", "prov", "bogus", "extra"};
    CHECK(sizeof(audio_prov_header_t) == 36 && sizeof(audio_provenance_t) == 180);
    CHECK(s_prov_configured_mode == AUDIO_PROV_FULL);
    reset_fallback_test();
    for (int state = AUDIO_STATE_READY; state <= AUDIO_STATE_ERROR; state++) {
        if (state == AUDIO_STATE_READY) continue;
        s_state = state;
        CHECK(usrcmd_audio(3, nowindow) != CMD_OK);
        CHECK(s_prov_configured_mode == AUDIO_PROV_FULL && !irq_disabled);
    }
    s_state = AUDIO_STATE_READY;
    CHECK(usrcmd_audio(2, nowindow) == CMD_ERR_USAGE);
    CHECK(usrcmd_audio(4, invalid) == CMD_ERR_USAGE);
    CHECK(usrcmd_audio(3, invalid) == CMD_ERR_INVALID_ARG);
    CHECK(s_prov_configured_mode == AUDIO_PROV_FULL);
    CHECK(usrcmd_audio(3, nowindow) == CMD_OK);
    CHECK(s_prov_configured_mode == AUDIO_PROV_NOWINDOW && s_timing.prov_session_mode == AUDIO_PROV_FULL);
    CHECK(audio_start(NULL, NULL) == FSP_SUCCESS);
    CHECK(s_timing.prov_session_mode == AUDIO_PROV_NOWINDOW);
    CHECK(usrcmd_audio(3, full) != CMD_OK && s_prov_configured_mode == AUDIO_PROV_NOWINDOW);
    /* Selecting the next mode while READY must not rewrite session metadata. */
    s_state = AUDIO_STATE_READY;
    CHECK(usrcmd_audio(3, full) == CMD_OK);
    CHECK(s_prov_configured_mode == AUDIO_PROV_FULL && s_timing.prov_session_mode == AUDIO_PROV_NOWINDOW);
    CHECK(audio_start(NULL, NULL) == FSP_SUCCESS && s_timing.prov_session_mode == AUDIO_PROV_FULL);

    for (unsigned native = 0; native < 2; native++) {
        s_state = AUDIO_STATE_READY;
        CHECK(usrcmd_audio(3, nowindow) == CMD_OK);
        reset_fallback_test(); mock_tx_notify = native != 0; mock_time_result = 0;
        s_prov_candidate.pre.sar = 0xDEADBEEF; s_prov_candidate.post.sar = 0xBAADF00D;
        mock_marker = 0; mock_prov_observations[0] = 0; mock_prov_monitor = 1;
        begin_tie_monitor(); delay_tx(0, 0);
        mock_monitor = mock_prov_monitor = 0;
        CHECK(s_timing.prov_session_mode == AUDIO_PROV_NOWINDOW && mock_prov_observations[0] == 1);
        CHECK(writes == 1 && fills == 1 && real_tx_calls == 1);
        CHECK(mock_events[0] == 5);
        for (unsigned i = 1; i <= 5; i++) CHECK(mock_events[i] == i);
        CHECK(write_fcr == MOCK_OTHER_FCR_BITS && write_readback == 1);
        CHECK(fill_fcr == (MOCK_OTHER_FCR_BITS | 8U));
        CHECK(s_prov_last.header.phase_mask == AUDIO_PROV_ENTRY);
        CHECK(s_prov_last.header.kind == (native ? 1U : 2U));
        CHECK(s_prov_last.pre.sar == 0xDEADBEEF && s_prov_last.post.sar == 0xBAADF00D);
        CHECK(s_timing.prov_pre_max == 0 && s_timing.prov_post_max == 0);
        CHECK(s_timing.fallback_attempt == (native ? 0U : 1U));
        R_ICU->IELSR_b[0].DTCE = 0; mock_descriptor.num_blocks = 0;
        delay_tx(30000000, 30);
        CHECK(s_timing.delay_count == 1 && s_prov_delay[0].header.phase_mask == AUDIO_PROV_ENTRY);
        audio_prov_snapshot_t sample;
        CHECK(audio_prov_snapshot_copy(s_timing_session, 0, 1, 0, &sample));
        CHECK(!audio_prov_snapshot_copy(s_timing_session, 0, 1, 1, &sample));
        CHECK(!audio_prov_snapshot_copy(s_timing_session, 0, 1, 2, &sample));
        output_len = output_calls = 0; output[0] = 0; change_session_on_print = 0;
        audio_timing_t timing = s_timing;
        audio_prov_print(s_timing_session, &timing);
        CHECK(output_has("disabled") && output_has("provenance_valid=1"));
        CHECK(!output_has("DEADBEEF") && !output_has("BAADF00D"));
        /* Missing data is masked even if the next configured mode is full. */
        s_state = AUDIO_STATE_READY; CHECK(usrcmd_audio(3, full) == CMD_OK);
        output_len = output_calls = 0; output[0] = 0;
        audio_prov_print(s_timing_session, &timing);
        CHECK(output_has("disabled") && !output_has("DEADBEEF") && !output_has("BAADF00D"));
        s_state = AUDIO_STATE_PLAYING;

        /* Failure still records the return value without POST sampling. */
        audio_provenance_t old = s_prov_last;
        R_ICU->IELSR_b[0].DTCE = 0; mock_descriptor.num_blocks = 0;
        write_result = MOCK_WRITE_ERROR;
        mock_prov_observations[0] = 0; mock_prov_monitor = 1;
        delay_tx(40000000, 40); mock_prov_monitor = 0;
        CHECK(mock_prov_observations[0] == 1 && writes == 3 && fills == 2 && stops == 1);
        CHECK(s_prov_candidate.header.write_error == MOCK_WRITE_ERROR);
        CHECK(stop_fcr == MOCK_OTHER_FCR_BITS && s_stop_request);
        for (unsigned i = 0; i < sizeof(old); i++)
            CHECK(((const unsigned char *)&old)[i] == ((const volatile unsigned char *)&s_prov_last)[i]);
    }
    /* Capacity still disables ENTRY in nowindow while transfer keeps working. */
    s_state = AUDIO_STATE_READY; CHECK(usrcmd_audio(3, nowindow) == CMD_OK);
    reset_fallback_test(); mock_tx_notify = true;
    s_timing.delay_count = AUDIO_DELAY_CAPACITY;
    mock_prov_monitor = 1; mock_prov_observations[0] = 0;
    delay_tx(0, 0); mock_prov_monitor = 0;
    CHECK(writes == 1 && fills == 1 && mock_prov_observations[0] == 0);
    s_state = AUDIO_STATE_READY; CHECK(usrcmd_audio(3, full) == CMD_OK);
    reset_fallback_test(); mock_tx_notify = true; delay_tx(0, 0);
    CHECK(s_timing.prov_session_mode == AUDIO_PROV_FULL);
    CHECK(s_prov_last.header.phase_mask == (AUDIO_PROV_ENTRY | AUDIO_PROV_PRE | AUDIO_PROV_POST));
    CHECK(print_while_masked == 0 && irq_disabled == 0);
    reset_fallback_test(); mock_time_ms = 1234;
    return 0;
}
static int test_fallback(void) {
    /* Every individual precondition must reject without Write or generation. */
    for (unsigned guard = 0; guard < 17; guard++) {
        reset_fallback_test();
        switch (guard) {
            case 0: s_ssi_open = false; break;
            case 1: s_state = AUDIO_STATE_STOPPING; break;
            case 2: s_stop_request = true; break;
            case 3: R_SSI0->SSICR_b.TEN = 0; break;
            case 4: R_SSI0->SSIFCR_b.TIE = 0; break;
            case 5: g_i2s_audio_ctrl.p_tx_src = s_pcm_buf; break;
            case 6: g_i2s_audio_ctrl.tx_src_samples = 1; break;
            case 7: R_ICU->IELSR_b[0].DTCE = 1; break;
            case 8: R_DTC->DTCSTS = 0x8000; break;
            case 9: mock_descriptor.num_blocks = 1; break;
            case 10: mock_descriptor.transfer_settings_word_b.mode = 0; break;
            case 11: mock_descriptor.transfer_settings_word_b.irq = 0; break;
            case 12: mock_descriptor.transfer_settings_word_b.chain_mode = 1; break;
            case 13: R_SSI0->SSIFSR_b.TDC = 0; break;
            case 14: R_SSI0->SSIFSR_b.TDC = 17; break;
            case 15: R_SSI0->SSISR_b.TUIRQ = 1; break;
            case 16: g_transfer_i2s_tx_cfg.p_info = NULL; break;
        }
        audio_fallback_refill();
        CHECK(writes == 0 && fills == 0 && stops == 0);
        CHECK(s_timing.fallback_attempt == 0);
        if (guard == 0) {
            CHECK(mock_reads[0] == 0 && mock_reads[1] == 0 && mock_reads[2] == 0);
            CHECK(mock_reads[3] == 0 && mock_reads[4] == 0);
        }
    }
    /* The shared helper itself rejects a late state/stop change as well. */
    reset_fallback_test(); s_state = AUDIO_STATE_ERROR; audio_refill(true);
    CHECK(writes == 0 && fills == 0 && s_timing.fallback_attempt == 0);
    s_state = AUDIO_STATE_PLAYING; s_stop_request = true; audio_refill(true);
    CHECK(writes == 0 && fills == 0 && s_timing.fallback_attempt == 0);

    /* Guard must observe current FIFO, not the trace's entry value. */
    reset_fallback_test(); mock_override_tdc = true; mock_irq_tdc = 0;
    __wrap_ssi_txi_isr();
    CHECK(s_trace[0].pre_tdc == 16 && s_trace[0].post_tdc == 0 && writes == 0);
    reset_fallback_test(); R_SSI0->SSIFSR_b.TDC = 0;
    mock_override_tdc = true; mock_irq_tdc = 1;
    __wrap_ssi_txi_isr();
    CHECK(s_trace[0].pre_tdc == 0 && s_trace[0].post_tdc == 1 && writes == 1);

    reset_fallback_test();
    uint32_t native = s_tx_empty_count;
    __wrap_ssi_txi_isr();
    CHECK(real_tx_calls == 1 && writes == 1 && fills == 1);
    CHECK(s_play_idx == 1 && last_written == 1 && last_filled == 0);
    CHECK(s_timing.fallback_attempt == 1 && s_timing.fallback_ok == 1 && s_timing.refill_ok == 1);
    CHECK(s_tx_empty_count == native && s_timing.tx_no_callback == 1);
    CHECK(s_trace[0].post_dtce == 0 && s_trace[0].post_descriptor_blocks == 0);
    CHECK(R_ICU->IELSR_b[0].DTCE == 1 && mock_descriptor.num_blocks == 81);
    __wrap_ssi_txi_isr(); /* Residual CPU IRQ while the newly armed DTC runs. */
    CHECK(real_tx_calls == 2 && writes == 1 && fills == 1);
    mock_tx_notify = true; R_SSI0->SSIFSR_b.TDC = 24;
    __wrap_ssi_txi_isr(); /* Pending native notification does not prove DTC completed. */
    CHECK(s_tx_empty_count == native + 1 && writes == 1 && fills == 1 && s_play_idx == 1);
    CHECK(s_timing.fallback_attempt == 1 && s_timing.refill_ok == 1);
    mock_tx_notify = false; R_SSI0->SSIFSR_b.TDC = 16;
    R_ICU->IELSR_b[0].DTCE = 0; mock_descriptor.num_blocks = 0;
    R_DTC->DTCSTS = 0x8001; /* A different DTC vector is not our transfer. */
    __wrap_ssi_txi_isr();
    CHECK(writes == 2 && fills == 2 && s_play_idx == 0);
    CHECK(last_written == 0 && last_filled == 1 && s_timing.refill_ok == 2);
    mock_tx_notify = true; R_SSI0->SSIFSR_b.TDC = 24;
    R_ICU->IELSR_b[0].DTCE = 0; mock_descriptor.num_blocks = 0;
    __wrap_ssi_txi_isr(); /* Completed DTC plus real callback => one normal refill. */
    CHECK(s_tx_empty_count == native + 2 && writes == 3 && fills == 3);
    CHECK(s_timing.fallback_attempt == 2 && s_timing.refill_ok == 3);

    /* Native callbacks independently enforce the same completion conditions. */
    for (unsigned guard = 0; guard < 6; guard++) {
        reset_fallback_test(); mock_tx_notify = true; R_SSI0->SSIFSR_b.TDC = 24;
        switch (guard) {
            case 0: R_ICU->IELSR_b[0].DTCE = 1; break;
            case 1: R_DTC->DTCSTS = 0x8000; break;
            case 2: mock_descriptor.num_blocks = 1; break;
            case 3: mock_descriptor.transfer_settings_word_b.mode = 0; break;
            case 4: mock_descriptor.transfer_settings_word_b.irq = 0; break;
            case 5: mock_descriptor.transfer_settings_word_b.chain_mode = 1; break;
        }
        uint32_t cb_before = s_tx_empty_count;
        __wrap_ssi_txi_isr();
        CHECK(s_tx_empty_count == cb_before + 1 && s_timing.tx_no_callback == 0);
        CHECK(writes == 0 && fills == 0 && s_play_idx == 0);
        CHECK(s_timing.fallback_attempt == 0 && s_timing.refill_ok == 0);
    }

    reset_fallback_test();
    mock_during_write_underflow = true;
    __wrap_ssi_txi_isr();
    CHECK(writes == 1 && fills == 0 && stops == 1 && s_play_idx == 0);
    CHECK(mock_descriptor.num_blocks == 81); /* Failure still reprogrammed DTC. */
    CHECK(s_timing.fallback_underflow == 1 && s_timing.fallback_ok == 0);
    CHECK(s_timing.refill_ok == 0 && s_last_error == FSP_ERR_UNDERFLOW && s_error_count == 1);
    CHECK(s_state == AUDIO_STATE_PLAYING && !s_stop_request);
    __wrap_ssi_txi_isr(); /* Stops cannot be undone by fallback before IDLE. */
    CHECK(writes == 1 && fills == 0);
    mock_during_write_underflow = false; R_SSI0->SSISR_b.TUIRQ = 0;
    mock_int_notify = true; __wrap_ssi_int_isr();
    CHECK(writes == 2 && fills == 2 && s_timing.recover_ok_count == 1);
    CHECK(s_state == AUDIO_STATE_PLAYING && s_timing.refill_ok == 0);

    /* API failure, each readback failure, and DTC-active all fail closed. */
    for (unsigned failure = 0; failure < 5; failure++) {
        reset_fallback_test(); mock_during_write_underflow = true;
        if (failure == 0) mock_stop_result = MOCK_WRITE_ERROR;
        else mock_stop_fail_mask = 1U << (failure - 1);
        __wrap_ssi_txi_isr();
        CHECK(writes == 1 && fills == 0 && stops == 1);
        CHECK(s_state == AUDIO_STATE_ERROR && s_stop_request);
        CHECK(s_timing.fallback_stop_fail == 1);
        mock_int_notify = true; mock_during_write_underflow = false;
        __wrap_ssi_int_isr();
        CHECK(s_state == AUDIO_STATE_ERROR && writes == 1 && fills == 0);
        mock_tx_notify = true; __wrap_ssi_txi_isr();
        CHECK(s_state == AUDIO_STATE_ERROR && writes == 1 && fills == 0);
    }
    reset_fallback_test(); write_result = MOCK_WRITE_ERROR;
    __wrap_ssi_txi_isr();
    CHECK(s_timing.fallback_fail == 1 && s_timing.fallback_underflow == 0);
    CHECK(s_stop_request && writes == 1 && fills == 0 && stops == 1);
    mock_int_notify = true; __wrap_ssi_int_isr();
    CHECK(writes == 1 && fills == 0);

    /* Saturation invalidates diagnostics, never the safety-checked refill. */
    reset_fallback_test(); s_timing.irq_saturated = 1;
    s_timing.fallback_attempt = s_timing.fallback_ok = s_timing.refill_ok = UINT32_MAX - 1;
    __wrap_ssi_txi_isr();
    CHECK(writes == 1 && fills == 1 && s_timing.refill_ok == UINT32_MAX);
    R_ICU->IELSR_b[0].DTCE = 0; mock_descriptor.num_blocks = 0;
    __wrap_ssi_txi_isr();
    CHECK(writes == 2 && fills == 2 && s_timing.refill_ok == UINT32_MAX);
    CHECK(s_timing.fallback_attempt == UINT32_MAX && s_timing.fallback_ok == UINT32_MAX);
    /* Successful refill interval is modulo DWT, excludes prefill/recovery. */
    reset_fallback_test(); DWT->CYCCNT = UINT32_MAX - 3;
    audio_fallback_refill();
    CHECK(s_timing.refill_gap_max == 0 && s_timing.refill_have == 1);
    R_ICU->IELSR_b[0].DTCE = 0; mock_descriptor.num_blocks = 0;
    DWT->CYCCNT = 20; audio_fallback_refill();
    CHECK(s_timing.refill_gap_max == 24);
    R_ICU->IELSR_b[0].DTCE = 0; mock_descriptor.num_blocks = 0;
    DWT->CYCCNT = 30; audio_fallback_refill();
    CHECK(s_timing.refill_gap_max == 24);

    /* Error counters also saturate without wrapping. */
    for (unsigned kind = 0; kind < 3; kind++) {
        reset_fallback_test();
        volatile uint32_t *counter = kind == 0 ? &s_timing.fallback_underflow :
                                     kind == 1 ? &s_timing.fallback_fail : &s_timing.fallback_stop_fail;
        *counter = UINT32_MAX - 1;
        if (kind == 1) write_result = MOCK_WRITE_ERROR;
        else mock_during_write_underflow = true;
        if (kind == 2) mock_stop_result = MOCK_WRITE_ERROR;
        audio_fallback_refill();
        CHECK(*counter == UINT32_MAX && s_timing.irq_saturated == 1);
    }
    reset_fallback_test();
    CHECK(s_timing.refill_ok == 0 && s_timing.fallback_attempt == 0 && s_timing.fallback_ok == 0);
    CHECK(s_timing.fallback_underflow == 0 && s_timing.fallback_fail == 0 && s_timing.fallback_stop_fail == 0);
    CHECK(s_timing.refill_have == 0 && s_timing.refill_gap_max == 0);
    return 0;
}

int run_tests(void) {
    /* Earlier timing tests model completed, configured DTC at every notification. */
    s_ssi_open = true; R_SSI0->SSICR_b.TEN = 1; R_SSI0->SSIFCR_b.TIE = 1;
    mock_descriptor.transfer_settings_word_b.mode = TRANSFER_MODE_BLOCK;
    mock_descriptor.transfer_settings_word_b.irq = TRANSFER_IRQ_END;
    mock_descriptor.transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_DISABLED;
    i2s_callback_args_t tx = {I2S_EVENT_TX_EMPTY}, idle = {I2S_EVENT_IDLE};
    /* Start uses real prefill/reset code; prefill cost must not be ISR cost. */
    s_state = AUDIO_STATE_READY; fill_cycles = 11; write_cycles = 7;
    s_timing.gap_max = 99; s_timing.tx_fill_max = 99;
    DWT->CYCCNT = 100;
    CHECK(audio_start(NULL, NULL) == FSP_SUCCESS);
    CHECK(fills == 2 && writes == 1 && DWT->CYCCNT == 129);
    CHECK(s_timing.tx_fill_max == 0 && s_timing.idle_fill_max == 0);
    CHECK(s_timing.gap_max == 0 && s_timing.have_tx == 0);
    CHECK(s_timing_session == 1 && s_timing_clock == SystemCoreClock);
    CHECK(s_start_ms == 1234 && irq_disabled == 0);
    CHECK(DWT->CTRL == 1 && CoreDebug->DEMCR == 1);

    /* A write crossing UINT32_MAX still takes seven cycles. */
    DWT->CYCCNT = UINT32_MAX - 3;
    audio_i2s_callback(&tx);
    CHECK(s_timing.tx_write_ok_max == 7 && s_timing.tx_fill_max == 11);
    CHECK(s_timing.gap_max == 0 && s_timing.have_tx == 1);
    CHECK(fills == 3 && s_play_idx == 1);
    DWT->CYCCNT = 20;
    audio_i2s_callback(&tx);
    CHECK(s_timing.gap_max == 24 && s_tx_empty_count == 2);
    /* A shorter later sample cannot reduce the maximum. */
    DWT->CYCCNT = 30; write_cycles = 2; fill_cycles = 3;
    audio_i2s_callback(&tx);
    CHECK(s_timing.gap_max == 24 && s_timing.tx_write_ok_max == 7);
    CHECK(s_timing.tx_fill_max == 11);

    write_result = MOCK_WRITE_ERROR; write_cycles = 19;
    uint32_t before_fills = fills;
    audio_i2s_callback(&tx);
    CHECK(s_timing.tx_write_fail_max == 19 && s_timing.tx_write_ok_max == 7);
    CHECK(fills == before_fills && s_stop_request && stops == 1);
    CHECK(s_error_count == 1 && s_last_error == MOCK_WRITE_ERROR);
    audio_i2s_callback(&idle); /* requested stop is not a recovery */
    CHECK(s_timing.recover_fail_count == 0 && s_timing.recover_ok_count == 0);
    CHECK(s_state == AUDIO_STATE_READY && flags == 1);

    s_state = AUDIO_STATE_PLAYING; write_result = FSP_SUCCESS;
    fill_cycles = 13; write_cycles = 17; DWT->CYCCNT = UINT32_MAX - 5;
    audio_i2s_callback(&idle);
    CHECK(s_timing.idle_fill_max == 13 && s_timing.idle_write_ok_max == 17);
    CHECK(s_timing.recover_ok_max == 43 && s_timing.recover_ok_count == 1);
    CHECK(s_state == AUDIO_STATE_PLAYING && s_restart_count == 1 && flags == 1);
    write_result = MOCK_WRITE_ERROR; write_cycles = 23;
    audio_i2s_callback(&idle);
    CHECK(s_timing.idle_write_fail_max == 23 && s_timing.recover_fail_max == 49);
    CHECK(s_timing.recover_fail_count == 1 && s_timing.recover_ok_count == 1);
    CHECK(s_state == AUDIO_STATE_READY && flags == 2 && s_error_count == 2);
    CHECK(s_timing.tx_fill_max == 11); /* IDLE and TX accounting are separate */

    write_result = FSP_SUCCESS;
    CHECK(audio_start(NULL, NULL) == FSP_SUCCESS);
    CHECK(s_timing_session == 2 && s_start_tx_empty == s_tx_empty_count);
    CHECK(s_timing.recover_ok_count == 0 && s_timing.recover_fail_count == 0);
    CHECK(s_timing.tx_write_fail_max == 0 && s_timing.idle_fill_max == 0);
    CHECK(s_timing.have_tx == 0 && s_timing.tx_fill_max == 0);
    int result = test_irq_wrappers();
    if (result) return result;
    result = test_trace();
    if (result) return result;
    result = test_int_trace();
    if (result) return result;
    result = test_delay_trace();
    if (result) return result;
    result = test_provenance();
    if (result) return result;
    result = test_provenance_modes();
    if (result) return result;
    result = test_tie_reissue();
    return result ? result : test_fallback();
}
"""


def main():
    sys.path.insert(0, os.environ.get("ISSUE206_TEST_DEPS", str(ROOT / "e2studio_CPU0/Debug/startup-screen/test-deps")))
    from unicorn import (Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_MEM_READ,
                         UC_HOOK_MEM_WRITE, UC_MEM_READ)
    from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
    source = SOURCE.read_text(encoding="utf-8")
    timing = re.search(r"typedef struct st_audio_timing\b.*?\} audio_timing_t;", source, re.S)
    if not timing:
        raise RuntimeError("Cannot extract timing structure")
    print_buffer = re.search(r"^#define AUDIO_PRINT_BUF_SIZE\s+.*$", source, re.M)
    if not print_buffer:
        raise RuntimeError("Cannot extract production print buffer size")
    mocks = re.sub(r"^#define AUDIO_PRINT_BUF_SIZE\s+.*$", print_buffer.group(0), MOCKS, flags=re.M)
    harness = mocks + timing.group(0) + "\nstatic volatile audio_timing_t s_timing;\n"
    trace = re.search(r"typedef struct st_audio_trace_record\b.*?\} audio_trace_record_t;", source, re.S)
    if not trace:
        raise RuntimeError("Cannot extract trace record structure")
    harness += trace.group(0) + "\n"
    harness += "\n".join(re.findall(r"^#define AUDIO_TRACE_.*$", source, re.M)) + "\n"
    harness += "static volatile audio_trace_record_t s_trace[AUDIO_TRACE_CAPACITY];\n"
    for typename in ("audio_tx_summary", "audio_int_record", "audio_delay_stamp", "audio_delay_record",
                     "audio_prov_snapshot", "audio_prov_header", "audio_provenance"):
        definition = re.search(r"typedef struct st_" + typename + r"\b.*?\} " + typename + r"_t;", source, re.S)
        if not definition:
            raise RuntimeError(f"Cannot extract {typename}")
        harness += definition.group(0) + "\n"
    harness += "\n".join(re.findall(r"^#define AUDIO_INT_.*$", source, re.M)) + "\n"
    harness += "static volatile audio_int_record_t s_int_trace[AUDIO_INT_TRACE_CAPACITY];\n"
    harness += "static volatile audio_tx_summary_t s_last_tx;\n"
    harness += "\n".join(re.findall(r"^#define AUDIO_DELAY_.*$", source, re.M)) + "\n"
    harness += "static volatile audio_delay_record_t s_delay_trace[AUDIO_DELAY_CAPACITY];\n"
    harness += "static audio_delay_stamp_t s_delay_current, s_delay_previous, s_delay_success;\n"
    harness += "static uint32_t s_delay_success_ordinal, s_delay_success_kind, s_delay_success_index, s_delay_success_address, s_delay_current_ordinal;\n"
    harness += "\n".join(re.findall(r"^#define AUDIO_PROV_.*$", source, re.M)) + "\n"
    harness += "static volatile audio_prov_snapshot_t s_prov_entry;\n"
    harness += "static volatile audio_provenance_t s_prov_candidate, s_prov_last, s_prov_delay[AUDIO_DELAY_CAPACITY];\n"
    harness += "static uint32_t s_prov_configured_mode = AUDIO_PROV_FULL;\n"
    harness += "static bool audio_delay_copy(uint32_t session, uint32_t index, audio_delay_record_t *record);\n"
    harness += "void __real_ssi_txi_isr(void);\nvoid __real_ssi_int_isr(void);\n"
    for name in ("audio_timing_max", "audio_irq_count", "audio_trace_pre", "audio_trace_post",
                 "audio_trace_publish", "audio_trace_copy", "audio_trace_print",
                 "audio_int_trace_pre", "audio_int_trace_publish", "audio_int_trace_copy", "audio_int_trace_print",
                 "audio_prov_sample", "audio_prov_entry", "audio_prov_prepare", "audio_prov_post", "audio_prov_commit", "audio_prov_freeze",
                 "audio_prov_header_copy", "audio_prov_snapshot_copy", "audio_prov_print",
                 "audio_delay_entry", "audio_delay_success", "audio_delay_copy", "audio_delay_print",
                 "audio_dtc_tx_active", "audio_refill_ready", "audio_fallback_ready", "audio_refill", "audio_fallback_refill",
                 "audio_i2s_callback", "audio_start",
                 "__wrap_ssi_txi_isr", "__wrap_ssi_int_isr", "audio_cmd_prov", "usrcmd_audio"):
        harness += function(source, name) + "\n"
    harness += TESTS
    with tempfile.TemporaryDirectory(prefix="issue206-timing-") as tmp:
        cfile, elf = Path(tmp) / "timing.c", Path(tmp) / "timing.elf"
        cfile.write_text(harness, encoding="utf-8")
        subprocess.run([CLANG, "--target=arm-none-eabi", "-mcpu=cortex-m4", "-mthumb", "-O1", "-mrestrict-it",
                        "-ffreestanding", "-fno-builtin", "-nostdlib", "-Wall", "-Wextra", "-Werror",
                        "-Wl,-Ttext=0x10000", "-Wl,-e,run_tests", str(cfile), "-o", str(elf)], check=True)
        data = elf.read_bytes()
        if data[:7] != b"\x7fELF\x01\x01\x01":
            raise RuntimeError("Expected little-endian ELF32")
        entry, phoff = struct.unpack_from("<II", data, 24)
        phsize, phnum = struct.unpack_from("<HH", data, 42)
        cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        cpu.mem_map(0, 0x1000000)
        for i in range(phnum):
            kind, offset, addr, _, size, memsize, _, _ = struct.unpack_from("<8I", data, phoff + phsize * i)
            if kind == 1:
                if addr + memsize >= 0xE00000:
                    raise RuntimeError("Image overlaps stack")
                cpu.mem_write(addr, data[offset:offset + size])
        def count_reads(uc, access, address, size, value, user):
            del access, size, value, user
            first_read, = struct.unpack("<I", uc.mem_read(0x900720, 4))
            if first_read == 1:
                uc.mem_write(0x900724, struct.pack("<I", address))
                uc.mem_write(0x900720, struct.pack("<I", 2))
            index = (address - 0x900000) // 0x100
            if index == 5:
                index = 4
            elif index >= 4:
                return
            if index < 5:
                counter = 0x900400 + index * 4
                old, = struct.unpack("<I", uc.mem_read(counter, 4))
                uc.mem_write(counter, struct.pack("<I", old + 1))
        cpu.hook_add(UC_HOOK_MEM_READ, count_reads, begin=0x900000, end=0x900501)
        monitor_state = {"clear": False, "fifo_reads": 0}
        def word(uc, addr):
            return struct.unpack("<I", uc.mem_read(addr, 4))[0]
        def tie_event(uc, access, address, size, value, user):
            del size, user
            if address == 0x900700 and access != UC_MEM_READ:
                monitor_state.update(clear=False, fifo_reads=0)
                return
            if not word(uc, 0x900700):
                return
            if access == UC_MEM_READ:
                if address == 0x900008 and monitor_state["clear"]:
                    uc.mem_write(0x900708, struct.pack("<I", 1))
                if address == 0x900000:
                    monitor_state["fifo_reads"] += 1
                    kind = word(uc, 0x90070C)
                    if kind and monitor_state["fifo_reads"] == 2:
                        if kind == 3:
                            uc.mem_write(0x90000C, struct.pack("<I", 1 << 29))
                        else:
                            uc.mem_write(address, struct.pack("<I", 0 if kind == 1 else 17 << 24))
                return
            if address == 0x900008:
                event = 4 if value & 8 else 1
                monitor_state["clear"] = event == 1
            elif address == 0x900704:
                event = value
                if event == 2:
                    monitor_state["clear"] = False
            else:
                return
            count = word(uc, 0x900600)
            if count >= 32:
                raise AssertionError("TIE event log overflow")
            uc.mem_write(0x900604 + count * 4, struct.pack("<I", event))
            uc.mem_write(0x900600, struct.pack("<I", count + 1))
        cpu.hook_add(UC_HOOK_MEM_READ, tie_event, begin=0x900000, end=0x900003)
        cpu.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, tie_event,
                     begin=0x900008, end=0x90000B)
        cpu.hook_add(UC_HOOK_MEM_WRITE, tie_event, begin=0x900700, end=0x900707)
        def provenance_read(uc, access, address, size, value, user):
            del access, address, size, value, user
            if not word(uc, 0x900730):
                return
            count = word(uc, 0x900800)
            if count >= 12:
                raise AssertionError("Provenance observation log overflow")
            uc.mem_write(0x900804 + count * 8,
                         struct.pack("<II", word(uc, 0x900008), word(uc, 0x900704)))
            uc.mem_write(0x900800, struct.pack("<I", count + 1))
            if word(uc, 0x900734):
                blocks, = struct.unpack("<H", uc.mem_read(0x90020C, 2))
                uc.mem_write(0x90020C, struct.pack("<H", blocks - 1))
                uc.mem_write(0x900204, struct.pack("<I", word(uc, 0x900204) + 4))
        cpu.hook_add(UC_HOOK_MEM_READ, provenance_read, begin=0x900204, end=0x900207)
        cpu.reg_write(UC_ARM_REG_SP, 0xF00000)
        cpu.reg_write(UC_ARM_REG_LR, 0x8001)
        cpu.emu_start(entry, 0x8000, timeout=10_000_000, count=10_000_000)
        if cpu.reg_read(UC_ARM_REG_PC) != 0x8000:
            raise RuntimeError("Execution limit exceeded")
        line = cpu.reg_read(UC_ARM_REG_R0)
        if line:
            lines = harness.splitlines()
            detail = lines[line - 1] if line <= len(lines) else "invalid CHECK line/return value"
            raise AssertionError(f"Generated harness line {line}: {detail}")
    print("PASS production audio timing: wrap, maxima, TX/IDLE outcomes, prefill exclusion, "
          "session reset; IRQ wrappers: exact calls, notification classification, streaks, "
          "counter wrap, immediate saturation and persistence; FIFO four bins, entry/exit values, "
          "trace order and 16/17 capacity, bounded MMIO reads, session invalidation, n/a post, reset; "
          "fallback: all guards, fresh FIFO, native counters, exact refill/swap, pending IRQ, "
          "UNDERFLOW recovery, Stop failures and ERROR isolation, saturation without playback suppression; "
          "TIE reissue: native/fallback MMIO/API/producer order, readback, other bits, "
          "TDE low/rising during Write, native-only statistics, requested-stop failures, "
          "fresh FIFO/TUIRQ, fail-closed; "
          "INT dedicated trace: independent capacity, pre-clear registers, last TX deltas, "
          "explicit stop, immutable full records/no extra MMIO, reset, session/saturation validity, "
          "maximum-width output in production print buffer; delay trace: 30ms boundary, "
          "DWT/OS disagreement and wraps, OS API failure, unknown clock, episode latch/rearm, "
          "four immutable records/no extra time calls or MMIO, success metadata, pre-ISR capture, "
          "same-entry success, IDLE/start invalidation, session/saturation validity, max-width display; "
          "handoff provenance: native/fallback ENTRY/PRE/POST order and TIE/Write boundaries, "
          "failed Write preserves history, changing CRB/source, old provenance freezes before new ENTRY, "
          "full skips sampling without suppressing playback, invalid ENTRY, IDLE/start reset, "
          "bounded copy association/session checks, real start between chunks, max display and static bounds; "
          "provenance modes: real shell READY/argument rejection, configured/session isolation, "
          "nowindow ENTRY-only sampling with unchanged TIE/Write/producer and failure behavior, "
          "phase-mask freeze/copy/missing-data display, capacity and next-start application")


if __name__ == "__main__":
    main()
