param(
    [string]$BuildDir = "out/pytest/build-refmem-sync-tests",
    [string]$ArmGcc = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
$refmemInclude = Join-Path $repo "components\distributed_refmem\inc"
$tdmaInclude = Join-Path $repo "components\tdma\inc"
$otaInclude = Join-Path $repo "components\ota_manager\inc"
$testSource = Join-Path $repo "tests\unit\test_refmem_sync.c"
$syncSource = Join-Path $repo "components\distributed_refmem\src\refmem_sync.c"
$syncFrameSource = Join-Path $repo "components\distributed_refmem\src\refmem_sync_frame.c"

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

    $cacheCompiler = Get-ChildItem -Path $repo `
        -Filter "CMakeCache.txt" `
        -Recurse `
        -ErrorAction SilentlyContinue |
        ForEach-Object {
            Select-String -Path $_.FullName -Pattern "^CMAKE_C_COMPILER:FILEPATH=(.*arm-none-eabi-gcc\.exe)$" -ErrorAction SilentlyContinue
        } |
        Select-Object -First 1
    if ($cacheCompiler -and $cacheCompiler.Matches.Count -gt 0) {
        return $cacheCompiler.Matches[0].Groups[1].Value
    }

    return ""
}

$hostCc = Get-ToolPath "gcc"
if (-not $hostCc) {
    $hostCc = Get-ToolPath "clang"
}

if ($hostCc) {
    $exe = Join-Path $build "test_refmem_sync.exe"
    & $hostCc -std=c11 -Wall -Wextra -Werror "-I$refmemInclude" "-I$tdmaInclude" "-I$otaInclude" $testSource $syncSource $syncFrameSource -o $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    & $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Write-Host "refmem_sync host unit tests passed"
    exit 0
}

if (-not $ArmGcc) {
    $ArmGcc = Find-ArmGcc
}

if (-not (Test-Path $ArmGcc)) {
    throw "No host C compiler found and ARM GCC not found at $ArmGcc"
}

foreach ($source in @($testSource, $syncSource, $syncFrameSource)) {
    $object = Join-Path $build ((Split-Path -Leaf $source) + ".o")
    & $ArmGcc -std=c11 -Wall -Wextra -Werror "-I$refmemInclude" "-I$tdmaInclude" "-I$otaInclude" -c $source -o $object
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host "refmem_sync tests compiled with ARM GCC; host execution skipped because no host C compiler was found"
