## e2 studio操作手順: ビルド／リンクオプションによる FLASH 削減（LTO / ICF）

### 関連Issue
- #184: ビルド／リンクオプションによる FLASH 削減（`--icf` / LTO の評価）

### 前提条件
- e2 studio 2025-12 (25.12.0) / FSP 6.3.0
- LLVM Embedded Toolchain for Arm (ATfE) 21.1.1
- CPU0 プロジェクト（`e2studio_CPU0`）が正常にビルドできる状態であること

### 結論（先に要点）

| オプション | FLASH 削減量 | 採否 |
|---|---:|---|
| **`-flto`（フル LTO）** | **−10,240 B** | **採用** |
| `-flto=thin`（ThinLTO） | +3,584 B（**増加**） | 不採用 |
| `-Wl,--icf=safe` | −512 B（LTO 併用時は追加効果なし） | 不採用（LTO 不採用時の代替案として保留） |
| `-Wl,--icf=all` | −512 B（`safe` と同じ） | 不採用 |
| 未使用 FSP スタックの削除 | 0 B（削除できる未使用スタックなし） | 対象なし |

---

## 1. 測定条件

- ベースラインコミット: `1be3dbd`（"Merge pull request #193 from grace2riku/feature/issue-192"、2026-08-13 時点の main）
- ビルド構成: `e2studio_CPU0` / Debug
- 測定は `e2studio_CPU0/Debug/makefile`・`*/subdir.mk` のフラグを直接書き換えて
  `make clean && make -j16 all` で実施（`Debug/` は `.gitignore` 対象・e2 studio が再生成するため
  リポジトリには影響しない）

### FLASH 使用量の定義

本ドキュメントの「FLASH used」は次で算出する。Issue #182〜#188 で使っている
「992 KiB 中 990,720 B 使用」と同じ数え方。

```
FLASH used = (ELF のセクション `.flash.endof` のアドレス) - FLASH_START
FLASH_START  = 0x02000000
FLASH_LENGTH = 0x000F8000 (1,015,808 B = 992 KiB)   ← Debug/memory_regions.lld
```

確認コマンド:

```bash
llvm-readelf -S mimamori_sense_CPU0.elf | grep '\.flash\.endof'
```

`llvm-size` の `text` はセクション間のアライメント埋めを含まないため、参考値として併記する。

---

## 2. 実測結果

| # | 設定 | `llvm-size` text (B) | FLASH used (B) | 使用率 | ベースライン比 |
|---|---|---:|---:|---:|---:|
| 0 | ベースライン（`-Wl,--icf=none` / LTO なし） | 798,782 | 799,232 | 78.68% | — |
| 1 | `-Wl,--icf=safe` | 798,390 | 798,720 | 78.63% | **−512 B** |
| 2 | `-Wl,--icf=all` | 798,342 | 798,720 | 78.63% | −512 B |
| 3 | `-flto=thin` | 802,218 | 802,816 | 79.03% | **+3,584 B** |
| 4 | `-flto`（フル LTO） | 788,530 | 788,992 | 77.67% | **−10,240 B** |
| 5 | `-flto` + `-Wl,--icf=safe` | 788,506 | 788,992 | 77.67% | −10,240 B |
| 6 | `-flto`（コンパイル行のみ・リンク行に付けない） | 788,530 | 788,992 | 77.67% | −10,240 B |

副次的な影響:

| 項目 | ベースライン | `-flto` | 差分 |
|---|---:|---:|---:|
| 内蔵 RAM 使用量（`.ram.endof` - `RAM_START`） | 1,359,872 B | 1,360,896 B | +1,024 B（原因は 4.1 参照） |
| SDRAM 配置セクション（`__sdram_noinit$$` 他） | 変化なし | 変化なし | 0 |
| フルリビルド時間（`make clean && make -j16`） | 1分28秒 | 1分43秒 | +16% |

### 各結果の読み方

**#1/#2 ICF**: LLD が畳み込んだのは 16 グループ（`safe`）／21 グループ（`all`）のみで、
いずれも 1〜数十バイトのリーフ関数（`lv_color_black` / `ethosu_mutex_unlock` /
`lv_clamp_height` など）。`all` にしても FLASH 使用量は `safe` と同じ（text 差 48 B）で、
関数ポインタ比較を壊すリスクに見合わない。

