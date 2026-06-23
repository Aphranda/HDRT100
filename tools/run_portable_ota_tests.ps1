param(
    [string]$BuildDir = "build-portable-ota-tests",
    [string]$ArmGcc = "C:\Users\Aphranda\.pico-sdk\toolchain\14_2_Rel1\bin\arm-none-eabi-gcc.exe"
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
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
            (Join-Path $src "pota_strings.c")
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
            (Join-Path $src "pota_session.c")
        )
    },
    @{
        Name = "test_portable_ota_core"
        Sources = @((Join-Path $tests "test_portable_ota_core.c")) + $commonSources + @(
            (Join-Path $src "pota_image.c"),
            (Join-Path $src "pota_core.c")
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
