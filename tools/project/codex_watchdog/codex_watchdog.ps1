# codex_watchdog.ps1
#
# Periodically detect whether the Codex (VSCode extension) session has stalled
# or died (e.g. transient network issues), and wake it according to WakeMode.
#
# Detection signals (DetectMode):
#   Rollout  - newest rollout-*.jsonl has not grown for IdleThresholdMinutes
#              (covers both "process alive but network-stuck" and "crashed")
#   Process  - codex / codex-code-mode-host process is gone
#   Both     - either condition triggers (default)
#
# Wake actions (WakeMode):
#   Log      - only write log + state (safe default; confirm manually first)
#   Exec     - start a NEW non-interactive session with the standalone CLI:
#              codex exec "<ResumePrompt>"
#   Resume   - continue the most recent session:
#              codex resume --last --include-non-interactive "<ResumePrompt>"
#              (NOTE: loads the existing (possibly huge) session; can be slow
#               or conflict with the still-open extension session. Use with care.)
#
# Debounce: a given (file + mtime) triggers once, and wakes are at least
# WakeCooldownMinutes apart.
#
# Typical usage (run every 5 minutes from Task Scheduler):
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\codex_watchdog\codex_watchdog.ps1 `
#       -DetectMode Both -IdleThresholdMinutes 10 -WakeMode Log
#
# Register (current user):
#   schtasks /Create /F /SC MINUTE /MO 5 /TN "CodexWatchdog" `
#     /TR "powershell -NoProfile -ExecutionPolicy Bypass -File \"<repo>\tools\codex_watchdog\codex_watchdog.ps1\" -WakeMode Exec -ResumePrompt 'Continue converging the DPLL oscillation; read the latest state first.'"

[CmdletBinding()]
param(
    [ValidateSet('Rollout', 'Process', 'Both')]
    [string]$DetectMode = 'Both',

    [ValidateSet('Log', 'Exec', 'Resume')]
    [string]$WakeMode = 'Log',

    [int]$IdleThresholdMinutes = 10,
    [int]$WakeCooldownMinutes = 5,

    [string]$SessionsRoot = (Join-Path $env:USERPROFILE '.codex\sessions'),
    [string]$CodexExe    = (Join-Path $env:USERPROFILE '.codex\.sandbox-bin\codex.exe'),

    [string]$ResumePrompt = 'Continue the task: converge the DPLL oscillation as soon as possible. Read the latest state and unfinished steps first, do not redo completed work, then continue directly.',

    # UTF-8 prompt file whose content is sent on wake (overrides ResumePrompt)
    [string]$PromptFile = '',

    # Only watch rollout files matching this id (empty = newest globally)
    [string]$SessionId = '',

    [string]$StateFile = '',
    [string]$LogFile   = ''
)

if (-not $StateFile) { $StateFile = Join-Path $PSScriptRoot 'state.json' }
if (-not $LogFile)   { $LogFile   = Join-Path $PSScriptRoot 'watchdog.log' }

$ErrorActionPreference = 'Stop'

function Write-Log([string]$message) {
    $line = '{0:yyyy-MM-dd HH:mm:ss}  {1}' -f (Get-Date), $message
    try { Add-Content -Path $LogFile -Value $line -Encoding UTF8 } catch { }
    Write-Output $line
}

# ---- Prompt resolution ----
$spec = ''
if ($PromptFile) {
    if (-not (Test-Path $PromptFile)) { Write-Output "FATAL: prompt file not found: $PromptFile"; exit 2 }
    $spec = Get-Content -Path $PromptFile -Raw -Encoding UTF8
}
if ($spec) { $ResumePrompt = $spec }

function Flatten-Prompt([string]$s) {
    return (($s -replace "`r", ' ' -replace "`n", ' ') -replace '\s+', ' ').Trim()
}
function Quote-Arg([string]$s) {
    return '"' + ($s -replace '"', '\"') + '"'
}
$resumeFlat = Flatten-Prompt $ResumePrompt

