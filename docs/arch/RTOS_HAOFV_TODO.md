# 基于 HAOFV 的 RTOS 待办事项

Status: Active
Domain: RTOS
Canonical: `docs/arch/RTOS_HAOFV_TODO.md`
Related: `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`, `docs/arch/HAOFV_MAINTENANCE_TODO.md`, `docs/interface/SCPI_TASK_PROGRESS.md`
Last updated: 2026-09-05

本文档只维护 RTOS + 双核 AMP 在 HAOFV 下的实施待办。已经完成的构建、烧录、
板端 smoke、工具输出和水位记录放在 `RTOS_HAOFV_TASK_PROGRESS.md`。

当前产品化开发分支：`feature/rtos-multicore-haofv`。该分支只维护 RTOS + 双核
AMP 主线，不再新增裸机单核兼容工作；裸机/单核仅作为历史 bring-up 参考和故障
定位时的对照路径。

## 阶段性长期任务：DPLL-LONG-001

RTOS 侧按 DPLL 基础件到闭环的顺序提供执行容器和门禁，不把 DPLL 算法、诊断或
波形分析塞入 Core1 硬实时路径。执行顺序与 TDMA 主域保持一致：

`P0 静态资源 → P1 TDMA 基线 → P2 active 校准矩阵 → P3 硬件 timestamp → P4 eligible gate → P5 最小 SyncDpllFB → P6 VdcVector/NO5 → P7 故障注入与长稳`。

当前发布状态：`ACTIVE`。P0 构建、链接余量和 host pytest 已通过；板端 heap/stack 和
NO1–NO4 四板 TDMA 基线是 TDMA 前置门禁，NO5 只读观测仅是 P6 DPLL/VDC 门禁；两者
分别记录，不得把缺少 NO5 的四板 TDMA 结果误标为 DPLL 完整结论。

RTOS 执行约束：

- `task_dpll`/`task_vdc_sync` 只服务对应 owner；`SyncDpllFB` 是 offset/rate/lock/DCO 的唯一 writer。
- Core1 只做固定 TDMA phase、buffer 选择和 FIFO 装载；PIO/DMA 负责确定性传输及硬件 latch；Core0 承担诊断、SD/SVG 和离线分析。
- active topology/path-delay/offset matrix 必须由 Calibration snapshot 原子加载并带 generation/freshness/CRC；无效时 fail-closed。
- DPLL/NO5/诊断负载只能使用已冻结的 TDMA payload 和拍级预算；超限在编译或 DeploymentGate 拒绝，不能运行时借用 guard。
- 每个阶段完成必须有 build、pytest、同包多板异步 OTA、板端只读查询和可回溯证据；失败时保留证据并回退到上一个已验证阶段。

## P0 - 任务边界固化

- [x] 将 `task_io_frontend` 拆为 `task_usb_device` 和 `task_scpi`。
- [x] 将 `app_comm_service()` 拆为 `app_usb_device_service()` 和 `app_scpi_service()`。
- [x] 让 `SYSTem:RTOS:STATus?` 显示拆分后的任务水位。
- [x] 建立 `task_refmem_sync` 空壳，按 64 KB 表维护本地 header、node slot 和 heartbeat。
- [x] 建立 `task_loop_engine` 空壳，只计数和响应状态查询。
- [x] 建立 `task_vdc_sync` 空壳，只维护 lock 状态和统计计数。
- [x] 建立 `task_dpll` 空壳，只维护 disabled/ready 状态。
- [x] 建立 `task_calibration` 空壳，支持 link/delay staging、snapshot 和计数器。
- [x] 将 `main.c` 中的 RTOS task 创建、裸机循环和 core1 启动细节迁入 `app_runtime`，`main.c` 只保留初始化、失败兜底和进入运行。
- [x] 将 ConfigGate、配置 ACK、SystemModeTable、ResourceArbiterTable 和 FaultCodeTable 只读快照迁入 `system_manager`。
- [x] 将 LoopEngine ready/service_count/first_service_ms/last_service_ms 状态迁入 `components/loop_engine/`，`task_loop_engine` 直接服务该 owner。
- [x] 将 Calibration ready/state/service_count/link_count/delay_count/active_crc32 状态迁入 `components/calibration_manager/`，`task_calibration` 直接服务该 owner。
- [x] 将 VDC ready/lock_state/service_count/sync_seq 与 DPLL ready/state/service_count/update_seq 状态迁入 `components/vdc_dpll_manager/`，`task_vdc_sync` 和 `task_dpll` 直接服务该 owner。
- [x] 将 UI 按键/刷新调度迁入 `components/ui_manager/`，内部状态界面渲染模块命名为 `status_ui`。
- [x] 将 Diagnostics housekeeping 节拍迁入 `components/diagnostics/`。
- [x] 将 RTOS task entry、栈大小和优先级迁入 `application/src/app_tasks.c`。
- [x] 收窄 `application/inc/app.h`，SCPI 读取直接依赖对应 owner 组件快照。
- [x] 当前分支默认构建和运行路径固化为 RTOS + 双核 AMP。
- [ ] 将空壳逐步替换为真正 AO service，`app.c` 只保留启动编排。

