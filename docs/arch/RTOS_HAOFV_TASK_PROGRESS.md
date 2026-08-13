# 基于 HAOFV 的 RTOS 任务进度追踪与回溯

Status: Active
Domain: RTOS
Canonical: `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`
Related: `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_TODO.md`, `docs/interface/SCPI_TASK_PROGRESS.md`
Last updated: 2026-08-13

本文档用于记录 DTC100 / RP2350_TRIG 工程中基于 HAOFV 的 RTOS + 双核 AMP、
分布式触发、模拟反射内存、任务拆分和板端烧录验证进度。每完成一个阶段，
都应追加任务记录，说明目标、完成内容、验证结果、剩余工作和下一步计划，
便于后续回溯任务边界、水位、core1 heartbeat、反射内存快照和 CAL/SYNC 骨架状态。

架构原则以 `docs/arch/RTOS_HAOFV_ARCHITECTURE.md` 为准，待办事项以
`docs/arch/RTOS_HAOFV_TODO.md` 为准。SCPI 模块拆分和命令表迁移记录放在
`docs/interface/SCPI_TASK_PROGRESS.md`。

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

### RTOS-DIST-TASK-20260813-008 - RefMem 64 KB 表契约冻结

- 状态：框架完成，代码未改
- 日期：2026-08-13
- 任务目标：
  - 根据 RefMem 主域 P3，冻结 RTOS + 双核 AMP 下 `DistributedVectorTable` 的 64 KB offset/size、slot owner、snapshot、version bundle 和时间回绕契约。
  - 保持 RTOS 架构文档与 RefMem canonical 一致。
- 完成内容：
  - `RTOS_HAOFV_ARCHITECTURE.md` 将 RefMem 表格同步为固定 offset/size，表尾固定 `0x10000`。
  - 明确详细 Header/Directory、slot guard、owner、snapshot、Version Bundle 和时间回绕契约以 `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md` 为准。
  - `RTOS_HAOFV_TODO.md` 将 P1 中的 P3 文档冻结项标记完成，并拆出 `refmem_vector_table.h/.c`、directory CRC、统一 guard、owner 写权限、seqlock/双缓冲和运行上下文字段的代码待办。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，保留 7 个既有文件命名 warning。
  - 本轮为文档契约冻结，未修改固件代码，未执行构建、烧录或板端 `SYSTem:REFMEM:*` 查询。
- 还需完成：
  - 按 RefMem canonical 进入代码落地，拆出 `refmem_vector_table.h/.c`。
  - 完成 directory CRC、slot directory 校验、slot owner 写权限、seqlock/双缓冲和运行上下文字段。
- 关联文件：
  - `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
  - `docs/arch/RTOS_HAOFV_TODO.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 继续 RefMem P4 Command / ACK / NACK 契约，随后进入代码拆分。

### RTOS-DIST-TASK-20260813-007 - Flash/XIP 双核保护框架补齐

- 状态：框架完成，代码未改
- 日期：2026-08-13
- 任务目标：
  - 先处理 S0 架构风险 `HAOFV-RISK-20260813-004`，不修改代码，只把 Flash/XIP 双核保护框架写清楚。
  - 将 core0 Flash 写入、core1 park/lockout、Resource Arbiter、RuntimeProtectionTable 和验证门禁串成统一实施契约。
- 完成内容：
  - 在 `RTOS_HAOFV_ARCHITECTURE.md` 新增 `Flash/XIP 双核保护框架` 小节。
  - 定义参与组件：`FlashWriteOwner`、`Resource Arbiter`、`Core1LockoutGate`、`core1_realtime`、`RuntimeProtectionTable` 和 `DiagnosticsAO`。
  - 定义 Flash 写入状态机：`FLASH_IDLE`、`REQUEST_LOCKOUT`、`WAIT_CORE1_ACK`、`PARKED_FOR_FLASH`、`FLASH_CRITICAL`、`RELEASE_LOCKOUT`、`FAULT_TIMEOUT`。
  - 定义接口契约：Flash 写请求、`FLASH_BUS` 资源申请、core1 lockout request/ack、runtime protection snapshot 和 flash write result。
  - 定义可观测字段和最小验证门禁，覆盖 lockout supported/online/requested/acknowledged、park_state、last_result 和 elapsed_us。
  - 在 `RTOS_HAOFV_TODO.md` P2 中拆出后续实现待办。
  - 在 `HAOFV_MAINTENANCE_TODO.md` 的 S0 风险项中计入本次框架补齐。
