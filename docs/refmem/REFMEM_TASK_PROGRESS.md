# Distributed RefMem 内部主域任务进度

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_TASK_PROGRESS.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`
Last updated: 2026-10-23

本文档记录 Distributed Vector Blackboard / RefMem Sync Domain 的阶段性任务进度、验证结果和后续动作。待办事项放在 `REFMEM_DOMAIN_TODO.md`，本文只记录已经发生的工作和可回溯结果。

### REFMEM-TASK-20261023-001 — C0/C1a 收口：命令槽放置冻结 + REFTMEM_CMD 全目标投递证据

- 状态：完成 C0 决策与 C1a HIL 证据；未改代码。等待 C11 独立复核。
- C0：规范物理位置冻结为 `distributed_refmem.c` 的 static `s_refmem_command_slot`；`ack_command` 预留区仅目录注册，不承载 live slot。已同步 `REFMEM-CTRL-01` 与 P8 C0。
- C1a（build `20261023121304`，`target_mask=0x0F`）：`REFTMEM_CMD` 从参考板 COM5 SCPI 入队 → TDMA 维护流 → COM4/COM3/COM6 每板 `rx_events/decoded/posted/acked=1`，源 `submitted/served/tx_ok=1`、`rejected=0`。
- 解析器：0x52/0x53/0x54 三类全过（0x53 `t2_static_applied=1`）；拒绝套件坏 CRC/坏 magic/坏 reserved 均按对应 parser 拒绝。
- 证据：`out/refmem_cmd_tdma/closeout_c0c1/`、`closeout_parser_suite/`、`closeout_reject_suite/`；评审提交单 `docs/check/submissions/REFMEM_UNIFIED_CONTROL_REVIEW_03.md`。
- 未覆盖：C1 非协调者 Entry 转发、C4/C5、C2/C3、聚合视图；五项契约保持 pending。
- 下一步：C11 独立复核 C0/C1a；之后按 C1 转发 → C2 负测 → C3 租约推进。

### REFMEM-TASK-20261021-005 — HAOFV 偏差审查

- 状态：完成条款修正；未改代码。
- 修正：预算字段、config-control subtype、session writer、ActionDispatcher 归属、本地/协调者边界、实时性。
- 验证：`docs_check` 229/0；`doc_regression` 48。
- 下一步：C11 独立复核。

### REFMEM-TASK-20261021-004 — 四条款复审 R2

- 状态：完成条款复审与修正；未改代码。
- 修正：ARMING 失败 DISARM；STOPPING 超时进 DEGRADED/FAULT；未注册 command_type 拒绝；mask 解析失败不回退本地。
- 文档：`docs/check/submissions/REFMEM_UNIFIED_CONTROL_REVIEW_02.md`。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（228 文件、0 warning）；`python tools/doc_regression_check.py` 通过（48 契约、19 进展日志）。
- 下一步：C11 独立复核；仍不改代码。

### REFMEM-TASK-20261021-003 — 单板控制保留条款

- 状态：完成 `REFMEM-LOCAL-01` 文档/契约；未改代码。
- 内容：
  - RefMem 统一面是增量层，不替换单板 SCPI、本地 command slot、本地 owner、本地 T2 `MOUNT/UNMOUNT`。
  - 本地 target 不走 `REFMEM_CMD`；统一面不可用时本地仍可用；远端/全目标必须拒绝 `UNIFIED_UNAVAILABLE`，不得静默缩成单板。
  - 两条路径共享同一本地门禁；EventPack 未激活不阻断本地基础命令。
- 契约：`REFMEM-LOCAL-01` pending；登记表/顶层/Review 01 §9 已同步。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（227 文件、0 warning）；`python tools/doc_regression_check.py` 通过（48 契约、19 进展日志）。
- 下一步：独立复核四条条款；仍不改代码。

### REFMEM-TASK-20261021-002 — 三缺口条款审查与修正

- 状态：完成条款级审查与修正；未修改固件/SCPI，未做板端验证。
- 结论：`PASS_WITH_CHANGES`；V-B1–V-B4 已改，V-M4（C11）仍开放。
- 修正：
  - `REFMEM-TRANSPORT-01`：active profile 必须含 config-control；缺失返回 `TRANSPORT_UNAVAILABLE`；`REFMEM_CMD_MAX_PAYLOAD` 绑定结构体上限。
  - `REFMEM-SESSION-01`：增加 `ARMING`；required 未全 arm 不得 START；START 后失败进入 `DEGRADED`；`T2:MOUNT` 仅兼容模式一次性映射。
  - `REFMEM-ACTION-01`：增加 `CommandHandlerRegistry`；有界 mailbox/优先级/BUSY；`action_registry_crc` 进 Version Bundle。
- 文档：`docs/check/submissions/REFMEM_UNIFIED_CONTROL_REVIEW_01.md`；索引已更新。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（227 文件、0 warning）；`python tools/doc_regression_check.py` 通过（47 契约、19 进展日志）。
- 下一步：独立复核三条条款；仍不改代码。

### REFMEM-TASK-20261021-001 — 单端口统一控制面三缺口 canonical 补齐

- 状态：完成架构/契约文档补齐；未修改固件、SCPI、工具或测试，未执行板端验证。
- 目标：只补三个 canonical 缺口——`REFMEM-TRANSPORT-01`、`REFMEM-SESSION-01`、`REFMEM-ACTION-01`，不改代码。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 新增三个同名小节：`REFMEM_CMD` 复用 TDMA config-control；系统级 `TriggerSession`（CONFIGURE/ARM/START/STOP/ABORT + 状态机 + T2 MOUNT 映射）；`ActionRegistry`/`ActionDispatcher`（action_id→handler、post、缺 handler gate）。
  - 命令类型表加入 `TRIGGER_SESSION_*`。
  - `TDMA_DOMAIN_ARCHITECTURE.md` 增加 `REFMEM-TRANSPORT-01` 载体条款；`TDMA_DOMAIN_TODO.md` 增加文档联审项。
  - `DOCS_REGISTRY.md` 登记三条 pending 契约；顶层 `HAOFV_ARCHITECTURE.md` 增加三缺口摘要。
  - `REFMEM_DOMAIN_TODO.md` 新增 M0 文档补齐清单；M1–M5 标为实现阶段、不在本目标。
- 边界：三条契约均 pending，未实现、未联审；不新增第二命令槽、不接 POSITION、不让 Event Plane 执行实时边沿。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（226 文件、0 warning）；`python tools/doc_regression_check.py` 通过（47 契约、19 进展日志）。
- 下一步：M0.5 独立复核三条条款；复核通过后仍不写代码。

### REFMEM-TASK-20261020-001 — 统一控制面契约细化与架构落位

- 状态：完成文档契约细化与登记；未修改固件、SCPI 或工具实现，未执行板端验证。
- 目标：把“通过任意一个板卡接口操作 RefMem、实现所有板卡的操作”从报告层愿景收敛为 RefMem 域可验证契约，并明确接入点无关、目标本地执行、显式掩码、单一协调者和聚合完成的边界。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 新增 `REFMEM-CTRL-01` 章节：CP1–CP8 不变量、Control Entry/Coordinator/Target/Observer 角色、命令信封既有字段语义与扩展字段、路由与命令状态机、fence/超时/部分成功、聚合与逐目标读模型、权限与运行态矩阵、失败恢复、边界、C0–C5 阶段和当前实现状态。
  - 架构章节补充“挂接关系：控制面接入 RefMem”：命令面、观测面、目录面三个挂接面；RefMem 是控制面的 bearer/router，不是执行者；不另建控制总线、第二套命令槽或第二套 ACK 事实。
  - 记录当前单板命令面物理放置：live slot 为 `distributed_refmem.c` 的 static `s_refmem_command_slot`，`refmem_vector_table_t.ack_command`（`DISTRIBUTED_REFMEM_ACK_CMD_SIZE`）仅目录注册；C0 需冻结“迁入该区”或“static 为规范位置”。
  - `DOCS_REGISTRY.md` 登记 `REFMEM-CTRL-01`（pending），`clause_loc` 指向架构章节，`code_anchor` 为 `refmem_command.h`。
  - 顶层 `HAOFV_ARCHITECTURE.md` 同日刷新：RefMem 定位补“统一控制面”，核心规则补接入点无关/目标本地执行/无强制位一条。
  - `REFMEM_DOMAIN_TODO.md` 新增 P8 统一控制面十项实现清单。
- 实现边界：跨节点命令路由、协调者租约、聚合视图、跨节点 FENCE 均未实现；当前只有本地 command slot 与 `SYSTem:COMMand:ACK?`/`NACK?`。本条不授予“任意接口控制全部设备”已产品化的结论。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（214 文件、0 warning）；`python tools/doc_regression_check.py` 通过（44 契约、10 顶层条款落点、18 进展日志）；`FORCE_DOC_GATES=1 sh .githooks/pre-commit` 通过。`python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider` 为 36 passed / 2 failed，失败项是 `TDMA_TASK_PROGRESS.md` 轮转债务和 `SYNC_IO_TASK_PROGRESS.md` 顺序债务在 2026-10-17 截止后的既有时间炸弹，与本次改动无关，未在本切片修复。
- 下一步：按 P8 顺序先冻结 C0 schema 与目录 CRC，再做 C1 两板 Entry→Coordinator→Target HIL。

### REFMEM-TASK-20260916-001 — 静态向量缩容与布局版本迁移

- 对应 `REFMEM-RAM-001/002/003/004`。范围为静态预留缩容、布局版本迁移和必要的 host 工具联动；保留区域 ID/顺序、owner、整表 clear、VDC/DPLL payload 及 seqlock、node 现有临界区。不改 OTA、PIO/DMA、TDMA 实时服务或 MPU，不引入 lease/TLV 解析前置。
- 实现快照，非容量契约：表从 65536 B 缩至 18432 B，布局版本升级为 2；node 固定 8 个逻辑槽，每槽 128 B。ARM 有效占用为 HEADER 264 B、NODE 每槽 36 B、VDC 含同步前缀 232 B、DPLL 224 B，均适配新分配。其余普通数组未发现实际 backing 字节写者，应用模型中的区域引用保留；不将这次缩容解释为对应业务摘要已实现。
- 生产验证：节点容量 2 至 8 配置及兼容反例共 17 项通过，既有 VDC vector suite 在 6/8 节点通过，vector table 的 ARM 6/8 编译通过。`production-delivery.json`、`capacity-probe.json` 和 `node-capacity.xml` 绑定修改及取证。
- A/B Release 与 Flash 链接检查通过，build `20260916151903`。相对冻结的 VDC r5，两槽实际静态 RAM 均净释放 47104 B；保留原 2 KiB heap 后余量从 28 B 增至 47132 B，SCRATCH_X 间隙仍为 24 B。新二进制独审重新计算栈，保持原限定四板 persona 的最深余量；不代表所有可选负载安全或原运输/调度失败已修复。见 `measured-build/`、`ram-comparison.json`、`independent-review/linked-shrink-review.json`。
- 布局 ABI 同时影响 Python 产包器和 CORE:VECTOR/PROT:STATUS 读回版本判定；RMTP 格式、VDC/DPLL payload 及 TDMA wire 的独立版本保持。实际 Python 包进入生产 owner 链时发现既有 TDMA ring CRC 算法漂移，原失败保存在 `host-layout-r1.xml`、`host-diagnose-r1/`；同步算法后继续验证旧布局正确 CRC 包拒绝及 active/rollback 内容保持，不以坏 CRC 反例冒充布局验证。
- 工具/兼容矩阵最终 25/25 通过，独立复跑相关测试 22/22 通过，源码指纹一致。真实 CLI 包在 6/8 节点及普通/short-enums 编译条件下进入生产 owner、stage、prepare、activate、commit；正确 CRC 的旧布局包由 ApplicationMap owner 拒绝。纯 validation 保留三角色，失败 stage 可按原语义清 staging，但 active/rollbackable 内容及 descriptor 不变。见 `tool-test-delivery.json`、`host-layout-r2.xml`、`independent-review/joint-delivery-review.json`。
- 当前六节点固件产包须显式使用 `refmem_pack_build --tdma-node-count 6`；RefMem 仍保留八个逻辑槽。`sd_fs_build` 仍使用默认八节点产包，本轮不扩展其部署能力；后续需要六节点 SD 整包时另做工具切片。
- `REFMEM-LAYOUT-01` 已在域 canonical 定义并登记 pending，顶层同日可见；独立 C11 接受该规范/登记，不代表全部硬件门禁通过。
- 四板 OTA 更新成功，板端 UID/build 均与目标匹配；全部 STOP 后 RefMem 专项通过：容量 18432 B、布局 2、八逻辑槽、初始化 READY/error=0、目录有效/CRC 标志、CORE:VECTOR/PROT:STATUS 版本和八槽逐项查询均正常，错误队列为空。原件为 `baseline-refmem.json`、`refmem-readback.json`、`stopped-diagnostics.json`；完整目录字节未通过硬件接口导出，目录 offset/size 由 host 与 ARM 结构检查证明。
- 当前源码 quick P3 已实际执行，`flow_completed=true`，但 `strict_gates_passed=false`，`closed_loop_passed=false`、`realtime_gate_passed=false`。仍报 `explicit startup barrier timed out`；启动观测曾出现一次健康样本，随后再次失效，未满足稳定窗口；持续运输和 VDC/RefMem 调度门禁也未通过。该结果与缩容前 r5 同属运输/调度未闭合，不能推断 RAM 改动已修复或导致全部时序问题。见 `p3-r1/acceptance.json`、`p3-r1/tdma-process-image/summary.json`。
- 本轮 P3 绑定源码 `0593b3ace86d5895184b1cf98ff7aeba5d3cc6bd829efc90df0550ea16db818c`、build `20260916151903`，包 SHA-256 为 `3be05d080112b40cd33549322af3408549bdecd48f57b385efe99480c21d9222`，与部署前 manifest 一致。随后验收分级工具完成并重新通过匹配源码四板 quick P3，代码已于 2026-09-17 提交为 `e6ff06a`，提交指纹及证据见 `VDC-PROGRESS-20260917-001`；诊断凭证不提升为严格通过。按下述验收范围澄清，`REFMEM-RAM-001/002/003/004` 均已完成本轮调试切片验收；下一步回到 `VDC-FAST-002` 处理运输/调度。
- 独立硬件复核通过指纹、全部 receipt 引用哈希、冻结产物及四份 native 记录重解一致性。观测窗口快照，非性能保证：NO1–NO4 的 missing 增量相对 r5 从 `3/2/4/3` 变为 `0/1/0/1`，NO3 node/soak 门禁通过；NO1 反馈 gap 仍为 3，NO2/NO4 仍有缺收。稳态 soak 未见 DOWN，但启动 sample 3 有 NO4 DOWN/invalid/failure，sample 5 短暂健康后 sample 6 再失败，最终 2.006283 s 超时。四板 VDC deadline 仍增长，部分从板 RefMem deadline 仍增长，不能将单次变化归因为缩容的确定性性能收益。见 `independent-review/hardware-shrink-review.json`。
- 本轮证据目录：`out/HardwareAcceptance/20260916/refmem-static-shrink-r1/`。缩容前运输与调度失败基线见 `VDC-PROGRESS-20260916-042`，后续结果须保留差分。RAM 切片验收后返回 `VDC-FAST-002`，不以释放 RAM 代替 DPLL 闭环。
- 验收范围澄清：用户指出范围不得随调试无限扩大。主控复核 r5 与本轮的配置 SHA-256 均为 `c032843ba0eab2e87bb3399920c0fe54f79cf1eddbb4404a7ed1d85b1ea34fc2`，均为 `QUICK_DIAGNOSTIC`，启动及窗口条件相同；本轮没有扩大 P3 脚本范围。`p3_hardware_acceptance.py::_validate_quick_diagnostic_receipt` 明确允许流程完成且保留 phase quality 失败，`check_staged` 支持该凭证类型。先前将 `strict_gates_passed=false` 解释为 RAM004 不能结项，是主控扩大退出条件，现已纠正；原始失败、凭证和源码不改。提交时仍须核验真实 staged 指纹，本条不代替提交门禁。

### REFMEM-TASK-20260817-052 - RefMem 初始化阶段码固化

- 状态：完成代码、接口文档、构建和 COM5/COM6 板端验证。
- 日期：2026-08-17
- 任务目标：
  - 固化 `distributed_refmem_init()` 的初始化阶段码，避免 HIL 闭环失败时只能看到 `core1=0` 或 `service_count=0`。
  - 保持 `SYSTem:REFMEM:STATus?` 既有前 8 个字段不变，追加 `initialized,init_stage,init_error` 作为永久诊断字段。
- 完成内容：
  - `distributed_refmem_status_t` 增加公开 `DISTRIBUTED_REFMEM_INIT_STAGE_*` 枚举，覆盖 app model、TDMA owner、command slot、NodeLoad AUTO、TDMA ops、TDMA profile、vector table 和 ready。
  - `distributed_refmem_init()` 的临时数字阶段替换为固定枚举；失败时 `init_error` 记录失败阶段，成功时 `init_stage=READY`、`init_error=0`。
  - `distributed_refmem_publish_status_locked()` 每次刷新时同步 `initialized`，避免 service 后状态字段漂移。
  - `SCPI_COMMANDS.md` 同步 `SYSTem:REFMEM:STATus?` 的 11 字段返回格式。
- 验证结果：
  - `run_tdma_profile_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过。
  - `run_tdma_traffic_scheduler_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260817104554`。
  - COM5 经 BOOTSEL factory UF2 恢复到 build `20260817104554` 后，`SYSTem:REFMEM:STATus?` 返回 `65536,1,97043,0,8,97042,97042,7,1,8,0`，末尾 `initialized,init_stage,init_error=1,8,0`。
  - COM6 经 OTA boot/commit 到 build `20260817104554` 后，`SYSTem:REFMEM:STATus?` 返回 `65536,1,22775,0,8,22774,22774,7,1,8,0`，末尾 `initialized,init_stage,init_error=1,8,0`。
  - 两板 `SYSTem:ERRor?` 均返回 `0,"No error"`。
- 后续动作：
  - 初始化阶段码已可用于后续定位；若再次出现 `core1_loop_count=0`、RefMem service 不递增或 `init_error!=0`，优先按 `init_stage/init_error` 映射到 app model、TDMA owner、NodeLoad AUTO、TDMA profile 或 vector table 阶段，而不是继续盲目扩大 heap。

### REFMEM-TASK-20260817-051 - 事件驱动局部同步约束冻结

- 状态：完成架构与待办细化；通用 dirty queue 和 per-target generation completion 尚未实现。
- 日期：2026-08-17
- 任务目标：
  - 明确 64 KB RefMem 不按周期整表广播，避免同步负载破坏 VDC/TDMA 实时性。
  - 将触发、排队、局部更新、ACK/fence 清 dirty 的生命周期冻结为产品契约。
- 完成内容：
  - 正常同步改为 owner fact commit 触发 dirty descriptor，critical delta 进入 TDMA short frame。
  - 状态事实允许在编码前按 slot/field 合并；命令、事件和累加量必须使用有界队列或 accumulate 语义。
  - dirty 清除绑定 per-target ACK/fence 和 generation，禁止 `transport_sent` 后提前清除，也禁止旧 ACK 清除新更新。
  - full snapshot 仅用于 bootstrap/resync/显式维护，必须走 long-frame 分片、CRC 和 fence，不允许周期整表刷新。
- 验证结果：
  - 本项为架构约束细化；当前已有 delta/dirty mask/frame/ACK/fence 基础契约，但尚无通用 dirty queue 的新增代码验证。
- 后续动作：
  - 在 TDMA PIO ring adapter 接通后，实现 dirty descriptor queue、critical/background 分级和 per-target generation completion。

### REFMEM-TASK-20260816-050 - NodeLoad AUTO HIL default adapter profile correction

- 状态：完成工具修复并通过 COM5/COM6 默认 HIL。
- 日期：2026-08-16
- 任务目标：
  - 修复 `refmem_node_load_auto_hil_validate.py` 默认路径误用 `REALtime:IO` 线序预检推导 RefMem TDMA adapter 的问题。
  - 保持 HAOFV 边界：GPIO16-24 是 TDMA/RefMem 通讯环路；GPIO4-7 和 `REALtime:IO` 是业务/观测 overlay，不能作为 RefMem transport 默认推导来源。
- 完成内容：
  - 脚本新增 `--adapter-profile`，默认 `min-system-gpio16-24`，直接使用当前两板最小系统已验证的 3-wire uplink/downlink adapter：A `uplink=16,18,23`、A `downlink=16,22,23`，B `uplink=16,18,23`、B `downlink=16,21,23`。
  - `io-preflight` 改为显式 profile，只用于 REALtime IO overlay 线序诊断；该路径失败时会写出 `io_preflight_result.json`，避免只返回一句错误。
  - 修复脚本生命周期问题：IO preflight 子进程增加超时，轮询等待拆分为“单条 SCPI 查询 timeout”和“同步总 timeout”，关键输出立即 flush，避免超时后残留 Python 进程占住 COM 口。
- 验证结果：
  - `python -m py_compile tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py` 通过。
  - `python tools\two_board_io_validate\two_board_io_validate.py --port-a COM5 --port-b COM6 --name-a A --name-b B --expect-a-to-b auto --expect-b-to-a auto --out-dir build-rtos-multicore-smoke\debug_refmem_io_preflight_manual --timeout 2` 快速失败，证实 REALtime IO overlay 当前不等同于 RefMem TDMA 通讯环路。
  - `python tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py --port-a COM5 --port-b COM6 --timeout-s 2 --sync-timeout-s 30 --maintenance-s 0.5` 默认参数通过，记录位于 `build-rtos-multicore-smoke\refmem_node_load_auto_hil_20260816235020\records.json`。
- 后续动作：
  - 继续 P0 的 ACK/重发/fence completion；当前 maintenance 阶段仍可在 TDMA snapshot 中观察到 `WINDOW_MISSED` 质量证据，不能把“重跑通过”替代产品级可靠性。

### REFMEM-TASK-20260816-049 - RefMem payload standard TDMA registration

- 状态：完成代码、host/build 验证和 COM5/COM6 AUTO HIL 回归。
- 日期：2026-08-16
- 任务目标：
  - RefMem 作为公共 TDMA payload producer/consumer 接入，不能在 `refmem_realtime_tdma` 兼容适配层里临时声明 binding。
  - 保持 HAOFV 边界：`DistributedRefMemAO` 负责 RefMem payload contract，`components/tdma` 只执行统一 scheduler/transport/timestamp spine。
