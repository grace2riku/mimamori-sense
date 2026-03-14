# F-003-3c: 転倒検出モデルのGrayscale (1ch) 入力対応 実装レポート

Issue: #103

## 1. 概要

MCU (EK-RA8P1) のカメラはGrayscale 1チャネルで運用予定のため、
転倒検出モデルの入力をRGB 3チャネルからGrayscale 1チャネルに変更する。

## 2. 対応方針の検討

### 3案の比較

| 案 | 方式 | 精度 | メモリ効率 | 実装コスト | 採用 |
|----|------|------|-----------|-----------|------|
| A | Grayscaleで再学習 | 高 | 高 (1ch) | 中 | 採用 |
| B | デプロイ時チャネル複製 | 中 | 低 (3ch) | 低 | 不採用 |
| C | ONNX後処理で入力修正 | 低 | 高 (1ch) | 中 | 不採用 |

### 案A採用の理由

- モデルがGrayscale画像の特徴を直接学習するため最も精度が高い
- 入力バッファサイズが1/3 (110,592 -> 36,864バイト) に削減される
- MCUカメラの出力と同条件で学習するため、ドメインシフトが最小
- 顔認識サンプル (YOLO-Fastest) も192x192x1で動作しており、実績がある

### 案B不採用の理由

- 入力バッファが3倍 (110KB) 必要でメモリ効率が悪い
- Grayscale -> RGB変換の前処理コストが不要な余分な処理
- RGB事前学習重みがGrayscale入力に最適化されていない

### 案C不採用の理由

- 3chの重みを1chに統合する際の精度劣化リスクが不透明
- 再学習なしの重み変換は品質保証が困難

## 3. 実装内容

### 3.1 作成した成果物

| ファイル | 説明 |
|----------|------|
| `dataset/scripts/train_yolov8_grayscale_colab.ipynb` | YOLOv8n Grayscale版学習ノートブック (新規) |
| `dataset/scripts/yolov8n-grayscale-fall.yaml` | YOLOv8n Grayscale版カスタムモデルYAML (新規) |
| `dataset/scripts/train_yolov8_pico_colab.ipynb` | pico/nano-slim版ノートブック (Grayscale変換追加) |
| `doc/report/f003_03c_grayscale_implementation_report.md` | 本レポート (新規) |

### 3.2 YOLOv8n Grayscale版 (`train_yolov8_grayscale_colab.ipynb`)

YOLOv8n標準構成 (width=0.25) でGrayscale 1ch入力対応の学習ノートブック。

主な特徴:
- **事前Grayscale変換**: データセット画像をPIL `Image.convert('L')` でGrayscaleに変換
- **カスタムモデルYAML**: `ch: 1` 指定でモデルアーキテクチャを1ch入力に
- **転移学習対応**: `yolov8n.pt` からの転移学習が可能 (Ultralyticsが最初のConv層を自動調整)
- **Grayscale用データ拡張**: `hsv_h=0.0`, `hsv_s=0.0` (色相・彩度変換を無効化)、`hsv_v=0.4` (明度変換のみ有効)
- **受け入れ条件チェック**: Step 8 で入力形状 [1, 192, 192, 1]、INT8推論テストを自動実行
- **RGB版との精度比較**: Step 5 で mAP@0.5 の差分を自動計算

### 3.3 カスタムモデルYAML (`yolov8n-grayscale-fall.yaml`)

```yaml
nc: 1  # person only
ch: 1  # Grayscale
scales:
  n: [0.33, 0.25, 1024]  # YOLOv8n standard
```

YOLOv8nの標準構成からの差分は `ch: 1` と `nc: 1` のみ。
backbone/head構造は変更なし。

### 3.4 pico版ノートブックの改善 (`train_yolov8_pico_colab.ipynb`)

既存のpicoノートブックに以下の改善を実施:

1. **Grayscale変換セルの追加** (Step 2):
   - RGB画像をGrayscaleに事前変換するコードを追加
   - `DATASET_GRAY_DIR` にGrayscale画像とラベルファイルをコピー
