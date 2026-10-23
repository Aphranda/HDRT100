$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out = Join-Path $root 'out\tdma_second_class_parser_tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$exe = Join-Path $out 'test_tdma_second_class_parser.exe'
gcc -std=c11 -Wall -Wextra -Werror `
    -I (Join-Path $root 'components\tdma\inc') `
    (Join-Path $root 'tests\unit\test_tdma_second_class_parser.c') `
    (Join-Path $root 'components\tdma\src\tdma_second_class_parser.c') `
    -o $exe
& $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output 'tdma_second_class_parser_tests: PASS'
