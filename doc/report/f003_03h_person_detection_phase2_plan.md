# F-003-3h: YOLO-Fastest 人物検出モデル精度改善 Phase 2 引き継ぎドキュメント

**対象 Issue**: #133
**前提 Issue**: #131 (転移学習ベースの Phase 1 改善)
**フェーズ**: Phase 2 (mAP@0.50 35.13% -> 50% 中間目標)
**最終目標**: mAP@0.50 90% (Phase 3 で対応)

## 1. このドキュメントの位置付け

Issue #133 のうち「Colab で学習を実行する前までの準備」までを本ドキュメントで扱う。
実機 (EK-RA8P1) 側のパラメータ更新は、Phase 2 の再学習完了後に別タスクとして実施する。

- MCU 側コードは今回変更していない (`e2studio_CPU0/src/fall_detection_*.{c,h}`)
- 量子化パラメータ / アンカー値は再学習後にノートブックの Step 9.5 で自動出力される値に差し替える

## 2. #131 の状態と未達要因の整理

| 指標 | #128 | #131 | 中間目標 | 備考 |
|---|---|---|---|---|
| mAP@0.50 | 20.71% | 35.13% | 50%+ | |
| Recall | 21% | 40% | 60%+ | conf_thresh=0.25 |
| Precision | - | 49% | 60%+ | conf_thresh=0.25 |

### 実機検証で観察された問題 (PR #132)
1. 検出スコアが 0.15-0.25 と低い (目標 0.5+)
2. 位置推定が不正確 (クロップ境界に偏る傾向)
3. conf_thresh=0.25 以上で人物が安定検出できない

### 原因仮説 (優先度順)
| 仮説 | Phase 2 での対処 |
|---|---|
| データセットにラベルミス・低品質画像が混在し学習上限を制約している | 2A: dataset_cleaning.py |
| darknet デフォルト augmentation では特徴の多様性が不足 | 2B: mosaic/mixup/cutmix + 色空間強化 |
| 100k iteration の step decay では後半で収束不足 | 2C: max_batches 200k + cosine annealing |
| アンカーの適合度 | 2D: 学習後に再度 calc_anchors して改善余地を確認 |

## 3. Phase 2 で実施する変更点サマリ

### 3.1 Phase 2A: データセットクリーニング (新規)

**追加スクリプト**: `dataset/scripts/dataset_cleaning.py`

検出項目:
- ぼけ画像 (Laplacian variance < 50)
- 暗すぎ (輝度平均 < 25) / 白飛び (輝度平均 > 235)
- ラベル異常 (範囲外座標、サイズ0、画像/ラベル対応ミス)
- 重複画像 (perceptual hash ahash + Hamming 距離 <= 4)

運用ポリシー:
- デフォルト dry-run (統計のみ)
- `--apply` 指定時に `<dataset>/_removed_phase2/` にファイル移動 (物理削除しない)
- 切り戻し可能
- レポートは `phase2_cleaning_report.json` に出力

### 3.2 Phase 2B: データ拡張強化 (cfg 変更)

darknet 内蔵の強力な augmentation を有効化:

