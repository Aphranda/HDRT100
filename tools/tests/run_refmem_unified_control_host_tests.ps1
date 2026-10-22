param(
    [string]$BuildDir = "out/pytest/build-refmem-unified-control-host-tests"
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$refmemInclude = Join-Path $repo "components\distributed_refmem\inc"
$testSource = Join-Path $repo "tests\unit\test_refmem_unified_control_host.c"
$sources = @(
    (Join-Path $repo "components\distributed_refmem\src\refmem_cmd_codec.c"),
    (Join-Path $repo "components\distributed_refmem\src\refmem_control_route.c"),
    (Join-Path $repo "components\distributed_refmem\src\refmem_trigger_session.c"),
    (Join-Path $repo "components\distributed_refmem\src\refmem_action_registry.c")
)

New-Item -ItemType Directory -Force -Path $build | Out-Null

$hostCc = Get-Command "gcc" -ErrorAction SilentlyContinue
if (-not $hostCc) {
    $hostCc = Get-Command "clang" -ErrorAction SilentlyContinue
}
if (-not $hostCc) {
    throw "No host C compiler found (gcc/clang)"
}

$exe = Join-Path $build "test_refmem_unified_control_host.exe"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror `
    "-I$refmemInclude" $testSource $sources -o $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
& $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
Write-Host "refmem_unified_control_host host tests passed"
