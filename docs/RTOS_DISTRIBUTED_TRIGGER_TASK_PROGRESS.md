# RTOS 分布式触发任务进度追踪与回溯

Status: Active
Domain: RTOS-DISTRIBUTED-TRIGGER
Canonical: `docs/RTOS_DISTRIBUTED_TRIGGER_TASK_PROGRESS.md`
Related: `docs/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md`, `docs/SCPI_TASK_PROGRESS.md`, `docs/MULTICORE_PARTITION_PLAN.md`, `docs/RTOS_PORTING_PLAN.md`
Last updated: 2026-08-12

本文档用于记录 DTC100 / RP2350_TRIG 工程中 RTOS + 双核 AMP、分布式触发、
模拟反射内存、任务拆分和板端烧录验证进度。每完成一个阶段，都应追加任务记录，
说明目标、完成内容、验证结果、剩余工作和下一步计划，便于后续回溯任务边界、
水位、core1 heartbeat、反射内存快照和 CAL/SYNC 骨架状态。

架构原则以 `docs/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md` 为准。SCPI 模块拆分和
命令表迁移记录放在 `docs/SCPI_TASK_PROGRESS.md`。

## 记录规则

- 每个正式 RTOS 分布式触发任务使用独立编号：`RTOS-DIST-TASK-YYYYMMDD-NNN`。
- 每条记录必须写明任务目标、完成内容、验证结果、剩余工作。
- 最新记录追加在“任务记录”章节顶部。
- 只要修改固件代码，必须执行构建、烧录和板端基础查询验证；如未完成烧录，必须明确记录。
- 双核相关任务必须记录 `SYST:CORE?` core1 heartbeat 是否增长。
- RTOS 任务相关任务必须记录 `SYST:RTOS:STAT?` 或等价 smoke 中的 stack/heap 水位。
- 反射内存相关任务必须记录 `SYST:REFM:*`、table size、layout version、table_seq、heartbeat 或 slot 摘要。
- CAL/SYNC/DPLL 任务必须记录对应 service_count 是否增长和错误队列状态。
- 串口或 USBTMC 验证脚本必须单 owner 管理端口生命周期，避免并行访问同一 COM/VISA 资源。

## 状态定义

| 状态 | 含义 |
|---|---|
| `完成` | 当前 RTOS 子任务目标已经达成，并完成必要构建、烧录、板端或离线验证。 |
| `进行中` | 已完成阶段性工作，但还未完成真实板端闭环或仍需后续接产品路径。 |
| `阻塞` | 当前无法继续，需要硬件、工具、资料或用户操作。 |
| `暂停` | 暂时不推进，但不是技术阻塞。 |

## 记录模板

