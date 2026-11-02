# OTA HAOFV 任务进度

Status: Active
Domain: OTA
Canonical: `docs/ota/OTA_TASK_PROGRESS.md`
Related: `docs/ota/OTA_HAOFV_ARCHITECTURE.md`, `docs/ota/OTA_TODO.md`, `docs/arch/HAOFV_FLASH_TASK_PROGRESS.md`
Last updated: 2026-11-02

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

### OTA-TDMA-PROGRESS-20261102-150 - 构建级二分：关掉 DPLL step 后仍 49–66k ⇒ 单点优化不足以回到 39k

- 临时 `if (0)` 关掉 `sync_dpll_fb_step()`（仅测量，已完整回退，该文件无 diff），重建 build `20261102051426`，
  同窗口测 DPLL 相位：max_rt 从 103–110k 降到 **49.2k(COM5)/61.5k(COM4)/66.2k(COM3)/63.5k(COM6)**，
  overrun 从 2915–4170 降到 **33(COM5)/581–631(其余)**。
- ⇒ step 占 ~45–50k，但**其余服务本身就有 49–66k**（跟随者更重，已超 39k 声明 WCET）；
  单靠削热点无法把相位压回 39k。
- 结论：需要调整 DPLL 服务节奏（隔周期执行等）或按 EXE-SEAT-01 重排静态窗口，属设计/合同变更。
- 台架：已回退并重刷清洁 build `20261102052052`，四板统一、IDLE、环停。
- 注意：工作树现 **42 条**（>§2.11 的 40），其中 `docs/validation/P3_FLOW.html` 为工作流生成的文档产物。

### OTA-TDMA-PROGRESS-20261102-149 - 坐实：DPLL 预算上涨与流式 OTA 改动无因果（09-17 的 VDC/DPLL 功能提交抬升）

- 时间线：09-07 42k → 09-15 39k → **09-17 117k 跳变** → 09-18..09-21 169k/174k/218k → 10 月稳定 100–133k → 11-02 104–111k。
  本会话固件改动最早 10-31 晚，而 10-31 早上的三次运行已是 99–125k ⇒ 上涨早于改动约 6 周。
- `git log -S` 归属：`vdc_boundary_service_core1`(dd8cba15,09-16)、`priority_summary_service(true)`(08af197f,09-18)、
  `reference_discipline_service_core1`(024915dc,09-19)、`priority_guard_service_core1`(37ca2351,09-19)；合同仍 39k 未同步。
- 机制：DPLL 验收期间无 OTA 会话，`ota_ao_is_active()` 为假；我们改动的路径全部被 OTA/flash 事务门控；
  唯一无条件新增代码是 `drv_flash_core1_lockout_poll()`（原子标志检查，数十周期），与 60k 差三个数量级。
- 新增残余：R4 COM4 USB 不稳定（块超时 / ClearCommError → 掉 RECOVERY，需工厂刷写恢复）；
  R5 `--diagnostic-continue` 不能放行 required objective（OTA），台架抖动直接阻断验收。
- 台架：COM4 已恢复并与其余三板统一到 `20261102045137`。

### OTA-TDMA-PROGRESS-20261102-148 - 判别结果：捕获不是原因；DPLL 服务本身 40–110k 周期 vs 声明 39k

- 先修台架卡点：recorder 残留 `state=5(FROZEN)`；`app_record_save()` 要求环已停，停机后四板补 SAVE → `state=8`。
- `--clock-evidence disabled` 复测（build `20261102034203`）：DPLL max 105,196–110,612、overrun 3,522–4,983、
  last_rt 39,273–62,948、deadline 3,437–3,516 ⇒ **捕获假设证伪**。
- 结论：DPLL 相位典型 40–65k、峰值 ~110k，声明 WCET 39,000 差 1.6–2.8 倍（门②合同与现实不符）。
- 下一轮：路线 A 调合同（需你按 EXE-SEAT-01 审核）/ 路线 B 优化子服务（需分段计时）。
- 台架：四板 build `20261102034203`、IDLE、环停、recorder SAVED。

### OTA-TDMA-PROGRESS-20261102-147 - 归因：DPLL 相位实测 max 109,715 周期 vs 声明 WCET 39,000（唯一超预算相位）

- 板端 `SYSTem:TDMA:SCHEDule?`（COM5，周期合同 375,000）：TDMA last 50,016/max 368,469；VDC last 1,303/max 12,658；
  **DPLL last 29,046 / max 109,715 / overrun 1,131,597**（WCET 39,000）；CAL 0；SYNC_CAP max 19,065；REFMEM max 18,762。
- ⇒ DPLL 是唯一自身超预算的相位；VDC/REFMEM 自己 overrun=0，其 miss 由前序相位拖延造成。
- 工具缺口：`trn03_closed_loop.py::validate_realtime_phase()` 收下 `allow_capture_disturbance` 却**从未使用**（死参数）。
- 下一轮判别：同窗口 `--clock-evidence disabled` 复测 DPLL，判定 110k 是捕获仪表成本还是 DPLL 服务成本。
- 台架：四板 build `20261102034203`、IDLE、环停。

### OTA-TDMA-PROGRESS-20261102-146 - 受管工作流 0–23 步全过；第 24 步 DPLL 调度门失败（非本次回归）

- `hardware_acceptance_workflow.py --profile full --preset pico2-v2-factory-candidate --full-bench --method ota`：
  步 0–23 OK，步 24「TDMA process-image/FIFO 闭环」FAIL（`realtime_gate_passed=false`、`closed_loop_passed=false`），
  步 25–36 未跑、凭证未落盘。
- 失败判据：`dpll_schedule_gate` 四节点 vdc `deadline_miss_count_grew`（1127 次）、refmem（48 次）。
- 对比 2026-10-31 五次 + 2026-11-02 两次运行：`dpll_schedule_gate` 全为 False（vdc miss 2683–7994），
  ⇒ **非本次改动回归**；现有 receipt 一直是 TDMA-only（SKIPPED_TDMA_ONLY）。
- 结论：§2.11 的 FULL 凭证卡在 VDC/REFMEM 实时 deadline（门②），需单独做实时预算归因。
- 台架：四板 build `20261102034203`、IDLE、环停。

### OTA-TDMA-PROGRESS-20261102-145 - 硬件验收 36/37 步通过；仅 DPLL 联合观测因示波器卡死失败

- COM4 的 OTA 失败**不是** journal 缺陷：全片擦除后令牌为 0；真因是单块耗时 >60 s。把
  `p3_hardware_acceptance.py` 两处 `--timeout 60` 提到 180 s 后 COM4 单独 OTA 成功（durable=1141000）。
- 四板 OTA 第二次因 USB 串口瞬时错误挂 COM3/COM6，重跑后 4/4 PASS；COM3/COM6 commit 后落进 RECOVERY，
  已用工厂镜像重刷恢复（build `20261102023509`、BOARD:NO 3/4 保持）。
- `resume`：P0T/粗校/coded marker/Latency Cal/TRN-00–03/四节点 TDMA 全过，provisional receipt 已写；
  **DPLL 60 s 联合观测失败**（两次，`scope 12/12 windows missed`）。
- 示波器 HDO4A244301137：CHAN1 有 7.23 Vpp/1 kHz 信号、触发设置正常，但 `:SINGLE`/`:RUN`/`:TRIG:FORC`
  后 `:TRIG:STAT?` 恒为 WAIT、测量查询超时 ⇒ 仪器侧卡死，需**断电重启示波器**。
- 台架：四板 build `20261102023509`、IDLE、环停；工作树 40 条（§2.11 上限）。

### OTA-TDMA-PROGRESS-20261102-144 - 硬件验收卡在 OTA 四板：COM4 失败即 D1 的 journal 同身份冲突（令牌 9113）

- `p3_hardware_acceptance.py run --full --preset pico2-v2-factory-candidate`：build `20261102011021` ✅，
  OTA 四板 ❌（COM3/COM5/COM6 PASS，COM4 FAIL，`failed_count=1`）。
- COM4 细节：`ota_stream_send → FAIL stream DATA rejected at 512: '<timeout>'`；事后 `SYST:OTA:PROG?`
  第 4 字段 = **9113**（owner_state=11、journal reason=3），与 relay 路径同因。
- 已试：用同指纹工厂镜像重刷 COM4 后重跑，仍同位置失败 ⇒ 非陈旧 journal，而是请求重试撞上既有记录身份。
- 结论：P3 的 OTA 步骤与 D1 同一缺陷；D1 修好前验收无法通过、积压无法按 §2.11 提交。
- 台架：四板 build `20261102011021`、IDLE、环停。

### OTA-TDMA-PROGRESS-20261101-143 - 日间收口（round 24/24）：D1 未收口、D4–D6 未开始；工作树 40 条触发 §2.11 冻结

- 今日结论：环不破坏 flash 事务；relay 倒在事务收尾的终态 journal 追加；收尾时曾未 park（已修）；
  现行阻塞是 journal 的“同身份不同载荷”fail-closed（reason=3），把 generation 纳入 identity 无效（疑恒 0）。
- 工作树 **40 条**（§2.11 上限）⇒ 必须先做 `--profile full` 验收并单批提交，才能继续加改动。
- 明日顺序：解冻结（P3）→ D1 收口（一次性发布闸门/确认 generation）→ D2/D3 → D4 → D5 → D6。
- 台架：四板 `IDLE`、环停、统一 build `20261101064459`，BOARD:NO 1..4。

### OTA-TDMA-PROGRESS-20261101-142 - 身份加 generation 仍冲突（疑 generation 恒 0）；记录缓存已撤回、堆恢复 −576

- 按 §9.31 把 `transaction_generation` 纳入 durable identity，复测仍 9113（reason=3）⇒ 冲突双方 generation 相同。
- 最可能 generation=0；也可能同一事件被 append 两次且两次载荷不同。
- 已撤回无效的记录缓存（省 RAM），FreeRTOS 堆恢复为 97 KiB−576。
- 下一轮：先确认 generation 是否恒 0，或改用“终态事件只发布一次”的闸门（不放宽 fail-closed）。
- 台架：四板 build `20261101064459`、IDLE、环停；工作树 37 条（<40），代码未提交（待 D5 P3）。

### OTA-TDMA-PROGRESS-20261101-141 - 冲突真因：journal 的 durable identity 跨事务碰撞（request_fingerprint 短路了 job/generation）

- 加“按 event 复用首次记录字节”缓存后仍 9113（reason=3）⇒ 冲突不在同一事务内。
- 读 `flash_transaction_journal_identity_equal()`：当 `request_fingerprint != 0` 时身份只比
  `request_fingerprint + store_generation + event`，**不比 job_id/transaction_generation/provider_generation**。
  同一块同一操作的再次请求指纹相同 ⇒ 新事务与旧事务同身份，载荷有差异即 fail-closed。
- 直连只做一次、relay 会重试 ⇒ 第二次撞上第一次的完成记录。
- 下一轮修法：durable identity 纳入 `transaction_generation`（或让重试的 fingerprint 带 job id）；
  保持 fail-closed 不变。
- 台架：四板 build `20261101063444`、IDLE、环停；工作树 37 条（<40），代码未提交（待 D5 P3）。

### OTA-TDMA-PROGRESS-20261101-140 - D1 定论：终态 journal “同身份不同载荷”被拒（reason=3）；parked 修复已生效

