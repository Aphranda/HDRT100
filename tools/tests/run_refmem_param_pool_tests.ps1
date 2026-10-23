param(
    [string]$BuildDir = "out/pytest/build-refmem-param-pool-tests",
    [string]$ArmGcc = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$refmemInclude = Join-Path $repo "components\distributed_refmem\inc"
$testSource = Join-Path $repo "tests\unit\test_refmem_param_pool.c"
$moduleSource = Join-Path $repo "components\distributed_refmem\src\refmem_param_pool.c"

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
    $exe = Join-Path $build "test_refmem_param_pool.exe"
    & $hostCc -std=c11 -Wall -Wextra -Werror "-I$refmemInclude" $testSource $moduleSource -o $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    & $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Write-Host "refmem_param_pool host unit tests passed"
    exit 0
}

if (-not $ArmGcc) {
    $ArmGcc = ""
}
if (-not (Test-Path $ArmGcc)) {
    throw "No host C compiler found and ARM GCC not found at $ArmGcc"
}

foreach ($source in @($testSource, $moduleSource)) {
    $object = Join-Path $build ((Split-Path -Leaf $source) + ".o")
    & $ArmGcc -std=c11 -Wall -Wextra -Werror "-I$refmemInclude" -c $source -o $object
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host "refmem_param_pool tests compiled with ARM GCC; host execution skipped because no host C compiler was found"