- 完成内容：
  - 新增 `refmem_tdma_payload.h/.c`，集中注册 `REFMEM_DELTA` 和 `REFMEM_ACK_FENCE` short-frame payload binding。
  - `refmem_realtime_tdma_init()` 改为调用 `refmem_tdma_payload_register()`；自身继续只做旧 API 到公共 `tdma_service` 的兼容适配。
  - `tdma_service` submit 阶段校验 `frame_class/payload_class/max_payload_size`，未注册 payload 不允许进入 core1 intent mailbox。
  - RefMem host 单测增加标准注册断言：`REFMEM_DELTA` / `REFMEM_ACK_FENCE` 可提交，未注册 VDC payload 在 RefMem-only TDMA service 中被拒绝。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260816103607`。
  - COM5/COM6 多线程 OTA 到 `20260816103607` 通过。
  - 首次 AUTO HIL 运行 A->B 通过、B->A 只收到 1/2 帧，现场状态显示接收侧 TDMA `WINDOW_MISSED`，不是 payload registry 拒绝；随后重跑完整 HIL 通过，记录位于 `build-rtos-multicore-smoke\refmem_auto_tdma_payload_regression_retry\records.json`。
- 后续动作：
  - P0 优先将 AUTO NodeLoad 从“单发 TX + 对端 RX 窗口命中”升级为 ACK/重发/fence completion，避免窗口 missed 后丢失一个 NodeLoad delta。TDMA 的核心验收是时间可预期和 completion 可证明，不能只以重跑成功作为产品级可靠性证据。
  - 继续将 long frame payload 用同样 registry 方式接入 OTA/config/log，不得绕过 `components/tdma`。

### REFMEM-TASK-20260816-048 - TDMA component extraction baseline

- 状态：完成并通过 COM5/COM6 RefMem 同步 HIL
- 日期：2026-08-16
- 任务目标：
  - 解决 “VDC 一套 TDMA、REFMEM 一套 realtime_tdma” 的命名和所有权混乱。
  - 将 TDMA scheduler / PIO transport / timestamp spine 抽为 `components/tdma` 基础件；RefMem 和 VDC 只作为 payload producer/consumer 挂载，不再拥有私有 TDMA scheduler。
- 完成内容：
  - 新增 `components/tdma/inc/tdma_service.h`、`components/tdma/src/tdma_service.c`，承载 core0 intent mailbox、core1 result snapshot、scheduled window、transport ops、短帧/长帧和 payload binding 基础契约。
  - `components/distributed_refmem/inc/src/refmem_realtime_tdma.*` 改为 RefMem 兼容适配层：旧 SCPI/quality/HIL API 保持字段不变，实际执行委托给 `tdma_service`。
  - RefMem DELTA 与 ACK/FENCE payload 作为 short frame payload binding 注册到 TDMA service；后续 VDC sync sample、idle beacon、OTA/config/log long frame 走同一基础件扩展。
  - 修正抽离后首轮 HIL 暴露的容量边界：`tdma_service` 保留 long frame 1024 字节上限，RefMem 物理适配层在 receive bridge 中按 `REFMEM_REALTIME_TDMA_FRAME_MAX` 收敛短帧接收容量，避免把长帧能力误传给 RefMem PIO RX DMA 上限校验。
  - `application/src/app.c` 将 VDC/DPLL service 收敛到 core1 realtime loop，core0 RTOS task 不再运行 VDC/DPLL 维护循环。
  - `tools/tests/run_host_unit_tests.ps1` 将 `run_refmem_realtime_tdma_tests.ps1` 纳入总门禁，避免 TDMA 兼容层只编译不执行。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，18/18 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260816095934`，package CRC `0x99D513A9`。
  - COM5/COM6 OTA send、boot 和 commit 到 build `20260816095934` 通过，两板 `SYSTem:FW:BUILD?` 均返回 `"20260816095934"`，错误队列均为 `0,"No error"`。
  - 执行 `python tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_node_load_auto_hil_COM5_COM6_20260816095934_tdma_component` 通过。
  - HIL 证据：A->B 只对 COM5 下发两条 `LOAD:NODE` 后，COM6 `applied_rx_count` 增长到 2，`last_frame_type=3`，NodeLoad TableRegistry CRC 与 COM5 一致；B->A 只对 COM6 下发两条 `LOAD:NODE` 后，COM5 `applied_rx_count` 增长到 2，`last_frame_type=3`，NodeLoad TableRegistry CRC 与 COM6 一致。
  - 增强版 AUTO 维护验证通过：`tools/refmem/refmem_node_load_auto_hil_validate/refmem_node_load_auto_hil_validate.py` 在 IO preflight 前先关闭残留 AUTO 并释放 `REALtime:IO`；同步完成后继续观察两板自动 RX 维护，确认 `pending_count=0`、`last_error=0`、`submitted_rx_count` 和 TDMA `service_count/completed_seq` 继续增长。记录位于 `build-rtos-multicore-smoke\refmem_node_load_auto_hil_COM5_COM6_20260816095934_auto_maintenance\records.json`。
  - 两方向 `SYSTem:REFMEM:CLAIM? 5` 返回一致的 SlotClaimMap 摘要，最终两板 `SYSTem:ERRor?` 均为 `0,"No error"`。
- 后续动作：
  - 继续把 VDC observation sample、idle beacon 和 long frame payload 从文档契约落到 `components/tdma` 的 producer/consumer 注册机制。

### REFMEM-TASK-20260816-047 - TDMA time-unit naming and diagnostic timestamp snapshot

- 状态：完成 host/build 验证；板端 DPLL timestamp latch 待实现
- 日期：2026-08-16
- 任务目标：
  - 统一 RefMem realtime TDMA 对外 deadline 口径：deadline 字段改为 `deadline_us`，质量字段改为 `p99_us/p999_us`。
  - 暴露当前 core1 realtime TDMA 诊断时间戳，作为后续 VDC/DPLL bring-up 的过渡 evidence，但不进入 100 ns lock gate。
- 完成内容：
  - `refmem_realtime_tdma` snapshot 追加 submit/core1 arm/start/done/elapsed ns 字段，并声明 `timestamp_source=SOFTWARE_US`、`timestamp_resolution_ns=1000`、`timestamp_flags=DIAGNOSTIC_ONLY`。
  - `DistributedRefMemAO` AUTO 配置、FENCE payload、QUALITY payload、RefMem command timeout、SCPI 维护命令和三份 HIL 脚本统一到 `deadline_us` / `timeout_us` 命名。
  - `refmem_spi_physical_adapter` 公开 RX timeout 参数改为 `timeout_ms`，保持内部 SDK 等待实现不扩散到协议命名。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，15/15 host test scripts passed。
  - `python -m py_compile tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py tools\refmem_quality_gate_hil_validate\refmem_quality_gate_hil_validate.py tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815174141`，package CRC `0x58997C54`。
- 后续动作：
  - 继续 VDC `VdcTDMATimestampEvidence` 和硬件 timestamp latch；RefMem 只提供 payload、quality 和 evidence，不拥有 DPLL offset/rate。

### REFMEM-TASK-20260815-046 - NodeLoad AUTO 3-wire uplink/downlink adapter correction

- 状态：完成并通过 COM5/COM6 B3 HIL
- 日期：2026-08-15
- 任务目标：
  - 修正 B3 HIL 未收到 frame 的结构性原因：NodeLoad AUTO 不能复用单组 pins，必须区分环路上行 adapter 和下行 adapter。
  - 按产品链路收敛为无 CS 的 3-wire SPI；每组 adapter 保留 `duplex_mode,rx,sck,tx`，支持 half/full duplex 配置。
- 完成内容：
  - `DistributedRefMemAO` 的 AUTO 配置由单组 pins 改为 `uplink_adapter_pins` 和 `downlink_adapter_pins`，并新增 uplink/downlink duplex mode。
  - NodeLoad AUTO RX window 使用 uplink adapter，语义为接收上一个板卡的数据；pending NodeLoad DELTA TX 使用 downlink adapter，语义为下发到下一个板卡。
  - `SYSTem:REFMEM:SYNC:AUTO` 改为配置 `enable,local_slot,target_mask,baud,deadline,uplink_mode,uplink_rx,uplink_sck,uplink_tx,downlink_mode,downlink_rx,downlink_sck,downlink_tx`；`AUTO?` 返回同序 snapshot 和计数/evidence。
  - `refmem_spi_physical_adapter` 支持 `csn_pin=REFMEM_SPI_PHYSICAL_PIN_UNUSED` 的无 CS 模式：master 不驱动 CS，slave PIO 不等待 CS，仍由 RM packet magic、length、CRC 和 RX stable 判断帧边界。
  - `tools/refmem/refmem_node_load_auto_hil_validate/refmem_node_load_auto_hil_validate.py` 固化两板 B3 验收，自动运行 IO preflight 后推导 A/B 的 uplink/downlink adapter，并只向源板发送 `LOAD:NODE`。
- 验证结果：
  - `python -m py_compile tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，15/15 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815165314`，package CRC `0x36DE7654`。
  - OTA build `20260815165314` 到 COM5/COM6 并 commit 通过，两板 `SYSTem:FW:BUILD?` 均返回 `"20260815165314"`，`SYSTem:ERRor?` 均为 `0,"No error"`。
  - 执行 `python -u tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_node_load_auto_hil_COM5_COM6_20260815165314` 通过。
  - IO preflight 实测线序：A->B 为 `[1,2,0,3]`，B->A 为 `[2,1,0,3]`。
  - AUTO 配置证据：A 使用 `SYSTem:REFMEM:SYNC:AUTO 1,0,2,25000000,1000000,1,16,18,23,1,16,22,23`，B 使用 `SYSTem:REFMEM:SYNC:AUTO 1,1,1,25000000,1000000,1,16,18,23,1,16,21,23`。
  - A->B：只向 A 下发 `LOAD:NODE 6,10,...` 和 `LOAD:NODE 5,9,...` 后，B `AUTO?` 显示 `applied_rx_count` 增长到 2，`last_frame_type=3`，`last_source_slot=0`，NodeLoad table staging CRC 与 A 一致。
  - B->A：只向 B 下发同样两条 `LOAD:NODE` 后，A `AUTO?` 显示 `applied_rx_count` 增长到 2，`last_frame_type=3`，`last_source_slot=1`，NodeLoad table staging CRC 与 B 一致。
  - HIL 记录：`build-rtos-multicore-smoke\refmem_node_load_auto_hil_COM5_COM6_20260815165314\records.json` 和 `adapter_plan.json`。
- 后续动作：
  - 在 B3 已闭环基础上，继续 P0 表镜像 activation gate 的跨节点 ACK/FENCE 与 owner validation callback 调度。

### REFMEM-TASK-20260815-045 - NodeLoad AUTO gate correction and HIL script

- 状态：完成 host/build 验证；COM5/COM6 HIL 待执行
- 日期：2026-08-15
- 任务目标：
  - 支持通过 SCPI 配置 RefMem NodeLoad 是否自动运行，并保证开启 AUTO 后仍可在 idle 业务状态提交本地 `LOAD:NODE`。
  - 固化 B3 双板验收脚本，验证 PC 不搬运 frame hex 的 NodeLoad 双向同步。
- 完成内容：
  - 新增 `distributed_refmem_can_accept_node_load_intent()`，由 `DistributedRefMemAO` 判断 NodeLoad intent 是否可接收。
  - `DistributedRefMemAO` 允许抢占自己挂起的 AUTO RX window：本地 `LOAD:NODE` staging 前 abort AUTO RX，staging 成功后进入 pending TX 队列；手工 TDMA/其他实时活动仍不被抢占。
  - `SYSTem:REFMEM:LOAD:NODE` 改为使用 RefMemAO 判定；`SYSTem:REFMEM:LOAD:SD` / `LOAD:BOARD` 仍保持 realtime idle 门禁。
  - 修复 RMTP inline package CRC 生成端：ApplicationModel 不再依赖 `ota_crc32_update()` 的隐含初值语义，改为本地 CRC32 算法，与 TableRegistry validator 的 package/payload/table CRC 完全一致。该问题此前导致板端 `LOAD:NODE` / `LOAD:BOARD` 返回 `PACKAGE_INVALID`，host stub 未暴露。
  - inline package 构建失败时保留 package validator 的失败表证据：若 validation 已给出 `first_bad_table`，则通过该表的 registry entry 暴露 `FAILED + last_result=package_error`，避免所有失败都折叠成 `BAD_TABLE_VERSION`。
  - 新增 `tools/refmem/refmem_node_load_auto_hil_validate/refmem_node_load_auto_hil_validate.py`：COM5/COM6 分别开启 `SYSTem:REFMEM:SYNC:AUTO`，只对源板下发两条 `LOAD:NODE`，目标板只查询 `AUTO?`、`LOAD:STATus?`、`TABle? 3` 和 `CLAIM?` 证据；随后反向执行。
- 验证结果：
  - `python -m py_compile tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，15/15 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815162909`，package CRC `0xFA4FE362`。
- 后续动作：
  - 将 build `20260815162909` OTA 到 COM5/COM6。
  - 执行 `python tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py --port-a COM5 --port-b COM6`，若出现窗口碰撞，再按 RefMemAO/TDMA 调度策略修正，不退回 PC frame bridge。

### REFMEM-TASK-20260815-044 - Configurable NodeLoad auto sync service

- 状态：完成 host/build 验证；板端 HIL 待执行
- 日期：2026-08-15
- 任务目标：
  - 推进风险评审 §3.4 子项 B2：NodeLoad DELTA 不再依赖维护命令逐帧触发，改由 RefMemAO service 和 core1 TDMA 自动运行。
  - 自动运行必须可配置，默认关闭；SCPI 只设置运行策略或提交 LOAD intent，不承担对端 frame 搬运。
- 完成内容：
  - `DistributedRefMemAO` 增加 NodeLoad auto sync 队列，`LOAD:NODE` / `ModelTurntable LOAD` staging 成功后将 instance id 加入 pending 队列。
  - `distributed_refmem_service()` 在临界区外推进 auto sync：有 pending NodeLoad 时提交 TDMA TX；无 pending 时按配置提交 RX window；收到 RX frame 后由 RefMem Sync state machine 校验，再交给 ApplicationModel staging。
  - 新增 `distributed_refmem_configure_node_load_auto_sync()` 和 `distributed_refmem_get_node_load_auto_sync()`，支持 enable、local slot、target mask、baud、deadline 和 PIO SPI pins。
  - 新增维护命令 `SYSTem:REFMEM:SYNC:AUTO` / `SYSTem:REFMEM:SYNC:AUTO?`；默认关闭，开启后才会自动占用 PIO SPI transport。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_node_load_sync_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，15/15 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `git diff --check` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815160049`，package CRC `0x11683118`。
- 后续动作：
  - 执行完整 host/docs/build 验证。
  - 固化 COM5/COM6 HIL 脚本：两板先分别开启 AUTO，再只对源板下发两个 `LOAD:NODE`，确认目标板 staging/table/claim/quality 同步；随后反向执行。

### REFMEM-TASK-20260815-043 - NodeLoad sync delta foundation

- 状态：完成 host/build 验证；板端 HIL 待执行
- 日期：2026-08-15
- 任务目标：
  - 推进风险评审 §3.4 子项 B：建立两板真实 NodeLoad 同步所需的数据语义和 RefMemAO apply 入口。
  - 保持 HAOFV 边界：RefMem Sync 只承载受控 DELTA；ApplicationModel 仍由 DistributedRefMemAO owner staging；SCPI 不直接改对端表。
- 完成内容：
  - 新增 `refmem_node_load_sync.h/.c`，定义 NodeLoad entry 到 RefMem DELTA payload/frame 的固定编码和解码。
  - 新增 `test_refmem_node_load_sync.c` 和 runner，覆盖 payload roundtrip、frame roundtrip、mirror 可见性和非法 entry 拒绝。
  - `ApplicationModel` 增加 `refmem_application_model_get_staging_node_load_entry()`，只导出当前 staging NodeLoad entry 的 const copy。
  - `DistributedRefMemAO` 增加 `distributed_refmem_build_node_load_sync_frame()` 和 `distributed_refmem_apply_node_load_sync_payload()`；源端从 staging entry 生成 NodeLoad DELTA，目标端解码后通过 ApplicationModel staging API 合并到本地 staging image。
  - 新增维护入口 `SYSTem:REFMEM:SYNC:TDMA:NODE:TX`：源板不再需要 PC 搬运 frame hex，可从本地 staging entry 直接提交 TDMA TX intent。
  - `SYSTem:REFMEM:SYNC:RX` 和 `SYSTem:REFMEM:SYNC:TDMA:FRAMe?` 收到 NodeLoad DELTA 后会交给 RefMemAO apply staging；非 NodeLoad DELTA 仍只更新 sync mirror。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_node_load_sync_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，15/15 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `git diff --check` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815154736`，package CRC `0xE10EC500`。
- 后续动作：
  - 补全自动 RX/TX 调度：当前仍需要维护面触发目标板 RX/FRAMe 查询，不满足最终“只对源板下发 SCPI LOAD intent”的验收。
  - 用 COM5/COM6 跑双向两节点同步 HIL，确认 X->Y 与 Y->X 的 NodeLoad staging、TableRegistry CRC、SlotClaimMap 和 quality/evidence 一致。

### REFMEM-TASK-20260815-042 - P0 risk 3.4 fake TX correction

- 状态：子项 A 完成 host/build 验证；子项 B 两板真实同步待实现
- 日期：2026-08-15
- 任务目标：
  - 修复风险评审 §3.4：`refmem_pio_spi_adapter_send()` 不能作为无物理后端的 fake TX 返回成功。
  - 保持 HAOFV 边界：旧 `SYSTem:REFMEM:SYNC:*?` 只作为 frame builder / PC bridge 诊断入口；真实发送必须走 physical adapter 或 core1 TDMA owner 路径。
  - 最终验收不是“能生成 hex”，而是两板真实双向同步：X 板 SCPI 加载两个节点并同步到 Y，随后 Y 板 SCPI 加载两个节点并同步回 X。
- 完成内容：
  - `refmem_transport_adapter.h` 增加 `REFMEM_TRANSPORT_ERROR_TX_UNBOUND`。
  - `refmem_pio_spi_adapter_send()` 在 frame/MTU 校验通过后仍拒绝发送，记录 `tx_reject_count`、`last_tx_size` 和 `TX_UNBOUND`，不再增加 `tx_count` 或返回成功。
  - `SYSTem:REFMEM:SYNC:HELLo?/EPOCh?/DELTa?/ACK?/FENCe?/QUALity:FRAMe?` 取消 fake adapter send 调用，只生成 frame hex；真实两板 TX 继续使用 `SYSTem:REFMEM:SYNC:TDMA:*` 或 physical raw bring-up。
  - `test_refmem_pio_spi_adapter.c` 将 send 单测改为断言无 TX 后端必须拒绝。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_pio_spi_adapter_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_hello_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `git diff --check` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815153622`，package CRC `0x6A4E1D33`。
- 后续动作：
  - 完成两板真实同步验收：PC 只对源板下发 SCPI intent，不搬运 frame hex、不直接写对端；目标板通过维护查询确认 NodeLoad / table image / SlotClaim / quality evidence 与源板一致。
  - 继续按风险评审 P0/P1 清单推进 §3.5 fence deadline、§3.6 receive raw capacity、§3.7 arm/capture 状态破坏等结构性纠偏。

### REFMEM-TASK-20260815-041 - Vector fixed regions rename

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 修正“Vector 16 槽”的误导命名：Vector directory 的 16 项是固定内存定义区（region），不是 A0-A7 可实例化节点槽位，也不是 SlotClaim 候选槽。
  - 保持 HAOFV 边界：NodeLoad / SlotClaim / A0-A7 继续使用逻辑 slot 语义；Vector 只提供固定事实 region、owner 边界和 CRC/seq/stale 保护。
- 完成内容：
  - `refmem_vector_table.h/.c` 将 `REFMEM_VECTOR_SLOT_*`、`slot_count`、`slots[]`、vector header/node 类型统一改为 `REFMEM_VECTOR_REGION_*`、`region_count`、`regions[]`、`*_region_t`。
  - `DistributedFbInstanceTable`、`DistributedEventLinkTable`、`DistributedDataLinkTable`、`DistributedDeploymentGate` 中指向 Vector directory 的字段改为 `state_region_ref`、`health_region_ref`、`evidence_region_ref`、`crc_region_ref`、`reject_region_ref`。
  - DataLink 可读路径从 `*Slot.*` 改为 `*Region.*`，Python RMTP 生成工具同步改用 `REGION_*` 常量和 `region_path_hash`。
  - `refmem_quality_evaluate_deployment_gate()` 的输出拆成 `refmem_quality_gate_result_t`，避免 quality runtime 的逻辑 `reject_slot` 与 DeploymentGate 表的 `reject_region_ref` 混用。
  - 架构文档明确：Vector directory 16 项是固定 region；A0-A7 / SlotClaim 才是节点实例化槽位。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815151411`，package CRC `0x6B9F4362`。
- 后续动作：
  - 后续架构评审中继续区分 Vector fixed region、A0-A7 logical slot、SlotClaim assignment 和 RMTP 九表模型，避免把 region 当成可加载节点槽。

### REFMEM-TASK-20260815-040 - Inline LOAD:NODE/BOARD to full RMTP package image

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 完整修复风险评审 §3.11：inline `LOAD:NODE` / `LOAD:BOARD` 不能停在 metadata-only 或单表 staging；单表 draft 必须合并为完整 Node Model Candidate，并生成 9 表 RMTP package image。
  - 保持 HAOFV 边界：SCPI 只提交 intent；ApplicationModel owner 生成 draft/candidate/package；TableRegistry 只管理完整 image lifecycle；Vector 不承载完整表数据。
