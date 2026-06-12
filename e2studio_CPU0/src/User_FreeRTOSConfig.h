#ifndef USER_FREERTOSCONFIG_H_
#define USER_FREERTOSCONFIG_H_

/*
 * R-006 (Issue #156): the LVGL task-switch trace hooks were REMOVED.
 *
 * Previous content (Issue #138):
 *   void lv_freertos_task_switch_in(const char * name);
 *   void lv_freertos_task_switch_out(void);
 *   #define traceTASK_SWITCHED_IN()   lv_freertos_task_switch_in(pxCurrentTCB->pcTaskName);
 *   #define traceTASK_SWITCHED_OUT()  lv_freertos_task_switch_out();
 *
 * Reason: with the LVGL OSAL switched to uT-Kernel 3.0 (LV_USE_OS =
 * LV_OS_CUSTOM in src/lv_conf_user.h), ra/lvgl/lvgl/src/osal/lv_freertos.c
 * compiles to nothing, so lv_freertos_task_switch_in/out no longer exist.
 * FreeRTOS's tasks.c remains linked through the (unreached) main() reference
 * chain, and its trace macro expansions would reference the now-undefined
 * symbols, breaking the link. The hooks were FreeRTOS-runtime-only CPU usage
 * instrumentation and have no role under boot method A (the FreeRTOS
 * scheduler never runs). The SYSMON idle percentage is now provided by
 * lv_os_get_idle_percent() in src/lv_os_mtkernel.c.
 *
 * This file itself must remain: the FSP FreeRTOS configuration references it
 * as the custom FreeRTOSConfig include (configuration.xml is not changed by
 * the migration policy).
 *
 * Reference: doc/migration/r006a-lvgl-osal-spike.md 2.5
 */

#endif /* USER_FREERTOSCONFIG_H_ */
