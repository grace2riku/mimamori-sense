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
 * （R-003 時点では blink のみ起動。以降のステップで ntshell（R-004）/ camera（R-005）/
 *  lvgl（R-006）/ ai_inference（R-007）のタスク生成・起動を本ファイルへ追加済み。）
 *
 * 起動経路（方式A）:
 *   R_BSP_WarmStart(BSP_WARM_START_POST_C)   ← src/hal_warmstart.c
 *     -> knl_start_mtkernel()                ← BSP2（戻らない）
 *       -> knl_main() -> 初期タスク -> usermain()  ← 本ファイル
 *
 * FreeRTOS 側の起動経路（ra_gen/main.c の vTaskStartScheduler()）には
 * 到達しない。ra_gen/ は編集しない方針のため、橋渡しは src/hal_warmstart.c で行う。
 *
 * Issue #186 Step 2/3: FSP の RTOS 設定を No RTOS に変更し FreeRTOS を撤去した。
 * ra_gen/main.c は hal_entry() を呼ぶだけになり、ra_gen/*_thread.{c,h} と
 * src/blinky_thread_entry.c は無くなった。本ファイル中の「FreeRTOS 版
 * blinky_thread_entry.c」等の参照は移行元を示す履歴記述であり、当該ファイルは
 * 既にリポジトリに存在しない（git 履歴で参照のこと）。
 *
 * Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <tk/tkernel.h>
#include <tm/tmonitor.h>

/* FSP / BSP API（LED 制御に R_BSP_PinWrite / bsp_leds_t を使用） */
#include "hal_data.h"

/* D/AVE 2D ヒープ（#186 Step 1 / #178）。DRW_CFG_CUSTOM_MALLOC 無効時は
 * d1_heap_init() は何もせず E_OK を返すスタブ（src/d1_heap_mtkernel.c）。 */
#include "d1_heap_mtkernel.h"

/*
 * ボード LED 定義（FSP 提供）。
 * FreeRTOS 版 blinky_thread_entry.c と同じく g_bsp_leds を使用する。
 */
extern bsp_leds_t g_bsp_leds;

/*
 * NT-Shell タスク本体（R-004 / src/ntshell_thread_entry.c）。
 * uT-Kernel タスク形式 void ntshell_task(INT stacd, void *exinf)。
 * uT-Kernel タスク形式のプロトタイプをここで直接宣言する（もとは ra_gen/ntshell_thread.h が
 * FreeRTOS 形式の宣言のみを提供していたため。同ヘッダは #186 Step 2 で生成されなくなった）。
 */
extern void ntshell_task(INT stacd, void *exinf);

/*
 * カメラ タスク本体（R-005 / src/camera_thread_entry.c）。
 * uT-Kernel タスク形式 void camera_task(INT stacd, void *exinf)。
 * uT-Kernel タスク形式のプロトタイプをここで直接宣言する（ntshell_task と同様。
 * ra_gen/camera_thread.h は #186 Step 2 で生成されなくなった）。
 */
extern void camera_task(INT stacd, void *exinf);

/*
 * LVGL タスク本体（R-006 / src/lvgl_thread_entry.c）。
 * uT-Kernel タスク形式 void lvgl_task(INT stacd, void *exinf)。
 * uT-Kernel タスク形式のプロトタイプをここで直接宣言する（ntshell/camera と同様。
 * ra_gen/lvgl_thread.h は #186 Step 2 で生成されなくなった）。
 */
extern void lvgl_task(INT stacd, void *exinf);

/*
 * AI 推論タスク本体（R-007 / src/ai_inference_thread_entry.c）。
 * uT-Kernel タスク形式 void ai_inference_task(INT stacd, void *exinf)。
 * uT-Kernel タスク形式のプロトタイプをここで直接宣言する（ntshell/camera/lvgl と同様。
 * ra_gen/ai_inference_thread.h は #186 Step 2 で生成されなくなった）。
 */
extern void ai_inference_task(INT stacd, void *exinf);

