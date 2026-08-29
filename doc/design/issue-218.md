# Issue #218 設計メモ — 全面白画面の切り分け計測

対象: `e2studio_CPU0/src/port/glcdc_port.{c,h}` / `e2studio_CPU0/src/port/lvgl_port_mtk3.c`
/ `e2studio_CPU0/src/diag_config.h`

## 0. このIssueの成果物の位置づけ

原因は未特定。**今回作るのは修正ではなく「切り分けを可能にする計測」**であり、
実機で採取した結果をもとに原因側の修正を別Issueで行う。
理由は §1 のとおり、**既存の計測が2つとも根拠として使えないことが判明した**ため。

## 1. 静的解析でわかったこと（Issueの切り分け表の再評価）

| # | 事実 | 根拠 | 帰結 |
|---|---|---|---|
| F1 | `s_swap_count` は Vsync 割り込みのたびに +1 されるだけで、実際のバッファ切替を数えていない | `glcdc_port.c:848,852,874`（`RM_LVGL_PORT_EVENT_DISPLAY_VPOS` 分岐内で `s_vsync_count++` と `s_swap_count++` を並べている） | `display dbuf` の Swap Count / Render FPS は **常に Vsync 数と同値**。「LVGL が flush を続けている」証拠にならない |
| F2 | `s_front_buffer_index` も Vsync ごとにトグルするだけ | `glcdc_port.c:875` / `glcdc_port.c:1180-1185` | `display dbuf` / `display fb` の "front/display" は**実際に GLCDC が読んでいるバッファではない**。Issueの「追跡値 FB[1] @ 0x6812C000」は根拠にならない |
| F3 | `R_GLCDC_BufferChange()` の戻り値は `FSP_ERR_INVALID_UPDATE_TIMING` 以外すべて捨てられている | `lvgl_port_mtk3.c:263-267`（`do{}while(INVALID_UPDATE_TIMING==error)` のみ、それ以外は無視） | バッファ切替が失敗し続けても誰も気づかない |
| F4 | GLCDC の背景色は黒（`bg_color = {a=255,r=0,g=0,b=0}`）、フェードは `DISPLAY_FADE_CONTROL_NONE` | `ra_gen/common_data.c` `g_display0_cfg.output.bg_color` / `.layer[0]` | 「レイヤ無効化で背景色が見えている」なら**黒**になるはず。白にはならない。アンダーフロー 0 と合わせ、**GLCDC はデータを読めていて、読んだ内容が白**か、**出力段/パネル**のどちらか |
| F5 | `camera display` の Update Count 増加が示すのは `lv_timer_handler()` が回っていることまで | `camera_display.c:212`（`lv_timer_create`）→ `camera_display_update()` 内で `s_update_count++`（:266） | 描画・flush の継続は別途 F1 を直さないと測れない |
| F6 | `flush_cb` の実行コンテキストは `lvgl_task` 1つだけ | `lv_refr.c:1373` `draw_buf_flush()` ← `lv_display_refr_timer()` ← `lv_timer_handler()` ← `lvgl_thread_entry.c:298` | 計測変数の書き手はタスク1本に閉じられる |
| F7 | `display test`（LVGL を経由せず FB へ直接描く）を常時ビルドに入れる増分は **3,072 B** | 実測: `.flash.endof` 0x020C5400 → 0x020C6000。`MIMAMORI_VERBOSE_DIAG=1` 全部入りでも 850,944 B（残 161 KiB） | Issue の「調査手段1」は**再ビルド不要にできる**（FLASH 残 203 KiB → 200 KiB / 992 KiB 中） |

## 2. 設計の入力（確定値・根拠付き）

| 項目 | 確定内容 | 根拠 |
|---|---|---|
| 呼び出し元 | 新規シェル `display reg` / `display fbstat` は `usrcmd_display()` の1箇所のみ（`ntshell_task`）。新規フック `glcdc_port_notify_flush()` は `lvgl_port_mtk3_flush_cb()` の1箇所のみ（`lvgl_task`）。**間接呼び出し**: `flush_cb` は `lv_display_set_flush_cb()` で登録され `lv_refr.c:1409` から呼ばれる。到達経路は `lv_timer_handler()` のみで **ISR 経路なし**（F6） | `lvgl_port_mtk3.c:162` / `lv_refr.c:1348,1373,1409` |
| 同時呼び出し | 書き手は `lvgl_task` 1本、読み手は `ntshell_task` 1本。既存の `s_vsync_count` / `s_underflow_count` の書き手は Vsync ISR のまま（別変数なので競合しない） | `glcdc_port.c:848` |
| ブロック許容時間 | `flush_cb` 内は**ブロック不可**（毎フレーム・Vsync 待ちの直前）。追加するのは volatile 変数への代入のみ。シェル側は `ntshell_task` なので数百 ms まで許容 | `lvgl_port_mtk3.c:256-272` |
| 依存先APIの最悪所要時間 | 新規に呼ぶブロックしうる API は**なし**。`display reg` は GLCDC レジスタ読み（ペリフェラルバス、待ちなし）。`display fbstat` は非キャッシュ SDRAM から 4,800 サンプル×2面 = 9,600 回の 16bit リード。SDRAM 実効 4 MB/s（`camera_display.c:16-21`）から 1 リード ≒ 0.5 µs と見積り **最悪 10 ms 未満**。ロックは一切取らない | `camera_display.c:16-21` |
| 失敗の返し方 | シェルは既存規約どおり `CMD_OK` / `CMD_ERR_INVALID_ARG`。GLCDC 未初期化時は `display status` と同じく「Not initialized」を出して `CMD_OK` | `glcdc_port.c:1234-1265` |
| 実行コンテキスト制約 | 新規コードは ISR から呼ばれない。FPU 不使用。I2C 不使用 | 上記 |

## 3. 状態変数と読み書きコンテキスト

| 変数 | 書き手 | 読み手 | 保護 |
|---|---|---|---|
| `s_flush_count`（実 flush 回数。F1 の是正） | `lvgl_task`（`glcdc_port_notify_flush()`） | `ntshell_task` | 不要（32bit `volatile` 単純ストア、書き手1本） |
| `s_last_flush_addr`（最後に `R_GLCDC_BufferChange()` へ渡したアドレス） | 同上 | 同上 | 同上 |
| `s_bufchange_err_count` / `s_bufchange_last_err`（F3 の是正） | 同上 | 同上 | 同上 |
| `s_vsync_count` / `s_underflow_count`（既存・変更なし） | Vsync ISR | `ntshell_task` | 既存どおり `volatile` |
| `s_swap_count` / `s_front_buffer_index`（既存） | Vsync ISR | `ntshell_task` | **削除**し、上記 `s_flush_count` / `s_last_flush_addr` に置き換える |

