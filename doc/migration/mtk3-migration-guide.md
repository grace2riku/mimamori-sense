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

### BSP2 版数（R-002 で確定）

| 項目 | 値 |
|------|-----|
| `mtk3_bsp2` タグ | **v1.00.04** |
| `mtk3_bsp2` コミット | `1ab52cc5a9f59450e62ab78e76de11f4dd89eb15` |
| 内包サブモジュール `mtkernel`（μT-Kernel 3.0 本体） | コミット `435096c96136c847774b5d6de07cc092b1398778` |
| RA FSP マニュアル版数 | RA FSP 編 Version 01.00.B11（2026.04.27、EK-RA8P1 対応版） |
| EK-RA8P1 対応状況 | **公式対応済み**（`sysdepend/ra_fsp/lib/libtm/ek_ra8p1/`、`include/sys/sysdepend/ra_fsp/ek_ra8p1/machine.h`） |

- 取得コマンド: `git clone --recursive https://github.com/tron-forum/mtk3_bsp2.git`
- 参照: TRON Forum `mtk3_bsp2` リポジトリ（EK-RA8P1 対応版）。
  - BSP2 RA FSP 手順: https://github.com/tron-forum/mtk3_bsp2/blob/main/doc/bsp2_ra_fsp_jp.md
  - BSP2 サンプル Start Guide: https://github.com/tron-forum/mtk3bsp2_samples/blob/main/Start_Guide/jp/startguide_ra_jp.md
- BSP2 公式手順は GNU ARM Embedded 前提。本プロジェクトは LLVM のため読み替えが必要
  （→ 「4.6 LLVM 読み替え」）。

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
| `src/camera_thread_entry.c` | `vTaskDelay`, `pdMS_TO_TICKS`, `xEventGroupWaitBits`（I2C 完了待ち） | R-005 ✅ |
| `src/camera_framebuffer.c` | `xTaskGetTickCountFromISR()`（ISR 内 FPS 計測） | R-005 ✅ |
| `src/camera_display.c` | `xEventGroupGetBits/SetBits/ClearBits`（AI アプリ連携イベント） | R-006 ✅（tick/イベント API 置換）／ R-007 ✅（`g_ai_app_flgid` 生成側の移行で AI 連携経路が有効化。定義は ai_inference_thread_entry.c へ移動） |
| `src/ov5640.c` | I2C 完了割り込み→タスク同期（旧 `g_i2c_event_group`。方式A 未生成のため uT-Kernel イベントフラグ新設） | R-005 ✅ |
| `src/led_ctrl.c` | **FreeRTOS ソフトウェアタイマ** `xTimerCreate/Start/Stop`（LED 点滅） | R-003/R-004（→ `tk_cre_cyc` 周期ハンドラへ） |
| `src/ai_inference_thread_entry.c` | `xEventGroupCreateStatic`, `xEventGroupWaitBits/SetBits`, `vTaskDelay`（推論同期） | R-007 ✅ |
| `src/lvgl_thread_entry.c` | `vTaskDelay(1)`（`lv_timer_handler` 駆動） | R-006 |
| `src/ai_application/ai_cmd.c` | `xEventGroupGetBits`（`ai status` の表示のみ。精査の結果これ 1 箇所） | R-007 ✅ |
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
| `ra/lvgl/lvgl/src/osal/lv_freertos.c` | LVGL の FreeRTOS OSAL 実装 | **R-006a で方針確定: 案A（μT-Kernel 向け OSAL 自作・`LV_OS_CUSTOM`）を採用**。`#if LV_USE_OS == LV_OS_FREERTOS` ガードで空コンパイルされるため編集・除外不要（→ 7.4 / `r006a-lvgl-osal-spike.md`） |
| `ra/lvgl/lvgl/src/osal/lv_cmsis_rtos2.c` | CMSIS-RTOS2 OSAL | 案B は不採用（μT-Kernel 向け CMSIS-RTOS2 実装が存在しない） |
| `src/lv_conf_user.h` | LVGL ユーザー設定（`LV_USE_OS` 等） | R-006 で `LV_USE_OS LV_OS_CUSTOM` / `LV_OS_CUSTOM_INCLUDE "lv_os_mtkernel.h"` を設定（FSP 生成 lv_conf.h は `#ifndef` ガードのため ra/ 無編集で上書き可能） |

> R-006a の追加発見: LVGL OSAL の外側で、FSP 読み取り専用コード（`rm_lvgl_port.c` /
> `r_drw_irq.c` / `rm_comms_i2c_driver_ra.c`）が `BSP_CFG_RTOS==2` 分岐で FreeRTOS の
> ブロッキング API を直接呼んでおり、方式A ではこちらの対処も必須（→ 7.4 の表）。

---

## 4. BSP2 組み込み手順（R-002 / ビルド確立）

> ゴール: **μT-Kernel をまだ起動させず、FreeRTOS のまま LLVM でビルドが通ること**。
> この段では `usermain()` への切り替えは行わない。BSP2 を組み込んで「コンパイル・リンクが
> 成功する」ことだけを確認する。

### 4.1 mtk3_bsp2 の取得・配置（R-002 で確定）

- 取得: `git clone --recursive https://github.com/tron-forum/mtk3_bsp2.git`
  （BSP2 は μT-Kernel 3.0 本体をサブモジュール `mtkernel` として内包するため `--recursive` 必須）。
- **配置先（確定）**: **`e2studio_CPU0/mtk3_bsp2/`**
  - e2 studio プロジェクト `mimamori_sense_CPU0` のルートは `e2studio_CPU0/` であり、
    公式手順の include path 表記 `${workspace_loc:/${ProjName}/mtk3_bsp2}` をそのまま使うには
    BSP2 がプロジェクト（=`e2studio_CPU0`）の内側に存在する必要がある。リポジトリ直下ではなく
    **CPU0 プロジェクト直下**へ置く。
  - 配置後のディレクトリ構成（主要部）:
    ```
    e2studio_CPU0/mtk3_bsp2/
      ├─ config/            （config.h, config_bsp.h, config_bsp/ra_fsp/config_bsp.h など）
      ├─ include/           （sys/ tk/ tm/ ― machine.h でターゲット分岐）
      ├─ sysdepend/ra_fsp/  （CPU コア依存・デバイスドライバ。EK-RA8P1 は armv8m を使用）
      ├─ etc/linker/mtkernel.ld   （μT-Kernel 用追加リンカスクリプト）
      └─ mtkernel/          （μT-Kernel 3.0 本体。kernel/knlinc を include path に追加）
    ```
- **`.gitignore` 方針（確定）**: **本体ソースをコミットする（vendoring）**。
  - 理由: ntshell は submodule だが、BSP2 は config 改変（`config_bsp.h` の `DEVCNF_USE_HAL_*` 等）を
    伴うため、本体を直接コミットして版数を本書 2 章に固定（タグ `v1.00.04`）する方が
    FSP 再生成・環境移行に強い。
  - clone 後に **入れ子の `.git`（`mtk3_bsp2/.git` と `mtk3_bsp2/mtkernel/.git`）を削除**して
    プレーンなソースとして取り込む（submodule 化しない）。
  - `.gitignore` には BSP2 のビルド生成物のみ追加済み:
    ```
    e2studio_CPU0/mtk3_bsp2/**/*.o
    e2studio_CPU0/mtk3_bsp2/**/*.obj
    e2studio_CPU0/mtk3_bsp2/**/*.a
    e2studio_CPU0/mtk3_bsp2/**/*.lib
    e2studio_CPU0/mtk3_bsp2/**/*.d
    ```

### 4.2 ビルド対象設定（Exclude resource from build の解除）― ユーザー手動

clone 直後、e2 studio は `mtk3_bsp2/` をビルド対象外（`Exclude resource from build`）に
している場合がある。**ビルドに含めるため解除する**。

操作: プロジェクト・エクスプローラで `mimamori_sense_CPU0/mtk3_bsp2` を右クリック →
Properties → C/C++ Build → 「Exclude resource from build」のチェックが入っていれば**外す**
（全構成 Configuration に対して）。

> 注意（ビルド対象の絞り込み）: `mtk3_bsp2` 配下には他ボード（nxp_mcux / stm32_cube / xmc_mtb）や
> 他 CPU コア（RX/RZ/STM32 系）のソースも含まれるが、各ソースは `machine.h` のターゲットマクロ
> （`MTKBSP_RAFSP` / `MTKBSP_EK_RA8P1` / `MTKBSP_CPU_CORE_ARMV8M`、および `#ifdef CPU_RX65N` 等）で
> `#if`/`#ifdef` ガードされており、**EK-RA8P1 以外のコードは（RX アセンブリ `hllint_ent.S` 等も含め）
> 空コンパイルされる**。重複シンボルやアセンブルエラーは発生しない（→ R-002 で Debug ビルド成功により実証）。
> **ターゲットマクロ（4.4）を必ず先に設定すること。**

#### 4.2.1 非ターゲットソースの除外（ビルド時間短縮 ― 確定適用済み）

上記のとおり非ターゲットソースは空コンパイルされるだけで**ビルドは壊れない**が、毎回のクリーンビルドで
数百ファイルを無駄にコンパイルするため、明確に非ターゲットなディレクトリは `.cproject` の sourceEntries で
除外する。**Debug / Release 両構成の `mtk3_bsp2` ソースフォルダに以下の `excluding` を設定済み**
（ガードで空コンパイルされる木のみを除外。`mtkernel/kernel/sysdepend/cpu/core`（Arm コア）と
`sysdepend/ra_fsp`（ターゲット）は保持するためビルド結果は不変）:

```
mtkernel/kernel/sysdepend/cpu/rx231
mtkernel/kernel/sysdepend/cpu/rx65n
mtkernel/kernel/sysdepend/cpu/rza2m
mtkernel/kernel/sysdepend/cpu/stm32h7
mtkernel/kernel/sysdepend/cpu/stm32l4
mtkernel/kernel/sysdepend/cpu/tx03_m367
sysdepend/nxp_mcux
sysdepend/stm32_cube
sysdepend/xmc_mtb
```

> e2 studio GUI で同等の操作を行う場合は、各ディレクトリを右クリック →
> 「Exclude resource from build」（全 Configuration）。`.cproject` 直接編集なら sourceEntries の
> `mtk3_bsp2` エントリに `excluding="...|..."`（`|` 区切り）として記述する。
> さらに削りたい場合は `sysdepend/ra_fsp` 配下の他 RA ボード/CPU（`arduino_unor4` 等）も除外可能だが、
> ガードで無害なため本プロジェクトでは保持している（任意）。

### 4.3 インクルードパス追加（e2 studio GUI 操作 ― ユーザー手動）

> include path / マクロ / リンカの変更は e2 studio のプロジェクト設定（GUI）で行う。
> `configuration.xml` ではなくビルド設定（`.cproject`）であり、FSP 自動生成で消えない。
> **以下の操作はユーザーが e2 studio 上で実施する。**
>
> **重要（全 Configuration へ適用）**: 4.3〜4.5 の include path / マクロ / リンカ設定は、
> Properties ダイアログ左上の **Configuration: [All configurations]** を選択してから行い、
> **Debug / Release 両構成に必ず同一設定を適用する**。片方のみだと、もう一方の構成で
> μT-Kernel を実起動した際（R-003 以降）にビルドが破綻する。
> （本リポジトリの `.cproject` は Debug / Release 両構成に設定済み・対称であることを確認済み。）
>
> **既知の制約（Release 構成の土台未整備 ― R-002 スコープ外）**: 本プロジェクトは従来 **Debug 構成のみ**で
> ビルド・デバッグしてきた。**Release 構成は FSP 生成ファイル `bsp_linker_info.h`（ビルド時に有効構成の
> 出力先へ生成されるマルチコア・パーティション情報）が未生成のため、CPU0/CPU1 とも現状ビルドできない**
> （`'bsp_linker_info.h' file not found`）。これは μT-Kernel/BSP2 とは無関係の既存のプロジェクト
> セットアップ事項であり、Release を実ビルド可能にするには「Release 構成をアクティブにして
> Generate Project Content」等の FSP 生成が別途必要（R-002 では未対応）。
> 上記 BSP2 設定は Release 構成にも**先行して反映済み（対称化）**だが、土台が整うまで Release は
> ビルド検証できない。R-002 の動作検証は **Debug 構成**で実施する。

e2 studio: プロジェクト `mimamori_sense_CPU0` を右クリック → Properties →
C/C++ Build → Settings → Tool Settings → **LLVM C Compiler → Includes**（Include paths）に
以下 4 つを追加する（**既存の include path は変更しない**）:

```
"${workspace_loc:/${ProjName}/mtk3_bsp2}"
"${workspace_loc:/${ProjName}/mtk3_bsp2/config}"
"${workspace_loc:/${ProjName}/mtk3_bsp2/include}"
"${workspace_loc:/${ProjName}/mtk3_bsp2/mtkernel/kernel/knlinc}"
```

> 公式手順（4 パス）と同一。4 番目 `mtkernel/kernel/knlinc` は μT-Kernel 本体の内部ヘッダ
> （`kernel.h` 等）用で、これが欠けると `sys_start.c` 等のビルドが失敗する。
>
> **アセンブラにも同じ include path を設定する。** BSP2 は `dispatch.S`（ARMv8-M）等の
> アセンブリを含むため、**LLVM Assembler → Includes** にも上記 4 パスを同様に追加する。

### 4.4 プリプロセッサ定義（ターゲット定義マクロ）

EK-RA8P1 のターゲット定義マクロを **LLVM C Compiler → Preprocessor → Defined symbols**
に追加する（**既存の定義は変更しない**）:

```
_RAFSP_EK_RA8P1_
```

- このマクロ 1 つで BSP2 内部の `include/sys/machine.h` が
  `include/sys/sysdepend/ra_fsp/ek_ra8p1/machine.h` を取り込み、以下が自動で有効になる:
  - `MTKBSP_RAFSP`（RA FSP ターゲット）
  - `MTKBSP_EK_RA8P1`（EK-RA8P1 ボード。`tm_com.c` の SCI8 シリアル選択等に使用）
  - `MTKBSP_CPU_CORE_ARMV8M`（Cortex-M85 = ARMv8-M。`sysdepend/ra_fsp/cpu/core/armv8m/` を選択）
  - `TARGET_DIR = ra_fsp/ek_ra8p1` / `TARGET_GRP_DIR = ra_fsp`（config_bsp 等のパス解決）
- **アセンブラにも同じ定義を設定する**（LLVM Assembler → Preprocessor。`dispatch.S` 等のため）。
- 他ボード用の `_RAFSP_*_` マクロは定義しないこと（重複定義はビルドエラーの原因）。

> 注意: T-Monitor シリアル出力は **SCI8（PD02=TXD8 / PD03=RXD8）, 115200/8N1** を直接レジスタ
> 操作で使用する（`sysdepend/ra_fsp/lib/libtm/ek_ra8p1/tm_com.c`）。本プロジェクトの既存
> J-Link コンソール（`jlink_console.c`）が使う UART と物理ポートが競合しないか R-003 で確認する。

### 4.5 リンカスクリプトの LLVM 整合

