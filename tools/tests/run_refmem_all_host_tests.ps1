param(
    [string]$Filter = "run_refmem_*_tests.ps1"
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$triggerInclude = Join-Path $repo "components\sync_trigger\inc"

# Several RefMem host tests include distributed_refmem.h, which pulls in
# trigger_position_descriptor.h. The individual runners do not add this include
# directory, so provide it through the compiler search path for the aggregate
# host run.
$oldIncludePath = $env:C_INCLUDE_PATH
if ($triggerInclude) {
    if ($oldIncludePath) {
        $env:C_INCLUDE_PATH = "$triggerInclude;$oldIncludePath"
    } else {
        $env:C_INCLUDE_PATH = $triggerInclude
    }
}

$scripts = Get-ChildItem $PSScriptRoot -Filter $Filter |
    Where-Object { $_.Name -ne "run_refmem_all_host_tests.ps1" } |
    Sort-Object Name
$failures = 0
foreach ($script in $scripts) {
    Write-Host "--- $($script.Name) ---"
    & powershell -ExecutionPolicy Bypass -File $script.FullName
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL $($script.Name)"
        $failures++
    } else {
        Write-Host "PASS $($script.Name)"
    }
}

if ($oldIncludePath) {
    $env:C_INCLUDE_PATH = $oldIncludePath
} else {
    Remove-Item Env:C_INCLUDE_PATH -ErrorAction SilentlyContinue
}

if ($failures -ne 0) {
    Write-Host "RefMem host tests: $failures failure(s)"
    exit 1
}
Write-Host "RefMem host tests: all passed"
