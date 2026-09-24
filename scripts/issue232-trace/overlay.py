"""Exact, fail-closed edits of eight USER sources in a diagnostic copy."""
from pathlib import Path
import difflib

def instrument(target: Path):
    diffs = []
    changed = []
    def edit(rel, pairs, region=None):
        p = target / 'src' / rel
        old = p.read_text(encoding='utf-8-sig')
        if region:
            a, b = old.index(region[0]), old.index(region[1])
        else:
            a, b = 0, len(old)
        part = old[a:b]
        for before, after in pairs:
            assert part.count(before) == 1, (rel, before, part.count(before))
            part = part.replace(before, after)
        new = old[:a] + part + old[b:]
        header = '../boot_trace.h' if '/' in rel else 'boot_trace.h'
        new = f'#include "{header}"\n' + new
        p.write_text(new, encoding='utf-8')
        diffs.extend(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                     fromfile='main/src/'+rel, tofile='trace/src/'+rel))
        changed.append(rel)
    def mark(name, result='0', aux='0U'):
        return f'boot_trace_mark(BT_{name}, {result}, {aux});'
    def surround(text, name, result='0', aux='0U'):
        return (text, mark(name+'_BEGIN')+'\n'+text+'\n    '+mark(name+'_END',result,aux))
    edit('hal_warmstart.c', [('        /* C runtime environment and system clocks are setup. */',
         '        boot_trace_post_c();\n        /* C runtime environment and system clocks are setup. */')])
    edit('ntshell_thread_entry.c', [('    if (TIME_CTRL_OK == time_ctrl_init()) {',
         '    '+mark('INIT_BEGIN')+'\n    time_ctrl_err_t trace_init_result = time_ctrl_init();\n    '+
         mark('INIT_END','(int32_t)trace_init_result')+'\n    if (TIME_CTRL_OK == trace_init_result) {')])
    edit('time_ctrl.c', [
        surround('err        = R_RTC_Open(&g_rtc_ctrl, &g_rtc_cfg);','OPEN','(int32_t)err'),
        ('if (!rtc_is_provisioned()) {', 'bool trace_provisioned = rtc_is_provisioned();\n            boot_trace_branch(trace_provisioned);\n            if (!trace_provisioned) {'),
        surround('(void)R_RTC_ClockSourceSet(&g_rtc_ctrl);','PROVISION'),
        surround('err        = R_RTC_CalendarTimeGet(&g_rtc_ctrl, &raw);','GET','(int32_t)err'),
        surround('sync_system_time(&now);','SYNC'),
    ], ('time_ctrl_err_t time_ctrl_init(void)', 'time_ctrl_err_t time_ctrl_get('))
    edit('lvgl_thread_entry.c', [('    glcdc_port_init();',
         '    '+mark('DISPLAY_BEGIN')+'\n    bool trace_display_ok = glcdc_port_init();\n    '+
         mark('DISPLAY_END','trace_display_ok ? 1 : 0'))])
    edit('port/glcdc_port.c', [
        surround('    glcdc_lcd_reset();','RESET'),
        ('        R_IOPORT_PinWrite(&g_ioport_ctrl, GLCDC_PIN_BACKLIGHT, BSP_IO_LEVEL_HIGH);',
         '        fsp_err_t trace_bl_result = R_IOPORT_PinWrite(&g_ioport_ctrl, GLCDC_PIN_BACKLIGHT, BSP_IO_LEVEL_HIGH);\n        '+mark('BACKLIGHT','(int32_t)trace_bl_result')),
    ])
    edit('port/lvgl_port_mtk3.c', [
        surround('    error = R_GLCDC_Open(p_cfg->p_display_instance->p_ctrl, &s_display_cfg);','GLCDC_OPEN','(int32_t)error'),
        surround('    error = R_GLCDC_Start(p_cfg->p_display_instance->p_ctrl);','GLCDC_START','(int32_t)error'),
        ('    if (NULL != p_cfg->p_framebuffer_1) {\n        do {',
         '    if (NULL != p_cfg->p_framebuffer_1) {\n        '+mark('INITIAL_BUFFER_BEGIN')+'\n        do {'),
        ('        } while (FSP_ERR_INVALID_UPDATE_TIMING == error);\n\n        if (error != FSP_SUCCESS)',
         '        } while (FSP_ERR_INVALID_UPDATE_TIMING == error);\n        '+mark('INITIAL_BUFFER_END','(int32_t)error','(uint32_t)(uintptr_t)p_cfg->p_framebuffer_1')+'\n\n        if (error != FSP_SUCCESS)'),
        ('    if (lv_display_flush_is_last(p_lv_display)) {\n#if BSP_CFG_DCACHE_ENABLED',
         '    if (lv_display_flush_is_last(p_lv_display)) {\n        '+mark('FLUSH_BEGIN','0','(uint32_t)(uintptr_t)p_px_map')+'\n#if BSP_CFG_DCACHE_ENABLED'),
        ('        glcdc_port_notify_flush(p_target, (int32_t)error);',
         '        '+mark('FLUSH_END','(int32_t)error','(uint32_t)(uintptr_t)p_target')+'\n        glcdc_port_notify_flush(p_target, (int32_t)error);'),
        ('        (void)tk_wai_sem(s_vsync_semid, 1, TMO_POL);',
         '        '+mark('WAIT_BEGIN')+'\n        (void)tk_wai_sem(s_vsync_semid, 1, TMO_POL);'),
        ('        ER ercd = tk_wai_sem(s_vsync_semid, 1, TMO_FEVR);',
         '        ER ercd = tk_wai_sem(s_vsync_semid, 1, TMO_FEVR);\n        '+mark('WAIT_END','(int32_t)ercd')),
    ])
    edit('ai_inference_thread_entry.c', [
        ('    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;\n    DWT->CYCCNT = 0;\n    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;',
         '    boot_trace_ai_dwt_reset();'),
    ])
    edit('usrcmd.c', [('static const cmd_table_t cmdlist[] = {',
         'static const cmd_table_t cmdlist[] = {\n    NTSHELL_CMD("boottrace", "Saved RTC/display boot timing", usrcmd_boottrace),')])
    return changed, ''.join(diffs)
