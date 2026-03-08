# F-003-4: MERA変換手順書・入出力仕様

Issue: #21
作成日: 2026-03-08
ステータス: 変換待ち（ユーザー実行）

---

## 1. 変換対象モデル

| 項目 | 値 |
|------|-----|
| ファイル | `dataset/models/yolov8_pico_fall_int8.tflite` |
| サイズ | 366 KB (374,456 bytes) |
| 入力 | 192x192x3 (RGB), NHWC, INT8 |
| 出力 | 1x5x756, INT8 |
| パラメータ数 | 0.263M |
| 量子化 | Post-training INT8 |

## 2. 環境構築 (Windows)

公式手順: https://github.com/renesas/ruhmi-framework-mcu/blob/main/install/README.md#installation---windows

### 2.1 前提ソフトウェア

- **Python 3.10**: https://www.python.org/downloads/release/python-3105/
- **Microsoft Visual C++ 2015-2022 Runtime**: https://aka.ms/vs/17/release/vc_redist.x64.exe

### 2.2 RUHMI Framework の取得

任意の作業ディレクトリに公式リポジトリをクローンする:

```powershell
cd C:\work
git clone https://github.com/renesas/ruhmi-framework-mcu.git
cd ruhmi-framework-mcu
```

### 2.3 仮想環境の構築 (PowerShell)

PowerShellを開き、クローンしたディレクトリで以下を実行:

```powershell
cd C:\work\ruhmi-framework-mcu

# Python 3.10 で仮想環境を作成
py -3.10 -m venv .venv

# 実行ポリシーの設定とアクティベート
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope Process
.venv\Scripts\Activate.ps1
```

プロンプトが `(.venv) PS C:\work\ruhmi-framework-mcu>` に変わることを確認。

### 2.4 MERA インストール (PowerShell)

```powershell
# MERA 本体のインストール
python -m pip install .\install\mera-2.5.0+pkg.3577-cp310-cp310-win_amd64.whl

# 依存パッケージのインストール
python -m pip install onnx==1.17.0 tflite==2.18.0
```

### 2.5 動作確認

```powershell
python -c "import mera; print(mera.__version__)"
# 期待出力: 2.5.0+pkg.3577

vela --version
# 期待出力: 4.2.0

python -c "import mera; print(dir(mera))"
```

## 3. 変換実行

仮想環境をアクティベートした状態で、クローンしたRUHMI Frameworkの `scripts/` ディレクトリで実行する。

### 3.1 変換手順

```powershell
# 仮想環境をアクティベート
cd C:\work\ruhmi-framework-mcu
.venv\Scripts\Activate.ps1

# 転倒検出モデルだけを一時ディレクトリに配置
# (mcu_deploy.py はディレクトリ内の全 .tflite を変換するため)
New-Item -ItemType Directory -Path C:\work\fall_model_input -Force
Copy-Item C:\Users\grace\github\mimamori-sense\dataset\models\yolov8_pico_fall_int8.tflite C:\work\fall_model_input\

# RUHMI変換 (Ethos-U55向け)
cd scripts
python mcu_deploy.py --ethos --ref_data C:\work\fall_model_input C:\work\deploy_fall_detection_output
```

### 3.2 生成コードの確認

```powershell
# 生成されたCソースコードの一覧
Get-ChildItem C:\work\deploy_fall_detection_output\ -Recurse -Include "*.c","*.h" | Select-Object Name, Length
```

期待される出力パス: `C:\work\deploy_fall_detection_output\yolov8_pico_fall_int8_no_ospi\build\MCU\compilation\src\`

### 3.3 e2studio プロジェクトへの配置

生成された `src/` 内から、MCU組み込みに必要なファイル（*.c, *.h）を以下にコピー:

```powershell
$src = "C:\work\deploy_fall_detection_output\yolov8_pico_fall_int8_no_ospi\build\MCU\compilation\src"
$dest = "C:\Users\grace\github\mimamori-sense\e2studio_CPU0\src\ai_application\fall_detection\mera"

