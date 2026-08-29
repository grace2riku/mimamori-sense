/**
 * @file ui_datetime.c
 * @brief ステータスバー日時ラベルの周期更新の実装（S-012-3 / Issue #213）
 * @details
 * 1 秒周期の `lv_timer` から `time_cache_get()`（ノンブロッキング）を呼び、年月日時分が
 * 変化したときだけ `ui_main_screen_set_datetime()` でラベルを書き換える。
 *
 * `camera_display.c` / `fall_detection_screen.c` と同じく、**タイマを持つ側が
 * 別モジュール**で、`ui_main_screen` の setter を呼ぶ形にしている。
 * `ui_main_screen.c` は LVGL だけに依存する表示層に保つ。
 *
 * 本モジュールも μT-Kernel には依存しない。RTC アクセス（ブロックしうる）は
 * `time_cache` のポーリングタスクが担当する。理由は `time_cache.h`。
 *
 * 設計メモ: `doc/design/issue-213.md`
 *
 * @note This file is part of the time display implementation (S-012-3).
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "ui_datetime.h"
#include "ui_main_screen.h"

#include "lvgl.h"

#include "time_cache.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * ポーリング周期 [ms]
 *
 * @note 60000 にしてはならない。`lv_timer` の位相は RTC の分境界と無関係なので、
 *       表示が最大 60 秒古くなる（`doc/design/issue-212.md` §7）。
 *       1 秒で回して「分が変わったときだけ書き換える」ことで、実際の再描画は
 *       1 分に 1 回・表示遅延は最大 2 秒に収まる。
 */
#define UI_DATETIME_POLL_PERIOD_MS  (1000)

/** 時刻が不明なときの表示（ラベル生成時の初期値と同じ: `ui_main_screen.c:352`） */
#define UI_DATETIME_UNSET_TEXT      "--:--"

/** 表示文字列のバッファサイズ（"YYYY-MM-DD hh:mm" = 16 文字 ＋ NUL） */
#define UI_DATETIME_TEXT_SIZE       (20)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/*
 * 以下の状態はすべて `lvgl_task` の 1 コンテキスト（`ui_datetime_init()` と
 * `lv_timer_handler()` 経由のコールバック）だけが読み書きする。他タスクも ISR も
 * 触らないので排他は持たない（設計メモ §3）。
 */

/** 登録した `lv_timer`。多重登録の判定に使う */
static lv_timer_t       *s_timer = NULL;

/** `s_shown` が有効か（＝ラベルに時刻を表示中か。false なら `"--:--"` 表示中） */
static bool              s_shown_valid = false;

/** ラベルに表示中の日時。`sec` / `wday` は表示しないので比較にも使わない */
static time_ctrl_time_t  s_shown;

/**********************************************************************************************************************
 Private (static) function prototypes
 *********************************************************************************************************************/
static void ui_datetime_timer_cb(lv_timer_t *timer);
static void ui_datetime_show_unset(void);

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

void ui_datetime_init(void)
{
    if (s_timer != NULL) {
        /* 多重登録を防ぐ。`ui_main_screen_create()` の `s_created` ガードと同じ役割
         * （`ui_main_screen.c:156-158`）。画面の破棄経路が存在しないため、
         * 対になる deinit は用意しない（設計メモ §5）。 */
        return;
    }

    s_shown_valid = false;

    /* 時刻ポーリングタスクを起動する。失敗しても続行する: `time_cache_get()` が
     * 常に false を返し、ラベルは `"--:--"` のままになるだけで、描画は妨げない。 */
    (void)time_cache_init();

    s_timer = lv_timer_create(ui_datetime_timer_cb,
                              UI_DATETIME_POLL_PERIOD_MS,
                              NULL);
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * 日時ラベル更新タイマのコールバック
 *
 * @details `lv_timer_handler()` から呼ばれる（`lv_timer.c:327`）。その区間は
 *          `lv_lock()` が保持されている（`lv_timer.c:81,144`。`LV_USE_OS ==
 *          LV_OS_CUSTOM`: `lv_conf_user.h:79`）。したがって
 *          `ui_main_screen_set_datetime()` を追加のロック無しで呼んでよい一方、
 *          **ここでブロックしうる API を呼んではならない**（LVGL 全体が止まる）。
 *          `time_cache_get()` は `tk_dis_dsp()` 区間の構造体コピーだけで、待たない。
 *
 * @param timer 本コールバックを登録したタイマ（未使用）
 */
static void ui_datetime_timer_cb(lv_timer_t *timer)
{
    time_ctrl_time_t now;
    char             text[UI_DATETIME_TEXT_SIZE];

    (void)timer;

    if (!time_cache_get(&now)) {
        /* 未初期化 / 時刻未設定 / RTC 異常 / キャッシュ陳腐化（ポーリングタスクが
         * RTC の中で戻らなくなった）のいずれか。時刻として表示できない。 */
        ui_datetime_show_unset();
        return;
    }

    /* 分が変わるまで再描画しない。実際に `lv_label_set_text()` が走るのは 1 分に 1 回。 */
    if (s_shown_valid
        && (now.year == s_shown.year)
        && (now.mon  == s_shown.mon)
        && (now.mday == s_shown.mday)
        && (now.hour == s_shown.hour)
        && (now.min  == s_shown.min))
    {
        return;
    }

    /* `time_cache_get()` が true を返すのは `time_ctrl_get()` が成功した値のみで、
     * year 2000-2099 / mon 1-12 / mday 1-31 / hour 0-23 / min 0-59 が保証されている
     * （`time_ctrl.c:288-294` の `time_is_valid()`）ので、書式は必ず 16 文字に収まる。 */
    (void)snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u",
                   (unsigned int)now.year,
                   (unsigned int)now.mon,
                   (unsigned int)now.mday,
                   (unsigned int)now.hour,
                   (unsigned int)now.min);

    ui_main_screen_set_datetime(text);

    s_shown       = now;
    s_shown_valid = true;
}

/**
 * ラベルを「時刻なし」表示に戻す
 *
 * @details 既に `"--:--"` を表示している場合は何もしない。毎秒 `lv_label_set_text()`
 *          を呼ぶと、時刻が不明なあいだステータスバーを無駄に再描画し続けるため。
 */
static void ui_datetime_show_unset(void)
{
    if (!s_shown_valid) {
        /* 初期状態（ラベル生成直後）と、既に "--:--" へ戻した後がここに該当する。 */
        return;
    }

    ui_main_screen_set_datetime(UI_DATETIME_UNSET_TEXT);
    s_shown_valid = false;
}
