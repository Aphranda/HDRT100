# SYNC_IO 任务进度

Status: Active
Domain: SYNC_IO
Canonical: `docs/sync/SYNC_IO_TASK_PROGRESS.md`
Related: `docs/sync/SYNC_IO_ARCHITECTURE.md`, `docs/sync/SYNC_IO_TODO.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`, `docs/storage/LOG_SYSTEM_TODO.md`
Last updated: 2026-09-04

本文档只记录 SYNC_IO 域的提交、构建、测试、OTA/HIL、失败、回退和证据位置。任务状态以
`SYNC_IO_TODO.md` 为唯一事实源，稳定语义以 `SYNC_IO_ARCHITECTURE.md` 为准。

## 文档接口

- 每条新记录必须有唯一 `SYNC-PROGRESS-*` ID，并引用一个或多个 `SYNC_*` TODO task ID。
- 架构契约、任务状态和实施证据分别由 Architecture、TODO 和本文件维护，不交叉替代。
- 历史 `SYNC_IO-TASK-*` 记录已迁移为 `SYNC-PROGRESS-*` ID；其中的单次数字都是当时验收
  快照，不是当前代码事实源。

## 当前 Checkpoint

### SYNC-PROGRESS-20260904-004 - 冻结逻辑分析仪公共契约

- TODO task ID：`SYNC-LA-001`、`SYNC-M3`。
- 变更：新增 `sync_io_logic_analyzer.h/.c`，冻结 `RAW_SAMPLE`、`EDGE_TIMESTAMP`、
  `TRIGGERED_CAPTURE` 的 config、record、snapshot、seqlock 读取和强类型结束 reason；source
  mask 直接校验 `LOGIC_ANALYZER` 只读 persona descriptor，配置不含 GPIO 写入字段。
- 门禁策略：新增 gate reason/action。调试模式仅对质量、CRC、超时和资源冲突记录快照及原始
  值，并按 `debug_continue_budget` 有界继续或本轮结束；产品模式严格拒绝；DMA 越界、非法
  内存/Flash 和失控 GPIO 始终硬停。
- 软件验证：`run_sync_io_logic_analyzer_tests.ps1` 通过；状态机资源检查通过；Python 全量
  回归 `705 passed`。VDC domain 既有 host 用例当前独立失败 4 项（slew/acquisition 断言），
  与本切片无文件交集，未将聚合 host 全绿误记为通过。
- 构建验证：`out/build/pico2-rtos-multicore-smoke` 与 `out/build/pico2-validation` release
  build 通过，生成 package build id 为构建快照，非代码事实源。
- 下一步：完成文档门禁后运行统一 P3；因本切片修改固件接口，仍必须复跑四节点 TDMA 短帧闭环，
  通过后再提交并推送。

### SYNC-PROGRESS-20260904-003 - 移除旧 PIO1 suspend/resume handoff

- TODO task ID：`SYNC-RES-003`、`SYNC-M4`。
- 变更提交：`c9c2fd3`，已推送到 `origin/refactor/tdma-phy-split-p3-gated`。TDMA flight
  resource manager 不再调用旧 suspend/resume API；SYNC_IO 不再初始化或持有旧 PIO1 wave
  SM，model pulse 与 fire pulse 统一转入 PIO0 persona 路径，迁移兼容状态位和 trace 已删除。
- 软件/构建验证：`tests/python` 全量验收快照为 `703 passed`；resource-arbiter host tests
  通过；`out/build/pico2-rtos-multicore-smoke` 与 `out/build/pico2-validation` release build
  通过，本轮 package build id 验收快照为 `20260904025945`。
- 硬件验证：统一 QUICK_DIAGNOSTIC P3 证据位于
  `out/hardware-acceptance/sync-res-003-remove-pio1-handoff-20260904/`；四节点使用同一 package
  完成 OTA，每板协商的 `max_data_block_size` 验收快照为 `4096`。`acceptance.json` 的
  `passed=true`、`flow_completed=true`；TDMA `closed_loop_passed=true`、
  `realtime_gate_passed=true`、`left_running=true`，NO1--NO4 SD 原始 capture、SVG 与离线
  分析均保留并通过。
- 诊断与策略：coarse CLK level 7 有一条 follower forwarding arm failure，已记录在
  `acceptance.json.diagnostic_failures`，流程继续完成 TDMA 和波形闭环；无越界 DMA、非法
  内存/Flash 操作或失控 GPIO 硬停条件，因此未拒绝本轮流程。
- 结论：`SYNC-RES-003` 标记为 `DONE`；`SYNC-M4` 继续 `IN PROGRESS`，下一项进入
  `LOGIC_ANALYZER` persona（`SYNC-LA-001`）。

### SYNC-PROGRESS-20260904-002 - PIO0 persona descriptor/lifecycle 与 TDMA 短帧闭环

- TODO task ID：`SYNC-RES-001`、`SYNC-RES-002`、`SYNC-RES-003`、`SYNC-M2`。
- 变更：PIO0 persona descriptor、兼容矩阵和 lifecycle manager 已接入输出/预约触发路径；
  WAVE_OUTPUT 使用 PIO0 专用 SM/DMA/workspace，SCHEDULED_TRIGGER 使用独立 PIO0 SM；
  旧 PIO1 suspend/resume 仍作为兼容层保留，未宣称迁移收尾。
- 软件/构建验证：资源检查通过；状态机/文档定向 pytest 46/46 通过；
  `out/build/pico2-rtos-multicore-smoke` 增量编译通过；4096B OTA 包使用
  `out/build/pico2-validation`。
- 硬件验证：QUICK_DIAGNOSTIC TDMA-only 证据位于
  `out/hardware-acceptance/sync-res-003-pio0-20260904/`。四节点 OTA 成功；
  `tdma-process-image/summary.json` 的 `closed_loop_passed`、`realtime_gate_passed`、
  `left_running` 均为 true；四节点 SD 原始 capture、SVG 和
  `analysis/ring_capture_analysis.json` 均保留且分析通过。NO5 未加入本轮 scope。
- 诊断：coarse CLK level 7 记录一条既有 calibration failure；流程按调试策略继续，未影响
  TDMA 短帧闭环，也未伪造严格 gate 通过。
- 下一步：移除旧 PIO1 suspend/resume compatibility handoff，再进入 LOGIC_ANALYZER persona。

### SYNC-PROGRESS-20260903-001 - SYNC_IO 域与逻辑分析仪契约重构

- TODO task ID：`SYNC-DOC-001`、`SYNC-M1`。
- 日期：2026-09-03。
- 变更：重构 SYNC_IO Architecture/TODO/Task Progress 三件套，纠正旧 PIO0 input、PIO1
  output、PIO2 AUX 分区；将不带限定词的 wave 拆为输出型 `WAVE_OUTPUT` 和只读
  `LOGIC_ANALYZER` persona，并冻结 pad-visible GPIO 旁路观测、目标 GPIO 不接管、目标
  FIFO 不消费、Core1 capture/Core0 落盘和长期 edge timestamp 边界。
- 跨域同步：状态机架构承接 `ARCH-PIOPARTITION-01`，SYNC_IO 架构承接
  `ARCH-IOANALYZER-01`；两项登记保持 pending，等待独立交叉审核后才能变更状态。
- 代码边界：本记录只重构文档，不修改固件、PIO、工具或测试，不产生新的 OTA/HIL 结论；
  当前 `BOARD_SYNC_PIO_WAVE` 到 TDMA TX PIO 的 suspend/resume handoff 仍是待迁移实现。
- 验证：`docs_check.py --strict-names` 通过，验收快照（非事实源）为 `files=121,
  warnings=0`；`doc_regression_check.py` 通过，登记表验收快照（非事实源）为 `25 contracts`，
  只保留既有旧格式 `TDMA-FLIGHT-BITMAP-01` warning；文档 pytest 验收快照（非事实源）为
  `18 passed`；pre-commit 通过并正确跳过纯文档变更的硬件门禁；`--log-check` 与
  `p3_hardware_acceptance.py check-staged` 均通过。
