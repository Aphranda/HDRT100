# Distributed RefMem 内部主域任务进度

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_TASK_PROGRESS.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`
Last updated: 2026-10-26

本文档记录 Distributed Vector Blackboard / RefMem Sync Domain 的阶段性任务进度、验证结果和后续动作。待办事项放在 `REFMEM_DOMAIN_TODO.md`，本文只记录已经发生的工作和可回溯结果。

## 当前 checkpoint

- 最新条目：`REFMEM-TASK-20261026-003`。
- 统一控制面：命令回复回程已打通；远程配置 stage 已 ACK；**目标侧配置授权（方案 A）已落地**，非协调者远程配置 12；租约、idle 窗口/事务已实现；非协调者入口在 N5.5。
- N3.2：N3.2.1–5、N3.2.8 完成；N3.2.4 补 per-target ACK；N3.2.6 延后；N3.2.7 部分（待补乱序/窗口 miss/ring stop→start/mid-ring/长期 soak）。
- N5.0：N5.0.2a config-txn 拆出（`distributed_refmem.c` 6,370→6,315）；P3 `20261024144807`；config txn HIL 14/14。
- N5.0.2b：cmd-reply 拆出（6,315→6,109）；P3 `20261024151357`；exec196 HIL 三目标 `frag_complete=[1,1,1]`。
- N5.0.2c：node control 拆出（6,109→6,003）；P3 `20261024154054`；config txn HIL 14/14。
- N5.0.2d：cmd-frame 拆出（6,003→5,946）；P3 `20261024160525`；exec196 HIL 三目标 `frag_complete=[1,1,1]`。
- N5.0.2e：dispatch/command 拆出（5,946→5,763）；P3 `20261024163157`；config txn HIL 14/14。
- 下一步：**N5.5.0b 意图运输决策**（先验 TLV/参数池同步与 intent 容量），再决定 N5.5.1 冻结或转 N5.2c/N5.3；细化里程碑见 TODO「N5.5 → N5.2c → N5.3 实施目标」。

## 归档索引

| 文件 | 条目范围 | 条目数 | 归档日期 |
|---|---|---|---|
| `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_01.md` | REFMEM-TASK-20260813-021..REFMEM-TASK-20260813-001 | 21 | 2026-10-17 |
| `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_02.md` | REFMEM-TASK-20260814-039..REFMEM-TASK-20260813-022 | 20 | 2026-10-17 |
| `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_03.md` | REFMEM-TASK-20260814-049..REFMEM-TASK-20260814-040 | 10 | 2026-10-24 |
| `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_04.md` | REFMEM-TASK-20260815-006..REFMEM-TASK-20260814-050 | 10 | 2026-10-25 |

### REFMEM-TASK-20261026-003 — N5.5.0 spike：D2 否定（远端意图不同步）

- 状态：spike 完成，结论改变 N5.5 设计前提；**未写生产代码**。
- D1 ✅：`refmem_sync_frame_header_t.source_slot` 已带发送者，`refmem_cmd_frame_apply()` 已取出（接收侧丢弃）；发送者授权无需改 wire，origin 仍用 `source_node` 回程。
- D2 ❌：默认模型没有覆盖 `REFTMEM_VECTOR_REGION_NODE` 命令块的 data link，sync 不搬该块；四板实测（build `20261026134613`）在 process-image（`--no-t2`）与 T2-seeded 环上，COM5 读 slot 2 始终 `control_state=0/last_update_ms=0`，COM3 本板为 `control_state=1 POSTED`。证据 `out/n55_spike/{d2_report_v2,d2b_report}.json`、`REPORT.md`。
- D3 ❌：无硬件伪造非 owner sender 的注入路径（`SYNC:RX` 只进 sync 解析；`LONGFRAME:TX` 的 stream id 不是 REFMEM_CMD；非协调者无上行）。
- 影响：N5.5 需先定意图运输（N5.5.0b）：(1) 紧凑 intent delta + TLV 参数引用；(2) 专用 payload class；(3) 产品级入口收敛。未定前不冻结、不写代码；b.5 硬件负向继续 host-only。
- 下一步：N5.5.0b（先验 TLV/参数池是否同步、intent 容量预算），再决定 N5.5.1 冻结或转 N5.2c/N5.3。

- 状态：b.1–b.4 完成、b.5 硬件负向受阻、b.6 收口；N5.2b 整体未标完成。
- 内容：`refmem_slot_claim_lowest_claimed` / `refmem_application_model_control_owner_slot` / `distributed_refmem_control_owner_slot`（派生值，不落状态）；dispatch 对 config 类先判 `origin ∈ {本地, owner}` 否则 12，再判 idle 窗口（7）；`admit_origin_trial` 与 `slot_claim_gate_ready` 要求 owner 可派生且落在 ring 内。
- host：新增 `test_refmem_command_authority`（6 情景，禁用检查的负测 4 处断言红）；`test_refmem_slot_claim` 增 `lowest_claimed`；`test_refmem_control_route` 增 origin 决策；`test_refmem_table_registry` 增 model owner；四套全绿。
- HIL（build `20261026134613`，四板，按 `command_seq` 对齐）：协调者 COM5→COM4 `state=4 ACKED/reason=0`；COM4 本地入口 ACK；COMMIT 后协调者 `state=5/reason=7`；非协调者 COM3 命令停在源板 POSTED、目标记录不前进（从板无上行注入点，N5.5 范围）。证据 `out/n52b_hil/report_v2.json`。
- P3：final `build 20261026134613`、`PASS_WITH_WARNINGS`（INFO 33 / WARN 11 / ERROR 0 / FATAL 0）、`source 708faa78…`、1590 files；首跑 `build 20261026132128` 曾出现 13 WARN（COM3 `refmem_overrun_count_grew` / `refmem_max_runtime_exceeded_wcet`），重跑未复现（`out/n52b/p3_run1.log` 仅存计数），记为台架抖动残余。
- 下一刀：N5.2c（STOP→stage→START）或 N5.3（显式 owner）；N5.5 补硬件负向。

### REFMEM-TASK-20261026-001 — N5.2a.3 根因更正与修复：seat 不进包，远程配置首次 ACK

- 状态：完成（代码 + host 用例 + 设备 build + P3 + HIL）；N5.2 整体仍未完成（权限、租约、聚合仍在 N5.2b–N5.7）。
- 根因（更正 N5.2a“环 start 污染 table registry”的结论）：`SYSTem:TDMA:RING:TOPology` 把本板 seat 写入 app-model live profile，打包序列化的也是 live profile，而 node_load 的 TDMA owner 固定在 node 0；于是非 0 槽板把自己 seat 打进包，table 9 owner 校验失败（`0x0908`）。该状态可逆，与 ring start/stop 本身无关。
- 单板对照（COM4、环停止）：修复前 build `20261025213526` —— slot 0 → `staging_state=2`/ACK；slot 1 → `staging_state=3`、`first_lint_error=2312`、reason 13；回 slot 0 → 再次 VALIDATED（`out/refmem_n52a3_slot_diag/run_slot_diag_prefix_20261025213526.log`）。修复后 build `20261026123411` 同一命令 → slot 1 `staging_state=2`、ACK、`first_lint_error=0`（`out/refmem_n52a3_slot_diag/run_slot_diag.log`）。
- 修复：新增设计期基线 `s_design_tdma_foundation_profile`（`refmem_application_model_init` 与 package 激活时刷新）；打包与 `tdma_foundation_profile_table_crc32` 改用设计基线；live/active profile 仍是唯一运行期 seat，`set_tdma_ring_topology`/ring apply 路径未改。host 用例 `test_stage_ignores_runtime_ring_seat`；负测（打包改回 live profile）三处断言变红。
- 远程 HIL（四板，build `20261026123411`）：COM4 `CONFig:BEGIN` → COM5 `CTRL:EXECute` 排队 → 环启动 → COM4 `state=4 ACKED/reason=0`、`staging_state=2`、package CRC `3301311711`；源板聚合 `state=4`，`CMD:REPLy?`=`4,1,0,0,0,0,0,0,17601,4,2,0,1,4,0`；STOP 后 COMMIT、再次下发同命令 → `state=5/reason=7`（窗口门禁仍生效）。证据 `out/refmem_n52a3_hil/report.json`。**这是首个远程配置命令 ACK**；延迟事务（RUN 期排队、STOP 执行）未实现，本轮证明“窗口开启 + 环运行”即可 ACK，deferral 是否仍需要按 RUN 期策略再评估。
- 验证：`tools/tests/run_refmem_table_registry_tests.ps1`、`run_refmem_unified_control_host_tests.ps1` 全绿；refmem host 子集 212 passed；设备三 profile `flash_link_contract=OK`（build `20261026123411`）；P3 `FOUR_NODE_DPLL_QUICK_DIAGNOSTIC`、`PASS_WITH_WARNINGS`（INFO 33/WARN 11/ERROR 0/FATAL 0）、source `343f61bbe7b788c10871db24166d455224c88f9cf28628524d27b98ff018aec8`、1589 files、receipt `config/hardware_acceptance/p3_acceptance_receipt.json`。
- 下一刀：N5.2b 远程 owner/permission 合法/未授权矩阵；N5.2c STOP→stage→START（含 BEGIN/ABORT 分支）；随后 N5.3 `control_owner_slot` 冻结。

### REFMEM-TASK-20261025-021 — N5.0.5 预算/残差/HAOFV 收口

- 状态：收口报告完成；**N5.0 未完成**（不得标 ✅）。
- 行数快照（2026-10-25，非事实源）：
  - `distributed_refmem.c` 5,372（>3,000，A.3/A.4 未完成）
  - `scpi_system_snapshot_commands.c` 4,627（>3,000，N5.0.3b 未完成）
  - `scpi_refmem_snapshot_commands.c` 1,150；`distributed_refmem_position.c` 188；`distributed_refmem_diag.c` 128（新文件在 1,300 预算内）
  - `vdc_dpll_manager.c` 4,826、`vdc_domain.c` 4,460、`calibration_manager.c` 4,138、`tdma_pio_spi_ring_adapter.c` 4,116（N5.0.4 可选未做）
- 完成：N5.0.1（EXE-CODE-01）、N5.0.3a（SCPI refmem）、A.1（flight 状态头）、A.2a（position 状态 wrapper）、A.2b（stage/get/transport 迁出）、N5.0.2g（flight-sync diag getters）。
- 未完成：A.3（VDC getter/process_completed）、A.4（flight publish/receive/parse）、N5.0.3b（SCPI sync/TDMA/system）、N5.0.4（可选）、line-count 检查。
- HAOFV 六门（N5.0 级别）：
  - ① 无新增总线/class/第二命令槽/第二 ACK：✅ 全部纯迁移。
  - ② 不占 core1：✅；A.2b `RAM_TEXT` 随函数迁移，无窗口变化。
  - ③ 单写者/owner：✅ 状态单实例/accessor。
  - ④ fail-closed + reason/evidence：✅ 每刀 P3；负结果回退。
  - ⑤ 布局/版本/契约同步：✅ 无 wire/表布局变化。
  - ⑥ host + 四板证据：⚠️ P3 四板通过；A.2b HIL 报告 build 仍为旧值（`20261025015413`），板端未确认切到 `20261025021925`；收口前需复验 OTA 槽位。
- 结论：目标**不能标记完成**；A.3/A.4/N5.0.3b 需要新目标/扩展轮次。

### REFMEM-TASK-20261025-020 — N5.0.2f-A.3 第一次尝试回退（负结果）

- 状态：A.3 未完成；改动已回退，工作树在 A.2b（`0515b730`）。
- 尝试：把 `vdc_flight_rx` read/same helper + `get_vdc_flight_rx` 搬到 `distributed_refmem_vdc.c`，状态走 accessor。
- 失败原因：
  - getter 块边界内含 `#include "distributed_refmem_vdc_feedback.inc"`/`distributed_action_probe.inc` 与 `#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED`，按函数边界切仍把 inc/`#if` 带走/切开；
  - 主文件缺少 inc 定义时 `tdma_prepare_vdc_command_record` 编译失败；vdc.c 出现 `unterminated #if`。
- 结论：VDC 组必须按**完整条件块 + inc 归属**切，不能只按函数边界。
- 下一步：A.3 先确定 inc/`#if` 归属（把 `vdc_feedback.inc` 独立成 TU 或留在 main），再切 getter；或先做 N5.0.5 收口。
- P3：N/A（回退，无净代码）。

### REFMEM-TASK-20261025-019 — N5.0.2f-A.2b 完成：transport 迁出

- 状态：完成；行为不变。
- 内容：`get_position_transport` 按**函数边界**（到 `#include "distributed_refmem_vdc_feedback.inc"` 之前）搬到 `distributed_refmem_position.c`；`vdc_feedback.inc`/`action_probe.inc` 留在主文件；清理 transport 前的孤儿 `DISTRIBUTED_REFMEM_RAM_TEXT`。
- 行数：`distributed_refmem.c` 5,429→**5,373**。
- 验证：
  - build 成功；P3 `20261025021925`，fingerprint `c3585be2250f3e53921712caf7200ee3d2ff77dbaa0430d3029c9dc83fcc7f12`，1954 files，`PASS_WITH_WARNINGS/strict=false`。
  - exec196：`frag_complete=[1,1,1]`、`inject_ok=54`；`out/refmem_transport_diag/a2b2_exec196_202610251045/`。
  - ⚠️ HIL 报告 `build` 仍显示 `20261025015413`（上一版），板端未切到 `20261025021925`；P3 四板凭证按源码指纹覆盖本改动，HIL 作为烟测；下一轮需查 OTA 槽位/重启。
- 下一刀：A.3 VDC。

### REFMEM-TASK-20261025-018 — N5.0.2f-A.2b 小切（stage/get/cancel）

- 状态：完成；行为不变。
- 内容：`stage_position_descriptor` + `position_descriptor_pending` + `get_position_descriptor` + `position_cancel_service` 搬到 `distributed_refmem_position.c`；`note_freeze_taken`/`priority_owned` 非 static；`cancel_service` 非 static + 私有头声明。
- 行数：`distributed_refmem.c` 5,549→**5,429**。
- 验证：
  - build 成功；P3 `20261025015510`，fingerprint `b5302db938a5a597dea18d91c52fe00eb7489ef31cd5bcf13bb62d392ce6ac82`，1954 files，`PASS_WITH_WARNINGS/strict=false`。
  - exec196：`frag_complete=[1,1,1]`、`inject_ok=62`，verdict H4（reassembler 重新对齐，无永久 wedge）；`out/refmem_transport_diag/a2b_exec196_202610251020/`。
- 剩余：`get_position_transport`（内含 `vdc_feedback.inc` 与 `#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED`）仍在 main；下一刀按函数边界单独切。

### REFMEM-TASK-20261025-017 — N5.0.2f-A.2b 第一次尝试回退（负结果）

- 状态：A.2b 未完成；改动已回退，工作树在 A.2a（`12e5480d`）。
- 尝试：把 622–801 连续块（stage/get/transport）整体搬到 `distributed_refmem_position.c`。
- 失败原因：
  - 块内 `get_position_transport` 含 `#include "distributed_refmem_vdc_feedback.inc"` 与 `#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED`，连续行边界把 `#if`/`#endif` 切开，主文件出现 `#endif without #if`；
  - 结构体条件字段（`next_command_seq`/`vdc_command_record`）在主文件被排除。
- 结论：不能用“连续行段”切；必须按**函数/条件块**边界切。
- 下一步：A.2b 小切：先搬 `descriptor_pending`+`get_position_descriptor`（710–740，无 inc/条件块），再单独处理 `stage` 与 `transport`。
- P3：N/A（回退，无净代码）。

### REFMEM-TASK-20261025-016 — N5.0.2f-A.2a position 状态 wrapper

- 状态：完成；行为不变。
- 内容：`distributed_refmem_position_state_t`（pending/mailboxes/latches/counters/hold 队列）入 `distributed_refmem_internal.h`；`distributed_refmem_position_state()` 返回单实例；`s_position_*` 宏别名映射到 struct 字段；删除 main/`position_ack.inc` 的 static 定义。
- 行数：`distributed_refmem.c` 5,568→**5,549**；新增 `distributed_refmem_position_state.c`（约 20 行）。
- 验证：
  - build 成功；P3 `20261025012759`，fingerprint `89422a8adbe34850f2e0565d73c6de3bbd6625484f99fbbc2f8d5c77a44acd0a`，1953 files，`PASS_WITH_WARNINGS/strict=false`。
  - exec196：`frag_complete=[1,1,1]`、`inject_ok=54`；`out/refmem_transport_diag/a2a_exec196_202610250950/`。
- 下一刀：A.2b position 搬迁（`stage_position_descriptor`/`get_position_*`/`tdma_parse_position_fragment`）。

### REFMEM-TASK-20261025-015 — N5.0.2f-A.2a position 状态字段清单

- 状态：清单/方案完成；无代码（避免半成品）。
- 字段（main）：
  - `pending[DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS]`、`drain_ms`/`last_drain_ms`/`coalesced_count`、`drain_cursor`、`wait_counted`、`rx_mailboxes[NODE_COUNT]`、`freeze_latches[NODE_COUNT]`、`cancel_requested`。
- 字段（`distributed_refmem_position_ack.inc`）：
  - `local_frozen`、`freeze_ack[NODE_COUNT]`、`priority_owned`、`priority_last[NODE_COUNT][SLICE_WIRE]`；
  - `priority_accept_count`/`duplicate_count`/`reject_count`/`decode_reject_count`；
  - `priority_hold[NODE_COUNT][HOLD_DEPTH]`、`hold_head/tail[NODE_COUNT]`、`hold_count`/`hold_dropped_count`/`hold_delivered_count`；
  - `freeze_latched_count`/`freeze_kept_count`/`freeze_taken_count`；
  - `priority_drain_count`/`drain_fail_count`、`hold_expired_count`、`ack_override_count`、`diag_refused_count`。
- 类型：`trigger_position_mailbox_t`/`freeze_latch_t`/`slice_t`、`distributed_refmem_position_pending_t`、`distributed_refmem_position_hold_entry_t`；宏 `PENDING_SLOTS`/`HOLD_DEPTH`。
- 方案：`distributed_refmem_position_state_t` + `distributed_refmem_position_state()`；`s_position_*` 宏别名映射到 struct 字段；删除 main/inc 的 static 定义；不改内存布局/语义。
- 风险：`.inc` 会在 host harness 编译；宏别名需在 internal.h；hold 队列的 `__atomic` 指针必须指向 struct 字段。
- 下一步：A.2a 代码（struct/宏/实例 + P3）→ A.2b position 搬迁。

### REFMEM-TASK-20261025-014 — N5.0.2f-A.2/A.3 状态拆分设计

- 状态：设计完成；无代码变更。
- 阻塞点：position 组依赖分散的 `s_position_*` 静态（pending 槽、rx mailboxes、freeze latches、drain/cancel 计数），VDC 组依赖 `s_vdc_flight_rx_*` 与 `s_vdc_command_context*`；不是单一 struct，不能像 A.1 那样直接搬。
- 设计：
  - **A.2a**：把 `s_position_*` 收敛为 `distributed_refmem_position_state_t` 单实例 + accessor（纯 rename/包装，不改行为）。
  - **A.2b**：把 `stage_position_descriptor`/`get_position_*`/`tdma_parse_position_fragment` 搬到 `distributed_refmem_position.c`，用 accessor；P3/HIL。
  - **A.3a**：把 `s_vdc_flight_rx_binding/snapshot/admission_ready` 收敛为 `distributed_refmem_vdc_flight_state_t` + accessor；`s_vdc_command_context` 同理。
  - **A.3b**：把 `get_vdc_flight_rx`/`vdc_follower_rx_process_completed`/`get_vdc_follower_*` 搬到 `distributed_refmem_vdc.c`。
  - **A.4**：publish/receive/parse 搬 `distributed_refmem_flight.c`，用 A.2/A.3 accessor。
- 验收：每步纯 rename/包装/移动，行为不变；build + P3 + exec196/soak HIL；单文件 ≤1,500。
- 风险：状态字段多、`__atomic`/time-critical 访问；wrapper 只改名字，不改内存布局/语义。
- 下一步：A.2a（代码 + P3）。

### REFMEM-TASK-20261025-013 — N5.0.2g flight-sync diag getters（A.5 部分）

- 状态：完成；行为不变。
- 内容：`get_tdma_flight_sync`/`_peer`/`_mirror`/`_quality` 迁出到 `distributed_refmem_diag.c`；通过 `distributed_refmem_tdma_flight_sync()` accessor 读状态；`MAILBOX_SIZE` 宏入 internal 头。
- 行数：`distributed_refmem.c` 5,690→**5,568**。
- 验证：
  - build 成功；P3 `20261025010050`，fingerprint `c3ebded73634a4fabcc76c3fc95df97e158cf0ce8912323994d2e6865c40f487`，1952 files，`PASS_WITH_WARNINGS/strict=false`。
  - SCPI smoke：`SYSTem:REFMEM:SYNC:FLIGHT?`、`SYSTem:REFMEM:STATus?` 正常；`out/refmem_transport_diag/flash_n502g_202610250915/`。
- 下一步：A.2 position（position 状态分散，需先做状态/访问器设计）或 A.3 VDC。

### REFMEM-TASK-20261025-012 — N5.0.2f-A.1 状态私有头（A.1）

- 状态：完成；行为不变。
- 内容：`distributed_refmem_tdma_flight_sync_t` + `DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_PAYLOAD_SIZE` 迁入 `distributed_refmem_internal.h`；实例留主文件，新增 `distributed_refmem_tdma_flight_sync()` 访问器。
- 行数：`distributed_refmem.c` 5,763→**5,690**。
- 验证：
  - build 成功；P3 `20261025003548`，fingerprint `3714dde606863c671329fdd88fefd2e480346ba68e63e6c7a114bfde5738efce`，1951 files，`PASS_WITH_WARNINGS/strict=false`。
  - exec196：`frag_complete=[1,1,1]`、`inject_ok=54`；`out/refmem_transport_diag/n502f_a1_exec196_202610250850/`。
- 下一刀：A.2 position descriptor 组。

### REFMEM-TASK-20261025-011 — N5.0.2f TDMA flight 拆分设计（A）

- 状态：设计完成；无代码变更。
- 现状：`distributed_refmem.c` 5,763 行；`distributed_refmem_tdma_flight_sync_t` 状态（约 80 行）+ publish/receive/parse/position/VDC 约 2,400 行。
- 拆分顺序：
  - **A.1 状态头**：把 `distributed_refmem_tdma_flight_sync_t` 与所需宏迁到 `distributed_refmem_internal.h`；实例留在主文件，新增 `distributed_refmem_tdma_flight_sync()` 访问器；不改行为。
  - **A.2 position**：`stage_position_descriptor`/`get_position_*` → `distributed_refmem_position.c`；用访问器。
  - **A.3 VDC**：VDC flight RX/command context → `distributed_refmem_vdc.c`。
  - **A.4 flight**：publish/receive/parse/compact mailbox → `distributed_refmem_flight.c`。
  - **A.5 core/diag**：剩余 core/status → `distributed_refmem_diag.c` / 保留 core。
- 验收：每步纯移动/访问器，行为不变；build + P3 + exec196/soak HIL；单文件 ≤1,500。
- 风险：状态字段访问面大；宏/类型归属（TDMA/trigger/VDC）需在私有头一次性收敛；time-critical 宏不能改。
- 下一步：A.1（代码 + P3）。

### REFMEM-TASK-20261025-010 — N5.0.3a 拆出 SCPI refmem 控制块（B）

- 状态：完成；行为不变。
- 内容：新增 `scpi_refmem_snapshot_commands.c`（1,150 行）；`REFMEM:STATus/NODE/BOARD/CLAIM/LOAD/CMD/CONFig/LEASE/CTRL/NODE:CONTrol/...` 控制块迁出；10 个 helper 经 `scpi_system_snapshot_internal.h` 共享；`s_refmem_sync` 通过 accessor 暴露。
- 行数：`scpi_system_snapshot_commands.c` 5,712→**4,627**。
- 验证：
  - build 成功；P3 `20261025001128`，fingerprint `b0024662f6af8aa0d993f07dfd2214fcdd6598b4721e01f8e0b472cd34b29d0c`，1951 files，`PASS_WITH_WARNINGS/strict=false`。
  - SCPI smoke（COM4）：`REFMEM:STATus?` 输出、`CTRL:CATalog?` 6 条、`LEASE?`=FREE、`LOAD:STATus?` 正常；`out/refmem_transport_diag/flash_n503a_202610250825/`。
- 下一刀：A（N5.0.2f TDMA flight，最大块）。

### REFMEM-TASK-20261025-009 — N5.0.1 预算与关注点地图（C）

- 状态：完成（docs，无代码）。
- 内容：在 `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` 新增 `EXE-CODE-01`：
  - 新文件 ≤800 行；拆后单元 ≤1,500；>2,000 需 TODO/豁免；>3,000 冻结新功能（拆分除外）；聚合器/SCPI ≤1,300。
  - 拆分约束：纯移动/重命名 + 私有头；不改行为；每步 build/P3/HIL/SCPI 烟测；代码/文档分离。
- 当前超标快照（非事实源）：`distributed_refmem.c` 5,763、`scpi_system_snapshot_commands.c` 5,712、`vdc_dpll_manager.c` 4,826 等。
- 下一步：B（N5.0.3a SCPI refmem 控制块拆分，目标 ~1,000 行）→ A（N5.0.2f TDMA flight）。

### REFMEM-TASK-20261025-008 — N5.2a.2 尝试：候选 TDMA profile 指针无效（负结果）

