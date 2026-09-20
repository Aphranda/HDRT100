# HAOFV 架构风险评估

Status: Active
Domain: HAOFV
Canonical: `docs/arch/HAOFV_ARCHITECTURE_RISK_EVALUATION.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_TODO.md`, `docs/arch/HAOFV_MAINTENANCE_TODO.md`, `docs/arch/HAOFV_PORTABILITY_EVALUATION.md`
Last updated: 2026-08-13

本文档是 HAOFV 顶层架构的独立风险评估快照。它记录对 `HAOFV_ARCHITECTURE.md`
及其实施落点（RTOS 子文档、`components/` 代码）的评审结论，按严重度分级，并给出
每条风险的处置去向。本文档只记录会影响 owner、跨核契约、硬实时边界、ECC 状态机规模
和恢复路径的架构风险，不记录普通功能开发流水账。

评估基线（2026-08-13）：

- `docs/arch/HAOFV_ARCHITECTURE.md`
- `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
- `docs/arch/RTOS_HAOFV_TODO.md`
- `components/sync_trigger/src/trigger_fb.c`
- `components/sync_trigger/inc/trigger_vector.h`
- `components/distributed_refmem/`

## 严重度分级

| 级别 | 含义 | 处置要求 |
|---|---|---|
| S0 致命 | 会导致 hard fault、变砖、双副本损坏或不可恢复的硬件故障 | 升格为顶层硬约束，落地强制机制 |
| S1 高 | 架构边界缺失或文档失真，造成错误 owner 假设或难发现的逻辑缺陷 | 顶层文档补契约 + 逐字段/逐规则落地 |
| S2 中 | 前瞻性或可观测性缺口，单核裸机阶段未必触发，RTOS+双核阶段放大 | 提前定义优先级/超时/允许矩阵 |
| S3 低 | 措辞/规范软约束，当前代码未违规 | 升格为硬规则措辞 |

## 风险状态定义

| 状态 | 含义 |
|---|---|
| 未处理 | 顶层文档与代码均未定义或未落地。 |
| 部分承接 | 文档已有部分约束，但不完整、非强制或未逐字段落地。 |
| 已记录待办 | 已在 `RTOS_HAOFV_TODO.md` / `HAOFV_MAINTENANCE_TODO.md` 有条目，但未完成。 |
| 顶层已承接 | `HAOFV_ARCHITECTURE.md` 已升格为硬约束，代码实现和验证仍按 TODO 推进。 |

## 事实校正

以下文档与代码不一致，作为风险判定的先决事实修正，避免沿用失真数字。

| 项 | 文档声称 | 代码实际 | 证据 |
|---|---|---|---|
| TriggerFB ECC 规则数 | 58 条 | **190 条** | `HAOFV_ARCHITECTURE.md` 当前规模段 vs `trigger_fb.c` `s_ecc_table[]` |
| TriggerFB 状态数 | 6 个 | **8 个** | `trigger_vector.h` `trig_state_t` 枚举 |
| TriggerFB 事件数 | 20 个 | **约 75 个** | `trigger_vector.h` `trig_event_type_t` 枚举 |
| TriggerVector 字段数 | 文档建议字段 9 个 | **约 90 个** | `trigger_vector.h` `trigger_vector_t` 结构体 |

> 文档中「58 条 / 6 状态 / 20 事件」已过时，实际规模约为其 3 倍以上。任何依赖该数字的
> 复杂度评估、测试覆盖目标或"可维护性"结论都应先按实际数字重新计算。

## 风险登记表

| ID | 严重度 | 状态 | 一句话结论 |
|---|---|---|---|
| [HAOFV-RISK-20260813-001](#haofv-risk-20260813-001) | S1 | 顶层已承接 | TriggerFB ECC 表 190 条且文档数字失真，"配置直通"条目可默认规则化 |
| [HAOFV-RISK-20260813-002](#haofv-risk-20260813-002) | S1 | 顶层已承接 | Vector 缺逐字段 writer/值域/生命周期/快照标注 |
| [HAOFV-RISK-20260813-003](#haofv-risk-20260813-003) | S1 | 顶层已承接 | 跨核反射内存契约未升格到 HAOFV 顶层，代码未落地 |
| [HAOFV-RISK-20260813-004](#haofv-risk-20260813-004) | S0 | 顶层已承接 | XIP + Flash 擦写双核冲突，core1 可能 hard fault |
| [HAOFV-RISK-20260813-005](#haofv-risk-20260813-005) | S2 | 已记录待办 | Resource Arbiter 缺资源优先级与等待队列 |
| [HAOFV-RISK-20260813-006](#haofv-risk-20260813-006) | S2 | 已记录待办 | OTA 允许矩阵缺集中定义，错误码/UI 提示未定义 |
| [HAOFV-RISK-20260813-007](#haofv-risk-20260813-007) | S2 | 顶层已承接 | 调度预算缺 overrun 回调，时间片语义未明确 |
| [HAOFV-RISK-20260813-008](#haofv-risk-20260813-008) | S1 | 顶层已承接 | Metadata 双副本双损坏缺强制 Bootloader failsafe |
| [HAOFV-RISK-20260813-009](#haofv-risk-20260813-009) | S1 | 顶层已承接 | `timestamp_ms` 49 天回绕，时间比较规则未强制 |
| [HAOFV-RISK-20260813-010](#haofv-risk-20260813-010) | S3 | 顶层已承接 | FB "不得长期阻塞"措辞偏软，未强制 BUSY/next_state=self |

## 风险明细

### HAOFV-RISK-20260813-001 - TriggerFB ECC 表规模失控且文档数字失真

- 严重度：S1
- 状态：顶层已承接
- 问题：
  - 文档写「58 条 ECC 规则，覆盖 6 个状态 + 20 个事件」，实际 `trigger_fb.c` 的
    `s_ecc_table[]` 有 **190 条**、8 个状态、约 75 个事件，文档低估 3 倍以上。
  - 190 条中绝大多数是 `{ IDLE/SEQ_CONFIGURED/BISS_CONFIGURED, SET_*, fb_instant_cmd }`
    这类"配置直通"条目，本质是同一类规则的穷举，且要靠 ECC 表逐状态注册。
  - `fb_instant_cmd` 内部又把约 60 个 `SET_*` 事件揉在一个大 switch 里，形成"表驱动 +
    代码 switch"双份维护。
- 影响：
  - 新增一个 BiSS/PCNT 配置字段需要同时改枚举、switch、以及多个状态的 ECC 表条目，遗漏
    即产生静默不响应。
  - 文档数字失真使复杂度评估和测试覆盖目标失真。
- 处置：
  - [x] 修正 `HAOFV_ARCHITECTURE.md` 中 ECC 规模数字为实际值，或改为"由 `TRIG_ECC_TABLE_COUNT` 推导"。
  - [ ] 引入"默认规则"：未命中的 `SET_*` 配置事件在 IDLE/CONFIGURED 态统一走 `fb_instant_cmd`，把直通条目合并为少数几条。
  - [ ] 增加静态校验脚本，抓重复 `(state, event)` 组合（首条匹配即返回，重复会被静默遮蔽）和不可达条目。
  - [ ] 将 BiSS-C 配置从 `trigger_fb.c` 的 `fb_instant_cmd` 巨 switch 拆为独立 `CommunicationFB` 配置域。

### HAOFV-RISK-20260813-002 - Vector 缺逐字段 writer/值域/生命周期/快照标注

- 严重度：S1
- 状态：顶层已承接
- 问题：
  - `HAOFV_ARCHITECTURE.md` 有「字段值域约定」表（仅 5 个字段）和「写权限规则」表（仅
    `state`/`system_mode`/`resource_locks` 等少数字段），**不是逐字段**。
  - 代码 `trigger_vector_t` 有约 90 个字段，其中 BiSS-C 段 60+ 字段平铺，只有零散 writer
    注释（如"HAOFV: TriggerFB 在 IDLE/SEQ_CONFIGURED 状态写入"），**没有**逐字段值域、
    生命周期和"是否需要快照/是否需要清零"标记。
- 影响：
  - 未来没人知道某字段谁写、何时更新、是否需要快照、DISARM 后是否应清零。
  - 违反 HAOFV 自身「Vector Blackboard 字段必须有唯一 writer」的顶层约束。
- 处置：
  - [ ] 把 BiSS-C 配置字段拆为子结构 `trigger_vector_t.biss_cfg`，减小顶层结构体复杂度。
  - [ ] 每个字段块加统一头注释：`writer / value domain / lifecycle / snapshot-needed`。
  - [x] 把「字段值域约定」「写权限规则」两张表升级为逐字段（或逐字段块）的完整表。

### HAOFV-RISK-20260813-003 - 跨核反射内存契约未升格到 HAOFV 顶层

- 严重度：S1
- 状态：顶层已承接
- 问题：
  - `HAOFV_ARCHITECTURE.md` 只在 FreeRTOS 集成段一句"core1 是 TriggerAO owner，core0 不能
    直接改写触发域内部状态"，**没有**定义跨核 owner 矩阵、原子性屏障或门铃语义。
  - `RTOS_HAOFV_ARCHITECTURE.md` 有逐 slot 写入者的反射内存表和队列方向表，但
    `RTOS_HAOFV_TODO.md` P2 的 `core_ipc_contract`、seqlock/双缓冲、doorbell 全部未完成。
  - 代码 `distributed_refmem/` 是 core0 侧本地 64 KB 表，grep 不到 `__atomic`/`dmb`/seqlock
    的实际跨核同步实现。
- 影响：
  - core0 的 SCPI 改配置槽后，core1 如何无锁、低延迟感知变化没有契约，半新半旧读取会破坏
    TriggerVector 一致性。
- 处置：
  - [x] 在 `HAOFV_ARCHITECTURE.md` 写权限规则章节补「跨核 owner 矩阵」：明确 core0-WO/core1-RO、
    core1-WO/core0-RO 字段清单。
  - [x] 强制跨核共享字段使用 `__atomic` 或 DMB 屏障；快照用 seqlock 或双缓冲。
  - [ ] 落地 `core_ipc_contract`（mailbox、doorbell、ack、timeout、reset）。

### HAOFV-RISK-20260813-004 - XIP + Flash 擦写双核冲突

- 严重度：S0
- 状态：顶层已承接
- 问题：
  - `HAOFV_ARCHITECTURE.md` 的 XIP 安全约束只写了单核视角："USB CDC 服务不应在 Flash 临界区
    内执行"，**没有**覆盖双核场景。
  - RP2350 擦写 W25Q32 时 XIP 访问会被硬件阻塞；若 core0 擦写 Flash 时 core1 正在执行
    Trigger ISR，或 PIO/DMA 通过 XIP 读常量数组，会导致 core1 hard fault 或总线超时。
  - 目前只有 RTOS 发布门禁一句"flash erase/program 前 core1 park/lockout 可确认"，是"确认"
    不是强制机制。
- 影响：
  - 产品运行期 OTA/配置落盘时，core1 实时核可能直接崩溃。
- 处置：
  - [x] 升格为顶层硬约束：所有 Flash 擦写只能在 core0 进行，且进入 Flash 临界区前必须
    park/lockout core1。
  - [ ] Resource Arbiter 增加 `SYS_RESOURCE_FLASH_BUS` 锁，持有期间阻塞 core1 TriggerAO 调度。
  - [ ] core1 进入 `WAIT_FOR_FLASH` 状态，只保留 PIO 硬件自动运行或停止触发动作。

### HAOFV-RISK-20260813-005 - Resource Arbiter 缺资源优先级与等待队列

- 严重度：S2
- 状态：已记录待办
- 问题：
  - 文档已有资源位、典型互锁、SPI 共享仲裁、死锁检测（编译期依赖分析 + 运行时超时返回
    `RESOURCE_ACQUIRE_TIMEOUT`），但**没有**资源优先级和等待队列 FIFO。
  - 当前单核主循环串行调用各 AO，不存在抢占，仲裁死锁/优先级反转/队列饥饿不会发生；
    该风险是 RTOS + 双核阶段的**前瞻性**风险。
- 影响：
  - 切到 RTOS 后，Flash > SD > LCD 的优先级若未定义，会出现优先级反转或 OtaAO 被 TriggerAO
    抢占后申请 Flash 被阻塞。
- 处置：
  - [ ] 定义资源优先级（建议 Flash > SD > LCD）。
  - [ ] 定义资源等待队列 FIFO 与超时升级策略，供 RTOS 阶段使用。

### HAOFV-RISK-20260813-006 - OTA 允许矩阵缺集中定义

- 严重度：S2
- 状态：已记录待办
- 问题：
  - 文档已有「Trigger 域拒绝 OTA 条件」（`capture_running / sync_clock_running / trigger_armed`
    任一为真时 `SYST:OTA:BEGIN` 返回 busy），但这是**分散在 Trigger 域的被动条件判断**。
  - 缺 SystemManager 集中式「OTA 允许矩阵」，也缺专门的 OTA busy 错误码和 UI"OTA 禁止"提示。
- 影响：
  - RUN 模式下误触发 OTA 的拒绝逻辑分散，后续加资源/模式后容易漏判。
- 处置：
  - [ ] 在 SystemManager 定义集中 OTA 允许矩阵（模式 × 资源占用 → 允许/拒绝）。
  - [ ] 定义 `OTA_BUSY` 错误码和 UI 禁止提示状态。

### HAOFV-RISK-20260813-007 - 调度预算缺 overrun 回调

- 严重度：S2
- 状态：顶层已承接
- 问题：
  - 文档定义了 500/1000/2000/5000/500 μs 预算表，只说"检查是否超出预算"，**没有**定义
    预算超时回调（Budget Overrun Handler）。
  - 没有明确预算语义是"连续运行时间片"还是"绝对截止时间"。
- 影响：
  - 裸机下预算只是软约束；RTOS 下 OtaAO 超预算（如页编程 3ms）被 TriggerAO 抢占后若申请
    Flash 被阻塞，会产生优先级反转，且无上报路径。
- 处置：
  - [x] 增加 overrun 回调：超预算时向 DiagnosticsAO 报 `WATCHDOG_WARNING` 并主动 yield。
  - [x] 明确 RTOS 下预算为"连续运行时间片"，非绝对截止时间。

### HAOFV-RISK-20260813-008 - Metadata 双副本双损坏缺强制 failsafe

- 严重度：S1
- 状态：顶层已承接
- 问题：
  - 文档写"绝不擦除最后一个有效副本"，并在「Golden Image 策略」段提到 BOOTSEL+UF2 终极恢复、
    规划 Scratch 640 KB / SD `/factory/` 恢复包。
  - 但这是"规划"，不是 Bootloader 启动策略里的**强制 failsafe 路径**；两副本同时 CRC 失败时
    的兜底策略未定义。
- 影响：
  - 电源在擦写 Metadata 瞬间掉电导致双副本无效时，若 Bootloader 无恢复路径可能变砖。
- 处置：
  - [x] 在 Bootloader 启动策略章节把"双副本无效 → 进入 USB MSD 或 SD 恢复模式"定为硬要求。

### HAOFV-RISK-20260813-009 - timestamp_ms 49 天回绕

- 严重度：S1
- 状态：顶层已承接
- 问题：
  - `timestamp_ms` 用 `uint32_t`，文档自曝"约 49 天回绕"，但**没有**强制时间比较规则。
  - VDC/DPLL 相位差若直接做无符号减法，回绕会造成一次 49 天的巨大跳变。
- 影响：
  - SYNC/DPLL/T2 相关计算在回绕点出现错误跳变，破坏时间事实。
- 处置：
  - [x] 在字段值域约定中强制：时间差一律用 `int32_t diff = (int32_t)(t1 - t0)` 有符号差值。
  - [ ] 或增加 `uint64_t` 的 `epoch_seconds` 扩展位。

### HAOFV-RISK-20260813-010 - FB "不得长期阻塞"措辞偏软

- 严重度：S3
- 状态：顶层已承接
- 问题：
  - 文档说"不得长期阻塞"+ "耗时动作由 service 分步执行"+ Flash 异步 job 模型，但措辞偏软。
  - 未明确"Action 必须立即返回；耗时操作返回 `FB_RESULT_BUSY` 且 `next_state` 指向自身，
    由下一次 Tick/循环调度推进"的硬规则。
  - 当前代码未违规：grep 未在 FB/AO 路径发现 `flash_job_wait_complete()` 等阻塞等待。
- 影响：
  - 未来 OtaFB 的 ERASE_SLOT 动作若同步等待 Job 完成，会挂起整个 AO。
- 处置：
  - [x] 把"Action 必须立即返回"升格为硬规则，并给出 `FB_RESULT_BUSY + next_state=self` 的标准写法。

## 推荐处置顺序

1. **HAOFV-RISK-20260813-004**（S0）：XIP 双核冲突，先定 park/lockout 硬约束。
2. **HAOFV-RISK-20260813-003**（S1）：跨核契约升格到 HAOFV 顶层 + 原子性屏障。
3. **HAOFV-RISK-20260813-001**（S1）：先修文档失真数字，再默认规则化 ECC 表。
4. **HAOFV-RISK-20260813-002 / 008 / 009**（S1）：逐字段标注、Metadata failsafe、时间比较规则。
5. **HAOFV-RISK-20260813-005 / 006 / 007**（S2）：资源优先级、OTA 允许矩阵、预算 overrun，供 RTOS 阶段用。
6. **HAOFV-RISK-20260813-010**（S3）：FB 阻塞措辞升格。

## 维护规则

- 本文档是风险登记表快照；每条风险被处置后必须更新对应「状态」并链接到 TODO 或进度文档。
- 涉及 S0/S1 风险的架构修改，必须同步更新 `HAOFV_ARCHITECTURE.md` 或
  `RTOS_HAOFV_ARCHITECTURE.md`，不允许只在本文档记录。
- 每次风险评估必须重新核对「事实校正」表，防止再次沿用失真的 ECC/状态/事件数字。