- 下一步：先完成 PIO0 persona descriptor 和兼容矩阵，再迁移输出 persona；逻辑分析仪按
  RAW、EDGE、TRIGGERED、Core0/SD 的顺序实现。

## 验证与证据索引

| progress ID | TODO task ID | 证据 |
|---|---|---|
| SYNC-PROGRESS-20260903-001 | SYNC-DOC-001 / SYNC-M1 | 本次文档 diff；docs_check、doc_regression、文档 pytest、pre-commit、log-check 和 staged acceptance 均通过。 |

## 历史实施记录

### SYNC-PROGRESS-20260816-008 - TDMA window capture budget

- TODO task ID：`SYNC-VDC-001`。

- 状态：代码、构建和 COM5 单板验证完成；COM6 物理恢复后待两板 HIL。
- 目标：解决 10 MHz capture self-test 中 core1 无界消费 DMA ring，避免窗口外采样拖垮 realtime loop 和 USB/SCPI。
- 完成：
  - `sync_io` capture 使用 DMA channel 3 环形缓存，core1 每次最多处理 128 个 word。
  - capture timestamp window 已 arm 时，依据硬件 tick timebase 和 DMA word sequence 跳过窗口外 backlog，只把 observation window 内 raw word 写入 latch ring。
  - 窗口外跳过不计为 DMA overflow；真实 ring 溢出仍清除 timebase valid 并保留 drop evidence，不能伪造 `DPLL_ELIGIBLE`。
  - VDC observer 单次批量从 8 提升到 32，覆盖一个 10 us / 100 ns observation window 的完整 word 集。
- 验证：
  - `cmake --build build-rtos-multicore-smoke` 通过，最终 build `20260816065050`，package CRC `0x65948FE3`。
  - 17/17 host unit test scripts、VDC tests、SCPI dry-run、docs check 通过。
  - COM5 已 OTA/boot/commit 到 `20260816065050`，`SYST:LOOP:STAT?` 持续增长，TX-only PIO/DMA self-test 成功。
  - COM6 在旧版高负载 self-test 后 CDC 不响应；OTA 和 picotool 强制 BOOTSEL 均无法完成，需物理复位或重新插拔后再部署最终包。
- 后续：
  - COM6 恢复后使用 `tools\vdc_tdma_selftest_validate\vdc_tdma_selftest_validate.py` 完成 X->Y、Y->X 双向验证；必须看到 accepted sample、gate pass、hardware tick、`DPLL_ELIGIBLE` 且无 `DIAGNOSTIC_ONLY`。

### SYNC-PROGRESS-20260816-007 - timer1 hardware tick diagnostic latch

- TODO task ID：`SYNC-VDC-001`。

- 状态：完成代码、host/build 和 COM5/COM6 HIL。
- 目标：把上一阶段 `time_us_64()*1000` 软件微秒 latch 升级为本地硬件 tick 时间基，同时不把 core1 drain FIFO 时刻冒充为 PIO 边沿硬锁存。
- 完成：
  - `sync_io` 初始化 `timer1_hw`，source 选 `CLK_SYS`，保留 SDK/default `timer0`。
  - `sync_io_capture_latch_service_core1()` 在 core1 搬运 PIO capture FIFO 时读取 `timer1` raw tick，并按 `clk_sys` 转换为 ns。
  - `REALtime:IO:SAMPle:LATCh?` 返回字段扩展为 9 项：`initialized,capture_running,capture_sample_hz,dropped_capture_words,latched_capture_words,dropped_latched_capture_words,capture_latch_source,capture_latch_resolution_ns,capture_latch_flags`。
  - VDC compact observation 保留 latch fact 自己声明的 timestamp source/resolution/flags；dictionary 只做 event/source slot/reference slot/payload class 语义匹配，不覆盖采样事实。
  - VDC gate 拒绝 source 与 dictionary 不一致的 compact sample，并拒绝带 `DIAGNOSTIC_ONLY` 的 hardware tick sample 进入 DPLL lock gate。
- 边界：
  - 本阶段报告 `HARDWARE_TICK / <=100 ns / DIAGNOSTIC_ONLY`。
  - 时间戳仍发生在 core1 drain FIFO 时刻，不等同于 PIO 边沿硬件锁存；只能作为 TDMA/VDC bring-up 诊断证据。
  - 后续仍需 PIO/DMA/IRQ/core1 edge latch，才允许生成 `DPLL_ELIGIBLE` observation sample。
- 验证：
  - `python -m py_compile tools\vdc_latch_validate\vdc_latch_validate.py tools\vdc_observer_validate\vdc_observer_validate.py tools\realtime_scpi_validate\realtime_scpi_validate.py` 通过。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run` 通过，generated=66。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，generated=128。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有既有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id `20260816040434`，OTA package CRC `0x84D58EA6`。
  - COM5/COM6 均 OTA 到 build `20260816040434` 并 commit；两板 `SYSTem:ERRor?` 均为 `0,"No error"`。
  - `python tools\vdc_latch_validate\vdc_latch_validate.py COM5 COM6 --expected-build 20260816040434 --out-dir build-rtos-multicore-smoke\vdc_latch_validate_20260816040434` 通过：COM5 `latched=118->270, observer_raw=0->274, source=2, resolution_ns=4, flags=1`，COM6 `latched=118->270, observer_raw=0->276, source=2, resolution_ns=4, flags=1`。
  - `python tools\vdc_observer_validate\vdc_observer_validate.py COM5 COM6 --expected-build 20260816040434 --out-dir build-rtos-multicore-smoke\vdc_observer_validate_20260816040434` 通过，两板 `schedule_crc32=974530568`、`dictionary_crc32=1814735745`。
  - 后续 gate 增强验证见 `VDC-TASK-20260816-023`：COM5/COM6 在 build `20260816042527` 上 forced edge 均进入 observer，且 `submitted=1,accepted=0,rejected=1,gate=9`，证明 `DIAGNOSTIC_ONLY` hardware tick 没有进入 DPLL accepted path。

### SYNC-PROGRESS-20260816-006 - core1 capture latch phase 1

- TODO task ID：`SYNC-VDC-001`。

- 状态：完成代码、host/build 和 COM5/COM6 HIL。
- 目标：把 PIO capture FIFO 从 core0 轮询 raw word，推进为 core1 realtime 侧搬运的 timestamped capture fact，供 VDC observer 消费。
- 完成：`sync_io_capture_latch_service_core1()` 在 core1 realtime loop 中从 `sync_capture_4bit` RX FIFO 读取 raw word，写入 bounded latch ring；每个 word 附带 `sample_seq`、`base_time_l32_ns`、`sample_period_ns`、timestamp source/resolution/flags 和 drop counter。
- 完成：`vdc_dpll_manager` 改为读取 `sync_io_read_capture_latched()`，不再直接消费 PIO FIFO；dictionary 只补 source/reference/payload 语义，不覆盖 latch 自身声明的 timestamp source/resolution/flags。
- 完成：新增 `REALtime:IO:SAMPle:LATCh?`，返回 `initialized,capture_running,capture_sample_hz,dropped_capture_words,latched_capture_words,dropped_latched_capture_words,capture_latch_source,capture_latch_resolution_ns`。
- 边界：本阶段 timestamp 仍来自 `time_us_64()*1000`，固定报告 `SOFTWARE_US / 1000 ns / DIAGNOSTIC_ONLY`；它只能作为 bring-up 诊断，不允许进入 100 ns DPLL lock gate。
- 验证：
  - `python -m py_compile tools\vdc_latch_validate\vdc_latch_validate.py tools\vdc_observer_validate\vdc_observer_validate.py tools\realtime_scpi_validate\realtime_scpi_validate.py` 通过。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run` 生成 66 条，包含 `REALtime:IO:SAMPle:LATCh?`。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，generated=128。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id `20260816034347`，OTA package CRC `0x962F5B65`。
  - COM5/COM6 均 OTA 到 build `20260816034347` 并 commit；`SYSTem:ERRor?` 均为 `0,"No error"`。
  - `python tools\vdc_latch_validate\vdc_latch_validate.py COM5 COM6 --expected-build 20260816034347 --out-dir build-rtos-multicore-smoke\vdc_latch_validate_20260816034347_v2` 通过：COM5 `latched=117->270, observer_raw=31->307`，COM6 `latched=120->272, observer_raw=31->310`。
  - `python tools\vdc_observer_validate\vdc_observer_validate.py COM5 COM6 --expected-build 20260816034347 --out-dir build-rtos-multicore-smoke\vdc_observer_validate_20260816034347` 通过，两板 `schedule_crc32=974530568`、`dictionary_crc32=1814735745`。
