# =============================================================================
# deploy_fall_detection.ps1
# RUHMI (MERA) による転倒/人物検出モデルの Ethos-U55 向け変換スクリプト
#
# 対応 RUHMI Framework: MERA 2.6.0+pkg.4513 以降（統合スクリプト mcu_compile.py）
#   ※ 旧 mcu_deploy.py / mcu_quantize.py は廃止され mcu_compile.py に統合された。
#
# 前提条件:
#   - ruhmi-framework-mcu リポジトリを別途クローンし、MERA 仮想環境を構築済みであること
#   - 仮想環境がアクティベート済みであること
#
# 使い方 (PowerShell):
#   cd C:\work\ruhmi-framework-mcu
#   .venv\Scripts\Activate.ps1
#   C:\Users\grace\github\mimamori-sense\scripts\deploy_fall_detection.ps1
#
#   または RUHMI_ROOT を指定:
#   .\scripts\deploy_fall_detection.ps1 -RuhmiRoot C:\work\ruhmi-framework-mcu
# =============================================================================

param(
    [string]$RuhmiRoot = ""
)

$ErrorActionPreference = "Stop"

# PowerShell コンソールの文字化け対策 (UTF-8)
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

# ---------- RUHMI Framework のパス解決 ----------
if ($RuhmiRoot -eq "") {
    # 引数未指定の場合、カレントディレクトリに scripts/mcu_compile.py があるか確認
    $candidate = Join-Path (Get-Location) "scripts\mcu_compile.py"
    if (Test-Path $candidate) {
        $RuhmiRoot = (Get-Location).Path
    } else {
        Write-Host "ERROR: RUHMI Framework のパスを特定できません" -ForegroundColor Red
        Write-Host ""
        Write-Host "以下のいずれかの方法で実行してください:"
        Write-Host "  方法1: ruhmi-framework-mcu ディレクトリで実行"
        Write-Host "    cd C:\work\ruhmi-framework-mcu"
        Write-Host "    $($MyInvocation.MyCommand.Path)"
        Write-Host ""
        Write-Host "  方法2: -RuhmiRoot オプションで指定"
        Write-Host "    $($MyInvocation.MyCommand.Path) -RuhmiRoot C:\work\ruhmi-framework-mcu"
        exit 1
    }
}

# パス設定
$ModelSrc = Join-Path $ProjectRoot "dataset\models\yolo_fastest_person_darknet_int8.tflite"
$RuhmiScripts = Join-Path $RuhmiRoot "scripts"
$McuCompile = Join-Path $RuhmiScripts "mcu_compile.py"

# 出力先 (mcu_compile.py が生成物を書き出す作業ディレクトリ)
$DeployOutput = Join-Path $ProjectRoot "scripts\deploy_fall_detection_output"

# 最終配置先
$MeraDest = Join-Path $ProjectRoot "e2studio_CPU0\src\ai_application\fall_detection\mera"

# ---------- 前提チェック ----------
Write-Host "=== 前提条件チェック ===" -ForegroundColor Cyan

if (-not (Test-Path $ModelSrc)) {
    Write-Host "ERROR: モデルファイルが見つかりません: $ModelSrc" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $McuCompile)) {
    Write-Host "ERROR: mcu_compile.py が見つかりません: $McuCompile" -ForegroundColor Red
    Write-Host "  RUHMI Framework (MERA 2.6.0 以降) のパスを確認してください"
    Write-Host "  git clone https://github.com/renesas/ruhmi-framework-mcu.git"
    Write-Host "  ※ 旧 mcu_deploy.py を使うバージョンは未対応です。最新版へ更新してください。"
    exit 1
}

try {
    python -c "import mera" 2>$null
} catch {
    Write-Host "ERROR: MERA パッケージが見つかりません" -ForegroundColor Red
    Write-Host "  python -m pip install .\install\mera-*-win_amd64.whl を実行してください"
    exit 1
}

$modelSize = (Get-Item $ModelSrc).Length
Write-Host "  RUHMI: $RuhmiRoot"
Write-Host "  Model: $ModelSrc"
Write-Host "  Size:  $([math]::Round($modelSize / 1KB)) KB ($modelSize bytes)"
Write-Host "  OK" -ForegroundColor Green

# ---------- 出力ディレクトリ準備 ----------
Write-Host ""
Write-Host "=== 変換準備 ===" -ForegroundColor Cyan

if (Test-Path $DeployOutput) { Remove-Item $DeployOutput -Recurse -Force }
New-Item -ItemType Directory -Path $DeployOutput -Force | Out-Null
Write-Host "  出力ディレクトリ: $DeployOutput"

# ---------- MERA 変換実行 ----------
# mcu_compile.py 引数:
#   <model_path> <output_dir> --npu --ref-data --suffix _net1
#   --npu      : Ethos-U55 NPU 向けコンパイル (旧 --ethos に相当)
#   --ref-data : ターゲット検証用のリファレンス入出力データを生成 (旧 --ref_data に相当)
#   --suffix _net1 : 生成 C 関数名のサフィックス。既存統合コード (model_net1.c /
#                    RunModel_net1 等) との互換のため _net1 を維持
#                    (旧 mcu_deploy.py の suffix='_net1' ハードコード相当)
#   ※ モデルは既に INT8 量子化済みのため --quantize は不要
#   ※ SDRAM への arena 配置は変換後に section(".sdram") 属性を手動付与する
#      (f003_04_mera_conversion_guide.md 5.6節)。--external は OSPI 配置のため使用しない。
Write-Host ""
Write-Host "=== MERA 変換実行 (Ethos-U55) ===" -ForegroundColor Cyan
Write-Host "  コマンド: python mcu_compile.py $ModelSrc $DeployOutput --npu --ref-data --suffix _net1"
Write-Host ""