- 补错误类编码后复测：令牌 **9113** ⇒ owner_state=11（occupied=1、parked=1）、reason=3（同身份不同载荷）。
- 说明：重新 park 的修复已生效；当前失败是 journal 幂等检查——同一 (transaction,event) 已存在记录但载荷不同，
  按设计 fail-closed。直连能过 ⇒ relay 下该终态记录被发布了不止一次（两次之间易变字段变了）。
- 修法：终态记录只在首次生成，重入时复用首次字节（冻结时间戳等）或跳过重复发布；不放宽冲突检查。
- 台架：四板 build `20261101062325`、IDLE、环停；工作树 37 条（<40），代码未提交（待 D5 P3）。

### OTA-TDMA-PROGRESS-20261101-139 - 证实 occupied=1/parked=0 并修复（终态 append 前重新 park）；失败类随之改变

- 令牌 `9000+owner*10+reason` 实测 **9105** ⇒ occupied=1、parked=0、reason=5 ⇒ 终态 journal 追加确实发生在
  “已解除 park”的窗口，嵌套 journal 路径被拒。
- 修复（`flash_transaction_fb.c` RELEASE）：发布终态记录前若未 park 则重新 `park_core1()`（既有 C1 机制），
  发布后按原逻辑释放。
- 复测 build `20261101061327`：令牌变 **7000**（`6000+100*state`，state=10 FAILED）⇒ 不再是 COMPLETION，
  slot-program 失败被消掉；同一阶段出现另一错误类，需在 6000 分支编码错误类（下轮）。
- 台架：四板 build `20261101061327`、IDLE、环停；工作树 37 条（<40），代码未提交（待 D5 P3）。

### OTA-TDMA-PROGRESS-20261101-138 - 具体原因：终态 journal 的 slot program 失败（state=FAILED, reason=5）

- journal append 失败分支编号入令牌（`7000+100*state+reason`），环跑 relay 得 **8005** ⇒ state=10（FAILED）、
  **reason=5（slot program failed）**。
- 链：`completion_append` → `journal_append` → slot program 失败 → COMPLETION → 终态 FAILED。
- 性质：该 append 只能在“事务仍持锁 + core1 已 park”时走嵌套 journal 路径；直连同一事务可成功
  ⇒ relay 侧这次 append 很可能发生在所有者已释放/已解 park 之后。
- 下一轮：append 失败时记录 `occupied`/`core1_parked`（令牌 `9000+occupied*10+parked`），确认后把终态
  append 挪回持锁窗口内（不得放宽准入条件，C1）。
- 台架：四板 build `20261101055327`、IDLE、环停；工作树 37 条（<40），代码未提交（待 D5 P3）。

### OTA-TDMA-PROGRESS-20261101-137 - 阶段定位：RELEASE 的“终态 journal 追加”失败（state=FAILED, error=COMPLETION）

- 令牌改按状态编码（`6000+100*state+site`），环跑 relay 读到 **7000** ⇒ state=10（FAILED）、site=0、error=22。
- 代码核对：`flash_transaction_fb.c` 的 RELEASE 分支里 `completion_append()` 失败会直接写
  `last_error=COMPLETION`（无站点标记，故 site=0）⇒ **擦写/校验通过，倒在终态 journal 追加**。
- 与 D2 早期 journal 假设一致，但这次是阶段证据；下一轮给该分支加站点，并把 append 失败原因
  （槽满/擦除/编程/CRC）透出为独立令牌，对比 relay 与直连两侧。
- 台架：四板 build `20261101054342`、IDLE、环停；工作树 37 条（<40），代码未提交（待 D5 P3）。

### OTA-TDMA-PROGRESS-20261101-136 - submit 拒绝令牌=0（请求其实被接受）；下轮改用“按状态”令牌定位

- 给 `flash_transaction_fb_submit()` 加了四种拒绝原因令牌（4000+reason）后重测环跑 relay：仍 `2000`
  ⇒ `submit_reject == 0`，**提交被接受**，“准入被拒”的推断也不成立。
- 现存确定事实：`completion.error=22`、`completion_site=0`（未命中任何已标注的 COMPLETION 调用点）
  ⇒ 仍有一条 `flash_transaction_fail(context, <变量>)` 把 22 带进来，站点标注覆盖不到。
- 下一步：令牌改为 `5000 + 10*vector.state + 站点号`，直接报“事务死在哪个状态”（状态枚举即阶段划分），
  不再逐个标注调用点。
- 台架：四板 build `20261101052619`、IDLE、环停；工作树 37 条（<40），代码未提交（待 D5 P3）。

### OTA-TDMA-PROGRESS-20261101-135 - 令牌按终态判定后仍 2000：读的是陈旧 vector，真凶是 submit（准入）被拒

- build `20261101051614`：令牌逻辑改为“未达终态 → 3000+state；达终态 → 按错误类”，环跑 relay 仍得 `2000`。
- 组合（终态 + error 22 + 站点 0）只能说明：本次请求**根本没提交**（`flash_transaction_ao_submit()` 被拒后提前返回，
  不填 completion、不改 vector），端口读到的是上一条事务遗留状态。
- 即“erase 失败”链条上先失败的是 **flash 事务准入**，不是擦写本身。
- 下一步：给 submit 拒绝加确定令牌（`4000+准入原因`：policy/resource/mode/tdma_training_active/calibration_active），
  重测即可对症。
- 台架：四板 build `20261101051614`、IDLE、环停；工作树 37 条（<40），代码未提交（待 D5 P3）。

### OTA-TDMA-PROGRESS-20261101-134 - 自我纠错：失败令牌不可靠（陈旧 vector.last_error），D1/D2 的 COMPLETION 结论作废

- 事务级停环窗口已实现并实测生效（窗口内 `ring_tx` 冻结），但失败仍是 `FLASH_ERASE`。
- 给 7 处 COMPLETION 站点加序号（内容锚定）+ `completion` 零初始化后，令牌变 `2000`（site=0）：
  **没有站点被命中**却被报成 COMPLETION ⇒ 该错误来自 `flash_transaction_ao_execute` 循环里
  拷入的**陈旧 `vector.last_error`**；事务实际是“16 步内未达终态”。
- 因此 D1/D2 的“COMPLETION/journal 写”结论**作废**；事务级停环窗口也随之改成直通（未启用）。
- 下一步：令牌改为优先按 `vector.state` 判定（未达终态 → `3000+state`），重测后再定 park/停放路线。
- 台架：四板 build `20261101050557`、IDLE、环停；工作树 37 条（<40 闸门），代码未提交（待 D5 P3）。

### OTA-TDMA-PROGRESS-20261101-133 - D2 判别：环跑 + 直连正常（durable 512），失败是 relay 独有的 journal 写

- 对照（同 build `20261101031742`）：环跑 + 直连 OTA 到 `durable_offset=512` 持续运行，无 COMPLETION；
  环跑 + relay 则 `COMPLETION`（令牌 420）。
- 结合代码：7 处 COMPLETION 全在 `flash_transaction_completion_append()`（journal 写 PROGRAMMED/COMMITTED）
  路径；journal 的 program 发生在 core1 解除 park 之后，环在跑 ⇒ 该 flash 写失败 ⇒ COMPLETION。
  直连时环未跑，故能过。
- 决定（HAOOV 最小可回退）：停环窗口上移到**整个 flash 事务**（`flash_transaction_ao_execute()` 包裹），
  覆盖 erase/program/verify + journal 完成记录；恢复前须记录“原本是否开着”，避免误启环。
- 台架：四板 build `20261101031742`、IDLE、环停；代码未提交（待 D5 P3）。

### OTA-TDMA-PROGRESS-20261101-132 - D1 结果：擦除失败类是 COMPLETION（error 22），不是 PARK；停环钩子无机会触发

- 给 `SYST:OTA:PROG?` 追加第 4 个只读字段（`portable_ota_port_flash_stage()`），环跑 + relay 读到
  `512,2280616,0,420` ⇒ `200+10*22+0` ⇒ **`FLASH_TRANSACTION_ERROR_COMPLETION`**，vector.state=0。
- 含义：事务在擦除开始前就失败（state 仍 IDLE），失败点 `completion_retain() → lease->retain()`；
  与 park 无关 ⇒ 之前“环跑导致 park 超时”的推断被推翻（也解释了 ring_tx 从未冻结）。
- 线索：环跑时 OTA 会话周期性重启（§9.11 的 5.6 s 周期）⇒ journal 租约失效 ⇒ retain false。
- 计划调整：停环窗口降级为后续优化；新 D2 = 查“环跑为何让会话反复重启”。
- 台架：四板 build `20261101031742`、IDLE、环停；代码未提交（待 D5 P3）。

### OTA-TDMA-PROGRESS-20261101-131 - REFMEM 停环窗口首版：钩子已接、实测环未停（擦除仍失败）；日间里程碑 D1–D6 立项

- 实现：`flash_transaction_ao.c` park 失败 → 弱符号 `app_flash_park_pause_ring()`（application 侧调
  `distributed_refmem_tdma_ring_stop()`）→ 重试 park；会话结束 `app_flash_park_resume_ring()` 恢复环。
  `nm` 确认真符号覆盖（`T app_flash_park_pause_ring`）。
- 实测 build `20261101024938`（环跑 + relay 150 s）：仍 `FAILED/FLASH_ERASE`，`ring_tx` 27683→28906
  **持续增长** ⇒ 环从未停 ⇒ 钩子未被调用或 `ring_stop()` 在该上下文返回 false；擦除失败分支未定。
- 日间任务已写入 playbook §9.21（D1 定位失败分支 → D2 停环窗口生效 → D3 状态入 REFMEM →
  D4 M4 端到端 → D5 P3 → D6 契约收口），D1–D3 未过不进 D4。
- 台架：四板 build `20261101024938`、IDLE、环停；代码未提交（待 D5 P3 同批）。

### OTA-TDMA-PROGRESS-20261101-130 - 方案 (c) 证伪：park 预算放大 50× 仍 FLASH_ERASE（core1 根本不轮询）

- 诊断构建 `20261031204000` 把 `DRV_FLASH_LOCKOUT_DEFAULT_WAIT_LOOPS` 提到 50 000 000（≈500 ms），
  环跑 + relay 整包 120 s 复测：仍 512 B 后 `FLASH_ERASE`，环 `tx==rx/bad=0`。
- 结论：不是预算问题；环服务期间 core1 不执行 `drv_flash_core1_lockout_poll()`。预算已回退 1 000 000。
- 剩余选项仅 (a) 停环 / (b) core1 周期内让位；需人工选择后继续。M4 因此**阻塞**。
- 台架：四板 build `20261031204000`（诊断镜像，待刷回）、IDLE、环停。

### OTA-TDMA-PROGRESS-20261101-129 - M4 决策请求：core1 park 在环运行时拿不到（方案 a 停环 / b 抢占）

- 机制核对：core0 擦写前 `drv_flash_lockout_begin(budget)` 等 core1 park ack；core1 在
  `app_runtime.c:191-208` 每周期调 `drv_flash_core1_lockout_poll()`。环跑时 core1 周期被 TDMA 占满，
  ack 超出 core0 预算 ⇒ `PARK_TIMEOUT` ⇒ OTA `FLASH_ERASE`；环停时同路径正常（856 576 B/180 s）。
- 方案 (a)：OTA 擦写期间停环（复用既有 STOP/START），改动小、可回退；代价是环中断数秒。
- 方案 (b)：park 可抢占 TDMA 席位，不中断环；但动 core1 实时调度，需 EXE-SEAT-01 级评审。
- 建议先 (a) 打通 M4 端到端，(b) 单独立项。等待用户选择后继续；在此之前不再无人值守改 flash/park。
- 台架：四板 build `20261031202200`、IDLE、环停；代码未提交（待 M5 P3）。

