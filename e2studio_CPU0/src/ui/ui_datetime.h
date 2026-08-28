/**
 * @file ui_datetime.h
 * @brief ステータスバー日時ラベルの周期更新（S-012-3 / Issue #213）
 * @details
 * メイン画面ステータスバー中央の日時ラベル（`ui_main_screen.c:351-355`）を、
 * RTC の現在時刻で更新する。`lv_timer` を 1 秒周期で回し、**年月日時分が
 * 変化したときだけ** `ui_main_screen_set_datetime()` を呼ぶ。
 *
 * 設計メモ: `doc/design/issue-213.md`
 *
 * ## 表示
 *
 * - 時刻が取得できたとき: `"YYYY-MM-DD hh:mm"`（例 `"2026-08-28 12:34"`）
 * - 時刻未設定・取得失敗のとき: `"--:--"`（ラベルの生成時の初期値と同じ）
 *
 * 実再描画は 1 分に 1 回で、表示の遅れは最大 1 秒。60 秒周期でポーリング
 * しないのは、`lv_timer` の位相が RTC の分境界と無関係なため表示が最大
 * 60 秒古くなるから（`doc/design/issue-212.md` §7）。
 *
 * ## 実行コンテキスト
 *
 * **`lvgl_task` 専用。** `ui_datetime_init()` は `lvgl_task`（`lvgl_thread_entry.c`）
 * から 1 回だけ呼ぶこと。内部のタイマコールバックも `lv_timer_handler()` 経由で
 * `lvgl_task` からのみ呼ばれる。本モジュールの状態変数を触るコンテキストは
 * この 1 つだけなので、排他は持たない。
 *
 * RTC アクセスの直列化は `time_ctrl` 内部の資源所有ミューテックスが行う
 * （`time_ctrl.c:481-521`）。呼び出し側で追加のロックを取ってはならない。
 *
 * ## RTC ハング時の縮退
 *
 * `time_ctrl_get()` が `TIME_CTRL_ERR_BUSY` を返し続けた場合、タイマを停止して
 * 更新を恒久的にあきらめる。詳細は `ui_datetime.c` の `ui_datetime_timer_cb()`。
 */

#ifndef UI_DATETIME_H
#define UI_DATETIME_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 日時ラベルの周期更新を開始する
 *
 * @details 1 秒周期の `lv_timer` を登録する。ラベルの実体は `ui_main_screen` が
 *          持つため、`ui_main_screen_create()` の後に呼ぶこと。
 *
 * @note `lvgl_task` から 1 回だけ呼ぶこと。2 回目以降は何もしない（多重登録を防ぐ）。
 */
void ui_datetime_init(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_DATETIME_H */
