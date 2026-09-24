"""Check patched control flow and reject the BL/B.W mistake that loses LR."""
import unittest
from make_wait_pair import CALL, STUB, DELAY, LAB, branch_destination, movw_value
from make_pair import Elf, mot_bytes, fnv, SYSINIT
from read_log import FINGERPRINTS


def b16_target(code, address):
    insn = int.from_bytes(code, 'little')
    assert insn & 0xF800 == 0xE000
    immediate = insn & 0x7FF
    if immediate & 0x400:
        immediate -= 0x800
    return address + 4 + immediate * 2


class WaitPairTests(unittest.TestCase):
    def test_link_and_tail_branch_are_not_interchangeable(self):
        # LLVM-assembled forward BL and backward B.W, with independent targets.
        call, tail = bytes.fromhex('00f001f8'), bytes.fromhex('fff75ebb')
        self.assertEqual(branch_destination(call, CALL, True), STUB)
        self.assertEqual(branch_destination(tail, STUB + 4, False), DELAY)
        with self.assertRaises(AssertionError):
            branch_destination(tail, STUB + 4, True)
        with self.assertRaises(AssertionError):
            branch_destination(call, CALL, False)

    def test_actual_images_return_to_rtc_skip(self):
        for name, units in [('W_wait_short_CPU0', 1), ('W_wait_200ms_CPU0', 1000)]:
            with self.subTest(name=name):
                e = Elf((LAB / 'wait-r1/artifacts' / (name + '.elf')).read_bytes())
                def code(a, n):
                    o = e.offset(a, n)
                    return e.data[o:o + n]
                self.assertEqual(branch_destination(code(CALL, 4), CALL, True), STUB)
                self.assertEqual(movw_value(code(STUB, 4)), (1, units))
                self.assertEqual(branch_destination(code(STUB + 4, 4), STUB + 4, False), DELAY)
                # BX LR in the unchanged delay returns to the W skip, NOT
                # the now non-instruction-aligned remnant at STUB+8.
                return_pc = ((CALL + 4) | 1) & ~1
                self.assertEqual(return_pc, 0x0203C39C)
                self.assertEqual(b16_target(code(return_pc, 2), return_pc), 0x0203C3D0)
                self.assertEqual(code(0x0203C3D0, 4), bytes.fromhex('05f11c09'))
                self.assertEqual(b16_target(code(0x0203C3D4, 2), 0x0203C3D4), 0x0203C3F6)
                self.assertEqual(code(0x0203BADE, 2), bytes.fromhex('7047'))

    def test_mot_and_log_identity(self):
        for name, label in [('W_wait_short_CPU0', 'W_wait_short'), ('W_wait_200ms_CPU0', 'W_wait_200ms')]:
            with self.subTest(name=name):
                out = LAB / 'wait-r1/artifacts'
                e = Elf((out / (name + '.elf')).read_bytes())
                memory, entry = mot_bytes(out / (name + '.mot'))
                self.assertEqual(memory, e.load_bytes())
                self.assertEqual(entry, e.entry)
                fingerprint = fnv(bytes(memory[SYSINIT + i] for i in range(4096)))
                self.assertEqual(FINGERPRINTS[fingerprint], label)

    def test_pair_has_only_two_immediate_bytes_changed(self):
        out = LAB / 'wait-r1/artifacts'
        short = (out / 'W_wait_short_CPU0.elf').read_bytes()
        long = (out / 'W_wait_200ms_CPU0.elf').read_bytes()
        self.assertEqual(len(short), len(long))
        elf = Elf(short)
        self.assertEqual({i for i, (a, b) in enumerate(zip(short, long)) if a != b},
                         {elf.offset(0x0203C3A0), elf.offset(0x0203C3A1)})


if __name__ == '__main__':
    unittest.main()
