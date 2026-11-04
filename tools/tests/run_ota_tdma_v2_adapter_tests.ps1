param(
    [string]$BuildDir = "out/pytest/build-ota-tdma-v2-adapter-tests",
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
    throw "A host gcc or clang compiler is required for OTA TDMA v2 adapter tests"
}

$exe = Join-Path $build "test_ota_tdma_v2_adapter.exe"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror `
    "-I$(Join-Path $repo 'components\ota_manager\inc')" `
    "-I$(Join-Path $repo 'third_party\portable_ota\include')" `
    (Join-Path $repo "tests\unit\test_ota_tdma_v2_adapter.c") `
    (Join-Path $repo "components\ota_manager\src\ota_tdma_v2_adapter.c") `
    (Join-Path $repo "components\ota_manager\src\ota_tdma_bridge.c") `
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
Write-Host "OTA TDMA v2 adapter host unit tests passed"