**#3 ThinLTO が増える理由**: ThinLTO はモジュール横断のサマリを使った限定的な
インライン化を行うが、`-Os` でもインライン展開によるコード増がデッドコード除去の
削減量を上回った。ThinLTO は e2 studio の GUI にも該当オプションがないため、
採用する場合は User defined options への手書きが必要になる。二重の理由で不採用。

**#4 フル LTO が減る理由**: 全翻訳単位の IR を 1 モジュールに統合してから最適化するため、
モジュールをまたいだインライン展開・定数伝播・内部化（internalize）が効く。
実測でも flash 内の関数シンボル数が **1,745 個 → 902 個**（−843 個）に減っており、
呼び出し規約に従った独立関数として存在していたコード（プロローグ／エピローグ／
引数セットアップ／`bl` 命令）が消えている。インライン展開によるコード複製よりも、
展開後に不要になったコードの削除量が上回った結果が −10,240 B。

**#4/#6**: `-flto` はコンパイル行だけに付いていればよい。リンク行にも付けた場合（#4）と
付けない場合（#6）で結果は完全に一致した。オブジェクトが LLVM ビットコードになれば
`ld.lld` がリンク時に LTO を実行するため。

**#5**: LTO がすでに同一コードを統合しているため、ICF を重ねても FLASH 使用量は変わらない
（text で 24 B の差のみ）。LTO を採用するなら ICF は不要。

---

## 3. 採用するオプション: `-flto`（フル LTO）

### 手順1: プロジェクト設定を開く

1. Project Explorer で **`e2studio_CPU0`** を右クリック → **Properties**
2. 左ツリーで **C/C++ Build → Settings** を選択
3. **Tool Settings** タブを開く

### 手順2: Optimization ページで LTO を有効化する

1. **Optimization** のページを開く
   - `Optimization Level = Optimize for code size (-Os)` が設定されているページと**同じページ**
   - このカテゴリはツールチェーン共通の設定で、コンパイラ／アセンブラ／リンカすべてに反映される
     （現に `-Os -ffunction-sections -fdata-sections` はコンパイル行とリンク行の両方に出力されている）
2. **`Link-time optimizer (-flto)`** のチェックボックスを **ON** にする
3. **Apply and Close**

> 設定は `.cproject` の
> `com.renesas.cdt.managedbuild.llvm.core.option.optimization.flto` として保存される。
> ベースライン状態ではデフォルト値（false）のため `.cproject` に記述がない。

### 手順3: リビルドして確認する

1. **Project → Clean...** で `e2studio_CPU0` をクリーン
2. リビルド
3. コンソールの `llvm-size` 出力が **text ≒ 788,530** になっていること
4. `Debug/mimamori_sense_CPU0.map` を退避しておく（切り戻し比較用）

### 手順4: 実機で全機能を確認する（必須）

LTO は関数配置・インライン展開を大きく変えるため、**起動確認を最優先**で行う。

- [ ] 電源投入で起動する（`[usermain] uT-Kernel 3.0 started.` が SCI8 に出る）
- [ ] `ntshell` プロンプトが出てコマンドが動く
- [ ] カメラ映像が LCD に表示される
- [ ] タッチ操作が効く
- [ ] 転倒検出（AI 推論）が動作する

起動しない場合は手順2のチェックを外して即座に切り戻す。

---

## 4. LTO のリスクと事前検証結果

Issue #184 で懸念として挙がっていた項目を、`-flto` ビルドの ELF に対して静的に検証した。
**いずれも問題なし**。ただし静的検証であり、手順4の実機確認を置き換えるものではない。

