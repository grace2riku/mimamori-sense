# R-006a: LVGL OSAL の FreeRTOS 依存への対応方針 技術検討（スパイク）

- 管轄 Issue: #159（R-006a）／ 結論の実装先: #156（R-006: LCD 画面（LVGL）の移行）
- 親 Epic: #150（R-000）／ 土台手順書: `doc/migration/mtk3-migration-guide.md`（R-001）
- 本書は**方針決定・PoC（コードスケッチ）・文書化**までが範囲。実装は R-006 で行う。

---

## 1. 目的・スコープ

LVGL の OSAL（OS 抽象層）実装が FreeRTOS に直接依存している
（`ra/lvgl/lvgl/src/osal/lv_freertos.c`）問題について、μT-Kernel 3.0 移行
（方式A: FreeRTOS 設定維持・`main()` 未到達・`usermain()` 起動）での対応方針を
3 案比較で確定する。

**重要な発見（本スパイクの主要成果）**: 調査の結果、R-006 で対応すべき FreeRTOS 依存は
「LVGL の OSAL」だけでなく、**OSAL の外側にある FSP 読み取り専用コード（`ra/fsp/`）の
FreeRTOS 直接依存が 3 箇所**あり、これらは **LV_USE_OS の選択（案A/B/C）に関わらず必ず
対処が必要**であることが判明した（→ 2.4）。この事実が案の優劣評価に大きく影響する
（案C「OS 非依存化」を選んでも FSP 層の問題は消えない）。

## 2. 調査結果

### 2.1 LVGL OSAL 抽象の仕組み（LV_USE_OS / lv_os.h）

- LVGL（v9.3.0+renesas）は `LV_USE_OS` で OSAL バックエンドを選択する。
  選択肢: `LV_OS_NONE / LV_OS_PTHREAD / LV_OS_FREERTOS / LV_OS_CMSIS_RTOS2 /
  LV_OS_RTTHREAD / LV_OS_WINDOWS / LV_OS_MQX / LV_OS_SDL2 / LV_OS_CUSTOM(=255)`
  （`ra/lvgl/lvgl/src/lv_conf_internal.h:14-20, 269-284`）。
- **`LV_OS_CUSTOM` のカスタム OSAL 機構は利用可能**（lv_os.h:40-41）:
  ```c
  #elif LV_USE_OS == LV_OS_CUSTOM
  #include LV_OS_CUSTOM_INCLUDE
  ```
  カスタムヘッダは **3 つの型**（`lv_thread_t` / `lv_mutex_t` / `lv_thread_sync_t`）を
  定義すればよく、**関数群はリンク時に解決**される（LVGL 本体側に弱定義はないため、
  自作 .c を 1 本リンクに加えるだけでよい）。
- 実装が必要な API（`lv_os.h:69-188`、`LV_USE_OS != LV_OS_NONE` のとき）:

  | 分類 | API |
  |------|-----|
  | スレッド | `lv_thread_init` / `lv_thread_delete` |
  | ミューテックス | `lv_mutex_init` / `lv_mutex_lock` / `lv_mutex_lock_isr` / `lv_mutex_unlock` / `lv_mutex_delete` |
  | 同期（条件変数風） | `lv_thread_sync_init` / `lv_thread_sync_wait` / `lv_thread_sync_signal` / `lv_thread_sync_signal_isr` / `lv_thread_sync_delete` |
  | グローバルロック | `lv_lock` / `lv_lock_isr` / `lv_unlock` — **lv_os.c に汎用実装あり**（内部で `lv_mutex_*` を呼ぶだけ。自作不要） |
  | 統計 | `lv_os_get_idle_percent`（`LV_USE_SYSMON` のデフォルト `LV_SYSMON_GET_IDLE`。→ 2.5） |

### 2.2 現状の設定と「ra/ 無編集」での切り替え経路

- 現在の設定: FSP 生成 `ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h:36-38` が
  ```c
  #ifndef LV_USE_OS
  #define LV_USE_OS (LV_OS_FREERTOS)
  #endif
  ```
  と **`#ifndef` ガード付き**で定義しており、同ファイルは先頭で
  `#include "lv_conf_user.h"`（src/ 配下・ユーザー管理）を取り込む。
- したがって **`src/lv_conf_user.h` に `#define LV_USE_OS LV_OS_CUSTOM` を書くだけで、
  `ra/lvgl/` `ra_cfg/` を一切編集せずに OSAL を差し替えられる**。
  `lv_freertos.c` は `#if LV_USE_OS == LV_OS_FREERTOS` ガードのため**空コンパイル**となり、
  ビルド除外操作も不要。**FSP 再生成耐性は完全**（lv_conf_user.h と自作 OSAL は src/ 配下）。
- `src/` はコンパイラ include path に含まれている（`.cproject` 確認済み）ため、
  `LV_OS_CUSTOM_INCLUDE "lv_os_mtkernel.h"`（src/ 直下）が解決できる。

### 2.3 OSAL 利用箇所の棚卸し

LVGL 内部（`ra/lvgl/` ― 編集しない。OSAL 差し替えでそのまま動く）:

| ファイル | 利用内容 |
|----------|----------|
| `src/osal/lv_os.c` | `lv_general_mutex`（`lv_lock`/`lv_unlock` の実体）。`lv_os_init()`→`lv_mutex_init` |
| `src/draw/lv_draw.c` | 描画ディスパッチの同期 `_draw_info.sync`（`lv_thread_sync_init/wait/signal`） |
| `src/draw/renesas/dave2d/lv_draw_dave2d.c` | **Dave2D レンダスレッド**（`lv_thread_init`, "dave2d", `LV_DRAW_THREAD_PRIO`=HIGH, stack `LV_DRAW_THREAD_STACK_SIZE`=0x2000）、HW 排他 `xd2Semaphore`（`lv_mutex_*`）、unit sync |
| `src/draw/sw/lv_draw_sw.c` | **SW レンダスレッド**（"swdraw" × `LV_DRAW_SW_DRAW_UNIT_CNT`=1） |
| `src/stdlib/builtin/lv_mem_core_builtin.c` | アロケータ排他 mutex（`LV_USE_OS` 時） |
| `src/misc/cache/lv_cache.c` | キャッシュごとの mutex（イメージ/ヘッダ/TinyTTF グリフ等、複数個） |
| `src/misc/lv_timer.c` | `lv_timer_handler()` 内の `lv_lock`/`lv_unlock` |

