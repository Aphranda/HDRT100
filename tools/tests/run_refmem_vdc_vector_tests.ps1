param(
    [string]$BuildDir = "out/pytest/build-refmem-vdc-vector-tests",
    [string]$HostGccDir = "",
    [string]$ArmGcc = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$refmemInclude = Join-Path $repo "components\distributed_refmem\inc"
$vdcInclude = Join-Path $repo "components\vdc_domain\inc"
$tdmaInclude = Join-Path $repo "components\tdma\inc"
$testSource = Join-Path $repo "tests\unit\test_refmem_vdc_vector.c"
$vectorSource = Join-Path $repo "components\distributed_refmem\src\refmem_vector_table.c"

New-Item -ItemType Directory -Force -Path $build | Out-Null

if ($HostGccDir -and (Test-Path $HostGccDir)) {
    $env:PATH = "$HostGccDir;$env:PATH"
}

$hostCc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
if (-not $hostCc) {
    $hostCc = (Get-Command clang -ErrorAction SilentlyContinue).Source
}

$includes = @("-I$refmemInclude", "-I$vdcInclude", "-I$tdmaInclude")
$sources = @($testSource, $vectorSource)

if ($hostCc) {
    $exe = Join-Path $build "test_refmem_vdc_vector.exe"
    & $hostCc -std=c11 -Wall -Wextra -Werror $includes $sources -o $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    & $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Write-Host "refmem_vdc_vector host unit tests passed"
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

foreach ($source in $sources) {
    $object = Join-Path $build ((Split-Path -Leaf $source) + ".o")
    & $ArmGcc -std=c11 -Wall -Wextra -Werror $includes -c $source -o $object
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host "refmem_vdc_vector tests compiled with ARM GCC; host execution skipped"
