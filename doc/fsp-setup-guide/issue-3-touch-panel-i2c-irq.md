# e2 studio操作手順: F-001-2 タッチパネル(GT911)用I2C・IRQドライバの追加

## 対象Issue

- Issue #3: F-001-2: FSPプロジェクト設定 - タッチパネル(GT911)用I2C・IRQドライバの追加

## 前提条件

- Issue #2 (GLCDC・LVGL関連モジュールの追加) が完了していること
- e2studio_CPU0/configuration.xml に lvgl_thread (LVGL Thread) が存在すること
- LVGL、GLCDC、Dave2D関連モジュールが lvgl_thread に追加済みであること
- リファレンスプロジェクト: reference_projects/lv_port_renesas_ek_ra8p1/configuration.xml

## 概要

タッチパネル (GT911/FT5X06互換、I2Cスレーブアドレス: 0x38) を動作させるため、以下のFSPモジュールを lvgl_thread に追加する。

- COMMS I2C Device (通信デバイス層)
  - COMMS I2C Bus (通信バス層)
    - I2C Master (IIC) (HALドライバ層)
- External IRQ (タッチ割り込み検出)
- FreeRTOS Event Group (I2C通信同期用)
- FreeRTOS Binary Semaphore (タッチ割り込み通知用)

リファレンスプロジェクトのスタック構成 (configuration.xml 行 779-784) に基づく。

---

## 手順1: COMMS I2C Device モジュールの追加

COMMS I2C Device を追加すると、依存モジュールとして COMMS I2C Bus と I2C Master (IIC) が自動的にスタックに追加される。

1. e2 studio で e2studio_CPU0/configuration.xml を開く
2. **Stacks** タブを選択する
3. 左側のスレッド一覧から **LVGL Thread (lvgl_thread)** を選択する
4. **New Stack** ボタンをクリックする
5. **Middleware > Communications > I2C Communications Device (rm_comms_i2c)** を選択する
6. 自動的に **COMMS I2C Bus** と **I2C Master (r_iic_master)** が子モジュールとして追加される

### 手順1-1: COMMS I2C Device の設定

スタック上で **g_comms_i2c_device0 I2C Communications Device (rm_comms_i2c)** を選択し、プロパティパネルで以下を設定する。

| プロパティ名 | 設定値 | 備考 |
|-------------|--------|------|
| Name | g_comms_i2c_device0 | リファレンスと同一 |
| Semaphore Timeout (RTOS only) | 0xFFFFFFFF | 無限待ち (デフォルト) |
| Slave Address | 0x38 | GT911/FT5X06のI2Cアドレス |
| Address Mode | 7-Bit | リファレンスと同一 |
| Callback | comms_i2c_callback | I2C通信完了コールバック |

> **リファレンス参照**: configuration.xml 行 719-724 の設定値をそのまま使用。

### 手順1-2: COMMS I2C Bus の設定

スタック上で **g_comms_i2c_bus0 I2C Communications Bus (rm_comms_i2c)** を選択し、プロパティパネルで以下を設定する。

| プロパティ名 | 設定値 | 備考 |
|-------------|--------|------|
| Name | g_comms_i2c_bus0 | リファレンスと同一 |
| Bus Timeout | 0xFFFFFFFF | 無限待ち (デフォルト) |
| Blocking Semaphore | Use | FreeRTOS セマフォを使用した排他制御 |
| Bus Recursive Mutex | Use | FreeRTOS 再帰ミューテックスを使用 |
| Channel | 1 | バスインスタンス番号 |
| Rate | Fast mode | 400kbps |

> **リファレンス参照**: configuration.xml 行 507-513 の設定値をそのまま使用。

### 手順1-3: I2C Master (r_iic_master) の設定

スタック上で **g_i2c_master0 I2C Master (r_iic_master)** を選択し、プロパティパネルで以下を設定する。

| プロパティ名 | 設定値 | 備考 |
|-------------|--------|------|
| Name | g_i2c_master0 | リファレンスと同一 |
| Channel | 0 | IICペリフェラルチャネル |
| Rate | Standard | 100kbps |
| Custom Rate (bps) | 0 | 使用しない |
| Rise Time (ns) | 120 | リファレンスと同一 |
| Fall Time (ns) | 120 | リファレンスと同一 |
| Duty Cycle (%) | 50 | リファレンスと同一 |
| Slave Address | 0x38 | GT911/FT5X06のI2Cアドレス |
| Address Mode | 7-Bit | リファレンスと同一 |
| Timeout Mode | Short Mode | リファレンスと同一 |
| Timeout during SCL Low | Enabled | リファレンスと同一 |
| Callback | NULL | COMMS層がコールバックを管理するためNULL |
| Interrupt Priority Level | Priority 12 | リファレンスと同一 |