## P0-RAM - 内部 SRAM 优化

目标：先把可证明不影响实时性能的 SRAM 浪费回收出来，再处理测试缓存、staging buffer 和任务栈。
当前开发阶段采用 `ram_budget_check.py --profile debug`。debug 目标由代码常量
`DEFAULT_DEBUG_MIN_FREE_BYTES` 定义；短差必须记录原因、map/BSS 快照和原始数据，但不得拒绝
状态机继续到下一状态、超时或本轮结束。产品/release profile 仍保持严格拒绝。任务栈与 heap
水位继续由 `rtos_watermark_capture.py` 留证，SMA、TDMA、OTA 和 core1 heartbeat 不得退化。

- [x] 建立 RAM 优化分层方案：P0a 链接布局浪费，P0b 重复静态表，P0c 测试缓存，P0d staging pool，P0e task stack/heap。
- [x] P0a：将 `s_sync_io_capture_dma_ring` 放入固定 `.sync_io_dma_ring` section，避免 32 KB 对齐对象在普通 `.bss` 中制造约 25 KB 空洞。
- [x] P0b-1：删除 `sync_io_model_sched` 的 4096 项 us->ns 兼容临时表，legacy us 接口直接编码 DMA words。
- [x] P0b-2：已用 DMA/PIO 运行态推导替代 `completion_ns[4096]`；保留 `completed_pulses` 查询语义，同时回收约 32 KB。
- [x] P0c：移除 VDC self-test 的 4096 项 pulse cache，改为直接调用周期脉冲调度接口；保留 4096 脉冲维护能力，不影响正式 RUN。
- [x] P0c-2：将诊断日志 RAM 队列从 2 KB/4 KB 收缩为 1 KB/2 KB；保留 high-watermark/drop 观测，只影响突发日志缓存深度。
- [x] P0d-a：RefMem staging image 已提供 `refmem_table_registry_begin_staging_write()` /
  `refmem_table_registry_end_staging_write()` 单 owner、非阻塞 lease；内联镜像和 SD package
  直接使用 registry-owned staging buffer，失败释放并清空，成功提交后保留。
- [ ] P0d-b：继续统一 Storage write buffer、OTA/package staging 的生命周期；事务持有期间不得
  覆盖，busy 必须 fail-closed。
- [x] P0e：按板端水位重算 task stack；`configTOTAL_HEAP_SIZE` 与 watchdog/UI/SCPI/OTA 栈均使用当前代码符号值。analyzer 与 waveform 保持有界缓冲，RefMem 三份事务镜像和 TDMA 实时对象未压缩；四板新镜像 Watermark 已保存。
- [x] P0f：增加 RAM 门禁脚本并解析 map；release profile 不达标时失败，debug profile 记录差额与原始 BSS 后强制继续。heap、任务栈水位由板端 `SYSTem:RTOS:STATus?` 独立留证。
- [x] P0g：本轮已完成 build、四板 Watermark、4096 OTA、P3/TRN、TDMA 短帧和 SD/SVG 分析；debug 拒绝原因和 forced-continue 原始证据均已保留。

### P0-RAM 门限精算（2026-09-05）

