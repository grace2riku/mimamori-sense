# LVGL画面デザイン方法とコード実装解説

## 1. はじめに

本レポートでは、LVGLの画面デザイン方法と、そのデザインをコードで使用する方法について解説します。解説にはEK-RA8P1向けリファレンス実装（`reference_projects/lv_port_renesas_ek_ra8p1`）を例として使用します。

また、OV5640カメラモジュールで撮影した画像をLCDに表示する方法と、Cortex-M85コア（e2studio_CPU0）への移植方針についても提案します。

---

## 2. LVGLの画面デザイン方法

### 2.1 画面デザインの基本概念

LVGLでは、画面（Screen）を最上位のコンテナとして、その中にウィジェット（Widget）を配置することでUIを構築します。

```
スクリーン (lv_obj_t)
├── コンテナ (lv_obj_t)
│   ├── ラベル (lv_label)
│   ├── ボタン (lv_btn)
│   └── スライダー (lv_slider)
└── チャート (lv_chart)
```

### 2.2 UIフォルダ構造

リファレンス実装では、以下のフォルダ構造でUIを管理しています：

```
ui/
├── components/     # 再利用可能なUIコンポーネント
├── screens/        # 画面定義
├── fonts/          # カスタムフォント
└── images/         # 画像リソース
```

### 2.3 利用可能なウィジェット

リファレンス実装で使用されている主要ウィジェット：

| ウィジェット | 作成関数 | 用途 |
|-------------|---------|------|
| Label | `lv_label_create()` | テキスト表示 |
| Button | `lv_btn_create()` | クリック可能なボタン |
| Button Array | `lv_btnmatrix_create()` | 複数ボタングループ |
| Container | `lv_obj_create()` | レイアウトコンテナ |
| Chart | `lv_chart_create()` | グラフ表示 |
| Slider | `lv_slider_create()` | 値調整スライダー |
| Image | `lv_image_create()` | 画像表示 |

---

## 3. 画面デザインをコードで使用する方法

### 3.1 スクリーンの作成と切り替え

```c
// 新しいスクリーンを作成
lv_obj_t * screen1 = lv_obj_create(NULL);

// スクリーンをアクティブにする（即時切り替え）
lv_scr_load(screen1);

// アニメーション付きで切り替え
lv_scr_load_anim(screen1, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
```

### 3.2 ウィジェットの配置

```c
// 現在のアクティブスクリーンを取得
lv_obj_t * scr = lv_scr_act();

// ラベルを作成
lv_obj_t * label = lv_label_create(scr);
lv_label_set_text(label, "Hello LVGL!");
lv_obj_align(label, LV_ALIGN_CENTER, 0, -50);

// ボタンを作成
lv_obj_t * btn = lv_btn_create(scr);
lv_obj_set_size(btn, 120, 50);
lv_obj_align(btn, LV_ALIGN_CENTER, 0, 50);

// ボタンにラベルを追加
lv_obj_t * btn_label = lv_label_create(btn);
lv_label_set_text(btn_label, "Click me");
lv_obj_center(btn_label);
```

### 3.3 イベントハンドリング

```c
// イベントコールバック関数
static void btn_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        // ボタンがクリックされた時の処理
        printf("Button clicked!\n");
    }
}

// イベントコールバックを登録
lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_ALL, NULL);
```

### 3.4 スタイルの適用

```c
// スタイルを定義
static lv_style_t style_btn;
lv_style_init(&style_btn);
lv_style_set_bg_color(&style_btn, lv_color_hex(0x2196F3));
lv_style_set_bg_opa(&style_btn, LV_OPA_COVER);
lv_style_set_radius(&style_btn, 10);
lv_style_set_shadow_width(&style_btn, 10);
lv_style_set_shadow_ofs_y(&style_btn, 5);

// スタイルをオブジェクトに適用
lv_obj_add_style(btn, &style_btn, 0);
```

### 3.5 レイアウト（Flexbox/Grid）

