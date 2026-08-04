param(
    [string]$BuildDir = "build-portable-ota-tests",
    [string]$ArmGcc = ""
)

& "$PSScriptRoot\tests\run_portable_ota_tests.ps1" -BuildDir $BuildDir -ArmGcc $ArmGcc
exit $LASTEXITCODE
