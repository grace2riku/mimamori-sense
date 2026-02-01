
# lv_port_renesas_ek_ra8p1

## 動作確認環境
- Windows 11 PCで確認
- e2 studio
  - Version: 2025-12 (25.12.0)
  - setup_fsp_v6_3_0_e2s_v2025-12.exeでインストール
  - ツールチェイン
    - LLVM for ARM, LLVM for Renesas Builder
    - LLVM Embeddded Toolchain for Arm, バージョン 21.1.1
- FSP
  - 6.2.0 (configuration.xmlの設定)

## 対象ソースコード
- lv_port_renesas_ek_ra8p1
  - https://github.com/lvgl/lv_port_renesas_ek_ra8p1
  - コミット 522075e
- FSP 6.2.0 に設定されていた(configuration.xmlの設定)
- LLVM Embeddded Toolchain for Arm, バージョン 18.1.3でビルドされていた


# quickstart_ek_ra8p1_ep