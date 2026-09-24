"""Host tests for rejecting misleading logs and corrupt programming files."""
import tempfile
import unittest
from pathlib import Path
from read_log import parse, POINTS
from make_pair import Elf, LAB, mot_bytes


def sample():
    lines = ['BOOTTRACE v=1 fnv4096=AC63EB02 hz=1000000000 ctrl=00000001 probe=8']
    for i, name in enumerate(POINTS):
        clock = 'DWT_MOD32' if i < 4 else 'OS_MS10'
        value = (i + 1) * 1000000 if i < 4 else i * 10
        lines.append(f'BT {name} clock={clock} valid=1 hi=0 lo={value} result=0')
    return '\n'.join(lines) + '\nBOOTTRACE END'


class ValidationTests(unittest.TestCase):
    def test_no_inferred_wrap_bound(self):
        data = parse(sample())
        self.assertEqual(data['dwt_time_status'], 'unresolved_wrap_bound')
        self.assertNotIn('preos_ms_from_post_clock', data)

    def test_external_bound(self):
        data = parse(sample(), 1000)
        self.assertEqual(data['preos_ms_from_post_clock']['OS_BEFORE'], 4)

    def test_unsafe_bound(self):
        for bound in (0, 3, 4295, float('nan')):
            with self.assertRaises(ValueError):
                parse(sample(), bound)

    def test_missing_duplicate_or_foreign_record(self):
        for value in (sample().replace('BOOTTRACE END', ''), sample() + sample(),
                      sample().replace('AC63EB02', '12345678'),
                      sample().replace('BT POST_C ', 'BT UNKNOWN ')):
            with self.assertRaises(ValueError):
                parse(value)

    def test_failed_probe_and_partial_slots(self):
        data = parse(sample().replace('probe=8', 'probe=0').replace('valid=1', 'valid=0'), 1000)
        self.assertFalse(data['record_complete'])
        self.assertFalse(data['dwt_probe_ok'])
        self.assertNotIn('preos_ms_from_post_clock', data)

    def test_real_mot_includes_constructor_table(self):
        out = LAB / 'trace-r1/artifacts'
        addressed, _ = mot_bytes(out / 'G_trace_CPU0.mot')
        elf = Elf((out / 'G_trace_CPU0.elf').read_bytes())
        self.assertEqual(addressed, elf.load_bytes())
        self.assertIn(0x020C3EB8, addressed)

    def test_corrupted_srecord(self):
        text = (LAB / 'trace-r1/artifacts/G_trace_CPU0.mot').read_text(encoding='ascii')
        lines = text.splitlines()
        lines[1] = lines[1][:-2] + ('00' if lines[1][-2:] != '00' else '01')
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'bad.mot'
            path.write_text('\n'.join(lines), encoding='ascii')
            with self.assertRaises(AssertionError):
                mot_bytes(path)


if __name__ == '__main__':
    unittest.main()
