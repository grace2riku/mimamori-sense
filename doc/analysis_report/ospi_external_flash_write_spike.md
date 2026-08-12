# 外部 OSPI フラッシュ書き込み手段のスパイク調査

- 対象 Issue: [#187](https://github.com/grace2riku/mimamori-sense/issues/187) Phase 2（ロードマップ上の **Phase 0**）
- 位置づけ: [#182 のロードマップコメント](https://github.com/grace2riku/mimamori-sense/issues/182#issuecomment-5262389314) における先行スパイク調査
- 調査日: 2026-08-12
- スコープ: **調査のみ。実装・設定変更は一切行っていない。**

---

## 1. 結論

**外部 OSPI フラッシュへの書き込み手段は成立する。** #187 / #188 が「書き込み手段が確立できないために丸ごと不成立」になるリスクは解消した。

根拠は 2 系統の独立した公式サポート手段が存在すること:

| | 手段 | 位置づけ | 確度 |
|---|---|---|---|
| **A** | e2 studio + J-Link（デバッグ実行時に内蔵 MRAM と一緒に書く） | 開発時の本命 | **高** — SEGGER 公式サポート＋同一ボードの実施例がリポジトリ内に存在 |
| **B** | Renesas Flash Programmer (RFP) V3.20.00 以降 | 量産書き込み／手段 A のフォールバック | **高** — EK-RA8P1 専用の外部メモリローダが手元の RFP V3.22 に実在 |

これに伴い、#187 Phase 2 に代替案として挙げられていた「初回書き込みモードの実装」「`ntshell` 経由のシリアル転送コマンドの実装」は**いずれも不要**になった。

### 副次的な発見（#187 Phase 3 の作業量が減る）

Phase 2 の調査過程で、#187 Phase 3 に列挙された作業項目のうち **2 件が FSP 標準機能でカバー済み**であることが判明した（詳細は §5）。

- 「リンカスクリプトに `.sdram_ospi_data` 相当のセクションを追加する」→ **不要**。FSP 生成の `fsp_gen.lld` に同等セクションが既にある。
- 「起動時の OSPI→SDRAM コピー処理を実装する」→ **不要**。FSP のスタートアップが自動でコピーする。

### 本調査の限界

本調査は**静的調査のみ**（ツールのインストール実体・リファレンス実装・公式ドキュメントの読み取り）。**実機での書き込み実証は未実施**。残る不確実性と実機確認項目は §7 に整理した。

---

## 2. 対象ハードウェアの確定

| 項目 | 値 | 根拠 |
|---|---|---|
| 外部フラッシュ品番 | **MX25LW51245G**（Macronix, 512 Mb = 64 MB, Octal） | `reference_projects/lv_port_renesas_ek_ra8p1/src/ospi_flash.c:11`、EK-RA8P1 UM §6.3.1 |
| デバイス ID | `0x3a86c2`（= C2 86 3A、ベンダ `0xC2` = Macronix） | `reference_projects/ruhmi-framework-mcu/application_examples/face_detection/src/external_memory/ospi_b_ep.h:15` |
| 接続 | OSPI0 CS1 | `e2studio_CPU0/Debug/memory_regions.lld:12-13` |
| メモリマップ | `0x90000000` から 128 MB | 同上（`OSPI0_CS1_START = 0x90000000` / `OSPI0_CS1_LENGTH = 0x08000000`） |
| リセットピン | **P1_06 = OSPI0 OM_Reset**（シンボル名 `OSPI_RESET`） | `e2studio/solution.xml`（`p106.ospi0.om_0_reset`、コメント `See SW4 in manual`） |

> P1_06 は本プロジェクトの `solution.xml` で**既に OSPI0 OM_Reset のペリフェラル機能に割り当て済み**（EK-RA8P1 BSP のボードデフォルト）。手段 A が要求するリセット操作（§3.3）と競合しない。

---

## 3. 手段 A: e2 studio + J-Link

### 3.1 成立根拠 1 — SEGGER が RA8P1 の外部 OSPI フラッシュを正式サポート

インストール済みの J-Link DLL のフラッシュバンク名テーブルに、RA8P1 の `Code MRAM` と隣接して以下の文字列が存在する（`C:\Program Files\SEGGER\JLink\JLink_x64.dll`、バイトオフセット 12,317,960 付近）:

```
Code MRAM
External OSPI flash CS1
External OSPI flash CS0
```

RA8P1 のデバイス名は `R7KA8P1KF` / `R7KA8P1KF_CPU0` / `R7KA8P1JF` / `R7KA8P1JF_CPU0` の 4 種で、外部フラッシュ用の別デバイス名（他の Renesas 品種にある `..._SPIBSC_SerialFlash` のような派生名）は存在しない。つまり **OSPI は通常のデバイス定義のフラッシュバンクとして扱われる**＝デバイス選択を変えずにそのまま書ける。

SEGGER のナレッジベース（[kb.segger.com/Renesas_RA8P1](https://kb.segger.com/Renesas_RA8P1)）も 4 リージョンすべてを "J-Link support" / "Flasher support" と明記している:

| リージョン | 先頭アドレス |
|---|---|
| External OSPI1 flash CS0 | `0x70000000` |
| External OSPI1 flash CS1 | `0x78000000` |
| External OSPI0 flash CS0 | `0x80000000` |
| **External OSPI0 flash CS1** | **`0x90000000`** ← 本ボードの搭載先 |

本プロジェクトが既に使用しているデバイス名も `R7KA8P1KF_CPU0` である（`e2studio_CPU0/mimamori_sense_CPU0 Debug_Flat.launch:20` の `-t R7KA8P1KF_CPU0`）。

### 3.2 成立根拠 2 — 同一ボードでの実施例がリポジトリ内にある

`reference_projects/lv_port_renesas_ek_ra8p1/`（LVGL 公式の EK-RA8P1 移植プロジェクト）に、**OSPI へデータを書くための仕組みが一式そろっている**。

| ファイル | 役割 |
|---|---|
| `generate_ospi_srecord.bat:9-11` | ELF を「OSPI 以外」と「OSPI のみ」の 2 つの S-record に分割し、OSPI 側をバイトスワップする |
| `.cproject:22` | 上記 bat をポストビルドステップとして実行（`postbuildStep="${workspace_loc:/${ProjName}}/generate_ospi_srecord.bat ${ProjName}"`） |
| `EK_RA8P1_LVGL Debug_Flat.launch:33-37` | 生成した 2 つの srec をダウンロードイメージとして登録 |
| `RA8x1_Reset_OSPI.JLinkScript` | フラッシュ書き込み前に外部フラッシュをハードウェアリセットする |
| `srecord/srec_cat.exe` | バイトスワップ用の SRecord 1.65 同梱（`srecord/readme.txt`） |

同じ `RA8x1_Reset_OSPI.JLinkScript` は Renesas 公式の QuickStart サンプルにも同梱されている（`reference_projects/quickstart_ek_ra8p1_ep/e2studio/script/RA8x1_Reset_OSPI.JLinkScript`、`quickstart_ek_ra8p1_ep Debug_Flat.launch:126` で参照）。**Renesas / LVGL の双方が、この構成を EK-RA8P1 の標準的な OSPI 書き込み手段として採用している。**

> ⚠ 注意: この 2 プロジェクトのコミット済みビルド成果物では `__ospi0_cs1_readonly$$` のサイズは 0（`Debug/EK_RA8P1_LVGL.map:115-117`）。つまり**「仕組みは完備しているが、その状態ではまだ OSPI に実データを置いていない」**。仕組みの正当性は §3.1 の SEGGER 側サポートと §3.3 の各要素の必然性から裏付けられるが、**実データを載せた書き込みの実証は本プロジェクトで行う必要がある**（§7）。

### 3.3 仕組みの内訳とハマりどころ

手段 A は 4 つの要素がすべて揃って初めて機能する。1 つでも欠けると失敗するため、それぞれの必然性を整理する。

#### (1) 書き込み前に外部フラッシュを単線 SPI モードへ戻す ★必須

**問題**: アプリが一度でも外部フラッシュを OPI/DOPI（8 ビット）モードへ遷移させると、J-Link のフラッシュローダは単線 SPI での通信を前提にしているため**次回以降の書き込みが失敗する**。FSP の Issue [renesas/fsp#330](https://github.com/renesas/fsp/issues/330) に報告された既知問題。

**対策**: Renesas が公式に配布する `RA8x1_Reset_OSPI.JLinkScript` を e2 studio のデバッグ構成に設定する。中身は `HandleBeforeFlashProg()` で **OM_Reset (P1_06) を High→Low→High にトグルして外部フラッシュをハードウェアリセット**し、単線 SPI モードへ戻すもの:

```c
JLINK_MEM_WriteU8 (0x40400d14, 0x00);        // PWPR: B0WI クリア
JLINK_MEM_WriteU8 (0x40400d14, 0x40);        // PWPR: PFSWE セット（PFS 書き込み許可）
JLINK_MEM_WriteU32(0x40400858, 0x00000005);  // P1_06 PFS: GPIO 出力 High
JLINK_MEM_WriteU32(0x40400858, 0x00000004);  //          → Low
JLINK_MEM_WriteU32(0x40400858, 0x00000005);  //          → High
JLINK_MEM_WriteU8 (0x40400d14, 0x00);        // PWPR: 書き込み保護に戻す
JLINK_MEM_WriteU8 (0x40400d14, 0x80);
```

`0x40400858` は P1_06 の PFS レジスタ（PFS ベース `0x40400800` + port1 `0x40` + pin6 `0x18`）、`0x40400d14` は PWPR。§2 で確認した P1_06 = OM_Reset と一致する。

**代替策**: RFP でマスイレース後に電源リセット（fsp#330 に記載）。

#### (2) DOPI モードで読むならバイトスワップが必要 ★モード依存

EK-RA8P1 ユーザーズマニュアル §6.3.1「OSPI Flash Read / Write Byte Order」より:

> The MX25LW51245GXDI00 flash device uses the byte order shown in Figure 28 ... when writing or reading data in DOPI mode. This order (D1, D0, D3, D2 ...) differs from the order that is used when reading or writing data in SPI mode (D0, D1, D2, D3, ...).

J-Link は**単線 SPI モードで書き込む**（∵ (1)）。一方アプリが **DOPI (8D-8D-8D) モードで読む**場合、2 バイト単位でバイト順が入れ替わる。そのため書き込むイメージをあらかじめスワップしておく必要がある。これが `generate_ospi_srecord.bat:11` の `srec_cat ... -byte_swap` の理由。

> **本プロジェクトでの判断ポイント（Phase 3 で決める）**: AI モデルデータは起動時に 1 回だけ SDRAM へコピーするだけで、実行時は SDRAM から NPU が読む。したがって **OSPI を DOPI にせず単線 SPI モードのまま運用すれば、バイトスワップは不要**になり、ポストビルド処理と `srec_cat` 依存を丸ごと省ける。代償は起動時コピーの所要時間のみ。DOPI にするかどうかは起動時間の実測値を見て決めるのが妥当。

#### (3) OSPI 分は ELF から分離して別イメージとしてダウンロードする

バイトスワップは OSPI 部分にだけ適用しなければならないため、ELF を 2 分割する必要がある（`generate_ospi_srecord.bat:9-10`）:

```bat
llvm-objcopy %1.elf -O srec -R __ospi0_cs1_readonly$$  %1_no_OSPI_Data.srec
llvm-objcopy -O srec -j __ospi0_cs1_readonly$$ %1.elf  "OSPI_Data.srec"
..\srecord\srec_cat "OSPI_Data.srec" -byte_swap -o "OSPI_Data_swapped.srec"
```

`__ospi0_cs1_readonly$$` は FSP がリンカスクリプトで定義するセクション名で、**本プロジェクトの `e2studio_CPU0/Debug/fsp_gen.lld:160-168` にも同名で存在する**（`*(.ospi0_cs1)` と `*(.ospi0_cs1_code)` を収集）。

なお (2) でバイトスワップ不要と判断した場合は、この分割自体が不要になり、**ELF をそのままダウンロードするだけで J-Link が内蔵 MRAM と外部 OSPI の両方を書く**構成にできる可能性が高い。

#### (4) フラッシュローダの作業用 RAM

`.jlink` 設定ファイルの `[GENERAL]` に `WorkRAMAddr = 0x22060000` / `WorkRAMSize = 0x10000` が指定される。本プロジェクトの CPU0 RAM 範囲（`0x22000000` + `0x1b0000`、`e2studio_CPU0/Debug/memory_regions.lld:2-3`）に収まっており、リファレンス 3 プロジェクトすべてで同一値。**特別な対応は不要**と判断する。

### 3.4 mimamori-sense に適用する場合の差分（実装は Phase 3 で行う）

現状との差分を整理する。**本 Issue のスコープ外なので変更は加えていない。**

| 項目 | 現状 | 必要な変更 |
|---|---|---|
| JLinkScript | 未設定（`e2studio_CPU0/mimamori_sense_CPU0 Debug_Flat.launch:52` が `...jlink.jlink.scriptFile" value=""`） | `RA8x1_Reset_OSPI.JLinkScript` をリポジトリに追加し、デバッグ構成の [Debug Tool Settings] → J-Link Script File に設定 |
| ダウンロードイメージ | 1 件のみ（同 `:26-28`） | DOPI 採用時のみ、srec 2 件構成に変更 |
| ポストビルド | 未設定（`.cproject` に `postbuildStep` なし） | DOPI 採用時のみ、srec 分割＋バイトスワップを追加 |
| OSPI ドライバ | 未導入（`e2studio_CPU0/ra/fsp/` に `r_ospi_b` が存在しない。`bsp_ospi_b.c/h` のみ） | #187 Phase 1 で FSP Stacks に OSPI-B を追加 |
| OSPI 起動時初期化 | 無効（`e2studio_CPU0/src/hal_warmstart.c:98-102` が `#if BSP_CFG_OSPI_B_STARTUP_ENABLED && defined(BSP_CFG_OSPI_B_STARTUP_FN)` で除外） | 同上 |

> ⚠ マルチコアプロジェクト特有の注意: 本プロジェクトのデバッグ構成は Flat / Multicore / Attach の複数系統がある。ダウンロードイメージと JLinkScript の設定を**どの構成に入れるか**は実機確認時に決める必要がある。

---

## 4. 手段 B: Renesas Flash Programmer (RFP)

### 4.1 成立根拠 — EK-RA8P1 専用の外部メモリローダが公式提供されている

RFP V3 リリースノート（R20UT4953EJ2200 Rev.22.00, Jul.01.26）§3.4.3「Release Information on **RFP V3.20.00**」:

> **Support for programming of external flash memory mounted on EK-RA8P1 and EK-RA8E2**
> Applies to: **MX25LW51245G**
> The RFP now supports the erasure and programming of external memory above.

品番・ボードともに §2 で確定した本ボードの構成と完全に一致する。

さらに **本 PC には RFP V3.22 が既にインストール済み**で、EK-RA8P1 専用ローダの実体が存在する:

```
C:\Program Files (x86)\Renesas Electronics\Programming Tools\Renesas Flash Programmer V3.22\
  Resources\ExternalMemory\RA\EK-RA8P1.hex     (10,153 B, 2025-12-10)
  rfp-cli.exe                                   (V3.22.00.000, Package V3.22.00 [1 Jan 2026])
```

`rfp-cli.exe -h` にも対応オプションがある:

```
-external-loader <name>       : Enables external flash programming and loading of external flash loader file
-show-external-loader <name>  : Shows detailed external loader information
-list-external-loader         : Shows a list of external loader names
```

### 4.2 使い方（リリースノート §3.6.2 より）

- **GUI**: [External Memory Settings] タブで [Enable external memory settings] を選択する
- **CLI**: `rfp-cli -device RA -tool jlink -external-loader EK-RA8P1 ...`
- ⚠ **既存の RFP プロジェクトを開いた場合は外部フラッシュ操作が動作しない。プロジェクトを新規作成し直すこと**（リリースノートに明記）

### 4.3 デュアルコアプロジェクトとの相性

リリースノート §3.2.3（V3.22.00）:

> **Support for the new RPD file format**
> The RFP now supports the new RPD file format generated by e2 studio and thus supports dual-core devices in the RA8 series.

本プロジェクトは CPU0/CPU1 のデュアルコア構成であり、**この対応が入った V3.22 が導入済み**である点は好都合。

### 4.4 位置づけ

手段 A（デバッグ実行のワンクリックで内蔵・外部の両方が書ける）の方が開発サイクルには適する。**手段 B は、手段 A が実機で躓いた場合のフォールバック、および将来の量産書き込み手段**として押さえておく。

---

## 5. 副次的発見: #187 Phase 3 の作業量削減

Phase 2 の調査中に、#187 Phase 3 の記述と実際の FSP 生成物との間に食い違いを見つけた。

### 5.1 リンカスクリプトの追加は不要

#187 は「`.sdram_ospi_data` / `.ospi_device_1` セクションはプロジェクトのリンカスクリプトに存在しない」としているが、これは **RUHMI サンプル独自のセクション名**（`e2studio_CPU0/src/ai_application/application_config.h:25-27` のコメントに書かれている名前）を探した結果である。

FSP は同等の機能を**別名で既に生成している**（`e2studio_CPU0/Debug/fsp_gen.lld`）:

| 用途 | FSP のセクション名 | 出力セクション | fsp_gen.lld |
|---|---|---|---|
| OSPI に置いたまま参照（XIP） | `.ospi0_cs1` / `.ospi0_cs1_code` | `__ospi0_cs1_readonly$$` | `:160-168` |
| OSPI に格納し起動時に SDRAM へ展開 | `.sdram_from_ospi0_cs1` / `.sdram_code_from_ospi0_cs1` | `__sdram_from_ospi0_cs1$$`（`}> SDRAM AT > OSPI0_CS1`） | `:59-67` |

**したがって専用リンカスクリプトの追加も `mtkernel.ld` への追記も不要。** モデルデータには `BSP_PLACE_IN_SECTION(".sdram_from_ospi0_cs1")` を付ければよい。

### 5.2 起動時の OSPI→SDRAM コピー処理の実装も不要

FSP のスタートアップが、リンカが生成したコピーリストを走査して**外部メモリ向けの初期化を自動実行する**。

`e2studio_CPU0/ra/fsp/src/bsp/cmsis/Device/RENESAS/Source/system.c:138-157`:

```c
static void SystemRuntimeInit (const uint32_t external)
{
    ...
    for (uint32_t i = 0; i < g_init_info.copy_count; i++)
    {
        if (external == g_init_info.p_copy_list[i].type.external)
        {
            memcpy(g_init_info.p_copy_list[i].p_base, g_init_info.p_copy_list[i].p_load, ...);
        }
    }
}
```

呼び出し側（同 `:470-476`）:

```c
    /* Call Post C runtime initialization hook. */
    R_BSP_WarmStart(BSP_WARM_START_POST_C);
#if BSP_CFG_C_RUNTIME_INIT
    /* Initialize data placed in external memories. */
    SystemRuntimeInit(1);
```

`BSP_WARM_START_POST_C` は SDRAM を初期化するフック（`e2studio_CPU0/src/hal_warmstart.c` 内で `R_BSP_SdramInit()` を呼ぶ）であり、**その直後に外部メモリ向けコピーが走る**。つまり `.sdram_from_ospi0_cs1` に置いたデータは、`main()` 到達時点で既に SDRAM 上に展開されている。

> ⚠ 前提: OSPI が読める状態になっているのは `BSP_WARM_START_POST_CLOCK` で `R_BSP_OspiBInit()` が呼ばれた後（`hal_warmstart.c:98-102`）。これは `POST_C` より前なので順序は成立する。ただし現状この呼び出しは `BSP_CFG_OSPI_B_STARTUP_ENABLED` が無効のため**コンパイル時に除外されている**（`#if`。実行時分岐ではない）。#187 Phase 1 で FSP 設定を有効化することが前提条件。

### 5.3 #187 Phase 3 の改訂案

| 元の作業項目 | 判定 |
|---|---|
| 1. リンカスクリプトにセクションを追加 | **削除**（§5.1） |
| 2. `mera/` の生成物にセクション属性を付与（スクリプト化 or ラッパ） | **維持** — 引き続き必要。ただし付与するセクション名は `.sdram_from_ospi0_cs1` |
| 3. `AI_MODEL_ALLOCATION` を変更 | **維持**（ただし現状 `ai_cmd.c` の表示用途のみで実体がないため、実質は 2 の付帯作業） |
| 4. 起動時の OSPI→SDRAM コピー処理を実装 | **削除**（§5.2） |

---

## 6. #187 Phase 2 に書かれていた代替案の扱い

| 代替案 | 判定 |
|---|---|
| 内蔵フラッシュに一時的に置いたデータを起動時に外部フラッシュへ書く「初回書き込みモード」 | **不要**。手段 A / B が成立したため。そもそも内蔵フラッシュの空きを要求する本末転倒な案だった |
| `ntshell` 経由でシリアル転送し外部フラッシュに書き込むコマンド | **書き込み手段としては不要**。ただし #187 Phase 1 の「`md` / `mw` で外部フラッシュ領域を読み書きして疎通確認する」は、書き込み結果のベリファイ手段として引き続き有用（`e2studio_CPU0/src/cmd_utils.h:79-82` で 0x70000000〜0xA0000000 は既に OSPI として定義済み） |

---

## 7. 残リスクと実機確認項目

静的調査では潰しきれなかった項目。**Phase 3 着手時、または軽く実機を触れるタイミングで先に確認しておくとよい。**

| # | 確認項目 | なぜ必要か | 失敗時の代替 |
|---|---|---|---|
| 1 | **SW4 の設定** | `solution.xml` の P1_06 コメントに `See SW4 in manual` とある。EK-RA8P1 UM に「Switch Configuration Definitions (SW4)」「Permitted Switch Configuration (SW4)」の表があり、Octo-SPI と Arduino ヘッダの排他などが SW4 で切り替わる。**ボードのスイッチが OSPI 側になっていないと何も書けない** | UM Table 3 / Table 4 に従って設定 |
| 2 | **実データを載せた書き込み＆ベリファイ** | リファレンス 2 プロジェクトはいずれも OSPI セクションが空のビルドしかコミットされていない（§3.2）。実データでの成功は未確認 | 手段 B (RFP) に切り替え |
| 3 | **オンボード J-Link OB で足りるか** | SEGGER KB は "J-Link support" と記載するが、EK-RA8P1 のオンボード J-Link OB で外部フラッシュローダが動作するかは未確認 | 外付け J-Link を使用、または手段 B |
| 4 | **DOPI にするか単線 SPI のままにするか** | バイトスワップとポストビルド処理の要否が決まる（§3.3 (2)）。起動時の 446 KB コピー時間の実測が判断材料 | — |
| 5 | **書き込み時間の実測** | 446 KB の追加書き込みでデバッグサイクルがどれだけ延びるか。開発効率に直結する | 開発中は内蔵配置、リリースビルドのみ OSPI 配置という使い分け |
| 6 | **マルチコア構成での設定反映先** | Flat / Multicore / Attach のどのデバッグ構成に JLinkScript とダウンロードイメージを設定するか（§3.4） | — |

---

## 8. 推奨する次のアクション

1. **#182（LVGL 未使用機能の無効化）を予定どおり Phase 1 の先頭として進める。** 本調査により「外部フラッシュ化がいつでも実行可能な選択肢として残っている」ことが確定したため、Phase 1 の判断ゲート（空き 150 KB 前後で #188 を見送る）は安心して適用できる。
2. **実機に触れるタイミングで §7 の #1（SW4）と #2（実書き込み）だけ先に潰しておく。** 30 分程度で済み、Phase 3 の見積り精度が大きく上がる。
3. **#187 の Phase 3 記述を §5.3 の改訂案で更新する。** 作業項目 4 件のうち 2 件が不要になっている。

---

## 9. 参考資料

### リポジトリ内

- `reference_projects/lv_port_renesas_ek_ra8p1/generate_ospi_srecord.bat`
- `reference_projects/lv_port_renesas_ek_ra8p1/RA8x1_Reset_OSPI.JLinkScript`
- `reference_projects/lv_port_renesas_ek_ra8p1/EK_RA8P1_LVGL Debug_Flat.launch:33-37, 83`
- `reference_projects/lv_port_renesas_ek_ra8p1/src/ospi_flash.c`
- `reference_projects/quickstart_ek_ra8p1_ep/e2studio/script/RA8x1_Reset_OSPI.JLinkScript`
- `e2studio_CPU0/Debug/fsp_gen.lld:59-67, 160-168`
- `e2studio_CPU0/ra/fsp/src/bsp/cmsis/Device/RENESAS/Source/system.c:138-157, 470-476`

### ローカルにインストール済みのツール

- `C:\Program Files\SEGGER\JLink\JLink_x64.dll`（フラッシュバンク名テーブル）
- `C:\Program Files (x86)\Renesas Electronics\Programming Tools\Renesas Flash Programmer V3.22\Resources\ExternalMemory\RA\EK-RA8P1.hex`

### 外部

- [SEGGER Knowledge Base — Renesas RA8P1](https://kb.segger.com/Renesas_RA8P1)
- [renesas/fsp Issue #330 — RA8M1 / RA8D1: J-Link OSPI Flash Loader fails if flash in OSPI mode](https://github.com/renesas/fsp/issues/330)
- [Renesas Flash Programmer V3 Release Note (R20UT4953EJ2200 Rev.22.00)](https://www.renesas.com/en/document/rln/renesas-flash-programmer-v3-release-notes)
- [EK-RA8P1 v1 User's Manual (R20UT5309EG0101)](https://www.mouser.com/pdfDocs/r20ut5309eg0101-ek-ra8p1-v1-um.pdf) — §6.3.1 OSPI Flash Read / Write Byte Order、Table 3/4 SW4
- [RA FSP Documentation — OSPI Flash (r_ospi_b)](https://renesas.github.io/fsp/group___o_s_p_i___b.html)
