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
 *   静的コンストラクタ（__init_array、GCC/Clang の __attribute__((constructor))）から
 *   μT-Kernel を起動する。コンストラクタは SystemInit() の終盤（SystemRuntimeInit(1)・
 *   TLS 初期化・I-Cache 無効化の後、main() より前）で実行される。
 *   knl_start_mtkernel() は戻らない（knl_main -> 初期タスク -> usermain）ため、
 *   この後に呼ばれるはずの ra_gen/main.c の main()（FreeRTOS vTaskStartScheduler）
 *   には到達しない。これにより:
 *     - ra_gen/main.c / ra_gen/*_thread.c を一切編集せず（編集禁止方針）、
 *       src/ 配下のフックだけで μT-Kernel 起動へ切り替えられる。
 *     - FreeRTOS スレッド（blinky/ntshell/camera/lvgl/ai_inference）は
 *       生成・起動されない（main() に到達しないため自動的に無効化される）。
 *
 * ■ 起動ポイントを POST_C 末尾から静的コンストラクタへ変更した理由（PR #163 codex 指摘 / P2）
 *   当初は R_BSP_WarmStart(BSP_WARM_START_POST_C) の末尾で knl_start_mtkernel() を
 *   呼んでいた。しかし POST_C フック（system.c: SystemInit 内）の直後には、戻らない
 *   ジャンプによってスキップされる重要な C ランタイム初期化が残っている:
 *     - SystemRuntimeInit(1): 外部メモリ(SDRAM)の .sdram ゼロクリア /
 *                             .sdram_from_flash・.sdram_ospi_data コピー
 *     - TLS 初期化（_init_tls / _set_tls）
 *   本リポジトリは BSP_CFG_C_RUNTIME_INIT=1 / BSP_CFG_SDRAM_ENABLED=1 で、
 *   .sdram に配置されるバッファ（例: ai_inference の model_buffer_int8）が存在する。
 *   POST_C で抜けると、後続の μT-Kernel タスクがこれらを未初期化のまま使う恐れがある。
 *
 *   当初は FSP の bsp_init()（SystemInit 最終段の weak フック）を上書きする案だったが、
 *   本ボードでは ra/board/ra8p1_ek/board_init.c が bsp_init() を強いシンボルで定義済みで
 *   （ra/ は編集禁止）、二重定義（duplicate symbol）になり使用できない。
 *   そこで上書き不要な静的コンストラクタを採用する。__init_array は SystemRuntimeInit(1)
 *   （system.c:476）の後（同:512-516）で実行されるため、外部メモリ初期化・TLS 初期化が
 *   完了した状態でカーネルへ入れる。無印 .init_array（優先度なし）は SORT 済みの優先度付き
 *   コンストラクタの後に実行される（fsp_gen.lld）。
 *   なお SDRAM コントローラの起動（R_BSP_SdramInit）は引き続き POST_C で行うため、
 *   「SDRAM 起動(POST_C) -> .sdram セクション初期化(SystemRuntimeInit) -> カーネル(コンストラクタ)」
 *   の順序が保たれる。
 *   （補足: bsp_irq_cfg()（ELC/NVIC 設定）はコンストラクタの後・main() 前に呼ばれるため
 *    本方式でも未実行となるが、これは当初の POST_C 方式でも同様で、R-003 は ELC 起動の
 *    割り込みを使わないため影響しない。必要になった段階で usermain() 側で対応する。）
 *
 * BSP_WARM_START_POST_C は C ランタイム・システムクロック確立後に BSP から一度だけ
 * 呼ばれる。本フックでピン設定（R_IOPORT_Open）と SDRAM 初期化を実施し、LED 制御に
 * 必要な IOPORT を構成しておく（実際のカーネル起動は後段のコンストラクタで行う）。
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

        /* 注意: μT-Kernel の起動はここ（POST_C）では行わない。
         * この直後に SystemInit が実行する SystemRuntimeInit(1)（外部 SDRAM の
         * .sdram ゼロクリア / .sdram_from_flash コピー）や TLS 初期化を飛ばさないよう、
         * 起動は SystemInit 終盤に実行される静的コンストラクタで行う
         * （本ファイル末尾 mimamori_start_mtkernel / 上部コメント参照・
         *  PR #163 codex 指摘 P2 への対応）。 */
    }
}

#if (MIMAMORI_USE_MTKERNEL_BOOT == 1)
/*******************************************************************************************************************//**
 * μT-Kernel 3.0 起動への橋渡し（R-003・方式A）。
 *
 * 静的コンストラクタ（__init_array）として登録し、FSP の SystemInit() 終盤
 * （SystemRuntimeInit(1) による外部 SDRAM セクション初期化・TLS 初期化の後、
 * bsp_irq_cfg()/main() より前）に実行させる。これにより C ランタイム初期化
 * （特に .sdram / .sdram_from_flash の初期化）が完了した状態でカーネルへ制御を渡す。
 * 上部コメント参照（PR #163 codex 指摘 P2 への対応 / bsp_init() は本ボードで強い
 * 定義済みのため使用不可）。
 **********************************************************************************************************************/
__attribute__((constructor)) static void mimamori_start_mtkernel (void)
{
    /* μT-Kernel 3.0 を起動する。knl_start_mtkernel() は戻らない
     * （μT-Kernel が制御を握り、初期タスク経由で src/usermain.c の usermain() を実行）。
     * よって以降の ra_gen/main.c（FreeRTOS vTaskStartScheduler）には到達しない。 */
    knl_start_mtkernel();

    /* ここには到達しない。万一戻った場合に備えてトラップする。 */
    while (1)
    {
        __asm volatile ("nop");
    }
}
#endif
