# tools/ 工具索引（按 domain 分组）

Last updated: 2026-10-10

由 `tools/tool_registry.json` 自动生成（B0 步骤）；域优先分组便于按子系统检索。
重新生成：见 `tools/archive/TOOLS_MIGRATION_PLAN.md` B0。

| domain | 工具数 |
|---|---|
| `CALIBRATION` | 4 |
| `COMMUNICATION` | 5 |
| `DOCS` | 2 |
| `FLASH` | 4 |
| `INTERFACE` | 4 |
| `OTA` | 15 |
| `REFMEM` | 11 |
| `RTOS` | 2 |
| `STATE_MACHINE` | 1 |
| `STORAGE` | 7 |
| `SYNC` | 7 |
| `TDMA` | 2 |
| `TOOLS` | 14 |
| `TRIGGER` | 3 |
| `VALIDATION` | 2 |
| `VDC` | 19 |
| `WATCHDOG` | 1 |

## CALIBRATION（4 项）

| id | 用途 | 入口 | lifecycle | operation |
|---|---|---|---|---|
| `TOOL-CALIBRATION-LOOPBACK-VALIDATE` | calibration loopback validate | `tools/calibration_loopback_validate/calibration_loopback_val` | UNASSESSED | UNASSESSED |
| `TOOL-CALIBRATION-RING-VALIDATE` | calibration ring validate | `tools/calibration_ring_validate/calibration_bias_train.py; t` | UNASSESSED | UNASSESSED |
| `TOOL-SMA-CABLE-DELAY-VALIDATE` | sma cable delay validate | `tools/sma_cable_delay_validate/sma_cable_appointment_validat` | UNASSESSED | UNASSESSED |
| `TOOL-SMA-LOOPBACK-VALIDATE` | sma loopback validate | `tools/sma_loopback_validate/sma_single_board_loopback.py; to` | UNASSESSED | UNASSESSED |

## COMMUNICATION（5 项）

| id | 用途 | 入口 | lifecycle | operation |
|---|---|---|---|---|
| `TOOL-BISS-BOARD-VALIDATE` | biss board validate | `tools/biss_board_validate/biss_board_validate.py` | UNASSESSED | UNASSESSED |
| `TOOL-BISS-WAVEGEN` | biss wavegen | `tools/biss_wavegen/biss_wave_plot.py; tools/biss_wavegen/bis` | UNASSESSED | UNASSESSED |
| `TOOL-DISTRIBUTED-LOOPBACK-VALIDATE` | distributed loopback validate | `tools/distributed_loopback_validate/distributed_loopback_val` | UNASSESSED | UNASSESSED |
| `TOOL-RS485-MODBUS-VALIDATE` | rs485 modbus validate | `tools/rs485_modbus_validate/rs485_modbus_validate.py` | UNASSESSED | UNASSESSED |
| `TOOL-UART-LOG-MONITOR` | uart log monitor | `tools/uart_log_monitor/uart_log_monitor.py` | UNASSESSED | UNASSESSED |

## DOCS（2 项）

| id | 用途 | 入口 | lifecycle | operation |
|---|---|---|---|---|
| `TOOL-DOC-REGRESSION` | doc regression | `tools/doc_regression_check.py` | UNASSESSED | UNASSESSED |
| `TOOL-DOCS-CHECK` | docs check | `tools/docs_check/docs_check.py` | UNASSESSED | UNASSESSED |

## FLASH（4 项）

| id | 用途 | 入口 | lifecycle | operation |
|---|---|---|---|---|
| `TOOL-FLASH-LOCKOUT-HIL-VALIDATE` | flash lockout hil validate | `tools/flash_lockout_hil_validate/flash_lockout_hil_validate.` | UNASSESSED | UNASSESSED |
| `TOOL-FLASH-MAP` | flash map | `tools/flash_map/flash_consumer_check.py; tools/flash_map/fla` | UNASSESSED | UNASSESSED |
| `TOOL-FLASH-PARK-TIMEOUT-HIL-VALIDATE` | flash park timeout hil validate | `tools/flash_park_timeout_hil_validate/flash_park_timeout_hil` | UNASSESSED | UNASSESSED |
| `TOOL-FLASH-SCRATCH-VALIDATE` | flash scratch validate | `tools/flash_scratch_validate/flash_scratch_validate.py` | UNASSESSED | UNASSESSED |

## INTERFACE（4 项）

