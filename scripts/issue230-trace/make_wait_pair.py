"""Issue #230 approved wait-only experiment. No hardware access or firmware relink.

Assemble a BL and two identical-width MOVW/B.W stubs at their final addresses,
then patch only the audited W_trace-r1 instruction windows. Preserve all prior
images. See doc/design/issue-230.md section 26 for the pre-OS calling contract.
"""
import argparse
import json
import shutil
import struct
import subprocess
from pathlib import Path
from build import LAB, LLVM, sha
from make_pair import CPU1_HASH, Elf, SYSINIT, fnv, mot_bytes

W_HASH = 'e9674e83e4f339d597fbda4383b66d0992a135a81a46bf17cd6ce7c8294266be'
CALL, STUB, DELAY = 0x0203C398, 0x0203C39E, 0x0203BA62
BEFORE = {CALL: bytes.fromhex('fff763fb'), STUB: bytes.fromhex('00bf287808b12ff0')}


def branch_destination(code, address, link):
    a, b = struct.unpack('<HH', code)
    assert a & 0xF800 == 0xF000
    assert b & 0xD000 == (0xD000 if link else 0x9000)
    s, j1, j2 = (a >> 10) & 1, (b >> 13) & 1, (b >> 11) & 1
    i1, i2 = 1 ^ j1 ^ s, 1 ^ j2 ^ s
    delta = (s << 24) | (i1 << 23) | (i2 << 22) | ((a & 1023) << 12) | ((b & 2047) << 1)
    if s:
        delta -= 1 << 25
    return address + 4 + delta


def movw_value(code):
    a, b = struct.unpack('<HH', code)
    assert a & 0xFBF0 == 0xF240 and b & 0x8000 == 0
    register = (b >> 8) & 15
    value = ((a & 15) << 12) | (((a >> 10) & 1) << 11) | (((b >> 12) & 7) << 8) | (b & 255)
    return register, value


def assemble(work, name, units):
    source = work / f'{name}.s'
    source.write_text(f'''.syntax unified
.cpu cortex-m85
.thumb
.section .wait_call,"ax",%progbits
.global wait_call
.thumb_func
wait_call:
    bl wait_stub
.section .wait_stub,"ax",%progbits
.global wait_stub
.thumb_func
wait_stub:
    movw r1, #{units}
    b.w delay_target
.type delay_target,%function
''', encoding='ascii')
    obj, elf = work / f'{name}.o', work / f'{name}.elf'
    subprocess.run([str(LLVM / 'clang.exe'), '--target=arm-none-eabi', '-mcpu=cortex-m85',
                    '-mthumb', '-mfloat-abi=hard', '-c', str(source), '-o', str(obj)], check=True)
    subprocess.run([str(LLVM / 'ld.lld.exe'), '-T', str(work / 'layout.ld'),
                    '--entry=wait_call', str(obj), '-o', str(elf)], check=True)
    assembled = Elf(elf.read_bytes())
    call_offset, stub_offset = assembled.offset(CALL, 4), assembled.offset(STUB, 8)
    call = assembled.data[call_offset:call_offset + 4]
    stub = assembled.data[stub_offset:stub_offset + 8]
    assert branch_destination(call, CALL, True) == STUB
    assert movw_value(stub[:4]) == (1, units)
    assert branch_destination(stub[4:], STUB + 4, False) == DELAY
    with (work / f'{name}-assembled.txt').open('w', encoding='utf-8') as log:
        subprocess.run([str(LLVM / 'llvm-objdump.exe'), '-d', str(elf)], stdout=log, check=True)
    return {CALL: call, STUB: stub}


