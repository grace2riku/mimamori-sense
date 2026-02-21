/**
 * @file led_ctrl.c
 * @brief LED control abstraction layer implementation
 * @details
 * Implements LED on/off/toggle/blink operations for the EK-RA8P1 board.
 * Uses FSP R_IOPORT API for GPIO control and FreeRTOS software timers
 * for blink functionality.
 *
 * EK-RA8P1 User LED Mapping (from board_leds.c / bsp_pin_cfg.h):
 *   Index 0 - LED1: Blue  - BSP_IO_PORT_06_PIN_00 (P600) - USER_LED_BLUE
 *   Index 1 - LED2: Green - BSP_IO_PORT_03_PIN_03 (P303) - USER_LED_GREEN
 *   Index 2 - LED3: Red   - BSP_IO_PORT_10_PIN_07 (PA07) - USER_LED_RED
 *
 * All LEDs are active-high (HIGH = ON, LOW = OFF).
 *
 * @note
 * This file is part of the LED control command (S-011) implementation.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include "led_ctrl.h"
#include "hal_data.h"
#include "common_data.h"
#include "bsp_pin_cfg.h"

#include "FreeRTOS.h"
#include "timers.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/**
 * LED information table
 *
 * Pin assignments are from board_leds.c (g_bsp_prv_leds[]):
 *   [0] BSP_IO_PORT_06_PIN_00  -> P600 (Blue,  USER_LED_BLUE)
 *   [1] BSP_IO_PORT_03_PIN_03  -> P303 (Green, USER_LED_GREEN)
 *   [2] BSP_IO_PORT_10_PIN_07  -> PA07 (Red,   USER_LED_RED)
 */
static const led_info_t s_led_info[LED_COUNT] = {
    { "LED1", "Blue",  "P600", (uint16_t)BSP_IO_PORT_06_PIN_00 },
    { "LED2", "Green", "P303", (uint16_t)BSP_IO_PORT_03_PIN_03 },
    { "LED3", "Red",   "PA07", (uint16_t)BSP_IO_PORT_10_PIN_07 },
};

/** Current LED state (tracked by software) */
static led_state_t s_led_state[LED_COUNT] = {
    LED_STATE_OFF, LED_STATE_OFF, LED_STATE_OFF
};

/** FreeRTOS software timer handles for blink functionality (one per LED) */
static TimerHandle_t s_blink_timer[LED_COUNT] = { NULL, NULL, NULL };

/** Blink timer names for FreeRTOS debug */
static const char * const s_timer_names[LED_COUNT] = {
    "LED0_Blink", "LED1_Blink", "LED2_Blink"
};

/** Module initialization flag */
static bool s_initialized = false;

/**********************************************************************************************************************
 Private (static) functions prototypes
 *********************************************************************************************************************/
static void led_blink_timer_callback(TimerHandle_t xTimer);
static void led_set_pin(uint32_t id, bsp_io_level_t level);
static bsp_io_level_t led_read_pin(uint32_t id);
static void led_stop_blink_timer(uint32_t id);

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Blink timer callback function
 * @details Called by FreeRTOS timer service at the blink interval.
 *          Toggles the LED pin state.
 * @param xTimer Timer handle (timer ID is set to the LED index)
 */
static void led_blink_timer_callback(TimerHandle_t xTimer)
{
    uint32_t id = (uint32_t)pvTimerGetTimerID(xTimer);

    if (id >= LED_COUNT) {
        return;
    }

    /* Toggle the pin */
    bsp_io_level_t current = led_read_pin(id);
    bsp_io_level_t next = (current == BSP_IO_LEVEL_HIGH) ? BSP_IO_LEVEL_LOW : BSP_IO_LEVEL_HIGH;
    led_set_pin(id, next);
}

/**
 * Set the GPIO pin level for the specified LED
 * @param id LED index
 * @param level Pin level (BSP_IO_LEVEL_HIGH or BSP_IO_LEVEL_LOW)
 */
static void led_set_pin(uint32_t id, bsp_io_level_t level)
{
    bsp_io_port_pin_t pin = (bsp_io_port_pin_t)s_led_info[id].pin;
    R_IOPORT_PinWrite(&g_ioport_ctrl, pin, level);
}

/**
 * Read the current GPIO pin level for the specified LED
 * @param id LED index
 * @return Current pin level
 */
static bsp_io_level_t led_read_pin(uint32_t id)
{
    bsp_io_level_t level = BSP_IO_LEVEL_LOW;
    bsp_io_port_pin_t pin = (bsp_io_port_pin_t)s_led_info[id].pin;
    R_IOPORT_PinRead(&g_ioport_ctrl, pin, &level);
    return level;
}

