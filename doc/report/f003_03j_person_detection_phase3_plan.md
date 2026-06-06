# F-003-3j: YOLO-Fastest 人物検出モデル精度向上 Phase 3 方針ドキュメント

**対象 Issue**: #137
**前提 Issue**: #135 / PR #136 (Phase 2 学習: mAP@0.50 48.89%)
**フェーズ**: Phase 3 (mAP@0.50 48.89% -> 50%+ / Precision 49% -> 60%+ / Recall 52% -> 60%+)
**前ドキュメント**: `doc/report/f003_03h_person_detection_phase2_plan.md`

---

## 1. このドキュメントの位置付け / 前提

Issue #137 のうち **Phase A (改善方針の調査・選定)** と **Phase B (Colab 学習の準備)** までを本ドキュメントで扱う。
実機 (EK-RA8P1) 側のパラメータ更新 (Phase D) は、KPI 達成を確認した後に別タスクとして実施する。本ドキュメント 7 章に更新箇所のみ記載する。

- MCU 側 C/H コード (`fall_detection_postprocess.{c,h}`, `mera/*`) は本セッションでは変更しない
- 量子化パラメータ / アンカー値は再学習後にノートブックの Step 9.5 (cell 36) で自動出力される値に差し替える
- Phase E (`ai detect` 表示不整合修正など MCU 側軽微改善) は本 Issue では任意・別対応とする

### 1.1 Phase 2 の出発点数値 (本 Phase 3 のベースライン)

評価条件: `darknet detector map`, conf_thresh=0.25, IoU 50%, truth=3653