/* ---------------------------------------------------------------------------
 *  LED 点滅タスク
 *
 *  FreeRTOS 版 blinky_thread_entry.c の最小相当。
 *  対応関係（FreeRTOS -> μT-Kernel 3.0、移行手順書 5 章 API 対応表）:
 *    vTaskDelay(configTICK_RATE_HZ / 2)  ->  tk_dly_tsk(500)   ※500ms（ミリ秒系）
 *
 *  点滅対象 LED（R-004 で移行前の挙動へ復元）:
 *    元の blinky のマルチコア分岐（#else）を踏襲し、マルチコア構成では
 *    自コア（_RA_CORE）のインデックスの LED 1 個だけを点滅させる。CPU0 は
 *    LED1(Blue, P600) のみ。なお LED2(Green, P303) は CPU1（FreeRTOS のまま・
 *    本移行の対象外）の blinky が点滅させる（_RA_CORE=1 → p_leds[1]）。
 *    LED3(Red, PA07) はどちらのコアも点滅させないため、led コマンド（S-011）
 *    で自由に操作できるのは Red のみ（移行前と同じ競合関係）。
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

        /* 元の FreeRTOS blinky_thread_entry.c のマルチコア分岐を踏襲する。
         * 本プロジェクトはデュアルコア（BSP_NUMBER_OF_CORES > 1）なので #else 側が
         * 有効となり、各コアは「自分のコア番号（_RA_CORE）のインデックスの LED
         * 1 個だけ」を点滅させる。CPU0（_RA_CORE = 0）は p_leds[0] = LED1(Blue, P600)
         * のみ。LED2(Green) は CPU1（FreeRTOS のまま）の blinky が点滅させるため、
         * led コマンドで自由に操作できるのは誰も点滅させない LED3(Red, PA07) のみ
         * となる（移行前と同じ挙動。R-004 で復元）。 */
#if BSP_NUMBER_OF_CORES == 1
        /* 単一コア構成: 全ボード LED を点滅 */
        for (uint32_t i = 0; i < leds.led_count; i++) {
            R_BSP_PinWrite((bsp_io_port_pin_t)leds.p_leds[i], pin_level);
        }
#else
        /* マルチコア構成: 自コアのインデックスの LED 1 個のみ点滅 */
        R_BSP_PinWrite((bsp_io_port_pin_t)leds.p_leds[_RA_CORE], pin_level);
#endif

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
 *  NT-Shell タスクの生成情報（R-004 / Issue #154）
 *
 *  本体は src/ntshell_thread_entry.c の ntshell_task()。
 *  - 優先度: blink タスク（itskpri=10）より低優先度（数値大 = 12）。
 *    NT-Shell は対話コンソールであり LED 点滅よりリアルタイム性が低いため。
 *  - スタック: FreeRTOS 版スレッド設定と同じ 4096 バイト
 *    （ntshell_execute / snprintf / コマンドディスパッチで消費するため）。
 *  USE_OBJECT_NAME = 0 のため dsname メンバは存在しない（初期化子に含めない）。
 * ------------------------------------------------------------------------- */
LOCAL T_CTSK ctsk_ntshell = {
    .exinf   = NULL,
    .tskatr  = TA_HLNG | TA_RNG3,
    .task    = ntshell_task,
    .itskpri = 12,            /* blink(10) より低優先度 */
    .stksz   = 4096,
    .bufptr  = NULL,          /* USE_IMALLOC = 1 によりスタックは自動確保 */
};

/* ---------------------------------------------------------------------------
 *  カメラ タスクの生成情報（R-005 / Issue #155）
 *
 *  本体は src/camera_thread_entry.c の camera_task()。
 *  - 優先度: itskpri=11。blink(10) と ntshell(12) の中間。
 *    カメラ初期化（I2C シーケンス・VIN/MIPI 起動）は NT-Shell の対話処理より
 *    リアルタイム性が高いため NT-Shell より高優先度（数値小）に置く。一方、
 *    LED 点滅（blink=10）は周期確定が重要なため、カメラはそれと同等〜やや低く
 *    （数値大 = 11）して LED 点滅周期への影響を避ける。
 *  - スタック: 4096 バイト（FreeRTOS 版 camera_thread と同等。snprintf による
 *    ログ整形と OV5640 の I2C シーケンスでスタックを消費するため）。
 *  USE_OBJECT_NAME = 0 のため dsname メンバは存在しない（初期化子に含めない）。
 * ------------------------------------------------------------------------- */
LOCAL T_CTSK ctsk_camera = {
    .exinf   = NULL,
    .tskatr  = TA_HLNG | TA_RNG3,
    .task    = camera_task,
    .itskpri = 11,            /* blink(10) と ntshell(12) の中間 */
    .stksz   = 4096,
    .bufptr  = NULL,          /* USE_IMALLOC = 1 によりスタックは自動確保 */
};

