# R-008 統合動作確認・KPI 検証・運用メモ（FreeRTOS → μT-Kernel 3.0 移行の締め）

- 対象 Issue: #158（R-008）
- 親 Epic: #150（R-000）
- 前提: R-001〜R-007 が実装済み（移行手順書 `doc/migration/mtk3-migration-guide.md`、
  スパイク報告書 `doc/migration/r006a-lvgl-osal-spike.md`）。
- 本書の位置づけ: 移行手順書（mtk3-migration-guide.md）の「8. 統合動作確認・KPI 検証」を
  実作業レベルへ展開した**参照ドキュメント**。手順書 8 章からは本書を参照する
  （重複記載を避けるため、タスク一覧・KPI 定義の**マスタは本書**とする）。

> 重要（捏造禁止）: 本書の KPI 実測値・統合動作確認結果は **EK-RA8P1 実機を持つユーザーが
> 後で測定して埋める**。本書作成者（Claude）は実機を持たないため、未測定欄は
> **「未測定（実機）」**と明記し、推測値・捏造値を入れない。本書は「測定手順・記録テンプレート」を
> 整備することが目的である。
>
> ソースコードから読み取った**静的な実態**（タスク優先度・スタックサイズ・同期オブジェクト・
> カーネル設定値）は出典 `file:line` 付きで記載する（これは実測ではなくコード事実）。

---

## 1. 統合構成の全体像（μT-Kernel 3.0 上の最終タスク構成）

方式A（`src/hal_warmstart.c` の静的コンストラクタ `mimamori_start_mtkernel()` →
`knl_start_mtkernel()`、`hal_warmstart.c:152-164`）により FreeRTOS の `main()` /
`vTaskStartScheduler()`（`ra_gen/main.c:112`）には到達せず、μT-Kernel 初期タスク →
`usermain()`（`src/usermain.c:256`）が全アプリタスクを生成・起動する。

`usermain()` が `tk_cre_tsk` + `tk_sta_tsk` で生成・起動するタスクの**起動順**
（`usermain.c` 内の呼び出し順）:

1. blink（`usermain.c:307-320`）
2. ntshell（`usermain.c:333-345`）
3. camera（`usermain.c:364-376`）
4. lvgl（`usermain.c:392-404`）
5. ai_inference（`usermain.c:417-429`）

加えて、LVGL OSAL（`src/lv_os_mtkernel.c`）が `lvgl_task` 内の `lv_init()`
（`lvgl_thread_entry.c:127`）実行中に**描画スレッドを動的生成**する
（dave2d / swdraw、`lv_thread_init` → `tk_cre_tsk`、`lv_os_mtkernel.c:90-122`）。

---

## 2. タスク優先度・スタックサイズ一覧（実ソースからの実態。出典 file:line 付き）

μT-Kernel の優先度は**数値が小さいほど高優先**（`CNF_MAX_TSKPRI=32`、
`mtk3_bsp2/config/config.h:37`）。

| # | タスク | 本体関数 | itskpri | stksz (byte) | 生成箇所 | 本体ファイル |
|---|--------|----------|---------|--------------|----------|--------------|
| 1 | blink | `blink_task` | 10 | 1024 | `usermain.c:143-151` | `usermain.c:92-140` |
| 2 | camera | `camera_task` | 11 | 4096 | `usermain.c:185-192` | `camera_thread_entry.c` |
| 3 | ntshell | `ntshell_task` | 12 | 4096 | `usermain.c:163-170` | `ntshell_thread_entry.c` |
| 4 | dave2d 描画 | LVGL render thread | 13 | 8192 (0x2000) | `lv_os_mtkernel.c:99-108`（`prio_map` HIGH=13、`lv_os_mtkernel.c:67`） | `ra/lvgl/.../lv_draw_dave2d.c:104` |
| 5 | swdraw 描画 | LVGL render thread | 13 | 8192 (0x2000) | 同上 | `ra/lvgl/.../lv_draw_sw.c:99` |
| 6 | lvgl | `lvgl_task` | 14 | 8192 | `usermain.c:207-214` | `lvgl_thread_entry.c:104` |
| 7 | ai_inference | `ai_inference_task` | 15 | 16384 (0x4000) | `usermain.c:234-241` | `ai_inference_thread_entry.c:321` |