Push-Location $RuhmiScripts
try {
    python mcu_compile.py $ModelSrc $DeployOutput --npu --ref-data --suffix _net1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: MERA 変換に失敗しました (exit code: $LASTEXITCODE)" -ForegroundColor Red
        exit 1
    }
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "=== 変換完了 ===" -ForegroundColor Green

# ---------- 生成ファイル確認 ----------
# 生成物パス例 (MERA 2.6.0): <out>\<model>_NPU\deploy\build\MCU\compilation\src
$GeneratedDir = Get-ChildItem -Path $DeployOutput -Recurse -Directory |
    Where-Object { $_.FullName -match "build[/\\]MCU[/\\]compilation[/\\]src$" } |
    Select-Object -First 1

if (-not $GeneratedDir) {
    Write-Host "ERROR: 生成されたCコードが見つかりません" -ForegroundColor Red
    Write-Host "  $DeployOutput 配下を確認してください"
    exit 1
}

Write-Host ""
Write-Host "=== 生成ファイル一覧 ===" -ForegroundColor Cyan
Get-ChildItem $GeneratedDir.FullName | Format-Table Name, Length -AutoSize

# ---------- 配置先にコピー ----------
Write-Host "=== e2studio プロジェクトへの配置 ===" -ForegroundColor Cyan

if (-not (Test-Path $MeraDest)) {
    New-Item -ItemType Directory -Path $MeraDest -Force | Out-Null
}

# 旧生成ファイルの掃除 (再変換で命名が変わった場合に stale ファイルが残らないように)
# mera/ は全て MERA 生成物のため *.c / *.h を一旦削除する (README.md 等は保持)
Get-ChildItem (Join-Path $MeraDest "*") -Include "*.c", "*.h" -File | ForEach-Object {
    Remove-Item $_.FullName -Force
    Write-Host "  CLEAN: $($_.Name) (旧生成ファイル)" -ForegroundColor DarkGray
}

# MCU組み込みに必要なファイルのみコピー (x86テスト用ファイルは除外)
$excludeFiles = @("CMakeLists.txt", "compare.cpp", "hal_entry.c", "python_bindings.cpp")

Get-ChildItem (Join-Path $GeneratedDir.FullName "*") -Include "*.c", "*.h" -File | ForEach-Object {
    if ($excludeFiles -contains $_.Name) {
        Write-Host "  SKIP: $($_.Name) (x86テスト用)" -ForegroundColor DarkGray
    } else {
        Copy-Item $_.FullName -Destination $MeraDest -Force
        Write-Host "  COPY: $($_.Name)" -ForegroundColor White
    }
}

# ---------- 入出力仕様の抽出 ----------
Write-Host ""
Write-Host "=== 入出力仕様の抽出 ===" -ForegroundColor Cyan

# Arena サイズ (--suffix _net1 適用時のファイル名で探索。命名が異なる場合は手動確認)
$tensorsH = Get-ChildItem $MeraDest -Filter "*tensors.h" -File | Select-Object -First 1
if ($tensorsH) {
    Write-Host "  Arena ($($tensorsH.Name)):"
    Select-String -Path $tensorsH.FullName -Pattern "kArenaSize" | ForEach-Object { Write-Host "    $($_.Line.Trim())" }
}

# 入出力サイズ
$ioDataH = Join-Path $MeraDest "model_io_data.h"
if (Test-Path $ioDataH) {
    Write-Host "  入出力バッファサイズ:"
    Select-String -Path $ioDataH -Pattern "_SIZE" | ForEach-Object { Write-Host "    $($_.Line.Trim())" }
}

# テンソルアドレス
$tensorsC = Get-ChildItem $MeraDest -Filter "*tensors.c" -File | Select-Object -First 1
if ($tensorsC) {
    Write-Host "  テンソルアドレス ($($tensorsC.Name)):"
    Select-String -Path $tensorsC.FullName -Pattern "address_" | ForEach-Object { Write-Host "    $($_.Line.Trim())" }
}

Write-Host ""
Write-Host "=== 完了 ===" -ForegroundColor Green
Write-Host "  生成コード配置先: $MeraDest"
Write-Host "  生成元 (参考用): $($GeneratedDir.FullName)"
Write-Host ""
Write-Host "次のステップ:" -ForegroundColor Yellow
Write-Host "  1. 上記の Arena サイズを確認 (内蔵SRAM超過時は .sdram セクション属性を付与)"
Write-Host "  2. model_io_data.h の入出力サイズを記録"
Write-Host "  3. *tensors.c の量子化パラメータを確認"
Write-Host "  4. 関数名サフィックス (_net1) と wrapper.h の関数名が一致するか確認"
Write-Host "  5. sub_0000_net1_invoke.c の arena 宣言に section(\".sdram\") を再付与 (f003_04 5.6節)"
