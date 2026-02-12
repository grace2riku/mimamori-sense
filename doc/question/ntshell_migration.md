# NT-Shell (Natural Tiny Shell) の移植手順

## 質問

追加したThreadにはNTShellを組み込みたいと考えています。
e2studio_CPU0プロジェクトにNTShellの移植の手順を提案してください。

NTShell URL: https://cubeatsystems.com/ntshell/

## 回答

### NT-Shellとは

NT-Shell (Natural Tiny Shell) は組み込みシステム向けの軽量シェルライブラリです。

**特徴**:
- **軽量**: ROM 10KB、RAM 1KB
- **VT100互換**: ターミナルエミュレータで動作
- **シンプルなAPI**: 3つの関数のみ
- **依存関係なし**: C89準拠、動的メモリ割り当て不要
- **機能**: カーソル移動、コマンド履歴、行編集

### 移植の概要

NT-Shellの移植には以下が必要です:

1. ソースファイルの追加
2. シリアル読み書き関数の実装
3. コマンド処理コールバックの実装
4. Thread内での初期化と実行

### 手順1: ソースファイルの取得

GitHubからNT-Shellのソースコードを取得します。

```bash
git clone https://github.com/chiabrian/ntshell.git
```

または、公式サイトからダウンロード:
https://cubeatsystems.com/ntshell/

### 手順2: ソースファイルの配置

以下のファイルをプロジェクトに追加します。

```
e2studio_CPU0/
├── src/
│   └── ntshell/
│       ├── core/
│       │   ├── ntshell.c
│       │   ├── ntshell.h
│       │   ├── ntconf.h
│       │   ├── ntint.h
│       │   ├── ntlibc.c
│       │   ├── ntlibc.h
│       │   ├── text_editor.c
│       │   ├── text_editor.h
│       │   ├── text_history.c
│       │   ├── text_history.h
│       │   ├── vtrecv.c
│       │   ├── vtrecv.h
│       │   ├── vtsend.c
│       │   └── vtsend.h
│       └── util/
│           ├── ntopt.c        (オプション解析、必要に応じて)
│           └── ntopt.h
```

### 手順3: インクルードパスの追加

e2 studioでインクルードパスを追加します。

1. プロジェクトを右クリック → **Properties**
2. **C/C++ Build** → **Settings**
3. **GNU ARM Cross C Compiler** → **Includes**
4. 以下を追加:
   - `${workspace_loc:/${ProjName}/src/ntshell/core}`
   - `${workspace_loc:/${ProjName}/src/ntshell/util}`

### 手順4: シリアル読み書き関数の実装

NT-Shellにはシリアル通信用のコールバック関数が必要です。
既存の `jlink_console.c` を活用して実装します。

**src/ntshell_port.c** を作成:

```c
#include <string.h>
#include "ntshell.h"
#include "jlink_console.h"
#include "FreeRTOS.h"
#include "task.h"

/**
 * NT-Shell用シリアル読み込み関数
 * @param buf 読み込みバッファ
 * @param cnt 読み込みバイト数
 * @param extobj 拡張オブジェクト（未使用）
 * @return 実際に読み込んだバイト数
 */
int ntshell_serial_read(char *buf, int cnt, void *extobj)
{
    (void)extobj;
    int i;

    for (i = 0; i < cnt; i++)
    {
        /* 1文字受信（ブロッキング） */
        buf[i] = input_from_console();
    }

    return i;
}

/**
 * NT-Shell用シリアル書き込み関数
 * @param buf 書き込みバッファ
 * @param cnt 書き込みバイト数
 * @param extobj 拡張オブジェクト（未使用）
 * @return 実際に書き込んだバイト数
 */
int ntshell_serial_write(const char *buf, int cnt, void *extobj)
{
    (void)extobj;

    /* 文字列をコンソールに出力 */
    /* 注意: print_to_consoleはNULL終端文字列を期待するため、
       バッファをコピーしてNULL終端を追加 */
    char temp[256];
    int len = (cnt < 255) ? cnt : 255;
    memcpy(temp, buf, len);
    temp[len] = '\0';

    print_to_console(temp);

    return len;
}
```

### 手順5: コマンド処理コールバックの実装

ユーザーがコマンドを入力した際に呼ばれるコールバック関数を実装します。

