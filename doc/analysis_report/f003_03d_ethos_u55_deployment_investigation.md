# F-003-3d: RUHMI/MERA SDKライセンス確認とEthos-U55向けモデル変換検証 調査レポート

Issue: #104
調査日: 2026-03-07
ステータス: 調査完了（ライセンス取得/変換実行はユーザー作業）

---

## 1. MERA SDK / RUHMI Frameworkの調査

### 1.1 概要（確認済み事実）

RUHMI (Renesas Unified Heterogeneous Model Integration) Frameworkは、Renesasが提供するAIモデル最適化・デプロイツールチェーンである。内部的にEdgeCortix社のMERAコンパイラを利用している。

- **正式名称**: RUHMI Framework (MCU向け)
- **開発元**: Renesas Electronics Corporation + EdgeCortix Inc.
- **用途**: AIモデル（TFLite/ONNX/PyTorch）をMCU向けCソースコードに変換
- **ターゲット**: Renesas RA8シリーズ MCU (Cortex-M85 + Ethos-U55)

### 1.2 ライセンス形態（確認済み事実）

| 項目 | 内容 |
|------|------|
| **ライセンス** | Apache License, Version 2.0 |
| **費用** | **無償**（オープンソース） |
| **MERA IPライセンス** | EdgeCortix Inc. からRenesas Electronics Corporationにサブライセンス権付きで供与。Apache 2.0 |
| **ソースコード公開** | GitHubリポジトリで公開済み |

ライセンスファイル（`LICENSE.md`）の記載:
> Copyright 2025 EdgeCortix Inc. -- Licensed to Renesas Electronics Corporation, with the right to sublicense under the Apache License 2.0.
> Copyright 2025 Renesas Electronics Corporation and its contributors. Licensed under the Apache License, Version 2.0.

**結論: 個別のライセンス契約や問い合わせは不要。Apache 2.0オープンソースとして自由に利用可能。**

### 1.3 入手方法（確認済み事実）

**GitHubリポジトリ**: https://github.com/renesas/ruhmi-framework-mcu

```bash
git clone https://github.com/renesas/ruhmi-framework-mcu.git
```

**MERAパッケージ**: リポジトリの `install/` ディレクトリにwhlファイルが同梱されている。

```bash
pip install ./install/mera-2.5.0+pkg.3577-cp310-cp310-manylinux_2_27_x86_64.whl
```

- PyPI上にも `mera` パッケージが公開されているが、RUHMI MCU向けにはリポジトリ同梱版を使用する。
- **評価版の概念はない**（Apache 2.0で全機能が利用可能）。

### 1.4 動作環境要件（確認済み事実）

| 要件 | 内容 |
|------|------|
| **Python** | 3.10.x（PyEnv or venvで構築） |
| **OS (Linux)** | Ubuntu 22.04 推奨 |
| **OS (Windows)** | Windows 10 or 11（11推奨）、Microsoft Visual C++ 2015-2022 Runtime必要 |
| **FSP** | 6.0.0以上 + CMSIS-NN 7.0.0 |

### 1.5 RUHMIの生成物の構造（確認済み事実）

RUHMI Frameworkは TFLite INT8モデルを入力として、以下のCソースコードを自動生成する。

Ethos-U55有効化時の生成ファイル:

| ファイル | 役割 |
|---------|------|
| `model.c / .h` | `RunModel()`関数、入出力ポインタ取得関数 |
| `model_io_data.c / .h` | モデル入出力バッファ定義 |
| `sub_XXXX_command_stream.c / .h` | NPUコマンドストリーム |
| `sub_XXXX_invoke.c / .h` | NPU推論実行関数 |
| `sub_XXXX_model_data.c / .h` | モデル重みパラメータ |
| `sub_XXXX_tensors.c / .h` | テンソルアドレス定義（Arenaサイズ含む） |
| `ethosu_common.h` | Ethos-U共通定義 |

