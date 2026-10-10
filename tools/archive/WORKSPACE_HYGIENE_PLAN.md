# 工作区整理完善规划（out/ 与 tools/）

Last updated: 2026-10-10

> 本文件综合 `out/` 与 `tools/` 两个工作区的治理规则、迁移计划与执行批次，作为后续整理的**唯一总纲**。
> 细则见 `out/README.md`、`tools/README.md`、`out/archive/OUT_ARCHIVE_MANIFEST.md`、
> `tools/archive/REFERENCE_AUDIT.md`、`tools/archive/TOOLS_MIGRATION_PLAN.md`。

## 0. 目标与范围

**目标**：让 `out/`（构建产物 + 验收证据）与 `tools/`（脚本与工具）**可发现、可审计、可回滚**，
杜绝"肆意生长"，且**不打断任何文档引用与门禁**。

**范围**：`out/**`、`tools/**`（含两者的 `archive/`）。不含 `docs/**` 的语料规则、不含源码目录。

**非目标**：不重写历史证据；不改变 P3/文档门禁口径；不删任何文件。

## 1. 现状基线（2026-10-10 实测）

| 项 | 现状 |
|---|---|
| `out/` 顶层 | 6 项（`archive/`、`build/`、`HardwareAcceptance/`、`p3/`、`quick/`、`.gitkeep`）——已整理 |
| `out/archive/` | `cold/`（10 类 88 项）、`deletable-candidates/`（4 类 170 项）、`loose-logs/`（156）、`pending-deletion/`（6） |
| `out/build/` | 4 个构建树（保留：`dpll60-latest`、凭证引用的 `p3-acceptance-20261009-232805`；另 2 个因占用未移动） |
| `tools/` 顶层 | **113 项**，其中 **111 项被引用**（审计见 `tools/archive/REFERENCE_AUDIT.md`） |
| `tools/scratch/` | 44 项（42 个草稿脚本 + README + .gitkeep），已按 .gitignore 忽略 |
| `tools/archive/deletable-candidates/` | 2 项（`__pycache__`、`t2_deploy_action_probe`） |
| 事故记录 | 2026-10-10 策略变更前一次批量清理**已删除** `out/build/` 下约 350 个构建树（可重建缓存，非证据）；作业即时停止 |

## 2. 治理原则（不可协商）

1. **不删除**：agent 只做移动与归档；删除由用户确认后执行。
2. **引用安全**：任何移动前做引用审计（`docs/**`、`config/**`、`components/**`、`application/**`、
   `middleware/**`、`linker/**`、`.githooks/**`、`CMakeLists.txt`、`tool_registry*.json`、`tool_layout.json`），
   同步更新全部引用；结果记入审计文件。
3. **证据优先**：验收证据与凭证引用的产物**原位保留**，只允许冷/热分层，不允许移动出原域。
4. **职责单一**：`out/` 只管构建产物与验收证据（**不存脚本**）；`tools/` 是脚本唯一归属地。
5. **审计留痕**：每次整理更新对应清单（动作 / 规模 / 例外 / 未决），并保留"旧 → 新"映射。
6. **分批可回滚**：每批次独立提交；失败用 `git mv` 回位或提交回退。

## 3. 规则文件（已完成）

| 文件 | 作用 |
|---|---|
| `out/README.md` | `out/` 管理规则（保留项、落位约定、归档结构、删除候选、禁止事项） |
| `out/archive/OUT_ARCHIVE_MANIFEST.md` | `out/` 归档清单与事故记录、未决事项 |
| `tools/README.md` §目录治理规则 | `tools/` 治理（扁平布局有意、新增须登记、草稿区、归档、不删除、移动前审计） |
| `tools/archive/REFERENCE_AUDIT.md`、`tools/_ref_audit.json` | `tools/` 引用审计（方法与 111/113 结论） |
| `tools/archive/TOOLS_MIGRATION_PLAN.md` | `tools/` 迁移细则（领域优先分类、兼容层、批次、验收、回滚） |
| **本文件** | 两者综合的总纲 |

## 4. 目标态

### 4.1 `out/`（已达目标态，剩 3 项收尾）

```text
out/
  README.md                  规则
  HardwareAcceptance/  p3/  quick/          证据（原位）
  build/dpll60-latest/  build/p3-acceptance-<凭证>/   保留构建树
  archive/
    OUT_ARCHIVE_MANIFEST.md
    cold/<域类别>/           冷数据（长期保留）
    loose-logs/              日志归集
    deletable-candidates/    ← 用户删除入口
    pending-deletion/        既有待删区
```

### 4.2 `tools/`（目标：顶层只剩"禁止移动集 + 域目录"）

**禁止移动集**：`docs_check/`、`doc_regression_check.py`、`checks/`、`hardware_acceptance/`、
`tool_governance/`、`common/`、`domains/`、`workflows/`、`tests/`、`bench/`、`archive/`、`scratch/`、
`flash_map/`、`tool_registry*.json`、`tool_layout.json`、`README.md`。

**域目录（领域优先，与 `docs/<域>` 对齐）**：
`calibration/`（~6）、`flash/`（~7）、`dpll/`（~7）、`vdc/`（~9）、`refmem/`（~11）、`tdma/`（~3）、
`trigger/`（~4）、`sync_io/`（~7）、`storage/`（~7）、`scpi/`（~6）、`ota/`（~14）、`analyzers/`（~6）、
`monitors/`（~3）、`project/`（~6）。逐项映射见 `TOOLS_MIGRATION_PLAN.md` §1。

**兼容层**：每个被移动目录在旧路径留 3–5 行转发入口（`runpy` 转发），
确保 `docs/**`、脚本、门禁中的旧路径继续可用；引用归零后 wrapper 移入 `tools/archive/`。

