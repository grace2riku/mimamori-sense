# #232 同一起動内のRTC・表示タイミング計測設計

**2026-09-24：ユーザー指示で調査を一時中断。TRONプログラミングコンテストの締切に向け、他機能の実装を優先する。再開の明示指示まで追加診断・ビルド・測定を進めない。**
結果一覧と再開地点は `doc/analysis_report/issue232-paused-20260924.md`。現在のブランチはmain。


## 追加診断 gate-r1（2026-09-23）

ユーザーの「はい。どんどん進めてください」を、直前に提示した「LCD RESET前で表示のみ保留→PG/TP4測定→コンソールで一度再開→再測定」という設計の実施承認として扱う。以下はその具体化。別段階の保留追加、EN操作、自動復旧は対象外。

- 調査根拠: `doc/analysis_report/issue232-trace-r1-pc/u2-startup-timing-audit.md`。
- 保留位置: 診断コピーのlvgl_taskからglcdc_port_initを呼ぶ直前（通常 `src/lvgl_thread_entry.c:168`）。lv_init/Dave2D初期化後、RESETパルス・GLCDC Open/Start前。既存のDISPLAY_BEGIN記録後に挿入する。
- 呼出し元はlvgl_taskのみ。RESET書込みはglcdc_lcd_resetの3点のみ（`src/port/glcdc_port.c:316,319,322`）。タッチ初期化は同一タスクの後段（`lvgl_thread_entry.c:189`）なので保留中には進まない。CPU1ユーザーコードに該当RESET書込みなし。
- コンソールは独立したntshell_task（`usermain.c:467,473`）で、RTC初期化後にコマンドループへ入る（`ntshell_thread_entry.c:213,234`）。RTC初期化の無期限レジスタ待ちが起きれば操作不能のままになるので、30秒でプロンプトが出なければ中止する。救済経路は追加しない。
- カメラ・AI・音声等の他タスクは継続。これらを一緒に止めて別条件を混ぜない。タッチがカメラのIIC解放を待つ順序は変えない（`camera_thread_entry.c:601`以降の失敗経路も解放フラグを立てる）。
- 新規イベントフラグ1個をLVGL所有者が作成、状態とIDを短いtk_dis_dsp区間でpublish。ntshellは同じ排他でsnapshotし、HOLDのみREQUESTEDへ変更、区間外でtk_set_flg。LVGLはtk_wai_flg(TMO_FEVR)から戻り、RUNNINGにして既存表示初期化を実行する。先行通知もフラグに残るため取りこぼさない。
- 状態はINIT→HOLD→REQUESTED→RUNNING→DISPLAY_READY（GLCDC初期化成功）／ERROR。DISPLAY_READYは実表示成功を意味しない。再開要求は一度のみ。ISRは新規状態を書かない。状態・ID・エラー・OS時刻はpublish/sampleとも同一区間。UARTとデバイスAPIと待ちは排他外。
- 下位APIの裏取り: `mtk3_bsp2/mtkernel/kernel/tkernel/eventflag.c:56,133,231`。createは空きオブジェクト取得、setは待ち行列走査でデバイス待ちなし、waitのみ指定どおり無期限。作成／wait失敗はERRORを保存してLVGLタスクを終了し、RESET/表示/タッチへ進まない。set失敗はHOLDへ戻してエラー保存、再要求可能。既存表示初期化失敗もERRORを保存しLVGL後続を止める。
- `bootgate status`は状態・エラー・各段階のOS稼働時刻（hi/lo、10ms粒度）を出す。`bootgate continue`は要求受理だけを返す。表示処理をシェル文脈から呼ばない。
- 診断専用版ではコマンドをbootgate/boottrace/help/versionに限定し、display/mw/reset等の経路から測定状態を変えない。通常版のコマンドは変更しない。ゲート状態を自動再初期化しない。
- 元のtrace-r1計測を再利用。手動保留中はDWT周回・AIのDWTリセットが起き得るため、保留時間はOS時刻だけで読む。旧解析器の固定fingerprint判定へ新ログを流用しない。
- 実装は `scripts/issue232-gate/` のビルド時オーバーレイ。通常ソース・ra/ra_gen・既存trace-r1配布物は無変更。新規公開シンボルはboot_gate_wait、boot_gate_complete、usrcmd_bootgate、boot_gate_command_allowed。関数定義はコピーのusrcmd.cにincludeする診断断片へ集約し、既存リンク対象に乗せる。
- 新しい出力は `e2studio_CPU0/Debug/issue232/gate-r1/`。CPU0全リンク対象を再コンパイル、CPU1は検証済み既存ペア。MOT/ELF一致・重複なし・入力ハッシュ・RAM配置・保留と再開経路を検証。旧配布物と区別できるパッケージ名とbootgate識別を付ける。

