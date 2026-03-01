/**
 * @file camera_framebuffer.c
 * @brief Camera frame buffer management implementation (F-002-6)
 * @details
 * Implements triple-buffer management for VIN camera capture:
 *   - ISR-safe latest-frame pointer update (from vin_port_callback)
 *   - Thread-safe latest-frame retrieval (for display/LVGL thread)
 *   - FPS measurement (1-second sliding window)
 *   - Diagnostic information for NT-Shell "camera fb" command
 *
 * The frame buffers are allocated by the FSP code generator:
 *   vin_image_buffer_1, vin_image_buffer_2, vin_image_buffer_3
 *   in SDRAM .sdram_noinit section (ra_gen/common_data.c:224-226)
 *
 * This module wraps those buffers with management state. It does NOT
 * allocate its own frame buffers.
 *
 * Thread safety model:
 *   - camera_framebuffer_set_latest() is called from ISR context (vin0_callback).
 *     It updates volatile variables without blocking.
 *   - camera_framebuffer_get_latest() is called from a single consumer thread
 *     (display/LVGL). It reads volatile variables and clears the new-frame flag.
 *   - This is a lock-free single-producer / single-consumer (SPSC) pattern.
 *     No mutex or semaphore is needed.
 *
 * Reference:
 *   - reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/display_thread_entry.c:157-160
 *     (display_next_buffer_set: the reference project's buffer hand-off)
 *   - reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:907-941
 *     (vin0_callback: frame_complete -> display_next_buffer_set)
 *
 * Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>

#include "camera_framebuffer.h"
#include "common_data.h"
#include "jlink_console.h"

#include "FreeRTOS.h"
#include "task.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size for diagnostic messages */
#define FB_PRINT_BUF_SIZE       (128)

/** FPS measurement interval in ticks (1 second) */
#define FPS_INTERVAL_TICKS      (configTICK_RATE_HZ)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/** Initialization flag */
static volatile bool s_initialized = false;

/**
 * Pointer to the most recently completed frame buffer.
 *
 * Updated from ISR context by camera_framebuffer_set_latest().
 * Read from consumer thread by camera_framebuffer_get_latest().
 *
 * This is the core of the lock-free SPSC hand-off, matching the
 * reference project's gp_camera_buffer pattern:
 *   reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/display_thread_entry.c:95,159
 */
static volatile void *s_latest_buffer = NULL;

/**
 * New-frame flag.
 *
 * Set to true by the ISR producer when a new frame completes.
 * Cleared by the consumer thread when it reads the frame.
 * Prevents the consumer from processing the same frame twice.
 */
static volatile bool s_new_frame_available = false;

/**
 * Total frame complete count since initialization.
 * Incremented from ISR context on each frame_complete callback.
 */
static volatile uint32_t s_frame_count = 0;

/**
 * FPS measurement state.
 *
 * s_fps_frame_count: frames counted in the current 1-second window
 * s_fps_last_tick:   tick count at the start of the current window
 * s_fps:            computed FPS from the most recent complete window
 */
static volatile uint32_t s_fps_frame_count = 0;
static volatile TickType_t s_fps_last_tick = 0;
static volatile uint32_t s_fps = 0;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Initialize the frame buffer management state
 *
 * @details Resets all internal counters and pointers.
 *          The actual frame buffers (vin_image_buffer_1/2/3) are
 *          FSP-generated and do not need allocation.
 *
 * Reference: e2studio_CPU0/ra_gen/common_data.c:224-226
 */
void camera_framebuffer_init(void)
{
    s_latest_buffer = NULL;
    s_new_frame_available = false;
    s_frame_count = 0;
    s_fps_frame_count = 0;
    s_fps_last_tick = xTaskGetTickCount();
    s_fps = 0;
    s_initialized = true;
}

/**
 * Set the latest completed frame buffer (called from ISR context)
 *
 * @details Updates the latest-frame pointer and increments counters.
 *          Called from vin_port_callback() when a frame_complete interrupt
 *          fires. This function must be ISR-safe (no blocking, no FreeRTOS
 *          API calls that require task context).
 *
 *          FPS measurement uses xTaskGetTickCountFromISR() which is safe
 *          in ISR context.
 *
 * Reference:
 *   reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/display_thread_entry.c:157-160
 *   reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:921
 *
 * @param p_buffer Pointer to the just-completed frame buffer
 */