- 完成内容：
  - 在架构文档中固化两张表：`Table Draft -> Node Model Candidate -> RMTP Package Image` 层级表，以及 `LOAD:SD / LOAD:NODE / LOAD:BOARD` 到最终 package 产物的路径表。
  - `refmem_application_model.c` 增加 inline RMTP serializer：按 9 张 canonical 表生成 header、directory、payload、per-table CRC、payload CRC 和 package CRC。
  - serializer 从当前 active 9 表读取未修改表，并用私有 `NodeLoadTable draft` / `BoardCapabilityTable draft` 覆盖候选表，形成完整 Node Model Candidate。
  - `LOAD:NODE` 成功路径改为调用 `refmem_table_registry_stage_package_image()`，不再把 `staging_package_crc32` 当成 NodeLoad 单表 CRC。
  - `LOAD:BOARD` 成功路径同步刷新系统级 `LOAD:STATus?` 的 inline package staging 状态，同时 `BOARD:STATus?` 保留 BoardCapability draft 整表 CRC。
  - host 单测新增 inline `LOAD:BOARD` 和 `LOAD:NODE` activation-ready package 断言：stage 后可 prepare staging view、activate、commit，并确认 runtime getter 发生预期变化。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815144746`，package CRC `0x8A0A9E09`。
- 后续动作：
  - 继续审查 TableRegistry `stage_table()` 的剩余用途，把产品路径中的单表 staging 约束为失败 evidence 或测试辅助。
  - 重新跑 COM5/COM6 HIL，确认 inline package staging 与 `LOAD:ACTivate` 在板端同样闭环。

### REFMEM-TASK-20260815-039 - LOAD:BOARD private table staging image

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 推进风险评审 §3.11：`SYSTem:REFMEM:LOAD:BOARD` 不能只发布单条 entry CRC 和 metadata-only staging，至少必须形成 RefMemAO 私有 `BoardCapabilityTable` staging image。
  - 保持 HAOFV 边界：SCPI 只提交 board capability load 意图；application model owner 构造候选表、执行 contract validation 并发布 TableRegistry staging 摘要。
- 完成内容：
  - 新增私有 `s_staging_board_capability_table` / `s_staging_board_capability_valid`，与 NodeLoad staging image 对齐。
  - 新增 `refmem_model_make_staging_board_capability_table()`：从当前 active 或已有 staging board table 复制候选表、替换目标 board entry、执行 `refmem_application_contract_validate_board_capability_table()`，并计算整表 CRC。
  - `refmem_application_model_stage_scpi_board_capability()` 不再使用单 entry CRC；合法路径发布 `BoardCapabilityTable` 整表 staging CRC，非法路径继续发布 FAILED。
  - 删除旧 `refmem_model_board_entry_crc32()`，避免继续暗示单 entry staging。
  - `test_refmem_table_registry.c` 增加 `LOAD:BOARD` 断言：registry table 1 staging CRC 等于 board load snapshot 的整表 CRC，且不同于单 entry CRC；active getter 在 staging 阶段不被改变。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815142112`，package CRC `0xE8171F9A`。
- 后续动作：
  - §3.11 仍未完全关闭；下一步需要让 inline NodeLoad/BoardCapability staging 合成完整 package image，或明确声明为不参与 activation 的诊断 staging。

### REFMEM-TASK-20260815-038 - TableRegistry OWNER_OK provenance

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 修复风险评审 §3.10：`OWNER_OK` 不能由 `present + CRC` 推导，必须代表 owner validation provenance。
  - 保持 HAOFV 边界：TableRegistry 只记录 image/validation 证据，不把 CRC 完整性伪装成 AO/FB owner 语义验证。
- 完成内容：
  - `refmem_table_registry_refresh_active()` 中编译内置 active entry 的 flags 从 `ACTIVE_PRESENT|CRC_OK|OWNER_OK` 降级为 `ACTIVE_PRESENT|CRC_OK`。
  - staging package 路径仍按 `owner_validated_table_mask` 逐表置 `OWNER_OK`；activation 后的 active image 可继承已验证 provenance。
  - `test_refmem_table_registry.c` 增加断言：初始化 active entry 有 CRC flag，但 `OWNER_OK` flag 必须为 0。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815141450`，package CRC `0x9A5F9C0D`。

### REFMEM-TASK-20260815-037 - SlotClaimGate explicit ready gating

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 修复风险评审 §3.3：DeploymentGate/SlotClaimGate 调用端必须真正以 gate ready 结果作为放行条件。
  - 保持 HAOFV 边界：RefMemAO 和 SystemManager 只消费 RefMem 派生的门禁事实，不绕过 SlotClaimMap 或 DeploymentGate。
- 完成内容：
  - `refmem_application_model.c` 静态表 validation 改为显式检查 `refmem_slot_claim_gate_evaluate(...) && claim_gate.ready`。
  - `distributed_refmem.c` activation gate 改为显式检查 `evaluate && ready`。
  - `system_manager.c` RUN gate 改为显式检查 `evaluate && ready`。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815141111`，package CRC `0xCE53713F`。

### REFMEM-TASK-20260815-036 - Cross-core seqlock guard release fence

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 修复风险评审 §3.1：跨核 seqlock/guard 写侧普通自增缺少 release fence，读侧 acquire 无法保证读取到完整可见的数据快照。
  - 保持 HAOFV 边界：`refmem_realtime_tdma` 仍由 core0 发布 intent、core1 发布 result；`refmem_command` 仍由 command slot owner 处理 ACK/NACK，不引入跨层业务逻辑。
- 完成内容：
  - `refmem_realtime_tdma.c` 新增 guard release increment helper，intent/result begin/end write 均通过 `__atomic_add_fetch(..., __ATOMIC_RELEASE)` 发布 guard。
  - `refmem_command.c` 新增 command guard release increment helper，command slot begin/end write 均通过 release 语义发布 guard。
  - 扫描 `components/distributed_refmem`，确认无 `guard++` 残留。
- 验证结果：
  - `rg -n "guard\+\+|\+\+.*guard" components\distributed_refmem -S` 无残留匹配。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_contract_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815140755`，package CRC `0x8FC40ACD`。

### REFMEM-TASK-20260815-035 - Staging table view pre-parse activation gate

- 状态：完成 COM5/COM6 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 收掉 active image 已切换但 application runtime snapshot 解析失败的理论窗口。
  - 保持 HAOFV 边界：SCPI 只提交 activation intent；`DistributedRefMemAO` 调度，`RefMemTableRegistry` 管 image，`refmem_application_model` 作为 owner 预解析 staging stable view。
- 完成内容：
  - 新增 `refmem_application_model_prepare_staging_table_views()`，从 staging image 借出 9 张 canonical 表并解析到 pending runtime snapshot。
  - 新增 `refmem_application_model_commit_prepared_table_views()` / `discard_prepared_table_views()`，registry activation 成功后才提交 pending，失败时丢弃。
  - `distributed_refmem_activate_staging()` 在调用 `refmem_table_registry_activate_staging()` 前先执行 staging view 预解析；预解析失败时记录 `STAGING_VIEW_INVALID`，并通过 command slot NACK。
  - 新增 `REFMEM_TABLE_ACTIVATE_ERR_STAGING_VIEW_INVALID` 和 `REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED`，并同步 `SYSTem:COMMand:NACK?` reason table。
  - `test_refmem_table_registry.c` 增加 prepare/commit 断言：prepare 不改变 active getter，commit 后才切到 package runtime snapshot，pending 只能提交一次。
  - `tools/refmem/refmem_table_registry_validate/refmem_table_registry_validate.py` 增加 preflight error queue drain，避免 Storage/OTA 历史错误污染本轮验证；summary 只将 `FAIL` 计入失败。
  - `tools/storage/storage_file_upload/storage_file_upload.py` 增加 StorageAO job idle 等待和 active write transaction abort preflight，避免目录/删除/写入 job 交错。
  - `tools/interface/scpi_query/scpi_query.py` 修正 `INFO? "path"` 这类带参数查询识别，并新增 `tools/interface/scpi_query/refmem_storage_info_probe.scpi` 作为 RefMem HIL SD 依赖探测命令集。
  - `BoardCapabilityTable` 当前产品 wire contract 固定为 8 条 A0-A7 能力记录；`CLAIM_CANDIDATE_MAX = 16` 只保留给 SlotClaim proposal / overflow evidence，不复用为 BoardCapability 表容量。
  - `scpi_refmem_read_package()` 先通过 StorageAO file info 获取真实文件大小，再按剩余长度分块读取，避免最后一块 512 字节越过文件尾导致 `LOAD:SD` 读包失败。
  - `tools/storage/sd_fs_build/sd_fs_build.py` 固化生成 `manifest_refmem_hil.idx`，用于 RefMem HIL，只要求 profile、mission、cal 和 RefMem `.rmtp` 四个小文件。
  - 新增 `tools/interface/scpi_query/refmem_file_read_probe.scpi`，用于通过 Storage `FILE:READ?` 探测 `/refmem/app_model.rmtp` 头尾读取。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python -m py_compile tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `python -m py_compile tools\storage_file_upload\storage_file_upload.py` 通过。
  - `python -m py_compile tools\scpi_query\scpi_query.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `python -m pytest tests\python\test_refmem_pack_build.py -q` 通过，2 passed；pytest cache 写入 `.pytest_cache` 因权限被拒绝产生 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，最终生成 build id `20260815135119`，package CRC `0x1801DEA8`。
  - `python tools\sd_fs_build\sd_fs_build.py --build-dir build-rtos-multicore-smoke --output-dir build-rtos-multicore-smoke\sdcard_8slot_20260815135119 --clean --no-zip --no-reports` 通过，生成 8 槽位 `.rmtp` size `4512`，package CRC `0x31062D02`，BoardCapability table size `296`。
  - COM5 OTA 到 build `20260815135119` 并 boot commit 成功；上传 8 槽位 HIL 文件集合成功。
  - COM5 执行 `refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_8slot_20260815135119\refmem\app_model.rmtp --load-sd --activate` 通过：`LOAD:SD` 返回 `STAGED`，`LOAD:ACTivate` 返回 `ACTIVE`，active image table mask `511`，BoardCapability table size `296`，最终 `SYSTem:ERRor?` 为 `0,"No error"`。
  - COM6 OTA 到 build `20260815135119` 并 boot commit 成功；上传 8 槽位 HIL 文件集合成功。
  - COM6 首次 `LOAD:SD` 在读包阶段返回 `REJECTED`，随后 `refmem_file_read_probe.scpi` 证明 `/refmem/app_model.rmtp` 头尾 Storage read 正常；复跑 `refmem_table_registry_validate.py COM6 --load-sd --activate` 通过，active image table mask `511`，BoardCapability table size `296`，最终 `SYSTem:ERRor?` 为 `0,"No error"`。
- 过程记录：
  - 全量 System Pack manifest 要求 `/update/RP2350_TRIG_UPDATE.pkg`，通用 Storage write API 当前单事务上限 8192 bytes，不能用它上传 523 KB OTA 包；本轮 RefMem HIL 改用 `manifest_refmem_hil.idx`，只要求 profile、mission、cal 和 RefMem `.rmtp` 四个小文件，不绕过 StorageAO manifest gate。
  - COM5 首次覆盖 manifest 后出现一次 `manifest_status=8 / missing_count=1`，单独 `SYSTem:SD:MANifest?` 随后返回 OK；该现象计入后续 StorageAO manifest scan/job 时序观察，不阻塞本轮 RefMem activation gate 结论。
  - COM6 最终 build 首次 `LOAD:SD` 后、Storage read probe 后复跑成功，继续作为 StorageAO job/manifest 首轮时序支线观察项。
- 后续动作：
  - 继续接入真实 owner validation callback 调度和跨节点 ACK/FENCE。

### REFMEM-TASK-20260815-034 - Active table views to application runtime snapshot

- 状态：完成 COM5/COM6 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 让 registry 级 active image activation 后，业务读取路径不再继续固定读编译内置表。
  - 保持 HAOFV 边界：`RefMemTableRegistry` 只管理 active/staging/rollbackable image 和 const table view，`refmem_application_model` 负责把 active view 解析成私有 runtime snapshot。
- 完成内容：
  - 新增 `refmem_application_model_apply_active_table_views()`，从 active image 借出 9 张 canonical 表的 stable view，解析到 application model 私有 runtime tables。
  - 外部 getter 切到 runtime snapshot：ApplicationMap、BoardCapability、GenericNode、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate、ConnectionQuality。
  - `DistributedRefMemAO` 在 `TABLE_PACKAGE_ACTIVATE` 成功后触发 application model apply，并更新 `active_package_crc32` 与 per-table CRC snapshot。
  - `tools\refmem_table_registry_validate.py` 增加 activation 后 `SYSTem:REFMEM:LOAD:STATus?` 检查，确认 application model active package CRC 已跟随 active image。
  - `tools\tests\run_refmem_table_registry_tests.ps1` 链接 application model 源，新增 host 断言覆盖 getter 从 active image runtime snapshot 取 BoardCapability/FBInstance。
- 当前验证：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python -m py_compile tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815124740`，package CRC `0x1E1584A1`。
  - `python -u tools\ota_send\ota_send.py COM5 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python -u tools\ota_boot_commit\ota_boot_commit.py COM5 --expected-build 20260815124740 --out-dir build-rtos-multicore-smoke\ota_commit_COM5_20260815124740_refmem_app_view` 通过。
  - `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815124740_app_view` 通过。
  - `python -u tools\ota_send\ota_send.py COM6 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python -u tools\ota_boot_commit\ota_boot_commit.py COM6 --expected-build 20260815124740 --out-dir build-rtos-multicore-smoke\ota_commit_COM6_20260815124740_refmem_app_view` 通过。
  - `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM6 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM6_20260815124740_app_view` 通过。
- 后续动作：
  - 把 activation 前 staging stable table view 预解析纳入 activation transaction，减少“registry 已切换但 application apply 失败”的理论窗口。
  - 接入真实 owner validation callback 调度和跨节点 ACK/FENCE。

### REFMEM-TASK-20260815-033 - Stable table view access/release

- 状态：完成 COM5/COM6 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 在 registry 级 image activation 后，提供稳定的单表只读 view，避免后续业务 AO/FB 直接拼 RMTP offset 或读取裸 buffer。
  - 保持 HAOFV 边界：TableRegistry 借出 const payload view，向量表和 SCPI 仍只输出摘要，不承载完整 table 数据。
- 完成内容：
  - 新增 `refmem_table_view_t`，包含 role、table id、table seq、package CRC、table CRC、image offset、image size 和 const payload pointer。
  - 新增 `refmem_table_registry_access_table(role, table_id, view)` / `refmem_table_registry_release_table(view)`，通过 RMTP directory 和 table CRC 定位 payload。
  - 增加 reader guard：active/staging/rollbackable 任一 image 存在未 release view 时，activation 返回 `IMAGE_BUSY`，避免表指针悬挂。
  - 新增 `SYSTem:REFMEM:TABle:VIEW? [role],[table_id]`，通过 access/release 读取 view 摘要：`version,role,table_id,table_seq,package_crc32,table_crc32,image_offset,image_size,first_u32`。
  - `tools/refmem/refmem_table_registry_validate.py --activate` 增加 active table 0 和 table 3 view 摘要断言。
- 当前验证：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过，覆盖 active view access/release、double release reject、unreleased view 阻断 activation。
  - `python -m py_compile tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815122901`，package CRC `0xDDB83D74`。
  - `python -u tools\ota_send\ota_send.py COM5 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python -u tools\ota_boot_commit\ota_boot_commit.py COM5 --expected-build 20260815122901 --out-dir build-rtos-multicore-smoke\ota_commit_COM5_20260815122901_refmem_view` 通过。
  - `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815122901_view` 通过。
  - `python -u tools\ota_send\ota_send.py COM6 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python -u tools\ota_boot_commit\ota_boot_commit.py COM6 --expected-build 20260815122901 --out-dir build-rtos-multicore-smoke\ota_commit_COM6_20260815122901_refmem_view` 通过。
  - `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM6 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM6_20260815122901_view` 通过。
  - COM5/COM6 均确认 `LOAD:SD`、`LOAD:ACTivate`、`TABle:IMAGe?`、`TABle:VIEW? 0,0`、`TABle:VIEW? 0,3`、9 张 `TABle?` 和 `SYSTem:ERRor?` 闭环通过。
- 后续动作：
  - 下一步把 active package view 解析为业务结构表 snapshot，并逐步替换继续读取编译内置表的路径。

### REFMEM-TASK-20260815-032 - Table package activation command

- 状态：完成 COM5/COM6 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 让 P0 table image activation 不再只停留在 host API；补齐板端维护入口和固化验证脚本。
  - 保持 HAOFV 边界：SCPI 只提交 activation intent 和读取 descriptor，真正 active/staging/rollbackable 切换由 `DistributedRefMemAO` / `RefMemTableRegistry` owner 完成。
- 完成内容：
  - 新增 `REFMEM_COMMAND_TYPE_TABLE_PACKAGE_ACTIVATE`，activation 入口也进入 command slot。
  - 新增 `distributed_refmem_activate_staging(realtime_idle)`：本地 owner take 后组装 activation gate，检查 RefMem idle、realtime idle、runtime protection、CRC/owner、SlotClaim、quality/DeploymentGate 和 local command take，再调用 `refmem_table_registry_activate_staging()`。
  - 新增 `SYSTem:REFMEM:LOAD:ACTivate`，返回 `ACTIVE/REJECTED`、registry snapshot，以及 active/staging/rollbackable 三个 image descriptor。
  - 新增 `SYSTem:REFMEM:TABle:IMAGe? [role]`，读取 active/staging/rollbackable descriptor：`version,role,state,table_mask,package_crc32,table_seq,path_hash,last_result,evidence_index`。
  - `tools/refmem/refmem_table_registry_validate/refmem_table_registry_validate.py` 增加 `--activate`，固化 `LOAD:SD -> LOAD:ACTivate -> command ACK -> image descriptor -> TABle? 0..8` 验证。
- 验证结果：
  - `python -m py_compile tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815121205`，package CRC `0xFE13F612`。
  - COM5 OTA 到 build `20260815121205` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM6 OTA 到 build `20260815121205` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM5 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815121205_activate` 通过。
  - COM6 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM6 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM6_20260815121205_activate` 通过。
- 结论：
  - P0 已具备板端可触发、可查询、可脚本验证的 registry 级 table package activation 闭环。
  - 本轮是首轮 activation，旧 active 来自编译内置表、没有 package bytes，因此 rollbackable descriptor 为空；后续需增加二次 activation 回滚验证、active package 到业务 stable table view 的解析、owner access/release，以及跨节点 ACK/FENCE。

### REFMEM-TASK-20260815-031 - Registry image lifecycle buffers

- 状态：完成 COM5/COM6 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `RefMemTableRegistry` 从 descriptor/CRC staging 推进到 registry 级真实 package image 生命周期。
  - 保持 HAOFV 边界：完整 `.rmtp` bytes 只进入 `DistributedRefMemAO` / `RefMemTableRegistry` 私有 buffer，向量表和 SCPI 查询只暴露 descriptor、CRC、state、seq 和 evidence 摘要。
- 完成内容：
  - `RefMemTableRegistry` 新增 active/staging/rollbackable 三组静态 image buffer 和 size，单包上限 `REFMEM_TABLE_IMAGE_BUFFER_SIZE=8192`。
  - 新增 `refmem_table_registry_stage_package_image()`，在 package/header/table CRC 与 owner validation summary 通过后复制完整 package bytes 到 staging image。
  - `refmem_table_registry_activate_staging()` 已能在 gate 通过且 staging bytes 存在时执行 registry 级切换：旧 active 进入 rollbackable，staging 进入 active，per-table active CRC 从 staging CRC 更新，staging descriptor/payload 清空。
  - metadata-only staging、失败 staging 或空 package staging 会清空 staging payload；activation 继续返回 `IMAGE_NOT_LOADED`，避免复用上一轮 package bytes 形成伪 active。
  - `SYSTem:REFMEM:LOAD:SD` 现在把 StorageAO 读取到的 package bytes 随 package validation summary 一起交给 `DistributedRefMemAO`，SCPI 仍不直接写 TableRegistry 或 active fact。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过；新增 stale payload 回归断言。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815115843`，package CRC `0x15FE50D0`。
  - COM5 OTA 到 build `20260815115843` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM6 OTA 到 build `20260815115843` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM5 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815115843_image_lifecycle` 通过。
  - COM6 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM6 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM6_20260815115843_image_lifecycle` 通过。
  - COM5/COM6 均执行 `tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py --skip-sd` 通过，确认 `LOAD:NODE`、`LOAD:BOARD` 和 command ACK/NACK 路径未被 staging payload 生命周期改动破坏。
- 结论：
  - P0 的 registry 级真实 active/staging/rollbackable image buffer 切换已经落地；向量表仍只承载摘要，符合 HAOFV。
  - activation 后 active package 到业务 stable table view 的解析、owner access/release、真实 owner validation callback 调度和跨节点 activation ACK/FENCE 仍是后续 P0/P2/P3 工作。

### REFMEM-TASK-20260815-030 - NodeLoadTable staging image

- 状态：完成 COM5 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `SYSTem:REFMEM:LOAD:NODE` 从单条候选状态快照推进为私有 staging `DistributedNodeLoadTable` image。
  - 保持 HAOFV 边界：SCPI 只 post `NODE_LOAD_STAGE` command，`DistributedRefMemAO` owner take 后更新 staging 表镜像、执行 validation、ACK/NACK，并只向 TableRegistry 发布摘要。
- 完成内容：
  - 新增 `s_staging_node_load_table` 和 `s_staging_node_load_valid`，`LOAD:NODE` 成功后在私有 staging 表上累积候选。
  - `LOAD:NODE` 现在按候选 `NodeLoadTable` 调用 `refmem_application_contract_validate_node_load_table()`，并计算整表 staging CRC。
  - 成功路径只更新 `RefMemTableRegistry` table 3 `NodeLoadTable` 的 staging CRC、`OWNER_OK` 状态和 flags；不再通过整包 staging refresh 把 9 张表都标成 staging。
  - 非法 node/instance 仍返回 `REJECTED` / command NACK，并把 table 3 标成 `FAILED`，不污染 active 表。
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 同步断言：合法 `LOAD:NODE` 必须有非零 NodeLoadTable staging CRC，`SYSTem:REFMEM:TABle? 3` 的 staging mask 至少包含 bit3。
- 验证结果：
  - `python -m py_compile tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815114032`，package CRC `0xF7BB88ED`。
  - COM5 OTA 到 build `20260815114032` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM5 执行 `python -u tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM5 --skip-sd --timeout 10 --out-dir build-rtos-multicore-smoke\refmem_load_COM5_20260815114032_nodeload_staging` 通过；合法 `LOAD:NODE 5,9,32,32,1,0,0` 生成 table 3 staging CRC `3388599922`，`SYSTem:REFMEM:TABle? 3` 返回 `staging_table_mask=8`。
  - COM5 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815114032` 通过，确认 `LOAD:SD` 全 9 表 staging CRC 未被破坏。
- 结论：
  - `LOAD:NODE` 已进入 P0 表镜像主线，不再只是单字段 snapshot。
  - active/staging/rollbackable 全表 image buffer、activation gate、abort/rollback 仍是 P0 下一步。
- 后续动作：
  - 按 P0-P3 优先级推进：P0 真实 table image activation/rollback；P1 SlotClaimMap 协调和溢出验证；P2 RefMemSlotContract 字段级 owner/snapshot；P3 Command/ACK/NACK 与 activation gate 统一。

### REFMEM-TASK-20260815-029 - TableRegistry per-table staging CRC gate

- 状态：完成 COM5 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 将 RefMem 主线从 `LOAD:SD => STAGED` 字符串检查推进到 TableRegistry per-table 证据检查。
  - 验证 `.rmtp` package directory 中 9 张 canonical 表的 CRC 是否逐表进入 `SYSTem:REFMEM:TABle?` staging entry。
  - 保持 HAOFV 边界：SCPI 只执行 `LOAD:SD` 意图和读取 snapshot，不直接写 active fact 或 TableRegistry。
- 发现问题：
  - COM5 旧 build 执行 `SYSTem:REFMEM:LOAD:SD` 后返回 `STAGED`，但 `SYSTem:REFMEM:TABle? 0` 的 `staging_crc32` 仍为整包 CRC `0xB27CF840`，不是 `ApplicationMap` 单表 CRC `0x60E14FD0`。
  - 根因是 `DistributedRefMemAO` 将 package parser summary 交给 `RefMemTableRegistry` 时漏填 `validation.table_mask`，导致 `refmem_table_registry_stage_package_validation()` 拒绝 per-table validation summary，并退回旧的整包 CRC staging refresh。
- 完成内容：
  - `distributed_refmem_stage_sd_system_pack()` 补齐 `validation.table_mask = 0x1FF`，让 RefMemAO 向 TableRegistry 提交完整 9 表 package validation 事实。
  - 新增 `tools/refmem/refmem_table_registry_validate/refmem_table_registry_validate.py`，读取本地 RMTP header/directory，校验 package/payload/table CRC，然后通过单次串口生命周期查询：
    - `SYSTem:REFMEM:LOAD:SD`
    - `SYSTem:COMMand:ACK?`
    - `SYSTem:REFMEM:LOAD:STATus?`
    - `SYSTem:REFMEM:TABle? 0..8`
    - `SYSTem:ERRor?`
  - 脚本断言 `staging_table_mask=0x1FF`、每表 `staging_crc32` 等于 RMTP directory CRC、`validation_state=OWNER_OK`、`STAGING_PRESENT|CRC_OK|OWNER_OK` flags 齐全。
- 验证结果：
  - `python -m py_compile tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815113037`，package CRC `0xFF54EA0C`。
  - COM5 OTA 到 build `20260815113037` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM5 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815113037_r2` 通过。
  - 验证通过的 9 表 staging CRC：
    - ApplicationMap `0x60E14FD0`
    - BoardCapability `0xCEFC3615`
    - GenericNode `0xA1681310`
    - NodeLoad `0x7D7762D1`
    - FbInstance `0x1CC15A37`
    - EventLink `0x5A4E36C8`
    - DataLink `0xD7E24830`
    - DeploymentGate `0x31FDBA68`
    - ConnectionQuality `0xA67C64D1`
