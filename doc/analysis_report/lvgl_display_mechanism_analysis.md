# LVGL画面表示機構 解析レポート

**作成日**: 2026-02-03
**対象**: `reference_projects/lv_port_renesas_ek_ra8p1`
**目的**: LVGL画面表示の仕組みを解析し、Cortex-M85コア(e2studio_CPU0)への移植指針を提供

---

## 1. LVGL画面表示の仕組み

### 1.1 概要

LVGLは抽象化されたディスプレイポートを通じて、ハードウェアのLCDコントローラ（GLCDC）と連携します。描画はダブルバッファリングにより実現され、ティアリングのない滑らかな表示を提供します。

### 1.2 描画フロー

```
LVGLアプリケーション（オブジェクト変更）
         ↓
  lv_refr_now()（自動/明示呼び出し）
         ↓
    ダーティエリア検出・描画
         ↓
  lv_disp_flush()（フラッシュコールバック）
         ↓
 フレームバッファ更新（SDRAM）
         ↓
    GLCDCレジスタ設定
         ↓
   LCD へDMA転送（自動）
         ↓
      画面表示更新
```

### 1.3 ディスプレイドライバの実装（lv_port_disp.c）

#### ディスプレイ初期化

```c
void lv_port_disp_init(void)
{
    // 1. GLCDC初期化（LCDリセット、タイミング設定）
    disp_init();

    // 2. LVGLディスプレイ構造体作成
    lv_disp_t *disp = lv_disp_create(1024, 600);

    // 3. ダブルバッファ設定
    lv_disp_set_buffers(disp, buf1, buf2, size, LV_DISP_RENDER_MODE_PARTIAL);

    // 4. フラッシュコールバック登録
    lv_disp_set_flush_cb(disp, lv_disp_flush);
}
```

#### フレームバッファ仕様

| 項目 | 値 |
|------|-----|
| 解像度 | 1024 x 600 ピクセル |
| カラーフォーマット | RGB565（16ビット） |
| バッファサイズ | 1,228,800 bytes（約1.2MB） |
| バッファ数 | 2面（ダブルバッファ） |
| 配置先 | 外部SDRAM（128MB） |
| 目的 | ティアリング防止、スムーズアニメーション |

#### 主要API関数

| 関数 | 役割 | 呼び出し元 |
|------|------|-----------|
| `lv_port_disp_init()` | GLCDC+LVGL初期化 | main/hal_entry |
| `lv_disp_flush()` | フレームバッファフラッシュ | LVGLフラッシュコールバック |
| `glcdc_flush_finish_event()` | フラッシュ完了イベント | LVGLイベント |
| `lvgl_glcdc_callback()` | GLCDC割込みハンドラ | GLCDC ISR |
| `disp_init()` | 低レベルLCD初期化 | lv_port_disp_init() |

---

## 2. タッチイベント検出の仕組み

### 2.1 タッチコントローラ仕様

| 項目 | 仕様 |
|------|------|
| 型式 | FT5X06（静電容量式） |
| マルチタッチ | 最大5点同時検出 |
| I2Cスレーブアドレス | 0x38 |
| 割込みモード | 立下りエッジ検出（IRQ19） |
| データサイズ | 43バイト（レジスタ + 5点×6bytes） |

### 2.2 タッチイベント検出フロー

```
タッチ発生
     ↓
FT5X06 IRQピン（P1-7）がLowに
     ↓
External IRQ Ch.19 割込み発火
     ↓
ISR: touch_irq_callback()
     ↓
バイナリセマフォ通知
     ↓
lv_timer_handler()
     ↓
lv_indev_read()
     ↓
I2C読み取り（FT5X06）
     ↓
LVGLイベント発火
     ↓
オブジェクトイベントハンドラ実行
```

### 2.3 割込みハンドラ実装

```c
void touch_irq_callback(external_irq_callback_args_t *p_args)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // バイナリセマフォを通知
    xSemaphoreGiveFromISR(g_irq_binary_semaphore, &xHigherPriorityTaskWoken);

    // FreeRTOS: ISRから最優先のスレッドをウェイクアップ
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

### 2.4 タッチデータ読み取り

```c
void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    // I2C経由でFT5X06タッチデータ取得
    fsp_err_t err = R_IIC_MASTER_Read(&g_i2c_master_ctrl, touch_buffer, 43);

    // タッチポイント数抽出
    uint8_t touch_count = touch_buffer[2] & 0x0F;

    // 座標抽出（X, Y）
    for (int i = 0; i < touch_count; i++) {
        data->point.x = (touch_buffer[3+i*6] << 8) | touch_buffer[4+i*6];
        data->point.y = (touch_buffer[5+i*6] << 8) | touch_buffer[6+i*6];
    }

    // LVGLにイベント通知
    data->state = touch_count > 0 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
```

### 2.5 入力デバイスAPI

| 関数 | 役割 | 呼び出し元 |
|------|------|-----------|
| `lv_port_indev_init()` | タッチパネル初期化 | main/hal_entry |
| `touchpad_read()` | タッチ座標読み取り | LVGL入力ハンドラ |
| `touchpad_is_pressed()` | 押下状態確認 | touchpad_read() |
| `touchpad_get_xy()` | 座標・状態取得 | touchpad_read() |
| `touch_irq_callback()` | タッチ割込みハンドラ | ICU ISR |
| `comms_i2c_callback()` | I2C完了コールバック | IIC ISR |

---

## 3. 画面遷移の仕組み

### 3.1 スクリーン構造

```
Root Screen (lv_scr_act())
├── Screen 1: 初期画面
│   ├── Label（タイトル）
│   ├── Container（メインコンテンツ）
│   └── Button Array（ナビゲーションボタン）
│
└── Screen 2: サブ画面
    ├── Chart（グラフ）
    ├── Slider（調整スライダー）
    └── Button（戻るボタン）
```

### 3.2 画面遷移メカニズム

```c
// ボタンクリックイベントハンドラ
void button_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        // 現在の画面を削除
        lv_obj_del(lv_scr_act());

        // 新しい画面を作成
        lv_obj_t *new_screen = create_screen_2();

        // LVGLに新しい画面をセット（アニメーション付き）
        lv_scr_load_anim(new_screen, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
    }
}
```

### 3.3 画面遷移の動作フロー

1. ユーザーがボタンをタッチ
2. タッチイベント → LVGLに入力
3. LVGLがボタンのclickイベントコールバック実行
4. コールバック内で画面削除/新規作成
5. `lv_scr_load()` でLVGLが新しい画面をレンダリング開始
6. フラッシュコールバック → GLCDCにバッファ更新指示
7. 新しい画面がLCDに表示される

### 3.4 タイミング詳細

```
t=0ms:     タッチイベント検出
t=1-5ms:   I2C読み取り、LVGLイベント処理
t=5-10ms:  画面削除、新規作成、レイアウト計算
t=10-20ms: アニメーション開始（フェードイン）
           ├─ フレーム1: alpha=0.0
           ├─ フレーム2: alpha=0.2
           ├─ ...
           └─ フレーム10: alpha=1.0（完全表示）
