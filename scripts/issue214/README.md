# Issue #214 時刻設定画面: 検証・実機確認

設計と実装根拠は `doc/design/issue-214.md`。
2026-09-25、ユーザー依頼により保存済み実装をローカルmain (`f2be297`) に統合。
参照チャットでは旧版のPC給電時の動作と、AC給電へ変更後の白画面が報告されている。
今回の統合版は実機未確認。旧版の結果を今回の合格とは扱わない。

## 書き込み用ファイル

今回の成果物は `e2studio_CPU0/Debug/issue214/main-integration-r1/`。
起動画面と時刻設定画面の両方を含む。検証数値は下記「main統合版の検証結果」を参照。

| ファイル | 内容 |
|---|---|
| `mimamori_sense_CPU0.mot` | 今回のCPU0。プログラム用Flashだけを格納。RAM/SDRAM・OFS領域は含めない |
| `mimamori_sense_CPU1.mot` | #230で使用したmain-wait-r2のCPU1をそのまま同梱。CPU1の変更なし |
| `mimamori_sense_CPU0.elf` / `.map` | デバッグ情報とリンク配置 |
| `manifest.json` / `verification.json` | 全再コンパイル、ソースハッシュ、ELF/MOT検証結果 |
| `size.txt`、各`.o.log`、`link.log` | メモリ使用量とビルドログ |

旧版build-r3のCPU0 MOT SHA-256: `6c6391a79ed9d75b50bf579e87a5c253d404912b8b4aaeff8a4b1ea660eececa`

CPU1 MOT SHA-256: `8cb5d5e9040d0a9bb0ad72d233d21115b5e68cba3c0524e03f3e0fb4d31d2c53`

旧版build-r1/r2/r3は今回の統合成果物ではない。今回使用するのはmain-integration-r1。

## main統合版の検証結果（2026-09-25）

- `cache_test.c` / `test_ui.c`: ARM実行テストPASS。
- CPU0全1,598オブジェクトを再コンパイル・リンク成功。警告295件（全体）、リンク警告なし。
- text=976,362、data=282、bss=8,006,561 B（SDRAMを含む）。Flash使用976,896 / 1,015,808 B、残り38,912 B。
- MOTとELFのFlash内容一致、S-recordチェックサム、CPU0/CPU1領域非重複を検証済み。
- CPU0 MOT SHA-256: `a7fff6598d62c446ade5223afb518910c8f8791429bd0f2349f3a3a2a4d1e16e`。
- `scripts/issue214/build.py` と `scripts/startup-screen/build.py` の両方に、両機能の新規ソースを列挙。今回の全体ビルドに使用したのは前者。両スクリプトのPython構文検証は合格。
- 実機、描画、タッチ、応答時間、コールドブートは未検証。

## PC上の再検証

Python 3とプロジェクトのARM LLVMを使用する。テストはUnicorn 2.1.4が必要。
依存を分離する場合は `python -m pip install --target <任意の依存フォルダ> unicorn==2.1.4` とし、
環境変数 `ISSUE214_TEST_DEPS` にそのフォルダを設定する。未指定時はOSの一時フォルダ内 `issue214-test-deps` を使用。

```powershell
python scripts/issue214/tests/run_arm_tests.py cache_test.c test_ui.c
python scripts/issue214/build.py --run main-integration-r2
python scripts/issue214/verify.py --run main-integration-r2
```

既存成果物を上書きしないよう、新しいrun名を指定する。ビルドはe2 studioが生成した `Debug/*.in` を参照し、生成物やFSPソースは編集しない。
新規画面と起動画面関連ソースのコンパイルフラグは既存ui_datetimeのものを使用する。e2 studioから通常ビルドする場合はプロジェクトをRefreshし、新しい `src/ui/ui_time_setting_screen.c` がビルド対象に入ったことを確認する（`.cproject`のsrcソースエントリ配下）。

テストは実際のC実装をARM向けにコンパイルして実行するが、RTC・RTOS・LVGL APIは模擬する。
日付境界、Cancel、二重OK、未完了のままBack→再入場、遅延完了、tick周回、各エラー、設定後の即時表示、生成失敗時の解放を確認。
実機の描画・タッチ入力・処理時間・ハードウェア故障はこのテストの対象外。

## EK-RA8P1の確認手順

1. 既存の書き込み手順で上記CPU0/CPU1のMOTを使用する。
2. 表示確認は5V ACアダプタで給電する。LCD対策の前提は `doc/design/white-screen-boot-regression.md` 冒頭の2026-09-23受入結果を参照。過去の切り分けは繰り返さない。
3. メイン画面の歯車を押し、現在の年月日時分が初期値になることを確認する。未設定時は警告と2026-01-01 00:00を表示する仕様。
4. 2028-01-31から2月へ変更して29日に補正されること、2027年に変更すると28日になること、4月は30日までであることを確認する。
5. 編集してCancelを押し、時計が通常の経過以外に変更されないことを確認する。
6. 再度開いて日時を変更しOKを押す。メイン画面へ戻った直後の表示とNT-Shellの `time` が一致することを確認する（秒はOK時に0に設定）。OK押下から復帰までの時間も記録する。
7. 再度開き、反映した時刻が初期値になることを確認する。設定画面、成功後のメイン画面の写真と `time` のログを保存する。
8. 表示回帰はAC給電・同一セッションの対照・コールドブート10回で確認し、正常／白／カラーバーを区別して記録する。詳細条件は `doc/hardware-setup-guide/power-supply.md`。

処理結果が3秒で確定しない場合、画面が動作していればBackを表示する。Backは取消ではなく、要求が後で適用される可能性がある。
高優先度RTCタスクがFSPの無期限busy waitに入ると、この表示自体が動く保証はない。故障注入やサブクロック停止を実機の通常確認として要求しない。

プロジェクト手順に従い、実機の確認結果を受け取ってからPR作成へ進む。
