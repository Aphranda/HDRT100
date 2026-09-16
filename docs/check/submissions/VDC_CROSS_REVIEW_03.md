# 核验提交单：VDC 特等席控制及参考事件运输

Status: Draft
Domain: VDC
Canonical: `docs/check/submissions/VDC_CROSS_REVIEW_03.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`, `docs/arch/HAOFV_ARCHITECTURE.md`
Last updated: 2026-09-16

## 提交内容

提交 `VDC-BOUNDARY-01` 修订：独立 RATE 反馈坐标与显式 STOP/AUTO 授权，Core0
持有同模型测量窗口，Core1 按各从误差生成有界负反馈，实际应用与精确 ACK 后再
接受新模型整窗；跨核争用仅暂停准备，不等价于模式关闭。

## 偏差声明

AUTO 与单次 probe 保持独立授权；旧 raw/MODEL 反馈语义不变。RATE 区间描述
内部 committed DCO 模型的频差，不直接授予物理 GPIO 误差、LOCKED、整表 WCET
或用户精度目标。未知应用结果不叠加命令，完整异常恢复另验。
板端命令记录为 decoded 语义副本，只有 ACK 记录为原始反馈字节；不得由重编码
声称原始 TX/RX 见证。记录满不等于完整采集。

## Alternatives considered

- 收窄旧 MODEL 区间：不采用，改变既有时间戳不确定度语义。
- 源与参考分别在同模型 RATE 坐标做差：采用，保留参考逐发括号与算术取整边界。
- 只有单次受限增量或持续 HOLD：保留诊断价值，但不能通过自动多轮控制验收。

## 核验结论

ACCEPT_WITH_DEVIATION：本轮独立源码、采集控制与 C11 复核通过，登记保持 pending。
硬件多轮 AUTO、物理锁相、精度与完整异常恢复仍由对应专项证明；测试和硬件结果
见 `VDC_TASK_PROGRESS.md`，不以设计评审替代实现或硬件证据。

## 交叉审核记录（C11）

- 审核方：`command_gate_audit`，独立于实现与文档作者。
- 审核方式：agent 只读审阅、实际源码 host 反例和原件核对。
- 审核结论：ACCEPT_WITH_DEVIATION；独立源码及采集控制测试通过，未授予物理锁相、
  用户精度目标、完整静态调度预算或完整异常恢复资格。
- 审核日期：2026-09-16。
- 证据目录：`out/HardwareAcceptance/20260916/dpll-auto-frequency-r1/`。
- 具体差异审核：`source-capture-c11-independent-review-r1.json`，SHA256
  `50ee60c5047612f3b3018243a010b18db9d7b8062e8f0730dbc85f0d22fb6ed7`。
- 后继 AUTO-only 模式冻结源码复核：`capture-mode-source-independent-review-r2.json`，
  同源四板 P3 原件复核 `p3-resume-independent-review-r1.json`；实板专项复核
  `auto-r5-independent-review-r1.json` 接受记录容量修复，但因全从多轮未闭合，
  保持完整 AUTO、频偏收敛及锁相资格未授予。后继交接修复须使用自己的证据。

## 后继参考事件运输审核范围

新增 `VDC-REFERENCE-01` 登记候选，状态保持 pending：显式 REFERENCE 与旧控制
互斥，复用 MODEL 格式和现有静态存储；只证明已提交 DCO 的参考事件发布/接收，
不声明 NO1 自主 PI、三从 ACK、本地 delay 跟踪或锁相已经完成。

本轮独立方 `command_gate_audit` 已审阅实现与采集边界，定向 host 验证通过；
新增域条款、登记及顶层映射的 C11 结论为 ACCEPT_WITH_DEVIATION，同意登记为
pending，不批准 active。审核日期为 2026-09-16；报告为
`reference-contract-c11-review-r1.json`，SHA256
`ab3cefd09be1e683ac8a383e749547198236b71a867e409ae063bfbd64c67076`。
该结论不替代 Release、当前源码四板 P3 和运输专项，也不授予持续 PI、ACK 或锁相。
原始记录位于 `out/HardwareAcceptance/20260916/dpll-reference-publish-r1/`，
采集工具与硬件原件位于 `out/HardwareAcceptance/20260916/dpll-local-follow-r1/`。

## 参考接收确认增量审核范围

`VDC-REFERENCE-01` v2 候选保持 pending，新增独立 typed RECEIPT_ACK 和每来源完整
发布证明。确认只说明收到原参考，既不等待本地配对/DCO 采用，也不授予应用或锁相。
复用现有快照 union，旧控制入口必须拒绝该 arm；有限历史不提升为全部组必达。

实施、host、采集器及后继构建/硬件证据位于
`out/HardwareAcceptance/20260916/dpll-receipt-ack-r1/`。独立方 `command_gate_audit`
于 2026-09-16 完成设计/文档 C11 增量审核，结论 ACCEPT_WITH_DEVIATION，仅同意
v2 pending 登记；实现与硬件尚未批准，须用本次冻结源码和新验收原件另行复核。
报告 `receipt-contract-v2-c11-review-r1.json`，SHA256
`a59c1f55c0fa5d494134e842db308acee40584a6b28e51b5787c74d3434b567d`。
v1 评审不自动覆盖本次 wire 类型和内存解释变更；不得以 pending 登记冒充验收。
