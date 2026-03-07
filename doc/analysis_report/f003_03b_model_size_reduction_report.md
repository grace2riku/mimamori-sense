# F-003-3b: 転倒検出モデルの小型化 調査・方針レポート

Issue #102 の調査結果と小型化方針をまとめる。

---

## 1. 現状分析

### 1.1 現在のモデル仕様

| 項目 | 値 |
|------|-----|
| アーキテクチャ | YOLOv8n (Ultralytics) |
| 入力 | 192x192x3 (RGB, INT8) |
| パラメータ数 | 約3.0M |
| INT8モデルサイズ | 3,149 KB (3.1 MB) |
| mAP@0.5 | 67.8% |
| Recall | 59.3% |

### 1.2 目標（内蔵SRAM配置の場合）

| 項目 | 目標値 |
|------|--------|
| INT8モデルサイズ | 432 KB (442,368バイト) 以内 |
| パラメータ数 | 約0.24M (YOLO-Fastest相当) |
| mAP@0.5 | 可能な限り維持 |

### 1.3 削減必要量

- 現在: 3,149 KB
- 目標: 432 KB
- 削減率: **86.3%** (約7.3分の1に縮小)

---

## 2. Tensor Arena 配置先の調査

### 2.1 RUHMI公式サンプルでの配置先

リポジトリ内の2つのRUHMI公式サンプルを調査し、配置先の方針を分析した。

#### 顔認識サンプル (face_detection)

```
ファイル: application_examples/face_detection/src/application_config.h

AI_MODEL_ALLOCATION            = ALLOCATE_TO_ONCHIP_ROM   (内蔵ROM)
AI_INPUT_IMAGE_ALLOCATION      = ALLOCATE_TO_ONCHIP_RAM   (内蔵RAM)
TENSOR_ARENA_ALLOCATION        = ALLOCATE_TO_ONCHIP_RAM   (内蔵RAM)
```

Arena情報 (sub_0000_tensors.c):
- Arenaサイズ: **442,368バイト (432KB)**
- モデルデータ (Flash): 422,048バイト (412KB)
- コマンドストリーム: 11,252バイト

#### 画像分類サンプル (image_classification / MobileNet v2)

```
ファイル: application_examples/image_classification/src/application_config.h

AI_MODEL_ALLOCATION            = ALLOCATE_TO_SDRAM_INITIAL_IN_OSPI  (SDRAM)
AI_INPUT_IMAGE_ALLOCATION      = ALLOCATE_TO_SDRAM                   (SDRAM)
TENSOR_ARENA_ALLOCATION        = (未定義 - デフォルト.bss = 内蔵RAM)
```

Arena情報 (sub_0000_tensors.c):
- Arenaサイズ: **401,408バイト (392KB)**
- モデルデータ (Flash): 505,776バイト (494KB)
- コマンドストリーム: 5,008バイト

### 2.2 配置先の分析結果

| 項目 | face_detection | image_classification |
|------|---------------|---------------------|
| モデルデータ | OnChip ROM (412KB) | SDRAM (OSPI経由, 494KB) |
| Tensor Arena | OnChip RAM (432KB) | OnChip RAM (392KB) |
| AI入力バッファ | OnChip RAM (37KB) | SDRAM (147KB) |
| 推論時間 | 約3ms | 不明 |

**重要な発見:**

1. **Tensor Arenaは両サンプルとも内蔵RAMに配置されている。** image_classificationサンプルでもArena自体はOnChip RAMに配置しており、SDRAMに配置する公式の実例は存在しない。

2. **ArenaサイズとINT8モデルサイズは別の概念である。** Arenaは推論中の中間テンソルを格納するワークメモリであり、モデルの重みデータ（MODEL）はFlashまたはSDRAMに別途配置される。

3. **Arena = 推論ワークメモリの上限が432KB付近** (face_detectionで442,368バイト、image_classificationで401,408バイト)。これはRA8P1のOnChip RAM容量と他機能（FreeRTOS、LVGL等）とのバランスで決まる。

4. **モデルデータ（重み）はFlashまたはSDRAMに配置可能** であり、モデルファイルサイズ自体はArena制約の対象外。ただし、大きなモデルはArena（中間テンソル）も大きくなる傾向がある。

