# SCPI 任务进度追踪与回溯

Status: Active
Domain: SCPI
Canonical: `docs/SCPI_TASK_PROGRESS.md`
Related: `docs/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md`, `docs/DTC100_SCPI_COMMAND_PLANNING.md`, `docs/RP1200波导天线测试系统分布式触发方案SCPI指令表.md`
Last updated: 2026-08-12

本文档用于记录 DTC100 / RP2350_TRIG 工程中 SCPI 指令模块拆分、产品命令树收敛、
板端烧录验证和工具闭环进度。每完成一个阶段，都应追加任务记录，说明目标、完成内容、
验证结果、剩余工作和下一步计划，便于后续回溯 SCPI 架构边界、串口生命周期问题和
板端证据。

架构原则以 `docs/DTC100_SCPI_COMMAND_PLANNING.md` 为准，RTOS / 反射内存 / owner
任务边界以 `docs/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md` 为准。

## 记录规则

- 每个正式 SCPI 任务使用独立编号：`SCPI-TASK-YYYYMMDD-NNN`。
- 每条记录必须写明任务目标、完成内容、验证结果、剩余工作。
- 最新记录追加在“任务记录”章节顶部。
- 只要修改固件代码，必须执行构建、烧录和板端基础查询验证；如未完成烧录，必须明确记录。
- 对框架拆分、命令表迁移和产品命令语义修正，必须执行 full RTOS + multicore smoke。
- 串口或 USBTMC 验证脚本必须单 owner 管理端口生命周期，避免并行访问同一 COM/VISA 资源。
- 产品 `TRIGger:MODE 0..4 = IDLE/TRIG/CAL/SYNC/SIM` 不得与底层 `SEQ_STEP/ENC/BISS` 模式混用。
- `scpi_port.c` 长期只保留 libscpi context、输入输出、错误队列、reset/flush/control 和命令表汇总。

## 状态定义

| 状态 | 含义 |
|---|---|
| `完成` | 当前 SCPI 子任务目标已经达成，并完成必要构建、烧录、板端或离线验证。 |
| `进行中` | 已完成阶段性工作，但还未完成真实板端闭环或仍需后续接 owner task。 |
| `阻塞` | 当前无法继续，需要硬件、工具、资料或用户操作。 |
| `暂停` | 暂时不推进，但不是技术阻塞。 |

## 记录模板

```markdown
### SCPI-TASK-YYYYMMDD-NNN - 任务标题

- 状态：进行中 / 完成 / 阻塞 / 暂停
- 日期：YYYY-MM-DD
- 任务目标：
  - ...
- 完成内容：
  - ...
- 验证结果：
  - ...
- 还需完成：
  - ...
- 关联文件：
  - `path/to/file`
- 下一步：
  - ...
```

## 当前目标

SCPI 模块已经完成 CAL、SYNC、CONFIG、TRIGGER、SYSTEM SNAPSHOT、LOOP STATUS、
SYSTEM RUNTIME、SYSTEM DIAGNOSTICS/EVIDENCE、MEASURE 的拆分和板端闭环。
当前剩余高风险区域是底层验证能力拆分：`BiSS-C`、`ENC/PCNT`、`SEQ_STEP/ARM/FAULT`
和 `sync_io` pulse validation。后续拆分必须保持产品主流程与底层验证入口分离。

Realtime 细分按内部基础组件推进，`SCPI_REALTIME_COMPONENT_COMMANDS` 保持聚合入口，
子域目标为：

- `scpi_realtime_pcnt_commands.c/.h`：`TRIGger:PCNT:*`，转台脉冲输入计数、比较、门控和滤波基础组件。
- `scpi_realtime_encoder_commands.c/.h`：`TRIGger:ENC:*`，编码器计数触发配置和观测。
- `scpi_realtime_io_commands.c/.h`：`TRIGger/PULSe/MARKer/RJ45` 即时 IO、`SAMPle:*`、`OUTPut:CLOCk:*`、`STATus:SYNC?`。
- `scpi_realtime_sequence_commands.c/.h`：`TRIGger:SEQ:*`、`TRIGger:SOURce/EDGE/GATE/SAFE`、`ARM/DISarm/DISAble/FAULT`。
- `scpi_realtime_status_commands.c/.h`：`STATus:TRIGger?` 和后续内部实时状态查询。

