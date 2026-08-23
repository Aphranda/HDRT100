param(
    [string]$BuildDir = "out/pytest/build-flash-store-record-tests",
    [string]$HostGccDir = "D:\Embedded\GCC\mingw64\bin"
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\.." )).Path
$build = Join-Path $repo $BuildDir
$source = Join-Path $repo "tests\unit\test_flash_store_record.c"
$service = Join-Path $repo "components\flash_store\src\flash_store_record.c"
$include = Join-Path $repo "components\flash_store\inc"
$portableInclude = Join-Path $repo "third_party\portable_ota\include"
$crc = Join-Path $repo "third_party\portable_ota\src\pota_crc32.c"
New-Item -ItemType Directory -Force -Path $build | Out-Null

$hostCc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $hostCc) {
    $candidate = Join-Path $HostGccDir "gcc.exe"
    if (Test-Path $candidate) { $hostCc = Get-Item $candidate }
}
if (-not $hostCc) { $hostCc = Get-Command clang -ErrorAction SilentlyContinue }
if (-not $hostCc) { throw "A host gcc or clang compiler is required" }

$exe = Join-Path $build "test_flash_store_record.exe"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror `
    "-I$include" "-I$portableInclude" $source $service $crc -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Flash store record host unit tests passed"
