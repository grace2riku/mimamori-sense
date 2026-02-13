# Mimamori-Sense プロジェクト

## 概要

Renesas RA8P1マイコンを使用した組み込みシステムプロジェクト。デュアルコア構成（Cortex-M85 + Cortex-M33）でFreeRTOSとLVGLを使用。

## 開発環境

- **IDE**: e2 studio 2025-12 (25.12.0)
- **FSP**: 6.2.0 / 6.3.0
- **ツールチェーン**: LLVM Embedded Toolchain for Arm 21.1.1
- **RTOS**: FreeRTOS
- **GUIライブラリ**: LVGL

## プロジェクト構成

```
mimamori-sense/
├── e2studio/           # ソリューションプロジェクト（マルチコア統合）
├── e2studio_CPU0/      # CPU0プロジェクト (Cortex-M85 @ 1GHz)
│   ├── configuration.xml
│   ├── src/            # ユーザーソースコード
│   ├── ra_gen/         # FSP生成コード（編集禁止）
│   └── ra/fsp/         # FSPライブラリ（編集禁止）
├── e2studio_CPU1/      # CPU1プロジェクト (Cortex-M33 @ 250MHz)
├── doc/                # ドキュメント
│   ├── analysis_report/  # 解析レポート
│   ├── analysis_request/ # 解析依頼
│   └── question/         # Q&A
└── reference_projects/ # 参照プロジェクト（.gitignoreで除外）
```

## コア構成

| コア | アーキテクチャ | クロック | 役割 |
|------|---------------|---------|------|
| CPU0 | Cortex-M85 | 1GHz | メインコア、ブート担当 |
| CPU1 | Cortex-M33 | 250MHz | サブコア |

## 重要な注意事項

### 自動生成コード
- `ra_gen/` と `ra/` ディレクトリ内のファイルは**編集禁止**
- FSP設定変更は `configuration.xml` で行い、コード生成を実行する

### マルチコア開発
- **ブートシーケンス**: CPU0が先にブートし、`R_BSP_SecondaryCoreStart()`でCPU1を起動
- **クロック初期化**: CPU0のみが実施（CPU1はスキップ）
- **GPIO制御**: 各コアが制御するGPIOを分離設計する（`_RA_CORE`マクロで分岐）
- **共有リソース**: IPCセマフォで排他制御

### Threadの追加手順
1. `configuration.xml` を開く
2. Stacksタブ → New Thread
3. プロパティを設定（Symbol, Name, Stack size, Priority）
4. Generate Project Content
5. `src/<symbol>_thread_entry.c` を手動作成

### ビルド・デバッグ
- Debug出力は `.gitignore` で除外されている
- JLinkを使用してデバッグ

## ドキュメント

- 解析レポートは `doc/analysis_report/` に配置
- Q&Aは `doc/question/` に配置
- 新規ドキュメントはマークダウン形式で作成

## 参考資料

- [RA8P1製品ページ](https://www.renesas.com/en/products/ra8p1)
- [lv_port_renesas_ek_ra8p1](https://github.com/lvgl/lv_port_renesas_ek_ra8p1)
