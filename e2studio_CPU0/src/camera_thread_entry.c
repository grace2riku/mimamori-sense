/**
 * @file camera_thread_entry.c
 * @brief Camera capture task entry and FSP callbacks (F-002-5)
 * @details
 * uT-Kernel task that manages the entire camera capture pipeline:
 *   1. Board-level signal routing (GreenPAK, PI4IOE5V6408 board switch)
 *   2. VIN driver open (which internally opens MIPI CSI and MIPI PHY)
 *   3. OV5640 sensor initialization (XCLK start, power-up, reset, register config)
 *   4. Continuous frame capture with triple buffering in SDRAM
 *   5. Capture state monitoring in the main loop
 *
 * IMPORTANT: Board-level signal routing (GreenPAK, PI4IOE5V6408) must be done
 * BEFORE R_VIN_Open(). In the reference project, common_init() initializes the
 * board switch before the camera thread starts. If the D-PHY initializes with
 * Hi-Z/floating MIPI data lanes, it fails to detect data lane activity.
 * The VIN/MIPI pipeline must then be opened BEFORE OV5640 initialization.
 * This matches the reference project order (common_init → R_VIN_Open → ov5640_init).
 * Issue #93 (F-002-6) identified three issues:
 *   1. Init order: board switch must precede R_VIN_Open()
 *   2. IIC1 channel conflict between camera and touch panel I2C drivers
 *   3. OV5640 wake-up settling: multi-core environment requires 5ms delay
 *      after software power-down wake-up before MIPI stream can start
 *
 * This file also provides the FSP-required callback functions:
 *   - vin0_callback()       : VIN capture frame complete / error handler
 *   - mipi_csi0_callback()  : MIPI CSI-2 error event handler
 *
 * These callbacks are declared in ra_gen/common_data.h and registered in the
 * FSP-generated VIN/CSI configuration. They forward events to the port layer
 * functions (vin_port_callback, csi2_port_callback) which handle statistics
 * tracking and diagnostics.
 *
 * FSP Instance Mapping (current project):
 *   VIN:       g_vin0_ctrl, g_vin0_cfg           (common_data.h)
 *   MIPI CSI:  g_mipi_csi0_ctrl, g_mipi_csi0_cfg (common_data.h)
 *   MIPI PHY:  g_mipi_phy0_ctrl, g_mipi_phy0_cfg (common_data.h)
 *   GPT Timer: g_timer_camera_xclk_ctrl/cfg       (hal_data.h)
 *   I2C:       g_i2c_master_camera_ctrl/cfg        (hal_data.h)
 *
 * R-005 / Issue #155（FreeRTOS -> uT-Kernel 3.0 移行）:
 *   本タスクは src/usermain.c の usermain() から tk_cre_tsk + tk_sta_tsk で
 *   生成・起動される（方式A: ra_gen/main.c の FreeRTOS スレッド生成経路は未到達）。
 *   - エントリ関数は uT-Kernel タスク形式 camera_task(INT stacd, void *exinf) へ移植。
 *   - FreeRTOS 依存（FreeRTOS.h/task.h/event_groups.h, vTaskDelay, pdMS_TO_TICKS,
 *     g_i2c_event_group の xEventGroupWaitBits）を除去/置換。
 *   - I2C 完了待ちは ov5640.c の uT-Kernel イベントフラグ（ov5640_i2c_sync_init /
 *     ov5640_i2c_wait_complete）へ統一（g_i2c_event_group は方式A で未生成のため）。
 *   旧 FreeRTOS エントリ camera_thread_entry() は ra_gen/camera_thread.c の
 *   リンク解決のため末尾に残していたが、Issue #186 Step 2 で FSP の RTOS 設定を
 *   No RTOS にし ra_gen/camera_thread.c 自体が生成されなくなったため削除した。
 *   詳細は doc/migration/mtk3-migration-guide.md 7.3。
 *
 * uT-Kernel タスク設定（usermain.c で生成）:
 *   Stack size: 4096 bytes（FreeRTOS 版スレッドと同等）
 *   Priority  : itskpri=11（blink=10 と ntshell=12 の中間）
 *
 * Reference:
 *   reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:309-377
 *   reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:907-1001
 *
 * Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>

#include "camera_thread_api.h"
#include "camera_framebuffer.h"
#include "hal_data.h"
#include "common_data.h"
#include "bsp_api.h"

#include "ov5640.h"
#include "ov5640_cfg.h"
#include "jlink_console.h"
#include "r_typedefs.h"

#include "port/vin_port.h"
#include "port/csi2_port.h"
#include "port/mipi_port.h"

#include <tk/tkernel.h>

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size for camera thread log messages */
#define CAMERA_PRINT_BUF_SIZE   (128)

/** Delay between VIN CaptureStart and stream_on (ms) */
#define CAMERA_CAPTURE_SETTLE_MS (1)

/** OV5640 wake-up settling delay before stream_on (ms).
 *  Required in multi-core environment to allow OV5640 internal PLL and
 *  MIPI transmitter to stabilize after software power-down wake-up. */
#define CAMERA_WAKEUP_SETTLE_MS  (5)

