/* Diagnostic only; included once by usrcmd.c. Task writers only, no ISR access.
 * The LVGL task owns all display operations. The shell only posts one request.
 * All multiword state publication and snapshots use the same dispatch exclusion.
 */
#include "boot_gate.h"

enum gate_state { G_INIT, G_HOLD, G_REQUESTED, G_RUNNING, G_DISPLAY_READY, G_ERROR };
typedef struct {
    enum gate_state state;
    ID flag;
    ER error;
    SYSTIM held, requested, running, finished;
} gate_record;
static gate_record s_boot_gate;

static void gate_fail(ER error)
{
    tk_dis_dsp();
    s_boot_gate.error = error;
    s_boot_gate.state = G_ERROR;
    tk_get_otm(&s_boot_gate.finished);
    tk_ena_dsp();
}

bool boot_gate_wait(void)
{
    const T_CFLG cfg = { .exinf = NULL, .flgatr = TA_TFIFO, .iflgptn = 0 };
    ID flag = tk_cre_flg(&cfg);
    if (flag <= 0) { gate_fail((ER)flag); return false; }
    tk_dis_dsp();
    s_boot_gate.flag = flag;
    s_boot_gate.state = G_HOLD;
    tk_get_otm(&s_boot_gate.held);
    tk_ena_dsp();
    UINT bits = 0;
    ER err = tk_wai_flg(flag, 1U, TWF_ORW | TWF_BITCLR, &bits, TMO_FEVR);
    if (err != E_OK) { gate_fail(err); return false; }
    tk_dis_dsp();
    bool requested = (s_boot_gate.state == G_REQUESTED);
    if (requested) {
        s_boot_gate.state = G_RUNNING;
        tk_get_otm(&s_boot_gate.running);
    }
    tk_ena_dsp();
    if (!requested) { gate_fail(E_OBJ); return false; }
    return true;
}

void boot_gate_complete(bool ok)
{
    tk_dis_dsp();
    s_boot_gate.state = ok ? G_DISPLAY_READY : G_ERROR;
    s_boot_gate.error = ok ? E_OK : E_SYS;
    tk_get_otm(&s_boot_gate.finished);
    tk_ena_dsp();
}

bool boot_gate_command_allowed(const char *name)
{
    return strcmp(name, "bootgate") == 0 || strcmp(name, "boottrace") == 0 ||
           strcmp(name, "help") == 0 || strcmp(name, "version") == 0;
}

int usrcmd_bootgate(int argc, char **argv)
{
    static const char * const names[] = {
        "INIT", "HOLD", "REQUESTED", "RUNNING", "DISPLAY_READY", "ERROR"
    };
    if (argc != 2 || (strcmp(argv[1], "status") && strcmp(argv[1], "continue"))) {
        print_to_console("Usage: bootgate status | bootgate continue\r\n");
        return 0;
    }
    if (!strcmp(argv[1], "continue")) {
        tk_dis_dsp();
        bool held = s_boot_gate.state == G_HOLD;
        ID flag = s_boot_gate.flag;
        if (held) {
            s_boot_gate.state = G_REQUESTED;
            s_boot_gate.error = E_OK;
            tk_get_otm(&s_boot_gate.requested);
        }
        tk_ena_dsp();
        if (!held) {
            print_to_console("Not accepted: state must be HOLD. Use bootgate status.\r\n");
            return 0;
        }
        ER err = tk_set_flg(flag, 1U);
        if (err != E_OK) {
            tk_dis_dsp();
            s_boot_gate.state = G_HOLD;
            s_boot_gate.error = err;
            tk_ena_dsp();
            print_to_console("Resume notification failed; still HOLD. Use bootgate status.\r\n");
        } else {
            print_to_console("Resume requested once. Use bootgate status to check completion.\r\n");
        }
        return 0;
    }
    gate_record copy;
    tk_dis_dsp();
    copy = s_boot_gate;
    tk_ena_dsp();
    char buf[200];
    snprintf(buf, sizeof(buf), "BOOTGATE232 v=23202 state=%s error=%ld tick_ms=10\r\n",
             names[copy.state], (long)copy.error);
    print_to_console(buf);
    const SYSTIM times[] = {copy.held, copy.requested, copy.running, copy.finished};
    const char * const labels[] = {"held", "requested", "running", "finished"};
    for (unsigned int i = 0; i < 4U; ++i) {
        snprintf(buf, sizeof(buf), "%s hi=%lu lo=%lu\r\n", labels[i],
                 (unsigned long)times[i].hi, (unsigned long)times[i].lo);
        print_to_console(buf);
    }
    print_to_console("DISPLAY_READY means GLCDC init returned success, not panel power OK.\r\n");
    return 0;
}
