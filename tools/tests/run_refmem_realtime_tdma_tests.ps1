param(
    [string]$BuildDir = "build-refmem-realtime-tdma-tests",
    [string]$ArmGcc = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$refmemInclude = Join-Path $repo "components\distributed_refmem\inc"
$tdmaInclude = Join-Path $repo "components\tdma\inc"
$testSource = Join-Path $repo "tests\unit\test_refmem_realtime_tdma.c"
$serviceSource = Join-Path $repo "components\distributed_refmem\src\refmem_realtime_tdma.c"
$payloadSource = Join-Path $repo "components\distributed_refmem\src\refmem_tdma_payload.c"
$tdmaSource = Join-Path $repo "components\tdma\src\tdma_service.c"
$tdmaProfileSource = Join-Path $repo "components\tdma\src\tdma_profile.c"
$tdmaRegistrySource = Join-Path $repo "components\tdma\src\tdma_payload_registry.c"
$tdmaRingSource = Join-Path $repo "components\tdma\src\tdma_ring_runtime.c"
$tdmaSchedulerSource = Join-Path $repo "components\tdma\src\tdma_traffic_scheduler.c"

New-Item -ItemType Directory -Force -Path $build | Out-Null

function Get-ToolPath {
    param([string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    return $null
}

function Find-ArmGcc {
    if ($ArmGcc -and (Test-Path $ArmGcc)) {
        return $ArmGcc
    }
    $candidate = "D:\Embedded\GCC\mingw64\bin\arm-none-eabi-gcc.exe"
    if (Test-Path $candidate) {
        return $candidate
    }
    if ($env:USERPROFILE) {
        $picoCandidate = Join-Path $env:USERPROFILE ".pico-sdk\toolchain\14_2_Rel1\bin\arm-none-eabi-gcc.exe"
        if (Test-Path $picoCandidate) {
            return $picoCandidate
        }
    }
    return ""
}

$hostCc = Get-ToolPath "gcc"
if (-not $hostCc) {
    $hostCc = Get-ToolPath "clang"
}

if ($hostCc) {
    $exe = Join-Path $build "test_refmem_realtime_tdma.exe"
    & $hostCc -std=c11 -Wall -Wextra -Werror "-I$refmemInclude" "-I$tdmaInclude" $testSource $serviceSource $payloadSource $tdmaSource $tdmaProfileSource $tdmaRegistrySource $tdmaRingSource $tdmaSchedulerSource -o $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    & $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Write-Host "refmem_realtime_tdma host unit tests passed"
    exit 0
}

$compiler = Find-ArmGcc
if (-not (Test-Path $compiler)) {
    throw "No host C compiler found and ARM GCC not found"
}

foreach ($source in @($testSource, $serviceSource, $payloadSource, $tdmaSource, $tdmaProfileSource, $tdmaRegistrySource, $tdmaRingSource, $tdmaSchedulerSource)) {
    $object = Join-Path $build ((Split-Path -Leaf $source) + ".o")
    & $compiler -std=c11 -Wall -Wextra -Werror "-I$refmemInclude" "-I$tdmaInclude" -c $source -o $object
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host "refmem_realtime_tdma tests compiled with ARM GCC; host execution skipped because no host C compiler was found"
