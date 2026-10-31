# OTA HAOFV 任务进度

Status: Active
Domain: OTA
Canonical: `docs/ota/OTA_TASK_PROGRESS.md`
Related: `docs/ota/OTA_HAOFV_ARCHITECTURE.md`, `docs/ota/OTA_TODO.md`, `docs/arch/HAOFV_FLASH_TASK_PROGRESS.md`
Last updated: 2026-10-31

本文只记录 OTA HAOFV 主线的代码、测试、构建、HIL、失败、回退和下一 gate。稳定语义见 Architecture，任务状态见 TODO。

## 文档接口

| 事实 | 位置 |
|---|---|
| 稳定 owner/ECC/Vector 契约 | `OTA_HAOFV_ARCHITECTURE.md` |
| 当前任务状态与退出门禁 | `OTA_TODO.md` |
| 原始运行证据 | `out/ota/`、P3 acceptance report |

## 当前 checkpoint

- 当前主线：`OTA-HAOFV-010` 收口（P3 receipt / power-cut / C11）。
- 进行中：`OTA-HAOFV-011` 栈帧已收敛并按实测水位定在 1792 words（曾压到 1152 触发四板 OTA 全崩，已修复）；`OTA-HAOFV-014` 失败分类/CAP fail-closed/失败即 ABORT 已落，待四板窗口实测；`OTA-HAOFV-012` 无进展超时已实现并过 host 单测，待 HIL 证明 FAILED 后可再 `BEGIN`；`OTA-HAOFV-013` 顺序修复已落（行为级证明改为台架负向，host 不可复现）。
- 已确认：BCB CRC 深栈副本导致的 HardFault 已修复，COM7 Direct A/B 双向闭环通过。
- 下一 gate：完成当前源码指纹的 P3 receipt、真实 power-cut 边界和 C11 复核；不得复用旧 receipt。

### OTA-TDMA-PROGRESS-20261031-080 - N2 开流阻塞根因：relay 的 OPEN 描述符指针悬垂（SCPI 栈帧 → 会话长期持有）

- 结论：目标侧 decode 出全零/垃圾 OPEN（历史 DIAG[6..9]=0、buffer[12..15]=0、trace 301/403 抖动）不是
  重组或 wire 格式问题。`ota_tdma_binding_relay_start()` 在 SCPI 任务栈构造 `uint8_t wire[88]`，经
  `ota_tdma_relay_open()` → `ota_tdma_session_init_source()` 只保存指针（`session->open_wire = open_wire`）；
  OPEN 真正编码发生在 `ota_tdma_relay_tick()`（AO service 栈），此时该栈帧已被后续 SCPI/USB 处理覆盖 →
  目标收到覆盖后的描述符（全零或垃圾）。
- 既有证据为何误判：`s_relay_wire_code` / phase `700000+...` 在构造时立即取值，证明的是“构造正确”，
  不是“发送时读取正确”；host roundtrip 用长寿 buffer，覆盖不到调用方栈帧回收。
- 复现（host）：`tests/unit/test_ota_tdma_relay.c` 新增生命周期回归——`relay_open()` 后覆写调用方
  buffer，再由 `relay_tick()` 发送；旧代码 `OPEN.payload[0]==0x5A` 断言失败（实测为覆写值），修复后通过。
- 修复：`ota_tdma_relay_t` 增加 `open_wire[88]` 副本，`ota_tdma_relay_open()` memcpy 后以
  `relay->open_wire` 初始化会话（+88 B .bss，无堆、无 API 变化、单写者/owner 边界不变）。
- 顺带收敛（relay 改动此前只上板验证，host 全红）：
  - OPEN 重组改为逐片期望长度校验（乱序到达不再被误判为短覆盖）；
  - pump 发送失败=瞬态（测试对齐 067 语义）；DATA 先于 OPEN=忽略并计 `ignored_opens`（对齐 067）；
  - session/carrier 用例补 `resume()` 起步（`start()` 只发 OPEN）；binding 用例补 watchdog 桩与 include；
  - 5 套用例补 `stream_abort/force_reset/last_session_result` 桩；refmem/tdma 运行器补 `sync_trigger`
    include 与 `tdma_service_core1.c`/`tdma_pio_spi_ring_origin.c`/`tdma_pio_spi_ring_rx_prepare.c` 源；
  - 环适配器用例按 `TDMA-SEAT-SHARE-01` 在线轮换语义更新“started 时拒绝”旧断言（STOP-only 在 SCPI wrapper）。
- 证据：`tools/tests/run_host_unit_tests.ps1` 全绿（`out/host_units_20261031d.log`）；8 套 TDMA host 用例。
- 未闭合：N2 判据仍需上板复测（目标 `READY_TO_REBOOT`、环 `tx≈rx` 不损坏、durable 递增）；本条目只关闭
  “根因 + host 回归”，不宣布 G1 通过。
- **P3（2026-10-31，`out/HardwareAcceptance/20261031/hawf-085223`，full+tdma-only）**：步骤 0–23 全 PASS
  （构建、四板 OTA 4/4、P0T、粗/编码标定、TRN-00..03）；**step 24（四板 TDMA 过程镜像）失败**：显式启动
  barrier 超时（up/down 未 running），step 26/34 连带 ERROR，凭证不可写。
- **台架现状（2026-10-31 13:20，快照）**：COM3/COM5 已在 `20261031043605`（P3 构建）；COM4/COM6 仍为
  `20261030000851`，其 stream 会话被一次变体固件试验留下旧 journal，open 恒返回 `STREAM_OPEN_5`，
  stream/legacy 两条 OTA 都被拒 → 远程无法再把新固件刷入；恢复需现场 BOOTSEL/UF2（或经批准对四板刷
  factory UF2，会清标定/BCB，P3 会重做标定）。**在此状态下 P3 无法出凭证，代码提交被门禁阻断（未提交）。**
- **P3 通过（2026-10-31 14:06，`out/HardwareAcceptance/20261031/hawf-134835`，build `20261031054904`）**：
  `PASS_WITH_WARNINGS`、ERROR 0、凭证已写 `config/hardware_acceptance/p3_acceptance_receipt.json`
  （`FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`、`passed=true`、source 1642 files）。step 24/26 通过。
- **step 24 根因（recorder OVERFLOW）**：板侧录制器终止原因 2 = `TDMA_RECORD_OVERFLOW`；本批把
  `STORAGE_MANAGER_FILE_WRITE_MAX_BYTES` 从 `3*4096` 压到 `2*4096`（relay OOM 期间的 RAM 压缩），
  15 样本 × ~0.5 s 的记录需 ~12.2 KiB（历史通过记录 11,048–12,164 B），8 KiB 在 ~10 样本处溢出 →
  `collection_errors` 非空 → startup barrier 被拒（环本身已在运行，样本可见 up/down running 与序号推进）。
  修复：恢复 12 KiB 窗口，并从 V2 FreeRTOS 堆让出 4 KiB（`101→97 KiB`，`configTOTAL_HEAP_SIZE`；
  实测空闲堆 1,344 B、无运行期 malloc 调用点）；`storage_manager.h` 仍为三个 4 KB 组。
- **recorder↔ring 隔离（同日，host 与固件双向排除）**：同一构建 `--no-record` 可稳定起环（多次），
  开启板载 recorder（P3 固定路径）失败；失败运行的板侧 `runtime_after.ring_last_error = 2`
  （`TDMA_RING_RUNTIME_REASON_EVIDENCE_MISSING` = adapter service 返回失败），barrier 期
  `up/down_not_running`。已排除：动作超时 0.05→0.5、barrier 6→20 s、`--short-open`、
  以及 OTA 侧 delta（禁用 `app_ota_service()` 内 TDMA binding/registration 的变体固件仍失败，2/2，
  证据 `out/trn03_variantA*.log`）。旧构建同样失败 → 不归因于本批 OTA 改动；下一步属台架级
  （断电冷启动/换 USB 口）或独立 recorder↔adapter 时序任务，不在本条目内闭环。

### OTA-TDMA-PROGRESS-20261030-068 - relay：OPEN 重组完成但 decode 返回 false；并发现刷写可能写到非 active 槽

- 复合 phase 诊断（100000 + trace*1000 + received*100 + total*10 + failure）：
  - 目标 phase=401323 → trace=301（`pota_stream_open_decode_le` 返回 false），received=3（两片齐）、total=2；
  - 在 session 内复制 decode 的 4 条校验（36/38/39/relay_cap/caps 白名单）打 411–414，**均未触发**，
    说明按这些规则 wire 合法，decode 的内部失败点仍未定位（third_party 弱符号 `pota_wire_trace` 未被覆盖，
    无法从 decode 内部上报）。
- 现场问题：NO.2 多次刷写后行为仍像旧固件（phase 不变），怀疑 **UF2 写入 slot A 但 active 仍是 slot B**，
  启动的是旧镜像；需要“刷写后强制从新槽启动/或切槽”才能验证新固件。
- 下一步：①先解决刷写落槽与 active 槽不一致（读 `SYST:OTA:SLOT?`，必要时用 OTA commit 切槽）；
  ②在 decode 前置校验通过的前提下，用强符号钩子（非 weak）或直接改 decode 返回码定位内部失败；
  ③修好后重跑 N2 判据。

### OTA-TDMA-PROGRESS-20261030-067 - relay 失败根因收窄：OPEN 第 3 片未达（received=0b011）

- 已修：
  - 发送失败（维护窗口关闭）视为**瞬态**，不再把 pump 置 FAILED/PROTOCOL；会话保持 RUNNING，由 resume 重试；
  - 目标容忍 DATA 先于 OPEN（忽略而非杀会话）；
  - **重复 OPEN 分片不再 reset 重组**（原实现每次重传都清零进度）；
  - 目标侧持久 phase：200+失败码、240+received 位图。
- 实测：目标 phase=**243**（240+0b011）→ 收到 seq0/seq1，**seq2 始终未达**；目标 `STREAM:STAT=6` 未开流；源 phase=73。
- 结论：协议/重组逻辑已就绪，剩余是**最后一分片的投递率**（维护窗口对 24–32 B 帧的丢弃）。
- 下一步：目标记录 `total`（phase 250+total）与 carrier 拒绝计数；源改为“只重发缺失分片/每片连发 3 次+退避”，并用 24 B 分片上限验证。

### OTA-TDMA-PROGRESS-20261030-066 - relay 可受理；目标仍未开流（OPEN 分片重组失败/维护窗口拒绝率高）

- 新增修复：
  - `ota_tdma_session_resend_open()`：源在首个 ACK 前每 200 ms 重发 OPEN 分片；
  - 目标容忍“DATA 先于 OPEN”：忽略未开流前的 DATA，不再 FAIL_PROTOCOL 杀会话；
  - 目标侧 added phase 80/81/82/83/84 打点（入口/绑定/帧结果/开流）。
- 实测（环 running、四板 SCPI ~0 ms）：
  - relay OPEN 受理（源 phase=73），但 durable 512 仍不达；
  - 目标 phase=**83**（`ota_tdma_session_on_frame` 返回 false），`STREAM:STAT=6` 未开流；
  - LONGframe 显示目标侧 rejects 数万、accepted 仅几十——维护窗口对 2 帧/tick 的突发拒绝率极高，OPEN 分片很难完整到达。
- 下一步：按窗口可用性节流发送（carrier 返回 false 时退避，不丢 OPEN）、把“未开流先 DATA”改为静默丢弃并回 ACK，或在目标侧记录 reassembly 失败码（seq/total）定位分片问题。

### OTA-TDMA-PROGRESS-20261030-065 - relay OOM 修复；relay OPEN 可受理但目标未开流

- 根因：relay 路径 `pvPortMalloc(sizeof(s_ctx))` + `pvPortMalloc(sizeof(s_relay))`（relay 含 4 KiB window）在 V2 小堆上失败 → malloc hook（phase 42）自旋 → USB 停摆。与环无关（空闲板同样复现）。
- 修复：
  - `s_ctx`/`s_relay` 改**静态存储**（单写者，不再 malloc）；
  - `OTA_TDMA_RELAY_WINDOW` 4096 → 2048（省 2 KiB）；
  - SD 整文件写窗口 `STORAGE_MANAGER_FILE_WRITE_MAX_BYTES` 3*4096 → 2*4096（用户已批准压缩；**这是对外可见上限变化，需在收口评审中确认/回滚或改流式**）。
- 实测：
  - 空闲板 relay OPEN：phase=73（relay started），不再 OOM；因无环，等待 durable 512 超时（预期）。
  - 起环后（过程镜像、静态维护、无 DPLL）relay OPEN 受理，四板 SCPI 正常（~20 ms），`STREAM:STAT=5`（relay 源会话）但目标 NO.2 `STREAM:STAT=6`（未开目标会话），durable 512 始终未达。
  - LONGframe：源 `mailbox_calls≈32k`、`frames≈684`、`rejects≈31k`；目标 `frames≈89`——维护帧能注入但目标侧大量拒绝/未开流。
- 下一步：给 binding 加 SCPI 计数器查询（或 phase 80–83 打点）定位目标 `handle_maintenance_frame` 的 init/open 失败点，再修目标开流。

### OTA-TDMA-PROGRESS-20261030-064 - N2 过程镜像环跑起来；relay 导致 SCPI/USB 停摆

- 方法：给 `trn03_closed_loop.py` 的 `--no-record` 路径加 `--hold-s`（起环后保持进程，hold 前释放持久串口），在 hold 窗口内从外部做 relay。
- 成功：四板 **过程镜像环 running**：`enabled=1, nodes=4, adapter=1, up=1, down=1, tx≈rx`（如 1686/1685、1709/1709），维护窗口 `window_open≈70-79 / cycle≈4431-5024`。
- 失败：窗口内 `ota_stream_send --relay-node 1` 的 `STREAM:OPEN` 无响应，随后 `SYST:OTA:STAT?`/`SYST:ERR?` 出现 **Write timeout**；四板 USB CDC 停摆（写超时），只能 `picotool reboot -f --ser <uid>` 恢复（无需 CS/断电，实测四板均恢复、`IDLE`、build `20261030115957`）。
- 判断：relay OPEN 路径或全速环的维护注入把 core0/USB 拖死（更可能是 SCPI 任务被维护窗口提交/环负载饿死），不是 ring 本身；下一步需在“静态维护态”（不跑 DPLL、降低 ring 负载）下重试，并给 SCPI/USB 保留 CPU。
- 现场：四板已恢复 `IDLE`。

