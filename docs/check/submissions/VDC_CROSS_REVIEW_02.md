# 核验提交单：VDC 同命令有界重复运输

Status: Draft
Domain: VDC
Canonical: `docs/check/submissions/VDC_CROSS_REVIEW_02.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`, `docs/arch/HAOFV_ARCHITECTURE.md`
Last updated: 2026-09-16

## 提交内容

提交 `VDC-BOUNDARY-01` 修订：同一不可变 offer 在原有效期内允许有界重复运输，
完整组之间成功发布普通 mailbox，精确 ACK 退休匹配 offer。命令 wire、逐从一次
诊断额度及 Core1 实际应用条件保持；语义以 VDC Architecture 为唯一事实源。

## 偏差声明

运输重复不等于重复校正。TX-done 只表示有限发送批次全部发布，ACK 提前退休时
批次完成计数可以为零；在途片段依靠从板去重和已用额度保持至多一次应用。
当前修订不授予自动控制、锁相、完整静态调度时序或物理精度资格。

## Alternatives considered

- 延长 TTL 或按接收时间续期：不采用，不能解决交付完整性且会放宽原测量年龄。
- 丢 ACK 后重新计算增量：不采用，未知是否已应用时可能叠加校正。
- 同一 offer 内有界重复完整组：采用，复用原发送记录和原准入检查，限制资源增量。

## 核验结论

- ACCEPT_WITH_DEVIATION：本次独立 C11 已批准修订；契约保持 pending，硬件及专项
  结果另见 VDC Task Progress，不授予自动控制或物理精度资格。

## 交叉审核记录（C11）

- 审核方：`command_gate_audit`（独立于文档及实现作者）。
- 审核方式：agent 交叉，复核 owner、重传边界、原有效期及至多一次应用。
- 审核结论：ACCEPT_WITH_DEVIATION；有界重复、提前 ACK 和评估器修订通过独立复核。
- 审核日期：2026-09-16。
- 证据目录：`out/HardwareAcceptance/20260916/dpll-command-delivery-r1/`。
- 具体差异审核：该目录 `source-capture-c11-independent-review-r1.json`；独立审核
  的源码、文档和测试摘要均绑定原件，不以设计草案替代本次 C11。
