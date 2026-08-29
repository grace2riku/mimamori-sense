/**
 * @file time_cache.h
 * @brief 現在時刻のノンブロッキング読み出し用キャッシュ（S-012-3 / Issue #213）
 * @details
 * `time_ctrl_get()` を 1 Hz で呼ぶ専用タスクを持ち、取得結果をスナップショットとして
 * publish する。読み出し側（`time_cache_get()`）は publish された値を写すだけなので、
 * **ロックも待ちも伴わない**。
 *
 * 設計メモ: `doc/design/issue-213.md`
 *
 * ## なぜキャッシュを挟むのか
 *
 * `time_ctrl_get()` は内部で `R_RTC_CalendarTimeGet()` を呼ぶ。この API は carry 割り込みを
 * 一時的に有効化するために RCR1 の反映を待つが（`r_rtc.c:435-441` →
 * `r_rtc_irq_set()` `r_rtc.c:1159-1170`）、その待ちは `FSP_HARDWARE_REGISTER_WAIT`
 * つまり**タイムアウトの無い `while` ループ**である（`bsp_common.h:122`）。
 * RCR1 の更新はカウントソース（サブクロック）に同期するため、**サブクロックが停止すると
 * `time_ctrl_get()` から戻らなくなる**。
 *
 * LVGL の `lv_timer` コールバックは `lv_timer_handler()` が `lv_lock()` を保持した区間で
 * 走る（`lv_timer.c:81,327,144`）。そこから直接 `time_ctrl_get()` を呼ぶと、この無限待ちが
 * `lv_lock()` を握ったまま発生し、**描画スレッドを含む LVGL 全体が恒久的にフリーズする**。
 * そこで RTC を触る役目を専用タスクへ追い出し、被害をそのタスク 1 本に閉じ込める。
 * （PR #217 レビュー指摘 P1。詳細は設計メモ §7）
 *
 * ## 陳腐化の検出
 *
 * ポーリングタスクが RTC の中でハングすると publish が止まる。`time_cache_get()` は
 * 最終 publish からの経過時間を見て、`TIME_CACHE_STALE_MS` を超えていたら false を返す。
 * 呼び出し側は「時刻不明」として扱えばよく、自タスクは止まらない。
 *
 * ## 実行コンテキスト制約
 *
 * - `time_cache_init()` は `lvgl_task` から 1 回だけ呼ぶ（`ui_datetime_init()` 経由）
 * - `time_cache_get()` は**タスクからのみ**呼ぶこと。内部で `tk_dis_dsp()` を使うため
 *   ISR からは呼べない。ブロックはしないので、ディスパッチ禁止区間を除けば
 *   どのタスクからでも安全に呼べる
 */

#ifndef TIME_CACHE_H
#define TIME_CACHE_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdbool.h>

#include "time_ctrl.h"      /* time_ctrl_time_t */

#ifdef __cplusplus
extern "C" {
#endif

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * 時刻キャッシュのポーリングタスクを起動する
 *
 * @details 1 Hz で `time_ctrl_get()` を呼び、結果を publish するタスクを生成・起動する。
 *          `time_ctrl_init()` より先に呼ばれてもよい（その間は `TIME_CTRL_ERR_NOT_INIT`
 *          が返るので、キャッシュは「無効」のまま publish される）。
 *
 * @note 1 回だけ呼ぶこと。2 回目以降は何もせず true を返す。
 *
 * @retval true  利用可能なポーリングタスクがある
 * @retval false タスクの生成または起動に失敗した（以後 `time_cache_get()` は常に false）
 */
bool time_cache_init(void);

/**
 * キャッシュされた現在時刻を取得する（ノンブロッキング）
 *
 * @details `tk_dis_dsp()` 区間でスナップショットを写すだけで、ロック取得も
 *          レジスタ待ちも行わない。所要時間は µs 未満。
 *
 * @param[out] p_out 取得したカレンダー時刻。false を返す場合は書き換えない
 *
 * @retval true  有効な時刻を取得した
 * @retval false 時刻が不明。次のいずれか:
 *               - `time_cache_init()` が未実行／失敗している
 *               - 直近のポーリングが失敗した（時刻未設定・RTC 異常など）
 *               - 最終更新から `TIME_CACHE_STALE_MS` 以上経過している
 *                 （＝ポーリングタスクが RTC の中で戻らなくなっている）
 */
bool time_cache_get(time_ctrl_time_t *p_out);

#ifdef __cplusplus
}
#endif

#endif /* TIME_CACHE_H */