```c
// Flexboxレイアウトを使用
lv_obj_t * cont = lv_obj_create(scr);
lv_obj_set_size(cont, 300, 200);
lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);
lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

// コンテナ内にボタンを追加
for (int i = 0; i < 6; i++) {
    lv_obj_t * btn = lv_btn_create(cont);
    lv_obj_set_size(btn, 80, 40);
}
```

---

## 4. リファレンス実装の画面・UI部品解説

### 4.1 初期化フロー

リファレンス実装（`lv_port_renesas_ek_ra8p1`）では、以下のフローでLVGLを初期化しています：

```c
// main.c または hal_entry.c
void hal_entry(void)
{
    // 1. LVGLコア初期化
    lv_init();

    // 2. ディスプレイ初期化
    lv_port_disp_init();

    // 3. 入力デバイス（タッチパネル）初期化
    lv_port_indev_init();

    // 4. UIの初期化（画面作成）
    ui_init();

    // 5. メインループ
    while (1) {
        lv_timer_handler();  // LVGLタイマー処理
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
```

### 4.2 ディスプレイ設定（lv_port_disp.c）

```c
void lv_port_disp_init(void)
{
    // 低レベルディスプレイ初期化
    disp_init();

    // LVGLディスプレイを作成
    lv_display_t * disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);

    // ダブルバッファを設定（SDRAMに配置）
    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // フラッシュコールバックを登録
    lv_display_set_flush_cb(disp, disp_flush);
}
```

### 4.3 タッチパネル設定（lv_port_indev.c）

```c
void lv_port_indev_init(void)
{
    // 入力デバイスを作成
    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read);
}

static void touchpad_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    // I2C経由でFT5X06タッチコントローラからデータ取得
    uint8_t touch_buffer[43];
    R_IIC_MASTER_Read(&g_i2c_master_ctrl, touch_buffer, 43);

    uint8_t touch_count = touch_buffer[2] & 0x0F;

    if (touch_count > 0) {
        data->point.x = ((touch_buffer[3] & 0x0F) << 8) | touch_buffer[4];
        data->point.y = ((touch_buffer[5] & 0x0F) << 8) | touch_buffer[6];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
```

### 4.4 フレームバッファとGLCDC

| 項目 | 値 |
|------|-----|
| 解像度 | 1024 x 600 ピクセル |
| カラーフォーマット | RGB565（16ビット） |
| バッファサイズ | 1,228,800 bytes（約1.2MB） |
| バッファ数 | 2面（ダブルバッファ） |
| 配置先 | 外部SDRAM（128MB） |

### 4.5 描画フロー

```
LVGLオブジェクト変更
       ↓
lv_timer_handler() 呼び出し
       ↓
ダーティエリア検出・レンダリング
       ↓
disp_flush() コールバック呼び出し
       ↓
GLCDCフレームバッファ更新
       ↓
DMA転送 → LCD表示更新
```

---

## 5. OV5640カメラ画像のLCD表示方法

### 5.1 カメラモジュール仕様

| 項目 | 仕様 |
|------|------|
| センサー解像度 | 最大5MP（2642x1952） |
| 出力解像度 | 1024x600（設定済み） |
| 出力フォーマット | YUV422 → RGB565変換 |
| インターフェース | MIPI CSI-2（2レーン） |
| I2Cアドレス | 0x3C（7ビット） |

### 5.2 カメラ→LCD表示アーキテクチャ

`quickstart_ek_ra8p1_ep`プロジェクトでは、以下のアーキテクチャでカメラ画像をLCDに表示しています：

```
OV5640カメラ
     ↓ MIPI CSI-2（YUV422）
MIPI CSI-2 PHY
     ↓
VIN（Video Input）モジュール
     ↓ RGB565変換 + DMA
カメラフレームバッファ（SDRAM）
     ↓
GLCDC レイヤー1
     ↓
LCD表示
```

### 5.3 マルチレイヤー構成

GLCDCのマルチレイヤー機能を使用して、カメラ画像とUI/グラフィックスを重ね合わせます：