/**
 * MIPI Interface Enable pin (P108 = J2_30 on Camera Expansion Board)
 *
 * This GPIO controls the MIPI data path on the Camera Expansion Board.
 * It is configured as "Output mode (Initial High)" in FSP, meaning the
 * MIPI interface is DISABLED at startup. It must be driven LOW to enable
 * MIPI CSI-2 data flow from OV5640 to the RA8P1 MIPI PHY receiver.
 *
 * Without setting this pin LOW, the VIN never receives frame complete
 * interrupts because MIPI data cannot reach the CSI-2 receiver.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:341
 * RUHMI symbolic name: MIPI_IF_EN
 */
#define CAMERA_MIPI_IF_EN_PIN   (BSP_IO_PORT_01_PIN_08)

/**
 * PI4IOE5V6408 I2C I/O Expander (SW4 on EK-RA8P1)
 *
 * Controls board-level signal routing including MIPI, OSPI, USB, etc.
 * Must be initialized to take GPIO pins out of Hi-Z state, otherwise
 * MIPI signal routing is floating and no data reaches the CSI-2 receiver.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/board_cfg_switch.c:67-107
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/common_init.c:96
 */
#define BOARD_SWITCH_I2C_ADDR       (0x43)          /* PI4IOE5V6408 7-bit I2C address */
#define BOARD_SWITCH_REG_DIR        (0x03)          /* Input/Output direction register */
#define BOARD_SWITCH_REG_OUTPUT     (0x05)          /* Output register */
#define BOARD_SWITCH_REG_HIZ        (0x07)          /* Hi-Z register */
#define BOARD_SWITCH_OUTPUT_VALUE   (0xF8)          /* Output: bits[7:3]=HIGH, bit2=LOW(OSPI_OE), bits[1:0]=LOW */
                                                    /* Reference: board_cfg_switch_funct_on(SW4_3) sets bit2 LOW */
#define BOARD_SWITCH_HIZ_VALUE      (0x00)          /* All pins driven (not Hi-Z) */
#define BOARD_SWITCH_DIR_VALUE      (0xFF)          /* All pins configured as output */

/**
 * GreenPAK (SLG46824) I2C addresses and configuration
 *
 * The GreenPAK on the Camera Expansion Board controls IO4/IO5 pins that
 * may affect MIPI signal routing. If not properly configured, these pins
 * can block MIPI CSI-2 data from reaching the RA8P1 receiver.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/board_greenpak.c
 */
#define GREENPAK_I2C_ADDR_RAM       (0x20)          /* GreenPAK RAM access */
#define GREENPAK_I2C_ADDR_EEPROM    (0x22)          /* GreenPAK EEPROM access */
#define GREENPAK_REG_START          (0x66)          /* IO4/IO5 config register start */
#define GREENPAK_IO4_EXPECTED       (0x30)          /* Expected IO4 config bits */
#define GREENPAK_IO4_MASK           (0xCF)          /* IO4 config mask */
#define GREENPAK_IO5_EXPECTED       (0x30)          /* Expected IO5 config bits */
#define GREENPAK_IO5_MASK           (0xCF)          /* IO5 config mask */

/* R-005: I2C 完了待ちは ov5640.c の uT-Kernel イベントフラグ（ov5640_i2c_wait_complete）へ
 * 統一したため、本ファイルの I2C_XFER_COMPLETE/ABORT/TIMEOUT_MS（旧 FreeRTOS イベントグループ
 * 用ビット・タイムアウト）と g_i2c_event_group 参照は不要となり削除した。 */

/** Main loop polling interval (ms). R-005: tick -> ms（tk_dly_tsk）。 */
#define CAMERA_POLL_INTERVAL    (1)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/** Camera thread initialization state */
static volatile bool s_camera_initialized = false;

/** Camera thread error flag (set if init fails, thread blocks) */
static volatile bool s_camera_init_error = false;

/**
 * Camera I2C operations complete flag
 *
 * Set to true after all OV5640 register writes and board I2C operations
 * are finished, and g_i2c_master_camera_ctrl has been closed.
 *
 * The touch panel driver (lv_port_indev_init) polls this flag before
 * opening g_i2c_master0 on IIC1, to prevent the IRQ context conflict
 * that causes I2C callbacks to be routed to the wrong handler.
 *
 * Reference: Issue #93 root cause - IIC1 channel conflict
 */
static volatile bool s_camera_i2c_done = false;

/**********************************************************************************************************************
 Private (static) function prototypes
 *********************************************************************************************************************/
static void camera_thread_log(const char *msg);
static void camera_thread_log_fsp_err(const char *context, fsp_err_t err);
static bool camera_greenpak_init(void);
static bool camera_board_switch_init(void);

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Camera Thread entry function
 *
 * @details
 * Performs the full camera capture pipeline initialization:
 *
 * 1. VIN driver open (MUST be first):
 *    R_VIN_Open() initializes VIN + MIPI CSI + MIPI PHY in one call.
 *    The FSP VIN driver internally chains the CSI and PHY initialization.
 *    This must complete before the OV5640 is configured so the MIPI
 *    receiver is ready.
 *
 * 2. OV5640 sensor initialization:
 *    Calls ov5640_init() which handles XCLK start, I2C open, power-up,
 *    reset, chip ID verification, and register configuration (~500ms).
 *    Includes HTS/VTS frame timing setup added in F-002-6 fix.
 *
 * 3. Capture start:
 *    Stops OV5640 stream, starts VIN continuous capture, then starts
 *    OV5640 stream. This ordering ensures VIN is ready before data arrives.
 *
 * 4. Main loop:
 *    Polls VIN capture status at 1-tick intervals for diagnostics.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:309-377
 *
 * R-005: uT-Kernel タスク本体（usermain() から tk_cre_tsk + tk_sta_tsk で生成・起動）。
 *
 * @param stacd タスク起動コード（未使用）
 * @param exinf 拡張情報（未使用）
 */
