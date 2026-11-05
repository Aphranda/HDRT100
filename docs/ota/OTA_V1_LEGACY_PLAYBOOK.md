# OTA V1 Legacy Playbook（冻结 / 恢复基线）

Status: Frozen
Domain: OTA
Canonical: `docs/ota/OTA_V1_LEGACY_PLAYBOOK.md`
Related: `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`, `docs/arch/HAOFV_FLASH_TODO.md`, `docs/ota/OTA_TODO.md`
Last updated: 2026-11-05

## 0. 决定

自 2026-11-05 起，V1 compat **功能参考已归档**：

```text
V1 = ARCHIVED-REFERENCE + RECOVERY-BASELINE
V2 = 功能主线 / 唯一有效的校准与引导档目标
```

V1 仍保留为现场可启动、可回退的 factory 基线，但不再接受新的功能验证结论。

历史验证记录（factory migration、四板 P3 FULL、A/B/revert/Recovery）按用户 2026-11-05
裁定视为已完成；真实掉电矩阵因台架能力限制**不在本轮退役门禁内**，也不得写成“已通过”。

## 1. 为什么冻结 V1

V1 compat 没有独立的 `CALIBRATION_NVS` 分区；V1 的校准/引导档存储只能落在兼容子区或返回
`UNSUPPORTED`。V2 才定义独立 `CALIBRATION_NVS` 和引导档子区。

因此：

- V1 上的校准/引导档结果不能证明 V2 功能；
- V1 的 `UNSUPPORTED` 不是 V2 缺陷；
- V1 的 P3/OTA 结果只证明 V1 兼容路径；
- 任何新校准、引导档、V2 资源布局验证必须在 V2 上做。

分区事实以 `config/flash_map_v1_compat.json` 和 `config/flash_map_v2.json` 为准；本文不复制 offset/size。

## 2. V1 允许 / 禁止的用途

| 用途 | 允许 | 说明 |
|---|---|---|
| 现场回退到已知可启动版本 | ✅ | 使用 V1 factory artifact + checksum + BOOTSEL runbook |
| 验证 V2 迁移失败后的可恢复性 | ✅ | 仅作为 rollback drill 的目标 |
| 验证 V1 兼容路径回归 | ✅ | 只记录“V1 兼容”结论，不外推 V2 |
| 校准/引导档功能验证 | ❌ | V1 无独立 `CALIBRATION_NVS`，结果无效 |
| V2 P3/资源布局/签名链结论 | ❌ | 必须用 V2 candidate + 同指纹凭证 |
| 新功能开发/新默认构建 | ❌ | 新默认构建不得再以 V1 为目标 |
| 删除 V1 factory/runbook | ❌ | 退役前必须保留可审计回退路径 |

## 3. 必须保留的 V1 资产

- V1 factory UF2 及其 SHA256；
- `config/flash_migration_policy.json` 中的 `last_recoverable_commit` / V1 map；
- V1 BOOTSEL full erase/reflash runbook；
- V1 兼容 map 与 factory 构建入口；
- 已归档的 V1 HIL/失败证据索引。

这些资产只允许**归档/只读**，不得删除或覆盖。

## 4. V1 退役状态

V1 的**功能参考**已归档。V1 的**运行/构建入口**仍保留为恢复基线，退役按下表：

| 项 | 状态 | 说明 |
|---|---|---|
| 四板 factory 迁移到 V2 | ✅ 已完成（历史记录） | 按用户 2026-11-05 裁定 |
| 四板 P3 FULL | ✅ 已完成（历史记录） | 同指纹凭证见历史 run |
| V2 A/B/revert/Recovery | ✅ 已完成（历史记录） | 不重开旧验证 |
| 真实掉电矩阵 | — 不适用 | 台架无法做；用户明确从门禁移除，不得写成已通过 |
| V2 `CALIBRATION_NVS` 引导档 store 板端闭环 | 以 V2 当前任务为准 | V1 无此分区，不能替代 |
| live consumer 切 V2、无工具依赖 V1 offset | ✅ 按 V1 归档处理 | V2 为正常构建路径；遗留 V1 offset 只允许 recovery 路径 |
| 产品接受“V2 失败后不再回 V1” | — 不等 | 用户 2026-11-05 明确取消该门禁 |
| V1 代码/构建入口 | ✅ 已归档为 legacy/reference | 源文件保留，不删除；不再作为正常开发入口 |
| C11 / Registry 状态 | 以登记表为准 | 属于 V2 激活门禁，不是 V1 归档门禁 |

