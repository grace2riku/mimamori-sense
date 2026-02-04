# Renesas RA8P1 マルチコアアーキテクチャ解説レポート

## 1. 概要

Renesas RA8P1は、**Arm Cortex-M85**（1GHz）と**Arm Cortex-M33**（250MHz）を搭載したヘテロジニアス（異種混合）デュアルコアMCUです。22nm ULL（Ultra-Low Leakage）プロセスで製造され、Arm Ethos-U55 NPUを内蔵することでAI処理にも対応しています。

### 主要スペック
| 項目 | 仕様 |
|------|------|
| CPU0 (メインコア) | Arm Cortex-M85 @ 1GHz |
| CPU1 (サブコア) | Arm Cortex-M33 @ 250MHz |
| MRAM | 0.5MB / 1MB |
| 外部Flash | 4MB / 8MB (SIP) |
| SRAM | 2MB (ECC保護) |
| I/Dキャッシュ | 各コアに32KB |
| NPU | Arm Ethos-U55 (256 GOPS @ 500MHz) |
| 性能 | 7300 CoreMark以上 |

---

## 2. Cortex-M85コアとCortex-M33コアの比較

### 2.1 両方のコアでできること

| 機能 | 説明 |
|------|------|
| GPIO制御 | 両コアとも`R_BSP_PinWrite()`等でGPIOを制御可能 |
| ペリフェラルアクセス | 基本的に全ペリフェラルにアクセス可能（セキュリティ設定による制限あり） |
| 割り込み処理 | 各コアが独自のNVIC（Nested Vectored Interrupt Controller）を持つ |
| FreeRTOS実行 | 両コアでFreeRTOSが動作可能 |
| TrustZone | 両コアともArm TrustZone対応 |
| メモリアクセス | 共有メモリ（SRAM、MRAM）へのアクセス |

### 2.2 Cortex-M85のみの機能（M33ではできないこと）

| 機能 | 説明 |
|------|------|
| **Helium MVE (M-Profile Vector Extension)** | SIMD処理によるDSP演算の高速化。M33には搭載されていない |
| **ITCM (Instruction Tightly Coupled Memory)** | 64KB。低レイテンシの命令メモリ |
| **DTCM (Data Tightly Coupled Memory)** | 128KB (16ブロック x 8KB、ECC付き)。低レイテンシのデータメモリ |
| **I-Cache / D-Cache (32KB各)** | 高性能キャッシュ。M33は別のキャッシュ構成 |
| **LOB (Low Overhead Branch)** | ループ処理の最適化拡張 |
| **倍精度浮動小数点演算** | IEEE 754-2008準拠。M33は単精度のみ |
| **ブートコア機能** | システム全体の起動とM33コアの起動制御を担当 |
| **NPU (Ethos-U55) 制御** | AI/ML処理のNPU制御はM85が主に担当 |

### 2.3 Cortex-M33のみの機能（M85ではできないこと）

| 機能 | 説明 |
|------|------|
| **CTCM (Code Tightly Coupled Memory)** | 64KB (ECC付き)。M33専用の命令TCM |
| **STCM (System Tightly Coupled Memory)** | 64KB (ECC付き)。M33専用のデータTCM |
| **低消費電力処理** | 250MHzで動作するため、低電力タスクに適している |

### 2.4 禁止事項・制約

| 禁止事項 | 説明 |
|----------|------|
| **クロック/システム初期化の重複** | システムクロックの初期化はCPU0（M85）のみが行う。CPU1は`BSP_CFG_SKIP_INIT`設定によりスキップ |
| **TrustZoneセキュリティ設定の競合** | SAR（Security Attribution Register）の設定はプライマリコアが行い、セカンダリコアはビット単位でAND演算で修正のみ可能 |
| **NPUセキュリティ** | NPUはデフォルトでSecure属性（`ofs2.npusa = secure`） |

---

## 3. ペリフェラル制御の制約

### 3.1 基本アーキテクチャ

RA8P1のバスアーキテクチャは以下のように構成されています：