t=310ms:   アニメーション完了
```

---

## 4. RA8P1 ハードウェア設定

### 4.1 GLCDC（Graphics LCD Controller）設定

| 項目 | 値 |
|------|-----|
| 解像度 | 1024 x 600 ピクセル |
| カラーフォーマット | RGB565（16ビット） |
| フレームレート | 60 Hz（VSYNC周期 16.67ms） |
| バッファモード | ダブルバッファ（ピンポン） |
| DMA | 有効（フレームバッファ→LCD自動転送） |
| GPUアクセラレータ | D/AVE 2D（矩形塗りつぶし、円描画、画像ブリット等） |

### 4.2 GPIOピン設定

#### LVGL関連の重要なピン

| ピン | 機能 | 方向 | 用途 |
|------|------|------|------|
| P7-7～P7-15, P8-5～P8-7 | LCD_GRAPHICS | 出力 | LCDデータライン（R,G,B） |
| P9-2～P9-4, P9-10～P9-15 | LCD_GRAPHICS | 出力 | LCDデータライン |
| P11-0～P11-7 | LCD_GRAPHICS | 出力 | LCDデータライン |
| P5-13, P5-15 | LCD_GRAPHICS | 出力 | LCD_VSYNC, LCD_HSYNC等 |
| P5-14 | GPIO出力 | 出力 | LCDバックライト（アクティブHIGH） |
| P1-7 | External IRQ Ch.19 | 入力 | タッチパネル割込み |
| P4-0 | IIC1_SDA | 双方向 | タッチコントローラI2C |
| P4-1 | IIC1_SCL | 出力 | タッチコントローラI2C |

#### GPIO設定フロー

```
pin_data.c (ioport_pin_cfg_t 配列)
         ↓
   R_IOPORT_PinsCfg() 呼び出し
         ↓
    各ピンを指定の機能に設定
         ↓
  ├─ GLCDCパラレル出力接続
  ├─ タッチIRQ割込み有効化
  └─ I2Cペリフェラル接続
```

### 4.3 クロック設定（bsp_clock_cfg.h）

| クロック | 周波数 | 用途 |
|---------|--------|------|
| ICLK | 1000 MHz | CPUメインクロック（M85） |
| PCLKA | 500 MHz | ペリフェラル（高速） |
| PCLKB | 250 MHz | I2C, SPI等 |
| PCLKD | 500 MHz | VIN, GLCDC |
| LCLK | 1000 MHz | L2キャッシュ |

#### PLL設定

- 入力: Crystal 24 MHz
- 目標: 1000 MHz (M85), 250 MHz (M33)
- PLL倍率: 約42倍

#### RTOSティック

- 周波数: 1000 Hz（1ms = 1ティック）
- タイマー: SysTick（ARMタイマー）

### 4.4 I2C（IIC1）設定

| 項目 | 設定 |
|------|------|
| チャネル | IIC1 |
| クロック | Fast-mode（400 kHz） |
| スレーブアドレス | 0x38（FT5X06） |
| バスマスター | RA8P1 |
| タイムアウト | 1000 ms |

### 4.5 使用ペリフェラル一覧

| ペリフェラル | チャネル/設定 | 用途 |
|-------------|--------------|------|
| GLCDC | - | LCDコントローラ |
| D/AVE 2D | - | グラフィックスアクセラレータ |
| SCI_B8 (UART) | 115200/8N1 | デバッグ出力 |
| IIC1 (I2C) | Fast-mode | タッチコントローラ通信 |
| ICU | Ch.19 | タッチ割込み |
| IOPORT | - | GPIO制御 |
| SDRAM | 128MB, 32bit | フレームバッファ |

---

## 5. LVGLソフトウェア構成（静的視点）

### 5.1 モジュール構成図

```
┌─────────────────────────────────────────────────────────┐
│           Application UI Layer                          │
│        (Screens, Widgets, Animations)                   │
└────────────────────────┬────────────────────────────────┘
                         ↑
┌────────────────────────┴────────────────────────────────┐
│              LVGL Core Library (v8.x)                   │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐    │
│  │  Rendering   │ │   Object     │ │    Style     │    │
│  │   Engine     │ │   Manager    │ │   System     │    │
│  └──────────────┘ └──────────────┘ └──────────────┘    │
│  ┌──────────────┐ ┌──────────────┐                     │
│  │Input Device  │ │  Animation   │                     │
│  │   Handler    │ │   System     │                     │
│  └──────────────┘ └──────────────┘                     │
└────────────────────────┬────────────────────────────────┘
                         ↑
┌────────────────────────┴────────────────────────────────┐
│                  Porting Layer                          │
│  ┌──────────────────┐  ┌──────────────────┐            │
│  │  lv_port_disp.c  │  │ lv_port_indev.c  │            │
│  │   (Display)      │  │    (Input)       │            │
│  └──────────────────┘  └──────────────────┘            │
│  ┌──────────────────┐                                  │
│  │ lv_conf_user.h   │                                  │
│  │ (Configuration)  │                                  │
│  └──────────────────┘                                  │
└────────────────────────┬────────────────────────────────┘
                         ↑
┌────────────────────────┴────────────────────────────────┐
│                    Renesas FSP                          │
│  ┌────────────┐ ┌────────────┐ ┌────────────┐          │
│  │  r_glcdc   │ │   r_icu    │ │r_iic_master│          │
│  │   (LCD)    │ │(Interrupt) │ │   (I2C)    │          │
│  └────────────┘ └────────────┘ └────────────┘          │
│  ┌────────────┐ ┌────────────┐                         │
│  │rm_lvgl_port│ │ FreeRTOS   │                         │
│  │   (FSP)    │ │   Port     │                         │
│  └────────────┘ └────────────┘                         │
└────────────────────────┬────────────────────────────────┘
                         ↑
