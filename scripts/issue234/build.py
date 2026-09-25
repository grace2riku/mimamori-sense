"""Fresh CPU0 build using e2 studio's response files, without editing them.

Run after e2 studio has generated Debug/*.in. New UI source uses the existing
UI compile flags. All objects are rebuilt into Debug/issue234/<run>.
"""
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'e2studio_CPU0'
DEBUG = BASE / 'Debug'
LLVM = Path('C:/Renesas/RA/e2studio_v2025-12_fsp_v6.3.0/toolchains/llvm_arm/ATfE-21.1.1-Windows-x86_64/bin')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(run):
    out = (DEBUG / 'issue234' / run).resolve()
    assert out.is_relative_to((DEBUG / 'issue234').resolve())
    out.mkdir(parents=True, exist_ok=False)
    link = (DEBUG / 'mimamori_sense_CPU0.elf.in').read_text(encoding='utf-8-sig')
    objects = re.findall(r'\./([^\s"]+\.o)\b', link)
    assert objects and len(objects) == len(set(objects))
    # The current main also includes the startup screen. e2 studio's response
    # files may predate both features, so include all new production sources.
    additions = ['ui_time_setting_screen', 'ui_startup_screen',
                 'ui_startup_image', 'puff/puff']
    extra_objects = {f'src/ui/{name}.o': name for name in additions}
    for extra in extra_objects:
        if extra not in objects:
            objects.append(extra)
            link += ' ./' + extra
    for name in ('rtc_boot_diag', 'rtc_backup'):
        obj = f'src/{name}.o'
        extra_objects[obj] = name
        if obj not in objects:
            objects.append(obj)
            link += ' ./' + obj
    inputs = {}
    for obj in objects:
        template = 'src/ui/ui_datetime.o' if obj in extra_objects else obj
        rsp = (DEBUG / (template + '.in')).read_text(encoding='utf-8-sig')
        if obj in extra_objects:
            rsp = rsp.replace('ui_datetime', extra_objects[obj])
        if obj in ('src/rtc_boot_diag.o', 'src/rtc_backup.o'):
            rsp = rsp.replace('src/ui/' + extra_objects[obj], 'src/' + extra_objects[obj])
        source = re.search(r'"(\.\./[^\"]+\.(?:c|cpp|cc|S|s))"', rsp)
        assert source, obj
        path = (DEBUG / source[1]).resolve()
        inputs[str(path.relative_to(BASE))] = digest(path)
        # Keep the original working directory and include paths. Only object
        # and dependency outputs are redirected, never sources or generated code.
        for ext in ('.o', '.d'):
            rel = str(Path(obj).with_suffix(ext)).replace('\\', '/')
            rsp = rsp.replace('"' + rel + '"', '"' + (out / rel).as_posix() + '"')
        target = out / (obj + '.in')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(rsp, encoding='utf-8')
        link = link.replace('./' + obj, '"' + (out / obj).as_posix() + '"')
    for folder in ('src', 'ra_cfg', 'ra_gen'):
        for path in (BASE / folder).rglob('*'):
            if path.is_file():
                inputs[str(path.relative_to(BASE))] = digest(path)
    for name in ('mimamori_sense_CPU0.elf', 'mimamori_sense_CPU0.map'):
        link = link.replace(name, (out / name).as_posix())
    (out / 'link.in').write_text(link, encoding='utf-8')

    def compile_one(obj):
        result = subprocess.run([str(LLVM / 'clang.exe'), '--target=arm-none-eabi',
                                 '@' + str(out / (obj + '.in'))], cwd=DEBUG, capture_output=True)
        (out / (obj + '.log')).write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f'{obj}: see {out / (obj + ".log")}')

    print(f'Rebuilding {len(objects)} objects into {out}', flush=True)
    errors = []
    with ThreadPoolExecutor(max_workers=6) as pool:
        for n, task in enumerate(as_completed([pool.submit(compile_one, obj) for obj in objects]), 1):
            try:
                task.result()
            except Exception as error:
                errors.append(str(error))
            if n % 100 == 0:
                print(f'{n}/{len(objects)}', flush=True)
    assert not errors, '\n'.join(errors)
    with (out / 'link.log').open('wb') as log:
        subprocess.run([str(LLVM / 'clang.exe'), '--target=arm-none-eabi', '@' + str(out / 'link.in')],
                       cwd=DEBUG, stdout=log, stderr=subprocess.STDOUT, check=True)
    elf = out / 'mimamori_sense_CPU0.elf'
    mot = out / 'mimamori_sense_CPU0.mot'
    # Flash programmer input must not contain SDRAM/RAM initialization or OFS
    # register records from the debugger ELF. verify.py checks this against LMA.
    sections = ['.flash*', '__flash*', '__ram_from_flash*', '__ram_tdata*',
                '__sdram_from_flash*', '__itcm_from_flash*', '__dtcm_from_flash*',
                '__ospi*_from_flash*']
    subprocess.run([str(LLVM / 'llvm-objcopy.exe'), '-O', 'srec',
                    *['--only-section=' + name for name in sections], str(elf), str(mot)], check=True)
    size = subprocess.check_output([str(LLVM / 'llvm-size.exe'), str(elf)], text=True)
    (out / 'size.txt').write_text(size, encoding='utf-8')
    for rel, value in inputs.items():
        assert digest(BASE / rel) == value, f'Source changed during build: {rel}'
    manifest = {'full_recompile': True, 'object_count': len(objects), 'source_sha256': inputs,
                'elf_sha256': digest(elf), 'mot_sha256': digest(mot), 'hardware_tested': False}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(size, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--run', required=True, help='New output directory name')
    build(parser.parse_args().run)