拆分顺序为 PCNT -> ENC -> IO -> SEQ -> STATUS。每一步都必须构建、dry-run、文档检查、
RTOS + multicore smoke，并在可用 COM 口上执行产品 SCPI 板端验证。

## 任务记录

### SCPI-TASK-20260812-020 - Realtime ENC 命令细分

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 将 `TRIGger:ENC:*` 从 realtime 大文件中拆出，作为编码器计数触发基础组件。
  - 保持 `SCPI_REALTIME_COMPONENT_COMMANDS` 聚合入口不变，避免影响 `scpi_port.c`。
  - 继续按 PCNT 拆分模板推进 realtime 子域细化。
- 完成内容：
  - 新增 `scpi_realtime_encoder_commands.c/.h`。
  - `TRIGger:ENC:TARGet/TARGet?/COUNt?/APIN/APIN?/REVolution?`
    从 `scpi_realtime_component_commands.c` 移入 ENC 子模块。
  - `scpi_realtime_component_commands.h` 引入 `SCPI_REALTIME_ENCODER_COMMANDS`，
    与 `SCPI_REALTIME_PCNT_COMMANDS` 一起保持 realtime 聚合入口。
  - `CMakeLists.txt` 纳入新 ENC 源文件。
- 验证结果：
  - `cmake --build build` 通过，build id：`20260812130437`。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，生成 `111` 条产品命令。
  - `python tools\docs_check\docs_check.py` 通过，保留 9 个历史文件命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - OTA 通过 COM6 写入 `build\RP2350_TRIG_UPDATE.pkg`，`SYSTem:OTA:BOOT` 后运行
    `SYSTem:FW:BUILD? -> "20260812130437"`。
  - `SYSTem:OTA:COMMit` 后 `SYSTem:OTA:SLOT? -> 2,0,2,0,0`。
  - `python tools\product_scpi_validate\product_scpi_validate.py COM6` 实机通过：
    `summary: passed=True failed=0`，输出目录
    `build\product_scpi_validation_20260812_210636`。
- 还需完成：
  - 继续按计划拆分 `IO`、`SEQ` 和 `STATUS` realtime 子域。
- 关联文件：
  - `middleware/scpi_port/src/scpi_realtime_component_commands.c`
  - `middleware/scpi_port/inc/scpi_realtime_component_commands.h`
  - `middleware/scpi_port/src/scpi_realtime_encoder_commands.c`
  - `middleware/scpi_port/inc/scpi_realtime_encoder_commands.h`

### SCPI-TASK-20260812-019 - Realtime PCNT 命令细分

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 将 `TRIGger:PCNT:*` 从 realtime 大文件中拆出，作为转台脉冲计数基础组件。
  - 保持 `SCPI_REALTIME_COMPONENT_COMMANDS` 聚合入口不变，避免影响 `scpi_port.c`。
  - 为后续 ENC、IO、SEQ、STATUS 细分建立模板。
- 完成内容：
  - 新增 `scpi_realtime_pcnt_commands.c/.h`。
  - `TRIGger:PCNT:DECode/DIRection/FILTer/GATE/CMP/PRESet/CLEar/TOTal?/FREQuency?`
    从 `scpi_realtime_component_commands.c` 移入 PCNT 子模块。
  - `scpi_realtime_component_commands.h` 引入 `SCPI_REALTIME_PCNT_COMMANDS`，继续作为
    realtime 聚合入口。
  - `CMakeLists.txt` 纳入新 PCNT 源文件。
