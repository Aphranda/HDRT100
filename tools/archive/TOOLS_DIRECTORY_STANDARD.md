# 标准：tools/ 目录布局与命名

Last updated: 2026-10-10

适用：**仅 `tools/`**。OUT 侧见 `out/standards/OUT_DIRECTORY_STANDARD.md`（两域不混写）。

## 1. 职责

`tools/` 是**脚本与工具的唯一归属地**（含草稿与归档）；**不放构建产物与验收证据**（归 `out/`）。

## 2. 目标布局

```text
tools/
  README.md  INDEX.md  tool_registry.json  tool_registry.schema.json  tool_layout.json
  docs_check/  doc_regression_check.py  checks/  hardware_acceptance/  tool_governance/
  common/  domains/  workflows/  tests/  bench/  flash_map/      ← 禁止移动集
  scratch/                                                      ← 草稿（gitignore；保留 README/.gitkeep）
  archive/                                                      ← 停维与归档（含 deletable-candidates/）
  <域>/                                                         ← 见 §3
```

## 3. 域目录（**对齐 `tool_registry.json` 的 `domain`**，小写化）

`vdc/ ota/ refmem/ sync/ storage/ communication/ calibration/ flash/ interface/ trigger/ docs/ rtos/
tdma/ validation/ state_machine/ watchdog/ project/`（TOOLS 域中非子系统工具归 `project/`）。
工具目录结构：`tools/<域>/<tool_name>/`，含 `README.md`（用途/入口/依赖/示例）与入口脚本。

## 4. 命名

| 对象 | 规则 |
|---|---|
| 工具目录 | `snake_case`，语义＝功能（`refmem_spi_hil_validate`）；域由父目录表达 |
| 入口脚本 | `snake_case.py` / `.ps1`；与目录同名或 `main.py` |
| 草稿 | `tools/scratch/<主题>_<yyyymmdd>.py` |
| 新增工具 | 必须登记于 `tool_registry.json`（`domain`/`lifecycle`/`operation`/`entrypoints`/`validation`） |

## 5. 禁止移动集

`docs_check/`、`doc_regression_check.py`、`checks/`、`hardware_acceptance/`、`tool_governance/`、
`common/`、`domains/`、`workflows/`、`tests/`、`bench/`、`flash_map/`、`tool_registry*.json`、
`tool_layout.json`、`archive/`、`scratch/`、`README.md`、`INDEX.md`。
