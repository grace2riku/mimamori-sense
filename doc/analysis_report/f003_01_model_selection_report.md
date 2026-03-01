# F-003-1: 転倒検出AIモデル調査・選定レポート

本レポートはIssue #18 (F-003-1)の成果物である。EK-RA8P1のEthos-U55 NPU上で動作する転倒検出AIモデルの調査・選定結果をまとめる。

---

## 1. 調査の前提条件

### 1.1 ハードウェア制約

| 項目 | 値 | 出典 |
|---|---|---|
| MCU | Renesas RA8P1 (Cortex-M85 @ 1GHz + Ethos-U55 NPU) | product-requirements.md |
| NPUアリーナサイズ上限 | 442,368バイト (432KB) | sub_0000_tensors.c |
| モデルデータ (Flash) | 422,048バイト (約412KB) ※顔認識モデル実績値 | sub_0000_tensors.c |
| コマンドストリーム | 11,252バイト (約11KB) ※顔認識モデル実績値 | sub_0000_tensors.c |
| 対応量子化形式 | INT8 (TFLite) | models_tested.md |

### 1.2 KPI要件

| KPI | 目標値 | 出典 |
|---|---|---|
| AI推論時間 | 5ms以内 | product-requirements.md |
| 転倒検出精度 (検出率) | 90%以上 | product-requirements.md |
| 誤検出率 | 5%以下 | product-requirements.md |
| 転倒検出から通知までの時間 | 10秒以内 | product-requirements.md |

### 1.3 ベースライン: 顔認識サンプルのAIモデル仕様

RUHMIフレームワークの顔認識サンプルが既にEK-RA8P1上で動作実績があり、これがベースラインとなる。

| 項目 | 値 |
|---|---|
| モデル | YOLO-Fastest (yolo-fastest-192_face_v4) |
| アーキテクチャ | YOLO-Fastest v1.1 (Darknet系) |
| 入力 | 192x192x1 (Grayscale, INT8) |
| 入力サイズ | 36,864バイト |
| 出力0 (Branch0) | 648バイト (6x6グリッド, 3アンカー, 6値) |
| 出力1 (Branch1) | 2,592バイト (12x12グリッド, 3アンカー, 6値) |
| クラス数 | 1 (顔) |
| パラメータ数 | 約0.24M (推定) |
| モデルデータサイズ (Flash) | 約412KB |
| NPUアリーナサイズ | 442,368バイト (432KB) |
| 推論時間 (EK-RA8P1 NPU) | 約3ms |
| 後処理 | NMS (閾値0.5, IoU閾値0.45) |
| RUHMI/MERA変換 | 実績あり (models_tested.mdに記載) |

---

## 2. 候補モデルの調査

### 2.1 候補モデル比較表

| # | モデル | パラメータ数 | 入力サイズ (想定) | モデルサイズ (INT8推定) | 計算量 | Ethos-U55推論時間見込み | TFLite INT8 | RUHMI変換実績 | 転倒検出適用性 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | **YOLO-Fastest v1.1** | 0.24M-0.35M | 192x192x1 | 約412KB (実測) | 0.252 BFLOPs (320x320時) | **約3ms (実測)** | 対応済 | **実績あり** | 高 |
| 2 | **YOLO-Fastest-XL v1.1** | 0.925M | 192x192x1 | 約900KB-1MB (推定) | 0.725 BFLOPs (320x320時) | 7-10ms (推定) | 対応可能 | 未確認 | 中 |
| 3 | **YOLOv5n (nano)** | 1.9M | 192x192x3 | 約2.1MB (推定) | 4.5 GFLOPs (640x640時) | 15ms以上 (推定) | 対応可能 | 未確認 | 低 |
| 4 | **YOLOv8n (nano)** | 3.2M | 192x192x3 | 約3.5MB (推定) | 8.7 GFLOPs (640x640時) | 20ms以上 (推定) | 対応可能 | 未確認 | 低 |
| 5 | **NanoDet-Plus-m** | 0.95M | 192x192x3 | 約980KB (推定) | 0.37 GFLOPs (320x320時) | 8-12ms (推定) | 対応可能 | **実績あり** (nanodet-plus-m-1.5x_416がONNXでテスト済) | 中 |
| 6 | **person-det (MobileNet系)** | 0.3M-0.5M | 96x96x1 | 約300KB (推定) | 極小 | 1-2ms (推定) | **対応済** | **実績あり** (models_tested.mdに記載) | 中 (分類のみ) |

