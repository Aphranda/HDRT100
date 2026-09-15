param(
    [string]$BuildDir = "out/pytest/build-vdc-time-mapping-tests",
    [string]$HostGccDir = ""
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
New-Item -ItemType Directory -Force -Path $build | Out-Null

$hostCc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $hostCc -and $HostGccDir) {
    $gcc = Join-Path $HostGccDir "gcc.exe"
    if (Test-Path $gcc) {
        $env:PATH = "$HostGccDir;$env:PATH"
        $hostCc = Get-Command gcc -ErrorAction SilentlyContinue
    }
}
if (-not $hostCc) {
    throw "Host GCC not found in PATH; pass -HostGccDir to override"
}

$exe = Join-Path $build "test_vdc_time_mapping.exe"
$include = Join-Path $repo "components\vdc_dpll_manager\inc"
$tdmaInclude = Join-Path $repo "components\tdma\inc"
$source = Join-Path $repo "tests\unit\test_vdc_time_mapping.c"
$mapping = Join-Path $repo "components\vdc_dpll_manager\src\vdc_time_mapping.c"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror "-I$include" "-I$tdmaInclude" `
    $source $mapping -o $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
& $exe
exit $LASTEXITCODE
