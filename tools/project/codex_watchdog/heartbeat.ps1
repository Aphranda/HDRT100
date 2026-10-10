# heartbeat.ps1 - visible heartbeat window whose payload is the probe status,
# with optional auto-resume on STALLED.
#
# Startup: StartupProbes probes, StartupIntervalSeconds apart (default 3 x 10s).
# Steady : one probe every IntervalSeconds (default 300s).
#
# On STALLED (codex process gone, or rollout idle >= StallMinutes), if
# ResumeOnStall is enabled it runs headless:
#   codex exec resume <session-id> "<PromptFile content>"
# (session-id is parsed from the stalled rollout filename; --last as fallback)
# guarded by WakeCooldownMinutes (no repeated wakes within the cooldown).
#
#   -Once                 probe once and exit (testing)
#   -DryRun               on STALLED, log the intended resume instead of running it
#   -ResumeOnStall        enable auto-resume (default true)
#   -PromptFile           spec text sent on resume (default dpll_task_prompt.md)
#   -CodexExe             standalone codex CLI
#   -StartupProbes / -StartupIntervalSeconds / -IntervalSeconds
#   -StallMinutes / -WakeCooldownMinutes

param(
    [int]$IntervalSeconds = 300,
    [int]$StallMinutes = 5,
    [int]$StartupProbes = 3,
    [int]$StartupIntervalSeconds = 10,
    [int]$WakeCooldownMinutes = 5,
    [bool]$ResumeOnStall = $true,
    [switch]$DryRun,
    [switch]$Once,

    [string]$PromptFile = '',
    [string]$CodexExe = (Join-Path $env:USERPROFILE '.codex\.sandbox-bin\codex.exe')
)

$log = Join-Path $PSScriptRoot 'heartbeat.log'
$stateFile = Join-Path $PSScriptRoot 'heartbeat_state.json'

if (-not $PromptFile) { $PromptFile = Join-Path $PSScriptRoot 'dpll_task_prompt.md' }
$script:promptText = ''
if (Test-Path $PromptFile) { $script:promptText = Get-Content -Path $PromptFile -Raw -Encoding UTF8 }
$script:codexExe = $CodexExe
$script:resumeOnStall = $ResumeOnStall
$script:dryRun = [bool]$DryRun
$script:tick = 0

function Flatten-Prompt([string]$s) {
    return (($s -replace "`r", ' ' -replace "`n", ' ') -replace '\s+', ' ').Trim()
}
function Quote-Arg([string]$s) {
    return '"' + ($s -replace '"', '\"') + '"'
}

function Get-LastResume {
    if (Test-Path $stateFile) {
        try {
            $t = (Get-Content $stateFile -Raw | ConvertFrom-Json).last_resume
            if ($t) { return [datetime]$t }
        } catch { }
    }
    return $null
}
function Set-LastResume([datetime]$d) {
    @{ last_resume = $d.ToString('o') } | ConvertTo-Json | Set-Content -Path $stateFile -Encoding UTF8
}

$script:lastResume = Get-LastResume

function Emit-Probe {
    $script:tick++
    $p = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Name -eq 'codex' }
    $alive = [bool]$p

    $n = Get-ChildItem (Join-Path $env:USERPROFILE '.codex\sessions') -Recurse -File -Filter 'rollout-*.jsonl' -ErrorAction SilentlyContinue |
         Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $idle = if ($n) { [math]::Round(((Get-Date) - $n.LastWriteTime).TotalMinutes, 2) } else { -1 }

    $verdict = 'OK'
    if (-not $alive) { $verdict = 'STALLED' }
    elseif ($idle -ge $StallMinutes) { $verdict = 'STALLED' }

    $action = ''
    if ($verdict -eq 'STALLED') {
        $since = if ($script:lastResume) { [math]::Round(((Get-Date) - $script:lastResume).TotalMinutes, 2) } else { $null }
        if ($script:lastResume -and $since -lt $WakeCooldownMinutes) {
            $action = 'cooldown'
        } elseif (-not $script:resumeOnStall) {
            $action = 'resume-off'
        } elseif ($script:dryRun) {
            $action = 'dry-run-resume'
            $script:lastResume = Get-Date
            Set-LastResume $script:lastResume
        } elseif (-not $script:promptText) {
            $action = 'no-prompt'
        } else {
            $flat = Flatten-Prompt $script:promptText
            $sid = ''
            if ($n -and $n.Name -match 'rollout-.+-([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})\.jsonl') {
                $sid = $Matches[1]
            }
            $target = if ($sid) { $sid } else { '--last' }
            $argLine = 'exec resume ' + $target + ' ' + (Quote-Arg $flat)
            $proc = Start-Process -FilePath $script:codexExe -ArgumentList $argLine -WindowStyle Hidden -PassThru
            $script:lastResume = Get-Date
            Set-LastResume $script:lastResume
            $action = 'resume-pid=' + $proc.Id + ' sid=' + $sid
        }
    }

    $line = '{0:yyyy-MM-dd HH:mm:ss} heartbeat#{1} codex_alive={2} idle_min={3} stall_min={4} verdict={5} action={6} newest={7}' `
        -f (Get-Date), $script:tick, $alive, $idle, $StallMinutes, $verdict, $action, $n.Name
    Write-Host $line
    try { Add-Content -Path $log -Value $line -Encoding UTF8 } catch { }
}

if ($Once) {
    Emit-Probe
    exit 0
}

for ($i = 1; $i -le $StartupProbes; $i++) {
    Emit-Probe
    if ($i -lt $StartupProbes) { Start-Sleep -Seconds $StartupIntervalSeconds }
}

while ($true) {
    Start-Sleep -Seconds $IntervalSeconds
    Emit-Probe
}
