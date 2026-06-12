/**
 * @file led_ctrl.h
 * @brief LED control abstraction layer for EK-RA8P1 board
 * @details
 * Provides a hardware-abstracted interface for controlling the user LEDs
 * on the EK-RA8P1 evaluation board. Supports on/off/toggle/blink operations.
 *
 * EK-RA8P1 User LED Mapping:
 *   LED0 (LED1): Blue  - BSP_IO_PORT_06_PIN_00 (P600)
 *   LED1 (LED2): Green - BSP_IO_PORT_03_PIN_03 (P303)
 *   LED2 (LED3): Red   - BSP_IO_PORT_10_PIN_07 (PA07)
 *
 * LEDs are active-high (HIGH = ON, LOW = OFF).
 *
 * @note
 * This file is part of the LED control command (S-011) implementation.
 */

#ifndef LED_CTRL_H
#define LED_CTRL_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Number of user LEDs on the EK-RA8P1 board */
#define LED_COUNT           (3)

/** Default blink interval in milliseconds */
#define LED_BLINK_DEFAULT_MS    (500)

/**********************************************************************************************************************
 Global Typedef definitions
 *********************************************************************************************************************/

/** LED state enumeration */
typedef enum {
    LED_STATE_OFF = 0,      /**< LED is off */
    LED_STATE_ON,           /**< LED is on (steady) */
    LED_STATE_BLINKING,     /**< LED is blinking */
} led_state_t;

/** LED information structure (read-only descriptor) */
typedef struct {
    const char *name;       /**< Display name (e.g., "LED1") */
    const char *color;      /**< Color string (e.g., "Blue") */
    const char *pin_name;   /**< Pin name string (e.g., "P600") */
    uint16_t    pin;        /**< BSP IO port pin value */
} led_info_t;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the LED control module
 * @details Initializes internal state for blink functionality.
 *          Blink cyclic handlers (uT-Kernel 3.0) are created on demand by
 *          led_ctrl_blink(). Must be called before using any other led_ctrl
 *          functions, from a uT-Kernel 3.0 task context (after the kernel starts).
 * @note R-004: migrated from FreeRTOS software timers to uT-Kernel 3.0 cyclic handlers.
 */
void led_ctrl_init(void);

/**
 * Turn on the specified LED
 * @details Sets the LED GPIO pin to HIGH. If the LED was blinking,
 *          the blink timer is stopped first.
 * @param id LED index (0 to LED_COUNT-1)
 * @return true on success, false if id is out of range
 */
bool led_ctrl_on(uint32_t id);

/**
 * Turn off the specified LED
 * @details Sets the LED GPIO pin to LOW. If the LED was blinking,
 *          the blink timer is stopped first.
 * @param id LED index (0 to LED_COUNT-1)
 * @return true on success, false if id is out of range
 */
bool led_ctrl_off(uint32_t id);

/**
 * Toggle the specified LED
 * @details Reads the current GPIO pin state and writes the opposite level.
 *          If the LED was blinking, the blink timer is stopped first.
 * @param id LED index (0 to LED_COUNT-1)
 * @param p_prev_state If non-NULL, receives the previous state (LED_STATE_ON or LED_STATE_OFF)
 * @return true on success, false if id is out of range
 */
bool led_ctrl_toggle(uint32_t id, led_state_t *p_prev_state);

/**
 * Start blinking the specified LED
 * @details Creates a uT-Kernel 3.0 cyclic handler that toggles the LED at the
 *          specified interval. If the LED is already blinking, the handler is
 *          recreated with the new interval (uT-Kernel 3.0 has no tk_set_cyc).
 * @param id LED index (0 to LED_COUNT-1)
 * @param interval_ms Blink interval in milliseconds (half-period)
 * @return true on success, false if id is out of range or handler creation fails
 */
bool led_ctrl_blink(uint32_t id, uint32_t interval_ms);

/**
 * Get the current state of the specified LED
 * @param id LED index (0 to LED_COUNT-1)
 * @return LED state, or LED_STATE_OFF if id is out of range
 */
led_state_t led_ctrl_get_state(uint32_t id);

/**
 * Get the LED information descriptor for the specified LED
 * @param id LED index (0 to LED_COUNT-1)
 * @return Pointer to the LED info structure, or NULL if id is out of range
 */
const led_info_t *led_ctrl_get_info(uint32_t id);

/**
 * Check if an LED id is valid
 * @param id LED index to check
 * @return true if id is in range [0, LED_COUNT-1]
 */
bool led_ctrl_valid_id(uint32_t id);

#ifdef __cplusplus
}
#endif

#endif /* LED_CTRL_H */
