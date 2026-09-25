#include <stdbool.h>
#include <stdint.h>
enum { VBTBPCR1, VBTBPCR2, VBTBPSR };
static uint8_t regs[3];
static uint32_t ofs, sel;
static unsigned writes, waits, locked, violations, fail_write;
#define BSP_REG_PROTECT_OM_LPC_BATT 1
#define BSP_DELAY_UNITS_MICROSECONDS 1
static void R_BSP_RegisterProtectDisable(int x) { (void)x; ++locked; }
static void R_BSP_RegisterProtectEnable(int x) { (void)x; --locked; }
static void R_BSP_SoftwareDelay(unsigned n, int unit)
{
    (void)unit;
    if (n != 100) ++violations;
    ++waits;
}
static void write_reg(unsigned reg, unsigned value)
{
    ++writes;
    if (!locked) ++violations;
    if (reg == VBTBPCR2 && (regs[reg] & 16) &&
        ((value ^ regs[reg]) & 7)) ++violations;
    if (reg == VBTBPCR1 && (regs[VBTBPCR2] & 16)) ++violations;
    if (reg == VBTBPCR2 && (value & 16) && !waits) ++violations;
    if (writes == fail_write) return;
    if (reg == VBTBPSR) regs[reg] &= (uint8_t)(value | 0x30);
    else regs[reg] = value;
}
#define BACKUP_READ(reg) regs[reg]
#define BACKUP_WRITE(reg, value) write_reg(reg, value)
#define BACKUP_OFS() ofs
#define BACKUP_SEL() sel
#include "../../../e2studio_CPU0/src/rtc_backup.c"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static void reset(void)
{
    regs[0] = 0; regs[1] = 6; regs[2] = 0x31;
    ofs = 0xFDFFFFF0; sel = 0;
    writes = waits = locked = violations = fail_write = 0;
}
int run_tests(void)
{
    bool lost;
    rtc_backup_status_t st;
    reset();
    CHECK(rtc_backup_prepare(&lost) && lost);
    CHECK(regs[1] == 0x11 && waits == 1 && !locked && !violations);
    CHECK(regs[2] == 0x31); /* power-loss evidence not cleared prematurely */
    CHECK(rtc_backup_finish() && regs[2] == 0x30);
    rtc_backup_get_status(&st); CHECK(st.ready && st.before_control2 == 6);
    reset(); regs[1] = 0x11; regs[2] = 0x30;
    CHECK(rtc_backup_prepare(&lost) && !lost && writes == 0 && waits == 0);
    regs[1] = 6; rtc_backup_get_status(&st); CHECK(!st.ready);
    reset(); ofs |= 8; CHECK(!rtc_backup_prepare(&lost) && !writes);
    reset(); ofs |= 1; CHECK(!rtc_backup_prepare(&lost) && !writes);
    reset(); sel = 8; CHECK(!rtc_backup_prepare(&lost) && !writes);
    reset(); regs[2] = 0; CHECK(!rtc_backup_prepare(&lost));
    CHECK(waits == 1000 && !writes && !locked);
    reset(); regs[0] = 1; regs[1] = 0x12;
    CHECK(rtc_backup_prepare(&lost) && !violations && !locked);
    for (unsigned i = 1; i <= 4; ++i) {
        reset(); regs[0] = 1; regs[1] = 0x12; fail_write = i;
        CHECK(!rtc_backup_prepare(&lost) && !locked && !violations);
    }
    reset(); CHECK(rtc_backup_prepare(&lost)); fail_write = writes + 1;
    CHECK(!rtc_backup_finish() && !locked);
    return 0;
}
