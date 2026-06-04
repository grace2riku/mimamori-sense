# RUHMI Framework バージョン確認・更新要否レポート

本レポートはIssue #142の成果物である。ローカルPC（`C:\work\ruhmi-framework-mcu`）にインストール済みの RUHMI Framework AI MCU Compiler（中身は EdgeCortix MERA）と、GitHub リポジトリ（[renesas/ruhmi-framework-mcu](https://github.com/renesas/ruhmi-framework-mcu)）の最新版を比較し、更新の要否についての見解をまとめる。

- **調査日**: 2026-06-05
- **Issue**: #142
- **対象**: RUHMI Framework AI MCU Compiler（MERA / vela）
- **比較情報源**: GitHub `renesas/ruhmi-framework-mcu`（main / releases / install）、ローカルのリファレンスクローン `reference_projects/ruhmi-framework-mcu`

---

## 0. 結論サマリ

- **GitHub はローカルPCより更新されている: Yes**。
  - ローカルPC mera `2.5.0+pkg.3577`（2025-09-30） → GitHub 最新 mera `2.6.0+pkg.4513`。
  - vela は `4.2.0` のまま**変更なし**。
- ローカルのリファレンスクローン取得時点（commit `82ebd1c` / `Release-2026-02-02`、MERA 2.5.0）以降、GitHub では **2回のリリース**（`Release-2026-03-11`、`Release-2026-04-27`）が行われている。
- 最も影響が大きい変更は次の2点:
  1. **MERA 2.5.0 → 2.6.0**（pkg.3577 → pkg.4513）。CPUサブグラフ出力テンソルをNPUサブグラフ入力へ供給する不具合の修正を含む。
  2. **スクリプトの統合（破壊的変更）**: 従来の `scripts/mcu_deploy.py` と `scripts/mcu_quantize.py` が廃止され、統合スクリプト **`scripts/mcu_compile.py`** に置き換えられた。
- **更新すべきか（見解）**: 本プロジェクト F-003（転倒検出AI）の手順書群が旧スクリプト（`mcu_deploy.py` / `mcu_quantize.py`）前提で書かれているため、**段階的な更新を推奨**する。ただし F-003 の量子化・変換を未実施の現段階で慌てて更新する必要はなく、**実際にモデル変換を実行する直前**に MERA 2.6.0 へ更新し、同時に手順書を `mcu_compile.py` ベースへ改訂するのが安全。Ethos-U55 / INT8 サポートおよびベースラインモデル `yolo-fastest-192_face_v4` は継続サポートされており、更新による機能的後退リスクは小さい（詳細は第4章）。

---

## 1. バージョン比較（Phase A / B）

### 1.1 対比表

| 項目 | ローカルPC（現状） | GitHub 最新版 | 差分 |
|---|---|---|---|
| mera バージョン | `2.5.0+pkg.3577`（released 2025-09-30） | `2.6.0+pkg.4513` | **更新あり**（2.5.0→2.6.0） |
| vela バージョン | `4.2.0` | `4.2.0` | 変更なし |
| MERA wheel（Windows） | `mera-2.5.0+pkg.3577-cp310-cp310-win_amd64.whl` | `mera-2.6.0+pkg.4513-cp310-cp310-win_amd64.whl` | **更新あり** |
| MERA wheel（Linux） | `mera-2.5.0+pkg.3577-cp310-cp310-manylinux_2_27_x86_64.whl` | `mera-2.6.0+pkg.4513-cp310-cp310-manylinux_2_27_x86_64.whl` | **更新あり** |
| mera_visualizer wheel | `mera_visualizer-2.5.0-py3-none-any.whl`（リファレンスクローン記載） | `mera_visualizer-2.5.0+mcuv4-py3-none-any.whl`（GitHub API確認） | ビルドメタ更新 |
| `get_mera_dna_version()` | None | 未確認 | - |
| `get_mera_tvm_version()` | 空 | 未確認 | - |
| Python | 3.10 | 3.10 | 変更なし |

> 補足: GitHub install/README.md の `mera.__version__` 出力例は `2.6.0+pkg.4513`、`vela --version` は `4.2.0` と記載されている（2026-06-05時点）。mera_visualizer の wheel 名は GitHub API 確認で `mera_visualizer-2.5.0+mcuv4-py3-none-any.whl`。MERA 2.6.0 系に対応する補助ツールである（バージョン本体は 2.5.0 系）。

### 1.2 リリース／コミット情報

| 項目 | ローカルのリファレンスクローン取得時点 | GitHub 最新版 |
|---|---|---|
| コミット | `82ebd1cfd663303f3de751936cd6279e054ed51a`（2026-02-08） | 個別ハッシュは未確認。main は約169コミット。最新リリースタグ後さらに約8コミット |
| 直近リリースタグ | `Release-2026-02-02`（`a458bb2`、MERA 2.5.0） | `Release-2026-04-27`（`757fb63422ed66585d8892d2ea623ec6856e681e`、MERA 2.6.0） |

