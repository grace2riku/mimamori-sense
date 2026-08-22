# e2 studio操作手順書: S-005-1: FSPプロジェクト設定 - オーディオ出力関連モジュールの追加

## 改訂履歴

| 版 | 内容 |
|---|---|
| rev1 | 方式A（GPT PWM + 圧電ブザー / P810）を選定 |
| rev2 | EK-RA8P1 に DA7212 Audio CODEC が実装されており、J33 にスピーカー接続済みであることが判明したため、方式C（SSIE0 / I2S + DA7212）へ全面差し替え。方式A / 方式B は付録に降格（削除はしていない） |
| rev3 | EK-RA8P1 回路図・ユーザーズマニュアルを参照し、rev2 の TBD 13箇所を確定（1-8節）。I2C アドレス 0x1A、J41 は初期設定で接続済み・作業不要、MCLK は MCU の GPT2 → PD06、Bit Clock Source は Internal AUDIO_CLK に確定。MIPI カメラとの併用可を確認。残る未確定は DA7212 データシート / RA8P1 HW マニュアルが必要な4件のみ（1-9節） |
| rev4 | 初回ビルドで発生した `VECTOR_NUMBER_SSI0_RXI` 未定義エラーを受けて、DTC サブスタックの Transmission / Reception 取り違えに対する予防策と復旧手順を追加（4-1節の⚠、9-3節 2b、9-5節トラブルシューティング、11-2 / 11-4 のチェック項目） |
| **rev5（本書）** | **実機プロジェクトで Generate → ビルド成功を確認し、生成物の実測値で記述を修正。(a) コールバックのスタブは本Issueでは不要（`-flto` + `--gc-sections` により未参照の設定構造体ごと除去されるため。`llvm-nm` で実測）→ 9-4節を全面書き換え。(b) `g_comms_i2c_codec` の生成先は `common_data.c` ではなく `hal_data.c` → 9-2節・11-4節を修正** |

## 対象Issue

- Issue #45: S-005-1: FSPプロジェクト設定 - オーディオ出力関連モジュールの追加

## 対象プロジェクト

- `e2studio_CPU0/configuration.xml`（Stacks タブ / Pins タブ）
- クロック変更は不要（`e2studio/solution.xml` は変更しない）

## リファレンスプロジェクト

- `reference_projects/lv_port_renesas_ek_ra8p1/configuration.xml`（SSIE ピン割り当ての記述のみ参照）
- `reference_projects/quickstart_ek_ra8p1_ep/e2studio/configuration.xml`（GPT インスタンス設定の書式のみ参照）

> **重要**: 両リファレンスプロジェクトとも **オーディオ関連のスタック（SSI / DAC / codec）を一切追加していません**。
> ヒットするのはピンのシンボル名（`AUDIO_BCLK` 等）とピン接続設定だけです。
> したがって本Issueは「リファレンスの設定をそのまま流用する」ことができず、**新規に設定する必要があります**。
> 本書で「リファレンスと同値」と明記していない設定値は、すべて本Issue固有の判断です。

---

## 0. サマリ

| 項目 | 内容 |
|---|---|
| 採用方式 | **方式C: SSIE0（I2S, Master）→ DA7212 Audio CODEC → J33 スピーカー** |
| 追加する FSP モジュール | `I2S (r_ssi)` ch0 ＋ その TX サブスタック `Transfer (r_dtc)` ＋ MCLK 用 `Timer, General PWM (r_gpt)` **ch2** ＋ AUDIO_CLK 用 `Timer, General PWM (r_gpt)` **ch1** ＋ コーデック制御用 `I2C Communication Device (rm_comms_i2c)` |
| Pins タブの作業 | **I2S 信号線は作業不要**（P402〜P406 がボードデフォルトで SSIE に割当済み。うちコーデックが実際に使うのは BCLK=P403 / WCLK=P404 / DATIN=P405 / DATOUT=P406）。**追加するのは PD06（MCLK）のみ**。GPT1 の GTIOC1A にはピンを割り当てない |
| コーデック I2C アドレス | **0x1A（7bit）** — 回路図 p18 で確定 |
| ボード側の配線作業 | **不要**。J41 は初期設定で 1-2 / 3-4 とも短絡済み（1-8-3節） |
| カメラとの併用 | **可能**。MIPI モードでは P405/P406 を使わない（1-8-5節） |
| クロック設定 | 変更なし |
| DMAC | **使わない**（`r_ssi` の転送サブスタックは DTC のみ） |
| 残る未確定事項 | 4件（1-9節）。いずれも **DA7212 データシート / RA8P1 HW マニュアル**が必要で、ボード資料では解決不可。**本Issueは暫定値のまま Generate → ビルドまで完了できる** |

---

## 1. 調査結果（受け入れ条件「音声出力方式が選定・決定されている」の根拠）

### 1-1. RA8P1 の内蔵DAC: **存在する（DAC_B、12bit × 2ch）**

Issue本文では「内蔵DAC: 有無」が調査項目になっていました。結論は **存在します**。ただし
**従来の `r_dac` / DAC12 ではなく `DAC_B` という別ペリフェラル** です。ドライバも `r_dac_b` を使う必要があります。

| 確認項目 | 結果 | 根拠（file:line） |
|---|---|---|
| DAC_B ペリフェラルの実装 | 有り | `e2studio_CPU0/ra/fsp/src/bsp/mcu/ra8p1/bsp_peripheral.h:57` `#define BSP_PERIPHERAL_DAC_B_PRESENT (1)` |
| DAC_B のチャネルマスク | 0x3（ch0, ch1 の2ch） | `bsp_peripheral.h:58` `BSP_PERIPHERAL_DAC_B_CHANNEL_MASK (0x3U)` |
| DAC_B のユニット数 / ユニットあたりch数 | 2ユニット × 1ch = 計2ch | `e2studio_CPU0/ra/fsp/src/bsp/mcu/ra8p1/bsp_feature.h:213-214` |
| ビット深度 | 12bit | `e2studio_CPU0/ra/fsp/src/bsp/cmsis/Device/RENESAS/Include/R7KA8P1KF_core0.h:5393` `@brief 12-bit D/A converter (R_DAC_B0)` |
| レジスタ実体 | R_DAC_B0 / R_DAC_B1 | 同上 `:64710-64711`（`R_DAC_B0_BASE 0x40233000` / `R_DAC_B1_BASE 0x40233100`） |
| 旧DAC12 / DAC8 | **非搭載** | `bsp_peripheral.h:59-62` `BSP_PERIPHERAL_DAC8_PRESENT (0)` / `BSP_PERIPHERAL_DAC12_PRESENT (0)`、`bsp_feature.h:221,228` |
| 出力アンプ（DAAMPCR） | **無し** | `bsp_feature.h:219` `BSP_FEATURE_DAC_HAS_OUTPUT_AMPLIFIER (0UL)` → **DAC出力でスピーカーを直接駆動できない。外部アンプ必須** |

**FSPスタック上の注意**: `r_dac_b` モジュールは **DTC/DMAC のサブスタックを一切持ちません**。
`Renesas##HAL Drivers##all##r_dac_b####6.3.0.xml:41-46` の `<requires>` は
`interface.mcu.dac_b`（ペリフェラル存在確認）1件のみで、転送ドライバの `requires` がありません。

→ **方式B の詳細は付録B**（DA7212 が使える以上、内蔵 DAC を選ぶ理由が無くなったため降格）。

### 1-2. I2S（SSIE）: **存在する（SSIE0 / SSIE1 の2ユニット）** ← 本Issueで採用

| 確認項目 | 結果 | 根拠（file:line） |
|---|---|---|
| SSIE ペリフェラル | R_SSI0 / R_SSI1 の2ユニット | `R7KA8P1KF_core0.h:14627` `@brief Serial Sound Interface Enhanced (SSIE) (R_SSI0)`、`:64793-64794`（ベースアドレス） |
| FSP から見たチャネル | ch0 / ch1 の2つとも有効 | `Renesas##BSP##ra8p1##device####6.3.0.xml:268-270`（`interface.mcu.ssie` / `.0` / `.1` を provides）、`:823-824`（チャネルマスク 0x3） |
| ELCイベント | SSI0 TXI/RXI/INT、SSI1 TXI/RXI/INT | `e2studio_CPU0/ra/fsp/src/bsp/mcu/ra8p1/bsp_elc.h:157-162` |
| SSI1 の制約 | TXI と RXI が **同一イベント番号 0x0B9** を共有 | `bsp_elc.h:160-161`。FSP側にも制約あり（`Renesas##HAL Drivers##all##r_ssi####6.3.0.xml:30-32` "Receive and Transmit interrupts cannot both be enabled on channel 1"）→ **SSIE0 を使う** |
| ch0 の制約 | TXI か RXI のどちらかが必須 | `r_ssi` xml `:27-29` |
| 全割り込み同一優先度の制約 | 有効化した割り込みはすべて同じ優先度でなければならない | `r_ssi` xml `:33-36` |
| FSPドライバ | `r_ssi`（Stacks タブ表示名 `Connectivity > ... I2S (r_ssi)`） | `r_ssi` xml `:26` `display="Connectivity|${module.driver.i2s.name} I2S (r_ssi)"` |
| 転送サブスタック | **DTCのみ**（DMACは選択不可） | 同xml `:67`, `:82` が `interface="interface.driver.transfer_on_dtc"`。同ファイル内に `dmac` の出現は 0 件 |

> **Issue本文の想定との差異**: Issue本文には「DMAコントローラ（DMAC）: 音声データの連続出力用」とありますが、
> FSPの `r_ssi` が用意している転送サブスタックは **DTC** です。
> **DMAC ではなく DTC を追加してください**（4章）。

#### Bit Clock Source の選択肢は2つだけ

`r_ssi` の `Bit Clock Source(available only in Master mode)`（`r_ssi` xml `:136-138`）は
`enum.driver.i2s.audio_clock` を参照しており、RA8P1 では次の2択です
（`Renesas##BSP##ra8p1##fsp####6.3.0.xml:8297-8300`）。

| 表示名 | enum id | C の値 | 意味 |
|---|---|---|---|
| External AUDIO_CLK | `module.driver.i2s.audio_clock.audio_clock_external` | `SSI_AUDIO_CLOCK_EXTERNAL` (=0) | **P402（SSIE_AUDIO_CLK 入力ピン）** から外部クロックを入れる |
| Internal AUDIO_CLK | `module.driver.i2s.audio_clock.audio_clock_gtioc1a` | `SSI_AUDIO_CLOCK_INTERNAL` (=1) | **GPT1 の GTIOC1A 出力**を内部接続する（enum id が `..._gtioc1a`）。**SSIE 側にピン設定は不要** |

C の enum 値は `Renesas.RA.6.3.0.pack` 内 `ra/fsp/inc/instances/r_ssi.h:30-34` で確認できます。

> **ドライバは GPT を設定しません（重要）**。`r_ssi` が Internal AUDIO_CLK に対して行うのは
> **SSICR.CKS ビットを立てることだけ**です
> （`Renesas.RA.6.3.0.pack` 内 `ra/fsp/src/r_ssi/r_ssi.c:259-266`。
> `ssicr |= (uint32_t) p_extend->audio_clock << SSI_PRV_SSICR_CKS_BIT;`。
> 同ファイルを `gpt` / `GPT` で検索してもヒットは無く、GPT を Open/Start するコードは存在しない）。
> **Internal AUDIO_CLK を選ぶ場合、GPT1 のインスタンス追加とアプリからの `R_GPT_Open()`/`R_GPT_Start()` はユーザーの責任です。**
> これは Issue #46 の作業になります（10-2節）。

### 1-3. GPT（MCLK 生成の候補）

| 確認項目 | 結果 | 根拠（file:line） |
|---|---|---|
| GPTチャネル数 | 14ch（GPT0〜GPT13） | `R7KA8P1KF_core0.h:64728-64741` |
| カウンタ幅 | 全14chが32bit | `bsp_feature.h:315` `BSP_FEATURE_GPT_32BIT_CHANNEL_MASK (0x3FFFUL)` |
| 使用中のチャネル | **GPT12 のみ**（カメラXCLK）。GPT1・GPT2 とも空き | `e2studio_CPU0/configuration.xml:843-845`（`g_timer_camera_xclk`, channel=12）。`timer_on_gpt` のインスタンスは `:843` の1件のみ |
| GPTドライバの Pin Output Support | **既に Enabled** | `e2studio_CPU0/configuration.xml:974` `config.driver.gpt.output_support_enable.enabled` |
| GPTのカウントソースクロック | **PCLKD = 250 MHz** | 下記「クロック源の確定」参照 |

> **クロック源の確定（コンパイル時分岐のため要注意）**
>
> `e2studio_CPU0/ra/fsp/src/r_gpt/r_gpt.c:1745` の `R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKD)` は
> **`#if BSP_PERIPHERAL_GPT_GTCLK_PRESENT && !GPT_CFG_GPTCLK_BYPASS` の `#else` 側**にあります
> （`r_gpt.c:1737-1747`。`R_GPT_PwmOutputDelayInitialize()` 内の `:1130-1141` も同じ構造）。
> RA8P1 は **GTCLK を搭載している**ため（`bsp_peripheral.h:99` `BSP_PERIPHERAL_GPT_GTCLK_PRESENT (1)`）、
> 「PCLKD が使われる」と言えるのは `GPT_CFG_GPTCLK_BYPASS` が真だからです。根拠の連鎖:
>
> | 段 | 内容 | file:line |
> |---|---|---|
> | 1 | `BSP_CFG_GPT_COUNT_CLOCK_SOURCE (1) /* GPT Src: PCLKD */` が定義されている | `e2studio_CPU0/ra_gen/bsp_clock_cfg.h:56` |
> | 2 | 定義済みなので `#else` 側が採られ `GPT_CFG_GPTCLK_BYPASS = BSP_CFG_GPT_COUNT_CLOCK_SOURCE = 1` | `e2studio_CPU0/ra_cfg/fsp_cfg/r_gpt_cfg.h:12-16` |
> | 3 | よって `1 && !1 == 0` → **PCLKD 側がコンパイルされる** | `r_gpt.c:1737`, `:1130` |
>
> PCLKD = PLL1P 1 GHz ÷ 4 = **250 MHz**（`e2studio_CPU0/ra_gen/bsp_clock_cfg.h:13`, `:38`）。
>
> **これは実行時に切り替わらないコンパイル時の確定であり、同時に「現在のクロック設定に依存する事実」です。**
> `e2studio/solution.xml` の Clocks タブで GPT Src を GPTCLK に変更すると、
> `BSP_CFG_GPT_COUNT_CLOCK_SOURCE` が 0 になり GTCLK 側がコンパイルされ、カウントクロックは
> GPTCLK = PLL2R 480 MHz ÷ 2 = **240 MHz** に変わります
> （`bsp_clock_cfg.h:27`, `:54`, `:55`）。**本Issueではクロック設定を変更しません。**