- 验证结果：
  - `cmake --build build` 通过，build id：`20260812125633`。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，生成 `111` 条产品命令。
  - `python tools\docs_check\docs_check.py` 通过，保留 9 个历史文件命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - OTA 通过 COM6 写入 `build\RP2350_TRIG_UPDATE.pkg`，`SYSTem:OTA:BOOT` 后运行
    `SYSTem:FW:BUILD? -> "20260812125633"`。
  - `SYSTem:OTA:COMMit` 后 `SYSTem:OTA:SLOT? -> 1,0,1,0,0`。
  - `python tools\product_scpi_validate\product_scpi_validate.py COM6` 实机通过：
    `summary: passed=True failed=0`，输出目录
    `build\product_scpi_validation_20260812_205831`。
- 还需完成：
  - 继续按计划拆分 `ENC`、`IO`、`SEQ` 和 `STATUS` realtime 子域。
- 关联文件：
  - `middleware/scpi_port/src/scpi_realtime_component_commands.c`
  - `middleware/scpi_port/inc/scpi_realtime_component_commands.h`
  - `middleware/scpi_port/src/scpi_realtime_pcnt_commands.c`
  - `middleware/scpi_port/inc/scpi_realtime_pcnt_commands.h`

### SCPI-TASK-20260812-017 - COMMUNICATION BiSS-C 命令拆分

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 将 BiSS-C 从底层 `TRIGger:BISS:*` 触发域中拆出，归入通信/协议验证域。
  - 保持产品 `TRIGger:MODE 0..4 = IDLE/TRIG/CAL/SYNC/SIM` 语义，不再把
    `TRIGger:MODE 3` 当作 BiSS 底层入口。
  - 保留旧 `TRIGger:BISS:*` / `STATus:BISS?` 作为 bench 兼容别名。
- 完成内容：
  - 新增 `scpi_communication_biss_commands.c/.h`。
  - 新主路径为 `COMMunication:BISS:*`。
  - 新增 `COMMunication:BISS:CONFigure`，用于把当前 BiSS profile 冻结为
    `BISS_CONFIGURED`；运行边界仍由 `TRIGger:ARM/DISarm` 控制。
  - `COMMunication:BISS:FRAMe/PULSe/CRC:ERRor/TIMEout:INJect` 作为调试注入事件，
    允许在 `BISS_ARMED` 中执行；profile 参数类命令仍禁止运行中修改。
  - `tools/biss_board_validate/biss_board_validate.py` 切到
    `COMMunication:BISS:*` 主路径，并加强串口日志/ACK 交织处理。
- 验证结果：
  - build id：`20260812104309`。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - picotool 烧录、verify、reboot 通过。
  - BiSS-C board smoke PASS：
    `build-rtos-multicore-smoke/validation_scpi_comm_biss_split`。
  - full RTOS + multicore smoke `16/16 PASS`：
    `build-rtos-multicore-smoke/validation_scpi_comm_biss_split_full`。
  - product SCPI validation `109/109 PASS`：
    `build-rtos-multicore-smoke/validation_scpi_comm_biss_split_product_ff`。
  - 注意：product SCPI 全量验证耗时约 174 秒，120 秒外层 timeout 会误判超时。
