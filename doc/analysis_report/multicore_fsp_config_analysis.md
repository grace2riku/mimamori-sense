# マルチコアプロジェクト FSP設定 解析レポート

**作成日**: 2026-02-11
**対象プロジェクト**:
- `e2studio` (マルチコア親プロジェクト)
- `e2studio_CPU0` (Cortex-M85コア)
- `e2studio_CPU1` (Cortex-M33コア)
- `reference_projects/quickstart_ek_ra8p1_ep` (リファレンス)

---

## 1. プロジェクト概要

### 1.1 マルチコアプロジェクト構成

```
e2studio/                    ← マルチコア親プロジェクト（ソリューション管理）
├── solution.xml
├── .secure_xml
└── .project

e2studio_CPU0/               ← Cortex-M85 (メインコア)
├── configuration.xml        ← FSP設定
├── ra_gen/                  ← FSP自動生成コード
└── src/                     ← アプリケーションコード

e2studio_CPU1/               ← Cortex-M33 (サブコア)
├── configuration.xml        ← FSP設定
├── ra_gen/                  ← FSP自動生成コード
└── src/                     ← アプリケーションコード
```

### 1.2 現在の構成サマリー

| 項目 | e2studio_CPU0 | e2studio_CPU1 | quickstart |
|------|--------------|--------------|------------|
| MCUコア | Cortex-M85 | Cortex-M33 | Cortex-M85 |
| RTOS | FreeRTOS | FreeRTOS | FreeRTOS |
| スレッド数 | 1 (Blinky) | 1 (Blinky) | 複数 |
| FSPモジュール数 | 2 | 2 | 20+ |

---

## 2. e2studio_CPU0/CPU1のFSP設定一覧

### 2.1 使用FSPモジュール

#### e2studio_CPU0

| No. | モジュール | インスタンス名 | 用途 |
|-----|-----------|--------------|------|
| 1 | I/O Port (r_ioport) | g_ioport | GPIO制御 |
| 2 | FreeRTOS Port (rm_freertos_port) | - | RTOS統合 |

#### e2studio_CPU1

| No. | モジュール | インスタンス名 | 用途 |
|-----|-----------|--------------|------|
| 1 | I/O Port (r_ioport) | g_ioport | GPIO制御 |
| 2 | FreeRTOS Port (rm_freertos_port) | - | RTOS統合 |

### 2.2 プロジェクト新規作成時からの変更点

e2studio_CPU0/CPU1は**EK-RA8P1 Blinky with FreeRTOS**テンプレートをベースに作成されており、以下の変更が確認されました：

#### 変更点一覧（e2studio_CPU0）

| No. | 設定メニューの場所 | 設定変更内容 | 設定変更理由 | FSP出力コード | 利用箇所 |
|-----|-------------------|-------------|-------------|--------------|---------|
| 1 | BSP > Board > RA8P1 EK | ボード選択 | EK-RA8P1評価ボード使用 | `bsp_cfg.h` | 全体 |
| 2 | BSP > Properties > Main Oscillator | XTAL 24MHz | ボード搭載水晶 | `bsp_clock_cfg.h` | クロック初期化 |
| 3 | BSP > Clocks > PLL | PLL1 x250, PLL2 x300 | M85@1GHz動作 | `bsp_clock_cfg.h` | クロック初期化 |
| 4 | BSP > Clocks > CPUCLK | 1GHz (PLL1P/1) | M85最大性能 | `bsp_clock_cfg.h` | CPUクロック |
| 5 | BSP > Clocks > CPUCLK1 | 250MHz (PLL1P/4) | M33コア用 | `bsp_clock_cfg.h` | サブコアクロック |
| 6 | Stacks > Blinky Thread | スタック512バイト | LED点滅用最小構成 | `blinky_thread.c` | `blinky_thread_entry.c` |
| 7 | Pins > 多数 | ボードピン設定 | 周辺機能接続 | `pin_data.c` | GPIO/周辺機能 |

#### ピン設定詳細（主要な周辺機能）