- 后续：先升级为 `timer1/CLK_SYS` hardware tick diagnostic latch，再继续推进 PIO/DMA/IRQ/core1 edge latch，最终形成 `HARDWARE_TICK / <=100 ns / DPLL_ELIGIBLE` 的 VDC observation sample。

### SYNC-PROGRESS-20260816-002 - VDC raw capture observer 接线

- TODO task ID：`SYNC-VDC-001`。

- 目标：把 `sync_io_read_capture_words()` 产出的 raw IO fact 接到 VDC compact observation path，同时保持 SYNC_IO 不解释 TDMA/DPLL 语义。
- 完成：`vdc_dpll_manager` 增加默认关闭的 sync_io observer 配置和状态 API；启用后每次 VDC service 读取 bounded raw word batch，经 `VdcSyncIoAdapter` 转为 `VdcCompactObservationSample`，再提交 VDC dictionary/wrap/gate。
- 完成：observer 状态记录 raw word、capture result、sample seq、event id、tick_l32、accepted/rejected/gate reject 计数；不自动启动 capture，不用 `board_uptime_ms()` 伪造硬件 timestamp。
- 完成：新增 `SYSTem:SYNC:VDC:OBServer?` 只读维护查询，返回 observer enabled、batch、raw/no-edge/ambiguous/submitted/accepted/rejected、last raw word、event id、tick_l32 和 gate reject；查询不触发 capture 或样本提交。
- 验证：`powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
- 验证：`powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
- 验证：`python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，generated=127。
- 验证：`python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
- 验证：`git diff --check` 通过，仅有既有 CRLF 提示。
- 验证：`cmake --build build-rtos-multicore-smoke` 通过，最新生成 build id `20260816024745`，package CRC `0x028BC853`。
- 验证：COM5/COM6 均 OTA 到 build `20260816024745` 并 commit；两板 `SYST:SYNC:VDC:OBServer?` 均返回 18 个零字段，符合默认 disabled observer；`SYST:ERR?` 均为 `0,"No error"`。

### SYNC-PROGRESS-20260816-003 - VDC raw capture observer 维护配置

- TODO task ID：`SYNC-VDC-001`。

- 状态：完成代码、文档、build 和 COM5/COM6 启停态 HIL。
- 完成：新增 `SYSTem:SYNC:VDC:OBServer` 维护配置命令；无参数或 `0` 只关闭 observer 并清零状态，避免产品枚举或误调用时打开采集链路。
- 完成：启用态要求显式给出 batch、rising/falling event id、observed mask、initial mask、base tick、sample period、expected window start 和 frame CRC，随后交由 `vdc_dpll_manager_configure_sync_io_observer()` 做合法性检查。
- 边界：该命令只配置 VDC manager observer，不启动 `sync_io` capture，不直接写 DPLL，不把 raw word 当作 lock evidence。
- 验证：
  - build `20260816030427`，OTA package CRC `0x5D46BBBD`。
  - COM5/COM6 均 OTA boot/commit 成功，`SYSTem:FW:BUILD?` 返回 `"20260816030427"`，`SYSTem:ERRor?` 返回 `0,"No error"`。
  - COM5/COM6 执行无参数 `SYST:SYNC:VDC:OBServer` 均返回 `1`，查询返回 disabled 全零字段。
  - COM5/COM6 执行 `SYST:SYNC:VDC:OBServer 1,1,1,2,1,0,0,1000,0,0,1` 均返回 `1`，查询显示 `enabled=1,max_words_per_service=1`；随后 `SYST:SYNC:VDC:OBServer 0` 关闭后查询回到 disabled 全零字段。

### SYNC-PROGRESS-20260816-004 - VDC observer HIL evidence fields

- TODO task ID：`SYNC-VDC-001`。

- 状态：完成代码、文档、build 和 COM5/COM6 启停态字段验证。
- 完成：`SYSTem:SYNC:VDC:OBServer?` 在原 18 字段之后追加配置和证据字段：rising/falling event、observed mask、initial mask、sample period、expected window、frame CRC、schedule CRC、dictionary CRC/profile CRC、edge index、timestamp source/resolution/flags、source/reference slot 和 payload class。
- 边界：关闭态仍保持 40 字段全零；启用态只暴露 observer 配置和 VDC dictionary/profile 证据，不启动 capture，不把空 FIFO 或软件时间当作 DPLL lock。
- 验证：
  - build `20260816031400`，OTA package CRC `0x0F669557`。
  - COM5/COM6 均 OTA 到 build `20260816031400`；`ota_boot_commit.py` 两次均因启动日志污染误判失败，独立 `scpi_query` 确认 `SYST:OTA:STAT? -> "COMMITTED",1,"NONE",5`、`SYST:OTA:SLOT? -> 2,0,2,0,0`、`SYST:ERR? -> 0,"No error"`。
  - COM5/COM6 启用最小合法 observer 后，`OBServer?` 返回 40 字段，其中 `schedule_crc32=974530568`、`dictionary_crc32=1814735745`、`dictionary_profile_crc32=974530568`；关闭后返回 40 个零字段。

### SYNC-PROGRESS-20260816-005 - VDC observer HIL script 固化

- TODO task ID：`SYNC-VDC-001`。

- 状态：完成并通过 COM5/COM6。
- 完成：新增 `tools/vdc_observer_validate/vdc_observer_validate.py`，固化 observer disable/enable/query/disable/error 验证流程，避免后续继续手写串口命令。
- 验证：`python tools\vdc_observer_validate\vdc_observer_validate.py COM5 COM6 --expected-build 20260816031400` 通过，两板均确认 `schedule_crc32=974530568`、`dictionary_crc32=1814735745`。
- 风险：当前仍依赖显式配置的 `next_base_time_l32_ns`、`sample_period_ns`、event id 和 frame CRC；真实 PIO/DMA timestamp latch 和 COM5/COM6 HIL 证据待后续补齐。
- 后续：补 HIL，把 dictionary CRC、edge index、timestamp source/resolution、profile CRC 和 VDC gate result 写入报告。
- 涉及文件：`components/vdc_dpll_manager/inc/vdc_dpll_manager.h`，`components/vdc_dpll_manager/src/vdc_dpll_manager.c`，`docs/sync/SYNC_IO_TODO.md`。

### SYNC-PROGRESS-20260816-001 - 通用 IO 观测器归属纠偏

- TODO task ID：`SYNC-LA-001`。

