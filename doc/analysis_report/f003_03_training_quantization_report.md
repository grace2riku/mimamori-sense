# F-003-3: 転倒検出AIモデルの学習とINT8量子化レポート

本レポートはIssue #20 (F-003-3)の成果物である。F-003-1で選定したYOLO-Fastest v1.1をF-003-2で準備したデータセットで学習し、Ethos-U55 NPU向けにINT8量子化するための手順・方針をまとめる。

---

## 1. 前提条件

### 1.1 F-003-1 選定モデルの仕様

| 項目 | 値 |
|---|---|
| アーキテクチャ | YOLO-Fastest v1.1 |
| 入力 | 192x192x1 (Grayscale, INT8) |
| パラメータ数 | 約0.24M |
| INT8モデルサイズ (推定) | 約412KB |
| 検出クラス | person (class_id=0) |
| 転倒判定方式 | アプローチC: 人物検出 + BBoxアスペクト比判定 |
| 出力ブランチ | 2 (6x6グリッド + 12x12グリッド, 各3アンカー) |

### 1.2 KPI目標

| KPI | 目標値 | 出典 |
|---|---|---|
| AI推論時間 | 5ms以内 | product-requirements.md |
| 転倒検出精度 (検出率) | 90%以上 | product-requirements.md |
| 誤検出率 | 5%以下 | product-requirements.md |
| モデルアリーナサイズ | 432KB (442,368バイト) 以内 | sub_0000_tensors.c |

### 1.3 F-003-2 データセット

| 項目 | 値 |
|---|---|
| 総画像数 | 15,787枚 |
| Trainセット | 11,050枚 (拡張後: 32,853枚) |
| Validationセット | 2,368枚 |
| Testセット | 2,369枚 |
| データソース | COCO 2017 (personクラス) + Roboflow Fall Detection |
| アノテーション形式 | YOLO (class_id cx cy w h) |

---

## 2. 学習環境の前提条件

### 2.1 ハードウェア要件

| 項目 | 推奨 |
|---|---|
| GPU | NVIDIA GPU (CUDA対応, RTX 3060以上推奨) |
| VRAM | 4GB以上 (YOLO-Fastestは軽量のため少量で可) |
| RAM | 16GB以上 |
| ストレージ | 50GB以上の空き容量 (データセット + チェックポイント) |

### 2.2 ソフトウェア環境

```bash
# Python環境
python >= 3.8
pip install torch torchvision       # PyTorch (パスB用)
pip install onnx onnxruntime
pip install tensorflow               # TFLite変換・検査用
pip install numpy Pillow opencv-python
pip install pycocotools              # mAP評価用

# RUHMI/MERA SDK (量子化・デプロイ用)
# Renesas MERA SDKライセンスが必要 - Renesasに問い合わせ
pip install mera                      # Renesas提供
```

---

## 3. 学習アプローチ

2つの学習パスが利用可能である。**パスB (PyTorch / Nota-NetsPresso)** を推奨する。モダンな学習パイプラインとONNXエクスポートツールが充実しているためである。

### 3.1 パスA: Darknet (Cフレームワーク)

