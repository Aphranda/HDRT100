param(
    [string]$HostGccDir = "D:\Embedded\GCC\mingw64\bin"
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$gcc = Join-Path $HostGccDir "gcc.exe"
if (-not (Test-Path $gcc)) {
    throw "Host GCC not found: $gcc"
}

$env:PATH = "$HostGccDir;$env:PATH"

$scripts = @(
    "run_biss_protocol_tests.ps1",
    "run_drv_flash_geometry_tests.ps1",
    "run_flash_map_tests.ps1",
    "run_flash_transaction_tests.ps1",
    "run_drv_flash_lockout_tests.ps1",
    "run_portable_log_tests.ps1",
    "run_portable_ota_tests.ps1",
    "run_refmem_application_contract_tests.ps1",
    "run_refmem_command_tests.ps1",
    "run_refmem_node_load_sync_tests.ps1",
    "run_refmem_pio_spi_adapter_tests.ps1",
    "run_refmem_quality_tests.ps1",
    "run_refmem_realtime_tdma_tests.ps1",
    "run_refmem_realtime_contract_tests.ps1",
    "run_tdma_profile_tests.ps1",
    "run_tdma_payload_registry_tests.ps1",
    "run_tdma_process_image_map_tests.ps1",
    "run_tdma_flight_fifo_tests.ps1",
    "run_tdma_ring_runtime_tests.ps1",
    "run_tdma_traffic_scheduler_tests.ps1",
    "run_tdma_service_scheduler_tests.ps1",
    "run_tdma_transport_frame_tests.ps1",
    "run_refmem_slot_claim_tests.ps1",
    "run_refmem_sync_frame_tests.ps1",
    "run_refmem_sync_hello_tests.ps1",
    "run_refmem_sync_tests.ps1",
    "run_refmem_table_registry_tests.ps1",
    "run_refmem_vdc_bridge_tests.ps1",
    "run_tdma_pio_spi_ring_adapter_tests.ps1",
    "run_vdc_domain_tests.ps1"
)

$passed = 0
foreach ($script in $scripts) {
    $path = Join-Path $PSScriptRoot $script
    Write-Host "==> $script"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $path
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    $passed++
}

Write-Host "host unit test scripts passed: $passed/$($scripts.Count)"
Write-Host "host gcc: $gcc"
