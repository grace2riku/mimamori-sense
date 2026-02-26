# e2 studio操作手順書: F-002-1: FSPプロジェクト設定 - VIN・MIPI CSI・MIPI PHYモジュールの追加

## 対象Issue

- Issue #10: F-002-1: FSPプロジェクト設定 - VIN・MIPI CSI・MIPI PHYモジュールの追加

## 対象プロジェクト

- `e2studio_CPU0/configuration.xml`

## リファレンスプロジェクト

- `reference_projects/quickstart_ek_ra8p1_ep/e2studio/configuration.xml`

## 現在のプロジェクト状態

現在の`e2studio_CPU0`に存在するスレッド:

- `blinky_thread` (priority: 1, stack: 512)
- `ntshell_thread` (priority: 1, stack: 4096)
- `lvgl_thread` (priority: 2, stack: 8192) --- LVGL, GLCDC, D/AVE 2D, タッチパネルI2C, IRQ含む

現在の`e2studio_CPU0`に存在するHAL/Commonモジュール:

- `r_ioport` (g_ioport)
- `rm_freertos_port` (FreeRTOS)
- `r_sci_b_uart` (UART)

**追加が必要なモジュール（3つとも未設定）:**

1. VIN (Video Input) ドライバ (`r_vin`)
2. MIPI CSI-2 ドライバ (`r_mipi_csi`)
3. MIPI PHY ドライバ (`r_mipi_phy`)

**既に設定済みの関連項目:**

- SDRAMは有効化済み（BSP > SDRAM Support = Enabled）
- MIPIピン設定は`mipi.mode.enabled.free`として設定済み
- `pin_data.c`にMIPIペリフェラルの設定あり
- CAMERA_IRQ (P010)、CAMERA_RESET (P709) のピンシンボル名も設定済み

---

## 前提条件の確認

### SDRAM

VINの出力バッファ（トリプルバッファ）は`.sdram_noinit`セクションに配置します。現プロジェクトではSDRAMは既に有効化済みのため、追加のSDRAM設定は不要です。

### MIPIピン

現プロジェクトの`configuration.xml`に以下のMIPI関連ピン設定が確認されています:

- `mipi.mode.enabled.free` --- MIPIペリフェラルが有効
- `MIPI_INT` (P111) --- MIPI割り込みピン
- `MIPI_TE` (P411) --- MIPI Tearing Effectピン

MIPI CSI-2はMIPIの専用レーン（MIPI_D0P/D0N, MIPI_D1P/D1N, MIPI_CLKP/CLKN）を使用します。これらは専用ピンであり、Pinsタブでの追加設定は通常不要です。

> **注意**: 上記のMIPIピンはディスプレイ用（MIPI DSI）のものです。MIPI CSI-2（カメラ入力）は物理的に異なる専用レーンを使用するため、Pinsタブでの個別設定は不要です。FSPのVINモジュール追加時に自動的にピンが割り当てられます。

### FreeRTOS configMAX_PRIORITIES

現在の`configMAX_PRIORITIES`は`5`です。リファレンスプロジェクトの`camera_thread`はpriority `3`で動作しているため、現在の設定のままで問題ありません。

---

## 手順1: Camera Threadの作成

リファレンスプロジェクトでは、VIN・MIPI CSI・MIPI PHYモジュールは`camera_thread`（FreeRTOSスレッド）の中にスタックされています。本プロジェクトでも同じ構造で作成します。

1. e2 studioで`e2studio_CPU0/configuration.xml`を開く
2. **Stacks** タブを選択
3. **New Thread** ボタンをクリック
4. 作成されたスレッドを選択し、Propertiesパネルで以下を設定

| パラメータ | 設定値 | 備考 |
|---|---|---|
| Symbol | `camera_thread` | スレッドエントリ関数名の基準になる |
| Name | `Camera Thread` | FreeRTOS上の表示名 |
| Stack size (bytes) | `10240` | リファレンスと同値。カメラ処理は大きなスタックが必要 |
| Priority | `3` | LVGL Thread (2) より高い優先度 |
| Thread Context | `NULL` | デフォルト |
| Memory Allocation | `Static` | 静的メモリ確保 |
| Allocate Secure Context | `Enable` | リファレンスと同値 |

> **確認事項**: スレッド作成後、`src/camera_thread_entry.c`を手動作成する必要があります（Generate Project Contentで自動生成されるエントリ関数のスタブは`ra_gen/camera_thread.c`に生成されます）。

---

## 手順2: VIN (Video Input) ドライバの追加

Camera Threadにr_vinモジュールをスタックとして追加します。

