param(
    [string]$BuildDir = "build-vdc-domain-tests",
    [string]$HostGccDir = "",
    [string]$ArmGcc = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$vdcInclude = Join-Path $repo "components\vdc_domain\inc"
$tdmaInclude = Join-Path $repo "components\tdma\inc"
$testSource = Join-Path $repo "tests\unit\test_vdc_domain.c"
$vdcSource = Join-Path $repo "components\vdc_domain\src\vdc_domain.c"
$vdcSyncIoAdapterSource = Join-Path $repo "components\vdc_domain\src\vdc_sync_io_adapter.c"
$vdcTdmaPayloadSource = Join-Path $repo "components\vdc_domain\src\vdc_tdma_payload.c"
$vdcTimestampSource = Join-Path $repo "components\vdc_domain\src\vdc_timestamp.c"
$tdmaSource = Join-Path $repo "components\tdma\src\tdma_service.c"
$tdmaFlightSource = Join-Path $repo "components\tdma\src\tdma_flight_fifo.c"
$tdmaFlightEngineSource = Join-Path $repo "components\tdma\src\tdma_flight_engine.c"
$tdmaProcessMapSource = Join-Path $repo "components\tdma\src\tdma_process_image_map.c"
$tdmaProfileSource = Join-Path $repo "components\tdma\src\tdma_profile.c"
$tdmaOperatingProfileSource = Join-Path $repo "components\tdma\src\tdma_operating_profile.c"
$tdmaRegistrySource = Join-Path $repo "components\tdma\src\tdma_payload_registry.c"
$tdmaRingSource = Join-Path $repo "components\tdma\src\tdma_ring_runtime.c"
$tdmaSchedulerSource = Join-Path $repo "components\tdma\src\tdma_traffic_scheduler.c"

New-Item -ItemType Directory -Force -Path $build | Out-Null

if ($HostGccDir -and (Test-Path $HostGccDir)) {
    $env:PATH = "$HostGccDir;$env:PATH"
}

function Get-ToolPath {
    param([string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    return $null
}

function Find-ArmGcc {
    $candidateHomes = @()
    if ($env:USERPROFILE) {
        $candidateHomes += $env:USERPROFILE
    }
    $dotNetHome = [Environment]::GetFolderPath("UserProfile")
    if ($dotNetHome) {
        $candidateHomes += $dotNetHome
    }

    foreach ($candidateHome in ($candidateHomes | Select-Object -Unique)) {
        $candidate = Join-Path $candidateHome ".pico-sdk\toolchain\14_2_Rel1\bin\arm-none-eabi-gcc.exe"
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $cacheCompiler = Get-ChildItem -Path $repo `
        -Filter "CMakeCache.txt" `
        -Recurse `
        -ErrorAction SilentlyContinue |
        ForEach-Object {
            Select-String -Path $_.FullName -Pattern "^CMAKE_C_COMPILER:FILEPATH=(.*arm-none-eabi-gcc\.exe)$" -ErrorAction SilentlyContinue
        } |
        Select-Object -First 1
    if ($cacheCompiler -and $cacheCompiler.Matches.Count -gt 0) {
        return $cacheCompiler.Matches[0].Groups[1].Value
    }

    return ""
}

$hostCc = Get-ToolPath "gcc"
if (-not $hostCc) {
    $hostCc = Get-ToolPath "clang"
}

if ($hostCc) {
    $exe = Join-Path $build "test_vdc_domain.exe"
    & $hostCc -std=c11 -Wall -Wextra -Werror "-I$vdcInclude" "-I$tdmaInclude" $testSource $vdcSource $vdcSyncIoAdapterSource $vdcTdmaPayloadSource $vdcTimestampSource $tdmaSource $tdmaFlightSource $tdmaFlightEngineSource $tdmaProcessMapSource $tdmaProfileSource $tdmaOperatingProfileSource $tdmaRegistrySource $tdmaRingSource $tdmaSchedulerSource -o $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    & $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Write-Host "vdc_domain host unit tests passed"
    exit 0
}

if (-not $ArmGcc) {
    $ArmGcc = Find-ArmGcc
}

if (-not (Test-Path $ArmGcc)) {
    throw "No host C compiler found and ARM GCC not found at $ArmGcc"
}

foreach ($source in @($testSource, $vdcSource, $vdcSyncIoAdapterSource, $vdcTdmaPayloadSource, $vdcTimestampSource, $tdmaSource, $tdmaFlightSource, $tdmaFlightEngineSource, $tdmaProcessMapSource, $tdmaProfileSource, $tdmaOperatingProfileSource, $tdmaRegistrySource, $tdmaRingSource, $tdmaSchedulerSource)) {
    $object = Join-Path $build ((Split-Path -Leaf $source) + ".o")
    & $ArmGcc -std=c11 -Wall -Wextra -Werror "-I$vdcInclude" "-I$tdmaInclude" -c $source -o $object
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host "vdc_domain tests compiled with ARM GCC; host execution skipped because no host C compiler was found"