**個々の変数を独立に読むだけで、複数ワードの組で意味を持つ値は作らない**ため、
CLAUDE.md「並行性の既定形」が要求する publish/sample のクリティカルセクションは不要。
`display reg` / `display fbstat` は**読み取り専用**（GLCDC レジスタも FB も書かない）なので、
`lvgl_task` の描画と同時に走っても壊れない。サンプリング中に描画が進んで
面内の一貫性が崩れうる点は、コマンド出力に注記する。

## 4. 追加・変更するもの

1. **`display test` の常時ビルド化** — `glcdc_port.{c,h}` の `#if MIMAMORI_VERBOSE_DIAG` ガードを外し、
   `diag_config.h` の「除外されるもの」一覧から `display test` を削除。増分 3 KiB（F7）。
   Issue の調査手段1を、専用ビルドなしで実行できるようにする。
2. **flush 計測の是正**（F1 / F2 / F3） — `glcdc_port_notify_flush(const void *fb, fsp_err_t err)` を追加し、
   `lvgl_port_mtk3_flush_cb()` の `do{}while` 直後に呼ぶ。`display dbuf` は
   Vsync 数と**実 flush 数**を別々に表示し、front/back は追跡値ではなく
   `s_last_flush_addr` から表示する。BufferChange のエラー回数と最終エラー値も表示する。
3. **`display reg`**（Issue 調査手段3） — GLCDC ハードウェアレジスタの読み戻し。
   `GR[0]`: `VEN` / `FLMRD` / `FLM2` / `FLM3` / `FLM5` / `FLM6` / `AB1`(DISPSEL) / `MON`(UNDFLST)、
   `BG`: `EN` / `BGC` / `MON`、`OUT`: `VLATCH` / `SET` / `BRIGHT1` / `BRIGHT2` / `CONTRAST` / `CLKPHASE`、
   `SYSCNT`: `INTEN` / `STMON`。`FLM2` が `fb_background[0]`/`[1]` のどちらと一致するか（あるいは
   どちらでもないか）を判定して出力する。
3.1. **Issue #219 への対応** — `display fb` の注記は `s_last_flush_addr` を
   **1回だけサンプルして両行に使う**。行の間に `print_to_console()`（ミリ秒オーダー）が
   入るうえ、この値は 20 fps で fb0/fb1 を行き来するため、行ごとに読むと
   両方に注記が付く／どちらにも付かない ―― #219 が旧 `s_front_buffer_index` に対して
   報告しているのと同じ欠陥になる。

4. **`display fbstat`** — `fb_background[0]` と `[1]` の内容を 16 画素 × 8 ラインおきに
   サンプリング（4,800 点/面）し、`0xFFFF`（白）/ `0x0000`（黒）/ その他の画素数と、
   代表4点（ステータスバー左、ステータスバー右端＝時刻表示位置、カメラ中央、レターボックス余白）の
   生の画素値を表示する。「FB の中身が白か」を1コマンドで判定する。

## 4.1 実装後に確定した事実（セルフ検証ゲートでの訂正を含む）

- **FLASH 実測**: 本 PR 適用後 `.flash.endof` = 0x020C6E00 → **814,592 B / 1,015,808 B**
  （ベースライン 807,936 B から **+6,656 B**）。残り 201,216 B。
  内訳の目安は `display test` の常時ビルド化が +3,072 B、残りが `display reg` /
  `display fbstat` / flush 計装。
- **訂正（初版の誤り）**: `AB1.DISPSEL` を「0 = 透明」と書いて実装したが、実際は
  `glcdc_plane_blend_t`（`ra/fsp/src/r_glcdc/r_glcdc.c:158-162`）で
  **1 = 透明 / 2 = 不透明 / 3 = 下位レイヤに合成**であり、`R_GLCDC_BufferChange()` は
  非 NULL のフレームバッファに対して **3** を書く（`r_glcdc.c:666-678`）。
  0 はドライバが書かない値。`display reg` の判定をこの通りに直した。
- **色補正レジスタの期待値を確定**: 本プロジェクトは `GLCDC_CFG_COLOR_CORRECTION_ENABLE = false`
  （`ra_cfg/fsp_cfg/r_glcdc_cfg.h:9`）なので、`R_GLCDC_Open()` が
  `r_glcdc.c:407-414` の `#else` 側で固定値を1回だけ書き、以後 `R_GLCDC_ColorCorrection()` を
  呼ぶ箇所は無い（`src/` を grep して0件）。期待値は
  `BRIGHT1 = 0x00000200` / `BRIGHT2 = 0x02000200` / `CONTRAST = 0x00808080`。
  `display reg` はこれと一致しなければ `<<< UNEXPECTED` を付ける。
- **F4 の補強**: グラフィクスレイヤ2は無効（`GLCDC_CFG_LAYER_2_ENABLE = false`、
  `ra_gen/common_data.h:106`）。よって DISPSEL=1（透明）になった場合に見えるのは背景プレーン
  ＝ `BG.BGC`（黒）であり、**白にはならない**ことが確定した。
- **`Flush Count` の限界**: これは LVGL が切替を**要求**した回数であって、GLCDC が
  Vsync で**反映し終えた**回数ではない。要求直後に `flush_wait_cb` が次の Vsync を待つため、
  1秒窓での差は最大1フレーム。コード側にも同じ注記を入れた（`glcdc_port.c` の
  `glcdc_cmd_dbuf()` ヘッダコメント）。
- **既存ドキュメントとの整合**: `doc/migration/r008-integration-kpi.md` は
  「レンダ FPS は Vsync を数えているだけで使えない／恒久対応は別 Issue 推奨」と記載していた。
  本 PR がその恒久対応にあたるため、同節を `Flush Rate` ベースに更新した。

## 5. 捨てた代替案

- **`MIMAMORI_VERBOSE_DIAG` の既定値を 1 にする** — 全部入りで +42 KiB。`display test` だけなら 3 KiB で足りる（F7）。他の verbose 診断は本件の切り分けに不要。
- **`md` コマンドで FB を直接ダンプして目視判定** — 追加実装ゼロだが、1,024×600 のうち白が全面か一部かを人手で判定できない。§4-4 の集計が必要。
- **白画面を検出したら自動でログを出す監視タスク** — 「何が白か」を判定するロジック自体が未確定な段階で監視条件を作れない。症状は発生後も継続する（Issue「以後戻らない」）ので、発生後に手で叩くコマンドで足りる。
- **`R_GLCDC_BufferChange()` の失敗時に再初期化して復旧する** — 原因未特定のまま復旧処理を入れると、症状が隠れて切り分けができなくなる。まず観測する。
- **`display dbuf` の既存カウンタ名を維持したまま意味だけ直す** — F1/F2 を踏んだ過去の観測記録と突き合わせられなくなるため、名前を分けて両方出す。