実態:
- 本プロジェクトのリンカは `e2studio_CPU0/script/fsp.lld`（LLVM lld 形式）。
  内容は `memory_regions.lld` と `fsp_gen.lld` を `INCLUDE` する FSP 標準構成。
  `fsp_gen.lld` が `MEMORY{ RAM ... }` と各セクションを定義する。
- BSP2 が要求する追加リンカスクリプトは `mtk3_bsp2/etc/linker/mtkernel.ld`。内容は以下のみ:
  ```
  SECTIONS
  {
      .mtk_bsp2 (NOLOAD) :
      {
          *(.mtk_exctbl)
          __mtk3_SYSMEM_START = .;
      }>RAM
  }
  ```
  - `.mtk_exctbl`: 例外/割り込みハンドラテーブル（`sys_start.c` の `knl_exctbl[]` を RAM に配置）。
  - `__mtk3_SYSMEM_START`: μT-Kernel のシステムメモリ（動的確保プール）の開始アドレス。
    既存 RAM セクションの**後ろ**（空き RAM 先頭）を指す必要がある。

方針（確定）:
- **既存 `fsp.lld` を維持し、BSP2 の `mtkernel.ld` を追加リンカスクリプトとして「後ろに」連結する**。
  `mtkernel.ld` を丸写し・改変せず、**そのまま追加スクリプトとして渡す**。
  - lld は複数の `-T`（Script files）を順に処理し、`.mtk_bsp2` の `>RAM` は
    `fsp_gen.lld` が定義した `RAM` 領域へ割り付けられる。`mtkernel.ld` を fsp.lld の**後**に
    処理させることで、`__mtk3_SYSMEM_START` が FSP 割り当て済み RAM の末尾（空き領域の先頭）を指す。
  - GNU ld 前提の公式は GUI の「Script files」末尾に追加するが、LLVM lld でも**スクリプトの
    指定順を fsp.lld の後**にすれば同じ効果になる。
- e2 studio GUI 操作（**確定方式 ― 2026-06-11 実機ビルド成功**）:
  - Properties → C/C++ Build → Settings → Tool Settings → **LLVM Linker → General →
    Script files (-T)** の**末尾**に以下を追加する（既存の `fsp.lld` 指定はそのまま、その後ろに置く）。
    **本方式で LLVM ビルド（コンパイル＋リンク）が成功することを確認済み**:
    ```
    "${workspace_loc:/${ProjName}/mtk3_bsp2/etc/linker/mtkernel.ld}"
    ```
  - 参考（未採用の代替案）: e2 studio の LLVM リンカ設定が `fsp.lld` を 1 本だけ指定する形で
    スクリプト追加欄が無い場合は、**`e2studio_CPU0/script/fsp.lld` の末尾に
    `INCLUDE etc/linker/mtkernel.ld` 相当を追記**して連結する方法もある（`fsp.lld` は
    FSP 自動生成対象外の `script/` 配下ユーザー管理ファイル）。本プロジェクトでは Script files 欄
    への追加で通ったため、この代替は使用していない。
- **R-002 段階の注意**: μT-Kernel をまだ起動しない（`knl_start_mtkernel()` を呼ばない）ため、
  `.mtk_bsp2` セクションや BSP2 のシンボルは**リンクされるが実行時には未使用**。リンクが通れば
  この段階の目的（ビルド確立）は達成。実行時挙動の確認は R-003 で行う。

### 4.6 LLVM 読み替え（GNU ARM Embedded → LLVM）

公式手順（`bsp2_ra_fsp_jp.md` 4.2.2）は GNU ARM Embedded の GUI ラベルで記述されている。
LLVM ツールチェインでは以下のように読み替える（設定する**値**は同一、設定する**場所**が異なる）。

| 公式（GNU ARM Embedded）の設定場所 | 本プロジェクト（LLVM）の設定場所 | 設定値 |
|----------------------------------|--------------------------------|--------|
| GNU Arm Cross C Compiler → Preprocessor → Define symbols | LLVM C Compiler → Preprocessor → Defined symbols | `_RAFSP_EK_RA8P1_` |
| GNU Arm Cross C Compiler → Includes → Include paths | LLVM C Compiler → Includes → Include paths | 4.3 の 4 パス |
| GNU Arm Cross Assembler → Preprocessor | LLVM Assembler → Preprocessor | `_RAFSP_EK_RA8P1_` |
| GNU Arm Cross Assembler → Includes | LLVM Assembler → Includes | 4.3 の 4 パス |
| GNU Arm Cross Linker → General → Script files | LLVM Linker → General → Script files (-T) | `mtkernel.ld` を末尾に追加（4.5） |

- 設定 GUI のラベル名は e2 studio の LLVM ツールチェイン表示に依存する。
  「LLVM C Compiler」「LLVM Assembler」「LLVM Linker」が見当たらない場合は、
  Tool Settings 内の Compiler / Assembler / Linker の各 Includes・Preprocessor・General を探す。
- BSP2 の C ソースは GNU 拡張（`__attribute__((section(...)))`、インライン asm 等）を使うが、
  LLVM/clang は GNU 拡張互換のため通常そのままビルドできる。万一警告/エラーが出た場合は
  該当箇所を R-002 実機ビルドで切り分ける。

### 4.7 R-002 完了条件（2026-06-11 実機ビルド成功で全項目達成）

- [x] `mtk3_bsp2`（タグ v1.00.04）が `e2studio_CPU0/mtk3_bsp2/` に配置され、`.gitignore` 方針
      （vendoring + ビルド生成物除外）が決定・反映されている
- [x] include path（4 パス）/ マクロ（`_RAFSP_EK_RA8P1_`）/ リンカ（`mtkernel.ld` 連結）が
      LLVM 向けに設定されている（**Debug / Release 両構成に設定・対称化済み**。4.3 参照）
- [x] Exclude resource from build が解除され、BSP2 ソースがビルド対象になっている。
      非ターゲット木（RX/RZ/STM32/他ベンダ）は両構成で除外済み（4.2.1）
- [x] **FreeRTOS のまま**（`knl_start_mtkernel()` は呼ばない）LLVM でビルド
      （コンパイル＋リンク）が成功する（Debug で実証。Release も同一設定）

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
- [ ] e2 studio ビルド設定の include path（`mtk3_bsp2`, `/config`, `/include`, `/mtkernel/kernel/knlinc`）が残っているか
- [ ] プリプロセッサ定義（BSP2 ターゲットマクロ `_RAFSP_EK_RA8P1_`、C/アセンブラ両方）が残っているか
- [ ] リンカスクリプト（`mtkernel.ld` を `fsp.lld` の後ろに連結する設定 ― 4.5）が残っているか
- [ ] `ra_gen/*_thread.c` の FreeRTOS スレッド生成が復活しても、μT-Kernel 側のタスク生成と
      二重起動・競合しない構成になっているか（→ 7.1 の橋渡し方式で吸収）
- [ ] `src/` 配下の μT-Kernel 化したファイルが上書きされていないか（`ra_gen/` のみ再生成対象だが念のため）
- [ ] `mtk3_bsp2/mtkernel/config/config_func.h` の機能トリミング（R-003 のフラッシュ対策）が維持されているか。
      移行で未使用の `USE_MAILBOX` / `USE_MESSAGEBUFFER` / `USE_RENDEZVOUS` / `USE_MEMORYPOOL` /
      `USE_FIX_MEMORYPOOL` / `USE_DEVICE` を `0`（他は既定値のまま）。**FSP 再生成では消えないが、BSP2 を
      再 vendoring すると既定値 `1` に戻る**ため要再適用（→ 7.1 末尾「コードフラッシュ・オーバーフロー」参照）
- [ ] コードフラッシュ区画の再配分（R-003 で実施。CPU0 約 992KB / CPU1 約 32KB）が `solution.xml` の Memories
      タブに維持されているか。`memory_regions.lld` の `FLASH_LENGTH`（CPU0 ≒ `0x000f8000`）で確認できる。
      区画は FSP 管理のため、環境移行・再インポート時は再設定が必要
- [ ] `mtk3_bsp2/include/sys/sysdepend/ra_fsp/ek_ra8p1/sysdef.h` が `cpu/ra8p1/sysdef.h` を include しているか
      （BSP2 v1.00.04 のベンダ不具合で既定は誤って `cpu/ra8m1/sysdef.h`。誤値だと `INTERNAL_RAM_END` が 896KB 相当に
      なり初期タスク生成が `!ERROR! Initial Task can not creat` で失敗）。再 vendoring 時は再適用必須（→ 7.1）
- [ ] `mtk3_bsp2/config/config.h` の `CNF_SYSTEMAREA_END = 0x221B0000`（CPU0 RAM 末尾）が維持されているか。
      既定値 `0` だとシステムメモリプールが CPU1 RAM 区画へはみ出して破壊される（→ 7.1）。**RAM 区画を変更したら
      追従させる**。再 vendoring 時も再適用要
- [ ] （R-006）**`ra/fsp/src/r_drw/r_drw_irq.c` のビルド除外（全 Configuration）が維持されているか**。
      除外は `.cproject` sourceEntries に保存され Generate Project Content では消えないが、
      **プロジェクト再インポート・環境移行時は再設定が必要**。除外が外れると
      `src/port/r_drw_irq_mtk3.c` と 4 シンボル（`d1_initirq_intern` / `d1_shutdownirq_intern` /
      `d1_queryirq` / `drw_int_isr`）が重複してリンクエラーになる（→ 7.4）
- [ ] （R-006）**FSP 版数更新時**: `ra/fsp/src/r_drw/r_drw_irq.c` と
      `ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c` の原本差分を確認し、対応する複製実装
      （`src/port/r_drw_irq_mtk3.c` / `src/port/lvgl_port_mtk3.c`）へ反映する
      （両ファイルとも原本と 1:1 対応のコメントを付与済み）
- [ ] （R-006）`mtk3_bsp2/config/config.h` の **`CNF_MAX_MTXID = 16`** が維持されているか
      （LVGL OSAL が mutex を 6 個以上生成。既定値 4 だと `tk_cre_mtx` が E_LIMIT で失敗し
      LVGL 初期化が破綻）。**BSP2 を再 vendoring すると既定値 4 に戻る**ため要再適用（→ 7.4）
- [ ] （R-004）`usermain()` 先頭の `bsp_irq_cfg()` 呼び出しが維持され、SCI8（NT-Shell）割り込みが結線されるか。
      `bsp_irq_cfg()` は FSP の **内部 API**（`ra/fsp/src/bsp/mcu/all/bsp_irq.h` で `// Used internally by BSP`）であり、
      方式A で `SystemInit()` の `bsp_irq_cfg()`（`system.c:525`）が未実行になる代替として src 側から呼んでいる（→ 7.2）。
      **FSP バージョンアップ時はシグネチャ/挙動（IELSR 設定内容・`g_interrupt_event_link_select[]` 形式）が
      変わっていないかを確認**すること。未結線だと `jlink_console_write` がハングする
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

実装メモ（R-003 / 2026-06-11 実装。実機確認はユーザー実施）:

- **採用方式: 方式A**（静的コンストラクタ（`__init_array`）から `knl_start_mtkernel()` を呼び、
  FreeRTOS の `main()` / `vTaskStartScheduler()` には到達させない）。
  - 橋渡しファイル: **`src/hal_warmstart.c`**（`src/` 配下・FSP 再生成で消えない）。POST_C フックと
    起動用コンストラクタの双方を本ファイルに置く。
  - 実装位置（**当初の POST_C 末尾から静的コンストラクタへ移設 ― PR #163 codex 指摘 P2 への対応**）:
    - **`R_BSP_WarmStart(BSP_WARM_START_POST_C)`**: ピン設定（`R_IOPORT_Open`）と SDRAM コントローラ起動
      （`R_BSP_SdramInit`/`sdram_port_init`）**のみ**行う。ここでは μT-Kernel を起動しない。
    - **`__attribute__((constructor)) static void mimamori_start_mtkernel(void)`**: `knl_start_mtkernel()` を呼ぶ。
      同関数は戻らない（`knl_main()` → 初期タスク → `usermain()`）。万一戻った場合に備え無限ループでトラップ。
      コンストラクタへのポインタは `fsp_gen.lld` の `KEEP(*(.init_array))` で保持され `gc-sections` で消えない。
  - **POST_C 末尾から起動位置を移した理由（codex P2）**: `SystemInit()`（`system.c`）は
    `R_BSP_WarmStart(POST_C)`（同:471）の**直後**に、戻らないジャンプではスキップされてしまう以下の C ランタイム
    初期化を実行する ―
    `SystemRuntimeInit(1)`（同:476。外部メモリ SDRAM の `.sdram` ゼロクリア / `.sdram_from_flash`・`.sdram_ospi_data`
    コピー）、TLS 初期化（同:481）、静的コンストラクタ `__init_array[]`（同:512-516）、`bsp_irq_cfg()`（同:525）。
    本リポジトリは `BSP_CFG_C_RUNTIME_INIT=1` / `BSP_CFG_SDRAM_ENABLED=1` で、`.sdram` 配置バッファ
    （例: `ai_inference_thread_entry.c` の `model_buffer_int8`）が存在する。POST_C 末尾で抜けると、後続の
    μT-Kernel タスクがこれらを**未初期化のまま**使う潜在バグになる（R-003 最小構成では `.sdram`/コンストラクタを
    使わないため実機では顕在化しないが、カメラ/AI 等の SDRAM 利用タスクを移行した段階で問題化する）。
  - **`bsp_init()` ではなくコンストラクタを使う理由**: 当初は `SystemInit` 最終段の FSP weak フック `bsp_init()` を
    上書きする案だったが、**本ボードでは `ra/board/ra8p1_ek/board_init.c` が `bsp_init()` を強いシンボルで定義済み**で
    （`ra/` は編集禁止）、二重定義（`ld.lld: error: duplicate symbol: bsp_init`）となり使用できない。
    そこで上書き不要な静的コンストラクタを使う。`__init_array` は `SystemRuntimeInit(1)` の**後**（同:512-516）に
    実行されるため、外部メモリ初期化・TLS 初期化が完了した状態でカーネルへ入れる。無印 `.init_array`（優先度なし）は
    `SORT(.init_array.*)`（優先度付き）の後に並ぶ（`fsp_gen.lld`）。
    SDRAM コントローラ起動（`R_BSP_SdramInit`）は引き続き POST_C で行うため、
    **「SDRAM 起動(POST_C) → `.sdram` セクション初期化(`SystemRuntimeInit`) → カーネル(コンストラクタ)」**の順序が保たれる。
    なお `bsp_irq_cfg()`（ELC/NVIC 設定）はコンストラクタの後・`main()` 前に呼ばれるため本方式でも未実行だが、
    これは当初の POST_C 方式でも同様で、R-003 は ELC 起動の割り込みを使わないため影響しない。
  - 切り戻し: `hal_warmstart.c` の `#define MIMAMORI_USE_MTKERNEL_BOOT (1)` を `0` にすると橋渡し
    （起動コンストラクタ含む）を無効化し、従来の FreeRTOS 起動（`ra_gen/main.c` → `vTaskStartScheduler()`）に戻せる。