本轮以 debug profile 推进，目标值由 `DEFAULT_DEBUG_MIN_FREE_BYTES` 给出。debug 短差属于可恢复诊断门禁：记录后有界继续；release profile 仍由 `DEFAULT_MIN_FREE_BYTES` 严格拒绝。链接余量与 FreeRTOS heap/任务栈水位分别留证。

静态精算以 `out/build/ram116-ota4096/DHRT100.elf.map` 为当前快照（非事实源）；它记录 `link_free_bytes=18288 B` 和 debug 目标短差 `14480 B`。更低 heap 的历史候选曾导致启动风险，因此当前不再只凭 map 数字继续下调；代码事实以 `configTOTAL_HEAP_SIZE`、`SYNC_IO_LOGIC_ANALYZER_MAX_RECORDS` 和 `VDC_DPLL_MANAGER_WAVEFORM_SEGMENT_MAX_RECORDS` 为准。

稳定性/可行性边界：更低 heap 候选曾导致启动风险，当前使用 `configTOTAL_HEAP_SIZE` 的代码值。四板新镜像已证明任务栈 gate、OTA 后目标 build、TDMA SHORT 和 SD 波形分析可继续运行；heap 产品门限与 release 静态门限仍是产品化工作，不阻塞 debug 状态机迁移。analyzer 和 waveform 仍保留有界捕获及异步双缓冲，未改变 TDMA SHORT、PIO/DMA phase、RefMem layout 或 TDMA realtime owner。

## P1 - RefMem 内部主域 / 反射内存主数据面

详细待办以 `docs/refmem/REFMEM_DOMAIN_TODO.md` 为准；本节只保留 RTOS + 双核 AMP 视角下必须纳入发布门禁和任务拆分的事项。

- [ ] 根据风险 `HAOFV-RISK-20260813-003/009`，将跨核 owner 矩阵和时间回绕规则升格为反射内存基础约束。
- [x] 在 HAOFV/RTOS 架构中明确 Distributed RefMem 不是完整 IEC 61499 分布式运行时，而是吸收其 application / instance / event connection / data connection / deployment / diagnostics 优点的静态分布式应用模型。
- [x] 将 RefMem 明确为 HAOFV 内部主域，并建立 `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`、`REFMEM_DOMAIN_TODO.md`、`REFMEM_TASK_PROGRESS.md`。
- [x] 文档定义 `DistributedApplicationMap`，覆盖 A0-A7 八个通用节点，以及加载到节点上的 board/pulse_distributor/link_switcher/instrument_controller/gateway/model_vna/model_turntable/model_dut/test_agent 等 role/persona/instance；允许无冲突时同一通用节点同时载入多个实例。
- [x] 文档定义 `DistributedFbInstanceTable`，覆盖每节点 AO/FB instance、domain、版本、enable 条件、资源/IO claim、时间预算和健康状态。
- [x] 文档定义 `DistributedEventLinkTable`，覆盖 START/STOP/FIRE_LOAD/DONE/FAULT/ACK/NACK 的 source、destination、transport、timeout、ACK 策略和 evidence。
- [x] 文档定义 `DistributedDataLinkTable`，覆盖 slot 字段 writer/reader、类型、单位、值域、生命周期、snapshot 策略和 stale 窗口。
- [x] 文档定义 `DistributedDeploymentGate`，把 layout、node、instance、resource、IO、writer、event、data、config、cal/sync quality 纳入 RUN 门禁。
- [x] 文档定义 `DistributedConnectionQualityTable`，覆盖 seq、CRC、stale、late、drop、timeout、last_error、p99/p999 和 evidence index。
- [ ] 将通用节点、实例加载、FB instance、事件连接、数据连接、部署门禁和连接质量表落到 RefMem Domain 代码组件，并接入 RUN gate。
- [x] 文档冻结 `DistributedVectorTable` 64 KB layout、slot offset、slot size、layout version 规则。
- [x] 文档定义 Header/Directory、directory CRC、slot directory、layout 兼容和 slot map 校验。
- [x] 文档定义 Version Bundle，包含 epoch、run_id、config/calibration/loop/action/sync/sequence/permission/storage/build/hw profile 版本。
- [x] 文档定义 slot owner 写权限规则，禁止非 owner 直接写其他节点 slot 或 active fact。
- [x] 文档定义 slot 级 snapshot 契约，查询只读快照，不临时触发现场 IO。
- [x] 文档定义 `DIRECT_ATOMIC`、`SEQLOCK`、`DOUBLE_BUFFER`、`EVIDENCE_REF` 四类快照策略。
- [x] 文档定义共享字段必须使用 `__atomic`、DMB 屏障或等价机制；跨核快照必须带 sequence/version。
- [x] 文档定义时间差一律使用回绕安全写法：`int32_t diff = (int32_t)(t1 - t0)`。
- [x] 文档定义 `epoch_id + tick32` 和 `dc_time64_ns` 语义，并要求增加 `epoch_seconds` / `time_epoch` 等价字段。
- [ ] 将 `distributed_refmem.h/.c` 拆出 `refmem_vector_table.h/.c`，并按文档冻结 offset/size/static assert。
- [ ] 为 DistributedVectorTable 实现 directory CRC 和 slot directory 校验。
- [ ] 为全部 slot 增加统一 guard 或等价兼容结构。
- [ ] 实现 slot owner 写权限检查，禁止非 owner 写其他节点 slot。
- [ ] 实现 seqlock 或双缓冲，避免字段半新半旧。
- [ ] 在代码中补齐 `epoch_id/run_id/epoch_seconds/dc_time64_ns` 等时间与运行上下文字段。
- [ ] 实现命令槽原子 Take/Clear，执行动作保持在临界区外。
- [ ] 将 core1 `trigger_status_ring` 合并到本节点 TriggerSlot 摘要。
- [x] 定义 CoreVectorOwnerTable 和 RuntimeProtectionTable。
- [x] 定义 SystemModeTable、ResourceArbiterTable 和 FaultCodeTable 只读查询接口。
- [ ] 统一所有共享表项的 `table_seq / slot_seq / owner / crc / stale / flags` 字段。
- [ ] 增加 `OK/STALE/MISSING/INVALID/FAULT` 节点新鲜度状态和 stale window。
- [ ] 将节点新鲜度纳入 `SYNC:CHECk`、`READ:SYNC:*?` 和 TRIG RUN 门禁。

