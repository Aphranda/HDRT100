# 分布式触发报告输出

Status: Active
Domain: RELEASE
Canonical: `docs/reports/distributed-trigger/README.md`
Related: `docs/reports/README.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
Last updated: 2026-10-19

本目录保存分布式触发技术报告、历史完整报告和仓库摘要。

## 统一报告接口

阶段报告共用同一套页面接口，内容按功能口径组织，不散装：

- **阶段状态报告**：`DHRT100_<TOPIC>_STATUS_REPORT_<YYYYMMDD>.html`，用于阶段快照，文档内标注状态与日期。
- **交付完成报告**：`DHRT100_<TOPIC>_COMPLETION_REPORT.html`，用于功能完成交付，**文档内不写日期**（避免过期感，完成结论以功能与证据表述）。

**功能状态优先原则**：阶段报告的主线是「功能能不能成立、各自完成到哪一步」，代码符号、文件与行号只作为出处坐标，不作为报告主体，也不逐行抄录实现细节。每页先给该功能的能做什么与判据，再给证据，最后给未闭合项或边界。

| 件 | 作用 |
|---|---|
| `DHRT100_REPORT_TEMPLATE.css` | **版式唯一事实源**：A4 竖版页面骨架、页头三栏、功能块四段式（定位 → 事实 → 边界 → 结论）、表格与状态标签。报告只写内容，不内嵌 `<style>`；改版式只改这一处。 |
| `DHRT100-brand.svg` | 统一品牌标志，页头 `div.brand` 内以 `<img class="brand-logo">` 引用，不再逐页内嵌 SVG。 |

每页骨架固定为 `div.page > page-header（h1 + subtitle ｜ brand ｜ doc-meta）> main > bar（本页结论）> page-footer（依据 + N/M）`；每个功能块独占一页，页内先给「本页定位」，再给事实/契约表，再给边界与未闭合项，最后给结论条。新报告一律照此接口写。

## 当前报告

| 路径 | 定位 |
|---|---|
| `DHRT100_SEQUENCE_PROGRESS_REPORT.html` / `DHRT100_SEQUENCE_PROGRESS_REPORT.pdf` | **SYNC_SEQUENCE 位置内序列进度报告**（A4 竖版七页，陈述式、**文档内不写日期**）：输入与解算（时间片 + 组序列表 → 逐组绝对 tick）、执行模型（整表交入、按组回执、完成推进）、容量与上限（18 组/位置，四处上限同改）、台架证据（N=2/16/18；16 组整圈两开关各 5776 = 16×361、零失败；18 组整圈满足 executed = 组数 × 位置数）、诊断与判读、剩余项与下一步。属 `docs/trigger/sequence/` 能力域。 |
| `DHRT100_T2_COMPLETION_REPORT.html` / `DHRT100_T2_COMPLETION_REPORT.pdf` | T2 **能力达成情况报告**（A4 竖版七页，陈述式、**文档内不写日期**）：能力总述与清单、判定链路与实现方式（八项实现机制 + 逐段判据）、能力清单（配置面/运行面/观测面/角色面）、验收证据一（361 全程三轮：源端无损、三从板各进窗·宣告·冻结 361/361/361）、验收证据二（T2 侧交接面：双组 722 次、容量 48 行）、达成路径与读数纪律、能力范围与边界（覆盖/不覆盖与归属；组序列解算与逐组证据归 `docs/trigger/sequence/` 能力域）。取代 `DHRT100_T2_STATUS_REPORT_20261016.html`（已删除，内容并入本版）。 |
| `DHRT100_T2_TIMELINE_REPORT_20261019.html` / `DHRT100_T2_TIMELINE_REPORT_20261019.pdf` | **项目提交链时间轴 · 全子域攻坚（五页）**：2026-06-21 → 2026-10-19，2 261 个提交、五个阶段；页一为总体节奏与跨域里程碑，页二为十二个子域攻坚总表（提交数/时间窗/成果）与四个决定性攻坚，页三为 VDC/DPLL、TDMA、REFMEM 三域深挖（每行对应一条登记契约或提交号），页四为达成轨迹（1 824 → 5 776 → 6 480 → 5 776/5 776/5 760）、链的形状与成本分布，页五为最后一役 78 提交分段、达成判据（`executed = 组数 × finish`，三后端 0/0/0）与剩余项。 |
| `DHRT100_PROJECT_OVERVIEW_REPORT_20260919.html` / `DHRT100_PROJECT_OVERVIEW_REPORT_20260919.pdf` | 项目综述更新版：沿用 A4 竖版模板，面向总体架构评审，覆盖当前 HAOFV、四板 DPLL、无外参十分钟、外参相干性及调参实测，保留明确进度口径。 |
| `DHRT100_PROJECT_OVERVIEW_REPORT_20260827.html` / `DHRT100_PROJECT_OVERVIEW_REPORT_20260827.pdf` | 2026-08-27 项目综述历史快照，保留原版。 |
| `DHRT100_DPLL_STATUS_REPORT_20260918.html` / `DHRT100_DPLL_STATUS_REPORT_20260918.pdf` | 四板 DPLL 阶段报告：A4 竖版七页，按统一接口组织为 功能块（一–四）／平台底座（五–八）／机制分解（九–十一）／实测（十二–十七）／改进与验证（十八–二十一）／门禁（二十二–二十四）；实验快照。 |
| `DHRT100_DPLL_STATUS_REPORT_20260919.html` / `DHRT100_DPLL_STATUS_REPORT_20260919.pdf` | DPLL 阶段更新：A4 竖版八页，保留历史收敛与方波，新增长窗、外参 100/50 ppb/s 对比、SCPI/Flash、相干性与输入电气待验证项；实验快照。 |
| `DISTRIBUTED_HARD_REALTIME_TRIGGER_OVERVIEW_REPORT.html` | Distributed Hard Real-Time Trigger System 3-4 页概述报告 |
| `RTOS_DISTRIBUTED_TRIGGER_0614_REPORT.html` | 0614 分布式触发完整报告 |
| `相控阵测试系统RP分布式触发方案技术报告0804.html` | 0804 分布式触发完整技术报告 |
| `相控阵测试系统RP分布式触发方案技术报告0804.md` | 0804 分布式触发仓库摘要 |
| `REFMEM_TDMA_ONE_TICK_BUDGET_REPORT.html` | REFMEM/TDMA 一拍负载预算快照（3 页，含单 Node recovery 规划） |