### OTA-TDMA-PROGRESS-20261101-128 - M4 硬阻塞（C1）：环运行时 erase 直接 FLASH_ERASE，环停则正常

- 同构建 `20261031202200` 对照：环停+直连 4 KB 块 → 擦除成功、写入 856 576 B/180 s；
  环跑+relay 整包 → 512 B 后 `"FAILED",2,"FLASH_ERASE",4`（两次复现），环侧 `tx==rx/bad=0` 正常。
- 路径：`flash_transaction_park_core1()` → `drv_flash_write_session_begin()` 的 lockout/park 握手；
  park 失败最终表现为 OTA 的 `FLASH_ERASE`。
- 判定为 C1 相关问题：环运行时 core1 不释放 ⇒ 擦写拿不到 park ⇒ 硬失败（非“慢”）。
  TDMA 流式 OTA 的硬前提是“擦写期间让环让出 core1”（维护窗或可抢占 park），需按 C1/§6 重新评审。
- 建议：此点不宜无人值守继续改动，等人工确认方案（停环 vs 抢占 park）后再推进 M4。
- 台架：四板 build `20261031202200`、IDLE、环停；代码未提交（待 M5 P3）。

### OTA-TDMA-PROGRESS-20261101-127 - 吞吐标定：512 B 块 ≈0.5 KB/s，4 KB 块 ≈4.7 KB/s（擦除后 ≥6 KB/s）

- 同一构建 `20261031200639`、直连+切片，只改块大小：512 B → 41 472 B/120 s；4096 B → **856 576 B/180 s**。
- 结论：低速率来自“每块一次 ACK 往返”，不是 AO/flash 极限；P3 直连（4 KB 块）≈28 KB/s 同量级偏快。
- 对 M4：relay 仍按 512 B 块 ACK，线路 ~0.9 KB/s ⇒ 1.14 MB ≈20 min、2.28 MB ≈40 min。
  下一轮评估把 relay ACK 粒度提到 4 KB（需核算 RAM 与 ACK 语义），或直接排一次 40 min 长跑。
- 台架：四板 build `20261031200639`、IDLE、环停；代码未提交（待 M5 P3）。

### OTA-TDMA-PROGRESS-20261101-126 - 擦除卡死根因修复：maintenance_required 改会话作用域；写入速率 0.4 KB/s 成新瓶颈

- 根因：`ota_tdma_v2_bridge_app_init()` 无条件 `ota_ao_set_tdma_maintenance_required(true)`，
  使本机 USB 会话也把“维护流可用”当前置条件，环没跑时擦除永不返回。P3 Step 4 能过是因为跑在
  bridge 关闭的构建上。
- 修复（build `20261031200639`）：改为会话作用域——首个 bridged 帧置位、release 且 bridge 不活跃时清除。
- 实测（直连+切片，120 s 上限）：`durable_offset` 512 → 8704 → 16896 → 25088 → 33280 → 41472，
  擦除完成并持续写入；此前同条件 20 s 内超时且板端失联。
- 新瓶颈：写入 ≈0.4 KB/s（1.14 MB ≈47 min、2.28 MB ≈95 min），夜间单轮不可行 ⇒ 先提速率再谈 M4。
- 台架：四板 build `20261031200639`、IDLE、环停；代码未提交（待 M5 P3）。

### OTA-TDMA-PROGRESS-20261101-125 - M4 A/B：环停时擦除同样卡住（修正 park 假设）；疑为 maintenance_required 被全局置位

- 环停直连对照：切片包 `durable_offset=512` 成功，随后 `DATA 512` 超时（AO 停在擦除）；
  90 s 观察中目标 USB CDC 失去响应（串口打开阻塞 >120 s），数分钟后自行恢复 `IDLE`、`PROG?=0,0,0`（疑似看门狗复位）。
- 直连+整包立即 `<timeout>`（直连期望切片包）⇒ `BAD_HEADER` 是 relay 特有，relay 与直连的 package 口径不一致（新发现）。
- 修正：擦除卡死与环/park 无关（环停同样卡）。新假设：v2 bridge 构建在 init 里无条件
  `ota_ao_set_tdma_maintenance_required(true)`，使 AO 在环没跑时等不到维护窗口 ⇒ 擦除不结束；
  P3 Step 4 之前跑在 bridge 关闭的构建上。
- 下一步：该标志改为“bridge 会话活跃才置位”，并用 bridge OFF 构建做对照；同时单列 relay/直连 package 口径差异。
- 台架：四板 build `20261031193724`、IDLE、环停（COM4 已自愈）；代码未提交（待 M5 P3）。

### OTA-TDMA-PROGRESS-20261101-124 - M4 首测：环 tx==rx/bad=0 健康；阻塞转为“erase 在运行中的环上不结束”

- 整包 relay（300 s 计划）：环侧 `tx==rx` 同步增长、`bad=0` ✅；目标 8.6 s `ERASE_SLOT`
  （`received=512 / expected=2280616`）；34 s 时源侧停滞看门狗中止（`result=171, durable=0`）。
- relay 中止后目标**仍停在 `ERASE_SLOT`**（`PROG?=512,2280616,0`）⇒ 不是没开始，而是擦不完。
- 首要怀疑（C1 相关）：擦写需 core1 park ACK，环运行时 core1 被占；P3 Step 4 能过是因为环没跑。
  下一轮取 park/flash 现场并做“环停 vs 环跑”A/B，再决定 OTA 期间暂停环还是修 park 时序。
- 另：目标侧 ABORT 语义已定稿——`release()` 只发 ABORT 帧、不再本地 abort（避免覆盖 AO 真错误）；
  周期看门狗改为“发送一次、不 abort”，失败通知由源侧看门狗兜底。
- 台架：四板 build `20261031193724`、IDLE、环停；代码未提交（待 M5 P3）。

### OTA-TDMA-PROGRESS-20261101-123 - M3 收口：焦点是“截断包”——改发整包后首块进 AO 并进入 ERASE_SLOT

- 诊断构建 `20261031192406`（保留 AO 失败、不覆盖为 ABORTED）读到真因：切片包 →
  `"FAILED",2,"BAD_HEADER",4`；`received_size` 仍 0。
- 根因：`selected_package_object()` 只发“原头 + 目标对象”（1,140,392 B），头里
  `package_size=2280616`、`image_count=2` 与载荷不符。
- 修复（仅 host 工具、无需刷板）：`ota_stream_send.py` 新增 `--package-whole`，按整包字节发送。
- 实测（整包 2,280,616 B）：1.4 s `RECEIVING`（expected=2280616）→ **8.6 s `ERASE_SLOT`、received=512**
  ⇒ 首块被 AO 接收、会话存活。M3 的“512 B 首块 ≤10 s”在有意义会话中成立。
- 待办：两个诊断开关（`OTA_TDMA_BRIDGE_KEEP_AB_FAILED`、目标侧看门狗 `if (false && …)`）
  必须在 M4 前定稿；下一步跑长 relay 看 erase→writing→durable 推进。
- 台架：四板 build `20261031192406`、IDLE、环停；代码未提交（待 M5 P3）。

### OTA-TDMA-PROGRESS-20261101-122 - M3 现状（二）：AO 从未写入；ABORT 与“截断包（头声明 2.28 MB、载荷 1.14 MB）”高度相关

- 诊断构建 `20261031191406` 临时关闭目标侧 ABORT 看门狗，0.2 s 采样：1.4 s `RECEIVING`
  （`PROG?=0,1140392,0`）→ 9.4 s `ABORTED`（`received_size` 仍为 0）→ 10.0 s 又 `RECEIVING` →
  15.0 s 再 `ABORTED`。**AO 从未消费任何块**，不是写失败。
- 首要候选：host `--package` 经 `selected_package_object()` 只发“原头 512 B + 1 个对象”
  （1,140,392 B），而头里 `package_size=2280616`、`image_count=2` ⇒ 头/载荷不一致。
  旁证：目标 `expected_size=1140392`（切片后长度），与包头声明不符。
- 下一轮：读 `pota_package.c` 头校验规则，改发“自洽单镜像包”或整包 2.28 MB 复测。
- 台架：四板 build `20261031191406`、IDLE、环停；代码未提交（含临时关闭看门狗的诊断改动，
  下一轮必须复原或明确取舍）。

### OTA-TDMA-PROGRESS-20261101-121 - M3 现状：首块 6.3 s 到齐后目标收到显式 ABORT（非写失败）

- 0.6 s 粒度采样目标：1.5 s `RECEIVING`（stage 0）→ 8.9 s `"ABORTED",2,"ABORTED",5`（bridge 已复位，
  diag16-19 全 0）→ 12.3 s 又出现 `RECEIVING`（新一轮会话，stage 192）。
- 解码 `SYST:OTA:STAT?`（state/target_slot/error/last_result）⇒ AO 收到**显式 ABORT 事件**
  （`OTA_ERR_ABORTED`），不是 flash 写失败；目标侧唯一来源是收到 TDMA ABORT 帧
  （`ota_tdma_v2_adapter.c:112-113` → `bridge_app_sink(OP_ABORT)`）。
- 未定：ABORT 的发出者与时机（块到齐后 ~2 s）。下一轮读源侧 `SYST:OTA:TDMA:DIAG?` 的 `s_last_result`
  并核对 relay/session 的 `encode_abort` 调用点。
- 台架：四板 build `20261031185850`、IDLE、环停；代码未提交（待 M5 P3）。

### OTA-TDMA-PROGRESS-20261101-120 - M3：块 ACK 截止 200 ms→2000 ms 打通首块（6.3 s 收满 512 B）；随后目标 ABORT

- 根因：200 ms 的块 ACK 截止短于“一个 512 B 块”的线路时间（8 帧 × 7 chunk ≈ 56 chunk ≈ 0.6 s），
  每块在传完前就超时重放，帧挤满 16 深队列 ⇒ `queue_full` 261–425 次/s、目标 stage 冻结 128 B。
- 修复：`ota_tdma_binding_relay_start()` pump 参数 `150 次/200 ms` → `20 次/2000 ms`（build `20261031185850`）。
- 实测：`queue_full` 10440 → **403**；目标 `stage_len` 64→**512 B（t≈6.3 s）**、`recv=512`（整块到齐）；
  但 8.4 s 目标 `ABORTED`，会话无法继续 ⇒ M3 只达成吞吐判据，未收口。
- 下一步：查该 ABORT 的 error_code/vector.state（块交给 AB 管线后约 2 s 失败），必要时对齐
  `OTA_EVENT_MAX_DATA_SIZE` 与块大小；并复核停滞看门狗 30 s 与 20 次×2 s 的组合。
- 台架：四板 build `20261031185850`；代码未提交（待 M5 P3）。

### OTA-TDMA-PROGRESS-20261101-119 - M2 收口：源侧停滞看门狗让 host 8.2 s 报错（原 122.6 s）

- 目标侧 ABORT 不可靠已证（§9.8）：`ota_tdma_v2_adapter_abort()` 先拆 bridge，排队 ABORT 发不出去；
  源板 `STREAM:STAT?` 恒为 `5,2,0,0,0`。
- 改为源侧看门狗：pump 首次发 DATA 后武装，`durable` 5 s 未推进 ⇒ relay abort + 粘性失败 +
  `STREAM:STAT?` 报 `FAILED`；host `wait_relay_durable` 立即抛错。
