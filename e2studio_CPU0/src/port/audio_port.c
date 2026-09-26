/**
 * @file audio_port.c
 * @brief Audio output device layer: SSIE0 (I2S master) -> DA7212 -> J33 speaker
 * @details See audio_port.h for the module contract.
 *
 * Ping-pong (double) buffering
 * ----------------------------
 * R_SSI_Write() hands one whole buffer to the DTC: it calls
 * transfer_api_t::reset with num_blocks = samples/2
 * (ra/fsp/src/r_ssi/r_ssi.c:822-850). The DTC transfer info is configured
 * with TRANSFER_IRQ_END (ra_gen/hal_data.c:160), which the transfer API
 * documents as "DTC triggers the interrupt of the activation source. Choosing
 * TRANSFER_IRQ_END with DTC will prevent activation source interrupts until
 * the transfer is complete" (ra/fsp/inc/api/r_transfer_api.h:159-163). The
 * SSI0 TXI request is therefore consumed by the DTC without reaching the CPU
 * until the LAST block has been moved. At that point p_tx_src is NULL and
 * tx_src_samples is 0 (r_ssi.c:843-848). The ISR reports I2S_EVENT_TX_EMPTY
 * only while FIFO occupancy is above half (r_ssi.c:1187-1192); delayed ISR
 * service can therefore miss this refill condition. Before reusing a buffer
 * we verify DTC completion even on a native callback: a pending IRQ after
 * fallback refill can otherwise produce a callback for an active transfer.
 *
 * The callback therefore:
 *   I2S_EVENT_TX_EMPTY -> submit the other buffer, then refill the freed one
 *   I2S_EVENT_IDLE     -> playback/stop is complete (r_i2s_api.h:172,186-187)
 *
 * Cache
 * -----
 * The DTC reads the PCM buffers from SRAM. No cache maintenance is emitted
 * because BSP_CFG_DCACHE_ENABLED is 0 for CPU0 - a COMPILE-time constant
 * (ra_cfg/fsp_cfg/bsp/bsp_mcu_family_cfg.h:427-433) - which is also why the
 * rest of this project only does cache maintenance behind the same guard
 * (e.g. src/port/lvgl_port_mtk3.c:259-261,
 * src/port/dave2d_cache_management.c:61-62,83-84). The guard below keeps the
 * code correct if the D-cache is ever enabled.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <tk/tkernel.h>

#include "audio_port.h"
#include "da7212.h"
#include "i2c_bus0.h"
#include "jlink_console.h"
#include "cmd_utils.h"
#include "ntlibc.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size */
#define AUDIO_PRINT_BUF_SIZE        (144)

/** Event flag bit set from the SSI callback when I2S_EVENT_IDLE arrives */
#define AUDIO_EVT_IDLE              (1U << 0)

/** Timeout for audio_stop(): the SSI stops on the next frame boundary and the
 *  DTC still has at most one queued buffer, so 2 buffers + margin. */
#define AUDIO_STOP_TIMEOUT_MS       ((AUDIO_BUFFER_MS * 2U) + 50U)

/** Attempts (not retries) at unmuting the codec in audio_start(), and the gap
 *  between them. One transient I2C hiccup must not abort playback, but a
 *  persistent failure must not be reported as success either. */
#define AUDIO_UNMUTE_ATTEMPTS       (3U)
#define AUDIO_UNMUTE_RETRY_MS       (5)

/** Default built-in test tone (Hz) */
#define AUDIO_TEST_TONE_DEFAULT_HZ  (1000U)

#define AUDIO_TRACE_CAPACITY        (16U)
#define AUDIO_TRACE_TX_NO_CB        (1U)
#define AUDIO_TRACE_INT_IDLE        (2U)
#define AUDIO_TRACE_INT_NO_IDLE     (3U)
#define AUDIO_TRACE_POST_VALID      (1U)
#define AUDIO_INT_TRACE_CAPACITY    (8U)
#define AUDIO_DELAY_CAPACITY        (4U)
#define AUDIO_DELAY_MS              (30U)
#define AUDIO_DELAY_PREV_DWT        (1U)
#define AUDIO_DELAY_PREV_OS         (2U)
#define AUDIO_DELAY_SUCCESS_DWT     (4U)
#define AUDIO_DELAY_SUCCESS_OS      (8U)
#define AUDIO_PROV_FULL             (0U)
#define AUDIO_PROV_NOWINDOW         (1U)
#define AUDIO_PROV_ENTRY            (1U)
#define AUDIO_PROV_PRE              (2U)
#define AUDIO_PROV_POST             (4U)

/* Match r_dtc.c security-aware DTCSTS selection for this core. */
#if (1U == BSP_CFG_CPU_CORE)
 #define AUDIO_DTC_SECURITY_ATTRIBUTE (R_CPSCU->DTCSAR_b.DTCSTSA1)
#else
 #define AUDIO_DTC_SECURITY_ATTRIBUTE ((R_CPSCU->DTCSAR & R_CPSCU_DTCSAR_DTCSTSA_Msk) >> R_CPSCU_DTCSAR_DTCSTSA_Pos)
#endif

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/**
 * Ping-pong PCM buffers, interleaved L,R int16.
 *
 * 4-byte aligned because R_SSI_Write() documents "p_src ... Must be 4 byte
 * aligned" (ra/fsp/inc/api/r_i2s_api.h:190) and the DTC source address is
 * incremented from it.
 */
static int16_t s_pcm_buf[AUDIO_BUFFER_COUNT][AUDIO_BUFFER_SAMPLES] BSP_ALIGN_VARIABLE(4);

/** Index of the buffer currently handed to the DTC. */
static volatile uint32_t s_play_idx = 0;

/** Set by audio_stop(), consumed by the ISR on the next I2S_EVENT_TX_EMPTY. */
static volatile bool s_stop_request = false;

/** Device state. Written by task context and by the ISR (single word). */
static volatile audio_state_t s_state = AUDIO_STATE_UNINITIALIZED;

/** Last error recorded outside of a function return path. */
static volatile fsp_err_t s_last_error = FSP_SUCCESS;

/** Diagnostics */
static volatile uint32_t s_tx_empty_count = 0;
static volatile uint32_t s_idle_count     = 0;
static volatile uint32_t s_error_count    = 0;

/**
 * Number of times the stream was restarted from an UNSOLICITED I2S_EVENT_IDLE.
 *
 * An idle event that arrives while the state is still AUDIO_STATE_PLAYING and
 * no stop was requested means the SSI ran dry: ssi_int_isr() takes the error
 * branch on a transmit underflow, calls r_ssi_stop_sub() and returns WITHOUT
 * invoking any callback (ra/fsp/src/r_ssi/r_ssi.c:1263-1275); the peripheral
 * then goes idle and only that idle reaches us. The same ISR clears every
 * SSISR flag on entry (r_ssi.c:1252), so TUIRQ can no longer be read back -
 * this counter is the only underflow evidence available to the application.
 *
 * Suspected dry-out mechanism: after the DTC finishes a buffer the only slack
 * left is the 32-stage transmit FIFO (~1 ms at fs 16 kHz), and the SSI TX
 * interrupt is IPL 2 while uT-Kernel's disint() raises BASEPRI to
 * INTPRI_VAL(INTPRI_MAX_EXTINT_PRI) with INTPRI_MAX_EXTINT_PRI == 1
 * (mtk3_bsp2/include/sys/sysdepend/ra_fsp/cpu/ra8p1/sysdef.h:76,
 * mtk3_bsp2/mtkernel/lib/libtk/sysdepend/cpu/core/armv7m/int_armv7m.c:56-66),
 * i.e. every kernel critical section masks this ISR. The duration and source
 * of the blocking section have not been measured (Issue #206).
 */
static volatile uint32_t s_restart_count = 0;

/**
 * Wall-clock stamp (ms, tk_get_otm) taken by audio_start(), plus the counter
 * values at that moment. audio_cmd_status() turns the deltas into a MEASURED
 * buffer delivery rate, NOT an independent bit-clock measurement. The expected
 * rate is AUDIO_SAMPLE_RATE_HZ / AUDIO_BUFFER_FRAMES (not exactly 100 buf/s).
 */
static volatile uint32_t s_start_ms       = 0;
static volatile uint32_t s_start_tx_empty = 0;

/** Session timing, in DWT cycles. SSI TX/IDLE (both IPL 2) are the writers.
 * gap_max counts native callbacks; TX fill/write include native+fallback.
 * Task reset and snapshot use DI/EI, not dispatch-disable. No ISR printing,
 * division or waits. Delay capture adds bounded tk_get_otm calls until full.
 * Prefill is deliberately not timed.
 * Each measured interval must be shorter than one CYCCNT wrap (~4.29 s at
 * 1 GHz); debugger stops and clock changes invalidate the trial. IDLE timing
 * starts AFTER the driver notified us and is NOT the total lost audio time. */
typedef struct st_audio_timing
{
    uint32_t last_tx;
    uint32_t have_tx;
    uint32_t gap_max;
    uint32_t tx_fill_max;
    uint32_t idle_fill_max;
    uint32_t tx_write_ok_max;
    uint32_t tx_write_fail_max;
    uint32_t idle_write_ok_max;
    uint32_t idle_write_fail_max;
    uint32_t recover_ok_max;
    uint32_t recover_fail_max;
    uint32_t recover_ok_count;
    uint32_t recover_fail_count;
    uint32_t tx_irq_count;
    uint32_t tx_no_callback;
    uint32_t tx_no_callback_run;
    uint32_t tx_no_callback_run_max;
    uint32_t int_irq_count;
    uint32_t int_no_idle;
    uint32_t irq_saturated;
    uint32_t fifo_no_cb[4];
    uint32_t trace_ordinal;
    uint32_t trace_count;
    uint32_t trace_truncated;
    uint32_t tx_pre_max;
    uint32_t tx_post_max;
    uint32_t int_pre_max;
    uint32_t int_post_max;
    uint32_t fallback_attempt;
    uint32_t fallback_ok;
    uint32_t fallback_underflow;
    uint32_t fallback_fail;
    uint32_t fallback_stop_fail;
    uint32_t fallback_max;
    uint32_t refill_ok;
    uint32_t refill_last;
    uint32_t refill_have;
    uint32_t refill_gap_max;
    uint32_t int_trace_count;
    uint32_t int_trace_omitted;
    uint32_t last_tx_valid;
    uint32_t tx_summary_max;
    uint32_t delay_count;
    uint32_t delay_latched;
    uint32_t delay_prev_valid;
    uint32_t delay_success_valid;
    uint32_t delay_current_valid;
    uint32_t delay_threshold_cycles;
    uint32_t prov_entry_valid;
    uint32_t prov_entry_ordinal;
    uint32_t prov_last_valid;
    uint32_t prov_entry_max;
    uint32_t prov_pre_max;
    uint32_t prov_post_max;
    uint32_t prov_aux_max;
    uint32_t prov_session_mode;
} audio_timing_t;

static volatile audio_timing_t s_timing;
static uint32_t s_timing_session;
static uint32_t s_timing_clock;
/* NT-Shell writes only under DI while READY. start samples this in its
 * existing session snapshot; a selection during prefill may affect that
 * start. ISRs use only the fixed session mode, never this configured value. */
static uint32_t s_prov_configured_mode = AUDIO_PROV_FULL;

/** Immutable after publication, until the next session. Keep the 896-byte
 * array OUTSIDE the aggregate copied under DI by status. Resetting the count
 * hides old records without clearing the array with interrupts disabled. */
typedef struct st_audio_trace_record
{
    uint32_t kind;
    uint32_t ordinal;
    uint32_t cycles;
    uint32_t callback_before;
    uint32_t callback_after;
    uint32_t pre_tdc;
    uint32_t pre_dtce;
    uint32_t pre_descriptor_blocks;
    uint32_t pre_sw_samples;
    uint32_t post_tdc;
    uint32_t post_dtce;
    uint32_t post_descriptor_blocks;
    uint32_t post_sw_samples;
    uint32_t flags;
} audio_trace_record_t;

_Static_assert(sizeof(audio_trace_record_t) == 56U, "SSI trace record size");
static volatile audio_trace_record_t s_trace[AUDIO_TRACE_CAPACITY];

/** Most recent completed TX wrapper, not necessarily the cause of an INT.
 * Only TX writes this summary; SSI TX and INT have the same IPL. */
typedef struct st_audio_tx_summary
{
    uint32_t ordinal;
    uint32_t entry_cycles;
    uint32_t exit_cycles;
    uint32_t entry_tdc;
    uint32_t native_delta;
    uint32_t fallback_attempt_delta;
    uint32_t fallback_ok_delta;
    uint32_t refill_ok_delta;
} audio_tx_summary_t;

typedef struct st_audio_int_record
{
    uint32_t ordinal;
    uint32_t cycles;
    uint32_t ssicr;
    uint32_t ssisr;
    uint32_t ssifcr;
    uint32_t ssifsr;
    uint32_t ielsr;
    uint32_t dtcsts;
    uint32_t descriptor_blocks;
    uint32_t sw_samples;
    uint32_t state;
    uint32_t stop;
    uint32_t kind;
    uint32_t idle_before;
    uint32_t idle_after;
    uint32_t last_valid;
    audio_tx_summary_t last_tx;
} audio_int_record_t;

_Static_assert(sizeof(audio_tx_summary_t) == 32U, "SSI TX summary size");
_Static_assert(sizeof(audio_int_record_t) == 96U, "SSI INT record size");
static volatile audio_tx_summary_t s_last_tx;
static volatile audio_int_record_t s_int_trace[AUDIO_INT_TRACE_CAPACITY];

typedef struct st_audio_delay_stamp
{
    uint32_t cycles;
    uint32_t ms;
    uint32_t time_error;
} audio_delay_stamp_t;

