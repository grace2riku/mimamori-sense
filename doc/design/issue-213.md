# Issue #213 設計メモ — ステータスバー日時ラベルの更新

対象: `e2studio_CPU0/src/ui/ui_datetime.{c,h}`（新規）/ `lvgl_thread_entry.c`

## 1. 設計の入力（確定値・根拠付き）

| 項目 | 確定内容 | 根拠 |
|---|---|---|
| 呼び出し元 | `ui_datetime_init()` は `lvgl_task` の 1 箇所のみ（`lvgl_thread_entry.c` Step 7c として追加）。**間接呼び出し**: `ui_datetime_timer_cb` は `lv_timer_create()` で登録され `lv_timer_handler()` から呼ばれる → 実行コンテキストは `lvgl_task` のみ | `lvgl_thread_entry.c:106,200,267` |
| 同時呼び出し | 本モジュールの状態を触るのは `lvgl_task` の 1 コンテキストだけ（writer / reader とも同一タスク）。`time_ctrl_get()` は本 Issue 完了後 `ntshell_task`(prio 12) と `lvgl_task`(prio 14) の 2 タスクから呼ばれるが、#212 の `TA_INHERIT` 資源所有ミューテックスが直列化する（`time_ctrl.c:481-521`）。**書き手はタスクのみ・ISR 経路なし**という #212 の前提は本 Issue でも崩れない → **#212 側の変更は不要** | `usermain.c:194,239` / `doc/design/issue-212.md` §5 |
| ブロック許容時間 | `lv_timer` コールバック内。1 フレーム予算 33.3 ms（KPI-01 / #176）を圧迫しないこと | Issue #213 |
| 依存先 API の最悪所要時間 | `time_ctrl_get()` ≒ **0.1 ms**（`R_RTC_CalendarTimeGet()` の carry IRQ 一時有効化・復元 2 待ち × 30.5 µs ＋ レジスタ読み）。1 Hz なので CPU 0.01%、フレーム予算の 0.3%。**ただしロック待ちは別**: サブクロック喪失で保持タスクが FSP 内で戻らなくなると `tk_loc_mtx()` のタイムアウト **1000 ms**（`time_ctrl.c:101,521`）まで `lvgl_task` がブロックする。§3 で対処する | `doc/design/issue-212.md` §1 / `r_rtc.c:421-470` |
| 失敗の返し方 | 表示専用。戻り値は使わない。`TIME_CTRL_OK` 以外はすべてラベルを `"--:--"` にして時刻表示と区別する | Issue #213 |
| 実行コンテキスト制約 | `lvgl_task` のみ。ISR から呼ばない（`time_ctrl_*` は ISR 禁止: `time_ctrl.h` 「実行コンテキスト制約」） | `time_ctrl.h` |

## 2. 状態変数と読み書きコンテキスト

| 変数 | 書き手 | 読み手 | 保護 |
|---|---|---|---|
| `s_timer` (`lv_timer_t *`) | `lvgl_task`（`ui_datetime_init()` のみ） | `lvgl_task`（多重登録の判定） | 不要（単一タスク） |
| `s_shown` (`time_ctrl_time_t`, 表示中の年月日時分) | `lvgl_task`（cb） | `lvgl_task`（cb） | 不要（単一タスク） |
| `s_shown_valid` (bool, 時刻を表示中か) | 同上 | 同上 | 不要（単一タスク） |
| `s_busy_count` (uint32_t) | 同上 | 同上 | 不要（単一タスク） |

**排他は不要**。writer / reader が `lvgl_task` の 1 コンテキストに閉じており、ISR も他タスクも
本モジュールの変数に触れない。RTC 資源の直列化は `time_ctrl` 内部のミューテックスが行う
（CLAUDE.md「資源所有ロックは保持したまま呼ぶのが正しい」に該当。呼び出し側での追加排他は禁止）。

### ロック順序（デッドロックしないことの確認）

本 Issue で **`lv_lock()` → RTC ミューテックス** という新しいロック順序が生まれる。コールバックは
`lv_timer_handler()` が `lv_lock()` を取った区間で走る（`lv_timer.c:81,327,144`）ため、
`time_ctrl_get()` はその内側で RTC ミューテックスを取る。逆順（RTC ミューテックス保持中に
`lv_lock()`）が存在しないことを確認済み:

- `time_ctrl.c` に LVGL 呼び出しは 1 件も無い（`lv_` の出現はコメント 1 箇所のみ）。
  RTC ミューテックスは `time_ctrl.c` 内部でしか保持されず、外部へ公開されていない
- `lv_lock()` を取るもう 1 つのコンテキストは `ntshell_task` の `lvgl` シェルコマンド
  （`usrcmd.c:410-590`）だが、`usrcmd.c` は `time_ctrl_*` を呼ばない（grep 0 件。
  `time` コマンドの実体は `time_cmd.c` で、そちらも LVGL を呼ばない）

