# TDMA 基础件主域任务进度

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_TASK_PROGRESS.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`
Last updated: 2026-08-17

本文档记录 TDMA foundation 的阶段性任务进度、验证结果和后续动作。待办事项放在 `TDMA_DOMAIN_TODO.md`。

## 记录规则

每条任务记录使用以下格式：

```text
### TDMA-TASK-YYYYMMDD-NNN - 标题

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

TDMA Domain 当前目标是把公共 `components/tdma` 从 RefMem/VDC 之间的隐式基础件，升级为 HAOFV 下的显式 foundation domain：

```text
TdmaSchedulerAO
+ TdmaRuntimeFB
+ TdmaPayloadRegistry
+ TdmaRingRuntime
+ TdmaQualityVector
+ TransportAdapter
```

VDC 消费 observation evidence，RefMem 消费 data/completion evidence，二者都不拥有上/下行 TDMA 环路。

## 任务记录

### TDMA-TASK-20260817-007 - 唯一 runtime owner 与三级 traffic scheduler

- 状态：完成软件调度基础件、公共 owner、三级门禁、per-class completion token、23/23 host 门禁和 A/B 固件构建；尚未形成两板物理闭环 evidence。
- 日期：2026-08-17
- 任务目标：
  - 判断当前 TDMA 是否足以支撑 VDC 闭环，并先关闭软件调度与 owner 层的阻断项。
  - 冻结 `VDC_REALTIME > REFMEM_REALTIME > maintenance`，低优先级流不抢占实时短帧。
  - 消除 VDC/RefMem 各自维护 TDMA service 的双 runtime 现象。
- 完成内容：
  - 新增 `TdmaTrafficScheduler`，实现五类固定队列、周期预算、deadline、time-aware gate、maintenance gate、fault/backpressure/drop-oldest/drop-newest 和基础质量计数。
  - maintenance gate 默认关闭；配置、OTA、LOG 只有 TDMA owner 确认不同步或进入显式维护窗口后才可执行。
  - 新增 `tdma_runtime_owner.*`；VDC 注册 observation payload，RefMem 注册 data payload 并绑定物理 adapter，二者共享唯一 service，core1 每轮只推进一次。
  - 调度帧池从 128 KiB FreeRTOS heap 一次性申请，避免 32 个 long-frame 槽进入 `.bss`；申请失败直接阻止应用初始化。
  - 修正优先级重排下的序号语义：service 内部执行序号与 scheduler enqueue token 分离，并为五类流发布持久 completion token，避免 VDC 后入队先完成时误确认 RefMem intent。
  - result/error/timestamp/frame completion 按 traffic class 独立持久化；VDC 读取 VDC completion metadata，RefMem 读取 RefMem completion frame，后完成的低优先级流不再覆盖实时流证据。
  - `SYSTem:REFMEM:SYNC:TDMA:STATus?` 在旧字段末尾追加 scheduler 配置、enqueue/dispatch、队列水位、fault、last result/class 和五类 completion token。
  - 新增 service/scheduler 集成测试，证明配置先入队时仍按 VDC、RefMem、配置顺序执行；maintenance gate 关闭时配置帧保持排队。
- 验证结果：
  - `run_tdma_traffic_scheduler_tests.ps1` 通过。
  - `run_tdma_service_scheduler_tests.ps1` 通过。
  - `run_refmem_realtime_tdma_tests.ps1` 和 `run_vdc_domain_tests.ps1` 通过。
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，23/23 host scripts passed。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过 A/B 双目标；build id `20260817073126`，package CRC `0xEDF7B9AF`。
  - 本轮未烧录板卡；`simultaneous_feedback_loop_evidence` 仍为 0，不能据此进入产品 DPLL 闭环调参。
- 还需完成：
  - 实现同时 UP/DOWN adapter runtime、IDLE_BEACON 和硬件 RX/TX timestamp correlation。
  - 发布正式 `TdmaQualityVector` 并完成两板 HIL。
- 关联文件：
  - `components/tdma/inc/tdma_traffic_scheduler.h`
  - `components/tdma/src/tdma_traffic_scheduler.c`
  - `components/tdma/inc/tdma_runtime_owner.h`
  - `components/tdma/src/tdma_runtime_owner.c`
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `tests/unit/test_tdma_service_scheduler.c`
- 下一步：
  - 先完成 per-class completion metadata，再接 adapter/timestamp correlation；硬件证据通过后才进入 DPLL 闭环。