```
┌─────────────┐     ┌─────────────┐
│  Cortex-M85 │     │  Cortex-M33 │
│   (CPU0)    │     │   (CPU1)    │
└──────┬──────┘     └──────┬──────┘
       │                    │
       ▼                    ▼
   ┌───────┐            ┌───────┐
   │ M-AXI │            │ C-AHB │
   │ P-AHB │            │ S-AHB │
   └───┬───┘            └───┬───┘
       │                    │
       └────────┬───────────┘
                ▼
         ┌──────────────┐
         │  Bus Matrix  │
         │  (AXI/AHB)   │
         └──────┬───────┘
                │
       ┌────────┼────────┐
       ▼        ▼        ▼
    ┌─────┐  ┌─────┐  ┌─────┐
    │SRAM │  │MRAM │  │ APB │
    │     │  │     │  │(ペリ)│
    └─────┘  └─────┘  └─────┘
```

### 3.2 ペリフェラルアクセスの制約

**基本原則：両コアとも全ペリフェラルにアクセス可能**

ただし、以下の条件により制約が発生します：

| 制約種別 | 説明 |
|----------|------|
| **セキュリティ属性（SAR）** | PSARB/PSARC/PSARD/PSAREレジスタでペリフェラルのSecure/Non-secure属性を設定。Secure属性のペリフェラルはNon-secureコードからアクセス不可 |
| **バスマスタMPU** | MMPUSARAレジスタでDMACのセキュリティ属性を設定 |
| **IPC専用レジスタ** | IPCSAREレジスタでコア間通信レジスタのセキュリティを設定（デフォルト：Secure only） |

### 3.3 GPIO制御の競合と対策

**GPIO制御の重複について：**

```c
// 両コアが同じGPIOを制御する場合のサンプルコード
// e2studio_CPU0/src/blinky_thread_entry.c より

// PFSレジスタへのアクセスを有効化
R_BSP_PinAccessEnable();

// マルチコアの場合、各コアが異なるLEDを制御
#if BSP_NUMBER_OF_CORES == 1
    // シングルコア：全LEDを制御
    for (uint32_t i = 0; i < leds.led_count; i++) {
        R_BSP_PinWrite((bsp_io_port_pin_t) leds.p_leds[i], pin_level);
    }
#else
    // マルチコア：自コアに対応するLEDのみ制御
    R_BSP_PinWrite((bsp_io_port_pin_t) leds.p_leds[_RA_CORE], pin_level);
#endif

// PFSレジスタへのアクセスを無効化
R_BSP_PinAccessDisable();
```

**重要なポイント：**

| 項目 | 説明 |
|------|------|
| **ハードウェア排他制御** | GPIO自体にはハードウェアレベルの排他制御機構はない |
| **ソフトウェア設計責任** | プログラマーが制御の重複を避けるように設計する必要がある |
| **推奨パターン** | 各コアが制御するGPIOを事前に分離する（上記コード例のように`_RA_CORE`で分岐） |
| **IPCセマフォ** | 共有リソースへのアクセスが必要な場合はIPCセマフォで排他制御 |

### 3.4 ペリフェラルセキュリティ設定

`configuration.xml`より、以下のセキュリティ設定がデフォルトで適用されています：

```xml
<!-- TrustZoneセキュリティ設定（抜粋） -->
<property id="config.bsp.fsp.tz.sramsar.sramsa0" value="both"/>  <!-- 両コアアクセス可 -->
<property id="config.bsp.fsp.tz.bussara" value="both"/>          <!-- バス制御：両コア -->
<property id="config.bsp.fsp.tz.saipcsem0" value="secure_only"/> <!-- IPCセマフォ：Secureのみ -->
<property id="config.bsp.fsp.tz.saipcnmi0" value="secure_only"/> <!-- IPC NMI：Secureのみ -->
```

---

## 4. メモリ空間の制約

### 4.1 メモリマップ概要

