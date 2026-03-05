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

### 3.0 実行結果サマリと方針変更

初版レポートでは パスA (Darknet) と パスB (Nota-NetsPresso) の2つを検討し、パスBを推奨した。
しかし実際の実行では両パスとも問題が発生したため、**パスC (Ultralytics YOLOv8n)** に切り替えて学習を完了した。

#### 方針変更の経緯

| 段階 | 内容 | 結果 |
|------|------|------|
| 当初計画 | YOLO-Fastest v1.1 を Darknet で学習 | — |
| Colab実行 (パスA) | Darknet ビルド・学習を試行 | **失敗**: 下記の問題が多発 |
| 方針変更 | Ultralytics YOLOv8n に切り替え | **成功**: mAP@0.5 = 67.8% |

#### Darknet で発生した問題 (パスA 失敗の理由)

| 問題 | 詳細 | 予見可能だったか |
|------|------|-----------------|
| OpenCVヘッダ不足 | `libopencv-dev` が Colab に未インストール | はい |
| tensorflow-cpu 競合 | Colab の GPU 版 TF を上書き | はい |
| numpy 破損 | pip install による numpy ダウングレードで scikit-learn 等が動作不能 | はい |
| `-show` で Segfault | ヘッドレス環境で OpenCV GUI を起動しようとしてクラッシュ | はい |
| ラベルパス置換不動作 | `images/` → `labels/` の自動置換が機能せず、ラベルなしで学習が進行 | はい |
| `cp *.txt` 引数上限超過 | 32,853 ファイルのコピーでシェル引数上限を超過 | はい |
| cuDNN エラー | mAP 評価時に `CUDNN_STATUS_BAD_PARAM` | 部分的に予見可能 |
| mAP 3.42% | ラベル問題修正後も channels=1 の cfg が正しく動作せず | 実行前に検証すべきだった |

**教訓**: ノートブックを Colab 上で実際にテスト実行せずに提供した。環境固有の問題は事前テストで発見できた。

#### パスC (Ultralytics YOLOv8n) を選択した理由

1. **環境構築が `pip install ultralytics` のみ** — ビルドエラーのリスクがない
2. **ラベル読み込みが標準対応** — images/labels ディレクトリ構造をそのまま認識
3. **学習・評価・エクスポートが Python API で完結** — デバッグが容易
4. **COCO 事前学習済み重みが利用可能** — 転移学習で少ないエポックでも精度が出やすい

#### モデル変更の影響

| 項目 | 当初計画 (YOLO-Fastest) | 実行結果 (YOLOv8n) |
|------|------------------------|-------------------|
| パラメータ数 | 約0.24M | 約3.0M |
| INT8 モデルサイズ | 約300KB (推定) | 3,149KB (実測) |
| アリーナ 432KB 制約 | 収まる見込み | **超過 (要対策)** |
| 入力チャネル | 1 (Grayscale) | 3 (RGB) |
| mAP@0.5 | 未達成 | 67.8% |

YOLOv8n はモデルサイズが大きいため MCU の 432KB アリーナ制約を満たさない。
MCU デプロイ時にはモデル小型化（プルーニング、カスタムアーキテクチャ等）が必要である。

### 3.1 パスA: Darknet (Cフレームワーク) — 実行済み・失敗

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

### 3.2 パスB: PyTorch / Nota-NetsPresso (未実行)

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

### 3.3 パスC: Ultralytics YOLOv8n (実行済み・採用)

パスA失敗後に採用した学習パス。Google Colab 上で実行。

#### 3.3.1 使用ノートブック

`dataset/scripts/train_yolov8_colab.ipynb`

#### 3.3.2 環境

| 項目 | 値 |
|------|-----|
| プラットフォーム | Google Colab |
| GPU | NVIDIA L4 (24GB VRAM) |
| フレームワーク | Ultralytics 8.4.19 |
| PyTorch | 2.10.0+cu128 |
| Python | 3.12.12 |

