"""Package the diagnostic and validate S-records against ELF load sections."""
from pathlib import Path
import argparse, hashlib, json, re, shutil, struct, subprocess
from build import ROOT, LAB, LLVM, sha

class Elf:
    def __init__(self,path):
        self.data=path.read_bytes();d=self.data
        assert d[:7]==b'\x7fELF\x01\x01\x01'
        self.entry,phoff,shoff=struct.unpack_from('<III',d,24)
        phsize,phnum,shsize,shnum=struct.unpack_from('<HHHH',d,42)
        assert phsize==32 and shsize==40
        self.segments=[struct.unpack_from('<8I',d,phoff+i*phsize) for i in range(phnum)]
        self.sections=[struct.unpack_from('<10I',d,shoff+i*shsize) for i in range(shnum)]
    def loads(self):
        result={}
        for s in self.sections:
            if s[1] in (0,8) or not s[2]&2 or not s[5]:continue
            matches=[p for p in self.segments if p[0]==1 and p[2]<=s[3] and s[3]+s[5]<=p[2]+p[5] and p[1]<=s[4] and s[4]+s[5]<=p[1]+p[4]]
            assert len(matches)==1
            p=matches[0];address=p[3]+s[3]-p[2]
            for i,v in enumerate(self.data[s[4]:s[4]+s[5]]):
                assert address+i not in result
                result[address+i]=v
        return result

def mot_bytes(path):
    result={};entry=None
    for line in path.read_text(encoding='ascii').splitlines():
        assert line.startswith('S')
        kind=int(line[1]);raw=bytes.fromhex(line[2:]);assert raw[0]+1==len(raw) and sum(raw)&255==255
        alen={0:2,1:2,2:3,3:4,5:2,6:3,7:4,8:3,9:2}[kind]
        address=int.from_bytes(raw[1:1+alen],'big')
        if kind in (1,2,3):
            for i,v in enumerate(raw[1+alen:-1]):
                assert address+i not in result
                result[address+i]=v
        if kind in (7,8,9):assert entry is None;entry=address
    assert result and entry is not None
    return result,entry

def verify(out):
    out=out.resolve();assert out.is_relative_to(LAB.resolve())
    art=out/'artifacts'
    assert not (art/'build-verification.json').exists(), 'Completed package is immutable'
    art.mkdir(exist_ok=True)  # Allow retry of an incomplete validation; never a released package.
    debug=out/'e2studio_CPU0/Debug'
    saved=ROOT/'e2studio_CPU0/Debug/issue230/main-wait-r2/artifacts'
    expected_cpu1='8cb5d5e9040d0a9bb0ad72d233d21115b5e68cba3c0524e03f3e0fb4d31d2c53'
    assert sha(saved/'MAIN_wait200_postc_CPU1.mot')==expected_cpu1
    manifest={'hardware_validated':False,'elf_patched':False,'hook':'POST_C','requested_wait_ms':200,
              'cpu1_reused_from':'main-wait-r2','images':{},'static_audit':'see static-audit.md'}
    loads=[]
    for core in (0,1):
        name=f'RTC_TRACE_CPU{core}';elf=art/(name+'.elf');mot=art/(name+'.mot')
        source=debug/f'mimamori_sense_CPU{core}' if core==0 else saved/f'MAIN_wait200_postc_CPU{core}'
        for ext in ('.elf','.map'):shutil.copy2(source.with_suffix(ext),art/(name+ext))
        if core==0:subprocess.run([str(LLVM/'llvm-objcopy.exe'),'-O','srec',str(elf),str(mot)],check=True)
        else:shutil.copy2(saved/'MAIN_wait200_postc_CPU1.mot',mot)
        data,entry=mot_bytes(mot);e=Elf(elf)
        assert data==e.loads() and entry==e.entry
        assert all(0x02000000<=a<0x03000000 for a in data)
        loads.append(data)
        with (art/(name+'-disassembly.txt')).open('w',encoding='utf-8') as log:
            subprocess.run([str(LLVM/'llvm-objdump.exe'),'-d',str(elf)],stdout=log,check=True)
        manifest['images'][name]={'mot_sha256':sha(mot),'elf_sha256':sha(elf),'load_bytes':len(data),
                                 'mot_matches_elf':True,'checksums_valid':True,'entry':hex(entry)}
    assert not loads[0].keys()&loads[1].keys()
    nm=subprocess.check_output([str(LLVM/'llvm-nm.exe'),'-S','--defined-only',str(art/'RTC_TRACE_CPU0.elf')],text=True)
    (art/'symbols.txt').write_text(nm,encoding='utf-8')
    symbols={}
    for l in nm.splitlines():
        p=l.split()
        if len(p)==4:
            try:symbols[p[3]]=(int(p[0],16),int(p[1],16),p[2])
            except ValueError:pass
    address,size,kind=symbols['s_boot_trace']
    assert 0x22000000<=address and address+size<=0x22200000 and kind.lower()=='b' and size<=1536
    sysinit=symbols['SystemInit'][0]&~1
    fnv=2166136261
    for i in range(4096):fnv=((fnv^loads[0][sysinit+i])*16777619)&0xFFFFFFFF
    manifest.update(trace_ram_address=hex(address),trace_ram_bytes=size,systeminit_address=hex(sysinit),
                    fingerprint_fnv4096=f'{fnv:08X}',cpu0_cpu1_overlap_bytes=0)
    prov=json.loads((out/'build-provenance.json').read_text(encoding='utf-8'))
    for rel,digest in prov['source_sha256'].items():
        assert sha(out/'e2studio_CPU0'/rel)==digest, 'Frozen source changed: '+rel
    manifest['input_source_hashes_verified']=True
    # Regular user sources and generated/library sources were not edited by the build.
    changed={'src/'+r for r in prov['changed_user_sources']}|{'src/boot_trace.c','src/boot_trace.h'}
    checked=0
    for rel,digest in prov['source_sha256'].items():
        if Path(rel).as_posix() not in changed:
            assert sha(ROOT/'e2studio_CPU0'/rel)==digest,'Original input changed: '+rel
            checked+=1
    manifest['unchanged_original_input_files_checked']=checked
    for name in ('boot_trace.c','boot_trace.h'):shutil.copy2(out/'e2studio_CPU0/src'/name,art/name)
    shutil.copy2(out/'instrumentation.diff',art/'instrumentation.diff')
    (art/'build-verification.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print(json.dumps(manifest,indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=LAB/'trace-r1');verify(p.parse_args().output)
