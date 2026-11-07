# 文档索引

Status: Active
Domain: Documentation
Canonical: `docs/README.md`
Related: `docs/docs/DOCS_NAMING_STRUCTURE_PLAN.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-11-06

本文档是 `docs/` 的总入口。新文档的命名、层级、元数据和迁移规则以
`docs/docs/DOCS_NAMING_STRUCTURE_PLAN.md` 为准。

当前阶段已经完成平铺文档命名迁移，后续文档管理进入“按产品主域目录化”的规划阶段。
域目录目标、迁移批次和 gate 以 `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md` 为准。迁移完成前，
根目录中的历史路径仍是有效路径；新文档优先按目标域选择落点。

## Docs 目标域结构

参考 `D:\Work\ADS_AUTO_SIM\docs` 的文档治理方式，Distributed Hard Real-Time Trigger System 后续采用“总 README + 域目录 README + canonical 主文档”的管理方式。
长期目标结构如下：

```text
docs/
  README.md
  standards/      通用规范与跨项目治理标准（非产品域）
  docs/           文档治理、命名规则、迁移表
  evaluation/     项目价值评估与长期监督（评估结论、行动追踪、复评闭环）
  arch/           产品架构、HAOFV、RTOS 和分布式总纲
  interface/      SCPI、USB、USBTMC、命令表、上位机接口
  trigger/        产品触发、序列、角度、core1 实时执行
  sync/           SYNC 动作、SYNC_IO、同步链路落地
  calibration/    CAL link、delay、参数、版本、质量
  tdma/           上行/下行 TDMA、payload registry、adapter 和环路 completion 基础件
  refmem/         分布式向量表、命令槽、ACK/NACK、节点事实
  refmem/event/   RefMem Event Plane 子域（静态域）：EventPack/Catalog/Map/Alias/EventVector
  vdc/            虚拟 DC、共同时间、DPLL、HOLDOVER、时间质量
  communication/  BiSS-C、UART、RS485、RJ45 后端维护
  measure/        测量原语、T2 摘要、链路 delay 测量服务
  storage/        SD、StorageAO、日志、trace、snapshot、报告证据
  ota/            OTA、boot、A/B、回滚、System Pack
  hardware/       IO 约束、PCB、网表、BOM、Gerber、硬件评审
  tools/          工具治理、登记、依赖、生命周期与交接
  console/        运维与调试控制面：Tk 应用框架、workflow、证据与安全门
  validation/     HIL、验证方法、闭环验证记录和证据说明
  state_machine/  PIO SM、DMA/FIFO、上下行资源和底层实时状态机
  release/        发布门禁、打印/PDF、产品冻结 checklist
  reports/        当前产品 HTML/PDF 输出、打印和交付件
  temp/           阶段性调试、验证和绘图归档
  legacy/         PinProbe、历史报告、最初版 HTML、外部迁入资料
  archive/        废弃路径说明、旧索引和批量迁移记录