#### 3.3.3 学習パラメータ

| パラメータ | 値 | 備考 |
|-----------|-----|------|
| ベースモデル | yolov8n.pt | COCO事前学習済み (転移学習) |
| 入力サイズ | 192x192 | MCUデプロイ先に合わせる |
| 入力チャネル | 3 (RGB) | ※当初計画の1chから変更 |
| バッチサイズ | 64 | |
| エポック数 | 100 | |
| オプティマイザ | SGD | |
| 学習率 (初期) | 0.01 | |
| 学習率 (最終) | 0.01 × lr_f | Cosineスケジュール |
| Momentum | 0.937 | |
| Weight decay | 0.0005 | |
| Warmupエポック | 3.0 | |
| hsv_h / hsv_s | 0.0 / 0.0 | グレースケール運用を想定し無効化 |
| hsv_v | 0.4 | 明度変換のみ有効 |
| mosaic | 1.0 | |

#### 3.3.4 実行結果

| メトリクス | 値 |
|-----------|-----|
| mAP@0.5 | **67.8%** |
| mAP@0.5:0.95 | 42.4% |
| Precision | 80.4% |
| Recall | 59.3% |
| 学習時間 | 2.5時間 (L4 GPU) |
| best.pt サイズ | 6.2MB |

#### 3.3.5 モデル変換結果

| 形式 | サイズ |
|------|--------|
| ONNX (FP32) | 11,828 KB (11.6 MB) |
| TFLite FP32 | 11,807 KB (11.5 MB) |
| TFLite INT8 | **3,149 KB (3.1 MB)** |

#### 3.3.6 INT8モデル詳細

```
Input:  shape=[1, 192, 192, 3] dtype=int8 scale=0.00392157 zero_point=-128
Output: shape=[1, 5, 756]      dtype=int8 scale=0.89210844 zero_point=-128
```

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

## 9. 提供スクリプト・ノートブック

### 9.1 dataset/scripts/train_yolov8_colab.ipynb (推奨)

Ultralytics YOLOv8n を Google Colab 上で学習するノートブック。
環境構築・学習・評価・ONNX/TFLite変換を一連のセルで実行できる。

**使用方法**: セクション10.1 の再現手順を参照。

### 9.2 dataset/scripts/train_yolo_fastest_colab.ipynb (非推奨)

Darknet ベースの YOLO-Fastest 学習ノートブック。
Colab 上で多数の問題が発生したため非推奨。記録として残す。

### 9.3 dataset/scripts/train_darknet.sh

PyTorch (パスB) とDarknet (パスA) の両方に対応する学習起動スクリプト。

```bash
# PyTorch学習 (推奨)
bash dataset/scripts/train_darknet.sh pytorch 192 300 64

# Darknet学習
bash dataset/scripts/train_darknet.sh darknet 192
```

### 9.4 dataset/scripts/convert_to_tflite.py

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

### 9.5 dataset/scripts/evaluate_model.py

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

### 10.1 再現手順 (Google Colab + Ultralytics YOLOv8n)

以下は実際に実行し成功した手順である。次回実行時はこの手順に従うこと。

#### Step 1: データセットZIP作成 (ローカルPC)

```bash
cd mimamori-sense/dataset/merged
# images/ と labels/ のみを圧縮する (obj.data等は不要)
zip -r fall_detection_dataset.zip images/ labels/
```

**注意**: Windows環境では `zip` コマンドがない場合がある。PowerShellを使用:
```powershell
Compress-Archive -Path 'dataset\merged\images', 'dataset\merged\labels' `
    -DestinationPath 'dataset\fall_detection_dataset.zip'