- 还需完成：
  - 后续将 `ENC/PCNT`、`SEQ_STEP/ARM/FAULT` 底层验证命令继续从 `scpi_port.c`
    拆到更明确的 foundation/validation 模块。
  - 后续文档可补充 `COMMunication:BISS:CONFigure` 作为 bench/debug 协议冻结入口，
    不进入产品业务指令树。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_communication_biss_commands.h`
  - `middleware/scpi_port/src/scpi_communication_biss_commands.c`
  - `middleware/scpi_port/src/scpi_port.c`
  - `tools/biss_board_validate/biss_board_validate.py`
- 下一步：
  - 继续拆分 `ENC/PCNT` 或 `SEQ_STEP/ARM/FAULT` 底层验证命令，保持产品主线和
    bench validation 主线分层。

### SCPI-TASK-20260812-016 - SYSTEM ACCESS 重命名与 product 符号收敛

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 将误导性的 `scpi_product_commands.c/.h` 重命名为系统访问策略模块。
  - 清理 SCPI 回调函数中的 `scpi_product_*` 前缀，使 C 符号名与域边界一致。
  - 保持对外 SCPI 指令字符串和响应格式不变。
- 完成内容：
  - 新增 `middleware/scpi_port/inc/scpi_system_access_commands.h`。
  - 新增 `middleware/scpi_port/src/scpi_system_access_commands.c`。
  - 删除 `middleware/scpi_port/inc/scpi_product_commands.h`。
  - 删除 `middleware/scpi_port/src/scpi_product_commands.c`。
  - 将 `SYSTem:SCPI:PERMission?`、`SYSTem:SCPI:ROLE?` 归入
    `SCPI_SYSTEM_ACCESS_COMMANDS`。
  - 将通用 accepted stub 从 `scpi_product_result_accepted()` 收敛为
    `scpi_port_result_accepted()`。
  - 将剩余 `scpi_product_*` 回调按模块改名为 `scpi_config_*`、
    `scpi_calibration_*`、`scpi_sync_*`、`scpi_system_diagnostics_*` 和
    `scpi_trigger_*`。
- 验证结果：
  - `rg -n "scpi_product_[A-Za-z0-9_]+" middleware\scpi_port\inc middleware\scpi_port\src`
    无残留。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - 烧录命令通过：
    `picotool load -f -v -x build-rtos-multicore-smoke\RP2350_TRIG_FACTORY.uf2`。
  - build id：`20260812100217`。
  - `python tools/product_scpi_validate/product_scpi_validate.py COM4 --root . --out-dir build-rtos-multicore-smoke\validation_scpi_system_access_rename_product_cls`
    通过，108/108 PASS。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM4 --timeout 8 --settle 2 --out-dir build-rtos-multicore-smoke\validation_scpi_system_access_rename_full_cls`
    通过，16/16 PASS。
  - 曾在旧 product validation 脚本下失败一次，原因是脚本仍硬编码
    `scpi_product_commands.c`；升级为多 SCPI 域文件扫描后通过。
  - 曾出现一次 full smoke `trigger_seq` 失败，原因是前序 product validation
    留下 `Missing parameter` 错误队列；脚本已在开始和结束执行 `*CLS`，串行复测通过。