## S0 - Flash/Core1 Lockout 发布前硬门禁

本节优先级高于 RefMem 基础件继续扩表。目标是先证明任何 Flash erase/program 都不会在 core1 仍可能从 XIP 取指时发生。

- [x] 将 `drv_flash` 中的静态 lockout 变量抽成 `drv_flash_lockout` 状态机，形成可测试、可观测的 Core1LockoutGate。
- [x] App 启动 core1 前初始化 lockout gate；bootloader 单核目标保持 `supported=false`。
- [x] 多核 App 中，core1 未 online 时 flash begin 拒绝写入；bootloader 单核写入不受该门禁阻塞。
- [x] core1 lockout poll 入口使用 RAM-resident 定义；Pico App target 显式启用 `wfe/sev/nop` 等待/唤醒指令，host/ARM compile fallback 不启用。
- [x] Flash erase/program 进入临界区前必须通过 `drv_flash_lockout_begin()`，未获得 ACK 时不调用底层 `flash_range_erase/program`。
- [x] 增加 host 单元测试 `test_drv_flash_lockout.c`，覆盖 request/ACK/PARKED/release、offline 拒绝和 no-ACK 故障注入。
- [x] 板端 HIL 验证：执行 OTA/metadata flash 写路径时查询 `SYSTem:PROTection:STATus?`，确认 online、park_state、request_seq、ack_seq、release_seq、last_result 和 timeout 证据。
- [ ] 将 no-ACK 故障注入接到受控维护接口或 HIL build，验证板端 flash job 不执行并返回 NACK/fault。

## P2 - 跨核通信与实时核保护

