param(
    [string]$BuildDir = "out/pytest/build-tdma-boot-plan-tests",
    [string]$HostGccDir = "D:\Embedded\GCC\mingw64\bin"
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo $BuildDir
New-Item -ItemType Directory -Force -Path $build | Out-Null
$cc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $cc) {
    $candidate = Join-Path $HostGccDir "gcc.exe"
    if (Test-Path $candidate) { $cc = Get-Item $candidate }
}
if (-not $cc) { $cc = Get-Command clang -ErrorAction SilentlyContinue }
if (-not $cc) { throw "A host gcc or clang compiler is required" }

$common = @(
    "-std=c11", "-Wall", "-Wextra", "-Werror",
    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
    "-I$repo\components\tdma\inc",
    "-I$repo\components\algo\inc",
    "-I$repo\components\flash_store\inc",
    "-I$repo\config"
)

$cases = @(
    @{ Name = "tdma_boot_profile"; Test = "test_tdma_boot_profile.c";
       Sources = @("components\tdma\src\tdma_boot_profile.c"); Defines = @() },
    @{ Name = "tdma_boot_plan"; Test = "test_tdma_boot_plan.c";
       Sources = @("components\tdma\src\tdma_boot_plan.c",
                   "components\tdma\src\tdma_boot_profile.c"); Defines = @() },
    @{ Name = "tdma_boot_sequence"; Test = "test_tdma_boot_sequence.c";
       Sources = @("components\tdma\src\tdma_boot_sequence.c",
                   "components\tdma\src\tdma_boot_plan.c",
                   "components\tdma\src\tdma_boot_profile.c"); Defines = @() },
    @{ Name = "tdma_boot_stage"; Test = "test_tdma_boot_stage.c";
       Sources = @("components\tdma\src\tdma_boot_stage.c"); Defines = @() },
    @{ Name = "boot_profile_region_v2"; Test = "test_flash_tdma_boot_profile_region.c";
       Sources = @(); Defines = @("-DPROJECT_FLASH_DEPLOYMENT_V2=1") },
    @{ Name = "boot_profile_region_v1"; Test = "test_flash_tdma_boot_profile_region.c";
       Sources = @(); Defines = @() }
)

foreach ($case in $cases) {
    $exe = Join-Path $build ("test_" + $case.Name + ".exe")
    $args = $common + $case.Defines + @($(Join-Path $repo ("tests\unit\" + $case.Test)))
    foreach ($source in $case.Sources) {
        $args += (Join-Path $repo $source)
    }
    $args += @("-o", $exe)
    Write-Host "==> $($case.Name)"
    & $cc.Source @args
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
Write-Host "TDMA boot plan/profile/sequence/stage host unit tests passed"
