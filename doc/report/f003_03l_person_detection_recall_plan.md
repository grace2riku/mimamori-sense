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

> **【2026-08 追記・重要】442,368 バイトは上限ではなかった。**
> `f003_03j` 3.1 の「アリーナは 442,368 バイト上限ピッタリ」という記述は
> **誤り**である。これは現行モデルが要求する量に過ぎず、使える RAM の上限ではない。
> 実測の結果 **内蔵 RAM に 583.5 KB の空きがある**ことが判明した (7.1)。
> **案H は Phase E の大規模な作業を待たずに着手できる。**

**結論 (2026-08 改訂)**: 案H/I/J を阻んでいた「アリーナ 432KB の壁」は**存在しなかった**。
RAM 実測 (7.1) により、224px も 256px も内蔵 RAM に収まる見込みが立った。
案G を尽くして無効と確定した (6.1.6) いま、**案H が次の本命**である。

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
| データセット zip | `fall_detection_dataset.zip` | **同じ zip のまま** | **同じ zip のまま** | Round B の追加画像は **Colab 上で COCO から直接取得**する (下記) |
| `-map` | なし | なし (`_final` を評価) | **あり (`_best` を保存)** | 途中の最良点を拾う (6.1.2 A3) |
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

#### Round B (データ拡充) — **拡充は Colab 上で行う。巨大 zip の受け渡しは不要**

当初は「ローカルで拡充 -> zip 作成 -> Drive にアップロード」を想定していたが、
**この経路は成立しない**:

- 拡充後の zip は約 6GB。`colab upload` は **1 ファイル 64MB が上限**
  (ガイド 6.8。80MB 以上は 400 Bad Request) のため CLI では送れない
- ブラウザから Drive へ 6GB を上げるのは時間がかかりすぎる

一方 **COCO の画像取得は Colab 側の方が速い** (ローカル実測 約 1.0 枚/秒 = 100 分)。
そこで `expand_coco_person.py --apply` を **Colab 上の cell[9] で実行**する方式に変更した。
データセット zip は Round A と同じ `fall_detection_dataset.zip` のままでよい。

- [x] ローカルでの `--apply` 実行と検証 (2026-08。選定 6,000 -> 破棄 818 -> **追加 5,182 枚**)
      — Colab 側は同じ seed・同じ元データなのでほぼ同じ選定になる
- [ ] Drive に `expand_coco_person.py` を配置 (**Round B の本体。無いと cell[9] が停止する**)
- [ ] cell[8] の `ROUND = 'B'` を確認 (保存先が `issue148_roundB/` に切り替わり、
      Round A の成果物と混ざらない)
- [ ] cell[8] の `USE_MAP = True` を確認 (`_best.weights` を作る)

> ローカルの `dataset/merged` は拡充・正規化とも適用済みだが、**Colab 側は元 zip から
> 展開して拡充をやり直す**ため、ローカルの状態は Round B の実行に影響しない。

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

### 6.1.1 Round A 実測結果 (2026-08、ラベル正規化のみ)

評価条件: `darknet detector map`, conf_thresh=0.25, IoU 50%, split=val,
**`unique_truth_count = 3763`** (round1 と完全一致 → 過去ラウンドと比較可能)。
学習: Phase 2 best 起点 / 100,000 iteration / L4 で **177.7 分** / hard negative 無効 /
アンカー再計算値 `8,20, 25,51, 45,112, 89,67, 107,127, 168,157` で学習・評価とも統一。

| 指標 | Phase 2 | round1 | round2 | **Round A** | KPI | 判定 |
|---|---|---|---|---|---|---|
| mAP@0.50 | 48.9% | **52.2%** | 50.9% | **51.07%** | >= 50% | ✅ 維持 |
| Precision | 49% | 68% | 57% | **53%** | >= 60% | ❌ |
| Recall | 52% | 49% | 51% | **53%** | >= 60% | ❌ |
| P + R | 101 | 117 | 108 | **106** | - | フロンティア内 |
| detections_count | - | - | - | 43,627 | - | - |

**結論: R8 (ラベル破損) は Recall 律速の主因ではなかった。**

- Recall 53% は 4 回中の最高値だが、Phase 2 (52%) から **+1pt** に留まる
- P+R = 106 は #137 が示したフロンティア帯 (101〜117) の**内側**であり、
  **フロンティアそのものは動いていない**。mAP も round1 の 52.2% を超えていない
- したがって「`0.0` 表記により学習データの 57% が誤読され、それが Recall を
  抑えていた」という仮説は**棄却**する。darknet が誤読していなかったか、
  誤読していても影響が小さかったかのいずれか (どちらであるかは切り分けていない)
- ただしラベル正規化は YOLO 仕様への準拠であり、**巻き戻さない**

**次アクション**: 6.2 の分岐表に従い **Round B (データ拡充) へ進む**。

### 6.1.2 Round A で判明した運用上の問題

| # | 問題 | 影響 | 対処 |
|---|---|---|---|
| A1 | **サブプロセスの出力がセル出力に残らない** (`subprocess.run` はカーネルの fd に直接書くため `colab exec` の記録に載らない) | ラベル正規化の修正行数・クリーニング統計・**Step 7.5 の姿勢別 Recall の表**をすべて取りこぼした | cell[9] / cell[12] / cell[33] を `capture_output=True` + `print` に変更済み |
| A2 | ラベル正規化レポートが VM 上 (`/content/issue148_labelfix.json`) にしか残らない | セッション停止で失われる | cell[9] で Drive (`issue148/`) にコピーするよう変更済み |
| A3 | 学習コマンドに **`-map` が無い**ため `_best.weights` が生成されない | cell[31] が `_final.weights` (100,000 iter 時点) を評価する。途中に最良点があっても拾えない | **Round B で対処**。cell[28] に `-map` を追加 (`USE_MAP`)。評価対象が `_best` に変わると「データ拡充の効果」と「best 選択の効果」が混ざるため、cell[31] で **`_final` も参考評価**して切り分ける |
| A4 | クリーニングで train 32,853 -> **22,214** (-32%) まで減る | `_aug` 画像が near-duplicate として大量に除去されている可能性 | 未検証。Round B で `cleaning_report.json` (Drive の `issue148/`) を確認する |

### 6.1.3 Round A 姿勢別診断の結果 (Step 7.5) — **律速は姿勢ではなくサイズ**

`issue148/issue148_fallen_recall.json` (val 2,157 画像 / GT 3,763、conf 0.25 / IoU 0.5)。
独自実装の貪欲マッチングによる値 (TP 1980 / FP 1731 = Recall 52.6%) で、
`darknet detector map` の Recall 53% とほぼ一致している。

**サイズ別 (bbox 面積、正規化)**

| 区分 | GT | TP | **Recall** |
|---|---|---|---|
| large (>= 0.10) | 1,881 | 1,525 | **81.1%** |
| medium (0.01〜0.10) | 1,049 | 404 | **38.5%** |
| small (< 0.01) | 833 | 51 | **6.1%** |