### 2.3 Arena/モデルデータの配置構造

```
┌──────────────────────────────────────────────┐
│ Flash (内蔵ROM / OSPI)                        │
│  - モデル重みデータ (MODEL)                    │
│  - コマンドストリーム (COMMAND_STREAM)          │
│  - 容量制約は比較的緩い                         │
├──────────────────────────────────────────────┤
│ OnChip RAM (内蔵SRAM)                          │
│  - Tensor Arena (ARENA) ← 推論ワークメモリ     │
│  - 入出力バッファ                               │
│  - FreeRTOS/LVGL/その他 と共有                 │
│  - 約432KB がAI推論に使える上限                 │
├──────────────────────────────────────────────┤
│ SDRAM (外部, 64MB)                             │
│  - モデルデータの配置先として使用可能             │
│  - 入力画像バッファの配置先として使用可能          │
│  - Arena (推論ワークメモリ) の配置実績なし       │
│  - カメラバッファ、フレームバッファ等と共有        │
└──────────────────────────────────────────────┘
```

### 2.4 MERA/Vela の Arena サイズ決定メカニズム

MERA SDK の `mcu_deploy.py` と `mcu_quantize.py` のソースコードから:

```python
vela_config = {
    "memory_mode": "Sram_Only",        # Arena は SRAM 前提
    "accel_config": "ethos-u55-256",   # 256 MACs 構成
    "optimise": "Performance",          # パフォーマンス優先最適化
}
```

`memory_mode: "Sram_Only"` は Vela コンパイラに「Arenaは内蔵SRAMのみ使用する」ことを指示している。Arenaサイズはモデルのアーキテクチャ・レイヤ構成に依存し、Velaが自動計算する。

### 2.5 Arena 配置先の結論

| 方針 | 推奨度 | 理由 |
|------|--------|------|
| **Arena を OnChip RAM に配置** | **推奨** | 公式サンプル2件とも OnChip RAM に配置。Vela の memory_mode も "Sram_Only"。動作実績あり |
| Arena を SDRAM に配置 | 非推奨 | 公式サンプルに実例なし。Vela の memory_mode 変更が必要。推論速度低下のリスク |
| モデルデータを SDRAM に配置 | 可能 | image_classification サンプルで実績あり (OSPI→SDRAM コピー) |

**結論: Tensor Arena は OnChip RAM (432KB制約) に収める必要がある。**

ただし、制約の対象は「INT8モデルファイルサイズ」ではなく「Arenaサイズ（推論中間テンソルサイズ）」である。これらは相関するが一致しない。

---

## 3. 制約の再整理

### 3.1 真の制約は「Arenaサイズ」

調査の結果、以下が判明した:

| 制約項目 | 制約値 | 配置先 |
|---------|--------|--------|
| **Arenaサイズ (推論ワークメモリ)** | **約432KB** | **OnChip RAM** |
| モデル重みデータ | Flash容量内 (数MB可) | OnChip ROM / OSPI / SDRAM |
| コマンドストリーム | Flash容量内 | Flash |

顔認識サンプルの実績:
- INT8モデルファイル: 約400KB
- そのうち MODEL (重みデータ): 422,048バイト → Flash配置
- ARENA (推論ワークメモリ): 442,368バイト → OnChip RAM配置

つまり、**INT8モデルファイルサイズが432KBを超えても、Arenaサイズが432KB以内ならデプロイ可能な可能性がある。** ただし、大きなモデルは中間テンソルも大きくなるため、Arenaサイズも増大する傾向がある。

### 3.2 YOLOv8n の Arena サイズ推定

YOLOv8nのINT8モデルサイズは3,149KB。同モデルをMERA/Velaでコンパイルした場合のArenaサイズは未検証だが、以下の推定が可能:

- 顔認識 (YOLO-Fastest): モデル 412KB → Arena 432KB (比率 1.05倍)
- 画像分類 (MobileNet v2): モデル 494KB → Arena 392KB (比率 0.79倍)

YOLOv8n (3,149KB) のArenaサイズは、モデル構造上も432KBを大幅に超過する可能性が高い。**モデル小型化は必須である。**