### OTA-TDMA-PROGRESS-20261030-063 - P3 V2 TDMA-only 通过；N2 环窗口仍不稳

- 关键修复（工具）：`ota_stream_send --boot-and-commit` 顺序错误（commit 前就 reboot/校验 build）→ 改为 commit 写 pending → 复位 → 重枚举 → 校验 slot/build；NO.2 实测 `pre_commit=1,2` → `applied=2,0`，exit 0。
- P3 V2：`p3_hardware_acceptance.py run --preset pico2-v2-factory-candidate --tdma-only` 返回 **PASS_WITH_WARNINGS**（exit 0）：四板 OTA（stream+commit）、P0T/CLK/MARK/P1/P2/P3、TRN-00/01/02/03、四节点 TDMA process-image/FIFO 全部执行；警告项含 TDMA 记录导出判据（`tdma.log` 内 trn03 `passed=false`，P3 以 diagnostic_continue 放行）。
- 四板已统一到 P3 构建 `20261030115957`（含修复版固件）。
- N2 尝试：
  - `trn03_closed_loop --stage process-image --leave-running --no-record` 可启动过程镜像环（recorder 判据被绕过，事件 `passed=true,left_running=true`），但工具退出后 `RING:STATus` 归零（环未持久）；
  - 在窗口内并发起环的等待脚本未捕捉到 `enabled=1`（trn03 提前退出/板卡发现失败：`boards not found ... 0010071E65B5CB38`），relay 未执行；
  - 现场四板 `IDLE`、build `20261030115957`。
- 下一步：把 relay 调用嵌入 trn03 的运行窗口（wrapper 内起环→等 `enabled=1`→relay→停环），或修 `--leave-running` 的持久语义。

### OTA-TDMA-PROGRESS-20261030-062 - V2 stream OTA workflow 全绿（run-32，commit/reboot/verify）

- workflow 语义修正：
  - 目标槽按 `SYST:OTA:TARG?` 动态选择（A/B 均可），包对象按目标槽切分；
  - `commit` 只负责写 pending（15 s 超时容忍），`reboot` 等待设备重枚举并校验 `active==target && pending==0`，`verify` 复核 `SLOT?`/`RESult`；
  - 端口在复位期间消失时按 90 s 重试，不再误判。
- 实测 `out/ota_workflow/run-32`（NO.1，A→B）：
  - identify/open/header/data/close/wait_ready 全 OK（`durable=1137880`，`READY_TO_REBOOT`）；
  - commit OK：`SLOT? = 1,2,1,0,0`（pending=2）；
  - 设备随 commit 自动重启；reboot/verify OK：`SLOT? = 2,0,1,1,0`、`RESult = "APPLIED"`；
  - 全程无 watchdog、无 recovery。
- 结论：目标①（V2 本机 stream OTA：OPEN→包头→4096 块→READY_TO_REBOOT→commit/boot/active 切换）**已打通并可复跑**。
- 下一步：N2 relay（四板环，NO.1 单口发起 → NO.2 达 READY 且环不损坏）；先把 NO.2/3/4 刷到同一修复版固件。

### OTA-TDMA-PROGRESS-20261030-061 - V2 stream OTA 本机闭环成功（含 commit/切槽）

- 根因链（本轮实证）：
  1. manifest finalize 在 `pota_slot_manifest_txn_begin/load` 上使用约 1.2–2 KB 栈局部，refmem_sync 任务仅 2 KiB 余量 → 栈溢出，表现为 `CORE0_SUPERVISOR_STALL`/assert hook（phase 40/55/607/614）；
  2. 直接加大任务栈（4096 words）会把 FreeRTOS 堆吃光 → malloc 失败（phase 42），已回退；
  3. 修复：把 `load`/`txn_begin` 的 manifest 记录改为**文件作用域静态缓冲**（OTA 单写者），V2 堆 101 KiB−64 B；同时保留页级 feed、progress/heartbeat 信号。
- 结果（NO.1，`out/ota_workflow/run-30`）：
  - `identify/open/header/data/close/wait_ready` 全部 OK，`READY_TO_REBOOT`（state=4）；
  - `commit` 写入 pending=2（`OTA:SLOT? = 1,2,1,0,0`，`RESult` 记录 size=1137368/crc）；
  - `SYST:BOOT:RESET` 后 bootloader 应用 pending：`SLOT? = 2,0,1,1,0`、`RESult = "APPLIED"`、`active slot = B`；
  - 复位历史为 `POWER_OR_EXTERNAL`（commit 触发的正常重启），无 watchdog。
- 传输性能：整块编程后 278 块约 182–207 s；擦除约 10–20 s。
- 仍需：workflow 的 `commit` 步应适配“commit 会触发重启”的语义（当前判为 timeout，但结果已提交成功）；随后进入 N2 relay。
- 现场：NO.1 运行于 slot B（active=2），NO.2/3/4 仍为旧 V2 镜像（20261030000851 时代）。

### OTA-TDMA-PROGRESS-20261030-060 - 面包屑确证：复位发生在 manifest（phase 6），非看门狗预算

- 在 SCPI DATA/CLOSE 加了持久面包屑（phase 20=写前、21=写成功、22=写失败、23=最后一块、24=CLOSE 前、25=CLOSE 返回），复位后读 `SYST:WATCHdog:OTA:PHase?`：
  - 结果仍为 **6**（`OTA_TRACE_PHASE_MANIFEST_BEGIN`），说明 20/21/23/24 都被后续 manifest 阶段覆盖 → **复位确实发生在 manifest 事务内**，且 CLOSE 已被处理（phase 24 曾置位）。
- 结论收敛：不是“DATA 写路径把设备打死”，而是 **CLOSE → END → manifest 事务**；该事务当前不在 SCPI 内完成（CLOSE 只排队 END），而在 AO service 推进。
- 已加但未阻止复位：`portable_ota_port_core_service` 入口/出口 `drv_watchdog_feed()`、manifest 每步 feed、OTA 窗口 120 s（硬件上限约 8.3 s，已钳制）。
- 下一步：确认 manifest 事务是否在 **task_refmem_sync 的 AO 循环**里连续执行超过 3 s（期间 diagnostics supervisor 因 REFMEM_SYNC 心跳缺失而停止喂狗）；若是，把 manifest 事务改为每 tick 只走一步并在 supervisor 心跳处显式 mark，而不是靠零散 feed。
- 现场：NO.1 `IDLE`，会话失效；其余三块空闲。

### OTA-TDMA-PROGRESS-20261030-059 - 4 KiB 整块编程加速；收尾仍停在 MANIFEST_BEGIN

- 加速：`pota_program_target` 新增整块快路径（页对齐时一次 `flash_program`，readback 仍按页），同一镜像传输 **1064 s → 193 s**（约 5.5×）。构建/刷写/回归正常。
- 收尾仍失败：`close` 步在 ~16.8 s 时端口消失；复位后 `SYST:WATCHdog:LOG?` 仍为 `WATCHDOG_TIMEOUT,CORE0_SUPERVISOR_STALL`，`OTA:PHase?=6`（MANIFEST_BEGIN）。
- 已尝试但未消除：页编程循环 feed+yield、CLOSE/DATA 期间 `drv_watchdog_enable(30000)`、manifest 每步后 `drv_watchdog_feed()`。说明复位发生在 **CLOSE→END→manifest 的调用链内部**，且该链在 SCPI 上下文同步执行（读/写/校验），预算内喂狗覆盖不到。
- 下一步（明确）：检查 `pota_session_end` 是否在 package 模式同步执行 manifest；若是，CLOSE 只应“排队 END”，由 AO service 分步推进（每步之间喂狗），把 finalize 从 SCPI 上下文移出。
- 现场：NO.1 `IDLE`、会话失效；其余三块空闲。

### OTA-TDMA-PROGRESS-20261030-058 - 收尾仍在 manifest 阶段触发看门狗（workflow 已能稳定复现）

- 多轮 workflow 实测（run-10/11/12，均 `--attempt-dir` 独立、步骤落盘）：
  - `identify/open/header` 稳定通过（header durable=512）；
  - `data` 稳定完成：`durable=1137688/1137704/1137712`、278 块、约 1064–1075 s（≈3.8 s/块）；
  - `close` 步在设备**已复位**后失败：`PermissionError(13)/OSError(22)`。
- 复位证据：`SYST:WATCHdog:LOG? = "WATCHDOG_TIMEOUT","CORE0_SUPERVISOR_STALL"`，`SYST:WATCHdog:OTA:PHase? = 6`（`OTA_TRACE_PHASE_MANIFEST_BEGIN`）→ 收尾发生在 manifest/哈希校验阶段。
- 已尝试：①页编程循环内 feed+yield；②CLOSE/DATA 期间把看门狗预算放宽到 30 s；仍复位（snapshot 记录 timeout=3000），说明复位点不在这些预算覆盖的调用内（疑似 AO/checkpoint 路径的 manifest 步）。
- 下一步：把 finalize（manifest 哈希/签名/BCB）显式拆成 AO 可服务的步骤并在每步喂狗；或在 manifest 读取循环内注册 feed hook（需给 crypto 端口注入 watchdog 回调，避免直接依赖 driver 头）。
- 现场：NO.1 `IDLE`、会话已失效；其余三块空闲；目标仍 active。

### OTA-TDMA-PROGRESS-20261030-057 - 收尾 watchdog stall 的直接原因：页编程循环未喂狗

- `pota_core.c::pota_program_target` 对每个 256 B 页做 program + readback，4 KiB 块 = 16 次往返；循环内只有块级 `pota_feed_watchdog`/`pota_yield_or_delay`，单块耗时约 3.2–3.8 s，**刚好超过 3 s core0 supervisor 预算** → 收尾（最后一块）时 `WATCHDOG_TIMEOUT / CORE0_SUPERVISOR_STALL`，会话丢失。
- 修复：在页级 program/readback 循环内每页调用 `pota_feed_watchdog()` + `pota_yield_or_delay()`（`third_party/portable_ota/src/pota_core.c`），构建通过。
- 这也解释了此前多次“数据全量到达后设备复位”的现象；属于门 ②/④ 的预算问题，不是 manifest/签名逻辑。
- 现场验证：已刷入 NO.1 并重跑完整 workflow（`out/ota_workflow/run-10`），进行中。

### OTA-TDMA-PROGRESS-20261030-056 - workflow 收敛：OPEN 无换行回复 / 包对象切分 / 慢速传输

- `ota_workflow` 逐步暴露并修掉三个问题：
  1. **OPEN 回复没有 CRLF**（设备只回 `"OK"` 两个字节），行读取会一直等到超时 → Link 增加“成对引号即返回”；
  2. **包对象切分错误**：直接发整包会导致 core 判 `BAD_HEADER`（token `6004` = `POTA_ERR_BAD_HEADER`）；改为与 `ota_stream_send` 一致，发 `package_header + selected_object`；
  3. **未被清理的旧会话**让下一次 OPEN 在 ingress 层被拒且无回复 → 固件在 OPEN 前主动 `portable_ota_port_stream_abort()`。
- 诊断链（本版固件）：`STREAM:STAT?` 第 4 字段依次给出 `1000+session`、`2000+flash_stage`、`3000+core_error`、`5000+write_result`、`6000+write_core_error`，本次据此定位到 `6004=BAD_HEADER`。
- 实测（NO.1）：`identify` 0.11 s、`open` 0.09 s、`header` 1.97 s（durable=512）全部通过；`data` 步在 900 s 截止时已推进 **971264 / ~1137k 字节（237 块）**，按约定写 `step_timeout` 并停止（无挂死）；把 data 截止放宽到 3600 s 后 `--resume` 继续。
- 观察：当前 real throughput ≈ 1 KB/s（每 4 KiB 块约 3.8 s），2 MB 擦除 + 1.1 MB 编程预计 ~20 分钟；这解释了此前 host 端 400 s 超时失败，也是后续要优化的点（批擦/写合并）。
- 现场：NO.1 已接收 ~971 KB（会话进行中）；其余三块空闲。

### OTA-TDMA-PROGRESS-20261030-055 - TDMA 流式 OTA 改为 workflow step 步进式

- 新增 `tools/ota/ota_stream_send/ota_workflow.py`：固定步骤 `identify → open → header → data → close → wait_ready → commit → reboot → verify`，每步：
  - 独立打开串口、独立超时（默认 300 s，OPEN/DATA 期间擦除可阻塞）；
  - 结果写 `steps/NN_<name>.json` 与 `state.json`（durable、completed、open 参数）；
  - 任一步失败/超时立即停止并打印 `stopped at step <name>`，**不存在无界循环**；
  - `--resume` 从上次完成的步骤继续；`--steps` 可只跑指定步骤。
- 新增 `tests/python/test_ota_workflow.py`（4 项）：步骤顺序、state 往返、record 语义、CLI 默认值。
- 首轮实测（NO.1，`out/ota_workflow/run-01`）：`identify` 0.12 s 通过；`open` 在 3 s 超时被判失败，但 `STREAM:STAT? = 0,2,0,<token>,0` 说明设备侧 OPEN **已成功**（状态 RECEIVING）→ 默认超时提高到 300 s 并复跑。
- 设计意图：把“脚本卡住”变成“某一步 timeout + 证据落盘”，后续 `tdma_night`/`tdma_matrix` 都改为调用该 workflow。

### OTA-TDMA-PROGRESS-20261030-054 - V2 下 picotool 进 BOOTSEL 已修复（四板可远程刷写）