**src/ntshell_port.c** に追加:

```c
#include <stdio.h>
#include <string.h>

/**
 * コマンド処理コールバック
 * @param text 入力されたコマンド文字列
 * @param extobj 拡張オブジェクト（未使用）
 * @return 0: 成功
 */
int ntshell_callback(const char *text, void *extobj)
{
    (void)extobj;

    /* 空コマンドは無視 */
    if (strlen(text) == 0)
    {
        return 0;
    }

    /* helpコマンド */
    if (strcmp(text, "help") == 0)
    {
        print_to_console("Available commands:\r\n");
        print_to_console("  help    - Show this help\r\n");
        print_to_console("  version - Show version\r\n");
        print_to_console("  led on  - Turn LED on\r\n");
        print_to_console("  led off - Turn LED off\r\n");
        return 0;
    }

    /* versionコマンド */
    if (strcmp(text, "version") == 0)
    {
        print_to_console("NT-Shell Demo v1.0\r\n");
        return 0;
    }

    /* led onコマンド */
    if (strcmp(text, "led on") == 0)
    {
        extern bsp_leds_t g_bsp_leds;
        R_BSP_PinAccessEnable();
        R_BSP_PinWrite((bsp_io_port_pin_t)g_bsp_leds.p_leds[0], BSP_IO_LEVEL_HIGH);
        R_BSP_PinAccessDisable();
        print_to_console("LED ON\r\n");
        return 0;
    }

    /* led offコマンド */
    if (strcmp(text, "led off") == 0)
    {
        extern bsp_leds_t g_bsp_leds;
        R_BSP_PinAccessEnable();
        R_BSP_PinWrite((bsp_io_port_pin_t)g_bsp_leds.p_leds[0], BSP_IO_LEVEL_LOW);
        R_BSP_PinAccessDisable();
        print_to_console("LED OFF\r\n");
        return 0;
    }

    /* 未知のコマンド */
    print_to_console("Unknown command: ");
    print_to_console((char *)text);
    print_to_console("\r\n");

    return 0;
}
```

### 手順6: Thread エントリ関数の実装

**src/shell_thread_entry.c** を作成:

```c
#include "shell_thread.h"
#include "ntshell.h"
#include "jlink_console.h"

/* NT-Shell用コールバック関数（外部定義） */
extern int ntshell_serial_read(char *buf, int cnt, void *extobj);
extern int ntshell_serial_write(const char *buf, int cnt, void *extobj);
extern int ntshell_callback(const char *text, void *extobj);

/* NT-Shellインスタンス */
static ntshell_t g_ntshell;

/**
 * Shell Thread エントリ関数
 */
void shell_thread_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    /* UARTの初期化（blinky_threadで初期化済みの場合は不要） */
    /* jlink_console_init(); */

    /* 起動メッセージ */
    print_to_console("\r\n");
    print_to_console("================================\r\n");
    print_to_console("  NT-Shell Demo on RA8P1\r\n");
    print_to_console("  Type 'help' for commands\r\n");
    print_to_console("================================\r\n");

    /* NT-Shellの初期化 */
    ntshell_init(
        &g_ntshell,
        ntshell_serial_read,
        ntshell_serial_write,
        ntshell_callback,
        NULL    /* 拡張オブジェクト（未使用） */
    );

    /* プロンプトの設定 */
    ntshell_set_prompt(&g_ntshell, "ra8p1> ");

    /* NT-Shell実行（この関数は戻らない） */
    ntshell_execute(&g_ntshell);
}
```

### 手順7: Threadの追加（e2 studio）

1. `configuration.xml` を開く
2. **Stacks** タブで **New Thread** をクリック
3. 以下を設定:

| 設定項目 | 設定値 |
|---------|-------|
| Symbol | `shell_thread` |
| Name | `Shell Thread` |
| Stack size | `2048` (NT-Shellは履歴等でスタックを使用) |
| Priority | `2` |

4. **Generate Project Content** をクリック

### 手順8: UARTの共有に関する注意

現在、`blinky_thread` が `g_jlink_console` (UART) を使用しています。
NT-Shell用のThreadでも同じUARTを使用する場合、以下の対応が必要です。

**方法1: blinky_threadのUART使用を停止**

