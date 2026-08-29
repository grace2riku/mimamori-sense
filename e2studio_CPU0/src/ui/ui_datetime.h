/**
 * @file ui_datetime.h
 * @brief ステータスバー日時ラベルの周期更新（S-012-3 / Issue #213）
 * @details
 * メイン画面ステータスバー中央の日時ラベル（`ui_main_screen.c:351-355`）を、
 * 現在時刻で更新する。`lv_timer` を 1 秒周期で回し、**年月日時分が変化した
 * ときだけ** `ui_main_screen_set_datetime()` を呼ぶ。
 *
 * 設計メモ: `doc/design/issue-213.md`
 *
 * ## 表示
 *
 * - 時刻が取得できたとき: `"YYYY-MM-DD hh:mm"`（例 `"2026-08-28 12:34"`）
 * - 時刻が不明なとき: `"--:--"`（ラベルの生成時の初期値と同じ）
 *
 * 実再描画は 1 分に 1 回で、表示の遅れは最大 2 秒（`time_cache` のポーリング 1 秒 ＋
 * 本モジュールの描画 1 秒）。60 秒周期でポーリングしないのは、`lv_timer` の位相が
 * RTC の分境界と無関係なため表示が最大 60 秒古くなるから
 * （`doc/design/issue-212.md` §7）。
 *
 * ## 時刻の入手経路
 *
 * `time_ctrl_get()` は**呼ばない**。`time_cache_get()`（ノンブロッキング）を使う。
 * `lv_timer` コールバックは `lv_lock()` を保持した区間で走るため、そこでブロックしうる
 * API を呼ぶと LVGL 全体が止まりうる。理由の詳細は `time_cache.h`
 * （PR #217 レビュー指摘 P1）。
 *
 * ## 実行コンテキスト
 *
 * **`lvgl_task` 専用。** `ui_datetime_init()` は `lvgl_task`（`lvgl_thread_entry.c`）
 * から 1 回だけ呼ぶこと。内部のタイマコールバックも `lv_timer_handler()` 経由で
 * `lvgl_task` からのみ呼ばれる。本モジュールの状態変数を触るコンテキストは
 * この 1 つだけなので、排他は持たない。
 */

#ifndef UI_DATETIME_H
#define UI_DATETIME_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 日時ラベルの周期更新を開始する
 *
 * @details `time_cache_init()`（時刻ポーリングタスクの起動）と、1 秒周期の `lv_timer`
 *          登録を行う。ラベルの実体は `ui_main_screen` が持つため、
 *          `ui_main_screen_create()` の後に呼ぶこと。
 *
 * @note `lvgl_task` から 1 回だけ呼ぶこと。2 回目以降は何もしない（多重登録を防ぐ）。
 *       `time_cache_init()` が失敗した場合もタイマは登録する
 *       （`time_cache_get()` が常に false を返し、ラベルは `"--:--"` のままになる）。
 */
void ui_datetime_init(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_DATETIME_H */
