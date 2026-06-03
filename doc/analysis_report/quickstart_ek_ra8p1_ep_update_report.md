# リファレンスプロジェクト quickstart_ek_ra8p1_ep 比較レポート

本レポートはIssue #139の成果物である。ローカルのリファレンスプロジェクト `reference_projects/quickstart_ek_ra8p1_ep` とGitHubリポジトリ（[renesas/ra-fsp-examples](https://github.com/renesas/ra-fsp-examples)）最新版の比較結果、および対応方針をまとめる。

- **実施日**: 2026-06-03
- **Issue**: #139
- **対象パス**: `example_projects/ek_ra8p1/_quickstart/quickstart_ek_ra8p1_ep`

---

## 0. 結論サマリ

- ローカル取得時点 `v6.3.0.example.1`（FSP 6.3.0）から GitHub 最新 `v6.4.0.example.3`（FSP 6.4.0）までの差分を、対象パス配下で精査した。
- **対象プロジェクトのソースコード（`src/` 配下の `.c`/`.h`）の変更は一切なし**。差分の本質は **FSP 6.3.0 → 6.4.0 のバージョンbumpのみ**。
- 唯一の機能的なFSP設定変更（`board.clock.bclkout.div` `.2`→`.0`）は、**mimamori-sense本体 `e2studio/solution.xml` で既に `.0`** となっており、本体は既に新しい値と一致している。
- 上記を踏まえ、ユーザー判断により **「記録のみ・移行しない」方針** を採用した。
  - ローカルのリファレンスプロジェクト本体ファイルは `v6.3.0.example.1`（FSP 6.3.0）のまま据え置く（Phase B の更新は実施しない）。
  - 本体ソフトウェア（`e2studio_CPU0` 他）の FSP 6.4.0 への移行は実施しない（Phase C の反映は不要）。
  - 比較した事実と判断根拠を本レポートおよび `reference_projects/README.md` に記録する。

---

## 1. 比較結果（Phase A）

### 1.1 バージョン情報

| 項目 | ローカル取得時点 | GitHub最新 |
|---|---|---|
| タグ | `v6.3.0.example.1` | `v6.4.0.example.3` |
| コミット | `a7f7046a1f501de9bc71bf393a453851a2ca6d14` | `a1ec3727069fc6e269a91bd2888581b665d28edf` |
| FSPバージョン | 6.3.0 | **6.4.0** |
| e2 studio | 2025-12 | 2025-12 |

> 補足: `v6.3.0.example.1` と最新の間には `v6.3.1.example.1`, `v6.4.0.example.1`, `v6.4.0.example.2`, `v6.4.0.example.3` のタグが存在する。ra-fsp-examples はリリースごとにスナップショットをコミットするモノレポ構成であり、対象パス配下に変更を加えたコミットは最新タグの `a1ec372` のみであった。

### 1.2 比較手法

ra-fsp-examples はモノレポであり、GitHub Compare API はファイル数上限（300件）で対象パスの差分を取得できないため、blobless partial clone + sparse-checkout で対象パスのみを取得し、タグ間で `git diff` を実施した。

```
git clone --filter=blob:none --no-checkout --sparse https://github.com/renesas/ra-fsp-examples
git sparse-checkout set "example_projects/ek_ra8p1/_quickstart/quickstart_ek_ra8p1_ep"
git fetch --depth=1 origin tag v6.3.0.example.1 tag v6.4.0.example.3
git diff v6.3.0.example.1 v6.4.0.example.3 -- "<対象パス>"
```

### 1.3 変更ファイル一覧（対象パス配下、7ファイル）

