# UART g_jlink_console の移植と使い方

## 質問1

quickstart_ek_ra8p1_epのUART g_jlink_consoleをe2studio_CPU0プロジェクトに移植したいと思います。
以下の手順で問題ないか確認してください。
過不足な設定手順があれば適宜、訂正し説明をお願いします。

- configuration.xmlを開き、StackタブからNew Stackを選択する
- ウィンドウ -> ビューの表示 -> プロパティーを選択する。
- プロパティタブからUARTの各種設定をquickstart_ek_ra8p1_epプロジェクトと同様に行う。
- Generate Project Contentを実行し、UARTのコードを自動生成する

## 回答1

記載いただいた手順は基本的な流れとして正しいですが、**ピン設定**と**コールバック関数名の指定**と**ソースファイルのコピー**が不足しています。
以下に正確な手順を示します。

### 手順1: UART Stackの追加

1. e2studio_CPU0の `configuration.xml` を開く
2. **Stacks** タブを選択する
3. **New Stack** -> **Connectivity** -> **UART (r_sci_b_uart)** を選択する

### 手順2: UARTプロパティの設定

1. 追加したUARTスタックを選択した状態で **プロパティ** タブを開く
2. 以下の設定をquickstart_ek_ra8p1_epプロジェクトと同様に行う

| 設定項目 | 設定値 |
|---------|-------|
| Name | g_jlink_console |
| Channel | 8 |
| Data Bits | 8bit |
| Parity | None |
| Stop Bits | 1bit |
| Baud Rate | 115200 |
| Callback | jlink_console_callback |
| Receive Interrupt Priority | 4 |
| Transmit Interrupt Priority | 12 |
| Transmit End Interrupt Priority | 12 |
| Error Interrupt Priority | 12 |

参考: quickstart_ek_ra8p1_epの設定ファイル
- `reference_projects/quickstart_ek_ra8p1_ep/e2studio/ra_gen/hal_data.c` (3623-3692行目)

### 手順3: ピン設定

1. **Pins** タブを選択する
2. SCI8のピンを以下のように設定する

| ピン | 機能 | 備考 |
|------|------|------|
| P502 | SCI8 TXD | 送信ピン |
| P601 | SCI8 RXD | 受信ピン |

**注意**: e2studio_CPU0では現在 P502 が `IOPORT_PERIPHERAL_PDM` (PDM機能) に割り当てられています (pin_data.c 203行目)。
UARTに変更すると PDM機能が使えなくなるため、PDM機能との競合がないか確認してください。

参考: quickstart_ek_ra8p1_epのピン設定
- TXD: `BSP_IO_PORT_05_PIN_02` に `IOPORT_PERIPHERAL_SCI0_2_4_6_8` (pin_data.c 119行目)
- RXD: `BSP_IO_PORT_06_PIN_01` に `IOPORT_PERIPHERAL_SCI0_2_4_6_8` (pin_data.c 164行目)

### 手順4: コード自動生成

1. **Generate Project Content** ボタンをクリックする
2. 以下のファイルが自動生成・更新される
   - `ra_gen/hal_data.c` — UARTインスタンス定義 (g_jlink_console)
   - `ra_gen/hal_data.h` — UARTインスタンス外部宣言
   - `ra_gen/pin_data.c` — ピン設定にSCI8のTXD/RXDが追加
   - `ra_gen/vector_data.c` — SCI8の割り込みベクタが追加
   - `ra_cfg/fsp_cfg/r_sci_b_uart_cfg.h` — UART設定マクロ

### 手順5: ソースファイルのコピー

quickstart_ek_ra8p1_epプロジェクトから以下のファイルをe2studio_CPU0の `src/` ディレクトリにコピーします。

| コピー元 | コピー先 |
|---------|---------|
| `reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/jlink_console.c` | `e2studio_CPU0/src/jlink_console.c` |
| `reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/jlink_console.h` | `e2studio_CPU0/src/jlink_console.h` |

**注意**: `jlink_console.c` は FreeRTOS の `vTaskDelay()` を使用しています。
e2studio_CPU0でFreeRTOSを使用していない場合は、待機処理を適宜修正してください。

### 手順6: アプリケーションコードからの呼び出し

`hal_entry.c` 等から初期化と使用を行います。

```c
#include "jlink_console.h"

// 初期化
jlink_console_init();

// 送信
print_to_console("Hello UART!\r\n");
```

---

## 質問2

生成されたUARTのコードの初期化、UART送信・受信の手順について解説してください。

## 回答2

quickstart_ek_ra8p1_epプロジェクトの `src/jlink_console.c` を参考に、UARTの初期化・送信・受信の手順を解説します。

### 1. 初期化

**ファイル**: `reference_projects/quickstart_ek_ra8p1_ep/e2studio/src/jlink_console.c` (117-131行目)

