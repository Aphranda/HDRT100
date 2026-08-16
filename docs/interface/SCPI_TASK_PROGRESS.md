# SCPI 任务进度追踪与回溯

Status: Active
Domain: SCPI
Canonical: `docs/interface/SCPI_TASK_PROGRESS.md`
Related: `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/interface/SCPI_COMMAND_PLAN.md`, `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md`
Last updated: 2026-08-16

本文档用于记录 Distributed Hard Real-Time Trigger System 中 SCPI 指令模块拆分、产品命令树收敛、
板端烧录验证和工具闭环进度。每完成一个阶段，都应追加任务记录，说明目标、完成内容、
验证结果、剩余工作和下一步计划，便于后续回溯 SCPI 架构边界、串口生命周期问题和
板端证据。`DTC100` 保留为当前设备型号，`RP2350_TRIG` 保留为历史工程和构建产物名。

架构原则以 `docs/interface/SCPI_COMMAND_PLAN.md` 为准，RTOS / 反射内存 / owner
任务边界以 `docs/arch/RTOS_HAOFV_ARCHITECTURE.md` 为准。

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
SYSTEM RUNTIME、SYSTEM DIAGNOSTICS/EVIDENCE、MEASURE 和 realtime 子域拆分的板端闭环。
当前重点仍是规范化产品 SCPI 指令：同步 Markdown/HTML 指令表，冻结 `TEST/SERVICE/DEBUG/FACTORY`
权限矩阵，统一 accepted/ACK/状态查询语义，复审业务配置、序列、运行控制、CAL/SYNC 门禁、
response block 字段和产品验证脚本覆盖。`docs/interface/SCPI_COMMAND_PLAN.md` 是本轮规范化
评审基线，后续需要处理序列建模命名、角度/断点缩写、校准 link 动词、SYNC/VDC/DPLL 层级、
通用 ACK/NACK、统计/T2/报告/MMEM 归属和 legacy alias 边界。底层实时验证入口统一以
`REALtime:*` 作为维护域主入口，旧 `TRIGger:*` 底层入口已经按组删除。realtime 内部基础组件
实现排在指令规范化之后。

Realtime 细分按内部基础组件推进，`SCPI_REALTIME_COMPONENT_COMMANDS` 保持聚合入口，
子域目标为：

- `scpi_realtime_pcnt_commands.c/.h`：`REALtime:PCNT:*`，转台脉冲输入计数、比较、门控和滤波基础组件；旧 `TRIGger:PCNT:*` 已删除。
- `scpi_realtime_encoder_commands.c/.h`：`REALtime:ENC:*`，编码器计数触发配置和观测；旧 `TRIGger:ENC:*` 已删除。
- `scpi_realtime_io_commands.c/.h`：`REALtime:IO:*` 即时 IO、采样和输出时钟；旧 `TRIGger/PULSe/MARKer/RJ45/SAMPle/OUTPut/STATus:SYNC?` 已删除。
- `scpi_realtime_sequence_commands.c/.h`：`REALtime:SEQ:*`、`REALtime:SOURce/EDGE/GATE/SAFE`、`REALtime:ARM/DISarm/DISAble/FAULT`；旧 `TRIGger:*` 底层入口已删除。
- `scpi_realtime_status_commands.c/.h`：`REALtime:STATus?` 和后续内部实时状态查询；旧 `STATus:TRIGger?` 已删除。

拆分顺序为 PCNT -> ENC -> IO -> SEQ -> STATUS，已全部完成并完成板端闭环。每一步都必须构建、
dry-run、文档检查、RTOS + multicore smoke，并在可用 COM 口上执行产品 SCPI 板端验证。

## SCPI 维护规范化待办

按优先级推进，不保留兼容入口。原则是先判断 legacy 功能是否已有正式归属：已有正式归属的，
确认 canonical 已覆盖同等参数、响应格式和验证脚本入口后，直接删除旧入口；尚无正式归属但确有
产品或维护价值的，先释放到 `REALtime`、`COMMunication`、`SYSTem`、`READ` 等正式域，再删除旧入口，
避免后续再补命令对照。

### P0 - 产品 TRIGger 域瘦身

- [x] 删除旧 `TRIGger:SOURce/EDGE/GATE/SAFE/SEQ/ARM/DISarm/FAULT` 入口，保留
  `REALtime:*` canonical。
- [x] 删除旧 `TRIGger:PCNT:*` 入口，保留 `REALtime:PCNT:*` canonical。
- [x] 删除旧 `TRIGger:ENC:*` 入口，保留 `REALtime:ENC:*` canonical。
- [x] 删除裸 `TRIGger/PULSe/MARKer/RJ45/SAMPle/OUTPut/STATus:SYNC?` 入口，确认必要能力已由
  `REALtime:IO:*` 覆盖。
- [x] 删除旧 `STATus:TRIGger?` 入口，保留 `REALtime:STATus?` canonical。
- [x] 文档和验证脚本默认只使用 canonical，不再新增 legacy 验证脚本。

### P1 - 通信和系统维护域收敛

- [x] `COMMunication:BISS:*` 作为 BiSS-C canonical 主入口，确认覆盖同等能力后删除
  `TRIGger:BISS:*` 和 `STATus:BISS?`。
- [x] `SYSTem:CONFigure:*`、`SYSTem:REFMEM:*`、`SYSTem:CORE:VECTOR?` 作为系统维护 canonical；
  确认覆盖后删除旧 `SYSTem:CFG:*`、`SYSTem:REFM:*`、`SYSTem:CORE:VECT?`，不保留兼容入口。
- [x] 裸 `STATus:*` 不进入产品主树；当前固件注册表只保留子域内部 `...:STATus?`，
  如后续实现 IEEE 488.2 status register，再单独规划。

### P2 - 重复读取和报告域收敛

- [ ] `SYSTem:RUN:SUMMary?` 作为 RUN 后摘要 canonical，`READ:RUN:SUMMary?` 保留兼容或主界面快捷查询。
- [ ] `READ:SEQuence:ACTive?` 保留 active 序列摘要，`READ:SEQuence:CHECk?` 保留逐项预检结果。
- [ ] `TRIGger:STARt [plan_id]` 保留受限便捷事务，但必须等价于“激活并启动”，不能绕过
  `CONFigure:SEQuence:ACTive` 的校验和门禁。
- [x] 断点命名收敛到 `CONFigure:ANGLe:BREAkpoint` / `READ:ANGLe:BREAkpoint?`，删除
  `BPOint` 入口。
- [x] 系统维护表入口删除短别名，保留 `SYSTem:PROTection:STATus?`、
  `SYSTem:MODE:TABle?`、`SYSTem:RESource:TABle?`、`SYSTem:FAULT:TABle?`。
- [x] 反射内存状态命名收敛到 `SYSTem:REFMEM:STATus?`。
- [x] 校准链路表动词收敛到 `LINK:ADD/UPDate/DELete/CLEAr`，补齐
  `CALibration:STOP` 和 `CALibration:CLEAr` 动作入口。
- [x] T2 明细读取从 `READ:T2:*` 收敛到 `SYSTem:T2:*?`。
- [x] 产品运行控制补齐 `TRIGger:ABORt`。
- [x] BiSS-C 参数命名从 `FBITs/POFFset/PBITs/PMODulo` 收敛到
  `FRAMe:BITS`、`POSition:OFFSet`、`POSition:BITS`、`POSition:MODulo`。
- [x] 在现有 `COMMunication` 主域下增加 `COMMunication:SERial:UART#:*` 维护入口，
  为后续 UART/RS485 通信接口扩展预留稳定命令树。