- 还需完成：
  - 后续按真实权限会话设计，决定 `SYSTem:SCPI:PERMission` 和
    `SYSTem:SCPI:ROLE` 写命令是保留、NACK 还是移除。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_system_access_commands.h`
  - `middleware/scpi_port/src/scpi_system_access_commands.c`
  - `middleware/scpi_port/inc/scpi_port_internal.h`
  - `middleware/scpi_port/src/scpi_port.c`
  - `tools/product_scpi_validate/product_scpi_validate.py`
  - `CMakeLists.txt`
- 下一步：
  - 进入 `BiSS-C`、`ENC/PCNT`、`SEQ_STEP/ARM/FAULT` 和 `sync_io` pulse validation
    的底层验证命令拆分。

### SCPI-TASK-20260812-015 - SYSTEM DIAGNOSTICS 吸收 REPORT 并扩展 MEASure

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 删除独立 `scpi_report_commands.c/.h`，避免与 `scpi_system_diagnostics_commands.c/.h`
    形成两个并行的 `SYSTem:*` 证据/诊断模块。
  - 将 report placeholder 命令合并到 SYSTEM DIAGNOSTICS。
  - 将 `MEASure` 保留为独立原始观测层，后续供 `CALibration` 和 `SYNC` 复用 backend。
- 完成内容：
  - 新增 `middleware/scpi_port/inc/scpi_system_diagnostics_commands.h`。
  - 新增 `middleware/scpi_port/src/scpi_system_diagnostics_commands.c`。
  - 删除 `middleware/scpi_port/inc/scpi_report_commands.h`。
  - 删除 `middleware/scpi_port/src/scpi_report_commands.c`。
  - 新增 `middleware/scpi_port/inc/scpi_measure_commands.h`。
  - 新增 `middleware/scpi_port/src/scpi_measure_commands.c`。
  - 将 `SYSTem:RUN:*`、`SYSTem:LOG:PAGE?`、`SYSTem:TRACe:DATA?`、
    `SYSTem:SNAPshot:DATA?`、`SYSTem:T2:DATA?`、`READ:RUN:*`、
    `READ:STATistics?`、`READ:T2:*`、`SYSTem:TRIGger:DBG?`、`SYSTem:RESource?`
    归入 `SCPI_SYSTEM_DIAGNOSTICS_COMMANDS` / `SCPI_SYSTEM_DIAGNOSTICS_READ_COMMANDS`。
  - 新增 `MEASure:PERiod?`、`MEASure:JITTer?`、`MEASure:PULSe:WIDTh?`、
    `MEASure:LINK:DELay?`、`MEASure:T2?`，保留 `MEASure:FREQuency?` 和
    `MEASure:REPort?`。
  - `MEASure:*` 无有效测量报告时返回 `"NO_REPORT"` 或 `"PENDING_BACKEND"`，
    不再推入 SCPI execution error。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - 烧录命令通过：
    `picotool load -f -v -x build-rtos-multicore-smoke\RP2350_TRIG_FACTORY.uf2`。
  - build id：`20260812093758`。
  - 快测命令覆盖：
    `SYSTem:RUN:*`、`SYSTem:TRIGger:DBG?`、`SYSTem:RESource?`、
    `READ:RUN:SUMMary?`、`READ:STATistics?`、`READ:T2:*`、
    `MEASure:FREQuency?/PERiod?/JITTer?/PULSe:WIDTh?`、
    `MEASure:LINK:DELay?`、`MEASure:T2?`、`MEASure:REPort?`。
  - 快测关键结果：
    `MEASure:PERiod? -> "NO_REPORT",0`，
    `MEASure:LINK:DELay? A0,OUT1,A1,IN1 -> "A0","OUT1","A1","IN1",0,0,0,"PENDING_BACKEND"`，
    `SYSTem:ERRor:COUNt? -> 0`。
  - `python tools/product_scpi_validate/product_scpi_validate.py COM4 --root . --out-dir build-rtos-multicore-smoke\validation_scpi_diag_measure_split_product` 通过。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM4 --timeout 8 --settle 2 --out-dir build-rtos-multicore-smoke\validation_scpi_diag_measure_split_full` 通过，`16/16 PASS`。
  - 曾并行启动 product validation 与 full smoke，product validation 因 COM4 被占用失败；随后串行重跑通过。
- 还需完成：
  - `MEASure:LINK:DELay?` 后续接入真实 link delay backend。
  - `MEASure:T2?` 后续接入 T2 原始观测/统计 backend。
  - `CALibration:STARt` 和 `SYNC:CHECk/STARt` 后续复用测量 backend，而不是调用 SCPI callback。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_system_diagnostics_commands.h`
  - `middleware/scpi_port/src/scpi_system_diagnostics_commands.c`
  - `middleware/scpi_port/inc/scpi_measure_commands.h`
  - `middleware/scpi_port/src/scpi_measure_commands.c`
  - `middleware/scpi_port/src/scpi_port.c`
  - `docs/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md`
- 下一步：
  - 进入 `BiSS-C`、`ENC/PCNT`、`SEQ_STEP/ARM/FAULT` 和 `sync_io` pulse validation
    的底层验证命令拆分。

### SCPI-TASK-20260812-014 - SYSTEM RUNTIME 命令拆分

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 从 `scpi_port.c` 拆出低风险系统 runtime 查询。
  - 保持命令名和响应字段不变。
- 完成内容：
  - 新增 `middleware/scpi_port/inc/scpi_system_runtime_commands.h`。
  - 新增 `middleware/scpi_port/src/scpi_system_runtime_commands.c`。
  - 移出 `*TST?`、`SYSTem:FW:*`、`SYSTem:BOOT:*`、
    `SYSTem:LOG:LEVel/LEVel?/STATus?`、`SYSTem:CORE?`、
    `SYSTem:RTOS:STATus?`。
  - `scpi_port.c` 继续只负责 libscpi context、stream I/O、reset/control 和命令表汇总。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - 烧录命令通过：
    `picotool load -f -v -x build-rtos-multicore-smoke\RP2350_TRIG_FACTORY.uf2`。
  - build id：`20260812091932`。
  - runtime 快测通过：`*IDN?`、`SYSTem:FW:BUILD?`、`*TST?`、
    `SYSTem:BOOT:CAPability?`、`SYSTem:LOG:LEVel?`、`SYSTem:LOG:STATus?`、
    `SYSTem:CORE?`、`SYSTem:RTOS:STATus?`。
  - `python tools/product_scpi_validate/product_scpi_validate.py COM4 --root . --out-dir build-rtos-multicore-smoke\validation_scpi_system_runtime_split_step1` 通过。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM4 --timeout 8 --settle 2 --out-dir build-rtos-multicore-smoke\validation_scpi_system_runtime_split_full` 通过，`16/16 PASS`。
  - `SYSTem:ERRor:COUNt? -> 0`，`SYSTem:ERRor? -> 0,"No error"`。
  - 曾并行启动一条 COM4 查询，因 product validation 正在占用端口失败；随后串行重跑通过。