| 周辺機能 | ピン数 | 用途 |
|---------|--------|------|
| LCD_GRAPHICS | 24本 | LCDデータ/制御 |
| BUS | 32本 | 外部バス（SDRAM） |
| OSPI | 12本 | Octa-SPI Flash |
| ETHER_RGMII | 12本 | Ethernet |
| IIC | 4本 | I2C (2チャネル) |
| DEBUG | 4本 | JTAG/SWD |
| MIPI | 1本 | MIPIカメラ |
| その他 | 多数 | USB, SSI, PDM等 |

### 2.3 クロック設定詳細

```
XTAL (24MHz)
    ↓
PLL1 (x250 = 2GHz)
├── PLL1P (/2 = 1GHz)  → CPUCLK (M85)
│                      → CPUCLK1 (/4 = 250MHz, M33)
├── PLL1Q (/6 = 333MHz) → SPICLK, OCTACLK
└── PLL1R (/5 = 400MHz)

PLL2 (x300 = 2.4GHz)
├── PLL2P (/4 = 600MHz) → GPTCLK
├── PLL2Q (/3 = 800MHz) → BCLKA (SDRAM)
└── PLL2R (/5 = 480MHz) → SCICLK, LCDCLK, USBCLK, ADCCLK
```

### 2.4 FreeRTOS設定

| 設定項目 | e2studio_CPU0 | e2studio_CPU1 |
|---------|--------------|--------------|
| Blinkyスレッドスタック | 512バイト | 512バイト |
| スレッド優先度 | 1 | 1 |
| 静的メモリ割り当て | 有効 | 有効 |
| TrustZone対応 | 有効 | 有効 |

### 2.5 マルチコア連携コード

```c
// blinky_thread_entry.c (CPU0)
#if (0 == _RA_CORE) && (1 == BSP_MULTICORE_PROJECT) && !BSP_TZ_NONSECURE_BUILD
    R_BSP_SecondaryCoreStart();  // CPU1を起動
#endif
```

---

## 3. quickstartプロジェクトのFSP設定

### 3.1 使用FSPモジュール一覧

| No. | モジュール | インスタンス名 | 用途 |
|-----|-----------|--------------|------|
| 1 | I/O Port (r_ioport) | g_ioport | GPIO制御 |
| 2 | FreeRTOS Port (rm_freertos_port) | - | RTOS統合 |
| 3 | FreeRTOS Heap 4 | - | 動的メモリ管理 |
| 4 | I2C Master (r_iic_master) | g_i2c_master | タッチ/センサー通信 |
| 5 | Graphics LCD (r_glcdc) | g_plcd_display | LCD表示 |
| 6 | D/AVE 2D (r_drw) | drw | 2Dグラフィックス |
| 7 | TES Dave2D | - | 描画アクセラレータ |
| 8 | GPT Timer (r_gpt) | 複数インスタンス | PWM/タイミング |
| 9 | UART (r_sci_b_uart) | g_uart | デバッグ出力 |
| 10 | OSPI (r_ospi_b) | - | Octa-SPI Flash |
| 11 | ADC (r_adc_b) | - | アナログ入力 |
| 12 | External IRQ (r_icu) | 複数 | タッチ/ボタン割込み |
| 13 | VIN (r_vin) | - | カメラ入力 |
| 14 | MIPI CSI (r_mipi_csi) | - | カメラインターフェース |
| 15 | MIPI PHY | - | MIPI物理層 |

### 3.2 quickstartのスレッド構成

| スレッド名 | 役割 |
|-----------|------|
| blinky_thread | LED点滅 |
| display_thread | LCD表示制御 |
| camera_thread | カメラ入力処理 |
| main_menu_thread | メインメニューUI |
| board_mon_thread | ボード監視 |
| tp_thread | タッチパネル処理 |

---

## 4. e2studio_CPU0 vs quickstart FSP設定比較

### 4.1 モジュール差分一覧

