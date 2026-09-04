param(
    [string]$BuildDir = "out/pytest/build-sync-io-logic-analyzer-tests",
    [string]$HostGccDir = "D:\Microsoft\mingw64\bin"
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$testSource = Join-Path $repo "tests\unit\test_sync_io_logic_analyzer.c"
$serviceSource = Join-Path $repo "components\sync_io\src\sync_io_logic_analyzer.c"
$resourceSource = Join-Path $repo "components\sync_io\src\sync_io_persona_resources.c"
$syncInclude = Join-Path $repo "components\sync_io\inc"
$boardInclude = Join-Path $repo "boards\rp2350_trig\inc"
$hostStubs = Join-Path $repo "tests\unit\host_stubs"
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

$exe = Join-Path $build "test_sync_io_logic_analyzer.exe"
& $hostCcPath -std=c11 -Wall -Wextra -Werror `
    "-I$hostStubs" "-I$boardInclude" "-I$syncInclude" `
    $testSource $serviceSource $resourceSource -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "SYNC_IO logic analyzer contract tests passed"