`blinky_thread_entry.c` から `print_to_console()` の呼び出しを削除またはコメントアウト。

**方法2: Mutex による排他制御**

```c
/* 共有リソース用Mutex */
static SemaphoreHandle_t uart_mutex;

/* 初期化 */
uart_mutex = xSemaphoreCreateMutex();

/* 使用時 */
if (xSemaphoreTake(uart_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
{
    print_to_console("message\r\n");
    xSemaphoreGive(uart_mutex);
}
```

**方法3: 別のUARTチャネルを使用**

Shell用に別のSCIチャネルを設定し、専用のUARTインスタンスを作成。

### 動作確認

1. ビルドして書き込み
2. ターミナルソフト（TeraTerm等）を115200bpsで接続
3. プロンプト `ra8p1>` が表示されることを確認
4. `help` コマンドを実行

```
ra8p1> help
Available commands:
  help    - Show this help
  version - Show version
  led on  - Turn LED on
  led off - Turn LED off
ra8p1>
```

### ファイル構成まとめ

```
e2studio_CPU0/
├── src/
│   ├── ntshell/
│   │   ├── core/
│   │   │   ├── ntshell.c
│   │   │   ├── ntshell.h
│   │   │   └── ... (その他のコアファイル)
│   │   └── util/
│   │       ├── ntopt.c
│   │       └── ntopt.h
│   ├── ntshell_port.c      ← 新規作成: シリアル関数・コールバック
│   ├── shell_thread_entry.c ← 新規作成: Threadエントリ
│   ├── jlink_console.c      ← 既存: UART低レベル処理
│   └── jlink_console.h
├── ra_gen/
│   ├── shell_thread.c       ← 自動生成
│   └── shell_thread.h       ← 自動生成
└── configuration.xml
```

### 参考: NT-Shell API

| 関数 | 説明 |
|------|------|
| `ntshell_init()` | ハンドラの初期化 |
| `ntshell_execute()` | シェル実行（無限ループ、戻らない） |
| `ntshell_set_prompt()` | プロンプト文字列の設定 |
| `ntshell_version()` | バージョン文字列の取得 |

### 参考: キーボード操作

| キー | 動作 |
|------|------|
| Ctrl+A / Home | 行頭へ移動 |
| Ctrl+E / End | 行末へ移動 |
| Ctrl+P / ↑ | 履歴: 前のコマンド |
| Ctrl+N / ↓ | 履歴: 次のコマンド |
| Tab | 履歴からの補完候補表示 |
| Backspace | 1文字削除 |

---

## 質問2

手順4のシリアル読み書き関数の実装で、`ntshell_serial_read` 関数内で `input_from_console()` を使って1文字受信しています。

コメントにあるように1文字受信するまでブロッキングすると思います。
ブロッキングしているということは既存のLED点滅処理も点滅が止まるという理解であっていますか?

ブロッキングしない構造の移植手順を提案してください。

## 回答2

### 結論: LED点滅は止まりません

`input_from_console()` は「ブロッキング」と呼んでいますが、FreeRTOSの `vTaskDelay()` を使用してポーリングしているため、**他のタスク（blinky_thread）は正常に動作し続けます**。

### input_from_console() の実装を確認

**jlink_console.c** (253-264行目):

```c
int8_t input_from_console (void)
{
    start_key_check();

    while (key_pressed() == false)
    {
        vTaskDelay(1);    // ← ここでCPUを他タスクに明け渡す
    }

    return ((int8_t)get_detected_key());
}
```

### なぜLED点滅が止まらないのか

FreeRTOSのスケジューラーは以下のように動作します:

```
shell_thread (優先度2)          blinky_thread (優先度1)
        |                               |
        | input_from_console()          |
        |   start_key_check()           |
        |   while (!key_pressed())      |
        |     vTaskDelay(1) ─────────→  | ← CPU使用権を取得
        |                               | vTaskDelay(500)でLED点滅
        |   ← vTaskDelay完了 ──────────  |
        |   while (!key_pressed())      |
        |     vTaskDelay(1) ─────────→  | ← 再びCPU使用権を取得
        |                               |
```

`vTaskDelay()` を呼ぶと、そのタスクは「Blocked状態」になり、他のタスクにCPU使用権を明け渡します。
これにより、shell_threadが入力待ちの間も、blinky_threadは正常にLED点滅を継続できます。