void camera_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    fsp_err_t err;
    char buf[CAMERA_PRINT_BUF_SIZE];

    s_camera_initialized = false;
    s_camera_init_error = false;

    /*
     * R-005: I2C 完了同期用 uT-Kernel イベントフラグを生成する。
     * 以降の board switch / GreenPAK / OV5640 の各 I2C 完了待ち
     * （ov5640_i2c_wait_complete）より前に必須。方式A では g_hal_init() が
     * 実行されず g_i2c_event_group が未生成のため、ここで自前に生成する。冪等。
     */
    ov5640_i2c_sync_init();

    /*
     * Wait for JLink console to be ready so we can log initialization progress.
     * The ntshell_thread initializes the UART console; we wait until it is
     * configured before attempting to print.
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:325-329
     */
    while (!jlink_configured())
    {
        tk_dly_tsk(100);
    }

    camera_thread_log("Camera thread started.\r\n");

    /* ======================================================================
     * Step 1: Initialize board-level signal routing BEFORE MIPI PHY init
     *
     * CRITICAL ORDERING: In the reference project, common_init() runs in
     * the main_menu_thread BEFORE the camera thread starts. common_init()
     * initializes GreenPAK and the board switch (PI4IOE5V6408), taking
     * MIPI signal routing out of Hi-Z state. The camera thread then waits
     * for system_up() before calling R_VIN_Open().
     *
     * This ordering ensures that when R_MIPI_PHY_Open() (called internally
     * by R_VIN_Open) initializes the D-PHY, the MIPI data lane signals
     * are properly routed and not floating. If the D-PHY initializes with
     * Hi-Z/floating data lanes, it may fail to detect data lane activity
     * even after signals are later routed correctly.
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/common_init.c:93-96
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:320-331
     * ====================================================================== */

    /* --- Step 1a: Initialize GreenPAK (SLG46824) --- */
    camera_thread_log("  Checking GreenPAK (SLG46824)...\r\n");

    if (camera_greenpak_init())
    {
        camera_thread_log("  GreenPAK IO4/IO5 configured correctly.\r\n");
    }
    else
    {
        camera_thread_log("  WARNING: GreenPAK check/init failed.\r\n");
    }

    /* --- Step 1b: Initialize Board Switch (PI4IOE5V6408) --- */
    camera_thread_log("  Initializing board switch (PI4IOE5V6408)...\r\n");

    if (camera_board_switch_init())
    {
        camera_thread_log("  Board switch initialized - MIPI signals routed.\r\n");

        /* Readback verification: confirm board switch I2C writes succeeded */
        {
            fsp_err_t rb_err = R_IIC_MASTER_SlaveAddressSet(&g_i2c_master_camera_ctrl,
                                              BOARD_SWITCH_I2C_ADDR, I2C_MASTER_ADDR_MODE_7BIT);
            if (FSP_SUCCESS == rb_err)
            {
                static const uint8_t verify_regs[] = {
                    BOARD_SWITCH_REG_OUTPUT, BOARD_SWITCH_REG_HIZ, BOARD_SWITCH_REG_DIR
                };
                uint8_t verify_vals[3] = { 0xFF, 0xFF, 0x00 };
                bool rb_ok = true;

                for (uint32_t i = 0; i < 3 && rb_ok; i++)
                {
                    uint8_t reg = verify_regs[i];

                    fsp_err_t e = R_IIC_MASTER_Write(&g_i2c_master_camera_ctrl, &reg, 1, true);
                    if (FSP_SUCCESS != e) { rb_ok = false; break; }

                    /* R-005: xEventGroupWaitBits -> ov5640_i2c_wait_complete (uT-Kernel flag) */
                    if (FSP_SUCCESS != ov5640_i2c_wait_complete()) { rb_ok = false; break; }

                    e = R_IIC_MASTER_Read(&g_i2c_master_camera_ctrl, &verify_vals[i], 1, false);
                    if (FSP_SUCCESS != e) { rb_ok = false; break; }

                    if (FSP_SUCCESS != ov5640_i2c_wait_complete()) { rb_ok = false; break; }
                }

                R_IIC_MASTER_SlaveAddressSet(&g_i2c_master_camera_ctrl,
                                              OV5640_I2C_ADDR, I2C_MASTER_ADDR_MODE_7BIT);

                if (rb_ok)
                {
                    snprintf(buf, sizeof(buf),
                             "  [Verify] SW4: Out=0x%02X Hi-Z=0x%02X Dir=0x%02X\r\n",
                             verify_vals[0], verify_vals[1], verify_vals[2]);
                }
                else
                {
                    snprintf(buf, sizeof(buf), "  [Verify] SW4 readback FAILED\r\n");
                }
                camera_thread_log(buf);
            }
        }
    }
    else
    {
        camera_thread_log("  WARNING: Board switch init failed - MIPI signals may be floating!\r\n");
    }

    /* ======================================================================
     * Step 2: VIN driver open (MUST be done BEFORE OV5640 init)
     *
     * R_VIN_Open() initializes the Video Input module and internally opens
     * the MIPI CSI-2 receiver and MIPI D-PHY via the FSP driver chain:
     *   R_VIN_Open -> R_MIPI_CSI2_Open -> R_MIPI_PHY_Open
     *
     * Now that board-level signal routing is configured (Step 1), the D-PHY
     * will initialize with properly routed MIPI signals on the data lanes,
     * allowing correct lane detection and calibration.
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:331
     * ====================================================================== */
    camera_thread_log("  Opening VIN driver...\r\n");

    err = R_VIN_Open(&g_vin0_ctrl, &g_vin0_cfg);
    if (FSP_SUCCESS != err)
    {
        camera_thread_log_fsp_err("R_VIN_Open", err);
        s_camera_init_error = true;
        goto camera_init_failed;
    }

    camera_thread_log("  VIN driver opened (VIN + MIPI CSI + MIPI PHY).\r\n");

    /* ======================================================================
     * Step 2b: Enable MIPI interface on Camera Expansion Board
     *
     * P108 (MIPI_IF_EN) controls the MIPI data path enable on the Camera
     * Expansion Board. It must be driven LOW to enable MIPI CSI-2 data
     * flow. This is done AFTER R_VIN_Open, matching the reference project
     * order (line 340-342, after R_VIN_Open at line 331).
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:340-342
     * ====================================================================== */
    R_BSP_PinAccessEnable();
    R_BSP_PinCfg(CAMERA_MIPI_IF_EN_PIN,
                  (uint32_t)(IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_LOW));
    R_BSP_PinAccessDisable();

    camera_thread_log("  MIPI_IF_EN (P108) set LOW - MIPI interface enabled.\r\n");

    /* ======================================================================
     * Step 3: OV5640 sensor initialization
     *
     * ov5640_init() performs:
     *   - XCLK generation start (GPT timer -> 24 MHz)
     *   - I2C master open
     *   - GPIO power-down exit and hardware reset sequence (~400ms)
     *   - Chip ID verification (registers 0x300A/0x300B)
     *   - Software reset
     *   - Full register configuration (100+ registers)
     *   - PLL clock configuration for target MIPI clock
     *   - MIPI virtual channel setup
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:354
     * ====================================================================== */
    camera_thread_log("  Initializing OV5640 sensor...\r\n");

    err = ov5640_init();
    if (FSP_SUCCESS != err)
    {
        const ov5640_status_t *st = ov5640_get_status();
        snprintf(buf, sizeof(buf),
                 "  ERROR: OV5640 init failed. Chip ID: 0x%04X, Errors: %lu\r\n",
                 st->chip_id, (unsigned long)st->init_error_count);
        camera_thread_log(buf);
        s_camera_init_error = true;
        goto camera_init_failed;
    }

    {
        const ov5640_status_t *st = ov5640_get_status();
        snprintf(buf, sizeof(buf),
                 "  OV5640 initialized. Chip ID: 0x%04X, I2C errors: %lu\r\n",
                 st->chip_id, (unsigned long)st->init_error_count);
        camera_thread_log(buf);

        /* XCLK presence check: 0x3008 bit7 is self-clearing on sw_reset.
         * 0x02 = bit7 cleared → XCLK reaching sensor (internal state machine ran)
         * 0x82 = bit7 stuck  → XCLK NOT reaching sensor (no internal clock) */
        snprintf(buf, sizeof(buf),
                 "  [Verify] OV5640 sw_reset check: 0x3008=%02X (%s)\r\n",
                 st->sw_reset_check,
                 (st->sw_reset_check == 0x02) ? "XCLK OK" :
                 (st->sw_reset_check == 0x82) ? "XCLK NOT REACHING SENSOR!" : "UNEXPECTED");
        camera_thread_log(buf);
    }

    /* ======================================================================
     * Step 4: Start capture
     *
     * The capture start sequence is:
     *   1. Stop OV5640 stream output (ensure no data on MIPI lanes)
     *   2. Software power-down OV5640 (register 0x3008 = 0x42)
     *   3. Start VIN continuous capture (R_VIN_CaptureStart)
     *   4. Wake up OV5640 (register 0x3008 = 0x02)
     *   5. Start OV5640 stream output
     *
     * This ordering ensures the VIN is ready to receive data before
     * the OV5640 starts transmitting frames on the MIPI CSI-2 bus.
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:355-363
     * ====================================================================== */
    camera_thread_log("  Starting capture...\r\n");

    /* F-002-6: Initialize frame buffer management before capture starts.
     * This resets frame counters, FPS tracker, and latest-frame pointer.
     * Must be called before R_VIN_CaptureStart so the first frame_complete
     * callback can safely call camera_framebuffer_set_latest(). */
    camera_framebuffer_init();
    camera_thread_log("  Frame buffer manager initialized.\r\n");

    ov5640_stream_off();
    ov5640_write_reg(OV5640_REG_SYS_CTRL, 0x42);  /* Software power-down */

    R_BSP_SoftwareDelay(CAMERA_CAPTURE_SETTLE_MS, BSP_DELAY_UNITS_MILLISECONDS);

    err = R_VIN_CaptureStart(&g_vin0_ctrl, NULL);
    if (FSP_SUCCESS != err)
    {
        camera_thread_log_fsp_err("R_VIN_CaptureStart", err);
        s_camera_init_error = true;
        goto camera_init_failed;
    }

    R_BSP_SoftwareDelay(CAMERA_CAPTURE_SETTLE_MS, BSP_DELAY_UNITS_MILLISECONDS);

    ov5640_write_reg(OV5640_REG_SYS_CTRL, 0x02);  /* Wake up from power-down */

    /*
     * OV5640 wake-up settling delay (Issue #93 fix).
     *
     * In the multi-core environment (CPU0 + CPU1), the OV5640 requires
     * additional settling time after waking from software power-down
     * before MIPI stream output can be started. Without this delay,
     * the MIPI D-PHY data lanes remain inactive (RXST=0, PMST=0x0C).
     *
     * The reference single-core project does not need this delay because
     * bus arbitration timing differs in the single-core configuration.
     */
    R_BSP_SoftwareDelay(CAMERA_WAKEUP_SETTLE_MS, BSP_DELAY_UNITS_MILLISECONDS);

    ov5640_stream_on();

    /* ======================================================================
     * Step 4a: Post-stream verification
     *
     * Wait for OV5640 PLL lock and MIPI data flow, then verify CSI-2
     * status before releasing IIC1 for touch panel use.
     * ====================================================================== */
    tk_dly_tsk(500);   /* R-005: vTaskDelay(pdMS_TO_TICKS(500)) -> tk_dly_tsk(500) */

    {
        /* Read CSI-2 receive status to confirm MIPI data is flowing */
        uint32_t rxst = R_MIPI_CSI->RXST;
        uint32_t vnms = R_VIN->MS;
        uint32_t vnlc = R_VIN->LC;

        snprintf(buf, sizeof(buf),
                 "  [Verify] CSI-2 RXST=%08lX VIN CA=%lu LC=%lu\r\n",
                 (unsigned long)rxst,
                 (unsigned long)(vnms & 0x1),
                 (unsigned long)vnlc);
        camera_thread_log(buf);

        if (rxst != 0) {
            camera_thread_log("  MIPI data path active.\r\n");
        } else {
            camera_thread_log("  WARNING: No MIPI data after 500ms.\r\n");
        }
    }

    /* ======================================================================
     * Step 4b: Release IIC1 bus for touch panel use
     *
     * CRITICAL: Close g_i2c_master_camera_ctrl to release IIC channel 1.
     *
     * Both the camera (g_i2c_master_camera_ctrl) and touch panel
     * (g_i2c_master0_ctrl via rm_comms_i2c) are configured on IIC channel 1.
     * When R_IIC_MASTER_Open() is called on the same channel with different
     * control structures, R_BSP_IrqCfgEnable() overwrites the IRQ context
     * for all four IIC1 interrupts (RXI, TXI, TEI, ERI). The last opener
     * "wins" -- its callback receives all interrupts, while the other's
     * callback never fires, causing xEventGroupWaitBits() timeouts.
     *
     * This is the root cause of Issue #93: when the LVGL thread's
     * lv_port_indev_init() opens g_i2c_master0 on IIC1 during OV5640
     * register configuration, the camera's i2c_camera_callback() stops
     * receiving completion interrupts, causing OV5640 MIPI configuration
     * registers to not be written. Without proper MIPI register config,
     * the OV5640 data lanes never activate (DLST0/DLST1 = 0), and VIN
     * frame complete interrupts never fire.
     *
     * By closing the camera I2C here and setting s_camera_i2c_done,
     * we ensure:
     *   1. All camera I2C writes have completed with correct callbacks
     *   2. IIC1 hardware is released for exclusive touch panel use
     *   3. lv_port_indev_init() can safely open g_i2c_master0 on IIC1
     *
     * The camera no longer needs I2C after stream_on -- MIPI CSI-2 data
     * flows independently of the I2C control bus. Diagnostic commands
     * that need OV5640 register reads will temporarily re-open/close
     * the I2C master.
     *
     * Reference: Issue #93 root cause analysis
     * Reference: e2studio_CPU0/ra/fsp/src/r_iic_master/r_iic_master.c:784-787
     * ====================================================================== */
    R_IIC_MASTER_Close(&g_i2c_master_camera_ctrl);
    s_camera_i2c_done = true;

    camera_thread_log("  IIC1 released (camera I2C closed for touch panel).\r\n");

    s_camera_initialized = true;
    camera_thread_log("  Camera capture started successfully.\r\n");
    camera_thread_log("  Camera initialization complete.\r\n");

    /* ======================================================================
     * Step 5: Main loop - Capture state monitoring
     *
     * Continuously polls VIN capture status. In normal operation,
     * the state should remain CAPTURE_STATE_IN_PROGRESS.
     * Frame data arrives asynchronously via vin0_callback.
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:367-376
     * ====================================================================== */
    while (1)
    {
        capture_status_t capture_status;
        R_VIN_StatusGet(&g_vin0_ctrl, &capture_status);

        if (capture_status.state == CAPTURE_STATE_IN_PROGRESS)
        {
            /*
             * Capture is running normally.
             * Frame completion is handled by vin0_callback in ISR context.
             * Future: inter-frame processing can be added here.
             */
        }

        tk_dly_tsk(CAMERA_POLL_INTERVAL);   /* R-005: vTaskDelay -> tk_dly_tsk (ms) */
    }

    /* Not reached during normal operation */
    return;

