# RTC保持診断版 diag-r1

2026-09-25。main `131811a` に承認済み診断を追加。実機未検証。保持不具合を修正する版ではない。
設計・今回までの実測・電圧監視0の設定に関する追加確認点は `doc/design/issue-234.md`。

## 書き込みファイル

出力ディレクトリ: `e2studio_CPU0/Debug/issue234/diag-r1/`

- `mimamori_sense_CPU0.mot`: 今回の診断版。SHA-256 `285cdc62417f235e6d32cad2fff02bcbfec9694ccdcf447bce3cfac923ba683b`
- `mimamori_sense_CPU1.mot`: 変更なし、既存main-wait-r2と同一。SHA-256 `8cb5d5e9040d0a9bb0ad72d233d21115b5e68cba3c0524e03f3e0fb4d31d2c53`
- `.elf` / `.map` / `disassembly.txt`: リンク・命令確認用。
- `manifest.json` / `verification.json` / `boot-storage-verification.json`: ソース一致、MOT、RAM領域の検証記録。

MOTはプログラムFlashのみ。オプション領域・FSP設定は変更しない。
起動画面、時刻設定GUI、既存200ms待ちを保持する。診断追加による配置・時間の変化はあり、白率比較には使用しない。

## 実機で行うこと（1組のみ）

1. 従来の書き込み方法でこのCPU0 MOTを書き込む。CPU1を指定する場合は同梱のものを使う。
2. 前回のRTC試験と同じPC給電・同じ接続で起動する。ほかのUSBやデバッガによる給電がある場合は接続状況も記録する。
3. GUIで現在日時を設定する。書き込み操作前の保持状態は試験の基準にしない。
4. `time status`を取得する。先頭に **RTC boot history v1** と5段階の履歴があることを確認する。
5. 電源をOFFにし60秒測ってから同じ接続でONにする。電源OFF直前・ON時の実時計も記録する。
6. **日時を設定し直さずに** `time status`を取得する。前後の出力全文とOFF時間を共有する。
7. 履歴が出ない、NOT CAPTUREDがある、シェルが使えない場合は反復せずその結果を共有する。白画面でもコンソールが使える場合は同じログを取得し、表示状態を併記する。

履歴は今回の起動時の記録で、GUI/コマンドによる時刻設定後も変わらない。
RESETはアプリの最初のフックであり、電源断中やBoot ROM内部の状態を直接観測したものではない。
早期の暦カウンタ読出しは仕様の待ち条件を満たさないため省略した。START/HR24等の変化する区間を調べる。
制御ビットが同じでも日時保持を証明できない。現在日時・OFF時間との照合も必要。

## 検証結果

- 記録モジュールをARM向けにコンパイルしUnicornで実行: PASS。再起動時の古い有効印消去、欠落、範囲外、NULL、各スロットの独立性、二重採取での上書き防止、模擬レジスタ非変更を確認。
- CPU0全1,599オブジェクト再コンパイル・リンク成功。全体警告295件。新規rtc_boot_diag.cのコンパイル警告0。
- text=977,138、data=282、bss=8,006,689 B。Flash使用977,920 / 1,015,808 B。
- MOTとELFのFlash内容一致、チェックサム、CPU0/CPU1非重複を検証済み。
- 記録領域 `0x220085ac`、120 B。実ELFのzero_list 11件、copy_list 25件との非重複を検証。
- 最終ソース（src/ra_cfg/ra_genのヘッダを含む）とビルド時ハッシュの一致を検証。
- 実機の保持・起動・描画、診断追加の所要時間は未測定。

## 再現手順

プロジェクトのLLVMとPython 3を使用。テスト用UnicornはIssue #214と同じ環境。

```powershell
python scripts/issue234/tests/run_arm_tests.py diag_test.c
python scripts/issue234/build.py --run diag-r2
python scripts/issue234/verify.py --run diag-r2
python scripts/issue234/verify_boot.py --run diag-r2
```

e2 studioからビルドする場合はRefreshし、`src/rtc_boot_diag.c`の追加を反映する。
本診断作業では専用のissue234/build.pyを使う。過去のissue214/startup-screen専用スクリプトは新規診断ソースを列挙していない。

## diag-r1の命令監査

下記アドレスはこのELF専用。別ビルドには転用しない。

| 記録点 | 命令・確認内容 |
|---|---|
| RESET | SystemInit `0x02041aac`からR_BSP_WarmStartへevent=0。`0x0200f61c`以降で5つの有効印を消去、5レジスタの読出し、最後に有効印。RTC書込み・追加API呼出しなし |
| POST_CLOCK | SystemInitにインライン展開。`0x02042156–0x02042190`で記録し、`0x02042204`のSystemRuntimeInit(0)より前 |
| POST_C | `0x0204237e`からevent=2。フックの`0x0200f316–0x0200f34e`で記録後、既存200ms要求へ |
| BEFORE_OPEN | ntshell_task `0x02012afa–0x02012b3a`。既存mutex取得成功の後、Openのインライン処理の前 |
| BEFORE_DECISION | `0x02012b86–0x02012ba6`。Open後、`0x02012baa`以降のSTART/HR24判定より前 |

初期化済み判定・RTC書込み経路そのものは変更していない。早期記録の依存はFlashコード、内部RAM、制御レジスタ、既存スタックのみ。
