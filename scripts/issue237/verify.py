"""Verify Issue237 ISR wrapping, CPU0 S-record and the known CPU1 pair."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
# Reuse the repository's existing ELF/S-record parsers; no diagnostic patching.
sys.path.insert(0, str(ROOT / 'scripts/issue232-trace'))
from verify import Elf, mot_bytes
from build import LLVM


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_wrapping(image, elf, out, nm):
    symbols = {}
    for line in nm.splitlines():
        fields = line.split()
        if len(fields) == 3 and re.fullmatch(r'[0-9a-fA-F]+', fields[0]):
            symbols[fields[2]] = int(fields[0], 16)
    vector = symbols['g_vector_table']
    # Vector addresses are VMAs. Read section bytes directly instead of assuming
    # their load and execution addresses are identical.
    def word(address):
        matches = [s for s in image.sections
                   if s[1] != 8 and s[3] <= address and address + 4 <= s[3] + s[5]]
        assert len(matches) == 1, hex(address)
        section = matches[0]
        offset = section[4] + address - section[3]
        return int.from_bytes(image.data[offset:offset + 4], 'little')

    names = ('ssi_txi_isr', 'ssi_int_isr')
    object_nm = subprocess.check_output(
        [str(LLVM / 'llvm-nm.exe'), str(out / 'src/port/audio_port.o')], text=True)
    (out / 'audio_port-symbols.txt').write_text(object_nm, encoding='utf-8')
    disassembly = subprocess.check_output(
        [str(LLVM / 'llvm-objdump.exe'), '-d', '--no-show-raw-insn',
         '--disassemble-symbols=' + ','.join((*names, *('__wrap_' + n for n in names))),
         str(elf)], text=True)
    (out / 'ssi-disassembly.txt').write_text(disassembly, encoding='utf-8')
    bodies = {}
    current = None
    for line in disassembly.splitlines():
        label = re.match(r'^([0-9a-fA-F]+) <([^>]+)>:', line)
        if label:
            current = label[2]
            bodies[current] = []
        elif current:
            bodies[current].append(line)

    def branch_targets(body):
        # Include direct calls and tail branches; ignore local branch offsets
        # unless they actually return to an ISR entry address.
        return [int(m[1], 16) & ~1 for line in body
                if (m := re.search(r'\s(?:bl|blx|b)(?:\.w)?\s+0x([0-9a-fA-F]+)', line))]

    report = {}
    for index, name in zip((20, 21), names):
        wrapper = '__wrap_' + name
        assert wrapper in symbols, 'Wrapper missing: ' + wrapper
        assert word(vector + index * 4) == (symbols[wrapper] | 1), 'Vector bypasses ' + wrapper
        assert re.search(r'\bU\s+__real_' + name + r'\s*$', object_nm, re.MULTILINE), \
            'Compiled wrapper must refer to __real_' + name
        if name not in symbols or name not in bodies or wrapper not in bodies:
            raise RuntimeError('Cannot verify out-of-line original ISR ' + name +
                               '; inspect LTO/inlining before releasing this image')
        original_address = symbols[name] & ~1
        wrapper_address = symbols[wrapper] & ~1
        assert original_address != wrapper_address
        wrapper_targets = branch_targets(bodies[wrapper])
        assert wrapper_targets.count(original_address) == 1, \
            'Expected exactly one direct call to original ' + name + '; inspect LTO output'
        assert wrapper_address not in wrapper_targets, 'Recursive wrapper: ' + wrapper
        assert not set(branch_targets(bodies[name])) & {symbols['__wrap_' + n] & ~1 for n in names}, \
            'Original ISR branches back into a wrapper: ' + name
        report[name] = {'vector_index': index, 'vector_target': hex(word(vector + index * 4)),
                        'wrapper_address': hex(wrapper_address),
                        'original_address': hex(original_address),
                        'real_reference_resolved_to_original': True,
                        'direct_original_calls': 1, 'recursive_wrapper_branch': False}
    return report


def verify(run):
    assert re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]*', run), 'Use a single directory name'
    out = (ROOT / 'e2studio_CPU0/Debug/issue237' / run).resolve()
    assert out.is_relative_to((ROOT / 'e2studio_CPU0/Debug/issue237').resolve())
    manifest = json.loads((out / 'manifest.json').read_text())
    elf = out / 'mimamori_sense_CPU0.elf'
    mot = out / 'mimamori_sense_CPU0.mot'
    assert sha(elf) == manifest['elf_sha256'] and sha(mot) == manifest['mot_sha256']
    assert manifest['full_recompile'] and manifest['object_count'] == 1600
    assert sha(out / 'link.in') == manifest['link_response_sha256']
    for rel, value in manifest['source_sha256'].items():
        assert sha(ROOT / 'e2studio_CPU0' / rel) == value, 'Source changed: ' + rel
    data, entry = mot_bytes(mot)
    image = Elf(elf)
    flash = {a: b for a, b in image.loads().items() if 0x02000000 <= a < 0x020F8000}
    assert data == flash and entry == image.entry
    assert all(0x02000000 <= addr < 0x020F8000 for addr in data)
    baseline = ROOT / 'e2studio_CPU0/Debug/issue230/main-wait-r2/artifacts'
    cpu1 = baseline / 'MAIN_wait200_postc_CPU1.mot'
    assert sha(cpu1) == '8cb5d5e9040d0a9bb0ad72d233d21115b5e68cba3c0524e03f3e0fb4d31d2c53'
    other, entry1 = mot_bytes(cpu1)
    image1 = Elf(baseline / 'MAIN_wait200_postc_CPU1.elf')
    assert other == image1.loads() and entry1 == image1.entry
    assert not data.keys() & other.keys()
    shutil.copy2(cpu1, out / 'mimamori_sense_CPU1.mot')
    nm = subprocess.check_output([str(LLVM / 'llvm-nm.exe'), str(elf)], text=True)
    (out / 'symbols.txt').write_text(nm, encoding='utf-8')
    manifest['isr_wrapping'] = verify_wrapping(image, elf, out, nm)
    flash_end = next(int(line.split()[0], 16) for line in nm.splitlines()
                     if line.split()[-1] == '__ddsc_FLASH_END')
    manifest.update(srecord_matches_elf_flash=True, srecord_checksums_valid=True,
                    cpu0_load_bytes=len(data), cpu0_last_load_address=hex(max(data)),
                    cpu1_mot_sha256=sha(cpu1), cpu1_source=str(cpu1.relative_to(ROOT)),
                    cpu0_cpu1_overlap_bytes=0, hardware_tested=False)
    manifest['flash_image_end'] = hex(flash_end)
    manifest['flash_used_bytes'] = flash_end - 0x02000000
    (out / 'verification.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(f'PASS ISR vectors/wrapping, S-record/ELF, checksums, flash range, '
          f'CPU0/CPU1 disjoint. CPU0 load: {len(data)} bytes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--run', required=True)
    verify(parser.parse_args().run)