### 3.3 目標の再設定

MERA/Velaでの実際のコンパイル結果を見るまで正確なArenaサイズは不明だが、安全側に設計するために以下の目標を設定する:

| 項目 | 目標値 | 根拠 |
|------|--------|------|
| INT8モデルファイルサイズ | 500KB以下 | 顔認識サンプル (412KB) の約1.2倍 |
| パラメータ数 | 0.3M以下 | YOLO-Fastest (0.24M) の約1.25倍 |
| 推定Arenaサイズ | 432KB以下 | OnChip RAM 制約 |

---

## 4. 小型化アプローチ

### 4.1 アプローチ比較

| # | アプローチ | 推定パラメータ数 | 推定INT8サイズ | 実装難度 | 推奨度 |
|---|-----------|----------------|---------------|---------|--------|
| 1 | カスタム YOLOv8 pico | 0.1-0.3M | 100-400KB | 低 | **最推奨** |
| 2 | 入力チャネル 1ch化 (F-003-3cと同時) | 現モデルの約2/3 | 約2,100KB | 低 | 単独では不十分 |
| 3 | プルーニング (枝刈り) | 0.5-1.5M | 500-1,500KB | 中 | 補助的 |
| 4 | Knowledge Distillation | 0.1-0.3M | 100-400KB | 高 | 精度改善に有効 |
| 5 | 入力サイズ縮小 (192→128 or 160) | パラメータ数は不変 | モデルは不変、Arena減少 | 低 | 補助的 |

### 4.2 推奨アプローチ: カスタム YOLOv8 pico

Ultralytics YOLOv8 のアーキテクチャはYAML設定で `depth_multiple` と `width_multiple` を変更することで任意のサイズに調整可能。

#### YOLOv8 の公式バリエーション

| モデル | depth_multiple | width_multiple | パラメータ数 | 備考 |
|--------|---------------|----------------|------------|------|
| YOLOv8n | 0.33 | 0.25 | 3.0M | 現在使用中 |
| YOLOv8s | 0.33 | 0.50 | 11.2M | 大きすぎる |
| YOLOv8m | 0.67 | 0.75 | 25.9M | 大きすぎる |

#### カスタム YOLOv8 pico の設計

YOLO-Fastest (0.24M) と同等のパラメータ数を目指し、以下の構成を設計した:

**pico構成 (推奨)**:
- `depth_multiple: 0.33` (YOLOv8n と同じ)
- `width_multiple: 0.10` (YOLOv8n の 0.25 → 0.10 に縮小)
- `ch: 1` (Grayscale, F-003-3c と同時対応)

推定パラメータ数: 約0.12-0.2M (YOLOv8n比で約15-25分の1)
推定INT8サイズ: 約150-300KB

**nano-slim構成 (精度優先)**:
- `depth_multiple: 0.33`
- `width_multiple: 0.15`
- `ch: 1` (Grayscale)

推定パラメータ数: 約0.2-0.4M
推定INT8サイズ: 約250-450KB

### 4.3 Ethos-U55 対応オペレータの確認

YOLOv8 の主要オペレータとMERA/Ethos-U55の対応状況:

| YOLOv8 オペレータ | TFLite 対応 | MERA C99 codegen | Ethos-U55 量子化 |
|------------------|-------------|-------------------|-----------------|
| Conv2d | tfl.conv_2d | TFLiteQConv2d | A8W8 |
| DepthwiseConv2d | tfl.depthwise_conv_2d | (Conv2dで処理) | A8W8 |
| Concat | tfl.concatenation | TFLiteConcatenate | A8 |
| Add | tfl.add | TFLiteQAdd | A8 |
| MaxPool2d | tfl.max_pool_2d | TFLiteMaxPool | A8 |
| Reshape | tfl.reshape | (サポート済み) | A8 |
| Sigmoid (SiLU) | tfl.logistic | TFLiteSigmoid | A8 |
| Upsample (Nearest) | tfl.resize_nearest_neighbor | TFLiteResizeNearest | A8 |
| Mul | tfl.mul | TFLiteMul | A8 (ETHOS) |

