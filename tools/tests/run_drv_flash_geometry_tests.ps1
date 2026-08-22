param(
    [string]$BuildDir = "build-drv-flash-geometry-tests",
    [string]$HostGccDir = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$driverInclude = Join-Path $repo "drivers\mcu\flash\inc"
$configInclude = Join-Path $repo "config"
$stubInclude = Join-Path $repo "tests\support\flash_hal"
$testSource = Join-Path $repo "tests\unit\test_drv_flash_geometry.c"
$driverSource = Join-Path $repo "drivers\mcu\flash\src\drv_flash.c"

New-Item -ItemType Directory -Force -Path $build | Out-Null

$hostCc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $hostCc) {
    $candidate = Join-Path $HostGccDir "gcc.exe"
    if (Test-Path $candidate) {
        $hostCc = Get-Item $candidate
    }
}
if (-not $hostCc) {
    $hostCc = Get-Command clang -ErrorAction SilentlyContinue
}
if (-not $hostCc) {
    throw "A host gcc or clang compiler is required for drv_flash geometry tests"
}

$exe = Join-Path $build "test_drv_flash_geometry.exe"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror `
    "-I$stubInclude" "-I$driverInclude" "-I$configInclude" `
    -DPROJECT_USE_MULTICORE=0 $testSource $driverSource -o $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
& $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
Write-Host "drv_flash geometry host unit tests passed"