- 根因：`reset_usb_boot()` 把 BOOTSEL magic 写入 watchdog scratch 后立即复位，但 core1 的 TDMA 优先级面包屑（`watchdog_hw->scratch[6]`）和看门狗进度（`scratch[5]`）仍在写同一组寄存器，ROM 采样前 magic 被覆盖 → 设备回到 app 而不是 BOOTSEL。V1 固件下这条路径未常驻，所以之前能进。
- 修复：`SYST:BOOT:BOOTSel` 先 `osal_critical_enter()` 关本地中断、`multicore_reset_core1()` 停 core1，再调用 `reset_usb_boot(0,0)`（`middleware/scpi_port/src/scpi_port.c`）。
- 工具接线：`tools/picotool_flash/picotool_flash.py` 在 `picotool reboot -f -u` 未得到 BOOTSEL 时，自动通过串口向应用发 `SYST:BOOT:BOOTSel`（带 12 次重试等待 USB 重新枚举），再执行 load。
- 证据（2026-10-30）：
  - 手工：发 `SYST:BOOT:BOOTSel` 后 CDC 断开，`picotool info` 显示 `Program Information`（真 BOOTSEL）；
  - 工具：NO.1 刷写日志出现 `application asked to enter BOOTSEL via SCPI` → `Loading into Flash 100%` → `The device was rebooted to start the application`；NO.4 在**不短接 CS** 的情况下同样刷写成功；
  - 四板现均为 build `20261030000851`（含上述修复、stream bootstrap、erase 等待与核心态门控）。
- 边界：picotool 自身的 `reboot -f -u` 仍会失败（它的 USB 复位接口不做 core1 quiesce），因此统一走 `tools/picotool_flash` 的 SCPI 入口；`SYST:BOOT:BOOTSel` 只存在于本版及更新固件。

### OTA-TDMA-PROGRESS-20261030-053 - V2 legacy OTA 在擦除阶段 wedge（278 扇区后冻结）

- 目的：验证“停 DPLL/静态态 + legacy OTA”能否作为无需 BOOTSEL 的恢复通道。结论：**不能**（当前固件）。
- 实测（NO.3，V2 build `20261029233336`）：
  - `ota_send` legacy：`PBEGIN` 被接受 → `RECEIVING` → `ERASE_SLOT`，`PROGress=512/2273528`；
  - 轮询 `SYST:DIAG:FLASH:TRANsaction?`：事务计数在 `278` 个扇区后**完全冻结**（10 s 内零变化），`SYST:OTA:STAT?` 一直 `ERASE_SLOT`，`SYST:ERR?` 为 `0,"No error"`；
  - host 超时后 `ABOR` 把状态置 `ABORTED`，但事务仍卡在 278，新的 `PBEGIN` 也只到 `RECEIVING`，擦除不再推进；
  - `SYST:BOOT:RESET` 后事务清零，`PBEGIN` 单独不再启动擦除（要等 DATA）。
- 解释：擦除按扇区推进，需要每步 park/unpark core1（`drv_flash_write_session_begin()`）；连续多次 park 后在 278 扇区处 wedge，且没有超时/失败上抛（fail-closed 缺口）。这解释了为什么 legacy 在 V1 能过、V2 不能。
- 结论：V2 的 OTA **能力本身存在**（CAP=7、map=2、DIRECT_AB、签名包、host stream 用例全绿），当前被两处固件问题挡住：① 工厂空白 BCB 导致流面未初始化（已修）；② legacy 擦除 park wedge（待修，需可刷入新固件后才能验证）。物理 BOOTSEL 仍是当前唯一入口。

### OTA-TDMA-PROGRESS-20261030-052 - OTA-TDMA-015 host 矩阵（重复性 + 注入）落地

- 新增 `tools/ota/ota_stream_send/tdma_matrix.py`：
  - 计划模型 `MatrixPlan`（`OTA_TDMA_MATRIX_PLAN_V1`），默认用例 `clean`×3、`drop-1`、`drop-3`、`drop-8`；
  - `classify()` 把单次结果映射成 PASS / recovered / drop-exhausted / transfer-failed；
  - `run_matrix()` 支持每用例的 drop 注入回调（对应 `SYST:REFMEM:CMD:DROP`）。
- 新增 `tests/python/test_ota_tdma_matrix.py`（4 项）：计划 JSON 往返、分类、注入顺序。
- 已知边界：duplicate/timeout/park 三类注入还没有设备侧命令接线；HIL 实跑依赖 N2。
- 门禁：pytest 4 passed；docs 检查待跑。

### OTA-TDMA-PROGRESS-20261030-051 - 找到 V2 stream OPEN 被拒的根因：空白 BCB → 流面从未初始化

- host 仿真（`out/sim_open.c`，V2 平台：map=2、APP_A_ID=2、APP_B_ID=3、page=256/sector=4096）复现：不带 checkpoint 回调时 `pota_stream_session_open` 返回 **CHECKPOINT(9)**；而设备端所有 OPEN 都退化成 `POTA_STREAM_INGRESS_SESSION(5)`。
- 代码核对：`ota_ao_init()` 在 V2 上只有 `ota_metadata_load(&metadata)` **成功**时才调用 `portable_ota_port_stream_init(&metadata)`。工厂全擦后的 V2 板没有 BCB → `ota_metadata_load` 返回 false → 流面永远未初始化 → 任何 `STREAM:OPEN` 都在 `portable_ota_port_stream_open` 第一行返回 SESSION。
- 修复（SCPI 侧惰性 bootstrap）：`STREAM:OPEN` 遇到 SESSION 且未 bootstrap 时，用工厂默认元数据（active=A、pending=NONE、slot 尺寸取自 `ota_partition_slot_size`）初始化一次流面并重试；成功置位，后续不再触发。V2/v1 均构建通过；host OTA 相关 30 项通过。
- 意义：这正是“工厂迁移后首次 OTA 无法打开会话”的修复；等新固件刷入即可验证。
- 现场：四板仍跑旧 V2 固件（`20261029233336`）、`IDLE`，等待物理 BOOTSEL。

### OTA-TDMA-PROGRESS-20261030-050 - 恢复通道准备：BOOTSEL SCPI 命令改为常开

- 根因之一确认：`SYST:BOOT:BOOTSel`（内部调用 `reset_usb_boot(0,0)`）原本被 `PROJECT_ENABLE_OTA_FAULT_INJECTION` 宏包住，V2 候选（fault injection=OFF）下命令完全不注册 → 无法从 SCPI 进 BOOTSEL。
- 修改：把 `scpi_cmd_boot_bootsel` 与其注册从 fault-injection 宏中移出、`pico/bootrom.h` 改为无条件包含；v1 与 V2 两个映射均构建通过。
- 意义：刷入该固件后，未来可用 `SYST:BOOT:BOOTSel` 直接进 BOOTSEL，不再依赖 picotool 的 USB 复位接口（V2 上被证实不可用）。
- 当前限制：四板仍跑旧 V2 固件（无该命令），且两条 OTA 通道都失败 → **仍需一次物理 BOOTSEL** 才能把新固件刷进去；此后即可闭环。
- 现场：四板 V2 `20261029233336`、`IDLE`、环停。

### OTA-TDMA-PROGRESS-20261030-049 - V2 板当前无法从 host 重新刷新（需物理 BOOTSEL）

- 事实链（NO.3，V2 build `20261029233336`）：
  1. `picotool reboot -f -u` 返回 0，但设备始终停在 app 模式（picotool info 只见 USB serial），无法进入 BOOTSEL；
  2. `SYST:USB:BOOT` 未实现（`-113 Undefined header`）；`SYST:BOOT:RESET` 只做应用复位；
  3. 1200 bps touch 打开被拒（驱动层拒绝 1200 波特）；
  4. 强制 legacy OTA（`ota_multi_update --transport legacy`）：`PBEGIN` 被接受、进入 `ERASE_SLOT` 后 10 分钟不结束，最终 `unexpected_final_state=IDLE`（擦除阶段卡住）；
  5. stream OTA：会话 `OPEN` 被拒（`last_result=5`，原因待诊断）。
- 结论：V2 候选固件**没有可用的 USB 复位/BOOTSEL 入口**，且两条 OTA 通道都不可用 → 四板目前只能靠**物理 BOOTSEL（按键/上电）**重新刷新。
- 影响：后续所有 V2 固件迭代都需要先解决“V2 下可进入 BOOTSEL”或修好 stream OTA；在解决前不应继续在板上盲改。
- 现场：四板 V2 `20261029233336`、`IDLE`、环停；未改源码。

### OTA-TDMA-PROGRESS-20261030-048 - V2 stream OPEN 在会话层被拒（需原因码）

- 直接探针（COM3，NO.3）：用 P3 包构造 88 B 描述符（caps=3、map=2、identity=sha256(IDN)[:16]、hash=sha256(image)、package_mode=true），分别对 slot A（partition=2）与 slot B（partition=3）发 `STREAM:OPEN`：
  - 两者都返回空响应；`STREAM:STAT?` 显示 `last_result=5`（`POTA_STREAM_INGRESS_SESSION`）→ 会话打开失败；
  - slot B 那次 `SYST:ERR?` 为 0，slot A 为 -200（SCPI 侧未带原因）。
- 已知 V2 映射：`APP_A_ID=2`、`APP_B_ID=3`（host 的 `app_partition_id(2, slot)` 与之一致），`destination_slot` 用 1/2 枚举；`pota_stream_session_open` 的 `destination_valid`/`partition_matches_destination` 在纸面上应通过——因此需要固件侧把**会话拒绝的具体 result**暴露出来（当前被映射成笼统的 SESSION）。
- 下一步：给 `STREAM:OPEN` 失败路径加明确 error 文本（含 ingress result 与会话 result），重建 V2、刷一板、复测定位；成功后再回到 P3 V2 全流程。
- 现场：四板 V2 `20261029233336`、`IDLE`、环停。

### OTA-TDMA-PROGRESS-20261030-047 - P3 增加 `--preset`；V2 上 P3 的本地 stream OTA 需调参

- `p3_hardware_acceptance.py`：新增 `--preset`（传给 `cmake_build_auto`），并在构建后把 `DHRT100_V2_CANDIDATE_UPDATE.pkg` 别名到标准 `DHRT100_UPDATE.pkg`，使 P3 证据路径与 v1 一致。
- 首次 V2 运行（`run --preset pico2-v2-factory-candidate --tdma-only`，run 目录 `p3-081643`）：构建成功（V2 产物齐全），OTA 步骤自动走 **stream**（V2 能力），但四板失败：
  - 三块 `stream state 2 timeout`，最终 `last='0,6,0,<token>,5'` → state 6（FAILED）；
  - COM6 到达 `durable_offset=512` 后 `DATA rejected at 512: '<timeout>'`（3 s 写超时）。
- 判断：V2 本地 stream OTA 的**写入/校验耗时**超过 host 默认 3 s；且失败态需要区分“签名/内容校验失败”与“超时”。下一步：提高 `--timeout`（如 30 s）、必要时减小 block 并复跑 P3；成功后再谈 TDMA relay。
- 现场：四板 V2 build `20261029233336`（P3 新包未刷入，OTA 未完成），`IDLE`。

### OTA-TDMA-PROGRESS-20261030-046 - relay 根因链：service 门 / flight process image

- 修复 1：`app_ota_service()` 的 relay 服务门——relay 由 SCPI 初始化 binding 时，app 的 `s_ota_tdma_binding_ready` 仍为 false，导致 `binding_service` 被跳过、`relay_tick` 从不运行。改为 relay_active 时直接 service。已重建并刷 NO.1/NO.2。
- 修复 2：新 OPEN 会先 abort 陈旧 relay（之前残留 relay 让后续 OPEN 直接被拒）。
- 观察：即使两处修好，`SYST:TDMA:LONGframe:STATus?` 的 `mailbox_calls=0`、`frames=0` → 维护帧根本没进 mailbox；根因是**维护流依赖 flight process image**：adapter 只在 `returned_view.payload_size >= (local_slot+1)*SHORT_SLOT` 时注入 maintenance mailbox。
- 试验：`SYST:TDMA:FLIGHT:MODE 1` 后，slot0 出现 `window_open≈399 / cycle≈25525`，但 slot1–3 `tx=0`、`rejects` 达 7.9 万——没有配套的 process image/调度（T2/T3 训练产物）时，非参考节点不参与。
- 结论：N2 的维护流必须先有 **P3/T2/T3 生成的 flight process image**，不能靠裸环；而 P3 构建步骤硬编码默认 v1 preset，需要给 P3 增加 `--preset`（或等价）才能用 V2 跑。现场已 STOP 全部环，四板 V2、`IDLE`。

### OTA-TDMA-PROGRESS-20261030-045 - V2 工厂迁移完成（四板）；四板环 running；relay 目标侧待打通

- **迁移构建**：`cmake --preset pico2-v2-factory-candidate` + 生成 `out/keys/dhrt100_v2_debug.pem`（key_id 3506438145，覆盖 `config/ota_public_keys.json` 的 v2_debug 条目）。构建期间修了两处真实问题：
  1. `ota_partition.h` 的静态断言：V2 把 seat-mount 表放进 calibration NVS，断言应改为对 `FLASH_MAP_SEAT_MOUNT_OFFSET`（分支断言，v1 保持原判据）；
  2. `FreeRTOSConfig.h`：V2 候选的 `.bss` 溢出 2760 B，按部署分支把堆 106→102 KiB（v1 保持 106 KiB）。
  产物：`DHRT100_V2_CANDIDATE_FACTORY.uf2`、`DHRT100_V2_CANDIDATE_UPDATE.pkg`（build `20261029233336`）。
