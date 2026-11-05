# TDMA 流式 OTA 里程碑计划（M5）

Status: Draft
Domain: OTA
Canonical: `docs/ota/OTA_TDMA_STREAM_PLAN.md`
Related: `docs/ota/OTA_V2STREAM_PLAYBOOK.md`, `docs/ota/OTA_TODO.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-29

## TDMA 流式 OTA 里程碑（M5，2026-10-29 重新规划：决策门 + 台架夜次）

### 0. 目标与非目标

**目标**：单一上位机串口下，协调者板读取 package，经 `TDMA_PAYLOAD_CLASS_OTA_BULK` / `TDMA_TRAFFIC_RELIABLE_BULK` 把镜像流式装进从板 inactive slot；A/B 安装、pending/confirm/rollback 仍由既有 `pota_stream_session` + `portable_ota_port` 负责。

**非目标**：`ARCH-OTASTREAM-01` 的其余本地 transport（USBTMC/UART/RS485/SD）；Recovery 分发；产线量产门禁；与实时流量共存。

### 1. 前提（用户 2026-10-29 裁定，不可协商）

1. **OTA 是静态动作**：升级期间只启动 TDMA 环，不启动 DPLL/VDC，不做测量/T2/校准；结束重启后按正常流程恢复并复测 P3/T2。
2. **owner 不变**：`TdmaSchedulerAO` 独占 gate/window/adapter，`OtaAO` 独占 session/distribution，`FlashTransactionAO` 独占 durable write。
3. **C1 不变**：erase/program 只由 core0 发起、core1 park ACK 有效；core1 侧不得触碰 flash API。
4. **先 2 板再四板**：四板、单口与契约都在 G1/G2 通过后才做。

### 2. 现状账（2026-10-29 只读核对，快照，非事实源）

| 面 | 现状 | 结论 |
|---|---|---|
| session / A-B | `pota_stream_session` / `pota_stream_ingress` 与 transport 无关；source 仅作准入/诊断；durable offset、checkpoint/journal、pending/confirm/rollback 齐备 | 可直接复用 |
| 接收入口 | `portable_ota_port_stream_write(source, offset, data, size, has_crc32, crc32)` 是唯一入口，`source_mask` 覆盖全部 source | 加一个 TDMA 源即可 |
| 环可靠通道 | `tdma_maintenance_runtime` 已有分片、按 target/required mask 的 ACK、超时/重试/重复计数（现用于 REFMEM 命令分片） | v1 载体；每帧载荷很小 |
| TDMA 服务 | `tdma_component_core1_service()` 在 `ota_ao_is_active()` 时整段返回 | 必须改为“静态态只服务维护流” |
| OTA bulk 生产/消费 | 无 `TdmaOtaIngress`、无 producer、无 distribution；`TDMA_PAYLOAD_CLASS_OTA_BULK` 仅有类与常量预留 | 本里程碑主体 |
| host 工具 | `--method tdma` 是预留位，`method_reserved()` 在 Step 3/4 直接失败 | 需实现 |
| 契约 | `ARCH-OTASTREAM-01` = `pending` | 最后收口 |

### 3. 形态结论（计划为什么这样切）

flash program/erase 期间 core1 必须 park（`drv_flash_lockout_core1_poll()` 进入 spin），core1 上的 TDMA 在写窗口内必然停摆。因此：

- 传输 = **分块 + 容忍停车**：收块入 RAM → park 写 → 解锁 → 回 durable ACK → 下一块；
- **stop-and-wait 是 v1 的正确选择**（写 flash 时间主导）；credit/窗口只在吞吐不足时启用；
- 静态态下没有实时流量要保护，②门退化为“升级期间确实没起 DPLL/VDC”（可 SCPI 读回）；
- 既有 maintenance stream 可直接承载 v1 的块，吞吐受“每帧载荷很小 + stop-and-wait”限制，只作可行性证明；v2 再评估 long frame（`TDMA_PROFILE_DEFAULT_LONG_CAPACITY`）与窗口化 ACK。

### 4. 三个决策门（每门可独立失败）

| 门 | 要回答的问题 | 通过判据 | 失败处理 |
|---|---|---|---|
| **G1 可行性** | 停车窗口内环协议是否仍成立？2 板能否装完？ | park 时长与 `schedule_miss` 增量有量化；2 板装好 inactive slot 到 `READY_TO_REBOOT`；环不损坏 | 反复重同步/数据损坏 → 判**不可行**，契约保持 `pending` |
| **G2 可重复** | 连续多次、断链/掉电/回滚是否可恢复？ | 连续 N 次通过；drop/duplicate/reset/超时注入后能续传或明确 abort；回滚可复现 | 收窄到已知可恢复集，未过项不得进 G3 |
| **G3 产品形态** | 单口四板、reference 迁移、契约 | 四板滚动成功、每节点可观测、同指纹凭证、C11 复核 | 停在 2 板形态，不宣称“单口可升级” |

### 5. 夜次编排（每晚只推进一个门的一步；白天写代码与评审）

| 夜次 | 目标 | 前置 | 判据 | 失败分支 |
|---|---|---|---|---|
| **N1 探针** | 不改产品代码：1–2 次现有 OTA，采集 park/miss/吞吐 | 离场前置全绿 + 恢复演练完成 | 三个数齐全且可复算 | 数值离谱 → 先改设计，不进 N2 |
| **N2 G1** | 2 板最小闭环：降级服务 + codec + ingress + stop-and-wait | N1 可行；host 用例绿 | 2 板 `READY_TO_REBOOT`，环不坏 | 失败按签名表处置，不硬刷 |
| **N3 G2** | 重复性与故障注入 + 回滚 | N2 通过 | 连续多次 + 恢复矩阵 | 未过项收窄，不进 G3 |
| **N4 G3** | 单口四板 + reference 迁移 | N3 通过、四板在位 | 四板滚动 + 证据包 | 停在 2 板形态 |

### 6. 任务表

| ID | 门 | 内容 | 依赖 | 退出判据 |
|---|---|---|---|---|
| OTA-TDMA-000 | 前置 | 关闭 `OTA-HAOFV-010`–`014`（011 栈隔离、012 无进展超时、013 DATA 拒绝路径消费、014 host 等待预算） | — | 010 收口、011–014 DONE |
| OTA-TDMA-001 | G1 | 设计说明：静态态 TDMA/flash 时序、park 边界、失败与恢复、C1/C2 逐条判定（不落代码） | 000 | 设计说明 + 用户审核记录 |
| OTA-TDMA-002 | G1 | `tdma_component_core1_service()` 改为“静态态只服务维护流”（VDC/RefMem/T2 仍按既有门退出） | 001 | host 静态断言；2 板环保持 running |
| OTA-TDMA-003 | G1 | 探针与环协议验证：park 时长、`schedule_miss` 增量、帧完整性（`SYSTem:PROTection:STATus?` / `TDMA:RING:STATus?`） | 002 | 数据齐全 + 环不损坏 |
| OTA-TDMA-004 | G1 | 四帧 codec：`OPEN/DATA/ACK/CLOSE`（golden + overflow + 未知 flags fail closed） | 002 | host roundtrip + fuzz |
| OTA-TDMA-005 | G1 | `pota_stream_ingress_source_t` 加 TDMA 源；`TdmaOtaIngress` 只调 `portable_ota_port_stream_write` | 004 | host 契约；不复制 session |
| OTA-TDMA-006a | G1 | durable ACK + stop-and-wait（ACK 在 program + readback 后回） | 005 | 2 板装 slot 到 `READY_TO_REBOOT` |
| OTA-TDMA-013 | G1 | 夜间执行器：状态机 + 超时 + 重试预算 + resume + `attempt-XX/` 证据包 + `--recover` | 000 | host 覆盖四类恢复；到点安全收尾 |
| OTA-TDMA-014 | G1 | host `--method tdma` 最小实现（单口 → 协调者 → 环，不并行） | 005 | 与 2 板 HIL 同批跑通 |
| OTA-TDMA-007 | G2 | resume：journal + readback 重建 durable offset | 006a | 两板 reset / 掉电重放 |
| OTA-TDMA-009 | G2 | 两板全链：装 slot → boot/confirm/revert → 重入环 | 007 | 同指纹凭证 + 两板摘要 + 升级后 P3/T2 恢复 |
| OTA-TDMA-015 | G2 | 重复性 soak + 注入矩阵（drop/duplicate/超时/park） | 007 | 连续 N 次通过；失败可分类 |
| OTA-TDMA-006b | G3 | credit/背压（仅当 G1/G2 吞吐不足时启用） | 006a | 零信用 / gate closed 用例 |
| OTA-TDMA-008 | G3 | `OtaDistributionFB`：per-node 向量、stage-all→verify→cohort commit、reference 先迁移 | 006b 或 006a | host 状态机 + 三/四板负测 |
| OTA-TDMA-010 | G3 | 四板滚动：每节点可观测、失败隔离、掉线重加入 | 009、008 | 四板 HIL + 每节点证据 |
| OTA-TDMA-011 | G3 | 单口产品形态：协调者单口发起、四板全部升级（REFMEM 单口收敛后的唯一通道） | 010 | 机器捕获（`port` + 四板 build 回读）+ 失败路径留证 |
| OTA-TDMA-012 | G3 | 契约收口：`ARCH-OTASTREAM-01` 转 `active` | 011 | C11 独立复核；六类 transport 证据边界写清 |

### 7. 夜间执行规则（配 `OTA-TDMA-013`）

**离场前置（任一不满足不启动）**：台架锁（确认无其它 T2/P3/OTA 在跑）→ 四板 STOP、VDC/DPLL 未启动、环 STOP、`ABORt` 清会话 → package sha256/build 写 `preflight.json`（夜间不重构建）→ host 用例绿 + 执行器 dry-run → 端口按 UID/VID-PID 绑定 → 证据目录与日志上限 → 恢复演练完成。

**运行循环**：一轮 = 一次可判定尝试，必须有 PASS/FAIL(reason) 与独立目录；每轮先读状态再决定；`--wall-clock` 到点即收尾（写 `final.json`、全 STOP、释放锁）；同一时刻只允许一块板写 Flash。

**失败签名 → 处置**：

| # | 签名 | 处置（有界） | 立即停 |
|---|---|---|---|
| 1 | `BEGIN` 无应答 | 重开端口 + `ABORt`，重试 1 次 | 连续 2 次 |
| 2 | `FAILED,1,"INVALID_STATE",4` | `ABORt` → `BEGIN`，重试 1 次 | `ABORt` 失败 |
| 3 | 中段停滞（块未消费） | `ABORt` → 按 durable offset 续传；无 resume 则重开 | 同一 offset 两次 |
| 4 | 维护帧 ACK 超时 | 退避重发；用尽 `TDMA_MAINTENANCE_MAX_ATTEMPTS` 判失败 | 同一帧 3 轮 |
| 5 | 环掉线/节点缺失 | 重读；必要时 `ARM→TRAIN→START`（≤2 次） | 两次失败 → 隔离该板 |
| 6 | 重启后 build 未变/回滚 | 只读 boot result 与槽位，不重刷 | 保留现场并停 |
| 7 | 任意命令无响应 | 恢复梯子（见下） | USB 不可见 |
| 8 | 串口消失/重枚举 | 按 UID 重扫，限时 | 超时 |
| 9 | 执行器崩溃 | supervisor 拉起 + resume，同阶段 ≤2 次 | 第三次 |
| 10 | 同错在等待/降温后重复 | 记录并停 | 立即 |
| 11 | 标定 generation 变化 | 立即停并标注 | 立即 |
| 12 | 证据写入失败 | 立即停 | 立即 |

**恢复梯子（picotool，允许但有界）**：台架已验证可强制进 BOOTSEL（`out/picotool_reset_a.json`）。顺序：串口可答 → `SYSTem:OTA:ABORt`；串口不答但 USB 在 → `picotool reboot -f -u`；已在 BOOTSEL → `picotool load` 同 build **app UF2**；USB 不可见 → 停（物理 BOOTSEL 属人工）。四条红线：① 只刷 app UF2，禁止 factory 镜像与 `--full-erase`（丢标定）；② 必须同被测 build；③ 每次写 `recovery.json`（原因/命令/前后 build/标定 generation/槽位/BCB/耗时），恢复不计 PASS、不掩盖原失败；④ 每板每晚 ≤2 次，第 3 次或与 preflight 不符即停。

**夜间硬禁止**：factory 镜像与 `--full-erase`；`--full-bench`（TRN-03 预算必失败）；夜间改产品代码后继续刷写；靠放大 timeout 或改终态判断“让测试通过”（`EXE-OTA-01`）；并行刷两块板；跳过 STOP 与恢复。

**晨间判读**：只回答三件事——park 时长分布、重试是否集中在 erase 窗口、吞吐是否随轮次退化；结论只允许“可行 / 需调参（写参数与依据）/ 不可行（写判据）”；`attempt-XX/` 与 `final.json` 以紧凑条目进 `OTA_TASK_PROGRESS.md`。

### 8. 硬门禁与不可行判据

**硬门禁**（任一不成立不得进入下一阶段，不得降级为 ⚠️）：

1. **C1**：erase/program 只由 core0 `FlashTransactionAO` 发起，core1 park ACK 有效，临界区内无 XIP/USB/SD/LCD；core1 调 flash API 判失败。
2. **C2**：不改 raw tick 回写与 `int32_t` 回绕安全语义。
3. **②**：升级期间只启动 TDMA（SCPI 读回为证），不得在测量/实时负载运行中发起 OTA；结束后按正常流程恢复并复测。席位/相位改动仍须工具核算 + `EXE-SEAT-01`。
4. **③**：三 owner 边界不变（见 §1 第 2 条）。
5. **④**：新失败路径有 reason/计数/快照，F0–F3 成立；gate closed 时保持 session，不形成重试风暴。
6. **⑤**：wire 版本/layout/DeploymentGate CRC/错误码段同步；契约未转 `active` 前不得出现“已支持流式 OTA”。
7. **⑥**：每阶段凭证绑同源码指纹；旧 build 与 scope 不覆盖的凭证不得充当本轮证据。

**不可行判据（fail-closed）**：若 G1 证明 park 窗口长到环无法维持协议（反复重同步或数据损坏），或 codec 无法在既有 payload class 与 `REFMEM-260B-01` 容量约束内成立，则判不可行，契约保持 `pending`，不降级验收、不以逐板接串口冒充“单口支持”。

### 9. 影响面

REFMEM 统一控制面按单串口收敛后，从板不再有独立升级串口；本里程碑是该形态下四板升级的唯一通道。未落地前，四板升级必须逐板接串口（`ota_multi_update`）或用 picotool（丢已存标定），不得写成“单口可升级”。