| id | 用途 | 入口 | lifecycle | operation |
|---|---|---|---|---|
| `TOOL-PRODUCT-SCPI-VALIDATE` | product scpi validate | `tools/product_scpi_validate/product_scpi_validate.py` | UNASSESSED | UNASSESSED |
| `TOOL-SCPI-COMMON` | Shared serial lifecycle, board identity and OTA timing modules | `—` | UNASSESSED | UNASSESSED |
| `TOOL-SCPI-LEGACY-VALIDATE` | scpi legacy validate | `tools/scpi_legacy_validate/scpi_legacy_validate.py` | UNASSESSED | UNASSESSED |
| `TOOL-SCPI-QUERY` | scpi query | `tools/domains/interface/scpi_query/scpi_query.py` | UNASSESSED | UNASSESSED |

## OTA（15 项）

| id | 用途 | 入口 | lifecycle | operation |
|---|---|---|---|---|
| `TOOL-FACTORY-PACKAGE` | factory package | `tools/factory_package/factory_package.py` | UNASSESSED | UNASSESSED |
| `TOOL-FACTORY-RESTORE` | factory restore | `tools/factory_restore/factory_restore.py` | UNASSESSED | UNASSESSED |
| `TOOL-OTA-BIN-INFO` | ota bin info | `tools/ota_bin_info/ota_bin_info.py` | UNASSESSED | UNASSESSED |
| `TOOL-OTA-BOARD-VALIDATE` | ota board validate | `tools/ota_board_validate/ota_board_validate.py` | UNASSESSED | UNASSESSED |
| `TOOL-OTA-BOOT-COMMIT` | ota boot commit | `tools/ota_boot_commit/ota_boot_commit.py` | UNASSESSED | UNASSESSED |
| `TOOL-OTA-HAOFV-GATE` | ota haofv gate | `tools/ota_haofv_gate/ota_haofv_gate.py` | UNASSESSED | UNASSESSED |
| `TOOL-OTA-KEYS` | ota keys | `tools/ota_keys/gen_ota_key_registry.py; tools/ota_keys/gener` | UNASSESSED | UNASSESSED |
| `TOOL-OTA-MULTI-UPDATE` | ota multi update | `tools/ota_multi_update/ota_multi_update.py` | UNASSESSED | UNASSESSED |
| `TOOL-OTA-PACKAGER` | ota packager | `tools/ota_packager/ota_packager.py` | UNASSESSED | UNASSESSED |
| `TOOL-OTA-SEND` | ota send | `tools/ota_send/ota_send.py` | UNASSESSED | UNASSESSED |
| `TOOL-OTA-STREAM-SEND` | ota stream send | `tools/ota_stream_send/ota_stream_send.py` | UNASSESSED | UNASSESSED |
| `TOOL-PICOTOOL-FLASH` | picotool flash | `tools/picotool_flash/picotool_flash.py` | UNASSESSED | UNASSESSED |
| `TOOL-PICOTOOL-REBOOT` | picotool reboot | `tools/picotool_reboot/picotool_reboot.py` | UNASSESSED | UNASSESSED |
| `TOOL-UF2-JOIN` | uf2 join | `tools/uf2_join/make_fill_bin.py; tools/uf2_join/uf2_join.py` | UNASSESSED | UNASSESSED |
| `TOOL-VISA-OTA-SEND` | visa ota send | `tools/visa_ota_send/visa_ota_send.py` | UNASSESSED | UNASSESSED |

## REFMEM（11 项）

| id | 用途 | 入口 | lifecycle | operation |
|---|---|---|---|---|
| `TOOL-REFMEM-NETWORK-VALIDATE` | refmem network validate | `tools/refmem_network_validate/refmem_network_validate.py` | UNASSESSED | UNASSESSED |
| `TOOL-REFMEM-NODE-LOAD-AUTO-HIL-VALIDATE` | refmem node load auto hil validate | `tools/refmem_node_load_auto_hil_validate/refmem_node_load_au` | UNASSESSED | UNASSESSED |
| `TOOL-REFMEM-PACK-BUILD` | refmem pack build | `tools/refmem_pack_build/refmem_pack_build.py` | UNASSESSED | UNASSESSED |
| `TOOL-REFMEM-PACK-WRITE` | refmem pack write | `tools/refmem_pack_write/refmem_pack_write.py` | UNASSESSED | UNASSESSED |
| `TOOL-REFMEM-QUALITY-GATE-HIL-VALIDATE` | refmem quality gate hil validate | `tools/refmem_quality_gate_hil_validate/refmem_quality_gate_h` | UNASSESSED | UNASSESSED |
| `TOOL-REFMEM-SCPI-LOAD-VALIDATE` | refmem scpi load validate | `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py` | UNASSESSED | UNASSESSED |
| `TOOL-REFMEM-SPI-HIL-VALIDATE` | refmem spi hil validate | `tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py` | UNASSESSED | UNASSESSED |
| `TOOL-REFMEM-SPI-LINE-ACTIVITY` | refmem spi line activity | `tools/refmem_spi_line_activity/refmem_spi_line_activity.py` | UNASSESSED | UNASSESSED |
| `TOOL-REFMEM-SYNC-HIL-VALIDATE` | refmem sync hil validate | `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py` | UNASSESSED | UNASSESSED |
| `TOOL-REFMEM-TABLE-IMAGE` | refmem table image | `—` | UNASSESSED | UNASSESSED |
| `TOOL-REFMEM-TABLE-REGISTRY-VALIDATE` | refmem table registry validate | `tools/refmem_table_registry_validate/refmem_table_registry_v` | UNASSESSED | UNASSESSED |