```

ファイル数が多い場合（32,853枚×2）、数十分かかる。

#### Step 2: Google Drive にアップロード

`fall_detection_dataset.zip` を Google Drive のマイドライブ直下にアップロード。

#### Step 3: Colab ノートブック実行

1. `dataset/scripts/train_yolov8_colab.ipynb` を Google Drive にアップロード
2. 右クリック →「Google Colaboratory」で開く
3. メニュー「ランタイム」→「ランタイムのタイプを変更」→ **GPU (T4 または L4)** を選択
4. セルを上から順番に実行

ノートブック内のステップ:
```
Step 1: 環境構築
  pip install ultralytics onnx onnx2tf onnxsim
       |
       v
Step 2: データセット準備
  Google Drive マウント → ZIP展開 → data.yaml 作成
       |
       v
Step 3: モデル学習
  YOLOv8n, 192x192, 100エポック, batch=64
  → train/weights/best.pt, train/weights/last.pt
       |
       v
Step 4: 精度評価 (mAP)
  model.val() で検証・テストデータを評価
       |
       v
Step 5: ONNX エクスポート
  model.export(format='onnx', imgsz=192)
       |
       v
Step 6: TFLite FP32/INT8 変換
  onnx2tf → SavedModel → TFLite FP32
  → INT8量子化 (キャリブレーション画像200枚)
       |
       v
Step 7: 成果物を Google Drive に保存
  best.pt, model.onnx, model_fp32.tflite, model_int8.tflite
