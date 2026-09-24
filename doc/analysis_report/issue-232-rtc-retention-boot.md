# #232 RTC保持状態による起動処理の差

調査日: 2026-09-23。静的解析のみ。コード変更・ビルド・書込み・追加実機測定は未実施。

## 結論

RTC保持状態に応じて変わる処理は存在する。ただし、今回の正常5/10・白5/10を説明する因果は未確定。
特に「RTC未保持時の初期化待ちが、表示の開始タイミングに偶然の余裕を与えていた」という仮説は成立しうるが、
起動タスク間の実行順と所要時間が未測定なので、保持すると必ず早くなるとは言えない。

確定した点:

- POST_Cの200ms要求はRTC保持の有無によらず実行される。
- サブクロック初期化はSOSCCR/SOMCRで分岐するが、このビルドには保持時だけ省略される1秒の安定化待ちはない。
- BSPのRCR4書込みは両条件で実行され、RTC全体のリセットは両条件ともBSPでは実行されない。
- アプリのRTC初期化はSTART/HR24で分岐し、保持された正常な計時状態ならリセットを省略して時刻を読む。
- 非保持時のSTART/HR24は不定であり、「電池OFFなら必ずリセット経路」とする二分は誤り（#234）。
- OS時刻同期はLVGLが使う稼働時間を変更しない。
- RTC待ちが無限化した場合の影響を「そのタスクだけ」とする既存コメントは誤り。低優先度タスクは実行機会を失う。
  ただし、今回のRTC読出しで無限待ちが起きた証拠はない。

各項目のコード・仕様根拠は以下を参照。

## 対象と根拠