- 还需完成：
  - 无 runtime 拆分遗留；后续系统诊断/证据命令继续从 `scpi_port.c` 拆出。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_system_runtime_commands.h`
  - `middleware/scpi_port/src/scpi_system_runtime_commands.c`
  - `middleware/scpi_port/src/scpi_port.c`
  - `docs/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md`
- 下一步：
  - 拆分系统诊断和测量相关命令。

### SCPI-TASK-20260812-013 - LOOP STATUS 与 SYNC/VDC 边界修正

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 只把 LOOP engine 状态命令拆入 loop 模块。
  - 把 `SYSTem:SYNC:VDC:*` 保持在 SYNC 域，避免 service status 横切模块重复。
- 完成内容：
  - 新增 `scpi_loop_engine_commands.c/.h`。
  - 移动 `LOOP:STATus?`、`LOOP:STAT?`、`STATus:LOOP?`。
  - 将 `SYSTem:SYNC:VDC:STATus?` 和 `SYSTem:SYNC:VDC:DPLL:STATus?`
    放回 `scpi_sync_commands.c/.h`。
  - 移除过宽的 `scpi_service_status_commands` 边界。
- 验证结果：
  - build id：`20260812062425`。
  - quick：`*IDN?`、`SYST:FW:BUILD?`、`LOOP:STAT?`、`VDC:STAT?`、
    `DPLL:STAT?`、`STATus:*` aliases 通过。
  - full RTOS + multicore smoke 通过，错误队列干净。
  - 边界修正后验证：`READ:SYNC:*`、`SYSTem:SYNC:VDC:*`、LOOP aliases
    和 full smoke 通过。
  - 归档：`build-rtos-multicore-smoke/validation_scpi_service_status_split_step1`。
  - 归档：`build-rtos-multicore-smoke/validation_scpi_sync_loop_boundary_fix_step1`。
- 还需完成：
  - `task_vdc_sync` 后续通过 queue 消费动作，不由 SCPI callback 直接改状态。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_loop_engine_commands.h`
  - `middleware/scpi_port/src/scpi_loop_engine_commands.c`
  - `middleware/scpi_port/inc/scpi_sync_commands.h`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
- 下一步：
  - 继续拆 system runtime / diagnostics。

### SCPI-TASK-20260812-012 - SYSTEM SNAPSHOT 命令拆分

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 将反射内存、核心向量、运行保护、配置门禁和系统表查询拆出。
- 完成内容：
  - 新增 `scpi_system_snapshot_commands.c/.h`。
  - 移动 REFMEM status/node、core vector、runtime protection、config gate ACK/NACK、
    role/loop/action/calibration snapshots、SCPI run policy、SystemMode/Resource/Fault tables。
  - `SYSTem:TRIGger:DBG?` 和 `SYSTem:RESource?` 当时暂留 `scpi_port.c`，
    因依赖本地 static debug helper。
