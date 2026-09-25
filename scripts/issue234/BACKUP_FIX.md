# RTCバックアップ修正 第2段階

2026-09-25。設計承認済み。アプリ実装・単体テスト・全ビルド完了。
FSPコード生成完了。XMLと生成ヘッダのPVDAS=0、VDSEL0=000の照合PASS。
実機オプション更新は未実施。backup-r1はFSP生成前のPC検証用。生成後の成果物はbackup-r2。

## 実装

- `e2studio_CPU0/src/rtc_backup.c:24` が実機OFS、電圧復帰、切替設定を確認する。
  必要な場合のみ検出停止→2.53 V設定→切替許可→100 µs待ち→検出有効化。
- `e2studio_CPU0/src/time_ctrl.c:174` は戻り値を処理し、失敗時は初期化済みにしない。
  保持喪失が記録されていた場合はRTCを再初期化し、成功後にフラグをクリアする。
- `e2studio_CPU0/src/time_cmd.c:188` はバックアップ準備状態、失敗理由、OFS、初期/現在の制御値を表示する。
- `configuration.xml` はSecure側のPVDAS=0、VDSEL0=000（2.85 V）に変更済み。
  FSP 6.3.0のRA8P1パックの選択肢IDと値を照合した。生成コードは手編集していない。

## FSP生成（完了、再現手順）

1. e2 studioのCPU0プロジェクトをRefresh（F5）する。
2. configuration.xmlを開き直し、OFS1_SECのCircuit Startがenabled、Levelが2.85 Vであることを確認する。
   外部変更と未保存変更の競合が表示された場合は、未保存の内容を確認し、古い無効設定で上書きしない。
3. Generate Project Contentを実行する。
4. 生成後は `python scripts/issue234/verify_fsp.py` でXMLと生成ヘッダを照合する。
5. 新しいrun名でCPU0を再ビルドし、verify.py/verify_boot.pyを通す。

この環境ではFSP生成専用のrasc.exeが見つからず、e2studio-cliのヘルプ呼出しから生成機能を確認できなかった。
IDEは利用者が開いているため、上記のGUI操作で生成する。生成完了を確認するまで最終書込みファイルとして扱わない。

2026-09-25: ユーザーが生成を実行。verify_fsp.pyはPASS。生成ヘッダの差分は
`bsp_mcu_ofs_cfg.h:14` の `1 <<3 | 7` → `0 <<3 | 0` のみ。

## オプション設定の更新（手順の確定待ち）

MOTはプログラム領域のみ。OFS更新は別途必要。
実機既読値 `FDFFFFFF` の下位4ビットだけを0にする場合、期待値は `FDFFFFF0`。
生成ヘッダ全体を実機に丸ごと書くと他ビットまで変わる可能性があるため、実機読戻し値を基準にする。

ローカルにRFP V3.22（CLI V1.15）が存在する。
付属 `docs/rfp-cli.md:733-754` ではwritebitはread-modify-writeで、開始ビットから下位方向へビット列を適用する。
したがって候補はOFS1_SECのbit 3から `0000` の4ビット変更。まだ実機では実行していない。
ユーザーはRFP V3.22またはe2 studioを使用。今回のオプション更新はRFP V3.22に揃える。
接続・RA8P1オプション領域対応を確認してから具体的なコマンドを確定する。
通常のシェルmw、erase-chip、DLM遷移、ロック設定は使わない。

### RFPとの接続確認（次の実機操作）

e2 studioのデバッグ接続とRFP GUIの接続を終了し、対象EK-RA8P1のJ-Link USB接続と給電を維持する。
複数のJ-Linkが接続されている場合は対象1台にするか、確認したシリアル番号で指定する。
Windows PowerShellから以下を実行する。これはメモリ読出しだけだが、RFP接続終了時にはデフォルトでターゲットがリセットされる。

```powershell
$rfp = 'C:\Program Files (x86)\Renesas Electronics\Programming Tools\Renesas Flash Programmer V3.22\rfp-cli.exe'
& $rfp -d RA -if swd -tool jlink -read-view 02C9F0C0 4 -view-size 4
& $rfp -d RA -if swd -tool jlink -read-view 02C9F120 4 -view-size 4
```

期待値は既測定のFDFFFFFF / 00000000。接続結果・読出し値を確認してから次の書込みへ進む。
根拠: ローカルRFP付属docs/rfp-cli.md:98（jlink）、:142（swd）、:643（read-view）、:665（view-size）。

### 接続確認後の限定更新案（未実行）

RFPのread-modify-writeで `-writebit 02C9F0C0 3 0000` を使用する案。
開始bit 3から下位4ビットのみ0にし、実機読戻しがFDFFFFF0となること、OFS1_SELが00000000のままであることを確認する。
この段階ではMOT指定やerase/autoなど他のFlash操作を併記しない（RFP仕様）。
実機オプションへのアクセスが拒否された場合は止め、エラー全文と接続情報を確認する。ロック解除や全消去で回避しない。
更新確認後、CPU0のbackup-r2/mimamori_sense_CPU0.motを従来のRFPプロジェクトから書き込む。
CPU1の指定が必要なら同梱mimamori_sense_CPU1.motを使用する（従来版と同一）。
いずれもプログラムFlashだけを含み、ELF全体やOFSを含む別MOTと混ぜない。

