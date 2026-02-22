# e2 studio操作手順: S-001-1: FSPプロジェクト設定 - SDRAMコントローラモジュールの追加

## 対象Issue

- Issue: [#29 S-001-1: FSPプロジェクト設定 - SDRAMコントローラモジュールの追加](https://github.com/grace2riku/mimamori-sense/issues/29)
- 対象SDRAM: IS42S16160J-6BLI (32MB SDRAM)
- ボード: EK-RA8P1

## 前提条件

- e2 studio 2025-12 (25.12.0) がインストール済みであること
- FSP 6.2.0 以降がインストール済みであること
- mimamori-sense プロジェクトがインポート済みであること
- マルチコアプロジェクト構成 (e2studio/, e2studio_CPU0/, e2studio_CPU1/) を理解していること

## 現状分析結果

リファレンスプロジェクト (lv_port_renesas_ek_ra8p1 および quickstart_ek_ra8p1_ep) と現在のプロジェクトを比較した結果、以下の設定状態が確認されました。

### 設定済み (変更不要) の項目

| 項目 | 状態 | 備考 |
|------|------|------|
| BSP SDRAM有効化 | 設定済み | config.bsp.fsp.sdram.enabled = enabled |
| SDRAMタイミングパラメータ | 設定済み | リファレンスと完全一致 |
| SDRAMピンアサイン (solution.xml) | 設定済み | 全ピン設定済み |
| SDRAMピンシンボリック名 (solution.xml) | 設定済み | リファレンスと一致 |
| SDCLKOUT有効化 | 設定済み | sdclkout.enable = enabled |

### 確認・検討が必要な項目

| 項目 | 現在の設定 | リファレンス (lv_port) の設定 | 差異 |
|------|-----------|---------------------------|------|
| BCLKAソース | PLL2Q | PLL1R | 異なるが最終周波数は同等 (後述) |
| BCLKA分周比 | 6 | 3 | BCLKAソースとの組合せで最終周波数は同等 |
| EBCLKソース | BCLK | BCLKA (As) | **異なる (要確認)** |
| EBCLK出力 | 有効 | **Disabled (0Hz)** | **異なる (要確認)** |

> **注記**: リファレンス (lv_port) ではEBCLKは**Disabled (0Hz)** です。SDCLKはBCLKAから直接供給され (~133.333MHz)、EBCLKとは独立しています。

## 手順1: BSP SDRAMタイミングパラメータの確認 (確認のみ)

BSP (Board Support Package) レベルのSDRAMタイミングパラメータは既に設定済みですが、設定内容を目視確認します。

1. e2 studioで `e2studio_CPU0/configuration.xml` を開く
2. **BSP** タブを選択する
3. **RA8P1 Family** > **Board Support Package** のプロパティを展開する
4. **SDRAM** セクションを見つける
5. 以下のパラメータが正しく設定されていることを確認する

### SDRAMタイミングパラメータ一覧

| プロパティ名 | 設定値 | 説明 | IS42S16160J-6BLI仕様との対応 |
|-------------|--------|------|---------------------------|
| SDRAM Enabled | Enabled | SDRAMコントローラ有効化 | - |
| tRAS (Row Active to Precharge) | 6 | 行アクティブからプリチャージまでのサイクル数 | tRAS = 42ns min |
| tRCD (RAS-to-CAS Delay) | 3 | RAS-CAS遅延サイクル数 | tRCD = 18ns min |
| tRP (Row Precharge Time) | 3 | 行プリチャージ時間サイクル数 | tRP = 18ns min |
| tWR (Write Recovery Time) | 2 | ライトリカバリー時間サイクル数 | tWR = 12ns min |
| tCL (CAS Latency) | 3 | CASレイテンシ | CL=3 (周波数に依存) |
| tRFC (Auto Refresh Cycle Time) | 937 | オートリフレッシュサイクル時間 (ns) | tRFC = 60ns (内部計算用値) |
| tREFW (Refresh Window) | 8 | リフレッシュウィンドウ | 8192行リフレッシュ |
| ARFI (Auto Refresh Interval) | 10 | オートリフレッシュ間隔 | - |
| ARFC (Auto Refresh Count) | 8 | オートリフレッシュ回数 | - |
| PRC (Precharge Cycles) | 3 | プリチャージサイクル数 | - |
| Address Shift | 9 | アドレスシフト量 | 9ビット (512列、32bitバス幅対応) |
| Endian Mode | Little | エンディアンモード | - |
| Continuous Access Mode | Enabled | 連続アクセスモード | - |
| Bus Width | 32 | バス幅 | 32bit (2チップx16bit) |

> **注記**: これらの値は2つのリファレンスプロジェクト (lv_port_renesas_ek_ra8p1、quickstart_ek_ra8p1_ep) と完全に一致しています。変更は不要です。

## 手順2: SDRAMピンアサインの確認 (確認のみ)

マルチコアプロジェクトではピン設定は `e2studio/solution.xml` で管理されます。既に全SDRAMピンが設定済みです。

1. e2 studioで `e2studio/solution.xml` を開く (**ソリューションプロジェクト側**)
2. **Pins** タブを選択する
3. 左パネルで **Peripherals** > **Memory** > **SDRAM** を選択する
4. 以下のピンアサインが正しいことを確認する

### データバス (DQ0-DQ31: 32本)

| 信号名 | ピン | シンボリック名 |
|--------|------|---------------|
| DQ0 | P302 | SDRAM_DQ0 |
| DQ1 | P301 | SDRAM_DQ1 |
| DQ2 | P300 | SDRAM_DQ2 |
| DQ3 | P112 | SDRAM_DQ3 |
| DQ4 | P113 | SDRAM_DQ4 |
| DQ5 | P114 | SDRAM_DQ5 |
| DQ6 | P115 | SDRAM_DQ6 |
| DQ7 | P609 | SDRAM_DQ7 |
| DQ8 | PA11 | SDRAM_DQ8 |
| DQ9 | PA12 | SDRAM_DQ9 |
| DQ10 | PA13 | SDRAM_DQ10 |
| DQ11 | PA14 | SDRAM_DQ11 |
| DQ12 | P610 | SDRAM_DQ12 |
| DQ13 | P611 | SDRAM_DQ13 |
| DQ14 | P612 | SDRAM_DQ14 |
| DQ15 | P613 | SDRAM_DQ15 |
| DQ16 | PC14 | SDRAM_DQ16 |
| DQ17 | PC13 | SDRAM_DQ17 |
| DQ18 | PC12 | SDRAM_DQ18 |
| DQ19 | PC11 | SDRAM_DQ19 |
| DQ20 | PC10 | SDRAM_DQ20 |
| DQ21 | PC09 | SDRAM_DQ21 |
| DQ22 | PC08 | SDRAM_DQ22 |
| DQ23 | PC07 | SDRAM_DQ23 |
| DQ24 | PC06 | SDRAM_DQ24 |
| DQ25 | PC05 | SDRAM_DQ25 |
| DQ26 | PC04 | SDRAM_DQ26 |
| DQ27 | PC03 | SDRAM_DQ27 |
| DQ28 | PC02 | SDRAM_DQ28 |
| DQ29 | PC01 | SDRAM_DQ29 |
| DQ30 | PC00 | SDRAM_DQ30 |
| DQ31 | P607 | SDRAM_DQ31 |

### アドレスバス (A2-A16: 15本)

FSPでは32bitバス幅の場合、アドレスラインは内部でシフトされます。シンボリック名の `_32BIT` サフィックスは、32bitバスモードでのアドレス対応を示しています。

| 信号名 | ピン | シンボリック名 |
|--------|------|---------------|
| A2 | PA03 | SDRAM_A0_32BIT |
| A3 | PA02 | SDRAM_A1_32BIT |
| A4 | PA01 | SDRAM_A2_32BIT |
| A5 | PA00 | SDRAM_A3_32BIT |
| A6 | P503 | SDRAM_A4_32BIT |
| A7 | P504 | SDRAM_A5_32BIT |
| A8 | P505 | SDRAM_A6_32BIT |
| A9 | P506 | SDRAM_A7_32BIT |
| A10 | P507 | SDRAM_A8_32BIT |
| A11 | P508 | SDRAM_A9_32BIT |
| A12 | P509 | SDRAM_A10_32BIT |
| A13 | P510 | SDRAM_A11_32BIT |
| A14 | P608 | SDRAM_A12_32BIT |
| A15 | PD00 | SDRAM_BA0 |
| A16 | PC15 | SDRAM_BA1 |

### 制御信号 (10本)

| 信号名 | ピン | シンボリック名 |
|--------|------|---------------|
| SDCLK | PA15 | SDRAM_CLK |
| SDCS | P813 | SDRAM_CS |
| RAS | PA10 | SDRAM_RAS |
| CAS | PA09 | SDRAM_CAS |
| WE | PA08 | SDRAM_WE |
| CKE | PA06 | SDRAM_CKE |
| DQM0 | P614 | SDRAM_DQM0 |
| DQM1 | PA05 | SDRAM_DQM1 |
| DQM2 | P615 | SDRAM_DQM2 |
| DQM3 | PA04 | SDRAM_DQM3 |

> **注記**: 上記のピンアサインは、リファレンスプロジェクト2つ (lv_port_renesas_ek_ra8p1、quickstart_ek_ra8p1_ep) と完全に一致しており、EK-RA8P1ボードの回路図に準拠しています。

## 手順3: SDRAMクロック設定の確認と調整 (要確認)

マルチコアプロジェクトではクロック設定は `e2studio/solution.xml` の Clocks タブから変更します。

1. e2 studioで `e2studio/solution.xml` を開く
2. **Clocks** タブを選択する
3. 以下のクロック設定を確認する

### 現在のクロック設定とリファレンスとの比較

| 設定項目 | 現在の設定 | リファレンス (lv_port) | リファレンス (quickstart) |
|---------|-----------|--------------------|-----------------------|
| BCLK分周比 | /8 | /8 | /8 |
| BCLKAソース | PLL2Q | PLL1R | Disabled |
| BCLKA分周比 | /6 | /3 | /6 |
| SDCLKOUT | Enabled | Enabled | Enabled |
| EBCLKソース | BCLK | BCLKA (As) | BCLK |
| EBCLK出力 | 有効 | **Disabled (0Hz)** | 有効 |

### SDCLK周波数の計算

**現在のプロジェクト (PLL2Qソース):**
- XTAL = 24MHz
- PLL2 = 24MHz / 3 x 300 = 2400MHz
- PLL2Q = 2400MHz / 3 = 800MHz
- BCLKA = 800MHz / 6 = 133.33MHz
- SDCLKはBCLKAから供給 → **SDCLK = 133.33MHz**

> **注記**: 現在のプロジェクトもBCLKA = 133.33MHzとなっているため、SDCLKはリファレンスと同等の周波数で動作していると考えられます。

**リファレンス lv_port (PLL1Rソース):**
- PLL1R = 400MHz
- BCLKA = 400MHz / 3 = 133.33MHz
- SDCLKはBCLKAから供給 → **SDCLK = ~133.333MHz**
- EBCLK = **Disabled (0Hz)**（未使用）

> **重要**: リファレンス (lv_port) ではEBCLKはDisabled (0Hz) です。SDCLKはEBCLK経由ではなく、BCLKAから直接供給されています。現在のプロジェクトでもBCLKA = 133.33MHzとなっているため、SDCLK周波数はリファレンスと同等です。

### 確認・検討事項

現在のプロジェクトのEBCLK設定がリファレンスと異なります（有効 vs Disabled）。リファレンスに合わせてEBCLKをDisabledに変更するかどうかは、EBCLK出力を使用する他のペリフェラルの有無に依存します。

> **注意**: クロック設定の変更は他のペリフェラルに影響する可能性があります。変更前に現在の設定をメモまたはスクリーンショットで記録してください。

リファレンスに合わせる場合の変更手順:

1. e2 studioで `e2studio/solution.xml` を開く
2. **Clocks** タブを選択する
3. クロックツリー図で以下の項目を変更する

| 設定項目 | 変更前 | 変更後 | 操作 |
|---------|--------|--------|------|
| EBCLK出力 | 有効 | Disabled | EBCLKをDisabledに変更 |

> **判断ポイント**: EBCLK出力を他のペリフェラルで使用していない場合は、リファレンスに合わせてDisabledにしても問題ありません。SDCLK自体はBCLKAから供給されるため、EBCLK設定はSDRAM動作に直接影響しません。

## 手順4: Generate Project Content

1. e2 studioで `e2studio_CPU0/configuration.xml` を開く
2. 画面右上の **Generate Project Content** ボタンをクリックする
3. コード生成が完了するまで待つ
4. Console ビューにエラーがないことを確認する

## 手順5: ビルド確認

1. e2 studioのメニューから **Project** > **Build Project** を選択する
   - または、e2studio_CPU0 プロジェクトを右クリック > **Build Project**
2. ビルドエラーがないことを確認する
3. ビルド成功後、以下のファイルが正しく生成されていることを確認する
   - `e2studio_CPU0/ra_gen/bsp_clock_cfg.h` (SDCLK関連の定義)
   - `e2studio_CPU0/ra_gen/bsp_pin_cfg.h` (SDRAMピン定義)

## 設定概要サマリ

### 設定済み (変更不要)

1. **BSP SDRAMタイミングパラメータ**: リファレンスプロジェクトと完全一致。IS42S16160J-6BLI データシートに準拠。
2. **SDRAMピンアサイン**: `e2studio/solution.xml` に全ピン (データ32本、アドレス15本、制御10本) が設定済み。リファレンスと完全一致。
3. **SDCLKOUT有効化**: 有効済み。

### 確認・検討が必要

1. **EBCLK設定**: リファレンス (lv_port) ではEBCLKがDisabled (0Hz) ですが、現在のプロジェクトでは有効です。SDCLK自体はBCLKAから供給されるためSDRAM動作に影響しませんが、リファレンスに合わせるかどうかを検討してください。

## リファレンスプロジェクト情報

| プロジェクト名 | 用途 | SDRAMモード |
|-------------|------|------------|
| lv_port_renesas_ek_ra8p1 | LVGL描画用 | custom.free / SDCLK=133MHz |
| quickstart_ek_ra8p1_ep | クイックスタート | custom.free / BCLKA=Disabled |

## 参考情報

### SDRAMメモリマップ

EK-RA8P1のSDRAMは以下のアドレス範囲にマッピングされます:
- ベースアドレス: `0x68000000`
- サイズ: 32MB (`0x02000000`)
- 範囲: `0x68000000` - `0x69FFFFFF`

### リンカスクリプトでのSDRAMセクション定義

リファレンスプロジェクトでは、ディスプレイのフレームバッファに以下のSDRAMセクションが使用されています:
- `.sdram_noinit_nocache` - Input Layer 0 (キャッシュ無効のノーキャッシュ領域)
- `.sdram_noinit` - Input Layer 1 (キャッシュ有効領域)

SDRAMにデータを配置する場合は、リンカスクリプト (`e2studio_CPU0/script/`) にSDRAMセクションの定義を追加する必要があります。

---

*本手順書は Issue #29 に対応するFSP設定ガイドです。*
*リファレンスプロジェクト: lv_port_renesas_ek_ra8p1 / quickstart_ek_ra8p1_ep*
*生成日: 2026-02-21*
