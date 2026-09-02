param(
    [string]$BuildDir = "out/pytest/build-tdma-pio-spi-ring-adapter-tests",
    [string]$HostGccDir = "",
    [string]$ArmGcc = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$include = Join-Path $repo "components\tdma\inc"
$testSource = Join-Path $repo "tests\unit\test_tdma_pio_spi_ring_adapter.c"
$adapterSource = Join-Path $repo "components\tdma\src\tdma_pio_spi_ring_adapter.c"
$commFsmSource = Join-Path $repo "components\tdma\src\tdma_adapter_comm_fsm.c"
$flightFifoSource = Join-Path $repo "components\tdma\src\tdma_flight_fifo.c"
$flightEngineSource = Join-Path $repo "components\tdma\src\tdma_flight_engine.c"
$receiveHealthSource = Join-Path $repo "components\tdma\src\tdma_receive_health.c"
$processMapSource = Join-Path $repo "components\tdma\src\tdma_process_image_map.c"
$runtimeSource = Join-Path $repo "components\tdma\src\tdma_ring_runtime.c"
$transportSource = Join-Path $repo "components\tdma\src\tdma_transport_frame.c"
$profileSource = Join-Path $repo "components\tdma\src\tdma_profile.c"

New-Item -ItemType Directory -Force -Path $build | Out-Null

if ($HostGccDir -and (Test-Path $HostGccDir)) {
    $env:PATH = "$HostGccDir;$env:PATH"
}

$hostCc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
if (-not $hostCc) {
    $hostCc = (Get-Command clang -ErrorAction SilentlyContinue).Source
}

if ($hostCc) {
    $exe = Join-Path $build "test_tdma_pio_spi_ring_adapter.exe"
    & $hostCc -std=c11 -Wall -Wextra -Werror "-I$include" `
        $testSource $adapterSource $commFsmSource $flightFifoSource $flightEngineSource $receiveHealthSource $processMapSource $runtimeSource $transportSource $profileSource `
        -o $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    & $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Write-Host "tdma_pio_spi_ring_adapter host unit tests passed"
    exit 0
}

if (-not $ArmGcc) {
    $candidate = Join-Path $env:USERPROFILE ".pico-sdk\toolchain\14_2_Rel1\bin\arm-none-eabi-gcc.exe"
    if (Test-Path $candidate) {
        $ArmGcc = $candidate
    }
}
if (-not $ArmGcc -or -not (Test-Path $ArmGcc)) {
    throw "No host C compiler or ARM GCC found"
}

foreach ($source in @($testSource, $adapterSource, $commFsmSource, $flightFifoSource, $flightEngineSource, $receiveHealthSource, $processMapSource, $runtimeSource, $transportSource, $profileSource)) {
    $object = Join-Path $build ((Split-Path -Leaf $source) + ".o")
    & $ArmGcc -std=c11 -Wall -Wextra -Werror "-I$include" -c $source -o $object
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host "tdma_pio_spi_ring_adapter tests compiled with ARM GCC; host execution skipped"
