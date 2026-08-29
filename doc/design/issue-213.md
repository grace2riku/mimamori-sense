# Issue #213 設計メモ — ステータスバー日時ラベルの更新

対象: `e2studio_CPU0/src/time_cache.{c,h}`（新規）/ `e2studio_CPU0/src/ui/ui_datetime.{c,h}`（新規）
/ `lvgl_thread_entry.c`

> **改訂（PR #217 レビュー指摘 P1 / 2026-08-29）**: 初版は `lv_timer` コールバックから
> `time_ctrl_get()` を直接呼んでいたが、**サブクロック喪失時に `lv_lock()` を握ったまま
> 戻らなくなる**（LVGL が恒久フリーズする）ことが判明した。ポーリングタスクを分離し、
> コールバックはキャッシュを読むだけに変更した。経緯と根拠は §7。

## 1. 設計の入力（確定値・根拠付き）

| 項目 | 確定内容 | 根拠 |
|---|---|---|
| 呼び出し元 | `ui_datetime_init()` は `lvgl_task` の 1 箇所のみ（`lvgl_thread_entry.c` Step 7c）。`time_cache_init()` の呼び出し元は `ui_datetime_init()` の 1 箇所のみ。**間接呼び出し**: `ui_datetime_timer_cb` は `lv_timer_create()` で登録され `lv_timer_handler()` から呼ばれる → 実行コンテキストは `lvgl_task` のみ。`time_cache_task` は `tk_cre_tsk()` で登録され μT-Kernel が起動する | `lvgl_thread_entry.c:106,200,267` |
| 同時呼び出し | `time_ctrl_get()` を呼ぶのは **`time_cache_task` のみ**（`ntshell_task` の `time` コマンドと合わせて 2 タスク）。#212 の `TA_INHERIT` 資源所有ミューテックスが直列化する（`time_ctrl.c:481-521`）。**書き手はタスクのみ・ISR 経路なし**という #212 の前提は崩れない → **#212 側の変更は不要** | `usermain.c:194` / `doc/design/issue-212.md` §5 |
| ブロック許容時間 | `lv_timer` コールバック内は **ブロック一切不可**（`lv_lock()` を保持しているため、1 回でも無限に待つと LVGL 全体が止まる。§7）。`time_cache_task` は 1 Hz の独立タスクなので**無制限に待ってよい**（ハングしても被害はこのタスク 1 本） | §7 |
| 依存先 API の最悪所要時間 | `time_cache_get()` = `tk_dis_dsp()` 区間での構造体コピー 1 回。**待ちゼロ・µs 未満**。`time_ctrl_get()` は正常時 ≒ 0.1 ms だが、**サブクロック喪失時は戻らない**（`r_rtc.c:435-441` → `r_rtc_irq_set()` `r_rtc.c:1159-1170` → `FSP_HARDWARE_REGISTER_WAIT` `bsp_common.h:122`、タイムアウト無し） | §7 |
| 失敗の返し方 | 表示専用。`time_cache_get()` が false（未取得・取得失敗・キャッシュが陳腐化）ならラベルを `"--:--"` にして時刻表示と区別する | Issue #213 |
| 実行コンテキスト制約 | `ui_datetime_*` は `lvgl_task` のみ。`time_ctrl_*` を呼ぶのは `time_cache_task` のみ。ISR からはどちらも呼ばない（`time_ctrl_*` は ISR 禁止: `time_ctrl.h` 「実行コンテキスト制約」） | `time_ctrl.h` |

## 2. モジュール分割

| モジュール | 責務 | ブロック |
|---|---|---|
| `time_ctrl`（#212、変更なし） | RTC アクセス。資源所有ミューテックスで直列化 | **する**（サブクロック喪失時は無限） |
| `time_cache`（新規、`src/`） | 1 Hz ポーリングタスク ＋ スナップショットの publish。LVGL に依存しない | 読み出し側は**しない** |
| `ui_datetime`（新規、`src/ui/`） | `lv_timer` ＋ 書式化 ＋ ラベル更新。μT-Kernel に依存しない | **しない** |

`src/ui/` は LVGL だけに依存する表示層に保ち、μT-Kernel タスクと RTC は `src/` 側に置く。
モジュールが自前でタスクを生成する作法は `lv_os_mtkernel.c:99-118`（dave2d / swdraw 描画スレッド）
に前例があり、`usermain.c` は変更しない。

## 3. 状態変数と読み書きコンテキスト

### `time_cache`

| 変数 | 書き手 | 読み手 | 保護 |
|---|---|---|---|
| `s_pub_time` (`time_ctrl_time_t`) | `time_cache_task` | `lvgl_task`（`time_cache_get()`） | `tk_dis_dsp()` 区間 |
| `s_pub_valid` (bool) | 同上 | 同上 | 同上 |
| `s_pub_tick` (uint32_t, `tk_get_otm()` の ms) | 同上 | 同上 | 同上 |
| `s_tskid` (ID) | `lvgl_task`（`time_cache_init()` のみ） | 同上（多重生成の判定） | 不要（単一タスク） |

