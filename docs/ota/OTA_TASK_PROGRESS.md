# OTA HAOFV 任务进度

Status: Active
Domain: OTA
Canonical: `docs/ota/OTA_TASK_PROGRESS.md`
Related: `docs/ota/OTA_HAOFV_ARCHITECTURE.md`, `docs/ota/OTA_TODO.md`, `docs/arch/HAOFV_FLASH_TASK_PROGRESS.md`
Last updated: 2026-09-06

本文只记录 OTA HAOFV 主线的代码、测试、构建、HIL、失败、回退和下一 gate。稳定语义见 Architecture，任务状态见 TODO。

## 文档接口

| 事实 | 位置 |
|---|---|
| 稳定 owner/ECC/Vector 契约 | `OTA_HAOFV_ARCHITECTURE.md` |
| 当前任务状态与退出门禁 | `OTA_TODO.md` |
| 原始运行证据 | `out/ota/`、P3 acceptance report |

## 当前 checkpoint

- 当前任务：`OTA-HAOFV-010`。
- 已确认：BCB CRC 深栈副本导致的 HardFault 已修复，COM7 Direct A/B 双向闭环通过。
- 下一 gate：完成当前源码指纹的 P3 receipt、真实 power-cut 边界和 C11 复核；不得复用旧 receipt。

### OTA-PROGRESS-20260906-010 - 当前切片 P3/TDMA 门禁阻塞

- 当前源码指纹 build：`20260906050112`，构建与 host OTA/BCB/wiring 回归通过；产物在
  `out/build/ota-haofv-current/`。
- `p3_hardware_acceptance.py run` 失败于 coarse CLK calibration 的 follower ARM，
  证据目录为 `out/hardware_acceptance/p3-20260906-130105/`；同一 build 的两次
  128-cycle TDMA SHORT 尝试（含一次软件复位后重试）均在
  `FB276192BEF9CCE1` ARM timeout，未达到 `ring_adapter_started=1`。
- 已按状态机约束执行 SD 诊断并保存 `sd-diagnostic/` 原始响应。SD 卡可用但无
  `/traces` capture 文件，无法完成波形解码修复；因此不生成当前切片 P3 receipt，
  不提交/推送代码，保留失败证据等待硬件现场恢复。

## 任务记录

### OTA-PROGRESS-20260906-001 - END reset 根因定位

- TODO：`OTA-HAOFV-002`。
- 现象：COM7 首轮 Slot A→B 成功，第二轮 Slot B→A 在完整接收后的 pending 阶段 USB 断开；重连为 `IDLE`，未达到 `READY_TO_REBOOT`。
- retained evidence：watchdog 报告 `CORE0_SUPERVISOR_STALL`；细化 BCB breadcrumb 后定位到 metadata load 的 lane/page read；exception capture 最终记录 HardFault。
- fault frame：stacked PC 反查到 `body_crc32()` 的 page-sized 栈分配，LR 位于 `body_valid()` 的 body CRC 校验调用点。
- 结论：watchdog 是 fault 后的回收机制，不是初始故障；BCB service delay/重复扫描不是本次剩余 HardFault 的直接根因。
- 失败证据：`out/ota/com7-fault-pc-20260906/ota2-slot-a/`。

### OTA-PROGRESS-20260906-002 - BCB CRC 固定栈修复与双向 HIL

- TODO：`OTA-HAOFV-002`。
- 变更：body/seal CRC 改为分段输入并在 CRC 字段位置注入零字，不再复制完整 page-sized 对象；保留 BCB page/validation 与 fault-frame retained diagnostics。
- 软件验证：portable stream/session/checkpoint、BootControlStore 和 portable OTA host runner 全部通过。
- 构建：`pico2-rtos-multicore-smoke` 的 App A、App B、Bootloader、factory UF2、unified package 和 link contract 通过。
- HIL：COM7 factory picotool load/verify 通过；Slot A→B 与 Slot B→A 均达到 `READY_TO_REBOOT → BOOT → COMMITTED`。
- 最终状态：active/confirmed Slot A，transaction 清零，错误队列为空，watchdog reset 为显式 software reboot。
- 成功证据：`out/ota/com7-crc-stack-fix-20260906/`。
- 回退：恢复原 copy-based CRC 会重新引入深栈风险，不作为可接受回退；功能回退应保留分段 CRC，仅移除临时 fault instrumentation。

### OTA-PROGRESS-20260906-003 - docs/ota 三件套收敛