#### オーディオ用クロックとしての 250 MHz の制約（必読）

**250 MHz からは標準的なオーディオマスタクロックを整数分周で作れません。**

| 目標 | 250 MHz からの分周比 | 整数か |
|---|---|---|
| 12.288 MHz | 250 / 12.288 ≒ 20.35 | **不可** |
| 11.2896 MHz | 250 / 11.2896 ≒ 22.14 | **不可** |
| 24.576 MHz | 250 / 24.576 ≒ 10.17 | **不可** |

参考: GPT Src を GPTCLK（240 MHz）に変えても 240 / 12.288 ≒ 19.53 で整数になりません。
GPT から出せる近傍値は 250 / 20 = **12.5 MHz**、250 / 22 ≒ 11.364 MHz、250 / 24 ≒ 10.417 MHz です。
→ **DA7212 が要求する MCLK 周波数と、そのトレランス／PLL 対応可否をデータシートで必ず確認してください**（1-9節 #1）。

### 1-4. DMAC / DTC

| 確認項目 | 結果 | 根拠（file:line） |
|---|---|---|
| DMACチャネル数 | 8ch（DMAC0〜DMAC7） | `R7KA8P1KF_core0.h:64714-64721` |
| DTC の搭載 | 有り | `Renesas##BSP##ra8p1##device####6.3.0.xml:53` `<provides interface="interface.mcu.dtc" />`, `:409-410` |
| 現プロジェクトでの使用 | **未使用**（DMAC/DTCモジュールとも未追加） | `e2studio_CPU0/configuration.xml:429-921` のモジュール一覧に `transfer_on_dmac` / `transfer_on_dtc` が存在しない |
| DTC ベクタテーブルの配置先 | **リンカスクリプトに既に用意されている** | `e2studio_CPU0/Debug/fsp_gen.lld:143-147` に `__ram_dtc_vector$$ (NOLOAD) { *(.fsp_dtc_vector_table) } > RAM`。DTC ドライバ側は `Renesas.RA.6.3.0.pack` 内 `ra/fsp/src/r_dtc/r_dtc.c:26-35, 90` で `.fsp_dtc_vector_table` に 1024バイト境界で配置する |
| DTC ベクタテーブルのサイズ | エントリ数 = `BSP_ICU_VECTOR_NUM_ENTRIES`（現在 20） × 4バイト。ただし1024バイトアラインのため RAM 上のパディングが発生しうる | `r_dtc.c:21, 90`、`e2studio_CPU0/ra_gen/vector_data.h:75` `#define BSP_ICU_VECTOR_NUM_ENTRIES (20)` |

> **リンカスクリプトの追加作業は不要です。** `.fsp_dtc_vector_table` 出力セクションは
> 自動生成される `e2studio_CPU0/Debug/fsp_gen.lld:143-147` に既に存在します
> （`e2studio_CPU0/script/fsp.lld` は6行の `INCLUDE fsp_gen.lld` のみ）。

### 1-5. EK-RA8P1 のピン使用状況

実際にビルドに入るピン設定の実体は `e2studio_CPU0/ra_gen/pin_data.c`（自動生成）です。

#### SSIE0 の5本は **ボードデフォルトで割当済み**

| ピン | シンボル名 | 機能 | pin_data.c |
|---|---|---|---|
| P402 | `PMOD1_RST` | SSIE AUDIO_CLK（入力） | `ra_gen/pin_data.c:163-165` |
| P403 | `AUDIO_BCLK` | SSIE0 SSIBCK0 | `:166-168` |
| P404 | `AUDIO_WCLK` | SSIE0 SSILRCK0 | `:169-171` |
| P405 | `AUDIO_SSITX` | SSIE0 SSITXD0 | `:172-175` |
| P406 | `AUDIO_SSIRX_DCAM_D2` | SSIE0 SSIRXD0 | `:176-179` |

5本すべて `IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_SSI` になっています。

シンボル名の定義は `e2studio_CPU0/configuration.xml:1129-1133`、PD06 = `AUDIO_MCLK` は `:1265`。
ボードパック側の定義は `Renesas.RA_board_ra8p1_ek.6.3.0.pack` 内
`.module_descriptions/Renesas##BSP##Board##ra8p1_ek####6.3.0##configuration.xml:513-524`
（`p402.ssie.audio_clk` / `p403.ssie0.ssibck0` / `p404.ssie0.ssilrck0` / `p405.ssie0.ssitxd0` / `p406.ssie0.ssirxd0`）
および `:926-932`（`ssie.audio_clk.p402` / `ssie.mode.custom.free` / `ssie0.mode.custom.free` / `ssie0.ssibck0.p403` / `ssie0.ssilrck0.p404` / `ssie0.ssirxd0.p406` / `ssie0.ssitxd0.p405`）。

本プロジェクトの `pincfg` にも `ssie.mode.custom.free` / `ssie0.mode.custom.free` が入っています
（`e2studio_CPU0/configuration.xml:1374-1375`）。

参考として `reference_projects/lv_port_renesas_ek_ra8p1/configuration.xml:1529-1538, 1930-1936` にも同じ割り当てがあります。

> **P402 の用途矛盾（要確認）**: P402 のシンボル名は **`PMOD1_RST`**（`configuration.xml:1129`）なのに、
> ボードデフォルトで **SSIE AUDIO_CLK** に割り当てられています
> （`pin_data.c:163-165`、ボードパック `:513`）。しかも `p402.comment` は "Enable when connected"
> （`configuration.xml:1303`、ボードパック `:265`）。
> **【解決済み】** UM 表32（1-8-1節）に **AUDIO_CLK / P402 の行は存在しません**。
> DA7212 が受け取るクロックは BCLK・WCLK・MCLK の3本だけで、**SSIE の外部 AUDIO_CLK 入力は
> ボード上で使われていません**。P402 のボードデフォルト割当は実質的に無害な設定です。
> → Bit Clock Source は **Internal AUDIO_CLK（GPT1）を採用**します（1-8-4節 / 2-4節）。
> P402 のピン設定は**変更しません**（既存のまま放置）。

> **J41 の正体【解決済み】**: P405 / P406 の "See J41 in manual" コメント
> （`e2studio_CPU0/configuration.xml:1304-1305`、ボードパック `:266-267`）が指す J41 は、
> **パラレル(DVP)カメラ使用時に CODEC を P405/P406 から切り離すための「開放リンク」**です（UM 6.6節）。
>
> - J41-1 = P406、J41-2 = DATOUT(U14-C7)、J41-3 = P405、J41-4 = DATIN(U14-C5)
> - **初期設定は 1-2 短絡＋3-4 短絡＝コーデック接続状態**（UM 表2。1-8-3節）
>
> → **出荷状態のままで DA7212 が P405/P406 に繋がっています。ジャンパ作業は不要です。**
> 本プロジェクトは MIPI カメラなので P405/P406 を明け渡す必要もありません（1-8-5節）。

#### MCLK 出力候補: **PD06 は SSIE 機能を持たない**

PD06 の Port Capabilities は
`ESWM_GMII/MII0: ET0_RXD7 / GPT2: GTIOC2A / IRQ: IRQ18 / SDHI0: SD0WP / USB HS: USBHS_OVRCURA` のみで、
**SSIE 機能を持ちません**（`PinCfgR7KA8P1KxxCAC.xml:33769`）。
つまりボード名 `AUDIO_MCLK` に対して MCU 側から出せるのは **GPT2 の GTIOC2A（PWM）だけ**です。

PD06 は現在未設定です（`ra_gen/pin_data.c:611-629` の PORT_13 は PIN_00,02,03,04,05,07 のみ。
`configuration.xml:1351` の `pd06.comment` も "Enable when connected"）。

GTIOC2A を出せるピンは P103 / P113 / P713 / PD06 の4つですが（`PinCfgR7KA8P1KxxCAC.xml:3186, 4931, 20576, 33915`）、
他の3本は使用中のため **PD06 一択**です。

| ピン | シンボル名 | 使用状況 |
|---|---|---|
| P103 | `OSPI_SIO2_ARDUINO_D10_SS`（`configuration.xml:1090`） | 使用中（`ra_gen/pin_data.c:38`） |
| P113 | `SDRAM_DQ4`（`:1100`） | 使用中（`pin_data.c:74`） |
| P713 | `PARLCD_D21R5`（`:1186`） | 使用中（`pin_data.c:331`） |
| **PD06** | `AUDIO_MCLK`（`:1265`） | **未使用** |

#### GPT1 GTIOC1A のピンは割り当てられない（割り当ててはいけない）

Internal AUDIO_CLK は GPT1 の GTIOC1A を内部接続で使います。GTIOC1A を出せる**ピン**は
P105 / P209 / P405 / P509（`PinCfgR7KA8P1KxxCAC.xml:3585, 6226, 10493, 14174`）ですが、
このうち **P405 は SSIE0 の SSITXD0 そのもの**（1-5節の表）で、他も OSPI / JTAG / SDRAM で使用中です。

→ **GPT1 に GTIOCA のピン割り当てをしてはいけません。** 内部接続のみで使います（7章）。

### 1-6. I2C バス構成（コーデック制御に直結する重要事項）

Issue #45 の作業には「コーデック制御用 I2C」が必要になるため、既存の I2C 構成を精査しました。
**configuration.xml の表示値と実際に生成される値が食い違う箇所があるので注意してください。**

| インスタンス | configuration.xml 上の channel | **生成コード上の実 channel** | slave | 用途 |
|---|---|---|---|---|
| `g_i2c_master0`（`g_comms_i2c_bus0` の下） | `0`（`configuration.xml:664`） | **`1`**（`e2studio_CPU0/ra_gen/common_data.c:442` `.channel = 1`） | `0x38`（`common_data.c:443`） | タッチパネル |
| `g_i2c_master_camera` | `1`（`configuration.xml:831`） | `1`（`e2studio_CPU0/ra_gen/hal_data.c:162`） | `0x3C`（`hal_data.c:163`） | カメラ（OV5640） |

`g_i2c_master0` の channel が 0 → 1 に化けるのは、`rm_comms_i2c` の I2C Shared Bus が
子の I2C マスタの channel を上書きするためです:
`Renesas##Middleware##all##rm_comms_i2c####6.3.0.xml:46`
`<override property="module.driver.i2c_master.channel" value="${module.driver.comms_i2c_bus.channel}"/>`。
そして `module.driver.comms_i2c_bus.channel` は **1** です（`e2studio_CPU0/configuration.xml:659`）。

**つまり、タッチパネルもカメラも同じ IIC1 = SYS_I2C（P511 / P512）上にいます。**

| 項目 | 内容 | 根拠 |
|---|---|---|
| SYS_I2C のピン | P511 = `SYS_I2C_SDA`, P512 = `SYS_I2C_SCL` | `configuration.xml:1154-1155` |
| ピン設定の実体 | 両方 `IOPORT_PERIPHERAL_IIC` で設定済み | `ra_gen/pin_data.c:241-249` |
| ピン capability | P511 / P512 とも IIC1 | `PinCfgR7KA8P1KxxCAC.xml:14369`, `:14547` |
| Pins タブ側 | `iic1.mode.custom.free` が有効 | `configuration.xml:1358` |

> **既存の排他制御方式（Issue #46 で必ず踏襲すること）**
>
> IIC1 上に **2つの独立した `R_IIC_MASTER` インスタンス**（`g_i2c_master_camera` と `g_i2c_master0`）が存在するため、
> アプリ側で時間的に排他しています。タッチパネル側は
> `e2studio_CPU0/src/port/lv_port_indev.c:530-533` で `camera_thread_i2c_done()`
> （宣言 `e2studio_CPU0/src/camera_thread_api.h:72`、実装 `src/camera_thread_entry.c:731`）を待ってから
> `g_i2c_master0` を Open しています。
>
> さらに重要な点として、**現在のビルドには `rm_comms_i2c` のバスミューテックス／セマフォが
> そもそも存在しません**。これは実行時の NULL 代入ではなく**コンパイル時に確定**しています:
>
> | 段 | 内容 | file:line |
> |---|---|---|
> | 1 | 構造体の `p_bus_recursive_mutex` / `p_blocking_semaphore` は `#if BSP_CFG_RTOS` の内側で宣言されている | `e2studio_CPU0/ra/fsp/inc/instances/rm_comms_i2c.h:89-93` |
> | 2 | `BSP_CFG_RTOS` は **0** に解決される（`#if (RA_NOT_DEFINED) != (RA_NOT_DEFINED)` は両方偽、`RA_NOT_DEFINED` は 0） | `e2studio_CPU0/ra_cfg/fsp_cfg/bsp/bsp_cfg.h:13-22` |
> | 3 | よって**両メンバーは構造体に存在せず**、`lv_port_indev.c:590-593` の NULL 代入も `#if BSP_CFG_RTOS` で囲まれているため**コンパイルされない** | `lv_port_indev.c:589-593` |
>
> つまり μT-Kernel 移行後の現状では、**`rm_comms_i2c` は常時コールバックモードで動作し、
> ミドルウェア層のバス排他は一切効いていません**。`lv_port_indev.c:555-588` のコメントが
> 「バス排他は不要（`:577-579` に **the touch panel is the ONLY rm_comms_i2c user**）」と述べているとおり、
> 現状の安全性は「利用者が1つしかいない」ことだけに依存しています。
>
> **DA7212 を同じバスに追加すると、この唯一の根拠が崩れます。**
> しかもミューテックスを「NULL でなくする」という復旧手段が使えない（メンバー自体が無い）ため、
> **排他はアプリ側で実装するしかありません**。Issue #46 では「バス排他をどう設計し直すか」を
> 必ず扱ってください（10-2節）。

### 1-7. DA7212 用の FSP ドライバは **存在しない**

FSP 6.3.0 の modules ディレクトリに codec 系ミドルウェアは
`Renesas##Middleware##all##rm_audio_playback_pwm####6.3.0.xml`（PWM 再生用）のみで、
**DA7212 / DA7xxx 用のドライバはありません**。

→ **DA7212 のレジスタ設定（初期化シーケンス、出力パス、音量、ミュート解除）は自前実装**になります（10-3節）。

### 1-8. 回路図・ユーザーズマニュアルで確定した事項

**出典**（いずれも `doc/reference/` に格納）:

- 回路図: `ek-ra8p1-v1-schematic.pdf` p18「Audio Devices / SSIE/I2S Audio Codec」
- UM: `r20ut5309jg0104-ek-ra8p1-v1-um.pdf`（R20UT5309JG0104 Rev.1.04）
  6.6 節「DA7212 Audio CODEC」（印刷 p40-42）、表32（印刷 p41）、表2「ジャンパ初期設定」（印刷 p15）、
  8.3 節「カメラ拡張ボード」＋表35/表36（印刷 p49-52）

#### 1-8-1. DA7212 の接続（UM 表32）