### 2.2 各モデルの詳細分析

#### 2.2.1 YOLO-Fastest v1.1 (推奨)

**概要:** dog-qiuqiu氏が開発した超軽量YOLO系物体検出モデル。グループ化畳み込みを多用し、パラメータ数と計算量を極限まで削減している。

- **出典:** https://github.com/dog-qiuqiu/Yolo-Fastest
- **パラメータ数:** 0.24M-0.35M (圧縮なし0.35M、圧縮後0.2M)
- **モデルサイズ (ncnn):** 666KB (FP32)、INT8ではさらに縮小
- **計算量:** 0.252 BFLOPs (320x320入力時)
- **EK-RA8P1での実績:** 192x192x1 Grayscaleで顔認識として**約3ms**で推論実績あり
- **TFLite INT8:** models_tested.mdに「yolo-fastest-192_face_v4 (tflite, INT8)」として検証済み
- **RUHMI/MERA変換:** 顔認識サンプルとして変換実績があり、mera/ディレクトリ全体が自動生成されている
- **Ethos-U55での推論:** Arm Virtual Platform上で5.4ms (128 MACs構成)、EK-RA8P1実機で約3ms (RUHMI/MERA経由)
- **転倒検出への適用:** クラス数を1(顔)から1-2(人物/転倒)に変更してカスタム学習すれば対応可能。アーキテクチャは同一のため、前後処理コードの流用性が最も高い

**利点:**
- EK-RA8P1上でのRUHMI/MERA変換・動作実績が確立されている
- 推論時間3msはKPI (5ms以内) を十分に満たす
- NPUアリーナサイズ (432KB) に収まることが実証済み
- 既存の後処理コード (DetectorPostProcessing) をほぼそのまま流用できる
- Nota-NetsPresso ModelZooでCortex-M85/Ethos-U55向けのチュートリアルが公開されている

**リスク:**
- 転倒検出用の事前学習済みモデルは存在しないため、カスタム学習が必要
- COCO mAP@0.5が24.40% (320x320) と精度面では他モデルに劣る
- 入力が192x192x1 (Grayscale) であり、色情報が利用できない

#### 2.2.2 YOLO-Fastest-XL v1.1

**概要:** YOLO-Fastestの拡張版。チャンネル数を増やし精度を向上させたバージョン。

- **出典:** https://github.com/dog-qiuqiu/Yolo-Fastest
- **パラメータ数:** 0.925M
- **モデルサイズ:** 3.7MB (FP32 ncnn)、INT8では約900KB-1MB
- **計算量:** 0.725 BFLOPs (320x320入力時)
- **COCO mAP@0.5:** 34.33% (320x320)、YOLO-Fastestから約10ポイント向上
- **Ethos-U55推論時間:** 実測データなし。計算量がYOLO-Fastestの約2.9倍のため、7-10ms程度と推定
- **TFLite INT8:** 変換可能と考えられるが未検証
- **RUHMI/MERA変換:** 未確認

**利点:**
- YOLO-Fastestと同じアーキテクチャベースのため、コード流用性が高い
- 精度がYOLO-Fastestより約10ポイント高い

**リスク:**
- モデルサイズ (INT8推定約900KB-1MB) がFlash格納量を圧迫する可能性
- 推論時間がKPI (5ms以内) を超える可能性が高い
- NPUアリーナサイズが432KBに収まるか未検証

#### 2.2.3 YOLOv5n (nano)

**概要:** Ultralytics社のYOLOv5シリーズの最小バリアント。