## RTOS（2 项）

| id | 用途 | 入口 | lifecycle | operation |
|---|---|---|---|---|
| `TOOL-MULTICORE-BOARD-VALIDATE` | multicore board validate | `tools/multicore_board_validate/multicore_board_validate.py` | UNASSESSED | UNASSESSED |
| `TOOL-RTOS-WATERMARK-CAPTURE` | rtos watermark capture | `tools/rtos_watermark_capture/rtos_watermark_capture.py` | UNASSESSED | UNASSESSED |

## STATE_MACHINE（1 项）

| id | 用途 | 入口 | lifecycle | operation |
|---|---|---|---|---|
| `TOOL-STATE-MACHINE-RESOURCE-CHECK` | state machine resource check | `tools/state_machine_resource_check/state_machine_resource_ch` | UNASSESSED | UNASSESSED |

## STORAGE（7 项）

| id | 用途 | 入口 | lifecycle | operation |
|---|---|---|---|---|
| `TOOL-SD-BOARD-VALIDATE` | sd board validate | `tools/sd_board_validate/sd_board_validate.py` | UNASSESSED | UNASSESSED |
| `TOOL-SD-FS-BUILD` | sd fs build | `tools/sd_fs_build/sd_fs_build.py` | UNASSESSED | UNASSESSED |
| `TOOL-SD-MKFS` | sd mkfs | `tools/sd_mkfs/sd_mkfs.py` | UNASSESSED | UNASSESSED |
| `TOOL-SD-RAW-CLEAR` | sd raw clear | `tools/sd_raw_clear/sd_raw_clear.py` | UNASSESSED | UNASSESSED |
| `TOOL-SD-TRACE-DECODE` | sd trace decode | `tools/domains/storage/sd_trace_decode/sd_trace_decode.py` | UNASSESSED | UNASSESSED |
| `TOOL-STORAGE-FILE-UPLOAD` | storage file upload | `tools/storage_file_upload/storage_file_upload.py` | UNASSESSED | UNASSESSED |
| `TOOL-STORAGE-SCPI-VALIDATE` | storage scpi validate | `tools/storage_scpi_validate/storage_scpi_validate.py` | UNASSESSED | UNASSESSED |

## SYNC（7 项）

| id | 用途 | 入口 | lifecycle | operation |
|---|---|---|---|---|
| `TOOL-ANALYZER-BURST` | analyzer burst | `tools/analyzer_burst/analyzer_burst.py` | UNASSESSED | UNASSESSED |
| `TOOL-ANALYZER-NO5-CORRELATOR` | analyzer no5 correlator | `tools/analyzer_no5_correlator/analyzer_no5_correlator.py` | UNASSESSED | UNASSESSED |
| `TOOL-ANALYZER-TRACE-BATCH-INDEX` | analyzer trace batch index | `tools/analyzer_trace_batch_index/analyzer_trace_batch_index.` | UNASSESSED | UNASSESSED |
| `TOOL-ANALYZER-TRACE-DECODE` | analyzer trace decode | `tools/analyzer_trace_decode/analyzer_trace_decode.py` | UNASSESSED | UNASSESSED |
| `TOOL-ANALYZER-TRACE-EXPORT` | analyzer trace export | `tools/analyzer_trace_export/analyzer_trace_export.py` | UNASSESSED | UNASSESSED |
| `TOOL-DEBUG-MODEL-OVERLAY-VALIDATE` | debug model overlay validate | `tools/debug_model_overlay_validate/debug_model_overlay_valid` | UNASSESSED | UNASSESSED |
| `TOOL-TWO-BOARD-IO-VALIDATE` | two board io validate | `tools/two_board_io_validate/two_board_io_validate.py` | UNASSESSED | UNASSESSED |