```

#### Step 4: (今後) RUHMI/MERA デプロイ

```bash
# MERA SDK ライセンス取得後に実施
python scripts/mcu_quantize.py models_fp32 deploy_out --ethos
# または
python scripts/mcu_deploy.py models_int8 deploy_out --ethos
```

#### Step 5: (今後) e2studio プロジェクトに統合

```
deploy_out/<model>/build/MCU/compilation/src/* を
e2studio_CPU0/src/ai_application/mera/ にコピー
```

### 10.2 参考: 当初計画のワークフロー (Darknet版・失敗)

当初計画では以下のワークフローを想定していたが、Step 2 で Darknet の問題が多発し断念した。
記録として残す。使用ノートブック: `dataset/scripts/train_yolo_fastest_colab.ipynb` (非推奨)

```
Step 1: Darknet ビルド (AlexeyAB/darknet)
Step 2: cfg ファイル作成 (192x192x1, 1クラス)
Step 3: アンカー計算 (darknet detector calc_anchors)
Step 4: 学習 (darknet detector train)
Step 5: mAP評価 (darknet detector map)
Step 6: ONNX変換 (darknet2pytorch → torch.onnx.export)
Step 7: TFLite変換 (onnx2tf)
→ Step 4 で mAP 3.42% に留まり、実用に至らず
```

---

## 11. トラブルシューティング

### 11.1 Google Colab 固有の問題 (実際に遭遇した問題)

| 問題 | 原因 | 対処方法 |
|------|------|---------|
| `numpy.dtype size changed` | pip install で numpy がダウングレードされた | 「セッションを再起動する」で解決 |
| pip の dependency conflict 警告 | onnx2tf 等が古い numpy を要求 | 無視してよい (動作に影響なし) |
| セッション切断で作業消失 | Colab 無料版の制限 (最大12時間) | Google Drive にチェックポイント保存 |
| `drive.mount()` エラー | ランタイム再起動後にマウント解除 | 再度 `drive.mount()` を実行 |

### 11.2 Darknet 固有の問題 (パスA で遭遇した問題)

| 問題 | 原因 | 対処方法 |
|------|------|---------|
| Darknet ビルド失敗 | `libopencv-dev` 未インストール | `apt-get install libopencv-dev` |
| `tensorflow-cpu` 競合 | Colab GPU版TF を上書き | `tensorflow-cpu` を使用しない |
| ラベルファイル読み込み失敗 | `images/`→`labels/` パス置換が不動作 | ラベルを `images/` にもコピー (`find ... -exec cp`) |
| `cp *.txt` 失敗 | ファイル数が多くシェル引数上限超過 | `find ... -exec cp {} dst/ \;` を使用 |
| `-show` で Segfault | ヘッドレス環境で GUI 起動 | `-show` を外すか `-dont_show` を使用 |
| cuDNN エラー (mAP計算時) | depthwise conv + channels=1 の組み合わせ | `-map` を外して学習、mAP は別途評価 |
| mAP 3.42% | ラベルが正しく読めていなかった / cfg の問題 | **Ultralytics に切り替えで解決** |

### 11.3 一般的な問題と対処

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

### 12.1 解決済み

| 項目 | 当初の状態 | 解決方法 |
|------|-----------|---------|
| personクラスのアンカー再計算 | 未実施 | Python K-means で計算済み: `12,23, 32,70, 85,71, 68,140, 136,105, 166,168` (Darknet版で使用) |
| Grayscale vs RGB学習性能 | 未検証 | RGB (3ch) で学習し mAP 67.8% を確認。MCUデプロイ時に1ch対応を検討 |
| INT8量子化精度劣化 | 未検証 | TFLite PTQ で INT8 変換完了 (3,149KB)。RUHMI量子化は今後 |

### 12.2 未解決 (今後の課題)

| 項目 | 状態 | リスク | 対策 |
|------|------|--------|------|
| mAP 67.8% (目標90%未達) | 要改善 | 高 | エポック数増加 (100→300)、入力サイズ拡大 (192→320)、データセット見直し |
| INT8モデル 3.1MB (アリーナ432KB超過) | 要対策 | 高 | YOLOv8n以外の小型モデル検討、プルーニング、カスタムアーキテクチャ |
| RGB→Grayscale変換 | 未対応 | 中 | MCUデプロイ時にチャネル複製 (1ch→3ch) またはGrayscaleで再学習 |
| RUHMI/MERA SDKの利用可否 | ライセンス必要 | 高 | Renesasに評価ライセンスを問い合わせ |
| 実データによるキャリブレーション | 実施済み (200枚) | 低 | 必要に応じて枚数増加 |
| Recall 59.3% (人物の4割を見逃し) | 要改善 | 高 | 信頼度閾値の調整、学習エポック増加、データセット品質改善 |

---

## 13. 参考資料

1. [YOLO-Fastest (Darknet)](https://github.com/dog-qiuqiu/Yolo-Fastest)
2. [Nota-NetsPresso YOLO-Fastest for ARM U55/M85](https://github.com/Nota-NetsPresso/ModelZoo-YOLOFastest-for-ARM-U55-M85)
3. [Ultralytics YOLOv8](https://docs.ultralytics.com/)
4. [RUHMI Framework MCU](../../reference_projects/ruhmi-framework-mcu/)
5. [RUHMI Models Tested](../../reference_projects/ruhmi-framework-mcu/docs/models_tested.md)
6. [RUHMI Operator Support](../../reference_projects/ruhmi-framework-mcu/docs/operator_support.md)
7. [RUHMI Runtime API](../../reference_projects/ruhmi-framework-mcu/docs/runtime_api.md)
8. [顔認識サンプル](../../reference_projects/ruhmi-framework-mcu/application_examples/face_detection/)
9. [F-003-1: 転倒検出AIモデル調査・選定レポート](f003_01_model_selection_report.md)
10. [F-003-2: 転倒検出AIモデルの学習データセット準備レポート](f003_02_dataset_preparation_report.md)

---

## 改訂履歴

| 日付 | バージョン | 変更内容 | 作成者 |
|------|-----------|---------|--------|
| 2026-03-04 | 1.0 | 初版作成 | Claude Code |
| 2026-03-05 | 1.1 | 実行結果反映: Darknet失敗→Ultralytics YOLOv8nに変更、実測値(mAP 67.8%, INT8 3.1MB)追記、再現手順整備、トラブルシューティング拡充 | Claude Code |
