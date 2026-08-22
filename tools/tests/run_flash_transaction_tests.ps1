param(
    [string]$BuildDir = "build-flash-transaction-tests",
    [string]$HostGccDir = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$testSource = Join-Path $repo "tests\unit\test_flash_transaction.c"
$serviceSource = Join-Path $repo "components\flash_transaction\src\flash_transaction_fb.c"
$publicInclude = Join-Path $repo "components\flash_transaction\inc"
$privateInclude = Join-Path $repo "components\flash_transaction\src"
$diagnosticsInclude = Join-Path $repo "components\diagnostics\inc"
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
    throw "A host gcc or clang compiler is required for FlashTransaction tests"
}

$exe = Join-Path $build "test_flash_transaction.exe"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror `
    "-I$publicInclude" "-I$privateInclude" "-I$diagnosticsInclude" "-I$configInclude" `
    $testSource $serviceSource -o $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
& $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
Write-Host "FlashTransaction host unit tests passed"