**3 ワードの publish と sample は、それぞれ同一の `tk_dis_dsp()` 区間に入れる。**
分けると読み手が「新しい時刻 ＋ 古いスタンプ」の組を採取し、有効な値を陳腐化と誤判定する
（CLAUDE.md「並行性の既定形」）。**書き手はタスクだけ**（ISR から書く経路は作らない）なので
`tk_dis_dsp()` で足りる。区間内では待ちを伴う処理を一切行わない（構造体コピーのみ）。

### `ui_datetime`

| 変数 | 書き手 | 読み手 | 保護 |
|---|---|---|---|
| `s_timer` (`lv_timer_t *`) | `lvgl_task`（`ui_datetime_init()` のみ） | `lvgl_task`（多重登録の判定） | 不要（単一タスク） |
| `s_shown` (`time_ctrl_time_t`, 表示中の年月日時分) | `lvgl_task`（cb） | `lvgl_task`（cb） | 不要（単一タスク） |
| `s_shown_valid` (bool, 時刻を表示中か) | 同上 | 同上 | 不要（単一タスク） |

## 4. ポーリング周期と陳腐化判定

- `time_cache_task`: `tk_dly_tsk(1000)` ごとに `time_ctrl_get()` → publish。
  `itskpri = 12`（`ntshell_task` と同値。どちらも RTC 利用者で緊急性が無い）、`stksz = 1024`。
- `ui_datetime`: `lv_timer_create(cb, 1000, NULL)`。**年月日時分が変化したときだけ**
  `ui_main_screen_set_datetime()` を呼ぶ（実再描画は 1 分に 1 回、表示遅延は最大 2 秒
  ＝ ポーリング 1 秒 ＋ 描画 1 秒）。60 秒周期にしない理由は `doc/design/issue-212.md` §7
  （位相が RTC の分境界と無関係）。
- **陳腐化しきい値 5000 ms**（ポーリング 5 周期分）。`tk_get_otm()` の差がこれを超えたら
  `time_cache_get()` は false を返し、ラベルは `"--:--"` になる。
  低優先度でのスケジューリング揺らぎで誤って陳腐化しないよう周期の 5 倍を取る。
- 表示書式は `"YYYY-MM-DD hh:mm"`（16 文字）。ラベルは `lv_font_montserrat_14`、
  ステータスバー中央（`ui_main_screen.c:351-355`）。左の `Idle` と右の 80px ボタンとは重ならない。

### RTC ハング時の挙動（検出と復旧）

サブクロックが停止すると `time_cache_task` が `time_ctrl_get()` の中で戻らなくなる。

- **検出**: `s_pub_tick` が進まなくなる → 5 秒後に `time_cache_get()` が false を返す
- **復旧**: ラベルが `"--:--"` に戻り、**LVGL は描画を続ける**。被害は
  `time_cache_task` 1 本（＋ RTC ミューテックスを握ったままなので、`time` コマンドが
  `TIME_CTRL_ERR_BUSY` を返すようになる。これは #212 が設計済みの挙動）
- 自動復旧はしない（サブクロックの発振停止に回復経路が無いため）。`time status` は
  ロック無しで生レジスタを読むので、この状況でも診断できる（`time_ctrl.h`）

## 5. 多重登録・破棄

`ui_main_screen` は `s_created` で二重生成を防ぎ（`ui_main_screen.c:156-158`）、**破棄経路は存在しない**
（`src/` 全体で `lv_obj_delete` / `lv_obj_del()` の呼び出しは 0 件、`lv_screen_load()` は
`ui_main_screen.c:199` の 1 件のみ）。
`ui_datetime_init()` は `s_timer != NULL` なら、`time_cache_init()` は `s_tskid > 0` なら no-op とする。
将来 `ui_main_screen` に破棄経路を足す場合は deinit を同時に追加すること
（本 Issue では呼び出し元が無いので作らない）。

## 6. 捨てた代替案

- **`lv_timer` コールバックから `time_ctrl_get()` を直接呼ぶ（初版）**: サブクロック喪失時に
  `lv_lock()` を握ったまま無限ブロックし、LVGL が恒久フリーズする。**PR #217 で P1 指摘**。破棄（§7）。
- **`tk_get_utc()` を読む**: 非ブロッキングだが SysTick ドリフトが乗り（#212 §7「正の時刻源は常に RTC」）、
  ms→暦の逆変換（`civil_from_days`）を新規に書く必要がある。破棄。
- **RTC 周期割り込み（1 分）**: ベクタ未割当で `configuration.xml` 変更が要り、ISR から LVGL API を
  呼べず中継が純増（#212 §7）。破棄。
- **`time_ctrl` に RTC 生レジスタ直読の非ブロッキング API を追加**: 待ちは無くせるが、R64CNT 二度読みで
  桁上がり安全性を自前で担保する必要があり、FSP がやっている処理の再実装になる。破棄。