## 任务记录

### SCPI-TASK-20260816-036 - VDC raw capture observer 维护查询

- 状态：完成离线、构建和 COM5/COM6 板端查询验证。
- 日期：2026-08-16
- 任务目标：
  - 为 VDC manager 的 SYNC_IO raw capture observer 增加只读维护查询。
  - 保持 SCPI 边界：查询只读 snapshot，不启动 capture、不配置 observer、不投递 VDC 样本。
- 完成内容：
  - 新增 `SYSTem:SYNC:VDC:OBServer?`，返回 enabled、batch、raw/no-edge/ambiguous/bad-argument/submitted/accepted/rejected、last raw word、event id、tick_l32、gate reject 和 next base tick。
  - 同步 `SCPI_COMMANDS.md`、`SCPI_COMMAND_PLAN.md`、正式指令表 Markdown 和 HTML 的 observer block 字段。
- 验证结果：
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，generated=127。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有既有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260816024745`，package CRC `0x028BC853`。
  - COM5 OTA 到 build `20260816024745` 并 commit：`SYST:OTA:STAT? -> "COMMITTED",1,"NONE",5`。
  - COM6 OTA 到 build `20260816024745` 并 commit：首次 boot/commit 工具输出被启动日志污染，复查 `SYST:FW:BUILD? -> "20260816024745"`、`SYST:OTA:SLOT? -> 2,0,2,0,0`、`SYST:OTA:STAT? -> "COMMITTED",1,"NONE",5`、`SYST:ERR? -> 0,"No error"`。
  - COM5 查询 `SYST:SYNC:VDC:OBServer? -> 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0`，`SYST:SYNC:VDC:STAT? -> 1,1,170836,3552,106131,170836`，`SYST:ERR? -> 0,"No error"`。
  - COM6 查询 `SYST:SYNC:VDC:OBServer? -> 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0`，`SYST:SYNC:VDC:STAT? -> 1,1,50875,3554,34170,50875`，`SYST:ERR? -> 0,"No error"`。
- 还需完成：
  - 把返回字段纳入两板 VDC/TDMA HIL 报告，并在 observer 启用后验证 raw/submitted/accepted/rejected 计数变化。

### SCPI-TASK-20260816-037 - VDC raw capture observer 维护配置

- 状态：完成命令注册、代码实现、文档同步和 COM5/COM6 板端验证。
- 完成：
  - 新增 `SYSTem:SYNC:VDC:OBServer`，固定返回 `1` 表示配置请求已被 manager 接受。
  - 无参数或 `0` 执行安全关闭并清零状态，兼容产品命令自动枚举验证。
  - `1` 启用态要求完整参数：`max_words_per_service,rising_event_id,falling_event_id,observed_mask,initial_sample_mask,next_base_time_l32_ns,sample_period_ns,expected_window_start_lo,expected_window_start_hi,frame_crc32[,max_backward_ticks,quality_flags,sample0_lsb]`。
- 边界：
  - 该命令属于 `SYSTem:SYNC:VDC:*` 维护域，不进入现场测试最小指令集。
  - 不启动 `REALtime:IO:SAMPle:STATe`，不直接写 DPLL 状态，不替代 `READ:SYNC:QUALity?` 的产品质量视图。
- 验证：
  - 产品命令生成：`product_scpi_validate.py --dry-run` 输出 `generated=128`，包含 `SYSTem:SYNC:VDC:OBServer -> 1`。
  - COM5/COM6：无参数关闭返回 `1`；启用态最小合法参数返回 `1` 且查询显示 `enabled=1,max_words_per_service=1`；关闭后查询为 disabled 全零字段；错误队列均为空。

### SCPI-TASK-20260816-038 - VDC observer 40-field evidence query

- 状态：完成代码、文档和 COM5/COM6 板端验证。
- 完成：
  - `SYSTem:SYNC:VDC:OBServer?` 保留前 18 个原始状态字段，追加 22 个配置/证据字段。
  - 新字段覆盖 event id、observed mask、sample period、expected window、frame CRC、schedule CRC、dictionary CRC、dictionary profile CRC、edge index 和 timestamp dictionary 展开结果。
- 验证：
  - dry-run 命令生成仍为 128 条，`OBServer?` 固定响应解析为 40 字段。
  - COM5/COM6 build `20260816031400` 上，启用最小合法 observer 后查询返回 40 字段，关闭后返回 40 个零字段，错误队列为空。

### SCPI-TASK-20260816-039 - VDC observer validation script

- 状态：完成。
- 完成：新增 `tools/vdc_observer_validate/vdc_observer_validate.py`，把 `SYSTem:SYNC:VDC:OBServer` / `OBServer?` 的启停、40 字段和 CRC 证据验证固化为脚本。
- 验证：COM5/COM6 运行通过，build 均为 `20260816031400`。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_sync_commands.h`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/interface/SCPI_COMMAND_PLAN.md`
  - `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md`
  - `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.html`
- 下一步：
  - 在真实 timestamp latch 或 observer 配置来源落地后，扩展 HIL 脚本验证 raw word -> compact observation -> VDC gate 证据链。

### SCPI-TASK-20260813-035 - 通信主域增加 UART 入口

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 在现有 `COMMunication` 主域下增加 UART 维护入口，为后续 RS485/UART 接口扩展预留稳定命令树。
  - 保持 USB 归 `SYSTem:USB:*`、反射内存归 `SYSTem:REFMEM:*`，避免泛化 `INTerface` 顶级域。
- 完成内容：
  - 新增 `scpi_communication_uart_commands.c/.h`，挂载
    `COMMunication:SERial:UART#:BAUD/FORMat/STATe/STATus?/TX:TEST/RX:COUNt?/ERRor?`。
  - `scpi_port.c` 命令表加入 `SCPI_COMMUNICATION_UART_COMMANDS`。
  - `CMakeLists.txt` 加入 UART SCPI 源文件。
  - `tools/product_scpi_validate/product_scpi_validate.py` 加入 UART 通信命令头文件和源文件。
  - 同步 `docs/interface/SCPI_COMMAND_PLAN.md`、`docs/interface/SCPI_COMMANDS.md` 和
    `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md/html`。
- 验证结果：
  - `python tools/product_scpi_validate/product_scpi_validate.py --dry-run`
    通过，生成 `125` 条产品命令。
  - `python -m py_compile` 覆盖 product/legacy/BiSS/multicore/distributed-loopback
    验证脚本，通过。
  - `python tools/docs_check/docs_check.py` 通过，保留 9 个既有文件名 warning。
  - `cmake --build build-validation` 通过，build id：`20260813014249`，
    package CRC：`0x180DAFD1`。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id：`20260813014249`，
    package CRC：`0xBA51CE76`。
  - `git diff --check` 通过，仅保留既有 CRLF 工作区 warning。
- 还需完成：
  - 后续接入真实 UART owner/driver 后，将 `PENDING_BACKEND` 响应改为真实端口状态和计数。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_communication_uart_commands.h`
  - `middleware/scpi_port/src/scpi_communication_uart_commands.c`
  - `middleware/scpi_port/src/scpi_port.c`
  - `tools/product_scpi_validate/product_scpi_validate.py`
  - `docs/interface/SCPI_COMMAND_PLAN.md`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md`

### SCPI-TASK-20260813-034 - SCPI 指令规范性审查收敛

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将本轮 SCPI 域拆分后的规范性审查结果加入待办，并逐条修正固件注册表、验证工具和活跃文档。
  - 继续坚持不保留兼容入口，避免上位机、HTML、固件和验证脚本各自使用一套命令。