補足（出典つき）:

- 描画スレッドのスタック `0x2000`（8192 byte）は `LV_DRAW_THREAD_STACK_SIZE`
  （`ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h:49`）。`lv_os_mtkernel.c:104` が
  `stack_size` をそのまま `T_CTSK.stksz` へ渡す。
- 描画スレッドは **2 本**（dave2d 1 本 + swdraw 1 本）。`LV_DRAW_SW_DRAW_UNIT_CNT` は
  本プロジェクトの `lv_conf.h` で未定義のため内部デフォルト **1**
  （`ra/lvgl/lvgl/src/lv_conf_internal.h:556`）。dave2d は `LV_USE_DRAW_DAVE2D=1` で 1 本。
- 描画スレッドは LVGL 設計上 `lvgl_task` より 1 段高優先（普段は sync 待ちで休眠）
  というコメント根拠は `lv_os_mtkernel.c:60-61`。
- 優先度設計の意図はソース内コメントに記載: blink=10（周期確定重視、`usermain.c:147`）、
  camera=11（NT-Shell より高・LED と同等〜やや低、`usermain.c:178-180`）、
  ntshell=12（対話のためリアルタイム性低、`usermain.c:156-160`）、
  lvgl=14（描画スレッド 13 より低、`usermain.c:198-202`）、
  ai=15（最下位グループ・描画を妨げない、`usermain.c:220-228`）。

> 注意（タスク ID 数の予算）: 静的タスク 5（blink/camera/ntshell/lvgl/ai）+ μT-Kernel 初期タスク 1
> + LVGL 描画スレッド 2 ≒ 8 タスク。`CNF_MAX_TSKID=32`（`config.h:42`）に対して十分。
> LVGL OSAL は `lv_thread_init` のたびにタスクを生成する設計だが、本構成で生成されるのは
> dave2d/swdraw の 2 本のみ（`LV_DRAW_SW_DRAW_UNIT_CNT=1` のため）。

### CPU1（Cortex-M33）について

本移行（R-001〜R-008）の対象は **CPU0 のみ**（手順書 2 章）。CPU1 は **FreeRTOS のまま**で、
CPU0 の `usermain()` が `R_BSP_SecondaryCoreStart()`（`usermain.c:301-304`）で起動する。
CPU1 側のタスク構成は本移行のスコープ外。CPU1 の blinky が LED2(Green) を点滅させる関係は
`usermain.c:84-87` のコメント参照。

---

## 3. 同期・排他オブジェクト一覧（μT-Kernel）と保護対象（出典 file:line 付き）

| オブジェクト | 種別 | 生成箇所 | 用途 / 保護対象 |
|--------------|------|----------|-----------------|
| `g_ai_app_flgid` | イベントフラグ `tk_cre_flg`（`TA_TFIFO\|TA_WMUL`） | `ai_inference_thread_entry.c:359` | カメラ→前処理→推論→オーバーレイのイベント通知（IMAGE_READY / RESULT_UPDATED / ETHOSU_INIT_DONE / AI_INIT_DONE）。`camera_display.c` が set/ref/clr（`camera_display.c:129`） |
| `s_i2c_flgid`（ov5640） | イベントフラグ `tk_cre_flg`（`TA_TFIFO\|TA_WMUL`） | `ov5640.c:185` | OV5640 の I2C 完了割り込み（ISR `i2c_camera_callback` が `tk_set_flg`）→ `ov5640_i2c_wait_complete` の `tk_wai_flg` 同期 |
| LVGL `lv_lock` 用 mutex 群 | 再帰 mutex `tk_cre_mtx`（`TA_INHERIT`）+ owner/count ラッパ | `lv_os_mtkernel.c:180` | LVGL 内部状態（general / builtin mem / Dave2D HW 排他 / 画像・フォントキャッシュ）。6 個以上生成のため `CNF_MAX_MTXID` を 4→16 へ拡張（`config.h:51`） |
| LVGL `lv_thread_sync` 用 sem | カウンティング sem `tk_cre_sem`（`TA_CNT`, maxsem=32767） | `lv_os_mtkernel.c:298` | LVGL 描画スレッドの sticky 同期 |
| Vsync sem（GLCDC ポート） | バイナリ sem `tk_cre_sem`（maxsem=1） | `lvgl_port_mtk3.c:103` | GLCDC Vsync（line-detect）ISR → flush 完了待ち |
| D/AVE 2D 完了 sem | バイナリ sem `tk_cre_sem`（maxsem=1） | `r_drw_irq_mtk3.c:135` | Dave2D dlist 完了割り込み（`drw_int_isr`）→ 描画スレッド待ち（`r_drw_irq.c` の置換実装） |
| タッチ I2C/IRQ フラグ・sem | `tk_cre_flg` / `tk_cre_sem` | `lv_port_indev.c:356` / `:373` | GT911 タッチの I2C 完了・外部 IRQ 同期 |
| LED 点滅 周期ハンドラ | 周期ハンドラ `tk_cre_cyc`（`TA_HLNG\|TA_STA\|TA_PHS`） | `led_ctrl.c:284` | NT-Shell `led <id> blink` の LED 点滅駆動（FreeRTOS ソフトタイマの置換） |

