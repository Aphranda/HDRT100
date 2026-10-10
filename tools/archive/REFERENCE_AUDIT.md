# tools/ 引用审计（2026-10-10）

方法：对 `tools/` 顶层每个条目名，在 `docs/**`、`config/**`、`components/**`、`application/**`、
`middleware/**`、`linker/**`、`.githooks/**`、`CMakeLists.txt`、`AGENTS.md`、`tools/tool_registry.json`、
`tools/tool_layout.json` 中做子串匹配。

| 结果 | 数量 | 处置 |
|---|---|---|
| 被引用 | **111 / 113** | **原位保留**（移动会打断引用） |
| 未被引用 | 2（`__pycache__`、`t2_deploy_action_probe`） | 已**移动**到 `tools/archive/deletable-candidates/` |

结论：`tools/` 的扁平布局属"被大量文档引用的既定结构"，**不做物理重组**；
需要重组时按 `tools/README.md` 第 1 条作为独立迁移执行。原始清单见 `tools/_ref_audit.json`。
### N1-pilot（2026-10-10）

- 范围：12 项（N1 中引用 ≤3 的最旧条目）
- 旧路径 → 新路径：
  - `tools/flash_park_timeout_hil_validate` → `tools/flash/flash_park_timeout_hil_validate`
  - `tools/pdf_text_extract` → `tools/project/pdf_text_extract`
  - `tools/trigger_timing_test` → `tools/trigger/trigger_timing_test`
  - `tools/uart_log_monitor` → `tools/project/uart_log_monitor`
  - `tools/model_turntable_load_validate` → `tools/trigger/model_turntable_load_validate`
  - `tools/refmem_spi_line_activity` → `tools/refmem/refmem_spi_line_activity`
  - `tools/rs485_modbus_validate` → `tools/communication/rs485_modbus_validate`
  - `tools/scpi_legacy_validate` → `tools/interface/scpi_legacy_validate`
  - `tools/sd_mkfs` → `tools/storage/sd_mkfs`
  - `tools/sd_raw_clear` → `tools/storage/sd_raw_clear`
  - `tools/visa_ota_send` → `tools/ota/visa_ota_send`
  - `tools/run_portable_log_tests.ps1` → `tools/project/run_portable_log_tests.ps1`
- wrapper（兼容层）：12 个
- 引用更新：本批**暂不改 docs**（wrapper 保证可用）；待文档批统一更新
- 验证：文档门禁（本批不涉固件/OTA 实现，无 P3 需求）
- 回滚：`git mv` 反向 + 删除 wrapper
- 审计结果：旧路径引用仍存在（由 wrapper 承接）；DoD §3 在文档批完成后达成

### N1-batch2（2026-10-10）

- 范围：12 项（N1 中引用 4–8 的最旧条目）
  - `tools/artifact_checksum` → `tools/project/artifact_checksum`
  - `tools/biss_wavegen` → `tools/communication/biss_wavegen`
  - `tools/factory_package` → `tools/ota/factory_package`
  - `tools/factory_restore` → `tools/ota/factory_restore`
  - `tools/flash_lockout_hil_validate` → `tools/flash/flash_lockout_hil_validate`
  - `tools/ota_bin_info` → `tools/ota/ota_bin_info`
  - `tools/ota_keys` → `tools/ota/ota_keys`
  - `tools/refmem_scpi_load_validate` → `tools/refmem/refmem_scpi_load_validate`
  - `tools/uf2_join` → `tools/ota/uf2_join`
  - `tools/debug_model_overlay_validate` → `tools/sync/debug_model_overlay_validate`
  - `tools/flash_scratch_validate` → `tools/flash/flash_scratch_validate`
  - `tools/picotool_reboot` → `tools/ota/picotool_reboot`
- 兼容层：旧路径 wrapper 同批建立
- 验证：文档门禁
- 回滚：`git mv` 反向 + 删除 wrapper

### N1-batch3（2026-10-10）