- 完成内容：
  - 业务断点入口从 `BPOint` 收敛到 `BREAkpoint`。
  - 系统维护表入口删除 `PROT:STAT?`、`MODE:TAB?`、`RESource:TAB?`、`FAULT:TAB?` 短别名。
  - 反射内存状态查询改为 `SYSTem:REFMEM:STATus?`。
  - 校准链路修改动词改为 `LINK:UPDate`，新增 `LINK:CLEAr`、`CALibration:STOP`、
    `CALibration:CLEAr`。
  - T2 明细读取改为 `SYSTem:T2:COUNt?` / `SYSTem:T2:DATA?`，移除 `READ:T2:*` 产品注册。
  - 产品运行控制补齐 `TRIGger:ABORt`。
  - BiSS-C 配置参数改为 `FRAMe:BITS`、`POSition:OFFSet`、`POSition:BITS`、
    `POSition:MODulo`。
  - `tools/biss_board_validate.py`、`tools/multicore_board_validate.py` 和
    `tools/distributed_loopback_validate.py` 切到 canonical 命令。
  - 同步 `docs/interface/SCPI_COMMANDS.md`、`docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md`
    和 `docs/arch/RTOS_HAOFV_ARCHITECTURE.md` 的当前接口描述。
- 验证结果：
  - `python tools/product_scpi_validate/product_scpi_validate.py --dry-run`
    通过，生成 `114` 条产品命令。
  - `python tools/realtime_scpi_validate/realtime_scpi_validate.py --dry-run`
    通过，生成 `57` 条 `REALtime:*` 维护命令。
  - `python tools/scpi_legacy_validate/scpi_legacy_validate.py --dry-run`
    通过，生成 `78` 条旧入口删除验证命令。
  - `python -m py_compile` 覆盖 product/realtime/legacy/BiSS/multicore/distributed-loopback
    验证脚本，通过。
  - `python tools/docs_check/docs_check.py` 通过，保留 9 个既有文件名 warning。
  - `git diff --check` 通过，仅保留既有 CRLF 工作区 warning。
  - `cmake --build build-validation` 通过，生成 validation 包，build id：
    `20260813012710`，package CRC：`0xE92D5E11`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 smoke 包，build id：
    `20260813012837`，package CRC：`0x20326F3F`。
  - `cmake --build build` 未使用为最终结论：该目录的 `CMakeCache.txt` 仍指向
    `D:/OneDrive/...`，当前工作区为 `E:/OneDrive/...`，触发 CMake cache 路径不一致；
    本轮改用 E 盘有效缓存的 `build-validation` 和 `build-rtos-multicore-smoke` 验证。
- 还需完成：
  - 后续如需要板端闭环，可 OTA validation 或 smoke 包后执行 product SCPI、
    legacy 删除验证和 multicore board validate。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_config_commands.h`
  - `middleware/scpi_port/inc/scpi_calibration_commands.h`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/inc/scpi_system_diagnostics_commands.h`
  - `middleware/scpi_port/inc/scpi_trigger_commands.h`
  - `middleware/scpi_port/inc/scpi_communication_biss_commands.h`
  - `tools/biss_board_validate/biss_board_validate.py`
  - `tools/multicore_board_validate/multicore_board_validate.py`
  - `tools/distributed_loopback_validate/distributed_loopback_validate.py`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md`
  - `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`

### SCPI-TASK-20260813-033 - P1 裸状态入口文档收口

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 完成 P1 收尾：确认裸 `STATus:*`、裸 `VDC:*` 和裸 `DPLL:*` 不进入当前产品主树。
  - 清理活跃 SCPI 文档中仍把已删除 legacy 入口写成当前接口的内容。
  - 暂不进入 SCPI 指令树整体一致性评审，等所有 legacy 清完后再单独处理。
- 完成内容：
  - `middleware/scpi_port` 注册表检索确认仅剩子域内部
    `COMMunication:BISS:STATus:GATE`，没有裸顶层 `STATus:*` 注册。
  - `docs\interface\SCPI_COMMANDS.md` 将旧裸 `VDC:STAT?`、`DPLL:STAT?`、
    `STATus:VDC?`、`STATus:DPLL?` 改为
    `SYSTem:SYNC:VDC:STATus?` 和 `SYSTem:SYNC:VDC:DPLL:STATus?`。
  - `docs\interface\SCPI_COMMANDS.md` 将旧裸 IO、`TRIGger:SEQuence:*`、
    `TRIGger:PCNT:*`、`STATus:SYNC?` 和 `STATus:TRIG?` 说明改为
    当前 `REALtime:*` 维护域 canonical。
  - `docs\interface\SCPI_COMMAND_PLAN.md` 将“不推荐命令”修正为“已删除旧入口”，
    明确后续若实现 IEEE 488.2 `STATus` register 需独立规划。
  - 新增 `tools\scpi_legacy_validate\scpi_legacy_validate.py`，固化逐条旧入口
    `Undefined header` 验证流程，避免后续手写串口验证脚本。
  - 新增 `tools\ota_boot_commit\ota_boot_commit.py`，固化 OTA 后 `BOOT/COMMit`
    串口重枚举闭环，避免后续手写启动提交脚本。
  - `tools\README.md` 补充上述两个固定工具的使用定位。
- 验证结果：
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run`
    通过，生成 `111` 条产品命令。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run`
    通过，生成 `57` 条 `REALtime:*` 维护命令。
  - `python -m py_compile tools\scpi_legacy_validate\scpi_legacy_validate.py tools\ota_boot_commit\ota_boot_commit.py`
    通过。
  - `python tools\scpi_legacy_validate\scpi_legacy_validate.py --dry-run`
    通过，生成 `58` 条旧入口删除验证命令。
  - `python tools\docs_check\docs_check.py` 通过，保留 9 个历史文件名 warning。
  - `git diff --check` 通过，仅有既有 CRLF 工作区 warning。
  - `cmake --build build` 通过，build id：`20260812163355`，
    `build\RP2350_TRIG_UPDATE.pkg` package CRC：`0xD4A4DF54`。
  - `cmake --build build-rtos-multicore-smoke` 通过，smoke build id：
    `20260812163355`，package CRC：`0xC353324A`。
  - 产品包 OTA 到 COM6 通过；`tools\ota_boot_commit\ota_boot_commit.py`
    验证并 commit 后，`SYSTem:FW:BUILD? -> "20260812163355"`，
    `SYSTem:OTA:SLOT? -> 2,0,2,0,0`，`SYSTem:ERRor? -> 0,"No error"`。
  - `python tools\product_scpi_validate\product_scpi_validate.py COM6 --out-dir build\product_scpi_validation_scpi_status_doc_cleanup`
    实机通过：`summary: passed=True failed=0`。
  - `python tools\scpi_legacy_validate\scpi_legacy_validate.py COM6 --group status-doc-cleanup --out-dir build\legacy_status_doc_cleanup_validation_tool`
    实机通过：旧裸状态、旧实时、旧 IO 入口共 `58` 条全部返回
    `-113,"Undefined header"`，最终错误队列 `0,"No error"`。
  - smoke 包 OTA 到 COM6 通过；`tools\ota_boot_commit\ota_boot_commit.py`
    验证并 commit 后，smoke 固件运行正常。
  - `python tools\multicore_board_validate\multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke\validation_scpi_status_doc_cleanup_full`
    实机通过：`16/16 PASS`。
  - 重新 OTA 回产品包并 commit 后，
    `python tools\product_scpi_validate\product_scpi_validate.py COM6 --out-dir build\product_scpi_validation_scpi_status_doc_cleanup_final`
    实机通过：`summary: passed=True failed=0`；板端最终停在产品固件
    `20260812163355`，slot `2,0,2,0,0`。
- 还需完成：
  - P1 legacy 清理已完成；下一轮继续 P2 重复读取和报告域收敛。
- 关联文件：
  - `docs/interface/SCPI_TASK_PROGRESS.md`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/interface/SCPI_COMMAND_PLAN.md`
  - `tools/scpi_legacy_validate/scpi_legacy_validate.py`
  - `tools/ota_boot_commit/ota_boot_commit.py`
  - `tools/README.md`

