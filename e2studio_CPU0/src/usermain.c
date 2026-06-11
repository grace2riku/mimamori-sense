/*
 * usermain.c
 *
 * μT-Kernel 3.0 ユーザメインプログラム（R-003: ブート・OS 起動の移行）
 *
 * 本ファイルは FreeRTOS から μT-Kernel 3.0（BSP2）への移行の最初のステップ
 * （Issue #153 / R-003）として、最小構成で μT-Kernel 3.0 を起動・動作させる。
 *
 *  - μT-Kernel 3.0 の初期タスクから呼ばれる usermain() を実装する
 *    （BSP2 の WEAK 定義 mtk3_bsp2/mtkernel/kernel/usermain/usermain.c を
 *     強い定義で上書きする）。
 *  - LED 点滅タスク（FreeRTOS blinky_thread_entry の最小相当）を生成する。
 *  - tm_printf による起動ログ（SCI8 / 115200 / 8N1）を出力する。
 *
 * この段階では ntshell / camera / lvgl / ai_inference は起動しない。
 *
 * 起動経路（方式A）:
 *   R_BSP_WarmStart(BSP_WARM_START_POST_C)   ← src/hal_warmstart.c
 *     -> knl_start_mtkernel()                ← BSP2（戻らない）
 *       -> knl_main() -> 初期タスク -> usermain()  ← 本ファイル
 *
 * FreeRTOS 側の起動経路（ra_gen/main.c の vTaskStartScheduler()）には
 * 到達しない。ra_gen/ は編集しない方針のため、橋渡しは src/hal_warmstart.c で行う。
 *
 * Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <tk/tkernel.h>
#include <tm/tmonitor.h>

/* FSP / BSP API（LED 制御に R_BSP_PinWrite / bsp_leds_t を使用） */
#include "hal_data.h"

/*
 * ボード LED 定義（FSP 提供）。
 * FreeRTOS 版 blinky_thread_entry.c と同じく g_bsp_leds を使用する。
 */
extern bsp_leds_t g_bsp_leds;

/* ---------------------------------------------------------------------------
 *  LED 点滅タスク
 *
 *  FreeRTOS 版 blinky_thread_entry.c の最小相当。
 *  対応関係（FreeRTOS -> μT-Kernel 3.0、移行手順書 5 章 API 対応表）:
 *    vTaskDelay(configTICK_RATE_HZ / 2)  ->  tk_dly_tsk(500)   ※500ms（ミリ秒系）
 *
 *  注意: CPU1 起動（R_BSP_SecondaryCoreStart）は usermain() 側で実施済み
 *        （マルチコア・デバッグ整合のため。元 FreeRTOS の blinky 先頭呼び出しを移設）。
 * ------------------------------------------------------------------------- */
LOCAL void blink_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    /* LED 構成のコピー（FreeRTOS 版 blinky と同様） */
    bsp_leds_t leds = g_bsp_leds;

    /* このボードに LED が無ければここで待機 */
    if (0 == leds.led_count) {
        tm_putstring((UB *)"[blink_task] no LED on this board.\n");
        tk_slp_tsk(TMO_FEVR);
    }

    bsp_io_level_t pin_level = BSP_IO_LEVEL_LOW;

    while (1) {
        /* PFS レジスタへのアクセスを許可（BSP IO 関数の作法に従う） */
        R_BSP_PinAccessEnable();

        /* 全ボード LED を更新（マルチコアだが本タスクは CPU0 のみで動作） */
        for (uint32_t i = 0; i < leds.led_count; i++) {
            uint32_t pin = leds.p_leds[i];
            R_BSP_PinWrite((bsp_io_port_pin_t)pin, pin_level);
        }

        /* PFS レジスタを保護 */
        R_BSP_PinAccessDisable();

        /* 次回書き込み用にレベルをトグル */
        pin_level = (BSP_IO_LEVEL_LOW == pin_level)
                        ? BSP_IO_LEVEL_HIGH
                        : BSP_IO_LEVEL_LOW;

        /* 500ms 待機（FreeRTOS: vTaskDelay(configTICK_RATE_HZ / 2) 相当） */
        tk_dly_tsk(500);
    }
}

