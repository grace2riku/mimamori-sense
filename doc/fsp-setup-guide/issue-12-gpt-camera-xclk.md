# e2 studio操作手順書: F-002-3: FSPプロジェクト設定 - カメラ用XCLK生成タイマー(GPT)の追加

## 対象Issue

- Issue #12: F-002-3: FSPプロジェクト設定 - カメラ用XCLK生成タイマー(GPT)の追加

## 対象プロジェクト

- `e2studio_CPU0/configuration.xml`（Stacksタブ、Pinsタブ）
- `e2studio/solution.xml`（Clocksタブ）

## リファレンスプロジェクト

- `reference_projects/quickstart_ek_ra8p1_ep/e2studio/configuration.xml`

## 現在のプロジェクト状態

### GPT関連の設定状況

現在のプロジェクトにはGPTモジュール（r_gpt）は追加されていません。GPTドライバのコンポーネント自体が未登録の状態です。

### ピン設定の状況

| ピン | シンボル名 | 現在の割り当て | 備考 |
|---|---|---|---|
| P501 | CAM_XCLK | 未割り当て | シンボル名は定義済み。GPT12 GTIOC12A を割り当てる必要あり |

GPT12のモード設定 (`gpt12.mode.gtiocaorgtiocb.free`) は既に有効化されていますが、GTIOC12AのP501へのピン割り当てはまだ行われていません。

### クロック設定の状況（solution.xml）

| クロック | 現在の設定 | 周波数 |
|---|---|---|
| PLL2 | source=xtal(24MHz), div=3, mul=300 | 2400 MHz |
| PLL2P | div=4 | 600 MHz |
| PLL2R | div=5 | 480 MHz |
| GPTCLK | source=PLL2P, div=2 | 300 MHz |
| GTCLK | source=GPTCLK | 300 MHz |

### Camera Threadの現状

```
Camera Thread (camera_thread)
  +-- g_vin0 Video Input (VIN) (r_vin)
        +-- g_mipi_csi0 MIPI CSI-2 (r_mipi_csi)
              +-- g_mipi_phy0 MIPI PHY Host (r_mipi_phy)
```

### HAL/Commonの現状

```
HAL/Common
  +-- g_ioport I/O Port (r_ioport)
  +-- rm_freertos_port
  +-- g_uart0 UART (r_sci_b_uart)
  +-- g_i2c_master_camera I2C Master (r_iic_master)
```

---

## リファレンスプロジェクトとの比較

リファレンスプロジェクト (quickstart_ek_ra8p1_ep) のカメラ用XCLK GPT設定:

| プロパティ | リファレンスの値 | 備考 |
|---|---|---|
| Name | g_timer_camera_xclk | カメラXCLK生成用 |
| Channel | 12 | GPT12チャネル |
| Mode | Periodic | 周期的な矩形波を出力 |
| Period | 24000 | 24MHz |
| Period Unit | Kilohertz | kHz指定 |
| Duty Cycle (%) | 50 | 50%デューティ |
| GTIOCA Output Enabled | True | GTIOC12Aから出力 |
| GTIOCA Stop Level | Pin Level Low | 停止時はLow |
| GTIOCB Output Enabled | False | Bチャネルは不使用 |
| Callback | NULL | 割り込み不使用 |
| Overflow/Crest Interrupt Priority | Disabled | 割り込み不使用 |
| 配置場所 | HAL/Common (_hal.0) | スレッドに属さない共通モジュール |

**クロック設定の相違点（重要）:**

| 項目 | リファレンス | 現プロジェクト | 備考 |
|---|---|---|---|
| GPTCLKソース | PLL2R | PLL2P | **要変更** |
| GPTCLK分周 | 2 | 2 | 同一 |
| GPTCLK周波数 | 240 MHz | 300 MHz | 300MHzでは24MHz生成不可 (300/24=12.5) |
| GTCLKソース | PCLKD | GPTCLK | 差異あり（直接影響なし） |

**クロック変更が必要な理由:**

現在のGPTCLK=300MHzでは24MHzを正確に生成できません（300/24=12.5で割り切れない）。GPTCLKソースをPLL2R（480MHz）に変更することで GPTCLK=480/2=240MHz となり、240/24=10 で正確に24MHzを生成できます。

> **影響範囲**: 現プロジェクトにはGPTモジュールのインスタンスが他に存在しないため、GPTCLKソースの変更による既存機能への影響はありません。PLL2P自体もGPTCLK以外で使用されていないため、安全に変更可能です。

---

## 前提条件の確認

### チャネルの競合確認

現プロジェクトにはGPTモジュールのインスタンスが存在しないため、チャネル12の競合はありません。