/**
 * Stop the blink timer for the specified LED if it is running
 * @param id LED index
 */
static void led_stop_blink_timer(uint32_t id)
{
    if (s_blink_timer[id] != NULL) {
        xTimerStop(s_blink_timer[id], 0);
    }
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Initialize the LED control module
 */
void led_ctrl_init(void)
{
    if (s_initialized) {
        return;
    }

    /* Create blink timers for each LED.
     * Timer period is set to a default value; it will be changed when blink is started.
     * Timers are created in the stopped state (not auto-reload until started). */
    for (uint32_t i = 0; i < LED_COUNT; i++) {
        s_blink_timer[i] = xTimerCreate(
            s_timer_names[i],
            pdMS_TO_TICKS(LED_BLINK_DEFAULT_MS),
            pdTRUE,                     /* Auto-reload */
            (void *)i,                  /* Timer ID = LED index */
            led_blink_timer_callback
        );
        /* Timer creation can fail if FreeRTOS heap is exhausted.
         * The blink command will report an error in that case. */
    }

    /* Ensure all LEDs start in OFF state */
    for (uint32_t i = 0; i < LED_COUNT; i++) {
        led_set_pin(i, BSP_IO_LEVEL_LOW);
        s_led_state[i] = LED_STATE_OFF;
    }

    s_initialized = true;
}

/**
 * Turn on the specified LED
 */
bool led_ctrl_on(uint32_t id)
{
    if (id >= LED_COUNT) {
        return false;
    }

    /* Stop blink timer if running */
    led_stop_blink_timer(id);

    led_set_pin(id, BSP_IO_LEVEL_HIGH);
    s_led_state[id] = LED_STATE_ON;

    return true;
}

/**
 * Turn off the specified LED
 */
bool led_ctrl_off(uint32_t id)
{
    if (id >= LED_COUNT) {
        return false;
    }

    /* Stop blink timer if running */
    led_stop_blink_timer(id);

    led_set_pin(id, BSP_IO_LEVEL_LOW);
    s_led_state[id] = LED_STATE_OFF;

    return true;
}

/**
 * Toggle the specified LED
 */
bool led_ctrl_toggle(uint32_t id, led_state_t *p_prev_state)
{
    if (id >= LED_COUNT) {
        return false;
    }

    /* Stop blink timer if running */
    led_stop_blink_timer(id);

    bsp_io_level_t current = led_read_pin(id);
    led_state_t prev = (current == BSP_IO_LEVEL_HIGH) ? LED_STATE_ON : LED_STATE_OFF;

    if (p_prev_state != NULL) {
        *p_prev_state = prev;
    }

    /* Write the opposite level */
    bsp_io_level_t next = (current == BSP_IO_LEVEL_HIGH) ? BSP_IO_LEVEL_LOW : BSP_IO_LEVEL_HIGH;
    led_set_pin(id, next);

    s_led_state[id] = (next == BSP_IO_LEVEL_HIGH) ? LED_STATE_ON : LED_STATE_OFF;

    return true;
}

/**
 * Start blinking the specified LED
 */
bool led_ctrl_blink(uint32_t id, uint32_t interval_ms)
{
    if (id >= LED_COUNT) {
        return false;
    }

    if (s_blink_timer[id] == NULL) {
        return false;   /* Timer creation failed during init */
    }

    /* Stop the timer if already running */
    xTimerStop(s_blink_timer[id], 0);

    /* Change the timer period to the requested interval */
    TickType_t period = pdMS_TO_TICKS(interval_ms);
    if (period == 0) {
        period = 1;     /* Minimum 1 tick */
    }
    xTimerChangePeriod(s_blink_timer[id], period, 0);

    /* Ensure the LED starts in ON state for the first blink cycle */
    led_set_pin(id, BSP_IO_LEVEL_HIGH);
    s_led_state[id] = LED_STATE_BLINKING;

    /* Start the timer */
    xTimerStart(s_blink_timer[id], 0);

    return true;
}

/**
 * Get the current state of the specified LED
 */
led_state_t led_ctrl_get_state(uint32_t id)
{
    if (id >= LED_COUNT) {
        return LED_STATE_OFF;
    }

    return s_led_state[id];
}

/**
 * Get the LED information descriptor for the specified LED
 */
const led_info_t *led_ctrl_get_info(uint32_t id)
{
    if (id >= LED_COUNT) {
        return NULL;
    }

    return &s_led_info[id];
}

/**
 * Check if an LED id is valid
 */
bool led_ctrl_valid_id(uint32_t id)
{
    return (id < LED_COUNT);
}