- **出典:** https://github.com/ultralytics/yolov5
- **パラメータ数:** 1.9M
- **モデルサイズ (INT8):** 約2.1MB
- **計算量:** 4.5 GFLOPs (640x640入力時)
- **COCO mAP@0.5:** 45.7% (640x640)
- **Ethos-U55推論時間:** 実測データなし。計算量がYOLO-Fastestの約18倍のため、15ms以上と推定
- **TFLite INT8:** Ultralytics公式でエクスポートサポートあり

**利点:**
- 精度が高い (mAP 45.7%)
- 公式のTFLite INT8エクスポートツールが整備されている
- 転倒検出用のカスタム学習が容易 (Ultralytics CLI)

**リスク:**
- モデルサイズ (INT8約2.1MB) がEthos-U55のFlash/SRAM制約を大幅に超過する可能性
- 推論時間がKPI (5ms以内) を大幅に超過する見込み
- NPUアリーナサイズが432KBに収まらない可能性が高い
- RUHMI/MERA変換実績なし

#### 2.2.4 YOLOv8n (nano)

**概要:** Ultralytics社のYOLOv8シリーズの最小バリアント。アンカーフリー方式を採用。

- **出典:** https://github.com/ultralytics/ultralytics
- **パラメータ数:** 3.2M
- **モデルサイズ (INT8推定):** 約3.5MB
- **計算量:** 8.7 GFLOPs (640x640入力時)
- **COCO mAP@0.5:** 52.6% (640x640)
- **TFLite INT8:** Ultralytics公式でエクスポートサポートあり

**利点:**
- 最高レベルの精度
- 公式ツールが整備されている
- 転倒検出の研究論文で多数使用されている

**リスク:**
- モデルサイズとパラメータ数がMCU環境には過大
- 推論時間がKPI (5ms以内) を大幅に超過する
- NPUアリーナサイズの制約を満たせない
- RUHMI/MERA変換実績なし

#### 2.2.5 NanoDet-Plus-m

**概要:** RangiLyu氏が開発した軽量アンカーフリー物体検出モデル。ShuffleNetV2バックボーン + Ghost-PAN。

- **出典:** https://github.com/RangiLyu/nanodet
- **パラメータ数:** 約0.95M
- **モデルサイズ (INT8):** 約980KB
- **COCO mAP@0.5:0.95:** 34.3% (416x416)
- **RUHMIテスト実績:** 「nanodet-plus-m-1.5x_416 (ONNX, FP32)」がmodels_tested.mdに記載
- **TFLite INT8:** ONNX経由で変換可能

**利点:**
- RUHMIで動作テスト実績あり (ただしONNX FP32)
- アンカーフリー方式で後処理が比較的シンプル
- 精度がYOLO-Fastestより高い

**リスク:**
- 416x416入力サイズを192x192に縮小した場合の精度低下が不明
- INT8量子化後のRUHMI/MERA変換実績がない
- モデルサイズ (INT8約980KB) がFlash制約に近い
- 推論時間がKPI (5ms以内) を超える可能性
- 後処理コード (NMS等) の新規実装が必要

#### 2.2.6 person-det (MobileNet系 人物検出/分類)

**概要:** MLCommons/TinyMLの人物検出モデル。MobileNet系アーキテクチャで人物の有無を判定する画像分類モデル。

- **出典:** https://github.com/mlcommons/tiny
- **RUHMIテスト実績:** models_tested.mdに「person-det (tflite, INT8)」として記載
- **入力:** 96x96 Grayscale
- **パラメータ数:** 約0.3M
- **モデルサイズ (INT8):** 約300KB

**利点:**
- EK-RA8P1/RUHMI上での動作実績あり
- 非常に軽量で高速な推論が可能
- INT8 TFLiteとして既に提供されている

**リスク:**
- 物体検出ではなく画像分類であり、バウンディングボックスを出力しない
- 転倒検出には「人物検出+姿勢判定」のアプローチが取れない
- フレーム全体の「人がいる/いない」判定のみで、転倒/非転倒の判定には別途学習が必要

---

## 3. 転倒検出アプローチの比較分析

### 3.1 アプローチ一覧