| 懸念 | 検証方法 | 結果 |
|---|---|---|
| `usermain()` の WEAK 上書きが壊れないか（BSP2 の `mtk3_bsp2/mtkernel/kernel/usermain/usermain.c` の `WEAK_FUNC` を `src/usermain.c` の強い定義で上書きしている） | LTO 版 ELF に `src/usermain.c` 固有の文字列 `[usermain] uT-Kernel 3.0 started` が含まれるか。WEAK 版は `return 0;` のみで各タスクを参照しない | 文字列あり。さらに `blink_task` / `ntshell_task` / `camera_task` / `lvgl_task` / `ai_inference_task` の 5 シンボルすべてが map に残存 → 強い定義が採用されている |
| ブート経路 `R_BSP_WarmStart` → `knl_start_mtkernel()` | LTO 版 map に `knl_start_mtkernel` が実体として存在するか | `.text.knl_start_mtkernel`（0xa8 B）として存在 |
| 割り込みベクタテーブル | `__flash_vectors$$` のエントリ数と非ゼロ数、`g_vector_table` / `__Vectors` シンボルの有無をベースラインと比較 | 両者とも 36 エントリ中 32 が非ゼロで一致。シンボルも両方存在 |
| `__attribute__((section))` によるセクション配置（`.sdram` / `.ospi*`） | セクションサイズをベースラインと比較 | `__sdram_noinit_nocache$$`=0x258000 / `__sdram_noinit$$`=0x1FA400 / `__sdram_zero$$`=0x118000 が完全一致 |
| 手書きアセンブラ（`dispatch.o` 等） | map 上でオブジェクトが残っているか | LTO 対象外の ELF オブジェクトとして個別に残存 |

### 採用による副作用（許容するデメリット）

#### 4.1 内蔵 RAM が +1,024 B 増える（1,359,872 → 1,360,896 B）

これは LTO のコード生成そのものではなく、**LTO によって `--gc-sections` の粒度が落ちる**ことが原因。

`--gc-sections` は「入力セクション」単位でしか削除できない。`-fdata-sections` は
通常の変数を `.bss.<name>` のように 1 変数 1 セクションへ分割するが、
`__attribute__((section(".noinit")))` のように**明示的にセクション名を指定した変数は分割されず**、
1 翻訳単位につき 1 つの `.noinit` にまとめられる。

- ベースライン: `.noinit` 入力セクションは **7 個**（μT-Kernel の 6 オブジェクト＋CMSIS-View）。
  未使用の `ra/arm/CMSIS-View/EventRecorder/Source/EventRecorder.o` 由来の `.noinit` は
  独立した入力セクションだったため `--gc-sections` が丸ごと削除できていた。
- `-flto`: 全モジュールが `mimamori_sense_CPU0.elf.lto.o` 1 つに統合されるため、
  `.noinit` 入力セクションは **1 個**に融合する。生きている μT-Kernel の制御ブロック
  （`knl_tcb_table` 等）と同居してしまい、EventRecorder 分だけを削除できなくなる。

LTO 版 map で新たに `__ram_noinit$$` に現れる 3 変数（`EventRecorder.c:194-198`）:

| シンボル | サイズ |
|---|---:|
| `EventBuffer` | 1,024 B |
| `EventFilter` | 128 B |
| `EventStatus` | 36 B |
| 計 | **1,188 B** |

差し引き（`knl_real_time_ofs` 8 B が消えた分と `__ram_zero$$` の 183 B 減を含む）で
正味 +1,024 B。RAM 全体は 0x1B0000 = 1,769,472 B なので余裕はある。

なお EventRecorder の**コードは LTO でも完全に削除されている**
（LTO 版 ELF に `EventRecord*` / `EventReg*` / `EventStart/Stop` の関数シンボルは 0 個）。
残っているのは上記の未初期化バッファのみ。

**解消したい場合**: `ra/arm/CMSIS-View` をビルド除外すれば +1,024 B は戻る
（`r_drw/r_drw_irq.c` や `ntshell/src/sample` と同じ除外方法）。
本 Issue は FLASH 削減が目的で RAM は逼迫していないため、本 Issue では対応しない。

#### 4.2 その他

2. **map ファイルのオブジェクト別内訳が失われる**。LTO 後は入力セクションの大半（1,078 個）が
   `mimamori_sense_CPU0.elf.lto.o` に集約され、「どのソースが何バイト使っているか」が
   追えなくなる（LTO 対象外の手書きアセンブラのみ個別に残る）。
   **#187（AI モデルの OSPI 移動）/ #188（LVGL の OSPI 移動）でサイズ内訳の分析が必要になった場合は、
   一時的に LTO を OFF にして map を取り直すこと。**
3. フルリビルド時間が +17%。

---

## 5. 不採用オプションの記録

### 5.1 `-flto=thin`

FLASH が **+3,584 B 増加**したため不採用（実測 #3）。ThinLTO のビットコード生成は
正常に行われていた（`.o` の先頭が `BC\xC0\xDE`＝LLVM ビットコード であることを確認済み）ので、
測定漏れではない。

### 5.2 `-Wl,--icf=safe`

