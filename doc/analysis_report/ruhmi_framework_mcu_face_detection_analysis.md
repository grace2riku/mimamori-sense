# RUHMI Framework MCU 顔認識サンプルプログラム解析レポート

本レポートのソースコードバージョン: [ruhmi-framework-mcu](https://github.com/renesas/ruhmi-framework-mcu) (reference_projects/ruhmi-framework-mcu)

---

## 1. 顔認識サンプルプログラムの解析

### 1.1 ファイルツリー

```
application_examples/face_detection/
├── configuration.xml                          # FSP設定ファイル
├── .cproject / .project                       # e2 studio プロジェクト設定
├── script/
│   └── fsp.lld                                # リンカスクリプト
├── ra/                                        # FSP・ライブラリ（自動生成・編集禁止）
│   ├── arm/
│   │   ├── CMSIS-DSP/                         # ARM CMSIS-DSP ライブラリ
│   │   ├── CMSIS-NN/                          # ARM CMSIS-NN ライブラリ
│   │   ├── CMSIS-View/                        # CMSIS-View
│   │   └── CMSIS_6/                           # CMSIS 6
│   ├── board/ra8p1_ek/                        # ボードサポート（LED、初期化）
│   ├── fsp/
│   │   ├── inc/api/                           # FSP APIヘッダ
│   │   ├── inc/instances/                     # ドライバインスタンスヘッダ
│   │   └── src/                               # ドライバ実装
│   │       ├── bsp/                           # BSP（ボードサポートパッケージ）
│   │       ├── r_dmac/                        # DMAコントローラ
│   │       ├── r_glcdc/                       # GLCDCドライバ
│   │       ├── r_gpt/                         # GPTタイマー
│   │       ├── r_icu/                         # 割り込みコントローラ
│   │       ├── r_iic_master/                  # I2Cマスタ
│   │       ├── r_ioport/                      # I/Oポート
│   │       ├── r_ospi_b/                      # OSPI（外部フラッシュ）
│   │       ├── r_sci_b_uart/                  # SCI UART
│   │       ├── r_drw/                         # D/AVE 2D描画エンジン
│   │       └── rm_freertos_port/              # FreeRTOSポート
│   ├── npu/                                   # NPUライブラリ
│   │   ├── ethos-u-core-driver/               # Ethos-U NPUコアドライバ
│   │   ├── ethos-u-core-software/             # Ethos-Uコアソフトウェア
│   │   ├── flatbuffers/                       # FlatBuffersシリアライザ
│   │   ├── gemmlowp/                          # 低精度行列乗算
│   │   ├── ruy/                               # 行列乗算ライブラリ
│   │   └── tflite-micro/                      # TensorFlow Lite Micro
│   ├── aws/FreeRTOS/                          # FreeRTOS カーネル
│   └── tes/dave2d/                            # D/AVE 2D描画ライブラリ
├── ra_cfg/                                    # FSP設定ファイル（自動生成）
├── ra_gen/                                    # FSP生成コード（自動生成・編集禁止）
├── src/                                       # ユーザーソースコード
│   ├── hal_entry.c                            # HALエントリ（WarmStart、IOPORT/SDRAM/OSPI初期化）
│   ├── application_config.h                   # アプリケーション設定（メモリ割当、機能有効化）
│   ├── common_util.c / .h                     # 共通ユーティリティ（LED制御、イベントフラグ定義、エラーハンドリング）
│   ├── camera_display_thread_entry.c          # カメラ表示スレッド（メインループ）
│   ├── ai_inference_thread_entry.c            # AI推論スレッド
│   ├── camera_layer/                          # カメラ制御レイヤー
│   │   ├── camera_layer.c / .h                # OV5640カメラ初期化・キャプチャ制御
│   │   ├── camera_layer_config.h              # カメラ設定（解像度、バッファ数）
│   │   ├── camera_utils.c / .h                # 画像変換ユーティリティ
│   │   ├── arducam.h                          # Arducamドライバヘッダ
│   │   └── arducam_port.c                     # Arducam I2Cポート実装
│   ├── display_layer/                         # ディスプレイ制御レイヤー
│   │   ├── display_layer.c / .h               # GLCDC・D/AVE 2D初期化・ダブルバッファ制御
│   │   ├── display_layer_config.h             # ディスプレイ設定（1024x600、RGB565）
│   │   ├── face_detection_screen_mipi.c       # 顔認識画面描画（カメラ画像表示＋バウンディングボックス）
│   │   └── bg_font_18_full.c / .h             # フォントデータ
│   ├── ai_application/                        # AI推論アプリケーション
│   │   ├── ai_application_config.h            # AI設定（入力192x192、1ch、最大検出20個）
│   │   ├── common/                            # AI共通モジュール
│   │   │   ├── ImageUtils.cc / .hpp           # 画像変換ユーティリティ
│   │   │   ├── PlatformMath.cc / .hpp         # 数学関数（Sigmoid等）
│   │   │   ├── Main.cc                        # face_detection()エントリ
│   │   │   └── log_macros.h                   # ログマクロ
│   │   ├── ethosu_dcache.c                    # Ethos-Uキャッシュ管理
│   │   └── face_detection/                    # 顔認識固有モジュール
│   │       ├── MainLoop_obj.cc                # 推論メインループ（MERA呼び出し・後処理）
│   │       ├── DetectorPostProcessing.cc / .hpp  # YOLO後処理（NMS、バウンディングボックス計算）
│   │       ├── DetectionResult.hpp            # 検出結果データ構造
│   │       ├── wrapper.h                      # MERAラッパー（入出力ポインタ取得）
│   │       └── mera/                          # MERA自動生成コード
│   │           ├── model.c / .h               # モデル実行（RunModel）
│   │           ├── model_io_data.c / .h       # モデル入出力データ定義
│   │           ├── sub_0000_command_stream.c / .h  # NPUコマンドストリーム
│   │           ├── sub_0000_invoke.c / .h     # NPU推論実行
│   │           ├── sub_0000_io_data.c / .h    # NPU入出力データ
│   │           ├── sub_0000_model_data.c / .h # NPUモデルデータ（重みパラメータ）
│   │           ├── sub_0000_tensors.c / .h    # テンソルアドレス定義
│   │           ├── ethosu_common.h            # Ethos-U共通定義
│   │           └── wrapper.h                  # MERAモデルラッパー
│   ├── fsp_custom/                            # FSPカスタムドライバ
│   │   ├── mipicsi_vin_hal_driver.c / .h      # MIPI-CSI VINドライバ（カメラキャプチャ）
│   │   ├── mipicsi_vin_hal_driver_config.h    # MIPI-CSI設定
│   │   ├── r_mipi_phy/r_mipi_phy.c            # MIPI PHYドライバ
│   │   ├── r_capture_api.h                    # キャプチャAPI
│   │   └── R7KA8P1KF_core0_additional.h       # RA8P1 Core0追加定義
│   ├── external_memory/                       # 外部メモリ制御
│   │   ├── ospi_b_ep.c / .h                   # OSPI（外部フラッシュ）制御
│   │   └── ospi_b_commands.c / .h             # OSPIコマンド
│   ├── console_output/                        # コンソール出力
│   │   ├── console_output.c / .h              # UART/RTTコンソール出力
│   │   ├── console_output_config.h            # コンソール設定
│   │   └── SEGGER_RTT/                        # SEGGER RTTライブラリ
│   └── time_counter/                          # 時間計測
│       ├── time_counter.c / .h                # 処理時間計測ユーティリティ
└── Debug/                                     # ビルド成果物
```

### 1.2 マルチコア構成

**シングルコアプロジェクトである。** Cortex-M85（CPU0）のみを使用している。

根拠:
- プロジェクトディレクトリは`face_detection/`の1つのみ（`e2studio_CPU0`/`e2studio_CPU1`のような分離がない）
- ヘッダ`R7KA8P1KF_core0_additional.h`でCore0向けの追加定義のみ存在
- `_RA_CORE`マクロによるコア分岐処理が存在しない
- FreeRTOSスレッドは`camera_display_thread`と`ai_inference_thread`の2つだが、いずれも同一コア上で動作

### 1.3 使用しているハードウェアリソースとペリフェラル

| ペリフェラル | FSPモジュール | 用途 |
|---|---|---|
| **GLCDC** | `r_glcdc` | LCD（1024x600、RGB565）への画面表示制御。ダブルバッファでVSYNC同期表示 |
| **D/AVE 2D (DRW)** | `r_drw` / `dave2d` | 2Dハードウェア描画エンジン。カメラ画像のスケーリング描画、バウンディングボックス描画、テキスト描画に使用 |
| **MIPI-CSI + VIN** | `fsp_custom/mipicsi_vin_hal_driver` | MIPI-CSIインターフェースによるカメラ画像キャプチャ。VIN（Video Input）モジュールでフレームデータ取得 |
| **MIPI PHY** | `fsp_custom/r_mipi_phy` | MIPI物理層制御 |
| **I2C (IIC Master)** | `r_iic_master` | OV5640カメラモジュールのレジスタ設定（SCCB通信）。スレーブアドレス: 0x3C |
| **GPT (タイマー)** | `r_gpt` | カメラ用クロック(XCLK)生成、処理時間計測用カウンタ |
| **IOPORT (GPIO)** | `r_ioport` | カメラリセット(CAM_RST)、LCDリセット(LCD_RST)、LCDバックライト(LCD_BLEN)、MIPI IF有効化、LED制御 |
| **ICU (外部割り込み)** | `r_icu` | 外部割り込み制御（ボタン入力等） |
| **DMAC** | `r_dmac` | DMA転送（MIPI-CSIデータ転送で使用） |
| **SCI UART** | `r_sci_b_uart` | コンソール出力（デバッグログ） |
| **OSPI** | `r_ospi_b` | 外部フラッシュメモリアクセス（AIモデルデータ格納用、設定による） |
| **SDRAM** | BSP設定 | 大容量データ格納（カメラキャプチャバッファ等） |
| **Ethos-U55 NPU** | `rm_ethosu` | AI推論のハードウェアアクセラレーション |

### 1.4 カメラ撮影画像から顔認識するまでの流れ

#### 1.4.1 静的な視点（ソフトウェアモジュール構造）

```
┌─────────────────────────────────────────────────────────────────────┐
│                        アプリケーション層                             │
│  camera_display_thread_entry.c    ai_inference_thread_entry.c       │
│  (カメラ表示スレッド)               (AI推論スレッド)                    │
│  application_config.h             ai_application_config.h           │
│  common_util.c/.h                                                   │
├─────────────────────────────────────────────────────────────────────┤
│                        機能レイヤー                                  │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────────────────────┐ │
│  │ camera_layer │ │ display_layer│ │ ai_application               │ │
│  │  camera_layer│ │  display_    │ │  common/ (ImageUtils,        │ │
│  │  camera_utils│ │   layer      │ │          PlatformMath)        │ │
│  │  arducam_port│ │  face_detect │ │  face_detection/             │ │
│  │              │ │   ion_screen │ │   MainLoop_obj               │ │
│  │              │ │  bg_font_18  │ │   DetectorPostProcessing     │ │
│  │              │ │              │ │   mera/ (自動生成)            │ │
│  └──────┬───────┘ └──────┬───────┘ └──────────────┬───────────────┘ │
├─────────┼────────────────┼────────────────────────┼─────────────────┤
│         │     ハードウェア抽象化層 (HAL / FSP)      │                 │
│  ┌──────┴───────┐ ┌──────┴───────┐ ┌──────────────┴───────────────┐ │
│  │ mipicsi_vin  │ │ r_glcdc     │ │ rm_ethosu (Ethos-U55 NPU)   │ │
│  │ r_iic_master │ │ dave2d (DRW)│ │ ethos-u-core-driver          │ │
│  │ r_gpt        │ │ r_ioport    │ │ tflite-micro                 │ │
│  │ r_mipi_phy   │ │             │ │ CMSIS-NN / CMSIS-DSP         │ │
│  └──────────────┘ └─────────────┘ └──────────────────────────────┘ │
├─────────────────────────────────────────────────────────────────────┤
│                     RTOS層 (FreeRTOS)                               │
│  タスク管理、イベントグループ、遅延                                     │
├─────────────────────────────────────────────────────────────────────┤
│                     ハードウェア                                     │
│  RA8P1 Cortex-M85 + Ethos-U55 NPU + OV5640カメラ + MIPI LCD       │
└─────────────────────────────────────────────────────────────────────┘
```

#### 1.4.2 動的な視点（カメラ撮影から顔認識までのシーケンス）

```
camera_display_thread          ISR(VIN callback)         ai_inference_thread
        │                            │                          │
        │  ① 初期化                   │                          │
        │  console_output_init()     │                          │
        │  external_irq_configure()  │                          │
        │  display_image_buffer_     │                          │
        │    initialize()            │                          │
        │  drw_init()                │                          │
        │  display_init()            │                          │
        │  ─SetBits(DISPLAY_INIT)──> │                          │
        │  camera_init(false)        │                          │ ② 待機
        │  ─SetBits(CAMERA_INIT)──>  │                          │ WaitBits(DISPLAY,CAMERA)
        │  TimeCounter_Init()        │                          │
        │                            │                          │ ③ Ethos-U初期化
        │  ④ 全初期化完了待ち          │                          │ RM_ETHOSU_Open()
        │  WaitBits(DISPLAY,CAMERA,  │                          │ ─SetBits(ETHOSU_INIT)─>
        │    ETHOSU,AI_INIT)         │                          │ ─SetBits(AI_INIT)────>
        │                            │                          │
        │  ⑤ カメラキャプチャ開始       │                          │
        │  camera_capture_start()    │                          │
        │         :                  │                          │
        │  ⑥ ループ開始               │                          │
        │  WaitBits(CAPTURE_DONE) <──┤  VIN_EVENT_VFS割り込み     │
        │                            │  ─SetBits(CAPTURE_DONE)─>│
        │  ⑦ カメラ後処理              │  camera_capture_start()  │
        │  camera_capture_post_      │  (次フレーム開始)           │
        │    process()               │                          │
        │  (バッファ切替+memcpy)       │                          │
        │                            │                          │
        │  ⑧ AI入力画像作成            │                          │
        │  image_rgb565_to_int8()    │                          │
        │  (320x240 RGB565 →         │                          │
        │   192x192 Grayscale INT8)  │                          │
        │  SCB_CleanDCache()         │                          │
        │                            │                          │
        │  ⑨ AI推論開始通知            │                          │
        │  ─SetBits(IMAGE_READY)───────────────────────────────>│
        │  vTaskDelay(1)             │                          │ ⑩ AI推論実行
        │                            │                          │ memcpy→mera_input_ptr()
        │                            │                          │ mera_invoke() ← NPU実行
        │                            │                          │ (Ethos-U55で推論)
        │                            │                          │
        │                            │                          │ ⑪ 後処理
        │                            │                          │ DetectorPostProcess
        │                            │                          │  ::DoPostProcess()
        │                            │                          │ (YOLO NMS処理)
        │                            │                          │
        │  ⑫ 結果確認                  │                          │ ⑬ 結果通知
        │  xEventGroupGetBits()  <────────────────────────────── │ ─SetBits(RESULT_UPDATED)
        │  (AI_INFERENCE_RESULT_     │                          │ vTaskDelay(25)
        │    UPDATED確認)             │                          │
        │                            │                          │
        │  ⑭ LCD表示                  │                          │
        │  do_face_reconition_       │                          │
        │    screen()                │                          │
        │  - WaitBits(GLCDC_VSYNC)   │                          │
        │  - graphics_start_frame()  │                          │
        │  - display_camera_image()  │                          │
        │    (D/AVE 2Dでスケーリング   │                          │
        │     320x240→704x528表示)    │                          │
        │  - calculate_and_draw_     │                          │
        │    bounding_box()          │                          │
        │    (顔検出枠描画)            │                          │
        │  - print_inf_time_and_     │                          │
        │    number_faces()          │                          │
        │  - graphics_end_frame()    │                          │
        │  vTaskDelay(20)            │                          │
        │         :                  │                          │
        │  (⑥に戻る)                 │                          │
```

**主要なデータフロー:**

1. OV5640カメラ → MIPI-CSI → VIN → `camera_capture_buffer[]` (320x240 RGB565, SDRAM)
2. `camera_capture_buffer[]` → memcpy → `camera_capture_image_rgb565[]` (320x240 RGB565, OnChip RAM)
3. `camera_capture_image_rgb565[]` → `image_rgb565_to_int8()` → `model_buffer_int8[]` (192x192 Grayscale INT8, OnChip RAM)
4. `model_buffer_int8[]` → memcpy → MERA入力バッファ (NPU arena内)
5. NPU推論実行 → 出力テンソル2つ (Identity_70275: 648bytes, Identity_1_70284: 2592bytes)
6. 出力テンソル → `DetectorPostProcess::DoPostProcess()` → `g_ai_detection[]` (バウンディングボックス座標)
7. `camera_capture_image_rgb565[]` + `g_ai_detection[]` → D/AVE 2D → フレームバッファ → GLCDC → LCD

### 1.5 LCD表示の実装

**LVGLは使用していない。** D/AVE 2D (DRW) ハードウェア描画エンジンを直接使用している。

#### 表示パイプライン

| ステップ | 処理 | モジュール |
|---|---|---|
| 1 | GLCDC初期化（`R_GLCDC_Open`/`R_GLCDC_Start`） | `display_layer.c` |
| 2 | D/AVE 2D初期化（`d2_opendevice`/`d2_inithw`） | `display_layer.c` |
| 3 | フレーム開始（`d2_startframe`） | `display_layer.c` |
| 4 | カメラ画像描画（`d2_setblitsrc`→`d2_blitcopy`で2.2倍スケーリング） | `face_detection_screen_mipi.c` |
| 5 | バウンディングボックス描画（`d2_renderline`で矩形描画、赤色） | `face_detection_screen_mipi.c` |
| 6 | テキスト描画（推論時間、検出数、モデル名） | `face_detection_screen_mipi.c` |
| 7 | フレーム終了（`d2_endframe`）＋バッファスワップ（`R_GLCDC_BufferChange`） | `display_layer.c` |

ディスプレイ仕様:
- **解像度**: 1024x600
- **色深度**: RGB565 (16bit)
- **バッファリング**: ダブルバッファ（`fb_background[2]`）
- **同期**: GLCDC VSync割り込みによるティアリング防止

---

## 2. AIモデルの解析

### 2.1 使用しているAIモデル

**YOLO-Fastest** (顔検出特化モデル)

- **タスク**: 物体検出（顔検出、1クラス）
- **入力**: 192x192ピクセル、グレースケール（1チャンネル、INT8量子化）
- **入力サイズ**: 36,864バイト (192 x 192 x 1)
- **出力**: 2つのテンソル
  - `Identity_70275`: 648バイト（6x6グリッド、3アンカー、6値[x,y,w,h,obj,cls]）
  - `Identity_1_70284`: 2,592バイト（12x12グリッド、3アンカー、6値[x,y,w,h,obj,cls]）
- **量子化パラメータ**:
  - 出力0: scale=0.13408391, zero_point=47
  - 出力1: scale=0.18535925, zero_point=10
- **アンカーボックス**:
  - Branch0 (大きな顔用): [38, 77, 47, 97, 61, 126]
  - Branch1 (小さな顔用): [14, 26, 19, 37, 28, 55]
- **NPUアリーナサイズ**: 442,368バイト (432KB)
- **後処理**: NMS (Non-Maximum Suppression)、閾値=0.5、NMS閾値=0.45
- **検証済みモデル**: docs/models_tested.mdに「YOLO-fastest face detection (TFLite INT8)」として記載

### 2.2 該当ファイル

| ファイル | 役割 |
|---|---|
| `src/ai_application/face_detection/MainLoop_obj.cc` | 推論メインループ。MERA呼び出し、後処理実行 |
| `src/ai_application/face_detection/DetectorPostProcessing.cc/.hpp` | YOLO後処理（NMS、バウンディングボックス座標計算） |
| `src/ai_application/face_detection/DetectionResult.hpp` | 検出結果のデータ構造定義 |
| `src/ai_application/face_detection/wrapper.h` | MERAモデルへのラッパー（入出力ポインタ取得） |
| `src/ai_application/face_detection/mera/` ディレクトリ全体 | **MERA自動生成コード**（モデルデータ、コマンドストリーム含む） |
| `src/ai_application/common/PlatformMath.cc/.hpp` | 数学関数（SigmoidF32等） |
| `src/ai_application/common/ImageUtils.cc/.hpp` | 画像変換ユーティリティ |

### 2.3 ツールで自動生成されているファイル

`src/ai_application/face_detection/mera/`ディレクトリ内の全ファイルがRUHMI（EdgeCortix MERA）ツールで自動生成されている。

| ファイル | 内容 |
|---|---|
| `model.c / .h` | `RunModel()`関数。NPUサブグラフの呼び出し、入出力ポインタ取得関数 |
| `model_io_data.c / .h` | モデル入出力バッファの定義。入力サイズ36,864、出力サイズ648+2,592 |
| `sub_0000_command_stream.c / .h` | NPU (Ethos-U55) 向けコマンドストリーム（推論命令列） |
| `sub_0000_invoke.c / .h` | `sub_0000_invoke()`関数。NPU推論実行の実体 |
| `sub_0000_io_data.c / .h` | NPUサブグラフの入出力データバッファ |
| `sub_0000_model_data.c / .h` | モデルの重みパラメータデータ |
| `sub_0000_tensors.c / .h` | テンソルのアドレス定義。アリーナサイズ442,368バイト |
| `ethosu_common.h` | Ethos-U共通型定義（TensorInfo等） |
| `wrapper.h` | MERA自動生成のラッパー（`model.h`を使用） |

### 2.4 プログラムとのインターフェース

```
アプリケーション
     │
     ▼
┌──────────────────────────────────┐
│ wrapper.h (手動作成)              │
│  mera_input_ptr()  → 入力バッファ  │  ← model_buffer_int8[] からmemcpyで入力データをコピー
│  mera_invoke()     → 推論実行     │  ← RunModel(false) を呼び出し
│  mera_output1_ptr()→ 出力1取得    │  ← DetectorPostProcess で後処理
│  mera_output2_ptr()→ 出力2取得    │
└───────────┬──────────────────────┘
            ▼
┌──────────────────────────────────┐
│ model.c (MERA自動生成)            │
│  GetModelInputPtr_image_input()  │  ← アリーナ内のオフセットでアドレス計算
│  GetModelOutputPtr_Identity_*()  │
│  RunModel()                      │  ← sub_0000_invoke() を呼び出し
└───────────┬──────────────────────┘
            ▼
┌──────────────────────────────────┐
│ sub_0000_invoke.c (MERA自動生成)  │
│  sub_0000_invoke()               │  ← Ethos-U55 NPUドライバに推論命令を発行
└───────────┬──────────────────────┘
            ▼
┌──────────────────────────────────┐
│ Ethos-U55 NPU ハードウェア        │
│  コマンドストリーム実行             │
│  モデルデータ（重み）参照           │
└──────────────────────────────────┘
```

呼び出しの流れ（`MainLoop_obj.cc`）:
1. `memcpy(mera_input_ptr(), model_buffer_int8, model_image_input_SIZE)` — 入力データをMERAアリーナにコピー
2. `mera_invoke()` — NPU推論実行（内部で`sub_0000_invoke()`→Ethos-U55ドライバ呼び出し）
3. `mera_output1_ptr()` / `mera_output2_ptr()` — 出力テンソルのポインタ取得
4. `DetectorPostProcess::DoPostProcess()` — YOLO後処理（NMS適用、バウンディングボックス座標計算）
5. `update_detection_result()` — 結果を`g_ai_detection[]`配列に格納

---

## 3. 画像分類サンプルプログラムの解析

### 3.1 ファイルツリー

```
application_examples/image_classification/
├── configuration.xml
├── ra/ (face_detectionと同一構造)
├── ra_cfg/
├── ra_gen/
├── src/
│   ├── hal_entry.c                             # face_detectionと同一
│   ├── application_config.h                    # AI_DEMO = IMAGE_CLASSIFICATION
│   ├── common_util.c / .h                      # face_detectionと共通
│   ├── camera_display_thread_entry.c           # メインループ（分類結果表示）
│   ├── ai_inference_thread_entry.c             # AI推論スレッド（分類用）
│   ├── camera_layer/                           # face_detectionと同一
│   ├── display_layer/
│   │   ├── display_layer.c / .h                # face_detectionと同一
│   │   ├── display_layer_config.h              # face_detectionと同一
│   │   ├── image_classification_screen_mipi.c  # ★ 分類結果画面（face_detectionとは異なる）
│   │   └── bg_font_18_full.c / .h
│   ├── ai_application/
│   │   ├── ai_application_config.h             # 入力224x224x3 RGB、最大5クラス
│   │   ├── common/
│   │   │   ├── Classifier.cc / .hpp            # ★ 分類器（Top-K結果取得）
│   │   │   ├── ClassificationResult.hpp        # ★ 分類結果データ構造
│   │   │   ├── TensorFlowLiteMicro.cc / .hpp   # ★ TFLite Micro共通処理
│   │   │   ├── ImageUtils.hpp
│   │   │   ├── PlatformMath.cc / .hpp
│   │   │   ├── Main.cc
│   │   │   └── log_macros.h
│   │   ├── ethosu_dcache.c
│   │   └── image_classification/               # ★ 分類固有モジュール
│   │       ├── MainLoop_img.cc                 # 推論メインループ
│   │       ├── ImgClassProcessing.cc / .hpp    # 分類後処理
│   │       ├── Labels.c / .h                   # MobileNet v2ラベル（1000クラス）
│   │       ├── wrapper.h                       # MERAラッパー
│   │       └── mera/                           # MERA自動生成コード
│   ├── fsp_custom/                             # face_detectionと同一
│   ├── external_memory/                        # face_detectionと同一
│   ├── console_output/                         # face_detectionと同一
│   └── time_counter/                           # face_detectionと同一
```

### 3.2 顔認識サンプルプログラムとの構造の違い

| 項目 | face_detection | image_classification |
|---|---|---|
| **AIモデル** | YOLO-Fastest（物体検出） | MobileNet v2 1.0 224（画像分類） |
| **入力サイズ** | 192x192x1 (Grayscale INT8) | 224x224x3 (RGB INT8) |
| **入力データサイズ** | 36,864バイト | 150,528バイト |
| **出力** | 2テンソル（バウンディングボックス座標） | 1テンソル（1000クラス確率） |
| **出力データサイズ** | 648 + 2,592 = 3,240バイト | 1,000バイト |
| **後処理** | NMS + バウンディングボックス座標計算 | Top-K分類結果取得 + ラベルマッピング |
| **前処理** | RGB565→Grayscale INT8変換 | RGB565→RGB INT8変換 (各バイト-128) |
| **画面表示** | カメラ画像 + バウンディングボックス描画 | カメラ画像 + 分類ラベル・確率テキスト表示 |
| **AIモデル配置** | OnChip ROM（デフォルト） | SDRAM (OSPI初期データ) |
| **AI入力バッファ配置** | OnChip RAM | SDRAM |
| **共通部分** | camera_layer, display_layer(基盤), fsp_custom, external_memory, console_output, time_counter | 同左 |

### 3.3 転倒検出への適性評価

| 評価観点 | face_detection (YOLO-Fastest) | image_classification (MobileNet v2) | 転倒検出への適性 |
|---|---|---|---|
| **タスクタイプ** | 物体検出（位置+クラス） | 画像分類（クラスのみ） | どちらも応用可能 |
| **位置情報** | バウンディングボックス出力あり | なし | face_detectionが有利。転倒した人の位置を特定できる |
| **入力サイズ** | 192x192 (小さい、高速) | 224x224 (標準的) | face_detectionが軽量で有利 |
| **入力チャンネル** | 1ch (Grayscale) | 3ch (RGB) | 転倒検出では姿勢認識にRGB情報が有用な場合がある |
| **モデル差し替え** | YOLOアーキテクチャの転倒検出モデルに差し替え | 「転倒/非転倒」2値分類モデルに差し替え | 両方可能 |
| **パイプライン再利用** | カメラ→前処理→NPU→後処理→表示の全体が再利用可能 | 同左 | 同等 |
| **後処理の複雑さ** | NMS + バウンディングボックス座標変換（複雑） | Top-K分類（単純） | image_classificationが簡潔 |

**結論:** 転倒検出の要件に応じて選択が分かれる。

- **位置特定が必要な場合（推奨）**: **face_detectionベースが適切**。YOLO系の物体検出モデルを転倒検出用に学習させ、転倒した人の位置をバウンディングボックスで示せる。後処理（NMS等）のコードもそのまま流用可能。
- **転倒有無のみ判定する場合**: image_classificationベースも選択肢。後処理が単純で実装が容易。ただし位置情報が得られないため、複数人環境での対応が難しい。

**product-requirements.mdのF-003「AIによる人の転倒検出」の要件を考慮すると、将来的な拡張性（複数人対応、転倒位置の通知等）の観点からface_detectionベースを推奨する。**

---

## 4. FreeRTOSとの連携およびμT-Kernel 3.0への移植観点

### 4.1 FreeRTOSの使用状況

#### 使用しているFreeRTOS機能

| FreeRTOS機能 | 使用箇所 | 用途 |
|---|---|---|
| **タスク (Task)** | `camera_display_thread_entry()`, `ai_inference_thread_entry()` | 2つのFreeRTOSスレッド。FSP Configuratorで定義 |
| **イベントグループ (Event Group)** | `g_ai_app_event`（グローバル） | スレッド間同期の中核。12種のイベントフラグで状態管理 |
| **`xEventGroupSetBits()`** | 多数 | 初期化完了通知、推論結果通知、カメラキャプチャ完了通知等 |
| **`xEventGroupWaitBits()`** | 多数 | 初期化完了待ち、カメラデータ待ち、推論入力準備待ち、VSYNC待ち等 |
| **`xEventGroupSetBitsFromISR()`** | ISRコールバック内 | カメラVINフレーム完了割り込み、GLCDC VSYNC割り込みからのイベント通知 |
| **`xEventGroupGetBits()`** | `camera_display_thread_entry.c` | 推論結果更新の非ブロック確認 |
| **`xEventGroupClearBits()`** | `camera_display_thread_entry.c` | 推論結果確認後のフラグクリア |
| **`vTaskDelay()`** | 各スレッド、ドライバ内 | スレッドのyield（1ms, 20ms, 25ms）、ソフトウェアウェイト |
| **`portYIELD_FROM_ISR()`** | ISRコールバック内 | ISR後の即時コンテキストスイッチ要求 |
| **`pdTICKS_TO_MS()`** | camera_layer.c, mipicsi_vin_hal_driver.c | msからティック値への変換 |

**使用していない機能**: セマフォ、キュー、ミューテックス、ソフトウェアタイマー、タスク通知

#### イベントフラグ一覧（`common_util.h`で定義）

| フラグ名 | ビット位置 | 用途 |
|---|---|---|
| `HARDWARE_DISPLAY_INIT_DONE` | bit 0 | ディスプレイ初期化完了 |
| `HARDWARE_CAMERA_INIT_DONE` | bit 1 | カメラ初期化完了 |
| `HARDWARE_ETHOSU_INIT_DONE` | bit 2 | Ethos-U NPU初期化完了 |
| `SOFTWARE_AI_INFERENCE_INIT_DONE` | bit 3 | AI推論ソフトウェア初期化完了 |
| `GLCDC_VSYNC` | bit 10 | GLCDC垂直同期割り込み |
| `MIPI_MESSAGE_SENT` | bit 11 | MIPIメッセージ送信完了 |
| `CAMERA_CAPTURE_COMPLETED` | bit 12 | カメラフレームキャプチャ完了 |
| `AI_INFERENCE_INPUT_IMAGE_READY` | bit 13 | AI推論入力画像準備完了 |
| `AI_INFERENCE_RESULT_UPDATED` | bit 14 | AI推論結果更新完了 |
| `DISPLAY_PAUSE` | bit 15 | ディスプレイ一時停止 |
| `CAMERA_AUTO_FOCUS_EXECUTE` | bit 16 | カメラオートフォーカス実行 |

#### RTOS依存部分の集中箇所

| ファイル | RTOS依存内容 |
|---|---|
| `camera_display_thread_entry.c` | xEventGroupSetBits/WaitBits/GetBits/ClearBits, vTaskDelay |
| `ai_inference_thread_entry.c` | xEventGroupWaitBits/SetBits, vTaskDelay |
| `camera_layer/camera_layer.c` | xEventGroupSetBitsFromISR, portYIELD_FROM_ISR, vTaskDelay(SOFTWARE_DELAY_MSマクロ) |
| `display_layer/display_layer.c` | xEventGroupSetBitsFromISR, portYIELD_FROM_ISR |
| `display_layer/face_detection_screen_mipi.c` | xEventGroupGetBits, xEventGroupWaitBits |
| `common_util.c` | xEventGroupGetBitsFromISR, xEventGroupSetBitsFromISR/ClearBitsFromISR |
| `console_output/console_output.c` | vTaskDelay |
| `fsp_custom/mipicsi_vin_hal_driver.c` | vTaskDelay(SOFTWARE_DELAY_MSマクロ) |
| `ra/fsp/src/rm_freertos_port/` | FreeRTOSのRA8P1向けポート実装 |
| `ra/aws/FreeRTOS/` | FreeRTOSカーネル本体 |

### 4.2 FreeRTOSからμT-Kernel 3.0への移植ポイント

#### API対応表

| FreeRTOS API | μT-Kernel 3.0 API | 備考 |
|---|---|---|
| `xTaskCreate()` | `tk_cre_tsk()` + `tk_sta_tsk()` | FSP Configuratorで自動生成されているスレッド定義をμT-Kernel 3.0のタスク生成に変更 |
| `vTaskDelay(ticks)` | `tk_dly_tsk(ms)` | 時間単位の違いに注意。FreeRTOSはティック単位、μT-Kernelはms単位 |
| `xEventGroupCreate()` | `tk_cre_flg()` | μT-Kernel 3.0のイベントフラグで代替可能 |
| `xEventGroupSetBits()` | `tk_set_flg()` | 直接対応。ビットパターン指定でフラグセット |
| `xEventGroupWaitBits()` | `tk_wai_flg()` | waitMode: TWF_ANDW(全ビット待ち) or TWF_ORW(いずれか待ち)。クリアモード指定あり |
| `xEventGroupClearBits()` | `tk_clr_flg()` | クリアマスクの指定方法が異なる（μT-Kernelは残すビットを指定） |
| `xEventGroupGetBits()` | `tk_ref_flg()` | フラグ状態の非ブロック参照 |
| `xEventGroupSetBitsFromISR()` | `tk_set_flg()` (ISR内) | μT-Kernel 3.0ではISR内からのAPI呼び出し制限を確認すること |
| `portYIELD_FROM_ISR()` | 不要（μT-Kernelが自動的にディスパッチ） | μT-Kernel 3.0ではISR終了後に自動的に再スケジュール |
| `pdTICKS_TO_MS()` | 不要 | μT-Kernel 3.0はms単位が標準 |
| `pvPortMalloc()` / `vPortFree()` | `tk_get_smb()` / `tk_rel_smb()` | 本プロジェクトでは動的メモリ確保は使用されていない |

#### 移植手順

1. **FreeRTOSカーネルの除去**: `ra/aws/FreeRTOS/`と`ra/fsp/src/rm_freertos_port/`をμT-Kernel 3.0のポート実装に置き換え
2. **タスク定義の変更**: FSP Configuratorで生成される`camera_display_thread.h`、`ai_inference_thread.h`のタスク定義をμT-Kernel 3.0のタスク生成コードに変更
3. **イベントグループの移行**: `g_ai_app_event`をμT-Kernel 3.0のイベントフラグ(`tk_cre_flg()`)に置き換え。12種のイベントフラグのビットパターンはそのまま流用可能
4. **ISRコールバック内の修正**: `xEventGroupSetBitsFromISR()`を`tk_set_flg()`に変更。`portYIELD_FROM_ISR()`は削除
5. **ソフトウェアウェイトの変更**: `vTaskDelay()`を`tk_dly_tsk()`に変更。`SOFTWARE_DELAY_MS`マクロの再定義
6. **ヘッダインクルードの変更**: FreeRTOSヘッダ (`FreeRTOS.h`, `task.h`, `event_groups.h`) をμT-Kernel 3.0ヘッダに変更

#### 移植リスク

- **低リスク**: 本プロジェクトのFreeRTOS使用は**タスク管理とイベントグループのみ**で、セマフォ・キュー・ミューテックス等の複雑な同期機構は使用していない。μT-Kernel 3.0のタスク管理とイベントフラグで1:1対応可能。
- **注意点**: FSP Configuratorが生成するRTOS統合コード（`ra_gen/`以下）はFreeRTOS前提。μT-Kernel 3.0移植時にはこの自動生成部分の手動書き換えまたは再生成が必要。

---

## 5. 性能特性

### 5.1 AI推論時間

ソースコード中にリアルタイムの推論時間計測機構が実装されている（`MainLoop_obj.cc:109-113`）。

```cpp
volatile uint32_t old_counter = TimeCounter_CurrentCountGet();
mera_invoke();
volatile uint32_t new_counter = TimeCounter_CurrentCountGet();
application_processing_time.ai_inference_time_ms = TimeCounter_CountValueConvertToMs(old_counter, new_counter);
```

- **YOLO-Fastest (顔検出)**: product-requirements.mdに「顔認識のサンプルプログラムが**3ms**で推論」と記載あり。Ethos-U55 NPUによるハードウェアアクセラレーション使用時の値。
- **参考**: docs/models_tested.mdによると、YOLO-fastest face detectionはTFLite INT8フォーマットで検証済み。
- **入力サイズ**: 192x192x1 = 36,864バイトと小さく、高速推論に寄与。

### 5.2 フレームレート

コンソール出力処理（`camera_display_thread_entry.c:296-309`）で各処理の時間を計測・表示する仕組みがある:

- **Camera image capture vsync period**: カメラフレームキャプチャ間隔
- **Camera post processing time**: カメラ画像後処理（バッファコピー）
- **AI inference pre processing time**: 前処理（RGB565→INT8変換）
- **AI inference time**: 推論時間（NPU実行）
- **LCD display vsync period**: LCD表示リフレッシュ間隔

カメラ設定としてOV5640は以下のフレームレートに対応（`camera_layer.h`）:
- 10fps, 20fps, 33fps, 40fps, 50fps, 100fps

ただし、実際のフレームレートは推論時間+前処理+後処理+表示の合計時間に制約される。スレッドのyield時間（camera_display: 20ms, ai_inference: 25ms）も考慮すると、実効的なフレームレートは計測が必要。

### 5.3 mimamori-senseのKPIとの比較

| KPI | 目標値 | face_detectionサンプルの見込み | 達成可能性 |
|---|---|---|---|
| フレームレート | 30fps以上 | カメラは33fps対応。ただし推論+表示のオーバーヘッドあり。スレッドyield合計45ms(≒22fps相当)が制約要因 | **要最適化**。yield時間の短縮、パイプライン並列化で改善余地あり |
| AI推論時間 | 5ms以内 | 顔認識で約3ms（Ethos-U55使用時） | **達成可能**。転倒検出モデルのサイズ次第だが、同等の軽量モデルを使用すれば達成見込み |
| 転倒検出精度 | 検出率90%以上 | モデル依存（サンプル自体は顔認識） | モデル選定・学習次第 |
| 誤検出率 | 5%以下 | モデル依存 | モデル選定・学習次第 |
| 転倒検出→通知時間 | 10秒以内 | 推論3ms + 前後処理数ms + 通知処理 | **達成可能** |

---

## 6. mimamori-senseへの流用可能性

### 6.1 カメラ撮影パイプラインの流用

**そのまま流用可能。**

以下のモジュールはAIモデルの種類に依存しないため、変更なしで使用できる:

| モジュール | 流用可否 | 理由 |
|---|---|---|
| `camera_layer/` 全体 | そのまま流用 | OV5640カメラの初期化・キャプチャ制御はモデル非依存 |
| `fsp_custom/mipicsi_vin_hal_driver` | そのまま流用 | MIPI-CSI VINドライバはモデル非依存 |
| `fsp_custom/r_mipi_phy` | そのまま流用 | MIPI PHYドライバはモデル非依存 |
| `display_layer/display_layer.c` | そのまま流用 | GLCDC・D/AVE 2D初期化・ダブルバッファ制御はモデル非依存 |
| `external_memory/` | そのまま流用 | OSPI外部メモリ制御はモデル非依存 |
| `console_output/` | そのまま流用 | コンソール出力はモデル非依存 |
| `time_counter/` | そのまま流用 | 時間計測はモデル非依存 |
| `common_util.c/.h` | そのまま流用 | イベントフラグ定義、エラーハンドリング等 |

### 6.2 AIモデルの差し替え方法（顔認識モデル→転倒検出モデル）

#### 手順

1. **転倒検出モデルの準備**
   - YOLO系の軽量物体検出モデル（YOLO-Fastest, YOLOv5-nano等）を「転倒/非転倒」の2クラスまたは「人物検出+姿勢」で学習
   - TFLiteまたはONNX形式でエクスポート

2. **RUHMIツールによるモデル変換**
   - `scripts/mcu_quantize.py`でINT8量子化（必要に応じて）
   - `scripts/mcu_deploy.py`またはRUHMI Webツールでモデルを変換
   - `mera/`ディレクトリの全ファイル（model.c, model.h, sub_0000_*.c/.h等）が自動生成される

3. **wrapper.hの更新**
   - 新モデルの入出力関数名に合わせて`mera_input_ptr()`, `mera_output1_ptr()`, `mera_output2_ptr()`を更新
   - 出力テンソル数が変わる場合は対応する出力取得関数を追加/削除

4. **ai_application_config.hの更新**
   - `AI_INPUT_IMAGE_WIDTH`, `AI_INPUT_IMAGE_HEIGHT`, `AI_INPUT_IMAGE_BYTE_PER_PIXEL`を新モデルの入力サイズに変更
   - `AI_MAX_DETECTION_NUM`を新モデルの最大検出数に変更

### 6.3 推論前後の画像前処理・後処理の変更箇所

#### 前処理の変更

| 変更箇所 | 現状（顔認識） | 転倒検出（想定） |
|---|---|---|
| `camera_display_thread_entry.c` 183行 | `image_rgb565_to_int8()` (RGB565→192x192 Grayscale INT8) | 新モデルの入力形式に合わせた変換関数に差し替え。例: RGB565→224x224x3 RGB INT8 |
| `camera_utils.c` | リサイズ・色空間変換ロジック | 新モデルの入力サイズ・色空間に対応する変換を実装 |

#### 後処理の変更

| 変更箇所 | 現状（顔認識） | 転倒検出（想定） |
|---|---|---|
| `MainLoop_obj.cc` | YOLOアンカー・量子化パラメータ設定、`DetectorPostProcess`呼び出し | 新モデルの出力仕様に合わせてアンカー値、量子化パラメータ、後処理を変更 |
| `DetectorPostProcessing.cc/.hpp` | 顔検出用NMS・バウンディングボックス計算 | 同じYOLOアーキテクチャなら流用可能。クラス数を変更（1→転倒検出クラス数）、閾値調整 |
| `face_detection_screen_mipi.c` | 顔検出結果のバウンディングボックス描画 | 転倒検出結果に合わせた表示に変更（警告メッセージ表示、バウンディングボックス色変更等） |
| `common_util.h` | `st_ai_detection_point_t`構造体 | 転倒検出結果に必要な情報（転倒スコア等）があれば拡張 |

### 6.4 FSP設定（configuration.xml）の変更有無

| 設定項目 | 変更要否 | 理由 |
|---|---|---|
| GLCDC設定 | **変更不要** | 表示解像度・色深度は同一 |
| MIPI-CSI / VIN設定 | **変更不要** | カメラキャプチャ設定は同一 |
| I2C設定 | **変更不要** | OV5640カメラ制御は同一 |
| GPTタイマー設定 | **変更不要** | カメラクロック・時間計測は同一 |
| Ethos-U (rm_ethosu)設定 | **変更不要** | NPU自体の設定は変わらない |
| IOPORT (GPIO)設定 | **変更不要** | LED・リセットピン等は同一 |
| DMAC設定 | **変更不要** | DMA転送設定は同一 |
| メモリ設定 (SDRAM/OSPI) | **要確認** | AIモデルサイズが大きい場合、SDRAM/OSPIの領域割り当て調整が必要。`application_config.h`の`AI_MODEL_ALLOCATION`設定で対応可能 |
| スレッド設定 | **μT-Kernel移植時に変更** | FreeRTOSスレッド定義をμT-Kernel 3.0タスク定義に変更 |
| **新規追加: F-004通知機能** | **追加必要** | 警報音出力用のペリフェラル（タイマーPWM出力、DAC等）の設定追加が必要 |

### 6.5 移植の手順まとめ

```
Phase 1: カメラ撮影パイプラインの移植（変更なし）
  ├── camera_layer/ をそのまま取り込み
  ├── fsp_custom/ をそのまま取り込み
  ├── display_layer/display_layer.c をそのまま取り込み
  ├── console_output/, time_counter/, external_memory/ をそのまま取り込み
  └── configuration.xml の該当ペリフェラル設定を反映

Phase 2: AI推論パイプラインの移植
  ├── 転倒検出AIモデルの準備・学習
  ├── RUHMIツールでモデル変換 → mera/ ファイル生成
  ├── ai_application_config.h の入力サイズ更新
  ├── wrapper.h の入出力関数更新
  ├── 前処理関数の差し替え（image_rgb565_to_int8 → 新変換関数）
  └── 後処理の実装（DetectorPostProcessing のパラメータ変更 or 新規実装）

Phase 3: 表示・通知の実装
  ├── face_detection_screen_mipi.c → 転倒検出画面に書き換え
  ├── F-004: 警報音通知機能の新規実装
  └── 表示情報の変更（推論時間、検出結果表示）

Phase 4: FreeRTOS → μT-Kernel 3.0 移植
  ├── FreeRTOSカーネルをμT-Kernel 3.0に置き換え
  ├── タスク定義の変更
  ├── イベントグループ → イベントフラグへの変更
  ├── vTaskDelay → tk_dly_tsk への変更
  └── ISRコールバック内のRTOS API変更

Phase 5: マルチコア対応（必要に応じて）
  ├── CPU0 (Cortex-M85): AI推論 + カメラ制御
  └── CPU1 (Cortex-M33): 通知処理 + その他
```

---

## 改訂履歴

| 日付 | バージョン | 変更内容 | 作成者 |
|------|-----------|---------|--------|
| 2026-02-20 | 1.0 | 初版作成 | Claude Code |