- 状态：N5.2a.2 未完成；代码已回退，工作树干净。
- 改动：`serialize_candidate_table(9)` 改用 `refmem_model_candidate_tdma_foundation_profile()`（`&s_tdma_foundation_profile`），不再用 active image profile。
- 结果：ring start/stop 后仍 `staging_state=3`、`lint=2312=0x0908`（OWNER_VALIDATION/table 9）；control 仍 `lint=0/staging_state=2`。
- 结论：table 9 的 owner/version 在 **profile 数据本身**，已被 ring 更新；仅换指针不够。需要 app-model init 时的 **profile 快照**，或把 table 9 的 owner 契约与 ring 运行态分离。
- 代码回退；实验 P3 `20261024181340`（e8a3db…）不作为提交凭证；保留为负结果证据。
- 下一步：N5.2a.2 snapshot API/owner 契约设计；N5.2a 仍阻塞。

### REFMEM-TASK-20261025-007 — N5.2a.1 诊断：table 9 TDMA foundation profile owner validation

- 状态：N5.2a.1 完成；无行为变更（仅诊断字段）。
- 实验：
  - 对照（未跑环）：`staging_state=2`、`lint=0`、`last_error=0`、`state=4 ACKED`。
  - ring start/stop 后：`staging_state=3`、`lint=2312`、`last_error=7`、`state=5/reason=13`。
- 解码：`2312 = 0x0908` → 低字节 `validation.error=8`（`REFTMEM_TABLE_PACKAGE_ERR_OWNER_VALIDATION`），高字节 `first_bad_table=9`（`REFTMEM_APP_TABLE_TDMA_FOUNDATION_PROFILE`）。
- 结论：ring start 改变了 **TDMA foundation profile** 表/owner；app-model inline package 的 owner 契约对 table 9 失败。不是 table registry 描述子、不是权限、不是“stage 瞬间环在跑”。
- 下一步：N5.2a.2 在 app-model 侧使用**稳定的 TDMA foundation profile 快照/owner 契约**（不让 ring 运行态 profile 进入 inline package），再跑 ring start/stop → stage HIL。
- 证据：`out/refmem_remote_txn_hil/n52a1_diag/report.json`。

### REFMEM-TASK-20261025-006 — N5.2a 设计决策：app-model staging 隔离

- 状态：N5.2a 未完成；设计决策完成，无代码变更。
- 补充诊断：`SYSTem:REFMEM:TABLE?` 在 ring start/stop 前后完全一致（`1,10,1023,0,1270410720,...`），说明 **table registry 描述子不是变化点**；失败仍在 app-model `stage_inline_package_image` 的 `PACKAGE_INVALID`。
- 结论：不能靠“STOP 重置 table registry”解决；需要把 **app-model staging 状态/表内容** 与 ring 运行期状态隔离。
- 决策：**N5.2a 选 option 1（app-model staging 隔离）**，不选 option 2（STOP 重置）：
  - N5.2a.1：在 `stage_inline_package_image` 失败路径暴露 `validation.error`/`first_bad_table`（诊断字段/日志），确认具体表；
  - N5.2a.2：建立 app-model staging registry 基线（表 owner/version/CRC 独立于 ring/process image）；
  - N5.2a.3：stage 只基于该基线构建/校验；
  - N5.2a.4：HIL ring start/stop 后 stage → `staging_state=2`/ACK。
- 证据：`out/refmem_remote_txn_hil/n52_table_diag/{before,after}.txt`（一致）；`n52_diag/REPORT.md`。
- 下一步：N5.2a.1 诊断字段（代码 + P3），再决定 N5.2a.2 的 API 形态。

### REFMEM-TASK-20261025-005 — N5.2a 诊断：STOP-only stage 仍失败（进行中，阻塞）

- 状态：N5.2a 未完成；deferral 代码已回退；工作树干净。
- 实验：B `CONFig:BEGIN`；A 排队 `LOAD_NODE`；ring1 start/stop 后读 B `LOAD:STATus?`：
  `load_seq=1, staging_state=3 FAILED, last_error=7 PACKAGE_INVALID, lint_error=1 BAD_TABLE_VERSION`。
- 对照：fresh reset、**未跑环**的本地 stage 为 `staging_state=2 VALIDATED` + ACK。
- 结论：不是“stage 瞬间环在跑”，而是**环 start 已经改变了 app-model table registry/package**，STOP 不恢复；因此“环内收命令、STOP 执行”的 deferral 不足以让 stage 成功。
- 下一步：N5.2a 设计——把 app-model table package/registry 与 TDMA process image 分离，或在 STOP 重建 staging context；未定前不写 deferral。
- 证据：`out/refmem_remote_txn_hil/n52_diag/REPORT.md`（追加第二次诊断）。

### REFMEM-TASK-20261025-004 — N5.0.2f 成功：VDC SCPI 拆分（设计后）

- 状态：N5.0.2f 完成；此前连续块尝试（003）失败后，按“TU + 私有头 + `.inc`/helper 一起迁移”的设计重做成功。
- 内容：新增 `scpi_vdc_snapshot_commands.c`（740 行）；VDC 反馈/运行/固定/细分 handler + `scpi_feedback_record` 迁出；`scpi_vdc_run_u32`/`run_end` 非 static，经 `scpi_system_snapshot_internal.h` 共享；`.inc` 链留在主文件（避免重复 trigger 定义）。
- 行数：`scpi_system_snapshot_commands.c` 6,393→**5,712**。
- 验证：
  - build 成功；P3 `20261024170018`，fingerprint `de957c6940d2c9517da37b931ec8848d122289548ca67cff8280c27c0091ddcd`，1950 files，`PASS_WITH_WARNINGS/strict=false`。
  - VDC SCPI smoke：`SYSTem:VDC:FEEDback:SESSion?`=0、`AUTO?`=0；`tools/interface/scpi_query`。
- 下一刀：N5.0.2g TDMA flight（状态/私有头设计后再切）。

### REFMEM-TASK-20261025-003 — N5.0.2f 尝试：VDC SCPI 拆分失败（已回退，负结果）

- 状态：N5.0.2f **未完成**；尝试后回退，无代码变更。
- 尝试：把 `scpi_system_snapshot_commands.c` 3032–3698 的 VDC 块整体移到 `scpi_vdc_snapshot_commands.c`。
- 失败：`scpi_vdc_output_slices.inc` 仍在主文件尾部，依赖被移走的 `scpi_vdc_run_end`/`vdc_run_output_status_t`；连续 handler 块移动破坏 include/helper 顺序，build 报 `unknown type name`/`implicit declaration`。
- 处理：`git checkout` 主文件、删除新文件、CMake 回退；工作树干净。
- 教训：剩余 SCPI/TDMA 大块不能按“连续行段”机械移动；必须按 **translation unit + 私有头** 拆，且把 `.inc`/类型/helper 一起迁移并保持定义顺序。
- N5.0.2f：VDC SCPI 拆分在设计后成功（6,393→5,712）；P3 `20261024170018`；VDC SCPI 查询 smoke 通过。
- N5.2a 第二次诊断：环 start/stop 后 stage 仍 PACKAGE_INVALID（staging_state=3/last_error=7）；deferral 不是充分条件，代码已回退。
- N5.2a 设计决策：选 **app-model staging 隔离**（option 1），不选 STOP 重置；先加 `validation.error` 诊断，再建 staging registry 基线。
- N5.2a.1：诊断字段已定位 table 9（TDMA foundation profile）owner validation 失败；`lint=2312=0x0908`。
- N5.2a.2 尝试：仅换 profile 指针无效（ring 后仍 lint=2312=0x0908）；需 profile 快照/owner 契约分离。
- N5.0.1（C）：预算/关注点地图完成；`EXE-CODE-01` 写入执行约束。
- N5.0.3a（B）：SCPI refmem 控制块拆出（5,712→4,627）；P3 `20261025001128`；SCPI smoke 通过。
- N5.0.2f（A）拆分设计完成：A.1 状态头 → A.2 position → A.3 VDC → A.4 flight publish/receive/parse → A.5 core/diag。
- A.1 完成：flight 状态/宏迁入 internal.h + accessor；主文件 5,763→5,690；P3 `20261025003548`；exec196 `[1,1,1]`。
- N5.0.2g（A.5 部分）：flight-sync 4 个只读 getter 拆出到 `distributed_refmem_diag.c`；5,690→5,568；P3 `20261025010050`；SCPI smoke 通过。
- A.2/A.3 状态设计完成：position/VDC 状态需先收敛为 struct + accessor，再搬 getter/process_completed。
- A.2a 字段清单完成：position 状态 20+ 字段（main + position_ack.inc）；下一步 struct+宏+accessor 代码。
- A.2a 完成：position 状态 wrapper（struct+宏+accessor）；5,568→5,549；P3 `20261025012759`；exec196 `[1,1,1]`。
- A.2b 第一次尝试回退：622–801 连续块跨 `#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED` 和 `vdc_feedback.inc`，边界不干净。
- A.2b 小切完成：stage/descriptor_pending/get_position_descriptor/cancel service 搬出；5,549→5,429；P3 `20261025015510`；exec196 `[1,1,1]`。
- A.2b 完成：transport 按函数边界迁出；5,429→5,373；P3 `20261025021925`；exec196 `[1,1,1]`（HIL build 显示旧值，⚠️）。
- A.3 第一次尝试回退：getter 块跨 `#include vdc_feedback.inc`/`#if`，边界不干净；工作树回到 A.2b。
- N5.0.5 收口报告完成：行数快照/完成项/残差/HAOFV 六门；N5.0 **未完成**。
- 下一步：A.3/A.4/N5.0.3b 需扩展目标；当前目标剩余轮次不足。

### REFMEM-TASK-20261025-002 — N5.0.2e 拆出 dispatch/command（进行中）

- 状态：N5.0.2 进行中；第五刀完成，行为不变。
- 内容：新增 `distributed_refmem_command.c`（191 行）；`dispatch_control`、`node_control_service`、lease reason/config type、三个共享状态迁出；`post_command_replacing_complete`/`node_load_auto_enqueue`/`vector_table` 经 internal 头共享。
- 行数：`distributed_refmem.c` 5,946→**5,763**。
- 验证：
  - build 成功；P3 `20261024163157`，fingerprint `711cfd6e2278682e0110456cf336f69a31a01e35b335c664573c1cb3d5df2e10`，1948 files，`PASS_WITH_WARNINGS/strict=false`。
  - config txn HIL 14/14（build `20261024162907`）：`out/refmem_config_txn_hil/n502e_202610250045/`。
- 下一刀：tdma-vdc / diag；每步 build/P3/HIL 烟测。

### REFMEM-TASK-20261025-001 — N5.0.2d 拆出 cmd-frame（进行中）

- 状态：N5.0.2 进行中；第四刀完成，行为不变。
- 内容：新增 `distributed_refmem_cmd_frame.c`（63 行）；`cmd_frame_counters` + `cmd_frame_receive` 及 5 个计数迁出。
- 行数：`distributed_refmem.c` 6,003→**5,946**。
- 验证：
  - build 成功；P3 `20261024160525`，fingerprint `f8bef21bd3f04a1333e4db51d06b00e86d5925f58fa24c72f3e1adb5293614dc`，1947 files，`PASS_WITH_WARNINGS/strict=false`。
  - exec196 HIL：`frag_complete=[1,1,1]`、`inject_ok=60`；`out/refmem_transport_diag/n502d_exec196_202610250020/`。
- 下一刀：dispatch/command 或 tdma-vdc / diag。

### REFMEM-TASK-20261024-021 — N5.0.2c 拆出 node control（进行中）

- 状态：N5.0.2 进行中；第三刀完成，行为不变。
- 内容：新增 `distributed_refmem_node_control.c`（109 行）；`node_control_write_with_params/read_params/read/ack/update_status/clear` 迁出；`distributed_refmem_node_region` 改为非 static，经 `distributed_refmem_internal.h` 共享。
- 行数：`distributed_refmem.c` 6,109→**6,003**。
- 验证：
  - build 成功；P3 `20261024154054`，fingerprint `7819143263a117d0d04321837c8ed156afdc160fa02148244845a2b5b915757f`，1946 files，`PASS_WITH_WARNINGS/strict=false`。
  - config txn HIL 14/14（build `20261024153758`）：`out/refmem_config_txn_hil/n502c_202610242357/`。
- 下一刀：dispatch/command 或 tdma-vdc / diag；每步 build/P3/HIL 烟测。

### REFMEM-TASK-20261024-020 — N5.0.2b 拆出 cmd-reply（进行中）

- 状态：N5.0.2 进行中；第二刀完成，行为不变。
- 内容：新增 `distributed_refmem_cmd_reply.c`（232 行）与私有头 `distributed_refmem_internal.h`；tx/rx 状态、arm/pending/store/service/status 全部迁出；主文件通过 `tx_get`/`target_mask`/`note_published` 访问，不再直接读 `s_cmd_reply_tx`。
- 行数：`distributed_refmem.c` 6,315→**6,109**。
- 验证：
  - build 成功；P3 `20261024151357`，fingerprint `e23626dec22b05704dc52a59c2e6d4234b9854de04309b95ab09b087273f13ed`，1945 files，`PASS_WITH_WARNINGS/strict=false`。
  - exec196 HIL：`frag_complete=[1,1,1]`、`inject_ok=54`、无丢包/重复；`out/refmem_transport_diag/n502b_exec196_202610242330/`。
- 下一刀：command dispatch/node control 或 tdma-vdc / diag；每步 build/P3/HIL 烟测。

### REFMEM-TASK-20261024-019 — N5.0.2a 拆出 config-txn（进行中）

- 状态：N5.0.2 进行中；第一刀完成，行为不变。
- 内容：新增 `refmem_config_txn.c`（idle 窗口 + BEGIN/COMMIT/ABORT/active + 状态）；`distributed_refmem.c` 6,370→6,315；dispatch gate 改用 `distributed_refmem_idle_window()`。
- 验证：
  - build 成功；P3 `20261024144807`，fingerprint `9a3f7cb06ca2f93e777fbf8aebbe843d2071d6ab38e786acc90b643fab827e2c`，1943 files，`PASS_WITH_WARNINGS/strict=false`。
  - config txn HIL 14/14（build `20261024144443`）：`out/refmem_config_txn_hil/n50a_202610242305/`。
- 下一刀：cmd-reply / command dispatch / tdma-vdc / diag 继续拆；每步 build/P3/HIL 烟测。

### REFMEM-TASK-20261024-018 — N5.2 诊断：reason 13 根因（进行中）

- 状态：N5.2 进行中；发现 reason 13 根因不是 owner/permission，而是**环运行期 app-model staging 失败**。
- 诊断（build `20261024132502`）：
  - 远程 A→B（环运行）：`command_type=14`、`source=0`、`target=2`、`state=5`、`reason=13`；`LOAD:STATus?` → `load_seq=1, staging_state=3 FAILED, last_error=7 PACKAGE_INVALID, lint_error=1 BAD_TABLE_VERSION`。
  - 本地 COM4（环停止）：`state=4 ACKED`、`staging_state=2 VALIDATED`、`package_crc` 非 0。
  - 本地 COM4（环运行）：`state=5 reason=13`、`staging_state=3`、`last_error=7`，与远程同签名。
- 结论：reason 13 是**环运行期 app-model staging/table-package 冲突**；单纯加权限门控只会把 A→B node 1 从 13 变成 12，不会让合法远程 stage ACK。
- 重排：N5.2a 环安全/延迟 staging → N5.2b owner/permission → N5.2c 真实 STOP→stage→START；N5.3/N5.4 仍先于 N5.5。
- raw-flight 对照：环运行（无 process-image）本地 stage 仍 `staging_state=3/last_error=7`，排除 process-image 因素。
- **N5.2a 决策**：选**延迟配置事务（STOP-only stage）**——环运行只收命令/排队，STOP 执行，START 后回 ACK；不选环安全 staging。开放：命令超时跨 STOP/START、队列深度/顺序、多目标 ACK 聚合、COMMIT 与 START 先后。
- 证据：`out/refmem_remote_txn_hil/n52_diag/REPORT.md`。
- 下一步：N5.2a 设计决策（环安全 staging vs 环内收命令/STOP 执行/重启回 ACK 的延迟事务）。

### REFMEM-TASK-20261024-017 — N5.1 契约交叉审核（C11）

- 状态：完成；审核结论 **activate subset**：`REFTMEM-LEASE-01` 转 active；`REFMEM-TRANSPORT-01`/`REFTMEM-IDLE-01` 保持 pending。
- 审核方：独立 subagent `c774c2f8`（agent-cross，独立上下文，未改文件）；方式：证据包 + 代码/文档/HIL/P3 复核；证据包 `out/refmem_contract_review/N5_1_EVIDENCE.md`。
- 结论：
  - `REFTMEM-LEASE-01`：PASS_WITH_NOTE → active。host + n333 两板 HIL 覆盖 clause；备注：lazy expiry（`LEASE?` 可能显示过期 GRANTED 直到下一次 TAKE/RELEASE）、边界（环 stop→start 保持、板复位清零、无跨板复制/协调者租约/心跳）已写入 verification。
  - `REFMEM-TRANSPORT-01`：FAIL → pending。blocking：clause “seat rotation 二等让位 s>0 且 f>=3” 与实现/预设不符（代码只校验 p>=1/f>=1；预置 `1,2,7`/`7,2,1` 的 f=2；HIL 自身用 `1,2,7`）；P3 凭证为 `PASS_WITH_WARNINGS`、`strict_gates_passed=false`（step 26 four-Node TDMA rc=1、step 33 预算超限），不能写成 strict passed；维护 ACK 的 clause_loc 跨 TDMA/REFMEM 两个 anchor。
  - `REFTMEM-IDLE-01`：FAIL → pending。blocking：无 host 用例；legacy `SYSTem:REFMEM:LOAD:NODE/BOARD/ACTIVATE` 绕过 idle 窗口（不同 gate/错误文本，直接 app-model stage）；无真实 STOP→stage→START 证据（HIL 只有 BEGIN/stage/COMMIT/ABORt）；remote “valid” stage 全为 reason 13（仅断言 !=7）；COMMIT 失败仍返回成功/关窗，无法区分。
- 数据修正：soak20 `tx_retries=5`（原写 4）；P3 统一表述为 `credential written (PASS_WITH_WARNINGS; strict=false)`，不写“P3 通过”。
- carry-over：N5.1a（TRANSPORT seat/条款/P3 诚实表述）、N5.1b（IDLE host/legacy/STOP/COMMIT）、N5.2a（环运行期 app-model staging，reason 13 根因）、N5.2b（owner/permission）、N5.2c（真实 STOP→stage→START）。
- 下一步：N5.2；N5.1a/b 作为 TRANSPORT/IDLE 复审前置。
### REFMEM-TASK-20261024-016 — N4.3/N4.4 远程事务 HIL 与收口

- 状态：完成；两板/四板 HIL 全过；P3 凭证已写。
- 完成内容：
  - HIL `tools/refmem/refmem_remote_txn_hil`：目标 BEGIN/COMMIT/ABORt 本地事务，A 经环发 `LOAD_NODE`。
  - 两板（COM4）与四板（COM4/3/6）逐目标证据。
- 验证结果：
  - 目标 BEGIN 后 A stage → `reason=13`（门控通过、业务失败）；
  - 目标 COMMIT 后同命令 → `reason=7`（RUN_STATE_DENIED）；
  - 目标 BEGIN + 无效 stage → `reason=13`；ABORt → `active=0,window=1`；
  - ABORt 后 A stage → `reason=13`（窗口重开）。
  - 证据 `out/refmem_remote_txn_hil/n43_two_b_202610242118/`、`n43_four_202610242120/`。
- 边界：远程配置 stage 当前返回业务 reason 13（远端 owner/permission 校验未实现）；窗口门控 reason 7 可区分。`REFTMEM-IDLE-01` 已登记（pending），N4.4 收口完成。
- P3：build `20261024132502`，源码指纹 `bb1c5932e5f2ce0b0b9f80c3fba10cb42bdb7a9e5dbfb426c74667c8a8e27094`（1942 files），`QUICK_DIAGNOSTIC`，credential written；step 23/26/33 warn，无 ERROR/FATAL。
- 下一步：N3.3/N4 目标收口；N3.2 剩余矩阵为可选补测。
### REFMEM-TASK-20261024-015 — N4.2 配置事务（STOP→stage→START）

- 状态：完成（本地事务 HIL）；P3 凭证已写。
- 完成内容：
  - `distributed_refmem_config_transaction_begin/commit/abort/active`：BEGIN 开窗并标记事务；COMMIT 提交 prepared table views（失败丢弃）并关窗；ABORt 丢弃并重开；状态只读。
  - `SYSTem:REFMEM:CONFig:BEGIN/COMMit/ABORt/STATus?`。
  - HIL `tools/refmem/refmem_config_txn_hil`。
- 验证结果（14 步全过）：
  - 初始 `active=0,window=1`；BEGIN → `1,1`；stage → ACK；COMMIT → `1`、`0,0`；
  - COMMIT 后 stage → `state=5,reason=7`；
  - 再次 BEGIN + 无效 stage(node 99) → `reason=13`（门控已过）；ABORt → `0`、`0,1`。
  - 证据 `out/refmem_config_txn_hil/n42b_202610242035/`。
- 边界：调用方负责外部 STOP/START 边界；prepared table views 有 commit/discard，节点/板级 stage 由各自 dispatch 校验；N4.3 做两板/四板组合 HIL。
- P3：build `20261024123825`，源码指纹 `e24f4bb5fa64accd4c6bdb5341a02638885f7b47ac0dbc952d204648b640e37b`（1941 files），`QUICK_DIAGNOSTIC`，credential written；step 8 拓扑首次失败、重试后通过；step 23/26/33 warn，无 ERROR/FATAL。
- 下一步：N4.3 两板/四板 HIL → N4.4 契约/文档收口。
### REFMEM-TASK-20261024-014 — N3.3.4 租约收口 + N4.1 idle 窗口门控

- 状态：N3.3.4 完成；N4.1 门控 + 测试钩子完成；P3 凭证已写。
- 完成内容：
  - N3.3.4：`REFTMEM-LEASE-01` 契约/架构/SCPI/里程碑收口；两板租约 HIL 工具 `tools/refmem/refmem_lease_hil`。
  - N4.1：`distributed_refmem_set_idle_window`/`idle_window`；配置类命令（BOARD/NODE/TABLE stage/activate）在窗口关闭时以 `RUN_STATE_DENIED` 拒绝，不进入 dispatch。
  - `SYSTem:REFMEM:IDLE:WINDow <0|1>` / `?` 测试/事务钩子。
- 验证结果：
  - 租约两板 HIL 全通过：`out/refmem_lease_hil/n333_202610242012/`。
  - idle HIL：默认开=1；关=0 后 LOAD_NODE → `state=5/reason=7`；开=1 后同命令 → `state=4/reason=0`。`out/refmem_idle_hil/n41_202610242040/`。
  - P3 凭证已写。
- P3：build `20261024120853`，源码指纹 `b3ccf1b1af4db71e676d391f0e6ebb3cbb3bd95d448c88d9f528251ddeee547a`（1940 files），`QUICK_DIAGNOSTIC`，credential written；step 23/26/33 warn，无 ERROR/FATAL。
- 下一步：N5.2 远程配置 owner/permission（同时解 IDLE remote reason 13）；N5.1a/b 作为 TRANSPORT/IDLE 复审前置。
### REFMEM-TASK-20261024-013 — N3.3.3 两板租约 HIL

- 状态：完成；两板 A TAKE→B CONFLICT→A RELEASE→B TAKE + ring stop→start 全部通过。
- 步骤（A=COM5 slot0，B=COM4 slot1，B 的 target bit=2）：
  - A 经环 `LEASE_TAKE` epoch1 → B `LEASE?=owner 0,GRANTED,lease_seq=1`。
  - B 本地 `LEASE_TAKE` epoch2 → `CONFLICT`：B `CTRL:STATus? state=5`、`reason=6 (RESOURCE_BUSY)`；B 仍归 A。
  - 停环（不复位）→ B 租约保持 `owner 0,GRANTED`（RAM 记录跨 ring stop）。
  - A 第二个环 `LEASE_RELEASE` epoch1 → B `FREE,lease_seq=2`。
  - B 本地 `LEASE_TAKE` epoch2 → `owner 1,GRANTED,lease_seq=3`、`state=ACKED`。
- 证据：`out/refmem_lease_hil/n333_202610242012/REPORT.md`。
- 工具 `tools/refmem/refmem_lease_hil/refmem_lease_hil.py` 已落盘，待与后续固件变更一起 P3/提交。
### REFMEM-TASK-20261024-012 — N3.3 最小租约（状态机 + 控制面接入）

- 状态：N3.3.1/2 完成（host + 本地 smoke）；P3 凭证已写；N3.3.3 两板 HIL 待做。
- 完成内容：
  - `refmem_lease` 纯状态机（FREE/GRANTED/EXPIRED，owner/epoch/seq/expiry；TAKE/CONFLICT/STALE/EXPIRED/NOT_OWNER 的 fail-closed reason）。
  - `refmem_lease_table` 每目标节点一条记录；目录新增 `REFTMEM_LEASE_TAKE`（type 18）/`REFTMEM_LEASE_RELEASE`（type 19），经现有命令槽/ACK 回程执行。
  - `distributed_refmem_dispatch_control` 执行租约并映射 reason（CONFLICT→RESOURCE_BUSY、STALE→EPOCH_MISMATCH、EXPIRED→TIMEOUT、NOT_OWNER→PERMISSION_DENIED）。
  - `SYSTem:REFMEM:LEASE?` 只读视图；`REFTMEM-LEASE-01` 契约登记（pending）。
