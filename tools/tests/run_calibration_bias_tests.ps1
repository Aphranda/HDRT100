param(
    [string]$BuildDir = "build-calibration-bias-tests",
    [string]$HostGccDir = "",
    [string]$ArmGcc = ""
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$include = Join-Path $repo "components\calibration_manager\inc"
$testSource = Join-Path $repo "tests\unit\test_calibration_bias.c"
$source = Join-Path $repo "components\calibration_manager\src\calibration_bias.c"
New-Item -ItemType Directory -Force -Path $build | Out-Null

if ($HostGccDir -and (Test-Path $HostGccDir)) {
    $env:PATH = "$HostGccDir;$env:PATH"
}

$hostCc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
if (-not $hostCc) {
    $hostCc = (Get-Command clang -ErrorAction SilentlyContinue).Source
}
if ($hostCc) {
    $exe = Join-Path $build "test_calibration_bias.exe"
    & $hostCc -std=c11 -Wall -Wextra -Werror "-I$include" $testSource $source -o $exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host "calibration_bias host unit tests passed"
    exit 0
}

if (-not $ArmGcc) {
    $resolved = Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue
    if ($resolved) { $ArmGcc = $resolved.Source }
}
if (-not $ArmGcc -or -not (Test-Path $ArmGcc)) {
    throw "No host C compiler or ARM GCC found"
}
Write-Host "ARM compiler found; host execution skipped"
