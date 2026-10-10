# tools/ 迁移计划（v1，评审稿）

Last updated: 2026-10-10

## 0. 目标与约束

**目标**：把 `tools/` 从"113 项扁平"整理为**按域分类**的结构，使新增工具可发现、可登记、可审计；同时不打断任何文档引用与门禁。

**硬约束**

1. **不删除**：任何动作只允许 移动 / 归档；删除由用户确认后执行。
2. **引用安全优先**：`tools/` 顶层 113 项中 **111 项被引用**（见 `tools/archive/REFERENCE_AUDIT.md`、
   `tools/_ref_audit.json`）。任何移动必须**同步更新全部引用**并通过门禁，否则不得合入。
3. **禁止移动集**（门禁/基建核心，任何批次都不得移动）：
   `docs_check/`、`doc_regression_check.py`、`checks/`、`hardware_acceptance/`、
   `tool_registry.json`、`tool_registry.schema.json`、`tool_layout.json`、`tool_governance/`、
   `common/`、`domains/`、`workflows/`、`tests/`、`bench/`、`archive/`、`scratch/`、`README.md`。
4. 每批次独立提交；文档改动与工具改动按仓库约定**分提交**，并跑文档门禁。

## 1. 目标分类法（顶层新增 5 个域目录）

| 目标目录 | 收纳内容 | 预计项数 |
|---|---|---|
| `tools/validators/` | 各域 HIL 校验器（`*_validate`、`*_board_validate`、`*_check`） | ~70 |
| `tools/analyzers/` | 波形/轨迹/残差分析（`analyzer_*`、`dpll_*_analyze`、`dpll_*_decode`、`sd_trace_decode`、`rtos_watermark_capture`） | ~12 |
| `tools/monitors/` | 长时间监视与探针（`*_monitor`、`scope_*`、`vdc_long_monitor`、`sma_cable_delay_validate` 类现场测量） | ~10 |
| `tools/ota/` | OTA 与打包（`ota_*`、`visa_ota_send`、`factory_*`、`uf2_join`、`picotool_*`、`artifact_checksum`、`release_check`） | ~18 |
| `tools/project/` | 工程与仓库工具（`cmake_build_auto`、`build_info`、`ram_budget_check`、`scpi_query`、`scpi_common`、`pdf_text_extract`、`run_*.ps1`、`rp2350_tk_toolbox.py`、`check_scpi_usb_namespace.py`） | ~10 |

> 说明：原 `tools/` 下已有 `common/`、`domains/`、`workflows/` 三个"架构性"目录（属禁止移动集），
> 新分类**不与其重叠**：`validators|analyzers|monitors|ota|project` 只收纳"可独立调用的工具目录"。

## 2. 兼容层策略（保证旧路径仍可用）

对每个被移动的工具目录，在**旧路径**留一个**转发入口**（薄 wrapper），内容形如：

```python
# tools/<old_name>/<entry>.py   ← 兼容转发（迁移期保留）
import runpy, pathlib
runpy.run_path(str(pathlib.Path(__file__).resolve().parents[1] / "<category>/<old_name>/<entry>.py"),
               run_name="__main__")
```

- 作用：文档、脚本、门禁里以 `tools/<old_name>/...` 形式出现的引用**继续可用**。
- 代价：旧路径仍在（但只含 1 个几行的转发文件），可发现性由 `tool_registry.json` 的 `category` 字段提供。
- 退出条件：当引用审计显示旧路径在 `docs/**`、`config/**`、门禁中**不再出现**时，wrapper 移入
  `tools/archive/`（仍不删除）。

## 3. 分批计划（每批：范围 → 操作 → 引用更新 → 验证 → 回滚）

