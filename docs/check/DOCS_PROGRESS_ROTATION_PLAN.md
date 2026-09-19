# 进展日志轮转计划（C14 / C15 / 环5）

Status: Active
Domain: Documentation Governance
Canonical: `docs/check/DOCS_PROGRESS_ROTATION_PLAN.md`
Related: `docs/check/DOCS_REGRESSION_PLAN.md`, `docs/check/DOCS_REGISTRY.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-09-19

> 本文件是 C14（轮转）与 C15（倒排）的**操作规程 + 债务基线**唯一落点。
> 约束条款本身在 `docs/check/DOCS_REGRESSION_PLAN.md` §0；机器事实源在
> `tools/doc_regression_check.py` 的环形检查与 `PROGRESS_*` 常量。
> 本文件**不复制**条目明细以外的数字，避免双源漂移。

## 1. 适用范围与条目判据

适用于 `docs/<domain>/<DOMAIN>_TASK_PROGRESS.md`。`legacy/`、`archive/`、`check/`、`temp/` 下的同名文件
视为归档区，**不参与**环5 检查（归档区自然豁免，且已排除在环1 新鲜度扫描外）。

条目只按 ID 识别，正文里的 8 位数字**不得**当日期：

```
^#{2,3}\s+<PREFIX>-YYYYMMDD-NNN
```

- `YYYYMMDD` 必须是合法日历日期（非法日期与 `YYYYMMDD-NNN` 字面模板都不计为条目）。
- 曾踩坑：按"任意 8 位数字"取日期会造出 `2078-49-87`、`2000-89-70` 这类假日期。

## 2. 规则（机检见 `--progress`）

| # | 规则 | 判定 |
|---|---|---|
| R1 | 规范进展日志单文件 ≤ 200KB（204800 字节，**按字节不按字符**） | 超限 FAIL；在登记债务内者 WARN 至截止日 |
| R2 | 归档必须**最旧优先连续截断**：归档条目最大日期 ≤ 规范文件保留条目最小日期 | 违反 FAIL |
| R3 | 归档位置 `docs/legacy/<domain>/LEGACY_<DOMAIN>_TASK_PROGRESS_<NN>.md` | 位置/命名不符 FAIL |
| R4 | `## 归档索引` 闭包：列出文件存在、声明条目数 == 实际、规范 ∪ 归档 ID 无重复、归档每个 ID 都落在某条索引区间内 | 违反 FAIL |
| R5 | 新鲜度：保留 `## 当前 checkpoint`；`Last updated` 不得早于最新条目日期 | 违反 FAIL |
| R6 | 倒排：日期序列非递增（**最新鲜的在最上面**）；归档文件同样倒排 | 违反 FAIL；登记债务内者 WARN 至截止日 |

R1 曾踩坑：用 `len(str)` 判 200KB 会放过 3 倍超限的文件（中文 1 字符 = 3 字节）。

## 3. 排序约定（倒排）

条目按新鲜度**倒排**——**最新鲜的在最上面**。新条目写入文件顶部（`## 当前 checkpoint` 之后），
因此轮转一律从**尾部**截取最旧的一段；归档文件与 `## 归档索引` 的 ID 区间同样倒排
（`<最新 ID>..<最旧 ID>`），检查器对区间方向**双向容忍**。

R2/R6 以**日期**为边界判定，**同日内部顺序不限**。实测依据：14 份规范日志中 12 份在同一天内
使用升序块（如 `FLASH-TASK-20260823-077 → -078`），只有 VDC 为严格倒排；按序列单调判定会误伤
存量文档。轮转时必须**保留每份日志自身的既有日内风格**。

保留窗口 90 天为**指导值**（R1 允许时尽量保留），**R1 的字节上限优先**。实测依据：
`VDC_TASK_PROGRESS.md` 176 条全部落在 12 天内已达 668KB，`HAOFV_FLASH_TASK_PROGRESS.md`
4 天即 209KB——时间窗口不构成约束，字节上限才是。

## 4. 轮转操作规程

