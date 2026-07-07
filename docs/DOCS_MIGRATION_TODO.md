# 文档体系迁移待办

Status: Active
Domain: Documentation
Canonical: `docs/DOCS_MIGRATION_TODO.md`
Related: `docs/README.md`, `docs/DOCS_NAMING_STRUCTURE_PLAN.md`
Last updated: 2026-07-07

本文档跟踪 `docs/` 从历史平铺文档向统一命名、统一入口、统一元数据演进的待办。
迁移过程优先保证引用不失效，不追求一次性大规模改名。

## P0 必须完成

- [x] P0-01 建立文档总入口：新增 `docs/README.md`，按领域列出当前文件归属。
- [x] P0-02 建立命名与层级规则：新增 `docs/DOCS_NAMING_STRUCTURE_PLAN.md`。
- [x] P0-03 根 README 接入文档入口：在 Key Configuration Files 中加入
  `docs/README.md` 和 `docs/DOCS_NAMING_STRUCTURE_PLAN.md`。
- [x] P0-04 核心主文档补齐元数据：至少覆盖 HAOFV、PIO、SYNC_IO、BiSS-C、
  OTA、SD、SCPI、Release。
- [x] P0-05 新增文档强制遵守命名规则：新文件使用
  `<DOMAIN>_<SUBJECT>_<TYPE>.md`，并加入 `docs/README.md`。

## P1 应该完成

- [x] P1-01 为每个领域明确 canonical 主文档：
  `ARCH/HAOFV`、`SYNC_IO`、`TRIGGER`、`BISSC`、`OTA`、`SD`、`LOG`、`SCPI`。
- [x] P1-02 把全局进度逐步收敛：新任务优先写入对应
  `<DOMAIN>_TASK_PROGRESS.md`，减少继续追加到 `TASK_PROGRESS.md`。
- [x] P1-03 为大型 TODO 文档补齐 P0/P1/P2 验收标准，避免只有任务标题。
- [x] P1-04 检查 `docs/` 内部交叉引用风格：同目录文档优先使用 `<FILE>.md`，
  根 README 使用 `docs/<FILE>.md`。
- [x] P1-05 为硬件约束类文档补齐冻结状态、适用硬件版本和未决项。
- [x] P1-06 为剩余历史文档补齐元数据：
  `BISSC_IMPLEMENTATION_TODO.md`、`BISSC_TASK_PROGRESS.md`、
  `SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md`、`HAOFV_PORTABILITY_EVALUATION.md`、
  `HAOFV_IMPLEMENTATION_PLAYBOOK.md`、
  `OTA_AB_SWITCH_DESIGN.md`、`OTA_COPY_TRANSACTION_DESIGN.md`、
  `OTA_LIBRARY_MIGRATION_PLAYBOOK.md`、`OTA_OPEN_SOURCE_COMPARISON.md`、
  `OTA_TODO.md`、`OTA_PORTABLE_ARCHITECTURE.md`、`TRIGGER_SYNC_TODO.md`、
  `TASK_PROGRESS.md`、`SD_TASK_PROGRESS.md`、
  `TRIGGER_PULSE_COUNT_ANALYSIS.md`、`TRIGGER_SEQ_STEP_DESIGN.md`。

## P2 可以延后

- [x] P2-01 评估并完成 `OTA方案.md` 到 `OTA_SYSTEM_DESIGN.md` 的迁移。
- [x] P2-02 评估并完成 `TASK_PROGRESS_SD.md` 到 `SD_TASK_PROGRESS.md` 的迁移。
- [x] P2-03 评估并完成 HAOFV 主文档到 `HAOFV_ARCHITECTURE.md` 的迁移。
- [x] P2-04 当某个领域文档超过 12 个且引用稳定后，再评估是否引入
  `docs/<domain>/` 子目录。
- [x] P2-05 增加脚本化检查：文件名、元数据、索引覆盖和 Markdown 引用扫描。
- [x] P2-06 小批量迁移剩余历史 ASCII 文件名，使默认文档检查不再产生命名 warning。
- [x] P2-07 小批量迁移高风险历史入口：OTA 中文主文档和 HAOFV 长文件名。

## 迁移约束

- 不在同一次改动中混合功能代码重构和大规模文档移动。
- 文件改名必须先列迁移表，再一次性更新 README、docs、tools 和脚本内引用。
- 历史中文文件名在迁移前仍视为有效文档，不创建内容重复的新文档。
- 已冻结硬件约束文档只允许勘误、补充版本和消除冲突，不再引入备选方向。

## 重命名迁移记录

