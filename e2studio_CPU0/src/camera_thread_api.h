/**
 * @file camera_thread_api.h
 * @brief Camera thread public API for inter-thread queries (F-002-5)
 * @details
 * Provides thread-safe query functions for camera thread state.
 * These functions can be called from other threads (e.g., ntshell_thread
 * for "camera thread" diagnostics, or lvgl_thread for display updates).
 *
 * Note: The camera_thread_entry() declaration is in ra_gen/camera_thread.h
 * (auto-generated, do not edit). This header provides additional public
 * functions implemented in camera_thread_entry.c.
 */

#ifndef CAMERA_THREAD_API_H
#define CAMERA_THREAD_API_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdbool.h>

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Check if camera thread has completed initialization and capture is running
 *
 * @retval true  Camera initialized and capture in progress
 * @retval false Camera not yet initialized or initialization failed
 */
bool camera_thread_is_initialized(void);

/**
 * Check if camera thread encountered an initialization error
 *
 * @retval true  Camera initialization failed
 * @retval false No error (may still be initializing)
 */
bool camera_thread_has_error(void);

/**
 * Check if camera thread has completed all I2C bus operations
 *
 * @details
 * The camera thread and touch panel driver both use IIC channel 1.
 * If both R_IIC_MASTER_Open() are called on the same channel with different
 * control structures, the second open overwrites the IRQ context via
 * R_BSP_IrqCfgEnable(), causing I2C callbacks to be routed to the wrong
 * handler. This results in I2C transfer timeouts and silent register
 * write failures.
 *
 * To prevent this conflict, the touch panel driver (lv_port_indev_init)
 * must wait until the camera thread has finished all its I2C operations
 * before opening the I2C bus.
 *
 * After this flag is set, the camera thread has closed its I2C master
 * instance, freeing IIC1 for exclusive use by the touch panel.
 *
 * Reference: Issue #93 root cause analysis - IIC1 channel conflict between
 *            g_i2c_master_camera_ctrl and g_i2c_master0_ctrl
 *
 * @retval true  Camera I2C operations complete, IIC1 is released
 * @retval false Camera I2C operations still in progress
 */
bool camera_thread_i2c_done(void);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_THREAD_API_H */