### TDMA-TASK-20260817-006 - TdmaRingRuntime 基础件拆分

- 状态：完成独立基础件、service 聚合接入、reason code、21/21 host 门禁和 A/B 固件构建；硬件 HIL 尚未执行。
- 日期：2026-08-17
- 任务目标：
  - 将 ring config、core1 runtime 推进和 ring snapshot 从 `tdma_service.c` 单体拆出。
  - 保持现有 `tdma_service_configure_ring_runtime()` 和维护 snapshot 字段兼容。
  - 冻结 adapter/scheduler/timestamp 后续共用的 ring reason code。
- 完成内容：
  - 新增 `tdma_ring_runtime.h/.c`，独立维护 config/result seqlock、config/reject/service/ring seq 和双向运行状态。
  - `tdma_service` 改为聚合 `tdma_ring_runtime_t`，配置、core1 service 和 snapshot 均通过基础件 API 完成。
  - 保留原有 ring snapshot 字段，并新增 `ring_config_reject_count`；维护查询只在末尾追加新字段。
  - 配置校验把 UP/DOWN group 缺失或相同明确映射为 `DIRECTION_CONFLICT`，其余 topology/flag/CRC 错误映射为 `BAD_CONFIG`。
  - 冻结 `EVIDENCE_MISSING`、`ADAPTER_MISSING`、`TIMESTAMP_MISSING`、`PAYLOAD_STARVATION`、`WINDOW_MISSED` 和 `RESOURCE_CONFLICT` 编号，供后续 owner 接入。
  - runtime service 仍强制保持 `simultaneous_feedback_loop_evidence=0`，未提供软件直接置位接口。
  - 新增独立 host 单测及测试脚本，并纳入全量 host 列表。
- 验证结果：
  - `run_tdma_ring_runtime_tests.ps1` 通过。
  - `run_refmem_realtime_tdma_tests.ps1` 通过，包含 config reject 和兼容 snapshot 断言。
  - `run_vdc_domain_tests.ps1` 通过。
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，21/21 host scripts passed。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过 A/B 双目标；build id `20260817063552`，package CRC `0xF15A8DA6`。
  - 本轮没有烧录板卡，不形成 UP/DOWN 同时物理运行或硬件闭环 evidence。
- 还需完成：
  - 建立五类固定队列、time-aware gate 和 reason-to-quality 映射。
  - 由 adapter/timestamp correlation 提供其余 reason 的真实发布源。
- 关联文件：
  - `components/tdma/inc/tdma_ring_runtime.h`
  - `components/tdma/src/tdma_ring_runtime.c`
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `tests/unit/test_tdma_ring_runtime.c`
  - `tools/tests/run_tdma_ring_runtime_tests.ps1`
- 下一步：
  - 全量闭环后进入五类固定队列和 time-aware gate 的数据结构设计。

### TDMA-TASK-20260817-005 - TdmaPayloadRegistry 基础件拆分

- 状态：完成独立基础件、service 聚合接入、维护快照、20/20 host 门禁和 A/B 固件构建；未执行硬件 HIL。
- 日期：2026-08-17
- 任务目标：
  - 将 payload binding、whitelist、capacity 和 admission 从 `tdma_service.c` 单体拆出。
  - 保持 VDC/RefMem 既有注册 API 和 RX-window 语义不变。
  - 为后续逐类队列、policing 和 DeploymentGate 提供可查询的 registry 水位。
- 完成内容：
  - 新增 `tdma_payload_registry.h/.c`，支持固定 8-entry registry、binding 注册/替换、active whitelist、short/long capacity 和 frame admission。
  - registry 使用独立 seqlock snapshot，发布 config seq、registration seq、used、admitted、reject、last result 和 last payload class。
  - `tdma_service_register_payload()` 和 submit admission 改为委托 registry；对外 API 名称和 producer 调用方式保持不变。
  - foundation profile 激活时同步配置 registry whitelist 和 capacity；如果现有 binding 与候选 profile 不兼容，runtime profile 配置拒绝。
  - 保留 RX window 的 `frame_size=0` 语义，它表示接收窗口尚无 payload，不按空 TX frame 拒绝。
  - RefMem 兼容 snapshot 和 `SYSTem:REFMEM:SYNC:TDMA:STATus?` 在末尾追加 registry 水位字段。
  - 新增独立 host 单测及测试脚本，并纳入全量门禁。