### 2-1: モジュールの追加

1. **Stacks** タブで左ツリーの **Camera Thread** を選択
2. **New Stack** ボタン（または右クリック > Add New Stack）をクリック
3. **HAL Drivers** > **Video Input (VIN) (r_vin)** を選択して追加

### 2-2: 入力制御 (Input Control) の設定

VINモジュール（`g_vin0`）を選択し、Propertiesパネルで以下を設定します。

#### General

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Name | `g_vin0` | デフォルトのまま |

#### Input Control > Configuration Bits

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Color Space Convert Bypass | `False` | YCbCr->RGB変換を使用 |
| Interlace Mode | `Odd-even` | インターレース対応 |
| Big Endian | `False` | リトルエンディアン |
| Dithering Mode | `With addition` | リファレンスと同値 |
| Input Mode | `YCbCr422 8-bit` | OV5640のYUV422出力に対応 |
| Dithering Direction | `Enabled` | リファレンスと同値 |
| Transform Mode (※GUI上の表示名。XML内部名: YUV444 Conversion) | `Y and CbCr` | YUV→RGB変換モード。リファレンスと同値 |
| Scaling Enable | `Disabled` | スケーリング無効 |
| Pixel Data Clipping | `YCbCr422 8-bit` | 入力モードと一致させる |

#### Input Control > Pre-Clip

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Line Start | `1` | |
| Pixel Start | `1` | |

#### Input Control > Image Size

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Vertical | `450` | 768x450解像度 |
| Horizontal | `768` | 768x450解像度 |

#### Input Control > CSI Mode Bits

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Virtual Channel | `0` | MIPI CSI-2仮想チャネル0 |
| Sign Extend Disable | `True` | リファレンスと同値 |

#### Input Control > CSI Detect Bits

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Field Detect Enable | `Enabled` | フィールド検出有効 |
| Even Field Number | `0` | リファレンスと同値 |

### 2-3: 出力制御 (Output Control) の設定

#### Output Control > Image Buffers

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Image Buffer 1 Name | `vin_image_buffer_1` | トリプルバッファ1 |
| Image Buffer 2 Name | `vin_image_buffer_2` | トリプルバッファ2 |
| Image Buffer 3 Name | `vin_image_buffer_3` | トリプルバッファ3 |
| Image Buffer 1 Section | `.sdram_noinit` | SDRAM上に配置 |
| Image Buffer 2 Section | `.sdram_noinit` | SDRAM上に配置 |
| Image Buffer 3 Section | `.sdram_noinit` | SDRAM上に配置 |

> **重要**: 3つのバッファ全てを`.sdram_noinit`セクションに配置します。これによりSDRAM上にフレームバッファが確保され、初期化時のゼロクリアも省略されます。1フレーム分のバッファサイズは 768 x 450 x 2(RGB565) = 691,200バイト です。

### 2-4: 変換制御 (Conversion Control) の設定

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Data Conversion Mode | `None` | リファレンスと同値 |
| Alpha Bit Value | `One` | リファレンスと同値 |
| Output Data Byte Swap | `Enabled` | バイトスワップ有効 |
| Extend RGB Converted Data | `Disabled` | リファレンスと同値 |
| YC Transform Disable | `False` | YC変換を使用 |
| YC Transform Mode | `Y_CbCr` | リファレンスと同値 |
| RGB8888 Alpha Value | `0xAA` | リファレンスと同値 |

### 2-5: 変換データ (Conversion Data) の設定

以下はYCbCr->RGB変換の係数設定です。リファレンスプロジェクトの値をそのまま使用してください。

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| UV Address | `0x0` | |
| Y Multiply | `1.164` | BT.601変換係数 |
| Round Down Disable | `False` | |
| Y Sub 2 | `256` | |
| CG-R Multiply 2 | `0.813` | |
| R-CR Multiply 2 | `1.596` | |
| G-CB Multiply 2 | `0.392` | |
| B-CB Multiply 2 | `2.017` | |
| NE BCB | `Bilinear` | 補間方式 |
| NE GY | `Bilinear` | 補間方式 |
| NE RCR | `Bilinear` | 補間方式 |
| Pixel Interpolation | `Bilinear or Nearest` | |
| Bilinear Advanced | `Enabled` | |
| Scale Up Pixel Count | `Minus One` | |
| CL VSize | `450` | Image Sizeと一致 |
| CL HSize | `768` | Image Sizeと一致 |

#### LUT (Look-Up Table) 設定 --- Y成分