typedef struct st_audio_delay_record
{
    uint32_t ordinal;
    audio_delay_stamp_t current;
    audio_delay_stamp_t previous;
    audio_delay_stamp_t success;
    uint32_t prev_valid;
    uint32_t success_valid;
    uint32_t reason;
    uint32_t success_ordinal;
    uint32_t success_kind;
    uint32_t success_index;
    uint32_t success_address;
    uint32_t state;
    uint32_t stop;
    uint32_t tx_count;
    uint32_t refill_count;
    uint32_t dwt_ctrl;
    uint32_t demcr;
    uint32_t clock;
    uint32_t ssisr;
    uint32_t ssicr;
    uint32_t ssifcr;
    uint32_t ssifsr;
    uint32_t ielsr;
    uint32_t dtcsts;
    uint32_t descriptor_blocks;
    uint32_t descriptor_source;
    uint32_t sw_samples;
    uint32_t play_index;
} audio_delay_record_t;

_Static_assert(sizeof(audio_delay_record_t) <= 160U, "SSI delay record size");
static volatile audio_delay_record_t s_delay_trace[AUDIO_DELAY_CAPACITY];
static audio_delay_stamp_t s_delay_current, s_delay_previous, s_delay_success;
static uint32_t s_delay_current_ordinal, s_delay_success_ordinal;
static uint32_t s_delay_success_kind, s_delay_success_index, s_delay_success_address;

typedef struct st_audio_prov_snapshot
{
    uint32_t cycles_begin, ielsr_begin, dtcsts_begin, crb_begin;
    uint32_t settings, sar, cra, ssifsr;
    uint32_t crb_end, dtcsts_end, ielsr_end, cycles_end;
} audio_prov_snapshot_t;

typedef struct st_audio_prov_header
{
    uint32_t valid, ordinal, kind, index, address, bytes, blocks, write_error;
    uint32_t phase_mask;
} audio_prov_header_t;

typedef struct st_audio_provenance
{
    audio_prov_header_t header;
    audio_prov_snapshot_t entry, pre, post;
} audio_provenance_t;

_Static_assert(sizeof(audio_prov_snapshot_t) == 48U, "SSI provenance snapshot size");
_Static_assert(sizeof(audio_prov_header_t) <= 48U, "SSI provenance header size");
_Static_assert(sizeof(audio_provenance_t) <= 192U, "SSI provenance size");
_Static_assert(sizeof(audio_prov_snapshot_t) + 6U * sizeof(audio_provenance_t) + 9U * sizeof(uint32_t)
               <= 1280U, "SSI provenance static budget");
static volatile audio_prov_snapshot_t s_prov_entry;
static volatile audio_provenance_t s_prov_candidate, s_prov_last;
static volatile audio_provenance_t s_prov_delay[AUDIO_DELAY_CAPACITY];
static void audio_prov_freeze(uint32_t index);

/** Entry timestamps are compared modulo 32 bits; no wrap-count inference.
 * Until four episodes are stored this adds one short tk_get_otm critical
 * section, including its possible PendSV request. It is not a wait API.
 * The unpublished static slot avoids adding a large ISR stack temporary. */
static void audio_delay_entry(uint32_t ordinal, uint32_t cycles)
{
    if (s_timing.delay_count >= AUDIO_DELAY_CAPACITY)
    {
        return;
    }
    SYSTIM now = {0, 0};
    ER time_error = tk_get_otm(&now);
    s_delay_current.cycles = cycles;
    s_delay_current.ms = (uint32_t)now.lo;
    s_delay_current.time_error = (uint32_t)time_error;
    s_delay_current_ordinal = ordinal;
    s_timing.delay_current_valid = 1U;
    uint32_t reason = 0U;
    if (0U != s_timing.delay_prev_valid)
    {
        if ((0U != s_timing.delay_threshold_cycles) &&
            ((cycles - s_delay_previous.cycles) >= s_timing.delay_threshold_cycles))
        {
            reason |= AUDIO_DELAY_PREV_DWT;
        }
        if ((E_OK == time_error) && ((uint32_t)E_OK == s_delay_previous.time_error) &&
            (((uint32_t)now.lo - s_delay_previous.ms) >= AUDIO_DELAY_MS))
        {
            reason |= AUDIO_DELAY_PREV_OS;
        }
    }
    if (0U != s_timing.delay_success_valid)
    {
        if ((0U != s_timing.delay_threshold_cycles) &&
            ((cycles - s_delay_success.cycles) >= s_timing.delay_threshold_cycles))
        {
            reason |= AUDIO_DELAY_SUCCESS_DWT;
        }
        if ((E_OK == time_error) && ((uint32_t)E_OK == s_delay_success.time_error) &&
            (((uint32_t)now.lo - s_delay_success.ms) >= AUDIO_DELAY_MS))
        {
            reason |= AUDIO_DELAY_SUCCESS_OS;
        }
    }
    if (0U == reason)
    {
        s_timing.delay_latched = 0U;
    }
    else if (0U == s_timing.delay_latched)
    {
        volatile audio_delay_record_t *record = &s_delay_trace[s_timing.delay_count];
        /* First peripheral read for this capture must precede FSP clearing.
         * These sequential observations are not a DTC hardware snapshot. */
        record->ssisr = R_SSI0->SSISR;
        record->ssicr = R_SSI0->SSICR;
        record->ssifcr = R_SSI0->SSIFCR;
        record->ssifsr = R_SSI0->SSIFSR;
        record->ielsr = R_ICU->IELSR[VECTOR_NUMBER_SSI0_TXI];
        record->dtcsts = FSP_STYPE3_REG16_READ(R_DTC->DTCSTS, !AUDIO_DTC_SECURITY_ATTRIBUTE);
        volatile transfer_info_t *info = (volatile transfer_info_t *)g_transfer_i2s_tx.p_cfg->p_info;
        record->descriptor_blocks = info->num_blocks;
        record->descriptor_source = (uint32_t)(uintptr_t)info->p_src;
        record->sw_samples = ((volatile ssi_instance_ctrl_t *)&g_i2s_audio_ctrl)->tx_src_samples;
        record->play_index = s_play_idx;
        record->ordinal = ordinal;
        record->current = s_delay_current;
        record->previous = s_delay_previous;
        record->success = s_delay_success;
        record->prev_valid = s_timing.delay_prev_valid;
        record->success_valid = s_timing.delay_success_valid;
        record->reason = reason;
        record->success_ordinal = s_delay_success_ordinal;
        record->success_kind = s_delay_success_kind;
        record->success_index = s_delay_success_index;
        record->success_address = s_delay_success_address;
        record->state = (uint32_t)s_state;
        record->stop = (uint32_t)s_stop_request;
        record->tx_count = s_tx_empty_count;
        record->refill_count = s_timing.refill_ok;
        record->dwt_ctrl = DWT->CTRL;
        record->demcr = CoreDebug->DEMCR;
        record->clock = s_timing_clock;
        /* Same IPL writers cannot interleave; publish only after all stores. */
        audio_prov_freeze(s_timing.delay_count);
        s_timing.delay_count++;
        s_timing.delay_latched = 1U;
    }
    s_delay_previous = s_delay_current;
    s_timing.delay_prev_valid = 1U;
}

/** Called only after a successful TX Write. Time belongs to that TX entry,
 * not the API completion instant. Start and IDLE prefill never call this. */
static void audio_delay_success(bool fallback, uint32_t index)
{
    if ((s_timing.delay_count < AUDIO_DELAY_CAPACITY) && (0U != s_timing.delay_current_valid))
    {
        s_delay_success = s_delay_current;
        s_delay_success_ordinal = s_delay_current_ordinal;
        s_delay_success_kind = fallback ? 2U : 1U;
        s_delay_success_index = index;
        s_delay_success_address = (uint32_t)(uintptr_t)&s_pcm_buf[index][0];
        s_timing.delay_success_valid = 1U;
    }
}

static void audio_timing_max(volatile uint32_t *maximum, uint32_t elapsed)
{
    if (elapsed > *maximum)
    {
        *maximum = elapsed;
    }
}

/** Mark the whole session invalid when any raw IRQ counter reaches its limit.
 * Called only by the two non-preempting SSI ISRs (IPL 2). */
static void audio_irq_count(volatile uint32_t *count)
{
    if (*count < UINT32_MAX)
    {
        (*count)++;
    }
    if (UINT32_MAX == *count)
    {
        s_timing.irq_saturated = 1U;
    }
}

/** Fixed-order observations bracket descriptor fields with activity/count.
 * CPU readback is not an atomic snapshot or proof of DTC internal values. */
static void audio_prov_sample(volatile audio_prov_snapshot_t *sample)
{
    volatile transfer_info_t *info = (volatile transfer_info_t *)g_transfer_i2s_tx.p_cfg->p_info;
    sample->cycles_begin = DWT->CYCCNT;
    sample->ielsr_begin = R_ICU->IELSR[VECTOR_NUMBER_SSI0_TXI];
    sample->dtcsts_begin = FSP_STYPE3_REG16_READ(R_DTC->DTCSTS, !AUDIO_DTC_SECURITY_ATTRIBUTE);
    sample->crb_begin = info->num_blocks;
    sample->settings = info->transfer_settings_word;
    sample->sar = (uint32_t)(uintptr_t)info->p_src;
    sample->cra = info->length;
    sample->ssifsr = R_SSI0->SSIFSR;
    sample->crb_end = info->num_blocks;
    sample->dtcsts_end = FSP_STYPE3_REG16_READ(R_DTC->DTCSTS, !AUDIO_DTC_SECURITY_ATTRIBUTE);
    sample->ielsr_end = R_ICU->IELSR[VECTOR_NUMBER_SSI0_TXI];
    sample->cycles_end = DWT->CYCCNT;
}

/** Called after delay detection saved the OLD successful provenance. */
static void audio_prov_entry(void)
{
    s_timing.prov_entry_valid = 0U;
    if ((s_timing.delay_count < AUDIO_DELAY_CAPACITY) && (0U != s_timing.delay_current_valid))
    {
        audio_prov_sample(&s_prov_entry);
        s_timing.prov_entry_ordinal = s_delay_current_ordinal;
        s_timing.prov_entry_valid = 1U;
        audio_timing_max(&s_timing.prov_entry_max, s_prov_entry.cycles_end - s_prov_entry.cycles_begin);
    }
}

/** Prepare a fresh candidate before TIE0 so the ENTRY copy does not extend
 * that interval. PRE itself is sampled only after TIE0 plus readback. */
static void audio_prov_prepare(bool fallback, uint32_t next)
{
    s_prov_candidate.header.valid = 0U;
    if ((s_timing.delay_count >= AUDIO_DELAY_CAPACITY) || (0U == s_timing.delay_current_valid) ||
        (0U == s_timing.prov_entry_valid) || (s_timing.prov_entry_ordinal != s_delay_current_ordinal))
    {
        return;
    }
    uint32_t begin = DWT->CYCCNT;
    s_prov_candidate.header.ordinal = s_delay_current_ordinal;
    s_prov_candidate.header.kind = fallback ? 2U : 1U;
    s_prov_candidate.header.index = next;
    s_prov_candidate.header.address = (uint32_t)(uintptr_t)&s_pcm_buf[next][0];
    s_prov_candidate.header.bytes = AUDIO_BUFFER_BYTES;
    s_prov_candidate.header.blocks = AUDIO_BUFFER_FRAMES;
    s_prov_candidate.header.write_error = (uint32_t)FSP_ERR_INTERNAL;
    s_prov_candidate.header.phase_mask = AUDIO_PROV_ENTRY |
        ((AUDIO_PROV_FULL == s_timing.prov_session_mode) ? (AUDIO_PROV_PRE | AUDIO_PROV_POST) : 0U);
    s_prov_candidate.entry = s_prov_entry;
    s_prov_candidate.header.valid = 1U;
    audio_timing_max(&s_timing.prov_aux_max, DWT->CYCCNT - begin);
}

/** POST precedes TIE1 even on success. Publishing the large history waits
 * until after the existing TIE1 operation, and never occurs on failure. */
static void audio_prov_post(fsp_err_t err)
{
    if (0U != s_prov_candidate.header.valid)
    {
        if (0U != (s_prov_candidate.header.phase_mask & AUDIO_PROV_POST))
        {
            audio_prov_sample(&s_prov_candidate.post);
            audio_timing_max(&s_timing.prov_post_max,
                             s_prov_candidate.post.cycles_end - s_prov_candidate.post.cycles_begin);
        }
        s_prov_candidate.header.write_error = (uint32_t)err;
    }
}

static void audio_prov_commit(void)
{
    if (s_timing.delay_count >= AUDIO_DELAY_CAPACITY)
    {
        return;
    }
    uint32_t begin = DWT->CYCCNT;
    s_timing.prov_last_valid = 0U;
    if ((0U != s_prov_candidate.header.valid) &&
        ((uint32_t)FSP_SUCCESS == s_prov_candidate.header.write_error) &&
        (0U != s_timing.delay_current_valid) && (0U != s_timing.delay_success_valid) &&
        (s_prov_candidate.header.ordinal == s_delay_current_ordinal) &&
        (s_prov_candidate.header.ordinal == s_delay_success_ordinal))
    {
        s_prov_last = s_prov_candidate;
        s_timing.prov_last_valid = 1U;
    }
    audio_timing_max(&s_timing.prov_aux_max, DWT->CYCCNT - begin);
}

/** Attach before delay_count publication. Same-IPL writers cannot interleave.
 * Invalid old arrays remain hidden by the header valid bit and ordinal. */
static void audio_prov_freeze(uint32_t index)
{
    uint32_t begin = DWT->CYCCNT;
    if ((0U != s_timing.prov_last_valid) && (0U != s_timing.delay_success_valid) &&
        (s_prov_last.header.ordinal == s_delay_success_ordinal))
    {
        s_prov_delay[index] = s_prov_last;
    }
    else
    {
        s_prov_delay[index].header.valid = 0U;
        s_prov_delay[index].header.ordinal = 0U;
    }
    audio_timing_max(&s_timing.prov_aux_max, DWT->CYCCNT - begin);
}