## TDMA（2 项）

| id | 用途 | 入口 | lifecycle | operation |
|---|---|---|---|---|
| `TOOL-ANALYZER-TDMA-HIL` | analyzer tdma hil | `tools/analyzer_tdma_hil/analyzer_tdma_hil.py` | UNASSESSED | UNASSESSED |
| `TOOL-TDMA-RING-MONITOR` | tdma ring monitor | `tools/tdma_ring_monitor/flight_bitmap_validate.py; tools/tdm` | UNASSESSED | UNASSESSED |

## TOOLS（14 项）

| id | 用途 | 入口 | lifecycle | operation |
|---|---|---|---|---|
| `TOOL-ARTIFACT-CHECKSUM` | artifact checksum | `tools/artifact_checksum/artifact_checksum.py` | UNASSESSED | UNASSESSED |
| `TOOL-BENCH` | bench | `tools/bench/rp2350_tk_toolbox.py; tools/rp2350_tk_toolbox.py` | UNASSESSED | UNASSESSED |
| `TOOL-BUILD-INFO` | build info | `tools/build_info/gen_build_info.py` | UNASSESSED | UNASSESSED |
| `TOOL-CATALOG` | Repository tool navigation and operating notes | `—` | UNASSESSED | OFFLINE |
| `TOOL-CHECKS` | checks | `tools/check_scpi_usb_namespace.py; tools/checks/check_scpi_u` | UNASSESSED | UNASSESSED |
| `TOOL-CMAKE-BUILD-AUTO` | cmake build auto | `tools/cmake_build_auto/cmake_build_auto.py` | UNASSESSED | UNASSESSED |
| `TOOL-CODEX-WATCHDOG` | codex watchdog | `tools/codex_watchdog/codex_supervisor.ps1; tools/codex_watch` | UNASSESSED | UNASSESSED |
| `TOOL-HARDWARE-ACCEPTANCE` | hardware acceptance | `tools/hardware_acceptance/p3_hardware_acceptance.py; tools/h` | UNASSESSED | UNASSESSED |
| `TOOL-HDO-RAW-READER` | HDO frozen RAW BYTE transport and offline block decoder | `—` | EXPERIMENTAL | DEVICE_CONFIGURE |
| `TOOL-HOST-EVIDENCE-CLOCK` | High-resolution monotonic host timestamp identity for auditable eviden | `—` | EXPERIMENTAL | OFFLINE |
| `TOOL-PDF-TEXT-EXTRACT` | pdf text extract | `tools/pdf_text_extract/pdf_text_extract.py` | UNASSESSED | UNASSESSED |
| `TOOL-RAM-BUDGET-CHECK` | ram budget check | `tools/ram_budget_check/ram_budget_check.py` | UNASSESSED | UNASSESSED |
| `TOOL-RELEASE-CHECK` | release check | `tools/release_check/release_check.py` | UNASSESSED | UNASSESSED |
| `TOOL-TOOL-GOVERNANCE` | Static file inventory, explicit tool registry checks and catalog gener | `tools/tool_governance/tool_governance.py` | EXPERIMENTAL | OFFLINE |

## TRIGGER（3 项）

| id | 用途 | 入口 | lifecycle | operation |
|---|---|---|---|---|
| `TOOL-MODEL-TURNTABLE-LOAD-VALIDATE` | model turntable load validate | `tools/model_turntable_load_validate/model_turntable_load_val` | UNASSESSED | UNASSESSED |
| `TOOL-TRIGGER-MEAS` | trigger meas | `tools/trigger_meas/debug_trigger.py; tools/trigger_meas/loop` | UNASSESSED | UNASSESSED |
| `TOOL-TRIGGER-TIMING-TEST` | trigger timing test | `tools/trigger_timing_test/trigger_timing_test.py` | UNASSESSED | UNASSESSED |

## VALIDATION（2 项）

| id | 用途 | 入口 | lifecycle | operation |
|---|---|---|---|---|
| `TOOL-REALTIME-SCPI-VALIDATE` | realtime scpi validate | `tools/realtime_scpi_validate/realtime_scpi_validate.py` | UNASSESSED | UNASSESSED |
| `TOOL-TESTS` | tests | `tools/run_biss_protocol_tests.ps1; tools/run_portable_log_te` | UNASSESSED | UNASSESSED |

