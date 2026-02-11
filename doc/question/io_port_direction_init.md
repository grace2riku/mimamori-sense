# IOポート入出力方向の初期化について

## 質問

e2studio_CPU0, e2studio_CPU1のプロジェクトで質問です。
LED0(P600), LED1(P303)をトグル出力していると思います。

IOポートの入出力方向の設定はどこで初期化していますか?

## 回答

### 概要

IOポートの入出力方向は、FSP (Flexible Software Package) の IOPORTドライバによって初期化されています。
ピンの方向設定データは **CPU0側のプロジェクト** で定義されており、CPU1側にはピン設定データがありません。

### ピン設定データの定義場所

**e2studio_CPU0/ra_gen/pin_data.c** に全ピンの設定が定義されています。

LED0 (P600) の設定 (261-263行目):
```c
{ .pin = BSP_IO_PORT_06_PIN_00, .pin_cfg = ((uint32_t) IOPORT_CFG_DRIVE_HIGH
        | (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT
        | (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW) },
```

LED1 (P303) の設定 (121-123行目):
```c
{ .pin = BSP_IO_PORT_03_PIN_03, .pin_cfg = ((uint32_t) IOPORT_CFG_DRIVE_HIGH
        | (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT
        | (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW) },
```

いずれも以下の設定です:
- **駆動能力**: IOPORT_CFG_DRIVE_HIGH (高駆動)
- **方向**: IOPORT_CFG_PORT_DIRECTION_OUTPUT (出力)
- **初期値**: IOPORT_CFG_PORT_OUTPUT_LOW (Low出力 = LED消灯)

### CPU1側のピン設定

**e2studio_CPU1/ra_gen/pin_data.c** ではピン設定が空です:
```c
const ioport_cfg_t g_bsp_pin_cfg =
        { .number_of_pins = 0, .p_pin_cfg_data = NULL };
```

CPU1はCPU0が設定済みのIOポートをそのまま利用します。

### 初期化の呼び出し箇所

両プロジェクトとも **src/hal_warmstart.c** の47行目で `R_IOPORT_Open` を呼び出しています:

```c
if (BSP_WARM_START_POST_C == event)
{
    /* C runtime environment and system clocks are setup. */

    /* Configure pins. */
    R_IOPORT_Open(&IOPORT_CFG_CTRL, &IOPORT_CFG_NAME);
```

### 初期化の流れ

```
R_BSP_WarmStart(BSP_WARM_START_POST_C)          (hal_warmstart.c)
  -> R_IOPORT_Open(&IOPORT_CFG_CTRL, &IOPORT_CFG_NAME)  (hal_warmstart.c:47)
       -> r_ioport_pins_config(p_cfg)            (r_ioport.c)
            -> pin_data.c の g_bsp_pin_cfg_data[] 配列の各エントリを
               レジスタ (PDR等) に書き込み
```

### pin_data.c の生成元

`pin_data.c` はFSPコンフィグレータ (e2studioのGUI) で自動生成されるファイルです。
e2studioの「Pins」タブでピンの機能・方向・駆動能力などを設定すると、このファイルが再生成されます。
手動で編集することも可能ですが、コンフィグレータで再生成すると上書きされるため注意が必要です。

### 関連ファイル一覧

| ファイル | 役割 |
|---------|------|
| e2studio_CPU0/ra_gen/pin_data.c | ピン設定データ (P600, P303含む全ピン) |
| e2studio_CPU0/ra_cfg/fsp_cfg/bsp/bsp_pin_cfg.h | ピンマクロ定義 |
| e2studio_CPU0/src/hal_warmstart.c | R_IOPORT_Open呼び出し (47行目) |
| e2studio_CPU0/ra/fsp/src/r_ioport/r_ioport.c | FSP IOPORTドライバ実装 |
| e2studio_CPU0/ra_gen/common_data.c | IOPORTインスタンス生成 |
| e2studio_CPU1/ra_gen/pin_data.c | CPU1側ピン設定 (空) |
| e2studio_CPU1/src/hal_warmstart.c | CPU1側のR_IOPORT_Open呼び出し |