function Read-State {
    if (Test-Path $StateFile) {
        try { return (Get-Content $StateFile -Raw | ConvertFrom-Json) } catch { }
    }
    return [pscustomobject]@{ last_seen_mtime = $null; last_seen_file = $null; last_wake = $null }
}

function Write-State([pscustomobject]$state) {
    $state | ConvertTo-Json | Set-Content -Path $StateFile -Encoding UTF8
}

function Get-NewestRollout {
    $files = Get-ChildItem -Path $SessionsRoot -Recurse -File -Filter 'rollout-*.jsonl' -ErrorAction SilentlyContinue
    if ($SessionId) {
        $files = $files | Where-Object { $_.FullName -match $SessionId }
    }
    if (-not $files) { return $null }
    return ($files | Sort-Object LastWriteTime -Descending | Select-Object -First 1)
}

function Test-ProcessAlive {
    $proc = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Name -eq 'codex' }
    return [bool]$proc
}

$state = Read-State
$now   = Get-Date

$processAlive = Test-ProcessAlive

$newest = Get-NewestRollout
$idleMinutes = $null
if ($newest) {
    $idleMinutes = [math]::Round(((Get-Date) - $newest.LastWriteTime).TotalMinutes, 2)
}

$stalled = switch ($DetectMode) {
    'Process' { -not $processAlive }
    'Rollout' { $newest -and $idleMinutes -ge $IdleThresholdMinutes }
    'Both'    { (-not $processAlive) -or ($newest -and $idleMinutes -ge $IdleThresholdMinutes) }
}

if (-not $stalled) {
    if ($newest) {
        $state.last_seen_file  = $newest.FullName
        $state.last_seen_mtime = $newest.LastWriteTime.ToString('o')
        Write-State $state
    }
    Write-Log "OK process_alive=$processAlive idle_min=$idleMinutes newest=$($newest.Name)"
    exit 0
}

$sameStall = ($state.last_seen_file -eq $newest.FullName -and
              $state.last_seen_mtime -eq $newest.LastWriteTime.ToString('o'))
$cooldownOk = $true
if ($state.last_wake) {
    $sinceWake = [math]::Round(((Get-Date) - [datetime]$state.last_wake).TotalMinutes, 2)
    if ($sinceWake -lt $WakeCooldownMinutes) { $cooldownOk = $false }
}
if ($sameStall -or -not $cooldownOk) {
    Write-Log "STALLED(skip) same=$sameStall cooldown_ok=$cooldownOk idle_min=$idleMinutes newest=$($newest.Name)"
    exit 0
}

Write-Log "STALLED process_alive=$processAlive idle_min=$idleMinutes newest=$($newest.Name) wake=$WakeMode"

$state.last_seen_file  = $newest.FullName
$state.last_seen_mtime = $newest.LastWriteTime.ToString('o')
$state.last_wake       = $now.ToString('o')

switch ($WakeMode) {
    'Log' {
        Write-Log "[Log] please continue the session manually (or switch -WakeMode Exec for auto-continue)"
    }
    'Exec' {
        if (-not (Test-Path $CodexExe)) { Write-Log "[Exec] codex not found: $CodexExe"; break }
        $argLine = 'exec ' + (Quote-Arg $resumeFlat)
        $proc = Start-Process -FilePath $CodexExe -ArgumentList $argLine -WindowStyle Hidden -PassThru
        Write-Log "[Exec] started continuation session pid=$($proc.Id) exe=$CodexExe"
    }
    'Resume' {
        if (-not (Test-Path $CodexExe)) { Write-Log "[Resume] codex not found: $CodexExe"; break }
        $argLine = 'resume --last --include-non-interactive ' + (Quote-Arg $resumeFlat)
        $proc = Start-Process -FilePath $CodexExe -ArgumentList $argLine -WindowStyle Hidden -PassThru
        Write-Log "[Resume] resumed last session pid=$($proc.Id) exe=$CodexExe"
    }
}

Write-State $state
exit 0
