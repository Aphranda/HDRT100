# VDC 内部主域任务进度

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_TASK_PROGRESS.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`
Last updated: 2026-08-16

本文档记录 Virtual Distributed Clock / VDC Domain 的阶段性任务进度、验证结果和后续动作。待办事项放在 `VDC_DOMAIN_TODO.md`，本文只记录已经发生的工作和可回溯结果。

## 记录规则

每条任务记录使用以下格式：

```text
### VDC-TASK-YYYYMMDD-NNN - 标题

- 状态：
- 日期：
- 任务目标：
- 完成内容：
- 验证结果：
- 还需完成：
- 关联文件：
- 下一步：
```

## 当前目标

VDC Domain 当前目标是把 VDC/DPLL 从同步域中的基础算法，升级为 HAOFV 内部基础主域：

```text
VdcSyncAO
+ SyncDpllFB
+ HoldoverFB / RelockFB
+ VdcVector
+ VdcClockModel
+ VdcQualityTable
+ TimestampDictionary
```

首阶段先完成文档主域和架构边界，并把现有 RefMem/TDMA 诊断字段收敛到不会冒充 100 ns DPLL evidence 的代码形态。

## 任务记录

### VDC-TASK-20260816-021 - Sync IO core1 latch observer source

- 状态：完成代码、host/build 和 COM5/COM6 HIL。
- 日期：2026-08-16
- 任务目标：
  - 将 VDC observer 的输入源从直接读取 PIO raw FIFO，改为读取 SYNC_IO owner 发布的 core1 latched capture fact。
  - 保持 HAOFV 边界：SYNC_IO/core1 只产本地 IO timestamp fact；VDC manager 只做 adapter 和提交；VDC domain 继续执行 dictionary/wrap/gate/DPLL admission。
- 完成内容：
  - `app_realtime_run_once()` 调用 `sync_io_capture_latch_service_core1()`，让 core1 realtime loop 负责搬运 capture FIFO。
  - `sync_io_capture_latched_word_t` 携带 raw word、sample seq、base time、sample period、timestamp source/resolution/flags 和 drop evidence。
  - `vdc_dpll_manager_sync_io_observer_service()` 改为读取 `sync_io_read_capture_latched()`；last timestamp evidence 使用 latch fact 的 source/resolution/flags，dictionary 只补节点和 payload 语义。
  - 新增 `REALtime:IO:SAMPle:LATCh?` 只读查询，给 HIL 读取 latch source/resolution 和 drop 证据。
- 验证结果：
  - `python -m py_compile tools\vdc_latch_validate\vdc_latch_validate.py tools\vdc_observer_validate\vdc_observer_validate.py tools\realtime_scpi_validate\realtime_scpi_validate.py` 通过。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run` 生成 66 条，包含 `REALtime:IO:SAMPle:LATCh?`。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，generated=128。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id `20260816034347`，OTA package CRC `0x962F5B65`。
  - COM5/COM6 均 OTA 到 build `20260816034347` 并 commit；两板 `SYSTem:ERRor?` 均为 `0,"No error"`。
  - `vdc_latch_validate.py` 通过：COM5 `latched=117->270, observer_raw=31->307`，COM6 `latched=120->272, observer_raw=31->310`，两板 timestamp source/resolution 均为 `1 / 1000 ns`。
  - `vdc_observer_validate.py` 通过，两板 `schedule_crc32=974530568`、`dictionary_crc32=1814735745`。
- 还需完成：
  - 把 phase 1 的 `SOFTWARE_US / 1000 ns / DIAGNOSTIC_ONLY` latch 升级为 PIO/DMA/IRQ/core1 hardware tick latch。
  - 在 COM5/COM6 上补启用态 HIL，记录 latch counter、observer submitted/accepted/rejected、timestamp source/resolution 和 gate result。
- 关联文件：
  - `components/sync_io/inc/sync_io.h`
  - `components/sync_io/src/sync_io.c`
  - `application/src/app.c`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `middleware/scpi_port/inc/scpi_realtime_io_commands.h`
  - `middleware/scpi_port/src/scpi_realtime_io_commands.c`

### VDC-TASK-20260816-017 - Manager-side Sync IO observer pump

- 状态：完成 host/build 验证和 COM5/COM6 默认 observer 查询；默认关闭，真实 PIO/DMA timestamp latch 和启用态 HIL 待后续实现
- 日期：2026-08-16
- 任务目标：
  - 将 `sync_io_read_capture_words()` 到 VDC compact observation 的任务接线落到 manager 层。
  - 保持 HAOFV 边界：SYNC_IO 只产 raw IO fact，VDC manager 负责适配和提交，VDC domain 负责 dictionary/wrap/gate/DPLL admission。
- 完成内容：
  - `vdc_dpll_manager` 增加 `vdc_dpll_manager_sync_io_observer_config_t` 和 status snapshot。
  - observer 默认关闭；启用时读取 bounded raw word batch，调用 `vdc_sync_io_capture_word_to_compact_observation()`，再提交 `vdc_domain_submit_compact_observation()`。
  - status 记录 raw/no-edge/ambiguous/bad-argument/submitted/accepted/rejected 计数，以及 last raw word、event id、tick_l32 和 gate reject code。
  - observer 不自动启动 SYNC_IO capture，不使用 `board_uptime_ms()` 构造 DPLL timestamp；timestamp base、sample period、event id、frame CRC 必须由上游显式配置。
  - 新增 `SYSTem:SYNC:VDC:OBServer?` 只读维护查询，用于 HIL 读取 observer status；查询不启动 capture、不投递样本、不改变 DPLL。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，generated=127。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有既有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，最新生成 build id `20260816024745`，package CRC `0x028BC853`。
  - COM5/COM6 均通过 OTA 更新并 commit 到 build `20260816024745`；两板 `SYST:OTA:STAT?` 均返回 `"COMMITTED",1,"NONE",5`。
  - COM5 查询 `SYST:SYNC:VDC:OBServer?` 返回 18 个零字段，符合默认 disabled observer；`SYST:SYNC:VDC:STAT?` 返回 `1,1,170836,3552,106131,170836`，错误队列为 `0,"No error"`。
  - COM6 查询 `SYST:SYNC:VDC:OBServer?` 返回 18 个零字段，符合默认 disabled observer；`SYST:SYNC:VDC:STAT?` 返回 `1,1,50875,3554,34170,50875`，错误队列为 `0,"No error"`。
- 还需完成：
  - 增加启用态 HIL 报告，输出 observer status、dictionary CRC、profile CRC、timestamp source/resolution 和 gate evidence。
  - 将真实 PIO/DMA/core1 timestamp latch 接入 observer 配置来源，避免人工配置 base tick。

### VDC-TASK-20260816-018 - VDC observer maintenance configuration

- 状态：完成代码、文档、build 和两板启停态验证。
- 本轮完成：
  - 新增 `SYSTem:SYNC:VDC:OBServer` 维护配置命令；无参数或 `enabled=0` 关闭 observer 并重置 status。
  - 启用态参数进入 `vdc_dpll_manager_configure_sync_io_observer()`，复用 manager 合法性检查：batch 上限、event id、observed mask、sample period、frame CRC 和初始 mask。
  - 保持 HAOFV 边界：SCPI 只写维护配置，不启动 SYNC_IO capture，不提交伪造 timestamp，不越过 VDC dictionary/gate。