| 批次 | 范围 | 关键动作 | 验证 | 回滚 |
|---|---|---|---|---|
| **B0 准备** | 无移动 | ① `tool_registry.json` 增 `category` 字段并补登全部工具；② 生成 `tools/INDEX.md`（按域分组索引）；③ 记录本计划为评审稿 | 文档门禁 + 注册表 schema 校验 | 删除索引与字段（commit 回退） |
| **B1 校验器** | ~70 个 `*_validate` | 建 `tools/validators/<域>/`（vdc/refmem/storage/sync/ota/board/trigger/flash）；逐个 `git mv` + 留 wrapper | ① 全量引用更新后跑 `docs_check`+`doc_regression_check`；② 抽样 3 个校验器按文档命令实跑（含 1 个 HIL） | 逐目录 `git mv` 回原位（wrapper 一并撤） |
| **B2 分析器** | ~12 | 建 `tools/analyzers/` | 抽样跑 1 个离线分析（如 `dpll_observation_decode`） | 同上 |
| **B3 监视器** | ~10 | 建 `tools/monitors/` | 抽样跑 1 个（如 `vdc_long_monitor --dry-run`） | 同上 |
| **B4 OTA/打包** | ~18 | 建 `tools/ota/` | **必须**：`p3_hardware_acceptance.py run` 一次 + OTA 路径冒烟（`ota_bin_info`/`uf2_join`） | 同上 |
| **B5 工程工具** | ~10 | 建 `tools/project/` | 文档门禁 + `cmake_build_auto` 冒烟 | 同上 |
| **B6 收尾** | wrapper 清理 | 对"引用已消失"的旧路径 wrapper 移入 `tools/archive/`；更新 `REFERENCE_AUDIT.md` | 重新审计：旧路径引用数 = 0 | 移回 wrapper |

**批次顺序理由**：先做**零移动**的 B0（登记与索引，立即改善可发现性且零风险），再从小到大迁移；
把**涉及固件/OTA 凭证**的 B4 放在中间（需 P3），把纯工程工具放最后。

## 4. 引用更新清单（每批次都要扫）

| 位置 | 说明 |
|---|---|
| `docs/**` | 文档中的 `tools/<name>/...` 路径（含进展日志归档与 legacy） |
| `tools/tool_registry.json`、`tool_layout.json` | 注册表与布局定义（B0 起即改为按 `category` 组织） |
| `config/**` | workflow inputs、acceptance 配置中引用的工具路径 |
| `.githooks/**`、`CMakeLists.txt` | 门禁与构建引用 |
| `components/**`、`application/**`、`middleware/**` | 源码注释/脚本中的工具引用 |
| `tools/archive/REFERENCE_AUDIT.md` | 审计记录（每批次更新） |

## 5. 验收标准

**单批验收**：① 该批目录已归位且 wrapper 生效；② 引用审计中旧路径引用已更新为 0（wrapper 除外）；
③ 文档门禁通过；④ 抽样命令按文档可实跑；⑤ B4 附 P3 凭证；⑥ 审计记录已更新。

**整体完成定义**：① 5 个域目录建成且 `tools/` 顶层只剩禁止移动集 + 5 个域目录；
② `tool_registry.json` 全量登记 `category`；③ `REFERENCE_AUDIT.md` 显示旧路径引用为 0；
④ 所有 wrapper 已移入 `tools/archive/`；⑤ 用户确认可删除 `tools/archive/deletable-candidates/`。

## 6. 风险与未决

| 风险 | 缓解 |
|---|---|
| 文档门禁对路径真实性的校验 | 每批次先跑 `docs_check --strict-names` + `doc_regression_check`，再合入 |
| 隐藏引用（字符串拼接、动态 import） | 引用审计按"目录名子串"匹配，覆盖拼接场景；B4 追加一次全量 P3 |
| `git mv` 在 Windows 上的大小写/锁定问题 | 每批次前确认无进程占用；失败则跳过该目录并记入审计 |
| 重新组织会稀释历史可追溯性 | 保留 wrapper + 审计表记录"旧路径 → 新路径"映射，写进 `REFERENCE_AUDIT.md` |

## 7. 立即可做的第一步（零移动）

**B0**：① 给 `tool_registry.json` 增加 `category` 字段并登记现有全部工具；② 生成 `tools/INDEX.md`；
③ 把本计划（评审稿）随提交入库。B0 不移动任何目录、零风险，但立刻让 `tools/` 可发现、可审计，
并为后续 5 个批次提供分类依据。
