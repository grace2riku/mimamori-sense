/*
* Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "hal_data.h"
#include "port/sdram_port.h"

FSP_CPP_HEADER
void R_BSP_WarmStart(bsp_warm_start_event_t event);

FSP_CPP_FOOTER

/* -------------------------------------------------------------------------
 * μT-Kernel 3.0 起動への橋渡し（R-003 / Issue #153）
 *
 * 採用方式: 方式A
 *   R_BSP_WarmStart(BSP_WARM_START_POST_C) の末尾で μT-Kernel を起動する。
 *   knl_start_mtkernel() は戻らない（knl_main -> 初期タスク -> usermain）ため、
 *   この後に呼ばれるはずの ra_gen/main.c の main()（FreeRTOS vTaskStartScheduler）
 *   には到達しない。これにより:
 *     - ra_gen/main.c / ra_gen/*_thread.c を一切編集せず（編集禁止方針）、
 *       src/ 配下のフックだけで μT-Kernel 起動へ切り替えられる。
 *     - FreeRTOS スレッド（blinky/ntshell/camera/lvgl/ai_inference）は
 *       生成・起動されない（main() に到達しないため自動的に無効化される）。
 *
 * BSP_WARM_START_POST_C は C ランタイム・システムクロック確立後、main() より前に
 * BSP から一度だけ呼ばれる。本フックの先頭でピン設定（R_IOPORT_Open）と
 * SDRAM 初期化を実施済みであり、LED 制御に必要な IOPORT は構成済みである。
 *
 * 最小構成（R-003）では g_hal_init()（FSP モジュール初期化）は実行しない:
 *   - LED 点滅は R_BSP_PinWrite（BSP 直接・モジュール不要）
 *   - tm_printf は SCI8 直接レジスタ操作（FSP モジュール不要）
 *   のため、FSP モジュール初期化に依存しない。
 *   後続ステップ（NT-Shell/カメラ等）で FSP モジュール（UART/I2C/MIPI 等）が
 *   必要になった時点で、g_hal_init() 相当の一度きり初期化を usermain() 側へ
 *   移設する（→ 移行手順書 7.1 / 7.2 以降）。
 *
 * 切り戻し: 下記マクロを 0 にすると本橋渡しを無効化し、従来の FreeRTOS 起動
 *   （ra_gen/main.c -> vTaskStartScheduler）に戻せる。
 * ------------------------------------------------------------------------- */
#define MIMAMORI_USE_MTKERNEL_BOOT  (1)

#if (MIMAMORI_USE_MTKERNEL_BOOT == 1)
/* BSP2 が提供する μT-Kernel 起動関数（戻らない）。
 * mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/sys_start.c */
extern void knl_start_mtkernel(void);
#endif

/*******************************************************************************************************************//**
 * This function is called at various points during the startup process.  This implementation uses the event that is
 * called right before main() to set up the pins.
 *
 * @param[in]  event    Where at in the start up process the code is currently at
 **********************************************************************************************************************/
void R_BSP_WarmStart (bsp_warm_start_event_t event)
{
    if (BSP_WARM_START_RESET == event)
    {
#if BSP_FEATURE_FLASH_LP_VERSION != 0

        /* Enable reading from data flash. */
        R_FACI_LP->DFLCTL = 1U;

        /* Would normally have to wait tDSTOP(6us) for data flash recovery. Placing the enable here, before clock and
         * C runtime initialization, should negate the need for a delay since the initialization will typically take more than 6us. */
#endif
    }

#if BSP_CFG_OSPI_B_STARTUP_ENABLED && defined(BSP_CFG_OSPI_B_STARTUP_FN)
    if (BSP_WARM_START_POST_CLOCK == event)
    {
        /* Setup OSPI_B SiP flash and initialize it. */
        R_BSP_OspiBInit(BSP_CFG_OSPI_B_STARTUP_FN, true);
    }
#endif

    if (BSP_WARM_START_POST_C == event)
    {
        /* C runtime environment and system clocks are setup. */

        /* Configure pins. */
        R_IOPORT_Open(&IOPORT_CFG_CTRL, &IOPORT_CFG_NAME);

#if BSP_CFG_SDRAM_ENABLED

        /* Setup SDRAM and initialize it. Must configure pins first.
         * R_BSP_SdramInit() performs the full SDRAM initialization sequence:
         *   1. SDCLK output enable
         *   2. PRECHARGE ALL command
         *   3. AUTO REFRESH x8
         *   4. MODE REGISTER SET (CAS latency=3, burst=1, sequential)
         *   5. Timing parameters (tRAS, tRCD, tRP, tWR, tCL)
         *   6. Auto-refresh enable
         *   7. SDRAM access enable
         * Reference: ra/fsp/src/bsp/mcu/all/bsp_sdram.c:59-147
         */
        R_BSP_SdramInit(true);

        /* Record initialization status with a quick sanity check */
        sdram_port_init();
#endif

#if (MIMAMORI_USE_MTKERNEL_BOOT == 1)
        /* μT-Kernel 3.0 を起動する（R-003 / 方式A）。
         * ピン設定・SDRAM 初期化が完了したこの位置で呼び出す。
         * knl_start_mtkernel() は戻らない（μT-Kernel が制御を握り、
         * 初期タスク経由で src/usermain.c の usermain() を実行する）。
         * よって以降の ra_gen/main.c（FreeRTOS）には到達しない。 */
        knl_start_mtkernel();

        /* ここには到達しない。万一戻った場合に備えてトラップする。 */
        while (1)
        {
            __asm volatile ("nop");
        }
#endif
    }
}