→ `LV_USE_OS != NONE` のとき LVGL が生成するスレッドは **2 本**（dave2d / swdraw、
各 8KB スタック・PRIO_HIGH）、mutex は **可変個**（general + mem + xd2 + キャッシュ群）。

ユーザーコード（`src/` ― R-006 で編集）:

| ファイル | 利用内容 |
|----------|----------|
| `src/usrcmd.c` | `lv_lock()` × 4 箇所・`lv_unlock()` × 6 箇所（エラーパス分岐含む。NT-Shell `lvgl` コマンド群。**別タスクから LVGL API を呼ぶ排他の要**） |
| `src/lvgl_thread_entry.c` | `lv_timer_handler()` ループ + `vTaskDelay(1)` |
| `src/camera_display.c` / `src/ui/*.h` | 「LVGL スレッドまたは lv_lock 区間から呼ぶこと」の契約をヘッダで明文化（lv_lock 契約に依存した設計） |
| `src/User_FreeRTOSConfig.h` | `traceTASK_SWITCHED_IN/OUT` → `lv_freertos_task_switch_in/out`（lv_freertos.c の関数）をフック（CPU 使用率統計用） |

### 2.4 OSAL の**外側**にある FreeRTOS 依存（FSP 読み取り専用層 ― 全案共通の必須対応）

`configuration.xml` の FreeRTOS 設定を維持する方針のため **`BSP_CFG_RTOS == 2`（FreeRTOS）で
FSP コードがコンパイルされ続ける**。以下 3 箇所は `#if (BSP_CFG_RTOS == 2)` で FreeRTOS API を
直接呼ぶため、**LV_USE_OS の選択と無関係に**方式A（FreeRTOS スケジューラ未起動）では破綻する。
FreeRTOS のブロッキング API（`xSemaphoreTake` 等）は待ちに入る際 `pxCurrentTCB`（FreeRTOS の
カレントタスク）を参照するため、スケジューラ未起動では**ハードフォルト/ハング**となる。

| ファイル（ra/fsp/ ― 編集禁止） | FreeRTOS 依存の内容 | R-006 での対処（→ 5 章） |
|------|------|------|
| `ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c` | Vsync 同期: GLCDC LINE_DETECTION ISR で `xSemaphoreGiveFromISR(g_semaphore_vpos)` + `portYIELD_FROM_ISR`、flush 待ちで `xSemaphoreTake(portMAX_DELAY)`。LVGL tick = `xTaskGetTickCount()` | **バイパス**（呼ばない）。`RM_LVGL_PORT_Open()` の代わりに src/ に μT-Kernel 版表示ポートを自作（5.4）。display cfg を**コピーしてコールバックだけ差し替え**て `R_GLCDC_Open` するため ra_gen 無編集・ビルド除外も不要 |
| `ra/fsp/src/r_drw/r_drw_irq.c` | D/AVE 2D の dlist 完了同期: `d1_queryirq()` が `xSemaphoreTake`、`drw_int_isr()` が `xSemaphoreGiveFromISR`。**d2 ライブラリ内部から呼ばれるためユーザーコードでバイパス不可** | **ビルド除外 + 同一シンボルの μT-Kernel 版を src/ に実装**（5.5）。除外設定は `.cproject`（sourceEntries）に保存され Generate Project Content で消えない（R-002 の mtk3_bsp2 非ターゲット除外と同機構） |
| `ra/fsp/src/rm_comms_i2c/rm_comms_i2c_driver_ra.c` | タッチ I2C: バス排他 `xSemaphoreTakeRecursive` / 完了ブロッキング `xSemaphoreTake` | **コールバックモード化**。`g_comms_i2c_bus0_extended_cfg`（ra_gen だが **非 const**）の `p_blocking_semaphore` / `p_bus_recursive_mutex` を open 前に実行時 NULL 化（`RM_COMMS_I2C_Open` は両方 NULL を正規サポート、rm_comms_i2c.c:99-103）。完了待ちはユーザー側で μT-Kernel イベントフラグ（R-005 の ov5640 と同一パターン）（5.6） |

補足:
- `_rm_lvgl_port_display_callback` は `ra_gen/common_data.c` の `g_display0_cfg.p_callback` に
  登録済みだが、自作ポートが **cfg のコピー**（callback 差し替え済み）で `R_GLCDC_Open` する
  ため**実行されない**（リンクには残るが死にコード）。
- カメラ側 I2C（r_iic_master 直接 + コールバック）は R-005 で対処済み。GLCDC/GPT/VIN 等の
  他 FSP ドライバはコールバック駆動で RTOS 非依存（確認済み）。

### 2.5 連動して対処が必要な事項

| 項目 | 内容 | 対処 |
|------|------|------|
| `User_FreeRTOSConfig.h` の trace フック | `LV_USE_OS != LV_OS_FREERTOS` にすると `lv_freertos.c` が空になり `lv_freertos_task_switch_in/out` が**未定義シンボル化**（FreeRTOS の tasks.c は `main()` 参照鎖でリンクに残るため）| `src/User_FreeRTOSConfig.h`（ユーザー管理）から trace マクロを削除 |
| `LV_SYSMON_GET_IDLE` | lv_conf_user.h:476 で `lv_os_get_idle_percent` を指定。**LVGL のデフォルトも同関数**（lv_conf_internal.h:3206）のため、未定義だと `LV_USE_PERF_MONITOR=1` でリンクエラー | 自作 OSAL で `lv_os_get_idle_percent()` を実装（`lv_timer_get_idle()` 委譲。精度差は既知の制限として記録）。lv_conf_user.h の override は削除 |
| μT-Kernel タイマ周期 `CNF_TIMER_PERIOD=10`(ms) | `tk_dly_tsk` の分解能が 10ms。`lv_timer_handler` 駆動が 10ms 量子化され **リフレッシュ実効 ~50fps**（`LV_DEF_REFR_PERIOD=16ms` → 20ms に量子化）。KPI 30fps は満たす。Vsync/dlist 完了などの**イベント起床は量子化されない**（tk_sig_sem は即時） | まず 10ms のまま実機計測。60fps を狙う場合は `mtk3_bsp2/config/config.h` の `CNF_TIMER_PERIOD` を 1 に変更（1GHz CM85 で 1kHz tick のオーバーヘッドは無視できる）。R-006 実機で判断 |
| カーネル資源数 | `CNF_MAX_MTXID=4` は不足（general + mem + xd2 + キャッシュ複数で 6 個以上）。タスク +3 本（lvgl/dave2d/swdraw）、セマフォ +6 個程度 | `config.h` の `CNF_MAX_MTXID` を **4→16** に変更（TSKID 32 / SEMID 16 / FLGID 16 は現状で足りる）。再 vendoring 時の再適用チェックリストに追加 |
| `lv_lock_isr` / `lv_mutex_lock_isr` | μT-Kernel はミューテックスを ISR から操作できない | 本プロジェクトでは**未使用**（src/ 全 grep で呼び出しゼロ。LVGL 内部も dave2d/sw 経路では不使用）。`LV_RESULT_INVALID` を返す実装とし、制約として文書化 |

