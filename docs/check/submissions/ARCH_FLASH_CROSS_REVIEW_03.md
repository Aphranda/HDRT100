# Flash Boot A/B fault matrix 核验提交单

Status: Active
Domain: Flash / OTA
Canonical: `docs/check/submissions/ARCH_FLASH_CROSS_REVIEW_03.md`
Related: `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`, `docs/arch/HAOFV_FLASH_TODO.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-08-24

## 提交内容

本次核验覆盖 Direct A/B 的 slot-specific image、向量/reset handler、CRC、镜像
SHA-256、manifest signature、product/hardware/bootloader/security compatibility，
以及 Recovery 只读投影。Boot 与 Recovery 都只读校验 inactive/active slot，Recovery
不提供任何 Flash writer 或 BCB 写回路径。

| fault | Boot 结果 | Recovery `SYST:RECOVERY:AB:STATUS?` slot 结果 | 判定 |
|---|---|---|---|
| 空 slot | `SLOT_EMPTY` | `SLOT_EMPTY` | PASS |
| 越界/零长度 | `SLOT_RANGE_INVALID` | `SLOT_RANGE_INVALID` | PASS |
| 初始 SP 或 reset handler 不在 slot | `VECTOR_INVALID` | `VECTOR_INVALID` | PASS |
| 镜像 CRC 不一致 | `IMAGE_CRC_INVALID` | `IMAGE_CRC_INVALID` | PASS |
| 镜像 SHA-256 不一致 | `IMAGE_HASH_INVALID` | `IMAGE_HASH_INVALID` | PASS |
| 签名/验签器失败 | `SIGNATURE_INVALID` | `SIGNATURE_INVALID` | PASS |
| product、hardware、bootloader、counter、slot/run offset 不兼容 | `COMPATIBILITY_INVALID` | `COMPATIBILITY_INVALID` | PASS |
| Recovery 向量/镜像不可用 | `RECOVERY_UNAVAILABLE` 写入 BCB | ROM/Recovery indication | PASS |

## 证据

- `tests/python/test_direct_ab_fault_matrix.py`：fault 名称、Boot 映射、Recovery 只读边界和
  manifest/hash/signature 入口回归。
- `tests/python/test_v2_direct_ab_policy.py`：Direct A/B pending/confirm/revert 策略回归。
- `tools/tests/run_portable_ota_tests.ps1`：portable package、image/vector、BCB、stream 回归。
- `out/build/pico2-release/`：v1 compatibility Boot/App A/App B link gate。
- `out/build/pico2-v2-factory-candidate/`：v2 debug 签名 Boot/App A/App B/Recovery link gate，
  `flash_link_contract=OK profile=recovery`。

## 独立 C11 交叉审核

- 审核方：独立 AI 评审（非本次代码作者）。
- 审核方式：层间交叉（Boot 实现 ↔ portable OTA contract ↔ Recovery SCPI projection）+ 文档交叉
  （M3-03/M3-05/M3-06 退出门禁）。
- 审核结论：PASS_WITH_NOTE。
- 审核日期：2026-08-24。
- NOTE：本单关闭的是代码/host/build fault matrix 和 Recovery read-only projection；
  真实 DHRT100 破坏性注入、BCB 双 lane/power-cut 和空片 Recovery restore 仍由 M3-05/M3-06
  的独立物理 gate 管理，不以本单替代。

## 处置

- M3-03 的 slot-specific/vector/hash/signature/compatibility 子项可标记完成；
  M3-06 的真实 Boot fault HIL 与 M3 总体退出门禁保持未完成。
- Registry 中现有 Flash 契约不改变 status，不触发自审自批；本提交单作为 C11 证据归档。