> 資源数の予算（出典 `config.h`）: SEM=16（`:43`）, FLG=16（`:44`）, MTX=16（`:51`、R-006 で 4→16 拡張）,
> CYC=4（`:55`）。上表の実使用数はいずれも上限内。R-008 統合時は、LVGL が生成する mutex 数
> （general + builtin mem + Dave2D + キャッシュ群）が 16 を超えないことを実機ログ（`tk_cre_mtx` の
> 戻り値）で最終確認する（→ 4. 統合動作確認チェックリスト）。

### ISR と共有する状態の保護（割り込みマスクが必須な箇所）

FreeRTOS の `taskENTER_CRITICAL()` は割り込みマスクを伴うが、μT-Kernel の `tk_dis_dsp()` は
ディスパッチ禁止のみで割り込みは止まらない（手順書 5 章の落とし穴）。本プロジェクトで
**ISR と共有する状態**を保護している箇所:

- `jlink_console.c`: UART RX ISR（`jlink_console_callback`）が更新する
  `s_out_of_band_received[]` / `s_g_out_of_band_index` を、タスク側 pop/copy 中に保護。
  R-004 で `taskENTER/EXIT_CRITICAL` ×3 を**割り込みマスク `DI`/`EI`** へ置換済み
  （手順書 7.2 / 更新履歴 2026-06-12 R-004）。`tk_dis_dsp` への単純置換は禁止。

統合時の確認: この割り込みマスク区間が短く保たれ、他の高優先度割り込み（GLCDC Vsync、
MIPI/VIN フレーム完了、NPU IRQ）のレイテンシを過度に増やさないこと（→ 4. チェックリスト）。

---

## 4. 統合動作確認チェックリスト（ユーザー実機実施）

全タスクを同時起動した状態（現在の `usermain.c` がそのまま全 5 タスク + 描画 2 スレッドを
起動する）で確認する。各項目の結果欄に **OK / NG / 未測定** を記入する。

### 4.1 起動・全タスク生成

| # | 確認項目 | 手順 / 期待値 | 結果 | 備考 |
|---|----------|---------------|------|------|
| I-01 | μT-Kernel 起動バナー | 電源投入後シリアル（115200/8N1）に `mimamori-sense uT-Kernel 3.0 boot` バナー（`usermain.c:265-267`） | 未測定 | |
| I-02 | 全タスク生成成功 | `[usermain] blink_task created & started.`〜`ai_inference_task created & started.` が**全 5 行**出る（`tk_cre_tsk` 失敗時は `... failed. ercd=` を出して return -1: `usermain.c:310/335/366/394/419`） | 未測定 | 1 つでも failed が出たら資源不足を疑う |
| I-03 | CPU1 起動 | `[usermain] secondary core (CPU1) started.`（`usermain.c:303`）。マルチコア・デバッグ接続が通る | 未測定 | |
| I-04 | mutex/sem/flg 生成成功 | LVGL `lv_init()` 後に `tk_cre_mtx failed`（`lv_os_mtkernel.c:186`）等のエラーログが出ないこと | 未測定 | 出たら `CNF_MAX_MTXID` 等の上限超過 |