/* LED 点滅タスクの生成情報 */
LOCAL T_CTSK ctsk_blink = {
    .exinf   = NULL,
    .tskatr  = TA_HLNG | TA_RNG3,
    .task    = blink_task,
    .itskpri = 10,            /* 中位優先度（CNF_MAX_TSKPRI = 32） */
    .stksz   = 1024,
    /* USE_OBJECT_NAME = 0 のため dsname メンバは存在しない（初期化子に含めない） */
    .bufptr  = NULL,          /* USE_IMALLOC = 1 によりスタックは自動確保 */
};

/* ---------------------------------------------------------------------------
 *  usermain()
 *
 *  μT-Kernel 3.0 の初期タスクから呼ばれる（mtkernel/kernel/inittask/inittask.c）。
 *  BSP2 の WEAK 定義（mtkernel/kernel/usermain/usermain.c）を本強い定義で上書きする。
 *
 *  公式手順（bsp2_ra_fsp_jp.md 4.3）に従い、
 *    (1) usermain() は初期タスクから呼ばれるため、自タスクを待ち状態にする
 *        システムコールを多用しない（タスク生成と起動のみ行う）。
 *    (2) 実アプリは生成した初期タスク側で行い、usermain() 自身は終了させずに
 *        待ち状態（tk_slp_tsk(TMO_FEVR)）にする。
 *        usermain() が return すると μT-Kernel はシャットダウンするため。
 * ------------------------------------------------------------------------- */
EXPORT INT usermain(void)
{
    ID  tskid;
    ER  ercd;

    /* 起動ログ（SCI8 / 115200 / 8N1）。
     * tm_printf/tm_putstring は SCI8 を直接レジスタ操作で使用する
     * （mtk3_bsp2/sysdepend/ra_fsp/lib/libtm/ek_ra8p1/tm_com.c）。 */
    tm_putstring((UB *)"\n");
    tm_putstring((UB *)"==============================================\n");
    tm_putstring((UB *)" mimamori-sense  uT-Kernel 3.0 boot (R-003)\n");
    tm_putstring((UB *)"==============================================\n");
    tm_printf((UB *)"[usermain] uT-Kernel 3.0 started. LED count = %d\n",
              (INT)g_bsp_leds.led_count);

    /* セカンダリコア（CPU1 / Cortex-M33）を起動する。
     *
     * RA8P1 では CPU1 はリセット保持で立ち上がり、CPU0 が R_BSP_SecondaryCoreStart()
     * を呼ぶまで解除されない。元の FreeRTOS 構成では blinky_thread_entry.c の先頭で
     * 呼んでいたが、方式A（main()/スケジューラ未到達）ではその経路が実行されないため、
     * ここ（μT-Kernel 初期タスク）で呼び CPU1 を解除する。
     *
     * これを呼ばないと CPU1 がリセット保持のままとなり、マルチコア・デバッグ
     * （Debug_Multicore Launch Group）の CPU1 接続が
     * 'monitor enable_stopped_notify_on_connect' でタイムアウトする。
     * 起動ガード条件は元の blinky と同一。 */
#if (0 == _RA_CORE) && (1 == BSP_MULTICORE_PROJECT) && !BSP_TZ_NONSECURE_BUILD
    R_BSP_SecondaryCoreStart();
    tm_putstring((UB *)"[usermain] secondary core (CPU1) started.\n");
#endif

    /* LED 点滅タスクを生成・起動 */
    tskid = tk_cre_tsk(&ctsk_blink);
    if (tskid <= E_OK) {
        /* 生成失敗（戻り値が負ならエラーコード） */
        tm_printf((UB *)"[usermain] tk_cre_tsk failed. ercd = %d\n", (INT)tskid);
        return -1;
    }

    ercd = tk_sta_tsk(tskid, 0);
    if (ercd != E_OK) {
        tm_printf((UB *)"[usermain] tk_sta_tsk failed. ercd = %d\n", (INT)ercd);
        return -1;
    }

    tm_putstring((UB *)"[usermain] blink_task created & started.\n");

    /* usermain() を終了させない（終了すると μT-Kernel がシャットダウンするため）。
     * 初期タスクは高優先度のため、ここで待ち状態に入れて他タスクへ実行を譲る。 */
    tk_slp_tsk(TMO_FEVR);

    return 0;
}
