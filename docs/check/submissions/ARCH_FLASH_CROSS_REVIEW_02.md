# 核验提交单：Flash canonical 重构 → HAOFV 顶层

Status: Active
Domain: HAOFV / Flash / Documentation Governance
Canonical: `docs/check/submissions/ARCH_FLASH_CROSS_REVIEW_02.md`
Related: `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`, `docs/arch/HAOFV_FLASH_TODO.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-08-21

## 提交内容

本次只重构文档信息架构和待办组织，不改变 FlashMap v2 快照、owner、Boot/OTA、RefMem/VDC、
PIO catalog 或迁移策略。canonical 改为“当前/目标 -> 契约 -> owner -> map -> data -> Boot -> OTA
-> validation/migration”；TODO 改为 M0-M6 依赖工作板。

## 契约语义迁移核验

| contract_id | 重构后落点 | 语义核验 | registry 状态 |
|---|---|---|---|
| `ARCH-FLASHMAP-01` | 架构第四章、TODO M0/M1 | 唯一 map、目标布局和 Boot/linker/App/factory/tool 同源规则保留。 | pending |
| `ARCH-FLASHOWNER-01` | 架构第三章、TODO M1 | App core0 唯一 writer、Boot 最小服务、park ACK 和 durable completion 保留。 | pending |
| `ARCH-BOOTCTRL-01` | 架构第六章、TODO M3 | BCB 双 lane、Direct A/B test-confirm-revert、Recovery 和掉电规则保留。 | pending |
| `ARCH-OTASTREAM-01` | 架构第七章、TODO M4/M5 | transport-neutral session、durable ACK、credit、resume 和 target 不缓存完整包保留。 | pending |
| `REFMEM-PERSIST-01` | 架构 5.3、TODO M2-05 | 只持久化 deployment input，上电新 epoch、peer 初始 stale 保留。 | pending |
| `VDC-PERSIST-01` | 架构 5.4、TODO M2-04 | 只持久化低频 profile，上电从 OFF/CHECKING 重锁且不恢复 offset/rate 保留。 | pending |
| `ARCH-PIOCAT-01` | 架构 5.5、TODO M2-07 | PIO 随签名 App，System Pack 只选 ID，Blob 拒绝 executable object 保留。 | pending |

## 当前事实交叉核验

| 层 | 证据 | 重构后的表述 |
|---|---|---|
| Hardware/build | 产品器件与 `CMakeLists.txt::PICO_FLASH_SIZE_BYTES` | 物理/构建声明 16 MiB。 |
| Driver | `drv_flash.h::DRV_FLASH_TOTAL_SIZE_BYTES` | App raw HAL 仍限制 4 MiB。 |
| Partition | `ota_partition.h::OTA_COMPAT_LAYOUT_SIZE` | 只分配下 4 MiB 兼容布局，上 12 MiB 未分配。 |
| Boot | CMake default/release check/Bootloader | Direct A/B 是发布默认，copy-to-active capability 仍存在。 |
| TDMA | `tdma_profile.h`、traffic scheduler | reliable bulk/maintenance gate 已有，OTA session/wire 尚无。 |
| RefMem/VDC/PIO | 对应 runtime 组件 | runtime 基础存在，生产持久化/catalog 尚无。 |

## TODO 结构核验

| 检查项 | 结果 |
|---|---|
| 依赖顺序 | M0 -> M1 -> M2/M3 -> M4 -> M5 -> M6；不允许绕过本地 OTA 直接做多板。 |
| 单一任务落点 | Store、Boot、stream、TDMA、wear 各自只有一个主工作包；域表和证据表只引用。 |
| 当前状态 | 已有基础与 v2 完成状态分开；没有把文档/现有 runtime 标成 v2 实现完成。 |
| 退出门禁 | 每个里程碑均有 host/build/HIL/rollback 证据要求。 |
| 回退 | v1 artifact、BOOTSEL、previous confirmed、Recovery、durable checkpoint 分层定义。 |

## 偏差声明

- 7 条目标契约继续保持 `pending`；结构重构不是实现证据，不触发 status 变化。
- v2 目标分区仍是 snapshot。生成式 `FLASH_MAP_*` 落地后，代码符号才成为数值事实源。
- COM8、两板、四板和 destructive/wear HIL 是后续里程碑任务，本次没有烧写或改变板上状态。

## Alternatives considered

- 保留 F0-F7 技术模块清单，只调整标题（拒绝：重复任务和跨阶段依赖仍不清晰）。
- 把 architecture 与 TODO 合成一个文件（拒绝：稳定契约与频繁状态更新生命周期不同）。
- 按 HAOFV 主域拆成多个 Flash TODO（拒绝：会重新产生私有 owner、分区和重复 gate）。
- 保持单一 canonical，TODO 使用 M0-M6 依赖工作板和引用索引（接受）。

## 核验结论

- 结论: ACCEPT_WITH_DEVIATION
- 核验人: HAOFV architecture owner

## 交叉审核记录（C11，必填）

- 审核方: `DOCS_REGISTRY` 契约、当前 driver/partition/Boot/TDMA 代码与 RefMem/VDC 域 canonical
- 审核方式: 文档交叉 + 层间交叉
- 审核结论: PASS_WITH_NOTE（结构与任务依赖更清晰；实现偏差和 pending 状态保持不变）
- 审核日期: 2026-08-21
