"""Execute real LVGL + production startup screen on Arm/Unicorn.

Requires the same test-only packages as test_decode.py. All compiled outputs are
kept under ignored Debug/startup-screen. Timing tests inject refresh events;
a separate final pass renders a software preview. No hardware access.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import sys

from build import ROOT, LLVM

sys.path.insert(0, str(ROOT / "e2studio_CPU0/Debug/startup-screen/test-deps"))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0


def main():
    here = Path(__file__).resolve().parent
    base = ROOT / "e2studio_CPU0"
    lvgl = base / "ra/lvgl/lvgl"
    out = base / "Debug/startup-screen/timing-test"
    out.mkdir(parents=True, exist_ok=True)
    flags = ["--target=arm-none-eabi", "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=soft",
             "-O1", "-ffunction-sections", "-fdata-sections", "-DLV_CONF_INCLUDE_SIMPLE",
             "-I" + str(here / "test-config"), "-I" + str(lvgl), "-I" + str(base / "src")]
    sources = sorted((lvgl / "src").rglob("*.c")) + [
        base / "src/ui/ui_startup_screen.c", base / "src/ui/ui_startup_image.c",
        base / "src/ui/puff/puff.c", here / "test_timing.c"]
    objects = [out / f"{i}.o" for i in range(len(sources))]

    def compile_one(pair):
        src, obj = pair
        result = subprocess.run([str(LLVM / "clang.exe"), *flags, "-c", str(src), "-o", str(obj)], capture_output=True)
        (obj.with_suffix(".log")).write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f"{src}: {result.stderr.decode(errors='replace')}")

    print(f"Compiling real LVGL test ({len(sources)} files)", flush=True)
    with ThreadPoolExecutor(max_workers=6) as pool:
        list(pool.map(compile_one, zip(sources, objects)))
    entries = ["init", "start", "duplicate", "null", "frame", "tick", "now", "on_main", "perf_visible", "animation_count", "angle", "cleanup", "heap_delta", "preview"]
    command = [*flags, "-nostartfiles", "-Wl,-Ttext=0x10000,-e,test_init,--gc-sections"]
    command += ["-Wl,--undefined=test_" + name for name in entries]
    command += [str(obj) for obj in objects] + ["-o", str(out / "timing.elf")]
    rsp = out / "link.in"
    rsp.write_text("\n".join('"' + arg.replace("\\", "/") + '"' for arg in command), encoding="utf-8")
    subprocess.run([str(LLVM / "clang.exe"), "@" + str(rsp)], check=True)
    machine = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    machine.mem_map(0x10000, 0x2000000)
    machine.mem_map(0x20000000, 0x800000)
    with (out / "timing.elf").open("rb") as stream:
        elf = ELFFile(stream)
        for segment in elf.iter_segments():
            if segment["p_type"] == "PT_LOAD":
                machine.mem_write(segment["p_vaddr"], segment.data())
        symbols = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}
    stop = 0x200fff0

    def call(name, value=0):
        machine.reg_write(UC_ARM_REG_SP, 0x207ff000)
        machine.reg_write(UC_ARM_REG_LR, stop | 1)
        machine.reg_write(UC_ARM_REG_R0, value)
        machine.emu_start(symbols["test_" + name] | 1, stop, timeout=50000000, count=500000000)
        assert machine.reg_read(UC_ARM_REG_PC) == stop, name + " did not return"
        return machine.reg_read(UC_ARM_REG_R0)

    call("init")
    assert call("null") == 0
    assert call("perf_visible") == 1
    for run in range(4):
        assert call("start") == 1
        assert call("on_main") == 0
        assert call("perf_visible") == 0
        assert call("duplicate") == 0
        assert call("animation_count") == 2
        angle = call("angle")
        call("tick", 250)
        advance = (call("angle") - angle) % 360
        assert 43 <= advance <= 47, f"Expected 45 degrees in 250ms (2s/revolution), got {advance}"
        call("tick", 6000)
        assert call("on_main") == 0, "Deadline started before first frame"
        if run == 1:
            # Exercise deadline across the 32-bit LVGL tick wrap.
            call("tick", (0xfffffff0 - call("now")) & 0xffffffff)
        call("frame")
        call("tick", 2000)
        call("frame")  # Additional refresh must not restart the deadline.
        call("tick", 2999)
        assert call("on_main") == 0, "Transition before 5000ms"
        assert call("perf_visible") == 0
        call("tick", 1)
        assert call("on_main") == 1, "No transition at 5000ms"
        assert call("perf_visible") == 1
        cleanup = call("cleanup")
        # The first lifecycle may grow LVGL's shared allocation capacity.
        # Subsequent lifecycles must return all per-screen allocations.
        assert (cleanup & 3) == 3
        print(f"Lifecycle {run}: cleanup={cleanup}, heap_delta={call('heap_delta'):08x}", flush=True)
        if run > 1:
            assert cleanup == 7, f"Cleanup flags={cleanup}, heap delta={call('heap_delta'):08x}"
        call("tick", 5000)
        assert call("on_main") == 1
    print("PASS: half-speed spinner, FPS hidden/restored, first-frame start, 4999/5000ms boundary, repeat-frame immunity,")
    print("      duplicate/null rejection, one-shot transition, cleanup, repeated lifecycle, tick wrap")
    assert call("start") == 1
    address = call("preview")
    raw = machine.mem_read(address, 1024 * 600 * 2)
    from PIL import Image
    rgb = bytearray()
    for i in range(0, len(raw), 2):
        value = raw[i] | (raw[i + 1] << 8)
        rgb.extend((((value >> 11) & 31) * 255 // 31,
                    ((value >> 5) & 63) * 255 // 63, (value & 31) * 255 // 31))
    Image.frombytes("RGB", (1024, 600), bytes(rgb)).save(out / "startup-preview.png")
    print("Saved real LVGL software-rendered startup-preview.png")


if __name__ == "__main__":
    main()