```
┌────────────────────────────────────────────────────────────┐
│                    アドレス空間                              │
├────────────────────────────────────────────────────────────┤
│ 0x0000_0000 - 0x0001_FFFF : ITCM (128KB) [M85専用]        │
│ 0x0200_0000 - 0x020F_FFFF : MRAM (1MB)   [共有/分割可能]  │
│ 0x1000_0000 - 0x1xxx_xxxx : Code Flash NS エイリアス       │
│ 0x2000_0000 - 0x2001_FFFF : DTCM (128KB) [M85専用]        │
│ 0x2200_0000 - 0x221F_FFFF : SRAM (2MB)   [共有/分割可能]  │
│ 0x3000_0000 - 0x3xxx_xxxx : SRAM NS エイリアス            │
│ 0x4000_0000 - 0x400x_xxxx : ペリフェラルレジスタ          │
│ 0x8000_0000 - 0x8xxx_xxxx : 外部メモリ空間 (OSPI等)       │
├────────────────────────────────────────────────────────────┤
│ [M33専用メモリ]                                            │
│ CTCM : 64KB (命令TCM)                                      │
│ STCM : 64KB (データTCM)                                    │
└────────────────────────────────────────────────────────────┘
```

### 4.2 メモリ共有と分割

**メモリ空間は両コアで共有されている（同一アドレスを参照可能）**

| メモリ種別 | 共有方式 | 説明 |
|------------|----------|------|
| **MRAM (コードFlash)** | デフォルトで均等分割 | CPU0とCPU1用に領域を分割して使用。パーティション設定で変更可能 |
| **SRAM** | デフォルトで均等分割 | データ領域も分割。共有領域を設定することも可能 |
| **TCM** | コア専用 | ITCM/DTCMはM85専用、CTCM/STCMはM33専用 |
| **ペリフェラル** | 共有 | 同一アドレスでアクセス |

### 4.3 メモリパーティション

BSPはメモリパーティションを以下のマクロで定義しています：

```c
// bsp_security.c より
// CPU0用パーティション
#define FLASH_NSC_START     ((uint32_t *) BSP_PARTITION_FLASH_CPU0_C_START)
#define FLASH_NS_START      ((uint32_t *) BSP_PARTITION_FLASH_CPU0_N_START)
#define RAM_NSC_START       ((uint32_t *) BSP_PARTITION_RAM_CPU0_C_START)
#define RAM_NS_START        ((uint32_t *) BSP_PARTITION_RAM_CPU0_N_START)

// CPU1用パーティション
#define FLASH_NSC_START     ((uint32_t *) BSP_PARTITION_FLASH_CPU1_C_START)
#define FLASH_NS_START      ((uint32_t *) BSP_PARTITION_FLASH_CPU1_N_START)
```

### 4.4 セキュリティ属性とアドレスエイリアス

TrustZone対応により、メモリ領域は**Secure**と**Non-secure**のエイリアスを持ちます：

| ベースアドレス | エイリアスオフセット | 説明 |
|----------------|---------------------|------|
| 0x0200_0000 | +0x1000_0000 | MRAM Secure → Non-secure |
| 0x2200_0000 | +0x1000_0000 | SRAM Secure → Non-secure |

---

## 5. ブートシーケンス

### 5.1 ブートシーケンス概要

```
┌──────────────────────────────────────────────────────────────┐
│                     電源投入 / リセット                       │
└───────────────────────────┬──────────────────────────────────┘
                            ▼
┌──────────────────────────────────────────────────────────────┐
│   CPU0 (Cortex-M85) ブート開始                               │
│   - Reset_Handler() 実行                                     │
│   - SystemInit() でクロック初期化、メモリ初期化              │
└───────────────────────────┬──────────────────────────────────┘
                            ▼
┌──────────────────────────────────────────────────────────────┐
│   CPU0: main() → アプリケーション開始                        │
│   - FreeRTOS スケジューラ開始                                │
│   - blinky_thread_entry() 実行                               │
└───────────────────────────┬──────────────────────────────────┘
                            ▼
┌──────────────────────────────────────────────────────────────┐
│   CPU0: R_BSP_SecondaryCoreStart() 呼び出し                  │
│   - CPU1のベクタテーブルアドレスを設定                       │
│   - CPU1ACTCSR に起動要求を書き込み                          │
└───────────────────────────┬──────────────────────────────────┘
                            ▼
┌──────────────────────────────────────────────────────────────┐
│   CPU1 (Cortex-M33) ブート開始                               │
│   - Reset_Handler() 実行                                     │
│   - SystemInit() (BSP_CFG_SKIP_INIT=1でクロック初期化スキップ)│
│   - main() → アプリケーション開始                            │
└──────────────────────────────────────────────────────────────┘
```

