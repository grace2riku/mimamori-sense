# μT-Kernel 3.0 移行手順書（FreeRTOS → μT-Kernel 3.0 BSP2）

本書は mimamori-sense（CPU0 / Cortex-M85）の RTOS を FreeRTOS から μT-Kernel 3.0（BSP2）へ
段階的に移行するための手順書である。TRON プログラミングコンテスト2026の必須要件
（product-requirements.md 1.2 / 6.2「RTOS に μT-Kernel 3.0 を使用」）に対応する。

- 対象 Issue: #150（Epic R-000）配下の R-001〜R-008
- 本書の管轄 Issue: #151（R-001）
- 本書は**全ステップの土台**であり、R-002〜R-007 の各ステップ完了時に追記・更新する
  （→ 「9. 手順書の更新運用」参照）

> 重要: 本書は「FreeRTOS の e2 studio GUI 設定を残したまま、ユーザーコード側で
> μT-Kernel 3.0 へルーティングする」方針を前提とする。FSP 再生成（Generate Project Content）の
> たびに `ra_gen/` の FreeRTOS 生成コードが復活するため、**再生成後に μT-Kernel 化を
> 再適用する手順**を本書で定義する（→ 「6. 再適用チェックリスト」）。

---

## 1. 移行全体像（R-000〜R-008）

| Issue | ID | 内容 | 本書での扱い |
|-------|------|------|------|
| #150 | R-000 | 移行全体を統括する Epic | 全体方針の親 |
| #151 | R-001 | 移行手順書の作成（本書） | 本書そのもの |
| #152 | R-002 | BSP2 組み込みと LLVM ツールチェイン対応（ビルド確立） | 「4. BSP2 組み込み手順」 |
| #153 | R-003 | ブート・OS 起動の移行（最小構成で起動・LED・tm_printf） | 「7.1 ブート」 |
| #154 | R-004 | NT-Shell 関連の移行 | 「7.2 NT-Shell」 |
| #155 | R-005 | カメラの移行 | 「7.3 カメラ」 |
| #159 | R-006a | LVGL OSAL 対応方針 技術検討（スパイク） | 「7.4 LCD/LVGL」前提 |
| #156 | R-006 | LCD 画面（LVGL）の移行 | 「7.4 LCD/LVGL」 |
| #157 | R-007 | 転倒検出 AI 推論の移行 | 「7.5 AI 推論」 |
| #158 | R-008 | 統合動作確認・KPI 検証・ドキュメント更新 | 「8. 統合・KPI」 |

### 移行の絶対方針（厳守）

1. **FreeRTOS 利用設定（e2 studio GUI / `configuration.xml`）は変更しない**
   - 理由: FreeRTOS 設定を外すとシステムコール呼び出し箇所が消え、μT-Kernel へ
     置き換えるべき箇所が分からなくなるため。
   - FSP は FreeRTOS スレッドを `ra_gen/` に生成し続ける。**ユーザーコード側で
     μT-Kernel 3.0 へルーティングする**。
2. **`ra_gen/` `ra/fsp/` `configuration.xml` は編集しない**
   - 編集は FSP 再生成で消える。変更は「再生成で消えない場所（`src/` 配下のユーザーコード、
     特に `hal_warmstart.c` 等のフック）」へ寄せる。
3. **ツールチェインは LLVM**（mimamori-sense 本体と統一。GNU ARM Embedded にしない）。
   - BSP2 公式手順は GNU ARM Embedded 前提のため、include path / リンカスクリプト /
     プリプロセッサ定義を LLVM 向けに読み替える。
4. **小さなステップで変更・テスト**し、各ステップで実機確認してから次へ進む。
5. **各ステップ完了時に本手順書へ差分適用ポイントを追記する**。

---

## 2. 前提環境

CLAUDE.md / プロジェクト実態に基づく前提環境。

| 項目 | バージョン / 値 | 備考 |
|------|----------------|------|
| IDE | e2 studio 2025-12 (25.12.0) | |
| FSP | 6.3.0 | `e2studio_CPU0/ra/fsp/inc/fsp_version.h` で確認（6.2.0 環境も併存） |
| ツールチェイン | LLVM Embedded Toolchain for Arm 21.1.1 | GNU ARM Embedded は使用しない |
| RTOS（移行前） | FreeRTOS（FSP `rm_freertos_port`） | 設定は維持する |
| RTOS（移行後） | μT-Kernel 3.0 BSP2（`mtk3_bsp2`） | TRON Forum 提供 |
| GUI ライブラリ | LVGL | OSAL が FreeRTOS 依存（R-006a で方針決定） |
| ボード | EK-RA8P1 | `ra_cfg.txt`: `RA8\|RA8P1\|EK-RA8P1` |
| MCU | R7KA8P1KFLCAC | Code Flash 1MB（CPU0 512KB + CPU1 512KB）, RAM 約1.9MB |
| コア構成 | CPU0: Cortex-M85 @1GHz（ブート担当） / CPU1: Cortex-M33 @250MHz | 本移行は **CPU0 のみ** が対象 |