┌────────────────────────┴────────────────────────────────┐
│                     Hardware                            │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐  │
│  │ RA8P1    │ │  GLCDC   │ │  SDRAM   │ │  FT5X06  │  │
│  │   MCU    │ │Peripheral│ │ (128MB)  │ │  Touch   │  │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘  │
│  ┌──────────────────────────────────────┐              │
│  │      LCD Display (1024x600)          │              │
│  └──────────────────────────────────────┘              │
└─────────────────────────────────────────────────────────┘
```

### 5.2 各モジュールの役割

#### アプリケーション層

| モジュール | 役割 |
|-----------|------|
| Screens | 画面定義（メイン画面、設定画面など） |
| Widgets | UIコンポーネント（ボタン、ラベル、チャート等） |
| Animations | 画面遷移アニメーション、エフェクト |

#### LVGLコア

| モジュール | 役割 |
|-----------|------|
| Rendering Engine | 描画処理、ダーティエリア管理 |
| Object Manager | ウィジェットのライフサイクル管理 |
| Style System | テーマ、スタイル、色管理 |
| Input Device Handler | タッチ・ボタン入力処理 |
| Animation System | アニメーション補間、タイミング制御 |

#### ポーティング層

| ファイル | 役割 | インターフェース |
|---------|------|-----------------|
| lv_port_disp.c | ディスプレイドライバ | GLCDC ← → LVGL |
| lv_port_indev.c | 入力デバイスドライバ | FT5X06 ← → LVGL |
| lv_conf_user.h | ユーザー設定 | メモリ、フォント、機能有効化 |

#### FSPモジュール

| モジュール | インスタンス | 役割 |
|-----------|-------------|------|
| r_glcdc | g_display0 | LCDコントローラ（1024x600、RGB565） |
| r_drw | drw | グラフィックスアクセラレータ |
| r_iic_master | g_i2c_master0 | I2C通信（Ch.1、Fast-mode） |
| r_icu | g_external_irq0 | タッチ割込み（Ch.19） |
| rm_lvgl_port | g_rm_lvgl_port0 | LVGLインターフェース |

### 5.3 モジュール間インターフェース

```
┌─────────────────────────────────────────────────────────────────┐
│                        連携フロー                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  [Application] ──(Widget API)──→ [LVGL Core]                   │
│        │                              │                         │
│        │                              ├──(lv_disp_flush())──→   │
│        │                              │                     ↓   │
│        │                              │            [lv_port_disp]│
│        │                              │                     │   │
│        │                              │     (R_GLCDC_BufferChange)
│        │                              │                     ↓   │
│        │                              │              [r_glcdc]   │
│        │                              │                     │   │
│        │                              │            (DMA Transfer)│
│        │                              │                     ↓   │
│        │                              │                 [LCD]    │
│        │                              │                         │
│        │                              ├──(lv_indev_read())──→   │
│        │                              │                     ↓   │
│        │                              │           [lv_port_indev]│
│        │                              │                     │   │
│        │                              │         (R_IIC_MASTER_Read)
│        │                              │                     ↓   │
│        │                              │           [r_iic_master] │
│        │                              │                     │   │
│        │                              │              (I2C Bus)   │
│        │                              │                     ↓   │
│        │                              │               [FT5X06]   │
│        │                              │                         │
└─────────────────────────────────────────────────────────────────┘
```

### 5.4 LVGL設定（lv_conf_user.h）

```c
// メモリ設定
#define LV_MEM_SIZE (1024 * 1024)              // 1MB ヒープ

// 画面設定
#define LV_HOR_RES_MAX 1024
#define LV_VER_RES_MAX 600

// 描画最適化
#define LV_DISP_ROT_OVERLAY_PART_AREA_MAX 102400  // 256KB
#define LV_DISP_RENDER_LVL 2                       // 描画レイヤー

// フォント設定
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1

// グラフィックス設定
#define LV_USE_GPU 1  // D/AVE 2D 有効
```

---

## 6. LVGLソフトウェア構成（動的視点）

### 6.1 初期化シーケンス

```
main()
  ↓
g_hal_init()                        // FSP共通モジュール初期化
  ├─ クロック初期化
  ├─ SDRAM初期化
  ├─ GPIO初期化（pin_data.c）
  └─ UART初期化（デバッグ出力）
  ↓
blinky_thread_create()              // FreeRTOSスレッド作成
  ↓
vTaskStartScheduler()               // スケジューラ起動
  ↓
blinky_thread_func()                // スレッド実行
  ├─ rtos_startup_common_init()
  │   └─ g_hal_init() 実行
  └─ blinky_thread_entry()          // アプリケーション開始
        ↓
      hal_entry()                   // HALエントリ
        ↓
      lv_init()                     // LVGL初期化
        ↓
      lv_port_disp_init()           // LVGLディスプレイ初期化
        ├─ GLCDC設定
        ├─ フレームバッファ割り当て
        ├─ lv_disp_create()
        └─ フラッシュコールバック登録
        ↓
      lv_port_indev_init()          // LVGL入力初期化
        ├─ FT5X06初期化
        ├─ I2C通信設定
        ├─ 外部割込み（Ch.19）有効化
        └─ lv_indev_drv_register()
        ↓
      UI_SCREEN_INIT()              // 初期画面作成
        ├─ create_screen_main()
        └─ lv_scr_load()
        ↓
      while(1)                      // メインループ
        └─ lv_timer_handler()       // LVGL定期処理
```

### 6.2 第1画面：メイン画面

```c
// 初期化時に作成
lv_obj_t *main_screen = lv_obj_create(NULL);

// タイトルラベル作成
lv_obj_t *label = lv_label_create(main_screen);
lv_label_set_text(label, "Welcome to RA8P1");
lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);

// ナビゲーションボタン作成
lv_obj_t *btn = lv_btn_create(main_screen);
lv_obj_set_size(btn, 200, 50);
lv_obj_center(btn);
lv_obj_add_event_cb(btn, button_event_handler, LV_EVENT_CLICKED, NULL);

lv_obj_t *btn_label = lv_label_create(btn);
lv_label_set_text(btn_label, "Next Screen");

// 画面を表示
lv_scr_load(main_screen);
```

#### メイン画面の構成要素

- タイトルテキスト（ラベル）
- ナビゲーションボタン
- ステータスバー（時刻/バッテリー等）
- グラデーション背景

### 6.3 第2画面への遷移

```c
void button_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        transition_to_screen_2();
    }
}