### SCPI-TASK-20260813-032 - 删除系统维护短旧入口

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 完成 P1 系统维护域收敛：保留 `SYSTem:CONFigure:*`、`SYSTem:REFMEM:*`、
    `SYSTem:CORE:VECTOR?` 作为 canonical。
  - 删除旧 `SYSTem:CFG:*`、`STATus:CFG?`、`SYSTem:REFM:*` 和
    `SYSTem:CORE:VECT?`，不保留兼容入口。
  - 同步当前验证脚本和活跃 SCPI 文档，避免后续工具继续依赖旧入口。
- 完成内容：
  - `SCPI_SYSTEM_SNAPSHOT_COMMANDS` 删除旧 `SYSTem:CFG:*`、`STATus:CFG?`、
    `SYSTem:REFM:*` 和 `SYSTem:CORE:VECT?` pattern。
  - `SYSTem:REFMEM:STATUS?`、`SYSTem:REFMEM:NODE?` 和 `SYSTem:CORE:VECTOR?`
    使用全长关键字段注册，避免 SCPI 最短缩写继续接受旧 `REFM` / `VECT`。
  - `tools\multicore_board_validate\multicore_board_validate.py` 切到
    `SYSTem:CONFigure:*` 和 `SYSTem:CORE:VECTOR?`。
  - `tools\distributed_loopback_validate\distributed_loopback_validate.py` 切到
    `SYSTem:CONFigure:*`。
  - 同步 `docs\interface\SCPI_COMMANDS.md`、
    `docs\interface\RP1200波导天线测试系统分布式触发方案SCPI指令表.md`、对应 HTML、
    `docs\interface\SCPI_COMMAND_PLAN.md` 和 `tools\README.md` 的当前接口说明。
- 验证结果：
  - 代码和工具检索确认 `middleware` / `tools` 中不再依赖旧
    `SYSTem:CFG:*`、`SYSTem:REFM:*`、`SYSTem:CORE:VECT?` 和 `STATus:CFG?`。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，
    生成 `111` 条产品命令。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run` 通过，
    生成 `57` 条 `REALtime:*` canonical 维护命令。
  - 首轮发现 `SYSTem:REFM:*` / `SYSTem:CORE:VECT?` 仍可被 libscpi 当作
    `REFMem` / `VECTor` 的标准短写匹配；随后将 canonical 注册改为全长
    `REFMEM` / `VECTOR`，重新构建验证。
  - `cmake --build build` 通过，最终 build id：`20260812161738`，
    `build\RP2350_TRIG_UPDATE.pkg` package CRC：`0x587B3797`。
  - `python tools\docs_check\docs_check.py` 通过，保留 9 个历史文件命名 warning。
  - `git diff --check` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 smoke build id：
    `20260812161125`。
  - 产品包 OTA 通过 COM6 写入并 commit，最终运行
    `SYSTem:FW:BUILD? -> "20260812161738"`，`SYSTem:OTA:SLOT? -> 1,0,1,0,0`。
  - canonical 长入口实机验证通过：
    `SYSTem:REFMEM:STATUS?`、`SYSTem:REFMEM:NODE?`、`SYSTem:CORE:VECTOR?`、
    `SYSTem:CONFigure:STAT?`、`SYSTem:CONFigure:ACK?`、`SYSTem:CONFigure:NACK?`。
  - 逐条旧入口删除验证通过：旧 `SYSTem:REFM:*`、`SYSTem:CORE:VECT?`、
    `SYSTem:CFG:*` 和 `STATus:CFG?` 共 `21` 条全部返回 `-113,"Undefined header"`。
  - `python tools\multicore_board_validate\multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke\validation_scpi_system_alias_removal_full`
    在 smoke 固件上实机通过：`16/16 PASS`。
  - 重新 OTA 回产品包并 commit 后，
    `python tools\product_scpi_validate\product_scpi_validate.py COM6 --out-dir build\product_scpi_validation_scpi_system_alias_removal_final`
    实机通过：`summary: passed=True failed=0`。
- 还需完成：
  - P1 剩余项只保留“裸 `STATus:*` 不进入产品主树”的后续标准状态寄存器规划，不再有当前固件旧入口删除项。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `tools/multicore_board_validate/multicore_board_validate.py`
  - `tools/distributed_loopback_validate/distributed_loopback_validate.py`
  - `docs/interface/SCPI_TASK_PROGRESS.md`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/interface/SCPI_COMMAND_PLAN.md`
  - `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md`
  - `docs/reports/scpi/RP1200波导天线测试系统分布式触发方案SCPI指令表.html`
  - `tools/README.md`

### SCPI-TASK-20260813-031 - 删除 BiSS-C 旧 TRIGger 通信入口

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 完成 P1 通信域首项：保留 `COMMunication:BISS:*` 作为 BiSS-C canonical 主入口。
  - 删除旧 `TRIGger:BISS:*` 和 `STATus:BISS?`，避免产品 `TRIGger:*` 再承载 BiSS-C 底层协议配置。
  - 通过构建、OTA 和实机逐条验证确认 canonical 可用、旧入口返回 undefined header。
- 完成内容：
  - `SCPI_COMMUNICATION_BISS_COMMANDS` 删除旧 `TRIGger:BISS:*` 和 `STATus:BISS?` pattern。
  - `COMMunication:BISS:*` callback 和响应格式保持不变。
  - P1 BiSS-C 待办标记完成。
  - P1 系统维护别名策略按最新要求修正：`SYSTem:CFG:*`、`SYSTem:REFM:*`、
    `SYSTem:CORE:VECT?` 后续同样删除，不保留兼容入口。