| 指標 | Phase 1 (#131) | **Phase 2 (#135)** | Phase 3 KPI |
|---|---|---|---|
| mAP@0.50 | 35.13% | **48.89%** | >= 50% |
| Precision | - | **49%** | >= 60% |
| Recall | - | **52%** | >= 60% |
| TP | - | **1909** | 増やす |
| FP | - | **1994** | 減らす (最重要) |
| FN | - | **1744** | 減らす |

Phase 2 は KPI に対し mAP で -1.11pt、Precision で -11pt、Recall で -8pt 未達。
**mAP はあと一歩だが、Precision/Recall が大きく未達**である点が Phase 3 の主課題。

---

## 2. FP/FN 分析と未達要因の整理

### 2.1 数値からの読み解き

- 全予測数 = TP + FP = 1909 + 1994 = 3903 件。うち約 51% が FP (誤検出)。
- 全正解数 (truth) = TP + FN = 1909 + 1744 = 3653 件。うち約 48% が取りこぼし (FN)。
- **FP と FN がほぼ同規模で両方多い**。これは「検出はたくさん出るが位置/クラス信頼度の品質が低い」典型パターン。
  - mAP@0.50 (48.89%) が Precision/Recall (49%/52%) と近い値で並ぶことから、PR 曲線が conf 全域で寝ている = スコア較正と局在化 (localization) の質が不足している。

### 2.2 Phase 2 までの施策と限界

| Phase 2 施策 | 効果 | Phase 3 で残る課題 |
|---|---|---|
| データクリーニング (2A) | 低品質画像/壊れラベル除去で上限引き上げ | 閾値が保守的。重複除去 hamming=4 は near-dup を取りこぼす可能性 |
| mosaic/mixup/cutmix (2B) | 多様性向上。ただし **小入力 192px では mosaic が過度に小物体化** | 192px で mosaic を全 iteration ON だと小さい人物が潰れ FN を増やしうる |
| max_batches 200k + SGDR (2C) | 後半収束改善 | 学習延長だけでは PR 曲線の質は頭打ち |
| アンカー K-means (2D 一部) | bbox 分布に適合 | クラスタが train 全体平均。小物体側のアンカー (8,20 等) が小人物に過小の可能性 |

### 2.3 未達要因の仮説 (優先度順)

| # | 仮説 | 主に効く指標 | Phase 3 での対処 |
|---|---|---|---|
| H1 | スコア較正不足 (obj スコアが全域で低く、conf=0.25 で FP/FN が両立) | Precision/Recall | NMS 前の conf 評価点見直し + 学習延長より loss バランス調整 |
| H2 | mosaic 常時 ON で小物体が潰れ、背景断片が人物として残り FP 化 | FP / FN 両方 | mosaic を確率制御 (mosaic_min_ratio / 後半 OFF) + cutmix を切る |
| H3 | hard negative (人物に似た背景: マネキン/影/家具) を区別できず FP | FP | hard negative mining で背景パッチを negative サンプルとして追加学習 |
| H4 | クリーニング閾値が保守的で低品質/重複が残存 | mAP 上限 | 重複 hamming 閾値・blur 閾値の再評価 (緩めすぎないよう dry-run 必須) |
| H5 | アンカーが train 平均で、小人物/縦長人物への適合不足 | FN (localization) | アンカー再計算を全 split (train) で再実施 + 分布確認 |
| H6 | 入力 192px の情報量不足 | FN (小物体) | 224px 化は **アリーナ 432KB を超過する高リスク**。Phase 3 では非採用 (8章 R1) |

---

## 3. 改善案の評価マトリクス

期待効果 pt は darknet / YOLO-Fastest の一般知見からの**見込み**であり実測ではない。
「アリーナ等制約への影響」はモデル構造 (層・チャネル・入力解像度) を変えるかどうかで判定する。
**構造不変なら 432KB / 単一 sub_0000 / 5ms / FLASH 960KB は維持される**(Step 10 Vela で再確認)。

| 案 | 分類 | 期待効果 (mAP/Prec/Rec) | コスト | アリーナ等制約への影響 | 採否 |
|---|---|---|---|---|---|
| A. hard negative mining | データ | FP -300~-700, Prec +5~+10pt | 中 (スクリプト新規 + データ生成) | **なし** (画像追加のみ、構造不変) | **採用 (主軸)** |
| B. クリーニング閾値見直し | データ | mAP +1~+3pt | 低 (引数調整) | なし | **採用** |
| C. mosaic/cutmix 抑制 (確率制御) | 学習 | FP -100~-400, Rec +3~+6pt | 低 (cfg 変更) | なし | **採用** |
| D. アンカー再最適化 (再 K-means) | 学習 | Rec +2~+5pt (localization) | 低 (既存 Step 4) | なし | **採用** |
| E. loss バランス調整 (iou/obj/cls) | 学習 | mAP +1~+3pt | 中 (要試行錯誤) | なし | **採用 (控えめに)** |
| F. Phase 2 best からの finetune 起点化 | 学習 | 収束安定・時間短縮 | 低 (マーカー運用) | なし | **採用** |
| G. データ量増加 (COCO val/test 追加) | データ | mAP +2~+5pt | 高 (取込・整形・再分割) | なし (構造不変) | 保留 (Phase 3.5) |
| H. 解像度 192->224/256 | 構造 | Rec +5~+10pt (小物体) | 高 | **高: アリーナ 432KB 超過確実 (~1.36倍)。要 Vela 再評価** | **非採用** |
| I. backbone チャネル増 | 構造 | mAP +3~+8pt | 高 | **高: アリーナ/FLASH 増。単一 sub_0000 崩れる恐れ** | **非採用** |
| J. branch 追加 (小物体 P2) | 構造 | Rec +3~+7pt | 高 | **高: アリーナ増・推論時間増・Vela 再評価必須** | **非採用** |
| K. augmentation 追加 (blur/cutout) | 学習 | Prec +1~+3pt (頑健性) | 低 | なし | 任意 (D 採用後に余裕あれば) |

### 3.1 アーキテクチャ変更を非採用とする根拠

Phase 2 時点で **アリーナは 442368 バイト (432KB) 上限ピッタリ** (`sub_0000_net1_tensors.c` の `_split_1_scratch` = 442368)。
解像度・チャネル・branch のいずれを増やしてもアクティベーションテンソルが増え、上限を超える。
超えた場合 Vela がサブグラフを分割 (sub_0000 単一の制約を破る) するか、そもそも配置不能になる。
**よって Phase 3 は「モデル構造を変えない範囲 (データセット + 学習設定)」での改善を最優先とする**(Issue 本文の方針と一致)。
構造変更が必要になるのは Phase 3 施策を尽くしても KPI 未達の場合のみで、その際は Issue 本文 Phase E のアリーナ余裕確保とセットで別 Issue 化する。

---

## 4. 確定方針 (採用施策と実施順)

Phase 3 は **「FP を減らす (Precision を上げる)」を最優先軸**に据える。FP=1994 の削減が Precision 49%->60% への最短路であり、hard negative mining が中核。同時に mosaic 抑制・アンカー再最適化で Recall (FN) も底上げする。

### 採用施策 (実施順)

1. **3B クリーニング閾値見直し** (前処理)
   - 重複検出を `hamming=3` に厳格化 (near-dup の取りこぼしを減らす)。
   - blur 閾値は `50` を維持 (緩めず)。dry-run で除去率が前回 (#135) と乖離しないか確認。
2. **3A hard negative mining** (データ追加) ← **本 Phase の主軸**
   - Phase 2 best weights で train 画像を推論し、**正解 bbox と重ならない高スコア検出 (= FP) が出たパッチ**を抽出。
   - 抽出パッチを「人物なし (空ラベル .txt)」の negative 画像として train に追加。
   - 追加上限を設け (例: 元 train の 15% まで)、データ偏りを防ぐ。
3. **3D アンカー再最適化** (学習設定)
   - クリーニング + negative 追加後の train 全体で K-means 再計算 (既存 Step 4 をそのまま再実行)。
   - 小物体側アンカー (mask=0,1,2) が極端に小さくないか分布を目視確認。
4. **3C mosaic/mixup/cutmix 全無効 + loss 微調整** (cfg 変更)
   - `cutmix=0` (192px では弊害が大きい)。
   - **`mosaic=0` / `mixup=0`**: hard negative (bbox ゼロの negative 画像) を train に追加した結果、OpenCV 無効ビルドの darknet で mosaic が負例画像を合成する際に SIGSEGV (segfault) するため、Phase 3 では mosaic/mixup を無効化する (8章 R7 参照)。hard negative が主軸であり、mosaic/mixup は副次的なので無効化のデメリットは限定的。
   - loss は `iou_loss=ciou` を明示 (localization 改善)、`obj_normalizer` を弱める方向は **まず既定のまま**で 1 周回し、未達なら次周で調整 (過調整リスク回避)。
5. **3F Phase 2 best からの finetune 起点化** (運用)
   - スクラッチではなく Phase 2 final/best を起点に finetune。`max_batches` は **100,000** に短縮 (finetune なので 200k 不要、Colab 時間も節約)。
   - 新マーカー `.issue137_started` で再開制御。

### 各施策の狙う指標

| 施策 | FP | FN | mAP | Precision | Recall |
|---|---|---|---|---|---|
| 3B クリーニング厳格化 | - | - | ↑ | ↑ | - |
| 3A hard negative | ↓↓ | - | ↑ | ↑↑ | - |
| 3D アンカー再最適化 | - | ↓ | ↑ | - | ↑ |
| 3C mosaic 抑制 | ↓ | ↓ | ↑ | ↑ | ↑ |
| 3F finetune 起点 | 安定化 | 安定化 | - | - | - |

楽観見込み合計: mAP +3~+8pt (49->52~57%)、Precision +8~+15pt (49->57~64%)、Recall +5~+11pt (52->57~63%)。
KPI 50/60/60 はギリギリ射程内だが、特に Precision の改善余地が大きい hard negative mining の出来に依存する。

---

## 5. Phase B 学習手順 (Colab、cfg/スクリプト変更点の具体値)

### 5.1 cfg 変更点 (Phase 2 -> Phase 3)

cell 13 (手書きテンプレート) と cell 14 (base-cfg patcher) の両方に反映する。

| パラメータ | Phase 2 (#135) | **Phase 3 (#137)** | 理由 |
|---|---|---|---|
| `max_batches` | 200000 | **100000** | Phase 2 best 起点の finetune のため短縮 |
| `cutmix` | 1 | **0** | 192px で弊害大 (背景断片が人物化 -> FP) |
| `mosaic` | 1 | **0** | hard negative (負例) との併用で darknet が SIGSEGV するため無効 (8章 R7) |
| `mosaic_min_ratio` | (なし) | 0.2 (無効時は不使用) | mosaic=0 のため実質未使用 |
| `mixup` | 1 | **0** | mosaic と同様、負例画像合成での crash 回避のため無効 |
| `policy` | sgdr | sgdr (維持) | finetune でも cosine restart 有効 |
| `burn_in` | 4000 | **1000** | finetune 起点なので warmup 短縮 |
| `[yolo] iou_loss` | (既定) | **ciou** | localization 改善 (FN/位置質) |
| `jitter` / `saturation` / `exposure` / `hue` | 0.5/2.0/2.0/.15 | 維持 | Phase 2 のまま |

> NOTE: `mosaic_min_ratio` と `iou_loss=ciou` は AlexeyAB darknet 系で対応。古い fork では無視される可能性があるため、Step 6 開始直後のログで反映を確認する (8章 R3)。

### 5.2 新規スクリプト: hard negative mining

`dataset/scripts/hard_negative_mining.py` を新規追加 (本セッションで作成済み)。

処理概要:
1. Phase 2 best weights + cfg で `train` 画像を darknet 推論 (または Step 8 の TFLite でも可。darknet 経路を既定とする)。
2. 各検出を正解ラベルと IoU 照合し、**IoU < 0.1 かつ conf >= しきい値 (既定 0.3)** の検出を FP とみなす。
3. FP が存在する画像を「hard negative 候補」として、`<dataset>/hard_negatives/` に画像コピー + **空ラベル .txt** を生成。
4. `--apply` 時のみ `images/train` / `labels/train` に追記し、`train.txt` を再生成。
5. デフォルト dry-run / 退避は `_removed_phase2` と同様に切り戻し可能 (追加分は `hard_negatives/` に原本を残す)。

運用ポリシー (Phase 2 作法踏襲):
- デフォルト dry-run (統計のみ)
- `--apply` で初めて train に追加
- `--max-add-ratio 0.15` で追加上限を元 train の 15% に制限
- レポートを `phase3_hardneg_report.json` に出力

### 5.3 ノートブック変更点 (Phase 3)

| cell | 変更内容 |
|---|---|
| 0 (header) | Phase 3 サマリ表を追記 (cfg 差分、hard negative、finetune 起点) |
| 10 (クリーニング) | dry-run の `--dup-hamming-threshold` を `4 -> 3` に。コメントに Phase 3 注記 |
| 10.6 (新規) | hard_negative_mining.py を dry-run -> `--apply` で実行し train.txt 再生成 |
| 13 / 14 (cfg) | 5.1 表の Phase 3 値に更新 (`max_batches=100000`, `cutmix=0`, `mosaic_min_ratio=.2`, `burn_in=1000`, `iou_loss=ciou`) |
| 22 (Step6 md) | finetune 起点・新マーカー・100k への変更を説明 |
| 23 (重み選択) | マーカー `.issue133_started` -> **`.issue137_started`**。Phase 3 初回は **Phase 2 final/best** を起点に finetune。旧 `_last.weights` は無視 |

### 5.4 ユーザー実行前チェックリスト

- [ ] Colab ランタイムが GPU (T4/L4 以上)
- [ ] `/content/drive/MyDrive/fall_detection_dataset.zip` が存在
- [ ] `/content/drive/MyDrive/yolo_fastest_darknet_person/dataset_cleaning.py` が最新
- [ ] `/content/drive/MyDrive/yolo_fastest_darknet_person/hard_negative_mining.py` を新規アップロード
- [ ] Phase 2 の `yolo-fastest-person-192_final.weights` (または `_best.weights`) が Drive backup に配置済み (finetune 起点)
- [ ] Drive 上の `.issue133_started` は残しても可 (Phase 3 は別マーカー `.issue137_started` を使う)
- [ ] 旧 `_last.weights` は Phase 3 初回では無視されるが、念のため backup 内の `_last.weights` を手動退避すると安全

### 5.5 実行順序

1. Step 1-2: darknet セットアップ、データセット展開 (Phase 2 と同じ)
2. Step 2.5: dataset_cleaning.py (hamming=3 で dry-run -> apply)
3. **Step 2.6 (新規)**: hard_negative_mining.py (dry-run で FP 統計 -> apply で train 追加)
4. Step 3: cfg 生成 (Phase 3 値)
5. Step 4: アンカー再計算 (negative 追加後の train で再 K-means)
6. Step 5: 事前学習重み (今回は Phase 2 best が起点なので backbone は予備)
7. Step 6: 学習 (100k iter finetune、`.issue137_started` 制御)
8. Step 7: `darknet detector map` で mAP/Precision/Recall (conf_thresh=0.25)
9. Step 8-9: TFLite INT8 変換
10. Step 9.5: MCU パラメータ自動出力
11. Step 10: Vela 互換性確認 (アリーナ 432KB / 単一 sub_0000 維持を必ず確認)
12. Step 11: 成果物ダウンロード

学習時間目安: T4 で 100k finetune は 3.5-5 時間、L4/A100 で 1.5-3 時間。

---

## 6. KPI 判定基準と未達時の分岐

### 6.1 判定基準 (Issue #137 受入条件)

評価は `darknet detector map`, conf_thresh=0.25, IoU 50% で実施:

- [ ] mAP@0.50 >= 50%
- [ ] Precision >= 60% (conf_thresh=0.25)
- [ ] Recall >= 60% (conf_thresh=0.25)
- [ ] Ethos-U55 アリーナ 432KB 以下を維持 (Step 10 Vela)
- [ ] NPU 単一サブグラフ (sub_0000) 維持
- [ ] NPU 推論時間 5ms 以内を維持
- [ ] CPU0 FLASH 960KB 以内を維持
- [ ] 実機で人物を安定検出 (Phase 2 同等以上)

### 6.1.1 Phase 3 round1 実測結果 (2026-06)

| 指標 | Phase 2 | **Phase 3 round1** | KPI | 判定 |
|---|---|---|---|---|
| mAP@0.50 | 48.89% | **52.22%** | >=50% | ✅ 達成 |
| Precision | 49% | **68%** | >=60% | ✅ 達成 |
| Recall | 52% | **49%** | >=60% | ❌ 未達 (低下) |
| TP/FP/FN | 1909/1994/1744 | 1826/863/1937 | - | FP -57% / FN +11% |

評価条件: `darknet detector map`, conf_thresh=0.25, IoU50%, unique_truth=3763。

**分析**: hard negative mining (max-add-ratio 0.15) が効きすぎ、FP を大幅削減・Precision を 68% まで押し上げた反面、検出を抑制する方向に寄り Recall が低下した。見守り製品では FN (見逃し) が致命的で Recall を最優先すべきため、Precision の余剰 (68% vs 目標60%) を Recall に振り戻す **round2** を実施する。

### 6.1.2 Phase 3 round2 方針 (Recall 改善再学習)

- `--max-add-ratio` 0.15 -> **0.08** (負例を減らし過剰抑制を緩和)
- `--fp-conf-threshold` 0.3 -> **0.45** (自信のあるFPのみ負例化。境界的な真の人物を負例にしない)
- mosaic/mixup は 0 維持 (R7 の segfault 回避)
- 起点は **Phase 2 best** から再 finetune (round1 の抑制学習を引きずらない)。Step 2.6 は冪等化済み (既存 hardneg_* を除去してから再 mining)
- round1 weights は `*-phase3r1.weights` として退避し、2/3 KPI 達成モデルとして保全

### 6.2 未達時の分岐

| 状況 | 次アクション |
|---|---|
| 全 KPI 達成 | Phase D (MCU 反映) へ。7 章のガイドに従い別タスク化 |
| mAP/Recall 達成・Precision 未達 | hard negative の追加比率を上げる (`--max-add-ratio 0.25`) + conf しきい値を下げて FP をより広く収集して再学習 |
| mAP/Precision 達成・Recall 未達 | アンカー再評価 + mosaic を一段抑制 (mosaic 後半 OFF) + データ量増加 (案 G: COCO val 追加) |
| いずれも僅差未達 (mAP 48-50% 等) | loss バランス調整 (案 E) を本格化。obj_normalizer / cls_normalizer を 1 段ずつ振って再学習 |
| 構造変更なしで頭打ち | 別 Issue で解像度 224px or branch 追加を検討。**必ずアリーナ余裕確保 (Phase E) とセット**で Vela 再評価 |

---

## 7. MCU 側反映ガイド (Phase D、KPI 達成時のみ)

**本セッションでは実施しない。KPI 達成確認後の別タスク。**
Step 9.5 (cell 36) が出力する値を以下に反映する。現在 (Phase 2 デプロイ済み) の値も併記する。

### 7.1 量子化パラメータ

更新先: `e2studio_CPU0/src/ai_application/fall_detection/fall_detection_postprocess.h`

```c
/* Phase 2 デプロイ済みの現行値 (Phase 3 で Step 9.5 出力に差し替え) */
#define POSTPROC_BRANCH0_SCALE       (0.11560755f)  /* StatefulPartitionedCall:0 (6x6) */
#define POSTPROC_BRANCH0_ZERO_POINT  (31)
#define POSTPROC_BRANCH1_SCALE       (0.13658379f)  /* StatefulPartitionedCall:1 (12x12) */
#define POSTPROC_BRANCH1_ZERO_POINT  (49)
```

### 7.2 アンカー値

更新先: `e2studio_CPU0/src/ai_application/fall_detection/fall_detection_postprocess.c` (`fall_detection_postprocess_init` 内)

```c
/* Phase 2 デプロイ済みの現行値 (Phase 3 で Step 4 / Step 9.5 出力に差し替え) */
/* Branch 0 (6x6, stride 32): (89,66) (106,127) (167,155) */
/* Branch 1 (12x12, stride 16): (8,20) (25,53) (46,114) */
```

クリーニング + hard negative 追加で bbox 分布が変わるため、Step 4 の再計算値に必ず更新する。

### 7.3 tflite / mera 再生成

- `dataset/models/yolo_fastest_person_darknet_int8.tflite` を Phase 3 INT8 モデルに差し替え。
- `e2studio_CPU0/src/ai_application/fall_detection/mera/*.{c,h}` を RUHMI/MERA 再変換した生成物で上書き
  (`sub_0000_net1_*`, `model_net1.*`, `model_io_data.*`, `sub_0000_io_data.*`)。
- 再変換後、`sub_0000_net1_tensors.c` の `_split_1_scratch` が **442368 以下**であることを確認 (アリーナ維持)。
- e2 studio でビルド成功 + EK-RA8P1 実機 `ai detect` 動作確認。

### 7.4 (任意・別対応) Phase E

- `ai cmd` の `Total detections` と `Last candidates/detections` の参照タイミング統一 (`fall_detection_postprocess.h` の `last_candidates`/`last_detections` 周辺)。
- アリーナ上限ピッタリ状態の余裕確保。**構造変更を伴う Phase 3 再試行を行う場合は必須**。

---

## 8. リスクと注意事項

### R1. アリーナ 432KB 上限 (最重要)

Phase 3 はモデル構造を変えないため、アリーナ 432KB / 単一 sub_0000 / 5ms / FLASH 960KB は維持される見込み。
ただし **Step 10 の Vela 互換性確認で必ず検証**する。万一サブグラフが分割された場合は cfg 変更が構造に波及していないか (filters/層が意図せず変わっていないか) を確認する。

### R2. hard negative mining の過剰追加

- 空ラベル画像を入れすぎると obj スコアが全体に下がり Recall を損なう。`--max-add-ratio 0.15` を既定とし、dry-run の追加枚数を必ず確認。
- FP 判定の IoU しきい値 (既定 0.1) と conf しきい値 (既定 0.3) を緩めすぎると、真の人物まで negative にしてラベルノイズ化する。dry-run のサンプル画像を目視推奨。

### R3. darknet cfg 互換性

- `mosaic_min_ratio` / `iou_loss=ciou` は AlexeyAB darknet 対応。Yolo-Fastest の fork が古い場合は無視される。
- Step 6 学習開始直後のログで `mosaic`, `mixup`, `ciou`, `sgdr` が有効になっているか確認 (無視されても学習は走るが効果が出ない)。
- `cutmix=0` は確実に効く (既定の augmentation を切るだけ)。

### R4. finetune 起点の重み不一致

- Phase 3 で `cutmix`/`mosaic_min_ratio` を変えても**ネットワーク構造 (層・filters) は不変**のため、Phase 2 best からの finetune は問題なく重みをロードできる。
- アンカー値を変えた場合でも [yolo] 層の filters (=18) は変わらないため重みロード互換は保たれる。

### R5. クリーニング閾値の副作用

- hamming=3 への厳格化で重複除去が増える。似た姿勢の有用サンプルまで消すと Recall を損なうため、dry-run の redundant 件数が Phase 2 から大きく増えないか確認。増えすぎる場合は hamming=4 に戻す。

### R7. hard negative + mosaic/mixup の SIGSEGV (Phase 3 実学習で発生)

- OpenCV 無効ビルドの darknet (Yolo-Fastest fork) で `mosaic=1`/`mixup=1` のまま hard negative (bbox ゼロの負例) を train に含めると、最初のバッチ読み込み (`Create N permanent cpu-threads` 直後) で SIGSEGV (終了コード -11) する。Phase 2 は全画像に人物がいたため顕在化しなかった。
- **対処**: Phase 3 cfg で `mosaic=0` / `mixup=0` に設定する (cfg 生成セル 15/16 で対応済み)。負例画像を mosaic 合成しないことで回避する。
- 関連: finetune 起点重み (phase2, 200k iter 済み) を `max_batches=100000` で学習する際は seen カウンタが上限超過となるため、学習コマンドに `-clear` を付与してカウンタを 0 リセットする (付与しないと学習 0 回 + double free で終了コード -6)。

### R6. 評価条件の固定

- KPI は **conf_thresh=0.25** で評価する (Phase 2 と同条件)。MCU 側の `POSTPROC_CONFIDENCE_THRESHOLD` は 0.5 だが、これは実機運用閾値であり mAP 評価条件 (0.25) とは別物。混同しないこと。

---

## 9. 変更ファイル一覧 (Phase A/B コミット対象)

| パス | 種別 | 内容 |
|---|---|---|
| `doc/report/f003_03j_person_detection_phase3_plan.md` | 新規 | 本ドキュメント (Phase A 成果物) |
| `dataset/scripts/hard_negative_mining.py` | 新規 | Phase 3 hard negative mining (dry-run/apply、切り戻し可) |
| `dataset/scripts/train_yolo_fastest_darknet_colab.ipynb` | 更新 | Phase 3 対応 (header / cfg / マーカー / hard negative ステップ) |

MCU 側 C/H、`configuration.xml`、`ra_gen/`、`ra/fsp/` は **本セッションでは未変更**。

---

## 10. 参考情報

- Phase 2 引き継ぎ: `doc/report/f003_03h_person_detection_phase2_plan.md`
- Phase 2 PR: #136 / Issue #135
- darknet CFG パラメータ (mosaic/cutmix/iou_loss): https://github.com/AlexeyAB/darknet/wiki/CFG-Parameters-in-the-%5Bnet%5D-section
- Hard negative mining (object detection): OHEM (https://arxiv.org/abs/1604.03540) の考え方を darknet 向けに簡略化
- Yolo-Fastest ベース: https://github.com/dog-qiuqiu/Yolo-Fastest
- アリーナ実測値: `e2studio_CPU0/src/ai_application/fall_detection/mera/sub_0000_net1_tensors.c` (`_split_1_scratch` = 442368)