- 目标：明确 `sync_io_read_capture_words()` 的架构归属，避免把通用 raw IO capture primitive 误写成 VDC/TDMA 私有能力。
- 完成：`SYNC_IO_ARCHITECTURE.md` 增加“通用 IO 观测器”章节，说明该接口读取 `sync_capture_4bit` PIO RX FIFO，每个 32-bit word 表示 8 个连续 4-bit 输入采样。
- 完成：明确该接口可观测 TDMA 通讯/同步 PIO 线、转台输入脉冲、READY/GATE/ARM/AUX 状态和调试线序，但只提供 raw IO fact，不解释业务语义，不写 DPLL/Trigger/RefMem/VDC。
- 完成：`SYNC_IO_TODO.md` P5 增加 raw observation 到 VDC adapter、转台输入/脉冲计数解释路径的后续接线项。
- 验证：`python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
- 风险：当前只是文档归属纠偏；代码中 `sync_io_read_capture_words()` 到 `VdcSyncIoAdapter`、转台输入 AO/FB 的实际任务接线仍未完成。
- 后续：实现 `sync_io` raw capture word 到 VDC compact observation 的任务接线，并为转台输入/脉冲计数建立独立解释路径，避免复用 VDC 语义。

### SYNC-PROGRESS-20260815-001 - 建立 IO 三分标准文档

- TODO task ID：`SYNC-DOC-001`。

- 目标：按 HAOFV 架构把 `docs/sync` 的 IO 文档收敛为架构、待办、任务进度三份标准入口，避免资源规划、重构计划和评审待办继续分叉。
- 完成：新增 `SYNC_IO_ARCHITECTURE.md`，明确 `SYNC:*`、`REALtime:*`、`sync_io`、mode driver、PIO/DMA/IRQ、RefMem 和 VDC 的 owner 边界。
- 完成：新增 `SYNC_IO_TODO.md`，把当前优先级收敛到 PIO 预约输出、真实最小物理链路、AUX 语义通道、mode self-test、组件化和 RefMem/VDC 集成。
- 完成：更新 `README.md`，将 `SYNC_IO_ARCHITECTURE.md`、`SYNC_IO_TODO.md`、`SYNC_IO_TASK_PROGRESS.md` 设为当前三分 canonical，旧资源规划和重构评审文档降为支撑输入。
- 风险：本次只做文档结构纠偏，未改代码；`ModelTurntableAO` 的 PIO 预约输出和真实 transport 仍是下一步实现重点。
- 后续：按 `SYNC_IO_TODO.md` P0 开始实现模型转台 PIO 预约输出路径，代码修改必须保持 AO 生成计划、`sync_io` owner 装载硬件、PIO/DMA 执行边沿的边界。

### SYNC-PROGRESS-20260815-002 - 融合旧 SYNC 文件并准备删除

- TODO task ID：`SYNC-DOC-001`。

- 目标：旧 `SYNC_IO_RESOURCE_PLAN.md`、`SYNC_IO_REFACTOR_PLAN.md`、`SYNC_IO_ARCH_REVIEW_TODO.md` 和 `SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` 已落后当前 HAOFV/RefMem/VDC 主线，先吸收有效内容，再删除旧入口。
- 完成：`SYNC_IO_ARCHITECTURE.md` 增加 PIO/DMA 资源基线、mode 资源互斥、CAL_RING / 分布式同步链路边界和预约触发链路。
- 完成：`SYNC_IO_TODO.md` 的文档清理项标记旧文件有效内容已迁入新三分结构，剩余动作是删除旧文件并修正全仓引用。
- 风险：历史任务记录中仍会提到旧文件名作为当时变更对象；这类记录只表达历史上下文，不再作为当前 canonical 入口。
- 后续：删除旧文件，更新 README、arch、trigger、vdc、hardware、communication 等引用到 `SYNC_IO_ARCHITECTURE.md` / `SYNC_IO_TODO.md`。

### SYNC-PROGRESS-20260707-001 - TriggerFB RESET/FAULT 语义收敛

- TODO task ID：`SYNC-MODE-001`。

- 目标：完成 `SYNC_IO_ARCH_REVIEW_TODO.md` P0-00，统一 `TRIG_EVENT_RESET` 在所有 ECC 状态下的语义，并为 reset/fault/clear/disarm 后的资源释放补充低频 trace。
- 完成：新增 `fb_reset_all()`，统一停止 clock/capture、释放 SEQ/ENC/BISS owner、回到 `TRIG_STATE_IDLE`、清理 `error_code` 和 `active_mode`；所有 ECC 表中的 `TRIG_EVENT_RESET` 均改为 `fb_reset_all()`。
- 完成：`TRIG_EVENT_CLEAR_FAULT` 保持复用 `fb_release_running_io()`，并补齐 `active_mode` 归零；`TRIG_EVENT_FAULT` 继续复用同一释放 helper 后进入 `TRIG_STATE_FAULT`。
- 完成：TriggerAO 新增 `trigger.resource_release` trace，字段为 `trigger_event`、`before_state`、释放前 `active_resources` 和 `released_resources`；离线解码器已同步解析事件 45。
- 完成：`tools/sd_board_validate/sd_board_validate.py` 增加 `--validate-trigger-release`，按板端路径执行 SEQ_STEP ARM 后 `*RST`、再次 ARM 后 `TRIG:FAULT`，并要求 fault trace 中同时出现 RESET/FAULT 的资源释放记录。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`。
- 验证：`python -m py_compile tools\sd_board_validate\sd_board_validate.py tools\sd_trace_decode\sd_trace_decode.py` 通过。
- 验证：`picotool load -f -v -x build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录和 Flash verify 通过，设备重启后 COM5 `*IDN?` 正常返回。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_p0_00_release` 通过，`summary.txt` 为 `PASS`。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --out-dir build-codex-sync-refactor\sd_validation_default_after_release_tool` 通过，确认新增 release-path 选项未破坏默认 SD/trace 验证。
- 验证：P0-01 合入后的最终 build 上复跑 `python tools\sd_board_validate\sd_board_validate.py COM5 --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_p0_00_release_final` 通过。
- 验证：`decoded_fault_trace.json` 中 trace header/size/CRC/idx 全部通过；`trigger.resource_release` 记录显示 RESET 和 FAULT 均从 `SEQ_ARMED` 释放 `PIO1`、`DMA`。
- 风险：P0-00 已完成 SEQ_STEP 板端释放闭环；ENC/BISS 的 RESET/FAULT 路径共用同一 `fb_reset_all()` 和 `fb_release_running_io()`，后续在 P0-01/P0-03 做 BiSS 专项板端验证时继续覆盖。
- 后续：进入 P0-01 BiSS runtime timeout/sample scan 闭环，将 sample scan 步进从 helper 静默 re-arm 收敛到 TriggerFB 管理面 action。
- 涉及文件：`components/sync_trigger/src/trigger_fb.c`，`components/sync_trigger/src/sync_trigger.c`，`tools/sd_trace_decode/sd_trace_decode.py`，`tools/sd_board_validate/sd_board_validate.py`，`docs/sync/SYNC_IO_TODO.md`。

### SYNC-PROGRESS-20260707-002 - BiSS timeout/sample scan 闭环

- TODO task ID：`SYNC-MODE-001`、`SYNC-PROFILE-001`。

- 目标：完成 `SYNC_IO_ARCH_REVIEW_TODO.md` P0-01，让 BiSS runtime timeout/sample scan 可持续推进、re-arm 失败可进入稳定错误路径，并能由板端工具验证 trace 证据。
- 完成：`biss_node_io_poll_runtime()` 返回结构化 poll 结果；timeout scan 只准备下一步 delay 和 TAP config，不再在 helper 内静默 re-arm。
- 完成：TriggerFB 在 `BISS_ARMED` 的 runtime action 中处理 `BISS_NODE_IO_POLL_SCAN_STEP`，通过 mode ops 执行 BiSS TAP re-arm；失败时进入 `TRIG_STATE_FAULT` 并设置 `TRIG_ERROR_IO_ARM_FAILED`。
- 完成：scan re-arm 成功后由 TriggerFB 回写 `biss_node_io_sample_scan_rearm_succeeded()`，解除 timeout latch 并更新时间戳；非 scan 模式保持单次 timeout latch。
- 完成：TriggerAO 新增 `trigger.biss_timeout` 和 `trigger.biss_scan_step` trace；`sd_trace_decode.py` 已解码 timeout count、sample delay 前后值、scan index 和 wrap count。
- 完成：`tools/biss_board_validate/biss_board_validate.py` 增加 `--scan-wait-s`、`--expect-scan-steps` 和 `--capture-trace`，可等待 scan 推进、触发 fault evidence、回读 trace 并断言 BiSS timeout/scan-step 事件。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 build id `20260707155208` 的 factory/update 产物。
- 验证：`picotool load -f -v -x build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录和 Flash verify 通过，COM5 `SYST:FW:BUILD?` 返回 `"20260707155208"`。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --enable-scan --skip-inject --timeout-us 1000 --sample-delay 4 --scan-start 4 --scan-end 12 --scan-step 2 --scan-wait-s 5 --expect-scan-steps 2 --capture-trace --out-dir build-codex-sync-refactor\biss_validation_p0_01_scan` 通过。
- 验证：`decoded_fault_trace.json` 中 trace header/size/CRC/idx 全部通过；`trigger.biss_scan_step` 记录显示 sample delay 按 4→6→8→10→12 推进，`sample_scan_index` 到 4。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --out-dir build-codex-sync-refactor\biss_validation_p0_01_default` 通过，确认普通 BiSS TAP 配置、ARM、软件 frame crossing、DISARM 未被 sample-scan 改动破坏。
- 风险：P0-01 已覆盖无帧 timeout scan 的板端路径；re-arm 失败路径通过代码路径和 trace/error 逻辑闭合，后续 P0-03 拆物理 arm 边界时应补一个故意非法 mode config 的 fault 注入验证。
- 后续：进入 P0-02 resource owner 边界，避免后续移动 mode arm 时引入重复 acquire。
- 涉及文件：`components/sync_trigger/inc/biss_node_io.h`，`components/sync_trigger/src/biss_node_io.c`，`components/sync_trigger/src/trigger_fb.c`，`components/sync_trigger/src/sync_trigger.c`，`tools/sd_trace_decode/sd_trace_decode.py`，`tools/biss_board_validate/biss_board_validate.py`，`docs/sync/SYNC_IO_TODO.md`。

