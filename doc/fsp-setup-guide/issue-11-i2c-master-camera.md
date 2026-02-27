# e2 studio操作手順書: F-002-2: FSPプロジェクト設定 - カメラセンサー制御用I2Cマスタドライバの追加

## 対象Issue

- Issue #11: F-002-2: FSPプロジェクト設定 - カメラセンサー制御用I2Cマスタドライバの追加

## 対象プロジェクト

- `e2studio_CPU0/configuration.xml`

## リファレンスプロジェクト

- `reference_projects/quickstart_ek_ra8p1_ep/e2studio/configuration.xml`

## 現在のプロジェクト状態

### 既存のI2C関連設定

現在のプロジェクトには以下のI2C関連モジュールが設定済みです:

| モジュール | インスタンス名 | チャネル | スレーブアドレス | 用途 | 配置場所 |
|---|---|---|---|---|---|
| I2C Communications Device (rm_comms_i2c) | g_comms_i2c_device0 | - | 0x38 | タッチパネル (FT5X06) | LVGL Thread |
| I2C Communications Bus (rm_comms_i2c) | g_comms_i2c_bus0 | 1 | - | タッチパネル用バス | LVGL Thread |
| I2C Master (r_iic_master) | g_i2c_master0 | 0 | 0x38 | タッチパネル用下位ドライバ | LVGL Thread |

**スタック構造（LVGL Thread内）:**

```
LVGL Thread (lvgl_thread)
  +-- g_comms_i2c_device0 I2C Communications Device (rm_comms_i2c)
        +-- g_comms_i2c_bus0 I2C Communications Bus (rm_comms_i2c)
              +-- g_i2c_master0 I2C Master (r_iic_master) [Channel 0]
```

### 既存のI2Cピン設定

- IIC1モード: `iic1.mode.custom.free` (有効化済み)
- SDA/SCLのピン割り当て: **未設定**（リファレンスではP511/P512に割り当て済み）
- ピンシンボル名: SYS_I2C_SDA (P511), SYS_I2C_SCL (P512) は定義済み

### 既存のCamera Thread

```
Camera Thread (camera_thread)
  +-- g_vin0 Video Input (VIN) (r_vin)
        +-- g_mipi_csi0 MIPI CSI-2 (r_mipi_csi)
              +-- g_mipi_phy0 MIPI PHY Host (r_mipi_phy)
```

Camera Threadは存在しますが、I2Cマスタドライバはまだ追加されていません。

### HALコンテキスト

```
HAL/Common
  +-- g_ioport I/O Port (r_ioport)
  +-- rm_freertos_port
  +-- g_uart0 UART (r_sci_b_uart)
```

HALコンテキストにI2Cモジュールは存在しません。

---

## リファレンスプロジェクトとの比較

リファレンスプロジェクト (quickstart_ek_ra8p1_ep) のカメラ用I2Cマスタ設定:

| プロパティ | リファレンスの値 | 備考 |
|---|---|---|
| Name | g_board_i2c_master | ボード共通のI2Cマスタ |
| Channel | 1 | IIC1チャネル |
| Rate | Standard | 100kHz |
| Slave Address | 0x00 | 汎用設定（実行時にAPIで変更可能） |
| Address Mode | 7-Bit | |
| Timeout Mode | Short Mode | |
| Timeout SCL Low | Enabled | |
| Callback | board_i2c_master_callback | |
| Interrupt Priority | Priority 4 | |
| 配置場所 | HAL/Common (_hal.0) | スレッドに属さない共通モジュール |

**重要な相違点:**

1. リファレンスではカメラ用I2CマスタはHALコンテキスト（スレッド外）に配置されている
2. リファレンスのスレーブアドレスは 0x00（汎用。実行時にAPIで設定する設計）
3. リファレンスではチャネル1を使用（ピン: P511=SDA, P512=SCL）
4. 本プロジェクトでは既にチャネル0がタッチパネル用I2Cで使用されているため、チャネル1を使用する

---

## 前提条件の確認

### チャネルの競合確認

| チャネル | 現在の使用状況 | 用途 |
|---|---|---|
| IIC0 | g_i2c_master0 (タッチパネル用) | FT5X06タッチコントローラ |
| IIC1 | **未使用**（モード有効化済みだがI2Cマスタモジュール未追加） | カメラ用に使用可能 |

IIC1チャネルはモード（iic1.mode.custom.free）が有効化されていますが、I2Cマスタのドライバインスタンスはまだ追加されていません。チャネル1をカメラ用に使用します。タッチパネル用（チャネル0）との競合はありません。

### ピンの確認

リファレンスプロジェクトではIIC1のピン割り当ては以下のとおりです:

| ピン | 機能 | シンボル名 |
|---|---|---|
| P511 | IIC1 SDA1 | SYS_I2C_SDA |
| P512 | IIC1 SCL1 | SYS_I2C_SCL |