def create(out):
    out = out.resolve()
    assert out.is_relative_to(LAB.resolve()) and not out.exists(), 'Use a new issue230 lab directory'
    base = LAB / 'trace-r1/artifacts/W_trace_CPU0.elf'
    cpu1 = LAB / 'trace-r1/artifacts/B_CPU1.mot'
    assert sha(base) == W_HASH and sha(cpu1) == CPU1_HASH
    original = base.read_bytes()
    source_elf = Elf(original)
    for address, before in BEFORE.items():
        offset = source_elf.offset(address, len(before))
        assert original[offset:offset + len(before)] == before
    # Preserve RCR4=0, r0=200, the W skip branches, and r9 initialization.
    assert original[source_elf.offset(0x0203C392, 6):source_elf.offset(0x0203C392, 6) + 6] == bytes.fromhex('c82001212c71')
    assert original[source_elf.offset(0x0203C39C, 2):source_elf.offset(0x0203C39C, 2) + 2] == bytes.fromhex('18e0')
    assert original[source_elf.offset(0x0203C3D0, 8):source_elf.offset(0x0203C3D0, 8) + 8] == bytes.fromhex('05f11c090fe000bf')
    out.mkdir(parents=True)
    work = out / 'assembly'
    work.mkdir()
    (work / 'layout.ld').write_text('''SECTIONS {
  .wait_call 0x0203C398 : { *(.wait_call) }
  .wait_stub 0x0203C39E : { *(.wait_stub) }
  delay_target = 0x0203BA63;
}
''', encoding='ascii')
    art = out / 'artifacts'
    art.mkdir()
    original_map = source_elf.load_bytes()
    cpu1_map, _ = mot_bytes(cpu1)
    images, addressed_maps, patches, records = [], [], [], {}
    for name, units in [('W_wait_short_CPU0', 1), ('W_wait_200ms_CPU0', 1000)]:
        patch = assemble(work, name, units)
        patched = bytearray(original)
        allowed_offsets, allowed_addresses = set(), set()
        for address, after in patch.items():
            before = BEFORE[address]
            assert len(after) == len(before)
            offset = source_elf.offset(address, len(after))
            patched[offset:offset + len(after)] = after
            for i, (a, b) in enumerate(zip(before, after)):
                if a != b:
                    allowed_offsets.add(offset + i)
                    allowed_addresses.add(address + i)
        actual_offsets = {i for i, (a, b) in enumerate(zip(original, patched)) if a != b}
        assert len(patched) == len(original) and actual_offsets == allowed_offsets
        e = Elf(patched)
        assert e.sections == source_elf.sections and e.segments == source_elf.segments
        elf_path, mot_path = art / f'{name}.elf', art / f'{name}.mot'
        elf_path.write_bytes(patched)
        subprocess.run([str(LLVM / 'llvm-objcopy.exe'), '-O', 'srec', str(elf_path), str(mot_path)], check=True)
        addressed, entry = mot_bytes(mot_path)
        assert addressed == e.load_bytes() and entry == e.entry == source_elf.entry
        assert addressed.keys() == original_map.keys()
        assert {a for a in addressed if addressed[a] != original_map[a]} == allowed_addresses
        assert all(0x02000000 <= a < 0x03000000 for a in addressed)
        overlap = addressed.keys() & cpu1_map.keys()
        assert all(addressed[a] == cpu1_map[a] for a in overlap)
        with (art / f'{name}-disassembly.txt').open('w', encoding='utf-8') as log:
            subprocess.run([str(LLVM / 'llvm-objdump.exe'), '-d', '--start-address=0x0203c380',
                            '--stop-address=0x0203c43c', str(elf_path)], stdout=log, check=True)
        fingerprint = fnv(bytes(addressed[SYSINIT + i] for i in range(4096)))
        records[name] = dict(units=units, requested_us=200 * units, elf_sha256=sha(elf_path),
                             mot_sha256=sha(mot_path), fingerprint_fnv4096=fingerprint,
                             modified_bytes_from_W=len(actual_offsets), cpu1_overlap_bytes=len(overlap),
                             patches={hex(a): dict(before=BEFORE[a].hex(), after=b.hex()) for a, b in patch.items()})
        images.append(patched)
        addressed_maps.append(addressed)
        patches.append(patch)
    # Two images differ ONLY within the MOVW immediate, not instruction size,
    # register destination, flags semantics, branching, data or any ELF metadata.
    assert patches[0][CALL] == patches[1][CALL]
    assert patches[0][STUB][4:] == patches[1][STUB][4:]
    assert movw_value(patches[0][STUB][:4]) == (1, 1)
    assert movw_value(patches[1][STUB][:4]) == (1, 1000)
    immediate_offsets = {source_elf.offset(STUB) + i for i, (a, b) in
                         enumerate(zip(patches[0][STUB][:4], patches[1][STUB][:4])) if a != b}
    pair_diff = {i for i, (a, b) in enumerate(zip(*images)) if a != b}
    assert pair_diff == immediate_offsets and len(pair_diff) == 2
    pair_addresses = {STUB + i for i, (a, b) in enumerate(zip(patches[0][STUB][:4], patches[1][STUB][:4])) if a != b}
    assert {a for a in addressed_maps[0] if addressed_maps[0][a] != addressed_maps[1][a]} == pair_addresses
    shutil.copy2(cpu1, art / cpu1.name)
    shutil.copy2(LAB / 'trace-r1/artifacts/G_trace_CPU0.map', art / 'shared-layout.map')
    # Keep hardware execution unclaimed: these are static verifications only.
    report = dict(hardware_validated=False, base_W_sha256=W_HASH, cpu1_sha256=CPU1_HASH,
                  firmware_relinked=False, pair_changed_elf_bytes=2, pair_changed_mot_bytes=2,
                  identical_section_and_program_headers=True, other_elf_bytes_identical=True,
                  mot_matches_elf=True, srecord_checksums_valid=True, cpu0_data_bytes=len(original_map),
                  call=hex(CALL), stub=hex(STUB), delay=hex(DELAY), return_lr=hex((CALL + 4) | 1),
                  pair_changed_addresses=[hex(a) for a in sorted(pair_addresses)], images=records)
    (art / 'wait-pair-verification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    assert sha(base) == W_HASH and sha(cpu1) == CPU1_HASH
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=LAB / 'wait-r1')
    create(parser.parse_args().output)