- 验证结果：
  - `run_tdma_payload_registry_tests.ps1` 通过。
  - `run_refmem_realtime_tdma_tests.ps1` 通过，包含 profile 配置后 registry seq/binding 集成断言。
  - `run_vdc_domain_tests.ps1` 通过。
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，20/20 host scripts passed。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过 A/B 双目标；build id `20260817061911`，package CRC `0x75320FF1`。
  - 本轮没有烧录板卡，不形成 UP/DOWN 同时运行或硬件闭环 evidence。
- 还需完成：
  - ring runtime 已由 TDMA-TASK-20260817-006 拆出。
  - 在 registry admission 之上建立五类固定队列和 time-aware gate。
  - 将 registry reject/deadline/budget 统计并入正式 `TdmaQualityVector`。
- 关联文件：
  - `components/tdma/inc/tdma_payload_registry.h`
  - `components/tdma/src/tdma_payload_registry.c`
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `tests/unit/test_tdma_payload_registry.c`
  - `tools/tests/run_tdma_payload_registry_tests.ps1`
- 下一步：
  - ring runtime 与 reason code 已完成；继续 scheduler queue runtime。

### TDMA-TASK-20260817-004 - Active Profile 到公共 Runtime 的激活闭环

- 状态：完成配置事务闭环、维护可观测性、定向 host 单测和 A/B 固件构建；未执行两板硬件闭环。
- 日期：2026-08-17
- 任务目标：
  - 让 RMTP 第 10 张 `TdmaFoundationProfile` 在激活后真正配置公共 TDMA runtime，而不是只停留在 active table view。
  - 在 TableRegistry 切换前拒绝与当前 VDC ring/schedule 不一致的 candidate，避免 active 表和 runtime 半提交。
  - 保持维护查询只读，并为后续 HIL 暴露 profile、ring 和 feedback evidence。
- 完成内容：
  - `refmem_realtime_tdma` 增加正式 foundation-profile 配置入口，业务上层不再访问内部 `scheduler` 成员。
  - prepared table views 增加 TDMA profile 的受控 copy getter；commit/discard 后 getter 立即失效。
  - VDC ring plan 增加 `cycle_period_ns`，作为 TDMA profile 的跨域只读调度契约。
  - 激活门禁比较 node count、local/reference、upstream/downstream、feedback、ring flags、ring/profile CRC、schedule CRC 和 cycle period。
  - factory profile 在 `distributed_refmem_init()` 中通过同一门禁装入 runtime；System Pack candidate 在 registry 激活和 model commit 后自动装入 runtime。
  - 新增 `REFMEM_TABLE_ACTIVATE_ERR_RUNTIME_PROFILE`，不一致候选映射为配置验证失败 NACK。
  - `SYSTem:REFMEM:SYNC:TDMA:STATus?` 在原字段末尾追加 profile CRC、owner、adapter、whitelist、ring config/runtime 和 feedback evidence。
- 验证结果：
  - `run_refmem_realtime_tdma_tests.ps1` 使用 Vivado MinGW GCC 通过。
  - `run_refmem_table_registry_tests.ps1` 使用 Vivado MinGW GCC 通过。
  - `run_vdc_domain_tests.ps1` 使用 Vivado MinGW GCC 通过。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过 A/B 双目标；build id `20260817060622`，package CRC `0x0271E168`。
  - 本轮没有烧录两板，也没有产生同时 UP/DOWN、硬件 timestamp correlation 或 `simultaneous_feedback_loop_evidence=1` 的 HIL 证据。