- 验证：
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 生成 128 条，包含 `SYSTem:SYNC:VDC:OBServer -> 1`。
  - `tools\tests\run_vdc_domain_tests.ps1` 通过；`tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 17/17 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，build `20260816030427`，OTA package CRC `0x5D46BBBD`。
  - COM5/COM6 均 OTA boot/commit 到 build `20260816030427`。
  - COM5/COM6 启用 `SYST:SYNC:VDC:OBServer 1,1,1,2,1,0,0,1000,0,0,1` 后查询得到 `enabled=1,max_words_per_service=1`；关闭后 observer 查询回到 disabled 全零字段，错误队列均为 `0,"No error"`。
- 下一步：
  - 在真实 timestamp latch/dictionary 配置到位后，再验证 raw/submitted/accepted/rejected 计数变化。

### VDC-TASK-20260816-019 - VDC observer evidence snapshot fields

- 状态：完成代码、文档、build 和 COM5/COM6 查询验证。
- 本轮完成：
  - `vdc_dpll_manager_sync_io_observer_status_t` 追加 observer 配置字段和 VDC 证据字段。
  - manager 在 capture word 解析出 compact observation 时记录 `last_edge_index`，并尝试通过 active `VdcTimestampDictionary` 展开 timestamp source/resolution/flags、source/reference slot 和 payload class。
  - `SYSTem:SYNC:VDC:OBServer?` 保持原 18 字段前缀不变，后续追加 22 个证据字段，合计 40 字段。
- 验证：
  - `product_scpi_validate.py --dry-run` 生成 128 条，`OBServer?` 解析为 40 字段。
  - VDC 单测通过，host 17/17 通过，docs check 通过但保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - build `20260816031400`，OTA package CRC `0x0F669557`。
  - COM5/COM6 均独立查询确认 committed：`SYST:FW:BUILD? -> "20260816031400"`、`SYST:OTA:STAT? -> "COMMITTED",1,"NONE",5`。
  - COM5/COM6 启用最小合法 observer 后查询到 `schedule_crc32=974530568`、`dictionary_crc32=1814735745`、`dictionary_profile_crc32=974530568`；关闭后 40 字段全零。
- 注意：
  - `ota_boot_commit.py` 在本轮两块板上都被启动日志污染输出误判失败，独立 `scpi_query` 已确认 commit 实际成功；后续可优化该脚本的启动日志过滤。

### VDC-TASK-20260816-020 - VDC observer HIL validator script

- 状态：完成脚本固化和 COM5/COM6 验证。
- 完成：
  - 新增 `tools/vdc_observer_validate/vdc_observer_validate.py`，统一串口生命周期、关闭态检查、启用态配置、40 字段解析、schedule/dictionary CRC 校验、最终关闭和错误队列检查。
  - 更新 `tools/README.md`，把该脚本列入闭环验证工具。
- 验证：
  - `python tools\vdc_observer_validate\vdc_observer_validate.py COM5 COM6 --expected-build 20260816031400` 通过。
  - COM5/COM6 均返回 `schedule_crc32=974530568`、`dictionary_crc32=1814735745`。
  - COM5/COM6 验证 raw word -> compact observation -> VDC gate 的板端证据。
- 关联文件：
  - `components/vdc_dpll_manager/inc/vdc_dpll_manager.h`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `middleware/scpi_port/inc/scpi_sync_commands.h`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/interface/SCPI_COMMAND_PLAN.md`
  - `docs/sync/SYNC_IO_TODO.md`
  - `docs/sync/SYNC_IO_TASK_PROGRESS.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
- 下一步：
  - 优先补真实 timestamp latch 或维护查询；在该证据出现前，observer 不能被当作 100 ns DPLL lock 已闭环。

### VDC-TASK-20260816-016 - Sync IO capture adapter contract

- 状态：完成 host/build 验证；`sync_io_read_capture_words()` 到 adapter 的任务接线和板端 HIL 待后续实现
- 日期：2026-08-16
- 任务目标：
  - 参考 RefMem 的适配层思路，在 VDC 侧建立 raw capture word 到 compact observation 的稳定 contract。
  - 保持 HAOFV 边界：adapter 只解析边沿和生成 compact fact，不访问 `sync_io` 内部状态，不写 DPLL offset/rate/lock。
- 完成内容：
  - 新增 `vdc_sync_io_adapter.h/.c`。
  - `vdc_sync_io_capture_word_to_compact_observation()` 支持 8 个 4-bit sample word、observed mask、rising/falling event id、sample period、base time 和 expected window。
  - adapter 输出 `VdcCompactObservationSample`，由 VDC active dictionary 决定 source/resolution/flags；adapter 本身不声明样本可进入 DPLL。
  - 单元测试覆盖 rising edge 解码、no edge、ambiguous edge，以及 adapter 输出通过 `vdc_domain_submit_compact_observation()` 进入 VDC gate。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过。
- 还需完成：
  - 在 `task_vdc_sync` 或 core1 realtime 边界读取 `sync_io_read_capture_words()` / capture ring，将真实 word 投递到 adapter。
  - 对 COM5/COM6 增加 HIL 证据：raw word、edge index、event id、tick_l32、dictionary CRC、gate result。
- 关联文件：
  - `components/vdc_domain/inc/vdc_sync_io_adapter.h`
  - `components/vdc_domain/src/vdc_sync_io_adapter.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 接实际 capture source，但仍不得把默认 1 MHz capture 或软件时间戳冒充为 100 ns hardware evidence。

### VDC-TASK-20260816-015 - Compact observation evidence gate

- 状态：完成 host/build 验证；真实 PIO/DMA capture ring 尚未接入
- 日期：2026-08-16
- 任务目标：
  - 参考 RefMem 的表契约思路，把 capture fact 进入 VDC 的路径收敛为 active dictionary + wrap tracker + evidence gate，而不是让 realtime IO 直接构造 DPLL sample。
  - 保持 HAOFV 边界：VDC owner 展开 timestamp fact 并执行 gate；`sync_io`/core1 只提供 compact observation，不写 offset/rate/lock。
- 完成内容：
  - 新增 `VdcCompactObservationSample`，作为 PIO/DMA/core1 capture fact 的最小载荷：`sample_seq`、`event_id`、`tick_l32`、expected window、CRC 和质量字段。
  - `vdc_domain_context_t` 增加 active `VdcTimestampDictionary` 与 `VdcWrapTracker`。
  - 新增 `vdc_domain_publish_timestamp_dictionary()`，要求 dictionary CRC、版本和 profile CRC 与 active TDMA schedule 匹配，发布时重置 wrap tracker。
  - 新增 `vdc_domain_expand_compact_observation()`，按 dictionary 展开 event/source/resolution/flags，按 wrap tracker 扩展 tick，再生成 `VdcTDMATimestampEvidence` 并经过 observation window gate。
  - 新增 `vdc_domain_submit_compact_observation()` 和 `vdc_dpll_manager_submit_compact_observation()` wrapper，后续 `task_vdc_sync` 可通过 manager 投递 capture fact。
  - 单元测试覆盖 compact observation 展开、dictionary CRC 拒绝、stale tick 拒绝、context submit accepted/rejected 计数。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过。