- 结论：
  - `LOAD:SD` 正向路径现在不只证明 package 能解析为 `STAGED`，还证明 TableRegistry 的 per-table staging 证据与 RMTP directory 一致。
  - 真实 active/staging/rollbackable image buffer 仍未落地，activation 仍应保持 `IMAGE_NOT_LOADED` 阻断。
- 后续动作：
  - 继续 RefMem 主线 P0：实现真实 active/staging/rollbackable table image 切换和 owner validation callback 调度。
  - 继续 P7：补无 SD、manifest 缺失、CRC 正确但 owner validation 失败、activation 失败回滚等验证。

### REFMEM-TASK-20260815-028 - Optimize LOAD:SD bounded read chunk

- 状态：完成 COM5 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 在不破坏 HAOFV 边界的前提下优化 `SYSTem:REFMEM:LOAD:SD` 耗时。
  - 保持 SCPI 只发起 load 意图和输出 snapshot，SD/FatFs 仍归 StorageAO，RefMem staging 仍归 DistributedRefMemAO。
- 完成内容：
  - `STORAGE_MANAGER_FILE_READ_MAX_BYTES` 从 128B 提高到 512B，扩大 StorageAO 内部 bounded read job 缓冲。
  - `SCPI_REFMEM_PACKAGE_READ_CHUNK` 从 128B 提高到 512B，让 `LOAD:SD` 读取 4800B RMTP 时从约 38 个 read job 降到约 10 个 read job。
  - 公开 `SYSTem:STORage:FILE:READ?` 的 `SCPI_STORAGE_MMEM_READ_BYTES_MAX` 仍保持 128B，避免普通 SCPI 文件读取响应突然变长。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815110412`，package CRC `0x81CE4C7E`。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - COM5 已通过 OTA/commit 升级到 build `20260815110412`。
  - COM5 写入当前 HIL manifest、profile、mission、cal 和 `/refmem/app_model.rmtp` 后，`SYSTem:REFMEM:LOAD:SD` 单次加载加后续状态查询总耗时约 `6.944 s`；返回 `STAGED`，Storage 最后一块 `FILE_READ` 为 192B，错误队列为 0。
  - COM5 执行 `python -u tools\refmem_pack_write\refmem_pack_write.py COM5 --timeout 10 --load-timeout 30 --chunk 256 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --out-dir build-rtos-multicore-smoke\refmem_pack_write_COM5_20260815110412_512b_load` 通过。
- 结论：
  - `LOAD:SD` 首轮优化有效，耗时从约 37 秒降到 7 秒量级。
  - 无参 `SYSTem:REFMEM:LOAD:SD` 在本轮干净串行测试中未复现 `Missing parameter`；此前污染来自命令行引号错误和同一 COM 口并发访问造成的响应串扰。
- 后续动作：
  - 当前 512B bounded read 路径作为主线可用基线保留；继续压缩到 1 秒级的 bounded stream read job 先转入支线优化，不阻塞后续 RefMem 主线闭环。

### REFMEM-TASK-20260815-027 - StorageAO/RefMem LOAD:SD HIL deep dive

- 状态：完成 COM6 板端闭环；COM5 已在后续任务中恢复并重刷
- 日期：2026-08-15
- 任务目标：
  - 深挖 `/refmem/app_model.rmtp` 通过 Storage SCPI 写入时反复撞到 `RUNNING`、最终 `RENAME_FAILED` 的原因。
  - 判断是否由最小系统板 RefMem PIO-SPI 与 SD 共享 SPI 引起，并给出结构性修复。
- 结论：
  - 当前板级配置中 SD 与 LCD 共用 `BOARD_SPI_PORT`，实际为 `spi1` 的 SCK/MOSI/MISO 总线；RefMem 最小 transport 使用 PIO-SPI GPIO16-19，不是 SD 使用的硬件 SPI 总线。
  - `RUNNING` 的主要原因不是 RefMem PIO-SPI 与 SD 物理共线，而是 StorageAO 服务优先级、boot snapshot 插队和 UI 刷屏共享 SPI 资源叠加。
  - `RENAME_FAILED` 发生在 CRC 已匹配后的临时文件替换阶段，说明 SCPI 分块上传和内存缓冲完整，失败集中在 FatFs rename/replace 语义或目标文件残留。
  - `SYSTem:REFMEM:LOAD:SD` 的挂死风险来自 SCPI 回调栈上 8 KB package buffer；该缓冲已迁移为模块静态加载缓冲，避免命令执行时压垮 SCPI task stack。
  - `SYSTem:REFMEM:LOAD:SD` 返回固定 IO_ERROR/REJECTED 的根因是 RefMem load 只等待 StorageAO job 约 200 ms；真实 manifest/package 分段读取会超过该窗口，导致 `RUNNING` 被误判为失败。等待窗口已与 Storage SCPI 统一到 10000 loops。
  - UI 不是旁路代码：`UiAO`/`LcdFb` 纳入 HAOFV 资源治理，UI 只读取 snapshot 并通过 ResourceArbiter 获取 `SPI0|LCD`，StorageAO 获取 `SPI0|SD`，板级 port 负责共享 SPI 模式和 CS 准备。
- 完成内容：
  - `storage_manager_service()` 改为显式 Storage job 优先于 boot snapshot/system pack 自动动作，避免 SCPI 已投递 job 后被启动快照插队。
  - FreeRTOS 任务优先级调整：`storage` 从 2 提到 3，`ui` 从 2 降到 1，避免 UI 与 StorageAO 在 `SPI0|LCD/SD` 资源上同级频繁碰撞。
  - `ui_manager_service()` 在 StorageAO job 处于 `QUEUED/RUNNING` 时跳过本轮刷新，让 SD 文件操作优先完成。
  - `StatusUI` 对 `SPI0|LCD` 使用 owner 标记 `StatusUI`，后续 ResourceArbiter 快照可直接看到冲突 holder。
  - `fatfs_port_write_text_file_atomic()` 与 `fatfs_port_write_binary_file_atomic()` 收敛到公共 bytes 写入 helper；临时文件 rename 失败后增加直写目标文件兜底，避免 FatFs 覆盖式 rename 或旧文件残留导致 StorageAO 卡死。
  - `SCPI_STORAGE_JOB_WAIT_LOOPS` 从 2000 增加到 10000；`tools/refmem/refmem_pack_write/refmem_pack_write.py` 默认 timeout 从 3 s 增加到 10 s，防止设备端仍在合法写入时 PC 侧先超时。
  - `SCPI_REFMEM_LOAD_JOB_WAIT_LOOPS` 从 200 增加到 10000；`tools/refmem/refmem_pack_write/refmem_pack_write.py` 增加 `--load-timeout`，默认 60 s，专门覆盖 `LOAD:SD` 的长耗时 staged 路径。
  - 新增 `tools/interface/scpi_common/scpi_serial.py`，固化 USB CDC 串口打开、settle、输入/输出缓冲清理、idle-gap 读行和 finally close 生命周期管理；`scpi_query`、`storage_scpi_validate`、`refmem_pack_write` 已接入。
  - 新增 `tools/storage/storage_file_upload/storage_file_upload.py`，作为通用 Storage SCPI 文件上传工具，支持多文件、自动建目录、分片写入、写后 `FILE:INFO?` 校验。
- 验证结果：
  - `python -m py_compile tools\scpi_common\scpi_serial.py tools\storage_file_upload\storage_file_upload.py tools\refmem_pack_write\refmem_pack_write.py tools\scpi_query\scpi_query.py tools\storage_scpi_validate\storage_scpi_validate.py` 通过。
  - `python -m pytest tests\python\test_refmem_pack_build.py -q` 通过，2 passed；pytest cache 写入 `.pytest_cache` 因权限被拒绝，只产生 warning。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，最终验证 build id `20260815104540`，package CRC `0x1995734A`。
  - OTA/commit COM6 到 build `20260815104540` 通过。
  - COM6 通用 Storage CRUD 使用唯一目录重跑通过；先前 COM5 directory rename 失败确认为历史目标目录残留，不是 `RUNNING`。
  - COM6 写入 HIL manifest（只要求 profile/mission/cal/refmem 四个小文件）后，`SYSTem:SD:MANifest?` 返回 `"OK",1,"RP2350_TRIG","rp2350_trig","20260815103459",4,0`。
  - COM6 执行 `python -u tools\refmem_pack_write\refmem_pack_write.py COM6 --timeout 10 --load-timeout 60 --chunk 256 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815103459\refmem\app_model.rmtp --out-dir build-rtos-multicore-smoke\refmem_pack_write_COM6_20260815104540_load_wait_fixed` 通过；`SYSTem:REFMEM:LOAD:SD` 返回 `STAGED`，staging package CRC `2994534464`，Storage job `DONE`，错误队列为 0。
  - 发现通用 Storage FILE 写事务仍是 RAM buffer 型，`STORAGE_MANAGER_WRITE_BUFFER_MAX_BYTES=8192`，不能用该接口直接上传 523 KB OTA package；大文件 OTA 仍应走 OTA AO/portable OTA 流式入口，Storage 文件接口后续若要支持大文件需设计流式后端。
- 后续动作：
  - 将 `LOAD:SD` 37 s 耗时继续优化：优先减少逐 128 B 文件读 job 的重复调度，或在 StorageAO 提供 bounded stream read job。
  - 继续确认无参 `SYSTem:REFMEM:LOAD:SD` 的错误队列行为，区分真实 parser 污染和 PC 侧命令串扰。
- 关联文件：
  - `application/src/app_tasks.c`
  - `components/storage_manager/src/storage_manager.c`
  - `components/ui_manager/src/ui_manager.c`
  - `components/ui_manager/src/status_ui.c`
  - `middleware/fatfs_port/src/fatfs_port.c`
  - `middleware/scpi_port/src/scpi_storage_commands.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/interface/scpi_common/scpi_serial.py`
  - `tools/refmem/refmem_pack_write/refmem_pack_write.py`
  - `tools/storage/storage_file_upload/storage_file_upload.py`

### REFMEM-TASK-20260815-026 - Complete RMTP canonical table payloads

- 状态：完成代码侧基础闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `.rmtp` 中剩余 5 张 canonical 表从 64 字节占位 payload 升级为真实固定 u32 wire payload。
  - 让 `RefMemTableRegistry` 对全 9 张表执行 owner validation，同时保持 active image 切换阻断，不伪激活。
- 完成内容：
  - `tools/refmem/refmem_table_image/refmem_table_image.py` 新增 `FbInstance`、`EventLink`、`DataLink`、`DeploymentGate`、`ConnectionQuality` payload 生成。
  - `FbInstance` 的 `instance_name` 和 `DataLink` 的 `slot_path` 不写入 C 指针，`.rmtp` 使用 `name_hash` / `slot_path_hash` 作为稳定 wire 字段；可读说明仍放在 idx/json。
  - `refmem_table_registry.c` 的 table image size 改为 wire size，避免把 C struct pointer size 当作 package layout。
  - `RefMemTableRegistry` 增加 4-8 号表 parser 和字段级 owner validation，成功 package 的 `owner_validated_table_mask` 覆盖 `REFMEM_APP_TABLE_MASK_ALL`。
  - 单元测试构造器从“前 4 张真实 + 后 5 张 placeholder”升级为全 9 张 contract package；staging descriptor 预期从 `CRC_OK` 更新为 `OWNER_OK`。
- 验证结果：
  - `python -m py_compile tools\refmem_table_image\refmem_table_image.py tools\refmem_pack_build\refmem_pack_build.py tools\sd_fs_build\sd_fs_build.py` 通过。
  - `python -m pytest tests\python\test_refmem_pack_build.py -q` 通过，2 passed；pytest cache 写入 `.pytest_cache` 因权限被拒绝，只产生 warning。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `python tools\refmem_pack_build\refmem_pack_build.py --output-dir build-rtos-multicore-smoke\refmem_pack_full_tables` 生成 9 表 package，size `4800`，CRC `0xEFAF178F`。
- 后续闭环：
  - 继续运行完整 host/unit/docs/build/HIL 验证；通过后再进入真实 staging/active/rollbackable image buffer。
- 关联文件：
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tools/refmem/refmem_table_image/refmem_table_image.py`
  - `tools/refmem/refmem_pack_build/refmem_pack_build.py`
  - `tests/unit/test_refmem_table_registry.c`
  - `tests/python/test_refmem_pack_build.py`

### REFMEM-TASK-20260815-025 - Block descriptor-only table activation

- 状态：完成基础纠偏
- 日期：2026-08-15
- 任务目标：
  - 消除 `RefMemTableRegistry` 只切 descriptor/CRC 就宣称 active image 已替换的架构风险。
  - 在真实 active/staging/rollbackable table buffer 未落地前，明确阻断 activation。
- 完成内容：
  - 新增 `REFMEM_TABLE_ACTIVATE_ERR_IMAGE_NOT_LOADED`。
  - `refmem_table_registry_activate_staging()` 在 validated staging 和 activation gate 都通过后，仍返回 false，保留 staging descriptor，并写入 `IMAGE_NOT_LOADED`；不修改 active descriptor、rollbackable descriptor 或 active entry CRC。
  - 单元测试从“成功切 descriptor”改为验证“真实 table image 未加载时拒绝伪激活”。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815085709`，package CRC `0x1C61EB50`。
  - COM5 执行 `python tools\ota_boot_commit\ota_boot_commit.py COM5 --expected-build 20260815085709 --out-dir build-rtos-multicore-smoke\ota_commit_COM5_20260815085709` 通过，`SYSTem:FW:BUILD?` 返回 `20260815085709`，`SYSTem:OTA:COMMit` 返回 `"OK"`，错误队列为 `0,"No error"`。
  - COM6 执行 `python tools\ota_boot_commit\ota_boot_commit.py COM6 --expected-build 20260815085709 --out-dir build-rtos-multicore-smoke\ota_commit_COM6_20260815085709` 通过，`SYSTem:FW:BUILD?` 返回 `20260815085709`，`SYSTem:OTA:COMMit` 返回 `"OK"`，错误队列为 `0,"No error"`。
- 后续闭环：
  - 后续必须引入真实 staging/active/rollbackable table image buffer 和业务表切换后，才能恢复 activation 成功路径。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_table_registry.h`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tests/unit/test_refmem_table_registry.c`

### REFMEM-TASK-20260815-024 - LOAD:SD via RefMem command slot

- 状态：完成首版闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `SYSTem:REFMEM:LOAD:SD` 的 RefMem staging 和 TableRegistry package validation 收敛到 `DistributedRefMemAO` command slot。
  - 保持 StorageAO/RefMemAO 职责边界：SD/FatFs/manifest/file read 属于 StorageAO，RefMem staging/ACK/NACK 属于 RefMemAO。
- 完成内容：
  - 新增 `REFMEM_COMMAND_TYPE_TABLE_PACKAGE_STAGE` 和 `distributed_refmem_stage_sd_system_pack()`。
  - `SYSTem:REFMEM:LOAD:SD` 改为先通过 StorageManager 获取 manifest/package validation 摘要，再调用 RefMem intent API；SCPI 不再直接调用 `refmem_application_model_stage_sd_system_pack()` 或 `refmem_table_registry_stage_package_validation()`。
  - Storage 前置 job busy/incomplete 也转换为 `REJECTED` load snapshot 和 command NACK，避免写命令无 completion。
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 增加 `LOAD:SD` 后的 `SYSTem:COMMand:ACK?` 验证，确认 command type 为 `TABLE_PACKAGE_STAGE`。
- 验证结果：
  - `python -m py_compile tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py` 通过。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815084755`，package CRC `0xE5D9FCC9`。
  - COM5/COM6 OTA 写入、boot 和 commit 到 build `20260815084755` 通过，错误队列均为 `0,"No error"`。
  - COM5 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM5 --out-dir build-rtos-multicore-smoke\refmem_load_COM5_20260815084755_sd_command_slot` 通过；当前 SD package 未有效 staging，`LOAD:SD` 返回 `REJECTED`，`SYSTem:COMMand:ACK?` 返回 `TABLE_PACKAGE_STAGE` NACK。
  - COM6 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM6 --out-dir build-rtos-multicore-smoke\refmem_load_COM6_20260815084755_sd_command_slot` 通过；结果同 COM5。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
- 后续闭环：
  - 继续扩展 SD HIL：覆盖无 SD、manifest 缺失、manifest OK 且 package valid 三类路径，并校验 TableRegistry per-table staging CRC。
  - `LOAD:SD` 仍不执行 active image 切换；P0 仍需实现真实 active/staging/rollbackable table image。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_command.h`
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py`

### REFMEM-TASK-20260815-023 - LOAD:BOARD via RefMem command slot

- 状态：完成首版闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `SYSTem:REFMEM:LOAD:BOARD` 从 SCPI 直接调用 BoardCapability staging API 收敛到 `DistributedRefMemAO` command slot / staging 路径。
  - 验证 `board_id` 只作为板卡/profile 候选 payload，不被误用为 A0-A7 target slot。
- 完成内容：
  - 新增 `REFMEM_COMMAND_TYPE_BOARD_CAPABILITY_STAGE` 和 `distributed_refmem_stage_board_capability()`。
  - `SYSTem:REFMEM:LOAD:BOARD` 改为调用 RefMem intent API；`DistributedRefMemAO` 原子清理已完成 command、post `BOARD_CAPABILITY_STAGE`、由本地 RefMem owner take、调用 `refmem_application_model_stage_scpi_board_capability()`，随后 ACK/NACK。
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 增加 BoardCapability 正向/负向验证：有效 board 候选应 ACK，非法 `board_id=16` 应 NACK。
  - 修正验证脚本参数格式：当前 SCPI 数字解析不接受 `0x...`，脚本改为十进制参数，避免把 parser `-101 Invalid character` 误判为 RefMem 卡死。
- 验证结果：
  - `python -m py_compile tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py` 通过。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815082900`，package CRC `0xF913F583`。
  - COM5/COM6 OTA 写入、boot 和 commit 到 build `20260815082900` 通过，错误队列均为 `0,"No error"`。
  - COM5 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM5 --skip-sd --out-dir build-rtos-multicore-smoke\refmem_load_COM5_20260815082900_board_command_slot_r3` 通过。
  - COM6 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM6 --skip-sd --out-dir build-rtos-multicore-smoke\refmem_load_COM6_20260815082900_board_command_slot_r3` 通过。
- 后续闭环：
  - `LOAD:BOARD` 仍是单条 staging snapshot，不是完整 BoardCapabilityTable active/staging/rollbackable image；P0 仍需升级为真实表镜像。
  - `SYSTem:REFMEM:LOAD:SD` 仍需接入 command slot，并保持 StorageAO/RefMemAO 职责分离。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_command.h`
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py`

### REFMEM-TASK-20260815-022 - LOAD:NODE via RefMem command slot

- 状态：完成首版闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `SYSTem:REFMEM:LOAD:NODE` 从 SCPI 直接调用 application staging API 收敛到 `DistributedRefMemAO` command slot / staging 路径。
  - 验证 `SYSTem:COMMand:ACK?` 能直接观察 `LOAD:NODE` 的 `NODE_LOAD_STAGE` completion。
- 完成内容：
  - 新增 `distributed_refmem_stage_node_load()`，由 RefMemAO 原子清理已完成 command、post `NODE_LOAD_STAGE`、take target slot、调用 `refmem_application_model_stage_scpi_node_config()`，随后 ACK/NACK。
  - 修复 command slot 抢占竞态：`clear completed command -> allocate seq -> post new command` 现在在 RefMemAO 内部同一个 critical section 完成，避免 `system_manager` 在间隙重新发布 `CONFIG_ACTIVATE`。
  - `SYSTem:REFMEM:LOAD:NODE` 改为调用 RefMem intent API；返回字段保持原 `STAGED/REJECTED + LOAD:STATus` 格式。
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 增加 `SYSTem:COMMand:ACK?` 校验，并把 table count / active mask 更新为当前 9 张表 / `0x1FF`。
- 验证结果：
  - `python -m py_compile tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py` 通过。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815081416`，package CRC `0x5D6D4C16`。
  - COM5/COM6 OTA 写入、boot 和 commit 到 build `20260815081416` 通过，错误队列均为 `0,"No error"`。
  - COM5 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM5 --skip-sd --out-dir build-rtos-multicore-smoke\refmem_load_node_COM5_20260815081416_command_slot` 通过。
  - COM6 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM6 --skip-sd --out-dir build-rtos-multicore-smoke\refmem_load_node_COM6_20260815081416_command_slot` 通过。
  - COM5/COM6 再次执行 `tools\model_turntable_load_validate\model_turntable_load_validate.py` 通过，确认共享 post helper 未破坏 `CONFigure:MODEl:TURNtable:LOAD`。