- **迁移**：四板 `picotool_flash --full-erase` 刷 FACTORY uf2，重设 NO.1–4。核验：四板 `STREAM:CAP? = 4096,7,2`（含 RELAY、map=2）、`MODE=DIRECT_AB`、`IDLE`；P0 四板邻接 `passed=true`，ring_order 正确。
- **起环**：按 P0 配方（`apply_profile` 含 `CAL:TOP:PROBe` → `_set_stopped_topology((4,slot,0))` → ARM → START，四板）成功：`enabled=1, nodes=4, adapter=1, down_running=1`，四板 `tx≈rx`（环真正闭环并有流量）。
- **relay 冒烟（未完成）**：coordinator 侧 OPEN 被接受、relay 进入 RECEIVING（`STREAM:STAT?=5,2,0,0,0`），但 durable 停在 0；target（NO.2）`STREAM:STAT?=6,0,0,0,0`（IDLE）→ **目标未打开会话**。已修一处“陈旧 relay 不被新 OPEN 覆盖”的拒绝（新 OPEN 先 abort 旧 relay），重建并刷四板后问题仍在，说明目标侧根本没收到/没分发 0x4F 帧。
- 现场：四板 V2、四板环 running（未停）。下一步：确认目标侧 REFMEM 的 `take()` 是否在维护态运行、以及 `0x4F` 是否到达 parser（需加只读计数或用 `SYST:TDMA:RING:DIAGnostic?` 定位）。

### OTA-TDMA-PROGRESS-20261030-044 - 目标进入阻塞态（部署前提待决策）

- 最终状态核验（2026-10-30）：8 个 TDMA host 套件全绿；`test_ota_*` 35 passed；`cmake_build_auto` 通过；`docs_check` 243/0 warn；`doc_regression` 通过；四板 `IDLE`、ring 已停。
- **阻塞条件（连续第 3 轮，Round 42–44）**：台架四板为 **v1_compat** 部署（`STREAM:CAP? = 4096,0,1`），固件结构上无法建立 stream 会话（直接 `STREAM:OPEN` 亦 `-200`）。TDMA 流式 OTA 的会话/durable ACK/relay 全部依赖 V2 stream。
- 需要用户二选一：
  - **A**：启动 V2 工厂迁移项目（`v2_candidate` + `PROJECT_FACTORY_MIGRATION_BUILD=ON` + `target_not_deployed` + `DIRECT_AB`，含迁移包与验收）；
  - **B**：为 v1_compat 重写 TDMA 会话语义（放弃/改写 durable checkpoint 判据，需重评审 G1）。
- 在决策前，N2（两板闭环）、N3/N4（resume/重复性/注入/回滚）、四板滚动与 `ARCH-OTASTREAM-01` 转 active 均无法取得经验证据；本轮按 fail-closed 将目标置为 blocked，而不是把 pending 写成完成。
- 恢复条件：用户给出 A/B 决策（或提供已迁移 V2 的台架），即可从 N2 继续。

### OTA-TDMA-PROGRESS-20261030-043 - v1 stream 结构性不可用（直接探针确认）

- 绕过 host 工具策略，直接向 NO.2 发 `SYST:OTA:STREAM:OPEN`（88 B 描述符：caps=3、map=1、partition=`app_partition_id(1,1)`、identity/hash 非零）：固件返回 `-200`，`STREAM:STAT?` 保持 `0,..,5`。
- 结论：**v1_compat 固件结构上不能建立 stream 会话**（与 `STREAM:CAP?` 报 0 一致），不是 host 工具限制 → 选项 B（在 v1 上跑 durable stream）不可行，除非重写会话层。
- V2 的门槛（`CMakeLists.txt`）：`v2_candidate` + `PROJECT_FACTORY_MIGRATION_BUILD=ON` + 源状态 `target_not_deployed` + `DIRECT_AB`；普通构建被强制 v1_compat → 选项 A 是一次**工厂迁移项目**（迁移构建/迁移包/验收），不是测试时开关。
- 本轮无代码变更。**需要你选择 A（迁移 V2）或 B（重写 v1 会话语义）**；在此之前 N2 无法继续。

### OTA-TDMA-PROGRESS-20261030-042 - 环 bring-up 成功；N2 被 v1 部署映射阻断

- **成功**：按 P0 配方（`apply_profile` → `configure_pair_topology` → `start_pair`，含 `CAL:TOP:PROBe` 与 receiver-first ARM）在两板（NO.1 slot0 / NO.2 slot1）起环：`enabled=1, node_count=2, adapter_started=1, down_running=1`，slot0 TX 计数 1250 且持续增长。配方已写入设计 §4.4（可复用，不再靠猜）。
- **阻断（新发现）**：台架 `SYST:OTA:STREAM:CAP?` = `4096,0,1` —— **stream 能力 0、部署 map = v1_compat**。TDMA 流式 OTA 的会话、durable ACK、relay 全建立在 V2 stream 之上；relay 冒烟直接被 host 工具拒绝（“stream transport is unavailable on v1_compat firmware”）。
- 这不是 OTA 代码缺陷，而是**部署前提缺失**。两个选项：
  - A：把台架迁移到 **V2 部署映射**（factory migration，属部署变更，需单独验收）；
  - B：为 v1_compat 设计无 durable checkpoint 的 TDMA 路径（与 G1 的 durable ACK 判据冲突，需重评审）。
- 现场：环已 STOP；四板 `IDLE`。
- 下一步需要用户决策 (A)/(B)；在此之前 N2 无法继续，host 侧工作（契约登记、N3/N4 脚本）可继续。

### OTA-TDMA-PROGRESS-20261030-041 - 找到 P0 的真实起环配方（CAL:TOP:PROBe）

- 台架先做了复位（`SYST:BOOT:RESET` 四板），P0 四板邻接重新 `passed=true`、ring_order 正确——说明前面失败是实验把 ring 配置留在 `enabled=1` 的脏状态。
- 读 P0 源码得到其**完整配方**（我们之前缺的前半段）：
  1. `apply_profile()`：`RING:STOP` → `wait_ring_stopped` → `OPMode:STAGe <level>`（校验 6 字段）→ `_apply_stopped_opmode` → **`CALibration:TOPology:PROBe 1,<cycles>`**；
  2. `configure_pair_topology()`：清空错误队列、要求 `VDC:FEEDback:SESSion? == 0`，再用 `stopped_profile._set_stopped_topology(board,(2,slot,0),…)`；
  3. `start_pair()`：**先 receiver(slot1) 后 driver(slot0)** 各自 `ARM`（重试 + `RING:ARM:STATus?==1`）→ `_wait_pair_state(started=True)` → 可选 `train()` → `_start_pair_board`。
- 结论：缺少的是 **`CAL:TOP:PROBe` 这一步**（它把校准 persona 拉起来），以及 receiver-first 的 ARM 顺序；这两点是我们裸 ARM/START 永远起不来 adapter 的原因。
- 下一步：把 P0 的 `apply_profile + configure_pair_topology + start_pair` 作为**可复用 bring-up**（import `calibration_ring_topology` 与其 `stopped_profile` 模块），在两板 pair 上起环后立刻 `tdma_night --relay-node 1`。
- 本轮无代码变更；未跑 relay，不记完成。

### OTA-TDMA-PROGRESS-20261030-040 - 起环依赖“校准/训练 persona”：N2 需复用 P3 路径

- 追到 core1 服务：`tdma_service_core1_service()` → `tdma_ring_runtime_service_with_stop_gate()` 才会启动 adapter；而该函数只在实时相位里被调用。停机态要跑它，必须进入**校准/训练的离线 persona**：
  - `app.c` 在 `calibration_manager_training_offline_active_core1()` 为真时改调 `calibration_manager_service_core1()` 并 **return**（不跑在线相位）；
  - 该 persona 的判据是 `s_training_activity_core1 && ring.enabled==0 && adapter_started==0`——这解释了为何“TOPology 后 enabled=1”的路径永远起不来 adapter，也解释了 P0（先进入训练 persona、ring 保持 disabled）为何能跑通。
- 结论：**N2 的起环不能靠裸 `RING:ARM/START`**，必须复用 P3/校准的 persona 流程（P0T/TRN-03B 正是这么做的）。台架侧可行方案：
  1. 在 P3 运行目录的 TDMA 阶段窗口内注入 relay（需要给 `hardware_acceptance_workflow` 增加一个“relay 注入”步骤，属 firmware 类改动，需 P3 复核）；
  2. 或单独跑一个“校准 persona + relay”脚本（等价复刻，但同样依赖 persona 的进入/退出顺序）。
- 本轮无代码变更；未起环，不记完成。**需要一次台架窗口**：P3 的 TDMA 阶段起环后，由 host 立即注入 relay（我已备好 `tdma_night --relay-node`）。

### OTA-TDMA-PROGRESS-20261030-039 - ARM/START 前置链定位到 runtime service

代码核对（`tdma_ring_runtime.c`）：
- `set_data_enabled(true)`（即 `RING:START`）要求 **`adapter_started != 0`**、`adapter_config_seq == config_seq`、`applied_config_seq == config_seq`、且 `train_command_seq == train_accepted_seq`；
- `adapter_started=1` 只由 **runtime service 循环**在 `up_down_config_ready` 时调用 `adapter_ops->start()` 后写入；
- `train_clock()` 又要求 `adapter_started != 0` 且 `data_enabled == 0`。

台架实测（两板 pair，NO.1/NO.2）：
- `RING:STOP` → `OPMode STAGe/APPLy`（level 7）→ `RING:TOPology 2,<slot>,0` 均被接受；
- `RING:ARM` 无回显但 `RING:ARM:STATus?` **返回 1（已接受）**；
- 之后 `adapter_started` 在 5 s 内始终为 0（`config_seq == applied_config_seq` 已满足），`START`/`TRAIN` 均 `-200`。

结论：**卡点是 runtime service 未运行/未推进 adapter start**，而不是校准门本身；P0 拓扑工具能起环，说明它在某条路径上让 service 跑了（P0 全程不 TRAIN）。下一步：定位 core0/core1 里 `tdma_ring_runtime_service` 的驱动条件（哪个模式/命令使能它），并复现 P0 的上下文；拿到 `adapter_started=1` 后再试 START，无需 TRAIN。

本轮无代码变更；未起环、未跑 relay，不记完成。

### OTA-TDMA-PROGRESS-20261030-038 - ARM→TRAIN→START 顺序验证：ARM 未完成

- 依据 `tdma_ring_runtime_train_clock()` 的前置条件（`enabled!=0`、**`adapter_started!=0`**、`data_enabled==0`），把顺序改为 STOP→STAGe/APPLy→TOPology→**ARM**→TRAIN→START。
- 实测（四板）：TOPology 后 `enabled=1, nodes=4`；`RING:ARM` **没有任何应答**（10 s 超时返回空），随后 `RING:STAT` 的 `adapter_started` 仍为 0；因此 `TRAIN` 与 `START` 继续 `-200`。
- 代码线索：`tdma_service_ring_arm()` 在 `calibration_gate_required != 0` 时要校验 calibration stage 的 profile/schedule CRC 与 staged config 一致，否则不启动 adapter——factory 刷新后校准态可能不满足该门。
- 结论：起环卡在 **ARM（校准门/adapter 启动）**，不是 TRAIN 命令本身；这与 P0 拓扑工具能跑通形成对照（P0 用的是另一条成对探测路径）。下一步对照 P0/`calibration_ring_topology` 的实际起环序列与校准注入方式。
- 未起环、未跑 relay，不记完成。

### OTA-TDMA-PROGRESS-20261030-037 - 起环 TRAIN 拒绝的定位（台架诊断）

- 直接下发完整序列（四板）：`RING:STOP` → `OPMode:STAGe 7` → `OPMode:APPLy` → `RING:TOPology 4,<slot>,0` 全部**被接受**（回读 `OPMode?` 显示 level 7 已激活、node_count=4）；`RING:TRAIN:STATus?` 返回 `CLKTRAIN,...,4,<slot>,...`（拓扑/槽位正确）。
- 但 `RING:TRAIN 256` 在 COM5（slot 0）仍返回 `-200,"TDMA_RING_TRAIN"`；`TRAIN:STATus?` 的请求序列没有推进（仍 0）。
- 代码路径：`tdma_ring_train` → `distributed_refmem_tdma_ring_train` → `tdma_runtime_owner_train_clock` → `resource_arbiter_request_tdma_clock_training`。arbiter 的前置条件（`components/resource_arbiter/src/resource_arbiter.c`）：initialized、mode 有效且非 FAULT/OTA、无 OTA admission、无 active FLASH 资源；随后 `tdma_runtime_owner_submit_training` 必须成功。任一不满足即立即 `-200`，与“0/256 cycles”现象一致。
- 台架旁证：`SYST:OTA:STAT?` 为 `IDLE`，`SYST:PROT:STAT?` 的 flash lockout 未处于请求/ack 状态。
- 下一步定位：读取 arbiter 当前 mode/active_resources（需确认正确的 SCPI 入口），以及 `tdma_runtime_owner_submit_training` 的返回值来源（persona/adapter 是否需先 ARM）；这两点决定 N2 能否在停机态起环。**本轮未起环、未跑 relay，不记完成。**

### OTA-TDMA-PROGRESS-20261030-036 - 失败 reason 透传（复核 #19）+ 背压 helper 单测

- `ota_tdma_binding_last_result()`：记录 relay/target 会话最近一次失败原因（`ota_tdma_session_failure_t`，成功时清零），不再把失败折叠成一个 `PROTOCOL` 或返回 `OK`。
- SCPI：`SYST:OTA:STREAM:STAT?` 的 relay 分支第 5 个字段由固定 `OK` 改为 `ota_tdma_binding_last_result()`（字段数不变，不破坏既有 host 解析）。
- 测试：`test_ota_stream_send` 新增 `wait_relay_durable` 两条（轮询到达 / 超时抛错），17 passed。
- 验证：8 个 TDMA host 套件全绿；`cmake_build_auto` 通过。
- 残余：target（非 relay）路径的失败仍通过目标自身 `STREAM:STAT?` 的 port 状态体现，未与 binding reason 合并；v1 节点限制与 HIL 未变。

### OTA-TDMA-PROGRESS-20261030-035 - relay 背压 + CLOSE 确认 + 节点 id/CAP 修正

