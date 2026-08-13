# HAOFV 架构维护独立待办

Status: Active
Domain: HAOFV
Canonical: `docs/arch/HAOFV_MAINTENANCE_TODO.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/HAOFV_IMPLEMENTATION_PLAYBOOK.md`, `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/interface/DTC100_SCPI_COMMAND_PLANNING.md`, `docs/interface/SCPI_TASK_PROGRESS.md`
Last updated: 2026-08-13

本文档用于独立维护 DTC100 / RP2350_TRIG 工程对 HAOFV 架构的符合性待办。
这里不记录普通功能开发流水账，而是记录会影响 owner、层次边界、反射内存事实、
Active Object / Function Block 划分、资源仲裁和硬实时边界的架构问题。

## 维护原则

- SCPI / UI / System Pack 只能表达意图、配置和查询，不直接驱动硬实时边沿。
- 产品 `TRIGger` 域是业务动作域，底层 PIO/DMA/IRQ 维护能力归 `REALtime` 或对应基础件域。
- 每个主域必须有明确 owner，外部入口只能投递事件、写命令槽或读取 snapshot。
- Vector Blackboard 只保存事实、摘要、命令槽、版本和 CRC；每个字段必须有唯一 writer。
- Active Object 拥有生命周期、事件队列、执行预算和对外 API。
- Function Block 执行 ECC 状态迁移、资源规则和错误归因，不做长时间阻塞。
- Resource Arbiter 统一管理 Flash、SD、USB、PIO、DMA、LCD、隔离链路等互斥资源。
- DPLL/VDC、Calibration、LoopEngine、Trigger、Storage、Communication 等域必须通过反射内存和事件机制交互，不互相直接篡改状态。

## 任务状态

| 状态 | 含义 |
|---|---|
| `待开始` | 已确认需要做，但还未建立代码或文档入口。 |
| `进行中` | 已有骨架或局部实现，但还未满足 HAOFV owner / vector / AO / FB 闭环。 |
| `待验证` | 已完成主要实现，等待构建、烧录、板端或离线验证闭环。 |
| `完成` | 已满足架构边界并完成必要验证。 |
| `暂停` | 暂不推进，但不是技术阻塞。 |

## 当前架构审查结论

### HAOFV-MAINT-20260813-001 - app.c 中心化状态需要拆出主域 owner

- 状态：进行中
- 问题：
  - `ConfigGate`、配置 ACK、SystemModeTable、ResourceArbiterTable 和 FaultCodeTable 已迁入 `components/system_manager/`。
  - `LoopEngine` ready、service_count 和状态查询已迁入 `components/loop_engine/`。
  - `Calibration` ready、state、service_count、link_count、delay_count 和 active_crc32 已迁入 `components/calibration_manager/`。
  - `VDC/DPLL` ready、service_count、lock/state 和 seq 计数已迁入 `components/vdc_dpll_manager/`。
  - `application/src/app.c` 已不再集中保存 LoopEngine、Calibration、VDC、DPLL 和 UI/Diagnostics 调度状态。
  - `system_manager` 目前是快照 owner 第一阶段，还不是完整 `SystemAO / SystemVector / SafetyFB`。
  - `loop_engine`、`calibration`、`vdc_dpll` 和其他 service 当前主要是 ready、计数器和时间戳维护，尚未形成独立 AO/FB/Vector。
- 影响：
  - 不符合 HAOFV 中“功能域 owner 拥有生命周期、事件队列、状态事实”的约束。
  - 后续 SCPI、UI、反射内存和 core1 实时侧容易再次直接耦合到 `app.c`。
