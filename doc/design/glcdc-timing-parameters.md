# GLCDC クロック・タイミングパラメータ設計書

## 対象Issue

- Issue #33: S-002-1: GLCDCクロック・タイミングパラメータの設計・設定

## リファレンスプロジェクト

- `reference_projects/lv_port_renesas_ek_ra8p1/configuration.xml`
- `reference_projects/lv_port_renesas_ek_ra8p1/ra_gen/common_data.c` (GLCDC設定構造体)
- `reference_projects/lv_port_renesas_ek_ra8p1/ra_gen/common_data.h` (DISPLAY_* マクロ)
- `reference_projects/lv_port_renesas_ek_ra8p1/ra_gen/bsp_clock_cfg.h` (クロック設定)

## 現在のプロジェクト

- `e2studio_CPU0/configuration.xml`
- `e2studio_CPU0/ra_gen/common_data.c` (GLCDC設定構造体)
- `e2studio_CPU0/ra_gen/common_data.h` (DISPLAY_* マクロ)
- `e2studio_CPU0/ra_gen/bsp_clock_cfg.h` (クロック設定)

---

## 1. LCDパネル仕様

### EK-RA8P1 パラレルグラフィックス拡張ボード搭載LCD

| 項目 | 値 | 備考 |
|---|---|---|
| 解像度 | 1024 x 600 | 水平1024ピクセル x 垂直600ライン |
| インタフェース | パラレルRGB (24-bit) | LCD_DATA[23:0] |
| 色深度 (パネル出力) | RGB888 (24bit/pixel) | GLCDC Output Format |
| 色深度 (フレームバッファ) | RGB565 (16bit/pixel) | GLCDC Input Format (メモリ節約) |

### パネルタイミング仕様

リファレンスプロジェクトの `configuration.xml` (lines 579-588) および
`ra_gen/common_data.c` (lines 165-170) から抽出した値。

#### 水平タイミング

| パラメータ | シンボル | 値 (clock cycles) | 計算 |
|---|---|---|---|
| 水平トータル | H_TOTAL | 1344 | = H_DISPLAY + H_BP + H_FP + H_SW |
| 水平表示期間 | H_DISPLAY | 1024 | アクティブ表示幅 |
| 水平バックポーチ | H_BP | 160 | |
| 水平フロントポーチ | H_FP | 156 | = H_TOTAL - H_DISPLAY - H_BP - H_SW |
| 水平同期パルス幅 | H_SW | 4 | |
| 水平同期極性 | H_POL | Low Active | DISPLAY_SIGNAL_POLARITY_LOACTIVE |

**検算**: 1024 + 160 + 156 + 4 = 1344 (一致)

#### 垂直タイミング

| パラメータ | シンボル | 値 (line cycles) | 計算 |
|---|---|---|---|
| 垂直トータル | V_TOTAL | 635 | = V_DISPLAY + V_BP + V_FP + V_SW |
| 垂直表示期間 | V_DISPLAY | 600 | アクティブ表示高さ |
| 垂直バックポーチ | V_BP | 23 | |
| 垂直フロントポーチ | V_FP | 9 | = V_TOTAL - V_DISPLAY - V_BP - V_SW |
| 垂直同期パルス幅 | V_SW | 3 | |
| 垂直同期極性 | V_POL | Low Active | DISPLAY_SIGNAL_POLARITY_LOACTIVE |

**検算**: 600 + 23 + 9 + 3 = 635 (一致)

#### タイミング図

```
水平タイミング (1フレームの1ライン):
  |<-- H_SW -->|<---- H_BP ---->|<---------- H_DISPLAY ---------->|<--- H_FP --->|
  |    4 clk   |    160 clk     |          1024 clk                |   156 clk    |
  |____________|                |================================|               |
  |  HSYNC     |  Back Porch    |      Active Display Area       |  Front Porch  |
  |<------------------------------- H_TOTAL = 1344 clk ---------------------------->|

垂直タイミング (1フレーム):
  |<-- V_SW -->|<---- V_BP ---->|<---------- V_DISPLAY ---------->|<--- V_FP --->|
  |   3 lines  |   23 lines     |          600 lines               |   9 lines    |
  |____________|                |================================|               |
  |  VSYNC     |  Back Porch    |      Active Display Area       |  Front Porch  |
  |<------------------------------- V_TOTAL = 635 lines --------------------------->|
```