- TODO：`OTA-HAOFV-001`。
- 变更：建立 OTA Architecture/TODO/Task Progress 三件套；旧 Direct A/B、copy transaction、portable migration、comparison 和 v1 system design 移入 `docs/legacy/ota/`。
- 契约影响：只细化现有 `ARCH-FLASHOWNER-01`、`ARCH-BOOTCTRL-01` 和 `ARCH-OTASTREAM-01` pending 语义，不改变 registry status，不触发自审激活。
- 验证：`docs_check.py --strict-names` 通过，doc regression freshness/registry/orphan 通过，文档 pytest `18 passed`；`docs/ota` 仅保留三份标准文件。
- 下一步：进入 `OTA-HAOFV-003` 显式 selection API。

### OTA-PROGRESS-20260906-004 - 显式 BCB selection 与 stale generation

- TODO：`OTA-HAOFV-003`。
- 变更：新增 `pota_bcb_selection_t`，selection 显式携带 newest、append plan、schema/map/lane geometry 和 store mutation generation；新增 `pota_bcb_txn_begin_from_selection()`。
- 删除：store 内 selection hint、一次性消费逻辑和 `select_newest(const store *)` 的 const 强转写入。
- 生命周期：transaction begin 后推进 store mutation generation，旧 selection 再使用返回 `POTA_BCB_RESULT_BUSY`。
- 单测：验证 from-selection begin 不新增 Flash read，并拒绝 stale selection；portable OTA/BCB runner 全绿。
- 构建/HIL：App A、App B、Bootloader 和 link contract 通过；COM7 factory verify、A→B、B→A 均通过。
- 证据：`out/ota/com7-explicit-selection-20260906/`。
- 下一步：`OTA-HAOFV-004` 分步 scan。

### OTA-PROGRESS-20260906-005 - HAOFV bounded scan/transaction 与 Vector snapshot

- TODO：`OTA-HAOFV-004`、`OTA-HAOFV-005`、`OTA-HAOFV-006`、`OTA-HAOFV-007`。
- 变更：新增固定 workspace `pota_bcb_scan_begin/step/result`；`MARK_PENDING` 每个 service tick 最多推进一个 BCB page 或一个 transaction step；App BCB 写入使用 FlashTransactionAO async step owner。
- Vector：OtaAO 发布双缓冲 `OtaVector` 和 metadata snapshot，SCPI 读取 snapshot，不再现场调用 metadata/BCB Flash scan；公开 vector reader 不自旋等待业务 action。
- 软件验证：portable OTA/BCB runner 全部通过；App A、App B、Bootloader、link contract 构建通过。
- HIL：COM7 factory load/verify 通过；A→B 和 B→A 均 `READY_TO_REBOOT → BOOT → COMMITTED`。
- 失败与修正：首版 seqlock reader 在 writer 覆盖整个 FB action 时导致 `TASK_STALL`；改为 inactive-buffer 写入后原子切换 active index，随后 HIL 通过。
- 证据：`out/ota/com7-vector-double-buffer-v2-20260906/`。

### OTA-PROGRESS-20260906-007 - 临时 fault trace 清理与最终 A/B 回归

- TODO：`OTA-HAOFV-008`、`OTA-HAOFV-009`、`OTA-HAOFV-010`。
- 清理：移除 HardFault/RTOS fatal 专用 OTA phase、BCB page trace callback 和 retained fault-frame 写入；Diagnostics/watchdog 保留稳定健康事实，不再把 fault 解释塞进 OTA Vector。
- snapshot：将 metadata reader 命名为 `ota_ao_get_metadata_snapshot()`，SCPI 不再暗示现场 Flash load；新增静态 wiring tests。
- 回归：portable OTA/BCB tests、OTA wiring/send Python tests、App A/B/Boot build/link contract 通过。
- HIL：最终源码 COM7 factory verify、Slot A→B、Slot B→A 均 `COMMITTED`。
- P3：`check-staged` 当前无 staged code change，因此只完成无 staged 变更检查；正式 receipt 需在 staging 后执行。
- 证据：`out/ota/com7-final-ota-20260906/`。
- 未闭合：公共同步兼容 API、stack-usage 报告、CRC 等价 golden、power-cut/负向矩阵和 C11。
- 下一步：`OTA-HAOFV-008` 清理已完成；继续核对兼容 wrapper 与最终 P3 边界。

### OTA-PROGRESS-20260906-006 - Vector 双缓冲修正与 snapshot HIL

