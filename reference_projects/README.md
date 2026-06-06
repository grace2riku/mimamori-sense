# リファレンスプロジェクト

本ディレクトリには、mimamori-senseの開発で参照するリファレンスプロジェクトを格納しています。

---

## lv_port_renesas_ek_ra8p1

### 対象ソースコード
- **リポジトリ**: https://github.com/lvgl/lv_port_renesas_ek_ra8p1
- **コミット**: `6ab5aa5a925025bd086127089faeb6f9765d801b` (2026-02-17)
- **コミットメッセージ**: "Merge pull request #2 from jeremy-baker/ospi_flash_support"
- **FSPバージョン**: 6.3.0
- **タグ**: なし（タグ未付与のリポジトリ）

### 動作確認環境
- Windows 11 PC
- e2 studio
  - Version: 2025-12 (25.12.0)
  - setup_fsp_v6_3_0_e2s_v2025-12.exeでインストール
  - ツールチェイン
    - LLVM for ARM, LLVM for Renesas Builder
    - LLVM Embeddded Toolchain for Arm, バージョン 21.1.1
- FSP 6.3.0 (configuration.xmlの設定)

### 備考
- オリジナルはLLVM Embeddded Toolchain for Arm バージョン 18.1.3でビルドされていた
- 2026-06-02 にコミット `522075e` (FSP 6.2.0) から `6ab5aa5` (FSP 6.3.0) へ更新（Issue #138）
  - 更新内容の詳細: `doc/analysis_report/lv_port_renesas_ek_ra8p1_update_report.md`
  - 上流の方針変更により、FSP/LVGL由来のファイル（`ra/fsp/src/r_drw/r_drw_memory.c`、tiny_ttf、`lv_draw_dave2d_image.c`）と LVGL Editor 生成物（`ui/`）は Git 管理対象外となった
  - そのためビルド前に e2 studio で **Generate Project Content** を実行し、FSP/LVGL の標準ファイルを再生成する必要がある

---

## quickstart_ek_ra8p1_ep

### 対象ソースコード
- **リポジトリ**: https://github.com/renesas/ra-fsp-examples
- **パス**: `example_projects/ek_ra8p1/_quickstart/quickstart_ek_ra8p1_ep`
- **タグ**: `v6.3.0.example.1` (`a7f7046a1f501de9bc71bf393a453851a2ca6d14`, 2025-12-27)
- **FSPバージョン**: 6.3.0

### 備考
- 2026-06-03 に GitHub 最新版と比較（Issue #139）。比較時点の最新は `v6.4.0.example.3` (`a1ec3727069fc6e269a91bd2888581b665d28edf`, FSP 6.4.0)。
  - 対象パス配下の差分は **FSP 6.3.0 → 6.4.0 のバージョンbumpのみ**で、ソースコード（`src/` の `.c`/`.h`）の変更はなし。
  - 唯一の機能的FSP設定変更（`board.clock.bclkout.div` `.2`→`.0`）は本体 `e2studio/solution.xml` で既に `.0` のため反映不要。
  - 移植価値のあるコード差分がないため、ローカルのリファレンスは `v6.3.0.example.1`（FSP 6.3.0）のまま据え置き、本体の FSP 6.4.0 移行も実施しない（ユーザー判断）。
  - 比較結果の詳細: `doc/analysis_report/quickstart_ek_ra8p1_ep_update_report.md`

---

## ruhmi-framework-mcu

### 対象ソースコード
- **リポジトリ**: https://github.com/renesas/ruhmi-framework-mcu
- **コミット**: `6f6159f853d50e3efa35cf770cf98554be8f4119` (2026-06-04)
- **直近のリリースタグ**: `Release-2026-04-27` (`757fb63`)
- **MERA バージョン**: `2.6.0+pkg.4513` (vela 4.2.0)
- **概要**: RUHMI Framework AI Compiler for MCU - EdgeCortix MERA を利用したAIモデル最適化・デプロイフレームワーク

### 備考
- 2026-06-06 にローカル PC（`C:\work\ruhmi-framework-mcu`）を `82ebd1c` (MERA 2.5.0+pkg.3577) から `6f6159f8` (MERA 2.6.0+pkg.4513) へ更新（Issue #144）。
  - 破壊的変更: 変換スクリプトが `mcu_deploy.py` / `mcu_quantize.py` から統合スクリプト `mcu_compile.py` へ移行。
  - これに伴い `scripts/deploy_fall_detection.ps1` を `mcu_compile.py`（`--npu --ref-data --suffix _net1`）へ対応させ、転倒/人物検出モデルを再変換。
  - 再変換で MERA 生成コードの内部ノードIDサフィックスが変化したため `fall_detection/wrapper.h` の出力ポインタ関数名を追従更新。
  - 更新手順・チェック項目の詳細: `doc/analysis_report/ruhmi_framework_update_procedure.md`、変換手順: `doc/analysis_report/f003_04_mera_conversion_guide.md`
