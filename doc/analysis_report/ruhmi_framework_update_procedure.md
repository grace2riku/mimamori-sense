# RUHMI Framework 更新手順・チェック項目（Issue #144）

## 1. 目的・背景

Issue #142 で、ローカル PC の RUHMI Framework が最新版より古い（`MERA 2.5.0+pkg.3577`）ことを確認し、更新を推奨した（詳細: `ruhmi_framework_version_check_report.md`）。

本書は、その推奨に基づきローカル PC の RUHMI Framework を **最新版（`MERA 2.6.0+pkg.4513`）へ更新する手順**と、**更新が正しく行われたかのチェック項目**をまとめたものである。あわせて、更新に伴い必要となったモデル再変換・コード統合・実機確認までの一連の作業手順を記録する。

本書の手順は **2026-06-06 に実機（EK-RA8P1）まで検証済み**である。

---

## 2. 対象環境

| 項目 | 値 |
|------|-----|
| RUHMI Framework クローン先 | `C:\work\ruhmi-framework-mcu`（git clone、本リポジトリ外） |
| Python 仮想環境 | `C:\work\ruhmi-framework-mcu\.venv`（Python 3.10） |
| ホスト OS | Windows 11 / PowerShell |
| 変換対象モデル | `dataset/models/yolo_fastest_person_darknet_int8.tflite`（192x192, INT8） |
| ターゲット NPU | Arm Ethos-U55（RA8P1 内蔵） |

### 更新前後のバージョン

| | 更新前 | 更新後 |
|---|--------|--------|
| mera | `2.5.0+pkg.3577` | `2.6.0+pkg.4513` |
| ethos-u-vela | 4.2.0 | 4.2.0（変化なし） |
| 変換スクリプト | `mcu_deploy.py` / `mcu_quantize.py` | **`mcu_compile.py`（統合）** |
| RUHMI コミット | `82ebd1c`（2026-02-08） | `6f6159f8`（2026-06-04） |

> **破壊的変更**: MERA 2.5.0 → 2.6.0 で、変換スクリプト `mcu_deploy.py` / `mcu_quantize.py` が廃止され、統合スクリプト **`mcu_compile.py`** に一本化された。

---

## 3. 更新手順

> 前提: `C:\work\ruhmi-framework-mcu` が git clone 済みで、作業ツリーがクリーン（ローカル変更なし）であること。
> ローカル変更がある場合は退避（`git stash` 等）してから実施する。

### 手順 A-1. 最新版の取得

```powershell
cd C:\work\ruhmi-framework-mcu
git status -sb           # 作業ツリーがクリーンか確認（ローカル変更がないこと）
git pull origin main     # fast-forward 更新（mcu_compile.py と新 wheel を取得）
```

### 手順 A-2. 仮想環境の有効化

```powershell
.venv\Scripts\Activate.ps1
# プロンプト先頭に (.venv) が表示されること
```

> 既存の `.venv` をそのまま再利用してよい（作り直し不要）。パッケージを入れ替えるだけでよい。

### 手順 A-3. mera パッケージの入れ替え

```powershell
python -m pip uninstall -y mera
python -m pip install .\install\mera-2.6.0+pkg.4513-cp310-cp310-win_amd64.whl
```

- 依存パッケージ（tensorflow 2.18.0 / ethos-u-vela 4.2.0 等）は既存環境で `already satisfied` となり追加インストール不要。
- 末尾に出る `WARNING: You are using pip version ...` は pip 自体の更新案内であり、無視してよい。
- 最終行に **`Successfully installed mera-2.6.0+pkg.4513`** が表示されれば成功。

### 手順 A-4.（任意）visualizer の更新

```powershell
python -m pip install .\install\mera_visualizer-2.5.0+mcuv4-py3-none-any.whl
```

---

## 4. 更新チェック項目（Phase B）

仮想環境を有効化した状態で以下を実行し、すべて「期待値」と一致することを確認する。

| # | コマンド | 期待値 |
|---|----------|--------|
| 1 | `python -c "import mera; print(mera.__version__)"` | `2.6.0+pkg.4513` |
| 2 | `python -c "import mera; print(mera.get_versions())"` | `mera version 2.6.0+pkg.4513 ...` |
| 3 | `vela --version` | `4.2.0` |
| 4 | `Test-Path .\scripts\mcu_compile.py` | `True`（新スクリプトが存在） |
| 5 | `Test-Path .\scripts\mcu_deploy.py` | `False`（旧スクリプトが廃止） |

5項目すべてが期待値どおりであれば、RUHMI Framework の更新は完了である。

---

## 5. モデル再変換とコード統合（Phase D）

PC ツールの更新だけでは MCU ファームウェアは変わらない。更新したツールでモデルを再変換し、生成コードを本リポジトリへ反映する必要がある。

