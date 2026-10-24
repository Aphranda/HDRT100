param(
    [string]$BuildDir = "out/pytest/build-refmem-lease-table-tests",
    [string]$ArmGcc = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$refmemInclude = Join-Path $repo "components\distributed_refmem\inc"
$testSource = Join-Path $repo "tests\unit\test_refmem_lease_table.c"
$leaseSource = Join-Path $repo "components\distributed_refmem\src\refmem_lease.c"
$tableSource = Join-Path $repo "components\distributed_refmem\src\refmem_lease_table.c"

New-Item -ItemType Directory -Force -Path $build | Out-Null

function Get-ToolPath {
    param([string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    return $null
}

$hostCc = Get-ToolPath "gcc"
if (-not $hostCc) {
    $hostCc = Get-ToolPath "clang"
}

if ($hostCc) {
    $exe = Join-Path $build "test_refmem_lease_table.exe"
    & $hostCc -std=c11 -Wall -Wextra -Werror "-I$refmemInclude" $testSource $leaseSource $tableSource -o $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    & $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Write-Host "refmem_lease_table host unit tests passed"
    exit 0
}

Write-Host "no host C compiler found; lease table host test skipped"
