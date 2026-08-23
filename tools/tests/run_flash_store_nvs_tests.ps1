param(
    [string]$BuildDir = "out/pytest/build-flash-store-nvs-tests",
    [string]$HostGccDir = "D:\Embedded\GCC\mingw64\bin"
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
New-Item -ItemType Directory -Force -Path $build | Out-Null
$cc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $cc) { $candidate = Join-Path $HostGccDir "gcc.exe"; if (Test-Path $candidate) { $cc = Get-Item $candidate } }
if (-not $cc) { $cc = Get-Command clang -ErrorAction SilentlyContinue }
if (-not $cc) { throw "A host gcc or clang compiler is required" }
$args = @("-std=c11", "-Wall", "-Wextra", "-Werror",
    "-I$repo\components\flash_store\inc",
    "-I$repo\third_party\portable_ota\include",
    "$repo\tests\unit\test_flash_store_nvs.c",
    "$repo\components\flash_store\src\flash_store_nvs.c",
    "$repo\components\flash_store\src\flash_store_record.c",
    "$repo\third_party\portable_ota\src\pota_crc32.c",
    "-o", "$build\test_flash_store_nvs.exe")
& $cc.Source @args
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& "$build\test_flash_store_nvs.exe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Flash store NVS host unit tests passed"
