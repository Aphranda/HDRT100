param(
    [string]$BuildDir = "out/pytest/build-tdma-traffic-scheduler-tests",
    [string]$HostGccDir = "",
    [string]$ArmGcc = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$include = Join-Path $repo "components\tdma\inc"
$testSource = Join-Path $repo "tests\unit\test_tdma_traffic_scheduler.c"
$schedulerSource = Join-Path $repo "components\tdma\src\tdma_traffic_scheduler.c"
$profileSource = Join-Path $repo "components\tdma\src\tdma_profile.c"

New-Item -ItemType Directory -Force -Path $build | Out-Null

if ($HostGccDir -and (Test-Path $HostGccDir)) {
    $env:PATH = "$HostGccDir;$env:PATH"
}

$hostCc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
if (-not $hostCc) {
    $hostCc = (Get-Command clang -ErrorAction SilentlyContinue).Source
}

if ($hostCc) {
    $exe = Join-Path $build "test_tdma_traffic_scheduler.exe"
    & $hostCc -std=c11 -Wall -Wextra -Werror "-I$include" `
        $testSource $schedulerSource $profileSource -o $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    & $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Write-Host "tdma_traffic_scheduler host unit tests passed"
    exit 0
}

if (-not $ArmGcc) {
    $candidate = Join-Path $env:USERPROFILE ".pico-sdk\toolchain\14_2_Rel1\bin\arm-none-eabi-gcc.exe"
    if (Test-Path $candidate) {
        $ArmGcc = $candidate
    }
}
if (-not $ArmGcc -or -not (Test-Path $ArmGcc)) {
    throw "No host C compiler or ARM GCC found"
}

foreach ($source in @($testSource, $schedulerSource, $profileSource)) {
    $object = Join-Path $build ((Split-Path -Leaf $source) + ".o")
    & $ArmGcc -std=c11 -Wall -Wextra -Werror "-I$include" -c $source -o $object
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host "tdma_traffic_scheduler tests compiled with ARM GCC; host execution skipped"