### SYNC-PROGRESS-20260707-003 - Resource owner 边界收口

- TODO task ID：`SYNC-RES-001`、`SYNC-MODE-001`。

- 目标：完成 `SYNC_IO_ARCH_REVIEW_TODO.md` P0-02，保持 TriggerFB 为唯一资源 owner，并让资源申请/释放与 mode ops 的 `.resources` 表字段一致。
- 完成：新增 `trigger_resource_map` 适配层，在 Trigger 域内将 `sync_io_mode_ops_t.resources` 映射到 `resource_arbiter_resource_t`，避免 `sync_io` core 反向依赖系统仲裁器。
- 完成：TriggerFB 的 SEQ/ENC/BISS acquire/release 全部改为从 mode ops 表驱动获取资源；裸 `sync_io_*_arm()` 仍不直接 acquire，避免多层重复持有。
- 完成：TriggerAO 的 resource snapshot trace 改用同一映射入口；资源冲突仍由 FB action 设置 `TRIG_ERROR_RESOURCE_CONFLICT` 并触发既有 `trigger.resource_snapshot`。
- 完成：`tools/sd_board_validate/sd_board_validate.py` 增加 `--validate-resource-owner`，按板端路径验证 SEQ、ENC、BISS arm 后 `SYST:RES?` 包含预期资源，disarm 后资源释放。
- 验证：`python -m py_compile tools\sd_board_validate\sd_board_validate.py tools\sd_trace_decode\sd_trace_decode.py tools\biss_board_validate\biss_board_validate.py` 通过。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 build id `20260707160600` 的 factory/update 产物。
- 验证：`picotool load -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录通过；`picotool verify -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` Flash verify 通过。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --validate-resource-owner --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_p0_02_resource_owner` 通过，`summary.txt` 为 `PASS`。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --out-dir build-codex-sync-refactor\biss_validation_p0_02_default` 通过，确认 BiSS TAP 配置、ARM、软件 frame crossing、DISARM 未被资源映射收口破坏。
- 风险：ENC_COUNT 现在按 mode table 映射为 `PIO1|DMA`，比旧手写 mask 多仲裁 DMA；这与 ENC 物理实现实际使用 DMA 的事实一致，但会改变未来与其他 DMA mode 的冲突判定。
- 后续：进入 P0-03 BiSS TAP 物理 ARM 边界，继续保持 TriggerFB owner 和 mode driver 物理实现边界清晰。
- 涉及文件：`components/sync_trigger/inc/trigger_resource_map.h`，`components/sync_trigger/src/trigger_resource_map.c`，`components/sync_trigger/src/trigger_fb.c`，`components/sync_trigger/src/sync_trigger.c`，`tools/sd_board_validate/sd_board_validate.py`，`docs/sync/SYNC_IO_TODO.md`。

### SYNC-PROGRESS-20260708-004 - BiSS TAP 物理 ARM 边界收口

- TODO task ID：`SYNC-MODE-001`、`SYNC-PROFILE-001`。

- 目标：完成 `SYNC_IO_ARCH_REVIEW_TODO.md` P0-03，让 BiSS TAP 的物理 PIO arm/disarm/is_running/read FIFO 实现归属 `sync_io_mode_biss_tap.c`，TriggerFB 继续只负责 ECC、资源 owner 和错误码。
- 完成：新增 `sync_io_core_internal.h`，只暴露 mode driver 所需的 core 初始化状态、PIO program offset、AUX 模式标记和 trace helper；没有暴露 `sync_io_context_t`。
- 完成：`sync_io_biss_tap_arm()`、`sync_io_biss_tap_disarm()`、`sync_io_biss_tap_is_running()`、`sync_io_biss_tap_read_frame_word()` 的物理实现从 `sync_io.c` 搬到 `sync_io_mode_biss_tap.c`。
- 完成：`sync_io_mode_biss_tap.c` 增加 mode 级 disarm/is_running/read/rx_fifo_full API，ops 表的 disarm/is_running 指向 mode driver；`biss_node_io` 不再直接包含 PIO/board SM 细节。
- 完成：硬件 pinout、AUX 方向和物理 frame/sample edge 限制保留在 `sync_io_biss_tap_mode_validate()`；协议 profile 范围、CRC/status/sample scan 语义继续由 `biss_profile_validate()` 和 `biss_node_io` 管理。
- 验证：`python -m py_compile tools\sd_board_validate\sd_board_validate.py tools\sd_trace_decode\sd_trace_decode.py tools\biss_board_validate\biss_board_validate.py` 通过。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 build id `20260707161717` 的 factory/update 产物。
- 验证：`picotool load -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录通过；`picotool verify -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` Flash verify 通过。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --out-dir build-codex-sync-refactor\biss_validation_p0_03_default` 通过，确认 BiSS TAP 配置、ARM、软件 frame crossing、DISARM 正常。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --enable-scan --skip-inject --timeout-us 1000 --sample-delay 4 --scan-start 4 --scan-end 12 --scan-step 2 --scan-wait-s 5 --expect-scan-steps 2 --capture-trace --out-dir build-codex-sync-refactor\biss_validation_p0_03_scan` 通过，确认 timeout sample-scan re-arm 和 trace 解码正常。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --validate-resource-owner --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_p0_03_resource_owner` 通过，确认 TriggerFB owner/release 边界未回退。
- 风险：为兼容现有调用，`sync_io.h` 中的 `sync_io_biss_tap_*` API 仍保留，但实现已位于 mode driver；P0-04 拆 `sync_io.c` 时可继续评估是否把这些声明迁入 mode 专用头。
- 后续：进入 P0-04 `sync_io.c` 单体拆分，优先按 mode driver 边界搬迁 SEQ_STEP 和 ENC_COUNT。
- 涉及文件：`components/sync_io/src/sync_io_core_internal.h`，`components/sync_io/src/sync_io.c`，`components/sync_io/inc/sync_io_mode_biss_tap.h`，`components/sync_io/src/sync_io_mode_biss_tap.c`，`components/sync_trigger/src/biss_node_io.c`，`components/sync_trigger/src/trigger_fb.c`，`docs/sync/SYNC_IO_TODO.md`。

### SYNC-PROGRESS-20260708-005 - SEQ_STEP 物理实现搬迁

- TODO task ID：`SYNC-MODE-001`、`SYNC-COMP-001`。

- 目标：推进 `SYNC_IO_ARCH_REVIEW_TODO.md` P0-04，将 SEQ_STEP 的物理 PIO/DMA/IRQ 实现从 `sync_io.c` 搬到 `sync_io_mode_seq_step.c`，但不在本子步骤搬迁 ENC_COUNT。
- 完成：`sync_io_seq_step_t` 状态、`sync_io_seq_step_arm()`、`sync_io_seq_step_disarm()`、index/rollover/runtime/trace 采样和 SEQ DMA IRQ service 已搬迁到 `sync_io_mode_seq_step.c`。
- 完成：`sync_io.c` 保留共享 `sync_io_core_dma_irq_handler()`，由它清 DMA IRQ 后分派到 SEQ mode 的 `sync_io_seq_step_dma_irq_service()` 和现有 ENC_COUNT IRQ service，避免本次改动同时迁移 ENC。
- 完成：`sync_io_core_internal.h` 扩展 SEQ/ENC 共享 DMA IRQ 常量、runtime flag/PIO state 打包 helper 和 SM enabled 查询；mode driver 不直接访问 `sync_io_context_t`。
- 验证：`python -m py_compile tools\sd_board_validate\sd_board_validate.py tools\sd_trace_decode\sd_trace_decode.py tools\biss_board_validate\biss_board_validate.py` 通过。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 build id `20260707162734` 的 factory/update 产物。
- 验证：`picotool load -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录通过；`picotool verify -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` Flash verify 通过。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --validate-resource-owner --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_p0_04_seq_owner` 通过，覆盖 SEQ ARM/DISARM、RESET/FAULT release 和 fault trace decode。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --out-dir build-codex-sync-refactor\biss_validation_p0_04_seq_regression` 通过，确认本次共享 core helper/IRQ 分派调整未回退 BiSS TAP。
- 风险：共享 DMA IRQ 仍服务 SEQ_STEP 和 ENC_COUNT；运行互斥由 TriggerFB/resource owner 保证。ENC_COUNT 尚在 `sync_io.c`，下一步迁移时应把 ENC IRQ service 一并搬到 `sync_io_mode_enc_count.c`。
- 后续：继续 P0-04 的 ENC_COUNT 物理实现搬迁，并复跑资源 owner 中的 ENC ARM/DISARM 板端断言。
- 涉及文件：`components/sync_io/src/sync_io_core_internal.h`，`components/sync_io/src/sync_io.c`，`components/sync_io/src/sync_io_mode_seq_step.c`，`docs/sync/SYNC_IO_TODO.md`。

