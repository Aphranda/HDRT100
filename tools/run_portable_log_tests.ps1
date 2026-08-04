param(
    [string]$BuildDir = "build-portable-log-tests",
    [string]$ArmGcc = ""
)

& "$PSScriptRoot\tests\run_portable_log_tests.ps1" -BuildDir $BuildDir -ArmGcc $ArmGcc
exit $LASTEXITCODE
