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

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_THREAD_API_H */