2. **data.yamlパスの修正**: Grayscale変換済みデータセットを参照するように変更
3. **INT8量子化キャリブレーションの修正**: Grayscaleデータセットからキャリブレーション画像を取得

## 4. 技術的詳細

### 4.1 UltralyticsでのGrayscale学習の注意点

Ultralyticsの `ch: 1` 指定には以下の注意点がある:

1. **モデルアーキテクチャ**: `ch: 1` を指定すると、最初のConv層の入力チャネルが1になる
2. **データローダー**: Ultralyticsのデータローダーはデフォルトでcv2.IMREAD_COLORを使用し、
   Grayscale画像もBGR (3ch) として読み込む場合がある
3. **対策**: データセット画像を事前にGrayscale (L mode) に変換しておくことで、
   読み込み時の挙動に依存しない確実なGrayscale学習が可能

### 4.2 転移学習時の重み調整

`YOLO(model_yaml_path).load('yolov8n.pt')` を実行すると:
- 最初のConv層: 3ch重み [out_ch, 3, k, k] -> 1ch重み [out_ch, 1, k, k]
  (3チャネルの平均を取って1チャネルに変換)
- その他の層: そのまま転移

### 4.3 入力バッファサイズの比較

| 項目 | RGB版 | Grayscale版 | 削減率 |
|------|-------|-------------|--------|
| 入力形状 | [1, 192, 192, 3] | [1, 192, 192, 1] | - |
| バッファサイズ | 110,592 バイト | 36,864 バイト | 66.7% |
| 顔認識サンプルと同一 | No | Yes | - |

### 4.4 モデルサイズへの影響

Grayscale化によるモデルサイズ変化は最小限:
- 影響を受けるのは最初のConv層のみ (3ch -> 1ch)
- YOLOv8nの場合、最初のConv層は全パラメータの0.1%未満
- INT8モデルサイズはほぼ変わらない (約3MB)
- サイズ削減が必要な場合は F-003-3b (pico/nano-slim) と組み合わせる

## 5. 受け入れ条件の検証方法

ノートブックのStep 8で以下を自動検証:

| 条件 | 検証内容 | 判定基準 |
|------|----------|----------|
| 入力形状 | TFLiteモデルの入力テンソル形状 | [1, 192, 192, 1] |
| 精度 | mAP@0.5 のRGB版との差分 | -10pt以内で許容 |
| INT8動作 | ダミー入力での推論テスト | エラーなく完了 |
| バッファサイズ | 入力テンソルのバイト数 | 36,864 バイト |

## 6. 使い方

### YOLOv8n Grayscale版 (精度優先、サイズ大)

```
Google Colab で train_yolov8_grayscale_colab.ipynb を実行
```

生成されるモデル:
- `model_grayscale_int8.tflite`: ~3MB (Arena 432KB超過)
- F-003-3b (小型化) との組み合わせが必要

### YOLOv8 pico Grayscale版 (サイズ優先)

```
Google Colab で train_yolov8_pico_colab.ipynb を実行
MODEL_VARIANT = 'pico' を指定
```

生成されるモデル:
- `model_pico_int8.tflite`: ~200KB (Arena 432KB以内)
- Grayscale + 小型化の両方に対応

## 7. 依存関係と次のステップ

### F-003-3bとの関係

YOLOv8n Grayscale版のINT8モデルは約3MBで Arena制約 (432KB) を超過する。
実用化にはF-003-3b (小型化) との組み合わせが必須:
- **pico版 (推奨)**: `train_yolov8_pico_colab.ipynb` で既にGrayscale対応済み
- **nano-slim版**: 同ノートブックで `MODEL_VARIANT = 'nano-slim'` を指定

### 次のステップ

1. Google ColabでGrayscale版ノートブックを実行し、精度を検証
2. RGB版との精度差を確認 (目標: -10pt以内)
3. pico版との精度・サイズのトレードオフを分析
4. F-003-3d (RUHMI/MERA SDK検証) でEthos-U55への変換を確認
