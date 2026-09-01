param(
    [string]$BuildDir = "out/pytest/build-tdma-persona-fsm-tests",
    [string]$HostGccDir = "D:\Microsoft\mingw64\bin"
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$testSource = Join-Path $repo "tests\unit\test_tdma_pio_spi_persona_fsm.c"
$serviceSource = Join-Path $repo "components\tdma\src\tdma_pio_spi_persona_fsm.c"
$include = Join-Path $repo "components\tdma\inc"
New-Item -ItemType Directory -Force -Path $build | Out-Null

$hostCc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $hostCc) {
    $candidate = Join-Path $HostGccDir "gcc.exe"
    if (Test-Path $candidate) { $hostCc = Get-Item $candidate }
}
if (-not $hostCc) { $hostCc = Get-Command clang -ErrorAction SilentlyContinue }
if (-not $hostCc) { throw "A host gcc or clang compiler is required" }

$hostCcPath = if ($hostCc.PSObject.Properties.Name -contains 'Source') {
    $hostCc.Source
} else {
    $hostCc.FullName
}

$exe = Join-Path $build "test_tdma_pio_spi_persona_fsm.exe"
Push-Location $repo
try {
    & $hostCcPath -std=c11 -Wall -Wextra -Werror `
        "-Icomponents\tdma\inc" "tests\unit\test_tdma_pio_spi_persona_fsm.c" `
        "components\tdma\src\tdma_pio_spi_persona_fsm.c" `
        -o "out\pytest\build-tdma-persona-fsm-tests\test_tdma_pio_spi_persona_fsm.exe"
} finally {
    Pop-Location
}
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "TDMA persona FSM host unit tests passed"