- 实测 build `20261031183523`：目标 4.1 s `RECEIVING`；host **8.2 s** 报
  `relay failed on the target (result=171)`；修复前同法 122.6 s。
- 偏离：机制从“目标 ABORT”改为“源侧停滞检测”，已在 playbook §9.9 记录；残余：合法慢块 >5 s
  会被误判，M4 需按实测调整 `OTA_TDMA_RELAY_STALL_MS`。
- 台架：四板 build `20261031183523`、IDLE、环停；代码未提交（待 M5 P3 同批）。

### OTA-TDMA-PROGRESS-20261101-118 - M2 失败回传首修无效：时延 79.3 s；根因是目标只在块完成回调里发 ABORT

- 测量（host 超时 120 s，2 s 粒度）：目标 4.1 s 进 `RECEIVING`、43.0 s `FAILED/NO_PROGRESS`，
  host 122.3 s 才因自身超时报错 ⇒ 端到端 79.3 s（修复前 77.1 s）。
- 首修（build `20261031180044`）：源侧加粘性 `relay_failed` 并让 `SYST:OTA:STREAM:STAT?` 返回 `FAILED`，
  host 见到即抛错。实测无效：源板 STAT 仍是 `5,2,0,0,0`（RECEIVING）⇒ 源的 relay 会话从未失败
  ⇒ **目标从未发送 ABORT**。
- 根因：ABORT 只在 `ota_tdma_v2_bridge_app_release()` 发，而它只由 AO 的 per-block 完成回调触发；
  AO `NO_PROGRESS` 失败后不再有块完成，回调不再发生 ⇒ 失败路径没有周期检测。
- 下一步：在 binding 周期服务里检测 AO vector FAILED 并主动发一次 ABORT，复用已上板的源侧上报；
  复测“目标失败 → host 可见 ≤5 s”。M2 仍未通过，不跨阶。
- 台架：四板 build `20261031180044`、IDLE、环停；未提交代码包含上述首修（待 M5 P3 同批）。

### OTA-TDMA-PROGRESS-20261101-117 - M2：package 模式会话已通；瓶颈确认为“队列满丢弃风暴”，失败回传未达 ≤5 s

- 关键发现：此前 relay 漏传 `--package`，目标按 raw 模式开会话 ⇒ `IMAGE_TOO_LARGE`。
  加 `--package` 后目标正常开出会话并消费数据（122 s 内 `frames +691`）。
- 122 s 增量：源 COM5 `chunks +11085 / mailbox +23118 / win_open +871 / rejects +58574`，其中
  **`queue_full +51887`（425 次/s）**、rx_chunk_rejects +6238、tx_retries +924、tx_fail +450；
  目标 COM4 `frames +691`、`rejects +4242 == rx_chunk_rejects`、restarts +1。
- 结论：线路排空 ≈91 chunks/s（≈9 帧/s、窗口占空比 1.6%）；relay 以每帧一次 `submit` 生产，
  远超排空速率 ⇒ 帧在 16 深队列口被丢弃而非节流，块凑不齐，host 等不到 `durable 512`。
  M2 的“失败 ≤5 s 回传 ABORT”未满足（host 先撞自己的 30 s 超时），M2 记为部分达成、不跨阶。
- 下一步：为 submit 加背压（队列近满时按排空速率节流），再测 512 B 首块；ABORT 时延单列验收。
- 台架：四板 IDLE、环停；代码仍未提交（待 M5 P3 同批）。

### OTA-TDMA-PROGRESS-20261101-116 - M1：23 字段上板；reject 归因指向 TX 队列满（+5057/12 s），非重组器

- 夜间里程碑（`OTA_V2STREAM_PLAYBOOK.md` §9）第一轮。修掉构建阻塞：`v2on` 被默认 preset 重配成
  `v1_compat` 导致 `region RAM overflowed by 4564 bytes`；改用
  `cmake --preset pico2-v2-factory-candidate -DPROJECT_OTA_TDMA_V2_BRIDGE=ON` 后四镜像全部链接。
- 四板统一刷 `20261031171732`（工厂 UF2，full-erase 需 `--flash-size`）；刷后 `BOARD:NO` 归 0，
  重新排 1..4 后 trn03 起环通过。
- `SYST:TDMA:LONGframe:STATus?` 追加 `rx_chunk_rejects/tx_retries/tx_fail/queue_full` 四个只读字段（23 字段），
  上板可读。12 s relay 增量：收端 COM4 `rejects=+1296 == rx_chunk_rejects`（重组器健康，frames +204、
  restarts +2、dups +1）；发端 COM5 `rejects=+7120`，其中 **queue_full=+5057**、rx_chunk_rejects=+2012、
  tx_retries=+128、tx_fail=+52。
- 结论：P0-W 的首要瓶颈是“生产者速度 vs 16 帧 TX 队列/窗口排空”，不是 chunk 损坏或交错重启；
  判据按方向分别写（见 playbook §9.6）。下一步 M2（relay package 模式可通）。
- 代码未提交（固件改动待 M5 P3 凭证同批提交）；台架已停环、四板 IDLE。

### OTA-TDMA-PROGRESS-20261101-115 - 四板统一 build `20261031162743` 并起环成功；19 字段归因实验顺延

- 四板全部刷到 `20261031162743`（COM3/COM6 补齐），trn03 `--expected-build` 通过，环
  `matrix_staged 4/4 → complete → hold`（`left_running=true`）。
- 本轮剩余预算用尽，relay 期间的 `restarts/duplicates/rejects/frames` 增量未采集；实验脚本已就绪：
  起环 → 读两端 19 字段基线 → relay 12 s → 再读增量。
- 台架已停环清理；下一轮直接复跑该归因实验。

### OTA-TDMA-PROGRESS-20261101-114 - RX 健康计数入 SCPI（19 字段）；环复跑被“混 build”挡住

- 代码：`scpi_node_snapshot_commands.c` 的 `SYST:TDMA:LONGframe:STATus?` 追加两个只读字段
  `rx_restarts`/`rx_duplicate_chunks`（来自 `tdma_maintenance_runtime_get_diag`），
  用于区分“交错重启”与“CRC 坏块”。固件编译通过（package 2,282,784）。
- 台架：build id 现在**每次构建都变**（`20261031162743`），而 COM3/COM6 仍是 `20261031153032`，
  trn03 的 `--expected-build` 一致性检查失败（`build mismatch`）⇒ 环起不来。
  修复方式（下一轮）：同一轮里四板统一刷同一 build，再起环。
- 结论：计数工具就绪，但本轮的根因归因实验被混 build 阻断，未取得 restart/duplicate 增量。

### OTA-TDMA-PROGRESS-20261101-113 - 下行恢复后：目标完成 297 个维护帧但 chunk reject +1682，bridge 仍只进 2 帧

- 本轮起环后目标维护服务在跑（`window_open=270→452, cycle=17277→28872`），下行恢复；
  relay 12 s 内：`frames 434→731（+297）`、`rejects 2471→4153（+1682 chunks）`，
  而 bridge `stage` 仍只有 **128**（2 个 DATA 帧），`RECEIVING/vec=3/ready=1`。
- 结论：**下行不再是"完全不送达"，而是 chunk 级拒绝率极高**（+1682 拒绝 vs +297 完成帧）：
  一帧 5 个 chunk，任何 1 个 chunk 被拒就整帧报废（`tdma_maintenance_stream.c:116-132` 的重启分支），
  所以 OTA DATA 帧极少完成。优先查 chunk 拒绝原因（CRC vs 身份不一致），并把 restart/duplicate 计数暴露到 SCPI。
- 另：112 的"目标维护计数全 0"是起环瞬时状态，本次同一流程下恢复正常——复跑前先确认 `window_open>0`。

### OTA-TDMA-PROGRESS-20261101-112 - 计数实验：源端 chunks=3235 已发出，目标侧维护计数全 0（下行未送达）

- 四板重启（计数清零）→ trn03 起环（`en=1/up=1`）→ 25 s relay：
  - 源 COM5：`window_open=642, cycle=41071, mailbox_calls=15558, chunks=3235, rejects=21455`（发送侧在跑）；
  - 目标 COM4：`window_open=0, cycle=0, mailbox_calls=0, chunks=0, frames=0, rejects=0`
    ⇒ **目标侧维护服务一个周期都没跑**（不是“丢帧”，是根本没接收）；
  - 两端 `OTATDMA:DIAG stage=0/type=0`、`OTA:STAT=IDLE`：relay 的 OPEN 也没到目标。
- 与 106（目标 frames=953）/110（目标 stage 128→192）对比 ⇒ 下行送达是**间歇**的，取决于本次环的角色/状态；
  这比“窗口吞吐”更靠前：先要保证维护帧稳定到达目标。
- 下一轮：对比不同起环方式/角色下目标侧 `window_open/cycle`（是否 >0），再定位下行服务为何不跑
  （ring role/service 调度 or 维护 mailbox 注册时机）。

### OTA-TDMA-PROGRESS-20261101-111 - 目标重定基线（P0-W）；吞吐瓶颈新假设：维护帧 chunk 级重启丢弃

- 目标已按用户要求改为 v2 重定基线（`goal-4a29ca94` rev 8/9：maxRounds 50、phase active/armed），
  当前唯一硬阻塞记为 **P0-W：维护流对 OTA 的有效吞吐 ≈0.07–0.2 帧/s**。
- 代码分析（本轮新读）：`tdma_maintenance_stream_rx_chunk()`（`tdma_maintenance_stream.c:93-152`）
  在“帧身份不一致”（chunk_count/frame_size/crc/stream_id/generation 任一变化）时**整体重启重组器**
  并 `restart_count++`。若 OTA 帧的 chunk 与其它维护帧（REFTMEM/训练 ACK）交错到达，
  每交错一次就整帧作废 ⇒ 表现为“目标 frames 计数很高、但 OTA DATA 极少完成”。
- 与实测一致：源端 chunks=7656、目标 frames=953，而 bridge 只收到 2–3 个 DATA 帧；
  目标 `rejects≈12216`。⇒ 不一定是“窗口不给时间”，可能是**同一维护流上的帧交错**导致 OTA 帧反复报废。
- 下一轮验证（不改行为）：读源/目标维护计数（`restart_count`/`duplicate_count`/`chunks`/`frames`）
  对比 OTA 发送帧数；若 restart 随 OTA 发送增长 ⇒ 确认交错假设，修法＝让一帧的 chunk 连续发送
  （或运行时按 stream_id 多重组器），而不是全局重启。

### OTA-TDMA-PROGRESS-20261031-110 - P0/P1 修复落地：目标不再 10 s 早死（RECEIVING 存活、staging 增长到 192 B）；块仍收不满＝窗口吞吐

- 已实现（host 49/49）：
  - **P0-1** `ota_ao.c`：首块期（`RECEIVING && received_size==0`）用 `OTA_FIRST_BLOCK_TIMEOUT_MS=40000`
    代替 10 s 无进展窗口；
  - **P0-3** `ota_tdma_v2_adapter.c`：CHECK/ERASE 期间的 DATA 仍进 bridge staging（不再静默丢弃），
    新增 `deferred_data` 计数；
  - **P0-2** `ota_tdma_v2_bridge_app.c`：块消费后发现 AO=FAILED → 向源端发 `ABORT(error_code)` 并
    `adapter_abort()`（bridge 回 IDLE，允许新 OPEN）；
  - **P1** `CMakeLists.txt`：`PROJECT_OTA_TDMA_V2_BRIDGE` 成为 cache 选项（默认 OFF，`v2on` 用 `-D...=ON`），
    并进 `target_compile_definitions`；`ota_tdma_binding.c` v2 分支按 bridge 会话取/放
    `resource_arbiter_request_ota_admission()`。
