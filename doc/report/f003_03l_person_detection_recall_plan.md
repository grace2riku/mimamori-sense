# F-003-3l: YOLO-Fastest 人物検出モデル Recall 改善 方針ドキュメント

**対象 Issue**: #148
**前提 Issue**: #137 / PR #146 (Phase 3 学習 2 ラウンド: mAP 52.2% / Precision 68% / Recall 49%)
**フェーズ**: Phase 4 (Recall 49-52% -> **60%+**、Precision >= 60% と mAP >= 50% は維持)
**前ドキュメント**: `doc/report/f003_03j_person_detection_phase3_plan.md`

---

## 1. このドキュメントの位置付け / 前提

Issue #148 のうち **Phase A (改善方針の調査・選定)** と **Phase B (Colab 学習の準備)** までを本ドキュメントで扱う。
実際の Colab 学習 (L4 で約 2.5 時間) と RUHMI/MERA 変換、実機 (EK-RA8P1) 反映はユーザーが後で実行する。

- MCU 側 C/H コード (`e2studio_CPU0/src/ai_application/**`, `mera/*`)、`configuration.xml`、`ra_gen/`、`ra/fsp/` は **本セッションでは一切変更しない**
- 本 Issue の成果物は「方針書 + 診断スクリプト + データ拡充スクリプト + 学習ノートブックの更新」
- MCU 反映 (量子化パラメータ・アンカーの差し替え) は KPI 達成後の別作業。手順は `f003_03j` 7 章がそのまま使える

### 1.1 出発点数値 (Phase 3 の 3 回の実測)

評価条件: `darknet detector map`, conf_thresh=0.25, IoU 50%

