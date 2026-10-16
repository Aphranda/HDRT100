# 归档：已退役的 out/ 依赖工具

Status: Archived
Domain: Tools
Canonical: `tools/archive/scratch/README.md`
Related: `docs/tools/DOCS_TOOLS_TASK_PROGRESS.md`（TOOLS-PROGRESS-20261016-003）
Last updated: 2026-10-16

本目录保存**已退役**的一次性脚本的归档副本。原则（用户要求）：**只归档、不删除**。
原件仍在其原位置（`tools/scratch/`，该目录被 `.gitignore` 忽略），此处副本用于让仓库保留它们。
这些脚本**不属维护面**：不得被任何工具/流程加载，也不得据此从 `out/` 加载代码。

| 归档文件 | 字节 | sha256（前 16） | 退役原因 |
|---|---|---|---|
| `condense_overview_report.py` | 8687 | `cb521a5dcb73d792` | Edits docs/reports HTML. It imported BeautifulSoup from the vendored out/report-deps directory, which no longer exists. |
| `report_logic_cleanup.py` | 1344 | `4a3f1861b4675c1a` | Edits docs/reports HTML. Depended on the removed out/report-deps vendor directory. |
| `sync-build.py` | 226 | `4e075fc7bd23775e` | Patches a build id inside out/origin-handshake.py. That target no longer exists and writing into out/ is not a maintained code path, so it is retired here. |
| `update_dpll_report_20260919.py` | 15090 | `69b5a5842734b2a3` | Edits docs/reports HTML. Depended on the removed out/report-deps vendor directory. |
| `update_overview_precision.py` | 1139 | `c93005ffe3dd4956` | Edits docs/reports HTML. Depended on the removed out/report-deps vendor directory. |

`out/` 仅存放产物与证据；可维护代码位于 `tools/`（实时验收栈见
`tools/workflows/vdc_joint_capture/`，历史迁移证据见
`tools/archive/vdc/vdc_joint_capture/lineage_v1/`，其 sha256 固定于
`config/tool_workflows/vdc_joint_capture_internal_seal.json` 的 `adapter.migrated_sources`）。
