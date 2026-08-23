param(
    [string]$BuildDir = "out/pytest/build-calibration-bidirectional-tests",
    [string]$HostGccDir = "",
    [string]$ArmGcc = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$include = Join-Path $repo "components\calibration_manager\inc"
$testSource = Join-Path $repo "tests\unit\test_calibration_bidirectional.c"
$source = Join-Path $repo "components\calibration_manager\src\calibration_bidirectional.c"
$pathSource = Join-Path $repo "components\calibration_manager\src\calibration_path_snapshot.c"

New-Item -ItemType Directory -Force -Path $build | Out-Null

if ($HostGccDir -and (Test-Path $HostGccDir)) {
    $env:PATH = "$HostGccDir;$env:PATH"
}

$hostCc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
if (-not $hostCc) {
    $hostCc = (Get-Command clang -ErrorAction SilentlyContinue).Source
}

if ($hostCc) {
    $exe = Join-Path $build "test_calibration_bidirectional.exe"
    & $hostCc -std=c11 -Wall -Wextra -Werror "-I$include" $testSource $source $pathSource -o $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    & $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Write-Host "calibration_bidirectional host unit tests passed"
    exit 0
}

if (-not $ArmGcc) {
    $candidates = @(
        (Join-Path $env:USERPROFILE ".pico-sdk\toolchain\14_2_Rel1\bin\arm-none-eabi-gcc.exe"),
        "arm-none-eabi-gcc.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            $ArmGcc = $candidate
            break
        }
        $resolved = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($resolved) {
            $ArmGcc = $resolved.Source
            break
        }
    }
}
if (-not $ArmGcc -or -not (Test-Path $ArmGcc)) {
    throw "No host C compiler or ARM GCC found"
}

foreach ($file in @($testSource, $source, $pathSource)) {
    $object = Join-Path $build ((Split-Path -Leaf $file) + ".o")
    & $ArmGcc -std=c11 -Wall -Wextra -Werror "-I$include" -c $file -o $object
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host "calibration_bidirectional tests compiled with ARM GCC; host execution skipped"
