"""Build a fresh diagnostic CPU0 from current sources; never access hardware.
All linked objects are recompiled; no stale object reuse. Outputs are isolated.
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed
import argparse, hashlib, json, re, shutil, subprocess, sys
from overlay import instrument

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
LLVM = Path('C:/Renesas/RA/e2studio_v2025-12_fsp_v6.3.0/toolchains/llvm_arm/ATfE-21.1.1-Windows-x86_64/bin')
LAB = ROOT / 'e2studio_CPU0/Debug/issue232'
BASE = ROOT / 'e2studio_CPU0'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def rebase(s, target):
    return s.replace(str(BASE).replace('\\','\\\\'), str(target).replace('\\','\\\\')).replace(BASE.as_posix(), target.as_posix()).replace(str(BASE),str(target))

def build(out, resume=False):
    out = out.resolve()
    assert out.is_relative_to(LAB.resolve()) and out != LAB.resolve()
    target = out/'e2studio_CPU0'
    debug = target/'Debug'
    if not resume:
        assert not out.exists(), 'Preserve existing runs; use --resume only for an unfinished run'
        out.mkdir(parents=True)
        print('Copying source inputs',flush=True)
        shutil.copytree(BASE,target,ignore=shutil.ignore_patterns('Debug','.git'))
        debug.mkdir()
        link = (BASE/'Debug/mimamori_sense_CPU0.elf.in').read_text(encoding='utf-8-sig')
        objects = re.findall(r'\./([^\s"]+\.o)\b',link)
        assert objects and len(objects)==len(set(objects))
        for obj in objects:
            rsp = BASE/'Debug'/(obj+'.in')
            assert rsp.exists(), f'No source response for {obj}'
            dest=debug/(obj+'.in'); dest.parent.mkdir(parents=True,exist_ok=True)
            dest.write_text(rebase(rsp.read_text(encoding='utf-8-sig'),target),encoding='utf-8')
        for name in ['fsp_gen.lld','memory_regions.lld','bsp_linker_info.h']:
            (debug/name).write_text(rebase((BASE/'Debug'/name).read_text(encoding='utf-8-sig'),target),encoding='utf-8')
        changed,diff = instrument(target)
        (out/'instrumentation.diff').write_text(diff,encoding='utf-8')
        for name in ['boot_trace.c','boot_trace.h']: shutil.copy2(HERE/name,target/'src'/name)
        (debug/'src/boot_trace.o.in').write_text((debug/'src/hal_warmstart.o.in').read_text(encoding='utf-8').replace('hal_warmstart','boot_trace'),encoding='utf-8')
        objects.append('src/boot_trace.o')
        (debug/'mimamori_sense_CPU0.elf.in').write_text(rebase(link,target)+' ./src/boot_trace.o\n',encoding='utf-8')
        provenance={'git_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
          'changed_user_sources':changed,'objects':objects,'full_recompile':True,
          'source_sha256':{str(p.relative_to(target)):sha(p) for p in target.rglob('*') if p.is_file() and 'Debug' not in p.relative_to(target).parts},
          'overlay_sha256':{p.name:sha(p) for p in HERE.iterdir() if p.is_file()}}
        (out/'build-provenance.json').write_text(json.dumps(provenance,indent=2),encoding='utf-8')
    else:
        assert not (out/'artifacts/build-verification.json').exists(), 'Completed runs are immutable'
        objects=json.loads((out/'build-provenance.json').read_text())['objects']
    def compile_one(obj):
        rsp=obj+'.in'
        result=subprocess.run([str(LLVM/'clang.exe'),'--target=arm-none-eabi','@'+rsp],cwd=debug,capture_output=True)
        (debug/(obj+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode: raise RuntimeError(obj+' failed; see '+str(debug/(obj+'.log')))
        return obj
    print('Recompiling',len(objects),'objects',flush=True)
    failures=[]
    with ThreadPoolExecutor(max_workers=6) as pool:
        for i,f in enumerate(as_completed([pool.submit(compile_one,o) for o in objects]),1):
            try:f.result()
            except Exception as e: failures.append(str(e)); print(str(e),flush=True)
            if i%100==0:print('Compiled',i,'/',len(objects),flush=True)
    assert not failures,'\n'.join(failures)
    print('Linking',flush=True)
    with (out/'link.log').open('wb') as log:
        subprocess.run([str(LLVM/'clang.exe'),'--target=arm-none-eabi','@mimamori_sense_CPU0.elf.in'],cwd=debug,stdout=log,stderr=subprocess.STDOUT,check=True)
    print('Link complete; run verify.py',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=LAB/'trace-r1');p.add_argument('--resume',action='store_true')
    a=p.parse_args();build(a.output,a.resume)