- 验证结果：
  - 本轮为文档框架修改，未执行代码构建、烧录或板端 SCPI。
- 还需完成：
  - 定义并实现 `FlashWriteOwner` 框架入口。
  - Resource Arbiter 增加 `FLASH_BUS` 资源、owner、timeout、conflict holder 和 fault escalation。
  - 定义 `Core1LockoutGate` 共享结构并对齐 RuntimeProtectionTable。
  - 增加 core1 不 ACK 故障注入验证，确认 Flash job 不执行并返回 NACK/fault。
- 关联文件：
  - `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
  - `docs/arch/RTOS_HAOFV_TODO.md`
  - `docs/arch/HAOFV_MAINTENANCE_TODO.md`
- 下一步：
  - 继续处理 S0 的实现前框架收敛：先完成 RuntimeProtectionTable/RefMem/SCPI 字段契约，再进入代码实现。

### RTOS-DIST-TASK-20260813-006 - App 组合根与 RTOS Task Registry 收窄

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 按 APP 剩余四项拆分继续收窄 `application/src/app.c`。
  - 将 Diagnostics housekeeping、RTOS task registry、SCPI snapshot wrapper 和裸机单核路径从 APP 主体中移出或收敛。
- 完成内容：
  - `components/diagnostics/` 新增 `diagnostics_housekeeping_init()` 和 `diagnostics_housekeeping_service()`，承接日志 flush 和健康心跳节拍。
  - 新增 `application/inc/app_tasks.h` 和 `application/src/app_tasks.c`，集中维护 RTOS task entry、栈大小、优先级和 task 创建表。
  - `application/src/app_runtime.c` 收窄为 bring-up、core1 启动、kernel init/start 和故障兜底。
  - `middleware/scpi_port` 中 Loop/Calibration/SYNC/System snapshot 查询改为直接读取对应 owner 组件快照，不再经由 `app.h` typedef/wrapper。
  - `application/inc/app.h` 删除状态 typedef、状态查询 wrapper、裸机 `run_once` 和 UI/loop/cal/vdc/dpll 转发接口。
  - 当前分支通过编译期检查和 CMake 默认值固化为 `PROJECT_USE_FREERTOS=ON` + `PROJECT_USE_MULTICORE=ON`。
- 验证结果：
  - build-validation 重新配置为 RTOS + 双核 AMP 后通过，build id：`20260813074910`。
  - build-rtos-multicore-smoke 通过，build id：`20260813074910`。
  - `python tools/docs_check/docs_check.py` 通过，保留 7 个既有文件命名 warning。
  - SCPI USB namespace check 在两个构建中通过。
  - 本轮未执行烧录、`SYSTem:CORE?`、`SYSTem:RTOS:STATus?` 和 LCD/按键板端 smoke。
- 还需完成：
  - 将剩余 `app_*_service()` 转发逐步替换为各域 AO 或 task registry 直接调用。
  - 为 `app_tasks.c` 增加 task 表快照、水位导出和任务预算字段。
  - 板端验证 core1 heartbeat、RTOS stack/heap 水位和基础 SCPI 查询。
- 关联文件：
  - `application/src/app.c`
  - `application/inc/app.h`
  - `application/src/app_runtime.c`
  - `application/src/app_tasks.c`
  - `components/diagnostics/`
  - `middleware/scpi_port/src/scpi_*_commands.c`
- 下一步：
  - 进入反射内存主数据面：冻结 64 KB DistributedVectorTable layout、slot owner 和 seqlock/CRC/stale 规则。

### RTOS-DIST-TASK-20260813-005 - UiManager 组件合并

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将 `app.c` 中的 UI 按键去抖、刷新节拍和 dirty 调度迁入独立 UI owner。
  - 将旧 UI 渲染组件合并到 `components/ui_manager/`，避免两个并列 UI 管理组件。
- 完成内容：
  - 新增 `components/ui_manager/inc/ui_manager.h` 和 `components/ui_manager/src/ui_manager.c`，承接按键、刷新周期和渲染调度。
  - 将旧渲染文件迁入 `components/ui_manager/` 后重命名为 `status_ui.c/h`，作为 UI 组件内部状态界面渲染模块。
  - CMake 删除独立 UI 渲染组件路径，统一使用 `components/ui_manager/`。
  - `application/src/app.c` 删除 UI 本地状态，`app_ui_service()` 保留为兼容 wrapper。
  - RTOS `task_ui` 直接调用 `ui_manager_service()`。
- 验证结果：
  - build-validation build id：`20260813073155`。
  - build-rtos-multicore-smoke build id：`20260813073155`。
  - `rg` 搜索确认代码与架构文档中不再残留旧 UI 渲染组件代码符号和组件路径。
  - `cmake --build build-validation` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - SCPI USB namespace check 在构建中通过。
  - `python tools/docs_check/docs_check.py` 通过，保留 7 个既有文件命名 warning。
  - 本轮未执行板端 LCD 刷新和按键切页 smoke。
- 还需完成：
  - 将 `UiManager` 升级为 `UiAO / UiVector`。
  - UI 只读公开 snapshot，动作入口走 System/Domain event。
  - 补充板端 LCD 刷新和按键切页 smoke 记录。
- 关联文件：
  - `components/ui_manager/`
  - `application/src/app.c`
  - `application/src/app_runtime.c`
  - `components/ui_manager/src/status_ui.c`
- 下一步：
  - 继续清理 `app.c` 中的 Diagnostics heartbeat wrapper，或开始为 UI 增加只读 snapshot 边界。

### RTOS-DIST-TASK-20260813-004 - VdcDpllManager 第一阶段组件化

- 状态：进行中
- 日期：2026-08-13
- 任务目标：
  - 将 `app.c` 中的 VDC 与 DPLL 状态计数迁入独立同步基础件组件。
  - 让 `task_vdc_sync` 和 `task_dpll` 直接服务 VDC/DPLL owner，减少 RTOS task 对 `app_*` wrapper 的依赖。
- 完成内容：
  - 新增 `components/vdc_dpll_manager/`，承接 VDC ready、lock_state、service_count、first_service_ms、last_service_ms、sync_seq 快照。
  - 同一组件承接 DPLL ready、state、service_count、first_service_ms、last_service_ms、update_seq 快照。
  - `application/inc/app.h` 使用 `vdc_dpll_manager_*_status_t` 兼容 `app_vdc_sync_status_t` 和 `app_dpll_status_t`。
  - `app_vdc_sync_service()`、`app_dpll_service()` 和对应 get_status 保留为兼容 wrapper。
  - RTOS `task_vdc_sync` 和 `task_dpll` 直接调用 `vdc_dpll_manager_*_service()`。
- 验证结果：
  - build-validation build id：`20260813071627`。
  - build-rtos-multicore-smoke build id：`20260813071620`。
  - `cmake --build build-validation` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - `python tools/docs_check/docs_check.py` 通过，保留 7 条既有文件命名 warning。
  - SCPI USB namespace check 在两个构建中均通过。
  - 本轮未执行烧录和板端 `SYSTem:SYNC:VDC:*?` 查询，后续继续小步拆分时需要补板端 smoke 记录。
- 还需完成：
  - 当前算法尚未开始实现；本步骤只迁出状态 owner。
  - 将 `VdcDpllManager` 升级为 `VdcSyncAO / SyncDpllFB / VdcVector`。
  - 实现 timestamp sample、offset/rate、环路滤波、LOCK/HOLDOVER/RELOCK、质量判据和版本管理。
  - 区分 SYNC DPLL 与 Angle DPLL，避免 VDC offset/rate 与 `T_fire_base` 预测混用。
- 关联文件：
  - `components/vdc_dpll_manager/`
  - `application/src/app.c`
  - `application/src/app_runtime.c`
  - `application/inc/app.h`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
- 下一步：
  - 继续清理 `app.c` 中的 UI/diag/storage wrappers，或开始把 `VdcDpllManager` 的状态字段映射到 Distributed RefMem VDC/DPLL slot。

### RTOS-DIST-TASK-20260813-003 - CalibrationManager 第一阶段组件化

- 状态：进行中
- 日期：2026-08-13
- 任务目标：
  - 将 `app.c` 中的 Calibration 状态计数、link/delay 摘要和 active_crc32 迁入独立功能域组件。
  - 让 `task_calibration` 直接服务 Calibration owner，减少 RTOS task 对 `app_*` wrapper 的依赖。
- 完成内容：
  - 新增 `components/calibration_manager/`，承接 ready、state、service_count、first_service_ms、last_service_ms、command_seq、link_count、delay_count、active_crc32 和 last_error 快照。
  - `application/inc/app.h` 使用 `calibration_manager_status_t` 兼容 `app_calibration_status_t`。
  - `app_calibration_service()` 和 `app_calibration_get_status()` 保留为兼容 wrapper。
  - RTOS `task_calibration` 直接调用 `calibration_manager_set_ready()` 和 `calibration_manager_service()`。
- 验证结果：
  - build-validation build id：`20260813070532`。
  - build-rtos-multicore-smoke build id：`20260813070526`。
  - `cmake --build build-validation` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - `python tools/docs_check/docs_check.py` 通过，保留 7 条既有文件命名 warning。
  - SCPI USB namespace check 在两个构建中均通过。
  - 本轮未执行烧录和板端 `READ:CALibration:*?` 查询，后续继续小步拆分时需要补板端 smoke 记录。
- 还需完成：
  - 将 Calibration 从状态计数器升级为 `CalibrationAO / CalibrationFB / CalibrationVector`。
  - 接入 link/parameter CRUD、短事务测量、保存/加载/激活/回滚和质量版本管理。
  - 将校准结果写入 Distributed RefMem 校准 slot，并提供给 VDC/DPLL 作为 T2/link delay 事实来源。
- 关联文件：
  - `components/calibration_manager/`
  - `application/src/app.c`
  - `application/src/app_runtime.c`
  - `application/inc/app.h`
  - `middleware/scpi_port/src/scpi_calibration_commands.c`
- 下一步：
  - 继续拆 `VdcSync` / `DPLL` 状态 owner，或开始给 Calibration 增加 link/parameter staged 数据面。

### RTOS-DIST-TASK-20260813-002 - LoopEngine 第一阶段组件化

- 状态：进行中
- 日期：2026-08-13
- 任务目标：
  - 将 `app.c` 中的 LoopEngine 状态计数和状态查询迁入独立功能域组件。
  - 让 `task_loop_engine` 直接服务 LoopEngine owner，减少 RTOS task 对 `app_*` wrapper 的依赖。
- 完成内容：
  - 新增 `components/loop_engine/`，承接 ready、service_count、first_service_ms、last_service_ms 快照。
  - `application/inc/app.h` 使用 `loop_engine_status_t` 兼容 `app_loop_engine_status_t`。
  - `app_loop_engine_service()` 和 `app_loop_engine_get_status()` 保留为兼容 wrapper。
  - RTOS `task_loop_engine` 直接调用 `loop_engine_set_ready()` 和 `loop_engine_service()`。
- 验证结果：
  - build-validation build id：`20260813065348`。
  - build-rtos-multicore-smoke build id：`20260813065348`。
  - `cmake --build build-validation` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过。
  - `python tools/docs_check/docs_check.py` 通过，保留 7 条既有文件命名 warning。
  - SCPI USB namespace check 在两个构建中均通过。
  - 本轮未执行烧录和板端 `SYSTem:LOOP:STATus?` 查询，后续继续小步拆分时需要补板端 smoke 记录。
- 还需完成：
  - 将 LoopEngine 从状态计数器升级为 `LoopEngineAO / LoopEngineFB / LoopVector`。
  - 接入 `CONFigure:TRIGger`、`CONFigure:ANGLe:*`、`CONFigure:SEQuence` 和 `CONFigure:SWITch#` 的 staged/active 数据面。
  - 增加 plan CRC、active id、last check result、拒绝原因和 ACK/NACK。
