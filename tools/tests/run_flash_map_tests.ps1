param(
    [string]$BuildDir = "out/pytest/build-flash-map-tests",
    [string]$HostGccDir = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$testSource = Join-Path $repo "tests\unit\test_flash_map.c"
$serviceSource = Join-Path $repo "middleware\flash_map\src\flash_map.c"
$serviceInclude = Join-Path $repo "middleware\flash_map\inc"
$configInclude = Join-Path $repo "config"

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
    throw "A host gcc or clang compiler is required for FlashMap tests"
}

$exe = Join-Path $build "test_flash_map.exe"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror `
    "-I$serviceInclude" "-I$configInclude" `
    $testSource $serviceSource -o $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
& $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
Write-Host "FlashMap host unit tests passed"