- 验证结果：
  - build id：`20260812060813`。
  - quick：`SYSTem:REFM/REFMem`、`CORE:VECTor`、`PROTection`、
    `CONFigure/CFG`、`SCPI:RUN:ALLOW`、`MODE/RESource/FAULT` tables 通过。
  - full RTOS + multicore smoke `16/16 PASS`。
  - `SYST:ERR? -> 0,"No error"`。
  - 归档：`build-rtos-multicore-smoke/validation_scpi_system_snapshot_split_step1`。
- 还需完成：
  - 后续把 `SYSTem:TRIGger:DBG?` 和 `SYSTem:RESource?` 移入 system diagnostics。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
- 下一步：
  - 收敛 LOOP / SYNC/VDC status 边界。

### SCPI-TASK-20260812-011 - REPORT PLACEHOLDER 命令拆分

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 将运行报告、分页日志、trace/snapshot/T2 占位读取从 `scpi_port.c` 拆出。
- 完成内容：
  - 新增 `scpi_report_commands.c/.h`。
  - 移动 `SYSTem:RUN:*`、`SYSTem:LOG:PAGE?`、`SYSTem:TRACe:DATA?`、
    `SYSTem:SNAPshot:DATA?`、`SYSTem:T2:DATA?`。
  - 移动 `READ:RUN:SUMMary?`、`READ:T2:COUNt?`、`READ:T2:DATA?`。
  - Storage / OTA / MMEM 当时仍保留在 `scpi_port.c`。
- 验证结果：
  - build id：`20260812055925`。
  - quick：`SYSTem:RUN:LAST?/SUMMary?/LOG?`、`SYSTem:LOG:PAGE?`、
    trace/snapshot/T2 data、`READ:RUN/T2` 通过。
  - full RTOS + multicore smoke `16/16 PASS`。
  - `SYST:ERR? -> 0,"No error"`。
  - 归档：`build-rtos-multicore-smoke/validation_scpi_report_split_step1`。
- 还需完成：
  - 后续复核发现 report 与 system diagnostics 应合并；见
    `SCPI-TASK-20260812-015`。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_report_commands.h`
  - `middleware/scpi_port/src/scpi_report_commands.c`
- 下一步：
  - 拆 system snapshot。

### SCPI-TASK-20260812-010 - 产品 TRIGGER RUN CONTROL 拆分

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 把产品运行控制与历史底层触发命令分层。
- 完成内容：
  - 新增 `scpi_trigger_commands.c/.h`。
  - 移动 `TRIGger:STARt`、`TRIGger:STOP`、`TRIGger:PAUSe`、
    `TRIGger:CONTinue` 和 `READ:TRIGger:STATe?`。
  - 旧 `TRIGger:MODE`、`TRIGger:SEQ`、`BISS`、`PCNT` 暂留 `scpi_port.c`。
- 验证结果：
  - build id：`20260812053420`。
  - quick：`TRIGger:MODE 1`、`TRIGger:MODE?`、
    `READ:TRIGger:STATe?`、`TRIGger:STARt`、`TRIGger:STOP` 通过。
  - full RTOS + multicore smoke `16/16 PASS`。
  - `SYST:ERR? -> 0,"No error"`。
  - 归档：`build-rtos-multicore-smoke/validation_scpi_trigger_split_step1`。
- 还需完成：
  - 后续旧底层触发命令拆入 validation/foundation 模块。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_trigger_commands.h`
  - `middleware/scpi_port/src/scpi_trigger_commands.c`
- 下一步：
  - 拆 report placeholders。

### SCPI-TASK-20260812-009 - CONFIG 命令拆分

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 将测试 recipe、角度、序列、SWITCH 配置与查询从 product common 移出。
- 完成内容：
  - 新增 `scpi_config_commands.c/.h`。
  - 移动 `CONFigure:TRIGger`、`CONFigure:ANGLe:*`、
    `CONFigure:SEQuence:*`、`CONFigure:SWITch#`。
  - 移动 `READ:TRIGger:PARameter?`、`READ:ANGLe:*?`、
    `READ:SEQuence:*?`、`READ:SWITch#?`。
  - `READ:RUN:*` 和 `READ:T2:*` 当时暂留 report/statistics。