### ピンの確認

リファレンスプロジェクトでは以下のピン割り当てが使用されています:

| ピン | 機能 | シンボル名 | 備考 |
|---|---|---|---|
| P501 | GPT12 GTIOC12A | CAM_XCLK | カメラXCLK出力ピン |

現プロジェクトでもP501のシンボル名は `CAM_XCLK` として定義済みです。GPT12のモード（`gpt12.mode.gtiocaorgtiocb.free`）も有効化済みですが、GTIOC12AとP501の接続設定が未完了です。

> **注意**: P501がEK-RA8P1ボード上でカメラ拡張ボードのXCLK入力に接続されていることを回路図で確認してください。リファレンスプロジェクトでは `CAM_XCLK` というシンボル名で使用されており、カメラモジュールのクロック入力に対応しています。

### 前提Issue

- Issue #10 (VIN/MIPI CSI/MIPI PHY) が完了していること
- Issue #11 (カメラ用I2Cマスタドライバ) が完了していること

---

## 手順 1: GPTCLKクロックソースの変更（solution.xml）

GPTで24MHzを正確に生成するため、GPTCLKのクロックソースを変更します。

> **重要**: マルチコアプロジェクトではクロック設定は `e2studio/solution.xml` の Clocks タブから変更します。`e2studio_CPU0/configuration.xml` の Clocks タブはロックされています。

### 1-1: solution.xmlのClocksタブを開く

1. e2 studioで `e2studio/solution.xml` を開く
2. **Clocks** タブを選択
3. クロックツリーが表示される

### 1-2: GPTCLKソースの変更

1. クロックツリー上で **GPTCLK** を探す
2. GPTCLKのソース選択を **PLL2P** から **PLL2R** に変更
3. 分周比（Divider）が **2** であることを確認（変更不要）

#### 変更前後のクロック周波数

| 項目 | 変更前 | 変更後 |
|---|---|---|
| GPTCLKソース | PLL2P (600 MHz) | PLL2R (480 MHz) |
| GPTCLK分周 | /2 | /2 |
| GPTCLK周波数 | 300 MHz | **240 MHz** |

> **確認ポイント**: Clocksタブ上でGPTCLKの周波数表示が **240 MHz** になっていることを確認してください。

### 1-3: 影響確認

以下の点を確認してください:

- PLL2R (480 MHz) の値自体は変更していないため、PLL2Rを使用する他のクロック（SCICLK, CANFDCLK, UCK, U60CK, ADCCLK）に影響はありません
- PLL2Pを使用するクロックはGPTCLKのみであったため、ソース変更後PLL2Pは未使用になりますが問題ありません

---

## 手順 2: GPTタイマーモジュールの追加（configuration.xml）

### 2-1: モジュールの追加

1. e2 studioで `e2studio_CPU0/configuration.xml` を開く
2. **Stacks** タブを選択
3. 左側のツリーで **HAL/Common** を選択
4. **New Stack** ボタンをクリック
5. **HAL Drivers** > **Timers** > **Timer, General PWM (r_gpt)** を選択して追加

> **備考**: リファレンスプロジェクトではカメラ用XCLK GPTタイマーはHAL/Common（スレッド外の共通領域）に配置されています。本プロジェクトでも同じ構造で追加します。HALに配置することで、Camera ThreadやLVGL Thread等、どのスレッドからでもXCLKの開始/停止が可能になります。

### 2-2: プロパティの設定

追加した Timer, General PWM (r_gpt) モジュールを選択し、Properties パネルで以下を設定します。

#### General

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Name | g_timer_camera_xclk | リファレンスと同値。カメラXCLK生成用であることを明示 |
| Channel | 12 | リファレンスと同値。GPT12チャネル |
| Mode | Periodic | リファレンスと同値。周期的な矩形波を出力 |
| Period | 24000 | リファレンスと同値。24MHz |
| Period Unit | Kilohertz | リファレンスと同値。kHz単位で指定 |

#### Output

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Custom Waveform Enable | Disabled | リファレンスと同値 |
| Duty Cycle Percent (only applicable in PWM mode) | 50 | リファレンスと同値。50%デューティで矩形波 |
| GTIOCA Output Enabled | True | **リファレンスと同値。GTIOC12Aピンから出力を有効化** |
| GTIOCA Stop Level | Pin Level Low | リファレンスと同値。タイマー停止時はLow |
| GTIOCB Output Enabled | False | リファレンスと同値。Bチャネルは不使用 |
| GTIOCB Stop Level | Pin Level Low | リファレンスと同値 |