---

## 2. GLCDCクロック設計

### クロックツリー

```
XTAL (24 MHz)
  |
  +-> PLL1 Div /3 = 8 MHz
  |     +-> PLL1 Mul x250 = 2000 MHz (PLL1 VCO)
  |           +-> PLL1R Div /5 = 400 MHz
  |                 +-> LCDCLK Div /2 = 200 MHz
  |                       +-> GLCDC Clock Div /4 = 50 MHz (Pixel Clock)
```

### クロックパラメータ一覧

| クロック | ソース | 分周比 | 周波数 | 設定箇所 |
|---|---|---|---|---|
| XTAL | - | - | 24 MHz | ボード固定 |
| PLL1 Input | XTAL | /3 | 8 MHz | solution.xml Clocks |
| PLL1 VCO | PLL1 Input | x250 | 2000 MHz | solution.xml Clocks |
| PLL1R | PLL1 VCO | /5 | 400 MHz | solution.xml Clocks |
| LCDCLK | PLL1R | /2 | 200 MHz | solution.xml Clocks |
| GLCDC Panel Clock | LCDCLK | /4 | **50 MHz** | configuration.xml GLCDC |

### ピクセルクロック計算

```
Pixel Clock = LCDCLK / GLCDC_PANEL_CLK_DIVISOR
            = 200 MHz / 4
            = 50 MHz
```

### フレームレート計算

```
Frame Rate = Pixel Clock / (H_TOTAL x V_TOTAL)
           = 50,000,000 / (1344 x 635)
           = 50,000,000 / 853,440
           = 58.59 Hz (approximately 59 Hz)
```

### bsp_clock_cfg.h の関連設定値

以下は `e2studio_CPU0/ra_gen/bsp_clock_cfg.h` からの抽出値。

| マクロ | 値 | 意味 |
|---|---|---|
| `BSP_CFG_LCDCLK_SOURCE` | `BSP_CLOCKS_SOURCE_CLOCK_PLL1R` | LCDCLK源: PLL1R (400 MHz) |
| `BSP_CFG_LCDCLK_DIV` | `BSP_CLOCKS_LCD_CLOCK_DIV_2` | LCDCLK分周比: /2 (= 200 MHz) |
| `BSP_CFG_PLL1R_FREQUENCY_HZ` | `400000000` | PLL1R = 400 MHz |

---

## 3. 出力フォーマット設定

### GLCDCレイヤ設定

| 項目 | 値 | 説明 |
|---|---|---|
| Graphics Layer 1 (BG) | **Enabled** | メイン表示レイヤ |
| Graphics Layer 2 (FG) | Disabled | 未使用 |

### Graphics Layer 1 (Background) 詳細

| パラメータ | 値 | 説明 |
|---|---|---|
| 水平サイズ | 1024 pixels | `DISPLAY_HSIZE_INPUT0` |
| 垂直サイズ | 600 pixels | `DISPLAY_VSIZE_INPUT0` |
| 入力カラーフォーマット | RGB565 (16-bit) | `DISPLAY_IN_FORMAT_16BITS_RGB565` |
| フレームバッファ名 | `fb_background` | FSP自動生成変数名 |
| フレームバッファ面数 | 2 (ダブルバッファ) | Vsync同期フレーム切り替え用 |
| セクション配置 | `.sdram_noinit_nocache` | SDRAM上、非キャッシュ、初期化なし |
| ラインアクセス方向 | Ascending (false) | 上から下へ描画 |
| ライン繰り返し | Disabled (false) | 通常表示 |
| フェード制御 | None | フェード効果なし |

