## e2 studio操作手順: F-003-5a: FSPプロジェクト設定 - AI推論スタック（CMSIS-NN / Ethos-U / TFLM）の追加

### 前提条件

- e2 studio 2025-12 (25.12.0) 以降がインストールされていること
- FSP 6.3.0 がインストールされていること
- 以下のFSPパック（コンポーネント）がe2 studioにインストールされていること:
  - `Arm.CMSIS-NN` (7.0.0+fsp.6.2.0 相当)
  - `Arm.CMSIS-DSP` (1.16.2+fsp.6.2.0 相当)
  - `Arm.Ethos-U-Core-Driver` (25.2.0+fsp.6.2.0 相当)
  - `Google.TFLM-Core-Lib` (25.2.0+fsp.6.2.0 相当)
  - `Google.TFLM-CMSIS-NN-Kernel` (25.2.0+fsp.6.2.0 相当)
  - `Google.Flatbuffers` (23.5.26+fsp.6.2.0 相当)
- 現在のプロジェクト (`e2studio_CPU0/configuration.xml`) にはAI推論関連モジュールが未設定であること（確認済み）
- リファレンスプロジェクト: `reference_projects/ruhmi-framework-mcu/application_examples/face_detection/configuration.xml`

> **注意**: リファレンスプロジェクトはFSP 6.2.0、本プロジェクトはFSP 6.3.0を使用している。バージョン差異によりパックバージョン番号が異なる場合があるが、e2 studioが自動的に互換バージョンを選択する。

---

### 手順1: FSPコンポーネントパックのインストール確認

1. e2 studioのメニューから `Renesas Views` > `Renesas Software Installer` を開く
2. 以下のパックがインストール済みか確認する:
   - Arm.CMSIS-NN
   - Arm.CMSIS-DSP
   - Arm.Ethos-U-Core-Driver
   - Google.TFLM-Core-Lib
   - Google.TFLM-CMSIS-NN-Kernel
   - Google.Flatbuffers
3. 未インストールのパックがあれば、チェックを入れてインストールする

> **注意**: FSP 6.3.0環境ではパックバージョンが `+fsp.6.3.0` となっている可能性がある。利用可能な最新互換バージョンを選択すること。

---

### 手順2: ai_inference_threadの新規作成

1. e2 studioで `e2studio_CPU0/configuration.xml` を開く
2. **Stacks** タブを選択する
3. 画面左下の **New Thread** ボタンをクリックする
4. 新しいスレッドが追加されるので、プロパティパネルで以下を設定する:

#### スレッドプロパティ設定

| プロパティ名 | 設定値 | 備考 |
|-------------|--------|------|
| Symbol | `ai_inference_thread` | リファレンス準拠 |
| Name | `AI Inference Thread` | リファレンス準拠 |
| Stack size (bytes) | `0x4000` (16384) | リファレンス準拠 |
| Priority | `2` | リファレンス準拠 |
| Thread Context | `NULL` | デフォルト |
| Memory Allocation | `Static` | リファレンス準拠 |
| Allocate Secure Context | `Enable` | TrustZone対応のため必須 |

---

### 手順3: TFLM Core Lib の追加（最上位モジュール）

1. Stacksタブで、手順2で作成した **AI Inference Thread** を選択する
2. **New Stack** ボタンをクリックする
3. カテゴリから `TFLM` > `TFLM Core Lib (Google.TFLM-Core-Lib)` を選択して追加する
4. TFLM Core Lib が追加され、自動的に以下の依存モジュールのスロットが表示される:
   - **Driver** スロット（Ethos-U用）
   - **Flatbuffers** スロット
   - **Kernel** スロット（CMSIS-NN Kernel用）

> **備考**: TFLM Core Lib自体にはユーザーが変更すべきプロパティ設定はない（デフォルトのまま）。

---

### 手順4: rm_ethosu (Ethos-U Driver Wrapper) の追加

1. TFLM Core Lib の **Driver** スロットをクリックする
2. **New Stack** > `Middleware` > `Ethos-U Driver Wrapper (rm_ethosu)` を選択して追加する
3. プロパティパネルで以下を設定する:

#### rm_ethosu プロパティ設定

| プロパティ名 | 設定値 | 備考 |
|-------------|--------|------|
| Name | `g_rm_ethosu0` | リファレンス準拠 |
| Callback | `NULL` | リファレンス準拠 |
| Secure mode | `Enabled` | リファレンス準拠。TrustZone Secureモードで動作 |
| Privilege mode | `Enabled` | リファレンス準拠。特権モードで動作 |
| IRQ Priority | `Priority 12` | リファレンス準拠 |

---

### 手順5: Ethos-U Core Driver の追加

1. rm_ethosu の下に自動的に **Driver** スロットが表示される
2. そのスロットをクリックし、**New Stack** > `Ethos-U` > `Ethos-U Core Driver (Arm.Ethos-U-Core-Driver)` を選択して追加する
3. プロパティパネルで以下を確認する:

#### Ethos-U Core Driver プロパティ設定

| プロパティ名 | 設定値 | 備考 |
|-------------|--------|------|
| Name | `g_ethosu0` | リファレンス準拠（デフォルト値） |

---

### 手順6: Flatbuffers の追加

1. TFLM Core Lib の **Flatbuffers** スロットをクリックする
2. **New Stack** > `Flatbuffers` > `Flatbuffers (Google.Flatbuffers)` を選択して追加する

> **備考**: Flatbuffersにはユーザーが変更すべきプロパティ設定はない。

---

### 手順7: TFLM CMSIS-NN Kernel の追加

