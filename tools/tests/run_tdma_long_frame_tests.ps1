$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out = Join-Path $root 'out\tdma_long_frame_tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$exe = Join-Path $out 'test_tdma_long_frame.exe'
gcc -std=c11 -Wall -Wextra -Werror `
    -I (Join-Path $root 'components\tdma\inc') `
    -I (Join-Path $root 'components\distributed_refmem\inc') `
    (Join-Path $root 'tests\unit\test_tdma_long_frame.c') `
    (Join-Path $root 'components\tdma\src\tdma_long_frame.c') `
    -o $exe
& $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output 'tdma_long_frame_tests: PASS'