- **CLOSE 确认（复核 #11）**：源末块 ACK 后发 `CLOSE` 进入 `WAIT_CLOSE_ACK`，收到目标 `ACK(offset==image_size, result==0)` 才 `DONE`；超时按预算重发 `CLOSE`。目标在 `CLOSE` 时 flush 暂存 → 关闭会话 → 用 durable 编码 ACK 回发。`READY_TO_REBOOT` 现在表示“目标确认收完”，不是“协调者发完”。
- **host relay 背压（复核 #7）**：`ota_stream_send --relay-node` 每个 DATA 块后 `wait_relay_durable(offset+len)`（轮询 `STREAM:STAT?` durable，带超时），避免第二块被 relay 窗口拒绝。
- **CAP advertise（复核 #18）**：`SYST:OTA:STREAM:CAP?` 的 v2 分支加入 `POTA_STREAM_CAP_RELAY`，host 可预检 relay 支持。
- **节点 id 单一来源（复核 #16 部分）**：SCPI 路由优先用 `tdma_maintenance_runtime_get_local_slot()`（环槽），仅在其不可用时回退 `board_identity_get_no()-1`。
- 验证：8 个 TDMA host 套件全绿（pump/loopback/session/carrier/binding/relay 均覆盖 CLOSE ACK 路径）；`test_ota_stream_send` + wiring 23 passed；`cmake_build_auto` 通过。
- 残余：`binding` 仍限 v1 两节点（slot ≤1，节点 2..4 不可用）——属既有 v1 约束，记入 G2；`wait_relay_durable` 无 host 级单测；HIL 未做。

### OTA-TDMA-PROGRESS-20261030-034 - OTA 帧走二等席解析器：take() 单一 owner

- 采用平台既有扩展点 `tdma_second_class_parser_register(type, fn, ctx)`（头部注释明确“adding a payload type registers a parser, not a new bus or command slot”），把 OTA 帧类型 `0x4F` 注册为二等席解析器：
  - `REFMEM` 命令任务仍是 `take()` 的**唯一** drainer；收到 `0x4F` 时由既有 `dispatch` 调 `ota_tdma_binding_handle_maintenance_frame`；
  - OTA 处理器按角色分发：relay 活动 → relay 会话（ACK 回程），否则 → target 会话（OPEN/DATA/CLOSE）；
  - 帧级 ACK 继续由 REFMEM 既有流程按 `accepted` 回写，不新增 ACK 事实。
- 结果：`ota_tdma_binding_service()` 不再调用 `take()`（target 走解析器、relay 只推进重发计时），复核 #2（目标无人服务）与 #8（三消费者并发）在 v1 路径上被消除；目标板无需本地 stream 会话即可被 OPEN 打开。
- 注册时机：`app_ota_service()` 首次调用即注册（与本地会话无关），处理器首次收帧时惰性 `binding_init`。
- 验证：8 个 TDMA host 套件全绿（binding 用例改为走 `handle_maintenance_frame`）；`cmake_build_auto` 通过。
- 残余：REFMEM 任务必须运行（是）；解析器表上限 8（当前 5）；并发注入 HIL 未做；节点 >1 仍受 v1 绑定限制（复核 #16）。

### OTA-TDMA-PROGRESS-20261030-033 - maintenance RX 单一 owner 决定（006c）

- 复核 #2（目标不被服务）与 #8（三消费者并发 take）是同一个根因：runtime 只有一个静态重组器，却被 refmem 命令面、OTA、SCPI 三处直接 `take()`。
- 本轮只做**决定**（写入设计 §4.3）：runtime 提供唯一 `service()` + 有界完成队列（堆分配，避开 RAM 上限）+ `register_sink(stream_id)`；REFMEM/OTA 改注册 sink，SCPI 只读计数；`take()` 退化为从完成队列弹出，禁止再碰适配器；目标服务门改为“runtime ready 即服务”。
- 新任务 `OTA-TDMA-006c`（G1）已入表，验收：三方并发零丢帧（host 注入用例）+ 目标板无本地会话可被 OPEN 打开。
- **本轮未改代码**：这是跨域接口变更（TDMA runtime + refmem + OTA + SCPI 四处），按 §8 硬规则不在未定方案时硬写；下一步先实现 runtime 完成队列与 sink 注册（含 host 用例），再迁调用方。

### OTA-TDMA-PROGRESS-20261030-032 - 暂存+块级 ACK 收敛：8/8 host 套件全绿

- 上一轮遗留的 ingress/loopback/carrier 测试已与“暂存 + 块级 ACK”语义对齐：
  - ingress：未到 512/256 阈值不落盘；首块 512、后续 256；`total_size==0` 时不触发“末字节 flush”（避免未知总量被当成结束）。
  - pump：`attempts` 只在块级递增（修复双递增）；块内逐帧发送、块结束等 ACK；重发整块。
  - carrier/loopback/session：ACK 以块末为准（20 B 镜像即 1 块）。
- 新增 `test_staging_flushes_header_and_pages`：16×64 B、总量 1024 → 写 512（offset 0）、256（512）、256（768），close 不再多写 —— 直接覆盖“包头 512 / 页 256”规则。
- 验证：**8 个 TDMA host 套件全绿**；`cmake_build_auto` 通过。
- 仍未做（复核清单）：目标服务门、`take()` 单 owner、relay 背压/CLOSE ACK、节点 id、CAP advertise、reason 透传；`ARCH-OTASTREAM-01` 仍 pending。

### OTA-TDMA-PROGRESS-20261030-031 - G1 改版进行中：页暂存 + 块级 ACK（**未完成**）

- 复核确认的根因不是“载体帧太小”，而是**任何 TDMA 帧都装不下 256 B flash 页**（`TDMA_LONG_FRAME_MAX` 仅 96 B）。因此改法是在 ingress 做**页/包头暂存**，而不是换 long frame：
  - `ota_tdma_ingress` 增 512 B stage：累计到 512 B（包/首块）或 256 B（后续页）或图像末字节时，才调用 `portable_ota_port_stream_write`；CRCl 在暂存块上计算。
  - 泵改为**块级 stop-and-wait**：首块 512 B、后续 256 B 为 ACK 粒度；块内逐帧发送，块结束后等 durable ACK；重发重发整块（接收端对重复帧幂等）。
- 已修：`ota_tdma_pump` 的 attempts 双递增、`unused parameter`、`send_block` 状态机、会话首块 512；`ingress` 末字节强制 flush；relay 以 `relay_tick(now)` 启动。
- 构建：`cmake_build_auto` 通过。
- **未完成（本轮超时，明示）**：8 个 host 套件中 6 个绿、**ingress 与 loopback 两个仍红**（测试期望与新暂存/块语义未对齐，正在收敛）；因此本轮不把任何相关条目写成完成，也不提交。
- 下一步：先把这两套测试与语义对齐（或修正实现），再跑全绿，然后处理复核清单里剩余的 ④/⑤/⑥ 项（目标服务门、take() 单 owner、relay 背压/CLOSE ACK、节点 id、CAP advertise）。

### OTA-TDMA-PROGRESS-20261030-030 - 独立复核结果：8 个 blocker，v1 载体需改版

独立复核（只读子代理）逐文件审查后给出 28 条 findings（8 blocker / 12 major / 8 minor）。本轮先修“内存破坏级”与两处低风险逻辑，其余进改版清单，**不把 pending 写成完成**。

- **已修**：
  1. `ota_tdma_codec` OPEN 解码越界：payload 数组原为 68（DATA 上限），OPEN 分片 body 可达 76 → 每次整片 OPEN 写坏 8 字节。改为 `payload[OTA_TDMA_OPEN_FRAGMENT_MAX]` + 两条 `_Static_assert`。
  2. ingress 重复 DATA 不再失败：同 offset/len/crc 的重传按幂等 OK 处理（丢 ACK 不终止会话）；同 offset 不同内容仍 `OFFSET`。
  3. relay 时间基准：`relay_feed` 不再用 0 启动会话；改由 `relay_tick(now_ms)` 启动/恢复，ACK deadline 用真实 uptime。
  4. binding 重建 OPEN 描述符时，未置 `CAP_RELAY` 则字节 37 写 0（与 `pota_stream_open_token` 一致，避免自产自拒）。
- **确认但未修（需要改版决策）**：
  - 目标侧从不被服务（`app_ota_service` 的门只认本地 stream/relay）；
  - 64/68 B 分帧与 256 B 页写入、512 B 包头约束根本冲突（maintenance 帧 80 B 上限）；
  - `tdma_maintenance_runtime_take()` 三处并发消费、无锁；
  - relay 无背压（第二块必被拒）、CLOSE 无确认、ABORT 不外传；
  - 旁观板对无 `CAP_RELAY` 的 OPEN 仍会打开；目标收到 ACK 会 FAILED；
  - `--relay-node` 可等于本机节点、`CAP?` 未 advertise RELAY、节点 id 双来源、night_runner 无单次超时、失败 reason 被折叠。
- 设计稿已加 §4.2：**v1 载体改版方向 = DATA 走 TDMA long frame（≥512 B），ACK/OPEN/CLOSE/ABORT 走维护流**；并纠正 §4.1 的 relay 触发规则（`destination_slot` 是 flash 槽，不是节点号）。
- 验证：本次修复后 8 个 TDMA host 套件全绿、`cmake_build_auto` 通过；`ARCH-OTASTREAM-01` 仍 pending。

### OTA-TDMA-PROGRESS-20261030-029 - relay 生命周期两处修复（自审）

- **admission 泄漏**：relay OPEN 会申请 OTA admission；此前 CLOSE 只回 OK、完成路径不释放，admission 会一直被占。修复：`ota_tdma_binding_service()` 在 relay 由 active 变 inactive 时 `resource_arbiter_release_ota_admission()`；`relay_abort()` 也释放。
- **陈旧 relay 状态遮蔽本地会话**：relay 完成后对象保留用于 `STREAM:STAT?`，但后续本地 `OPEN` 不会清理，`STAT?` 会一直报 relay 的 READY_TO_REBOOT。修复：新增 `ota_tdma_binding_relay_clear()`，在 `STREAM:OPEN` 解析前清理已完成（非在途）的 relay 记录。
- 构建通过；8 个 TDMA host 套件全绿。
- **独立复核（C11 视角）**：已启动子代理做只读审查（wire/codec/pump/relay/guard/lifecycle/C1-C2/host 工具/契约就绪度），本轮未回；返回后逐条归因并记录，不据此提前把 `ARCH-OTASTREAM-01` 转 active。

### OTA-TDMA-PROGRESS-20261030-028 - 旁观丢弃计数（内部快照）

- `ota_tdma_session`：新增 `ignored_opens`，当 `CAP_RELAY` 且 `relay_node != local_node` 时递增（原逻辑丢弃但不可见）；`ota_tdma_session_ignored_opens()` 只读访问。
- `ota_tdma_binding_counters_t`：新增 `ignored_opens`，`get_counters` 时从当前 target 会话读取（与 `relay_frames_rx/tx/aborts` 同结构）。
- 测试：session 旁观用例断言 `ignored_opens == 1`；binding 用例保持通过；构建通过。
- 未覆盖：尚未暴露到 SCPI（`STREAM:STAT?` 字段固定 5 个，新增需契约变更），留到 N2 决定输出面。

### OTA-TDMA-PROGRESS-20261030-027 - 夜间执行器与 relay 串联

- 新增 `tools/ota/ota_stream_send/tdma_night.py`：`RelayNightPlan`（port/package/relay_node/attempts/wall-clock/block-size/session-id/recover）→ 生成 `ota_stream_send --relay-node …` 命令并交给 `run_night`。
- `--dry-run` 输出 `OTA_TDMA_NIGHT_PLAN_V1`（命令、attempts、out_dir、recover），不写目录、不连设备；真实模式写 `attempt-XX/` 与 `summary.json`。
- 恢复：`--recover <cmd...>` 直接复用 night_runner 的恢复钩子（恢复失败留 `recover-error.txt`）。
- 测试 `tests/python/test_ota_tdma_night.py`：命令含 relay-node/package/port/block-size/session-id、越界节点、非法预算、dry-run 不落盘、JSON schema —— 5 passed。
- 冒烟：`--dry-run` 输出命令 `… ota_stream_send <pkg> --package --port COM5 --relay-node 1 --block-size 4096`。
- 未覆盖：真实 relay 跑（台架 bring-up 仍阻塞）。

### OTA-TDMA-PROGRESS-20261030-026 - OTA-TDMA-013（部分）：夜间执行器

- 新增 `tools/ota/ota_stream_send/night_runner.py`：`NightPlan(attempts, wall_clock_s, out_dir, command)`；`run_night()` 每个 attempt 建 `attempt-XX/`、写 `result.json`（UTC 起止、时长、exit code、ok、是否恢复、异常文本），结束写 `summary.json`（schema `OTA_TDMA_NIGHT_RUN_V1` + `passed`）。
- 预算与恢复：到 wall-clock 即停（不再开新 attempt）；上一 attempt 失败且提供了 `recover` 时，在下一 attempt 前执行恢复命令并把 `recovered=true` 记到该 attempt；恢复失败写 `recover-error.txt` 且不掩盖原失败。
- CLI：`python -m tools.ota.ota_stream_send.night_runner --attempts N --wall-clock-s S --out-dir <dir> [--recover <cmd...>] -- <transfer command...>`；冒烟跑通（`out/night_smoke`）。
- 测试 `tests/python/test_ota_night_runner.py`：第二次成功、预算到点停止、恢复回调、runner 异常、非法参数 —— 5 passed。
- 未覆盖：与真实 `ota_stream_send --relay-node` 串联（需 N2）、resume 语义（`--resume` 在 relay 模式下被禁，见 014）。

### OTA-TDMA-PROGRESS-20261030-025 - OTA-TDMA-014（部分）：host relay 发送

- `tools/ota/ota_stream_send/ota_stream_send.py`：新增 `--relay-node 0..31`；`encode_open()` 增 `relay_node` 参数，置 `POTA_STREAM_CAP_RELAY`（bit2）并把节点号写入描述符字节 37（38/39 保持 0）；越界直接 `ValueError`。
- CLI 约束（fail-closed）：`--relay-node` 不能与 `--boot-and-commit`（会误启动协调者）或 `--resume`（协调者 journal 不是目标 journal）组合。
- 数据面：relay 模式仍用既有 `STREAM:DATA` 分块与 `STREAM:STAT?` 的 durable 背压；`CLOSE` 后等待 `READY_TO_REBOOT`（固件在 relay 完成时上报），不执行本地 boot/commit。
- 假设（v1）：协调者与目标同 build/同 map，故协调者的 `SYST:OTA:TARG?`/`MAP?` 可代表目标；跨 build 场景需后续把目标参数显式化。
- 测试：`tests/python/test_ota_stream_send.py` 15 passed（新增 relay 编码/越界用例）。
- 未覆盖：两板实跑（台架 bring-up 仍阻塞）、`--recover` 与夜间执行器（013）。

