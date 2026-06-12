/**
 * @file ov5640.h
 * @brief OV5640 camera sensor driver interface
 * @details
 * Provides I2C register access, power/reset sequences, sensor initialization,
 * stream control, and chip ID verification for the OV5640 5MP camera sensor.
 *
 * The OV5640 is connected via:
 *   - I2C (IIC1): Register read/write (16-bit address, 8-bit data)
 *   - GPIO P704: Power-down control (active HIGH)
 *   - GPIO P709: Hardware reset (active LOW)
 *   - GPT12: 24 MHz XCLK generation
 *   - MIPI CSI-2: 2-lane video data output
 *
 * Usage sequence:
 *   1. ov5640_hw_init()          - Set initial GPIO state
 *   2. ov5640_exit_power_down()  - Exit power-down with timing
 *   3. ov5640_hw_reset()         - Hardware reset via GPIO
 *   4. ov5640_sw_reset()         - Software reset via I2C
 *   5. ov5640_verify_chip_id()   - Verify correct sensor is connected
 *   6. ov5640_init()             - Full register initialization
 *   7. ov5640_stream_on()        - Start MIPI CSI-2 output
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640_cfg.h
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640.c
 */

#ifndef OV5640_H
#define OV5640_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include "bsp_api.h"
#include "hal_data.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** OV5640 expected chip ID values */
#define OV5640_CHIP_ID_H_EXPECTED   (0x56)  /* High byte of chip ID */
#define OV5640_CHIP_ID_L_REV1A      (0x40)  /* Low byte: revision 1a */
#define OV5640_CHIP_ID_L_REV2A      (0x41)  /* Low byte: revision 2a */
#define OV5640_CHIP_ID_L_REV2C      (0x4C)  /* Low byte: revision 2c */

/**********************************************************************************************************************
 Global Typedef definitions
 *********************************************************************************************************************/

/** OV5640 driver status */
typedef struct {
    bool     initialized;       /* true after ov5640_init() succeeds */
    bool     streaming;         /* true when stream is on */
    bool     chip_id_verified;  /* true after chip ID verification passes */
    uint16_t chip_id;           /* Actual chip ID read from sensor */
    uint32_t init_error_count;  /* Number of register write errors during init */
    uint8_t  sw_reset_check;    /* 0x3008 readback after sw_reset: 0x02=XCLK OK, 0x82=no XCLK */
} ov5640_status_t;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Write a single register on the OV5640 (16-bit address, 8-bit data)
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640.c:66-81
 *
 * @param reg  16-bit register address
 * @param data 8-bit data to write
 * @return FSP_SUCCESS on success, error code on failure
 */
extern fsp_err_t ov5640_write_reg(uint16_t reg, uint8_t data);

/**
 * Read a single register from the OV5640 (16-bit address, 8-bit data)
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640.c:92-99
 *
 * @param reg  16-bit register address
 * @return Read value (8-bit), or 0xFF on error
 */
extern uint8_t ov5640_read_reg(uint16_t reg);

/**
 * Write a register with read-back verification
 *
 * @param reg  16-bit register address
 * @param data 8-bit data to write
 * @return FSP_SUCCESS if write and verify succeeded, FSP_ERR_ASSERTION if mismatch
 */
extern fsp_err_t ov5640_write_reg_verified(uint16_t reg, uint8_t data);

/**
 * Set initial GPIO state: RESET=LOW, PWDN=HIGH (enter reset and power-down)
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640.c:110-119
 */
extern void ov5640_hw_init(void);

/**
 * Exit power-down state with correct timing sequence
 * Sequence: RESET=LOW -> 20ms -> PWDN=LOW -> 50ms -> RESET=HIGH -> 20ms
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640.c:130-143
 */
extern void ov5640_exit_power_down(void);

/**
 * Hardware reset via GPIO with timing
 * Sequence: RESET=LOW -> 20ms -> RESET=HIGH -> 20ms
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640.c:154-163
 */
extern void ov5640_hw_reset(void);

/**
 * Software reset via I2C register write
 * Writes 0x82 to register 0x3008, then waits 300ms
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/ov5640.c:180-189
 */
extern void ov5640_sw_reset(void);

/**
 * Full OV5640 sensor initialization
 * Performs:
 *   - GPT timer open and start (24 MHz XCLK)
 *   - Power-down exit and reset sequence (~400ms total)
 *   - Chip ID verification
 *   - Software reset with register configuration
 *   - PLL, timing, AWB, gamma, color matrix, ISP settings
 *   - Clock configuration
 *   - MIPI virtual channel setup
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:456-896
 *
 * @return FSP_SUCCESS on success, FSP_ERR_ASSERTION if chip ID check fails
 */
extern fsp_err_t ov5640_init(void);

/**
 * Start camera MIPI CSI-2 stream output
 * Writes 0x00 to register 0x4202
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:426-429
 */
extern void ov5640_stream_on(void);

/**
 * Stop camera MIPI CSI-2 stream output
 * Writes 0x0F to register 0x4202
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:440-443
 */
extern void ov5640_stream_off(void);

/**
 * Read and verify the OV5640 chip ID
 * Reads registers 0x300A (high) and 0x300B (low).
 * Valid IDs: 0x5640, 0x5641, 0x564C
 *
 * @return FSP_SUCCESS if valid chip ID detected, FSP_ERR_ASSERTION otherwise
 */
extern fsp_err_t ov5640_verify_chip_id(void);

/**
 * Get current driver status (for diagnostics / NT-Shell commands)
 *
 * @return Pointer to internal status structure (read-only)
 */
extern const ov5640_status_t *ov5640_get_status(void);

/**
 * Configure OV5640 PLL clocks
 * Sets PLL dividers for target MIPI clock frequency based on 24 MHz XCLK.
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:1056-1227
 *
 * @return Calculated MIPI clock frequency in Hz
 */
extern uint32_t ov5640_configure_clocks(void);

/**
 * Set MIPI virtual channel
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:405-415
 *
 * @param vchannel Virtual channel number (0-3)
 */
extern void ov5640_set_mipi_virtual_channel(uint32_t vchannel);

/**
 * Create the uT-Kernel event flag for I2C completion synchronization (R-005)
 *
 * @details Must be called once (from the camera task, at startup) before any
 *          camera I2C operation. Replaces the FreeRTOS g_i2c_event_group which
 *          is not created at runtime under method A (g_hal_init() never runs).
 *          Idempotent: re-creation is skipped if already created.
 */
extern void ov5640_i2c_sync_init(void);

/**
 * Wait for an I2C transfer to complete (R-005)
 *
 * @details Blocks on the uT-Kernel event flag until the I2C completion
 *          interrupt (i2c_camera_callback) signals completion or abort,
 *          or the timeout (1000 ms) elapses. Public so camera_thread_entry.c
 *          can use it for board switch / GreenPAK I2C waits.
 *
 * @return FSP_SUCCESS on transfer complete,
 *         FSP_ERR_ABORTED on transfer abort (or flag error),
 *         FSP_ERR_TIMEOUT on timeout.
 */
extern fsp_err_t ov5640_i2c_wait_complete(void);

#endif /* OV5640_H */