### 5.2 セカンダリコア起動コード

```c
// bsp_common.h より
__STATIC_INLINE void R_BSP_SecondaryCoreStart (void)
{
    // CPU1のベクタテーブルアドレスを設定
    R_CPU_CTRL->CPU1INITVTOR = (uint32_t) BSP_PARTITION_FLASH_CPU1_S_START;

    // デバッグ時にCPU1が既に起動している場合の対応
    // CPU1WAITCRをクリアして起動許可
    R_CPU_CTRL->CPU1WAITCR = 0;

    // CPU1起動要求（キーコード + 起動要求ビット）
    R_CPU_CTRL->CPU1ACTCSR = (BSP_CPU1ACTCSR_KEY_CODE << R_CPU_CTRL_CPU1ACTCSR_KEY_Pos) |
                             R_CPU_CTRL_CPU1ACTCSR_ACTREQ_Msk;
}
```

### 5.3 アプリケーションからの起動タイミング

```c
// blinky_thread_entry.c より
void blinky_thread_entry (void * pvParameters)
{
    // マルチコアプロジェクトでCPU0の場合のみCPU1を起動
    #if (0 == _RA_CORE) && (1 == BSP_MULTICORE_PROJECT) && !BSP_TZ_NONSECURE_BUILD
        R_BSP_SecondaryCoreStart();
    #endif

    // 以降、LED制御処理...
}
```

---

## 6. 割り込み

### 6.1 割り込みベクタの独立性

**各コアは独立した割り込みベクタテーブルを持つ**

```c
// startup.c より - 各コア用ベクタテーブル
BSP_DONT_REMOVE const exc_ptr_t __VECTOR_TABLE[BSP_CORTEX_VECTOR_TABLE_ENTRIES]
    BSP_PLACE_IN_SECTION(BSP_SECTION_FIXED_VECTORS) =
{
    (exc_ptr_t) (&g_main_stack[0] + BSP_CFG_STACK_MAIN_BYTES), // Initial SP
    Reset_Handler,        // Reset Handler
    NMI_Handler,          // NMI Handler
    HardFault_Handler,    // Hard Fault Handler
    MemManage_Handler,    // MPU Fault Handler
    BusFault_Handler,     // Bus Fault Handler
    UsageFault_Handler,   // Usage Fault Handler
    SecureFault_Handler,  // Secure Fault Handler
    // ... 以下略
};
```

### 6.2 割り込み構成

| 項目 | CPU0 (M85) | CPU1 (M33) |
|------|------------|------------|
| NVICベースアドレス | 独立 | 独立 |
| 割り込み数 | 96 (IRQ) | 96 (IRQ) |
| 例外ハンドラ | 独自定義 | 独自定義 |
| VTOR | 独自設定 | CPU0が初期設定 |

### 6.3 NMI（Non-Maskable Interrupt）によるコア間通知

IPCモジュールはNMIを使用してコア間で割り込み通知を行います：

```c
// bsp_ipc.c より
// IPC0NMI: Core1 → Core0
// IPC1NMI: Core0 → Core1

#if (BSP_CFG_CPU_CORE == 0)
    #define BSP_IPC_PRV_NMI_REG_SET    IPC1NMI  // Core0がCore1にNMI送信
    #define BSP_IPC_PRV_NMI_REG_CLEAR  IPC0NMI  // Core0がCore1からのNMI受信
#else
    #define BSP_IPC_PRV_NMI_REG_SET    IPC0NMI  // Core1がCore0にNMI送信
    #define BSP_IPC_PRV_NMI_REG_CLEAR  IPC1NMI  // Core1がCore0からのNMI受信
#endif
```

---

## 7. コア間通信（IPC: Inter-Processor Communication）

### 7.1 通信手法

RA8P1は以下のIPC機構を提供しています：