- 还需完成：
  - payload registry 已由 TDMA-TASK-20260817-005 拆出；ring runtime 仍待拆分。
  - 实现五类固定队列、time-aware gate、policing/backpressure 和 `TdmaQualityVector`。
  - 在两板固件中同时常驻运行 UP/DOWN leg，再进入闭环 timestamp HIL。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_realtime_tdma.h`
  - `components/distributed_refmem/src/refmem_realtime_tdma.c`
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
- 下一步：
  - 先拆分 `TdmaPayloadRegistry` 并建立可查询的 admission snapshot，再实现逐类固定队列。

### TDMA-TASK-20260817-003 - RMTP Foundation Profile 与 DeploymentGate 闭环

- 状态：完成正式表镜像、跨表资源门禁、host 全量验证和固件构建；profile activation hook 已由 TDMA-TASK-20260817-004 完成。
- 日期：2026-08-17
- 任务目标：
  - 将 TDMA foundation profile 从独立 C contract 升级为可由 SD/System Pack staging、激活和回滚的正式 RMTP 表。
  - 在激活前完成 owner、NodeLoad、SlotClaim、RealtimeCapabilityContract、物理 IO、payload 和容量一致性检查。
  - 冻结可供后续 time-aware scheduler 使用的周期容量、guard band 与 queue RAM 契约。
- 完成内容：
  - RMTP 表数量由 9 扩展为 10，新增 table id 9 `TdmaFoundationProfile`，owner 标记为 `TDMA_AO`。
  - 新增 71-word 固定 `u32 little-endian` profile row；编码/解码逐字段执行，不依赖 C struct padding。
  - profile 增加 cycle period、cycle capacity、guard band 和 queue RAM capacity；validator 拒绝总预留预算、单类 MTU 和队列 RAM overcommit。
  - staged candidate 必须恰好存在一个已加载 `TdmaSchedulerAO`，profile owner 必须匹配 NodeLoad local slot、owner resource claim、IO/IP claim 和 SlotClaim/RealtimeCapabilityContract。
  - TDMA adapter IO 改由 foundation owner 独占；业务 FB 只能通过 payload/intent 使用 TDMA，不能重复声明 adapter IO。
  - Python System Pack 生成器、pack builder、registry HIL validator 和 SCPI load validator 全部改为从统一 10 表定义派生 table count、mask 和 stage payload size。
  - active profile 到公共 runtime 的自动配置与 VDC schedule 交叉门禁已由 TDMA-TASK-20260817-004 补齐。
- 验证结果：
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，19/19 host scripts passed。
  - `python -m pytest tests\python\test_refmem_pack_build.py -q` 通过，2 passed；仅保留 OneDrive `.pytest_cache` warning。
  - `python tools\docs_check\docs_check.py` 通过，保留既有两项 risk review 文件名 warning。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过；最终 build id 和 package CRC 见本次提交记录。
- 还需完成：
  - 建立五类固定队列、time-aware gate、policing/backpressure 和 `TdmaQualityVector`。
  - 在两板硬件上同时运行 UP/DOWN leg，并用硬件 timestamp correlation 置位真实闭环 evidence。
- 关联文件：
  - `components/tdma/inc/tdma_profile.h`
  - `components/tdma/src/tdma_profile.c`
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tools/refmem_table_image/refmem_table_image.py`
- 下一步：
  - profile activation hook 已闭环；继续逐类固定队列与 time-aware gate。

### TDMA-TASK-20260817-002 - Foundation profile、HAOFV owner 与 TSN-style 流治理

- 状态：完成首版代码契约与 host 单元验证；RMTP profile 表已由 TDMA-TASK-20260817-003 补齐，真实逐流 scheduler 尚待实现。
- 日期：2026-08-17
- 任务目标：
  - 将 `TDMARingProfile` owner 从 VDC 迁入 TDMA Foundation。
  - 将 TDMA 表达为 HAOFV 可装载 system node，并冻结运行期资源声明。
  - 面向 VDC、RefMem、配置、OTA 和 LOG，吸收 TSN 的流分类、准入、time-aware gate、整形和背压思想。
