# YOLO-Fastest 人物検出モデル学習・TFLite INT8変換 調査レポート

調査日: 2026-03-21
ステータス: 調査完了（学習・変換の実行はユーザー作業）

---

## 背景と経緯

EK-RA8P1 (Ethos-U55 NPU) 上で転倒検出を実現するにあたり、以下の経緯で YOLO-Fastest アーキテクチャへの移行を検討している。

1. **YOLOv8系カスタムモデル（pico等）がEthos-U55で動作しない** - YOLOv8はアンカーフリー設計であり、Ethos-U55 NPU向けVelaコンパイラでの変換時に複数サブグラフに分割され、NPU上で効率的に動作しない可能性が高い
2. **顔認識サンプルのYOLO-Fastestは動作実績あり** - emza-vs/ModelZooの yolo-fastest_192_face_v4.tflite (INT8) がRUHMI/MERAで変換済みで、Ethos-U55上で約3msで推論できている
3. **YOLO-Fastestはアンカーベース（YOLOv3系）** であり、Ethos-U55の単一サブグラフ変換と相性が良い

目標: YOLO-Fastest アーキテクチャで人物検出（person + fall の2クラスまたは person の1クラス）モデルを学習し、TFLite INT8に変換してEthos-U55上で動作させる。

---

## 1. 学習フレームワークの比較・選定

### 1.1 候補一覧

| 候補 | フレームワーク | リポジトリ | Ethos-U55実績 |
|------|-------------|-----------|--------------|
| **A) Nota-NetsPresso ModelZoo** | PyTorch (YOLOv5ベース) | [Nota-NetsPresso/ModelZoo-YOLOFastest-for-ARM-U55-M85](https://github.com/Nota-NetsPresso/ModelZoo-YOLOFastest-for-ARM-U55-M85) | あり（明示的にEthos-U55対応） |
| **B) darknet (原著)** | C (darknet) | [dog-qiuqiu/Yolo-Fastest](https://github.com/dog-qiuqiu/Yolo-Fastest) | 間接的（emza-vs経由で変換実績あり） |
| **C) Yolo-FastestV2** | PyTorch | [dog-qiuqiu/Yolo-FastestV2](https://github.com/dog-qiuqiu/Yolo-FastestV2) | 不明 |
| **D) 既存TFLite INT8モデル** | N/A（学習不要） | emza-vs/ModelZoo, MLCommons等 | 人物検出版は見つからず |

### 1.2 詳細評価

#### A) Nota-NetsPresso ModelZoo-YOLOFastest-for-ARM-U55-M85

**Ethos-U55を明示的にサポート。変換パイプラインが完備。**

- **概要**: NetsPresso社がYOLO-FastestをYOLOv5フレームワークベースでPyTorch再実装し、ARM Cortex-M85 / Ethos-U55向けに最適化したリポジトリ
- **対応デバイス**: Renesas-RA8D1 (Cortex-M85), Ensemble-E7-DevKit-Gen2 (Cortex-M55 + Ethos-U55)
- **入力サイズ**: 256x256（デフォルト）、カスタマイズ可能
- **パラメータ数**: 約0.3M（圧縮後0.2M）
- **推論時間参考値**: Ethos-U55 (128 MACs, 256x256) で 6.8ms
- **tutorial.ipynb**: Colabで実行可能なチュートリアルノートブックが提供されている

**利点**: Ethos-U55最適化済み、PyTorchベースでColab学習容易、YOLO形式データセット互換、TFLite INT8変換パイプライン内蔵

**注意点**: NetsPresso アカウント要、192x192カスタマイズ可否要確認、YOLOv5ベース再実装のため出力テンソル構造が顔認識サンプル(2ブランチ)と異なる可能性あり

#### B) darknet (原著 dog-qiuqiu/Yolo-Fastest)

**顔認識サンプルと同一アーキテクチャ。変換パスが複雑。**

- **概要**: YOLO-Fastestの原著リポジトリ。C言語のdarknetフレームワーク
- **入力サイズ**: 320x320（デフォルト）、cfgファイルで変更可能
- **TFLite変換**: 多段階の変換が必要（darknet .weights -> Keras .h5 -> TFLite FP32 -> TFLite INT8）

**利点**: 顔認識サンプルと同一のYOLO-Fastest V1アーキテクチャ、出力テンソル構造（2ブランチ、アンカーベース）同一で後処理コード流用可能

**問題点**: darknet->TFLite変換の信頼性が低い、INT8量子化後の精度劣化リスク未知、変換ツールのメンテナンス状況不透明

#### C) Yolo-FastestV2 (PyTorch)

**軽量だが、Ethos-U55との互換性が未検証。** PyTorch実装、パラメータ約250K。Shuffle Channel等の新オペレータがEthos-U55でサポートされるか不明。

#### D) 既存の人物検出TFLite INT8モデル

