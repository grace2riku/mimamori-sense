"""Compile real production C with mocks and execute on a deterministic ARM CPU.

Requires Python package unicorn (tested 2.1.4) and the project's ARM LLVM.
No board, RTC peripheral, RTOS scheduling or LVGL rendering is emulated.
Usage: python run_arm_tests.py diag_test.c
Optional: ARM_CLANG and ISSUE214_TEST_DEPS environment variables.
"""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, os.environ.get("ISSUE214_TEST_DEPS", str(Path(tempfile.gettempdir()) / "issue214-test-deps")))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0

CLANG = os.environ.get("ARM_CLANG", "C:/Renesas/RA/e2studio_v2025-12_fsp_v6.3.0/toolchains/llvm_arm/ATfE-21.1.1-Windows-x86_64/bin/clang.exe")


def run(source):
    source = Path(source)
    if not source.is_absolute():
        source = HERE / source
    with tempfile.TemporaryDirectory(prefix="issue214-test-") as tmp:
        elf = Path(tmp) / "test.elf"
        subprocess.run([CLANG, "--target=arm-none-eabi", "-mcpu=cortex-m4", "-mthumb",
                        "-O1", "-g", "-ffreestanding", "-fno-builtin", "-nostdlib",
                        "-Wall", "-Wextra", "-Werror", "-I" + str(HERE / "mocks"),
                        "-I" + str(HERE / "ui_stubs"),
                        "-I" + str(ROOT / "e2studio_CPU0/src"),
                        "-Wl,-Ttext=0x10000", "-Wl,-e,run_tests",
                        str(source), str(HERE / "memory.c"), "-o", str(elf)], check=True)
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
                    raise RuntimeError("Test image overlaps reserved stack")
                cpu.mem_write(addr, data[offset:offset + size])
        cpu.reg_write(UC_ARM_REG_SP, 0xF00000)
        cpu.reg_write(UC_ARM_REG_LR, 0x8001)
        cpu.emu_start(entry, 0x8000, timeout=10_000_000, count=10_000_000)
        from unicorn.arm_const import UC_ARM_REG_PC
        if cpu.reg_read(UC_ARM_REG_PC) != 0x8000:
            raise RuntimeError("Test exceeded execution limit")
        failed_line = cpu.reg_read(UC_ARM_REG_R0)
        if failed_line:
            raise AssertionError(f"{source.name}:{failed_line}: check failed")
        print(f"PASS {source.name}")


if __name__ == "__main__":
    for arg in sys.argv[1:] or ["diag_test.c"]:
        run(arg)