/* ---------------------------------------------------------------------------
 *  LVGL タスクの生成情報（R-006 / Issue #156）
 *
 *  本体は src/lvgl_thread_entry.c の lvgl_task()。
 *  - 優先度: itskpri=14。優先度表（スパイク報告書 5.7）:
 *      blink=10 / camera=11 / ntshell=12 / dave2d・swdraw=13 / lvgl=14
 *    LVGL OSAL（src/lv_os_mtkernel.c）が lv_init() 中に生成する描画スレッド
 *    （dave2d / swdraw、LV_THREAD_PRIO_HIGH → itskpri=13、各 8KB スタック）は
 *    LVGL の設計どおり lvgl_task より 1 段高優先（普段は sync 待ちで眠っている）。
 *  - スタック: 8192 バイト（FreeRTOS 版 lvgl_thread と同値 ―
 *    ra_gen/lvgl_thread.c の lvgl_thread_stack[8192]。同ファイルは
 *    Issue #186 Step 2 で生成されなくなった）。
 *  USE_OBJECT_NAME = 0 のため dsname メンバは存在しない（初期化子に含めない）。
 * ------------------------------------------------------------------------- */
LOCAL T_CTSK ctsk_lvgl = {
    .exinf   = NULL,
    .tskatr  = TA_HLNG | TA_RNG3,
    .task    = lvgl_task,
    .itskpri = 14,            /* 描画スレッド(13) より低く、最低優先度ではない */
    .stksz   = 8192,
    .bufptr  = NULL,          /* USE_IMALLOC = 1 によりスタックは自動確保 */
};

/* ---------------------------------------------------------------------------
 *  AI 推論タスクの生成情報（R-007 / Issue #157）
 *
 *  本体は src/ai_inference_thread_entry.c の ai_inference_task()。
 *  - 優先度: itskpri=15。優先度表（R-006 までの確定分 + R-007）:
 *      blink=10 / camera=11 / ntshell=12 / dave2d・swdraw=13 / lvgl=14 / ai=15
 *    FreeRTOS では ai_inference(2) は lvgl(2) と同列だったが、AI 推論は
 *    1 サイクルごとに 25ms 譲る設計（人の反応時間に対して十分な周期）であり、
 *    描画パイプライン（dave2d=13 / lvgl=14）を妨げないよう lvgl より 1 段
 *    低い 15 とする。NPU 推論中の CPU 待ち（ethosu_semaphore_take の
 *    __WFE ループ ― ethosu_driver.c:205-223）はブロックではなく実行状態の
 *    まま CPU を眠らせるが、割り込みで高優先度タスクが起きれば即座に
 *    プリエンプトされるため、最下位の本タスクが他タスクを飢えさせることはない。
 *  - スタック: 16384 バイト（FreeRTOS 版 ai_inference_thread と同値 ―
 *    ra_gen/ai_inference_thread.c の ai_inference_thread_stack[0x4000]。
 *    同ファイルは Issue #186 Step 2 で生成されなくなった。
 *    MERA 推論・後処理（NMS/expf）・snprintf ログ整形で消費するため）。
 *  USE_OBJECT_NAME = 0 のため dsname メンバは存在しない（初期化子に含めない）。
 * ------------------------------------------------------------------------- */