## 5. 执行计划（分批）

### P0 — 已完成（2026-10-10）
- `out/` 整理完毕（6 顶层项；归档 10 类 88 项；删除候选 170 项）。
- `tools/` 治理规则 + 引用审计 + 迁移计划成文；2 个未引用项归档。
- 相关提交：`aac7e8a8`、`f3fd9557`、`fb381dab`、`b42942e2`、`0408bfc6`、`d6d29ee0`。

### P1 — `out/` 收尾（3 项，低风险）
| 项 | 动作 | 验证 |
|---|---|---|
| P1-1 | `out/build/` 中 2 个移动失败目录（`sm-res-007-runtime-conflicts-20260903` 与其 `-final`）：按 Windows ACL 排障后**移动**到 `archive/cold/misc/` | 移动后可读；无引用 |
| P1-2 | `HardwareAcceptance/` 冷/热分层：>30 天移入 `archive/cold/misc/acceptance-<年月>/`（保留 `diagnostic.json`/`alarms.json`/凭证相关目录在原处） | 凭证引用目录不动；抽样可读 |
| P1-3 | 根目录剩余 `*.json/*.txt/*.md` 散件（19 项）移入 `archive/cold/notes|probes/` | 移动后 `out/` 顶层仍 6 项 |

### P2 — `tools/` 迁移（B0 → B7）
| 批次 | 范围 | 交付物 | 验证 | 回滚 |
|---|---|---|---|---|
| **B0** | 零移动 | `tool_registry.json` 增 `category`+`kind` 并登记全部工具；生成 `tools/INDEX.md` | 文档门禁 + registry schema | 回退提交 |
| **B1** | `calibration/` + `flash/`（~13） | 目录归位 + wrapper | 引用更新后跑 `docs_check`/`doc_regression_check`；抽样 3 命令实跑 | `git mv` 回位 |
| **B2** | `dpll/` + `vdc/`（~16） | 同上 | 抽样跑 `dpll_observation_decode`、`vdc_long_monitor --dry-run` | 同上 |
| **B3** | `refmem/` + `tdma/` + `trigger/`（~18） | 同上 | 抽样跑 `refmem_table_registry_validate`、`tdma_ring_monitor` | 同上 |
| **B4** | `sync_io/` + `storage/` + `scpi/`（~20） | 同上 | 抽样跑 `two_board_io_validate`、`sd_board_validate` | 同上 |
| **B5** | `ota/`（~14） | 同上 | **必跑 `p3_hardware_acceptance.py run`** + `ota_bin_info`/`uf2_join` 冒烟 | 同上 |
| **B6** | `analyzers/` + `monitors/` + `project/`（~15） | 同上 | 文档门禁 + `cmake_build_auto` 冒烟 | 同上 |
| **B7** | wrapper 清理 | 引用归零的 wrapper 移入 `tools/archive/`；更新审计 | 重审计：旧路径引用 = 0 | 移回 wrapper |

**顺序理由**：B0 零风险先做；自低引用密度域到高；涉及固件/OTA 凭证的 B5 强制 P3；纯工程工具最后。

### P3 — 收尾与验收
- `tools/INDEX.md`、`tools/README.md`、`REFERENCE_AUDIT.md`、`TOOLS_MIGRATION_PLAN.md` 与本文件保持一致。
- 每批次按仓库约定：文档改动与工具改动**分提交**；改固件/工具实现需 P3 凭证。
- 最终由用户确认删除 `out/archive/deletable-candidates/` 与 `tools/archive/deletable-candidates/`。

## 6. 职责边界

| 角色 | 可做 | 不可做 |
|---|---|---|
| Agent（我） | 移动、归档、建索引与审计、更新引用与规则文件、跑门禁与 P3、提交推送 | **删除任何文件**；移动被引用的目录而不更新引用；改动他人并行会话的未提交改动 |
| 用户 | 确认删除候选；决定冷热分层阈值；批准迁移批次 | — |

## 7. 风险登记

| 风险 | 影响 | 缓解 |
|---|---|---|
| 文档门禁对路径真实性的校验 | 迁移批次被阻断 | 每批先跑 `docs_check --strict-names` + `doc_regression_check` 再合入 |
| 隐藏引用（字符串拼接/动态 import） | 功能静默失效 | 审计按"目录名子串"匹配；B5 追加全量 P3 |
| 文件被占用导致移动失败 | 批次不完整 | 跳过并记入审计；下次重试；不改用删除 |
| 并行会话同时在改树 | 提交互相污染 | 只 stage 自己的文件；提交前核对 `git status`；发现他人改动时报告不合并 |
| 历史可追溯性稀释 | 审计困难 | wrapper + "旧→新"映射表常驻审计文件 |

## 8. 完成定义（DoD）

1. `out/` 顶层恒为 6 项；`tools/` 顶层只剩禁止移动集 + 域目录。
2. `tool_registry.json` 全量带 `category`/`kind`；`tools/INDEX.md` 可按域检索。
3. 引用审计显示旧路径引用为 0（wrapper 除外），且 wrapper 已归入 `tools/archive/`。
4. 每批次均有：移动清单、引用更新清单、门禁结果、抽样实跑证据、回滚路径；B5 附 P3 凭证。
5. 两份清单（`OUT_ARCHIVE_MANIFEST.md`、`REFERENCE_AUDIT.md`）与"旧→新"映射完整。
6. 用户确认后，两个 `deletable-candidates/` 被清空（由用户执行）。

## 9. 立即下一步

**P2-B0（零移动、零风险）**：给 `tool_registry.json` 增 `category`/`kind` 并登记全部工具，
生成 `tools/INDEX.md`。完成后即具备按域检索能力，再按 B1…B7 逐批迁移。