### ブロッキングの種類

| 種類 | 他タスクへの影響 | 例 |
|------|----------------|-----|
| **真のブロッキング** (busy wait) | CPUを占有し続ける、他タスクが動けない | `while(1);` |
| **FreeRTOSブロッキング** | CPUを明け渡す、他タスクは動作可能 | `vTaskDelay()`, `xSemaphoreTake()` |

現在の実装は「FreeRTOSブロッキング」なので、マルチタスク環境では問題ありません。

---

## 質問2への追加提案: ノンブロッキング構造

ただし、よりエレガントな実装として、ノンブロッキング構造を提案します。
この構造では、入力がない場合に即座に0を返し、NT-Shell側で再試行させます。

### ノンブロッキング版 ntshell_serial_read の実装

**src/ntshell_port.c** (ノンブロッキング版):

```c
#include <string.h>
#include "ntshell.h"
#include "jlink_console.h"
#include "FreeRTOS.h"
#include "task.h"

/**
 * NT-Shell用シリアル読み込み関数（ノンブロッキング版）
 * @param buf 読み込みバッファ
 * @param cnt 読み込みバイト数
 * @param extobj 拡張オブジェクト（未使用）
 * @return 実際に読み込んだバイト数（0: データなし）
 */
int ntshell_serial_read(char *buf, int cnt, void *extobj)
{
    (void)extobj;
    (void)cnt;
    uint8_t temp_buf[16];
    uint32_t received;

    /* 受信データがあるかチェック（ノンブロッキング） */
    received = get_new_chars(temp_buf);

    if (received > 0)
    {
        /* 受信データをバッファにコピー */
        buf[0] = (char)temp_buf[0];
        return 1;
    }

    /* データがない場合は少し待機してから0を返す */
    vTaskDelay(1);
    return 0;
}
```

### ノンブロッキング版の動作原理

```
NT-Shell                         ntshell_serial_read
    |                                    |
    | ntshell_execute()                  |
    |   vtrecv_execute()                 |
    |     serial_read() ───────────────→ |
    |                                    | get_new_chars()
    |                                    | if (received > 0)
    |     ← return 1 (データあり) ─────── |   return 1
    |     または                          |
    |     ← return 0 (データなし) ─────── |   vTaskDelay(1)
    |                                    |   return 0
    |   (return 0の場合、再試行)          |
```

### jlink_console.c の get_new_chars() 関数

この関数は既に実装されており、割り込みで受信したデータを取得できます:

```c
uint32_t get_new_chars(uint8_t* pBuf)
{
    /* 単一文字受信をチェック */
    if (g_receive_done)
    {
        pBuf[0] = s_rx_buf;
        g_receive_done = 0;
        return 1;
    }
    /* アウトオブバンド受信をチェック */
    else if (s_g_out_of_band_index > 0)
    {
        // バッファからコピー
        ...
        return count;
    }
    return 0;
}
```

### ノンブロッキング版の利点

| 項目 | ブロッキング版 | ノンブロッキング版 |
|------|--------------|------------------|
| 実装の単純さ | シンプル | やや複雑 |
| CPU効率 | 良好（vTaskDelayで明け渡し） | 良好 |
| 応答性 | 1文字ずつ待機 | 即座に返答 |
| 他タスクへの影響 | なし | なし |
| 将来の拡張性 | 限定的 | 高い（タイムアウト等が実装しやすい） |

### ノンブロッキング版の注意点

1. **NT-Shellの内部ループ** — `ntshell_execute()` は内部で無限ループを持ち、`serial_read` が0を返しても再試行します

2. **vTaskDelay(1) の必要性** — データがない場合でも `vTaskDelay(1)` を入れることで、CPUを他タスクに明け渡します。これを省略するとCPUを占有し続けます

3. **受信開始の呼び出し** — ノンブロッキング版では、別途 `start_key_check()` を定期的に呼ぶか、常時受信モードにする必要がある場合があります

### 推奨

現在の実装（ブロッキング版）でもLED点滅は正常に動作するため、まずはそのまま試してみてください。
問題が発生した場合や、より高度な制御が必要な場合にノンブロッキング版への移行を検討してください
