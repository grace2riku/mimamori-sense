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
# MERA 本体のインストール (MERA 2.6.0 以降)
python -m pip install .\install\mera-2.6.0+pkg.4513-cp310-cp310-win_amd64.whl

# 依存パッケージのインストール
python -m pip install onnx==1.17.0 tflite==2.18.0
```

> 注: 最新の正確な wheel ファイル名は [install ディレクトリ](https://github.com/renesas/ruhmi-framework-mcu/tree/main/install) で確認すること（pkg 番号はリリースで変わる）。MERA 2.6.0 で変換スクリプトが `mcu_deploy.py` / `mcu_quantize.py` から統合スクリプト `mcu_compile.py` へ置き換わった。

### 2.5 動作確認

```powershell
python -c "import mera; print(mera.__version__)"
# 期待出力: 2.6.0+pkg.4513

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

# RUHMI変換 (Ethos-U55向け)
# mcu_compile.py は単一モデルファイルを直接受け付けるため一時ディレクトリは不要
cd scripts
python mcu_compile.py C:\Users\grace\github\mimamori-sense\dataset\models\yolov8_pico_fall_int8.tflite C:\work\deploy_fall_detection_output --npu --ref-data --suffix _net1
```

> 引数の対応（旧 `mcu_deploy.py` → 新 `mcu_compile.py`）:
> - `--ethos` → `--npu`（Ethos-U55 NPU 向け）
> - `--ref_data`（フラグ）→ `--ref-data`（フラグ）
> - モデル/出力は位置引数 `<model_path> <output_dir>`。`mcu_compile.py` は単一ファイル指定が可能
> - `--suffix _net1`: 生成関数名のサフィックス。旧 `mcu_deploy.py` の `suffix='_net1'` ハードコード相当
> - モデルは既に INT8 のため `--quantize` は不要

### 3.2 生成コードの確認

```powershell
# 生成されたCソースコードの一覧
Get-ChildItem C:\work\deploy_fall_detection_output\ -Recurse -Include "*.c","*.h" | Select-Object Name, Length
```

期待される出力パス（MERA 2.6.0 の命名規約 `{model_name}_NPU`）: `C:\work\deploy_fall_detection_output\yolov8_pico_fall_int8_NPU\deploy\build\MCU\compilation\src\`

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

- `mcu_compile.py` は単一モデルファイルを直接受け付ける（旧 `mcu_deploy.py` のようにディレクトリ内全 `.tflite` を変換しないため、一時ディレクトリでの隔離は不要）。
- モデルサイズ 366KB は `--memory-threshold`（既定 0.8MB）未満のため外部メモリモードは自動無効（出力ディレクトリ名に `_external` は付かない）。
- `--suffix _net1` で生成関数名のサフィックスを明示する。既存統合コード（`model_net1.c` / `RunModel_net1()` 等）との互換のため `_net1` を維持する。サフィックスを変える場合は上位 `wrapper.h` の関数名を合わせて調整する。
- 内蔵SRAMを超える arena は、`--external`（OSPI配置）ではなく `.sdram` セクション属性の手動付与で対応する（5.6節）。

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

`mcu_compile.py` の `--suffix _net1` により、関数名が `RunModel_net1()` 等になる。
顔認識サンプル（サフィックスなし）との互換性が必要な場合は `--suffix ""`（空文字）にするか、`wrapper.h` で吸収する。
本プロジェクトの統合コードは `_net1` 付きで実装済みのため、再変換時も `--suffix _net1` を維持する。

---

## 改訂履歴

| 日付 | バージョン | 変更内容 |
|------|-----------|---------|
| 2026-03-08 | 1.0 | 初版作成（変換手順書・入出力仕様テンプレート） |
| 2026-03-08 | 1.1 | 変換実行完了。入出力仕様を実測値で更新。Arena超過問題を記録 |
| 2026-06-05 | 1.2 | RUHMI Framework 更新 (Issue #144) に伴い、MERA 2.6.0 / 統合スクリプト `mcu_compile.py` へ変換手順・引数・バージョン記述を更新 |