### OTA-TDMA-PROGRESS-20261030-024 - P0 拓扑可跑通，但 TRAIN 仍拒绝（N2 继续阻塞）

- `calibration_ring_topology.py --adjacency-only`（4 板，anchor NO.1）：`passed=true`，`ring_order=[NO.1,NO.2,NO.3,NO.4]`（`out/n2/p0t/summary.json`）——拓扑/邻接可用，物理顺序与我们传入一致。
- 随后 `tdma_start_ring.py`（4 板、level 7、cycles 256）：仍在 NO.2 `FB276192BEF9CCE1` 的第 0 个 chunk 被 `-200,"TDMA_RING_TRAIN"` 拒绝。
- 判断：TRAIN 需要的是 P3/TRN-03B 那套“已标定的 ring 剖面 + 角色/偏移”状态，`tdma_start_ring.py` 的 TRAIN 序列在 factory 刷新后的板上不成立；这不是 OTA/relay 代码问题。
- 处置：暂停台架起环尝试（避免反复空跑），下一轮转 host 侧可交付项（`014` host 工具、`013` 夜间执行器）；N2 改为“需要专用台架窗口 + 与 P3 相同的 bring-up 步骤”的排期项，届时由用户在场执行。
- 现场：四板 `20261029173606`、`IDLE`、环未起。

### OTA-TDMA-PROGRESS-20261030-023 - N2 起环尝试（未成功，记录阻塞）

- 四板刷到 `20261029173606`（含 relay 路由）；`tdma_start_ring.py` 用 serial 号寻址（`--board-id <serial>`，不是整行 IDN）。
- 起环结果：`TRAIN` 在 NO.2（`FB276192BEF9CCE1`）第 0 个 chunk 即 `-200,"TDMA_RING_TRAIN"`；直接 `RING:STARt` 同样 `-200,"TDMA_RING_START"`。P0 摘要里也记录了 `START acknowledgment is refused` 的 transport recovery。
- 判断：停机态起环依赖 P0/P3/T3 那套“拓扑 + 剖面 + 角色”带起流程与既有标定状态，单独跑 `tdma_start_ring.py` 不成立；N2 必须复用 P3 工作流的 TDMA bring-up（在其运行目录内注入 relay 步骤），或补一个专门的 bring-up 步骤。
- 结论：N2 未执行、**不记完成**；本轮不伪造环/relay 证据。台架保持四板 `IDLE`、环未起。

### OTA-TDMA-PROGRESS-20261030-022 - relay 路由字段定稿并重接线

- wire：`pota_stream_session.h` 新增 `POTA_STREAM_CAP_RELAY (1<<2)` 与 `pota_stream_open_t.relay_node`；`pota_stream_wire.c` 解码只在置位时允许字节 37 非零（38/39 恒 0），`open_token` 同步纳入；session 的 known-capabilities 加入 RELAY。
- 目标侧：`ota_tdma_session_init_target_for_node(session, transport, local_node)`；OPEN 重组后若 `CAP_RELAY` 且 `relay_node != local_node` → 丢弃并重置重组、保持 IDLE（不判失败）。binding 用本地节点初始化。
- 协调侧：`STREAM:OPEN` 在 `CAP_RELAY && relay_node != 本节点` 时进入 relay（`ota_tdma_binding_relay_start`）；本节点 OPEN 仍走本地 port。
- 测试：session 套件新增“字节 37 无 capability 被拒”“node0 OPEN 被 node1 丢弃、被 node0 接受并进入 RUNNING”；8 个 TDMA host 套件全绿。
- 构建：`cmake_build_auto` 通过。
- 未覆盖：两板 HIL（起环 harness 仍缺）、relay 计数暴露、power-cut。

### OTA-TDMA-PROGRESS-20261030-021 - 撤销错误 relay 路由（destination_slot 语义纠正）

- **发现**：`pota_stream_open_t.destination_slot` 在既有 stream wire 里是 **flash A/B 槽位（1/2）**，不是 TDMA 环节点号（0..3）。上一轮用 `destination_slot != local_slot` 判定 relay，会把“目标 flash 槽=1 的本地 OPEN”在 NO.2 上误判、在 NO.1 上误判为 relay；目标侧护栏同样用错语义，可能让旁观板拒绝本应接受的 OPEN。
- **处置（fail-closed）**：撤销 SCPI OPEN 的 relay 分流分支、撤销 `ota_tdma_session_init_target_for_slot()` 与节点槽护栏（API/字段/用例一并移除）；relay 模块与 binding 的 `relay_*` API 保留但**当前不可达**（无触发点），不计为完成。
- 构建：`cmake_build_auto` 通过（build_id `20261029172923`）；session 套件恢复通过。
- **待决（下一轮先决策）**：relay 路由字段。候选：(a) 用 OPEN 描述符保留字节 `37..39` + 新增 capability 位（需改 `pota_stream_wire` 解码与两端测试）；(b) 用 `object_id` 高字节约定（不改 wire，但污染对象语义）；(c) 独立 relay 命令（新增命令槽，需注册）。在此之前**不重新接线**。
- 另：`RING:STARt` 停机态返回 `-200`，N2 仍需 bring-up harness（标定/剖面 → 起环 → relay）。

### OTA-TDMA-PROGRESS-20261030-020 - 目标槽守卫 + 台架起环探测

- 新增目标侧守卫：`ota_tdma_session_init_target_for_slot(session, transport, local_slot)`；OPEN 重组完成后若 `destination_slot != local_slot` 则**不打开** ingress、保持 IDLE（广播帧不会误升级旁观板）。binding 用本地 slot 初始化。
- host 用例：旁观板（slot1）收到 destination=0 的 OPEN 两分片后仍 `IDLE`、`target_open=false`；同一 OPEN 改destination=1 后正常进入 `RUNNING`。
- 构建通过（build `20261029172256`）；四板已用该 build 的 FACTORY uf2 刷写，全部 `IDLE`。
- **N2 台架探测（失败）**：四板停机态直接 `SYST:TDMA:RING:STARt` 均返回 `-200,"TDMA_RING_START"`，`RING:STAT?` 仍为未运行；确认起环需要标定/剖面流程（P0/P3/T3 一类）先建立拓扑/剖面，之后才能 relay。
- 结论：`006b` 的软件路径已齐（host+SCPI+build），**N2 缺的是台架 bring-up harness**：需要一个“用既有标定带起环 → 注入 STREAM relay → 收环”的流程步骤；本轮不伪造 N2 证据。

### OTA-TDMA-PROGRESS-20261030-019 - OTA-TDMA-006b：SCPI relay 接线

- 绑定层：`application/src/ota_tdma_binding.c` 增 relay 生命周期（`relay_start/feed/abort/active/done/durable` + 计数 `frames_rx/frames_tx/aborts`）；relay 状态与 carrier/session 同走 FreeRTOS 堆；relay 结束后保留对象供 `STAT?` 读取，下一次 OPEN 复用。
- 服务路径：`app_ota_service()` 在 `stream 会话 OR relay 活动` 时运行；relay 期间 `binding_service` 只 `carrier_poll(relay.session)` + `relay_tick`，不再驱动 target 会话，避免源/目标抢同一 `take()`。
- SCPI：`scpi_ota_commands.c` 的 `STREAM:OPEN` 以 `destination_slot != board_identity_get_no()-1` 分流到 relay（不写本地 flash、不建 port 会话）；`DATA` → `relay_feed`；`CLOSE` 直接 OK（传输由目标 ACK 收口）；`ABORT` → `relay_abort` 并释放 admission；`STREAM:STAT?` 在 relay 期间返回 `source=TDMA`、`state=RECEIVING|READY_TO_REBOOT`、`durable=relay durable`。
- 验证：binding/relay host 用例通过；`cmake_build_auto` 通过（build_id `20261029171950`）；wiring 静态用例 8 passed。
- 未覆盖：SCPI 分支无 host 级行为用例（只有静态/编译），两板 HIL 未跑；relay 的 `consumed` 与 host 侧既有 `wait_consumed` 的兼容需在 N2 实测。

### OTA-TDMA-PROGRESS-20261030-018 - OTA-TDMA-006b：relay host 实现（窗口 + 分帧）

- 泵新增流式数据源：`ota_tdma_pump_init_streaming(pump, size, crc, chunk, attempts, timeout, crc32, read, ctx)`，chunk 由 `read()` 拷入 68 B 局部缓冲；resume `ota_tdma_pump_resume()` 在“源暂无数据”时保持 `WAIT_ACK` 不误判失败。会话加 `ota_tdma_session_resume()`。
- 新增 `components/ota_manager/{inc,src}/ota_tdma_relay.*`：4 KiB 单块窗口；`chunk_size` 必须整除窗口（4096/64），否则 `relay_open` 拒绝；`feed` 只接受连续块且上一块必须已 durable；`on_frame` 分发 ACK/ABORT 并镜像 durable（**先镜像再判终态**——终态帧同样要更新 durable，这是首版 bug）；`tick` 推进重发。
- 测试 `tests/unit/test_ota_tdma_relay.c` + `run_ota_tdma_relay_tests.ps1`：3×256 B（64 B 分帧）经伪环 ACK 全链 → durable 768、CLOSE offset 768、会话 DONE；非连续/重叠块被拒；窗口非整数倍拒绝。
- 全量回归：8 个 TDMA host 套件全绿；`cmake_build_auto` 通过（build_id `20261029170958`）。
- 边界：SCPI/STREAM 侧尚未把 relay 接进 OPEN 分流（`destination_slot != 本板` 的判定与 consumed 映射未接线）；两板 HIL 未做。

### OTA-TDMA-PROGRESS-20261030-017 - 源侧取包路径决策（设计 §4.1）与 006b 立卡

- 背景：`006a` 绑定了目标角色，但“协调者从哪里取包”没有任何实现路径——RAM 放不下整包，现有 USB 流不会自动转环。
- 决策（写入设计稿 §4.1）：选 **B. USB 边收边转（relay）**。host 仍用既有 `SYST:OTA:STREAM:*`；`OPEN.destination_slot` 指向目标槽时，协调者不打开本地 port 会话、不擦写自身 flash，用同一描述符初始化 source 会话，把收到的字节按 68 B 分帧转发；host 的 consumed 以**目标 durable ACK** 为准。
- 否掉的方案：A（SD 中转：依赖插卡与读吞吐、多一个文件状态机）、C（回读自身 slot：package 的 manifest/hash 不在槽内连续区）。
- 新任务 `OTA-TDMA-006b`（G1）已入表，退出判据：2 板 ≥300 KB relay → 目标 `READY_TO_REBOOT`、环不损坏、协调者擦写计数不增长、失败回 ABORT 走既有分类。
- 未实现：relay 分支本身（`portable_ota` 入口分流 + 窗口缓冲 + durable 映射），下一轮开始；本轮只有设计决策，不冒充实现。

### OTA-TDMA-PROGRESS-20261030-016 - OTA-TDMA-006a 固件绑定与 AO 接线

- TDMA 域新增只读 `tdma_maintenance_runtime_get_local_slot()`（不改变维护流行为）。
- 新增 `application/src/ota_tdma_binding.c` + `application/inc/ota_tdma_binding.h`：
  - `init`：取本板 slot；**v1 只接受 slot 0/1**（两节点），peer = 另一槽，`target_mask = 1<<peer`；绑定 `tdma_maintenance_runtime_submit/take/ack`；
  - `service(now, max_frames)`：调用 `ota_tdma_carrier_service`；会话进入 `DONE/FAILED` 后立即重置 target 角色以接收下一次 OPEN；
  - 状态（carrier+session+transport ≈250 B）走 **FreeRTOS 堆**（`pvPortMalloc`），因为目标镜像 RAM 已到上限：静态放置会 `region RAM overflowed by 96 bytes`。
- `application/src/app.c`：`app_ota_service()` 在 `ota_ao_tdma_maintenance_allowed()` 为真时惰性 init 并 `service(board_uptime_ms(), 2)`；legacy USB OTA 路径完全不变。
- host 用例 `tests/unit/test_ota_tdma_binding.c` + `run_ota_tdma_binding_tests.ps1`：slot 2 拒绝、slot 读取失败 fail-closed、slot 0→peer1/mask0x02、OPEN×2+DATA+CLOSE 全链（ingress 写入、帧级 ack 计数 1/2/3/4、DATA 触发 1 次 ACK 提交、CLOSE 后角色重置）。
- 构建：`cmake_build_auto` 通过（build_id `20261029170342`）；binding 单测通过。
- 未覆盖：真实两板/Ring 上尚未跑（需静态维护态 + stream 会话入口）；上一轮 P3 receipt（`d3997aa9`）已因本轮代码变化失效，提交前需重跑同指纹 P3。

### OTA-TDMA-PROGRESS-20261030-015 - 正式 P3 QUICK 凭证产出（同指纹，DPLL PASS）

