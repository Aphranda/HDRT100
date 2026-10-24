param(
    [string]$BuildDir = "out/pytest/build-refmem-lease-tests",
    [string]$ArmGcc = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$refmemInclude = Join-Path $repo "components\distributed_refmem\inc"
$testSource = Join-Path $repo "tests\unit\test_refmem_lease.c"
$leaseSource = Join-Path $repo "components\distributed_refmem\src\refmem_lease.c"

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
    $exe = Join-Path $build "test_refmem_lease.exe"
    & $hostCc -std=c11 -Wall -Wextra -Werror "-I$refmemInclude" $testSource $leaseSource -o $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    & $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Write-Host "refmem_lease host unit tests passed"
    exit 0
}

if (-not $ArmGcc) {
    $candidate = Join-Path $env:USERPROFILE ".pico-sdk\toolchain\14_2_Rel1\bin\arm-none-eabi-gcc.exe"
    if (Test-Path $candidate) {
        $ArmGcc = $candidate
    }
}

if (-not (Test-Path $ArmGcc)) {
    throw "No host C compiler found and ARM GCC not found at $ArmGcc"
}

$object = Join-Path $build "test_refmem_lease.o"
& $ArmGcc -std=c11 -Wall -Wextra -Werror "-I$refmemInclude" -c $testSource -o $object
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
$leaseObject = Join-Path $build "refmem_lease.o"
& $ArmGcc -std=c11 -Wall -Wextra -Werror "-I$refmemInclude" -c $leaseSource -o $leaseObject
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "refmem_lease tests compiled with ARM GCC; host execution skipped"