- 完成内容：
  - 新增 `tdma_profile.h/.c`，定义 `tdma_ring_profile_t`、`tdma_foundation_profile_t`、adapter、PIO/SM/DMA/core1 resource、frame capacity、payload whitelist 和 profile CRC。
  - `vdc_tdma_schedule_profile_t` 改为只读嵌入 `ring_binding`；VDC schedule CRC 绑定 TDMA ring CRC，ring topology/CRC 校验由 TDMA owner 实现。
  - `tdma_service_configure_foundation_profile()` 将 owner、adapter、PIO/SM/DMA、core1 service、capacity、IO/IP claim、payload whitelist 和 profile CRC 冻结到 runtime snapshot。
  - payload registry 按 active whitelist 做 admission，未登记 payload 被拒绝。
  - 固定五类 traffic class：VDC realtime、RefMem realtime、config control、OTA bulk、LOG best effort；每类定义周期预算、帧数、队列深度、deadline、gate/shaping/preemption 和 overflow policy。
  - RefMem Application Model 增加 TDMA baseline capability、`TdmaSchedulerAO` instance、NodeLoad、resource/IP claim 和 DeploymentGate check；默认模型只允许一个 active TDMA owner。
- 验证结果：
  - `run_tdma_profile_tests.ps1` 使用 Vivado MinGW GCC host 执行通过。
  - `run_vdc_domain_tests.ps1` 使用 Vivado MinGW GCC host 执行通过。
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 全部通过，19/19 脚本均执行 host 测试。
  - `python -m pytest tests\python\test_refmem_pack_build.py -q` 通过，2 passed；仅有 OneDrive `.pytest_cache` 无法写入 warning，不影响测试结果。
  - `python tools\docs_check\docs_check.py` 通过，保留既有两项 risk review 文件名 warning。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，最终 build id `20260817051719`，package CRC `0x086043EE`。
- 还需完成：
  - 正式 RMTP/System Pack 表镜像和 DeploymentGate staged-candidate 校验已由 TDMA-TASK-20260817-003 完成。
  - 实现 core1 逐类队列、time-aware gate、policing/backpressure 和质量计数。
- 关联文件：
  - `components/tdma/inc/tdma_profile.h`
  - `components/tdma/src/tdma_profile.c`
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
- 下一步：
  - RMTP 表项和 activation hook 已完成；继续 scheduler queue runtime。

### TDMA-TASK-20260817-001 - 上/下行 TDMA 边界升格为基础件

- 状态：完成首版边界升级、文档主域、单元测试和构建验证；后续进入 TDMA system node / 同时 UP-DOWN runtime。
- 日期：2026-08-17
- 任务目标：
  - 根据架构纠偏，将上行/下行 TDMA 从 VDC 描述中拆出，作为独立基础件主域。
  - 保持 HAOFV 边界：TDMA owner 管 runtime、payload、adapter 和 evidence；VDC 只消费 timestamp，RefMem 只消费 payload completion。
- 完成内容：
  - 新建 `docs/tdma/` 标准三件套和 README。
  - `tdma_service` 已增加 ring runtime config/snapshot 字段，用于表达 active node、local/reference slot、UP/DOWN group、ring seq、running state、profile CRC 和 schedule CRC。
  - `tdma_service_configure_ring_runtime()` 拒绝 bad config：节点数不足、slot 越界、UP/DOWN group 相同、缺少 simultaneous flag、CRC 为 0。
  - 单元测试增加公共 TDMA ring runtime contract，验证 `up_running/down_running` 可由 runtime 发布，但 `simultaneous_feedback_loop_evidence` 仍保持 0，防止把配置就绪误判为闭环。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过 ARM GCC 编译门禁；当前环境未找到 host gcc，因此 host 执行跳过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，输出 `vdc_domain tests passed`。
  - `python tools\docs_check\docs_check.py` 通过，`files=90 warnings=2`；仅保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 和 `VDC_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `python -m py_compile tools\docs_check\docs_check.py` 通过。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260817043820`，package CRC `0xB91E63B5`。
- 还需完成：
  - 更新 `docs/README.md`、HAOFV 架构和 VDC 文档。
  - 后续把 TDMA 作为 HAOFV system node 接入 NodeLoad / DeploymentGate。
  - 后续建立真实 core1 同时 UP/DOWN runtime 和硬件 timestamp correlation。
- 关联文件：
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `tests/unit/test_refmem_realtime_tdma.c`
  - `docs/tdma/README.md`
  - `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
  - `docs/tdma/TDMA_DOMAIN_TODO.md`
- 下一步：
  - 按 HAOFV 索引和 VDC 文档收敛 TDMA ownership，再运行 host/doc 验证。