- 后续闭环：
  - `LOAD:NODE` 仍是单条 staging snapshot，不是完整 NodeLoadTable active/staging/rollbackable image；P0 仍需升级为真实表镜像。
  - `SYSTem:REFMEM:LOAD:SD` 和 `SYSTem:REFMEM:LOAD:BOARD` 仍需接入 command slot。
- 关联文件：
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py`

### REFMEM-TASK-20260815-021 - Generic command ACK/NACK SCPI view

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 增加通用 command slot 维护视图，避免继续把 `SYSTem:CONFigure:ACK?` 当成所有 command 的查询入口。
  - 保持单一 ACK/NACK 事实源：新接口直接读取 `DistributedRefMemAO` 的 `AckCommandSlot` snapshot，不另建状态。
- 完成内容：
  - 新增 `SYSTem:COMMand:ACK?`，返回 schema version、state、command seq、source/target、command type/class、payload 摘要、taken/ACK/NACK/busy/timeout flags、last reason、evidence、clear seq 和 last completed seq。
  - 新增 `SYSTem:COMMand:NACK? [reason_id]`，默认读取当前 command snapshot 的 `last_reason`，也可按 reason id 查询 RefMem command reason 表。
  - `SYSTem:CONFigure:ACK? / NACK?` 继续只作为 `CONFIG_ACTIVATE` 的配置门禁兼容视图。
  - `tools/trigger/model_turntable_load_validate/model_turntable_load_validate.py` 增加 `SYSTem:COMMand:ACK?` 和 `SYSTem:COMMand:NACK?` 检查，验证 `NODE_LOAD_STAGE` command 的 ACK 事实。
- 验证结果：
  - `python -m py_compile tools\model_turntable_load_validate\model_turntable_load_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `python tools\docs_check\docs_check.py` 通过。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815080110`，package CRC `0x093AC676`。
  - COM5/COM6 OTA 写入、boot 和 commit 到 build `20260815080110` 通过，错误队列均为 `0,"No error"`。
  - COM5 执行 `python tools\model_turntable_load_validate\model_turntable_load_validate.py COM5 --expected-build 20260815080110 --slot 1 --output 0 --out-dir build-rtos-multicore-smoke\model_turntable_load_COM5_20260815080110_command_ack` 通过。
  - COM6 执行 `python tools\model_turntable_load_validate\model_turntable_load_validate.py COM6 --expected-build 20260815080110 --slot 1 --output 0 --out-dir build-rtos-multicore-smoke\model_turntable_load_COM6_20260815080110_command_ack` 通过。
- 后续闭环：
  - 后续 START/STOP、activation、resource job 接入 command slot 后，统一用 `SYSTem:COMMand:*` 验证 command completion；配置上位机仍可读取 `SYSTem:CONFigure:*` 兼容视图。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/trigger/model_turntable_load_validate/model_turntable_load_validate.py`

### REFMEM-TASK-20260815-020 - ModelTurntable LOAD via RefMem command slot

- 状态：完成首版闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `CONFigure:MODEl:TURNtable:LOAD <slot_id>,<output_index>` 从 SCPI 直接业务调用收敛到 RefMem command slot / NodeLoad staging 路径。
  - 保持 HAOFV 边界：SCPI 只提交 intent；`DistributedRefMemAO` 负责 command、staging 和 ACK/NACK；实际加载由 `ModelTurntableAO` registered owner 执行。
- 完成内容：
  - 新增 `distributed_refmem_register_node_load_owner()`，按 `instance_id` 注册 AO/FB owner 回调；`distributed_refmem.c` 不再直接 include `model_turntable.h`。
  - `ModelTurntableAO` 在初始化时注册 `Template.ModelTurntableAO` 的 NodeLoad owner，owner 回调内部执行 `model_turntable_load()`。
  - `CONFigure:MODEl:TURNtable:LOAD` 改为调用 `distributed_refmem_stage_model_turntable_load()`；该入口 post `NODE_LOAD_STAGE` command、写入 NodeLoad staging snapshot、由 owner 执行加载，并 ACK/NACK command slot。
  - `system_manager` 的 `SYSTem:CONFigure:ACK?` 兼容视图只读取 `CONFIG_ACTIVATE` command，避免模型 LOAD command 污染配置门禁 ACK。
  - 新增 `tools/trigger/model_turntable_load_validate/model_turntable_load_validate.py`，固化 build id、默认未加载、LOAD、staging snapshot、配置 ACK 兼容视图和错误队列检查。
- 验证结果：
  - `python -m py_compile tools\model_turntable_load_validate\model_turntable_load_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815075135`，package CRC `0x3F00DD7D`。
  - COM5/COM6 OTA 写入、boot 和 commit 到 build `20260815075135` 通过，错误队列均为 `0,"No error"`。
  - COM5 执行 `python tools\model_turntable_load_validate\model_turntable_load_validate.py COM5 --expected-build 20260815075135 --slot 1 --output 0 --out-dir build-rtos-multicore-smoke\model_turntable_load_COM5_20260815075135` 通过。
  - COM6 执行 `python tools\model_turntable_load_validate\model_turntable_load_validate.py COM6 --expected-build 20260815075135 --slot 1 --output 0 --out-dir build-rtos-multicore-smoke\model_turntable_load_COM6_20260815075135` 通过。
- 后续闭环：
  - 当前仍是首版 staging snapshot，不是完整 NodeLoadTable active/staging/rollbackable image；P0 仍需把 `CONFigure:MODEl:TURNtable:LOAD` 升级为真实 table image、owner validation、SlotClaimMap、RealtimeCapabilityContract、DeploymentGate 和 rollback。
  - 将 `ModelVnaAO`、`LinkSwitcherAO` 等其余可加载实例接入同一 NodeLoad owner 注册和 command slot 路径。
- 关联文件：
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/model_turntable/src/model_turntable.c`
  - `components/system_manager/src/system_manager.c`
  - `middleware/scpi_port/src/scpi_model_commands.c`
  - `tools/trigger/model_turntable_load_validate/model_turntable_load_validate.py`

### REFMEM-TASK-20260815-019 - SystemManager config ACK maps to RefMem command slot

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 消除 `SYSTem:CONFigure:ACK?` 由 SystemManager 本地字段单独拼装造成的 ACK/NACK 事实分裂。
  - 保持 SCPI 兼容查询不变，同时让底层 ACK/NACK 来自 RefMem `AckCommandSlot` snapshot。
- 完成内容：
  - `DistributedRefMemAO` 增加 command slot owner API：reason table CRC、post、ACK、NACK、timeout、clear 和 snapshot。
  - `system_manager` 在配置 RUN gate 初始化和 service 中发布 `CONFIG_ACTIVATE` command snapshot，并按 gate 结果写入 ACK 或 NACK。
  - 修正 quality gate 变坏时 ACK/NACK flags 可能仍停留在旧全 ACK 的问题；现在 reject 时 `SYSTem:CONFigure:ACK?` 返回 `ack=0,nack=target`。
  - `tools/refmem/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py` 增加 config ACK 正向、负向和恢复后的全 ACK 检查。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过 host GCC 断言执行。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815061144`，package CRC `0xAB77E7E3`。
  - COM5/COM6 OTA 到 build `20260815061144` 并 commit 通过。
  - COM5/COM6 执行 `multicore_board_validate.py --tests build_id config_gate_status ack_reason_and_run_policy` 均通过。
  - COM5 执行 `refmem_quality_gate_hil_validate.py` 通过，确认 TDMA timeout 后 config gate reject、config ACK reject，并通过 OTA 恢复 ready / full ACK。
  - COM6 在 COM5 负向验证后再次执行配置 ACK 相关 HIL 查询通过。
- 后续闭环：
  - 将 `CONFigure:MODEl:*:LOAD` 接入 command slot，让 SCPI 只 post `NODE_LOAD_STAGE` 或 `CONFIG_STAGE`。
  - 评估是否新增 `SYSTem:COMMand:ACK? / NACK?` 作为通用 command slot 维护视图。
- 关联文件：
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/distributed_refmem/inc/refmem_command.h`
  - `components/distributed_refmem/src/refmem_command.c`
  - `components/system_manager/src/system_manager.c`
  - `tools/refmem/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py`

### REFMEM-TASK-20260815-018 - Command slot foundation

- 状态：完成基础件
- 日期：2026-08-15
- 任务目标：
  - 将 `AckCommandSlot` 从文档契约落到可执行 C 基础件。
  - 为后续 `CONFigure:MODEl:*:LOAD`、NodeLoad staging、配置 activation 和跨节点 ACK/NACK 收敛提供统一数据面。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_command.h` 和 `components/distributed_refmem/src/refmem_command.c`。
  - 实现 `try_post`、`try_take`、`ack`、`nack`、`mark_timeout`、`clear` 和 seqlock snapshot。
  - 字段与 `REFMEM_COMMAND` / `REFMEM_ACK_NACK` payload 对齐，提供 `refmem_command_to_sync_command_payload()` 与 `refmem_command_to_sync_ack_payload()`。
  - TAKE 只做目标、epoch/run_id 和 payload CRC 检查；耗时动作仍必须由 AO/FB service tick 执行，避免违反 HAOFV owner 边界。
  - 新增 `tests/unit/test_refmem_command.c` 和 `tools/tests/run_refmem_command_tests.ps1`，并纳入 `tools/tests/run_host_unit_tests.ps1`。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过 host GCC 断言执行。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815060551`，package CRC `0x06B0C911`。
- 后续闭环：
  - 将模型加载动作接入 command slot：SCPI accepted 后只 post `NODE_LOAD_STAGE` 或 `CONFIG_STAGE`，由 RefMem owner ACK/NACK。
  - 将现有 `system_manager` 配置 ACK 映射到同一 command snapshot，避免 ACK/NACK 事实分裂。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_command.h`
  - `components/distributed_refmem/src/refmem_command.c`
  - `tests/unit/test_refmem_command.c`
  - `tools/tests/run_refmem_command_tests.ps1`

### REFMEM-TASK-20260815-017 - Quality gate negative HIL script

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 将 `TDMA timeout -> RefMem quality gate reject -> SystemManager config gate reject -> OTA restore` 固化成可重复 HIL 脚本。
  - 避免后续用手工 SCPI 命令制造 TDMA timeout 后忘记恢复板卡运行态。
- 完成内容：
  - 新增 `tools/refmem/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py`。
  - 脚本读取初始 `SYSTem:CONFigure:STAT?` 和 `SYSTem:REFMEM:SYNC:TDMA:STATus?`，确认初始 config gate ready。
  - 脚本发送短窗口 `SYSTem:REFMEM:SYNC:TDMA:RX` 且不发 TX，轮询 TDMA timeout，并确认 `SYSTem:CONFigure:STAT?` 进入 `ready=0, gate_state=2`。
  - 支持 `--restore-package` 和 `--expected-build`，在负向验证后调用 OTA send + boot/commit 恢复板卡，并再次确认 config gate ready。
  - 修正 `SYSTem:CONFigure:STAT?` 解析：首字段 build id 是带引号数字字符串，不能被当成 ready 字段。
- 验证结果：
  - `python -m py_compile tools\refmem_quality_gate_hil_validate\refmem_quality_gate_hil_validate.py` 通过。
  - COM5 执行 `python tools\refmem_quality_gate_hil_validate\refmem_quality_gate_hil_validate.py COM5 --expected-build 20260815053147 --restore-package build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --out-dir build-rtos-multicore-smoke\refmem_quality_gate_COM5_20260815053147_scripted_r3` 通过。
  - 报告路径：`build-rtos-multicore-smoke\refmem_quality_gate_COM5_20260815053147_scripted_r3\summary.json`。
- 关联文件：
  - `tools/refmem/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py`

### REFMEM-TASK-20260815-016 - Quality gate RUN gate integration

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 将 `refmem_quality_evaluate_deployment_gate()` 接入产品 config RUN gate。
  - 保持 `SystemManager` 只消费 `DistributedRefMemAO` 的 quality gate 结果，不直接读取 TDMA mailbox 或 physical adapter counter。
- 完成内容：
  - `DistributedRefMemAO` 增加 `distributed_refmem_quality_gate_ready()`，内部从 core1 TDMA snapshot 派生 runtime quality entry，并调用 quality gate evaluator。
  - `system_manager` 的 config gate 同时检查 distributed config、SlotClaim gate 和 RefMem quality gate；任一失败时 `ready=0`、`gate_state=2`、ACK 清空/NACK target mask。
  - 修复 `distributed_refmem.h` 与 `refmem_quality.h` 的 include 循环，公开 API 只暴露布尔 quality gate，不泄漏 quality table 内部类型。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815053147`，package CRC `0x190D53A9`。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过 host GCC 断言执行。
  - COM5/COM6 OTA 提交通过，均返回 `SYSTem:FW:BUILD? => "20260815053147"`。
  - 正向：COM5/COM6 查询 `SYSTem:CONFigure:STAT?` 均返回 `ready=1, gate_state=1, target_mask=15, ack_flags=15, nack_flags=0`。
  - 负向：COM5 执行 `SYSTem:REFMEM:SYNC:TDMA:RX 1000,25000000,16,17,18,23` 且不发 TX 后，`TDMA:STATus?` 显示 `state=5, timeout_count=1, last_result=3, last_error=3`，`SYSTem:CONFigure:STAT?` 变为 `ready=0, gate_state=2`。
  - 恢复：COM5 重新 OTA 同一 package 后恢复 `ready=1, gate_state=1`；COM6 保持 `ready=1, gate_state=1`。
- 后续闭环：
  - quality gate 负向流程已在 `REFMEM-TASK-20260815-017` 固化为 HIL 脚本。
- 关联文件：
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/system_manager/src/system_manager.c`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`

### REFMEM-TASK-20260815-015 - Quality gate evaluator

- 状态：完成基础件，SystemManager 接入待继续
- 日期：2026-08-15
- 任务目标：
  - 在 TDMA runtime quality entry 已进入 `DistributedConnectionQualityTable` 派生视图后，提供表驱动的 `DeploymentGate.QUALITY` 消费入口。
  - 避免 RUN gate 直接遍历 adapter、core1 mailbox 或 SCPI 查询结果。
- 完成内容：
  - `refmem_quality.h/.c` 增加 `refmem_quality_gate_threshold_t`、`refmem_quality_gate_reason_t` 和 `refmem_quality_evaluate_deployment_gate()`。
  - evaluator 输入 `refmem_quality_runtime_table_t` 和阈值，输出 `refmem_deployment_gate_entry_t`，包含 `check_id=REFMEM_APP_GATE_QUALITY`、pass/reject 状态、reject reason、node/slot 和 evidence index。
  - 当前 reason 覆盖 bad argument、empty runtime table、CRC、stale、late、drop、timeout 和 last_error。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 明确 `DeploymentGate.QUALITY` 只能消费 quality evaluator 输出，不能直接读取底层 adapter 或 core1 mailbox。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过 host GCC 断言执行。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815052521`，package CRC `0x346FB00F`。
- 后续闭环：
  - `system_manager` config RUN gate 接入和 TDMA timeout 板端负向验证已在 `REFMEM-TASK-20260815-016` 完成。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_quality.h`
  - `components/distributed_refmem/src/refmem_quality.c`
  - `tests/unit/test_refmem_quality.c`

### REFMEM-TASK-20260815-014 - TDMA quality runtime mapping

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 将 core1 realtime TDMA service 的 window timeout、DMA/physical overrun、missed window 和 physical adapter error 纳入 `DistributedConnectionQualityTable` 派生运行态视图。
  - 保持 active static `ConnectionQualityTable` 只作为契约/CRC 来源，不被 SCPI 或维护 bridge 热写。
- 完成内容：
  - `refmem_quality.h/.c` 增加 TDMA service runtime entry，`quality_id=0x54444D41`，scope 为 `REFMEM_APP_QUALITY_TRANSPORT_ADAPTER`。
  - 映射规则：TDMA `timeout_count -> timeout_count`，`reject_count -> late_count`，`overrun_count -> drop_count`，`last_error -> last_error`，`intent_seq/completed_seq -> seq_expected/seq_last`。
  - `SYSTem:REFMEM:QUALity?` 查询时读取 `distributed_refmem_get_realtime_tdma()` snapshot，并把 TDMA entry 放在 index 1；index 0 仍为本地 PIO SPI adapter 诊断 entry，后续 remote QUALITY entry 依次追加。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 补齐 `TRANSPORT_ADAPTER` scope 和 TDMA 派生映射规则。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过 host GCC 断言执行。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过 host GCC 断言执行。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815051252`，package CRC `0x5B12BF64`。
  - COM5/COM6 OTA 提交通过，均返回 `SYSTem:FW:BUILD? => "20260815051252"`。
  - COM5/COM6 查询 `SYSTem:REFMEM:QUALity? 1` 可见 TDMA runtime entry；HIL 前 entry_count 为 2，HIL 后 entry_count 为 3（追加 remote QUALITY entry）。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --transport tdma --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815051252_tdma_quality_mapping` 通过。
- 还需完成：
  - 将 TDMA quality entry 接入 DeploymentGate/RUN gate evidence 消费规则。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_quality.h`
  - `components/distributed_refmem/src/refmem_quality.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_quality.c`

### REFMEM-TASK-20260815-013 - P4.5 SPI frame legacy removal

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 在 TDMA HIL 通过后删除 `SYSTem:REFMEM:SYNC:SPI:*` 的帧级阻塞命令，避免后续继续通过 core0/SCPI 直驱 RefMem frame transport。
  - 保留 `SPI:ARM/DISarm/STATus?/LINE:*/RAW:*` 作为 bring-up 诊断入口。
- 完成内容：
  - 从 SCPI 命令表删除 `SYSTem:REFMEM:SYNC:SPI:RX?`、`SPI:HELLo`、`SPI:EPOCh`、`SPI:DELTa`、`SPI:ACK`、`SPI:FENCe`、`SPI:QUALity:FRAMe`。
  - 删除上述命令实现和 `scpi_refmem_sync_spi_send_result()`，消除 core0 直发 RefMem frame 的维护路径。
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 改为 TDMA-only frame transport；RAW 仍作为线序/物理链路诊断。
  - HIL frame plan 改为直接使用 `SYSTem:REFMEM:SYNC:HELLo?/EPOCh?/DELTa?/ACK?/FENCe?/QUALity:FRAMe?` 生成 frame，再通过 `TDMA:TX/RX/FRAMe?` 完成两板交换。
- 验证结果：
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815045059`，package CRC `0x732BD11F`。
  - COM5/COM6 OTA 提交通过，均返回 `SYSTem:FW:BUILD? => "20260815045059"`。
  - Negative check：`SYSTem:REFMEM:SYNC:SPI:HELLo` 和 `SYSTem:REFMEM:SYNC:SPI:RX?` 已返回 `Undefined header`，确认旧 SPI frame 入口不再注册。
  - Positive check：`SYSTem:REFMEM:SYNC:SPI:ARM`、`SYSTem:REFMEM:SYNC:SPI:RAW:TX` 和 `SYSTem:REFMEM:SYNC:TDMA:STATus?` 仍可用。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --transport tdma --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815045059_tdma_spi_frame_removed` 通过。
  - HIL 报告路径：`build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815045059_tdma_spi_frame_removed\refmem_spi_hil_report.json`。
- 后续闭环：
  - TDMA window timeout、DMA overrun、missed window 和 physical adapter error 到 `DistributedConnectionQualityTable` 的正式映射已在 `REFMEM-TASK-20260815-014` 完成。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py`

### REFMEM-TASK-20260815-012 - P4.5 COM5/COM6 core1 TDMA HIL 闭环

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 将 RefMem Sync 的两板物理验证从 SCPI 阻塞帧级命令收敛到 core1 realtime TDMA service。
  - 证明 PC 只投递 RX/TX intent 和读取 snapshot，`HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 帧通过真实 PIO+DMA 物理链路完成。
- 完成内容：
  - 修正 `SYSTem:REFMEM:SYNC:TDMA:TX`：SCPI 层只解码 hex 并提交 TDMA intent，不再在 core0 直接调用 physical adapter 发送帧。
  - 固化 HIL 脚本的 TDMA hex 字符串参数，避免未加引号的 A-F hex 被 SCPI lexer 当作数字/指数解析。
  - 将 `refmem_spi_physical_adapter_receive()` 拆成 begin/poll 异步 RX 基础能力；blocking receive 保持兼容并复用同一解析逻辑。
  - 将 TDMA RX 从长时间阻塞 core1 改为每轮 core1 loop poll 一次，`PENDING` 时保持 ARMED/ACCEPTED 快照，不提前写 completed_seq。
  - 将 `REFMEM_REALTIME_TDMA_FRAME_MAX` 对齐到 RefMem Sync 协议 MTU：`REFMEM_SYNC_FRAME_HEADER_SIZE + REFMEM_SYNC_FRAME_PAYLOAD_MAX`。
  - HIL 脚本在 TDMA exchange 前清理两端上一次 intent，并用 `TDMA:STATus?` 做 receiver arm gate。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815043100`，package CRC `0x49E7D77C`。
  - COM5/COM6 OTA 提交通过，均返回 `SYSTem:FW:BUILD? => "20260815043100"`。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --transport tdma --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815043100_tdma_mtu` 通过。
  - HIL 报告路径：`build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815043100_tdma_mtu\refmem_spi_hil_report.json`。
  - 报告中 `A_RAW_B`、`B_RAW_A`、双向 `HELLO/EPOCH/DELTA/ACK/FENCE/QUALITY` 全部 passed；最终 TDMA snapshot 显示 A/B `ready_count=12`、`timeout_count=0`、`overrun_count=0`、`reject_count=0`。