現プロジェクトでもP511/P512のシンボル名は SYS_I2C_SDA / SYS_I2C_SCL として定義済みですが、Pinsタブでのピン割り当て（iic1.scl1.p512, iic1.sda1.p511）が未設定です。手順の中で設定します。

> **注意**: P511/P512がEK-RA8P1ボード上でカメラ拡張ボードのI2Cラインに接続されていることを回路図で確認してください。リファレンスプロジェクト（quickstart_ek_ra8p1_ep）では SYS_I2C というシンボル名で使用されており、ボード上のシステムI2Cバス（カメラセンサーを含む）に対応しています。

### 前提Issue

- Issue #10 (VIN/MIPI CSI/MIPI PHY) が完了していること

---

## 手順 1: カメラ用I2Cマスタドライバの追加

リファレンスプロジェクトではカメラ用I2CマスタはHALコンテキスト（スレッド外の共通領域）に配置されています。本プロジェクトでも同じ構造で追加します。

### 1-1: モジュールの追加

1. e2 studioで `e2studio_CPU0/configuration.xml` を開く
2. **Stacks** タブを選択
3. 左側のツリーで **HAL/Common** を選択（スレッドではなくHALコンテキスト）
4. **New Stack** ボタンをクリック
5. **HAL Drivers** > **I2C Master (r_iic_master)** を選択して追加

> **備考**: カメラ用I2Cマスタをタッチパネル用の rm_comms_i2c のような共有バス構造ではなく、直接 r_iic_master として追加します。これはリファレンスプロジェクトと同じ構成です。OV5640はカメラ専用のI2Cバスを使用するため、共有バスの仕組みは不要です。

### 1-2: プロパティの設定

追加した I2C Master モジュールを選択し、Properties パネルで以下を設定します。

#### General

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Name | g_i2c_master_camera | カメラ用であることを明示。リファレンスでは g_board_i2c_master だが本プロジェクトではタッチパネル用と区別するため変更 |
| Channel | 1 | IIC1チャネル。リファレンスと同値 |
| Rate | Standard | 100kHz。OV5640はFast mode (400kHz) にも対応するが、まずはStandardで動作確認を推奨 |
| Custom Rate (bps) | 0 | Rate で Standard/Fast を選択した場合は0（自動計算） |
| Rise Time (ns) | 120 | リファレンスと同値 |
| Fall Time (ns) | 120 | リファレンスと同値 |
| Duty Cycle (%) | 50 | リファレンスと同値 |
| Slave Address | 0x3C | OV5640の7bitアドレス（8bitアドレス0x78を右シフト） |
| Address Mode | 7-Bit | OV5640は7bitアドレス |
| Timeout Mode | Short Mode | リファレンスと同値 |
| Timeout SCL Low | Enabled | リファレンスと同値。SCL Low状態のタイムアウト検出 |

> **スレーブアドレスについての補足**: リファレンスプロジェクトではスレーブアドレスが 0x00 に設定されていますが、これは汎用設計のためです。本プロジェクトではOV5640専用として 0x3C を設定します。実行時に R_IIC_MASTER_SlaveAddressSet() APIでアドレスを変更することも可能です。

#### Interrupts

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Callback | i2c_camera_callback | ユーザー定義コールバック関数名。リファレンスでは board_i2c_master_callback |
| Interrupt Priority Level | Priority 4 | リファレンスと同値。タッチパネル用I2C (Priority 12) より高い優先度 |

> **割り込み優先度について**: リファレンスではPriority 4（数値が小さいほど高優先度）が設定されています。カメラの初期化やレジスタ設定はリアルタイム性が求められるため、タッチパネル用（Priority 12）より高い優先度にしています。

---

## 手順 2: ピン設定

現在のプロジェクトではIIC1のモードは有効化されていますが、SDA/SCLのピン割り当てが未設定です。リファレンスプロジェクトに合わせてP511/P512にIIC1を割り当てます。

### 2-1: IIC1ペリフェラルのピン割り当て

1. **Pins** タブを選択
2. 左側のツリーで **Peripherals** > **Connectivity:IIC** > **IIC1** を選択
3. 以下のピンを設定

| ピン機能 | 設定値 | 備考 |
|---|---|---|
| SDA1 | P511 | リファレンスと同値。EK-RA8P1のSYS_I2C_SDAピン |
| SCL1 | P512 | リファレンスと同値。EK-RA8P1のSYS_I2C_SCLピン |

### 2-2: ピンの駆動能力設定

リファレンスプロジェクトではP511/P512のGPIO Drive Capacityが High に設定されています。

1. **Pins** タブの左側ツリーで **Ports** > **P5** > **P511** を選択
2. 以下を確認・設定

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Mode | Peripheral mode | ペリフェラル機能として使用 |
| Drive Capacity | High | I2C信号の駆動能力を確保 |

3. 同様に **P512** についても同じ設定を行う

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Mode | Peripheral mode | ペリフェラル機能として使用 |
| Drive Capacity | High | I2C信号の駆動能力を確保 |

