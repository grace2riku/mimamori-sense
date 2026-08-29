# Issue #222 設計メモ — `display pins` に GPIO 制御 2 本の読み戻しを追加

## 1. 設計の入力（Issue 記載を裏取りした結果）

| 項目 | 確定内容 | 根拠 |
|---|---|---|
| 呼び出し元 | `glcdc_cmd_pins()` の 1 箇所のみ。`usrcmd_display()` の `"pins"` 分岐から呼ばれる（`ntshell_task` 実行）。関数ポインタ登録なし（`grep glcdc_cmd_pins` の一致は宣言・定義・この 1 呼び出しのみ） | `glcdc_port.c:273,1380,2323` |
| 同時呼び出し | 状態変数を新設しないため排他不要。読むのは MCU レジスタのみ | ― |
| ブロック許容時間 | ブロックしうる API を呼ばない | ― |
| 依存先 API の最悪所要時間 | `R_PFS->PORT[].PIN[].PmnPFS` の読みのみ、待ちゼロ。**読み出しに PWPR アンロックは不要**（`R_BSP_PinRead()` が unlock なしで同レジスタを読んでいる。Issue 本文の `bsp_io.h:363` は `R_BSP_PinWrite()` 内の行で誤引用） | `bsp_io.h:347-351` |
| 失敗の返し方 | 戻り値なし（`void`）。呼び出し元は常に `CMD_OK`。判定は出力文言で表す。既存 30 本セクションと同じ | `glcdc_port.c:1380,2322-2325` |
| 実行コンテキスト制約 | ISR から呼ばれない。FPU / I2C 不使用。**PmnPFS への書き込みは一切行わない**（読み専用なので `glcdc_lcd_reset()` / `glcdc_port_backlight_control()` と競合しない） | ― |

ビット位置は MCU ヘッダで確認済み: `PODR`=b0 / `PIDR`=b1 / `PDR`=b2 / `DSCR`=b[11:10] /
`PMR`=b16 / `PSEL`=b[28:24]（`R7KA8P1KF_core0.h:66216-66237`）。既存コードの
`(pfs>>24)&0x1F` / `(pfs>>16)&1` と一致する。

対象 2 本は起動時に `IOPORT_CFG_DRIVE_MID | PORT_DIRECTION_OUTPUT | PORT_OUTPUT_LOW`
（`pin_data.c:255-257` P514 / `:267-269` P606）。すなわち **起動直後は
リセット assert・バックライト OFF** が正常状態。

## 2. 状態変数

**新設しない。** 表 `g_glcdc_gpio_pins[]` は `static const`（ROM 常駐・書き手なし）。
唯一の読み手は `glcdc_cmd_pins()`（`ntshell_task`）。
PmnPFS の書き手は既存の `R_IOPORT_PinWrite()` 経路（`glcdc_lcd_reset()` は
`glcdc_port_init()` から、`glcdc_port_backlight_control()` は LVGL flush-finish
コールバックと `display backlight` コマンド）で、本 Issue はそこを変更しない。
読み側は 32bit 単一ロードなので tearing しない。

## 3. 排他方針

CLAUDE.md「並行性の既定形」の対象外（reconcile するデバイス操作を追加しない、
共有可変状態を追加しない）。クリティカルセクション不要。
2 本の PmnPFS を別々のタイミングで読むが、両者は独立した診断値であり
「同一時刻のスナップショット」である必要はない。

## 4. 出力設計

既存 30 本とは正常条件が逆（あちらは `PSEL=0x19`/`PMR=1`、こちらは `PSEL=0`/`PMR=0`）
なので、独立したセクション `[LCD GPIO Control Pins (PmnPFS) Readback]` として出す。
判定は上から順に評価し、最初に当たった 1 つだけを表示する:

| # | 条件 | 出力 | 異常か |
|---|---|---|---|
| 1 | `PMR!=0` または `PSEL!=0` | `<<< NOT GPIO (taken by a peripheral)` | 異常 |
| 2 | `PDR!=1` | `<<< NOT OUTPUT` | 異常 |
| 3 | `PODR==1` かつ `PIDR==0` | `<<< DRIVEN HIGH BUT PAD IS LOW (pulled down off-chip)` | 異常 |
| 4 | `PODR==0` かつ `PIDR==1` | `<<< DRIVEN LOW BUT PAD IS HIGH (driven off-chip)` | 異常 |
| 5 | `PODR==0` かつ P606 | `<<< RESET ASSERTED …` ／ 初期化前は `<<< RESET NOT RELEASED …` | 異常 |
| 6 | `PODR==0` かつ P514 | `-- backlight off (normal after …)` ／ 初期化前は `-- backlight off (raised after the first LVGL flush)` | 正常 |
| 7 | それ以外 | （空欄） | 正常 |