### 4.2 機能の同時動作（結合）

| # | 確認項目 | 手順 / 期待値 | 結果 |
|---|----------|---------------|------|
| I-05 | LED 点滅（blink） | LED1(Blue) が約 0.5s 周期で点滅（`tk_dly_tsk(500)`、`usermain.c:138`） | 未測定 |
| I-06 | NT-Shell 応答 | シリアルで `help` がコマンド一覧を返す（`usrcmd.c:137-153`） | 未測定 |
| I-07 | カメラフレーム取得 | `camera status` で Frame Complete / FPS が増加（`vin_port.c:515`） | 未測定 |
| I-08 | LCD 表示 | カメラ画像が LCD に表示される（`display status` で状態確認） | 未測定 |
| I-09 | タッチ入力 | `touch read` / 画面タッチで座標が取れる（`lv_port_indev.c`） | 未測定 |
| I-10 | AI 推論実行 | `ai status` で Inference count が増加・状態遷移（`ai_cmd.c`） | 未測定 |
| I-11 | 転倒検出 | `fall status` でステートマシン遷移（SUSPECTED/COOLDOWN/Confirmed） | 未測定 |
| I-12 | LED コマンド | `led <id> blink` で周期ハンドラ点滅（`led_ctrl.c:284`） | 未測定 |

### 4.3 リソース競合・デッドロック・優先度逆転の確認観点

| # | 観点 | 確認方法 | 結果 |
|---|------|----------|------|
| I-13 | SCI8 競合（T-Monitor ⇄ jlink_console） | 起動ログに文字化け（`m□` 等）が無いこと。R-005 で T-Monitor 一本化済み（`usermain.c:347-362`） | 未測定 |
| I-14 | デッドロック | 長時間（例 30 分以上）連続動作で全タスクが応答し続ける（NT-Shell が固まらない・LED 点滅継続・FPS が 0 にならない） | 未測定 |
| I-15 | 優先度逆転 | 低優先 ai(15) が mutex 保持中に高優先タスクが待たされ続けないこと。LVGL mutex は `TA_INHERIT`（優先度継承、`lv_os_mtkernel.c:14`）で緩和済み。`lv_lock`（lvgl=14）が長時間 ai(15) に占有されないか観察 | 未測定 |
| I-16 | 割り込みレイテンシ | `jlink_console.c` の `DI/EI` 区間中に Vsync/MIPI/NPU 割り込みが過度に遅延しないこと（フレーム落ち・推論失敗の有無で間接確認） | 未測定 |
| I-17 | スタックオーバーフロー | 各タスクのスタック（2 章の stksz）が枯渇しないこと。異常リセット・ハードフォルトが起きないこと。疑わしい場合は stksz を増やして再現性確認 | 未測定 |
| I-18 | AI 並走時の描画 FPS 低下 | R-007 実機で **AI 並走時カメラ表示 20 FPS（CPU 60%）** を観測済み（手順書 更新履歴 2026-06-12 R-007）。30fps KPI との照合は KPI-01 で実施 | 未測定 |

> I-18 は R-007 時点で残された唯一の KPI 懸念。R-008 の主眼は「全機能同時動作で 30fps KPI を
> 満たすか」の実測判定（→ 5. KPI-01）と、満たさない場合の調整方針（→ 6.）。

---

## 5. KPI 検証シート（ユーザー実機実施）

KPI 定義の出典は `doc/product-requirements.md` 3.1 定量的指標（`:102` 見出し、KPI 表 `:105-109`）。