Runtime API:
```c
// 入力データ設定
memcpy(GetModelInputPtr_<input_name>(), input_data, input_size);

// 推論実行
RunModel(false);

// 出力データ取得
int8_t* output = GetModelOutputPtr_<output_name>();
```

### 1.6 変換コマンド（確認済み事実）

#### 既にINT8量子化済みのTFLiteモデルの場合（本プロジェクトに該当）

```bash
cd scripts/
python mcu_deploy.py --ethos --ref_data ../models_int8 deploy_ethos
```

#### FP32モデルから量子化+デプロイする場合

```bash
cd scripts/
python mcu_quantize.py -e ../models_fp32 deploy_ethos
```

### 1.7 Vela設定パラメータ（確認済み事実）

`mcu_deploy.py` 内のVela設定:

```python
vela_config = {
    'enable_ospi': with_ospi,       # OSPIフラッシュメモリ使用
    'sys_config': 'RA8P1',          # ターゲットシステム
    'memory_mode': 'Sram_Only',     # メモリモード
    'accel_config': 'ethos-u55-256', # Ethos-U55 256 MAC構成
    'optimise': 'Performance',       # 最適化方針
}
```

### 1.8 オペレータサポート状況（確認済み事実）

YOLOv8で使用される主要オペレータのRUHMI MCU_ETHOS対応:

| オペレータ | TFLite名 | MCU_ETHOS量子化 | C99コード生成 | 判定 |
|-----------|---------|----------------|-------------|------|
| Conv2D | tfl.conv_2d | A8W8 | 対応 | OK |
| DepthwiseConv2D | tfl.depthwise_conv_2d | (記載なし、Conv2Dに含む) | (記載なし) | 要確認 |
| MaxPool2D | tfl.max_pool_2d | A8 | 対応 | OK |
| Concatenation | tfl.concatenation | A8 | 対応 | OK (4次元まで) |
| Reshape | tfl.reshape | A8 | 対応 | OK |
| Add | tfl.add | A8 | 対応 | OK |
| Mul | tfl.mul | A8 | 対応 | OK |
| Sigmoid | tfl.logistic | A8 | 対応 | OK |
| Resize (Upsample) | tfl.resize_nearest_neighbor | A8 | 対応 | OK |
| Transpose | tfl.transpose | A8 | 対応 | OK |
| Slice | tfl.slice | A8 | 対応 | OK |
| Pad | tfl.pad | A8 | 対応 | OK |
| Relu/Relu6 | tfl.relu / tfl.relu6 | A8 | 対応 | OK |
| HardSwish (SiLU) | tfl.hard_swish | A8 | 対応 | OK |

**注意**: `tfl.concatenation` は4次元入力までの制限がある。YOLOv8の出力結合部分で5次元以上が使われている場合は問題になる可能性がある。

---

## 2. Arm Vela Compilerの調査

### 2.1 概要（確認済み事実）

Arm Ethos-U Vela Compilerは、Armが提供するオープンソースのMLモデルコンパイラである。TFLite/TOSAモデルをEthos-U NPU向けに最適化する。

**重要な発見**: RUHMI Framework自体が内部でVelaコンパイラを使用している。`mcu_deploy.py`の `vela_config` パラメータがそれを示している。つまり、RUHMIはVelaのラッパー＋Cコード生成器として機能している。

### 2.2 インストール方法（確認済み事実）

```bash
pip3 install ethos-u-vela
```

- **最新バージョン**: 5.0.0 (2026年3月3日リリース)
- **Python要件**: 3.9以上
- **対応OS**: Linux, macOS, Windows
- **ライセンス**: Apache 2.0
- **公式リポジトリ**: https://review.mlplatform.org/plugins/gitiles/ml/ethos-u/ethos-u-vela
- **PyPI**: https://pypi.org/project/ethos-u-vela/

### 2.3 単体Velaでの変換手順（確認済み事実）