| DA7212 ピン | コネクタピン | 説明 | RA8P1 |
|---|---|---|---|
| BCLK | － | DAI ビットクロック | **P403**（`AUDIO_BCLK`） |
| WCLK | － | DAI ワードクロック(L/R 選択) | **P404**（`AUDIO_WCLK`） |
| DATIN | J41-4 | DAI データ入力（＝**再生**に使う） | **P405**（`AUDIO_SSITX`） |
| DATOUT | J41-2 | DAI データ出力（録音。本Issueでは未使用） | **P406**（`AUDIO_SSIRX_DCAM_D2`） |
| MCLK | － | マスタクロック | **PD06**（`AUDIO_MCLK`） |
| SDA / SCL | － | I2C データ / クロック | **P511 / P512**（SYS_I2C = IIC1） |
| SP_P / SP_N | **J33-1 / J33-2** | スピーカ出力 | － |

電源: VDD_SP = **+5 V**（スピーカドライバ）、VDD_A = +1.8 V、VDD_IO = +3.3 V。

**これは 1-5節で FSP のピンマッピングから導いた内容と完全に一致します。**
MCLK が PD06 に来ている＝**MCU 側が MCLK を生成して供給する**構成であり、
PD06 で使える該当機能は GPT2 の GTIOC2A のみ（`PinCfgR7KA8P1KxxCAC.xml:33769`）なので、
**5章（GPT2 で MCLK 生成）は実施必須**です。

#### 1-8-2. I2C スレーブアドレス = **0x1A（7bit）** ✅確定

回路図 p18 に `I2C Address: 0x1A (7-bits)` と明記されています。
6章の Slave Address はこの値で確定です。IIC1（SYS_I2C）上の3番目のデバイスになります
（既存: タッチパネル 0x38、カメラ 0x3C。1-6節）。**アドレス重複はありません。**

#### 1-8-3. J41 は**初期設定で両方短絡済み** ✅確定・作業不要

UM 表2「ジャンパ初期設定」（印刷 p15）:

| 位置 | 初期設定 | 機能 |
|---|---|---|
| J41 | **ジャンパピン 1-2 短絡** | U14-C7 (DATOUT) を P406 に接続 |
| J41 | **ジャンパピン 3-4 短絡** | U14-C5 (DATIN) を P405 に接続 |

表32 の脚注「P405 と P406 はそれぞれピン J41-3 と J41-1 に接続されています」および
回路図 p18（P405→J41-3 / DATIN(U14-C5)→J41-4、P406→J41-1 / DATOUT(U14-C7)→J41-2）とも整合します。

→ **出荷時のまま DA7212 は P405/P406 に接続されています。ジャンパの追加作業は不要です。**
再生に必要なのは DATIN 側（J41 3-4）だけですが、初期状態で両方入っているため何もしません。

#### 1-8-4. P402 の用途矛盾は「無害」と判断してよい ✅解決

1-5節で挙げた「P402 のシンボル名が `PMOD1_RST` なのに SSIE AUDIO_CLK が割り当たっている」問題ですが、
**UM 表32 に AUDIO_CLK / P402 の行は存在しません**。DA7212 が MCU から受け取るクロックは
BCLK・WCLK・MCLK の3本だけで、**SSIE の外部 AUDIO_CLK 入力はボード上で使われていません**。

→ **2-4節の Bit Clock Source は C-2（Internal AUDIO_CLK / GPT1）を採用**します。
C-1（External AUDIO_CLK）は、P402 にクロック源が繋がっていないため**選択できません**。

#### 1-8-5. MIPI カメラと Audio CODEC は**共存できる** ✅確定（重要）

UM 8.3 節に「P405 と P406 は Audio CODEC と共有されているため、カメラは Audio CODEC と同時に
使用することはできません」という記述がありますが、**これはパラレル(DVP)モード限定の話です**。

| モード | SW4-6 | P405 / P406 の扱い | 出典 |
|---|---|---|---|
| パラレル(DVP) | ON | **CAM_D2 = P405 / CAM_D3 = P406** として使用 → **競合する** | UM 表35（印刷 p51） |
| **MIPI(CSI)** | **OFF（初期設定）** | **P405/P406 は一切現れない**。使うのは MIPI 差動レーンと P511/P512(I2C)、P709、P501、P010 | UM 表36（印刷 p52） |

本プロジェクトは **MIPI CSI-2 構成**（`g_vin0` → `g_mipi_csi0` → `g_mipi_phy0`。`configuration.xml`）で、
使用ピンも表36 と一致します（P709 = `CAMERA_RESET`, P501 = カメラ XCLK, P010 = `CAMERA_IRQ`,
P511/P512 = カメラ I2C）。SW4-6 の初期設定も MIPI モードです。

→ **カメラを止めずにオーディオを鳴らせます。** 排他制御は不要です
（ただし I2C バスは共有するため 1-6節のバス排他の話は別途成立します）。

### 1-9. 残る未確定事項（ボード資料では解決できないもの）

以下は EK-RA8P1 の回路図・UM には記載がなく、**DA7212 データシート**または
**RA8P1 ハードウェアマニュアル**が必要です。

| # | 未確認事項 | 必要な資料 | 影響する設定 |
|---|---|---|---|
| 1 | **DA7212 が許容する MCLK 周波数**。PCLKD 250 MHz からは 12.288 MHz を整数分周できず、最寄りは **12.5 MHz**（÷20）。DA7212 の PLL がこれを受けられるか | DA7212 データシート | 5-2節 Period |
| 2 | **fs 誤差 ±0.15% の許容可否**（2-4節 C-2 の表） | DA7212 データシート | 5-4節 GPT1 Period |
| 3 | **DA7212 の DAI ワード長設定**（16bit で合わせられるか） | DA7212 データシート | 3-2節 Bit Depth / Word Length |
| 4 | **Internal AUDIO_CLK（GTIOC1A → SSIE）に GPT の出力イネーブル（GTIOR.OAE）が必要か** | RA8P1 ハードウェアマニュアル SSIE/GPT 章 | 5-4節 GTIOCA Output Enabled |

> #1〜#3 は **Issue #46（コーデック初期化）で DA7212 データシートを読む際に併せて確定**させれば足ります。
> 本Issue（FSP 設定）では暫定値で Generate → ビルドまで通せます。

---

## 2. 方式比較と選定

### 2-1. 方式選定を差し替えた理由

rev1 では**方式A（GPT PWM + 圧電ブザー）**を選定していました。その主な根拠のひとつが
「**J41 の先にオーディオコーデックが実装されているのか、単なる I2S ヘッダなのかを、
リポジトリ内の情報だけでは判定できない**」という保留でした。

その後、ユーザーから次の情報が提供され、**この保留は解消されました**。

- EK-RA8P1 に **DA7212 Audio CODEC が実装されている**
- **J33 にスピーカーを接続済み**
- **DA7212 から音声を再生したい**（＝要件が「ブザー音」ではなく「オーディオ再生」）

したがって:

- 「外部 I2S DAC 基板が別途必要」という方式C のコストは **発生しません**（ボードに載っている）。
- 「外部アンプが必要」という方式B のコストは **回避できます**（DA7212 のスピーカーアンプを使う）。
- 「圧電ブザー1個で済む」という方式A の利点は、**ユーザー要件（DA7212 で鳴らす）を満たさないため無効**です。

→ **方式C を採用します。方式A / 方式B は将来の代替として付録A / 付録B に残します。**

### 2-2. 比較表（rev2 更新版）

| 観点 | 方式A: GPT PWM（付録A） | 方式B: 内蔵DAC_B + 外部アンプ（付録B） | **方式C: SSIE0(I2S) + DA7212（採用）** |
|---|---|---|---|
| 出力先 | 圧電ブザー（P810 / GTIOC10A） | 外部アンプ + スピーカー（P014 / DA0） | **ボード実装の DA7212 → J33 スピーカー** |
| 出力ピン | P810（要追加） | P014（要追加） | **P403〜P406（ボードデフォルトで割当済み・追加不要）** |
| 追加FSPモジュール | `r_gpt` × 1 | `r_dac_b` + `r_gpt` + ELC + `r_dmac` | `r_ssi` + `r_dtc` + `r_gpt` × 2（MCLK / AUDIO_CLK）+ `rm_comms_i2c` デバイス |
| 追加ピン設定 | GTIOC10A = P810 の1本 | DAC120 有効化 + P014 | **I2S は不要**。MCLK を出す場合のみ PD06 |
| 必要な追加ハードウェア | 他励式圧電ブザー | **外部アンプ基板必須**（`bsp_feature.h:219` により DAC 内蔵アンプ無し）+ LPF | **無し**（DA7212 実装済み・スピーカー接続済み） |
| 音質 | 矩形波のみ | 任意波形 12bit | **任意波形 16/24bit。音声メッセージ再生も可** |
| CPU負荷 | ほぼゼロ | 中 | 低（DTC が FIFO へ転送） |
| DMA | 不要 | DMAC を自力で配線 | **DTC 1ch**（`r_ssi` のサブスタックとして正式サポート） |
| RAM使用 | 0バイト | ダブルバッファ数KB | ダブルバッファ数KB + DTCベクタテーブル（1-4節） |
| FSPリファレンス設定 | 無し | 無し | 無し |
| 実装コスト | 小 | 中〜大 | **中〜大**（コーデック初期化シーケンスの自前実装が必要。1-7節） |
| ユーザー要件との適合 | **不適合**（ブザー音しか出せない） | 部分適合（外部基板が要る） | **適合** |
| 主なリスク | — | — | **MCLK / fs の周波数精度が DA7212 の許容範囲か未確認**（1-9節 #1・#2）。**IIC1 のバス排他設計の見直しが必要**（1-6節） |

### 2-3. 選定結果: **方式C（SSIE0 I2S + DA7212）を採用**

理由:

1. **ユーザー要件そのもの**。DA7212 から音声を再生したいという要求に直接応える唯一の方式。
2. **追加ハードウェアがゼロ**。DA7212 も J33 スピーカーも既に存在する。
3. **ピン追加がゼロ**（I2S 信号線）。P403〜P406 はボードデフォルトで SSIE0 に割当済み（`ra_gen/pin_data.c:166-179`）ため、既存機能（SDRAM / GLCDC / Ethernet / OSPI / MIPI）のピンを奪わない。
4. **FSP が転送を正式サポート**。`r_ssi` の DTC サブスタック（`r_ssi` xml `:67`）で、CPU 介在を最小化して連続再生できる。
5. **Issue #46 / #47 の作業内容（サンプリングレート、ビット深度、DMA転送、ダブルバッファ、正弦波LUT）が素直に成立する**（10-2節）。

### 2-4. Bit Clock Source の選択: 2案とその成立条件

**結論: C-2（Internal AUDIO_CLK）を採用します。** UM 表32 に AUDIO_CLK / P402 の行が無く、
**ボードは SSIE の外部 AUDIO_CLK 入力を使っていない**ことが確定したため、C-1 は選択できません（1-8-4節）。
以下の比較は判断の記録として残します。

| | C-1: External AUDIO_CLK（**採用しない**） | **C-2: Internal AUDIO_CLK（採用）** |
|---|---|---|
| クロック源 | P402（SSIE_AUDIO_CLK 入力） | GPT1 の GTIOC1A（内部接続） |
| 成立条件 | ボードが P402 にオーディオクロックを供給していること → **UM 表32 に該当行が無く、供給されていない**。P402 のシンボル名も `PMOD1_RST`（`configuration.xml:1129`） | GPT1 インスタンスを追加し、アプリが Open/Start すること（`r_ssi` は GPT を触らない。1-2節） |
| 追加ピン設定 | 不要（P402 は既に割当済み） | 不要（**GTIOC1A のピン割り当てはしない**。1-5節） |
| サンプリングレート精度 | 供給周波数が 12.288 MHz なら**誤差ゼロ**（下表） | PCLKD 250 MHz 由来のため **±0.15% 程度の誤差**（下表） |
| リスク | P402 にクロックが来ていなければ **BCLK が出ず無音**になる | 誤差がコーデックの許容範囲か要確認 |

#### C-1 の分周比（**採用しない**。外部 AUDIO_CLK = 12.288 MHz を仮定した場合の参考値）

Word Length = 16 Bits のとき 1フレーム = 2ch × 16bit = **32 BCLK**。BCLK = fs × 32、AUDIO_CLK = BCLK × div。

| fs | BCLK | 必要な Bit Clock Divider | 選択肢に存在するか（`r_ssi` xml `:140-152`） |
|---|---|---|---|
| 8 kHz | 256 kHz | /48 | あり（`Audio Clock / 48`） |
| 16 kHz | 512 kHz | /24 | あり |
| 24 kHz | 768 kHz | /16 | あり |
| 32 kHz | 1.024 MHz | /12 | あり |
| 48 kHz | 1.536 MHz | /8 | あり |

（44.1 kHz 系を使う場合の AUDIO_CLK は 11.2896 MHz 系になります。）

#### C-2 の GPT1 設定値（Bit Clock Divider = /1、Word Length = 16 Bits）

AUDIO_CLK = BCLK なので、GPT1 の出力周波数 = fs × 32。GPT1 のカウントクロックは PCLKD = 250 MHz（1-3節）。

| 目標 fs | 目標 BCLK | GPT1 period_counts N | 実 BCLK = 250 MHz / N | 実 fs | 誤差 |
|---|---|---|---|---|---|
| 8,000 Hz | 256,000 Hz | 977 | 255,885 Hz | 7,996.4 Hz | -0.045% |
| **16,000 Hz** | **512,000 Hz** | **488** | **512,295 Hz** | **16,009.2 Hz** | **+0.058%** |
| 22,050 Hz | 705,600 Hz | 354 | 706,215 Hz | 22,069.2 Hz | +0.087% |
| 32,000 Hz | 1,024,000 Hz | 244 | 1,024,590 Hz | 32,018.4 Hz | +0.058% |
| 44,100 Hz | 1,411,200 Hz | 177 | 1,412,429 Hz | 44,138.4 Hz | +0.087% |
| 48,000 Hz | 1,536,000 Hz | 163 | 1,533,742 Hz | 47,929.4 Hz | -0.147% |

DA7212 は BCLK / WCLK に従うスレーブとして動くため、fs にこの程度の誤差があってもピッチが僅かにずれるだけです
（非同期リサンプリングは発生しません）。**ただし DA7212 の PLL / SRC 設定がこの fs を許容するかはデータシートで要確認**（1-9節 #2）。

### 2-5. DMAC ではなく DTC を使う

Issue本文の「DMAコントローラ（DMAC）」は本Issueでは**追加しません**。理由:

- `r_ssi` が受け付ける転送サブスタックは `interface.driver.transfer_on_dtc`、すなわち **DTC のみ**（`r_ssi` xml `:67`, `:82`）。同ファイルに `dmac` の記述は0件。
- `config.driver.ssi.dtc_enable` のデフォルトは Enabled（`r_ssi` xml `:9-12`）で、
  「DTC support が Enabled なら DTC の TX か RX スタックを追加せよ」という制約がある（同 xml `:57-61`）。
  → **DTC スタックを追加しないなら DTC Support を Disabled にしなければビルド構成エラーになります。**