## 6. 実機での切り分け手順（この実装後）

1. 白画面になったら `display reg` → `FLM2` が `fb_background[0]`/`[1]` のいずれかと一致するか、
   `BRIGHT1/2`・`CONTRAST` が既定値か、`AB1.DISPSEL` が有効かを見る。
2. `display dbuf` → **Flush Count が止まっているか**（LVGL が flush をやめたか）、
   BufferChange エラーが出ていないかを見る。
3. `display fbstat` → FB の中身が白か（`White(FFFF)` の割合を見る）。
   - **中身が白** → LVGL/Dave2D/SDRAM 側。
   - **中身は正常** → GLCDC 出力段またはパネル側。続けて `display test colorbar` で
     LVGL を経由せず FB を上書きし、出るかどうかを見る。
     `display test` は **fb_background[0] と [1] の両方**を書く（`glcdc_port.c` の
     `glcdc_cmd_test()`）ので、LVGL が flush を止めていて GLCDC がどちらのバッファを
     読んでいても模様が出る。LVGL が動いていればカメラ領域（毎秒20回 invalidate される）
     だけが上書きされて消えるので、**カメラ領域以外に模様が残るかどうか**を見ること。
4. Issue 調査手段4（発生までの時間・負荷依存・`camera stop` 状態での再現有無）は
   上記の結果とセットで記録する。
## 7. 実機採取結果（白画面状態、2026-08-29）

本計装を入れた最初の採取で、**白画面の状態のまま**次を得た。

### `display reg`

| レジスタ | 実測 | 期待値の根拠 | 判定 |
|---|---|---|---|
| `GR0.FLM2` | `0x68000000` | `fb_background[0]`。`last flush` と一致 | ✓ |
| `GR0.FLM3` | `0x08000000` | `line_offset << 16`（`r_glcdc.c:1591`）＝ 2048 B | ✓ |
| `GR0.FLM5` | `0x0257001F` | `((600-1)<<16) + (2048/64 - 1)`（`r_glcdc.c:1600`） | ✓ |
| `GR0.FLM6` | `0x00000000` | `g_format_lut[RGB565] << 28`。`GLCDC_INPUT_INTERFACE_FORMAT_RGB565 = 0`（`ra/fsp/inc/instances/r_glcdc.h:130`）なので **0 が正しい** | ✓ |
| `GR0.FLMRD` / `AB1` | `1` / `0x3` | 読み出し有効・DISPSEL=3（下位レイヤに合成） | ✓ |
| `GR0.MON` | `0` | レイヤ1 UNDFLST clear | ✓ |
| `BG.EN` / `BG.MON` | `0x00010001` | EN=1 / VEN=0 / SWRST=1（リセット解除） | ✓ |
| `BG.BGC` | `0` | 黒 | ✓ |
| `OUT.SET` | `0` | RGB888(`0<<12`)＋リトルエンディアン＋RGB順（`r_glcdc.c:1646-1665`。BIG/BGR のときだけビットが立つ） | ✓ |
| `BRIGHT1` / `BRIGHT2` / `CONTRAST` | `0x200` / `0x02000200` / `0x808080` | §4.1 の期待値 | ✓ |
| `CLKPHASE` | `0x00000178` | LCDEDGE / TCON0-3EDGE=1（`sync_edge = FALLING`） | ✓ |
| `SYSCNT.STMON` | `0x00000004` | **bit2 = L2UNDF**（グラフィクス2アンダーフロー）のラッチ | ⚠ 無害・下記 |

`SYSCNT.STMON` の L2UNDF は、レイヤ2が無効（`GLCDC_CFG_LAYER_2_ENABLE = false`）で
その割り込みベクタも生成されていない（`ra_gen/vector_data.h` に
`VECTOR_NUMBER_GLCDC_UNDERFLOW_2` が無く `underflow_2_irq = FSP_INVALID_VECTOR`）ため、
**誰もクリアせず誰も割り込まれない**フラグ。表示には影響しない。今後「発見」と誤認しないよう記録する。

### `display dbuf` / `display fbstat`

- `Vsync Rate 71 Hz` / **`Flush Rate 20 fps`** — 別々の値になった（旧計装なら両方 71）。
  **LVGL は白画面中も描画・flush を継続している**ことが初めて実証された。
  20 fps はカメラの invalidate レートと一致する。`BufChange Errors 0`。
- `fbstat`: 両バッファとも **`White(FFFF)` は 1%** のみ。Black 63%/43%、Other 35%/55%、Min≠Max。
  ステータスバーは両バッファとも `0x1905`、カメラ中央は `0x8C51` / `0x0000`。
  → **フレームバッファの中身は白ではなく、正常な画像が入っている**。

### `display test red`（同じ白画面状態で追加採取）

`display test red` は `glcdc_port_fill_color()` で **CPU から** `fb_background[0]` と `[1]` の
両方を全面 `RGB565_RED` で埋める（`glcdc_port.c` の `glcdc_cmd_test()`）。
結果は **パネルは白のまま変化なし**。

これで「フレームバッファの中身が何であってもパネルには届いていない」ことが確定した。
`fbstat` の census（＝CPU リードで見た中身）と、パネルの見え方が完全に切り離されている。

### 結論（消えた仮説と残った仮説）

白は「フレームバッファの中身」でも「GLCDC のレジスタ設定」でもなく、
**GLCDC がフレームバッファを読んだ *後* の経路**で発生している。
`display test red` で FB を全面赤にしてもパネルが変化しないことから、
これは「たまたま白いデータが表示されている」のではなく
**フレームバッファのデータがパネルに到達していない**状態である。

- **消えた仮説**: LVGL が白を描いている / GLCDC が別アドレスを読んでいる /
  レイヤの無効化・透明化 / 輝度・コントラスト補正の飽和 / ピクセル形式・ストライドの破壊 /
  SDRAM 帯域不足（アンダーフロー）。
- **残った仮説**（ソフトからは相互に切り分けられない3択）:
  1. GLCDC の SDRAM 読み出しパス（**CPU とは別のバスマスタ**。`md`/`fbstat` の CPU リードが
     正常でも、GLCDC 側だけが 0xFFFF を読んでいる可能性は残る。アンダーフロー 0 とも矛盾しない
     ―― 全1を高速に返すバスは FIFO を枯渇させない）
  2. GLCDC の出力段
  3. LCD パネル本体／基板上の信号経路

`display test` も GLCDC の読み出しパスを通るため 1 と 2/3 を分離できない。
分離にはロジアナ／オシロでの LCD データ線観測か、GLCDC 再初期化での復旧可否の確認が要る。
本 Issue の範囲（計装）はここまでで、以降は別 Issue とする。

