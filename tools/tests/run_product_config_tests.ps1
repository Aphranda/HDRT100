param(
    [string]$BuildDir = "out/pytest/build-product-config-tests",
    [string]$HostGccDir = "D:\Embedded\GCC\mingw64\bin"
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
New-Item -ItemType Directory -Force -Path $build | Out-Null

$cc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $cc) {
    $candidate = Join-Path $HostGccDir "gcc.exe"
    if (Test-Path $candidate) { $cc = Get-Item $candidate }
}
if (-not $cc) { $cc = Get-Command clang -ErrorAction SilentlyContinue }
if (-not $cc) { throw "A host gcc or clang compiler is required" }

$args = @(
    "-std=c11", "-Wall", "-Wextra", "-Werror",
    "-I$repo\components\product_config\inc",
    "-I$repo\components\flash_transaction\inc",
    "-I$repo\components\ota_manager\inc",
    "-I$repo\drivers\mcu\flash\inc",
    "-I$repo\config",
    "$repo\tests\unit\test_product_config.c",
    "$repo\components\product_config\src\product_config.c",
    "-o", "$build\test_product_config.exe"
)
& $cc.Source @args
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& "$build\test_product_config.exe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "product_config host unit tests passed"