- 待办：
  - [x] 建立 `SystemManager` 第一阶段组件，迁出 ConfigGate 快照、配置 ACK 和系统只读表。
  - [x] 建立 `LoopEngine` 第一阶段组件，迁出循环引擎状态计数和状态查询。
  - [x] 建立 `CalibrationManager` 第一阶段组件，迁出校准状态计数、link/delay 摘要和状态查询。
  - [x] 建立 `VdcDpllManager` 第一阶段组件，迁出 VDC/DPLL 状态计数和状态查询。
  - [ ] 将 `SystemManager` 升级/收敛为 `SystemAO / SystemVector / SafetyFB`，接管系统模式、故障锁存、恢复策略和资源策略。
  - [ ] 将 `components/loop_engine/` 升级为 `LoopEngineAO / LoopEngineFB / LoopVector`，承接业务配置、序列展开和运行计划。
  - [ ] 将 `components/calibration_manager/` 升级为 `CalibrationAO / CalibrationFB / CalibrationVector`，承接 link/parameter CRUD、短事务测量和校准版本质量。
  - [ ] 将 `components/vdc_dpll_manager/` 升级为 `VdcSyncAO / SyncDpllFB / VdcVector`，承接 timestamp、offset/rate、锁定状态、质量判据和版本管理。
  - [x] `app.c` 第一阶段只保留启动编排、ready 状态和少量 RTOS task 入口转发。
  - [ ] 继续把剩余 task 入口转发收敛到各域 AO 或 runtime task registry。
- 关联文件：
  - `application/src/app.c`
  - `application/inc/app.h`

### HAOFV-MAINT-20260813-002 - RTOS 任务壳已建立，但 AO 边界尚未闭合

- 状态：进行中
- 问题：
  - `application/src/main.c` 已保留为初始化、失败兜底和进入运行的入口骨架。
  - `application/src/app_tasks.c` 承接 `system/usb_device/scpi/refmem_sync/loop_engine/vdc_sync/calibration/dpll/config_gate/ota/storage/ui` 任务入口、栈大小和优先级。
  - `application/src/app_runtime.c` 只保留 bring-up、core1 启动、kernel init/start 和故障兜底。
  - 部分任务仍调用少量 `app_*_service()` 转发，还不是独立域 AO 的 service。
- 影响：
  - RTOS 调度维度已经展开，但 HAOFV 的 owner、事件队列、执行预算还未落在各域。
- 待办：
  - [x] 将 RTOS task 创建、裸机循环和 core1 启动细节从 `main.c` 迁入 `app_runtime`。
  - [x] 将 RTOS task entry、栈大小和优先级从 `app_runtime.c` 迁入 `app_tasks.c`。
  - [x] 当前分支固化为 RTOS + 双核 AMP，不再维护裸机单核运行路径。
  - [x] 将 `task_loop_engine` 第一阶段接到 `components/loop_engine/` owner service。
  - [ ] 将 `task_loop_engine` 升级接到 `loop_engine_ao_service()`。
  - [x] 将 `task_calibration` 第一阶段接到 `components/calibration_manager/` owner service。
  - [ ] 将 `task_calibration` 升级接到 `calibration_ao_service()`。
  - [x] 将 `task_vdc_sync` / `task_dpll` 第一阶段接到 `components/vdc_dpll_manager/` owner service。
  - [ ] 将 `task_vdc_sync` / `task_dpll` 升级接到同步基础件 AO/FB owner。
  - [ ] 为每个 AO 增加 queue depth、service budget、watermark、last error snapshot。
  - [ ] 板端验证时记录 RTOS stack/heap 水位和 core1 heartbeat。
- 关联文件：
  - `application/src/main.c`
  - `application/src/app_runtime.c`
  - `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`

### HAOFV-MAINT-20260813-003 - TRIGger 产品域需要从 SCPI 静态状态迁出

- 状态：进行中
- 问题：
  - `middleware/scpi_port/src/scpi_trigger_commands.c` 中 `s_product_trigger_mode` 由 SCPI 文件持有。
  - `TRIGger:MODE` 仍会直接向 `sync_trigger` 投递 reset。
  - `TRIGger:STARt/STOP/PAUSe/CONTinue/ABORt` 当前只是返回成功，没有进入产品业务状态机。
- 影响：
  - 违反“SCPI 只表达意图，状态由 owner/vector 管理”的原则。
  - 产品业务动作域和底层实时验证域仍存在混用风险。
