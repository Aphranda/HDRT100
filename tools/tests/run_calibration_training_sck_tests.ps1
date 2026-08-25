param(
    [string]$BuildDir = "out/pytest/build-calibration-training-sck"
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$testSource = Join-Path $repo "tests\unit\test_calibration_training_sck.c"
$source = Join-Path $repo "components\calibration_manager\src\calibration_training_sck.c"
$phaseSource = Join-Path $repo "components\calibration_manager\src\calibration_training_phase.c"
$include = Join-Path $repo "components\calibration_manager\inc"

New-Item -ItemType Directory -Force -Path $build | Out-Null

$hostCompiler = Get-Command gcc -ErrorAction SilentlyContinue
if ($null -ne $hostCompiler) {
    $exe = Join-Path $build "test_calibration_training_sck.exe"
    & $hostCompiler.Source -std=c11 -Wall -Wextra -Werror `
        -I $include $testSource $source $phaseSource -o $exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host "calibration_training_sck host unit tests passed"
    exit 0
}

$armCompiler = Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue
if ($null -eq $armCompiler) {
    throw "neither gcc nor arm-none-eabi-gcc is available"
}

foreach ($entry in @(
    @{ Source = $testSource; Name = "test_calibration_training_sck.o" },
    @{ Source = $source; Name = "calibration_training_sck.o" },
    @{ Source = $phaseSource; Name = "calibration_training_phase.o" }
)) {
    & $armCompiler.Source -std=c11 -Wall -Wextra -Werror `
        -mcpu=cortex-m33 -mthumb -I $include -c $entry.Source `
        -o (Join-Path $build $entry.Name)
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
Write-Host "calibration_training_sck ARM compile checks passed"