---

## 3. 3 案の比較評価

| 評価軸 | 案A: μT-Kernel 向け OSAL 自作（LV_OS_CUSTOM） | 案B: CMSIS-RTOS2 抽象層経由 | 案C: OSAL 非依存化（LV_OS_NONE） |
|--------|----|----|----|
| 仕組みの成立性 | ◎ `LV_OS_CUSTOM` + `LV_OS_CUSTOM_INCLUDE` は公式機構。lv_conf_user.h だけで切替可能（2.1/2.2 で確認） | △ `lv_cmsis_rtos2.c` は存在するが、**μT-Kernel 向け CMSIS-RTOS2 実装（osThreadNew/osMutexNew(再帰)/osSemaphore/osKernelGetTickCount 等）が存在しない**（リポジトリ内・TRON Forum 公式とも無し）。橋渡し層の自作が前提 | ○ 機構としては成立（lv_os.h がインライン no-op を提供） |
| 実装コスト | ○ 新規 1 ファイル（~400 行、本書 5.3 にスケッチ完備）+ lv_conf_user.h 数行 | × 案A 相当の作業（μT-Kernel で CMSIS-RTOS2 を実装）**に加えて** CMSIS-RTOS2 API 全般の意味論（カーネル状態・tick 換算・フラグ）合わせが必要。3 案中最大 | △ OSAL コードは不要だが、**`lv_lock` が no-op 化**するため NT-Shell（usrcmd.c の lv_lock 区間×4 箇所）と LVGL タスク間の排他を**自前ミューテックスで全箇所再設計**。ヘッダで明文化済みの lv_lock 契約（ui_main_screen.h ほか）も書き換え |
| 保守性 | ◎ LVGL 標準の契約（lv_lock/描画スレッド）を維持。ユーザーコード無変更（usrcmd.c そのまま）。対応関係が lv_freertos.c と 1:1 でレビュー容易 | × 二重抽象（LVGL→CMSIS→μT-Kernel）。障害解析時に層が 1 つ増える。CMSIS 層は LVGL 以外に利用者がおらず費用対効果が無い | △ LVGL 内部 `#if LV_USE_OS` 分岐が全て非 OS 側になり、将来 LVGL 更新時の挙動差異が読みにくい。自前排他は LVGL 更新と無関係に保守が必要 |
| 性能（30fps 目標） | ◎ 現行 FreeRTOS 構成と同じ**レンダスレッド並列モード**（dave2d スレッド + GPU 非同期）を維持。60fps 実績構成と等価 | ○ 理論上は案A 同等だが、CMSIS 層のオーバーヘッド（ハンドル変換・属性解釈）が毎ロックに乗る | △ 描画が `lv_timer_handler` 内の**同期実行**になり、GPU 実行中 CPU がブロック。30fps は満たす見込みだが 60fps 実績構成からの後退。実測リスクあり |
| FSP 再生成耐性 | ◎ 変更は src/（lv_conf_user.h / 自作 OSAL）のみ。`ra/lvgl/` `ra_cfg/` 無編集 | ○ 同左（CMSIS 層も src/ に置ける） | ◎ 同左 |
| **FSP 層の FreeRTOS 依存（2.4）への効果** | －（別途対処。5.4-5.6） | －（同左） | **－ 削減効果なし**。`r_drw_irq.c` 等は `BSP_CFG_RTOS==2` で分岐しており `LV_USE_OS` と無関係。案C を選んでも 2.4 の 3 件は丸ごと残る |
| リスク | 低: lv_thread_sync の意味論合わせ（→ 5.3 でセマフォにより等価実装、FreeRTOS 版の sticky signal はカウンティングセマフォが包含）。`lv_lock_isr` 非対応（未使用確認済み） | 高: CMSIS-RTOS2 の仕様面積が広く、未使用パスの実装漏れ・意味論差異が潜在 | 中: 排他再設計の漏れ＝再現困難なレース。性能後退の可能性 |
| 総合 | **◎ 採用** | × 不採用 | △ 不採用（案A 失敗時のフォールバック） |

## 4. 採用案の決定と根拠

**案A「μT-Kernel 向け LVGL OSAL 自作（`LV_USE_OS = LV_OS_CUSTOM`）」を採用する。**

根拠:
1. **切替コストが最小かつ ra/ 無編集**: FSP 生成 lv_conf.h の `#ifndef` ガードにより、
   `src/lv_conf_user.h` への 2 行（`LV_USE_OS` / `LV_OS_CUSTOM_INCLUDE`）と src/ の自作
   OSAL 1 ファイルで完結。`lv_freertos.c` はガードで空コンパイルされ、ビルド除外も不要。
   FSP 再生成耐性が完全。
2. **検証済み構成の温存**: 現行の 60fps 実績は「dave2d レンダスレッド + lv_lock 契約」の
   上に成立している。案A はこの構成を OS だけ差し替えて維持するため、性能・排他設計の
   再検証範囲が最小。usrcmd.c（NT-Shell `lvgl` コマンド）等のユーザーコードは無変更。
3. **案C の見かけの簡単さは成立しない**: 案C でも FSP 層の FreeRTOS 依存（2.4 の 3 件）は
   全て残るため削減効果が小さく、一方で lv_lock no-op 化に伴う排他の自前再設計という
   新たなリスクを抱え込む。
4. **案B は前提（μT-Kernel 向け CMSIS-RTOS2 実装）が存在せず**、それを自作するなら
   案A を直接作る方が層が 1 つ少なく、性能・保守の両面で合理的。