camera_init_failed:
    /*
     * Initialization failed - block the thread.
     *
     * Close I2C master if it was opened, and set i2c_done flag to
     * unblock lv_port_indev_init() which is waiting for IIC1 release.
     *
     * The camera thread remains alive but idle so that NT-Shell
     * "camera status" can still report the error state.
     *
     * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:495-498
     */
    if (g_i2c_master_camera_ctrl.open != 0)
    {
        R_IIC_MASTER_Close(&g_i2c_master_camera_ctrl);
    }
    s_camera_i2c_done = true;  /* Unblock touch panel init even on error */

    camera_thread_log("  ERROR: Camera initialization failed. Thread blocked.\r\n");
    while (1)
    {
        tk_dly_tsk(1000);   /* R-005: vTaskDelay(1000) -> tk_dly_tsk(1000) */
    }
}

/**********************************************************************************************************************
 * Function Name: vin0_callback
 * Description  : VIN capture callback handler (FSP-registered)
 *
 * @details
 * This function is declared in ra_gen/common_data.h and registered as the
 * VIN callback in the FSP-generated configuration (g_vin0_cfg.p_callback).
 * It is called from ISR context when a VIN event occurs.
 *
 * Events are forwarded to vin_port_callback() in the port layer which
 * handles frame complete tracking, error statistics, and buffer management.
 *
 * Events:
 *   - VIN_EVENT_NOTIFY
 *     - frame_complete: A complete frame has been written to SDRAM buffer
 *     - fifo_overflow: VIN FIFO overflow (data loss, typically bandwidth issue)
 *     - end_of_frame: End of frame marker detected
 *   - VIN_EVENT_ERROR
 *     - AXI bus error or other hardware error
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:907-941
 *
 * @param p_args Pointer to capture_callback_args_t from FSP VIN driver
 *********************************************************************************************************************/