- 还需完成：
  - 将 `SYSTem:REFMEM:SYNC:SPI:*` 帧级阻塞命令降级为 legacy/diagnostic 或删除，仅保留 line/raw bring-up 必要入口。
  - 将 TDMA window timeout、DMA overrun、missed window 和 physical adapter error 正式映射到 `DistributedConnectionQualityTable`。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_realtime_tdma.h`
  - `components/distributed_refmem/inc/refmem_spi_physical_adapter.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/distributed_refmem/src/refmem_realtime_tdma.c`
  - `components/distributed_refmem/src/refmem_spi_physical_adapter.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py`
- 下一步：
  - 收敛 `SYSTem:REFMEM:SYNC:SPI:*` 帧级阻塞命令，并把 TDMA 质量计数并入 RefMem quality/evidence。

### REFMEM-TASK-20260815-011 - P4.5 core1 TDMA service contract 收敛

- 状态：完成 service contract、physical ops 绑定与构建闭环；SCPI 维护路径迁移和 HIL 待继续
- 日期：2026-08-15
- 任务目标：
  - 将当前 PIO+DMA physical adapter 从“只有 SCPI 维护命令能表达状态”向 HAOFV 的 RefMemAO/core1 realtime TDMA service 边界收敛。
  - 先建立 core0 intent 与 core1 result 的字段归属，避免跨核共享状态双 writer。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_realtime_tdma.h` 和 `components/distributed_refmem/src/refmem_realtime_tdma.c`。
  - service 使用双 guard：core0 writer 只更新 intent mailbox、frame staging 和 reject counter；core1 writer 只更新 runtime/result snapshot、completed seq、ready/timeout/overrun/error counter。
  - TDMA service 增加 physical ops 边界，避免把具体总线实现写死在基础状态机中；host 单测使用 fake ops，固件使用 PIO+DMA physical adapter wrapper。
  - `DistributedRefMemAO` 绑定 `refmem_spi_physical_adapter_transmit/receive`，core1 service 可按 intent role、baud 和 deadline 调用真实 PIO+DMA TX/RX。
  - TDMA service 增加 RX result frame 缓存和只读 API；收到的 frame 不写入向量表大数据，后续由 RefMem Sync owner 读取并进入协议 decode/quality。
  - `distributed_refmem_init()` 初始化 TDMA service，`app_realtime_run_once()` 通过 `distributed_refmem_realtime_run_once()` 在 core1 循环中推进。
  - 新增维护查询 `SYSTem:REFMEM:SYNC:TDMA:STATus?`，只读取 snapshot，不直接驱动硬件动作。
  - 新增维护入口 `SYSTem:REFMEM:SYNC:TDMA:TX/RX/FRAMe?/ABORt`：`TX/RX` 只 post intent，`FRAMe?` 读取 core1 result frame 并交给 RefMem Sync receive state machine。
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 增加 `--transport tdma`，在现有线序 remap 和 frame plan 基础上改走 TDMA intent 路径；RAW 仍保留为 SPI line diagnostic。
  - 新增 `tests/unit/test_refmem_realtime_tdma.c` 和 `tools/tests/run_refmem_realtime_tdma_tests.ps1`。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过 host gcc 断言执行。
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815040106`，package CRC `0xE67D0C6D`。
- 后续：
  - 用 COM5/COM6 执行 `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --transport tdma --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_tdma`。
  - TDMA HIL 通过后再收敛或删除 `SYSTem:REFMEM:SYNC:SPI:*` 的帧级阻塞命令。

### REFMEM-TASK-20260815-010 - P4.5 PIO+DMA physical adapter 25 MHz 闭环

- 状态：完成当前 HIL；core1 TDMA service 化待继续
- 日期：2026-08-15
- 任务目标：
  - 按 VDC/TDMA 架构纠偏 P4.5 physical adapter：不得使用 RP2350 内置 SPI，也不得把 SIO bitbang 当作闭环结论。
  - 使用真实线序检测结果驱动 PIO SPI pin plan，在 COM5/COM6 两块最小系统板之间完成真实物理链路 RefMem Sync 帧闭环。
  - 目标速率提升到 25 MHz，避免继续以低速 PC 搬运或逐字节轮询掩盖架构问题。
- 完成内容：
  - 新增 `components/distributed_refmem/src/refmem_spi_physical.pio`，提供 PIO TX byte 和 PIO RX byte state machine。
  - `refmem_spi_physical_adapter.c` 从内置 `spi0`/SIO bitbang 纠偏为 PIO TX、PIO RX 和 DMA RX 缓冲；CPU 不再逐 bit 或逐字节搬运 RX FIFO。
  - `board_config.h` 增加 RefMem PIO transport 的 PIO/SM 资源定义，默认 baud 改为 25 MHz。
  - `tools/sync/two_board_io_validate/two_board_io_validate.py` 支持 auto/detect 模式，并把 observed remap 写入报告。
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 默认使用 auto remap 和 25 MHz，根据实测 OUT->IN map 推导双向 PIO SPI pin plan。
  - 新增 `tools/refmem/refmem_spi_line_activity/refmem_spi_line_activity.py`，用于慢速长 RAW:TX 时观察对端输入 mask，证明 PIO TX 已在真实线束上活动。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，build `20260815031915`，package CRC `0xD5A25528`。
  - COM5/COM6 均 OTA 写入、boot 并 commit 到 build `20260815031915`。
  - `python tools\refmem_spi_line_activity\refmem_spi_line_activity.py --sender COM5 --receiver COM6 --sender-to-receiver 1,2,0,3 --baud 1000 --count 256 --seed 255` 显示 B 侧 IN0/IN2 有高低变化，证明 PIO TX 的 RX/SCK 已过线。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --baud 1000000 --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815031915_pio_dma_1m` 通过。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815031915_pio_dma_25m` 通过。
  - 25 MHz 报告记录：线序 preflight PASS，A->B remap `[1,2,0,3]`，B->A remap `[2,1,0,3]`；`RAW/HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 14 个 exchange 全部 PASS；`failures=[]`。
- 还需完成：
  - 当前 HIL 仍由 SCPI 在帧级触发 TX/RX。产品化下一步必须把 PIO+DMA physical adapter 接入 core1 realtime TDMA service，core0 只配置窗口和帧意图，core1 发布 frame-ready/timeout/overrun snapshot。
  - PIO/SM/DMA 资源需要接入 ResourceArbiter 和 RealtimeCapabilityContract，避免维护 HIL 直接抢占现有 `sync_io` 资源。
- 关联文件：
  - `components/distributed_refmem/src/refmem_spi_physical.pio`
  - `components/distributed_refmem/src/refmem_spi_physical_adapter.c`
  - `components/distributed_refmem/inc/refmem_spi_physical_adapter.h`
  - `boards/rp2350_trig/inc/board_config.h`
  - `tools/sync/two_board_io_validate/two_board_io_validate.py`
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py`
  - `tools/refmem/refmem_spi_line_activity/refmem_spi_line_activity.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`

### REFMEM-TASK-20260815-009 - P4.5 SPI physical adapter 首次接入与线级阻塞定位

- 状态：部分完成，HIL 阻塞在物理线级 preflight
- 日期：2026-08-15
- 任务目标：
  - 按 P4.5 要求，把 RefMem Sync 从 PC/SCPI hex 搬运推进到真实两板 SPI 物理链路。
  - 保持 HAOFV 边界：SCPI 只作为维护触发和诊断入口，RefMem frame 仍由 RefMem Sync validate/receive 处理，不让 Vector 承载数据。
  - 在 COM5/COM6 两块最小系统板上验证 `HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 真实过线。
- 完成内容：
  - 新增 `refmem_spi_physical_adapter.h/.c`，使用 `spi0` 的 debug bring-up profile：RX16、CS17、SCK18、TX19，默认 500 kHz。
  - `SYSTem:REFMEM:SYNC:SPI:*` 增加 ARM/DISarm/STATus、frame TX/RX、raw byte TX/RX 和 line release/drive/status 维护入口。
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 固化三层验证：先扫线，再 raw byte，再 RefMem frame；PC 只触发 TX/RX，不搬运 frame hex。
  - build `20260815022459` 已通过 OTA 写入并在 COM5/COM6 上 commit，双板 `SYSTem:FW:BUILD?` 均返回该 build。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，package CRC `0xAE0FFEB2`。
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - OTA COM5/COM6 正常进入 `READY_TO_REBOOT` 并完成 `SYSTem:OTA:COMMit`。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815022459_linefull` 失败在 line preflight。
  - 六条线级检查均读 0：A_CS_B、A_SCK_B、A_TX_B_RX、B_CS_A、B_SCK_A、B_TX_A_RX。因此当前不是 RefMem frame/CRC/target 语义问题，也不是 raw SPI 负载问题，而是 GPIO16-19 互联未成立或 COM5/COM6 对应板未按该线序连接。
- 下一步：
  - 按 playbook 接线后重跑同一 HIL：`SCK18-SCK18`、`CS17-CS17`、`A_TX19 -> B_RX16`、`B_TX19 -> A_RX16`、`GND-GND`。
  - 线级 preflight 通过后，再进入 raw byte 和 `HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` frame 层闭环。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_spi_physical_adapter.h`
  - `components/distributed_refmem/src/refmem_spi_physical_adapter.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`

### REFMEM-TASK-20260815-008 - Host GCC 单元测试断言门禁

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 使用 `D:\Embedded\GCC\mingw64\bin\gcc.exe` 让现有 C 单元测试在 host 上真实执行断言。
  - 纠正此前多处测试脚本在无 host C 编译器时只做 ARM GCC compile-only 的验证口径。
  - 将该门禁作为继续推进 RefMem 表模型、S0 flash/core1、真实 transport 和 PIO 预约输出前的基础验证。
- 完成内容：
  - 新增 `tools/tests/run_host_unit_tests.ps1`，默认将 `D:\Embedded\GCC\mingw64\bin` 加入 `PATH`，顺序运行 BiSS、portable LOG/OTA 和 RefMem 相关测试脚本。
  - 修正 `run_refmem_application_contract_tests.ps1` 的链接源，补入 `refmem_slot_claim.c`。
  - 修复 `refmem_claim_propose_frame_init()` 在设置 `payload_count` 前计算 payload CRC 的协议 bug。
  - 扩大 `test_refmem_table_registry.c` 的测试构包容量，避免当前 `.rmtp` P0-P3 表 payload 超过旧 `1536` 字节缓冲导致 host 栈破坏。
  - 修正 `test_portable_ota_core.c` 的 package-mode 测试夹具，使 header package CRC 字段保持与当前 packager 一致的 `0` 语义。
  - 修正 `test_biss_protocol.c` 中 CRC6 golden vector 的参与位拼接，改为明确的 18-bit payload/status 和 24-bit payload 向量。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1` 通过。
  - S0 补入 Flash/Core1 lockout 断言后，覆盖 13 个测试脚本、18 个 C 单元测试文件，期望输出 `host unit test scripts passed: 13/13`。
  - Host GCC：`D:\Embedded\GCC\mingw64\bin\gcc.exe`。
- 还需完成：
  - 下一轮优先进入 S0：Flash erase/program 前 core1 park/lockout/RAM-resident 入口完整握手和故障注入。
  - 然后接入真实最小 physical transport，让 `DELTA/ACK/FENCE` 在两板真实链路上跑通。
  - `ModelTurntableAO` 需要补真实 PIO scheduled fire 输出路径，验证“到点出边沿”。
- 关联文件：
  - `tools/tests/run_host_unit_tests.ps1`
  - `tools/tests/run_refmem_application_contract_tests.ps1`
  - `components/distributed_refmem/src/refmem_claim_protocol.c`
  - `tests/unit/test_refmem_table_registry.c`
  - `tests/unit/test_portable_ota_core.c`
  - `tests/unit/test_biss_protocol.c`
  - `docs/validation/README.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 不继续扩大 RefMem 静态表模型，先按 `REFMEM_DOMAIN_TODO.md` 的近期主线完成 S0、真实 transport 和 PIO 预约输出闭环。

### REFMEM-TASK-20260815-007 - RMTP NodeLoadTable 真实表镜像

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 继续 P0 逐表真实化，将 `.rmtp` table 3 `NodeLoadTable` 从占位 payload 升级为固定 u32 表镜像。
  - 让 NodeLoad 的基础字段合法性由公共 application contract 校验，为后续 `LOAD:NODE` staging image 和动态实例装载做准备。
- 完成内容：
  - `tools/refmem/refmem_table_image/refmem_table_image.py` 新增 `build_node_load_payload()`，按当前 11 条默认 NodeLoad 模板输出固定 u32 payload。
  - `refmem_application_contract.h/.c` 新增 `refmem_application_contract_validate_node_load_table()`，校验 version、load count、load id、application/profile、node 范围、instance 范围、fail policy、enabled/required 和 target node mask。
  - `refmem_application_model.c` 的静态 NodeLoad linter 复用公共 contract，并保留对当前内置 FB instance 表的实例存在性、重复 enabled instance 和 required enabled instance 覆盖检查。
  - `refmem_table_registry.c` 新增 NodeLoad parser，并在 `.rmtp` package owner validation 中把 table 3 纳入 owner-validated mask。
  - `tests/python/test_refmem_pack_build.py` 增加 table 3 size/count 断言；`tests/unit/test_refmem_table_registry.c` 增加 NodeLoad package 构造和 owner mask/staging 状态断言。
- 验证结果：
  - `python -m pytest tests\python\test_refmem_pack_build.py` 通过，2 passed。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\refmem_pack_build\refmem_pack_build.py --output-dir build-rtos-multicore-smoke\refmem_pack_nodeload` 通过，生成 `.rmtp` size `1924`、CRC `3A20B868`，table 3 CRC `7D7762D1`。
  - `python tools\sd_fs_build\sd_fs_build.py --build-dir build-rtos-multicore-smoke --output-dir build-rtos-multicore-smoke\sdcard_nodeload --clean` 通过。
  - 独立打包输出与 SD staging 输出的 `/refmem/app_model.rmtp` SHA256 均为 `C2EC768B52E284403D655BF60309E5AE102AE974909763A3B078B5FA79CE75CD`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814172116`，package CRC `0x5B9C1C34`。
- 还需完成：
  - 继续将 FbInstance、EventLink、DataLink、DeploymentGate 和 ConnectionQuality 五张表升级为真实表镜像。
  - `SYSTem:REFMEM:LOAD:NODE` 仍需从单条候选 snapshot 升级为真正的 NodeLoad staging image；本轮只是让 RMTP package 中的完整 NodeLoad 表具备 canonical payload 和 contract。
- 关联文件：
  - `tools/refmem/refmem_table_image/refmem_table_image.py`
  - `tools/refmem/refmem_pack_build/refmem_pack_build.py`
  - `components/distributed_refmem/inc/refmem_application_contract.h`
  - `components/distributed_refmem/src/refmem_application_contract.c`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tests/python/test_refmem_pack_build.py`
  - `tests/unit/test_refmem_table_registry.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`

### REFMEM-TASK-20260815-006 - RMTP ApplicationMap 真实表镜像

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 按 P0 “逐表替换占位 payload”推进 `.rmtp` table 0 `ApplicationMap`。
  - 将 ApplicationMap 校验从 application model 内部静态 linter 提升为可被 SD/System Pack parser 复用的公共 table contract。
- 完成内容：
  - `tools/refmem/refmem_table_image/refmem_table_image.py` 新增 `build_application_map_payload()`，table 0 从 64 字节占位改为 6 个 u32 的真实 `ApplicationMap` payload。
  - `refmem_application_contract.h/.c` 新增 `refmem_application_contract_validate_application_map()`，校验 version、application/profile id、layout version 和 target node mask。
  - `refmem_application_model.c` 的静态 linter 复用公共 ApplicationMap contract，避免内置模型与 `.rmtp` parser 分叉。
  - `refmem_table_registry.c` 新增 ApplicationMap parser，并在 package owner validation 中把 table 0 纳入 owner-validated mask。
  - `tests/python/test_refmem_pack_build.py` 更新 table 0 size/字段断言；`tests/unit/test_refmem_table_registry.c` 更新 placeholder 拒绝点和 owner mask 预期。
- 验证结果：
  - `python -m pytest tests\python\test_refmem_pack_build.py` 通过，2 passed。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\refmem_pack_build\refmem_pack_build.py --output-dir build-rtos-multicore-smoke\refmem_pack_appmap` 通过，生成 `.rmtp` size `1496`、CRC `2F945F7D`，table 0 CRC `60E14FD0`。
  - `python tools\sd_fs_build\sd_fs_build.py --build-dir build-rtos-multicore-smoke --output-dir build-rtos-multicore-smoke\sdcard_appmap --clean` 通过。
  - 独立打包输出与 SD staging 输出的 `/refmem/app_model.rmtp` SHA256 均为 `CAD97D3A4CF3D205AFFEB5375DDE3E49A8A3BD55D4A4140C95AF4F9A9E55A379`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814171242`，package CRC `0x2A60F721`。
- 还需完成：
  - 继续将 NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate 和 ConnectionQuality 六张表升级为真实表镜像。
  - 对包含字符串语义的 FB/Data 表先确定 binary canonical 表达方式，例如固定 hash/id 字段，而不是把 C 指针或可变文本写入 `.rmtp`。
- 关联文件：
  - `tools/refmem/refmem_table_image/refmem_table_image.py`
  - `components/distributed_refmem/inc/refmem_application_contract.h`
  - `components/distributed_refmem/src/refmem_application_contract.c`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tests/python/test_refmem_pack_build.py`
  - `tests/unit/test_refmem_table_registry.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`

### REFMEM-TASK-20260815-005 - TableRegistry package staging 表级 CRC 纠偏

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 修复 `.rmtp` `LOAD:SD` staging 中 `RefMemTableRegistry` 把同一个 package CRC 写入所有 table entry `staging_crc32` 的结构性偏离。
  - 将 package descriptor 与 table entry 的职责分开：descriptor 记录包级 CRC/mask/path，entry 记录对应表自己的 CRC、CRC_OK/OWNER_OK 状态和 owner validation 覆盖情况。
- 完成内容：
  - `refmem_table_package_validation_t` 增加 `table_mask`、`table_crc32[9]` 和 `owner_validated_table_mask`。
  - `.rmtp` parser 在 directory 校验通过时保存每张表的 CRC；BoardCapability/GenericNode owner contract 通过后只把 table 1/2 标记进 `owner_validated_table_mask`。
  - 新增 `refmem_table_registry_stage_package_validation()`，用 package validation summary 更新 per-table staging entry：所有表进入 `CRC_OK`，当前已 owner validation 的 BoardCapability/GenericNode 进入 `OWNER_OK`。
  - staging descriptor 的 `package_crc32` 保持包级 CRC；由于其余七张表仍未 owner validation，descriptor state 保持 `CRC_OK`，不能被 `activate_staging()` 当作完整 `OWNER_OK` active image 激活。
  - `SYSTem:REFMEM:LOAD:SD` 在 package parser 通过后调用新的 package staging API，`SYSTem:REFMEM:TABle? <id>` 可观察真实表级 staging CRC。
  - 单元测试补充 per-table CRC、partial owner state 和 partial owner staging 不可激活的约束。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814170816`，package CRC `0x9B75313C`。
- 还需完成：
  - 将其余七张 `.rmtp` 表升级为真实表镜像并接入 owner validation，使完整 package staging descriptor 能从 `CRC_OK` 进入 `OWNER_OK`。
  - 将 owner validation reason/evidence 进一步落入 table entry 的 `last_result/evidence_index`，而不是只保留 validation summary。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_table_registry.h`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_table_registry.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`

### REFMEM-TASK-20260815-004 - RMTP table image 生成器共享基础件

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 消除 `tools/refmem/refmem_pack_build/refmem_pack_build.py` 与 `tools/storage/sd_fs_build/sd_fs_build.py` 中重复维护的 RMTP 表格式、表名、CRC、BoardCapability 和 GenericNode payload 生成逻辑。
  - 保证独立 RefMem package 工具和 SD System Pack staging 输出同一份 `.rmtp` 二进制表镜像，避免后续表格式演进时出现双源分叉。
- 完成内容：
  - 新增 `tools/refmem/refmem_table_image/refmem_table_image.py`，作为 canonical RMTP table image builder，统一 `RMTP` header、directory、package CRC、9 张表顺序、默认 BoardCapabilityTable 和 GenericNodeTable payload。
  - `tools/refmem/refmem_pack_build/refmem_pack_build.py` 改为复用共享 builder，只保留独立输出目录、manifest 和 idx 生成职责。
  - `tools/storage/sd_fs_build/sd_fs_build.py` 删除私有 `build_refmem_table_package()`、`build_refmem_board_capability_payload()`、`build_refmem_generic_node_payload()` 等重复实现，SD staging 直接调用共享 builder。
  - 两个脚本都补齐直接脚本运行时的 repo root import path，避免 `python tools\...\*.py` 因 `tools` 包不可见失败。
  - `REFMEM_DOMAIN_TODO.md` 与架构文档同步当前状态：BoardCapability/GenericNode 已是 `.rmtp` 真实表镜像，其余表仍按 P0 逐表替换。
- 验证结果：
  - `python -m py_compile tools\sd_fs_build\sd_fs_build.py tools\refmem_pack_build\refmem_pack_build.py tools\refmem_table_image\refmem_table_image.py` 通过。
  - `python -m pytest tests\python\test_refmem_pack_build.py` 通过，2 passed。
  - `python tools\refmem_pack_build\refmem_pack_build.py --output-dir build-rtos-multicore-smoke\refmem_pack_shared` 通过，生成 `.rmtp` size `1536`、CRC `E24B033A`。
  - `python tools\sd_fs_build\sd_fs_build.py --build-dir build-rtos-multicore-smoke --output-dir build-rtos-multicore-smoke\sdcard_shared --clean` 通过。
  - 独立打包输出与 SD staging 输出的 `/refmem/app_model.rmtp` SHA256 均为 `6D3A788DB55DC43BF63434C38C5E00E9741C631B9382710F9768317928401188`。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814170001`，package CRC `0xBD91DC17`。
- 还需完成：
  - 将其余七张 `.rmtp` 表升级为真实表镜像，并继续接入各自 owner validation。
  - 后续可把 manifest/idx 的公共描述逻辑继续收敛，但二进制 table image 已经只有一个生成源。
- 关联文件：
  - `tools/refmem/refmem_table_image/refmem_table_image.py`
  - `tools/refmem/refmem_table_image/__init__.py`
  - `tools/refmem/refmem_pack_build/refmem_pack_build.py`
  - `tools/storage/sd_fs_build/sd_fs_build.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`

### REFMEM-TASK-20260815-003 - RMTP Board/Generic owner validation

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 推进 P0 `.rmtp` 从纯 CRC parser 走向真实表镜像 owner validation。
  - 先闭环固定 u32 布局的 `BoardCapabilityTable` 和 `GenericNodeTable`，避免继续把 64 字节 placeholder 当成可验证表。
- 完成内容：
  - `tools/refmem/refmem_pack_build/refmem_pack_build.py` 和 `tools/storage/sd_fs_build/sd_fs_build.py` 生成真实 `BoardCapabilityTable` / `GenericNodeTable` payload；其他表暂时仍是 placeholder，后续逐表替换。
  - `RefMemTableRegistry` 在 `.rmtp` header、directory、payload、package 和单表 CRC 通过后，解析 table 1/2，并调用 `refmem_application_contract_validate_slot_substrate()` 做 owner validation。
  - `refmem_table_package_error_t` 增加 `REFMEM_TABLE_PACKAGE_ERR_OWNER_VALIDATION`，旧 placeholder package 会被拒绝，`first_bad_table` 指向 table 1。
  - `tests/unit/test_refmem_table_registry.c` 增加旧 placeholder package 拒绝和 contract table package 接受两条回归。
  - `tests/python/test_refmem_pack_build.py` 更新表 1/2 的期望尺寸，确认 payload header 为 `version,node_count`。
  - `REFMEM_DOMAIN_TODO.md` 标记 BoardCapabilityTable 的 `LOAD:SD` owner validation 完成，并新增其余表真实镜像待办。