#### GTIOCA Output Waveform（詳細出力波形設定）

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Initial Output Level | Low | リファレンスと同値 |
| Cycle End Output Level | Retain | リファレンスと同値 |
| Compare Match Output Level | Retain | リファレンスと同値 |
| Retain Output Level at Count Stop | Disabled | リファレンスと同値 |

#### GTIOCB Output Waveform

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Initial Output Level | Low | リファレンスと同値 |
| Cycle End Output Level | Retain | リファレンスと同値 |
| Compare Match Output Level | Retain | リファレンスと同値 |
| Retain Output Level at Count Stop | Disabled | リファレンスと同値 |

#### Input

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Count Up Source | (空欄) | リファレンスと同値 |
| Count Down Source | (空欄) | リファレンスと同値 |
| Start Source | (空欄) | リファレンスと同値 |
| Stop Source | (空欄) | リファレンスと同値 |
| Clear Source | (空欄) | リファレンスと同値 |
| Capture A Source | (空欄) | リファレンスと同値 |
| Capture B Source | (空欄) | リファレンスと同値 |
| GTIOCA Input Filter | None | リファレンスと同値 |
| GTIOCB Input Filter | None | リファレンスと同値 |

#### Compare Match

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Compare Match A | Disabled / 0 | リファレンスと同値 |
| Compare Match B | Disabled / 0 | リファレンスと同値 |

#### Interrupts

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Callback | NULL | リファレンスと同値。割り込みは不使用 |
| Overflow/Crest Interrupt Priority | Disabled | リファレンスと同値 |
| Capture A Interrupt Priority | Disabled | リファレンスと同値 |
| Capture B Interrupt Priority | Disabled | リファレンスと同値 |
| Trough Interrupt Priority | Disabled | リファレンスと同値 |

#### Extra Features

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Extra Features | Disabled | リファレンスと同値 |
| Output Disable | (空欄) | リファレンスと同値 |

> **24MHzクロック生成の仕組み**: GPTをPeriodicモードで動作させ、Period=24000kHz（=24MHz）、Duty Cycle=50%に設定することで、GTIOC12Aピンから24MHzの矩形波（50%デューティ）が出力されます。GPTCLKが240MHzの場合、分周比は 240/24=10 となり、正確に24MHzが生成されます。

---

## 手順 3: ピン設定

### 3-1: GPT12ペリフェラルのピン割り当て

1. **Pins** タブを選択
2. 左側のツリーで **Peripherals** > **Timers:GPT** > **GPT12** を選択
3. 以下のピンを設定

| ピン機能 | 設定値 | 備考 |
|---|---|---|
| GTIOC12A | P501 | リファレンスと同値。カメラXCLK出力ピン |

> **確認**: GPT12のOperation Modeが `GTIOCA or GTIOCB` に設定されていることを確認してください。現プロジェクトでは `gpt12.mode.gtiocaorgtiocb.free` が既に設定済みのため、GPT12のモードは有効化されています。

### 3-2: ピンのGPIOモード確認

1. **Pins** タブの左側ツリーで **Ports** > **P5** > **P501** を選択
2. 以下を確認

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Mode | Peripheral mode | ペリフェラル機能として使用。GPT12割り当て後に自動設定される場合あり |

> **注意**: P501にGPT12 GTIOC12Aを割り当てると、GPIO Modeが自動的にPeripheral modeに設定される場合があります。設定後にPinsタブのP501で確認してください。

---

## 手順 4: GPTドライバ設定の確認

GPTモジュールを初めて追加するため、GPTドライバのモジュール設定（Module Configuration）も確認します。

1. **Stacks** タブで追加したGPTモジュールを選択
2. Properties パネルの **Module** セクション（または BSP > Properties > RA Configuration）でGPTドライバの設定を確認

| プロパティ名 | 推奨値 | 備考 |
|---|---|---|
| Parameter Checking | BSP | リファレンスと同値。BSPの設定に従う |
| Pin Output Support | Enabled | **リファレンスと同値。ピン出力機能を有効化。重要** |
| Write Protect Enable | Disabled | リファレンスと同値 |

> **重要**: Pin Output Support が **Enabled** でない場合、GTIOC12Aピンからの出力が機能しません。必ず Enabled に設定してください。

---

## 手順 5: スタック構成の確認

全ての設定が完了すると、**Stacks** タブで以下の構成になっているはずです。

### HAL/Common

```
HAL/Common
  +-- g_ioport I/O Port (r_ioport)
  +-- rm_freertos_port
  +-- g_uart0 UART (r_sci_b_uart)
  +-- g_i2c_master_camera I2C Master (r_iic_master)
  +-- g_timer_camera_xclk Timer, General PWM (r_gpt)   <-- 新規追加
```

### Camera Thread (変更なし)