- 关联文件：
  - `components/loop_engine/`
  - `application/src/app.c`
  - `application/src/app_runtime.c`
  - `application/inc/app.h`
  - `middleware/scpi_port/src/scpi_loop_engine_commands.c`
- 下一步：
  - 继续拆 `CalibrationAO` 或开始给 LoopEngine 增加 staged/active 配置骨架。

### RTOS-DIST-TASK-20260813-001 - SystemManager 与 AppRuntime 第一阶段拆分

- 状态：进行中
- 日期：2026-08-13
- 任务目标：
  - 按 HAOFV 功能域思路拆分 `app.c`，先迁出 System/ConfigGate 事实源。
  - 将 `main.c` 中的运行容器逻辑拆出，避免入口文件继续承载 RTOS task 创建和 core1 启动细节。
- 完成内容：
  - 新增 `components/system_manager/`，承接 ConfigGate 状态、配置 ACK、SystemModeTable、ResourceArbiterTable 和 FaultCodeTable 快照。
  - `app.c` 删除本地 system/resource/fault 静态表和 CRC helper，仅保留兼容 wrapper。
  - `application/inc/app.h` 使用 `system_manager_*` 类型别名保持现有 SCPI 调用方兼容。
  - 新增 `application/src/app_runtime.c` 和 `application/inc/app_runtime.h`，承接 bring-up、RTOS task 创建、裸机循环和 core1 realtime 启动实现。
  - `application/src/main.c` 保留一般嵌入式入口职责：初始化、失败兜底和进入运行，不承载 RTOS task 表或功能域细节。
