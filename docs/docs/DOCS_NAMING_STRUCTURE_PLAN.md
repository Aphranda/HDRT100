# 文档命名与层级规则

Status: Active
Domain: Documentation
Canonical: `docs/docs/DOCS_NAMING_STRUCTURE_PLAN.md`
Related: `docs/README.md`, `docs/docs/DOCS_MIGRATION_TODO.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-13

本文档定义 `docs/` 下 Markdown 文档的统一命名格式、层级关系、交叉引用规则和
后续新增文件规则。所有 Markdown 文档按 UTF-8 读取和写入。

## 基本原则

- `docs/README.md` 是文档总入口，新增文档后必须更新索引。
- 文件名使用 ASCII、全大写域名前缀、下划线分隔和明确类型后缀。
- 新文档不使用中文文件名；历史中文文件名暂不强制迁移。
- 一个文档只承担一种主职责：架构、设计、计划、TODO、进度、命令或发布检查。
- 设计文档不长期堆积任务日志；任务清单放入 `*_TODO.md`，验证记录放入
  `*_TASK_PROGRESS.md`。
- 涉及硬件 pin、PIO、DMA、SCPI、Flash layout、协议帧格式的文档，应优先写成
  表格或固定字段，避免只用自然语言描述。

## 文件名格式

新文件统一使用：

```text
<DOMAIN>_<SUBJECT>_<TYPE>.md
```

示例：

```text
BISSC_TAP_BRIDGE_DESIGN.md
SYNC_IO_REFACTOR_PLAN.md
OTA_COPY_TRANSACTION_DESIGN.md
SD_STORAGE_LAYOUT_DESIGN.md
LOG_RUNTIME_CORE_DESIGN.md
```

命名字段规则：

| 字段 | 规则 |
|---|---|
| `DOMAIN` | 领域前缀，使用全大写 ASCII，例如 `BISSC`、`SYNC_IO`、`OTA`。 |
| `SUBJECT` | 主题名，使用全大写 ASCII 和下划线，表达具体对象。 |
| `TYPE` | 文档类型后缀，必须从允许列表中选择。 |

## 领域前缀

优先使用下列表驱动的领域前缀。新增领域前缀前，应先确认是否能归入已有领域。

| 前缀 | 领域 |
|---|---|
| `ARCH` | 顶层架构、跨域架构和文档治理。 |
| `HAOFV` | HAOFV 架构专项评估、补充和迁移。 |
| `SYNC_IO` | SYNC_IO 硬件 profile、模式框架和触发 IO。 |
| `TRIGGER` | 触发业务模式、触发算法和工业增强。 |
| `BISSC` | BiSS-C 协议、TAP Bridge、波形、硬件和验证。 |
| `OTA` | OTA、Bootloader、升级包、A/B、回滚和掉电恢复。 |
| `SD` | SD 卡、StorageAO、文件系统和持久化观测。 |
| `LOG` | 日志 core、中间层、诊断日志和 trace。 |
| `SCPI` | SCPI 命令、协议入口和命令兼容性。 |
| `RTOS` | RTOS/OSAL 迁移。 |
| `MULTICORE` | RP2350 双核分工。 |
| `RELEASE` | 发布门禁、量产检查和 release 流程。 |
| `DOCS` | 文档体系、命名规则和索引。 |
| `CALIBRATION` | 校准 link、delay、参数、版本和质量。 |
| `REFMEM` | 分布式向量表、命令槽、ACK/NACK 和节点事实。 |
| `COMMUNICATION` | BiSS-C、UART、RS485 和通信维护。 |
| `MEASURE` | 测量原语、T2 摘要和链路 delay 测量服务。 |
| `HARDWARE` | IO 约束、PCB、网表、BOM、Gerber 和硬件评审。 |
| `VALIDATION` | HIL、工具验证、任务进度和闭环记录。 |
| `LEGACY` | 历史报告、外部迁入资料和冻结参考。 |

历史文件名不一定完全符合上述前缀，例如
`HAOFV_ARCHITECTURE.md` 和 `OTA_SYSTEM_DESIGN.md`。这些文件在迁移前仍按
`docs/README.md` 中的索引作为当前有效文档处理。

## 类型后缀

| 后缀 | 用途 |
|---|---|
| `_ARCHITECTURE.md` | 顶层或跨模块架构，说明边界、依赖方向和长期原则。 |
| `_DESIGN.md` | 具体功能、硬件、协议、状态机或数据结构设计。 |
| `_PLAN.md` | 阶段性实施计划、迁移计划、重构计划。 |
| `_TODO.md` | 可执行任务清单，按 P0/P1/P2 或阶段拆分。 |
| `_TASK_PROGRESS.md` | 任务进度、闭环验证、风险和决策记录。 |
| `_CHECKLIST.md` | 发布、评审、硬件冻结或验证门禁检查表。 |
| `_PLAYBOOK.md` | 可重复执行的迁移或操作手册。 |
| `_COMPARISON.md` | 方案、器件或开源项目对比。 |
| `_EVALUATION.md` | 可行性、移植性、性能或风险评估。 |
| `_COMMANDS.md` | 命令、协议入口或用户可调用接口列表。 |
| `_ANALYSIS.md` | 波形、性能、数据或问题分析。 |

不得随意发明近义后缀，例如 `_NOTE.md`、`_DOC.md`、`_SPEC.md`。确实需要新增后缀时，
先更新本文档。

## 层级关系

当前 `docs/` 正从平铺文件存放进入按产品主域目录化管理的规划阶段。目标目录、迁移批次和
gate 以 `DOCS_DOMAIN_STRUCTURE_PLAN.md` 为准。迁移完成前，根目录中的历史路径仍视为有效路径；
新增正式文档应优先按目标域选择落点，并同步更新 `docs/README.md`。

逻辑层级如下：

```text
00 文档治理
01 系统架构
02 硬件与资源约束
03 触发与 SYNC_IO
04 BiSS-C
05 OTA 与启动
06 存储与 SD
07 诊断、日志与 SCPI
08 发布、验证与全局进度
```

目标域目录如下：

```text
docs/docs/
docs/arch/
docs/interface/
docs/trigger/
docs/sync/
docs/calibration/
docs/refmem/
docs/communication/
docs/measure/
docs/storage/
docs/ota/
docs/hardware/
docs/validation/
docs/release/
docs/legacy/
docs/archive/
```

迁移到子目录必须满足：

- 先提出迁移表，列出旧路径、新路径和引用影响。
- 同一提交或同一任务内更新所有 Markdown、README、脚本内引用。
- 不混合功能代码改动和大规模文档移动。
- 迁移后保留清晰入口，避免读者必须搜索全仓库才能找到主文档。

新增文档落点按 `DOCS_DOMAIN_STRUCTURE_PLAN.md` 的“新文档落点规则”判断。文件名仍使用
`<DOMAIN>_<SUBJECT>_<TYPE>.md`，目录只表达文档归属，不替代文件名前缀。

## 文档元数据

新文档标题下方应包含固定元数据块：

```text
Status: Draft | Active | Frozen | Deprecated
Domain: <DOMAIN>
Canonical: `docs/<FILE>.md`
Related: `docs/<RELATED>.md`
Last updated: YYYY-MM-DD
```

字段含义：

| 字段 | 含义 |
|---|---|
| `Status` | 文档状态。`Draft` 草案，`Active` 当前有效，`Frozen` 已冻结只允许勘误，`Deprecated` 已废弃。 |
| `Domain` | 所属领域，必须能映射到领域前缀。 |
| `Canonical` | 当前规范入口文件。 |
| `Related` | 直接相关文档，多个文件用逗号分隔。 |
| `Last updated` | 最后一次有意义内容更新日期。 |

历史文件可以逐步补元数据，不要求一次性全部改完。

## 新增文件规则

新增文档前按以下顺序判断：

1. 能否补充到现有 canonical 文档中。
2. 是否需要独立生命周期，例如独立 TODO、独立进度、独立硬件约束。
3. 是否已有同域同类型文件，避免重复创建近义文档。
4. 文件名是否符合 `<DOMAIN>_<SUBJECT>_<TYPE>.md`。
5. 新文件是否已加入 `docs/README.md` 索引。
6. 新文件是否在相关主文档中被引用。

推荐拆分方式：

| 场景 | 建议 |
|---|---|
| 长期原则、依赖边界、架构分层 | 写入 `_ARCHITECTURE.md`。 |
| 固件功能、硬件电路、协议帧、状态机 | 写入 `_DESIGN.md`。 |
| 还没开始实现，需分阶段推进 | 写入 `_PLAN.md`。 |
| 已经决定要做，需要 P0/P1/P2 拆任务 | 写入 `_TODO.md`。 |
| 已经开始闭环，需要记录过程和结论 | 写入 `_TASK_PROGRESS.md`。 |
| 命令、接口、用户可调用入口 | 写入 `_COMMANDS.md`。 |
| 发布前必须逐项确认 | 写入 `_CHECKLIST.md`。 |

## TODO 与进度规则

`*_TODO.md` 用于还没完成的工作，推荐格式：

```text
## P0 必须完成
- [ ] P0-01 任务标题：验收标准。

