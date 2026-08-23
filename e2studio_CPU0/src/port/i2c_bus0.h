/**
 * @file i2c_bus0.h
 * @brief IIC1 (SYS_I2C) shared-bus ownership and mutual exclusion (Issue #46)
 * @details
 * Centralises the open responsibility and the mutual exclusion of the
 * `g_comms_i2c_bus0` shared I2C bus (lower level driver `g_i2c_master0`,
 * IIC channel 1 = SYS_I2C, P511/P512).
 *
 * Why this module exists
 * ----------------------
 * Up to Issue #45 the touch panel was the ONLY `rm_comms_i2c` device on the
 * bus, so no arbitration was needed and `lv_port_indev_init()` opened
 * `g_i2c_master0` on its own (see the historical comment block in
 * `src/port/lv_port_indev.c`). Issue #46 adds the DA7212 audio CODEC
 * (`g_comms_i2c_codec`, slave 0x1A) on the SAME bus
 * (`ra_gen/hal_data.c:14` -> `.p_extend = &g_comms_i2c_bus0_extended_cfg`),
 * which breaks that assumption for two independent reasons:
 *
 *  1. `rm_comms_i2c` keeps ONE shared "current device" pointer per bus
 *     (`p_current_ctrl`, `ra_gen/common_data.c:540`). Every transfer calls
 *     `rm_comms_i2c_bus_reconfigure()`
 *     (`ra/fsp/src/rm_comms_i2c/rm_comms_i2c_driver_ra.c:345-380`) which,
 *     when the requesting device differs from `p_current_ctrl`, rewrites the
 *     IIC slave address AND the lower level driver callback. If that happens
 *     while another device's transfer is still in flight, the in-flight
 *     transfer completes into the wrong callback / wrong slave address.
 *  2. The middleware's own bus mutex is NOT available in this build. The
 *     members `p_bus_recursive_mutex` / `p_blocking_semaphore` only exist
 *     inside `#if BSP_CFG_RTOS` (`ra/fsp/inc/instances/rm_comms_i2c.h:90-93`)
 *     and `BSP_CFG_RTOS` resolves to 0 here
 *     (`ra_cfg/fsp_cfg/bsp/bsp_cfg.h:13-22`). This is a COMPILE-TIME fact:
 *     the members do not exist, so "assign a non-NULL mutex" is not an option.
 *     Arbitration therefore has to live in the application.
 *
 * On top of that, IIC1 is also used by the camera through a SECOND, separate
 * `R_IIC_MASTER` instance (`g_i2c_master_camera`). Opening two
 * `R_IIC_MASTER` control blocks on the same channel makes the later
 * `R_BSP_IrqCfgEnable()` overwrite the IRQ context so the earlier owner stops
 * receiving completion callbacks (Issue #93 root cause;
 * `ra/fsp/src/r_iic_master/r_iic_master.c:784-787`). The camera therefore
 * signals `camera_thread_i2c_done()` once it has closed its instance, and
 * `i2c_bus0_open_once()` waits for that before opening `g_i2c_master0`.
 *
 * Usage contract
 * --------------
 *  - `i2c_bus0_sync_init()` MUST run before any task that touches the bus is
 *    started. It is called from `usermain()` before the first `tk_cre_tsk()`
 *    (`src/usermain.c`), i.e. from the uT-Kernel initial task
 *    (`itskpri = 1`, `mtk3_bsp2/mtkernel/include/sys/inittask.h:26`) which
 *    runs to completion before any created task can be dispatched.
 *    It is idempotent and is also invoked lazily by `i2c_bus0_lock()` /
 *    `i2c_bus0_open_once()` as a safety net.
 *  - Every bus user calls `i2c_bus0_open_once()` once, then wraps EACH
 *    transfer (submit + completion wait) in
 *    `i2c_bus0_lock()` / `i2c_bus0_unlock()`.
 *  - Task context only. Never call these from an ISR.
 */

#ifndef I2C_BUS0_H
#define I2C_BUS0_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdbool.h>

#include "hal_data.h"
#include "common_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Default timeout (ms) used by i2c_bus0_lock(). Long enough to cover the
 *  slowest single transfer plus scheduling jitter, short enough that a stuck
 *  owner is reported instead of hanging the caller forever. */
#define I2C_BUS0_LOCK_TIMEOUT_MS    (1000)

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Create the uT-Kernel mutex that guards the IIC1 shared bus.
 *
 * Idempotent. Must be called before any bus user task is started; see the
 * file header for the ordering argument.
 *
 * @retval FSP_SUCCESS          Mutex available.
 * @retval FSP_ERR_INTERNAL     tk_cre_mtx() failed.
 */
fsp_err_t i2c_bus0_sync_init(void);

/**
 * Open the lower level I2C master (`g_i2c_master0`) exactly once.
 *
 * Blocks until the camera thread has released IIC1
 * (`camera_thread_i2c_done()`), then opens the driver and switches the
 * `rm_comms_i2c` bus to callback mode. Safe to call from several tasks; only
 * the first call performs the open.
 *
 * @retval FSP_SUCCESS          Bus is open and usable.
 * @retval FSP_ERR_INTERNAL     Mutex not available.
 * @retval FSP_ERR_TIMEOUT      Could not acquire the bus mutex.
 * @return                      Any error returned by `i2c_master_api_t::open`.
 */
fsp_err_t i2c_bus0_open_once(void);

/**
 * @retval true   `g_i2c_master0` has been opened by i2c_bus0_open_once().
 * @retval false  Not open yet.
 */
bool i2c_bus0_is_ready(void);

/**
 * Acquire exclusive use of the IIC1 shared bus.
 *
 * Must wrap the whole transfer, i.e. from the `RM_COMMS_I2C_*` submit call
 * until the completion callback has been observed by the caller.
 *
 * @retval FSP_SUCCESS          Bus acquired.
 * @retval FSP_ERR_INTERNAL     Mutex not available / lock failed.
 * @retval FSP_ERR_TIMEOUT      Timed out after I2C_BUS0_LOCK_TIMEOUT_MS.
 */
fsp_err_t i2c_bus0_lock(void);

/**
 * Release the IIC1 shared bus acquired with i2c_bus0_lock().
 *
 * @retval FSP_SUCCESS          Released.
 * @retval FSP_ERR_INTERNAL     Mutex not available / unlock failed.
 */
fsp_err_t i2c_bus0_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* I2C_BUS0_H */
