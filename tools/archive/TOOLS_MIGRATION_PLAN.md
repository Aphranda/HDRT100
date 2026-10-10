# tools/ 迁移计划（v1，评审稿）

Last updated: 2026-10-10

## 0. 目标与约束

**目标**：把 `tools/` 从"113 项扁平"整理为**按域分类**的结构，使新增工具可发现、可登记、可审计；同时不打断任何文档引用与门禁。

**硬约束**

1. **不删除**：任何动作只允许 移动 / 归档；删除由用户确认后执行。
2. **引用安全优先**：`tools/` 顶层 113 项中 **111 项被引用**（见 `tools/archive/REFERENCE_AUDIT.md`、
   `tools/_ref_audit.json`）。任何移动必须**同步更新全部引用**并通过门禁，否则不得合入。
3. **禁止移动集**（门禁/基建核心，任何批次都不得移动）：
   `docs_check/`、`doc_regression_check.py`、`checks/`、`hardware_acceptance/`、
   `tool_registry.json`、`tool_registry.schema.json`、`tool_layout.json`、`tool_governance/`、
   `common/`、`domains/`、`workflows/`、`tests/`、`bench/`、`archive/`、`scratch/`、`README.md`。
4. 每批次独立提交；文档改动与工具改动按仓库约定**分提交**，并跑文档门禁。

## 1. 目标分类法（**领域优先**，与 `docs/<域>`、`components/<域>` 对齐）

一级按**子系统域**（可发现性最好：找"校准/Flash/DPLL"直接进对应域），二级用**工具种类后缀**；
种类同时写入 `tool_registry.json` 的 `kind` 字段（validator/analyzer/monitor/capture/packager/infra）。

| 目标目录 | 收纳（按现名） | 预计项数 |
|---|---|---|
| `tools/calibration/` | `calibration_ring_validate`、`calibration_loopback_validate`、`sma_loopback_validate`、`sma_cable_delay_validate`、`trigger_meas`、`trigger_timing_test` | ~6 |
| `tools/flash/` | `flash_scratch_validate`、`flash_park_timeout_hil_validate`、`flash_lockout_hil_validate`、`picotool_flash`、`picotool_reboot`、`artifact_checksum`、`build_info`（`flash_map/` **禁止移动**：构建期生成映射，CMake 直接调用） | ~7 |
| `tools/dpll/` | `dpll_observation_capture`、`dpll_observation_decode`、`dpll_residual_analyze`、`dpll_servo_tune`、`dpll_waveform_capture`、`dpll_vdc_monitor`、`scope_dpll_capture` | ~7 |
| `tools/vdc/` | `vdc_tdma_selftest_validate`、`vdc_observer_validate`、`vdc_long_monitor`、`vdc_lock_readiness_validate`、`vdc_latch_validate`、`vdc_gpio_lock_validate`、`vdc_sync_metrology`、`vdc_dpll_replay`、`vdc_priority_trace` | ~9 |
| `tools/refmem/` | `refmem_table_registry_validate`、`refmem_sync_hil_validate`、`refmem_spi_line_activity`、`refmem_spi_hil_validate`、`refmem_scpi_load_validate`、`refmem_quality_gate_hil_validate`、`refmem_pack_write`、`refmem_node_load_auto_hil_validate`、`refmem_network_validate`、`refmem_table_image`、`refmem_pack_build` | ~11 |
| `tools/tdma/` | `tdma_ring_monitor`、`ram_budget_check`（TDMA 预算）、`tdma-*` 类脚本 | ~3 |
| `tools/trigger/` | `sync_trigger`、`model_turntable_load_validate`、`debug_model_overlay_validate`、`state_machine_resource_check` | ~4 |
| `tools/sync_io/` | `two_board_io_validate`、`distributed_loopback_validate`、`multicore_board_validate`、`biss_board_validate`、`biss_wavegen`、`rs485_modbus_validate`、`rtos_watermark_capture` | ~7 |
| `tools/storage/` | `sd_trace_decode`、`sd_raw_clear`、`sd_mkfs`、`sd_fs_build`、`sd_board_validate`、`storage_scpi_validate`、`storage_file_upload` | ~7 |
| `tools/scpi/` | `scpi_query`、`scpi_common`、`scpi_legacy_validate`、`realtime_scpi_validate`、`product_scpi_validate`、`check_scpi_usb_namespace.py` | ~6 |
| `tools/ota/` | `ota_send`、`ota_stream_send`、`ota_multi_update`、`ota_board_validate`、`ota_haofv_gate`、`ota_keys`、`ota_bin_info`、`ota_boot_commit`、`ota_packager`、`visa_ota_send`、`factory_package`、`factory_restore`、`uf2_join`、`release_check` | ~14 |
| `tools/analyzers/` | `analyzer_burst`、`analyzer_trace_export`、`analyzer_trace_batch_index`、`analyzer_no5_correlator`、`analyzer_tdma_hil`、`analyzer_trace_decode` | ~6 |
| `tools/monitors/` | `uart_log_monitor`、`watchdog_monitor`、`codex_watchdog` | ~3 |
| `tools/project/` | `cmake_build_auto`、`pdf_text_extract`、`run_biss_protocol_tests.ps1`、`run_portable_ota_tests.ps1`、`run_portable_log_tests.ps1`、`rp2350_tk_toolbox.py` | ~6 |