- **方式 B を採らなかった理由**: 本プロジェクトは FreeRTOS 構成のため、BSP2 公式手順の橋渡し先である
  `hal_entry.c` が FSP により生成されない（代わりに `ra_gen/main.c` が `vTaskStartScheduler()` を呼ぶ）。
  方式 B（最初の FreeRTOS タスク blinky から橋渡し）は、FreeRTOS スケジューラとカーネル構造を一旦起動してから
  μT-Kernel へ移ることになり、二重 RTOS 初期化・スタック/ベクタ競合のリスクが高い。
  方式 A は `main()` に到達しないため FreeRTOS スレッド（blinky/ntshell/camera/lvgl/ai_inference）が
  そもそも生成・起動されず、**最もクリーンに既存 FreeRTOS スレッドを無効化**できる（`ra_gen/` は無編集）。

- **`usermain()` の配置ファイル: `src/usermain.c`**（新規作成）。
  - BSP2 の WEAK 定義（`mtk3_bsp2/mtkernel/kernel/usermain/usermain.c`、`return 0` のみ）を**強い定義で上書き**。
    `inittask.c:127` の初期タスクから呼ばれる。`usermain()` は最小タスク（LED 点滅）を `tk_cre_tsk` + `tk_sta_tsk`
    で生成・起動し、自身は `tk_slp_tsk(TMO_FEVR)` で待ち状態に入る（`usermain()` が return すると μT-Kernel は
    シャットダウンするため終了させない ― 公式手順 4.3.2）。
  - LED 点滅タスク `blink_task(INT stacd, void *exinf)`: FreeRTOS `blinky_thread_entry.c` の最小相当。
    `g_bsp_leds` / `R_BSP_PinWrite` で LED 制御し、`vTaskDelay(configTICK_RATE_HZ/2)` → **`tk_dly_tsk(500)`**（ms 系）。
    タスク生成情報 `T_CTSK` は `TA_HLNG | TA_RNG3`、`itskpri=10`、`stksz=1024`、`bufptr=NULL`
    （`USE_IMALLOC=1` によりスタック自動確保）。**`USE_OBJECT_NAME=0`（config.h:114）のため `T_CTSK` に
    `dsname` メンバは無く、初期化子に含めてはならない**（含めるとコンパイルエラー）。
  - **CPU1（セカンダリコア）起動 ― マルチコア・デバッグ整合のため `usermain()` で実施**:
    - 当初は「最小構成優先で CPU1 起動（`R_BSP_SecondaryCoreStart`）を行わない」方針としたが、**実機デバッグで
      問題が顕在化**した。RA8P1 では CPU1 はリセット保持で立ち上がり、CPU0 が `R_BSP_SecondaryCoreStart()` を
      呼ぶまで解除されない。元の FreeRTOS では `blinky_thread_entry.c` 先頭で呼んでいたが、**方式A は
      `main()`/スケジューラに到達しない**ためこの経路が実行されず、CPU1 がリセット保持のままになる。
    - 結果、**マルチコア・デバッグ（`Debug_Multicore Launch Group`）の CPU1 接続が
      `Command 'monitor enable_stopped_notify_on_connect' is timed out` でタイムアウト**し、デバッグ開始に
      失敗する（区画変更とは無関係）。
    - 対処: `usermain()` の起動ログ直後に、元 blinky と同一ガード
      `#if (0 == _RA_CORE) && (1 == BSP_MULTICORE_PROJECT) && !BSP_TZ_NONSECURE_BUILD` で
      `R_BSP_SecondaryCoreStart()` を呼び、CPU1 を解除する（元の挙動の復元）。これでマルチコア・デバッグの
      CPU1 接続が通る。CPU1 は自プロジェクトのイメージを独立実行する（R-003 の CPU0 受け入れ条件には不干渉）。
    - 代替（CPU1 をあえて止めたままにする場合）: マルチコア Launch Group ではなく **CPU0 単体のデバッグ構成**で
      起動する。

- **HAL 初期化の一度きり保証**:
  - 方式 A では `ra_gen/main.c` の `main()` に到達しないため `g_hal_init()`（FSP モジュール初期化）は実行されない。
    R-003 の最小構成は LED が `R_BSP_PinWrite`（BSP 直接・モジュール不要）、`tm_printf` が SCI8 直接レジスタ操作
    （`tm_com.c`・FSP モジュール不要）であり、**FSP モジュール初期化に依存しないため `g_hal_init()` は不要**。
  - LED 制御に必要な IOPORT は、`R_BSP_WarmStart(POST_C)` 先頭の `R_IOPORT_Open()` で構成済み。
    POST_C フックは BSP から **main() より前に一度だけ**呼ばれることが保証されるため、一度きり初期化が成立する。
  - 後続ステップ（R-004 NT-Shell 以降）で FSP モジュール（UART/I2C/MIPI 等）が必要になった時点で、
    FreeRTOS の `rtos_startup_common_init()` が担っていた `g_hal_init()` 相当の一度きり初期化を
    **`usermain()` の先頭**（初期タスク内・他タスク生成前）へ移設する。`usermain()` は初期タスクから
    1 回だけ呼ばれるため一度きりが自然に保証される。

- **T-Monitor シリアル初期化**: `libtm_init()`（SCI8 を 115200/8N1 に直接レジスタ初期化）は μT-Kernel の
  カーネル初期化シーケンス `sysinit.c:38` で呼ばれる。よって `usermain()` 到達時点で `tm_printf`/`tm_putstring`
  が使用可能。`knl_startup_hw()`（`hw_setting.c`）は実質空で、システムタイマ初期化は `knl_main()` 側で行う。

- **SCI8 と jlink_console UART の競合確認結果（手順書 4.4 注記の確定）**:
  - `tm_com.c`（T-Monitor）は **SCI8**（`SCI8_BASE=0x40358800`、PD02=TXD8/PD03=RXD8）を直接レジスタ操作で
    115200/8N1 に初期化・使用する。
  - 既存 J-Link コンソール `jlink_console.c` も FSP UART（`g_jlink_console_cfg.channel = 8` ― `ra_gen/hal_data.c:206`）
    で **同じ SCI8** を使用する（物理ポートは J-Link 上の VCOM で共通）。**両者は同一 SCI8 を共有する**。
  - R-003 では jlink_console を含む ntshell スレッドを起動しない（方式 A により FreeRTOS スレッド未生成）ため、
    SCI8 を初期化・使用するのは T-Monitor 側のみで**競合は発生しない**。
  - **R-004（NT-Shell 移行）での注意 ― 確定**: ntshell の `R_SCI_B_UART_Open(g_jlink_console_ctrl)` と
    T-Monitor の SCI8 直接初期化が**同一 SCI8 を二重に初期化・送受信して競合する**。
    R-004 では **「起動バナーまで T-Monitor → NT-Shell が SCI8 を FSP UART で開いた後は jlink_console が専有」**
    の段階的一本化方針を採用（→ 7.2 実装メモ「SCI8 一本化」）。さらに方式A では `bsp_irq_cfg()`（IELSR 設定）が
    未実行のため、`usermain()` 先頭で FSP 公開関数 `bsp_irq_cfg()` を呼んで SCI8 の TXI/RXI/TEI/ERI 割り込みを
    NVIC へ結線する（未実施だと `R_SCI_B_UART_Write` 後に `g_transfer_done` が立たずハングする）。詳細は 7.2 を参照。
    最小構成 R-003 の段階ではこの競合・割り込み未結線は顕在化しない（ntshell 未起動・SCI8 割り込み未使用のため）。

- **コードフラッシュ・オーバーフローと対処（R-003 実機ビルドで顕在化・確定）**:
  - 症状: `knl_start_mtkernel()` を**実際に呼ぶ**ようにした R-003 のリンクで、CPU0 のコードフラッシュ領域が
    **約 5.6KB オーバーフロー**した（`ld.lld: section '__flash_readonly$$' ... overflowed by 5598 bytes` 等）。
    コンパイルは全ソース成功し、**リンク段階のみ**失敗。
  - 原因: R-002（`knl_start_mtkernel()` 未呼び出し）では μT-Kernel 本体は**未参照**のため `gc-sections` で
    大半が除去されていた。R-003 で実呼び出しすると `knl_main` → 各サブシステム init が参照され、μT-Kernel が
    リンクに取り込まれる。一方、方式 A でも **FreeRTOS / LVGL / カメラ / AI（Ethos-U）のコードは `ra_gen/main.c`
    経由で依然リンクされ続ける**（移行期は 2 つの RTOS とアプリ全体が同居し、フラッシュ使用がピークになる）。
  - **根本原因はコードフラッシュ区画の偏り**: 当初 CPU0=**960KB**（`FLASH_LENGTH = 0x000f0000`）/ CPU1=**64KB**
    （`0x00010000`、`FLASH_START = 0x020f0000`）。ところが **CPU1 の実イメージは約 11KB**（`llvm-size` 実測:
    text 11,206 / bss 3,696）に過ぎず、**64KB 区画の大半（約 53KB）が遊んでいる**。一方 CPU0 は移行中 2 つの
    RTOS とアプリ全体を抱えて区画上限に達している。総コードフラッシュは 1MB（0x100000）。
  - **対処（2段構え。確定）**:
    1. **使い捨てサブシステムのトリミング（`mtk3_bsp2/mtkernel/config/config_func.h`）**:
       移行で**一度も使わない**サブシステムだけを機能選択スイッチで無効化し、`knl_init_object()`（常時リンクされる
       初期化経路）からの参照を断って `gc-sections` に除去させる:
       - 恒久 0: `USE_MAILBOX` / `USE_MESSAGEBUFFER` / `USE_RENDEZVOUS` / `USE_MEMORYPOOL` /
         `USE_FIX_MEMORYPOOL` / `USE_DEVICE`（API 置換表 5 章はメールボックス／メモリプール等を使わない）。
       - **保持（1）**: `USE_SEMAPHORE` / `USE_EVENTFLAG` / `USE_MUTEX` / `USE_CYCLICHANDLER` /
         `USE_ALARMHANDLER`（R-004〜R-007 で使用。**段階的な再有効化の手間を避けるため最初から有効**）。
       - 効果（`llvm-size` 実測）: 5,598 → **約 1,662 byte 超過**まで縮小（実効削減 約 3.9KB。`gc-sections` は
         関数単位で動くため、各サブシステムの未使用関数は元々除去済みで、削減はおもに `knl_init_object` から
         参照される初期化・制御ブロック分に留まる ― これ以上の機能トリミングは**実コードを削るしかなく頭打ち**）。
    2. **コードフラッシュ区画の再配分（構造的な確定対処）**: 遊んでいる CPU1 区画から CPU0 へフラッシュを移す。
       残り約 1.7KB の超過に対し十分なマージンを確保し、**かつ R-004 以降で μT-Kernel 機能が増えても耐える**
       ため、**CPU0 を 960KB → 約 992KB（+32KB）、CPU1 を 64KB → 約 32KB** へ変更する（CPU1 実使用 約 11KB に
       対し 32KB は十分）。**この操作は solution.xml の Memories タブで行い、ユーザーが手動で実施**（→ 下記手順）。
  - **ユーザー手動操作（FSP / 区画変更）**:
    1. `e2studio/solution.xml`（マルチコア・ソリューション）を開き、**Memories タブ**を選択。
    2. CPU0（Cortex-M85）側のコードフラッシュ割当を **960KB → 992KB** に増やし、CPU1（Cortex-M33）側を
       **64KB → 32KB** に減らす（合計 1MB を維持。FSP がブロック境界へ整合させる）。
    3. **Generate Project Content** を実行（`ra_gen/`・`memory_regions.lld` が新区画で再生成される。`src/` の
       変更と `mtk3_bsp2/` の設定は影響を受けない）。
    4. CPU0・CPU1 を Debug 構成で再ビルド。
  - 安全性: トリミングした各サブシステム本体（`mailbox.c` 等）と `tkinit.c` の `knl_init_object()` 内 init 呼び出しは
    すべて同名の `USE_*` ガードで囲まれており（確認済み）、スイッチ 0 で本体・初期化・制御ブロックテーブルが
    まとめて外れる。RA FSP の `knl_init_device()` は `sysdepend/ra_fsp/devinit.c` で**無条件かつ実体は
    `return E_OK`** のため `USE_DEVICE=0` でも未定義参照は発生しない。`config_bsp.h` の `DEVCNF_USE_HAL_*` は
    元から全て 0（HAL ドライバ未リンク）。CPU1 区画縮小は実使用 11KB << 32KB のため安全。
  - 編集ファイル（トリミング）は vendored の `config_func.h`（BSP2 公式の「機能選択」設定ファイル）。変更箇所に
    `[mimamori-sense R-003]` コメント。**FSP 再生成では消えないが、BSP2 を再 vendoring した場合は再適用が必要**
    （→ 6.2 再適用チェックリスト）。区画再配分は `solution.xml`（FSP 管理）側の設定で、`configuration.xml` 同様
    ユーザーが GUI で管理する。
  - 将来: 各ステップで FreeRTOS コードを μT-Kernel へ置換し終えると CPU0 フラッシュに余裕が出るため、移行完了後
    （R-008）に区画を見直して CPU1 へ戻す余地がある。