1. 解析条目 ID，按日期分组，统计各日字节数。
2. 选保留边界：从最新日期向下累加，取**满足 R1 的最大保留集**（即最小可接受边界）。
3. 迁出条目**逐字**写入归档文件，按倒排排列；**不得编辑条目正文**。
4. 归档文件补 5 字段元数据，`Status: Frozen`。
5. 规范文件文末补 `## 归档索引`；保留 `## 当前 checkpoint`；更新 `Last updated`。
6. 归档文件在 `docs/README.md` 补索引行（`docs_check` 强制要求）。
7. 字节守恒自检：`canonical + archive − 新增头部 − 索引 − 指针 == 原文件`；
   并断言 `规范 ID ∪ 归档 ID == 原 ID 集合`、无重复。**这两条断言是轮转的正确性证明**。
8. 跑 `python tools/doc_regression_check.py --progress`，必须 exit 0。

## 5. 债务基线

语义（沿用体系的债务模式）：**只可删除条目，不可新增**；新增豁免需交叉审核（C11）。
条目超截止日仍违规则 **FAIL**。机器事实源为检查器常量 `PROGRESS_ROTATION_DEBT` /
`PROGRESS_ORDER_DEBT`（C5 三同步），本表为其文档落点。

### 5.1 体量债务（R1 豁免）

| 文件 | 登记时体量 | 状态 / 截止日 |
|---|---|---|
| `docs/vdc/VDC_TASK_PROGRESS.md` | 668KB / 176 条 | **已轮转**（2026-09-17，→ 归档段 `_01`） |
| `docs/tdma/TDMA_TASK_PROGRESS.md` | 570KB / 132 条 | **已轮转**（2026-09-19，归档 `_01`–`_03`；证据见该域归档索引及 `out/doc-audit/tdma-refresh-20260919/rotation-audit.json`） |
| `docs/refmem/REFMEM_TASK_PROGRESS.md` | 297KB / 108 条 | 待轮转，截止 2026-10-17 |
| `docs/arch/HAOFV_FLASH_TASK_PROGRESS.md` | 209KB / 123 条 | 待轮转，截止 2026-10-17 |

### 5.2 倒序债务（R6 豁免）

存量偏离均为**尾部补记或顶部回填**造成的单点倒置：

| 文件 | 日期级倒置处数 | 截止日 |
|---|---|---|
| `docs/sync/SYNC_IO_TASK_PROGRESS.md` | 2 | 2026-10-17 |
| `docs/arch/HAOFV_FLASH_TASK_PROGRESS.md` | 1 | 2026-10-17 |
| `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md` | 1 | 2026-10-17 |
| `docs/communication/COMMUNICATION_RS485_TASK_PROGRESS.md` | 1 | 2026-10-17 |
| `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md` | 1 | 2026-10-17 |

TDMA 的尾部补记已在 2026-09-19 轮转中按日期稳定归位，旧正文保留；TDMA 尺寸和倒序债务已完成。检查器中的既有截止日兼容项本轮不改，检查当前文件已不触发对应告警。

### 5.3 已完成的样板轮转

| 规范文件 | 轮转前 | 轮转后 | 归档段 |
|---|---|---|---|
| `docs/vdc/VDC_TASK_PROGRESS.md` | 684,444 B / 176 条 | 158,873 B / 30 条 | `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_01.md`（526,615 B / 146 条） |

## 6. 验收路径与留证

C14/C15 的机检随**文档门禁**运行，验收定义见 `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` §6
（`docs_check --strict-names` + `doc_regression_check.py` + 对应 pytest + pre-commit），
留证 `out/doc-audit/<run-id>/`。

**不走** EXE-CHANGE-01 第 4 步的四板 P3 路径——后者适用对象是固件/PIO/构建/工具/测试的
**功能实现**改动，文档门禁工具由 §6 单独定义。

## 7. 与其它环的关系

- **环1 新鲜度**：`legacy/` 已在 `FRESHNESS_EXCLUDE_DIRS` 内，归档**不改变**顶层 7 天刷新判定。
- **环2 登记**：C14/C15 已登记 `DOCS-PROGRESSROTATE-01`、`DOCS-PROGRESSORDER-01`。
- **环5 实现**：`tools/doc_regression_check.py::check_progress_rotation()`，CLI `--progress`，
  已接入默认运行（pre-commit 生效）。C6 预算实测 1.39s ≤ 5s。
- **待改进**：VDC 单条均 ≈5KB，200KB 上限只给出约一天的规范窗口；根治要靠**压缩条目篇幅**
  （证据索引而非叙事），而非继续加轮转频率。
