# run_hourly.ps1 - hourly watchdog invocation for the DPLL long task.
# The scheduled task points at this short path; all parameters live here.
& (Join-Path $PSScriptRoot 'codex_watchdog.ps1') `
    -DetectMode Both `
    -IdleThresholdMinutes 50 `
    -WakeCooldownMinutes 50 `
    -WakeMode Resume `
    -PromptFile (Join-Path $PSScriptRoot 'dpll_task_prompt.md')
exit $LASTEXITCODE