void transition_to_screen_2(void)
{
    // 古い画面削除
    lv_obj_del(lv_scr_act());

    // 新画面作成
    lv_obj_t *screen2 = create_screen_2();

    // 遷移アニメーション設定
    lv_scr_load_anim(screen2, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
}

lv_obj_t *create_screen_2(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);

    // チャート（グラフ）
    lv_obj_t *chart = lv_chart_create(screen);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_obj_set_size(chart, 400, 200);
    lv_obj_align(chart, LV_ALIGN_CENTER, 0, -50);

    // スライダー（値調整）
    lv_obj_t *slider = lv_slider_create(screen);
    lv_obj_set_width(slider, 300);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 100);
    lv_obj_add_event_cb(slider, slider_handler, LV_EVENT_VALUE_CHANGED, NULL);

    // 戻るボタン
    lv_obj_t *btn_back = lv_btn_create(screen);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_add_event_cb(btn_back, back_button_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn_back);
    lv_label_set_text(label, "Back");

    return screen;
}
```

### 6.4 メインループ処理

```c
while (1)
{
    // LVGLタイマー実行（アニメーション、自動リフレッシュ等）
    lv_timer_handler();

    // FreeRTOS制御へ
    vTaskDelay(pdMS_TO_TICKS(5));  // 5ms周期
}
```

#### lv_timer_handler()の内部処理

1. **入力デバイス読み取り**: `lv_indev_read()`
   - タッチパネルから座標読み取り
   - オブジェクトヒットテスト
   - イベント発火

2. **アニメーション更新**: `lv_anim_refr_now()`
   - 現在時刻に基づいて値更新
   - フレームごとに中間値計算

3. **オブジェクト無効化エリア追跡**: `lv_obj_mark_dirty()`
   - 変更があったオブジェクトのみ記録
   - 次フラッシュ時に該当エリアのみ再描画

4. **描画＆フラッシュ**: `lv_refr_now()`
   - ダーティエリア内を描画
   - `lv_disp_flush()` 実行
   - GLCDCへバッファ更新指示

---

## 7. e2studio_CPU0への移植ステップ

### 7.1 移植戦略概要

現在の`e2studio_CPU0`はLED点滅のみの簡単なプログラムです。LVGL画面表示機能を段階的に追加します。

### 7.2 Phase 1: 基盤整備

**目標**: LVGLライブラリとFSPモジュールの追加

#### 1.1 LVGLライブラリのコピー

```bash
cp -r reference_projects/lv_port_renesas_ek_ra8p1/ra/lvgl e2studio_CPU0/ra/
```

#### 1.2 LVGLポーティングファイルの追加

```bash
cp reference_projects/lv_port_renesas_ek_ra8p1/src/lv_port_*.c e2studio_CPU0/src/
cp reference_projects/lv_port_renesas_ek_ra8p1/src/lv_conf_user.h e2studio_CPU0/src/
```

#### 1.3 FSPモジュール設定

e2studioで`configuration.xml`を開き、以下のコンポーネントを追加:

| モジュール | 設定 |
|-----------|------|
| Graphics LCD (r_glcdc) | 1024x600, RGB565 |
| D/AVE 2D (r_drw) | グラフィックスアクセラレータ |
| IIC Master (r_iic_master) | I2C Ch.1, 400kHz |
| External IRQ (r_icu) | Ch.19（タッチ割込み） |
| FreeRTOS Port | LVGLタイマー用 |

#### 1.4 ビルド確認

- エラーフリーでビルド可能か確認
- LVGLコンパイルエラー修正

### 7.3 Phase 2: ハードウェア層の統合

**目標**: GLCDCとFT5X06の正常動作確認

#### 2.1 GPIOピン設定

| 設定対象 | ピン |
|---------|------|
| LCD出力ピン | RGB データ、HSYNC, VSYNC |
| バックライト制御ピン | P5-14（アクティブHIGH） |
| タッチ割込みピン | P1-7（External IRQ Ch.19） |
| I2Cピン | P4-0, P4-1 |

#### 2.2 GLCDC初期化テスト

```c
void test_glcdc(void)
{
    // GLCDC を起動
    R_GLCDC_Open(&g_display0_ctrl, &g_display0_cfg);

    // テストパターン描画（緑色）
    fill_frame_buffer(0x07E0);  // RGB565 green

    // LCD に表示確認
    // → 緑色画面が表示されたら成功
}
```

#### 2.3 タッチパネル初期化テスト

```c
void test_touch(void)
{
    // FT5X06初期化
    R_IIC_MASTER_Open(&g_i2c_master0_ctrl, &g_i2c_master0_cfg);

    // タッチデータ読み取りテスト
    uint8_t buffer[43];
    R_IIC_MASTER_Read(&g_i2c_master0_ctrl, buffer, 43, false);

    // ポイント数検出
    uint8_t points = buffer[2] & 0x0F;
    printf("Touch points: %d\n", points);
}
```

#### 2.4 ISRハンドラ確認

- `touch_irq_callback()` が呼ばれているか確認
- デバッグ出力でISR実行確認

### 7.4 Phase 3: LVGL統合

**目標**: LVGLコア機能の動作確認

#### 3.1 メモリ確保

```c
// lv_conf_user.h
#define LV_MEM_SIZE (1024 * 1024)  // 1MB ヒープ
```

#### 3.2 LVGL初期化

```c
void main_task(void)
{
    // LVGL 初期化
    lv_init();

    // ディスプレイ初期化
    lv_port_disp_init();

    // 入力デバイス初期化
    lv_port_indev_init();

    // メインループ
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
```

#### 3.3 テストUI作成

```c
void create_test_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);

    // テスト用ボタン
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 200, 50);
    lv_obj_center(btn);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "Test Button");

    lv_scr_load(scr);
}
```

#### 3.4 確認事項

- ボタンがLCDに表示されるか確認
- タッチ反応確認（色変化など）

### 7.5 Phase 4: リファレンス実装の移植

**目標**: 実際の画面デザインの移植

#### 4.1 UIコンポーネントの移植

```bash
cp -r reference_projects/lv_port_renesas_ek_ra8p1/src/ui e2studio_CPU0/src/
```

#### 4.2 画面定義コードの統合

```c
// src/ui/screens/main_screen.c
lv_obj_t *ui_main_screen;

void ui_init(void)
{
    ui_main_screen = create_screen_main();
    // その他画面初期化
}
```

#### 4.3 ナビゲーションロジックの実装

```c
typedef enum {
    SCREEN_MAIN,
    SCREEN_SETTINGS,
    SCREEN_GRAPH,
    // ...
} screen_id_t;