| アプローチ | 概要 | 必要なモデル出力 | 後処理の複雑さ |
|---|---|---|---|
| **A: 人物検出 + 姿勢推定** | 人物のバウンディングボックスを検出し、別途姿勢推定モデルで姿勢を判定 | バウンディングボックス + 姿勢キーポイント | 高 (2段階推論) |
| **B: 転倒/非転倒の2クラス物体検出** | 「standing (立位)」と「fallen (転倒)」の2クラスで物体検出 | バウンディングボックス + クラス | 中 (NMS + クラス判定) |
| **C: 人物検出 + アスペクト比判定** | 人物のバウンディングボックスを検出し、縦横比で転倒を判定 | バウンディングボックス | 低 (NMS + アスペクト比計算) |

### 3.2 各アプローチの詳細分析

#### アプローチA: 人物検出 + 姿勢推定

**方式:** まず人物をバウンディングボックスで検出し、次に姿勢推定 (Pose Estimation) モデルで骨格キーポイントを推定して転倒状態を判定する。

**利点:**
- 最も高精度な転倒判定が可能
- 姿勢の詳細分析により誤検出を低減できる
- 研究論文でYOLOv8 + AlphaPose等の組み合わせで96%以上の精度が報告されている

**欠点:**
- 2つのモデルを同時にNPU上で実行する必要があり、推論時間がKPI (5ms) を超過する
- NPUアリーナサイズ (432KB) に2モデル分のテンソルが収まらない可能性が高い
- 姿勢推定モデル (例: MoveNet Lightning) 自体が軽量でも追加の推論コストが発生
- 実装の複雑さが増大する

**判定:** MCU環境の制約上、**不適切**。

#### アプローチB: 転倒/非転倒の2クラス物体検出

**方式:** 1つのYOLO系モデルで「standing (立位の人物)」と「fallen (転倒した人物)」の2クラスを検出する。

**利点:**
- 単一モデルで転倒検出が完結する (推論1回で済む)
- バウンディングボックスにより転倒した人の位置を特定できる
- 既存のYOLO後処理 (NMS) コードをクラス数変更のみでほぼ流用可能
- KPI (5ms以内) を満たせる見込みがある

**欠点:**
- 「standing」と「fallen」の2クラス分のアノテーション付きデータセットが必要
- 転倒の多様な姿勢 (横倒れ、うつ伏せ、仰向け等) を学習データでカバーする必要がある
- 「座っている」「しゃがんでいる」状態と転倒の区別が難しい場合がある
- クラス数が1から2に増えるためモデルサイズがわずかに増加する (出力テンソルの値が6から7に)

**判定:** **有力候補**。単一モデルで完結し、既存パイプラインの流用性が高い。

#### アプローチC: 人物検出 + アスペクト比判定

**方式:** 人物を1クラスで検出し、バウンディングボックスの縦横比 (アスペクト比) で転倒を判定する。立位の人物は縦長 (高さ > 幅)、転倒した人物は横長 (幅 > 高さ) になる性質を利用する。

**利点:**
- モデル側は人物検出 (1クラス) のみでよく、最もシンプル
- 顔認識モデルとほぼ同じアーキテクチャ (クラス数1) が使える
- 後処理でアスペクト比を計算するだけなので追加の推論コストがゼロ
- 学習データセットは「人物」のアノテーションのみで済む (COCOの"person"クラス等)
- YOLO-Fastest (192x192x1) の実績ある構成をそのまま活用できる
- 推論時間は顔認識モデルと同等 (約3ms) になる見込み

**欠点:**
- しゃがんでいる、座っている等の「横長に近い」姿勢を転倒と誤判定するリスク
- カメラの角度によってアスペクト比が変動する
- 遠方の人物ではバウンディングボックスが小さく精度が低下する
- 時系列的な変化 (急に縦長から横長に変わった) を考慮する場合はフレーム間追跡が必要

**判定:** **最有力候補**。実装の容易さ、既存パイプラインの流用性、推論時間のKPI達成確度が最も高い。