- 待办：
  - [ ] 建立 `TriggerProductAO` 或由 `LoopEngineAO` 承接产品运行控制。
  - [ ] `TRIGger:MODE` 改为写命令槽或投递 System/Trigger owner 事件。
  - [ ] `TRIGger:STARt/STOP/PAUSe/CONTinue/ABORt` 接入产品运行状态机。
  - [ ] `READ:TRIGger:STATe?` 从产品运行 vector 读取真实状态。
  - [ ] 明确 `TRIGger` 与 `REALtime` 的双向隔离边界。
- 关联文件：
  - `middleware/scpi_port/src/scpi_trigger_commands.c`
  - `middleware/scpi_port/inc/scpi_trigger_commands.h`
  - `components/sync_trigger/`

### HAOFV-MAINT-20260813-004 - CONFIG/LoopEngine 还没有真实配置数据面

- 状态：进行中
- 问题：
  - `CONFigure:TRIGger`、`CONFigure:ANGLe:*`、`CONFigure:SEQuence`、`CONFigure:SWITCH#` 多数查询仍返回固定样例。
  - 暂未形成 staged/active 配置、CRC、ACK/NACK、门禁和运行计划展开。
- 影响：
  - 上位机看到的产品指令表和固件内部状态没有闭环。
  - “扫描角度 + 断点角度 + 每角度测试序列”的业务模型还未落入 LoopEngine。
- 待办：
  - [ ] 定义 `LoopPlanVector`：trigger 参数、angle sweep、angle breakpoint、sequence、switch map。
  - [ ] `CONFigure:*` 写 staged 配置，`READ:*` 读 staged/active snapshot。
  - [ ] 增加 plan CRC、version、active id、last check result。
  - [ ] 增加 `CONFigure:SEQuence` 状态展开和 SP8T/SP2T 映射校验。
  - [ ] 运行前由 ConfigGate 校验 build/hardware/config/calibration/sync 版本一致性。
- 关联文件：
  - `middleware/scpi_port/src/scpi_config_commands.c`
  - `components/distributed_config/`
  - `components/distributed_refmem/`

### HAOFV-MAINT-20260813-005 - Calibration 域需要建立 link/parameter/version/quality 闭环

- 状态：进行中
- 问题：
  - 校准查询目前返回固定 `SMA/A0/OUT1/A1/IN1` 和默认 CRC。
  - `CalibrationManager` 第一阶段已承接状态计数、link/delay 摘要和 active_crc32。
  - 还没有 link 增删改查、delay 参数表、短事务测量、保存/激活/回滚和质量状态。
- 影响：
  - VDC/DPLL 和预测分发无法获得可信的 T2/link delay 基础事实。
- 待办：
  - [x] 建立 `CalibrationManager` 第一阶段状态 owner。
  - [ ] 将 `CalibrationManager` 升级为 `CalibrationAO / CalibrationFB / CalibrationVector`。
  - [ ] 实现 link CRUD：任意 `node,out_port -> node,in_port` 链路。
  - [ ] 实现 calibration parameter CRUD：链路 delay、质量、来源、时间戳、版本。
  - [ ] `CALibration:STARt <src_node>,<src_port>,<dst_node>,<dst_port>` 执行指定链路短测量。
  - [ ] `READ:CALibration:LINK?` / `READ:CALibration:PARameter?` 输出真实表。
  - [ ] `CALibration:SAVE/LOAD/ACTivate/ROLLback/CLEAr` 接入 Storage 和 ConfigGate。
- 关联文件：
  - `middleware/scpi_port/src/scpi_calibration_commands.c`
  - `middleware/scpi_port/inc/scpi_calibration_commands.h`
  - `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md`

### HAOFV-MAINT-20260813-006 - SYNC/VDC/DPLL 基础件还未真正实现

- 状态：进行中
- 问题：
  - `READ:SYNC:*` 当前大多返回固定字段。
  - `SYSTem:SYNC:VDC:STATus?` 和 `SYSTem:SYNC:VDC:DPLL:STATus?` 已改为读取 `components/vdc_dpll_manager/` 第一阶段状态 owner。
  - 当前尚未开始实现 VDC/DPLL 算法，`vdc_dpll_manager` 只维护 ready、状态计数器、时间戳和 seq。
  - 尚未实现 timestamp sample、VDC offset/rate、lock/holdover/relock、DPLL 环路质量和版本管理。