- [x] 根据风险 `HAOFV-RISK-20260813-004`，将 Flash/XIP 双核冲突列为 P2 首要硬约束。
- [x] 在 `RTOS_HAOFV_ARCHITECTURE.md` 定义 Flash/XIP 双核保护框架、状态机、接口契约、可观测字段和验证门禁。
- [ ] 定义 `FlashWriteOwner` 框架入口，禁止 OtaAO/metadata/config 落盘直接调用底层 erase/program。
- [ ] Resource Arbiter 增加 `FLASH_BUS` 资源、owner、timeout、conflict holder 和 fault escalation。
- [x] 定义 `Core1LockoutGate` request/ack/state/sequence/timeout/last_result 共享结构。
- [x] 将 RuntimeProtectionTable 字段对齐到 `flash_lockout_supported/online/requested/acknowledged/park_state/last_result/elapsed_us`。
- [ ] 抽象 `trigger_command_queue`，替代直接暴露 TriggerAO 内部队列。
- [ ] 抽象 `trigger_status_ring`，core1 只写轻量事件，core0 负责格式化和落盘。
- [ ] 增加跨核 doorbell 作为唤醒信号，业务 payload 仍走队列。
- [ ] 为 TriggerVector snapshot 增加 sequence/version。
- [ ] 抽象 `core_ipc_contract`，定义 mailbox、doorbell、ack、timeout 和 reset 语义。
- [x] 实现 core1 park/lockout 握手和超时升级流程。
- [x] Flash erase/program 前必须申请 Flash bus 资源锁并等待 core1 park/lockout ACK。
- [ ] core1 增加 `WAIT_FOR_FLASH` / `PARKED_FOR_FLASH` 或等价可观测状态。
- [ ] Flash 临界区超时或 core1 未 ACK 时进入 FAULT，禁止继续 erase/program。
- [x] 增加 core1 不 ACK 故障注入验证，确认 Flash job 不执行并返回 NACK/fault。
- [ ] 审计 `storage_manager_trace_event()`，禁止 core1 直接调用。
- [ ] 为 core1 增加 stack/heartbeat/last_event 诊断字段。
- [ ] 拆分 core0/core1/shared 三类内存区域。
- [ ] 将 core1 入口、flash lockout poll 和实时快路径迁移到 RAM-resident section。
- [ ] 评估 core1 独立 RAM vector table / VTOR。
- [ ] 增加 linker map 断言，校验实时入口、lockout poll、status ring 和私有状态位置。

## P3 - SystemAO / ConfigGate / LoopEngine

- [x] 建立 `SystemManager` 第一阶段组件，先迁出 ConfigGate 快照、配置 ACK 和系统只读表。
- [x] 建立 `LoopEngine` 第一阶段组件，先迁出状态计数和只读快照。
- [ ] 将 `SystemManager` 升级/收敛为 `SystemAO / SystemVector / SafetyFB`，接管系统模式、故障锁存、恢复策略和资源策略。
- [ ] 把 SystemModeTable 和 ResourceArbiterTable 接入真实模式切换。
- [ ] 根据风险 `HAOFV-RISK-20260813-005/006/007`，定义资源优先级、等待队列、OTA 允许矩阵和预算 overrun handler。
- [ ] Resource priority 首版建议：Flash > SD > LCD；所有资源等待必须带 timeout 和升级策略。
- [ ] SystemManager 定义 OTA 允许矩阵：system mode × resource × trigger state -> allow/busy/fault。
- [ ] 定义 `OTA_BUSY`、`RESOURCE_ACQUIRE_TIMEOUT`、`BUDGET_OVERRUN` 错误码和 UI/SCPI 提示。
- [ ] RTOS 预算语义明确为“连续运行时间片”；超预算向 Diagnostics 记录事件并主动 yield。
- [x] 文档定义 RefMem `AckCommandSlot`，并明确现有 `SYSTem:CONFigure:ACK? / NACK?` 是配置门禁视图，后续通用 `SYSTem:COMMand:*` 读取同一底层事实。
- [ ] 将现有配置 ACK 代码迁移或映射到 RefMem AckCommandSlot snapshot。
- [ ] 评估并建立通用 `SYSTem:COMMand:ACK? / NACK?`，保持 `SYSTem:CONFigure:*` 兼容配置视图。
- [ ] 增加 `task_gateway_a3`，接收上位机配置、START/STOP 和数据查询。
- [ ] 将 `components/loop_engine/` 升级为 `LoopEngineAO / LoopEngineFB / LoopVector`。
- [ ] 实现 `CONFigure:TRIGger` 自动展开状态表。
- [ ] 实现 `CONFigure:ANGLe:SWEEp`、`CONFigure:ANGLe:PULSe`、`READ:ANGLe:POSition?`、
  `CONFigure:ANGLe:BREAkpoint` 的 staging/active 字段。
