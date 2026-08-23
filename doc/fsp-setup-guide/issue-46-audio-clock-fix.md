# e2 studio操作手順書: Issue #46 実測で判明したオーディオクロック設定の修正

## 改訂履歴

| 版 | 日付 | 内容 |
|---|---|---|
| rev1 | 2026-08-23 | 初版。実機実測に基づき #45 の設定誤りを修正 |

## 対象Issue

- 発見元: #46 (S-005-2: オーディオ出力デバイス初期化処理の実装)
- 修正対象: #45 (S-005-1) で設定した r_ssi / GPT の構成

## 対象プロジェクト

`e2studio_CPU0`

---

## 0. サマリ

Issue #45 は **SSIE0 の内部 AUDIO_CLK を GPT1 が供給する**という前提で設定を行いました。
**この前提は実機実測により否定されました。** 実際には **GPT2** が供給しています。

| 項目 | #45 の設定 | 実測で判明した正しい値 |
|---|---|---|
| SSIE 内部 AUDIO_CLK の供給元 | GPT1 (`g_timer_audio_clk`) | **GPT2 (`g_timer_audio_mclk`)** |
| r_ssi の Bit Clock Divider | `Audio Clock / 1` | **`Audio Clock / 24`** |
| GPT1 (`g_timer_audio_clk`) | AUDIO_CLK 生成に必須 | **未使用。削除可** |

本書はこの2点を e2 studio 上で修正する手順です。

> **現在の暫定処置**: `src/port/audio_port.c` の `audio_init()` が `R_SSI_Open()` 直後に
> SSICR.CKDV を直接書き換えて `/24` を適用しています（`AUDIO_SSI_BIT_CLOCK_DIV`）。
> 本手順を実施したら、この暫定処置は削除できます（削除は必須ではなく、値が一致していれば無害）。

---

## 1. 判明の経緯（受け入れ根拠）

### 1-1. FSP のドキュメントはチャネルを明示していない

`e2studio_CPU0/ra/fsp/inc/instances/r_ssi.h:35`:

```c
SSI_AUDIO_CLOCK_INTERNAL = 1,  ///< Audio clock source is internal connection to a MCU specific GPT channel output
```

「**MCU specific** GPT channel」としか書かれておらず、RA8P1 でどのチャネルかは記載がありません。
`ra/fsp/src/bsp/mcu/ra8p1/bsp_feature.h` にも SSI 関連は3行（`:552-554`）しかなく、
クロック源の記載はありません。#45 はここを GPT1 と推定しました。

### 1-2. 実測による否定

`audio status` の `Measured` 行（`src/port/audio_port.c` の `audio_cmd_status()`）で、
1バッファ = 10 ms として実バッファレートを測定しました。期待値は 100 buf/s です。

| SSICR.CKDV | 実測 | 期待比 |
|---|---|---|
| `/1`（#45 の設定） | 1669 buf/s | 16.7 倍速 |
| `/16` | 151 buf/s | 1.5 倍速 |
| **`/24`** | **99〜100 buf/s** | **一致** |

`/16` 時の実測 fs ≈ 24,160 Hz から逆算すると:

```
BCLK      = 24,160 × 32 = 773,120 Hz
AUDIO_CLK = 773,120 × 16 = 12,369,920 Hz  ≈ 12.37 MHz
```

**GPT2 の出力 250 MHz / 20 = 12.5 MHz とほぼ一致**します（誤差 1%、アンダーフロー再起動による
`tx_empty` の水増し分で説明可能）。

GPT1 が供給元であれば、その出力は 250 MHz / 488 = 512,295 Hz なので、
`/1` でちょうど 100 buf/s になっていたはずです。そうならなかったことが GPT1 説の反証です。

### 1-3. GPT1 は正常動作しているが誰も使っていない

実機のレジスタ読み出し（`mr` コマンド）:

| レジスタ | アドレス | 読み値 | 意味 |
|---|---|---|---|
| GPT1 GTCNT | `0x40322148` | `0x190` → `0x11C` | カウント中 |
| GPT1 GTPR | `0x40322164` | `0x1E7` (487) | 周期 488。設定どおり |
| GPT1 GTCR | `0x4032212C` | `0x00000001` | CST=1（動作中） |

**GPT1 は設定どおり正しく動作しています。その出力を消費する経路が無いだけです。**

