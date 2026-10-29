param(
    [string]$BuildDir = "out/pytest/build-refmem-position-reset-policy-tests"
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$refmemInclude = Join-Path $repo "components\distributed_refmem\inc"
$tdmaInclude = Join-Path $repo "components\tdma\inc"
$otaInclude = Join-Path $repo "components\ota_manager\inc"
$triggerInclude = Join-Path $repo "components\sync_trigger\inc"
$osalInclude = Join-Path $repo "osal\inc"
$testSource = Join-Path $repo "tests\unit\test_refmem_position_reset_policy.c"
$policySource = Join-Path $repo "components\distributed_refmem\src\refmem_position_reset_policy.c"

New-Item -ItemType Directory -Force -Path $build | Out-Null

$hostCc = Get-Command "gcc" -ErrorAction SilentlyContinue
if (-not $hostCc) {
    $hostCc = Get-Command "clang" -ErrorAction SilentlyContinue
}
if (-not $hostCc) {
    throw "No host C compiler found (gcc/clang)"
}

$exe = Join-Path $build "test_refmem_position_reset_policy.exe"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror `
    "-I$refmemInclude" "-I$tdmaInclude" "-I$otaInclude" "-I$triggerInclude" "-I$osalInclude" `
    $testSource $policySource -o $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
& $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
Write-Host "refmem_position_reset_policy host unit tests passed"
