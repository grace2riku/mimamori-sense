/**
 * @file ui_datetime.c
 * @brief ステータスバー日時ラベルの周期更新の実装（S-012-3 / Issue #213）
 * @details
 * 1 秒周期の `lv_timer` から `time_ctrl_get()` を呼び、年月日時分が変化した
 * ときだけ `ui_main_screen_set_datetime()` でラベルを書き換える。
 *
 * `camera_display.c` / `fall_detection_screen.c` と同じく、**タイマを持つ側が
 * 別モジュール**で、`ui_main_screen` の setter を呼ぶ形にしている。
 * `ui_main_screen.c` は LVGL だけに依存する表示層に保ち、μT-Kernel / RTC への
 * 依存（`time_ctrl`）を持ち込まない。
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

#include "time_ctrl.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * ポーリング周期 [ms]
 *
 * @note 60000 にしてはならない。`lv_timer` の位相は RTC の分境界と無関係なので、
 *       表示が最大 60 秒古くなる（`doc/design/issue-212.md` §7）。
 *       1 秒で回して「分が変わったときだけ書き換える」ことで、実際の再描画は
 *       1 分に 1 回・表示遅延は最大 1 秒に収まる。
 */
#define UI_DATETIME_POLL_PERIOD_MS  (1000)

/** `TIME_CTRL_ERR_BUSY` がこの回数連続したら更新をあきらめる */
#define UI_DATETIME_BUSY_GIVEUP     (3U)

/** 時刻未設定・取得失敗時の表示（ラベル生成時の初期値と同じ: `ui_main_screen.c:352`） */
#define UI_DATETIME_UNSET_TEXT      "--:--"

/** 表示文字列のバッファサイズ（"YYYY-MM-DD hh:mm" = 16 文字 ＋ NUL） */
#define UI_DATETIME_TEXT_SIZE       (20)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/*
 * 以下の状態はすべて `lvgl_task` の 1 コンテキスト（`ui_datetime_init()` と
 * `lv_timer_handler()` 経由のコールバック）だけが読み書きする。他タスクも ISR も
 * 触らないので排他は持たない（設計メモ §2）。
 */

/** 登録した `lv_timer`。多重登録の判定に使う */
static lv_timer_t       *s_timer = NULL;

/** `s_shown` が有効か（＝ラベルに時刻を表示中か。false なら `"--:--"` 表示中） */
static bool              s_shown_valid = false;

/** ラベルに表示中の日時。`sec` / `wday` は表示しないので比較にも使わない */
static time_ctrl_time_t  s_shown;

/** `TIME_CTRL_ERR_BUSY` の連続回数 */
static uint32_t          s_busy_count = 0;

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
         * 対になる deinit は用意しない（設計メモ §4）。 */
        return;
    }

    s_shown_valid = false;
    s_busy_count  = 0;

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
 *          `lv_lock()` が保持されている（`lv_timer.c:81`。`LV_USE_OS ==
 *          LV_OS_CUSTOM`: `lv_conf_user.h:79`）ため、`ui_main_screen_set_datetime()`
 *          を追加のロック無しで呼んでよい。
 *
 * @param timer 本コールバックを登録したタイマ（`s_timer` と同じ）
 */
static void ui_datetime_timer_cb(lv_timer_t *timer)
{
    time_ctrl_time_t now;
    time_ctrl_err_t  err;
    char             text[UI_DATETIME_TEXT_SIZE];

    err = time_ctrl_get(&now);

    if (TIME_CTRL_ERR_BUSY == err) {
        /*
         * BUSY は「RTC ロックを TIME_CTRL_LOCK_TIMEOUT_MS = 1000 ms 待って取れ
         * なかった」を意味する（`time_ctrl.c:101,517-521`）。正常時のロック保持は
         * 0.2 ms 以下（`doc/design/issue-212.md` §1）なので、競合では BUSY になり
         * えない。実際に起きるのは、保持タスクがサブクロック喪失で FSP の
         * `FSP_HARDWARE_REGISTER_WAIT`（タイムアウト無し）から戻らなくなった場合で、
         * これには復旧経路が無い。
         *
         * 放置すると本コールバックが毎秒 1000 ms、**`lv_lock()` を握ったまま**
         * ブロックし、描画スレッドを含む LVGL 全体が止まる。したがって連続 BUSY を
         * 検出したらタイマを停止し、更新を恒久的にあきらめる（表示は "--:--" のまま）。
         *
         * 停止には `lv_timer_delete()` ではなく `lv_timer_pause()` を使う。
         * 自タイマの削除自体は LVGL 側で安全に扱われる（`lv_timer.c:342`）が、
         * pause なら `s_timer` が有効なまま残るので `ui_datetime_init()` の
         * 多重登録ガードが効き続ける。停止したタイマは次回起動時刻の計算からも
         * 除外される（`lv_timer.c:118`）。
         */
        if (s_busy_count < UI_DATETIME_BUSY_GIVEUP) {
            s_busy_count++;
        }
        if (s_busy_count >= UI_DATETIME_BUSY_GIVEUP) {
            lv_timer_pause(timer);
        }
    }
    else {
        s_busy_count = 0;
    }

    if (TIME_CTRL_OK != err) {
        /* NOT_INIT（`time_ctrl_init()` 前）/ NOT_SET（`time set` 未実行）/
         * HW（RTC の値が壊れている）/ BUSY のいずれも、時刻として表示できない。 */
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

    /* `time_ctrl_get()` は成功時に year 2000-2099 / mon 1-12 / mday 1-31 /
     * hour 0-23 / min 0-59 を保証している（`time_ctrl.c:288-294` の
     * `time_is_valid()`）ので、書式は必ず 16 文字に収まる。 */
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
 *          を呼ぶと、時刻未設定のあいだステータスバーを無駄に再描画し続けるため。
 */
static void ui_datetime_show_unset(void)
{
    if (!s_shown_valid) {
        /* 初期状態（ラベル生成直後）と giveup 後がここに該当する。 */
        return;
    }

    ui_main_screen_set_datetime(UI_DATETIME_UNSET_TEXT);
    s_shown_valid = false;
}
