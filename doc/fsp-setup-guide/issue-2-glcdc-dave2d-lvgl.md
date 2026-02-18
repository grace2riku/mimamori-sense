# e2 studio操作手順書: F-001-1: FSPプロジェクト設定 - GLCDC・D/AVE 2D・LVGL関連モジュールの追加

## 対象Issue

- Issue #2: F-001-1: FSPプロジェクト設定 - GLCDC・D/AVE 2D・LVGL関連モジュールの追加

## 対象プロジェクト

- `e2studio_CPU0/configuration.xml`

## リファレンスプロジェクト

- `reference_projects/lv_port_renesas_ek_ra8p1/configuration.xml`

## 現在のプロジェクト状態

現在の`e2studio_CPU0`に存在するモジュール:

- `r_ioport` (g_ioport)
- `rm_freertos_port` (FreeRTOS)
- `r_sci_b_uart` (g_jlink_console, ch8)

スレッド: `blinky_thread`, `ntshell_thread`

**追加が必要なモジュール（4つとも未設定）:**

1. GLCDC Display Driver (`r_glcdc`)
2. D/AVE 2D Graphics Engine (`r_drw` + TES Dave2D)
3. RM_LVGL_Port Middleware (`rm_lvgl_port`)
4. LVGL Component (`lvgl`)

---

## 前提条件: FreeRTOS設定の変更

LVGLはFreeRTOSのDynamic Allocation、Mutex、Recursive Mutexを必要とします。

### 手順 0-1: FreeRTOS設定

1. e2 studioで`e2studio_CPU0/configuration.xml`を開く
2. **Stacks** タブ → **HAL/Common** を選択
3. **FreeRTOS** のプロパティで以下を変更

| パラメータ | 現在の値 | 変更後の値 |
|---|---|---|
| Use Mutexes | Disabled | **Enabled** |
| Use Recursive Mutexes | Disabled | **Enabled** |
| Support Dynamic Allocation | Disabled | **Enabled** |
| Total Heap Size (bytes) | 1024 | **262144** |
| Use Malloc Failed Hook | Disabled | **Enabled** |

### 手順 0-2: SDRAM有効化

GLCDCのフレームバッファはSDRAM上に配置されます。

1. **BSP** タブ → **RA8P1 Family > Board Support Package** セクション
2. `SDRAM Support` を **Enabled** に変更

---

## 手順1: LVGLスレッドの作成

1. **Stacks** タブ → **New Thread** ボタンをクリック
2. Propertiesで以下を設定

| パラメータ | 設定値 |
|---|---|
| Symbol | `lvgl_thread` |
| Name | `LVGL Thread` |
| Stack size (bytes) | `8192` |
| Priority | `2` |
| Memory Allocation | Static |
| Secure Context | Enable |

---

## 手順2: FreeRTOS Heap 4の追加

1. **LVGL Thread** を選択 → **New Stack** → `heap` で検索
2. **FreeRTOS Heap 4** を選択して追加（デフォルト設定のまま）

---

## 手順3: LVGL Componentの追加

1. **LVGL Thread** を選択 → **New Stack** → `LVGL` で検索
2. **LVGL** を選択して追加

### 主要プロパティ

| パラメータ | 設定値 |
|---|---|
| Custom lv_conf.h | `lv_conf_user.h` |
| Color depth | `16` |
| Memory size (bytes) | `0x20000` (128KB) |
| Draw layer simple buffer size | `0x6000` |
| Draw layer max memory | `0` |
| Draw thread stack size | `0x2000` |
| OS selection | `FreeRTOS` |
| Draw thread priority | `High` |
| Use Dave2D | **Enable** |
| Assert handler include | `<bsp_api.h>` |
| Assert handler | `__BKPT(0);` |

### SW描画フォーマットサポート（全てEnable）

| パラメータ | 設定値 |
|---|---|
| RGB565 | Enable |
| RGB565 swapped | Enable |
| RGB565A8 | Enable |
| RGB888 | Enable |
| XRGB8888 | Enable |
| ARGB8888 | Enable |
| ARGB8888 premultiplied | Enable |
| L8 | Enable |
| AL88 | Enable |
| A8 | Enable |
| I1 | Enable |

### フォント設定

全Montserratフォント(8〜48)をEnable。Default fontは`lv_font_montserrat_14`。

その他のフォント:
- Dejavu 16 Persian Hebrew: Enable
- Source Han Sans SC 14/16 CJK: Enable
- UNSCII 8/16: Enable
- Use font placeholder: Enable

---

## 手順4: RM_LVGL_Port Middlewareの設定

LVGL追加時に自動追加される場合があります。されない場合はLVGLモジュール内の依存アイコンから手動追加。

| パラメータ | 設定値 |
|---|---|
| Name | `g_rm_lvgl_port0` |
| Display configuration inheritance | `Layer 1` |
| Callback | `lvgl_glcdc_callback` |
| Parameter Checking | Default (BSP) |
| Provide Tick Callback | **Enabled** |

---

## 手順5: GLCDC Display Driverの設定

RM_LVGL_Portの**Display**依存先として`r_glcdc`を追加。

### 基本設定

| パラメータ | 設定値 |
|---|---|
| Name | `g_display0` |
| Callback | `glcdc_callback` |

### 割り込み優先度

| パラメータ | 設定値 |
|---|---|
| Line Detect Interrupt Priority | Priority 12 |
| Underflow 1 Interrupt Priority | Priority 12 |
| Underflow 2 Interrupt Priority | Disabled |

