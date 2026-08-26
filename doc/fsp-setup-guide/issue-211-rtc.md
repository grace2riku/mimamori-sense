# e2 studio操作手順書: S-012-1: FSPプロジェクト設定 - RTC(r_rtc)モジュールの追加

## 対象Issue

[#211](https://github.com/grace2riku/mimamori-sense/issues/211) S-012-1: FSPプロジェクト設定 - RTC(r_rtc)モジュールの追加

S-012（時刻管理）の基盤。本手順書は FSP 設定変更のみを扱い、アプリコード（`time_ctrl` / `time` コマンド）は
[#212](https://github.com/grace2riku/mimamori-sense/issues/212) で実装する。

## 対象プロジェクト

`e2studio_CPU0`（Cortex-M85）のみ。**CPU1 と `e2studio/solution.xml` は変更しない**（理由は「手順 2」）。

## リファレンスプロジェクト

**該当なし。** `reference_projects/` の 2 プロジェクトはいずれも RTC を使っていない。

| プロジェクト | 確認結果 |
|---|---|
| `reference_projects/quickstart_ek_ra8p1_ep` | `configuration.xml` に `module.driver.rtc` / `r_rtc` の記述なし。`ra/fsp/src/r_rtc` も存在しない |
| `reference_projects/lv_port_renesas_ek_ra8p1` | 同上 |

そのため本手順書のプロパティ値は、**インストール済み FSP 6.3.0 のモジュール定義 XML を直接展開して**確定した。

参照元（読み取り専用。プロジェクト外）:

```
C:\Renesas\RA\e2studio_v2025-12_fsp_v6.3.0\internal\projectgen\ra\packs\
  Renesas.RA.6.3.0.pack
    .module_descriptions/Renesas##HAL Drivers##all##r_rtc####6.3.0.xml
  Renesas.RA_mcu_ra8p1.6.3.0.pack
    .module_descriptions/Renesas##BSP##ra8p1##device####6.3.0.xml   (ファミリレベル)
    .module_descriptions/Renesas##BSP##ra8p1##fsp####6.3.0.xml      (enum / BSP プロパティ)
```

---

## 現在のプロジェクト状態

### RTC 関連の設定状況

| 項目 | 現状 | 根拠 |
|---|---|---|
| `r_rtc` ドライバのソース | **未取り込み** | `e2studio_CPU0/ra/fsp/src/` に存在するのは 16 ディレクトリのみ（`bsp`, `r_drw`, `r_dtc`, `r_glcdc`, `r_gpt`, `r_icu`, `r_iic_master`, `r_ioport`, `r_mipi_csi`, `r_mipi_phy`, `r_sci_b_uart`, `r_ssi`, `r_vin`, `rm_comms_i2c`, `rm_ethosu`, `rm_lvgl_port`）。FSP は使用モジュールのみ生成する |
| RTC モジュールインスタンス | **なし** | `e2studio_CPU0/configuration.xml` に `module.driver.rtc` の記述なし |
| `hal_data.h` の RTC インスタンス | **なし** | `e2studio_CPU0/ra_gen/hal_data.h` に RTC 関連の `extern` なし |
| RTC 用 BSP プロパティ | **既に存在する** | `e2studio_CPU0/configuration.xml:108`（`config.bsp.fsp.mcu.rtc.err_adjustment_value.max_value` = `63`）。MCU 定義由来のため、モジュール未追加でも入っている |

### MCU 側の RTC サポート（BSP feature）

`e2studio_CPU0/ra/fsp/src/bsp/mcu/ra8p1/bsp_feature.h`

| マクロ | 値 | 行 | 意味 |
|---|---|---|---|
| `BSP_FEATURE_RTC_IS_AVAILABLE` | `1UL` | `:491` | **RA8P1 に RTC がある** |
| `BSP_FEATURE_RTC_HAS_TCEN` | `1UL` | `:495` | タイマキャプチャあり |
| `BSP_FEATURE_RTC_HAS_VBTICTLR` | `1UL` | `:496` | RTC への VBATT 入力制御をサポート |
| `BSP_FEATURE_RTC_IS_IRTC` | `0UL` | `:497` | IRTC ではない（サブクロック用の独立電源ドメイン VRTC を持たない） |

FSP モジュール定義側も一致する:

- ファミリ BSP `bsp.ra8p1` が `interface.mcu.rtc` を提供（`Renesas##BSP##ra8p1##device####6.3.0.xml:249`）
- `enum.mcu.rtc.present` の既定は `is_present` = "RTC is available on the RA8P1."（同 `:706-707`）
- `enum.mcu.rtc_c.present` / `enum.mcu.irtc.present` はいずれも `not_present`（同 `:709-713`）
  → **使うべきドライバは `r_rtc` であり、`r_rtc_c` ではない**

> **調査時の落とし穴（記録）**
> デバイス個別 XML（`Renesas##BSP##ra8p1##device##R7KA8P1KFLCAC##6.3.0.xml`）は
> `interface.mcu.rtc.external` **しか** provide していない（`:328`。当該ファイル内の rtc 該当行はこの 1 行のみ）。
> これだけを見ると「RTC が使えない」と誤読する。`interface.mcu.rtc` を提供しているのは
> **variant 名が空のファミリレベル BSP XML** であり、両者は合成される。
> 対象デバイスは `R7KA8P1KFLCAC`（`e2studio_CPU0/configuration.xml:7,18`）。

### サブクロックの状況

| 項目 | 現状 | 根拠 |
|---|---|---|
| サブクロック水晶の実装宣言 | 有効 | `e2studio/solution.xml:292`（`config.bsp.common.subclock_populated.enabled`） |
| ドライブ能力 | Standard | 同 `:293`（`config.bsp.common.subclock_drive.standard`） |
| 安定化待ち時間の設定値 | 1000 ms | 同 `:294`（`config.bsp.common.subclock_stabilization_ms`）。**ただし本プロジェクトでは無効化される。「#212 への申し送り」参照** |
| サブクロック周波数 | 32768 Hz | 同 `raClockConfiguration` の `board.clock.subclk.freq.32768` |
| P214 | `XCOUT` 割当済み | `e2studio_CPU0/configuration.xml:1238`、コメント `:1418`（`Subclock`） |
| P215 | `XCIN` 割当済み | 同 `:1239`、コメント `:1419`（`Subclock`） |

→ **Pins タブ・Clocks タブでの作業は不要。**

### VBATT（バッテリバックアップ）の状況

- **実機の EK-RA8P1 は VBATT に 3.0V コイン電池を接続済み**（2026-08-26 に実機確認）。
  したがって RTC は電源断後も時刻を保持できる
- **バックアップ自体に FSP の設定項目は無い。** VBATT が給電されていればハードウェアの動作
- `VBTICTLR` は VBATT ドメインの**ピン入力許可**（P402 / P403 / P404）であってバックアップの有効化ではない
  （`e2studio_CPU0/ra/fsp/src/r_ioport/r_ioport.c:95-100` の `g_vbatt_pins_input[]`）

#### オーディオ（SSIE）ピンとの干渉は無い

本プロジェクトでは P402〜P406 が SSIE に割り当てられており（`doc/fsp-setup-guide/issue-45-audio-output-modules.md:57`）、
うち **P402 / P403 は VBATT ドメインのピンと重なる**。ただし干渉しない:

- `bsp_vbatt_init()`（`r_ioport.c`）は、対象ピンの PSEL が `IOPORT_PERIPHERAL_AGT` または
  `IOPORT_PERIPHERAL_CLKOUT_COMP_RTC` の場合にのみ `VBTICTLR` のビットを立てる。SSIE ではこの条件を満たさない
- BSP は起動時に `R_SYSTEM->VBTICTLR = 0U` でクリアする（`bsp_clocks.c:3421`）

→ **本 Issue で追加の対処は不要。**

### HAL/Common の現状

`e2studio_CPU0/configuration.xml:1030-1074` の `<context id="_hal.0">` に以下がぶら下がっている。

| スタック | 用途 |
|---|---|
| `ioport_on_ioport` | I/O ポート |
| `uart_on_sci_b_uart` | J-Link コンソール（NT-Shell） |
| `i2c_on_iic_master` ×2 | カメラ用 / 共有バス |
| `timer_on_gpt` ×2 | `g_timer_camera_xclk`（GPT12）/ `g_timer_audio_mclk`（GPT2） |
| `lvgl` → `rm_lvgl_port` → `display_on_glcdc` / `drw` | LVGL 表示系 |
| `comms_i2c_on_comms_i2c_device` ×2 | DA7212 コーデック / タッチ |
| `external_irq_on_icu` | タッチ IRQ |
| `vin` → `mipi_csi` → `mipi_phy` | カメラ |
| `tflm` → `rm_ethosu` / `flatbuffers` / `cmsis-nn` | AI 推論 |
| `i2s_on_ssi` → `transfer_on_dtc` | オーディオ出力 |

**RTC はこの `HAL/Common` に単独のスタックとして追加する**（他モジュールにぶら下げない）。

---

## 前提条件の確認

### モジュール成立条件

`r_rtc` の `<constraint>`（`Renesas##HAL Drivers##all##r_rtc####6.3.0.xml`）:

| 制約 | 本プロジェクトでの充足 |
|---|---|
| `"${interface.mcu.rtc}" === "1"`（Requires RTC Peripheral） | ✅ ファミリ BSP が提供（上記） |
| Unique name required for each instance | ✅ 既存の RTC インスタンスなし |
| A callback function is required when interrupts are enabled | ✅ 条件式は `alarm_ipl` と `periodic_ipl` のみを見る。この2つを Disabled にすれば `p_callback = NULL` で成立する（Carry は有効にするが判定に含まれない。手順 1-3） |
| Error Adjustment Value ≦ 63（`max_value` が 63 の場合） | ✅ 値 0 を設定する。RA8P1 の `max_value` は 63（`configuration.xml:108`、`Renesas##BSP##ra8p1##fsp####6.3.0.xml:288`） |

### リソース競合の確認

| リソース | 競合 | 根拠 |
|---|---|---|
| RTC ペリフェラル | なし | プロジェクト内に RTC 使用箇所なし |
| P214 / P215（サブクロック） | なし | 既に `XCOUT` / `XCIN` として割当済み。**追加のピン設定をしない** |
| 割り込みベクタ | なし | Carry 割り込み（`rtc_carry_isr`）を 1 本使う。既存ベクタとの競合は無く、`VECTOR_DATA_IRQ_COUNT` が 22 → 23 になるだけ。Alarm / Period は使わない（手順 1-3） |

### 前提Issue

なし（S-012 の起点）。

---

## 手順 1: RTCモジュールの追加（configuration.xml）

### 1-1: モジュールの追加

1. e2 studio で `mimamori_sense_CPU0` の `configuration.xml` を開く
2. **Stacks** タブを選択
3. 左ペインの **HAL/Common** を選択（Thread ではなく HAL/Common であることを確認）
4. **New Stack** → **Timers** → **Realtime Clock (r_rtc)** を選択

> 表示名はモジュール定義の `display` 属性が `Timers|${module.driver.rtc.name} Realtime Clock (r_rtc)` なので、
> 実際には `g_rtc0 Realtime Clock (r_rtc)` のように表示される。

### 1-2: プロパティの設定

追加した RTC スタックを選択し、Properties ビューで以下を設定する。

#### General

| 項目（GUI 表示） | 設定値 | プロパティ id / 既定値 | 根拠・理由 |
|---|---|---|---|
| **Name** | **`g_rtc`** | `module.driver.rtc.name` / 既定 `g_rtc0` | 既存の命名（`g_timer_camera_xclk`, `g_i2s_audio` 等）に合わせ、インスタンスが 1 つなので番号を付けない |
| **Clock Source** | **`Sub-Clock`** ⚠️**必ず変更** | `module.driver.rtc.clock_source` / **既定は LOCO** | LOCO は内蔵 RC 発振器で精度が低く時計用途に適さない。32.768 kHz 水晶（実装済み）を使う。選択肢は Sub-Clock / LOCO の 2 つのみ（`Renesas##BSP##ra8p1##fsp####6.3.0.xml:881-884`） |
| **Frequency Comparison Value (LOCO)** | `255`（既定のまま） | `module.driver.rtc.freq_cmpr_value_loco` | **LOCO 選択時のみ使われる。** Sub-Clock では未使用のため既定のまま触らない |

#### Error Adjustment（誤差補正）

**初期は無補正にする。** 実誤差を測ってから値を決めるため。

| 項目（GUI 表示） | 設定値 | プロパティ id | 理由 |
|---|---|---|---|
| **Automatic Adjustment Mode** | **`Disabled`**（= `RTC_ERROR_ADJUSTMENT_MODE_MANUAL`） | `module.driver.rtc.err_adjustment_mode`（既定 Enabled） | 補正値が未定の段階で自動補正を回す意味がない |
| **Automatic Adjustment Period** | **`NONE`** | `module.driver.rtc.err_adjustment_period`（既定 10 Seconds） | 同上 |
| **Adjustment Type (Plus-Minus)** | **`NONE`**（既定のまま） | `module.driver.rtc.err_adjustment_type` | 無補正 |
| **Error Adjustment Value** | **`0`**（既定のまま） | `module.driver.rtc.err_adjustment_value` | 無補正。**RA8P1 での上限は 63**（127 ではない） |

> 補正値は #212 以降で、実機の時計を長時間走らせて実誤差（ppm）を測ってから決める。
> 32.768 kHz 水晶の一般的な精度は ±20〜50 ppm（≒ 1.7〜4.3 秒/日）。

### 1-3: 割り込みの設定（Carry のみ有効、他は Disabled）

| 項目（GUI 表示） | 設定値 | プロパティ id |
|---|---|---|
| **Callback** | **`NULL`**（既定のまま） | `module.driver.rtc.p_callback` |
| **Alarm Interrupt Priority** | **`Disabled`** | `module.driver.rtc.alarm_ipl` |
| **Period Interrupt Priority** | **`Disabled`** | `module.driver.rtc.periodic_ipl` |
| **Carry Interrupt Priority** | **`Priority 12`** | `module.driver.rtc.carry_ipl` |

#### Alarm / Period を Disabled にする理由

本 Issue の用途はカレンダー時刻の読み書きだけで、アラーム・周期割り込みを使わない。

#### Carry は Disabled にできない（そして、してはいけない）

`module.driver.rtc.carry_ipl` の選択肢は `enum.mcu.nvic.priorities.mandatory`（必須）で、
Alarm / Period の `optional` と型が違うため **GUI に Disabled の選択肢が無い**。
これは仕様どおりで、**Carry 割り込みは `R_RTC_CalendarTimeGet()` の動作に必須**である:

- `R_RTC_CalendarTimeGet()` には carry IRQ の割当を要求するチェックがある
  （`ra/fsp/src/r_rtc/r_rtc.c` の同関数冒頭
  `FSP_ERROR_RETURN(p_instance_ctrl->p_cfg->carry_irq >= 0, FSP_ERR_IRQ_BSP_DISABLED)`）。
  **ただし本プロジェクトではこのチェックは無効化されている**（下記）。
  したがって「未割当ならエラーが返る」ことを当てにできない
- カレンダーは複数レジスタにまたがるため、読み出し中に桁上がりが起きると不正な値になる。
  FSP はこれを `do { ... } while (p_instance_ctrl->carry_isr_triggered)` で読み直して回避しており、
  そのフラグを立てるのが `rtc_carry_isr()`（`r_rtc.c` の同 ISR、`p_ctrl->carry_isr_triggered = true`）
- 呼び出し時に carry IRQ が無効なら、FSP が一時的に有効化して読み終わったら元に戻す

##### ⚠️ 本プロジェクトでは FSP のパラメータチェックが全て無効化されている

`r_rtc_cfg.h` は `RTC_CFG_PARAM_CHECKING_ENABLE (BSP_CFG_PARAM_CHECKING_ENABLE)` を定義し、
本プロジェクトの `BSP_CFG_PARAM_CHECKING_ENABLE` は **`(0)`**（`ra_cfg/fsp_cfg/bsp/bsp_cfg.h:33`。
`BSP_CFG_ASSERT` も `(0)`、同 `:34`）。したがって `r_rtc.c` 内の
`#if RTC_CFG_PARAM_CHECKING_ENABLE` ブロックは**すべてコンパイル時に消える**。

**帰結: 上記の `FSP_ERR_IRQ_BSP_DISABLED` チェックは存在しない。**
仮に carry を未割当（`FSP_INVALID_VECTOR` = −1）にすると:

1. `NVIC_GetEnableIRQ(-1)` は CMSIS 側が負値をガードして 0 を返す
   （`ra/arm/CMSIS_6/CMSIS/Core/Include/core_cm85.h:3948-3958`）
2. その結果「無効だから有効化しよう」の分岐に入り、`r_rtc_irq_set(true, R_RTC_RCR1_CIE_Msk)` と
   `R_BSP_IrqEnable(-1)` が実行される。後者は `R_BSP_IrqClearPending(-1)` を呼ぶ
   （`ra/fsp/src/bsp/mcu/all/bsp_irq.h:166-173`）
3. 加えて、読み出し中の桁上がりが検出されないまま不正な時刻が返る

→ **Carry を有効にすることは必須。** GUI に Disabled が無いのは正しい設計。

> これは本手順書「#212 への申し送り (2)」と同じ
> **「分岐がコンパイル時（`#if`）か実行時（`if`）か」**（CLAUDE.md 成果物の検証ルール 1）の罠である。
> 本手順書の初版はこの節でまさに同じ誤りを犯していた（PR #215 のレビューで検出・修正）。

**優先度 12 を選ぶ理由**: 本プロジェクトの既定値。`configuration.xml` 内の
`board.icu.common.irq.priority12` は 16 箇所で使われており、他は priority2 が 3 箇所、
priority4 が 2 箇所のみ。

**Callback は NULL のままでよい**: `r_rtc` の制約
「A callback function is required when interrupts are enabled」は条件式が
`("${module.driver.rtc.p_callback}" != "NULL") || ("${module.driver.rtc.alarm_ipl}" === "_disabled" && "${module.driver.rtc.periodic_ipl}" === "_disabled")`
であり、**`carry_ipl` は判定に含まれない**。Alarm / Period を Disabled にしてあれば成立する。

`rtc_carry_isr()` はフラグを立てるだけでカーネル API を呼ばない。ISR 冒頭・末尾の
`FSP_CONTEXT_SAVE` / `FSP_CONTEXT_RESTORE` は、本プロジェクトでは
`BSP_CFG_RTOS = 0`（`ra_cfg/fsp_cfg/bsp/bsp_cfg.h:14-22`）のため**空に展開される**
（`ra/fsp/src/bsp/mcu/all/bsp_common.h:60-65`）。μT-Kernel 3.0 との相性の問題はない。

### 1-4: ドライバ設定（`r_rtc_cfg.h`）

`r_rtc` の `<config>` が生成する設定は 2 つだけ。**いずれも既定のまま**でよい。

| マクロ | 既定 | プロパティ id |
|---|---|---|
| `RTC_CFG_PARAM_CHECKING_ENABLE` | `(BSP_CFG_PARAM_CHECKING_ENABLE)` | `config.driver.rtc.param_checking_enable` |
| `RTC_CFG_OPEN_SET_CLOCK_SOURCE` | `(1)` = Enabled | `config.driver.rtc.open_set_source_clock` |

> `RTC_CFG_OPEN_SET_CLOCK_SOURCE` を Enabled のままにすると、`R_RTC_Open()` の中で
> クロック源の設定と RTC ソフトウェアリセットが行われる。Disabled にすると
> `R_RTC_ClockSourceSet()` をアプリから別途呼ぶ必要がある。
> **Enabled のままとし、`R_RTC_Open()` に集約する**（呼び出し順の間違いを作らないため）。

---

## 手順 2: 変更しない項目の確認

以下は**触らない**。誤って変更すると既存機能が壊れる。

| タブ / ファイル | 作業 | 理由 |
|---|---|---|
| **Pins タブ** | **不要** | P214 = `XCOUT`、P215 = `XCIN` が既に割当済み（`configuration.xml:1238-1239`）。RTC は内部でサブクロックを受けるので追加のピン割当は無い |
| **Clocks タブ（`e2studio/solution.xml`）** | **不要** | `subclock_populated` は既に enabled（`solution.xml:292`）、`board.clock.subclk.freq.32768` も設定済み。システムクロック源（PLL1P）を変更してはいけない |
| **`e2studio_CPU1/configuration.xml`** | **不要** | RTC は CPU0 のみで使う。実際、CPU1 側で生成しても `configuration.xml` に差分は出ない |

### `.secure_azone` は CPU0・CPU1 の**両方**を生成する（自動更新される）

`.secure_azone` は手編集しない。RTC を追加すると **CPU0 側は自動で更新される**が、
**CPU1 側は CPU1 プロジェクトでも Generate Project Content を実行しないと更新されない。**

| ファイル | 追加される内容 |
|---|---|
| `e2studio_CPU0/.secure_azone` | `<peripheral name="RTC.CPU0" security="s"/>`、`<slot name="IRQ22.CPU0" secure="true"/>` |
| `e2studio_CPU1/.secure_azone` | 上記 2 行に加えて `<assign peripheral="RTC"/>`、`<assign peripheral="ICU.IRQ22"/>`（CPU1 側のファイルだけが `<assign>` セクションを持つ） |

> **⚠️ 片方だけ生成するとドリフトする。**
> CPU1 の `.secure_azone` は CPU0 のペリフェラルも列挙しており、両ファイルの `<partition>` 部は
> 一致していなければならない。Issue #202 では CPU0 からのみ GPT1 を削除して CPU1 に取り残し、
> [PR #210](https://github.com/grace2riku/mimamori-sense/pull/210) で別途修正することになった。
> 正しい例は `18883bd`（Issue #45）で、両ファイルが同一コミットで更新されている。
>
> **手順: CPU0 で Generate → CPU1 プロジェクトを選択して Generate → 両ファイルの `<partition>` 部が一致することを確認する。**

---

## 手順 3: コード生成とビルド確認

### コード生成

1. `configuration.xml` を保存
2. **Generate Project Content** をクリック

> **コード生成は GUI で実施すること。** ヘッドレス生成では `ra_gen/*.c` にコードフォーマッタがかからず、
> コミットする生成物の整形がコミット済みのものと揃わない。

### 生成されるファイルの確認

| ファイル | 期待される変化 |
|---|---|
| `e2studio_CPU0/ra/fsp/src/r_rtc/r_rtc.c` | **新規生成** |
| `e2studio_CPU0/ra/fsp/inc/instances/r_rtc.h` | 新規生成 |
| `e2studio_CPU0/ra/fsp/inc/api/r_rtc_api.h` | 新規生成 |
| `e2studio_CPU0/ra_cfg/fsp_cfg/r_rtc_cfg.h` | 新規生成（`RTC_CFG_PARAM_CHECKING_ENABLE` / `RTC_CFG_OPEN_SET_CLOCK_SOURCE`） |
| `e2studio_CPU0/ra_gen/hal_data.h` | `#include "r_rtc.h"` と `extern rtc_instance_ctrl_t g_rtc_ctrl;` / `extern const rtc_cfg_t g_rtc_cfg;` が追加される |
| `e2studio_CPU0/ra_gen/hal_data.c` | `g_rtc_cfg` の定義が追加される。**`clock_source` が `RTC_CLOCK_SOURCE_SUBCLK` になっていることを目視確認する** |
| `e2studio_CPU0/configuration.xml` | `module.driver.rtc_on_rtc.*` の `<module>` と、`<context id="_hal.0">` への `<stack>` が追加される |

### ビルド確認

1. プロジェクトをビルドする
2. エラー・警告が出ないことを確認する
3. FLASH 使用量の増分を記録する（`.flash.endof` で測る）

---

## #212 への申し送り（実装時の注意）

**#212 の「設計の入力」に転記すること。** CLAUDE.md「依存先APIの最悪所要時間を実測・裏取りする」に基づき、
`r_rtc.c`（FSP 6.3.0）と BSP を読んで確認した内容。

### (1) `R_RTC_Open()` にはタイムアウトが無い

`R_RTC_Open()` → `r_rtc_set_clock_source()`（`RTC_CFG_OPEN_SET_CLOCK_SOURCE` = Enabled のため実行される）の中の待ち:

| 待ち | 行（FSP 6.3.0 `r_rtc.c`） | 内容 |
|---|---|---|
| ソフトウェアディレイ | `:1073` | `R_BSP_SoftwareDelay(BSP_PRV_RTC_RESET_DELAY_US, ...)`。値は **200 µs**（`ra/fsp/src/bsp/mcu/all/bsp_clocks.h:527`） |
| START ビット同期待ち | `:1043`（`r_rtc_start_bit_update()`） | `FSP_HARDWARE_REGISTER_WAIT(R_RTC->RCR2_b.START, value)` |
| CNTMD 同期待ち | `:1091` | `FSP_HARDWARE_REGISTER_WAIT(R_RTC->RCR2_b.CNTMD, RTC_CALENDAR_MODE)` |
| ソフトウェアリセット完了待ち | `:1057`（`r_rtc_software_reset()`） | `FSP_HARDWARE_REGISTER_WAIT(R_RTC->RCR2_b.RESET, 0U)` |
| RCR1 反映待ち | `:1100` | `FSP_HARDWARE_REGISTER_WAIT(R_RTC->RCR1, 0)` |
| HR24 反映待ち | `:1109` | `FSP_HARDWARE_REGISTER_WAIT(R_RTC->RCR2_b.HR24, 1)` |

**`FSP_HARDWARE_REGISTER_WAIT` の実体は `while (reg != required_value) { }` で、タイムアウトも試行上限も無い**
（`e2studio_CPU0/ra/fsp/src/bsp/mcu/all/bsp_common.h:122`）。

上表の待ちは、いずれも「カウントソース（＝サブクロック）に同期して更新される」ビットが対象である
（`r_rtc.c` の各コメントがハードウェアマニュアルの RCR1 / RCR2 の記述を引用している）。したがって:

- **正常時**: 200 µs ＋ 各同期待ち。32.768 kHz の 1 周期は約 30.5 µs なので、合計は数百 µs オーダ
- **サブクロックが発振していない場合**: 対象ビットが更新されず、**`R_RTC_Open()` は戻らない**

### (2) サブクロックの安定化待ちは、本プロジェクトでは**コンパイル時に消えている**

BSP は起動時にサブクロックを起動するが、**安定化待ちは行わない**。

1. `bsp_clock_init()`（`bsp_clocks.c:2240`）が `bsp_prv_sosc_init()`（`:3178`）を呼ぶ（`:2326`）
2. `bsp_prv_sosc_init()` は SOSC を起動する（`:3223` の `R_SYSTEM->SOSCCR = 0U`）
3. その直後の安定化待ちは `#if` で囲まれている（`:3229-3230`）:
   ```c
   #if (BSP_CLOCKS_SOURCE_CLOCK_SUBCLOCK == BSP_CFG_CLOCK_SOURCE) || (BSP_PRV_HOCO_USE_FLL)
       R_BSP_SubClockStabilizeWait(BSP_CLOCK_CFG_SUBCLOCK_STABILIZATION_MS);
   #endif
   ```
4. SOSC がリセット時点で既に動いていた場合は `R_BSP_SubClockStabilizeWaitAfterReset()` を呼ぶ（`:3241`）が、
   **その関数の本体も同じ条件で囲まれている**（`:2856-2865`、条件は `:2858`）

本プロジェクトの実際の値:

| マクロ | 値 | 根拠 |
|---|---|---|
| `BSP_CFG_CLOCK_SOURCE` | `BSP_CLOCKS_SOURCE_CLOCK_PLL1P` | `e2studio_CPU0/ra_gen/bsp_clock_cfg.h:28` |
| `BSP_CFG_FLL_ENABLE` | `(0)` | `e2studio_CPU0/ra_cfg/fsp_cfg/bsp/bsp_mcu_family_cfg.h:41` |
| `BSP_PRV_HOCO_USE_FLL` | `(0)` | `bsp_clocks.h:515-522`（`BSP_FEATURE_CGC_HAS_FLL && BSP_CFG_FLL_ENABLE && BSP_CLOCK_CFG_SUBCLOCK_POPULATED` が偽） |

→ 条件は両方とも偽。**どちらの経路でも待ちは 1 命令も生成されない。**

**したがって `e2studio/solution.xml:294` の `subclock_stabilization_ms = 1000` は、本プロジェクトでは何の効果もない。**
これは CLAUDE.md「分岐がコンパイル時（`#if`）か実行時（`if`）か」の該当例であり、
設定値を見ただけで「1 秒待ってくれる」と考えると誤る。

**#212 での帰結:**

- コールドスタート直後（VBATT 電池を抜いた後の初回投入など）に `R_RTC_Open()` を呼ぶと、
  水晶の発振立ち上がり時間を `R_RTC_Open()` 内の無限待ちで吸収することになる
- 通常運用ではコイン電池により SOSC がリセットをまたいで動き続けるため、この待ちは問題にならない見込み。
  **ただし「見込み」なので、`time_ctrl_init()` の配置は「最悪、ここで止まっても切り分けられる」位置にする**
- 実機確認は、**サブクロックの発振を単独で確認してから `R_RTC_Open()` を呼ぶ順序**で行うこと
  （J-Link 接続下で `R_RTC_Open()` にハングすると原因の切り分けが難しい）

### (3) `R_RTC_CalendarTimeGet()` は Carry 割り込みに依存する

手順 1-3 に書いたとおり、Carry 割り込みは必須。#212 で時刻読み出しを実装する際の制約:

- **割り込み禁止区間から呼んではいけない。** `R_RTC_CalendarTimeGet()` は
  `do { ... } while (p_instance_ctrl->carry_isr_triggered)` で読み直す設計で、
  そのフラグを立てるのは `rtc_carry_isr()`。ISR が走れない区間で呼ぶと桁上がりを検出できない
  （`tk_dis_dsp()` はタスクディスパッチを止めるだけで割り込みは止めないので、こちらは問題ない）
- **ループ回数の上界**: 桁上がりは 1 秒に 1 回なので、実質 1 回の読み直しで抜ける
- 呼び出し時に carry IRQ が無効だと FSP が一時的に有効化して元に戻す。
  他所で carry IRQ を触ると干渉するため、**アプリ側から carry IRQ を操作しないこと**

### (4) FSP のパラメータチェックは全て無効 ― `time_ctrl` が自前で防御する

`BSP_CFG_PARAM_CHECKING_ENABLE = (0)`（`ra_cfg/fsp_cfg/bsp/bsp_cfg.h:33`、`BSP_CFG_ASSERT` も `(0)`）
のため、`r_rtc.c` の `#if RTC_CFG_PARAM_CHECKING_ENABLE` ブロックは全てコンパイル時に消える。
**FSP が守ってくれる前提で設計してはいけない。**

消えるチェックと、`time_ctrl` 側で持つべき責務:

| 消えるチェック | 帰結 | `time_ctrl` の責務 |
|---|---|---|
| `r_rtc_time_and_date_validate(p_time)`（`R_RTC_CalendarTimeSet()` 内） | **2月30日・25時などがそのまま BCD レジスタに書かれる。エラーにならない** | `time_ctrl_set()` が**自前で妥当性検証**する（月ごとの日数・うるう年・時分秒の範囲） |
| `FSP_ERROR_RETURN(RTC_OPEN == p_instance_ctrl->open, FSP_ERR_NOT_OPEN)` | `R_RTC_Open()` 前に Get/Set を呼んでも検出されない | `time_ctrl` が**自前で初期化済みフラグ**を持ち、未初期化なら API を呼ばずにエラーを返す |
| `FSP_ERROR_RETURN(p_cfg->carry_irq >= 0, FSP_ERR_IRQ_BSP_DISABLED)` | 手順 1-3 の「Carry は Disabled にできない」の節を参照 | 設定側で担保済み（carry_ipl = 12） |
| `FSP_ASSERT(NULL != p_instance_ctrl)` / `FSP_ASSERT(p_time)` | NULL 逆参照が検出されない | `time_ctrl` の公開 API で引数を検証する |

### (5) `R_RTC_CalendarTimeSet()` も無限待ちを持ち、曜日は自分で計算する必要がある

`R_RTC_CalendarTimeSet()`（`r_rtc.c`）の性質:

- **無限待ち**: `r_rtc_start_bit_update(0U)` と `r_rtc_start_bit_update(1U)` を前後で呼ぶ。
  それぞれが `FSP_HARDWARE_REGISTER_WAIT(R_RTC->RCR2_b.START, value)`（`r_rtc.c:1043`）を含む
- さらに **`clock_source == RTC_CLOCK_SOURCE_SUBCLK` のとき（＝本プロジェクトの設定）**
  `r_rtc_error_adjustment_set()` が呼ばれ、その中にも
  `FSP_HARDWARE_REGISTER_WAIT(R_RTC->RADJ, ...)` / `(R_RTC->RCR2_b.AADJE, mode)` /
  `(R_RTC->RCR2_b.AADJP, period)` がある（`r_rtc.c:1589,1593,1607,1625` 付近）。
  → **(1)(2) と同じく、サブクロックが止まっていれば戻らない**
- **`tm_wday` は FSP が導出しない。** `R_RTC->RWKCNT = rtc_dec_to_bcd(p_time->tm_wday)` と
  そのまま書き込まれる。**`time_ctrl_set()` が年月日から曜日を計算して渡すこと**
  （矛盾した曜日を渡すと、そのまま矛盾した値が RTC に入る）
- `tm_mon` は 0 起点、`tm_year` は 1900 起点（C 標準 `struct tm` 互換）。
  FSP が `+1` / `-RTC_C_TIME_OFFSET` して BCD に変換する

### (6) FLASH 増分は本 Issue ではほぼゼロ ― ドライバはまだリンクされない

本 Issue の時点では **`R_RTC_Open()` を呼ぶコードが無い**ため、LTO と `--gc-sections` により
`r_rtc.c` の中身はイメージから除去される。実際にリンクされているのはベクタテーブルから
参照される `rtc_carry_isr`（48 B）だけで、`R_RTC_Open` / `g_rtc_cfg` / `g_rtc` は
ELF のシンボルテーブルに存在しない（`llvm-nm` で確認済み）。

**帰結:**

- 本 Issue の PR で「FLASH 増分 ≒ 48 B」と報告する場合は、**この理由を併記する**こと。
  そうしないと「RTC ドライバは軽い」と誤読される
- **RTC ドライバの実コストは #212 で `time_ctrl` が API を呼んだ時点で現れる。**
  #212 側で改めて FLASH 増分を測ること
- 同じ理由で、**本 Issue の状態では RTC は実行時に初期化されない**（`R_RTC_Open()` が呼ばれないため）。
  カレンダーの進行を実機で確認するには暫定コードが要る。#211 / #212 のどちらで確認するかは
  Issue 側で決める

---

## 受け入れ確認チェックリスト

- [ ] Stacks タブの HAL/Common に RTC (r_rtc) が追加されている
- [ ] **Clock Source が `Sub-Clock` になっている**（既定の LOCO のままでない）
- [ ] Alarm / Period が Disabled、**Carry が Priority 12**、Callback が NULL
- [ ] Error Adjustment が無補正（Mode = Disabled、Period = NONE、Type = NONE、Value = 0）
- [ ] `e2studio_CPU0/ra/fsp/src/r_rtc/r_rtc.c` が生成されている
- [ ] `e2studio_CPU0/ra_gen/hal_data.h` に `g_rtc_ctrl` / `g_rtc_cfg` が現れる
- [ ] `e2studio_CPU0/ra_gen/hal_data.c` の `g_rtc_cfg` で `clock_source` が `RTC_CLOCK_SOURCE_SUBCLK` になっている
- [ ] Pins タブ・Clocks タブ・`e2studio_CPU1/configuration.xml` に差分が出ていない
- [ ] **CPU1 側でも Generate Project Content を実行し、`e2studio_CPU0/.secure_azone` と
      `e2studio_CPU1/.secure_azone` の `<partition>` 部が一致している**
- [ ] CPU0 のビルドが通り、FLASH 使用量を記録した（増分がほぼゼロになる理由は「#212 への申し送り (6)」を併記）
- [ ] 上記「#212 への申し送り」(1)〜(6) を Issue #212 の「設計の入力」に転記した

### 実機での動作確認は #212 で行う

**本 Issue には実機確認の受け入れ条件を置かない。** 理由は「#212 への申し送り (6)」のとおりで、
`R_RTC_Open()` を呼ぶコードが無い本 Issue の状態では、LTO と `--gc-sections` により
RTC ドライバがイメージから除去され、**RTC は実行時に初期化されない**。
確認のためだけに暫定コードを書いても #212 で捨てることになる。

#212 で以下を確認する（#212 の受け入れ条件に含めること）:

- サブクロックが発振している
- RTC のカレンダーが 1 秒周期で進む
- **リセットおよび電源断をまたいで時刻が保持される**（VBATT コイン電池の効果確認）
- RTC ドライバがリンクされた状態での FLASH 増分

---

## 参照情報

| 項目 | 参照先 |
|---|---|
| Issue | [#211](https://github.com/grace2riku/mimamori-sense/issues/211)（本手順書）、[#212](https://github.com/grace2riku/mimamori-sense/issues/212)（アプリ実装） |
| FSP モジュール定義 | `Renesas.RA.6.3.0.pack` → `.module_descriptions/Renesas##HAL Drivers##all##r_rtc####6.3.0.xml` |
| RA8P1 BSP 定義（ファミリ） | `Renesas.RA_mcu_ra8p1.6.3.0.pack` → `.module_descriptions/Renesas##BSP##ra8p1##device####6.3.0.xml` |
| RA8P1 BSP 定義（enum / プロパティ） | 同 → `.module_descriptions/Renesas##BSP##ra8p1##fsp####6.3.0.xml` |
| RTC ドライバ実装 | `e2studio_CPU0/ra/fsp/src/r_rtc/r_rtc.c`（生成後） |
| BSP feature | `e2studio_CPU0/ra/fsp/src/bsp/mcu/ra8p1/bsp_feature.h:491-497` |
| BSP クロック初期化 | `e2studio_CPU0/ra/fsp/src/bsp/mcu/all/bsp_clocks.c:2240, 2326, 3178, 3223, 3229-3241` |
| 既存の FSP 手順書 | `doc/fsp-setup-guide/issue-12-gpt-camera-xclk.md`（単一モジュール追加の例）、`issue-45-audio-output-modules.md` |
