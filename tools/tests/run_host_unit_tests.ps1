param(
    [string]$HostGccDir = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$hostCc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $hostCc -and $HostGccDir) {
    $gcc = Join-Path $HostGccDir "gcc.exe"
    if (Test-Path $gcc) {
        $env:PATH = "$HostGccDir;$env:PATH"
        $hostCc = Get-Command gcc -ErrorAction SilentlyContinue
    }
}
if (-not $hostCc) {
    throw "Host GCC not found in PATH; pass -HostGccDir to override"
}

# Several RefMem/TDMA host tests include distributed_refmem.h, which pulls in
# trigger_position_descriptor.h from components\sync_trigger\inc.  The single
# runners do not add that directory, so provide it through the compiler search
# path for the whole aggregate run (same approach as run_refmem_all_host_tests).
$triggerInclude = Join-Path $repo "components\sync_trigger\inc"
$oldIncludePath = $env:C_INCLUDE_PATH
if ($oldIncludePath) {
    $env:C_INCLUDE_PATH = "$triggerInclude;$oldIncludePath"
} else {
    $env:C_INCLUDE_PATH = $triggerInclude
}

$scripts = @(
    "run_biss_protocol_tests.ps1",
    "run_calibration_training_store_tests.ps1",
    "run_drv_flash_geometry_tests.ps1",
    "run_flash_map_tests.ps1",
    "run_flash_transaction_tests.ps1",
    "run_drv_flash_lockout_tests.ps1",
    "run_portable_log_tests.ps1",
    "run_portable_ota_tests.ps1",
    "run_ota_journal_tests.ps1",
    "run_ota_progress_watch_tests.ps1",
    "run_ota_tdma_codec_tests.ps1",
    "run_ota_tdma_ingress_tests.ps1",
    "run_ota_tdma_pump_tests.ps1",
    "run_ota_tdma_bridge_tests.ps1",
    "run_ota_tdma_v2_adapter_tests.ps1",
    "run_ota_tdma_loopback_tests.ps1",
    "run_ota_tdma_session_tests.ps1",
    "run_ota_tdma_carrier_tests.ps1",
    "run_ota_tdma_binding_tests.ps1",
    "run_ota_tdma_relay_tests.ps1",
    "run_refmem_application_contract_tests.ps1",
    "run_refmem_command_tests.ps1",
    "run_refmem_read_catalog_tests.ps1",
    "run_refmem_node_load_sync_tests.ps1",
    "run_refmem_pio_spi_adapter_tests.ps1",
    "run_refmem_quality_tests.ps1",
    "run_refmem_realtime_tdma_tests.ps1",
    "run_refmem_realtime_contract_tests.ps1",
    "run_resource_arbiter_tests.ps1",
    "run_tdma_profile_tests.ps1",
    "run_tdma_boot_profile_store_tests.ps1",
    "run_tdma_boot_plan_tests.ps1",
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
    "run_refmem_vdc_vector_tests.ps1",
    "run_tdma_pio_spi_persona_fsm_tests.ps1",
    "run_tdma_pio_spi_ring_adapter_tests.ps1",
    "run_sma_cable_delay_tests.ps1",
    "run_sync_io_logic_analyzer_tests.ps1",
    "run_vdc_time_mapping_tests.ps1",
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

if ($oldIncludePath) {
    $env:C_INCLUDE_PATH = $oldIncludePath
} else {
    Remove-Item Env:C_INCLUDE_PATH -ErrorAction SilentlyContinue
}

Write-Host "host unit test scripts passed: $passed/$($scripts.Count)"
Write-Host "host gcc: $($hostCc.Source)"
