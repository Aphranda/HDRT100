# OTA HAOFV 实施待办

Status: Active
Domain: OTA
Canonical: `docs/ota/OTA_TODO.md`
Related: `docs/ota/OTA_HAOFV_ARCHITECTURE.md`, `docs/ota/OTA_TASK_PROGRESS.md`, `docs/arch/HAOFV_FLASH_TODO.md`
Last updated: 2026-09-06

本文维护 OTA 域当前主线、稳定任务 ID、状态和退出门禁。历史完成清单与单次验证快照已迁入 `docs/legacy/ota/`；实现证据只追加到 `OTA_TASK_PROGRESS.md`。

## 文档接口

| 文件 | 唯一职责 |
|---|---|
| `OTA_HAOFV_ARCHITECTURE.md` | owner、ECC、Vector、Boot Control 和失败恢复稳定语义。 |
| `OTA_TODO.md` | 任务状态、依赖、退出门禁和阻塞项。 |
| `OTA_TASK_PROGRESS.md` | 提交、测试、构建、HIL、失败和回退证据。 |

跨域 Flash v2、Recovery、signature、TDMA stream 和 durable journal 总依赖仍由 `HAOFV_FLASH_TODO.md` 跟踪。本文件只维护 OTA 域内落地，不重复宣布跨域里程碑完成。

## 状态规则

状态只使用：

- `DONE`：代码、相关软件测试、构建和要求的硬件证据闭合。
- `IN PROGRESS`：当前唯一主线，已有可复核进展。
- `PENDING`：前置条件未完成或尚未开始。
- `BLOCKED`：存在明确外部阻塞，且进度文件记录了失败证据和恢复条件。

## 已有基线

| 基线 | 当前事实源 |
|---|---|
| Direct A/B、test/confirm/revert 和统一 package | `HAOFV_FLASH_ARCHITECTURE.md`、BootControlStore/portable OTA 代码及历史 HIL |
| App Flash 唯一 writer | `FlashTransactionAO`/`FlashTransactionFB` 和 `ARCH-FLASHOWNER-01` pending 契约 |
| BCB 双 lane append/commit/GC | `BootControlStore` 和 `ARCH-BOOTCTRL-01` pending 契约 |
| Stream ingress/session | `OtaStreamSession` 和 `ARCH-OTASTREAM-01` pending 契约 |
| 当前目标板验证 | `OTA_TASK_PROGRESS.md` 与 `out/ota/` 原始证据 |

## 当前主线

将 OTA pending 路径收敛为符合 HAOFV 的单 owner、非阻塞 FB、显式 selection、固定 workspace、Flash intent/completion 和 Vector snapshot 流程。公共 SCPI 状态保持兼容，内部逐步移除同步扫描、hidden hint、现场查询和临时 fault 语义混放。

## 里程碑总览

| 阶段 | 目标 | 退出门禁 |
|---|---|---|
| P0 根因修复 | 消除 BCB CRC 深栈 HardFault，保留可复核故障证据。 | portable tests、App/Boot build、COM7 A/B 双向 OTA。 |
| P1 selection 收敛 | 一次 pending 只生成并消费一个显式 selection。 | 无 hidden hint、无 const mutation、host fault matrix 通过。 |
| P2 非阻塞 scan | BCB scan 每 tick 推进一个有界步骤，执行预算生效。 | OtaFB 返回 BUSY/DONE/FAILED，满 lane 仍不阻塞 AO。 |
| P3 owner/Vector 收敛 | BCB append 由 FlashTransactionAO typed intent 执行；SCPI 只读 Vector。 | raw writer/link gate、seqlock snapshot、无查询时 Flash IO。 |
| P4 发布验收 | 完整 fault/power-cut/stack/HIL/P3 证据。 | 当前源码指纹的 acceptance receipt 和 C11 审核。 |