| プロパティ名 | 設定値 |
|---|---|
| Setting 1 Bits Y > LRP | `263` |
| Setting 2 Bits Y > LGP | `516` |
| Setting 2 Bits Y > LBP | `100` |
| Setting 3 Bits Y > LAP | `256` |
| Setting 3 Bits Y > LHEN | `Disabled` |

#### LUT設定 --- Cb成分

| プロパティ名 | 設定値 |
|---|---|
| Setting 1 Bits Cb > LRP | `-152` |
| Setting 2 Bits Cb > LGP | `-298` |
| Setting 2 Bits Cb > LBP | `450` |
| Setting 3 Bits Cb > LAP | `2048` |
| Setting 3 Bits Cb > LHEN | `Disabled` |

#### LUT設定 --- Cr成分

| プロパティ名 | 設定値 |
|---|---|
| Setting 1 Bits Cr > LRP | `450` |
| Setting 2 Bits Cr > LGP | `-377` |
| Setting 2 Bits Cr > LBP | `-73` |
| Setting 3 Bits Cr > LAP | `2048` |
| Setting 3 Bits Cr > LHEN | `Disabled` |

> **備考**: 上記のLUT設定値はBT.601 YCbCr->RGB変換の標準的な係数です。リファレンスプロジェクトの値をそのまま使用しています。

### 2-6: 割り込み制御 (Interrupt Control) の設定

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Status Enable Mask | `Frame Write Complete` | フレーム書き込み完了割り込みを有効化 |
| Scanline Compare Value | `0` | 使用しない |
| Callback | `vin0_callback` | ユーザー定義コールバック関数名 |
| Context | `NULL` | デフォルト |
| Status Interrupt Priority | `Priority 12` | リファレンスと同値 |
| Error Interrupt Priority | `Priority 12` | リファレンスと同値 |

---

## 手順3: MIPI CSI-2 ドライバの追加

VINモジュールの依存関係としてMIPI CSIモジュールを追加します。

### 3-1: モジュールの追加

1. **Stacks** タブで、手順2で追加した **g_vin0** モジュールを選択
2. **New Stack** ボタンをクリック（または右クリック > Add dependent module）
3. **HAL Drivers** > **MIPI CSI-2 (r_mipi_csi)** を選択して追加

> **備考**: VINモジュールにはMIPI CSIへの依存関係（requires）が定義されているため、VINを選択した状態でNew Stackを追加すると、MIPI CSIが依存モジュールとして自動的にVINの下にスタックされます。もしNew Stackの一覧にMIPI CSIが表示されない場合は、VINモジュール自体を選択していることを再確認してください。

### 3-2: 基本設定 (Control 0)

MIPI CSIモジュール（`g_mipi_csi0`）を選択し、Propertiesパネルで以下を設定します。

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Name | `g_mipi_csi0` | デフォルトのまま |
| Lane Count | `2` | デュアルレーン |
| Zero Length Packet Output | `Disabled` | リファレンスと同値 |
| Error Frame Notify | `Enabled` | エラーフレーム通知有効 |
| Reserved Packet Reception | `Enabled` | リファレンスと同値 |
| ECC Check 24 bits | `Enabled` | ECCチェック有効 |
| Descramble Enable | `Disabled` | リファレンスと同値 |

### 3-3: Control 2設定

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| FRRCLK Adjustment | `0` | デフォルト |
| FRRSKW Adjustment | `0` | デフォルト |

### 3-4: Option Data

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Data Type Enable (User) | (空欄) | リファレンスと同値 |

### 3-5: 割り込み制御 (Interrupt Control)

#### 割り込み優先度

全ての割り込み優先度を`Priority 12`に設定します。

| プロパティ名 | 設定値 |
|---|---|
| RX Priority | `Priority 12` |
| Data Lane 0 Priority | `Priority 12` |
| VC 0 Priority | `Priority 12` |
| Power Management Priority | `Priority 12` |
| Short Packet FIFO Priority | `Priority 12` |

#### RXイベント

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| RX Events | (空欄 / 何も選択しない) | リファレンスと同値 |

#### Data Lane 0 イベント

以下の4つ全てにチェックを入れます:

| イベント名 | 設定 |
|---|---|
| Error SOT HS Detect | チェック |
| Error SOT Sync Detect | チェック |
| Error Control Detect | チェック |
| Error Escape Detect | チェック |

#### Data Lane 1 イベント

Data Lane 0と同じく、以下の4つ全てにチェックを入れます:

| イベント名 | 設定 |
|---|---|
| Error SOT HS Detect | チェック |
| Error SOT Sync Detect | チェック |
| Error Control Detect | チェック |
| Error Escape Detect | チェック |

