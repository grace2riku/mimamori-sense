"""Compose the existing full-recompile trace builder with a one-shot display gate."""
from pathlib import Path
import difflib
import hashlib
import json
import shutil
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'issue232-trace'))
import build
import verify

trace_instrument = build.instrument

def instrument(target):
    changed, diff = trace_instrument(target)
    def edit(name, pairs):
        nonlocal diff
        path = target / 'src' / name
        old = path.read_text(encoding='utf-8')
        new = old
        for before, after in pairs:
            assert new.count(before) == 1, (name, before)
            new = new.replace(before, after)
        path.write_text(new, encoding='utf-8')
        diff += ''.join(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                      fromfile='trace/src/'+name, tofile='gate/src/'+name))
    edit('lvgl_thread_entry.c', [
        ('#include "boot_trace.h"', '#include "boot_trace.h"\n#include "boot_gate.h"'),
        ('    bool trace_display_ok = glcdc_port_init();',
         '    if (!boot_gate_wait()) { tk_ext_tsk(); return; }\n'
         '    bool trace_display_ok = glcdc_port_init();'),
        ('    boot_trace_mark(BT_DISPLAY_END, trace_display_ok ? 1 : 0, 0U);',
         '    boot_trace_mark(BT_DISPLAY_END, trace_display_ok ? 1 : 0, 0U);\n'
         '    boot_gate_complete(trace_display_ok);\n'
         '    if (!trace_display_ok) { tk_ext_tsk(); return; }'),
    ])
    edit('usrcmd.c', [
        ('#include "boot_trace.h"', '#include "boot_trace.h"\n#include "boot_gate.h"'),
        ('static const cmd_table_t cmdlist[] = {',
         'static const cmd_table_t cmdlist[] = {\n'
         '    NTSHELL_CMD("bootgate", "LCD start gate: status/continue", usrcmd_bootgate),'),
        ('    const cmd_table_t *p = &cmdlist[0];\n    for (int i = 0;',
         '    if (!boot_gate_command_allowed(argv[0])) {\n'
         '        print_to_console("Gate diagnostic: only bootgate, boottrace, help, version are enabled.\\r\\n");\n'
         '        return 0;\n    }\n'
         '    const cmd_table_t *p = &cmdlist[0];\n    for (int i = 0;'),
    ])
    path = target / 'src/usrcmd.c'
    with path.open('a', encoding='utf-8') as f:
        f.write('\n#include "boot_gate.inc"\n')
    diff += '\n# Gate implementation included by src/usrcmd.c: boot_gate.inc\n'
    for name in ['boot_gate.h', 'boot_gate.inc']:
        shutil.copy2(HERE/name, target/'src'/name)
        changed.append(name)
    return changed, diff

if __name__ == '__main__':
    out = build.LAB / 'gate-r1'
    build.instrument = instrument
    build.build(out, resume='--resume' in sys.argv)
    verify.verify(out)
    audit = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
             for p in HERE.iterdir() if p.is_file()}
    (out/'gate-source-sha256.json').write_text(json.dumps(audit, indent=2), encoding='utf-8')