## 8. 追加設計: `display blank on|off`（§7 の結果を受けた第2弾）

### 8.1 目的

§7 で残った3択のうち **① GLCDC の SDRAM 読み出しパス** と **②出力段 / ③パネル** を分ける。

`R_GLCDC_BufferChange(ctrl, NULL, layer)` は `AB1.DISPSEL = 1`（透明）と `FLMRD = 0`
（フレームバッファ読み出し停止）を書く（`ra/fsp/src/r_glcdc/r_glcdc.c:666-678`）。
このときパネルに出るのは背景プレーンの `BG.BGC` ＝ 黒（`ra_gen/common_data.c` の
`bg_color = {a=255,r=0,g=0,b=0}`。グラフィクスレイヤ2は無効なので下位レイヤ＝背景プレーン）。
**この経路は SDRAM を一切読まない。**

- **黒くなる** → GLCDC 出力段とパネルは健全 → **①が原因**
- **白のまま** → **②か③**。ソフトでの切り分けはここで終わり

### 8.2 設計の入力（確定値・根拠付き）

| 項目 | 確定内容 | 根拠 |
|---|---|---|
| 呼び出し元 | `display blank` は `usrcmd_display()` の1箇所（`ntshell_task`）。`glcdc_port_blank_requested()` は `lvgl_port_mtk3_flush_cb()` の1箇所（`lvgl_task`）。**間接呼び出し**は §2 と同じ（`flush_cb` ← `lv_refr.c:1409` ← `lv_timer_handler()`）で **ISR 経路なし** | §2 / F6 |
| 同時呼び出し | `s_blank_desired` は書き手 `ntshell_task` 1本・読み手 `lvgl_task` 1本。`s_blank_applied` はその逆。どちらも単一の 32bit `volatile` | 8.3 |
| ブロック許容時間 | `flush_cb` 内は**ブロック不可**。追加するのは `volatile` の読み1回だけで、`R_GLCDC_BufferChange()` の**呼び出し回数は変わらない**（引数が NULL になるだけ）→ **flush 経路の所要時間の増分ゼロ** | 8.4 |
| 依存先APIの最悪所要時間 | コマンド側で反映待ちに `tk_dly_tsk(10)` を最大50回＝**500 ms**。他にブロックしうる API は呼ばない | 8.5 |
| 失敗の返し方 | 引数不正 → `CMD_ERR_INVALID_ARG`。GLCDC 未初期化 → メッセージ＋`CMD_ERR_EXECUTE`。反映待ちタイムアウト → 「LVGL が flush していない」と明示して `CMD_OK`（宣言自体は成立しており、次の flush で反映されるため） | 8.5 |
| 実行コンテキスト制約 | ISR から呼ばれない。FPU / I2C 不使用 | 上記 |

### 8.3 排他方針 — 宣言 + 所有タスクの reconcile（CLAUDE.md「並行性の既定形」）

**`display blank` から直接 `R_GLCDC_BufferChange()` を呼んではいけない。**
`flush_cb` が毎秒20回同じ関数を呼んでいるため、呼び出し元が2タスクになると
同関数内の `AB1` → `FLMRD` → `FLM2` → `VEN` の4つの書き込み（`r_glcdc.c:677-685`）が
インターリーブしうる。たとえば

```
ntshell: AB1=1(透明), FLMRD=0
lvgl   :                      AB1=3(可視), FLMRD=1, FLM2=fb
ntshell:                                                     FLM2=0, PVEN=1
```

の順で走ると **「レイヤ可視 ＋ FLM2=0」** がラッチされ、GLCDC が 0 番地から
1.2 MB を読み続ける。切り分けのための計測が新しい故障を作ることになる。

したがって **`R_GLCDC_BufferChange()` を `flush_cb` と並行して呼ぶ経路を作らない**。
（`lvgl_port_mtk3_open()` も初期化時に1回呼ぶが `lvgl_port_mtk3.c:144`、
`glcdc_cmd_blank()` は `s_glcdc_status != GLCDC_STATUS_INITIALIZED` の間は受け付けず、
このフラグは `lvgl_port_mtk3_open()` から戻ったあとに立つ（`glcdc_port_init()`）ので、
両者が重なる窓は存在しない。）
`display blank` は「あるべき状態」を宣言するだけで、所有タスク（`lvgl_task`）が
毎フレーム差分を reconcile する。

| 変数 | 書き手 | 読み手 | 保護 |
|---|---|---|---|
| `s_blank_desired` (uint32) | `ntshell_task`（`display blank`） | `lvgl_task`（`glcdc_port_blank_requested()`） | 不要（単一ワード・単一書き手） |
| `s_blank_applied` (uint32) | `lvgl_task`（`glcdc_port_notify_flush()`） | `ntshell_task`（反映待ち） | 同上 |

- **組で意味を持つ複数ワードを作らない**ので、publish/sample のクリティカルセクションは不要。
- **シーケンス番号・チケットも不要**。reconcile は冪等（毎フレーム同じ判断をしてよい）で、
  「最後の宣言が勝つ」が正しい意味論。CLAUDE.md が「不要になるのは順序付け」と言っている形。
- 反映は最大1フレーム遅れる。許容。

### 8.4 flush_cb の変更（これだけ）

```c
uint8_t *p_target = glcdc_port_blank_requested() ? NULL : p_px_map;
do {
    error = R_GLCDC_BufferChange(ctrl, p_target, layer);
} while (FSP_ERR_INVALID_UPDATE_TIMING == error);
glcdc_port_notify_flush(p_target, (int32_t)error);
```

`notify_flush` の引数が NULL かどうかで `s_blank_applied` を更新するので、**シグネチャは変えない**。
副次的に `display dbuf` の `Last flushed by LVGL` がブランク中は `0x00000000` になり、
状態が目で見える。

### 8.5 コマンドの挙動

- `display blank on` / `off` で `s_blank_desired` を書き、`s_blank_applied` が一致するまで
  10 ms 間隔・最大 500 ms 待って結果を報告する。
- **反映は LVGL が次にフレームを描いたときに起きる**。カメラ動作中は 20 fps なので 50 ms 以内。
  カメラ停止中など invalidate が無いと flush が起きず反映されないため、
  タイムアウト時は「LVGL が flush していない」と明示する（これ自体が有用な情報）。
- `on` のままだと画面が黒いままになるので、出力に `off` で戻す旨を明記する。

### 8.6 レビュー指摘への対応（PR #220 P2 / 2026-08-29）

> `R_GLCDC_BufferChange()` がリトライ対象外のエラーを返した場合でも、
> `s_blank_applied` を `p_framebuffer` だけから更新しているため、
> `display blank` が「適用済み」と誤報する。