#### VC 0 (Virtual Channel 0) イベント

以下の全てにチェックを入れます:

| イベント名 | 設定 |
|---|---|
| Malformed Packet Detect | チェック |
| Error ECC 2 Bit Detect | チェック |
| Error CRC Detect | チェック |
| Error ID Detect | チェック |
| Error Word Count Detect | チェック |
| Error ECC 1 Bit Detect | チェック |
| Error Frame Sync Detect | チェック |
| Error Frame Data Detect | チェック |
| Short Packet FIFO Overflow Detect | チェック |

> **備考**: リファレンスプロジェクトではVC 1からVC 15にも全て同じイベントが設定されていますが、実際に使用する仮想チャネルは0のみです。必要に応じてVC 1以降も同様に設定してください。本手順ではリファレンスに合わせてVC 0のみでなく全VCのイベントを有効にすることを推奨します。

#### Power Management イベント

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Power Management Events | (空欄 / 何も選択しない) | リファレンスと同値 |

#### Short Packet FIFO イベント

| イベント名 | 設定 |
|---|---|
| Overflow | チェック |

### 3-6: コールバック設定

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Callback | `mipi_csi0_callback` | ユーザー定義コールバック関数名 |
| Context | `NULL` | デフォルト |

---

## 手順4: MIPI PHY ドライバの追加

MIPI CSIモジュールの依存関係としてMIPI PHYモジュールを追加します。

### 4-1: モジュールの追加

1. **Stacks** タブで、手順3で追加した **g_mipi_csi0** モジュールを選択
2. **New Stack** ボタンをクリック（または右クリック > Add dependent module）
3. **HAL Drivers** > **MIPI PHY Host (r_mipi_phy)** を選択して追加

> **備考**: MIPI CSIモジュールにはMIPI PHYへの依存関係が定義されているため、MIPI CSIを選択した状態でNew Stackを追加すると、MIPI PHYが依存モジュールとしてMIPI CSIの下にスタックされます。

### 4-2: PLL設定

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Name | `g_mipi_phy0` | デフォルトのまま |
| PLL Frequency (MHz) | `1000.00` | OV5640 2レーン時の設定 |
| LP Divisor | `5` | リファレンスと同値 |

### 4-3: タイミングパラメータ

全てのタイミングパラメータをリファレンスプロジェクトと同じ値に設定します。

| プロパティ名 | ns値 | UI値 | 備考 |
|---|---|---|---|
| t_init | `600000` | - | 初期化待機時間 |
| t_hs_prep | `40` | `5` | HS準備時間 |
| t_hs_settle | `200` | `0` | HSセトル時間 |
| t_hs_zero | `140` | `10` | HSゼロ時間 |
| t_hs_trail | `60` | `4` | HSトレイル時間 |
| t_clk_post | `60` | `52` | クロックポスト時間 |
| t_clk_pre | `0` | `8` | クロックプリ時間 |
| t_clk_prep | `75` | `0` | クロック準備時間 |
| t_clk_settle | `500` | `0` | クロックセトル時間 |
| t_clk_miss | `300` | `0` | クロックミス時間 |
| t_lp_exit | `60` | `0` | LP離脱時間 |
| t_clk_trail | `60` | `0` | クロックトレイル時間 |
| t_clk_zero | `230` | `0` | クロックゼロ時間 |
| t_hs_exit | `100` | `0` | HS離脱時間 |

> **重要**: 上記のタイミングパラメータはリファレンスプロジェクトの値をそのまま使用しています。OV5640センサーとの通信に最適化された値です。変更する場合はMIPI D-PHYの仕様書を参照してください。

---

## 手順5: ピン設定の確認

### 5-1: MIPIピンの確認

1. **Pins** タブを選択
2. ピン一覧で以下が設定されていることを確認

現プロジェクトでは以下のMIPI関連ピン設定が既に存在します:

| ピン | シンボル名 | 用途 |
|---|---|---|
| P111 | `MIPI_INT` | MIPI割り込み |
| P411 | `MIPI_TE` | Tearing Effect |
| (MIPI専用レーン) | - | MIPI CSI-2データ/クロックレーン |

また、`configuration.xml`には`mipi.mode.enabled.free`が設定されており、MIPIペリフェラルは有効です。

> **注意**: MIPI CSI-2のデータレーン（D0P/D0N, D1P/D1N）とクロックレーン（CLKP/CLKN）は専用ピンのため、Pinsタブでの個別設定は不要です。VINモジュールを追加してGenerate Project Contentを実行すれば、必要なピン設定は自動的に反映されます。