- 影响：
  - 四板环路 VDC、DC 时钟同步、T2 计算和预测分发尚无法产品化闭环。
- 待办：
  - [x] 建立 `VdcDpllManager` 第一阶段状态 owner。
  - [ ] 将 `VdcDpllManager` 升级为 `VdcSyncAO / SyncDpllFB / VdcVector`。
  - [ ] 定义 timestamp sample 最小传输格式和批量预定义表。
  - [ ] 实现 DPLL 虚拟环路滤波器参数、状态、锁定质量和调试接口。
  - [ ] 将 calibration link delay 作为 T2 参数来源。
  - [ ] 将 VDC 稳态输出提供给 Trigger/LoopEngine 的预测分发。
  - [ ] 增加同步版本、质量、健康、holdover/relock 统计。
- 关联文件：
  - `middleware/scpi_port/src/scpi_sync_commands.c`
  - `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md`

### HAOFV-MAINT-20260813-007 - Distributed RefMem 需要从本地表骨架升级为主数据面

- 状态：进行中
- 问题：
  - 64 KB slot 表已经建立，但目前主要维护 header、node heartbeat 和 core 保护状态。
  - 尚未实现跨板同步、slot 唯一 writer、命令槽、ACK/NACK、CRC/seqlock 和 slot 提交流程。
- 影响：
  - 当前 SCPI/域状态仍容易绕过反射内存直接调用函数。
- 待办：
  - [ ] 定义每个 slot 的 owner 和 writer 规则。
  - [ ] 增加 slot seqlock、CRC、stale、version 和 dirty 标记。
  - [ ] 建立 command slot / ack slot / fault slot 的通用协议。
  - [ ] 将 SCPI 产品配置统一写入 RefMem command/staging 区。
  - [ ] 增加跨节点同步、冲突检测和超时策略。
- 关联文件：
  - `components/distributed_refmem/`
  - `components/distributed_config/`

### HAOFV-MAINT-20260813-008 - Storage 需要区分产品异步 job 与维护同步操作

- 状态：进行中
- 问题：
  - Storage 已有 manager/vector/job 模型，但部分 SCPI 维护命令仍同步等待或直接执行 raw/format/probe。
- 影响：
  - 维护接口可以保留强操作，但产品接口不应被阻塞式 job 等待污染。
- 待办：
  - [ ] 明确 `MMEMory/STORage` 产品接口只提交 job 并查询结果。
  - [ ] raw clear/read/format 标为维护或工厂权限。
  - [ ] 所有长操作返回 job id，完成状态通过 `...:JOB?` 查询。
  - [ ] Storage manager 逐步补齐 `StorageAO / StorageFB / StorageVector` 命名和边界。
- 关联文件：
  - `middleware/scpi_port/src/scpi_storage_commands.c`
  - `components/storage_manager/`

### HAOFV-MAINT-20260813-009 - Communication 域需要从命令树走向独立通信 owner

- 状态：待开始
- 问题：
  - `COMMunication:BISS:*` 已成为 canonical 命令树，但底层仍大量复用 `sync_trigger` vector/events。
  - `COMMunication:SERial:UART#:*` 目前为 `PENDING_BACKEND` 占位，RS485 尚未接入。
- 影响：
  - BiSS-C、UART、RS485 后续可能继续挤入 Trigger 或 SCPI callback。
- 待办：
  - [ ] 建立 `CommunicationAO / CommunicationVector`。
  - [ ] 将 BiSS-C 从 Trigger 业务动作域剥离为通信基础件。
  - [ ] UART/RS485 接入统一端口生命周期、收发计数、错误统计和资源仲裁。
  - [ ] 与 Realtime/Trigger 的关系改为事实发布和事件通知，不直接共用业务状态。
- 关联文件：
  - `middleware/scpi_port/src/scpi_communication_biss_commands.c`
  - `middleware/scpi_port/src/scpi_communication_uart_commands.c`
  - `components/sync_trigger/`

### HAOFV-MAINT-20260813-010 - Measure/T2/Diagnostics/UI 需要补 owner 和闭环