---

## 3. 手順1: I2S (r_ssi) モジュールの追加

### 3-1. モジュールの追加

1. e2 studio で `e2studio_CPU0/configuration.xml` を開く
2. **Stacks** タブを選択
3. 左側のツリーで **HAL/Common** を選択
4. **New Stack** ボタンをクリック
5. **Connectivity** > **I2S (r_ssi)** を選択して追加

> **配置場所の根拠**: 本プロジェクトは RTOS に μT-Kernel を使用しており、FSP 側の RTOS 設定は無し
> （`e2studio_CPU0/configuration.xml:11` `<option key="#RTOS#" value="_none"/>`）。
> FSP スタックのコンテキストは `_hal.0`（HAL/Common）のみです（`configuration.xml:922-957`）。
> 既存の全モジュールも HAL/Common に配置されています。

> **メニュー階層の根拠**: `Renesas##HAL Drivers##all##r_ssi####6.3.0.xml:26` の
> `display="Connectivity|${module.driver.i2s.name} I2S (r_ssi)"`。`|` の前がカテゴリ名（Connectivity）です。

### 3-2. プロパティの設定

追加した I2S (r_ssi) を選択し、Properties パネルで以下を設定します。
プロパティ名は `r_ssi` xml の `display` 属性、設定値は `option` の `display` 属性そのままです。

| プロパティ名 | 設定値 | 根拠 / 備考 |
|---|---|---|
| Name | `g_i2s_audio` | `r_ssi` xml `:101`。本Issue固有の命名 |
| Channel | `0` | `r_ssi` xml `:106`。**SSIE0 必須**。SSIE1 は TXI/RXI が同一 ELC イベント 0x0B9（`bsp_elc.h:160-161`）で、FSP 側にも「ch1 では TXI と RXI を同時に有効化できない」制約がある（`r_ssi` xml `:30-32`）。またボードの I2S 配線は SSIE0（1-5節） |
| Operating Mode (Master/Slave) | `Master Mode` | `r_ssi` xml `:109-112`。MCU が BCLK / WCLK を生成する。この設定は SSICR.MST と SSIFCR.AUCKE を立てる（`r_ssi.c:246-247`、`I2S_MODE_MASTER = 1`: `r_i2s_api.h:87`） |
| Bit Depth | `16 Bits`（暫定） | `r_ssi` xml `:113-121`。DA7212 の DAI ワード長設定と合わせること（**1-9節 #3**）。16bit なら 18〜24bit と違い右詰め補正が入らない（`r_ssi.c:256-262`） |
| Word Length | `16 Bits` | `r_ssi` xml `:122-131`。Bit Depth 以上である必要がある。2-4節の分周比計算はこの値（1フレーム32BCLK）が前提 |
| WS Continue Mode | `Disabled` | `r_ssi` xml `:132-135`。送信アイドル中も WS を出し続けるか。DA7212 側の要求が不明なため既定値。Master Mode なので Enabled も選択可（Slave Mode との併用のみ禁止: `r_ssi` xml `:62-65`） |
| **Bit Clock Source(available only in Master mode)** | **`Internal AUDIO_CLK`** ✅確定（2-4節 C-2） | `r_ssi` xml `:136-138` → `Renesas##BSP##ra8p1##fsp####6.3.0.xml:8297-8300`。**`External AUDIO_CLK` は選択不可** — UM 表32 に AUDIO_CLK/P402 の行が無く、ボードは外部 AUDIO_CLK を供給していない（1-8-4節） |
| **Bit Clock Divider(available only in Master mode)** | **`Audio Clock / 1`** ✅確定 | `r_ssi` xml `:139-153`。C-2 では GPT1 が BCLK そのものを生成するため分周不要。目標 fs は GPT1 の Period 側で決める（5-4節） |
| Callback | `audio_i2s_callback` | `r_ssi` xml `:155-157`。デフォルトは `NULL` だが、**送信完了で次バッファを積むためコールバックが必要**。Issue #46 で `void audio_i2s_callback(i2s_callback_args_t * p_args)` を `src/` に実装する。生成コードは `#ifndef` 付きでプロトタイプ宣言を出す（`r_ssi` xml `:203-205`） |
| Transmit Interrupt Priority | `Priority 2` | `r_ssi` xml `:158-161`。デフォルトが `board.icu.common.irq.priority2`。**ch0 では TXI か RXI のどちらかが必須**（`r_ssi` xml `:27-29`）。また **DTC TX サブスタックを追加するには TXI が有効である必要がある**（同 xml `:46-48`） |
| Receive Interrupt Priority | `Disabled`（空欄） | `r_ssi` xml `:162-165`。**再生専用のため受信は使わない**。Disabled にすることで RX 用 DTC も不要になる（同 xml `:43-45`） |
| Idle/Error Interrupt Priority | `Priority 2` | `r_ssi` xml `:166-169`。この項目は mandatory（`enum.mcu.nvic.priorities.mandatory`）で Disabled にできない。**Transmit Interrupt Priority と同じ値にすること**（同 xml `:33-36` "All enabled interrupts must be the same priority."） |

> **優先度 2 を選ぶ理由**: `r_ssi` の既定値（`board.icu.common.irq.priority2`）であり、
> 既存の D/AVE 2D も priority2 を使っている（`configuration.xml:644`）。
> 既存の I2C / 外部IRQ は priority12（`configuration.xml:675`, `:685`）なので衝突しません。
> なお `r_ssi` の制約により **TXI と Idle/Error は必ず同値**にする必要があります。

### 3-3. r_ssi ドライバ共通設定（Module Configuration）

Stacks タブで `g_i2s_audio` を選択し、Properties の **Common**（`config.driver.ssi`）セクションを確認します。

| プロパティ名 | 設定値 | 根拠 |
|---|---|---|
| Parameter Checking | `Default (BSP)` | `r_ssi` xml `:4-8`。既定値。既存の他ドライバもすべて BSP 準拠（`configuration.xml:959-971`） |
| **DTC Support** | **`Enabled`（既定のまま）** | `r_ssi` xml `:9-12`。Enabled のままにする場合は **4章の DTC スタック追加が必須**（同 xml `:57-61`）。DTC を使わないなら Disabled にする |

---

## 4. 手順2: DTC サブスタックの追加（送信用）

### 4-1. サブスタックの追加

1. **Stacks** タブで `g_i2s_audio I2S (r_ssi)` のブロックを選択
2. ブロック内に表示される **`Add DTC Driver for Transmission [Recommended but optional]`** をクリック
3. **New** > **Transfer (r_dtc)** を選択

> 表示文字列の根拠: `r_ssi` xml `:67` の
> `display="Add DTC Driver for Transmission [Recommended but optional]"`。
> モジュール表示名は `Transfer|${module.driver.transfer.name} Transfer (r_dtc) ${activation_source}`
> （`Renesas##HAL Drivers##all##r_dtc####6.3.0.xml:24`）。

> **受信用（`Add DTC Driver for Reception`、`r_ssi` xml `:82`）は追加しません。**
> Receive Interrupt Priority を Disabled にしているため、追加すると制約違反になります（同 xml `:43-45`）。

> ### ⚠ 最頻出のミス: Transmission と Reception のスロットを取り違える
>
> I2S ブロックには **`Add DTC Driver for Transmission`** と **`Add DTC Driver for Reception`** の
> 2つのスロットが隣接して表示されます。**Reception 側に追加してしまうとビルドエラーになります**
> （9-5節に実例あり）。名前を `g_transfer_i2s_tx` にしても中身は変わりません。
>
> **追加直後に必ずブロックのラベルで確認してください。** `r_dtc` のモジュール表示名は
> `${name} Transfer (r_dtc) ${activation_source}` で、**活性化要因がラベル末尾に出ます**
> （`Renesas##HAL Drivers##all##r_dtc####6.3.0.xml:24`）。
>
> | ラベル末尾 | 判定 |
> |---|---|
> | `... Transfer (r_dtc) **SSI0 TXI**` | ✅ 正しい（Transmission 側） |
> | `... Transfer (r_dtc) **SSI0 RXI**` | ❌ 間違い（Reception 側）。削除して Transmission 側に追加し直す |

### 4-2. プロパティの設定

**ほとんどの項目は `r_ssi` 側の override で強制設定され、GUI 上では変更できません（グレー表示）。**
下表の「override」列が `r_ssi` xml の行番号です。

| プロパティ名 | 設定値 | override | 備考 |
|---|---|---|---|
| Name | `g_transfer_i2s_tx` | — | `r_dtc` xml `:52`。ユーザーが設定する。本Issue固有の命名 |
| Mode | `Block` | `r_ssi` xml `:69` | 変更不可 |
| Transfer Size | `4 Bytes` | `:68` | 変更不可 |
| Destination Address Mode | `Fixed` | `:70` | 変更不可。転送先は SSIFTDR レジスタ（`r_ssi.c:715`） |
| Source Address Mode | `Incremented` | `:71` | 変更不可 |
| Repeat Area (Unused in Normal Mode) | `Source` | `:72` | 変更不可 |
| Interrupt Frequency | `After all transfers have completed` | `:76` | 変更不可 |
| Number of Transfers | `0` | `:75` | 変更不可。実行時に `R_SSI_Open()` が `SSI_PRV_TRANSFER_BLOCK_SIZE`(=2) を設定する（`r_ssi.c:719`, `:49`） |
| Number of Blocks (Valid only in Block Mode) | `0` | `:77` | 変更不可。`R_SSI_Write()` 時に設定される（`r_ssi.c:833-837`） |
| Activation Source | `SSI0 TXI (Transmit data empty)` | `:80` | 変更不可 |
| Number of Transfer Descriptors | `1`（既定） | — | `r_dtc` xml `:92`。変更不要 |

### 4-3. DTC ドライバ共通設定（Module Configuration）

| プロパティ名 | 設定値 | 根拠 |
|---|---|---|
| Parameter Checking | `Default (BSP)` | `r_dtc` xml `:4-8` |
| Linker section to keep DTC vector table | `.fsp_dtc_vector_table`（既定のまま） | `r_dtc` xml `:9`。**変更しないこと**。この名前でリンカスクリプトに出力セクションが用意されている（`e2studio_CPU0/Debug/fsp_gen.lld:143-147`。1-4節） |

---

## 5. 手順3【必須】: MCLK 用 GPT2 インスタンスの追加

> **本章は必須です。** UM 表32 で **MCLK が PD06 に接続されている**ことが確定しました（1-8-1節）。
> ボード上に DA7212 用の発振器は無く（回路図 p18）、**MCU が MCLK を生成して供給する構成**です。
> PD06 で使える該当機能は **GPT2 の GTIOC2A のみ**（`PinCfgR7KA8P1KxxCAC.xml:33769`）なので、
> GPT2 の PWM 出力で MCLK を作ります。7-2節の PD06 ピン設定もあわせて実施してください。
>
> 出力周波数の確定値は 1-9節 #1（DA7212 データシート待ち）ですが、**暫定値のままビルドまで通せます**。

### 5-1. モジュールの追加

1. **Stacks** タブ > **HAL/Common** を選択 > **New Stack**
2. **Timers** > **Timer, General PWM (r_gpt)** を選択して追加

> メニュー階層の根拠: `Renesas##HAL Drivers##all##r_gpt####6.3.0.xml:38` の
> `display="Timers|... Timer, General PWM (r_gpt)"`。

### 5-2. プロパティの設定

「リファレンスと同値」は `reference_projects/quickstart_ek_ra8p1_ep/e2studio/configuration.xml:668-719`
（`g_timer_camera_xclk`）と同じ値であることを示します。

#### General

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Name | `g_timer_audio_mclk` | 本Issue固有 |
| Channel | `2` | 本Issue固有。**GTIOC2A を空きピン PD06 に出せる唯一のチャネル**（1-5節）。GPT2 は未使用（`configuration.xml:843-845` の GPT インスタンスは channel 12 のみ） |
| Mode | `Periodic` | リファレンスと同値 |
| Period | **`12500`**（暫定） | PCLKD 250 MHz を 20 分周した 12.5 MHz。**12.288 MHz は 250 MHz から整数分周できない**（1-3節）。DA7212 が許容する MCLK 周波数をデータシートで確認して確定させること（**1-9節 #1**）。この値のままでも Generate → ビルドは通る |
| Period Unit | `Kilohertz` | 12500 kHz = 12.5 MHz。リファレンス（`g_timer_camera_xclk`）も Kilohertz |

#### Output

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Custom Waveform Enable | `Disabled` | リファレンスと同値 |
| Duty Cycle Percent (only applicable in PWM mode) | `50` | リファレンスと同値。MCLK は 50% デューティの矩形波 |
| GTIOCA Output Enabled | `True` | GTIOC2A（PD06）から出力 |
| GTIOCA Stop Level | `Pin Level Low` | リファレンスと同値 |
| GTIOCB Output Enabled | `False` | リファレンスと同値 |
| GTIOCB Stop Level | `Pin Level Low` | リファレンスと同値 |

#### GTIOCA / GTIOCB Output Waveform

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Initial Output Level | `Low` | リファレンスと同値（A/B両方） |
| Cycle End Output Level | `Retain` | リファレンスと同値（A/B両方） |
| Compare Match Output Level | `Retain` | リファレンスと同値（A/B両方） |
| Retain Output Level at Count Stop | `Disabled` | リファレンスと同値（A/B両方） |

#### Input / Compare Match / Interrupts / Extra Features

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Count Up / Count Down / Start / Stop / Clear / Capture A / Capture B Source | すべて空欄 | リファレンスと同値 |
| GTIOCA / GTIOCB Input Filter | `None` | リファレンスと同値 |
| Compare Match A〜F | すべて `Disabled` | リファレンスと同値（`configuration.xml:848-859`） |
| Callback | `NULL` | リファレンスと同値。割り込み不使用 |
| Overflow/Crest, Capture A, Capture B, Trough Interrupt Priority | すべて `Disabled` | リファレンスと同値 |
| Extra Features | `Disabled` | リファレンスと同値 |
| Output Disable | 空欄 | リファレンスと同値 |

### 5-3. GPT ドライバ共通設定の確認（変更不要）

| プロパティ名 | 期待値 | 根拠 |
|---|---|---|
| Parameter Checking | `Default (BSP)` | `configuration.xml:973` |
| **Pin Output Support** | **`Enabled`** | `configuration.xml:974`。Disabled だと GTIOC2A から波形が出ません |
| Write Protect Enable | `Disabled` | `configuration.xml:975` |

### 5-4【必須】: AUDIO_CLK 生成用 GPT1 インスタンス

Bit Clock Source は C-2（Internal AUDIO_CLK）で確定したため（1-8-4節）、
**AUDIO_CLK 生成用に GPT1 のインスタンスも必須です。**

