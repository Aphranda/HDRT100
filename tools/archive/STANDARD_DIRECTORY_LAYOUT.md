# 标准：目录布局与命名（out/ 与 tools/）

Last updated: 2026-10-10

状态：**标准（normative）**。与 `WORKSPACE_HYGIENE_PLAN.md`（总纲）、`out/README.md`、`tools/README.md`
配套；变更本文件需评审并同步三处引用。

## 1. 职责划分

| 区域 | 职责 | 明确不做 |
|---|---|---|
| `out/` | 构建产物与验收证据的保存、归档、冷热分层 | 不存脚本；不删文件 |
| `tools/` | **脚本与工具的唯一归属地**（含草稿与归档） | 不放构建产物与验收证据 |

## 2. `out/` 目标布局

```text
out/
  README.md                      规则
  HardwareAcceptance/ p3/ quick/ 证据（原位，允许冷热分层）
  build/dpll60-latest/           当前迭代构建树
  build/p3-acceptance-<凭证戳>/  凭证引用的构建树（不得移动）
  archive/
    OUT_ARCHIVE_MANIFEST.md      归档清单
    cold/<域类别>/               冷数据（默认保留）
    loose-logs/                  根目录日志归集
    deletable-candidates/        可删除候选（用户删除入口）
    pending-deletion/            既有待删区
```

命名：构建树 `out/build/<topic>-<yyyymmdd>[-rN]/`；证据目录由工具生成，禁止手工改名。

## 3. `tools/` 目标布局

```text
tools/
  README.md  tool_registry.json  tool_layout.json  tool_registry.schema.json
  docs_check/  doc_regression_check.py  checks/  hardware_acceptance/  tool_governance/
  common/  domains/  workflows/  tests/  bench/  flash_map/        ← 禁止移动集
  scratch/                                                        ← 草稿（gitignore，保留 README/.gitkeep）
  archive/                                                        ← 停维与归档（含 deletable-candidates/）
  <域>/                                                           ← 域目录（见 §4）
```

**域目录（领域优先，与 `docs/<域>` 对齐）**：`calibration/ flash/ dpll/ vdc/ refmem/ tdma/ trigger/
sync_io/ storage/ scpi/ ota/ analyzers/ monitors/ project/`。

工具目录内部结构：`tools/<域>/<tool_name>/`，至少含 `README.md`（用途/入口/依赖/示例）与入口脚本；
工具名即目录名（`snake_case`）。

## 4. 命名与落位规则

| 对象 | 规则 |
|---|---|
| 工具目录 | `snake_case`，语义＝功能（`refmem_spi_hil_validate`）；域由父目录表达 |
| 脚本 | `snake_case.py`；PS 脚本 `.ps1`；入口名与目录同名或 `main.py` |
| 草稿脚本 | `tools/scratch/<主题>_<yyyymmdd>.py` |
| 构建树 | `out/build/<topic>-<yyyymmdd>[-rN]/` |
| 实验产物目录 | `<topic>-<yyyymmdd>[-rN]/`（归档时进 `out/archive/cold/<类别>/`） |
| 归档类别 | 与域同名优先（`t2-experiments`/`dpll-vdc-experiments` 等历史类别可保留） |

**落位决策**：① 是构建产物或验收证据 ⇒ `out/`；② 是脚本/工具 ⇒ `tools/`；
③ 被门禁/凭证引用 ⇒ **原位不动**；④ 停维 ⇒ 对应 `archive/`。

## 5. 禁止移动集（任何批次都不得移动）

`docs_check/`、`doc_regression_check.py`、`checks/`、`hardware_acceptance/`、`tool_governance/`、
`common/`、`domains/`、`workflows/`、`tests/`、`bench/`、`flash_map/`、`tool_registry*.json`、
`tool_layout.json`、`archive/`、`scratch/`、`README.md`；以及 `out/` 的证据目录与凭证引用的构建树。

## 6. 合规检查（人工，暂不接门禁）

- `out/` 顶层 = 6 项（§2）；`tools/` 顶层 = 禁止移动集 + 域目录（§3）。
- 新增工具已登记于 `tool_registry.json`（含 `category`、`kind`）。
- 命名与落位符合 §4；草稿只在 `scratch/`。