---

## 2. 現在のクロック経路（修正後の姿）

```
PCLKD 250 MHz
   │
   ├─ GPT2 (÷20) = 12.5 MHz ──┬─→ PD06 ─→ DA7212 MCLK ─→ 内部PLL ─→ sysclk 12.288 MHz
   │                          │                                (SR=16kHz → 内部 16,000 Hz)
   │                          └─→ SSIE0 内部 AUDIO_CLK
   │                                 └─ CKDV ÷24 → BCLK 520,833 Hz
   │                                        └─ ÷32 (16bit × 2ch) → WCLK/fs 16,276 Hz
   │                                               └─→ P403/P404 ─→ DA7212 DAI
   │
   └─ GPT1 (÷488) = 512,295 Hz ──→ 未接続（削除対象）
```

### 2-1【重要】GPT2 の兼任による制約

**GPT2 は MCLK 供給と SSIE の AUDIO_CLK を兼任しています。**
GPT2 の周期を変更すると **MCLK と fs の両方が同時に変化**します。

MCLK が変われば DA7212 の PLL 帰還分周値も再計算が必要です
（`src/port/da7212.c` の `DA7212_PLL_FBDIV_*` はマクロで自動計算されるため、
`DA7212_MCLK_HZ` を更新すれば追従します。ただし `PLL_INDIV` のレンジ選択は手動確認が必要）。

**fs を調整したい場合は、必ずコーデックの PLL 設定とセットで検討してください。**

### 2-2. 現在の fs 誤差（既知の逸脱）

| 項目 | 値 |
|---|---|
| SSIE が生成する fs | 16,276 Hz |
| DA7212 の SR レジスタ設定 | 16 kHz（`0x22` = `0x05`） |
| 誤差 | **+1.7%** |

`ssi_clock_div_t`（`r_ssi.h`）に `/25` が無いため、12.5 MHz から 16 kHz ちょうどは作れません。
実機では問題なく再生できています（DA7212 の `PC_RESYNC_AUTO`（`0x94` bit1、既定 1）が
DAI とシステムクロックのドリフトを吸収します）。

より正確な fs が必要な場合は GPT2 の周期と CKDV の組み合わせを再設計します。
例: GPT2 周期 61（MCLK 4.098 MHz）＋ CKDV `/8` → fs 16,010 Hz（誤差 +0.06%）。
ただし MCLK が変わるため PLL_INDIV のレンジ（`0x27` bits3:2）が `00`（2-10 MHz）になり、
その分周比はデータシート表36の例（13/15/19.2 MHz、すべて 10-20 MHz 帯）では検証できません。
採用する場合はデータシートでの確認が必要です。

---

## 3. 手順1: Bit Clock Divider の変更【必須】

1. e2 studio で `e2studio_CPU0/configuration.xml` を開く
2. **Stacks** タブを開く
3. **HAL/Common** の **`g_i2s_audio I2S Driver on r_ssi`** をクリック
4. **Properties** ビューで以下を変更:

| プロパティ | 変更前 | **変更後** |
|---|---|---|
| Bit Clock Divider | `Audio Clock / 1` | **`Audio Clock / 24`** |

5. 他のプロパティは変更しない（Channel=0 / Master Mode / Bit Depth=16 Bits /
   Word Length=16 Bits / Bit Clock Source=Internal AUDIO_CLK / Callback=`audio_i2s_callback`）

---

## 4. 手順2: GPT1 インスタンスの削除

1. **Stacks** タブで **`g_timer_audio_clk Timer, General PWM (r_gpt)`** を選択
2. 右クリック → **Delete**（または Delete キー）
3. 削除後、**`g_timer_audio_mclk`（GPT2）は残す**ことを確認する
   （こちらが MCLK と AUDIO_CLK の両方を供給しているため、**削除してはいけない**）

### 4-1. 削除に伴うコード変更（本手順の実施後に必要）

`src/port/audio_port.c` の `audio_init()` に GPT1 の Open/Start があります。
FSP から `g_timer_audio_clk` を削除すると `hal_data.h` から宣言が消えるため、
**この2箇所を削除しないとビルドエラーになります**:

