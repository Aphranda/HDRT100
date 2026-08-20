param(
    [string]$BuildDir = "build-tdma-operating-profile-tests",
    [string]$HostGccDir = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$include = Join-Path $repo "components\tdma\inc"
$testSource = Join-Path $repo "tests\unit\test_tdma_operating_profile.c"
$profileSource = Join-Path $repo "components\tdma\src\tdma_operating_profile.c"

New-Item -ItemType Directory -Force -Path $build | Out-Null
if ($HostGccDir -and (Test-Path $HostGccDir)) {
    $env:PATH = "$HostGccDir;$env:PATH"
}
$hostCc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
if (-not $hostCc) {
    $hostCc = (Get-Command clang -ErrorAction SilentlyContinue).Source
}
if (-not $hostCc) {
    throw "No host C compiler found"
}

$exe = Join-Path $build "test_tdma_operating_profile.exe"
& $hostCc -std=c11 -Wall -Wextra -Werror "-I$include" `
    $testSource $profileSource -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "tdma_operating_profile host unit tests passed"