### BSP2 版数（要確認）

- 使用する `mtk3_bsp2` のコミット/タグは **R-002 着手時に確定し、本表へ追記する（現時点: 要確認）**。
- 参照: TRON Forum `mtk3_bsp2` リポジトリ（EK-RA8P1 対応版）。
  - BSP2 RA FSP 手順: https://github.com/tron-forum/mtk3_bsp2/blob/main/doc/bsp2_ra_fsp_jp.md
  - BSP2 サンプル Start Guide: https://github.com/tron-forum/mtk3bsp2_samples/blob/main/Start_Guide/jp/startguide_ra_jp.md
- BSP2 公式手順は GNU ARM Embedded 前提。本プロジェクトは LLVM のため読み替えが必要
  （→ 「4.4 LLVM 読み替え」）。

---

## 3. 現状の FreeRTOS 依存（移行対象の棚卸し）

調査（Grep/Read）に基づく実態。移行で手を入れる箇所の一覧。

### 3.1 FSP 自動生成コード（`ra_gen/` ― 編集禁止 / 再生成で復活）

| ファイル | 役割 | μT-Kernel 化での扱い |
|----------|------|----------------------|
| `ra_gen/main.c` | `main()` で FreeRTOS スケジューラを起動（`vTaskStartScheduler()`）。各 `*_thread_create()` を呼ぶ | **編集しない**。`vTaskStartScheduler()` 到達前のフックで μT-Kernel 起動へ橋渡し（→ 7.1） |
| `ra_gen/blinky_thread.c` | `xTaskCreateStatic` で blinky スレッド生成。`blinky_thread_func` → `blinky_thread_entry()` を呼ぶ | **編集しない**。スレッド生成は μT-Kernel 側で再構築 |
| `ra_gen/ntshell_thread.c` | 同上（ntshell） | 同上 |
| `ra_gen/camera_thread.c` | 同上（camera） | 同上 |
| `ra_gen/lvgl_thread.c` | 同上（lvgl） | 同上 |
| `ra_gen/ai_inference_thread.c` | 同上（ai_inference） | 同上 |
| `ra_gen/*_thread.h` | 各スレッドヘッダ。`#include "FreeRTOS.h" / task.h / semphr.h` を含む | **編集しない**（include 連鎖に注意） |

`ra_gen/main.c` の起動シーケンス（実態）:

```
main()
 ├─ g_fsp_common_initialized_semaphore = xSemaphoreCreateCounting(...)
 ├─ blinky_thread_create();
 ├─ ntshell_thread_create();
 ├─ lvgl_thread_create();
 ├─ camera_thread_create();
 ├─ ai_inference_thread_create();
 └─ vTaskStartScheduler();   ← ここを μT-Kernel 起動へ橋渡しする（main.c は触らない）
```

各 `*_thread_func()` は最初に `rtos_startup_common_init()` を呼び、
最初のスレッドが `g_hal_init()`（HAL 共通初期化）を実行する設計。μT-Kernel 化後も
**`g_hal_init()` 相当の HAL 初期化が一度だけ実行されること**を保証する必要がある。

### 3.2 FreeRTOS 固有ユーザーファイル（`src/` ― 編集可）

| ファイル | 内容 | μT-Kernel 化での扱い |
|----------|------|----------------------|
| `src/freertos_hooks.c` | `vApplicationMallocFailedHook()` のみ実装 | μT-Kernel 化後は不要化（FreeRTOS 設定を残す間は残置可） |
| `src/User_FreeRTOSConfig.h` | `traceTASK_SWITCHED_IN/OUT` を LVGL の `lv_freertos_task_switch_in/out` にフック | LVGL OSAL 方針（R-006a）に依存。LVGL の FreeRTOS タスク統計用 |
| `src/hal_warmstart.c` | `R_BSP_WarmStart()`（BSP ウォームスタートフック）。ピン/SDRAM 初期化 | **μT-Kernel 起動の橋渡し候補**（再生成で消えない `src/` 配下） |

### 3.3 FreeRTOS API を使用するユーザーコード（`src/` ― 編集可）

Grep 実測。各 API は「FreeRTOS → μT-Kernel 3.0 API 対応表」（→ 5 章）で置換する。