- 状态：待开始
- 问题：
  - Measure 目前主要是查询占位，T2 明细、timestamp 质量、报告导出还未形成独立数据链。
  - Diagnostics 有基础状态，但尚未形成产品级 fault/log/trace/snapshot/report 闭环。
  - UI 调度与 `status_ui` 渲染模块已合并到 `components/ui_manager/`。
  - UI 当前直接读多个模块 snapshot，尚未形成完整 `UiAO / UiVector`。
- 影响：
  - 分布式系统后续测试报告、故障恢复、现场维护和上位机闭环会缺少统一事实来源。
- 待办：
  - [ ] 建立 `MeasureAO / TimestampService / T2Vector`。
  - [ ] T2 数据归入系统测量/同步事实，不放在业务域。
  - [ ] Diagnostics 接入 fault clear、fault evidence、trace、snapshot、log page。
  - [x] 建立 `UiManager` 第一阶段组件，合并按键/刷新调度和 `status_ui` 渲染模块路径。
  - [ ] UI 只读公开 snapshot，动作入口走 System/Domain event。
  - [ ] 增加报告导出的分页读取和批次索引。
- 关联文件：
  - `middleware/scpi_port/src/scpi_measure_commands.c`
  - `components/diagnostics/`
  - `components/ui_manager/`

## 架构风险处置总表

来源：`docs/arch/HAOFV_ARCHITECTURE_RISK_EVALUATION.md`。

本章节把风险登记表中的 S0/S1/S2/S3 风险转成可执行待办。风险文档负责记录评审事实和严重度，本章节负责维护处置入口、优先级和落地去向。

### HAOFV-RISK-TODO-20260813-001 - S0 双核 Flash/XIP 安全硬约束

- 来源风险：`HAOFV-RISK-20260813-004`
- 状态：待开始
- 优先级：P0 / S0
- 问题：
  - Flash erase/program 会阻塞 XIP；core0 写 Flash 时 core1 若仍从 XIP 执行实时路径，可能 hard fault 或总线超时。
  - 当前只有 park/lockout 方向，尚未形成强制资源锁、core1 状态和验证门禁。
- 待办：
  - [x] 在 `HAOFV_ARCHITECTURE.md` 增加双核 Flash/XIP 顶层硬约束。
  - [x] 在 `RTOS_HAOFV_ARCHITECTURE.md` 写入 Flash/XIP 双核保护框架、状态机、接口契约、可观测字段和验证门禁。
  - [ ] 定义 `FlashWriteOwner` 框架入口，所有 OTA/metadata/config 落盘先进入该 owner，不直接调用底层 erase/program。
  - [ ] Resource Arbiter 增加 `SYS_RESOURCE_FLASH_BUS` 或等价资源锁。
  - [ ] 定义 `Core1LockoutGate` request/ack/state/sequence/timeout/last_result 共享结构。
  - [ ] Flash 临界区进入前强制 core1 park/lockout ACK；超时进入 FAULT。
  - [ ] core1 增加 `WAIT_FOR_FLASH` / `PARKED_FOR_FLASH` 可观测状态。
  - [ ] RuntimeProtectionTable 对齐 lockout support/online/requested/acknowledged/park_state/last_result/elapsed_us。
  - [ ] 验证 OTA/metadata/program 路径中 core1 heartbeat、park ack 和恢复状态。
- 落地去向：
  - `docs/arch/RTOS_HAOFV_TODO.md` P2。
  - `components/resource_arbiter/`
  - `drivers/mcu/flash/`
  - `application/src/app_runtime.c`

### HAOFV-RISK-TODO-20260813-002 - S1 跨核反射内存契约升格

- 来源风险：`HAOFV-RISK-20260813-003`
- 状态：进行中
- 优先级：P1 / S1
- 问题：
  - core0/core1 之间的 owner 矩阵、doorbell、mailbox、ACK、timeout、reset 和内存屏障尚未成为 HAOFV 顶层契约。
  - 反射内存 slot 尚未强制 seqlock/双缓冲/CRC，存在半新半旧读取风险。
