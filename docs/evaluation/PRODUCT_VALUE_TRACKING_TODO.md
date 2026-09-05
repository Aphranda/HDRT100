# 项目价值评估行动追踪

Status: Active
Domain: EVALUATION
Canonical: `docs/evaluation/PRODUCT_VALUE_TRACKING_TODO.md`
Related: `docs/evaluation/README.md`, `docs/evaluation/PRODUCT_VALUE_EVALUATION.md`, `docs/arch/ARCH_FUTURE_APPLICATION_PLAN.md`, `docs/check/DOCS_PROJECT_STATUS_REVIEW_20260824.md`
Last updated: 2026-09-05

本文件是价值评估域的行动追踪 TODO：只维护 `PRODUCT_VALUE_EVALUATION.md` §4 行动 A1–A7 的
状态、证据与退出门禁；不复制评估结论，不替代工程域 TODO。

## 文档接口

- 评估语义与行动定义：`PRODUCT_VALUE_EVALUATION.md`（§4 行动层 A1–A7）。
- 域规则与更新触发：`docs/evaluation/README.md`。
- 工程执行证据：各域 `*_TODO.md` / `*_TASK_PROGRESS.md` 与 `out/` 验收目录。
- 本文件状态变化必须回到 README 触发信号校验，并在评估文档留痕。

## 状态规则

- 状态只使用 `DONE` / `IN PROGRESS` / `PENDING` / `BLOCKED`。
- 仅当证据（receipt/报告/commit/决策）落地才可标 `DONE`；单方口头结论不构成退出。
- 行动状态是"评估视角"的追踪；实际工程执行由对应工程域 TODO 拥有，本文件不得复制流水账。

## 当前主线

按投入产出比推进 A1–A3（开源放行）→ A4–A5（工程闭环与产品化）→ A6–A7（验证与学术/IP）。
其中 A4 依赖工程域 DPLL/Calibration 主线，A6 依赖 A4 复现的四板 LOCKED 证据。

## 任务表

| ID | 任务（行动定义见评估文档 §4） | 状态 | 完成或退出门禁 | 证据/追踪 |
|---|---|---|---|---|
| A1 | 落地 LICENSE + third_party 许可证清单 + CONTRIBUTING/CHANGELOG/SECURITY + README 英文化 | PENDING | 根目录出现 LICENSE/CONTRIBUTING/CHANGELOG/SECURITY；README 英文入口可用 | — |
| A2 | 厂商/开发证据清理 + 分层开源（先开 portable_ota/log、工具、simulator/validator/visualizer） | PENDING | 厂商串/序列号/台架拓扑与开放内容分层完成；开放目录有独立打包与复现说明 | — |
| A3 | GitHub topics/描述/Releases + 一键复现脚本（hooks 配置、钉死 pico-sdk） | PENDING | clone→构建→host 测试可一键复现；hooks 文档化 | — |
| A4 | 解 DPLL P1 板端 OTA 回滚（COM3/COM25）与 endpoint bias 硬件环回；重构分支复现四板 LOCKED+T2+24 h 长稳 | PENDING | `TDMA-DPLL-*`/P3 验收 receipt 显示四板复现 LOCKED、FINE_100NS 档与 24 h 长稳 | 归工程主线（TDMA/VDC/Calibration 域 TODO） |
| A5 | 冻结产品板硬件约束 + AMC1301 修复 + USB VID/PID 产品化 | PENDING | 产品板约束文档非 Draft；AMC1301 阻塞解除；VID/PID 确定 | 归 `docs/hardware/` 与 HIL 记录 |
| A6 | 第三方/跨平台（STM32H7）精度复现 + 误差分解 + 整理既有两板 LOCKED HIL 证据链 | PENDING | 复现报告 + 误差分解文档；既有证据链可对外主张 | — |
| A7 | IP 审查 + 实证/架构两条线论文与脱敏数据 artifact | PENDING | 公司 IP 放行；论文草稿或 artifact 发布 | 待 A6 实验与公司决策 |

## 当前阻塞项

- A4：`TDMA-DPLL-*` 与 Calibration endpoint bias（P3-03/P3-LB-06）属工程主线阻塞，
  非评估域可解除；本文件只追踪状态。
- 其余行动无阻塞，等待执行窗口。

## 统一完成定义

A1–A7 全部 `DONE` 且评估文档 C1 放行手续（许可、收敛后的闭环证据、IP 授权）齐备后，
本追踪 TODO 转 `DONE` 归档；后续新价值议题在评估文档复评记录中另立。