/** Sequential observations, NOT an atomic hardware snapshot. TDC is read by
 * the caller. DTCE is permission to activate; descriptor memory may differ
 * from DTC internal state. Neither it nor zero software samples proves DTC
 * completion (r_ssi.c:822-850, r_dtc.c:378-413). No peripheral writes. */
static void audio_trace_pre(audio_trace_record_t *record)
{
    record->pre_dtce = R_ICU->IELSR_b[VECTOR_NUMBER_SSI0_TXI].DTCE;
    record->pre_descriptor_blocks =
        ((volatile transfer_info_t *)g_transfer_i2s_tx.p_cfg->p_info)->num_blocks;
    record->pre_sw_samples = ((volatile ssi_instance_ctrl_t *)&g_i2s_audio_ctrl)->tx_src_samples;
}

static void audio_trace_post(audio_trace_record_t *record)
{
    record->post_tdc = R_SSI0->SSIFSR_b.TDC;
    record->post_dtce = R_ICU->IELSR_b[VECTOR_NUMBER_SSI0_TXI].DTCE;
    record->post_descriptor_blocks =
        ((volatile transfer_info_t *)g_transfer_i2s_tx.p_cfg->p_info)->num_blocks;
    record->post_sw_samples = ((volatile ssi_instance_ctrl_t *)&g_i2s_audio_ctrl)->tx_src_samples;
    record->flags = AUDIO_TRACE_POST_VALID;
}

/** SSI TX and INT share IPL 2; neither writer can preempt the other. */
static void audio_trace_publish(const audio_trace_record_t *record)
{
    uint32_t index = s_timing.trace_count;
    if (index < AUDIO_TRACE_CAPACITY)
    {
        s_trace[index] = *record;
        s_timing.trace_count = index + 1U;
    }
    else
    {
        s_timing.trace_truncated = 1U;
    }
}

/** Read SSISR first, before the original ISR reads and clears its flags.
 * Reads are sequential, not an atomic hardware snapshot. No FIFO data reads
 * or peripheral writes. DTCSTS.VECN is meaningful only when ACT is set. */
static void audio_int_trace_pre(audio_int_record_t *record)
{
    record->ssisr = R_SSI0->SSISR;
    record->ssicr = R_SSI0->SSICR;
    record->ssifcr = R_SSI0->SSIFCR;
    record->ssifsr = R_SSI0->SSIFSR;
    record->ielsr = R_ICU->IELSR[VECTOR_NUMBER_SSI0_TXI];
    record->dtcsts = FSP_STYPE3_REG16_READ(R_DTC->DTCSTS, !AUDIO_DTC_SECURITY_ATTRIBUTE);
    record->descriptor_blocks =
        ((volatile transfer_info_t *)g_transfer_i2s_tx.p_cfg->p_info)->num_blocks;
    record->sw_samples = ((volatile ssi_instance_ctrl_t *)&g_i2s_audio_ctrl)->tx_src_samples;
    record->state = (uint32_t)s_state;
    record->stop = (uint32_t)s_stop_request;
    record->last_valid = s_timing.last_tx_valid;
    if (0U != record->last_valid)
    {
        record->last_tx = s_last_tx;
    }
}

/** Publish only complete immutable INT records; metadata reset hides old data. */
static void audio_int_trace_publish(const audio_int_record_t *record)
{
    uint32_t index = s_timing.int_trace_count;
    if (index < AUDIO_INT_TRACE_CAPACITY)
    {
        s_int_trace[index] = *record;
        s_timing.int_trace_count = index + 1U;
    }
    else
    {
        audio_irq_count(&s_timing.int_trace_omitted);
    }
}

/* Linker --wrap redirects the vector references; __real resolves to the
 * unmodified FSP driver. Both symbols MUST be wrapped in every build.
 * Delay capture adds tk_get_otm until its four slots are full; other trace
 * instrumentation only reads registers and memory, with no logging or wait.
 * The subsequent fallback guard reads live transfer
 * state and may call Write/Stop; those existing driver calls include waits.
 * All counters reset with s_timing before the first Write of a session.
 * The foreground snapshot cannot run between entry and exit of an ISR.
 * Pre/post maxima cover the code BETWEEN each pair of CYCCNT reads (record
 * publication included). DWT read cost, max updates, prologue/epilogue and
 * original ISR execution are excluded: these are not full wrapper overhead.
 * Each trace stops detailed sampling at its own capacity. Entry TDC
 * classification continues. TX summary publication has its own maximum. */
void __real_ssi_txi_isr(void);
void __real_ssi_int_isr(void);
void __wrap_ssi_txi_isr(void);
void __wrap_ssi_int_isr(void);
static void audio_fallback_refill(void);

void __wrap_ssi_txi_isr(void)
{
    uint32_t begin = DWT->CYCCNT;
    uint32_t before = s_tx_empty_count;
    bool summarize = s_timing.int_trace_count < AUDIO_INT_TRACE_CAPACITY;
    uint32_t attempt_before = 0U, fallback_before = 0U, refill_before = 0U;
    if (summarize)
    {
        attempt_before = s_timing.fallback_attempt;
        fallback_before = s_timing.fallback_ok;
        refill_before = s_timing.refill_ok;
    }
    audio_trace_record_t record = {0};
    bool detail = s_timing.trace_count < AUDIO_TRACE_CAPACITY;
    audio_irq_count(&s_timing.trace_ordinal);
    record.ordinal = s_timing.trace_ordinal;
    record.cycles = begin;
    audio_delay_entry(record.ordinal, begin);
    audio_prov_entry();
    record.pre_tdc = R_SSI0->SSIFSR_b.TDC;
    if (detail)
    {
        audio_trace_pre(&record);
    }
    audio_irq_count(&s_timing.tx_irq_count);
    audio_timing_max(&s_timing.tx_pre_max, DWT->CYCCNT - begin);
    __real_ssi_txi_isr();
    begin = DWT->CYCCNT;
    if (before == s_tx_empty_count)
    {
        if (detail)
        {
            audio_trace_post(&record);
        }
        audio_irq_count(&s_timing.tx_no_callback);
        audio_irq_count(&s_timing.tx_no_callback_run);
        audio_timing_max(&s_timing.tx_no_callback_run_max, s_timing.tx_no_callback_run);
        uint32_t category = (record.pre_tdc == 0U) ? 0U :
                            ((record.pre_tdc <= 16U) ? 1U : ((record.pre_tdc <= 32U) ? 2U : 3U));
        audio_irq_count(&s_timing.fifo_no_cb[category]);
        record.kind = AUDIO_TRACE_TX_NO_CB;
        record.callback_before = before;
        record.callback_after = s_tx_empty_count;
        audio_trace_publish(&record);
    }
    else
    {
        s_timing.tx_no_callback_run = 0U;
    }
    audio_timing_max(&s_timing.tx_post_max, DWT->CYCCNT - begin);
    /* Preserve native notification counts and pre-fallback trace first.
     * This interval is separate from the wrapper instrumentation maxima. */
    if (before == s_tx_empty_count)
    {
        begin = DWT->CYCCNT;
        audio_fallback_refill();
        audio_timing_max(&s_timing.fallback_max, DWT->CYCCNT - begin);
    }
    if (summarize)
    {
        /* This exit stamp precedes summary construction, not actual ISR
         * return. No extra TX peripheral reads; use existing observations. */
        begin = DWT->CYCCNT;
        audio_tx_summary_t summary = {
            .ordinal = record.ordinal,
            .entry_cycles = record.cycles,
            .exit_cycles = begin,
            .entry_tdc = record.pre_tdc,
            .native_delta = s_tx_empty_count - before,
            .fallback_attempt_delta = s_timing.fallback_attempt - attempt_before,
            .fallback_ok_delta = s_timing.fallback_ok - fallback_before,
            .refill_ok_delta = s_timing.refill_ok - refill_before,
        };
        s_last_tx = summary;
        s_timing.last_tx_valid = 1U;
        audio_timing_max(&s_timing.tx_summary_max, DWT->CYCCNT - begin);
    }
    s_timing.delay_current_valid = 0U;
    s_timing.prov_entry_valid = 0U;
}

void __wrap_ssi_int_isr(void)
{
    uint32_t begin = DWT->CYCCNT;
    uint32_t before = s_idle_count;
    audio_int_record_t int_record = {0};
    bool int_detail = s_timing.int_trace_count < AUDIO_INT_TRACE_CAPACITY;
    if (int_detail)
    {
        /* Before the older trace also reads any peripheral state. */
        audio_int_trace_pre(&int_record);
    }
    audio_trace_record_t record = {0};
    audio_irq_count(&s_timing.trace_ordinal);
    record.ordinal = s_timing.trace_ordinal;
    record.cycles = begin;
    if (s_timing.trace_count < AUDIO_TRACE_CAPACITY)
    {
        record.pre_tdc = R_SSI0->SSIFSR_b.TDC;
        audio_trace_pre(&record);
    }
    audio_irq_count(&s_timing.int_irq_count);
    audio_timing_max(&s_timing.int_pre_max, DWT->CYCCNT - begin);
    __real_ssi_int_isr();
    begin = DWT->CYCCNT;
    record.kind = AUDIO_TRACE_INT_IDLE;
    if (before == s_idle_count)
    {
        audio_irq_count(&s_timing.int_no_idle);
        record.kind = AUDIO_TRACE_INT_NO_IDLE;
    }
    record.callback_before = before;
    record.callback_after = s_idle_count;
    audio_trace_publish(&record);
    if (int_detail)
    {
        int_record.ordinal = record.ordinal;
        int_record.cycles = record.cycles;
        int_record.kind = record.kind;
        int_record.idle_before = before;
        int_record.idle_after = s_idle_count;
    }
    audio_int_trace_publish(&int_record);
    audio_timing_max(&s_timing.int_post_max, DWT->CYCCNT - begin);
}

/** uT-Kernel event flag used to wait for I2S_EVENT_IDLE. 0 = not created. */
static ID s_audio_flgid = 0;

/** Buffer producer installed by audio_start(). */
static audio_fill_cb_t s_fill_cb      = NULL;
static void           *s_fill_context = NULL;

/** Built-in test tone state (used when audio_start() is called with NULL). */
static uint32_t s_tone_freq_hz  = AUDIO_TEST_TONE_DEFAULT_HZ;
static uint16_t s_tone_amplitude = AUDIO_TEST_AMPLITUDE;
static volatile uint32_t s_tone_phase = 0;
static uint32_t s_tone_step  = 0;

/** true once R_SSI_Open()/R_GPT_Open() succeeded (so re-init is idempotent). */
static bool s_ssi_open  = false;
static bool s_gpt_open  = false;

/**********************************************************************************************************************
 Private (static) function prototypes
 *********************************************************************************************************************/
static fsp_err_t audio_sync_init(void);
static void      audio_fill_buffer(uint32_t index);
static void      audio_tone_fill(int16_t *p_frames, uint32_t frame_count, void *p_context);
static void      audio_tone_update_step(void);
static void      audio_log(const char *msg);
static void      audio_cmd_status(void);
static void      audio_cmd_usage(void);

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Create the uT-Kernel event flag used to wait for I2S_EVENT_IDLE.
 *
 * Idempotent. Called at the very beginning of audio_init(), i.e. before
 * R_SSI_Open() makes audio_i2s_callback() reachable, so the ISR can never
 * observe an uncreated flag. audio_i2s_callback() additionally checks the ID.
 */
static fsp_err_t audio_sync_init(void)
{
    if (s_audio_flgid > 0)
    {
        return FSP_SUCCESS;
    }

    T_CFLG cflg = {
        .exinf   = NULL,
        .flgatr  = TA_TFIFO | TA_WMUL,
        .iflgptn = 0,
    };

    ID flgid = tk_cre_flg(&cflg);
    if (flgid <= E_OK)
    {
        return FSP_ERR_INTERNAL;
    }

    s_audio_flgid = flgid;

    return FSP_SUCCESS;
}

/**
 * Recompute the phase increment of the built-in test tone.
 *
 * Q32 phase accumulator: step = freq * 2^32 / fs.
 */
static void audio_tone_update_step(void)
{
    uint64_t step = ((uint64_t)s_tone_freq_hz << 32) / (uint64_t)AUDIO_SAMPLE_RATE_HZ;

    s_tone_step = (uint32_t)step;
}

/**
 * Built-in test tone producer (square wave).
 *
 * Deliberately minimal: the sine LUT, the alarm patterns and the envelope
 * belong to Issue #47 (S-005-3). This exists only so that Issue #46 can be
 * verified on hardware.
 */
static void audio_tone_fill(int16_t *p_frames, uint32_t frame_count, void *p_context)
{
    FSP_PARAMETER_NOT_USED(p_context);

    uint32_t phase = s_tone_phase;
    int16_t  hi    = (int16_t)s_tone_amplitude;
    int16_t  lo    = (int16_t)(-(int32_t)s_tone_amplitude);

    for (uint32_t i = 0; i < frame_count; i++)
    {
        int16_t sample = (0U != (phase & 0x80000000U)) ? hi : lo;

        p_frames[(i * AUDIO_CHANNELS) + 0] = sample;    /* left  */
        p_frames[(i * AUDIO_CHANNELS) + 1] = sample;    /* right -> MIXOUT_R -> LINE amp */

        phase += s_tone_step;
    }

    s_tone_phase = phase;
}

/**
 * Fill one ping-pong buffer through the installed producer.
 *
 * Called from the SSI ISR (and from audio_start() before playback begins).
 */
static void audio_fill_buffer(uint32_t index)
{
    audio_fill_cb_t cb = s_fill_cb;

    if (NULL != cb)
    {
        cb(&s_pcm_buf[index][0], AUDIO_BUFFER_FRAMES, s_fill_context);
    }
    else
    {
        memset(&s_pcm_buf[index][0], 0, AUDIO_BUFFER_BYTES);
    }

#if BSP_CFG_DCACHE_ENABLED
    /* Make the freshly produced samples visible to the DTC. Compiled out in
     * this build (BSP_CFG_DCACHE_ENABLED == 0, see the file header). */
    SCB_CleanDCache_by_Addr((uint32_t *)&s_pcm_buf[index][0], (int32_t)AUDIO_BUFFER_BYTES);
#endif
}

