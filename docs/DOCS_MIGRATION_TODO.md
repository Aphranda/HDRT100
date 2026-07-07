# 文档体系迁移待办

Status: Active
Domain: Documentation
Canonical: `docs/DOCS_MIGRATION_TODO.md`
Related: `docs/README.md`, `docs/DOCS_NAMING_AND_STRUCTURE.md`
Last updated: 2026-07-07

本文档跟踪 `docs/` 从历史平铺文档向统一命名、统一入口、统一元数据演进的待办。
迁移过程优先保证引用不失效，不追求一次性大规模改名。

## P0 必须完成

- [x] P0-01 建立文档总入口：新增 `docs/README.md`，按领域列出当前文件归属。
- [x] P0-02 建立命名与层级规则：新增 `docs/DOCS_NAMING_AND_STRUCTURE.md`。
- [x] P0-03 根 README 接入文档入口：在 Key Configuration Files 中加入
  `docs/README.md` 和 `docs/DOCS_NAMING_AND_STRUCTURE.md`。
- [x] P0-04 核心主文档补齐元数据：至少覆盖 HAOFV、PIO、SYNC_IO、BiSS-C、
  OTA、SD、SCPI、Release。
- [ ] P0-05 新增文档强制遵守命名规则：新文件使用
  `<DOMAIN>_<SUBJECT>_<TYPE>.md`，并加入 `docs/README.md`。

## P1 应该完成

- [ ] P1-01 为每个领域明确 canonical 主文档：
  `ARCH/HAOFV`、`SYNC_IO`、`TRIGGER`、`BISSC`、`OTA`、`SD`、`LOG`、`SCPI`。
- [ ] P1-02 把全局进度逐步收敛：新任务优先写入对应
  `<DOMAIN>_TASK_PROGRESS.md`，减少继续追加到 `TASK_PROGRESS.md`。
- [ ] P1-03 为大型 TODO 文档补齐 P0/P1/P2 验收标准，避免只有任务标题。
- [ ] P1-04 检查 `docs/` 内部交叉引用风格：同目录文档优先使用 `<FILE>.md`，
  根 README 使用 `docs/<FILE>.md`。
- [ ] P1-05 为硬件约束类文档补齐冻结状态、适用硬件版本和未决项。
- [x] P1-06 为剩余历史文档补齐元数据：
  `BISSC_IMPLEMENTATION_TODO.md`、`BISSC_TASK_PROGRESS.md`、
  `DISTRIBUTED_DPLL_SYNC_DESIGN.md`、`HAOFV_PORTABILITY_EVALUATION.md`、
  `HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE_SUPPLEMENT.md`、
  `OTA_AB_SWITCH_DESIGN.md`、`OTA_COPY_TRANSACTION_DESIGN.md`、
  `OTA_LIBRARY_MIGRATION_PLAYBOOK.md`、`OTA_OPEN_SOURCE_COMPARISON.md`、
  `OTA_TODO.md`、`PORTABLE_OTA_ARCHITECTURE.md`、`SYNC_TRIGGER_TODO.md`、
  `TASK_PROGRESS.md`、`TASK_PROGRESS_SD.md`、
  `TRIGGER_PULSE_COUNT_ANALYSIS.md`、`TRIGGER_SEQ_STEP_MODE.md`。

## P2 可以延后

- [ ] P2-01 评估是否将 `OTA方案.md` 迁移为 `OTA_SYSTEM_DESIGN.md` 或
  `OTA_ARCHITECTURE.md`。
- [ ] P2-02 评估是否将 `TASK_PROGRESS_SD.md` 迁移为 `SD_TASK_PROGRESS.md`。
- [ ] P2-03 评估 HAOFV 主文档是否保持现名，或迁移为更短的
  `HAOFV_ARCHITECTURE.md`。
- [ ] P2-04 当某个领域文档超过 12 个且引用稳定后，再评估是否引入
  `docs/<domain>/` 子目录。
- [ ] P2-05 增加脚本化检查：文件名、元数据、索引覆盖和 Markdown 引用扫描。

## 迁移约束

- 不在同一次改动中混合功能代码重构和大规模文档移动。
- 文件改名必须先列迁移表，再一次性更新 README、docs、tools 和脚本内引用。
- 历史中文文件名在迁移前仍视为有效文档，不创建内容重复的新文档。
- 已冻结硬件约束文档只允许勘误、补充版本和消除冲突，不再引入备选方向。
