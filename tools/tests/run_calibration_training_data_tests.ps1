param(
    [string]$BuildDir = "out/pytest/build-calibration-training-data-tests",
    [string]$HostGccDir = "",
    [string]$ArmGcc = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$include = Join-Path $repo "components\calibration_manager\inc"
$testSource = Join-Path $repo "tests\unit\test_calibration_training_data.c"
$source = Join-Path $repo "components\calibration_manager\src\calibration_training_data.c"

New-Item -ItemType Directory -Force -Path $build | Out-Null

if ($HostGccDir -and (Test-Path $HostGccDir)) {
    $env:PATH = "$HostGccDir;$env:PATH"
}

$hostCc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
if (-not $hostCc) {
    $hostCc = (Get-Command clang -ErrorAction SilentlyContinue).Source
}

if ($hostCc) {
    $exe = Join-Path $build "test_calibration_training_data.exe"
    & $hostCc -std=c11 -Wall -Wextra -Werror "-I$include" `
        $testSource $source -o $exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host "calibration_training_data host unit tests passed"
    exit 0
}

if (-not $ArmGcc) {
    $candidates = @(
        (Join-Path $env:USERPROFILE ".pico-sdk\toolchain\14_2_Rel1\bin\arm-none-eabi-gcc.exe"),
        (Join-Path $env:USERPROFILE ".pico-sdk\toolchain\13_3_Rel1\bin\arm-none-eabi-gcc.exe")
    )
    $ArmGcc = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $ArmGcc) {
    throw "No host gcc/clang or ARM GCC found"
}

$objects = @()
foreach ($entry in @(
    @{ Source = $testSource; Name = "test_calibration_training_data.o" },
    @{ Source = $source; Name = "calibration_training_data.o" }
)) {
    $object = Join-Path $build $entry.Name
    & $ArmGcc -std=c11 -Wall -Wextra -Werror "-I$include" -c $entry.Source -o $object
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $objects += $object
}
Write-Host "calibration_training_data ARM compile checks passed"