> **注意**: I2Cはオープンドレイン方式のため、外部プルアップ抵抗が必要です。EK-RA8P1ボード上にSYS_I2Cバス用のプルアップ抵抗が実装されていることを回路図で確認してください。

---

## 手順 3: スタック構成の確認

全ての設定が完了すると、**Stacks** タブで以下の構成になっているはずです。

### HAL/Common

```
HAL/Common
  +-- g_ioport I/O Port (r_ioport)
  +-- rm_freertos_port
  +-- g_uart0 UART (r_sci_b_uart)
  +-- g_i2c_master_camera I2C Master (r_iic_master)   <-- 新規追加
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
  +-- g_comms_i2c_device0 I2C Communications Device (rm_comms_i2c)   <-- タッチパネル用（既存）
  |     +-- g_comms_i2c_bus0 I2C Communications Bus (rm_comms_i2c)
  |           +-- g_i2c_master0 I2C Master (r_iic_master) [Channel 0]
  +-- g_external_irq0 External IRQ (r_icu)
```

> **設計上の補足**: カメラ用I2CマスタをCamera Threadではなく HAL/Common に配置する理由は、リファレンスプロジェクトの設計に従ったものです。HALに配置することで、Camera Thread以外からもカメラセンサーのI2Cレジスタにアクセス可能になります（例: デバッグスレッドからの読み出し等）。ただし、複数スレッドからアクセスする場合はミューテックス等による排他制御が必要です。

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
| ra_gen/hal_data.c | I2Cマスタの構成データ（g_i2c_master_cameraのcfg構造体等）が追加される |
| ra_gen/hal_data.h | g_i2c_master_cameraのextern宣言が追加される |
| ra_gen/vector_data.c | IIC1チャネルの割り込みベクタ（TXI, RXI, TEI, ERI）が追加される |
| ra_gen/vector_data.h | 割り込みベクタの定義が更新される |
| ra_gen/pin_data.c | IIC1ピン設定（P511/P512）が追加される |

> **注意**: ra_gen/ 配下のファイルは自動生成されるため、手動で編集しないでください。

### ビルド確認

1. **Project** > **Build Project** を実行
2. ビルドがエラーなく完了することを確認
3. i2c_camera_callback のコールバック関数が未定義の場合、リンクエラーが発生します。その場合は src/ 配下に以下のスタブを作成してください:

```c
#include "hal_data.h"

/* I2C Camera callback */
void i2c_camera_callback(i2c_master_callback_args_t *p_args)
{
    FSP_PARAMETER_NOT_USED(p_args);
    /* TODO: I2C通信完了時の処理を実装
     *   - I2C_MASTER_EVENT_ABORTED: 通信エラー
     *   - I2C_MASTER_EVENT_RX_COMPLETE: 受信完了
     *   - I2C_MASTER_EVENT_TX_COMPLETE: 送信完了
     */
}
```

> **補足**: このコールバック関数は最終的にはカメラドライバ実装（後続Issue）で適切なイベント通知処理に置き換えます。ここでは空のスタブを用意してビルドを通すことが目的です。

---

## 受け入れ確認チェックリスト

- [ ] configuration.xml にカメラ用I2Cマスタドライバ (g_i2c_master_camera) が追加されている
- [ ] I2Cマスタのチャネルが 1 (IIC1) に設定されている
- [ ] スレーブアドレスが 0x3C (OV5640の7bitアドレス) に設定されている
- [ ] アドレスモードが 7-Bit に設定されている
- [ ] レートが Standard (100kHz) に設定されている
- [ ] コールバックが i2c_camera_callback に設定されている
- [ ] 割り込み優先度が Priority 4 に設定されている
- [ ] I2Cマスタが HAL/Common コンテキストに配置されている
- [ ] Pins タブで IIC1 の SDA1 が P511 に割り当てられている
- [ ] Pins タブで IIC1 の SCL1 が P512 に割り当てられている
- [ ] P511/P512 の Drive Capacity が High に設定されている
- [ ] タッチパネル用 I2C (チャネル0, g_i2c_master0) との競合がない
- [ ] Generate Project Content でエラーなくコード生成が完了する
- [ ] ビルドがエラーなく完了する

---

## 参照情報

| 項目 | 参照先 |
|---|---|
| リファレンスプロジェクト | reference_projects/quickstart_ek_ra8p1_ep/e2studio/configuration.xml |
| I2Cマスタ設定値（行番号） | リファレンス configuration.xml L475-L488 |
| HALスタック構成（行番号） | リファレンス configuration.xml L2014-L2018 |
| ピン設定（行番号） | リファレンス configuration.xml L2556-L2558, L2696-L2701 |
| ピンシンボル名（行番号） | リファレンス configuration.xml L2297-L2298 |
| IIC Masterモジュール設定（行番号） | リファレンス configuration.xml L2111-L2114 |
| Issue | https://github.com/grace2riku/mimamori-sense/issues/11 |
| 前提Issue | https://github.com/grace2riku/mimamori-sense/issues/10 |
