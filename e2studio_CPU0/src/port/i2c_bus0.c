/**
 * @file i2c_bus0.c
 * @brief IIC1 (SYS_I2C) shared-bus ownership and mutual exclusion (Issue #46)
 * @details See i2c_bus0.h for the full rationale and the usage contract.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdbool.h>

#include <tk/tkernel.h>

#include "i2c_bus0.h"
#include "camera_thread_api.h"

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/**
 * uT-Kernel mutex guarding the IIC1 shared bus.
 *
 * TA_INHERIT (priority inheritance) is used, matching the mutex attribute
 * already used elsewhere in this project (src/lv_os_mtkernel.c:176-180):
 * the LVGL task (itskpri 14) and the audio task both take this mutex, so a
 * low priority owner must not block a higher priority waiter unboundedly.
 *
 * 0 = not yet created.
 */
static ID s_bus0_mtxid = 0;

/** true once g_i2c_master0 has been opened. Written only while the mutex is
 *  held; read lock-free by i2c_bus0_is_ready() (single word, no tearing). */
static volatile bool s_bus0_open = false;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Create the uT-Kernel mutex that guards the IIC1 shared bus.
 *
 * Idempotent. The re-check is done with dispatching disabled so that two
 * tasks racing on the lazy path cannot both create a mutex (same pattern as
 * check_mutex_init() in src/lv_os_mtkernel.c:164-192). tk_dis_dsp() requires
 * task context, which is satisfied both from usermain() (uT-Kernel initial
 * task) and from any application task.
 */
fsp_err_t i2c_bus0_sync_init(void)
{
    if (s_bus0_mtxid > 0)
    {
        return FSP_SUCCESS;
    }

    if (E_OK != tk_dis_dsp())
    {
        return FSP_ERR_INTERNAL;
    }

    if (s_bus0_mtxid <= 0)          /* re-check under dispatch disable */
    {
        T_CMTX cmtx = {
            .exinf  = NULL,
            .mtxatr = TA_INHERIT,
        };

        ID mtxid = tk_cre_mtx(&cmtx);
        if (mtxid > E_OK)
        {
            s_bus0_mtxid = mtxid;
        }
    }

    (void)tk_ena_dsp();

    return (s_bus0_mtxid > 0) ? FSP_SUCCESS : FSP_ERR_INTERNAL;
}

/**
 * Acquire exclusive use of the IIC1 shared bus.
 */
fsp_err_t i2c_bus0_lock(void)
{
    fsp_err_t err = i2c_bus0_sync_init();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    ER ercd = tk_loc_mtx(s_bus0_mtxid, (TMO)I2C_BUS0_LOCK_TIMEOUT_MS);
    if (E_TMOUT == ercd)
    {
        return FSP_ERR_TIMEOUT;
    }
    if (E_OK != ercd)
    {
        return FSP_ERR_INTERNAL;
    }

    return FSP_SUCCESS;
}

/**
 * Release the IIC1 shared bus.
 */
fsp_err_t i2c_bus0_unlock(void)
{
    if (s_bus0_mtxid <= 0)
    {
        return FSP_ERR_INTERNAL;
    }

    return (E_OK == tk_unl_mtx(s_bus0_mtxid)) ? FSP_SUCCESS : FSP_ERR_INTERNAL;
}

/**
 * Open the lower level I2C master (g_i2c_master0) exactly once.
 */