**YOLO-Fastest アーキテクチャの人物検出モデルは見つからなかった。** emza-vs/ModelZooには顔検出のみ。MLCommons person-detはMobileNet SSD系。

### 1.3 推奨順位

| 順位 | 候補 | 理由 |
|------|------|------|
| **1位** | **A) Nota-NetsPresso** | Ethos-U55向け最適化済み、変換パイプライン完備、Colabチュートリアルあり |
| **2位** | **B) darknet (原著)** | 顔認識サンプルと同一アーキテクチャ、後処理コード流用性最高 |
| **3位** | **C) Yolo-FastestV2** | PyTorchで学習しやすいが、Ethos-U55互換性未検証 |

**最終推奨**: まず **A) Nota-NetsPresso** で試し、出力構造の互換性に問題があれば **B) darknet** にフォールバックする2段階アプローチを推奨する。

---

## 2. データセット形式

既存の dataset/fall_detection_dataset.zip（COCO-person + Roboflow-fall）はYOLO形式 (class_id x_center y_center w h) でアノテーション済み。darknet、Nota-NetsPresso、Yolo-FastestV2のいずれでも**そのまま使用可能**。darknet用には .data/.names ファイルとパスリストの追加生成が必要。

---

## 3. TFLite INT8変換の具体的手順

### 3.1 パターンA: Nota-NetsPresso経由（推奨第1案）

変換フロー: カスタムデータ -> train.py -> best.pt -> auto_process.py -> TFLite INT8 -> RUHMI mcu_deploy.py --ethos -> mera/

Colabでの学習コマンド:

    !git clone https://github.com/Nota-NetsPresso/ModelZoo-YOLOFastest-for-ARM-U55-M85.git
    !pip install -r requirements.txt
    !python train.py --data ./data/person_fall.yaml --epochs 300 --weights "" --cfg ./models/yolo-fastest.yaml --batch-size 64 --img 192
    !python auto_process.py --data ./data/person_fall.yaml --name yolo_fastest_person_fall --weight_path ./runs/train/exp/weights/best.pt --target_device Ensemble-E7-DevKit-Gen2

### 3.2 パターンB: darknet -> Keras -> TFLite INT8（フォールバック案）

変換フロー: カスタムデータ -> darknet train -> best.weights -> convert.py (Keras .h5) -> TFLite FP32 -> INT8 -> RUHMI -> mera/

Colabでの手順概要:

    # darknetコンパイル・学習
    !git clone https://github.com/dog-qiuqiu/Yolo-Fastest.git
    # Makefile: GPU=1, CUDNN=1, OPENCV=1
    !make && ./darknet partial cfg/yolo-fastest.cfg weights yolo-fastest.conv.109 109
    !./darknet detector train data/person.data cfg/yolo-fastest-person.cfg yolo-fastest.conv.109 -dont_show -map

    # TFLite変換 (david8862/keras-YOLOv3-model-set)
    !python convert.py cfg weights output.h5 -f
    !python custom_tflite_convert.py --keras_model_file output.h5 --output_file fp32.tflite
    !python post_train_quant_convert.py --keras_model_file output.h5 --annotation_file valid.txt --model_input_shape 192x192 --sample_num 200 --output_file int8.tflite

---

## 4. Ethos-U55互換性の確認方法

### 4.1 Arm Velaコンパイラで確認

    pip install ethos-u-vela
    vela --accelerator-config ethos-u55-256 --system-config Ethos_U55_High_End_Embedded --memory-mode Sram_Only --verbose-all model_int8.tflite

確認ポイント: Operators placed on NPU の割合、サブグラフ数（sub_0000のみが理想）、CPU fallbackオペレータの有無

### 4.2 RUHMI mcu_deploy.py で確認

    python mcu_deploy.py ./models/ ./output/ --ethos

生成された mera/ 内に sub_0000_* のみ存在すれば単一サブグラフ。sub_0001_* 以降があればCPU-NPU分割が発生。

### 4.3 YOLO-Fastest V1 のEthos-U55互換性

顔認識サンプル実績: yolo-fastest_192_face_v4.tflite がRUHMI変換済み、sub_0000のみ（単一サブグラフ）、推論約3ms。
YOLO-Fastest V1のオペレータ（Conv2D, DepthwiseConv2D, LeakyReLU/ReLU6, MaxPool, Concat, Upsample）は全てEthos-U55サポート済み。
**結論**: クラス数・入力サイズ変更しても単一サブグラフ動作の可能性が非常に高い。Mish等の未サポートオペレータを導入しないこと。

### 4.4 Nota-NetsPresso版の互換性注意

YOLOv5ベースのためSiLU活性化関数がEthos-U55未サポートの可能性あり。--target_device Ensemble-E7-DevKit-Gen2 指定で最適化されるが、変換後にVela/RUHMIで必ず確認すること。

---

## 5. 入力チャネル（Grayscale vs RGB）