### 3.3 アプローチ選定結果

**選定: アプローチC (人物検出 + アスペクト比判定) を第一選択とし、アプローチB (2クラス検出) を第二選択とする。**

#### 選定理由

1. **推論時間のKPI達成確度が最も高い**: アプローチCはモデル構成が顔認識サンプルと同一 (1クラス物体検出) であり、推論時間約3msの実績がそのまま適用される。転倒判定はCPU側の後処理 (アスペクト比計算) で行うため、NPU推論時間に影響しない。

2. **既存パイプラインの最大流用**: アプローチCでは以下のコードをほぼそのまま流用できる:
   - `mera/` ディレクトリのMERA自動生成コード (モデル構造が同一のため)
   - `DetectorPostProcessing.cc/.hpp` (NMS処理、バウンディングボックス計算)
   - `MainLoop_obj.cc` (推論ループ、クラス数変更のみ)
   - `wrapper.h` (入出力ポインタ取得)

3. **NPUアリーナサイズ制約の確実な充足**: 432KBのアリーナサイズに収まることが顔認識モデルで実証されている。アプローチCは同一アーキテクチャのため確実に収まる。

4. **学習データセットの調達容易性**: COCOデータセットの"person"クラスやVisual Wake Words等の大規模人物検出データセットを活用できる。アプローチBのように転倒/非転倒の2クラス分のアノテーションを用意する必要がない。

5. **精度の補完可能性**: アスペクト比判定の精度が不十分な場合、以下の補完戦略を段階的に追加できる:
   - アスペクト比の閾値調整
   - フレーム間のアスペクト比変化率の監視 (急激な変化を転倒と判定)
   - 複数フレームにわたる横長状態の持続時間による確定判定
   - 将来的にアプローチBへの移行 (2クラス学習)

#### アプローチCの転倒判定ロジック (想定)

```
入力: バウンディングボックス (x, y, w, h)
出力: 転倒判定 (true/false)

1. 人物検出 (YOLO-Fastest) でバウンディングボックスを取得
2. アスペクト比 = w / h を計算
3. 判定条件:
   - アスペクト比 > 1.0 (幅 > 高さ) -> 転倒候補
   - 前フレームのアスペクト比との差分 > 閾値 -> 急激な姿勢変化を検出
   - N フレーム連続で転倒候補 -> 転倒確定
4. 転倒確定時 -> アラート通知
```

#### 段階的な精度改善パス

将来的にアプローチCの精度が不十分な場合、以下の順序で段階的に改善できる:

```
Phase 1: アプローチC (人物検出 + アスペクト比)
  | 精度不足の場合
Phase 2: アプローチC + 時系列分析 (フレーム間追跡)
  | 精度不足の場合
Phase 3: アプローチB (2クラス物体検出) に移行
```

---

## 4. モデル選定結果

### 4.1 選定モデル

**YOLO-Fastest v1.1 (192x192x1 Grayscale INT8)** を選定する。

### 4.2 選定モデルの仕様

| 項目 | 値 |
|---|---|
| モデル名 | YOLO-Fastest v1.1 |
| アーキテクチャ | YOLO-Fastest (Darknet系、グループ化畳み込み) |
| 入力サイズ | 192x192x1 (Grayscale, INT8) |
| 入力データサイズ | 36,864バイト |
| パラメータ数 | 約0.24M-0.35M |
| モデルデータサイズ (Flash) | 約412KB (INT8) |
| 計算量 | 約0.1 BFLOPs (192x192入力時推定) |
| 出力テンソル数 | 2 (Branch0: 6x6グリッド, Branch1: 12x12グリッド) |
| クラス数 | 1 (person) |
| 量子化方式 | INT8 (Post-Training Quantization) |
| NPUアリーナサイズ | 442,368バイト (432KB) |
| 推論時間 (EK-RA8P1 NPU) | 約3ms (実績値ベース) |
| 後処理 | NMS + アスペクト比判定 |

### 4.3 選定理由の要約

