# 核验提交单：TDMA process image → HAOFV / VDC / RefMem

Status: Active
Domain: TDMA
Canonical: `docs/check/submissions/TDMA_CROSS_REVIEW_02.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-08-28

## 提交内容

| 父层条款 | 符合性 | 证据 |
|---|---|---|
| TDMA 是确定性 transport 唯一 owner | 符合 | `tdma_process_image_layout.h` 只冻结机械布局；publisher/parser 仍在域 owner 边界内。 |
| VDC 是共同时间唯一 owner | 符合 | wire 只镜像 phase/rate/lock/quality 摘要，量化值不回写本地 DPLL。 |
| RefMem 是共同事实 owner | 符合 | critical delta 保留独立 generation/field/value；fast-header seq 不替代 commit/ACK。 |
| mandatory-first、编译期拒绝 overcommit | 符合 | `app_runtime.c` 编译断言和 `tdma_process_image_budget.py` 同源检查。 |
| 多板运行证据 | 待完成 | 新 wire version 已构建但尚未执行 NO.1..NO.5 OTA/HIL。 |

## 偏差声明

- `TDMA-PROCESSIMAGE-01` 保持 `pending`：正式 ACK/fence consumer、control opcode owner 和五板
  HIL 尚未闭环，当前只冻结基础布局与准入规则。

## Alternatives considered

- 保留 RefMem 独占整个 Node body（拒绝：无法同时承载节点锁相/VDC 和最小控制）。
- 运行时按剩余空间追加 payload（拒绝：破坏固定 wire time 和编译期确定性）。
- mandatory-first 固定布局，余量只在构建阶段静态准入（接受）。

## 核验结论

- 结论: ACCEPT_WITH_DEVIATION
- 核验人: TDMA↔VDC↔RefMem canonical 文档层间交叉核验

## 交叉审核记录（C11，必填）

- 审核方: `VDC_DOMAIN_ARCHITECTURE.md` 与 `REFMEM_DOMAIN_ARCHITECTURE.md` 的既有 owner/输入契约
- 审核方式: 文档交叉 + 层间交叉（域文档 ↔ code anchor ↔ 预算工具/测试）
- 审核结论: PASS_WITH_NOTE（NOTE: registry 保持 pending，五板 HIL 与 completion owner 完成后另行激活审核）
- 审核日期: 2026-08-28

## 处置

- 新增 `TDMA-PROCESSIMAGE-01` pending 登记并刷新 HAOFV 顶层可见性。
- HIL 失败时整体回退 wire-layout 变更，不通过关闭 mandatory 字段绕过。