**注意事項:**
- `tfl.mul` は MCU_CPU ではF32のみだが、MCU_ETHOS では A8 をサポート
- YOLOv8 の SiLU (Sigmoid Linear Unit) は `Sigmoid + Mul` の組み合わせ。Ethos-U55 では対応可能
- `tfl.concatenation` は4次元入力まで対応（制限事項）

全主要オペレータがMERA/Ethos-U55でサポートされており、**YOLOv8系アーキテクチャのデプロイは技術的に可能**と判断する。

### 4.4 入力サイズの検討

| 入力サイズ | 入力バッファ (1ch) | Arena影響 | 精度影響 |
|-----------|-------------------|-----------|---------|
| 192x192 | 36,864 B | 基準 | 基準 |
| 160x160 | 25,600 B | 減少 | やや低下 |
| 128x128 | 16,384 B | 大幅減少 | 低下 |

入力サイズ192x192は顔認識サンプルと同一であり、検証済みの設定として維持を推奨する。Arenaサイズが制約に収まらない場合に160x160への縮小を検討する。

---

## 5. 実験結果

### 5.1 実験1: pico構成 (width=0.10, max_channels=1024)

初回の設定でモデルを構築し、パラメータ数を事前確認した。

| 項目 | 値 |
|------|-----|
| depth_multiple | 0.33 |
| width_multiple | 0.10 |
| max_channels | 1024 |
| 入力 | 192x192x3 (RGB) |
| パラメータ数 | 730,147 (0.730M) |
| 推定INT8サイズ | 713 KB |

**結果: パラメータ数が目標(0.3M以下)を大幅に超過。** Detectヘッドのパラメータが `width_multiple` で十分に縮小されず、backbone/headの深い層のチャネル数が大きいままだったことが原因。学習は実施せず、設定を見直した。

### 5.2 実験2: pico構成 (width=0.08, max_channels=256) — 採用

`max_channels` を1024→256に制限し、`width_multiple` も0.10→0.08に縮小。

| 項目 | 値 |
|------|-----|
| depth_multiple | 0.33 |
| width_multiple | 0.08 |
| max_channels | 256 |
| 入力 | 192x192x3 (RGB) |
| パラメータ数 | 263,435 (0.263M) |
| 推定INT8サイズ | 257 KB |

パラメータ数が目標範囲内に収まったため、この設定で学習を実施した。

#### 学習設定

| 項目 | 値 |
|------|-----|
| エポック数 | 200 |
| バッチサイズ | 64 |
| オプティマイザ | SGD (lr=0.01, momentum=0.937) |
| 入力サイズ | 192x192 |
| GPU | NVIDIA L4 (Google Colab) |
| 学習時間 | 約5時間 |

#### 精度結果 (検証データ)

| 指標 | pico (実験2) | YOLOv8n (参考) | 差分 |
|------|-------------|----------------|------|
| mAP@0.5 | 45.9% | 67.8% | -21.9pt |
| mAP@0.5:0.95 | 23.7% | — | — |
| Precision | 57.6% | — | — |
| Recall | 44.1% | 59.3% | -15.2pt |

#### サイズ結果

| 形式 | サイズ | Arena制約(432KB)との比較 |
|------|--------|------------------------|
| FP32 TFLite | 1,087.9 KB | OVER |
| **INT8 TFLite** | **365.7 KB** | **OK (余裕66KB)** |

YOLOv8n INT8 (3,149KB) からの削減率: **88.4%**

#### 備考

- 入力は `ch: 1` (Grayscale) を指定していたが、実際には3ch (RGB) で学習・変換された。Grayscale対応はIssue #103 (F-003-3c) で別途対応。
- 学習曲線はEpoch 50付近からmAP50の改善が鈍化し、最終的にmAP50=45.9%で収束。
- Recall 44.1%は「転倒の約4割しか検出できない」水準であり、実用上は改善が必要。

### 5.3 実験3: nano-slim構成 (width=0.15, max_channels=1024) — パラメータ確認のみ

picoより精度を上げるためnano-slim構成を検討。まず元の設定でパラメータ数を確認した。

| 項目 | 値 |
|------|-----|
| depth_multiple | 0.33 |
| width_multiple | 0.15 |
| max_channels | 1024 |
| パラメータ数 | 1,319,083 (1.319M) |
| 推定INT8サイズ | 1,288 KB |

