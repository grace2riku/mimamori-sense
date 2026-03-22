# RUHMIリファレンス顔検出モデル アーキテクチャ調査レポート

## 調査日: 2026-03-21
## 関連Issue: #126 (F-003-3e)

## 調査背景

転倒検出用YOLOv8カスタムモデル（pico width=0.08, nano-slim width=0.12）がEthos-U55 NPU上でクラススコアが全てINT8最小値（-128）になり検出不能。CPU上のTFLite Interpreterでは正常動作（mAP 45-50%）するため、Ethos-U55 NPU固有の問題。

RUHMIリファレンスの顔検出モデルはEthos-U55で動作実績があるため、そのアーキテクチャを調査し、人物検出への転用可能性を評価する。

---

## 1. モデル概要

| 項目 | 顔検出（リファレンス） | 転倒検出（現行） |
|---|---|---|
| モデル名 | yolo-fastest_192_face_v4 | YOLOv8 pico カスタム |
| ベース | **YOLOv3** (YOLO-Fastest) | **YOLOv8** (Ultralytics) |
| 検出方式 | **アンカーベース** (各ブランチ3アンカー) | **アンカーフリー** |
| 入力 | 192x192x1 (Grayscale) | 192x192x3 (RGB) |
| 出力 | **2テンソル** (6x6, 12x12) | **1テンソル** [5, 756] |
| クラス数 | 1 (顔) | 1 (人物) |
| INT8サイズ | ~412KB | 366KB (pico) |
| NPUサブグラフ数 | **1 (sub_0000のみ)** | **3 (sub_0000→sub_0001→sub_0002)** |
| NPUアリーナ | 432KB | 969KB |
| Ethos-U55動作 | **OK** | **NG (class score全て-128)** |

## 2. 成功と失敗の原因分析

### 顔検出モデルが成功する理由

1. **単一NPUサブグラフ**: 全オペレータがEthos-U55対応。CPUフォールバックなし
2. **D-cache管理が単純**: Ethos-Uドライバ内部で自動処理。手動Clean/Invalidate不要
3. **アンカーベース方式**: Sigmoid、Conv2D、DepthwiseConv2D等、全てEthos-U55対応オペレータ

### 転倒検出モデル（YOLOv8）が失敗する理由

1. **3サブグラフ分割**: YOLOv8のアンカーフリーヘッドに含まれる**StridedSlice**がEthos-U55未対応のため、CPUフォールバックが発生
2. **D-cache管理が複雑**: サブグラフ間でClean/Invalidateの手動挿入が必要
3. **中間データコピー（memcpy）が必要**: sub_0000→sub_0002のデータ受け渡し
4. **Sigmoid出力の異常**: sub_0000のSigmoid1出力が全て-128

```
【顔検出: 単一NPUサブグラフ】
入力 → [sub_0000: NPU全体] → 出力0 (6x6) + 出力1 (12x12)

【転倒検出: 3サブグラフに分割（問題あり）】
入力 → [sub_0000: NPU] → Sigmoid1 + Reshape
     → [sub_0001: CPU] → StridedSlice (分割)
     → [sub_0002: NPU] → Concatenate → 最終出力
```

## 3. 顔検出モデルの技術詳細

### 3.1 出力テンソル構造

2つの出力テンソル（2スケール検出ヘッド）:

| 出力 | グリッド | サイズ | 値/アンカー |
|---|---|---|---|
| Output0 | 6x6 (stride 32) | 648バイト | 3アンカー x 6値 (x,y,w,h,obj,cls) |
| Output1 | 12x12 (stride 16) | 2,592バイト | 3アンカー x 6値 |

### 3.2 アンカーボックス

| ブランチ | 解像度 | アンカー (w,h) |
|---|---|---|
| Branch0 | 6x6 | (38,77), (47,97), (61,126) |
| Branch1 | 12x12 | (14,26), (19,37), (28,55) |

### 3.3 量子化パラメータ

| 出力 | Scale | Zero Point |
|---|---|---|
| Output0 (6x6) | 0.13408391 | 47 |
| Output1 (12x12) | 0.18535925 | 10 |

### 3.4 後処理（DetectorPostProcessing.cc）

YOLOv3スタイルのアンカーベースデコード:
1. INT8デクオンタイズ: `(int8_val - zeroPoint) * scale`
2. objectness = sigmoid(値[4])、閾値0.5で判定
3. bbox: sigmoid(tx)+cx, sigmoid(ty)+cy, exp(tw)*anchor_w, exp(th)*anchor_h
4. class_score = sigmoid(値[5]) * objectness
5. NMS（IoU閾値0.45）

### 3.5 学習パイプライン

- 学習フレームワーク: **darknet**（YOLO-Fastest原著）
- 事前学習済みモデル: emza-vs/ModelZoo から取得（WIDERFACEデータセット）
- 量子化: PTQ（Post-Training Quantization）、INT8 TFLiteで配布
- **学習スクリプトはRUHMIリファレンス内に含まれない**

## 4. 転倒検出への転用方針

### 4.1 推奨アプローチ: YOLO-Fastestで人物検出モデルを新規学習

| 項目 | 内容 |
|---|---|
| ベースモデル | YOLO-Fastest (dog-qiuqiu/Yolo-Fastest) |
| 学習フレームワーク | darknet |
| データセット | 既存の fall_detection_dataset (COCO-person + Roboflow-fall) |
| クラス設計 | 1クラス (person) ※転倒判定はアスペクト比で後処理 |
| 入力 | 192x192 (RGB or Grayscale) |
| 量子化 | PTQ INT8 |
| NPU実行 | sub_0000のみ（単一サブグラフ） |

### 4.2 必要な変更

| ファイル | 変更内容 |
|---|---|
| `mera/` 全体 | MERA再生成（自動） |
| `wrapper.h` | 新モデルの入出力関数名に更新（2出力対応） |
| `fall_detection_postprocess.c/h` | YOLOv3アンカーベースデコードに書き換え |
| `ai_application_config.h` | 出力テンソルサイズ・構造の更新 |
| `ai_cmd.c` | 新テンソル名・構造に対応 |

### 4.3 出力テンソルサイズ試算（person 1クラス, 192x192入力）

- Branch0 (6x6): 6x6x3x6 = 648バイト
- Branch1 (12x12): 12x12x3x6 = 2,592バイト
- 合計: 3,240バイト（顔検出と同一）
- アリーナ: 432KB程度（顔検出と同等）

## 5. 参考リンク

- [emza-vs/ModelZoo - yolo-fastest_192_face_v4.tflite](https://github.com/emza-vs/ModelZoo/tree/master/object_detection)
- [dog-qiuqiu/Yolo-Fastest](https://github.com/dog-qiuqiu/Yolo-Fastest)
- [dog-qiuqiu/Yolo-FastestV2](https://github.com/dog-qiuqiu/Yolo-FastestV2)