- 验证结果：
  - 代码检索确认 `middleware/scpi_port` 中不再注册旧 `TRIGger:BISS:*` 和 `STATus:BISS?`。
  - `cmake --build build` 通过，build id：`20260812155051`，
    `build\RP2350_TRIG_UPDATE.pkg` package CRC：`0x02B09C2D`。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，
    生成 `111` 条产品命令。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run` 通过，
    生成 `57` 条 `REALtime:*` canonical 维护命令。
  - `python tools\docs_check\docs_check.py` 通过，保留 9 个历史文件命名 warning。
  - `git diff --check` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - OTA 通过 COM6 写入 `build\RP2350_TRIG_UPDATE.pkg`，`SYSTem:OTA:BOOT` 后运行
    `SYSTem:FW:BUILD? -> "20260812155051"`，`SYSTem:OTA:SLOT? -> 1,0,2,1,0`。
  - `SYSTem:OTA:COMMit` 通过，`SYSTem:OTA:SLOT? -> 1,0,1,0,0`，
    `SYSTem:ERRor? -> 0,"No error"`。
  - `python tools\biss_board_validate\biss_board_validate.py COM6 --skip-arm --skip-inject`
    实机通过，输出目录 `build\biss_validation_scpi_comm_biss_alias_removal`。
  - `python tools\product_scpi_validate\product_scpi_validate.py COM6 --out-dir build\product_scpi_validation_scpi_comm_biss_alias_removal`
    实机通过：`summary: passed=True failed=0`。
  - 逐条旧入口删除验证通过：旧 `TRIGger:BISS:*` 和 `STATus:BISS?` 共 `71` 条全部返回
    `-113,"Undefined header"`。
- 还需完成：
  - 下一轮处理 P1 系统维护别名删除：
    `SYSTem:CFG:*`、`SYSTem:REFM:*`、`SYSTem:CORE:VECT?`。
- 关联文件：
  - `docs/interface/SCPI_TASK_PROGRESS.md`
  - `middleware/scpi_port/inc/scpi_communication_biss_commands.h`

### SCPI-TASK-20260812-030 - 删除 realtime STATUS 旧 TRIGger 查询

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 完成 P0 产品 `TRIGger` 域瘦身最后一项：删除旧 `STATus:TRIGger?`。
  - 保留 `REALtime:STATus?` 作为底层实时维护状态 canonical 查询。
  - 确认产品 `TRIGger:*` 不再承载底层实时 PCNT/ENC/SEQ/IO/STATUS 验证入口。
- 完成内容：
  - `SCPI_REALTIME_STATUS_COMMANDS` 删除旧 `STATus:TRIGger?` pattern。
  - P0 待办中 `STATus:TRIGger?` 删除项和 canonical-only 验证策略标记完成。
- 验证结果：
  - 代码检索确认 `middleware/scpi_port` 中不再注册旧
    `STATus:TRIGger?`，也不再注册旧 `TRIGger:SOURce/EDGE/GATE/SAFE/SEQ/ARM/DISarm/DISAble/FAULT/PCNT/ENC/WIDTh/IMMediate`。
  - `cmake --build build` 通过，build id：`20260812154120`，
    `build\RP2350_TRIG_UPDATE.pkg` package CRC：`0xEC3C99BA`。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，
    生成 `111` 条产品命令。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run` 通过，
    生成 `57` 条 `REALtime:*` canonical 维护命令。
  - `python tools\docs_check\docs_check.py` 通过，保留 9 个历史文件命名 warning。
  - `git diff --check` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - OTA 通过 COM6 写入 `build\RP2350_TRIG_UPDATE.pkg`，`SYSTem:OTA:BOOT` 后运行
    `SYSTem:FW:BUILD? -> "20260812154120"`，`SYSTem:OTA:SLOT? -> 2,0,1,1,0`。
  - `SYSTem:OTA:COMMit` 通过，`SYSTem:OTA:SLOT? -> 2,0,2,0,0`。
  - 逐条 status 实机验证通过：`REALtime:STATus?` 返回 9 字段状态块；
    `STATus:TRIGger?` 返回 `-113,"Undefined header"`。
  - `python tools\product_scpi_validate\product_scpi_validate.py COM6` 实机通过：
    `summary: passed=True failed=0`，输出目录
    `build\product_scpi_validation_20260812_234318`。
- 还需完成：
  - P0 产品 `TRIGger` 域瘦身完成；下一步进入 P1。
  - P1 首项建议处理 BiSS-C：逐条确认 `COMMunication:BISS:*` 覆盖后，删除
    `TRIGger:BISS:*` 和 `STATus:BISS?`。
- 关联文件：
  - `docs/interface/SCPI_TASK_PROGRESS.md`
  - `middleware/scpi_port/inc/scpi_realtime_status_commands.h`

### SCPI-TASK-20260812-029 - 删除 realtime IO 裸旧入口

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 继续按“一组一闭环、逐条测试”的策略，删除已经由 `REALtime:IO:*` 覆盖的裸 IO 旧入口。
  - 本轮删除旧 `TRIGger:WIDTh/IMMediate`、`PULSe:*`、`MARKer:*`、`RJ45:TRIGger:*`、
    `SAMPle:*`、`OUTPut:CLOCk:*` 和 `STATus:SYNC?`。
- 完成内容：
  - `SCPI_REALTIME_IO_COMMANDS` 删除裸旧 IO pattern。
  - 保留 `REALtime:IO:OUTPut/PULSe/MARKer/RJ45/SAMPle/CLOCk/SYNC?` canonical。
  - P0 待办中裸 IO 旧入口删除项标记完成。
- 验证结果：
  - 代码检索确认 `middleware/scpi_port` 中不再注册裸旧 IO 入口；检索结果只剩
    `REALtime:IO:*` 和产品测量域 `MEASure:PULSe:WIDTh?`。
  - `cmake --build build` 通过，build id：`20260812153144`，
    `build\RP2350_TRIG_UPDATE.pkg` package CRC：`0x8B164CB6`。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，
    生成 `111` 条产品命令。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run` 通过，
    生成 `57` 条 `REALtime:*` canonical 维护命令。
  - `python tools\docs_check\docs_check.py` 通过，保留 9 个历史文件命名 warning。
  - `git diff --check` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - OTA 通过 COM6 写入 `build\RP2350_TRIG_UPDATE.pkg`，`SYSTem:OTA:BOOT` 后运行
    `SYSTem:FW:BUILD? -> "20260812153144"`，`SYSTem:OTA:SLOT? -> 1,0,2,1,0`。
  - `SYSTem:OTA:COMMit` 通过，`SYSTem:OTA:SLOT? -> 1,0,1,0,0`。
  - 逐条 canonical IO 实机验证通过：`REALtime:IO:*` 共 `14` 个写/读用例覆盖 `22` 条
    canonical pattern；写命令按本模块语义无响应，但逐条确认 `SYSTem:ERRor? -> 0,"No error"`，
    可回读配置均逐条回读一致。
  - 逐条旧入口删除验证通过：裸旧 IO 入口共 `22` 条逐条返回 `-113,"Undefined header"`。
  - `python tools\product_scpi_validate\product_scpi_validate.py COM6` 实机通过：
    `summary: passed=True failed=0`，输出目录
    `build\product_scpi_validation_20260812_233528`。
- 还需完成：
  - 继续按一组一闭环删除 `STATus:TRIGger?`，确认 `REALtime:STATus?` 已覆盖。
  - 之后进入 P1：BiSS-C 先确认 `COMMunication:BISS:*` 覆盖，再删除 `TRIGger:BISS:*` 和
    `STATus:BISS?`。
- 关联文件：
  - `docs/interface/SCPI_TASK_PROGRESS.md`
  - `middleware/scpi_port/inc/scpi_realtime_io_commands.h`

### SCPI-TASK-20260812-028 - 删除 realtime ENC 旧 TRIGger 入口

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 继续按“一组一闭环、逐条测试”的策略，删除已经由 `REALtime:ENC:*` 覆盖的旧
    `TRIGger:ENC:*` 入口。
  - 确认 encoder 目标计数、计数查询、A/B/Z 引脚查询和圈数查询的 canonical 参数与响应覆盖旧入口。
- 完成内容：
  - `SCPI_REALTIME_ENCODER_COMMANDS` 删除旧
    `TRIGger:ENC:TARGet/TARGet?/COUNt?/APIN/APIN?/REVolution?` pattern。
  - 保留 `REALtime:ENC:TARGet/TARGet?/COUNt?/APIN/APIN?/REVolution?` canonical。
  - P0 待办中 `TRIGger:ENC:*` 删除项标记完成。
- 验证结果：
  - 代码检索确认 `middleware/scpi_port` 中不再注册 `TRIGger:ENC:*`。
  - `cmake --build build` 通过，build id：`20260812151951`，
    `build\RP2350_TRIG_UPDATE.pkg` package CRC：`0x02FFCC6E`。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，
    生成 `111` 条产品命令。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run` 通过，
    生成 `57` 条 `REALtime:*` canonical 维护命令。
  - `python tools\docs_check\docs_check.py` 通过，保留 9 个历史文件命名 warning。
  - `git diff --check` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - OTA 通过 COM6 写入 `build\RP2350_TRIG_UPDATE.pkg`，`SYSTem:OTA:BOOT` 后运行
    `SYSTem:FW:BUILD? -> "20260812151951"`，`SYSTem:OTA:SLOT? -> 2,0,1,1,0`。
  - `SYSTem:OTA:COMMit` 通过，`SYSTem:OTA:SLOT? -> 2,0,2,0,0`。
  - 逐条 canonical ENC 实机验证通过：`REALtime:ENC:*` 共 `6` 条逐条 PASS。
  - 逐条旧入口删除验证通过：`TRIGger:ENC:*` 共 `6` 条逐条返回
    `-113,"Undefined header"`。
  - `python tools\product_scpi_validate\product_scpi_validate.py COM6` 实机通过：
    `summary: passed=True failed=0`，输出目录
    `build\product_scpi_validation_20260812_232146`。
