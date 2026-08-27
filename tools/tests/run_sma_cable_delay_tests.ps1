param(
    [string]$BuildDir = "out/pytest/build-sma-cable-delay-tests",
    [string]$HostGccDir = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$include = Join-Path $repo "components\sma_cable_delay\inc"
$testSource = Join-Path $repo "tests\unit\test_sma_cable_delay.c"
$componentSource = Join-Path $repo "components\sma_cable_delay\src\sma_cable_delay.c"

New-Item -ItemType Directory -Force -Path $build | Out-Null

if ($HostGccDir -and (Test-Path $HostGccDir)) {
    $env:PATH = "$HostGccDir;$env:PATH"
}

$compiler = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $compiler) {
    $compiler = Get-Command clang -ErrorAction SilentlyContinue
}
if (-not $compiler) {
    throw "No host C compiler found (gcc or clang)"
}

$exe = Join-Path $build "test_sma_cable_delay.exe"
& $compiler.Source -std=c11 -Wall -Wextra -Werror "-I$include" `
    $testSource $componentSource -lm -o $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
Write-Host "sma_cable_delay host unit tests passed"
