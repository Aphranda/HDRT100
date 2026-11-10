#Requires -Version 5.1
<#
.SYNOPSIS
    一键实机验收 DHRT100 C#/WPF 序列触发调试工具（--probe / --budget / --bench）。

.DESCRIPTION
    按顺序跑：设备身份探测 → 预算读取 → 三个模式的 配置→启动→读回→停止。
    每一步用 Start-Process + 单条参数串调用（PowerShell 会把 -ArgumentList 数组
    按空格拆分，带空格的 SCPI 命令必须整体加引号），失败时打印日志尾部。

.EXAMPLE
    .\verify-bench.ps1 -Resource COM4

.EXAMPLE
    .\verify-bench.ps1 -Resource COM4 -Only independent -Build -Test
#>
param(
    [string]$Exe = (Join-Path $PSScriptRoot 'src\DhrT100.SequenceDebug.App\bin\Debug\net8.0-windows\DhrT100.SequenceDebug.exe'),
    [string]$Resource = 'COM4',
    [ValidateSet('all', 'probe', 'budget', 'independent', 'rj45', 'turntable')]
    [string]$Only = 'all',
    [switch]$Build,
    [switch]$Test,
    [int]$TailLines = 12
)

$ErrorActionPreference = 'Stop'
$script:results = New-Object System.Collections.ArrayList
$scriptTemp = Join-Path $env:TEMP ("dhrt100-verify-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $scriptTemp -Force | Out-Null

function Invoke-Step {
    param([string]$Name, [string]$ArgumentLine)

    if (-not (Test-Path -LiteralPath $Exe)) {
        throw "找不到可执行文件：$Exe（先去掉 -Build 之外的方式编译，或用 -Exe 指定）"
    }

    $out = Join-Path $scriptTemp "$Name.out.txt"
    $err = Join-Path $scriptTemp "$Name.err.txt"
    $process = Start-Process -FilePath $Exe -ArgumentList $ArgumentLine -PassThru -Wait `
        -RedirectStandardOutput $out -RedirectStandardError $err -WindowStyle Hidden

    $stdout = @()
    if (Test-Path -LiteralPath $out) { $stdout = @(Get-Content -LiteralPath $out -Encoding UTF8) }
    $stderr = @()
    if (Test-Path -LiteralPath $err) { $stderr = @(Get-Content -LiteralPath $err -Encoding UTF8) }

    $timeouts = @($stdout | Where-Object { $_ -match '=> <timeout>' }).Count
    $passed = ($process.ExitCode -eq 0) -and ($timeouts -eq 0)

    [void]$script:results.Add([pscustomobject]@{
        Step     = $Name
        ExitCode = $process.ExitCode
        Timeouts = $timeouts
        Passed   = $passed
        Log      = $out
    })

    $mark = 'PASS'
    if (-not $passed) { $mark = 'FAIL' }
    Write-Host ("[{0}] {1,-14} exit={2} timeouts={3}" -f $mark, $Name, $process.ExitCode, $timeouts)

    if (-not $passed) {
        foreach ($line in $stderr) { Write-Host "    stderr: $line" }
        $tail = @($stdout | Select-Object -Last $TailLines)
        foreach ($line in $tail) { Write-Host "    | $line" }
    }
    return $passed
}

function Invoke-Dotnet {
    param([string]$Name, [string[]]$Arguments)
    Write-Host "== $Name =="
    & dotnet @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Name 失败（exit $LASTEXITCODE）" }
}

Write-Host "exe      : $Exe"
Write-Host "resource : $Resource"
Write-Host "log dir  : $scriptTemp"
Write-Host ""

if ($Build) {
    Invoke-Dotnet 'dotnet build' @('build', (Join-Path $PSScriptRoot 'SequenceTriggerDebugWpf.sln'), '-v', 'q', '--nologo')
}
if ($Test) {
    Invoke-Dotnet 'dotnet test' @('test',
        (Join-Path $PSScriptRoot 'tests\DhrT100.SequenceDebug.Tests\DhrT100.SequenceDebug.Tests.csproj'),
        '--nologo', '-v', 'q', '-p:MSBuildEnableWorkloadResolver=false')
}

$wanted = @()
if ($Only -eq 'all') { $wanted = @('probe', 'budget', 'independent', 'rj45', 'turntable') }
else { $wanted = @($Only) }

foreach ($step in $wanted) {
    switch ($step) {
        'probe' {
            Invoke-Step 'probe' ('--probe Serial "{0}"' -f $Resource) | Out-Null
        }
        'budget' {
            Invoke-Step 'budget' ('--budget Serial "{0}"' -f $Resource) | Out-Null
        }
        default {
            Invoke-Step "configure-$step" ('--configure {0} --resource "{1}" --no-settings' -f $step, $Resource) | Out-Null
            Invoke-Step "bench-$step" ('--bench {0} --resource "{1}" --no-settings' -f $step, $Resource) | Out-Null
        }
    }
}

Write-Host ""
Write-Host "== summary =="
$script:results | Format-Table -AutoSize Step, ExitCode, Timeouts, Passed, Log

$failed = @($script:results | Where-Object { -not $_.Passed })
if ($failed.Count -gt 0) {
    Write-Host ("{0} 步失败；日志目录：{1}" -f $failed.Count, $scriptTemp)
    exit 1
}
Write-Host "全部通过。"
exit 0
