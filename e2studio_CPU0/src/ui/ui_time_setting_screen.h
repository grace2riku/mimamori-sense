/**
 * @file ui_time_setting_screen.h
 * @brief Touch date/time settings (Issue #214), LVGL task only.
 *
 * +--------------------------------------------------------------+
 * |                       Set Date & Time                        |
 * |    Year       Month       Day       Hour       Min           |
 * |  [2026]       [09]        [24]       [12]       [34]          |
 * |                     <result/status>                          |
 * |               [ OK ]                [ Cancel / Back ]       |
 * +--------------------------------------------------------------+
 *
 * Year range is 2000..2099; days follow the selected month/leap year.
 * Seconds are set to zero. Cache unavailable: default 2026-01-01 00:00
 * with an explicit warning. Cancel before submission never writes RTC.
 *
 * RTC calls run on time_cache_task. Pending requests block editing and
 * duplicate submission. After 3 s, Back leaves without cancelling; monitoring
 * continues while hidden and when reopened. Completion never steals another
 * screen. Successful writes refresh the datetime label from the readback cache.
 * A readback failure is reported separately from a write failure.
 *
 * FSP register waits can be unbounded; a higher-priority busy-waiting RTC task
 * can prevent the LVGL timer from running. See doc/design/issue-214.md.
 */
#ifndef UI_TIME_SETTING_SCREEN_H
#define UI_TIME_SETTING_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Open/reuse the screen from lvgl_task. Reopening while pending preserves the
 * submitted values and request. Calling while active is a no-op. Partial
 * construction is cleaned up on failure and the original screen remains active.
 */
void ui_time_setting_screen_open(void);

#ifdef __cplusplus
}
#endif
#endif /* UI_TIME_SETTING_SCREEN_H */