| 機構 | 説明 |
|------|------|
| **IPCセマフォ** | ハードウェアセマフォ（16個）による排他制御 |
| **IPC NMI** | 相手コアへのNMI割り込み送信 |
| **共有メモリ** | SRAM上の共有領域を使用したデータ交換 |

### 7.2 IPCセマフォ（排他制御）

```c
// bsp_ipc.c より

// セマフォ取得（排他ロック）
fsp_err_t R_BSP_IpcSemaphoreTake (bsp_ipc_semaphore_handle_t const * const p_semaphore_handle)
{
    // セマフォが既に取得されていればエラー
    FSP_ERROR_RETURN(!R_IPC->IPCSEM[p_semaphore_handle->semaphore_num], FSP_ERR_IN_USE);
    return FSP_SUCCESS;
}

// セマフォ解放（排他ロック解除）
fsp_err_t R_BSP_IpcSemaphoreGive (bsp_ipc_semaphore_handle_t const * const p_semaphore_handle)
{
    R_IPC->IPCSEM[p_semaphore_handle->semaphore_num] = BSP_IPC_PRV_SEMAPHORE_CLEAR;
    return FSP_SUCCESS;
}
```

### 7.3 IPC NMI（コア間割り込み）

```c
// NMI有効化（コールバック登録）
fsp_err_t R_BSP_IpcNmiEnable (bsp_ipc_nmi_cb_t p_callback)
{
    gp_nmi_callback = p_callback;

    // NMIコールバック設定
    R_BSP_GroupIrqWrite(BSP_GRP_IRQ_IPC, ipc_nmi_internal_callback);

    // NMI有効化
    R_ICU->NMIER = R_ICU_NMIER_IPCEN_Msk;

    return FSP_SUCCESS;
}

// 相手コアへのNMI送信
fsp_err_t R_BSP_IpcNmiRequestSet (void)
{
    R_IPC->BSP_IPC_PRV_NMI_REG_SET.SET = 1;
    return FSP_SUCCESS;
}
```

### 7.4 同期パターン例

```
┌─────────────────┐                    ┌─────────────────┐
│     CPU0        │                    │     CPU1        │
│   (Cortex-M85)  │                    │   (Cortex-M33)  │
└────────┬────────┘                    └────────┬────────┘
         │                                      │
         │  1. R_BSP_IpcSemaphoreTake()         │
         │  (セマフォ取得)                      │
         ├──────────────────────────────────────┤
         │                                      │
         │  2. 共有メモリにデータ書き込み       │
         │                                      │
         ├──────────────────────────────────────┤
         │                                      │
         │  3. R_BSP_IpcNmiRequestSet()         │
         │  (NMI送信) ─────────────────────────►│
         │                                      │
         │                              4. NMIハンドラ実行
         │                              5. 共有メモリ読み取り
         │                                      │
         │  6. R_BSP_IpcSemaphoreGive()         │
         │  (セマフォ解放)                      │
         ▼                                      ▼
```

---

## 8. プロジェクト構成解析

### 8.1 ディレクトリ構成

```
mimamori-sense/
├── e2studio/                    # ソリューションプロジェクト（マルチコア統合）
│   ├── .project
│   └── solution.xml
│
├── e2studio_CPU0/               # CPU0プロジェクト (Cortex-M85)
│   ├── configuration.xml        # FSP設定 (Core: CPU0, cortex-m85)
│   ├── src/
│   │   ├── blinky_thread_entry.c
│   │   └── hal_warmstart.c
│   ├── ra_gen/                  # FSP生成コード
│   │   ├── vector_data.c
│   │   └── pin_data.c
│   └── ra/fsp/                  # FSPライブラリ
│
└── e2studio_CPU1/               # CPU1プロジェクト (Cortex-M33)
    ├── configuration.xml        # FSP設定 (Core: CPU1, cortex-m33)
    ├── src/
    │   ├── blinky_thread_entry.c
    │   └── hal_warmstart.c
    ├── ra_gen/                  # FSP生成コード
    └── ra/fsp/                  # FSPライブラリ
```

### 8.2 設定の違い

