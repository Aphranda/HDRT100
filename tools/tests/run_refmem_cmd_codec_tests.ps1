param(
    [string]$BuildDir = "out/pytest/build-refmem-cmd-codec-tests"
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$refmemInclude = Join-Path $repo "components\distributed_refmem\inc"
$tdmaInclude = Join-Path $repo "components\tdma\inc"
$testSource = Join-Path $repo "tests\unit\test_refmem_cmd_codec.c"
$codecSource = Join-Path $repo "components\distributed_refmem\src\refmem_cmd_codec.c"

New-Item -ItemType Directory -Force -Path $build | Out-Null

$hostCc = Get-Command "gcc" -ErrorAction SilentlyContinue
if (-not $hostCc) {
    $hostCc = Get-Command "clang" -ErrorAction SilentlyContinue
}
if (-not $hostCc) {
    throw "No host C compiler found (gcc/clang)"
}

$exe = Join-Path $build "test_refmem_cmd_codec.exe"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror `
    "-I$refmemInclude" "-I$tdmaInclude" `
    $testSource $codecSource -o $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
& $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
Write-Host "refmem_cmd_codec host unit tests passed"