# x86テスト用ファイルを除外してコピー
Get-ChildItem $src -Include "*.c","*.h" -File | Where-Object {
    $_.Name -notin @("CMakeLists.txt","compare.cpp","hal_entry.c","python_bindings.cpp")
} | Copy-Item -Destination $dest -Force
```

### 3.4 注意事項

- `mcu_deploy.py` は `models_dir` 内の全 `.tflite` を再帰変換するため、`dataset/models/` を直接渡すと3モデル全てが変換される。上記手順ではpicoモデルのみを一時ディレクトリに配置して回避している。
- モデルサイズ 366KB < 1.5MB なので OSPI は自動無効 (`_no_ospi` サフィックス)
- `mcu_config['suffix']` が `'_net1'` にハードコードされている。生成される関数名にサフィックスが付く場合は、`wrapper.h` の関数名を合わせて調整する。

## 4. 生成ファイル一覧と配置先

### 4.1 配置先ディレクトリ

```
e2studio_CPU0/src/ai_application/fall_detection/mera/
```

### 4.2 期待されるファイル

| ファイル | 役割 | MCU組み込みに必要 |
|---------|------|-----------------|
| `ethosu_common.h` | TensorInfo構造体定義 | Yes |
| `model.c` | RunModel(), GetModelInputPtr/OutputPtr | Yes |
| `model.h` | 上記のプロトタイプ宣言 | Yes |
| `model_io_data.c` | テスト用リファレンスデータ | Optional |
| `model_io_data.h` | 入出力バッファサイズ定数 | Yes |
| `sub_0000_command_stream.c` | NPUコマンドストリーム | Yes |
| `sub_0000_command_stream.h` | 上記のextern宣言 | Yes |
| `sub_0000_invoke.c` | NPU推論実行関数 | Yes |
| `sub_0000_invoke.h` | 上記のプロトタイプ | Yes |
| `sub_0000_io_data.c` | サブグラフ用リファレンスデータ | Optional |
| `sub_0000_io_data.h` | サブグラフ入出力サイズ | Yes |
| `sub_0000_model_data.c` | モデル重みデータ | Yes |
| `sub_0000_model_data.h` | 上記のextern宣言 | Yes |
| `sub_0000_tensors.c` | テンソルアドレス、Arenaサイズ | Yes |
| `sub_0000_tensors.h` | kArenaSize定義 | Yes |
| `wrapper.h` | MERA関数のインラインラッパー | Yes |

**除外するファイル**: `CMakeLists.txt`, `compare.cpp`, `hal_entry.c`, `python_bindings.cpp` (x86テスト用)

## 5. 入出力仕様（変換結果）

### 5.1 入力テンソル

| 項目 | 値 |
|------|-----|
| テンソル名 | `serving_default_images_0` |
| サイズ (bytes) | 110,592 (192x192x3) |
| データ型 | INT8 |
| 入力関数 | `GetModelInputPtr_net1_serving_default_images_0()` |

### 5.2 出力テンソル

| 項目 | 値 |
|------|-----|
| テンソル名 | `PartitionedCall_0_70452` |
| サイズ (bytes) | 3,780 (5x756) |
| データ型 | INT8 |
| 出力関数 | `GetModelOutputPtr_net1_PartitionedCall_0_70452()` |

### 5.3 推論実行API

| 関数 | 説明 |
|------|------|
| `RunModel_net1(bool clean_outputs)` | 推論実行（3ステージ: NPU→CPU→NPU） |
| `GetModelInputPtr_net1_serving_default_images_0()` | 入力バッファポインタ取得 |
| `GetModelOutputPtr_net1_PartitionedCall_0_70452()` | 出力バッファポインタ取得 |

### 5.4 NPUリソース

| 項目 | 上限 | 実測値 | 判定 |
|------|------|--------|------|
| Arena サイズ (sub_0000) | 442,368 bytes (432KB) | 991,872 bytes (969KB) | **超過 (+537KB)** |
| Arena サイズ (sub_0002) | - | 8,400 bytes (8.2KB) | OK |
| モデルデータ (sub_0000) | - | 315,280 bytes (308KB) | - |
| モデルデータ (sub_0002) | - | 3,824 bytes (3.7KB) | - |
| コマンドストリーム (sub_0000) | - | 27,160 bytes (26.5KB) | - |
| コマンドストリーム (sub_0002) | - | 1,064 bytes (1KB) | - |

### 5.5 モデル分割構造

顔認識サンプル（1リージョン）とは異なり、3リージョンに分割された:

| リージョン | ターゲット | 役割 |
|-----------|-----------|------|
| sub_0000 | ARM_ETHOS_U55 | メイン推論（293 ops, 72M MACs） |
| sub_0001 | C_CODEGEN (CPU) | Sigmoid + Reshape等の中間処理 |
| sub_0002 | ARM_ETHOS_U55 | 出力結合処理（10 ops, 6.8K MACs） |

### 5.6 Arena超過の問題と対策

sub_0000のArenaサイズ 991,872 bytes (969KB) が、内蔵SRAM (960KB) を超過している。

**検証結果:**
- `--ospi` オプションで再変換 → Arenaサイズ変化なし（重みではなくスクラッチ領域が原因）

**採用した対策: SDRAMにArena配置**

`sub_0000_net1_invoke.c` のArena宣言にセクション属性を追加:

```c
// 変更前
__attribute__((aligned(16))) uint8_t sub_0000_net1_arena[991872];