| レイヤー | 内容 | 用途 |
|---------|------|------|
| レイヤー1 | カメラ画像（VIN入力） | ライブビデオストリーム |
| レイヤー2 | グラフィックス/UIオーバーレイ | メニュー、情報表示 |

### 5.4 カメラ初期化コード例

```c
// OV5640初期化（ov5640.c参照）
void camera_init(void)
{
    // ハードウェアリセット
    ov5640_hw_reset();

    // OV5640初期化
    ov5640_init();

    // MIPI CSI-2設定
    ov5640_set_mipi_virtual_channel(0);

    // VIN（ビデオ入力）モジュール開始
    R_VIN_Open(&g_vin0_ctrl, &g_vin0_cfg);
    R_VIN_CaptureStart(&g_vin0_ctrl, camera_buffer);
}
```

### 5.5 VINコールバックでのフレームバッファ切り替え

```c
void vin0_callback(capture_callback_args_t *p_args)
{
    if (p_args->event == CAPTURE_EVENT_FRAME_END) {
        // キャプチャ完了：フレームバッファをディスプレイレイヤー1に設定
        display_next_buffer_set(p_args->p_buffer);
    }
}
```

### 5.6 LVGLとカメラ画像の統合方法

LVGLでカメラ画像を表示する方法は2つあります：

**方法1: GLCDCマルチレイヤー（推奨）**
- レイヤー1: カメラ画像（VIN直接出力）
- レイヤー2: LVGL UI（オーバーレイ）
- メリット: 高パフォーマンス、CPUオーバーヘッドなし

**方法2: LVGLキャンバスに描画**
```c
// カメラバッファをLVGLイメージとして表示
lv_obj_t * cam_img = lv_image_create(lv_scr_act());

// イメージディスクリプタを設定
static lv_image_dsc_t cam_dsc = {
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.w = 1024,
    .header.h = 600,
    .data_size = 1024 * 600 * 2,
    .data = camera_buffer,
};

lv_image_set_src(cam_img, &cam_dsc);

// 定期的に更新
void camera_frame_update(void)
{
    lv_obj_invalidate(cam_img);  // 再描画をトリガー
}
```

---

## 6. Cortex-M85コア（e2studio_CPU0）への移植方針

### 6.1 現状分析

リファレンス実装は以下の構成で動作しています：

| 項目 | 現状 |
|------|------|
| MCU | RA8P1（Cortex-M85） |
| RTOS | FreeRTOS |
| グラフィックス | LVGL + D/AVE 2D |
| IDE | e2 studio |

### 6.2 移植のポイント

#### 6.2.1 FSP（Flexible Software Package）の活用

RA8P1向けFSPには以下のモジュールが含まれており、これらを活用します：

- **r_glcdc**: LCDコントローラドライバ
- **rm_lvgl_port**: LVGLポーティングレイヤー
- **r_vin**: ビデオ入力ドライバ（カメラ用）
- **r_mipi_csi2**: MIPI CSI-2ドライバ
- **r_iic_master**: I2Cドライバ（タッチ/カメラ制御）

#### 6.2.2 e2studio_CPU0プロジェクトへの統合手順

1. **FSPコンフィギュレータでペリフェラル追加**
   - GLCDC、VIN、MIPI CSI-2、I2Cを有効化
   - スレッド/タスクの追加

2. **LVGLライブラリの追加**
   ```
   e2studio_CPU0/
   ├── ra/lvgl/lvgl/        # LVGLライブラリ
   ├── src/
   │   ├── lv_port_disp.c   # ディスプレイポート
   │   ├── lv_port_indev.c  # 入力デバイスポート
   │   └── lv_conf.h        # LVGL設定
   └── ui/                   # UIファイル
   ```

3. **lv_conf.hの設定**
   ```c
   #define LV_COLOR_DEPTH          16
   #define LV_HOR_RES_MAX          1024
   #define LV_VER_RES_MAX          600
   #define LV_USE_GPU_RA6M3_G2D    1   // D/AVE 2D有効化
   #define LV_MEM_SIZE             (1024 * 1024)  // 1MB
   ```

