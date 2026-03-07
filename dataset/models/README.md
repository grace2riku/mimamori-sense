# 転倒検出モデル

## yolov8n_fall_int8.tflite

| 項目 | 値 |
|------|-----|
| ベースモデル | Ultralytics YOLOv8n |
| 学習データ | fall_detection_dataset.zip (COCO-person + Roboflow-fall) |
| 入力 | 192×192×3 (RGB), NHWC, INT8 |
| 出力 | 1×2268×6 (INT8) |
| ファイルサイズ | 3,149 KB |
| 量子化 | Post-training INT8 (200枚キャリブレーション) |
| mAP@0.5 | 67.8% |
| Precision | 80.4% |
| Recall | 59.3% |
| 学習エポック | 100 |
| 学習環境 | Google Colab (L4 GPU) |
| 作成日 | 2026-03-05 |
| 作成ノートブック | `dataset/scripts/train_yolov8_colab.ipynb` |

## yolov8_pico_fall_int8.tflite

| 項目 | 値 |
|------|-----|
| ベースモデル | カスタム YOLOv8 pico (depth=0.33, width=0.08, max_channels=256) |
| 学習データ | fall_detection_dataset.zip (COCO-person + Roboflow-fall) |
| 入力 | 192×192×3 (RGB), NHWC, INT8 |
| 出力 | 1×5×756 (INT8) |
| ファイルサイズ | 366 KB |
| パラメータ数 | 263,435 (0.263M) |
| 量子化 | Post-training INT8 (200枚キャリブレーション) |
| mAP@0.5 | 45.9% |
| Precision | 57.6% |
| Recall | 44.1% |
| 学習エポック | 200 |
| 学習環境 | Google Colab (L4 GPU) |
| 作成日 | 2026-03-07 |
| 作成ノートブック | `dataset/scripts/train_yolov8_pico_colab.ipynb` |

### yolov8n との比較

| 指標 | pico | YOLOv8n | 差分 |
|------|------|---------|------|
| INT8サイズ | 366 KB | 3,149 KB | -88.4% |
| パラメータ数 | 0.263M | 3.0M | -91.2% |
| mAP@0.5 | 45.9% | 67.8% | -21.9pt |
| Recall | 44.1% | 59.3% | -15.2pt |

## yolov8_nano-slim_fall_int8.tflite

| 項目 | 値 |
|------|-----|
| ベースモデル | カスタム YOLOv8 nano-slim (depth=0.33, width=0.12, max_channels=256) |
| 学習データ | fall_detection_dataset.zip (COCO-person + Roboflow-fall) |
| 入力 | 192×192×3 (RGB), NHWC, INT8 |
| 出力 | 1×5×756 (INT8) |
| ファイルサイズ | 457 KB |
| パラメータ数 | 348,667 (0.349M) |
| 量子化 | Post-training INT8 (200枚キャリブレーション) |
| mAP@0.5 | 50.7% |
| Precision | 64.6% |
| Recall | 46.0% |
| 学習エポック | 200 |
| 学習環境 | Google Colab (L4 GPU) |
| 作成日 | 2026-03-07 |
| 作成ノートブック | `dataset/scripts/train_yolov8_pico_colab.ipynb` |
| 備考 | INT8サイズ457KBがArena上限(432KB)を25KB超過のため不採用 |

### 3モデル比較

| 指標 | pico | nano-slim | YOLOv8n |
|------|------|-----------|---------|
| INT8サイズ | 366 KB | 457 KB | 3,149 KB |
| パラメータ数 | 0.263M | 0.349M | 3.0M |
| mAP@0.5 | 45.9% | 50.7% | 67.8% |
| Recall | 44.1% | 46.0% | 59.3% |
| Arena制約 | OK | OVER (+25KB) | OVER (+2,717KB) |
| 採用 | **採用** | 不採用 | 不採用 |

## 既知の課題

- モデルサイズ 3.1MB は MCU 内蔵SRAM Arena (432KB) を超過 → Issue #102
- RGB 3ch 入力、カメラは Grayscale 1ch → Issue #103
- mAP 67.8% は目標 90% に未達 → Issue #101
