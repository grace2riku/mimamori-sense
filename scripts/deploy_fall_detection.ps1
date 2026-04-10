# =============================================================================
# deploy_fall_detection.ps1
# RUHMI (MERA) による転倒検出モデルの Ethos-U55 向け変換スクリプト
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
    # 引数未指定の場合、カレントディレクトリに scripts/mcu_deploy.py があるか確認
    $candidate = Join-Path (Get-Location) "scripts\mcu_deploy.py"
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
$McuDeploy = Join-Path $RuhmiScripts "mcu_deploy.py"

# 一時ディレクトリ (picoモデルだけを配置して変換)
$TempModelDir = Join-Path $ProjectRoot "scripts\_temp_model_input"
$DeployOutput = Join-Path $ProjectRoot "scripts\deploy_fall_detection_output"

# 最終配置先
$MeraDest = Join-Path $ProjectRoot "e2studio_CPU0\src\ai_application\fall_detection\mera"

# ---------- 前提チェック ----------
Write-Host "=== 前提条件チェック ===" -ForegroundColor Cyan

if (-not (Test-Path $ModelSrc)) {
    Write-Host "ERROR: モデルファイルが見つかりません: $ModelSrc" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $McuDeploy)) {
    Write-Host "ERROR: mcu_deploy.py が見つかりません: $McuDeploy" -ForegroundColor Red
    Write-Host "  RUHMI Framework のパスを確認してください"
    Write-Host "  git clone https://github.com/renesas/ruhmi-framework-mcu.git"
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

# ---------- 一時ディレクトリ準備 ----------
Write-Host ""
Write-Host "=== 変換準備 ===" -ForegroundColor Cyan

if (Test-Path $TempModelDir) { Remove-Item $TempModelDir -Recurse -Force }
New-Item -ItemType Directory -Path $TempModelDir -Force | Out-Null
Copy-Item $ModelSrc -Destination $TempModelDir
Write-Host "  一時モデルディレクトリ: $TempModelDir"

# ---------- MERA 変換実行 ----------
Write-Host ""
Write-Host "=== MERA 変換実行 (Ethos-U55) ===" -ForegroundColor Cyan
Write-Host "  コマンド: python mcu_deploy.py --ethos --ref_data $TempModelDir $DeployOutput"
Write-Host ""

Push-Location $RuhmiScripts
try {
    python mcu_deploy.py --ethos --ref_data $TempModelDir $DeployOutput
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

# Arena サイズ
$tensorsH = Join-Path $MeraDest "sub_0000_tensors.h"
if (Test-Path $tensorsH) {
    Write-Host "  Arena:"
    Select-String -Path $tensorsH -Pattern "kArenaSize" | ForEach-Object { Write-Host "    $($_.Line.Trim())" }
}

# 入出力サイズ
$ioDataH = Join-Path $MeraDest "model_io_data.h"
if (Test-Path $ioDataH) {
    Write-Host "  入出力バッファサイズ:"
    Select-String -Path $ioDataH -Pattern "_SIZE" | ForEach-Object { Write-Host "    $($_.Line.Trim())" }
}

# テンソルアドレス
$tensorsC = Join-Path $MeraDest "sub_0000_tensors.c"
if (Test-Path $tensorsC) {
    Write-Host "  テンソルアドレス:"
    Select-String -Path $tensorsC -Pattern "address_" | ForEach-Object { Write-Host "    $($_.Line.Trim())" }
}

# ---------- クリーンアップ ----------
Remove-Item $TempModelDir -Recurse -Force

Write-Host ""
Write-Host "=== 完了 ===" -ForegroundColor Green
Write-Host "  生成コード配置先: $MeraDest"
Write-Host "  生成元 (参考用): $($GeneratedDir.FullName)"
Write-Host ""
Write-Host "次のステップ:" -ForegroundColor Yellow
Write-Host "  1. 上記の Arena サイズが 442,368 (432KB) 以内か確認"
Write-Host "  2. model_io_data.h の入出力サイズを記録"
Write-Host "  3. sub_0000_tensors.c の量子化パラメータを確認"
Write-Host "  4. wrapper.h の関数名を確認し、必要に応じて上位の wrapper.h を更新"