**姿勢別**

| 区分 | GT | Recall |
|---|---|---|
| upright | 2,018 | 41.0% |
| ambiguous | 1,021 | 63.8% |
| fallen | 724 | 69.3% |

**姿勢を「倒れ姿勢の方が得意」と読んではいけない (Simpson のパラドックス)**

サイズを固定すると、姿勢による差はほとんど消える:

| サイズ | upright | ambiguous | fallen | 姿勢による差 |
|---|---|---|---|---|
| large | 77.2% | 84.4% | 81.5% | 7.2pt |
| medium | 39.2% | 36.6% | 37.2% | 2.7pt |
| small | 7.0% | 4.0% | **0.0%** (0/38) | 7.0pt |

姿勢別の見かけの差 (fallen 69.3% vs upright 41.0%) は**構成比の違いによる錯覚**である。
fallen の GT は 77% が large (557/724) なのに対し、upright は small+medium が 68% (1,378/2,018)。
**つまり「倒れ姿勢が苦手」ではなく「小さい人物が苦手」**。

**データ由来別も同じ話**

| 由来 | GT | Recall |
|---|---|---|
| coco (屋外・群衆が多い = 小さい人物が多い) | 2,242 | 36.0% |
| rf (Roboflow Fall = 屋内・被写体が大きい) | 1,521 | **77.1%** |

**改善余地の試算** (現在 TP 1,980 / 3,763 = 52.6%)

| 仮定 | 到達 Recall |
|---|---|
| small を 6% -> 40% | **60.1%** |
| medium を 38% -> 70% | **61.4%** |
| 両方 | **68.9%** |

**KPI 60% は small か medium のどちらか一方を改善するだけで届く。**
逆に large (既に 81%) をいくら磨いても届かない。

**結論と方針への影響**

- 6.2 の分岐表の「**診断で `small` の Recall だけ低い -> 解像度律速。案H を最優先に切り替え**」に該当する
- 案G (データ拡充) は**同じ 192px で COCO データを増やす**施策であり、
  小物体を検出できない根本は変わらない。**単独では期待値が低い**
- ただし**構造を変えずに小物体に効く手**が残っていた (下記)。案H (Phase E 必須) の前にこちらを試す

**Round B に追加した施策 (構造不変・アリーナ影響なし)**

| 施策 | 内容 | 小物体への効き方 |
|---|---|---|
| `mosaic=1` | 4 枚を縮小合成して 1 枚にする | 縮小により**小さい人物の学習例が増える** |
| `random=1` | 10 バッチごとに入力解像度を振る (マルチスケール学習) | スケール変化への頑健性が上がる |