```
Camera Thread (camera_thread)
  +-- g_vin0 Video Input (VIN) (r_vin)
        +-- g_mipi_csi0 MIPI CSI-2 (r_mipi_csi)
              +-- g_mipi_phy0 MIPI PHY Host (r_mipi_phy)
```

### LVGL Thread (変更なし)

```
LVGL Thread (lvgl_thread)
  +-- LVGL (lvgl)
  |     +-- LVGL Port (rm_lvgl_port)
  |           +-- GLCDC (r_glcdc)
  |           +-- D/AVE 2D Port (drw)
  |                 +-- D/AVE 2D (tes dave2d)
  +-- g_comms_i2c_device0 I2C Communications Device (rm_comms_i2c)
  |     +-- g_comms_i2c_bus0 I2C Communications Bus (rm_comms_i2c)
  |           +-- g_i2c_master0 I2C Master (r_iic_master) [Channel 0]
  +-- g_external_irq0 External IRQ (r_icu)
```

---

## 最終手順: コード生成とビルド確認

### コード生成

1. `e2studio_CPU0/configuration.xml` のエディタ上部にある **Generate Project Content** ボタンをクリック
2. コード生成が完了するまで待つ
3. エラーが表示されないことを確認

### 生成されるファイルの確認

コード生成により以下のファイルが ra_gen/ 配下に生成（または更新）されます:

| ファイル | 内容 |
|---|---|
| ra_gen/hal_data.c | GPTタイマーの構成データ（g_timer_camera_xclkのcfg構造体、timer_on_gpt拡張構造体等）が追加される |
| ra_gen/hal_data.h | g_timer_camera_xclkのextern宣言が追加される |
| ra_gen/pin_data.c | GPT12ピン設定（P501 = GTIOC12A）が追加される |
| ra_gen/common_data.c | GPTドライバのモジュール設定（output_support等）が追加される |

> **注意**: ra_gen/ 配下のファイルは自動生成されるため、手動で編集しないでください。

### ビルド確認

1. **Project** > **Build Project** を実行
2. ビルドがエラーなく完了することを確認
3. CallbackがNULLに設定されているため、コールバック関数の未定義エラーは発生しません

> **補足**: XCLKの実際の出力開始は、アプリケーションコードから `R_GPT_Open()` および `R_GPT_Start()` を呼び出すことで行います。これは後続のカメラドライバ実装Issueで対応します。

---

## 受け入れ確認チェックリスト

- [ ] solution.xml の Clocks タブで GPTCLK ソースが PLL2R に変更されている
- [ ] GPTCLK 周波数が 240 MHz になっている
- [ ] configuration.xml に GPT タイマーモジュール (g_timer_camera_xclk) が追加されている
- [ ] GPT チャネルが 12 に設定されている
- [ ] Mode が Periodic に設定されている
- [ ] Period が 24000 (Kilohertz) に設定されている
- [ ] Duty Cycle が 50% に設定されている
- [ ] GTIOCA Output Enabled が True に設定されている
- [ ] GTIOCA Stop Level が Pin Level Low に設定されている
- [ ] GTIOCB Output Enabled が False に設定されている
- [ ] Callback が NULL に設定されている（割り込み不使用）
- [ ] GPT ドライバの Pin Output Support が Enabled に設定されている
- [ ] GPT タイマーが HAL/Common コンテキストに配置されている
- [ ] Pins タブで GPT12 の GTIOC12A が P501 に割り当てられている
- [ ] P501 のシンボル名が CAM_XCLK であることを確認
- [ ] Generate Project Content でエラーなくコード生成が完了する
- [ ] ビルドがエラーなく完了する

---

## 参照情報

| 項目 | 参照先 |
|---|---|
| リファレンスプロジェクト | reference_projects/quickstart_ek_ra8p1_ep/e2studio/configuration.xml |
| GPTタイマー設定値（行番号） | リファレンス configuration.xml L668-L718 |
| HALスタック構成（行番号） | リファレンス configuration.xml L2014-L2019 |
| ピン設定（行番号） | リファレンス configuration.xml L2552 (gpt12.gtioc12a.p501), L2668 (p501.gpt12.gtioc12a) |
| ピンシンボル名（行番号） | リファレンス configuration.xml L2287 (CAM_XCLK) |
| GPTドライバ設定（行番号） | リファレンス configuration.xml L2116-L2119 |
| クロック設定（行番号） | リファレンス configuration.xml L330-L332 (GPTCLK) |
| Issue | https://github.com/grace2riku/mimamori-sense/issues/12 |
| 前提Issue | https://github.com/grace2riku/mimamori-sense/issues/10, https://github.com/grace2riku/mimamori-sense/issues/11 |