fsp_err_t i2c_bus0_open_once(void)
{
    fsp_err_t err;

    if (s_bus0_open)
    {
        return FSP_SUCCESS;
    }

    /*
     * Wait for the camera thread to release IIC1.
     *
     * Both g_i2c_master_camera_ctrl (camera) and g_i2c_master0_ctrl (this
     * bus) use IIC channel 1. R_IIC_MASTER_Open() installs the channel IRQ
     * context via R_BSP_IrqCfgEnable()
     * (ra/fsp/src/r_iic_master/r_iic_master.c:784-787), so opening the second
     * instance while the first is still open silently steals the completion
     * callbacks of the first (Issue #93 root cause). The camera closes its
     * instance and then sets the flag queried by camera_thread_i2c_done()
     * (src/camera_thread_entry.c:731). The flag is also set when camera init
     * FAILS, so this loop cannot block forever on a broken camera.
     *
     * This preserves the behaviour that lv_port_indev_init() had before
     * Issue #46 - the wait simply moved here.
     *
     * CRITICAL - this wait MUST stay OUTSIDE the bus mutex.
     * The camera needs several seconds to get through GreenPAK, the board
     * switch, VIN and the OV5640 before it sets the flag. Holding the mutex
     * across that wait makes every other i2c_bus0_lock() caller fail with
     * FSP_ERR_TIMEOUT after I2C_BUS0_LOCK_TIMEOUT_MS (1000 ms): lvgl_task
     * (itskpri 14) reaches this function first, so audio_task (itskpri 16)
     * timed out here and da7212_init() never issued a single I2C transfer
     * (observed as "audio: init failed (err=0x14)" during bring-up).
     *
     * Reading the flag unlocked is safe: s_camera_i2c_done is a one-way
     * volatile bool that is only ever set true (src/camera_thread_entry.c:559
     * on success, :612 on camera-init failure) and never cleared, so there is
     * no false-true window. The s_bus0_open re-check below still runs under
     * the mutex, which is what actually prevents a double open.
     */
    while (!camera_thread_i2c_done())
    {
        tk_dly_tsk(10);
    }

    err = i2c_bus0_lock();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    if (s_bus0_open)                /* another task won the race */
    {
        (void)i2c_bus0_unlock();
        return FSP_SUCCESS;
    }

    /*
     * Open the lower level I2C master through the shared bus extended
     * configuration, exactly as lv_port_indev_init() used to do.
     * g_comms_i2c_bus0_extended_cfg.p_driver_instance is &g_i2c_master0
     * (ra_gen/common_data.c:532-534).
     */
    {
        i2c_master_instance_t * p_driver_instance =
            (i2c_master_instance_t *)g_comms_i2c_bus0_extended_cfg.p_driver_instance;

        err = p_driver_instance->p_api->open(p_driver_instance->p_ctrl,
                                             p_driver_instance->p_cfg);
        if (FSP_SUCCESS != err)
        {
            (void)i2c_bus0_unlock();
            return err;
        }
    }

    /*
     * Switch the rm_comms_i2c bus to CALLBACK MODE.
     *
     * Kept byte-for-byte equivalent to the code this replaces in
     * lv_port_indev.c: g_comms_i2c_bus0_extended_cfg is a NON-const object
     * (ra_gen/common_data.c:532) so these are runtime assignments, and the
     * whole block is compiled out when BSP_CFG_RTOS == 0 because the struct
     * members only exist inside `#if BSP_CFG_RTOS`
     * (ra/fsp/inc/instances/rm_comms_i2c.h:90-93). With BSP_CFG_RTOS == 0 the
     * driver is permanently in callback mode anyway, so the behaviour is the
     * same under either setting.
     */
#if BSP_CFG_RTOS
    g_comms_i2c_bus0_extended_cfg.p_blocking_semaphore  = NULL;
    g_comms_i2c_bus0_extended_cfg.p_bus_recursive_mutex = NULL;
#endif

    s_bus0_open = true;

    (void)i2c_bus0_unlock();

    return FSP_SUCCESS;
}

/**
 * Query whether the shared bus has been opened.
 */
bool i2c_bus0_is_ready(void)
{
    return s_bus0_open;
}

/**
 * Abort an in-flight transfer on the shared IIC1 bus.
 */
fsp_err_t i2c_bus0_abort_transfer(void)
{
    if (!s_bus0_open)
    {
        return FSP_ERR_NOT_OPEN;
    }

    i2c_master_instance_t * p_driver_instance =
        (i2c_master_instance_t *)g_comms_i2c_bus0_extended_cfg.p_driver_instance;

    if (NULL == p_driver_instance->p_api->abort)
    {
        return FSP_ERR_UNSUPPORTED;
    }

    return p_driver_instance->p_api->abort(p_driver_instance->p_ctrl);
}
