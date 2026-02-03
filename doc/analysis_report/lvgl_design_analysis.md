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

## 7. まとめ

### 7.1 LVGLの画面デザイン

- スクリーン→ウィジェットの階層構造でUIを構築
- `lv_xxx_create()`関数でウィジェットを作成
- イベントコールバックでユーザー操作を処理
- スタイルとレイアウト（Flexbox/Grid）で見た目を調整

### 7.2 カメラ画像表示

- OV5640 → MIPI CSI-2 → VIN → GLCDC の経路で表示
- マルチレイヤー構成でカメラ画像とUIを重ね合わせ
- VINコールバックでフレームバッファを切り替え

### 7.3 移植方針

- FSPのrm_lvgl_portモジュールを活用
- SDRAMにフレームバッファを配置
- D/AVE 2Dハードウェアアクセラレーションを有効化
- 段階的に機能を追加・検証

---

## 8. 参考資料

- [LVGL公式ドキュメント](https://docs.lvgl.io/)
- [Renesas RA8P1 FSPドキュメント](https://www.renesas.com/jp/ja/products/microcontrollers-microprocessors/ra-cortex-m-mcus/ra8p1-480-mhz-arm-cortex-m85-powerful-ai-acceleration-and-advanced-security)
- `reference_projects/lv_port_renesas_ek_ra8p1/` - LVGLリファレンス実装
- `reference_projects/quickstart_ek_ra8p1_ep/` - カメラ対応実装
- `doc/analysis_report/lvgl_display_mechanism_analysis.md` - LVGL表示機構詳細