- **`ui_main_screen.c` にタイマを内蔵**: UI 層に `time_ctrl` / μT-Kernel が入り層が混ざる。破棄。
- **`lvgl_thread_entry.c` にコールバックを直書き**: 状態変数とタイマ寿命がスレッド起動コードに混ざる。破棄。
- **BUSY 連続回数で giveup（初版の縮退）**: `TIME_CTRL_ERR_BUSY` は「**他タスクが**ミューテックスを
  保持している」ときにしか返らないため、`lvgl_task` 自身が最初にハングするケースを検出できない。
  §4 のスタンプ陳腐化に置き換えた。破棄。

## 7. PR #217 レビュー指摘 P1 の検証記録

**指摘**: LVGL ロックを保持したまま失敗した RTC を読むな。

確認したコードパス（すべて実際に読んで裏取り済み）:

```
time_ctrl_get()                          time_ctrl.c:251     [RTC ミューテックス取得]
  └ R_RTC_CalendarTimeGet()              r_rtc.c:435-441
      └ NVIC_GetEnableIRQ(carry_irq) == 0
          └ r_rtc_irq_set(true, CIE)      r_rtc.c:1159-1170
              └ FSP_HARDWARE_REGISTER_WAIT((R_RTC->RCR1 & mask), mask)
                 = while (reg != required_value) {}    bsp_common.h:122
```

1. `carry_irq = VECTOR_NUMBER_RTC_CARRY = 22` は**有効なベクタ**（`ra_gen/vector_data.h:81`、
   `ra_gen/hal_data.c:24-28`）。`carry_ipl = 12`
2. `R_RTC_Open()` は `R_BSP_IrqCfg()` を呼ぶだけで **`R_BSP_IrqEnable()` を呼ばない**
   （`r_rtc.c:1147-1149`）。よって NVIC の enable ビットは常に 0 で、
   **`time_ctrl_get()` は毎回 `r_rtc_irq_set()` の無限待ちの経路を通る**
3. RCR1 の更新はカウントソース（サブクロック）に同期するため、サブクロックが止まると
   この `while` から出られない。これは #212 が `R_RTC_ClockSourceSet()` について
   既に文書化しているのと同じ機序（`time_ctrl.h` の `@warning`）

**初版の縮退（BUSY 連続 3 回で `lv_timer_pause()`）が効かない理由**:
`TIME_CTRL_ERR_BUSY` は `time_lock()` が失敗したとき、すなわち**他タスクが既にミューテックスを
保持している**ときにしか返らない（`time_ctrl.c:251-253,517-521`）。`lvgl_task` が最初にこの経路へ
入った場合はミューテックスを自分で取得してから FSP 内でハングするため、BUSY は 1 度も返らず、
カウンタは回らない。コールバックは `lv_timer_handler()` が `lv_lock()` を取ってから
`lv_unlock()` するまでの区間で走る（`lv_timer.c:81,327,144`）ので、**LVGL は恒久フリーズする**。

**#212 の封じ込めが成立しない理由**: `time_ctrl.h` は「被害を呼び出しタスク 1 本に閉じ込める」と
書いているが、その 1 本が `lv_lock()` を保持する `lvgl_task` の場合、描画スレッドと
`lvgl` シェルコマンドまで巻き添えになる。初版はこの前提の差を見落としていた。

## 8. ロック順序（デッドロックしないことの確認）

改訂後、`lv_lock()` を保持した状態で取得するロックは無い。`ui_datetime_timer_cb` が
`time_cache_get()` 経由で入るのは `tk_dis_dsp()` 区間だけで、ここではロックを取らない。
`time_ctrl` の RTC ミューテックスを取るのは `time_cache_task` と `ntshell_task` のみで、
いずれも `lv_lock()` を保持していない（`time_ctrl.c` に LVGL 呼び出しは 0 件、
`usrcmd.c` は `time_ctrl_*` を呼ばない — grep 0 件）。

## 9. ビルド結果（CLI ビルドで実測。`e2studio_CPU0/Debug` で `make -j16 all`）

| | text | bss | `.flash.endof` | FLASH 使用量 |
|---|---|---|---|---|
| 変更前（main 相当） | 806,778 | 6,776,865 | `0x020C5200` | 807,424 B |
| 改訂後 | 807,490 | 6,776,921 | `0x020C5400` | 807,936 B |
| 差分 | **+712 B** | **+56 B** | — | **+512 B** |

FLASH 使用量は 807,936 B / 1,015,808 B（992 KiB, `0xF8000`）で **79.5%**。
`time_cache.c` / `ui_datetime.c` 自身のコンパイル警告は 0 件
（`-Wconversion` / `-Wextra` 有効。残る 2 件は `ui_datetime.c` が include する
LVGL ヘッダ由来の既存警告）。

`time_cache_task` のスタック 1024 B は `bufptr = NULL` ＋ `USE_IMALLOC = 1` により
カーネルのメモリプールから確保されるため `bss` には現れない
（`bss` の +56 B は本モジュールの静的変数）。プールは内部 RAM 末尾まで
（`CNF_SYSTEMAREA_END = 0x221B0000`、`mtk3_bsp2/config/config.h:35`）確保されており、
タスク数も 11 本程度で `CNF_MAX_TSKID = 32`（同 :42）に余裕がある。