- [ ] 实现 `CONFigure:SEQuence`、`READ:SEQuence:MAP?`、`READ:SEQuence:CHECk?`、
  `CONFigure:SEQuence:ACTive`、`READ:SEQuence:ACTive?` 的序列库、CRC、拒绝原因和 ACK。
- [ ] 实现 `CONFigure:SWITch# / READ:SWITch#?`，RUN 中序列引擎占用时返回 busy。
- [ ] 实现断点保存和恢复策略。
- [ ] 冻结 `TEST/SERVICE/DEBUG/FACTORY` 权限矩阵和 RUN 态策略表。

## P4 - Calibration / SYNC / RJ45_SYNC_RING

- [x] 建立 `CalibrationManager` 第一阶段组件，先迁出状态计数、link/delay 摘要和只读快照。
- [ ] 将 `components/calibration_manager/` 升级为 `CalibrationAO / CalibrationFB / CalibrationVector`。
- [ ] 实现 `CONFigure:CALibration:LINK:ADD/UPDate/DELete/CLEAr` 和 link key 去重。
- [ ] 实现 `CALibration:STARt <src_node>,<src_port>,<dst_node>,<dst_port>` 短事务。
- [ ] 实现 `READ:CALibration:STATe? / RESult? / LINK? / PARameter? / VERSion? / QUALity?` 固定字段。
- [ ] 实现 `CALibration:SAVE/LOAD/ACTivate/ROLLback/CLEAr` 的资源仲裁、ACK/NACK 和 storage package。
- [ ] 实现 GPIO26/27 `ring_rx_tx` PIO 原型。
- [ ] 定义 SYNC、`FIRE_LOAD`、DONE、MEAS_DONE、FAULT、`REFMEM_DELTA`、`REFMEM_EPOCH` 帧格式和 CRC。
- [ ] 实现 slot delta 合并、slot_version、stale_count 和 CRC 检查。
- [ ] 实现 A3 本地镜像查询，slot stale 时返回 stale。
- [ ] 实现 ACK/NACK/busy_flags 位图同步。
- [ ] 所有 CAL/SYNC 写动作统一接入分布式 ACK 语义。
- [ ] 帧和证据全部携带 epoch/run_id 或可回溯上下文。

## P5 - VDC / SYNC DPLL / Angle DPLL

详细待办以 `docs/vdc/VDC_DOMAIN_TODO.md` 为准；本节只保留 RTOS + 双核 AMP 视角下必须纳入发布门禁和任务拆分的事项。

- [x] 建立 `VdcDpllManager` 第一阶段组件，先迁出 VDC/DPLL 状态计数和只读快照。
- [x] 将 VDC 明确为 HAOFV 内部主域，并建立 `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`、`VDC_DOMAIN_TODO.md`、`VDC_TASK_PROGRESS.md`。
- [ ] 将 `components/vdc_dpll_manager/` 升级为 `VdcSyncAO / SyncDpllFB / VdcVector`。
- [ ] 实现 `CONFigure:SYNC:CALibration/RING/VDC:DPLL/GATE/LIMit` 的 staging 配置和拒绝原因。
- [ ] 实现 `SYNC:CHECk/STARt/STOP/RELock/HOLDover`。
- [ ] 实现 SYNC DPLL 的 offset/rate 更新、LOCK/HOLDOVER/RELOCK。
- [ ] 增加虚拟环路滤波器调试接口，但默认不要求上位机调节。
- [ ] 实现 NODE/RJ45 link delay 引入 VDC 计算。
- [ ] 统计 `e_vdc`、crc_count、seq_error、node freshness。
- [ ] 建立 `AngleDpllFB`，接入转台 Compare Out。
- [ ] 实现角度预测 DPLL 输出 `T_fire_base`，并明确不参与 VDC offset/rate 收敛。
- [ ] 区分 `e_vdc`、`e_pll` 和 `e_act`，统计口径不得混用。
- [ ] 定义 HOLDOVER/RELOCK 策略：失锁、STALE、CRC 连错、RELOCK 后是否重新 ARM。

