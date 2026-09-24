"""Run the production C decoder as Arm code, including malformed-input checks.

Test-only dependencies: unicorn==2.1.4, pyelftools==0.32. The test uses Cortex-M4
instructions supported by Unicorn; the firmware build separately targets M85.
"""
import hashlib
from pathlib import Path
import random
import subprocess
import sys
import zlib

from convert_image import ROOT, pixels
from build import LLVM

sys.path.insert(0, str(ROOT / "e2studio_CPU0/Debug/startup-screen/test-deps"))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3


def main():
    out = ROOT / "e2studio_CPU0/Debug/startup-screen/arm-test"
    out.mkdir(parents=True, exist_ok=True)
    source = ROOT / "e2studio_CPU0/src/ui/ui_startup_image.c"
    (out / "harness.c").write_text(
        f'#include "{source.as_posix()}"\n'
        "bool test_decode(const uint8_t *src, size_t n, uint8_t *dst, size_t cap) {\n"
        "    return decode_zlib(src, n, dst, cap);\n}\n"
        "bool test_asset(uint8_t *dst, size_t cap) {\n"
        "    return ui_startup_image_decode(dst, cap);\n}\n", encoding="utf-8")
    puff = ROOT / "e2studio_CPU0/src/ui/puff/puff.c"
    subprocess.run([str(LLVM / "clang.exe"), "--target=arm-none-eabi", "-mcpu=cortex-m4",
                    "-mthumb", "-mfloat-abi=soft", "-O2", "-nostartfiles",
                    "-Wl,-Ttext=0x10000,-e,test_decode", str(out / "harness.c"), str(puff),
                    "-o", str(out / "decode.elf")], check=True)
    machine = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    machine.mem_map(0x10000, 0x100000)
    machine.mem_map(0x20000000, 0x800000)
    with (out / "decode.elf").open("rb") as stream:
        elf = ELFFile(stream)
        for segment in elf.iter_segments():
            if segment["p_type"] == "PT_LOAD":
                machine.mem_write(segment["p_vaddr"], segment.data())
        symbols = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}
    stop = 0x10fff0
    src = 0x20000000
    dst = 0x20200010

    def call(name, *args):
        machine.reg_write(UC_ARM_REG_SP, 0x207ff000)
        machine.reg_write(UC_ARM_REG_LR, stop | 1)
        for reg, value in zip([UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3], args):
            machine.reg_write(reg, value)
        machine.emu_start(symbols[name] | 1, stop, timeout=20000000, count=100000000)
        assert machine.reg_read(UC_ARM_REG_PC) == stop, "Execution budget exceeded"
        return bool(machine.reg_read(UC_ARM_REG_R0))
    cases = 0

    def check(data, capacity, valid, expected=None):
        nonlocal cases
        machine.mem_write(src, data)
        machine.mem_write(dst - 16, b"\xa5" * (capacity + 32))
        result = call("test_decode", src, len(data), dst, capacity)
        guarded = machine.mem_read(dst - 16, capacity + 32)
        assert bytes(guarded[:16]) == b"\xa5" * 16
        assert bytes(guarded[-16:]) == b"\xa5" * 16
        assert result == valid, (cases, len(data), capacity)
        if valid:
            assert bytes(guarded[16:-16]) == expected
        cases += 1

    raw = pixels()
    assert call("test_asset", dst, len(raw))
    assert bytes(machine.mem_read(dst, len(raw))) == raw
    assert not call("test_asset", dst, len(raw) - 1)
    assert not call("test_asset", 0, len(raw))
    encoded = zlib.compress(raw, 9)
    check(encoded, len(raw), True, raw)
    check(encoded, len(raw) - 1, False)
    check(encoded, len(raw) + 1, False)
    check(encoded[:-1], len(raw), False)
    check(encoded + b"trailing", len(raw), False)
    for i in [0, 1, len(encoded) // 2, len(encoded) - 1]:
        corrupt = bytearray(encoded)
        corrupt[i] ^= 0x80
        check(bytes(corrupt), len(raw), False)
    rng = random.Random(240924)
    for data in [b"a", b"abc" * 1000, bytes(range(256)) * 50, rng.randbytes(8192)]:
        for level in [0, 1, 9]:
            packed = zlib.compress(data, level)
            check(packed, len(data), True, data)
            for cut in sorted(set([0, 1, 2, 5, len(packed) // 2, len(packed) - 4])):
                check(packed[:cut], len(data), False)
        compressor = zlib.compressobj(strategy=zlib.Z_FIXED)
        packed = compressor.compress(data) + compressor.flush()
        check(packed, len(data), True, data)
    for _ in range(100):
        data = b"\x78\x9c" + rng.randbytes(rng.randrange(1, 128)) + b"\0\0\0\0"
        check(data, 4096, False)
    print(f"PASS: embedded image byte-exact, size/null guards, {cases} zlib cases with buffer canaries")
    print("RGB565 SHA256:", hashlib.sha256(raw).hexdigest())


if __name__ == "__main__":
    main()