| ファイル | 使用している主な FreeRTOS API | 移行ステップ |
|----------|------------------------------|--------------|
| `src/blinky_thread_entry.c` | `vTaskDelay(configTICK_RATE_HZ/2)` | R-003 |
| `src/jlink_console.c` | `vTaskDelay(1)`, `taskENTER_CRITICAL()` | R-004 |
| `src/usrcmd.c` | `vTaskDelay(pdMS_TO_TICKS(100))` | R-004 |
| `src/camera_thread_entry.c` | `vTaskDelay`, `pdMS_TO_TICKS`, `xEventGroupWaitBits`（I2C 完了待ち） | R-005 |
| `src/camera_framebuffer.c` | `xTaskGetTickCountFromISR()`（ISR 内 FPS 計測） | R-005 |
| `src/camera_display.c` | `xEventGroupGetBits/SetBits/ClearBits`（AI アプリ連携イベント） | R-005 / R-007 |
| `src/ov5640.c` | FreeRTOS 依存（要精査） | R-005 |
| `src/led_ctrl.c` | **FreeRTOS ソフトウェアタイマ** `xTimerCreate/Start/Stop`（LED 点滅） | R-003/R-004（→ `tk_cre_cyc` 周期ハンドラへ） |
| `src/ai_inference_thread_entry.c` | `xEventGroupCreateStatic`, `xEventGroupWaitBits/SetBits`, `vTaskDelay`（推論同期） | R-007 |
| `src/lvgl_thread_entry.c` | `vTaskDelay(1)`（`lv_timer_handler` 駆動） | R-006 |
| `src/ai_application/ai_cmd.c` | FreeRTOS 依存（要精査） | R-007 |
| `src/port/lv_port_indev.c`, `glcdc_port.c`, `dave2d_port.c`, `sdram_port.c`, `camera_test.c`, `dave2d_cache_management.c` | LVGL/描画ポート層の FreeRTOS 依存 | R-006 |
| `src/ui/fall_detection_screen.c` | UI 層の FreeRTOS 依存 | R-006 / R-007 |

> 注意（特殊ケース）:
> - `led_ctrl.c` は **タスク遅延ではなくソフトウェアタイマ**を使う。μT-Kernel では
>   周期ハンドラ（`tk_cre_cyc` / `tk_sta_cyc`）またはアラームハンドラ（`tk_cre_alm`）へ置換する。
> - `camera_framebuffer.c` / `camera_display.c` の `*FromISR` / イベント set は
>   **割り込みコンテキスト**から呼ばれる。μT-Kernel API を割り込みハンドラから呼ぶ際の
>   制約（割り込みコンテキスト対応）に注意（→ R-005）。
> - `jlink_console.c` の `taskENTER_CRITICAL()` は、UART RX ISR（`jlink_console_callback()`）と
>   共有する `s_out_of_band_received[]` / `s_g_out_of_band_index` を保護している。
>   FreeRTOS のクリティカルセクションは**割り込みマスク**を伴うため、μT-Kernel では
>   `tk_dis_dsp()`（ディスパッチ禁止のみ）に置換してはならず、**割り込みマスク**で守ること
>   （→ 5 章の注意・7.2）。

### 3.4 LVGL OSAL（`ra/lvgl/` ― FSP 管理 / 編集回避）

| ファイル | 内容 | 扱い |
|----------|------|------|
| `ra/lvgl/lvgl/src/osal/lv_freertos.c` | LVGL の FreeRTOS OSAL 実装 | R-006a で対応方針決定（案A自作 / 案B CMSIS-RTOS2 / 案C OS非依存） |
| `ra/lvgl/lvgl/src/osal/lv_cmsis_rtos2.c` | CMSIS-RTOS2 OSAL | 案B の候補 |
| `src/lv_conf_user.h` | LVGL ユーザー設定（`LV_USE_OS` 等） | R-006a の方針に従い設定 |

---

## 4. BSP2 組み込み手順（R-002 / ビルド確立）

> ゴール: **μT-Kernel をまだ起動させず、FreeRTOS のまま LLVM でビルドが通ること**。
> この段では `usermain()` への切り替えは行わない。BSP2 を組み込んで「コンパイル・リンクが
> 成功する」ことだけを確認する。

### 4.1 mtk3_bsp2 の取得・配置

- 取得: `git clone --recursive`（BSP2 はサブモジュールを含むため `--recursive` 必須）。
- 配置先（案）: リポジトリ直下 `mtk3_bsp2/`、または `e2studio_CPU0/` 配下。
  - **R-002 着手時に配置先を確定し本節へ記載する（要確認）**。