/**
 * Console logging helper.
 */
static void audio_log(const char *msg)
{
    (void)print_to_console((char_t *)msg);
}

/**********************************************************************************************************************
 FSP callback (declared by ra_gen/hal_data.h:51 and referenced by
 ra_gen/hal_data.c:205 - it MUST exist or the image does not link)
 *********************************************************************************************************************/

/** One security-aware observation, not the FSP wait loop (r_dtc.c:673-678).
 * Other DTC activation sources do not own the audio PCM buffers. */
static bool audio_dtc_tx_active(void)
{
    uint32_t status = FSP_STYPE3_REG16_READ(R_DTC->DTCSTS, !AUDIO_DTC_SECURITY_ATTRIBUTE);
    return status == ((1U << 15) | (uint32_t)VECTOR_NUMBER_SSI0_TXI);
}

/** Limited to the current SSI0 BLOCK/IRQ_END/non-chained DTC configuration.
 * DTCE must be disabled BEFORE observing inactivity and descriptor memory:
 * no new transfer can start between these checks. Never use trace samples
 * for this decision. Also required for native callbacks: ICU IR clearing
 * does not clear NVIC pending (bsp_irq.h:74-108), and DTC enable only sets
 * DTCE (r_dtc.c:497-512). A residual native IRQ after fallback can pass the
 * driver's FIFO threshold while the NEW transfer still owns the buffer. */
static bool audio_refill_ready(void)
{
    if ((!s_ssi_open) || (AUDIO_STATE_PLAYING != s_state) || s_stop_request ||
        (0U == R_SSI0->SSICR_b.TEN) || (0U == R_SSI0->SSIFCR_b.TIE) ||
        (NULL != ((volatile ssi_instance_ctrl_t *)&g_i2s_audio_ctrl)->p_tx_src) ||
        (0U != ((volatile ssi_instance_ctrl_t *)&g_i2s_audio_ctrl)->tx_src_samples))
    {
        return false;
    }
    if ((0U != R_ICU->IELSR_b[VECTOR_NUMBER_SSI0_TXI].DTCE) || audio_dtc_tx_active())
    {
        return false;
    }
    volatile transfer_info_t *info = (volatile transfer_info_t *)g_transfer_i2s_tx.p_cfg->p_info;
    if ((NULL == info) || (0U != info->num_blocks) ||
        (TRANSFER_MODE_BLOCK != info->transfer_settings_word_b.mode) ||
        (TRANSFER_IRQ_END != info->transfer_settings_word_b.irq) ||
        (TRANSFER_CHAIN_MODE_DISABLED != info->transfer_settings_word_b.chain_mode))
    {
        return false;
    }
    return true;
}

/** Fallback additionally requires some FIFO data and no pending underflow.
 * Native callbacks do not use this occupancy restriction. */
static bool audio_fallback_ready(void)
{
    if (!audio_refill_ready())
    {
        return false;
    }
    uint32_t tdc = R_SSI0->SSIFSR_b.TDC;
    return (tdc >= 1U) && (tdc <= 16U) && (0U == R_SSI0->SSISR_b.TUIRQ);
}

/** Submit the next buffer, and only on success regenerate the freed buffer.
 * Both callers are inside SSI TX ISR. The self-guard also blocks a residual
 * native callback after ERROR -> IDLE cleared s_stop_request, and prevents
 * a pending native callback from resubmitting an incomplete DTC transfer. */
static void audio_refill(bool fallback)
{
    if (!(fallback ? audio_fallback_ready() : audio_refill_ready()))
    {
        return;
    }
    if (fallback)
    {
        audio_irq_count(&s_timing.fallback_attempt);
    }
    uint32_t next = s_play_idx ^ 1U;
    audio_prov_prepare(fallback, next);
    uint32_t write_start = DWT->CYCCNT;
    /* RA8P1 manual Rev.1.30, 47.7.2: TXI is a pulse. Rearming DTC
     * alone does not reissue a request when TDE is already set. Even a
     * native notification's FIFO check can become stale before Write.
     * Disable TIE only AFTER the final completion guard, preserving all
     * other FIFO control bits, for both native and fallback refills. */
    R_SSI0->SSIFCR &= ~R_SSI0_SSIFCR_TIE_Msk;
    (void)R_SSI0->SSIFCR; /* Complete the peripheral write before rearming. */
    if ((0U != s_prov_candidate.header.valid) &&
        (0U != (s_prov_candidate.header.phase_mask & AUDIO_PROV_PRE)))
    {
        audio_prov_sample(&s_prov_candidate.pre);
        audio_timing_max(&s_timing.prov_pre_max,
                         s_prov_candidate.pre.cycles_end - s_prov_candidate.pre.cycles_begin);
    }
    fsp_err_t err = R_SSI_Write(&g_i2s_audio_ctrl, &s_pcm_buf[next][0], AUDIO_BUFFER_BYTES);
    audio_prov_post(err);
    if (FSP_SUCCESS == err)
    {
        /* DTC is ready now. TIE 0->1 reissues TXI if TDE=1; otherwise
         * the next TDE rising edge does. Do this before generating PCM.
         * On failure leave TIE disabled and use the Stop path below. */
        R_SSI0->SSIFCR |= R_SSI0_SSIFCR_TIE_Msk;
    }
    uint32_t now = DWT->CYCCNT;
    audio_timing_max((FSP_SUCCESS == err) ? &s_timing.tx_write_ok_max :
                     &s_timing.tx_write_fail_max, now - write_start);
    if (FSP_SUCCESS != err)
    {
        s_last_error = err;
        s_error_count++;
        if (!fallback)
        {
            /* Preserve the native callback failure policy. */
            s_stop_request = true;
            (void)R_SSI_Stop(&g_i2s_audio_ctrl);
            return;
        }
        bool underflow = (FSP_ERR_UNDERFLOW == err);
        audio_irq_count(underflow ? &s_timing.fallback_underflow : &s_timing.fallback_fail);
        if (!underflow)
        {
            s_stop_request = true;
        }
        /* Write already reset the DTC before it can return UNDERFLOW.
         * Stop's success alone is insufficient: its lower-level disable
         * return value is ignored (r_ssi.c:767-805). Inactivity must follow
         * disable before IDLE is allowed to regenerate both PCM buffers. */
        fsp_err_t stop_err = R_SSI_Stop(&g_i2s_audio_ctrl);
        bool stopped = (FSP_SUCCESS == stop_err) &&
                       (0U == R_SSI0->SSICR_b.TEN) &&
                       (0U == R_SSI0->SSIFCR_b.TIE) &&
                       (0U == R_ICU->IELSR_b[VECTOR_NUMBER_SSI0_TXI].DTCE) &&
                       !audio_dtc_tx_active();
        if (!stopped)
        {
            audio_irq_count(&s_timing.fallback_stop_fail);
            s_last_error = (FSP_SUCCESS != stop_err) ? stop_err : FSP_ERR_INTERNAL;
            s_stop_request = true;
            s_state = AUDIO_STATE_ERROR;
        }
        /* Confirmed UNDERFLOW stop keeps PLAYING/no stop request, handing
         * recovery to the existing INT-IDLE path. No retry loop here. */
        return;
    }
    if (fallback)
    {
        audio_irq_count(&s_timing.fallback_ok);
    }
    audio_irq_count(&s_timing.refill_ok);
    audio_delay_success(fallback, next);
    audio_prov_commit();
    if (0U != s_timing.refill_have)
    {
        audio_timing_max(&s_timing.refill_gap_max, now - s_timing.refill_last);
    }
    s_timing.refill_last = now;
    s_timing.refill_have = 1U;
    uint32_t freed = s_play_idx;
    s_play_idx = next;
    uint32_t fill_start = DWT->CYCCNT;
    audio_fill_buffer(freed);
    audio_timing_max(&s_timing.tx_fill_max, DWT->CYCCNT - fill_start);
}

/** Safe completion guard before supplementing a missed native notification. */
static void audio_fallback_refill(void)
{
    if (audio_fallback_ready())
    {
        audio_refill(true);
    }
}

/**
 * SSI transmit / idle callback.
 *
 * @details ISR context (SSI0 TXI and SSI0 INT, both at priority 2 -
 *          ra_gen/hal_data.c:222). Keeps the stream running by submitting the
 *          other ping-pong buffer, then regenerating the buffer that was just
 *          drained.
 */
