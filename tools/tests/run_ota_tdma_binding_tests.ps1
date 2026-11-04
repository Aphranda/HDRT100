param(
    [string]$BuildDir = "out/pytest/build-ota-tdma-binding-tests",
    [string]$HostGccDir = ""
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
New-Item -ItemType Directory -Force -Path $build | Out-Null

$hostCc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $hostCc -and $HostGccDir) {
    $candidate = Join-Path $HostGccDir "gcc.exe"
    if (Test-Path $candidate) {
        $hostCc = Get-Item $candidate
    }
}
if (-not $hostCc) {
    $hostCc = Get-Command clang -ErrorAction SilentlyContinue
}
if (-not $hostCc) {
    throw "A host gcc or clang compiler is required for OTA TDMA binding tests"
}

$exe = Join-Path $build "test_ota_tdma_binding.exe"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror -DPROJECT_OTA_TDMA_V2_BRIDGE=0 `
    "-I$(Join-Path $repo 'application\inc')" `
    "-I$(Join-Path $repo 'components\ota_manager\inc')" `
    "-I$(Join-Path $repo 'components\tdma\inc')" `
    "-I$(Join-Path $repo 'components\resource_arbiter\inc')" `
    "-I$(Join-Path $repo 'drivers\mcu\watchdog\inc')" `
    "-I$(Join-Path $repo 'components\tdma\inc')" `
    "-I$(Join-Path $repo 'boards\rp2350_trig\inc')" `
    "-I$(Join-Path $repo 'middleware\portable_ota_port\inc')" `
    "-I$(Join-Path $repo 'third_party\portable_ota\include')" `
    "-I$(Join-Path $repo 'components\flash_transaction\inc')" `
    "-I$(Join-Path $repo 'config')" `
    (Join-Path $repo "tests\unit\test_ota_tdma_binding.c") `
    (Join-Path $repo "application\src\ota_tdma_binding.c") `
    (Join-Path $repo "components\ota_manager\src\ota_tdma_carrier.c") `
    (Join-Path $repo "components\ota_manager\src\ota_tdma_relay.c") `
    (Join-Path $repo "components\ota_manager\src\ota_tdma_session.c") `
    (Join-Path $repo "components\ota_manager\src\ota_tdma_pump.c") `
    (Join-Path $repo "components\ota_manager\src\ota_tdma_ingress.c") `
    (Join-Path $repo "components\ota_manager\src\ota_tdma_codec.c") `
    (Join-Path $repo "third_party\portable_ota\src\pota_stream_wire.c") `
    (Join-Path $repo "third_party\portable_ota\src\pota_crc32.c") `
    -o $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
& $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
Write-Host "OTA TDMA binding host unit tests passed"