**禁止移动集（补充）**：除原列外，新增 `flash_map/`（构建期映射生成，CMake 调用）、
`docs_check/`、`doc_regression_check.py`、`checks/`、`hardware_acceptance/`、`tool_governance/`、
`common/`、`domains/`、`workflows/`、`tests/`、`bench/`、`archive/`、`scratch/`、`tool_registry*.json`、
`tool_layout.json`、`README.md`。

**分类原则**：① 先问"它属于哪个子系统"（域）→ 进该域目录；② 域内再按种类命名（`*_validate`/`*_monitor`/
`*_analyze`/`*_capture`/`*_pack`）；③ 跨域工具（如 `artifact_checksum`、`build_info`）归"首个主要消费者"所在域，
并在 registry 的 `kind=infra` 标注；④ `tools/scratch/` 只放草稿，不进域目录。


> **B0 实测修订（2026-10-10）**：`tools/tool_registry.json` 已含权威 `domain` 字段（103 个工具）：
> VDC 19、OTA 15、TOOLS 14、REFMEM 11、SYNC 7、STORAGE 7、COMMUNICATION 5、CALIBRATION 4、
> FLASH 4、INTERFACE 4、TRIGGER 3、DOCS 2、RTOS 2、TDMA 2、VALIDATION 2、STATE_MACHINE 1、WATCHDOG 1。
> ⇒ **B1+ 的域目录名以 registry `domain` 为准**（小写化：`vdc/ ota/ refmem/ sync/ storage/ calibration/
> flash/ interface/ trigger/ docs_tools/ rtos/ tdma/ communication/ validation/ state_machine/ watchdog/
> project(TOOLS 中非域工具)`），不再另造 `analyzers/monitors` 等新域；种类信息用 registry 的
> `lifecycle`/`operation` 表达。索引见 `tools/INDEX.md`（由 registry 自动生成）。

## 2. 兼容层策略（保证旧路径仍可用）

对每个被移动的工具目录，在**旧路径**留一个**转发入口**（薄 wrapper），内容形如：

```python
# tools/<old_name>/<entry>.py   ← 兼容转发（迁移期保留）
import runpy, pathlib
runpy.run_path(str(pathlib.Path(__file__).resolve().parents[1] / "<category>/<old_name>/<entry>.py"),
               run_name="__main__")
```

- 作用：文档、脚本、门禁里以 `tools/<old_name>/...` 形式出现的引用**继续可用**。
- 代价：旧路径仍在（但只含 1 个几行的转发文件），可发现性由 `tool_registry.json` 的 `category` 字段提供。
- 退出条件：当引用审计显示旧路径在 `docs/**`、`config/**`、门禁中**不再出现**时，wrapper 移入
  `tools/archive/`（仍不删除）。

## 3. 分批计划（每批：范围 → 操作 → 引用更新 → 验证 → 回滚）