| パラメータ | #131 | Phase 2 |
|---|---|---|
| `mosaic` | 無効 | **1 (有効)** |
| `mixup` | 無効 | **1 (有効)** |
| `cutmix` | 無効 | **1 (有効)** |
| `jitter` | 0.3 | **0.5** |
| `saturation` | 1.5 | **2.0** |
| `exposure` | 1.5 | **2.0** |
| `hue` | 0.1 | **0.15** |
| `angle` | 15 (#131 から維持) | 15 |

### 3.3 Phase 2C: 学習最適化 (cfg 変更)

| パラメータ | #131 | Phase 2 |
|---|---|---|
| `max_batches` | 100,000 | **200,000** |
| `policy` | steps (60k, 80k, 90k) | **sgdr (cosine annealing)** |
| `sgdr_cycle` | - | **1000** |
| `sgdr_mult` | - | **2** |
| `burn_in` | 2,000 | **4,000** |
| `batch` / `subdivisions` | 64 / 16 | 64 / 16 (変更なし) |

`batch=96/128` は T4 GPU のメモリ余裕を見て必要なら手動で上げる余地あり (本家ノートブックは 64 のまま)。

### 3.4 Phase 2D: アーキテクチャ検証 (オプション - 今回は範囲外)

以下は本 Issue では実施せず、Phase 3 に回す:
- 入力解像度 224x224 化 (アリーナ 432KB 制約のため慎重に検証が必要)
- Grayscale 1ch 化 (F-003-3c の成果を YOLO-Fastest 人物検出に横展開)
- アンカーの二次最適化 (学習後の再計算)

## 4. 変更ファイル一覧 (今回のコミット対象)

| パス | 種別 | 内容 |
|---|---|---|
| `dataset/scripts/dataset_cleaning.py` | 新規 | Phase 2A クリーニングスクリプト |
| `dataset/scripts/train_yolo_fastest_darknet_colab.ipynb` | 更新 | Phase 2 対応 (タイトル / Step 2.5 追加 / Step 3 cfg / Step 6 マーカー) |
| `doc/report/f003_03h_person_detection_phase2_plan.md` | 新規 | 本ドキュメント |

### ノートブックの変更詳細

1. **先頭 markdown** (cell 0): Phase 2 の変更サマリと新ワークフローを追記
2. **Step 2.5 追加** (cell 9, 10): `dataset_cleaning.py` を dry-run -> `--apply` で実行し、`train.txt`/`valid.txt` を再生成
3. **Step 3 markdown** (cell 11): Phase 2 cfg 変更点の説明に更新
4. **Step 3 手書きテンプレート** (cell 13): cfg_content 内の `[net]` セクションを Phase 2 パラメータに更新 (`max_batches=200000`, `policy=sgdr`, `sgdr_cycle=1000`, `sgdr_mult=2`, `burn_in=4000`, `mosaic=1`, `mixup=1`, `cutmix=1`, `saturation=2.0`, `exposure=2.0`, `hue=.15`)、`[yolo]` セクションは `jitter=.5`
5. **Step 3 base-cfg patcher** (cell 14): リポジトリ内の base cfg をベースに正規表現で Phase 2 パラメータを適用するロジックに差し替え
6. **Step 6 markdown** (cell 22): Phase 2 の改善ポイントと再開手順に更新
7. **Step 6 重み選択ロジック** (cell 23):
   - マーカー `.issue131_started` -> **`.issue133_started`**
   - Phase 2 初回実行時、`yolo-fastest-person-192_best.weights` (=#131 の best) が Google Drive にあれば COCO backbone より優先して転移学習元に使用
   - 旧 Issue (#128, #131) の `_last.weights` は Phase 2 初回実行では無視される

## 5. ユーザー実行手順 (Colab)

### 5.1 事前準備

1. ローカルで `dataset/scripts/dataset_cleaning.py` を Google Drive にアップロード:
   ```
   Drive: /MyDrive/yolo_fastest_darknet_person/dataset_cleaning.py
   ```
   (Colab のノートブック Step 2.5 で自動的に `/content/` にコピーされる)

2. `/MyDrive/fall_detection_dataset.zip` が最新であることを確認 (#131 と同じ)

3. #131 の best weights を Phase 2 の転移学習元として使いたい場合、Drive に配置:
   ```
   Drive: /MyDrive/yolo_fastest_darknet_person/backup/yolo-fastest-person-192_best.weights
   Drive: /MyDrive/yolo_fastest_darknet_person/yolo-fastest-person-192_best.weights
   ```
   どちらか 1 つあれば Step 6 が自動的にそれを使う。
   使わない場合 (フル再学習したい場合) は `.issue133_started` マーカーと `_last.weights` を削除するだけで OK。

4. (安全策) Drive 上の `.issue131_started` は残したまま問題ない (Phase 2 は別マーカーを使う)。
   一方で Drive 上の古い `_last.weights` が残っていると、それを無視するロジックは入っているが、
   **新規実行時は `/MyDrive/yolo_fastest_darknet_person/backup/` の `_last.weights` を手動削除**しておくと安全。

### 5.2 学習実行前チェックリスト

- [ ] Colab ランタイムが GPU (T4 以上) に設定されている
- [ ] `/content/drive/MyDrive/fall_detection_dataset.zip` が存在する
- [ ] `/content/drive/MyDrive/yolo_fastest_darknet_person/dataset_cleaning.py` が配置済み
- [ ] `_last.weights` (旧 Issue) は削除済み or そのまま無視されることを確認
- [ ] (任意) #131 best weights が Drive backup に配置済み (Phase 2 の転移学習元として使う場合)

### 5.3 ノートブック実行順序

1. **Step 1-2**: #131 と同じ (darknet セットアップ、データセット展開)
2. **Step 2.5 (新規)**: dataset_cleaning.py を実行
   - dry-run で統計を確認し、想定内であれば `APPLY_CLEANING = True` (デフォルト) のまま続行
   - 想定外に大量に除去される場合は `APPLY_CLEANING = False` に変更して dry-run 結果を確認してから閾値 (`--blur-threshold` 等) を調整
3. **Step 3**: 2 つの cfg 生成セルを実行 (base cfg 版が優先される)
4. **Step 4**: クリーニング後のデータセットに対して K-means アンカー再計算
5. **Step 5**: COCO backbone 取得 (#131 と同じ)
6. **Step 6**: 学習開始
   - 初回実行で `.issue133_started` マーカーが作成され、`_best.weights` または COCO backbone から学習開始
   - 接続切れ後は再実行するだけで `_last.weights` から自動再開
   - 学習時間の目安: T4 で 7-10 時間、A100 で 2-4 時間
7. **Step 7**: 精度評価 (`darknet detector map`)
8. **Step 8-9**: TFLite INT8 変換
9. **Step 9.5**: MCU 向けパラメータ自動出力
10. **Step 10**: Vela 互換性確認
11. **Step 11**: 成果物ダウンロード

### 5.4 クリーニングの閾値チューニング

Step 2.5 の dry-run で「過剰に除去される」と判断した場合の調整:

| 症状 | 対処 |
|---|---|
| blur 候補が多すぎる | `--blur-threshold 30` (50 -> 30 に下げる) |
| too_dark 候補が多すぎる | `--dark-threshold 15` (25 -> 15 に下げる) |
| too_bright 候補が多すぎる | `--bright-threshold 245` |
| 重複検出が過剰 (似ている姿勢を消してしまう) | `--dup-hamming-threshold 2` (4 -> 2 に下げる) |
| 大規模データで重複検出が遅い | `--skip-duplicates` で重複検出を飛ばす |

## 6. Phase 2 完了後の MCU 側更新ガイド (再学習完了時に実施)

**本 Issue では実施しない。再学習後の別作業。**

Step 9.5 が出力する値を以下のファイルに反映する:

### 6.1 量子化パラメータ (`e2studio_CPU0/src/fall_detection_postprocess.h`)

```c
// #131 時点
#define BRANCH0_OUTPUT_SCALE      (0.10909886f)
#define BRANCH0_OUTPUT_ZERO_POINT (34)
#define BRANCH1_OUTPUT_SCALE      (0.10983318f)
#define BRANCH1_OUTPUT_ZERO_POINT (23)
```

再学習後は Step 9.5 出力を貼り付ける。

### 6.2 アンカー値 (`e2studio_CPU0/src/fall_detection_postprocess.c`)

```c
// #131 時点
// Branch 0 (6x6, stride 32): (50,119), (120,111), (157,166)
// Branch 1 (12x12, stride 16): ( 8, 20), ( 25, 54), ( 81, 64)
```

Phase 2 のクリーニング + augmentation で bbox 分布が変わっているため、Step 4 で再計算した値に必ず更新する。

### 6.3 tflite モデル (`dataset/models/yolo_fastest_person_darknet_int8.tflite`)

Phase 2 の INT8 モデルに差し替え。ファイル名は保持して、MCU 側の組み込み C 配列化 (別途手順) で反映。

## 7. 期待される改善効果 (Phase ごとの見込み)

本数値は darknet / YOLO-Fastest の一般的な改善知見からの**見込み**であり、実測値ではない。

| Phase | 改善内容 | mAP@0.50 寄与見込み | 備考 |
|---|---|---|---|
| 2A | データセットクリーニング | +2 ~ +5 pt | ラベルミス・低品質除去で上限が引き上がる |
| 2B | mosaic/mixup/cutmix + 色空間強化 | +3 ~ +7 pt | darknet 内蔵 augmentation は YOLOv4 で +3-5 pt 実績あり |
| 2C | max_batches 200k + cosine annealing | +3 ~ +5 pt | 100k 以降の停滞を解消、cosine で最終収束改善 |
| 2D | (今回は未実施) | - | Phase 3 で対応 |
| **合計 (楽観見込み)** | - | **+8 ~ +17 pt** | 35.13% -> 43-52% |

中間目標 50% を達成するには、Phase 2A-C の合計で +15 pt 以上が必要。
2A-C の重ね掛けで飽和する可能性もあり、Phase 2 完了時点で 45-50% 付近になることを想定する。

50% 未達だった場合に Phase 3 で検討する手段:
- データセット拡充 (家庭内見守り環境に特化したデータ収集)
- 入力解像度 224x224 化 (アリーナ制約との両立検証が必要)
- バックボーンのチャンネル数拡張 (計算量とトレードオフ)
- YOLO-Fastest V2 / YOLOX-Nano 等の別アーキテクチャへの乗り換え検討

## 8. リスクと注意事項

### 8.1 cfg 変更の darknet 互換性

- `policy=sgdr` は AlexeyAB darknet (Yolo-Fastest のベース) で対応済み
- `sgdr_cycle` / `sgdr_mult` は [net] セクション内で指定
- `mosaic` / `mixup` / `cutmix` は YOLOv4 系の AlexeyAB darknet で対応
- Yolo-Fastest リポジトリが古い fork の場合、これらのパラメータが無視される可能性あり
- **対処**: Step 6 の学習開始直後のログで `mosaic`, `mixup`, `cutmix`, `sgdr` が有効になっていることを確認する

### 8.2 学習時間の増加

- 200k iterations + mosaic/mixup は T4 で 7-10 時間想定
- Colab 無料枠 (12h 制限) を超える可能性があるため、**Colab Pro / Pro+ を推奨**
- マーカー + `_last.weights` による自動再開で分割実行は可能

### 8.3 過学習リスク

- 200k iteration は #131 (100k) の 2 倍で、データセットサイズ (~32k) に対して多めに見える
- 対策: cosine annealing による late-stage の低 LR、mosaic/mixup の強い正則化効果、Step 7 の `darknet detector map` で val mAP を確認
- `_best.weights` が 150k 前後で更新されなくなる場合、200k まで回さず early stop を検討

### 8.4 クリーニングの副作用

- 閾値設定によっては正常な画像も除去される可能性あり
- 退避先は `_removed_phase2/` で切り戻し可能
- dry-run 統計を必ず確認してから `--apply` を実行すること

### 8.5 アリーナ制約維持

- Phase 2 は cfg の [net] と augmentation のみ変更し、**モデル構造は #131 から変えない**
- よってアリーナサイズ 432KB、NPU 推論時間 5ms、単一 sub_0000 サブグラフは維持される見込み
- Step 10 の Vela 互換性確認で必ず検証する

## 9. 参考情報

- darknet Mosaic/Mixup/Cutmix 仕様: https://github.com/AlexeyAB/darknet/wiki/CFG-Parameters-in-the-%5Bnet%5D-section
- Yolo-Fastest ベースリポジトリ: https://github.com/dog-qiuqiu/Yolo-Fastest
- SGDR (Stochastic Gradient Descent with Warm Restarts): https://arxiv.org/abs/1608.03983
- #131 PR: https://github.com/grace2riku/mimamori-sense/pull/132
- #128 Phase 1 完了時の成果物レポート: `doc/report/f003_03f_yolo_fastest_person_training_investigation.md` (存在しない場合は #128 のコミット参照)

## 10. 承認フロー (Phase 2 完了判定)

Phase 2 完了の受け入れは以下で判定する (Issue #133 の受入条件):

- [ ] mAP@0.50 >= 50%
- [ ] Recall >= 60% (conf_thresh=0.25)
- [ ] Precision >= 60% (conf_thresh=0.25)
- [ ] 実機で conf_thresh=0.25 以上で人物を安定検出
- [ ] Ethos-U55 アリーナ 432KB 維持
- [ ] NPU 推論時間 5ms 以内維持
- [ ] 単一 NPU サブグラフ (sub_0000) 維持

受入条件未達の場合は、本ドキュメントの「7. 期待される改善効果」の Phase 3 候補手段を検討する。
