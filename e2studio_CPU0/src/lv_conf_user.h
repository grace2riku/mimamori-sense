/* See ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h
 * to see default overrides
 */

#ifndef LV_CONF_USER_H_
#define LV_CONF_USER_H_

/** Possible values
 * - LV_STDLIB_BUILTIN:     LVGL's built in implementation
 * - LV_STDLIB_CLIB:        Standard C functions, like malloc, strlen, etc
 * - LV_STDLIB_MICROPYTHON: MicroPython implementation
 * - LV_STDLIB_RTTHREAD:    RT-Thread implementation
 * - LV_STDLIB_CUSTOM:      Implement the functions externally
 */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    /** Size of memory available for `lv_malloc()` in bytes (>= 2kB) */
    #define LV_MEM_SIZE (0x20000)               /**< [bytes] 128KB (matches FSP default) */
#endif  /*LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN*/

#define LV_DEF_REFR_PERIOD  16      /**< [ms] */

/** The target buffer size for simple layer chunks. */
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE    (256 * 1024)    /**< [bytes]*/

/* Limit the max allocated memory for simple and transformed layers. */
#define LV_DRAW_LAYER_MAX_MEMORY (256 * 1024)  /**< [bytes]*/

#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 0
    #define LV_LOG_USE_TIMESTAMP 1
    #define LV_LOG_USE_FILE_LINE 1
#endif  /*LV_USE_LOG*/

#define LV_USE_ASSERT_NULL          0
#define LV_USE_ASSERT_MALLOC        0
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

#define LV_USE_XML 0
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS    0

/** 1: Enable system monitor component */
#define LV_USE_SYSMON   1
#if LV_USE_SYSMON
    #define LV_SYSMON_GET_IDLE lv_timer_get_idle

    #define LV_USE_PERF_MONITOR 1
    #if LV_USE_PERF_MONITOR
        #define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT
        #define LV_USE_PERF_MONITOR_LOG_MODE 0
    #endif

    #define LV_USE_MEM_MONITOR 0
#endif /*LV_USE_SYSMON*/

#endif /* LV_CONF_USER_H_ */