- 待办：
  - [x] 在 `HAOFV_ARCHITECTURE.md` 增加跨核 owner 矩阵。
  - [ ] 定义 core0-WO/core1-RO 与 core1-WO/core0-RO 字段清单。
  - [ ] 定义 `core_ipc_contract`：mailbox、doorbell、ACK/NACK、timeout、reset。
  - [ ] 共享字段强制使用 `__atomic` 或 DMB 屏障。
  - [ ] 反射内存快照采用 seqlock 或双缓冲，并带 version/CRC/stale。
- 落地去向：
  - `docs/arch/RTOS_HAOFV_TODO.md` P1/P2。
  - `components/distributed_refmem/`
  - `components/sync_trigger/`

### HAOFV-RISK-TODO-20260813-003 - S1 TriggerFB ECC 规模和默认规则化

- 来源风险：`HAOFV-RISK-20260813-001`
- 状态：进行中
- 优先级：P2 / S1
- 问题：
  - `HAOFV_ARCHITECTURE.md` 中 TriggerFB ECC 规模数字失真；实际 ECC 表约 190 条。
  - 大量 `SET_*` 配置直通规则被逐状态穷举，新增字段时容易遗漏。
- 待办：
  - [x] 修正 `HAOFV_ARCHITECTURE.md` 中 TriggerFB 状态数、事件数、ECC 规则数。
  - [ ] 增加 ECC 表静态检查脚本，检测重复 `(state,event)`、不可达条目和未覆盖事件。
  - [ ] 引入 `SET_*` 默认规则，IDLE/CONFIGURED 态统一走配置处理函数。
  - [ ] 将 BiSS-C 配置从 TriggerFB 巨 switch 拆出到 CommunicationFB 或 CommunicationAO。
  - [ ] 增加 TriggerFB ECC 规则数量阈值或增长审查门禁。
- 落地去向：
  - `docs/arch/RTOS_HAOFV_TODO.md` P6。
  - `components/sync_trigger/src/trigger_fb.c`
  - `components/sync_trigger/inc/trigger_vector.h`
  - `components/sync_trigger/` 与未来 `components/communication/`

### HAOFV-RISK-TODO-20260813-004 - S1 Vector 字段契约和时间回绕规则

- 来源风险：`HAOFV-RISK-20260813-002`、`HAOFV-RISK-20260813-009`
- 状态：进行中
- 优先级：P2 / S1
- 问题：
  - Vector 只在原则层声明唯一 writer，缺逐字段 writer/value domain/lifecycle/snapshot-needed 表。
  - `timestamp_ms` 为 `uint32_t`，缺强制回绕安全比较规则。
- 待办：
  - [ ] 将 `trigger_vector_t` 的 BiSS-C 配置字段拆成 `biss_cfg` 子结构或字段块。
  - [ ] 为每个 Vector 字段块增加 `writer / value domain / lifecycle / snapshot-needed` 注释。
  - [x] 在架构文档中补逐字段或逐字段块写权限表。
  - [x] 规定时间差计算统一使用 `int32_t diff = (int32_t)(t1 - t0)`。
  - [ ] 评估是否增加 `epoch_seconds` / `time_epoch` 扩展字段。
- 落地去向：
  - `docs/arch/RTOS_HAOFV_TODO.md` P1/P5/P6。
  - `components/sync_trigger/inc/trigger_vector.h`
  - `components/distributed_refmem/`

### HAOFV-RISK-TODO-20260813-005 - S1 Bootloader Metadata Failsafe

- 来源风险：`HAOFV-RISK-20260813-008`
- 状态：进行中
- 优先级：P2 / S1
- 问题：
  - Metadata 双副本损坏后的强制恢复路径尚未写入 Bootloader 启动策略。
  - BOOTSEL/UF2、Scratch、SD `/factory/` 仍偏规划表述。
- 待办：
  - [x] 在 Bootloader 启动策略中规定 metadata 双副本无效的 failsafe 状态机。
  - [ ] 定义 USB MSD / BOOTSEL / SD factory package 的恢复优先级。
  - [ ] 定义双副本无效错误码、LED/UI/SCPI 可观测状态。
  - [ ] 增加 metadata 双损坏注入测试。
- 落地去向：
  - `docs/arch/RTOS_HAOFV_TODO.md` P7。
  - `bootloader/`
  - `components/ota_manager/`
  - `middleware/portable_ota_port/`