> **リファレンス参照**: configuration.xml 行 515-528 の設定値をそのまま使用。
>
> **注意**: I2C Master の Channel=0 と、ピン設定で有効化する IIC チャネルの関係はボード回路図で確認すること。リファレンスプロジェクトではピン設定上 IIC1 (P511/P512) が有効化されている。Channel プロパティの値とピン設定の IIC チャネルの整合性について、e2 studio 上で警告が表示される場合は、ボードの回路図に合わせて Channel 値を調整すること。

---

## 手順2: External IRQ モジュールの追加

1. **Stacks** タブで **LVGL Thread (lvgl_thread)** が選択されていることを確認する
2. **New Stack** ボタンをクリックする
3. **HAL Drivers > Input > External IRQ (r_icu)** を選択する

### External IRQ の設定

スタック上で **g_external_irq0 External IRQ (r_icu)** を選択し、プロパティパネルで以下を設定する。

| プロパティ名 | 設定値 | 備考 |
|-------------|--------|------|
| Name | g_external_irq0 | リファレンスと同一 |
| Channel | 19 | タッチパネル割り込みチャネル |
| Trigger | Falling | タッチ検出時の立ち下がりエッジ |
| Digital Filtering | Enabled | チャタリング防止 |
| Digital Filtering Sample Clock | PCLK | リファレンスと同一 |
| Clock Source Divider | 1 | 分周なし |
| Callback | touch_irq_callback | タッチ割り込みコールバック関数 |
| Interrupt Priority Level | Priority 12 | リファレンスと同一 |

> **リファレンス参照**: configuration.xml 行 530-538 の設定値をそのまま使用。
>
> **注意**: IRQチャネル19はリファレンスプロジェクトの値。ボードの回路図でタッチパネルのINT信号が接続されているピンとIRQチャネルの対応を確認すること。

---

## 手順3: FreeRTOS Event Group の追加 (I2C同期用)

1. **Stacks** タブで **LVGL Thread (lvgl_thread)** が選択されていることを確認する
2. **New Stack** ボタンをクリックする
3. **RTOS > FreeRTOS > FreeRTOS Event Group** を選択する (カテゴリ内に見つからない場合は **New Stack > RTOS > FreeRTOS Object > Event Flags** を探す)

### FreeRTOS Event Group の設定

| プロパティ名 | 設定値 | 備考 |
|-------------|--------|------|
| Symbol | g_i2c_event_group | I2C通信完了イベントの同期用 |
| Memory Allocation | Static | 静的メモリ割り当て |

> **リファレンス参照**: configuration.xml 行 758-761 の設定値をそのまま使用。

---

## 手順4: FreeRTOS Binary Semaphore の追加 (タッチ割り込み用)

1. **Stacks** タブで **LVGL Thread (lvgl_thread)** が選択されていることを確認する
2. **New Stack** ボタンをクリックする
3. **RTOS > FreeRTOS > FreeRTOS Binary Semaphore** を選択する

### FreeRTOS Binary Semaphore の設定

| プロパティ名 | 設定値 | 備考 |
|-------------|--------|------|
| Symbol | g_irq_binary_semaphore | タッチ割り込みからタスクへの通知用 |
| Memory Allocation | Static | 静的メモリ割り当て |

> **リファレンス参照**: configuration.xml 行 762-764 の設定値をそのまま使用。

---

## 手順5: ピン設定 (I2C)

1. **Pins** タブを選択する
2. 左側のペリフェラル一覧から **Connectivity > IIC > IIC1** を選択する
3. 以下のピンを設定する

| ピン機能 | ピン番号 | 設定 | 備考 |
|---------|---------|------|------|
| SDA1 | P511 | IIC1 SDA1 に割り当て | SYS_I2C_SDA |
| SCL1 | P512 | IIC1 SCL1 に割り当て | SYS_I2C_SCL |

4. P511 および P512 のピン属性を以下のように設定する

| ピン | プロパティ | 設定値 | 備考 |
|-----|-----------|--------|------|
| P511 | Drive Capacity | Medium | リファレンスと同一 |
| P511 | Mode | Peripheral | リファレンスと同一 |
| P511 | Output Type | N-Channel Open Drain | I2Cバス要件 |
| P512 | Drive Capacity | Medium | リファレンスと同一 |
| P512 | Mode | Peripheral | リファレンスと同一 |
| P512 | Output Type | N-Channel Open Drain | I2Cバス要件 |