- 还需完成：
  - 将 `sync_io` / PIO capture ring 的 raw word 转换为 `VdcCompactObservationSample`。
  - 增加板端 COM5/COM6 HIL：真实 PIO observation sample 报告 source、resolution、late、phase error、CRC 和 gate result。
- 关联文件：
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `components/vdc_dpll_manager/inc/vdc_dpll_manager.h`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 进入 `sync_io` capture word 到 `VdcCompactObservationSample` 的最小接线，仍不得把软件时间戳标成 100 ns hardware evidence。

### VDC-TASK-20260816-014 - Timestamp dictionary and wrap tracker contract

- 状态：完成 host/build 验证；capture ring 的完整 `seq_delta` 扩展待后续实现
- 日期：2026-08-16
- 任务目标：
  - 冻结 compact timestamp 展开和 32 位 tick 回绕扩展的基础规则，避免后续 PIO latch 直接把裸 event/tick 写进 DPLL。
  - 保持 HAOFV 边界：dictionary 和 wrap tracker 只生成 timestamp sample 的事实字段，不承载业务数据，不写 VDC offset/rate/lock。
- 完成内容：
  - `vdc_timestamp.h/.c` 增加 `VdcTimestampDictionary`：`event_id`、source/reference slot、source、resolution、default flags、port/signal 和 payload class。
  - dictionary 增加版本号、entry_count、profile CRC、dictionary CRC、entry 有效性和 event id 唯一性校验。
  - 新增 `vdc_timestamp_dictionary_apply()`，把 compact latch sample 按 dictionary 展开为正式 timestamp source/resolution/flags/slot。
  - 新增 `VdcWrapTracker`，支持 `tick_l32` 正向扩展、正向回绕识别、stale/pre-wrap 拒绝和小幅倒退容差。
  - 单元测试覆盖 dictionary CRC 篡改、重复 event id、compact sample 展开、tick 回绕和 stale/backward 拒绝。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过。
- 还需完成：
  - 将 PIO/DMA capture ring 的 compact event/tick 接入 dictionary + wrap tracker。
  - 增加 System Pack / SD / SCPI profile loader 对 timestamp dictionary 的导入、激活和回滚。
- 关联文件：
  - `components/vdc_domain/inc/vdc_timestamp.h`
  - `components/vdc_domain/src/vdc_timestamp.c`
  - `tests/unit/test_vdc_domain.c`
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
- 下一步：
  - 继续 `VDC_OBSERVATION_WINDOW` 的 hardware latch bring-up，先把 capture ring sample 接到该基础件，再进入 DPLL admission。

### VDC-TASK-20260816-013 - Timestamp contract split

- 状态：完成 host/build 验证；真实 PIO/DMA timestamp latch 待后续实现
- 日期：2026-08-16
- 任务目标：
  - 将 DPLL timestamp admission 的基础规则从 `vdc_domain.c` 单体中拆出，形成后续硬件 latch、timestamp dictionary 和 wrap tracker 的接入口。
  - 保持 HAOFV 边界：timestamp 基础件只判断 source/resolution/flag/window，`SyncDpllFB` / VDC owner 仍是 offset/rate/lock 唯一 writer。
- 完成内容：
  - 新增 `vdc_timestamp.h/.c`，定义 `SOFTWARE_US`、`HARDWARE_TICK`、`DIAGNOSTIC_ONLY`、`DPLL_ELIGIBLE`、latch sample 和 admission code。
  - `vdc_domain_validate_tdma_timestamp_evidence()` 改为调用 `vdc_timestamp_dpll_admission_check()`；诊断时间戳、非硬件 tick 和超出 `100 ns` 分辨率的样本仍被拒绝进入 DPLL。
  - `vdc_timestamp_observed_in_window()` 统一 expected/observed window + guard 判断，为后续 PIO capture ring 映射提供公共规则。
  - 单元测试增加 timestamp helper 覆盖：软件诊断样本拒绝、硬件 tick 诊断标志拒绝、分辨率超限拒绝、`<=100 ns` 硬件样本通过。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过。
- 还需完成：
  - 接入真实 PIO/DMA/IRQ/core1 timestamp latch，产出 `HARDWARE_TICK / <=100 ns / DPLL_ELIGIBLE` 的 observation sample。
  - 冻结 `VdcTimestampDictionary` 和 `VdcWrapTracker`，把 compact timestamp 展开为节点、端口、信号语义和 64 位本地 tick。
- 关联文件：
  - `components/vdc_domain/inc/vdc_timestamp.h`
  - `components/vdc_domain/src/vdc_timestamp.c`
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 继续 P3/P4，按 HAOFV 边界把硬件 capture ring 和 `VDC_OBSERVATION_WINDOW` sample bring-up 接到该 contract。

### VDC-TASK-20260816-012 - DPLL sample commits VDC clock and DCO snapshot

- 状态：完成 host/build 验证；真实 servo 和硬件 timestamp latch 待后续实现
- 日期：2026-08-16
- 任务目标：
  - 让 DPLL accepted sample 形成实质性的 VDC clock model 输出，而不是只推进 lock state 计数。
  - 保持 HAOFV 边界：`SyncDpllFB` / VDC owner 是 `offset/rate/lock` 唯一 writer，RefMem 和 core1/PIO 不写 clock model。