void audio_i2s_callback(i2s_callback_args_t *p_args)
{
    fsp_err_t err;

    switch (p_args->event)
    {
        case I2S_EVENT_TX_EMPTY:
        {
            uint32_t now_cycles = DWT->CYCCNT;
            if (0U != s_timing.have_tx)
            {
                audio_timing_max(&s_timing.gap_max, now_cycles - s_timing.last_tx);
            }
            s_timing.last_tx = now_cycles;
            s_timing.have_tx = 1U;
            s_tx_empty_count++;

            if (s_stop_request)
            {
                /* Stop completes asynchronously with I2S_EVENT_IDLE
                 * (ra/fsp/inc/api/r_i2s_api.h:172). */
                err = R_SSI_Stop(&g_i2s_audio_ctrl);
                if (FSP_SUCCESS != err)
                {
                    s_last_error = err;
                    s_error_count++;
                }
                break;
            }

            audio_refill(false);
            break;
        }

        case I2S_EVENT_IDLE:
        {
            s_idle_count++;
            /* IDLE prefill is not a successful TX submission timestamp. */
            s_timing.delay_success_valid = 0U;
            s_timing.prov_last_valid = 0U;

            /*
             * UNSOLICITED idle: still PLAYING and nobody asked to stop, so the
             * SSI ran dry (see s_restart_count). Re-prime both buffers and
             * resubmit instead of falling silent - a dropped alarm tone is
             * worse than the click this causes. R_SSI_Write() from the
             * callback is the documented pattern (r_ssi.c:321).
             */
            if ((AUDIO_STATE_PLAYING == s_state) && (!s_stop_request))
            {
                uint32_t recover_start = DWT->CYCCNT;
                s_restart_count++;

                uint32_t fill_start = DWT->CYCCNT;
                audio_fill_buffer(0);
                audio_timing_max(&s_timing.idle_fill_max, DWT->CYCCNT - fill_start);
                fill_start = DWT->CYCCNT;
                audio_fill_buffer(1);
                audio_timing_max(&s_timing.idle_fill_max, DWT->CYCCNT - fill_start);
                s_play_idx = 0;

                uint32_t write_start = DWT->CYCCNT;
                err = R_SSI_Write(&g_i2s_audio_ctrl, &s_pcm_buf[0][0], AUDIO_BUFFER_BYTES);
                uint32_t write_cycles = DWT->CYCCNT - write_start;
                uint32_t recover_cycles = DWT->CYCCNT - recover_start;
                if (FSP_SUCCESS == err)
                {
                    audio_timing_max(&s_timing.idle_write_ok_max, write_cycles);
                    audio_timing_max(&s_timing.recover_ok_max, recover_cycles);
                    s_timing.recover_ok_count++;
                    break;              /* stream is running again */
                }

                audio_timing_max(&s_timing.idle_write_fail_max, write_cycles);
                audio_timing_max(&s_timing.recover_fail_max, recover_cycles);
                s_timing.recover_fail_count++;

                /* Could not restart - fall through to the stop handling so the
                 * device does not stay wedged in PLAYING. */
                s_last_error = err;
                s_error_count++;
            }

            s_stop_request = false;

            /* The stop is now complete, so the device really is idle.
             *
             * PLAYING  : unsolicited idle that could not be restarted.
             * STOPPING : audio_stop() is either still waiting (it sets READY
             *            itself when the wait returns - harmless duplicate) or
             *            it already TIMED OUT and deliberately left the state
             *            at STOPPING. Clearing it here is what recovers the
             *            device in that case; r_ssi_stop_sub() re-enables the
             *            idle interrupt, so this callback is guaranteed to
             *            run. */
            if ((AUDIO_STATE_PLAYING == s_state) || (AUDIO_STATE_STOPPING == s_state))
            {
                s_state = AUDIO_STATE_READY;
            }

            if (s_audio_flgid > 0)
            {
                (void)tk_set_flg(s_audio_flgid, AUDIO_EVT_IDLE);
            }
            break;
        }

        default:
        {
            /* I2S_EVENT_RX_FULL cannot occur: the receive interrupt is
             * disabled (ra_gen/hal_data.c:213-215, rxi_irq =
             * FSP_INVALID_VECTOR). */
            break;
        }
    }
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Bring up the audio output device.
 */
fsp_err_t audio_init(void)
{
    fsp_err_t err;
    char      buf[AUDIO_PRINT_BUF_SIZE];

    /* ---------------------------------------------------------------------
     * Claim the initialisation atomically.
     *
     * audio_task runs audio_init() at boot and the shell can run it again via
     * `audio init`, so two TASKS can enter here. Testing only for READY /
     * PLAYING is not enough: the state stays UNINITIALIZED for the whole
     * roughly 0.4 s power-up sequence, so both callers would pass the check
     * and interleave the DA7212 soft reset, LDO/PLL bring-up and register
     * writes. The IIC1 bus mutex does NOT prevent that - it serialises single
     * transfers, not this multi-step sequence - and one caller could return
     * success and start playback while the other is still muting the codec.
     *
     * tk_dis_dsp() is enough to make the test-and-set atomic: the only other
     * writer of s_state is the SSI ISR, and it only ever moves PLAYING ->
     * READY (audio_i2s_callback), never into or out of INITIALIZING.
     * ------------------------------------------------------------------ */
    if (E_OK != tk_dis_dsp())
    {
        return FSP_ERR_INTERNAL;
    }

    if ((AUDIO_STATE_READY == s_state) ||
        (AUDIO_STATE_PLAYING == s_state) ||
        (AUDIO_STATE_STOPPING == s_state))
    {
        (void)tk_ena_dsp();
        return FSP_SUCCESS;
    }

    if (AUDIO_STATE_INITIALIZING == s_state)
    {
        (void)tk_ena_dsp();
        return FSP_ERR_IN_USE;
    }

    s_state = AUDIO_STATE_INITIALIZING;
    (void)tk_ena_dsp();

    /* Every exit below is terminal: the success path sets READY and
     * init_failed: sets ERROR, so INITIALIZING is never left behind. */

    /* Event flag before anything can call audio_i2s_callback(). */
    err = audio_sync_init();
    if (FSP_SUCCESS != err)
    {
        s_last_error = err;
        s_state      = AUDIO_STATE_ERROR;
        return err;
    }

    audio_tone_update_step();

    if (!s_gpt_open)
    {
        /* -----------------------------------------------------------------
         * Step 1: GPT2. One timer, two consumers:
         *   - GTIOC2A -> PD06 = MCLK for the DA7212
         *   - internal connection = SSIE0 AUDIO_CLK (see audio_port.h)
         *
         * FIRST, because the codec's PLL locks to MCLK and step 2 writes the
         * PLL registers, and because r_ssi only sets SSICR.CKS for
         * SSI_AUDIO_CLOCK_INTERNAL (ra/fsp/src/r_ssi/r_ssi.c:259-266) and
         * contains no GPT code at all, so nothing else starts the audio clock.
         *
         * The pin is already configured as the GPT peripheral output
         * (ra_gen/pin_data.c:629-630) and the driver enables the output
         * because gtioca.output_enabled is true (ra_gen/hal_data.c:50-51)
         * which makes r_gpt.c:1412-1421 compute a GTIOR with OAE set
         * (r_gpt.c:1760-1764).
         * -------------------------------------------------------------- */
        err = R_GPT_Open(&g_timer_audio_mclk_ctrl, &g_timer_audio_mclk_cfg);
        if ((FSP_SUCCESS != err) && (FSP_ERR_ALREADY_OPEN != err))
        {
            goto init_failed;
        }

        err = R_GPT_Start(&g_timer_audio_mclk_ctrl);
        if (FSP_SUCCESS != err)
        {
            goto init_failed;
        }

        s_gpt_open = true;
    }

    /* ---------------------------------------------------------------------
     * Step 2: DA7212 over I2C. Uses the IIC1 shared bus lock internally and
     * waits until the camera has released IIC1 (i2c_bus0_open_once()).
     * ------------------------------------------------------------------ */
    err = da7212_init();
    if (FSP_SUCCESS != err)
    {
        goto init_failed;
    }

    /* ---------------------------------------------------------------------
     * Step 3: SSIE0. R_SSI_Open() also opens the DTC transmit instance
     * (r_ssi.c:713-720), so g_transfer_i2s_tx must never be opened here.
     *
     * SSICR.CKDV comes straight from the generated configuration since Issue
     * #202 set "Bit Clock Divider" to Audio Clock / 24 (SSI_CLOCK_DIV_24 in
     * ra_gen/hal_data.c, applied by r_ssi.c:259-266). The stop-gap that used
     * to overwrite CKDV here is gone; `audio status` prints the live SSICR so
     * the field can still be checked on hardware.
     * ------------------------------------------------------------------ */
    if (!s_ssi_open)
    {
        err = R_SSI_Open(&g_i2s_audio_ctrl, &g_i2s_audio_cfg);
        if ((FSP_SUCCESS != err) && (FSP_ERR_ALREADY_OPEN != err))
        {
            goto init_failed;
        }

        s_ssi_open = true;
    }

    s_stop_request = false;
    s_play_idx     = 0;
    s_state        = AUDIO_STATE_READY;
    s_last_error   = FSP_SUCCESS;

    /* Apply the volume that may have been requested before the codec was up. */
    (void)da7212_set_volume(da7212_get_volume());

    snprintf(buf, sizeof(buf),
             "  audio: ready. fs=%lu Hz BCLK=%lu Hz MCLK=%lu Hz buf=%lu frames (%u ms) x2\r\n",
             (unsigned long)AUDIO_SAMPLE_RATE_HZ,
             (unsigned long)AUDIO_BCLK_HZ,
             (unsigned long)AUDIO_MCLK_HZ,
             (unsigned long)AUDIO_BUFFER_FRAMES,
             (unsigned)AUDIO_BUFFER_MS);
    audio_log(buf);

    return FSP_SUCCESS;

init_failed:
    s_last_error = err;
    s_state      = AUDIO_STATE_ERROR;

    snprintf(buf, sizeof(buf), "  audio: init failed (err=0x%lX).\r\n",
             (unsigned long)err);
    audio_log(buf);

    return err;
}

/**
 * Start continuous playback.
 */
fsp_err_t audio_start(audio_fill_cb_t p_fill, void *p_context)
{
    fsp_err_t err;

    if (AUDIO_STATE_READY != s_state)
    {
        /* STOPPING and INITIALIZING are "busy", not "not opened": the device
         * exists and will become usable shortly. STOPPING in particular means
         * the SSI has not reached idle yet, and r_ssi_start() would reject the
         * restart with FSP_ERR_IN_USE anyway (it requires SSISR.IIRQ == 1). */
        if ((AUDIO_STATE_PLAYING == s_state) ||
            (AUDIO_STATE_STOPPING == s_state) ||
            (AUDIO_STATE_INITIALIZING == s_state))
        {
            return FSP_ERR_IN_USE;
        }
        return FSP_ERR_NOT_OPEN;
    }

    s_fill_cb      = (NULL != p_fill) ? p_fill : audio_tone_fill;
    s_fill_context = p_context;

    if (audio_tone_fill == s_fill_cb)
    {
        s_tone_phase = 0;
        audio_tone_update_step();
    }

    /* Pre-fill both buffers before any of them is handed to the DTC. */
    audio_fill_buffer(0);
    audio_fill_buffer(1);

    s_play_idx     = 0;
    s_stop_request = false;
    s_state        = AUDIO_STATE_PLAYING;

    /* Share the free-running DWT with AI/Dave2D; never reset it. No playback
     * has been submitted yet. Publish baseline + timing reset atomically. */
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        const audio_timing_t empty = {0};
        SYSTIM now = {0, 0};
        UINT intsts;
        DI(intsts);
        (void)tk_get_otm(&now);
        s_start_ms       = (uint32_t)now.lo;
        s_start_tx_empty = s_tx_empty_count;
        s_timing = empty;
        s_timing.prov_session_mode = s_prov_configured_mode;
        s_prov_candidate.header.valid = 0U;
        s_timing_session++;
        s_timing_clock = SystemCoreClock;
        s_timing.delay_threshold_cycles = (uint32_t)(((uint64_t)s_timing_clock * AUDIO_DELAY_MS) / 1000U);
        EI(intsts);
    }

    err = R_SSI_Write(&g_i2s_audio_ctrl, &s_pcm_buf[0][0], AUDIO_BUFFER_BYTES);
    if (FSP_SUCCESS != err)
    {
        s_state      = AUDIO_STATE_READY;
        s_last_error = err;
        return err;
    }

    /* Unmute only after the stream is running so the first samples are not a
     * step from silence. LINE_AMP_RAMP_EN / DAC_R_RAMP_EN make the unmute
     * ramp instead of click (DA7212 datasheet 13.14, p38). */
    /*
     * The unmute must NOT be best-effort.
     *
     * da7212_apply_mute() writes DAC_R_CTRL and LINE_CTRL in two SEPARATE I2C
     * transactions, so a failure can leave the codec half-unmuted. Returning
     * FSP_SUCCESS then leaves the stream in AUDIO_STATE_PLAYING and tells the
     * shell "tone playing" while the speaker stays silent, with nothing
     * retrying - exactly the "reports success but no sound" failure mode this
     * driver already cost a long bring-up to diagnose.
     *
     * Retry a few times for a transient bus hiccup, then roll the stream back
     * so the device is not left PLAYING while inaudible.
     */
    for (uint32_t attempt = 0; attempt < AUDIO_UNMUTE_ATTEMPTS; attempt++)
    {
        err = da7212_mute(false);
        if (FSP_SUCCESS == err)
        {
            return FSP_SUCCESS;
        }

        if ((attempt + 1U) < AUDIO_UNMUTE_ATTEMPTS)
        {
            tk_dly_tsk(AUDIO_UNMUTE_RETRY_MS);
        }
    }

    s_last_error = err;
    (void)audio_stop();

    return err;
}

/**
 * Stop playback.
 */
fsp_err_t audio_stop(void)
{
    fsp_err_t err;

    if ((AUDIO_STATE_PLAYING != s_state) && (AUDIO_STATE_STOPPING != s_state))
    {
        return FSP_SUCCESS;
    }

    /* Immediate silence: the codec mute is applied within a few hundred
     * microseconds over I2C, long before the SSI reaches its next frame
     * boundary. */
    (void)da7212_mute(true);

    /* Drop any AUDIO_EVT_IDLE left over from an earlier unsolicited idle so
     * the wait below cannot be satisfied by a stale bit.
     * tk_clr_flg() ANDs the pattern with the current bits. */
    (void)tk_clr_flg(s_audio_flgid, (UINT)~AUDIO_EVT_IDLE);

    /* State first, then the request: the ISR's I2S_EVENT_IDLE handler only
     * forces READY while the state is still PLAYING, so this ordering
     * guarantees the ISR never races the assignment below. */
    s_state        = AUDIO_STATE_STOPPING;
    s_stop_request = true;

    UINT flgptn = 0;
    ER   ercd   = tk_wai_flg(s_audio_flgid,
                             (UINT)AUDIO_EVT_IDLE,
                             TWF_ORW | TWF_BITCLR,
                             &flgptn,
                             (TMO)AUDIO_STOP_TIMEOUT_MS);

    if (E_OK != ercd)
    {
        /* No I2S_EVENT_IDLE: force the stop from task context so the device
         * does not stay half-running. */
        err = R_SSI_Stop(&g_i2s_audio_ctrl);
        if (FSP_SUCCESS != err)
        {
            s_last_error = err;
        }

        /* Stay STOPPING - do NOT advertise READY here.
         *
         * R_SSI_Stop() only REQUESTS the stop; it completes later through
         * I2S_EVENT_IDLE ("Stop is complete after an I2S_EVENT_IDLE
         * interrupt", r_ssi.c R_SSI_Stop). Reporting READY would let a caller
         * run audio_start() while the peripheral is still stopping, and
         * r_ssi_start() rejects exactly that with FSP_ERR_IN_USE because it
         * requires SSISR.IIRQ == 1 before setting TEN.
         *
         * This does not wedge the device: r_ssi_stop_sub() explicitly
         * re-enables the idle interrupt (`ssicr |= 1 << SSI_PRV_SSICR_IIEN_BIT`),
         * so the idle WILL arrive, and audio_i2s_callback() moves
         * STOPPING -> READY when it does. Until then `audio status` honestly
         * shows STOPPING. s_stop_request stays true so the ISR treats the
         * late idle as a requested stop and does not auto-restart the stream. */
        return FSP_ERR_TIMEOUT;
    }

    s_state = AUDIO_STATE_READY;

    return FSP_SUCCESS;
}

/**
 * Set the hardware volume.
 */
fsp_err_t audio_set_volume(uint8_t percent)
{
    return da7212_set_volume(percent);
}

uint8_t audio_get_volume(void)
{
    return da7212_get_volume();
}

/**
 * Configure the built-in test tone.
 */
fsp_err_t audio_set_test_tone(uint32_t freq_hz, uint16_t amplitude)
{
    /* Nyquist: refuse anything the 16 kHz stream cannot represent. */
    if ((0U == freq_hz) || (freq_hz >= (AUDIO_SAMPLE_RATE_HZ / 2U)))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    s_tone_freq_hz   = freq_hz;
    s_tone_amplitude = amplitude;
    audio_tone_update_step();

    return FSP_SUCCESS;
}

audio_state_t audio_get_state(void)
{
    return s_state;
}

uint32_t audio_get_sample_rate(void)
{
    return (uint32_t)AUDIO_SAMPLE_RATE_HZ;
}

audio_fill_cb_t audio_get_fill_cb(void)
{
    return s_fill_cb;
}

/**
 * uT-Kernel task entry.
 *
 * Runs audio_init() once and then sleeps forever. A dedicated task is used
 * (rather than initialising from ntshell_task) because audio_init() blocks
 * for ~0.4 s in the mandatory DA7212 power-up delays and may additionally
 * wait for the camera to release IIC1.
 */
void audio_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    audio_log("[audio_task] initialising audio output (SSIE0 -> DA7212 -> J33).\r\n");

    (void)audio_init();

    tk_slp_tsk(TMO_FEVR);
}

/**********************************************************************************************************************
 NT-Shell command implementation
 *********************************************************************************************************************/

static void audio_cmd_usage(void)
{
    cmd_print_usage("audio", "<subcommand>");
    print_to_console("  status              - Show audio device / codec state\r\n");
    print_to_console("  init                - Retry device initialisation\r\n");
    print_to_console("  start               - Start the test tone\r\n");
    print_to_console("  stop                - Stop playback\r\n");
    print_to_console("  prov <full|nowindow> - Select provenance mode while READY (sampled by start)\r\n");
    print_to_console("  tone <hz> [ampl]    - Set test tone frequency / amplitude\r\n");
    print_to_console("  volume [0-100]      - Show or set the speaker volume\r\n");
    print_to_console("  mute <on|off>       - Mute or unmute the codec\r\n");
    print_to_console("  reg <addr> [value]  - Read or write a DA7212 register\r\n");
}

/** Copy one immutable published record, checking reset under the same mask. */
static bool audio_trace_copy(uint32_t session, uint32_t index, audio_trace_record_t *record)
{
    UINT intsts;
    bool valid;
    DI(intsts);
    valid = (session == s_timing_session) && (index < s_timing.trace_count) &&
            (index < AUDIO_TRACE_CAPACITY);
    if (valid)
    {
        *record = s_trace[index];
    }
    EI(intsts);
    return valid;
}