```c
/* Step 2: GPT1 -> GTIOC1A = SSIE internal AUDIO_CLK (= BCLK). */
err = R_GPT_Open(&g_timer_audio_clk_ctrl, &g_timer_audio_clk_cfg);   /* ← 削除 */
...
err = R_GPT_Start(&g_timer_audio_clk_ctrl);                          /* ← 削除 */
```

`src/port/audio_port.h` の `AUDIO_GPT_AUDIO_CLK_COUNTS`（488）も未使用になります。

> **順序の注意**: 先にコードから GPT1 参照を削除してから Generate すると、
> 一時的にもビルドエラーになりません。FSP 削除を先に行う場合はコード修正まで
> 一気に行ってください。

---

## 5. 最終手順: コード生成とビルド確認

### 5-1. コード生成

1. **Generate Project Content** をクリック
2. 生成完了を待つ

### 5-2. 生成結果の確認（必ず実施）

`e2studio_CPU0/ra_gen/hal_data.c` を開き、以下を確認:

- [ ] `g_i2s_audio_cfg_extend` の `.bit_clock_div` が **`SSI_CLOCK_DIV_24`** になっている
- [ ] `g_timer_audio_clk` 関連の定義が**すべて消えている**
- [ ] `g_timer_audio_mclk`（GPT2, `.channel = 2`, `period_counts = 0x14`）は**残っている**

`e2studio_CPU0/ra_gen/hal_data.h` も同様に確認:

- [ ] `extern const timer_instance_t g_timer_audio_clk;` が消えている
- [ ] `extern const timer_instance_t g_timer_audio_mclk;` は残っている

### 5-3. 暫定処置の削除（任意）

FSP 側が `/24` になったので、`src/port/audio_port.c` の SSICR 上書きは不要になります。
削除する場合は `R_SSI_Open()` 直後の以下のブロックを消してください:

```c
{
    uint32_t ssicr = R_SSI0->SSICR;
    ssicr &= ~(0xFUL << 4);
    ssicr |= (AUDIO_SSI_CKDV_VALUE << 4);
    R_SSI0->SSICR = ssicr;
}
```

残しても値が一致するため無害です。**削除する場合は 5-4 の実機確認を必ず再実施**してください
（FSP 設定が意図どおり効いていることの確認になります）。

### 5-4. 実機での確認

```
audio start
audio status
```

- [ ] `Measured` が **99〜100 buf/s**（`expect 100`）
- [ ] `SSI regs` の `SSICR` の bits7:4（CKDV）が **`A`**（例: `0x600940A2`）
- [ ] スピーカーから 1 kHz の音が出る
- [ ] `restart` が `tx_empty` の 1% 程度以下

---

## 6. #45 手順書への申し送り

`doc/fsp-setup-guide/issue-45-audio-output-modules.md` には以下の誤りがあります。
本書が正となります（CLAUDE.md「ドキュメント間の整合」ルールに従い、#45 側には本書への参照のみ追記）。

| #45 の記述 | 実態 |
|---|---|
| 1-2節・2-4節: 「Internal AUDIO_CLK は GPT1 の GTIOC1A」 | **GPT2**。GPT1 は未使用 |
| 2-4節: GPT1 period 488 で fs 16,009 Hz | GPT1 は無関係。fs は GPT2 と CKDV で決まる |
| 5-4節: 「GPT1 インスタンスは必須」 | 不要 |
| 10-4節: 「J41 は初期設定で短絡済み。作業不要」 | **実機は片組が開放だった**。目視確認が必要 |

### 6-1. J41 について（最重要の申し送り）

#45 は UM 表2 の記載から「J41 は出荷時に 1-2 / 3-4 とも短絡済み」としていましたが、
**実機では 3-4（P405 = SSITXD0 → DA7212 DATIN）が開放**でした。
この状態では I2S 再生データがコーデックに1ビットも届きません。

**DA7212 で音が出ない場合、最初に J41 の 3-4 を目視確認してください。**

### 6-2. 切り分けの落とし穴（DA7212 内蔵ビープ）

DA7212 の内蔵ビープ（`0xB4` START_STOPN）は切り分けに有用ですが、
**コーデック内部で生成され DAI→DAC 経路に注入される**ため（データシート §13.9, p36）、
**DATIN の配線を経由しません**。

→ **ビープが鳴っても I2S データ経路の正常性は証明されません。**
本Issueではこの穴に気づくのが遅れ、原因特定が長引きました。