5. μT-Kernel API の対応付けが素直（→ 5.2）: スレッド=tk_cre_tsk/tk_sta_tsk、
   mutex=tk_cre_mtx（再帰は薄いラッパで付与）、sync=カウンティングセマフォ
   （ISR からの signal も `tk_sig_sem` で許可される ― 手順書 5.1 の制約に適合）。

フォールバック: 案A の実装で予期せぬ問題（描画スレッドの同期不全等）が出た場合は、
`lv_conf_user.h` の `LV_USE_OS` を `LV_OS_NONE` へ**コンパイル時に切り替える**ことで
案C（同期描画）へ縮退できる（`#if LV_USE_OS` の `#else` パスで `execute_drawing` が
ディスパッチ内で同期実行される）。OSAL 以外の成果物（表示ポート・r_drw 置換・タッチ）は
全案共通のためそのまま流用でき、完全な作り直しは不要。ただし `LV_OS_NONE` では
`lv_lock` が no-op 化するため、縮退時は usrcmd.c の `lvgl` コマンドを一時無効化するか
自前排他を併設する（3 章 案C の評価どおり）。
**注意**: 「`lv_thread_init` を失敗させれば実行時に同期描画へ自動退避する」は**不成立**。
`lv_draw_dave2d.c:104` / `lv_draw_sw.c:98` は `lv_thread_init` の戻り値を無視し、
ディスパッチは `#if LV_USE_OS` のコンパイル時分岐で `lv_thread_sync_signal` を呼び続ける
（さらに sync 自体がレンダスレッド内 `lv_draw_dave2d.c:446` で初期化されるため、
スレッド未生成では未初期化 sync への signal となり描画が停止する）。
フォールバックは必ず `LV_OS_NONE` への明示的な切り替えで行うこと。

---

## 5. R-006 実装方針（着手可能な粒度）

### 5.1 変更対象ファイル一覧

| # | ファイル | 新規/変更 | 内容 |
|---|----------|-----------|------|
| 1 | `src/lv_os_mtkernel.h` | **新規** | カスタム OSAL 型定義（5.3） |
| 2 | `src/lv_os_mtkernel.c` | **新規** | カスタム OSAL 実装（5.3） |
| 3 | `src/lv_conf_user.h` | 変更 | `LV_USE_OS LV_OS_CUSTOM` / `LV_OS_CUSTOM_INCLUDE "lv_os_mtkernel.h"` を追加。`LV_SYSMON_GET_IDLE` override を削除（デフォルト＝自作 `lv_os_get_idle_percent` に委ねる） |
| 4 | `src/User_FreeRTOSConfig.h` | 変更 | `traceTASK_SWITCHED_IN/OUT` フックを削除（未定義シンボル化の回避） |
| 5 | `src/port/lvgl_port_mtk3.c` / `.h` | **新規** | rm_lvgl_port の μT-Kernel 版（Vsync セマフォ・flush/flush_wait・LVGL tick）（5.4） |
| 6 | `src/port/glcdc_port.c` | 変更 | `RM_LVGL_PORT_Open(&g_lvgl_port_ctrl, &g_lvgl_port_cfg)` → `lvgl_port_mtk3_open(&g_lvgl_port_cfg)`。`g_lvgl_port_ctrl.p_lv_display` 参照 → `lvgl_port_mtk3_get_display()` |
| 7 | `src/port/r_drw_irq_mtk3.c` | **新規** | `r_drw_irq.c` の μT-Kernel 版（`d1_initirq_intern`/`d1_shutdownirq_intern`/`d1_queryirq`/`drw_int_isr`）（5.5） |
| 8 | （e2 studio GUI ― ユーザー手動） | 設定 | `ra/fsp/src/r_drw/r_drw_irq.c` を Exclude resource from build（全 Configuration。`.cproject` に保存され FSP 再生成で消えない） |
| 9 | `src/port/lv_port_indev.c` | 変更 | rm_comms_i2c コールバックモード化 + イベントフラグ/セマフォの μT-Kernel 化 + `vTaskDelay`→`tk_dly_tsk`（5.6） |
| 10 | `src/lvgl_thread_entry.c` | 変更 | uT-Kernel タスク `lvgl_task(INT, void*)` 化（旧エントリはラッパ残置 ― ntshell/camera と同一パターン）。`vTaskDelay(1)`→`lv_timer_handler` 戻り値ベースの `tk_dly_tsk` |
| 11 | `src/usermain.c` | 変更 | `T_CTSK ctsk_lvgl`（itskpri=14 / stksz=8192）を追加し camera の後に生成・起動 |
| 12 | `src/camera_display.c` | 変更 | `xTaskGetTickCount()`→`tk_get_otm`。AI 連携イベント（`g_ai_app_event`）→ μT-Kernel イベントフラグ ID 参照へ（R-007 で生成。未生成（ID<=0）はスキップする現行ガード踏襲） |
| 13 | `src/ui/fall_detection_screen.c` / `src/port/dave2d_port.c` | 変更 | 未使用の `#include "FreeRTOS.h"` を削除（または μT-Kernel 化コメントへ置換） |
| 14 | `mtk3_bsp2/config/config.h` | 変更 | `CNF_MAX_MTXID 4→16`。（任意・実測後）`CNF_TIMER_PERIOD 10→1` |
| 15 | `doc/migration/mtk3-migration-guide.md` | 変更 | 7.4 実装メモ・6.2 再適用チェックリスト更新 |

変更しないもの: `configuration.xml` / `ra_gen/` / `ra/fsp/`（除外設定のみ）/ `ra/lvgl/` /
`ra_cfg/` / `src/usrcmd.c`（lv_lock 契約維持のため無変更）。

### 5.2 API 対応表（LVGL OSAL → μT-Kernel 3.0）