| # | 選定理由 | 詳細 |
|---|---|---|
| 1 | **動作実績** | EK-RA8P1のEthos-U55 NPU上でRUHMI/MERA経由の動作実績が確立されている (顔認識サンプル) |
| 2 | **KPI達成** | 推論時間約3msでKPI (5ms以内) を十分に満たす |
| 3 | **メモリ制約充足** | NPUアリーナ432KB、モデルデータ412KBの制約を実証済み |
| 4 | **コード流用性** | 既存の前処理・後処理・MERA統合コードをほぼそのまま流用可能 |
| 5 | **RUHMI/MERA変換** | 変換パイプラインが確立されており、カスタム学習モデルも同じ手順で変換可能 |
| 6 | **段階的改善** | アプローチC (アスペクト比判定) からアプローチB (2クラス検出) への移行パスがある |

### 4.4 選定しなかったモデルの理由

| モデル | 非選定理由 |
|---|---|
| YOLO-Fastest-XL | 推論時間がKPI (5ms) を超過する可能性が高い。モデルサイズがFlash制約に近い |
| YOLOv5n | パラメータ数1.9M、モデルサイズ約2.1MBでMCU環境には過大。RUHMI変換実績なし |
| YOLOv8n | パラメータ数3.2M、モデルサイズ約3.5MBでMCU環境には不適。RUHMI変換実績なし |
| NanoDet-Plus-m | モデルサイズ約980KBでFlash制約に近い。INT8でのRUHMI変換実績なし。後処理の新規実装が必要 |
| person-det | 画像分類モデルでバウンディングボックスを出力しないため、アプローチCに適さない |

---

## 5. NPUアリーナサイズ制約の確認

### 5.1 現行の顔認識モデルのメモリレイアウト

`sub_0000_tensors.c` から読み取ったメモリ構成:

| テンソル名 | 種別 | サイズ | アリーナ内オフセット |
|---|---|---|---|
| `_split_1_command_stream` | COMMAND_STREAM | 11,252バイト | - |
| `_split_1_flash` | MODEL (重みデータ) | 422,048バイト | - |
| `_split_1_scratch` | ARENA (作業領域) | **442,368バイト** | 0x0 |
| `_split_1_scratch_fast` | FAST_SCRATCH | 442,368バイト | 0x0 |
| `image_input` | INPUT_TENSOR | 36,864バイト | 0x24000 |
| `Identity_1_70284` | OUTPUT_TENSOR | 2,592バイト | 0x5580 |
| `Identity_70275` | OUTPUT_TENSOR | 648バイト | 0xd80 |

### 5.2 転倒検出モデルでの見通し

YOLO-Fastest v1.1を人物検出 (1クラス) で学習した場合:

- **入力テンソル:** 192x192x1 = 36,864バイト (顔認識と同一)
- **出力テンソル:** クラス数1のため出力構造は顔認識と同一 (6値 = x,y,w,h,obj,cls)
  - Branch0: 6x6x3x6 = 648バイト
  - Branch1: 12x12x3x6 = 2,592バイト
- **アリーナサイズ:** 顔認識モデルと同一の442,368バイト (432KB) に収まる見込み
- **モデルデータ (Flash):** 約412KB (顔認識と同等規模)

**結論:** NPUアリーナサイズ制約を満たす。

---

## 6. RUHMI/MERA変換の実現性

### 6.1 変換パイプライン

models_tested.mdに記載されたRUHMI検証済みモデル一覧を確認すると、以下の形式がサポートされている:

- **ONNX (FP32):** EfficientNet, MNASNet, MobileNetV2, NanoDet-Plus-m-1.5x等
- **PyTorch (FP32):** ResNet18, SqueezeNet1.0
- **TFLite (FP32):** ad01_fp32, mobilenetv2_model
- **TFLite (INT8):** Ad_medium, KWS_micronet_m, person-det, vww4_128_128, **yolo-fastest-192_face_v4**

### 6.2 転倒検出モデルの変換手順 (想定)

