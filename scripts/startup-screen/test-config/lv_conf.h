/* Test only: real LVGL timers/widgets without board drivers or rendering. */
#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_USE_OS 0
#define LV_USE_DRAW_DAVE2D 0
#define LV_USE_LOG 0
#define LV_USE_SYSMON 1
#define LV_USE_PERF_MONITOR 1
#define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT
#define LV_USE_MEM_MONITOR 0
#define LV_MEM_SIZE (256 * 1024U)
#define LV_USE_ASSERT_NULL 0
#define LV_USE_ASSERT_MALLOC 0
#endif