- 范围：12 项
  - `tools/build_info` → `tools/project/build_info`
  - `tools/check_scpi_usb_namespace.py` → `tools/project/check_scpi_usb_namespace.py`
  - `tools/distributed_loopback_validate` → `tools/communication/distributed_loopback_validate`
  - `tools/ota_board_validate` → `tools/ota/ota_board_validate`
  - `tools/ota_packager` → `tools/ota/ota_packager`
  - `tools/product_scpi_validate` → `tools/interface/product_scpi_validate`
  - `tools/realtime_scpi_validate` → `tools/validation/realtime_scpi_validate`
  - `tools/refmem_network_validate` → `tools/refmem/refmem_network_validate`
  - `tools/refmem_node_load_auto_hil_validate` → `tools/refmem/refmem_node_load_auto_hil_validate`
  - `tools/refmem_quality_gate_hil_validate` → `tools/refmem/refmem_quality_gate_hil_validate`
  - `tools/refmem_spi_hil_validate` → `tools/refmem/refmem_spi_hil_validate`
  - `tools/refmem_sync_hil_validate` → `tools/refmem/refmem_sync_hil_validate`
- 兼容层：旧路径 wrapper（按扩展名转发）
- 验证：文档门禁

### N1-final（2026-10-10）

- 范围：8 项（N1 收官）
  - `tools/rp2350_tk_toolbox.py` → `tools/project/rp2350_tk_toolbox.py`
  - `tools/run_biss_protocol_tests.ps1` → `tools/validation/run_biss_protocol_tests.ps1`
  - `tools/run_portable_ota_tests.ps1` → `tools/validation/run_portable_ota_tests.ps1`
  - `tools/sd_fs_build` → `tools/storage/sd_fs_build`
  - `tools/trigger_meas` → `tools/trigger/trigger_meas`
  - `tools/two_board_io_validate` → `tools/sync/two_board_io_validate`
  - `tools/analyzer_no5_correlator` → `tools/sync/analyzer_no5_correlator`
  - `tools/ram_budget_check` → `tools/project/ram_budget_check`
- 兼容层：旧路径 wrapper
- 验证：文档门禁

### N2（2026-10-10）

- 范围：6 项（2026-09-06…09-12）
  - `tools/ota_haofv_gate` → `tools/ota/ota_haofv_gate`
  - `tools/release_check` → `tools/project/release_check`
  - `tools/analyzer_trace_batch_index` → `tools/sync/analyzer_trace_batch_index`
  - `tools/analyzer_trace_decode` → `tools/sync/analyzer_trace_decode`
  - `tools/codex_watchdog` → `tools/project/codex_watchdog`
  - `tools/dpll_waveform_capture` → `tools/vdc/dpll_waveform_capture`
- 兼容层：旧路径 wrapper
- 验证：文档门禁

### batch 2026-09-13…2026-09-20（2026-10-10）

- 范围：4 项
  - `tools/multicore_board_validate` → `tools/rtos/multicore_board_validate`
  - `tools/refmem_pack_build` → `tools/refmem/refmem_pack_build`
  - `tools/refmem_table_image` → `tools/refmem/refmem_table_image`
  - `tools/scope_dpll_capture` → `tools/vdc/scope_dpll_capture`
- 兼容层：旧路径 wrapper
- 验证：文档门禁

### batch 2026-09-21…2026-09-23（2026-10-10）

- 范围：14 项
  - `tools/analyzer_burst` → `tools/sync/analyzer_burst`
  - `tools/analyzer_tdma_hil` → `tools/tdma/analyzer_tdma_hil`
  - `tools/analyzer_trace_export` → `tools/sync/analyzer_trace_export`
  - `tools/biss_board_validate` → `tools/communication/biss_board_validate`
  - `tools/calibration_loopback_validate` → `tools/calibration/calibration_loopback_validate`
  - `tools/dpll_observation_capture` → `tools/vdc/dpll_observation_capture`
  - `tools/dpll_observation_decode` → `tools/vdc/dpll_observation_decode`
  - `tools/dpll_residual_analyze` → `tools/vdc/dpll_residual_analyze`
  - `tools/dpll_servo_tune` → `tools/vdc/dpll_servo_tune`
  - `tools/dpll_vdc_monitor` → `tools/vdc/dpll_vdc_monitor`
  - `tools/ota_boot_commit` → `tools/ota/ota_boot_commit`
  - `tools/ota_multi_update` → `tools/ota/ota_multi_update`
  - `tools/ota_send` → `tools/ota/ota_send`
  - `tools/ota_stream_send` → `tools/ota/ota_stream_send`
- 兼容层：旧路径 wrapper
- 验证：文档门禁
