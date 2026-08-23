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

/**
 * true while i2c_bus0_suspend() is holding the lock on behalf of its caller.
 *
 * Set only when the lock was actually acquired, so a FAILED suspend leaves it
 * false and the paired resume becomes a no-op instead of unlocking a mutex
 * this task never took.
 */
static volatile bool s_bus0_suspended = false;

/**
 * true when the suspend that is currently in effect actually closed the bus,
 * i.e. resume must re-open it.
 *
 * The bus may not have been opened yet when a camera diagnostic runs (the
 * shell is up long before touch/audio get there). That case still has to
 * RESERVE the bus for the duration - otherwise i2c_bus0_open_once() could run
 * concurrently and steal the camera's IIC1 IRQ context - but resume must not
 * open the bus, because that would bypass the camera_thread_i2c_done()
 * ordering that i2c_bus0_open_once() enforces.
 */
static volatile bool s_bus0_reopen_on_resume = false;

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
/**
 * Wait for the camera hand-off, then take the bus lock (retrying on timeout).
 *
 * @note On FSP_SUCCESS the lock is HELD and the caller must release it.
 */
static fsp_err_t bus0_acquire_for_open(void)
{
    fsp_err_t err;

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

    /*
     * Retry the lock instead of failing on the first timeout.
     *
     * This is a one-shot INITIALISATION path and it may legitimately have to
     * wait: i2c_bus0_suspend() now holds the lock for a whole camera
     * diagnostic, which can easily outlast I2C_BUS0_LOCK_TIMEOUT_MS if one
     * OV5640 access burns its own timeout. Treating that expected hold as
     * fatal is what breaks the callers - lv_port_indev_init() would fail its
     * open and audio_init() would go to AUDIO_STATE_ERROR, i.e. an early
     * `camera diag` from the shell would permanently disable touch and audio.
     *
     * Bounded rather than unbounded so a genuine deadlock still surfaces as an
     * error instead of hanging the caller forever. The camera hand-over above
     * is the only long holder and is nowhere near this budget.
     */
    for (uint32_t attempt = 0; ; attempt++)
    {
        err = i2c_bus0_lock();
        if (FSP_SUCCESS == err)
        {
            break;
        }

        if ((FSP_ERR_TIMEOUT != err) || ((attempt + 1U) >= I2C_BUS0_OPEN_LOCK_ATTEMPTS))
        {
            return err;
        }
    }

    return FSP_SUCCESS;                 /* lock held - caller releases it */
}

/**
 * Open g_i2c_master0 unless it is already open.
 *
 * @warning THE BUS LOCK MUST BE HELD. The caller releases it either way.
 */
static fsp_err_t bus0_open_master_locked(void)
{
    fsp_err_t err;

    if (s_bus0_open)                /* another task won the race */
    {
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

    return FSP_SUCCESS;
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

    err = bus0_acquire_for_open();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = bus0_open_master_locked();

    (void)i2c_bus0_unlock();

    return err;
}

/**
 * Open the shared master AND an rm_comms_i2c device under a single lock.
 */
fsp_err_t i2c_bus0_open_device(rm_comms_ctrl_t * const p_ctrl,
                               rm_comms_cfg_t const * const p_cfg)
{
    fsp_err_t err;

    /* No s_bus0_open fast path: the DEVICE still has to be opened even when
     * the master is already up. */
    err = bus0_acquire_for_open();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = bus0_open_master_locked();
    if (FSP_SUCCESS == err)
    {
        /*
         * Same lock as the master open above - that is the whole point.
         * RM_COMMS_I2C_Open() validates that the lower level driver is open
         * (rm_comms_i2c.c calls rm_comms_i2c_bus_status_check() and returns
         * FSP_ERR_COMMS_BUS_NOT_OPEN otherwise). If the lock were dropped in
         * between, a camera diagnostic could slip in through
         * i2c_bus0_suspend(), close the master, and make this open fail.
         */
        err = RM_COMMS_I2C_Open(p_ctrl, p_cfg);
    }

    (void)i2c_bus0_unlock();

    return err;
}

/**
 * Query whether the shared bus has been opened.
 */
bool i2c_bus0_is_ready(void)
{
    return s_bus0_open;
}

/**
 * Hand IIC1 over to a different i2c_master instance (the camera's).
 */
fsp_err_t i2c_bus0_suspend(void)
{
    fsp_err_t err;
    bool      was_open;

    /*
     * Take the lock FIRST and unconditionally - held until i2c_bus0_resume().
     *
     * This is required even when the bus is not open yet. The shell is running
     * long before touch/audio call i2c_bus0_open_once(), so a diagnostic can
     * start, open the camera master and then block on an OV5640 transfer; if
     * the bus were left unreserved, another task could pass the
     * camera_thread_i2c_done() wait and open the shared master right then,
     * replacing the camera's IIC1 IRQ context. The diagnostic's close would
     * then disable the shared master's interrupts with nothing to restore
     * them. i2c_bus0_open_once() takes this same lock before opening, so
     * holding it here blocks that race.
     */
    err = i2c_bus0_lock();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    was_open = s_bus0_open;

    if (was_open)
    {
        i2c_master_instance_t * p_driver_instance =
            (i2c_master_instance_t *)g_comms_i2c_bus0_extended_cfg.p_driver_instance;

        err = p_driver_instance->p_api->close(p_driver_instance->p_ctrl);
        if (FSP_SUCCESS != err)
        {
            (void)i2c_bus0_unlock();
            return err;
        }

        s_bus0_open = false;
    }

    /* Only re-open on resume if this suspend is what closed it. */
    s_bus0_reopen_on_resume = was_open;
    s_bus0_suspended        = true;

    return FSP_SUCCESS;
}

/**
 * Take IIC1 back after i2c_bus0_suspend().
 */
fsp_err_t i2c_bus0_resume(void)
{
    fsp_err_t err = FSP_SUCCESS;

    if (!s_bus0_suspended)
    {
        /* Nothing to undo: the paired suspend() FAILED, so it holds no lock
         * and closed nothing. Unlocking here would target a mutex this task
         * never acquired. */
        return FSP_SUCCESS;
    }

    if (s_bus0_reopen_on_resume)
    {
        i2c_master_instance_t * p_driver_instance =
            (i2c_master_instance_t *)g_comms_i2c_bus0_extended_cfg.p_driver_instance;

        err = p_driver_instance->p_api->open(p_driver_instance->p_ctrl,
                                             p_driver_instance->p_cfg);
        if (FSP_SUCCESS == err)
        {
            s_bus0_open = true;
        }
    }

    /*
     * Forget which device the bus was last programmed for.
     *
     * The re-open above reset the hardware slave address and the lower level
     * callback to the cfg defaults, but rm_comms_i2c skips reprogramming them
     * when the requesting device still matches its cached p_current_ctrl
     * (rm_comms_i2c_driver_ra.c rm_comms_i2c_bus_reconfigure). Without this
     * the first transfer after a resume would run against the wrong slave
     * address. g_comms_i2c_bus0_extended_cfg is non-const (ra_gen/common_data.c),
     * so this is a runtime assignment, not an edit of generated code.
     */
    g_comms_i2c_bus0_extended_cfg.p_current_ctrl = NULL;

    /* Cleared even if the re-open failed, so the lock is always released and a
     * later i2c_bus0_open_once() can retry the open. */
    s_bus0_reopen_on_resume = false;
    s_bus0_suspended        = false;

    (void)i2c_bus0_unlock();

    return err;
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
