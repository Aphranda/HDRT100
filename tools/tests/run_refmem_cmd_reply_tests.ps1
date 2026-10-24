param(
    [string]$BuildDir = "out/pytest/build-refmem-cmd-reply-tests",
    [string]$ArmGcc = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$refmemInclude = Join-Path $repo "components\distributed_refmem\inc"
$tdmaInclude = Join-Path $repo "components\tdma\inc"
$testSource = Join-Path $repo "tests\unit\test_refmem_cmd_reply.c"
$replySource = Join-Path $repo "components\distributed_refmem\src\refmem_cmd_reply.c"

New-Item -ItemType Directory -Force -Path $build | Out-Null

function Get-ToolPath {
    param([string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    return $null
}

function Find-ArmGcc {
    $candidateHomes = @()
    if ($env:USERPROFILE) {
        $candidateHomes += $env:USERPROFILE
    }
    $dotNetHome = [Environment]::GetFolderPath("UserProfile")
    if ($dotNetHome) {
        $candidateHomes += $dotNetHome
    }

    foreach ($candidateHome in ($candidateHomes | Select-Object -Unique)) {
        $candidate = Join-Path $candidateHome ".pico-sdk\toolchain\14_2_Rel1\bin\arm-none-eabi-gcc.exe"
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    return ""
}

$hostCc = Get-ToolPath "gcc"
if (-not $hostCc) {
    $hostCc = Get-ToolPath "clang"
}

if ($hostCc) {
    $exe = Join-Path $build "test_refmem_cmd_reply.exe"
    & $hostCc -std=c11 -Wall -Wextra -Werror "-I$refmemInclude" "-I$tdmaInclude" $testSource $replySource -o $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    & $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Write-Host "refmem_cmd_reply host unit tests passed"
    exit 0
}

if (-not $ArmGcc) {
    $ArmGcc = Find-ArmGcc
}

if (-not (Test-Path $ArmGcc)) {
    throw "No host C compiler found and ARM GCC not found at $ArmGcc"
}

foreach ($source in @($testSource, $replySource)) {
    $object = Join-Path $build ((Split-Path -Leaf $source) + ".o")
    & $ArmGcc -std=c11 -Wall -Wextra -Werror "-I$refmemInclude" "-I$tdmaInclude" -c $source -o $object
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host "refmem_cmd_reply tests compiled with ARM GCC; host execution skipped because no host C compiler was found"