- 追加手順は 5-1 と同じ（**Timers** > **Timer, General PWM (r_gpt)**）
- Name: `g_timer_audio_clk`、Channel: **`1`**
- Mode: `Periodic`、Period / Period Unit: **2-4節 C-2 の表から目標 fs に対応する値**を選ぶ。
  例: fs = 16 kHz なら BCLK = 512 kHz なので `Period = 512295`, `Period Unit = Hertz`
  （e2 studio が period_counts = 488 を自動計算する。生成後に `ra_gen/hal_data.c` のコメントで実周波数を確認すること）。
  fs 誤差 ±0.15% を DA7212 が許容するかは **1-9節 #2**
- **GTIOCA Output Enabled**: **未確定（1-9節 #4）**。内部接続（GTIOC1A → SSIE AUDIO_CLK）が GPT の出力イネーブル
  （GTIOR の OAE ビット）を必要とするかは RA8P1 ハードウェアマニュアルの SSIE / GPT 章で確認してください。
  **FSP のソースからは判定できません**（`r_ssi.c` は GPT を一切設定しない。1-2節）。
  判断がつかない場合は **`True`（有効）にしておくのが無難**です。ピンを割り当てなければ外部には出ません。
- **GTIOC1A のピン割り当ては絶対に行わないこと**（P405 = SSITXD0 と衝突する。1-5節 / 7-3節）。
- Callback: `NULL`、全割り込み優先度: `Disabled`

> **なぜここが未確定なのか**: CLAUDE.md の検証ルールに従い、コードで裏取りできない動作は断定しません。
> 「Internal AUDIO_CLK にすれば GPT1 の出力が SSIE に届く」ことは
> FSP の enum id（`..._audio_clock_gtioc1a`）と description
> （`r_ssi` xml `:136` "internal connection to MCU specific GPT channel. Please refer to the hardware manual..."）
> から**強く示唆されますが、レジスタレベルの前提条件はハードウェアマニュアルの確認が必要です**。

---

## 6. 手順4: コーデック制御用 I2C デバイスの追加

> **【確定済み】** UM 表32 により **DA7212 の SDA/SCL は P511/P512 = SYS_I2C（IIC1）** で確定、
> スレーブアドレスは回路図 p18 より **0x1A（7bit）** で確定です（1-8-1節 / 1-8-2節）。
> したがって本章は「**既存の `g_comms_i2c_bus0` へのデバイス追加**」で確定であり、
> 新規 `r_iic_master` インスタンスやピン設定は不要です。

### 6-1. なぜ「新規 I2C インスタンス」ではなく「既存バスへのデバイス追加」なのか

1-6節のとおり、**IIC1 は既にタッチパネル（`g_i2c_master0`, slave 0x38）とカメラ（`g_i2c_master_camera`, slave 0x3C）で使われています**。
ここに3つ目の `r_iic_master` インスタンスを作ると、同一ハードウェアチャネルを3つのドライバインスタンスが
Open することになり、排他管理がさらに複雑になります。

既に `rm_comms_i2c` の I2C Shared Bus（`g_comms_i2c_bus0`）が存在するので、
**その下にデバイスを1つ追加する**のが最も素直です。

### 6-2. デバイスの追加

1. **Stacks** タブ > **HAL/Common** を選択 > **New Stack**
2. **Connectivity** > **I2C Communication Device (rm_comms_i2c)** を選択して追加
   （表示名の根拠: `Renesas##Middleware##all##rm_comms_i2c####6.3.0.xml:177`
   `display="Connectivity|${module.driver.comms_i2c_device.name} I2C Communication Device (rm_comms_i2c)"`）
3. 追加されたブロックの **`I2C Shared Bus`** サブスタック（同 xml `:190`）で
   **`Use` > `g_comms_i2c_bus0`** を選び、**既存のバスを共有する**
   （`find="true"` なので既存インスタンスが候補に出ます）
   - **新しい I2C Shared Bus を作らないこと。** 作ると `r_iic_master` インスタンスがもう1つ生えます。

### 6-3. プロパティの設定

| プロパティ名 | 設定値 | 根拠 / 備考 |
|---|---|---|
| Name | `g_comms_i2c_codec` | `rm_comms_i2c` xml `:194`。本Issue固有 |
| Semaphore Timeout (RTOS only) | `0xFFFFFFFF` | 同 xml `:197`。既存 `g_comms_i2c_device0` と同値（`configuration.xml:649`）。本プロジェクトは FSP 上 RTOS 無し設定のため実質未使用 |
| **Slave Address** | **`0x1A`** ✅確定 | 回路図 `doc/reference/ek-ra8p1-v1-schematic.pdf` p18 に `I2C Address: 0x1A (7-bits)` と明記（1-8-2節）。7bit アドレスなので 0x7F 以下の制約も満たす（同 xml `:184-186`）。既存の 0x38（タッチ）・0x3C（カメラ）と重複しない |
| Address Mode | `7-Bit` | 同 xml `:203-206`。既存デバイスと同値（`configuration.xml:651`） |
| Callback | **`audio_codec_i2c_callback`** | 同 xml `:208-212`。**デフォルトの `comms_i2c_callback` を使わないこと。** その名前は既にタッチパネル用として `e2studio_CPU0/src/port/lv_port_indev.c:451` で実装済みで、同名にすると両デバイスが同じ関数を共有してしまいます |

### 6-4. 既存バス設定（変更しない）

`g_comms_i2c_bus0` と `g_i2c_master0` のプロパティは**変更しません**。参考値:

| モジュール | プロパティ | 現在値 | 出典 |
|---|---|---|---|
| `g_comms_i2c_bus0` | Channel | `1` | `configuration.xml:659` |
| `g_comms_i2c_bus0` | Rate | `Fast-mode` | `configuration.xml:660` |
| `g_i2c_master0` | 実効 channel | `1`（bus の override 由来） | `ra_gen/common_data.c:442`、override は `rm_comms_i2c` xml `:46` |

> **Issue #46 への申し送り（重要）**: `g_comms_i2c_bus0` の
> ミューテックス／セマフォは **`BSP_CFG_RTOS == 0` によりコンパイル時に構造体から消えており**、
> ミドルウェア層のバス排他は一切効いていません（詳細と根拠の連鎖は 1-6節）。
> 現状の安全性の根拠は「タッチパネルが唯一の rm_comms_i2c ユーザーである」こと
> （`lv_port_indev.c:577-579`）だけです。
> **DA7212 を追加するとこの前提が崩れ、かつミューテックスを有効化する選択肢も無いため、
> 排他はアプリ側で実装する必要があります。**
> さらにカメラ（`g_i2c_master_camera`）とも `camera_thread_i2c_done()` による時間分離（`lv_port_indev.c:530-533`）で
> 共存しています。**Issue #46 では IIC1 の排他設計を明示的に見直してください**（10-2節）。
> なおコーデックの I2C アクセスは「起動時の初期化」と「音量変更」程度で頻度が低いため、
> 既存の直列化ポリシー（カメラ初期化完了後に触る）に乗せるのが現実的です。

---

## 7. 手順5: Pins タブ

### 7-1. I2S 信号線: **作業不要**

P402〜P406 は EK-RA8P1 のボードデフォルト pincfg（`configuration.xml:1352` の `<pincfg name="RA8P1 EK">`）で
既に SSIE に割り当てられており、`ra_gen/pin_data.c:163-179` に反映済みです（1-5節）。
Pins タブ側でも `ssie.mode.custom.free` / `ssie0.mode.custom.free` が有効です（`configuration.xml:1374-1375`）。

**Pins** タブ > **Peripherals** > **Connectivity:SSIE**
（グループ名の根拠: `PinCfgR7KA8P1KxxCAC.xml:41252` `<components id="ssie" name="Connectivity:SSIE">`）
を開いて、以下になっていることを**確認するだけ**にしてください。

| 項目 | 期待値 | 根拠 |
|---|---|---|
| SSIE Operation Mode | `Custom` | `configuration.xml:1374` `ssie.mode.custom.free` |
| SSIE AUDIO_CLK | `P402` | ボードパック `:926` `ssie.audio_clk.p402` |
| SSIE0 Operation Mode | `Custom` | `configuration.xml:1375` `ssie0.mode.custom.free` |
| SSIE0 SSIBCK0 | `P403` | ボードパック `:929` |
| SSIE0 SSILRCK0 | `P404` | ボードパック `:930` |
| SSIE0 SSITXD0 | `P405` | ボードパック `:932` |
| SSIE0 SSIRXD0 | `P406` | ボードパック `:931` |

> **P402（SSIE AUDIO_CLK）の割り当てを外すかどうか**
>
> Bit Clock Source に `Internal AUDIO_CLK` を選ぶと SSIE の AUDIO_CLK **入力**は使いません。
> P402 の本来のシンボル名は `PMOD1_RST`（`configuration.xml:1129`）なので、
> PMOD1 を使うようになった時点では衝突します。
> **本Issueでは変更しないこと**を推奨します（`ra_gen/pin_data.c` の差分を最小化するため。
> AUDIO_CLK は入力なので、割り当てたままでも他機能を阻害しません）。
> PMOD1 を使う予定が立った時点で別Issueとして扱ってください。

### 7-2【必須】: MCLK 用 PD06 のピン割り当て

> **5章をスキップした場合、本節も実施しません。**

1. **Pins** タブ > **Peripherals** > **Timers:GPT** > **GPT2** を選択
   （グループ名の根拠: `PinCfgR7KA8P1KxxCAC.xml:44906` `<components id="gpt" name="Timers:GPT">`）
2. 以下を設定

| 項目 | 設定値 | 根拠 |
|---|---|---|
| Operation Mode | `GTIOCA or GTIOCB` | 既存の GPT12 も同じモード（`configuration.xml:1356` `gpt12.mode.gtiocaorgtiocb.free`） |
| GTIOC2A | **`PD06`** | `pd06.gpt2.gtioc2a`（`PinCfgR7KA8P1KxxCAC.xml:33915`）。他候補 P103 / P113 / P713 はすべて使用中（1-5節） |
| GTIOC2B | `None` | 未使用 |

3. **Pins** タブ > **Ports** > **PD** > **PD06** で以下を確認

| プロパティ名 | 設定値 | 備考 |
|---|---|---|
| Mode | `Peripheral mode` | GTIOC2A 割り当て後に自動設定されるはず。されていなければ手動設定 |
| Symbolic Name | `AUDIO_MCLK` のまま | ボードデフォルト（`configuration.xml:1265`）。変更不要 |
| Drive Capacity | 既定 → 波形が鈍る場合のみ `Middle` | 12.5 MHz を引き回すため。DA7212(U14) はボード上の至近距離にあり（回路図 p18）配線は短いので、まず既定で試す |

### 7-3. GPT1（AUDIO_CLK 用）: **ピン割り当てをしない**

5-4節で GPT1 インスタンスを追加した場合でも、**Pins タブで GPT1 の GTIOC1A にピンを割り当ててはいけません。**
GTIOC1A の候補ピンは P105 / P209 / P405 / P509 で、**P405 は SSIE0 SSITXD0 そのもの**です（1-5節）。
割り当てると I2S のデータ線が壊れます。

### 7-4. 既存ピン設定への影響がないことの確認

- I2S 信号線（P402〜P406）は既存のまま。**新規に奪うピンは無し**。
- PD06 は現在 `ra_gen/pin_data.c` に含まれていない（PORT_13 は PIN_00,02,03,04,05,07 のみ。`:611-629`）ため、
  追加しても既存機能のピンを奪わない。
- I2C（P511 / P512）は既存の設定をそのまま使う。**変更なし**。
- クロック設定（`e2studio/solution.xml`）は変更しない。

---

## 8. 手順6: スタック構成の確認

設定完了後、**Stacks** タブが以下の構成になっていることを確認します（`<--` が本Issueでの追加分）。

```
HAL/Common
  +-- g_ioport I/O Port (r_ioport)
  +-- g_uart0 UART (r_sci_b_uart)
  +-- g_i2c_master_camera I2C Master (r_iic_master)          [Channel 1]
  +-- g_timer_camera_xclk Timer, General PWM (r_gpt)         [Channel 12]
  +-- g_i2s_audio         I2S (r_ssi)                        [Channel 0]     <-- 新規
  |     +-- g_transfer_i2s_tx Transfer (r_dtc) SSI0 TXI                      <-- 新規
  +-- g_timer_audio_clk   Timer, General PWM (r_gpt)         [Channel 1]     <-- 新規(AUDIO_CLK, 内部接続)
  +-- g_timer_audio_mclk  Timer, General PWM (r_gpt)         [Channel 2]     <-- 新規(MCLK → PD06)
  +-- LVGL (lvgl)
  |     +-- LVGL Port (rm_lvgl_port)
  |           +-- GLCDC (r_glcdc)
  |           +-- D/AVE 2D Port (drw)
  |                 +-- D/AVE 2D (tes dave2d)
  +-- g_comms_i2c_device0 I2C Communication Device (rm_comms_i2c)   [slave 0x38 タッチ]
  |     +-- g_comms_i2c_bus0 I2C Shared Bus (rm_comms_i2c)          [Channel 1]
  |           +-- g_i2c_master0 I2C Master (r_iic_master)
  +-- g_comms_i2c_codec   I2C Communication Device (rm_comms_i2c)   [slave 0x1A]  <-- 新規
  |     +-- g_comms_i2c_bus0 I2C Shared Bus (rm_comms_i2c)          [既存を共有]
  +-- g_external_irq0 External IRQ (r_icu)
  +-- g_vin0 Video Input (VIN) (r_vin)
  |     +-- g_mipi_csi0 MIPI CSI-2 (r_mipi_csi)
  |           +-- g_mipi_phy0 MIPI PHY Host (r_mipi_phy)
  +-- TensorFlow Lite for Microcontrollers
        +-- g_rm_ethosu0 Ethos-U (rm_ethosu)
        +-- FlatBuffers / CMSIS-NN / CMSIS-DSP
```

（現状の構成は `e2studio_CPU0/configuration.xml:922-957` を参照）

---

## 9. 最終手順: コード生成とビルド確認

### 9-1. コード生成

1. `e2studio_CPU0/configuration.xml` のエディタ上部 **Generate Project Content** をクリック
2. Configuration Problems ビューにエラーが出ないことを確認

よく出る制約エラーと対処:

| エラーメッセージ | 原因 | 対処 |
|---|---|---|
| `Add DTC TX or RX stack if DTC support is enabled` | DTC Support が Enabled なのに DTC サブスタックが無い | 4章を実施、または 3-3節で DTC Support を Disabled |
| `All enabled interrupts must be the same priority.` | TXI と Idle/Error の優先度が違う | 3-2節のとおり両方 Priority 2 |
| `Requires Transmit Interrupt (TXI) enabled or Receive Interrupt (RXI) enabled` | ch0 で両方 Disabled | TXI を Priority 2 に |
| `DTC Driver for Transmission requires Transmit Interrupt (TXI) to be enabled` | TXI Disabled のまま DTC TX を追加した | TXI を有効化 |