void vin0_callback(capture_callback_args_t *p_args)
{
    /* Forward to port layer handler which tracks statistics and buffer state */
    vin_port_callback((void *)p_args);
}

/**********************************************************************************************************************
 * Function Name: mipi_csi0_callback
 * Description  : MIPI CSI-2 callback handler (FSP-registered)
 *
 * @details
 * This function is declared in ra_gen/common_data.h and registered as the
 * MIPI CSI callback in the FSP-generated configuration (g_mipi_csi0_cfg.p_callback).
 * It is called from ISR context when a MIPI CSI-2 event occurs.
 *
 * Events are forwarded to csi2_port_callback() in the port layer which
 * handles error counting, frame sync tracking, and diagnostics.
 *
 * Events:
 *   - MIPI_CSI_EVENT_DATA_LANE: Data lane errors (ECC, escape, control)
 *   - MIPI_CSI_EVENT_FRAME_DATA: Frame data errors
 *   - MIPI_CSI_EVENT_VIRTUAL_CHANNEL: Virtual channel status (frame start/end, errors)
 *   - MIPI_CSI_EVENT_POWER: Power management events
 *   - MIPI_CSI_EVENT_SHORT_PACKET_FIFO: Short packet FIFO overflow
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/camera_thread_entry.c:948-1001
 *
 * @param p_args Pointer to mipi_csi_callback_args_t from FSP MIPI CSI driver
 *********************************************************************************************************************/