// 変更後
__attribute__((aligned(16), section(".sdram"))) uint8_t sub_0000_net1_arena[991872];
```

- SDRAM (64MB, 0x68000000〜) は初期化済み (`hal_warmstart.c`)
- `.sdram` セクションはリンカスクリプトで定義済み
- FSP設定変更は不要
- MERA再変換時はセクション属性の再追加が必要

### 5.4 参考: 顔認識サンプルの仕様

| 項目 | 顔認識サンプル |
|------|--------------|
| 入力テンソル名 | `image_input` |
| 入力サイズ | 36,864 bytes (192x192x1) |
| 出力0テンソル名 | `Identity_70275` |
| 出力0サイズ | 648 bytes (6x6x3x6) |
| 出力0量子化 | scale=0.13408391, zero_point=47 |
| 出力1テンソル名 | `Identity_1_70284` |
| 出力1サイズ | 2,592 bytes (12x12x3x6) |
| 出力1量子化 | scale=0.18535925, zero_point=10 |
| Arena | 442,368 bytes (432KB) |

## 6. 受け入れ条件チェックリスト

変換完了後に確認:

- [ ] MERAコードが正常に自動生成されている（全ファイルが存在）
- [ ] e2 studio でコンパイルエラーなし
- [ ] 入出力バッファサイズが上記テーブルに記録されている
- [ ] NPUアリーナサイズが 442,368 bytes (432KB) 以内
- [ ] 量子化パラメータ (scale, zero_point) が記録されている

## 7. トラブルシューティング

### Arena超過の場合

picoモデル (366KB) はArena 432KB以内に収まる見込みだが、入力サイズが顔認識の3倍 (110KB vs 36KB) のため超過する可能性がある。

対策:
1. `--ospi` オプションでOSPIフラッシュを使用（モデル重みを外部メモリに配置）
2. モデルの更なる小型化
3. SDRAM上にArena確保（要FSP設定変更）

### オペレータ非対応の場合

変換時に `Unsupported operator` エラーが出た場合:
1. `doc/analysis_report/f003_03d_ethos_u55_deployment_investigation.md` のオペレータ一覧を参照
2. 非対応オペレータを回避するモデル構造に変更（再学習が必要）

### suffix問題

`mcu_deploy.py` の `suffix='_net1'` により、関数名が `RunModel_net1()` 等になる場合がある。
顔認識サンプルとの互換性のため、`mcu_deploy.py` の `suffix` を `''` (空文字)に変更するか、`wrapper.h` で吸収する。

---

## 改訂履歴

| 日付 | バージョン | 変更内容 |
|------|-----------|---------|
| 2026-03-08 | 1.0 | 初版作成（変換手順書・入出力仕様テンプレート） |
| 2026-03-08 | 1.1 | 変換実行完了。入出力仕様を実測値で更新。Arena超過問題を記録 |