void navigate_to_screen(screen_id_t screen_id)
{
    lv_obj_del(lv_scr_act());

    lv_obj_t *screen = NULL;
    switch (screen_id) {
        case SCREEN_MAIN:
            screen = create_screen_main();
            break;
        case SCREEN_SETTINGS:
            screen = create_screen_settings();
            break;
        // ...
    }

    if (screen) {
        lv_scr_load(screen);
    }
}
```

#### 4.4 テスト項目

- 各ボタンのクリック
- スライダー/チャート等の動作確認
- アニメーション確認

### 7.6 Phase 5: 最適化と検証

**目標**: パフォーマンス確保、バグ修正

#### 5.1 フレームレート測定

```c
static uint32_t frame_count = 0;
static uint32_t last_tick = 0;

void measure_fps(void)
{
    frame_count++;
    uint32_t now = xTaskGetTickCount();
    if (now - last_tick >= 1000) {
        printf("FPS: %lu\n", frame_count);
        frame_count = 0;
        last_tick = now;
    }
}
```

#### 5.2 メモリ使用量監視

```c
void check_memory(void)
{
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    printf("Used: %lu/%lu bytes\n",
           mon.total_size - mon.free_size,
           mon.total_size);
}
```

#### 5.3 パフォーマンス目標

| 指標 | 目標値 |
|------|--------|
| フレームレート | 30 FPS以上 |
| CPU使用率 | 90%以下 |
| LVGLヒープ残量 | 200KB以上 |

### 7.7 移植チェックリスト

| 項目 | Phase | 確認方法 |
|------|-------|---------|
| LVGLライブラリ | 1 | コンパイル成功 |
| GLCDC初期化 | 2 | LCDに色表示 |
| タッチパネル | 2 | デバッグ出力で座標確認 |
| LVGL基本画面 | 3 | LCDにボタン表示 |
| 画面遷移 | 3 | ボタンクリックで画面変化 |
| 実装画面 | 4 | リファレンス同様の表示 |
| FPS測定 | 5 | 30 FPS以上確保 |
| メモリ | 5 | ヒープ残量あり |

### 7.8 既知の課題と対応

#### 課題1: デュアルコア構成

- CPU0とCPU1が同じGLCDCとSDRAMを共有
- **対応**: CPU1はカメラ処理専用に、CPU0はLVGL専用に分離

#### 課題2: メモリ競合

- フレームバッファ（2.4MB）+ LVGLヒープ（1MB）がSDRAM必要
- **対応**: SDRAM 128MBなので問題なし

#### 課題3: タッチパネルタイムアウト

- I2Cハング可能性
- **対応**: タイムアウト値を適切に設定（1000ms推奨）

---

## 8. FreeRTOSとLVGLのインターフェース

### 8.1 概要

LVGLはFreeRTOSと密接に連携して動作します。主な連携ポイントは以下の通りです：

- **タスク管理**: LVGLメインループはFreeRTOSタスク内で実行
- **同期機構**: セマフォ、イベントグループによるISR-タスク間通信
- **タイミング**: FreeRTOSティックによる時間管理

### 8.2 FreeRTOS機能の使用状況

#### 8.2.1 セマフォ（Semaphore）

**バイナリセマフォ - タッチイベント通知用**

| 項目 | 内容 |
|------|------|
| 変数名 | `g_irq_binary_semaphore` |
| 用途 | ISR → LVGLスレッドへのタッチイベント通知 |
| 生成 | 静的割り当て |

```c
// ISRからのセマフォ通知
void touch_irq_callback(external_irq_callback_args_t *p_args)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(g_irq_binary_semaphore, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// タッチタスクからの待機（lv_port_indev.c内）
xSemaphoreTake(g_irq_binary_semaphore, pdMS_TO_TICKS(1000));
```

**カウンティングセマフォ - FSP初期化同期用**

| 項目 | 内容 |
|------|------|
| 変数名 | `g_fsp_common_initialized_semaphore` |
| 用途 | 複数スレッドのFSP初期化完了同期 |
| 初期カウント | 256 |

```c
// main()内
g_fsp_common_initialized_semaphore = xSemaphoreCreateCounting(256, 1);

// スレッド内（rtos_startup_common_init）
xSemaphoreTake(g_fsp_common_initialized_semaphore, portMAX_DELAY);
// 初期化処理...
xSemaphoreGive(g_fsp_common_initialized_semaphore);
```

#### 8.2.2 イベントグループ（Event Groups）

**I2C通信同期用**

| 項目 | 内容 |
|------|------|
| 変数名 | `g_i2c_event_group` |
| ビット定義 | COMPLETE (0x1), ABORT (0x2) |
| 用途 | I2C転送完了/エラー通知 |

```c
// I2C完了コールバック内（ISRコンテキスト）
void comms_i2c_callback(rm_comms_callback_args_t *p_args)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (p_args->event == RM_COMMS_EVENT_OPERATION_COMPLETE) {
        xEventGroupSetBitsFromISR(g_i2c_event_group, 0x1, &xHigherPriorityTaskWoken);
    } else {
        xEventGroupSetBitsFromISR(g_i2c_event_group, 0x2, &xHigherPriorityTaskWoken);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// タッチ読み取りスレッド内
EventBits_t result = xEventGroupWaitBits(
    g_i2c_event_group,
    0x1 | 0x2,              // COMPLETE | ABORT待機
    pdTRUE,                 // 自動クリア
    pdFALSE,                // いずれか1ビット
    pdMS_TO_TICKS(1000)     // 1秒タイムアウト
);
```

#### 8.2.3 タスク/スレッド構成

| タスク | スタックサイズ | 優先度 | 役割 |
|--------|--------------|--------|------|
| new_thread0 / blinky_thread | 8192 bytes | 2 | メインLVGL/UIスレッド |
| Idle Task | 128 bytes | 0 | カーネルアイドル |
| Timer Task | 可変 | 3 | FreeRTOSタイマー |

```c
// タスク生成（FSP自動生成）
void blinky_thread_create(void)
{
    // 内部的にxTaskCreate()を呼び出し
}

// タスクエントリ
void blinky_thread_entry(void)
{
    // LVGL初期化
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    // メインループ
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));  // 5ms周期
    }
}
```

#### 8.2.4 遅延/タイミング

| 設定 | 値 | 用途 |
|------|-----|------|
| configTICK_RATE_HZ | 1000 | 1msティック |
| LVGL周期 | 5ms | lv_timer_handler()呼び出し間隔 |
| タッチタイムアウト | 1000ms | I2C読み取りタイムアウト |

```c
// 遅延マクロ
vTaskDelay(pdMS_TO_TICKS(5));           // 5ms遅延
vTaskDelay(configTICK_RATE_HZ / 2);     // 500ms遅延