- **初期タスク生成失敗（`!ERROR! Initial Task can not creat`） ― 2 つの RAM 不具合
  （R-003 実機ランタイムで顕在化・確定）**:
  - 症状: ビルド・書込・μT-Kernel 起動後、シリアルに **`!ERROR! Initial Task can not creat`** が出力され、
    `usermain()`（バナー）に到達せず LED も点滅しない。`sysinit.c` がカーネル初期タスク（`usermain` を呼ぶタスク）の
    `tk_cre_tsk()` に失敗（`task_manage.c` の `knl_Imalloc()` が NULL → `E_NOMEM`）。
  - 切り分け方法: `sys_start.o` を `llvm-objdump -d` で逆アセンブルし、`knl_lowmem_limit` へ格納する即値を確認したところ
    **`0x220E0000`** だった（`movt r3,#0x220e; str r3,[&knl_lowmem_limit]`）。`__mtk3_SYSMEM_START`（実 RAM 使用末尾）
    は `0x2211e100` で、**プール上限 < プール先頭**となり領域が空/負 → `knl_init_Imalloc` の領域計算が破綻していた。
  - **原因① BSP2 ベンダ不具合（主因）**: `include/sys/sysdepend/ra_fsp/ek_ra8p1/sysdef.h` が CPU 定義として
    **誤って `cpu/ra8m1/sysdef.h` を include**していた（ek_ra8m1 からのコピー時の取り違え。mtk3_bsp2 v1.00.04）。
    RA8M1 は `INTERNAL_RAM_SIZE = 0x000E0000`（896KB）で、RA8P1 の正値 `0x001D4000`（1872KB）と異なる。
    このため `INTERNAL_RAM_END = 0x22000000 + 0xE0000 = 0x220E0000` となり、実 RAM 使用末尾より低位を指していた。
    **対処: `ek_ra8p1/sysdef.h` の include を `cpu/ra8p1/sysdef.h` に修正**（INTERNAL_RAM、N_INTVEC 等が RA8P1 の
    正値になる）。修正箇所に `[mimamori-sense R-003]` コメント。
  - **原因② マルチコア RAM 区画はみ出し（①修正後に顕在化する 2 つ目）**: ①を直すと
    `INTERNAL_RAM_END = 0x221D4000`（SRAM 全 1872KB を**単一コア占有前提**）になる。しかし本機はマルチコアで、
    FSP は CPU0 の RAM 区画を `0x22000000..0x221B0000`（1728KB ― `memory_regions.lld` の `RAM_START` +
    `RAM_LENGTH = 0x1b0000`）に制限。`0x221B0000..0x221D4000`（144KB）は **CPU1 区画**で CPU0 は所有しない。
    既定（`CNF_SYSTEMAREA_END = 0`）だとプールが `[__mtk3_SYSMEM_START, 0x221D4000]` となり、`knl_init_Imalloc()`
    が高位エリア境界（AreaQue 終端、`knl_lowmem_limit` 近傍）の QUEUE 構造を CPU1 区画へ書いてプールが壊れる。
    **対処: `mtk3_bsp2/config/config.h` の `CNF_SYSTEMAREA_END = 0x221B0000`（CPU0 RAM 末尾）**。`sys_start.c` の
    `if((SYSTEMAREA_END != 0) && (INTERNAL_RAM_END > CNF_SYSTEMAREA_END)) knl_lowmem_limit = SYSTEMAREA_END - EXC_STACK_SIZE;`
    が効き、`knl_lowmem_limit = 0x221B0000`（`EXC_STACK_SIZE=0`）。
  - 結果（①＋②）: プール = `[__mtk3_SYSMEM_START(≒0x2211e100), 0x221B0000]` ≒ 585KB、全て CPU0 RAM 内で有効。
    `g_heap`/`g_main_stack` は RAM 低位 `0x220000e0` 付近にあり、この空きと重ならないことを map で確認済み。
    **両方の修正が必要**（①だけだとプールが空/負、②だけ＝①未修正だと `INTERNAL_RAM_END=0x220E0000<CNF_SYSTEMAREA_END`
    で条件不成立となり cap が効かない）。
  - 代替案（不採用）: `USE_STATIC_SYS_MEM=1`（`config_bsp.h`）で固定長 `knl_system_mem[]` を使う方法。確実だが
    `.mtk_sysmem` セクションがどのリンカスクリプトにも配置定義が無く（orphan 配置依存）、固定サイズが移行後半で
    不足し得るため見送り。原因①は sysdef のターゲット取り違えという明確なベンダ不具合のため、CPU 定義の修正で対応。
  - **注意（再 vendoring / RAM 区画変更時）**: ①`ek_ra8p1/sysdef.h` の RA8P1 include 修正、②`CNF_SYSTEMAREA_END`
    の CPU0 RAM 末尾値 ― いずれも BSP2 を再 vendoring すると失われるため再適用必須。RAM 区画（CPU0/CPU1 の SRAM 配分）を
    変更した場合は②の値を新しい CPU0 RAM 末尾へ追従させること（コードフラッシュ区画とは別物。今回は RAM 区画は既定のまま）。

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

実装メモ（R-004 / 2026-06-12 実装。実機確認はユーザー実施）:

- **ntshell の uT-Kernel タスク化**:
  - `src/ntshell_thread_entry.c` のスレッド本体を uT-Kernel タスク形式
    `void ntshell_task(INT stacd, void *exinf)` へ移植。FreeRTOS 依存（`FreeRTOS.h`/`task.h`,
    `FSP_PARAMETER_NOT_USED`, 起動バナーの `tskKERNEL_VERSION_NUMBER`）を除去/置換。
  - `src/usermain.c` に NT-Shell タスクの生成情報 `T_CTSK ctsk_ntshell`（`TA_HLNG|TA_RNG3`,
    `itskpri=12`（blink=10 より低優先度・数値大）, `stksz=4096`（FreeRTOS 版スレッドと同等）,
    `bufptr=NULL`）を追加し、`tk_cre_tsk`+`tk_sta_tsk` で生成・起動。**`USE_OBJECT_NAME=0` のため
    `T_CTSK.dsname` は初期化子に含めない**（R-003 と同様）。
  - 旧 FreeRTOS エントリ `ntshell_thread_entry(void*)` は**削除せず**、本体を `ntshell_task(0, NULL)`
    へ委譲する薄いラッパとして残置。理由: `ra_gen/ntshell_thread.c`（編集禁止）の
    `ntshell_thread_func()` が `ntshell_thread_entry` を参照し、その参照鎖は startup が参照する
    `main()` から辿れる（方式A で `main()` は実行されないが**リンク時には解決が必要**）。
    `#if 0` で消すと未解決シンボルになるため、リンク可能なまま残す（FreeRTOS へ切り戻しても動作）。
- **SCI8 一本化（T-Monitor と FSP UART の競合解決）**:
  - 方針: 起動バナー（`usermain()` 序盤）までは T-Monitor（`tm_printf`/`tm_putstring`, SCI8 直接
    レジスタ）を使用。NT-Shell タスクが `jlink_console_init()`（`R_SCI_B_UART_Open`, channel=8）で
    SCI8 を FSP UART として開いた後は、**SCI8 を jlink_console が専有**し、`usermain()` 以降は
    T-Monitor を使わない（同一 SCI8 への二重送信を避ける）。`tm_putstring` はポーリング送信
    （TEND 待ち）でブロッキングのため、バナーは NT-Shell が SCI8 を開く前に送信完了済み。
- **割り込み構成の解決（方式A で `bsp_irq_cfg()` 未実行への対処 ― 最重要）**:
  - 調査結果: FSP の `R_SCI_B_UART_Open()` は内部で `R_BSP_IrqCfg`（NVIC 優先度 + ISR コンテキスト）と
    `R_BSP_IrqEnable`（NVIC 有効化）を rxi/txi/tei/eri に対して行う（`r_sci_b_uart.c:1403-1408,382-392`）が、
    **ELC イベント -> NVIC ベクタの対応（`R_ICU->IELSR[]`）は設定しない**。この IELSR 設定は FSP の
    `bsp_irq_cfg()`（`bsp_irq.c:237`、`g_interrupt_event_link_select[]` を IELSR へ書く）が担い、
    通常は `SystemInit()` が `__init_array` 実行**直後**（`system.c:525`）に呼ぶ。
  - 方式A（静的コンストラクタ `mimamori_start_mtkernel` で `knl_start_mtkernel()` を呼び戻らない）では、
    コンストラクタは `__init_array` の一部として実行されるため、その後の `bsp_irq_cfg()`（system.c:525）に
    **到達しない**。結果 IELSR が未設定のままになり、`R_SCI_B_UART_Open` 後も SCI8 の TXI/RXI/TEI/ERI が
    NVIC ベクタに結線されず割り込みが発火しない（`R_SCI_B_UART_Write` 後に `g_transfer_done` が立たず
    `jlink_console_write` がハングする）。
  - **対処（src 側・ra_gen 無編集）**: `usermain()` の先頭（uT-Kernel 初期タスク内・他タスク生成前 = 一度きり
    保証）で **FSP 公開関数 `bsp_irq_cfg()` を呼ぶ**。`bsp_irq_cfg()` は `bsp_api.h`（→`hal_data.h` 経由で
    可視）に宣言され、IELSR と ICU セキュリティ状態を書くだけで副作用が少なく一度呼べば足りる。これにより
    以降の `R_SCI_B_UART_Open()`（NVIC 有効化）と組み合わさって SCI8 割り込みが正しく発火する。
  - `g_hal_init()` は呼ばない: `g_hal_init()`→`g_common_init()` は **FreeRTOS の `xEventGroupCreateStatic`/
    `xSemaphoreCreateBinaryStatic`（I2C 用、R-005 で使用）を生成するだけ**で（`common_data.c:908`）、
    割り込み構成（IELSR）には寄与しない。むしろ未起動の FreeRTOS オブジェクトを生成するため R-004 では呼ばない。
    SCI8 の FSP モジュール初期化は `jlink_console_init()` の `R_SCI_B_UART_Open` が単体で行う。
- **`jlink_console.c` のクリティカルセクション置換**:
  - `vTaskDelay(1)`（4 箇所: `print_to_console`/`jlink_console_write`/`input_from_any_console`/
    `input_from_console`）→ `tk_dly_tsk(1)`。
  - `taskENTER_CRITICAL()`/`taskEXIT_CRITICAL()`（3 箇所: `input_from_console` の 2 箇所・`get_new_chars` の 1 箇所）
    → **`DI(intsts)`/`EI(intsts)`（割り込みマスク）**。これらは UART RX ISR（`jlink_console_callback()`）が
    更新する `s_out_of_band_received[]`/`s_g_out_of_band_index` を保護しており、ISR と共有するため
    `tk_dis_dsp()`（ディスパッチ禁止のみ）は不可（手順書 5 章の落とし穴）。ARMv8-M（Cortex-M85）の
    `DI`/`EI` は BASEPRI を操作し、FreeRTOS の `taskENTER_CRITICAL()` と同一機構（カーネルレベル以下の
    割り込みをマスク）。保護区間はごく短い（数バイトの pop/copy）ため全カーネルレベル割り込みの一時マスクで問題ない。
  - `assert` は従来 FreeRTOS の include 連鎖経由で可視だったため、FreeRTOS include 除去に伴い
    `#include <assert.h>` を明示追加。
- **`usrcmd.c`**: `vTaskDelay(pdMS_TO_TICKS(100))`（`usrcmd_reset`）→ `tk_dly_tsk(100)`。
  `tskKERNEL_VERSION_NUMBER`（info/version の RTOS 版数表示 2 箇所）→ 固定文字列 `"uT-Kernel 3.0"`
  （`MTKERNEL_VERSION_NUMBER` マクロ。表示ラベルも `FreeRTOS` → `RTOS` に変更）。`<tk/tkernel.h>` を include、
  FreeRTOS include を除去。**lvgl/camera/ai サブコマンドは R-005 以降の対象のため未変更**。
- **`led_ctrl.c` の周期ハンドラ化**:
  - FreeRTOS ソフトウェアタイマ（`xTimerCreate/Start/Stop/ChangePeriod`）→ uT-Kernel 3.0 周期ハンドラ
    （`tk_cre_cyc`/`tk_sta_cyc`/`tk_stp_cyc`/`tk_del_cyc`）。**LED ごとに 1 つの周期ハンドラ**を持つ設計
    （旧 1 タイマ/LED と対応）。1 つのハンドラ関数 `led_blink_cyc_handler(void *exinf)` を全 LED で共用し、
    LED インデックスは `T_CCYC.exinf` で渡す。
  - `T_CCYC`: `cycatr = TA_HLNG | TA_STA | TA_PHS`（C 記述・生成時即起動・位相保存）、`cyctim = cycphs = interval_ms`
    （ミリ秒系 RELTIM, 最小 1ms）。**`USE_OBJECT_NAME=0` のため `dsname` は初期化子に含めない**。
  - 周期ハンドラは割り込みコンテキスト相当で動作。中で呼ぶのは `R_IOPORT_PinRead`/`R_IOPORT_PinWrite`
    （FSP・RTOS 非依存）のみで、待ち系システムコールは呼ばない（手順書 5.1 ISR 制約を満たす）。
  - **uT-Kernel 3.0 には `tk_set_cyc` が無い**ため、blink interval 変更（旧 `xTimerChangePeriod`）は
    既存ハンドラを `tk_stp_cyc`+`tk_del_cyc` で削除 → 新間隔で `tk_cre_cyc`+`tk_sta_cyc` し直す方式で実現
    （`led_ctrl_blink()` 内で `led_stop_blink_timer()` を先に呼んで再生成）。
  - 周期ハンドラはオンデマンド生成（`led_ctrl_blink()` 時に作成、停止時に削除）。`led_ctrl_init()` は
    内部状態の初期化と全 LED OFF のみ行う（旧 init は全タイマを事前生成していたが、再生成方式に合わせて廃止）。
    `led_ctrl.h` の `led_ctrl_init`/`led_ctrl_blink` の Doxygen コメントも FreeRTOS タイマ言及から更新。
- **`config_func.h` 確認**: `USE_CYCLICHANDLER=1`/`USE_SEMAPHORE=1`/`USE_MUTEX=1`/`USE_EVENTFLAG=1`
  は R-003 で保持済み（変更不要）。本実装で追加で必要な機能スイッチは無し。
- **UART 送受信の排他について**: 本実装では NT-Shell は単一タスクから SCI8 を駆動し、複数タスクからの
  同時アクセスは無いため、`tk_cre_mtx`/`tk_cre_sem` による UART 排他は導入していない（手順書の
  「UART 送受信の排他は `tk_cre_mtx`/`tk_cre_sem` を用いる」は複数タスク競合時の指針。R-005 以降で
  camera/lvgl 等が `print_to_console` を併用する場合に再検討する）。ISR と共有する状態の保護は上記の
  割り込みマスク（`DI`/`EI`）で対応済み。
