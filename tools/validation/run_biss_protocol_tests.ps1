param(
    [string]$BuildDir = "build-biss-protocol-tests",
    [string]$ArmGcc = ""
)

& "$PSScriptRoot\tests\run_biss_protocol_tests.ps1" -BuildDir $BuildDir -ArmGcc $ArmGcc
exit $LASTEXITCODE