→ 逆順の取得経路が無いのでデッドロックしない。ただし RTC ハング時は `lvgl_task` が
`lv_lock()` を握ったまま 1000 ms 待つため、`lvgl` シェルコマンドも巻き添えで待たされる。
これは §3 の縮退で有限回に抑える。

## 3. ポーリング周期と RTC ハング時の縮退

- **1 秒周期 ＋ 変化検出**。`lv_timer_create(cb, 1000, NULL)`。年月日時分が変わったときだけ
  `ui_main_screen_set_datetime()` を呼ぶ（実再描画は 1 分に 1 回、表示遅延は最大 1 秒）。
  60 秒周期にしない理由は `doc/design/issue-212.md` §7（位相が RTC の分境界と無関係）。
- 表示書式は `"YYYY-MM-DD hh:mm"`（16 文字）。ラベルは `lv_font_montserrat_14`、
  ステータスバー中央（`ui_main_screen.c:351-355`）。左の `Idle` と右の 80px ボタンとは重ならない。
- **`TIME_CTRL_ERR_BUSY` が `UI_DATETIME_BUSY_GIVEUP`(=3) 回連続したらタイマを停止して更新を止める。**
  理由: BUSY は 1000 ms 待って取れなかったことを意味する。正常時の保持は 0.2 ms 以下なので
  競合による BUSY は起きえず、BUSY = 保持タスクが RTC 内でハングしている（復旧経路が無い）。
  放置すると毎秒 1000 ms、**`lv_lock()` を握ったまま**ブロックする（コールバックは
  `lv_timer_handler()` が `lv_lock()` を取った区間で走る: `lv_timer.c:81,327`）ので、
  描画スレッドを含む LVGL 全体が止まる。
  giveup 後はラベルを `"--:--"` に固定する（検出＝BUSY 連続、復旧＝更新の恒久停止）。
- 停止は `lv_timer_delete()` ではなく **`lv_timer_pause()`** を使う。自タイマの削除自体は
  LVGL 側で安全に扱われる（`lv_timer.c:342` の `state.timer_deleted` ガード）が、pause なら
  `s_timer` が有効なまま残り、`ui_datetime_init()` の多重登録ガード（§4）が効き続ける。
  停止したタイマは次回起動時刻の計算からも除外される（`lv_timer.c:118`）。

## 4. 多重登録・破棄

`ui_main_screen` は `s_created` で二重生成を防ぎ（`ui_main_screen.c:156-158`）、**破棄経路は存在しない**
（`src/` 全体で `lv_obj_delete` / `lv_obj_del()` の呼び出しは 0 件、`lv_screen_load()` は
`ui_main_screen.c:199` の 1 件のみ）。
本モジュールも `s_timer != NULL` なら `ui_datetime_init()` を no-op とし、多重登録を防ぐ。
将来 `ui_main_screen` に破棄経路を足す場合は `ui_datetime_deinit()` を同時に追加すること
（本 Issue では呼び出し元が無いので作らない）。

## 5. 捨てた代替案

- **`tk_get_utc()` を読む**: RTC アクセスもロックも不要で最速だが、SysTick ドリフトが乗る（#212 §7「正の時刻源は常に RTC」）。破棄。
- **RTC 周期割り込み（1 分）**: ベクタ未割当で `configuration.xml` 変更が要り、ISR から LVGL API を呼べず中継が純増（#212 §7）。破棄。
- **`ui_main_screen.c` にタイマを内蔵**: UI 層に `time_ctrl`（μT-Kernel / RTC）が入り層が混ざる。`camera_display.c` / `fall_detection_screen.c` と同じ「別モジュールがタイマを持ち setter を呼ぶ」形に揃える。破棄。
- **BUSY で giveup せず毎秒リトライ**: RTC ハング時に画面が 1 fps に固まる（§3）。破棄。
- **`lvgl_thread_entry.c` にコールバックを直書き**: 状態変数 4 個とタイマ寿命がスレッド起動コードに混ざる。破棄。

## 6. ビルド結果（CLI ビルドで実測。`e2studio_CPU0/Debug` で `make -j16 all`）

| | text | bss | `.flash.endof` |
|---|---|---|---|
| 変更前（本ブランチの変更を stash） | 806,778 | 6,776,865 | `0x020C5200` |
| 変更後 | 807,114 | 6,776,897 | `0x020C5200` |
| 差分 | **+336 B** | **+32 B** | **±0** |

FLASH 使用量は 807,424 B / 1,015,808 B（992 KiB, `0xF8000`）で **変化なし**
（`.flash.endof` の 0x200 境界のパディングに収まった）。
`ui_datetime.c` 自身のコンパイル警告は 0 件（残る 2 件は LVGL ヘッダ由来の既存警告）。