// タイムアウト付きセマフォ待機
xSemaphoreTake(handle, pdMS_TO_TICKS(1000));  // 1秒タイムアウト
```

### 8.3 FreeRTOS API使用一覧

| API | 呼び出し元 | 目的 |
|-----|-----------|------|
| `xSemaphoreCreateBinary()` | main.c | バイナリセマフォ作成 |
| `xSemaphoreCreateCounting()` | main.c | カウンティングセマフォ作成 |
| `xSemaphoreGiveFromISR()` | touch_irq_callback() | ISRからセマフォ解放 |
| `xSemaphoreTake()` | lv_port_indev.c | セマフォ獲得待機 |
| `xEventGroupCreate()` | main.c | イベントグループ作成 |
| `xEventGroupSetBitsFromISR()` | comms_i2c_callback() | ISRからビット設定 |
| `xEventGroupWaitBits()` | i2c_wait() | ビット待機 |
| `xTaskCreate()` | FSP生成コード | タスク作成 |
| `vTaskDelay()` | blinky_thread_entry() | タスク遅延 |
| `portYIELD_FROM_ISR()` | ISRハンドラ | 文脈切り替え要求 |

### 8.4 同期フロー図

#### タッチイベント同期

```
タッチ発生
    ↓
FT5X06 IRQピン (P1-7) → Low
    ↓
External IRQ Ch.19 割込み発火
    ↓
touch_irq_callback() [ISRコンテキスト]
├─ xSemaphoreGiveFromISR(g_irq_binary_semaphore, &woken)
└─ portYIELD_FROM_ISR(woken)
    ↓
LVGLメインスレッド [ブロック状態から復帰]
    ↓
touchpad_read() [lv_port_indev.c]
├─ xSemaphoreTake(g_irq_binary_semaphore, pdMS_TO_TICKS(1000))
├─ R_IIC_MASTER_Read() 非同期開始
└─ i2c_wait() でI2C完了待ち
    ↓
I2C ISR (comms_i2c_callback)
├─ xEventGroupSetBitsFromISR(g_i2c_event_group, 0x1, &woken)
└─ portYIELD_FROM_ISR(woken)
    ↓
touchpad_read() 再開
├─ xEventGroupWaitBits() から復帰
├─ タッチデータ抽出（座標、ポイント数）
└─ LVGLに座標通知
```

#### ディスプレイフラッシュ同期

```
lv_refr_now() [LVGL内部]
    ↓
ダーティエリア検出・描画
    ↓
lv_disp_flush() [コールバック]
└─ フレームバッファ更新 (SDRAM)
    ↓
R_GLCDC_BufferChange() [FSP API]
    ↓
GLCDC HW [DMA転送]
├─ フレームバッファ → LCD
└─ VSYNC完了割込み
    ↓
lvgl_glcdc_callback() [ISRコンテキスト]
├─ LV_EVENT_FLUSH_FINISH発火
└─ 初回フラッシュ時：バックライト有効化
```

### 8.5 FreeRTOSConfig.h 重要設定

```c
// タスク管理
#define configUSE_PREEMPTION                1    // プリエンプション有効
#define configUSE_TIME_SLICING              0    // タイムスライス無効
#define configMAX_PRIORITIES                5    // 優先度レベル数

// ティック設定
#define configTICK_RATE_HZ                  1000 // 1msティック
#define configTICK_TYPE_WIDTH_IN_BITS       1    // 32ビットティック

// 同期機構
#define configUSE_MUTEXES                   0    // ミューテックス無効
#define configUSE_RECURSIVE_MUTEXES         0    // 再帰ミューテックス無効
#define configSUPPORT_STATIC_ALLOCATION     1    // 静的割り当て有効

// メモリ管理
#define configTOTAL_HEAP_SIZE               (配列サイズ)  // ヒープサイズ

// 割込み
#define configPRIO_BITS                     4    // 割込み優先度ビット数
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 5  // システムコール可能な最大優先度
```

---

## 9. μT-Kernel 3.0への移植ポイント

### 9.1 移植の背景

FreeRTOSからμT-Kernel 3.0への移植を行う場合、RTOS APIの差異を吸収する必要があります。本セクションでは、LVGLポーティング層で使用しているFreeRTOS機能のμT-Kernel 3.0への対応方法を解説します。

### 9.2 APIマッピング表

| FreeRTOS | μT-Kernel 3.0 | 備考 |
|----------|---------------|------|
| `xSemaphoreCreateBinary()` | `tk_cre_sem()` (初期値=0) | パラメータ形式異なる |
| `xSemaphoreCreateCounting()` | `tk_cre_sem()` | maxcnt指定 |
| `xSemaphoreGive()` | `tk_sig_sem()` | V操作 |
| `xSemaphoreTake()` | `tk_wai_sem()` | P操作 |
| `xSemaphoreGiveFromISR()` | `tk_sig_sem()` | ISR対応版 |
| `xEventGroupCreate()` | `tk_cre_flg()` | フラグ作成 |
| `xEventGroupSetBits()` | `tk_set_flg()` | ビット設定 |
| `xEventGroupWaitBits()` | `tk_wai_flg()` | ビット待機 |
| `xTaskCreate()` | `tk_cre_tsk()` + `tk_sta_tsk()` | 2段階処理 |
| `vTaskDelay()` | `tk_dly_tsk()` | 遅延 |
| `portENTER_CRITICAL()` | `tk_dis_int()` | 割込み禁止 |
| `portEXIT_CRITICAL()` | `tk_ena_int()` | 割込み許可 |
| `portYIELD_FROM_ISR()` | 自動（不要） | カーネル自動処理 |

### 9.3 セマフォの移植

#### FreeRTOS（現行実装）

```c
// バイナリセマフォ作成
SemaphoreHandle_t g_irq_binary_semaphore;
g_irq_binary_semaphore = xSemaphoreCreateBinary();

// ISRからセマフォ解放
BaseType_t xHigherPriorityTaskWoken = pdFALSE;
xSemaphoreGiveFromISR(g_irq_binary_semaphore, &xHigherPriorityTaskWoken);
portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

