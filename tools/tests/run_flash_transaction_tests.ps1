param(
    [string]$BuildDir = "build-flash-transaction-tests",
    [string]$HostGccDir = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$testSource = Join-Path $repo "tests\unit\test_flash_transaction.c"
$tests = Join-Path $repo "tests\unit"
$journalTestSource = Join-Path $repo "tests\unit\test_flash_transaction_journal.c"
$serviceSource = Join-Path $repo "components\flash_transaction\src\flash_transaction_fb.c"
$journalSource = Join-Path $repo "components\flash_transaction\src\flash_transaction_journal.c"
$publicInclude = Join-Path $repo "components\flash_transaction\inc"
$privateInclude = Join-Path $repo "components\flash_transaction\src"
$diagnosticsInclude = Join-Path $repo "components\diagnostics\inc"
$configInclude = Join-Path $repo "config"

New-Item -ItemType Directory -Force -Path $build | Out-Null

$hostCc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $hostCc) {
    $candidate = Join-Path $HostGccDir "gcc.exe"
    if (Test-Path $candidate) {
        $hostCc = Get-Item $candidate
    }
}
if (-not $hostCc) {
    $hostCc = Get-Command clang -ErrorAction SilentlyContinue
}
if (-not $hostCc) {
    throw "A host gcc or clang compiler is required for FlashTransaction tests"
}

$exe = Join-Path $build "test_flash_transaction.exe"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror `
    "-I$publicInclude" "-I$privateInclude" "-I$diagnosticsInclude" "-I$configInclude" `
    $testSource $serviceSource -o $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
& $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
Write-Host "FlashTransaction host unit tests passed"

$validationExe = Join-Path $build "test_flash_transaction_validation.exe"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror `
    -DPROJECT_ENABLE_FLASH_VALIDATION=1 `
    "-I$publicInclude" "-I$privateInclude" "-I$diagnosticsInclude" "-I$configInclude" `
    $testSource $serviceSource -o $validationExe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
& $validationExe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
Write-Host "FlashTransaction validation host unit tests passed"

$otaJournalExe = Join-Path $build "test_flash_transaction_ota_journal.exe"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror `
    -DPROJECT_FLASH_DEPLOYMENT_V2=1 `
    "-I$publicInclude" "-I$privateInclude" "-I$diagnosticsInclude" "-I$configInclude" `
    (Join-Path $tests "test_flash_transaction_ota_journal.c") $serviceSource -o $otaJournalExe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
& $otaJournalExe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
Write-Host "FlashTransaction OTA journal owner host unit tests passed"

$journalExe = Join-Path $build "test_flash_transaction_journal.exe"
& $hostCc.Source -std=c11 -Wall -Wextra -Werror `
    "-I$publicInclude" "-I$privateInclude" "-I$diagnosticsInclude" "-I$configInclude" `
    $journalTestSource $journalSource -o $journalExe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
& $journalExe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
Write-Host "FlashTransaction journal host unit tests passed"