- 完成内容：
  - `vdc_dpll_state_t` 增加 `last_frequency_error_ppb`、`last_expected_window_start_ns` 和 `last_observed_time_ns`。
  - accepted hardware sample 后，VDC owner 根据 `phase_error_ns` 更新 `VdcClockModel.phase_offset_ns`，并同步派生 `VdcDcoControl`。
  - 连续样本之间通过 expected/observed period delta 估算 `frequency_error_ppb`，按 servo sanity limit 限幅，并写入 `clock.period_adjust_ppb` 和 `VdcErrorBudget.freq_offset_ppb/freq_skew_ppb`。
  - 单元测试覆盖 phase offset 写入 clock/DCO，以及 10 ms 样本间隔下的 100 ppb frequency error 估算。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815192045`，package CRC `0xFC02E459`。
- 还需完成：
  - 将该首版 period/phase 更新替换或升级为 `SyncDpllFB` 内部 PI/linreg servo。
  - 增加 outlier gate、servo reset、step/slew policy 和 HOLDOVER/RELOCK 状态迁移。
  - 接入硬件 timestamp latch 后，禁止诊断 timestamp 进入该更新路径。
- 关联文件：
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 提交推送后进入硬件 timestamp latch 或 `SyncDpllFB` 组件化拆分。

### VDC-TASK-20260816-011 - RefMem TDMA consumes VDC data window plan

- 状态：完成 host/build 验证；COM5/COM6 HIL 待后续烧录轮次执行
- 日期：2026-08-16
- 任务目标：
  - 让 RefMem data frame 成为 VDC/TDMA schedule 上的 payload，而不是收到 intent 后立即执行。
  - 保持 HAOFV 边界：RefMem 不计算 TDMA 相位，只向 VDC manager 请求 `REFMEM_DATA_WINDOW` 计划；core1 realtime 只消费计划并执行窗口。
- 完成内容：
  - `refmem_realtime_tdma_intent_config_t` 增加 VDC window plan 字段：plan valid、window class、schedule CRC、window start/end、guard start/end。
  - `refmem_realtime_tdma_snapshot_t` 增加窗口计划和执行证据：window miss count、wait_ns、late_ns、window/guard 起止。
  - `refmem_realtime_tdma_core1_service()` 在未到 guard 前保持 pending 并返回 `WAITING_FOR_WINDOW`；错过 payload window 或 guard end 时返回 `WINDOW_MISSED`；进入 payload window 后才调用 TX/RX ops。
  - `DistributedRefMemAO` 的 NodeLoad AUTO TX/RX 提交前调用 `vdc_dpll_manager_plan_tdma_window(VDC_DOMAIN_WINDOW_REFMEM_DATA, ...)`，把 VDC-owned data window plan 下发给 core1 service。
  - `SYSTem:REFMEM:SYNC:TDMA:STATus?` 追加 VDC window plan 和 wait/late/miss evidence 字段，保留旧字段顺序。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815191550`，package CRC `0x25D841EE`。
- 还需完成：
  - COM5/COM6 烧录后验证 NodeLoad AUTO 在真实 PIO 25 MHz 环路中产生 `vdc_window_plan_valid=1`、wait/late/miss evidence。
  - 将同一机制扩展到 `VDC_OBSERVATION_WINDOW` 的 SYNC sample，并接硬件 timestamp latch。
  - 同步完整 SCPI 指令表中的 `SYSTem:REFMEM:SYNC:TDMA:STATus?` 追加字段。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_realtime_tdma.h`
  - `components/distributed_refmem/src/refmem_realtime_tdma.c`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_realtime_tdma.c`
- 下一步：
  - 提交推送后进入硬件 timestamp latch / observation window sample bring-up。

### VDC-TASK-20260816-010 - VDC quality table and error budget snapshot

- 状态：完成 host/build 验证；板端查询待后续 HIL 轮次执行
- 日期：2026-08-16
- 任务目标：
  - 按 HAOFV 边界补齐 VDC 自有的同步时钟健康评估机制。
  - 明确 TDMA sample 的 accepted/rejected、timestamp resolution、freshness、offset、jitter 和 gate reject 由 VDC owner 形成质量事实。
  - 避免 RefMem payload 同步成功被误当作 VDC/DPLL 健康结论。
- 完成内容：
  - `vdc_domain_snapshot_t` / `vdc_domain_context_t` 增加 `VdcQualityTable` 和 `VdcErrorBudget`。
  - `vdc_domain_submit_tdma_evidence()` 在 DPLL sample 通过或拒绝时同步更新 health state、sample counter、consecutive good/bad、timestamp source/resolution/flags、offset RMS/max、jitter、path delay、dispersion 和 root distance。
  - `vdc_domain_service()` 增加 sample age 刷新，形成 `last_sample_age_1e3ns`，为后续 HOLDOVER / stale / RUN gate 提供基础字段。
  - `vdc_dpll_manager_get_snapshot()` 提供只读 VDC snapshot 入口，保持 SCPI 读取 snapshot，不直接访问 VDC context。
  - `READ:SYNC:QUALity?` 从固定回复改为读取 VDC quality/error budget snapshot。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815190808`，package CRC `0xF1ACEE53`。
- 还需完成：
  - 将 `READ:SYNC:QUALity?` 的更新字段同步到 SCPI 指令表。
  - 接真实 DPLL servo 后补 frequency error、outlier gate 和 rate/skew 统计。
  - 将 VDC quality/error budget 接入 RUN gate 和 RefMem VdcSlot。
- 关联文件：
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `components/vdc_dpll_manager/inc/vdc_dpll_manager.h`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 提交推送后进入 TDMA plan 被 core1/PIO 消费的 P0 项。

### VDC-TASK-20260816-009 - VDC TDMA window planner contract

- 状态：完成并已通过 COM5/COM6 板端查询和两板 HIL
- 日期：2026-08-16
- 任务目标：
  - 为 core1/PIO 后续按 `VdcTdmaScheduleProfile` 等待 `REFMEM_DATA_WINDOW` / `VDC_OBSERVATION_WINDOW` 提供 VDC-owned 计划契约。
  - 避免 RefMem 或 SCPI 自行计算窗口相位，保持 VDC 拥有 schedule/window/gate 语义。
- 完成内容：
  - 新增 `vdc_tdma_window_plan_t`，字段覆盖 `now_ns`、窗口起止、guard 起止、`wait_ns`、`late_ns`、inside/missed 标志、slot/reference 和 schedule CRC。
  - 新增 `vdc_domain_plan_tdma_window()`，从 active profile 计算当前或下一 TDMA window；窗口已错过 guard 后自动转入下一周期并记录 `missed_current_window`。
  - `vdc_dpll_manager` 增加只读 wrapper，SCPI 继续通过 manager 读取 VDC 计划，不直接访问内部 context。
  - 新增维护命令 `SYSTem:SYNC:VDC:TDMA:PLAN? [window_class],[now_ns_lo],[now_ns_hi]`，默认查询 `REFMEM_DATA_WINDOW` 当前窗口计划。
  - 同步更新 `SCPI_COMMANDS.md`、`SCPI_COMMAND_PLAN.md` 和 VDC 架构/TODO。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815185032`，package CRC `0xB6C4C634`。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - OTA 到 COM5/COM6 后查询 `SYST:FW:BUILD?` 均为 `"20260815185032"`，`SYST:OTA:STAT?` 均为 `"COMMITTED"`。
  - COM5/COM6 执行 `SYSTem:SYNC:VDC:TDMA:PLAN? 2,15000,0` 均返回默认 `REFMEM_DATA_WINDOW`：`window_start_ns=20000`、`window_end_ns=820000`、`guard_start_ns=19000`、`guard_end_ns=821000`、`wait_ns=5000`、schedule CRC `974530568`。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_spi_hil_vdc_plan` 通过；报告 build A/B 均为 `"20260815185032"`，`failures=[]`。
- 还需完成：
  - 让 `refmem_realtime_tdma` / core1 PIO service 消费该 plan，真正按 data/observation window 执行 TX/RX 和 timestamp latch。
- 关联文件：
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `components/vdc_dpll_manager/inc/vdc_dpll_manager.h`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `middleware/scpi_port/inc/scpi_sync_commands.h`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 增加 TDMA service 对 window plan 的消费字段和执行前等待策略。

### VDC-TASK-20260816-008 - COM5/COM6 VDC data-window diagnostic gate

- 状态：完成并已通过 COM5/COM6 HIL；正式 schedule wait / hardware latch 待实现
- 日期：2026-08-16
- 任务目标：
  - 复盘 COM5/COM6 `refmem_spi_hil_validate.py` 失败原因，区分真实物理链路问题和 VDC schedule/window 问题。
  - 保持 HAOFV 边界：RefMem 只提供 frame/payload/CRC/timestamp evidence，VDC gate 负责 schedule/window 判定，DPLL 不消费诊断 timestamp。
- 完成内容：
  - 确认 COM5/COM6 线序检测、25 MHz PIO TDMA RAW/HELLO/EPOCH/DELTA 均已通过，失败点为 `ACK_NACK` 的 VDC gate `BAD_FRAME`。
  - 失败原因收敛为当前 TDMA service 收到 intent 后立即执行，尚未按 `VdcTdmaScheduleProfile` 等待 `REFMEM_DATA_WINDOW`；诊断 timestamp 落在默认 data window 外时会被 VDC gate 拒绝。
  - `refmem_vdc_bridge` 的 `frame_seq/sample_seq` 改为 TDMA `completed_seq`，避免把 RefMem 业务 `seq32/ack_seq32` 和物理 TDMA frame evidence 混用。
  - HIL 脚本保留对 RefMem RX、frame CRC、payload CRC、payload class 和 source slot 的检查；若 VDC gate 因窗口相位拒绝诊断样本，记录为“diagnostic timestamp outside active VDC data window”，不再误判为物理链路失败。
- 验证结果：
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_vdc_bridge_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815183936`，package CRC `0x690AF5D4`。
  - OTA 到 COM5/COM6 后查询 `SYST:FW:BUILD?` 均为 `"20260815183936"`，`SYST:OTA:STAT?` 均为 `"COMMITTED"`。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_spi_hil_vdc` 通过；报告确认 25 MHz PIO TDMA RAW/HELLO/EPOCH/DELTA/ACK/FENCE/QUALITY 双向链路成功，frame/payload CRC 有效。
  - HIL 报告中 `B_QUALITY_A` 的 VDC data evidence 为 `REJECTED` / `VDC_GATE_BAD_FRAME`，脚本记录为 `diagnostic timestamp outside active VDC data window`，用于证明当前还缺少按 active VDC data window 执行的 TDMA scheduler，不作为 100 ns DPLL evidence。
- 还需完成：
  - core1/PIO TDMA 必须按 active VDC schedule 等待 data/observation window，并增加硬件 timestamp latch；当前诊断 gate 通过或窗口拒绝都不得作为 100 ns DPLL lock evidence。
- 关联文件：
  - `components/distributed_refmem/src/refmem_vdc_bridge.c`
  - `tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