| LVGL OSAL API | FreeRTOS 実装（lv_freertos.c） | μT-Kernel 3.0 実装（自作 OSAL） |
|---------------|-------------------------------|--------------------------------|
| `lv_thread_init` | `xTaskCreate`（prio = `tskIDLE_PRIORITY + lv_prio`） | `tk_cre_tsk`（`TA_HLNG\|TA_RNG3`, stksz=引数 bytes, `bufptr=NULL`） + `tk_sta_tsk`。優先度マップは 5.7 |
| `lv_thread_delete` | `vTaskDelete` | `tk_ter_tsk` + `tk_del_tsk`（本プロジェクトでは平常時未使用。スレッド本体終了時は `tk_exd_tsk`） |
| `lv_mutex_init` | `xSemaphoreCreateRecursiveMutex`（遅延生成） | `tk_cre_mtx`（`T_CMTX.mtxatr=TA_INHERIT` ― 優先度継承で描画スレッド/NT-Shell 間の優先度逆転を回避）。**再帰は owner(`tk_get_tid`)+count のラッパで付与**（μT-Kernel mutex は非再帰、二重ロックは E_ILUSE） |
| `lv_mutex_lock` | `xSemaphoreTakeRecursive(portMAX_DELAY)` | owner==自タスクなら count++、それ以外 `tk_loc_mtx(id, TMO_FEVR)` |
| `lv_mutex_lock_isr` | `xSemaphoreTakeFromISR` | **非対応**（μT-Kernel は ISR から mutex 操作不可）。`LV_RESULT_INVALID` を返す。**本プロジェクト未使用を確認済み** |
| `lv_mutex_unlock` | `xSemaphoreGiveRecursive` | count>1 なら count--、最後は `tk_unl_mtx` |
| `lv_mutex_delete` | `vSemaphoreDelete` | `tk_del_mtx` |
| `lv_thread_sync_init` | カウンティングセマフォ + sticky signal | `tk_cre_sem`（`T_CSEM`: `sematr=TA_TFIFO\|TA_CNT`, `isemcnt=0`, `maxsem=32767`）。カウンティングセマフォは「待ち前の signal を記憶する」ため FreeRTOS 版の sticky signal 意味論を包含 |
| `lv_thread_sync_wait` | `xSemaphoreTake(portMAX_DELAY)` 相当 | `tk_wai_sem(id, 1, TMO_FEVR)` |
| `lv_thread_sync_signal` | `xSemaphoreGive` 相当 | `tk_sig_sem(id, 1)` |
| `lv_thread_sync_signal_isr` | `xSemaphoreGiveFromISR` + `portYIELD_FROM_ISR` | `tk_sig_sem(id, 1)`（通知系・ISR 可 ― 手順書 5.1。遅延ディスパッチのため明示 yield 不要） |
| `lv_thread_sync_delete` | `vSemaphoreDelete` | `tk_del_sem` |
| `lv_lock` / `lv_unlock` | （lv_os.c 汎用実装） | 同左（自作 `lv_mutex_*` がそのまま使われる。**ユーザーコード無変更**） |
| `lv_os_get_idle_percent` | trace フックで計測（タスクスイッチ毎） | `lv_timer_get_idle()` へ委譲（LVGL タイマベース。精度は劣るが PERF_MONITOR 表示用途には十分。R-008 で必要なら BSP2 アイドルフック計測に拡張） |
| LVGL tick（OSAL 外・rm_lvgl_port が設定） | `xTaskGetTickCount()` | `tk_get_otm(SYSTIM*)` の `.lo`（ms。R-005 の camera_framebuffer と同一手法） |

### 5.3 PoC コードスケッチ ― カスタム OSAL（リスク最大箇所）

`src/lv_os_mtkernel.h`（型定義。LVGL は構造体を**ゼロ初期化**して使うため、ID=0 を
「未生成」マーカーに使える ― μT-Kernel の ID は 1 以上）:

```c
#ifndef LV_OS_MTKERNEL_H
#define LV_OS_MTKERNEL_H

#include <tk/tkernel.h>

typedef struct {
    void (*pfn)(void *);    /* LVGL スレッド関数 */
    void *arg;
    ID   tskid;             /* 0 = 未生成 */
} lv_thread_t;

typedef struct {
    ID   mtxid;             /* 0 = 未生成 */
    ID   owner;             /* 再帰用: 保持タスク ID（0 = 非保持） */
    UINT count;             /* 再帰ロック深度 */
} lv_mutex_t;

typedef struct {
    ID   semid;             /* 0 = 未生成（カウンティングセマフォ） */
} lv_thread_sync_t;

#endif /* LV_OS_MTKERNEL_H */
```

`src/lv_os_mtkernel.c`（主要部。エラーハンドリングは E_OK / 正 ID チェックを全箇所で行う）:

```c
#include "lvgl.h"               /* lv_os.h / lv_result_t / LV_LOG_* */
#if LV_USE_OS == LV_OS_CUSTOM

#include <tk/tkernel.h>

/* --- 優先度マップ（5.7 の表と一致させる） --- */
static PRI prio_map(lv_thread_prio_t p)
{
    switch (p) {
        case LV_THREAD_PRIO_HIGHEST: return 12;
        case LV_THREAD_PRIO_HIGH:    return 13;   /* dave2d / swdraw */
        case LV_THREAD_PRIO_MID:     return 14;   /* = lvgl_task と同位 */
        case LV_THREAD_PRIO_LOW:     return 15;
        default:                     return 16;
    }
}

/* --- スレッド --- */
static void thread_entry(INT stacd, void *exinf)
{
    (void)stacd;
    lv_thread_t *t = (lv_thread_t *)exinf;
    t->pfn(t->arg);
    tk_exd_tsk();               /* 自タスクを終了し資源を返却 */
}

lv_result_t lv_thread_init(lv_thread_t *thread, const char *const name,
                           lv_thread_prio_t prio, void (*callback)(void *),
                           size_t stack_size, void *user_data)
{
    (void)name;                 /* USE_OBJECT_NAME=0 のため dsname は使わない */
    thread->pfn = callback;
    thread->arg = user_data;

    T_CTSK ctsk = {
        .exinf   = thread,
        .tskatr  = TA_HLNG | TA_RNG3,
        .task    = thread_entry,
        .itskpri = prio_map(prio),
        .stksz   = (SZ)stack_size,   /* LV_DRAW_THREAD_STACK_SIZE = 0x2000 */
        .bufptr  = NULL,             /* USE_IMALLOC=1 で自動確保 */
    };
    ID tskid = tk_cre_tsk(&ctsk);
    if (tskid <= E_OK) { LV_LOG_ERROR("tk_cre_tsk %d", (int)tskid); return LV_RESULT_INVALID; }
    if (tk_sta_tsk(tskid, 0) != E_OK) { tk_del_tsk(tskid); return LV_RESULT_INVALID; }
    thread->tskid = tskid;
    return LV_RESULT_OK;
}

/* --- ミューテックス（再帰対応ラッパ） --- */
static lv_result_t check_mutex_init(lv_mutex_t *m)
{
    if (m->mtxid > 0) return LV_RESULT_OK;
    tk_dis_dsp();                       /* タスク間の生成競合のみ防げばよい（ISR からは生成しない） */
    if (m->mtxid <= 0) {
        T_CMTX cmtx = { .exinf = NULL, .mtxatr = TA_INHERIT };
        ID id = tk_cre_mtx(&cmtx);
        if (id > 0) { m->mtxid = id; m->owner = 0; m->count = 0; }
    }
    tk_ena_dsp();
    return (m->mtxid > 0) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

lv_result_t lv_mutex_lock(lv_mutex_t *m)
{
    if (check_mutex_init(m) != LV_RESULT_OK) return LV_RESULT_INVALID;
    ID self = tk_get_tid();
    if (m->owner == self) { m->count++; return LV_RESULT_OK; }   /* 再帰ロック */
    if (tk_loc_mtx(m->mtxid, TMO_FEVR) != E_OK) return LV_RESULT_INVALID;
    m->owner = self; m->count = 1;
    return LV_RESULT_OK;
}

lv_result_t lv_mutex_unlock(lv_mutex_t *m)
{
    if (m->owner != tk_get_tid()) return LV_RESULT_INVALID;
    if (--m->count > 0) return LV_RESULT_OK;
    m->owner = 0;
    return (tk_unl_mtx(m->mtxid) == E_OK) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

lv_result_t lv_mutex_lock_isr(lv_mutex_t *m)
{
    (void)m;                            /* μT-Kernel: ISR から mutex 不可。本 PJ 未使用 */
    LV_LOG_ERROR("lv_mutex_lock_isr is not supported on uT-Kernel");
    return LV_RESULT_INVALID;
}

/* --- スレッド同期（カウンティングセマフォ） --- */
lv_result_t lv_thread_sync_init(lv_thread_sync_t *s)
{
    if (s->semid > 0) return LV_RESULT_OK;
    T_CSEM csem = { .exinf = NULL, .sematr = TA_TFIFO | TA_CNT,
                    .isemcnt = 0, .maxsem = 32767 };
    ID id = tk_cre_sem(&csem);
    if (id <= E_OK) return LV_RESULT_INVALID;
    s->semid = id;
    return LV_RESULT_OK;
}

lv_result_t lv_thread_sync_wait(lv_thread_sync_t *s)
{
    return (tk_wai_sem(s->semid, 1, TMO_FEVR) == E_OK) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

lv_result_t lv_thread_sync_signal(lv_thread_sync_t *s)
{
    ER er = tk_sig_sem(s->semid, 1);
    return (er == E_OK || er == E_QOVR) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

lv_result_t lv_thread_sync_signal_isr(lv_thread_sync_t *s)
{
    ER er = tk_sig_sem(s->semid, 1);    /* 通知系・ISR 可（手順書 5.1）。yield 不要 */
    return (er == E_OK || er == E_QOVR) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

/* --- SYSMON 用アイドル率（LV_SYSMON_GET_IDLE のデフォルト解決先） --- */
uint32_t lv_os_get_idle_percent(void)
{
    return lv_timer_get_idle();         /* LVGL タイマベース。trace フック方式より粗いが表示用途には十分 */
}

#endif /* LV_USE_OS == LV_OS_CUSTOM */
```

`src/lv_conf_user.h` への追記（FSP lv_conf.h の `#ifndef` ガードで ra/ 無編集のまま有効化）:

```c
/* R-006: LVGL OSAL を μT-Kernel 3.0 自作バックエンドへ切替（R-006a 案A） */
#define LV_USE_OS            LV_OS_CUSTOM
#define LV_OS_CUSTOM_INCLUDE "lv_os_mtkernel.h"
/* LV_SYSMON_GET_IDLE の override は削除（デフォルト lv_os_get_idle_percent =
 * 自作 OSAL の実装が使われる） */
```

### 5.4 表示ポート（rm_lvgl_port の μT-Kernel 版）スケッチ

`src/port/lvgl_port_mtk3.c` ― `rm_lvgl_port.c`（FSP）と 1:1 対応。
**`g_lvgl_port_cfg`（ra_gen）を読み取り専用で再利用**するため、FSP 再生成で
フレームバッファ名・サイズが変わっても追従する。