在这些条件完成前：

```text
V1 功能参考：已归档，不再用于校准/引导档结论；
V1 运行/factory 基线：保留，不得删除；
V1 代码/构建入口：已从正常开发归档为 legacy/reference；
                 源文件保留，仅用于 recovery/历史审计。
```

历史证据入口（不重跑）：

- `out/HardwareAcceptance/20261105/p3-091603-resume`（step36 OK）
- `out/HardwareAcceptance/20261105/p3-082941-resume`（step36 OK）
- `out/HardwareAcceptance/20261104/hawf-162803/acceptance.json`（build 20261104074335，passed）
- `out/HardwareAcceptance/20261104/hawf-081955/acceptance.json`（build 20261103195548，passed）
- `docs/arch/HAOFV_FLASH_TASK_PROGRESS.md` 中的 factory/A-B/revert/Recovery 记录

## 5. 新会话操作规则

1. 先读 `SYST:OTA:STREAM:CAP?`：最后一位是 map version；当前现场若仍为 V1，不要做 V2 校准结论。
2. V1 板上只做兼容/回退验证，不做引导档/`CALIBRATION_NVS` 验证。
3. 需要验证 V2 功能时，先完成 V2 factory migration 和四板同 build 检查。
4. 不把 V1 P3/校准结果写进 V2 验收报告。
5. 不删除 V1 factory/checksum/runbook；需要归档时按 `docs/legacy/` 规则移动并保留索引。

## 6. 当前快照（2026-11-05，非事实源）

- V1 功能参考已归档；
- 历史 factory migration、四板 P3 FULL、A/B/revert/Recovery 记录已完成；
- 真实掉电矩阵不适用，已从门禁移除；
- V1 runtime/factory baseline 继续保留；
- V1 代码/构建入口已归档为 legacy/reference，不再作为正常开发入口；源文件未删除。
- 默认构建已切到 V2；普通 preset 不再选择 `v1_compat`。
- V1 只允许显式审计恢复构建：`PROJECT_V1_RECOVERY_BUILD=ON`；该开关不得进入默认/普通 preset。

该快照只用于交接；实际状态以板端 `SYST:FW:BUILD?` / `SYST:OTA:STREAM:CAP?` 和
`docs/arch/HAOFV_FLASH_TODO.md` 为准。

## 7. 归档记录

归档日期：2026-11-05。

归档范围：

- V1 功能参考与正常开发入口；
- V1 代码/构建入口标记为 legacy/reference；
- V2 成为功能主线。

保留、不删除：

- V1 factory UF2、SHA256、`last_recoverable_commit`；
- V1 BOOTSEL full erase/reflash runbook；
- V1 兼容 map 与 factory 构建入口（只读/恢复用途）；
- V1 HIL/失败证据与 `docs/legacy/ota/` 历史资料。

依据：

- 用户 2026-11-05 裁定：factory migration、四板 P3 FULL、A/B/revert/Recovery 已有历史完成记录；
- 真实掉电矩阵不可执行，已从门禁移除；
- 用户 2026-11-05 明确“不用等产品回退决策”。

边界：

```text
V1 归档 ≠ 删除 V1 factory/recovery 资产；
V1 归档 ≠ 用 V1 结果证明 V2；
V1 归档后不再接受新的 V1 功能验证结论。
```