| 項目 | e2studio_CPU0 | e2studio_CPU1 |
|------|---------------|---------------|
| Core設定 | `CPU0` | `CPU1` |
| アーキテクチャ | `cortex-m85` | `cortex-m33` |
| コア数 | `2` | `2` |
| RTOS | FreeRTOS | FreeRTOS |
| ツールチェーン | LLVM/Clang | LLVM/Clang |

### 8.3 ブートシーケンス（プロジェクト視点）

1. **e2studio_CPU0が先にブート**
   - システムクロック初期化
   - ペリフェラル初期化
   - FreeRTOSスケジューラ開始
   - `blinky_thread_entry()`でCPU1を起動

2. **e2studio_CPU1がブート**
   - クロック初期化はスキップ（CPU0が実施済み）
   - FreeRTOSスケジューラ開始
   - 独自のLED制御開始

### 8.4 GPIO使用方法

```c
// 両プロジェクトで同じコードだが、_RA_COREの値が異なる
// CPU0: _RA_CORE = 0 → leds.p_leds[0]を制御
// CPU1: _RA_CORE = 1 → leds.p_leds[1]を制御

#if BSP_NUMBER_OF_CORES == 1
    // シングルコア：全LED制御
    for (uint32_t i = 0; i < leds.led_count; i++) {
        R_BSP_PinWrite((bsp_io_port_pin_t) leds.p_leds[i], pin_level);
    }
#else
    // マルチコア：自分のLEDのみ制御
    R_BSP_PinWrite((bsp_io_port_pin_t) leds.p_leds[_RA_CORE], pin_level);
#endif
```

### 8.5 メモリ使用

| 領域 | CPU0 (M85) | CPU1 (M33) |
|------|------------|------------|
| コード (MRAM) | パーティション分割 | パーティション分割 |
| データ (SRAM) | パーティション分割 | パーティション分割 |
| スタック | 独立確保 | 独立確保 |
| TCM | ITCM/DTCM使用 | CTCM/STCM使用 |

### 8.6 コア間通信の実装状況

**現在のプロジェクトでは明示的なコア間通信は実装されていません。**

- 各コアは独立してLEDを制御
- 共有リソースへの同時アクセスはなし
- IPCセマフォ、NMIは未使用

**拡張する場合の推奨パターン：**

```c
// 例：共有バッファを使用したコア間データ交換
// 1. 共有メモリ領域を定義（リンカスクリプトで指定）
// 2. IPCセマフォで排他制御
// 3. NMIで通知
```

---

## 9. まとめ

### 9.1 RA8P1デュアルコアの特徴

| 特徴 | 説明 |
|------|------|
| **ヘテロジニアス構成** | 高性能M85と低消費電力M33の組み合わせ |
| **メモリ共有** | 同一アドレス空間を共有、パーティションで分割管理 |
| **TrustZone対応** | 両コアでセキュリティ機能を利用可能 |
| **柔軟なIPC** | セマフォとNMIによるコア間通信 |

### 9.2 設計上の注意点

1. **ブートシーケンス**: CPU0が必ず先にブートし、CPU1を起動する
2. **クロック初期化**: CPU0のみが実施
3. **ペリフェラル競合**: ソフトウェアで管理（ハードウェア排他なし）
4. **メモリパーティション**: e2 studioのFSPコンフィギュレータで設定

### 9.3 参考資料

- [RA8P1 - 1GHz Arm Cortex-M85 and Ethos-U55 NPU Based AI Microcontroller | Renesas](https://www.renesas.com/en/products/ra8p1)
- [Getting Started with RA8P1 Memory Architecture, Configurations and Topologies](https://www.renesas.com/en/document/apn/getting-started-ra8p1-memory-architecture-configurations-and-topologies)
- [The Most Powerful M85 Core Chip on the Surface - RA8P1 Review | RT-Thread IoT OS](https://rt-thread.medium.com/the-most-powerful-m85-core-chip-on-the-surface-ra8p1-review-technology-showcase-6d77da3962fa)
- [RA8P1 Evaluation Kit - Zephyr Project Documentation](https://docs.zephyrproject.org/latest/boards/renesas/ek_ra8p1/doc/index.html)

---

*本レポートは、RA8P1のFSP (Flexible Software Package) v6.3.0およびe2studioプロジェクトのソースコード解析に基づいて作成されました。*