- `.gitignore` 方針:
  - 既存 `.gitignore` は `reference_projects/**` の生成物のみ除外している。
  - `mtk3_bsp2` を**サブモジュールとして取り込む**か、**本体をコミットする**かを R-002 で決定する。
    本体コミットする場合、BSP2 のビルド生成物（`*.o` `*.a` 等）を `.gitignore` へ追加する。

### 4.2 インクルードパス追加（e2 studio GUI 操作 ― ユーザー手動）

> include path / マクロ / リンカの変更は e2 studio のプロジェクト設定（GUI）で行う。
> `configuration.xml` ではなくビルド設定であり、自動生成で消えない。
> **以下の操作はユーザーが e2 studio 上で実施する。**

e2 studio: プロジェクト `mimamori_sense_CPU0` を右クリック → Properties →
C/C++ Build → Settings → Tool Settings → (LLVM) Compiler → Includes に追加:

```
mtk3_bsp2
mtk3_bsp2/config
mtk3_bsp2/include
（および BSP2 の RA/FSP 対応ディレクトリ ― bsp2_ra_fsp_jp.md に従い R-002 で確定）
```

### 4.3 プリプロセッサ定義（ターゲット定義マクロ）

EK-RA8P1 / FSP 向けのターゲット定義マクロを Compiler → Preprocessor → Defined symbols に追加:

```
_RAFSP_   （BSP2 の RA-FSP ターゲットを示すマクロ。正確な名称は bsp2_ra_fsp_jp.md で確定 ― 要確認）
```

> マクロ名は BSP2 のバージョンにより異なるため、R-002 で公式手順を参照して確定し本節を更新する。

### 4.4 リンカスクリプトの LLVM 整合

実態:
- 本プロジェクトのリンカは `e2studio_CPU0/script/fsp.lld`（LLVM lld 形式）。
  内容は `memory_regions.lld` と `fsp_gen.lld` を `INCLUDE` する FSP 標準構成。
- BSP2 公式は GNU ld 用 `mtkernel.ld` 相当を提供（GNU ARM Embedded 前提）。

方針:
- **既存の `fsp.lld`（LLVM lld）をベースに維持**し、μT-Kernel が要求するセクション
  （カーネルスタック・タスク管理領域等）を追記する形で整合させる。
  BSP2 の `mtkernel.ld` を丸ごと差し替えるのではなく、必要なセクション定義を移植する。
- 具体的な追記内容は R-002 で BSP2 リンカ要件を確認して確定し、本節へ記載する。

### 4.5 R-002 完了条件

- [ ] `mtk3_bsp2` が配置され、`.gitignore` 方針が決まっている
- [ ] include path / マクロ / リンカが LLVM 向けに設定されている
- [ ] **FreeRTOS のまま** LLVM でビルド（コンパイル＋リンク）が成功する
- [ ] 本書 2 章「BSP2 版数」、4.1〜4.4 の「要確認」が確定値で埋まっている

---

## 5. FreeRTOS → μT-Kernel 3.0 API 対応表

実装時はこの表を基準に置換する。**μT-Kernel 3.0 の時間指定は原則ミリ秒（TMO/RELTIM）**で、
FreeRTOS の tick（`pdMS_TO_TICKS` / `configTICK_RATE_HZ`）とは単位系が異なる点に注意。

