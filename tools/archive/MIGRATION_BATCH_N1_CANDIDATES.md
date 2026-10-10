# N1 迁移候选（新鲜度 ≤ 2026-09-05，共 44 项）

状态：**候选，待用户过目**；执行前需重算新鲜度并跳过 24h 内被修改者。
原则：只移动 + 旧路径 wrapper；同批更新引用；*不删除*。

| # | 名称 | 类型 | 最新修改 | 建议目标域 |
|---|---|---|---|---|
| 1 | `check_scpi_usb_namespace.py` | file | 2026-09-01 | `tools/interface/` |
| 2 | `rp2350_tk_toolbox.py` | file | 2026-09-01 | `tools/project/` |
| 3 | `run_biss_protocol_tests.ps1` | file | 2026-09-01 | `tools/communication/` |
| 4 | `run_portable_log_tests.ps1` | file | 2026-09-01 | `tools/project/` |
| 5 | `run_portable_ota_tests.ps1` | file | 2026-09-01 | `tools/ota/` |
| 6 | `artifact_checksum` | dir | 2026-09-01 | `tools/flash/` |
| 7 | `ota_packager` | dir | 2026-09-01 | `tools/ota/` |
| 8 | `flash_lockout_hil_validate` | dir | 2026-09-01 | `tools/flash/` |
| 9 | `flash_park_timeout_hil_validate` | dir | 2026-09-01 | `tools/flash/` |
| 10 | `flash_scratch_validate` | dir | 2026-09-01 | `tools/flash/` |
| 11 | `ota_bin_info` | dir | 2026-09-01 | `tools/ota/` |
| 12 | `picotool_reboot` | dir | 2026-09-01 | `tools/flash/` |
| 13 | `uf2_join` | dir | 2026-09-01 | `tools/ota/` |
| 14 | `factory_package` | dir | 2026-09-01 | `tools/ota/` |
| 15 | `factory_restore` | dir | 2026-09-01 | `tools/ota/` |
| 16 | `biss_wavegen` | dir | 2026-09-01 | `tools/communication/` |
| 17 | `build_info` | dir | 2026-09-01 | `tools/project/` |
| 18 | `debug_model_overlay_validate` | dir | 2026-09-01 | `tools/trigger/` |
| 19 | `distributed_loopback_validate` | dir | 2026-09-01 | `tools/sync/` |
| 20 | `model_turntable_load_validate` | dir | 2026-09-01 | `tools/trigger/` |
| 21 | `ota_board_validate` | dir | 2026-09-01 | `tools/ota/` |
| 22 | `ota_keys` | dir | 2026-09-01 | `tools/ota/` |
| 23 | `pdf_text_extract` | dir | 2026-09-01 | `tools/project/` |
| 24 | `product_scpi_validate` | dir | 2026-09-01 | `tools/interface/` |
| 25 | `realtime_scpi_validate` | dir | 2026-09-01 | `tools/interface/` |
| 26 | `refmem_network_validate` | dir | 2026-09-01 | `tools/refmem/` |
| 27 | `refmem_node_load_auto_hil_validate` | dir | 2026-09-01 | `tools/refmem/` |
| 28 | `refmem_quality_gate_hil_validate` | dir | 2026-09-01 | `tools/refmem/` |
| 29 | `refmem_scpi_load_validate` | dir | 2026-09-01 | `tools/refmem/` |
| 30 | `refmem_spi_hil_validate` | dir | 2026-09-01 | `tools/refmem/` |
| 31 | `refmem_spi_line_activity` | dir | 2026-09-01 | `tools/refmem/` |
| 32 | `refmem_sync_hil_validate` | dir | 2026-09-01 | `tools/refmem/` |
| 33 | `rs485_modbus_validate` | dir | 2026-09-01 | `tools/communication/` |
| 34 | `scpi_legacy_validate` | dir | 2026-09-01 | `tools/interface/` |
| 35 | `sd_fs_build` | dir | 2026-09-01 | `tools/storage/` |
| 36 | `sd_mkfs` | dir | 2026-09-01 | `tools/storage/` |
| 37 | `sd_raw_clear` | dir | 2026-09-01 | `tools/storage/` |
| 38 | `trigger_meas` | dir | 2026-09-01 | `tools/trigger/` |
| 39 | `trigger_timing_test` | dir | 2026-09-01 | `tools/trigger/` |
| 40 | `two_board_io_validate` | dir | 2026-09-01 | `tools/sync/` |
| 41 | `uart_log_monitor` | dir | 2026-09-01 | `tools/project/` |
| 42 | `visa_ota_send` | dir | 2026-09-01 | `tools/ota/` |
| 43 | `analyzer_no5_correlator` | dir | 2026-09-05 | `tools/project/` |
| 44 | `ram_budget_check` | dir | 2026-09-05 | `tools/tdma/` |
