# e2 studioでThreadを追加する手順

## 質問

e2 studioでThreadを追加する手順を教えてください。
e2 studioでconfiguration.xmlを開き、StackタブでNew Threadを選択すると想像していますが、合っていますか?
その後の手順を教えて欲しいです。

## 回答

### 概要

はい、基本的な認識は合っています。ただし「New Stack」ではなく「**New Thread**」ボタンを使用します。
以下に正確な手順を示します。

### 手順1: configuration.xmlを開く

1. e2 studioのプロジェクトエクスプローラーで `configuration.xml` をダブルクリック
2. FSP Configuration エディタが開く

### 手順2: Threadの追加

1. **Stacks** タブを選択する
2. 画面左側の **Threads** パネルを確認する
3. **New Thread** ボタンをクリックする（HAL/Common Stacksの「New Stack」ではありません）

```
┌─────────────────────────────────────────────────────┐
│ Threads                                             │
│ ┌──────────────────────────────────────────────┐   │
│ │ [New Thread]  [New Object]                   │   │
│ └──────────────────────────────────────────────┘   │
│                                                     │
│   ▼ Blinky Thread (既存)                           │
│      - g_jlink_console (UART)                      │
│                                                     │
│   ▼ New Thread (新規追加)                          │
│                                                     │
└─────────────────────────────────────────────────────┘
```

### 手順3: Threadプロパティの設定

追加されたThreadを選択し、**Properties** ビューで以下を設定します。

| 設定項目 | 説明 | 例 |
|---------|------|-----|
| **Symbol** | プログラム内で使用するシンボル名 | `shell_thread` |
| **Name** | 表示名（デバッグ等で使用） | `Shell Thread` |
| **Stack size (bytes)** | スタックサイズ | `1024` |
| **Priority** | 優先度（0が最低、configMAX_PRIORITIES-1が最高） | `2` |
| **Thread Context** | タスクに渡すコンテキストポインタ | `NULL` |
| **Memory Allocation** | メモリ割り当て方式 | `Static`（推奨） |
| **Allocate Secure Context** | TrustZone使用時のみ関連 | `Disabled` |

### 手順4: Thread用のStackを追加（オプション）

新規Threadで使用するペリフェラルがある場合、Threadを選択した状態で **New Stack** をクリックします。
例えば、UARTを使用する場合：

1. 追加したThreadを選択
2. **New Stack** → **Connectivity** → **UART (r_sci_b_uart)** を選択
3. プロパティでUARTの設定を行う

### 手順5: コード生成

1. **Generate Project Content** ボタンをクリック
2. 以下のファイルが自動生成される

| ファイル | 内容 |
|---------|------|
| `ra_gen/<symbol>_thread.c` | Thread生成コード |
| `ra_gen/<symbol>_thread.h` | Threadヘッダ |

### 手順6: エントリ関数の実装

1. `src/` ディレクトリに `<symbol>_thread_entry.c` を作成
2. エントリ関数を実装

```c
#include "<symbol>_thread.h"

void <symbol>_thread_entry(void * pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    // 初期化処理

    while (1)
    {
        // タスクのメイン処理

        vTaskDelay(pdMS_TO_TICKS(100));  // 100msごとに実行
    }
}
```

### 生成されるコードの例

**ra_gen/shell_thread.c** (抜粋):

```c
static StaticTask_t shell_thread_memory;
static uint8_t shell_thread_stack[1024] BSP_PLACE_IN_SECTION(...);
TaskHandle_t shell_thread;

void shell_thread_create(void) {
    g_fsp_common_thread_count++;

    shell_thread = xTaskCreateStatic(
        shell_thread_func,
        (const char*) "Shell Thread",
        1024 / 4,                    // スタックサイズ (ワード単位)
        (void*) &shell_thread_parameters,
        2,                           // 優先度
        (StackType_t*) &shell_thread_stack,
        (StaticTask_t*) &shell_thread_memory
    );
}
```

### 参考: 既存プロジェクトの構成

e2studio_CPU0プロジェクトでは、`blinky_thread` が同様の手順で作成されています。

| ファイル | 役割 |
|---------|------|
| `ra_gen/blinky_thread.c` | Thread生成コード（自動生成） |
| `ra_gen/blinky_thread.h` | Threadヘッダ（自動生成） |
| `src/blinky_thread_entry.c` | エントリ関数実装（ユーザー作成） |

### 注意事項

1. **エントリ関数は自動生成されません** — `src/` に手動で作成する必要があります
2. **シンボル名とファイル名** — シンボルが `my_thread` の場合、エントリ関数名は `my_thread_entry` になります
3. **スタックサイズ** — 使用するライブラリや処理内容に応じて十分なサイズを確保してください
4. **優先度** — 現在のプロジェクトでは `configMAX_PRIORITIES = 5` なので、0〜4の範囲で設定できます