### SYNC-PROGRESS-20260708-006 - ENC_COUNT 物理实现搬迁与 P0-04 收口

- TODO task ID：`SYNC-MODE-001`、`SYNC-COMP-001`。

- 目标：完成 `SYNC_IO_ARCH_REVIEW_TODO.md` P0-04，将 ENC_COUNT 的物理 PIO/DMA/IRQ 实现从 `sync_io.c` 搬到 `sync_io_mode_enc_count.c`，并保持 TriggerFB 作为唯一资源 owner 边界。
- 完成：`sync_io_enc_count_t` 状态、`sync_io_enc_count_arm()`、`sync_io_enc_count_disarm()`、count/runtime/trace 采样和 ENC DMA IRQ service 已搬迁到 `sync_io_mode_enc_count.c`。
- 完成：`sync_io_core_dma_irq_handler()` 保留在 `sync_io.c`，只负责清 `DMA_IRQ_0` 中断位并分发到 `sync_io_seq_step_dma_irq_service()` 和 `sync_io_enc_count_dma_irq_service()`。
- 完成：`sync_io.c` 不再持有 BiSS TAP、SEQ_STEP、ENC_COUNT 的物理 mode arm/disarm 实现；当前约 772 行，保留 core 初始化、capture、clock、pulse、AUX、trace helper、`sync_io_context_t` 和共享 IRQ 分发。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 build id `20260707163440` 的 factory/update 产物。
- 验证：`python -m py_compile tools\sd_board_validate\sd_board_validate.py tools\sd_trace_decode\sd_trace_decode.py tools\biss_board_validate\biss_board_validate.py` 通过。
- 验证：`picotool load -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录通过；`picotool verify -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` Flash verify 通过。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --validate-resource-owner --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_p0_04_enc_owner` 通过，覆盖 SEQ/ENC/BISS owner、DISARM 释放和 RESET/FAULT release trace。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --out-dir build-codex-sync-refactor\biss_validation_p0_04_enc_regression` 通过，确认共享 IRQ 分发调整未回退 BiSS TAP 默认闭环。
- 风险：SEQ_STEP 和 ENC_COUNT 仍共享 `BOARD_SYNC_OUTPUT_SM`、`DMA_IRQ_0`，运行互斥依赖 TriggerFB/resource owner；该共享关系已作为 P1-01 后续显式资源表/断言任务保留。
- 后续：进入 P1-01，将 PIO instance、SM、DMA channel、IRQ 的互斥关系显式记录到 mode resource 表或验证表，避免后续并发 mode 改动误用共享资源。
- 涉及文件：`components/sync_io/src/sync_io_core_internal.h`，`components/sync_io/src/sync_io.c`，`components/sync_io/src/sync_io_mode_enc_count.c`，`docs/sync/SYNC_IO_TODO.md`。

### SYNC-PROGRESS-20260708-007 - SYNC_IO P1 架构小项收口

- TODO task ID：`SYNC-RES-001`、`SYNC-MODE-001`。

- 目标：按 HAOFV 分层完成 `SYNC_IO_ARCH_REVIEW_TODO.md` P1-01 到 P1-05，保持 TriggerFB 为唯一 owner 边界，不把资源 acquire 下沉到 mode driver。
- 完成：`sync_io_mode_ops_t` 增加 `hw` 元数据，记录 PIO instance、SM mask、DMA channel mask 和 IRQ mask；SEQ_STEP/ENC_COUNT 显式声明共享 `pio1/sm0` 和 `DMA_IRQ_0`，BiSS TAP 声明 `pio2/sm0,2,3`。
- 完成：`trigger_resource_map` 从 mode `.resources` 和 `.hw` 共同派生 `resource_arbiter` mask；`sync_io_core_dma_irq_handler()` 增加 SEQ/ENC 不能同时运行的 ISR 入口断言。
- 完成：RJ45 trigger 按硬件层语义增加 `BOARD_SYNC_RJ45_TRIG_IN_PIN`、`BOARD_SYNC_RJ45_TRIG_OUT_PIN` 和 `BOARD_SYNC_RJ45_TRIGGER_SM`；硬件定义优先，历史 `MARK:*` 兼容命令输出到 `RJ45_TRIG_OUT`，不再定义独立 marker 物理信号。
- 完成：`SYNC_IO_MODE_VOID_DISPATCH()` 宏替代三个 mode wrapper 中重复的 `const void*` 转 typed config 胶水函数。
- 完成：预留 mode (`AUX_DIFF_TRIGGER`、`SELF_CAL`) 在 `sync_io_mode_get_ops()` 中显式返回 NULL，`sync_io_mode_get_by_index()` 只枚举已实现 mode。
- 完成：`TRIG_MODE_BISS_BRIDGE` 保留为 deprecated 兼容别名；真实语义使用 `TRIG_MODE_PROTOCOL_TRIGGER + protocol + biss_role`，Bridge 是 BiSS role 子角色。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 build id `20260707164537` 的 factory/update 产物。
- 验证：`python -m py_compile tools\sd_board_validate\sd_board_validate.py tools\sd_trace_decode\sd_trace_decode.py tools\biss_board_validate\biss_board_validate.py` 通过。
- 验证：`picotool load -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录通过；`picotool verify -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` Flash verify 通过。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --validate-resource-owner --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_p1_sync_arch` 通过，覆盖 mode resource map、SEQ/ENC/BISS owner 和 RESET/FAULT release。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --out-dir build-codex-sync-refactor\biss_validation_p1_sync_arch` 通过，确认 BiSS TAP 和 RJ45 trigger 语义入口未回退。
- 风险：历史 ABI 中仍保留 `MARK:*` / `marker_width_us` 名称；这些名称只表示 RJ45 trigger 兼容入口，不表示独立硬件输出。
- 后续：如需继续清理，可在 SCPI/UI 层新增正式 `RJ45:*` 命令，再把 `MARK:*` 标记为 deprecated 兼容命令。
- 涉及文件：`boards/rp2350_trig/inc/board_config.h`，`components/sync_io/inc/sync_io_hw_profile.h`，`components/sync_io/inc/sync_io_mode.h`，`components/sync_io/src/sync_io.c`，`components/sync_io/src/sync_io_mode_*.c`，`components/sync_trigger/inc/trigger_vector.h`，`components/sync_trigger/src/trigger_resource_map.c`，`docs/sync/SYNC_IO_TODO.md`，`docs/sync/SYNC_IO_ARCHITECTURE.md`，`docs/interface/SCPI_COMMANDS.md`。