void camera_framebuffer_set_latest(void *p_buffer)
{
    if (!s_initialized || (p_buffer == NULL))
    {
        return;
    }

    /* Update the latest frame pointer (atomic pointer write on Cortex-M) */
    s_latest_buffer = p_buffer;
    s_new_frame_available = true;

    /* Increment total frame count */
    s_frame_count++;

    /* FPS measurement: count frames in 1-second windows */
    s_fps_frame_count++;

    TickType_t now = xTaskGetTickCountFromISR();
    TickType_t elapsed = now - s_fps_last_tick;

    if (elapsed >= FPS_INTERVAL_TICKS)
    {
        /*
         * One second (or more) has elapsed since the window started.
         * Compute FPS for this window and start a new one.
         *
         * For accuracy: fps = frames * configTICK_RATE_HZ / elapsed_ticks
         * This accounts for windows that are slightly longer than 1 second.
         */
        s_fps = (s_fps_frame_count * (uint32_t)configTICK_RATE_HZ) / (uint32_t)elapsed;
        s_fps_frame_count = 0;
        s_fps_last_tick = now;
    }
}

/**
 * Get the latest completed frame buffer (called from display thread)
 *
 * @details Returns the latest completed frame buffer if a new frame
 *          is available since the last call. Returns NULL if no new
 *          frame has arrived.
 *
 *          The consumer thread should call this periodically (e.g., on
 *          each LVGL tick or display VSYNC) and only process the buffer
 *          when non-NULL is returned.
 *
 * @return Pointer to the latest frame buffer, or NULL if no new frame
 */
void *camera_framebuffer_get_latest(void)
{
    if (!s_initialized)
    {
        return NULL;
    }

    if (!s_new_frame_available)
    {
        return NULL;
    }

    /*
     * Read the latest pointer and clear the new-frame flag.
     * Order matters: read pointer first, then clear flag.
     * A memory barrier ensures the compiler does not reorder these.
     */
    void *p_buf = (void *)s_latest_buffer;
    s_new_frame_available = false;
    __DMB();    /* Data Memory Barrier: ensure flag write is visible */

    return p_buf;
}

/**
 * Get the three frame buffer addresses
 *
 * @details Returns the addresses of the FSP-generated VIN capture buffers.
 *          These are vin_image_buffer_1/2/3 declared in ra_gen/common_data.h:67-69
 *          and allocated in ra_gen/common_data.c:224-226.
 *
 * @param pp_buf1 Output: pointer to buffer 1
 * @param pp_buf2 Output: pointer to buffer 2
 * @param pp_buf3 Output: pointer to buffer 3
 */
void camera_framebuffer_get_buffers(void **pp_buf1, void **pp_buf2, void **pp_buf3)
{
    if (pp_buf1 != NULL)
    {
        *pp_buf1 = (void *)vin_image_buffer_1;
    }
    if (pp_buf2 != NULL)
    {
        *pp_buf2 = (void *)vin_image_buffer_2;
    }
    if (pp_buf3 != NULL)
    {
        *pp_buf3 = (void *)vin_image_buffer_3;
    }
}

/**
 * Get the total frame complete count
 */
uint32_t camera_framebuffer_get_frame_count(void)
{
    return s_frame_count;
}

/**
 * Get the measured FPS
 *
 * @details Returns the FPS computed from the most recent completed
 *          1-second measurement window.
 */
uint32_t camera_framebuffer_get_fps(void)
{
    return s_fps;
}

/**
 * Get comprehensive frame buffer status information
 *
 * @details Fills the info structure with current state. Accesses volatile
 *          variables; a brief interrupt disable ensures consistency.
 */