| 批次 | 状态 | 旧路径 | 新路径 | 引用影响 |
|---|---|---|---|---|
| 2026-07-07-SD-PROGRESS | 已完成 | `TASK_PROGRESS_SD.md` | `docs/SD_TASK_PROGRESS.md` | 已更新 README、docs 内引用，并从 docs 检查脚本 allowlist 移除旧历史名。 |
| 2026-07-07-NAMING-BATCH-2 | 已完成 | `DOCS_NAMING_AND_STRUCTURE.md` | `docs/DOCS_NAMING_STRUCTURE_PLAN.md` | 已更新 README、docs、tools 内引用；保持文档治理 canonical 入口。 |
| 2026-07-07-NAMING-BATCH-2 | 已完成 | `PIO_RESOURCE_PLAN.md` | `docs/SYNC_IO_RESOURCE_PLAN.md` | 已更新 README、docs、tools 内引用；保持 SYNC_IO 资源规划入口。 |
| 2026-07-07-NAMING-BATCH-2 | 已完成 | `DISTRIBUTED_DPLL_SYNC_DESIGN.md` | `docs/SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` | 已更新 README、docs、tools 内引用；归入 SYNC_IO 设计文档。 |
| 2026-07-07-NAMING-BATCH-2 | 已完成 | `PORTABLE_OTA_ARCHITECTURE.md` | `docs/OTA_PORTABLE_ARCHITECTURE.md` | 已更新 README、docs、tools 内引用；归入 OTA 架构文档。 |
| 2026-07-07-NAMING-BATCH-2 | 已完成 | `SYNC_TRIGGER_TODO.md` | `docs/TRIGGER_SYNC_TODO.md` | 已更新 README、docs、tools 内引用；归入 TRIGGER 待办。 |
| 2026-07-07-NAMING-BATCH-2 | 已完成 | `TRIGGER_ENC_COUNT_MODE.md` | `docs/TRIGGER_ENC_COUNT_DESIGN.md` | 已更新 README、docs、tools 内引用；类型后缀改为 DESIGN。 |
| 2026-07-07-NAMING-BATCH-2 | 已完成 | `TRIGGER_INDUSTRIAL_ENHANCEMENT.md` | `docs/TRIGGER_INDUSTRIAL_ENHANCEMENT_DESIGN.md` | 已更新 README、docs、tools 内引用；类型后缀改为 DESIGN。 |
| 2026-07-07-NAMING-BATCH-2 | 已完成 | `TRIGGER_SEQ_STEP_MODE.md` | `docs/TRIGGER_SEQ_STEP_DESIGN.md` | 已更新 README、docs、tools 内引用；类型后缀改为 DESIGN。 |
| 2026-07-07-NAMING-BATCH-3 | 已完成 | `OTA方案.md` | `docs/OTA_SYSTEM_DESIGN.md` | 已更新 README、docs、tools 内引用；保留 OTA 主方案 canonical 入口语义。 |
| 2026-07-07-NAMING-BATCH-3 | 已完成 | `HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE.md` | `docs/HAOFV_ARCHITECTURE.md` | 已更新 README、docs、tools 内引用；保留 HAOFV 顶层架构入口语义。 |
| 2026-07-07-NAMING-BATCH-3 | 已完成 | `HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE_SUPPLEMENT.md` | `docs/HAOFV_IMPLEMENTATION_PLAYBOOK.md` | 已更新 README、docs、tools 内引用；将补充示例和迁移步骤归入 playbook。 |

## 子目录评估记录

- 2026-07-07：本轮完成平铺文件命名迁移后，默认和 strict 文档检查均已无 warning。
  当前不引入 `docs/<domain>/` 子目录，避免在刚完成文件级重命名后继续放大引用移动面。
  后续只有当单一领域文档持续超过 12 个、引用稳定且确有导航压力时，再单独提出子目录迁移表。

## 进度与引用治理记录

- 2026-07-07：`docs/README.md` 新增“进度记录路由”，明确 BiSS-C 使用
  `BISSC_TASK_PROGRESS.md`、SD 使用 `SD_TASK_PROGRESS.md`、文档治理使用
  `DOCS_MIGRATION_TODO.md`；`TASK_PROGRESS.md` 只保留全局历史和跨域总览。
- 2026-07-07：交叉引用风格已检查。`docs/` 内部导航性引用优先使用同目录文件名；
  元数据 `Canonical` / `Related`、历史任务记录的“涉及文件”和迁移记录表保留
  `docs/<FILE>.md` 作为仓库路径引用。