**リポジトリ**: [dog-qiuqiu/Yolo-Fastest](https://github.com/dog-qiuqiu/Yolo-Fastest)

#### 3.1.1 セットアップ

```bash
git clone https://github.com/dog-qiuqiu/Yolo-Fastest.git
cd Yolo-Fastest

# GPU対応ビルド
# Makefile編集: GPU=1, CUDNN=1, OPENCV=1
make -j$(nproc)
```

#### 3.1.2 設定ファイル

**データ設定** (`fall_detection.data`):
```
classes = 1
train = data/fall_detection/train.txt
valid = data/fall_detection/val.txt
names = data/fall_detection.names
backup = backup/
```

**クラス名ファイル** (`fall_detection.names`):
```
person
```

**ネットワーク設定** (`yolo-fastest-1.1-fall.cfg`):

ベースの yolo-fastest-1.1.cfg からの主な変更点:

| パラメータ | 値 | 備考 |
|---|---|---|
| width | 192 | 入力幅 (顔認識ベースラインと同じ) |
| height | 192 | 入力高さ |
| channels | 1 | Grayscale入力 |
| batch | 64 | 学習バッチサイズ |
| subdivisions | 16 | 実効ミニバッチ = 64/16 = 4 |
| max_batches | 10000 | 総学習イテレーション |
| steps | 8000,9000 | 学習率減衰スケジュール |
| classes | 1 | 各[yolo]レイヤに設定 |
| filters | 18 | 各[yolo]レイヤ直前のconv: 3*(5+1)=18 |
| anchors | 後述 | personデータセットに合わせて再計算 |

**アンカー計算**:
```bash
./darknet detector calc_anchors fall_detection.data \
    -num_of_clusters 6 -width 192 -height 192
```

顔認識ベースラインのアンカー (参考):
- Branch 0 (6x6): `38,77, 47,97, 61,126`
- Branch 1 (12x12): `14,26, 19,37, 28,55`

人物は顔よりアスペクト比が異なるため、personデータセットに合わせて再計算が必須である。

#### 3.1.3 学習コマンド

```bash
./darknet detector train fall_detection.data \
    yolo-fastest-1.1-fall.cfg \
    -map -gpus 0
```

#### 3.1.4 出力

- `backup/yolo-fastest-1.1-fall_best.weights` (最良mAPチェックポイント)
- `backup/yolo-fastest-1.1-fall_final.weights`

### 3.2 パスB: PyTorch / Nota-NetsPresso (推奨)

**リポジトリ**: [Nota-NetsPresso/ModelZoo-YOLOFastest-for-ARM-U55-M85](https://github.com/Nota-NetsPresso/ModelZoo-YOLOFastest-for-ARM-U55-M85)

#### 3.2.1 セットアップ

```bash
git clone https://github.com/Nota-NetsPresso/ModelZoo-YOLOFastest-for-ARM-U55-M85.git
cd ModelZoo-YOLOFastest-for-ARM-U55-M85
pip install -r requirements.txt
```

#### 3.2.2 データセット配置

YOLO形式でデータセットを配置する:
```
dataset/fall_detection/
  train/
    images/
    labels/
  val/
    images/
    labels/
  test/
    images/
    labels/
  data.yaml
```

**data.yaml**:
```yaml
train: dataset/fall_detection/train/images
val: dataset/fall_detection/val/images
test: dataset/fall_detection/test/images
nc: 1
names:
  - person
```

#### 3.2.3 推奨ハイパーパラメータ

| パラメータ | 推奨値 | 備考 |
|---|---|---|
| 入力サイズ | 192x192 | Ethos-U55デプロイ先に合わせる |
| バッチサイズ | 64 | GPU VRAMに応じて調整 |
| エポック数 | 300 | Early stopping patience=50 |
| 学習率 (初期) | 0.01 | SGD + Cosine Annealing |
| 学習率 (最終) | 0.001 | Cosineスケジュール終端 |
| Momentum | 0.937 | SGD momentum |
| Weight decay | 0.0005 | L2正則化 |
| Warmupエポック | 3 | 線形ウォームアップ |
| IoU閾値 | 0.5 | mAP計算用 |
| 信頼度閾値 | 0.5 | NMS用 |
| NMS IoU閾値 | 0.45 | NMS用 |
| 入力チャンネル | 1 | Grayscale |

#### 3.2.4 学習コマンド

```bash
python train.py \
    --data dataset/fall_detection/data.yaml \
    --cfg configs/yolo-fastest-1.1.cfg \
    --img-size 192 \
    --batch-size 64 \
    --epochs 300 \
    --weights "" \
    --name fall_detection_v1
```

#### 3.2.5 ONNXエクスポート

```bash
python export.py \
    --weights runs/train/fall_detection_v1/weights/best.pt \
    --img-size 192 \
    --batch-size 1
```

出力: `best.onnx`

---

## 4. モデル変換パイプライン

### 4.1 変換フロー

```
PyTorch (.pt)  -->  ONNX (.onnx)  -->  TFLite FP32 (.tflite)
    or                                       |
Darknet (.weights + .cfg)                    v
                                    TFLite INT8 (.tflite)
                                        [RUHMI/MERAで量子化]
                                             |
                                             v
                                    MCU用C99コード + Vela最適化モデル
                                        [RUHMI mcu_deploy.py]
```

### 4.2 ONNX → TFLite FP32変換

`dataset/scripts/convert_to_tflite.py` を使用する:

```bash
python dataset/scripts/convert_to_tflite.py \
    --onnx best.onnx \
    --output model_fp32.tflite
```

### 4.3 Darknet weightsの変換 (パスAのみ)

Darknetで学習したモデルの場合、darknet2pytorch等の変換ツールでまずONNXに変換し、上記のONNX→TFLiteパイプラインに接続する。

---

## 5. INT8量子化

### 5.1 量子化アプローチ

モデル形式に応じて2つの量子化パスがある:

| アプローチ | 入力形式 | ツール | 出力 |
|---|---|---|---|
| A: RUHMI mcu_quantize.py | ONNX / TFLite FP32 | MERA Quantizer | .mera (量子化済み) |
| B: TFLite組み込みPTQ | TFLite FP32 | TensorFlow Lite | TFLite INT8 |

**アプローチAを推奨する。** 最終的なデプロイコード生成に使用するMERA量子化器と同じツールを使用するため、一貫性が保証される。

### 5.2 アプローチA: RUHMI mcu_quantize.py

RUHMIスクリプト `scripts/mcu_quantize.py` は以下の完全パイプラインを実行する:
1. FP32モデル (ONNX or TFLite) の読み込み
2. キャリブレーションデータの生成 (ランダムまたはデータセットから)
3. MERA量子化器によるINT8量子化
4. 量子化モデルのデプロイとMERAインタプリタでの実行
5. MCU用C99コードの生成 (Ethos-U55最適化オプション付き)
6. 生成コードの検証 (インタプリタ出力との一致確認)

#### 5.2.1 使用方法

```bash
# FP32モデルをディレクトリに配置
mkdir -p models_fp32
cp model_fp32.tflite models_fp32/

# Ethos-U55向け量子化 + デプロイ
python scripts/mcu_quantize.py models_fp32 deploy_output \
    --calib_num 5 \
    --ethos \
    --ref_data
```

#### 5.2.2 主要パラメータ

| パラメータ | デフォルト値 | 説明 |
|---|---|---|
| models_path | (必須) | FP32モデル格納ディレクトリ |
| deploy_dir | (必須) | デプロイ成果物の出力先 |
| --calib_num (-c) | 5 | キャリブレーションサンプル数 |
| --ethos (-e) | false | Ethos-U55 NPUデプロイを有効化 |
| --ref_data (-r) | false | リファレンステストデータの生成 |

#### 5.2.3 内部量子化器の設定

`mcu_quantize.py` のソースコードから:

```python
# MCUターゲット用量子化器プリセット
quantizer_config = quantizer.QuantizerConfigPresets.MCU

# 品質閾値: PSNR >= 5 が必要
if Q_result["psnr"] < 5:
    # 量子化品質が低すぎる場合は中断
    ...

# 検証基準: MSE <= 0.1 または PSNR >= 28
matches = (mse <= 0.1 or psnr >= 28)
```

#### 5.2.4 RA8P1向けVela設定

ARM Ethos-Uコンパイラ (Vela) の設定:

```python
vela_config = {
    "sys_config": "RA8P1",
    "memory_mode": "Sram_Only",
    "accel_config": "ethos-u55-256",  # 256 MACs構成
    "optimise": "Performance",
    "enable_ospi": False,  # モデルが1.5MB超の場合はTrue
    "verbose_all": False,
}

mcu_config = {
    "suffix": "",
    "weight_location": "flash",  # または "iram"
    "use_x86": True,  # 検証用; 本番ではFalse
}
```

### 5.3 アプローチB: TFLite組み込みPTQ

`dataset/scripts/convert_to_tflite.py` を使用する:

```bash
python dataset/scripts/convert_to_tflite.py \
    --quantize \
    --input model_fp32.tflite \
    --output model_int8.tflite \
    --cal-images dataset/merged/images/val/ \
    --cal-count 100
```

TFLiteの学習後量子化 (Post-Training Quantization) を代表データセットで実行する。

### 5.4 キャリブレーションデータ

最良の量子化品質を得るためには、ランダムデータではなく実際の学習/検証画像を使用する:

- **最低推奨**: 100枚のキャリブレーション画像
- **最適**: 学習セットから500-1,000枚
- 多様なシーン (立位、座位、転倒、様々な背景) をカバーすること

RUHMIスクリプトのデフォルト (`--calib_num 5`) はクイックテスト用であり、本番品質には不十分である。`generate_input_data()` 関数を修正して実画像を読み込むことを推奨する。

---

## 6. RUHMI/MERAデプロイ (mcu_deploy.py)

### 6.1 INT8 TFLiteモデルの直接デプロイ

アプローチBや外部ツールでINT8量子化済みTFLiteモデルがある場合、`mcu_deploy.py` で直接デプロイする:

```bash
mkdir -p models_int8
cp model_int8.tflite models_int8/

python scripts/mcu_deploy.py models_int8 deploy_ethos \
    --ethos \
    --ref_data
```

### 6.2 出力ディレクトリ構成

```
deploy_ethos/
  <model_name>_no_ospi/
    build/
      MCU/
        compilation/
          src/
            sub_0000.c         # メインモデル計算
            sub_0000.h         # API宣言ヘッダ
            sub_0000_tensors.c # 重みデータとアリーナ確保
            sub_0000_tensors.h # テンソルサイズ定義
            sub_0000_params.h  # モデルパラメータ
```

### 6.3 生成されるAPI関数

生成されるC99コードは以下のAPIを提供する:

**CPU + Ethos (Platform.MCU_ETHOS) の場合:**

```c
// モデルの初期化と実行
void RunModel(bool clean_outputs);

// 入出力バッファのポインタ取得
int8_t* GetModelInputPtr_image_input();
int8_t* GetModelOutputPtr_Identity_XXXXX();   // Branch 0 (6x6)
int8_t* GetModelOutputPtr_Identity_1_XXXXX(); // Branch 1 (12x12)
```

**CPU単体 (Platform.MCU_CPU) の場合:**

```c
void compute_sub_0000(
    void* input_buffer,
    size_t input_size,
    void* output_buffer,
    size_t output_size
);
```

### 6.4 アプリケーションコードとの統合

参考: 顔認識サンプル (`face_detection/src/ai_application/`)

```c
// wrapper.h - 生成されたMERA APIの薄いラッパー
static inline uint8_t* mera_input_ptr() {
    return (uint8_t*) GetModelInputPtr_image_input();
}
static inline uint8_t* mera_output1_ptr() {
    return (uint8_t*) GetModelOutputPtr_Identity_XXXXX();
}
static inline uint8_t* mera_output2_ptr() {
    return (uint8_t*) GetModelOutputPtr_Identity_1_XXXXX();
}
static inline void mera_invoke() { RunModel(false); }

// MainLoop - 推論実行
memcpy(mera_input_ptr(), preprocessed_image, 192*192*1);
mera_invoke();

// 出力デコード (量子化パラメータを使用)
int8_t* output0 = (int8_t*)mera_output1_ptr();  // 6x6ブランチ
int8_t* output1 = (int8_t*)mera_output2_ptr();  // 12x12ブランチ
// 逆量子化: float_val = (int8_val - zero_point) * scale
```

---

## 7. 精度評価

### 7.1 量子化前評価 (FP32)

**Nota-NetsPresso (パスB) の場合:**
```bash
python val.py \
    --weights runs/train/fall_detection_v1/weights/best.pt \
    --data dataset/fall_detection/data.yaml \
    --img-size 192 \
    --task val
```

**Darknet (パスA) の場合:**
```bash
./darknet detector map fall_detection.data \
    yolo-fastest-1.1-fall.cfg \
    backup/yolo-fastest-1.1-fall_best.weights
```

### 7.2 量子化後評価 (INT8)

提供する評価スクリプトを使用する:
```bash
# INT8モデルの構造検査
python dataset/scripts/evaluate_model.py \
    --inspect model_int8.tflite

# FP32 vs INT8のサイズ比較
python dataset/scripts/evaluate_model.py \
    --compare model_fp32.tflite model_int8.tflite

# 検証データセットでの検出メトリクス評価
python dataset/scripts/evaluate_model.py \
    --eval --model model_int8.tflite \
    --data dataset/fall_detection/val \
    --img-size 192 --conf 0.5 --iou 0.45
```

### 7.3 期待される品質メトリクス

| メトリクス | 目標 | 顔認識ベースライン |
|---|---|---|
| mAP@0.5 (FP32) | >= 0.90 | N/A |
| mAP@0.5 (INT8) | >= 0.85 | N/A |
| PSNR (量子化品質) | >= 28 dB | RUHMIで検証済み |
| INT8精度劣化 | FP32から5%以内 | YOLO-Fastestで典型的 |

### 7.4 量子化品質の検証

RUHMI mcu_quantize.py は量子化品質を自動検証する:

1. **PSNRチェック**: 量子化モデル出力の PSNR >= 5 dB (最低閾値)
2. **C99コード検証**: 生成コードの出力がMERAインタプリタの出力と一致
   - MSE <= 0.1 または PSNR >= 28 dB

チェックが失敗した場合の対処:
- キャリブレーションサンプル数を増やす (`--calib_num 100`)
- ランダムデータではなく実画像をキャリブレーションに使用
- INT8量子化に適さないレイヤがないか確認

---

## 8. モデルサイズの確認

### 8.1 サイズバジェット

| コンポーネント | バジェット | 備考 |
|---|---|---|
| NPUアリーナ (SRAM) | 432KB (442,368バイト) | sub_0000_tensors.c から |
| モデル重み (Flash) | 約2MB利用可能 | Flashメモリに格納 |
| 入力バッファ | 36,864バイト (192x192x1) | Grayscale画像 |
| 出力0 (6x6) | 648バイト (6x6x3x6) | 3アンカー、各6値 |
| 出力1 (12x12) | 2,592バイト (12x12x3x6) | 3アンカー、各6値 |

### 8.2 顔認識ベースラインの参考値

顔認識モデル (yolo-fastest-192_face_v4.tflite) の実績値:

| 項目 | 値 |
|---|---|
| 推論時間 | Ethos-U55で約3ms |
| モデルファイルサイズ | 約400KB (INT8) |
| 出力0 scale/zp | 0.13408391 / 47 |
| 出力1 scale/zp | 0.18535925 / 10 |
| アンカー (Branch 0) | [38, 77, 47, 97, 61, 126] |
| アンカー (Branch 1) | [14, 26, 19, 37, 28, 55] |

転倒検出モデル (人物検出) は同一アーキテクチャを使用するため、同等のサイズ特性が期待される。

---

## 9. 提供スクリプト

### 9.1 dataset/scripts/train_darknet.sh

PyTorch (パスB) とDarknet (パスA) の両方に対応する学習起動スクリプト。

```bash
# PyTorch学習 (推奨)
bash dataset/scripts/train_darknet.sh pytorch 192 300 64

# Darknet学習
bash dataset/scripts/train_darknet.sh darknet 192
```

### 9.2 dataset/scripts/convert_to_tflite.py

モデル変換スクリプト: ONNX → TFLite (FP32/INT8)。

```bash
# ONNX → TFLite FP32変換
python dataset/scripts/convert_to_tflite.py \
    --onnx model.onnx --output model_fp32.tflite

# INT8量子化付き変換
python dataset/scripts/convert_to_tflite.py \
    --quantize --input model_fp32.tflite --output model_int8.tflite

# モデル検査
python dataset/scripts/convert_to_tflite.py \
    --inspect --input model_int8.tflite
```

### 9.3 dataset/scripts/evaluate_model.py

モデル評価・検査ツール。

```bash
# モデル構造、量子化パラメータ、サイズの検査
python dataset/scripts/evaluate_model.py --inspect model_int8.tflite

# FP32 vs INT8モデルサイズ比較
python dataset/scripts/evaluate_model.py \
    --compare model_fp32.tflite model_int8.tflite

# 検出メトリクス評価 (簡易版)
python dataset/scripts/evaluate_model.py \
    --eval --model model_int8.tflite \
    --data dataset/fall_detection/val
```

---

## 10. End-to-Endワークフロー

```
Step 1: データセット準備 (F-003-2)
  dataset/scripts/download_coco_person.py
  dataset/scripts/download_roboflow_fall.py
  dataset/scripts/merge_and_split.py
  dataset/scripts/augment_offline.py
       |
       v
Step 2: モデル学習 (本レポート)
  [パスB] python train.py --data data.yaml --cfg yolo-fastest-1.1.cfg
       |
       v
Step 3: ONNXエクスポート
  python export.py --weights best.pt --img-size 192
       |
       v
Step 4: TFLite FP32変換
  python dataset/scripts/convert_to_tflite.py --onnx best.onnx
       |
       v
Step 5: INT8量子化 + MCUコード生成
  python scripts/mcu_quantize.py models_fp32 deploy_out --ethos
  [代替: python scripts/mcu_deploy.py models_int8 deploy_out --ethos]
       |
       v
Step 6: 精度評価
  python dataset/scripts/evaluate_model.py --inspect model_int8.tflite
  python dataset/scripts/evaluate_model.py --eval --model model_int8.tflite --data val/
       |
       v
Step 7: 生成C99コードをe2studioプロジェクトに統合
  deploy_out/<model>/build/MCU/compilation/src/* を
  e2studio_CPU0/src/ai_application/mera/ にコピー
```

---

## 11. トラブルシューティング

### 11.1 よくある問題と対処

| 問題 | 対処方法 |
|---|---|
| 量子化後PSNRが低い | キャリブレーションサンプル数を増やす; 実画像を使用 |
| モデルがアリーナに収まらない | 入力サイズを160x160に縮小; チャンネル数をプルーニング |
| ONNX→TFLite変換失敗 | 未サポートオペレータの確認; ONNXグラフの簡略化 |
| Velaコンパイルエラー | docs/operator_support.md でサポート状況を確認 |
| C99コード検証不一致 | より多くのキャリブレーションデータで再量子化 |
| 検出精度が低い | 学習をより長く; データセットを拡張; アンカーを調整 |

### 11.2 RUHMIオペレータサポート

`reference_projects/ruhmi-framework-mcu/docs/operator_support.md` にサポートされるTFLite/ONNXオペレータの完全リストがある。YOLO-Fastestで使用される主要オペレータ:

- Conv2D (A8W8)
- DepthwiseConv2D (A8W8)
- Add, Mul (A8/F32)
- MaxPool2D, AveragePool2D
- Reshape, Concatenation
- LeakyRelu, Relu

---

## 12. 未解決事項とリスク

| 項目 | 状態 | リスク | 対策 |
|---|---|---|---|
| personクラスのアンカー再計算 | 未実施 | 中 | personデータセットでcalc_anchorsを実行 |
| Grayscale vs RGB学習性能 | 未検証 | 中 | 両方で比較; 必要に応じてRGBにフォールバック |
| INT8量子化精度劣化 | 未検証 | 低 | YOLO-Fastestは良好な量子化特性を持つ |
| RUHMI/MERA SDKの利用可否 | ライセンス必要 | 高 | Renesasに評価ライセンスを問い合わせ |
| Nota-NetsPressoリポジトリの互換性 | 未検証 | 中 | 最新バージョンでテスト |
| 実データによるキャリブレーション | 未実施 | 低 | mcu_quantize.pyのgenerate_input_data()を修正 |

---

## 13. 参考資料

1. [YOLO-Fastest (Darknet)](https://github.com/dog-qiuqiu/Yolo-Fastest)
2. [Nota-NetsPresso YOLO-Fastest for ARM U55/M85](https://github.com/Nota-NetsPresso/ModelZoo-YOLOFastest-for-ARM-U55-M85)
3. [RUHMI Framework MCU](../../reference_projects/ruhmi-framework-mcu/)
4. [RUHMI Models Tested](../../reference_projects/ruhmi-framework-mcu/docs/models_tested.md)
5. [RUHMI Operator Support](../../reference_projects/ruhmi-framework-mcu/docs/operator_support.md)
6. [RUHMI Runtime API](../../reference_projects/ruhmi-framework-mcu/docs/runtime_api.md)
7. [顔認識サンプル](../../reference_projects/ruhmi-framework-mcu/application_examples/face_detection/)
8. [F-003-1: 転倒検出AIモデル調査・選定レポート](f003_01_model_selection_report.md)
9. [F-003-2: 転倒検出AIモデルの学習データセット準備レポート](f003_02_dataset_preparation_report.md)

---

## 改訂履歴

| 日付 | バージョン | 変更内容 | 作成者 |
|------|-----------|---------|--------|
| 2026-03-04 | 1.0 | 初版作成 | Claude Code |
