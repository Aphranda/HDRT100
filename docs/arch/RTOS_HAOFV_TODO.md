# 基于 HAOFV 的 RTOS 待办事项

Status: Active
Domain: RTOS
Canonical: `docs/arch/RTOS_HAOFV_TODO.md`
Related: `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`, `docs/arch/HAOFV_MAINTENANCE_TODO.md`, `docs/interface/SCPI_TASK_PROGRESS.md`
Last updated: 2026-08-13

本文档只维护 RTOS + 双核 AMP 在 HAOFV 下的实施待办。已经完成的构建、烧录、
板端 smoke、工具输出和水位记录放在 `RTOS_HAOFV_TASK_PROGRESS.md`。

当前产品化开发分支：`feature/rtos-multicore-haofv`。该分支只维护 RTOS + 双核
AMP 主线，不再新增裸机单核兼容工作；裸机/单核仅作为历史 bring-up 参考和故障
定位时的对照路径。

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
- [ ] 将空壳逐步替换为真正 AO service，`app.c` 只保留启动编排和顶层调度。

## P1 - 反射内存主数据面

- [ ] 冻结 `distributed_vector_table.h`：64 KB layout、slot offset、slot size、layout version。
- [ ] 为 DistributedVectorTable 增加 directory CRC 和 slot directory 校验。
- [ ] 增加 epoch、run_id、config/calibration/loop/action/sync/sequence/permission/storage version。
- [ ] 实现 slot owner 写权限检查，禁止非 owner 写其他节点 slot。
- [ ] 实现 slot 级 snapshot API，查询只读快照，不临时触发现场 IO。
- [ ] 实现 seqlock 或双缓冲，避免字段半新半旧。
- [ ] 实现命令槽原子 Take/Clear，执行动作保持在临界区外。
- [ ] 将 core1 `trigger_status_ring` 合并到本节点 TriggerSlot 摘要。
- [x] 定义 CoreVectorOwnerTable 和 RuntimeProtectionTable。
- [x] 定义 SystemModeTable、ResourceArbiterTable 和 FaultCodeTable 只读查询接口。
- [ ] 统一所有共享表项的 `table_seq / slot_seq / owner / crc / stale / flags` 字段。
- [ ] 增加 `OK/STALE/MISSING/INVALID/FAULT` 节点新鲜度状态和 stale window。
- [ ] 将节点新鲜度纳入 `SYNC:CHECk`、`READ:SYNC:*?` 和 TRIG RUN 门禁。

## P2 - 跨核通信与实时核保护

- [ ] 抽象 `trigger_command_queue`，替代直接暴露 TriggerAO 内部队列。
- [ ] 抽象 `trigger_status_ring`，core1 只写轻量事件，core0 负责格式化和落盘。
- [ ] 增加跨核 doorbell 作为唤醒信号，业务 payload 仍走队列。
- [ ] 为 TriggerVector snapshot 增加 sequence/version。
- [ ] 抽象 `core_ipc_contract`，定义 mailbox、doorbell、ack、timeout 和 reset 语义。
- [ ] 实现 core1 park/lockout 握手和超时升级流程。
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
- [ ] 建立通用 `SYSTem:COMMand:ACK? / NACK?` 或收敛现有配置 ACK。
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

- [ ] 建立 `CalibrationAO / CalibrationFB / CalibrationVector`。
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

- [ ] 建立 `VdcSyncAO / SyncDpllFB / VdcVector`。
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

- [ ] 24h 四板长稳：core1 heartbeat 不停、heap/stack 水位稳定。
- [ ] 24h DistributedVectorTable：slot 不撕裂，stale/heartbeat/CRC 统计稳定。
- [ ] SD/OTA/UI/SCPI 并发压力下 late=0 或按规则进入 HOLDOVER/FAULT。
- [ ] 故障证据落盘：CRC、seq、late、READY timeout、watchdog reset。
- [ ] RUN 保存 epoch/run_id、四板 build/hw profile、配置 CRC、校准 CRC、T2/e_act/e_vdc/e_pll 统计。
- [ ] RUN 后报告闭环覆盖 run summary、log、trace、snapshot、T2、fault evidence。
- [ ] 验证上电、bootloader、看门狗、通信丢失和 FAULT 下的安全默认态。
- [ ] release preset 明确 RTOS + 双核产品化门禁；单核/裸机仅保留 bring-up 路径。
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