- 下一步：
  - 实现 core1/PIO 按 `VdcTdmaScheduleProfile` 等待 `REFMEM_DATA_WINDOW` / `VDC_OBSERVATION_WINDOW` 的执行路径，再接硬件 timestamp latch。

### VDC-TASK-20260816-007 - Two-board RefMem TDMA VDC evidence HIL script

- 状态：完成脚本编译检查；COM5/COM6 首次复跑暴露 VDC data window 调度缺口，已转入 VDC-TASK-20260816-008
- 日期：2026-08-16
- 任务目标：
  - 将 `SYSTem:REFMEM:SYNC:TDMA:VDC?` 纳入两板 RefMem PIO TDMA HIL 验收。
  - 对 `DELTA/ACK_NACK/FENCE/QUALITY` 交换记录 VDC bridge/gate/CRC/timestamp evidence，作为后续硬件 timestamp latch 的对照基线。
- 完成内容：
  - `tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 的 `ExchangeResult` 增加 `vdc_response/vdc_passed/vdc_reason`。
  - `tdma_exchange()` 在 RX frame 处理后查询接收板 `SYSTem:REFMEM:SYNC:TDMA:VDC? <receiver_local_slot>,<reference_slot>`。
  - 对支持的 RefMem data evidence frame 要求 VDC bridge `ACCEPTED`、gate pass、window class 为 `REFMEM_DATA`、timestamp 为 `SOFTWARE_US/1000 ns/DIAGNOSTIC_ONLY`。
  - `HELLO/EPOCH` 仍只记录为非 data evidence，不作为 VDC bridge 通过条件。
- 验证结果：
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815182630`，package CRC `0x8ABF373F`。
- 还需完成：
  - 烧录包含 `SYSTem:REFMEM:SYNC:TDMA:VDC?` 的固件到 COM5/COM6。
  - 运行 `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6`，保存 report。
- 关联文件：
  - `tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py`
- 下一步：
  - 跑全量 host/build 验证，通过后提交推送，再进入烧录和 COM5/COM6 HIL。

### VDC-TASK-20260816-006 - RefMem TDMA data frame to VDC envelope bridge

- 状态：完成 host 验证；仍为诊断 timestamp，硬件 latch 待实现
- 日期：2026-08-16
- 任务目标：
  - 将 COM5/COM6 已跑通的 RefMem TDMA data frame 纳入 VDC frame envelope/evidence 体系。
  - 保持 HAOFV 边界：RefMem 只映射 payload/evidence，VDC gate 只校验时间契约，offset/rate 仍只能由 `VdcSyncAO / SyncDpllFB` 写。
- 完成内容：
  - 新增 `refmem_vdc_bridge` 基础件，把 `REFMEM_SYNC_FRAME_DELTA` 映射为 `VDC_DOMAIN_PAYLOAD_REFMEM_DELTA`，把 `ACK_NACK/FENCE/QUALITY` 映射为 `VDC_DOMAIN_PAYLOAD_ACK_NACK_FENCE_QUALITY`。
  - 新增独立适配头 `distributed_refmem_vdc_bridge.h`，避免 `distributed_refmem.h` 直接依赖 VDC 主域头文件。
  - 新增 `distributed_refmem_build_realtime_tdma_vdc_envelope()`，从最近 TDMA RX result frame 构造 `vdc_tdma_frame_envelope_t`。
  - 新增维护查询 `SYSTem:REFMEM:SYNC:TDMA:VDC? [local_slot],[reference_slot]`，输出 bridge result、VDC gate result、frame/window/payload、CRC 和 timestamp 摘要。
  - 诊断 timestamp 仍保持 `SOFTWARE_US`、`timestamp_resolution_ns=1000`、`DIAGNOSTIC_ONLY`，可通过 VDC frame envelope 质量检查，但要求 DPLL eligibility 时会被拒绝。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_vdc_bridge_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815181647`，package CRC `0x48C97051`。
- 还需完成：
  - 将 PIO/DMA/IRQ/core1 硬件 timestamp latch 接入 bridge 输入，使 `timestamp_resolution_ns <= 100` 的 `VDC_OBSERVATION_WINDOW` 样本能够进入正式 DPLL gate。
  - 增加 HIL 脚本查询 `SYSTem:REFMEM:SYNC:TDMA:VDC?`，记录 COM5/COM6 的 frame CRC、payload CRC、late/delay 和 gate result。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_vdc_bridge.h`
  - `components/distributed_refmem/src/refmem_vdc_bridge.c`
  - `components/distributed_refmem/inc/distributed_refmem_vdc_bridge.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_vdc_bridge.c`
  - `tools/tests/run_refmem_vdc_bridge_tests.ps1`
