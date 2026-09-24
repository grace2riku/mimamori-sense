# #232 U2配線確認：回路図取得待ち

**更新：回路図受領・配線確認完了。以下は取得前の記録。確定結果と測定候補は [u2-schematic/README.md](u2-schematic/README.md) を正とする。**

## 確認済み

基板マニュアルR20UT5462EG0101 Rev.1.01 表1/2/3:

- J1-8 = VCC5、TP3。
- J1-5/7 = VCC33、TP8。
- J1-6 = RESET_L。
- J2-7 = RESET_L、LCDパネル44番RESET。
- J3-4 = RESET_L、タッチパネル4番RESET。
- J1-1 = BLEN。マニュアル4.4節ではU1（ISL97682）バックライト制御用。
- U2はISL78010、LCD用AVDD/VGH/VGL/VCOMを生成（4.5節）。

このRESET_Lの同名信号接続はコネクタ表で確認できるが、U2 ENとの接続は掲載されていない。
BLENをU2 ENと読み替える根拠もない。
ソフトはDISP_RESET=P606をHigh→Low→Highに駆動する
（`e2studio_CPU0/src/port/glcdc_port.c:301,316,319,322`）。これだけでU2を再起動できるとは言えない。

## 未確定

U2のVIN/VDDが5V/3.3Vのどちらから、どの抵抗・トランジスタ経由で供給されるか。
ENの駆動元、RESET_Lとの分岐・抵抗接続、表側からアクセス可能な代替点。
TP3/TP8の電圧を、そのままU2端子の入力電圧と断定しない。
小型ICのENピン番号や基準回路から、実基板の測定点を推定しない。

## 資料取得状況

公式EK-RA8P1ページのDocumentationに、対象の
「Parallel Graphics Expansion Board 1 v1 for EK-RA8xx - Design Package」
（16.33 MB、2025-07-14）が存在することを内蔵ブラウザで確認。
取得操作でログイン認証ページへ移動したため、ZIP内容の確認には至っていない。
直接HTTP取得は403、Web検索では回路図本体を取得できず。
旧EK-RA6M3G用4.3インチ版やMIPI版の回路図は代用しない。

マニュアル6節で、ZIP名は`app_lcd-ek_par_1-v1-designpackage.zip`、
回路図PDF名は`app_lcd-ek_par_1-v1-schematics`と案内されている。

- https://www.renesas.com/en/design-resources/boards-kits/ek-ra8p1
- https://www.renesas.com/en/document/mat/parallel-graphics-expansion-board-1-v1-users-manual

## 測定対象の絞込み

必要なのはU2の実入力電源とEN（正常／白の比較）。LCD RESETはENとの接続確認後に優先度を決める。
現在表面から確実に案内できるのは既取得のTP3/TP8とTP4〜TP7だけで、新しい測定点は未確定。
回路図PDFまたは設計ZIP受領後、ネット名・部品番号・端子番号を追い、表側で測れる点の有無を確認する。
この段階で追加の測定・分解・はんだ付けは依頼しない。