## P1 应该完成
- [ ] P1-01 任务标题：验收标准。

## P2 可以延后
- [ ] P2-01 任务标题：验收标准。
```

`*_TASK_PROGRESS.md` 用于记录已经发生的工作，推荐格式：

```text
### <DOMAIN>-TASK-YYYYMMDD-NNN - 任务标题

- 目标：
- 完成：
- 验证：
- 风险：
- 后续：
- 涉及文件：
```

设计文档中可以保留简短的“未决问题”，但不应把大量任务过程堆在设计正文里。

新任务进度路由：

| 领域 | 进度入口 |
|---|---|
| BiSS-C | `BISSC_TASK_PROGRESS.md` |
| SD | `SD_TASK_PROGRESS.md` |
| 文档治理 | `DOCS_MIGRATION_TODO.md` |
| 其他领域 | 优先新建或补齐 `<DOMAIN>_TASK_PROGRESS.md` |

`TASK_PROGRESS.md` 只作为全局历史文件保留。除跨域总览或迁移前历史修正外，
新任务不再默认追加到 `TASK_PROGRESS.md`。

## 交叉引用规则

- 从仓库根 README 引用文档时，使用 `docs/<FILE>.md`。
- 从 `docs/` 内部文档引用同目录文件时，优先使用 `<FILE>.md`。
- 需要强调绝对仓库路径时，使用 `docs/<FILE>.md`。
- 元数据字段 `Canonical` / `Related`、历史任务记录中的“涉及文件”和迁移记录表，
  可以保留 `docs/<FILE>.md`，用于表达仓库路径而不是导航链接。
- 文件改名或移动时，必须用全文搜索确认引用已经同步更新。
- 禁止只在一个文档中写“见前文/见上文”而不提供文件名。

引用检查建议：

```powershell
rg -n "docs/|docs\\|<OLD_FILE>|<NEW_FILE>" README.md docs -g "*.md"
```

自动检查建议：

```powershell
python tools\docs_check\docs_check.py
python tools\docs_check\docs_check.py --strict-names
```

默认检查用于当前仓库：元数据、索引覆盖、冲突标记、`docs/` 下 Markdown 引用有效性必须通过；
当前检查递归扫描 `docs` 目录下全部 Markdown，子目录文档必须在总 README 中以相对路径列出。历史命名不规范文件只给 warning。新增文件评审时应使用 `--strict-names`，确保不再引入
新的历史债务。

## 历史文件迁移建议

下表只记录建议方向，不代表当前已经改名。

| 当前文件 | 建议方向 | 处理策略 |
|---|---|---|
| `OTA方案.md` | 已迁移为 `OTA_SYSTEM_DESIGN.md` | 2026-07-07 已完成中文历史文件名迁移并同步引用。 |
| `HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE.md` | 已迁移为 `HAOFV_ARCHITECTURE.md` | 2026-07-07 已完成 HAOFV 主文档短名迁移并同步引用。 |
| `HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE_SUPPLEMENT.md` | 已迁移为 `HAOFV_IMPLEMENTATION_PLAYBOOK.md` | 2026-07-07 已将实施补充迁移为 playbook。 |
| `TASK_PROGRESS.md` | 保留全局历史；新进度使用 `<DOMAIN>_TASK_PROGRESS.md` | 不继续把所有域进度堆到全局文件。 |
| `TASK_PROGRESS_SD.md` | 已迁移为 `SD_TASK_PROGRESS.md` | 2026-07-07 已完成小批量改名并同步引用。 |

迁移优先级：

- P0：新增文档必须遵守规则。
- P1：为核心主文档补齐元数据。
- P2：低风险历史文件按域小批量改名。

## 评审规则

新增或重构文档时，至少检查以下项：

- 文件名符合规则。
- 顶部元数据齐全。
- 所属层级已加入 `docs/README.md`。
- 相关设计、TODO、进度文件互相可追踪。
- 硬件约束、代码实现和 SCPI 命令没有语义冲突。
- 文档采用 UTF-8，中文内容没有乱码。