- 还需完成：
  - 继续按一组一闭环删除裸 IO 旧入口，确认 `REALtime:IO:*` 已逐条覆盖。
  - 继续按一组一闭环删除 `STATus:TRIGger?`，确认 `REALtime:STATus?` 已覆盖。
- 关联文件：
  - `docs/interface/SCPI_TASK_PROGRESS.md`
  - `middleware/scpi_port/inc/scpi_realtime_encoder_commands.h`

### SCPI-TASK-20260812-027 - 删除 realtime sequence 和 PCNT 旧 TRIGger 入口

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 按“不兼容保留，改完之后直接删除”的策略，删除已经由 `REALtime:*` canonical 覆盖的旧
    `TRIGger:*` 底层实时入口。
  - 本轮只处理 sequence/gate/arm/fault 和 PCNT 两组，逐条验证每个正式入口可用、每个旧入口失效。
  - 将 SCPI 维护规范化待办从“legacy alias 隔离”调整为“canonical 覆盖后删除”。
- 完成内容：
  - 删除 `SCPI_LEGACY_VALIDATION_COMMANDS` 聚合入口和 `scpi_legacy_validation_commands.h`。
  - `scpi_port.c` 不再挂载 legacy validation 命令宏。
  - `SCPI_REALTIME_PCNT_COMMANDS` 删除旧 `TRIGger:PCNT:*` pattern，仅保留 `REALtime:PCNT:*`。
  - 上一轮已从 `SCPI_REALTIME_SEQUENCE_COMMANDS` 拆出的旧
    `TRIGger:SOURce/EDGE/GATE/SAFE/SEQ/ARM/DISarm/DISAble/FAULT` 不再通过 legacy 聚合入口注册。
  - 待办原则更新为：已有正式归属并逐条验证覆盖后，直接删除旧入口；无正式归属的功能先释放到
    正式域，再删除旧入口。
