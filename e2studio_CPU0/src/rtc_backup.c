/* RA8P1 UM Rev.1.30 sections 12.2.11-13, 12.3.7, 13.1, 70.
 * Design: doc/design/issue-234.md, stage 2. No ISR/CPU1 writers. */
#ifndef BACKUP_READ
#include "bsp_api.h"
#endif
#include "rtc_backup.h"

#ifndef BACKUP_READ
#define BACKUP_READ(reg) (R_SYSTEM->reg)
#define BACKUP_WRITE(reg, value) (R_SYSTEM->reg = (value))
#define BACKUP_OFS() (*(volatile const uint32_t *)0x02C9F0C0UL)
#define BACKUP_SEL() (*(volatile const uint32_t *)0x02C9F120UL)
#endif

static rtc_backup_status_t s_backup;

static bool options_match(void)
{
    /* Secure VDSEL0=000 (2.85 V), PVDAS=0; only relevant bits. */
    return (0U == (BACKUP_SEL() & 0x0FU)) &&
           (0U == (BACKUP_OFS() & 0x0FU));
}

bool rtc_backup_prepare(bool *lost)
{
    s_backup.ofs1 = BACKUP_OFS();
    s_backup.selection = BACKUP_SEL();
    s_backup.before_control1 = BACKUP_READ(VBTBPCR1);
    s_backup.before_control2 = BACKUP_READ(VBTBPCR2);
    s_backup.before_status = BACKUP_READ(VBTBPSR);
    *lost = (s_backup.before_status & 1U) != 0U;
    s_backup.result = RTC_BACKUP_OPTIONS;
    if (!options_match()) { return false; }

    s_backup.result = RTC_BACKUP_VOLTAGE;
    for (unsigned int n = 0; !(BACKUP_READ(VBTBPSR) & 0x10U); ++n) {
        if (n == 1000U) { return false; }
        R_BSP_SoftwareDelay(100, BSP_DELAY_UNITS_MICROSECONDS);
    }
    *lost = *lost || (BACKUP_READ(VBTBPSR) & 1U) != 0U;
    if (BACKUP_READ(VBTBPCR1) == 0U && BACKUP_READ(VBTBPCR2) == 0x11U) {
        s_backup.result = RTC_BACKUP_READY;
        return true;
    }

    bool ok = false;
    s_backup.result = RTC_BACKUP_READBACK;
    R_BSP_RegisterProtectDisable(BSP_REG_PROTECT_OM_LPC_BATT);
    /* Never change voltage or BPWSWSTP while detection is enabled. */
    BACKUP_WRITE(VBTBPCR2, BACKUP_READ(VBTBPCR2) & 0x07U);
    if (BACKUP_READ(VBTBPCR2) & 0x10U) { goto done; }
    BACKUP_WRITE(VBTBPCR2, 0x01U);
    if (BACKUP_READ(VBTBPCR2) != 0x01U) { goto done; }
    BACKUP_WRITE(VBTBPCR1, 0U);
    if (BACKUP_READ(VBTBPCR1) != 0U) { goto done; }
    /* tDETWT max 20 us; request 100 us. No interrupt masking while waiting. */
    R_BSP_SoftwareDelay(100, BSP_DELAY_UNITS_MICROSECONDS);
    BACKUP_WRITE(VBTBPCR2, 0x11U);
    ok = BACKUP_READ(VBTBPCR2) == 0x11U;
done:
    R_BSP_RegisterProtectEnable(BSP_REG_PROTECT_OM_LPC_BATT);
    if (ok) { s_backup.result = RTC_BACKUP_READY; }
    return ok;
}

bool rtc_backup_finish(void)
{
    /* Preserve captured evidence. The status register's only writable flag
     * is VBPORF; reserved bits must be zero (UM 12.2.13). */
    R_BSP_RegisterProtectDisable(BSP_REG_PROTECT_OM_LPC_BATT);
    BACKUP_WRITE(VBTBPSR, 0U);
    R_BSP_RegisterProtectEnable(BSP_REG_PROTECT_OM_LPC_BATT);
    if ((BACKUP_READ(VBTBPSR) & 0x11U) != 0x10U) {
        s_backup.result = RTC_BACKUP_VOLTAGE;
        return false;
    }
    return true;
}

void rtc_backup_get_status(rtc_backup_status_t *status)
{
    *status = s_backup;
    status->ofs1 = BACKUP_OFS();
    status->selection = BACKUP_SEL();
    status->control1 = BACKUP_READ(VBTBPCR1);
    status->control2 = BACKUP_READ(VBTBPCR2);
    status->status = BACKUP_READ(VBTBPSR);
    status->ready = s_backup.result == RTC_BACKUP_READY && options_match() &&
                    status->control1 == 0U && status->control2 == 0x11U &&
                    (status->status & 0x11U) == 0x10U;
}