### Input - Graphics Layer 1 (Background)

| パラメータ | 設定値 |
|---|---|
| Enable | `True` |
| Horizontal size | `1024` |
| Vertical size | `600` |
| Horizontal position | `0` |
| Vertical position | `0` |
| Frame buffer name | `fb_background` |
| Number of frame buffers | `2` |
| Section for frame buffer allocation | `.sdram_noinit_nocache` |
| Color format | `RGB565 (16bit)` |
| Line descending enable | `False` |
| Lines repeat enable | `False` |
| Lines repeat times | `0` |
| Fade control | `None` |
| Fade speed | `0` |

### Input - Graphics Layer 2 (Foreground)

| パラメータ | 設定値 |
|---|---|
| Enable | `False` |

### Output - タイミング (1024x600 LCD)

| パラメータ | 設定値 | 備考 |
|---|---|---|
| **水平** | | |
| Total cycles | `1344` | 1024 + 160(BP) + 156(FP) + 4(SW) |
| Display cycles | `1024` | アクティブ表示幅 |
| Back porch | `160` | |
| Sync width | `4` | |
| Sync polarity | `Low active` | |
| **垂直** | | |
| Total cycles | `635` | 600 + 23(BP) + 9(FP) + 3(SW) |
| Display cycles | `600` | アクティブ表示高さ |
| Back porch | `23` | |
| Sync width | `3` | |
| Sync polarity | `Low active` | |

### Output - その他

| パラメータ | 設定値 |
|---|---|
| Data Enable polarity | `High active` |
| Sync edge | `Falling edge` |
| Output format | `24bit RGB888` |
| Color order | `RGB` |
| Endian | `Little endian` |

### Output - 背景色

| パラメータ | 設定値 |
|---|---|
| Alpha / Red / Green / Blue | `255` / `0` / `0` / `0` |

### TCON (Timing Control)

| パラメータ | 設定値 |
|---|---|
| HSYNC pin | `None` |
| VSYNC pin | `None` |
| Data Enable pin | `TCON2` |

### クロック設定

| パラメータ | 設定値 |
|---|---|
| Clock source | `Internal` |
| Clock division ratio | `1/4` |

### GLCDC モジュール設定

| パラメータ | 設定値 |
|---|---|
| Parameter Checking | Default (BSP) |
| Color Correction | **Off** |

---

## 手順6: D/AVE 2D (DRW) ドライバの設定

RM_LVGL_Portの**Dave2D Port**依存先として`r_drw`を追加。追加すると**TES DAVE 2D Drawing Engine**が自動追加されます。

### D/AVE 2D Port (r_drw) のプロパティ

| パラメータ | 設定値 |
|---|---|
| Handle name | `d2_handle0` |
| Interrupt priority | `Priority 2` |

### D/AVE 2D モジュール設定

| パラメータ | 設定値 |
|---|---|
| Indirect mode | `On` |
| Memory allocation mode | `Default` |

---

## 手順7: ピン設定

**Pins** タブ → **Peripherals > Graphics > GLCDC** で以下を設定:

| 信号名 | ピン | 説明 |
|---|---|---|
| LCD_CLK | P515 | ピクセルクロック |
| LCD_EXTCLK | P710 | 外部クロック |
| LCD_TCON2 | P807 | Data Enable (DE) |
| LCD_DATA0 | P914 | R0 |
| LCD_DATA1 | P915 | R1 |
| LCD_DATA2 | P903 | R2 |
| LCD_DATA3 | P902 | R3 |
| LCD_DATA4 | P910 | R4 |
| LCD_DATA5 | P911 | R5 |
| LCD_DATA6 | P912 | R6 |
| LCD_DATA7 | P913 | R7 |
| LCD_DATA8 | P904 | G0 |
| LCD_DATA9 | P207 | G1 |
| LCD_DATA10 | PB07 | G2 |
| LCD_DATA11 | PB06 | G3 |
| LCD_DATA12 | PB05 | G4 |
| LCD_DATA13 | PB01 | G5 |
| LCD_DATA14 | PB04 | G6 |
| LCD_DATA15 | PB03 | G7 |
| LCD_DATA16 | PB02 | B0 |
| LCD_DATA17 | PB00 | B1 |
| LCD_DATA18 | P707 | B2 |
| LCD_DATA19 | P711 | B3 |
| LCD_DATA20 | P712 | B4 |
| LCD_DATA21 | P713 | B5 |
| LCD_DATA22 | P714 | B6 |
| LCD_DATA23 | P715 | B7 |

---

## 手順8: スタック構成の確認

設定完了後、**Stacks** タブで **LVGL Thread** のスタック構成が以下のツリー構造になっていることを確認:

```
LVGL Thread (lvgl_thread)
  +-- FreeRTOS Heap 4
  +-- LVGL (lvgl)
       +-- RM LVGL Port (g_rm_lvgl_port0)
            +-- Display on GLCDC (g_display0)
            +-- D/AVE 2D Port (d2_handle0)
                 +-- TES DAVE 2D Drawing Engine
```

---

## 手順9: コード生成とビルド

1. **Generate Project Content** を実行
2. `ra_gen/`に対応する初期化コードが生成されることを確認
3. ビルドしてエラーがないことを確認

---

## 補足

- タッチパネル関連モジュール（I2C, External IRQ等）は本Issueの対象外
- コード生成後、`src/lvgl_thread_entry.c`の手動作成が必要（別Issue対応）
- リファレンスプロジェクトはFSP 6.2.0、本プロジェクトはFSP 6.3.0