| ID | KPI | 合格基準 | 出典（要件） | 測定方法 | 測定値 | 判定 |
|----|-----|----------|--------------|----------|--------|------|
| KPI-01 | フレームレート（F-001/F-002） | **30fps 以上** | PR 3.1 `:105` | 全機能同時動作で `camera status` の FPS（`camera_framebuffer.c` の 1 秒窓 FPS、`:170`）。表示更新側は LVGL refresh（`LV_DEF_REFR_PERIOD=16ms`、`lv_conf_user.h:139`） | 未測定 | 未判定 |
| KPI-02 | AI 推論時間（F-003） | **5ms 以内** | PR 3.1 `:106` | `ai time` で NPU inference 時間（DWT サイクル計測、`ai_inference_thread_entry.c:575-584`）。R-007 単独で 5ms 達成済み（更新履歴 R-007）→ R-008 は**全機能同時動作下で再測定** | 未測定 | 未判定 |
| KPI-03 | 転倒検出精度（F-003） | **検出率 90% 以上** | PR 3.1 `:107` | 転倒シナリオを N 回実施し、`fall status` の Confirmed 数 / 実施回数。データセット・回数は別途定義 | 未測定 | 未判定 |
| KPI-04 | 誤検出率（F-003） | **5% 以下** | PR 3.1 `:108` | 非転倒シナリオ（歩行・着座・しゃがみ等）M 回中の誤 Confirmed 数 / M | 未測定 | 未判定 |
| KPI-05 | 転倒検出→通知時間 | **10 秒以内** | PR 3.1 `:109` | 転倒発生から通知（警報音 F-004 / 画面表示）までの実測時間。ステートマシンの COOLDOWN/確定遅延を含む | 未測定 | 未判定 |

### KPI 測定上の注意（コード事実に基づく）

- **KPI-02 の計測粒度**: 推論時間は `dwt_cycles_to_ms()`（`ai_inference_thread_entry.c:217-220`）で
  **整数 ms** に丸められる（`diff / DWT_CYCLES_PER_MS`、`DWT_CYCLES_PER_MS = SystemCoreClock/1000`、
  `:120`）。5ms KPI を ms 粒度で見ると分解能が粗いため、境界付近では DWT サイクル値（`:214`）か
  μs 換算で確認することを推奨。R-007 では `ai time` 表示で 5ms を確認済み（更新履歴 R-007）。
- **KPI-01 と システムティック**: LVGL メインループは `lv_timer_handler()` の戻り値（次タイマまでの ms）で
  `tk_dly_tsk` する（`lvgl_thread_entry.c:264-273`）。`CNF_TIMER_PERIOD=10`（`config.h:39`）のため
  `tk_dly_tsk` は **10ms 量子**で目覚める。`LV_DEF_REFR_PERIOD=16ms` に対し実効リフレッシュは
  約 50fps（30fps KPI を満たす想定。コメント `lvgl_thread_entry.c:255-258`）。一方、Vsync /
  dlist 完了の起床はイベント駆動（`tk_sig_sem`）で量子化されない（同コメント `:257-258`）。
  **30fps に届かない場合の調整は 6. を参照**。
  - 検証ルール上の裏取り: 「実効 ~50fps」は LVGL の自律リフレッシュ周期に基づく**設計上の見積もり**で
    あり、実機の描画負荷（Dave2D/SW 描画時間・SDRAM 帯域）に依存する。**実測 KPI-01 が一次情報**。
    コメントの 50fps は無負荷時の上限であって測定値ではない。
- **カメラ FPS と表示 FPS の別**: `camera status` の FPS は**センサ/VIN フレーム取得**の FPS
  （`camera_framebuffer.c:170` の 1 秒窓カウント）。LCD 表示の滑らかさは別途、目視 + LVGL の
  描画完了周期で評価する。KPI-01（F-001/F-002）は両者を区別して記録する。

---

## 6. KPI-01（30fps）未達時の調整方針（実機測定後に適用）

実測で 30fps に届かない場合、以下を順に検討する（いずれも実機で前後比較する）。