4. **メモリ配置（リンカスクリプト）**
   ```
   /* SDRAM領域にフレームバッファを配置 */
   .sdram_data (NOLOAD) : {
       *(.sdram_data*)
       . = ALIGN(64);
       _fb_start = .;
       . += (1024 * 600 * 2 * 2);  /* ダブルバッファ */
       _fb_end = .;
   } > SDRAM
   ```

### 6.3 μT-Kernel環境への移植（将来的な拡張）

既存のμT-Kernel環境に移植する場合の追加考慮事項：

1. **タスク管理の変更**
   - FreeRTOS → μT-Kernel APIへの変換
   - `xTaskCreate()` → `tk_cre_tsk()`
   - `vTaskDelay()` → `tk_dly_tsk()`

2. **同期プリミティブの変更**
   - `xSemaphoreCreateBinary()` → `tk_cre_sem()`
   - `xQueueCreate()` → `tk_cre_mbf()`

3. **タイマー処理**
   ```c
   // μT-Kernelでのlv_timer_handler呼び出し
   void lvgl_task(INT stacd, void *exinf)
   {
       while (1) {
           lv_timer_handler();
           tk_dly_tsk(5);  // 5ms周期
       }
   }
   ```

### 6.4 推奨移植手順

```
Step 1: FSPプロジェクト作成
        ↓
Step 2: GLCDC + タッチパネル動作確認
        ↓
Step 3: LVGL統合・基本UI表示確認
        ↓
Step 4: VIN + カメラ初期化
        ↓
Step 5: カメラ画像表示（レイヤー1）
        ↓
Step 6: LVGL UIオーバーレイ（レイヤー2）
        ↓
Step 7: パフォーマンス最適化
```

### 6.5 パフォーマンス目標

| 指標 | 目標値 |
|------|--------|
| UI更新FPS | 30 fps以上 |
| カメラプレビューFPS | 30 fps |
| CPU使用率 | 70%以下 |
| メモリ使用量 | SDRAM 16MB以下 |

---

## 7. 再利用可能なGUI部品と変動部分の分析

リファレンス実装（`lv_port_renesas_ek_ra8p1`）をベースに別の画面仕様のアプリケーションを作成する際の、再利用可能なコードと作り直しが必要なコードを分類します。

### 7.1 プロジェクト構造と再利用可能性

```
lv_port_renesas_ek_ra8p1/
├── src/                          # アプリケーション層
│   ├── port/                      ★ 再利用可能
│   │   ├── lv_port_disp.c/h       # ディスプレイポーティング
│   │   └── lv_port_indev.c/h      # タッチ入力ポーティング
│   ├── hal_entry.c                ◆ 設定変更で対応
│   ├── new_thread0_entry.c        ▲ 変動部分
│   ├── lv_conf_user.h             ◆ 設定変更で対応
│   ├── Free_RTOS_Hooks.c          ★ 再利用可能
│   └── LLVM_printf_redirect.c     ★ 再利用可能
├── ui/                            # UI定義層
│   ├── ui.c/h                     ▲ 変動部分
│   ├── ui_gen.c/h                 ▲ 変動部分（自動生成）
│   ├── components/                ★/▲ 混在
│   ├── screens/                   ▲ 変動部分
│   ├── images/                    ▲ 変動部分
│   └── fonts/                     ★ 再利用可能
├── ra/                            # Renesas FSP
│   ├── fsp/                       ◆ 設定変更で対応
│   └── lvgl/                      ◆ 設定変更で対応
└── configuration.xml              ◆ 設定変更で対応

凡例: ★ 再利用可能 / ◆ 設定変更で対応 / ▲ 変動部分
```

### 7.2 再利用可能なコード（ハードウェア依存・ポーティング層）

#### 7.2.1 ディスプレイポーティング層

**ファイル:** `src/port/lv_port_disp.c` / `lv_port_disp.h`