単独では −512 B、`-flto` 併用時は追加効果なし。LTO を採用するため不採用。
**LTO を実機確認で断念した場合の代替案**として、以下の手順で有効化できる。

#### ICF の設定場所（重要）

Issue 本文では「Linker → その他オプション（User defined options）」に
`-Wl,--icf=safe` を追加する案が書かれていたが、**この方法では効かない**。

生成されるリンクコマンドでは User defined options が `--icf=none` より**前**に置かれる:

```
... -Wl,--gc-sections -Wl,--cref -Wl,-z,norelro -Wl,--icf=none ...
                                 ^^^^^^^^^^^^^^^ ここが User defined options
```

LLD の `--icf` は後勝ちのため、`--icf=safe --icf=none` の順になると `none` が採用される。
実測でも、この順序では畳み込みが 0 グループ・text は 798,782 B でベースラインと完全に同じだった。

正しい設定場所は専用のドロップダウン:

1. Properties → C/C++ Build → Settings → Tool Settings タブ
2. **Linker** のページ（カテゴリ配下ではなく Linker ノード直下。
   `Enable garbage collection of unused input sections(-gc-sections)` と同じページ）
3. **`Identical code folding`** のドロップダウンを
   **`Enable safe identical code folding`** に変更する
   - 選択肢は `Disable identical code folding`（デフォルト＝`--icf=none`）／
     `Enable safe identical code folding`（`--icf=safe`）／
     `Enable identical code folding`（`--icf=all`）の 3 つ
4. `.cproject` には
   `com.renesas.cdt.managedbuild.llvm.core.option.linker.common.identicalFormating` として保存される

`--icf=all` は FLASH 使用量が `safe` と同じ（削減量は変わらない）一方で、
LVGL のようにコールバックを関数ポインタで識別するコードを壊すリスクがあるため選択しない。

### 5.3 未使用 FSP ドライバ／中間コンポーネントの棚卸し

`configuration.xml` の Stacks に登録されている全モジュールについて、
ベースライン map の `__flash_readonly$$` への寄与を集計した（FSP 分の合計 17,578 B）。

| モジュール | FLASH 寄与 | 用途 | 判定 |
|---|---:|---|---|
| `r_iic_master`（2 インスタンス） | 2,624 B | タッチパネル / カメラ I2C | 使用中 |
| `r_glcdc` | 2,606 B | LCD | 使用中 |
| `bsp_clocks` | 2,124 B | BSP | 必須 |
| `r_mipi_csi` | 1,466 B | カメラ | 使用中 |
| `r_sci_b_uart` | 1,372 B | `src/jlink_console.c:135` の `R_SCI_B_UART_Open()` | 使用中 |
| `r_gpt` | 1,234 B | カメラ XCLK | 使用中 |
| `r_vin` | 1,064 B | カメラ | 使用中 |
| `rm_freertos_port` | 804 B | FreeRTOS | **#186 で削除対象** |
| `r_ioport` / `r_icu` / `rm_comms_i2c` / `r_mipi_phy` / `rm_ethosu` / `r_drw` 他 | 各 30〜600 B | 使用中 | 使用中 |
| **CMSIS-DSP** | **0 B** | 未使用 | `--gc-sections` がすでに全削除済み。スタックを外しても削減量 0 |

FreeRTOS 本体（`ra/aws/FreeRTOS/`）は 8,344 B で、#186 の削減見込み（9 KB）とほぼ一致する。

**結論: 本 Issue で外せる未使用スタックはない。** 未使用の CMSIS-DSP は
`--gc-sections` によりすでにコストゼロになっており、FreeRTOS 分は #186 の担当。

---

## 6. 切り戻し手順

1. Properties → C/C++ Build → Settings → Tool Settings → Optimization
2. **`Link-time optimizer (-flto)`** のチェックを外す
3. Clean してリビルド
4. `llvm-size` の text が 798,782 に戻ることを確認する

---

## 7. 参考: 削減後の残余量

```
FLASH used : 788,992 B (0xC0A00)  77.67%
FLASH free : 226,816 B (0x37600)
FLASH total: 1,015,808 B (992 KiB)
```

#182（LVGL 機能無効化）・#183（文字列リテラル削減）適用済みの状態からさらに 10 KiB 削減。
Issue #184 起票時点の「990,720 B 使用・空き 25,088 B」からは大きく改善している。