### SYNC-PROGRESS-20260708-008 - RJ45_TRIG 硬件定义优先收口

- TODO task ID：`SYNC-PROFILE-001`。

- 目标：按硬件定义优先原则，舍弃独立 `MARKER_OUT` 物理信号，将历史 `MARK:*` 命令收敛为 `RJ45_TRIG_OUT` 兼容入口，避免 `pio1/sm3` 误驱动 AUX3/GPIO29。
- 完成：`BOARD_SYNC_MARKER_OUT_PIN` 改为 deprecated alias，指向 `BOARD_SYNC_RJ45_TRIG_OUT_PIN`；`BOARD_SYNC_AUX3_OUT_PIN` 只表示 AUX3 固定输出，不再承载 marker 语义。
- 完成：`sync_io_init()` 使用 `BOARD_SYNC_RJ45_TRIGGER_SM` + `BOARD_SYNC_RJ45_TRIG_OUT_PIN` 初始化 `pio1/sm3`；旧 `sync_io_fire_marker_*()` 保留为 RJ45 trigger 兼容函数。
- 完成：TriggerFB 的 `TRIG_EVENT_FIRE_MARKER` 直接调用 `sync_io_fire_rj45_trigger_us()`；`trigger_vector.h` 和 `sync_trigger.h` 对历史 marker event/field 增加 deprecated RJ45 compat 注释。
- 完成：同步更新 `SYNC_IO_REFACTOR_PLAN.md`、`SYNC_IO_RESOURCE_PLAN.md`、`SCPI_COMMANDS.md`、`SYNC_IO_ARCH_REVIEW_TODO.md`、HAOFV 文档、BiSS 硬件约束文档和 Trigger 待办，明确 AUX3 不再是 marker 目标。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 build id `20260707170752` 的 factory/update 产物。
- 验证：`python -m py_compile tools\sd_board_validate\sd_board_validate.py tools\sd_trace_decode\sd_trace_decode.py tools\biss_board_validate\biss_board_validate.py` 通过。
- 验证：`picotool reboot -f -u` 后 `picotool load -x build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录并启动应用成功。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --validate-resource-owner --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_rj45_hw_definition` 通过，`summary.txt` 为 `PASS`。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --out-dir build-codex-sync-refactor\biss_validation_rj45_hw_definition` 通过，确认 BiSS TAP 与 RJ45 trigger 语义入口未回退。
- 风险：SCPI/UI/TriggerVector 仍保留 `MARK:*` / `marker_width_us` 历史命名；短期作为 ABI 兼容保留，后续可新增正式 `RJ45:*` 命令再逐步 deprecated。
- 后续：如继续清理命名，应先增加 `RJ45:*` SCPI/UI 入口和状态字段，再保留 `MARK:*` 作为兼容别名，不改动 `GPIO23/RJ45_TRIG_OUT` 硬件定义。
- 涉及文件：`boards/rp2350_trig/inc/board_config.h`，`components/sync_io/inc/sync_io.h`，`components/sync_io/src/sync_io.c`，`components/sync_trigger/inc/sync_trigger.h`，`components/sync_trigger/inc/trigger_vector.h`，`components/sync_trigger/src/trigger_fb.c`，`docs/sync/SYNC_IO_TODO.md`，`docs/sync/SYNC_IO_ARCHITECTURE.md`，`docs/interface/SCPI_COMMANDS.md`，`docs/sync/SYNC_IO_TODO.md`，`docs/sync/SYNC_IO_TASK_PROGRESS.md`，`docs/communication/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`，`docs/communication/BISSC_TAP_BRIDGE_DESIGN.md`，`docs/arch/HAOFV_ARCHITECTURE.md`，`docs/arch/HAOFV_IMPLEMENTATION_PLAYBOOK.md`，`docs/sync/SYNC_IO_ARCHITECTURE.md`，`docs/trigger/TRIGGER_SYNC_TODO.md`。

### SYNC-PROGRESS-20260708-009 - ENC_COUNT 3-pin 软件定义收口

- TODO task ID：`SYNC-MODE-001`、`SYNC-PROFILE-001`。

- 目标：按硬件定义优先原则固定 `GPIO19/RJ45_TRIG_IN`，将 ENC_COUNT 软件定义收口为 A/B/Z=`GPIO16/GPIO17/GPIO18`，避免 ENC 再占用 IN3。
- 完成：`SYNC_IO_HW_ENC_Z_PIN` 改为 `BOARD_SYNC_INPUT_BASE_PIN + 2`；`sync_io_hw_enc_pins_valid()`、TriggerVector 默认值和 `TRIG:ENC:APIN` 事件载荷均使用 A=16、B=17、Z=18。
- 完成：`enc_count.pio` 从 4-pin 采样改为 3-pin contiguous 采样，Z 从 bit2 提取；PIO init/disarm 只初始化和释放 GPIO16..18，不再触碰 GPIO19。
- 完成：`sync_io_enc_count_mode_validate()` 只接受 `in_pin_base=16` 且 Z=`base+2`；`TRIG:ENC:APIN 26` 继续作为关闭能力返回执行错误。
- 完成：同步更新 HAOFV、SYNC_IO、SCPI、BiSS 和 Trigger 文档，明确 `GPIO19` 是 `RJ45_TRIG_IN`，`GATE_IN` 只是模式层解释，ENC Z 不再使用 IN3。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 build id `20260707172833` 的 factory/update 产物。
- 验证：`python -m py_compile tools\sd_board_validate\sd_board_validate.py tools\sd_trace_decode\sd_trace_decode.py tools\biss_board_validate\biss_board_validate.py` 通过。
- 验证：`git diff --check boards/rp2350_trig/inc/board_config.h components/sync_io/inc/sync_io_hw_profile.h components/sync_io/src/enc_count.pio components/sync_io/src/sync_io_mode_enc_count.c components/sync_trigger/inc/trigger_vector.h docs/arch/HAOFV_IMPLEMENTATION_PLAYBOOK.md docs/arch/HAOFV_ARCHITECTURE.md docs/interface/SCPI_COMMANDS.md docs/sync/SYNC_IO_ARCHITECTURE.md docs/sync/SYNC_IO_TODO.md docs/communication/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md docs/communication/BISSC_TAP_BRIDGE_DESIGN.md docs/communication/BISSC_IMPLEMENTATION_TODO.md docs/trigger/TRIGGER_SYNC_TODO.md docs/trigger/TRIGGER_ENC_COUNT_DESIGN.md docs/trigger/TRIGGER_PULSE_COUNT_ANALYSIS.md` 通过，仅有既有 CRLF warning。
- 验证：`picotool reboot -f -u` 后 `picotool load -x build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录并启动应用成功；板端 `SYST:FW:BUILD?` 返回 `"20260707172833"`。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --validate-resource-owner --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_enc_3pin_pinout_final` 通过，`summary.txt` 为 `PASS`。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --out-dir build-codex-sync-refactor\biss_validation_enc_3pin_pinout_final` 通过，确认 BiSS TAP 未被 ENC pinout 调整回退。
- 验证：板端 `TRIG:ENC:APIN?` 返回 `16,17,18`；执行 `TRIG:ENC:APIN 26` 后 `SYST:ERR?` 返回 `-200,"Execution error"`，再次查询仍为 `16,17,18`。
- 风险：`docs/archive/TASK_PROGRESS.md` 中仍保留迁移前历史记录的旧 ENC 16/17/19 描述；按文档规则该文件作为全局历史保留，不作为当前硬件约束入口。
- 后续：如继续推进 P2 自检，应在板端闭环脚本中增加 ENC A/B/Z loopback 或外部回放验证，覆盖真实 A/B/Z 脉冲输入，而不仅是 SCPI 配置与资源 owner 断言。
- 涉及文件：`boards/rp2350_trig/inc/board_config.h`，`components/sync_io/inc/sync_io_hw_profile.h`，`components/sync_io/src/enc_count.pio`，`components/sync_io/src/sync_io_mode_enc_count.c`，`components/sync_trigger/inc/trigger_vector.h`，`components/sync_trigger/src/trigger_fb.c`，`docs/sync/SYNC_IO_TODO.md`，`docs/sync/SYNC_IO_ARCHITECTURE.md`，`docs/interface/SCPI_COMMANDS.md`，`docs/arch/HAOFV_ARCHITECTURE.md`，`docs/arch/HAOFV_IMPLEMENTATION_PLAYBOOK.md`，`docs/sync/SYNC_IO_TASK_PROGRESS.md`。