- 上板（v2-on build `20261031153032`，四板同 build，环 up）：
  - 目标 `RECEIVING` 持续到约 45 s（不再 11 s 早死），`stage_len` 由 128 → **192**，`vec=3`、`ready=1`；
  - 45 s 后仍 `NO_PROGRESS`（40 s 首块预算用尽），源端 relay 失败；
  - ⇒ 首块 512 B 需 8 帧，实测 3 帧 / 45 s（≈0.07 帧/s），**维护窗口对 OTA 流的有效吞吐才是硬瓶颈**
    （与 097/099/100 的窗口/训练竞争结论一致）。
- 结论：P0/P1 把“早死 + 静默丢帧”修掉了，下一步必须做**窗口吞吐/配额**（或 OTA 期间暂停训练），
  否则首块永远收不满。

### OTA-TDMA-PROGRESS-20261031-109 - P3 复跑仍卡 Step 4（2/4 板）：OTA 步骤可复现地“首次成功、二次失败”

- 复跑（`--run hawf-221207`）：四板重启 → 重跑 Step 2-4；Step 4 再次 `failed_count=2`，四板仍停在
  旧 build `20261031135608`（OTA 未生效）；四板 `SYST:OTA:STAT?` 均为 `IDLE`（无 pending）。
- 对照：**fresh factory flash 后第一次 OTA 成功**（107 的 Step 4 PASS），随后第二次 OTA 复现 2/4 失败。
  ⇒ 与“板端已提交过一次 OTA（BCB/槽位状态变化）”相关，属 OTA 工具/目标状态机问题，不是本次架构改动。
- 结论：P3 凭证在修好该 OTA 二刷问题 + DPLL 采集（107 的 Step 29）之前不可得；
  代码保持未提交（同指纹冻结），功能主线（v2-C 维护流承载）未动。

### OTA-TDMA-PROGRESS-20261031-108 - P3 第二次尝试：Step 4 四板 OTA 全过（fresh factory flash 消除陈旧态）；Step 29 DPLL 采集仍失败

- **Step 4 PASS**：四板先做 fresh factory flash（`picotool_flash.py`，COM3/COM6 首次失败、重试成功），
  再跑 `--profile full`：四板 stream OTA 全部 `durable → READY → commit → applied`，`failed_count=0`。
  ⇒ 上一轮 COM3/COM6 的 `DATA 512 timeout` 属**板端陈旧状态**（残留会话/BCB），不是确定性代码缺陷；
  换新 factory 镜像后消失。
- **Step 29 FAIL**（两次）：`vdc_priority_joint_capture` → `child_capture: Capture exited 2`
  （`out/HardwareAcceptance/20261031/hawf-221207/dpll-short*.log`），60 s 联合采集未完成；
  凭证因此未生成。
- 附：`portable_ota_core_port.c` 的失败 token 增加“失败状态”编码
  （`200 + 10*error + vector.state`，见 `s_flash_execute_stage`），便于区分 VALIDATE/ERASE_PROGRAM/VERIFY。
- 下一轮：定位 `vdc_priority_joint_capture` 的 exit 2（子进程参数/板端 VDC 状态），或按 §2.7 用
  `--method picotool` 变体；通过后即可按同指纹单批提交代码。

### OTA-TDMA-PROGRESS-20261031-107 - 架构 v2 收口进展：C11 复核落档、build id 每构建必变；P3 被既有 stream 路径阻塞

- **HAOFV 架构对齐（已提交）**：
  - `5f651246`：`ARCH_OTA_TDMA_STREAM_DESIGN.md` 升为 v2（REFMEM 控制面 + `0x4F` 数据面，
    §13 决定/符合性/迁移/契约待办）；登记 `ARCH-OTASTREAM-02`（pending）。
  - `28be2bfd`：C11 交叉复核落档（`docs/check/submissions/ARCH_OTA_CROSS_REVIEW_01.md`，
    结论：有条件通过、保留 pending、01 暂不 superseded）；按复核意见修正条款
    （限定 `0x4F`、改为“不再作主路径/按 V2-D 分期退役”、补过渡期 SCPI 入口与 §13.4 退出条件）、
    `code_anchor` 改为 `ota_fb.c`、`docs/README.md` 索引口径同步。
- **build id 修复（已实现并验证，未提交）**：`CMakeLists.txt` 改为每次构建重新生成
  `project_build_info`（`BYPRODUCTS` + 四镜像 `add_dependencies`）；`gen_build_info.py`
  修 Python 3.10 兼容。连续两次构建 id `20261031131629` / `20261031131946`（不同）。
  修复后 `PROJECT_OTA_TDMA_V2_BRIDGE` 默认值切回 **0**（v2 作为 opt-in，V2-C 通过后再默认开）。
- **P3 尝试（flag off，build `20261031132341`，run `hawf-212311`）**：
  - Step 4（OTA 四板）**FAIL**：`COM3/COM6` 在 `ota_stream_send` 失败（COM4/COM5 通过）；
  - Step 29（DPLL 60 s 联合采集）**FAIL**：`TDMA stopped handoff incomplete`，
    `phase=TRN-03B, stage=process-image, explicit startup barrier timed out`；
  - 凭证未生成（`not eligible`）。两次失败均指向**既有 stream OTA 路径**与**台架采集**，
    与 v2 架构改动无直接关系（v2 bridge 已关闭）。

### OTA-TDMA-PROGRESS-20261031-106 - mailbox 修复上板验证：源端 chunks=7656 / 目标 frames=953（此前恒 0），传输打通；块仍未收满

- 修复后上板（四板重启 → trn03 环 up → relay 失败）读数：
  - 源 COM5：`window_open=780, cycle=49889, mailbox_calls=19895, chunks=7656, frames=0, rejects=43433`；
  - 目标 COM4：`window_open=854, cycle=54645, mailbox_calls=0, chunks=0, frames=953, rejects=12216`。
  → **源端 mailbox_calls 由 0 变 19895、chunks 7656；目标 frames 953**：维护传输真正打通。
- 但目标 OTA 仍 `FAILED/NO_PROGRESS`，`DIAG stage_len=128、last_off=64、vec=9`：
  块只收到 2 帧就停（其余帧因 offset 不连续被丢弃或会话已失败后门控关闭）。
- 结论：本轮解决的是“维护帧根本不发/不收”的传输层问题（101-105 链条闭环）；
  剩下的是**块级收满/调度**问题（单窗口可承载的 chunk 数、AO 10 s 无进展窗口）。
- 收口状态：目标未完成（N2 未闭环、N3 未开始、无 P3 凭证）；代码 32 文件未提交。

### OTA-TDMA-PROGRESS-20261031-105 - 修复 mailbox 覆盖：首个注册者生效（runtime 优先），host 49/49

- 代码：`components/tdma/src/tdma_pio_spi_ring_adapter.c:847-869`
  `tdma_pio_spi_ring_adapter_set_maintenance_mailbox()` 改为**首个非空 provider 生效**，
  后续不同 provider 的注册被忽略（注释说明 OTA runtime 队列否则无人取）。
- 依据：`tdma_long_frame_ensure_ready()` 在 SCPI 命令首次使用时才注册（`scpi_node_snapshot_commands.c:62`、
  `scpi_tdma_snapshot_commands.c:1141/1172`），必然晚于 boot 期 `tdma_maintenance_runtime_init`
  （`tdma_runtime_owner.c:430`），因此 SCPI 注册会覆盖 runtime。
- host：**49/49** 绿（无测试直接依赖 mailbox 重注册）；固件编译通过（package 2,281,992 B）。
- 待验证（下一轮）：`mailbox_calls>0`、目标 `DIAG type=1`、单块收满与 durable 递增。

### OTA-TDMA-PROGRESS-20261031-104 - 根因假设：adapter 的 maintenance mailbox 被 SCPI 长帧路径**覆盖**，OTA runtime 队列无人取

