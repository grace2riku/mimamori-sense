"""Validate a pasted boottrace log. Never infer a DWT wrap bound from itself."""
import argparse
import json
import re
from pathlib import Path

POINTS = ['POST_C', 'SDRAM_BEFORE', 'SDRAM_AFTER', 'OS_BEFORE',
          'DISPLAY_BEFORE', 'RESET_BEFORE', 'RESET_AFTER',
          'GLCDC_OPEN', 'GLCDC_START', 'DISPLAY_AFTER']
FINGERPRINTS = {'AC63EB02': 'G_trace', '53FDAD50': 'W_trace',
                'DFDA3AF5': 'W_wait_short', 'AD78E8B0': 'W_wait_200ms'}


def parse(text, upper_bound_ms=None):
    header = re.search(r'BOOTTRACE v=(\d+) fnv4096=([0-9A-Fa-f]{8}) hz=(\d+) ctrl=([0-9A-Fa-f]{8}) probe=(\d+)', text)
    if header is None or 'BOOTTRACE END' not in text:
        raise ValueError('Incomplete BOOTTRACE block')
    if len(re.findall(r'BOOTTRACE v=', text)) != 1:
        raise ValueError('Supply exactly one boot record per input file')
    version, fingerprint, hz, ctrl, probe = header.groups()
    hz, ctrl, probe = int(hz), int(ctrl, 16), int(probe)
    if version != '1' or fingerprint.upper() not in FINGERPRINTS:
        raise ValueError('Unknown firmware/record version')
    slots = {}
    pattern = r'BT (\w+) clock=(\w+) valid=(\d+) hi=(\d+) lo=(\d+) result=(-?\d+)'
    for name, clock, valid, hi, lo, result in re.findall(pattern, text):
        if name not in POINTS or name in slots:
            raise ValueError('Unknown or duplicate slot')
        expected = 'DWT_MOD32' if POINTS.index(name) < 4 else 'OS_MS10'
        if clock != expected or int(valid) not in (0, 1):
            raise ValueError('Invalid clock/slot flag')
        if not 0 <= int(hi) < 2**32 or not 0 <= int(lo) < 2**32:
            raise ValueError('Timestamp outside uint32 range')
        slots[name] = dict(clock=clock, valid=bool(int(valid)), hi=int(hi), lo=int(lo), result=int(result))
    if set(slots) != set(POINTS):
        raise ValueError('Missing slot lines (including valid=0 lines)')
    result = {'image': FINGERPRINTS[fingerprint.upper()], 'hz': hz, 'slots': slots}
    result['record_complete'] = all(s['valid'] for s in slots.values())
    result['dwt_probe_ok'] = bool(hz > 0 and ctrl & 1 and not ctrl & (1 << 25) and probe > 0)
    result['dwt_time_status'] = 'unresolved_wrap_bound'
    if upper_bound_ms is not None:
        if hz <= 0 or not 0 < upper_bound_ms < 2**32 * 1000 / hz:
            raise ValueError('Independent pre-OS bound must be positive and shorter than one wrap')
        if result['dwt_probe_ok'] and all(slots[p]['valid'] for p in POINTS[:4]):
            cycles = [slots[p]['lo'] for p in POINTS[:4]]
            if any(slots[p]['hi'] for p in POINTS[:4]) or cycles != sorted(cycles):
                raise ValueError('DWT timestamps contradict the supplied no-wrap bound')
            if cycles[-1] * 1000 / hz > upper_bound_ms:
                raise ValueError('DWT elapsed time exceeds the supplied bound')
            result['dwt_time_status'] = 'valid_with_external_bound'
            result['preos_ms_from_post_clock'] = {p: slots[p]['lo'] * 1000 / hz for p in POINTS[:4]}
    # Show OS timestamps in their own domain; never add them to DWT time.
    result['os_ms'] = {p: (slots[p]['hi'] << 32) | slots[p]['lo']
                       for p in POINTS[4:] if slots[p]['valid']}
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('log', type=Path)
    parser.add_argument('--preos-upper-bound-ms', type=float,
                        help='Independently observed power-on to post-OS upper bound, NOT DWT-derived')
    args = parser.parse_args()
    print(json.dumps(parse(args.log.read_text(encoding='utf-8-sig'), args.preos_upper_bound_ms), indent=2))
