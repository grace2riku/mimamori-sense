#ifndef ISSUE232_BOOT_GATE_H
#define ISSUE232_BOOT_GATE_H
#include <stdbool.h>
bool boot_gate_wait(void);
void boot_gate_complete(bool ok);
int usrcmd_bootgate(int argc, char **argv);
bool boot_gate_command_allowed(const char *name);
#endif