- 验证结果：
  - host `test_refmem_lease` / `test_refmem_lease_table` PASS。
  - COM4 本地 smoke：TAKE(epoch1) → `1,1,1,90406,1,"GRANTED"`、`CTRL:STATus? state=ACKED`；RELEASE → FREE；TAKE(epoch2) → `1,2,1,"GRANTED"`。
  - 固件构建/刷写通过；`CTRL:CATalog?` 列出 id 5/6、type 18/19。
- P3：build `20261024113544`，源码指纹 `3ba76ea57489ce930b8efd129ca8db04e51723637a44b35ffa7df1839ca3d55c`（1938 files），`QUICK_DIAGNOSTIC`，credential written；step 26/33 warn，无 ERROR/FATAL。
- 下一步：N3.3.3 两板 HIL（A TAKE→B CONFLICT→A RELEASE→B TAKE，含 ring stop→start）。
### REFMEM-TASK-20261024-011 — per-target 维护帧 ACK

- 状态：完成；P3 凭证已写（PASS_WITH_WARNINGS；step 26/33 warn）。
- 问题：soak 20 出现 3/20 失败（COM4 漏帧、COM3/6 完成）；根因是帧级 ACK 由**任一**目标回复即释放，源端误判整帧已送达所有目标。
- 修正：
  - `tdma_maintenance_runtime_submit` 增加 `target_mask`；队列保存并写入 mailbox target mask，只发给显式目标。
  - `tdma_maintenance_runtime_ack` 增加 `source_slot`，按目标累积 `ack_mask`；仅当 `required_mask` 全覆盖才释放，NACK/50 ms 超时重传整帧（最多 3 次）。
  - `parse_mailbox` 把 ACK 来源槽传给 runtime；命令回复仍优先占用 ACK 席位。
- 验证结果：
  - per-target soak 10/10：`inject_ok=54`（1 次重传为 60）、三目标 `frag_complete=1`。`out/refmem_transport_diag/n327_pertarget_soak10_202610241803/`。
  - drop 1 1/1：`inject_ok=67`、三目标 `frag_complete=1`。`n327_pertarget_drop1_202610241810/`。
  - reply HIL multi：`state=4`、`ack_flags=14`、`aggregate_state=4`。`out/refmem_cmd_reply/pertarget_multi_202610241810/`。
  - per-target soak 20/20：5 次重传恢复（`inject_ok=60/62`）、1 次 restart，三目标 `frag_complete=1`。`out/refmem_transport_diag/n327_pertarget_soak20_202610241849/`。
  - P3：build `20261024102324`，源码指纹 `9da69200c16121a8d8205d9f4316997df8395a77ff1b441122d47ff1cbf547fd`（1930 files），`QUICK_DIAGNOSTIC`，credential written；DPLL 前两次失败后 `--steps 7-36` 重试通过；step 26/33 warn，无 ERROR/FATAL。

### REFMEM-TASK-20261024-010 — N3.2.8 契约/文档收口与 ACK 席位优先级

- 状态：完成；ACK 优先级修正 + 契约/架构/SCPI 记录；N3.2.6 延后、N3.2.7 部分在案。
- 完成内容：
  - process-image builder 的 ACK 分支改为**命令回复优先**，维护 ACK 仅在无命令回复时发布；publish flags 同步。
  - `REFTMEM-TRANSPORT-01` 登记维护流帧级 ACK（复用 ACK 席位、诊断 magic `0xA0`、命令回复优先）。
  - `TDMA_DOMAIN_ARCHITECTURE`/`REFMEM_DOMAIN_ARCHITECTURE` 补 stop-and-wait/重试/≥2 s 窗口/优先级；`SCPI_COMMANDS` 已记 `CMD:DROP`。
- 验证结果：
  - reply HIL multi：origin `state=4`、`ack_flags=14`、`aggregate_state=4`、`acked_mask=14`；targets 各自 ACK 位。证据 `out/refmem_cmd_reply/ackprio_multi_202610241700/`。
  - exec196 normal 1/1、drop 1 1/1（`frag_complete=1`，restart 正常）。证据 `out/refmem_transport_diag/ackprio_exec196_202610241705/`、`ackprio_drop1_202610241705/`。
  - P3：build `20261024090754`，源码指纹 `b81745bc830f23ae1eaa9fa71ca6c336e10a0f7afae86869188b5f5aead5279d`（1930 files），`QUICK_DIAGNOSTIC`，credential written；step 26/33 warn，无 ERROR/FATAL。
- 下一步：补 N3.2.7 剩余矩阵（重复/乱序/窗口 miss/RX timeout/ring stop→start/mid-ring/长期 soak）。

### REFMEM-TASK-20261024-009 — N3.2.7 HIL 矩阵（部分）

- 状态：部分矩阵完成；重复/乱序/窗口 miss/RX timeout/ring stop→start/mid-ring/长期 soak 待补。
- 完成内容/证据：
  - 7 帧 exec196 1/1：`inject_ok=54`、三目标 `frag_complete=1`。`out/refmem_transport_diag/n327_normal_202610241700/`。
  - 单目标 `0x02`：源 `target_mask=2`、`required=2`、`ack_flags=2`、`state=ACKED`；COM3/6 仅重组不执行。`n327_single_202610241700/`。
  - `--drop 200`：无完成、源 `POSTED`、`tx_retries=8`、队列未排空，无假 ACK（fail-closed）。`n327_dropfail_202610241705/`。
  - soak 5/5（2 s 环窗口）：`inject_ok=54`、三目标 `frag_complete=1`。`n327_soak5b_202610241707/`。
  - 0.5 s 窗口 soak 2/5 未排空 → stop-and-wait 7 帧命令需 ≥2 s 环窗口。
  - 重复：raw56_x2 同一 wire 两次 → 三目标 `refmem_parser_ok=2`、`posted=1`、`acked=1`（第二次被去重）。`n327_dup_202610241730/`。
- 下一步：补重复/乱序/窗口 miss/RX timeout/ring stop→start/mid-ring/长期 soak。

### REFMEM-TASK-20261024-008 — N3.2.6 紧凑命令帧实验（负结果，延后）

- 状态：未完成；紧凑帧投递成功但目标 reply 未 arm；已回退，未入库。
- 实验：headerless 56 B codec wire（SELF_CHECK 2 维护帧）：
  - 投递/重组：`frag_ok=2`、三目标 `frag_complete=1`、源 `tx_ack_ok=2`、队列 2/2 排空。
  - 回复：目标 `CMD:REPLy?` `armed=0/published=0`；源 `CTRL:STATus?` `POSTED`；补 SELF_CHECK 参数后仍失败。
- 结论：目标执行/reply 触发依赖 v1 快照/信封的某个字段；需目标侧 `cmd_frame_calls/ok/apply_fail/target_miss/request_fail` 才能定段。作为尺寸优化延后，不阻塞可靠性主链；N3.2.8 收口时记录为已知偏差。

### REFMEM-TASK-20261024-007 — N3.2.5 单环多批命令

- 状态：16 槽队列 + 双命令单环验证通过；P3 凭证已写。
- 任务目标：一个环启动内执行 >1 批命令；队列不因第二条 7 帧命令而满；两批命令按序完成、不串 seq/回复。
- 完成内容：
  - `tdma_maintenance_runtime` TX 队列 8→16 槽（可用 15），可预排两条 7 帧命令。
  - HIL 新增 `multibatch`：环启动前排入两条 `REFMEM_MAINTENANCE_SELF_CHECK`，单环内按序执行。
- 验证结果：
  - `out/refmem_transport_diag/n325_multibatch3_202610241544/`：`submitted=2`、`queue_enqueued=14`、`queue_dequeued=14`、`queue_depth=0`、`queue_full=0`、`inject_ok=108`、`tx_ack_ok=14`、`tx_retries=0`；三目标 `frag_ok=14`/`frag_complete=2`。
  - 回归：exec196 1/1 仍 `frag_complete=1`；16 槽 queue_full 提交 18 条仍显式拒绝。
- 限制：真正的“环运行中再入队”需要与 trn03 并发的测试驱动；当前验证的是“一个环启动内执行 >1 批预排队命令”，mid-ring 并发留 N3.2.7。
- P3：build `20261024075123`，源码指纹 `2f05da00ffe6f061bd6ac8cd71215281649f8faccf976ea07a519711cfc77f41`（1930 files），`QUICK_DIAGNOSTIC`，`credential: written`；step 26/33 warn，无 ERROR/FATAL。
- 下一步：N3.2.6 帧尺寸/param_kind=ref。

### REFMEM-TASK-20261024-006 — N3.2.4 维护帧 ACK/有界重试

- 状态：帧级 stop-and-wait ACK + 有界重试完成；正常、丢 1 chunk 与 P3 均通过。
- 任务目标：维护帧丢失后不静默丢；TX 保留未 ACK 帧，目标回帧级 ACK，超时重传；重试 generation+1，目标按新 generation 重启重组。
- 完成内容：
  - `tdma_maintenance_runtime`：整帧发完后保留槽位并等待 ACK（`tx_awaiting_ack`）；50 ms 超时、最多 3 次、每次 generation+1；`tdma_maintenance_runtime_ack()` 只接受匹配 `stream_id:generation` 的 ACK。
  - 目标：`refmem_cmd_load_service` 在解析维护帧后 arm ACK；process-image builder 用 ACK 席位回 `stream_id:generation + result`（诊断字节 `0xA0|result`，flags 仅 VALID，不带 COMMAND），源端 `parse_mailbox` 识别并调用 runtime ACK。
  - 新增 `SYSTem:REFMEM:CMD:DROP <n>` 调试注入口，丢 chunk 触发重试；`CMD:STATus?` 追加 `tx_awaiting_ack/tx_ack_ok/tx_retries/tx_ack_fail/tx_dropped_chunks`。
- 验证结果：
  - 正常 exec196 2/2：`tx_ack_ok=7`、`tx_retries=0`、`tx_ack_fail=0`、三目标 `frag_complete=1`。证据 `out/refmem_transport_diag/n324_exec196_202610241433/`。
  - 丢 1 chunk 2/2：`tx_dropped_chunks=1`、`tx_retries=1`、`tx_ack_ok=7`、三目标 `frag_complete=1`、`rx_restarts=1`。证据 `out/refmem_transport_diag/n324_drop1_202610241435/`。
- 未覆盖：重复/乱序/窗口 miss 的完整矩阵（N3.2.7）；当前是整帧重传，不是 per-chunk 位图。
- P3：build `20261024071934`，源码指纹 `5a4c52f479cf8823b48e232e6dcb323536a01494c2f6580576ab106b33e57d84`（1930 files），`QUICK_DIAGNOSTIC`，`credential: written`；step 29 DPLL 最终通过（146.9s），step 26/33 warn；前三次 DPLL `recorder cancel did not complete` 判为台架/recorder 间歇，最终全量重跑恢复。
- 下一步：N3.2.5 多批命令；N3.2.7 HIL 矩阵（重复/乱序/窗口 miss/RX timeout/soak）。

### REFMEM-TASK-20261024-005 — N3.2.3 RX 新帧重启与唯一 chunk 计数

- 状态：RX 重启/计数完成；host 用例通过；exec196 6/6 成功；P3 凭证已写。
- 任务目标：一次丢 chunk 不得永久 wedge RX；区分唯一/重复 chunk；验证新 sequence + 每周期 1 chunk。
- 完成内容：
  - `tdma_maintenance_stream_rx_chunk` header 不一致时放弃未完成旧帧、重启新帧（此前永久 `return false`）；新增 `restart_count`/`duplicate_count`。
  - runtime `rx_restarts`/`rx_duplicate_chunks` 接入 `SYSTem:REFMEM:CMD:STATus?`（再追加 2 字段）。
  - host `test_tdma_maintenance_stream` 新增“旧帧未完成 + 新帧头重启/重复 chunk”用例。
- 验证结果：
  - host `run_tdma_maintenance_stream_tests.ps1` PASS。
  - HIL `out/refmem_transport_diag/n323_exec196_202610241355/`：6/6 exec196、3 目标 `frag_complete=1`；源 54/54 launched、new sequence 54、repeated 0、dropped 0。
  - N3.2.2 的 COM3 H4 wedge 未再复现；若发生丢帧，新逻辑会 restart 而非永久拒绝，但整帧丢失仍需 N3.2.4 重试。
  - P3：build `20261024060410`，源码指纹 `b344beda5d5304088ec70e63ad0597e9bd70760bd24c7698d7d64c12d5bb77ac`（1930 files），`QUICK_DIAGNOSTIC`，`credential: written`；step 8 首次拓扑失败后重试通过，step 26/33 warn，无 ERROR/FATAL。
- 下一步：N3.2.4 per-stream ACK/位图 + 有界重试。

### REFMEM-TASK-20261024-004 — N3.2.2 维护流 TX 队列 SPSC 交接

- 状态：队列交接/满队列背压完成；H4（RX 重组阻塞）在本轮 exec196 复现，移交 N3.2.3。
- 任务目标：core0 producer / core1 consumer 的 8 帧维护队列改为 release/acquire；满队列显式拒绝；修正“在途帧槽位被覆盖”的时序缺陷。
- 完成内容：
  - head/tail 改 SPSC，producer release tail、consumer acquire tail / release head；移除共享 count。
  - 槽位只在帧完全发完后释放（此前 `tx_begin` 即推进 head，producer 可覆盖仍在发送的槽）。
  - 新增 `tdma_spsc_index.h`（next/empty/full/depth）与 `run_tdma_spsc_index_tests.ps1`。
  - `queue_full`/`dequeued`/`depth` 计数接入 `SYSTem:REFMEM:CMD:STATus?`。
- 验证结果：
  - host `run_tdma_spsc_index_tests.ps1` 通过；RefMem host 全量仍全绿。
  - HIL `out/refmem_transport_diag/n322_202610241327/`：queue_full 9 次提交 → 7 `QUEUED` + 2 拒绝，`queue_full=2`、depth=0；raw16/raw56/raw56_x2 成功。
  - exec196：COM4/COM6 `frag_complete=1`，COM3 `frag_ok=2/frag_complete=0`；源 54/54 launched、dropped/repeated=0 → H4（RX 旧帧阻塞），移交 N3.2.3。
  - P3：build `20261024053749`，源码指纹 `f8e29d0c8981b7b0245c94c215327e2e4f0d00d6a12148abf958ee4a25b06821`（1930 files），`QUICK_DIAGNOSTIC`，`credential: written`；step 26/33 warn，无 ERROR/FATAL。
- 下一步：N3.2.3 RX 新帧重启 + 唯一/重复 chunk 计数 + 注入配额。

### REFMEM-TASK-20261024-003 — N3.2.1 维护流诊断计数与 1/2/7 帧定位

- 状态：诊断计数与四板 HIL 完成；H1–H4 有初步结论；N3.2.2/N3.2.3 待做。
- 任务目标：只加只读计数，定位 196 B/7 维护帧命令的丢帧阶段，不改传输语义。
- 完成内容：
  - `tdma_maintenance_runtime` 新增队列深度/enqueued/dequeued/full、`rx_chunk_rejects` 计数与 `get_diag()`。
  - `tdma_pio_spi_ring_adapter` 新增 inject launched/dropped_unlaunched、new/repeated sequence、beacon_frames、rx polls/seen/accepted/dedup/bad mailbox/last sequence 计数与 `get_maintenance_diag()`。
  - `SYSTem:REFMEM:CMD:STATus?` 末尾追加 18 个只读诊断字段；新增 `tools/refmem/refmem_transport_diag_hil`，覆盖 raw16（1 帧/2 chunk）、raw56（1 帧/6 chunk）、raw56_x2（2 帧/12 chunk）、exec196（7 帧/54 chunk）。
- 验证结果（build `20261024030428`）：
  - 1/2/7 帧应用层均成功：raw16 `t2_static_ok=1`；raw56 `refmem_parser_ok=1`；raw56_x2 `refmem_parser_ok=2`；exec196 `frag_ok=7`/`frag_complete=1`（三目标一致）。
  - exec196 连续 6 次加前序 1 次全部 `frag_complete=1`、源 `CTRL:STATus? state=ACKED`；源 `inject_ok=54`、`inject_launched=54`、`inject_dropped_unlaunched=0`、`inject_new_sequence=54`、`inject_repeated_sequence=0`、`queue_depth=0`、`queue_full=0`。
  - `rx_accepted_chunks`/`rx_dedup_sequence`/`rx_chunk_rejects` 含重复投递噪声（同一 mailbox 在未注入新 chunk 的帧上重复出现），不能直接当唯一 chunk 丢失；交付以 `frag_complete`/parser 计数为准。
- 结论：H1（源侧 sequence 复用）未出现；H2/H3（注入未发射/队列不排空）未出现；H4（重组器阻塞）未造成应用层失败；原 `rx_seen=2` 失败未在 10 次 7 帧运行中复现，倾向启动时序/历史状态的偶发问题，需 N3.2.2/N3.2.3 用唯一 chunk 计数与故障注入继续收敛。
- 证据：`out/refmem_transport_diag/n321_202610241248/`、`out/refmem_transport_diag/n321_exec196_202610241257/`。
- P3：build `20261024050910`，源码指纹 `212e893f6ed10505ad5ceb2b4d4f17ceeebbd8475efe93bb4ed7adcf7ab973f1`（1927 files），`QUICK_DIAGNOSTIC`，`credential: written`；step 26/33 为 warn，无 ERROR/FATAL。
- 下一步：N3.2.2 队列 release/acquire；N3.2.3 唯一 chunk 计数 + 注入配额/序列。

### REFMEM-TASK-20261024-002 — 统一控制面命令回复回程（C1b 方向）

- 状态：代码、构建、四板 HIL（单目标/多目标/负向）与 P3 硬件验收完成；文档收口进行中。
- 任务目标：
  - 目标执行 `REFMEM_CMD` 后把 ACK/NACK 终态回传源板，源板可读聚合结果；不新增第二命令槽、第二 ACK 事实、payload class 或物理总线。
- 完成内容：
  - 复用 process-image ACK 席位：新增 `TDMA_PROCESS_IMAGE_ACK_FLAG_COMMAND`（bit3），`seq16 = command_seq & 0xFFFF`，`reason` 走可选诊断字节；目标连续发布 4 个普通镜像，位置/VDC/feedback 一等席优先，回复不抢占。
  - 新增 `refmem_cmd_reply.h/.c`（编码/解码 + 逐目标聚合）与 host 用例；源板仅在 `source_node == local` 时聚合，写回源 node 控制记录的 `state/ack_flags/nack_flags/last_reason`，不写其他板记录。
  - 新增 `SYSTem:REFMEM:CMD:REPLy?`：`rx/applied/stale/bad_target/duplicate/armed/published/pending` 计数 + 聚合快照。
  - 命令目录新增 `REFTMEM_MAINTENANCE_SELF_CHECK`（id=4，`command_type=MAINTENANCE`，1 参数），作为 RUN 中可成功执行的控制面探针；`EXECute` 远端路径写源 node 记录，供聚合使用。
  - 聚合规则修正：required 任一 NACK 立即判失败，但仍继续折叠回复直到所有 required 目标都有终态，避免只记录第一个 NACK。
- 验证结果：
  - host：`run_refmem_cmd_reply_tests.ps1` 通过；`run_refmem_command_catalog_tests.ps1` 更新到 4 条后通过；`run_refmem_all_host_tests.ps1` 全绿（`out/refmem/host_tests_20261024_r3.log`）。
  - 构建/刷写：P3 build `20261024035648`；四板 OTA 成功（`out/HardwareAcceptance/20261024/hawf-115636/ota-four-board/`）。
  - 单目标 HIL：COM5→COM4 `SELF_CHECK,0x02,0x02`；COM4 `state=ACKED`、`ack_flags=2`；COM5 `state=ACKED`、`ack_flags=2`，`CMD:REPLy? rx_count=4/applied_count=1`。证据 `out/refmem_cmd_reply/p3build_single_try1/`。
  - 多目标 HIL：COM5→COM4/3/6；源板 `ACKED`、`ack_flags=0x0E`、`applied_count=3`、`rx_count=12`；目标记录保留全局 `required_mask` 并置自身 ack 位（`TAKEN`），完成态以源板聚合为准。证据 `out/refmem_cmd_reply/p3build_multi_try1/`。
  - 负向 HIL：RUN 中 `LOAD_NODE` 三目标 `NACKED reason=13`；源板 `NACKED`、`nack_flags=0x0E`、`reason=13`、`applied_count=3`。证据 `out/refmem_cmd_reply/p3build_nack_try1/`。
  - P3 硬件验收：build `20261024035648`，源码指纹 `23690f53cf5fcdcee47c0bb65ce3897de7e476ea034f9db35b6e4a782a9d3a31`（1926 files），`QUICK_DIAGNOSTIC` scope，`credential: written`；receipt `config/hardware_acceptance/p3_acceptance_receipt.json`，`check-staged` 通过；step 26 TDMA 与 step 33 时间预算为 warn，无 ERROR/FATAL。
- 已知偏差/未闭合：
  - 回复只带低 16 位 `seq16`；聚合不广播给其他 observer；node 控制记录未全环复制；静态命令槽仍是本地执行边；协调者租约、非协调者入口转发未实现。
  - fragmented `EXECute`（约 7 个维护帧）在环启动窗口存在偶发投递失败；最终三组 HIL 均首跑通过，但前序诊断跑出现过启动窗口丢帧，归入 N3.2 P0 可靠性，不把重试当作可靠语义。
- 下一步：P3 firmware 验收 → 文档/C14 收口 → N3.3 租约 → N4 idle 窗口。

### REFMEM-TASK-20261023-001 — C0/C1a 收口：命令槽放置冻结 + REFTMEM_CMD 全目标投递证据

- 状态：完成 C0 决策与 C1a HIL 证据；未改代码。等待 C11 独立复核。
- C0：规范物理位置冻结为 `distributed_refmem.c` 的 static `s_refmem_command_slot`；`ack_command` 预留区仅目录注册，不承载 live slot。已同步 `REFMEM-CTRL-01` 与 P8 C0。
- C1a（build `20261023121304`，`target_mask=0x0F`）：`REFTMEM_CMD` 从参考板 COM5 SCPI 入队 → TDMA 维护流 → COM4/COM3/COM6 每板 `rx_events/decoded/posted/acked=1`，源 `submitted/served/tx_ok=1`、`rejected=0`。
- 解析器：0x52/0x53/0x54 三类全过（0x53 `t2_static_applied=1`）；拒绝套件坏 CRC/坏 magic/坏 reserved 均按对应 parser 拒绝。
- 证据：`out/refmem_cmd_tdma/closeout_c0c1/`、`closeout_parser_suite/`、`closeout_reject_suite/`；评审提交单 `docs/check/submissions/REFMEM_UNIFIED_CONTROL_REVIEW_03.md`。
- 未覆盖：C1 非协调者 Entry 转发、C4/C5、C2/C3、聚合视图；五项契约保持 pending。
- 下一步：C11 独立复核 C0/C1a；之后按 C1 转发 → C2 负测 → C3 租约推进。
- 追加（2026-10-23，T2_DYNAMIC 执行闭环）：解析器在 envelope 校验后调用 `tdma_t2_dynamic_apply_fn`；默认 executor 记录 `t2_dynamic_applied/last_target/last_run/last_config/last_value`，可用 `tdma_t2_dynamic_set_apply()` 替换为真实 T2 运行时；`REFTMEM:CMD:STATus?` 追加 5 字段。HIL（build `20261023131748`，`target_mask=0x0F`）三目标均 `t2_dynamic_ok=1`、`t2_dynamic_applied=1`，记录字段 `15/42/7/9` 与发送 envelope 一致；证据 `out/refmem_cmd_tdma/t2_dynamic_closeout/`。
- C1 转发实验（2026-10-23）：以 COM4（从板）为 Entry、`target_mask=0x0C` 提交 `REFTMEM_CMD`，源端 `submitted/served/tx_ok=1`，但 COM3/COM6 `rx_events=0`、源端 `inject_attempts=0`。原因：从板转发走 PIO 直通（`PHYSICAL_FLIGHT`/`PHYSICAL_PROCESS_IMAGE`），软件 TX 路径不参与，维护流没有上行注入点。⇒ 非协调者 Entry 需要 process-overlay 注入或专用 uplink mailbox（或把入口收敛到协调者）；C1 保持未完成，本轮未保留无效代码。证据 `out/refmem_cmd_tdma/c1_entry_com4/`。
- C0 方向修订（2026-10-23）：RefMem 是对称复制黑板，任意板写入 REFMEM 的数据同步到其他板；统一控制面 = 修改 REFMEM 控制所有板、观测 REFMEM 快照观测所有板。命令槽改由 REFMEM 复制区承载，static `s_refmem_command_slot` 仅作迁移前载体；`--no-t2` 纯直通环验证不了从板写，后续 HIL 用启用 flight engine / process-image 的环。原“static 冻结”结论作废。
- C0 容量扩展（2026-10-23，build `20261023144347`）：node slot 128→256 B（8 槽 2 KB）、表 18,432→19,456 B；node slot 增加控制块（`refmem_command_snapshot_t` 108 B + seq/CRC + rx/tx/reject/last_error）。为容纳 +1 KB，`storage_manager` 文件写缓冲 16→12 KB（3×4 KB）；TDMA 帧/槽未动，RAM 余量 3,376 B。`refmem_all_host_tests` 全过；P3 `hawf-224329` step 8/29 OK、`credential: written`（step 29 仍用一次重试，step 8 为 P0T 瞬时 flake，手动复测通过）。
- C0 命令路径（2026-10-23，build `20261023152319`）：`refmem_node_control` 写/读/ACK/清除（seq+CRC）落地；AO 在 post/take/ack/nack/timeout/clear 后镜像 command slot 到本板 node slot，`refmem_cmd_load_submit` 发送侧也写本板记录；新增 `SYSTem:REFMEM:NODE:CONTrol?` 只读快照。四板 HIL（`c0_node_control_r2`）：源 COM5 `POSTED`、required 目标 COM4 `ACKED`、COM3/COM6 `TAKEN` 并记录 ACK 位；P3 `hawf-232301` step 8/29/30 OK、`credential: written`（step 29 一次通过）。未完成：跨板复制该记录与从板写入口。
- 夜间 N0/N1/N2.3（2026-10-24，build `20261023164831`）：命令目录 3 条试点 + `SYSTem:CTRL:EXECute/STATus?/CATalog?` + 本地分发（LOAD_NODE ACKED、LOAD_BOARD 负路径）+ `SYSTem:REFMEM:NODES?`（本板 stale=0、远端 stale=1）；工作区未提交，等批量 P3。
- 夜间 N2.4/N2.5/N3.1（2026-10-24，build `20261023172516`）：参数池绑定 TLV；请求/状态分离（`update_status`/`apply_request`）；命令帧 `refmem_cmd_frame`（snapshot+params）build/apply；`EXECute` 帧 loopback 验证 QUEUED→ACKED；SCPI 全量目录 829 条（386 setter）。
- 夜间 N3.2（2026-10-24，build `20261023175156`）：分片传输（9 B 记录/0x55 解析/节流）落地；本地帧回环 HIL 通过；带环远端 HIL 未通过（参考板 QUEUED，目标无记录），待诊断维护 TX/AO 服务节奏。
- 夜间 N3.2 传输（2026-10-24，build `20261024022034`）：维护流 8 帧 TX 队列/分片打包/动态 local slot/消费门禁修正；远端 frame→apply→consume→dispatch 打通（COM4 NACK reason=13 为 RUN 配置门禁）。