// タスクからセマフォ待機
xSemaphoreTake(g_irq_binary_semaphore, pdMS_TO_TICKS(1000));
```

#### μT-Kernel 3.0（移植後）

```c
// セマフォ作成
ID g_irq_semid;
T_CSEM csem = {
    .sematr = TA_TPRI,     // タスク優先度順
    .isemcnt = 0,          // 初期カウント（バイナリなら0）
    .maxsem = 1            // 最大カウント（バイナリなら1）
};
g_irq_semid = tk_cre_sem(&csem);

// ISRからセマフォ解放（文脈切り替えは自動）
tk_sig_sem(g_irq_semid, 1);

// タスクからセマフォ待機
ER result = tk_wai_sem(g_irq_semid, 1, TMO_POL);  // ポーリング
// または
ER result = tk_wai_sem(g_irq_semid, 1, 1000);     // 1秒タイムアウト
```

### 9.4 イベントフラグの移植

#### FreeRTOS（現行実装）

```c
// イベントグループ作成
EventGroupHandle_t g_i2c_event_group;
g_i2c_event_group = xEventGroupCreate();

// ISRからビット設定
xEventGroupSetBitsFromISR(g_i2c_event_group, 0x1, &xHigherPriorityTaskWoken);
portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

// タスクからビット待機
EventBits_t result = xEventGroupWaitBits(
    g_i2c_event_group,
    0x1 | 0x2,
    pdTRUE,
    pdFALSE,
    pdMS_TO_TICKS(1000)
);
```

#### μT-Kernel 3.0（移植後）

```c
// フラグ作成
ID g_i2c_flgid;
T_CFLG cflg = {
    .flgatr = TA_TPRI | TA_WMUL,  // タスク優先度順、複数待機可
    .iflgptn = 0x00               // 初期フラグパターン
};
g_i2c_flgid = tk_cre_flg(&cflg);

// ISRからビット設定（自動文脈切り替え）
tk_set_flg(g_i2c_flgid, 0x1);

// タスクからビット待機
UINT flgptn;
ER result = tk_wai_flg(
    g_i2c_flgid,
    0x1 | 0x2,                    // 待機パターン
    TWF_ORW | TWF_CLR,            // OR待機、クリア
    &flgptn,                      // 結果パターン
    1000                          // 1秒タイムアウト（ms）
);
```

### 9.5 タスク管理の移植

#### FreeRTOS（現行実装）

```c
// タスク作成（1ステップ）
TaskHandle_t xHandle;
xTaskCreate(
    task_func,        // タスク関数
    "TaskName",       // タスク名
    8192 / 4,         // スタックサイズ（ワード単位）
    NULL,             // パラメータ
    2,                // 優先度
    &xHandle          // ハンドル
);
```

#### μT-Kernel 3.0（移植後）

```c
// タスク作成（2ステップ）
ID tskid;
T_CTSK ctsk = {
    .tskatr = TA_HLNG | TA_RNG0,  // 高級言語、リング0
    .task = (FP)task_func,        // タスク開始アドレス
    .itskpri = 2,                 // 初期優先度
    .stksz = 8192,                // スタックサイズ（バイト）
    .sstksz = 0,                  // システムスタック（0=自動）
    .stkptr = NULL,               // スタックポインタ（NULL=自動）
    .uatb = NULL,                 // ユーザ属性（NULL=なし）
    .lsid = 0,                    // 論理空間ID
    .resid = 0                    // リソースID
};

// タスク生成
tskid = tk_cre_tsk(&ctsk);

// タスク起動
tk_sta_tsk(tskid, 0);  // stacdは起動コード
```

### 9.6 遅延の移植

#### FreeRTOS（現行実装）

```c
// ミリ秒遅延
vTaskDelay(pdMS_TO_TICKS(5));  // 5ms
```

#### μT-Kernel 3.0（移植後）

```c
// ミリ秒遅延
tk_dly_tsk(5);  // 5ms（μT-Kernelは標準でms単位）
```

### 9.7 割込みハンドラの移植

#### FreeRTOS（現行実装）

```c
void touch_irq_callback(external_irq_callback_args_t *p_args)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(g_irq_binary_semaphore, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);  // 手動で文脈切り替え要求
}
```

#### μT-Kernel 3.0（移植後）

```c
void touch_irq_callback(external_irq_callback_args_t *p_args)
{
    tk_sig_sem(g_irq_semid, 1);  // 文脈切り替えは自動
    // portYIELD_FROM_ISR()相当は不要（カーネルが自動処理）
}
```

### 9.8 RTOS抽象化層の設計

移植を容易にするため、RTOS抽象化層（OSAL: Operating System Abstraction Layer）を設計することを推奨します。

```c
// osal.h - RTOS抽象化ヘッダ

#ifdef USE_FREERTOS
    #include "FreeRTOS.h"
    #include "semphr.h"
    #include "event_groups.h"
    typedef SemaphoreHandle_t osal_sem_t;
    typedef EventGroupHandle_t osal_event_t;
#else  // USE_UTKERNEL
    #include <tk/tkernel.h>
    typedef ID osal_sem_t;
    typedef ID osal_event_t;
#endif

// セマフォAPI
osal_sem_t osal_sem_create(int initial_count, int max_count);
int osal_sem_give(osal_sem_t sem);
int osal_sem_take(osal_sem_t sem, uint32_t timeout_ms);
int osal_sem_give_from_isr(osal_sem_t sem);

// イベントAPI
osal_event_t osal_event_create(void);
int osal_event_set(osal_event_t event, uint32_t bits);
int osal_event_wait(osal_event_t event, uint32_t bits, uint32_t timeout_ms, uint32_t *result);

// 遅延API
void osal_delay_ms(uint32_t ms);
```

```c
// osal_utkernel.c - μT-Kernel実装

#include "osal.h"

osal_sem_t osal_sem_create(int initial_count, int max_count)
{
    T_CSEM csem = {
        .sematr = TA_TPRI,
        .isemcnt = initial_count,
        .maxsem = max_count
    };
    return tk_cre_sem(&csem);
}

int osal_sem_give(osal_sem_t sem)
{
    return (tk_sig_sem(sem, 1) >= 0) ? 0 : -1;
}

int osal_sem_take(osal_sem_t sem, uint32_t timeout_ms)
{
    TMO tmo = (timeout_ms == OSAL_WAIT_FOREVER) ? TMO_FEVR : timeout_ms;
    return (tk_wai_sem(sem, 1, tmo) >= 0) ? 0 : -1;
}

int osal_sem_give_from_isr(osal_sem_t sem)
{
    return (tk_sig_sem(sem, 1) >= 0) ? 0 : -1;
}

