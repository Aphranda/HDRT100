param(
    [string]$BuildDir = "build-tdma-service-scheduler-tests",
    [string]$HostGccDir = ""
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$include = Join-Path $repo "components\tdma\inc"
New-Item -ItemType Directory -Force -Path $build | Out-Null

if ($HostGccDir -and (Test-Path $HostGccDir)) {
    $env:PATH = "$HostGccDir;$env:PATH"
}
$cc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
if (-not $cc) {
    $cc = (Get-Command clang -ErrorAction SilentlyContinue).Source
}
if (-not $cc) {
    throw "No host C compiler found"
}

$sources = @(
    (Join-Path $repo "tests\unit\test_tdma_service_scheduler.c"),
    (Join-Path $repo "components\tdma\src\tdma_service.c"),
    (Join-Path $repo "components\tdma\src\tdma_profile.c"),
    (Join-Path $repo "components\tdma\src\tdma_payload_registry.c"),
    (Join-Path $repo "components\tdma\src\tdma_flight_fifo.c"),
    (Join-Path $repo "components\tdma\src\tdma_flight_engine.c"),
    (Join-Path $repo "components\tdma\src\tdma_process_image_map.c"),
    (Join-Path $repo "components\tdma\src\tdma_ring_runtime.c"),
    (Join-Path $repo "components\tdma\src\tdma_traffic_scheduler.c")
)
$exe = Join-Path $build "test_tdma_service_scheduler.exe"
& $cc -std=c11 -Wall -Wextra -Werror "-I$include" $sources -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "tdma_service_scheduler host unit tests passed"
