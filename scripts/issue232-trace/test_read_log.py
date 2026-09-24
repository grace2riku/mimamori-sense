import unittest
from read_log import analyze,NAMES

def capture(values):
    count=sum(bool(v['seq']) for v in values.values())
    rows=['BOOTTRACE232 v=23201 fnv=1234ABCD hz=1000000000 tick_ms=10 probe=12 fault=0 epoch=1 count='+str(count),
          'POST_C cycles=123 ctrl=00000001']
    for n in NAMES:
        v=values.get(n,{})
        rows.append(f"BT {n} seq={v.get('seq',0)} hi=0 lo={v.get('ms',0)} cyc={v.get('cyc',0)} epoch={v.get('ep',0)} ctrl=00000001 result={v.get('result',0)} aux=00000000 extra=00000000")
    return '\n'.join(rows+['BOOTTRACE232 END'])

class LogTest(unittest.TestCase):
    def pair(self,**overrides):
        b=dict(seq=2,ms=10,cyc=200,ep=0);b.update(overrides)
        return capture({'INIT_BEGIN':dict(seq=1,ms=10,cyc=100,ep=0),'INIT_END':b})
    def test_same_tick_not_zero_duration(self):
        d=analyze(self.pair())['durations']['INIT']
        self.assertEqual(d['os_delta_ms_quantized'],0);self.assertEqual(d['dwt_cycle_interval'],100)
    def test_missing_end_is_not_success(self):
        d=analyze(capture({'INIT_BEGIN':dict(seq=1)}))['durations']['INIT']
        self.assertEqual(d['status'],'not_completed_at_snapshot');self.assertNotIn('result',d)
    def test_missing_path_is_not_zero_duration(self):
        self.assertEqual(analyze(self.pair())['durations']['GET']['status'],'not_reached')
    def test_generation_reset_rejected(self):
        self.assertEqual(analyze(self.pair(ep=1))['durations']['INIT']['dwt_invalid_reason'],'counter_reset_between_events')
    def test_long_interval_ambiguous(self):
        self.assertEqual(analyze(self.pair(ms=5000))['durations']['INIT']['dwt_invalid_reason'],'wrap_bound_not_established')
    def test_modulo_wrap_under_bound(self):
        s=capture({'INIT_BEGIN':dict(seq=1,cyc=0xFFFFFFF0),'INIT_END':dict(seq=2,cyc=16)})
        self.assertEqual(analyze(s)['durations']['INIT']['dwt_cycle_interval'],32)
    def test_error_retained(self):self.assertEqual(analyze(self.pair(result=-5))['durations']['INIT']['result'],-5)
    def test_wrong_firmware(self):
        with self.assertRaises(ValueError):analyze(self.pair(),'AAAAAAAA')
    def test_duplicate_sequence(self):
        with self.assertRaises(ValueError):analyze(self.pair(seq=1))
    def test_truncated(self):
        with self.assertRaises(ValueError):analyze(self.pair().replace('BOOTTRACE232 END',''))
    def test_probe_zero(self):
        self.assertNotIn('dwt_cycle_interval',analyze(self.pair().replace('probe=12','probe=0'))['durations']['INIT'])
    def test_inconsistent_dwt(self):
        self.assertEqual(analyze(self.pair(cyc=100000000))['durations']['INIT']['dwt_invalid_reason'],'counter_inconsistent_with_os_bound')
if __name__=='__main__':unittest.main()