> 注: ローカルPC（`C:\work\ruhmi-framework-mcu`）のインストール環境はリポジトリ管理外のため、本レポートからは直接参照できない。ユーザー確認済みの mera `2.5.0+pkg.3577` / vela `4.2.0` を現状値とした。リファレンスクローン（`reference_projects/ruhmi-framework-mcu`）の install/README.md も `2.5.0+pkg.3577` を参照しており、ローカルPCのバージョンと整合する。

---

## 2. ローカルPCから更新されているか

**Yes（更新されている）。**

- mera 本体が `2.5.0+pkg.3577` から `2.6.0+pkg.4513` へ上がっている。
- vela は `4.2.0` で据え置き。
- ローカルのリファレンスクローン取得時点（`Release-2026-02-02`）から GitHub 最新（`Release-2026-04-27`）まで、間に `Release-2026-03-11` を含む2回のリリースが挟まる。

---

## 3. 差分調査（Phase C）

ローカル取得時点 `Release-2026-02-02`（MERA 2.5.0）以降の主なリリース変更点（GitHub releases より）:

### 3.1 Release-2026-03-11（commit `b0a3a12`）

| 分類 | 内容 | F-003への関連 |
|---|---|---|
| **破壊的変更** | `mcu_compile.py` を、従来の `mcu_deploy.py` と `mcu_quantize.py` を統合した**統一スクリプト**としてリリース | 大（手順書の改訂が必要） |
| 新規ユーティリティ | `scripts/utils/check_model_metrics.py` 追加 | 中（精度・メトリクス確認に有用） |
| ドキュメント | CIFAR-10 量子化ワークフローのチュートリアル追加 | 中（量子化手順の参考） |
| FSP | アプリケーション例を FSP 6.4.0 対応に更新 | 小（本体は FSP 6.3.0。移行は別判断） |

### 3.2 Release-2026-04-27（commit `757fb63`、現時点の最新タグ）

| 分類 | 内容 | F-003への関連 |
|---|---|---|
| **MERAバージョン** | MERA 2.6.0（pkg.4513）の Windows/Linux インストールパッケージへ改訂 | 大（本更新の中心） |
| **バグ修正** | CPUサブグラフの出力テンソルをNPUサブグラフの入力として利用できるよう復旧 | 中（CPU+NPU混在モデルの変換信頼性向上。転倒検出の後処理分割に影響しうる） |
| **破壊的変更** | レガシースクリプト `mcu_quantize.py` を削除（`mcu_compile.py` へ統合済み） | 大（旧スクリプト前提の手順は要改訂） |
| ライセンス | MERA RA8 Support Packages（binary / wheel）をライセンス一覧で明示化 | 小 |
| FSP | ベンチマークプロジェクトを FSP 6.4.0 へ更新 | 小 |

> 注: タグ `Release-2026-04-27` 以降も main に追加コミット（約8件）があり、micro な修正が継続している。個別コミットの内容は本調査では未確認。

### 3.3 サポート対象・オペレータ・API の変更有無

| 観点 | 結果（GitHub main 最新 vs ローカル） |
|---|---|
| 対象NPU（Ethos-U55） | **変更なし**。`docs/models_tested.md` は引き続き「RA8P1 with Ethos U55 and Cortex-M85」を対象とする |
| 量子化形式（INT8） | **変更なし**。INT8（および FP32 / CPU-only）対応を継続 |
| ベースラインモデル | `yolo-fastest-192_face_v4`（顔検出、F-003のベースライン）は引き続きテスト済みモデル一覧に存在 |
| 対応オペレータ（`docs/operator_support.md`） | TFLiteLeakyRelu / OnnxDepthToSpace 対応は `Release-2026-02-02` 時点で既にローカルへ反映済み。それ以降の operator 表の大きな追加・削除は本調査では確認されず（個別差分は未精査） |
| ランタイムAPI（`docs/runtime_api.md`） | 本調査では明確な破壊的API変更は確認されず（未精査箇所あり） |

---

## 4. RUHMI Framework を更新すべきか（見解）

### 4.1 更新のメリット

- **不具合修正の取り込み**: CPUサブグラフ出力→NPUサブグラフ入力の復旧は、後処理（NMS等）を含む混在モデルを Ethos-U55 へデプロイする F-003 のユースケースで信頼性向上が期待できる。
- **公式手順との整合**: GitHub の手順・ドキュメントは既に `mcu_compile.py` 前提に移行している。ローカルを最新化すれば、公式ドキュメント・チュートリアル（CIFAR-10量子化等）と齟齬なく作業できる。
- **将来サポート**: 新規 issue 報告やサポートを受ける際、最新版が前提となる可能性が高い。

### 4.2 更新のリスク・コスト

- **破壊的変更（スクリプト統合）**: `mcu_deploy.py` / `mcu_quantize.py` が無くなり `mcu_compile.py` に統合される。本プロジェクトの F-003 手順書群はこれらの旧スクリプト名を前提に記述されており、**手順書の改訂が必須**となる。
- **コンパイラ挙動の差**: MERA 2.5.0→2.6.0 でコード生成や量子化結果が微妙に変わる可能性がある。既に量子化済みモデルがある場合、再量子化・再検証が必要になりうる（現時点で F-003 は量子化未実施のため影響は限定的）。
- **再インストール作業**: venv への wheel 入れ替えと依存（onnx / tflite）の再確認が必要。