### gate-r1の測定手順・判断

最初は従来どおりJ10-PC、同じケーブル、RTC電池ON・設定保持・スピーカー条件固定。最大2起動の取得試運転であり白率評価ではない。

1. 新ペアをRFP書込み・ベリファイ。電源OFF 30秒→ON。
2. `bootgate status`がHOLDになってからPG（C19上側）とAVDD（TP4）をGND基準で測り、投入からの概算秒数とともに記録。保留中はバックライトOFFが想定されるので画面の白判定はしない。
3. `bootgate continue`を一度実行。`bootgate status`でDISPLAY_READYを確認。別途画面が落ち着いてから正常／白／その他を記録し、PG/TP4を再測定、boottrace全文も保存。
4. 状態がERROR、30秒経ってもINIT/RUNNING等から進まない、プロンプト不能ならその回で中止。再開を連打しない。測定のため給電を差し替えない。

HOLD中からPG HighかつTP4低下なら、その診断起動では表示開始前に異常あり。進行後のみ異常なら後段を次の対象にする。他タスクの影響は残る。保留で再発が消えた場合は時系列を確定できず、修正成功とはしない。ACでの白率比較は別途、既存の同一セッション・10回基準を守る。

### gate-r1実装結果

全1595オブジェクト再コンパイル・リンク、MOT/ELF一致、CPU間重複0、入力ハッシュ照合を完了。
追加状態は内部RAM44バイト。手順は `scripts/issue232-gate/README.md`、静的根拠は同ディレクトリの `static-audit.md`。
配布先は `e2studio_CPU0/Debug/issue232/gate-r1/LCD_GATE_r1.zip`。実機未確認、書込みは未実施。

実機結果追記: ユーザーから2起動のログ・電圧を受領。両回ともHOLD中からPG=4.79V、TP4約2.62V。
再開後もPG High・TP4低下が継続し、1回目は白を明記、2回目の画面分類は未記載。
保留・再開・GLCDC成功復帰を確認。詳細と限界は `doc/analysis_report/issue232-gate-r1-pc/README.md`。
上記「実機未確認」は配布時点の記録であり、本追記を最新とする。gate-r1の追加反復は現時点で不要。

---

進捗追記: trace-r1の実機ログを2起動分受領（白1・正常1）。両方でRTC初期化済み分岐、初期化復帰、最初の描画提出／Vsync待ち復帰を確認。解析は `doc/analysis_report/issue232-trace-r1-pc/README.md`。追加調査は同ディレクトリの `follow-up.md` に記載し、既存コマンドで取得するため追加実装・ビルドは不要。以下の「実機未確認」は配布時点の記録。

2026-09-23／ユーザーが本設計での実装・ビルドを承認。診断版trace-r1の実装・ビルド・静的検証を完了。書込み・実機測定は未実施。

## 目的・範囲

RTC初期化の実際の分岐・所要時間と、GLCDC開始・最初の描画提出の前後関係を1回の起動記録で比較する。
ユーザー選択により、最初は従来のJ10-PC接続で取得を試す。AC給電中の取得方法は別途決定し、ACの白5/10との因果比較には使わない。
通常mainのPOST_C 200ms要求、RTC機能、表示設定、タスク優先度を維持した診断版1組を作る。待ち増量・RTC初期化修正は対象外。
調査根拠のマスタは `doc/analysis_report/issue-232-rtc-retention-boot.md`。旧G/W試験版を基点にしない。

## 設計の入力

