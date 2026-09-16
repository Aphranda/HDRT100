# 核验提交单：VDC 服务边界频率命令

Status: Draft
Domain: VDC
Canonical: `docs/check/submissions/VDC_CROSS_REVIEW_01.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`, `docs/arch/HAOFV_ARCHITECTURE.md`
Last updated: 2026-09-16

## 提交内容

提交 `VDC-BOUNDARY-01` 的独立 wire 类型、固定配额、目标模型年龄、逐从未决命令、
实际应用 ACK 和连续重基规则。canonical 条款在 VDC Architecture，登记状态为
pending；本提交单不冻结另一份字段表，不把待实现语义宣称为实板闭环。

## 偏差声明

首期只显式启用单次诊断校正，自动频率跟踪、物理相位精度、完整时序和恢复均需
后续验收。完整绝对时间映射及启动首帧精细证明不作为本地频率命令的前置；旧绝对
时间 API 的规则不放宽。运输完成不能代替 Core1 实际应用确认。

## Alternatives considered

- 复用旧绝对生效时间命令：拒绝，本地服务时间与旧共同时间语义不同。
- 复制 NO1 的 DCO 补偿：拒绝，不能反映各从板独立误差。
- 独立边界命令并复用已配置邮箱：采用，允许从现有稳态观测渐进接通实际应用。

## 核验结论

- 结论：ACCEPT_WITH_DEVIATION，独立 C11 已批准 canonical/登记/接口语义；契约保持 pending，实际接线及硬件退出尚待验证。
- 当前源码尚未实现命令运输及实际应用接线，不授予硬件验收资格。

## 交叉审核记录（C11）

- 审核方：`command_gate_audit`（独立于文档及实现作者）。
- 审核方式：agent 交叉，定向检查 wire、owner、取消和跨核接口。
- 审核结论：ACCEPT_WITH_DEVIATION；主机依据年龄复算与 probe 显式重新授权两项修正已复核。
- 审核日期：2026-09-16。
- 方案依据：`out/HardwareAcceptance/20260916/dpll-boundary-command-r1/design-review-r1.json`。
- 本次具体差异审核：`out/HardwareAcceptance/20260916/dpll-boundary-command-r1/c11-review-r1.json`；
  不以先前草案审核代替本次 C11。
