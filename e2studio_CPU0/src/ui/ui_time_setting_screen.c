/**
 * @file ui_time_setting_screen.c
 * @brief 時刻設定画面の実装（S-012-4 / Issue #214）
 * @details
 * `lv_roller` 5 本（年 / 月 / 日 / 時 / 分）と OK / Cancel ボタンを持つ画面。
 * OK は `time_cache_set_request()` で要求を出すだけで戻り、結果は `lv_timer` で
 * ポーリングして受け取る。**RTC には一切触れない**（理由は `ui_time_setting_screen.h`）。
 *
 * 設計メモ: `doc/design/issue-214.md`
 *
 * @note This file is part of the time setting implementation (S-012-4).
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ui_time_setting_screen.h"
#include "ui_main_screen.h"     /* UI_DISPLAY_WIDTH / UI_DISPLAY_HEIGHT */
#include "ui_datetime.h"

#include "../time_cache.h"
#include "../time_ctrl.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** ack ポーリング周期 [ms]。OK 押下から画面復帰までの体感遅延の主成分 */
#define UI_TIME_SETTING_ACK_POLL_MS     (50)

/**
 * 結果未確認と表示して Back を許可するまでの時間 [ms]。
 * 要求を破棄したり再送可能にしたりする期限ではない。
 * FSP が高優先度タスクで busy wait する場合、このタイマが走る保証はない。
 */
#define UI_TIME_SETTING_ACK_TIMEOUT_MS  (3000U)

/** 時刻が取得できないときにロータへ入れる初期値 */
#define UI_TIME_SETTING_DEF_YEAR        (2026U)
#define UI_TIME_SETTING_DEF_MON         (1U)
#define UI_TIME_SETTING_DEF_MDAY        (1U)

/**
 * ロータの選択肢文字列のバッファサイズ [byte]
 *
 * @details "\n" 区切り ＋ 終端でちょうど収まる大きさ。最大は年の
 *          100 件 × 4 桁 ＋ 区切り 99 ＋ 終端 1 = 500 byte。
 *
 * @note `lv_roller_set_options()` は渡された文字列を**内部にコピーする**
 *       （`LV_ROLLER_MODE_NORMAL` では `lv_label_set_text()` に渡すだけで、
 *       ラベルが自前に複製する）。したがって 1 本のバッファを 5 つのロータで
 *       使い回してよく、静的に持つ必要も無い。
 */
#define UI_TIME_SETTING_OPT_BUF_SIZE    (((TIME_CTRL_YEAR_MAX - TIME_CTRL_YEAR_MIN) + 1U) * 5U)

/** 日ロータの選択肢バッファサイズ [byte]（31 件 × 2 桁 ＋ 区切り 30 ＋ 終端 1） */
#define UI_TIME_SETTING_DAY_OPT_SIZE    (31U * 3U)

/** 画面レイアウト（1024x600 に手で配置する。LV_USE_FLEX / LV_USE_GRID は 0） */
#define UI_TIME_SETTING_TITLE_Y         (24)
#define UI_TIME_SETTING_HEADER_Y        (96)
#define UI_TIME_SETTING_ROLLER_Y        (128)
#define UI_TIME_SETTING_MSG_Y           (400)
#define UI_TIME_SETTING_BTN_Y           (464)

#define UI_TIME_SETTING_YEAR_W          (150)
#define UI_TIME_SETTING_FIELD_W         (110)
#define UI_TIME_SETTING_FIELD_GAP       (20)
#define UI_TIME_SETTING_ROLLER_ROWS     (3)

#define UI_TIME_SETTING_BTN_W           (180)
#define UI_TIME_SETTING_BTN_H           (64)
#define UI_TIME_SETTING_BTN_GAP         (80)