**結果: 推定INT8サイズ1,288KBでArena上限を大幅超過。** picoと同様にmax_channelsが原因。学習は実施せず設定を見直した。

### 5.4 実験4: nano-slim構成 (width=0.15, max_channels=256) — パラメータ確認のみ

`max_channels` を1024→256に制限。

| 項目 | 値 |
|------|-----|
| depth_multiple | 0.33 |
| width_multiple | 0.15 |
| max_channels | 256 |
| パラメータ数 | 461,523 (0.462M) |
| 推定INT8サイズ | 451 KB |

**結果: 推定INT8サイズ451KBでArena上限(432KB)を19KB超過。** ギリギリだが安全側に設計するため`width_multiple`を縮小することにした。

### 5.5 実験5: nano-slim構成 (width=0.12, max_channels=256) — 学習実施

`width_multiple` を0.15→0.12に縮小し、安全にArena内に収まる構成にした。

| 項目 | 値 |
|------|-----|
| depth_multiple | 0.33 |
| width_multiple | 0.12 |
| max_channels | 256 |
| 入力 | 192x192x3 (RGB) |
| パラメータ数 | 348,667 (0.349M) |
| 推定INT8サイズ | 340 KB |

パラメータ数がpicoの0.263Mより大きく、推定サイズもArena内に収まるため学習を実施した。

#### 精度結果 (検証データ)

| 指標 | nano-slim (実験5) | pico (実験2) | YOLOv8n (参考) |
|------|-------------------|-------------|----------------|
| mAP@0.5 | 50.7% | 45.9% | 67.8% |
| mAP@0.5:0.95 | 27.2% | 23.7% | — |
| Precision | 64.6% | 57.6% | — |
| Recall | 46.0% | 44.1% | 59.3% |

#### サイズ結果

| 形式 | サイズ | Arena制約(432KB)との比較 |
|------|--------|------------------------|
| FP32 TFLite | 1,420.6 KB | OVER |
| **INT8 TFLite** | **457.1 KB** | **OVER (超過25KB)** |

**結果: 精度はpicoより+4.8pt (mAP50) 向上したが、INT8サイズが457KBでArena上限を25KB超過。** 推定340KBに対して実測457KBと大きな乖離があり、推定値はあくまで目安であることが確認された。

### 5.6 全実験結果の比較

| # | 構成 | width | max_ch | パラメータ数 | 推定INT8 | 実測INT8 | mAP50 | サイズ判定 |
|---|------|-------|--------|------------|---------|---------|-------|----------|
| 1 | pico | 0.10 | 1024 | 0.730M | 713KB | — | — | 学習せず |
| 2 | **pico** | **0.08** | **256** | **0.263M** | **257KB** | **366KB** | **45.9%** | **OK** |
| 3 | nano-slim | 0.15 | 1024 | 1.319M | 1,288KB | — | — | 学習せず |
| 4 | nano-slim | 0.15 | 256 | 0.462M | 451KB | — | — | 学習せず |
| 5 | nano-slim | 0.12 | 256 | 0.349M | 340KB | 457KB | 50.7% | OVER |

### 5.7 採用モデルの決定

**pico構成 (実験2: width=0.08, max_channels=256) を採用する。**