/** Format outside DI. Each line identifies the snapshot session; a reset
 * invalidates ALL previously printed records from this status invocation. */
static void audio_trace_print(uint32_t session, const audio_timing_t *timing)
{
    char buf[AUDIO_PRINT_BUF_SIZE];
    bool valid = (0U == timing->irq_saturated);
    snprintf(buf, sizeof(buf), "  FIFO no_cb   : tdc0=%lu tdc1_16=%lu tdc17_32=%lu other=%lu\r\n",
             (unsigned long)timing->fifo_no_cb[0], (unsigned long)timing->fifo_no_cb[1],
             (unsigned long)timing->fifo_no_cb[2], (unsigned long)timing->fifo_no_cb[3]);
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Trace        : session=%lu count=%lu full=%u truncated=%lu\r\n",
             (unsigned long)session, (unsigned long)timing->trace_count,
             (unsigned)(timing->trace_count == AUDIO_TRACE_CAPACITY),
             (unsigned long)timing->trace_truncated);
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Wrapper max  : tx_pre=%lu tx_post=%lu int_pre=%lu int_post=%lu cycles\r\n",
             (unsigned long)timing->tx_pre_max, (unsigned long)timing->tx_post_max,
             (unsigned long)timing->int_pre_max, (unsigned long)timing->int_post_max);
    print_to_console(buf);
    print_to_console("  Trace scope  : first 16 events only; FIFO counts cover session; later details omitted when full.\r\n");
    print_to_console("  Trace scope  : sequential reads; DTCE/descriptor_blocks do not prove DTC completion; cycles may wrap.\r\n");
    print_to_console("  Trace scope  : TX post is before fallback refill; later INT/IDLE is not implied.\r\n");
    print_to_console("  Wrapper scope: sampled pre/post sections only; excludes original ISR and timing bookkeeping.\r\n");
    for (uint32_t index = 0U; index < timing->trace_count; index++)
    {
        audio_trace_record_t record;
        if (!audio_trace_copy(session, index, &record))
        {
            valid = false;
            break;
        }
        const char *kind = (record.kind == AUDIO_TRACE_TX_NO_CB) ? "TX-no-cb" :
                           ((record.kind == AUDIO_TRACE_INT_IDLE) ? "INT-IDLE" : "INT-no-IDLE");
        snprintf(buf, sizeof(buf), "  Trace s=%lu #%lu %s cycles=%lu cb=%lu->%lu\r\n",
                 (unsigned long)session, (unsigned long)record.ordinal, kind,
                 (unsigned long)record.cycles, (unsigned long)record.callback_before,
                 (unsigned long)record.callback_after);
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "  Trace s=%lu #%lu pre: tdc=%lu dtce=%lu descriptor_blocks=%lu sw_samples=%lu\r\n",
                 (unsigned long)session, (unsigned long)record.ordinal,
                 (unsigned long)record.pre_tdc, (unsigned long)record.pre_dtce,
                 (unsigned long)record.pre_descriptor_blocks, (unsigned long)record.pre_sw_samples);
        print_to_console(buf);
        if (0U != (record.flags & AUDIO_TRACE_POST_VALID))
        {
            snprintf(buf, sizeof(buf), "  Trace s=%lu #%lu post: tdc=%lu dtce=%lu descriptor_blocks=%lu sw_samples=%lu\r\n",
                     (unsigned long)session, (unsigned long)record.ordinal,
                     (unsigned long)record.post_tdc, (unsigned long)record.post_dtce,
                     (unsigned long)record.post_descriptor_blocks, (unsigned long)record.post_sw_samples);
        }
        else
        {
            snprintf(buf, sizeof(buf), "  Trace s=%lu #%lu post: n/a\r\n",
                     (unsigned long)session, (unsigned long)record.ordinal);
        }
        print_to_console(buf);
    }
    /* Also catches a reset during the last record's UART output, or an empty
     * snapshot. A later start cannot alter the already copied old records. */
    UINT intsts;
    DI(intsts);
    valid = valid && (session == s_timing_session);
    EI(intsts);
    snprintf(buf, sizeof(buf), "  Trace s=%lu trace_valid=%u (0: discard all trace lines above)\r\n",
             (unsigned long)session, (unsigned)valid);
    print_to_console(buf);
}

/** Copy at most one 96-byte record under DI; never print in this section. */
static bool audio_int_trace_copy(uint32_t session, uint32_t index, audio_int_record_t *record)
{
    UINT intsts;
    bool valid;
    DI(intsts);
    valid = (session == s_timing_session) && (index < s_timing.int_trace_count) &&
            (index < AUDIO_INT_TRACE_CAPACITY);
    if (valid)
    {
        *record = s_int_trace[index];
    }
    EI(intsts);
    return valid;
}

/** Dedicated INT history remains available after the shared trace is full. */
static void audio_int_trace_print(uint32_t session, const audio_timing_t *timing)
{
    char buf[AUDIO_PRINT_BUF_SIZE];
    bool valid = (0U == timing->irq_saturated);
    snprintf(buf, sizeof(buf), "  INT trace s=%lu count=%lu capacity=%u omitted=%lu tx_summary_max=%lu cycles\r\n",
             (unsigned long)session, (unsigned long)timing->int_trace_count,
             (unsigned)AUDIO_INT_TRACE_CAPACITY, (unsigned long)timing->int_trace_omitted,
             (unsigned long)timing->tx_summary_max);
    print_to_console(buf);
    print_to_console("  INT scope    : entry registers before FSP; sequential reads; DTCSTS vector valid only if ACT=1.\r\n");
    print_to_console("  Last TX scope: nearest completed TX; exit before summary, not ISR return; DWT intervals < one wrap.\r\n");
    print_to_console("  INT scope    : explicit stops also recorded; full stops extra INT reads and TX summary updates.\r\n");
    for (uint32_t index = 0U; index < timing->int_trace_count; index++)
    {
        audio_int_record_t record;
        if (!audio_int_trace_copy(session, index, &record))
        {
            valid = false;
            break;
        }
        snprintf(buf, sizeof(buf), "  INT s=%lu #%lu %s cycles=%lu idle=%lu->%lu state=%lu stop=%lu\r\n",
                 (unsigned long)session, (unsigned long)record.ordinal,
                 (record.kind == AUDIO_TRACE_INT_IDLE) ? "IDLE" : "no-IDLE",
                 (unsigned long)record.cycles, (unsigned long)record.idle_before,
                 (unsigned long)record.idle_after, (unsigned long)record.state, (unsigned long)record.stop);
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "  INT s=%lu #%lu SSISR=%08lX SSICR=%08lX SSIFCR=%08lX SSIFSR=%08lX\r\n",
                 (unsigned long)session, (unsigned long)record.ordinal,
                 (unsigned long)record.ssisr, (unsigned long)record.ssicr,
                 (unsigned long)record.ssifcr, (unsigned long)record.ssifsr);
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "  INT s=%lu #%lu IELSR=%08lX DTCSTS=%08lX descriptor_blocks=%lu sw_samples=%lu\r\n",
                 (unsigned long)session, (unsigned long)record.ordinal,
                 (unsigned long)record.ielsr, (unsigned long)record.dtcsts,
                 (unsigned long)record.descriptor_blocks, (unsigned long)record.sw_samples);
        print_to_console(buf);
        if (0U != record.last_valid)
        {
            snprintf(buf, sizeof(buf), "  INT s=%lu #%lu last_valid=1 TX#%lu entry=%lu exit=%lu tdc=%lu\r\n",
                     (unsigned long)session, (unsigned long)record.ordinal,
                     (unsigned long)record.last_tx.ordinal, (unsigned long)record.last_tx.entry_cycles,
                     (unsigned long)record.last_tx.exit_cycles, (unsigned long)record.last_tx.entry_tdc);
            print_to_console(buf);
            snprintf(buf, sizeof(buf), "  INT s=%lu #%lu TX delta: native=%lu fallback_attempt=%lu fallback_ok=%lu refill_ok=%lu\r\n",
                     (unsigned long)session, (unsigned long)record.ordinal,
                     (unsigned long)record.last_tx.native_delta, (unsigned long)record.last_tx.fallback_attempt_delta,
                     (unsigned long)record.last_tx.fallback_ok_delta, (unsigned long)record.last_tx.refill_ok_delta);
        }
        else
        {
            snprintf(buf, sizeof(buf), "  INT s=%lu #%lu last_valid=0 TX=n/a\r\n",
                     (unsigned long)session, (unsigned long)record.ordinal);
        }
        print_to_console(buf);
    }
    UINT intsts;
    DI(intsts);
    valid = valid && (session == s_timing_session) && (0U == s_timing.irq_saturated);
    EI(intsts);
    snprintf(buf, sizeof(buf), "  INT trace s=%lu int_trace_valid=%u (0: discard all INT trace lines above)\r\n",
             (unsigned long)session, (unsigned)valid);
    print_to_console(buf);
}

/** Only published records can be copied; one bounded record per DI section. */
static bool audio_delay_copy(uint32_t session, uint32_t index, audio_delay_record_t *record)
{
    UINT intsts;
    bool valid;
    DI(intsts);
    valid = (session == s_timing_session) && (index < s_timing.delay_count) &&
            (index < AUDIO_DELAY_CAPACITY);
    if (valid)
    {
        *record = s_delay_trace[index];
    }
    EI(intsts);
    return valid;
}

/** Raw time pairs are retained: neither clock is an independent wall clock,
 * and modulo DWT differences are not corrected by guessing a wrap count. */
static void audio_delay_print(uint32_t session, const audio_timing_t *timing)
{
    char buf[AUDIO_PRINT_BUF_SIZE];
    bool valid = (0U == timing->irq_saturated);
    snprintf(buf, sizeof(buf), "  Delay s=%lu count=%lu capacity=%u full=%u threshold_ms=%u threshold_cycles=%lu\r\n",
             (unsigned long)session, (unsigned long)timing->delay_count,
             (unsigned)AUDIO_DELAY_CAPACITY, (unsigned)(timing->delay_count == AUDIO_DELAY_CAPACITY),
             (unsigned)AUDIO_DELAY_MS, (unsigned long)timing->delay_threshold_cycles);
    print_to_console(buf);
    print_to_console("  Delay reason : 1=previous DWT, 2=previous OS, 4=successful TX DWT, 8=successful TX OS; bits combine.\r\n");
    print_to_console("  Delay scope  : TX entry times, not Write completion or silence; sequential reads; clocks can both lag.\r\n");
    print_to_console("  Delay scope  : time_ok=0 means ignore ms; clock_ok=0 disables DWT threshold; no wrap correction.\r\n");
    print_to_console("  Delay scope  : success kind 1=native 2=fallback; excludes start/IDLE prefill; four episodes then no sampling.\r\n");
    for (uint32_t index = 0U; index < timing->delay_count; index++)
    {
        audio_delay_record_t record;
        if (!audio_delay_copy(session, index, &record))
        {
            valid = false;
            break;
        }
        snprintf(buf, sizeof(buf), "  Delay s=%lu #%lu reason=%lu state=%lu stop=%lu tx=%lu refill=%lu\r\n",
                 (unsigned long)session, (unsigned long)record.ordinal, (unsigned long)record.reason,
                 (unsigned long)record.state, (unsigned long)record.stop,
                 (unsigned long)record.tx_count, (unsigned long)record.refill_count);
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "  Delay s=%lu #%lu now: cycles=%lu ms=%lu time_ok=%u err=%08lX\r\n",
                 (unsigned long)session, (unsigned long)record.ordinal,
                 (unsigned long)record.current.cycles, (unsigned long)record.current.ms,
                 (unsigned)(record.current.time_error == (uint32_t)E_OK), (unsigned long)record.current.time_error);
        print_to_console(buf);
        if (0U != record.prev_valid)
        {
            snprintf(buf, sizeof(buf), "  Delay s=%lu #%lu prev: cycles=%lu ms=%lu time_ok=%u err=%08lX\r\n",
                     (unsigned long)session, (unsigned long)record.ordinal,
                     (unsigned long)record.previous.cycles, (unsigned long)record.previous.ms,
                     (unsigned)(record.previous.time_error == (uint32_t)E_OK), (unsigned long)record.previous.time_error);
        }
        else
        {
            snprintf(buf, sizeof(buf), "  Delay s=%lu #%lu prev: n/a\r\n", (unsigned long)session, (unsigned long)record.ordinal);
        }
        print_to_console(buf);
        if (0U != record.success_valid)
        {
            snprintf(buf, sizeof(buf), "  Delay s=%lu #%lu success: cycles=%lu ms=%lu time_ok=%u err=%08lX\r\n",
                     (unsigned long)session, (unsigned long)record.ordinal,
                     (unsigned long)record.success.cycles, (unsigned long)record.success.ms,
                     (unsigned)(record.success.time_error == (uint32_t)E_OK), (unsigned long)record.success.time_error);
            print_to_console(buf);
            snprintf(buf, sizeof(buf), "  Delay s=%lu #%lu success: TX#%lu kind=%lu index=%lu address=%08lX\r\n",
                     (unsigned long)session, (unsigned long)record.ordinal,
                     (unsigned long)record.success_ordinal, (unsigned long)record.success_kind,
                     (unsigned long)record.success_index, (unsigned long)record.success_address);
        }
        else
        {
            snprintf(buf, sizeof(buf), "  Delay s=%lu #%lu success: n/a\r\n", (unsigned long)session, (unsigned long)record.ordinal);
        }
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "  Delay s=%lu #%lu clock=%lu clock_ok=%u DWT_CTRL=%08lX DEMCR=%08lX\r\n",
                 (unsigned long)session, (unsigned long)record.ordinal, (unsigned long)record.clock,
                 (unsigned)(timing->delay_threshold_cycles != 0U), (unsigned long)record.dwt_ctrl, (unsigned long)record.demcr);
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "  Delay s=%lu #%lu SSISR=%08lX SSICR=%08lX SSIFCR=%08lX SSIFSR=%08lX\r\n",
                 (unsigned long)session, (unsigned long)record.ordinal,
                 (unsigned long)record.ssisr, (unsigned long)record.ssicr,
                 (unsigned long)record.ssifcr, (unsigned long)record.ssifsr);
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "  Delay s=%lu #%lu IELSR=%08lX DTCSTS=%08lX blocks=%lu source=%08lX\r\n",
                 (unsigned long)session, (unsigned long)record.ordinal,
                 (unsigned long)record.ielsr, (unsigned long)record.dtcsts,
                 (unsigned long)record.descriptor_blocks, (unsigned long)record.descriptor_source);
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "  Delay s=%lu #%lu sw_samples=%lu play_index=%lu\r\n",
                 (unsigned long)session, (unsigned long)record.ordinal,
                 (unsigned long)record.sw_samples, (unsigned long)record.play_index);
        print_to_console(buf);
    }
    UINT intsts;
    DI(intsts);
    valid = valid && (session == s_timing_session) && (0U == s_timing.irq_saturated);
    EI(intsts);
    snprintf(buf, sizeof(buf), "  Delay s=%lu delay_valid=%u (0: discard all Delay lines above)\r\n",
             (unsigned long)session, (unsigned)valid);
    print_to_console(buf);
}