### Graphics Layer 2 (Foreground) 設定

Layer 2はDisabledだが、configuration.xmlにはデフォルト値として以下が定義されている。

| パラメータ | 値 | 説明 |
|---|---|---|
| 水平サイズ | 480 pixels | `DISPLAY_HSIZE_INPUT1` |
| 垂直サイズ | 854 pixels | `DISPLAY_VSIZE_INPUT1` |
| 入力カラーフォーマット | RGB565 (16-bit) | `DISPLAY_IN_FORMAT_16BITS_RGB565` |
| フレームバッファ名 | `fb_foreground` | Layer 2無効のため未使用 |
| セクション配置 | `.sdram_noinit` | Layer 2無効のため未使用 |

### 出力設定

| パラメータ | 値 | 設定値 |
|---|---|---|
| 出力フォーマット | RGB888 (24-bit) | `DISPLAY_OUT_FORMAT_24BITS_RGB888` |
| エンディアン | Little Endian | `DISPLAY_ENDIAN_LITTLE` |
| カラーオーダー | RGB | `DISPLAY_COLOR_ORDER_RGB` |
| Data Enable極性 | High Active | `DISPLAY_SIGNAL_POLARITY_HIACTIVE` |
| Sync Edge | Falling | `DISPLAY_SIGNAL_SYNC_EDGE_FALLING` |
| 背景色 (ARGB) | A=255, R=0, G=0, B=0 | 黒 (不透明) |
| ディザリング | Disabled | `dithering_on = false` |

### 入出力フォーマットの関係

```
フレームバッファ (SDRAM)          GLCDC内部            LCDパネル出力
  RGB565 (16-bit)      ---->    フォーマット変換    ---->  RGB888 (24-bit)
  1024 x 600                     16bit -> 24bit           1024 x 600
  ~1.2 MB/面                     (自動拡張)               パラレルRGB
```

GLCDC はフレームバッファの RGB565 データを読み出し、内部で RGB888 に変換して
LCDパネルへ出力する。これにより、フレームバッファのメモリ使用量を抑えつつ、
パネル出力は24-bitフルカラーとなる。

---

## 4. フレームバッファ設計

### メモリレイアウト

| パラメータ | 値 | 計算 |
|---|---|---|
| ピクセルあたりのビット数 | 16 bits | RGB565 |
| バッファ幅ストライド (バイト) | 2048 bytes | `((1024 * 16 + 0x1FF) >> 9) << 6` |
| バッファ幅ストライド (ピクセル) | 1024 pixels | `(2048 * 8) / 16` |
| 1面あたりのサイズ | 1,228,800 bytes (1.17 MB) | 2048 x 600 |
| 2面合計サイズ | 2,457,600 bytes (2.34 MB) | 2048 x 600 x 2 |
| アライメント | 64-byte | BSP_ALIGN_VARIABLE(64) |
| 配置セクション | `.sdram_noinit_nocache` | 非キャッシュ、初期化なし |

### ストライド計算の詳細

```c
// common_data.h の DISPLAY_BUFFER_STRIDE_BYTES_INPUT0 マクロ
// HSIZE = 1024, BPP = 16
stride_bytes = ((1024 * 16 + 0x1FF) >> 9) << 6
             = ((16384 + 511) >> 9) << 6
             = (16895 >> 9) << 6
             = 32 << 6
             = 2048 bytes

stride_pixels = (2048 * 8) / 16
              = 1024 pixels
```

この場合、ストライドは表示幅と一致する（パディングなし）。

### 配置先コード（FSP自動生成）

```c
// e2studio_CPU0/ra_gen/common_data.c:7
uint8_t fb_background[2][DISPLAY_BUFFER_STRIDE_BYTES_INPUT0 * DISPLAY_VSIZE_INPUT0]
    BSP_ALIGN_VARIABLE(64)
    BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".sdram_noinit_nocache");
```