| モジュール | e2studio_CPU0 | quickstart | 追加が必要 |
|-----------|--------------|------------|-----------|
| I/O Port | ○ | ○ | - |
| FreeRTOS Port | ○ | ○ | - |
| FreeRTOS Heap 4 | × | ○ | **要追加** |
| I2C Master | × | ○ | **要追加** |
| Graphics LCD | × | ○ | **要追加** |
| D/AVE 2D | × | ○ | **要追加** |
| TES Dave2D | × | ○ | **要追加** |
| GPT Timer | × | ○ | **要追加** |
| UART | × | ○ | **要追加** |
| OSPI | × | ○ | **要追加** |
| ADC | × | ○ | オプション |
| External IRQ | × | ○ | **要追加** |
| VIN | × | ○ | オプション |
| MIPI CSI/PHY | × | ○ | オプション |

### 4.2 設定値の差分

#### ピン設定
- **共通**: LCD, BUS, OSPI, IIC等の基本ピン設定は同一
- **差分**: quickstartは追加の割込みピン設定あり

#### クロック設定
- **共通**: PLL設定はほぼ同一（M85@1GHz）
- **差分**: 軽微な分周比の違いのみ

#### メモリ設定
- **e2studio_CPU0**: 最小限のスタック（512バイト）
- **quickstart**: 大きなヒープ、複数スレッド用スタック

---

## 5. quickstartコードをe2studio_CPU0に移植するステップ

### 5.1 移植方針

quickstartはシングルコア構成のため、マルチコア対応のe2studio_CPU0への移植では、コア間の責務分担を考慮する必要があります。

**推奨構成**:
- **CPU0 (M85)**: LCD表示、UI処理、メインロジック
- **CPU1 (M33)**: カメラ入力、画像処理

### 5.2 移植ステップ

#### Phase 1: FSPモジュール追加（e2studioで実施）

| ステップ | 操作 | 詳細 |
|---------|------|------|
| 1-1 | FreeRTOS Heap 4追加 | Stacks > New Stack > RTOS > FreeRTOS Heap 4 |
| 1-2 | I2C Master追加 | Stacks > New Stack > Connectivity > I2C Master |
| 1-3 | GLCDC追加 | Stacks > New Stack > Graphics > Graphics LCD |
| 1-4 | D/AVE 2D追加 | Stacks > New Stack > Graphics > D/AVE 2D |
| 1-5 | GPT Timer追加 | Stacks > New Stack > Timers > General PWM |
| 1-6 | UART追加 | Stacks > New Stack > Connectivity > UART |
| 1-7 | External IRQ追加 | Stacks > New Stack > Input > External IRQ |
| 1-8 | OSPI追加 | Stacks > New Stack > Storage > OSPI |
| 1-9 | Generate Project Content | FSPコード生成 |

#### Phase 2: スレッド追加

| ステップ | 操作 | 詳細 |
|---------|------|------|
| 2-1 | display_thread作成 | Stacks > New Thread > 名前設定 |
| 2-2 | tp_thread作成 | タッチパネル用スレッド |
| 2-3 | スタックサイズ設定 | 各スレッド4096バイト以上推奨 |
| 2-4 | 優先度設定 | display: 3, tp: 2 など |

#### Phase 3: ソースコード移植

| ステップ | コピー元 | コピー先 | 備考 |
|---------|---------|---------|------|
| 3-1 | `quickstart/src/display_thread_entry.c` | `e2studio_CPU0/src/` | LCD初期化・描画 |
| 3-2 | `quickstart/src/tp_thread_entry.c` | `e2studio_CPU0/src/` | タッチ処理 |
| 3-3 | `quickstart/src/touch_FT5316.c/h` | `e2studio_CPU0/src/` | タッチドライバ |
| 3-4 | `quickstart/src/board_sdram.c/h` | `e2studio_CPU0/src/` | SDRAM初期化 |
| 3-5 | `quickstart/src/common_init.c/h` | `e2studio_CPU0/src/` | 共通初期化 |

#### Phase 4: FSP設定の詳細調整

##### 4-1. GLCDCモジュール設定