| 機能 | FreeRTOS | μT-Kernel 3.0 |
|------|----------|---------------|
| タスク生成 | `xTaskCreate` / `xTaskCreateStatic` | `tk_cre_tsk` + `tk_sta_tsk` |
| タスク削除 | `vTaskDelete` | `tk_ter_tsk` + `tk_del_tsk` |
| 遅延 | `vTaskDelay(pdMS_TO_TICKS(n))` | `tk_dly_tsk(n)` ※n は ms |
| スケジューラ起動 | `vTaskStartScheduler` | BSP2: `knl_start_mtkernel()` → `usermain()` |
| バイナリ/カウンティングセマフォ | `xSemaphoreCreateBinary` / `Counting` | `tk_cre_sem` |
| セマフォ取得 | `xSemaphoreTake` | `tk_wai_sem` |
| セマフォ返却 | `xSemaphoreGive` | `tk_sig_sem` |
| ISR からのセマフォ返却 | `xSemaphoreGiveFromISR` | `tk_sig_sem`（割り込みハンドラ内で呼ぶ） |
| ミューテックス生成 | `xSemaphoreCreateMutex` | `tk_cre_mtx` |
| ミューテックス ロック/解放 | `xSemaphoreTake` / `Give` | `tk_loc_mtx` / `tk_unl_mtx` |
| イベントグループ生成 | `xEventGroupCreate` | `tk_cre_flg` |
| イベントセット | `xEventGroupSetBits` | `tk_set_flg` |
| イベント待ち | `xEventGroupWaitBits` | `tk_wai_flg` |
| イベントクリア | `xEventGroupClearBits` | `tk_clr_flg` |
| キュー生成 | `xQueueCreate` | `tk_cre_mbf`（メッセージバッファ）/ `tk_cre_mbx`（メールボックス） |
| キュー送受信 | `xQueueSend` / `xQueueReceive` | `tk_snd_mbf` / `tk_rcv_mbf` |
| ソフトウェアタイマ | `xTimerCreate` / `xTimerStart` / `xTimerStop` | `tk_cre_cyc` / `tk_sta_cyc` / `tk_stp_cyc`（周期）または `tk_cre_alm`（アラーム） |
| クリティカルセクション（タスク間のみの排他） | `taskENTER_CRITICAL` / `EXIT` | `tk_dis_dsp` / `tk_ena_dsp`（ディスパッチ禁止） |
| クリティカルセクション（**ISR と共有する状態の保護**） | `taskENTER_CRITICAL` / `EXIT` | **割り込みマスクが必須**: `DI(intsts)` / `EI(intsts)`（全割り込み禁止）または `tk_dis_int(intno)` / `tk_ena_int(intno)`（当該割り込みのみマスク）。`tk_dis_dsp` は**不可** |
| ISR 判定/遅延処理 | `xHigherPriorityTaskWoken` | μT-Kernel はシステムコールが直接ディスパッチを起こす（不要） |
| 起動時間取得 | `xTaskGetTickCount` / `xTaskGetTickCountFromISR` | `tk_get_otm` / `tk_get_tim` |

> 重要（クリティカルセクションの落とし穴）: FreeRTOS の `taskENTER_CRITICAL()` は
> **割り込みマスク**を伴う（`configMAX_SYSCALL_INTERRUPT_PRIORITY` 以下の割り込みを禁止する）。
> 一方 μT-Kernel の `tk_dis_dsp()` は**ディスパッチ（タスク切替）を禁止するだけで割り込みは止まらない**。
> そのため **ISR（割り込みハンドラ）とタスクで共有する状態**を `taskENTER_CRITICAL()` で
> 守っている箇所を `tk_dis_dsp()` に置換すると、保護中に当該 ISR が走ってデータを破壊する
> レースが残る。**ISR と共有するデータは必ず割り込みマスク（`DI`/`EI` または `tk_dis_int`/`tk_ena_int`）
> で保護する**こと（→ 7.2 の `jlink_console.c` が該当）。`tk_dis_dsp()` はタスク間のみの排他に限る。

### 5.1 割り込みハンドラ（ISR）から呼べる API の制約

FreeRTOS は ISR 用に別 API（`...FromISR` 系）を持つが、μT-Kernel 3.0 は**同じシステムコールを
タスク／割り込みハンドラ双方から呼ぶ**設計である。ただし以下の制約があるため、上記の対応表で
`...FromISR` を置換する際は ISR から呼んでよい API かを必ず確認する。

| 分類 | 割り込みハンドラからの可否 | 該当 API（本プロジェクトの置換対象） |
|------|---------------------------|--------------------------------------|
| **自タスクを待ち状態にしうる API（禁止）** | ❌ 呼べない | `tk_wai_flg` / `tk_wai_sem` / `tk_loc_mtx`、`tmo > 0` の `tk_dly_tsk` 等 |
| 通知・セット系（許可） | ✅ 呼べる | `tk_set_flg` / `tk_sig_sem` |
| 参照・時刻取得系（許可） | ✅ 呼べる | `tk_ref_flg` / `tk_get_tim` / `tk_get_otm` |

- μT-Kernel ではシステムコールが直接ディスパッチを起こさず、割り込みハンドラからの通知は
  **遅延ディスパッチ**として割り込み出口で処理される。FreeRTOS の `xHigherPriorityTaskWoken` +
  `portYIELD_FROM_ISR()` のような明示的なコンテキストスイッチ要求は不要。
- 本プロジェクトの該当箇所:
  - `camera_framebuffer.c` の `xTaskGetTickCountFromISR()` → `tk_get_tim`/`tk_get_otm`（参照系・ISR 可）
  - `camera_display.c` / `camera_thread_entry.c` の ISR からのイベント set → `tk_set_flg`（通知系・ISR 可）
  - **待ち（`tk_wai_flg` 等）は ISR から呼ばない**こと。待つのはタスク側のみ。
- API ごとの正確な ISR 可否は使用する `mtk3_bsp2` のバージョン仕様で必ず確認する（→ R-005 で確定）。