- 验证结果：
  - `python -m pytest tests\python\test_refmem_pack_build.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python -m py_compile tools\sd_fs_build\sd_fs_build.py tools\refmem_pack_build\refmem_pack_build.py` 通过。
  - `python tools\refmem_pack_build\refmem_pack_build.py --output-dir build-rtos-multicore-smoke\refmem_pack_owner_contract` 通过。
  - `python tools\sd_fs_build\sd_fs_build.py --build-dir build-rtos-multicore-smoke --output-dir build-rtos-multicore-smoke\sdcard_owner_contract --clean` 通过。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `git diff --check` 通过；仅报告工作区 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814164837`，package CRC `0xC2461ED3`。
- 还需完成：
  - 将其余七张 `.rmtp` 表升级为真实表镜像，并按 ApplicationMap/NodeLoad/FB/Event/Data/Gate/Quality owner contract 逐表校验。
  - 将 package owner validation 结果进一步写入 TableRegistry evidence，而不是只返回 package validation error。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_table_registry.h`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tools/refmem/refmem_pack_build/refmem_pack_build.py`
  - `tools/storage/sd_fs_build/sd_fs_build.py`
  - `tests/unit/test_refmem_table_registry.c`
  - `tests/python/test_refmem_pack_build.py`

### REFMEM-TASK-20260815-002 - Realtime contract 入口收敛到 SlotClaimMap

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 消除旧 `refmem_realtime_contract_derive()` 通过 `active_default_slot` 反查 BoardCapability 的 legacy 路径。
  - 保证 RealtimeCapabilityContract 只从 SlotClaimMap resolved assignment 派生，不再把 BoardCapability default slot 当作生产绑定事实。
- 完成内容：
  - 从 `refmem_realtime_contract.h/.c` 删除 `refmem_realtime_contract_derive()` 和内部 `active_default_slot` 查找 helper。
  - `derive_from_claim_map()` 改用公共 `REFMEM_APP_CAP_BASELINE`，与 application contract baseline 语义一致。
  - `tests/unit/test_refmem_realtime_contract.c` 改为构造 `refmem_slot_claim_map_t` 中的 claimed assignment，并只调用 `refmem_realtime_contract_derive_from_claim_map()`。
  - `REFMEM_DOMAIN_TODO.md` 将 P2 legacy realtime contract 偏离项标记完成，并把 P2 当前基线改为 SlotClaimMap resolved assignment。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `git diff --check` 通过；仅报告工作区 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814164049`，package CRC `0x69842137`。
- 还需完成：
  - 继续 P0：`.rmtp` 真实表镜像生成、表级 owner validation、active/staging/rollbackable image buffer。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_realtime_contract.h`
  - `components/distributed_refmem/src/refmem_realtime_contract.c`
  - `tests/unit/test_refmem_realtime_contract.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`

### REFMEM-TASK-20260815-001 - GenericNode slot substrate linter 纠偏

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 审查当前 REFMEM 代码中偏离 HAOFV 的表职责边界，并先闭环修复已开始的 P1 `GenericNodeTable` linter 问题。
  - 保证 A0-A7 仍是通用逻辑插槽，不被 `BoardCapabilityTable[i]` 的 UUID、persona、hw profile 或 default slot 固定绑定。
- 完成内容：
  - `REFMEM_DOMAIN_TODO.md` 增加本轮架构偏离审查待办，覆盖 TableRegistry active image、`.rmtp` owner validation、NodeLoad staging、GenericNode linter、legacy realtime contract 和 vector mutable accessor。
  - 新增 `refmem_application_contract.h/.c`，作为 GenericNode slot substrate 与 BoardCapability 候选能力表的表契约校验基础件。
  - 将 GenericNode contract 与 BoardCapability contract 拆成独立 validator：GenericNode 只校验 node id、baseline、claim policy、online/fail policy；BoardCapability 只校验 board id、baseline、default slot 范围、online flag 和 IO/IP capability 覆盖；组合校验不引入新的 A/B 固定绑定。
  - 将 `REFMEM_APP_CAP_BASELINE` 上移为公共应用模型常量，默认表、SCPI staging 和 application contract 使用同一份 `BOARD + REFMEM + VDC` baseline 定义。
  - 移除 linter 中 `BoardCapabilityTable[i]` 与 `GenericNodeTable[i]` 的 slot、UUID、persona、hw profile 一一相等要求。
  - 新增 `tests/unit/test_refmem_application_contract.c` 和 `tools/tests/run_refmem_application_contract_tests.ps1`，覆盖错位 default slot、不同 UUID/persona/hw profile 仍可通过 substrate contract，以及 baseline/default slot/capability coverage 负例。
  - `REFMEM_DOMAIN_TODO.md` 将对应 P1 偏离项标记完成。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `git diff --check` 通过；仅报告工作区 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814162658`，package CRC `0x6F2E6B9C`。
- 还需完成：
  - 继续处理 P0：真实 active/staging/rollbackable table image、`.rmtp` 表级 owner validation、NodeLoadTable staging image。
  - 后续处理 P2：删除或降级 legacy `refmem_realtime_contract_derive()` 的 default-slot fallback。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_contract.h`
  - `components/distributed_refmem/src/refmem_application_contract.c`
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `tests/unit/test_refmem_application_contract.c`
  - `tools/tests/run_refmem_application_contract_tests.ps1`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`

### REFMEM-TASK-20260814-053 - SlotClaim gate integrity checks

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 优先完成 P1 SlotClaimMap 的关键门禁，避免后续动态节点装载和物理同步链路绕过 A0-A7 通用插槽约束。
  - 补齐 stale claim、claim CRC 和 map CRC 检查，同时纠正“UUID 硬绑定 A slot”的错误方向。
- 完成内容：
  - `refmem_slot_claim_reason_t` 增加 `STALE`、`CLAIM_CRC` 和 `MAP_CRC` reason，并移除硬绑定 mismatch reason，避免把 A0-A7 误当固定物理板或固定功能。
  - `refmem_slot_claim_derive_map()` 不再用 UUID 反查固定 slot；`STRICT_UUID` 只要求候选板具备稳定 UUID。
  - SlotClaim resolved assignment 的 runtime capability 采用 BoardCapability 中的实际能力，slot 本身只承担通用地址、claim policy、priority 和 required 语义。
  - `refmem_slot_claim_gate_evaluate()` 重新计算 map CRC 和每个 assignment CRC，发现篡改或不一致时拒绝 gate。
  - `refmem_slot_claim_gate_evaluate()` 将显式 `STALE` 状态或 assignment epoch 与 map epoch 不一致视为 stale claim，拒绝进入 ready。
  - `tests/unit/test_refmem_slot_claim.c` 覆盖 duplicate、缺失 UUID、任意 slot claim、candidate overflow、stale、claim CRC 和 map CRC。
  - `tools/rtos/multicore_board_validate.py` 不再把 A0/A2/A7 解释成固定功能位置，只验证 generic SlotClaimMap gate、required/optional 和 config RUN gate 一致性。
  - `REFMEM_DOMAIN_TODO.md` 将 P1 SlotClaim 完整性检查项标记完成。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python -m py_compile tools\multicore_board_validate\multicore_board_validate.py` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814155637`，package CRC `0xA9699AF4`。
  - COM5 OTA/commit 通过，`SYSTem:FW:BUILD?` 返回 `20260814155637`，`SYSTem:ERRor?` 返回 `0,"No error"`。
  - COM6 OTA/commit 通过，`SYSTem:FW:BUILD?` 返回 `20260814155637`，`SYSTem:ERRor?` 返回 `0,"No error"`。
  - 初次双板 `multicore_board_validate.py` 只有旧脚本的固定 slot 功能期望失败；修正为 generic slot gate 并烧录最终 build 后，COM5/COM6 均 17/17 passed。
  - 板端 SlotClaim 查询：`SYSTem:REFMEM:CLAIM? 0` 在 COM5/COM6 均返回 gate ready、map CRC `386979554`、无 conflict/overflow/mismatch；`SYSTem:REFMEM:CLAIM:EVIDence? 0` 返回空 evidence；`SYSTem:CONFigure:STAT?` ready；`SYSTem:ERRor?` 为 no error。
- 还需完成：
  - 后续继续 P1：BoardCapabilityTable 纳入 `.rmtp` 真实表镜像、单板 16 候选反向验证和动态装载验证。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_slot_claim.h`
  - `components/distributed_refmem/src/refmem_slot_claim.c`
  - `tests/unit/test_refmem_slot_claim.c`
  - `tools/rtos/multicore_board_validate/multicore_board_validate.py`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 继续 P1：BoardCapabilityTable `.rmtp` 镜像、动态装载和 16 候选溢出 HIL。

### REFMEM-TASK-20260814-052 - RefMem Quality runtime snapshot

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将本地 PIO SPI adapter 计数和 remote `REFMEM_QUALITY` snapshot 规范映射为 `DistributedConnectionQualityTable` entry 形状。
  - 保持 active static `ConnectionQualityTable` 只作为契约/CRC 来源，不在运行中被维护 bridge 热写。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_quality.h` 和 `src/refmem_quality.c`。
  - 新增 `refmem_quality_runtime_table_t`，包含 active quality table CRC、local slot、epoch/run、overflow count 和 runtime quality entries。
  - 本地 adapter 条目映射 sync CRC/stale/drop 计数和 adapter bad/drop/timeout/last error/latency class。
  - remote 条目映射 `REFMEM_QUALITY` 的 source/target、seq、CRC/stale/drop/late/timeout、last error、p99/p999 和 evidence。
  - 新增维护查询 `SYSTem:REFMEM:QUALity? [index]`，读取 runtime derived quality snapshot，不修改 active RefMem fact。
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 增加 runtime quality 查询校验，确认远端 QUALITY frame 接收后进入派生质量视图。
  - `REFMEM_DOMAIN_TODO.md` 将 P4.5 quality 映射项和 P5 `refmem_quality.h/.c` 标记完成。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python -m py_compile tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814151311`，package CRC `0x97D14458`。
  - COM5/COM6 均通过 OTA 更新并 commit 到 build `20260814151311`。
  - `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814151311 --package-crc 0x97D14458 --line-remap-a-to-b 1,2,0,3 --line-remap-b-to-a 2,1,0,3 --preflight-io --out-dir build-rtos-multicore-smoke\refmem_sync_quality_runtime_hil_COM5_COM6_20260814151311_r2` 通过。
  - HIL 关键结果：IO preflight PASS；B0->B1 remap `[1,2,0,3]`，B1->B0 remap `[2,1,0,3]`；RefMem Sync 63 条记录全部 PASS；两端 runtime quality index `1` 均正确反映对端 QUALITY frame，A 侧记录 B->A `crc_error_count=1,drop_count=2,last_error=9`，B 侧记录 A->B 无错误。
- 还需完成：
  - 真实 PIO SPI physical adapter service 接入，替换当前 PC/SCPI frame 搬运。
  - 后续把 quality snapshot 接入 DeploymentGate/RUN gate evidence。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_quality.h`
  - `components/distributed_refmem/src/refmem_quality.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_quality.c`
  - `tools/tests/run_refmem_quality_tests.ps1`
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`
- 下一步：
  - 编译、烧录 COM5/COM6 并执行带 IO preflight 的 RefMem Sync HIL，完成本轮闭环后提交推送。

### REFMEM-TASK-20260814-051 - RefMem Sync HIL 报告与 IO 预检固化

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 补齐 P4.5 阶段 0 的线序与串口生命周期检查，把 `REALtime:IO:PROFile?`、输出 release、逐线 remap 和退出清理纳入 RefMem Sync HIL 前置流程。
  - 扩展 RefMem Sync HIL 报告，记录 package CRC、双向线序 remap 和 IO preflight 结果，避免后续依赖手写日志。
- 完成内容：
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 新增 `--package-crc`、`--line-remap-a-to-b`、`--line-remap-b-to-a` 和 `--preflight-io`。
  - `--preflight-io` 会在主协议交换前调用 `tools/sync/two_board_io_validate/two_board_io_validate.py`，该工具完成双板 profile 查询、输出 release、逐线 remap 验证和退出释放。
  - `summary.json` 与 `transcript.txt` 写入 package CRC、line remap 和 `io_preflight` 摘要；preflight 结果保存到同一报告目录下的 `io_preflight/`。
  - `REFMEM_DOMAIN_TODO.md` 将阶段 0 与 HIL 报告扩展项标记完成。
- 验证结果：
  - `python -m py_compile tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py` 通过。
  - `python tools\two_board_io_validate\two_board_io_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\two_board_io_COM5_COM6_preflight_20260814143942` 通过。
  - `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814143942 --package-crc 0x4D1483AE --line-remap-a-to-b 1,2,0,3 --line-remap-b-to-a 2,1,0,3 --preflight-io --out-dir build-rtos-multicore-smoke\refmem_sync_report_hil_COM5_COM6_20260814143942` 通过。
  - HIL 关键结果：IO preflight PASS；B0->B1 remap `[1,2,0,3]`，B1->B0 remap `[2,1,0,3]`；完整 RefMem Sync HIL 61 条记录全部 PASS；报告记录 package CRC `0x4D1483AE`。
- 还需完成：
  - 真实 PIO SPI physical adapter service 接入，替换当前 PC/SCPI frame 搬运。
  - 将 remote quality snapshot 映射到 `DistributedConnectionQualityTable`。
- 关联文件：
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
  - `tools/README.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
- 下一步：
  - 进入真实 PIO SPI physical adapter service 的实现拆解；先定义 service 状态机、TX/RX ownership、线序依赖和与当前 SCPI bridge 的并存边界。

### REFMEM-TASK-20260814-050 - RefMem Sync QUALITY frame 最小闭环

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 在 FENCE 已通过的基础上，增加 `REFMEM_QUALITY` frame，把本地 receive quality counter 和 peer seq 摘要同步给对端。
  - 保持 QUALITY frame 只写 remote quality snapshot，不直接写 active `DistributedConnectionQualityTable`。
- 完成内容：
  - `refmem_sync_context_t` 增加按 source slot 索引的 `refmem_sync_remote_quality_snapshot_t`。
  - `refmem_sync_receive_frame()` 接收 `REFMEM_QUALITY` 后提交 remote quality snapshot，记录 quality id、scope、target slot、seq expected/last、CRC/stale/drop/late/timeout、last error 和 evidence。
  - 新增 `refmem_sync_get_remote_quality()`。
  - 新增 `SYSTem:REFMEM:SYNC:QUALity:FRAMe?`，从本地 receive quality counter 和指定 peer seq 摘要生成 QUALITY frame。
  - 新增 `SYSTem:REFMEM:SYNC:QUALity:STATus?`，查询指定 source slot 最近一次 remote QUALITY snapshot。
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 扩展为 HELLO/EPOCH/DELTA/MIRROR/ACK_NACK/FENCE/QUALITY 全流程，记录 61 条 HIL 结果。
- 验证结果：
  - `python -m py_compile tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814143942`，package CRC `0x4D1483AE`。
  - COM5/COM6 均 OTA 并 commit 到 build `20260814143942`。
  - `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814143942 --out-dir build-rtos-multicore-smoke\refmem_sync_quality_hil_COM5_COM6_20260814143942` 通过，61 条记录全部 PASS。
  - HIL 关键结果：两板 build id 均为 `20260814143942`；SlotClaimMap CRC 均为 `386979554`；A->B QUALITY snapshot 记录 `seq_expected=9,seq_last=8` 且错误计数为 0；B->A QUALITY snapshot 记录 `seq_expected=10,seq_last=9,crc_error_count=1,drop_count=2,last_error=9`。
- 还需完成：
  - 将 remote QUALITY snapshot 映射到 `DistributedConnectionQualityTable`，并明确 active table owner validation 与 evidence index。
  - 真实 PIO SPI physical adapter service 接入，替换当前 PC/SCPI frame 搬运。
  - 增加 quality 与 DeploymentGate/RUN gate 的消费规则。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_sync.h`
  - `components/distributed_refmem/src/refmem_sync.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_sync.c`
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`
- 下一步：
  - 回到 P4.5 未完成项：真实 PIO SPI physical adapter service、线序/生命周期阶段 0 固化，以及 quality 到 `DistributedConnectionQualityTable` 的正式映射。

### REFMEM-TASK-20260814-049 - RefMem Sync FENCE 最小闭环

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 在 DELTA mirror 和 ACK/NACK 已通过的基础上，增加最小 `REFMEM_FENCE`，让接收板能基于 source mirror visible 与最小 seq 形成 pass/fail/timeout snapshot。
  - 保持 FENCE 仍为 RefMem Sync 维护闭环，不直接触发产品 RUN gate、DistributedDeploymentGate 或 fault latch。
- 完成内容：
  - `refmem_sync_context_t` 增加按 source slot 索引的 `refmem_sync_fence_snapshot_t`。
  - `refmem_sync_receive_frame()` 接收 `REFMEM_FENCE` 后解析 `refmem_sync_fence_payload_t`，检查 local slot 是否在 required mask 中、source mirror 是否 visible、mirror frame seq 是否满足 `min_table_seq`。
  - 新增 `refmem_sync_get_fence()`。
  - 新增 `SYSTem:REFMEM:SYNC:FENCe?`，生成最小 FENCE frame，携带 fence seq、scope、required mask、min table seq、CRC bundle 和 deadline。
  - 新增 `SYSTem:REFMEM:SYNC:FENCe:STATus?`，查询指定 source slot 最近一次 FENCE snapshot。
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 扩展为 HELLO/EPOCH/DELTA/MIRROR/ACK_NACK/FENCE 全流程，增加双向 FENCE pass 和 min seq 不满足的 fail/timeout 场景。
- 验证结果：
  - `python -m py_compile tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814142748`，package CRC `0xE3F75DB7`。
  - COM5/COM6 均 OTA 并 commit 到 build `20260814142748`。
  - `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814142748 --out-dir build-rtos-multicore-smoke\refmem_sync_fence_hil_COM5_COM6_20260814142748` 通过，55 条记录全部 PASS。
  - HIL 关键结果：两板 build id 均为 `20260814142748`；SlotClaimMap CRC 均为 `386979554`；A->B 与 B->A FENCE 在对端 mirror visible 且 `min_table_seq=3` 时 passed；A->B `min_table_seq=99,deadline_us=0` 时 failed，`missing_mask=2,timed_out=1,last_reason=3`。
- 还需完成：
  - 将 FENCE snapshot 接入正式 `refmem_command.h/.c` completion、DeploymentGate 和 `DistributedConnectionQualityTable`。
  - 实现 `REFMEM_QUALITY` frame，把 adapter CRC/drop/late/timeout 计数作为总线无关质量事实发布。
  - 真实 PIO SPI physical adapter service 接入，替换当前 PC/SCPI frame 搬运。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_sync.h`
  - `components/distributed_refmem/src/refmem_sync.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_sync.c`
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`
- 下一步：
  - 进入 P4.5 阶段 4 的 `REFMEM_QUALITY` frame，把现有 adapter 和 receive quality counter 从本地查询扩展为可同步质量事实。

### REFMEM-TASK-20260814-048 - RefMem Sync ACK/NACK 最小闭环

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 在 DELTA mirror 已通过的基础上，增加 `REFMEM_ACK_NACK` 回传，让两块板能对已接收 DELTA 和异常 RX 结果形成可查询确认。
  - 覆盖正向 ACK、duplicate seq、target mismatch 和 payload CRC mismatch，不直接写 active ApplicationModel、SlotClaimMap 或业务 active fact。
- 完成内容：
  - `refmem_sync_context_t` 增加按 source slot 索引的 `refmem_sync_ack_snapshot_t`。
  - `refmem_sync_receive_frame()` 接收 `REFMEM_ACK_NACK` 后提交 ACK/NACK snapshot，记录 command/delta seq、taken/ack/nack/busy/timeout 位图、reason、evidence、frame seq 和 received count。
  - payload CRC 错误时尽量保留可解码 header 到 RX snapshot，使维护 bridge 能为坏帧生成 NACK 证据。
  - 新增 `refmem_sync_get_ack()`。
  - 新增 `SYSTem:REFMEM:SYNC:ACK?`，基于本板最近一次 RX snapshot 生成 ACK_NACK frame。
  - 新增 `SYSTem:REFMEM:SYNC:ACK:STATus?`，查询指定 source slot 最近一次 ACK/NACK snapshot。
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 扩展为 HELLO/EPOCH/DELTA/MIRROR/ACK_NACK 全流程，正向 ACK 双向生成后再互相注入，避免 ACK frame 覆盖 DELTA `last_rx`。
- 验证结果：
  - `python -m py_compile tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814141201`，package CRC `0xA1E002E6`。
  - COM5/COM6 均 OTA 并 commit 到 build `20260814141201`。
  - `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814141201 --out-dir build-rtos-multicore-smoke\refmem_sync_ack_hil_COM5_COM6_20260814141201_r3` 通过，46 条记录全部 PASS。
  - HIL 关键结果：两板 build id 均为 `20260814141201`；SlotClaimMap CRC 均为 `386979554`；A->B 与 B->A DELTA seq `3` 均收到 ACK；duplicate seq NACK reason `6`、target mismatch NACK reason `4`、payload CRC mismatch NACK reason `9` 均由对端接收并可查询。
- 还需完成：
  - 将 ACK/NACK 从维护 bridge 的 `last_rx` 生成模式收敛到正式 `refmem_command.h/.c` command slot 完成语义。
  - 增加 `REFMEM_FENCE` 可见性门禁，把 mirror visible、ACK/NACK 和 RUN/SYNC gate 串起来。
  - 真实 PIO SPI physical adapter service 接入，替换当前 PC/SCPI frame 搬运。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_sync.h`
  - `components/distributed_refmem/src/refmem_sync.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_sync.c`
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`
- 下一步：
  - 进入 P4.5 阶段 4 的 FENCE/QUALITY 闭环，并开始把 ACK/NACK 与正式 command slot completion 语义对齐。

### REFMEM-TASK-20260814-047 - RefMem Sync DELTA mirror 最小闭环

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 在 HELLO/EPOCH 已通过的基础上，增加最小 `REFMEM_DELTA` test field，让两块板能通过受控协议帧同步一个 u32 事实到对端 mirror snapshot。
  - 保持 DELTA mirror 仍属于 RefMem Sync 维护闭环，不直接写 active ApplicationModel、SlotClaimMap 或业务 active fact。