| 批次 | 范围 | 关键动作 | 验证 | 回滚 |
|---|---|---|---|---|
| **B0 准备** | 无移动 | ① `tool_registry.json` 增 `category` 字段并补登全部工具；② 生成 `tools/INDEX.md`（按域分组索引）；③ 记录本计划为评审稿 | 文档门禁 + 注册表 schema 校验 | 删除索引与字段（commit 回退） |
| **B1 校验器** | ~70 个 `*_validate` | 建 `tools/validators/<域>/`（vdc/refmem/storage/sync/ota/board/trigger/flash）；逐个 `git mv` + 留 wrapper | ① 全量引用更新后跑 `docs_check`+`doc_regression_check`；② 抽样 3 个校验器按文档命令实跑（含 1 个 HIL） | 逐目录 `git mv` 回原位（wrapper 一并撤） |
| **B2 分析器** | ~12 | 建 `tools/analyzers/` | 抽样跑 1 个离线分析（如 `dpll_observation_decode`） | 同上 |
| **B3 监视器** | ~10 | 建 `tools/monitors/` | 抽样跑 1 个（如 `vdc_long_monitor --dry-run`） | 同上 |
| **B4 OTA/打包** | ~18 | 建 `tools/ota/` | **必须**：`p3_hardware_acceptance.py run` 一次 + OTA 路径冒烟（`ota_bin_info`/`uf2_join`） | 同上 |
| **B5 工程工具** | ~10 | 建 `tools/project/` | 文档门禁 + `cmake_build_auto` 冒烟 | 同上 |
| **B6 收尾** | wrapper 清理 | 对"引用已消失"的旧路径 wrapper 移入 `tools/archive/`；更新 `REFERENCE_AUDIT.md` | 重新审计：旧路径引用数 = 0 | 移回 wrapper |

**批次顺序理由**：先做**零移动**的 B0（登记与索引，立即改善可发现性且零风险），再从小到大迁移；
把**涉及固件/OTA 凭证**的 B4 放在中间（需 P3），把纯工程工具放最后。

## 4. 引用更新清单（每批次都要扫）

| 位置 | 说明 |
|---|---|
| `docs/**` | 文档中的 `tools/<name>/...` 路径（含进展日志归档与 legacy） |
| `tools/tool_registry.json`、`tool_layout.json` | 注册表与布局定义（B0 起即改为按 `category` 组织） |
| `config/**` | workflow inputs、acceptance 配置中引用的工具路径 |
| `.githooks/**`、`CMakeLists.txt` | 门禁与构建引用 |
| `components/**`、`application/**`、`middleware/**` | 源码注释/脚本中的工具引用 |
| `tools/archive/REFERENCE_AUDIT.md` | 审计记录（每批次更新） |

## 5. 验收标准

**单批验收**：① 该批目录已归位且 wrapper 生效；② 引用审计中旧路径引用已更新为 0（wrapper 除外）；
③ 文档门禁通过；④ 抽样命令按文档可实跑；⑤ B4 附 P3 凭证；⑥ 审计记录已更新。

**整体完成定义**：① 5 个域目录建成且 `tools/` 顶层只剩禁止移动集 + 5 个域目录；
② `tool_registry.json` 全量登记 `category`；③ `REFERENCE_AUDIT.md` 显示旧路径引用为 0；
④ 所有 wrapper 已移入 `tools/archive/`；⑤ 用户确认可删除 `tools/archive/deletable-candidates/`。

## 6. 风险与未决

| 风险 | 缓解 |
|---|---|
| 文档门禁对路径真实性的校验 | 每批次先跑 `docs_check --strict-names` + `doc_regression_check`，再合入 |
| 隐藏引用（字符串拼接、动态 import） | 引用审计按"目录名子串"匹配，覆盖拼接场景；B4 追加一次全量 P3 |
| `git mv` 在 Windows 上的大小写/锁定问题 | 每批次前确认无进程占用；失败则跳过该目录并记入审计 |
| 重新组织会稀释历史可追溯性 | 保留 wrapper + 审计表记录"旧路径 → 新路径"映射，写进 `REFERENCE_AUDIT.md` |

## 7. 立即可做的第一步（零移动）

**B0**：① 给 `tool_registry.json` 增加 `category` 字段并登记现有全部工具；② 生成 `tools/INDEX.md`；
③ 把本计划（评审稿）随提交入库。B0 不移动任何目录、零风险，但立刻让 `tools/` 可发现、可审计，
并为后续 5 个批次提供分类依据。

## 10. 按新鲜度分批（2026-10-10 修订；**不干扰主线**）