1. **システムティック分解能を上げる**: `CNF_TIMER_PERIOD` を 10→1（ms）にする
   （`config.h:39`）。`tk_dly_tsk` 量子が 1ms になり LVGL リフレッシュが `LV_DEF_REFR_PERIOD=16ms`
   に追従しやすくなる。ティック割り込み頻度が 10 倍になるためオーバーヘッドを実測比較する
   （手順書 7.4 / スパイク報告書 5.7 でも 60fps 狙いは 1ms へ変更し実測判断、と記載）。
   - 注意（再 vendoring）: `config.h` は BSP2 vendoring ファイル。変更は再 vendoring で失われる
     （→ 手順書 6.2 再適用チェックリスト）。
2. **AI タスクの譲り量調整**: AI は 1 サイクルごとに `tk_dly_tsk(AI_THREAD_YIELD)`
   （定義 `AI_THREAD_YIELD=25`: `ai_inference_thread_entry.c:106`、推論ループ末尾での使用:
   `ai_inference_thread_entry.c:650`）で譲る。`CNF_TIMER_PERIOD=10` だと
   `tk_dly_tsk(25)` は次ティック境界へ切り上がり**実効約 30ms**になる（10ms 量子）。
   ティックを 1ms にすれば 25ms に近づく。描画と AI のバランスを実測で最適化する。
   - コード事実: `AI_THREAD_YIELD=25` は `ai_inference_thread_entry.c:106` 定義。
     「描画スレッド性能に影響するため低くしすぎない」旨のコメントは `:97-99`。
3. **優先度の微調整**: ai(15) が描画（dave2d/swdraw=13、lvgl=14）を妨げていないかを I-18 / KPI-01 で
   切り分け、必要なら ai の譲り量を増やす（優先度は既に最下位グループ）。
4. **描画経路の見直し**: Dave2D が使われているか（`dave2d status`）、SW フォールバックが多発して
   いないかを確認。SW 描画主体だと FPS が落ちる。

> いずれの変更も「1 つ変える→実機測定→記録」を守り、本書 5. の表に変更前後の測定値を残す。

---

## 7. FreeRTOS 残置物の整理（維持方針の最終確認・今後の運用メモ）

### 7.1 維持方針（なぜ FreeRTOS 設定を残すか）

移行の絶対方針（手順書 1 章）として **FreeRTOS 利用設定（`configuration.xml` / e2 studio GUI）は
変更しない**。理由は、FreeRTOS 設定を外すと FSP が生成するシステムコール呼び出し箇所が消え、
μT-Kernel へ置換すべき箇所の追跡基準が失われるため。R-008 完了時点でもこの方針を維持する。

確認（コード事実）: `configuration.xml` に FreeRTOS 参照が残存（`grep -c FreeRTOS` = 9 件）。
FSP は `ra_gen/main.c` で `vTaskStartScheduler()`（`:112`）と
`xSemaphoreCreateCounting`（`:90/92`）を生成し続ける。

### 7.2 残置物の一覧と「なぜ問題ないか」（出典 file:line 付き）