- 两处注册同一个 adapter mailbox：
  - `components/tdma/src/tdma_maintenance_runtime.c:194-195`：runtime init 把 mailbox 设为
    runtime 自己的 `maintenance_mailbox`（OTA/REFMEM 帧从 `s_tx_queue` 取）；
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c:220`（`tdma_long_frame_ensure_ready`）：
    把 mailbox 设为 SCPI 的 `tdma_ring_maintenance_mailbox`（从长帧 FIFO 取）。
  **后注册者覆盖前者**：若 SCPI 初始化晚于 runtime init，adapter 只会调用 SCPI 版本，
  OTA runtime 队列（`tdma_maintenance_runtime_submit` 入队）**永远没人取**。
- 与观测一致：`SYST:TDMA:LONGframe:STATus?` 打印的是 **runtime 计数器**（`get_counters`），
  所以 `mailbox_calls=0` 并不代表“adapter 没调用 mailbox”，而代表“runtime 的 mailbox 没被用”；
  同时 window_open 增长（维护服务在跑）。
- 修复方向（最小改动，待验证）：mailbox 注册改为**首个注册者生效**（或让 SCPI 长帧路径与
  OTA runtime 共用一个 provider，SCPI 路径改为向 runtime 队列投递）；随后以
  `mailbox_calls>0` + 目标 `DIAG type=1` 为判据复跑。

### OTA-TDMA-PROGRESS-20261031-103 - 维护注入有两条路径（TX/RX）；trn03 静态环下两条都未触发

- TX 侧（`tdma_pio_spi_ring_adapter_tx.c:927-948`）：需要
  `returned_view.payload_size >= (local_slot_id+1)*TDMA_FLIGHT_SHORT_SLOT_SIZE`，
  满足才向 runtime 取维护帧并塞进本板 short slot。
- RX 侧（`tdma_pio_spi_ring_adapter_rx.c:195-206`）：`!has_flight_tx && maintenance_mailbox != NULL`
  时用维护帧替代 ring payload（maintenance beacon）。
- 实测：trn03 process-image 静态环下 `window_open` 增长、`mailbox_calls=0` ⇒ **两条路径都未进入**
  （TX 侧视图不足；RX 侧 `has_flight_tx` 为真或该路径未被调度）。
- 结论：在当前 ring profile 下，维护流没有稳定的承载槽位。需要设计层面的选择：
  ①在 ring profile 中给维护流固定槽位/配额（TX 侧视图按 slot 数补齐）；
  ②OTA 期间暂停训练（回到 legacy 行为）；
  ③新增独立于 flight 槽位的 OTA 承载（需门①登记）。
- 下一轮：先按①做最小验证（补齐 TX 侧 `payload_size` 或让 `has_flight_tx` 周期性为假），
  以 `mailbox_calls>0` 为判据。

### OTA-TDMA-PROGRESS-20261031-102 - 维护注入条件定位：`returned_view.payload_size ≥ (slot+1)*SHORT_SLOT` 不满足

- 代码锚点：`components/tdma/src/tdma_pio_spi_ring_adapter_tx.c:927-948`
  ```c
  if (adapter->maintenance_mailbox != NULL &&
      returned_view.payload_size >=
          (adapter->config.local_slot_id + 1u) * TDMA_FLIGHT_SHORT_SLOT_SIZE) {
      ... maintenance_mailbox(...) ...   // 只有这时才会向 runtime 取维护帧
  }
  ```
  `mailbox_calls=0` ⇒ 该条件每周期都为假：process-image（trn03 静态环）返回的
  `returned_view.payload_size` 不足以覆盖本板槽位的 short slot。
- 与 101 的关系：窗口在开（window_open 增长）但注入分支从未进入 ⇒ 维护帧根本不进
  `tdma_maintenance_runtime` 的 TX 队列，OTA 的 OPEN/DATA 全部发不出去。
- 结论：TDMA 维护流在“process-image 静态环”配置下**没有可用的 flight 槽位**承载维护帧；
  要么让 process-image 视图覆盖本板 short slot，要么为 OTA 定义一个不依赖 flight 槽位的
  维护承载方式（需按 HAOFV 门①登记）。
- 下一轮：核对 trn03 矩阵/process-image 的 slot 数与 `returned_view.payload_size`，据此选择
  配置修正或新增承载方式；验证 `mailbox_calls>0`。

### OTA-TDMA-PROGRESS-20261031-101 - 维护快照实测：`mailbox_calls=0/chunks=0/frames=0` —— 维护流根本没注入

- 说明：`SYSTem:TDMA:LONGframe:STATus?` **早已存在**（`scpi_node_snapshot_commands.c:60`，
  命令行表 `scpi_system_snapshot_commands.h:577`），上轮“需要新增命令”的前提不成立（证伪）。
- 实测（四板重启基线 → trn03 起环 → relay 失败）：
  - 重启基线：`tx_sent=0/window_open=0/cycle=0/mailbox_calls=0`；
  - 起环后：`window_open_count≈253/274`、`cycle_count≈16k/17k`，但 `mailbox_calls=0/chunks=0/frames=0`，
    `rejects≈5.7k/6.4k`；
  - relay 后：`window_open_count≈679/727`、`cycle≈43k/46k`，仍 `mailbox_calls=0/chunks=0/frames=0`，
    `rejects≈32.7k/17.2k`；两端 `DIAG stage=0/type=0`（**连 OPEN 都没到目标**）。
- 结论：维护窗口在开（window_open 增长），但**维护帧从未注入 mailbox**（mailbox_calls=0），
  即 `tdma_pio_spi_ring_adapter` 的 maintenance mailbox 注入条件未满足（历史上该注入依赖
  flight process image 的 `returned_view.payload_size ≥ (local_slot+1)*SHORT_SLOT`）。
- 下一轮：核对 process-image（flight）是否真正在 returned view 中、以及 maintenance mailbox
  注入条件；必要时在 OTA 期间显式开启维护帧注入。

### OTA-TDMA-PROGRESS-20261031-100 - 维护窗口参数核对：period=64/window=8 cycles；第二类 slot 与 process-image 共享

- 代码核对（`middleware/scpi_port/src/scpi_system_snapshot_commands.c:231-235`）：
  `period_cycles=64 / window_cycles=8 / max_window_bytes=TDMA_LONG_FRAME_MAX`；
  `tdma_maintenance_on_cycle` 每周期一次、窗口内每周期至多送 1 个 chunk
  （`components/tdma/src/tdma_maintenance.c:117` 相位判定）⇒ 理论约 8 chunk / 64 cycle。
- 一帧 76 B = 5 chunk ⇒ 理论约 1.6 帧/窗口；若 ring 周期 ~4 kHz，约 100 帧/s，足以支撑
  ~6 KB/s 的块传输。**实测仅 ~0.2 帧/s，差 3 个数量级** ⇒ 窗口在训练流量下实际被阻塞/延后，
  而非窗口参数本身小。
- 下一步：把 `tdma_maintenance_snapshot_t`（`window_open_count/tx_sent_count/tx_blocked_count`）
  暴露为 SCPI 只读快照，relay 期间在**源端**读取，直接证明 `tx_blocked` 增长；据此决定
  “OTA 期间暂停训练”还是“第二类 slot 给维护流固定配额”。

### OTA-TDMA-PROGRESS-20261031-099 - 窗口配额核对：TX 队列 16 帧不是瓶颈，瓶颈是维护流排程（训练流量竞争）

- 代码核对（`components/tdma/src/tdma_maintenance_runtime.c`）：
  - `TDMA_MAINTENANCE_RUNTIME_TX_QUEUE = 16` 帧，单块 8 帧可完整入队 ⇒ **队列深度不是瓶颈**；
  - `tdma_maintenance_runtime_submit()` 仅在 adapter 未就绪/队列满时拒绝；队列由维护流发送路径
    `take_maintenance_chunk` 按窗口排程排出；
  - 一帧 76 B 需 `ceil(76 / 18) = 5` 个 22 B chunk；process-image 训练同样占用第二类流窗口。
- 结论：目标端只见 2 帧就停在 `NO_PROGRESS`，与“训练流量把窗口吃满、OTA 队列入队后长期排不出去”
  一致；**配额问题在窗口排程，不在队列容量**。
- 下一轮方向（按优先级）：①OTA 期间给维护流固定配额（第二类流调度加 OTA 优先级/最低配额）；
  ②或 OTA 期间暂停 process-image 训练（与 legacy 行为一致）；③首块降尺寸成本高（512 B header
  语义），暂不采用。

### OTA-TDMA-PROGRESS-20261031-098 - 环保持问题定位（OTA 失败态残留导致 RING en=0）；干净重启后 DATA 仍只进 2 帧

- 环保持根因（本轮验证）：四板 OTA 处于 `FAILED` 残留时，`trn03` 虽报 `left_running`，板上
  `TDMA:RING:STATus? en=0/up=0`；**干净重启四板（OTA=IDLE）后起环 `en=1/up=1` 稳定**
  ⇒ 环启动被残留 OTA 状态挡住，属恢复流程问题（需在起环前复位 OTA 状态）。
- 复跑（build `20261031101949`，含 maintenance-required + 周期重放）：
  目标 `stage_len=128 / received=128`（2 个 64 B 帧进入 staging），随后仍 `NO_PROGRESS`；
  末帧 `off=64`，**128 及以后从未到达**。
- 结论：维护窗口在此配置下每次只放行约 2 帧，10 s 无进展窗口内收不满 512 B 块。
  下一轮方向：①提高 OTA 期间维护流配额/优先级（或 OTA 期间暂停 process-image 训练）；②放宽
  AO 首块前的无进展窗口；③或把 bridge 的首块目标降到可在一个窗口内完成的大小（需与 flash 页对齐核算）。

### OTA-TDMA-PROGRESS-20261031-097 - 找到真门控：维护传输在 AB 会话激活后停摆；修复后 DATA 首次进入 staging（128 B）

- **根因（本轮新证据）**：`ota_ao_tdma_maintenance_allowed()` 只返回
  `portable_ota_port_stream_is_active()`。v2 bridge 不打开 stream 会话，因此 **AB 会话一进入
  激活态，维护传输就被关掉**，DATA 帧不再到达——这解释了“帧在 IDLE 期到达、进入 OTA 后消失”。
  `DIAG[23]=9(FAILED)/ready=0` 与此一致。
- 修复：新增 `ota_ao_set_tdma_maintenance_required(bool)`，bridge app 初始化时置 true；
  `ota_ao_tdma_maintenance_allowed()` 改为 `stream_active || required`。
- 修复后上板：目标 `stage_len=128 / received=128`（**DATA 第一次进入 bridge staging**），
  但随即因小窗口停摆：ACK 超时“整块从块首重放”在窗口只放行 ~2 帧时永远到不了块尾。
- 再修 pump：超时改为**游标续传 + 每第 3 次整块重放**（兼顾小窗口推进与静默丢帧修复）；
  host 49/49 绿（`test_ack_timeout_replays_block_from_start` 已按新策略更新）。
- 台架残余：最新构建的验证被“trn03 报 left_running 但板上 `RING:STATus? en=0/up=0`”阻塞
  （环启动后未保持运行），已停环清理；下一轮先解决环保持问题再复跑。

### OTA-TDMA-PROGRESS-20261031-096 - 门控输入可观测（DIAG[23]=vector state / [24]=ready）；台架复跑被环启动失败阻塞

- 新增：`ota_tdma_v2_bridge_app_stats()` 扩到 9 项，增加 **最近一次门控看到的 vector state** 与
  **ready 结果**；binding 在 bridge 模式把二者映射到 `DIAG[23]`/`DIAG[24]`
  （替代原先的 ingress 结果/relay 标志，仅桥接模式）。
- host：**49/49** 绿；固件编译通过（package 2,281,912 B）。
- 台架复跑未完成：本轮多次起环失败（`could not open port COM6: PermissionError`——前序被 kill 的
  python 进程占用串口），relay 未能到达目标，`DIAG[23]/[24]` 未取得有效读数。
  已清理残留进程并停环；下一轮需先确认四个 COM 口可用再起环。
- 结论：门控输入现在**可观测**，但"门控关闭的瞬间状态"仍待一次有效上板读数确认。

### OTA-TDMA-PROGRESS-20261031-095 - 源/目标双侧对照：源端确实发过 offset=0；目标收到 offset=0 但被 **ready 门控**丢弃

- 新增源端可观测：pump 记录 `last_sent_offset / send_count / timeout_replays`，relay 暴露
  `ota_tdma_relay_pump_diag()`；binding 在每次 relay tick 后把 **400000+last_sent_offset** 写入
  OTA phase（host 工具退出后仍可读）。
- 上板对照（同一次运行，读于 relay 退出后）：
  - 源端 `WATCHdog:OTA:PHase? = 400064` → **源端最后一次成功提交的 DATA 是 offset=64**（说明 0 也发过、传输接受了）；
  - 目标 `DIAG`：`last_type=2 / last_off=0 / last_len=64`，但 `stage_len=0 / received=0`
    → **offset=0 的帧到达了目标，却没进 staging**。
- 结论：`bridge_data()` 未被调用或未被接受，唯一在它之前的分支就是 **`ready` 门控**
  （`vector.state == OTA_STATE_RECEIVING` 才放行）。即：帧到达时 vector 不是 RECEIVING
  （擦除窗口或已 NO_PROGRESS），帧被“忽略但返回成功”(phase 82)，stage 永远为 0。
- 下一步（已定）：把 **ready 门控 + vector state 暴露到 DIAG**，并在门控关闭时**暂存**该帧
  （不丢首帧）而不是直接忽略；或把门控改成“核心是否可写”而不是 AO vector。

### OTA-TDMA-PROGRESS-20261031-094 - 帧级诊断：DATA 帧到达但**首帧 offset=0 缺失**（末帧 64/128），staging 恒为 0

- 新增帧级可观测：adapter 记录 `last_type/last_offset/last_length`，binding 映射到
  `DIAG[20]=类型 / [21]=offset / [15]=payload_len`（bridge 模式下）。
- 上板读数（两轮一致）：`last_type=2(DATA)，last_off=64 或 128，last_len=64`，
  同时 `stage_off=0 / stage_len=0 / received=0` ⇒ **offset=0 的首帧从未到达目标**，
  后续帧因不连续被 bridge 丢弃。
- 已修一处真实缺陷：pump 的 `send_block` 在**续传**时也会重置 ACK deadline，
  导致 relay 每 tick 的 `resume` 不断把超时往后推、**整块重放永远不触发**
  （静默丢头帧无法修复）。现在只在“块首”装载 deadline；pump host 用例仍全绿。
- 但修后上板仍 `last_off=64`、`received=0` ⇒ 需查**源端是否真的发出 offset=0**：
  下一轮加源端统计（pump 已发送的 offset/attempts/replay 次数）与目标侧对照，
  判断是“源端未发 0”还是“0 被维护窗口丢弃”。
- host：**49/49** 绿。

### OTA-TDMA-PROGRESS-20261031-093 - 目标 bridge 统计上板：`stage/received=0`——DATA 根本没进 staging

- 本轮加固：pump 在 **ACK 超时**时改为**整块从块首重放**（此前按游标续传，遇到维护窗口静默丢帧会
  永久卡在缺口）；新增 host 回归 `test_ack_timeout_replays_block_from_start`；**49/49** 绿。
- 诊断曝光：`ota_tdma_v2_bridge_app_stats()` → `SYST:OTA:TDMA:DIAG?` 的 16..19
  （stage_offset/stage_length/received/inflight）。
- 上板（build `20261031101949`，四板）：
  - 目标状态时序：`IDLE → RECEIVING`（约 1–4 s）→ `FAILED/NO_PROGRESS`（约 11 s）；
  - `DIAG count=231` 但 **stage_off=0 / stage_len=0 / received=0 / inflight=0**：
    没有任何 DATA 进入 bridge staging。
- 两个候选（下一轮用一次实验区分）：
  1. **pump 没有发 DATA**（源端会话/ACK 路径问题）；
  2. **ready 门控一直为假**（帧到达瞬间 vector 不是 `OTA_STATE_RECEIVING`）。
- 决定性实验：把 ready 恒真 + 保留统计；若 `received>0` → 是门控（改门控语义/放宽）；
  若仍为 0 → 是源端未发 DATA（查 relay 会话状态/ACK 处理）。

### OTA-TDMA-PROGRESS-20261031-092 - V2 质量加固：DATA 等 AB 就绪、relay 重试覆盖擦除窗口；目标仍 NO_PROGRESS

- 质量项（本轮）：
  - 适配器新增 **ready 回调**：仅当既有 AB 管线处于 `OTA_STATE_RECEIVING` 才接受 DATA，
    擦除/权限阶段的数据帧被忽略（源端重传），避免 `INVALID_STATE` 直接判死会话；
  - relay 重试从 3×100 ms 提升到 **150×200 ms**（≈30 s），覆盖 inactive slot 擦除窗口；
  - binding host 测试编译时强制 `PROJECT_OTA_TDMA_V2_BRIDGE=0`（v2 代码由 app TU 覆盖）；
  - FreeRTOS 堆二次下调（`97*1024-512`）以容纳 staging/适配器；package 2,281,656 B。
- 上板（build `20261031101949`）：目标 `SYST:OTA:STAT?` 仍收敛到 **FAILED / NO_PROGRESS**，
  `TDMA:DIAG? count=626`（大量 DATA 帧在场，pump 在重传），说明 **AB 会话没有在 10 s 窗口内
  到达 RECEIVING**（或 vector 在擦除期间未推进），数据门控因此一直不放行。
- 关键疑问（下一轮第一件事）：`portable_ota_port_core_begin` 之后 vector 状态序列
  (`CHECK_PERMISSION→ERASE_SLOT→RECEIVING`) 是否真的在推进；需在 relay 期间**逐秒轮询**
  `SYST:OTA:STAT?`，并检查 AO 的 tick 是否在驱动 core service。
- host：**49/49** 绿（bridge 6 组、adapter 5 组含就绪门控）。

### OTA-TDMA-PROGRESS-20261031-091 - V2 上板首测：TDMA 帧已进既有 OTA 管线（BEGIN 被接受），块未收满 + NO_PROGRESS

- 接线完成：`ota_tdma_v2_bridge_app`（application）+ binding `PROJECT_OTA_TDMA_V2_BRIDGE=1`
  （维护帧走 adapter，sink → `ota_ao_post_event`，AO 消费后 `release` → ACK）；
  `ota_ao` 新增 data-done hook；CMake 源清单补 3 个文件；为容纳 512 B staging 把 FreeRTOS 堆
  从 `97*1024-64` 调到 `97*1024-448`（RAM 曾溢出 380 B）。
- 上板（build `20261031101949`，四板 factory + 静态维护环）：目标 `SYST:OTA:STAT?` 变为
  **FAILED / NO_PROGRESS(4)**，说明 **既有 AB 管线确实接住了 BEGIN**（架构 v2 目标已打通一半）；
  `TDMA:DIAG? count=9`（OPEN 2 片 + 7 个 DATA 帧）→ 块差最后 1 帧未收满，pump 等不到 ACK。
- 诊断：适配器新增“开流握手 ACK(0)”（否则源端 pump 不会发 DATA）；目前卡在
  **块收满 + 10 s NO_PROGRESS**：`OTA_PROGRESS_WATCH_DEFAULT_TIMEOUT_MS = 10000`，
  而 relay 的维护窗口丢 1 帧后 pump 只有 3 次重试（100 ms/次），块迟迟不齐。
- 下一步：①提高 relay 的块重试次数/窗口（或按帧 ACK 的 v2 语义改造 pump）；②首块延迟与
  10 s 无进展窗口的冲突（BEGIN 后到首块 durable 的时间预算）；③复跑后进 V2-C 判据。

### OTA-TDMA-PROGRESS-20261031-090 - V2-B 收敛：bridge 按“flash 可写块”聚合（512/256），ACK 与块边界对齐

- 问题：适配器初版按 64 B 帧逐块投递并逐帧 ACK，与 pump 的“块级 ACK”（首块 512、其后 256）不一致，
  会把正常重传判成协议错误；且每 64 B 一个 OTA 事件对 flash 写入效率极低。
- 修正（`ota_tdma_bridge.[ch]`）：
  - bridge 内置 **staging**：按 64 B 帧累积，满 **512 B（首块）/ 256 B（其后）** 才向 sink 投递一个
    DATA 操作；这是既有 OTA 管线一次 flash 写入的粒度；
  - **单块在途**：块投递后 `block_in_flight=true`，后续帧返回 backpressure（源端重传），
    直到 AO 消费完调用 `release()`；
  - `release()` 返回块尾偏移，适配器据此回 ACK —— ACK 与 pump 的块边界严格对齐；
  - `end()` 要求无在途块、无残余 stage、收满镜像；abort 清空 staging 回 IDLE。
- 适配器相应简化：帧解码/分发 + OPEN 重组；非连续 offset/重复帧一律忽略（不 NACK，避免误杀 pump），
  仅 sink 失败为致命。
- host：bridge 6 组 + adapter 4 组用例全过；聚合 **49/49** 绿；固件编译通过（package 2,279,896 B）。
- 下一步：binding 侧 `PROJECT_OTA_TDMA_V2_BRIDGE` 开关接入（sink→现有 OTA event、AO 消费后 release），
  本地单板验证后进 V2-C。

### OTA-TDMA-PROGRESS-20261031-089 - V2-B 核心：维护帧→bridge 适配器（OPEN 重组→begin、DATA→背压、CLOSE→end、ABORT）

- 新增 `components/ota_manager/inc/ota_tdma_v2_adapter.h` + `src/ota_tdma_v2_adapter.c`：
  - OPEN 分片重组 → `pota_stream_open_decode_le` → `bridge_begin(total_size, crc32, package_mode)`；
  - DATA：offset 连续校验；重复块忽略（不重复投递）、在途块背压时返回 true（让 pump 重传）；
  - **ACK 时机**：只有既有管线消费完块（`ota_tdma_v2_adapter_release`）后才回 ACK，
    与“数据先进 AO、再确认 durable”的 v2 语义一致；
  - CLOSE → `bridge_end` + ACK；ABORT → `bridge_abort` 回 IDLE；ACK 帧不消费。
- host 测试 `tests/unit/test_ota_tdma_v2_adapter.c`（全流程/背压/重复/abort/拒绝），
  已加入聚合 runner：**49/49 绿**。
- 固件编译通过（`out/build/v2adapter`，package 2,279,896 B）。
- 下一步：binding 侧 feature 开关接入 adapter（sink → 现有 OTA event；AO 消费后回调 release），
  先本地单板跑通 `SYST:OTA:STAT` 的 `BEGIN→ERASE→WRITING→VERIFY→READY_TO_REBOOT`。

### OTA-TDMA-PROGRESS-20261031-088 - V2-A/B 起步：TDMA→既有 OTA 管线 bridge（单块在途 + 背压），host 绿

- 新增 `components/ota_manager/inc/ota_tdma_bridge.h` + `src/ota_tdma_bridge.c`：
  - 把维护流操作映射为 `BEGIN/DATA/END/ABORT` 并交给 sink（sink 最终接既有 `ota_ao` 事件管线）；
  - 状态机 IDLE/RECEIVING/DONE/FAILED；越界/短镜像/重复 begin 一律拒绝；
  - **单块在途 + 背压**：DATA 只有在前一块被 `ota_tdma_bridge_release()` 释放后才转发下一块，
    避免 AO 仍持有帧缓冲时被覆写（与 080 的 OPEN 指针悬垂同类风险）；
  - sink 拒绝 → FAILED（fail-closed），不再向 flash 转发。
- 新增 host 测试 `tests/unit/test_ota_tdma_bridge.c`（6 组用例，含背压/短镜像/sink 失败/abort），
  已加入聚合 runner；单测与 **48/48** 聚合套件均绿。
- 固件编译通过（`out/build/v2b`，package 2279896 B）；bridge 尚未接入 AO 事件入口（下一步）。
- 下一步（V2-B 后半）：AO 消费完 DATA 事件后回调 `ota_tdma_bridge_release()`；
  维护帧 OPEN/CLOSE/ABORT 走 bridge 而非 stream session（feature 开关），
  先本地单板验证 `SYST:OTA:STAT` 走 BEGIN→ERASE→WRITING→VERIFY→READY_TO_REBOOT。

### OTA-TDMA-PROGRESS-20261031-087 - 架构 v2 决定：REFMEM 控制面 + TDMA 数据面（用户确认）

- 用户确认接受代价，要求"先把架构更新一下"；决定写入
  `docs/ota/OTA_V2STREAM_PLAYBOOK.md` §8（架构 v2 决定 + 迁移阶段 V2-A..V2-D）。
- 要点：①REFMEM 统一控制面负责 begin/commit/switch/abort；②TDMA 0x4F 只承载 OTA 数据块；
  ③目标侧维护帧 DATA 投递到既有 `ota_ao`/`ota_fb` → `portable_ota_port_core_begin/write/end`
  （与 SCPI 路径同一套 flash/BCB/切换）；④退役目标侧第二套 stream session/ingress/checkpoint
  （过渡期保留 USB 直连并加开关）。
- 依据：既有一台一板 AB 流程已验证；stream 路径阻塞在 completion journal（`COMPLETION(22)`）且
  多一套状态机/持久化；估算第二套静态约 3–4 KB；台架实测残留 RECEIVING stream 会话会挡住
  legacy `SYST:OTA:BEGIN`（第二套污染第一套）。
- 基线备注（本轮台架）：`ota_send.py` raw 镜像模式在 V2 上命中 `SIGNATURE_INVALID`
  （require_signature + 非 package），AB 基线必须走 package 模式；COM4 残留会话经
  `picotool_reboot` 后 `STREAM:STAT?` 回到 IDLE。
- 未做：V2-A 代码 bridge（下一轮）；P3 凭证仍不覆盖当前未提交改动。

### OTA-TDMA-PROGRESS-20261031-086 - 擦除粒度澄清与方向：复用正常 flash 流程 / REFMEM 控制面

- 更正 085 的假设：`pota_core.c:171 pota_erase_one_step()` **按扇区（flash_sector_size）逐步擦除**，
  不是一次性擦整槽；因此“大擦除被拒”不成立。
- 失败点仍是事务内的 **completion journal**（`completion.error=COMPLETION(22)`），
  与 `ota_journal_completion_append` → `ota_journal_region_program` 的页读改写/嵌套 journal 有关。
- 对照：正常 AB 路径（`ota_fb.c` → `portable_ota_port_core_begin/write`）用的是**同一个 port 的同一套
  flash 操作**且能通过；stream 路径（`pota_stream_session_write`）在同板上失败，说明差异在
  事务的 completion lease/journal 状态而非擦除粒度。
- 方向（按用户指示）：stream 的 flash 擦除/编程**复用正常流程**（REFMEM 统一控制面调用正常 flash
  流程），而不是在 stream port 里另起一套事务语义；下一步优先验证“stream 图像事务不挂 completion
  lease（改由 stream 自身的 checkpoint journal 保证持久性）”是否与正常流程一致。
- 目标提醒：TDMA 流式 OTA 的最终目标是**只用一个端口升级所有板卡**；当前 N2 是单跳（NO.1→NO.2）。

### OTA-TDMA-PROGRESS-20261031-085 - 目标 flash 失败码定为 COMPLETION(22)，且本地 SCPI stream 同样复现 → 非 TDMA 专属

- 诊断加固：`portable_ota_core_port.c` 的 `s_flash_execute_stage` 改为 `200 + completion.error`；
  上板 token = **2222** = `2000 + (200 + 22)` → `FLASH_TRANSACTION_ERROR_COMPLETION(22)`。
- 关键对照实验：同一 build `20261031091955`、同一目标板 COM4，用**本地 USB SCPI stream**
  发同一 package（`--package`）——同样 `durable_offset=512` 后下一块 DATA 超时（工具 20 s 超时；
  板端 SCPI handler 正在“等 core 离开 ERASE_SLOT”，最长 300 s）。
- 结论：**不是 TDMA/relay 专属问题**。目标侧首块（package header，512 B）落地后，core 进入
  erase，`flash_transaction_ao_execute` 返回 FAILED/COMPLETION，擦除无法收敛；本地与 TDMA
  两条入口一致复现。
- COMPLETION 的来源（`flash_transaction_fb.c`）：completion lease 校验/retain/append 失败
  （L456-458、L664-667、L675-678）。下一个候选差异：
  `ota_journal_completion_program` → `ota_journal_region_program` 的“读-改-写”页（L128-140）
  在事务进行中读回失败，或 factory 空白 BCB 下 completion journal 槽位未就绪。
- 下一轮：①在 completion append 失败点加 reason（retain/valid/append/page-read）；
  ②对比 legacy 路径（同样用 flash 事务但走 ota_fb）为何可通过；③定位后修 stream 擦除路径。

### OTA-TDMA-PROGRESS-20261031-084 - N2 目标侧 OTA admission 缺口修复；flash 事务仍 FAILED（需 error 码）

- 先排除“训练锁”：环运行期间 `SYSTem:RESource:TRAINing? = 0,0`（COM5/COM4 均如此），
  说明 `FLASH_TRANSACTION_ERROR_TDMA_TRAINING_ACTIVE` 不是 083 的失败原因。
- 找到真实缺口：`flash_transaction_ao` 对 OTA requester 要求
  `resource_arbiter_ota_admission_active()`，否则返回 **MODE**；而 OTA admission 只由 **SCPI**
  OTA 命令获取（`scpi_ota_commands.c` 的统一入口）。TDMA 目标的 OPEN 走维护流、不经过 SCPI，
  **从未申请 admission** → 目标侧 flash 事务被 MODE 拒绝。
- 修复（`application/src/ota_tdma_binding.c`）：目标侧在 OPEN 成功后
  `resource_arbiter_request_ota_admission()`，会话 DONE/FAILED/reset 时释放；新增
  `test_ota_tdma_binding` 断言（OPEN 后 request=1、DONE 后 release=1），binding host 测试通过。
- 上板（build `20261031090405`）：目标仍 `STREAM:STAT? = 5,6,512,2102,5`（duration=512、FAILED、
  token=2102=`2000+flash_execute_stage(102)`）。admission 修复生效但事务仍报
  `FLASH_TRANSACTION_RESULT_FAILED`；**需把 `completion.error` 编入诊断**（下一步：
  `stage = 200 + error`，可区分 MODE/POLICY/RANGE/RAW_OPERATION/COMPLETION 等）。
- 台架：四板 build `20261031090405`、环已停；工作区 7 个源码改动未提交（pump/binding/port/tests）。

### OTA-TDMA-PROGRESS-20261031-083 - N2 pump 部分窗口续传修复：目标首个 512 B 块 durable，随后 flash 执行阶段 102 失败

- 定位（082 遗留）：目标 ingress `stage=0/stage_len=64/expected=64/count=50`——窗口每次只放行 1 帧，
  pump 重试却总从块首重发，块头被反复重传、目标永远停在 64 B。根因在 `ota_tdma_pump_send_block`：
  未按块内游标续传，且 `sent_offset/sent_length` 被逐 chunk 覆盖，导致 ACK 期望与重试判据都错位。
- 修复（`components/ota_manager/src/ota_tdma_pump.c` + `.h`）：
  ① 新增 `send_cursor`（块内首个未发送 chunk），部分发送后重试从游标续传；
  ② `sent_offset/sent_length` 恢复为 **块语义**（`send_data` 不再逐 chunk 覆盖），块 ACK 恒与块尾对齐；
  ③ 窗口关闭且一帧未发出时保留原状态（可重入），部分发出则进入 `WAIT_ACK` 由重试定时器续传；
  ④ ACK 超时且整块已发完时把游标复位到块首重放。
- Host：新增 `test_partial_window_resumes_at_cursor`（预算 1 帧/次）；全量 host 单测 **47/47 绿**。
- 上板（build `20261031084017`，四板 factory + 静态维护环）：目标首个 512 B 块 **durable=512**
  （`STREAM:STAT? = 5,6,512,2102,5`），较 082 的 durable=0 实测前进；随后会话 FAILED，
  token `2102` = `2000 + s_flash_execute_stage`，即 **bounded FlashTransaction 步骤 102 失败**。
- 结论：pump/窗口层已打通；剩余阻塞转到目标侧 flash 执行步骤（与 082 的“擦除窗口”同源：首块
  package header 落地后 core 进入 permission/erase，下一块写入/提交落在该窗口内）。

### OTA-TDMA-PROGRESS-20261031-082 - N2 首次打通 relay OPEN：目标进入 TDMA RECEIVING（durable 仍 0）

- 固件：`n2final` build `20261031075103`（含 stream 平台恢复：OPEN 前 ensure init + force_reset 保留 journal sink），
  四板 factory 刷写、`1,0,1,0,0`；静态维护环 running（`out/n2_ring_c.log`，tx≈rx）。
- 结果：NO.1 → `ota_stream_send --relay-node 1 --package` 后，目标 `SYST:OTA:STREAM:STAT?` =
  **`5,2,0,99939879,0`**（source=TDMA、state=RECEIVING、last_result=OK），`SYST:WATCHdog:OTA:PHase?` = **84**
  （target stream opened），`SYST:OTA:TDMA:DIAG?` trace=**304**（开流成功）。
- **Runbook 坑（已确认）**：host 发 package 必须带 `--package`，否则 OPEN 的 `package_mode=0`，
  目标 `pota_begin` 命中 `require_signature && !package_mode` → `POTA_ERR_SIGNATURE_INVALID(16)`
  （表现为 `STREAM_OPEN_5`，Diag stage=816）。原始 relay 尝试失败即此因，不是平台恢复问题。
- 平台恢复链（factory 刷写后）：boot 期 `ota_ao_init` 可能跳过 stream init，OPEN 恒 SESSION；
  在 `portable_ota_port_stream_open` 前 ensure（`ota_metadata_load` 成功则 stream_init，否则 factory
  metadata 兜底）；`force_reset` 必须走 strict（挂 journal sink），degraded 无 sink 会被 `DURABLE_ACK`
  拒绝为 `CHECKPOINT(9)`。
- **仍阻塞**：coordinator 侧 `relay durable offset 512 not reached before timeout`，目标 durable=0。
  进一步定位：目标 DIAG `entry.size=76 / b0=2 (DATA) / count 46→94` 证明 **DATA 帧已持续到达目标**，
  且 phase 仍为 84（帧处理返回成功、未标记失败）；但目标 session `durable=0`、`TRANSaction?` 全 0
  → 卡在目标侧“512 B 块写入/提交/ACK”这一段（ingress flush → port write → core 事务），不是 relay 或 OPEN。
- **不对称点（下一轮入手）**：SCPI 本地路径在每次 DATA 前有“等 core 离开 CHECK_PERMISSION/ERASE_SLOT”
  的有界等待（`scpi_cmd_ota_stream_data`，最长 300 s、5 ms 粒度）；**TDMA ingress 的 flush 没有这个等待**，
  首块（package header）触发擦除后，后续 flush 可能落在 ERASE_SLOT 窗口内被拒/丢弃，而 ingress 仍报 OK
  → 目标 durable 不推进、无 ACK。修复方向：ingress 在 flush 前做有界等待，或在擦除窗口内保持 staging 并
  让 relay 重传（需要 offset/duplicate 语义配合）。
- 代码状态：ensure 修复在 `portable_ota_core_port.c`（未提交，指纹已变，需重跑 P3 才能提交）。

### OTA-TDMA-PROGRESS-20261031-081 - N2 上板：relay OPEN 已修复但目标 stream 平台恢复链仍有缺口

- 现场：四板 factory 刷写（build `20261031063547`）+ 静态维护环 running（`out/n2_ring_b.log`，hold 900 s，
  `tx≈rx`），NO.1 → `ota_stream_send --relay-node 1`。
- **relay 修复已生效**：目标 `SYST:OTA:TDMA:DIAG?` 解出 trace=302/303，`map_version=2`、`destination=2`、
  `size=2278xxx`、`caps=7`（历史全零/MISMATCH 不再出现），`wire` 内容正确到达目标。
- 新阻塞：目标 `pota_stream_session_open` 失败，raw 结果经诊断通道定位为
  **CHECKPOINT(9)**（`DURABLE_ACK` 要求 session 挂 checkpoint sink；degraded 重建无 sink → 恒 CHECKPOINT），
  改为 strict（journal attach）后前进到 **CORE(10)**（`pota_session_begin` 在目标槽上失败）。
- 现场事实：factory 刷写后 BCB 为空白，boot 期 `ota_ao_init` 可能跳过 stream init；
  SCPI bootstrap 只在 `STREAM_OPEN` 的 SESSION 分支触发，TDMA ingress 无此 bootstrap。
- 实验改动（`portable_ota_core_port.c`：ensure init / degraded 重建 / stage 诊断）**已回退**，
  备份 `out/n2_stream_recovery_experiment.patch`；回退后工作树与 `34037072` 一致。
- 下一步：①按 host 可测方式实现 stream 平台恢复（确保 sink 挂接 + 陈旧 checkpoint 可清除/忽略）；
  ②定位目标 `pota_session_begin` 的 CORE 失败（权限/擦除/槽位）；③再跑 N2 判据。

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
