# 评估与监督域

Status: Active
Domain: EVALUATION
Canonical: `docs/evaluation/README.md`
Related: `docs/README.md`, `docs/check/DOCS_REGRESSION_PLAN.md`, `docs/check/DOCS_PROJECT_STATUS_REVIEW_20260824.md`, `docs/arch/HAOFV_ARCHITECTURE.md`
Last updated: 2026-09-05

本目录是 Distributed Hard Real-Time Trigger System 的**价值评估与长期监督域**。它收容对
项目的开源潜力、商业价值与学术价值的评估结论、行动追踪与复评闭环，作为长期审核监督项目
运行，随工程演进滚动更新。

## 域定位

| 文件 | 定位 | 使用规则 |
|---|---|---|
| `PRODUCT_VALUE_EVALUATION.md` | 项目价值评估 canonical：上游评估(R)/证据(E)→判断(J)→结论(C)→行动(A) 单链结论。 | 评审快照，非契约，不登记 `DOCS_REGISTRY.md`（C7）；被监督域每次更新时回到本文件刷新证据与判断。 |
| `PRODUCT_VALUE_TRACKING_TODO.md` | 评估行动 A1–A7 的状态追踪（唯一状态事实源）。 | 只维护状态与退出门禁；证据回引工程域 Task Progress 与 `out/` 验收目录。 |
| `README.md`（本文件） | 域入口：定位、边界、追踪规则与文件索引。 | 定义"何时更新、由谁更新、更新什么"，不保存评估正文。 |

## 与相邻域的边界

- `docs/arch/`（架构/产品 canonical + 专项评估：`docs/arch/HAOFV_PORTABILITY_EVALUATION.md`、
  `docs/arch/HAOFV_ARCHITECTURE_RISK_EVALUATION.md`）：本域**消费**其结论（R1/R2/R3/R4），不修改它们。
- `docs/check/`（文档契约/治理门禁 + 项目状态审查）：本文档为评审快照，**不得登记为契约**
  （`docs/check/DOCS_REGRESSION_PLAN.md` C7）；状态审查快照（R5）是本域完成度输入。
- 工程各域 `*_TODO.md` / `*_TASK_PROGRESS.md`：执行与证据在工程域维护，本域只引用并追踪
  "评估视角"的完成度，不复制工程流水账。

## 追踪规则（长期审核监督运行方式）

本域按"复评信号驱动、事件留痕、滚动更新"方式运行：

1. **更新触发（任一发生即应更新）**：
   - 上游专项评估或状态审查更新（R1/R2/R3/R5 任一修订）；
   - 行动 A1–A7 任一项落地/受阻（许可证落地、DPLL 复现 LOCKED、endpoint bias 解除、
     pico-sdk 钉版、论文/专利动作、平台独立化决策等）；
   - 关键 HIL/门禁里程碑：`TDMA-DPLL-*`、P3 硬件验收 receipt、四板 LOCKED+T2+24 h 长稳；
   - 公司 IP/开源决策变化（`docs/arch/ARCH_FUTURE_APPLICATION_PLAN.md` F0–F4 阶段推进）。
2. **更新动作**：
   - 在 `PRODUCT_VALUE_EVALUATION.md` 刷新受影响证据(E)/判断(J)/结论(C)/行动(A)，追加复评
     记录并更新 `Last updated`；
   - 在 `PRODUCT_VALUE_TRACKING_TODO.md` 更新对应行动状态（DONE/IN PROGRESS/PENDING/BLOCKED）
     与证据引用；
   - 跑文档自回归门禁（见 §验证）；涉及状态变化遵循 C11 交叉审核精神，评估更新需注明依据。
3. **输出节奏**：无固定日历周期，以事件驱动为主；建议每季度至少一次全面复评。

## 验证命令（域内改动后必跑）

```powershell
python tools/docs_check/docs_check.py --strict-names
python tools/doc_regression_check.py
python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider --basetemp <可写临时目录>
sh .githooks/pre-commit
```
