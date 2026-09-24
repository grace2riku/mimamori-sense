"""Validate one complete boottrace capture. Missing events remain missing.
OS differences are quantized; DWT differences are cycle intervals, not wall time.
"""
import argparse, json, re
from pathlib import Path

NAMES = ('INIT_BEGIN INIT_END OPEN_BEGIN OPEN_END BRANCH PROVISION_BEGIN PROVISION_END '
         'GET_BEGIN GET_END SYNC_BEGIN SYNC_END DISPLAY_BEGIN DISPLAY_END RESET_BEGIN RESET_END '
         'GLCDC_OPEN_BEGIN GLCDC_OPEN_END GLCDC_START_BEGIN GLCDC_START_END '
         'INITIAL_BUFFER_BEGIN INITIAL_BUFFER_END FLUSH_BEGIN FLUSH_END BACKLIGHT WAIT_BEGIN WAIT_END').split()
HEADER = re.compile(r'BOOTTRACE232 v=(\d+) fnv=([0-9A-Fa-f]{8}) hz=(\d+) tick_ms=(\d+) probe=(\d+) fault=(\d+) epoch=(\d+) count=(\d+)')
ROW = re.compile(r'BT (\w+) seq=(\d+) hi=(\d+) lo=(\d+) cyc=(\d+) epoch=(\d+) ctrl=([0-9A-Fa-f]{8}) result=(-?\d+) aux=([0-9A-Fa-f]{8}) extra=([0-9A-Fa-f]{8})')

def analyze(text, expected_fnv=None):
    headers=list(HEADER.finditer(text))
    if len(headers)!=1 or text.count('BOOTTRACE232 END')!=1:
        raise ValueError('Provide exactly one complete boot capture')
    h=headers[0]; end=text.index('BOOTTRACE232 END')
    if end<h.end():raise ValueError('Terminator precedes header')
    body=text[h.end():end]
    if not re.search(r'POST_C cycles=\d+ ctrl=[0-9A-Fa-f]{8}',body):raise ValueError('Missing POST_C metadata')
    v,fnv,hz,tick,probe,fault,epoch,count=h.groups()
    hz,tick,probe,fault,epoch,count=map(int,(hz,tick,probe,fault,epoch,count))
    if int(v)!=23201 or hz<=0 or tick!=10:raise ValueError('Unsupported trace clock/version')
    if expected_fnv and fnv.upper()!=expected_fnv.upper():raise ValueError('Firmware fingerprint mismatch')
    rows={}
    for match in ROW.finditer(body):
        name,seq,hi,lo,cyc,ep,ctrl,result,aux,extra=match.groups()
        if name not in NAMES or name in rows:raise ValueError('Unknown/duplicate event '+name)
        nums=list(map(int,(seq,hi,lo,cyc,ep)))
        if any(x<0 or x>0xFFFFFFFF for x in nums):raise ValueError('u32 out of range')
        seq,hi,lo,cyc,ep=nums
        rows[name]=dict(name=name,seq=seq,ms=(hi<<32)|lo,cycles=cyc,epoch=ep,
                       ctrl=int(ctrl,16),result=int(result),aux=int(aux,16),extra=int(extra,16))
    if set(rows)!=set(NAMES):raise ValueError('Truncated/malformed event list')
    events=sorted((r for r in rows.values() if r['seq']),key=lambda r:r['seq'])
    if [r['seq'] for r in events]!=list(range(1,count+1)):raise ValueError('Sequence duplicate/gap/count mismatch')
    if any(a['ms']>b['ms'] for a,b in zip(events,events[1:])):raise ValueError('Operating time reversed')
    warnings=[]
    if fault:warnings.append('Recorder fault: all DWT intervals invalid')
    if not probe:warnings.append('DWT progression probe failed: all DWT intervals invalid')
    durations={}
    for begin in (n for n in NAMES if n.endswith('_BEGIN')):
        name=begin[:-6]; a,b=rows[begin],rows[name+'_END']
        if not a['seq'] or not b['seq']:
            if b['seq']:raise ValueError('END without BEGIN: '+name)
            durations[name]={'status':'not_reached' if not a['seq'] else 'not_completed_at_snapshot'}
            continue
        if a['seq']>=b['seq']:raise ValueError('Reversed pair '+name)
        dt=b['ms']-a['ms']
        item={'status':'returned','os_delta_ms_quantized':dt,'tick_ms':tick,'result':b['result']}
        reason=None
        if fault or not probe:reason='recorder_or_probe_fault'
        elif not (a['ctrl']&b['ctrl']&1):reason='counter_disabled'
        elif a['epoch']!=b['epoch']:reason='counter_reset_between_events'
        elif (dt+2*tick)*hz>=2**32*1000:reason='wrap_bound_not_established'
        else:
            cycles=(b['cycles']-a['cycles'])&0xFFFFFFFF
            if cycles*1000>(dt+2*tick)*hz:reason='counter_inconsistent_with_os_bound'
            else:item['dwt_cycle_interval']=cycles
        item['dwt_invalid_reason']=reason
        durations[name]=item
    branch=rows['BRANCH']
    branch_info={'status':'not_reached'}
    if branch['seq']:
        if branch['result'] not in (0,1):raise ValueError('Invalid branch flag')
        if branch['result'] and rows['PROVISION_BEGIN']['seq']:raise ValueError('Provisioned branch ran reset')
        raw=branch['aux'];clock=branch['extra']
        branch_info=dict(provisioned_predicate=bool(branch['result']),rcr1=f'{raw&255:02X}',
             rcr2=f'{(raw>>8)&255:02X}',rcr4=f'{(raw>>16)&255:02X}',
             sosccr=f'{clock&255:02X}',somcr=f'{(clock>>8)&255:02X}')
    for r in events:
        if r['epoch']>epoch:raise ValueError('Event epoch exceeds snapshot epoch')
    return {'firmware_fnv':fnv.upper(),'fingerprint_checked':bool(expected_fnv),
            'branch':branch_info,'ordered_events':events,'durations':durations,'warnings':warnings,
            'limits':'Cycles include preemption and may exclude stopped/sleep clock time; OS delta is 10ms quantized. No visual success inferred.'}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--manifest',type=Path)
    a=p.parse_args();expected=None
    if a.manifest:expected=json.loads(a.manifest.read_text(encoding='utf-8'))['fingerprint_fnv4096']
    print(json.dumps(analyze(a.log.read_text(encoding='utf-8-sig'),expected),ensure_ascii=False,indent=2))