/** Header and snapshots are copied separately to bound DI duration. */
static bool audio_prov_header_copy(uint32_t session, uint32_t index, uint32_t expectedordinal,
                                   audio_prov_header_t *header)
{
    UINT intsts;
    bool valid;
    DI(intsts);
    valid = (session == s_timing_session) && (index < s_timing.delay_count) && (index < AUDIO_DELAY_CAPACITY);
    if (valid)
    {
        *header = s_prov_delay[index].header;
        valid = (0U == header->valid) ||
                ((header->ordinal == expectedordinal) && (0U != s_delay_trace[index].success_valid) &&
                 (header->ordinal == s_delay_trace[index].success_ordinal));
    }
    EI(intsts);
    return valid;
}

static bool audio_prov_snapshot_copy(uint32_t session, uint32_t index, uint32_t expectedordinal,
                                     uint32_t which, audio_prov_snapshot_t *sample)
{
    UINT intsts;
    bool valid;
    DI(intsts);
    valid = (session == s_timing_session) && (index < s_timing.delay_count) &&
            (index < AUDIO_DELAY_CAPACITY) && (which < 3U);
    if (valid)
    {
        valid = (0U != s_prov_delay[index].header.valid) &&
                (s_prov_delay[index].header.ordinal == expectedordinal) &&
                (0U != (s_prov_delay[index].header.phase_mask & (1U << which))) &&
                (0U != s_delay_trace[index].success_valid) &&
                (s_delay_trace[index].success_ordinal == expectedordinal);
        if (valid)
        {
            if (0U == which) { *sample = s_prov_delay[index].entry; }
            else if (1U == which) { *sample = s_prov_delay[index].pre; }
            else { *sample = s_prov_delay[index].post; }
        }
    }
    EI(intsts);
    return valid;
}

/** No provenance-sized local or DI copy. Association is checked for every
 * chunk and again after its output; a session switch invalidates all lines. */
static void audio_prov_print(uint32_t session, const audio_timing_t *timing)
{
    char buf[AUDIO_PRINT_BUF_SIZE];
    bool valid = (0U == timing->irq_saturated);
    if (AUDIO_PROV_FULL == timing->prov_session_mode)
    {
        snprintf(buf, sizeof(buf), "  Prov max s=%lu entry=%lu pre=%lu post=%lu aux=%lu cycles\r\n",
                 (unsigned long)session, (unsigned long)timing->prov_entry_max,
                 (unsigned long)timing->prov_pre_max, (unsigned long)timing->prov_post_max,
                 (unsigned long)timing->prov_aux_max);
    }
    else
    {
        snprintf(buf, sizeof(buf), "  Prov max s=%lu entry=%lu pre=disabled post=disabled aux=%lu cycles\r\n",
                 (unsigned long)session, (unsigned long)timing->prov_entry_max,
                 (unsigned long)timing->prov_aux_max);
    }
    print_to_console(buf);
    print_to_console("  Prov scope   : sequential CPU observations; POST precedes TIE1; not proof of DTC internal values.\r\n");
    print_to_console("  Prov scope   : maxima are sampled read/copy sections, excluding bookkeeping; may include preemption.\r\n");
    for (uint32_t index = 0U; index < timing->delay_count; index++)
    {
        audio_prov_header_t header;
        uint32_t expectedordinal;
        UINT intsts;
        /* Only fetch the association word, not another whole Delay record. */
        DI(intsts);
        bool present = (session == s_timing_session) && (index < s_timing.delay_count) &&
                       (index < AUDIO_DELAY_CAPACITY);
        expectedordinal = present ? s_delay_trace[index].success_ordinal : 0U;
        EI(intsts);
        if (!present || !audio_prov_header_copy(session, index, expectedordinal, &header))
        {
            valid = false;
            break;
        }
        if (0U == header.valid)
        {
            snprintf(buf, sizeof(buf), "  Prov s=%lu delay_index=%lu history=n/a\r\n",
                     (unsigned long)session, (unsigned long)index);
            print_to_console(buf);
            continue;
        }
        snprintf(buf, sizeof(buf), "  Prov s=%lu delay_index=%lu TX#%lu kind=%lu index=%lu address=%08lX\r\n",
                 (unsigned long)session, (unsigned long)index, (unsigned long)header.ordinal,
                 (unsigned long)header.kind, (unsigned long)header.index, (unsigned long)header.address);
        print_to_console(buf);
        snprintf(buf, sizeof(buf), "  Prov s=%lu TX#%lu bytes=%lu blocks=%lu Write=%08lX phase_mask=%lu\r\n",
                 (unsigned long)session, (unsigned long)header.ordinal,
                 (unsigned long)header.bytes, (unsigned long)header.blocks, (unsigned long)header.write_error,
                 (unsigned long)header.phase_mask);
        print_to_console(buf);
        for (uint32_t which = 0U; which < 3U; which++)
        {
            audio_prov_snapshot_t sample;
            const char *point = (0U == which) ? "ENTRY" : ((1U == which) ? "PRE" : "POST");
            if (0U == (header.phase_mask & (1U << which)))
            {
                snprintf(buf, sizeof(buf), "  Prov s=%lu TX#%lu %s disabled/n/a\r\n",
                         (unsigned long)session, (unsigned long)header.ordinal, point);
                print_to_console(buf);
                continue;
            }
            if (!audio_prov_snapshot_copy(session, index, expectedordinal, which, &sample))
            {
                valid = false;
                break;
            }
            snprintf(buf, sizeof(buf), "  Prov s=%lu TX#%lu %s cycles=%lu->%lu CRB=%lu->%lu\r\n",
                     (unsigned long)session, (unsigned long)header.ordinal, point,
                     (unsigned long)sample.cycles_begin, (unsigned long)sample.cycles_end,
                     (unsigned long)sample.crb_begin, (unsigned long)sample.crb_end);
            print_to_console(buf);
            snprintf(buf, sizeof(buf), "  Prov s=%lu TX#%lu %s IELSR=%08lX->%08lX DTCSTS=%08lX->%08lX\r\n",
                     (unsigned long)session, (unsigned long)header.ordinal, point,
                     (unsigned long)sample.ielsr_begin, (unsigned long)sample.ielsr_end,
                     (unsigned long)sample.dtcsts_begin, (unsigned long)sample.dtcsts_end);
            print_to_console(buf);
            snprintf(buf, sizeof(buf), "  Prov s=%lu TX#%lu %s SAR=%08lX CRA=%lu settings=%08lX SSIFSR=%08lX\r\n",
                     (unsigned long)session, (unsigned long)header.ordinal, point,
                     (unsigned long)sample.sar, (unsigned long)sample.cra,
                     (unsigned long)sample.settings, (unsigned long)sample.ssifsr);
            print_to_console(buf);
        }
        if (!audio_prov_header_copy(session, index, expectedordinal, &header))
        {
            valid = false;
        }
        if (!valid) { break; }
    }
    UINT intsts;
    DI(intsts);
    valid = valid && (session == s_timing_session) && (0U == s_timing.irq_saturated);
    EI(intsts);
    snprintf(buf, sizeof(buf), "  Prov s=%lu provenance_valid=%u (0: discard all Prov lines above)\r\n",
             (unsigned long)session, (unsigned)valid);
    print_to_console(buf);
}

static void audio_cmd_status(void)
{
    char        buf[AUDIO_PRINT_BUF_SIZE];
    const char *state_str;
    audio_timing_t timing;
    SYSTIM now = {0, 0};
    uint32_t session, clock_hz, start_ms, start_tx, tx, idle, errors, restarts;
    uint32_t configured_mode;
    audio_state_t state;
    fsp_err_t last_error;
    audio_fill_cb_t producer;
    ER time_error;
    UINT intsts;

    /* A bounded copy only; formatting and UART output must remain outside.
     * tk_get_otm nests its own DI/EI and restores this mask on return. */
    DI(intsts);
    time_error = tk_get_otm(&now);
    timing = s_timing;
    session = s_timing_session;
    configured_mode = s_prov_configured_mode;
    clock_hz = s_timing_clock;
    start_ms = s_start_ms;
    start_tx = s_start_tx_empty;
    tx = s_tx_empty_count;
    idle = s_idle_count;
    errors = s_error_count;
    restarts = s_restart_count;
    state = s_state;
    last_error = s_last_error;
    producer = s_fill_cb;
    EI(intsts);

    switch (state)
    {
        case AUDIO_STATE_INITIALIZING: state_str = "INITIALIZING"; break;
        case AUDIO_STATE_READY:    state_str = "READY";    break;
        case AUDIO_STATE_PLAYING:  state_str = "PLAYING";  break;
        case AUDIO_STATE_STOPPING: state_str = "STOPPING"; break;
        case AUDIO_STATE_ERROR:    state_str = "ERROR";    break;
        default:                   state_str = "UNINITIALIZED"; break;
    }

    print_to_console("Audio output (SSIE0 I2S master -> DA7212 -> J33)\r\n");

    snprintf(buf, sizeof(buf), "  State        : %s (last err=0x%lX)\r\n",
             state_str, (unsigned long)last_error);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Format       : %lu Hz, %u bit, %u ch (I2S frame)\r\n",
             (unsigned long)AUDIO_SAMPLE_RATE_HZ,
             (unsigned)AUDIO_SAMPLE_BITS,
             (unsigned)AUDIO_CHANNELS);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Clocks       : MCLK %lu Hz, BCLK %lu Hz, WCLK %lu Hz\r\n",
             (unsigned long)AUDIO_MCLK_HZ,
             (unsigned long)AUDIO_BCLK_HZ,
             (unsigned long)AUDIO_SAMPLE_RATE_HZ);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Buffers      : 2 x %lu frames (%u ms, %lu bytes)\r\n",
             (unsigned long)AUDIO_BUFFER_FRAMES,
             (unsigned)AUDIO_BUFFER_MS,
             (unsigned long)AUDIO_BUFFER_BYTES);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  ISR counters : tx_empty=%lu idle=%lu err=%lu restart=%lu\r\n",
             (unsigned long)tx,
             (unsigned long)idle,
             (unsigned long)errors,
             (unsigned long)restarts);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Snapshot     : session=%lu time_ms=%lu clock=%lu producer=%s time_ok=%u\r\n",
             (unsigned long)session, (unsigned long)(uint32_t)now.lo,
             (unsigned long)clock_hz,
             (NULL == producer) ? "none" : ((audio_tone_fill == producer) ? "tone" : "callback"),
             (unsigned)(E_OK == time_error));
    print_to_console(buf);

    /* Raw cycles retain sub-microsecond resolution without ISR division.
     * Zero maxima mean no completed samples or zero elapsed cycles. */
    snprintf(buf, sizeof(buf), "  DWT cycles   : gap_max=%lu tx_fill_max=%lu idle_fill_max=%lu\r\n",
             (unsigned long)timing.gap_max, (unsigned long)timing.tx_fill_max,
             (unsigned long)timing.idle_fill_max);
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Write max    : tx_ok=%lu tx_fail=%lu idle_ok=%lu idle_fail=%lu cycles\r\n",
             (unsigned long)timing.tx_write_ok_max, (unsigned long)timing.tx_write_fail_max,
             (unsigned long)timing.idle_write_ok_max, (unsigned long)timing.idle_write_fail_max);
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Recovery     : ok=%lu fail=%lu ok_max=%lu fail_max=%lu cycles\r\n",
             (unsigned long)timing.recover_ok_count, (unsigned long)timing.recover_fail_count,
             (unsigned long)timing.recover_ok_max, (unsigned long)timing.recover_fail_max);
    print_to_console(buf);
    print_to_console("  Timing scope : gap=native callback; TX fill/write=native+fallback; intervals < one DWT wrap.\r\n");
    print_to_console("  Timing scope : recovery excludes pre-IDLE delay.\r\n");
    snprintf(buf, sizeof(buf), "  IRQ entry    : tx=%lu tx_no_cb=%lu no_cb_run_max=%lu\r\n",
             (unsigned long)timing.tx_irq_count, (unsigned long)timing.tx_no_callback,
             (unsigned long)timing.tx_no_callback_run_max);
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  IRQ entry    : int=%lu int_no_idle=%lu saturated=%lu (session counters)\r\n",
             (unsigned long)timing.int_irq_count, (unsigned long)timing.int_no_idle,
             (unsigned long)timing.irq_saturated);
    print_to_console(buf);

    audio_trace_print(session, &timing);
    audio_int_trace_print(session, &timing);
    audio_delay_print(session, &timing);
    audio_prov_print(session, &timing);
    snprintf(buf, sizeof(buf), "  Prov mode    : configured=%s session=%s (selected value sampled at start session snapshot)\r\n",
             (AUDIO_PROV_FULL == configured_mode) ? "full" : "nowindow",
             (0U == session) ? "n/a" : ((AUDIO_PROV_FULL == timing.prov_session_mode) ? "full" : "nowindow"));
    print_to_console(buf);
    print_to_console("  Write scope  : TX maxima include TIE operations and enabled PRE/POST reads, not API time alone.\r\n");

    snprintf(buf, sizeof(buf), "  Fallback     : attempt=%lu ok=%lu underflow=%lu fail=%lu stop_fail=%lu\r\n",
             (unsigned long)timing.fallback_attempt, (unsigned long)timing.fallback_ok,
             (unsigned long)timing.fallback_underflow, (unsigned long)timing.fallback_fail,
             (unsigned long)timing.fallback_stop_fail);
    print_to_console(buf);
    snprintf(buf, sizeof(buf), "  Refill       : ok=%lu gap_max=%lu fallback_max=%lu cycles\r\n",
             (unsigned long)timing.refill_ok, (unsigned long)timing.refill_gap_max,
             (unsigned long)timing.fallback_max);
    print_to_console(buf);
    print_to_console("  Refill scope : native+fallback Write successes; excludes start/IDLE prefill; gaps < one DWT wrap.\r\n");
    print_to_console("  Fallback max : guard+refill elapsed cycles; excluded from wrapper pre/post maxima.\r\n");

    /* Live SSI hardware state - TEN tells whether the transmitter is actually
     * running right now, independently of the software state above.
     * SSICR bit 1 = TEN, bit 3 = MUEN (R7KA8P1KF_core0.h SSICR_b). */
    {
        uint32_t ssicr = R_SSI0->SSICR;
        uint32_t ssisr = R_SSI0->SSISR;

        snprintf(buf, sizeof(buf),
                 "  SSI regs     : SSICR=0x%08lX (TEN=%lu MUEN=%lu) SSISR=0x%08lX\r\n",
                 (unsigned long)ssicr,
                 (unsigned long)((ssicr >> 1) & 1U),
                 (unsigned long)((ssicr >> 3) & 1U),
                 (unsigned long)ssisr);
        print_to_console(buf);
    }

    /* Delivered buffers against kernel uptime, not an observed bit clock.
     * Timing maxima remain readable after stop; a rate is shown only while
     * PLAYING so time spent stopped is not interpreted as lost audio. */
    if ((0U != session) && (AUDIO_STATE_PLAYING == state) && (E_OK == time_error))
    {
        uint32_t elapsed_ms = (uint32_t)now.lo - start_ms;
        uint32_t bufs       = tx - start_tx;

        if (elapsed_ms >= 100U)
        {
            uint32_t rate_x100 = (uint32_t)(((uint64_t)bufs * 100000U) / elapsed_ms);
            uint32_t expect_x100 = (AUDIO_SAMPLE_RATE_HZ * 100U) / AUDIO_BUFFER_FRAMES;

            snprintf(buf, sizeof(buf),
                     "  Delivery     : %lu native cb in %lu ms = %lu.%02lu cb/s (expect %lu.%02lu)\r\n",
                     (unsigned long)bufs,
                     (unsigned long)elapsed_ms,
                     (unsigned long)(rate_x100 / 100U), (unsigned long)(rate_x100 % 100U),
                     (unsigned long)(expect_x100 / 100U), (unsigned long)(expect_x100 % 100U));
            print_to_console(buf);
            uint32_t refill_rate_x100 = (uint32_t)(((uint64_t)timing.refill_ok * 100000U) / elapsed_ms);
            snprintf(buf, sizeof(buf),
                     "  Refill rate  : %lu buf in %lu ms = %lu.%02lu buf/s (expect %lu.%02lu)\r\n",
                     (unsigned long)timing.refill_ok, (unsigned long)elapsed_ms,
                     (unsigned long)(refill_rate_x100 / 100U), (unsigned long)(refill_rate_x100 % 100U),
                     (unsigned long)(expect_x100 / 100U), (unsigned long)(expect_x100 % 100U));
            print_to_console(buf);
        }
    }

    snprintf(buf, sizeof(buf), "  Test tone    : %lu Hz, amplitude %u (square)\r\n",
             (unsigned long)s_tone_freq_hz, (unsigned)s_tone_amplitude);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  I2C bus0     : %s\r\n",
             i2c_bus0_is_ready() ? "open" : "not open");
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  DA7212       : %s, PLL %s, volume %u%% (LINE_AMP_GAIN=0x%02X), %s\r\n",
             da7212_is_ready() ? "ready" : "not ready",
             da7212_pll_is_locked() ? "locked" : "UNLOCKED",
             (unsigned)da7212_get_volume(),
             (unsigned)da7212_get_volume_code(),
             da7212_is_muted() ? "muted" : "unmuted");
    print_to_console(buf);
}