- TODO：`OTA-HAOFV-004`、`OTA-HAOFV-007`。
- 失败：首版 seqlock reader 在 writer 业务 action 期间持续自旋，COM7 首个 DATA 触发 `TASK_STALL`/host Write timeout。
- 修正：改为 OtaAO 双缓冲；writer 写 inactive buffer 后原子切换 active index，reader 单次读取 active snapshot，不等待业务 action。
- 软件/构建：portable runner、App A/B、Bootloader、link contract 通过。
- HIL：COM7 A→B、B→A 均 `READY_TO_REBOOT → BOOT → COMMITTED`。
- 证据：`out/ota/com7-vector-double-buffer-v2-20260906/`。
- 结论：snapshot-only 查询边界通过；原失败保留，不覆盖。

### OTA-PROGRESS-20260906-009 - 008/009/010 最终收口证据

- TODO：`OTA-HAOFV-008`、`OTA-HAOFV-009`、`OTA-HAOFV-010`。
- 008：临时 HardFault/RTOS fatal OTA phase、BCB trace callback 和 retained fault-frame 已移除；主 App pending 仅走固定 scan/async owner 路径。
- 009：`ota_haofv_gate.py` 通过；当前 ELF 中 `pota_bcb_scan_step=8`、`pota_bcb_txn_begin_from_selection=0`、`ota_metadata_mark_pending_step=0`、`body_crc32=0` 字节额外栈帧；portable BCB、FlashTransaction、Journal reset boundary 和 OTA wiring tests 全部通过。
- 010：重新刷入当前 factory UF2 后，COM7 完整 positive/negative OTA validation 全部 PASS；NO1–NO4 使用同一最终 UF2 完成 load/verify/re-enumeration，四板均为目标 build、`IDLE`、无 watchdog/SCPI error。
- 证据：`out/ota/com7-ota-final-20260906/`、`out/ota/com7-ota-final-20260906/ota_haofv_gate.json`、`out/ota/no1-no4-uf2-final-20260906/`。
- P3 边界：`p3_hardware_acceptance.py check-staged` 报告当前无 staged code change；仓库现有 P3 receipt 的 source/build 指纹不匹配当前切片，未伪造新 receipt。
- 未闭合：真实可控 power-cut、当前切片正式 P3 receipt 和独立 C11 审核。以上三项不允许用 host 或旧 receipt 替代。

## 验证与证据索引

| Progress ID | 软件/构建 | 板端/原始证据 | 结果 |
|---|---|---|---|
| `OTA-PROGRESS-20260906-001` | fault instrumentation build | `out/ota/com7-fault-pc-20260906/` | 复现并定位 HardFault |
| `OTA-PROGRESS-20260906-002` | portable OTA runner + RTOS multicore build | `out/ota/com7-crc-stack-fix-20260906/` | PASS |
| `OTA-PROGRESS-20260906-003` | docs check、doc regression、文档 pytest | 本文件 | PASS |
| `OTA-PROGRESS-20260906-004` | portable OTA runner + RTOS multicore build | `out/ota/com7-explicit-selection-20260906/` | PASS |
| `OTA-PROGRESS-20260906-005` | portable runner + RTOS multicore build + bounded scan/intent/vector HIL | `out/ota/com7-vector-double-buffer-v2-20260906/` | PASS |
| `OTA-PROGRESS-20260906-006` | vector double-buffer build + COM7 A/B HIL | `out/ota/com7-vector-double-buffer-v2-20260906/` | PASS |
| `OTA-PROGRESS-20260906-009` | OTA HAOFV gate + full COM7 validation + NO1–NO4 final UF2 | `out/ota/com7-ota-final-20260906/`, `out/ota/no1-no4-uf2-final-20260906/` | PASS_WITH_REMAINING_GATES |
| `OTA-PROGRESS-20260906-007` | final build + wiring tests + COM7 A/B + P3 no-staged check | `out/ota/com7-final-ota-20260906/` | PASS_WITH_REMAINING_GATES |

## 失败与回退

- 所有失败 run 保留原始 `timing.json` 和 `serial_trace.jsonl`，不得被最终 PASS 覆盖。
- `READY_TO_REBOOT` 前的 transport reset 一律判失败，不自动发送 BOOT/COMM。
- 文档迁移可通过 git 恢复旧路径；历史内容已保存在 `docs/legacy/ota/`。

## 下一 gate

完成 `OTA-HAOFV-010`：真实 power-cut、当前源码指纹 P3 receipt 和 C11 独立审核全部闭合后，才将 OTA 项目标记为完成。