### SYNC-PROGRESS-20260708-010 - SYNC_CLK_OUT AUX2 运行路径迁移

- TODO task ID：`SYNC-PROFILE-001`。

- 目标：完成 P2-04 中 `SYNC_CLK_OUT` 从旧 GPIO22/`pio1/sm1` 到 AUX2/GPIO28/`pio2/sm2` 的运行路径迁移，保持硬件定义优先。
- 完成：`BOARD_SYNC_SYNC_CLK_OUT_PIN` 解析到 `BOARD_SYNC_AUX_SYNC_CLK_OUT_PIN`；新增 `BOARD_SYNC_MODE_OUT2_PIN` 表达 GPIO22 仍是主口 OUT2/模式本地输出。
- 完成：`sync_io_start_clock()` 改用 `BOARD_SYNC_PIO_AUX`、`BOARD_SYNC_AUX2_SM`、`BOARD_SYNC_AUX_SYNC_CLK_OUT_PIN`，并在启动期间持有 `PIO2 + AUX` 资源；停止时释放资源并恢复 AUX2 输入安全态。
- 完成：TriggerFB 在 `OUTP:CLOC:*` 对应事件中检查 `sync_io_start_clock()` 结果，失败时同步真实 clock 状态并设置 `TRIG_ERROR_RESOURCE_CONFLICT` 或 `TRIG_ERROR_IO_ARM_FAILED`。
- 完成：`sync_io_hw_profile.h` 增加主口、RJ45_TRIG_IN/OUT、ARM_IN、EXT_CLK_IN、SYNC_CLK_OUT、AUX3 和 deprecated marker alias 的编译期断言。
- 验证：`cmake --build build-codex-rj45-interface` 通过，生成 factory/update 产物。
- 风险：`ARM_IN`、`EXT_CLK_IN` 仍是语义占位，旧低层宏只做 pull-down/诊断采样；后续需要迁移到 AUX0/AUX1 并接入 TriggerFB 资格/外部时钟逻辑。
- 涉及文件：`boards/rp2350_trig/inc/board_config.h`，`components/sync_io/inc/sync_io_hw_profile.h`，`components/sync_io/src/sync_io.c`，`components/sync_trigger/src/trigger_fb.c`，`docs/interface/SCPI_COMMANDS.md`，`docs/sync/SYNC_IO_ARCHITECTURE.md`，`docs/trigger/TRIGGER_SYNC_TODO.md`，`docs/sync/SYNC_IO_TODO.md`，`docs/communication/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`，`docs/sync/SYNC_IO_TASK_PROGRESS.md`。

### SYNC-PROGRESS-20260815-003 - ModelTurntableAO PIO 预约输出首版

- TODO task ID：`SYNC-OUT-001`。

- 目标：完成当前 P0 的第一段闭环，让 `ModelTurntableAO` 不再依赖 `time_us_64()` 主循环软件翻 GPIO 输出脉冲，而是生成 bounded pulse plan，由 `sync_io` realtime primitive 使用 PIO/DMA 到点输出边沿。
- 完成：新增 `sync_io_model_sched.c`，提供 `sync_io_model_pulse_schedule_arm/disarm/is_running/get_runtime`；首版使用 `pio1/sm1 + DMA2`，1 MHz tick，支持 `delay_us`、`high_us`、上升/下降有效边沿、最多 256 个脉冲和 PIO/DMA/FIFO runtime snapshot。
- 完成：`sync_io.pio` 增加 active-high / active-low 两个预约脉冲程序；`board_config.h` 明确 `BOARD_SYNC_MODEL_SCHED_SM=1`，与产品主触发 `pio1/sm0` 隔离。
- 完成：`ModelTurntableAO` 改为在 start 时根据扫描起止、步长、速度/加速度和脉宽生成计划并提交给 `sync_io`；service 只读取 runtime snapshot 更新 emitted/phase/current_position，不再直接写 debug GPIO。
- 验证：`cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`。
- 风险：本轮只完成编译闭环，尚未烧录和示波/loopback 验证真实边沿；late/drop/overflow 计数、RESET 统一 release、Host C 计划生成断言仍在 P0/P3 待办。
- 后续：优先补单板或两板 loopback HIL，捕获模型转台输出脉冲数量、宽度、完成态和 abort；随后再推进 RefMem 真实最小 transport，不继续扩大静态表模型。
- 涉及文件：`CMakeLists.txt`，`boards/rp2350_trig/inc/board_config.h`，`components/model_turntable/src/model_turntable.c`，`components/sync_io/inc/sync_io.h`，`components/sync_io/src/sync_io.c`，`components/sync_io/src/sync_io.pio`，`components/sync_io/src/sync_io_core_internal.h`，`components/sync_io/src/sync_io_model_sched.c`，`docs/sync/SYNC_IO_ARCHITECTURE.md`，`docs/sync/SYNC_IO_TODO.md`，`docs/sync/SYNC_IO_TASK_PROGRESS.md`。

## 失败与回退

当前文档重构不改变固件运行态，无代码回退动作。后续 persona 迁移如果出现 PIO instruction
space 不足、SM/DMA 冲突、GPIO function 被 analyzer 接管、TDMA cycle/CRC 回归或 capture
不可定位丢失，必须停止新 persona、保存 snapshot 和原始波形，并恢复最近通过统一硬件验收
的资源配置。不得用旧 PIO1 wave handoff 掩盖 PIO0 persona manager 的缺口。

## 下一 Gate

先完成 `SYNC-DOC-001` 的文档门禁和提交，再进入 `SYNC-RES-001`。`SYNC-RES-001` 只建立
descriptor、容量计算和冲突负测，不同时搬迁输出 PIO；通过快速 TDMA 短帧闭环后，才进入
`SYNC-RES-002` 生命周期实现。