```c
void jlink_console_init(void)
{
    fsp_err_t fsp_err = FSP_SUCCESS;

    fsp_err = R_SCI_B_UART_Open(&g_jlink_console_ctrl, &g_jlink_console_cfg);

    if (FSP_SUCCESS == fsp_err)
    {
        s_configured = true;
    }
    return;
}
```

- `R_SCI_B_UART_Open()` に制御構造体 (`g_jlink_console_ctrl`) と設定構造体 (`g_jlink_console_cfg`) を渡す
- これらの構造体は **Generate Project Content** で自動生成される (`ra_gen/hal_data.c`)
- 成功すると SCI8チャネルが初期化され、割り込みが有効になる

### 2. UART送信

**ファイル**: `jlink_console.c` (208-225行目)

```c
static void jlink_console_write(const char_t *buffer)
{
    fsp_err_t err = FSP_SUCCESS;

    g_transfer_done = false;

    err = R_SCI_B_UART_Write(&g_jlink_console_ctrl, (uint8_t *)buffer, (uint32_t)strlen(buffer));

    assert(FSP_SUCCESS == err);

    while (!g_transfer_done)
    {
        vTaskDelay(1);
    }
}
```

**送信の流れ**:
1. 送信完了フラグ `g_transfer_done` を `false` にリセットする
2. `R_SCI_B_UART_Write()` を呼び出し、非同期送信を開始する
   - 引数: 制御構造体、送信データバッファ、送信バイト数
3. 送信は割り込み駆動で1バイトずつ送信される
4. 全データの送信が完了するとコールバック関数が `UART_EVENT_TX_COMPLETE` イベントで呼ばれる
5. `while (!g_transfer_done)` ループで送信完了を待つ

**上位関数** `print_to_console()` (139-157行目) は、初期化チェック付きのラッパーです:

```c
fsp_err_t print_to_console(char_t * p_data)
{
    while(!jlink_configured())
    {
        jlink_console_init();
    }

    jlink_console_write(p_data);
    vTaskDelay(50);  // スロットリング

    return (err);
}
```

### 3. UART受信

**ファイル**: `jlink_console.c` (165-174行目, 251-265行目)

#### 受信開始 (非ブロッキング)

```c
void start_key_check(void)
{
    s_rx_buf       = 0;
    g_receive_done = false;

    R_SCI_B_UART_Read(&g_jlink_console_ctrl, &s_rx_buf, 1);
}
```

- `R_SCI_B_UART_Read()` を呼び出し、1バイトの非同期受信を開始する
- データ受信完了時にコールバック関数が `UART_EVENT_RX_COMPLETE` イベントで呼ばれる

#### 受信待ち (ブロッキング)

```c
int8_t input_from_console(void)
{
    start_key_check();

    while (key_pressed() == false)
    {
        vTaskDelay(1);
    }

    return ((int8_t)get_detected_key());
}
```

**受信の流れ**:
1. `start_key_check()` で1バイト受信を開始する
2. `key_pressed()` (= `g_receive_done` フラグ) で受信完了を待つ
3. 受信完了後、`get_detected_key()` で受信データ (`s_rx_buf`) を取得する

### 4. コールバック関数

**ファイル**: `jlink_console.c` (273-330行目)

```c
void jlink_console_callback(uart_callback_args_t *p_args)
{
    switch (p_args->event)
    {
        case UART_EVENT_RX_CHAR:
            // 1文字受信: バッファに蓄積
            s_out_of_band_received[s_g_out_of_band_index++] = (uint8_t) p_args->data;
            break;

        case UART_EVENT_RX_COMPLETE:
            // 受信完了: フラグをセット
            g_receive_done = 1;
            break;

        case UART_EVENT_TX_COMPLETE:
            // 送信完了: フラグをセット
            g_transfer_done = 1;
            break;

        default:
            break;
    }
}
```

| イベント | 発生タイミング | 処理内容 |
|---------|--------------|---------|
| UART_EVENT_RX_CHAR | 1文字受信ごと | 受信バッファ `s_out_of_band_received[]` に蓄積 |
| UART_EVENT_RX_COMPLETE | `R_SCI_B_UART_Read()` で指定したバイト数を受信完了 | `g_receive_done` フラグをセット |
| UART_EVENT_TX_COMPLETE | `R_SCI_B_UART_Write()` の全データ送信完了 | `g_transfer_done` フラグをセット |

### 5. 同期方式のまとめ

送信・受信ともに以下のパターンで同期しています:

```
[メイン処理]                    [割り込みコールバック]
    |                                  |
    | フラグ = false                    |
    | R_SCI_B_UART_Write/Read()        |
    |   (非同期処理開始)                |
    |                                  |
    | while(!フラグ)  <--- 待機 -----  | フラグ = true (完了通知)
    |   vTaskDelay(1)                  |
    |                                  |
    | 完了後の処理                      |
```

