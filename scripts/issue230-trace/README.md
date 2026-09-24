# Issue #230 boot trace overlay

Design: `doc/design/issue-230.md` sections 21–22. User approved implementation after the J10-PC acquisition amendment.

This directory contains experimental user-source instrumentation, a build script, a guarded binary patcher, a log reader, and host validation tests. It does not modify the normal checkout's firmware or any generated FSP source. The build script creates a separate copy of the preserved G project under `e2studio_CPU0/Debug/issue230/trace-r1/`.

The normal build system is not changed. `build.py` reuses the original G's unchanged LLVM LTO objects and response files, recompiles only the five instrumented user sources plus `boot_trace.c`, and links once. The baseline ELF is SHA-256 guarded. The full source changes and reused object hashes are saved as `instrumentation.diff` and `build-provenance.json`.

`make_pair.py` is deliberately specific to the audited trace-r1 ELF hash and instruction addresses. A rebuild at a different path or with different inputs may change the hash; it must be re-audited before adapting the patcher. It skips RCR2/RCR1/TCEN operations while retaining the 200us delay, RCR4, and r9 initialization. It changes only two Thumb branch halfwords. It verifies all other ELF bytes, section/program headers, addressed MOT load bytes (including INIT_ARRAY), checksums, and CPU1 provenance.

The current output and user procedure are in `e2studio_CPU0/Debug/issue230/trace-r1/artifacts/READ-ME.md`. Do not overwrite or regenerate measured output in place. The scripts do not connect to hardware.

The approved wait-only follow-up (design sections 26–27) is implemented in `make_wait_pair.py`. It assembles a shared BL/MOVW/B.W path inside W_trace's skipped RTC region and varies only the MOVW immediate (1 versus 1000). Outputs are in `e2studio_CPU0/Debug/issue230/wait-r1/artifacts/`; read that directory's READ-ME.md for the current AC-only test sequence. The existing trace-r1 measurements remain intact. Tiny ELFs under wait-r1/assembly are encoding probes, not flashable firmware.

## Host commands

Run from the repository root with the available Python runtime. Building needs the LLVM 21.1.1 installation named in `build.py` and the saved G project.

```powershell
python scripts/issue230-trace/build.py
python scripts/issue230-trace/make_pair.py
python scripts/issue230-trace/make_wait_pair.py
python -m unittest discover -s scripts/issue230-trace -p "test_*.py" -v
python scripts/issue230-trace/read_log.py G-boot1.txt
```

Supply `--preos-upper-bound-ms` to the log reader only with an independently observed bound shorter than one DWT wrap. Without that bound, the reader reports raw cycles and `unresolved_wrap_bound`, never a fabricated absolute pre-OS time. A stopped DWT probe or incomplete slots prevent time acceptance. OS time remains separate and quantized to 10ms. Host tests exercise log truncation/duplicates, a foreign firmware fingerprint, a stopped counter, invalid bounds, S-record corruption, and the ELF constructor-table conversion.

## Callers and storage

All added entry points have bounded, fixed callers:

| Entry point | Caller/context |
|---|---|
| boot_trace_clock_start | R_BSP_WarmStart POST_CLOCK, pre-C-init; registers only |
| boot_trace_post_c | R_BSP_WarmStart POST_C, after internal C init |
| boot_trace_cycles | R_BSP_WarmStart before/after the SDRAM pair; OS-start constructor |
| boot_trace_os | LVGL task through lvgl_thread_entry, glcdc_port_init, lvgl_port_mtk3_open |
| usrcmd_boottrace | NT-Shell cmdlist registration → usrcmd_execute → ntopt callback |

No ISR or CPU1 writes the record. The shell takes a 176-byte snapshot with task dispatch disabled and reenables dispatch before formatting/UART. No UART output or additional DSB/cache maintenance is performed by the instrumentation during startup. Retrieval is through the CPU's UART path; raw debugger memory retrieval is not supported by this version's cache-publication contract.

The record resides in the internal zeroed RAM section at 0x2210be20. Source/disassembly checks must accompany address claims: the reference for this revision is `trace-r1/artifacts/G_trace_CPU0.map`. `boottrace` fingerprints 4096 readable flash bytes beginning at SystemInit (0x0203bb90), after the snapshot, to distinguish the pair without adding a second data difference.

Acceptance is not just successful compilation. G_trace/W_trace must preserve the previous normal/white contrast under the same PC supply conditions. If they do not, stop and assess observer effects/supply conditions; do not label a relink-induced success a fix.
