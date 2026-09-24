"""Build diagnostic G from the preserved G inputs. Never accesses hardware.

Uses unchanged saved LTO objects and recompiles the five instrumented user
sources plus boot_trace.c. No generated ra/ra_gen source is edited.
Output directories must be new, to preserve all previously measured images.
"""
from pathlib import Path
import argparse
import difflib
import hashlib
import json
import shutil
import subprocess

BASE_HASH = '13b44d4518a8c2f456294e16c6ff733fd366d1831f9b6fa71dcc5e216130b221'
LLVM = Path('C:/Renesas/RA/e2studio_v2025-12_fsp_v6.3.0/toolchains/llvm_arm/ATfE-21.1.1-Windows-x86_64/bin')
HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
LAB = ROOT / 'e2studio_CPU0/Debug/issue230'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(out):
    base = LAB / 'G/e2studio_CPU0'
    assert sha(base / 'Debug/mimamori_sense_CPU0.elf') == BASE_HASH, 'Unexpected baseline'
    out = out.resolve()
    assert out.is_relative_to(LAB.resolve()), 'Output must stay inside the issue230 lab'
    assert not out.exists(), 'Output exists; preserve it and choose a new directory'
    target = out / 'e2studio_CPU0'
    out.mkdir(parents=True)
    shutil.copytree(base, target)
    debug = target / 'Debug'
    # All old inputs remain intact. Rebase response files used in this clone.
    for path in debug.rglob('*.in'):
        value = path.read_text(encoding='utf-8-sig')
        value = value.replace(base.as_posix(), target.as_posix())
        value = value.replace(str(base).replace('\\', '\\\\'), str(target).replace('\\', '\\\\'))
        path.write_text(value, encoding='utf-8')

    changes = {
        'hal_warmstart.c': [
            ('    if (BSP_WARM_START_RESET == event)',
             '    if (BSP_WARM_START_POST_CLOCK == event) { boot_trace_clock_start(); }\n\n    if (BSP_WARM_START_RESET == event)'),
            ('        /* C runtime environment and system clocks are setup. */',
             '        boot_trace_post_c(DWT->CYCCNT);\n        /* C runtime environment and system clocks are setup. */'),
            ('        R_BSP_SdramInit(true);',
             '        boot_trace_cycles(BT_SDRAM_BEFORE, 0);\n        R_BSP_SdramInit(true);'),
            ('        sdram_port_init();',
             '        bool trace_sdram_ok = sdram_port_init();\n        boot_trace_cycles(BT_SDRAM_AFTER, trace_sdram_ok ? 1 : 0);'),
            ('    knl_start_mtkernel();',
             '    boot_trace_cycles(BT_OS_BEFORE, 0);\n    knl_start_mtkernel();'),
        ],
        'lvgl_thread_entry.c': [
            ('    glcdc_port_init();',
             '    boot_trace_os(BT_DISPLAY_BEFORE, 0);\n    bool trace_display_ok = glcdc_port_init();\n    boot_trace_os(BT_DISPLAY_AFTER, trace_display_ok ? 1 : 0);'),
        ],
        'port/glcdc_port.c': [
            ('    glcdc_lcd_reset();',
             '    boot_trace_os(BT_RESET_BEFORE, 0);\n    glcdc_lcd_reset();\n    boot_trace_os(BT_RESET_AFTER, 0);'),
        ],
        'port/lvgl_port_mtk3.c': [
            ('    error = R_GLCDC_Open(p_cfg->p_display_instance->p_ctrl, &s_display_cfg);',
             '    error = R_GLCDC_Open(p_cfg->p_display_instance->p_ctrl, &s_display_cfg);\n    boot_trace_os(BT_GLCDC_OPEN, (int32_t)error);'),
            ('    error = R_GLCDC_Start(p_cfg->p_display_instance->p_ctrl);',
             '    error = R_GLCDC_Start(p_cfg->p_display_instance->p_ctrl);\n    boot_trace_os(BT_GLCDC_START, (int32_t)error);'),
        ],
        'usrcmd.c': [
            ('static const cmd_table_t cmdlist[] = {',
             'static const cmd_table_t cmdlist[] = {\n    NTSHELL_CMD("boottrace", "Read saved boot timing (no reset)", usrcmd_boottrace),'),
        ],
    }
    diff = []
    for rel, replacements in changes.items():
        path = target / 'src' / rel
        old = path.read_text(encoding='utf-8-sig')
        new = old
        for before, after in replacements:
            assert new.count(before) == 1, f'Non-unique patch: {rel}: {before}'
            new = new.replace(before, after)
        header = '../boot_trace.h' if '/' in rel else 'boot_trace.h'
        new = f'#include "{header}"\n' + new
        path.write_text(new, encoding='utf-8')
        diff.extend(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                                       fromfile='G/src/' + rel, tofile='G_trace/src/' + rel))
    for name in ['boot_trace.c', 'boot_trace.h']:
        shutil.copy2(HERE / name, target / 'src' / name)
    (out / 'instrumentation.diff').write_text(''.join(diff), encoding='utf-8')

    response = debug / 'src/boot_trace.o.in'
    response.write_text((debug / 'src/hal_warmstart.o.in').read_text(encoding='utf-8')
                        .replace('hal_warmstart', 'boot_trace'), encoding='utf-8')
    link = debug / 'mimamori_sense_CPU0.elf.in'
    link.write_text(link.read_text(encoding='utf-8') + ' ./src/boot_trace.o\n', encoding='utf-8')

    with (out / 'build.log').open('w', encoding='utf-8') as log:
        for rel in [*changes, 'boot_trace.c']:
            rsp = 'src/' + rel.removesuffix('.c') + '.o.in'
            print('Compiling', rel, flush=True)
            subprocess.run([str(LLVM / 'clang.exe'), '--target=arm-none-eabi', '@' + rsp],
                           cwd=debug, stdout=log, stderr=subprocess.STDOUT, check=True)
        print('Linking', flush=True)
        subprocess.run([str(LLVM / 'clang.exe'), '--target=arm-none-eabi', '@' + link.name],
                       cwd=debug, stdout=log, stderr=subprocess.STDOUT, check=True)
    artifacts = out / 'artifacts'
    artifacts.mkdir()
    elf = artifacts / 'G_trace_CPU0.elf'
    shutil.copy2(debug / 'mimamori_sense_CPU0.elf', elf)
    shutil.copy2(debug / 'mimamori_sense_CPU0.map', artifacts / 'G_trace_CPU0.map')
    subprocess.run([str(LLVM / 'llvm-objcopy.exe'), '-O', 'srec', str(elf),
                    str(artifacts / 'G_trace_CPU0.mot')], check=True)
    with (out / 'disassembly.txt').open('w', encoding='utf-8') as log:
        subprocess.run([str(LLVM / 'llvm-objdump.exe'), '-d', str(elf)], stdout=log, check=True)
    # Full provenance for reused objects and instrumented source inputs.
    inputs = {str(p.relative_to(base)): sha(p) for p in (base / 'Debug').rglob('*.o')}
    for p in HERE.glob('*'):
        if p.is_file():
            inputs['overlay/' + p.name] = sha(p)
    (out / 'build-provenance.json').write_text(json.dumps({
        'base_elf_sha256': BASE_HASH, 'g_trace_sha256': sha(elf),
        'recompiled': [*changes, 'boot_trace.c'], 'inputs': inputs,
    }, indent=2), encoding='utf-8')
    print('Built:', elf, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=LAB / 'trace-r1')
    build(parser.parse_args().output)