- 下一步：
  - 跑 docs/diff/build 验证，通过后提交推送；随后进入硬件 timestamp latch 或 HIL 查询脚本固化。

### VDC-TASK-20260816-005 - VDC DCO control snapshot contract

- 状态：完成 host 验证；core1/PIO 实际消费待实现
- 日期：2026-08-16
- 任务目标：
  - 将 DPLL 输出给 core1/PIO 的 DCO 控制快照从文档契约落到 C 结构。
  - 明确 `SyncDpllFB` 侧的 clock model / DCO snapshot 是唯一受控输出，core1 后续只读稳定 snapshot。
- 完成内容：
  - 新增 `vdc_dco_control_t`，覆盖 `base_local_tick64`、`base_vdc_time64_ns`、`nominal_period_ns`、`period_adjust_ppb`、`phase_offset_ns`、`slew_limit_ppb`、`dco_update_seq`、`source_model_seq`、`epoch_id/run_id`、lock state 和 profile CRC。
  - `vdc_domain_context_t` / `vdc_domain_snapshot_t` 增加 `dco` 字段。
  - 新增 `vdc_domain_default_dco_control()`、`vdc_domain_dco_control_validate()` 和 `vdc_domain_publish_dco_control()`。
  - `vdc_domain_publish_clock_model()` 会从新的 `VdcClockModel` 派生 DCO snapshot，并由 VDC owner 单调递增 `dco_update_seq`。
  - DCO validate 拒绝无效 snapshot、CRC 不匹配、非法 lock state 和超过 servo sanity limit 的 slew。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，16/16 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815180531`，package CRC `0x3512BD4E`。
- 还需完成：
  - core1 realtime 读取 DCO snapshot 时增加 seqlock、双缓冲或等价 guard。
  - 将 DCO snapshot 接到 `FIRE_LOAD` 预测路径，验证 late/半更新 snapshot 不会输出边沿。
- 关联文件：
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 跑全量 host/build 验证；通过后提交推送，再继续硬件 timestamp latch 或 RefMem TDMA frame envelope 接入。

### VDC-TASK-20260816-004 - TDMA frame envelope and window class contract

- 状态：完成 host 验证；硬件 PIO timestamp latch 待接入
- 日期：2026-08-16
- 任务目标：
  - 将“每一帧首先是 TDMA/VDC frame envelope，RefMem 只是 payload class”落成 C 契约。
  - 将 `VDC_OBSERVATION_WINDOW`、`REFMEM_DATA_WINDOW` 和 `IDLE_BEACON` 纳入同一 `VdcTdmaScheduleProfile`。
  - 防止 RefMem data frame 或诊断 timestamp 被当成 DPLL 样本进入 lock gate。
- 完成内容：
  - `VdcTdmaScheduleProfile` 增加 RefMem data window 和 idle beacon window 的 offset/width 字段，schedule CRC 覆盖所有窗口定义。
  - 新增 `vdc_domain_tdma_window_class_t` 和 `vdc_tdma_frame_envelope_t`。
  - 新增 `vdc_domain_validate_tdma_frame_envelope()`，校验 frame version、schedule epoch/CRC、source/reference slot、window class、payload class、frame/payload CRC、timestamp source/resolution 和窗口边界。
  - `VDC_OBSERVATION_WINDOW` 只允许 `SYNC_SAMPLE/IDLE_BEACON`，并复用 timestamp evidence gate；要求 DPLL eligibility 时必须是硬件 tick、分辨率不大于 `100 ns`。
  - `REFMEM_DATA_WINDOW` 只允许 `REFMEM_DELTA/ACK_NACK_FENCE_QUALITY`，可以携带诊断 timestamp 和 quality evidence，但在要求 DPLL 样本时必须拒绝。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，16/16 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815180148`，package CRC `0x4AF89705`。
- 还需完成：
  - 将真实 PIO/DMA/IRQ/core1 timestamp latch 接到 frame envelope 的 `timestamp` 字段。
  - 将 frame envelope 接入现有 RefMem PIO SPI adapter 的 TX/RX 路径，使 COM5/COM6 板端同步报告不再依赖 host 侧耗时。
- 关联文件：
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 增加 DCO snapshot / core1 消费契约，或者先把 frame envelope 映射到 RefMem TDMA adapter 的板端 evidence 输出。

### VDC-TASK-20260816-003 - VDC domain core contract landing

- 状态：完成 host/build 验证；硬件 timestamp latch 和真实 servo 待实现
- 日期：2026-08-16
- 任务目标：
  - 把 VDC 从 `vdc_dpll_manager` 的 ready/service stub 推进为独立 HAOFV 内部基础组件。
  - 冻结并落地 `VdcClockModel`、`VdcTdmaScheduleProfile`、`VdcTDMATimestampEvidence`、`VdcServoProfile`、`VdcDpllState` 和 `VdcGateResult` 的首版 C 契约。
  - 实现 `local_tick -> vdc_time64_ns` 映射和 TDMA observation window 输入门禁，明确诊断时间戳不得进入 DPLL。