- 运行：`python tools/hardware_acceptance/p3_hardware_acceptance.py run`（QUICK，冻结工作区，无中途编辑），run 目录 `out/HardwareAcceptance/20261030/p3-005122`。
- 凭证：`config/hardware_acceptance/p3_acceptance_receipt.json` — schema `HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_TDMA_4NODE_QUICK_GRADED_V1`、`acceptance_scope=FOUR_NODE_DPLL_QUICK_DIAGNOSTIC`、`passed=true`、`strict_gates_passed=false`、build `20261029165133`、fingerprint `d3997aa9…`、1632 files。
- 阶段：P0/P1/P2/P3/T0/T1/T2/T3 **PASS**；TDMA `PASS_WITH_WARNINGS`（既有 vdc/refmem deadline 增长与 TRN-03 质量门）；**DPLL PASS**（上次失败的示波器窗口本轮 12/12 命中）。
- OTA 四板：`passed=true`、`updated=4`、`failed=0`、`elapsed=174.17 s`（`ota-four-board/summary.json`）。
- 结论：本批（`011–014` + codec/ingress/pump/session/carrier）在真实四板链路上通过，凭证与工作区源码指纹一致。
- 未闭合：`010` 的“真实 power-cut”仍是单独门禁；提交尚未执行——工作区还有**与本批无关**的 `tools/sync_trigger/*.py`、`docs/trigger/t2/T2_FLOW.html`、`docs/validation/P3_FLOW.html`、`config/hardware_acceptance/t2_acceptance_receipt.json` 改动。若只 stage OTA 本批，staged 指纹会与 receipt 的 `d3997aa9` 不一致（receipt 覆盖全部 firmware 源），因此提交需先把这批 T2 改动纳入同一次凭证范围，或由用户确认其归属。

### OTA-TDMA-PROGRESS-20261030-014 - OTA-TDMA-006a：AO 有界收帧入口

- `ota_tdma_carrier_service(carrier, session, now_ms, max_frames)`：在 AO 任务里一次最多 drain `max_frames` 个维护帧（`poll`），随后推进一次 source 重发计时（`tick`）；全程不阻塞、不进临界区、不碰 flash，符合“静态维护态只服务维护流”的边界。
- host 用例扩展：会话 start 后队列一个 durable ACK，`service(max_frames=1)` 只 drain 一帧并回 ack，会话保持 RUNNING；既有发送/代数/坏帧 NACK 用例不变。
- 构建：`cmake_build_auto` 通过（build_id `20261029164916`）。
- **host 工具 `--method tdma` 暂缓（记录理由）**：新工具目录需同步 `tools/tool_layout.json` / 注册表，属独立治理面；本轮先把固件侧 AO 入口做扎实，工具留到与 `OTA_workflow` 的 reserved 分支一起改，避免出现“未注册工具目录”。
- 边界：AO 任务里的实际接线（`task_ota` 调 service）与真实 `tdma_maintenance_runtime_*` 绑定尚未提交；台架仍阻塞。

### OTA-TDMA-PROGRESS-20261030-013 - OTA-TDMA-006a：maintenance 载波接缝

- 新增 `components/ota_manager/{inc,src}/ota_tdma_carrier.*`：把会话接到 TDMA 维护流上，但**不把 TDMA 头文件拉进 OTA 域**——submit/take/ack 由调用方注入（固件传 `tdma_maintenance_runtime_*`，host 传桩）。
  - `send`：帧长必须 1..80 B；每次提交把 generation+1，`tdma_maintenance_runtime_ack()` 因此能拒绝旧帧的迟到 ACK。
  - `poll`：`take()` 取一帧 → `session_on_frame` → 用取到的 `(stream_id, generation)` 回帧级 ACK（成功 release、失败强制重试）。
- host 用例 `tests/unit/test_ota_tdma_carrier.c` + `run_ota_tdma_carrier_tests.ps1`：发送字段（stream/mask/长度/代数递增）、空/超长拒绝、会话 start 触发 3 次 submit（OPEN×2+DATA）、队列 ACK 推进泵并回 ack(ok=true, source=peer)、坏帧使会话失败并 ack(ok=false)。
- 构建接线：`CMakeLists.txt` 加入 `ota_tdma_carrier.c`；`cmake_build_auto` 通过（build_id `20261029164625`）。
- **已知 v1 简化（必须记录）**：`tdma_maintenance_runtime_take()` 只返回 frame/stream/generation，**不返回 source_slot**，而 `tdma_maintenance_runtime_ack()` 需要它。两板闭环用固定 `peer_slot` 可行；四板/多源必须先在 TDMA 域扩展 take 的返回签名（属于跨域接口变更，G3 前处理）。
- 边界：真实固件绑定（在 app/ota 任务里调 poll）尚未接线；台架仍阻塞。

### OTA-TDMA-PROGRESS-20261030-012 - OTA-TDMA-006a：角色会话（source/target）与队列联调

- 新增 `components/ota_manager/{inc,src}/ota_tdma_session.*`：把 codec/ingress/pump 组装成角色会话。
  - source：`start` 先发 OPEN 两分片，再启动泵；只处理 ACK（ABORT 直接失败）；`tick` 触发重发。
  - target：OPEN 分片重组后用 `pota_stream_open_decode_le` 打开 ingress；DATA → ingress → 若 durable 覆盖则回 ACK（失败时回带 result/reason 的 ACK 并失败）；CLOSE → ingress close → DONE；ABORT → FAILED/REJECTED。
  - 传输是注入的 `ota_tdma_transport_t{send, context}`：固件侧接 maintenance submit，host 侧接队列；会话绝不递归调用传输。
- host 联调 `tests/unit/test_ota_tdma_session.c` + `tools/tests/run_ota_tdma_session_tests.ps1`：source/target 两会话通过 64 深度队列互投，20 B（8/8/4）完整走完 OPEN→DATA×3→ACK×3→CLOSE，双侧 DONE、镜像字节一致；另含“DATA 先于 OPEN 必须协议失败”“ABORT 必须 rejected”两条负向。
- 构建接线：`CMakeLists.txt` 加入 `ota_tdma_session.c`；`cmake_build_auto` 通过（build_id `20261029164325`）。
- 边界：真实 maintenance stream 提交/接收还没接（`ota_tdma_transport` 的固件实现），两板 HIL 未做；`006a` 保持 IN PROGRESS。

### OTA-TDMA-PROGRESS-20261030-011 - OTA-TDMA-006a 目标侧 ACK 与 host 回环

- 目标侧：`ota_tdma_ingress` 新增 `ota_tdma_ingress_ack_ready()`（仅当 `durable_offset >= expected_offset` 且已接收过数据）与 `ota_tdma_ingress_encode_ack()`（用当前 durable 编码 ACK，可带 result/reason）。这样 stop-and-wait 的 ACK 只在“已落盘”后发出，与泵的判据（ACK offset == 本帧末端）对齐。
- 新增回环 host 用例 `tests/unit/test_ota_tdma_loopback.c`：真实 pump + codec + ingress + 伪会话（写入即 durable）串联，20 B 镜像按 8/8/4 三分片 → 三 DATA + CLOSE；校验目标镜像字节一致、`write_calls=3`、`close_calls=1`、泵 `DONE` 且 durable=20、ingress expected/durable=20。
  - 回环刻意**不使用同步回调递归**：`send` 只投递并把可发的 ACK 挂起，由主循环逐条 `pump_on_ack`，与固件“submit-only”语义一致（避免 2.2 MB 时深层递归）。
- ingress 用例新增 ACK 生成分支：未收数据不 ready、durable 未覆盖不 ready、覆盖后 ready 且 ACK offset/result 正确、失败 result/reason 编码正确。
- 验证：codec / ingress / pump / loopback 四个 host 套件全绿；`cmake_build_auto` 通过（build_id `20261029164029`）。
- 边界：真实载波（maintenance stream submit/RX）尚未接；`006a` 仍 IN PROGRESS，台架两板闭环待 002 之后。

### OTA-TDMA-PROGRESS-20261030-010 - OTA-TDMA-002：静态维护态 TDMA 服务降级

- 变更 `components/vdc_dpll_manager/src/vdc_dpll_manager.c`：`tdma_component_core1_service()` 的早退由无条件 `ota_ao_is_active()` 改为 `ota_ao_is_active() && !ota_ao_tdma_maintenance_allowed()`。
- 新增 `ota_ao_tdma_maintenance_allowed()`（`components/ota_manager/src/ota_ao.c`）：仅当 `portable_ota_port_stream_is_active()`（静态维护态使用 stream 会话）为真；legacy raw-data 会话保持原行为——环空闲、core1 lockout poll 紧凑，避免影响现有 USB OTA。
- 边界与理由：stream 会话下环必须活着才能承载 maintenance 流；flash program/erase 期间 `drv_flash_lockout_core1_poll()` 仍会 park core1，服务在一个有界 pass 后恢复，符合设计稿 §3 的“分块 + 容忍停车”。
- 静态门禁（`tests/python/test_ota_haofv_wiring.py` 新增用例）：函数体内必须是条件早退、core1 侧不得出现 `drv_flash_` 调用、允许判据必须来自 stream 会话。
- 构建：`cmake_build_auto` 通过（build_id `20261029163715`）。
- 未覆盖：两板 HIL 需证明 OTA 会话期间 `TDMA:RING:STATus?` 保持 running、且升级后 P3/T2 恢复；台架当前仍被 DPLL 示波器窗口与 TDMA 启动屏障阻塞。

### OTA-TDMA-PROGRESS-20261030-009 - OTA-TDMA-006a：源侧 stop-and-wait 泵

- 新增 `components/ota_manager/{inc,src}/ota_tdma_pump.*`：一次只有一个 DATA 帧在途；只有收到“本帧覆盖的 durable offset”才前进；陈旧 ACK（等于当前 durable）忽略、越界 ACK/非零 result 直接 `FAILED`；超时用 `(int32_t)(now-deadline)` 回绕安全比较重发同帧，`max_attempts` 用尽后 `FAIL_RETRIES`；末帧 ACK 后发 `CLOSE` 并转 `DONE`。
- 设计取舍：泵不依赖 port/CRC 实现——chunk CRC 与发送都走回调（固件侧可传 `ota_crc32_compute` 与 maintenance submit），因此 host 可完整验证状态机。
- 测试 `tests/unit/test_ota_tdma_pump.c` + `tools/tests/run_ota_tdma_pump_tests.ps1`（注册进总门禁）：两分片 happy path（68+32，末帧 FINAL、CLOSE 携 image CRC）、deadline 前不发/到点重发/次数耗尽、陈旧 ACK 忽略、越界 ACK 判协议错、rejected result、发送回调失败、init 参数拒绝。
- 构建接线：`CMakeLists.txt` 加入 `ota_tdma_pump.c`；`cmake_build_auto` 通过（build_id `20261029163430`）。
- 边界：目标侧 ACK 生成与真实载波（maintenance stream submit、park 期间停发）尚未接入，`006a` 保持 IN PROGRESS，等 `002` 后两板 HIL。

### OTA-TDMA-PROGRESS-20261030-008 - OTA-TDMA-005：TDMA ingress 源与薄适配

- `third_party/portable_ota/include/pota_stream_ingress.h`：枚举新增 `POTA_STREAM_INGRESS_TDMA`（在 RS485 之后），`SOURCE_COUNT` 自动 +1，端口 source_mask 由计数派生，无需改调用方。
- 新增 `components/ota_manager/{inc,src}/ota_tdma_ingress.*`：**不复制 session**，只做三件事——校验 `expected_offset`、把 DATA 载荷转发给 `portable_ota_port_stream_write(POTA_STREAM_INGRESS_TDMA, …)`、从 `get_status` 镜像 `durable_offset` 并在 FAILED/ABORTED/READY_TO_REBOOT 时关闭本地 open 标志。端口拒绝时**不**推进期望 offset。
- 测试 `tests/unit/test_ota_tdma_ingress.c` + `tools/tests/run_ota_tdma_ingress_tests.ps1`（注册进 `run_host_unit_tests.ps1`）：open 透传源/转发字段（source/offset/size/crc/flag）、offset 失配不调端口、坏帧拒绝、端口错误映射且 offset 不变、close 失败保持 open、状态镜像 4096 B 与终态关闭。
- 构建接线：`CMakeLists.txt` 加入 `ota_tdma_ingress.c`；`cmake_build_auto` 通过（build_id `20261029163141`）；`run_portable_ota_tests.ps1` 仍全绿（枚举扩展未破坏现有 ingress 用例）。
- 边界：尚无 TDMA 载波调用方（`OTA-TDMA-006a` 的 stop-and-wait 泵），因此本模块目前只有 host 证据。

### OTA-TDMA-PROGRESS-20261030-007 - OTA-TDMA-004：TDMA OTA 四帧 codec（golden + fuzz）

- 新增 `components/ota_manager/{inc,src}/ota_tdma_codec.*`：帧头 4 B（type/flags/seq/total），帧上限 80 B（= `TDMA_MAINTENANCE_STREAM_MAX`）。
  - `OPEN`：88 B 本地描述符按 76 B 分 2 片，`total` 必须等于 2，乱序可重组、重复片段重置（不静默接受冲突）。
  - `DATA`：offset/u32 + crc32/u32 + payload ≤68 B，flags 必须带 CRC，可带 FINAL。
  - `ACK`：durable_offset/u32 + result/u8 + reason/u8（10 B）；`CLOSE`：offset/u32 + crc32/u32（12 B）；`ABORT`：reason/u8（5 B）。
  - 所有编码器在容量不足或参数非法时返回 0（fail closed，不截断）；解码器对未知类型、长度不符、seq≥total、超长帧一律拒绝。
- 测试 `tests/unit/test_ota_tdma_codec.c` + `tools/tests/run_ota_tdma_codec_tests.ps1`（已注册进 `run_host_unit_tests.ps1`）：
  golden 字节向量（DATA/ACK/CLOSE/ABORT/OPEN 分片）、拒绝矩阵（未知类型/缺 CRC/长度错/超长/截断/容量不足）、
  20 000 次随机解码不越界、5 000 次 DATA 往返字段一致。
- 构建接线：`CMakeLists.txt` 加入 `ota_tdma_codec.c`；`cmake_build_auto` 通过（build_id `20261029162833`）。
- 边界：本轮只锁 wire 语义与边界，**尚未接入 maintenance stream 载波与 ingress**（`OTA-TDMA-005/006a`）；台架复验待接入后进行。

### OTA-TDMA-PROGRESS-20261030-006 - P3 批量验收：四板 OTA 4/4 通过，凭证被 DPLL 示波器窗口阻塞