1. YOLO-Fastest v1.1を人物検出 (personクラス) でカスタム学習
2. Darknet形式からONNX形式にエクスポート
3. `scripts/mcu_quantize.py` でINT8量子化 + Ethos-U55向けデプロイ:
   ```
   cd scripts/
   python mcu_quantize.py -e ../models_fp32_ethos deploy_ethos
   ```
4. 生成された `mera/` ディレクトリのファイルを `src/ai_application/fall_detection/mera/` に配置
5. `wrapper.h` の入出力関数名を更新
6. `MainLoop_obj.cc` のアンカー値、量子化パラメータを新モデルに合わせて更新

### 6.3 変換実績による信頼性

YOLO-Fastest (TFLite INT8) のRUHMI変換は顔認識サンプルで実証済みであり、同一アーキテクチャのカスタム学習モデルも同じパイプラインで変換可能である。変換の技術的リスクは低い。

---

## 7. 既存コードの変更箇所まとめ

選定モデル (YOLO-Fastest v1.1) + 選定アプローチ (アプローチC) で実装する場合の変更箇所:

### 7.1 変更が必要なファイル

| ファイル | 変更内容 | 変更規模 |
|---|---|---|
| `mera/` ディレクトリ全体 | RUHMIツールで新モデルから自動生成したファイルに置き換え | 自動生成 |
| `wrapper.h` | 新モデルの入出力テンソル名に合わせて関数名を更新 | 小 |
| `MainLoop_obj.cc` | アンカー値、量子化パラメータ (scale, zero_point) を新モデルの値に更新 | 小 |
| `ai_application_config.h` | `AI_INPUT_IMAGE_WIDTH=192, HEIGHT=192, BYTE_PER_PIXEL=1` は変更不要。`AI_MAX_DETECTION_NUM` の調整のみ | 小 |
| `DetectorPostProcessing.cc` | `numClasses=1` は変更不要。転倒判定ロジック (アスペクト比計算) を後処理に追加 | 中 |
| `face_detection_screen_mipi.c` を `fall_detection_screen_mipi.c` に変更 | 表示内容の変更 (転倒検出結果の表示、警告UI) | 中 |
| `common_util.h` | 検出結果構造体に転倒フラグ等のフィールドを追加 | 小 |
| 画像前処理 (`camera_display_thread_entry.c`) | `image_rgb565_to_int8()` はそのまま流用可能 (Grayscale INT8変換) | 変更不要 |

### 7.2 変更不要なファイル

| ファイル/モジュール | 理由 |
|---|---|
| `camera_layer/` 全体 | カメラキャプチャはモデル非依存 |
| `display_layer/display_layer.c` | GLCDC/Dave2D初期化はモデル非依存 |
| `fsp_custom/` 全体 | ハードウェアドライバはモデル非依存 |
| `external_memory/` | 外部メモリ制御はモデル非依存 |
| `console_output/` | コンソール出力はモデル非依存 |
| `time_counter/` | 時間計測はモデル非依存 |
| `common_util.c` (大部分) | イベントフラグ定義等はモデル非依存 |
| `ai_inference_thread_entry.c` | 推論スレッドの基本構造は変更不要 |

---

## 8. 今後の調査・作業項目

| # | 項目 | 対応Issue | 優先度 |
|---|---|---|---|
| 1 | 人物検出用学習データセットの調達と準備 (COCO person等) | F-003-2 | 高 |
| 2 | YOLO-Fastest v1.1の人物検出カスタム学習 | F-003-3 | 高 |
| 3 | INT8量子化 (mcu_quantize.py) の実行と精度評価 | F-003-3 | 高 |
| 4 | RUHMIツールによるモデル変換 (mcu_deploy.py) | F-003-4 | 高 |
| 5 | アスペクト比判定の閾値チューニング | 実装フェーズ | 中 |
| 6 | フレーム間追跡による転倒確定ロジックの実装 | 実装フェーズ | 中 |
| 7 | アプローチBへの移行判断 (アプローチCの精度評価後) | 精度評価後 | 低 |

---

## 9. 参考資料

### 9.1 モデル・フレームワーク

