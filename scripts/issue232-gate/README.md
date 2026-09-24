# #232 LCD開始保留診断 gate-r1

白画面の修正版ではありません。実機未確認です。
通常ソースをコピーし、表示タスクだけをLCD RESET直前で保留します。
カメラ・AI・音声等は動き続けるため、保留中を「LCD基板だけの動作」とは解釈しません。

## 書込み

配布場所: `e2studio_CPU0/Debug/issue232/gate-r1/artifacts/`。
このフォルダの `RTC_TRACE_CPU0.mot` と `RTC_TRACE_CPU1.mot` を1組で使用し、RFPでベリファイしてください。
ファイル名は旧trace-r1と同じですが、CPU0の内容が異なります。フォルダやZIPを混ぜないでください。
識別は `bootgate status` の `BOOTGATE232 v=23202` と `build-verification.json` のSHA-256。
CPU1はmain-wait-r2/trace-r1と同じ検証済みイメージです。

## 最初の取得試運転（最大2起動）

従来どおりJ10-PC、同一USBポート／ケーブル、115200 baud / 8N1。
RTC電池ON・時刻保持・スピーカー接続条件を維持。測定中のtime setは行いません。
AC給電の白率と比較しない取得試運転です。給電の差替えは別起動になるので行いません。

1. 電源OFFを30秒保ってON。プロンプトが出たら `bootgate status`。
2. `state=HOLD error=0`を確認。黒リードをTP9またはTP10（GND）に固定し、直流電圧で以下を測定。
   - C19上側（PG）。C19両端間では測らない。
   - TP4（AVDD）。
3. 投入からの概算経過秒数、各電圧、`bootgate status`全文を保存。保留中はバックライトOFFなので画面の正常／白判定はしない。
4. `bootgate continue`を1回実行。これは再開要求の受理であり、初期化完了ではない。
5. `bootgate status`が `DISPLAY_READY error=0`になったことを確認。これはGLCDC初期化の復帰で、LCD電源／表示の正常保証ではない。
6. continueから約10秒後、画面を正常／白／黒／その他で記録し、PGとTP4を再測定。測定時刻も記録。
7. `bootgate status`と `boottrace`の全文を保存。同じ手順で2回目を行い、正常と白が揃うまで無制限に繰り返さない。

30秒でプロンプトが出ない、再開後30秒でDISPLAY_READYへ進まない、ERRORが出た場合はその回で中止し、ログを保存してください。
HOLDはcontinueまで無期限で、測定中に自動再開しません。continueの重複は拒否されます。
表示／メモリ書込み等の干渉を防ぐため、コマンドはbootgate/boottrace/help/versionのみ有効です。
helpには通常のコマンド名も出ますが、この診断版では実行が制限されています。

| 回 | 段階 | 投入後秒数 | PG (V) | TP4 (V) | 画面／状態 |
|---|---|---|---|---|---|
| 1 | HOLD | | | | 画面判定対象外 |
| 1 | 再開後 | | | | |
| 2 | HOLD | | | | 画面判定対象外 |
| 2 | 再開後 | | | | |

## 読み方

- HOLDからPG High・TP4低下：その診断起動では、RESETパルス／GLCDC開始前から異常状態が存在。
- HOLDは正常で再開後に異常：再開後の区間と並行タスクが次の対象。GLCDC単独の因果確定ではない。
- 再発しない：保留による起動時刻変更の影響を含むので、改善や原因除外とはしない。

bootgateの時刻はOS稼働時間hi/lo、10ms刻みです。0は未記録の段階でも出るのでstateと併せて読みます。
boottraceは従来形式ですが長い保留中はDWT周回やAIによるリセットを含み得ます。
保留をまたぐDWT差を時間に換算せず、旧trace-r1の固定fingerprint解析に流用しません。

## 実装・再現

設計マスタは `doc/design/issue-232.md` のgate-r1節。
`build_gate.py`が既存traceビルダーへ診断オーバーレイを合成し、1595個のリンク対象を全再コンパイルします。
`boot_gate.inc`はコピーのusrcmd.cだけにincludeし、定義の重複や既存ソースのリンク除外を行いません。
通常ソース・ra/ra_gen・CPU1・旧配布物は変更しません。
完成出力は再利用しません。`--resume`は未完了ビルドの復帰専用です。