void mipi_csi0_callback(mipi_csi_callback_args_t *p_args)
{
    /* Forward to port layer handler which tracks error counters and frame events */
    csi2_port_callback((void *)p_args);
}

/**********************************************************************************************************************
 * Function Name: camera_thread_is_initialized
 * Description  : Check if camera thread has completed initialization
 *
 * @details Thread-safe query of camera initialization state.
 *          Can be called from other threads (e.g., ntshell_thread via
 *          "camera status" command, or lvgl_thread for display updates).
 *
 * @retval true  Camera thread has completed initialization and capture is running
 * @retval false Camera thread has not yet completed initialization or init failed
 *********************************************************************************************************************/
bool camera_thread_is_initialized(void)
{
    return s_camera_initialized;
}

/**********************************************************************************************************************
 * Function Name: camera_thread_has_error
 * Description  : Check if camera thread encountered an initialization error
 *
 * @details Thread-safe query of camera error state.
 *
 * @retval true  Camera initialization failed
 * @retval false No initialization error (may still be initializing)
 *********************************************************************************************************************/
bool camera_thread_has_error(void)
{
    return s_camera_init_error;
}

/**********************************************************************************************************************
 * Function Name: camera_thread_i2c_done
 * Description  : Check if camera thread has completed all I2C bus operations
 *
 * @details Thread-safe query of camera I2C completion state.
 *          Called by lv_port_indev_init() to synchronize IIC1 bus access.
 *
 *          Both g_i2c_master_camera_ctrl (camera) and g_i2c_master0_ctrl (touch)
 *          use IIC channel 1. Opening both causes IRQ context conflicts where
 *          R_BSP_IrqCfgEnable() overwrites the IRQ context, routing callbacks to
 *          the wrong handler. The camera must finish all I2C and close its master
 *          before the touch panel opens its I2C master.
 *
 *          This is also set to true if camera init fails (after I2C cleanup),
 *          so the touch panel is not blocked indefinitely.
 *
 * Reference: Issue #93 root cause - IIC1 channel conflict
 *
 * @retval true  Camera I2C operations complete, IIC1 is available
 * @retval false Camera I2C operations still in progress
 *********************************************************************************************************************/