| 残置物 | 場所 | 状態 | なぜ問題ないか（裏取り） |
|--------|------|------|--------------------------|
| `vTaskStartScheduler()` | `ra_gen/main.c:112` | リンクされるが**実行されない** | 方式A の静的コンストラクタ `mimamori_start_mtkernel()` が `knl_start_mtkernel()`（戻らない、`hal_warmstart.c:157`）を `main()` より前に呼ぶため `main()` に到達しない。`knl_start_mtkernel` は `__init_array` 実行段（`SystemRuntimeInit` の後）で起動（`hal_warmstart.c:44-47`） |
| `xSemaphoreCreateCounting`（FSP common init sem） | `ra_gen/main.c:90/92` | リンクされるが**実行されない** | 同上（`main()` 未到達） |
| 旧 FreeRTOS blinky（`vTaskDelay(configTICK_RATE_HZ/2)`） | `blinky_thread_entry.c:73` | **未改変・未実行**。機能は `usermain.c` の `blink_task` が代替 | `ra_gen/blinky_thread.c:74` から `blinky_thread_entry()` が参照されリンクには残るが、`ra_gen/blinky_thread.c:55` の `blinky_thread_func` を呼ぶのは `main()` 経由のみ。`main()` 未到達のため実行されない。**blinky だけはラッパ化せず原本のまま残置**（他スレッドと異なる点） |
| 旧 FreeRTOS スレッドエントリ（ntshell/camera/lvgl/ai） | 各 `*_thread_entry.c` 末尾 | **薄いラッパとして残置**（実行されない） | `ra_gen/*_thread.c`（編集禁止）が旧シンボルを参照し、その鎖は `main()` から辿れるため**リンク時に解決が必要**。よって削除せず μT-Kernel タスク本体へ委譲するラッパを残す（例: `lvgl_thread_entry.c:276-289` のコメント）。実行はされない |
| `freertos_hooks.c`（`vApplicationMallocFailedHook`） | `src/freertos_hooks.c:22` | リンクされるが**呼ばれない** | FreeRTOS のヒープ確保失敗時のみ呼ばれるフック。FreeRTOS スケジューラ未起動・`pvPortMalloc` 未使用のため発火しない。FreeRTOS 設定を残す間は残置可（手順書 3.2） |
| `User_FreeRTOSConfig.h` | `src/User_FreeRTOSConfig.h` | 残置（trace フックは R-006 で削除済み） | R-006 で LVGL の `lv_freertos_task_switch_in/out` trace フック定義を削除済み（`lv_freertos.c` 空化による未定義シンボル回避。更新履歴 2026-06-12 R-006）。FreeRTOS 設定を残すために本ファイル自体は保持 |
| `ra/aws/FreeRTOS/...`（FreeRTOS カーネル本体） | FSP 管理 | リンクされるが未実行 | FSP が FreeRTOS 設定に基づき配置。スケジューラ未起動のため未実行。`gc-sections` で未参照部は除去される |
| `ra/lvgl/lvgl/src/osal/lv_freertos.c` | FSP 管理 | **空コンパイル** | `#if LV_USE_OS == LV_OS_FREERTOS` ガード（手順書 3.4）。`lv_conf_user.h` で `LV_USE_OS=LV_OS_CUSTOM` のため中身がコンパイルされない。R-006a 案A（自作 OSAL）採用 |

> 検証ルール（裏取り）注記:
> - 「`vTaskStartScheduler()` が実行されない」は、起動経路がコンストラクタ
>   （`hal_warmstart.c:152` の `__attribute__((constructor))`）で `knl_start_mtkernel()`（戻らない、
>   `:157`）を呼び、その後を無限ループでトラップ（`:160-163`）していることで担保される。
>   コンストラクタは `fsp_gen.lld` の `KEEP(*(.init_array))` で gc-sections から保護
>   （`hal_warmstart.c:46-47` のコメント）。
> - 「blinky だけ原本のまま」は `blinky_thread_entry.c` が R-003 以降未改変で
>   `vTaskDelay(configTICK_RATE_HZ/2)`（`:73`）を保持し、機能は `usermain.c` の `blink_task`
>   （`:92-140`）が μT-Kernel API で代替している、というコード状態に基づく。

### 7.3 切り戻し手段（FreeRTOS ブートへ戻す）

`hal_warmstart.c:70` の `#define MIMAMORI_USE_MTKERNEL_BOOT (1)` を `0` にすると、橋渡し
（起動コンストラクタ）が `#if` で無効化され（`hal_warmstart.c:72/141` のコンパイル時分岐）、
従来の FreeRTOS 起動（`ra_gen/main.c` → `vTaskStartScheduler()`）に戻る。

> 重要（裏取り・誤解防止）: これは**コンパイル時分岐（`#if`）**であり、実行時の自動切替ではない。
> マクロを 0 にして**再ビルド**して初めて FreeRTOS ブートになる。さらに、各タスク本体
> （`*_thread_entry.c` の `*_task`）は `tk_*` の μT-Kernel API を直接呼ぶため、マクロ 0 で
> FreeRTOS ブートへ戻しても**リンクは通るが各タスクの実行には本体 API を FreeRTOS へ
> 差し戻す必要がある**（`lvgl_thread_entry.c:285-286` のラッパコメントと同趣旨）。
> よって `MIMAMORI_USE_MTKERNEL_BOOT=0` は「起動経路の切替」であって「完全な FreeRTOS 復帰」では
> ない点に注意。

