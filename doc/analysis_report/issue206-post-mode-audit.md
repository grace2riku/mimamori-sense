# Issue #206: mode-r1比較後の安全条件監査

対象は受領済みログ、gaptrace-r1/mode-r1のビルドmanifest、現行ソース、mode-r1最終ELF。製品コードは変更しない。根本原因は未確定。

## 今回確定できた範囲

1. 両manifestのsource_sha256を全キー比較したところ、gaptrace側に記録された入力で差があるのは `src/port/audio_port.c` のみ。AI/カメラ側のソース変更を今回の版間差の説明に用いる根拠はない。ただしこれはコンパイル入力の記録であり、実行時負荷・全ビルド条件の同一性を証明しない。
2. 異常2試行の開始基準は39.60/41.24秒、異常区間の入口は起動68.04/71.61秒、再生開始から28.44/30.37秒。mode-r1の正常試行は開始41.10～42.42秒。起動時刻だけで成否を分けられず、異常2例だけから「30秒タイマー」を導けない。根拠: `issue206-gaptrace-r1-recording-session1.txt:72,149`、`issue206-gaptrace-r1-session1.txt:78,155`、`issue206-mode-r1-summary.json`（いずれも本フォルダ）。
3. ログにはカメラ初期化結果、推論回数、表示更新回数がない。AIの実行は単なる起動後経過時間では決まらない。カメラ初期化完了またはエラーを待ち、画像イベント待ちの成功後に推論へ進む（`e2studio_CPU0/src/ai_inference_thread_entry.c:445,589,594,635`）。画像側はframeがNULLならreturnし、AI初期化確認後に前処理と通知を行う（`e2studio_CPU0/src/camera_display.c:365,366,401,413,422`）。したがって正常/異常時の負荷一致は既存ログから復元できない。
4. `jlink_configured()` はデバッガ接続の判定ではなくUART open成功で立つフラグ（`e2studio_CPU0/src/jlink_console.c:83,135,139`）。ユーザーのスタンドアロン確認と矛盾しない。デバッガ停止を再度原因候補に戻さない。

## あるべき安全条件と現行との差

以下のFSP/ソースパスは `e2studio_CPU0/` 相対。

| 安全条件 | 現行の証拠 | 判定と不足証拠 |
|---|---|---|
| 転送所有中のPCMを再投入・再生成しない | `src/port/audio_port.c:993` の共通ガードがDTCE/ACT/CRB/モードとソフト残量を確認。通常/補完とも `audio_refill:1033` から通る | CPU側の検査は存在。検査後に保留ハードウェア動作がないという保証は別問題 |
| 次の162ブロックを用意してからDTCを有効にする | `ra/fsp/src/r_ssi/r_ssi.c:825,837,839` の648 bytes→324 samples→162 blocks。ResetのSAR/CRB更新後にDTCEを有効化（`r_dtc.c:243-297`） | 値の計算とCPU命令順は整合。CPU読戻しとDTC内部読取りの同一性は未証明 |
| 旧転送の書戻しが設定更新へ割り込まない | ResetはDTCE無効後、DTCSTSが自ベクタのACT値である間だけ待つ（`r_dtc.c:673-678`） | 待ちAPIが全pending要求や未来の旧書戻しを排出することをコードだけで証明しない。既存モデルはここを仮定している |
| CPUがDTCEのハードウェア更新を巻き戻さない | IR clearはIELSRのbitfield操作（`bsp_irq.h:74-81`）。最終ELFは全word load→IR bit除去→全word store | **未保証の箇所**。元ISRより後の補充ガードではこのRMWを保護できない（`audio_port.c:737`、`r_ssi.c:1179`） |
| 調査追加が競合の時間関係を変えない | mode-r1はnowindowでも元ISR前のENTRY採取を残す（`audio_port.c:729`） | PRE/POST比較だけでは、元ISR内RMWまでの時間変化を評価できない。別モードを直ちに増やす根拠にはしない |

最終ELFの根拠は `e2studio_CPU0/Debug/issue206/mode-r1/reset-audit-disassembly.txt`。IR clearは0x020423a4 load→0x020423a6 IR除去→0x020423aa store。Resetは0x02044d58 DTCE0→0x02044d70 ACT読取り/待ち→0x02044d78 RRS0→0x02044d80 SAR→0x02044d98 CRB halfword store→0x02044dac RRS1→0x02044dbc DTCE1。前版のアドレスを現行の根拠として流用していない。

## 原因候補の優先度

- **優先して詰める点: 旧転送終了と次設定の境界。** 観測は65536フレーム相当、SAR差0x40000、CRB0、約4.026531秒で整合している。しかし「RMWがDTCEを戻しただけ」なら正常162フレーム分を加えたSAR差0x40288となり、観測との差648 bytesが残る。RMWを見つけただけで根本原因とはしない。SAR再設定との順序や旧書戻し等、追加の具体的経路を示す必要がある。
- **CPU負荷だけで4秒停止した説は主説明にしない。** SARの進行と録音の異常音を説明する必要がある。負荷は競合タイミングを変える条件としては残る。
- **他のDTC利用者の通常API操作は現行検索では見つからない。** CPU0 src/ra_genのDTC instanceは `g_transfer_i2s_tx`（`ra_gen/hal_data.c:182-222`）、CPU1 src/ra_genには同instance/R_DTC参照なし。未検索のバイナリや任意メモリ破壊まで不存在と主張しない。
- **AIの領域を読んだこととAIが記述子を壊したことを分離する。** mode-r1では記述子0x22000150/16 bytes、PCM0x2200a7b8/1296 bytes、AI arena0x2200bc10/0x93000 bytes。領域は別。旧ログの逸脱sourceがarena内に入る事実だけでは書き手を特定できない（最終ELF `mode-review-symbols.txt`）。

## 次の作業の境界

既存ログから初期状態/負荷の比較をこれ以上進めることはできない。同じ4試行の追加は依頼しない。次に必要なのは、保留要求・書戻し・IR clearの関係をDTC/ICUの仕様と照合し、SAR差と所要時間の両方に整合する順序を構成または否定すること。これを満たさない対策（理由のないDSB追加、FSPの一括置換、AI停止を原因確定と扱うこと）は採用しない。

現時点で新たな恒久修正案の承認を求めるだけの証拠はない。計測機能は比較試行で動作したが、Issue全体の受け入れは未完了。