- `BSP_UNINIT_SECTION_PREFIX` = `".uninit"` (Cortex-M85ではプレフィックスが付加される)
- 最終的なセクション名: `.uninit.sdram_noinit_nocache`
- SDRAM上に配置され、MPUで非キャッシュに設定される

### フレームバッファアドレス（SDRAM上）

SDRAM ベースアドレス `0x68000000` 上に配置される。
実際のアドレスはリンカが決定するが、`.sdram_noinit_nocache` セクションの
先頭に配置される。

```
SDRAM (0x68000000 - 0x69FFFFFF, 32MB)
  +-------------------------------+ 0x68000000
  | .sdram_from_flash             |
  +-------------------------------+
  | fb_background[0]              | 2048 x 600 = 1,228,800 bytes (1.17 MB)
  | fb_background[1]              | 2048 x 600 = 1,228,800 bytes (1.17 MB)
  +-------------------------------+
  | (other sdram sections)        |
  +-------------------------------+
  |         (free)                |
  +-------------------------------+ 0x69FFFFFF
```

---

## 5. TCON (Timing Control Output) 設定

| パラメータ | 値 | 説明 |
|---|---|---|
| HSYNC ピン | None (`GLCDC_TCON_PIN_NONE`) | HSYNC信号は外部出力しない |
| VSYNC ピン | None (`GLCDC_TCON_PIN_NONE`) | VSYNC信号は外部出力しない |
| Data Enable ピン | TCON2 (`GLCDC_TCON_PIN_2`) | DE信号をTCON2ピンから出力 |

EK-RA8P1のパラレルグラフィックス拡張ボードのLCDはDE (Data Enable) モードで
動作する。HSYNC/VSYNC は個別にピン出力せず、GLCDC内部でタイミング制御に使用する。

---

## 6. 色補正設定

| パラメータ | 値 | 説明 |
|---|---|---|
| Color Correction | Disabled | `GLCDC_CFG_COLOR_CORRECTION_ENABLE = false` |
| 輝度調整 | Disabled | `brightness.enable = false` |
| コントラスト調整 | Disabled | `contrast.enable = false` |
| ガンマ補正 (R/G/B) | Disabled | `GLCDC_CFG_CORRECTION_GAMMA_ENABLE_* = false` |

---

## 7. GLCDC拡張設定

| パラメータ | 値 | 説明 |
|---|---|---|
| クロックソース | Internal | `GLCDC_CLK_SRC_INTERNAL` (LCDCLK使用) |
| クロック分周比 | 1/4 | `GLCDC_PANEL_CLK_DIVISOR_4` |
| 補正処理順序 | Brightness/Contrast -> Gamma | `GLCDC_CORRECTION_PROC_ORDER_BRIGHTNESS_CONTRAST2GAMMA` |
| ディザリングモード | Truncate | `GLCDC_DITHERING_MODE_TRUNCATE` |
| ディザリングパターン A | 11 | `GLCDC_DITHERING_PATTERN_11` |
| ディザリングパターン B | 11 | `GLCDC_DITHERING_PATTERN_11` |
| ディザリングパターン C | 11 | `GLCDC_DITHERING_PATTERN_11` |
| ディザリングパターン D | 11 | `GLCDC_DITHERING_PATTERN_11` |
| PHYレイヤ | NULL | MIPI DSI非使用（パラレルRGBインタフェース） |

---

## 8. 割り込み設定

| パラメータ | 値 | 説明 |
|---|---|---|
| Line Detect IRQ | Priority 12 | VSYNC（ライン検出）割り込み |
| Underflow 1 IRQ | Priority 12 | Layer 1 アンダーフロー割り込み |
| Underflow 2 IRQ | Disabled | Layer 2 未使用のため無効 |