- 完成内容：
  - 新增 `components/vdc_domain/inc/vdc_domain.h` / `src/vdc_domain.c` 和组件 `CMakeLists.txt`。
  - 新增 schedule CRC、schedule validate、默认 schedule/servo/clock model、clock mapping、timestamp evidence gate 和最小 DPLL 状态推进。
  - 门禁要求 active schedule、schedule CRC、epoch、reference slot、source slot、payload class、hardware timestamp source、`DPLL_ELIGIBLE` flag、`timestamp_resolution_ns <= 100` 和 observation window bound 全部通过。
  - 旧 `components/vdc_dpll_manager/` 改为兼容 wrapper，应用任务和现有 `SYSTem:SYNC:VDC:*?` 查询继续走旧接口，但状态来源开始接入 `vdc_domain`。
  - 新增 `tests/unit/test_vdc_domain.c` 和 `tools/tests/run_vdc_domain_tests.ps1`，并加入 `run_host_unit_tests.ps1`。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，16/16 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815175522`，package CRC `0xB090BCE7`。
- 还需完成：
  - 将 PIO/DMA/IRQ/core1 的真实 timestamp latch 输出接入 `vdc_domain_submit_tdma_evidence()`。
  - 实现 `SyncDpllFB` 的真实 offset/rate servo，而不是当前 sample-count 分阶段状态推进。
  - 将 VDC snapshot 映射到 RefMem VDC/DPLL 区域，并完善 SCPI 字段。
- 关联文件：
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `tests/unit/test_vdc_domain.c`
  - `tools/tests/run_vdc_domain_tests.ps1`
- 下一步：
  - 增加板端帧级 timestamp evidence 输入路径，先让 COM5/COM6 在真实 PIO TDMA 环路中产出被 VDC gate 明确拒绝或接受的样本证据。

### VDC-TASK-20260816-002 - TDMA diagnostic timestamp and 1e3ns deadline contract

- 状态：完成 host/build 验证；硬件 latch 和 DPLL gate 待实现
- 日期：2026-08-16
- 任务目标：
  - 将 RefMem realtime TDMA 的 deadline 接口收敛为 `deadline_1e3ns`，与 VDC 以 ns 为基础单位的命名一致。
  - 给当前 TDMA snapshot 增加板端时间戳诊断字段，但明确来源仍是 `time_us_64()*1000`，分辨率为 `1000 ns`，不得作为 100 ns DPLL lock evidence。
  - 保持 `SYSTem:REFMEM:SYNC:TDMA:STATus?` 既有字段顺序稳定，新增 timestamp 字段只追加。
- 完成内容：
  - `refmem_realtime_tdma` 新增 `timestamp_source`、`timestamp_resolution_ns`、`timestamp_flags`、submit/core1 arm/start/done 时间和 `core1_elapsed_ns` snapshot。
  - `distributed_refmem`、RefMem sync frame、RefMem command timeout、quality snapshot、SCPI handler 和 HIL 脚本统一使用 `deadline_1e3ns` / `timeout_1e3ns` / `p99_1e3ns` / `p999_1e3ns` 语义。
  - PIO SPI physical adapter 的公开 timeout 参数收敛为 `timeout_1e6ns`；内部仍使用 Pico SDK 的微秒 API 执行等待，但接口语义不再暴露 `timeout_ms`。
  - VDC 文档补充每帧都是 TDMA/VDC envelope，DPLL 维护是总线时序骨架，RefMem 作为 payload class 搭载。
- 验证结果：
  - `python -m py_compile tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py tools\refmem_quality_gate_hil_validate\refmem_quality_gate_hil_validate.py tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，15/15 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815174141`，package CRC `0x58997C54`。
- 还需完成：
  - 实现 PIO/DMA/IRQ/core1 硬件 timestamp latch，使正式 DPLL 样本满足 `timestamp_resolution_ns <= 100`。
  - 增加 COM5/COM6 板端脚本，采集每帧 payload class、schedule CRC、frame/sample CRC、late/jitter 和 phase error。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_realtime_tdma.h`
  - `components/distributed_refmem/src/refmem_realtime_tdma.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
- 下一步：
  - 冻结 `VdcTDMATimestampEvidence` C 结构和正式 `VDC_OBSERVATION_WINDOW` sample gate。

### VDC-TASK-20260816-001 - Two-board TDMA baseline to DPLL input contract

- 状态：完成文档收敛；代码和板端 DPLL 采样待实现
- 日期：2026-08-16
- 任务目标：
  - 明确 COM5/COM6 两板真实 TDMA/PIO 环路已经形成后续 DPLL 的硬件基础。
  - 防止把 RefMem frame 同步成功、host 侧耗时或微秒软件时间戳误判为 100 ns 级 DPLL evidence。
  - 将 RefMem data TDMA window 和 VDC observation window 收敛为同一总线 TDMA cycle 下的不同 window class，并明确帧级 TDMA/VDC envelope。
- 完成内容：
  - `VDC_DOMAIN_ARCHITECTURE.md` 增加 Two-board TDMA hardware baseline，记录无 CS 3-wire PIO SPI、25 MHz、core1 realtime TDMA service 和 RefMem `HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 的关系。
  - 定义 TDMA frame envelope：每一帧先表达 schedule、slot、frame_seq、timestamp evidence、CRC 和 payload class，RefMem 只是 payload。
  - 定义两类 TDMA window class：`VDC_OBSERVATION_WINDOW` 用于高优先级 DPLL timestamp observation，`REFMEM_DATA_WINDOW` 在同一 VDC/TDMA 骨架上同步共同事实。
  - 明确总线在同步数据过程中也维护 DPLL；总线在循环维护 DPLL 时可以顺带同步 RefMem；无业务数据时仍需要 `IDLE_BEACON` 或等价同步帧维持 freshness。
  - 明确 DPLL 只能消费板端硬实时 timestamp sample；`time_us_64()*1000` 只能作为诊断时间戳，必须报告 `timestamp_resolution_ns=1000`，不得作为 100 ns evidence。
  - `VDC_DOMAIN_TODO.md` 增加 `VdcTDMATimestampEvidence`、两板 observation window bring-up、timestamp resolution gate 和 COM5/COM6 板端 evidence 验证项。
- 验证结果：
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
- 还需完成：
  - 实现 PIO/DMA/IRQ/core1 timestamp latch，使 `timestamp_resolution_ns <= 100` 的样本进入 DPLL gate。
  - 增加 COM5/COM6 板端脚本，记录每帧 expected/observed/apply timestamp、late/jitter、sample CRC、payload class 和 phase error。
- 关联文件：
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
- 下一步：
  - 清理当前 TDMA timestamp 代码草稿，确保过渡诊断字段不冒充 100 ns DPLL evidence；随后进入 `VDC_OBSERVATION_WINDOW` 的 C 结构和 SCPI snapshot 落地。

### VDC-TASK-20260814-005 - TDMA + DPLL 融合架构补充

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将 TDMA 与 DPLL 的融合架构输入纳入 VDC canonical，而不是把二者作为可替代方案。
  - 明确 TDMA、DPLL 和低频驯服环的分层职责、writer 边界和数据契约。
  - 将性能描述收敛为待验证目标，避免在未实测前写成产品保证。
- 完成内容：
  - `README.md` 增加当前主线摘要：TDMA 硬实时环、DPLL 锁相环、低频驯服环和 core0/core1/PIO 边界。
  - `VDC_DOMAIN_ARCHITECTURE.md` 增加 `TDMA + DPLL 融合控制模型`，定义三层控制环、TDMA observation window、DPLL servo/DCO contract、low frequency discipline 和 fused state machine。
  - `VDC_DOMAIN_ARCHITECTURE.md` 的内部数据模型新增 `VdcTdmaScheduleProfile`、`VdcDcoControl` 和 `VdcDisciplineModel`，核心字段增加 TDMA schedule CRC、DCO update seq、period adjust、slew limit、aging/temperature compensation 和 holdover drift bound。
  - 状态机从单一 `LOCKING` 细分为 `INITIAL_SYNC -> FREQ_LOCK -> PHASE_LOCK -> LOCKED`，便于后续分别验证初始同步、频率拉入、相位收敛和正式锁定。
  - `VDC_DOMAIN_TODO.md` 增加 TDMA schedule profile、DCO snapshot、observation window gate、低频驯服任务和对应验证项。
- 验证结果：
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
- 还需完成：
  - 冻结 `VdcTdmaScheduleProfile`、`VdcDcoControl` 和 `VdcDisciplineModel` 的 C 结构、字段单位和 snapshot guard。
  - 代码中实现 TDMA observation window 输入门禁和 DCO snapshot 提交。
  - 增加收敛验证脚本，记录 lock_time、RMS/peak offset、outlier ratio、DCO slew 和 HOLDOVER drift bound。
- 关联文件：
  - `docs/vdc/README.md`
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
- 下一步：
  - 按 P2 先冻结 VDC 数据契约，再进入 P3 的 DPLL/Clock Model 代码落地。