bool camera_thread_i2c_done(void)
{
    return s_camera_i2c_done;
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Log a message to the console from camera thread context
 *
 * @param msg Null-terminated message string
 */
static void camera_thread_log(const char *msg)
{
    print_to_console((char_t *)msg);
}

/**
 * Log an FSP error with context description
 *
 * @param context Description of the operation that failed
 * @param err     FSP error code
 */
static void camera_thread_log_fsp_err(const char *context, fsp_err_t err)
{
    char buf[CAMERA_PRINT_BUF_SIZE];
    snprintf(buf, sizeof(buf), "  ERROR: %s failed (err=0x%lX).\r\n",
             context, (unsigned long)err);
    camera_thread_log(buf);
}

/**********************************************************************************************************************
 * Function Name: camera_greenpak_init
 * Description  : Check and initialize GreenPAK (SLG46824) IO4/IO5 configuration
 *
 * @details The GreenPAK on the Camera Expansion Board has IO4/IO5 pins that
 *          affect signal routing. This function reads the EEPROM configuration
 *          and, if the values don't match expectations, writes corrected values
 *          to the GreenPAK RAM (volatile, lost on power cycle but sufficient
 *          for the current session).
 *
 *          Uses the camera I2C master with temporary slave address change.
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/board_greenpak.c:71-130
 *
 * @retval true   GreenPAK IO4/IO5 are correctly configured (or repaired)
 * @retval false  Failed to communicate with GreenPAK
 *********************************************************************************************************************/
static bool camera_greenpak_init(void)
{
    fsp_err_t err;

    /* Open camera I2C master if not already open */
    if (0 == g_i2c_master_camera_ctrl.open)
    {
        err = R_IIC_MASTER_Open(&g_i2c_master_camera_ctrl, &g_i2c_master_camera_cfg);
        if (FSP_SUCCESS != err)
        {
            return false;
        }
    }

    /* Switch slave address to GreenPAK EEPROM */
    err = R_IIC_MASTER_SlaveAddressSet(&g_i2c_master_camera_ctrl,
                                        GREENPAK_I2C_ADDR_EEPROM,
                                        I2C_MASTER_ADDR_MODE_7BIT);
    if (FSP_SUCCESS != err)
    {
        goto restore_addr;
    }

    /* Read IO4 and IO5 configuration from EEPROM (registers 0x66-0x67) */
    uint8_t eeprom_vals[2] = {0, 0};
    for (uint32_t i = 0; i < 2; i++)
    {
        uint8_t reg_addr = GREENPAK_REG_START + (uint8_t)i;

        /* Write register address */
        err = R_IIC_MASTER_Write(&g_i2c_master_camera_ctrl, &reg_addr, 1, true);
        if (FSP_SUCCESS != err)
        {
            goto restore_addr;
        }

        /* R-005: xEventGroupWaitBits -> ov5640_i2c_wait_complete (uT-Kernel flag) */
        if (FSP_SUCCESS != ov5640_i2c_wait_complete())
        {
            goto restore_addr;
        }

        /* Read register value */
        err = R_IIC_MASTER_Read(&g_i2c_master_camera_ctrl, &eeprom_vals[i], 1, false);
        if (FSP_SUCCESS != err)
        {
            goto restore_addr;
        }

        if (FSP_SUCCESS != ov5640_i2c_wait_complete())
        {
            goto restore_addr;
        }
    }

    /* Check if IO4/IO5 are configured correctly */
    uint8_t io4_ok = (eeprom_vals[0] & GREENPAK_IO4_EXPECTED) == GREENPAK_IO4_EXPECTED;
    uint8_t io5_ok = (eeprom_vals[1] & GREENPAK_IO5_EXPECTED) == GREENPAK_IO5_EXPECTED;

    if (!io4_ok || !io5_ok)
    {
        /* IO4/IO5 not configured - repair via RAM write */
        char buf[CAMERA_PRINT_BUF_SIZE];
        snprintf(buf, sizeof(buf),
                 "  GreenPAK EEPROM: IO4=0x%02X IO5=0x%02X (repairing...)\r\n",
                 eeprom_vals[0], eeprom_vals[1]);
        camera_thread_log(buf);

        /* Compute corrected values */
        uint8_t ram_vals[2];
        ram_vals[0] = (eeprom_vals[0] & GREENPAK_IO4_MASK) | GREENPAK_IO4_EXPECTED;
        ram_vals[1] = (eeprom_vals[1] & GREENPAK_IO5_MASK) | GREENPAK_IO5_EXPECTED;

        /* Switch to GreenPAK RAM address */
        err = R_IIC_MASTER_SlaveAddressSet(&g_i2c_master_camera_ctrl,
                                            GREENPAK_I2C_ADDR_RAM,
                                            I2C_MASTER_ADDR_MODE_7BIT);
        if (FSP_SUCCESS != err)
        {
            goto restore_addr;
        }

        /* Write corrected IO4/IO5 values to RAM */
        for (uint32_t i = 0; i < 2; i++)
        {
            uint8_t wr_data[2] = { GREENPAK_REG_START + (uint8_t)i, ram_vals[i] };

            err = R_IIC_MASTER_Write(&g_i2c_master_camera_ctrl, wr_data, 2, false);
            if (FSP_SUCCESS != err)
            {
                goto restore_addr;
            }

            /* R-005: xEventGroupWaitBits -> ov5640_i2c_wait_complete (uT-Kernel flag) */
            if (FSP_SUCCESS != ov5640_i2c_wait_complete())
            {
                goto restore_addr;
            }
        }
    }

    /* Restore slave address to OV5640 */
    R_IIC_MASTER_SlaveAddressSet(&g_i2c_master_camera_ctrl,
                                  OV5640_I2C_ADDR, I2C_MASTER_ADDR_MODE_7BIT);
    return true;

restore_addr:
    R_IIC_MASTER_SlaveAddressSet(&g_i2c_master_camera_ctrl,
                                  OV5640_I2C_ADDR, I2C_MASTER_ADDR_MODE_7BIT);
    return false;
}

/**********************************************************************************************************************
 * Function Name: camera_board_switch_init
 * Description  : Initialize PI4IOE5V6408 I2C I/O expander (SW4 on EK-RA8P1)
 *
 * @details Configures the PI4IOE5V6408 to take all GPIO pins out of Hi-Z state
 *          and set the correct output levels for board signal routing (including
 *          MIPI). Without this initialization, the I/O expander outputs are in
 *          Hi-Z mode and MIPI signals cannot be properly routed to the MCU.
 *
 *          The function opens the camera I2C master if needed, temporarily
 *          switches the slave address to the PI4IOE5V6408 (0x43), writes three
 *          configuration registers, then restores the slave address to OV5640.
 *
 *          Register configuration (matching reference project):
 *            Output (0x05) = 0xFC : bits[7:2]=HIGH, bits[1:0]=LOW
 *            Hi-Z   (0x07) = 0x00 : All pins driven (not Hi-Z)
 *            Dir    (0x03) = 0xFF : All pins configured as output
 *
 * Reference: reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/board_cfg_switch.c:67-107
 *            reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/common_init.c:96
 *
 * @retval true   PI4IOE5V6408 initialized successfully
 * @retval false  Initialization failed (I2C error or timeout)
 *********************************************************************************************************************/
static bool camera_board_switch_init(void)
{
    fsp_err_t err;

    /* Open camera I2C master if not already open */
    if (0 == g_i2c_master_camera_ctrl.open)
    {
        err = R_IIC_MASTER_Open(&g_i2c_master_camera_ctrl, &g_i2c_master_camera_cfg);
        if (FSP_SUCCESS != err)
        {
            return false;
        }
    }

    /* Switch slave address to PI4IOE5V6408 */
    err = R_IIC_MASTER_SlaveAddressSet(&g_i2c_master_camera_ctrl,
                                        BOARD_SWITCH_I2C_ADDR,
                                        I2C_MASTER_ADDR_MODE_7BIT);
    if (FSP_SUCCESS != err)
    {
        return false;
    }

    /*
     * Write 3 registers to configure the I/O expander.
     * Each write: [register_address, value] (2 bytes, no restart).
     */
    static const uint8_t switch_regs[][2] = {
        { BOARD_SWITCH_REG_OUTPUT, BOARD_SWITCH_OUTPUT_VALUE },  /* Output levels */
        { BOARD_SWITCH_REG_HIZ,    BOARD_SWITCH_HIZ_VALUE },     /* Disable Hi-Z */
        { BOARD_SWITCH_REG_DIR,    BOARD_SWITCH_DIR_VALUE },     /* All outputs */
    };

    for (uint32_t i = 0; i < (sizeof(switch_regs) / sizeof(switch_regs[0])); i++)
    {
        err = R_IIC_MASTER_Write(&g_i2c_master_camera_ctrl,
                                  switch_regs[i], 2, false);
        if (FSP_SUCCESS != err)
        {
            return false;
        }

        /* Wait for I2C transfer completion.
         * R-005: xEventGroupWaitBits -> ov5640_i2c_wait_complete (uT-Kernel flag) */
        if (FSP_SUCCESS != ov5640_i2c_wait_complete())
        {
            return false;
        }
    }

    /* Restore slave address to OV5640 for subsequent camera operations */
    R_IIC_MASTER_SlaveAddressSet(&g_i2c_master_camera_ctrl,
                                  OV5640_I2C_ADDR,
                                  I2C_MASTER_ADDR_MODE_7BIT);

    return true;
}
