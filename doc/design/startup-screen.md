# 起動画面と5秒間のローディング表示

2026-09-24: 表示設計と圧縮格納方式をユーザー承認済み。実装・自動検証完了。
2026-09-25: ユーザーによる実機確認が完了し、問題なしと判断。詳細は末尾の受入結果を参照。
Issue番号未指定のため、この名前で記録する。

## 表示と遷移

- 原画は `doc/mimamori_sense_start_disp.png`。縦横比維持で1024×600へ縮小し、RGB565化する。中央に直径64pxの青い回転リングと84pxの淡い円形下地を置く（`e2studio_CPU0/src/ui/ui_startup_screen.c:71`、同`:83`、同`:92`）。
- 既存メイン画面を生成し、カメラ・転倒表示・時計の初期化後、最初の描画前に起動画面へ切り替える（`e2studio_CPU0/src/lvgl_thread_entry.c:209`、同`:234`、同`:254`、同`:282`、同`:286`）。既存画面の生成失敗はログを残してタスク終了（同`:210`）。
- 初回の `LV_EVENT_REFR_READY` でタイマーを開始し、5000ms経過後のタイマー処理で保持していたメイン画面へ戻す。追加の描画完了で期限を延長しない（`e2studio_CPU0/src/ui/ui_startup_screen.c:29`、同`:40`、同`:100`）。
- 終了時にイベント登録、タイマー、起動画面と子ウィジェットを削除（同`:40`）。オブジェクト削除時のアニメーション削除は `e2studio_CPU0/ra/lvgl/lvgl/src/core/lv_obj.c:525`。
- メイン画面とその更新タイマーは保持する。表示期間は電源投入時刻やAI／カメラ準備完了とは連動しない。タッチでのスキップ機能は設けない。

## 圧縮画像と容量

当初の非圧縮RGB565定数（1,228,800 bytes）は、内蔵フラッシュ割当1,015,808 bytesを超えるため不採用。圧縮格納・SDRAMへの事前展開へ変更し、ユーザーの再承認を得た。

- **縮小・RGB565化した画素列を可逆圧縮**する。LANCZOS縮小後の最終資産は136,642 bytes（約133KiB）。当初の調査値130,779 bytesはPillow既定の縮小補間での値だった。
- 変換スクリプトは `scripts/startup-screen/convert_image.py`。資産は `ui_startup_image_data.inc`、画像ハッシュは同ファイル冒頭。実行時PNGデコードや外部フラッシュ初期化は追加しない。
- zlib公式の小型DEFLATE展開器puffをライセンス付きで使用。出典・バージョン・ハッシュのマスタは `e2studio_CPU0/src/ui/puff/README.md`。
- 初期描画前に、64バイト整列した1,228,800 bytesの専用SDRAMへ1回展開し、キャッシュをcleanしてから描画に渡す。表示中に画像を変更しない（`e2studio_CPU0/src/ui/ui_startup_screen.c:10`、同`:61`、同`:65`）。
- 展開先サイズをpuffへ渡し、ヘッダ、消費／展開バイト数、Adler-32を確認する（`e2studio_CPU0/src/ui/ui_startup_image.c:6`、同`:20`、同`:38`）。失敗時は呼び出し元でログを残し既存画面を維持する（`e2studio_CPU0/src/lvgl_thread_entry.c:286`）。
- 画像用にLVGLの256KiBヒープを使わない。動的確保は画面・ウィジェット・タイマー等のみ。返却値がある生成APIの失敗は後始末してfalseを返す（`e2studio_CPU0/src/ui/ui_startup_screen.c:68`、同`:118`）。LVGL内部の全メモリ不足経路の安全性を保証する変更ではない。

## 呼び出し元・並行性・所要時間

- 入口は `lvgl_task` から1回のみ。イベント／タイマーコールバックも同タスクのハンドラ経由（`e2studio_CPU0/src/lvgl_thread_entry.c:313` → `e2studio_CPU0/ra/lvgl/lvgl/src/misc/lv_timer.c:327`）。
- 状態は起動画面、復帰先画面、期限タイマー、初回描画待ちフラグ。書き手・読み手はLVGLタスクのみ。ISR共有要求、所有タスクの追加、順序付けmutexは不要。
- 完成した画像のみ描画側が読む。専用SDRAMは静的領域として予約したまま。復帰先画面を削除する経路は追加しない。
- 5秒のsleepや下位デバイスAPIを追加しない。展開時間は5秒に含めない。
- 既存のBufferChange再試行とVsync無期限待ちは残るため、故障時の遷移時間に有限の保証はない（`e2studio_CPU0/src/port/lvgl_port_mtk3.c:286`、同`:317`）。描画完了イベントは物理LCDの表示確認ではない（`e2studio_CPU0/ra/lvgl/lvgl/src/core/lv_refr.c:429`）。
- M85向け非LTOコンパイルの参考値として、puffの静的スタックフレームは1704 bytes。LVGLタスクは8192 bytes（`e2studio_CPU0/src/usermain.c:240`）。タスク全体の実機最大使用量は未測定。