### VDC-TASK-20260813-004 - PIO/VDC 首版硬实时装配链补充

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将首版 `PIO_SM0: SYNC_RX_CAPTURE`、`PIO_SM1: SYNC_TX_FIRE`、DMA、core1 realtime、`task_vdc_sync` 和 `task_loop_engine` 的参考数据通路写入 VDC 主域。
  - 明确硬实时采样/输出链路和软件 DPLL/VDC owner 的边界。
- 完成内容：
  - `VDC_DOMAIN_ARCHITECTURE.md` 增加“首版 PIO/VDC 参考装配链”，定义捕获链、DPLL 更新链和 FIRE_LOAD 输出链。
  - `RTOS_HAOFV_ARCHITECTURE.md` 增加 PIO/VDC 首版数据通路表和数据流。
  - `VDC_DOMAIN_TODO.md` 增加 PIO_SM0 FIFO/DMA/timestamp、PIO_SM1 FIRE_LOAD 小载荷、core1 写入边界和输出链路冻结待办。
  - `VDC_DOMAIN_TODO.md` 增加 PIO capture 到 VdcSlot、LoopEngine 到 PIO_SM1 的验证项。
- 验证结果：
  - 本任务为文档框架补足，尚未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 后续根据实际 PIO 资源、布线和 board profile 调整具体 PIO instance、GPIO、DMA channel 和 ring buffer 大小。
  - 后续在代码中定义 `FIRE_LOAD` 小载荷和 capture FIFO word 格式。
- 关联文件：
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
  - `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
- 下一步：
  - 进入 VDC P2/P4，把 timestamp sample、DPLL input ring 和 `FIRE_LOAD` payload 字段契约冻结。

### VDC-TASK-20260813-003 - 虚拟 DC 时钟参考框架补足

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 基于 LinuxPTP / Chrony / SOEM EtherCAT DC 的时间同步机制，补足 VDC 主域框架。
  - 将参考项目落到 reference clock、servo profile、error budget、DC sync pipeline 和 HOLDOVER model。
- 完成内容：
  - `VDC_DOMAIN_ARCHITECTURE.md` 增加 VDC 框架补足章节。
  - 定义 `VdcReferenceClockTable`，首版可固定 A0，后续支持 candidate、priority、source 和 failover reason。
  - 定义 `VdcServoProfile`，覆盖 servo type、kp/ki、update period、step threshold、sanity frequency limit、lock threshold、outlier threshold 和 reset policy。
  - 定义 `VdcErrorBudget`，覆盖 offset RMS/max、frequency skew、path delay、delay stddev、dispersion 和 root distance 等价误差上界。
  - 定义 `VdcDcSyncPipeline`，覆盖 reference select、active calibration delay、timestamp dictionary/profile CRC、initial sync、drift compensation、LOCKED publish 和 T2/READY validation。
  - `VDC_DOMAIN_TODO.md` 补充 reference clock、servo profile、error budget、DC sync pipeline 和 holdover drift bound 待办。
- 验证结果：
  - 本任务为文档框架补足，尚未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 在 `HAOFV_VDC_DPLL_ARCHITECTURE.md` 增加质量字段与 PTP/Chrony servo 字段映射。
  - 在 `SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` 增加 initial sync / drift compensation / holdover 检查链。
- 关联文件：
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
- 下一步：
  - 进入 VDC P2，冻结 `VdcClockModel`、`VdcServoProfile`、`VdcErrorBudget` 和 `VdcDcSyncPipeline` 字段契约。

### VDC-TASK-20260813-002 - VDC 外部时间同步参考拆分

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将刚刚列出的外部参考项目按虚拟反射内存和虚拟 DC 时钟拆分。
  - 把 LinuxPTP / Chrony、SOEM / EtherCAT DC 和 VDC owner/FB 边界参考归入 VDC Domain。
- 完成内容：
  - `VDC_DOMAIN_TODO.md` 增加“参考项目收敛原则”，明确 VDC 只吸收 offset/rate、jitter、servo reset、HOLDOVER、reference clock、传播 delay、initial sync、drift compensation 和 timestamp 机制。
  - `VDC_DOMAIN_TODO.md` 增加 P1.5 外部时间同步参考机制工程化收敛章节。
  - `VDC_DOMAIN_ARCHITECTURE.md` 增加外部参考机制矩阵，区分 LinuxPTP/Chrony、SOEM/EtherCAT DC 和 IEC 61499 的 VDC 落地方式与不采用内容。
  - VDC P8 增加 PTP/Chrony-style 和 EtherCAT DC-style 验证项。
- 验证结果：
  - 本任务为文档拆分，尚未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 在 `HAOFV_VDC_DPLL_ARCHITECTURE.md` 增加 VDC/DPLL 质量字段与 PTP/Chrony servo 字段映射。
  - 在 `SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` 增加 EtherCAT DC-style initial sync / drift compensation / holdover 检查链。
- 关联文件：
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
  - `docs/vdc/VDC_TASK_PROGRESS.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_TASK_PROGRESS.md`
- 下一步：
  - 按 VDC P2/P3 冻结 VDC 数据契约和 DPLL/clock model。

### VDC-TASK-20260813-001 - VDC 三份标准文档建立

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 在 `docs/vdc/` 中建立 VDC 内部主域的三份标准文件和目录 README。
  - 将“虚拟 DC 时钟升格为 HAOFV 内部基础主域”的边界写入文档。
- 完成内容：
  - 新增 `VDC_DOMAIN_ARCHITECTURE.md`，定义 VDC Domain 的定位、职责边界、HAOFV 层级、内部数据模型、共同时间映射、状态机、跨域契约、SCPI 边界、目标代码形态和验证门禁。
  - 新增 `VDC_DOMAIN_TODO.md`，把文档同步、HAOFV 主域升级、数据契约、DPLL、CAL/SYNC/MEAS/TRIG 边界、RefMem 映射、代码组件化、SCPI/System Pack 和验证拆成 P0-P8 待办。
  - 新增 `VDC_TASK_PROGRESS.md`，作为 VDC 主域独立任务进度入口。
  - 新增 `README.md`，作为 VDC 目录入口。
  - 更新 `docs/README.md`、`docs/arch/README.md` 和 `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`，把 VDC 从 `sync/` 中拆出为内部基础主域。
  - 更新 `HAOFV_ARCHITECTURE.md`、`RTOS_HAOFV_ARCHITECTURE.md`、`RTOS_HAOFV_TODO.md`、`HAOFV_MAINTENANCE_TODO.md`、`sync/README.md` 和 RefMem 入口，明确 VDC 与 RefMem 并列：VDC 管共同时间，RefMem 管共同事实。
- 验证结果：
  - 本任务为文档生成，尚未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 运行 `python tools/docs_check/docs_check.py`。
- 关联文件：
  - `docs/vdc/README.md`
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
  - `docs/vdc/VDC_TASK_PROGRESS.md`
- 下一步：
  - 按 `VDC_DOMAIN_TODO.md` 的 P0/P1 更新索引和架构入口。
