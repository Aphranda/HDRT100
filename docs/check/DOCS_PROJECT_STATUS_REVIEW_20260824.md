# 项目状态审查快照（2026-08-24）

Status: Active
Domain: Documentation Governance / Project Review
Canonical: `docs/check/DOCS_PROJECT_STATUS_REVIEW_20260824.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`, `docs/check/DOCS_REGRESSION_TODO.md`
Last updated: 2026-08-24

> 本文是 2026-08-24 项目状态审查快照，不是新的冻结契约，也不是代码或 HIL 事实源。
> 数字、构建号、板端日志和单次训练结果必须回到 Related 文档及其域内 Task Progress 查证。

## 审查结论

项目已从架构搭建和原型验证阶段进入跨域闭环集成阶段。架构、基础组件、诊断能力和文档治理体系已形成；但核心产品链路尚未完成统一 active gate，因此当前不能判定为发布收尾阶段。

主链路仍按以下顺序收敛：

```text
硬件时间戳 -> Calibration active -> TDMA 常驻双向环
-> VDC/DPLL LOCKED -> RefMem 可靠完成语义
-> T2 预约触发 -> 长稳与发布
```

当前工程瓶颈不是缺少局部代码，而是局部能力尚未通过统一门禁串成产品级闭环。

## 域状态

以下为各域 TODO 的勾选快照，仅用于本次审查定位，不作为项目总完成率：

| 领域 | 快照 | 当前判断 |
|---|---:|---|
| OTA v1 | 约 86% | 正向、负向、A/B 和 portable OTA 迁移较成熟；不能等同于 Flash v2 完成。 |
| RefMem | 约 70% | 表模型和组件较丰富；AUTO NodeLoad 仍缺 ACK/重发/fence completion。 |
| SD/Storage | 约 63% | P0 文件、快照、trace 基础较完整；Pack/Ref、掉电和长稳待完成。 |
| TDMA | 50% | 物理层、persona、profile 基础存在；常驻同时 UP/DOWN 闭环未完成。 |
| VDC | 约 46% | DPLL、clock model、诊断 gate 已有；固件内常驻锁相尚未完成。 |
| Flash v2 | 约 45% | 仍处 M1-M4 迁移阶段，目标契约保持 pending。 |
| RTOS/HAOFV | 约 39% | 双核主线已建立；AO、保护、发布和长期验证仍有大量待办。 |
| Calibration | 约 39% 原始勾选 | 当前最活跃，也是实时主链路的直接瓶颈。 |
| RS485 | 约 17% 原始勾选 | SCPI 短帧基础已有；V2 OTA 和完整 DHRT100 HIL 尚未完成。 |

事实源：各域 `*_TODO.md`、`*_TASK_PROGRESS.md` 和 canonical Architecture 文档；本表不替代这些文件。

## 关键偏差

### Calibration TRN-02

- TRN-01 A-D 已完成当前 build/拓扑下的诊断闭环。
- TRN-02A、TRN-02C 已完成；TRN-02B、TRN-02D 仍进行中。
- 当前四链路证据为单 profile、每链路一次 repeat，且保持 `diagnostic_only`。
- 在关闭 TRN-02B/D 前，不得进入 TRN-03 ARM/START、发布 active calibration 或向 VDC/DPLL 发布正式路径延迟。
- 进行固定频率阶梯前，应解释训练记录中的 `OPMODE`/`OPMODE_APPLY` timeout，并确认 profile 切换状态读回语义。

证据入口：`docs/calibration/CALIBRATION_DOMAIN_TODO.md`、`docs/calibration/CALIBRATION_TASK_PROGRESS.md` 及其引用的训练 `summary.json`。

### VDC/DPLL

VDC 已具备 schedule、timestamp gate、clock model、DCO snapshot 和诊断监控，但仍主要依赖 host 交替触发单向 leg，尚未形成固件内同时运行的 UP/DOWN feedback loop。`WINDOW_BOUND`、`BAD_FRAME` 和非连续 `FINE_100NS` 仍是产品化风险。正式出口需要无 host 续窗的常驻运行、可追溯 ring evidence 和稳定质量/新鲜度。

事实源：`docs/vdc/VDC_DOMAIN_TODO.md` 的 P0.5b 及 `docs/vdc/VDC_TASK_PROGRESS.md`。

### RefMem 可靠性

AUTO NodeLoad 仍可能在 `WINDOW_MISSED` 时丢失单发 DELTA。必须补齐 `origin_encoded -> transport_sent -> target_validated -> target_committed -> acked/fenced` 完成证据、有界重发/backoff、重复序号处理和故障注入，之后才适合扩大跨节点 active activation。

事实源：`docs/refmem/REFMEM_DOMAIN_TODO.md` 的 P0 可靠性项和 P4/P7 验证项。

### 顶层 HAOFV 合规

登记表显示：5 条 active 契约、10 条 pending 契约和 1 条 superseded 记录；顶层条款中仍存在 `PARTIAL`、`PENDING`，且 `HAOFV-879` seqlock 细化条款为 `VIOLATED`。登记覆盖通过不等于实现合规通过。

事实源：`docs/check/DOCS_REGISTRY.md`。

## 文档治理审查

2026-08-24 实时检查结果：

- `docs_check.py --strict-names`：114 个文档，0 warning。
- `doc_regression_check.py`：freshness、registry、orphan、log-check 通过。
- 检查器相关 pytest：17/17 通过；沙箱固定 `out/pytest/runs` 时出现的是临时目录权限错误，改用可写临时目录后通过。
- 当前登记表有一条旧格式 ID `TDMA-FLIGHT-BITMAP-01` 未被 `ROW_RE` 计数，只产生 WARN；因此登记行数与机器实际校验行数不一致。
- T24-T27 仍未关闭：module 锚点校验、外部绝对路径、过期截止日期描述和“约 190 条”硬数字标注。

这些结果说明门禁体系有效，但仍有检查器覆盖盲区；详见 `DOCS_REGRESSION_TODO.md` 的 T24-T29。

## 当前在制改动边界

工作区存在未提交的校准训练数据、TDMA 物理层和相关工具改动。它们应视为 TRN-02 的在制实现，不能在本快照中被解释为已合并基线或产品完成证据。代码、单测、构建和 HIL 必须按代码变更与文档变更分离提交。

## 建议顺序

1. 完成 TRN-02 固定 profile 阶梯、多次 repeat 和同 generation residence 证据。
2. 完成 TRN-03 per-link staging、周期预算、短帧/FIFO 闭环和故障注入。
3. 完成 endpoint bias、topology/profile freshness、active/staging/rollback，解除 `diagnostic_only`。
4. 修复 `HAOFV-879` seqlock，并让 TDMA UP/DOWN 成为固件内常驻同时运行环路。
5. 让 VDC 完成无 host 续窗的 5 分钟闭环，稳定达到 `FINE_100NS`。
6. 为 RefMem 补 ACK/重发/fence completion，再推进跨节点 active activation。
7. 之后再推进 T2、24 小时长稳和发布门禁；文档治理 T24-T29 并行清理。

## 审查边界

本次审查未修改代码，未重新执行完整 CMake 固件构建、四板烧录或实时 HIL；本文件不替代已有板端证据，也不改变任何 registry contract status。