> **リファレンス参照**: configuration.xml 行 1292-1294, 1497-1504 の設定値をそのまま使用。
>
> **注意**: 現在のプロジェクトでは iic1.mode.custom.free が既に設定されている (configuration.xml 行 1092)。P511 (SYS_I2C_SDA)、P512 (SYS_I2C_SCL) のシンボリック名も既に登録済み (行 888-889)。ピン割り当て (SDA1/SCL1) とピン属性 (Output Type等) の設定が必要。

---

## 手順6: ピン設定 (External IRQ)

1. **Pins** タブを選択する
2. 左側のペリフェラル一覧から **ICU > IRQ** を展開し、IRQ19 に対応するピンを確認する
3. タッチパネルの INT 信号が接続されているピンを IRQ として有効化する

> **重要**: IRQ19 に対応するピンはボードの回路図で確認が必要。リファレンスプロジェクトでは複数のピンで gpio_irq が有効化されている (P000, P006, P008, P009, P012, P107, P111, P200)。パラレルグラフィックス拡張ボードのタッチパネル INT 信号の接続先を回路図で確認し、該当ピンの GPIO IRQ を Enabled に設定すること。

該当ピンの設定:

| ピン | プロパティ | 設定値 | 備考 |
|-----|-----------|--------|------|
| (要確認) | Input/Output | Input | 入力モード |
| (要確認) | GPIO IRQ | Enabled | IRQ機能を有効化 |
| (要確認) | Mode | GPIO mode (Input) | GPIO入力モード |

---

## 手順7: FreeRTOS カーネル設定の確認

COMMS I2C Bus で Blocking Semaphore と Bus Recursive Mutex を使用するため、FreeRTOS のカーネル設定で以下が有効になっていることを確認する。

1. **Stacks** タブを選択する
2. いずれかの **Thread** を選択する (例: LVGL Thread)
3. プロパティパネルで以下を確認する

| プロパティ名 | 必要な設定値 | 備考 |
|-------------|------------|------|
| Use Mutexes | Enabled | Bus Recursive Mutex に必要 |
| Use Recursive Mutexes | Enabled | Bus Recursive Mutex に必要 |
| Use Counting Semaphores | Enabled | Blocking Semaphore に必要 |

> **注意**: FreeRTOS カーネル設定はグローバル設定のため、どのスレッドから変更しても全スレッドに適用される。

---

## 最終手順: コード生成とビルド確認

1. **Stacks** タブで追加したモジュールの構成を確認する。以下のスタック構成になっていること:

```text
LVGL Thread (lvgl_thread)
+-- FreeRTOS Heap 4
+-- LVGL (既存)
|   +-- LVGL Port (rm_lvgl_port) (既存)
|       +-- GLCDC (既存)
|       +-- D/AVE 2D (既存)
|           +-- D/AVE 2D Driver (既存)
+-- I2C Communications Device (rm_comms_i2c) [新規]
|   +-- I2C Communications Bus (rm_comms_i2c) [新規]
|       +-- I2C Master (r_iic_master) [新規]
+-- External IRQ (r_icu) [新規]
+-- FreeRTOS Event Group [新規]
+-- FreeRTOS Binary Semaphore [新規]
```

2. **Generate Project Content** ボタンをクリックしてコード生成を実行する
3. エラーや警告が表示されないことを確認する
4. プロジェクトをビルドしてエラーがないことを確認する

---

## 生成されるファイル (参考)

コード生成後、ra_gen/ ディレクトリに以下のファイルが生成・更新される (直接編集禁止):

- I2C Master 関連の初期化コード
- COMMS I2C Bus/Device 関連の初期化コード
- External IRQ 関連の初期化コード
- FreeRTOS Event Group / Binary Semaphore の宣言

## 実装時の注意事項

1. **コールバック関数の実装が必要**:
   - comms_i2c_callback() - I2C通信完了時のコールバック (src/ 配下に手動実装)
   - touch_irq_callback() - タッチ割り込み発生時のコールバック (src/ 配下に手動実装)

2. **I2C MasterのChannel値に関する注意**:
   リファレンスプロジェクトでは I2C Master の Channel=0 が設定されているが、ピン設定では IIC1 (P511/P512) が使用されている。COMMS I2C ミドルウェア経由で使用する場合、COMMS I2C Bus の Channel プロパティとピン設定の IIC チャネルの整合性を確認すること。e2 studio 上でエラーや警告が表示された場合は、ボードの回路図に基づいて適切な値に修正すること。

3. **RA8P1 EK ボードの場合**:
   - I2C: P511 (SYS_I2C_SDA) / P512 (SYS_I2C_SCL) は IIC1 チャネルに接続
   - タッチ割り込み: IRQ チャネル19 の対応ピンを回路図で確認すること