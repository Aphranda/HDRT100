# 标准：tools/ 归档与迁移流程

Last updated: 2026-10-10

适用：**仅 `tools/`**。细则见 `TOOLS_MIGRATION_PLAN.md`；审计见 `STANDARD_REFERENCE_AUDIT.md`（TOOLS）。

## 1. 三分类

| 类别 | 判据（限 tools/） | 处置 |
|---|---|---|
| 保留 | 被 `docs/**`、`config/**`、门禁、registry 引用；禁止移动集 | 原位 |
| 冷数据 | 停维但仍需追溯的工具 | `tools/archive/` |
| 可删除候选 | 未被引用且可重建（缓存、未引用一次性目录） | `tools/archive/deletable-candidates/` |

## 2. 迁移（域目录化）

1. **B0 已完成**：生成 `tools/INDEX.md`；域口径对齐 registry `domain`。
2. B1+：逐域 `git mv` 到 `tools/<域>/<tool_name>/`，旧路径留 **3–5 行 `runpy` 转发 wrapper**。
3. 同批更新全部引用（`docs/**`、`config/**`、`tool_registry.json`、`tool_layout.json`、
   `.githooks/**`、`CMakeLists.txt`），并跑文档门禁；涉及固件/OTA 的批次**必须附 P3 凭证**。
4. 每批更新 `REFERENCE_AUDIT.md`（含"旧 → 新"映射与旧路径引用计数）。

## 3. 操作与留痕

只移动不删除；移动前校验绝对路径与对象清单；失败项逐条记录；批次独立提交、可 `git mv` 回滚。

## 4. 删除

用户确认后自行删除 `tools/archive/deletable-candidates/`；agent 不代为删除，
亦不得使用"批量匹配 + 删除"脚本（事故模式）。
