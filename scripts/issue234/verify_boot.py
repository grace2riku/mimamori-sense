"""Verify the linked diagnostic storage against actual runtime init tables."""
import argparse
import json
import struct
import subprocess
import sys
import hashlib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/issue232-trace'))
from verify import Elf, LLVM

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main(run):
    out = (ROOT / 'e2studio_CPU0/Debug/issue234' / run).resolve()
    assert out.is_relative_to((ROOT / 'e2studio_CPU0/Debug/issue234').resolve())
    elf = out / 'mimamori_sense_CPU0.elf'
    nm = subprocess.check_output([str(LLVM / 'llvm-nm.exe'), '-S', str(elf)], text=True)
    symbols = {}
    for line in nm.splitlines():
        parts = line.split()
        if len(parts) == 4:
            symbols[parts[3]] = (int(parts[0], 16), int(parts[1], 16))
    base, size = symbols['s_rtc_boot']
    assert size == 5 * 24 and 0x22000000 <= base < base + size <= 0x22200000
    assert symbols['__ram_noinit$$Base'][0] <= base
    assert base + size <= symbols['__ram_noinit$$Limit'][0]
    data = Elf(elf).loads()
    counts = {}
    for table, stride in [('zero_list', 12), ('copy_list', 16)]:
        addr, length = symbols[table]
        assert length % stride == 0
        for offset in range(0, length, stride):
            raw = bytes(data[addr + offset + i] for i in range(stride))
            start, end = struct.unpack_from('<II', raw)
            assert start <= end
            assert end <= base or start >= base + size or start == end, table
        counts[table] = length // stride
    manifest = json.loads((out / 'manifest.json').read_text())
    for rel, digest in manifest['source_sha256'].items():
        assert sha(ROOT / 'e2studio_CPU0' / rel) == digest, rel
    audit = dict(storage=hex(base), bytes=size, runtime_tables_disjoint=counts,
                 source_hashes_match=True, hardware_tested=False)
    (out / 'boot-storage-verification.json').write_text(json.dumps(audit, indent=2))
    print('PASS diagnostic storage: internal SRAM, no runtime zero/copy overlap, source hashes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--run', required=True)
    main(parser.parse_args().run)