いずれも**学習時のみ**の処理で、デプロイ時の 192x192 入力・アリーナ 442,368B・
単一サブグラフ・FLASH には一切影響しない。
`cell[18]` は Phase 3 以来 `mosaic=0` を強制していたが、これは
**negative (bbox ゼロ) 画像との併用で SIGSEGV した** ため (#137 R7)。
Round A/B は hard negative を使わず空ラベルも作らないため該当せず、
Phase 2 は `mosaic=1` で完走している。

### 6.1.4 Round B 初回実行の失敗と原因分析 (2026-08)

学習は 0 分で異常終了し (exit=-11 = SIGSEGV)、以降の評価・変換もすべて停止した。
**3 つの問題が連鎖**していた。

**① 拡充スクリプトの annotations パス不一致 (根本原因)**

`--annotations-dir /content/coco_annotations` を渡したが、COCO の zip は
`annotations/` を含む構造で配布される。`ensure_annotations` は
`ann_dir.parent` に展開するため、実体は **`/content/annotations/`** に置かれ、
指定した `/content/coco_annotations/` は空のままだった。

```
WARNING: /content/coco_annotations/instances_val2017.json が見つかりません。スキップします
ERROR: 追加候補が 0 件です
```

**② 拡充の失敗がラベル正規化を巻き込んだ (設計ミス)**

cell[9] に「拡充 -> ラベル正規化」の順で書いていたため、①で例外が飛んだ時点で
**ラベル正規化が実行されなかった**。`colab exec -f` はセルが落ちても後続を
実行し続けるため、正規化前のデータのままクリーニングに進んだ。

その結果 `dataset_cleaning.py` が `0.0` 表記のラベルを **`bad_label` として
21,803 件除去**し、train が 32,853 -> **10,340 枚**まで減った
(Round A は正規化済みで 22,214 枚)。

> **副産物**: これは Round A でラベル正規化が確かに効いていたことの裏付けにもなる。
> 正規化が無ければ Round A も 10,340 枚になっていたはずである。

**③ `mosaic=1` で darknet が SIGSEGV (R9)**

VM 上で 1 要素ずつ切り分けた結果:

| 条件 | 結果 |
|---|---|
| baseline (`mosaic=0` / `random=0` / `-map` なし) | 90 秒生存 OK (iteration 780 到達) |
| **`mosaic=1` のみ** | **SIGSEGV (exit=-11)**。データ読み込みスレッド起動直後 |
| `random=1` のみ | 90 秒生存 OK (iteration 570 到達) |
| `-map` のみ | 90 秒生存 OK (iteration 813 到達) |

このときの train は **空ラベル 0 件 / ラベル欠落 0 件 / float 表記 0 件**の
完全にクリーンな状態だった (VM 上で実測確認)。
したがって **#137 R7 の「negative 画像との併用が原因」という理解は不正確**で、
**この darknet ビルドでは `mosaic` 自体が使用不能**と結論する (8 章 R9)。

**対処 (すべて実施済み)**

| # | 対処 |
|---|---|
| ① | `expand_coco_person.py` の `ensure_annotations` が **実際に JSON があるディレクトリを返す**ようにし、呼び出し側はその戻り値を使う。ディレクトリ名がどうであれ吸収する。ノートブックも `--annotations-dir /content/annotations` に修正 |
| ② | cell[9] を **「ラベル正規化 -> 拡充」の順**に入れ替え。さらに cell[9] 末尾で `/content/.dataset_prepared` を書き、**cell[12] (クリーニング) が無ければ停止**するようにした |
| ③ | `CFG_MOSAIC = 0` に戻した。小物体対策は `random=1` (マルチスケール) とデータ拡充で行う |

**この失敗から得た教訓**

- 準備段階の各ステップに**完了マーカーとガード**が必要 (#179 で cleaning / hardneg /
  anchors には入れたが、Step 2 のデータ準備自体には無かった)
- **クリーニングは壊れた入力に対して破壊的**に働く。前工程の成否確認が必須

### 6.1.5 Round B 再実行 (2 回目) の結果 — 準備は全部通り、学習が SIGABRT

6.1.4 の 3 件を修正して再実行した。**準備工程はすべて成功**した。

| 工程 | 結果 |
|---|---|
| ラベル正規化 (順序変更後・先に実行) | 成功。クリーニングの `removed_bad_label` が **0 件**になり、正規化が効いていることを確認 |
| COCO 拡充 | **成功**。DL 6,000 / 失敗 0 / near-dup 破棄 818 / **追加 5,182 枚** -> train 38,035 |
| クリーニング | train 38,035 -> **27,292** (blur 2,301 / dark 225 / bright 52 / **bad_label 0** / duplicate 8,599) |
| cfg | `mosaic=0, mixup=0, cutmix=0, random=1` |
| アンカー再計算 | 60,019 box から `8,18, 23,47, 41,106, 88,66, 94,132, 162,152` |
| 学習 | **iteration 3,000 超で SIGABRT (exit=-6)**。8.8 分で停止 |

**原因: `-map` の初回 mAP 計算 (R10)**

darknet の mAP 計算間隔は `4 * train画像数 / (batch * subdivisions)`。
今回は **4 x 27,292 / 32 = 3,411 iteration** が初回にあたる。
ログの最終進捗が iteration 3,000、停止までの所要 8.8 分 (ETA 4.2h から逆算して
約 3,400 iteration 相当) であり、**初回 mAP 計算の位置と一致する**。

> **先の切り分けが取りこぼした理由**: 6.1.4 の bisect で「`-map` のみ」は 90 秒生存と
> 判定したが、そのとき到達したのは iteration 813 で、**初回 mAP 計算 (当時の
> データ量では 1,292) に届いていなかった**。テスト時間が短すぎた。
> 短時間の生存確認は「その時点までは安全」しか意味しない。

**対処**: `USE_MAP = False`。best 選択は諦め、Round A と同じく `_final` を評価する
(結果として Round A との比較条件も揃う)。`random=1` は 3,000 iteration 以上を
問題なく走破しており維持する。

**あわせて入れた再発防止**

- cell[28] が darknet 出力の**末尾 60 行をリングバッファに保持**し、
  異常終了時に**セル出力へ表示**する。加えて `training_log.txt` を Drive の
  `training_log_failed.txt` に退避する
- 今回 SIGABRT の具体的なメッセージを取り逃がしたのは、ログが VM 上にしか無く
  セッション消失で読めなくなったため。同じ取りこぼしは起きなくなる

**学習時間の見積り更新**: 進捗表示の ETA は **4.2 時間**だった
(Round A は 3.0 時間)。`random=1` によるマルチスケール化と、
クリーニング後 train が 22,214 -> 27,292 に増えたことによる。

### 6.1.6 Round B 実測結果 (2026-08) — **案G は無効。解像度律速で確定**

3 回目の実行で完走した。学習 100,000 iteration / **255.8 分** (4.3 時間、`random=1` による)。
評価条件は `unique_truth_count = 3763` で Round A・round1 と一致。

| 指標 | Phase 2 | round1 | round2 | Round A | **Round B** | KPI |
|---|---|---|---|---|---|---|
| mAP@0.50 | 48.9% | **52.2%** | 50.9% | 51.07% | **49.11%** | >= 50% ❌ |
| Precision | 49% | 68% | 57% | 53% | **51%** | >= 60% ❌ |
| Recall | 52% | 49% | 51% | **53%** | **52%** | >= 60% ❌ |
| P + R | 101 | 117 | 108 | 106 | **103** | - |

**Round B は Round A より全指標で悪化した。** P+R = 103 は #137 のフロンティア帯
(101〜117) の**最下限**であり、フロンティアは押し上がっていない。

**決定的な事実: 小物体の Recall が 1 件も動かなかった**

| サイズ | Round A | **Round B** | 差 |
|---|---|---|---|
| large | 1525/1881 = 81.1% | 1501/1881 = 79.8% | -1.3pt |
| medium | 404/1049 = 38.5% | 391/1049 = 37.3% | -1.2pt |
| **small** | **51/833 = 6.1%** | **51/833 = 6.1%** | **±0.0pt** |

small の TP は **両ラウンドとも 51 件で完全に同一**。
COCO 画像 5,182 枚の追加と `random=1` (マルチスケール学習) を投入して、
**小物体の検出数は 1 件も増えなかった**。

データソース別も同様に悪化: coco 36.0% -> 36.4%、rf 77.1% -> **74.0%**。

**結論**

1. **案G (データ拡充) は Recall 律速に対して無効**であることが実測で確定した。
   hard negative (#137) に続き、**データ側の施策は 2 つとも無効**だった
2. `random=1` (マルチスケール学習) も小物体には効かなかった
3. 悪化の理由は断定できないが、(a) 追加した COCO データに検出不能な小物体が多く
   学習が薄まった、(b) `random=1` が固定 192px 評価と噛み合わなかった、
   のいずれか/両方が考えられる。**切り分けは行っていない** (2 変数を同時に入れたため)
4. **192x192 という入力解像度が小物体検出の構造的な上限**である、という
   #137 以来の仮説が、データ側から潰しきったことで**消去法的に確定**した

**採用モデルは引き続き round1** (`*-phase3r1.weights`、mAP 52.2 / P68 / R49)。
Round A / Round B とも round1 の mAP を超えていない。

**次アクション: 案H (解像度 192 -> 224/256) へ移行する。**
ただし 3.1 のとおりアリーナ超過が確実で、**Phase E (アリーナ余裕確保) が前提**。
そして Phase E の第一歩は **「442,368 バイトが本当に上限なのかを確定させること」**
(7.1)。本 Issue で未検証のまま残している最大の不確実性であり、
ここが崩れれば案H は即座に着手できる。

### 6.1.7 Round C 実測結果 (224px) — **過去最高の mAP。ただし小物体は動かず**

学習 100,000 iteration / **267.9 分**。評価は `unique_truth = 3763` で全ラウンド共通。

| 指標 | Phase 2 | round1 | round2 | Round A | Round B | **Round C (224px)** | KPI |
|---|---|---|---|---|---|---|---|
| mAP@0.50 | 48.9% | 52.2% | 50.9% | 51.1% | 49.1% | **54.97%** | >= 50% ✅ |
| Precision | 49% | 68% | 57% | 53% | 51% | **62%** | >= 60% ✅ |
| Recall | 52% | 49% | 51% | 53% | 52% | **54%** | >= 60% ❌ |
| P + R | 101 | 117 | 108 | 106 | 103 | **116** | - |

**mAP は全ラウンド最高**で、Precision も KPI を満たした。**フロンティアは初めて外側へ動いた**
(mAP 52.2 -> 54.97)。解像度アップには確かに効果があった。

**しかし Recall は +2pt (52 -> 54) に留まり、原因は明確**:

| サイズ | Round A | Round B | **Round C (224px)** |
|---|---|---|---|
| large | 1525/1881 = 81.1% | 79.8% | **1561/1881 = 83.0%** |
| medium | 404/1049 = 38.5% | 37.3% | **424/1049 = 40.4%** |
| **small** | **51/833 = 6.1%** | 6.1% | **49/833 = 5.9%** |

**small は 3 ラウンドとも約 6% で不動。** large +1.9pt / medium +1.9pt と
中〜大は改善したが、小物体は 1 件も増えていない (むしろ 51 -> 49)。

姿勢別・ソース別では大きく改善している:

| 区分 | Round B | **Round C** |
|---|---|---|
| fallen | 65.9% | **73.9%** |
| ambiguous | 62.1% | **66.5%** |
| rf (屋内・被写体が大きい) | 74.0% | **80.7%** |
| coco (屋外・小さい人物が多い) | 36.4% | 36.0% |

**なぜ 224px でも小物体が検出できないのか**

192 -> 224 は**線形で 1.17 倍**にすぎない。normalized area < 0.01 の物体は
192px で約 19x19 px、224px でも約 22x22 px。最細ブランチは **stride 16**
(グリッド 14x14) なので、22px の物体は約 1.4 セルしか占めない。
**解像度を 1.17 倍しても、stride 16 に対する物体の相対サイズはほとんど変わらない。**

小物体を検出可能にするには次のどちらかが要る:

- **stride 8 の検出ブランチ (P2) を追加する = 案J**。物体あたりのセル数が 2 倍になる。
  演算量の増加は解像度を上げるより小さく済む可能性がある
- 解像度を 2 倍 (384px) にする。推論時間 23ms 相当で、改定後 KPI 10ms でも超過

**したがって次の一手は案H の延長ではなく案J (小物体ブランチの追加)。**

### 6.1.8 Round C で判明した不具合: **学習 224px / 変換 192px の不整合**

Round C の変換結果を見ると出力テンソルが **12x12 と 6x6** になっていた。
224px なら 14x14 / 7x7 のはずで、**TFLite が 192px のまま作られていた**。

原因は `cell[38]` の `IMG_SIZE = 192` ハードコード。解像度可変化 (Round C 準備) で
cell[18] / cell[20] / cell[21] だけを直し、**変換側の cell[38] と cfg テンプレートの
cell[17] を見落としていた**。

**影響範囲**

| 対象 | 影響 |
|---|---|
| **darknet の学習** | **なし**。cfg は 224 で正しく学習された |
| **darknet の評価 (mAP/Precision/Recall)** | **なし**。cfg 224 で評価しており 6.1.7 の数値は有効 |
| Step 7.5 の姿勢別診断 | なし (同じく darknet 経由) |
| **TFLite / Vela / MCU 用パラメータ** | **無効**。192px のモデルが出力された |

つまり**精度の結論は有効だが、デプロイ用の成果物は作り直しが必要**。

**対処 (実施済み)**

- `cell[38]`: `IMG_SIZE` を `INPUT_SIZE` から取得するよう修正
- `cell[17]`: cfg テンプレートの `width/height` をプレースホルダ化し `INPUT_SIZE` で置換
- `cell[21]`: ブランチ表示のグリッドを解像度から計算 (`12x12`/`6x6` 固定を廃止)
- 全セルを走査し、コード中の `192` がすべて「`INPUT_SIZE` の既定値」か
  cfg ファイル名のみであることを確認

**教訓**: 解像度のような横断的パラメータを可変化するときは、**関係しそうなセルだけを
見るのではなくノートブック全体を機械的に走査する**こと。今回は
「学習側 3 セルを直した」時点で満足し、変換側を見ていなかった。

### 6.1.9 評価スコープの制定 (2026-08) と Round C の再判定

#### 経緯

6.1.7 で Round C の Recall が 54.1% に留まった原因は `small` (面積比 1% 未満) の
Recall 5.9% だった。ここで **`small` とは何かを実測で定義し直した**ところ、
**屋内用途とは無関係な対象**であることが判明した。

| 区分 | 面積比 | 人物の高さ (画面比・中央値) | 画面上の高さ (224px) |
|---|---|---|---|
| large | >= 10% | 60% | 31〜224 px |
| medium | 1〜10% | 26% | 16〜166 px |
| **small** | **< 1%** | **9%** | **4〜72 px** |

`small` は身長 1.7m の人物なら**十数メートル先**に相当する
(画面の高さ 9% = 視野が縦 19m 相当)。

#### 決定的な根拠: 屋内データに small はほぼ無い

val 4,195 件をデータ由来別に集計 (ローカル実測):

| 由来 | large | medium | **small** | 計 |
|---|---|---|---|---|
| coco (屋外・群衆) | 625 (24.5%) | 943 (37.0%) | **980 (38.5%)** | 2,548 |
| **rf (屋内・転倒データ)** | 1,402 (85.1%) | 240 (14.6%) | **5 (0.3%)** | 1,647 |
| 合計 | 2,027 | 1,183 | 985 | 4,195 |

**val 全体の small 985 件のうち 980 件が COCO 由来**であり、
屋内相当データには **1,647 件中 5 件 (0.3%)** しか存在しない。

つまり Recall 54.1% という数字は、**製品が扱わない対象を 4 分の 1 近く
含めて測った値**だった。

#### 制定したスコープ

ユーザー判断により、**屋外利用は想定しない**ことを前提に評価スコープを制定した
(`product-requirements.md` 3.1.2)。

| # | 定義 |
|---|---|
| **S-1** | 画面に占める面積が **1% 以上**の人物を検出対象とする |
| **S-2** | **屋内相当データ**を主評価とし、屋外・群衆データは参考値とする |

#### Round C のスコープ適用後の判定

| 集計範囲 | Recall | KPI 60% |
|---|---|---|
| 従来の測り方 (スコープ外を含む) | 54.1% | ❌ |
| **S-1 適用 (small 除外)** | **1,985/2,930 = 67.7%** | ✅ |
| **S-2 適用 (rf のみ)** | **80.7%** | ✅ |
| (参考) large のみ | 83.0% | ✅ |

**Round C は評価スコープ下で全 KPI を達成した。**

| 指標 | Round C | KPI | 判定 |
|---|---|---|---|
| mAP@0.50 | 54.97% | >= 50% | ✅ |
| Precision | 62% | >= 60% | ✅ |
| **Recall (S-1 適用)** | **67.7%** | >= 60% | ✅ |
| 推論時間 | 7.86 ms (見込み) | <= 10ms (改定後) | ✅ |
| メモリ | +195KB / 583.5KB 空き | 収まること | ✅ |

#### スコープ外として記録する事実

本スコープは「測り方」を製品用途に合わせるものであり、
**技術的な制約が消えたわけではない**。以下は明示的に残す。

- `small` の Recall は **5.9%** で実質的に検出できていない。
  **屋外・広い空間へ用途を広げる際は再び律速になる**
- `medium` (数メートル先に相当) も **40.4%** で高くない。
  **屋内でも部屋が広ければ medium に該当する**ため、
  想定する部屋の広さ・カメラ設置距離の定義が望ましい
- 小物体を本気で改善するなら **案J (stride 8 ブランチ追加)** が次の一手
  (6.1.7)。解像度 2 倍 (384px) は推論 23ms で KPI 10ms でも超過する

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

### 7.1 メモリ実測結果 (2026-08 実施・完了) — **壁は無かった**

ビルド成果物 `e2studio_CPU0/Debug/mimamori_sense_CPU0.map` (LLVM lld のマップ) と
`e2studio_CPU0/Debug/memory_regions.lld` から実測した。

**メモリ領域の定義** (`memory_regions.lld`、FSP 生成)

| 領域 | CPU0 | CPU1 |
|---|---|---|
| 内蔵 RAM | `0x22000000` + `0x001b0000` = **1,728.0 KB** | `0x221b0000` + `0x00024000` = 144.0 KB |
| 内蔵 FLASH | `0x02000000` + `0x000f8000` = **992.0 KB** | `0x020f8000` + `0x00008000` = 32.0 KB |
| SDRAM | `0x68000000` + `0x04000000` = **64.0 MB** | `0x6c000000` + `0x04000000` = 64.0 MB |

**アリーナの実際の配置**: `sub_0000_net1_arena` は section 属性の無い通常のグローバル配列で、
`.bss` に置かれている (map: `0x2200c650`、サイズ `0x6c000` = 442,368 B)。つまり**内蔵 RAM**。

**CPU0 内蔵 RAM の使用状況** (出力セクションのみを集計)

| 項目 | 値 |
|---|---|
| 容量 | 1,769,472 B = **1,728.0 KB** |
| 使用 | 1,172,014 B = **1,144.5 KB (66.2%)** |
| **空き** | **597,458 B = 583.5 KB** |

うちアリーナが 432.0 KB、LVGL バッファ 256.0 KB、FreeRTOS ヒープ 256.0 KB、
AI 推論スレッドの入力バッファ 108.0 KB。

**解像度を上げた場合の収支**

| 入力 | アリーナ見積り | 増分 | 残り空き | 判定 |
|---|---|---|---|---|
| 192x192 (現行) | 442,368 B | — | 583.5 KB | — |
| **224x224** | 602,112 B | +156.0 KB | **427.5 KB** | ✅ **収まる** |
| **256x256** | 786,432 B | +336.0 KB | **247.5 KB** | ✅ **収まる** |

**内蔵 FLASH の使用状況 (こちらは逼迫している)**

| 項目 | 値 |
|---|---|
| 容量 | 1,015,808 B = 992.0 KB |
| 使用 | 988,678 B = **965.5 KB (97.3%)** |
| 空き | 27,130 B = **26.5 KB** |

うち NPU モデル本体 `sub_0000_net1_model_data` が 433,760 B = 423.6 KB。
**畳み込みの重みは入力解像度に依存しない**ため、解像度を上げても FLASH はほぼ不変の見込み。
ただし残り 26.5 KB しかないため、**Vela 出力が少しでも増えると入らない**。要実測。

**結論**

1. **「アリーナ 432KB が上限」は誤り。実際は内蔵 RAM 1,728 KB のうち 583.5 KB が空いている**
2. 224px / 256px とも**内蔵 RAM には収まる見込み**。Phase E の「余裕確保」作業は不要
3. 制約は RAM ではなく **FLASH (残り 26.5 KB)** に移った
4. SDRAM は 64 MB 中 5.4 MB しか使っておらず潤沢だが、アリーナを SDRAM に置くと
   NPU のアクセスが遅くなり推論時間 5ms を割る恐れがある。**内蔵 RAM に置いたまま**が前提

**この実測が覆した前提**

`f003_03j` 3.1 は「アリーナは 442,368 バイト上限ピッタリ」として
解像度・チャネル・branch 増をすべて非採用にしていた。**その根拠は成立しない**。
Phase 3 以降の「構造は変えられない」という制約は、**実測されないまま引き継がれた思い込み**だった。

> **`f003_03j` の記述の訂正**: 同ドキュメント 3.1 および本ドキュメントの旧記述にあった
> 「`sub_0000_net1_invoke.c` が 442,368 バイトの `fast_scratch` も宣言している」は誤り。
> 実際は `uint8_t* sub_0000_net1_fast_scratch = sub_0000_net1_arena;`
> (`sub_0000_net1_invoke.c:22`) で**同じアリーナを指すポインタ**であり、
> 追加のメモリは消費していない。

### 7.1.1 残る未検証事項

1. **Vela 実測**: 上表のアリーナ見積りは「入力画素数に比例」という仮定の概算。
   224/256px の tflite を Vela に通して実際の Arena サイズを確認すること
2. **単一サブグラフの維持**: 解像度が変わっても `sub_0000` 単一で収まるか
3. **推論時間 5ms**: アクティベーションが 1.36〜1.78 倍になるぶん推論時間も伸びる。
   Ethos-U55 の実測が必要
4. **FLASH 残り 26.5 KB**: Vela 出力が現行 423.6 KB から増えないか
5. MCU 側の入力バッファ (現行 110,592 B = 192x192x3) も増える。
   **アリーナと入力バッファを合わせた収支**は以下のとおりで、いずれも収まる:

| 入力 | アリーナ増 | 入力バッファ増 | 合計増 | 残り空き | 判定 |
|---|---|---|---|---|---|
| 224x224 | +156.0 KB | +39.0 KB | +195.0 KB | **388.5 KB** | ✅ |
| 256x256 | +336.0 KB | +84.0 KB | +420.0 KB | **163.5 KB** | ✅ |

### 7.1.2 Vela 実測 (2026-08、ローカル実行) — 制約は RAM から**推論時間**へ

**Vela はローカル (Windows) で動く。** `pip install ethos-u-vela` だけで入り、
Colab セッションも Drive も不要。構造検証はこれで完結する。

```
vela --accelerator-config ethos-u55-256 --optimise Performance \
     --output-dir <out> dataset/models/yolo_fastest_person_darknet_int8.tflite
```

**現行 192x192 モデルの実測値** (既定 `Ethos_U55_High_End_Embedded` / 500MHz / `Shared_Sram`)

| 項目 | 値 |
|---|---|
| **CPU operators** | **0 (0.0%)** |
| **NPU operators** | **108 (100.0%)** |
| Total SRAM used | 360.83 KiB |
| Total Off-chip Flash used | 358.48 KiB |
| nn_macs | 40,460,832 |
| cycles_total | 2,769,261 |
| **inference_time** | **5.54 ms** |

**判明したこと 1: `Resize` 未対応の警告は誤検出だった**

ノートブック cell[40] は毎回
`WARNING: Ethos-U55未対応の可能性があるオペレータ: {'Resize'}` を出していたが、
実際に Vela に通すと **CPU operators = 0 / NPU operators = 100%** で
全オペレータが NPU 実行される。**ヒューリスティックが誤っていた**。
cell[40] を修正し、判定は Vela の CPU/NPU 比率で行うようにした。

**判明したこと 2: メモリではなく推論時間が制約になる**

Vela の SRAM 360.83 KiB に対し MERA のアリーナは 442,368 B (432 KiB) で、比 1.20。
入力面積に比例するとして換算すると:

| 入力 | Vela SRAM 換算 | MERA アリーナ換算 | 内蔵 RAM 収支 (7.1) | **推論時間 (比例換算)** |
|---|---|---|---|---|
| 192x192 | 360.8 KiB | 432 KiB | 583.5 KB 空き | **5.54 ms** |
| 224x224 | 491 KiB | 588 KiB | 388.5 KB 空き ✅ | **7.54 ms** ❌ |
| 256x256 | 641 KiB | 768 KiB | 163.5 KB 空き ✅ | **9.85 ms** ❌ |

**KPI「NPU 推論時間 5ms 以内」に対し、192px ですら Vela 見積りは 5.54ms で超過している。**

ただしこの絶対値は信用しきれない:

- Vela の既定システム構成 (500MHz / Shared_Sram) が EK-RA8P1 実機と一致する保証がない
- `f003_01_model_selection_report.md:73,76` は **EK-RA8P1 実機で約 3ms** と記録しているが、
  これは **192x192x1 グレースケールの顔認識サンプル**の値で、
  本件の 192x192x3 RGB 人物検出モデルの実測ではない
- **本モデルの実機推論時間は測定されていない**

実機 3ms を基準に比例換算すると 224px = 4.1ms (KPI 内)、256px = 5.3ms (超過) となり、
**224px なら成立、256px は厳しい**という別の結論になる。

**したがって次にやるべきは「現行 192px モデルの実機推論時間の実測」**である。
これが 3ms 側なら 224px は問題なく進められ、5.5ms 側なら解像度アップは
推論時間の壁に当たる。**この 1 点で案H の可否が決まる。**

### 7.1.3 224/256px の Vela 実測がまだできていない理由

Vela に通すには **その解像度の INT8 tflite が必要**で、生成には
darknet cfg + weights -> Keras (`keras-YOLOv3-model-set` の `convert.py`) -> TFLite
の経路をたどる。ローカルには TensorFlow も darknet weights も無い
(weights は Drive 上、1,150 KB)。

YOLO-Fastest は全層畳み込みなので **重みは解像度非依存**であり、
cfg の `width/height` を 224 に変えて既存の Phase 3 weights を通すだけで
224px の tflite を作れる。Colab で変換セル (Step 8-10) だけを回せば
学習なしで数分で得られる。

### 7.1.4 実機推論時間の実測 (2026-08) — **現行モデルが既に KPI 未達**

`ai bench` コマンド (本 Issue で追加) を EK-RA8P1 実機で実行した結果:

```
Samples        : 1915
CPU clock      : 1000000000 Hz (1000 cycles/us)
min            : 5777 us (5777254 cyc)   <- 代表値
avg            : 6093 us
max            : 21401 us
```

| 指標 | 値 |
|---|---|
| **実機 min** | **5.777 ms** |
| 実機 avg | 6.09 ms |
| 実機 max | 21.40 ms (他タスクへのプリエンプション込み) |
| Vela 見積り | 5.539 ms |
| **乖離** | **+4.3%** |

min は 1,034 サンプル時点と 1,915 サンプル時点で `5777254 cyc` 完全一致しており、
値は安定している。

**判明 1: Vela の見積りは信用できる (誤差 4.3%)**

これにより **224/256px の tflite を作らなくても Vela 換算で判断してよい**。
7.1.3 に書いた「Colab で変換して Vela に通す」作業の優先度は下がる。

**判明 2: 現行 192px モデルは既に KPI 5ms を超えている**

`product-requirements.md:115` の KPI「F-003 AI推論時間 5ms以内」に対し **5.78 ms**。
同 `:23` に「KPI-02（推論時間）は未測定」とあるとおり、**これが初の実測**である。
`f003_01_model_selection_report.md` の「実機 約3ms」は 192x192x1 グレースケールの
顔認識サンプルの値で、本モデル (192x192x3 RGB) には当てはまらなかった。

**判明 3: 解像度アップは時間の壁に当たる**

| 入力 | 推論時間 (実機基準の面積比換算) | KPI 5ms |
|---|---|---|
| 192x192 (現行) | 5.78 ms | ❌ |
| 224x224 | **7.86 ms** | ❌ |
| 256x256 | **10.27 ms** | ❌ |

**メモリは足りていた (7.1) が、時間が足りない。** これが案H の真の制約である。

### 7.1.5 ここから取りうる選択肢

**選択肢 A: KPI 5ms の妥当性を見直す**

`product-requirements.md:115` の設定根拠は
「**顔認識のサンプルプログラムが3msで推論していたことから達成可能であろう時間を設定**」
であり、**製品要件から導かれた数値ではない**。しかも比較対象のサンプルは
別モデル (グレースケール顔認識) だった。

フレーム予算に対する占有率:

| フレームレート | 1 フレーム | 推論 5.78ms の占有率 |
|---|---|---|
| 30fps (KPI-01 目標) | 33.3 ms | 17.3% |
| 20fps (現状、#176 で改善中) | 50.0 ms | 11.6% |

推論は NPU 上で走り、その間 CPU は他の処理に使える。
**見守り用途で毎フレーム推論する必要があるかも含め、要件の再定義が有効**。
仮に 10ms 以内で良ければ 224px (7.86ms) がそのまま通る。

**選択肢 B: 224px にしつつモデルを細くする**

MAC 数は概ね「入力面積 x 幅^2」に比例する。解像度を上げつつチャネル幅を削れば
時間内に収まる:

| 構成 | 推論時間 (換算) | KPI 5ms |
|---|---|---|
| 224px x 幅 1.00 | 7.86 ms | ❌ |
| 224px x 幅 0.85 | 5.68 ms | ❌ |
| 224px x 幅 0.80 | 5.03 ms | ほぼ境界 |
| **224px x 幅 0.75** | **4.42 ms** | ✅ |
| 224px x 幅 0.70 | 3.85 ms | ✅ |

小物体検出には**チャネル幅より解像度が効く**ため、この交換は理にかなう。
ただし cfg のチャネル数変更 = **アーキテクチャ変更**であり、
再学習 + RUHMI/MERA 再生成 + MCU 側パラメータ更新が必要になる。

**選択肢 C: 現行 192px のまま運用範囲を定義する**

6.1.3 のとおり、屋内・被写体が大きい `rf` サブセットでは Recall 74.0% 出ている。
「見守りカメラとして検出すべき人物サイズ」を定義し直せば、
現行モデルで要件を満たせる可能性がある。

**推奨順序**: **A (要件の再定義) を先に確認 -> B (224px + 幅削減)**。
A は判断だけで済み、結果次第で B の設計目標 (許容時間) が決まる。
C は A の一部として検討する。

### 7.1.6 224px 版の変換と Vela 実測 (2026-08) — **見積りは全項目で的中**

6.1.8 の不整合を受け、学習なしで変換だけをやり直した
(`dataset/scripts/convert_only_colab.py`)。**形状検証 (入力 [1,224,224,3] /
出力グリッド 14x14・7x7) を通過**し、正しい 224px 成果物を得た。

**Vela 実測の比較**

| 項目 | 192px | **224px** | 比 |
|---|---|---|---|
| Total SRAM used | 360.83 KiB | **490.83 KiB** | 1.36x |
| Off-chip Flash used | 358.48 KiB | **359.36 KiB** | **1.00x** |
| nn_macs | 40,460,832 | **55,071,688** | 1.36x |
| **CPU operators** | 0 | **0** | 変わらず |
| **NPU operators** | 108 (100%) | **108 (100%)** | 変わらず |
| INT8 tflite | 503.0 KB | **503.0 KB** | 1.00x |
| Vela 出力 tflite | 374.0 KB | **376.1 KB** | 1.01x |

**7.1 / 7.1.2 の見積りは全項目で的中した**:

| 予測 | 実測 | 判定 |
|---|---|---|
| MAC は入力面積比 1.361 倍 | 1.361 倍 | ✅ ほぼ完全一致 |
| FLASH はほぼ不変 (重みは解像度非依存) | 1.00 倍 (+0.9 KiB) | ✅ |
| 単一サブグラフ・全 NPU 実行を維持 | CPU 0 / NPU 108 | ✅ |
| 推論時間 7.86 ms | (MAC 比換算で) 7.86 ms | ✅ |

**制約の最終確認**

| 制約 | 値 | 判定 |
|---|---|---|
| 推論時間 | 7.86 ms (実機 192px 実測 5.777ms x MAC比 1.361) | ✅ 改定後 KPI 10ms 以内 |
| 内蔵 RAM | アリーナ換算 587.6 KB + 入力バッファ増 39.0 KB = **+194.6 KB** -> 残り **388.8 KB** | ✅ |
| 内蔵 FLASH | ほぼ不変 (残り 26.5 KB を食わない) | ✅ |
| 単一サブグラフ | 維持 | ✅ |

**7.1.1 に挙げた未検証事項 5 件はすべて解消した。**

**MCU 側へ反映する値 (Round C / 224px)**

```
入力解像度: 224x224x3   入力バッファ 150,528 バイト (現行 110,592 から +39,936)
アンカー  : 10,23, 29,59, 53,130, 104,79, 125,148, 196,184

#define POSTPROC_BRANCH1_SCALE       (0.11362218f)   /* 14x14 */
#define POSTPROC_BRANCH1_ZERO_POINT  (36)
#define POSTPROC_BRANCH0_SCALE       (0.10635098f)   /* 7x7 */
#define POSTPROC_BRANCH0_ZERO_POINT  (30)
```

成果物: Drive の `issue148_roundC/model_fixed/`

### 7.1.7 変換で踏んだ 2 つの互換性問題 (記録)

`convert_only_colab.py` を新規に書き起こしたことで、学習ノートブックが
暗黙に回避していた問題が表面化した。両方ともスクリプトに恒久対処済み。

| # | 症状 | 原因 | 対処 |
|---|---|---|---|
| 1 | `AttributeError: module 'numpy' has no attribute 'product'` | 変換ツールが古く、NumPy 2.0 で削除された `np.product` を使う | clone 後に `np.product` -> `np.prod` を全 `.py` へ適用。**clone の有無に関わらず毎回実行**する (べき等。clone 済み VM で再実行したときの取りこぼしを防ぐ) |
| 2 | `ValueError: Unrecognized keyword arguments passed to Conv2D: {'weights': ...}` | 変換ツールは `Conv2D(..., weights=[...])` という Keras 2 の書き方をするが、Colab の TF 2.20 は既定で Keras 3.13 を使う | `TF_USE_LEGACY_KERAS=1` を設定し `tensorflow.keras` を `tf_keras` (Keras 2 系) に向ける |

> **デバッグ上の注意**: 症状 2 の例外メッセージには**重み配列全体が含まれる**ため、
> 出力をそのまま流すとエラー本文が数千行の数値に埋もれて読めない。
> `convert_only_colab.py` の `run()` は出力を末尾 40 行に切り詰めるようにした。

### 7.1.8 RUHMI/MERA 再生成の実測 (2026-08) — 全制約クリア

`scripts/deploy_fall_detection.ps1` で 224px の INT8 tflite から `mera/` を再生成した。
**RUHMI は実機構成 (System configuration: RA8P1 / Memory mode: Sram_Only) で
Vela を回す**ため、7.1.2 のローカル実行 (既定の Shared_Sram) より信頼できる。

| 項目 | 値 |
|---|---|
| Accelerator | Ethos_U55_256 / 500 MHz |
| System configuration | **RA8P1** |
| Memory mode | **Sram_Only** |
| **CPU operators** | **0 (0.0%)** |
| **NPU operators** | **112 (100.0%)** (192px 時は 108) |
| Total SRAM used | 588.00 KiB |
| Total On-chip Flash used | 435.86 KiB |
| nn_macs | 55,097,560 |

**生成物の検証**

| 項目 | 期待値 | 実測 | 判定 |
|---|---|---|---|
| `kArenaSize_sub_0000_net1` | 602,112 (7.1 の見積り) | **602,112** | ✅ **完全一致** |
| 入力バッファ | 150,528 (224x224x3) | **150,528** | ✅ |
| 出力 branch1 | 3,528 (14x14x3x6) | **3,528** | ✅ |
| 出力 branch0 | 882 (7x7x3x6) | **882** | ✅ |
| arena のセクション属性 | `.bss` (内蔵 RAM) のまま | `.bss` (属性なし) | ✅ |

`sub_0000_net1_fast_scratch` は今回もアリーナを指すポインタで、追加消費はない。

**メモリ収支 (確定値)**

| 項目 | 192px | 224px | 増分 |
|---|---|---|---|
| アリーナ | 442,368 | **602,112** | +159,744 |
| 入力バッファ | 110,592 | **150,528** | +39,936 |
| 合計 | — | — | **+199,680 B** |
| **内蔵 RAM 残り空き** | 597,458 | **397,778 B (388.5 KB)** | ✅ |

**FLASH は実質不変 (先の見積りを訂正)**

| 項目 | 192px | 224px | 差 |
|---|---|---|---|
| `sub_0000_net1_model_data` | 433,760 | **433,712** | **-48 B** |
| `sub_0000_net1_command_stream` | 12,624 | **12,600** | **-24 B** |
| 合計 | 446,384 | **446,312** | **-72 B** |
| **FLASH 残り空き** | 27,130 B | **27,202 B (26.6 KB)** | ✅ |

> **訂正**: Vela が報告する `Total On-chip Flash used 435.86 KiB` を
> 7.1 の「モデル本体 423.6 KB」と比較して「+12.5 KB 増えた」と判断したのは誤り。
> 435.86 KiB = model_data + command_stream の合計であり、
> 実際は **72 バイト減っている**。「重みは入力解像度に依存しない」という
> 7.1 の予測どおりだった。

**7.1.1 の未検証事項はすべて解消し、案H の制約はすべてクリアした。**

残るは実機でのビルド・書き込みと、`ai bench` による推論時間の実測
(予測 7.86 ms、改定後 KPI 10ms) である。

### 7.1.9 実機検証結果 (2026-08) — **Phase D 完了**

e2 studio でビルドし EK-RA8P1 に書き込んで確認した。

**ビルド・動作**

| 項目 | 結果 |
|---|---|
| ビルド | ✅ 成功 |
| 検出動作 | ✅ **人物に枠が出る** |
| `ai model` の表示 | 入力 224x224x3 (150,528 B) / Branch0 7x7 (882 B) / Branch1 14x14 (3,528 B) |

**推論時間 (`ai bench`、1,885 サンプル)**

| 指標 | 値 |
|---|---|
| **min (代表値)** | **7.777 ms** |
| avg | 7.823 ms |
| max | 15.97 ms (プリエンプション込み) |
| **KPI 10ms** | ✅ **達成** |

**予測精度**

| | 値 |
|---|---|
| 予測 (192px 実測 5.777ms x MAC 比 1.361) | 7.86 ms |
| **実測** | **7.777 ms** |
| **誤差** | **-1.1%** |

逆方向の整合も取れている。224px の実測から 192px を逆算すると 5.714 ms で、
実測 5.777 ms に対し -1.1%。**「MAC 数は入力面積に比例する」という仮定は
両方向で 1% 精度で成立**した。

**Issue #148 の KPI 最終判定**

| 指標 | 実測 | KPI | 判定 |
|---|---|---|---|
| mAP@0.50 | 54.97% | >= 50% | ✅ |
| Precision | 62% | >= 60% | ✅ |
| **Recall (評価スコープ S-1 適用)** | **67.7%** | >= 60% | ✅ |
| (参考) Recall (屋内相当データのみ) | 80.7% | — | — |
| **推論時間** | **7.777 ms** | <= 10ms | ✅ |
| 内蔵 RAM | +199,680 B (残り 388.5 KB) | 収まること | ✅ |
| 内蔵 FLASH | -72 B (残り 26.6 KB) | 収まること | ✅ |
| 単一サブグラフ | CPU 0 / NPU 112 (100%) | 維持 | ✅ |

**Issue #148 は全 KPI を達成した。**

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

### R9. `mosaic` はこの darknet ビルドでは使用不能 (実測)

- Round B 初回で `mosaic=1` にしたところ、darknet が**データ読み込みスレッド起動直後に
  SIGSEGV** した。空ラベル 0 件のクリーンなデータでも再現する (6.1.4 ③)
- `#137 R7` は「negative (bbox ゼロ) 画像との併用が原因」としていたが、
  **negative の有無に関わらず落ちる**。R7 の理解を本節で更新する
- `random=1` (マルチスケール) と `-map` は同条件で正常動作した。
  小物体対策としては `random` を使う
- 将来 mosaic を使いたい場合は darknet 本体の更新か別フォークへの差し替えが必要。
  cfg で有効化するだけでは動かない

### R10. `-map` は使用不能 (実測)

- 初回 mAP 計算 (`4 * train画像数 / (batch * subdivisions)` iteration 目) で
  darknet が **SIGABRT (exit=-6)** する (6.1.5)
- `_best.weights` は作られないため、評価対象は常に `_final.weights` になる
- **短時間の生存確認では検出できない**。初回 mAP 計算まで到達しないと再現しない。
  同種のオプションを試す場合は「初回イベントが起きる iteration」を計算し、
  そこを超えるまで走らせて確認すること

### R11. Colab セッションは頻繁に失われる

- 本 Issue の作業中に **3 回**セッションを失った (アイドル回収と思われる)。
  失われると `/content` は全消去され、`colab ls` / `download` も
  「File or directory not found」という紛らわしいエラーになる
- **VM 上にしか無いファイルは失われる前提で設計する**。ログ・レポートは
  その場で Drive (`GDRIVE_RUN_DIR`) にコピーすること
- 実行中 (`Status: BUSY`) もファイル操作は受け付けられず同じエラーになる。
  進捗確認は `colab --auth=adc status -s <name>` を使う
  (`Cell: ... at <時刻>` が更新されていれば次のセルに進んでいる)

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

**Round B (データ拡充) — いまここ**

6. **Drive**: Round A の 3 本に加えて **`expand_coco_person.py`** を配置する
   (データセット zip は Round A と同じものでよい)
7. **Colab**: cell[8] が `ROUND = 'B'` / `USE_MAP = True` であることを確認して実行。
   cell[9] が COCO から追加画像を取得し (数分〜十数分)、保存先は `issue148_roundB/`
8. **判定**: `_best` の値で 6.1 の KPI を判定し、`_final` の参考値と見比べて
   「データ拡充の効果」と「best 選択の効果」を切り分ける。
   6.2 の Round B 分岐表に従って Round C/D または Phase D へ
9. 実測値が出たら「6.1.3 Round B 実測結果」節を追記する

**Round A の診断 JSON の回収 (未実施)**

`issue148/issue148_fallen_recall.json` は Drive にあるが、VM 上の `/content` は
アイドル回収で消えるため `colab download /content/...` では取れない。
Drive の Web UI から直接ダウンロードするか、Drive をマウントした状態で
`colab download -s <name> /content/drive/MyDrive/yolo_fastest_darknet_person/issue148/issue148_fallen_recall.json ./`
とする。**この中身 (fallen / upright / small 別の Recall) が案G と案H の分岐材料になる。**

---

## 11. 参考情報

- Phase 3 引き継ぎ: `doc/report/f003_03j_person_detection_phase3_plan.md` (3 章 評価マトリクス / 6.1.1-6.1.3 / 8 章 R1・R7)
- Phase 2 引き継ぎ: `doc/report/f003_03h_person_detection_phase2_plan.md`
- Colab CLI 運用: `doc/colab-cli-setup-guide/colab-cli-setup-guide.md`、`dataset/scripts/colab_cli/README.md`
- アリーナ実測値: `e2studio_CPU0/src/ai_application/fall_detection/mera/sub_0000_net1_tensors.h:11`
- COCO 2017: https://cocodataset.org/
- darknet CFG パラメータ: https://github.com/AlexeyAB/darknet/wiki/CFG-Parameters-in-the-%5Bnet%5D-section
- Yolo-Fastest ベース: https://github.com/dog-qiuqiu/Yolo-Fastest
