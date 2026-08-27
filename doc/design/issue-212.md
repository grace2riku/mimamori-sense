# Issue #212 設計メモ — 時刻管理モジュール `time_ctrl` と NT-Shell `time` コマンド

対象: `e2studio_CPU0/src/time_ctrl.{c,h}` / `time_cmd.{c,h}` / `usrcmd.c` / `ntshell_thread_entry.c`

## 1. 設計の入力（確定値・根拠付き）

| 項目 | 確定内容 | 根拠 |
|---|---|---|
| 呼び出し元 | `ntshell_task` のみ。`usrcmd_time`（`usrcmd.c` の `cmdlist[]`）と `time_ctrl_init()`（`ntshell_thread_entry.c` の `ntshell_task`）。**間接呼び出しなし**（`time_ctrl_*` を関数ポインタに登録する箇所を作らない。RTC コールバックは `p_callback = NULL`） | `ra_gen/hal_data.c:10-11` |
| 同時呼び出し | 本 Issue の時点では **`ntshell_task` 単独**。#213 完了後に `lvgl_task` が読み手として加わる前提で、最初から排他を入れる。**書き手はタスクのみ**（ISR から `time_ctrl_*` を呼ぶ経路は作らない） | — |
| ブロック許容時間 | `ntshell_task`: 数十 ms 可。`lvgl_task`(#213): 33.3 ms/frame を圧迫しない → 下表のとおり全 API が 1 ms 未満なので、#213 でも `time_ctrl_get()` を直接呼んでよい | — |
| 依存先 API 最悪所要時間 | 下表。**`R_RTC_ClockSourceSet()` のみサブクロック停止時に無限ループ**（`FSP_HARDWARE_REGISTER_WAIT` にタイムアウト無し: `bsp_common.h:122`） | 下表 |
| 失敗の返し方 | `time_ctrl_err_t` で区別する。`NOT_INIT`（未初期化）/ `NOT_SET`（時刻未設定＝計時停止中、ユーザに `time set` を促す）/ `INVALID_ARG`（引数不正）/ `HW`（FSP がエラーを返した）。**「未設定」と「HW 異常」は呼び出し元の次の行動が違う**ので別値 | — |
| 実行コンテキスト制約 | ISR から呼ばない。`R_RTC_CalendarTimeGet()` は carry ISR が走れないと桁上がりを検出できないため **割り込み禁止区間から呼ばない**（`r_rtc.c:421-470` の `do{}while(carry_isr_triggered)`）。本モジュールは資源所有ミューテックスで直列化するため、割り込みもディスパッチも止めない（§5） | `r_rtc.c:421-470` |

### 依存先 API の最悪所要時間（1 カウントソース周期 = 1/32768 s ≒ 30.5 µs）

> **前提**: 以下はいずれも**サブクロックが正常に発振している場合**の値。発振が失われると `FSP_HARDWARE_REGISTER_WAIT`（タイムアウト無し）で**戻らなくなる**。該当箇所の一覧と、それを踏まえた排他方式の決定は §5 を参照。

| API | 内訳 | 最悪 |
|---|---|---|
| `R_RTC_Open()` | `RTC_CFG_OPEN_SET_CLOCK_SOURCE = (0)`（`ra_cfg/fsp_cfg/r_rtc_cfg.h:9`）のため `r_rtc_set_clock_source()` は呼ばれない（`r_rtc.c:240-245`）。実体は `r_rtc_config_rtc_interrupts()` = `R_BSP_IrqCfg()` 1 回のみ。**レジスタ待ちゼロ** | 数 µs |
| `R_RTC_ClockSourceSet()` | `r_rtc_set_clock_source()`（`r_rtc.c:1066-1119`）: `R_BSP_SoftwareDelay(200µs)` ＋ START 同期 ＋ CNTMD 同期 ＋ ソフトウェアリセット ＋ RCR1 反映 ＋ HR24 反映 ＋ TCEN×3（`BSP_FEATURE_RTC_HAS_TCEN=1`）= 200 µs ＋ 8 待ち × 30.5 µs | **≒ 0.45 ms**（SOSC 停止時は**戻らない**） |
| `R_RTC_CalendarTimeSet()` | `start_bit_update(0)` ＋ 7 レジスタ書き込み ＋ `r_rtc_error_adjustment_set()`（AADJE 一致時は RADJ 書き込みの 1 待ちのみ／不一致なら 3 待ち）＋ `start_bit_update(1)` = 最大 5 待ち × 30.5 µs | **≒ 0.16 ms** |
| `R_RTC_CalendarTimeGet()` | carry IRQ 一時有効化（RCR1 反映待ち 1）＋ 読み出しループ（桁上がりは 1 s に 1 回なので実質 2 周）＋ 復元（RCR1 反映待ち 1）= 2 待ち × 30.5 µs ＋ レジスタ読み | **≒ 0.1 ms** |
| `tk_set_utc()` / `tk_get_utc()` | `BEGIN/END_CRITICAL_SECTION` で 64bit 加減算 1 回（`time_calls.c:35-58`） | µs 未満 |

## 2. FSP のパラメータチェックは無効 ― `time_ctrl` が自前で防御する

`BSP_CFG_PARAM_CHECKING_ENABLE = (0)`（`ra_cfg/fsp_cfg/bsp/bsp_cfg.h:33`）→ `r_rtc.c` の
`#if RTC_CFG_PARAM_CHECKING_ENABLE` ブロックは全て消える。よって:

- `r_rtc_time_and_date_validate()` は**存在しない** → `time_ctrl_set()` が月ごとの日数・うるう年・時分秒範囲を自前で検証する
- `FSP_ERR_NOT_OPEN` チェックが無い → `time_ctrl` が `s_initialized` フラグを自前で持つ
- `FSP_ASSERT(NULL != ...)` が無い → 公開 API で引数 NULL を自前で弾く
- `tm_wday` は FSP が導出しない（`r_rtc.c:392` が `RWKCNT` にそのまま書く）→ `time_ctrl_set()` が
  年月日から算出して渡す（1970-01-01 起点の通日を求める `days_from_civil` を曜日算出と
  `tk_set_utc()` 用エポック計算で共用する）
- `tm_mon` は 0 起点、`tm_year` は 1900 起点（`r_rtc.c:396,399` の `+1` / `-RTC_C_TIME_OFFSET`）

**さらに帰結として、本ビルドでは RTC API はエラーを返さない。**
`R_RTC_Open()` / `R_RTC_ClockSourceSet()` / `R_RTC_CalendarTimeSet()` / `R_RTC_CalendarTimeGet()` の
いずれも、`fsp_err_t err = FSP_SUCCESS;` から `return err;` までの間に `err` を書き換える文が
`#if RTC_CFG_PARAM_CHECKING_ENABLE` の**外に無い**（`r_rtc.c:196-253`, `:331-350`, `:362-407`, `:421-470`）。
したがって `time_ctrl` 側の「FSP がエラーを返した」分岐は**防御的コードであり現状は到達しない**。
`TIME_CTRL_ERR_HW` が実際に返るのは、**読み出した BCD 値が日時として不正だった場合のみ**。
この事実は `time_ctrl.h` の `@retval` と `time_ctrl.c` 冒頭にも明記する。

## 3. プロビジョニング判定（最重要論点）

`R_RTC_Open()` はクロック源を触らないので、**コールドスタート時のみ** `R_RTC_ClockSourceSet()` を
呼ぶ必要がある。無条件に呼ぶと `r_rtc_set_clock_source()` が `R_RTC->RCR2 = 0` とソフトウェア
リセットを実行し（`r_rtc.c:1089-1094`）、**電池バックアップ中の計時を毎回壊す**。

**判定式**: `provisioned := (RCR2.START == 1) && (RCR2.HR24 == 1)`

- `START` を 1 にする箇所は `r_rtc.c` 全体で `R_RTC_CalendarTimeSet()` の `:404` **1 箇所のみ**。
  他 3 箇所（`:282` Close / `:382` Set 冒頭 / `:1075` set_clock_source）は全て `0U`
- `HR24` を 1 にするのは `r_rtc_set_clock_source()` の `:1104` のみ
- → `START==1 && HR24==1` は「本 FW がクロック源を設定し、かつ時刻を設定して計時中」と一意に対応する
- VBATT 喪失後は RCR2 = 0x00（両ビット 0）→ 未プロビジョニングと判定される
- **`RCR4`(RCKSEL) は判定に使えない**: `RTC_CLOCK_SOURCE_SUBCLK == 0`（`r_rtc_api.h:72`）で
  リセット値と同値。加えて `R_BSP_Init_RTC()` が起動のたびに `R_RTC->RCR4 = 0` を書く
  （`bsp_clocks.c:3381`。`BSP_PRV_LOCO_USED = 0` ← `ra_gen/bsp_clock_cfg.h` に LOCO 源が一つも無い）
- **`R_BSP_Init_RTC()` は RCR2 を触らない**: RCR2 リセット部は `#if !BSP_CFG_RTC_USED` の内側で、
  `BSP_CFG_RTC_USED = (1)`（`ra_cfg/fsp_cfg/bsp/bsp_cfg.h:24`）のため丸ごと消える（`bsp_clocks.c:3385-3414`）

**「時刻未設定」の判定も同じビット**を使う: `START == 0` ⇒ 計時していない ⇒ `TIME_CTRL_ERR_NOT_SET`。
`R_RTC_ClockSourceSet()` 直後は START = 0 なので、`time_ctrl_set()` が呼ばれるまで「未設定」のまま。
RAM フラグではなく RCR2 を見るので、**リセットをまたいでも判定が正しい**。

## 4. 状態変数と読み書きコンテキスト

| 変数 | 書き手 | 読み手 | 保護 |
|---|---|---|---|
| `s_initialized` (bool) | `ntshell_task`（`time_ctrl_init()` 1 回のみ） | `ntshell_task`、将来 `lvgl_task` | ロック取得**前**に読んで早期 return し、以後はミューテックス区間内で扱う |
| `s_last_err` (fsp_err_t, `time status` 表示用) | `ntshell_task` | 同上 | 同上 |
| RTC レジスタ／`g_rtc_ctrl.carry_isr_triggered` | FSP（`R_RTC_*` 内）／carry ISR | FSP | 資源所有ミューテックスで `R_RTC_*` 呼び出しを直列化（§5） |

`time_ctrl` は ISR と共有する自前の状態を持たない。carry ISR が触るのは FSP 内部の
`carry_isr_triggered` だけで、`time_ctrl` の変数には触れない（`r_rtc.c:1687`）。

## 5. 排他方針

> **改訂（PR #216 レビュー指摘 P1 / 2026-08-27）**: 当初は `tk_dis_dsp()` で囲む方針だったが、
> **サブクロック喪失時に RTC API が戻らない**ことと `tk_dis_dsp()` を組み合わせると
> CPU0 の全タスクが巻き添えで停止するため、資源所有ミューテックスに変更した。
> 経緯は末尾「当初案（`tk_dis_dsp()`）を破棄した理由」を参照。

**`TA_INHERIT` 属性の資源所有ミューテックスで RTC アクセス区間を囲む。**

- 保護対象は「RTC レジスタ列 ＋ `g_rtc_ctrl`（`carry_isr_triggered`, carry IRQ の一時有効化・復元）」
  という**資源**。並行 `R_RTC_CalendarTimeGet()` が互いの carry IRQ 復元を踏むと不整合な時刻を返す。
  要求の順序付けが目的ではないので、CLAUDE.md が「保持したまま呼ぶのが正しい」と定める
  **資源所有ロック**に該当する（`i2c_bus0_lock()` と同じ類型）
- 書き手・読み手とも**タスクのみ**（`ntshell_task`、#213 以降は `lvgl_task` が読み手に加わる）。
  ミューテックスは ISR から取れないので、この制約は機構によって強制される
- ミューテックスはディスパッチを止めない。よって carry ISR も他タスクも区間中に走り続ける
- ロック取得は 1000 ms でタイムアウトさせ、保持タスクがハングしても
  待ち側は `TIME_CTRL_ERR_BUSY` で復帰して自タスクの処理を続けられるようにする
- 生成手順・属性は `i2c_bus0_sync_init()`（`src/port/i2c_bus0.c:78-100`）に合わせる。
  二重生成の防止にのみ `tk_dis_dsp()` を使い、**その区間に RTC アクセスは入れない**
- `time_ctrl_get_status()`（`time status` 用）だけは、ロックが取れなくても
  ロック無しで採取して `lock_busy = true` を返す。RTC がハングしている状況こそ
  状態を見たいため。ここでのレジスタ読みは 1 バイト単位でカウントソース同期の待ちを伴わない

### 当初案（`tk_dis_dsp()`）を破棄した理由

FSP の RTC API は `FSP_HARDWARE_REGISTER_WAIT`（タイムアウト無し、`bsp_common.h:122`）で
カウントソース同期の完了を待つ。§1 の所要時間表は**サブクロックが発振している前提**の値であり、
発振が失われると以下はいずれも**戻らない**:

| API | 無制限待ちの箇所 |
|---|---|
| `R_RTC_ClockSourceSet()` | `r_rtc.c:1043,1057,1091,1100,1109`（START/RESET/CNTMD/RCR1/HR24） |
| `R_RTC_CalendarTimeSet()` | `r_rtc.c:1043`（START ×2）、`:1589,1593,1607,1625`（誤差調整） |
| `R_RTC_CalendarTimeGet()` | `r_rtc.c:1169,1176`（RCR1 の CIE 更新） |

当初案は「戻らない」ケースを `R_RTC_ClockSourceSet()` についてのみ認識しており、
**被害範囲**を評価していなかった。`tk_dis_dsp()` 区間内でハングするとディスパッチが戻らず、
`camera_task` / `lvgl_task` / `audio_task` / `alarm_task` / `blink_task` まで停止する。
ミューテックスなら被害は呼び出しタスク 1 本に閉じ込められる。

なお、どちらの方式でもサブクロック喪失時に RTC が使えないこと自体は変わらない
（FSP にタイムアウトが無い以上、`ra/` を編集せずには回避できない）。
変わるのは**被害の封じ込め**である。

### そのほかに捨てた代替案

- **単一所有タスクへの集約（`time_task` 新設）**: CLAUDE.md の既定形は「ブロックしうるデバイス操作を
  所有タスクに集約する」だが、本件で必要なのは**資源の直列化**であってデバイス状態の reconcile ではない
  （宣言すべき「あるべき状態」が無く、`time_ctrl_get()` は本質的に同期的な問い合わせ）。
  タスク 1 本とイベントフラグ・要求バッファを足すコストに見合わず、
  「要求されていない汎用性」に当たるため却下。
  ハング封じ込めの観点でも、所有タスクがハングすれば要求元は結局待たされるので、
  ミューテックスのタイムアウトと比べて優位性が無い
- **RAM キャッシュ＋周期ハンドラで更新**: 読み出しが 0.1 ms と十分速く、キャッシュの鮮度管理が
  純増になるため却下。`tk_set_utc()` 同期があるので、将来キャッシュが要る利用者は `tk_get_utc()` を使える

## 6. `time_ctrl_init()` の呼び出し位置

**`ntshell_task`（`ntshell_thread_entry.c`）のバナー・Dave2D ステータス表示の直後、
`ntshell_init()` の前**に置く。

- 理由 1: `R_RTC_ClockSourceSet()` はサブクロック停止時に**戻らない**（§1）。かつサブクロック
  安定化待ちは本プロジェクトではコンパイル時に消えている（`bsp_clocks.c:3229-3230` の条件が
  `BSP_CFG_CLOCK_SOURCE = PLL1P`（`ra_gen/bsp_clock_cfg.h:28`）と `BSP_PRV_HOCO_USE_FLL = 0` で両方偽）。
  ブート経路の**最後方**に置くことで水晶の立ち上がり時間を最大限稼ぐ
- 理由 2: 切り分け可能性。ここで止まった場合の観測像は「**バナーと Dave2D 行までは出たあと、
  `Time (RTC) : init...` で止まりプロンプトが出ない。一方 blink LED は点滅し続け、
  camera/lvgl/audio タスクは動く**」。バナーが出たあとなのでコンソール自体の障害とも
  区別できる。`usermain()` に置くと以降のタスク生成が全て止まり、原因の切り分けができない
- 理由 3: 呼び出し元が `ntshell_task` だけなので、初期化と利用が同一タスク内で順序保証される
- 進捗ログを `print_to_console()` で前後に出す（`jlink_console_init()` 済みなので SCI8 競合はない）

## 7. タイムゾーンと `tk_set_utc()` 同期

- **JST 固定のローカル時刻**として扱う。UTC オフセットは管理しない。RTC には JST の壁時計時刻を
  そのまま格納する
- `time_ctrl_init()` 成功時と `time_ctrl_set()` 成功時に `tk_set_utc()` を呼び、システム時刻を
  RTC に同期する。以降 #213 等は `tk_get_utc()`（µs 未満）だけで時刻を得られる
- **`tk_get_utc()` が返すのは「JST を UTC とみなした 1970-01-01 起点の ms」**であり、真の UTC では
  ない。この読み替えは `time_ctrl.h` に明記する
- システムタイマ周期 10 ms（`mtk3_bsp2/config/config.h:39`）のため `tk_get_utc()` の分解能は 10 ms。
  秒表示には十分。ただし SysTick 由来のドリフトがあるため、**正の時刻源は常に RTC**とし、
  `time` コマンドは `time_ctrl_get()`（RTC 直読）を使う

### #213（画面表示）での想定利用形態 — 割り込みは不要

分単位の画面表示に **RTC 周期割り込みは要らない**。`lv_timer_create(cb, 1000, NULL)` で 1 秒ごとに
`time_ctrl_get()` を呼び、**時分が変わったときだけ** `lv_label_set_text()` する形を想定する。
`time_ctrl_get()` は最悪 0.1 ms（§1）なので 1 Hz なら CPU 0.01%・ロック保持区間も 33.3 ms 予算の
0.3% で、KPI-01 に影響しない。LVGL 側は既に `lv_timer_handler()` ループが回っている
（`lvgl_thread_entry.c:267-274`）ので追加機構は不要。
**60 秒周期でポーリングしてはいけない**: `lv_timer` の位相は RTC の分境界と無関係なので、表示が
最大 60 秒古くなる。1 秒周期＋変化検出なら実再描画は 1 分に 1 回、遅延は最大 1 秒に収まる。
RTC 周期割り込み（`RTC_PERIODIC_IRQ_SELECT_1_MINUTE`, `r_rtc_api.h:132`）は、ベクタが未割当
（`ra_gen/vector_data.h` に `VECTOR_NUMBER_RTC_PERIOD` が無く `periodic_irq = FSP_INVALID_VECTOR`）
のため FSP 設定変更が要り、かつ ISR から LVGL API は呼べない（LVGL の所有は `lvgl_task`）ので
イベントフラグ経由の中継が純増する。**本 Issue では periodic/alarm 割り込みを使わない**方針を維持する。

## 8. 公開 API

```c
typedef enum { TIME_CTRL_OK = 0, TIME_CTRL_ERR_NOT_INIT, TIME_CTRL_ERR_NOT_SET,
               TIME_CTRL_ERR_INVALID_ARG, TIME_CTRL_ERR_HW,
               TIME_CTRL_ERR_BUSY } time_ctrl_err_t;   /* BUSY = RTC ロック取得失敗 */

typedef struct { uint16_t year; uint8_t mon; uint8_t mday;   /* 西暦 / 1-12 / 1-31 */
                 uint8_t hour; uint8_t min; uint8_t sec; uint8_t wday; } time_ctrl_time_t;

time_ctrl_err_t time_ctrl_init(void);                              /* ntshell_task から 1 回 */
time_ctrl_err_t time_ctrl_get(time_ctrl_time_t *p_time);
time_ctrl_err_t time_ctrl_set(const time_ctrl_time_t *p_time);     /* 入力の wday は無視し内部で算出 */
time_ctrl_err_t time_ctrl_parse(const char *date, const char *tm, time_ctrl_time_t *out);
void            time_ctrl_get_status(time_ctrl_status_t *p_status); /* time status 表示用 */
```

`time_ctrl_get_status()` が返すもの: 初期化済みフラグ / プロビジョニング済みフラグ /
設定済み(START)フラグ / サブクロック停止ビット（`R_SYSTEM->SOSCCR_b.SOSTP`）/
RTC 生値（RCR1, RCR2, RCR4 と BCD カレンダーレジスタ）/ 直近の `fsp_err_t`。

## 9. コマンド仕様

```
time                            現在時刻を表示   例: 2026-08-26 22:15:03 (Wed)
time set YYYY-MM-DD hh:mm:ss    時刻を設定（検証 NG なら設定せずエラー）
time status                     時刻源・各フラグ・RTC 生値・システム時刻を表示
```

対応年範囲は 2000-2099（RTC の RYRCNT は BCD 2 桁 ＋ `RTC_C_TIME_OFFSET` = 100 のため）。

## 10. ビルドへの追加

`e2studio_CPU0/Debug/src/subdir.mk` に `time_ctrl.o` / `time_cmd.o` を追加して CLI ビルドで検証する
（`Debug/` は `.gitignore` 対象。`.cproject` への正式登録は e2 studio が自動で行う）。
FLASH 増分（RTC ドライバが初めてリンクされる分）を `.flash.endof` で測り PR に記録する。