| 機能 | 再利用性 |
|------|----------|
| GLCDC初期化（`lv_port_disp_init()`） | そのまま流用可能 |
| フレームバッファ設定 | そのまま流用可能 |
| バックライト制御 | ピン定義のみ変更 |
| LCDリセット処理 | ピン定義のみ変更 |

```c
// ピン定義のみ変更すれば流用可能
#define LCD_BLEN  /* バックライト有効ピン */
#define LCD_RESET /* LCDリセットピン */
```

#### 7.2.2 タッチ入力ポーティング層

**ファイル:** `src/port/lv_port_indev.c` / `lv_port_indev.h`

| 機能 | 再利用性 |
|------|----------|
| FT5X06タッチ初期化 | そのまま流用可能（同じタッチIC使用時） |
| I2C通信処理 | そのまま流用可能 |
| マルチタッチ座標取得 | そのまま流用可能 |
| 割り込み/ポーリング切替 | 設定値変更のみ |

```c
// 設定で駆動方式を選択可能
#define INDEV_EVENT_DRIVEN 0  // 0=ポーリング, 1=イベント駆動
```

#### 7.2.3 その他の再利用可能ファイル

| ファイル | 内容 | 再利用性 |
|---------|------|----------|
| `Free_RTOS_Hooks.c` | FreeRTOSフック関数 | そのままコピー可能 |
| `LLVM_printf_redirect.c` | printf出力リダイレクト | そのままコピー可能 |
| `lv_conf_user.h` | LVGL設定 | メモリサイズ調整のみ |

### 7.3 再利用可能なUIコンポーネント

`ui/components/`フォルダ内のコンポーネントの再利用可能性：

| コンポーネント | 機能 | 再利用性 | 備考 |
|---------------|------|----------|------|
| `row_create()` | Flexbox横配置コンテナ | ★★★★★ | 汎用レイアウト部品 |
| `column_create()` | Flexbox縦配置コンテナ | ★★★★★ | 汎用レイアウト部品 |
| `checkbox_create()` | チェックボックス | ★★★★ | バインド対象の変更のみ |
| `icon_create()` | アイコン表示 | ★★★ | 画像リソースの変更が必要 |
| `title_create()` | タイトルラベル | ★★★ | スタイル調整で流用可能 |
| `subtitle_create()` | サブタイトル | ★★★ | スタイル調整で流用可能 |

**汎用コンテナの実装例（そのまま流用可能）：**

```c
// row_create() - 別の画面でもそのまま使える
lv_obj_t * row_create(lv_obj_t * parent) {
    static lv_style_t style_main;
    lv_style_set_layout(&style_main, LV_LAYOUT_FLEX);
    lv_style_set_flex_flow(&style_main, LV_FLEX_FLOW_ROW);
    // 汎用レイアウトコンテナとして機能
}
```

### 7.4 変動部分（画面仕様変更時に作り直しが必要）

#### 7.4.1 画面特定のUIコンポーネント

| コンポーネント | 再利用性 | 理由 |
|---------------|----------|------|
| `header_create()` | ★★ | ヘッダーレイアウトは画面仕様に依存 |
| `setclock_create()` | ★★ | 時間設定は機能特定 |

#### 7.4.2 画面定義ファイル（完全に作り直し）

**ファイル:** `ui/screens/*/`

```
ui/screens/
└── settings/
    └── settings_gen.c  # 設定画面の定義 → 別画面では全く新規
```

現在の設定画面（`settings_create()`）の構成：
- ヘッダー（タイトル、アイコン）
- チェックボックス群（通知、Bluetooth、WiFi）
- 時刻設定ローラー

→ 別の画面仕様では、構成が全く異なるため全て新規実装が必要

#### 7.4.3 UI自動生成ファイル

**ファイル:** `ui/ui_gen.c` / `ui/ui_gen.h`

| 要素 | 内容 | 変動性 |
|------|------|--------|
| `lv_subject_t` | グローバル状態変数 | 画面仕様に依存 |
| `font_*` | フォントリソース | 再利用可能 |
| `img_*` | 画像リソース | 画面仕様に依存 |

