# 核验提交单：TDMA bounded recovery → HAOFV / VDC / RefMem

Status: Active
Domain: TDMA
Canonical: `docs/check/submissions/TDMA_CROSS_REVIEW_03.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-08-28

## 提交内容

| 父层条款 | 符合性 | 证据 |
|---|---|---|
| TDMA 是确定性 transport 唯一 owner | 符合 | recovery 的 window、预算、PIO/DMA 发送和 completion 均归 TDMA owner。 |
| Core0/Core1/PIO owner 边界 | 符合 | Core0 准备数据，Core1 只选择 buffer/装载 FIFO，PIO/DMA 发送。 |
| 固定短帧 wire plan 不被重传改变 | 符合 | recovery 复用原 Node segment offset，不追加 mailbox、不扩展正常 SHORT。 |
| 双 buffer 与有界恢复 | 部分符合 | 架构和现有 scheduler 已有 `EMPTY/READY/IN_FLIGHT`、交替、单周期单帧、retry/backpressure 语义；原 Node offset 的产品态接入和四/八节点 HIL 尚未完成。 |
| 实时诊断隔离 | 符合 | SD/SVG、原始波形和详细归因留在 Core0 或 maintenance/LONG；短帧/recovery 只保留基础摘要。 |

## 偏差声明

- `TDMA-RECOVERY-01` 保持 `pending`：代码仍需把现有 recovery scheduler 与正式 process-image owner、原 Node segment offset 和 ACK/fence consumer 完整接通，并完成板端错误注入证据。

## Alternatives considered

- 在正常 SHORT 中增加两个冗余 Node mailbox（拒绝：会改变固定 payload/wire 长度并增加 PIO 传输预算）。
- 运行时借用 guard 或剩余字节发送 recovery（拒绝：破坏拍级确定性和独立预算）。
- Core1 解析 SD/SVG/详细诊断后决定重传（拒绝：把非实时工作引入实时路径）。
- Core0 双 buffer 准备、Core1 固定窗口装载、PIO 发送并复用原 Node offset（接受）。

## 核验结论

- 结论: ACCEPT_WITH_DEVIATION
- 核验人: TDMA↔HAOFV/VDC/RefMem canonical 文档层间核验

## 交叉审核记录（C11，必填）

- 审核方: `HAOFV_ARCHITECTURE.md`、`TDMA_DOMAIN_ARCHITECTURE.md`、`TDMA_DOMAIN_TODO.md` 三件套交叉约束
- 审核方式: 文档交叉 + 登记表/code anchor/现有 scheduler 单测接口核对
- 审核结论: PASS_WITH_NOTE（NOTE: 契约保持 pending，待正式 owner 接线、预算工具和多板 HIL 后激活）
- 审核日期: 2026-08-28