## P6 - 本地预约触发与 T2 闭环

- [ ] 根据风险 `HAOFV-RISK-20260813-001/002/009/010`，先修正 TriggerFB/TriggerVector 架构事实和字段契约。
- [ ] 修正 `HAOFV_ARCHITECTURE.md` 中 TriggerFB ECC 表规模、状态数、事件数和 TriggerVector 字段数。
- [ ] 增加 ECC 表静态检查脚本，检测重复 `(state,event)`、不可达条目和未覆盖事件。
- [ ] 引入 `SET_*` 默认配置规则，减少 TriggerFB 直通 ECC 穷举。
- [ ] 将 BiSS-C 配置字段从 `trigger_vector_t` 顶层拆为 `biss_cfg` 字段块。
- [ ] 为 TriggerVector 每个字段块补 `writer / value domain / lifecycle / snapshot-needed` 注释。
- [ ] FB action 必须立即返回；耗时动作使用 `FB_RESULT_BUSY + next_state=self` 分步推进。
- [ ] 实现 `FIRE_LOAD` 到 core1 `local_fire` 装载。
- [ ] 实现 `delta_ticks/mask/pulse_width/polarity` 小载荷。
- [ ] 实现 late 判断，late frame 禁止补救触发。
- [ ] 实现 GPIO20..23 反序输入捕获和通道映射。
- [ ] 实现 T2/READY 捕获扩展到 LOCKED 虚拟 DC 时间戳。
- [ ] 实现 DEVICE/T2 校准计算动作补偿。
- [ ] 实现 `e_act = T2_i - T_fire_base - delay_i` 统计。
- [ ] 增加 `SYSTem:T2:DATA?` 分页读取。
- [ ] 冻结 TriggerFB 产品 ECC 状态转移表。
- [ ] 接入 TriggerActionTable，定义 role/mode/action 的装载时序和禁止条件。
- [ ] 增加 SMA_OUTx -> SMA_INx 回环自动验证脚本。

## P7 - 产品发布门禁

- [ ] 根据风险 `HAOFV-RISK-20260813-008`，定义 Bootloader metadata 双副本无效 failsafe。
- [ ] metadata 双副本无效时进入 USB MSD / BOOTSEL / SD factory package 恢复路径，禁止继续启动未知镜像。
- [ ] 增加 metadata 双损坏注入测试和恢复验证。
- [ ] 24h 四板长稳：core1 heartbeat 不停、heap/stack 水位稳定。
- [ ] 24h DistributedVectorTable：slot 不撕裂，stale/heartbeat/CRC 统计稳定。
- [ ] SD/OTA/UI/SCPI 并发压力下 late=0 或按规则进入 HOLDOVER/FAULT。
- [ ] 故障证据落盘：CRC、seq、late、READY timeout、watchdog reset。
- [ ] RUN 保存 epoch/run_id、四板 build/hw profile、配置 CRC、校准 CRC、T2/e_act/e_vdc/e_pll 统计。
- [ ] RUN 后报告闭环覆盖 run summary、log、trace、snapshot、T2、fault evidence。
- [ ] 验证上电、bootloader、看门狗、通信丢失和 FAULT 下的安全默认态。
- [x] release preset 明确 RTOS + 双核产品化门禁；单核/裸机仅保留 bring-up 路径。
- [ ] README、SCPI 命令文档、HIL 工具和生产测试流程同步更新。

## 验证要求

每一步代码修改必须执行：

```text
cmake build
flash UF2
board smoke
SYSTem:RTOS:STATus? 水位记录
SYSTem:CORE? core1 heartbeat
SYSTem:REFMEM:* table/slot 摘要
CAL/SYNC/DPLL 对应 service_count 和状态查询
SYSTem:ERRor? 错误队列确认
```

涉及 SCPI 命令树的修改，详细记录放在 `docs/interface/SCPI_TASK_PROGRESS.md`。
涉及 RTOS / 双核 / 反射内存的验证记录，追加到 `RTOS_HAOFV_TASK_PROGRESS.md`。
