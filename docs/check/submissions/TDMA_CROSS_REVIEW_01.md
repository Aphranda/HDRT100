# 核验提交单：TDMA → HAOFV 顶层（HAOFV-879 seqlock 偏差）

Status: Active
Domain: TDMA
Canonical: `docs/check/submissions/TDMA_CROSS_REVIEW_01.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-08-19

> 首份提交单（逐级核验流程首次实操，T19）。
> 提交方: TDMA 域 → 核验方: HAOFV 顶层（由独立 AI 评审交叉审核）。

## 提交内容

| 父层条款 | 符合性 | 证据 |
|---|---|---|
| HAOFV-139 跨核共享事实（多字段必须 seqlock/双缓冲/version） | ⚠️ 部分 | flight fifo/engine 已用 atomic + owner 状态机；见偏差声明 |
| HAOFV-879 多字段事实必须 seqlock | ❌ | `tdma_pio_spi_ring_adapter_get_snapshot`（`tdma_pio_spi_ring_adapter.c:662-713`）裸读约 30 个 core1 写的字段，无 seqlock/sequence |

## 偏差声明

- TDMA-SEQLOCK-01: adapter get_snapshot 缺 seqlock；owner=TDMA 域；
  计划=随迁移阶段修复（见 `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md:575` seqlock snapshot 要求）；
  接受理由=当前 `DIAGNOSTIC_ONLY` 阶段无 100ns 硬件时间戳证据需求（`tdma_runtime_owner.c:68`），
  证据门保持关闭，不产生虚假闭环结论。

## Alternatives considered

- 方案A 立即加 seqlock 快照（拒绝：改动 adapter 结构 + 迁移阶段未到，收益低）
- 方案B 登记偏差、分批修复、状态保持 VIOLATED 持续报警（接受）

## 核验结论

- 结论: ACCEPT_WITH_DEVIATION
- 核验人: 独立 AI 评审（非代码作者）

## 交叉审核记录（C11，必填）

- 审核方: 独立 AI 评审（agent 交叉，非 Codex/代码作者）
- 审核方式: agent 交叉 + 文档交叉（对照 `TDMA_DOMAIN_ARCHITECTURE.md:575,606` seqlock 契约）
- 审核结论: PASS_WITH_NOTE（NOTE: 偏差可接受，但 HAOFV-879 状态保持 VIOLATED 直至修复）
- 审核日期: 2026-08-19

## 处置

- 登记表 `docs/check/DOCS_REGISTRY.md` 条款落点表 HAOFV-879 保持 VIOLATED（持续报警直至修复）
- 修复计划在 `docs/temp/TDMA_CODE_REVIEW.md` P1-4（给 Codex 的代码修复单）