### HAOFV-RISK-TODO-20260813-006 - S2 Resource/OTA/Budget 产品运行门禁

- 来源风险：`HAOFV-RISK-20260813-005`、`HAOFV-RISK-20260813-006`、`HAOFV-RISK-20260813-007`
- 状态：进行中
- 优先级：P3 / S2
- 问题：
  - Resource Arbiter 缺资源优先级、等待队列和超时升级策略。
  - OTA 允许矩阵仍分散在业务域判断，缺 SystemManager 集中定义。
  - 调度预算只有表格，缺 overrun handler 和 RTOS 时间片语义。
- 待办：
  - [ ] 定义资源优先级，首版建议 Flash > SD > LCD。
  - [ ] 增加资源等待 FIFO、timeout、retryable/blocking 和 fault escalation 规则。
  - [ ] 在 SystemManager 定义 OTA 允许矩阵：system mode × resource × trigger state。
  - [ ] 定义 `OTA_BUSY` / `RESOURCE_ACQUIRE_TIMEOUT` / `BUDGET_OVERRUN` 错误码。
  - [x] 定义预算 overrun handler：记录 Diagnostics 事件并主动 yield。
  - [x] 明确 RTOS 预算语义为“连续运行时间片”，不等价于绝对截止时间。
- 落地去向：
  - `docs/arch/RTOS_HAOFV_TODO.md` P3/P7。
  - `components/resource_arbiter/`
  - `components/system_manager/`
  - `components/diagnostics/`
  - `components/ota_manager/`

### HAOFV-RISK-TODO-20260813-007 - S3 FB 非阻塞硬规则

- 来源风险：`HAOFV-RISK-20260813-010`
- 状态：进行中
- 优先级：P4 / S3
- 问题：
  - “不得长期阻塞”仍偏软约束，缺标准状态机写法和代码审计项。
- 待办：
  - [x] 在 `HAOFV_ARCHITECTURE.md` 中明确 FB action 必须立即返回。
  - [x] 耗时动作必须返回 `FB_RESULT_BUSY` 且 `next_state=self`，由下一次 tick 推进。
  - [ ] 增加禁止在 FB/AO 快路径中等待 flash/storage/job complete 的审计规则。
  - [ ] 在新 FB 模板中加入非阻塞示例。
- 落地去向：
  - `docs/arch/HAOFV_ARCHITECTURE.md`
  - `docs/arch/HAOFV_IMPLEMENTATION_PLAYBOOK.md`
  - `components/*_manager/` 与未来 AO/FB 模板

## 推荐推进顺序

1. 先处理 S0：双核 Flash/XIP park/lockout 硬约束。
2. 再处理 S1 跨核契约、反射内存 seqlock/CRC/stale 和 Vector 字段契约。
3. 修正 TriggerFB ECC 事实数字，并开始默认规则化/CommunicationFB 拆分。
4. 建立 `SystemAO + SystemVector + command slot`，统一产品指令入口、OTA 允许矩阵和资源门禁。
5. 拆出 `LoopEngineAO`，承接业务配置、序列展开、角度扫描和断点角度。
6. 建立 `CalibrationAO` 与 `VdcSyncAO / SyncDpllFB`，形成 link delay、DC 时钟同步和 T2 事实来源。
7. 将 `Distributed RefMem` 升级为跨域主数据面，补齐 slot writer、CRC、seqlock、ACK/NACK。
8. 收敛 `CommunicationAO`、`MeasureAO`、`DiagnosticsAO`、`UiAO`，完成产品化维护闭环。

## 验证要求

- 每次代码框架修改后，必须构建并记录 build id。
- 涉及 RTOS/双核/反射内存的修改，必须记录 `SYSTem:RTOS:STATus?`、`SYSTem:CORE:*`、`SYSTem:REFMEM:*`。
- 涉及产品指令入口的修改，必须执行 SCPI dry-run 和产品 smoke。
- 涉及 CAL/SYNC/DPLL 的修改，必须记录对应 service_count、状态、版本、CRC、quality/fault。
- 涉及硬实时路径的修改，必须单步烧录验证，避免问题累积。