- FSPのUART APIは非同期 (ノンブロッキング) で動作する
- 完了通知はコールバック関数経由でグローバルフラグを使って行われる
- メイン処理側は `vTaskDelay()` でCPUを解放しながらフラグをポーリングする

---

## 質問3

quickstart_ek_ra8p1_epのUART g_jlink_consoleをe2studio_CPU0プロジェクトに移植しています。
以下のコンパイルエラーが発生している原因と対策を教えてください。

```
../src\r_typedefs.h:88:31: error: use of undeclared identifier 'DISPLAY_BUFFER_STRIDE_BYTES_INPUT1'
   88 |     uint8_t        pixel_data[DISPLAY_BUFFER_STRIDE_BYTES_INPUT1 * DISPLAY_VSIZE_INPUT1 * 2 + 1];
      |                               ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
../src\r_typedefs.h:88:68: error: use of undeclared identifier 'DISPLAY_VSIZE_INPUT1'
   88 |     uint8_t        pixel_data[DISPLAY_BUFFER_STRIDE_BYTES_INPUT1 * DISPLAY_VSIZE_INPUT1 * 2 + 1];
      |                                                                    ^~~~~~~~~~~~~~~~~~~~
../src/jlink_console.c:25:10: fatal error: 'common_utils.h' file not found
   25 | #include "common_utils.h"
      |          ^~~~~~~~~~~~~~~~
```

## 回答3

### エラーの概要

コンパイルエラーは2種類あり、いずれも **quickstart_ek_ra8p1_ep固有のファイルをそのままコピーしたことが原因** です。

### エラー1: `DISPLAY_BUFFER_STRIDE_BYTES_INPUT1` / `DISPLAY_VSIZE_INPUT1` が未定義

**発生箇所**: `e2studio_CPU0/src/r_typedefs.h` 88行目

```c
typedef struct
{
    uint16_t       width;
    uint16_t       height;
    uint16_t       bytes_per_pixel;
    uint8_t        pixel_data[DISPLAY_BUFFER_STRIDE_BYTES_INPUT1 * DISPLAY_VSIZE_INPUT1 * 2 + 1];
} st_full_image_rgb565_t;
```

**原因**:

`DISPLAY_BUFFER_STRIDE_BYTES_INPUT1` と `DISPLAY_VSIZE_INPUT1` は、quickstart_ek_ra8p1_epプロジェクトの `ra_gen/common_data.h` で定義されているマクロです。
quickstartプロジェクトにはGLCDC (Graphics LCD Controller) が設定されており、Generate Project Content時にディスプレイ関連のマクロが自動生成されます。

```c
// quickstart_ek_ra8p1_ep/e2studio/ra_gen/common_data.h で定義
#define DISPLAY_HSIZE_INPUT1  (1024)
#define DISPLAY_VSIZE_INPUT1  (600)
#define DISPLAY_BUFFER_STRIDE_BYTES_INPUT1  (((DISPLAY_HSIZE_INPUT1 * DISPLAY_BITS_PER_PIXEL_INPUT1 + 0x1FF) >> 9) << 6)
```

e2studio_CPU0プロジェクトにはGLCDCが設定されていないため、`ra_gen/common_data.h` にこれらのマクロが存在しません。
`r_typedefs.h` は22行目で `common_data.h` をインクルードしていますが、CPU0の `common_data.h` にはIOPORTの設定のみ含まれています。

**対策**:

`r_typedefs.h` の88行目の構造体 `st_full_image_rgb565_t` はディスプレイ用のフレームバッファ構造体です。
UART移植では不要なので、**未定義マクロを固定値に置き換える** か、**構造体定義自体を削除** してください。

固定値に置き換える場合:
```c
typedef struct
{
    uint16_t       width;
    uint16_t       height;
    uint16_t       bytes_per_pixel;
    uint8_t        pixel_data[4096 * 600 * 2 + 1];  // DISPLAY_BUFFER_STRIDE_BYTES_INPUT1=4096, DISPLAY_VSIZE_INPUT1=600
} st_full_image_rgb565_t;
```

### エラー2: `common_utils.h` が見つからない

**発生箇所**: `e2studio_CPU0/src/jlink_console.c` 25行目

```c
#include "common_utils.h"
#include "common_init.h"
#include "board_cfg.h"
```

**原因**:

`jlink_console.c` をquickstart_ek_ra8p1_epからそのままコピーしたため、quickstartプロジェクト固有のヘッダファイルへの依存が残っています。
これらのヘッダはe2studio_CPU0プロジェクトには存在しません。

| ヘッダ | quickstartでの役割 | jlink_console.cでの使用 |
|-------|-------------------|----------------------|
| `common_utils.h` | GUI構造体定義、ディスプレイ操作マクロ | **使用していない** |
| `common_init.h` | 初期化関数宣言、グローバル変数宣言 | **使用していない** |
| `board_cfg.h` | ボード固有設定 | **使用していない** |

