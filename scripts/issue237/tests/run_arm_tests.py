"""Compile production C and execute deterministic tests with mocked RTOS/device APIs.

Requires the project's ARM LLVM and Python package unicorn (2.1.4 tested).
No peripheral, real scheduler, inference accuracy or audible sound is emulated.
ARM_CLANG and ISSUE237_TEST_DEPS optionally override dependencies.
"""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, os.environ.get("ISSUE237_TEST_DEPS", str(Path(tempfile.gettempdir()) / "issue214-test-deps")))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC, UC_ARM_REG_C1_C0_2

CLANG = os.environ.get("ARM_CLANG", "C:/Renesas/RA/e2studio_v2025-12_fsp_v6.3.0/toolchains/llvm_arm/ATfE-21.1.1-Windows-x86_64/bin/clang.exe")


def run(source):
    source = Path(source)
    if not source.is_absolute():
        source = HERE / source
    with tempfile.TemporaryDirectory(prefix="issue237-test-") as tmp:
        elf = Path(tmp) / "test.elf"
        includes = [HERE / "mocks", ROOT / "e2studio_CPU0/src",
                    ROOT / "e2studio_CPU0/src/ntshell/src/lib/core",
                    ROOT / "e2studio_CPU0/ra/fsp/inc/api", ROOT / "e2studio_CPU0/ra/fsp/inc"]
        subprocess.run([CLANG, "--target=arm-none-eabi", "-mcpu=cortex-m4", "-mthumb",
                        "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard", "-fshort-enums", "-Os", "-g",
                        "-ffreestanding", "-fno-builtin", "-nostdlib",
                        "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-Werror",
                        *["-I" + str(p) for p in includes],
                        "-Wl,--gc-sections", "-Wl,-Ttext=0x10000", "-Wl,-e,run_tests",
                        str(source), str(HERE / "memory.c"), "-lc", "-lm", "-o", str(elf)], check=True)
        data = elf.read_bytes()
        if data[:7] != b"\x7fELF\x01\x01\x01":
            raise RuntimeError("Expected little-endian ELF32")
        entry, phoff = struct.unpack_from("<II", data, 24)
        phsize, phnum = struct.unpack_from("<HH", data, 42)
        cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        cpu.mem_map(0, 0x1000000)
        for i in range(phnum):
            kind, offset, addr, _, size, memsize, _, _ = struct.unpack_from("<8I", data, phoff + phsize * i)
            if kind == 1:
                if addr + memsize >= 0xE00000:
                    raise RuntimeError("Test image overlaps reserved stack")
                cpu.mem_write(addr, data[offset:offset + size])
        cpu.reg_write(UC_ARM_REG_C1_C0_2, 0xF << 20)  # Permit FP instructions.
        cpu.reg_write(UC_ARM_REG_SP, 0xF00000)
        cpu.reg_write(UC_ARM_REG_LR, 0x8001)
        cpu.emu_start(entry, 0x8000, timeout=10_000_000, count=10_000_000)
        if cpu.reg_read(UC_ARM_REG_PC) != 0x8000:
            raise RuntimeError("Test exceeded execution limit")
        failed_line = cpu.reg_read(UC_ARM_REG_R0)
        if failed_line:
            raise AssertionError(f"{source.name}:{failed_line}: check failed")
        print(f"PASS {source.name}")


if __name__ == "__main__":
    for arg in sys.argv[1:] or ["fall_test.c", "alarm_test.c", "integration_test.c", "alarm_task_test.c"]:
        run(arg)