- RTC初期化の呼出し元はntshell_task（`e2studio_CPU0/src/ntshell_thread_entry.c:213`）。Open→分岐→ClockSourceSetまたはCalendarTimeGet→条件付きOS時刻同期（`src/time_ctrl.c:145-230`）。
- 分岐は既存のrtc_is_provisioned()評価結果を一度だけ保存して記録する。判定のための追加読出しを行わず、戻り値処理と同期条件を維持する。
- RTC資源ロック取得は最大1000msだが、FSPのレジスタ待ちは上限なし（`src/time_ctrl.c:102,163`、`ra/fsp/src/bsp/mcu/all/bsp_common.h:122`）。戻らなければENDは未記録となる。
- GLCDCはlvgl_task→glcdc_port_init→lvgl_port_mtk3_open（`src/lvgl_thread_entry.c:168`、`src/port/glcdc_port.c:1945-1983`）。Open/Startの既存エラー分岐を維持する（`src/port/lvgl_port_mtk3.c:128-152`）。
- flushはlv_display_set_flush_cbで登録され、LVGLのcall_flush_cbからタスク文脈で呼ばれる（`src/port/lvgl_port_mtk3.c:161`、`ra/lvgl/lvgl/src/core/lv_refr.c:1389-1410`）。
- LV_EVENT_FLUSH_FINISHはflush_cb復帰直後であり、Vsync待ち完了ではない。バックライトONはこのイベント内（`src/port/glcdc_port.c:342-350`）。画面に正常な絵が見えた証拠とは扱わない。
- flush_waitは次のVsync待ちで、無期限待ち。BufferChangeもINVALID_UPDATE_TIMING時に無制限再試行する（`src/port/lvgl_port_mtk3.c:285-324`）。これらの待ち方を変更しない。
- 以下の短縮パスはすべてe2studio_CPU0配下。上記APIの下位経路・復帰処理は既存解析と実コードを確認済み。

## 記録点（固定スロット・各点初回のみ）

| 系統 | 記録点／付帯情報 |
|---|---|
| 起点 | POST_C入口：DWT状態・SystemCoreClock。OS時刻とは別のメタ情報 |
| RTC全体 | INIT_BEGIN／INIT_END（ntshell呼出しを囲む、最終戻り値を保存） |
| RTC内訳 | OPEN_BEGIN/END、BRANCH（実際の判定結果）、PROVISION_BEGIN/END、GET_BEGIN/END、SYNC_BEGIN/END。未実行の点は未記録 |
| RTC状態 | Open後・分岐評価後のRCR1/2/4とSOSCCR/SOMCRを一度採取。追加読出しの観測影響を明記。BSP操作前の値とは呼ばない |
| 表示初期化 | DISPLAY_BEGIN/END、RESET_BEGIN/END、GLCDC_OPEN_BEGIN/END、GLCDC_START_BEGIN/END、初期BufferChangeのBEGIN/END（API戻り値付き） |
| 最初の描画 | 最初の最終領域flushのBEGIN／BufferChange復帰（targetと結果）。初期の黒バッファ設定と区別 |
| 表示の後続 | BACKLIGHT要求直後（PinWriteの戻り値付き）、最初の最終領域flush_waitのBEGIN/END（戻り値付き） |

描画の用語は「描画済みバッファの提出」「Vsync待ち復帰」「バックライト要求」を分ける。パネルでの表示成功はユーザーの目視結果を別項目として記録する。
INIT_BEGIN〜ENDにはロック取得・プリエンプト時間も含む。API区間も他タスク／ISRの実行を含み、CPU専有時間とは呼ばない。

## 時刻・並行性・保存

- 全OS後イベントに、通番・tk_get_otmの64bit稼働時刻（10ms刻み）・DWT生値・DWT世代・結果を保存。記録順は通番で判定し、同じOS時刻を同時発生と解釈しない。
- DWTはPOST_Cで有効化するが、既存値をリセットしない。AIは既存でCYCCNTを0に戻す（`src/ai_inference_thread_entry.c:225-229`）ため、その処理と診断用世代加算を同じ短いタスク排他区間で実行する。元のリセット自体は維持する。
- カメラ側はDWT無効時のみゼロ化する（`src/camera_layer/camera_utils.c:214-221`）。全CYCCNT/CTRL書込みをビルド時に再検索し、未管理の再初期化があればDWT差分を無効にする。
- DWT差分は同じ世代・有効状態で、OS時刻差と10ms量子化余裕から1周未満と確認できる区間だけ計算する（1GHzなら約4.295秒）。確認できない区間を推定で補完しない。
- DWT値はサイクル差として出す。sleep/停止等をまたぐ一般区間の実時間とは断言しない。OS時刻差も併記し、10ms未満を0msと断定しない。カウンタ進行プローブも保存する。
- 状態は内部RAMの固定スロット配列（最大32点、約1.5KB以内）、次通番、DWT世代のみ。BSS初期化後のPOST_Cから使用し、SDRAMに置かない。起動後の消去コマンドは設けない。
- 書き手はPOST_C（OS前）、ntshell/LVGL/AIの各タスク。ISR・CPU1は書かない。OS後の時刻採取・通番加算・スロットpublishと世代変更はtk_dis_dspの同一区間で行う。
- 読み手はntshellのboottraceコマンド。tk_dis_dsp中に固定サイズをsnapshotし、解除してからUART出力。下位API、UART、待ち、動的確保を排他区間に入れない。
- コマンドはcmdlist→usrcmd_execute→ntopt経由（`src/usrcmd.c`、旧` scripts/issue230-trace/boot_trace.c:58-107`参照）。追加UARTログを起動中に出さない。
- RTC初期化でntshellが戻らない場合はコマンド取得も不能。この初版で専用救済タスク・強制リセット・フラッシュ保存は追加せず、取得不能として試運転を中断する。RAMの生デバッガ読出しもキャッシュ整合未保証なので対象外。