**妥当な指摘。修正した。** `glcdc_port_notify_flush()` は
**`err == FSP_SUCCESS` のときだけ** `s_blank_applied` を更新する。

ただし到達可能性は正確に記録しておく: **現構成では起きない**。
`GLCDC_CFG_PARAM_CHECKING_ENABLE` は `BSP_CFG_PARAM_CHECKING_ENABLE`
（`ra_cfg/fsp_cfg/bsp/bsp_cfg.h:33` で **0**）に従うので、
`R_GLCDC_BufferChange()` が返しうる非成功値は
`FSP_ERR_INVALID_UPDATE_TIMING` だけであり（`r_glcdc.c:650-660`）、
これは `flush_cb` のリトライループが消費する。
指摘が挙げた `FSP_ERR_INVALID_MODE` はパラメータチェック有効時のみ返る。
それでも直す理由は、**パラメータチェックを有効にするのは、まさにこの診断を使って
デバッグしている場面だから**。

**ナイーブに直すと別の誤報が出る点にも対処した。** `s_blank_applied` を成功時のみ
更新するだけだと、失敗時にコマンドはタイムアウトし
「LVGL has not flushed」と表示してしまう ―― LVGL は flush しており、
失敗したのは BufferChange なので、これは事実に反する。
待機前に `s_bufchange_err_count` をサンプルしておき、タイムアウト時に
2つの原因を区別して報告する:

| 状況 | 出力 | 戻り値 |
|---|---|---|
| 適用された | `applied by LVGL after N ms` | `CMD_OK` |
| BufferChange が失敗した | `NOT applied: R_GLCDC_BufferChange() failed (err)` | `CMD_ERR_EXECUTE` |
| LVGL が flush していない | `LVGL has not flushed within 500 ms` | `CMD_OK`（宣言は成立） |

### 8.7 捨てた代替案

- **`mw` で `AB1` を直接叩く** — `flush_cb` が 50 ms で `DISPSEL=3` に戻す。加えて 8.3 の
  レジスタ書き込み競合をそのまま踏む。
- **`display blank` から直接 `R_GLCDC_BufferChange()` を呼ぶ** — 8.3 のとおり
  「レイヤ可視 ＋ FLM2=0」をラッチしうる。
- **`camera stop` してから `mw`** — 競合は減るが、ステータスバーの1分更新で戻る。手順も煩雑。
- **`R_GLCDC_Stop()` / `Close()` で消す** — 出力タイミングごと止まるので、
  「背景プレーンは出るか」という問いに答えられない（黒くなっても意味が違う）。

## 9. `display blank` の実機結果（白画面状態、2026-08-29）

```
display blank on   -> Blank ON, applied by LVGL after 20 ms.
```

**パネルは白のまま変化なし。** 同じ状態で `display reg` を採取し、ブランクが
ハードウェアに効いていることを確認した（「効かなかっただけ」という解釈を排除するため）。

| レジスタ | ブランク前 | ブランク中 | 意味 |
|---|---|---|---|
| `GR0.FLMRD` | `0x1` | **`0x0`** | フレームバッファ読み出し **停止** |
| `GR0.AB1` | `0x3` | **`0x1`** | DISPSEL=1 ＝ グラフィクスプレーン **透明** |
| `GR0.FLM2` | `0x68000000` | **`0x0`** | ―― |
| `BG.BGC` / `BG.MON` | `0x0` / `0x00010001` | 同じ | 背景プレーンは動作中・色は黒 |
| `OUT.*` | 期待値 | 同じ | 出力ブロックの設定に変化なし |

**したがって、GLCDC が SDRAM を一切読まずに内部生成した黒を出しているのに、
パネルは白のままである。** SDRAM 読み出しパス（§7 の残り候補①）も消えた。

### 副作用（実機で観測。故障ではない）

ブランク中は `GR0.MON` が `0x00010000`（**UNDFLST セット**）になった。
`FLMRD = 0` でグラフィクス FIFO への供給を止めたまま、プレーンのパイプライン自体は
走り続けるため、**毎フレーム レイヤ1 アンダーフローが起きるのは当然**。
`SYSCNT.STMON` の L1UNDF が立っていないのは、`glcdc_underflow_1_isr()` が
`STCLR.L1UNDFCLR` を書いてクリアしているから（`r_glcdc.c:1953`）。
その ISR は `s_underflow_count` を進めるので、**ブランクを使ったあとの
`display dbuf` の Underflow Count は水増しされている**。
これを後日「発見」として追わないよう、`display blank on` の出力とヘッダに明記した。

### 残った候補

| # | 候補 | 状態 |
|---|---|---|
| ① | GLCDC の SDRAM 読み出しパス | **消えた**（SDRAM を読まない状態でも白） |
| ② | GLCDC 出力段（TCON のピン出力タイミングを含む） | **残る** |
| ③ | LCD パネル本体／基板の信号経路 | **残る** |

## 10. 追加設計: `display reg` に未読レジスタを追加（ソフトで読める最後の領域）

### 10.1 なぜ TCON か

`GLCDC_TCON_PIN_2` に DE を割り当てている（`ra_gen/common_data.c` の
`g_display0_extend_cfg.tcon_de = GLCDC_TCON_PIN_2`）。FSP は TCON ピンごとの
信号選択を `g_tcon_lut[] = { &STVA2, &STVB2, &STHA2, &STHB2 }`（`r_glcdc.c:283-286`）で
書くので、**DE の割り当ては `TCON.STHA2.SEL`** に入る。

ここが壊れると **DE が止まってパネルが有効データ区間を失う**（多くの TFT で全面白になる）
一方、ライン検出割り込みは GLCDC 内部カウンタ由来なので **71 Hz のまま出続ける**。
現在の観測（Vsync 生存・パネル白・FB もレジスタも正常）と矛盾しない、
**まだ潰せていない唯一のソフト側候補**である。

同様に `SYSCNT.PANEL_CLK` は `CLKEN`（パネルクロック出力イネーブル）・`DCDR`（分周比）・
`PIXSEL` を持つが未読。`OUT.PDTHA`（ディザ）と `GAM`（ガンマ）も未読。

### 10.2 変更内容

`glcdc_cmd_reg()` に読み出しを追加するだけ。**新しい状態変数も排他も無く、
既存の排他方針は一切変わらない**（すべて読み取り専用のレジスタアクセス）。

- `TCON.TIM` / `STVA1` / `STVA2` / `STVB1` / `STVB2` / `STHA1` / `STHA2` / `STHB1` / `STHB2` / `DE`
  （`STHA2` は DE 割り当てなので `SEL` フィールドを分解して表示する）
- `SYSCNT.PANEL_CLK`（`DCDR` / `CLKEN` / `CLKSEL` / `PIXSEL` を分解）
- `OUT.PDTHA`