## VDC（19 项）

| id | 用途 | 入口 | lifecycle | operation |
|---|---|---|---|---|
| `TOOL-DPLL-OBSERVATION-CAPTURE` | dpll observation capture | `tools/dpll_observation_capture/dpll_observation_capture.py` | UNASSESSED | UNASSESSED |
| `TOOL-DPLL-OBSERVATION-DECODE` | Offline CRC-checked decode of persisted DPLL observation captures into | `tools/domains/vdc/dpll_observation_decode/dpll_observation_d` | EXPERIMENTAL | OFFLINE |
| `TOOL-DPLL-RESIDUAL-ANALYZE` | Offline DPLL residual, convergence and anomaly analysis over monitor o | `tools/domains/vdc/dpll_residual_analyze/dpll_residual_analyz` | EXPERIMENTAL | OFFLINE |
| `TOOL-DPLL-SERVO-TUNE` | dpll servo tune | `tools/dpll_servo_tune/dpll_servo_tune.py` | UNASSESSED | UNASSESSED |
| `TOOL-DPLL-VDC-MONITOR` | dpll vdc monitor | `tools/dpll_vdc_monitor/dpll_vdc_monitor.py` | UNASSESSED | UNASSESSED |
| `TOOL-DPLL-WAVEFORM-CAPTURE` | dpll waveform capture | `tools/dpll_waveform_capture/dpll_waveform_capture.py` | UNASSESSED | UNASSESSED |
| `TOOL-SCOPE-DPLL-CAPTURE` | scope dpll capture | `tools/scope_dpll_capture/scope_dpll_capture.py` | UNASSESSED | UNASSESSED |
| `TOOL-VDC-DPLL-REPLAY` | Offline diagnostic replay of recorded or generated VDC samples through | `tools/domains/vdc/dpll_replay/vdc_dpll_replay.py` | EXPERIMENTAL | OFFLINE |
| `TOOL-VDC-GPIO-LOCK-VALIDATE` | vdc gpio lock validate | `tools/vdc_gpio_lock_validate/vdc_gpio_lock_validate.py` | UNASSESSED | UNASSESSED |
| `TOOL-VDC-JOINT-WORKFLOW` | Pinned inputs and outer four-board lifecycle for VDC joint capture | `—` | EXPERIMENTAL | DEVICE_CONFIGURE |
| `TOOL-VDC-LATCH-VALIDATE` | vdc latch validate | `tools/vdc_latch_validate/vdc_latch_validate.py` | UNASSESSED | UNASSESSED |
| `TOOL-VDC-LOCK-READINESS-VALIDATE` | vdc lock readiness validate | `tools/vdc_lock_readiness_validate/vdc_lock_readiness_validat` | UNASSESSED | UNASSESSED |
| `TOOL-VDC-LONG-MONITOR` | vdc long monitor | `tools/vdc_long_monitor/vdc_long_monitor.py` | UNASSESSED | UNASSESSED |
| `TOOL-VDC-OBSERVER-VALIDATE` | vdc observer validate | `tools/vdc_observer_validate/vdc_observer_validate.py` | UNASSESSED | UNASSESSED |
| `TOOL-VDC-PRIORITY-TRACE` | vdc priority trace | `tools/vdc_priority_trace/vdc_priority_trace.py; tools/vdc_pr` | UNASSESSED | UNASSESSED |
| `TOOL-VDC-PROBE-SUPPORT` | Pure parsing and provenance support for VDC four-board probes | `—` | EXPERIMENTAL | OFFLINE |
| `TOOL-VDC-SPARSE-SCOPE` | Explicit HDO setup for sparse DPLL scope observation | `—` | EXPERIMENTAL | DEVICE_CONFIGURE |
| `TOOL-VDC-SYNC-METROLOGY` | vdc sync metrology | `tools/domains/vdc/sync_metrology/vdc_sync_metrology.py` | UNASSESSED | UNASSESSED |
| `TOOL-VDC-TDMA-SELFTEST-VALIDATE` | vdc tdma selftest validate | `tools/vdc_tdma_selftest_validate/vdc_tdma_selftest_validate.` | UNASSESSED | UNASSESSED |

## WATCHDOG（1 项）

| id | 用途 | 入口 | lifecycle | operation |
|---|---|---|---|---|
| `TOOL-WATCHDOG-MONITOR` | watchdog monitor | `tools/watchdog_monitor/watchdog_reset_probe.py` | UNASSESSED | UNASSESSED |