### 4.3 本プロジェクト F-003 への影響

以下の F-003 手順書は旧スクリプト名（`scripts/mcu_quantize.py` / `scripts/mcu_deploy.py`）を前提に記述されている。MERA 2.6.0 へ更新する場合、これらを `mcu_compile.py` ベースへ改訂する必要がある:

- `doc/analysis_report/f003_03_training_quantization_report.md`（`mcu_quantize.py` を参照）
- `doc/analysis_report/f003_04_mera_conversion_guide.md`（`mcu_deploy.py` を参照）
- その他 `f003_01` / `f003_03b` / `f003_03d` / `f003_03f` も旧スクリプト名に言及あり

なお、Ethos-U55 / INT8 / アリーナ上限（432KB）等のハード制約や、ベースライン `yolo-fastest-192_face_v4` のサポートは更新後も維持されるため、**設計方針そのものへの影響はない**。

### 4.4 推奨アクション

**段階的更新を推奨（即時の強制更新は不要）。**

1. **現段階（F-003 の量子化・変換を未実施）**: ローカルPCは MERA 2.5.0 のまま据え置いてよい。本レポートで差分を記録済み。
2. **モデル変換を実行する直前**: 以下をまとめて実施する。
   - ローカルPC の MERA を **2.6.0+pkg.4513** へ更新（第5章の手順）。
   - リファレンスクローン `reference_projects/ruhmi-framework-mcu` を GitHub 最新（`Release-2026-04-27` 以降）へ更新し、`mcu_compile.py` を取り込む（別Issue推奨）。
   - F-003 手順書（上記）を `mcu_compile.py` ベースへ改訂。
3. **更新後**: ベースラインの顔検出モデルで変換が再現することを確認してから、転倒検出モデルの量子化・変換へ進む。

---

## 5. 更新する場合の手順概要

ローカルPC（`C:\work\ruhmi-framework-mcu`、Windows / venv 環境）を想定。**実行はユーザーが行う**。

```powershell
cd C:\work\ruhmi-framework-mcu
.venv\Scripts\Activate.ps1

# 既存 MERA をアンインストール（任意）
python -m pip uninstall -y mera

# 新しい wheel を取得
Invoke-WebRequest -Uri "https://github.com/renesas/ruhmi-framework-mcu/raw/main/install/mera-2.6.0+pkg.4513-cp310-cp310-win_amd64.whl" -OutFile "mera-2.6.0+pkg.4513-cp310-cp310-win_amd64.whl"

# インストール
python -m pip install .\mera-2.6.0+pkg.4513-cp310-cp310-win_amd64.whl
python -m pip install onnx==1.17.0 tflite==2.18.0

# バージョン確認（2.6.0+pkg.4513 を期待 / vela は 4.2.0）
python -c "import mera; print(mera.__version__)"
vela --version
```

- Linux の場合は wheel を `mera-2.6.0+pkg.4513-cp310-cp310-manylinux_2_27_x86_64.whl` に読み替える。
- 最新の正確な wheel ファイル名は必ず [install ディレクトリ](https://github.com/renesas/ruhmi-framework-mcu/tree/main/install) で確認すること（pkg番号はリリースで変わる）。
- スクリプトは `mcu_deploy.py` / `mcu_quantize.py` ではなく **`mcu_compile.py`** を使用する（GitHub `scripts/` を参照）。

---

## 6. 未確認・要追加調査の項目

- GitHub main ブランチの**最新コミットの個別ハッシュ・日付**（WebFetch では HTML から取得できず。タグ `Release-2026-04-27`=`757fb63` 以降に約8コミットがある点のみ確認）。
- `Release-2026-04-27` 以降の main 追加コミット（約8件）の具体的内容。
- `docs/operator_support.md` / `docs/runtime_api.md` の MERA 2.5.0→2.6.0 における行レベルの厳密な差分（破壊的API変更がないことは概観確認したが、全項目は未精査）。
- `mcu_compile.py` の正確な引数仕様（旧 `mcu_deploy.py` / `mcu_quantize.py` からの移行マッピング）。更新実施時にリファレンスクローン更新後の `scripts/README.md` で確認すること。

---

## 7. 参考

- リポジトリ: https://github.com/renesas/ruhmi-framework-mcu
- install ディレクトリ（wheel 一覧）: https://github.com/renesas/ruhmi-framework-mcu/tree/main/install
- リリース一覧: https://github.com/renesas/ruhmi-framework-mcu/releases
- ローカルのリファレンスクローン: `reference_projects/ruhmi-framework-mcu`（取得時点 commit `82ebd1c` / `Release-2026-02-02` / MERA 2.5.0）
- 関連レポート: `doc/analysis_report/ruhmi_framework_mcu_face_detection_analysis.md`、`doc/analysis_report/f003_03_training_quantization_report.md`、`doc/analysis_report/f003_04_mera_conversion_guide.md`
- 同種の比較・更新レポート例: `doc/analysis_report/quickstart_ek_ra8p1_ep_update_report.md`（Issue #139）