- 验证结果：
  - 代码检索确认 `middleware/scpi_port` 中不再注册
    `TRIGger:SOURce/EDGE/GATE/SAFE/SEQ/ARM/DISarm/DISAble/FAULT/PCNT:*`。
  - `cmake --build build` 通过，build id：`20260812150846`，
    `build\RP2350_TRIG_UPDATE.pkg` package CRC：`0xD244950E`。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，
    生成 `111` 条产品命令。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run` 通过，
    生成 `57` 条 `REALtime:*` canonical 维护命令。
  - `python tools\docs_check\docs_check.py` 通过，保留 9 个历史文件命名 warning。
  - `git diff --check` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - OTA 通过 COM6 写入 `build\RP2350_TRIG_UPDATE.pkg`，`SYSTem:OTA:BOOT` 后运行
    `SYSTem:FW:BUILD? -> "20260812150846"`，`SYSTem:OTA:SLOT? -> 1,0,2,1,0`。
  - `SYSTem:OTA:COMMit` 通过，`SYSTem:OTA:SLOT? -> 1,0,1,0,0`。
  - 逐条 canonical 实机验证通过：`REALtime:SOURce/EDGE/GATE/SAFE/SEQ/ARM/DISarm/DISAble/FAULT`
    和 `REALtime:PCNT:*` 共 `32` 条逐条 PASS。
  - 逐条旧入口删除验证通过：对应 `TRIGger:*` 旧入口共 `20` 条逐条返回
    `-113,"Undefined header"`。
  - `python tools\product_scpi_validate\product_scpi_validate.py COM6` 实机通过：
    `summary: passed=True failed=0`，输出目录
    `build\product_scpi_validation_20260812_231230`。
- 还需完成：
  - 继续按一组一闭环删除 `TRIGger:ENC:*`，确认 `REALtime:ENC:*` 已逐条覆盖。
  - 继续按一组一闭环删除裸 IO 旧入口，确认 `REALtime:IO:*` 已逐条覆盖。
  - 继续按一组一闭环删除 `STATus:TRIGger?`，确认 `REALtime:STATus?` 已覆盖。
- 关联文件：
  - `docs/interface/SCPI_TASK_PROGRESS.md`
  - `middleware/scpi_port/inc/scpi_realtime_pcnt_commands.h`
  - `middleware/scpi_port/src/scpi_port.c`
  - `middleware/scpi_port/inc/scpi_legacy_validation_commands.h`

### SCPI-TASK-20260812-026 - Legacy realtime sequence alias 拆出

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 按 P0 产品 `TRIGger` 域瘦身待办，先把底层 realtime sequence 的旧 `TRIGger:*` alias
    从 canonical `SCPI_REALTIME_SEQUENCE_COMMANDS` 中拆出。
  - 保持旧 alias 可用，不改变板端行为，只改变命令表归属边界。
  - 建立后续 PCNT、ENC、IO、STATUS 和 BiSS legacy alias 继续迁移的聚合入口。
- 完成内容：
  - 新增 `middleware/scpi_port/inc/scpi_legacy_validation_commands.h`。
  - 新增 `SCPI_LEGACY_REALTIME_SEQUENCE_COMMANDS`，集中注册旧
    `TRIGger:SOURce/EDGE/GATE/SAFE/SEQ/ARM/DISarm/DISAble/FAULT` alias。
  - `SCPI_REALTIME_SEQUENCE_COMMANDS` 只保留 `REALtime:*` canonical pattern。
  - `scpi_port.c` 单独挂载 `SCPI_LEGACY_VALIDATION_COMMANDS`，让 legacy validation 与产品/维护
    canonical 宏在代码结构上分离。
  - 在本文档新增 SCPI 维护规范化待办，按 P0/P1/P2 记录后续收敛顺序。
- 验证结果：
  - `cmake --build build` 通过，build id：`20260812145629`，
    `build\RP2350_TRIG_UPDATE.pkg` package CRC：`0x27884087`。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，
    生成 `111` 条产品命令。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run` 通过，
    生成 `57` 条 `REALtime:*` canonical 维护命令。
  - `python tools\docs_check\docs_check.py` 通过，保留 9 个历史文件命名 warning。
  - `git diff --check` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - OTA 通过 COM6 写入 `build\RP2350_TRIG_UPDATE.pkg`，`SYSTem:OTA:BOOT` 后运行
    `SYSTem:FW:BUILD? -> "20260812145629"`，`SYSTem:OTA:SLOT? -> 2,0,1,1,0`。
  - `SYSTem:OTA:COMMit` 通过，`SYSTem:OTA:SLOT? -> 2,0,2,0,0`。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py COM6` 实机通过：
    `summary: passed=True failed=0 generated=57`。
  - legacy alias 查询烟测通过：`TRIGger:SOURce? -> 16`、
    `TRIGger:EDGE? -> "RISING",0`、`TRIGger:SEQ:INDex? -> 0`。
  - `python tools\product_scpi_validate\product_scpi_validate.py COM6` 实机通过：
    `summary: passed=True failed=0`，输出目录
    `build\product_scpi_validation_20260812_225816`。
- 还需完成：
  - 继续从 PCNT、ENC、IO、STATUS 和 BiSS canonical 宏中拆出 legacy alias。
  - 后续为 validation 脚本增加显式 `--legacy` 选项，默认只验证 canonical。
- 关联文件：
  - `docs/interface/SCPI_TASK_PROGRESS.md`
  - `middleware/scpi_port/inc/scpi_legacy_validation_commands.h`
  - `middleware/scpi_port/inc/scpi_realtime_sequence_commands.h`
  - `middleware/scpi_port/src/scpi_port.c`

### SCPI-TASK-20260812-025 - REALtime IO 输出命名去 TRIGger

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 按“慢慢将 realtime 中的 trig 删除”的原则，先从新 `REALtime:*` 主域中移除 IO 输出子树里的
    `TRIGger` 命名。
  - 将 `REALtime:IO:TRIGger:*` 收敛为 `REALtime:IO:OUTPut:*`，避免底层实时维护域继续复用
    产品 `TRIGger:*` 的业务运行语义。
  - 在规划文档中补充 `REALtime` 主域定位，明确它是底层实时维护和 validation 域，不是现场测试主链路。
- 完成内容：
  - `REALtime:IO:TRIGger:WIDTh/WIDTh?/IMMediate` 改为
    `REALtime:IO:OUTPut:WIDTh/WIDTh?/IMMediate`。
  - `tools/realtime_scpi_validate/realtime_scpi_validate.py` 同步使用新的
    `REALtime:IO:OUTPut:*` 验证入口。
  - `docs/interface/SCPI_COMMAND_PLAN.md` 增加 `REALtime` 主域定位小节，写明
    `REALtime:PCNT/ENC/SEQ/IO/STATus` 的职责、权限边界和 legacy alias 策略。
  - 文档中的产品 `TRIGger:*` 收敛为 start/stop/pause/continue 和运行模式切换，不再写
    `arm/disarm`。
- 验证结果：
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run` 通过，
    生成 `57` 条 `REALtime:*` 维护域验证命令。
  - `cmake --build build` 通过，build id：`20260812143237`，
    `build\RP2350_TRIG_UPDATE.pkg` package CRC：`0x6F668981`。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，
    仍生成 `111` 条产品命令。
  - `python tools\docs_check\docs_check.py` 通过，保留 9 个历史文件命名 warning。
  - `git diff --check` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - OTA 通过 COM6 写入 `build\RP2350_TRIG_UPDATE.pkg`，`SYSTem:OTA:BOOT` 后运行
    `SYSTem:FW:BUILD? -> "20260812143237"`，`SYSTem:OTA:SLOT? -> 1,0,2,1,0`。
  - `SYSTem:OTA:COMMit` 通过，`SYSTem:OTA:SLOT? -> 1,0,1,0,0`。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py COM6` 实机通过：
    `summary: passed=True failed=0 generated=57`。
  - `python tools\product_scpi_validate\product_scpi_validate.py COM6` 实机通过：
    `summary: passed=True failed=0`，输出目录
    `build\product_scpi_validation_20260812_223508`。
- 还需完成：
  - 继续分阶段清理 realtime 内部 callback、文件名、注释和 legacy alias 中不必要的 trigger 命名。
  - 后续再评审 `REALtime:SEQ:*`、`REALtime:ARM/DISarm/*` 是否需要进一步拆成更贴近执行层的子域。
- 关联文件：
  - `docs/interface/SCPI_COMMAND_PLAN.md`
  - `middleware/scpi_port/inc/scpi_realtime_io_commands.h`
  - `tools/realtime_scpi_validate/realtime_scpi_validate.py`

### SCPI-TASK-20260812-024 - REALtime 维护域规范化

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 将底层实时验证入口从产品 `TRIGger:*` 主树提升为独立 `REALtime:*` 维护域。
  - 保留旧 `TRIGger:*`、`PULSe:*`、`MARKer:*`、`RJ45:*`、`SAMPle:*`、
    `OUTPut:CLOCk:*` 和 `STATus:*` 入口作为 legacy validation alias。
  - 增加 realtime 维护域验证脚本，避免该域游离在产品验证脚本之外。
- 完成内容：
  - 新增 `REALtime:PCNT:*`、`REALtime:ENC:*`、`REALtime:SEQ:*`、
    `REALtime:SOURce/EDGE/GATE/SAFE`、`REALtime:ARM/DISarm/DISAble/FAULT`、
    `REALtime:IO:*` 和 `REALtime:STATus?` 命令 pattern。
  - 旧底层实时入口继续注册，作为兼容 alias，不再作为产品主流程扩展入口。
  - 新增 `tools/realtime_scpi_validate/realtime_scpi_validate.py`，从 realtime 头文件
    生成 `REALtime:*` 验证用例。
  - realtime 验证脚本按维护域语义处理写命令：写命令验证可发送并 drain ACK，查询命令验证字段数。
  - 文档补充 `REALtime:*` 作为实质 realtime 维护域，产品 `TRIGger:*` 保持运行控制语义。
- 验证结果：
  - `cmake --build build` 通过，build id：`20260812141458`，
    `build\RP2350_TRIG_UPDATE.pkg` package CRC：`0xD37D2D89`。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，
    仍生成 `111` 条产品命令，确认 `REALtime:*` 未混入产品主树。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run` 通过，
    生成 `57` 条 `REALtime:*` 维护域验证命令。
  - `python tools\docs_check\docs_check.py` 通过，保留 9 个历史文件命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - OTA 通过 COM6 写入 `build\RP2350_TRIG_UPDATE.pkg`，`SYSTem:OTA:BOOT` 后运行
    `SYSTem:FW:BUILD? -> "20260812141458"`。
  - `SYSTem:OTA:COMMit` 后 `SYSTem:OTA:SLOT? -> 2,0,2,0,0`。
  - `python tools\product_scpi_validate\product_scpi_validate.py COM6` 实机通过：
    `summary: passed=True failed=0`，输出目录
    `build\product_scpi_validation_20260812_221703`。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py COM6` 实机通过：
    `summary: passed=True failed=0 generated=57`。
  - realtime 验证脚本修正 drain 后，再次运行产品实机验证通过：
    `summary: passed=True failed=0`，输出目录
    `build\product_scpi_validation_20260812_222154`。
- 还需完成：
  - 继续规范化正式 SCPI 指令表和 HTML，冻结产品主树、维护域和 legacy alias 边界。
  - 评审是否在产品指令表中增加 `REALtime:*` 维护页，或只放入开发/维护附录。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_realtime_pcnt_commands.h`
  - `middleware/scpi_port/inc/scpi_realtime_encoder_commands.h`
  - `middleware/scpi_port/inc/scpi_realtime_sequence_commands.h`
  - `middleware/scpi_port/inc/scpi_realtime_io_commands.h`
  - `middleware/scpi_port/inc/scpi_realtime_status_commands.h`
  - `tools/realtime_scpi_validate/realtime_scpi_validate.py`

### SCPI-TASK-20260812-023 - Realtime STATUS 命令细分

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 将 `STATus:TRIGger?` 从 realtime component 聚合文件中拆出。
  - 保持 `SCPI_REALTIME_COMPONENT_COMMANDS` 聚合入口不变，完成 PCNT/ENC/IO/SEQ/STATUS
    realtime 子域拆分链路。
  - 为后续内部实时状态查询扩展保留独立 status 模块。
- 完成内容：
  - 新增 `scpi_realtime_status_commands.c/.h`。
  - `STATus:TRIGger?` 和底层 trigger mode 字符串转换移入 STATUS 子模块。
  - `scpi_realtime_component_commands.h` 引入 `SCPI_REALTIME_STATUS_COMMANDS`。
  - `scpi_realtime_component_commands.c` 收缩为聚合占位源文件，后续可按 CMake 边界决定是否删除。
  - `CMakeLists.txt` 纳入新 STATUS 源文件。
- 验证结果：
  - `cmake --build build` 通过，build id：`20260812133754`，
    `build\RP2350_TRIG_UPDATE.pkg` package CRC：`0x47BA0ADD`。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，
    生成 `111` 条产品命令。
  - `python tools\docs_check\docs_check.py` 通过，保留 9 个历史文件命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - OTA 通过 COM6 写入 `build\RP2350_TRIG_UPDATE.pkg`，`SYSTem:OTA:BOOT` 后运行
    `SYSTem:FW:BUILD? -> "20260812133754"`。
  - `SYSTem:OTA:COMMit` 后 `SYSTem:OTA:SLOT? -> 1,0,1,0,0`。
  - `python tools\product_scpi_validate\product_scpi_validate.py COM6` 实机通过：
    `summary: passed=True failed=0`，输出目录
    `build\product_scpi_validation_20260812_213929`。
- 还需完成：
  - realtime 拆分链路已完成；下一轮转向序列触发和脉冲计数内部基础组件实现。
- 关联文件：
  - `middleware/scpi_port/src/scpi_realtime_component_commands.c`
  - `middleware/scpi_port/inc/scpi_realtime_component_commands.h`
  - `middleware/scpi_port/src/scpi_realtime_status_commands.c`
  - `middleware/scpi_port/inc/scpi_realtime_status_commands.h`

### SCPI-TASK-20260812-022 - Realtime SEQ 命令细分

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 将 `TRIGger:SEQ:*`、source/edge/gate/safe 和低层 ARM/DISARM/FAULT 验证入口
    从 realtime component 聚合文件中拆出。
  - 保持产品命令树、callback 行为和 `SCPI_REALTIME_COMPONENT_COMMANDS` 聚合入口不变。
  - 让 `scpi_realtime_component_commands.c` 继续收缩，为后续 STATUS 独立拆分做准备。
- 完成内容：
  - 新增 `scpi_realtime_sequence_commands.c/.h`。
  - 移入 `TRIGger:SEQ:LENGth/LENGth?/WIDTh/WIDTh?/INDex?/DATA/DATA?`。
  - 移入 `TRIGger:SOURce/SOURce?/EDGE/EDGE?/GATE/GATE?/SAFE/SAFE?`。
  - 移入 `TRIGger:ARM/DISarm/DISAble/FAULT` 低层验证路径。
  - `scpi_realtime_component_commands.h` 引入 `SCPI_REALTIME_SEQUENCE_COMMANDS`，
    `CMakeLists.txt` 纳入新 SEQ 源文件。
- 验证结果：
  - `cmake --build build` 通过，build id：`20260812133120`，
    `build\RP2350_TRIG_UPDATE.pkg` package CRC：`0x01F0A2EF`。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，
    生成 `111` 条产品命令。
  - `python tools\docs_check\docs_check.py` 通过，保留 9 个历史文件命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - OTA 通过 COM6 写入 `build\RP2350_TRIG_UPDATE.pkg`，`SYSTem:OTA:BOOT` 后运行
    `SYSTem:FW:BUILD? -> "20260812133120"`。
  - `SYSTem:OTA:COMMit` 后 `SYSTem:OTA:SLOT? -> 2,0,2,0,0`。
  - `python tools\product_scpi_validate\product_scpi_validate.py COM6` 实机通过：
    `summary: passed=True failed=0`，输出目录
    `build\product_scpi_validation_20260812_213326`。
- 还需完成：
  - 拆分 `STATUS` realtime 子域，将 `STATus:TRIGger?` 移入独立状态模块。
- 关联文件：
  - `middleware/scpi_port/src/scpi_realtime_component_commands.c`
  - `middleware/scpi_port/inc/scpi_realtime_component_commands.h`
  - `middleware/scpi_port/src/scpi_realtime_sequence_commands.c`
  - `middleware/scpi_port/inc/scpi_realtime_sequence_commands.h`

### SCPI-TASK-20260812-021 - Realtime IO 命令细分

- 状态：完成
- 日期：2026-08-12
- 任务目标：
  - 将 realtime 中的即时 IO、脉冲、标记、RJ45 触发、采样和输出时钟命令拆出。
  - 保持 `SCPI_REALTIME_COMPONENT_COMMANDS` 作为聚合入口，继续降低
    `scpi_realtime_component_commands.c` 的职责和文件长度。
  - 验证拆分后产品 SCPI 命令数量、构建产物和板端行为不变化。
- 完成内容：
  - 新增 `scpi_realtime_io_commands.c/.h`。
  - `TRIGger:WIDTh`、`TRIGger:IMMediate`、`PULSe:*`、`MARKer:*`、
    `RJ45:TRIGger:*`、`SAMPle:*`、`OUTPut:CLOCk:*`、`STATus:SYNC?`
    从 `scpi_realtime_component_commands.c` 移入 IO 子模块。
  - `scpi_realtime_component_commands.h` 引入 `SCPI_REALTIME_IO_COMMANDS`，
    与 PCNT、ENC 子模块一起组成 realtime 聚合入口。
  - `CMakeLists.txt` 纳入新 IO 源文件。
- 验证结果：
  - `cmake --build build` 通过，build id：`20260812131915`。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，
    生成 `111` 条产品命令。
  - `python tools\docs_check\docs_check.py` 通过，保留 9 个历史文件命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - OTA 通过 COM6 写入 `build\RP2350_TRIG_UPDATE.pkg`，`SYSTem:OTA:BOOT` 后运行
    `SYSTem:FW:BUILD? -> "20260812131915"`。
  - `SYSTem:OTA:COMMit` 后 `SYSTem:OTA:SLOT? -> 1,0,1,0,0`。
  - `python tools\product_scpi_validate\product_scpi_validate.py COM6` 实机通过：
    `summary: passed=True failed=0`，输出目录
    `build\product_scpi_validation_20260812_212109`。
- 还需完成：
  - 继续按计划拆分 `SEQ` 和 `STATUS` realtime 子域。
- 关联文件：
  - `middleware/scpi_port/src/scpi_realtime_component_commands.c`
  - `middleware/scpi_port/inc/scpi_realtime_component_commands.h`
  - `middleware/scpi_port/src/scpi_realtime_io_commands.c`
  - `middleware/scpi_port/inc/scpi_realtime_io_commands.h`

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
  - `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
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
  - `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
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