参考: [SEGGER RA8のオプション領域でのRFP read/writebit例](https://kb.segger.com/Renesas_RA8)。
これは別レジスタの例であり、今回のOFS書込み成功を実証するものではない。

## PC検証結果

- ARM命令をUnicornで実行: backup_test.c、diag_test.c PASS。
- 初期設定、保持済み省略、OFS不一致、電圧未復帰の有限待ち、検出停止順序、4段階の書込み失敗、フラグクリア失敗、保護復帰を確認。
- backup-r1: 1,600オブジェクトを全再コンパイル・リンク成功。text 978130、data 282、bss 8006705。
- 新規rtc_backup.cのコンパイラ警告なし。既存依存ヘッダの警告は残る。
- S-record/ELF内容一致、チェックサム、CPU0/CPU1非重複、診断RAMのゼロ/コピー除外、ソースハッシュ確認PASS。
- 実機の電圧波形・保持・白画面はこのテストでは検証していない。

### FSP生成後の最終ビルド backup-r2

- 1,600オブジェクト全再コンパイル・リンク成功。text/data/bssはbackup-r1と同じ。
- verify.py、verify_boot.py、verify_options.pyは全てPASS。
- ELF上のOFS1_SECはFDFFFFFF→FDFFFFF0、変更マスク0xF。OFS1_SELは0のまま。
- CPU0 MOT SHA-256: `e995fe330c1d0d7c3de68a64872acdecf8bce450484cc7a84eb5f14202a4ecf8`。
- 出力: `e2studio_CPU0/Debug/issue234/backup-r2/mimamori_sense_CPU0.mot`。
- CPU1: 同ディレクトリの `mimamori_sense_CPU1.mot`（以前の版と同一）。
- このMOTにもOFSは含めていない。実機オプションの更新・読戻しと組み合わせて試験する。

## 最終書込み後の確認

2026-09-25 実機報告: RFP V3.22 / J-Link OB-RA4M2 / SWDでR7KA8P1KFLCACに接続。
`-writebit 02C9F0C0 3 '0000'` がOperation successful。
RFPはConfig Area 2の02C9F0C0–02C9F0CFを読み取り・書込み・検証した。
独立した読戻しでOFS1_SEC=FDFFFFF0、OFS1_SEL=00000000を確認。
オプション更新完了。backup-r2のプログラム書込みとRTC保持確認は未実施。

OFS1_SECとSELを再読出しし、変更が下位4ビットだけであることを確認する。
`time status`でBackup ready、CR1=00、CR2=11を確認後、シェルまたはGUIで時刻設定。
OFF60秒の前後で時刻が進み、再起動でClock sourceがkeptであることを確認する。
再起動後はログ取得前に時刻を再設定しない。白画面でもシェルが動けば試験可能。

## 実機保持試験の結果（2026-09-25）

ユーザーは、先行する失敗試験では電池側の電源がOFFだったと報告した。
電池側ON、主電源・USB給電60秒OFFの手順を案内した後の再試験ログでは、
電源断前13:22:11、再起動後13:23:31（表示間隔80秒）を確認。
80秒には電源操作・起動・コマンド入力の時間が含まれるため、時計精度の評価値とはしない。

- 再起動直後RESETからBEFORE_DECISIONまでRCR2=41、SOSCCR=00を保持。
- VBATT boot/nowはいずれもCR1/CR2/SR=00/11/30。
- Clock source=kept、Time set=yes、Backup=ready、Last FSP err=0。

この1回の試験で、修正版・OFS更新・電池側ONの組合せによる電源断後の時刻保持を確認した。
先行失敗試験は電池側OFFという条件差があり、ソフトウェア不具合だけの証拠には使わない。
各修正の寄与を個別に実証した比較試験ではない。長時間保持・反復信頼性・白画面改善は未評価。

続いてユーザーが主電源10分OFF後のログを報告。現在時刻は2026-09-25 13:35:42。
RESETからBEFORE_DECISIONまでRCR2=41、SOSCCR=00を保持し、Clock source=kept、
Time set=yes、Backup=ready、Last FSP err=0。VBATT boot/nowはともに00/11/30。
10分OFF後の保持と起動時の非リセットを確認した。直前の時刻・実時計との対比は未提示のため、
10分間の進み量や時計精度は定量評価していない。
基本保持確認は60秒・10分の各1回で成功。さらに長時間の保持、反復信頼性、白画面改善は未評価。

## RTC保持確認後の表示試験（2026-09-25）

ユーザー報告: ACアダプタ給電、OFF30秒、10回の起動で全て白画面。
個別結果: 白、白、白、白、白、白、白、白、白、白（10/10）。
会話上はbackup-r2の保持確認に続く試験。今回の試験時のイメージ読戻し・電池スイッチ状態・
各起動のRTCログは提示されていない。RTC保持試験の成功と、表示試験の不合格を分けて記録する。
同じセッション内の旧版対照がないため、今回のRTC修正による白率悪化とは判定しない。

次の調査候補と過去の電圧測定は `doc/analysis_report/issue232-paused-20260924.md` を参照。
追加の同条件反復、待ち時間総当たり、電池設定の戻しはこの結果だけを根拠に要求しない。