実装規約:
- ヘッダは `<tk/tkernel.h>` 系。
- 戻り値は `ER` 型。`E_OK` 以外は必ずエラーとしてハンドリングする。
- 時間指定は μT-Kernel のミリ秒系に揃える（FreeRTOS の tick 換算を持ち込まない）。
- FreeRTOS コードはいきなり削除せず、対応関係がレビューで追えるよう（コメント等で）残す。
  完全削除はステップが安定してから。

---

## 6. 再適用チェックリスト（Generate Project Content 実行後）

FSP 再生成（Generate Project Content）を行うと `ra_gen/` の FreeRTOS 生成コードが
復活し、μT-Kernel 化が上書き・消失する懸念がある。**再生成のたびに本チェックリストで
μT-Kernel 化が維持されていることを確認する。**

### 6.1 設計原則（再生成で消えない場所に変更を寄せる）

- 変更は **`src/` 配下のユーザーコード**に置く。`ra_gen/` `ra/fsp/` `configuration.xml` は触らない。
- μT-Kernel 起動の橋渡しは **`src/hal_warmstart.c`（再生成で消えない `src/` 配下のフック）**に置く
  （→ 7.1 の方式）。`ra_gen/main.c` は触らない。
- include path / マクロ / リンカ設定は **e2 studio のビルド設定（GUI）**に置く。これは
  自動生成では消えない（ただし環境移行時は再設定が要るためビルド設定値を本書に控える）。

### 6.2 再生成後の確認チェックリスト

- [ ] `ra_gen/main.c` の `vTaskStartScheduler()` 経路が、フック（`hal_warmstart.c` 等）で
      μT-Kernel 起動へ橋渡しされる構成のまま動くか（橋渡しは `src/` 側にあるので残るはず）
- [ ] e2 studio ビルド設定の include path（`mtk3_bsp2`, `/config`, `/include`）が残っているか
- [ ] プリプロセッサ定義（BSP2 ターゲットマクロ）が残っているか
- [ ] リンカスクリプト（`script/fsp.lld` への μT-Kernel セクション追記）が残っているか
- [ ] `ra_gen/*_thread.c` の FreeRTOS スレッド生成が復活しても、μT-Kernel 側のタスク生成と
      二重起動・競合しない構成になっているか（→ 7.1 の橋渡し方式で吸収）
- [ ] `src/` 配下の μT-Kernel 化したファイルが上書きされていないか（`ra_gen/` のみ再生成対象だが念のため）
- [ ] LLVM でビルドが通り、実機で μT-Kernel が起動するか（最低限 R-003 の LED + `tm_printf`）

> 補足: e2 studio のビルド設定（include / マクロ / リンカ）は `.cproject` に保存され
> 通常は再生成で消えないが、**プロジェクト再インポート・環境移行時に失われる**ため、
> 設定値そのものを本書 4 章に必ず控えておくこと。

---

## 7. ステップ別 差分適用ポイント

各ステップの実装時に本節を参照し、完了時に「実装メモ」を追記する。

### 7.1 ブート・OS 起動（R-003）

ゴール: μT-Kernel を起動し、LED 点滅 + `tm_printf` の起動ログ（115200/8N1）を確認。

差し替え方針（`ra_gen/main.c` は編集しない）:
- FSP 生成の `main()` → `vTaskStartScheduler()` 到達前に μT-Kernel を起動させる。
- 橋渡し候補は **`src/hal_warmstart.c` の `R_BSP_WarmStart()` フック**。
  - 実態: `R_BSP_WarmStart()` は `BSP_WARM_START_POST_C`（C ランタイム・クロック確立後）で
    ピン設定・SDRAM 初期化を行っている。ここは `main()` より前に呼ばれる BSP フックである。
  - 方式の選択肢（R-003 で検証して確定）:
    - **方式A**: ウォームスタート後の経路で μT-Kernel（`knl_start_mtkernel()` → `usermain()`）へ
      制御を移し、FreeRTOS スケジューラ（`vTaskStartScheduler()`）には戻らない構成にする。
    - **方式B**: FreeRTOS の最初のタスク（blinky）から μT-Kernel 起動へ橋渡しする構成。
  - いずれも **`ra_gen/main.c` / `ra_gen/*_thread.c` を編集せず** `src/` 側で吸収すること。
- `usermain()` を `src/` 配下に新規実装し、最小タスクを生成:
  - LED 点滅タスク（`blinky_thread_entry.c` の `vTaskDelay(configTICK_RATE_HZ/2)` →
    `tk_dly_tsk(500)` 相当）。
  - `tm_printf` による起動ログ出力。