```bash
# TFLite INT8モデルをEthos-U55向けに最適化
vela \
  --accelerator-config ethos-u55-256 \
  --system-config Ethos_U55_High_End_Embedded \
  --memory-mode Sram_Only \
  --output-dir output/ \
  model_int8.tflite
```

出力: `output/model_int8_vela.tflite` （Ethos-U55カスタムオペレータを含むTFLiteモデル）

### 2.4 Vela単体 vs RUHMI比較（確認済み事実+推測）

| 項目 | Vela単体 | RUHMI Framework |
|------|----------|-----------------|
| **出力形式** | Vela最適化TFLite | Cソースコード (.c/.h) |
| **RA8P1対応** | 汎用（設定で対応可能） | RA8P1専用プリセットあり |
| **Cコード生成** | なし（別途TFLMが必要） | 自動生成 |
| **ランタイム** | TFLite Micro + Ethos-Uドライバ | 生成コード内蔵（TFLM不要） |
| **顔認識サンプルとの互換性** | 低（wrapper.hの構造が異なる） | 高（同じAPI構造） |

**推測**: Vela単体で変換したモデルをRA8P1で使うには、TFLite Micro + Ethos-Uデリゲートのランタイム環境を自前で構築する必要があり、RUHMI顔認識サンプルの `mera/` ディレクトリ構造とは互換性がない。RUHMIを使った方が、既存のface_detectionサンプルのパイプラインに直接組み込める。

### 2.5 Vela単体を選択する場合の注意事項（推測）

- RUHMI自動生成コードの代わりに、TFLite Micro (TFLM) ランタイムをRA8P1に移植する必要がある
- Ethos-Uデリゲートの設定が必要
- 顔認識サンプルの `wrapper.h` / `mera/` ディレクトリの構造を大幅に書き換える必要がある
- RUHMI経由の方が圧倒的に楽であるため、Vela単体は推奨しない

---

## 3. TensorFlow Lite Micro (TFLM) の調査

### 3.1 概要（確認済み事実）

TFLMはTensorFlowの組み込み向け推論エンジンで、マイクロコントローラ上でTFLiteモデルを実行できる。

### 3.2 RA8P1での利用状況（確認済み事実）

RUHMI顔認識サンプルには既にTFLMが組み込まれている:
```
ra/npu/tflite-micro/    # TensorFlow Lite Micro
ra/npu/flatbuffers/     # FlatBuffersシリアライザ
ra/npu/gemmlowp/        # 低精度行列乗算
ra/npu/ruy/             # 行列乗算ライブラリ
```

ただし、RUHMI生成コード経由で使用されており、TFLMを直接呼び出すのではなく、RUHMI生成の `RunModel()` -> `sub_XXXX_invoke()` -> Ethos-U55ドライバの経路で推論している。

### 3.3 CPU専用推論（Ethos-U55なし）の可能性（確認済み事実+推測）

RUHMI Frameworkは **CPU only** デプロイにも対応している:

```bash
python mcu_deploy.py --ref_data ../models_int8 deploy_cpu_only
```

`--ethos` フラグを外すことで、全オペレータをCPU (Cortex-M85) で実行するCコードが生成される。CMSIS-NN最適化カーネルを使用する。

### 3.4 CPU専用推論時の速度見積もり（推測・未確認）

以下は推測であり、実測値ではない。

**前提条件**:
- Cortex-M85 @ 1GHz（Helium/MVE拡張あり）
- CMSIS-NN最適化カーネル使用
- YOLOv8n (3.0Mパラメータ, 192x192入力, INT8)

**推測根拠**:
- 顔認識 YOLO-Fastest (0.24Mパラメータ) がEthos-U55で約3ms
- Ethos-U55はINT8推論に特化したNPUで、CPU比で10-50倍高速（一般論）
- YOLOv8nは YOLO-Fastest の約12.5倍のパラメータ数

**推定速度**:

| シナリオ | モデル | 推定推論時間 | 根拠 |
|---------|--------|------------|------|
| Ethos-U55 + YOLO-Fastest | 0.24M params | 約3ms | 実測値（Renesasサンプル） |
| Ethos-U55 + YOLOv8n | 3.0M params | 15-40ms | パラメータ比から推測。KPI 5msを超過する可能性大 |
| Ethos-U55 + YOLOv8 pico | 0.1-0.3M params | 3-10ms | YOLO-Fastest同等規模ならKPI達成の可能性 |
| CPU only + YOLOv8n | 3.0M params | 300-2000ms | CPU推論はNPU比10-50倍遅い。KPIは未達成 |
| CPU only + YOLOv8 pico | 0.1-0.3M params | 30-150ms | 小型化してもKPI 5msは未達成 |

**結論（推測）**: KPI 5ms以内を達成するにはEthos-U55の利用が必須。CPU専用推論は現実的ではない。また、YOLOv8nの3.0Mパラメータはそのままでは推論時間KPIを達成できない可能性が高く、Issue #102のモデル小型化（YOLOv8 pico）との連携が重要。

---

## 4. デプロイパスの比較と推奨

### 4.1 選択肢の比較

| 選択肢 | 実現性 | 開発工数 | 顔認識サンプル互換性 | 推奨度 |
|--------|--------|---------|-------------------|--------|
| **A: RUHMI Framework** | 高 | 小 | 高（同じAPI構造） | **推奨** |
| B: Vela + TFLM自前構築 | 中 | 大 | 低（大幅書き換え） | 非推奨 |
| C: CPU only (RUHMI) | 高 | 小 | 高 | 速度不足 |
| D: CPU only (TFLM直接) | 中 | 大 | 低 | 速度不足 |

### 4.2 推奨デプロイパス

```
学習済みモデル (PyTorch/Ultralytics)
    |
    v
TFLite INT8 エクスポート (Ultralytics export)
    |
    v
RUHMI Framework (mcu_deploy.py --ethos)
    |  内部でArm Vela Compilerを呼び出し
    |  Ethos-U55コマンドストリーム生成
    |  Cソースコード自動生成
    v
生成されたCソースコード (model.c, sub_XXXX_*.c 等)
    |
    v
e2 studio プロジェクトに組み込み
    |  顔認識サンプルのmera/ディレクトリを差し替え
    |  wrapper.h / ai_application_config.h を更新
    v
RA8P1 (Cortex-M85 + Ethos-U55) で推論実行
```

---

## 5. 変換検証の手順書（ユーザー実施用）

### 5.1 環境構築

```bash
# 1. リポジトリクローン
git clone https://github.com/renesas/ruhmi-framework-mcu.git
cd ruhmi-framework-mcu

# 2. Python 3.10仮想環境の作成
python3.10 -m venv mera-env

# Linux
source mera-env/bin/activate
# Windows
mera-env\Scripts\activate

# 3. 依存パッケージインストール
pip install --upgrade pip
pip install decorator typing_extensions psutil attrs pybind11 cmake junitparser

# 4. MERAインストール（リポジトリ同梱whlファイル）
pip install ./install/mera-2.5.0+pkg.3577-cp310-cp310-manylinux_2_27_x86_64.whl
```

### 5.2 転倒検出モデルの変換

```bash
# 転倒検出モデルのTFLite INT8ファイルを配置
mkdir -p models_int8
cp <path-to-fall-detection-model>/best_saved_model_int8.tflite models_int8/

# Ethos-U55向けにデプロイ
cd scripts/
python mcu_deploy.py --ethos --ref_data ../models_int8 deploy_fall_detection
```

### 5.3 生成物の確認

```bash
# 生成されたCソースコードの確認
ls deploy_fall_detection/*/build/MCU/compilation/src/

# 期待されるファイル:
# model.c, model.h
# model_io_data.c, model_io_data.h
# sub_XXXX_command_stream.c, sub_XXXX_command_stream.h
# sub_XXXX_invoke.c, sub_XXXX_invoke.h
# sub_XXXX_model_data.c, sub_XXXX_model_data.h
# sub_XXXX_tensors.c, sub_XXXX_tensors.h
# ethosu_common.h
```

