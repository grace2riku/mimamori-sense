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

---

## ruhmi-framework-mcu

### 対象ソースコード
- **リポジトリ**: https://github.com/renesas/ruhmi-framework-mcu
- **コミット**: `82ebd1cfd663303f3de751936cd6279e054ed51a` (2026-02-08)
- **直近のリリースタグ**: `Release-2026-02-02` (`a458bb2`)
- **概要**: RUHMI Framework AI Compiler for MCU - EdgeCortix MERA を利用したAIモデル最適化・デプロイフレームワーク
