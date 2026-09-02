param(
    [string]$BuildDir = "out/pytest/build-tdma-adapter-comm-fsm-tests",
    [string]$HostGccDir = "D:\Microsoft\mingw64\bin"
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
New-Item -ItemType Directory -Force -Path $build | Out-Null

$hostCc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $hostCc) {
    $candidate = Join-Path $HostGccDir "gcc.exe"
    if (Test-Path $candidate) {
        $env:Path = "$HostGccDir;$env:Path"
        $hostCc = Get-Command $candidate -ErrorAction SilentlyContinue
    }
}
if (-not $hostCc) { $hostCc = Get-Command clang -ErrorAction SilentlyContinue }
if (-not $hostCc) { throw "A host gcc or clang compiler is required" }

$hostCcPath = if ($hostCc.PSObject.Properties.Name -contains 'Source') {
    $hostCc.Source
} else {
    $hostCc.FullName
}

$exe = Join-Path $build "test_tdma_adapter_comm_fsm.exe"
Push-Location $repo
try {
    & $hostCcPath -std=c11 -Wall -Wextra -Werror `
        "-Icomponents\tdma\inc" "tests\unit\test_tdma_adapter_comm_fsm.c" `
        "components\tdma\src\tdma_adapter_comm_fsm.c" `
        -o $exe
} finally {
    Pop-Location
}
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "TDMA adapter communication FSM host tests passed"