## 変更範囲と代替案

- 既存変更: `src/lvgl_thread_entry.c`、`src/lv_conf_user.h`（ARC/SPINNERを有効化、後者`:588`）。
- 追加: `src/ui/ui_startup_screen.c/.h`、`ui_startup_image.c/.h`、`ui_startup_image_data.inc`、`puff/`、`scripts/startup-screen/`。
- FSP生成コード、`ra/`、`ra_gen/` の編集・ビルド除外・シンボル置換はない。既存ファイルの定義シンボルの削除もない。
- 5秒のタスク停止はアニメーションが止まるため不採用。実行時PNGは依存とヒープ消費、低解像度化は画質、外部フラッシュは起動・書込み運用の変更を避けるため不採用。

## 検証結果

- CPU0全1597オブジェクトをLLVM Arm 21.1.1で再コンパイル、リンクとS-record生成に成功。最終出力は `e2studio_CPU0/Debug/startup-screen/build-final/`。入力ハッシュと個別ログを保存する。
- 最終ビルドのmapでフラッシュ末尾0x020ec600、割当末尾0x020f8000、空き47,616 bytesを確認。圧縮画像は0x020cad44（内蔵フラッシュ）、展開バッファは0x68452400（SDRAM）に配置。
- Armエミュレータ上のC展開器で、組み込み画像全バイトの一致、サイズ／NULL拒否、195件の正常・破損・切断・出力不足ケースを検証。前後のcanary破壊なし。
- 実際のLVGLと起動画面コードをArmエミュレータで実行し、リングの角度変化、初回描画前の遷移なし、4999/5000ms境界、追加描画で期限延長なし、二重起動・NULL拒否、イベント／アニメーション削除、繰り返し利用でのヒープ安定、32bit tick周回を確認。
- 初回ライフサイクルではLVGL共通領域の確保サイズに16 bytesの差が出たため、ウォームアップ後にヒープ使用量が増えないことを検証した。
- LVGLソフトウェア描画プレビューを目視確認済み（`Debug/startup-screen/timing-test/startup-preview.png`）。テストはDave2D・GLCDC・物理タッチを検証しない。手順は `scripts/startup-screen/README.md`。
- 自動検証完了時点では実機未確認だった。白画面対策は `white-screen-boot-regression.md` 冒頭のmain-wait-r2正常10/10受入を確認済み。本変更の実機受入結果は末尾を参照。

## 2026-09-25 ユーザー依頼による調整

- 回転周期を1000msから2000msへ変更し、回転速度を半分にした（`e2studio_CPU0/src/ui/ui_startup_screen.c:98`）。起動画面の表示期間は5000msのまま。
- FPS/CPUモニターはディスプレイ生成時に作成され、画面共通のシステムレイヤーに属する（`e2studio_CPU0/ra/lvgl/lvgl/src/display/lv_display.c:163`、`e2studio_CPU0/ra/lvgl/lvgl/src/others/sysmon/lv_sysmon.c:100`）。起動画面のロード直前に非表示にし、通常画面への復帰時に再表示する（`e2studio_CPU0/src/ui/ui_startup_screen.c:112`、同`:46`）。生成失敗で通常画面を維持する経路では非表示にしない。
- `LV_USE_PERF_MONITOR` はコンパイル時の条件。モニター有効ビルドのみ表示切替APIを呼ぶ。同APIは既存ラベルのHIDDENフラグを切り替え、デバイス待ちを追加しない（`e2studio_CPU0/ra/lvgl/lvgl/src/others/sysmon/lv_sysmon.c:100`、同`:129`）。
- 今回のテストでは性能モニターを有効化し、250msで約45度進むこと、起動画面中の非表示、通常画面への復帰後の再表示を確認する。ヒープ比較にFPS文字列の周期更新による確保が混入しないよう、テスト内のみ性能サンプリングタイマーを停止する。
- 今回のCPU0出力先: `e2studio_CPU0/Debug/startup-screen/build-slower-no-fps/`。全1597オブジェクトの再ビルド・リンク・S-record生成が成功し、フラッシュ末尾は0x020ec600で変化なし。
- 上記の回転速度・FPS表示切替テスト、および4999/5000ms境界・tick周回・イベント／アニメーション削除の回帰テストが成功。モニター有効設定での最初の2ライフサイクルはヒープ空きがそれぞれ12 bytes変化したが、後続2回は増減0を確認。

## 2026-09-25 実機受入結果（ユーザー報告）

ユーザーが修正版の実機動作を確認し、「問題なし」と判断した。以下はユーザー報告の記録であり、エージェントによる実測ではない。

- 給電: ACアダプタ
- OFF時間: 30秒
- 確認観点: 連続10回のON⇒OFFで、白画面にならずに起動できるか
- 各回の結果: 1 正常、2 正常、3 正常、4 正常、5 正常、6 正常、7 正常、8 正常、9 正常、10 正常
- 結果: 正常10/10、白画面0/10。本変更は実機受入済み。
- 同一セッションの対照ファーム測定結果や個別の表示時間実測値は報告されていないため、本記録には補完しない。