### 5.4 e2 studioプロジェクトへの組み込み

1. 生成された `src/` 内の全ファイルを `e2studio_CPU0/src/ai_application/face_detection/mera/` に配置（既存ファイルを置き換え）
2. `wrapper.h` を新モデルの入出力関数名に合わせて更新
3. `ai_application_config.h` の入力サイズ、チャンネル数、最大検出数を更新
4. `DetectorPostProcessing.cc` のアンカーボックス、量子化パラメータ、クラス数を更新
5. ビルド・動作確認

### 5.5 確認すべきポイント

- [ ] `sub_XXXX_tensors.c` 内のArenaサイズが432KB (442,368バイト) 以内に収まっているか
- [ ] 変換時にエラー・警告が出ていないか
- [ ] 全オペレータがEthos-U55にマッピングされているか（CPUフォールバックがないか）
- [ ] 入出力テンソルの形状・型が期待通りか

---

## 6. リスクと課題

### 6.1 確認済みのリスク

| リスク | 影響度 | 状況 | 対策 |
|--------|--------|------|------|
| ライセンス取得の遅延 | - | **解消済み**: Apache 2.0で無償利用可能 | - |
| モデルサイズがArena制約超過 | 高 | YOLOv8n (3.1MB) は432KB Arenaに収まらない | Issue #102のモデル小型化が必須。SDRAMアリーナの可能性も調査中 |
| YOLOv8オペレータの非対応 | 中 | 主要オペレータは対応済みだが、一部未確認 | 変換実行時に判明する。非対応の場合はモデル構造の変更で対応 |
| 推論時間のKPI (5ms) 達成 | 高 | YOLOv8nの3.0Mパラメータではおそらく5ms超過 | YOLOv8 picoへの小型化で対応 |

### 6.2 次のアクション

1. **最優先**: Issue #102のモデル小型化を進め、YOLOv8 picoモデルのTFLite INT8を生成する
2. **環境構築**: RUHMI Frameworkの環境をローカルに構築する（Python 3.10 + MERA whl）
3. **変換テスト**: 小型化後のモデルでRUHMI変換を実行し、Arenaサイズを確認する
4. **e2 studio組み込み**: 変換成功後、顔認識サンプルをベースに転倒検出プロジェクトを構築する

---

## 7. 補足: RUHMI Frameworkの公式リソース一覧

| リソース | URL |
|---------|-----|
| GitHubリポジトリ | https://github.com/renesas/ruhmi-framework-mcu |
| MERA API仕様書 | https://renesas.github.io/ruhmi-framework-mcu/mera_api.html |
| オペレータサポート一覧 | リポジトリ内 `docs/operator_support.md` |
| 検証済みモデル一覧 | リポジトリ内 `docs/models_tested.md` |
| Quick Start Guide | https://www.renesas.com/en/document/qsg/renesas-ruhmi-framework-quick-start-guide |
| Ethos-U NPU with RA8 MCUs (App Note) | https://www.renesas.com/en/document/apn/using-ethos-u-npu-ra8-mcus-0 |
| RUHMI Framework製品ページ | https://www.renesas.com/en/software-tool/ruhmi-framework |
| EdgeCortix MERAプレスリリース | https://www.edgecortix.com/en/press-releases/edgecortix-mera-compiler-software-powers-renesas-new-ruhmi-framework-for-efficient-ai-deployment-across-mcus-and-mpus |
| Arm Vela Compiler (PyPI) | https://pypi.org/project/ethos-u-vela/ |

---

## 改訂履歴

| 日付 | バージョン | 変更内容 | 作成者 |
|------|-----------|---------|--------|
| 2026-03-07 | 1.0 | 初版作成 | Claude Code |