- **`blink_task` と `led` コマンドの LED 競合解消（実機確認で発覚 → R-004 で修正）**:
  - 症状: `led 0 off` 等を実行してもコマンドは成功表示するが LED が点滅したまま戻る。
  - 原因: R-003 で `usermain.c` に作成した `blink_task` が、元の FreeRTOS `blinky_thread_entry.c` の
    **マルチコア分岐（`#else`: 自コア `_RA_CORE` のインデックスの LED 1 個のみ点滅）を落として、
    全ボード LED（Blue/Green/Red）を無条件で 500ms トグル**する実装になっていた。`led` コマンド
    （`led_ctrl`）が同じ物理ピンを LOW にしても直後に `blink_task` が点滅へ戻すため、R-004 受け入れ条件
    「既存デバッグコマンド（mr/md/mw/**led**）が動作する」を満たせていなかった。
  - 移行前（FreeRTOS）の挙動: デュアルコアのため `#else` 側が有効で、CPU0 は `p_leds[0]`=LED1(Blue, P600)
    のみ、CPU1 は `p_leds[1]`=LED2(Green) のみを点滅。`p_leds[2]`=LED3(Red) は誰も点滅させないため
    `led 2` が自由に効いた（`led 0`/`led 1` は点滅タスクと競合する仕様）。
  - 対処（移行前の挙動へ復元）: `blink_task` のループを元の blinky と同一の
    `#if BSP_NUMBER_OF_CORES == 1 … #else …#endif` 構造に戻し、マルチコア構成では
    `R_BSP_PinWrite(leds.p_leds[_RA_CORE], …)` で**自コアのインデックスの LED 1 個だけ**を点滅させる。
    結果、CPU0 は Blue のみ点滅、Green は CPU1（FreeRTOS のまま・移行対象外）の blinky が点滅、
    Red はどちらのコアも点滅させない。**`led` コマンドで自由に操作できるのは Red のみ**となり、
    `led 0`(Blue) は CPU0 の `blink_task`、`led 1`(Green) は CPU1 の blinky と競合する
    （いずれも移行前と同じ競合関係に戻る）。

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

実装メモ（R-005 / 2026-06-12 実装。実機確認はユーザー実施）:

- **camera スレッドの uT-Kernel タスク化**:
  - `src/camera_thread_entry.c` のスレッド本体を uT-Kernel タスク形式
    `void camera_task(INT stacd, void *exinf)` へ移植。FreeRTOS 依存
    （`FreeRTOS.h`/`task.h`/`event_groups.h`, `vTaskDelay`/`pdMS_TO_TICKS`→`tk_dly_tsk(ms)`,
    `g_i2c_event_group` の `xEventGroupWaitBits`）を除去/置換。
  - `src/usermain.c` に `T_CTSK ctsk_camera`（`TA_HLNG|TA_RNG3`, **`itskpri=11`**（blink=10 と
    ntshell=12 の中間。カメラ初期化は NT-Shell の対話処理よりリアルタイム性が高いため
    ntshell より高優先度・数値小、一方 LED 点滅周期への影響を避けるため blink と同等〜やや低く）,
    `stksz=4096`（FreeRTOS 版 camera_thread と同等）, `bufptr=NULL`）を追加し、
    ntshell タスク起動の後に `tk_cre_tsk`+`tk_sta_tsk` で生成・起動。**`USE_OBJECT_NAME=0` のため
    `T_CTSK.dsname` は初期化子に含めない**。生成ログは blink/ntshell と同様 **T-Monitor
    （`tm_putstring`/`tm_printf`）** で出力する（**SCI8 一本化の要点・実機修正で確定**）。当初
    `print_to_console`（jlink_console）で出力したところ、本コード実行時点では NT-Shell がまだ
    `jlink_console_init()` 未実行で SCI8 未オープンのため、`print_to_console` が `jlink_configured()`
    待ちで `tk_dly_tsk(1)` 譲り → NT-Shell が SCI8 を開いてバナー出力開始 → 復帰した usermain と
    同時に SCI8 へ書き込み**競合・文字化け**（実機で `m□` 様の化け＋本ログ消失を確認）した。
    usermain の全ログは tm_putstring（ポーリング送信・ブロッキング）で `tk_slp_tsk(TMO_FEVR)` 前に
    送信し切ってから NT-Shell が SCI8 を開く、という R-004 の一本化設計に揃える。
  - 旧 FreeRTOS エントリ `camera_thread_entry(void*)` は**削除せず**、本体を `camera_task(0, NULL)`
    へ委譲する薄いラッパとして残置。理由: `ra_gen/camera_thread.c`（編集禁止）の `camera_thread_func()`
    が `camera_thread_entry` を参照し、その参照鎖は startup が参照する `main()` から辿れる（方式A で
    `main()` は実行されないが**リンク時には解決が必要**）。ntshell_thread_entry.c 末尾と同一パターン。
- **I2C 完了割り込み→タスク同期の置換（uT-Kernel イベントフラグ新設 ― 最重要）**:
  - 背景: I2C 完了の割り込み→タスク同期の実体は FreeRTOS イベントグループ `g_i2c_event_group`
    （`ra_gen/common_data.c` の `g_common_init()`/`g_hal_init()` で生成）だが、**方式A では
    `g_hal_init()` が呼ばれないため実行時に未生成（NULL）**。そのため FreeRTOS のまま放置すると
    カメラ I2C 完了待ちが機能しない。
  - 対処: `src/ov5640.c` に **uT-Kernel イベントフラグ `s_i2c_flgid`** を新設。
    - 新規公開 `void ov5640_i2c_sync_init(void)`: `tk_cre_flg`（`T_CFLG`: `flgatr=TA_TFIFO|TA_WMUL`,
      `iflgptn=0`。`dsname` は `USE_OBJECT_NAME=0` のため含めない）でイベントフラグを生成。**冪等**
      （`s_i2c_flgid>0` なら再生成しない）。`camera_task` 先頭（各 I2C 操作より前）で呼ぶ。
    - `i2c_camera_callback`（ISR）: `xEventGroupSetBitsFromISR`/`xHigherPriorityTaskWoken`/
      `portYIELD_FROM_ISR` を全廃し、TX/RX 完了で `tk_set_flg(s_i2c_flgid, I2C_TRANSFER_COMPLETE)`、
      ABORT で `tk_set_flg(..., I2C_TRANSFER_ABORT)`。`tk_set_flg` は通知系で ISR から呼んでよい
      （手順書 5.1）。uT-Kernel は割り込み出口で遅延ディスパッチするため明示的 yield 不要。末尾の
      `R_BSP_IrqStatusClear(R_FSP_CurrentIrqGet())` は維持。
    - `ov5640_i2c_wait_complete`（**static 除去・公開化**, `ov5640.h` にプロトタイプ追加）:
      `xEventGroupWaitBits` → `tk_wai_flg(s_i2c_flgid, COMPLETE|ABORT, TWF_ORW|TWF_BITCLR, &flgptn,
      I2C_TIMEOUT_MS)`。戻り値で分岐: `E_OK`+COMPLETE→`FSP_SUCCESS`、ABORT→`FSP_ERR_ABORTED`、
      `E_TMOUT`→`FSP_ERR_TIMEOUT`、その他（`E_ID` 等）→`FSP_ERR_ABORTED`。`TWF_BITCLR`（一致ビットのみ
      クリア）を採用（元 FreeRTOS の pdTRUE=該当ビットクリア相当）。
    - `I2C_TIMEOUT_MS` は `portTICK_PERIOD_MS` 依存を除去しミリ秒定数（1000）に。ビットパターン
      `I2C_TRANSFER_COMPLETE`(1<<0)/`I2C_TRANSFER_ABORT`(1<<1) はそのまま流用。
  - `camera_thread_entry.c` のインライン待ち（board switch readback verify, GreenPAK,
    board switch init の各 `xEventGroupWaitBits(g_i2c_event_group, ...)`）は全て公開関数
    `ov5640_i2c_wait_complete()` へ統一。`EventBits_t uxBits;` 宣言と判定を整理し、`g_i2c_event_group`
    参照・ファイル内の `I2C_XFER_COMPLETE`/`ABORT`/`TIMEOUT_MS` マクロ（未使用化）を削除。
- **camera_framebuffer.c の ISR FPS 計測**:
  - `xTaskGetTickCountFromISR()`（`vin0_callback`→frame_complete 割り込みから呼ばれる）→
    **`tk_get_otm(SYSTIM*)`**（ISR 可・参照系、手順書 5.1）。`SYSTIM` は 64bit（`hi`/`lo`）でミリ秒を返す。
    FPS は秒内差分のため下位 32bit（`.lo`）を ms 値として使用（`SYSTIM now; tk_get_otm(&now);
    uint32_t now_ms=(uint32_t)now.lo;`）。`init` 内の `xTaskGetTickCount()` も同様に置換。
  - `FPS_INTERVAL_TICKS`(=`configTICK_RATE_HZ`)→`FPS_INTERVAL_MS`(=1000)。`s_fps_last_tick`(`TickType_t`)
    →`s_fps_last_ms`(`uint32_t` ms)。FPS 計算式 `frames*configTICK_RATE_HZ/elapsed`→`frames*1000/elapsed_ms`
    （単位 ms で一貫するので係数 1000）。`camera_framebuffer_get_info` の `__disable_irq()/__enable_irq()`
    は RTOS 非依存のため維持。
- **camera_display.c は R-006（LVGL）へ繰り越し**:
  - `src/camera_display.c` は LVGL タイマ駆動（`lv_timer_handler`）かつ AI 連携（`g_ai_app_event`,
    R-007）であり、**R-005 では実行されず**、Issue 作業項目にも非記載。FreeRTOS ヘッダは維持されており
    現状コンパイルは通るため、R-005 では変更しない（LVGL の OSAL 方針確定後 R-006 で移行）。
- **vin_port.c は変更不要**:
  - `src/port/vin_port.c` は FreeRTOS API 非依存（`__disable_irq`/`__enable_irq` のみ・RTOS 非依存）の
    ため R-005 では変更しない。

### 7.4 LCD / LVGL（R-006 / 前提 R-006a）

前提: **R-006a（#159）で LVGL OSAL 対応方針を確定済み（2026-06-12）**。
詳細は **`doc/migration/r006a-lvgl-osal-spike.md`**（スパイク報告書）を参照。

**R-006a の結論（確定）: 案A「μT-Kernel 向け LVGL OSAL 自作（`LV_USE_OS = LV_OS_CUSTOM`）」を採用。**
- 比較した 3 案:
  - **案A（採用）**: μT-Kernel 向け LVGL OSAL 自作（`src/lv_os_mtkernel.{h,c}` 新規 +
    `lv_conf_user.h` で `LV_USE_OS LV_OS_CUSTOM` / `LV_OS_CUSTOM_INCLUDE "lv_os_mtkernel.h"`）
  - 案B（不採用）: CMSIS-RTOS2 抽象層経由 ― μT-Kernel 向け CMSIS-RTOS2 実装が存在せず、
    自作するなら案A を直接作る方が層が少なく合理的（二重抽象の保守・性能劣化）
  - 案C（不採用・フォールバック）: `LV_OS_NONE` ― `lv_lock` が no-op 化し NT-Shell
    （usrcmd.c の lv_lock 区間×4 箇所）との排他を自前再設計する必要が生じる上、
    後述の **FSP 層 FreeRTOS 依存は `LV_USE_OS` と無関係に残る**ため削減効果が小さい
- 採用根拠の要点:
  1. FSP 生成 `lv_conf.h` の `LV_USE_OS` は `#ifndef` ガード付き → **`src/lv_conf_user.h`
     だけで切替可能（`ra/lvgl/` `ra_cfg/` 無編集・ビルド除外不要・FSP 再生成耐性が完全）**。
     `lv_freertos.c` は `#if LV_USE_OS == LV_OS_FREERTOS` ガードで空コンパイルされる。
  2. 現行 60fps 実績構成（dave2d レンダスレッド + lv_lock 契約）を OS だけ差し替えて維持。
     usrcmd.c 等のユーザーコードは無変更。
  3. 案A で問題が出た場合は `lv_conf_user.h` の `LV_USE_OS` を `LV_OS_NONE` へ**コンパイル
     時に切り替える**ことで案C（同期描画）へ縮退可能（OSAL 以外の成果物は全案共通のため
     流用可。ただし `lv_lock` no-op 化のため usrcmd.c の `lvgl` コマンドは一時無効化が必要）。
     ※「`lv_thread_init` を失敗させれば実行時に自動退避」は不成立（`lv_draw_dave2d.c` は
     戻り値を無視し、ディスパッチが `#if LV_USE_OS` のコンパイル時分岐で sync signal を
     呼び続けるため描画が停止する。スパイク報告書 4 章の注意を参照）。

**R-006a の重要発見 ― OSAL の外側にある FSP 層（`ra/fsp/`・読み取り専用）の FreeRTOS 依存。
`BSP_CFG_RTOS==2` 固定のため LV_USE_OS の選択と無関係に、方式A では必ず対処が必要**:

| FSP ファイル | 依存内容 | 対処 |
|------|------|------|
| `rm_lvgl_port.c` | Vsync セマフォ（ISR Give / flush 待ち `xSemaphoreTake(portMAX_DELAY)`）・LVGL tick | **バイパス**: `RM_LVGL_PORT_Open` を呼ばず、`src/port/lvgl_port_mtk3.c`（新規）で display cfg をコピーし callback を差し替えて `R_GLCDC_Open`。Vsync は `tk_cre_sem`/`tk_sig_sem`(ISR)/`tk_wai_sem`、tick は `tk_get_otm` |
| `r_drw_irq.c`（D/AVE 2D） | `d1_queryirq` が `xSemaphoreTake` ブロック（d2 ライブラリ内部から呼ばれるためバイパス不可） | **ビルド除外 + 同名シンボル置換**: `ra/fsp/src/r_drw/r_drw_irq.c` を Exclude from Build（ユーザー手動・`.cproject` 保存で再生成耐性あり）し、`src/port/r_drw_irq_mtk3.c` で `d1_initirq_intern`/`d1_shutdownirq_intern`/`d1_queryirq`/`drw_int_isr` の **4 シンボル全部**を μT-Kernel セマフォで再実装（`d1_shutdownirq_intern` は `r_drw_base.c:99` から呼ばれており、欠けるとリンクエラー） |
| `rm_comms_i2c_driver_ra.c`（タッチ I2C） | バス排他/完了ブロッキングに FreeRTOS セマフォ | **コールバックモード化**: `g_comms_i2c_bus0_extended_cfg`（ra_gen・非 const）の `p_blocking_semaphore`/`p_bus_recursive_mutex` を open 前に実行時 NULL 化（両方 NULL は driver の正規サポート）。完了待ちは ov5640 と同じ uT-Kernel イベントフラグ |

対象（変更ファイル一覧の確定版はスパイク報告書 5.1）:
`src/lv_os_mtkernel.{h,c}`（新規）, `src/port/lvgl_port_mtk3.{c,h}`（新規）,
`src/port/r_drw_irq_mtk3.c`（新規）, `src/lv_conf_user.h`, `src/User_FreeRTOSConfig.h`,
`src/port/glcdc_port.c`, `src/port/lv_port_indev.c`, `src/lvgl_thread_entry.c`,
`src/usermain.c`, `src/camera_display.c`, `src/ui/fall_detection_screen.c`,
`src/port/dave2d_port.c`, `mtk3_bsp2/config/config.h`。

差し替えポイント（確定）:
- `lvgl_thread_entry.c`: uT-Kernel タスク `lvgl_task(INT, void*)` 化（旧エントリはラッパ残置）。
  ループは `lv_timer_handler()` の戻り値（次タイマまでの ms）で `tk_dly_tsk`。
- `usermain.c`: `T_CTSK ctsk_lvgl`（itskpri=14 / stksz=8192）。OSAL が生成する描画スレッド
  （dave2d/swdraw 各 8KB）は PRIO_HIGH → itskpri=13 にマップ（スパイク報告書 5.7 の優先度表）。
- `User_FreeRTOSConfig.h`: `traceTASK_SWITCHED_IN/OUT` フックを**削除**
  （`lv_freertos.c` が空になり `lv_freertos_task_switch_in/out` が未定義シンボル化するため必須）。
- `lv_conf_user.h`: `LV_SYSMON_GET_IDLE` override を削除し、自作 OSAL の
  `lv_os_get_idle_percent()`（`lv_timer_get_idle()` 委譲）に解決させる。
- `mtk3_bsp2/config/config.h`: **`CNF_MAX_MTXID 4→16`**（LVGL が general + builtin mem +
  xd2 + キャッシュ群で 6 個以上の mutex を使用）。`CNF_TIMER_PERIOD=10` のままだと
  `tk_dly_tsk` 量子化でリフレッシュ実効 ~50fps（KPI 30fps 充足）。60fps 狙いは
  `CNF_TIMER_PERIOD=1` を実測判断。
- `lv_mutex_lock_isr`/`lv_lock_isr` は μT-Kernel 非対応（ISR から mutex 不可）だが
  **本プロジェクト未使用を grep で確認済み**（`LV_RESULT_INVALID` 返却で実装）。

実装ステップ（小さく刻む ― スパイク報告書 5.9）:
①OSAL+表示ポート+r_drw 置換で空画面（カラーバー）表示 → ②タッチ → ③カメラ表示+
`lvgl` コマンド → ④FPS 計測・`CNF_TIMER_PERIOD` 調整。

確認: LCD 表示(カメラ画像・UI)が正常表示。フレームレート 30fps 目標への影響確認。

実装メモ（R-006 / 2026-06-12 実装。実機確認はユーザー実施）:

- **OSAL（案A）**: `src/lv_os_mtkernel.h`（lv_thread_t/lv_mutex_t/lv_thread_sync_t の 3 型）+
  `src/lv_os_mtkernel.c`（OSAL 全 API）を新規実装。`src/lv_conf_user.h` に
  `LV_USE_OS LV_OS_CUSTOM` / `LV_OS_CUSTOM_INCLUDE "lv_os_mtkernel.h"` を追加
  （FSP lv_conf.h の `#ifndef` ガードで ra/ 無編集のまま有効化。`lv_freertos.c` は空コンパイル）。
  - スレッド: `tk_cre_tsk`+`tk_sta_tsk`（トランポリンで pfn/arg 受け渡し、終了時 `tk_exd_tsk`）。
    優先度マップ: HIGHEST=12 / **HIGH=13（dave2d・swdraw）** / MID=14 / LOW=15 / LOWEST=16。
    **`lv_thread_init` は確実に成功させる**（`lv_draw_dave2d.c:104`・`lv_draw_sw.c:98` は戻り値を
    無視し、失敗すると描画ハングになるため失敗時は `LV_LOG_ERROR` で顕在化）。
  - mutex: `tk_cre_mtx(TA_INHERIT)` + owner(`tk_get_tid`)/count による再帰ラッパ
    （μT-Kernel mutex は非再帰）。生成は遅延・冪等（生成競合は `tk_dis_dsp` で直列化）。
    `lv_mutex_lock_isr`/`lv_lock_isr` は **非対応**（`LV_RESULT_INVALID`。本 PJ 未使用確認済み）。
  - sync: カウンティングセマフォ（`TA_CNT`, maxsem=32767）。`signal_isr` は `tk_sig_sem`
    （通知系・ISR 可、遅延ディスパッチのため明示 yield 不要）。
  - `lv_os_get_idle_percent()` = `lv_timer_get_idle()` 委譲（`LV_SYSMON_GET_IDLE` のデフォルト解決先。
    lv_conf_user.h の override は削除。精度は「LVGL タスク内アイドル」基準に低下するが表示用途には十分）。
- **FSP 層 3 件の対処**（上の表のとおり実装）:
  1. `src/port/lvgl_port_mtk3.{c,h}`（新規）: `RM_LVGL_PORT_Open` をバイパス。
     `g_lvgl_port_cfg`（ra_gen）読み取り専用再利用、`g_display0_cfg` を**コピー**して
     `p_callback` のみ差し替え `R_GLCDC_Open`。FB クリア → Open → Start → BufferChange(fb1) →
     `lv_display_create` → flush/flush_wait/buffers(DIRECT) → `lv_tick_set_cb`（`tk_get_otm`）の順で
     `rm_lvgl_port.c:82-171` と 1:1。Vsync は `tk_cre_sem(maxsem=1)` / ISR `tk_sig_sem`（E_QOVR 無視）/
     flush_wait は二段取り `tk_wai_sem(TMO_POL)`→`tk_wai_sem(TMO_FEVR)`。既存の Vsync/underflow 統計
     コールバック（`lvgl_glcdc_callback`）へのイベント変換・転送も原本と同一に維持。
  2. `src/port/r_drw_irq_mtk3.c`（新規）+ **`ra/fsp/src/r_drw/r_drw_irq.c` をビルド除外（ユーザー手動・
     Debug 構成のみ。Release 構成は他にも未整備の設定があり本プロジェクトでは未使用のため対象外）**。
     除外ファイルの全シンボル 4 つ（`d1_initirq_intern` / `d1_shutdownirq_intern`（`r_drw_base.c:99`
     参照・欠落でリンクエラー）/ `d1_queryirq` / `drw_int_isr`（`ra_gen/vector_data.c:17` のベクタが
     同名参照））を実装。OS 部のみ置換（binary sem → `tk_cre_sem(maxsem=1)`、
     `xSemaphoreTake(timeout)` → `tk_wai_sem((TMO)timeout)`（`d1_to_wait_forever(-1)==TMO_FEVR`）、
     Give FromISR → `tk_sig_sem`）。レジスタ操作・dlist indirect 継続は原本と同一ロジック。
  3. `src/port/lv_port_indev.c`: open 前に `g_comms_i2c_bus0_extended_cfg`（ra_gen・非 const）の
     `p_blocking_semaphore`/`p_bus_recursive_mutex` を実行時 NULL 化（**コールバックモード化**。
     両方 NULL はドライバ正規サポート: `rm_comms_i2c.c:99-104`、各操作は NULL ガード済み:
     `rm_comms_i2c_driver_ra.c:119-130/167-178/219-230/326-331/353-358`）。
     I2C 完了は `s_touch_i2c_flgid`（`tk_cre_flg`/ISR `tk_set_flg`/`tk_wai_flg(TWF_ORW|TWF_BITCLR)`、
     ov5640.c と同一パターン）、タッチ IRQ は `s_touch_irq_semid`（maxsem=1/ISR `tk_sig_sem`/
     `tk_wai_sem(TMO_POL)`）。ra_gen の `g_i2c_event_group`/`g_irq_binary_semaphore`（方式A で未生成）
     参照は全廃。`vTaskDelay` → `tk_dly_tsk`。
- **タスク化**: `src/lvgl_thread_entry.c` を `lvgl_task(INT, void*)` 化（旧 `lvgl_thread_entry` は
  ラッパ残置 ― ntshell/camera と同一パターン）。ループは `lv_timer_handler()` 戻り値ベースの
  `tk_dly_tsk`（0→1ms、>500→500ms にクランプ）。`src/usermain.c` に `T_CTSK ctsk_lvgl`
  （itskpri=14 / stksz=8192）を camera の後に追加生成・起動。
- **連動変更**: `src/User_FreeRTOSConfig.h` の trace フック削除（必須 ― 未定義シンボル化回避）。
  `src/camera_display.c`（R-005 繰り越し）: `xTaskGetTickCount`→`tk_get_otm`(ms)、AI 連携は
  `g_ai_app_event`（FreeRTOS）→ **`g_ai_app_flgid`（uT-Kernel フラグ ID、camera_display.c で定義・
  R-007 の ai_inference 側が生成して設定。0 のままなら従来の NULL ガードと同様にスキップ）**。
  `xEventGroupClearBits(grp,bit)` → `tk_clr_flg(id, ~bit)`（`tk_clr_flg` は AND クリア）。
  `src/port/glcdc_port.c`: `RM_LVGL_PORT_Open`→`lvgl_port_mtk3_open`、`p_lv_display`→
  `lvgl_port_mtk3_get_display()`、`display dbuf` の `vTaskDelay(1000)`→`tk_dly_tsk(1000)`。
  `src/port/camera_test.c` / `src/port/sdram_port.c`（3.3 で R-006 割当の残件）:
  `vTaskDelay`/`xTaskGetTickCount`→`tk_dly_tsk`/`tk_get_otm`(ms)。
  `src/ui/fall_detection_screen.c` / `src/port/dave2d_port.c`: 未使用 FreeRTOS include 削除。
  `src/port/dave2d_cache_management.c` の `#if (BSP_CFG_RTOS==2)` ガード付き include は
  API 呼び出しゼロのため無変更。`src/usrcmd.c` は無変更（lv_lock 契約維持）。
- **カーネル設定**: `mtk3_bsp2/config/config.h` の `CNF_MAX_MTXID 4→16`。
  `CNF_TIMER_PERIOD=10` は据え置き（実効 ~50fps 見込み。実測後に 1ms 化を判断）。
- **割り込み優先度の確認**: DRW_INT_IPL=2 / GLCDC・ICU19・IIC1=12。BSP2 ARMv8-M は
  カーネルクリティカルセクションを BASEPRI=INTPRI_MAX_EXTINT_PRI(1) でマスクするため、
  優先度 2〜12 の ISR から通知系 API（`tk_sig_sem`/`tk_set_flg`）を呼んでよい。
- **実機確認結果（2026-06-12 ユーザー実施・全項目 OK）**:
  ①起動ログ: blink/ntshell/camera/lvgl 全タスク起動（`lvgl_task created & started.`）。
  ②LCD メイン画面表示 OK（`display status`: Vsync Count 増加・58.58Hz 出力）。
  ③タッチ OK（`touch status`: Read count 増加・座標取得）。
  ④カメラ映像表示 OK（キャプチャ 65fps・エラー 0）。
  ⑤NT-Shell `lvgl`/`display`/`touch`/`camera` コマンド OK（lv_lock 経由の別タスクアクセス）。
  ⑥**FPS 実測: PERF_MONITOR 表示 30fps / CPU 40%（変動あり）→ KPI 30fps 充足**。
  `CNF_TIMER_PERIOD=10` 据え置きで確定（フル幅カメラ領域 768x450 の再描画負荷と 10ms 量子化の
  組み合わせとして妥当。さらに引き上げたい場合のみ `CNF_TIMER_PERIOD=1` を試行）。
- **既知の仕様（不具合ではない）**: `lvgl testpat` はカメラライブ表示開始後は**無効**。
  カメラ初回フレームで `camera_display_update()` がウィジェットのソースをゼロコピー記述子
  （VIN フレーム直接参照）へ差し替えるため（`camera_display.c:251-258`）、以降テストパターンを
  描く静的バッファ `camera_buf` はウィジェットから参照されない。テストパターンは画面作成直後
  〜カメラ初回フレームまでの間のみ表示される（FreeRTOS 時代から同一のロジック）。

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

実装メモ（R-007 / 2026-06-12 実装。実機確認はユーザー実施）:

- **タスク化**: `src/ai_inference_thread_entry.c` のスレッド本体を uT-Kernel タスク
  `ai_inference_task(INT, void*)` へ移植。`src/usermain.c` に `T_CTSK ctsk_ai_inference`
  （**itskpri=15 / stksz=16384**＝FreeRTOS 版 `ra_gen/ai_inference_thread.c` の
  `ai_inference_thread_stack[0x4000]` と同値）を追加し、lvgl の後に `tk_cre_tsk`+`tk_sta_tsk`
  で生成・起動（移行前の `ra_gen/main.c` でも ai_inference_thread_create() は最後）。
  旧 `ai_inference_thread_entry(void*)` は `ra_gen/ai_inference_thread.c`（編集禁止）の
  参照鎖が `main()` 経由でリンクに残るため**削除せず** `ai_inference_task` へ委譲する
  薄いラッパとして残置（ntshell/camera/lvgl と同一パターン）。
  - 優先度の確定値: blink=10 / camera=11 / ntshell=12 / dave2d・swdraw=13 / lvgl=14 /
    **ai_inference=15**。FreeRTOS では ai(2)=lvgl(2) と同列だったが、AI は 1 サイクル
    ごとに 25ms 譲る設計のため、描画パイプライン（13/14）を妨げない最下位とした。
  - タスク先頭で `jlink_configured()` 待ち（`tk_dly_tsk(100)` ポーリング、camera_task と
    同一パターン）。`print_to_console` 内部の `jlink_console_init()` ループによる
    SCI8 二重オープン競合を回避するため。
- **同期オブジェクト（カメラ→前処理→推論パイプライン）**: FreeRTOS イベントグループ
  `g_ai_app_event`（`xEventGroupCreateStatic` で本ファイル生成）→ uT-Kernel イベントフラグ
  **`g_ai_app_flgid`**。R-006 で camera_display.c に暫定定義していた ID 変数を、本来の
  所有者である `ai_inference_thread_entry.c` の定義へ移動（extern 宣言は
  `ai_inference_thread_api.h` ― FreeRTOS include も同ヘッダから除去）。生成は
  `ai_inference_task` 先頭で `tk_cre_flg`（`TA_TFIFO|TA_WMUL`、ov5640.c と同一属性・冪等）。
  ビット割り当て（common_util.h）は不変。ID が 0 の間（AI タスク起動前）は参照側が
  `>0` ガードでスキップ（旧 `g_ai_app_event != NULL` ガード相当）。
  - 推論要求の待ち: `xEventGroupWaitBits(IMAGE_READY, pdTRUE, pdTRUE, portMAX_DELAY)` →
    `tk_wai_flg(g_ai_app_flgid, AI_INFERENCE_INPUT_IMAGE_READY, TWF_ANDW|TWF_BITCLR,
    &flgptn, TMO_FEVR)`。**`TWF_BITCLR`（待ちビットのみクリア）が必須** ― `TWF_CLR` は
    全ビットクリアのため `ETHOSU_INIT_DONE`/`AI_INFERENCE_INIT_DONE` まで消え、
    camera_display.c の init 確認（`tk_ref_flg` ポーリング、camera_display.c:399-409）と
    `ai status` 表示が壊れる（I2C の単発完了待ちと違い、本フラグは状態ビット同居型）。
  - 通知: `xEventGroupSetBits` → `tk_set_flg`（ETHOSU_INIT_DONE / AI_INFERENCE_INIT_DONE /
    RESULT_UPDATED の 3 箇所 + スタブ側 1 箇所。全て ER 戻り値チェック付き）。
  - パイプライン全体（R-006 で受け側は準備済み・R-007 のフラグ生成で有効化）:
    camera_display.c の LVGL タイマ cb が前処理（RGB565→INT8）+
    `tk_set_flg(IMAGE_READY)`（camera_display.c:411-422）→ ai_inference_task が
    `tk_wai_flg` で受けて memcpy → `mera_invoke()` → 後処理 → `fall_detection_update()` →
    `tk_set_flg(RESULT_UPDATED)` → camera_display.c が `tk_ref_flg`+`tk_clr_flg(~bit)` で
    受けてオーバーレイ更新（camera_display.c:439-451）。
  - `vTaskDelay` → `tk_dly_tsk`: カメラ初期化ポーリング 100ms / エラー停止ループ 1000ms /
    サイクル末尾の `AI_THREAD_YIELD`（25 tick @1000Hz = 25ms で値は不変）。
- **Ethos-U55 ドライバと RTOS の連携箇所（調査結果: 置換不要）**: Ethos-U コアドライバの
  RTOS フック `ethosu_mutex_create/lock/unlock`・`ethosu_semaphore_create/take/give` は
  weak のベアメタル実装（`__WFE`/`__SEV`、`ra/npu/ethos-u-core-driver/src/ethosu_driver.c:157-232`）
  のままで、**本プロジェクトに FreeRTOS 上書き実装は元々存在しない**（grep 実測:
  当該シンボルの定義・参照は ethosu_driver.c/h のみ）。よって FreeRTOS 時と挙動同一で
  uT-Kernel 下でも無変更で動作する。NPU 完了割り込み `rm_ethosu_isr`（ipl=12、
  callback=NULL ― ra_gen/common_data.c:13）→ `ethosu_irq_handler` →
  `ethosu_semaphore_give`＝count++ と `__SEV` のみ（ethosu_driver.c:383-395, 226-232）で
  **カーネル API を一切呼ばない**ため 5.1 の ISR 制約に非該当。`FSP_CONTEXT_SAVE/RESTORE`
  （rm_ethosu.c:251/266）は ThreadX 以外では空マクロ（bsp_common.h:60-65）。
  推論中の `ethosu_semaphore_take` の `__WFE` ループ（同:205-223）は「実行状態のまま
  CPU を眠らせる」busy-wait だが、ai タスクは最下位優先度（15）のため、割り込みで
  起きた高優先度タスクに即プリエンプトされ他タスクの飢餓は起きない。
- **`ai_cmd.c`（`ai status`）**: `xEventGroupGetBits(g_ai_app_event)` →
  `tk_ref_flg(g_ai_app_flgid, &rflg)` の `rflg.flgptn`（参照系・`>0` ガード）。
  表示ラベルを `Event group` → `Event flag` へ変更。FreeRTOS 依存はこの 1 箇所のみ
  （要精査だった本ファイルの精査完了）。
- **`fall_detection_logic.c` / `fall_detection_cmd.c`（連携確認結果: 無変更）**:
  両ファイルとも RTOS API 非依存（grep 実測ゼロ。転倒判定はフレームカウントベースの
  ステートマシンで時刻 API も不使用）。`fall_detection_update()` は ai_inference_task の
  推論ループ内から従来どおり呼ばれる（呼び出し位置不変）。
- **カーネル資源**: イベントフラグは 3 個目（ov5640 I2C / touch I2C / AI）で
  `CNF_MAX_FLGID=16` 内、タスクは 8 個目（init/blink/ntshell/camera/lvgl/dave2d/swdraw/ai）で
  `CNF_MAX_TSKID=32` 内。`mtk3_bsp2/config/config.h` の変更は不要。
- **実機確認結果（2026-06-12 ユーザー実施・合格）**:
  - ①起動ログ: `ai_inference_task created & started.` と AI 初期化シーケンス
    （`Camera ready. Initializing Ethos-U55 NPU...` → `NPU initialized. Arena
    sub0=442368 bytes (YOLO-Fastest V1)` → `AI inference initialization complete.`）を確認。
  - ②`ai status`: Thread state=IDLE、Inference count 増加（2681→2998→…）、
    ETHOSU_INIT(bit2)/AI_INIT(bit3) SET、IMAGE_READY/RESULT_UPD がパイプライン
    進行に応じて遷移することを確認。
  - ③`ai time`: **NPU inference (mera_invoke) = 5 ms ― KPI（5ms 以内）達成**。
    Preprocessing 6ms / Input memcpy 3ms / Total cycle 10〜18ms / End-to-end 16〜24ms。
  - ④`ai detect`: 人物検出 OK（score=0.86〜0.88, class=0）、LCD バウンディング
    ボックス表示 OK。
  - ⑤`fall status`: ステートマシンが NORMAL → SUSPECTED → COOLDOWN → NORMAL と
    遷移、転倒姿勢で Confirmed total 増加（aspect ratio 1.36 / score 0.87 /
    Position hint LOWER）を確認。
  - ⑥カメラ表示は AI 並走時 **20 FPS（CPU 60%）**。R-006 単体時の実効 ~50fps から
    低下（前処理 6ms + memcpy 3ms が LVGL タイマ cb 内で実行されるため）。
    KPI 30fps との照合・チューニングは R-008（統合 KPI 検証）で扱う。
  - 回帰確認: LED 制御・NT-Shell コマンド・タッチ（read count 増加・座標取得）OK。
  - 既知の軽微事象: 起動ログで usermain の `ai_inference_task created & started.`
    直後に AI タスク初回ログとの混線による文字化け（`]H�`）が 1 箇所出る
    （usermain=T-Monitor と AI タスク=jlink_console の SCI8 共有による表示上の
    競合。機能影響なし。R-005 の SCI8 一本化と同種の事象）。

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
| 2026-06-10 | R-002 | BSP2 組み込み確定: タグ **v1.00.04**（mtkernel サブモジュール `435096c`）を `e2studio_CPU0/mtk3_bsp2/` へ vendoring 配置（入れ子 `.git` 削除・submodule 化せず）。`.gitignore` に BSP2 ビルド生成物除外を追加。2 章「BSP2 版数」を確定値で更新。4 章を全面改訂: 配置先・Exclude 解除（4.2）・include path 4 パス確定（4.3、`mtkernel/kernel/knlinc` を含む）・ターゲットマクロ確定 `_RAFSP_EK_RA8P1_`（4.4、1 つで RAFSP/EK_RA8P1/ARMV8M を自動有効化）・リンカ `mtkernel.ld` を `fsp.lld` の後ろに連結する LLVM 整合方針（4.5）・GNU→LLVM 読み替え表（4.6）・完了条件（4.7）を追記 |
| 2026-06-11 | R-002 | **実機 LLVM ビルド成功を確認**（FreeRTOS のまま・コンパイル＋リンク成功）。リンカは **Script files (-T) 欄末尾に `mtkernel.ld` を追加**する方式で通ることを確定（4.5）。`fsp.lld` への INCLUDE 追記は未採用の代替として記載。4.7 完了条件を全項目達成（チェック済み）に更新 |
| 2026-06-11 | R-002 | レビュー反映: ① BSP2 ビルド設定が Debug 構成のみだった不整合を解消し、**Release 構成にも include path / マクロ / リンカ / sourceEntry を同一適用して対称化**（4.3 に全 Configuration 適用の注意を追記）。② 非ターゲットソース（RX/RZ/STM32/他ベンダ BSP 層）を Debug/Release 両構成の sourceEntries で除外（4.2.1 新設。ガード済みで空コンパイルされる木のみのためビルド結果は不変、ビルド時間のみ短縮）。③ vendoring 後に残っていた `mtk3_bsp2/.gitmodules`（submodule 宣言の残骸）を削除 |
| 2026-06-11 | R-002 | Release ビルド検証で **`bsp_linker_info.h' file not found`（CPU1 Release）を確認**。調査の結果、**Release 構成は FSP 生成ファイル未生成のため CPU0/CPU1 とも従来からビルド不可**（μT-Kernel/BSP2 とは無関係の既存のプロジェクトセットアップ事項）と判明。4.3 に「既知の制約（Release 構成の土台未整備）」を追記。R-002 の動作検証は Debug 構成で実施する方針を明記。Release 向け BSP2 設定は対称化のため反映済みだが、土台整備までビルド検証は保留 |
| 2026-06-11 | R-003 | **実機ビルドでコードフラッシュ・オーバーフロー（約 5.6KB 超過）を確認・対処**。`knl_start_mtkernel()` 実呼び出しで μT-Kernel 本体がリンクに取り込まれる一方、方式 A でも FreeRTOS/LVGL/カメラ/AI が `ra_gen/main.c` 経由で同居リンクされ（移行期のフラッシュピーク）、CPU0 の 960KB 区画を超過。**根本原因は区画の偏り**: CPU1 の実イメージは約 11KB（`llvm-size` 実測 text 11,206/bss 3,696）に過ぎず 64KB 区画の約 53KB が遊休だった。**対処は 2 段構え**: ①`config_func.h` の機能スイッチで移行が一度も使わない IPC・ハンドラ系（mbx/mbf/por/mpl/mpf/device）のみ `gc-sections` 除去（5,598→約1,662 byte 超過まで縮小、実効約 3.9KB。`gc-sections` は関数単位のため未使用関数は元々除去済みで頭打ち）。sem/flg/mtx/cyc/alm は R-004 以降で使うため**有効のまま保持**（段階的再有効化の手間を排除）。②遊休 CPU1 区画から CPU0 へフラッシュを再配分（**CPU0 960→約992KB / CPU1 64→約32KB**、`solution.xml` Memories タブでユーザー手動 + Generate Project Content）。`tkinit.c` の init 呼び出し・各サブシステム本体は同名 `USE_*` ガードで保護、RA FSP の `knl_init_device()` は無条件 `return E_OK` のため安全。7.1 末尾に詳細（区画変更手順含む）・6.2 再適用チェックリストに 2 項目追加 |
| 2026-06-12 | R-003 | **実機動作確認 完了（受け入れ条件 全達成）**。EK-RA8P1 で μT-Kernel 3.0 起動・`usermain()` 到達（バナー `microT-Kernel Version 3.00` / `LED count = 3` / `secondary core (CPU1) started.` / `blink_task created & started.`）、**LED 3 個が約1秒周期で点滅**、`tm_printf` シリアル出力（115200/8N1）を確認。FreeRTOS 設定（configuration.xml）は維持したまま方式A で実現。R-003 クローズ |
| 2026-06-12 | R-003 | **PR #163 codex レビュー反映（P2）: カーネル起動を POST_C 末尾から静的コンストラクタへ移設**。当初 `R_BSP_WarmStart(BSP_WARM_START_POST_C)` 末尾で `knl_start_mtkernel()`（戻らない）を呼んでいたが、その直後に `SystemInit()` が実行する **`SystemRuntimeInit(1)`（外部 SDRAM `.sdram` ゼロクリア / `.sdram_from_flash`・`.sdram_ospi_data` コピー）・TLS 初期化等をスキップ**してしまう問題。本リポジトリは `BSP_CFG_C_RUNTIME_INIT=1`/`BSP_CFG_SDRAM_ENABLED=1` で `.sdram` 配置バッファ（`ai_inference` の `model_buffer_int8`）が存在し、将来 SDRAM 利用タスク移行時に未初期化となる潜在バグ（R-003 最小構成は `.sdram`/コンストラクタ未使用のため実機は非顕在）。**対処（初回 `bsp_init()` 上書き案は失敗→コンストラクタへ）**: 当初は `SystemInit` 最終段の FSP weak フック `bsp_init()` を `src/hal_warmstart.c` で上書きする案だったが、**本ボードは `ra/board/ra8p1_ek/board_init.c` が `bsp_init()` を強いシンボルで定義済み**（`ra/` 編集禁止）のため `ld.lld: duplicate symbol: bsp_init` でビルド失敗。上書き不要な **`__attribute__((constructor)) static void mimamori_start_mtkernel(void)`** から `knl_start_mtkernel()` を呼ぶ方式に変更。`__init_array` は `SystemRuntimeInit(1)`（system.c:476）の後（同:512-516）に実行されるため C ランタイム初期化完了後にカーネルへ入る。POST_C はピン設定（`R_IOPORT_Open`）と SDRAM コントローラ起動（`R_BSP_SdramInit`）のみに変更し、「SDRAM 起動(POST_C)→`.sdram` 初期化(`SystemRuntimeInit`)→カーネル(コンストラクタ)」の順序を保つ。コンストラクタは `fsp_gen.lld` の `KEEP(*(.init_array))` で `gc-sections` から保護。7.1 実装メモを更新。**実機再検証はユーザー実施待ち** |
| 2026-06-12 | R-003 | **初期タスク生成失敗（`!ERROR! Initial Task can not creat`）を修正 ― RAM 不具合 2 件**。`sys_start.o` 逆アセンブルで `knl_lowmem_limit=0x220E0000`（実 RAM 使用末尾 `__mtk3_SYSMEM_START≒0x2211e100` より低位 → プール空/負 → `knl_Imalloc` NULL → `tk_cre_tsk` E_NOMEM）と判明。**原因①（主因・ベンダ不具合）**: `include/sys/sysdepend/ra_fsp/ek_ra8p1/sysdef.h` が誤って `cpu/ra8m1/sysdef.h`（`INTERNAL_RAM_SIZE=0xE0000`=896KB）を include していた（mtk3_bsp2 v1.00.04、ek_ra8m1 からのコピー取り違え）。`cpu/ra8p1/sysdef.h`（正値 `0x1D4000`=1872KB）へ修正。**原因②（①修正後に顕在化）**: 正しい `INTERNAL_RAM_END=0x221D4000` は SRAM 全域＝単一コア前提だが、本機マルチコアで CPU0 RAM 区画は `0x221B0000` まで。既定 `CNF_SYSTEMAREA_END=0` だとプールが CPU1 区画へはみ出し `knl_init_Imalloc` が高位境界を CPU0 非所有領域へ書いて破壊。`mtk3_bsp2/config/config.h` の `CNF_SYSTEMAREA_END=0x221B0000` で cap。両修正でプール `[≒0x2211e100,0x221B0000]`≒585KB が CPU0 RAM 内に収まる。7.1 に詳細・切り分け手順、6.2 にチェック 2 項目追加（再 vendoring 時 再適用要） |
| 2026-06-12 | R-003 | **マルチコア・デバッグ起動失敗を修正**。`Debug_Multicore Launch Group` の CPU1 接続が `'monitor enable_stopped_notify_on_connect' is timed out` で失敗（区画変更とは無関係）。原因は方式A が `main()`/スケジューラ未到達で、元 FreeRTOS の `blinky_thread_entry.c` 先頭にあった `R_BSP_SecondaryCoreStart()` が実行されず CPU1 がリセット保持のままになり、CPU1 デバッガ接続がタイムアウトしていたこと。`src/usermain.c` の起動ログ直後に元と同一ガードで `R_BSP_SecondaryCoreStart()` を移設して CPU1 を解除（元挙動の復元）。7.1 実装メモに記録。代替は CPU0 単体デバッグ構成 |
| 2026-06-12 | R-003 | **区画再配分で LLVM ビルド成功を確認**（Debug 構成・コンパイル＋リンク成功）。`solution.xml` Memories タブで `FLASH_CPU0_CPU0_S` を `0xF0000`（960KB）→ `0xF8000`（992KB）、CPU1 フラッシュを `0x10000`（64KB）→ `0x8000`（32KB）へ変更（合計 `0x100000`=1MB 維持）し Generate Project Content。これで R-003 のコードフラッシュ・オーバーフローが解消。実機 LED 点滅・シリアル出力確認はユーザー実施待ち |
| 2026-06-12 | R-004 | **NT-Shell 関連を μT-Kernel 3.0 へ移行**。`ntshell_thread_entry.c` のスレッド本体を uT-Kernel タスク `ntshell_task(INT, void*)` へ移植し、`usermain.c` に `T_CTSK ctsk_ntshell`（`itskpri=12`/`stksz=4096`）を追加して `tk_cre_tsk`+`tk_sta_tsk` で起動。旧 `ntshell_thread_entry(void*)` は `ra_gen/ntshell_thread.c`（編集禁止）からの参照鎖が `main()` 経由でリンクに残るため**削除せず**本体を `ntshell_task` へ委譲する薄いラッパとして残置。**SCI8 競合を「起動バナーまで T-Monitor → NT-Shell が SCI8 を FSP UART で開いた後は jlink_console 専有」で一本化**。**方式A で未実行となる FSP `bsp_irq_cfg()`（ELC→NVIC の IELSR 設定）を `usermain()` 先頭で呼び SCI8 割り込み（TXI/RXI/TEI/ERI）を NVIC へ結線**（未実施だと `jlink_console_write` がハング）。`g_hal_init()` は FreeRTOS オブジェクト生成のみで割り込み構成に無関係のため呼ばない。`jlink_console.c`: `vTaskDelay(1)`×4→`tk_dly_tsk(1)`、`taskENTER/EXIT_CRITICAL`×3→**割り込みマスク `DI`/`EI`**（ISR と共有する `s_out_of_band_received[]` 保護のため `tk_dis_dsp` 不可）、`<assert.h>` 明示追加。`usrcmd.c`: `vTaskDelay(pdMS_TO_TICKS(100))`→`tk_dly_tsk(100)`、`tskKERNEL_VERSION_NUMBER`→`"uT-Kernel 3.0"`（reset/version 表示のみ。lvgl/camera/ai サブコマンドは R-005 以降）。`led_ctrl.c`: FreeRTOS ソフトウェアタイマ→uT-Kernel 周期ハンドラ（`tk_cre_cyc`/`tk_sta_cyc`/`tk_stp_cyc`/`tk_del_cyc`、LED ごと 1 ハンドラ・`exinf` で index 伝達・`TA_HLNG\|TA_STA\|TA_PHS`、`tk_set_cyc` 不在のため interval 変更は削除→再生成）。`config_func.h` は R-003 で必要機能（cyc/sem/mtx/flg）保持済みで変更不要。7.2 実装メモ・7.1 末尾「R-004 での注意」を確定内容で更新。**実機確認（ntshell 起動・mr/md/mw/led 各コマンド・LED blink）はユーザー実施待ち** |
| 2026-06-12 | R-005 | **カメラ関連を μT-Kernel 3.0 へ移行**。`camera_thread_entry.c` のスレッド本体を uT-Kernel タスク `camera_task(INT, void*)` へ移植し、`usermain.c` に `T_CTSK ctsk_camera`（`itskpri=11`（blink=10 と ntshell=12 の中間）/`stksz=4096`）を追加して ntshell 起動の後に `tk_cre_tsk`+`tk_sta_tsk` で起動。旧 `camera_thread_entry(void*)` は `ra_gen/camera_thread.c`（編集禁止）の参照鎖が `main()` 経由でリンクに残るため**削除せず** `camera_task` へ委譲する薄いラッパとして残置。**最重要 ― I2C 完了割り込み→タスク同期の実体 `g_i2c_event_group`（FreeRTOS イベントグループ）が方式A で `g_hal_init()` 未実行のため未生成（NULL）**。よって `ov5640.c` に uT-Kernel イベントフラグ `s_i2c_flgid` を新設: 公開 `ov5640_i2c_sync_init()`（`tk_cre_flg`, `TA_TFIFO\|TA_WMUL`, 冪等）を `camera_task` 先頭で生成、`i2c_camera_callback`（ISR）は `xEventGroupSetBitsFromISR`/`portYIELD_FROM_ISR` 全廃→`tk_set_flg`（通知系・ISR 可、明示 yield 不要）、`ov5640_i2c_wait_complete`（static 除去・公開化）は `xEventGroupWaitBits`→`tk_wai_flg`（`TWF_ORW\|TWF_BITCLR`, ms タイムアウト）+ 戻り値→`FSP_SUCCESS`/`FSP_ERR_ABORTED`/`FSP_ERR_TIMEOUT`。`camera_thread_entry.c` のインライン I2C 待ち（board switch verify/GreenPAK/board switch init）を公開関数 `ov5640_i2c_wait_complete()` へ統一し `g_i2c_event_group`・`I2C_XFER_*` マクロを除去。`vTaskDelay`/`pdMS_TO_TICKS`→`tk_dly_tsk(ms)`。`camera_framebuffer.c`: `xTaskGetTickCountFromISR()`/`xTaskGetTickCount()`→`tk_get_otm(SYSTIM*)`（ISR 可・参照系、`.lo` を ms として使用）、`FPS_INTERVAL_TICKS`(configTICK_RATE_HZ)→`FPS_INTERVAL_MS`(1000)、FPS 式は `frames*1000/elapsed_ms`。**`camera_display.c` は LVGL タイマ駆動・AI 連携（`g_ai_app_event`）のため R-005 では実行されず、R-006（LVGL）へ繰り越し**（FreeRTOS ヘッダ維持で現状コンパイル可）。`vin_port.c` は RTOS 非依存（`__disable_irq`/`__enable_irq`）で変更不要。7.3 実装メモを確定内容で更新。**実機確認（camera タスク起動・`camera status` で Frame Count/FPS 増加）はユーザー実施待ち** |
| 2026-06-12 | R-005 | **実機動作確認でフレームキャプチャ成功を確認**（`camera status` で Frame Complete が増加・FPS≒65・HW State=IN_PROGRESS・各種エラー 0、LED コマンド動作）。**SCI8 一本化の取りこぼしを修正**: `usermain.c` のカメラ生成ログのみ `print_to_console`（jlink_console）を使っていたため、NT-Shell が `jlink_console_init()` で SCI8 を開く前に呼ばれて `jlink_configured()` 待ち→`tk_dly_tsk(1)` 譲り→NT-Shell バナーと SCI8 出力が競合し**文字化け（`m□`）＋カメラ生成ログ消失**が発生。blink/ntshell と同じ **T-Monitor（`tm_putstring`/`tm_printf`）** へ統一し、不要化した `jlink_console.h` include を除去。7.3 実装メモを修正。**修正後の実機再確認はユーザー実施待ち** |
| 2026-06-11 | R-003 | **ブート・OS 起動を μT-Kernel 3.0 へ移行（最小構成）**。採用方式を**方式A**に確定（`src/hal_warmstart.c` の `R_BSP_WarmStart(POST_C)` 末尾で `knl_start_mtkernel()` を呼び、FreeRTOS `main()`/`vTaskStartScheduler()` に到達させない。切替マクロ `MIMAMORI_USE_MTKERNEL_BOOT` で切り戻し可）。`src/usermain.c` を新規作成し、BSP2 の WEAK `usermain()` を強い定義で上書き ― LED 点滅タスク（`tk_cre_tsk`+`tk_sta_tsk`、`vTaskDelay`→`tk_dly_tsk(500)`）生成と `tm_printf` 起動ログ、自身は `tk_slp_tsk(TMO_FEVR)`。`ra_gen/` 無編集で既存 FreeRTOS スレッド（blinky/ntshell/camera/lvgl/ai_inference）を `main()` 未到達により無効化。`g_hal_init()` は最小構成で不要（LED=BSP 直接 / `tm_printf`=SCI8 直接）。7.1 実装メモに方式 A 選定理由・`usermain` 配置・HAL 一度きり保証の移設方針・`USE_OBJECT_NAME=0` による `T_CTSK.dsname` 不在の注意・**SCI8 競合確認結果**（`tm_com.c` と `jlink_console.c`(`channel=8`) が同一 SCI8 を共有。R-003 は ntshell 未起動で非競合、R-004 で一本化方針が必要）を記録。実機 LED 点滅・シリアル出力確認はユーザー実施 |
| 2026-06-12 | R-006 | **LCD 画面（LVGL）を μT-Kernel 3.0 へ移行**（R-006a 案A の実装）。新規: `src/lv_os_mtkernel.{h,c}`（LVGL カスタム OSAL ― tk_cre_tsk/tk_cre_mtx(TA_INHERIT)+再帰ラッパ/カウンティングセマフォ。優先度マップ HIGH=13）、`src/port/lvgl_port_mtk3.{c,h}`（RM_LVGL_PORT バイパス ― display cfg コピー+callback 差し替えで R_GLCDC_Open、Vsync=tk_sem(maxsem=1)、tick=tk_get_otm。rm_lvgl_port.c:82-171 と 1:1）、`src/port/r_drw_irq_mtk3.c`（`r_drw_irq.c` ビルド除外（**ユーザー手動・全 Configuration**）+ 全 4 シンボル `d1_initirq_intern`/`d1_shutdownirq_intern`/`d1_queryirq`/`drw_int_isr` を uT-Kernel セマフォで再実装）。変更: `lv_conf_user.h`（`LV_USE_OS LV_OS_CUSTOM` 切替・`LV_SYSMON_GET_IDLE` override 削除）、`User_FreeRTOSConfig.h`（trace フック削除 ― lv_freertos.c 空化による未定義シンボル回避）、`lv_port_indev.c`（rm_comms_i2c コールバックモード化＝bus cfg の semaphore/mutex 実行時 NULL 化 + uT-Kernel フラグ/セマフォ + tk_dly_tsk）、`lvgl_thread_entry.c`（`lvgl_task(INT,void*)` 化・`lv_timer_handler` 戻り値駆動 `tk_dly_tsk`・旧エントリはラッパ残置）、`usermain.c`（`ctsk_lvgl` itskpri=14/stksz=8192 を camera 後に生成・起動）、`camera_display.c`（tick→tk_get_otm、AI 連携 `g_ai_app_event`→`g_ai_app_flgid`(R-007 で生成・未生成スキップ)）、`glcdc_port.c`（`lvgl_port_mtk3_open` へ差し替え・dbuf の vTaskDelay→tk_dly_tsk）、`camera_test.c`/`sdram_port.c`（tick/遅延の uT-Kernel 化）、`fall_detection_screen.c`/`dave2d_port.c`（未使用 include 削除）、`mtk3_bsp2/config/config.h`（**CNF_MAX_MTXID 4→16**。CNF_TIMER_PERIOD=10 据え置き）。6.2 に再適用 3 項目（r_drw_irq.c 除外 / FSP 更新時の原本差分反映 / CNF_MAX_MTXID）を追加、7.4 実装メモを確定内容で更新。**実機確認（LCD 表示・タッチ・カメラ表示・FPS）はユーザー実施待ち** |
| 2026-06-12 | R-007 | **転倒検出 AI 推論を μT-Kernel 3.0 へ移行**。`ai_inference_thread_entry.c` のスレッド本体を uT-Kernel タスク `ai_inference_task(INT, void*)` へ移植し、`usermain.c` に `T_CTSK ctsk_ai_inference`（**itskpri=15**（lvgl=14 より低い最下位）/`stksz=16384`＝ra_gen の 0x4000 と同値）を lvgl の後に追加して `tk_cre_tsk`+`tk_sta_tsk` で起動。旧エントリは `ra_gen/ai_inference_thread.c` のリンク解決用ラッパとして残置（ntshell/camera/lvgl と同一パターン）。**同期は FreeRTOS イベントグループ `g_ai_app_event`（xEventGroupCreateStatic/WaitBits/SetBits）→ uT-Kernel イベントフラグ `g_ai_app_flgid`（tk_cre_flg `TA_TFIFO\|TA_WMUL` / tk_wai_flg / tk_set_flg）へ置換**。ID 変数の定義は R-006 暫定の camera_display.c から ai_inference_thread_entry.c へ移動（extern は ai_inference_thread_api.h、同ヘッダの FreeRTOS include 除去）。推論要求待ちは `tk_wai_flg(IMAGE_READY, TWF_ANDW\|TWF_BITCLR, TMO_FEVR)` ― **TWF_BITCLR 必須**（TWF_CLR だと同居する ETHOSU_INIT_DONE/AI_INIT_DONE 状態ビットまで消える）。これにより R-006 で受け側準備済みのカメラ→前処理→推論→オーバーレイのイベント経路（camera_display.c の IMAGE_READY set / RESULT_UPDATED ref+clr）が有効化。`vTaskDelay`→`tk_dly_tsk`（100/1000/25ms）。**Ethos-U ドライバの RTOS フックは置換不要と判明**: `ethosu_mutex_*`/`ethosu_semaphore_*` は weak ベアメタル実装（__WFE/__SEV、ethosu_driver.c:157-232）のままで FreeRTOS 上書きが元々存在せず（grep 実測）、NPU IRQ（ipl=12）→`ethosu_semaphore_give`＝`__SEV` のみでカーネル API 不使用（ISR 制約に非該当）。`ai_cmd.c` の `ai status`: `xEventGroupGetBits`→`tk_ref_flg`。`fall_detection_logic.c`/`fall_detection_cmd.c` は RTOS 非依存（grep ゼロ）で無変更・`fall_detection_update()` の呼び出し位置も不変。カーネル資源は CNF_MAX_FLGID=16/CNF_MAX_TSKID=32 内で config.h 変更不要。7.5 実装メモ・3.3 表を確定内容で更新。**実機確認済み（2026-06-12 ユーザー実施・合格）**: 起動ログ・AI 初期化 OK、`ai status`（Inference count 増加・イベントビット遷移）、`ai time` で **NPU inference 5ms ― KPI 達成**、`ai detect` 人物検出（score 0.86-0.88）+ LCD オーバーレイ、`fall status` ステートマシン遷移（SUSPECTED/COOLDOWN/Confirmed 増加）、LED・タッチ回帰 OK。AI 並走時のカメラ表示は 20 FPS（CPU 60%）― 30fps KPI との照合は R-008 で実施。なお実装時にコメント内の `ethosu_mutex_*/ethosu_semaphore_*` 表記の `*/` がブロックコメント終端と解釈されるビルドエラーが発生し、`* / ` （空白挿入）へ修正した |
| 2026-06-12 | R-006a | **LVGL OSAL 対応方針を確定（スパイク完了）**。新規 `doc/migration/r006a-lvgl-osal-spike.md` に 3 案比較・決定根拠・R-006 実装方針（変更ファイル一覧・API 対応表・PoC コードスケッチ）を文書化。**結論: 案A「μT-Kernel 向け LVGL OSAL 自作（`LV_USE_OS=LV_OS_CUSTOM` + `src/lv_os_mtkernel.{h,c}`）」を採用**（FSP 生成 lv_conf.h の `#ifndef` ガードにより `src/lv_conf_user.h` のみで切替可能・`ra/lvgl/` 無編集・FSP 再生成耐性が完全・現行 60fps 実績構成（dave2d レンダスレッド + lv_lock 契約）を維持・usrcmd.c 無変更）。案B は μT-Kernel 向け CMSIS-RTOS2 実装が存在せず不採用、案C は lv_lock no-op 化で NT-Shell との排他再設計が必要かつ FSP 層依存の削減効果が無く不採用（`LV_USE_OS=LV_OS_NONE` へのコンパイル時切替によるフォールバックとして温存。実行時の自動退避は不成立 ― PR#166 Codex 指摘で訂正）。**追加発見: OSAL の外側で `ra/fsp/` 読み取り専用コード 3 件（`rm_lvgl_port.c`=Vsync 同期/tick、`r_drw_irq.c`=D/AVE 2D dlist 完了同期、`rm_comms_i2c_driver_ra.c`=タッチ I2C ブロッキング）が `BSP_CFG_RTOS==2` で FreeRTOS ブロッキング API を直接呼んでおり、LV_USE_OS の選択と無関係に方式A（FreeRTOS スケジューラ未起動）で破綻するため R-006 で必須対応**（rm_lvgl_port=バイパス自作ポート `src/port/lvgl_port_mtk3.c` / r_drw_irq=ビルド除外+同名シンボル置換 `src/port/r_drw_irq_mtk3.c` / rm_comms_i2c=ra_gen の非 const cfg を実行時 NULL 化してコールバックモード化）。連動対応も特定: `User_FreeRTOSConfig.h` trace フック削除（`lv_freertos.c` 空化による `lv_freertos_task_switch_in/out` 未定義シンボル化の回避）、`lv_os_get_idle_percent` 自作実装（`LV_SYSMON_GET_IDLE` のデフォルト解決先のため未定義だとリンクエラー）、`CNF_MAX_MTXID 4→16`（LVGL は general+builtin mem+xd2+キャッシュ群で mutex 6 個以上）、`CNF_TIMER_PERIOD=10` のままで実効 ~50fps（KPI 30fps 充足・60fps 狙いは 1ms へ変更を実機で実測判断）。`lv_mutex_lock_isr`/`lv_lock_isr` は μT-Kernel 非対応だが本プロジェクト未使用を確認。3.4 / 7.4 を確定内容で更新。コード変更なし（方針決定・文書化のみ） |