void camera_framebuffer_get_info(camera_framebuffer_info_t *info)
{
    if (info == NULL)
    {
        return;
    }

    __disable_irq();
    info->initialized        = s_initialized;
    info->frame_count        = s_frame_count;
    info->fps                = s_fps;
    info->p_latest           = (void *)s_latest_buffer;
    info->new_frame_available = s_new_frame_available;
    __enable_irq();

    /* Buffer addresses from FSP-generated variables */
    info->buffer_addr[0] = (uint32_t)(uintptr_t)vin_image_buffer_1;
    info->buffer_addr[1] = (uint32_t)(uintptr_t)vin_image_buffer_2;
    info->buffer_addr[2] = (uint32_t)(uintptr_t)vin_image_buffer_3;
}

/**
 * NT-Shell "camera fb" sub-command handler
 *
 * @details Displays:
 *   - Initialization state
 *   - Frame buffer addresses and sizes
 *   - Frame count and FPS
 *   - Latest frame pointer and new-frame flag
 *   - Memory layout summary
 */
void camera_framebuffer_cmd_status(void)
{
    char buf[FB_PRINT_BUF_SIZE];
    camera_framebuffer_info_t info;

    camera_framebuffer_get_info(&info);

    /* Module status */
    print_to_console("[Camera Frame Buffer (F-002-6)]\r\n");

    snprintf(buf, sizeof(buf), "  Initialized   : %s\r\n",
             info.initialized ? "Yes" : "No");
    print_to_console(buf);

    /* Frame buffer layout */
    print_to_console("[Frame Buffers (SDRAM .sdram_noinit)]\r\n");

    snprintf(buf, sizeof(buf), "  Resolution    : %u x %u x %u bpp\r\n",
             (unsigned int)CAMERA_FRAME_WIDTH,
             (unsigned int)CAMERA_FRAME_HEIGHT,
             (unsigned int)CAMERA_FRAME_BPP);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Frame Size    : %lu bytes (%lu KB)\r\n",
             (unsigned long)CAMERA_FRAME_SIZE,
             (unsigned long)(CAMERA_FRAME_SIZE / 1024));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Buffer Count  : %u (triple buffering)\r\n",
             (unsigned int)CAMERA_FRAME_NUM_BUFFERS);
    print_to_console(buf);

    for (uint32_t i = 0; i < CAMERA_FRAME_NUM_BUFFERS; i++)
    {
        snprintf(buf, sizeof(buf), "  Buffer[%lu]     : 0x%08lX - 0x%08lX\r\n",
                 (unsigned long)i,
                 (unsigned long)info.buffer_addr[i],
                 (unsigned long)(info.buffer_addr[i] + CAMERA_FRAME_SIZE - 1));
        print_to_console(buf);
    }

    snprintf(buf, sizeof(buf), "  Total Memory  : %lu bytes (%lu KB)\r\n",
             (unsigned long)(CAMERA_FRAME_SIZE * CAMERA_FRAME_NUM_BUFFERS),
             (unsigned long)((CAMERA_FRAME_SIZE * CAMERA_FRAME_NUM_BUFFERS) / 1024));
    print_to_console(buf);

    /* Capture state */
    print_to_console("[Capture State]\r\n");

    snprintf(buf, sizeof(buf), "  Frame Count   : %lu\r\n",
             (unsigned long)info.frame_count);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  FPS           : %lu\r\n",
             (unsigned long)info.fps);
    print_to_console(buf);

    if (info.p_latest != NULL)
    {
        snprintf(buf, sizeof(buf), "  Latest Frame  : 0x%08lX\r\n",
                 (unsigned long)(uintptr_t)info.p_latest);
    }
    else
    {
        snprintf(buf, sizeof(buf), "  Latest Frame  : (none)\r\n");
    }
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  New Frame     : %s\r\n",
             info.new_frame_available ? "Yes (not yet consumed)" : "No");
    print_to_console(buf);

    /* Memory section info */
    print_to_console("[Memory Layout]\r\n");
    print_to_console("  Section       : .sdram_noinit (no cache, DMA safe)\r\n");
    print_to_console("  Alignment     : 128 bytes (FSP-generated)\r\n");
    print_to_console("  Managed by    : FSP VIN driver (round-robin)\r\n");
}
