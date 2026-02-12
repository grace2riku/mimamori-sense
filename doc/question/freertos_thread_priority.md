# FreeRTOS Threadの優先度について

## 質問

e2 studioでconfiguration.xmlを開き、StackタブでThreadを追加すると、FreeRTOSのThreadが作成できると認識しています。
FreeRTOSの知識がないのですが、Threadの優先度という概念は存在するのですか?
存在する場合、数字が低い方が優先順位が高い仕様ですか?

## 回答

### 概要

はい、FreeRTOSにはTaskの優先度という概念が存在します。
**数字が大きいほど優先度が高い** 仕様です（Linux等のNICE値とは逆です）。

### 優先度の仕様

| 優先度値 | 意味 |
|---------|------|
| 0 | 最低優先度（アイドルタスクと同じ） |
| configMAX_PRIORITIES - 1 | 最高優先度 |

現在のプロジェクト（e2studio_CPU0）では `configMAX_PRIORITIES = 5` に設定されているため、使用可能な優先度は **0〜4** です。

### 優先度設定の場所

**FreeRTOSConfig.h** (e2studio_CPU0/ra_cfg/aws/FreeRTOSConfig.h:51行目):

```c
#define configMAX_PRIORITIES (5)
```

### プロジェクト内のTask優先度

| Task | 優先度 | 役割 |
|------|-------|------|
| Idle Task | 0 | FreeRTOS内部タスク（CPU空き時間に実行） |
| Blinky Thread | 1 | LED点滅処理 |
| Timer Task | 3 | ソフトウェアタイマー管理（FreeRTOS内部） |

**blinky_thread** の優先度設定（ra_gen/blinky_thread.c:35行目）:

```c
shell_thread = xTaskCreateStatic(
    blinky_thread_func,
    (const char*) "Blinky Thread",
    512 / 4,
    (void*) &blinky_thread_parameters,
    1,                               // ← 優先度: 1
    (StackType_t*) &blinky_thread_stack,
    (StaticTask_t*) &blinky_thread_memory
);
```

### 優先度とスケジューリング

FreeRTOSのスケジューラーは以下のルールで動作します:

1. **プリエンプティブ方式** — 高優先度タスクが Ready 状態になると、低優先度タスクを中断して即座に切り替え
2. **同一優先度** — ラウンドロビン方式（順番に実行）またはタイムスライシングで切り替え
3. **Idle Task** — 実行可能なタスクがない場合に実行される

```
優先度 4: ─────────┐
優先度 3: ────┐    │ 高優先度タスクが Ready になると
優先度 2: ───┐│    │ 低優先度タスクは中断される
優先度 1: ──┐││    │
優先度 0: ─┐│││    │
           ↓↓↓↓    ↓
           Idle    最優先で実行
```

### 優先度設定のガイドライン

| 優先度 | 推奨用途 |
|-------|---------|
| 4 (最高) | 緊急処理（異常検知、安全停止など） |
| 3 | 高頻度・リアルタイム性が必要な処理 |
| 2 | 通常の処理（通信、センサー読み取りなど） |
| 1 | バックグラウンド処理（ログ、LED点滅など） |
| 0 (最低) | 優先度を持たない処理（使用非推奨） |

### 現在の設定値の確認

e2 studioで確認する場合:

1. `configuration.xml` を開く
2. **Stacks** タブを選択
3. 対象のThreadを選択
4. **Properties** ビューで **Priority** を確認

### 優先度を変更する方法

**方法1: e2 studio GUI（推奨）**

1. `configuration.xml` を開く
2. 対象Threadを選択
3. Properties の **Priority** を変更
4. **Generate Project Content** を実行

**方法2: 実行時に動的変更**

```c
#include "FreeRTOS.h"
#include "task.h"

// 優先度を取得
UBaseType_t current_priority = uxTaskPriorityGet(shell_thread);

// 優先度を変更
vTaskPrioritySet(shell_thread, 3);  // 優先度を3に変更
```

### 注意事項

1. **優先度逆転問題** — 低優先度タスクがリソースを占有し、高優先度タスクが待たされる状況に注意
2. **Idle Task** — 優先度0はIdle Taskと同じため、他のタスクが常に実行可能だと永久に実行されない可能性あり
3. **優先度の範囲** — `configMAX_PRIORITIES` を超える値を設定するとアサートエラーが発生

### 参考: 他のRTOSとの比較

| RTOS | 優先度の向き |
|------|-------------|
| FreeRTOS | 数字が大きいほど高優先度 |
| Linux (NICE) | 数字が小さいほど高優先度 |
| Azure RTOS (ThreadX) | 数字が小さいほど高優先度 |
| Zephyr | 数字が小さいほど高優先度 |

FreeRTOSは他の多くのRTOSと優先度の向きが **逆** であることに注意してください。
