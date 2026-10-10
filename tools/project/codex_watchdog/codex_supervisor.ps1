# codex_supervisor.ps1
#
# Drives a long-running Codex task to completion, auto-resuming the SAME
# session when Codex exits or stalls (e.g. transient network issues).
#
# Flow:
#   attempt 1 : codex exec  "<TaskPrompt>"
#   on fail   : codex resume --last --include-non-interactive "<ContinuePrompt>"
#               (continue the same session, preserve progress/context)
#   ... repeat up to MaxRetries, with BackoffSeconds between attempts.
#
# Prompt sources:
#   -TaskPrompt   inline prompt for the first attempt
#   -PromptFile   UTF-8 text file; its content is used for BOTH the first
#                 attempt and every resume (recommended for long specs).
#                 Newlines are flattened to spaces for safe command-line pass.
#
# Failure is declared when:
#   - the codex process exits with a non-zero exit code, or
#   - the newest rollout-*.jsonl has not grown for StallMinutes while the
#     process is still running (network hang) -> the process is killed.
#
# A clean exit code 0 is treated as "task finished" and the supervisor stops.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\codex_watchdog\codex_supervisor.ps1 `
#       -PromptFile tools\codex_watchdog\dpll_task_prompt.md

[CmdletBinding()]
param(
    [string]$TaskPrompt = '',

    [string]$PromptFile = '',

    [string]$ContinuePrompt = 'Continue the task. First read the latest state and the steps already completed; do not redo finished work; then proceed directly.',

    [string]$CodexExe = (Join-Path $env:USERPROFILE '.codex\.sandbox-bin\codex.exe'),
    [int]$MaxRetries = 10,
    [int]$BackoffSeconds = 15,
    [int]$StallMinutes = 10,
    [int]$PollSeconds = 10,

    [string]$SessionsRoot = (Join-Path $env:USERPROFILE '.codex\sessions'),

    [string]$LogFile = '',
    [string]$OutDir  = ''
)

if (-not $LogFile) { $LogFile = Join-Path $PSScriptRoot 'supervisor.log' }
if (-not $OutDir)  { $OutDir  = Join-Path $PSScriptRoot 'attempts' }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

$ErrorActionPreference = 'Stop'

function Write-Log([string]$message) {
    $line = '{0:yyyy-MM-dd HH:mm:ss}  {1}' -f (Get-Date), $message
    try { Add-Content -Path $LogFile -Value $line -Encoding UTF8 } catch { }
    Write-Output $line
}

function Flatten-Prompt([string]$s) {
    return (($s -replace "`r", ' ' -replace "`n", ' ') -replace '\s+', ' ').Trim()
}

function Quote-Arg([string]$s) {
    return '"' + ($s -replace '"', '\"') + '"'
}

function Get-NewestRolloutMtime {
    $f = Get-ChildItem -Path $SessionsRoot -Recurse -File -Filter 'rollout-*.jsonl' -ErrorAction SilentlyContinue |
         Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $f) { return $null }
    return $f.LastWriteTime
}

# ---- Resolve prompts ----
$spec = ''
if ($PromptFile) {
    if (-not (Test-Path $PromptFile)) {
        Write-Output "FATAL: prompt file not found: $PromptFile"
        exit 2
    }
    $spec = Get-Content -Path $PromptFile -Raw -Encoding UTF8
}
if (-not $TaskPrompt -and $spec) { $TaskPrompt = $spec }
if (-not $TaskPrompt) {
    Write-Output 'FATAL: provide -TaskPrompt or -PromptFile'
    exit 2
}
if ($spec) { $ContinuePrompt = $spec }

$taskFlat     = Flatten-Prompt $TaskPrompt
$continueFlat = Flatten-Prompt $ContinuePrompt

Write-Log "supervisor start exe=$CodexExe max_retries=$MaxRetries stall_min=$StallMinutes prompt_chars=$($taskFlat.Length)"

for ($attempt = 1; $attempt -le ($MaxRetries + 1); $attempt++) {
    $isFirst = ($attempt -eq 1)
    if ($isFirst) {
        $argLine = 'exec ' + (Quote-Arg $taskFlat)
    } else {
        $argLine = 'resume --last --include-non-interactive ' + (Quote-Arg $continueFlat)
    }
    $outFile = Join-Path $OutDir ("attempt_{0}.out.log" -f $attempt)
    $errFile = Join-Path $OutDir ("attempt_{0}.err.log" -f $attempt)

    if (-not (Test-Path $CodexExe)) {
        Write-Log "FATAL: codex not found at $CodexExe"
        exit 2
    }

    Write-Log "attempt=$attempt cmd=start"
    $proc = Start-Process -FilePath $CodexExe -ArgumentList $argLine `
        -RedirectStandardOutput $outFile -RedirectStandardError $errFile `
        -WindowStyle Hidden -PassThru
    $procStart = Get-Date
    $lastRolloutMtime = Get-NewestRolloutMtime
    $lastChangeTime = Get-Date
    Write-Log "attempt=$attempt pid=$($proc.Id)"

    $stalled = $false
    while (-not $proc.HasExited) {
        Start-Sleep -Seconds $PollSeconds
        $m = Get-NewestRolloutMtime
        if ($m -and $m -ne $lastRolloutMtime) {
            $lastRolloutMtime = $m
            $lastChangeTime = Get-Date
        }
        $noGrowthMin = [math]::Round(((Get-Date) - $lastChangeTime).TotalMinutes, 2)
        $runningMin  = [math]::Round(((Get-Date) - $procStart).TotalMinutes, 2)
        if ($runningMin -ge $StallMinutes -and $noGrowthMin -ge $StallMinutes) {
            Write-Log "attempt=$attempt STALL (no rollout growth for ${noGrowthMin}min); killing pid=$($proc.Id)"
            $stalled = $true
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
            break
        }
    }
    $proc.WaitForExit()
    $code = $proc.ExitCode
    Write-Log "attempt=$attempt finished exit_code=$code stalled=$stalled"

    if (-not $stalled -and $code -eq 0) {
        Write-Log "DONE: task finished cleanly on attempt $attempt"
        exit 0
    }

    if ($attempt -le $MaxRetries) {
        Write-Log "attempt=$attempt failed; backoff ${BackoffSeconds}s then resume"
        Start-Sleep -Seconds $BackoffSeconds
    }
}

Write-Log "MAX_RETRIES_REACHED: task did not finish cleanly"
exit 1