（メッセージ文言の出典: `r_ssi` xml `:27-29`, `:33-36`, `:46-48`, `:57-61`）

### 9-2. 生成・更新されるファイル

| ファイル | 内容 |
|---|---|
| `ra_gen/hal_data.c` | `g_i2s_audio_ctrl` / `g_i2s_audio_cfg` / `g_i2s_audio_cfg_extend` / `g_i2s_audio` と、`g_transfer_i2s_tx` の DTC 構造体が追加される（生成テンプレートは `r_ssi` xml `:211-271`） |
| `ra_gen/hal_data.h` | `extern const i2s_instance_t g_i2s_audio;` 等と、コールバック `audio_i2s_callback` のプロトタイプ（`r_ssi` xml `:196-206`） |
| `ra_gen/hal_data.c` / `.h` | `g_comms_i2c_codec` の rm_comms_i2c 構造体が追加される。**`common_data.c` ではなく `hal_data.c` の先頭付近**（既存の `g_comms_i2c_device0` と同じ場所。実測で `hal_data.c:5-25`）|
| `ra_gen/vector_data.c` / `.h` | **SSI0 TXI / SSI0 INT のベクタが追加される**。`BSP_ICU_VECTOR_NUM_ENTRIES` が 20（現在値: `ra_gen/vector_data.h:75`）から増える |
| `ra_gen/pin_data.c` | `BSP_IO_PORT_13_PIN_06`（PD06 = MCLK）が GPT ペリフェラル指定付きで追加される。**I2S 信号線（PORT_04 PIN_02〜06）のエントリは変化しない** |
| `ra/fsp/src/r_ssi/`, `ra/fsp/src/r_dtc/` | **新規にコピーされる**（現在プロジェクトに存在しない） |
| `ra_cfg/fsp_cfg/r_ssi_cfg.h`, `r_dtc_cfg.h` | 新規生成 |

> `ra_gen/` および `ra/fsp/` 配下は自動生成／ライブラリです。**手動編集しないでください。**

### 9-3. 生成結果の確認（必ず実施）

1. `ra_gen/hal_data.c` の `g_i2s_audio_cfg_extend` が
   `.audio_clock = (ssi_audio_clock_t) SSI_AUDIO_CLOCK_INTERNAL`（C-2 の場合）、
   `.bit_clock_div = SSI_CLOCK_DIV_1` になっていること
2. `g_i2s_audio_cfg` の `.channel = 0`、`.p_transfer_tx = &g_transfer_i2s_tx`、`.p_transfer_rx = NULL` になっていること
   （NULL 判定は `r_ssi` xml `:250-261` の `RA_NOT_DEFINED` マクロで行われる）
2b. **【重要】`g_transfer_i2s_tx_cfg_extend` の `.activation_source` が
   `VECTOR_NUMBER_SSI0_TXI` になっていること**。`VECTOR_NUMBER_SSI0_RXI` になっていたら
   DTC が Reception 側に付いています（4-1節の⚠を参照）。あわせて `g_transfer_i2s_tx_info` が
   **`.dest_addr_mode = TRANSFER_ADDR_MODE_FIXED`** / **`.src_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED`** /
   **`.repeat_area = TRANSFER_REPEAT_AREA_SOURCE`** であることを確認（メモリ → SSI レジスタの向き）。
   逆向き（dest=INCREMENTED / src=FIXED / repeat=DESTINATION）なら受信用です
3. 5-4節で GPT1 を追加した場合、`ra_gen/hal_data.c` の `g_timer_audio_clk_cfg` 直前の
   自動生成コメント `/* Actual period: ... */` で **実 BCLK が 2-4節の表と一致すること**、
   および `.source_div = (timer_source_div_t) 0`（分周比1）であることを確認
4. 5章で GPT2 を追加した場合、`g_timer_audio_mclk_cfg` の実周波数が **12.5 MHz** であること
   （period_counts = 20 になるはず）
5. `ra_gen/pin_data.c` の PORT_04 PIN_02〜06（SSI）と PORT_05 PIN_11/12（IIC）が**変化していないこと**

### 9-4. ビルド確認

1. **Project** > **Build Project** を実行
2. エラーなく完了することを確認

   > **コールバックのスタブは本Issueでは不要です（実測で確認済み）。**
   >
   > `ra_gen/hal_data.c` は `.p_callback = audio_i2s_callback` / `audio_codec_i2c_callback` を参照し、
   > `hal_data.h:24-25, 60-61` にプロトタイプ宣言も出ますが、**本Issueの段階ではリンクエラーになりません。**
   >
   > 本プロジェクトのビルドは `-flto` / `-ffunction-sections` / `--gc-sections` が有効で
   > （`e2studio_CPU0/Debug/makefile`）、この時点では **`g_i2s_audio` / `g_comms_i2c_codec` を
   > 参照するコードがどこにも存在しない**（`R_SSI_Open()` 等の呼び出しは Issue #46）ため、
   > 設定構造体ごと最適化で除去され、コールバックへの参照も消えるためです。
   >
   > 実測（`llvm-nm` で生成 ELF を確認）:
   >
   > | シンボル | ELF 内 |
   > |---|---|
   > | `ssi_txi_isr` / `ssi_int_isr` | **存在する**（ベクタテーブル 20/21 から参照されるため） |
   > | `g_i2s_audio` / `g_comms_i2c_codec` / `g_transfer_i2s_tx` / `g_timer_audio_clk` / `g_timer_audio_mclk` | **存在しない**（未参照のため除去） |
   > | `audio_i2s_callback` / `audio_codec_i2c_callback` | **存在しない**（同上） |
   >
   > **ただし Issue #46 で `R_SSI_Open()` / `RM_COMMS_I2C_Open()` を呼んだ瞬間に、
   > 両コールバックは未定義シンボルとしてリンクエラーになります。** Open を書く前に
   > 実体を `e2studio_CPU0/src/` に実装してください（`ra_gen/` ではなく `src/`）。

3. FLASH / RAM 使用量の増分を確認する（測定方法は `doc/build-setup-guide/` 配下の手順を参照）。
   `r_ssi.c` と `r_dtc.c` がプロジェクトに加わりますが、**上記のとおり現時点では未参照のため
   コードサイズはほとんど増えません**。実質的な増分は Issue #46 で Open を呼んでからになります。

> **この時点ではまだ音は鳴りません。** `R_SSI_Open()` / `R_SSI_Write()` の呼び出しと
> DA7212 の初期化は Issue #46 で実装します。

### 9-5. トラブルシューティング（実際に発生した事例）

#### `error: use of undeclared identifier 'VECTOR_NUMBER_SSI0_RXI'`

```
../ra_gen/hal_data.c:305:3: error: use of undeclared identifier 'VECTOR_NUMBER_SSI0_RXI'
  305 |                 VECTOR_NUMBER_SSI0_RXI, };
make: *** [ra_gen/subdir.mk:36: ra_gen/hal_data.o] Error 1
```

**原因**: DTC サブスタックを **`Add DTC Driver for Transmission` ではなく
`Add DTC Driver for Reception` の側に追加している**（4-1節の⚠）。
Receive Interrupt Priority を `Disabled` にしているため `VECTOR_NUMBER_SSI0_RXI` が
`ra_gen/vector_data.h` に生成されず、受信用 DTC がそれを参照して未定義識別子になる。

インスタンス名を `g_transfer_i2s_tx` にしていても中身は Reception 用です。
生成物で以下のように RX 用の override が当たっているのが決定的な証拠です（`r_ssi` xml `:82-96`）。

| `hal_data.c` の値 | RX（誤） | TX（正） |
|---|---|---|
| `activation_source` | `VECTOR_NUMBER_SSI0_RXI` | `VECTOR_NUMBER_SSI0_TXI` |
| `dest_addr_mode` | `INCREMENTED` | `FIXED` |
| `src_addr_mode` | `FIXED` | `INCREMENTED` |
| `repeat_area` | `DESTINATION` | `SOURCE` |

**Stacks タブでの見え方**（実際に発生した状態）:

I2S ブロックの下に DTC が**2つ**並びます。**左が Transmission スロット、右が Reception スロット**です
（`r_ssi` xml の `requires` 宣言順。transfer_tx が `:67`、transfer_rx が `:82`）。

```
        ┌───────────────────────────────┐
        │  g_i2s_audio  I2S (r_ssi)     │
        └───────────────┬───────────────┘
                        ▲
        ┌───────────────┴───────────────┐
┌───────────────────────┐   ┌───────────────────────────┐
│ g_transfer0           │   │ g_transfer_i2s_tx         │
│ Transfer (r_dtc)      │   │ Transfer (r_dtc)          │
│ SSI0 TXI              │   │ Disabled                  │
│ (Transmit data empty) │   │                           │
│   ✅ 残す（送信用）    │   │   ❌ 削除（受信用）        │
└───────────────────────┘   └───────────────────────────┘
   ← Transmission スロット      Reception スロット →
```

**Reception 側のラベルが `Disabled` になる**のが目印です（RXI を Disabled にしているため
活性化要因が無効表示になる）。名前は自分で付けた文字列なので判断材料になりません
——上図では紛らわしいことに `g_transfer_i2s_tx` という名前の方が受信用です。

**対処**:

1. **Stacks** タブで **Reception 側（ラベルが `Disabled` の方）のブロック**を選択し、
   右クリック > **Delete**（またはキーボードの Delete）で削除する
2. 残った Transmission 側のブロックを選択し、Properties の **Name** を `g_transfer_i2s_tx` にする
   （既定は `g_transfer0`。手順書の命名に合わせるだけなので必須ではないが、変えない場合は
   Issue #46 のコードから `g_transfer0` を参照することになる）
3. ブロックのラベルが `g_transfer_i2s_tx Transfer (r_dtc) SSI0 TXI (Transmit data empty)` に
   なっていることを確認（4-1節の⚠の表）
4. **Generate Project Content** を再実行
5. 9-3節の 2b で `activation_source = VECTOR_NUMBER_SSI0_TXI` を確認
6. **Project** > **Build Project**

> **Transmission スロットに1つも追加していなかった場合**は、手順1で削除したあと
> `g_i2s_audio I2S (r_ssi)` ブロック内の
> **`Add DTC Driver for Transmission [Recommended but optional]`** をクリックし、
> **New** > **Transfer (r_dtc)** で追加してください。

> **予防**: e2 studio の Stacks タブには「DTC Driver for Reception requires Receive Interrupt (RXI)
> to be enabled」という制約が定義されており（`r_ssi` xml `:43-45`）、
> 誤って Reception 側に追加すると **Configuration Problems ビューに警告が出ます**。
> Generate 前にこのビューを確認する習慣をつけると、ビルドまで待たずに気付けます。

---

## 10. 次Issueへの引き継ぎ事項

### 10-1. Issue #46 で使う API

宣言は `Renesas.RA.6.3.0.pack` 内 `ra/fsp/inc/instances/r_ssi.h:102-113` にあります
（Generate 後は `e2studio_CPU0/ra/fsp/inc/instances/r_ssi.h` に配置されます）。

| API | 用途 | 宣言 |
|---|---|---|
| `R_SSI_Open(&g_i2s_audio_ctrl, &g_i2s_audio_cfg)` | 初期化。内部で DTC も Open される（`r_ssi.c:713-720`） | `r_ssi.h:102` |
| `R_SSI_Write(&g_i2s_audio_ctrl, p_src, bytes)` | 再生データ投入。全データがキューされると `I2S_EVENT_TX_EMPTY`、送信完了で `I2S_EVENT_IDLE` がコールバックされる（`r_i2s_api.h:186-187`） | `r_ssi.h:105` |
| `R_SSI_Stop(&g_i2s_audio_ctrl)` | 停止。停止完了は `I2S_EVENT_IDLE` コールバックで通知（`r_i2s_api.h:172`） | `r_ssi.h:103` |
| `R_SSI_Mute(&g_i2s_audio_ctrl, mute_enable)` | ミュート | `r_ssi.h:109` |
| `R_SSI_StatusGet(&g_i2s_audio_ctrl, &status)` | 状態取得 | `r_ssi.h:104` |
| `R_SSI_Close(&g_i2s_audio_ctrl)` | 終了 | `r_ssi.h:110` |
| `R_SSI_CallbackSet(...)` | コールバックの実行時差し替え | `r_ssi.h:111` |

GPT を使う場合（5章 / 5-4節）は `R_GPT_Open()` / `R_GPT_Start()`
（`e2studio_CPU0/ra/fsp/inc/instances/r_gpt.h:420-422`）を、**`R_SSI_Open()` より前に**呼びます。

> **戻り値の扱い**: 上記API群はすべて `fsp_err_t` を返します。CLAUDE.md の検証ルールに従い、
> Issue #46 の実装では**戻り値を必ず判定してください**（無視すると失敗時の挙動が変わりません）。

### 10-2. Issue #46 / #47 の再スコープ（方式C 前提に書き直し）

rev1 では方式A採用に伴い「#46 / #47 の多くが不成立」としていましたが、**方式C では大半が成立します**。

