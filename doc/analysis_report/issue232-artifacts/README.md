# #232 実機試験に使用した診断版の保存

調査中断に伴い、配布済みZIPをDebug配下から無改変コピーして保存する。
一般のビルド生成物ではなく、実験条件を再現するためにこの2組だけを履歴へ残す。
SHA-256はsha256.json。ZIP内のMOTは同名なので、別々のフォルダへ展開して使用する。

- RTC_TRACE_r1.zip：最初のRTC／表示時刻計測。BOOTTRACE232 fnv=E10C55EF。
- LCD_GATE_r1.zip：表示開始前に保留する版。BOOTGATE232 v=23202、BOOTTRACE232 fnv=3615F648。

通常版や白画面の修正版ではない。調査は2026-09-24時点で中断中。
測定結果・再開地点は ../issue232-paused-20260924.md を参照。
再ビルド用スクリプトはscripts/issue232-trace、scripts/issue232-gate。
再ビルドにはe2 studioのビルド入力・指定LLVMツールチェーンと、スクリプトが参照する旧Debug成果物が必要。
Git cloneだけでそのままビルドできる構成ではないため、同一ファームの再試験には保存ZIPを使用する。