- [YOLO-Fastest (dog-qiuqiu)](https://github.com/dog-qiuqiu/Yolo-Fastest) - 超軽量YOLO物体検出モデル
- [Nota-NetsPresso ModelZoo YOLO-Fastest for ARM U55/M85](https://github.com/Nota-NetsPresso/ModelZoo-YOLOFastest-for-ARM-U55-M85) - Ethos-U55/Cortex-M85向けYOLO-Fastest学習・デプロイチュートリアル
- [RUHMI Framework MCU (Renesas)](https://github.com/renesas/ruhmi-framework-mcu) - RUHMIフレームワーク
- [emza-vs/face_detection_example_arm_u55](https://github.com/emza-vs/face_detection_example_arm_u55) - Ethos-U55上のYOLO-Fastest顔検出実装 (192x192x1, INT8, 5.4ms@128MACs)
- [Ultralytics YOLOv5](https://github.com/ultralytics/yolov5) - YOLOv5シリーズ
- [Ultralytics YOLOv8](https://github.com/ultralytics/ultralytics) - YOLOv8シリーズ
- [NanoDet-Plus](https://github.com/RangiLyu/nanodet) - 軽量アンカーフリー物体検出モデル

### 9.2 転倒検出関連研究

- [Fall Detection and Pose Classification (YOLOv8 + MediaPipe)](https://github.com/InvictusRex/Fall-Detection-and-Pose-Classification) - エッジAI向け転倒検出
- [Human Fall Detection using Normalized Shape Aspect Ratio](https://www.researchgate.net/publication/328746754_Human_fall_detection_using_normalized_shape_aspect_ratio) - アスペクト比による転倒検出手法
- [Human Fall Detection Based on Motion Tracking and Shape Aspect Ratio](https://www.researchgate.net/publication/310510818_Human_Fall_Detection_Based_on_Motion_Tracking_and_Shape_Aspect_Ratio) - 動作追跡+アスペクト比による転倒検出
- [Fall Detection System With AI-Based Edge Computing](https://www.researchgate.net/publication/357596359_Fall_Detection_System_With_Artificial_Intelligence-Based_Edge_Computing) - エッジAIによる転倒検出システム
- [A Hybrid Human Fall Detection Method (YOLOv8s + AlphaPose)](https://www.nature.com/articles/s41598-025-86429-6) - ハイブリッド転倒検出
- [An Efficient Algorithm for Pedestrian Fall Detection (YOLOv8n)](https://www.nature.com/articles/s41598-025-93667-1) - YOLOv8nベースの効率的な転倒検出

### 9.3 Ethos-U55 NPU

- [Arm Ethos-U55 Product Page](https://www.arm.com/products/silicon-ip-cpu/ethos/ethos-u55) - Ethos-U55製品ページ
- [Benchmarking Ultra-Low-Power uNPUs](https://arxiv.org/html/2503.22567v2) - マイクロNPUベンチマーク
- [Arm Ethos-U NPU Backend (ExecuTorch)](https://docs.pytorch.org/executorch/1.0/backends-arm-ethos-u.html) - ExecuTorch Ethos-Uバックエンド
- [Memory Considerations for ML Embedded Evaluation Kit](https://review.mlplatform.org/plugins/gitiles/ml/ethos-u/ml-embedded-evaluation-kit/+/HEAD/docs/sections/memory_considerations.md) - メモリ設計ガイド

### 9.4 量子化・デプロイ

- [YOLOv5-Lite](https://github.com/ppogg/YOLOv5-Lite) - 軽量YOLOv5 (INT8で900KB)
- [MLCommons Tiny (person-det)](https://github.com/mlcommons/tiny) - TinyML人物検出ベンチマーク
- [Wake Vision Dataset](https://blog.tensorflow.org/2024/12/introducing-wake-vision-new-dataset-for-person-detection-in-tinyml.html) - TinyML向け大規模人物検出データセット

---

## 改訂履歴

| 日付 | バージョン | 変更内容 | 作成者 |
|------|-----------|---------|--------|
| 2026-03-01 | 1.0 | 初版作成 | Claude Code |