### 7.4 今後の運用上の注意

- **FSP 再生成（Generate Project Content）後**: `ra_gen/` の FreeRTOS 生成コードが復活するが、
  方式A により実行されないため μT-Kernel 化のやり直しは原則不要。ただし手順書 6.2
  「再適用チェックリスト」（include path / マクロ / リンカ / 区画 / `config.h` 修正等）を確認する。
- **BSP2 再 vendoring 後**: `mtk3_bsp2/config/config.h`（`CNF_MAX_MTXID=16`, `CNF_SYSTEMAREA_END`,
  `CNF_TIMER_PERIOD` 等）、`config_func.h`（機能トリミング）、
  `include/sys/sysdepend/ra_fsp/ek_ra8p1/sysdef.h`（RA8P1 include 修正）の各カスタムが失われる。
  手順書 6.2 / 7.1 の `[mimamori-sense R-00x]` コメント箇所を再適用する。
- **将来の FreeRTOS 完全撤去**: 移行が安定し全タスクが μT-Kernel で動作することを十分確認した後、
  別 Issue で「FreeRTOS 設定を `configuration.xml` から外し、`ra_gen/` の FreeRTOS スレッド生成・
  ラッパ・`freertos_hooks.c` を削除する」クリーンアップを検討できる。R-008 時点では移行方針
  （設定維持）に従い**撤去しない**。撤去するとフラッシュ・RAM に余裕が出る（手順書 7.1 末尾
  「将来」）ため、撤去時に R-003 で行った CPU0/CPU1 区画再配分の見直しも併せて検討する。

---

## 8. LLVM ツールチェイン・ビルドの最終確認（受け入れ条件）

- ツールチェイン: LLVM Embedded Toolchain for Arm 21.1.1（手順書 2 章）。GNU ARM Embedded は不使用。
- ビルド構成: **Debug 構成**で検証する（Release 構成は FSP 生成ファイル `bsp_linker_info.h` 未生成のため
  従来からビルド不可。μT-Kernel/BSP2 とは無関係の既存事項。手順書 4.3 既知の制約）。
- 確認項目（ユーザー実機）:

| # | 項目 | 結果 |
|---|------|------|
| B-01 | CPU0 を LLVM・Debug でクリーンビルド（コンパイル＋リンク成功） | 未測定 |
| B-02 | コードフラッシュ・RAM がオーバーフローしない（R-003 の区画再配分 CPU0 992KB / CPU1 32KB 適用済み） | 未測定 |
| B-03 | 書き込み後 μT-Kernel 起動・全タスク動作（4. チェックリスト） | 未測定 |

---

## 9. R-008 受け入れ条件の対応状況

| 受け入れ条件（Issue #158） | 対応 | 状態 |
|---------------------------|------|------|
| 全機能が μT-Kernel 3.0 上で結合動作する | R-003〜R-007 で各機能を μT-Kernel 化。全タスクは `usermain.c` から生成・起動。**結合動作の実機確認は 4. チェックリスト**（ユーザー実施） | コード整備済 / 実機未測定 |
| 主要 KPI が測定・記録されている | **5. KPI 検証シート**を整備（測定方法・合格基準・記録欄）。KPI-02 は R-007 で 5ms 達成、KPI-01 は AI 並走時 20fps の懸念あり（→ 6. 調整方針） | シート整備済 / 実測はユーザー |
| 移行手順書が最終化されている | 手順書 8 章から本書を参照する形で最終化（→ 手順書側に追記） | 本 PR で対応 |
| LLVM ツールチェインでビルド・動作している | **8. ビルド最終確認**（ユーザー実機）。Debug 構成で検証 | 手順整備済 / 実機未測定 |

---

## 10. 参照

- 移行手順書（マスタ）: `doc/migration/mtk3-migration-guide.md`
- LVGL OSAL スパイク報告書: `doc/migration/r006a-lvgl-osal-spike.md`
- 要件・KPI 定義: `doc/product-requirements.md` 3.1
