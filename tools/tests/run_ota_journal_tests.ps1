param(
    [string]$BuildDir = "build-ota-journal-tests",
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
    throw "A host gcc or clang compiler is required for OTA journal tests"
}

$exe = Join-Path $build "test_ota_journal.exe"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror `
    -DPROJECT_FLASH_DEPLOYMENT_V2=1 `
    "-I$(Join-Path $repo 'components\ota_manager\inc')" `
    "-I$(Join-Path $repo 'components\flash_transaction\inc')" `
    "-I$(Join-Path $repo 'drivers\mcu\flash\inc')" `
    "-I$(Join-Path $repo 'third_party\portable_ota\include')" `
    "-I$(Join-Path $repo 'config')" `
    (Join-Path $repo "tests\unit\test_ota_journal.c") `
    (Join-Path $repo "components\ota_manager\src\ota_journal.c") `
    (Join-Path $repo "third_party\portable_ota\src\pota_stream_checkpoint.c") `
    (Join-Path $repo "third_party\portable_ota\src\pota_crc32.c") `
    -o $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
& $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
Write-Host "OTA journal adapter host unit tests passed"
