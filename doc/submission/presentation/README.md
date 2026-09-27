# 応募プログラム紹介スライド

- [PDF（閲覧・提出用）](MimamoriSense-TRON2026.pdf)
- [PowerPoint（文字と図を編集可能）](MimamoriSense-TRON2026.pptx)
- [本文Markdown](introduction.md)

16:9、全10枚。既存の実機写真を使用し、作品の目的、処理フロー、状態遷移、画面、日時設定、μT-Kernelのタスク構成、実機確認手順、制約を説明しています。


## 提出

[公式応募要項の「2. 応募プログラムの紹介資料」](https://www.tron.org/ja/programming_contest-2026/apply/)は、スライドをインターネット上で閲覧またはダウンロードできる状態にして、そのURLを知らせることを求めています（2026年9月26日確認）。

PDFまたはPPTXを、審査員がアクセスできるGitHub Releasesや共有ストレージ等に配置してください。本作業では公開・アップロードを行っていません。提出時は対応するファームウェア版を固定し、応募フォーム側の応募者名・作品名との整合を確認してください。

## 編集と再生成

通常の修正はPowerPointで行えます。PDFはレイアウトを保持した画像形式で、編集元はPPTXです。

プログラムで再生成する場合はCodexのバンドル済みNode.jsとPythonを使います。

1. `build.mjs` の本文・配置を編集する。
2. `build.mjs` をNode.jsで実行してPPTXと本文Markdownを生成する。
3. `render-final.mjs` をNode.jsで実行して最終PPTXを画像化する。
4. `build_pdf.py` をPythonで実行して閲覧用PDFを生成する。

`ARTIFACT_RUNTIME` はバンドル依存先、`PRESENTATION_SKILL` はプレゼンテーションスキル、`PRESENTATION_BUILD` は中間生成先を変更できます。再生成時は空の中間生成先を指定してください。検証済みのPPTXを成果物へコピーします。

`introduction.md` はスライド本文の書き出しです。単独で編集してもPPTXには反映されません。