/** 配色（ステータスバーと同系統でまとめる） */
#define UI_TIME_SETTING_BG_COLOR        lv_color_make(0x1A, 0x23, 0x2F)
#define UI_TIME_SETTING_PANEL_COLOR     lv_color_make(0x2C, 0x3E, 0x50)
#define UI_TIME_SETTING_SEL_COLOR       lv_color_make(0x1A, 0x6E, 0xB8)
#define UI_TIME_SETTING_TEXT_COLOR      lv_color_white()
#define UI_TIME_SETTING_HEADER_COLOR    lv_color_make(0x9E, 0xB0, 0xC0)
#define UI_TIME_SETTING_OK_COLOR        lv_color_make(0x1E, 0x7E, 0x34)
#define UI_TIME_SETTING_CANCEL_COLOR    lv_color_make(0x6C, 0x75, 0x7D)
#define UI_TIME_SETTING_ERR_COLOR       lv_color_make(0xE7, 0x4C, 0x3C)

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/*
 * 本モジュールの状態はすべて `lvgl_task`（`ui_time_setting_screen_open()` と
 * `lv_timer_handler()` 経由のコールバック）だけが読み書きする。他タスクも ISR も
 * 触らないので排他は持たない。
 */

/** 画面本体。生成済みかの判定にも使う（1 度だけ生成して再利用する） */
static lv_obj_t   *s_screen = NULL;

/** 戻り先の画面（open 時の `lv_screen_active()`） */
static lv_obj_t   *s_return_screen = NULL;

/** 年 / 月 / 日 / 時 / 分 のロータ */
static lv_obj_t   *s_roller_year = NULL;
static lv_obj_t   *s_roller_mon  = NULL;
static lv_obj_t   *s_roller_mday = NULL;
static lv_obj_t   *s_roller_hour = NULL;
static lv_obj_t   *s_roller_min  = NULL;

/** OK / Cancel ボタンと、状態・エラーを出すメッセージラベル */
static lv_obj_t   *s_ok_btn      = NULL;
static lv_obj_t   *s_cancel_btn  = NULL;
static lv_obj_t   *s_msg_label   = NULL;
static lv_obj_t   *s_cancel_label = NULL;
static lv_obj_t   *s_input_cover = NULL;

/** ack ポーリング用タイマ。要求を出している間だけ resume する */
static lv_timer_t *s_ack_timer = NULL;

/** 待っている要求の番号（`time_cache_set_request()` が返した値） */
static uint32_t    s_pending_seq = 0;

/** 要求を出してからの経過時間 [ms]（`UI_TIME_SETTING_ACK_TIMEOUT_MS` と比較する） */
static uint32_t    s_pending_started = 0;
static bool        s_pending = false;
static bool        s_unconfirmed = false;
/* A completion while hidden is reported once on the next entry. */
static const char *s_completion_notice = NULL;
static bool        s_completion_error = false;

/** 日ロータに現在入っている選択肢の日数。作り直しの要否判定に使う */
static uint8_t     s_mday_opt_cnt = 0;

/**********************************************************************************************************************
 Private (static) function prototypes
 *********************************************************************************************************************/
static bool      ui_time_setting_build(void);
static lv_obj_t *ui_time_setting_create_roller(lv_obj_t *parent, int32_t x, int32_t w,
                                               const char *header, const char *options);
static lv_obj_t *ui_time_setting_create_button(lv_obj_t *parent, int32_t x, const char *text,
                                               lv_color_t color, lv_event_cb_t cb);
static void      ui_time_setting_build_numbers(char *p_buf, size_t buf_size,
                                               uint32_t first, uint32_t last, bool four_digits);
static uint8_t   ui_time_setting_days_in_month(uint16_t year, uint8_t mon);
static void      ui_time_setting_load_time(const time_ctrl_time_t *p_time);
static void      ui_time_setting_refresh_mday(void);
static void      ui_time_setting_set_busy(bool busy);
static void      ui_time_setting_show_msg(const char *text, bool is_error);
static void      ui_time_setting_close(void);
static void      ui_time_setting_ok_cb(lv_event_t *e);
static void      ui_time_setting_cancel_cb(lv_event_t *e);
static void      ui_time_setting_date_changed_cb(lv_event_t *e);
static void      ui_time_setting_ack_timer_cb(lv_timer_t *timer);
static const char *ui_time_setting_err_text(time_ctrl_err_t err);

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