#### 7.4.4 画像リソース（完全に作り直し）

**ファイル:** `ui/images/`

| ファイル | 用途 |
|---------|------|
| `bell-solid.png` | 通知アイコン |
| `bluetooth-brands.png` | Bluetooth状態 |
| `wifi-solid.png` | WiFi状態 |
| `img_*_data.c` | C配列化データ |

→ 新しい画面仕様では、新しいアイコン・画像が必要

### 7.5 設定変更で対応可能なファイル

| ファイル | 変更内容 |
|---------|----------|
| `lv_conf_user.h` | メモリサイズ、機能フラグの調整 |
| `configuration.xml` | FSP設定（GPIO、I2C、GLCDC等） |
| `lv_port_disp.c` | ピン定義（ハードウェアが異なる場合） |
| `lv_port_indev.h` | タッチ駆動方式の選択 |

### 7.6 新規画面開発時のワークフロー

```
Step 1: ポーティング層のコピー（そのまま流用）
        ├── lv_port_disp.c/h
        ├── lv_port_indev.c/h
        ├── Free_RTOS_Hooks.c
        └── lv_conf_user.h
              ↓
Step 2: 汎用コンポーネントのコピー
        ├── row_create()
        ├── column_create()
        └── checkbox_create()
              ↓
Step 3: 画面定義の新規作成
        └── myscreen_gen.c を実装
              ↓
Step 4: 画像・アイコンリソースの作成
        └── ui/images/ に新規追加
              ↓
Step 5: ui_gen.c/h の編集
        ├── lv_subject_t の定義
        └── リソース初期化処理
              ↓
Step 6: スレッドエントリの修正
        └── new_thread0_entry.c で新画面を呼び出し
```

### 7.7 再利用可能性サマリー

| カテゴリ | 再利用可能 | 作り直し | 工数削減効果 |
|---------|------------|----------|-------------|
| ハードウェアポーティング | 100% | 0% | 高 |
| LVGL設定 | 90% | 10% | 高 |
| 汎用UIコンポーネント | 50% | 50% | 中 |
| 画面定義 | 0% | 100% | - |
| 画像リソース | 0% | 100% | - |
| フォント | 80% | 20% | 中 |

**結論:**
- ポーティング層（`src/port/`）は**ほぼ100%再利用可能**
- 汎用UIコンポーネント（row, column, checkbox）は**そのまま流用可能**
- 画面定義・画像リソースは**完全に新規作成が必要**
- **2つ目以降の画面開発は、初回の30〜50%の工数**で完成可能

---

## 8. まとめ

### 8.1 LVGLの画面デザイン

- スクリーン→ウィジェットの階層構造でUIを構築
- `lv_xxx_create()`関数でウィジェットを作成
- イベントコールバックでユーザー操作を処理
- スタイルとレイアウト（Flexbox/Grid）で見た目を調整

### 8.2 カメラ画像表示

- OV5640 → MIPI CSI-2 → VIN → GLCDC の経路で表示
- マルチレイヤー構成でカメラ画像とUIを重ね合わせ
- VINコールバックでフレームバッファを切り替え

### 8.3 移植方針

- FSPのrm_lvgl_portモジュールを活用
- SDRAMにフレームバッファを配置
- D/AVE 2Dハードウェアアクセラレーションを有効化
- 段階的に機能を追加・検証

---

## 9. 参考資料

- [LVGL公式ドキュメント](https://docs.lvgl.io/)
- [Renesas RA8P1 FSPドキュメント](https://www.renesas.com/jp/ja/products/microcontrollers-microprocessors/ra-cortex-m-mcus/ra8p1-480-mhz-arm-cortex-m85-powerful-ai-acceleration-and-advanced-security)
- `reference_projects/lv_port_renesas_ek_ra8p1/` - LVGLリファレンス実装
- `reference_projects/quickstart_ek_ra8p1_ep/` - カメラ対応実装
- `doc/analysis_report/lvgl_display_mechanism_analysis.md` - LVGL表示機構詳細