### REFMEM-TASK-20261021-005 — HAOFV 偏差审查

- 状态：完成条款修正；未改代码。
- 修正：预算字段、config-control subtype、session writer、ActionDispatcher 归属、本地/协调者边界、实时性。
- 验证：`docs_check` 229/0；`doc_regression` 48。
- 下一步：C11 独立复核。

### REFMEM-TASK-20261021-004 — 四条款复审 R2

- 状态：完成条款复审与修正；未改代码。
- 修正：ARMING 失败 DISARM；STOPPING 超时进 DEGRADED/FAULT；未注册 command_type 拒绝；mask 解析失败不回退本地。
- 文档：`docs/check/submissions/REFMEM_UNIFIED_CONTROL_REVIEW_02.md`。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（228 文件、0 warning）；`python tools/doc_regression_check.py` 通过（48 契约、19 进展日志）。
- 下一步：C11 独立复核；仍不改代码。

### REFMEM-TASK-20261021-003 — 单板控制保留条款

- 状态：完成 `REFMEM-LOCAL-01` 文档/契约；未改代码。
- 内容：
  - RefMem 统一面是增量层，不替换单板 SCPI、本地 command slot、本地 owner、本地 T2 `MOUNT/UNMOUNT`。
  - 本地 target 不走 `REFMEM_CMD`；统一面不可用时本地仍可用；远端/全目标必须拒绝 `UNIFIED_UNAVAILABLE`，不得静默缩成单板。
  - 两条路径共享同一本地门禁；EventPack 未激活不阻断本地基础命令。
- 契约：`REFMEM-LOCAL-01` pending；登记表/顶层/Review 01 §9 已同步。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（227 文件、0 warning）；`python tools/doc_regression_check.py` 通过（48 契约、19 进展日志）。
- 下一步：独立复核四条条款；仍不改代码。

### REFMEM-TASK-20261021-002 — 三缺口条款审查与修正

- 状态：完成条款级审查与修正；未修改固件/SCPI，未做板端验证。
- 结论：`PASS_WITH_CHANGES`；V-B1–V-B4 已改，V-M4（C11）仍开放。
- 修正：
  - `REFMEM-TRANSPORT-01`：active profile 必须含 config-control；缺失返回 `TRANSPORT_UNAVAILABLE`；`REFMEM_CMD_MAX_PAYLOAD` 绑定结构体上限。
  - `REFMEM-SESSION-01`：增加 `ARMING`；required 未全 arm 不得 START；START 后失败进入 `DEGRADED`；`T2:MOUNT` 仅兼容模式一次性映射。
  - `REFMEM-ACTION-01`：增加 `CommandHandlerRegistry`；有界 mailbox/优先级/BUSY；`action_registry_crc` 进 Version Bundle。
- 文档：`docs/check/submissions/REFMEM_UNIFIED_CONTROL_REVIEW_01.md`；索引已更新。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（227 文件、0 warning）；`python tools/doc_regression_check.py` 通过（47 契约、19 进展日志）。
- 下一步：独立复核三条条款；仍不改代码。

### REFMEM-TASK-20261021-001 — 单端口统一控制面三缺口 canonical 补齐

- 状态：完成架构/契约文档补齐；未修改固件、SCPI、工具或测试，未执行板端验证。
- 目标：只补三个 canonical 缺口——`REFMEM-TRANSPORT-01`、`REFMEM-SESSION-01`、`REFMEM-ACTION-01`，不改代码。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 新增三个同名小节：`REFMEM_CMD` 复用 TDMA config-control；系统级 `TriggerSession`（CONFIGURE/ARM/START/STOP/ABORT + 状态机 + T2 MOUNT 映射）；`ActionRegistry`/`ActionDispatcher`（action_id→handler、post、缺 handler gate）。
  - 命令类型表加入 `TRIGGER_SESSION_*`。
  - `TDMA_DOMAIN_ARCHITECTURE.md` 增加 `REFMEM-TRANSPORT-01` 载体条款；`TDMA_DOMAIN_TODO.md` 增加文档联审项。
  - `DOCS_REGISTRY.md` 登记三条 pending 契约；顶层 `HAOFV_ARCHITECTURE.md` 增加三缺口摘要。
  - `REFMEM_DOMAIN_TODO.md` 新增 M0 文档补齐清单；M1–M5 标为实现阶段、不在本目标。
- 边界：三条契约均 pending，未实现、未联审；不新增第二命令槽、不接 POSITION、不让 Event Plane 执行实时边沿。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（226 文件、0 warning）；`python tools/doc_regression_check.py` 通过（47 契约、19 进展日志）。
- 下一步：M0.5 独立复核三条条款；复核通过后仍不写代码。

### REFMEM-TASK-20261020-001 — 统一控制面契约细化与架构落位

- 状态：完成文档契约细化与登记；未修改固件、SCPI 或工具实现，未执行板端验证。
- 目标：把“通过任意一个板卡接口操作 RefMem、实现所有板卡的操作”从报告层愿景收敛为 RefMem 域可验证契约，并明确接入点无关、目标本地执行、显式掩码、单一协调者和聚合完成的边界。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 新增 `REFMEM-CTRL-01` 章节：CP1–CP8 不变量、Control Entry/Coordinator/Target/Observer 角色、命令信封既有字段语义与扩展字段、路由与命令状态机、fence/超时/部分成功、聚合与逐目标读模型、权限与运行态矩阵、失败恢复、边界、C0–C5 阶段和当前实现状态。
  - 架构章节补充“挂接关系：控制面接入 RefMem”：命令面、观测面、目录面三个挂接面；RefMem 是控制面的 bearer/router，不是执行者；不另建控制总线、第二套命令槽或第二套 ACK 事实。
  - 记录当前单板命令面物理放置：live slot 为 `distributed_refmem.c` 的 static `s_refmem_command_slot`，`refmem_vector_table_t.ack_command`（`DISTRIBUTED_REFMEM_ACK_CMD_SIZE`）仅目录注册；C0 需冻结“迁入该区”或“static 为规范位置”。
  - `DOCS_REGISTRY.md` 登记 `REFMEM-CTRL-01`（pending），`clause_loc` 指向架构章节，`code_anchor` 为 `refmem_command.h`。
  - 顶层 `HAOFV_ARCHITECTURE.md` 同日刷新：RefMem 定位补“统一控制面”，核心规则补接入点无关/目标本地执行/无强制位一条。
  - `REFMEM_DOMAIN_TODO.md` 新增 P8 统一控制面十项实现清单。
- 实现边界：跨节点命令路由、协调者租约、聚合视图、跨节点 FENCE 均未实现；当前只有本地 command slot 与 `SYSTem:COMMand:ACK?`/`NACK?`。本条不授予“任意接口控制全部设备”已产品化的结论。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（214 文件、0 warning）；`python tools/doc_regression_check.py` 通过（44 契约、10 顶层条款落点、18 进展日志）；`FORCE_DOC_GATES=1 sh .githooks/pre-commit` 通过。`python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider` 为 36 passed / 2 failed，失败项是 `TDMA_TASK_PROGRESS.md` 轮转债务和 `SYNC_IO_TASK_PROGRESS.md` 顺序债务在 2026-10-17 截止后的既有时间炸弹，与本次改动无关，未在本切片修复。
- 下一步：按 P8 顺序先冻结 C0 schema 与目录 CRC，再做 C1 两板 Entry→Coordinator→Target HIL。

### REFMEM-TASK-20260916-001 — 静态向量缩容与布局版本迁移

- 对应 `REFMEM-RAM-001/002/003/004`。范围为静态预留缩容、布局版本迁移和必要的 host 工具联动；保留区域 ID/顺序、owner、整表 clear、VDC/DPLL payload 及 seqlock、node 现有临界区。不改 OTA、PIO/DMA、TDMA 实时服务或 MPU，不引入 lease/TLV 解析前置。
- 实现快照，非容量契约：表从 65536 B 缩至 18432 B，布局版本升级为 2；node 固定 8 个逻辑槽，每槽 128 B。ARM 有效占用为 HEADER 264 B、NODE 每槽 36 B、VDC 含同步前缀 232 B、DPLL 224 B，均适配新分配。其余普通数组未发现实际 backing 字节写者，应用模型中的区域引用保留；不将这次缩容解释为对应业务摘要已实现。
- 生产验证：节点容量 2 至 8 配置及兼容反例共 17 项通过，既有 VDC vector suite 在 6/8 节点通过，vector table 的 ARM 6/8 编译通过。`production-delivery.json`、`capacity-probe.json` 和 `node-capacity.xml` 绑定修改及取证。
- A/B Release 与 Flash 链接检查通过，build `20260916151903`。相对冻结的 VDC r5，两槽实际静态 RAM 均净释放 47104 B；保留原 2 KiB heap 后余量从 28 B 增至 47132 B，SCRATCH_X 间隙仍为 24 B。新二进制独审重新计算栈，保持原限定四板 persona 的最深余量；不代表所有可选负载安全或原运输/调度失败已修复。见 `measured-build/`、`ram-comparison.json`、`independent-review/linked-shrink-review.json`。
- 布局 ABI 同时影响 Python 产包器和 CORE:VECTOR/PROT:STATUS 读回版本判定；RMTP 格式、VDC/DPLL payload 及 TDMA wire 的独立版本保持。实际 Python 包进入生产 owner 链时发现既有 TDMA ring CRC 算法漂移，原失败保存在 `host-layout-r1.xml`、`host-diagnose-r1/`；同步算法后继续验证旧布局正确 CRC 包拒绝及 active/rollback 内容保持，不以坏 CRC 反例冒充布局验证。
- 工具/兼容矩阵最终 25/25 通过，独立复跑相关测试 22/22 通过，源码指纹一致。真实 CLI 包在 6/8 节点及普通/short-enums 编译条件下进入生产 owner、stage、prepare、activate、commit；正确 CRC 的旧布局包由 ApplicationMap owner 拒绝。纯 validation 保留三角色，失败 stage 可按原语义清 staging，但 active/rollbackable 内容及 descriptor 不变。见 `tool-test-delivery.json`、`host-layout-r2.xml`、`independent-review/joint-delivery-review.json`。
- 当前六节点固件产包须显式使用 `refmem_pack_build --tdma-node-count 6`；RefMem 仍保留八个逻辑槽。`sd_fs_build` 仍使用默认八节点产包，本轮不扩展其部署能力；后续需要六节点 SD 整包时另做工具切片。
- `REFMEM-LAYOUT-01` 已在域 canonical 定义并登记 pending，顶层同日可见；独立 C11 接受该规范/登记，不代表全部硬件门禁通过。
- 四板 OTA 更新成功，板端 UID/build 均与目标匹配；全部 STOP 后 RefMem 专项通过：容量 18432 B、布局 2、八逻辑槽、初始化 READY/error=0、目录有效/CRC 标志、CORE:VECTOR/PROT:STATUS 版本和八槽逐项查询均正常，错误队列为空。原件为 `baseline-refmem.json`、`refmem-readback.json`、`stopped-diagnostics.json`；完整目录字节未通过硬件接口导出，目录 offset/size 由 host 与 ARM 结构检查证明。
- 当前源码 quick P3 已实际执行，`flow_completed=true`，但 `strict_gates_passed=false`，`closed_loop_passed=false`、`realtime_gate_passed=false`。仍报 `explicit startup barrier timed out`；启动观测曾出现一次健康样本，随后再次失效，未满足稳定窗口；持续运输和 VDC/RefMem 调度门禁也未通过。该结果与缩容前 r5 同属运输/调度未闭合，不能推断 RAM 改动已修复或导致全部时序问题。见 `p3-r1/acceptance.json`、`p3-r1/tdma-process-image/summary.json`。
- 本轮 P3 绑定源码 `0593b3ace86d5895184b1cf98ff7aeba5d3cc6bd829efc90df0550ea16db818c`、build `20260916151903`，包 SHA-256 为 `3be05d080112b40cd33549322af3408549bdecd48f57b385efe99480c21d9222`，与部署前 manifest 一致。随后验收分级工具完成并重新通过匹配源码四板 quick P3，代码已于 2026-09-17 提交为 `e6ff06a`，提交指纹及证据见 `VDC-PROGRESS-20260917-001`；诊断凭证不提升为严格通过。按下述验收范围澄清，`REFMEM-RAM-001/002/003/004` 均已完成本轮调试切片验收；下一步回到 `VDC-FAST-002` 处理运输/调度。
- 独立硬件复核通过指纹、全部 receipt 引用哈希、冻结产物及四份 native 记录重解一致性。观测窗口快照，非性能保证：NO1–NO4 的 missing 增量相对 r5 从 `3/2/4/3` 变为 `0/1/0/1`，NO3 node/soak 门禁通过；NO1 反馈 gap 仍为 3，NO2/NO4 仍有缺收。稳态 soak 未见 DOWN，但启动 sample 3 有 NO4 DOWN/invalid/failure，sample 5 短暂健康后 sample 6 再失败，最终 2.006283 s 超时。四板 VDC deadline 仍增长，部分从板 RefMem deadline 仍增长，不能将单次变化归因为缩容的确定性性能收益。见 `independent-review/hardware-shrink-review.json`。
- 本轮证据目录：`out/HardwareAcceptance/20260916/refmem-static-shrink-r1/`。缩容前运输与调度失败基线见 `VDC-PROGRESS-20260916-042`，后续结果须保留差分。RAM 切片验收后返回 `VDC-FAST-002`，不以释放 RAM 代替 DPLL 闭环。
- 验收范围澄清：用户指出范围不得随调试无限扩大。主控复核 r5 与本轮的配置 SHA-256 均为 `c032843ba0eab2e87bb3399920c0fe54f79cf1eddbb4404a7ed1d85b1ea34fc2`，均为 `QUICK_DIAGNOSTIC`，启动及窗口条件相同；本轮没有扩大 P3 脚本范围。`p3_hardware_acceptance.py::_validate_quick_diagnostic_receipt` 明确允许流程完成且保留 phase quality 失败，`check_staged` 支持该凭证类型。先前将 `strict_gates_passed=false` 解释为 RAM004 不能结项，是主控扩大退出条件，现已纠正；原始失败、凭证和源码不改。提交时仍须核验真实 staged 指纹，本条不代替提交门禁。

### REFMEM-TASK-20260817-052 - RefMem 初始化阶段码固化

- 状态：完成代码、接口文档、构建和 COM5/COM6 板端验证。
- 日期：2026-08-17
- 任务目标：
  - 固化 `distributed_refmem_init()` 的初始化阶段码，避免 HIL 闭环失败时只能看到 `core1=0` 或 `service_count=0`。
  - 保持 `SYSTem:REFMEM:STATus?` 既有前 8 个字段不变，追加 `initialized,init_stage,init_error` 作为永久诊断字段。
- 完成内容：
  - `distributed_refmem_status_t` 增加公开 `DISTRIBUTED_REFMEM_INIT_STAGE_*` 枚举，覆盖 app model、TDMA owner、command slot、NodeLoad AUTO、TDMA ops、TDMA profile、vector table 和 ready。
  - `distributed_refmem_init()` 的临时数字阶段替换为固定枚举；失败时 `init_error` 记录失败阶段，成功时 `init_stage=READY`、`init_error=0`。
  - `distributed_refmem_publish_status_locked()` 每次刷新时同步 `initialized`，避免 service 后状态字段漂移。
  - `SCPI_COMMANDS.md` 同步 `SYSTem:REFMEM:STATus?` 的 11 字段返回格式。
- 验证结果：
  - `run_tdma_profile_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过。
  - `run_tdma_traffic_scheduler_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260817104554`。
  - COM5 经 BOOTSEL factory UF2 恢复到 build `20260817104554` 后，`SYSTem:REFMEM:STATus?` 返回 `65536,1,97043,0,8,97042,97042,7,1,8,0`，末尾 `initialized,init_stage,init_error=1,8,0`。
  - COM6 经 OTA boot/commit 到 build `20260817104554` 后，`SYSTem:REFMEM:STATus?` 返回 `65536,1,22775,0,8,22774,22774,7,1,8,0`，末尾 `initialized,init_stage,init_error=1,8,0`。
  - 两板 `SYSTem:ERRor?` 均返回 `0,"No error"`。
- 后续动作：
  - 初始化阶段码已可用于后续定位；若再次出现 `core1_loop_count=0`、RefMem service 不递增或 `init_error!=0`，优先按 `init_stage/init_error` 映射到 app model、TDMA owner、NodeLoad AUTO、TDMA profile 或 vector table 阶段，而不是继续盲目扩大 heap。

### REFMEM-TASK-20260817-051 - 事件驱动局部同步约束冻结

- 状态：完成架构与待办细化；通用 dirty queue 和 per-target generation completion 尚未实现。
- 日期：2026-08-17
- 任务目标：
  - 明确 64 KB RefMem 不按周期整表广播，避免同步负载破坏 VDC/TDMA 实时性。
  - 将触发、排队、局部更新、ACK/fence 清 dirty 的生命周期冻结为产品契约。
- 完成内容：
  - 正常同步改为 owner fact commit 触发 dirty descriptor，critical delta 进入 TDMA short frame。
  - 状态事实允许在编码前按 slot/field 合并；命令、事件和累加量必须使用有界队列或 accumulate 语义。
  - dirty 清除绑定 per-target ACK/fence 和 generation，禁止 `transport_sent` 后提前清除，也禁止旧 ACK 清除新更新。
  - full snapshot 仅用于 bootstrap/resync/显式维护，必须走 long-frame 分片、CRC 和 fence，不允许周期整表刷新。
- 验证结果：
  - 本项为架构约束细化；当前已有 delta/dirty mask/frame/ACK/fence 基础契约，但尚无通用 dirty queue 的新增代码验证。
- 后续动作：
  - 在 TDMA PIO ring adapter 接通后，实现 dirty descriptor queue、critical/background 分级和 per-target generation completion。

### REFMEM-TASK-20260816-050 - NodeLoad AUTO HIL default adapter profile correction

- 状态：完成工具修复并通过 COM5/COM6 默认 HIL。
- 日期：2026-08-16
- 任务目标：
  - 修复 `refmem_node_load_auto_hil_validate.py` 默认路径误用 `REALtime:IO` 线序预检推导 RefMem TDMA adapter 的问题。
  - 保持 HAOFV 边界：GPIO16-24 是 TDMA/RefMem 通讯环路；GPIO4-7 和 `REALtime:IO` 是业务/观测 overlay，不能作为 RefMem transport 默认推导来源。
- 完成内容：
  - 脚本新增 `--adapter-profile`，默认 `min-system-gpio16-24`，直接使用当前两板最小系统已验证的 3-wire uplink/downlink adapter：A `uplink=16,18,23`、A `downlink=16,22,23`，B `uplink=16,18,23`、B `downlink=16,21,23`。
  - `io-preflight` 改为显式 profile，只用于 REALtime IO overlay 线序诊断；该路径失败时会写出 `io_preflight_result.json`，避免只返回一句错误。
  - 修复脚本生命周期问题：IO preflight 子进程增加超时，轮询等待拆分为“单条 SCPI 查询 timeout”和“同步总 timeout”，关键输出立即 flush，避免超时后残留 Python 进程占住 COM 口。
- 验证结果：
  - `python -m py_compile tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py` 通过。
  - `python tools\two_board_io_validate\two_board_io_validate.py --port-a COM5 --port-b COM6 --name-a A --name-b B --expect-a-to-b auto --expect-b-to-a auto --out-dir build-rtos-multicore-smoke\debug_refmem_io_preflight_manual --timeout 2` 快速失败，证实 REALtime IO overlay 当前不等同于 RefMem TDMA 通讯环路。
  - `python tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py --port-a COM5 --port-b COM6 --timeout-s 2 --sync-timeout-s 30 --maintenance-s 0.5` 默认参数通过，记录位于 `build-rtos-multicore-smoke\refmem_node_load_auto_hil_20260816235020\records.json`。
- 后续动作：
  - 继续 P0 的 ACK/重发/fence completion；当前 maintenance 阶段仍可在 TDMA snapshot 中观察到 `WINDOW_MISSED` 质量证据，不能把“重跑通过”替代产品级可靠性。

### REFMEM-TASK-20260816-049 - RefMem payload standard TDMA registration

- 状态：完成代码、host/build 验证和 COM5/COM6 AUTO HIL 回归。
- 日期：2026-08-16
- 任务目标：
  - RefMem 作为公共 TDMA payload producer/consumer 接入，不能在 `refmem_realtime_tdma` 兼容适配层里临时声明 binding。
  - 保持 HAOFV 边界：`DistributedRefMemAO` 负责 RefMem payload contract，`components/tdma` 只执行统一 scheduler/transport/timestamp spine。
- 完成内容：
  - 新增 `refmem_tdma_payload.h/.c`，集中注册 `REFMEM_DELTA` 和 `REFMEM_ACK_FENCE` short-frame payload binding。
  - `refmem_realtime_tdma_init()` 改为调用 `refmem_tdma_payload_register()`；自身继续只做旧 API 到公共 `tdma_service` 的兼容适配。
  - `tdma_service` submit 阶段校验 `frame_class/payload_class/max_payload_size`，未注册 payload 不允许进入 core1 intent mailbox。
  - RefMem host 单测增加标准注册断言：`REFMEM_DELTA` / `REFMEM_ACK_FENCE` 可提交，未注册 VDC payload 在 RefMem-only TDMA service 中被拒绝。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260816103607`。
  - COM5/COM6 多线程 OTA 到 `20260816103607` 通过。
  - 首次 AUTO HIL 运行 A->B 通过、B->A 只收到 1/2 帧，现场状态显示接收侧 TDMA `WINDOW_MISSED`，不是 payload registry 拒绝；随后重跑完整 HIL 通过，记录位于 `build-rtos-multicore-smoke\refmem_auto_tdma_payload_regression_retry\records.json`。
- 后续动作：
  - P0 优先将 AUTO NodeLoad 从“单发 TX + 对端 RX 窗口命中”升级为 ACK/重发/fence completion，避免窗口 missed 后丢失一个 NodeLoad delta。TDMA 的核心验收是时间可预期和 completion 可证明，不能只以重跑成功作为产品级可靠性证据。
  - 继续将 long frame payload 用同样 registry 方式接入 OTA/config/log，不得绕过 `components/tdma`。

### REFMEM-TASK-20260816-048 - TDMA component extraction baseline

- 状态：完成并通过 COM5/COM6 RefMem 同步 HIL
- 日期：2026-08-16
- 任务目标：
  - 解决 “VDC 一套 TDMA、REFMEM 一套 realtime_tdma” 的命名和所有权混乱。
  - 将 TDMA scheduler / PIO transport / timestamp spine 抽为 `components/tdma` 基础件；RefMem 和 VDC 只作为 payload producer/consumer 挂载，不再拥有私有 TDMA scheduler。