void ui_time_setting_screen_open(void)
{
    time_ctrl_time_t now;

    if ((s_screen != NULL) && (lv_screen_active() == s_screen)) {
        return;
    }

    if (s_screen == NULL) {
        if (!ui_time_setting_build()) {
            /* 生成に失敗した。元の画面のままにする（遷移しない方が安全）。 */
            return;
        }
    }

    s_return_screen = lv_screen_active();
    if (s_pending) {
        /* Preserve the submitted selection and its timer across Back/re-entry. */
        lv_screen_load(s_screen);
        return;
    }

    /* 開いた時点の現在時刻を初期値にする。`time_cache_get()` はノンブロッキングで、
     * `lv_lock()` を保持したこの区間から呼んでよい（`time_cache.h`）。 */
    memset(&now, 0, sizeof(now));
    bool valid = time_cache_get(&now);
    if (!valid) {
        /* 時刻未設定 / RTC 異常 / キャッシュ陳腐化。既定値を入れて初回設定を可能にする。 */
        now.year = (uint16_t)UI_TIME_SETTING_DEF_YEAR;
        now.mon  = (uint8_t)UI_TIME_SETTING_DEF_MON;
        now.mday = (uint8_t)UI_TIME_SETTING_DEF_MDAY;
        now.hour = 0U;
        now.min  = 0U;
    }
    ui_time_setting_load_time(&now);

    /* 前回の表示が残らないように、メッセージとボタン状態を初期化する。 */
    ui_time_setting_show_msg(valid ? "" : "Current time unavailable; choose date/time", !valid);
    if (s_completion_notice != NULL) {
        ui_time_setting_show_msg(s_completion_notice, s_completion_error);
        s_completion_notice = NULL;
    }
    ui_time_setting_set_busy(false);

    /* 戻り先を覚えてから切り替える。`ui_main_screen` を参照しないための仕掛け。 */
    lv_screen_load(s_screen);
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * 画面とウィジェットを生成する（初回 open 時に 1 度だけ）
 *
 * @retval true  生成できた
 * @retval false 画面またはいずれかのウィジェットの生成に失敗した
 */
static bool ui_time_setting_build(void)
{
    /* 5 本のロータで使い回す（`lv_roller_set_options()` がコピーするため）。
     * 本関数は LVGL のイベントコールバックから呼ばれる＝`lvgl_task` のスタック
     * （8192 byte, `usermain.c`）を消費するので、5 本ぶんを同時に積まない。 */
    char    options[UI_TIME_SETTING_OPT_BUF_SIZE];
    int32_t x;

    s_screen = lv_obj_create(NULL);
    if (s_screen == NULL) {
        goto fail;
    }

    lv_obj_set_style_bg_color(s_screen, UI_TIME_SETTING_BG_COLOR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* タイトル */
    {
        lv_obj_t *title = lv_label_create(s_screen);
        if (title == NULL) {
            goto fail;
        }
        lv_label_set_text(title, "Set Date & Time");
        lv_obj_set_style_text_color(title, UI_TIME_SETTING_TEXT_COLOR, LV_PART_MAIN);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_20, LV_PART_MAIN);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, UI_TIME_SETTING_TITLE_Y);
    }

    /* ロータ 5 本。年だけ 4 桁ぶん広くする。
     * 合計幅 = 150 + 110*4 + 20*4 = 670 を画面中央に置く。
     * 日は 31 日で作っておき、`ui_time_setting_load_time()` が実際の年月に合わせて直す。 */
    x = (UI_DISPLAY_WIDTH
         - (UI_TIME_SETTING_YEAR_W + (UI_TIME_SETTING_FIELD_W * 4)
            + (UI_TIME_SETTING_FIELD_GAP * 4))) / 2;

    ui_time_setting_build_numbers(options, sizeof(options),
                                  TIME_CTRL_YEAR_MIN, TIME_CTRL_YEAR_MAX, true);
    s_roller_year = ui_time_setting_create_roller(s_screen, x, UI_TIME_SETTING_YEAR_W,
                                                  "Year", options);
    x += UI_TIME_SETTING_YEAR_W + UI_TIME_SETTING_FIELD_GAP;

    ui_time_setting_build_numbers(options, sizeof(options), 1U, 12U, false);
    s_roller_mon = ui_time_setting_create_roller(s_screen, x, UI_TIME_SETTING_FIELD_W,
                                                 "Month", options);
    x += UI_TIME_SETTING_FIELD_W + UI_TIME_SETTING_FIELD_GAP;

    ui_time_setting_build_numbers(options, sizeof(options), 1U, 31U, false);
    s_roller_mday = ui_time_setting_create_roller(s_screen, x, UI_TIME_SETTING_FIELD_W,
                                                  "Day", options);
    s_mday_opt_cnt = 31U;
    x += UI_TIME_SETTING_FIELD_W + UI_TIME_SETTING_FIELD_GAP;

    ui_time_setting_build_numbers(options, sizeof(options), 0U, 23U, false);
    s_roller_hour = ui_time_setting_create_roller(s_screen, x, UI_TIME_SETTING_FIELD_W,
                                                  "Hour", options);
    x += UI_TIME_SETTING_FIELD_W + UI_TIME_SETTING_FIELD_GAP;

    ui_time_setting_build_numbers(options, sizeof(options), 0U, 59U, false);
    s_roller_min = ui_time_setting_create_roller(s_screen, x, UI_TIME_SETTING_FIELD_W,
                                                 "Min", options);

    if ((s_roller_year == NULL) || (s_roller_mon == NULL) || (s_roller_mday == NULL)
        || (s_roller_hour == NULL) || (s_roller_min == NULL)) {
        goto fail;
    }

    /* 年 / 月 が変わったら日の選択肢を作り直す（うるう年・月ごとの日数）。 */
    lv_obj_add_event_cb(s_roller_year, ui_time_setting_date_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_roller_mon, ui_time_setting_date_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* メッセージラベル（"Setting..." / エラー文言） */
    s_msg_label = lv_label_create(s_screen);
    if (s_msg_label == NULL) {
        goto fail;
    }
    lv_label_set_text(s_msg_label, "");
    lv_obj_set_style_text_color(s_msg_label, UI_TIME_SETTING_TEXT_COLOR, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_msg_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(s_msg_label, LV_ALIGN_TOP_MID, 0, UI_TIME_SETTING_MSG_Y);

    /* OK / Cancel */
    s_ok_btn = ui_time_setting_create_button(
        s_screen,
        -((UI_TIME_SETTING_BTN_W + UI_TIME_SETTING_BTN_GAP) / 2),
        "OK", UI_TIME_SETTING_OK_COLOR, ui_time_setting_ok_cb);

    s_cancel_btn = ui_time_setting_create_button(
        s_screen,
        (UI_TIME_SETTING_BTN_W + UI_TIME_SETTING_BTN_GAP) / 2,
        "Cancel", UI_TIME_SETTING_CANCEL_COLOR, ui_time_setting_cancel_cb);

    if ((s_ok_btn == NULL) || (s_cancel_btn == NULL)) {
        goto fail;
    }
    s_cancel_label = lv_obj_get_child(s_cancel_btn, 0);

    /* Topmost clickable cover blocks roller touch/scroll while a request is pending.
     * It excludes the message and buttons. DISABLED alone does not block scrolling. */
    s_input_cover = lv_obj_create(s_screen);
    if (s_input_cover == NULL) {
        goto fail;
    }
    lv_obj_set_pos(s_input_cover, 0, UI_TIME_SETTING_HEADER_Y);
    lv_obj_set_size(s_input_cover, UI_DISPLAY_WIDTH,
                    UI_TIME_SETTING_MSG_Y - UI_TIME_SETTING_HEADER_Y);
    lv_obj_remove_flag(s_input_cover, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_input_cover, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(s_input_cover, UI_TIME_SETTING_BG_COLOR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_input_cover, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_input_cover, 0, LV_PART_MAIN);

    /* ack ポーリングタイマ。要求を出している間だけ動かすので、作ってすぐ止める。 */
    s_ack_timer = lv_timer_create(ui_time_setting_ack_timer_cb,
                                  UI_TIME_SETTING_ACK_POLL_MS,
                                  NULL);
    if (s_ack_timer == NULL) {
        goto fail;
    }
    lv_timer_pause(s_ack_timer);

    return true;

fail:
    /* Deleting the root recursively releases all partially built children. */
    if (s_screen != NULL) {
        lv_obj_delete(s_screen);
    }
    s_screen = NULL;
    s_roller_year = s_roller_mon = s_roller_mday = NULL;
    s_roller_hour = s_roller_min = NULL;
    s_ok_btn = s_cancel_btn = s_msg_label = NULL;
    s_cancel_label = s_input_cover = NULL;
    s_mday_opt_cnt = 0;
    return false;
}

/**
 * 見出しラベル付きのロータを 1 本作る
 *
 * @param parent  親（画面）
 * @param x       左端の X 座標（画面左上原点）
 * @param w       幅
 * @param header  ロータ上に出す見出し
 * @param options `lv_roller_set_options()` へ渡す "\n" 区切りの選択肢
 *
 * @return 生成したロータ。失敗時は NULL
 */
static lv_obj_t *ui_time_setting_create_roller(lv_obj_t *parent, int32_t x, int32_t w,
                                              const char *header, const char *options)
{
    lv_obj_t *label;
    lv_obj_t *roller;

    label = lv_label_create(parent);
    if (label == NULL) {
        return NULL;
    }
    lv_label_set_text(label, header);
    lv_obj_set_style_text_color(label, UI_TIME_SETTING_HEADER_COLOR, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_pos(label, x, UI_TIME_SETTING_HEADER_Y);
    lv_obj_set_width(label, w);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    roller = lv_roller_create(parent);
    if (roller == NULL) {
        return NULL;
    }

    /* LV_ROLLER_MODE_NORMAL: 端で止まる。INFINITE は端の巻き込みで日付が
     * 分かりにくくなるうえ、選択肢を複製するために lv_malloc() を使う。 */
    lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);

    lv_obj_set_width(roller, w);
    lv_obj_set_pos(roller, x, UI_TIME_SETTING_ROLLER_Y);

    lv_obj_set_style_bg_color(roller, UI_TIME_SETTING_PANEL_COLOR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(roller, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(roller, 6, LV_PART_MAIN);
    lv_obj_set_style_text_color(roller, UI_TIME_SETTING_TEXT_COLOR, LV_PART_MAIN);
    lv_obj_set_style_text_font(roller, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_align(roller, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    /* Row height is calculated from the selected font. */
    lv_roller_set_visible_row_count(roller, UI_TIME_SETTING_ROLLER_ROWS);

    /* 選択中の行を目立たせる */
    lv_obj_set_style_bg_color(roller, UI_TIME_SETTING_SEL_COLOR, LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_color(roller, UI_TIME_SETTING_TEXT_COLOR, LV_PART_SELECTED);

    return roller;
}

/**
 * OK / Cancel ボタンを 1 個作る
 *
 * @param parent 親（画面）
 * @param x      画面中央からの X オフセット
 * @param text   ボタンのラベル
 * @param color  背景色
 * @param cb     `LV_EVENT_CLICKED` のハンドラ
 *
 * @return 生成したボタン。失敗時は NULL
 */
static lv_obj_t *ui_time_setting_create_button(lv_obj_t *parent, int32_t x, const char *text,
                                              lv_color_t color, lv_event_cb_t cb)
{
    lv_obj_t *btn;
    lv_obj_t *label;

    btn = lv_button_create(parent);
    if (btn == NULL) {
        return NULL;
    }

    lv_obj_set_size(btn, UI_TIME_SETTING_BTN_W, UI_TIME_SETTING_BTN_H);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, x, UI_TIME_SETTING_BTN_Y);

    lv_obj_set_style_bg_color(btn, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);

    label = lv_label_create(btn);
    if (label == NULL) {
        return NULL;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, UI_TIME_SETTING_TEXT_COLOR, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(label);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    return btn;
}

/**
 * 連番の選択肢文字列を組み立てる
 *
 * @details `"01\n02\n...\n12"` のように "\n" で区切る。バッファに収まらない場合は
 *          そこで打ち切る（呼び出し側のサイズ定義と範囲が矛盾している場合のみ起きる）。
 *
 * @param[out] p_buf       格納先
 * @param      buf_size    `p_buf` のサイズ [byte]
 * @param      first       最初の値
 * @param      last        最後の値（`first` 以上であること）
 * @param      four_digits true なら "%04u"、false なら "%02u" で書式化する
 */
static void ui_time_setting_build_numbers(char *p_buf, size_t buf_size,
                                          uint32_t first, uint32_t last, bool four_digits)
{
    size_t   len = 0U;
    uint32_t v;

    if ((NULL == p_buf) || (0U == buf_size)) {
        return;
    }
    p_buf[0] = '\0';

    for (v = first; v <= last; v++) {
        int n;

        if (four_digits) {
            n = snprintf(&p_buf[len], buf_size - len, "%s%04u",
                         (v == first) ? "" : "\n", (unsigned int)v);
        } else {
            n = snprintf(&p_buf[len], buf_size - len, "%s%02u",
                         (v == first) ? "" : "\n", (unsigned int)v);
        }

        if ((n <= 0) || ((size_t)n >= (buf_size - len))) {
            break;      /* 収まらなかった。ここまでの内容は終端済み。 */
        }
        len += (size_t)n;
    }
}

/**
 * その年月の日数を返す（うるう年を考慮する）
 *
 * @param year 西暦
 * @param mon  月（1-12）
 *
 * @return 日数（28-31）。`mon` が範囲外なら 31
 */
static uint8_t ui_time_setting_days_in_month(uint16_t year, uint8_t mon)
{
    static const uint8_t days[12] = { 31U, 28U, 31U, 30U, 31U, 30U,
                                      31U, 31U, 30U, 31U, 30U, 31U };

    if ((mon < 1U) || (mon > 12U)) {
        return 31U;
    }

    if (2U == mon) {
        /* グレゴリオ暦のうるう年判定。設定範囲は 2000-2099 なので 2000 年
         * （400 で割り切れる）だけが 100 年ルールの例外に当たる。 */
        bool leap = ((0U == (year % 4U)) && ((0U != (year % 100U)) || (0U == (year % 400U))));
        return leap ? 29U : 28U;
    }

    return days[mon - 1U];
}

/**
 * 各ロータの選択位置を指定時刻に合わせる
 *
 * @param p_time 反映する時刻（`sec` / `wday` は使わない）
 */
static void ui_time_setting_load_time(const time_ctrl_time_t *p_time)
{
    uint16_t year = p_time->year;
    uint8_t  mon  = p_time->mon;
    uint8_t  mday = p_time->mday;

    /* `time_cache_get()` が返す値は `time_is_valid()` を通っているが
     * （`time_ctrl.c:288-294`）、既定値経路も含めてここで範囲を締めておく。 */
    if ((year < TIME_CTRL_YEAR_MIN) || (year > TIME_CTRL_YEAR_MAX)) {
        year = (uint16_t)UI_TIME_SETTING_DEF_YEAR;
    }
    if ((mon < 1U) || (mon > 12U)) {
        mon = (uint8_t)UI_TIME_SETTING_DEF_MON;
    }

    lv_roller_set_selected(s_roller_year, (uint32_t)(year - TIME_CTRL_YEAR_MIN), LV_ANIM_OFF);
    lv_roller_set_selected(s_roller_mon, (uint32_t)(mon - 1U), LV_ANIM_OFF);

    /* 日の選択肢を年月に合わせてから日を選ぶ（順序を逆にすると範囲外になる）。
     * `lv_roller_set_selected()` は内部状態を更新して再配置するだけで
     * `LV_EVENT_VALUE_CHANGED` を送らない（`lv_roller.c` の同関数）。つまり上の 2 行では
     * `ui_time_setting_date_changed_cb()` が走らないので、ここで明示的に呼ぶ必要がある。 */
    ui_time_setting_refresh_mday();

    if (mday < 1U) {
        mday = 1U;
    }
    if (mday > s_mday_opt_cnt) {
        mday = s_mday_opt_cnt;
    }
    lv_roller_set_selected(s_roller_mday, (uint32_t)(mday - 1U), LV_ANIM_OFF);

    lv_roller_set_selected(s_roller_hour, (uint32_t)p_time->hour, LV_ANIM_OFF);
    lv_roller_set_selected(s_roller_min, (uint32_t)p_time->min, LV_ANIM_OFF);
}

/**
 * 日ロータの選択肢を、現在選択中の年月の日数に合わせて作り直す
 *
 * @details 日数が変わらない場合は何もしない（`lv_roller_set_options()` は選択位置を
 *          0 にリセットするので、無用に呼ぶと選択中の日が飛ぶ）。日数が減って
 *          選択中の日が範囲外になる場合は末日へクランプする。
 *          これにより **2 月 30 日のような日付は選択できない**。
 */
static void ui_time_setting_refresh_mday(void)
{
    char     options[UI_TIME_SETTING_DAY_OPT_SIZE];
    uint16_t year;
    uint8_t  mon;
    uint8_t  days;
    uint32_t sel;

    year = (uint16_t)(TIME_CTRL_YEAR_MIN + lv_roller_get_selected(s_roller_year));
    mon  = (uint8_t)(lv_roller_get_selected(s_roller_mon) + 1U);
    days = ui_time_setting_days_in_month(year, mon);

    if (days == s_mday_opt_cnt) {
        return;
    }

    /* `lv_roller_set_options()` は先頭で選択位置を 0 に戻すので、先に控えて入れ直す。 */
    sel = lv_roller_get_selected(s_roller_mday);
    if (sel >= days) {
        sel = (uint32_t)(days - 1U);
    }

    ui_time_setting_build_numbers(options, sizeof(options), 1U, days, false);
    lv_roller_set_options(s_roller_mday, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(s_roller_mday, sel, LV_ANIM_OFF);

    s_mday_opt_cnt = days;
}

/** Block editing and duplicate requests while retaining a Back action after 3 s. */
static void ui_time_setting_set_busy(bool busy)
{
    if (busy) {
        lv_obj_add_state(s_ok_btn, LV_STATE_DISABLED);
        lv_obj_add_state(s_cancel_btn, LV_STATE_DISABLED);
        lv_obj_remove_flag(s_input_cover, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_state(s_ok_btn, LV_STATE_DISABLED);
        lv_obj_remove_state(s_cancel_btn, LV_STATE_DISABLED);
        lv_obj_add_flag(s_input_cover, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(s_cancel_label, "Cancel");
}

/**
 * メッセージラベルを更新する
 *
 * @param text     表示する文字列（空文字なら消える）
 * @param is_error true なら赤、false なら白で表示する
 */
static void ui_time_setting_show_msg(const char *text, bool is_error)
{
    if (s_msg_label == NULL) {
        return;
    }

    lv_obj_set_style_text_color(s_msg_label,
                                is_error ? UI_TIME_SETTING_ERR_COLOR : UI_TIME_SETTING_TEXT_COLOR,
                                LV_PART_MAIN);
    lv_label_set_text(s_msg_label, text);
    lv_obj_align(s_msg_label, LV_ALIGN_TOP_MID, 0, UI_TIME_SETTING_MSG_Y);
}

/**
 * 画面を閉じて呼び出し元の画面へ戻る
 *
 * @details 未完了要求の監視は画面を離れても継続する。
 */
static void ui_time_setting_close(void)
{
    if ((s_return_screen != NULL) && (lv_screen_active() == s_screen)) {
        lv_screen_load(s_return_screen);
    }
}

/**
 * OK ボタンのハンドラ
 *
 * @details ロータの選択値から時刻を組み立て、`time_cache_set_request()` で要求を出す。
 *          **ここで `time_ctrl_set()` を呼んではならない**（`ui_time_setting_screen.h`）。
 *          結果は `ui_time_setting_ack_timer_cb()` が受け取る。
 *
 * @param e LVGL イベント（未使用）
 */
static void ui_time_setting_ok_cb(lv_event_t *e)
{
    time_ctrl_time_t req;
    uint32_t         seq = 0;

    (void)e;

    if (s_pending) {
        return;
    }
    memset(&req, 0, sizeof(req));
    req.year = (uint16_t)(TIME_CTRL_YEAR_MIN + lv_roller_get_selected(s_roller_year));
    req.mon  = (uint8_t)(lv_roller_get_selected(s_roller_mon) + 1U);
    req.mday = (uint8_t)(lv_roller_get_selected(s_roller_mday) + 1U);
    req.hour = (uint8_t)lv_roller_get_selected(s_roller_hour);
    req.min  = (uint8_t)lv_roller_get_selected(s_roller_min);
    /* 秒は画面で扱わない（Issue #214 の作業内容どおり）。0 秒に合わせる。 */
    req.sec  = 0U;
    /* `wday` は `time_ctrl_set()` が年月日から算出するので設定しない（`time_ctrl.c:319`）。 */

    if (!time_cache_set_request(&req, &seq)) {
        ui_time_setting_show_msg("Time service unavailable or busy", true);
        return;
    }

    s_pending_seq = seq;
    s_pending = true;
    s_unconfirmed = false;
    s_pending_started = lv_tick_get();

    ui_time_setting_set_busy(true);
    ui_time_setting_show_msg("Setting...", false);

    if (s_ack_timer != NULL) {
        lv_timer_resume(s_ack_timer);
    }
}

/**
 * Cancel ボタンのハンドラ
 *
 * @details Cancel は発行前のみ。Back は要求を取り消さずに画面を離れる。
 *
 * @param e LVGL イベント（未使用）
 */
static void ui_time_setting_cancel_cb(lv_event_t *e)
{
    (void)e;

    if (!s_pending || s_unconfirmed) {
        ui_time_setting_close();
    }
}

/**
 * 年 / 月 ロータの `LV_EVENT_VALUE_CHANGED` ハンドラ
 *
 * @param e LVGL イベント（未使用）
 */
static void ui_time_setting_date_changed_cb(lv_event_t *e)
{
    (void)e;

    ui_time_setting_refresh_mday();
}

/**
 * ack ポーリングタイマのコールバック
 *
 * @details `lv_timer_handler()` から `lv_lock()` を保持した区間で呼ばれる
 *          （`lv_timer.c:81,144,327`）。`time_cache_set_result()` は
 *          `tk_dis_dsp()` 区間のコピーだけで待たないので、ここから呼んでよい。
 *
 *          未確認期限後も監視を継続し、結果が確定するまで再送しない。
 *
 * @param timer 本コールバックを登録したタイマ（未使用）
 */
static void ui_time_setting_ack_timer_cb(lv_timer_t *timer)
{
    time_ctrl_err_t err = TIME_CTRL_OK;
    time_ctrl_err_t read_err = TIME_CTRL_OK;

    (void)timer;

    if (!s_pending) {
        return;
    }
    if (!time_cache_set_result(s_pending_seq, &err, &read_err)) {
        if (!s_unconfirmed &&
            (lv_tick_elaps(s_pending_started) >= UI_TIME_SETTING_ACK_TIMEOUT_MS)) {
            s_unconfirmed = true;
            lv_label_set_text(s_cancel_label, "Back");
            lv_obj_remove_state(s_cancel_btn, LV_STATE_DISABLED);
            ui_time_setting_show_msg("Result unconfirmed; Back does not cancel", true);
        }
        return;
    }

    lv_timer_pause(s_ack_timer);
    s_pending = false;
    s_unconfirmed = false;
    ui_time_setting_set_busy(false);

    if (TIME_CTRL_OK != err) {
        if (lv_screen_active() != s_screen) {
            s_completion_notice = ui_time_setting_err_text(err);
            s_completion_error = true;
        }
        ui_time_setting_show_msg(ui_time_setting_err_text(err), true);
        return;
    }

    ui_datetime_refresh();
    if (lv_screen_active() != s_screen) {
        s_completion_notice = (read_err == TIME_CTRL_OK)
                              ? "Time set" : "Time set; readback unavailable";
        s_completion_error = (read_err != TIME_CTRL_OK);
    }
    if (read_err != TIME_CTRL_OK) {
        ui_time_setting_show_msg("Time set; readback unavailable", true);
        return;
    }
    ui_time_setting_close();
}

/**
 * `time_ctrl_err_t` を画面表示用の文言へ変換する
 *
 * @param err 設定結果
 *
 * @return 表示する文字列
 */
static const char *ui_time_setting_err_text(time_ctrl_err_t err)
{
    switch (err) {
        case TIME_CTRL_ERR_INVALID_ARG:
            /* ロータ側で日付を制限しているので通常は出ない。出た場合は
             * 選択肢の作り直しに漏れがある。 */
            return "Invalid date/time";
        case TIME_CTRL_ERR_NOT_INIT:
            return "RTC not initialized";
        case TIME_CTRL_ERR_BUSY:
            return "RTC busy - try again";
        case TIME_CTRL_ERR_HW:
            return "RTC access failed";
        case TIME_CTRL_ERR_NOT_SET:
        case TIME_CTRL_OK:
        default:
            return "Failed to set time";
    }
}