| 設定項目 | 設定値 | 場所 |
|---------|--------|------|
| Name | g_plcd_display | Properties > Name |
| Callback | glcdc_vsync_isr | Properties > Callback |
| Input 0 Enable | True | Graphics Layer 0 |
| Input 0 H Size | 1024 | 水平解像度 |
| Input 0 V Size | 600 | 垂直解像度 |
| Buffer Name | fb_background | フレームバッファ名 |

##### 4-2. I2Cモジュール設定

| 設定項目 | 設定値 | 場所 |
|---------|--------|------|
| Channel | 1 | Properties > Channel |
| Rate | Fast-mode | Properties > Rate |
| Slave Address | 0x38 | タッチコントローラ |

##### 4-3. 外部割込み設定

| 設定項目 | 設定値 | 用途 |
|---------|--------|------|
| Channel | 19 | タッチ割込み |
| Trigger | Falling | 立下りエッジ |
| Callback | touch_irq_callback | コールバック関数 |

#### Phase 5: マルチコア対応調整

```c
// hal_entry.c または blinky_thread_entry.c に追加
void hal_entry(void)
{
    // SDRAM初期化（CPU0のみ実行）
    #if (0 == _RA_CORE)
    board_sdram_init();
    #endif

    // 共通初期化
    common_init();

    // CPU1起動
    #if (0 == _RA_CORE) && (1 == BSP_MULTICORE_PROJECT)
    R_BSP_SecondaryCoreStart();
    #endif
}
```

#### Phase 6: 動作確認

| ステップ | 確認内容 |
|---------|---------|
| 6-1 | ビルドエラーなし |
| 6-2 | SDRAM初期化成功 |
| 6-3 | LCD表示確認 |
| 6-4 | タッチ入力確認 |
| 6-5 | マルチコア動作確認 |

### 5.3 FSP設定変更チェックリスト

```
□ FreeRTOS Heap 4 追加
□ I2C Master (Ch.1, 400kHz) 追加
□ GLCDC (1024x600, RGB565) 追加
□ D/AVE 2D 追加
□ GPT Timer 追加（必要数）
□ UART (デバッグ用) 追加
□ External IRQ (Ch.19 タッチ) 追加
□ OSPI 追加（Flash用）
□ display_thread 作成
□ tp_thread 作成
□ スタックサイズ調整
□ ピン設定確認（GLCDCピンの割り当て）
□ 割込み優先度設定
□ Generate Project Content 実行
```

---

## 6. 注意事項

### 6.1 マルチコア固有の考慮点

1. **SDRAM初期化**: CPU0のみで実行し、CPU1は待機
2. **共有リソース**: フレームバッファはCPU0で管理
3. **起動順序**: CPU0 → SDRAM初期化 → CPU1起動
4. **TrustZone**: セキュア/非セキュア設定の整合性

### 6.2 メモリマップ考慮

| 領域 | アドレス | サイズ | 用途 |
|------|---------|--------|------|
| 内蔵RAM | 0x20000000 | 1.87MB | スタック、ヒープ |
| SDRAM | 0x68000000 | 128MB | フレームバッファ |
| OSPI Flash | 0x60000000 | 64MB | コード/データ |

### 6.3 パフォーマンス目標

| 指標 | 目標値 |
|------|--------|
| LCD FPS | 30以上 |
| タッチ応答 | 50ms以下 |
| CPU0使用率 | 80%以下 |

---

## 付録A: FSP出力ファイル一覧

| ファイル | 内容 |
|---------|------|
| `ra_gen/hal_data.c/h` | HAL層初期化 |
| `ra_gen/common_data.c/h` | 共通データ定義 |
| `ra_gen/bsp_clock_cfg.h` | クロック設定 |
| `ra_gen/pin_data.c` | ピン設定 |
| `ra_gen/vector_data.c/h` | 割込みベクタ |
| `ra_gen/<thread>_thread.c/h` | スレッド定義 |

---

*このドキュメントはClaude Codeにより自動生成されました*