`e2studio_CPU0/ra_gen/vector_data.h` での割り込みベクタ番号:
- `VECTOR_NUMBER_GLCDC_LINE_DETECT` = IRQ 4
- `VECTOR_NUMBER_GLCDC_UNDERFLOW_1` = IRQ 5

---

## 9. LVGL連携設定

### RM LVGL Port モジュール

| パラメータ | 値 | 説明 |
|---|---|---|
| インスタンス名 | `g_rm_lvgl_port0` | |
| 表示設定の継承元 | Layer 1 | Graphics Layer 1 (Background) |
| コールバック | `lvgl_glcdc_callback` | ユーザー実装コールバック |
| パラメータチェック | Default (BSP) | |
| Tickコールバック提供 | Enabled | |

### コールバック構造

```
GLCDC Line Detect IRQ
  |
  +-> glcdc_line_detect_isr()              (FSP ISRハンドラ)
       |
       +-> _rm_lvgl_port_display_callback() (FSP自動生成、rm_lvgl_portモジュール内部)
            |
            +-> lvgl_glcdc_callback()       (ユーザー実装コールバック)
                 +-> RM_LVGL_PORT_EVENT_UNDERFLOW 時: assert(0)
```

### フレームバッファとLVGLの関係

```c
// FSP自動生成コード (e2studio_CPU0/ra_gen/common_data.c)
const rm_lvgl_port_cfg_t g_lvgl_port_cfg = {
    .p_display_instance  = &g_display0,
    .inherit_frame_layer = DISPLAY_FRAME_LAYER_1,
    .p_framebuffer_0     = &fb_background[0],    // LVGL描画バッファ0
    .p_framebuffer_1     = &fb_background[1],    // LVGL描画バッファ1
    .p_callback          = lvgl_glcdc_callback,
};
```

LVGL はダブルバッファリングを使用し、一方のバッファに描画しながら他方を表示する。
バッファ切り替えはVSYNC割り込み（Line Detect）に同期して行われる。

---

## 10. LVGLカラー設定との整合性

| LVGL設定項目 | 値 | GLCDC設定 | 整合性 |
|---|---|---|---|
| `LV_COLOR_DEPTH` | 16 | Input Format: RGB565 (16-bit) | OK |
| `LV_USE_DRAW_DAVE2D` | Enable | Dave2D Port有効 | OK |
| 表示解像度 (H) | `DISPLAY_HSIZE_INPUT0` = 1024 | GLCDC H_DISPLAY = 1024 | OK |
| 表示解像度 (V) | `DISPLAY_VSIZE_INPUT0` = 600 | GLCDC V_DISPLAY = 600 | OK |

---

## 11. 現在のプロジェクトとリファレンスの差分分析

### 比較結果

現在のプロジェクト (`e2studio_CPU0/configuration.xml`) とリファレンスプロジェクト
(`reference_projects/lv_port_renesas_ek_ra8p1/configuration.xml`) を比較した結果、
**GLCDCタイミングパラメータに関しては完全一致している**。

#### 一致している設定項目

| カテゴリ | 状態 |
|---|---|
| 水平タイミング (total_cyc=1344, display_cyc=1024, back_porch=160, sync_width=4) | 一致 |
| 垂直タイミング (total_cyc=635, display_cyc=600, back_porch=23, sync_width=3) | 一致 |
| HSYNC/VSYNC極性 (Low Active) | 一致 |
| 出力フォーマット (RGB888, Little Endian, RGB order) | 一致 |
| Data Enable極性 (High Active) | 一致 |
| Sync Edge (Falling) | 一致 |
| Graphics Layer 1 設定 (1024x600, RGB565, ダブルバッファ, .sdram_noinit_nocache) | 一致 |
| Graphics Layer 2 設定 (Disabled) | 一致 |
| TCON設定 (HSYNC=None, VSYNC=None, DE=TCON2) | 一致 |
| クロック設定 (Internal, Divisor 4) | 一致 |
| 色補正設定 (全てDisabled) | 一致 |
| ディザリング設定 (Truncate, pattern_11) | 一致 |
| 割り込み優先度 (Line=12, UF1=12, UF2=Disabled) | 一致 |
| LVGL Port設定 (Layer1継承, lvgl_glcdc_callback) | 一致 |