- `g_hal_init()` 相当の HAL 共通初期化が**一度だけ**実行されることを保証する
  （FreeRTOS では `rtos_startup_common_init()` が担っていた ― 3.1 参照）。
- この段では ntshell / camera / lvgl / ai_inference は**起動しない / 無効化**する。

確認: 実機で LED 点滅・シリアル出力 → 起動経路の確定内容を本節へ追記。

実装メモ（R-003 完了時に記入）:
- 採用方式（A/B）と橋渡しファイル:
- `usermain()` の配置ファイル:
- HAL 初期化の一度きり保証の実装:

### 7.2 NT-Shell（R-004）

対象: `src/ntshell_thread_entry.c`, `src/jlink_console.c`, `src/usrcmd.c`,
`src/ntshell/`, `src/led_ctrl.c`。

差し替えポイント（実測）:
- `jlink_console.c`: `vTaskDelay(1)` → `tk_dly_tsk(1)`。
  `taskENTER_CRITICAL()`/`taskEXIT_CRITICAL()`（260/280/391 行の 3 箇所）は、UART RX ISR
  （`jlink_console_callback()`）が更新する共有状態 `s_out_of_band_received[]` /
  `s_g_out_of_band_index` を、タスク側の pop/copy 中に保護している。
  **これは ISR と競合するため `tk_dis_dsp()`/`tk_ena_dsp()`（ディスパッチ禁止のみ）では不十分**
  （ディスパッチ禁止では ISR が走り続け、NT-Shell 入力文字の取りこぼし・破損が起きる）。
  → **割り込みマスク**（`DI()`/`EI()`、または当該 UART 割り込みのみ `tk_dis_int()`/`tk_ena_int()`）で
  置換する。代替として、ISR セーフな同期設計（リングバッファ化し ISR からは `tk_set_flg` 等で
  通知のみ行う）へ作り替えてもよい。**`tk_dis_dsp()` への単純置換は禁止**。
- `usrcmd.c`: `vTaskDelay(pdMS_TO_TICKS(100))` → `tk_dly_tsk(100)`。
- `led_ctrl.c`: FreeRTOS ソフトウェアタイマ（`xTimerCreate/Start/Stop`）→ μT-Kernel 周期ハンドラ
  （`tk_cre_cyc`/`tk_sta_cyc`/`tk_stp_cyc`）。**LED 点滅の駆動方式が変わるため要注意**。
- UART 送受信の排他は `tk_cre_mtx`/`tk_cre_sem` を用いる。
- ntshell スレッドを μT-Kernel タスク化（`tk_cre_tsk` + `tk_sta_tsk`）。

確認: 既存デバッグコマンド（mr/md/mw/led, S-007〜S-011）が動作すること。

実装メモ（R-004 完了時に記入）:

### 7.3 カメラ（R-005）

対象: `src/camera_thread_entry.c`, `src/camera_framebuffer.c`, `src/camera_display.c`,
`src/ov5640.c`。

差し替えポイント（実測）:
- `camera_thread_entry.c`: `vTaskDelay` / `pdMS_TO_TICKS` → `tk_dly_tsk(ms)`。
  `xEventGroupWaitBits(g_i2c_event_group, ...)`（I2C 完了待ち、複数箇所）→ `tk_wai_flg` +
  `tk_cre_flg`（I2C 完了イベントフラグ）。
- `camera_framebuffer.c`: `xTaskGetTickCountFromISR()`（ISR 内 FPS 計測）→ `tk_get_otm`/`tk_get_tim`。
  **`vin0_callback`（frame_complete 割り込み）から呼ばれる**ため割り込みコンテキスト対応に注意。
- `camera_display.c`: `xEventGroupGetBits/SetBits/ClearBits(g_ai_app_event)` → `tk_ref_flg` /
  `tk_set_flg` / `tk_clr_flg`。MIPI/VIN フレーム完了割り込み → タスク通知の同期を
  イベントフラグ/セマフォへ置換。**割り込みハンドラから μT-Kernel API を呼ぶ制約に注意**。
- camera スレッドを μT-Kernel タスク化。

確認: カメラ取得・表示が μT-Kernel 下で動作。

実装メモ（R-005 完了時に記入）:

### 7.4 LCD / LVGL（R-006 / 前提 R-006a）

前提: **R-006a（#159）で LVGL OSAL 対応方針が確定していること**。
- 案A: μT-Kernel 向け LVGL OSAL 自作（`lv_freertos.c` 相当を新規実装、`LV_USE_OS` カスタム）
- 案B: CMSIS-RTOS2 抽象層経由（`lv_cmsis_rtos2.c` + μT-Kernel 向け CMSIS-RTOS2 バックエンド）
- 案C: LVGL OSAL 非依存化（`LV_USE_OS = LV_OS_NONE`、自前で `lv_timer_handler` 駆動）