## 変更ファイル・ビルド・検証

`scripts/issue232-trace/`に診断用boot_trace.c/h、オーバーレイビルド・検証・ログ解析スクリプトを置き、診断用コピーにのみ適用する。
コピー内の変更対象は `src/{hal_warmstart,ntshell_thread_entry,time_ctrl,lvgl_thread_entry,ai_inference_thread_entry,usrcmd}.c` と `src/port/{glcdc_port,lvgl_port_mtk3}.c`。新規boot_traceの全公開シンボルと参照を照合する。
通常ソース・ra/ra_gen・CPU1は編集しない。FSP内部の個別レジスタ待ちへの記録は初版の範囲外で、停止位置はAPI単位までとする。BSP初期化前のレジスタ採取も今回の目的から外す。
現行mainのソース／ビルド入力を固定して診断コピーをビルドし、CPU0/CPU1ペア・map・逆アセンブル・差分・SHA-256・入力来歴を新規 `e2studio_CPU0/Debug/issue232/trace-r1/` へ保存。旧成果物を上書きしない。
検証はリンク成功、記録RAM配置、200ms位置、RTC判定/戻り値処理の維持、全記録点の呼出し、DWT世代処理、CPU1ペア整合、MOT/ELF内容照合。ログ解析は欠落・エラー・10ms同値・周回/世代跨ぎを検証する。
再リンクと計測処理で配置・タイミングは変わる。診断版で白が消えても修正成功とは扱わず、従来版との固定配置比較とは呼ばない。

## 試運転と判断

最初はRTC電池ON・時刻設定済み・スピーカー接続ありを維持し、J10-PCの同一ポート／ケーブルで最大2起動、OFF 30秒。追加のtime setは必要時のみ事前準備として実施し、測定中には行わない。
各回で起動後の表示分類（正常／白／黒／カラーバー）、表示判定までの時間、boottrace全文を採る。起動後30秒で取得不能なら中断し、電源を切る前に状況を報告する。正常・白の両方が出るまで繰り返さない。
ログにはFW識別・実行分岐・欠落点・API結果・時間差を含める。終了点がなければ「未到達/未完了」であり成功・0msとはしない。
この2回は取得の試運転であり白率の判定ではない。成立後に記録を見てRTC保持／非保持の同一セッション比較（必要なら各10回）を別途決める。電池OFFでも非保持／初期化実行を仮定せず実際の分岐を確認する。
捨てた案: 旧boottraceの10ms時刻だけ（短い差と同刻の順序が不明）、DWTだけ（AIリセット／周回）、新規タイマー（初期化介入が増える）、FSP書換え（生成コード編集禁止）、起動中printf（タイミング攪乱）、AC→PC差替え（別起動になる）。

## 実装結果（2026-09-23）

- 保存先: `e2studio_CPU0/Debug/issue232/trace-r1/`。通常ソースと生成コードは無変更。CPU0は全1,595オブジェクトを診断コピーから再コンパイルしリンク成功。
- 固定26点、内部RAM `0x2210BEA0` に968バイト。シェルsnapshotも同サイズ。実装は `scripts/issue232-trace/boot_trace.c`、差分はrun内 `instrumentation.diff`。
- MOTとELFのロード内容／エントリポイント／S-recordチェックサム一致、CPU間ロード領域重複0、入力ソースハッシュを確認。CPU1はmain-wait-r2の配布物と同一。
- 配布物のハッシュ・ログ識別値・取得手順のマスタは `scripts/issue232-trace/README.md`。artifacts/READ-ME.mdはその配布用コピー。
- ログ解析器の12テスト成功（未完了・同一tick・DWTリセット・周回・エラー・不正識別等）。実機の所要時間・表示結果はまだない。
- POST_C 200ms要求とRTC分岐を実命令で確認。個別のアドレス・記録点・検証範囲はartifacts/static-audit.mdへ保存。
