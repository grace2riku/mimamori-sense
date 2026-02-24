/**
 * @file lv_port_indev.h
 * @brief Touch panel input device port layer for LVGL
 * @details
 * Provides the touch panel (GT911/FT5X06-compatible) input device
 * initialization and LVGL indev registration for the EK-RA8P1
 * Parallel Graphics Expansion Board.
 *
 * The GT911 touch controller is connected via I2C and generates
 * an external interrupt (IRQ) on touch events. This module reads
 * touch coordinates from the controller and reports them to LVGL
 * as pointer input events.
 *
 * Touch Controller:
 *   Device     : GT911 (FT5X06-compatible register interface)
 *   Interface  : I2C (slave address 0x38)
 *   IRQ        : External IRQ channel 19 (falling edge)
 *   Max points : 5 (multi-touch capable, single-touch used for LVGL)
 *
 * Operating Mode:
 *   Default: Polling mode (INDEV_EVENT_DRIVEN = 0)
 *     - touchpad_read() is called periodically by LVGL
 *     - Checks the IRQ semaphore to detect new touch events
 *     - Reads touch data via I2C when a touch is detected
 *
 *   Optional: Event-driven mode (INDEV_EVENT_DRIVEN = 1)
 *     - touchpad_read() reads touch data on every LVGL poll
 *     - Suitable when IRQ line is not available
 *
 * LCD Resolution: 1024 x 600
 *
 * FSP Configuration Required (Issue #3):
 *   The following FSP modules must be added to the lvgl_thread
 *   in configuration.xml before this module can be used:
 *
 *   1. I2C Communication Device (rm_comms_i2c):
 *      - Name: g_comms_i2c_device0
 *      - Slave address: 0x38
 *      - Callback: comms_i2c_callback
 *
 *   2. I2C Shared Bus (rm_comms_i2c):
 *      - Name: g_comms_i2c_bus0
 *      - Channel: 1
 *      - Rate: Fast mode
 *      - Blocking semaphore: Use
 *      - Bus recursive mutex: Use
 *
 *   3. I2C Master (r_iic_master):
 *      - Channel: 1
 *      - Callback: rm_comms_i2c_callback
 *
 *   4. External IRQ (r_icu):
 *      - Name: g_external_irq0
 *      - Channel: 19
 *      - Trigger: Falling edge
 *      - Callback: touch_irq_callback
 *
 *   5. FreeRTOS Objects (on lvgl_thread):
 *      - Event Group: g_i2c_event_group
 *      - Binary Semaphore: g_irq_binary_semaphore
 *
 * Reference:
 *   - reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_indev.c
 *   - reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_indev.h
 *   - reference_projects/lv_port_renesas_ek_ra8p1/ra_gen/new_thread0.h
 *
 * @note
 * This file is part of the touch panel control (F-001-5) implementation.
 */

#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include "lvgl.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * Input device operating mode
 *
 *   0: Polling mode (default) - reads touch only when IRQ semaphore is set
 *   1: Event-driven mode - reads touch on every LVGL poll
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_indev.h:29
 */
#define INDEV_EVENT_DRIVEN      (0)

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the touch panel input device
 *
 * @details Performs the following initialization:
 *   1. Creates an LVGL pointer-type input device
 *   2. Registers the touchpad_read callback for periodic polling
 *   3. Opens the I2C bus driver (IIC Master)
 *   4. Creates FreeRTOS semaphore and mutex for I2C bus synchronization
 *   5. Opens the I2C communication device (RM_COMMS_I2C)
 *   6. Opens and enables the external IRQ for touch events
 *
 * This function must be called after lv_init() and glcdc_port_init().
 * It should be called from the LVGL thread.
 *
 * Preconditions:
 *   - lv_init() has been called
 *   - glcdc_port_init() has been called (LCD reset is shared with touch)
 *   - FSP I2C and External IRQ modules are configured (Issue #3)
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_indev.c:95-149
 */
void lv_port_indev_init(void);

/**
 * Get the LVGL input device handle
 *
 * @return Pointer to the LVGL indev object, or NULL if not initialized
 */
lv_indev_t *lv_port_indev_get(void);

/**
 * NT-Shell command handler for touch panel diagnostics
 *
 * @details Registered as the "touch" command in usrcmd.c.
 *          Sub-commands:
 *            touch status - Show touch panel initialization state
 *            touch info   - Show touch controller information
 *            touch read   - Read current touch data once
 *            touch mon    - Monitor touch events for N seconds
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return CMD_OK on success, CMD_ERR_* on error
 */
int usrcmd_touch(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_INDEV_H */
