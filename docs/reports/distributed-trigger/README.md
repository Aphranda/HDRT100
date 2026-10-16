# 分布式触发报告输出

Status: Active
Domain: RELEASE
Canonical: `docs/reports/distributed-trigger/README.md`
Related: `docs/reports/README.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
Last updated: 2026-10-16

本目录保存分布式触发技术报告、历史完整报告和仓库摘要。

## 统一报告接口

阶段报告（`DHRT100_<TOPIC>_STATUS_REPORT_<YYYYMMDD>.html`）共用同一套页面接口，内容按功能口径组织，不散装：

**功能状态优先原则（2026-10-15 确立）**：阶段报告的主线是「功能能不能成立、各自完成到哪一步」，代码符号、文件与行号只作为出处坐标，不作为报告主体，也不逐行抄录实现细节。每页先给该功能的能做什么与判据，再给证据，最后给未闭合项。

| 件 | 作用 |
|---|---|
| `DHRT100_REPORT_TEMPLATE.css` | **版式唯一事实源**：A4 竖版页面骨架、页头三栏、功能块四段式（定位 → 事实 → 边界 → 结论）、表格与状态标签。报告只写内容，不内嵌 `<style>`；改版式只改这一处。 |
| `DHRT100-brand.svg` | 统一品牌标志，页头 `div.brand` 内以 `<img class="brand-logo">` 引用，不再逐页内嵌 SVG。 |

每页骨架固定为 `div.page > page-header（h1 + subtitle ｜ brand ｜ doc-meta）> main > bar（本页结论）> page-footer（依据 + N/M）`；每个功能块独占一页，页内先给「本页定位」，再给事实/契约表，再给边界与未闭合项，最后给结论条。新报告一律照此接口写。

## 当前报告

| 路径 | 定位 |
|---|---|
| `DHRT100_T2_STATUS_REPORT_20261016.html` / `DHRT100_T2_STATUS_REPORT_20261016.pdf` | T2 预约触发**功能状态报告**：A4 竖版八页。主结果：**361 全程达成**——连续三轮，源端全程无损，三从板各自进窗·宣告·冻结 361/361/361、执行 354–360/361（98–99.7%），残差全部有据且负载均衡。功能面按 F1 配置与静态部署／F2 时间片与预测／F3 位置承载／F4 全程执行／F5 每位置多组序列／F6 就绪与物理证据陈述，另附批次收口（代码待 P3 凭证）。**已撤回**上一版「按板分发公平性」的判断：车道是向四板广播同一批时间片，板侧按静态延时执行，不存在按板分发目标。取代 `DHRT100_T2_STATUS_REPORT_20261015.html`（已删除，内容并入本版）。 |
| `DHRT100_PROJECT_OVERVIEW_REPORT_20260919.html` / `DHRT100_PROJECT_OVERVIEW_REPORT_20260919.pdf` | 项目综述更新版：沿用 A4 竖版模板，面向总体架构评审，覆盖当前 HAOFV、四板 DPLL、无外参十分钟、外参相干性及调参实测，保留明确进度口径。 |
| `DHRT100_PROJECT_OVERVIEW_REPORT_20260827.html` / `DHRT100_PROJECT_OVERVIEW_REPORT_20260827.pdf` | 2026-08-27 项目综述历史快照，保留原版。 |
| `DHRT100_DPLL_STATUS_REPORT_20260918.html` / `DHRT100_DPLL_STATUS_REPORT_20260918.pdf` | 四板 DPLL 阶段报告：A4 竖版七页，按统一接口组织为 功能块（一–四）／平台底座（五–八）／机制分解（九–十一）／实测（十二–十七）／改进与验证（十八–二十一）／门禁（二十二–二十四）；实验快照。 |
| `DHRT100_DPLL_STATUS_REPORT_20260919.html` / `DHRT100_DPLL_STATUS_REPORT_20260919.pdf` | DPLL 阶段更新：A4 竖版八页，保留历史收敛与方波，新增长窗、外参 100/50 ppb/s 对比、SCPI/Flash、相干性与输入电气待验证项；实验快照。 |
| `DISTRIBUTED_HARD_REALTIME_TRIGGER_OVERVIEW_REPORT.html` | Distributed Hard Real-Time Trigger System 3-4 页概述报告 |
| `RTOS_DISTRIBUTED_TRIGGER_0614_REPORT.html` | 0614 分布式触发完整报告 |
| `相控阵测试系统RP分布式触发方案技术报告0804.html` | 0804 分布式触发完整技术报告 |
| `相控阵测试系统RP分布式触发方案技术报告0804.md` | 0804 分布式触发仓库摘要 |
| `REFMEM_TDMA_ONE_TICK_BUDGET_REPORT.html` | REFMEM/TDMA 一拍负载预算快照（3 页，含单 Node recovery 规划） |
