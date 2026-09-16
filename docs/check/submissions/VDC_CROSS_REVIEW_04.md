# 核验提交单：VDC Core1 同步邮箱直接发布

Status: Draft
Domain: VDC
Canonical: `docs/check/submissions/VDC_CROSS_REVIEW_04.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`, `docs/arch/HAOFV_ARCHITECTURE.md`
Last updated: 2026-09-17

## 提交内容

提交 `VDC-PRIORITY-01` pending：独立 typed body、output-ns 算术区间、STOP-only
运行代际和 Core1 origin provider；TDMA owner 保持 PIO/DMA 与双缓冲退休所有权。

## 偏差声明

同步单位和事件身份明确，不代表 GPIO 误差已校准。编码/offer、物理提交、选中、
运输、消费和控制分别证明；当前条款不授予 DCO、单圈期限或锁相。普通分片路线
的历史参考/ACK 结果不能替代本路径证据。

## Alternatives considered

- 通过普通 Core0 overlay 工位准备每次同步记录：不满足特等席确定性边界。
- Core1 复用独立 origin exchange 双缓冲：采用，保留硬件 selection 退休规则。
- 将区间取中点或用 RATE 坐标冒充 VDC 时间：不采用，保留 committed 模型和上下界。

## 交叉审核记录（C11）

- 审核方：`p0_root_review`，独立于源码与文档作者。
- 审核方式：只读源码/负测/资源复核，契约、登记和顶层映射复核。
- 初次 owner 审核指出普通工位等待、注册 STOP guard、同步 fallback 类型隔离
  三项问题；已修正并由独立方复核，原报告保留。
- 本次契约结论：ACCEPT_WITH_DEVIATION，同意 `VDC-PRIORITY-01` pending 登记，
  不批准 active；已纠正 mailbox event sequence 与 transport carrier sequence
  的文字歧义。审核日期 2026-09-17，原件为 `c11-review.json`。
- 证据根：`out/HardwareAcceptance/20260917/dpll-priority-tx-r1/`。

## 接收交接增补（v2 pending）

`VDC-PRIORITY-01` v2 增加 STOP 注册固定接收入口、发布后同步交接、承载帧完整
序号回绕后的 epoch、退休及重复/冲突语义。IRQ 只执行定长解码和留存，不执行
PI 或取得 PIO/DMA 所有权；原轮询诊断与新入口分离。热链迁入主 RAM 不替代
实测预算或单圈期限证明。

独立方 `p0_root_review` 完成源码、负测、A/B 资源及 C11 复核，结论
`APPROVE_PENDING_REGISTRATION`；修正“载荷回绕”为“承载帧完整序号回绕”。
仍不批准 active、DCO 或锁相。原件在
`out/HardwareAcceptance/20260917/dpll-priority-rx-direct-r1/c11-review.json`，
代码/资源/实板审核分别见同目录 `code-review.json`、`resource-review.json`、
`hardware-review.json`。IRQ 候选预算仍未满足，P3 严格失败保留于 Task Progress。

## 精确匹配增补（v3 pending）

`VDC-PRIORITY-01` v3 增加 Core1 live handoff、源事件直接索引、STOP 请求会话
绑定、实际 committed DCO 投影和带暂定正向 CS delay 的残差区间。匹配器不发
DCO 命令或 ACK，不授予物理精度与单圈期限。已有 reverse-DATA 矩阵只在方向
来源明确时转置；中继驻留和端点校准留至后续精度任务。

独立方 `p0_root_review` 对照源码、负测、资源与三份契约文档复核，指出并闭合
首次匹配前未锁存 session 的实现缺口，以及 RX 交接暂不可用与 epoch 退休的
文字差异。C11 结论 `APPROVE_PENDING_REGISTRATION`，仍不批准 active。
原件为 `out/HardwareAcceptance/20260917/dpll-priority-match-r1/c11-review.json`；
代码及资源复核见同目录 `code-review.json`、`resource-review.json`。本次硬件
判定由 Task Progress 引用原始专项，不以本增补替代。

## 最终核验结论

ACCEPT_WITH_DEVIATION。P3、资源和 typed 实板专项的实际结果只记录在 VDC Task Progress，
不能以 pending 登记代替验收。
