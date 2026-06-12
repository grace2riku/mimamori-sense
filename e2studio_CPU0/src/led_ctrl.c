/**
 * @file led_ctrl.c
 * @brief LED control abstraction layer implementation
 * @details
 * Implements LED on/off/toggle/blink operations for the EK-RA8P1 board.
 * Uses FSP R_IOPORT API for GPIO control and uT-Kernel 3.0 cyclic handlers
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
 *
 * @note R-004 / Issue #154 (FreeRTOS -> uT-Kernel 3.0 migration):
 * Blink functionality was migrated from FreeRTOS software timers
 * (xTimerCreate/Start/Stop/ChangePeriod) to uT-Kernel 3.0 cyclic handlers
 * (tk_cre_cyc/tk_stp_cyc/tk_del_cyc; started via TA_STA at creation).
 * See doc/migration/mtk3-migration-guide.md 7.2.
 *   - One cyclic handler is created per LED (mirrors the previous one-timer-per-LED design).
 *   - The cyclic handler runs in interrupt context; it only calls
 *     R_IOPORT_PinWrite/Read (FSP/RTOS-independent) and never a waiting system call
 *     (migration guide 5.1 ISR constraint).
 *   - uT-Kernel 3.0 has no tk_set_cyc, so changing the blink interval is implemented
 *     by deleting and recreating the cyclic handler with the new cycle time.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include "led_ctrl.h"
#include "hal_data.h"
#include "common_data.h"
#include "bsp_pin_cfg.h"

#include <tk/tkernel.h>

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

/**
 * uT-Kernel 3.0 cyclic handler IDs for blink functionality (one per LED).
 * 0 means "not created". Cyclic handlers are created on demand by
 * led_ctrl_blink() and deleted by led_stop_blink_cyc().
 */
static ID s_blink_cyc[LED_COUNT] = { 0, 0, 0 };

/** Module initialization flag */
static bool s_initialized = false;

/**********************************************************************************************************************
 Private (static) functions prototypes
 *********************************************************************************************************************/
static void led_blink_cyc_handler(void *exinf);
static void led_set_pin(uint32_t id, bsp_io_level_t level);
static bsp_io_level_t led_read_pin(uint32_t id);
static void led_stop_blink_cyc(uint32_t id);

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Blink cyclic handler function
 * @details Called by the uT-Kernel 3.0 kernel at the blink interval.
 *          Runs in interrupt context; only toggles the LED pin via FSP
 *          R_IOPORT API (no waiting system calls; migration guide 5.1).
 *          The LED index is passed through the cyclic handler's exinf.
 * @param exinf Extended information (LED index, set at tk_cre_cyc time)
 */
static void led_blink_cyc_handler(void *exinf)
{
    uint32_t id = (uint32_t)exinf;

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
 * Stop the blink cyclic handler for the specified LED if it is running.
 * @details Stops and deletes the cyclic handler. The handler is recreated
 *          by led_ctrl_blink() when blinking is (re)started, which also
 *          allows the cycle interval to be changed (no tk_set_cyc in uT-Kernel 3.0).
 * @param id LED index
 */
static void led_stop_blink_cyc(uint32_t id)
{
    if (s_blink_cyc[id] > 0) {
        /* Errors are non-fatal here: if already stopped/deleted, just clear the id. */
        (void)tk_stp_cyc(s_blink_cyc[id]);
        (void)tk_del_cyc(s_blink_cyc[id]);
        s_blink_cyc[id] = 0;
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

    /* uT-Kernel 3.0: cyclic handlers are created on demand in led_ctrl_blink()
     * (with the requested cycle interval) and deleted when blinking stops.
     * Nothing to pre-create here. */
    for (uint32_t i = 0; i < LED_COUNT; i++) {
        s_blink_cyc[i] = 0;
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

    /* Stop blink cyclic handler if running */
    led_stop_blink_cyc(id);

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

    /* Stop blink cyclic handler if running */
    led_stop_blink_cyc(id);

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

    /* Stop blink cyclic handler if running */
    led_stop_blink_cyc(id);

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
 * @details Creates (or recreates) a uT-Kernel 3.0 cyclic handler that toggles
 *          the LED at the specified half-period interval.
 *          uT-Kernel 3.0 has no tk_set_cyc, so changing the interval is done by
 *          deleting any existing handler (led_stop_blink_cyc) and creating a
 *          new one with the requested cycle time.
 */
bool led_ctrl_blink(uint32_t id, uint32_t interval_ms)
{
    if (id >= LED_COUNT) {
        return false;
    }

    /* Stop and delete any existing cyclic handler (also handles interval change). */
    led_stop_blink_cyc(id);

    /* uT-Kernel 3.0 cycle interval is in milliseconds (RELTIM). Minimum 1ms. */
    RELTIM cyctim = (RELTIM)interval_ms;
    if (cyctim == 0) {
        cyctim = 1;
    }

    /* Create the cyclic handler.
     *   TA_HLNG : handler is written in C
     *   TA_STA  : start the cyclic handler immediately on creation
     *   TA_PHS  : preserve cycle phase (not strictly required here)
     * exinf carries the LED index so a single handler function serves all LEDs.
     * USE_OBJECT_NAME = 0, so T_CCYC has no dsname member (do not initialize it). */
    T_CCYC ccyc = {
        .exinf  = (void *)id,
        .cycatr = TA_HLNG | TA_STA | TA_PHS,
        .cychdr = (FP)led_blink_cyc_handler,
        .cyctim = cyctim,
        .cycphs = cyctim,   /* first invocation after one full interval */
    };

    ID cycid = tk_cre_cyc(&ccyc);
    if (cycid <= E_OK) {
        /* Creation failed (negative return is an error code).
         * led_stop_blink_cyc() above already cleared s_blink_cyc[id]; leave the
         * LED in a defined OFF state so s_led_state does not stay stuck at
         * LED_STATE_BLINKING with no handler running (review nit-3). */
        led_set_pin(id, BSP_IO_LEVEL_LOW);
        s_led_state[id] = LED_STATE_OFF;
        return false;
    }
    s_blink_cyc[id] = cycid;

    /* Ensure the LED starts in ON state for the first blink cycle.
     * TA_STA already started the cyclic handler at creation, so no separate
     * tk_sta_cyc() call is needed (review nit-2). */
    led_set_pin(id, BSP_IO_LEVEL_HIGH);
    s_led_state[id] = LED_STATE_BLINKING;

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