### 5-2: カメラ関連GPIOの確認

現プロジェクトで既に設定されているカメラ関連GPIO:

| ピン | シンボル名 | 用途 |
|---|---|---|
| P010 | `CAMERA_IRQ` | カメラ割り込み |
| P709 | `CAMERA_RESET` | カメラリセット |

これらは既に設定済みのため、追加の変更は不要です。

---

## 手順6: スタック構成の確認

全ての設定が完了すると、**Stacks** タブのCamera Thread配下で以下のような階層構造になっているはずです:

```
Camera Thread (camera_thread)
  +-- g_vin0 Video Input (VIN) (r_vin)
        +-- g_mipi_csi0 MIPI CSI-2 (r_mipi_csi)
              +-- g_mipi_phy0 MIPI PHY Host (r_mipi_phy)
```

この階層はリファレンスプロジェクト`quickstart_ek_ra8p1_ep`の構成と一致しています。

---

## 最終手順: コード生成とビルド確認

### コード生成

1. `e2studio_CPU0/configuration.xml`のエディタ上部にある **Generate Project Content** ボタンをクリック
2. コード生成が完了するまで待つ
3. エラーが表示されないことを確認

### 生成されるファイルの確認

コード生成により以下のファイルが`ra_gen/`配下に生成（または更新）されます:

| ファイル | 内容 |
|---|---|
| `ra_gen/camera_thread.c` | Camera Threadのエントリ関数スタブ |
| `ra_gen/camera_thread.h` | Camera Threadのヘッダ |
| `ra_gen/hal_data.c` | VIN, MIPI CSI, MIPI PHYの構成データ |
| `ra_gen/hal_data.h` | 各ドライバのextern宣言 |
| `ra_gen/vector_data.c` | 割り込みベクタテーブル（VIN/MIPI CSI割り込み追加） |
| `ra_gen/pin_data.c` | ピン設定データ（更新） |

> **注意**: `ra_gen/`配下のファイルは自動生成されるため、手動で編集しないでください。

### ビルド確認

1. **Project** > **Build Project** を実行
2. ビルドがエラーなく完了することを確認
3. `vin0_callback`および`mipi_csi0_callback`のコールバック関数が未定義の場合、リンクエラーが発生します。その場合は`src/camera_thread_entry.c`に以下のスタブを作成してください:

```c
#include "camera_thread.h"

/* Camera Thread entry function */
void camera_thread_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    /* TODO: カメラ初期化・キャプチャ処理を実装 */
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* VIN callback */
void vin0_callback(vin_callback_args_t *p_args)
{
    FSP_PARAMETER_NOT_USED(p_args);
    /* TODO: フレームキャプチャ完了時の処理を実装 */
}

/* MIPI CSI-2 callback */
void mipi_csi0_callback(mipi_csi_callback_args_t *p_args)
{
    FSP_PARAMETER_NOT_USED(p_args);
    /* TODO: MIPI CSIイベント処理を実装 */
}
```

---

## 受け入れ確認チェックリスト

- [ ] `configuration.xml`にVIN（`g_vin0`）モジュールが追加されている
- [ ] `configuration.xml`にMIPI CSI（`g_mipi_csi0`）モジュールが追加されている
- [ ] `configuration.xml`にMIPI PHY（`g_mipi_phy0`）モジュールが追加されている
- [ ] 3モジュールがCamera Thread内でVIN > MIPI CSI > MIPI PHYの階層構造になっている
- [ ] トリプルバッファ（`vin_image_buffer_1/2/3`）が`.sdram_noinit`セクションに配置されている
- [ ] 画像サイズが768 x 450に設定されている
- [ ] MIPI CSI-2のレーン数が2に設定されている
- [ ] MIPI PHYのPLL周波数が1000.00 MHzに設定されている
- [ ] 全割り込み優先度がPriority 12に設定されている
- [ ] Generate Project Contentでエラーなくコード生成が完了する
- [ ] ビルドがエラーなく完了する

---

## 参照情報

| 項目 | 参照先 |
|---|---|
| リファレンスプロジェクト | `reference_projects/quickstart_ek_ra8p1_ep/e2studio/configuration.xml` |
| VIN設定値（行番号） | リファレンス configuration.xml L1856-L1925 |
| MIPI CSI設定値（行番号） | リファレンス configuration.xml L1926-L1965 |
| MIPI PHY設定値（行番号） | リファレンス configuration.xml L1966-L1997 |
| スタック構成（行番号） | リファレンス configuration.xml L2045-L2057 |
| Issue | https://github.com/grace2riku/mimamori-sense/issues/10 |