行 5 / 6 の文言は `s_glcdc_status` で切り替える（PR #224 レビュー指摘。詳細は §8）。

**行 4 は実装時に追加した**（Issue の表・初版メモは 6 行だった）。プッシュプル出力では
`PIDR` は必ず `PODR` に一致するので、不一致は向きを問わず異常。これが無いと
`PODR=0` かつ `PIDR=1`（＝ソフトは Low を出しているがパッドは High ＝
リセットは実際には解除されている）で `<<< RESET ASSERTED` と**事実と逆の診断**を
出してしまう。判定の順序上、行 3 と対になる位置に置く必要がある。

`<<<` は既存セクションと同じく異常マーカ、`--` は情報表示に使い分ける。
P514 の `PODR=0` を異常にしないのは、**`display backlight off` 後と
初回 flush 前（`glcdc_backlight_on_event` が ON にするまで）が両方これになる**ため
（`glcdc_port.c:318-333`）。P606 と P514 の違いは表のフラグ 1 個
（`low_is_fault`）＋文言で表現し、判定コードは共通にする。

生値（32bit）と `PODR`/`PIDR`/`PDR`/`PMR`/`PSEL` の分解を併記する。
末尾に異常本数のサマリを 1 行出す。

## 5. 捨てた代替案

- **既存 `g_glcdc_pins[]` に 2 本を追加する** — 正常条件が真逆なので `driven` 相当の
  フラグ分岐が全 32 エントリに波及し、「30/30 assigned」サマリの意味も壊れる。
- **`display signals` にも 2 本を足す** — あちらは 100 ms 間のトグル検出。静的レベルの
  制御線は必ず `STUCK` と出て偽陽性ノイズになる。本 Issue の目的は静的レベルの確認。
- **新サブコマンド `display gpio` を作る** — 白画面の切り分けで最初に叩くのは
  `display pins`。別コマンドにすると今回と同じ「見落とし」が再発する。
- **`R_BSP_PinRead()` / `R_IOPORT_PinRead()` を使う** — `PIDR` しか取れず、
  `PODR`/`PDR`/`PMR`/`PSEL` を別途読む必要がある。PmnPFS を 1 回読めば全部入る。
- **診断中にピンを叩いて応答を見る（アクティブ試験）** — 稼働中のパネルとタッチを
  リセットしうる。診断コマンドは読み専用に保つ。

## 6. 受け入れ確認の方法

FLASH 増分は `.flash.endof` で測る（`llvm-readelf -S <elf>` のアドレス − `0x02000000`）。

| | `.flash.endof` | 使用量 | 992 KiB (`0xF8000`) 中の残り |
|---|---|---|---|
| ベースライン（`main`） | `0x020C8400` | 820,224 B | 195,584 B |
| 本 Issue 実装後 | `0x020C8800` | 821,248 B | 194,560 B |
| **増分** | | **+1,024 B** | |

新規警告なし（ビルド時の 6 件はすべて CMSIS-DSP / LVGL / `r_typedefs.h` 由来の既存分で、
`glcdc_port.c` からは 0 件）。

## 7. 実機確認結果（2026-08-29、EK-RA8P1）

以下は **§8 のレビュー対応より前**の出力。対応後は 2 行目の下に `Display init: done` が
1 行増える（判定結果とサマリは変わらない）。

`display pins` の新セクション（30 本セクションは従来どおり `30 / 30`）:

```
[LCD GPIO Control Pins (PmnPFS) Readback]
  Not GLCDC pins. Expect PSEL=0x00, PMR=0, PDR=1, PODR=PIDR.
  DISP_RESET P606 0x00000407 PODR=1 PIDR=1 PDR=1 PMR=0 PSEL=0x00
    panel + touch reset
  DISP_BLEN  P514 0x00000407 PODR=1 PIDR=1 PDR=1 PMR=0 PSEL=0x00
    backlight enable
  2 / 2 GPIO control pins healthy.
```

`display backlight off` 実行後（P514 のみ変化。警告マーカーは出ない）:

```
  DISP_BLEN  P514 0x00000404 PODR=0 PIDR=0 PDR=1 PMR=0 PSEL=0x00
    backlight enable  -- backlight off (normal after 'display backlight off')
  2 / 2 GPIO control pins healthy.
```

受け入れ条件は全て充足。あわせて、`PODR` を 0 にすると **`PIDR` も 0 に追従する**ことが
確認できた。これは §4 の判定が前提とする「プッシュプル出力では `PODR==PIDR`」が実機で
成立していること、すなわち行 3 / 行 4 のパッド不一致チェックが機能する条件が
揃っていることの裏付けになる。