調査の結果、`jlink_console.c` はこれら3つのヘッダから **何も使用していません**。
実際に必要なインクルードは以下のみです:

- `<stdio.h>` — 標準ライブラリ
- `<string.h>` — `strlen()` の使用 (214行目)
- `"r_typedefs.h"` — `char_t`, `bool_t` 型定義
- `"jlink_console.h"` — 関数宣言、`hal_data.h` と `r_sci_b_uart.h` の間接インクルード
- `"FreeRTOS.h"` — `vTaskDelay()` の使用

**対策**:

`jlink_console.c` の不要な `#include` を削除してください:

```c
// 変更前 (25-27行目)
#include "common_utils.h"
#include "common_init.h"
#include "board_cfg.h"

// 変更後: 上記3行を削除
```

### 対策まとめ

| 対策 | 対象ファイル | 修正内容 |
|------|------------|---------|
| 1 | `e2studio_CPU0/src/jlink_console.c` | 25-27行目の `#include "common_utils.h"`, `#include "common_init.h"`, `#include "board_cfg.h"` を削除 |
| 2 | `e2studio_CPU0/src/r_typedefs.h` | 88行目の `DISPLAY_BUFFER_STRIDE_BYTES_INPUT1 * DISPLAY_VSIZE_INPUT1` を固定値 `4096 * 600` に置き換えるか、`st_full_image_rgb565_t` 構造体を削除 |

これら2つの修正でコンパイルエラーは解消されます。

---

## 質問4

quickstart_ek_ra8p1_epのUART g_jlink_consoleをe2studio_CPU0プロジェクトに移植しています。
以下のコンパイルエラーが発生している原因と対策を教えてください。

```
../src/jlink_console.c:151:5: error: call to undeclared function 'vTaskDelay';
  ISO C99 and later do not support implicit function declarations
  151 |     vTaskDelay(50);
      |     ^
../src/jlink_console.c:276:2: error: call to undeclared function 'rand';
  ISO C99 and later do not support implicit function declarations
  276 |         rand();
      |         ^
```

## 回答4

### エラーの概要

`vTaskDelay` と `rand` の関数宣言が見つからないというエラーです。
質問3の対策で `common_utils.h`, `common_init.h`, `board_cfg.h` の3つの `#include` を削除しましたが、これらのヘッダが **間接的にインクルードしていたヘッダ** の依存が切れたことが原因です。

### 原因の詳細

quickstart_ek_ra8p1_epプロジェクトでは、削除した3つのヘッダが以下のヘッダを間接的にインクルードしていました:

| 削除したヘッダ | 間接インクルード | 提供する関数 |
|--------------|----------------|------------|
| `common_init.h` | `"task.h"` (26行目) | `vTaskDelay()` |
| `common_utils.h` | `<stdlib.h>` (21行目) | `rand()` |

つまり、`jlink_console.c` が直接使用する関数の宣言が、削除したヘッダ経由で解決されていました。

- `vTaskDelay()` — FreeRTOSのタスク遅延関数。`FreeRTOS.h` だけではなく `task.h` のインクルードが必要
- `rand()` — C標準ライブラリの乱数関数。`<stdlib.h>` のインクルードが必要

### 対策

`jlink_console.c` のインクルード部分に `<stdlib.h>` と `"task.h"` を追加してください:

```c
// 変更前
#include <stdio.h>
#include <string.h>

#include "r_typedefs.h"

//#include "common_utils.h"
//#include "common_init.h"
//#include "board_cfg.h"
#include "jlink_console.h"
#include "FreeRTOS.h"

// 変更後
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "r_typedefs.h"

#include "jlink_console.h"
#include "FreeRTOS.h"
#include "task.h"
```

**注意**: `"FreeRTOS.h"` は必ず `"task.h"` より先にインクルードする必要があります。
FreeRTOSの規約により、`FreeRTOS.h` が最初に読み込まれていないと `task.h` のコンパイルに失敗します。

### 回答3の補足

回答3で挙げた必要なインクルード一覧を以下のように訂正します:

| ヘッダ | 用途 |
|-------|------|
| `<stdio.h>` | 標準入出力 |
| `<string.h>` | `strlen()` |
| `<stdlib.h>` | `rand()` |
| `"r_typedefs.h"` | `char_t`, `bool_t` 型定義 |
| `"jlink_console.h"` | 関数宣言、`hal_data.h` / `r_sci_b_uart.h` の間接インクルード |
| `"FreeRTOS.h"` | FreeRTOS基本定義 (`FreeRTOS.h` は `task.h` より先に記述) |
| `"task.h"` | `vTaskDelay()` |
