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

## 既知の課題

- モデルサイズ 3.1MB は MCU 内蔵SRAM Arena (432KB) を超過 → Issue #102
- RGB 3ch 入力、カメラは Grayscale 1ch → Issue #103
- mAP 67.8% は目標 90% に未達 → Issue #101
