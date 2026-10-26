param(
    [string]$BuildDir = "out/pytest/build-tdma-seat-rotation-online-tests"
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$tdmaInclude = Join-Path $repo "components\tdma\inc"
$osalInclude = Join-Path $repo "osal\inc"
$testSource = Join-Path $repo "tests\unit\test_tdma_seat_rotation_online.c"

New-Item -ItemType Directory -Force -Path $build | Out-Null

$hostCc = Get-Command "gcc" -ErrorAction SilentlyContinue
if (-not $hostCc) {
    $hostCc = Get-Command "clang" -ErrorAction SilentlyContinue
}
if (-not $hostCc) {
    throw "No host C compiler found (gcc/clang)"
}

$exe = Join-Path $build "test_tdma_seat_rotation_online.exe"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror `
    "-I$tdmaInclude" "-I$osalInclude" $testSource -o $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
& $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
Write-Host "tdma_seat_rotation_online host unit tests passed"