| 選択肢 | 利点 | 欠点 |
|--------|------|------|
| **A) 1ch Grayscale（推奨）** | 前処理流用可能、入力1/3で高速、Arena余裕 | cfgでchannels=1変更必要、色情報なし |
| **B) 3ch RGB** | 標準構成、色情報利用可 | 前処理変更必要、推論時間3倍 |
| **C) RGB学習+推論時Gray->3ch** | 標準学習可 | 分布不一致で精度劣化リスク |

**推奨: A) 1ch Grayscale**。転倒検出は姿勢判定が主であり色情報の重要度は低い。192x192x1=36,864バイトでArena制約に余裕。5ms以内KPI達成容易。

---

## 6. cfgファイルのカスタマイズ（darknet使用時）

    [net] width=192, height=192, channels=1
    [yolo] classes=2 (person + fall)
    [convolutional] filters=21  # (2+5)*3

アンカー再計算: ./darknet detector calc_anchors data/person.data -num_of_clusters 6 -width 192 -height 192

---

## 7. RUHMIによるMERAコード生成

### 7.1 mcu_deploy.py

    python scripts/mcu_deploy.py models/ output/ --ethos

主要設定: sys_config=RA8P1, memory_mode=Sram_Only, accel_config=ethos-u55-256, weight_location=flash

### 7.2 FP32からの量子化

    python scripts/mcu_quantize.py models_fp32/ output/ --ethos --calib_num 5

注意: キャリブレーションはランダムデータ。学習フレームワーク側でINT8量子化してmcu_deploy.pyに入力する方が精度が良い。

### 7.3 生成ファイル

model.c/.h, model_io_data.c/.h, sub_0000_command_stream.c/.h, sub_0000_invoke.c/.h, sub_0000_io_data.c/.h, sub_0000_model_data.c/.h, sub_0000_tensors.c/.h, ethosu_common.h, wrapper.h

---

## 8. 実施計画（推奨手順）

### Phase 1: Nota-NetsPresso による迅速な検証（1-2日）

1. NetsPresso アカウント登録
2. Colabで tutorial.ipynb ベースのノートブック作成
3. fall_detection_dataset をYOLO形式で投入
4. train.py で学習（192x192、person+fall 2クラス）
5. auto_process.py で TFLite INT8 変換
6. RUHMI mcu_deploy.py --ethos で MERA 変換
7. **確認**: 出力テンソル数・サイズ・構造、アリーナサイズ、サブグラフ数

### Phase 2: darknetで再挑戦（予備3-5日）

Phase 1で互換性問題があれば実施。darknetコンパイル -> cfg カスタマイズ -> 学習 -> Keras -> TFLite INT8 -> RUHMI変換

### Phase 3: MCU組み込み統合

mera/ を e2studio_CPU0/src/ai_application/fall_detection/mera/ に配置、wrapper.h更新、後処理パラメータ更新、ビルド・実機テスト

---

## 9. リスクと不確実要素

| リスク | 影響度 | 軽減策 |
|--------|--------|--------|
| Nota-NetsPresso版の出力構造が顔認識サンプルと大きく異なる | 高 | darknet版にフォールバック |
| darknet -> TFLite変換でオペレータ互換性エラー | 中 | 複数の変換ツールを試す |
| 192x192 Grayscale入力でのINT8量子化後精度が低い | 中 | 入力サイズ拡大やRGB入力も検討 |
| TFLite INT8モデルがArena上限 (432KB) を超過 | 低 | YOLO-Fastest約0.3Mパラメータで超過リスク低い |

---

## 10. 参考リンク

- [Nota-NetsPresso/ModelZoo-YOLOFastest-for-ARM-U55-M85](https://github.com/Nota-NetsPresso/ModelZoo-YOLOFastest-for-ARM-U55-M85)
- [dog-qiuqiu/Yolo-Fastest](https://github.com/dog-qiuqiu/Yolo-Fastest)
- [dog-qiuqiu/Yolo-FastestV2](https://github.com/dog-qiuqiu/Yolo-FastestV2)
- [Lebhoryi/yolo-fastest_inference](https://github.com/Lebhoryi/yolo-fastest_inference)
- [david8862/keras-YOLOv3-model-set](https://github.com/david8862/keras-YOLOv3-model-set)
- [emza-vs/ModelZoo](https://github.com/emza-vs/ModelZoo)
- [renesas/ruhmi-framework-mcu](https://github.com/renesas/ruhmi-framework-mcu)
- [Renesas RUHMI Framework Quick Start Guide (RA8P1)](https://www.renesas.com/en/document/qsg/renesas-ruhmi-framework-quick-start-guide-ra8p1)

---

## 改訂履歴

| 日付 | バージョン | 変更内容 | 作成者 |
|------|-----------|---------|--------|
| 2026-03-21 | 1.0 | 初版作成 | Claude Code |