| Issue | 現在の記述 | **方式C 採用時の実態** |
|---|---|---|
| #46 | 「サンプリングレート設定（8kHz〜44.1kHz）」 | **成立する**。ただし設定箇所は3つに分かれる: (a) SSIE の Bit Clock Divider（Stacks タブ）、(b) AUDIO_CLK 源の周波数（外部クロック or GPT1 の period）、(c) DA7212 側のレジスタ設定。実現可能な fs と誤差は 2-4節の表を参照 |
| #46 | 「ビット深度設定（8bit/16bit）」 | **成立する**。`Bit Depth` / `Word Length` プロパティ（3-2節）。ただし**再生中の動的変更は不可**（`R_SSI_Close()` → 設定変更 → `R_SSI_Open()` が必要） |
| #46 | 「DMA転送設定（音声バッファ→出力デバイス）」 | **成立する。ただし DMAC ではなく DTC**（2-5節）。DTC の設定は `R_SSI_Open()` が行うため（`r_ssi.c:713-720`）、アプリ側で DTC を直接操作する必要は無い |
| #46 | 「DMA転送完了割り込みハンドラ」 | **成立する**。実体は **SSI のコールバック** `audio_i2s_callback`。`I2S_EVENT_TX_EMPTY`（次バッファを積むタイミング）と `I2S_EVENT_IDLE`（再生終了）を分岐する（`r_i2s_api.h:76-78`） |
| #46 | 「ダブルバッファリング」 | **成立する**。`I2S_EVENT_TX_EMPTY` で裏バッファを `R_SSI_Write()` する2面バッファで実装できる |
| #46 | 「音量制御関数」 | **成立する（むしろ方式C の利点）**。DA7212 のボリュームレジスタで**ハードウェア音量制御**が可能。ソフト側でサンプル値をスケールする方法も併用可 |
| #46 | （新規追加）**コーデック初期化** | **新規作業**。DA7212 のレジスタ設定を自前実装する（10-3節） |
| #46 | （新規追加）**IIC1 のバス排他設計の見直し** | **新規作業（必須）**。DA7212 を追加すると `lv_port_indev.c:577-579` の「タッチパネルが唯一の rm_comms_i2c ユーザー」という前提が崩れる（1-6節、6-4節） |
| #46 | （新規追加）**MCLK / AUDIO_CLK 用 GPT の Open/Start** | **新規作業**。GPT2（MCLK, PD06）と GPT1（AUDIO_CLK, 内部接続）を SSIE より先に起動する必要がある（`r_ssi` は GPT を触らない。1-2節） |
| #47 | 「正弦波のトーン生成」「ルックアップテーブル」 | **成立する**。16bit PCM の正弦波 LUT を生成して `R_SSI_Write()` で流す |
| #47 | 「周波数 1kHz〜4kHz」 | **成立する**。fs = 16 kHz だと 4 kHz はナイキスト限界に近いので、**4 kHz を綺麗に出すなら fs = 32 kHz 以上が望ましい**（2-4節の表から選ぶ） |
| #47 | 「音量エンベロープ（フェードイン/アウト）」 | **成立する**。サンプル値へのゲイン乗算で滑らかに実現できる |
| #47 | 「ダブルバッファのコールバックで次のバッファを動的生成」 | **成立する**（#46 のダブルバッファ実装の上に乗る） |
| #47 | 「`alarm_sound_start()` / `alarm_sound_stop()` / `alarm_sound_set_pattern()`」 | **そのまま実装可能** |
| #47 | 「開始/停止が即座に反映される（停止遅延100ms以内）」 | **達成可能だが要注意**。`R_SSI_Stop()` の完了は `I2S_EVENT_IDLE` コールバックで通知される非同期動作（`r_i2s_api.h:172`）。即座に無音にしたい場合は `R_SSI_Mute()` または DA7212 側のミュートを併用する。**1回の `R_SSI_Write()` に渡すバッファ長を短く（例: 10〜20 ms）しておくこと** |

### 10-3. DA7212 の初期化シーケンスは自前実装

1-7節のとおり **FSP に DA7212 用ドライバはありません**。Issue #46 で以下を自前実装します。

- DA7212 のレジスタマップ定義（ヘッダ）
- I2C 経由の read / write ヘルパ（`RM_COMMS_I2C_Write()` / `RM_COMMS_I2C_WriteRead()` を使う。
  既存のタッチパネル実装 `e2studio_CPU0/src/port/lv_port_indev.c` が呼び出しパターンの参考になる）
- 電源投入シーケンス、PLL / クロック設定、I2S フォーマット設定（word length / fs）、
  DAC → スピーカーアンプの出力パス設定、ミュート解除、音量設定
- **DA7212 データシートの推奨スタートアップシーケンスに厳密に従うこと**

**本Issue（#45）のスコープはあくまで FSP モジュールとピンの設定までです。**

### 10-4. ハードウェア側の引き継ぎ事項

**ボード側の確認は完了しており、ハードウェアの改造・ジャンパ作業は一切不要です**（1-8節）。

| 項目 | 状態 |
|---|---|
| J41 ジャンパ（DATIN/DATOUT ↔ P405/P406） | **初期設定で短絡済み。作業不要**（UM 表2） |
| スピーカー接続 | **J33-1 / J33-2**（SP_P / SP_N）。ユーザーは接続済み |
| MCLK 供給元 | **MCU の GPT2 → PD06**。5章を必ず実施 |
| I2C スレーブアドレス | **0x1A（7bit）**。6-3節に反映済み |
| MIPI カメラとの併用 | **可能**（SW4-6 = OFF の初期設定のまま。1-8-5節） |

残るのは **DA7212 データシート / RA8P1 ハードウェアマニュアル**が必要な4項目のみです（1-9節）。
Issue #46（コーデック初期化）でデータシートを読む際に、以下を併せて確定させてください。

- **MCLK 周波数**（暫定 12.5 MHz）と DA7212 の PLL 設定 → 5-2節の Period に反映
- **fs の許容誤差**（Internal AUDIO_CLK では ±0.15% 程度） → 5-4節の Period に反映
- **DAI ワード長**（暫定 16bit） → 3-2節の Bit Depth / Word Length に反映
- **DA7212 の出力パス設定**（DAC → スピーカアンプ SP_P/SP_N の経路、音量、ミュート解除）

### 10-5. 本Issueの調査中に見つかった別件（対応は本Issueのスコープ外）

> 以下は rev1（方式A）の調査中に発見した申し送りで、方式C に変更しても**引き続き有効**です。

GPT のカウントクロックを確定させる過程で、**既存のカメラXCLK設定に設定値と実出力の不一致**が
あることが分かりました。**本Issueでは変更しません**が、別Issue化の検討を推奨します。

| 項目 | 内容 | file:line |
|---|---|---|
| 設定意図 | カメラXCLK 24 MHz | `e2studio_CPU0/configuration.xml:843-845`（`g_timer_camera_xclk`, channel=12） |
| 生成結果 | `period_counts = 0xa (=10)`, `source_div = 0`、コメント `Actual period: 4e-8 seconds` | `e2studio_CPU0/ra_gen/hal_data.c:132-135` |
| 実出力 | 250 MHz / 10 = **25 MHz**（24 MHz ではない） | 上記 + 1-3節「クロック源の確定」 |
| 先行手順書の記述 | 「GPTCLK を 240 MHz にすれば 24 MHz が正確に出る」 | `doc/fsp-setup-guide/issue-12-gpt-camera-xclk.md:86-91` |

**先行手順書のこの前提は、現在の設定では成立しません。** `bsp_clock_cfg.h:56` が
`BSP_CFG_GPT_COUNT_CLOCK_SOURCE (1) /* GPT Src: PCLKD */` であるため、GPT は GPTCLK ではなく
PCLKD を数えているからです（1-3節の連鎖表を参照）。GPTCLK を 240 MHz にしても、GPT Src を
GPTCLK に切り替えない限り XCLK は変わりません。

なお **GPT Src を GPTCLK（240 MHz）に切り替えると、本Issueで追加する GPT1 / GPT2 の設定にも影響します**
（period_counts が変わり、2-4節・5-2節の値の再計算が必要になります）。
カメラ側を修正する場合は、オーディオ側の period も併せて再確認してください。

---

## 11. 受け入れ確認チェックリスト

> 本章の各項目は、3章〜7章の設定表と1対1で対応させています（CLAUDE.md「ドキュメント間の整合」ルール）。
> 設定値の根拠が必要な場合は各章の表を参照してください。

### 11-0. 事前確認

#### 確定済み（回路図・UM で確認完了。作業不要）

- [x] J41 は初期設定で 1-2 / 3-4 とも短絡済み＝コーデック接続状態（UM 表2。1-8-3節）
- [x] スピーカーは J33-1 / J33-2（SP_P / SP_N）に接続（UM 表32）
- [x] MCLK は MCU の GPT2 → PD06 から供給する構成（UM 表32。**5章を実施**）
- [x] I2C スレーブアドレス = **0x1A（7bit）**（回路図 p18。**6-3節に反映済み**）
- [x] Bit Clock Source は **C-2（Internal AUDIO_CLK / GPT1）に確定**。P402 は使わない（1-8-4節）
- [x] MIPI カメラと併用可能（SW4-6 = OFF のまま。1-8-5節）

#### 未確定（DA7212 データシート / RA8P1 HW マニュアル待ち。暫定値でビルドは通る）

- [ ] 1-9節 #1: DA7212 が許容する MCLK 周波数 → 5-2節の Period（暫定 12.5 MHz）を確定
- [ ] 1-9節 #2: fs 誤差 ±0.15% の許容可否 → 5-4節の GPT1 Period を確定
- [ ] 1-9節 #3: DA7212 の DAI ワード長 → 3-2節の Bit Depth / Word Length（暫定 16bit）を確定
- [ ] 1-9節 #4: Internal AUDIO_CLK に GPT の出力イネーブル（GTIOR.OAE）が必要か → 5-4節
- [ ] 2-4節: 目標サンプリングレートを決定し、GPT1 の Period を確定した

### 11-1. Issue #45 の受け入れ条件との対応

- [ ] **音声出力方式が選定・決定されている** → **方式C: SSIE0 (I2S, Master) + DA7212 Audio CODEC + J33 スピーカー**
- [ ] **FSP configuration.xml に必要なモジュールが追加されている** → `g_i2s_audio`（r_ssi ch0）/ `g_transfer_i2s_tx`（r_dtc）/ `g_comms_i2c_codec`（rm_comms_i2c device, 0x1A）/ `g_timer_audio_clk`（GPT1）/ `g_timer_audio_mclk`（GPT2）
- [ ] **ピンアサインが設定されている** → I2S 信号線はボードデフォルトを流用（追加作業なし）。MCLK を出す場合のみ GPT2 GTIOC2A = PD06
- [ ] **コード生成が正常に完了し、ビルドエラーがない** → 9章
- [ ] **方式選定の根拠がissueコメントに記録されている** → 本書の 1章・2章を Issue #45 にコメント転記する

### 11-2. Stacks タブ設定チェック（3〜6章に対応）

I2S (r_ssi) — 3-2節:

- [ ] `g_i2s_audio` が HAL/Common に追加されている
- [ ] Channel = `0`
- [ ] Operating Mode (Master/Slave) = `Master Mode`
- [ ] Bit Depth = `16 Bits`
- [ ] Word Length = `16 Bits`
- [ ] WS Continue Mode = `Disabled`
- [ ] Bit Clock Source = `Internal AUDIO_CLK`（C-2 で確定。`External AUDIO_CLK` は選ばない）
- [ ] Bit Clock Divider = `Audio Clock / 1`（C-2）/ 2-4節の表の値（C-1）
- [ ] Callback = `audio_i2s_callback`
- [ ] Transmit Interrupt Priority = `Priority 2`
- [ ] Receive Interrupt Priority = `Disabled`
- [ ] Idle/Error Interrupt Priority = `Priority 2`（TXI と同値）

r_ssi 共通設定 — 3-3節:

- [ ] Parameter Checking = `Default (BSP)`
- [ ] DTC Support = `Enabled`

DTC — 4章:

- [ ] `g_transfer_i2s_tx` が `g_i2s_audio` の `Add DTC Driver for Transmission` サブスタックとして追加されている
- [ ] 受信用 DTC（`Add DTC Driver for Reception`）は**追加していない**
- [ ] **Stacks タブの DTC ブロックのラベル末尾が `SSI0 TXI`**（`SSI0 RXI` なら Reception 側。4-1節⚠ / 9-5節）
- [ ] Configuration Problems ビューに警告が出ていない
- [ ] DTC 共通設定の「Linker section to keep DTC vector table」が `.fsp_dtc_vector_table`（既定）のまま

MCLK 用 GPT2 — 5章【必須】:

- [ ] `g_timer_audio_mclk` / Channel = `2` / Mode = `Periodic`
- [ ] Period = `12500`, Period Unit = `Kilohertz`（暫定 12.5 MHz。1-9節 #1 確定後に見直す）
- [ ] Duty Cycle Percent = `50` / GTIOCA Output Enabled = `True` / GTIOCA Stop Level = `Pin Level Low`
- [ ] GTIOCB Output Enabled = `False` / Callback = `NULL` / 全割り込み優先度 = `Disabled`

AUDIO_CLK 用 GPT1 — 5-4節【必須】:

- [ ] `g_timer_audio_clk` / Channel = `1` / Mode = `Periodic`
- [ ] Period / Period Unit が 2-4節 C-2 の表の値
- [ ] Callback = `NULL` / 全割り込み優先度 = `Disabled`
- [ ] **Pins タブで GTIOC1A にピンを割り当てていない**（7-3節）

コーデック制御 I2C — 6章:

- [ ] `g_comms_i2c_codec` が追加されている
- [ ] I2C Shared Bus に **既存の `g_comms_i2c_bus0` を選択**している（新規バスを作っていない）
- [ ] Slave Address = `0x1A`（回路図 p18 で確定）
- [ ] Address Mode = `7-Bit`
- [ ] Callback = `audio_codec_i2c_callback`（既存の `comms_i2c_callback` と**別名**）
- [ ] `g_comms_i2c_bus0` / `g_i2c_master0` / `g_i2c_master_camera` のプロパティを変更していない

### 11-3. Pins タブ設定チェック（7章に対応）

- [ ] SSIE Operation Mode = `Custom`、SSIE AUDIO_CLK = `P402`（既存のまま・変更していない）
- [ ] SSIE0 Operation Mode = `Custom`、SSIBCK0 = `P403` / SSILRCK0 = `P404` / SSITXD0 = `P405` / SSIRXD0 = `P406`（既存のまま）
- [ ] （5章実施時）GPT2 Operation Mode = `GTIOCA or GTIOCB`、GTIOC2A = `PD06`、PD06 の Mode = `Peripheral mode`
- [ ] GPT1 の GTIOC1A にピンを割り当てていない
- [ ] IIC1（P511 / P512）の設定を変更していない
- [ ] 既存の GPT12 (`g_timer_camera_xclk`) の設定が変わっていない
- [ ] クロック設定（`e2studio/solution.xml`）を変更していない

### 11-4. 生成結果チェック（9-3節に対応）

- [ ] `ra_gen/hal_data.c` に `g_i2s_audio_cfg` が生成され、`.channel = 0` になっている
- [ ] `g_i2s_audio_cfg_extend` の `.audio_clock` / `.bit_clock_div` が 11-2節で設定した値と一致する
- [ ] `g_i2s_audio_cfg` の `.p_transfer_tx = &g_transfer_i2s_tx`、`.p_transfer_rx = NULL`
- [ ] **`g_transfer_i2s_tx_cfg_extend.activation_source` = `VECTOR_NUMBER_SSI0_TXI`**（`..._RXI` なら 9-5節）
- [ ] **`g_transfer_i2s_tx_info` が `dest=FIXED` / `src=INCREMENTED` / `repeat_area=SOURCE`**（逆向きなら受信用。9-5節）
- [ ] **`ra_gen/hal_data.c`**（`common_data.c` ではない）に `g_comms_i2c_codec` 一式が生成され、`.slave = 0x1A` になっている
- [ ] `ra_gen/vector_data.h` の `BSP_ICU_VECTOR_NUM_ENTRIES` が増えている（変更前: 20）
- [ ] （GPT 追加時）`ra_gen/hal_data.c` の `Actual period` コメントが 2-4節 / 5-2節の想定値と一致する
- [ ] `ra_gen/pin_data.c` の PORT_04 PIN_02〜06（SSI）と PORT_05 PIN_11/12（IIC）が変化していない
- [ ] （5章実施時）`ra_gen/pin_data.c` に `BSP_IO_PORT_13_PIN_06` が追加されている
- [ ] `ra/fsp/src/r_ssi/` と `ra/fsp/src/r_dtc/` がプロジェクトに追加されている
- [ ] Build Project がエラーなく完了する（**コールバックのスタブは不要**。理由は 9-4節）

