# 标准：out/ 目录布局与命名

Last updated: 2026-10-10

适用：**仅 `out/`**。TOOLS 侧见 `tools/archive/TOOLS_DIRECTORY_STANDARD.md`（两域不混写）。

## 1. 职责

`out/` 只负责**构建产物**与**验收证据**的保存、归档、冷热分层。**不存脚本**（脚本归 `tools/`），**不删文件**。

## 2. 目标布局

```text
out/
  README.md                      入口规则
  standards/                     本域标准（仅 OUT）
  HardwareAcceptance/ p3/ quick/ 验收证据（原位；允许冷热分层）
  build/dpll60-latest/           当前迭代构建树
  build/p3-acceptance-<凭证戳>/  凭证引用构建树（不得移动）
  archive/
    OUT_ARCHIVE_MANIFEST.md      归档清单
    cold/<类别>/                 冷数据
    loose-logs/                  根目录日志归集
    deletable-candidates/        可删除候选（用户删除入口）
    pending-deletion/            待删区
```

## 3. 命名

| 对象 | 规则 |
|---|---|
| 构建树 | `out/build/<topic>-<yyyymmdd>[-rN]/` |
| 证据目录 | 由工具生成（如 `HardwareAcceptance/<yyyymmdd>/<tool>-<hhmmss>/`），**禁止手工改名** |
| 冷数据类别 | 与来源域同名优先（`t2-experiments`/`dpll-vdc-experiments` 等历史类别可保留） |
| 日志归集 | `out/archive/loose-logs/*.log` |

## 4. 顶层保留项（不得移动）

`HardwareAcceptance/`、`p3/`、`quick/`、`build/dpll60-latest/`、
`build/p3-acceptance-<当前凭证戳>/`、`archive/`、`standards/`、`README.md`、`.gitkeep`。

## 5. 落位决策

① 构建产物或验收证据 ⇒ `out/`；② 脚本/工具 ⇒ `tools/`（**不放 `out/`**）；
③ 被凭证引用 ⇒ 原位不动；④ 停维 ⇒ `out/archive/`；⑤ 可重建且无引用 ⇒ `deletable-candidates/`。