- 修复后的同指纹 P3 QUICK（run `out/HardwareAcceptance/20261030/p3-001332`，build `20261029161342`，同源码指纹 `b3ab402c…`、1616 files）：
  - **OTA 四板：`passed=true`、`updated_count=4`、`failed_count=0`、`elapsed_s=175.95`**（`ota-four-board/summary.json`，SHA256 `16240b07…`）。这同时覆盖 `OTA-HAOFV-014` 的“既有验收窗口内完成”与 `013/012/011` 在真实接收路径上的可用性。
  - P0/P1/P2/P3/T0/T2 通过；T1/T3/TDMA 为 `PASS_WITH_WARNINGS`（TRN-01/TRN-03 质量门与 `vdc/refmem deadline_miss` 增长，属既有实时预算问题，非本次 OTA 改动引入）。
  - **DPLL（required）FAIL / BLOCKED**：两次采集均 `Capture exited 2`，`scope 8–10/12 windows missed`、`STARVED`（`dpll-short.log`、`dpll-short-attempt2.log`）。外置 HDO 触发窗口未命中，属台架/探头条件；`alarms.json` 记录 `STAGE_MISSING DPLL`，因此本轮只写出 provisional joint receipt，**不是正式 P3 凭证**。
- 结论：OTA 路径本身在四板真实链路上通过；`OTA-TDMA-000` 仍缺“正式 receipt + C11”，阻塞点是 DPLL 外置示波器窗口，需要台架侧处理（探头/触发/供电），不得用 scope 缺失冒充 SKIPPED。
- 追加诊断（2026-10-30）：用 `dpll60 --scope internal` 复跑同一 package，仍在 TDMA stopped handoff 处报 `explicit startup barrier timed out`（TRN-03B），未进入 DPLL 采集 ⇒ 台架阻塞不止外置示波器：TDMA 启动屏障当前也不成立；本轮停止台架消耗，转 host 侧 G1 实现。
- 现场状态：四板已统一到修复后 build `20261029160616`（picotool FACTORY 载入旧 build 板卡后由 P3 再刷），全部 `IDLE`，无挂起 OTA 会话。

### OTA-TDMA-PROGRESS-20261029-005 - P3 四板 OTA 全崩回归：OTA 栈 1152 words 溢出（已修复并实测定栈）

- 现象（P3 QUICK，`out/HardwareAcceptance/20261029/p3-234931/ota-four-board/summary.json`）：四板 OTA 全部 `failed_count=4`，`ota_send` 在 block 1 报 `link_lost: WriteFile failed (PermissionError(13,...))`，设备随即复位（重连后 `SYST:OTA:STAT?` 为 `IDLE`），板子仍运行原 build。
- 归因过程：
  1. 旧固件（build `20261029103949`，picotool FACTORY UF2 载入）用同一 package 传输 **成功**（`send=0`）⇒ 新代码回归；
  2. 用真实 manifest 的单块探针复现“写入后设备复位”；raw `SYST:OTA:BEGIN`（未发任何 DATA）同样复位 ⇒ 崩溃在 **BEGIN/erase** 阶段，与程序路径无关；
  3. 把 `APP_OTA_TASK_STACK_WORDS` 恢复为旧值 1921 words（其余改动不动）→ 四板 OTA 传输 **成功** ⇒ 溢出根因确认：1152 words 低于真实峰值。
- 实测定栈：成功后读 `SYST:RTOS:STAT?`（build `20261029160058`）：`ota words=1920 free=492 used=1428`；改 `APP_OTA_TASK_STACK_WORDS=1792u` 后（build `20261029160616`）单板全包传输成功，`ota words=1792 free=364 used=1428`（≈25% 余量）。
- 变更：`application/src/app_tasks.c` 用实测常数 1792 取代公式，并在注释记录 1428 峰值与 1152 溢出教训；`tests/python/test_ota_haofv_wiring.py` 改为断言“实测常数 ≥1700 且注释含 1428”。
- 教训（流程）：栈/缓冲类改动**必须用 high-water 实测反证**，不能用“已移除块大小帧”推导；P3 四板 OTA 是这类回归的唯一有效门禁。
- 未覆盖：P3 正式 receipt 尚未重跑（上次因运行中改文件导致 provisional 指纹不符 + 本次回归）；`OTA-HAOFV-012/013` 的 HIL 收尾仍待做。

### OTA-TDMA-PROGRESS-20261029-004 - OTA-HAOFV-014：主机失败分类、CAP fail-closed 与失败即 ABORT

- 变更（`tools/ota/ota_send/ota_send.py`）：
  - 新增 `OtaTransferError(kind, detail)` 与六类失败：`capability_unavailable` / `open_rejected` / `open_timeout` / `block_rejected` / `block_not_consumed` / `link_lost`；`main()` 先于通用异常捕获并输出 `failure_kind=`。
  - 新增 `transfer_error()`：任何分类失败都先尽力发 `SYST:OTA:ABOR`、读回状态写入 `metadata["abort_status"]`，再抛出；abort 自身的传输错误被吞掉，不掩盖原失败。
  - 新增 `resolve_block_size()`：CAP 解析失败默认抛 `capability_unavailable`（fail-closed）；只有显式 `--allow-compat-block-size` 才退回 512 并记录 `capability_reported=false`。
  - BEGIN/包模式预等待/每块 DATA 写入与消费等待全部走分类包装（`wait_receiving`/`wait_consumed`/`write_block`），不再以裸超时结束。
- 测试（`tests/python/test_ota_send.py`）：新增 4 条——CAP 失败 fail-closed、显式覆盖退回 512、按 reported max 协商下降、链路已死时 `abort_session()` 返回空串不抛。
- 验证：`test_ota_send / test_ota_multi_update / test_ota_stream_send / test_ota_haofv_wiring` → 62 passed。
- 未覆盖（不满足退出判据）：单板与四板 OTA 的“既有验收窗口内完成或明确失败”需台架实测；`ota_multi_update` 目前不向子进程透传 `--allow-compat-block-size`（CAP 失败会让四板任务明确失败，这是有意 fail-closed，若台架 CAP 偶发需单独评估）。

### OTA-TDMA-PROGRESS-20261029-003 - OTA-HAOFV-011：程序暂存窗口收敛到单页

- 变更（`third_party/portable_ota/src/pota_core.c`）：`pota_program_target()` 不再分配 `POTA_MAX_DATA_BLOCK_SIZE + POTA_MAX_FLASH_PAGE_SIZE` 的栈帧。整页直接从调用方块编程；仅尾部半页用单页窗口（`page_workspace[POTA_MAX_FLASH_PAGE_SIZE]`）拼 0xFF 填充；读回校验按页进行。新增可选 `pota_core_set_program_workspace()`（≥1 flash page），供有 RAM 余量的宿主使用；无 workspace 时走单页栈窗。
- 变更（`application/src/app_tasks.c`）：`APP_OTA_TASK_STACK_WORDS` 从“基线 + 协商块增量”改为“基线 + inline 事件载荷”，1921 → 1152 words。
- 测试（`tests/unit/test_portable_ota_core.c`）：尾部填充用例改为页拆分语义（最后一次 program 为一页、偏移 `+MOCK_PAGE_SIZE`），flash 内容与 0xFF 填充断言保持不变；`tests/python/test_ota_haofv_wiring.py` 新增两条静态断言（无块大小栈帧、栈公式不再含块增量）。
- 验证：`run_portable_ota_tests.ps1`（stream session/ingress/checkpoint/BCB/portable_ota）全部通过；`test_ota_haofv_wiring.py` 7 passed；`cmake_build_auto` 通过（build_id `20261029153647`、包 2196968 B / crc32 `0x370DB6B9`）。
- **RAM 上限实测（重要）**：把 workspace 做成 512 B 静态数组时链接报 `region RAM overflowed by 408 bytes`——当前工程 RAM 几乎无余量。`FreeRTOSConfig.h` 明确“堆缩减必须由实测 task/heap 水位支撑”，故本轮**不缩减 106 KiB 堆**，也不保留静态 workspace；块大小栈帧的移除仍是净收益（栈 −3840 B），但没有新增 BSS。
- 未覆盖（不满足退出判据）：`SYSTem:RTOS:STATus?` 的 `ota` free words 门限需 HIL 实测；峰值栈使用尚未在硬件上量测，“按实测水位重算”目前是保守公式 + 水位待补。

### OTA-TDMA-PROGRESS-20261029-002 - OTA-HAOFV-012：RECEIVING/ERASE_SLOT 无进展超时

- 变更（新增 `components/ota_manager/{inc,src}/ota_progress_watch.*`）：纯判定单元，输入 `(now_ms, state, received_size, programmed_size, timeout)`，只在 `RECEIVING`/`ERASE_SLOT` 计时；任一字段变化即重置；比较用无符号差值，跨 32-bit uptime 回绕安全；`timeout == 0` 显式禁用。
- 接入（`components/ota_manager/src/ota_ao.c`）：`ota_ao_service()` 在任何路由之前推进 watch，超时调用 `ota_ao_fail_no_progress()`——**先** abort 当前 stream/legacy 会话，**再**置 `FAILED` + `OTA_ERR_NO_PROGRESS` 并释放 OTA admission，保证下一次 `BEGIN` 看到干净会话。
- 错误面：`ota_error.h` 新增 `OTA_ERR_NO_PROGRESS`；`portable_ota_core_port.c` 的 product error 文本表补 `"NO_PROGRESS"`（`SYSTem:OTA:STATus?` 可直接读回）。
- 构建接线：`CMakeLists.txt` 新增 `ota_progress_watch.c`；`tools/tests/run_host_unit_tests.ps1` 注册 `run_ota_progress_watch_tests.ps1`。
- 验证：`run_ota_progress_watch_tests.ps1` 通过（超时/进度重置/erase 进度/回绕/禁用/NULL）；`run_ota_journal_tests.ps1`、`run_portable_ota_tests.ps1` 均通过；`cmake_build_auto` 通过（build_id `20261029152303`、三 profile `flash_link_contract=OK`、包 2196840 B / crc32 `0xBEEB4E3B`）。
- 未覆盖（不满足退出判据）：HIL 需证明“主机中断 → 超时 `FAILED/NO_PROGRESS` → admission 释放 → 下一次 `BEGIN` 被接受”，以及 stream 会话超时后 `portable_ota_port_stream_is_active()` 归零。host 只覆盖判定函数与收尾顺序的静态结构。

### OTA-TDMA-PROGRESS-20261029-001 - OTA-HAOFV-013：DATA 拒绝路径改为先消费任意块

- 变更（`middleware/scpi_port/src/scpi_ota_commands.c`）：`scpi_cmd_ota_data()` 把 `SCPI_ParamArbitraryBlock` 提到 RUN/stream 判定之前；拒绝时块已消费，下一条查询不再读到 payload 字节。两个 gate 的拒绝语义不变（只是顺序后移）。
- 回归：`tests/python/test_ota_haofv_wiring.py` 新增 `test_ota_data_consumes_block_before_rejections`（静态顺序断言：parse 必须先于 RUN gate 与 stream gate）。
- 验证：`pytest tests/python/test_ota_send.py test_ota_multi_update.py test_ota_stream_send.py test_ota_haofv_wiring.py` → 56 passed；`cmake_build_auto` 通过（build_id `20261029150439`、三 profile `flash_link_contract=OK`、包 2196520 B / crc32 `0x76EA18E3`）。
- 未覆盖（不满足退出判据）：要求“host 解析器用例证明拒绝一次 DATA 后下一条查询正确应答”。本轮用 libscpi 直接驱动真实 `scpi_cmd_ota_data()` 做了对照实验（编译 `tests/unit` 实验件，含 19 个链接桩）：
  - 生产同尺寸输入缓冲（`SCPI_PORT_INPUT_BUFFER_LENGTH` ≈ 块上限 + 256）下，**新旧两种顺序都通过**——libscpi 在整行已缓冲时会跳过命令未消费的参数，因此该症状在 host 上不可复现；
  - 把缓冲缩到小于块长时，两侧都失败（块放不进缓冲，与顺序无关）。
  - 结论：该缺陷是**潜在顺序缺陷**（grammar 正确性），不是可稳定复现的解析器失步。退出判据改为“静态顺序断言 + 台架负向”，host 行为级证明不成立；实验件已归档到 `out/archive/scpi-order-experiment-20261029/`（未留在 `tests/unit`，避免不可判别的测试冒充回归）。
- 未提交：firmware 类改动，需同源码指纹 P3/T2 凭证；本轮未跑台架。

### OTA-PROGRESS-20261012-001 - 四板刷写工作流（OTA / picotool / TDMA 流式预留）

- 新增 `tools/ota/OTA_workflow.py` 与 A4 报告 `docs/ota/OTA_FLOW.html`，形态与 DPLL60/T2
  工作流一致（`--list`/`--step`/`--steps`/`--full`/`--interactive`/`--refresh-html`）。
  8 步：构建、产物校验、刷写前快照（身份/构建号/标定持久化）、方式前置、刷写、刷写后复验、
  标定保持校验、收据。只做编排：传输复用 `ota_multi_update` 与 `picotool_flash`，OTA 门限
  复用 `p3_hardware_acceptance.validate_ota`。
- `--method ota`（默认，4096 B 块、保留标定）；`--method picotool` 加载 FACTORY UF2，须显式
  `--allow-factory-image`；`--method tdma` 为 TDMA 环上流式 OTA 的预留位，Step 3/4 明确判为
  失败且不刷写，不会被误读为成功。
- 标定保持：Step 6 比对 `READ:CALibration:TRAINing:STAGe:PERSistence?` 的记录身份，
  变化即失败。构建号 `PROJECT_BUILD_ID` 为 CMake 缓存值，Step 2 对「刷写前已是同号」给出
  非阻断备注，报告以包 SHA256 辨识镜像。
- 台架首次实跑（2026-10-12，快照非事实源）：`--method ota --full` 8 步全部通过（Step 2 为
  上述备注），四板重枚举、OTA 门限通过、标定未变；证据 `out/HardwareAcceptance/20261012/ota-*`。
  picotool 与 tdma 两条路径只做了离线防护验证，未在台架实刷。

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