- 完成内容：
  - 新增 `components/tdma/inc/tdma_service.h`、`components/tdma/src/tdma_service.c`，承载 core0 intent mailbox、core1 result snapshot、scheduled window、transport ops、短帧/长帧和 payload binding 基础契约。
  - `components/distributed_refmem/inc/src/refmem_realtime_tdma.*` 改为 RefMem 兼容适配层：旧 SCPI/quality/HIL API 保持字段不变，实际执行委托给 `tdma_service`。
  - RefMem DELTA 与 ACK/FENCE payload 作为 short frame payload binding 注册到 TDMA service；后续 VDC sync sample、idle beacon、OTA/config/log long frame 走同一基础件扩展。
  - 修正抽离后首轮 HIL 暴露的容量边界：`tdma_service` 保留 long frame 1024 字节上限，RefMem 物理适配层在 receive bridge 中按 `REFMEM_REALTIME_TDMA_FRAME_MAX` 收敛短帧接收容量，避免把长帧能力误传给 RefMem PIO RX DMA 上限校验。
  - `application/src/app.c` 将 VDC/DPLL service 收敛到 core1 realtime loop，core0 RTOS task 不再运行 VDC/DPLL 维护循环。
  - `tools/tests/run_host_unit_tests.ps1` 将 `run_refmem_realtime_tdma_tests.ps1` 纳入总门禁，避免 TDMA 兼容层只编译不执行。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，18/18 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260816095934`，package CRC `0x99D513A9`。
  - COM5/COM6 OTA send、boot 和 commit 到 build `20260816095934` 通过，两板 `SYSTem:FW:BUILD?` 均返回 `"20260816095934"`，错误队列均为 `0,"No error"`。
  - 执行 `python tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_node_load_auto_hil_COM5_COM6_20260816095934_tdma_component` 通过。
  - HIL 证据：A->B 只对 COM5 下发两条 `LOAD:NODE` 后，COM6 `applied_rx_count` 增长到 2，`last_frame_type=3`，NodeLoad TableRegistry CRC 与 COM5 一致；B->A 只对 COM6 下发两条 `LOAD:NODE` 后，COM5 `applied_rx_count` 增长到 2，`last_frame_type=3`，NodeLoad TableRegistry CRC 与 COM6 一致。
  - 增强版 AUTO 维护验证通过：`tools/refmem/refmem_node_load_auto_hil_validate/refmem_node_load_auto_hil_validate.py` 在 IO preflight 前先关闭残留 AUTO 并释放 `REALtime:IO`；同步完成后继续观察两板自动 RX 维护，确认 `pending_count=0`、`last_error=0`、`submitted_rx_count` 和 TDMA `service_count/completed_seq` 继续增长。记录位于 `build-rtos-multicore-smoke\refmem_node_load_auto_hil_COM5_COM6_20260816095934_auto_maintenance\records.json`。
  - 两方向 `SYSTem:REFMEM:CLAIM? 5` 返回一致的 SlotClaimMap 摘要，最终两板 `SYSTem:ERRor?` 均为 `0,"No error"`。
- 后续动作：
  - 继续把 VDC observation sample、idle beacon 和 long frame payload 从文档契约落到 `components/tdma` 的 producer/consumer 注册机制。

### REFMEM-TASK-20260816-047 - TDMA time-unit naming and diagnostic timestamp snapshot

- 状态：完成 host/build 验证；板端 DPLL timestamp latch 待实现
- 日期：2026-08-16
- 任务目标：
  - 统一 RefMem realtime TDMA 对外 deadline 口径：deadline 字段改为 `deadline_us`，质量字段改为 `p99_us/p999_us`。
  - 暴露当前 core1 realtime TDMA 诊断时间戳，作为后续 VDC/DPLL bring-up 的过渡 evidence，但不进入 100 ns lock gate。
- 完成内容：
  - `refmem_realtime_tdma` snapshot 追加 submit/core1 arm/start/done/elapsed ns 字段，并声明 `timestamp_source=SOFTWARE_US`、`timestamp_resolution_ns=1000`、`timestamp_flags=DIAGNOSTIC_ONLY`。
  - `DistributedRefMemAO` AUTO 配置、FENCE payload、QUALITY payload、RefMem command timeout、SCPI 维护命令和三份 HIL 脚本统一到 `deadline_us` / `timeout_us` 命名。
  - `refmem_spi_physical_adapter` 公开 RX timeout 参数改为 `timeout_ms`，保持内部 SDK 等待实现不扩散到协议命名。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，15/15 host test scripts passed。
  - `python -m py_compile tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py tools\refmem_quality_gate_hil_validate\refmem_quality_gate_hil_validate.py tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815174141`，package CRC `0x58997C54`。
- 后续动作：
  - 继续 VDC `VdcTDMATimestampEvidence` 和硬件 timestamp latch；RefMem 只提供 payload、quality 和 evidence，不拥有 DPLL offset/rate。

### REFMEM-TASK-20260815-046 - NodeLoad AUTO 3-wire uplink/downlink adapter correction

- 状态：完成并通过 COM5/COM6 B3 HIL
- 日期：2026-08-15
- 任务目标：
  - 修正 B3 HIL 未收到 frame 的结构性原因：NodeLoad AUTO 不能复用单组 pins，必须区分环路上行 adapter 和下行 adapter。
  - 按产品链路收敛为无 CS 的 3-wire SPI；每组 adapter 保留 `duplex_mode,rx,sck,tx`，支持 half/full duplex 配置。
- 完成内容：
  - `DistributedRefMemAO` 的 AUTO 配置由单组 pins 改为 `uplink_adapter_pins` 和 `downlink_adapter_pins`，并新增 uplink/downlink duplex mode。
  - NodeLoad AUTO RX window 使用 uplink adapter，语义为接收上一个板卡的数据；pending NodeLoad DELTA TX 使用 downlink adapter，语义为下发到下一个板卡。
  - `SYSTem:REFMEM:SYNC:AUTO` 改为配置 `enable,local_slot,target_mask,baud,deadline,uplink_mode,uplink_rx,uplink_sck,uplink_tx,downlink_mode,downlink_rx,downlink_sck,downlink_tx`；`AUTO?` 返回同序 snapshot 和计数/evidence。
  - `refmem_spi_physical_adapter` 支持 `csn_pin=REFMEM_SPI_PHYSICAL_PIN_UNUSED` 的无 CS 模式：master 不驱动 CS，slave PIO 不等待 CS，仍由 RM packet magic、length、CRC 和 RX stable 判断帧边界。
  - `tools/refmem/refmem_node_load_auto_hil_validate/refmem_node_load_auto_hil_validate.py` 固化两板 B3 验收，自动运行 IO preflight 后推导 A/B 的 uplink/downlink adapter，并只向源板发送 `LOAD:NODE`。
- 验证结果：
  - `python -m py_compile tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，15/15 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815165314`，package CRC `0x36DE7654`。
  - OTA build `20260815165314` 到 COM5/COM6 并 commit 通过，两板 `SYSTem:FW:BUILD?` 均返回 `"20260815165314"`，`SYSTem:ERRor?` 均为 `0,"No error"`。
  - 执行 `python -u tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_node_load_auto_hil_COM5_COM6_20260815165314` 通过。
  - IO preflight 实测线序：A->B 为 `[1,2,0,3]`，B->A 为 `[2,1,0,3]`。
  - AUTO 配置证据：A 使用 `SYSTem:REFMEM:SYNC:AUTO 1,0,2,25000000,1000000,1,16,18,23,1,16,22,23`，B 使用 `SYSTem:REFMEM:SYNC:AUTO 1,1,1,25000000,1000000,1,16,18,23,1,16,21,23`。
  - A->B：只向 A 下发 `LOAD:NODE 6,10,...` 和 `LOAD:NODE 5,9,...` 后，B `AUTO?` 显示 `applied_rx_count` 增长到 2，`last_frame_type=3`，`last_source_slot=0`，NodeLoad table staging CRC 与 A 一致。
  - B->A：只向 B 下发同样两条 `LOAD:NODE` 后，A `AUTO?` 显示 `applied_rx_count` 增长到 2，`last_frame_type=3`，`last_source_slot=1`，NodeLoad table staging CRC 与 B 一致。
  - HIL 记录：`build-rtos-multicore-smoke\refmem_node_load_auto_hil_COM5_COM6_20260815165314\records.json` 和 `adapter_plan.json`。
- 后续动作：
  - 在 B3 已闭环基础上，继续 P0 表镜像 activation gate 的跨节点 ACK/FENCE 与 owner validation callback 调度。

### REFMEM-TASK-20260815-045 - NodeLoad AUTO gate correction and HIL script

- 状态：完成 host/build 验证；COM5/COM6 HIL 待执行
- 日期：2026-08-15
- 任务目标：
  - 支持通过 SCPI 配置 RefMem NodeLoad 是否自动运行，并保证开启 AUTO 后仍可在 idle 业务状态提交本地 `LOAD:NODE`。
  - 固化 B3 双板验收脚本，验证 PC 不搬运 frame hex 的 NodeLoad 双向同步。
- 完成内容：
  - 新增 `distributed_refmem_can_accept_node_load_intent()`，由 `DistributedRefMemAO` 判断 NodeLoad intent 是否可接收。
  - `DistributedRefMemAO` 允许抢占自己挂起的 AUTO RX window：本地 `LOAD:NODE` staging 前 abort AUTO RX，staging 成功后进入 pending TX 队列；手工 TDMA/其他实时活动仍不被抢占。
  - `SYSTem:REFMEM:LOAD:NODE` 改为使用 RefMemAO 判定；`SYSTem:REFMEM:LOAD:SD` / `LOAD:BOARD` 仍保持 realtime idle 门禁。
  - 修复 RMTP inline package CRC 生成端：ApplicationModel 不再依赖 `ota_crc32_update()` 的隐含初值语义，改为本地 CRC32 算法，与 TableRegistry validator 的 package/payload/table CRC 完全一致。该问题此前导致板端 `LOAD:NODE` / `LOAD:BOARD` 返回 `PACKAGE_INVALID`，host stub 未暴露。
  - inline package 构建失败时保留 package validator 的失败表证据：若 validation 已给出 `first_bad_table`，则通过该表的 registry entry 暴露 `FAILED + last_result=package_error`，避免所有失败都折叠成 `BAD_TABLE_VERSION`。
  - 新增 `tools/refmem/refmem_node_load_auto_hil_validate/refmem_node_load_auto_hil_validate.py`：COM5/COM6 分别开启 `SYSTem:REFMEM:SYNC:AUTO`，只对源板下发两条 `LOAD:NODE`，目标板只查询 `AUTO?`、`LOAD:STATus?`、`TABle? 3` 和 `CLAIM?` 证据；随后反向执行。
- 验证结果：
  - `python -m py_compile tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，15/15 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815162909`，package CRC `0xFA4FE362`。
- 后续动作：
  - 将 build `20260815162909` OTA 到 COM5/COM6。
  - 执行 `python tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py --port-a COM5 --port-b COM6`，若出现窗口碰撞，再按 RefMemAO/TDMA 调度策略修正，不退回 PC frame bridge。

### REFMEM-TASK-20260815-044 - Configurable NodeLoad auto sync service

- 状态：完成 host/build 验证；板端 HIL 待执行
- 日期：2026-08-15
- 任务目标：
  - 推进风险评审 §3.4 子项 B2：NodeLoad DELTA 不再依赖维护命令逐帧触发，改由 RefMemAO service 和 core1 TDMA 自动运行。
  - 自动运行必须可配置，默认关闭；SCPI 只设置运行策略或提交 LOAD intent，不承担对端 frame 搬运。
- 完成内容：
  - `DistributedRefMemAO` 增加 NodeLoad auto sync 队列，`LOAD:NODE` / `ModelTurntable LOAD` staging 成功后将 instance id 加入 pending 队列。
  - `distributed_refmem_service()` 在临界区外推进 auto sync：有 pending NodeLoad 时提交 TDMA TX；无 pending 时按配置提交 RX window；收到 RX frame 后由 RefMem Sync state machine 校验，再交给 ApplicationModel staging。
  - 新增 `distributed_refmem_configure_node_load_auto_sync()` 和 `distributed_refmem_get_node_load_auto_sync()`，支持 enable、local slot、target mask、baud、deadline 和 PIO SPI pins。
  - 新增维护命令 `SYSTem:REFMEM:SYNC:AUTO` / `SYSTem:REFMEM:SYNC:AUTO?`；默认关闭，开启后才会自动占用 PIO SPI transport。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_node_load_sync_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，15/15 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `git diff --check` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815160049`，package CRC `0x11683118`。
- 后续动作：
  - 执行完整 host/docs/build 验证。
  - 固化 COM5/COM6 HIL 脚本：两板先分别开启 AUTO，再只对源板下发两个 `LOAD:NODE`，确认目标板 staging/table/claim/quality 同步；随后反向执行。

### REFMEM-TASK-20260815-043 - NodeLoad sync delta foundation

- 状态：完成 host/build 验证；板端 HIL 待执行
- 日期：2026-08-15
- 任务目标：
  - 推进风险评审 §3.4 子项 B：建立两板真实 NodeLoad 同步所需的数据语义和 RefMemAO apply 入口。
  - 保持 HAOFV 边界：RefMem Sync 只承载受控 DELTA；ApplicationModel 仍由 DistributedRefMemAO owner staging；SCPI 不直接改对端表。
- 完成内容：
  - 新增 `refmem_node_load_sync.h/.c`，定义 NodeLoad entry 到 RefMem DELTA payload/frame 的固定编码和解码。
  - 新增 `test_refmem_node_load_sync.c` 和 runner，覆盖 payload roundtrip、frame roundtrip、mirror 可见性和非法 entry 拒绝。
  - `ApplicationModel` 增加 `refmem_application_model_get_staging_node_load_entry()`，只导出当前 staging NodeLoad entry 的 const copy。
  - `DistributedRefMemAO` 增加 `distributed_refmem_build_node_load_sync_frame()` 和 `distributed_refmem_apply_node_load_sync_payload()`；源端从 staging entry 生成 NodeLoad DELTA，目标端解码后通过 ApplicationModel staging API 合并到本地 staging image。
  - 新增维护入口 `SYSTem:REFMEM:SYNC:TDMA:NODE:TX`：源板不再需要 PC 搬运 frame hex，可从本地 staging entry 直接提交 TDMA TX intent。
  - `SYSTem:REFMEM:SYNC:RX` 和 `SYSTem:REFMEM:SYNC:TDMA:FRAMe?` 收到 NodeLoad DELTA 后会交给 RefMemAO apply staging；非 NodeLoad DELTA 仍只更新 sync mirror。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_node_load_sync_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，15/15 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `git diff --check` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815154736`，package CRC `0xE10EC500`。
- 后续动作：
  - 补全自动 RX/TX 调度：当前仍需要维护面触发目标板 RX/FRAMe 查询，不满足最终“只对源板下发 SCPI LOAD intent”的验收。
  - 用 COM5/COM6 跑双向两节点同步 HIL，确认 X->Y 与 Y->X 的 NodeLoad staging、TableRegistry CRC、SlotClaimMap 和 quality/evidence 一致。

### REFMEM-TASK-20260815-042 - P0 risk 3.4 fake TX correction

- 状态：子项 A 完成 host/build 验证；子项 B 两板真实同步待实现
- 日期：2026-08-15
- 任务目标：
  - 修复风险评审 §3.4：`refmem_pio_spi_adapter_send()` 不能作为无物理后端的 fake TX 返回成功。
  - 保持 HAOFV 边界：旧 `SYSTem:REFMEM:SYNC:*?` 只作为 frame builder / PC bridge 诊断入口；真实发送必须走 physical adapter 或 core1 TDMA owner 路径。
  - 最终验收不是“能生成 hex”，而是两板真实双向同步：X 板 SCPI 加载两个节点并同步到 Y，随后 Y 板 SCPI 加载两个节点并同步回 X。
- 完成内容：
  - `refmem_transport_adapter.h` 增加 `REFMEM_TRANSPORT_ERROR_TX_UNBOUND`。
  - `refmem_pio_spi_adapter_send()` 在 frame/MTU 校验通过后仍拒绝发送，记录 `tx_reject_count`、`last_tx_size` 和 `TX_UNBOUND`，不再增加 `tx_count` 或返回成功。
  - `SYSTem:REFMEM:SYNC:HELLo?/EPOCh?/DELTa?/ACK?/FENCe?/QUALity:FRAMe?` 取消 fake adapter send 调用，只生成 frame hex；真实两板 TX 继续使用 `SYSTem:REFMEM:SYNC:TDMA:*` 或 physical raw bring-up。
  - `test_refmem_pio_spi_adapter.c` 将 send 单测改为断言无 TX 后端必须拒绝。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_pio_spi_adapter_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_hello_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `git diff --check` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815153622`，package CRC `0x6A4E1D33`。
- 后续动作：
  - 完成两板真实同步验收：PC 只对源板下发 SCPI intent，不搬运 frame hex、不直接写对端；目标板通过维护查询确认 NodeLoad / table image / SlotClaim / quality evidence 与源板一致。
  - 继续按风险评审 P0/P1 清单推进 §3.5 fence deadline、§3.6 receive raw capacity、§3.7 arm/capture 状态破坏等结构性纠偏。

### REFMEM-TASK-20260815-041 - Vector fixed regions rename

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 修正“Vector 16 槽”的误导命名：Vector directory 的 16 项是固定内存定义区（region），不是 A0-A7 可实例化节点槽位，也不是 SlotClaim 候选槽。
  - 保持 HAOFV 边界：NodeLoad / SlotClaim / A0-A7 继续使用逻辑 slot 语义；Vector 只提供固定事实 region、owner 边界和 CRC/seq/stale 保护。
- 完成内容：
  - `refmem_vector_table.h/.c` 将 `REFMEM_VECTOR_SLOT_*`、`slot_count`、`slots[]`、vector header/node 类型统一改为 `REFMEM_VECTOR_REGION_*`、`region_count`、`regions[]`、`*_region_t`。
  - `DistributedFbInstanceTable`、`DistributedEventLinkTable`、`DistributedDataLinkTable`、`DistributedDeploymentGate` 中指向 Vector directory 的字段改为 `state_region_ref`、`health_region_ref`、`evidence_region_ref`、`crc_region_ref`、`reject_region_ref`。
  - DataLink 可读路径从 `*Slot.*` 改为 `*Region.*`，Python RMTP 生成工具同步改用 `REGION_*` 常量和 `region_path_hash`。
  - `refmem_quality_evaluate_deployment_gate()` 的输出拆成 `refmem_quality_gate_result_t`，避免 quality runtime 的逻辑 `reject_slot` 与 DeploymentGate 表的 `reject_region_ref` 混用。
  - 架构文档明确：Vector directory 16 项是固定 region；A0-A7 / SlotClaim 才是节点实例化槽位。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815151411`，package CRC `0x6B9F4362`。
- 后续动作：
  - 后续架构评审中继续区分 Vector fixed region、A0-A7 logical slot、SlotClaim assignment 和 RMTP 九表模型，避免把 region 当成可加载节点槽。

### REFMEM-TASK-20260815-040 - Inline LOAD:NODE/BOARD to full RMTP package image

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 完整修复风险评审 §3.11：inline `LOAD:NODE` / `LOAD:BOARD` 不能停在 metadata-only 或单表 staging；单表 draft 必须合并为完整 Node Model Candidate，并生成 9 表 RMTP package image。
  - 保持 HAOFV 边界：SCPI 只提交 intent；ApplicationModel owner 生成 draft/candidate/package；TableRegistry 只管理完整 image lifecycle；Vector 不承载完整表数据。
- 完成内容：
  - 在架构文档中固化两张表：`Table Draft -> Node Model Candidate -> RMTP Package Image` 层级表，以及 `LOAD:SD / LOAD:NODE / LOAD:BOARD` 到最终 package 产物的路径表。
  - `refmem_application_model.c` 增加 inline RMTP serializer：按 9 张 canonical 表生成 header、directory、payload、per-table CRC、payload CRC 和 package CRC。
  - serializer 从当前 active 9 表读取未修改表，并用私有 `NodeLoadTable draft` / `BoardCapabilityTable draft` 覆盖候选表，形成完整 Node Model Candidate。
  - `LOAD:NODE` 成功路径改为调用 `refmem_table_registry_stage_package_image()`，不再把 `staging_package_crc32` 当成 NodeLoad 单表 CRC。
  - `LOAD:BOARD` 成功路径同步刷新系统级 `LOAD:STATus?` 的 inline package staging 状态，同时 `BOARD:STATus?` 保留 BoardCapability draft 整表 CRC。
  - host 单测新增 inline `LOAD:BOARD` 和 `LOAD:NODE` activation-ready package 断言：stage 后可 prepare staging view、activate、commit，并确认 runtime getter 发生预期变化。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815144746`，package CRC `0x8A0A9E09`。
- 后续动作：
  - 继续审查 TableRegistry `stage_table()` 的剩余用途，把产品路径中的单表 staging 约束为失败 evidence 或测试辅助。
  - 重新跑 COM5/COM6 HIL，确认 inline package staging 与 `LOAD:ACTivate` 在板端同样闭环。

### REFMEM-TASK-20260815-039 - LOAD:BOARD private table staging image

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 推进风险评审 §3.11：`SYSTem:REFMEM:LOAD:BOARD` 不能只发布单条 entry CRC 和 metadata-only staging，至少必须形成 RefMemAO 私有 `BoardCapabilityTable` staging image。
  - 保持 HAOFV 边界：SCPI 只提交 board capability load 意图；application model owner 构造候选表、执行 contract validation 并发布 TableRegistry staging 摘要。
- 完成内容：
  - 新增私有 `s_staging_board_capability_table` / `s_staging_board_capability_valid`，与 NodeLoad staging image 对齐。
  - 新增 `refmem_model_make_staging_board_capability_table()`：从当前 active 或已有 staging board table 复制候选表、替换目标 board entry、执行 `refmem_application_contract_validate_board_capability_table()`，并计算整表 CRC。
  - `refmem_application_model_stage_scpi_board_capability()` 不再使用单 entry CRC；合法路径发布 `BoardCapabilityTable` 整表 staging CRC，非法路径继续发布 FAILED。
  - 删除旧 `refmem_model_board_entry_crc32()`，避免继续暗示单 entry staging。
  - `test_refmem_table_registry.c` 增加 `LOAD:BOARD` 断言：registry table 1 staging CRC 等于 board load snapshot 的整表 CRC，且不同于单 entry CRC；active getter 在 staging 阶段不被改变。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815142112`，package CRC `0xE8171F9A`。
- 后续动作：
  - §3.11 仍未完全关闭；下一步需要让 inline NodeLoad/BoardCapability staging 合成完整 package image，或明确声明为不参与 activation 的诊断 staging。

### REFMEM-TASK-20260815-038 - TableRegistry OWNER_OK provenance

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 修复风险评审 §3.10：`OWNER_OK` 不能由 `present + CRC` 推导，必须代表 owner validation provenance。
  - 保持 HAOFV 边界：TableRegistry 只记录 image/validation 证据，不把 CRC 完整性伪装成 AO/FB owner 语义验证。
- 完成内容：
  - `refmem_table_registry_refresh_active()` 中编译内置 active entry 的 flags 从 `ACTIVE_PRESENT|CRC_OK|OWNER_OK` 降级为 `ACTIVE_PRESENT|CRC_OK`。
  - staging package 路径仍按 `owner_validated_table_mask` 逐表置 `OWNER_OK`；activation 后的 active image 可继承已验证 provenance。
  - `test_refmem_table_registry.c` 增加断言：初始化 active entry 有 CRC flag，但 `OWNER_OK` flag 必须为 0。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815141450`，package CRC `0x9A5F9C0D`。

### REFMEM-TASK-20260815-037 - SlotClaimGate explicit ready gating

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 修复风险评审 §3.3：DeploymentGate/SlotClaimGate 调用端必须真正以 gate ready 结果作为放行条件。
  - 保持 HAOFV 边界：RefMemAO 和 SystemManager 只消费 RefMem 派生的门禁事实，不绕过 SlotClaimMap 或 DeploymentGate。
- 完成内容：
  - `refmem_application_model.c` 静态表 validation 改为显式检查 `refmem_slot_claim_gate_evaluate(...) && claim_gate.ready`。
  - `distributed_refmem.c` activation gate 改为显式检查 `evaluate && ready`。
  - `system_manager.c` RUN gate 改为显式检查 `evaluate && ready`。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815141111`，package CRC `0xCE53713F`。

### REFMEM-TASK-20260815-036 - Cross-core seqlock guard release fence

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 修复风险评审 §3.1：跨核 seqlock/guard 写侧普通自增缺少 release fence，读侧 acquire 无法保证读取到完整可见的数据快照。
  - 保持 HAOFV 边界：`refmem_realtime_tdma` 仍由 core0 发布 intent、core1 发布 result；`refmem_command` 仍由 command slot owner 处理 ACK/NACK，不引入跨层业务逻辑。
- 完成内容：
  - `refmem_realtime_tdma.c` 新增 guard release increment helper，intent/result begin/end write 均通过 `__atomic_add_fetch(..., __ATOMIC_RELEASE)` 发布 guard。
  - `refmem_command.c` 新增 command guard release increment helper，command slot begin/end write 均通过 release 语义发布 guard。
  - 扫描 `components/distributed_refmem`，确认无 `guard++` 残留。
- 验证结果：
  - `rg -n "guard\+\+|\+\+.*guard" components\distributed_refmem -S` 无残留匹配。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_contract_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815140755`，package CRC `0x8FC40ACD`。

### REFMEM-TASK-20260815-035 - Staging table view pre-parse activation gate

- 状态：完成 COM5/COM6 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 收掉 active image 已切换但 application runtime snapshot 解析失败的理论窗口。
  - 保持 HAOFV 边界：SCPI 只提交 activation intent；`DistributedRefMemAO` 调度，`RefMemTableRegistry` 管 image，`refmem_application_model` 作为 owner 预解析 staging stable view。
