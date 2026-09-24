"""Create W_trace from the audited G_trace-r1 ELF and verify ELF/MOT bytes."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
from build import LAB, LLVM, sha

G_HASH = 'f107596a0d184d8b10cb32dfee153eb820f72373598bdbe0684056b3c28e103c'
CPU1_HASH = 'c39182f0bb014fd3ced7108a87dbbd79b56b0e14a9632c1eb10fb3d8191fb610'
SYSINIT = 0x0203BB90
PATCHES = [(0x0203C39C, bytes.fromhex('2c70'), bytes.fromhex('18e0'), 0x0203C3D0),
           (0x0203C3D4, bytes.fromhex('00bf'), bytes.fromhex('0fe0'), 0x0203C3F6)]


class Elf:
    def __init__(self, data):
        self.data = data
        assert data[:7] == b'\x7fELF\x01\x01\x01'
        self.entry, phoff, shoff = struct.unpack_from('<III', data, 24)
        phsize, phnum, shsize, shnum = struct.unpack_from('<HHHH', data, 42)
        assert phsize == 32 and shsize == 40
        self.segments = [struct.unpack_from('<8I', data, phoff + i * phsize) for i in range(phnum)]
        self.sections = [struct.unpack_from('<10I', data, shoff + i * shsize) for i in range(shnum)]

    def offset(self, address, length=1):
        found = [s[4] + address - s[3] for s in self.sections
                 if s[1] == 1 and s[2] & 6 == 6 and s[3] <= address
                 and address + length <= s[3] + s[5]]
        assert len(found) == 1, 'Not a unique executable PROGBITS range'
        return found[0]

    def load_bytes(self):
        # S-record output excludes NOBITS even if lld gave a LOAD segment
        # a nonzero file size. Include all allocated file-backed sections,
        # including INIT_ARRAY; exclude NOBITS and NULL.
        result = {}
        for s in self.sections:
            if s[1] in (0, 8) or not s[2] & 2 or not s[5]:
                continue
            matches = [p for p in self.segments if p[0] == 1
                       and p[2] <= s[3] and s[3] + s[5] <= p[2] + p[5]
                       and p[1] <= s[4] and s[4] + s[5] <= p[1] + p[4]]
            assert len(matches) == 1
            p = matches[0]
            address = p[3] + s[3] - p[2]
            for i, value in enumerate(self.data[s[4]:s[4] + s[5]]):
                assert address + i not in result
                result[address + i] = value
        return result


def mot_bytes(path):
    result = {}
    entry = None
    for line in path.read_text(encoding='ascii').splitlines():
        assert line.startswith('S') and len(line) >= 4
        kind = int(line[1])
        raw = bytes.fromhex(line[2:])
        assert raw[0] + 1 == len(raw), 'Invalid S-record length'
        assert sum(raw) & 255 == 255, 'Invalid S-record checksum'
        address_len = {0: 2, 1: 2, 2: 3, 3: 4, 5: 2, 6: 3, 7: 4, 8: 3, 9: 2}[kind]
        address = int.from_bytes(raw[1:1 + address_len], 'big')
        if kind in (1, 2, 3):
            for i, value in enumerate(raw[1 + address_len:-1]):
                assert address + i not in result, 'Overlapping S-records'
                result[address + i] = value
        if kind in (7, 8, 9):
            assert entry is None, 'Duplicate termination'
            entry = address
    assert result and entry is not None
    return result, entry


def fnv(data):
    value = 2166136261
    for b in data:
        value = ((value ^ b) * 16777619) & 0xFFFFFFFF
    return f'{value:08X}'


def create(out):
    g = out / 'G_trace_CPU0.elf'
    w = out / 'W_trace_CPU0.elf'
    assert sha(g) == G_HASH, 'Re-audit addresses/control flow for any new build'
    original = g.read_bytes()
    elf = Elf(original)
    patched = bytearray(original)
    allowed_offsets = set()
    allowed_addresses = set()
    for address, before, after, dest in PATCHES:
        offset = elf.offset(address, len(before))
        assert original[offset:offset + len(before)] == before
        insn = int.from_bytes(after, 'little')
        assert insn & 0xF800 == 0xE000
        immediate = insn & 0x7FF
        if immediate & 0x400:
            immediate -= 0x800
        assert address + 4 + 2 * immediate == dest
        patched[offset:offset + len(before)] = after
        for i, (a, b) in enumerate(zip(before, after)):
            if a != b:
                allowed_offsets.add(offset + i)
                allowed_addresses.add(address + i)
    actual = {i for i, (a, b) in enumerate(zip(original, patched)) if a != b}
    assert actual == allowed_offsets and len(original) == len(patched)
    if w.exists():
        assert w.read_bytes() == patched, 'Existing W differs; preserve it'
    else:
        w.write_bytes(patched)
    wel = Elf(patched)
    assert elf.sections == wel.sections and elf.segments == wel.segments
    subprocess.run([str(LLVM / 'llvm-objcopy.exe'), '-O', 'srec', str(w),
                    str(out / 'W_trace_CPU0.mot')], check=True)
    fingerprints = {}
    maps = []
    for name in ('G', 'W'):
        e = Elf((out / f'{name}_trace_CPU0.elf').read_bytes())
        addressed, entry = mot_bytes(out / f'{name}_trace_CPU0.mot')
        assert addressed == e.load_bytes(), f'{name} MOT differs from ELF load data'
        assert entry == e.entry
        assert all(0x02000000 <= a < 0x03000000 for a in addressed), 'Unexpected non-flash data'
        maps.append(addressed)
        fingerprints[name] = fnv(bytes(addressed[SYSINIT + i] for i in range(4096)))
        with (out / f'{name}_rtc_disassembly.txt').open('w', encoding='utf-8') as log:
            subprocess.run([str(LLVM / 'llvm-objdump.exe'), '-d', '--start-address=0x0203c380',
                            '--stop-address=0x0203c46a', str(out / f'{name}_trace_CPU0.elf')],
                           stdout=log, check=True)
    assert maps[0].keys() == maps[1].keys()
    assert {a for a in maps[0] if maps[0][a] != maps[1][a]} == allowed_addresses
    assert fingerprints['G'] != fingerprints['W']
    cpu1 = LAB / 'artifacts/B_CPU1.mot'
    assert sha(cpu1) == CPU1_HASH
    # Both new CPU0 images share the already measured CPU1 image.
    shutil.copy2(cpu1, out / cpu1.name)
    cpu1_bytes, _ = mot_bytes(out / cpu1.name)
    overlap = maps[0].keys() & cpu1_bytes.keys()
    assert all(maps[0][a] == cpu1_bytes[a] for a in overlap), 'CPU0/CPU1 conflicting addresses'
    files = {p.name: sha(p) for p in out.iterdir() if p.suffix in ('.elf', '.map', '.mot')}
    report = {
        'hardware_validated': False,
        'changed_elf_bytes': len(actual), 'changed_mot_data_bytes': len(allowed_addresses),
        'all_other_elf_bytes_identical': True, 'identical_section_and_program_headers': True,
        'mot_matches_elf_allocated_load_data': True, 'srecord_checksums_valid': True,
        'cpu0_cpu1_overlap_bytes': len(overlap), 'cpu0_cpu1_conflicting_bytes': 0,
        'cpu0_flash_data_bytes': len(maps[0]), 'fingerprint_fnv4096': fingerprints,
        'systeminit_address': hex(SYSINIT), 'trace_ram_address': '0x2210be20', 'trace_ram_bytes': 176,
        'patches': [{'address': hex(a), 'before': b.hex(), 'after': c.hex(), 'destination': hex(d)}
                    for a, b, c, d in PATCHES], 'sha256': files,
    }
    (out / 'pair-verification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('sha256', 'patches')}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--artifacts', type=Path, default=LAB / 'trace-r1/artifacts')
    create(parser.parse_args().artifacts.resolve())