/**
 * NT-Shell "audio" command handler.
 *
 * @note Runs in ntshell_task context. Playback is asynchronous (ISR driven),
 *       so "audio start" returns immediately and the shell stays responsive.
 */
static int audio_cmd_prov(int argc, char **argv)
{
    if (argc != 3)
    {
        cmd_print_usage("audio prov", "<full|nowindow>");
        return CMD_ERR_USAGE;
    }
    uint32_t mode;
    if (0 == ntlibc_strcmp(argv[2], "full"))
    {
        mode = AUDIO_PROV_FULL;
    }
    else if (0 == ntlibc_strcmp(argv[2], "nowindow"))
    {
        mode = AUDIO_PROV_NOWINDOW;
    }
    else
    {
        cmd_print_error("Provenance mode must be full or nowindow.");
        return CMD_ERR_INVALID_ARG;
    }
    UINT intsts;
    DI(intsts);
    bool ready = (AUDIO_STATE_READY == s_state);
    if (ready)
    {
        s_prov_configured_mode = mode;
    }
    EI(intsts);
    if (!ready)
    {
        cmd_print_error("Provenance mode can only be selected while READY.");
        return CMD_ERR_EXECUTE;
    }
    print_to_console((AUDIO_PROV_FULL == mode) ?
                     "audio prov: full selected (sampled by start).\r\n" :
                     "audio prov: nowindow selected (sampled by start).\r\n");
    return CMD_OK;
}

int usrcmd_audio(int argc, char **argv)
{
    char      buf[AUDIO_PRINT_BUF_SIZE];
    fsp_err_t err;

    if (argc < 2)
    {
        audio_cmd_usage();
        return CMD_ERR_USAGE;
    }

    if (0 == ntlibc_strcmp(argv[1], "prov"))
    {
        return audio_cmd_prov(argc, argv);
    }

    if (0 == ntlibc_strcmp(argv[1], "status"))
    {
        audio_cmd_status();
        return CMD_OK;
    }

    if (0 == ntlibc_strcmp(argv[1], "init"))
    {
        err = audio_init();
        snprintf(buf, sizeof(buf), "audio init: %s (err=0x%lX)\r\n",
                 (FSP_SUCCESS == err) ? "OK" : "FAILED", (unsigned long)err);
        print_to_console(buf);
        return (FSP_SUCCESS == err) ? CMD_OK : CMD_ERR_EXECUTE;
    }

    if (0 == ntlibc_strcmp(argv[1], "start"))
    {
        err = audio_start(NULL, NULL);
        if (FSP_SUCCESS != err)
        {
            snprintf(buf, sizeof(buf), "audio start failed (err=0x%lX)\r\n",
                     (unsigned long)err);
            print_to_console(buf);
            return CMD_ERR_EXECUTE;
        }

        snprintf(buf, sizeof(buf), "audio start: %lu Hz test tone playing.\r\n",
                 (unsigned long)s_tone_freq_hz);
        print_to_console(buf);
        return CMD_OK;
    }

    if (0 == ntlibc_strcmp(argv[1], "stop"))
    {
        err = audio_stop();
        snprintf(buf, sizeof(buf), "audio stop: %s (err=0x%lX)\r\n",
                 (FSP_SUCCESS == err) ? "OK" : "TIMEOUT", (unsigned long)err);
        print_to_console(buf);
        return (FSP_SUCCESS == err) ? CMD_OK : CMD_ERR_EXECUTE;
    }

    if (0 == ntlibc_strcmp(argv[1], "tone"))
    {
        if (argc < 3)
        {
            cmd_print_usage("audio tone", "<hz> [amplitude]");
            return CMD_ERR_USAGE;
        }

        cmd_parse_result_t freq = cmd_parse_uint32(argv[2]);
        if (!freq.valid)
        {
            cmd_print_error("Invalid frequency.");
            return CMD_ERR_INVALID_ARG;
        }

        uint16_t ampl = s_tone_amplitude;
        if (argc >= 4)
        {
            cmd_parse_result_t a = cmd_parse_uint32(argv[3]);
            if ((!a.valid) || (a.value > 32767U))
            {
                cmd_print_error("Invalid amplitude (0-32767).");
                return CMD_ERR_INVALID_ARG;
            }
            ampl = (uint16_t)a.value;
        }

        err = audio_set_test_tone(freq.value, ampl);
        if (FSP_SUCCESS != err)
        {
            snprintf(buf, sizeof(buf),
                     "Frequency must be 1..%lu Hz (below Nyquist).\r\n",
                     (unsigned long)((AUDIO_SAMPLE_RATE_HZ / 2U) - 1U));
            print_to_console(buf);
            return CMD_ERR_INVALID_ARG;
        }

        snprintf(buf, sizeof(buf), "audio tone: %lu Hz, amplitude %u.\r\n",
                 (unsigned long)s_tone_freq_hz, (unsigned)s_tone_amplitude);
        print_to_console(buf);
        return CMD_OK;
    }

    if (0 == ntlibc_strcmp(argv[1], "volume"))
    {
        if (argc < 3)
        {
            snprintf(buf, sizeof(buf), "audio volume: %u%% (LINE_AMP_GAIN=0x%02X)\r\n",
                     (unsigned)da7212_get_volume(), (unsigned)da7212_get_volume_code());
            print_to_console(buf);
            return CMD_OK;
        }

        cmd_parse_result_t vol = cmd_parse_uint32(argv[2]);
        if ((!vol.valid) || (vol.value > 100U))
        {
            cmd_print_error("Volume must be 0-100.");
            return CMD_ERR_INVALID_ARG;
        }

        err = audio_set_volume((uint8_t)vol.value);
        snprintf(buf, sizeof(buf), "audio volume: %u%% -> %s (err=0x%lX)\r\n",
                 (unsigned)vol.value,
                 (FSP_SUCCESS == err) ? "OK" : "FAILED",
                 (unsigned long)err);
        print_to_console(buf);
        return (FSP_SUCCESS == err) ? CMD_OK : CMD_ERR_EXECUTE;
    }

    if (0 == ntlibc_strcmp(argv[1], "mute"))
    {
        if (argc < 3)
        {
            cmd_print_usage("audio mute", "<on|off>");
            return CMD_ERR_USAGE;
        }

        bool mute;
        if (0 == ntlibc_strcmp(argv[2], "on"))
        {
            mute = true;
        }
        else if (0 == ntlibc_strcmp(argv[2], "off"))
        {
            mute = false;
        }
        else
        {
            cmd_print_usage("audio mute", "<on|off>");
            return CMD_ERR_INVALID_ARG;
        }

        err = da7212_mute(mute);
        snprintf(buf, sizeof(buf), "audio mute %s: %s (err=0x%lX)\r\n",
                 mute ? "on" : "off",
                 (FSP_SUCCESS == err) ? "OK" : "FAILED",
                 (unsigned long)err);
        print_to_console(buf);
        return (FSP_SUCCESS == err) ? CMD_OK : CMD_ERR_EXECUTE;
    }

    if (0 == ntlibc_strcmp(argv[1], "reg"))
    {
        if (argc < 3)
        {
            cmd_print_usage("audio reg", "<addr> [value]");
            return CMD_ERR_USAGE;
        }

        cmd_parse_result_t addr = cmd_parse_uint32(argv[2]);
        if ((!addr.valid) || (addr.value > 0xFFU))
        {
            cmd_print_error("Register address must be 0x00-0xFF.");
            return CMD_ERR_INVALID_ARG;
        }

        if (argc >= 4)
        {
            cmd_parse_result_t val = cmd_parse_uint32(argv[3]);
            if ((!val.valid) || (val.value > 0xFFU))
            {
                cmd_print_error("Register value must be 0x00-0xFF.");
                return CMD_ERR_INVALID_ARG;
            }

            err = da7212_write_reg((uint8_t)addr.value, (uint8_t)val.value);
            snprintf(buf, sizeof(buf), "DA7212[0x%02X] <- 0x%02X : %s (err=0x%lX)\r\n",
                     (unsigned)addr.value, (unsigned)val.value,
                     (FSP_SUCCESS == err) ? "OK" : "FAILED",
                     (unsigned long)err);
            print_to_console(buf);
            return (FSP_SUCCESS == err) ? CMD_OK : CMD_ERR_EXECUTE;
        }

        uint8_t value = 0;
        err = da7212_read_reg((uint8_t)addr.value, &value);
        if (FSP_SUCCESS != err)
        {
            snprintf(buf, sizeof(buf), "DA7212[0x%02X] read failed (err=0x%lX)\r\n",
                     (unsigned)addr.value, (unsigned long)err);
            print_to_console(buf);
            return CMD_ERR_EXECUTE;
        }

        snprintf(buf, sizeof(buf), "DA7212[0x%02X] = 0x%02X\r\n",
                 (unsigned)addr.value, (unsigned)value);
        print_to_console(buf);
        return CMD_OK;
    }

    snprintf(buf, sizeof(buf), "Error: Unknown sub-command '%s'.\r\n", argv[1]);
    print_to_console(buf);
    audio_cmd_usage();

    return CMD_ERR_INVALID_ARG;
}