- 完成内容：
  - 新增 `refmem_application_model_prepare_staging_table_views()`，从 staging image 借出 9 张 canonical 表并解析到 pending runtime snapshot。
  - 新增 `refmem_application_model_commit_prepared_table_views()` / `discard_prepared_table_views()`，registry activation 成功后才提交 pending，失败时丢弃。
  - `distributed_refmem_activate_staging()` 在调用 `refmem_table_registry_activate_staging()` 前先执行 staging view 预解析；预解析失败时记录 `STAGING_VIEW_INVALID`，并通过 command slot NACK。
  - 新增 `REFMEM_TABLE_ACTIVATE_ERR_STAGING_VIEW_INVALID` 和 `REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED`，并同步 `SYSTem:COMMand:NACK?` reason table。
  - `test_refmem_table_registry.c` 增加 prepare/commit 断言：prepare 不改变 active getter，commit 后才切到 package runtime snapshot，pending 只能提交一次。
  - `tools/refmem/refmem_table_registry_validate/refmem_table_registry_validate.py` 增加 preflight error queue drain，避免 Storage/OTA 历史错误污染本轮验证；summary 只将 `FAIL` 计入失败。
  - `tools/storage/storage_file_upload/storage_file_upload.py` 增加 StorageAO job idle 等待和 active write transaction abort preflight，避免目录/删除/写入 job 交错。
  - `tools/interface/scpi_query/scpi_query.py` 修正 `INFO? "path"` 这类带参数查询识别，并新增 `tools/interface/scpi_query/refmem_storage_info_probe.scpi` 作为 RefMem HIL SD 依赖探测命令集。
  - `BoardCapabilityTable` 当前产品 wire contract 固定为 8 条 A0-A7 能力记录；`CLAIM_CANDIDATE_MAX = 16` 只保留给 SlotClaim proposal / overflow evidence，不复用为 BoardCapability 表容量。
  - `scpi_refmem_read_package()` 先通过 StorageAO file info 获取真实文件大小，再按剩余长度分块读取，避免最后一块 512 字节越过文件尾导致 `LOAD:SD` 读包失败。
  - `tools/storage/sd_fs_build/sd_fs_build.py` 固化生成 `manifest_refmem_hil.idx`，用于 RefMem HIL，只要求 profile、mission、cal 和 RefMem `.rmtp` 四个小文件。
  - 新增 `tools/interface/scpi_query/refmem_file_read_probe.scpi`，用于通过 Storage `FILE:READ?` 探测 `/refmem/app_model.rmtp` 头尾读取。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python -m py_compile tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `python -m py_compile tools\storage_file_upload\storage_file_upload.py` 通过。
  - `python -m py_compile tools\scpi_query\scpi_query.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `python -m pytest tests\python\test_refmem_pack_build.py -q` 通过，2 passed；pytest cache 写入 `.pytest_cache` 因权限被拒绝产生 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，最终生成 build id `20260815135119`，package CRC `0x1801DEA8`。
  - `python tools\sd_fs_build\sd_fs_build.py --build-dir build-rtos-multicore-smoke --output-dir build-rtos-multicore-smoke\sdcard_8slot_20260815135119 --clean --no-zip --no-reports` 通过，生成 8 槽位 `.rmtp` size `4512`，package CRC `0x31062D02`，BoardCapability table size `296`。
  - COM5 OTA 到 build `20260815135119` 并 boot commit 成功；上传 8 槽位 HIL 文件集合成功。
  - COM5 执行 `refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_8slot_20260815135119\refmem\app_model.rmtp --load-sd --activate` 通过：`LOAD:SD` 返回 `STAGED`，`LOAD:ACTivate` 返回 `ACTIVE`，active image table mask `511`，BoardCapability table size `296`，最终 `SYSTem:ERRor?` 为 `0,"No error"`。
  - COM6 OTA 到 build `20260815135119` 并 boot commit 成功；上传 8 槽位 HIL 文件集合成功。
  - COM6 首次 `LOAD:SD` 在读包阶段返回 `REJECTED`，随后 `refmem_file_read_probe.scpi` 证明 `/refmem/app_model.rmtp` 头尾 Storage read 正常；复跑 `refmem_table_registry_validate.py COM6 --load-sd --activate` 通过，active image table mask `511`，BoardCapability table size `296`，最终 `SYSTem:ERRor?` 为 `0,"No error"`。
- 过程记录：
  - 全量 System Pack manifest 要求 `/update/RP2350_TRIG_UPDATE.pkg`，通用 Storage write API 当前单事务上限 8192 bytes，不能用它上传 523 KB OTA 包；本轮 RefMem HIL 改用 `manifest_refmem_hil.idx`，只要求 profile、mission、cal 和 RefMem `.rmtp` 四个小文件，不绕过 StorageAO manifest gate。
  - COM5 首次覆盖 manifest 后出现一次 `manifest_status=8 / missing_count=1`，单独 `SYSTem:SD:MANifest?` 随后返回 OK；该现象计入后续 StorageAO manifest scan/job 时序观察，不阻塞本轮 RefMem activation gate 结论。
  - COM6 最终 build 首次 `LOAD:SD` 后、Storage read probe 后复跑成功，继续作为 StorageAO job/manifest 首轮时序支线观察项。
- 后续动作：
  - 继续接入真实 owner validation callback 调度和跨节点 ACK/FENCE。

### REFMEM-TASK-20260815-034 - Active table views to application runtime snapshot

- 状态：完成 COM5/COM6 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 让 registry 级 active image activation 后，业务读取路径不再继续固定读编译内置表。
  - 保持 HAOFV 边界：`RefMemTableRegistry` 只管理 active/staging/rollbackable image 和 const table view，`refmem_application_model` 负责把 active view 解析成私有 runtime snapshot。
- 完成内容：
  - 新增 `refmem_application_model_apply_active_table_views()`，从 active image 借出 9 张 canonical 表的 stable view，解析到 application model 私有 runtime tables。
  - 外部 getter 切到 runtime snapshot：ApplicationMap、BoardCapability、GenericNode、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate、ConnectionQuality。
  - `DistributedRefMemAO` 在 `TABLE_PACKAGE_ACTIVATE` 成功后触发 application model apply，并更新 `active_package_crc32` 与 per-table CRC snapshot。
  - `tools\refmem_table_registry_validate.py` 增加 activation 后 `SYSTem:REFMEM:LOAD:STATus?` 检查，确认 application model active package CRC 已跟随 active image。
  - `tools\tests\run_refmem_table_registry_tests.ps1` 链接 application model 源，新增 host 断言覆盖 getter 从 active image runtime snapshot 取 BoardCapability/FBInstance。
- 当前验证：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python -m py_compile tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815124740`，package CRC `0x1E1584A1`。
  - `python -u tools\ota_send\ota_send.py COM5 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python -u tools\ota_boot_commit\ota_boot_commit.py COM5 --expected-build 20260815124740 --out-dir build-rtos-multicore-smoke\ota_commit_COM5_20260815124740_refmem_app_view` 通过。
  - `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815124740_app_view` 通过。
  - `python -u tools\ota_send\ota_send.py COM6 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python -u tools\ota_boot_commit\ota_boot_commit.py COM6 --expected-build 20260815124740 --out-dir build-rtos-multicore-smoke\ota_commit_COM6_20260815124740_refmem_app_view` 通过。
  - `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM6 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM6_20260815124740_app_view` 通过。
- 后续动作：
  - 把 activation 前 staging stable table view 预解析纳入 activation transaction，减少“registry 已切换但 application apply 失败”的理论窗口。
  - 接入真实 owner validation callback 调度和跨节点 ACK/FENCE。

### REFMEM-TASK-20260815-033 - Stable table view access/release

- 状态：完成 COM5/COM6 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 在 registry 级 image activation 后，提供稳定的单表只读 view，避免后续业务 AO/FB 直接拼 RMTP offset 或读取裸 buffer。
  - 保持 HAOFV 边界：TableRegistry 借出 const payload view，向量表和 SCPI 仍只输出摘要，不承载完整 table 数据。
- 完成内容：
  - 新增 `refmem_table_view_t`，包含 role、table id、table seq、package CRC、table CRC、image offset、image size 和 const payload pointer。
  - 新增 `refmem_table_registry_access_table(role, table_id, view)` / `refmem_table_registry_release_table(view)`，通过 RMTP directory 和 table CRC 定位 payload。
  - 增加 reader guard：active/staging/rollbackable 任一 image 存在未 release view 时，activation 返回 `IMAGE_BUSY`，避免表指针悬挂。
  - 新增 `SYSTem:REFMEM:TABle:VIEW? [role],[table_id]`，通过 access/release 读取 view 摘要：`version,role,table_id,table_seq,package_crc32,table_crc32,image_offset,image_size,first_u32`。
  - `tools/refmem/refmem_table_registry_validate.py --activate` 增加 active table 0 和 table 3 view 摘要断言。
- 当前验证：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过，覆盖 active view access/release、double release reject、unreleased view 阻断 activation。
  - `python -m py_compile tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815122901`，package CRC `0xDDB83D74`。
  - `python -u tools\ota_send\ota_send.py COM5 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python -u tools\ota_boot_commit\ota_boot_commit.py COM5 --expected-build 20260815122901 --out-dir build-rtos-multicore-smoke\ota_commit_COM5_20260815122901_refmem_view` 通过。
  - `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815122901_view` 通过。
  - `python -u tools\ota_send\ota_send.py COM6 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python -u tools\ota_boot_commit\ota_boot_commit.py COM6 --expected-build 20260815122901 --out-dir build-rtos-multicore-smoke\ota_commit_COM6_20260815122901_refmem_view` 通过。
  - `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM6 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM6_20260815122901_view` 通过。
  - COM5/COM6 均确认 `LOAD:SD`、`LOAD:ACTivate`、`TABle:IMAGe?`、`TABle:VIEW? 0,0`、`TABle:VIEW? 0,3`、9 张 `TABle?` 和 `SYSTem:ERRor?` 闭环通过。
- 后续动作：
  - 下一步把 active package view 解析为业务结构表 snapshot，并逐步替换继续读取编译内置表的路径。

### REFMEM-TASK-20260815-032 - Table package activation command

- 状态：完成 COM5/COM6 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 让 P0 table image activation 不再只停留在 host API；补齐板端维护入口和固化验证脚本。
  - 保持 HAOFV 边界：SCPI 只提交 activation intent 和读取 descriptor，真正 active/staging/rollbackable 切换由 `DistributedRefMemAO` / `RefMemTableRegistry` owner 完成。
- 完成内容：
  - 新增 `REFMEM_COMMAND_TYPE_TABLE_PACKAGE_ACTIVATE`，activation 入口也进入 command slot。
  - 新增 `distributed_refmem_activate_staging(realtime_idle)`：本地 owner take 后组装 activation gate，检查 RefMem idle、realtime idle、runtime protection、CRC/owner、SlotClaim、quality/DeploymentGate 和 local command take，再调用 `refmem_table_registry_activate_staging()`。
  - 新增 `SYSTem:REFMEM:LOAD:ACTivate`，返回 `ACTIVE/REJECTED`、registry snapshot，以及 active/staging/rollbackable 三个 image descriptor。
  - 新增 `SYSTem:REFMEM:TABle:IMAGe? [role]`，读取 active/staging/rollbackable descriptor：`version,role,state,table_mask,package_crc32,table_seq,path_hash,last_result,evidence_index`。
  - `tools/refmem/refmem_table_registry_validate/refmem_table_registry_validate.py` 增加 `--activate`，固化 `LOAD:SD -> LOAD:ACTivate -> command ACK -> image descriptor -> TABle? 0..8` 验证。
- 验证结果：
  - `python -m py_compile tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815121205`，package CRC `0xFE13F612`。
  - COM5 OTA 到 build `20260815121205` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM6 OTA 到 build `20260815121205` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM5 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815121205_activate` 通过。
  - COM6 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM6 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM6_20260815121205_activate` 通过。
- 结论：
  - P0 已具备板端可触发、可查询、可脚本验证的 registry 级 table package activation 闭环。
  - 本轮是首轮 activation，旧 active 来自编译内置表、没有 package bytes，因此 rollbackable descriptor 为空；后续需增加二次 activation 回滚验证、active package 到业务 stable table view 的解析、owner access/release，以及跨节点 ACK/FENCE。

### REFMEM-TASK-20260815-031 - Registry image lifecycle buffers

- 状态：完成 COM5/COM6 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `RefMemTableRegistry` 从 descriptor/CRC staging 推进到 registry 级真实 package image 生命周期。
  - 保持 HAOFV 边界：完整 `.rmtp` bytes 只进入 `DistributedRefMemAO` / `RefMemTableRegistry` 私有 buffer，向量表和 SCPI 查询只暴露 descriptor、CRC、state、seq 和 evidence 摘要。
- 完成内容：
  - `RefMemTableRegistry` 新增 active/staging/rollbackable 三组静态 image buffer 和 size，单包上限 `REFMEM_TABLE_IMAGE_BUFFER_SIZE=8192`。
  - 新增 `refmem_table_registry_stage_package_image()`，在 package/header/table CRC 与 owner validation summary 通过后复制完整 package bytes 到 staging image。
  - `refmem_table_registry_activate_staging()` 已能在 gate 通过且 staging bytes 存在时执行 registry 级切换：旧 active 进入 rollbackable，staging 进入 active，per-table active CRC 从 staging CRC 更新，staging descriptor/payload 清空。
  - metadata-only staging、失败 staging 或空 package staging 会清空 staging payload；activation 继续返回 `IMAGE_NOT_LOADED`，避免复用上一轮 package bytes 形成伪 active。
  - `SYSTem:REFMEM:LOAD:SD` 现在把 StorageAO 读取到的 package bytes 随 package validation summary 一起交给 `DistributedRefMemAO`，SCPI 仍不直接写 TableRegistry 或 active fact。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过；新增 stale payload 回归断言。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815115843`，package CRC `0x15FE50D0`。
  - COM5 OTA 到 build `20260815115843` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM6 OTA 到 build `20260815115843` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM5 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815115843_image_lifecycle` 通过。
  - COM6 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM6 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM6_20260815115843_image_lifecycle` 通过。
  - COM5/COM6 均执行 `tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py --skip-sd` 通过，确认 `LOAD:NODE`、`LOAD:BOARD` 和 command ACK/NACK 路径未被 staging payload 生命周期改动破坏。
- 结论：
  - P0 的 registry 级真实 active/staging/rollbackable image buffer 切换已经落地；向量表仍只承载摘要，符合 HAOFV。
  - activation 后 active package 到业务 stable table view 的解析、owner access/release、真实 owner validation callback 调度和跨节点 activation ACK/FENCE 仍是后续 P0/P2/P3 工作。

### REFMEM-TASK-20260815-030 - NodeLoadTable staging image

- 状态：完成 COM5 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `SYSTem:REFMEM:LOAD:NODE` 从单条候选状态快照推进为私有 staging `DistributedNodeLoadTable` image。
  - 保持 HAOFV 边界：SCPI 只 post `NODE_LOAD_STAGE` command，`DistributedRefMemAO` owner take 后更新 staging 表镜像、执行 validation、ACK/NACK，并只向 TableRegistry 发布摘要。
- 完成内容：
  - 新增 `s_staging_node_load_table` 和 `s_staging_node_load_valid`，`LOAD:NODE` 成功后在私有 staging 表上累积候选。
  - `LOAD:NODE` 现在按候选 `NodeLoadTable` 调用 `refmem_application_contract_validate_node_load_table()`，并计算整表 staging CRC。
  - 成功路径只更新 `RefMemTableRegistry` table 3 `NodeLoadTable` 的 staging CRC、`OWNER_OK` 状态和 flags；不再通过整包 staging refresh 把 9 张表都标成 staging。
  - 非法 node/instance 仍返回 `REJECTED` / command NACK，并把 table 3 标成 `FAILED`，不污染 active 表。
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 同步断言：合法 `LOAD:NODE` 必须有非零 NodeLoadTable staging CRC，`SYSTem:REFMEM:TABle? 3` 的 staging mask 至少包含 bit3。
- 验证结果：
  - `python -m py_compile tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815114032`，package CRC `0xF7BB88ED`。
  - COM5 OTA 到 build `20260815114032` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM5 执行 `python -u tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM5 --skip-sd --timeout 10 --out-dir build-rtos-multicore-smoke\refmem_load_COM5_20260815114032_nodeload_staging` 通过；合法 `LOAD:NODE 5,9,32,32,1,0,0` 生成 table 3 staging CRC `3388599922`，`SYSTem:REFMEM:TABle? 3` 返回 `staging_table_mask=8`。
  - COM5 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815114032` 通过，确认 `LOAD:SD` 全 9 表 staging CRC 未被破坏。
- 结论：
  - `LOAD:NODE` 已进入 P0 表镜像主线，不再只是单字段 snapshot。
  - active/staging/rollbackable 全表 image buffer、activation gate、abort/rollback 仍是 P0 下一步。
- 后续动作：
  - 按 P0-P3 优先级推进：P0 真实 table image activation/rollback；P1 SlotClaimMap 协调和溢出验证；P2 RefMemSlotContract 字段级 owner/snapshot；P3 Command/ACK/NACK 与 activation gate 统一。

### REFMEM-TASK-20260815-029 - TableRegistry per-table staging CRC gate

- 状态：完成 COM5 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 将 RefMem 主线从 `LOAD:SD => STAGED` 字符串检查推进到 TableRegistry per-table 证据检查。
  - 验证 `.rmtp` package directory 中 9 张 canonical 表的 CRC 是否逐表进入 `SYSTem:REFMEM:TABle?` staging entry。
  - 保持 HAOFV 边界：SCPI 只执行 `LOAD:SD` 意图和读取 snapshot，不直接写 active fact 或 TableRegistry。
- 发现问题：
  - COM5 旧 build 执行 `SYSTem:REFMEM:LOAD:SD` 后返回 `STAGED`，但 `SYSTem:REFMEM:TABle? 0` 的 `staging_crc32` 仍为整包 CRC `0xB27CF840`，不是 `ApplicationMap` 单表 CRC `0x60E14FD0`。
  - 根因是 `DistributedRefMemAO` 将 package parser summary 交给 `RefMemTableRegistry` 时漏填 `validation.table_mask`，导致 `refmem_table_registry_stage_package_validation()` 拒绝 per-table validation summary，并退回旧的整包 CRC staging refresh。
- 完成内容：
  - `distributed_refmem_stage_sd_system_pack()` 补齐 `validation.table_mask = 0x1FF`，让 RefMemAO 向 TableRegistry 提交完整 9 表 package validation 事实。
  - 新增 `tools/refmem/refmem_table_registry_validate/refmem_table_registry_validate.py`，读取本地 RMTP header/directory，校验 package/payload/table CRC，然后通过单次串口生命周期查询：
    - `SYSTem:REFMEM:LOAD:SD`
    - `SYSTem:COMMand:ACK?`
    - `SYSTem:REFMEM:LOAD:STATus?`
    - `SYSTem:REFMEM:TABle? 0..8`
    - `SYSTem:ERRor?`
  - 脚本断言 `staging_table_mask=0x1FF`、每表 `staging_crc32` 等于 RMTP directory CRC、`validation_state=OWNER_OK`、`STAGING_PRESENT|CRC_OK|OWNER_OK` flags 齐全。
- 验证结果：
  - `python -m py_compile tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815113037`，package CRC `0xFF54EA0C`。
  - COM5 OTA 到 build `20260815113037` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM5 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815113037_r2` 通过。
  - 验证通过的 9 表 staging CRC：
    - ApplicationMap `0x60E14FD0`
    - BoardCapability `0xCEFC3615`
    - GenericNode `0xA1681310`
    - NodeLoad `0x7D7762D1`
    - FbInstance `0x1CC15A37`
    - EventLink `0x5A4E36C8`
    - DataLink `0xD7E24830`
    - DeploymentGate `0x31FDBA68`
    - ConnectionQuality `0xA67C64D1`
- 结论：
  - `LOAD:SD` 正向路径现在不只证明 package 能解析为 `STAGED`，还证明 TableRegistry 的 per-table staging 证据与 RMTP directory 一致。
  - 真实 active/staging/rollbackable image buffer 仍未落地，activation 仍应保持 `IMAGE_NOT_LOADED` 阻断。
- 后续动作：
  - 继续 RefMem 主线 P0：实现真实 active/staging/rollbackable table image 切换和 owner validation callback 调度。
  - 继续 P7：补无 SD、manifest 缺失、CRC 正确但 owner validation 失败、activation 失败回滚等验证。

### REFMEM-TASK-20260815-028 - Optimize LOAD:SD bounded read chunk

- 状态：完成 COM5 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 在不破坏 HAOFV 边界的前提下优化 `SYSTem:REFMEM:LOAD:SD` 耗时。
  - 保持 SCPI 只发起 load 意图和输出 snapshot，SD/FatFs 仍归 StorageAO，RefMem staging 仍归 DistributedRefMemAO。
- 完成内容：
  - `STORAGE_MANAGER_FILE_READ_MAX_BYTES` 从 128B 提高到 512B，扩大 StorageAO 内部 bounded read job 缓冲。
  - `SCPI_REFMEM_PACKAGE_READ_CHUNK` 从 128B 提高到 512B，让 `LOAD:SD` 读取 4800B RMTP 时从约 38 个 read job 降到约 10 个 read job。
  - 公开 `SYSTem:STORage:FILE:READ?` 的 `SCPI_STORAGE_MMEM_READ_BYTES_MAX` 仍保持 128B，避免普通 SCPI 文件读取响应突然变长。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815110412`，package CRC `0x81CE4C7E`。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - COM5 已通过 OTA/commit 升级到 build `20260815110412`。
  - COM5 写入当前 HIL manifest、profile、mission、cal 和 `/refmem/app_model.rmtp` 后，`SYSTem:REFMEM:LOAD:SD` 单次加载加后续状态查询总耗时约 `6.944 s`；返回 `STAGED`，Storage 最后一块 `FILE_READ` 为 192B，错误队列为 0。
  - COM5 执行 `python -u tools\refmem_pack_write\refmem_pack_write.py COM5 --timeout 10 --load-timeout 30 --chunk 256 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --out-dir build-rtos-multicore-smoke\refmem_pack_write_COM5_20260815110412_512b_load` 通过。
- 结论：
  - `LOAD:SD` 首轮优化有效，耗时从约 37 秒降到 7 秒量级。
  - 无参 `SYSTem:REFMEM:LOAD:SD` 在本轮干净串行测试中未复现 `Missing parameter`；此前污染来自命令行引号错误和同一 COM 口并发访问造成的响应串扰。
- 后续动作：
  - 当前 512B bounded read 路径作为主线可用基线保留；继续压缩到 1 秒级的 bounded stream read job 先转入支线优化，不阻塞后续 RefMem 主线闭环。

### REFMEM-TASK-20260815-027 - StorageAO/RefMem LOAD:SD HIL deep dive

- 状态：完成 COM6 板端闭环；COM5 已在后续任务中恢复并重刷
- 日期：2026-08-15
- 任务目标：
  - 深挖 `/refmem/app_model.rmtp` 通过 Storage SCPI 写入时反复撞到 `RUNNING`、最终 `RENAME_FAILED` 的原因。
  - 判断是否由最小系统板 RefMem PIO-SPI 与 SD 共享 SPI 引起，并给出结构性修复。
- 结论：
  - 当前板级配置中 SD 与 LCD 共用 `BOARD_SPI_PORT`，实际为 `spi1` 的 SCK/MOSI/MISO 总线；RefMem 最小 transport 使用 PIO-SPI GPIO16-19，不是 SD 使用的硬件 SPI 总线。
  - `RUNNING` 的主要原因不是 RefMem PIO-SPI 与 SD 物理共线，而是 StorageAO 服务优先级、boot snapshot 插队和 UI 刷屏共享 SPI 资源叠加。
  - `RENAME_FAILED` 发生在 CRC 已匹配后的临时文件替换阶段，说明 SCPI 分块上传和内存缓冲完整，失败集中在 FatFs rename/replace 语义或目标文件残留。
  - `SYSTem:REFMEM:LOAD:SD` 的挂死风险来自 SCPI 回调栈上 8 KB package buffer；该缓冲已迁移为模块静态加载缓冲，避免命令执行时压垮 SCPI task stack。
  - `SYSTem:REFMEM:LOAD:SD` 返回固定 IO_ERROR/REJECTED 的根因是 RefMem load 只等待 StorageAO job 约 200 ms；真实 manifest/package 分段读取会超过该窗口，导致 `RUNNING` 被误判为失败。等待窗口已与 Storage SCPI 统一到 10000 loops。
  - UI 不是旁路代码：`UiAO`/`LcdFb` 纳入 HAOFV 资源治理，UI 只读取 snapshot 并通过 ResourceArbiter 获取 `SPI0|LCD`，StorageAO 获取 `SPI0|SD`，板级 port 负责共享 SPI 模式和 CS 准备。