- 完成内容：
  - `refmem_sync_context_t` 增加按 source slot 索引的 `refmem_sync_mirror_snapshot_t`。
  - `refmem_sync_receive_frame()` 接收 `REFMEM_DELTA` 后解析 `refmem_sync_delta_header_t + u32 value`，更新 mirror visible、slot、field、slot_seq、value、payload CRC、frame seq、committed count 和 visible count。
  - 新增 `refmem_sync_get_mirror()`。
  - `SYSTem:REFMEM:SYNC:DELTa?` 可生成带 ACK request flag 的最小 u32 DELTA frame。
  - `SYSTem:REFMEM:SYNC:MIRRor?` 可查询指定 source slot 的 mirror snapshot。
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 从 HELLO/EPOCH 扩展为 HELLO/EPOCH/DELTA/MIRROR，并增加 build id、SlotClaimMap CRC 和 adapter snapshot 预检。
- 验证结果：
  - `python -m py_compile tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814134858`，package CRC `0xA776513E`。
  - COM5/COM6 均 OTA 并 commit 到 build `20260814134858`。
  - `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814134858 --out-dir build-rtos-multicore-smoke\refmem_sync_delta_hil_COM5_COM6_20260814134858_report` 通过，26 条记录全部 PASS。
  - HIL 关键结果：两板 build id 均为 `20260814134858`；SlotClaimMap CRC 均为 `386979554`；adapter id 均为 `1`；A->B DELTA value `2768240641`、B->A DELTA value `3053453314` 均在对端 mirror 可见；两板 quality `accepted_count=3` 且 frame、CRC、target、epoch 错误计数为 0。
- 还需完成：
  - `REFMEM_ACK_NACK` 回传，把 DELTA accepted、duplicate seq、target mismatch 和 payload CRC mismatch 转成可查询 ACK/NACK 结果。
  - `REFMEM_FENCE` 可见性门禁，把 mirror visible 与 RUN/SYNC gate 串起来。
  - 真实 PIO SPI physical adapter service 接入，替换当前 PC/SCPI frame 搬运。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_sync.h`
  - `components/distributed_refmem/src/refmem_sync.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_sync.c`
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
- 下一步：
  - 进入 P4.5 阶段 3 的 ACK_NACK 闭环，优先让接收侧根据最近一次 RX snapshot 生成 ACK_NACK frame 并由对端接收/记录。

### REFMEM-TASK-20260814-046 - RefMem Sync HELLO/EPOCH SCPI 搬运闭环入口

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 在真实 PIO SPI 物理 adapter 完成前，先通过系统维护面把 RefMem Sync frame 在两块最小系统板之间搬运起来。
  - 验证 HELLO/EPOCH 的 frame encode、adapter RX staging、receive state machine、peer snapshot 和 quality counter，不直接修改 active RefMem fact。
- 完成内容：
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c` 增加 `SYSTem:REFMEM:SYNC:*` 维护入口：`INITialize`、`HELLo?`、`EPOCh?`、`RX`、`PEER?`、`QUALity?`、`ADAPter?`。
  - `HELLo?` 从 BoardCapabilityTable、ApplicationModel snapshot、adapter caps 和 build id 摘要生成 HELLO frame。
  - `EPOCh?` 从 TableRegistry/ApplicationModel snapshot 生成首版 EPOCH frame。
  - `RX` 执行 `hex -> PIO SPI adapter RX staging -> poll -> refmem_sync_receive_frame()`，只更新 sync context 的 peer/quality 状态。
  - 新增 `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py`，固化两板 HELLO/EPOCH 搬运验证，支持 COM 口和 USBTMC VISA resource。
  - `REFMEM_SYNC_ARCHITECTURE.md` 增加维护面 SCPI bridge 边界，明确该入口不是裸 RefMem 域，也不是 active fact 写入口。
  - `REFMEM_MIN_SYSTEM_PLAYBOOK.md` 增加两板 HELLO/EPOCH 验证命令、脚本和通过条件。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814133439`，package CRC `0x1926CA52`。
  - `python tools\ota_send\ota_send.py COM5 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python tools\ota_boot_commit\ota_boot_commit.py COM5 --expected-build 20260814133439 --out-dir build-rtos-multicore-smoke\ota_commit_COM5_20260814133439` 通过，`SYSTem:FW:BUILD?` 返回 `20260814133439`，`SYSTem:ERRor?` 返回 `0,"No error"`。
  - `python tools\ota_send\ota_send.py COM6 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python tools\ota_boot_commit\ota_boot_commit.py COM6 --expected-build 20260814133439 --out-dir build-rtos-multicore-smoke\ota_commit_COM6_20260814133439` 通过，`SYSTem:FW:BUILD?` 返回 `20260814133439`，`SYSTem:ERRor?` 返回 `0,"No error"`。
  - `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --out-dir build-rtos-multicore-smoke\refmem_sync_hil_COM5_COM6_20260814133439` 通过，14 条记录全部 PASS。
  - HIL 关键结果：A/B 双向 HELLO 与 EPOCH 的 `RX` 均 `ACCEPTED`；A peer slot 1 与 B peer slot 0 均 `hello_seen=1, epoch_seen=1, frame_count=2`；A/B quality 均 `accepted_count=2` 且 frame、CRC、target、epoch 错误计数为 0。
- 还需完成：
  - 后续把同一帧通路接到真实 PIO SPI 物理 adapter service，再推进 DELTA、ACK_NACK、FENCE 和 QUALITY。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
  - `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 烧录两块板并执行 HELLO/EPOCH HIL；若通过，关闭 P4.5 阶段 2 的 SCPI 搬运闭环，再开始真实 PIO SPI adapter service。

### REFMEM-TASK-20260814-045 - TableRegistry image descriptor 与 activation 骨架

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将上一轮文档中的 active/staging/rollbackable table image contract 落到 `RefMemTableRegistry` 的首版代码形态。
  - 先建立 registry 级 descriptor、activation gate 和失败不污染 active 的验证，不提前改动真实业务表 buffer。
- 完成内容：
  - `refmem_table_registry.h/.c` 增加 `refmem_table_image_descriptor_t`，覆盖 role、state、table mask、package CRC、table seq、path hash、last result 和 evidence index。
  - 增加 active/staging/rollbackable 三类 descriptor，`refresh_active()` 初始化 active descriptor，`refresh_staging()` 和 `stage_table()` 更新 staging descriptor。
  - 增加 `refmem_table_activation_gate_t` 和 `refmem_table_registry_activate_staging()`：必须显式传入 RefMem idle、realtime idle、flash safe、CRC OK、owner OK、SlotClaim OK、DeploymentGate OK 和 command ACK OK；gate 失败时只记录结果，不改 active。
  - activation 成功时旧 active descriptor 进入 rollbackable，新 active descriptor 从 validated staging 派生，registry entry 的 active CRC 和状态同步更新，staging descriptor 清空。
  - 新增 `tests/unit/test_refmem_table_registry.c` 和 `tools/tests/run_refmem_table_registry_tests.ps1`，固化 active descriptor、gate 失败、成功 activation、rollbackable descriptor 和无有效 staging 拒绝验证。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814131250`，package CRC `0xA9FBFD22`。
- 还需完成：
  - 将 activation gate 的各个输入接到真实 RefMem mode、产品实时 idle/park、flash lockout/RAM-resident 状态、SlotClaimMap、DeploymentGate 和 command ACK。
  - 实现真实 active/staging/rollbackable table buffer 切换，而不是只切 registry descriptor 和 CRC。
  - 把 image descriptor 暴露到维护查询或专用验证脚本，形成板端 activation 正反向闭环。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_table_registry.h`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tests/unit/test_refmem_table_registry.c`
  - `tools/tests/run_refmem_table_registry_tests.ps1`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 继续 P0，将 owner validation callback 调度接入 TableRegistry，并把 validation result 写入 table id、owner id、validator id、reason 和 evidence。

### REFMEM-TASK-20260814-044 - RefMem table image activation 主线收敛

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 通读 `docs/refmem` 当前主线，承接 StorageAO 通用文件管理和 `.rmtp` staging load 的最新闭环。
  - 将下一轮 RefMem 工作从“能加载 staging snapshot”收敛到“可验证、可激活、可回滚的 active/staging/rollbackable table image”。
  - 明确 RefMem 向量表不承载 table image 或文件数据，只承载状态、CRC、path hash、version、seq、quality 和 evidence 摘要。
- 完成内容：
  - `README.md` 更新到 2026-08-14，补充 StorageAO 文件/目录 CRUD、`app_model.rmtp` 上传/读回、`SYSTem:REFMEM:LOAD:SD` staging 正向闭环，以及下一轮 activation 主线。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `Table Image Activation Contract`，定义 `active_image`、`staging_image`、`rollbackable_image` 三类镜像、activation 状态链、activation gate、descriptor 切换规则和失败回滚证据。
  - `REFMEM_DOMAIN_TODO.md` 将 Storage 文件上传和通用 CRUD 板端验证标记完成，并新增 table image descriptor、activation gate、owner validation evidence、activation 正向和失败回滚验证待办。
- 验证结果：
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
- 还需完成：
  - 实现真实 table image descriptor 和 active/staging/rollbackable 切换。
  - 实现 owner validation callback 调度，并将 table id、owner id、validator id、reason 和 evidence 写入 TableRegistry。
  - 增加 activation 正向和失败回滚脚本，覆盖 CRC 正确但 owner validation/SlotClaim/DeploymentGate/ACK 失败不得污染旧 active image。
- 关联文件：
  - `docs/refmem/README.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 进入 P0 table image activation 代码闭环，优先落地 descriptor、activation gate 和 owner validation callback。

### REFMEM-TASK-20260814-043 - 功能 AO 模板化与 ModelTurntableAO 首个可加载实例

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 取消 `TriggerMasterAO`、`TriggerAO`、`LinkSwitcherAO`、`PulseCounterAO`、`InstrumentControllerAO`、`ModelVnaAO` 和 `ModelTurntableAO` 的默认固定运行语义。
  - 保持 A0-A7 为通用逻辑槽位，功能节点最终由 SCPI 或 SD System Pack staging 确认装载，不在默认表中写死。
  - 先落地一个可验证的模拟转台实例，作为后续模型节点实例化的首个样例。
- 完成内容：
  - `refmem_application_model.c` 中功能 AO 名称改为 `Template.*` 语义；默认 active 基线只保留基础 RefMem/Loop/System/Calibration 类 owner，功能 AO 作为待加载模板。
  - `refmem_application_model.h` 增加模型信号 IO claim：`MODEL_TURNTABLE_PULSE`、`MODEL_VNA_READY`、`MODEL_VNA_TRIGGER` 和 `LINK_SWITCH_EVENT`。
  - `refmem_realtime_contract.c` 将模型信号 IO claim 映射为 `PIO + DMA + CORE1_RT` 实时能力需求。
  - 新增 `components/model_turntable`，实现模拟转台脉冲发生器：支持扫描起止/步长、脉宽、边沿、超时、速度和加速度配置；脉冲间隔按加速、匀速、减速形成疏密变化。
  - 新增 `SCPI_MODEL_COMMANDS`，提供 `CONFigure:MODEl:TURNtable:*`、`READ:MODEl:TURNtable:*` 和 `MODEl:TURNtable:STARt/STOP`。
  - 当前 debug 输出经 `sync_io_debug_model_write_pin()` 驱动 GPIO4..7 overlay；后续应迁移到 PIO/DMA/core1 预约输出。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814112552`，package CRC `0xD7B2581D`。
  - `python tools\ota_send\ota_send.py COM3 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python tools\ota_boot_commit\ota_boot_commit.py COM3 --expected-build 20260814112552 --out-dir build-rtos-multicore-smoke\ota_boot_commit_model_template_COM3` 通过，COM3 commit 成功。
  - COM3 查询 `READ:MODEl:TURNtable:LOAD?` 返回 `0,4294967295,0`，确认默认未加载。
  - 未加载时执行 `MODEl:TURNtable:STARt` 后，`SYSTem:ERRor?` 返回 `-200,"Execution error"`，符合必须先 LOAD 的约束。
  - 执行 `CONFigure:MODEl:TURNtable:LOAD 1,0` 后，`READ:MODEl:TURNtable:LOAD?` 返回 `1,1,0`。
  - 执行 `CONFigure:MODEl:TURNtable:TRIGger 0,0,10,1,2000,1,-1` 和 `CONFigure:MODEl:TURNtable:MOTion 10,20` 后，`MODEl:TURNtable:STARt/STOP` 均返回 `"OK"`，最终 `SYSTem:ERRor?` 为 `0,"No error"`。
- 还需完成：
  - 将 `ModelTurntableAO` 的 LOAD 结果接入 RefMem NodeLoad staging / activation，而不是只停留在本地运行时状态。
  - 将 debug GPIO 输出迁移为 PIO/DMA/core1 预约输出，并把脉冲计数、last tick、profile phase 写入 RefMem snapshot。
  - 按同一模式补 `ModelVnaAO`、`LinkSwitcherAO`、`PulseDistributorAO` 和 `VnaGatewayAO` 的可加载实例。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/refmem_realtime_contract.c`
  - `components/model_turntable/inc/model_turntable.h`
  - `components/model_turntable/src/model_turntable.c`
  - `middleware/scpi_port/inc/scpi_model_commands.h`
  - `middleware/scpi_port/src/scpi_model_commands.c`
  - `components/sync_io/inc/sync_io.h`
  - `components/sync_io/src/sync_io.c`
- 下一步：
  - 继续推进 P4.6 最小业务 HIL，先让模拟转台脉冲由可加载实例驱动，再由对端捕获并形成 VDC/RefMem 时间事实。

### REFMEM-TASK-20260814-042 - GPIO4..7 overlay 维护接口与预检工具

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 为 GPIO4..7 最小模型 overlay 增加方向安全维护接口和双板预检脚本。
  - 先确保 UART1 不占用 GPIO4/5，且脚本能以串口生命周期管理方式 release、驱动、读取和退出清理。
- 完成内容：
  - `sync_io.h/.c` 增加 `sync_io_debug_model_*` 维护函数，覆盖 GPIO4..7 output enable/value、release 和 input level snapshot。
  - `scpi_realtime_io_commands.c/h` 增加 `REALtime:IO:MODel:PROFile?`、`REALtime:IO:MODel:INPut:LEVel?`、`REALtime:IO:MODel:OUTPut:MASK`、`REALtime:IO:MODel:OUTPut:MASK?` 和 `REALtime:IO:MODel:OUTPut:RELease`。
  - `REALtime:IO:MODel:PROFile?` 返回 `base_pin,pin_count,uart_conflict_mask,uart_enabled`，当前期望为 `4,4,3,0`。
  - `REALtime:IO:MODel:OUTPut:MASK <enable_mask>,<value_mask>` 显式区分输出 owner 和输出电平；未 enable 的线恢复输入下拉。
  - 新增 `tools/sync/debug_model_overlay_validate/debug_model_overlay_validate.py`，默认验证 X->Y GPIO4/GPIO5/GPIO7 和 Y->X GPIO6，运行前后 release 双方 GPIO4..7。
  - `tools/README.md`、`REFMEM_MIN_SYSTEM_PLAYBOOK.md` 和 `REFMEM_DOMAIN_TODO.md` 同步新增工具入口和待办状态。
- 验证结果：
  - `python -m py_compile tools\debug_model_overlay_validate\debug_model_overlay_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC 为 `0x2DF62B6E`，build id 为 `20260814104920`。
  - `python tools\ota_send\ota_send.py COM3 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，COM3 OTA 进入 `READY_TO_REBOOT`。
  - `python tools\ota_boot_commit\ota_boot_commit.py COM3 --expected-build 20260814104920 --out-dir build-rtos-multicore-smoke\ota_boot_commit_debug_model_COM3` 通过，COM3 启动并 commit 到 build `20260814104920`。
  - COM3 顺序查询 `REALtime:IO:MODel:PROFile?` 返回 `4,4,3,0`，确认 GPIO4..7 overlay profile 生效且 UART stdio 关闭。
  - COM3 顺序查询 `REALtime:IO:MODel:INPut:LEVel?` 返回 `4,4,0`。
  - COM4 首次 OTA 未完成：`ota_send.py COM4 ...` 打开端口失败，错误为 `PermissionError(13, '拒绝访问。', None, 5)`，确认为串口软件占用；关闭占用工具后重试通过。
  - `python tools\ota_send\ota_send.py COM4 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，COM4 OTA 进入 `READY_TO_REBOOT`。
  - `python tools\ota_boot_commit\ota_boot_commit.py COM4 --expected-build 20260814104920 --out-dir build-rtos-multicore-smoke\ota_boot_commit_debug_model_COM4` 通过，COM4 启动并 commit 到 build `20260814104920`。
  - COM4 顺序查询 `REALtime:IO:MODel:PROFile?` 返回 `4,4,3,0`。
  - `python tools\debug_model_overlay_validate\debug_model_overlay_validate.py --port-x COM3 --port-y COM4 --out-dir build-rtos-multicore-smoke\debug_model_overlay_COM3_COM4` 通过。
  - HIL 方向结果：`TURN_POS_PULSE` X.GPIO4 -> Y.GPIO4、`VNA_READY` X.GPIO5 -> Y.GPIO5、`VNA_TRIG` Y.GPIO6 -> X.GPIO6、`LINK_SWITCH` X.GPIO7 -> Y.GPIO7 均 PASS。
- 还需完成：
  - 进入最小业务 HIL：A1 位置脉冲、A4 捕获并更新时间事实、A3 预约链路切换、A5 触发虚拟网分、A2 返回 READY。
- 关联文件：
  - `components/sync_io/inc/sync_io.h`
  - `components/sync_io/src/sync_io.c`
  - `middleware/scpi_port/inc/scpi_realtime_io_commands.h`
  - `middleware/scpi_port/src/scpi_realtime_io_commands.c`
  - `tools/sync/debug_model_overlay_validate/debug_model_overlay_validate.py`
  - `tools/README.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 将 overlay 节点写入 NodeLoad/Capability staging，并补齐最小业务 HIL 的 RefMem snapshot / quality / evidence 读取闭环。

### REFMEM-TASK-20260814-041 - GPIO4..7 最小模型 overlay 规划

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将用户当前已连接的 `GPIO4..7` 记录为最小系统业务模型 overlay。
  - 明确 Y 板模型实例整体向后挪一个槽位，避免和 X 板 `A3` 链路控制冲突。
  - 消除最小系统 UART1 对 `GPIO4/5` 的默认占用风险。
- 完成内容：
  - `REFMEM_MIN_SYSTEM_PLAYBOOK.md` 增加 `GPIO4..7` 最小模型 overlay，定义 X 板 `A1/A2/A3` 和 Y 板 `A4/A5`。
  - `HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md` 增加 overlay 硬件约束，明确每根线的唯一输出 owner、输入 receiver 和模拟信号。
  - `REFMEM_DOMAIN_TODO.md` 增加 `P4.6 - 最小模型系统 GPIO4..7 Overlay`，把 profile、node load、RealtimeCapabilityContract、方向安全脚本和业务 HIL 拆成后续待办。
  - `board_config.h` 注明 `GPIO4/5` 的 UART1 兼容定义只在 UART stdio 启用时有效，最小模型 overlay 可复用。
  - `board.c` 将 `drv_uart.h` include 和 `board_init_uart()` 内部初始化改为受 `PROJECT_ENABLE_UART_STDIO` 控制；默认最小系统 build 不初始化 UART1。
- 当前 overlay 线束：
  - `GPIO4`: X `A1` 模拟转台输出 `TURN_POS_PULSE` -> Y `A4` 脉冲分发输入。
  - `GPIO5`: X `A2` 模拟网分输出 `VNA_READY` -> Y `A5` VNA 网关输入。
  - `GPIO6`: Y `A5` VNA 网关输出 `VNA_TRIG` -> X `A2` 模拟网分输入。
  - `GPIO7`: X `A3` 链路控制输出 `LINK_SWITCH` -> Y `A5` VNA 网关输入。
- 还需完成：
  - 增加 debug model board profile 或等价配置表，显式声明 `GPIO4..7` overlay 与 UART1 互斥。
  - 增加方向安全检测脚本，确保运行前双方非 owner 引脚 release。
  - 增加最小业务 HIL，验证位置脉冲、VDC 发布时间、预约链路切换、虚拟网分触发和 READY 回传。
- 关联文件：
  - `boards/rp2350_trig/inc/board_config.h`
  - `boards/rp2350_trig/src/board.c`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 先做 GPIO4..7 方向安全脚本和 profile 声明，再把 overlay 节点写入 RefMem node load / realtime capability contract。

### REFMEM-TASK-20260814-040 - REFMEM_HELLO bundle helper

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 为 P4.5 阶段 2 的 `REFMEM_HELLO` 双向交换建立可复用 payload/frame 生成入口。
  - 将 board capability、adapter caps、layout/application/config CRC 和 build id CRC 收敛为标准 HELLO，而不是在后续 HIL 或 SCPI 中临时拼字段。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_transport_adapter.h`，抽出通用 adapter id、capability、state、error 和 `refmem_transport_caps_t`。
  - `refmem_pio_spi_adapter.h` 改为引用公共 transport adapter 头，避免后续 BISS-C/RJ45/UART/RS485 adapter 重复定义。
  - 新增 `components/distributed_refmem/inc/refmem_sync_hello.h`。
  - 新增 `components/distributed_refmem/src/refmem_sync_hello.c`。
  - 实现 `refmem_sync_hello_payload_from_board()`：从 board capability、adapter caps 和版本 CRC 生成 `refmem_sync_hello_payload_t`。
  - 实现 `refmem_sync_hello_encode_frame()`：把 HELLO payload 编码为标准 RefMem Sync frame。
  - 新增 `tests/unit/test_refmem_sync_hello.c` 和 `tools/tests/run_refmem_sync_hello_tests.ps1`，覆盖 payload 字段、frame validate、adapter inject/poll 和 payload byte match。
  - 将 `refmem_sync_hello.c` 加入根 `CMakeLists.txt` 固件源列表。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_hello_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_pio_spi_adapter_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_frame_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC 为 `0x36759853`，build id 为 `20260814103620`。
  - 本轮未烧录板端，未运行 COM3/COM4 HIL；真实两板 `REFMEM_HELLO` 双向交换仍未完成。
- 还需完成：
  - 定义 `task_refmem_sync` 内的 adapter service 调用点，接入 HELLO TX/RX 周期。
  - 增加两板 HIL 工具步骤：双方生成 HELLO、发送、接收、校验 build/layout/application/config/capability/adapter bundle。
  - 将 HELLO 结果写入 peer state 和 connection quality snapshot。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_transport_adapter.h`
  - `components/distributed_refmem/inc/refmem_sync_hello.h`
  - `components/distributed_refmem/src/refmem_sync_hello.c`
  - `components/distributed_refmem/inc/refmem_pio_spi_adapter.h`
  - `tests/unit/test_refmem_sync_hello.c`
  - `tools/tests/run_refmem_sync_hello_tests.ps1`
  - `CMakeLists.txt`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 设计 `RefMemSyncService` 的 TX/RX service 边界，并把 HELLO frame 进入 `refmem_sync_receive_frame()` 的路径固定下来。