#### クロック設定の一致確認

| クロック | リファレンス | 現在のプロジェクト | 状態 |
|---|---|---|---|
| LCDCLK Source | PLL1R | PLL1R | 一致 |
| LCDCLK Div | /2 | /2 | 一致 |
| PLL1R | 400 MHz | 400 MHz | 一致 |
| LCDCLK | 200 MHz | 200 MHz | 一致 |
| Pixel Clock (200MHz/4) | 50 MHz | 50 MHz | 一致 |

#### 差分がある設定項目

| パラメータ | リファレンス | 現在のプロジェクト | 影響 |
|---|---|---|---|
| GLCDC Callback | `glcdc_callback` | `NULL` | **要対応** (後述) |
| FSP Version | 6.2.0 | 6.3.0 | 影響なし (互換) |
| SPICLK Div | /8 | /1 | GLCDC無関係 |
| CANFDCLK Div | /10 | /6 | GLCDC無関係 |
| I3CCLK Div | /5 | /4 | GLCDC無関係 |

### GLCDC Callbackの差分について

- **リファレンス**: `module.driver.display.callback` = `glcdc_callback`
- **現在のプロジェクト**: `module.driver.display.callback` = `NULL`

ただし、RM LVGL Port ミドルウェアがGLCDCの上位に配置されているため、
FSP自動生成コードでは `p_callback` が `_rm_lvgl_port_display_callback` に
オーバーライドされる。

現在の自動生成コード (`e2studio_CPU0/ra_gen/common_data.c:209`) を確認すると:
```c
.p_callback = _rm_lvgl_port_display_callback,
```

**結論**: RM LVGL Port ミドルウェアがコールバックをラップしているため、
`configuration.xml` で `NULL` と設定されていても LVGL Port の内部コールバックが
正しく設定される。ただし、リファレンスに合わせて `glcdc_callback` に変更する
ことを推奨する（下記「FSP設定変更手順」を参照）。

---

## 12. FSP configuration.xml 設定変更手順

現在のプロジェクトの GLCDC タイミングパラメータはリファレンスと完全一致しているため、
タイミング関連の変更は不要。

### 推奨変更: GLCDC Callbackの設定

リファレンスプロジェクトに合わせて、GLCDCのコールバック名を設定する。

**手順**:
1. `e2studio_CPU0/configuration.xml` を e2 studio で開く
2. **Stacks** タブを選択
3. LVGL Thread -> LVGL -> RM LVGL Port -> **Display on GLCDC (g_display0)** を選択
4. プロパティパネルで以下を変更:

| パラメータ | 現在の値 | 変更後の値 |
|---|---|---|
| Callback | `NULL` | `glcdc_callback` |

5. **Generate Project Content** を実行

**変更の理由**:
- リファレンスプロジェクトとの一貫性を保つ
- RM LVGL Port ミドルウェアが内部でコールバックをオーバーライドするため
  動作に影響はないが、将来 LVGL Port を介さずに直接 GLCDC を使用する場合に
  コールバックが `NULL` だと問題が発生する可能性がある

**注意**: この変更は任意であり、現在の `NULL` 設定でも LVGL Port 経由の
表示には影響しない。

---

## 13. 設定値サマリー (FSP configuration.xml プロパティ一覧)

以下は、e2 studio の Stacks タブで "Display on GLCDC (g_display0)" を選択した際に
表示される全プロパティの設定値一覧。

### General

| プロパティ | 値 |
|---|---|
| Name | `g_display0` |
| Callback | `NULL` (推奨: `glcdc_callback`) |

### Interrupts

| プロパティ | 値 |
|---|---|
| Line Detect Interrupt Priority | Priority 12 |
| Underflow 1 Interrupt Priority | Priority 12 |
| Underflow 2 Interrupt Priority | Disabled |