| 指標 | Phase 2 (#135) | Phase 3 round1 (#137) | Phase 3 round2 (#137) | Phase 4 KPI (#148) |
|---|---|---|---|---|
| mAP@0.50 | 48.9% | **52.2%** | 50.9% | >= 50% (維持) |
| Precision | 49% | **68%** | 57% | >= 60% (維持) |
| Recall | 52% | **49%** | 51% | **>= 60%** |
| TP/FP/FN | 1909/1994/1744 | 1826/863/1937 | 2153/1609/2042 | - |
| unique_truth | 3653 | 3763 | 4195 (val 未クリーニング) | 固定する (4.1.4) |

出典: `doc/report/f003_03j_person_detection_phase3_plan.md` 1.1 / 6.1.1 / 6.1.3。
**採用モデルは round1** (`*-phase3r1.weights`)。本 Issue はこの round1 を超える Recall を目指す。

---

## 2. #137 で確定した事実と本 Issue の起点

### 2.1 3 回の実測はトレードオフ・フロンティア上を滑っただけ

Precision + Recall の和は 3 回とも 101〜117 の範囲に収まり、**hard negative の量を振っても
Precision と Recall を交換しているだけ**だった (`f003_03j` 6.1.3)。

| 実測 | Precision | Recall | P+R |
|---|---|---|---|
| Phase 2 | 49% | 52% | 101 |
| round1 (neg 15%) | 68% | 49% | 117 |
| round2 (neg 8%) | 57% | 51% | 108 |

hard negative は FP (Precision) にしか効かない。**Recall の律速要因は誤検出ではなく
「モデルが人を見つける力」そのもの**であり、hard negative 調整の延長では Recall 60% に
構造的に到達できない、というのが #137 の最終結論。

### 2.2 Recall 律速の候補 (本 Issue で切り分ける)

| # | 候補 | 本 Issue での扱い |
|---|---|---|
| R-a | **データ量・多様性の不足** (特に倒れ姿勢) | **案G で対処 (最優先)**。構造不変なのでアリーナに影響しない |
| R-b | **入力解像度 192px の情報量不足** (小さい人物が潰れる) | 案H。アリーナ超過確定のため Phase E とセット (3.1 / 7 章) |
| R-c | **モデル容量不足** (backbone チャネル / branch 数) | 案I/J。同上 |
| R-d | 学習不足 (iteration 数) | round1/round2 とも 100k finetune で loss は収束済み。優先度低 |

R-a と R-b/R-c は**同時には切り分けられない**ため、まず構造を変えない R-a (案G) を実施し、
それでも Recall が伸びない場合に R-b/R-c を Phase E とセットで別途検討する、という順序を採る。

---

## 3. 改善案の評価マトリクス (案G/H/I/J の再評価)

期待効果は一般知見からの**見込み**であり実測ではない。
「アリーナ等制約への影響」はモデル構造 (層・チャネル・入力解像度) を変えるかどうかで判定する。

| 案 | 分類 | 期待効果 (Recall) | コスト | アリーナ等制約への影響 | 採否 |
|---|---|---|---|---|---|
| **G. データ拡充 (COCO 追加 + 倒れ姿勢優先)** | データ | **+3〜+8pt** | 中 (取込スクリプト + 再学習 2.5h) | **なし** (構造不変) | **採用 (最優先)** |
| G'. 家庭内見守り環境の自前データ収集・アノテーション | データ | +2〜+6pt | 高 (撮影・アノテーション工数) | なし | 保留 (工数が本 Issue の枠外) |
| H. 解像度 192 -> 224 / 256 | 構造 | +5〜+10pt (小物体) | 高 | **アリーナ 1.36 倍 / 1.78 倍。432KB 超過確定** | **条件付き (Phase E とセット)** |
| I. backbone チャネル増 | 構造 | +3〜+8pt | 高 | アリーナ・FLASH 増。単一 sub_0000 が崩れる恐れ | **条件付き (Phase E とセット)** |
| J. 小物体用 branch (P2) 追加 | 構造 | +3〜+7pt | 高 | アリーナ増・推論時間増。Vela 再評価必須 | **条件付き (Phase E とセット)** |
| K. hard negative の再調整 | データ | **±0 (無効)** | 低 | なし | **非採用** (#137 6.1.3 で無効と確定) |

### 3.1 アリーナ制約の実測値と案H/I/J の定量見積り

現行モデル (192x192x3 INT8, YOLO-Fastest V1) の MERA 生成物が要求するアリーナは **442,368 バイト**。

| 実測の出所 | 値 |
|---|---|
| `e2studio_CPU0/src/ai_application/fall_detection/mera/sub_0000_net1_tensors.h:11` | `#define kArenaSize_sub_0000_net1 442368` |
| 同 `mera/sub_0000_net1_tensors.c:6` | `{ "_split_1_scratch", 4, 442368, "ARENA", 0x0 }` |
| 同 `mera/sub_0000_net1_invoke.c:18` | `uint8_t sub_0000_net1_arena[442368];` |
| `e2studio_CPU0/src/ai_application/fall_detection/wrapper.h:69` (コメント) | `Arena size: 432 KiB (442368 bytes)` |

442,368 = 192 x 192 x 12 で、**入力画素数にちょうど比例する形**になっている。
アクティベーションが入力面積に比例すると仮定した場合の見積りは以下のとおり。

| 入力 | アリーナ見積り | 現行比 | 432KB (442,368B) との差 | FLASH (重み) |
|---|---|---|---|---|
| 192x192 (現行) | 442,368 B (432 KiB) | 1.00 | ちょうど | 515,112 B (`dataset/models/yolo_fastest_person_darknet_int8.tflite`) |
| 224x224 | 約 602,112 B (588 KiB) | **1.361** | **+159,744 B 超過** | ほぼ不変 (畳み込み重みは入力解像度に依存しない) |
| 256x256 | 約 786,432 B (768 KiB) | **1.778** | **+344,064 B 超過** | ほぼ不変 |

> **不確実性の明示**: この見積りは「アリーナ = 入力画素数に比例」という仮定に基づく概算である。
> Vela はテンソルの生存区間を見て再利用するため、実際の配置量は比例からずれる。
> **実施する場合は必ず Vela を実行して実測すること** (7 章)。
> また 442,368 バイトが「上限ピッタリ」であるという `f003_03j` 3.1 の記述は、
> 現行生成物が要求する値であって、EK-RA8P1 で使える SRAM の上限そのものを実測したものではない。
> Phase E ではまずこの上限の根拠 (利用可能 SRAM とリンカ配置) を確定させる必要がある。

**結論**: 案H/I/J は **アリーナ余裕確保 (Phase E) とセットでなければ着手できない**。
本 Issue では構造不変の案G を先行させる。

---

## 4. 確定方針

**Phase 4 は Round A / Round B の 2 段構えで実施する。**
hard negative は Recall に無力であることが確定しているため両ラウンドとも **無効化**し、
変数を 1 つに絞る (交絡を避ける)。

| ラウンド | 変更点 | 狙い | 学習 |
|---|---|---|---|
| **Round A** | **`_aug` ラベルのクラス ID 正規化のみ** (データ量は変えない) | 学習データの 57% を占める破損ラベルの影響を単独で測る (8 章 R8) | 2.5h |
| **Round B** | Round A + **COCO 追加 5,182 枚** | データ拡充によるフロンティア拡張 (案G) | 2.5h |

> **Round A を先に置く理由**: R8 の破損は train の 57% に及び、
> 横長 bbox として誤読される可能性がある。これを抱えたままデータを増やすと、
> Recall が改善しても「破損修正の効果」と「データ増量の効果」を区別できない。
> **Round A だけで KPI を満たす可能性もあり、その場合 Round B は不要になる。**

### 4.1 案G の具体設計 (Round B の内容)

#### 4.1.1 ① 倒れ姿勢サブセットの recall 診断 (系統的見逃しの確認方法)

`darknet detector map` は全 GT をまとめた Recall しか出さないため、
**GT をサブグループに分けてグループ別 Recall を出す**診断スクリプトを新規追加した
(`dataset/scripts/fallen_pose_recall_eval.py`)。

サブセットの切り出し基準:

| 軸 | 区分 | 判定 |
|---|---|---|
| 姿勢 | `fallen` / `ambiguous` / `upright` | bbox のアスペクト比 AR = w/h。`AR >= 1.2` を fallen、`AR <= 0.8` を upright、その間を ambiguous |
| サイズ | `small` / `medium` / `large` | 正規化 bbox 面積 w*h が `< 0.01` / `0.01-0.10` / `>= 0.10` |
| ソース | `coco` / `rf` / `cocoext` / `hardneg` | ファイル名の接頭辞。`rf_` = Roboflow Fall Detection 由来 (転倒データセット) |

- AR による姿勢判定は `quality_check.py:167-168` が既に採用している区分 (portrait / landscape) の拡張である。
  ただし同スクリプトは**正規化された** w/h をそのまま使っており、画像自体のアスペクト比の分だけ歪む。
  本スクリプトは既定で画像の実寸を読み `AR_px = (w_norm/h_norm) x (W/H)` に補正する
  (`--no-image-size` で無効化可)。
- **AR は倒れ姿勢の近似**である。正面から見た仰臥位など「横長にならない倒れ姿勢」は upright/ambiguous に落ちる。
  数値は「横長な人物の Recall」と解釈する。より厳密にやるなら COCO の
  `person_keypoints_*.json` から肩-腰軸の傾きを使う方法があるが、本 Issue では採らない。

**現状の val (`dataset/merged`, クリーニング前) の構成 (実測)**:

```
python dataset/scripts/fallen_pose_recall_eval.py --dataset dataset/merged --split val --dry-run
```

| 軸 | 区分 | GT 数 | 構成比 |
|---|---|---|---|
| 姿勢 | upright | 2,288 | 54.5% |
| | ambiguous | 1,107 | 26.4% |
| | **fallen** | **800** | **19.1%** |
| サイズ | large | 2,027 | 48.3% |
| | medium | 1,183 | 28.2% |
| | small | 985 | 23.5% |
| ソース | coco | 2,548 | 60.7% |
| | rf (Roboflow Fall) | 1,647 | 39.3% |
| 合計 | | **4,195** | ラベルファイル 2,368 / 空ラベル 0 |

> この 4,195 は `f003_03j` 6.1.3 が記録した **round2 の unique_truth = 4195 と一致**する。
> すなわちローカルの `dataset/merged` は**クリーニング前**の状態であり、
> Step 2.5 のクリーニング適用後に round1 と同じ 3,763 になる、という対応関係が確認できる。
> これは 4.1.4 の評価条件固定の根拠でもある。

**判定基準**:

| 診断結果 | 解釈 | 次アクション |
|---|---|---|
| `fallen` の Recall が `upright` より 5pt 以上低い | 倒れ姿勢の系統的な見逃しあり | 案G で倒れ姿勢を重点的に追加 (本方針どおり) |
| 姿勢差なし・`small` の Recall だけ低い | 律速は解像度 (R-b) | 案H + Phase E を優先度上げ |
| 姿勢差なし・サイズ差なし | 律速はモデル容量 (R-c) またはデータ量全般 | 案G を量で押す / 案I を検討 |
| `fallen` の GT 数 < 100 | 統計的に判定不能 | まず母数を増やしてから再診断 |

現状 val の fallen GT は 800 件あるため判定可能。

#### 4.1.2 ② COCO からの人物データ追加取り込み

新規スクリプト `dataset/scripts/expand_coco_person.py` を追加した。

**未使用の候補プール (実測)**:

```
python dataset/scripts/expand_coco_person.py --coco-split both --max-add 6000
```

| ソース | 画像 | 人物あり | 既使用で除外 | bbox 全落ちで除外 | **候補** | うち倒れ姿勢を含む |
|---|---|---|---|---|---|---|
| `instances_val2017.json` | 5,000 | 2,693 | 0 | 40 | **2,653** | 497 |
| `instances_train2017.json` | 118,287 | 64,115 | 5,000 | 895 | **58,220** | 11,018 |
| 合計 | | | | | **60,873** | **11,515** |

- val2017 が丸ごと未使用なのは、`download_coco_person.py:84` が
  `instances_train2017.json` のみを読んでいるため。train2017 側も
  `--max-images` 既定 5,000 (`download_coco_person.py:175`) しか使っていない。
- **Round B の設定**: `--max-add 6000 --fallen-ratio 0.5`

**`--apply` の実測結果 (2026-08、実行済み)**:

| 段階 | 枚数 |
|---|---|
| 選定 | 6,000 画像 / bbox 24,278 / 倒れ姿勢を含む 3,000 |
| ダウンロード | 6,000（**失敗 0 件**） |
| near-duplicate で破棄 | **818**（13.6%。val/test と ahash hamming <= 4） |
| **実際に追加** | **5,182 画像 / bbox 20,465** |
| train 枚数 | 32,853 -> **38,035**（**+15.8%**） |

> `--max-add` は**選定枚数の上限**であり、追加枚数ではない。
> near-duplicate 破棄は選定後・ダウンロード後に効くため、
> 実際の追加枚数は指定値より少なくなる。Round B で 20,000 枚に増やす場合も
> 同様に 13〜14% 程度は落ちる前提で見積もること。

**リーク防止 (同一画像が split をまたがない設計)**:

| # | 対策 | 実装 |
|---|---|---|
| 1 | **`merge_and_split.py` を再実行しない** | 同スクリプトは `dataset/merged` を丸ごと `shutil.rmtree` し (`merge_and_split.py:147`)、全ペアを `random.shuffle` して振り直す (`merge_and_split.py:66`)。データ追加後に再実行すると **以前 val だった画像が train に入る**ため、過去ラウンドとの比較が成立しなくなる。`expand_coco_person.py` は merged/train にのみ追記し、val/test には一切触れない |
| 2 | 既使用画像の除外 | merged の全 split のファイル名から接頭辞 (`coco_` / `rf_` / `cocoext_` / `hardneg_`) と `augment_offline.py:142` が付ける `_aug<N>` を剥がした「元 stem」で照合し、一致する COCO 画像は候補から外す |
| 3 | near-duplicate 除去 | `--apply` 時に val/test 画像の perceptual hash (ahash、`dataset_cleaning.py` と同じ定義) と照合し、hamming <= 4 の候補を破棄する (`--skip-dedup` で無効化可) |
| 4 | 追加先の単一化 | `--target-split` 既定 `train`。val/test を指定すると警告を出す |

**追加上限・重複除去・空ラベルの扱い**:

- 追加上限: `--max-add` (既定 5,000)。Round A では 6,000 を指定する
- 倒れ姿勢の優先: `--fallen-ratio` (既定 0.5) の比率で「倒れ姿勢 bbox を含む画像」を先に選ぶ。
  プールが足りない場合は残りを通常画像で埋める
- 重複: 上表 #2 (stem 一致) と #3 (ahash) の二段構え。加えて追加先に同名ファイルが既にあればスキップ
- **空ラベル**: 人物アノテーションはあるが `--min-bbox-px` (既定 10、`download_coco_person.py:104` と同値) で
  全 bbox が落ちた画像は、**空ラベルにせず候補から除外**する (意図しない負例の混入を防ぐ)。
  除外件数は `skipped_empty_after_filter` としてレポートに出る (val2017 で 40 件 / train2017 で 895 件)。
  人物のいない画像を明示的に負例として入れたい場合のみ `--allow-empty --max-negatives N` を使う。
  この場合も**空の `.txt` を必ず生成する** (画像とラベルの 1:1 対応が崩れると
  `dataset/scripts/colab_cli/setup_colab.py:116-128` の展開時検証で弾かれるため)
- 原本は `dataset/coco_person_ext/` に残す (切り戻し・再現用)。
  `merge_and_split.py` はこのディレクトリを読まないので、**ゼロから作り直す場合は本スクリプトを再実行する**

#### 4.1.3 ③ 再学習 cfg / 起点重み / 手順

| 項目 | Phase 3 (#137) | **Round A (#148)** | **Round B (#148)** | 理由 |
|---|---|---|---|---|
| ラベル形式 | `_aug` が `0.0` (破損) | **`0` に正規化** | 正規化済み | **Round A の唯一の変更点** (8 章 R8) |
| データセット | merged (train 32,853) | **変更なし (32,853)** | +COCO 5,182 (**38,035**) | Round A で破損修正の効果を単独測定 |
| データセット zip | `fall_detection_dataset.zip` | **同じ zip のまま** | `fall_detection_dataset_v2.zip` | Round A は画像が変わらないため再アップロード不要 |
| hard negative | round1 0.15 / round2 0.08 | **無効 (`RUN_HARD_NEGATIVE = False`)** | 無効 | Recall に無力 (#137 6.1.3) |
| 学習起点 | Phase 2 best (snapshot) | **Phase 2 best (同じ)** | Phase 2 best | round1/round2 と同じ起点にして比較可能にする |
| `max_batches` | 100,000 | **100,000 (据え置き)** | 100,000 | 比較可能性を優先 |
| cfg のその他 | `mosaic=0 / mixup=0 / cutmix=0`, `burn_in=1000`, `iou_loss=ciou`, `policy=sgdr` | **据え置き** | 据え置き | 変数を 1 つに絞る |
| アンカー | Step 4 で再計算 | **再計算する** | 再計算する | Round A でも bbox の読み取り値が変わるため必須 |
| クリーニング | hamming=3 | **hamming=3 (据え置き)** | hamming=3 | val の状態を固定するため (4.1.4) |
| 再開マーカー | `.issue137_started` | **`.issue148_started`** | 同左 | Phase 3 の再開制御と分離 |
| Drive 保存先 | `yolo_fastest_darknet_person/` 直下と `backup/` | **`.../issue148/` 配下のみ** | 同左 | **Phase 3 成果物の上書き防止 (8 章 R1)** |

> **Round A でもアンカー再計算が必要な理由**: R8 の破損により、これまでの学習・
> アンカー計算は誤読された bbox (極端な横長) を見ていた可能性がある。
> 正規化後は bbox の分布そのものが変わるため、Step 4 の再計算は必須。

> **mosaic/mixup について**: Phase 3 で無効化したのは、負例 (bbox ゼロ) 画像と mosaic の併用で
> darknet が SIGSEGV するため (`f003_03j` 8 章 R7)。Round A/B とも負例を追加しないため
> 理屈上は mosaic を戻せるが、**変数を増やさないため 0 のまま据え置く**。
> mosaic の復活は Round D の候補として 6.2 に記載する。

#### 4.1.4 ④ 評価条件の固定

`f003_03j` 6.1.3 は「round2 の評価は val が未クリーニング (unique_truth=4195、round1 は 3763) のため
完全比較ではない」と注記している。**同じ失敗を繰り返さないため、以下を固定する。**

- 評価コマンド: `darknet detector map`、**conf_thresh=0.25**、**IoU 50%**
  (MCU 側の `POSTPROC_CONFIDENCE_THRESHOLD` = 0.5 は実機運用閾値であり評価条件とは別物)
- 評価対象 split: `val` (= `data/person/valid.txt`)。**案G の追加は train のみ**なので val は round1/round2 と同一集合のまま
- **クリーニングを毎回必ず実行する**。`dataset_cleaning.py` は train/val/test すべてを走査するため
  (`dataset_cleaning.py:75`)、実行の有無で val の GT 数が変わる。閾値も Phase 3 と同一に固定する
  (blur 50 / dark 25 / bright 235 / hash 8 / **hamming 3**)
- **`unique_truth` を必ず記録し、round1 の 3,763 と一致するか確認する**。
  一致しない場合、その回の Recall は過去ラウンドと比較してはならない。
  そのためノートブックの評価セル (cell[31]) に `valid.txt` の枚数出力を追加した
- 診断 (Step 7.5) も同じ conf 0.25 / IoU 0.5 で行う。
  ただし `fallen_pose_recall_eval.py` の Precision/Recall は貪欲マッチングによる独自実装であり、
  `darknet detector map` と数 pt ずれうる。**用途はグループ間の相対比較**であり、
  KPI 判定は `darknet detector map` の値で行う

---

## 5. Phase B 実装 (新規スクリプトとノートブック変更点)

### 5.1 `dataset/scripts/fallen_pose_recall_eval.py` (新規)

| 項目 | 内容 |
|---|---|
| 入力 | `--dataset` (既定 `/content/dataset`) / `--split` (既定 `val`) / `--predictions` (darknet `detector test -out` の JSON) |
| 主な引数 | `--conf-threshold` 0.25 / `--iou-threshold` 0.5 / `--fallen-ar` 1.2 / `--upright-ar` 0.8 / `--no-image-size` / `--source-prefixes` |
| dry-run | `--dry-run` で **推論 JSON 不要**。GT の構成 (姿勢/サイズ/ソース別の件数) だけを集計する。閾値の当たりを付けるのに使う |
| 出力 | 標準出力の表 + `--report <path>` で JSON。`--list-out` で「倒れ姿勢 GT を含む画像」のパス一覧 |
| 副作用 | **データセットを変更しない** (書き込むのは `--report` / `--list-out` で指定したファイルのみ) |
| 予測 JSON 形式 | `hard_negative_mining.py` と同一 (`filename` / `objects[].confidence` / `relative_coordinates`) |

マッチングは conf 降順の貪欲マッチング (1 検出 : 1 GT、IoU >= 閾値)。
GT に対応が無い画像の検出はすべて FP として数える。

### 5.2 `dataset/scripts/expand_coco_person.py` (新規)

| 項目 | 内容 |
|---|---|
| 入力 | `--annotations-dir` (既定 `dataset/downloads/coco/annotations`) / `--coco-split` `val2017`\|`train2017`\|`both` (既定 both) |
| 出力先 | 原本 `--source-out` (既定 `dataset/coco_person_ext/`)、追加先 `--merged/images/<target-split>` + `labels/<target-split>` |
| 主な引数 | `--max-add` 5000 / `--fallen-ratio` 0.5 / `--fallen-ar` 1.2 / `--min-bbox-px` 10 / `--dedup-hamming` 4 / `--allow-empty` + `--max-negatives` / `--seed` 42 |
| dry-run | 既定。**ネットワークアクセスなし**で annotations の解析と候補選定だけを行う (4.1.2 の実測表はこれで取得) |
| apply | `--apply` で COCO の個別 URL から画像取得 -> ahash 照合 -> merged/train へ追加 -> `train.txt` 再生成 |
| レポート | `--report <path>` で JSON |
| 切り戻し | merged の `cocoext_*` を削除してリストを再生成すればよい (原本は `coco_person_ext/` に残る) |

### 5.2.1 `dataset/scripts/fix_float_class_ids.py` (新規・Round A の本体)

| 項目 | 内容 |
|---|---|
| 目的 | YOLO ラベルのクラス ID が float 表記 (`0.0`) の行を整数 (`0`) に正規化する (8 章 R8) |
| 入力 | `--dataset` (既定 `dataset/merged`) / `--splits` (既定 `train,val,test`) |
| dry-run | **既定**。件数を出すだけで書き換えない |
| apply | `--apply` でラベルファイルを上書き。**bbox 座標には触れず行頭トークンのみ置換**。行順・桁は保持 |
| 冪等性 | 2 回目以降は修正対象 0 行。Colab で毎回実行しても安全 |
| 安全側の判断 | クラス ID が非整数 (`0.5` 等) の行は「想定外の壊れ方」として**自動修正せず** `lines_malformed` に計上する |
| レポート | `--report <path>` で JSON |
| 実行済み | ローカル `dataset/merged` に適用済み (train 38,135 行 / val・test 0 行、再検査 0 件) |

**根本原因も修正済み**: `augment_offline.py:81` を `int(cls_id)` に変更し、
以後の `augment_offline.py` 実行では正しい表記が生成される。

### 5.3 ノートブック変更点 (`dataset/scripts/train_yolo_fastest_darknet_colab.ipynb`)

セル番号は**更新後**のもの。今回 6 セル (markdown 3 / code 3) を位置 7 / 32 / 48 に挿入したため、
**位置 7 以降のセル番号がすべて +2 ずれた** (Issue #148 コメント ① の `cell[25]` は
**更新後は `cell[27]`**)。ずれの影響と対応は 5.3.1 を参照。

| cell | 種別 | 変更内容 |
|---|---|---|
| 0 | markdown | Phase 4 (#148) のサマリ表と警告を追記。Round A / Round B の 2 段構えと R8 を明記 |
| **7 / 8** | **新規** | **Step 2.0 実行スコープ設定**。`GDRIVE_RUN_DIR` (= `.../yolo_fastest_darknet_person/issue148`)、`DATASET_ZIP`、`ISSUE_MARKER`、`DATASET_ID_NAME` を決めて環境変数に出す。隔離先が Phase 3 の保存先と一致していたら `RuntimeError`。Phase 3 成果物のサイズ/更新時刻をベースラインとして `/content/.issue148_drive_baseline.json` に記録 (監視対象 `_PROTECTED_ROOTS` = ルート直下 / `backup/` / `model/` / `phase2_source/`) |
| **9** | code | `DATASET_ZIP` / `GDRIVE_BACKUP` を環境変数から取得するよう変更。**末尾に Round A の本体である `fix_float_class_ids.py --apply` を追加**（セルを新設せず既存セルに追記したのは、セル挿入で番号が再びずれて 5.3.1 の参照が壊れるのを避けるため） |
| 12 | code | 再開判定のマーカーと Drive パスを隔離先に変更。クリーニングレポートの保存先を `issue148/cleaning_report.json` に変更。`--dup-hamming-threshold 3` の据え置き理由をコメント化 |
| 13 | markdown | Round A では hard negative 無効であることを明記 |
| 14 | code | `RUN_HARD_NEGATIVE = False` (Round A)。`MARK_SKIP_AS_DONE = True` を追加し、意図的スキップ時に完了マーカーを書く。Phase 2 weights の**読み取り元は Phase 3 ルート、書き込みは隔離先**に分離 |
| 26 | markdown | 再開マーカーの説明を `.issue148_started` に更新。保存先の隔離を明記 |
| **27** | code | `ISSUE137_MARKER` -> `ISSUE_MARKER_PATH` / `is_issue137_started` -> `is_issue_started` に改名し、マーカー名とデータセット識別子名を環境変数化。**`backup` の symlink 先が Phase 3 の `backup/` と一致していたら `RuntimeError`** を追加。finetune 起点の探索先を Phase 3 ルート (読み取り専用) に固定 |
| 29 | code | 学習成果物の Drive コピー先を隔離先に変更 |
| 31 | code (%%bash) | best weights の復元元を `${GDRIVE_RUN_DIR}` に変更。`valid.txt` の枚数を出力 (評価母数の記録) |
| **32 / 33** | **新規** | **Step 7.5 倒れ姿勢サブセット Recall 診断**。`DIAGNOSE_WEIGHTS` が空なら Step 7 が評価した重み (`/content/.eval_ok`) を、指定があればその重みを診断する。`darknet detector test -thresh 0.25` で val を推論 -> `fallen_pose_recall_eval.py` を実行 -> レポートを隔離先に保存 |
| 47 | code | 成果物 (`model/`) の保存先を隔離先に変更 |
| **48 / 49** | **新規** | **Step 11.5 Phase 3 成果物の無傷確認**。ベースラインと現在を比較し、変更・消失があれば `RuntimeError` |
| 51 | markdown | Issue #148 の実行順序を追記 |

**Drive 上書き事故の防止 (Issue #148 コメント ① への対応)**:

Phase 3 の成果物 (`_80000.weights` / `_90000.weights` / `*-phase3r1.weights` ほか) を上書きしうる
経路は、更新前のノートブックに **4 か所**あった。すべて隔離した。

| # | 経路 | 上書き先 (更新前) | 対策 |
|---|---|---|---|
| 1 | cell[27] が `backup` を Drive へ symlink し、darknet が 1000 iteration ごとに保存 | `.../yolo_fastest_darknet_person/backup/` | symlink 先を `issue148/backup/` に変更 + 一致検査で `RuntimeError` |
| 2 | cell[29] が `backup/*.weights` を Drive ルートへコピー | `.../yolo_fastest_darknet_person/` 直下 | コピー先を `issue148/` に変更 |
| 3 | cell[47] が変換成果物を `model/` へコピー | `.../yolo_fastest_darknet_person/model/` | `issue148/model/` に変更 |
| 4 | cell[12] がクリーニングレポートを上書き | `.../phase2_cleaning_report.json` | `issue148/cleaning_report.json` に変更 |

加えて cell[8] がベースラインを取り、cell[49] が差分ゼロを機械的に検証する。
検証の監視対象は `_PROTECTED_ROOTS` (ルート直下 / `backup/` / `model/` / `phase2_source/`) で、
上表 4 経路の書き込み先 (更新前) をすべて覆う。**監視対象に漏れがあると、
その場所への書き込みは Step 11.5 をすり抜ける**ため、書き込み先を追加する場合は
`_PROTECTED_ROOTS` にも追加すること。

### 5.3.1 セル番号のずれ (+2) への追随

ノートブックのセル番号は、リポジトリ内の**他のスクリプト・手順書からも参照されている**。
今回の挿入で位置 7 以降が +2 ずれたため、以下をすべて更新した。

| ファイル | 影響 | 対応 |
|---|---|---|
| `dataset/scripts/colab_cli/make_prep_nb.py` | **機能的な破損**。`PICK = [8, 14, 15, 16]` が位置で `nb["cells"][i]` を取るため、更新後は別のセル (Step 2.0 設定セル / hard negative セル等) を抜き出してしまう | `PICK = [10, 16, 17, 18]` に修正。加えて `EXPECT` (各セルに必ず含まれる文字列) との照合を追加し、再びずれたら**黙って別セルを抜き出さず異常終了**するようにした |
| `dataset/scripts/colab_cli/train_launch.py` | Drive 上書き警告メッセージ (`cell[25] が実行済みです`) が別セルを指す | `cell[27]` に更新 |
| `dataset/scripts/colab_cli/drive_guard.py` / `setup_colab.py` / `check_notebook_errors.py` / `README.md` | コメント・手順の参照先 | +2 に更新 |
| `doc/colab-cli-setup-guide/colab-cli-setup-guide.md` | 6.5 節ほか 37 か所の参照 | +2 に更新 |

対応の規則は「**7 以上のセル番号を +2**」(位置 7 より前のセルは動いていない)。
更新前後の対応が正しいことは、更新前のノートブック (`git show HEAD:...`) の
セル本文と更新後のセル本文を突き合わせて確認した (`old[8]->new[10]`, `old[14]->new[16]`,
`old[15]->new[17]`, `old[16]->new[18]`, `old[18]->new[20]`, `old[19]->new[21]`,
`old[22]->new[24]`, `old[23]->new[25]`。本文を編集したセルは一致しないが位置は同じ規則)。

### 5.4 ユーザー実行前チェックリスト

#### Round A (ラベル正規化のみ) — **データセットの再アップロードは不要**

Round A は画像が 1 枚も変わらないため、Drive にある既存の
`fall_detection_dataset.zip` (4.87GB) をそのまま使う。ラベル正規化は
Colab 上で cell[9] が実行する。

**Drive (スクリプト配置)**

- [ ] `/content/drive/MyDrive/yolo_fastest_darknet_person/dataset_cleaning.py` (最新)
- [ ] `.../fallen_pose_recall_eval.py` (**新規**)
- [ ] `.../fix_float_class_ids.py` (**新規・Round A の本体。無いと cell[9] が停止する**)
- [ ] `hard_negative_mining.py` は Round A/B では不要 (Round C で使う)
- [ ] Phase 2 の finetune 起点が読める場所にあること
      (`.../phase2_source/yolo-fastest-person-192_phase2.weights` または
      `.../yolo-fastest-person-192_best.weights` / `_final.weights`)

**Colab**

- [ ] ランタイムが GPU (L4 推奨。L4 で約 0.09 秒/iteration、100k で約 2.5 時間。#179 コメント ②)
- [ ] **Step 2.0 の設定セル (cell[8]) を必ず実行する**。飛ばすと Phase 3 と同じ場所に書き込む
- [ ] cell[9] の出力で「修正しました: 38,135 行」を確認する。
      **0 行だった場合は正規化済みの zip を掴んでいるか、スクリプトが古い**
- [ ] Phase 3 の `.issue137_started` は残してよい (Phase 4 は `.issue148_started` を使う)
- [ ] 同一 VM で #137 を回した直後の場合は `/content/dataset` を削除してから再展開する
      (`RUN_HARD_NEGATIVE = False` のとき既存 `hardneg_*` を除去する処理 (2.6.0) は走らないため)

#### Round B (データ拡充) — Round A の結果を見てから

ローカルの `dataset/merged` は**拡充とラベル正規化の両方が適用済み** (2026-08 実施) なので、
zip を作り直してアップロードするだけでよい。

- [x] `expand_coco_person.py --coco-split both --max-add 6000 --apply` (実行済み。5,182 枚追加)
- [x] `quality_check.py` (実行済み。ALL CHECKS PASSED)
- [x] `fix_float_class_ids.py --apply` (実行済み。38,135 行)
- [ ] `cd dataset/merged && zip -r fall_detection_dataset_v2.zip images/ labels/`
- [ ] **zip の検証**: `python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]); print('OK')" fall_detection_dataset_v2.zip`
      (`file` コマンドでは破損を検出できない。#179 コメント ③ 参照)
- [ ] `fall_detection_dataset_v2.zip` を Drive のマイドライブ直下にアップロード
- [ ] cell[8] の `DATASET_ZIP` を `fall_detection_dataset_v2.zip` に変更
- [ ] `colab_cli` を使う場合は `dataset/scripts/colab_cli/setup_colab.py:21` の `DRIVE_ZIP` も変更
- [ ] Round A とは**別の `GDRIVE_RUN_DIR`** (例: `RUN_NAME = 'issue148_roundB'`) にするか、
      Round A の成果物を退避してから実行する (同名ファイルの上書きを避けるため)

### 5.5 実行順序 (Colab)

1. Step 1: darknet セットアップ
2. Step 2: Drive マウント
3. **Step 2.0 (新規): 実行スコープ設定** — 保存先の隔離とベースライン記録
4. Step 2 続き: データセット展開 (Round A は `fall_detection_dataset.zip`)、
   **ラベルのクラス ID 正規化 (Round A の本体、cell[9] 末尾)**、darknet 形式ファイル生成
5. Step 2.5: `dataset_cleaning.py` (hamming=3、dry-run -> apply)
6. Step 2.6: hard negative — **Round A/B では自動スキップ** (完了マーカーは書かれる)
7. Step 3: cfg 生成 (Phase 3 と同一パラメータ)
8. Step 4: **アンカー再計算 (必須)** — 正規化で bbox の読み取り値が変わる (Round B では追加データでも変わる)
9. Step 5: 事前学習重み (Phase 2 起点なので予備)
10. Step 6: 学習 (100k iter finetune、`.issue148_started` 制御、L4 で約 2.5 時間)
11. Step 7: `darknet detector map` (conf 0.25 / IoU 50)。**`unique_truth` を記録し 3,763 と一致するか確認**
12. **Step 7.5 (新規): 倒れ姿勢サブセット Recall 診断**
13. Step 8-9.5: TFLite INT8 変換 / MCU パラメータ出力
14. Step 10: Vela 互換性確認 (アリーナ 442,368 B 以下 / 単一 `sub_0000` 維持)
15. Step 11: 成果物保存
16. **Step 11.5 (新規): Phase 3 成果物の無傷確認**

---

## 6. KPI 判定基準と未達時の分岐

### 6.1 判定基準 (Issue #148 受入条件)

評価は `darknet detector map`, conf_thresh=0.25, IoU 50%, split=val で実施:

- [ ] **Recall >= 60%**
- [ ] Precision >= 60% (維持)
- [ ] mAP@0.50 >= 50% (維持)
- [ ] `unique_truth` が round1 と同じ 3,763 (評価条件の同一性)
- [ ] Ethos-U55 アリーナ 442,368 バイト以下を維持 (Step 10 Vela)
- [ ] NPU 単一サブグラフ (`sub_0000`) 維持
- [ ] NPU 推論時間 5ms 以内を維持
- [ ] CPU0 FLASH 960KB 以内を維持

構造を変えないため後半 4 項目は維持される見込みだが、**Step 10 の Vela で必ず検証**する。

### 6.2 未達時の分岐

**Round A (ラベル正規化のみ) の結果による分岐**:

| 状況 | 次アクション |
|---|---|
| 全 KPI 達成 (Recall >= 60%) | **Round B は不要**。Phase D (MCU 反映) へ。手順は `f003_03j` 7 章 |
| Recall が大きく改善したが 60% 未満 | **Round B へ** (データ拡充 5,182 枚)。R8 が主因だったと判断でき、データ増量が上積みになる見込み |
| Recall がほぼ伸びない (< +2pt) | R8 は主因ではなかった。**Round B へ** (データ量が律速かを検証)。R8 の修正自体は仕様準拠なので巻き戻さない |
| Precision が 60% を割った | Round B の後に **Round C** で hard negative を `--max-add-ratio 0.05` / `--fp-conf-threshold 0.45` で復活 |

**Round B (データ拡充) の結果による分岐**:

| 状況 | 次アクション |
|---|---|
| 全 KPI 達成 | Phase D (MCU 反映) へ |
| Recall +2〜+8pt だが 60% 未満 | **Round D-(a)**: 追加を 6,000 -> 20,000 に増量 (プールは 60,873 あるので可能)。倒れ姿勢比率を 0.7 に上げる。near-duplicate 破棄 13〜14% を見込むこと |
| Recall がほぼ伸びない (< +2pt) | データ量が律速ではない。**Round D-(b)/(c)**: mosaic=1 復活 / `max_batches` 150,000。それでも駄目なら **案H へ移行 (Phase E とセット、7 章)** |

**診断 (Step 7.5) の結果による分岐 (両ラウンド共通)**:

| 状況 | 次アクション |
|---|---|
| 診断で `fallen` の Recall だけ極端に低い | 倒れ姿勢データをさらに重点追加 (`--fallen-ratio 0.8`)。Roboflow Fall 以外の転倒データセット追加も検討。**R8 で横長 bbox が汚染されていたため、Round A 前後の `fallen` Recall 比較は特に重要** |
| 診断で `small` の Recall だけ低い | 解像度律速。**案H を最優先に切り替え、Phase E を先に実施** |
| `unique_truth` が 3,763 と一致しない | 比較不能。クリーニングの実行状態と閾値を揃えて再評価する (数値を KPI 判定に使わない) |

---

## 7. 案H (解像度 224/256) を実施する場合に必要な作業

**本 Issue では実施しない。** Round C まで尽くしても Recall が届かない場合の準備として記載する。

### 7.1 Phase E (アリーナ余裕確保) で先に確定させること

1. **アリーナ上限の根拠の確定**: 現行の 442,368 バイトが「使える SRAM の上限ピッタリ」なのか、
   単に現行モデルが要求する量なのかを、リンカスクリプトと FSP のメモリ配置から確定させる。
   `f003_03j` 3.1 は上限ピッタリと記述しているが、本 Issue では未検証
2. 余裕を作る手段の評価: (a) 他機能のバッファ削減、(b) 外部 SDRAM 配置 (推論速度への影響を要実測)、
   (c) `FAST_SCRATCH` の使い方見直し (`sub_0000_net1_invoke.c:42-44` が 442,368 バイトの
   `fast_scratch` も宣言している)

### 7.2 案H 実施時の Vela 再評価項目

| 項目 | 確認内容 | 判定基準 |
|---|---|---|
| アリーナ | Vela 出力の Arena/`_split_1_scratch` | 確保できる SRAM 以内 |
| サブグラフ数 | `sub_0000` 単一か | 分割されないこと (分割すると MCU 側の `wrapper.h` / `mera/*` の前提が崩れる) |
| NPU 実行率 | CPU フォールバック op の有無 | 100% NPU が理想 |
| 推論時間 | Vela の推定サイクル数と実機実測 | 5ms 以内 |
| FLASH | INT8 tflite サイズ | 現行 515,112 バイトからほぼ増えない見込み (重みは入力解像度に非依存) だが要確認 |

### 7.3 MCU 側への波及 (参考)

解像度を変えると入力バッファサイズ、`wrapper.h` の入出力サイズ、グリッド数 (6x6/12x12 ->
7x7/14x14 or 8x8/16x16)、後処理のアンカー・ストライドがすべて変わる。
**アンカー値は必ずその解像度で再計算する** (#137 6.1.3 のとおり、学習時と異なるアンカーで
評価すると mAP が 21% まで落ちた実績がある)。

---

## 8. リスクと注意事項

### R1. Phase 3 成果物の上書き (最重要)

- Phase 3 の重み (`_80000.weights` / `_90000.weights` / `*-phase3r1.weights` ほか) は
  Drive の `yolo_fastest_darknet_person/` 直下と `backup/` にある。更新前のノートブックは
  この 2 か所に書き込む設計だった (5.3 の表)
- 対策: Step 2.0 (cell[8]) で `GDRIVE_RUN_DIR` を `issue148/` に隔離、
  cell[27] で symlink 先の一致検査、cell[49] でベースライン差分検証
- **設定セルを飛ばさないこと**。飛ばすと環境変数が無いため全セルが既定値 (= Phase 3 と同じ場所) に
  フォールバックする
- 短い動作確認をしたい場合は、従来どおり `dataset/scripts/colab_cli/train_launch.py` を使う
  (保存先を `/content/backup_smoke` に隔離し、`backup` が Drive を指していたら中止する:
  `train_launch.py:39`, `:61-67`)

### R2. データ拡充によるデータ偏り

- COCO は屋外・群衆・スポーツシーンが多く、**家庭内見守りの分布とはずれる**。
  6,000 枚 (+18%) 程度に留めるのはこのため。20,000 枚まで増やす Round C-(a) では
  偏りによる副作用 (家庭内シーンでの Recall がむしろ落ちる) に注意する
- 倒れ姿勢の優先選択は bbox のアスペクト比による近似であり、
  「横たわった人物」ではなく「横向きに大きく写った人物」を拾うことがある。
  `--apply` 前に dry-run の件数を確認し、追加後に数十枚を目視すること

### R3. ラベル品質

- COCO の person アノテーションには群衆をまとめた `iscrowd=1` が含まれる。
  `expand_coco_person.py` は `download_coco_person.py:100-101` と同じく除外している
- `--min-bbox-px 10` 未満の極小 bbox は捨てる。これを緩めると 192px 入力では
  1 画素未満の物体を学習させることになり、Recall ではなくノイズが増える

### R4. 評価条件のずれ (再発防止)

- #137 round2 で起きた「val 未クリーニングのまま評価」を避けるため、4.1.4 の固定条件を守る
- ノートブックの評価セルが `valid.txt` の枚数を出力するようにした。
  `unique_truth` と併せて記録し、round1 の 3,763 と一致するか毎回確認する

### R5. 学習時間と Colab セッション

- L4 で 100k iteration = 約 2.5 時間 (#179 コメント ②)。`colab exec` を使う場合は
  `--timeout` を学習時間より十分大きく取る
- 接続断からの再開は `.issue148_started` + `.issue148_dataset_id` で制御される。
  データセットや cfg を変えた場合は**別実験として扱われ、再開は拒否される** (cell[27] の識別子検証)

### R6. データセット受け渡し

- `colab upload` は 1 ファイル 64〜80MB 上限・実効 3.05MB/s のため、5GB 規模の zip には使えない
  (#179 コメント ④)。**Drive 経由で受け渡す**
- 拡充後の zip は 6,000 枚増でおおむね +0.5〜1GB 程度になる見込み。作成直後に必ず zip を検証する

### R7. `merge_and_split.py` を再実行しないこと

- 再実行すると `dataset/merged` が丸ごと作り直され (`merge_and_split.py:147`)、
  全ペアがシャッフルし直される (`merge_and_split.py:66`)。
  **val/test の中身が変わり、過去ラウンドとの比較が一切できなくなる**
- データを増やす場合は `expand_coco_person.py` (train のみ追記) を使う
- どうしても作り直す場合は、val/test の画像名リストを保存しておき、
  同じ割り当てを復元する仕組みを別途作ること (本 Issue では未実装)

### R8. `_aug` ラベルのクラス ID が float 表記 (Round A で判明・最重要)

**事実 (実測)**:

| 項目 | 値 |
|---|---|
| 該当ファイル | `dataset/merged/labels/train/*_aug<N>.txt` **21,803 ファイル** |
| 該当行 | **38,135 行** (train 全 78,536 行の **48.6%**) |
| train 画像に占める割合 | 21,803 / 38,035 = **57.3%** |
| 表記 | `0.0 0.726047 ...` (正しくは `0 0.726047 ...`) |
| 原因 | `augment_offline.py:81` が albumentations の返す float をそのまま f-string 出力 |
| val / test | **該当 0 行** (`augment_offline.py` は train のみ拡張するため) |

**darknet への影響 (推定・未確定)**:

darknet の `read_boxes` は `fscanf(file, "%d %f %f %f %f", ...)` で読むとされる。
C の `scanf` 意味論では `%d` が `0` だけを消費して `.0` が次の `%f` に流れ込み、
フィールドが 1 つずつずれる。実際に同じ意味論を再現して確認した結果:

```
正しい形式 (0)   : id=0 x=0.273953 y=0.859979 w=0.111750 h=0.280042
_aug の形式(0.0) : id=0 x=.0       y=0.726047 w=0.859979 h=0.111750  ← 本来の h が余る
```

座標がずれるだけでなく、**w/h 比が 7.7 倍の極端な横長 bbox** として読まれる。
横長 = 本 Issue が重視する「倒れ姿勢」の形状であり、
**倒れ姿勢の学習を系統的に汚染していた可能性がある**。

**未確定な点 (正直に記録する)**:

- 本リポジトリに darknet のソースは無く、**書式文字列そのものは未確認**である
- 上記は C の `scanf` 意味論の再現であって、darknet を実行した検証ではない
- 確実に言えるのは「YOLO 仕様 (クラス ID は整数) から外れており、
  同一データセット内の非 `_aug` ラベルは `0` を使っている」ことのみ

**対処**:

- 根本原因: `augment_offline.py:81` を `int(cls_id)` に修正済み (以後の生成は正しくなる)
- 既存データ: `dataset/scripts/fix_float_class_ids.py` で正規化 (dry-run 既定)。
  ローカルの `dataset/merged` は適用済み (38,135 行、再検査 0 件)
- Colab: データセット展開セル (cell[9]) の末尾で `--apply` を実行する。
  **Round A の変更点はこれだけ**であり、スキップすると実験の意味が無くなる
- 評価への影響: val/test は該当 0 行のため、**過去ラウンドの評価数値は汚染されていない**。
  汚染されていたのは学習データ側のみ

---

## 9. 変更ファイル一覧 (Phase A/B コミット対象)

| パス | 種別 | 内容 |
|---|---|---|
| `doc/report/f003_03l_person_detection_recall_plan.md` | 新規 | 本ドキュメント (Phase A 成果物) |
| `dataset/scripts/fallen_pose_recall_eval.py` | 新規 | 倒れ姿勢サブセット Recall 診断 (dry-run / JSON レポート) |
| `dataset/scripts/expand_coco_person.py` | 新規 | COCO からの人物データ追加取り込み (dry-run / --apply / リーク防止) |
| `dataset/scripts/fix_float_class_ids.py` | 新規 | ラベルのクラス ID 正規化 (**Round A の本体**、8 章 R8) |
| `dataset/scripts/augment_offline.py` | 更新 | **R8 の根本原因を修正** (`f"{cls_id}"` -> `f"{int(cls_id)}"`) |
| `dataset/scripts/train_yolo_fastest_darknet_colab.ipynb` | 更新 | Issue #148 対応 (実行スコープ設定 / 保存先隔離 / hard negative 無効 / Step 7.5 診断 / Step 11.5 検証) |
| `dataset/scripts/colab_cli/make_prep_nb.py` | 更新 | セル番号ずれ (+2) の修正 + `EXPECT` によるずれ検出 (5.3.1) |
| `dataset/scripts/colab_cli/train_launch.py` | 更新 | セル番号参照を +2 に更新 |
| `dataset/scripts/colab_cli/drive_guard.py` | 更新 | 同上 |
| `dataset/scripts/colab_cli/setup_colab.py` | 更新 | 同上 |
| `dataset/scripts/colab_cli/check_notebook_errors.py` | 更新 | 同上 |
| `dataset/scripts/colab_cli/README.md` | 更新 | 同上 |
| `doc/colab-cli-setup-guide/colab-cli-setup-guide.md` | 更新 | 同上 (37 か所) |
| `.gitignore` | 更新 | `dataset/coco_person_ext/` (拡充データの原本) と `dataset/fall_detection_dataset_v2.zip` を除外に追加 |

MCU 側 C/H (`e2studio_CPU0/src/ai_application/**`, `mera/*`)、`configuration.xml`、
`ra_gen/`、`ra/fsp/` は **本セッションでは未変更**。

### 9.1 検証内容

| 対象 | 検証 | 結果 |
|---|---|---|
| `fallen_pose_recall_eval.py` | `python -m py_compile` / `--help` | OK |
| 同 | `--dataset dataset/merged --split val --dry-run` の実行 | OK (GT 4,195 件を集計。4.1.1 の表) |
| `expand_coco_person.py` | `python -m py_compile` / `--help` | OK |
| 同 | `--coco-split val2017 --max-add 3000` (dry-run) | OK (候補 2,653) |
| 同 | `--coco-split both --max-add 6000` (dry-run) | OK (候補 60,873 / 選定 6,000。4.1.2 の表) |
| 同 | `--apply` の実行 (**実施済み**) | OK (DL 6,000 / 失敗 0 / near-dup 破棄 818 / **追加 5,182**。train 32,853 -> 38,035) |
| `quality_check.py` | 拡充後の merged 全体 | **ALL CHECKS PASSED** (1:1 対応・座標範囲・空ラベルすべて OK) |
| `fix_float_class_ids.py` | `py_compile` / dry-run / `--apply` (**実施済み**) | OK (train 38,135 行を修正 / val・test 0 行。再検査 0 件) |
| 同 | 適用後の `fallen_pose_recall_eval.py --split train` | OK (GT 78,536・空ラベル 0 で実データと一致。**修正前は 40,401 しか数えられていなかった**) |
| darknet の誤読 (R8) | C の `scanf` 意味論を再現して確認 | フィールドが 1 つずれることを確認。**ただし darknet 本体は未参照** |
| ノートブック | JSON パース / `nbformat.validate` | OK (52 セル) |
| 同 | 全 Python セルを IPython 変換後に `compile()` | OK |
| 同 | 全 `%%bash` セルを `bash -n` | OK (5 セル) |
| `make_prep_nb.py` | 実行して抜き出しセルを確認 | OK (`cell[10]/[16]/[17]/[18]` を取得。`EXPECT` 照合も通過) |
| セル番号の +2 対応 | 更新前ノートブックとの本文突き合わせ | OK (5.3.1) |

Colab 学習は本セッションでは実行していない (ローカルのデータ準備は完了済み)。

---

## 10. 次のステップ (ユーザー作業)

**Round A (ラベル正規化のみ) — いまここ**

1. **Drive**: `fix_float_class_ids.py` (**新規・必須**)、`fallen_pose_recall_eval.py` (新規)、
   最新の `dataset_cleaning.py` を `yolo_fastest_darknet_person/` に配置
   ※ データセット zip は既存の `fall_detection_dataset.zip` のままでよい (再アップロード不要)
2. **Colab (診断のみ・任意)**: Step 7.5 の `DIAGNOSE_WEIGHTS` に Phase 3 round1 の重みを指定し、
   修正前モデルの姿勢別 Recall を先に取っておく (Round A との差分が見える)。
   その際 cfg のアンカーが round1 学習時の値であることを必ず確認する
3. **Colab**: 5.5 の順序で Round A を実行 (L4 で約 2.5 時間)。
   cell[9] の「修正しました: 38,135 行」を必ず確認する
4. **判定**: 6.1 の KPI と 6.2 の分岐表に従う。
   **Recall >= 60% ならここで完了** (Round B 不要)
5. 実測値が出たら本ドキュメントに「6.1.1 Round A 実測結果」節を追記する
   (`f003_03j` 6.1.1 / 6.1.3 と同じ書き方)

**Round B (データ拡充) — Round A の結果を見てから**

6. **ローカル**: `fall_detection_dataset_v2.zip` を作成・検証し Drive にアップロード
   (データ拡充と正規化は適用済みなので zip 作成だけでよい)
7. **Colab**: `DATASET_ZIP` と `RUN_NAME` を切り替えて Round B を実行
8. **判定**: 6.2 の Round B 分岐表に従って Round C/D または Phase D へ

---

## 11. 参考情報

- Phase 3 引き継ぎ: `doc/report/f003_03j_person_detection_phase3_plan.md` (3 章 評価マトリクス / 6.1.1-6.1.3 / 8 章 R1・R7)
- Phase 2 引き継ぎ: `doc/report/f003_03h_person_detection_phase2_plan.md`
- Colab CLI 運用: `doc/colab-cli-setup-guide/colab-cli-setup-guide.md`、`dataset/scripts/colab_cli/README.md`
- アリーナ実測値: `e2studio_CPU0/src/ai_application/fall_detection/mera/sub_0000_net1_tensors.h:11`
- COCO 2017: https://cocodataset.org/
- darknet CFG パラメータ: https://github.com/AlexeyAB/darknet/wiki/CFG-Parameters-in-the-%5Bnet%5D-section
- Yolo-Fastest ベース: https://github.com/dog-qiuqiu/Yolo-Fastest