LOCAL T_CTSK ctsk_ai_inference = {
    .exinf   = NULL,
    .tskatr  = TA_HLNG | TA_RNG3,
    .task    = ai_inference_task,
    .itskpri = 15,            /* lvgl(14) より低い最下位グループ */
    .stksz   = 16384,
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
    tm_putstring((UB *)" mimamori-sense  uT-Kernel 3.0 boot (R-007)\n");
    tm_putstring((UB *)"==============================================\n");
    tm_printf((UB *)"[usermain] uT-Kernel 3.0 started. LED count = %d\n",
              (INT)g_bsp_leds.led_count);

    /* ---------------------------------------------------------------------
     * FSP 割り込み構成（ELC -> NVIC マッピング）の復元 ― R-004 / Issue #154
     *
     * 方式A（静的コンストラクタで knl_start_mtkernel() を呼ぶ）では
     * FSP の SystemInit() が __init_array 実行直後に行う bsp_irq_cfg()
     * （system.c:525）が実行されない（その前にカーネルへ制御が移るため）。
     * bsp_irq_cfg() は R_ICU->IELSR[]（ELC イベント -> NVIC ベクタの対応）を
     * g_interrupt_event_link_select[] から設定する FSP 公開関数で、これが
     * 未実行だと R_SCI_B_UART_Open()（jlink_console_init 内）が NVIC を有効化しても
     * SCI8 の TXI/RXI/TEI/ERI が NVIC ベクタに結線されず、UART 割り込みが発火しない
     * （R_SCI_B_UART_Write 後に g_transfer_done が立たず jlink_console_write がハングする）。
     *
     * usermain() は uT-Kernel 初期タスクから一度だけ呼ばれるため、他タスク生成前の
     * ここで bsp_irq_cfg() を一度だけ呼び、IELSR マッピングを構成する。
     * （ra_gen は編集禁止のため src 側から FSP 公開関数を呼んで対応 ― 手順書 7.1/7.2）。
     * --------------------------------------------------------------------- */
    bsp_irq_cfg();
    tm_putstring((UB *)"[usermain] bsp_irq_cfg() done (ELC->NVIC IELSR configured).\n");

    /* ---------------------------------------------------------------------
     * D/AVE 2D ヒープ（可変長メモリプール）を生成する ― Issue #186 Step 1 / #178
     *
     * FSP の r_drw_memory.c は DRW_CFG_CUSTOM_MALLOC が有効なとき D2 ヒープの
     * 確保を d1_malloc()/d1_free()（src/d1_heap_mtkernel.c）へ委譲する。実体は
     * uT-Kernel の可変長メモリプール（tk_cre_mpl / tk_get_mpl / tk_rel_mpl）。
     *
     * 生成タイミング（重要）: d1_allocmem() へ到達する唯一の経路は
     *   lvgl_task -> lv_init()（src/lvgl_thread_entry.c:127）-> lv_draw_dave2d_init()
     *   -> lv_dave2d_init() -> d2_opendevice()/d2_inithw()
     * であり、lvgl_task の生成（本関数の後段 tk_cre_tsk(&ctsk_lvgl)）より前に
     * ここで生成しておけば、最初の確保時にプールは必ず存在する。
     * tk_cre_mpl() は CHECK_DISPATCH() を行うためタスクコンテキストが必要だが、
     * usermain() は uT-Kernel 初期タスクから呼ばれるので条件を満たす。
     * --------------------------------------------------------------------- */
    ercd = d1_heap_init();
    if (ercd != E_OK) {
        /* Dave2D（LVGL 描画）が確保できず LCD 表示が成立しないため起動を止める。 */
        tm_printf((UB *)"[usermain] d1_heap_init failed. ercd = %d\n", (INT)ercd);
        return -1;
    }

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

    /* ---------------------------------------------------------------------
     * NT-Shell タスクを生成・起動（R-004 / Issue #154）
     *
     * SCI8 一本化: ここまで（起動ログ）は T-Monitor（tm_printf / SCI8 直接レジスタ）で出力。
     * NT-Shell タスクが起動して jlink_console_init()（R_SCI_B_UART_Open, channel=8）で
     * SCI8 を FSP UART として開いた後は、SCI8 は jlink_console が専有する。
     * tm_printf/tm_putstring（T-Monitor）は SCI8 を直接初期化・送信するため、ここ以降の
     * usermain では T-Monitor を使わない（同一 SCI8 の二重送信による競合を避ける）。
     * tm_putstring はポーリング送信（TEND 待ち）でブロッキングのため、上の起動ログは
     * この時点で送信完了済み。
     * --------------------------------------------------------------------- */
    tskid = tk_cre_tsk(&ctsk_ntshell);
    if (tskid <= E_OK) {
        tm_printf((UB *)"[usermain] tk_cre_tsk(ntshell) failed. ercd = %d\n", (INT)tskid);
        return -1;
    }

    ercd = tk_sta_tsk(tskid, 0);
    if (ercd != E_OK) {
        tm_printf((UB *)"[usermain] tk_sta_tsk(ntshell) failed. ercd = %d\n", (INT)ercd);
        return -1;
    }

    tm_putstring((UB *)"[usermain] ntshell_task created & started. (SCI8 -> NT-Shell)\n");

    /* ---------------------------------------------------------------------
     * カメラ タスクを生成・起動（R-005 / Issue #155）
     *
     * SCI8 一本化（重要）: usermain（初期タスク）の起動ログは blink / ntshell と
     * 同様に **T-Monitor（tm_putstring/tm_printf、SCI8 直接レジスタ・ポーリング送信）**
     * で出力する。tm_putstring はブロッキング（TEND 待ち）で送信完了してから戻るため、
     * usermain の全ログは tk_slp_tsk(TMO_FEVR) で待ち状態に入る前に送信し切れる。
     * その後に NT-Shell タスクが jlink_console_init()（R_SCI_B_UART_Open, channel=8）で
     * SCI8 を FSP UART として開く。これにより T-Monitor と jlink_console の SCI8 競合が
     * 起きない。
     *
     * ここで print_to_console（jlink_console）を使うと、本コード実行時点では NT-Shell が
     * まだ jlink_console_init() を実行しておらず SCI8 が未オープンのため、print_to_console が
     * 内部で jlink_configured() 待ちに入り tk_dly_tsk(1) で譲る。すると NT-Shell が SCI8 を
     * 開いてバナー出力を始め、復帰した usermain と同時に SCI8 へ書き込み**競合・文字化け**
     * （実機で確認: 'm□' のような化け＋本ログ消失）が起きる。よって T-Monitor を使う。
     * --------------------------------------------------------------------- */
    tskid = tk_cre_tsk(&ctsk_camera);
    if (tskid <= E_OK) {
        tm_printf((UB *)"[usermain] tk_cre_tsk(camera) failed. ercd = %d\n", (INT)tskid);
        return -1;
    }

    ercd = tk_sta_tsk(tskid, 0);
    if (ercd != E_OK) {
        tm_printf((UB *)"[usermain] tk_sta_tsk(camera) failed. ercd = %d\n", (INT)ercd);
        return -1;
    }

    tm_putstring((UB *)"[usermain] camera_task created & started.\n");

    /* ---------------------------------------------------------------------
     * LVGL タスクを生成・起動（R-006 / Issue #156）
     *
     * camera の後に起動する（タッチ初期化 lv_port_indev_init() が
     * camera_thread_i2c_done() を待って IIC1 を引き継ぐため、起動順は
     * 機能上の制約ではないが、移行前の FreeRTOS 構成と同じ依存関係を保つ）。
     * ログは blink / ntshell / camera と同じく T-Monitor（tm_putstring /
     * tm_printf、ポーリング送信）で出力する（SCI8 一本化 ― R-005 の注意参照）。
     *
     * lvgl_task は lv_init() の中で LVGL OSAL（lv_os_mtkernel.c）経由の
     * 追加タスク（dave2d / swdraw、itskpri=13）と複数の mutex / semaphore を
     * 生成する。カーネル資源数は CNF_MAX_MTXID=16 へ拡張済み
     * （mtk3_bsp2/config/config.h ― R-006）。
     * --------------------------------------------------------------------- */
    tskid = tk_cre_tsk(&ctsk_lvgl);
    if (tskid <= E_OK) {
        tm_printf((UB *)"[usermain] tk_cre_tsk(lvgl) failed. ercd = %d\n", (INT)tskid);
        return -1;
    }

    ercd = tk_sta_tsk(tskid, 0);
    if (ercd != E_OK) {
        tm_printf((UB *)"[usermain] tk_sta_tsk(lvgl) failed. ercd = %d\n", (INT)ercd);
        return -1;
    }

    tm_putstring((UB *)"[usermain] lvgl_task created & started.\n");

    /* ---------------------------------------------------------------------
     * AI 推論タスクを生成・起動（R-007 / Issue #157）
     *
     * lvgl の後に起動する（移行前の ra_gen/main.c でも ai_inference_thread_create()
     * は最後だった。機能上の起動順制約はなく、AI タスクは内部で
     * camera_thread_is_initialized() のポーリングと jlink_configured() 待ちで
     * 依存先の準備を待つ）。AI 同期用イベントフラグ g_ai_app_flgid は
     * ai_inference_task 自身が先頭で tk_cre_flg により生成し、camera_display
     * （LVGL タイマ）側は ID が 0 の間は AI 連携ブロックをスキップする。
     * ログは他タスクと同じく T-Monitor（SCI8 一本化 ― R-005 の注意参照）。
     * --------------------------------------------------------------------- */
    tskid = tk_cre_tsk(&ctsk_ai_inference);
    if (tskid <= E_OK) {
        tm_printf((UB *)"[usermain] tk_cre_tsk(ai_inference) failed. ercd = %d\n", (INT)tskid);
        return -1;
    }

    ercd = tk_sta_tsk(tskid, 0);
    if (ercd != E_OK) {
        tm_printf((UB *)"[usermain] tk_sta_tsk(ai_inference) failed. ercd = %d\n", (INT)ercd);
        return -1;
    }

    tm_putstring((UB *)"[usermain] ai_inference_task created & started.\n");

    /* usermain() を終了させない（終了すると μT-Kernel がシャットダウンするため）。
     * 初期タスクは高優先度のため、ここで待ち状態に入れて他タスクへ実行を譲る。 */
    tk_slp_tsk(TMO_FEVR);

    return 0;
}
