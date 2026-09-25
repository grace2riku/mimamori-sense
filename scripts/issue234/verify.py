"""Verify Issue234 CPU0 S-record against ELF and package the known CPU1 pair."""
from pathlib import Path
import argparse
import hashlib
import json
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


def verify(run):
    out = (ROOT / 'e2studio_CPU0/Debug/issue234' / run).resolve()
    assert out.is_relative_to((ROOT / 'e2studio_CPU0/Debug/issue234').resolve())
    manifest = json.loads((out / 'manifest.json').read_text())
    elf = out / 'mimamori_sense_CPU0.elf'
    mot = out / 'mimamori_sense_CPU0.mot'
    assert sha(elf) == manifest['elf_sha256'] and sha(mot) == manifest['mot_sha256']
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
    flash_end = next(int(line.split()[0], 16) for line in nm.splitlines()
                     if line.split()[-1] == '__ddsc_FLASH_END')
    manifest.update(srecord_matches_elf_flash=True, srecord_checksums_valid=True,
                    cpu0_load_bytes=len(data), cpu0_last_load_address=hex(max(data)),
                    cpu1_mot_sha256=sha(cpu1), cpu1_source=str(cpu1.relative_to(ROOT)),
                    cpu0_cpu1_overlap_bytes=0, hardware_tested=False)
    manifest['flash_image_end'] = hex(flash_end)
    manifest['flash_used_bytes'] = flash_end - 0x02000000
    (out / 'verification.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(f'PASS S-record/ELF, checksums, flash range, CPU0/CPU1 disjoint. CPU0 load: {len(data)} bytes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--run', required=True)
    verify(parser.parse_args().run)