```c
#include "common_data.h"        /* g_lvgl_port_cfg, rm_lvgl_port_cfg.h (LVGL_DISPLAY_*) */
#include "r_glcdc.h"
#include "lv_display.h"
#include <tk/tkernel.h>

static ID            s_vsync_semid;        /* Vsync(LINE_DETECTION) 同期 */
static display_cfg_t s_disp_cfg;           /* g_display0_cfg のコピー（callback 差し替え用） */
static lv_display_t *s_lv_display;
static rm_lvgl_port_cfg_t const *s_p_cfg;  /* = &g_lvgl_port_cfg */

/* GLCDC ISR から呼ばれる（_rm_lvgl_port_display_callback の代替） */
static void disp_callback(display_callback_args_t *p_args)
{
    if (DISPLAY_EVENT_LINE_DETECTION == p_args->event) {
        (void)tk_sig_sem(s_vsync_semid, 1);     /* E_QOVR は無視（maxsem=1 飽和） */
    }
    /* 既存のユーザーコールバック（glcdc_port.c の Vsync 統計）へ転送
     * ― rm_lvgl_port と同じイベント変換（VPOS/UNDERFLOW）を行って呼ぶ */
    ...
}

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    if (lv_display_flush_is_last(d)) {
        SCB_CleanInvalidateDCache_by_Addr(px,
            LVGL_DISPLAY_BUFFER_STRIDE_BYTES_INPUT * LVGL_DISPLAY_VSIZE_INPUT);
        fsp_err_t e;
        do {
            e = R_GLCDC_BufferChange(s_p_cfg->p_display_instance->p_ctrl, px,
                                     s_p_cfg->inherit_frame_layer);
        } while (FSP_ERR_INVALID_UPDATE_TIMING == e);
    }
}

static void flush_wait_cb(lv_display_t *d)
{
    if (lv_display_flush_is_last(d)) {
        (void)tk_wai_sem(s_vsync_semid, 1, TMO_POL);   /* 残っていれば捨てる（rm_lvgl_port と同じ二段取り） */
        (void)tk_wai_sem(s_vsync_semid, 1, TMO_FEVR);  /* 次の Vsync まで待つ（イベント起床・tick 量子化なし） */
    }
}

static uint32_t tick_get_cb(void)
{
    SYSTIM tim; tk_get_otm(&tim);
    return (uint32_t)tim.lo;            /* ms（R-005 camera_framebuffer と同一手法） */
}

fsp_err_t lvgl_port_mtk3_open(rm_lvgl_port_cfg_t const *p_cfg)
{
    s_p_cfg = p_cfg;
    T_CSEM csem = { .sematr = TA_TFIFO | TA_FIRST, .isemcnt = 0, .maxsem = 1 };
    s_vsync_semid = tk_cre_sem(&csem);   /* <=0 ならエラー復帰 */

    /* フレームバッファクリア → display cfg コピー + callback 差し替え → GLCDC 起動 */
    s_disp_cfg = *(p_cfg->p_display_instance->p_cfg);
    s_disp_cfg.p_callback = disp_callback;
    R_GLCDC_Open(p_cfg->p_display_instance->p_ctrl, &s_disp_cfg);
    R_GLCDC_Start(...); R_GLCDC_BufferChange(... fb1 ...);   /* rm_lvgl_port.c:103-140 と同一手順 */

    s_lv_display = lv_display_create(LVGL_DISPLAY_HSIZE_INPUT, LVGL_DISPLAY_VSIZE_INPUT);
    lv_display_set_flush_cb(s_lv_display, flush_cb);
    lv_display_set_flush_wait_cb(s_lv_display, flush_wait_cb);
    lv_display_set_buffers_with_stride(...);                 /* rm_lvgl_port.c:153-158 と同一 */
    lv_tick_set_cb(tick_get_cb);
    return FSP_SUCCESS;
}

lv_display_t *lvgl_port_mtk3_get_display(void) { return s_lv_display; }
```

### 5.5 D/AVE 2D 割り込み（r_drw_irq）置換スケッチ

- **ユーザー手動（e2 studio GUI）**: `ra/fsp/src/r_drw/r_drw_irq.c` を右クリック →
  Resource Configurations → Exclude from Build →（Debug/Release 両方にチェック）。
  除外は `.cproject` sourceEntries に保存され Generate Project Content で消えない
  （R-002 で mtk3_bsp2 非ターゲット木に使ったのと同じ機構）。FSP **版数更新時のみ**
  原本との差分を確認して `r_drw_irq_mtk3.c` に反映する（再適用チェックリストに追加）。
- `src/port/r_drw_irq_mtk3.c` ― 原本（200 行・自己完結）と同一構造で OS 部のみ置換:

```c
#include "../../ra/fsp/src/r_drw/r_drw_base.h"   /* d1_device_flex（FSP 内部ヘッダを相対 include） */
#include "r_drw_cfg.h"
#include <tk/tkernel.h>

static ID s_d1_semid;                    /* dlist 完了同期 */
extern const uint8_t DRW_INT_IPL;
void drw_int_isr(void);

d1_int_t d1_initirq_intern(d1_device_flex *handle)
{
    R_DRW->IRQCTL = DRW_PRV_IRQCTL_ALLIRQ_CLEAR_AND_DLISTIRQ_ENABLE;
    R_BSP_IrqCfgEnable((IRQn_Type)DRW_CFG_INT_IRQ, DRW_INT_IPL, handle);
    T_CSEM csem = { .sematr = TA_TFIFO | TA_FIRST, .isemcnt = 0, .maxsem = 1 };
    s_d1_semid = tk_cre_sem(&csem);
    return (s_d1_semid > 0) ? 1 : 0;
}

d1_int_t d1_shutdownirq_intern(d1_device_flex *handle)   /* r_drw_base.c:99 (d1_closedevice 経路) から呼ばれる。欠けるとリンクエラー */
{
    (void)handle;
    NVIC_DisableIRQ((IRQn_Type)DRW_CFG_INT_IRQ);
    R_DRW->IRQCTL = DRW_PRV_IRQCTL_ALLIRQ_DISABLE_AND_CLEAR;
    tk_del_sem(s_d1_semid);             /* vSemaphoreDelete の置換 */
    s_d1_semid = 0;
    return 1;
}

d1_int_t d1_queryirq(d1_device *handle, d1_int_t irqmask, d1_int_t timeout)
{
    /* d1_to_wait_forever(-1) == TMO_FEVR(-1)。0 は TMO_POL。それ以外は ms とみなす
     * （FreeRTOS 版も tick=1ms で生値を渡しており同じ意味） */
    ER er = tk_wai_sem(s_d1_semid, 1, (TMO)timeout);
    return (er == E_OK) ? 1 : 0;
}

void drw_int_isr(void)                   /* ベクタは ra_gen が drw_int_isr シンボルを参照（同名で置換） */
{
    ...原本と同一（STATUS 読み・IRQCTL クリア・indirect 継続）...
    (void)tk_sig_sem(s_d1_semid, 1);     /* xSemaphoreGiveFromISR + portYIELD_FROM_ISR の置換。E_QOVR 無視 */
    R_BSP_IrqStatusClear(irq);
}
```

### 5.6 タッチパネル（lv_port_indev.c）の置換

| 現状（FreeRTOS） | R-006（μT-Kernel） |
|------------------|--------------------|
| `g_comms_i2c_bus0_extended_cfg` に blocking semaphore / recursive mutex を実行時生成して渡す（rm_comms_i2c が内部で `xSemaphoreTake` ブロック） | open 前に `p_extend->p_blocking_semaphore = NULL; p_extend->p_bus_recursive_mutex = NULL;` を設定（**ra_gen の cfg 構造体は非 const のため実行時上書き可・編集不要**）。driver はコールバックモードになる |
| `comms_i2c_callback`（ISR）: `xEventGroupSetBitsFromISR(g_i2c_event_group, ...)` | `tk_set_flg(s_touch_i2c_flgid, ...)`（R-005 の ov5640 `i2c_camera_callback` と同一パターン） |
| `i2c_wait()`: `xEventGroupWaitBits` | `tk_wai_flg(..., TWF_ORW \| TWF_BITCLR, &ptn, I2C_TIMEOUT_MS)` |
| `touch_irq_callback`（ISR）: `xSemaphoreGiveFromISR(g_irq_binary_semaphore)` | `tk_sig_sem(s_touch_irq_semid, 1)`（maxsem=1, E_QOVR 無視） |
| `touchpad_is_pressed()`: `xSemaphoreTake(g_irq_binary_semaphore, 0)` | `tk_wai_sem(s_touch_irq_semid, 1, TMO_POL) == E_OK` |
| `vTaskDelay(10)` / `vTaskDelay(pdMS_TO_TICKS(100))` | `tk_dly_tsk(10)` / `tk_dly_tsk(100)` |