- 完成内容：
  - `storage_manager_service()` 改为显式 Storage job 优先于 boot snapshot/system pack 自动动作，避免 SCPI 已投递 job 后被启动快照插队。
  - FreeRTOS 任务优先级调整：`storage` 从 2 提到 3，`ui` 从 2 降到 1，避免 UI 与 StorageAO 在 `SPI0|LCD/SD` 资源上同级频繁碰撞。
  - `ui_manager_service()` 在 StorageAO job 处于 `QUEUED/RUNNING` 时跳过本轮刷新，让 SD 文件操作优先完成。
  - `StatusUI` 对 `SPI0|LCD` 使用 owner 标记 `StatusUI`，后续 ResourceArbiter 快照可直接看到冲突 holder。
  - `fatfs_port_write_text_file_atomic()` 与 `fatfs_port_write_binary_file_atomic()` 收敛到公共 bytes 写入 helper；临时文件 rename 失败后增加直写目标文件兜底，避免 FatFs 覆盖式 rename 或旧文件残留导致 StorageAO 卡死。
  - `SCPI_STORAGE_JOB_WAIT_LOOPS` 从 2000 增加到 10000；`tools/refmem/refmem_pack_write/refmem_pack_write.py` 默认 timeout 从 3 s 增加到 10 s，防止设备端仍在合法写入时 PC 侧先超时。
  - `SCPI_REFMEM_LOAD_JOB_WAIT_LOOPS` 从 200 增加到 10000；`tools/refmem/refmem_pack_write/refmem_pack_write.py` 增加 `--load-timeout`，默认 60 s，专门覆盖 `LOAD:SD` 的长耗时 staged 路径。
  - 新增 `tools/interface/scpi_common/scpi_serial.py`，固化 USB CDC 串口打开、settle、输入/输出缓冲清理、idle-gap 读行和 finally close 生命周期管理；`scpi_query`、`storage_scpi_validate`、`refmem_pack_write` 已接入。
  - 新增 `tools/storage/storage_file_upload/storage_file_upload.py`，作为通用 Storage SCPI 文件上传工具，支持多文件、自动建目录、分片写入、写后 `FILE:INFO?` 校验。
- 验证结果：
  - `python -m py_compile tools\scpi_common\scpi_serial.py tools\storage_file_upload\storage_file_upload.py tools\refmem_pack_write\refmem_pack_write.py tools\scpi_query\scpi_query.py tools\storage_scpi_validate\storage_scpi_validate.py` 通过。
  - `python -m pytest tests\python\test_refmem_pack_build.py -q` 通过，2 passed；pytest cache 写入 `.pytest_cache` 因权限被拒绝，只产生 warning。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，最终验证 build id `20260815104540`，package CRC `0x1995734A`。
  - OTA/commit COM6 到 build `20260815104540` 通过。
  - COM6 通用 Storage CRUD 使用唯一目录重跑通过；先前 COM5 directory rename 失败确认为历史目标目录残留，不是 `RUNNING`。
  - COM6 写入 HIL manifest（只要求 profile/mission/cal/refmem 四个小文件）后，`SYSTem:SD:MANifest?` 返回 `"OK",1,"RP2350_TRIG","rp2350_trig","20260815103459",4,0`。
  - COM6 执行 `python -u tools\refmem_pack_write\refmem_pack_write.py COM6 --timeout 10 --load-timeout 60 --chunk 256 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815103459\refmem\app_model.rmtp --out-dir build-rtos-multicore-smoke\refmem_pack_write_COM6_20260815104540_load_wait_fixed` 通过；`SYSTem:REFMEM:LOAD:SD` 返回 `STAGED`，staging package CRC `2994534464`，Storage job `DONE`，错误队列为 0。
  - 发现通用 Storage FILE 写事务仍是 RAM buffer 型，`STORAGE_MANAGER_WRITE_BUFFER_MAX_BYTES=8192`，不能用该接口直接上传 523 KB OTA package；大文件 OTA 仍应走 OTA AO/portable OTA 流式入口，Storage 文件接口后续若要支持大文件需设计流式后端。
- 后续动作：
  - 将 `LOAD:SD` 37 s 耗时继续优化：优先减少逐 128 B 文件读 job 的重复调度，或在 StorageAO 提供 bounded stream read job。
  - 继续确认无参 `SYSTem:REFMEM:LOAD:SD` 的错误队列行为，区分真实 parser 污染和 PC 侧命令串扰。
- 关联文件：
  - `application/src/app_tasks.c`
  - `components/storage_manager/src/storage_manager.c`
  - `components/ui_manager/src/ui_manager.c`
  - `components/ui_manager/src/status_ui.c`
  - `middleware/fatfs_port/src/fatfs_port.c`
  - `middleware/scpi_port/src/scpi_storage_commands.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/interface/scpi_common/scpi_serial.py`
  - `tools/refmem/refmem_pack_write/refmem_pack_write.py`
  - `tools/storage/storage_file_upload/storage_file_upload.py`

### REFMEM-TASK-20260815-026 - Complete RMTP canonical table payloads

- 状态：完成代码侧基础闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `.rmtp` 中剩余 5 张 canonical 表从 64 字节占位 payload 升级为真实固定 u32 wire payload。
  - 让 `RefMemTableRegistry` 对全 9 张表执行 owner validation，同时保持 active image 切换阻断，不伪激活。
- 完成内容：
  - `tools/refmem/refmem_table_image/refmem_table_image.py` 新增 `FbInstance`、`EventLink`、`DataLink`、`DeploymentGate`、`ConnectionQuality` payload 生成。
  - `FbInstance` 的 `instance_name` 和 `DataLink` 的 `slot_path` 不写入 C 指针，`.rmtp` 使用 `name_hash` / `slot_path_hash` 作为稳定 wire 字段；可读说明仍放在 idx/json。
  - `refmem_table_registry.c` 的 table image size 改为 wire size，避免把 C struct pointer size 当作 package layout。
  - `RefMemTableRegistry` 增加 4-8 号表 parser 和字段级 owner validation，成功 package 的 `owner_validated_table_mask` 覆盖 `REFMEM_APP_TABLE_MASK_ALL`。
  - 单元测试构造器从“前 4 张真实 + 后 5 张 placeholder”升级为全 9 张 contract package；staging descriptor 预期从 `CRC_OK` 更新为 `OWNER_OK`。
- 验证结果：
  - `python -m py_compile tools\refmem_table_image\refmem_table_image.py tools\refmem_pack_build\refmem_pack_build.py tools\sd_fs_build\sd_fs_build.py` 通过。
  - `python -m pytest tests\python\test_refmem_pack_build.py -q` 通过，2 passed；pytest cache 写入 `.pytest_cache` 因权限被拒绝，只产生 warning。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `python tools\refmem_pack_build\refmem_pack_build.py --output-dir build-rtos-multicore-smoke\refmem_pack_full_tables` 生成 9 表 package，size `4800`，CRC `0xEFAF178F`。
- 后续闭环：
  - 继续运行完整 host/unit/docs/build/HIL 验证；通过后再进入真实 staging/active/rollbackable image buffer。
- 关联文件：
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tools/refmem/refmem_table_image/refmem_table_image.py`
  - `tools/refmem/refmem_pack_build/refmem_pack_build.py`
  - `tests/unit/test_refmem_table_registry.c`
  - `tests/python/test_refmem_pack_build.py`

### REFMEM-TASK-20260815-025 - Block descriptor-only table activation

- 状态：完成基础纠偏
- 日期：2026-08-15
- 任务目标：
  - 消除 `RefMemTableRegistry` 只切 descriptor/CRC 就宣称 active image 已替换的架构风险。
  - 在真实 active/staging/rollbackable table buffer 未落地前，明确阻断 activation。
- 完成内容：
  - 新增 `REFMEM_TABLE_ACTIVATE_ERR_IMAGE_NOT_LOADED`。
  - `refmem_table_registry_activate_staging()` 在 validated staging 和 activation gate 都通过后，仍返回 false，保留 staging descriptor，并写入 `IMAGE_NOT_LOADED`；不修改 active descriptor、rollbackable descriptor 或 active entry CRC。
  - 单元测试从“成功切 descriptor”改为验证“真实 table image 未加载时拒绝伪激活”。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815085709`，package CRC `0x1C61EB50`。
  - COM5 执行 `python tools\ota_boot_commit\ota_boot_commit.py COM5 --expected-build 20260815085709 --out-dir build-rtos-multicore-smoke\ota_commit_COM5_20260815085709` 通过，`SYSTem:FW:BUILD?` 返回 `20260815085709`，`SYSTem:OTA:COMMit` 返回 `"OK"`，错误队列为 `0,"No error"`。
  - COM6 执行 `python tools\ota_boot_commit\ota_boot_commit.py COM6 --expected-build 20260815085709 --out-dir build-rtos-multicore-smoke\ota_commit_COM6_20260815085709` 通过，`SYSTem:FW:BUILD?` 返回 `20260815085709`，`SYSTem:OTA:COMMit` 返回 `"OK"`，错误队列为 `0,"No error"`。
- 后续闭环：
  - 后续必须引入真实 staging/active/rollbackable table image buffer 和业务表切换后，才能恢复 activation 成功路径。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_table_registry.h`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tests/unit/test_refmem_table_registry.c`

### REFMEM-TASK-20260815-024 - LOAD:SD via RefMem command slot

- 状态：完成首版闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `SYSTem:REFMEM:LOAD:SD` 的 RefMem staging 和 TableRegistry package validation 收敛到 `DistributedRefMemAO` command slot。
  - 保持 StorageAO/RefMemAO 职责边界：SD/FatFs/manifest/file read 属于 StorageAO，RefMem staging/ACK/NACK 属于 RefMemAO。
- 完成内容：
  - 新增 `REFMEM_COMMAND_TYPE_TABLE_PACKAGE_STAGE` 和 `distributed_refmem_stage_sd_system_pack()`。
  - `SYSTem:REFMEM:LOAD:SD` 改为先通过 StorageManager 获取 manifest/package validation 摘要，再调用 RefMem intent API；SCPI 不再直接调用 `refmem_application_model_stage_sd_system_pack()` 或 `refmem_table_registry_stage_package_validation()`。
  - Storage 前置 job busy/incomplete 也转换为 `REJECTED` load snapshot 和 command NACK，避免写命令无 completion。
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 增加 `LOAD:SD` 后的 `SYSTem:COMMand:ACK?` 验证，确认 command type 为 `TABLE_PACKAGE_STAGE`。
- 验证结果：
  - `python -m py_compile tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py` 通过。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815084755`，package CRC `0xE5D9FCC9`。
  - COM5/COM6 OTA 写入、boot 和 commit 到 build `20260815084755` 通过，错误队列均为 `0,"No error"`。
  - COM5 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM5 --out-dir build-rtos-multicore-smoke\refmem_load_COM5_20260815084755_sd_command_slot` 通过；当前 SD package 未有效 staging，`LOAD:SD` 返回 `REJECTED`，`SYSTem:COMMand:ACK?` 返回 `TABLE_PACKAGE_STAGE` NACK。
  - COM6 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM6 --out-dir build-rtos-multicore-smoke\refmem_load_COM6_20260815084755_sd_command_slot` 通过；结果同 COM5。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
- 后续闭环：
  - 继续扩展 SD HIL：覆盖无 SD、manifest 缺失、manifest OK 且 package valid 三类路径，并校验 TableRegistry per-table staging CRC。
  - `LOAD:SD` 仍不执行 active image 切换；P0 仍需实现真实 active/staging/rollbackable table image。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_command.h`
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py`

### REFMEM-TASK-20260815-023 - LOAD:BOARD via RefMem command slot

- 状态：完成首版闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `SYSTem:REFMEM:LOAD:BOARD` 从 SCPI 直接调用 BoardCapability staging API 收敛到 `DistributedRefMemAO` command slot / staging 路径。
  - 验证 `board_id` 只作为板卡/profile 候选 payload，不被误用为 A0-A7 target slot。
- 完成内容：
  - 新增 `REFMEM_COMMAND_TYPE_BOARD_CAPABILITY_STAGE` 和 `distributed_refmem_stage_board_capability()`。
  - `SYSTem:REFMEM:LOAD:BOARD` 改为调用 RefMem intent API；`DistributedRefMemAO` 原子清理已完成 command、post `BOARD_CAPABILITY_STAGE`、由本地 RefMem owner take、调用 `refmem_application_model_stage_scpi_board_capability()`，随后 ACK/NACK。
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 增加 BoardCapability 正向/负向验证：有效 board 候选应 ACK，非法 `board_id=16` 应 NACK。
  - 修正验证脚本参数格式：当前 SCPI 数字解析不接受 `0x...`，脚本改为十进制参数，避免把 parser `-101 Invalid character` 误判为 RefMem 卡死。
- 验证结果：
  - `python -m py_compile tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py` 通过。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815082900`，package CRC `0xF913F583`。
  - COM5/COM6 OTA 写入、boot 和 commit 到 build `20260815082900` 通过，错误队列均为 `0,"No error"`。
  - COM5 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM5 --skip-sd --out-dir build-rtos-multicore-smoke\refmem_load_COM5_20260815082900_board_command_slot_r3` 通过。
  - COM6 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM6 --skip-sd --out-dir build-rtos-multicore-smoke\refmem_load_COM6_20260815082900_board_command_slot_r3` 通过。
- 后续闭环：
  - `LOAD:BOARD` 仍是单条 staging snapshot，不是完整 BoardCapabilityTable active/staging/rollbackable image；P0 仍需升级为真实表镜像。
  - `SYSTem:REFMEM:LOAD:SD` 仍需接入 command slot，并保持 StorageAO/RefMemAO 职责分离。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_command.h`
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py`

### REFMEM-TASK-20260815-022 - LOAD:NODE via RefMem command slot

- 状态：完成首版闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `SYSTem:REFMEM:LOAD:NODE` 从 SCPI 直接调用 application staging API 收敛到 `DistributedRefMemAO` command slot / staging 路径。
  - 验证 `SYSTem:COMMand:ACK?` 能直接观察 `LOAD:NODE` 的 `NODE_LOAD_STAGE` completion。
- 完成内容：
  - 新增 `distributed_refmem_stage_node_load()`，由 RefMemAO 原子清理已完成 command、post `NODE_LOAD_STAGE`、take target slot、调用 `refmem_application_model_stage_scpi_node_config()`，随后 ACK/NACK。
  - 修复 command slot 抢占竞态：`clear completed command -> allocate seq -> post new command` 现在在 RefMemAO 内部同一个 critical section 完成，避免 `system_manager` 在间隙重新发布 `CONFIG_ACTIVATE`。
  - `SYSTem:REFMEM:LOAD:NODE` 改为调用 RefMem intent API；返回字段保持原 `STAGED/REJECTED + LOAD:STATus` 格式。
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 增加 `SYSTem:COMMand:ACK?` 校验，并把 table count / active mask 更新为当前 9 张表 / `0x1FF`。
- 验证结果：
  - `python -m py_compile tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py` 通过。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815081416`，package CRC `0x5D6D4C16`。
  - COM5/COM6 OTA 写入、boot 和 commit 到 build `20260815081416` 通过，错误队列均为 `0,"No error"`。
  - COM5 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM5 --skip-sd --out-dir build-rtos-multicore-smoke\refmem_load_node_COM5_20260815081416_command_slot` 通过。
  - COM6 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM6 --skip-sd --out-dir build-rtos-multicore-smoke\refmem_load_node_COM6_20260815081416_command_slot` 通过。
  - COM5/COM6 再次执行 `tools\model_turntable_load_validate\model_turntable_load_validate.py` 通过，确认共享 post helper 未破坏 `CONFigure:MODEl:TURNtable:LOAD`。
- 后续闭环：
  - `LOAD:NODE` 仍是单条 staging snapshot，不是完整 NodeLoadTable active/staging/rollbackable image；P0 仍需升级为真实表镜像。
  - `SYSTem:REFMEM:LOAD:SD` 和 `SYSTem:REFMEM:LOAD:BOARD` 仍需接入 command slot。
- 关联文件：
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py`

### REFMEM-TASK-20260815-021 - Generic command ACK/NACK SCPI view

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 增加通用 command slot 维护视图，避免继续把 `SYSTem:CONFigure:ACK?` 当成所有 command 的查询入口。
  - 保持单一 ACK/NACK 事实源：新接口直接读取 `DistributedRefMemAO` 的 `AckCommandSlot` snapshot，不另建状态。
- 完成内容：
  - 新增 `SYSTem:COMMand:ACK?`，返回 schema version、state、command seq、source/target、command type/class、payload 摘要、taken/ACK/NACK/busy/timeout flags、last reason、evidence、clear seq 和 last completed seq。
  - 新增 `SYSTem:COMMand:NACK? [reason_id]`，默认读取当前 command snapshot 的 `last_reason`，也可按 reason id 查询 RefMem command reason 表。
  - `SYSTem:CONFigure:ACK? / NACK?` 继续只作为 `CONFIG_ACTIVATE` 的配置门禁兼容视图。
  - `tools/trigger/model_turntable_load_validate/model_turntable_load_validate.py` 增加 `SYSTem:COMMand:ACK?` 和 `SYSTem:COMMand:NACK?` 检查，验证 `NODE_LOAD_STAGE` command 的 ACK 事实。
- 验证结果：
  - `python -m py_compile tools\model_turntable_load_validate\model_turntable_load_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `python tools\docs_check\docs_check.py` 通过。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815080110`，package CRC `0x093AC676`。
  - COM5/COM6 OTA 写入、boot 和 commit 到 build `20260815080110` 通过，错误队列均为 `0,"No error"`。
  - COM5 执行 `python tools\model_turntable_load_validate\model_turntable_load_validate.py COM5 --expected-build 20260815080110 --slot 1 --output 0 --out-dir build-rtos-multicore-smoke\model_turntable_load_COM5_20260815080110_command_ack` 通过。
  - COM6 执行 `python tools\model_turntable_load_validate\model_turntable_load_validate.py COM6 --expected-build 20260815080110 --slot 1 --output 0 --out-dir build-rtos-multicore-smoke\model_turntable_load_COM6_20260815080110_command_ack` 通过。
- 后续闭环：
  - 后续 START/STOP、activation、resource job 接入 command slot 后，统一用 `SYSTem:COMMand:*` 验证 command completion；配置上位机仍可读取 `SYSTem:CONFigure:*` 兼容视图。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/trigger/model_turntable_load_validate/model_turntable_load_validate.py`

### REFMEM-TASK-20260815-020 - ModelTurntable LOAD via RefMem command slot

- 状态：完成首版闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `CONFigure:MODEl:TURNtable:LOAD <slot_id>,<output_index>` 从 SCPI 直接业务调用收敛到 RefMem command slot / NodeLoad staging 路径。
  - 保持 HAOFV 边界：SCPI 只提交 intent；`DistributedRefMemAO` 负责 command、staging 和 ACK/NACK；实际加载由 `ModelTurntableAO` registered owner 执行。
- 完成内容：
  - 新增 `distributed_refmem_register_node_load_owner()`，按 `instance_id` 注册 AO/FB owner 回调；`distributed_refmem.c` 不再直接 include `model_turntable.h`。
  - `ModelTurntableAO` 在初始化时注册 `Template.ModelTurntableAO` 的 NodeLoad owner，owner 回调内部执行 `model_turntable_load()`。
  - `CONFigure:MODEl:TURNtable:LOAD` 改为调用 `distributed_refmem_stage_model_turntable_load()`；该入口 post `NODE_LOAD_STAGE` command、写入 NodeLoad staging snapshot、由 owner 执行加载，并 ACK/NACK command slot。
  - `system_manager` 的 `SYSTem:CONFigure:ACK?` 兼容视图只读取 `CONFIG_ACTIVATE` command，避免模型 LOAD command 污染配置门禁 ACK。
  - 新增 `tools/trigger/model_turntable_load_validate/model_turntable_load_validate.py`，固化 build id、默认未加载、LOAD、staging snapshot、配置 ACK 兼容视图和错误队列检查。
- 验证结果：
  - `python -m py_compile tools\model_turntable_load_validate\model_turntable_load_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815075135`，package CRC `0x3F00DD7D`。
  - COM5/COM6 OTA 写入、boot 和 commit 到 build `20260815075135` 通过，错误队列均为 `0,"No error"`。
  - COM5 执行 `python tools\model_turntable_load_validate\model_turntable_load_validate.py COM5 --expected-build 20260815075135 --slot 1 --output 0 --out-dir build-rtos-multicore-smoke\model_turntable_load_COM5_20260815075135` 通过。
  - COM6 执行 `python tools\model_turntable_load_validate\model_turntable_load_validate.py COM6 --expected-build 20260815075135 --slot 1 --output 0 --out-dir build-rtos-multicore-smoke\model_turntable_load_COM6_20260815075135` 通过。
- 后续闭环：
  - 当前仍是首版 staging snapshot，不是完整 NodeLoadTable active/staging/rollbackable image；P0 仍需把 `CONFigure:MODEl:TURNtable:LOAD` 升级为真实 table image、owner validation、SlotClaimMap、RealtimeCapabilityContract、DeploymentGate 和 rollback。
  - 将 `ModelVnaAO`、`LinkSwitcherAO` 等其余可加载实例接入同一 NodeLoad owner 注册和 command slot 路径。
- 关联文件：
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/model_turntable/src/model_turntable.c`
  - `components/system_manager/src/system_manager.c`
  - `middleware/scpi_port/src/scpi_model_commands.c`
  - `tools/trigger/model_turntable_load_validate/model_turntable_load_validate.py`

### REFMEM-TASK-20260815-019 - SystemManager config ACK maps to RefMem command slot

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 消除 `SYSTem:CONFigure:ACK?` 由 SystemManager 本地字段单独拼装造成的 ACK/NACK 事实分裂。
  - 保持 SCPI 兼容查询不变，同时让底层 ACK/NACK 来自 RefMem `AckCommandSlot` snapshot。
- 完成内容：
  - `DistributedRefMemAO` 增加 command slot owner API：reason table CRC、post、ACK、NACK、timeout、clear 和 snapshot。
  - `system_manager` 在配置 RUN gate 初始化和 service 中发布 `CONFIG_ACTIVATE` command snapshot，并按 gate 结果写入 ACK 或 NACK。
  - 修正 quality gate 变坏时 ACK/NACK flags 可能仍停留在旧全 ACK 的问题；现在 reject 时 `SYSTem:CONFigure:ACK?` 返回 `ack=0,nack=target`。
  - `tools/refmem/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py` 增加 config ACK 正向、负向和恢复后的全 ACK 检查。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过 host GCC 断言执行。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815061144`，package CRC `0xAB77E7E3`。
  - COM5/COM6 OTA 到 build `20260815061144` 并 commit 通过。
  - COM5/COM6 执行 `multicore_board_validate.py --tests build_id config_gate_status ack_reason_and_run_policy` 均通过。
  - COM5 执行 `refmem_quality_gate_hil_validate.py` 通过，确认 TDMA timeout 后 config gate reject、config ACK reject，并通过 OTA 恢复 ready / full ACK。
  - COM6 在 COM5 负向验证后再次执行配置 ACK 相关 HIL 查询通过。
- 后续闭环：
  - 将 `CONFigure:MODEl:*:LOAD` 接入 command slot，让 SCPI 只 post `NODE_LOAD_STAGE` 或 `CONFIG_STAGE`。
  - 评估是否新增 `SYSTem:COMMand:ACK? / NACK?` 作为通用 command slot 维护视图。
- 关联文件：
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/distributed_refmem/inc/refmem_command.h`
  - `components/distributed_refmem/src/refmem_command.c`
  - `components/system_manager/src/system_manager.c`
  - `tools/refmem/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py`

### REFMEM-TASK-20260815-018 - Command slot foundation

- 状态：完成基础件
- 日期：2026-08-15
- 任务目标：
  - 将 `AckCommandSlot` 从文档契约落到可执行 C 基础件。
  - 为后续 `CONFigure:MODEl:*:LOAD`、NodeLoad staging、配置 activation 和跨节点 ACK/NACK 收敛提供统一数据面。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_command.h` 和 `components/distributed_refmem/src/refmem_command.c`。
  - 实现 `try_post`、`try_take`、`ack`、`nack`、`mark_timeout`、`clear` 和 seqlock snapshot。
  - 字段与 `REFMEM_COMMAND` / `REFMEM_ACK_NACK` payload 对齐，提供 `refmem_command_to_sync_command_payload()` 与 `refmem_command_to_sync_ack_payload()`。
  - TAKE 只做目标、epoch/run_id 和 payload CRC 检查；耗时动作仍必须由 AO/FB service tick 执行，避免违反 HAOFV owner 边界。
  - 新增 `tests/unit/test_refmem_command.c` 和 `tools/tests/run_refmem_command_tests.ps1`，并纳入 `tools/tests/run_host_unit_tests.ps1`。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过 host GCC 断言执行。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815060551`，package CRC `0x06B0C911`。