1. TFLM Core Lib の **Kernel** スロットをクリックする
2. **New Stack** > `TFLM` > `TFLM CMSIS-NN Kernel (Google.TFLM-CMSIS-NN-Kernel)` を選択して追加する
3. TFLM CMSIS-NN Kernel の下に **CMSIS-NN** スロットが表示される

> **備考**: TFLM CMSIS-NN Kernelにはユーザーが変更すべきプロパティ設定はない。

---

### 手順8: CMSIS-NN の追加

1. TFLM CMSIS-NN Kernel の **CMSIS-NN** スロットをクリックする
2. **New Stack** > `CMSIS` > `CMSIS-NN (Arm.CMSIS-NN)` を選択して追加する
3. CMSIS-NN の下に **DSP** スロットが表示される

> **備考**: CMSIS-NNにはユーザーが変更すべきプロパティ設定はない。

---

### 手順9: CMSIS-DSP の追加

1. CMSIS-NN の **DSP** スロットをクリックする
2. **New Stack** > `CMSIS` > `CMSIS-DSP (Arm.CMSIS-DSP)` を選択して追加する

> **備考**: CMSIS-DSPにはユーザーが変更すべきプロパティ設定はない。

---

### 手順10: モジュールスタック構成の確認

追加完了後、AI Inference Thread のスタック構成が以下のツリーになっていることを確認する:

```
ai_inference_thread (FreeRTOS Thread)
  └─ TFLM Core Lib (Google.TFLM-Core-Lib)
       ├─ rm_ethosu (Renesas.RA - Ethos-U Driver Wrapper)
       │    └─ Ethos-U Core Driver (Arm.Ethos-U-Core-Driver)
       ├─ Flatbuffers (Google.Flatbuffers)
       └─ TFLM CMSIS-NN Kernel (Google.TFLM-CMSIS-NN-Kernel)
            └─ CMSIS-NN (Arm.CMSIS-NN)
                 └─ CMSIS-DSP (Arm.CMSIS-DSP)
```

---

### 手順11: rm_ethosu モジュール共通設定の確認

1. **Stacks** タブの下部にある **Properties** パネルで、rm_ethosu のグローバル設定を確認する
2. 以下の設定がリファレンスと一致していることを確認する:

#### rm_ethosu 共通設定 (Components タブ)

| プロパティ名 | 設定値 | 備考 |
|-------------|--------|------|
| Parameter Checking | `Default (BSP)` | リファレンス準拠 |
| Log Level | `Off` | リファレンス準拠 |

---

### 手順12: NPUセキュリティ設定の確認

現在のプロジェクトのNPUセキュリティ属性 (npusa) は `secure` に設定されている。リファレンスプロジェクト (face_detection) では `nonsecure` に設定されている。

#### 判断基準

- **rm_ethosu の Secure mode が Enabled** の場合: npusa は `secure` のままで問題ない（現在の設定を維持）
- NPUをNon-secureワールドから使用する場合のみ `nonsecure` に変更する

本プロジェクトでは rm_ethosu の Secure mode を `Enabled` に設定するため、npusa は現在の `secure` のままで整合性が取れている。変更は不要と判断する。

> **注意**: npusa 設定を変更する必要がある場合は、以下の手順で行う:
> 1. `e2studio_CPU0/configuration.xml` を開く
> 2. **BSP** タブを選択する
> 3. `RA8P1 > Linker` セクションの `OFS2 Option Setting` を展開する
> 4. `NPU Security Attribution (NPUSA)` の値を変更する

---

### 最終手順: コード生成と確認

1. **Generate Project Content** ボタン（歯車アイコン）をクリックして、コード生成を実行する
2. 以下のディレクトリ/ファイルが生成されることを確認する:
   - `ra/arm/CMSIS-NN/` - CMSIS-NNライブラリソース
   - `ra/arm/CMSIS-DSP/` - CMSIS-DSPライブラリソース
   - `ra/npu/ethos-u-core-driver/` - Ethos-Uコアドライバ
   - `ra/google/tflm-core-lib/` - TFLMコアライブラリ
   - `ra/google/tflm-cmsis-nn-kernel/` - TFLM CMSIS-NNカーネル
   - `ra/google/flatbuffers/` - Flatbuffersライブラリ
   - `ra/fsp/src/rm_ethosu/` - Ethos-Uドライバラッパー
   - `ra_gen/ai_inference_thread.h` - スレッドヘッダ
3. `src/ai_inference_thread_entry.c` を手動作成する（Generate Project Contentでは生成されない場合がある。既にファイルが存在する場合はスキップ）
4. プロジェクトをビルドし、AI推論スタック関連のコンパイルエラーがないことを確認する
5. MERA生成コード (`src/ai_application/fall_detection/mera/`) のビルドエラーが解消されることを確認する

> **注意**: 生成されるディレクトリ名やパスはFSPバージョンにより若干異なる場合がある。

---

### 補足: 完成後のスレッド一覧

設定完了後、プロジェクトのスレッド構成は以下のようになる:

| スレッド | Symbol | Stack Size | Priority | 用途 |
|---------|--------|------------|----------|------|
| Blinky Thread | `blinky_thread` | 512 | 1 | LED点滅（既存） |
| NT-Shell Thread | `ntshell_thread` | 4096 | 1 | デバッグシェル（既存） |
| LVGL Thread | `lvgl_thread` | 8192 | 2 | GUI描画（既存） |
| Camera Thread | `camera_thread` | 4096 | 4 | カメラ制御（既存） |
| **AI Inference Thread** | **`ai_inference_thread`** | **0x4000** | **2** | **AI推論（新規追加）** |