### Input > Graphics Layer 1

| プロパティ | 値 |
|---|---|
| General > Enable | True |
| General > Horizontal size | 1024 |
| General > Vertical size | 600 |
| General > Horizontal position | 0 |
| General > Vertical position | 0 |
| Buffer > Frame buffer name | `fb_background` |
| Buffer > Number of frame buffers | 2 |
| Buffer > Section for frame buffer allocation | `.sdram_noinit_nocache` |
| Buffer > Color format | RGB565 (16-bit) |
| Line descending enable | False |
| Lines repeat enable | False |
| Lines repeat times | 0 |
| Fade control | None |
| Fade speed | 0 |

### Input > Graphics Layer 2

| プロパティ | 値 |
|---|---|
| General > Enable | False |

### Output > Timing

| プロパティ | 値 |
|---|---|
| Horizontal > Total cycles | 1344 |
| Horizontal > Display cycles | 1024 |
| Horizontal > Back porch | 160 |
| Horizontal > Sync width | 4 |
| Horizontal > Sync polarity | Low active |
| Vertical > Total cycles | 635 |
| Vertical > Display cycles | 600 |
| Vertical > Back porch | 23 |
| Vertical > Sync width | 3 |
| Vertical > Sync polarity | Low active |

### Output > Format

| プロパティ | 値 |
|---|---|
| Data Enable polarity | High active |
| Sync edge | Falling edge |
| Output format | 24-bit RGB888 |
| Color order | RGB |
| Endian | Little endian |

### Output > Background

| プロパティ | 値 |
|---|---|
| Alpha | 255 |
| Red | 0 |
| Green | 0 |
| Blue | 0 |

### Output > Color Correction

| プロパティ | 値 |
|---|---|
| Brightness > Enable | False |
| Contrast > Enable | False |
| Gamma R/G/B > Enable | Off |

### TCON

| プロパティ | 値 |
|---|---|
| HSYNC pin | None |
| VSYNC pin | None |
| Data Enable pin | TCON2 |

### Clock

| プロパティ | 値 |
|---|---|
| Clock source | Internal |
| Clock division ratio | 1/4 |

### Dithering

| プロパティ | 値 |
|---|---|
| Dithering on | False |
| Dithering mode | Truncate |
| Pattern A/B/C/D | 11 |

---

## 14. ピン設定

GLCDCのピン設定は `configuration.xml` の Pins タブ > `Peripherals > HMI:GLCDC` で
設定する。既存の設定手順は `doc/fsp-setup-guide/issue-2-glcdc-dave2d-lvgl.md` の
「手順7: ピン設定」に記載済み。

| 信号名 | ピン | 説明 |
|---|---|---|
| LCD_CLK | P515 | ピクセルクロック出力 (50 MHz) |
| LCD_EXTCLK | P710 | 外部クロック入力 (未使用: Internal clock使用) |
| LCD_TCON2 | P807 | Data Enable (DE) 信号出力 |
| LCD_DATA[0:7] | P914,P915,P903,P902,P910,P911,P912,P913 | R[0:7] |
| LCD_DATA[8:15] | P904,P207,PB07,PB06,PB05,PB01,PB04,PB03 | G[0:7] |
| LCD_DATA[16:23] | PB02,PB00,P707,P711,P712,P713,P714,P715 | B[0:7] |

---

## 15. 関連ドキュメント

| ドキュメント | 説明 |
|---|---|
| `doc/fsp-setup-guide/issue-2-glcdc-dave2d-lvgl.md` | GLCDC/Dave2D/LVGLモジュール追加手順 |
| `doc/fsp-setup-guide/issue-29-sdram-controller.md` | SDRAMコントローラ設定手順 |

---

## 変更履歴

| 日付 | 変更内容 |
|---|---|
| 2026-02-22 | 初版作成 (Issue #33 S-002-1) |