- ローカルHEAD: `4685202ea6f50872fd8015b18b3fc94fff5e14ae`。
- 対象配布物: `MAIN_wait200_postc_CPU0.mot`、CPU1は同名ペア。
- 保存済みCPU0 MOTのSHA-256を再確認: `265f8f8e6a9744fa6055b0be5d823c67afbd8cbcd321eff70e31610809754fcf`。
- 逆アセンブル: `e2studio_CPU0/Debug/issue230/main-wait-r2/artifacts/MAIN_wait200_postc_CPU0-disassembly.txt`。
- 公式仕様: [RA8P1 User's Manual: Hardware Rev.1.30](https://www.renesas.com/en/document/mah/ra8p1-group-users-manual-hardware)
  （R01UH1064EJ0130、2026-02-27）。Web再取得は失敗したため、#230で取得済みのPDFとページ区切り付き抽出テキストを参照した。
  保存先: `e2studio_CPU0/Debug/issue230/ra8p1-hardware-manual.pdf` / `.txt`。
- ユーザー報告の全試行・3条件の同時変更は [#232](https://github.com/grace2riku/mimamori-sense/issues/232) をマスタとする。
  前回正常10/10の記録は `doc/design/issue-230.md` 32節。今回の追加条件での再発とは分ける。

## 1. OS起動前

### 1.1 サブクロック

CPU0のSystemInit→bsp_clock_init→bsp_prv_sosc_initがサブクロックを扱う
（`e2studio_CPU0/ra/fsp/src/bsp/cmsis/Device/RENESAS/Source/system.c:277-296`、
`e2studio_CPU0/ra/fsp/src/bsp/mcu/all/bsp_clocks.c:2326,3178-3248`）。
CPU1はBSP_CFG_SKIP_INIT=1でこのクロック／RTC初期化を省く
（`e2studio_CPU1/ra_cfg/fsp_cfg/bsp/bsp_mcu_family_cfg.h:16-19`、上記system.c:277,384）。

| 起動時に読んだ状態 | 実行処理 |
|---|---|
| SOSCCRが停止状態 | SOMCRを設定しSOSCCR=0で発振開始 |
| 発振中だがSODRVが設定値0と不一致 | SOSCCR=1、200µs要求の停止間隔、停止確認、SOMCR設定、発振開始 |
| 発振中でSODRVも一致 | 発振を再起動せず、R_BSP_SubClockStabilizeWaitAfterResetへ進む |

根拠: bsp_clocks.c:3195-3241。ここは実行時分岐。ただし電池スイッチそのものを読んでいるわけではなく、
実際に各起動がどの分岐を通ったかは未採取。公式仕様12.1/12.3.1（pp.502,516）はRTCとSOSCを電池給電領域に含める。

安定化時間の設定値は1000msだが、今回のPLL1P/FLL無効構成では新規発振側の待ち呼出しがコンパイル時に除外され、
発振継続側のWaitAfterResetも待たない。したがって「電池ONで1秒待ちが消えた」という説明は当てはまらない。
根拠: `ra_cfg/fsp_cfg/bsp/bsp_cfg.h:50-57`、`ra_gen/bsp_clock_cfg.h:28`、
`ra_cfg/fsp_cfg/bsp/bsp_mcu_family_cfg.h:41`、`ra/fsp/src/bsp/mcu/all/bsp_clocks.h:516-523`、
bsp_clocks.c:2856-2865,3229-3231（この段落の相対パスはe2studio_CPU0配下）。
発振安定化そのものが不要という意味ではない。公式仕様9.2.14（p.340）は開始後の発振安定時間を要求する。

### 1.2 BSP RTC設定と200ms

`R_BSP_Init_RTC()` はRCR4=0を書き、VBTICTLRをクリアする。BSP_CFG_RTC_USED=1なので、
RCR2/RESET/RCR1/TCENを初期化するブロックはコンパイル時に除外される。
保持の有無に応じてこのブロックが動的に切り替わるわけではない。
根拠: bsp_clocks.c:3361-3423、`e2studio_CPU0/ra_cfg/fsp_cfg/bsp/bsp_cfg.h:24`、
`e2studio_CPU0/ra/fsp/src/bsp/mcu/ra8p1/bsp_feature.h:495-497`。

その後graphics/NPUドメイン処理→POST_Cの200ms要求→IOPORT→SDRAMという順序。
根拠: system.c:387,440-471、`e2studio_CPU0/src/hal_warmstart.c:120-145`。
配布物の実命令でもRCR4書込み0x0203DD62、200ms要求の呼出し0x0203DF02が確認済み
（`doc/design/issue-230.md:1033-1038`）。待ち実時間は未測定。

保持された計時中にもRCR4へ同値0を書き直す点は仕様照合の対象として残る。
公式仕様27.2.23（p.1246）は初期設定前のカウント源選択を規定するが、
同値再書込みだけでLCD白画面が生じるとの根拠はない。RCR4=0を計時停止や発振再開と同一視しない。

## 2. OS起動後の実行時分岐

呼出し元はntshell_taskの`time_ctrl_init()`（`e2studio_CPU0/src/ntshell_thread_entry.c:212-217`）。
成功ならready、エラーならNOT availableを表示し、どちらもシェルへ続行する。API内で戻らない場合はこの判定まで到達しない。

| 判定 | 処理 | 所要時間の確定状況 |
|---|---|---|
| START=1かつHR24=1 | クロック源設定・リセット省略。STARTが維持されていればCalendarTimeGet→BCD/日付検証→OS時刻同期 | 実測なし |
| 上記以外 | ClockSourceSet→RCR4設定→200µs要求→START停止確認→RCR2=0/CNTMD確認→RESET完了確認→RCR1=0確認→HR24設定確認→TCENクリア確認 | 固定200µsに加えて上限なしのレジスタ待ち。最悪時間は有限と保証できない |

根拠: `e2studio_CPU0/src/time_ctrl.c:145-230,553-564`、
`e2studio_CPU0/ra/fsp/src/r_rtc/r_rtc.c:330-350,1035-1119`。
ClockSourceSetの戻り値は捨てられるが、本設定ではパラメータ検査が無効で成功を返す経路のみ。
「エラーを返して起動を中断する」処理ではない（`ra_cfg/fsp_cfg/r_rtc_cfg.h`、time_ctrl.c:178-182）。
初期化完了後もBCD/日付が不正なら同期しない一方、time_ctrl_init自体はOKを返す（time_ctrl.c:185,195-200,220-230）。

配布物にも同じ分岐がある。0x02011EEA〜0x02011EF4でSTART/HR24を検査し、成立時は0x02011FA2へ飛ぶ。
非成立時の0x02011EF6〜0x02011FA0には上記リセットと待ちループが存在する。
保持側では0x02011FB4からCalendarTimeGetへ進む。これはLTOでntshell_taskへインライン展開されたtime_ctrl_initであり、
ソース上だけの未リンク処理ではない。

### 2.1 電池OFFとの対応は単純ではない

公式仕様27.2.21（p.1242）のSTART/HR24はリセット値が不定。
したがって非保持時にも「初期化済み」と誤判定する可能性があり、
`time_ctrl.c:542` の「VBATT喪失後はRCR2=0」というコメントを根拠にしてはいけない（#234）。
電池ON・時刻設定済みで両ビットが正しく保持された場合はリセット省略になるが、
今回の白5件すべてでその状態だったことは、個別ログなしには確定しない。

### 2.2 時刻読出しと割り込み

Openは制御ブロックと割り込みの優先度／コンテキストを設定する。Open内のクロック源設定は無効。
根拠: r_rtc.c:193-253,1130-1151、`e2studio_CPU0/ra/fsp/src/bsp/mcu/all/bsp_irq.h:120-137`。
CalendarTimeGetは必要時にCIEとNVIC carry IRQを一時有効化し、時刻を読み、carryがあれば再読出しし、最後に元へ戻す
（r_rtc.c:421-470,1160-1177,1687-1702）。「毎回次の1秒境界まで待つ」処理ではない。

公式仕様27.6.5（p.1264）ではCIEは即時読戻し可能。従って、既存コメントにある
「サブクロック停止ならCIE待ちが必ず止まる」という説明は、この仕様だけからは裏付けられない。
レジスタ読戻し待ちに上限がないことと、今回それが発生したことは区別する。
同節の復帰後1/128秒・リセット後6カウント源周期の要件も、発振していることが前提である。
200ms要求だけを根拠に全条件で満足と断言しない。

## 3. 表示への影響

### 3.1 スケジューリングによる間接影響はありうる

ntshell優先度12、LVGL優先度14（数値が小さい方が高優先度）:
`e2studio_CPU0/src/usermain.c:190-198,235-242`。
RTCレジスタ待ちはsleepではなくCPUを使うwhileループ
（`e2studio_CPU0/ra/fsp/src/bsp/mcu/all/bsp_common.h:122`）。
優先度順に実行タスクを選ぶ根拠は
`e2studio_CPU0/mtk3_bsp2/mtkernel/kernel/tkernel/ready_queue.h:84-92,109-131`、`task.c:119-139`。

そのため、RTC初期化待ちの有無・長さはLVGLの実行時刻に影響しうる。
ただしNT-Shellのログ出力はtk_dly_tskでCPUを譲るので、RTC初期化完了がGLCDC開始より必ず先とは限らない
（`e2studio_CPU0/src/jlink_console.c:153-167,221-236`）。
GLCDC初期化はlvgl_task内（`e2studio_CPU0/src/lvgl_thread_entry.c:168`）。
保持ありで描画が何ms早くなるか、リセット省略がLCD起動の余裕を減らすかは実測が必要。
また、time_ctrl_initはSDRAM初期化より後なので、その待ちの差を「SDRAM初期化前の待ちが減った」と説明してはいけない。

### 3.2 無限待ちのタスク隔離に関する潜在的欠陥

時刻キャッシュタスクも優先度12で、LVGLタスクから起動される
（`e2studio_CPU0/src/time_cache.c:48,95-133`、`src/ui/ui_datetime.c:89-107`）。
このタスクのRTC読出しが戻らなければ、末尾のtk_dly_tskに到達せず、低優先度の描画タスクは実行できない
（time_cache.c:227-242）。ミューテックスはこのCPU占有を防がない。
`time_ctrl.c:34-36`、`time_cache.c:218-222`、`ntshell_thread_entry.c:204-206`等の
「他タスクは動く／被害は1タスクに閉じる」という説明には優先度の考慮が欠ける。
高優先度LEDが動くことだけでも描画タスクの健全性は証明できない。

これはコード上の潜在的問題であり、今回のRTC保持時の白画面で発生したとは確認していない。
特にCIEの性質は2.2節のとおり。#230の過去の「描画が進むのに全面白」という観測と、
描画タスクが進まない場合は異なる故障モードとして扱う。

### 3.3 OS時刻の値によるタイマー飛びは該当しない

time_ctrlの同期はtk_set_utcを呼ぶが、変更するのはknl_real_time_ofsのみ。
稼働時間knl_current_timeは変更しない
（`e2studio_CPU0/src/time_ctrl.c:746-759`、
`e2studio_CPU0/mtk3_bsp2/mtkernel/kernel/tkernel/time_calls.c:36-44,103-110`）。
LVGL tickは有効なコールバック設定でtk_get_otmを使う
（`e2studio_CPU0/ra_cfg/fsp_cfg/middleware/rm_lvgl_port_cfg.h:11`、
`e2studio_CPU0/src/port/lvgl_port_mtk3.c:178-180,337-344`）。
従って設定した日付へのジャンプがそのままLVGLタイマーへ伝わる経路ではない。
UIの日時更新もRTC直読でなくキャッシュ参照（`e2studio_CPU0/src/ui/ui_datetime.c:119-129`）。

## 4. 次に必要な証拠

既存の白画面試験・待ち値探索を繰り返す前に、同じ起動内で以下を区別できる診断設計が必要。

1. BSP初期化前のSOSCCR/SOMCRと、アプリ初期化前のRCR1/2/4。保持状態を後からの値で代用しない。
2. time_ctrl_initの分岐、開始／完了、レジスタ待ちの位置と所要時間。
3. GLCDC開始・最初の描画とRTC初期化の前後関係。白時にも描画カウンタが進むか。

既存time statusはdid_provisionと採取時のRTC値を示すが、BSP操作前の値・待ち時間・描画との順序は示さない
（`e2studio_CPU0/src/time_ctrl.c:411-449`）。シェル自身がRTC初期化で止まれば入力もできない。
PC給電で得たログをAC給電の白率と同一条件の証拠にしない。

追加計測を実装する場合は#232の設計メモで保存場所・配置変動・取得方法・試行上限・停止条件を先に定める。
本調査では計測コードや待ち値の変更は行っていない。RTC判定の修正は#234と整合させる。