背景：并行会话（Claude）正在做主线任务 ⇒ 迁移**只动最不新鲜的条目**，最近改动一律不碰。

**保护规则（强制）**

1. **不动近 7 天内有修改的条目**（当前即 2026-10-04 之后的 `sync_trigger/`、`vdc_priority_trace/`、
   `hardware_acceptance/`、`tdma_ring_monitor/`、`calibration_ring_validate/`、`scratch/`、`INDEX.md` 等）。
2. **不动禁止移动集**（见 `TOOLS_DIRECTORY_STANDARD.md` §5）。
3. 每批只覆盖一个新鲜度区间；**每批独立提交**；失败即停并记入审计。
4. 每次执行前重算新鲜度（`tools/_freshness.json`）；若目标条目在 **24h 内被修改** ⇒ 跳过并延后。
5. wrapper 保证旧路径继续可用 ⇒ 主线按旧路径调用不受影响。
6. 迁移期间**不批量改写 `tool_registry.json`**（避免与主线冲突），只在必要时追加条目。

**新鲜度批次（可移动条目共 95 项：2026-09 有 89、2026-10 有 6）**

| 批次 | 新鲜度区间 | 项数（约） | 目标域目录 | 风险 |
|---|---|---|---|---|
| **N1** | ≤ 2026-09-05 | **44** | ota/ flash/ refmem/ sd(→storage)/ scpi/ interface/ communication/ project/ */_validate 分类 | 低（最旧） |
| N2 | 2026-09-06 … 09-12 | ~12 | dpll/ vdc/ monitors/ | 低 |
| N3 | 2026-09-13 … 09-20 | ~10 | vdc/ interface/ project/ | 中 |
| N4 | 2026-09-21 … 09-27 | ~15 | vdc/ dpll/ refmem/ tdma/ trigger/ | 中 |
| N5 | 2026-09-28 … 10-03 | ~5 | tdma/ vdc/ | 中 |
| **N6（暂缓）** | ≥ 2026-10-04 | **6** | — | 高（主线在用）⇒ **等主线空闲再做** |

**每批流程**：① 出候选清单（含引用审计摘要）→ ② 用户过目 → ③ `git mv` + 旧路径 wrapper →
④ 同批更新引用 → ⑤ 跑文档门禁（涉固件/Ota 批次加 P3）→ ⑥ 更新 `REFERENCE_AUDIT.md`（含旧→新映射）
→ ⑦ 独立提交。

**N1 候选清单**：见 `tools/archive/MIGRATION_BATCH_N1_CANDIDATES.md`（44 项，含每项建议目标域）。

## 11. 执行状态（2026-10-10 回写）

| 项 | 状态 |
|---|---|
| 已迁移 | **89 项**（N1 44 + N2 6 + N3 4 + N4 三批 35）|
| 域目录 | 16 个：calibration · communication · flash · interface · ota · project · refmem · rtos · storage · sync · tdma · trigger · validation · vdc · watchdog · state_machine |
| 兼容层 | 209 个 wrapper 文件 + 123 个 alias，已按仓库机制登记（`role=compatibility_wrapper`）；治理校验通过 |
| registry/layout | 路径按域归位（252 + 136 处），352 条 tools 路径 100% 存在 |
| README catalog | `tools/README.md` 收录 registry catalog 块（校验器要求） |
| 引用归零 | docs/** 与 config/components/application/middleware/linker/.githooks 中旧路径引用 = **0** |
| 未迁移（延后） | **6 项**：`sync_trigger`、`tdma_ring_monitor`、`vdc_priority_trace`、`calibration_ring_validate`（主线在用/7 天保护期）、`cmake_build_auto`、`picotool_flash`（近期构建链工具） |

**延后触发条件**：主线空闲且最近修改超过 7 天后，用 `tools/archive/migration-ledger/migrate_batch.py`
按同流程迁移（该脚本已随台账入库，含 wrapper 生成与审计追加）。

**兼容层保留决策**：wrapper **保留**为受治理兼容层，不归档。理由：① 已合法登记并通过治理校验；
② 主线可能仍按旧路径调用（用户明确要求"不干扰主线"）；③ 归档会使旧路径失效。
触发归档的条件：主线确认不再使用任何旧路径后，按 `TOOLS_ARCHIVE_STANDARD.md` 移入 `tools/archive/`。
