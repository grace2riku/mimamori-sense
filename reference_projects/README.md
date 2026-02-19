# リファレンスプロジェクト

本ディレクトリには、mimamori-senseの開発で参照するリファレンスプロジェクトを格納しています。

---

## lv_port_renesas_ek_ra8p1

### 対象ソースコード
- **リポジトリ**: https://github.com/lvgl/lv_port_renesas_ek_ra8p1
- **コミット**: `522075e5303228b6576cc399a82fe5adad35693e` (2025-12-01)
- **コミットメッセージ**: "upgrade to FSP v6.2.0"
- **FSPバージョン**: 6.2.0
- **タグ**: なし（タグ未付与のリポジトリ）

### 動作確認環境
- Windows 11 PC
- e2 studio
  - Version: 2025-12 (25.12.0)
  - setup_fsp_v6_3_0_e2s_v2025-12.exeでインストール
  - ツールチェイン
    - LLVM for ARM, LLVM for Renesas Builder
    - LLVM Embeddded Toolchain for Arm, バージョン 21.1.1
- FSP 6.2.0 (configuration.xmlの設定)

### 備考
- オリジナルはLLVM Embeddded Toolchain for Arm バージョン 18.1.3でビルドされていた

---

## quickstart_ek_ra8p1_ep

### 対象ソースコード
- **リポジトリ**: https://github.com/renesas/ra-fsp-examples
- **パス**: `example_projects/ek_ra8p1/_quickstart/quickstart_ek_ra8p1_ep`
- **タグ**: `v6.3.0.example.1` (`a7f7046a1f501de9bc71bf393a453851a2ca6d14`, 2025-12-27)
- **FSPバージョン**: 6.3.0
