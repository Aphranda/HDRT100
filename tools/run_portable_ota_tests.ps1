param(
    [string]$BuildDir = "build-portable-ota-tests",
    [string]$ArmGcc = ""
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
if (-not $ArmGcc) {
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
            $ArmGcc = $candidate
            break
        }
    }
}
if (-not $ArmGcc) {
    $cacheCompiler = Get-ChildItem -Path $repo `
        -Filter "CMakeCache.txt" `
        -Recurse `
        -ErrorAction SilentlyContinue |
        ForEach-Object {
            Select-String -Path $_.FullName -Pattern "^CMAKE_C_COMPILER:FILEPATH=(.*arm-none-eabi-gcc\.exe)$" -ErrorAction SilentlyContinue
        } |
        Select-Object -First 1
    if ($cacheCompiler -and $cacheCompiler.Matches.Count -gt 0) {
        $ArmGcc = $cacheCompiler.Matches[0].Groups[1].Value
    }
}
if (-not (Test-Path $ArmGcc)) {
    $candidate = Get-ChildItem -Path "C:\Users" `
        -Filter "arm-none-eabi-gcc.exe" `
        -Recurse `
        -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like "*\.pico-sdk\toolchain\14_2_Rel1\bin\arm-none-eabi-gcc.exe" } |
        Select-Object -First 1
    if ($candidate) {
        $ArmGcc = $candidate.FullName
    }
}
$build = Join-Path $repo $BuildDir
$include = Join-Path $repo "third_party\portable_ota\include"
$src = Join-Path $repo "third_party\portable_ota\src"
$tests = Join-Path $repo "tests\unit"

New-Item -ItemType Directory -Force -Path $build | Out-Null

$commonSources = @(
    (Join-Path $src "pota_crc32.c"),
    (Join-Path $src "pota_package.c"),
    (Join-Path $src "pota_metadata.c")
)

$testPrograms = @(
    @{
        Name = "test_portable_ota_package"
        Sources = @((Join-Path $tests "test_portable_ota_package.c")) + $commonSources
    },
    @{
        Name = "test_portable_ota_metadata"
        Sources = @((Join-Path $tests "test_portable_ota_metadata.c")) + $commonSources
    },
    @{
        Name = "test_portable_ota_strings"
        Sources = @((Join-Path $tests "test_portable_ota_strings.c")) + @(
            (Join-Path $src "pota_strings.c"),
            (Join-Path $src "pota_compat.c")
        )
    },
    @{
        Name = "test_portable_ota_image"
        Sources = @((Join-Path $tests "test_portable_ota_image.c")) + $commonSources + @(
            (Join-Path $src "pota_image.c")
        )
    },
    @{
        Name = "test_portable_ota_session"
        Sources = @((Join-Path $tests "test_portable_ota_session.c")) + $commonSources + @(
            (Join-Path $src "pota_image.c"),
            (Join-Path $src "pota_core.c"),
            (Join-Path $src "pota_operation.c"),
            (Join-Path $src "pota_session.c")
        )
    },
    @{
        Name = "test_portable_ota_core"
        Sources = @((Join-Path $tests "test_portable_ota_core.c")) + $commonSources + @(
            (Join-Path $src "pota_image.c"),
            (Join-Path $src "pota_core.c"),
            (Join-Path $src "pota_operation.c")
        )
    }
)

function Get-ToolPath {
    param([string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    return $null
}

$hostCc = Get-ToolPath "gcc"
if (-not $hostCc) {
    $hostCc = Get-ToolPath "clang"
}

if ($hostCc) {
    foreach ($program in $testPrograms) {
        $exe = Join-Path $build ($program.Name + ".exe")
        & $hostCc -std=c11 -Wall -Wextra -Werror "-I$include" @($program.Sources) -o $exe
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
        & $exe
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
    Write-Host "portable_ota host unit tests passed"
    exit 0
}

if (-not (Test-Path $ArmGcc)) {
    throw "No host C compiler found and ARM GCC not found at $ArmGcc"
}

foreach ($program in $testPrograms) {
    foreach ($source in $program.Sources) {
        $objectName = ($program.Name + "_" + (Split-Path -Leaf $source) + ".o")
        $object = Join-Path $build $objectName
        & $ArmGcc -std=c11 -Wall -Wextra -Werror "-I$include" -c $source -o $object
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
}

Write-Host "portable_ota tests compiled with ARM GCC; host execution skipped because no host C compiler was found"