void osal_delay_ms(uint32_t ms)
{
    tk_dly_tsk(ms);
}
```

### 9.9 移植時の注意事項

#### 9.9.1 タイムアウト値の単位

| RTOS | 単位 | 無限待ち |
|------|------|---------|
| FreeRTOS | ティック | portMAX_DELAY |
| μT-Kernel 3.0 | ミリ秒 | TMO_FEVR |

#### 9.9.2 優先度の方向

| RTOS | 高優先度 | 低優先度 |
|------|---------|---------|
| FreeRTOS | 大きい数値 | 小さい数値 |
| μT-Kernel 3.0 | 小さい数値 | 大きい数値 |

**注意**: 優先度の変換が必要です。

```c
// 優先度変換（FreeRTOS → μT-Kernel）
#define CONVERT_PRIORITY(freertos_pri) (configMAX_PRIORITIES - 1 - freertos_pri)
```

#### 9.9.3 ISRからの操作

| 操作 | FreeRTOS | μT-Kernel 3.0 |
|------|----------|---------------|
| セマフォ解放 | `xSemaphoreGiveFromISR()` + `portYIELD_FROM_ISR()` | `tk_sig_sem()` のみ |
| イベント設定 | `xEventGroupSetBitsFromISR()` + `portYIELD_FROM_ISR()` | `tk_set_flg()` のみ |

μT-Kernelでは`portYIELD_FROM_ISR()`相当が不要で、カーネルが自動で文脈切り替えを処理します。

#### 9.9.4 スタックサイズの単位

| RTOS | 単位 |
|------|------|
| FreeRTOS | ワード（4バイト） |
| μT-Kernel 3.0 | バイト |

### 9.10 移植スケジュール案

| フェーズ | 内容 | 期間 |
|---------|------|------|
| Phase 1 | OSAL設計・実装 | 1週間 |
| Phase 2 | セマフォ/イベント移植 | 1週間 |
| Phase 3 | タスク管理移植 | 1週間 |
| Phase 4 | 割込みハンドラ移植 | 1週間 |
| Phase 5 | 統合テスト | 2週間 |

### 9.11 移植チェックリスト

| 項目 | 確認内容 |
|------|---------|
| セマフォ | バイナリ/カウンティング両方動作確認 |
| イベントフラグ | ビット設定/待機動作確認 |
| タスク | 生成/起動/遅延動作確認 |
| 割込み | ISRからのセマフォ/イベント操作確認 |
| タイムアウト | 各APIのタイムアウト動作確認 |
| 優先度 | タスク優先度の変換確認 |
| メモリ | ヒープ/スタック使用量確認 |
| パフォーマンス | FPS、応答時間測定 |

---

## 10. 総括

### 10.1 LVGL画面表示仕組みのポイント

1. **描画はダブルバッファリング**: ティアリング防止、滑らかな表示
2. **ダーティエリア追跡**: 変更部分のみ再描画で効率化
3. **D/AVE 2Dアクセラレータ**: CPU負荷軽減
4. **イベント駆動**: タッチ割込み → セマフォ → 座標読み取り → UI更新

### 10.2 ハードウェア設定のポイント

1. **GLCDC**: RGB565, 1024x600, ダブルバッファ, DMA自動転送
2. **タッチ**: FT5X06, I2C 0x38, External IRQ Ch.19
3. **GPIO**: LCDデータライン多数 + バックライト + タッチ割込み
4. **クロック**: M85 @ 1GHz, M33 @ 250MHz, SDRAM初期化必須

### 10.3 移植の核心

**Phase 1-2（基盤）が最重要**。GLCDCとタッチパネルが動作確認できれば、Phase 3-5は技術的には容易です。

**推奨優先順位**:
1. GLCDCテストパターン表示
2. タッチパネル座標読み取り
3. 簡易LVGL画面
4. リファレンス実装統合

---

## 付録A: ファイル参照一覧

### 主要ソースファイル

| ファイル | 役割 |
|---------|------|
| `src/hal_entry.c` | HALエントリポイント |
| `src/new_thread0_entry.c` | メインFreeRTOSスレッド（LVGL初期化） |
| `src/lv_port_disp.c` | ディスプレイポート（GLCDC統合） |
| `src/lv_port_indev.c` | 入力デバイスポート（タッチ制御） |
| `src/lv_conf_user.h` | LVGLユーザー設定 |

### FSP生成ファイル

| ファイル | 役割 |
|---------|------|
| `ra_gen/main.c` | メインエントリ |
| `ra_gen/pin_data.c` | GPIOピン設定 |
| `ra_gen/bsp_clock_cfg.h` | クロック設定 |

### プロジェクト構造

```
e2studio_CPU0/
├── src/                    # アプリケーションコード
│   ├── blinky_thread_entry.c
│   ├── hal_warmstart.c
│   ├── lv_port_disp.c      # 【追加】
│   ├── lv_port_indev.c     # 【追加】
│   └── lv_conf_user.h      # 【追加】
├── ra_gen/                 # FSP自動生成
│   ├── main.c
│   ├── pin_data.c
│   └── bsp_clock_cfg.h
├── ra/                     # FSPライブラリ
│   └── lvgl/               # 【追加】
├── ui/                     # 【追加】UIリソース
├── configuration.xml       # FSP設定ファイル
└── .cproject               # Eclipseプロジェクト設定
```

---

## 付録B: 用語集

| 用語 | 説明 |
|------|------|
| LVGL | Light and Versatile Graphics Library |
| GLCDC | Graphics LCD Controller |
| FSP | Flexible Software Platform（Renesas） |
| D/AVE 2D | Renesas製グラフィックスアクセラレータ |
| FT5X06 | 静電容量式タッチコントローラ |
| RGB565 | 16ビットカラーフォーマット（R5G6B5） |
| ダブルバッファ | 2面のフレームバッファを交互に使用する描画方式 |
| ダーティエリア | 変更があり再描画が必要な領域 |
| FreeRTOS | リアルタイムオペレーティングシステム |
| μT-Kernel 3.0 | TRONプロジェクトの組込みリアルタイムOS |
| セマフォ | タスク間同期のためのカウンタ機構 |
| イベントフラグ | ビットパターンによるタスク間通信機構 |
| ISR | Interrupt Service Routine（割込みサービスルーチン） |
| OSAL | Operating System Abstraction Layer（OS抽象化層） |
| 文脈切り替え | 実行タスクを切り替える処理 |
| ティック | RTOSの時間管理単位 |

---

*このレポートはClaude Codeにより自動生成されました*
