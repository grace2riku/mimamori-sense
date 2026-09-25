"""Audit linked OFS delta and record the separate device-side update.

Does not generate a programming image or connect to a target.
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/issue232-trace'))
from verify import Elf

def word(data, address):
    return int.from_bytes(bytes(data[address + i] for i in range(4)), 'little')

def verify(run):
    base = ROOT / 'e2studio_CPU0/Debug/issue234'
    out = (base / run).resolve()
    assert out.is_relative_to(base.resolve())
    current = Elf(out / 'mimamori_sense_CPU0.elf').loads()
    previous = Elf(base / 'backup-r1/mimamori_sense_CPU0.elf').loads()
    ofs, sel = 0x02C9F0C0, 0x02C9F120
    old, new = word(previous, ofs), word(current, ofs)
    assert (old & 0xF) == 0xF and new == (old & ~0xF)
    assert word(previous, sel) == word(current, sel) == 0
    result = {
        'elf_ofs1_sec_before': hex(old), 'elf_ofs1_sec_after': hex(new),
        'elf_ofs1_sel': '0x0', 'changed_mask': '0xf',
        'device_observed_ofs1_sec': '0xfdffffff',
        'device_expected_ofs1_sec': '0xfdfffff0',
        'device_programmed': False,
        'note': 'Program flash MOT excludes OFS. Preserve device bits 31:4.'
    }
    (out / 'options-verification.json').write_text(json.dumps(result, indent=2))
    print(f'PASS ELF OFS1_SEC {old:08X} -> {new:08X}: only bits 3:0; SEL unchanged')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--run', required=True)
    verify(parser.parse_args().run)