```

现阶段不直接大规模移动文件。迁移顺序：先建立域 README，再按单一主域小批量迁移 Markdown，最后处理 HTML/PDF 和历史资料。
每批迁移必须更新本索引、`Canonical`/`Related`、脚本引用，并运行文档检查。

## 域目录入口

| 目录 | 入口 | 当前作用 |
|---|---|---|
| `docs/` | `docs/README.md` | 文档治理、命名规则、迁移表 |
| `standards/` | `standards/README.md` | 通用规范：可复用的大型项目治理标准（非产品域，不作为产品契约） |
| `evaluation/` | `evaluation/README.md` | 项目价值评估与长期监督：开源/商业/学术价值评估、行动追踪与复评闭环 |
| `arch/` | `arch/README.md` | 产品架构、HAOFV、RTOS 和分布式总纲 |
| `interface/` | `interface/README.md` | SCPI、USB、USBTMC、命令表和上位机接口 |
| `trigger/` | `trigger/README.md` | 产品触发、序列、角度、core1 实时执行 |
| `sync/` | `sync/README.md` | SYNC 动作、SYNC_IO、同步链路和硬实时 IO 落地 |
| `calibration/` | `calibration/README.md` | CAL link、delay、参数、版本和质量 |
| `tdma/` | `tdma/README.md` | 上行/下行 TDMA、payload registry、adapter、ring runtime 和 completion evidence 基础件 |
| `refmem/` | `refmem/README.md` | 分布式向量表、命令槽、ACK/NACK、同步架构和最小系统板操作 |
| `refmem/event/` | `refmem/event/REFMEM_EVENT_ARCHITECTURE.md` | RefMem Event Plane 子域（静态域）：EventPack/Catalog/Map/Alias/EventVector |
| `vdc/` | `vdc/README.md` | 虚拟 DC、共同时间、DPLL、HOLDOVER 和时间质量 |
| `communication/` | `communication/README.md` | BiSS-C、UART、RS485 和通信维护 |
| `measure/` | `measure/README.md` | 测量原语、T2 摘要和链路 delay 测量服务 |
| `storage/` | `storage/README.md` | SD、StorageAO、日志、trace、snapshot 和报告证据 |
| `ota/` | `ota/OTA_HAOFV_ARCHITECTURE.md` | OTA HAOFV、boot、A/B、回滚和 System Pack |
| `hardware/` | `hardware/README.md` | IO 约束、PCB、网表、BOM、Gerber 和硬件评审 |
| `tools/` | `tools/DOCS_TOOLS_ARCHITECTURE.md` | 工具治理三件套：身份、依赖、生命周期、验证与交接；实现仍位于根目录 tools/ |
| `console/` | `console/README.md` | 运维与调试控制面：单端口/多端口 transport、流程编排、证据凭证与安全门；调试 Demo 为首个交付物 |
| `validation/` | `validation/README.md` | HIL、验证方法、任务进度和闭环验证记录 |
| `state_machine/` | `state_machine/HAOFV_STATE_MACHINE_ARCHITECTURE.md` | PIO SM、DMA/FIFO、上下行资源和底层实时状态机 |
| `watchdog/` | `watchdog/README.md` | 硬件 watchdog、健康监督、复位证据和故障注入 |
| `release/` | `release/README.md` | 发布门禁、打印/PDF、产品冻结 checklist |
| `reports/` | `reports/README.md` | 当前产品 HTML/PDF 输出、打印和交付件 |
| `temp/` | `temp/README.md` | 阶段性调试、验证和绘图归档 |
| `legacy/` | `legacy/README.md` | PinProbe、历史报告、最初版 HTML 和外部迁入资料 |
| `archive/` | `archive/README.md` | 废弃路径说明、旧索引和批量迁移记录 |

## Canonical 主文档

| 领域 | 当前 canonical 主文档 | 说明 |
|---|---|---|
| STANDARDS | `standards/STANDARDS_DOMAIN_ARCHITECTURE.md` | 通用规范域架构：定位、边界、文档模型、标准生命周期与验证映射；主标准见 `standards/STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.md`。 |
| EVALUATION | `evaluation/PRODUCT_VALUE_EVALUATION.md` | 项目价值评估 canonical：开源/商业/学术三维评估单链结论；行动追踪见 `evaluation/PRODUCT_VALUE_TRACKING_TODO.md`。 |
| TOOLS | `tools/DOCS_TOOLS_ARCHITECTURE.md` | 工具域治理草案，定义工具、组件、工作流、证据及交接边界；自动化尚待实施。 |
| CONSOLE | `console/CONSOLE_ARCHITECTURE.md` | 运维与调试控制面主域：域边界、子域、单端口/多端口原则、契约候选、证据与验收；调试 Demo 为首个交付物。 |
| CONSOLE/TK | `console/CONSOLE_TK_ARCHITECTURE.md` | Tk 应用框架：Shell/Router/Page/Store/Intent/UiPump/Dialog/WidgetKit、测试与迁移。 |
| CONSOLE/WORKFLOW | `console/CONSOLE_WORKFLOW_ARCHITECTURE.md` | **Workflow 唯一定义**：WorkflowSpec、步骤模型、能力范围/矩阵、成熟度、运行语义、证据与版本、必须工作流化的流程与禁止（从主域架构 §7 摘出）。 |
| CONSOLE/TODO | `console/CONSOLE_DOMAIN_TODO.md` | CONSOLE 任务表（P0/P1/P2）、阻塞与验收口径。 |
| CONSOLE/PROGRESS | `console/CONSOLE_TASK_PROGRESS.md` | CONSOLE 真实执行、证据、失败与下一 gate。 |
| ARCH/HAOFV | `arch/HAOFV_ARCHITECTURE.md` | 顶层 HAOFV 架构入口，定义组件约束、层次逻辑、Vector/Blackboard 和约束传递。 |
| ARCH/FLASH | `arch/HAOFV_FLASH_ARCHITECTURE.md` | 16 MiB 板载 Flash、Boot/Direct A/B、NVS/blob/FCB、统一 OTA stream 和 TDMA OTA 跨域 canonical。 |
| ARCH/T2 | `arch/ARCH_T2_RESERVATION_ARCHITECTURE.md` | 已弃用（2026-10-11）：T2 预约与分布式时钟分发的历史跨域输入；冲突处以 `trigger/t2/TRIGGER_T2_ARCHITECTURE.md` 的 T2-SEMANTICS 为准。 |
| VDC | `vdc/VDC_DOMAIN_ARCHITECTURE.md` | VDC 内部主域架构，定义共同时间事实、同步 DPLL、HOLDOVER、时间质量和预测分发时间基准。 |
| ARCH/VDC-DPLL | `arch/HAOFV_VDC_DPLL_ARCHITECTURE.md` | 既有 HAOFV VDC/DPLL 融合架构输入；后续逐步迁入 VDC canonical。 |
| ARCH/PRODUCT | `arch/ARCH_PRODUCT_ARCHITECTURE.md` | 面向 Distributed Hard Real-Time Trigger System 的产品系统架构特化，服从 HAOFV 顶层约束。 |
| ARCH/FUTURE | `arch/ARCH_FUTURE_APPLICATION_PLAN.md` | 当前产品完成后的应用场景、跨平台移植、版本分层和开源生态路线图。 |
| ARCH/SYNCCORE | `arch/ARCH_SYNC_STANDARD_CORE_DESIGN.md` | 分布式同步核心规范第一版条款清单（甜区：通用 MCU 上 10-100 ns 级相位与触发同步）；草稿，未登记为契约。 |
| ARCH/OTA-TDMA | `arch/ARCH_OTA_TDMA_STREAM_DESIGN.md` | OTA TDMA 流式升级设计说明（v2，2026-10-31 起以 §13 为准）：REFMEM 控制面 + `0x4F` TDMA 数据面、目标复用既有 AB 流程、V2-A..D 迁移与六门符合性；契约 `ARCH-OTASTREAM-02`（pending，C11 有条件通过，见核验提交单）。 |
| SYNC_IO | `sync/SYNC_IO_ARCHITECTURE.md` | PIO、GPIO、DMA、语义 IO 和硬实时资源约束入口。 |
| TDMA | `tdma/TDMA_DOMAIN_ARCHITECTURE.md` | TDMA 基础件主域，定义上/下行 TDMA、payload registry、adapter、ring runtime、completion evidence 和 HAOFV system node 边界。 |
| STATE_MACHINE | `state_machine/HAOFV_STATE_MACHINE_ARCHITECTURE.md` | 状态机与底层实时资源域，定义 PIO SM、DMA/FIFO、上下行控制和资源仲裁边界。 |
| WATCHDOG | `watchdog/ARCH_WATCHDOG_ARCHITECTURE.md` | Watchdog 硬件适配、唯一 feed owner、健康门禁、复位证据和 OTA/Flash 故障定位边界。 |
| TRIGGER | `trigger/TRIGGER_SYNC_TODO.md` | 触发业务模式、生产化缺口和跨模式待办入口。 |
| REFMEM/EVENT | `refmem/event/REFMEM_EVENT_ARCHITECTURE.md` | RefMem Event Plane 子域（静态域）canonical：EventPack/Catalog/Map/Alias/EventVector；运行时分发/执行归 RefMem/TRIGGER。 |
| BISSC | `communication/BISSC_TAP_BRIDGE_DESIGN.md` | BiSS-C 协议、TAP bridge、固件 persona 和验证边界入口。 |
| OTA | `ota/OTA_HAOFV_ARCHITECTURE.md` | OTA 域 owner、OtaAO/OtaFB/OtaVector、BCB selection 和 Boot 交接；跨域 Flash 语义服从 `arch/HAOFV_FLASH_ARCHITECTURE.md`。 |
| OTA/OPS | `ota/OTA_OPERATIONS_PLAYBOOK.md` | OTA 日常操作：单板/四板刷写、失败分类、恢复与证据留档。 |
| OTA/V1 | `ota/OTA_V1_LEGACY_PLAYBOOK.md` | V1 compat 已归档/恢复基线：不再作为功能参考，保留 factory 回退。 |
| OTA | `ota/OTA_TDMA_STREAM_PLAN.md` | TDMA 流式 OTA（M5）里程碑计划：目标/前提、三个决策门、夜次编排、任务表与硬门禁（2026-10-29 重规划，Draft）。 |
| SD | `storage/SD_TODO.md` | SD、StorageAO、System Pack、快照和持久化观测入口。 |
| LOG | `storage/LOG_SYSTEM_TODO.md` | 日志 core、诊断 trace、持久化和故障证据入口。 |
| SCPI | `interface/SCPI_COMMANDS.md` | SCPI 命令语义、兼容性和用户可调用接口入口。 |
| SCPI | `interface/SCPI_INTERFACE_COMMANDS.md` | SCPI 指令面机械导出清单（813 条 / 28 域，按域-功能-参数归类），含命名一致性审查与接口缺陷记录（Draft）。 |
| USB | `interface/SCPI_USB_INTERFACE_DESIGN.md` | USB CDC、USBTMC/USB488、VISA 枚举和供电描述符策略。 |

## 进度记录路由

| 领域 | 当前进度入口 | 规则 |
|---|---|---|
| BiSS-C | `communication/BISSC_TASK_PROGRESS.md` | BiSS-C 新任务记录写入本文件。 |
| SYNC_IO | `sync/SYNC_IO_TASK_PROGRESS.md` | SYNC_IO / Trigger 同步重构闭环记录写入本文件。 |
| TRIGGER | `trigger/sequence/TRIGGER_SEQUENCE_ARCHITECTURE.md` | 位置内序列的解算与执行（拿到位置时间片后自行按约定展开）；深度 ≥2 绝对 deadline 队列与逐组证据口径。 |
| TRIGGER | `trigger/sequence/TRIGGER_SEQUENCE_TODO.md` | 序列解算与执行的任务状态（`TRIGGER-SEQ-*`，跨域前置 `SYNC-SCHED-001`）。 |
| TRIGGER | `trigger/sequence/TRIGGER_SEQUENCE_TASK_PROGRESS.md` | 序列解算与执行的闭环与台架证据记录。 |
| REFMEM/EVENT | `refmem/event/REFMEM_EVENT_TASK_PROGRESS.md` | RefMem Event Plane 子域新任务记录写入本文件。 |
| RTOS | `arch/RTOS_HAOFV_TASK_PROGRESS.md` | RTOS / 双核 / 分布式触发任务闭环记录写入本文件。 |
| Flash | `arch/HAOFV_FLASH_TASK_PROGRESS.md` | FlashMap、事务 owner、store、Boot/OTA 迁移和 HIL checkpoint 写入本文件。 |
| SCPI | `interface/SCPI_TASK_PROGRESS.md` | SCPI 指令框架、验证脚本和接口拆分闭环记录写入本文件。 |
| SD | `storage/SD_TASK_PROGRESS.md` | SD / StorageAO / System Pack 新任务记录写入本文件。 |
| Documentation | `docs/docs/DOCS_MIGRATION_TODO.md` | 文档治理和迁移记录写入本文档体系待办。 |
| TDMA | `tdma/TDMA_TASK_PROGRESS.md` | TDMA 基础件、上/下行 runtime、adapter 和环路闭环验证记录写入本文件。 |
| STATE_MACHINE | `state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md` | 状态机资源迁移、PIO SM 重构和底层实时验证记录写入本文件。 |
| STATE_MACHINE | `state_machine/HAOFV_STATE_MACHINE_TODO.md` | 状态机域可执行任务、退出门禁和后续迁移工作写入本文件。 |
| OTA | `ota/OTA_TODO.md` | OTA HAOFV 任务状态、依赖和退出门禁。 |
| OTA | `ota/OTA_V2STREAM_PLAYBOOK.md` | V2 流式 OTA / TDMA relay 交接记录：已完成、现场命令、踩坑、债务与下一步。 |
| OTA | `ota/OTA_TASK_PROGRESS.md` | OTA 代码、构建、A/B HIL、失败与回退证据。 |
| WATCHDOG | `watchdog/ARCH_WATCHDOG_TODO.md` | Watchdog 子域任务、故障注入、五板 P3 和 OTA/reset/commit 验收写入本文件。 |
| WATCHDOG | `watchdog/ARCH_WATCHDOG_TASK_PROGRESS.md` | Watchdog 构建、测试、P3、五板 OTA 和失败证据写入本文件。 |
| TOOLS | `tools/DOCS_TOOLS_TASK_PROGRESS.md` | 工具盘点、依赖收敛、门禁与交接复现证据；状态见 `tools/DOCS_TOOLS_TODO.md`。 |
| 其他领域 | 新建或补齐 `<DOMAIN>_TASK_PROGRESS.md` | 后续新闭环记录优先建立领域进度文件，不再追加到全局历史文件。 |
| 全局历史 | `TASK_PROGRESS.md` | 只保留跨域历史和迁移前记录；除跨域总览外不再作为默认新任务入口。 |

## 00 文档治理

工具域入口：[治理架构](tools/DOCS_TOOLS_ARCHITECTURE.md)、[实施待办](tools/DOCS_TOOLS_TODO.md)、[任务进度](tools/DOCS_TOOLS_TASK_PROGRESS.md)。工具治理归 `docs/tools/`，通用文档体系仍归 `docs/check/` 与 `docs/docs/`。

| 文件 | 定位 |
|---|---|
| `docs/standards/README.md` | 通用规范子域入口：可复用治理标准的定位、索引与边界。 |
| `docs/standards/STANDARDS_DOMAIN_ARCHITECTURE.md` | 通用规范域架构：定位、边界、文档模型、标准生命周期与验证映射。 |
| `docs/standards/STANDARDS_DOMAIN_TODO.md` | 通用规范域任务状态唯一落点：里程碑、任务表、阻塞与完成定义。 |
| `docs/standards/STANDARDS_TASK_PROGRESS.md` | 通用规范域交付与验证证据：按时间倒排，不替代 TODO 状态。 |
| `docs/standards/STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.md` | 通用化大型项目 AI 协作管理体系：文档/契约/工具/Skill/工作流/证据治理，含模板、路线图与结构工程适配示例。 |
| `docs/standards/STANDARDS_ADOPTION_GUIDE_PLAYBOOK.md` | 外部团队 90 天采用手册：阶段路线、角色、最小产物、门禁、裁剪和止损。 |
| `docs/standards/STANDARDS_LOCALIZATION_KIT_PLAYBOOK.md` | 本地化填空工具包：六系统映射、三件套、契约/工具/WorkflowSpec、验收凭证和结构工程示例。 |
| `docs/standards/STANDARDS_VERSION_REVIEW_ARCHIVE_PLAYBOOK.md` | 版本、复审、changelog 与归档机制：版本规则、季度复审、旧版归档和模板。 |
| `docs/standards/STANDARDS_FEEDBACK_EVALUATION_PLAYBOOK.md` | 反馈与评估手册：反馈日志、分诊/处置/回流、反向评估记录和模板。 |
| `docs/docs/DOCS_NAMING_STRUCTURE_PLAN.md` | 文档命名格式、层级关系、新增文件规则和迁移规则。 |
| `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md` | 按产品主域目录化管理的目标结构、迁移批次、域映射和 gate。 |
| `docs/docs/DOCS_MIGRATION_TODO.md` | 文档体系迁移待办，跟踪元数据补齐、历史改名和索引维护。 |
| `docs/check/DOCS_REGISTRY.md` | 文档契约登记表 + 条款落点表（自回归体系唯一事实源，配合 `tools/doc_regression_check.py`）。 |
| `docs/check/DOCS_REGRESSION_PLAN.md` | 文档自回归体系实施规格（v3 归档版，源方案）。 |
| `docs/check/DOCS_REGRESSION_TODO.md` | 文档自回归体系实施待办，跟踪 T1-T15 执行状态与每步反馈。 |
| `docs/check/DOCS_REGRESSION_REVIEW.md` | 文档自回归体系实施经验总结（问题清单 + 解法 + 维护建议）。 |
| `docs/check/DOCS_PROJECT_STATUS_REVIEW_20260824.md` | 2026-08-24 项目状态审查快照：域进度、关键闭环、治理门禁和在制改动边界；不作为冻结契约或代码事实源。 |
| `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` | 跨 worker/agent 的长期执行约束、证据闭环、Debug 门禁和主控复核规则（首版 Draft）。 |
| `docs/check/DOCS_PROJECT_GOVERNANCE_GOALS_PLAN.md` | 项目治理长期目标 G1–G4、里程碑、度量、台架需求与复审节奏（规划基线，非契约）。 |
| `docs/check/DOCS_CONTRACT_ACTIVATION_WAVE_PLAN.md` | 契约激活波次计划：Wave 0 host-only、Wave 1 高风险台架、Wave 2 剩余项与激活协议。 |
| `docs/check/DOCS_PROGRESS_ROTATION_PLAN.md` | 进展日志轮转（C14/C15 环5）操作规程与债务基线唯一落点：规则 R1-R6、倒排约定、轮转步骤与归档命名。 |
| `docs/check/submissions/README.md` | 核验提交单归档目录说明 + 模板（层间逐级核验，C11 交叉审核）。 |
| `docs/check/submissions/DOCS_PROGRESS_CROSS_REVIEW_01.md` | C14/C15 进展日志契约的两次独立 C11 复核：`DOCS-PROGRESSROTATE-01`、`DOCS-PROGRESSORDER-01` 均 `PASS_WITH_NOTE`，保持 pending；列明闭包/同向/边界缺口与后续任务。 |
| `docs/check/submissions/TDMA_CROSS_REVIEW_01.md` | 首份核验提交单：HAOFV-879 seqlock 偏差（ACCEPT_WITH_DEVIATION，2026-08-19）。 |
| `docs/check/submissions/TDMA_CROSS_REVIEW_02.md` | TDMA mandatory-first process image 的 HAOFV/VDC/RefMem 层间核验（pending，2026-08-28）。 |
| `docs/check/submissions/TDMA_CROSS_REVIEW_03.md` | TDMA bounded recovery 双 buffer、原 Node offset 与实时诊断边界层间核验（pending，2026-08-28）。 |
| `docs/check/submissions/TDMA_CROSS_REVIEW_04.md` | TDMA resident process image、单轮多 Node overlay 与状态机生命周期层间核验（pending，2026-09-02）。 |
| `docs/check/submissions/TDMA_CROSS_REVIEW_05.md` | TDMA Core1 phase 预算归因：500 µs 目标下未达（实测 667–692 µs），缺口归因到 adapter RX 路径并建立优化队列；2026-09-14 判据修订（预算升至 850 µs）后改判 `MET`（有限窗口快照，非事实源；C11 pending）。 |
| `docs/check/submissions/TDMA_CROSS_REVIEW_06.md` | TDMA 对 DPLL 的支撑能力核验：`PARTIAL_SUPPORT`——硬件 latch 8 ns/非 diagnostic/链路零误码已成立，同圈关联与生产准入未闭合；给出 P1–P5 优先任务（有限窗口快照，非事实源；C11 pending，2026-09-14）。 |
| `docs/check/submissions/REFMEM_CROSS_REVIEW_01.md` | RefMem 静态缩容评审结论与 TODO 移交；独立方案复核及 `REFMEM-LAYOUT-01` pending 登记审核完成，实施与四板验收证据见 RefMem Task Progress。 |
| `docs/check/submissions/ARCH_OTA_CROSS_REVIEW_01.md` | `ARCH-OTASTREAM-02`（OTA TDMA v2：REFMEM 控制面 + `0x4F` 数据面）C11 交叉复核：有条件通过，保留 pending，01 暂不 supersede（2026-10-31）。 |
| `docs/check/submissions/EVENT_CROSS_REVIEW_01.md` | Event Plane 架构第二版与 RefMem 事件面微调的独立复评：B1–B3 已闭合，E0 前仍有 5 项阻塞；`PASS_WITH_CHANGES`，C11 最终签署待独立复核。 |
| `docs/check/submissions/EVENT_CROSS_REVIEW_02.md` | Event Plane 架构第三版（命令面/EventPack/传输/失败恢复）复审：R-B1/B2/B4 部分通过，新增 B6 POSITION/EVR 重复接收；`PASS_WITH_CHANGES`。 |
| `docs/check/submissions/EVENT_CROSS_REVIEW_03.md` | Event Plane 静态域重写复审：静态域约束基本落实；RefMem 仍列 `EVENT_INJECT/ABORT`、静态 owner 未命名、`VALidate?`/`CONFigure` 语义不清；`PASS_WITH_CHANGES`。 |
| `docs/check/submissions/EVENT_CROSS_REVIEW_04.md` | RefMem Event Plane 子域迁移 R4 复审：迁移/索引通过，S-B1 闭合；S-B2/S-B3、EventPack 路径/rollback、EventVector 存放仍开放；`PASS_WITH_CHANGES`。 |
| `docs/check/submissions/EVENT_CROSS_REVIEW_05.md` | Event Plane 静态基线 + R0–R6 运行时方向 R5 审查：静态基线 PASS；运行时 T-B1–T-B5 未闭合，需先定 owner/状态/预算/handler 契约。 |
| `docs/check/submissions/EVENT_CROSS_REVIEW_06.md` | Event Plane R6 HAOFV 符合性复审：静态基线 PASS；T-B1–T-B5 已文档修正；U-M1–U-M8（event_id/ABORT/target/契约/预算/日志）仍开放。 |
| `docs/check/submissions/EVENT_CROSS_REVIEW_07.md` | Event Plane R7 设计终审：静态基线 + 方案 B 运行时设计均符合 HAOFV；剩余为实现、契约登记、TDMA 联审和 C11。 |
| `docs/check/submissions/REFMEM_UNIFIED_CONTROL_REVIEW_01.md` | RefMem 单端口统一控制面三缺口 R1：三条 canonical 已落位；config-control profile、ARMING、MOUNT 兼容、command handler/队列仍待闭合。 |
| `docs/check/submissions/REFMEM_UNIFIED_CONTROL_REVIEW_02.md` | RefMem 四条款 R2：单板保留 + 统一面增量；ARMING DISARM、STOPPING 超时、未注册 command、mask 回退已补。 |
| `docs/check/submissions/REFMEM_UNIFIED_CONTROL_REVIEW_03.md` | RefMem C0/C1a 收口：命令槽物理放置冻结（static 为规范位置）+ REFTMEM_CMD 四板全目标投递/解析/ACK、三类解析器与拒绝负测（build 20261023121304）；C1 转发/C4/C5 仍开放，建议 `PASS_WITH_OPEN_ITEMS`。 |
| `docs/check/submissions/REFMEM_HAOFV_REVIEW_01.md` | RefMem 四条款 HAOFV 偏差 R1：预算符号、config-control subtype、session writer、ActionDispatcher 归属、本地/协调者边界、实时性已修。 |
| `docs/check/submissions/REFMEM_C11_REVIEW_01.md` | `REFMEM-CTRL-01` 独立交叉复核（C11）：5 满足/2 部分/1 未满足（接入点无关），N8.5 功能成立但证据不可复跑；结论 `PASS_WITH_CHANGES`，阻断 pending→active。 |
| `docs/check/submissions/REFMEM_C11_REVIEW_02.md` | `REFMEM-ENTRY-01` 拆分复核（C11 第二轮）：拆分方向忠实但 CTRL-01 旧入口子句未移交，形成同语义双源；单口/命令 id 证据仅作者自注；结论 `PASS_WITH_CHANGES`，ENTRY-01 不得转 active。 |
| `docs/check/submissions/REFMEM_C11_REVIEW_03.md` | `REFMEM-ENTRY-01` 整改复审（C11 第三轮）：5 项必改 2 闭环/3 部分，clause 与 check 仍不等价（入口 `NOT_CONTROL_OWNER` vs reason 12、四域/`FUNC_*` 单口证据缺项）；结论 `PASS_WITH_CHANGES`，ENTRY-01 不得转 active，CTRL-01 保持 pending。 |
| `docs/check/submissions/REFMEM_C11_REVIEW_04.md` | `REFMEM-ENTRY-01` N9 收尾文本复核（C11 第四轮）：QUICK+HIL 定级一致、不冒充 full；C11-03 五项 2 闭环/2 部分/1 未闭环；结论 `PASS_WITH_CHANGES`，ENTRY-01 不得转 active，CTRL-01 保持 pending。 |
| `docs/check/submissions/REFMEM_C11_REVIEW_05.md` | `REFMEM-ENTRY-01` 终审（C11 第五轮）：三项整改 2 闭环/1 部分（登记行过期句未删、TODO 旧 reason 12 口径），快照 P3/单口证据已回正；结论 `PASS_WITH_CHANGES`，ENTRY-01 暂不得转 active，CTRL-01 保持 pending。 |
| `docs/check/submissions/REFMEM_C11_REVIEW_06.md` | N10.2 环停远端命令 fail-closed 修复独立复核（C11 第六轮）：SCPI/submit 双层拒绝成立、拒绝先于记录写入，HIL 环停拒绝 + 环起 `ack=14`；必改 3 项（状态/条款文本、build+ring 证据行、submit 层覆盖与拒绝可观测）；结论 `PASS_WITH_CHANGES`，N10.2 有条件闭环。 |
| `docs/check/submissions/ARCH_FLASH_CROSS_REVIEW_01.md` | Flash v2 首次层间核验：硬件/构建/实现/域文档一致性与当前 v1 偏差。 |
| `docs/check/submissions/VDC_CROSS_REVIEW_01.md` | VDC 服务边界频率命令的 wire、owner、实际应用 ACK 及取消规则 C11 核验。 |
| `docs/check/submissions/VDC_CROSS_REVIEW_02.md` | VDC 同命令原有效期内有界重复运输、ACK 退休与至多一次应用 C11 核验。 |
| `docs/check/submissions/VDC_CROSS_REVIEW_03.md` | VDC 独立 RATE 观测、显式 AUTO 与逐从多轮频率闭环 C11 核验。 |
| `docs/check/submissions/VDC_CROSS_REVIEW_04.md` | VDC Core1 typed 同步邮箱直接发布、运行代际和 owner 边界核验。 |
| `docs/check/submissions/ARCH_FLASH_CROSS_REVIEW_02.md` | Flash canonical 结构重构核验：契约语义迁移、M0-M6 工作板和 v1 偏差复查。 |
| `docs/check/submissions/ARCH_FLASH_CROSS_REVIEW_03.md` | Direct A/B slot/vector/hash/signature/compatibility fault matrix 与 Recovery 只读投影 C11 核验。 |
| `docs/check/submissions/TRIGGER_CROSS_REVIEW_01.md` | T2 位置切片跨域契约 `ARCH-TRIGGERSLICE-01` 的 C11 独立交叉核验：10 B wire 布局、尺寸静态断言、容器（10 B RefMem 子区）、owner `TriggerRegion[n]`、每帧一条记录、帧级 CRC 归属与布局版本耦合归属；结论 APPROVE WITH NOTES，三项措辞/归属缺陷已修正。 |
| `docs/check/submissions/TRIGGER_CROSS_REVIEW_02.md` | T2 位置邮箱跨域契约 `ARCH-TRIGGERMAILBOX-01` 的 C11 独立交叉核验：单生产者/单消费者四态邮箱、`READY` release/acquire 配对、载荷读写态与静默边界复位例外、FREEZE latch 归属、以及「位置」域三方口径（15 / 3 / 3）不一致；结论 APPROVE WITH NOTES，四项措辞/归属缺陷已修正，域口径收敛另立 `T2-IDENT-002`。 |
| `docs/check/submissions/TRIGGER_CROSS_REVIEW_03.md` | T2 一等席运输的 **TDMA 域联审**（关闭 `T2-AUDIT-03` 第三项）：10 B 单帧布局/越界/静态断言与轮换、「机会不空过」机制均成立，`SYNC_TRIGGER` 相位预算获独立重算确认；结论**有条件接受**，登记行三处表述与一处悬空契约引用已修正，跨域条件 C1–C5（TDMA 席位条款/布局版本递增/预算比例矛盾/期限口径、arch 文档引用已删除符号）仅登记为指针。 |
| `docs/check/submissions/DOCS_CROSS_REVIEW_01.md` | agent 约束收敛的 C11 独立交叉核验：`tools/**` 与 `.agents/**/*.py` 归 host、契约 id 全量校验、`DRAFT_CLAUSE_DEBT` 限时债务、§8 模板迁入 EXE §7；初审 `APPROVE-WITH-FIXES` → 复审 `PASS_WITH_NOTE`，4 项必修功能面闭环，minor 残留随 M2/M3 处理。 |
| `docs/check/submissions/CALIBRATION_GUIDE_CROSS_REVIEW_01.md` | 单口引导期校准流程（10 kHz 载体）HAOFV 双审：v1 判 `REJECT`（9 BLOCKER / 13 MAJOR），v2 逐条处置，待复审；未登记、未改 registry。 |
| `docs/check/submissions/ARCH_ALGORITHM_REUSE_CROSS_REVIEW_01.md` | 校验原语合并 + P3 自检修复的 C11 交叉核验：`components/algo/inc/algo_checksum.h` 为已迁移站点唯一实现，逐字节等价经独立复核；审查 11 项（1 流程 BLOCKER、2 MAJOR）已逐条处置；结论 `ACCEPT_WITH_DEVIATION`，C11 `PASS_WITH_NOTE`（host 凭证由接受会话重生成）。 |
| `docs/check/submissions/ARCH_CONCEPT_DUPLICATION_CROSS_REVIEW_01.md` | 重复概念合并/归档计划的 C11 交叉核验：A1–A11 合并、B1 归档、C 保持；评审判 4 处 A 项需修订、A10 不合并、批次重排，`REFMEM-ACTION-01` 由 pending 改 superseded；结论 `ACCEPT_WITH_DEVIATION`。 |
| `docs/trigger/t2/TRIGGER_T2_HANDOFF_CHECKLIST.md` | T2 会话交接与自审清单：环境事实、已闭环项与可核验处、立即要做的 3 项、流程铁律与自审命令（原「用户偏好」小节已于 2026-10-13 移除，索引随之更正）。 |
| `docs/trigger/t2/TRIGGER_T2_DEBUG_PLAYBOOK.md` | T2 调试手册：测量纪律、失败定位的固定顺序、已踩坑位（症状→根因→修法）、决策规则、工具配方、网关预装两组的精确设计。 |
| `docs/interface/SCPI_HOST_ARCHITECTURE.md` | 接口域架构：上位机与多板系统的通信边界、单串口 + RefMem 分布式控制拓扑与待冻结项。 |
| `docs/interface/SCPI_HOST_TODO.md` | 接口域待办：会话与寻址、命令转发与应答、隔离与权限、可观测性（IF-001..IF-010，含 2026-10-20 评审转办）。 |
| `docs/interface/SCPI_HOST_TASK_PROGRESS.md` | 接口域任务进度：建域与现状取证（节点作用域命令仅 4 条等实测）。 |
| `README.md` | 本索引文件，提供当前 `docs/` 文件归属。 |
| `docs/README.md` | 文档治理域 README。 |
| `arch/README.md` | 架构域 README。 |
| `interface/README.md` | 接口域 README。 |
| `trigger/README.md` | 触发域 README。 |
| `sync/README.md` | 同步域 README。 |
| `calibration/README.md` | 校准域 README。 |
| `tdma/README.md` | TDMA 基础件主域 README。 |
| `tdma/TDMA_DOMAIN_ARCHITECTURE.md` | TDMA 总体架构评审入口：按代码说明实时预算、自主飞行、typed 快速通道与 HAOFV owner 边界。 |
| `tdma/TDMA_RUNTIME_CONSTRAINTS.md` | TDMA 运行接口细则：wire/CRC、异步准备、特等席 IRQ、拍级预算、STOP/ARM 和分级验收。 |
| `calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md` | 校准域维护多板 SPI CLK 训练、双向测量、residence/bias/path-delay 证据和 EtherCAT DC 风格门禁。 |
| `calibration/CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md` | 校准训练子域的 CS marker cut-through、DATA codeword 相关、单跳收敛和 TDMA 接入方案。 |
| `calibration/CALIBRATION_RING_AUTOCALIBRATION_PLAN.md` | 校准域 P0 环序搜索、P1--P3 板内单指令自校准、SCPI 候选接口、状态机和实施待办。 |
| `calibration/CALIBRATION_DOMAIN_TODO.md` | 校准域分阶段待办、跨域边界、验收门禁和八节点扩展准备。 |
| `calibration/CALIBRATION_TASK_PROGRESS.md` | 校准域方案、粗捕获、编码测距和双向测距的任务记录与证据规则。 |
| `calibration/CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md` | 单口引导期（10 kHz 载体）C/T/R/Q/S 五段流程、跨域 owner 边界、失败归属与验收判据（Draft，未登记）。 |
| \calibration/CALIBRATION_HANDOVER_PLAN.md\ | 校准交接（2026-11-07 夜）：现状与证据、未完成项（单口启环/帧归属/首次写入/全 10MHz 回归）、资产入口、六类误判坑、起步清单。 |
| `calibration/CALIBRATION_CTRQS_CHECKLIST.md` | 单口引导 CTRQS 交接：硬约束、已入库项、三个阻塞点、下一步命令与台架注意（新会话入口）。 |
| `tdma/TDMA_DOMAIN_TODO.md` | TDMA 未完成/已完成任务及退出门禁，优先实时预算、resident 飞行和确定性交付，保留原 ID。 |
| `tdma/TDMA_TASK_PROGRESS.md` | TDMA 近期进展、失败及证据，历史从文末归档索引查找。 |
| `tdma/TDMA_TIMING_REVIEW.md` | 2026-10-09 TDMA 飞行处理与 PIO 时序冗余评审快照：重复计算/复制、预留与 guard 叠加、短槽恢复原因码误读；非契约。 |
| `tdma/TDMA_SEAT_TX_CHANNEL_REVIEW.md` | 2026-10-11 一等席独立 TX 通道方案评审（**已作废**：owner 定案快速通道唯一、两席分时复用；保留为决策过程记录与 32 B 逐字节表） |
| `tdma/TDMA_SEAT_ACCEPTANCE_REVIEW.md` | 2026-10-11 一等席验收矩阵：把"稳定性达到特等席水平"拆成 16 条可核查条目（实现落点/证据/状态）与待决事项、复核命令；仅作索引，不定义语义；非契约。 |
| `refmem/README.md` | 反射内存域 README。 |
| `refmem/REFMEM_DOMAIN_ARCHITECTURE.md` | Distributed RefMem 内部主域架构，定义 RefMem Domain 的职责边界、静态分布式应用模型和目标代码形态。 |
| `refmem/REFMEM_DOMAIN_TODO.md` | Distributed RefMem 内部主域待办，跟踪分布式应用模型、slot 契约、ACK/NACK、sync protocol 和组件化。 |
| `refmem/REFMEM_TASK_PROGRESS.md` | Distributed RefMem 内部主域任务进度。 |
| `refmem/REFMEM_UNIFIED_CONTROL_REVIEW.md` | REFMEM 统一控制面收口快照（未完成）：子项总表、证据索引、开放项。 |
| `refmem/REFMEM_SYNC_ARCHITECTURE.md` | Distributed RefMem Sync 内部架构，定义总线无关同步协议和 adapter 边界。 |
| `refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md` | Distributed RefMem 最小系统板 bring-up 记录，维护当前两板线序和验证步骤。 |
| `refmem/REFMEM_DOMAIN_RISK_REVIEW.md` | Distributed RefMem 主域风险评审，记录 P0-P3 架构偏差、HAOFV 边界风险和纠偏结论。 |
| `refmem/REFMEM_CONTROL_PLANE_REVIEW.md` | RefMem 统一控制面与触发执行方案评审稿（2026-10-21 评审通过，非契约）：控制面挂接、动作分发、MRF Event Plane、分阶段计划与 MRF 子域升级评估。 |
| `refmem/event/REFMEM_EVENT_ARCHITECTURE.md` | RefMem Event Plane 子域架构（Draft，静态域）：EventPack/Catalog/Map/Alias/EventVector；运行时事件归 RefMem/TRIGGER。 |
| `refmem/event/REFMEM_EVENT_TODO.md` | RefMem Event Plane 子域待办（静态域）：E0 静态契约、E1 静态实现、E2 别名静态映射、E3 运行时读取契约。 |
| `refmem/event/REFMEM_EVENT_TASK_PROGRESS.md` | RefMem Event Plane 子域任务进度：域建立、迁移和静态域重写记录。 |
| `vdc/README.md` | VDC 内部主域 README。 |
| `vdc/VDC_DOMAIN_ARCHITECTURE.md` | VDC 内部主域架构，定义共同时间、DPLL、timestamp、HOLDOVER 和质量门禁。 |
| `vdc/VDC_RUNTIME_CONSTRAINTS.md` | VDC 已登记运行接口细则：typed 同步、本地跟踪、输出/探针及兼容分片协议。 |
| `vdc/VDC_DOMAIN_TODO.md` | VDC 内部主域待办，跟踪数据契约、DPLL、RefMem 映射、组件化和验证。 |
| `vdc/VDC_COMMAND_TRANSPORT_PLAN.md` | VDC 定时命令与共同时间传输待审方案，含原函数反例、固定邮箱候选及资源/负测门禁。 |
| `vdc/VDC_STABLE_INPUT_PLAN.md` | VDC 稳定时间输入与丢样本处理方案；首帧优化退出锁相前置，有效样本驱动更新。 |
| `vdc/VDC_TASK_PROGRESS.md` | VDC 内部主域任务进度。 |
| `vdc/VDC_DPLL_STATUS_REVIEW.md` | 四板 DPLL 实际闭环、连续启动收敛、有限精度与发布缺口的阶段报告。 |
| `reports/distributed-trigger/DHRT100_DPLL_STATUS_REPORT_20260918.html` | 四板 DPLL 阶段报告，沿用项目 A4 竖版模板；含组成机制、连续收敛、完整方波与上升沿实测 SVG。 |
| `reports/distributed-trigger/DHRT100_DPLL_STATUS_REPORT_20260918.pdf` | 四板 DPLL 阶段报告打印版，A4 竖版七页。 |
| `reports/distributed-trigger/DHRT100_PROJECT_OVERVIEW_REPORT_20260919.html` | 项目综述更新版：A4 竖版，汇总 HAOFV、四板 DPLL、无外参长窗、外参相干性及调参实测；保留 20260827 原版。 |
| `reports/distributed-trigger/DHRT100_PROJECT_OVERVIEW_REPORT_20260919.pdf` | 项目综述更新打印版，与同日期 HTML 同步。 |
| `reports/distributed-trigger/DHRT100_DPLL_STATUS_REPORT_20260919.html` | DPLL 更新报告：A4 竖版八页，含收敛/方波、长窗、外参斜率对比、SCPI/Flash 与相干性边界。 |
| `reports/distributed-trigger/DHRT100_DPLL_STATUS_REPORT_20260919.pdf` | DPLL 更新打印版，与同日期 HTML 同步，保留 20260918 历史版本。 |
| `vdc/VDC_DOMAIN_RISK_REVIEW.md` | VDC/DPLL 主域风险评审，记录共同时间、DPLL、硬实时 capture/fire 和文档漂移风险。 |
| `communication/README.md` | 通信域 README。 |
| `measure/README.md` | 测量域 README。 |
| `storage/README.md` | 存储与证据域 README。 |
| `ota/OTA_HAOFV_ARCHITECTURE.md` | OTA 与启动域 HAOFV canonical。 |
| `hardware/README.md` | 硬件域 README。 |
| `validation/README.md` | 验证域 README。 |
| `release/README.md` | 发布域 README。 |
| `reports/README.md` | 报告输出域 README。 |
| `reports/scpi/README.md` | SCPI 报告输出 README。 |
| `reports/distributed-trigger/README.md` | 分布式触发报告输出 README。 |
| `temp/README.md` | 阶段性调试、验证和绘图归档入口。 |
| `temp/TDMA_CODE_REVIEW.md` | 2026-08-19 TDMA flight/ring adapter 代码评审单（供 Codex 修复）。 |
| `temp/HAOFV_REFRESH_PLAN.md` | 2026-08-19 HAOFV 顶层文档刷新任务单（规则 + 待办，供 Codex 执行，8-26 截止）。 |
| `legacy/README.md` | 历史资料域 README。 |
| `legacy/pinprobe/README.md` | PinProbe A1 历史资料 README。 |
| `legacy/rp1200/README.md` | RP1200 历史资料 README。 |
| `legacy/external/README.md` | 外部参考资料 README。 |
| `legacy/tdma/LEGACY_TDMA_DOMAIN_ARCHITECTURE.md` | TDMA 架构重构前历史快照，保留旧方案、迁移过程与原描述。 |
| `legacy/tdma/LEGACY_TDMA_DOMAIN_TODO.md` | TDMA 待办重构前历史快照，当前状态以主域 TODO 为准。 |
| `legacy/trigger/LEGACY_TRIGGER_T2_TODO_01.md` | T2 待办重构前历史快照（原附录 A：实现注记与探索日志）。 |
| `legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_01.md` | T2 进展归档（C14 轮转）：`T2-PROGRESS-20260926-016`..`T2-PROGRESS-20260922-001`，35 条，条目逐字保留。 |
| `legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_02.md` | T2 进展归档（C14 第二轮）：`T2-PROGRESS-20260927-017`..`T2-PROGRESS-20260927-017`，1 条；日志达 200 KB 上限后最早条目迁出，条目逐字保留。 |
| `legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_03.md` | T2 进展归档（C14 第三轮）：`T2-PROGRESS-20260927-020`..`T2-PROGRESS-20260927-018`，3 条，条目逐字保留。 |
| `legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_04.md` | T2 进展归档（C14 第四轮）：`T2-PROGRESS-20261010-023`..`T2-PROGRESS-20261009-021`，3 条，条目逐字保留。 |
| `legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_05.md` | T2 进展归档（C14 第五轮）：`T2-PROGRESS-20261012-001`..`T2-PROGRESS-20261010-024`，5 条，条目逐字保留。 |
| `legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_06.md` | T2 进展归档（C14 第六轮）：`T2-PROGRESS-20261013-001`..`T2-PROGRESS-20261013-001`，1 条，条目逐字保留。 |
| `legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_07.md` | T2 进展归档（C14 第七轮）：`T2-PROGRESS-20261014-003`..`T2-PROGRESS-20261013-002`，4 条，条目逐字保留。 |
| `legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_08.md` | T2 进展归档（C14 第八轮）：`T2-PROGRESS-20261015-003`..`T2-PROGRESS-20261014-004`，6 条，条目逐字保留。 |
| `legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_09.md` | T2 进展归档（C14 第九轮）：`T2-PROGRESS-20261016-007`..`T2-PROGRESS-20261015-004`，10 条，条目逐字保留。 |
| `legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_10.md` | T2 进展归档（C14 第十轮）：`T2-PROGRESS-20261016-016`..`T2-PROGRESS-20261016-008`，9 条，条目逐字保留。 |
| `legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_11.md` | T2 进展归档（C14 第十一轮）：`T2-PROGRESS-20261018-034`..`T2-PROGRESS-20261017-020`，13 条，条目逐字保留。 |
| `legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_12.md` | T2 进展归档（C14 第十一轮）：`T2-PROGRESS-20261018-034`..`T2-PROGRESS-20261017-020`，13 条，条目逐字保留。 |
| `trigger/t2/TRIGGER_T2_RUNTIME_CONSTRAINTS.md` | T2 运行时约束：启动期 PIO 资源准入、越界判定与验证证据分级。 |
| `legacy/arch/LEGACY_HAOFV_FLASH_TASK_PROGRESS_01.md` | HAOFV_FLASH 进展归档：`FLASH-TASK-20260821-001`..`FLASH-TASK-20260821-001`，3 条。 |
| `legacy/arch/LEGACY_HAOFV_FLASH_TASK_PROGRESS_02.md` | HAOFV_FLASH 进展归档：`FLASH-TASK-20260822-004`..`FLASH-TASK-20260822-002`，4 条。 |
| `legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_01.md` | REFMEM 进展归档：`REFMEM-TASK-20260813-021`..`REFMEM-TASK-20260813-001`，21 条。 |
| `legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_02.md` | REFMEM 进展归档：`REFMEM-TASK-20260814-039`..`REFMEM-TASK-20260813-022`，20 条。 |
| `legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_03.md` | REFMEM 进展归档：`REFMEM-TASK-20260814-049`..`REFMEM-TASK-20260814-040`，10 条。 |
| `legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_04.md` | REFMEM 进展归档：`REFMEM-TASK-20260815-006`..`REFMEM-TASK-20260814-050`，10 条。 |
| `legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_05.md` | REFMEM 进展归档：`REFMEM-TASK-20260815-016`..`REFMEM-TASK-20260815-007`，10 条。 |
| `legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_06.md` | REFMEM 进展归档：`REFMEM-TASK-20260815-036`..`REFMEM-TASK-20260815-017`，20 条。 |
| `legacy/tdma/LEGACY_TDMA_TASK_PROGRESS_04.md` | TDMA 进展归档：`TDMA-PROGRESS-20260914-008`..`TDMA-TASK-20260817-001`，10 条。 |
| `legacy/tdma/LEGACY_TDMA_TASK_PROGRESS_05.md` | TDMA 进展归档：`TDMA-PROGRESS-20260917-001`..`TDMA-PROGRESS-20260914-009`，7 条。 |
| `legacy/vdc/LEGACY_VDC_TASK_PROGRESS_03.md` | None：`VDC-PROGRESS-20260917-011`..`VDC-PROGRESS-20260917-011`，1 条。 |
| `legacy/vdc/LEGACY_VDC_TASK_PROGRESS_14.md` | None：`VDC-PROGRESS-20260919-034`..`VDC-PROGRESS-20260919-030`，6 条。 |
| `legacy/vdc/LEGACY_VDC_TASK_PROGRESS_15.md` | None：`VDC-PROGRESS-20260921-038`..`VDC-PROGRESS-20260919-035`，5 条。 |
| `legacy/vdc/LEGACY_VDC_TASK_PROGRESS_16.md` | None：`VDC-PROGRESS-20260922-042`..`VDC-PROGRESS-20260921-039`，4 条。 |
| `legacy/tdma/LEGACY_TDMA_TASK_PROGRESS_03.md` | TDMA 进展归档：`TDMA-PROGRESS-20260913-067`..`TDMA-PROGRESS-20260913-030`，38 条。 |
| `legacy/tdma/LEGACY_TDMA_TASK_PROGRESS_02.md` | TDMA 进展归档：`TDMA-PROGRESS-20260912-029`..`TDMA-PROGRESS-20260912-001`，29 条。 |
| `legacy/tdma/LEGACY_TDMA_TASK_PROGRESS_01.md` | TDMA 进展归档：`TDMA-PROGRESS-20260911-007`..`TDMA-TASK-20260817-001`，51 条；重复历史编号别名见归档头。 |
| `legacy/vdc/README.md` | VDC 三件标准文件重构前历史快照入口。 |
| `legacy/vdc/LEGACY_VDC_DOMAIN_ARCHITECTURE.md` | VDC 重构前架构历史快照。 |
| `legacy/vdc/LEGACY_VDC_DOMAIN_TODO.md` | VDC 重构前待办历史快照。 |
| `legacy/vdc/LEGACY_VDC_TASK_PROGRESS.md` | VDC 重构前任务进度历史快照。 |
| `legacy/vdc/LEGACY_VDC_TASK_PROGRESS_09.md` | VDC C14 archive: `VDC-PROGRESS-20260918-006`..`VDC-PROGRESS-20260918-004`, 3 unchanged entries. |
| `legacy/vdc/LEGACY_VDC_TASK_PROGRESS_13.md` | VDC C14 archive: `VDC-PROGRESS-20260919-029`..`VDC-PROGRESS-20260919-015`, 15 unchanged entries. |
| `legacy/vdc/LEGACY_VDC_TASK_PROGRESS_12.md` | VDC C14 archive: `VDC-PROGRESS-20260919-014`..`VDC-PROGRESS-20260919-007`, 8 unchanged entries. |
| `legacy/vdc/LEGACY_VDC_TASK_PROGRESS_11.md` | VDC C14 archive: `VDC-PROGRESS-20260919-006`..`VDC-PROGRESS-20260918-034`, 25 unchanged entries. |
| `legacy/vdc/LEGACY_VDC_TASK_PROGRESS_10.md` | VDC C14 archive: `VDC-PROGRESS-20260918-033`..`VDC-PROGRESS-20260918-007`, 27 unchanged entries. |
| `legacy/vdc/LEGACY_VDC_TASK_PROGRESS_08.md` | VDC C14 archive: `VDC-PROGRESS-20260918-003`..`VDC-PROGRESS-20260917-031`, 4 unchanged entries. |
| `legacy/vdc/LEGACY_VDC_TASK_PROGRESS_07.md` | VDC C14 archive: `VDC-PROGRESS-20260917-030`..`VDC-PROGRESS-20260917-025`, 6 unchanged entries. |
| `legacy/vdc/LEGACY_VDC_TASK_PROGRESS_06.md` | VDC 进展日志 C14 归档（`VDC-PROGRESS-20260917-024`..`VDC-PROGRESS-20260917-023`，2 条），旧条目逐字保留。 |
| `legacy/vdc/LEGACY_VDC_TASK_PROGRESS_05.md` | VDC 进展日志 C14 归档（`VDC-PROGRESS-20260917-022`..`VDC-PROGRESS-20260917-022`，1 条），旧条目逐字保留。 |
| `legacy/vdc/LEGACY_VDC_TASK_PROGRESS_04.md` | VDC 进展日志 C14 归档（`VDC-PROGRESS-20260917-021`..`VDC-PROGRESS-20260917-012`，9 条），旧条目逐字保留。 |
| `legacy/vdc/LEGACY_VDC_TASK_PROGRESS_02.md` | VDC 进展日志 C14 轮转归档段（`VDC-PROGRESS-20260917-010`..`VDC-PROGRESS-20260917-001`，10 条），旧条目逐字保留。 |
| `legacy/vdc/LEGACY_VDC_TASK_PROGRESS_01.md` | VDC 进展日志 C14 轮转归档段（`VDC-PROGRESS-20260916-043`..`VDC-PROGRESS-20260906-002`，146 条，按新鲜度倒排），条目逐字保留。 |
| `archive/README.md` | 归档域 README。 |

## 01 系统架构

| 文件 | 定位 |
|---|---|
| `arch/HAOFV_ARCHITECTURE.md` | HAOFV 顶层产品架构主文档，阐述组件约束、层次逻辑和约束传播，不直接冻结硬件 pin map。 |
| `arch/HAOFV_FLASH_ARCHITECTURE.md` | 板载 Flash v2 架构，定义 FlashMap、事务 owner、Boot/Direct A/B、NVS/blob/FCB 与 TDMA 流式 OTA。 |
| `arch/HAOFV_FLASH_TODO.md` | Flash v2 M0-M6 实施工作板，按依赖组织 Map/owner、Stores、Boot、本地 OTA、TDMA OTA 和量产门禁。 |
| `arch/HAOFV_FLASH_TASK_PROGRESS.md` | Flash v2 阶段任务进度、提交、验证缺口、板端部署状态和下一 gate。 |
| `arch/ARCH_T2_RESERVATION_ARCHITECTURE.md` | 已弃用（2026-10-11）：HAOFV 下 T2 预约的历史跨域入口；现行口径见 `trigger/t2/TRIGGER_T2_ARCHITECTURE.md` 与 `trigger/t2/TRIGGER_T2_STEP_REVIEW.md`，实现细项分别落入 Trigger、VDC、TDMA、RefMem 和 SYNC_IO。 |
| `arch/ARCH_ACTIVATION_LOAD_ARCHITECTURE.md` | Activation/Load 跨域主线：staging→校验→gate→激活→回滚的一致性装配，含三类加载术语与代码对账快照；非新 owner，细项下沉各域。 |
| `arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md` | 实时负载 phase/owner/四级 seat 挂载规范；明确 T2 使用 `SYNC_TRIGGER` 与一等 T2 position transport，不进入 VDC/DPLL phase。 |
| `arch/HAOFV_MAINTENANCE_TODO.md` | HAOFV 架构符合性维护待办，跟踪 owner、AO/FB/Vector、反射内存和硬实时边界偏差。 |
| `arch/HAOFV_VDC_DPLL_ARCHITECTURE.md` | HAOFV 下 VDC/DPLL 既有融合架构输入；VDC 主域 canonical 见 `vdc/VDC_DOMAIN_ARCHITECTURE.md`。 |
| `arch/ARCH_PRODUCT_ARCHITECTURE.md` | 产品化系统架构特化，统一 DHRT100 产品目标、双核 AMP、Vector/Blackboard、四板分布式、维护域和发布门禁。 |
| `arch/HAOFV_IMPLEMENTATION_PLAYBOOK.md` | HAOFV 实施补充、示例和历史迁移说明；不作为硬件资源 canonical。 |
| `arch/HAOFV_PORTABILITY_EVALUATION.md` | HAOFV 可移植性评估快照，用于识别平台耦合和迁移风险。 |
| `arch/HAOFV_ARCHITECTURE_RISK_EVALUATION.md` | HAOFV 顶层架构独立风险评估快照，记录 S0-S3 分级风险、事实校正和处置去向。 |
| `arch/ARCH_RAM_REVIEW.md` | 面向 DPLL/VDC 调试的 SRAM 余量评审与建议（RAM-01～RAM-11）：当前余量已到链接硬底、按子系统的占用与消耗归因、已落地核实的优化、剩余回收与一致性修正条目。容量与增量均为快照，不作为冻结契约或代码事实源。 |
| `reports/distributed-trigger/RTOS_DISTRIBUTED_TRIGGER_0614_REPORT.html` | 0614 分布式触发完整原始报告，已从外部 DOC 迁入。 |
| `arch/RTOS_HAOFV_ARCHITECTURE.md` | 基于 HAOFV 的 RTOS + 双核 AMP 架构，整合任务划分、OSAL 移植、双核边界和 0614 摘要。 |
| `arch/RTOS_HAOFV_TODO.md` | 基于 HAOFV 的 RTOS 实施待办事项。 |
| `arch/RTOS_HAOFV_TASK_PROGRESS.md` | RTOS / 双核 / 分布式触发任务进度和闭环验证记录。 |
| `reports/distributed-trigger/相控阵测试系统RP分布式触发方案技术报告0804.md` | 0804 分布式触发报告的仓库内摘要入口。 |
| `reports/distributed-trigger/相控阵测试系统RP分布式触发方案技术报告0804.html` | 0804 RP 分布式触发完整原始报告，已从外部 DOC 迁入。 |
| `reports/distributed-trigger/DISTRIBUTED_HARD_REALTIME_TRIGGER_OVERVIEW_REPORT.html` | Distributed Hard Real-Time Trigger System 3-4 页概述报告。 |
| `archive/TASK_PROGRESS.md` | 全局历史任务进度和跨域迁移记录。 |

## 02 硬件与资源约束

| 文件 | 定位 |
|---|---|
| `sync/SYNC_IO_ARCHITECTURE.md` | SYNC_IO / realtime IO 架构、PIO/DMA/IRQ 资源、语义 IO、mode driver 和同步链路边界。 |
| `hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md` | 当前运行/调试最小系统板约束，用于软件架构、小步烧录和闭环验证。 |
| `hardware/HARDWARE_PRODUCT_BOARD_CONSTRAINTS.md` | 产品板硬件约束入口，由最新产品网表、IO 约束和生产输出派生。 |
| `hardware/PRODUCT_BOARD_MIGRATION_PLAN.md` | 产品样板迁移方案与待办，静态映射到固件构建到样板实测三层收口。 |
| `hardware/RP2350B_QFN80_IO_CONSTRAINTS.md` | RP2350B QFN-80 硬件版本 GPIO 分配与 IO 使用约束。 |
| `hardware/Netlist_CTL-SYNCTRIG4F4-HASL_2026-08-13.tel` | 最新产品板网表，是产品硬件约束刷新和网表评审的事实来源。 |
| `communication/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md` | BiSS-C TAP Bridge、RJ45、SYNC_IO、AUX 两收两发和外围电路约束。 |

## 03 触发与 SYNC_IO

| 文件 | 定位 |
|---|---|
| `sync/SYNC_IO_ARCHITECTURE.md` | SYNC_IO / realtime IO 架构入口。 |
| `sync/SYNC_IO_TODO.md` | SYNC_IO / realtime IO 待办，覆盖 PIO 预约输出、真实 transport、AUX 语义通道和 mode self-test。 |
| `trigger/TRIGGER_SYNC_TODO.md` | 触发系统生产化待办。 |
| `trigger/TRIGGER_FOUR_BOARD_DISTRIBUTED_PLAN.md` | RP2350B 四板分布式触发方案。 |
| `trigger/t2/TRIGGER_T2_ARCHITECTURE.md` | T2 实施架构草案：多板槽位部署、预约身份、READY/fence、VDC 映射与实际回读边界。 |
| `trigger/t2/TRIGGER_T2_TODO.md` | 多板实际槽位部署后开展 T2 预约触发的任务、依赖与验收。 |
| `trigger/t2/TRIGGER_T2_TASK_PROGRESS.md` | T2 分支参考、切片实现、验证、失败与交接记录。 |
| `trigger/t2/TRIGGER_T2_REVIEW.md` | 2026-10-09 T2 代码与架构语义评审快照：偏离项、实际状态对账、文档漂移与建议顺序；2026-10-18 增补四轮 sequence 独立审核，2026-10-20/21 增补代码级复审、停摆修复复核、基础模型/pilot 实现复核与开关 mode 2 复核（网关计数 PASS、开关 service/码字/时序缺口）；非契约。 |
| `trigger/t2/TRIGGER_T2_STEP_REVIEW.md` | 2026-10-11 T2 复核汇总：状态对账、predictor/runtime/wire 与分支角度前端两份审计的核对、31 位置/STEP/身份/模拟源影响与待裁定决定清单；非契约。 |
| `trigger/t2/T2_FLOW.html` | T2 起链工作流 A4 视图：由 `tools/sync_trigger/t2_workflow.py` 注入最近一次运行的逐步结果；数据快照，非事实源。 |
| `trigger/t2/DPLL60_FLOW.html` | DPLL60 工作流 A4 视图：由 `tools/vdc_priority_trace/DPLL60_workflow.py` 注入最近一次运行的逐步结果；不是验收凭证。 |
| `trigger/t2/T2_BRINGUP_FLOW.svg` | T2 起链工作流流程图（`t2_workflow.py` 的 22 步与配置/挂载/启动三分）；所注耗时与状态为绘制时快照，非事实源。 |
| `trigger/TRIGGER_SEQ_STEP_DESIGN.md` | 序列步进触发模式设计。 |
| `trigger/TRIGGER_ENC_COUNT_DESIGN.md` | 编码器计数触发模式设计。 |
| `trigger/TRIGGER_PULSE_COUNT_ANALYSIS.md` | 脉冲计数分析。 |
| `trigger/TRIGGER_INDUSTRIAL_ENHANCEMENT_DESIGN.md` | 工业级触发增强方案。 |
| `sync/SYNC_IO_TASK_PROGRESS.md` | SYNC_IO / Trigger 同步重构任务进度和闭环验证记录。 |

## 04 BiSS-C

| 文件 | 定位 |
|---|---|
| `communication/BISSC_TAP_BRIDGE_DESIGN.md` | BiSS-C TAP Bridge 协议、模式和固件架构主设计。 |
| `communication/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md` | BiSS-C 相关硬件和 SYNC_IO 外围电路约束。 |
| `communication/BISSC_IMPLEMENTATION_TODO.md` | BiSS-C 实现待办，按 P0/P1/P2 细分。 |
| `communication/BISSC_TASK_PROGRESS.md` | BiSS-C 任务进度、闭环验证和决策记录。 |
| `communication/COMMUNICATION_RS485_ARCHITECTURE.md` | RS485 owner、半双工生命周期和 OTA ingress 架构。 |
| `communication/COMMUNICATION_RS485_TODO.md` | RS485 host、固件和 DHRT100 联调待办。 |
| `communication/COMMUNICATION_RS485_TASK_PROGRESS.md` | RS485 提交、构建、COM11 和 DHRT100 证据。 |

## 05 OTA 与启动

| 文件 | 定位 |
|---|---|
| `arch/HAOFV_FLASH_ARCHITECTURE.md` | 当前板载 Flash/Boot/OTA 跨域 canonical，v2 分区与 owner 以此为准。 |
| `arch/HAOFV_FLASH_TODO.md` | Flash/OTA/TDMA stream v2 依赖工作板与证据矩阵。 |
| `ota/OTA_HAOFV_ARCHITECTURE.md` | OTA 域当前稳定架构。 |
| `ota/OTA_TODO.md` | OTA 域当前任务状态与退出门禁。 |
| `ota/OTA_TASK_PROGRESS.md` | OTA 域实现、验证、失败与回退证据。 |
| `validation/P3_FLOW.html` | 硬件验收（P3）37 步工作流 A4 视图：由 `tools/hardware_acceptance/hardware_acceptance_workflow.py` 注入最近一次运行的逐步结果、调用与凭证资格；视图本身不是验收凭证。 |
| `validation/VALIDATION_HARDWARE_ACCEPTANCE_PLAYBOOK.md` | 硬件验收操作手册：前置检查、标准命令、续跑/重试边界、失败处置表与收口判据，避免其他会话误跑。 |
| `ota/OTA_FLOW.html` | 四板刷写工作流 A4 视图（OTA / picotool / TDMA 流式预留）：由 `tools/ota/OTA_workflow.py` 注入最近一次运行；不是验收凭证。 |
| `legacy/ota/README.md` | OTA 历史文档索引。 |
| `legacy/ota/OTA_SYSTEM_DESIGN.md` | v1 OTA 系统设计历史快照。 |
| `legacy/ota/OTA_AB_SWITCH_DESIGN.md` | Direct A/B 迁移历史设计。 |
| `legacy/ota/OTA_COPY_TRANSACTION_DESIGN.md` | Copy-to-active 事务历史设计。 |
| `legacy/ota/OTA_PORTABLE_ARCHITECTURE.md` | Portable OTA 历史架构。 |
| `legacy/ota/OTA_OPEN_SOURCE_COMPARISON.md` | OTA 开源方案历史比较。 |
| `legacy/ota/OTA_LIBRARY_MIGRATION_PLAYBOOK.md` | Portable OTA 迁移历史记录。 |
| `legacy/pinprobe/LEGACY_PINPROBEA1_OTA_CAN_DISTRIBUTION_ARCHITECTURE.md` | PinProbe A1 OTA 固件升级与 CAN 多机分发历史方案，作为当前 OTA/多机分发参考。 |

## 06 存储与 SD

| 文件 | 定位 |
|---|---|
| `storage/SD_TODO.md` | SD 文件系统、StorageAO 和持久化观测层设计/待办。 |
| `storage/SD_TASK_PROGRESS.md` | SD 域任务进度和验证记录。 |

## 07 诊断、日志与 SCPI

| 文件 | 定位 |
|---|---|
| `interface/SCPI_COMMANDS.md` | SCPI 命令清单、语义和边界。 |
| `interface/SCPI_COMMAND_PLAN.md` | DHRT100 仪器式 SCPI 指令规划方案，定义主线挂载、SYNC/VDC/DPLL 层级和后续收敛步骤。 |
| `interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md` | RP1200 / DHRT100 产品级 SCPI 指令表 Markdown 源文档，面向上位机联调和协议冻结。 |
| `reports/scpi/RP1200波导天线测试系统分布式触发方案SCPI指令表.html` | RP1200 / DHRT100 产品级 SCPI 指令表 HTML 版，当前与 Markdown 0.7 同步。 |
| `storage/LOG_SYSTEM_TODO.md` | 日志系统待办和演进方向。 |
| `interface/SCPI_USB_INTERFACE_DESIGN.md` | USB CDC/USBTMC 接口设计、描述符、bus-powered/self-powered 决策记录。 |
| `interface/SCPI_TASK_PROGRESS.md` | SCPI 指令框架、接口拆分、USB 验证和产品指令闭环记录。 |
| `interface/SCPI_HAOFV_REVIEW.md` | 2026-10-20 接口域 HAOFV 符合性评审：实现面（阻塞/直驱/空实现）与契约面（命令树/摘要/错误码）不符合项、证据和处置顺序；非契约。 |

## 08 发布、验证与全局进度

| 文件 | 定位 |
|---|---|
| `release/RELEASE_CHECKLIST.md` | 发布门禁检查表。 |
| `archive/TASK_PROGRESS.md` | 全局历史任务进度。新域建议使用 `<DOMAIN>_TASK_PROGRESS.md`。 |
| `communication/BISSC_NETWORK_LOOPBACK_PLAYBOOK.md` | 2026-08-11 BiSS 组网 preflight 处理流程和跨电脑继续工作交接记录。 |

### 验证工具入口

| 工具 | 定位 |
|---|---|
| `tools/rtos/multicore_board_validate/multicore_board_validate.py` | 单板 RTOS + multicore + 表查询 smoke。 |
| `tools/communication/distributed_loopback_validate/distributed_loopback_validate.py` | BiSS 组网 HIL preflight：A3 是唯一外部 COM 入口，其他板作为内部 peer 通过 BiSSC 组网。 |

## 09 历史方案与外部资料迁入

| 文件 | 定位 |
|---|---|
| `legacy/pinprobe/LEGACY_PINPROBEA1_RAM_REFLECTIVE_MEMORY_ARCHITECTURE.md` | PinProbe A1 多机协同与 RAM 反射内存历史方案，当前分布式 Vector/命令槽设计复用其原则。 |
| `legacy/pinprobe/LEGACY_PINPROBEA1_OTA_CAN_DISTRIBUTION_ARCHITECTURE.md` | PinProbe A1 OTA 固件升级与 CAN 多机分发历史方案，当前 OTA/SD/System Pack 方案可参考其分片、ACK 和本地校验边界。 |
| `legacy/pinprobe/PinProbe A1控制箱 嵌入式整体方案与架构报告.html` | PinProbe A1 控制箱整体方案原始 HTML 报告。 |
| `legacy/pinprobe/PinProbe A1 箱体控制 SCPI 指令说明20260728.html` | PinProbe A1 SCPI 指令原始参考。 |
| `reports/distributed-trigger/RTOS_DISTRIBUTED_TRIGGER_0614_REPORT.html` | 0614 分布式触发完整原始报告。 |
| `reports/distributed-trigger/相控阵测试系统RP分布式触发方案技术报告0804.html` | 0804 RP 分布式触发完整原始报告。 |
| `reports/distributed-trigger/相控阵测试系统RP分布式触发方案技术报告0804.md` | 0804 分布式触发报告仓库内摘要。 |

## 10 评估与监督（evaluation 子域）

| 文件 | 定位 |
|---|---|
| `evaluation/README.md` | 评估与监督域 README：域定位、边界、长期追踪规则与验证命令。 |
| `evaluation/PRODUCT_VALUE_EVALUATION.md` | 项目价值评估 canonical：上游评估(R)/证据(E)→判断(J)→结论(C)→行动(A) 单链结论（开源/商业/学术三维）。 |
| `evaluation/PRODUCT_VALUE_TRACKING_TODO.md` | 评估行动 A1–A7 状态追踪 TODO（唯一状态事实源，配合评估文档 §4）。 |

## 快速查找规则

- 查系统边界：先读 `arch/HAOFV_ARCHITECTURE.md`。
- 查开源/商业/学术价值评估与行动追踪：读 `evaluation/README.md`，再读 `evaluation/PRODUCT_VALUE_EVALUATION.md` 与 `evaluation/PRODUCT_VALUE_TRACKING_TODO.md`。
- 查通用治理规范/跨项目 AI 工作流：读 `standards/README.md`，再读 `standards/STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.md`。
- 查 VDC/DPLL：先读 `vdc/VDC_DOMAIN_ARCHITECTURE.md`，再读 `arch/HAOFV_VDC_DPLL_ARCHITECTURE.md`；底层 IO 链路见 `sync/SYNC_IO_ARCHITECTURE.md`。
- 查上/下行 TDMA 与环路基础件：先读 `tdma/TDMA_DOMAIN_ARCHITECTURE.md`，再读 `refmem/REFMEM_SYNC_ARCHITECTURE.md` 和 `vdc/VDC_DOMAIN_ARCHITECTURE.md` 的消费边界。
- 查当前运行板约束：读 `hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md`。
- 查产品板约束：读 `hardware/HARDWARE_PRODUCT_BOARD_CONSTRAINTS.md`，再读最新网表和 IO 约束。
- 查 IO/PIO 资源和 realtime IO 边界：先读 `sync/SYNC_IO_ARCHITECTURE.md`。
- 查 SYNC_IO 当前待办：先读 `sync/SYNC_IO_TODO.md`。
- 查 BiSS-C：先读 `communication/BISSC_TAP_BRIDGE_DESIGN.md` 和
  `communication/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`。
- 查命令：先读 `interface/SCPI_COMMANDS.md`。
- 查待办：优先读对应域的 `*_TODO.md`。
- 查验证记录：优先读对应域的 `*_TASK_PROGRESS.md`。
- `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_03.md` ? VDC progress archive 03.

| [`docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_14.md`](docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_14.md) | VDC 任务进度归档（C14 轮转，20260919-034..20260919-030） |
| [`docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_15.md`](docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_15.md) | VDC 任务进度归档（C14 轮转，VDC-PROGRESS-20260921-038..VDC-PROGRESS-20260919-035） |
| [`docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_16.md`](docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_16.md) | VDC 任务进度归档（C14 轮转，VDC-PROGRESS-20260922-042..VDC-PROGRESS-20260921-039） |
