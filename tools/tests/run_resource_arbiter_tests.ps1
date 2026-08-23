param(
    [string]$BuildDir = "out/pytest/build-resource-arbiter-tests",
    [string]$HostGccDir = "D:\Embedded\GCC\mingw64\bin"
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$testSource = Join-Path $repo "tests\unit\test_resource_arbiter.c"
$serviceSource = Join-Path $repo "components\resource_arbiter\src\resource_arbiter.c"
$include = Join-Path $repo "components\resource_arbiter\inc"
$osalInclude = Join-Path $repo "osal\inc"
New-Item -ItemType Directory -Force -Path $build | Out-Null

$hostCc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $hostCc) {
    $candidate = Join-Path $HostGccDir "gcc.exe"
    if (Test-Path $candidate) { $hostCc = Get-Item $candidate }
}
if (-not $hostCc) { $hostCc = Get-Command clang -ErrorAction SilentlyContinue }
if (-not $hostCc) { throw "A host gcc or clang compiler is required" }

$exe = Join-Path $build "test_resource_arbiter.exe"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror `
    "-I$include" "-I$osalInclude" $testSource $serviceSource -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "ResourceArbiter host unit tests passed"