- 验证结果：
  - build id：`20260813063506`。
  - `cmake --build build-validation` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 RTOS + multicore smoke UF2 和 OTA 包。
  - SCPI USB namespace check 在两个构建中均通过。
  - 本轮未执行烧录和板端 `SYSTem:*` 查询，后续继续小步拆分时需要补板端 smoke 记录。
- 还需完成：
  - 将 `task_system` 继续接入真正 `SystemAO / SystemVector / SafetyFB`，而不是长期停留在 `system_manager` snapshot manager。
  - 继续迁出 `loop_engine`、`calibration`、`vdc_sync`、`dpll` 状态，建立各自 AO/FB/Vector。
  - 将 RTOS task 配置进一步从 `app_runtime.c` 拆到域注册表或 runtime task table。
- 关联文件：
  - `application/src/app.c`
  - `application/src/app_runtime.c`
  - `application/src/main.c`
  - `application/inc/app.h`
  - `application/inc/app_runtime.h`
  - `components/system_manager/`
- 下一步：
  - 拆 `LoopEngineAO` 或建立 `SystemAO` 命令槽，逐步替换 `app_*_service()` 空壳。

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
  - `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
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
  - 后续继续拆 SCPI 命令模块，详见 `docs/interface/SCPI_TASK_PROGRESS.md`。
- 关联文件：
  - `application/src/app.c`
  - `middleware/scpi_port/src/scpi_port.c`
- 下一步：
  - 建立 `task_loop_engine` 和 `task_refmem_sync` 骨架。