これで **ソフトから読めるものは全部読んだ**状態になり、以降はロジアナ／オシロで
LCD_TCON2(DE) / HSYNC / VSYNC / PCLK / データ線を観測する話に引き渡せる。

### 10.3 実装後に確定した期待値

`tcon_hsync` / `tcon_vsync` はどちらも `GLCDC_TCON_PIN_NONE` なので、`R_GLCDC_Open()` は
`r_glcdc_data_enable_set()` だけを呼ぶ（`r_glcdc.c:1286-1302`）。したがって書かれるのは
次の4つだけで、残り（`STVA1` / `STVA2` / `STVB2` / `STHA1` / `STHB2`）は**一度も書かれず
リセット値のまま**。よって期待値を主張するのはこの4つに限る。

| レジスタ | 期待値 | 根拠 |
|---|---|---|
| `TCON.STHA2.SEL` | **7**（DE） | `tcon_de = GLCDC_TCON_PIN_2`、`g_tcon_lut[2] = &STHA2`（`r_glcdc.c:283-286,1331`）。`GLCDC_TCON_PIN_0..3` は 0..3（`inc/instances/r_glcdc.h:71-75`） |
| `TCON.STHB1` | **0x00A00400** | `(h back_porch 160 << 16) | h display 1024`（`r_glcdc.c:1375-1376`、マスクは 0x7FF なので桁落ちなし） |
| `TCON.STVB1` | **0x00170258** | `(v back_porch 23 << 16) | v display 600`（`r_glcdc.c:1377-1378`） |
| `TCON.DE` | **0** | `data_enable_polarity = HIACTIVE`。`LOACTIVE` のときだけ 1 が書かれる（`r_glcdc.c:1317-1325`） |

`SYSCNT.PANEL_CLK` は `CLKEN=0`（パネルクロック出力停止）のときだけ警告を出す。
`DCDR` / `CLKSEL` / `PIXSEL` / `VER` は分解表示のみ（期待値は主張しない）。

**FLASH**: 本追加で +1,536 B。Issue #218 全体で main から **+9,728 B**
（817,664 / 1,015,808 B、残り 198,144 B）。

### 10.4 捨てた代替案

- **`mw` で個別に読む** — オフセット計算を毎回手で行うことになり、
  `SEL` などのビットフィールド分解も人手。白画面の再現待ちの最中に間違えたくない。
- **TCON を書き換えて復旧を試す** — 原因未特定のまま復旧を入れない方針（§5）を維持する。
  まず読む。

## 11. TCON / パネルクロック採取結果（白画面状態、2026-08-29）

白画面を再現させて `display reg` を採取。**追加した項目もすべて期待値だった。**

| レジスタ | 実測 | 期待値 | 判定 |
|---|---|---|---|
| `TCON.STHA2` | `0x00000007` | SEL=7（DE）→ LCD_TCON2 が DE を出力 | ✓ |
| `TCON.STHB1` | `0x00A00400` | `(160<<16) \| 1024` | ✓ |
| `TCON.STVB1` | `0x00170258` | `(23<<16) \| 600` | ✓ |
| `TCON.DE` | `0x00000000` | HIACTIVE | ✓ |
| `TCON.TIM` / `STVA1` / `STVA2` / `STVB2` / `STHA1` / `STHB2` | `0` | 未書き込み（リセット値） | ✓ |
| `SYSCNT.PANEL_CLK` | `0x01100144` | CLKEN=1 / DCDR=4 / CLKSEL=1 / PIXSEL=0 | ✓ |
| `OUT.PDTHA` | `0` | ディザ無効 | ✓ |

`PANEL_CLK` の各フィールドは `r_glcdc.c:1243-1252` と config の突き合わせで確認した:
`clock_div_ratio = GLCDC_PANEL_CLK_DIVISOR_4 = 4` → DCDR=4、
`clksrc = GLCDC_CLK_SRC_INTERNAL` → CLKSEL=1、CLKEN は常に 1、
`pixsel` は **`!=`** の条件（`r_glcdc.c:1244-1246`）なので RGB888 では **0 が正しい**
（`PIXSEL` は `OUT_SET.FRQSEL[1]` と一致させる必要があり、`OUT.SET = 0` とも整合）。

なお `VEN = 0x00000001` は採取時にバッファ切替がラッチ待ちだっただけで、
通常描画中に見える値（前回採取時は `0`）。

### 到達点

**GLCDC が持つ設定・状態のうち、ソフトから読めるものはすべて正常。**
フレームバッファの中身も正常。グラフィクスプレーンを完全に切り離しても白。
それでもパネルは白である。

残るソフト側の未確認領域は **ピン機能設定（PFS）** のみ。
GLCDC は 30 本のピンを使う（`ra_gen/pin_data.c` の `IOPORT_PERIPHERAL_LCD_GRAPHICS`:
P207 / P513 / P515 / P707 / P710-715 / P805-807 / P902-904 / P910-915 / P1100-1107）。
このうち **PCLK・DE・同期のいずれか1本でも周辺機能から外れれば、
パネルは全面白になりうる**（データ線1本なら色化けで済む）。

`src/` 内で IOPORT に書き込むのは `led_ctrl.c:121` と `common_util.h:36-41` のマクロ
（LED: P600 / P414 / P107 または P600 / P303 / P1007）と、`glcdc_port.c` の
DISP_BLEN(P514) / DISP_RESET(P606) だけで、**上記30本とは重複しない**。
それでも PFS が変化していないことは読み戻さない限り確定できない。

## 12. 追加設計・実装: `display pins`（ソフトで読める最後の層）

### 12.1 目的

§11 で GLCDC のレジスタが全項目正常と分かったため、残るソフト側の未確認領域である
**ピン機能設定（PmnPFS）** を読み戻す。ピンが周辺機能から外れていれば、
**GLCDC は完全に健全に見えたまま、信号がパネルに届かない** ―― まさに現在の状態になる。

**1本で全面白になりうるのは `DISP_CLK` / `PARLCD_DE` / `PARLCD_HSYNC` / `PARLCD_VSYNC` のみ。**
データ線1本なら色化けで済むので、症状に合うのはこの4本である。

### 12.2 設計の入力

| 項目 | 確定内容 | 根拠 |
|---|---|---|
| 呼び出し元 | `usrcmd_display()` の1箇所（`ntshell_task`）のみ | ― |
| 同時呼び出し | **状態変数を持たない**。既存の排他方針に影響なし | ― |
| ブロック許容時間 | ブロックしうる API を呼ばない（レジスタ読み30回＋出力） | ― |
| 依存先APIの最悪所要時間 | `R_PFS->PORT[].PIN[].PmnPFS` の読み出しのみ。待ちなし。**PmnPFS の読み出しに PWPR のアンロックは不要**（書き込み時のみ必要） | `bsp_io.h:363` |
| 失敗の返し方 | 常に `CMD_OK`。判定結果は出力に `<<< NOT LCD_GRAPHICS` として出す | ― |
| 実行コンテキスト制約 | ISR から呼ばれない。**書き込みは一切行わない** | ― |

