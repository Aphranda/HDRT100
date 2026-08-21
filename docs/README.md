# 文档索引

Status: Active
Domain: Documentation
Canonical: `docs/README.md`
Related: `docs/docs/DOCS_NAMING_STRUCTURE_PLAN.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-21

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
  docs/           文档治理、命名规则、迁移表
  arch/           产品架构、HAOFV、RTOS 和分布式总纲
  interface/      SCPI、USB、USBTMC、命令表、上位机接口
  trigger/        产品触发、序列、角度、core1 实时执行
  sync/           SYNC 动作、SYNC_IO、同步链路落地
  calibration/    CAL link、delay、参数、版本、质量
  tdma/           上行/下行 TDMA、payload registry、adapter 和环路 completion 基础件
  refmem/         分布式向量表、命令槽、ACK/NACK、节点事实
  vdc/            虚拟 DC、共同时间、DPLL、HOLDOVER、时间质量
  communication/  BiSS-C、UART、RS485、RJ45 后端维护
  measure/        测量原语、T2 摘要、链路 delay 测量服务
  storage/        SD、StorageAO、日志、trace、snapshot、报告证据
  ota/            OTA、boot、A/B、回滚、System Pack
  hardware/       IO 约束、PCB、网表、BOM、Gerber、硬件评审
  validation/     HIL、工具验证、闭环验证记录和脚本说明
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
| `arch/` | `arch/README.md` | 产品架构、HAOFV、RTOS 和分布式总纲 |
| `interface/` | `interface/README.md` | SCPI、USB、USBTMC、命令表和上位机接口 |
| `trigger/` | `trigger/README.md` | 产品触发、序列、角度、core1 实时执行 |
| `sync/` | `sync/README.md` | SYNC 动作、SYNC_IO、同步链路和硬实时 IO 落地 |
| `calibration/` | `calibration/README.md` | CAL link、delay、参数、版本和质量 |
| `tdma/` | `tdma/README.md` | 上行/下行 TDMA、payload registry、adapter、ring runtime 和 completion evidence 基础件 |
| `refmem/` | `refmem/README.md` | 分布式向量表、命令槽、ACK/NACK、同步架构和最小系统板操作 |
| `vdc/` | `vdc/README.md` | 虚拟 DC、共同时间、DPLL、HOLDOVER 和时间质量 |
| `communication/` | `communication/README.md` | BiSS-C、UART、RS485 和通信维护 |
| `measure/` | `measure/README.md` | 测量原语、T2 摘要和链路 delay 测量服务 |
| `storage/` | `storage/README.md` | SD、StorageAO、日志、trace、snapshot 和报告证据 |
| `ota/` | `ota/README.md` | OTA、boot、A/B、回滚和 System Pack |
| `hardware/` | `hardware/README.md` | IO 约束、PCB、网表、BOM、Gerber 和硬件评审 |
| `validation/` | `validation/README.md` | HIL、工具验证、任务进度和闭环验证记录 |
| `release/` | `release/README.md` | 发布门禁、打印/PDF、产品冻结 checklist |
| `reports/` | `reports/README.md` | 当前产品 HTML/PDF 输出、打印和交付件 |
| `temp/` | `temp/README.md` | 阶段性调试、验证和绘图归档 |
| `legacy/` | `legacy/README.md` | PinProbe、历史报告、最初版 HTML 和外部迁入资料 |
| `archive/` | `archive/README.md` | 废弃路径说明、旧索引和批量迁移记录 |

## Canonical 主文档

| 领域 | 当前 canonical 主文档 | 说明 |
|---|---|---|
| ARCH/HAOFV | `arch/HAOFV_ARCHITECTURE.md` | 顶层 HAOFV 架构入口，定义组件约束、层次逻辑、Vector/Blackboard 和约束传递。 |
| ARCH/T2 | `arch/ARCH_T2_RESERVATION_ARCHITECTURE.md` | T2 预约与分布式时钟分发跨域主线，定义训练、VDC 映射、flight 分发、fence、本地执行和 completion。 |
| VDC | `vdc/VDC_DOMAIN_ARCHITECTURE.md` | VDC 内部主域架构，定义共同时间事实、同步 DPLL、HOLDOVER、时间质量和预测分发时间基准。 |
| ARCH/VDC-DPLL | `arch/HAOFV_VDC_DPLL_ARCHITECTURE.md` | 既有 HAOFV VDC/DPLL 融合架构输入；后续逐步迁入 VDC canonical。 |
| ARCH/PRODUCT | `arch/ARCH_PRODUCT_ARCHITECTURE.md` | 面向 Distributed Hard Real-Time Trigger System 的产品系统架构特化，服从 HAOFV 顶层约束。 |
| ARCH/FUTURE | `arch/ARCH_FUTURE_APPLICATION_PLAN.md` | 当前产品完成后的应用场景、跨平台移植、版本分层和开源生态路线图。 |
| SYNC_IO | `sync/SYNC_IO_ARCHITECTURE.md` | PIO、GPIO、DMA、语义 IO 和硬实时资源约束入口。 |
| TDMA | `tdma/TDMA_DOMAIN_ARCHITECTURE.md` | TDMA 基础件主域，定义上/下行 TDMA、payload registry、adapter、ring runtime、completion evidence 和 HAOFV system node 边界。 |
| TRIGGER | `trigger/TRIGGER_SYNC_TODO.md` | 触发业务模式、生产化缺口和跨模式待办入口。 |
| BISSC | `communication/BISSC_TAP_BRIDGE_DESIGN.md` | BiSS-C 协议、TAP bridge、固件 persona 和验证边界入口。 |
| OTA | `OTA_SYSTEM_DESIGN.md` | 历史 OTA 主方案入口；后续迁移方向见 `docs/docs/DOCS_NAMING_STRUCTURE_PLAN.md`。 |
| SD | `storage/SD_TODO.md` | SD、StorageAO、System Pack、快照和持久化观测入口。 |
| LOG | `storage/LOG_SYSTEM_TODO.md` | 日志 core、诊断 trace、持久化和故障证据入口。 |
| SCPI | `interface/SCPI_COMMANDS.md` | SCPI 命令语义、兼容性和用户可调用接口入口。 |
| USB | `interface/SCPI_USB_INTERFACE_DESIGN.md` | USB CDC、USBTMC/USB488、VISA 枚举和供电描述符策略。 |

## 进度记录路由

| 领域 | 当前进度入口 | 规则 |
|---|---|---|
| BiSS-C | `communication/BISSC_TASK_PROGRESS.md` | BiSS-C 新任务记录写入本文件。 |
| SYNC_IO | `sync/SYNC_IO_TASK_PROGRESS.md` | SYNC_IO / Trigger 同步重构闭环记录写入本文件。 |
| RTOS | `arch/RTOS_HAOFV_TASK_PROGRESS.md` | RTOS / 双核 / 分布式触发任务闭环记录写入本文件。 |
| SCPI | `interface/SCPI_TASK_PROGRESS.md` | SCPI 指令框架、验证脚本和接口拆分闭环记录写入本文件。 |
| SD | `storage/SD_TASK_PROGRESS.md` | SD / StorageAO / System Pack 新任务记录写入本文件。 |
| Documentation | `docs/docs/DOCS_MIGRATION_TODO.md` | 文档治理和迁移记录写入本文档体系待办。 |
| TDMA | `tdma/TDMA_TASK_PROGRESS.md` | TDMA 基础件、上/下行 runtime、adapter 和环路闭环验证记录写入本文件。 |
| 其他领域 | 新建或补齐 `<DOMAIN>_TASK_PROGRESS.md` | 后续新闭环记录优先建立领域进度文件，不再追加到全局历史文件。 |
| 全局历史 | `TASK_PROGRESS.md` | 只保留跨域历史和迁移前记录；除跨域总览外不再作为默认新任务入口。 |

## 00 文档治理

| 文件 | 定位 |
|---|---|
| `docs/docs/DOCS_NAMING_STRUCTURE_PLAN.md` | 文档命名格式、层级关系、新增文件规则和迁移规则。 |
| `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md` | 按产品主域目录化管理的目标结构、迁移批次、域映射和 gate。 |
| `docs/docs/DOCS_MIGRATION_TODO.md` | 文档体系迁移待办，跟踪元数据补齐、历史改名和索引维护。 |
| `docs/check/DOCS_REGISTRY.md` | 文档契约登记表 + 条款落点表（自回归体系唯一事实源，配合 `tools/doc_regression_check.py`）。 |
| `docs/check/DOCS_REGRESSION_PLAN.md` | 文档自回归体系实施规格（v3 归档版，源方案）。 |
| `docs/check/DOCS_REGRESSION_TODO.md` | 文档自回归体系实施待办，跟踪 T1-T15 执行状态与每步反馈。 |
| `docs/check/DOCS_REGRESSION_REVIEW.md` | 文档自回归体系实施经验总结（问题清单 + 解法 + 维护建议）。 |
| `docs/check/submissions/README.md` | 核验提交单归档目录说明 + 模板（层间逐级核验，C11 交叉审核）。 |
| `docs/check/submissions/TDMA_CROSS_REVIEW_01.md` | 首份核验提交单：HAOFV-879 seqlock 偏差（ACCEPT_WITH_DEVIATION，2026-08-19）。 |
| `README.md` | 本索引文件，提供当前 `docs/` 文件归属。 |
| `docs/README.md` | 文档治理域 README。 |
| `arch/README.md` | 架构域 README。 |
| `interface/README.md` | 接口域 README。 |
| `trigger/README.md` | 触发域 README。 |
| `sync/README.md` | 同步域 README。 |
| `calibration/README.md` | 校准域 README。 |
| `tdma/README.md` | TDMA 基础件主域 README。 |
| `tdma/TDMA_DOMAIN_ARCHITECTURE.md` | TDMA 基础件架构，定义上/下行 TDMA、payload registry、adapter、ring runtime 和 HAOFV 边界。 |
| `calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md` | 校准域维护多板 SPI CLK 训练、双向测量、residence/bias/path-delay 证据和 EtherCAT DC 风格门禁。 |
| `calibration/CALIBRATION_RING_AUTOCALIBRATION_PLAN.md` | 校准域 P0 环序搜索、P1--P3 板内单指令自校准、SCPI 候选接口、状态机和实施待办。 |
| `calibration/CALIBRATION_DOMAIN_TODO.md` | 校准域分阶段待办、跨域边界、验收门禁和八节点扩展准备。 |
| `calibration/CALIBRATION_TASK_PROGRESS.md` | 校准域方案、粗捕获、编码测距和双向测距的任务记录与证据规则。 |
| `tdma/TDMA_DOMAIN_TODO.md` | TDMA 基础件待办，跟踪 runtime、reliability、system node、adapter 和 HIL 验收。 |
| `tdma/TDMA_TASK_PROGRESS.md` | TDMA 基础件任务进度。 |
| `refmem/README.md` | 反射内存域 README。 |
| `refmem/REFMEM_DOMAIN_ARCHITECTURE.md` | Distributed RefMem 内部主域架构，定义 RefMem Domain 的职责边界、静态分布式应用模型和目标代码形态。 |
| `refmem/REFMEM_DOMAIN_TODO.md` | Distributed RefMem 内部主域待办，跟踪分布式应用模型、slot 契约、ACK/NACK、sync protocol 和组件化。 |
| `refmem/REFMEM_TASK_PROGRESS.md` | Distributed RefMem 内部主域任务进度。 |
| `refmem/REFMEM_SYNC_ARCHITECTURE.md` | Distributed RefMem Sync 内部架构，定义总线无关同步协议和 adapter 边界。 |
| `refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md` | Distributed RefMem 最小系统板 bring-up 记录，维护当前两板线序和验证步骤。 |
| `refmem/REFMEM_DOMAIN_RISK_REVIEW.md` | Distributed RefMem 主域风险评审，记录 P0-P3 架构偏差、HAOFV 边界风险和纠偏结论。 |
| `vdc/README.md` | VDC 内部主域 README。 |
| `vdc/VDC_DOMAIN_ARCHITECTURE.md` | VDC 内部主域架构，定义共同时间、DPLL、timestamp、HOLDOVER 和质量门禁。 |
| `vdc/VDC_DOMAIN_TODO.md` | VDC 内部主域待办，跟踪数据契约、DPLL、RefMem 映射、组件化和验证。 |
| `vdc/VDC_TASK_PROGRESS.md` | VDC 内部主域任务进度。 |
| `vdc/VDC_DOMAIN_RISK_REVIEW.md` | VDC/DPLL 主域风险评审，记录共同时间、DPLL、硬实时 capture/fire 和文档漂移风险。 |
| `communication/README.md` | 通信域 README。 |
| `measure/README.md` | 测量域 README。 |
| `storage/README.md` | 存储与证据域 README。 |
| `ota/README.md` | OTA 与启动域 README。 |
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
| `archive/README.md` | 归档域 README。 |

## 01 系统架构

| 文件 | 定位 |
|---|---|
| `arch/HAOFV_ARCHITECTURE.md` | HAOFV 顶层产品架构主文档，阐述组件约束、层次逻辑和约束传播，不直接冻结硬件 pin map。 |
| `arch/ARCH_T2_RESERVATION_ARCHITECTURE.md` | HAOFV 下 T2 预约与分布式时钟分发主线；各 owner 的实现细项分别落入 Trigger、VDC、TDMA、RefMem 和 SYNC_IO。 |
| `arch/HAOFV_MAINTENANCE_TODO.md` | HAOFV 架构符合性维护待办，跟踪 owner、AO/FB/Vector、反射内存和硬实时边界偏差。 |
| `arch/HAOFV_VDC_DPLL_ARCHITECTURE.md` | HAOFV 下 VDC/DPLL 既有融合架构输入；VDC 主域 canonical 见 `vdc/VDC_DOMAIN_ARCHITECTURE.md`。 |
| `arch/ARCH_PRODUCT_ARCHITECTURE.md` | 产品化系统架构特化，统一 DTC100 产品目标、双核 AMP、Vector/Blackboard、四板分布式、维护域和发布门禁。 |
| `arch/HAOFV_IMPLEMENTATION_PLAYBOOK.md` | HAOFV 实施补充、示例和历史迁移说明；不作为硬件资源 canonical。 |
| `arch/HAOFV_PORTABILITY_EVALUATION.md` | HAOFV 可移植性评估快照，用于识别平台耦合和迁移风险。 |
| `arch/HAOFV_ARCHITECTURE_RISK_EVALUATION.md` | HAOFV 顶层架构独立风险评估快照，记录 S0-S3 分级风险、事实校正和处置去向。 |
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

## 05 OTA 与启动

| 文件 | 定位 |
|---|---|
| `ota/OTA_SYSTEM_DESIGN.md` | 现有 OTA 主方案历史文件。后续迁移建议见命名规则文档。 |
| `ota/OTA_TODO.md` | OTA 产品化待办。 |
| `ota/OTA_AB_SWITCH_DESIGN.md` | Direct A/B 切换设计。 |
| `ota/OTA_COPY_TRANSACTION_DESIGN.md` | Copy-to-active 掉电恢复事务设计。 |
| `ota/OTA_PORTABLE_ARCHITECTURE.md` | Portable OTA 架构和复用方案。 |
| `ota/OTA_OPEN_SOURCE_COMPARISON.md` | OTA 开源方案对比。 |
| `ota/OTA_LIBRARY_MIGRATION_PLAYBOOK.md` | Portable OTA 库化迁移 playbook。 |
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
| `interface/SCPI_COMMAND_PLAN.md` | DTC100 仪器式 SCPI 指令规划方案，定义主线挂载、SYNC/VDC/DPLL 层级和后续收敛步骤。 |
| `interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md` | RP1200 / DTC100 产品级 SCPI 指令表 Markdown 源文档，面向上位机联调和协议冻结。 |
| `reports/scpi/RP1200波导天线测试系统分布式触发方案SCPI指令表.html` | RP1200 / DTC100 产品级 SCPI 指令表 HTML 版，当前与 Markdown 0.7 同步。 |
| `storage/LOG_SYSTEM_TODO.md` | 日志系统待办和演进方向。 |
| `interface/SCPI_USB_INTERFACE_DESIGN.md` | USB CDC/USBTMC 接口设计、描述符、bus-powered/self-powered 决策记录。 |
| `interface/SCPI_TASK_PROGRESS.md` | SCPI 指令框架、接口拆分、USB 验证和产品指令闭环记录。 |

## 08 发布、验证与全局进度

| 文件 | 定位 |
|---|---|
| `release/RELEASE_CHECKLIST.md` | 发布门禁检查表。 |
| `archive/TASK_PROGRESS.md` | 全局历史任务进度。新域建议使用 `<DOMAIN>_TASK_PROGRESS.md`。 |
| `communication/BISSC_NETWORK_LOOPBACK_PLAYBOOK.md` | 2026-08-11 BiSS 组网 preflight 处理流程和跨电脑继续工作交接记录。 |

### 验证工具入口

| 工具 | 定位 |
|---|---|
| `tools/multicore_board_validate/multicore_board_validate.py` | 单板 RTOS + multicore + 表查询 smoke。 |
| `tools/distributed_loopback_validate/distributed_loopback_validate.py` | BiSS 组网 HIL preflight：A3 是唯一外部 COM 入口，其他板作为内部 peer 通过 BiSSC 组网。 |

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

## 快速查找规则

- 查系统边界：先读 `arch/HAOFV_ARCHITECTURE.md`。
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