### 手順 D-1. 再変換スクリプトの実行

```powershell
# venv 有効化済みの状態で実行
cd C:\work\ruhmi-framework-mcu
C:\Users\grace\github\mimamori-sense\scripts\deploy_fall_detection.ps1
```

`deploy_fall_detection.ps1` は内部で以下を実行する（旧 `mcu_deploy.py` から移行済み）:

```
python mcu_compile.py <model>.tflite <out> --npu --ref-data --suffix _net1
```

| 引数 | 意味 |
|------|------|
| `--npu` | Ethos-U55 NPU 向けコンパイル（旧 `--ethos` 相当） |
| `--ref-data` | ターゲット検証用リファレンス入出力データ生成（旧 `--ref_data` 相当） |
| `--suffix _net1` | 生成 C 関数名のサフィックス。既存統合コードとの互換のため維持 |

> モデルは INT8 量子化済みのため `--quantize` は不要。
> 生成物パスは `<out>\<model>_NPU_net1\deploy\build\MCU\compilation\src`（スクリプトが再帰探索で吸収）。

スクリプトは生成 `*.c` / `*.h` を `e2studio_CPU0\src\ai_application\fall_detection\mera\` へ配置する（旧ファイルは自動掃除）。

### 手順 D-2. 生成コードの整合確認

再変換により、MERA が内部で付与する**ノードIDサフィックス（`_NNNNN`）が変化する場合がある**。今回は出力テンソルの関数名が以下のように変わった:

| | 更新前 | 更新後 |
|---|--------|--------|
| 出力0（branch0, 648B） | `..._StatefulPartitionedCall_0_70535` | `..._StatefulPartitionedCall_0_70327` |
| 出力1（branch1, 2592B） | `..._StatefulPartitionedCall_1_70554` | `..._StatefulPartitionedCall_1_70338` |

手書き統合コード `fall_detection/wrapper.h` がこれらの関数を名前で呼んでいるため、**`mera/model_net1.h` の関数名に合わせて更新する**こと（更新を怠ると undefined reference でビルドエラー）。

```c
// wrapper.h（更新後）
mera_output_ptr_branch0 -> GetModelOutputPtr_net1_StatefulPartitionedCall_0_70327()
mera_output_ptr_branch1 -> GetModelOutputPtr_net1_StatefulPartitionedCall_1_70338()
```

### 手順 D-3. メモリ配置の確認

- Arena サイズ: `442368` bytes（432 KiB）。更新前後で**不変**。
- arena は `sub_0000_net1_invoke.c` で通常の `.bss`（`uint8_t sub_0000_net1_arena[442368]`）として宣言される。
- **本モデルでは `section(".sdram")` 属性は不要**（プロジェクト全体で未使用、再生成後も invoke.c はコミット済みとバイト一致）。
  - ※ 将来モデルが大型化し arena が内蔵 SRAM を超える場合は、`f003_04_mera_conversion_guide.md` 5.6 節に従い `.sdram` 配置を検討する。

---

## 6. ビルド・実機確認（Phase E / F）

| Phase | 作業 | 確認内容 |
|-------|------|----------|
| E | e2 studio で `e2studio_CPU0`（または solution）を **Build** | エラー0でビルド成功。特に `wrapper.h` のサフィックス更新後の関数が `model_net1.c` のシンボルと解決されること |
| F | EK-RA8P1 へ書き込み、実機動作確認 | カメラ→NPU推論→LCD 表示が従来どおり動作すること（`ethosu_invoke` 成功） |

> **2026-06-06 実施結果**: ビルド成功・実機で従来どおりの動作を確認済み。

---

## 7. 既知のハマりどころ

| 事象 | 対処 |
|------|------|
| `mcu_deploy.py` が見つからない | 2.6.0 で廃止。`mcu_compile.py` を使う（`deploy_fall_detection.ps1` は対応済み） |
| ビルドで `GetModelOutputPtr_..._70535` 等が未定義 | 再変換でサフィックスが変わったため。`wrapper.h` を `model_net1.h` の新関数名に追従（手順 D-2） |
| `deploy_fall_detection.ps1` の文字化け・パースエラー | `.ps1` は **UTF-8 BOM 付き**で保存すること（PowerShell 5.1 が BOM 無し UTF-8 を ANSI と誤認するため） |
| pip の version warning | pip 自体の更新案内。mera とは無関係、無視可 |

---

## 8. 改訂履歴

| 日付 | バージョン | 変更内容 |
|------|-----------|----------|
| 2026-06-06 | 1.0 | 初版作成。RUHMI Framework を MERA 2.6.0+pkg.4513 へ更新し、実機まで検証（Issue #144） |