### 12.3 期待値と判定

`ra_gen/pin_data.c` の `IOPORT_PERIPHERAL_LCD_GRAPHICS` エントリは **30本**あり、
**全て** `IOPORT_CFG_DRIVE_HIGH | IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_LCD_GRAPHICS`
（`grep -c` で30件、`IOPORT_CFG_*` の内訳も各30件と確認）。
FSP が書く IOPORT 設定語は **PmnPFS レジスタ値そのもの**（`bsp_io.h:386`）なので、期待ビットは:

- `PSEL[28:24] = 0x19`（`IOPORT_PERIPHERAL_LCD_GRAPHICS`、`r_ioport.h:428`、
  `IOPORT_PRV_PFS_PSEL_OFFSET = 24` は `r_ioport.h:33`）
- `PMR[16] = 1`（`IOPORT_CFG_PERIPHERAL_PIN = 0x00010000`、`r_ioport.h:481`）

**判定はこの2つだけに限る。** `PIDR` は入力レベルを反映して勝手に変わるし、
駆動能力（`DSCR`、`IOPORT_CFG_DRIVE_HIGH = 0x00000C00`）は信号品質の話であって
「GLCDC に繋がっているか」ではない。生値は表示するが判定には使わない。

対象30本と基板ネット名（`ra_cfg/fsp_cfg/bsp/bsp_pin_cfg.h`）:

| 用途 | ピン |
|---|---|
| クロック・同期・DE | `DISP_CLK`(P515) / `PARLCD_DE`(P807) / `PARLCD_HSYNC`(P805) / `PARLCD_VSYNC`(P806) / `DISP_TCON3`(P513) / `PARLCD_EXTCLK`(P710) |
| データ R | `PARLCD_D16R0`(P1102) / `D17R1`(P1100) / `D18R2`(P707) / `D19R3`(P711) / `D20R4`(P712) / `D21R5`(P713) / `D22R6`(P714) / `D23R7`(P715) |
| データ G | `PARLCD_D8G0`(P904) / `D9G1`(P207) / `D10G2`(P1107) / `D11G3`(P1106) / `D12G4`(P1105) / `D13G5`(P1101) / `D14G6`(P1104) / `D15G7`(P1103) |
| データ B | `PARLCD_D0B0`(P914) / `D1B1`(P915) / `D2B2`(P903) / `D3B3`(P902) / `D4B4`(P910) / `D5B5`(P911) / `D6B6`(P912) / `D7B7`(P913) |

**FLASH**: 本追加で +1,024 B。Issue #218 全体で main から **+10,752 B**
（818,688 / 1,015,808 B、残り 197,120 B）。

### 12.4 捨てた代替案

- **`mr` で PFS を1本ずつ読む** — 30本分のアドレス計算とビットフィールド分解を人手で行うことになる。
  白画面の再現待ちの最中にやりたくない。
- **`R_IOPORT_PinCfgGet()` を使う** — FSP の IOPORT API に**読み出し関数は存在しない**
  （`inc/api/r_ioport_api.h` の関数ポインタ一覧に `pinCfgGet` は無い）。PmnPFS を直接読むしかない。
- **ピンを再設定して復旧を試す** — 原因未特定のまま復旧を入れない方針（§5）を維持する。まず読む。

## 13. `display pins` の実機結果と症状の進行（2026-08-29）

### 症状が進行した

Issue 記載時は「電源 ON 後 **数分〜20 分**で白」だったが、この時点で
**毎回、電源 ON 直後から白**になるようになった（ユーザー確認）。
ソフトウェアの状態は電源断で必ず消えるので、**この進行の仕方はソフト起因では説明できない**。

### `display pins`（電源 ON 直後・白画面）

**30 / 30 すべて `PSEL=0x19`（LCD_GRAPHICS）・`PMR=1`。**
`R_IOPORT_Open(g_bsp_pin_cfg)` が起動時に書いた設定がそのまま保持されている。

生値に `0x19010C00` と `0x19010C02` の 2 種類があるのは **bit1 = PIDR**
（読んだ瞬間のピン入力レベル）の差で、変動して当然のフィールド。
§12.3 のとおり判定からは除外している。

### ソフト側の切り分け完了

| 層 | 結果 |
|---|---|
| フレームバッファの中身 | 正常な画像（White 1%） |
| GLCDC レイヤ設定（アドレス・形式・ストライド・DISPSEL） | 期待値 |
| 色補正（BRIGHT1/2・CONTRAST・PDTHA） | 期待値（中立） |
| 出力ブロック（OUT.SET・CLKPHASE） | 期待値 |
| TCON（DE 割り当て・DE ウィンドウ・極性） | 期待値 |
| パネルクロック（CLKEN/DCDR/CLKSEL/PIXSEL） | 期待値 |
| ピン機能（30本の PmnPFS） | **30/30 LCD_GRAPHICS** |
| グラフィクスプレーン遮断（SDRAM 非アクセス） | **それでも白** |
| LVGL / GLCDC の動作 | Flush 20fps / Vsync 71Hz / アンダーフローなし |

**ソフトウェアが設定・観測できる範囲に異常はない。原因はハードウェア側**
（パラレルグラフィクス拡張ボード、そのコネクタ／フレキ、またはパネル本体）。

### ハードウェア確認の優先順

1. **拡張ボードのコネクタ・フレキを挿し直す**
   （「時間経過で悪化 → 恒久化」という進行は接触不良の典型）
2. 拡張ボードの電源電圧、パネルのバックライト以外の電源系
3. コネクタ部の目視（曲がり・浮き・断線）
4. ロジアナ／オシロで DE・PCLK・HSYNC・VSYNC・データ線

過去に EK-RA8P1 で J41 の片組開放（UM の記載と実物が違う）を踏んでいるので、
ロジアナを出す前に物理接続を疑う価値が高い。

## 14. 追加実装: `display signals`（ロジアナを当てる線を決める）

### 14.1 目的

`PmnPFS.PIDR`（bit1）は**ピンの実際の信号レベル**なので、これを 30 本まとめて
数フレーム分サンプリングすれば、**駆動されていない（トグルしない）線を特定できる**。

- `DISP_CLK` が固着 → ピクセルクロックが出ていない
- `PARLCD_DE` が固着 → DE が出ていない
- データ線が固着 → ピクセルデータが出ていない
- **全部トグル** → MCU 側は正しく駆動している。故障はピンより先（コネクタ／フレキ／
  拡張ボード／パネル）と確定し、§13 の 1〜3 に直行できる