| ファイル | 変更行数 | 実質的意味 |
|---|---|---|
| `readme.txt` | 2 | FSPバージョン表記 6.3.0 → 6.4.0 のみ |
| `e2studio/configuration.xml` | 100 | 全コンポーネントのFSPバージョン文字列 6.3.0→6.4.0 + クロック設定1箇所 |
| `e2studio/script/memory_regions.ld` | 146 | **先頭インデント除去のみ（アドレス値は完全同一）** |
| `e2studio/quickstart_ek_ra8p1_ep.hex` | 49,144 | 再ビルド成果物（バイナリ）・対象外 |
| `e2studio/.settings/...componentfiles.prefs` | 96 | パック参照バージョン 6.3.0→6.4.0 |
| `e2studio/.settings/e2studio_project.prefs` | 2 | タイムスタンプのみ |
| `e2studio/.settings/language.settings.xml` | 6 | タイムスタンプ・env-hash再生成のみ |

**ソースコード（`src/` の `.c`/`.h`）の変更はゼロ。**

### 1.4 configuration.xml の機能的変更（バージョン文字列を除く）

バージョン文字列（`6.3.0`→`6.4.0`、パック名）以外の実差分は次の1箇所のみ:

```diff
-    <node id="board.clock.bclkout.div" option="board.clock.bclkout.div.2" />
+    <node id="board.clock.bclkout.div" option="board.clock.bclkout.div.0" />
```

- BCLKOUT分周設定の変更。
- **本体 `e2studio/solution.xml` は既に `board.clock.bclkout.div.0`** であり、本体側は既にこの新しい値と一致している（差分なし）。

---

## 2. リファレンスプロジェクトの更新（Phase B）

**実施しない（据え置き）。**

理由:
- 対象パスにソースコードの変更がなく、リファレンスとしての移植価値のある差分が存在しない（差分はFSP 6.4.0へのバージョンbumpとビルド成果物のみ）。
- ローカルを FSP 6.4.0 のリファレンスへ更新しても、本体（FSP 6.3.0）および他リファレンス（`lv_port_renesas_ek_ra8p1` は FSP 6.3.0）との間でFSPバージョンが不整合になるだけで利点がない。

`reference_projects/README.md` には、本比較を実施した事実（比較日・比較対象タグ・差分の結論）を追記する。取得時点情報（タグ/コミット/FSPバージョン）は `v6.3.0.example.1`（FSP 6.3.0）のまま維持する。

---

## 3. 本体ソフトウェアへの反映（Phase C）

**反映なし。**

| 観点 | 状況 |
|---|---|
| ソースコード差分 | なし → 反映対象なし |
| `bclkout.div` 設定変更 | 本体 `solution.xml` で既に `.0`。差分なし → 反映不要 |
| FSPバージョン 6.4.0 への移行 | ユーザー判断により**実施しない**（本体は FSP 6.3.0 を維持） |

本体（`e2studio_CPU0` / `e2studio_CPU1` / `e2studio/solution.xml`）はいずれも FSP 6.3.0 で、現状維持とする。FSP 6.4.0 への移行は、FSP 6.4.0 の e2 studio インストール・全コアの `Generate Project Content`・カメラ画像表示/LCD表示等の全機能リグレッション確認を伴う大規模かつ環境依存の作業であり、本Issueの範囲では実施しない。将来、本体を FSP 6.4.0 へ移行する判断をした際に、本レポートを参照すること。

---

## 4. 受け入れ条件の充足状況

| 受け入れ条件 | 状況 |
|---|---|
| ローカルのリファレンスが GitHub 最新版と一致 | 差分はFSPバージョンbumpのみで移植価値がないため、ユーザー判断で据え置き（不一致を許容） |
| `reference_projects/README.md` が最新の取得時点情報に更新（差分があった場合） | 比較記録を追記。取得時点情報は 6.3.0.example.1 を維持 |
| 変更点が `e2studio_CPU0` に反映されビルド・実機確認完了（差分があった場合） | 反映すべきコード差分なし・本体FSP移行は非対象のため該当なし |
| 比較・更新・反映レポートが `doc/analysis_report/` に存在 | 本レポート |

---

## 5. 参考

- リポジトリ: https://github.com/renesas/ra-fsp-examples
- 関連レポート: `doc/analysis_report/quickstart_ek_ra8p1_ep_analysis.md`
- 関連レポート（同種の比較・更新例）: `doc/analysis_report/lv_port_renesas_ek_ra8p1_update_report.md`（Issue #138）