## 分阶段任务表

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| OTA-HAOFV-001 | 将 `docs/ota` 收敛为 Architecture/TODO/Task Progress 三件套，旧文件迁入 legacy，修复全库引用。 | DONE | `docs/ota` 仅三文件；严格命名、索引、链接、registry 和 pytest 全绿，pre-commit 在最终代码切片统一执行。 |
| OTA-HAOFV-002 | 修复 BCB body/seal CRC 的 page-sized 深栈副本并建立 fault-frame 证据。 | DONE | PC/LR 定位、分段 CRC、portable tests、RTOS multicore build、COM7 A→B→A PASS。 |
| OTA-HAOFV-003 | 用显式 `pota_bcb_selection_t` 和 from-selection transaction API 替换 selection hint。 | DONE | 删除 hidden hint/const mutation；selection generation/lifecycle 明确；正反单测与 COM7 A→B→A 通过。 |
| OTA-HAOFV-004 | 实现 `pota_bcb_scan_begin/step/result` 固定 workspace。 | DONE | 每 step 最多处理一个 page；full lane、torn/CRC/schema/map、no-valid 和 stale-generation 测试通过。 |
| OTA-HAOFV-005 | 将 `MARK_PENDING` 接入 OtaFB 内部 ECC 子状态并真正执行 `budget_us`。 | DONE | scan/prepare/transaction 每 tick 有界；AO service 每次只推进一个动作；COM7 A/B 通过。 |
| OTA-HAOFV-006 | 增加 versioned Boot Control append intent，由 FlashTransactionAO 内部推进 transaction。 | DONE | App BCB program/erase 使用 submit + service + committed Vector；Boot 保持同步 adapter；COM7 A/B 通过。 |
| OTA-HAOFV-007 | 扩展 OtaVector/DiagnosticsVector 并将 SCPI 查询迁为 snapshot-only。 | DONE | OtaAO 双缓冲 Vector/metadata snapshot；SCPI 查询不触发 BCB/Flash IO；COM7 A/B 通过。 |
| OTA-HAOFV-008 | 移除旧同步 App scan、selection service hook 和完成定位后的临时 fault instrumentation。 | DONE | 主 App pending 路径仅走 HAOFV step/async owner；临时 fault/page trace 已移除；portable/RTOS build 与 COM7 A/B 通过。非主线历史同步 wrapper 保留在兼容边界并单独审计。 |
| OTA-HAOFV-009 | 建立 stack-usage、CRC 等价、BCB 满 lane/GC/torn 和查询无 IO 回归门禁。 | DONE | OTA HAOFV static gate、固定 workspace 栈帧 gate、portable BCB/FlashTransaction/Journal reset matrix、CRC/BCB tests、query wiring 全部通过。 |
| OTA-HAOFV-010 | 执行 COM7 多轮 A/B、负向矩阵、power-cut 边界和仓库 P3 hardware acceptance。 | IN PROGRESS | 当前源码构建、COM7 positive/negative matrix、NO1–NO4 UF2 和 P3 staged 检查通过；真实 power-cut、正式 P3 receipt/C11 尚未闭合。 |

## 当前阻塞项

`OTA-HAOFV-010` 剩余真实 power-cut 控制、当前源码指纹的正式 P3 acceptance receipt 和 C11 独立审核；已有 host/reset matrix、COM7 negative HIL 与 NO1–NO4 UF2 证据不能替代这些门禁。

## 统一完成定义

- Architecture、TODO、Task Progress 事实边界无复制冲突。
- OtaAO 是 session/ECC/Vector 唯一运行 owner。
- OtaFB action 立即返回，所有 scan/write/wait 分步推进。
- App erase/program 仅由 FlashTransactionAO 执行，Boot 使用 BootFlashService。
- 一次 pending 只有一个显式 selection，没有 hidden hint 或 const mutation。
- CRC/scan/transaction 使用固定 workspace并满足栈门禁。
- SCPI/UI/Diagnostics 只读取版本化 Vector，不现场扫描 Flash。
- A→B、B→A、回滚、torn/power-cut、BCB 损坏和 fault matrix 有确定结果。
- 文档门禁、软件测试、构建、P3 硬件验收和 C11 审核全部闭合。