どちらに転んでも**ロジアナで最初に当てる線が決まる**。

### 14.2 設計の入力

| 項目 | 確定内容 |
|---|---|
| 呼び出し元 | `usrcmd_display()` の1箇所（`ntshell_task`）のみ |
| 同時呼び出し | **状態変数を持たない**。既存の排他方針に影響なし |
| ブロック許容時間 | `tk_dly_tsk(10)` × 10 = **約 100 ms**。`ntshell_task` なので許容 |
| 依存先APIの最悪所要時間 | `PmnPFS` の読み出しと `tk_dly_tsk` のみ。待ちは上記の 100 ms だけ |
| 失敗の返し方 | GLCDC 未初期化ならメッセージのみ。それ以外は常に `CMD_OK` |
| 実行コンテキスト制約 | ISR から呼ばれない。**書き込みは一切行わない** |

### 14.3 サンプリング設計

`GLCDC_SIGNALS_ROUNDS`(10) × `GLCDC_SIGNALS_BURST`(200) = **2,000 サンプル/ピン**を、
`GLCDC_SIGNALS_GAP_MS`(10 ms) 間隔のバーストに分けて採る。

- **ギャップ 10 ms はフレーム周期 ~14.08 ms（71 Hz）の約数ではない。**
  したがってバーストごとにフレーム内の位相がずれ、同じラインを再サンプルし続けない。
- レベルを読むだけなので**周波数は分からない**。ピクセルクロックはこのループより遥かに速く、
  サンプルは実質ランダム位相になる。それで十分 ―― 問うているのは
  「一度でも変化するか」だけである。
- カウンタは `uint16_t`（最大 2,000）。`ntshell_task` のスタックは 4096 B
  （`usermain.c` の `stksz`）なので、30要素×2本を 240 B ではなく **120 B** に抑える。

### 14.4 出力を読むときの注意（コマンド自身も表示する）

- **データ線は、走査中の絵がそのビットを一定に保っていれば正当に固着する。**
  先に `display test checker1`（1画素市松）を実行すると、全データ線が
  ピクセルクロックで強制的にトグルするので「固着」に意味が出る。
- `PARLCD_HSYNC` / `PARLCD_VSYNC` には期待値を置かない。本プロジェクトは
  `tcon_hsync` / `tcon_vsync` が `GLCDC_TCON_PIN_NONE` で **DE しか駆動していない**ため
  （§11）、これらのピンは TCON レジスタの初期値のまま。

**FLASH**: 本追加（判定対象の絞り込みを含む）で +1,536 B。Issue #218 全体で main から
**+12,288 B**（820,224 / 1,015,808 B、残り 195,584 B）。

### 14.5 捨てた代替案

- **1ピンずつ長時間サンプリング** — 30本×数フレームで数秒かかるうえ、
  ピン間で観測窓が違うので比較にならない。全ピンを同一窓で観測する。
- **エッジ数を数えて周波数を出す** — CPU ポーリングではピクセルクロックに追いつけず、
  意味のある数字にならない。「変化したか」だけに絞る。

## 15. `display signals` の実機結果 — 最終結論（2026-08-29）

`display test checker1`（1画素市松）を表示させたうえで、白画面状態で採取。

### MCU は正しい映像信号を出している

| 信号 | 実測 | 設定から計算した期待値 | 判定 |
|---|---|---|---|
| `DISP_CLK`(P515) ピクセルクロック | **49.6% high**（lo=1008 / hi=992） | クロックなので 50% | ✓ |
| `PARLCD_DE`(P807) | **74.4% high**（lo=512 / hi=1488） | `(1024/1344) × (600/635)` = **72.0%** | ✓ |
| データ線 24 本（checker1 表示中） | **平均 35.6%**（27.7〜43.0%、n=24） | 有効区間 × 50% = **36.0%** | ✓ |

「トグルしている」だけでなく、**デューティ比が設定した映像タイミングと表示中のパターンに
定量的に一致する**。MCU は 1024×600 の正しい映像信号をピンまで出している。

DE のデューティが 72% と一致したことで、**`PARLCD_DE`(P807) = LCD_TCON2**
（`tcon_de = GLCDC_TCON_PIN_2` の出力先）であることも実測で裏付けられた。

### STUCK の 4 本はこの構成が駆動していないピン

| ピン | 実測 | 駆動されない理由 |
|---|---|---|
| `PARLCD_HSYNC`(P805) = LCD_TCON0 | STUCK LOW | `tcon_hsync = GLCDC_TCON_PIN_NONE`（DE-only 構成。`r_glcdc.c:1286-1294`） |
| `PARLCD_VSYNC`(P806) = LCD_TCON1 | STUCK LOW | `tcon_vsync = GLCDC_TCON_PIN_NONE` |
| `DISP_TCON3`(P513) = LCD_TCON3 | STUCK LOW | `tcon_de = GLCDC_TCON_PIN_2` なので TCON3 には何も割り当たらない |
| `PARLCD_EXTCLK`(P710) | STUCK HIGH | `clksrc = GLCDC_CLK_SRC_INTERNAL`。外部クロック入力は未使用 |

**コマンド側の不備を修正**: 初版はこの4本も「STUCK ＝ ここにプローブを当てろ」と
報告していた。`g_glcdc_pins[]` に `driven` フラグを追加し、
**この構成が駆動するピンだけを判定対象**にした（26本）。
駆動しないピンは `idle (not driven in this config)` と表示する。

### 最終結論

**ソフトウェアが設定・観測できる範囲に異常はなく、MCU のピンには正しい映像信号が出ている。
それでもパネルは白。**

したがって故障箇所は **MCU のピンより先** ―― パラレルグラフィクス拡張ボードの
コネクタ／フレキ、拡張ボード自体、またはパネル本体。

さらに症状が「電源 ON 後 数分〜20 分で白」から「**毎回、電源 ON 直後から白**」へ
進行しており（§13）、ソフトの状態は電源断で必ず消えることと合わせて、
**物理的な劣化・接触不良**の進行として整合する。

### 次のアクション（本 Issue の範囲外・別 Issue へ）

1. **拡張ボードのコネクタ・フレキを挿し直す**（最優先。上記の進行の仕方に最も合う）
2. 拡張ボードの電源電圧、パネルのバックライト以外の電源系を確認
3. コネクタ部の目視（曲がり・浮き・断線）
4. それでも直らなければ、ロジアナ／オシロを **コネクタのパネル側**に当て、
   MCU ピンで確認済みの PCLK・DE・データ線がパネルまで届いているかを見る

過去に EK-RA8P1 で J41 の片組開放（UM の記載と実物が違う）を踏んでいるので、
基板側の物理確認から入るのが妥当。