```markdown
### RTOS-DIST-TASK-YYYYMMDD-NNN - 任务标题

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

RTOS 主线已经完成 `task_usb_device/task_scpi` 拆分、`task_refmem_sync` 64 KB
本地表骨架、`task_dpll`、`task_vdc_sync`、CoreVector/RuntimeProtection 快照和
`task_calibration` 空壳。当前重点转入反射内存 slot 一致性、跨核通信契约、
CAL/SYNC staging + ACK/NACK、RJ45_SYNC_RING 和 `FIRE_LOAD/T2` 闭环。

## 任务记录

### RTOS-DIST-TASK-20260812-006 - task_calibration 骨架与 CAL 快照

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 增加 `task_calibration` 空壳和 `calibration_job_queue`。
  - 先支持 link/delay 表 staging、snapshot 和计数器，不接真实测量和落盘。
- 完成内容：
  - 增加 `task_calibration` RTOS task。
  - 发布 `READ:CALibration:STATe?`、`READ:CALibration:LINK?`、
    `READ:CALibration:PARameter?`、`READ:CALibration:HEALth?` 所需快照字段。
  - 保留 calibration measurement 和 storage package inactive。
- 验证结果：
  - build id：`20260812043516`。
  - single：`calibration_status 1/1 PASS`。
  - full：RTOS + multicore smoke `16/16 PASS`。
  - RTOS：`calibration` task stack `2048 words`，used `28 words`。
  - CAL：`READ:CALibration:STATe?` service_count `673265 -> 674940 -> 676614`。
  - CAL：active CRC `268435459`，link_seq `676621`，parameter_seq `676629`。
  - Trigger：`TRIGger:MODE 1 -> TRIGger:STARt -> TRIGger:STOP` product smoke PASS。
  - Error queue：`SYST:ERR? -> 0,"No error"`。
  - 归档：`build-rtos-multicore-smoke/validation_calibration_step1_full_retry`。
- 还需完成：
  - 实现 `CONFigure:CALibration:LINK:ADD/SET/DELete` 和 link key 去重。
  - 实现 `CALibration:STARt` 短事务和 `CALibration:SAVE/LOAD/ACTivate/ROLLback`。
  - 将 CAL 结果写入 `CalibrationSlot` 并接入 storage/version/quality。
- 关联文件：
  - `application/src/app.c`
  - `middleware/scpi_port/src/scpi_calibration_commands.c`
  - `docs/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md`
- 下一步：
  - 接入 CAL link 增删改查 staging + ACK/NACK。

### RTOS-DIST-TASK-20260810-005 - CoreVector 与 RuntimeProtection 快照

- 状态：完成
- 日期：2026-08-10
- 任务目标：
  - 增加 `CoreVectorOwnerTable` 和 `RuntimeProtectionTable`。
  - 暴露 core0/core1 VTOR owner、IRQ owner、entry owner、flash lockout 和 park 状态。
- 完成内容：
  - 在 DistributedVectorTable header 中加入 core0/core1 VTOR owner、IRQ owner mask、
    entry table owner 和 guard 字段。
  - 在 DistributedVectorTable header 中加入 RAM-resident、flash lockout/park 和
    entry owner 状态。
  - 增加 `SYST:CORE:VECT?` 和 `SYST:PROT:STAT?` 查询。
- 验证结果：
  - build id：`20260810151918`。
  - OTA baseline：`SYST:OTA:COMM -> "OK"`；
    `SYST:OTA:STAT? -> "COMMITTED",2,"NONE",5`。
  - `SYST:CORE:VECT? -> 1,<table_seq>,2,0,1,15,3840,2,0,2,<guard_crc>,0,0`。
  - `SYST:PROT:STAT? -> 1,<table_seq>,1,1,1,0,0,0,2,11,2,<guard_crc>,0,0`。
  - smoke：identity/build_id/core_heartbeat/loop_status/vdc_status/dpll_status/
    config_gate_status/config_snapshot_queries/runtime_protection_tables/
    trigger_seq/error_queue/log_stat/trace_last `13/13 PASS`。
- 还需完成：
  - 实现 core1 park/lockout 握手和超时升级流程。
  - 增加 linker map 断言，确认 core1 关键入口和 lockout poll 位于预期 section。
- 关联文件：
  - `components/distributed_refmem/`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
- 下一步：
  - 继续补分布式 ACK/NACK reason、RUN 态 SCPI 策略表和系统资源表。

### RTOS-DIST-TASK-20260810-004 - task_vdc_sync 与 task_dpll 骨架

- 状态：完成
- 日期：2026-08-10
- 任务目标：
  - 增加 `task_vdc_sync` 空壳，只维护 lock 状态和计数器。
  - 增加 `task_dpll` 空壳，只维护 disabled/ready 状态。
  - 不接真实 DC convergence、转台 Compare Out 或角度预测收敛。
- 完成内容：
  - 增加 `task_vdc_sync` RTOS task。
  - 增加 `task_dpll` RTOS task。
  - 增加 `SYSTem:SYNC:VDC:STATus?` 和 `SYSTem:SYNC:VDC:DPLL:STATus?`
    所需状态快照。
  - 增加配置门禁快照查询 `SYST:CFG:STAT?`。
- 验证结果：
  - build id：`20260810132729`。
  - smoke：identity/build_id/core_heartbeat/loop_status/vdc_status/dpll_status/
    config_gate_status/trigger_seq/error_queue/log_stat/trace_last `11/11 PASS`。
  - VDC：`SYSTem:SYNC:VDC:STATus? -> 1,0,<service_count>,<first_service_ms>,<last_service_ms>,<sync_seq>`。
  - DPLL：`SYSTem:SYNC:VDC:DPLL:STATus? -> 1,0,<service_count>,<first_service_ms>,<last_service_ms>,<update_seq>`。
  - CFG：`SYST:CFG:STAT?` 返回 build id、ready、gate_state、epoch、run_id、
    version、ACK/NACK/busy/timeout 位和 CRC 快照。
  - RTOS：task_count `11`；`vdc_sync/dpll/cfg_gate/ui` 可见；heap min free
    `27968 bytes`。
- 还需完成：
  - 实现 SYNC DPLL 的 VDC offset/rate 更新、LOCK/HOLDOVER/RELOCK。
  - 实现角度预测 DPLL 的 Compare Out 输入和 `T_fire_base` 输出。
- 关联文件：
  - `application/src/app.c`
  - `components/distributed_config/`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
- 下一步：
  - 增加 CoreVector/RuntimeProtection 快照。

### RTOS-DIST-TASK-20260810-003 - task_refmem_sync 与本地 64 KB 反射内存表

- 状态：完成
- 日期：2026-08-10
- 任务目标：
  - 增加 `task_refmem_sync` 空壳。
  - 按 64 KB 完整布局维护本地 DistributedVectorTable header、node slot 和 heartbeat。
  - 增加本地 DistributedVectorTable snapshot 查询，先不做跨板同步。
- 完成内容：
  - 增加 `task_refmem_sync` RTOS task。
  - 预留本地 64 KB DistributedVectorTable layout。
  - 发布本地 header/node heartbeat snapshot。
  - 增加 `SYST:REFM:STAT?` 和 `SYST:REFM:NODE?`。
- 验证结果：
  - build id：`20260810110636`。
  - smoke：identity/build_id/core_heartbeat/trigger_seq/error_queue `5/5 PASS`。
  - REFMEM：`SYST:REFM:STAT? -> 65536,1,<table_seq>,0,8,<heartbeat>,<service_count>,0`。
  - REFMEM：`SYST:REFM:NODE? 7 -> 7,0,0,0,0,0,0,0,0`。
  - RTOS：`refmem_sync` used `32 words`，heap min free `65288 bytes`。
- 还需完成：
  - 冻结完整 `distributed_vector_table.h` layout、slot offset、slot size 和 layout version。
  - 实现 slot owner、slot_seq、CRC、stale、seqlock/double buffer。
  - 接入跨板 `REFMEM_DELTA`。
- 关联文件：
  - `components/distributed_refmem/`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
- 下一步：
  - 增加 DPLL/VDC skeleton。

### RTOS-DIST-TASK-20260810-002 - task_loop_engine 空壳

- 状态：完成
- 日期：2026-08-10
- 任务目标：
  - 建立 `task_loop_engine` 空壳，只计数和响应状态查询，不接业务扫描。
- 完成内容：
  - 增加 `task_loop_engine` RTOS task。
  - 增加 `LOOP:STAT?` / `STAT:LOOP?` 只读查询。
  - 暴露本地 service_count、first_service_ms、last_service_ms 快照。
- 验证结果：
  - 已随后续 multicore smoke 多次覆盖。
  - `LOOP:STAT?` service counter 持续增长。
- 还需完成：
  - 接入 A0 扫描状态机。
  - 接入 `CONFigure:TRIGger` 自动展开状态表和 active sequence。
- 关联文件：
  - `application/src/app.c`
  - `middleware/scpi_port/src/scpi_loop_engine_commands.c`
- 下一步：
  - 建立反射内存骨架。

### RTOS-DIST-TASK-20260810-001 - task_io_frontend 拆为 USB 与 SCPI

- 状态：完成
- 日期：2026-08-10
- 任务目标：
  - 将当前 `task_io_frontend` 拆为 `task_usb_device` 和 `task_scpi`。
  - 让 USB 设备栈服务和 SCPI 解析分离，为后续控制面拆分打基础。
- 完成内容：
  - `task_io_frontend` 拆为 `task_usb_device` 和 `task_scpi`。
  - `app_comm_service()` 拆为 `app_usb_device_service()` 和 `app_scpi_service()`。
  - 裸机路径保留 `app_comm_service()` wrapper；FreeRTOS 路径由两个任务直接调用。
  - `SYST:RTOS:STAT?` 显示拆分后的任务水位。
- 验证结果：
  - build id：`20260810104144`。
  - smoke：identity/build_id/core_heartbeat/trigger_seq/error_queue `5/5 PASS`。
  - RTOS：`usb_device` used `32 words`，`scpi` used `1166 words`。
  - heap min free：`73584 bytes`。
  - 归档：`build-rtos-multicore-smoke/validation_split_usb_scpi_step1`。
- 还需完成：
  - 后续继续拆 SCPI 命令模块，详见 `docs/SCPI_TASK_PROGRESS.md`。
- 关联文件：
  - `application/src/app.c`
  - `middleware/scpi_port/src/scpi_port.c`
- 下一步：
  - 建立 `task_loop_engine` 和 `task_refmem_sync` 骨架。