- 验证结果：
  - build id：`20260812052827`。
  - quick：`READ:TRIGger:PARameter?`、`READ:ANGLe:*?`、
    `READ:SEQuence:*?`、`READ:SWITch1?`、`READ:RUN:SUMMary?`、
    `READ:T2:*` 通过。
  - full RTOS + multicore smoke `16/16 PASS`。
  - 错误队列 follow-up 后干净。
  - 归档：`build-rtos-multicore-smoke/validation_scpi_config_split_step1`。
- 还需完成：
  - 当前 `CONFigure:*` 仍是 accepted stub，不消费参数。
  - 接入 `task_loop_engine` staging 时，写命令要么完整解析并投递配置事件，
    要么明确 NACK，不能 accepted 后留下 parser 参数错误。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_config_commands.h`
  - `middleware/scpi_port/src/scpi_config_commands.c`
- 下一步：
  - 拆产品 run control。

### SCPI-TASK-20260812-008 - SYNC 命令拆分

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 将 SYNC 域和 `SYSTem:SYNC:VDC:*` 维护入口从 `scpi_port.c` 拆出。
- 完成内容：
  - 新增 `scpi_sync_commands.c/.h`。
  - 保留现有 SYNC 响应字段和 accepted stubs。
  - `READ:STATistics?` 从 SYNC 边界移到 report/statistics，避免与 SYNC quality 重叠。
- 验证结果：
  - build id：`20260812050219`。
  - quick：`READ:SYNC:STATe?/PARameter?/HEALth?/NODE?/LINK?/CHECk?/QUALity?/VERSion?` 通过。
  - `SYNC:CHECk -> "PASS","ACTIVE","FIELD_DEFAULT",268435459,"FIELD_SYNC_DEFAULT",536870914,"A0>A1>A2>A3>A0",1,"","","","OK","","","","NONE"`。
  - full RTOS + multicore smoke `16/16 PASS`。
  - `SYST:ERR? -> 0,"No error"`。
  - 归档：`build-rtos-multicore-smoke/validation_scpi_sync_split_step1`。
- 还需完成：
  - 后续 `READ:SYNC:*?` 响应来源迁到 `VdcSlot/NodeSlot/StatisticsSlot`。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_sync_commands.h`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
- 下一步：
  - 拆 CONFIG。

### SCPI-TASK-20260812-007 - CALIBRATION 命令拆分

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 将 CAL 域响应和 accepted stubs 从 `scpi_port.c` 拆出。
- 完成内容：
  - 新增 `scpi_calibration_commands.c/.h`。
  - 保持 CAL 响应字段不变。
  - `READ:SYNC:LINK?` 继续复用 CAL link snapshot。
- 验证结果：
  - build id：`20260812045404`。
  - quick：`*IDN?`、`SYST:FW:BUILD?`、`READ:CALibration:STATe?`、
    `READ:CALibration:LINK?`、`READ:SYNC:LINK?`、`SYST:ERR?` 通过。
  - full RTOS + multicore smoke `16/16 PASS`。
  - `READ:CALibration:STATe? -> "DONE","SMA","A0:OUT1","A1:IN1",0,0,0,0,"NONE",1,0,24460,268435459`。
  - `READ:SYNC:LINK? -> 24475,1,0,268435459,0,1,"SMA","A0","OUT1","A1","IN1","BIDIR",1,1,1`。
  - `SYST:ERR? -> 0,"No error"`。
  - 归档：`build-rtos-multicore-smoke/validation_scpi_cal_split_step1`。
- 还需完成：
  - 后续实现 `CalibrationSlot`，把 CAL 读取从 app task snapshot 迁移为反射内存快照。
  - 接入 CAL link 增删改查 staging + ACK/NACK。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_calibration_commands.h`
  - `middleware/scpi_port/src/scpi_calibration_commands.c`
- 下一步：
  - 拆 SYNC。