- 后续闭环：
  - 将模型加载动作接入 command slot：SCPI accepted 后只 post `NODE_LOAD_STAGE` 或 `CONFIG_STAGE`，由 RefMem owner ACK/NACK。
  - 将现有 `system_manager` 配置 ACK 映射到同一 command snapshot，避免 ACK/NACK 事实分裂。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_command.h`
  - `components/distributed_refmem/src/refmem_command.c`
  - `tests/unit/test_refmem_command.c`
  - `tools/tests/run_refmem_command_tests.ps1`

### REFMEM-TASK-20260815-017 - Quality gate negative HIL script

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 将 `TDMA timeout -> RefMem quality gate reject -> SystemManager config gate reject -> OTA restore` 固化成可重复 HIL 脚本。
  - 避免后续用手工 SCPI 命令制造 TDMA timeout 后忘记恢复板卡运行态。
- 完成内容：
  - 新增 `tools/refmem/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py`。
  - 脚本读取初始 `SYSTem:CONFigure:STAT?` 和 `SYSTem:REFMEM:SYNC:TDMA:STATus?`，确认初始 config gate ready。
  - 脚本发送短窗口 `SYSTem:REFMEM:SYNC:TDMA:RX` 且不发 TX，轮询 TDMA timeout，并确认 `SYSTem:CONFigure:STAT?` 进入 `ready=0, gate_state=2`。
  - 支持 `--restore-package` 和 `--expected-build`，在负向验证后调用 OTA send + boot/commit 恢复板卡，并再次确认 config gate ready。
  - 修正 `SYSTem:CONFigure:STAT?` 解析：首字段 build id 是带引号数字字符串，不能被当成 ready 字段。
- 验证结果：
  - `python -m py_compile tools\refmem_quality_gate_hil_validate\refmem_quality_gate_hil_validate.py` 通过。
  - COM5 执行 `python tools\refmem_quality_gate_hil_validate\refmem_quality_gate_hil_validate.py COM5 --expected-build 20260815053147 --restore-package build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --out-dir build-rtos-multicore-smoke\refmem_quality_gate_COM5_20260815053147_scripted_r3` 通过。
  - 报告路径：`build-rtos-multicore-smoke\refmem_quality_gate_COM5_20260815053147_scripted_r3\summary.json`。
- 关联文件：
  - `tools/refmem/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py`

### REFMEM-TASK-20260815-016 - Quality gate RUN gate integration

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 将 `refmem_quality_evaluate_deployment_gate()` 接入产品 config RUN gate。
  - 保持 `SystemManager` 只消费 `DistributedRefMemAO` 的 quality gate 结果，不直接读取 TDMA mailbox 或 physical adapter counter。
- 完成内容：
  - `DistributedRefMemAO` 增加 `distributed_refmem_quality_gate_ready()`，内部从 core1 TDMA snapshot 派生 runtime quality entry，并调用 quality gate evaluator。
  - `system_manager` 的 config gate 同时检查 distributed config、SlotClaim gate 和 RefMem quality gate；任一失败时 `ready=0`、`gate_state=2`、ACK 清空/NACK target mask。
  - 修复 `distributed_refmem.h` 与 `refmem_quality.h` 的 include 循环，公开 API 只暴露布尔 quality gate，不泄漏 quality table 内部类型。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815053147`，package CRC `0x190D53A9`。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过 host GCC 断言执行。
  - COM5/COM6 OTA 提交通过，均返回 `SYSTem:FW:BUILD? => "20260815053147"`。
  - 正向：COM5/COM6 查询 `SYSTem:CONFigure:STAT?` 均返回 `ready=1, gate_state=1, target_mask=15, ack_flags=15, nack_flags=0`。
  - 负向：COM5 执行 `SYSTem:REFMEM:SYNC:TDMA:RX 1000,25000000,16,17,18,23` 且不发 TX 后，`TDMA:STATus?` 显示 `state=5, timeout_count=1, last_result=3, last_error=3`，`SYSTem:CONFigure:STAT?` 变为 `ready=0, gate_state=2`。
  - 恢复：COM5 重新 OTA 同一 package 后恢复 `ready=1, gate_state=1`；COM6 保持 `ready=1, gate_state=1`。
- 后续闭环：
  - quality gate 负向流程已在 `REFMEM-TASK-20260815-017` 固化为 HIL 脚本。
- 关联文件：
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/system_manager/src/system_manager.c`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`

### REFMEM-TASK-20260815-015 - Quality gate evaluator

- 状态：完成基础件，SystemManager 接入待继续
- 日期：2026-08-15
- 任务目标：
  - 在 TDMA runtime quality entry 已进入 `DistributedConnectionQualityTable` 派生视图后，提供表驱动的 `DeploymentGate.QUALITY` 消费入口。
  - 避免 RUN gate 直接遍历 adapter、core1 mailbox 或 SCPI 查询结果。
- 完成内容：
  - `refmem_quality.h/.c` 增加 `refmem_quality_gate_threshold_t`、`refmem_quality_gate_reason_t` 和 `refmem_quality_evaluate_deployment_gate()`。
  - evaluator 输入 `refmem_quality_runtime_table_t` 和阈值，输出 `refmem_deployment_gate_entry_t`，包含 `check_id=REFMEM_APP_GATE_QUALITY`、pass/reject 状态、reject reason、node/slot 和 evidence index。
  - 当前 reason 覆盖 bad argument、empty runtime table、CRC、stale、late、drop、timeout 和 last_error。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 明确 `DeploymentGate.QUALITY` 只能消费 quality evaluator 输出，不能直接读取底层 adapter 或 core1 mailbox。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过 host GCC 断言执行。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815052521`，package CRC `0x346FB00F`。
- 后续闭环：
  - `system_manager` config RUN gate 接入和 TDMA timeout 板端负向验证已在 `REFMEM-TASK-20260815-016` 完成。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_quality.h`
  - `components/distributed_refmem/src/refmem_quality.c`
  - `tests/unit/test_refmem_quality.c`

### REFMEM-TASK-20260815-014 - TDMA quality runtime mapping

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 将 core1 realtime TDMA service 的 window timeout、DMA/physical overrun、missed window 和 physical adapter error 纳入 `DistributedConnectionQualityTable` 派生运行态视图。
  - 保持 active static `ConnectionQualityTable` 只作为契约/CRC 来源，不被 SCPI 或维护 bridge 热写。
- 完成内容：
  - `refmem_quality.h/.c` 增加 TDMA service runtime entry，`quality_id=0x54444D41`，scope 为 `REFMEM_APP_QUALITY_TRANSPORT_ADAPTER`。
  - 映射规则：TDMA `timeout_count -> timeout_count`，`reject_count -> late_count`，`overrun_count -> drop_count`，`last_error -> last_error`，`intent_seq/completed_seq -> seq_expected/seq_last`。
  - `SYSTem:REFMEM:QUALity?` 查询时读取 `distributed_refmem_get_realtime_tdma()` snapshot，并把 TDMA entry 放在 index 1；index 0 仍为本地 PIO SPI adapter 诊断 entry，后续 remote QUALITY entry 依次追加。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 补齐 `TRANSPORT_ADAPTER` scope 和 TDMA 派生映射规则。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过 host GCC 断言执行。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过 host GCC 断言执行。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815051252`，package CRC `0x5B12BF64`。
  - COM5/COM6 OTA 提交通过，均返回 `SYSTem:FW:BUILD? => "20260815051252"`。
  - COM5/COM6 查询 `SYSTem:REFMEM:QUALity? 1` 可见 TDMA runtime entry；HIL 前 entry_count 为 2，HIL 后 entry_count 为 3（追加 remote QUALITY entry）。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --transport tdma --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815051252_tdma_quality_mapping` 通过。
- 还需完成：
  - 将 TDMA quality entry 接入 DeploymentGate/RUN gate evidence 消费规则。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_quality.h`
  - `components/distributed_refmem/src/refmem_quality.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_quality.c`

### REFMEM-TASK-20260815-013 - P4.5 SPI frame legacy removal

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 在 TDMA HIL 通过后删除 `SYSTem:REFMEM:SYNC:SPI:*` 的帧级阻塞命令，避免后续继续通过 core0/SCPI 直驱 RefMem frame transport。
  - 保留 `SPI:ARM/DISarm/STATus?/LINE:*/RAW:*` 作为 bring-up 诊断入口。
- 完成内容：
  - 从 SCPI 命令表删除 `SYSTem:REFMEM:SYNC:SPI:RX?`、`SPI:HELLo`、`SPI:EPOCh`、`SPI:DELTa`、`SPI:ACK`、`SPI:FENCe`、`SPI:QUALity:FRAMe`。
  - 删除上述命令实现和 `scpi_refmem_sync_spi_send_result()`，消除 core0 直发 RefMem frame 的维护路径。
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 改为 TDMA-only frame transport；RAW 仍作为线序/物理链路诊断。
  - HIL frame plan 改为直接使用 `SYSTem:REFMEM:SYNC:HELLo?/EPOCh?/DELTa?/ACK?/FENCe?/QUALity:FRAMe?` 生成 frame，再通过 `TDMA:TX/RX/FRAMe?` 完成两板交换。
- 验证结果：
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815045059`，package CRC `0x732BD11F`。
  - COM5/COM6 OTA 提交通过，均返回 `SYSTem:FW:BUILD? => "20260815045059"`。
  - Negative check：`SYSTem:REFMEM:SYNC:SPI:HELLo` 和 `SYSTem:REFMEM:SYNC:SPI:RX?` 已返回 `Undefined header`，确认旧 SPI frame 入口不再注册。
  - Positive check：`SYSTem:REFMEM:SYNC:SPI:ARM`、`SYSTem:REFMEM:SYNC:SPI:RAW:TX` 和 `SYSTem:REFMEM:SYNC:TDMA:STATus?` 仍可用。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --transport tdma --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815045059_tdma_spi_frame_removed` 通过。
  - HIL 报告路径：`build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815045059_tdma_spi_frame_removed\refmem_spi_hil_report.json`。
- 后续闭环：
  - TDMA window timeout、DMA overrun、missed window 和 physical adapter error 到 `DistributedConnectionQualityTable` 的正式映射已在 `REFMEM-TASK-20260815-014` 完成。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py`

### REFMEM-TASK-20260815-012 - P4.5 COM5/COM6 core1 TDMA HIL 闭环

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 将 RefMem Sync 的两板物理验证从 SCPI 阻塞帧级命令收敛到 core1 realtime TDMA service。
  - 证明 PC 只投递 RX/TX intent 和读取 snapshot，`HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 帧通过真实 PIO+DMA 物理链路完成。
- 完成内容：
  - 修正 `SYSTem:REFMEM:SYNC:TDMA:TX`：SCPI 层只解码 hex 并提交 TDMA intent，不再在 core0 直接调用 physical adapter 发送帧。
  - 固化 HIL 脚本的 TDMA hex 字符串参数，避免未加引号的 A-F hex 被 SCPI lexer 当作数字/指数解析。
  - 将 `refmem_spi_physical_adapter_receive()` 拆成 begin/poll 异步 RX 基础能力；blocking receive 保持兼容并复用同一解析逻辑。
  - 将 TDMA RX 从长时间阻塞 core1 改为每轮 core1 loop poll 一次，`PENDING` 时保持 ARMED/ACCEPTED 快照，不提前写 completed_seq。
  - 将 `REFMEM_REALTIME_TDMA_FRAME_MAX` 对齐到 RefMem Sync 协议 MTU：`REFMEM_SYNC_FRAME_HEADER_SIZE + REFMEM_SYNC_FRAME_PAYLOAD_MAX`。
  - HIL 脚本在 TDMA exchange 前清理两端上一次 intent，并用 `TDMA:STATus?` 做 receiver arm gate。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815043100`，package CRC `0x49E7D77C`。
  - COM5/COM6 OTA 提交通过，均返回 `SYSTem:FW:BUILD? => "20260815043100"`。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --transport tdma --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815043100_tdma_mtu` 通过。
  - HIL 报告路径：`build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815043100_tdma_mtu\refmem_spi_hil_report.json`。
  - 报告中 `A_RAW_B`、`B_RAW_A`、双向 `HELLO/EPOCH/DELTA/ACK/FENCE/QUALITY` 全部 passed；最终 TDMA snapshot 显示 A/B `ready_count=12`、`timeout_count=0`、`overrun_count=0`、`reject_count=0`。
- 还需完成：
  - 将 `SYSTem:REFMEM:SYNC:SPI:*` 帧级阻塞命令降级为 legacy/diagnostic 或删除，仅保留 line/raw bring-up 必要入口。
  - 将 TDMA window timeout、DMA overrun、missed window 和 physical adapter error 正式映射到 `DistributedConnectionQualityTable`。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_realtime_tdma.h`
  - `components/distributed_refmem/inc/refmem_spi_physical_adapter.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/distributed_refmem/src/refmem_realtime_tdma.c`
  - `components/distributed_refmem/src/refmem_spi_physical_adapter.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py`
- 下一步：
  - 收敛 `SYSTem:REFMEM:SYNC:SPI:*` 帧级阻塞命令，并把 TDMA 质量计数并入 RefMem quality/evidence。

### REFMEM-TASK-20260815-011 - P4.5 core1 TDMA service contract 收敛

- 状态：完成 service contract、physical ops 绑定与构建闭环；SCPI 维护路径迁移和 HIL 待继续
- 日期：2026-08-15
- 任务目标：
  - 将当前 PIO+DMA physical adapter 从“只有 SCPI 维护命令能表达状态”向 HAOFV 的 RefMemAO/core1 realtime TDMA service 边界收敛。
  - 先建立 core0 intent 与 core1 result 的字段归属，避免跨核共享状态双 writer。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_realtime_tdma.h` 和 `components/distributed_refmem/src/refmem_realtime_tdma.c`。
  - service 使用双 guard：core0 writer 只更新 intent mailbox、frame staging 和 reject counter；core1 writer 只更新 runtime/result snapshot、completed seq、ready/timeout/overrun/error counter。
  - TDMA service 增加 physical ops 边界，避免把具体总线实现写死在基础状态机中；host 单测使用 fake ops，固件使用 PIO+DMA physical adapter wrapper。
  - `DistributedRefMemAO` 绑定 `refmem_spi_physical_adapter_transmit/receive`，core1 service 可按 intent role、baud 和 deadline 调用真实 PIO+DMA TX/RX。
  - TDMA service 增加 RX result frame 缓存和只读 API；收到的 frame 不写入向量表大数据，后续由 RefMem Sync owner 读取并进入协议 decode/quality。
  - `distributed_refmem_init()` 初始化 TDMA service，`app_realtime_run_once()` 通过 `distributed_refmem_realtime_run_once()` 在 core1 循环中推进。
  - 新增维护查询 `SYSTem:REFMEM:SYNC:TDMA:STATus?`，只读取 snapshot，不直接驱动硬件动作。
  - 新增维护入口 `SYSTem:REFMEM:SYNC:TDMA:TX/RX/FRAMe?/ABORt`：`TX/RX` 只 post intent，`FRAMe?` 读取 core1 result frame 并交给 RefMem Sync receive state machine。
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 增加 `--transport tdma`，在现有线序 remap 和 frame plan 基础上改走 TDMA intent 路径；RAW 仍保留为 SPI line diagnostic。
  - 新增 `tests/unit/test_refmem_realtime_tdma.c` 和 `tools/tests/run_refmem_realtime_tdma_tests.ps1`。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过 host gcc 断言执行。
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815040106`，package CRC `0xE67D0C6D`。
- 后续：
  - 用 COM5/COM6 执行 `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --transport tdma --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_tdma`。
  - TDMA HIL 通过后再收敛或删除 `SYSTem:REFMEM:SYNC:SPI:*` 的帧级阻塞命令。

### REFMEM-TASK-20260815-010 - P4.5 PIO+DMA physical adapter 25 MHz 闭环

- 状态：完成当前 HIL；core1 TDMA service 化待继续
- 日期：2026-08-15
- 任务目标：
  - 按 VDC/TDMA 架构纠偏 P4.5 physical adapter：不得使用 RP2350 内置 SPI，也不得把 SIO bitbang 当作闭环结论。
  - 使用真实线序检测结果驱动 PIO SPI pin plan，在 COM5/COM6 两块最小系统板之间完成真实物理链路 RefMem Sync 帧闭环。
  - 目标速率提升到 25 MHz，避免继续以低速 PC 搬运或逐字节轮询掩盖架构问题。
- 完成内容：
  - 新增 `components/distributed_refmem/src/refmem_spi_physical.pio`，提供 PIO TX byte 和 PIO RX byte state machine。
  - `refmem_spi_physical_adapter.c` 从内置 `spi0`/SIO bitbang 纠偏为 PIO TX、PIO RX 和 DMA RX 缓冲；CPU 不再逐 bit 或逐字节搬运 RX FIFO。
  - `board_config.h` 增加 RefMem PIO transport 的 PIO/SM 资源定义，默认 baud 改为 25 MHz。
  - `tools/sync/two_board_io_validate/two_board_io_validate.py` 支持 auto/detect 模式，并把 observed remap 写入报告。
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 默认使用 auto remap 和 25 MHz，根据实测 OUT->IN map 推导双向 PIO SPI pin plan。
  - 新增 `tools/refmem/refmem_spi_line_activity/refmem_spi_line_activity.py`，用于慢速长 RAW:TX 时观察对端输入 mask，证明 PIO TX 已在真实线束上活动。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，build `20260815031915`，package CRC `0xD5A25528`。
  - COM5/COM6 均 OTA 写入、boot 并 commit 到 build `20260815031915`。
  - `python tools\refmem_spi_line_activity\refmem_spi_line_activity.py --sender COM5 --receiver COM6 --sender-to-receiver 1,2,0,3 --baud 1000 --count 256 --seed 255` 显示 B 侧 IN0/IN2 有高低变化，证明 PIO TX 的 RX/SCK 已过线。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --baud 1000000 --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815031915_pio_dma_1m` 通过。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815031915_pio_dma_25m` 通过。
  - 25 MHz 报告记录：线序 preflight PASS，A->B remap `[1,2,0,3]`，B->A remap `[2,1,0,3]`；`RAW/HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 14 个 exchange 全部 PASS；`failures=[]`。
- 还需完成：
  - 当前 HIL 仍由 SCPI 在帧级触发 TX/RX。产品化下一步必须把 PIO+DMA physical adapter 接入 core1 realtime TDMA service，core0 只配置窗口和帧意图，core1 发布 frame-ready/timeout/overrun snapshot。
  - PIO/SM/DMA 资源需要接入 ResourceArbiter 和 RealtimeCapabilityContract，避免维护 HIL 直接抢占现有 `sync_io` 资源。
- 关联文件：
  - `components/distributed_refmem/src/refmem_spi_physical.pio`
  - `components/distributed_refmem/src/refmem_spi_physical_adapter.c`
  - `components/distributed_refmem/inc/refmem_spi_physical_adapter.h`
  - `boards/rp2350_trig/inc/board_config.h`
  - `tools/sync/two_board_io_validate/two_board_io_validate.py`
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py`
  - `tools/refmem/refmem_spi_line_activity/refmem_spi_line_activity.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`

### REFMEM-TASK-20260815-009 - P4.5 SPI physical adapter 首次接入与线级阻塞定位

- 状态：部分完成，HIL 阻塞在物理线级 preflight
- 日期：2026-08-15
- 任务目标：
  - 按 P4.5 要求，把 RefMem Sync 从 PC/SCPI hex 搬运推进到真实两板 SPI 物理链路。
  - 保持 HAOFV 边界：SCPI 只作为维护触发和诊断入口，RefMem frame 仍由 RefMem Sync validate/receive 处理，不让 Vector 承载数据。
  - 在 COM5/COM6 两块最小系统板上验证 `HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 真实过线。
- 完成内容：
  - 新增 `refmem_spi_physical_adapter.h/.c`，使用 `spi0` 的 debug bring-up profile：RX16、CS17、SCK18、TX19，默认 500 kHz。
  - `SYSTem:REFMEM:SYNC:SPI:*` 增加 ARM/DISarm/STATus、frame TX/RX、raw byte TX/RX 和 line release/drive/status 维护入口。
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 固化三层验证：先扫线，再 raw byte，再 RefMem frame；PC 只触发 TX/RX，不搬运 frame hex。
  - build `20260815022459` 已通过 OTA 写入并在 COM5/COM6 上 commit，双板 `SYSTem:FW:BUILD?` 均返回该 build。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，package CRC `0xAE0FFEB2`。
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - OTA COM5/COM6 正常进入 `READY_TO_REBOOT` 并完成 `SYSTem:OTA:COMMit`。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815022459_linefull` 失败在 line preflight。
  - 六条线级检查均读 0：A_CS_B、A_SCK_B、A_TX_B_RX、B_CS_A、B_SCK_A、B_TX_A_RX。因此当前不是 RefMem frame/CRC/target 语义问题，也不是 raw SPI 负载问题，而是 GPIO16-19 互联未成立或 COM5/COM6 对应板未按该线序连接。
- 下一步：
  - 按 playbook 接线后重跑同一 HIL：`SCK18-SCK18`、`CS17-CS17`、`A_TX19 -> B_RX16`、`B_TX19 -> A_RX16`、`GND-GND`。
  - 线级 preflight 通过后，再进入 raw byte 和 `HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` frame 层闭环。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_spi_physical_adapter.h`
  - `components/distributed_refmem/src/refmem_spi_physical_adapter.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`

### REFMEM-TASK-20260815-008 - Host GCC 单元测试断言门禁

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 使用 `D:\Embedded\GCC\mingw64\bin\gcc.exe` 让现有 C 单元测试在 host 上真实执行断言。
  - 纠正此前多处测试脚本在无 host C 编译器时只做 ARM GCC compile-only 的验证口径。
  - 将该门禁作为继续推进 RefMem 表模型、S0 flash/core1、真实 transport 和 PIO 预约输出前的基础验证。
- 完成内容：
  - 新增 `tools/tests/run_host_unit_tests.ps1`，默认将 `D:\Embedded\GCC\mingw64\bin` 加入 `PATH`，顺序运行 BiSS、portable LOG/OTA 和 RefMem 相关测试脚本。
  - 修正 `run_refmem_application_contract_tests.ps1` 的链接源，补入 `refmem_slot_claim.c`。
  - 修复 `refmem_claim_propose_frame_init()` 在设置 `payload_count` 前计算 payload CRC 的协议 bug。
  - 扩大 `test_refmem_table_registry.c` 的测试构包容量，避免当前 `.rmtp` P0-P3 表 payload 超过旧 `1536` 字节缓冲导致 host 栈破坏。
  - 修正 `test_portable_ota_core.c` 的 package-mode 测试夹具，使 header package CRC 字段保持与当前 packager 一致的 `0` 语义。
  - 修正 `test_biss_protocol.c` 中 CRC6 golden vector 的参与位拼接，改为明确的 18-bit payload/status 和 24-bit payload 向量。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1` 通过。
  - S0 补入 Flash/Core1 lockout 断言后，覆盖 13 个测试脚本、18 个 C 单元测试文件，期望输出 `host unit test scripts passed: 13/13`。
  - Host GCC：`D:\Embedded\GCC\mingw64\bin\gcc.exe`。
- 还需完成：
  - 下一轮优先进入 S0：Flash erase/program 前 core1 park/lockout/RAM-resident 入口完整握手和故障注入。
  - 然后接入真实最小 physical transport，让 `DELTA/ACK/FENCE` 在两板真实链路上跑通。
  - `ModelTurntableAO` 需要补真实 PIO scheduled fire 输出路径，验证“到点出边沿”。
- 关联文件：
  - `tools/tests/run_host_unit_tests.ps1`
  - `tools/tests/run_refmem_application_contract_tests.ps1`
  - `components/distributed_refmem/src/refmem_claim_protocol.c`
  - `tests/unit/test_refmem_table_registry.c`
  - `tests/unit/test_portable_ota_core.c`
  - `tests/unit/test_biss_protocol.c`
  - `docs/validation/README.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 不继续扩大 RefMem 静态表模型，先按 `REFMEM_DOMAIN_TODO.md` 的近期主线完成 S0、真实 transport 和 PIO 预约输出闭环。

### REFMEM-TASK-20260815-007 - RMTP NodeLoadTable 真实表镜像

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 继续 P0 逐表真实化，将 `.rmtp` table 3 `NodeLoadTable` 从占位 payload 升级为固定 u32 表镜像。
  - 让 NodeLoad 的基础字段合法性由公共 application contract 校验，为后续 `LOAD:NODE` staging image 和动态实例装载做准备。
- 完成内容：
  - `tools/refmem/refmem_table_image/refmem_table_image.py` 新增 `build_node_load_payload()`，按当前 11 条默认 NodeLoad 模板输出固定 u32 payload。
  - `refmem_application_contract.h/.c` 新增 `refmem_application_contract_validate_node_load_table()`，校验 version、load count、load id、application/profile、node 范围、instance 范围、fail policy、enabled/required 和 target node mask。
  - `refmem_application_model.c` 的静态 NodeLoad linter 复用公共 contract，并保留对当前内置 FB instance 表的实例存在性、重复 enabled instance 和 required enabled instance 覆盖检查。
  - `refmem_table_registry.c` 新增 NodeLoad parser，并在 `.rmtp` package owner validation 中把 table 3 纳入 owner-validated mask。
  - `tests/python/test_refmem_pack_build.py` 增加 table 3 size/count 断言；`tests/unit/test_refmem_table_registry.c` 增加 NodeLoad package 构造和 owner mask/staging 状态断言。
- 验证结果：
  - `python -m pytest tests\python\test_refmem_pack_build.py` 通过，2 passed。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\refmem_pack_build\refmem_pack_build.py --output-dir build-rtos-multicore-smoke\refmem_pack_nodeload` 通过，生成 `.rmtp` size `1924`、CRC `3A20B868`，table 3 CRC `7D7762D1`。
  - `python tools\sd_fs_build\sd_fs_build.py --build-dir build-rtos-multicore-smoke --output-dir build-rtos-multicore-smoke\sdcard_nodeload --clean` 通过。
  - 独立打包输出与 SD staging 输出的 `/refmem/app_model.rmtp` SHA256 均为 `C2EC768B52E284403D655BF60309E5AE102AE974909763A3B078B5FA79CE75CD`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814172116`，package CRC `0x5B9C1C34`。
- 还需完成：
  - 继续将 FbInstance、EventLink、DataLink、DeploymentGate 和 ConnectionQuality 五张表升级为真实表镜像。
  - `SYSTem:REFMEM:LOAD:NODE` 仍需从单条候选 snapshot 升级为真正的 NodeLoad staging image；本轮只是让 RMTP package 中的完整 NodeLoad 表具备 canonical payload 和 contract。
- 关联文件：
  - `tools/refmem/refmem_table_image/refmem_table_image.py`
  - `tools/refmem/refmem_pack_build/refmem_pack_build.py`
  - `components/distributed_refmem/inc/refmem_application_contract.h`
  - `components/distributed_refmem/src/refmem_application_contract.c`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tests/python/test_refmem_pack_build.py`
  - `tests/unit/test_refmem_table_registry.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`

