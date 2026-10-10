# out/ 目录管理规则（README 规则）

Last updated: 2026-10-10

本文件规定 `out/`（构建产物与验收证据的工作区）的整理规则。**范围仅限 `out/` 及其归档区**，
不改变 `docs/**` 的文档语料规则；本规则**暂不接入门禁检查**（先作为约定生效，稳定后再评估是否加检查器）。

## 0. 最高优先级：不删除

- **任何人不得删除 `out/` 下的内容**；删除只由用户本人在确认后自行执行。
- 允许的动作只有两类：**移动**（同卷 rename）与**归档**（移入 `out/archive/**`）。
- 例外：无。（构建缓存也不例外——它属于"可删除候选"，仍由用户删除。）

## 1. 顶层保留项（不得移动）

| 路径 | 说明 |
|---|---|
| `out/HardwareAcceptance/` | P3/DPLL 等硬件验收证据（含 `diagnostic.json`、`alarms.json`、采集原件） |
| `out/p3/`、`out/quick/` | P3 与快速回归证据 |
| `out/build/dpll60-latest/` | 当前快速迭代构建树 |
| `out/build/p3-acceptance-<凭证时间戳>/` | **当前 P3 凭证引用**的构建树（见 `config/hardware_acceptance/p3_acceptance_receipt.json`） |
| `out/archive/` | 归档区（见第 3 节） |
| `out/.gitkeep` | 占位（保持目录入库存续） |

证据与凭证引用**一律原位保留**；需要压缩体积时只做冷/热分层（移入 `out/archive/cold/`），不删除。

## 2. 新增产物的落位约定

| 产物类型 | 落位 | 命名 |
|---|---|---|
| 一次性构建树（实验/回归） | `out/build/<topic>-<yyyymmdd>[-rN]/` | 取自变更主题，便于按名追溯 |
| 硬件验收运行 | `out/HardwareAcceptance/<yyyymmdd>/<tool>-<hhmmss>/`（由工具自动生成） | 不要手工改名 |
| 临时日志 | 放入 `out/archive/loose-logs/` | `*.log` |
| 主题实验产物（多文件） | 结束后移入 `out/archive/cold/<类别>/` | 沿用第 3 节类别 |

原则：`out/` 的**顶层只保留第 1 节的保留项**；其余一律归档，避免再次“肆意生长”。

**脚本不属于 `out/`**：`out/` 只负责构建产物与验收证据的保存与归档；
任何临时/探测/分析脚本一律放 `tools/`（草稿区 `tools/scratch/`，见其 README），本目录不设脚本归集区。

## 3. 归档区结构与分类

| 路径 | 用途 |
|---|---|
| `out/archive/cold/` | **冷数据**：按类别保存，建议长期保留 |
| `out/archive/cold/t2-experiments/` | T2 各阶段实验 |
| `out/archive/cold/dpll-vdc-experiments/` | DPLL/VDC 实验 |
| `out/archive/cold/tdma-experiments/` | TDMA 实验 |
| `out/archive/cold/action-experiments/` | 动作表/执行实验 |
| `out/archive/cold/io-window-experiments/` | 槽位/计窗/IO 实验 |
| `out/archive/cold/probes/` | 串口与只读探测快照 |
| `out/archive/cold/notes/` | 笔记、清单、备份 |
| `out/archive/cold/docs-audit/` | 文档审计产物 |
| `out/archive/cold/reviews/` | 评审材料 |
| `out/archive/cold/misc/` | 工具、OTA、其它杂项 |
| `out/archive/loose-logs/` | 根目录散落日志的归集地（`out/` 不归集脚本，脚本见 `tools/`） |
| `out/archive/deletable-candidates/` | **可删除候选**（见第 4 节） |
| `out/archive/pending-deletion/` | 仓库既有待删区（沿用旧约定） |

新增类别时，请在本节补一行，并在 `OUT_ARCHIVE_MANIFEST.md` 记录分类结果。

## 4. 可删除候选（deletable-candidates）

- 判定标准：**可由源码/工具重建**（编译缓存、测试缓存、worktree 副本、历史实验产物），
  且**不被任何凭证或文档引用**。
- 处置：移动进 `out/archive/deletable-candidates/<子类>/`，并在
  `OUT_ARCHIVE_MANIFEST.md` 登记规模与理由。
- **删除动作由用户执行**：确认后直接删除 `out/archive/deletable-candidates/` 整个目录即可。

## 5. 归档清单（必须维护）

- 每次归档后更新 `out/archive/OUT_ARCHIVE_MANIFEST.md`：
  记录**动作**（移动了什么、移动到哪）、**规模**、**事故或例外**（如移动失败）、**未决事项**。
- 清单本身不得删除；若需变更结构，先改清单再动文件。

## 6. 禁止事项

1. 删除 `out/` 下任何内容（只允许用户操作）。
2. 把证据（`HardwareAcceptance/`、`p3/`、`quick/`）移出 `out/` 或改名。
3. 移动当前 P3 凭证引用的构建树。
4. 用脚本批量匹配（如 `Get-ChildItem out/build -Directory | Remove-Item`）执行破坏性动作；
   任何移动前必须**打印将被移动的对象清单**并获得确认（或至少落盘记录）。
5. 让 `out/` 顶层重新堆积临时目录与散件——新产物按第 2 节直接落到归档区。
6. 在 `out/` 存放任何脚本——脚本由 `tools/` 管理（草稿区 `tools/scratch/`）。