対象: `src/lvgl_thread_entry.c`, `src/lv_conf_user.h`, `src/User_FreeRTOSConfig.h`,
`src/port/lv_port_indev.c`, `glcdc_port.c`, `dave2d_port.c`, `sdram_port.c`,
`dave2d_cache_management.c`, `src/ui/fall_detection_screen.c`,
`ra/lvgl/lvgl/src/osal/lv_freertos.c`（方針により扱い変動）。

差し替えポイント（実測）:
- `lvgl_thread_entry.c`: `vTaskDelay(1)`（`lv_timer_handler` 駆動）→ `tk_dly_tsk(1)`。
- `User_FreeRTOSConfig.h` の `traceTASK_SWITCHED_IN/OUT` → LVGL のタスク統計フック。
  R-006a の方針（案A/B/C）に従い扱いを決定。
- LVGL のロック/タイマ駆動を確定方針に従い実装。

確認: LCD 表示（カメラ画像・UI）が正常表示。フレームレート 30fps 目標への影響確認。

実装メモ（R-006 完了時に記入）:

### 7.5 AI 推論（R-007）

対象: `src/ai_inference_thread_entry.c`, `src/ai_application/ai_cmd.c`,
`src/fall_detection_logic.c`, `src/fall_detection_cmd.c`, `src/camera_display.c`（連携）。

差し替えポイント（実測）:
- `ai_inference_thread_entry.c`: `xEventGroupCreateStatic` → `tk_cre_flg`。
  `xEventGroupWaitBits` → `tk_wai_flg`、`xEventGroupSetBits` → `tk_set_flg`
  （`HARDWARE_ETHOSU_INIT_DONE`, `SOFTWARE_AI_INFERENCE_INIT_DONE`,
  `AI_INFERENCE_RESULT_UPDATED` 等）。`vTaskDelay` → `tk_dly_tsk(ms)`。
- カメラ→前処理→推論のイベント通知（前処理完了・推論要求）を μT-Kernel API へ置換。
- Ethos-U55 ドライバ・推論実行と RTOS の連携箇所を置換。
- 転倒判定ロジック（`fall_detection_logic.c`）との連携を確認。
- ai_inference スレッドを μT-Kernel タスク化。

確認: 推論時間 KPI（5ms 以内）。

実装メモ（R-007 完了時に記入）:

---

## 8. 統合動作確認・KPI 検証（R-008）

- 全機能（ブート/NT-Shell/カメラ/LCD/AI）を同時起動して結合動作確認。
  - タスク優先度・スタックサイズ・同期/排他の最終調整。
  - リソース競合・デッドロック・優先度逆転の有無を確認。
- KPI 検証:
  - フレームレート 30fps / 推論 5ms 以内 / 転倒検出精度・誤検出率 / 通知 10 秒以内。
- LLVM ツールチェインでビルド・動作していることを最終確認。
- 本書の最終化、`doc/product-requirements.md` の状態反映。

---

## 9. 手順書の更新運用

本書は R-001 の成果物だが、**全ステップの土台**として継続的に更新する。

- R-002〜R-007 の各ステップ完了時に、対応する節（4 章 / 7.x）の「実装メモ」「要確認」を
  実際の確定値で埋める。
- 「要確認」と記した箇所（BSP2 版数、配置先、ターゲットマクロ名、リンカ追記内容、起動橋渡し方式）は
  該当ステップで確定したら本書を更新する。
- 各ステップは**実機動作確認（ユーザー実施）を経てから次へ進む**。確認完了の通知を待つこと。
- 更新は対象ステップの Issue（R-002〜R-008）のブランチで行い、PR に本書差分を含める。
- 進捗は親 Epic #150（R-000）で集約する。

### 更新履歴

| 日付 | ステップ | 更新内容 |
|------|----------|----------|
| 2026-06-10 | R-001 | 初版作成（全体像・前提環境・FreeRTOS 依存棚卸し・API 対応表・再適用チェックリスト・ステップ別差分ポイント） |
| 2026-06-10 | R-001 | レビュー反映: `jlink_console.c` のクリティカルセクションは ISR と共有状態を保護するため、`tk_dis_dsp()` ではなく割り込みマスク（`DI`/`EI`・`tk_dis_int`/`tk_ena_int`）が必要な旨を 3.3 / 5 / 7.2 に明記 |
| 2026-06-10 | R-001 | レビュー反映: 5.1 節を追加し、割り込みハンドラ（ISR）から呼べる μT-Kernel API の制約（待ち系は禁止／通知・参照系は可）を対応表化 |
