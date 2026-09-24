# gate-r1 静的検証結果

実機未確認。保留・再開のハードウェア動作を確認したものではない。

- CPU0全1595オブジェクト再コンパイル・リンク成功。新規ゲート断片に対する警告なし。既存ライブラリヘッダの型変換等の警告は残る。
- CPU0/1両方のMOTについてチェックサム、ELFロード内容、開始アドレスを照合。ロード領域の重複0。
- 3722個の非変更元入力ファイルをハッシュ照合。凍結した診断コピー全入力も照合。
- `s_boot_gate`は内部RAM `0x220CB780`、44バイト。既存トレースは内部RAM `0x2210BEC0`、968バイト。
- LTO後のlvgl_taskでtk_cre_flg呼出し `0x0200F946`、tk_wai_flg呼出し `0x0200F9B0`。後続の成功分岐を通った先でRESET_BEGIN記録 `0x0200FA0C`、RESET最初のHigh書込み `0x0200FA1E`。
- POST_Cトレース後の200ms要求は `0x0203F672`でr0=200、`0x0203F674`でr1=1000、`0x0203F678`でR_BSP_SoftwareDelay呼出し。その後にピン初期化へ進む。
- シェルハンドラusrcmd_bootgateは `0x020149F8`。boot_gate_wait/completeはLTOによりlvgl_taskへインライン化されている。

## ソースとカーネル経路の確認

ソースのマスタは `scripts/issue232-gate/boot_gate.inc` と `build_gate.py`。
以下はレビューによる確認で、実行テストの結果とは区別する。

| 条件 | 処理 |
|---|---|
| continueがINIT中 | HOLD条件を満たさず拒否。後日のHOLDへ自動予約しない |
| フラグ待ちに入る直前のcontinue | REQUESTEDをpublish後にフラグset。カーネルがビットを保持するため先行通知を失わない |
| 通常のcontinue | LVGLのみ起床しRUNNINGへ。シェルはGLCDCを呼ばない |
| continue重複 | HOLD以外では拒否。再度のRESET・再初期化なし |
| create/wait失敗 | ERRORを記録、呼び元のtk_ext_tsk/returnでLVGL終了。タッチへ進まない |
| set失敗 | カーネルはID/存在チェック時点で返るため通知なし。HOLDへ戻しエラー保存、再要求可能 |
| GLCDC初期化失敗 | DISPLAY_END記録後にERROR、タッチへ進まずLVGL終了 |
| 別の診断コマンド | ディスパッチ前の許可リストで拒否。通常版には適用しない |

イベントフラグ実装の根拠:
`e2studio_CPU0/mtk3_bsp2/mtkernel/kernel/tkernel/eventflag.c:56,133,231`。
状態の書き手はLVGLとntshellだけ。publishとsnapshotはtk_dis_dspの同一条件で保護。
フラグ操作、デバイス処理、UARTを排他内で待たない。ゲート再生成・削除・状態の巻戻しは行わない。

追加公開シンボル4個はヘッダ宣言と定義・参照を照合済み:
boot_gate_wait（LVGL）、boot_gate_complete（LVGL）、usrcmd_bootgate（cmdlist）、boot_gate_command_allowed（dispatcher）。
boot_gate.incは診断コピーのusrcmd.cだけにincludeされる。既存ファイル／シンボルのビルド除外なし。

識別: BOOTGATE232 v=23202、BOOTTRACE232 v=23201 fnv=3615F648。
旧trace-r1と同じ形式のboottraceを残すが、このfingerprintと配布MOTハッシュで区別する。
