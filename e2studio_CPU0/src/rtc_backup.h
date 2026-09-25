#ifndef RTC_BACKUP_H
#define RTC_BACKUP_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RTC_BACKUP_NOT_CHECKED, RTC_BACKUP_READY, RTC_BACKUP_OPTIONS,
    RTC_BACKUP_VOLTAGE, RTC_BACKUP_READBACK
} rtc_backup_result_t;
typedef struct {
    uint32_t ofs1, selection;
    uint8_t before_control1, before_control2, before_status;
    uint8_t control1, control2, status;
    rtc_backup_result_t result;
    bool ready;
} rtc_backup_status_t;

/* Called only by time_ctrl_init in ntshell_task under the RTC resource mutex.
 * finish acknowledges power loss only after RTC initialization succeeds. */
bool rtc_backup_prepare(bool *lost);
bool rtc_backup_finish(void);
/* Diagnostic reader: ntshell_task only; does not write hardware. */
void rtc_backup_get_status(rtc_backup_status_t *status);
#endif
