param(
    [string]$BuildDir = "out/pytest/build-tdma-flight-overlay-tests",
    [string]$HostGccDir = ""
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$include = Join-Path $repo "components\tdma\inc"
$testSource = Join-Path $repo "tests\unit\test_tdma_flight_overlay.c"
$overlaySource = Join-Path $repo "components\tdma\src\tdma_flight_overlay.c"
New-Item -ItemType Directory -Force -Path $build | Out-Null

if ($HostGccDir -and (Test-Path $HostGccDir)) {
    $env:PATH = "$HostGccDir;$env:PATH"
}
$hostCc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
if (-not $hostCc) {
    $hostCc = (Get-Command clang -ErrorAction SilentlyContinue).Source
}
if (-not $hostCc) {
    throw "No host C compiler found"
}
$exe = Join-Path $build "test_tdma_flight_overlay.exe"
& $hostCc -std=c11 -Wall -Wextra -Werror "-I$include" `
    $testSource $overlaySource -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "tdma_flight_overlay host unit tests passed"
