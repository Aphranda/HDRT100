param(
    [string]$BuildDir = "out/pytest/build-refmem-read-catalog-tests"
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$refmemInclude = Join-Path $repo "components\distributed_refmem\inc"
$testSource = Join-Path $repo "tests\unit\test_refmem_read_catalog.c"
$tableSource = Join-Path $repo "components\distributed_refmem\src\refmem_read_catalog_table.c"

New-Item -ItemType Directory -Force -Path $build | Out-Null

# Drift gate: the generated header/table must match config/refmem/read_catalog.yaml.
$gen = Join-Path $repo "tools\refmem\refmem_read_catalog_gen.py"
& python $gen --check
if ($LASTEXITCODE -ne 0) { throw "read catalog drift detected; rerun $gen" }

$hostCc = Get-Command "gcc" -ErrorAction SilentlyContinue
if (-not $hostCc) {
    $hostCc = Get-Command "clang" -ErrorAction SilentlyContinue
}
if (-not $hostCc) {
    throw "No host C compiler found (gcc/clang)"
}

$exe = Join-Path $build "test_refmem_read_catalog.exe"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror -I $refmemInclude `
    $testSource $tableSource -o $exe
if ($LASTEXITCODE -ne 0) { throw "compile failed" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "test failed" }
Write-Output "REFMEM read catalog host tests passed"
