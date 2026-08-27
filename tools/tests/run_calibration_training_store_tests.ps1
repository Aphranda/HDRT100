param(
    [string]$BuildDir = "out/pytest/build-calibration-training-store-tests",
    [string]$HostGccDir = "D:\Embedded\GCC\mingw64\bin"
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
New-Item -ItemType Directory -Force -Path $build | Out-Null
$cc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $cc) {
    $candidate = Join-Path $HostGccDir "gcc.exe"
    if (Test-Path $candidate) { $cc = Get-Item $candidate }
}
if (-not $cc) { $cc = Get-Command clang -ErrorAction SilentlyContinue }
if (-not $cc) { throw "A host gcc or clang compiler is required" }

$args = @(
    "-std=c11", "-Wall", "-Wextra", "-Werror",
    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
    "-I$repo\components\calibration_manager\inc",
    "-I$repo\components\tdma\inc",
    "-I$repo\components\flash_store\inc",
    "-I$repo\components\flash_transaction\inc",
    "-I$repo\drivers\mcu\flash\inc",
    "-I$repo\config",
    "$repo\tests\unit\test_calibration_training_store.c",
    "$repo\components\calibration_manager\src\calibration_training_store.c",
    "-o", "$build\test_calibration_training_store.exe"
)
& $cc.Source @args
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& "$build\test_calibration_training_store.exe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Calibration training store host unit tests passed"