---

## 付録A: 方式A（GPT PWM + 圧電ブザー）— rev1 の選定内容

**方式C を採用したため本Issueでは実施しません。** DA7212 が使えない状況になった場合の代替として残します。

### A-1. 概要

GPT10 の GTIOC10A（P810）から矩形波を出し、他励式圧電ブザーを鳴らす方式。追加モジュールは `r_gpt` 1個のみ。
1〜4 kHz は PCLKD = 250 MHz から誤差ゼロで生成できます（period_counts = 250,000〜62,500）。

### A-2. ピンの根拠

| ピン | シンボル名 | 使える機能 | 空きの根拠 |
|---|---|---|---|
| **P810** | `ARDUINO_D4_MIKROBUS_PWM`（`configuration.xml:1199`） | **GPT10 GTIOC10A**（`PinCfgR7KA8P1KxxCAC.xml:22588` の capability list） | `ra_gen/pin_data.c:343-391` の PORT_08 は PIN_00〜08 と PIN_12〜15 のみで **PIN_10 は未設定**。`configuration.xml:1330` のコメントも "Enable when connected" |

GTIOC10A の他候補（P109 / P408 / PA13）はすべて使用中です（`ra_gen/pin_data.c:61`, `:184`, `:503`）。

### A-3. 移行手順（概要）

1. **Stacks** タブ > HAL/Common > New Stack > **Timers** > **Timer, General PWM (r_gpt)**
2. プロパティ

   | プロパティ名 | 設定値 | 備考 |
   |---|---|---|
   | Name | `g_timer_buzzer` | |
   | Channel | `10` | |
   | Mode | `Periodic` | |
   | Period / Period Unit | `2000` / `Hertz` | 初期音程 2 kHz |
   | Duty Cycle Percent | `50` | |
   | GTIOCA Output Enabled | `True` | |
   | GTIOCA Stop Level | `Pin Level Low` | 停止時にブザーを無音にする |
   | GTIOCB Output Enabled | `False` | |
   | Callback / 全割り込み優先度 | `NULL` / `Disabled` | 断続パターンは μT-Kernel の周期ハンドラで制御する |

3. **Pins** タブ > Peripherals > **Timers:GPT** > **GPT10** > Operation Mode = `GTIOCA or GTIOCB`、GTIOC10A = `P810`
4. GPT ドライバ共通設定の **Pin Output Support = Enabled** を確認（`configuration.xml:974`。既に Enabled）

### A-4. 方式A 特有の有用な知見（方式C でも参考になる）

`R_GPT_PeriodSet()` は Periodic モードのとき **自動で約50%デューティに再設定します**
（`e2studio_CPU0/ra/fsp/src/r_gpt/r_gpt.c:466-477`、
`uint32_t duty_cycle_50_percent = (period_counts >> 1) - 1U;` を GTCCRC/GTCCRE に書き込む）。
そのため周波数変更のたびに `R_GPT_DutyCycleSet()` を呼び直す必要はありません。

> **この挙動もコンパイル時分岐に依存します。** 該当ブロックは `#if GPT_CFG_OUTPUT_SUPPORT_ENABLE`
> の内側にあり（`r_gpt.c:465` / `:478`）、`GPT_CFG_OUTPUT_SUPPORT_ENABLE (1)`
> （`e2studio_CPU0/ra_cfg/fsp_cfg/r_gpt_cfg.h:9`）のため**有効**です。
> この値は Stacks タブの GPT ドライバ共通設定 **Pin Output Support**（`configuration.xml:974` = Enabled）に追従します。
> また分岐条件 `TIMER_MODE_PERIODIC == p_instance_ctrl->p_cfg->mode` は**実行時判定**です。

この知見は **5章の MCLK 用 GPT2 / 5-4節の GPT1 でも有効**です（周波数を実行時に変えたい場合）。
実カウントクロックは `R_GPT_InfoGet()` の `info.clock_frequency` から取得できます（`r_gpt.h:431`）。
250 MHz をハードコードしないでください。

### A-5. 方式A のハードウェア要件

- **他励式（外部駆動タイプ）の圧電ブザー**を P810 と GND の間に接続する
- **自励発振ブザー（発振回路内蔵タイプ）は不可**（周波数変更ができない）
- P810 は Arduino D4 と mikroBUS PWM の両方に出ているため、拡張ボード装着時に衝突する可能性がある

---

## 付録B: 方式B（内蔵DAC_B + 外部アンプ）

**方式C を採用したため本Issueでは実施しません。**
DA7212 が使えず、かつ矩形波では音質が不足する場合の代替として残します。

### B-1. ピンの根拠

DAC の出力ピンは **P014 / P015 に固定**です。FSP のピンマッピング定義
`PinCfgR7KA8P1KxxCAC.xml:34845-34877` に `dac120.da0.p014` と `dac121.da1.p015` の1候補ずつしか存在しません。

| ピン | シンボル名 | 空きの根拠 |
|---|---|---|
| **P014** | `ARDUINO_AN4`（`configuration.xml:1276`） | `ra_gen/pin_data.c:7-24` の PORT_00 は PIN_00,06,08,09,12,13 のみ → **PIN_14 は未設定** |
| **P015** | `ARDUINO_AN5`（`configuration.xml:1277`） | 同上（PIN_15 未設定） |

### B-2. 移行手順（概要）

1. **Stacks** タブ > HAL/Common > New Stack > **Analog** > **DAC (r_dac_b)** を追加
   （メニュー階層の根拠: `Renesas##HAL Drivers##all##r_dac_b####6.3.0.xml:21`
   `display="Analog|${module.driver.dac.name} DAC (r_dac_b)"`）
2. プロパティ

   | プロパティ名 | 設定値 | 備考 |
   |---|---|---|
   | Name | `g_dac_alarm` | |
   | Channel | `0` | DA0 = P014。ch1（P015）も選択可 |
   | DAC Operating Voltage Mode | `Normal voltage mode (VREFH >= 2.7v)` | `r_dac_b` xml `:60` |
   | Data Format | `Right Justified` | `r_dac_b` xml `:64` |
   | Internal Output | `Disabled` | 外部ピンへ出す |
   | ELC Trigger Source | サンプリング用 GPT のオーバーフローイベント | 連続波形出力に必須 |

3. **Pins** タブ > Peripherals > **Analog:DAC12** > **DAC120** > Operation Mode = `Enabled`、DA0 = `P014`
   （`PinCfgR7KA8P1KxxCAC.xml:34826-34853`。ピンツール上のグループ名は DAC12 だがドライバは `r_dac_b`）
4. サンプリング周期生成用に **GPT インスタンスをもう1つ**追加し、ELC 経由で DAC をトリガする
5. **Transfer** > **Transfer (r_dmac)** を**単独スタック**として追加し、波形バッファ → DAC のデータレジスタへ転送する
   （`r_dac_b` は転送サブスタックを持たないため自前で構成する。根拠: `r_dac_b` xml `:41-46`）
6. **外部アンプ回路が必須**（`bsp_feature.h:219` `BSP_FEATURE_DAC_HAS_OUTPUT_AMPLIFIER (0UL)`）
7. P014 / P015 の VREFH / AVCC 周りの制約を回路図で確認する

---

## 12. 参照情報

| 項目 | 参照先 |
|---|---|
| Issue | https://github.com/grace2riku/mimamori-sense/issues/45 |
| **EK-RA8P1 回路図** | `doc/reference/ek-ra8p1-v1-schematic.pdf` **p18**「Audio Devices / SSIE/I2S Audio Codec」（U14 = DA7212, J33 = スピーカ, J41 = 開放リンク, `I2C Address: 0x1A (7-bits)`） |
| **EK-RA8P1 ユーザーズマニュアル** | `doc/reference/r20ut5309jg0104-ek-ra8p1-v1-um.pdf`（R20UT5309JG0104 Rev.1.04, 2025.10.23） |
| └ DA7212 の説明とピン割り当て | 同 6.6節（印刷 p40-42）、**表32**（印刷 p41） |
| └ ジャンパ初期設定（J41） | 同 **表2**（印刷 p15） |
| └ カメラ拡張ボードのモード別ピン割り当て | 同 8.3節＋**表35**（パラレル, 印刷 p51）/ **表36**（MIPI, 印刷 p52） |
| 後続Issue | #46（デバイス初期化）, #47（警報音生成）, #48（動作確認テスト） |
| 現プロジェクトのモジュール構成 | `e2studio_CPU0/configuration.xml:429-957` |
| 現プロジェクトのスタック構成（`_hal.0`） | `e2studio_CPU0/configuration.xml:922-957` |
| 現プロジェクトの GPT12 設定 | `e2studio_CPU0/configuration.xml:843-908` |
| GPT ドライバ共通設定 | `e2studio_CPU0/configuration.xml:972-976` |
| 現プロジェクトのピンシンボル定義 | `e2studio_CPU0/configuration.xml:1070-1266`（SSIE 関連は `:1129-1133`, `:1265`） |
| 現プロジェクトのピンコメント | `e2studio_CPU0/configuration.xml:1303-1305`（P402/P405/P406）, `:1351`（PD06） |
| 現プロジェクトのボード pincfg | `e2studio_CPU0/configuration.xml:1352-1377`（SSIE は `:1374-1375`, IIC1 は `:1358`） |
| 実際に有効なピン設定（自動生成） | `e2studio_CPU0/ra_gen/pin_data.c`（SSIE0 = `:163-179`, IIC1 = `:241-249`） |
| I2C バスの実効設定（自動生成） | `e2studio_CPU0/ra_gen/common_data.c:434-481`（`g_i2c_master0`, 実 channel=1）, `ra_gen/hal_data.c:162-163`（`g_i2c_master_camera`） |
| I2C バス排他の既存実装 | `e2studio_CPU0/src/port/lv_port_indev.c:530-608`（特に `:555-593` のコメント）, `src/camera_thread_entry.c:711-731`, `src/camera_thread_api.h:72` |
| 既存 I2C コールバック実装 | `e2studio_CPU0/src/port/lv_port_indev.c:451` |
| DTC ベクタテーブルのリンカ配置 | `e2studio_CPU0/Debug/fsp_gen.lld:143-147`, `e2studio_CPU0/ra_gen/vector_data.h:75` |
| SSIE の存在根拠 | `e2studio_CPU0/ra/fsp/src/bsp/cmsis/Device/RENESAS/Include/R7KA8P1KF_core0.h:14623-14822, 64793-64794`, `bsp_elc.h:157-162` |
| DAC_B の存在根拠 | `e2studio_CPU0/ra/fsp/src/bsp/mcu/ra8p1/bsp_peripheral.h:55-62`, `bsp_feature.h:211-230` |
| DMAC の存在根拠 | `R7KA8P1KF_core0.h:64714-64721` |
| GPT のカウントクロック（PCLKD / GTCLK のコンパイル時分岐） | `e2studio_CPU0/ra/fsp/src/r_gpt/r_gpt.c:1737-1747`, `:1130-1141` |
| GPT が PCLKD を数える根拠（分岐の確定） | `e2studio_CPU0/ra_gen/bsp_clock_cfg.h:56` + `e2studio_CPU0/ra_cfg/fsp_cfg/r_gpt_cfg.h:12-16` + `bsp_peripheral.h:99` |
| PCLKD 周波数 | `e2studio_CPU0/ra_gen/bsp_clock_cfg.h:13, 38` |
| GPTCLK 周波数（GPT Src 変更時の値） | `e2studio_CPU0/ra_gen/bsp_clock_cfg.h:27, 54, 55` |
| `R_GPT_PeriodSet()` の50%デューティ自動設定 | `e2studio_CPU0/ra/fsp/src/r_gpt/r_gpt.c:466-477`（`#if GPT_CFG_OUTPUT_SUPPORT_ENABLE` 内。`ra_cfg/fsp_cfg/r_gpt_cfg.h:9` = 1） |
| GPT API 宣言 | `e2studio_CPU0/ra/fsp/inc/instances/r_gpt.h:420-448` |
| SSI API 宣言 / 型定義 | `Renesas.RA.6.3.0.pack` 内 `ra/fsp/inc/instances/r_ssi.h:30-53, 102-113`（Generate 後は `e2studio_CPU0/ra/fsp/inc/instances/r_ssi.h`） |
| SSI ドライバ実装（クロック設定・DTC 連携） | `Renesas.RA.6.3.0.pack` 内 `ra/fsp/src/r_ssi/r_ssi.c:28-49, 246-266, 713-720, 833-837` |
| I2S API イベント定義 | `Renesas.RA.6.3.0.pack` 内 `ra/fsp/inc/api/r_i2s_api.h:76-78, 86-87, 172, 186-187` |
| DTC ドライバ実装（ベクタテーブル配置） | `Renesas.RA.6.3.0.pack` 内 `ra/fsp/src/r_dtc/r_dtc.c:21, 26-35, 90, 530-545` |
| SSIE0 ピン割り当ての参考 | `reference_projects/lv_port_renesas_ek_ra8p1/configuration.xml:1529-1538, 1930-1936` |
| GPT インスタンス設定の参考 | `reference_projects/quickstart_ek_ra8p1_ep/e2studio/configuration.xml:668-719` |
| FSP モジュール定義（FSP 6.3.0） | `%USERPROFILE%\.eclipse\com.renesas.platform_1213781633\internal\projectgen\ra\modules\` 配下の `Renesas##HAL Drivers##all##r_ssi####6.3.0.xml` / `r_dtc` / `r_gpt` / `r_dac_b` / `r_dmac`、`Renesas##Middleware##all##rm_comms_i2c####6.3.0.xml` |
| MCU 定義（SSIE / DTC の provides、audio_clock enum） | 同上ディレクトリ `Renesas##BSP##ra8p1##device####6.3.0.xml:53, 268-270, 823-824, 1109-1111`, `Renesas##BSP##ra8p1##fsp####6.3.0.xml:8297-8300` |
| ピンマッピング定義（RA8P1 CAC パッケージ） | `%USERPROFILE%\.eclipse\com.renesas.platform_1213781633\internal\projectgen\ra\6.3\pinmapping\PinCfgR7KA8P1KxxCAC.xml`（SSIE グループ `:41252`, GPT グループ `:44906`, PD06 capability `:33769`） |
| ボードデフォルトピン設定 | FSP パック `Renesas.RA_board_ra8p1_ek.6.3.0.pack` 内 `.module_descriptions/Renesas##BSP##Board##ra8p1_ek####6.3.0##configuration.xml:264-267, 513-524, 926-932` |
| 関連手順書 | `doc/fsp-setup-guide/issue-12-gpt-camera-xclk.md`（GPT 設定の先行例）, `doc/fsp-setup-guide/issue-11-i2c-master-camera.md`（I2C 設定の先行例）, `doc/fsp-setup-guide/issue-3-touch-panel-i2c-irq.md`（rm_comms_i2c の先行例） |