- サイズ: INT8 366KB — Arena制約432KB以内で余裕66KB
- 精度: mAP50 = 45.9% — YOLOv8nの67.8%から-21.9ptの劣化あり
- 採用理由: Arena制約を満たす唯一の構成。精度改善は別途Issue (#101等) で対応する
- 方針: まずアプリケーション全体の推論パイプラインを構築・検証し、モデル精度の改善は後から反復的に行う

### 5.8 今後の精度改善候補

精度改善のために以下のアプローチを今後検討する（未実施）:

| # | アプローチ | 期待される効果 | 備考 |
|---|-----------|--------------|------|
| 1 | 入力解像度を上げる (192→256) | 小さい対象の検出精度向上 | Arenaサイズ増大のリスク |
| 2 | Knowledge Distillation | 小型モデルの精度底上げ | 実装難度が高い |
| 3 | データ拡張の強化 | 汎化性能の向上 | 学習パラメータ調整 |
| 4 | width=0.09〜0.10, max_channels=256で中間構成 | picoとnano-slimの中間を狙う | INT8サイズ要確認 |

---

## 6. 小型化実装計画

### 6.1 Phase 1: カスタム YOLOv8 pico の学習 — 完了

1. カスタムモデルYAML (`yolov8-pico-fall.yaml`) を作成
2. 学習ノートブックを更新（pico構成対応）
3. Google Colab で学習実行（200エポック）
4. INT8量子化・変換で正常完了を確認 → **366KB (OK)**
5. 精度・サイズのトレードオフを評価 → **サイズOK、精度は要改善**

### 6.2 Phase 2: nano-slim構成の試行 — 完了

nano-slim構成を3パターン試行した結果、INT8サイズがArena制約を超過。
picoモデルを採用し、精度改善は別途対応する方針を決定。

- nano-slim (width=0.15, max_channels=1024): 推定1,288KB → 学習せず
- nano-slim (width=0.15, max_channels=256): 推定451KB → 学習せず
- nano-slim (width=0.12, max_channels=256): 実測457KB → **25KB超過**

### 6.3 Phase 3: MERA/Vela での Arena サイズ確認

MERA SDK が利用可能になった時点で:

1. pico モデルの INT8 TFLite を MERA でコンパイル
2. 生成された `sub_0000_tensors.c` の Arena サイズを確認
3. 432KB 以内であることを検証
4. 超過する場合は `width_multiple` を更に縮小

---

## 6. 成果物一覧

| 成果物 | パス | 説明 |
|--------|------|------|
| 本レポート | `doc/analysis_report/f003_03b_model_size_reduction_report.md` | 調査結果・方針・実験結果 |
| カスタムモデルYAML (pico) | `dataset/scripts/yolov8-pico-fall.yaml` | depth=0.33, width=0.08, max_channels=256 |
| カスタムモデルYAML (nano-slim) | `dataset/scripts/yolov8-nano-slim-fall.yaml` | depth=0.33, width=0.12, max_channels=256 |
| 学習ノートブック | `dataset/scripts/train_yolov8_pico_colab.ipynb` | pico/nano-slim 対応版 |
| INT8モデル (pico) | `dataset/models/yolov8_pico_fall_int8.tflite` | 366KB, 0.263Mパラメータ, mAP50=45.9% |

---

## 7. リスクと対策

| リスク | 影響度 | 対策 |
|--------|--------|------|
| pico モデルの精度が大幅に低下 | 高 | width_multiple を段階的に調整 (0.10→0.15→0.20)。Knowledge Distillation の検討 |
| Arena サイズが 432KB を超過 | 中 | 入力サイズ縮小 (160x160)、width_multiple 更に縮小 |
| MERA/Vela で未サポートオペレータ | 低 | YOLOv8 の主要オペレータはすべてサポート済み。SiLU の変換に注意 |
| INT8 量子化で精度劣化 | 中 | キャリブレーション画像数の増加、QAT (Quantization-Aware Training) の検討 |

---

## 8. 未解決事項

| 項目 | 状態 | 次のアクション |
|------|------|---------------|
| MERA SDK ライセンス | 未取得 | Issue #104 で対応中 |
| pico モデルの実際の Arena サイズ | 未検証 | MERA SDK 取得後に検証 |
| SDRAM への Arena 配置可否 | 公式実例なし | Renesas に直接問い合わせ (推奨しないが選択肢として残す) |
| Knowledge Distillation の効果 | 未検証 | Phase 1 の精度が不十分な場合に検討 |

---

## 改訂履歴

| 日付 | バージョン | 変更内容 | 作成者 |
|------|-----------|---------|--------|
| 2026-03-06 | 1.0 | 初版作成: Arena配置先調査、小型化方針策定 | Claude Code |
| 2026-03-07 | 1.1 | 実験結果追加: pico構成2パターンの結果、INT8モデル(366KB)生成 | Claude Code |
| 2026-03-07 | 1.2 | nano-slim実験結果追加(3パターン)、picoモデル採用決定 | Claude Code |