なお `0x00000407` / `0x00000404` は Issue #222 本文の手動測定（`mr` 直読み）と一致しており、
コマンド実装がレジスタを正しく読めていることの相互確認にもなっている。

## 8. PR #224 レビュー対応: 起動途中の偽陽性（P2）

### 指摘

`glcdc_port_init()` が P606 を解放する前に `display pins` を実行すると、正常な電源投入時の
状態を `<<< RESET ASSERTED` ＋ `<<< PANEL CANNOT DISPLAY` と報告してしまう。

### 裏取り（3 点とも成立）

| 主張 | 検証 |
|---|---|
| NT-Shell タスクは LVGL タスク生成より前に起動される | `usermain.c` の `tk_sta_tsk(ntshell)` が `tk_cre_tsk(&ctsk_lvgl)` より前 |
| `glcdc_port_init()` が P606 を解放する | Step 1 の `glcdc_lcd_reset()`（`glcdc_port.c:1911`） |
| P606 は Low で初期化される | `pin_data.c:267-269` の `PORT_OUTPUT_LOW` |

加えて優先度も指摘に有利: μT-Kernel は数値が小さいほど高優先で、`ntshell=12` / `lvgl=14`
（`usermain.c` の優先度表）。`display pins` は `s_glcdc_status` を見ていない
（`glcdc_cmd_blank` 等は `:747,1210,1635,1757` でゲートしている）ので初期化前でも素通しする。

窓自体は数十 ms（`usermain` の残りのタスク生成 ＋ `lv_init()` ＋ `dave2d_port_init()`）で、
人手のタイピングでは届かない。現実に当たるのは ①端末が起動直後にコマンドを自動送信する場合、
②**`lvgl_task` が `glcdc_port_init()` に到達しない場合**（生成失敗・ハング・クラッシュ）。

### 採った対処 ―― 抑制ではなく原因の帰属を変える

指摘の提案（`NOT_INITIALIZED` の間は Low を初期化状態として扱う）を**そのままは採らない**。
上記②の状態は「パネルが映らない本当の原因」であり、判定を抑制すると `display pins` が
`2 / 2 healthy` と報告して原因を隠す。**Issue #222 が潰した盲点を別の場所で再現する**ことになる。

そこで `low_is_fault`（サマリと `PANEL CANNOT DISPLAY` を左右する）は立てたまま、
**文言だけ `s_glcdc_status` で切り替える**。表に `low_note_early` を足し、判定コードは共通のまま。

| `s_glcdc_status` | P606 が Low のときの文言 |
|---|---|
| `NOT_INITIALIZED` | `<<< RESET NOT RELEASED (display init has not run yet)` |
| `INITIALIZED` / `ERROR` | `<<< RESET ASSERTED (panel and touch held in reset)` |

P514 も同様に `-- backlight off (raised after the first LVGL flush)` を初期化前用に持たせた
（初期化前に「`display backlight off` 実行後なら正常」と出るのは事実と合わないため）。

あわせてセクション冒頭に `Display init:` の 1 行を出し、読み手が自分で判断できるようにした。

`GLCDC_STATUS_ERROR` は考慮不要。`glcdc_lcd_reset()` は Step 1 で走り、失敗パス（`:1947`）は
その後なので、ERROR 時には P606 はすでに High。P606 の Low が正当なのは `NOT_INITIALIZED` のときだけ。

### 同期について

`s_glcdc_status` は `lvgl_task` が書き、ここでは `ntshell_task` が読む。`volatile` ではないが、
**既存の `display` サブコマンドが同じ読み方をしている**（`:747,1210,1635,1757`）のでそれに倣う。
古い値を読んでも診断の 1 行の文言が変わるだけで、判定の可否や副作用は無い。
`early` はループの外で 1 回だけ採取し、2 本の出力が同じ時点を指すようにしている。

### FLASH

| | `.flash.endof` | `llvm-size` text |
|---|---|---|
| ベースライン（`main`） | `0x020C8400` | 819,714 B |
| #222 初版 | `0x020C8800` | 820,706 B |
| 本レビュー対応後 | `0x020C8800` | 820,962 B |

`.flash.endof` は **`0x020C8800` のまま（`main` から +1,024 B）**。セクション終端がアラインされる
ため、今回の +256 B は同じ粒度に吸収された。細かい差分が要るときは `text` を併記する
（`text` はセクション間の埋めを含まないので絶対値は `.flash.endof` とずれる）。
