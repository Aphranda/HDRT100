# 核验提交单归档

Status: Active
Domain: Documentation Governance
Canonical: `docs/check/submissions/README.md`
Related: `docs/check/DOCS_REGISTRY.md`, `docs/check/DOCS_REGRESSION_TODO.md`
Last updated: 2026-09-02

> 用途：层间逐级核验（域 → 父层）的提交单归档目录。每份提交单一个文件：
> `docs/check/submissions/<DOMAIN>_CROSS_REVIEW_<NN>.md`
> （如 `TDMA_CROSS_REVIEW_01.md`；命名须满足 docs_check 规则：前缀在 ALLOWED_PREFIXES、后缀 REVIEW）。

## 提交单模板（复制使用）

```markdown
# 核验提交单：<域> → <父层>

Status: Active
Domain: <域>
Canonical: `docs/check/submissions/<文件名>`
Related: <父层文档>, `docs/check/DOCS_REGISTRY.md`
Last updated: <YYYY-MM-DD>

## 提交内容

| 父层条款 | 符合性 | 证据 |
|---|---|---|
| HAOFV-879 seqlock 快照 | ❌ | adapter get_snapshot 裸读，见偏差声明 |

## 偏差声明

- <contract_id>: 计划<修复方案>；接受理由=<理由>

## Alternatives considered

- 方案A <描述>（拒绝：<原因>）
- 方案B <描述>（接受）

## 核验结论

- 结论: ACCEPTED / ACCEPT_WITH_DEVIATION / REJECTED
- 核验人: <owner>

## 交叉审核记录（C11，必填）

- 审核方: <另一 agent / 人工 / 另一份文档>
- 审核方式: agent 交叉 / 文档交叉 / 层间交叉
- 审核结论: PASS / PASS_WITH_NOTE / FAIL
- 审核日期: <YYYY-MM-DD>
```

## 规则

- 每份提交单对应一次层间核验；结论必须写回 `docs/check/DOCS_REGISTRY.md` 的契约/条款状态
- 交叉审核记录必填（C11），禁止自审自批
- 归档文件不删除（历史可追溯，对应 git reflog 审计链）

## 已归档提交单

| 提交单 | 范围 | 结论 |
|---|---|---|
| `TDMA_CROSS_REVIEW_01.md` | TDMA seqlock 与顶层 HAOFV | `ACCEPT_WITH_DEVIATION` |
| `TDMA_CROSS_REVIEW_02.md` | TDMA mandatory-first process image 与 HAOFV/VDC/RefMem | `ACCEPT_WITH_DEVIATION` |
| `TDMA_CROSS_REVIEW_03.md` | TDMA bounded recovery 双 buffer、原 Node offset 与实时诊断边界 | `ACCEPT_WITH_DEVIATION` |
| `TDMA_CROSS_REVIEW_04.md` | TDMA resident process image、单轮多 Node overlay 与状态机生命周期 | `ACCEPT_WITH_DEVIATION` |
| `ARCH_FLASH_CROSS_REVIEW_01.md` | Flash v2 与 hardware/build/Boot/OTA/TDMA/RefMem/VDC | `ACCEPT_WITH_DEVIATION` |
| `ARCH_FLASH_CROSS_REVIEW_02.md` | Flash canonical 重构与 M0-M6 工作板 | `ACCEPT_WITH_DEVIATION` |