`g_i2c_event_group` / `g_irq_binary_semaphore`（ra_gen・`g_hal_init()` 未実行で NULL）への
参照は撤去し、src 側の μT-Kernel オブジェクト（タスク先頭で冪等生成）に置き換える。

### 5.7 タスク優先度・スタック構成（初期値。R-008 で最終調整）

| タスク | itskpri | stksz | 備考 |
|--------|---------|-------|------|
| 初期タスク（usermain） | （BSP2 既定） | － | 既存 |
| blink | 10 | 1024 | 既存（R-003） |
| camera | 11 | 4096 | 既存（R-005） |
| ntshell | 12 | 4096 | 既存（R-004） |
| **dave2d / swdraw（OSAL 生成）** | **13**（PRIO_HIGH） | 0x2000（`LV_DRAW_THREAD_STACK_SIZE`） | LVGL 設計どおり lvgl_task より高優先（レンダスレッドは通常 sync 待ちで眠っている） |
| **lvgl_task** | **14** | **8192**（FreeRTOS 版 lvgl_thread と同値） | `lv_timer_handler` ループ |

ループ駆動（lvgl_thread_entry.c）:

```c
while (1) {
    uint32_t wait_ms = lv_timer_handler();      /* 次タイマまでの ms を返す */
    if (wait_ms == 0) wait_ms = 1;
    if (wait_ms > 500) wait_ms = 500;
    tk_dly_tsk((RELTIM)wait_ms);
}
```

`CNF_TIMER_PERIOD=10` のままなら遅延は 10ms 量子化 → リフレッシュ実効 ~50fps
（KPI 30fps 充足）。60fps を狙う場合のみ `CNF_TIMER_PERIOD=1` へ変更して実測比較する。

### 5.8 カーネル設定変更（`mtk3_bsp2/config/config.h`）

| 項目 | 現値 | R-006 | 理由 |
|------|------|-------|------|
| `CNF_MAX_MTXID` | 4 | **16** | LVGL が general + builtin mem + xd2 + キャッシュ群（イメージ×2 + TinyTTF 等）で 6 個以上使用 |
| `CNF_TIMER_PERIOD` | 10 | 10 のまま（任意で 1） | 5.7 参照。実測後に判断 |
| `CNF_MAX_TSKID` / `CNF_MAX_SEMID` / `CNF_MAX_FLGID` | 32 / 16 / 16 | 変更なし | タスク +3、セマフォ +6 程度、フラグ +1 で収まる |

### 5.9 実装ステップ（小さく刻み、各段で実機確認）

1. **OSAL + 空画面**: #1〜#8（OSAL・表示ポート・r_drw 置換・lvgl_task 起動）。
   タッチ初期化（`lv_port_indev_init`）と `camera_display_init` は一時スキップし、
   メイン画面（カラーバー）が LCD に出ること・PERF_MONITOR の FPS 表示を確認。
2. **タッチ**: #9 を適用し、タッチ操作（設定ボタン等）と `touch` コマンドを確認。
3. **カメラ表示**: #12 を適用し、カメラ映像のレターボックス表示と FPS（30fps 目標）を確認。
   NT-Shell `lvgl` コマンド（lv_lock 経由の別タスクアクセス）の動作確認。
4. **計測・調整**: FPS が不足する場合 `CNF_TIMER_PERIOD=1` を試行。結果を手順書 7.4 へ記録。

## 6. リスクと未確定事項

| リスク | 影響 | 緩和策 |
|--------|------|--------|
| `tk_get_tid()` を ISR/タスク独立部で呼ぶと意味を持たない（再帰 mutex の owner 判定） | lv_mutex_lock を ISR から呼んだ場合の誤動作 | LVGL/本 PJ は lv_mutex_lock を タスクからのみ呼ぶ（lock_isr は別 API・非対応と明示）。低リスク |
| dave2d レンダスレッドの起床遅延（同 13 と ntshell 12 の競合） | 描画レイテンシ | レンダスレッドは大半が sync 待ち。問題があれば R-008 で優先度再配置 |
| `r_drw_irq_mtk3.c` が FSP 版数更新で原本と乖離 | Dave2D 動作不全 | 再適用チェックリストに「FSP 更新時に r_drw_irq.c 差分確認」を追加。原本は小さく（200 行）差分確認は容易 |
| `lv_timer_handler` 戻り値駆動と 10ms tick の相互作用で実効 fps が想定下回り | 60fps 未達（30fps KPI は余裕） | `CNF_TIMER_PERIOD=1` への変更で解消可能（5.7） |
| LVGL ヒープ（`lv_malloc`）はタスク間共有 | 排他は builtin mem の mutex（OSAL 経由）で確保 | 案A はこの mutex を有効化したまま維持（案C だと no-op 化して危険だった） |
| `lv_os_get_idle_percent` の精度低下（trace フック廃止） | PERF_MONITOR の CPU% が「LVGL タスク内アイドル」基準になる | 表示用途には十分。KPI 計測は FPS（PERF_MONITOR）と推論時間（R-007）で行うため影響なし。必要なら R-008 で BSP2 アイドルフック計測へ拡張 |

## 7. 受け入れ条件チェック

- [x] 3 案の比較評価（評価軸つきの表）: → 3 章
- [x] 採用案の決定と根拠の明記: → 4 章（案A）
- [x] R-006 で着手できる粒度の実装方針（変更対象ファイル・API 対応表・PoC コードスケッチ）: → 5 章
- [x] `doc/migration/mtk3-migration-guide.md` への結論反映: → 同書 7.4 を更新（本スパイクと同時コミット）
