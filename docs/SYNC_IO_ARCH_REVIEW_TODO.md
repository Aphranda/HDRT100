# SYNC_IO 架构评审待办

Status: Active
Domain: SYNC_IO
Canonical: `docs/SYNC_IO_ARCH_REVIEW_TODO.md`
Related: `docs/SYNC_IO_REFACTOR_PLAN.md`, `docs/TRIGGER_SYNC_TODO.md`, `docs/LOG_SYSTEM_TODO.md`
Last updated: 2026-07-07

本文档跟踪 2026-07-07 SYNC 架构评审中发现的问题。评审结论：架构方向正确——五层分离
（hw_profile → mode → core I/O → TriggerFB → AO）、表驱动状态机、owner-based 资源仲裁、
分层 trace/LOG 都已在位。核心问题是重构卡在 P0→P1 过渡点：mode wrapper 骨架已就位，
但实际 PIO/DMA/IRQ 逻辑尚未从 `sync_io.c` 单体中搬迁出来。

HAOFV 约束：TriggerAO 只负责事件队列和服务节拍；TriggerFB 负责 ECC 状态、配置向量、
资源 owner 和错误码；mode driver 负责表驱动 validate/arm/disarm/is_running；sync_io core
只提供公共 PIO/GPIO/DMA 原语和 trace helper。资源申请必须有单一 owner 边界，不能在
TriggerFB 和底层 `sync_io_*_arm()` 中重复 acquire。

## 验收标准摘要

| 优先级 | 验收标准 |
|---|---|
| P0 | ECC reset/fault 语义一致；BiSS timeout/sample-scan 可恢复且有 trace；资源 owner 边界唯一且表驱动；所有物理 mode arm 统一通过 mode ops/dispatcher。 |
| P1 | RJ45 trigger 语义独立于 marker；胶水函数宏化消除重复；SM/IRQ 共享关系显式记录；未实现 mode 在查询接口中正确过滤。 |
| P2 | 双核验证通过；闭环自检脚本覆盖全 mode；长期兼容命令返回稳定错误码。 |

---

## 推荐执行顺序

| 顺序 | 待办 | 原因 |
|---|---|---|
| 1 | P0-00 reset/fault 语义 | 先保证同一事件在 ECC 表中语义一致，避免后续拆分时复制错误 action。 |
| 2 | P0-01 BiSS timeout/sample scan | 这是当前 BiSS 运行闭环的真实行为风险，优先于文件层级整理。 |
| 3 | P0-02 resource owner 边界 | 先固定资源归属规则，再移动 mode arm 实现，避免双重 acquire。 |
| 4 | P0-03 BiSS TAP 物理 arm 边界 | BiSS 已涉及 AUX RX/TX、timeout、sample scan，最能检验 HAOFV 分层。 |
| 5 | P0-04 sync_io.c 拆分 | 在行为和 owner 边界稳定后，再搬迁 SEQ/ENC/BISS 实现。 |
| 6 | P1 维护性改进 | RJ45/marker、SM/IRQ 表、胶水函数、预留 mode 查询等可在 P0 行为闭环后处理。 |

---

## P0 架构正确性（阻塞后续重构）

### P0-00 收敛 TriggerFB ECC 的 reset/fault 语义

**现状：** `TRIG_EVENT_RESET` 在不同 armed 状态下映射到不同 handler：
`IDLE/CONFIGURED` 走 `fb_instant_cmd()`，会停止 clock/capture 并统一释放运行 IO；
`SEQ_ARMED/ENC_ARMED/BISS_ARMED` 则分别走 mode-specific disarm handler。BiSS armed reset
当前只释放 BiSS TAP，不执行通用 reset 的 clock/capture 停止语义。

**影响：** 同一事件在不同状态下语义不一致，违反 HAOFV ECC 表“事件语义稳定、action 可复用”
的约束。后续新增 mode 时容易继续复制这种分叉。

- [ ] 增加 `fb_reset_all()` action：停止 clock/capture，释放所有 mode owner，清理运行态，再回到 IDLE
- [ ] 所有状态下 `TRIG_EVENT_RESET` 统一映射到 `fb_reset_all()`，除非文档明确说明该状态只允许局部 reset
- [ ] `TRIG_EVENT_FAULT` 和 `TRIG_EVENT_CLEAR_FAULT` 复用同一套底层释放 helper，避免资源锁残留
- [ ] 为 reset/fault release 记录低频 trace，字段包含 before_state、active_resources、released mask

### P0-01 修正 BiSS runtime timeout/sample scan 闭环

**现状：** BiSS armed 已进入 `TRIG_EVENT_RUNTIME_SAMPLE`，但 timeout/sample scan 仍在
`biss_node_io_check_timeout()` 内直接重新 arm TAP。该 re-arm 结果当前未上报；同时
`timeout_latched` 会阻止无帧场景下连续扫描多个 sample delay。

**影响：** BiSS sample scan 的状态推进、失败恢复和 trace 证据仍不完整。现场调试时可能看到
`sample_scan_index` 推进，但物理 TAP 未按新 delay 成功重启。

- [ ] 将 sample-scan 步进建模为 TriggerFB 管理面 action，而不是在 helper 内静默重 arm
- [ ] re-arm 失败时进入稳定错误路径：设置 `TRIG_ERROR_IO_ARM_FAILED` 或 mode-specific 错误码，
  记录 fault/resource/runtime trace
- [ ] 明确 timeout latch 策略：scan 模式下允许按配置周期继续推进 delay；非 scan 模式下只锁存一次 timeout
- [ ] 补齐 BiSS timeout/sample-scan trace 解码和板端验证断言

### P0-02 收口资源 owner 边界

**现状：** Mode wrapper 声明了 `.resources` 位掩码，但 `sync_io_mode.c` 目前只做 ops 查询，
资源冲突检测由 TriggerFB action 直接调用 `resource_arbiter_acquire_owned()` 完成。如果未来
再在 `sync_io_*_arm()` 中直接 acquire，会与 TriggerFB 已持有的 owner 发生自冲突。

**影响：** HAOFV 要求资源仲裁统一，但不是多层重复仲裁。当前缺少一个明确的 mode resource
调度边界，导致 `.resources` 字段没有成为执行规则。

- [ ] 明确单一资源 owner 边界：P0 继续由 TriggerFB action acquire/release；P1 再评估是否迁移到
  `sync_io_mode_arm_with_owner(id, config, owner)` 统一 dispatcher
- [ ] 不在裸 `sync_io_*_arm()` 中直接重复 acquire；如需纵深防御，增加只读 owned/assert 检查和
  trace，不改变资源持有关系
- [ ] 将 `sync_io_mode_ops_t.resources` 与 `resource_arbiter_resource_t` 建立表驱动映射，避免
  TriggerFB 手写 `FB_SEQ_RESOURCES/fb_biss_resources()` 长期漂移
- [ ] 冲突时由 FB action 返回稳定错误码，并记录 resource snapshot trace

### P0-03 统一 BiSS TAP 的物理 ARM 边界

**现状：**
- `sync_io_biss_tap_arm()` 在 `sync_io.c` — 直接操作 PIO，仍是物理 TAP arm 实现
- `biss_node_io_arm()` 在 `biss_node_io.c` — 协议层 profile/assembler/timeout 编排入口
- `biss_node_io_arm()` 已通过 `sync_io_mode_get_ops(SYNC_IO_MODE_ID_BISS_TAP)` 调用
  `sync_io_biss_tap_mode_arm()`，但协议编排与物理 mode 实现仍未分层收口

**影响：** 当前不是完全绕过 mode wrapper，而是 BiSS 协议 helper 与 mode driver 的职责仍然混在
迁移中间态。HAOFV 下 `biss_node_io` 可以作为 TriggerFB/协议域 helper，但不能拥有 PIO/DMA
物理实现；物理 TAP arm/disarm 必须归 mode driver。

- [ ] 保持 TriggerFB 只处理 BISS_CONFIGURED/BISS_ARMED ECC、资源 owner 和错误码
- [ ] `biss_node_io` 只保留 profile validate、frame assembler、timeout/sample scan 运行态逻辑
- [ ] `sync_io_mode_biss_tap.c` 成为唯一物理 TAP arm/disarm/is_running 实现入口
- [ ] 消除物理 pin/profile validate 重复：硬件 pinout 校验统一在 mode validate，协议 profile
  校验统一在 `biss_profile_validate()`

### P0-04 拆分 sync_io.c 单体

**现状：** `components/sync_io/src/sync_io.c` 约 1500 行，包含 PIO 初始化、capture、clock、
pulse、AUX 管理、BiSS TAP、SEQ_STEP、ENC_COUNT 的全部实现。Mode wrapper 文件
（`sync_io_mode_enc_count.c` 72 行、`sync_io_mode_seq_step.c` 85 行、
`sync_io_mode_biss_tap.c` 66 行）只是薄壳——validate 后转发调用仍在 `sync_io.c` 中的函数。

**影响：** mode driver 抽象是 leaky 的。真正的 mode 逻辑不在 mode driver 文件里，
新增 mode 或修改 mode 行为仍然需要在单体文件中定位代码。

**对应 Plan：** `SYNC_IO_REFACTOR_PLAN.md` P1-2（SEQ_STEP 拆分）、P1-3（BISS_TAP 拆分）

- [ ] 将 `sync_io_biss_tap_arm()` / `sync_io_biss_tap_disarm()` 及 BiSS TAP 全部静态函数搬迁到
  `sync_io_mode_biss_tap.c`
- [ ] 将 `sync_io_seq_step_arm()` / `sync_io_seq_step_disarm()` 及 SEQ_STEP 全部静态函数和
  `sync_io_seq_step_t` 结构体搬迁到 `sync_io_mode_seq_step.c`
- [ ] 将 `sync_io_enc_count_arm()` / `sync_io_enc_count_disarm()` 及 ENC_COUNT 全部静态函数和
  `sync_io_enc_count_t` 结构体搬迁到 `sync_io_mode_enc_count.c`
- [ ] `sync_io.c` 只保留公共基础设施：PIO 程序加载、capture、clock、pulse、AUX 读写、
  trace helper、`sync_io_context_t`
- [ ] 搬迁后 `sync_io.c` 行数显著下降；`< 600 行` 作为 P1/P2 清理目标，不作为阻塞 P0
  的唯一验收条件

---

## P1 架构改进（提升可维护性和一致性）

### P1-01 SEQ_STEP 和 ENC_COUNT 共享 PIO SM / DMA IRQ 需显式记录

**现状：** 两个 mode 都使用 `BOARD_SYNC_OUTPUT_SM`（同一个 PIO state machine），
共享 DMA IRQ handler `sync_io_seq_step_dma_handler()`。ISR 内部通过 `running` 标志区分。

**影响：** 虽然运行时互斥（resource_arbiter 保证），但如果将来需要并发
（例如 ENC_COUNT 在 core1 运行），这种共享会直接崩溃。

- [ ] 在静态 mode resource 表中显式记录 PIO instance、SM、DMA channel、IRQ 的互斥关系，
  由表驱动生成/校验 `resource_arbiter` mask
- [ ] 评估是否可以为 ENC_COUNT 分配独立的 SM（如果硬件资源允许）
- [ ] 如不能独立 SM，至少在 ISR 入口增加 `assert(!(s_seq_step.running && s_enc.running))`

### P1-02 RJ45 trigger 输出语义独立于 marker

**现状：** `sync_io_fire_rj45_trigger_us()` 与 `sync_io_fire_marker_us()` 实现完全相同，
都使用 `BOARD_SYNC_MARKER_SM`。只是别名，未独立。

**对应 Plan：** `SYNC_IO_REFACTOR_PLAN.md` P1-4

- [ ] 给 RJ45 trigger 分配独立的 PIO SM（如果硬件资源允许）和独立语义名
- [ ] 如果不能独立 SM，在文档和注释中明确"RJ45 trigger 复用 marker SM"的约束
- [ ] 更新 SCPI `MARK:*` 命令文档，说明与 RJ45 trigger 的关系

### P1-03 mode wrapper 的 void* 胶水函数宏化

**现状：** 每个 mode wrapper 需要一对 `_validate_void` / `_arm_void` 胶水函数做
`const void* → const typed_config*` 转换。三个 mode × 2 = 6 个几乎相同的函数。

- [ ] 用宏消除胶水函数重复：
  ```c
  #define SYNC_IO_MODE_VOID_DISPATCH(prefix, config_type) \
    static bool prefix##_validate_void(const void *c) { \
      return prefix##_validate((const config_type *)c); \
    } \
    static bool prefix##_arm_void(const void *c) { \
      return prefix##_arm((const config_type *)c); \
    }
  ```

### P1-04 未实现 mode 的查询接口行为明确

**现状：** `sync_io_mode_id_t` 枚举中声明了 `SYNC_IO_MODE_ID_AUX_DIFF_TRIGGER` 和
`SYNC_IO_MODE_ID_SELF_CAL`，但 `sync_io_mode_get_ops()` 返回 NULL，
`sync_io_mode_get_by_index()` 也不包含它们。当前未看到 SCPI mode-list 查询直接暴露该表；
新增查询接口前必须先定义 NULL/预留 mode 的过滤行为。

- [ ] 在 `sync_io_mode_get_ops()` 中为未实现 mode 显式返回 NULL（当前已是，确认调用方处理 NULL）
- [ ] 如新增 SCPI/UI mode 列表查询，必须过滤掉 NULL ops 的 mode，并用表驱动名称返回已实现 mode
- [ ] 在 `sync_io_mode.h` 注释中标注预留项状态

### P1-05 TRIG_MODE_BISS_BRIDGE 别名语义混淆

**现状：** `trigger_vector.h` 中 `TRIG_MODE_BISS_BRIDGE = TRIG_MODE_PROTOCOL_TRIGGER`，
将"协议触发"和"BiSS 桥接"两个不同概念用同一个枚举值表示。

- [ ] 评估是否可以用独立的 mode 值，或将 BRIDGE 作为 PROTOCOL_TRIGGER 的子角色
- [ ] 如果必须保留别名，在 SCPI 查询中返回一致的名称

---

## P2 长期改进（双核、自检、兼容层）

**对应 Plan：** `SYNC_IO_REFACTOR_PLAN.md` P2

### P2-01 双核 / RTOS 调度边界

- [ ] 实时 mode driver 可放到 core1 或高优先级任务
- [ ] SCPI/UI/日志保留在管理侧
- [ ] 单核路径与双核路径使用同一 mode driver 表

### P2-02 模式级自检

- [ ] PIO FIFO 状态、DMA restart、IRQ 活性、丢帧计数统一查询接口
- [ ] 板端 loopback 或外部回放闭环验证

### P2-03 长期兼容层

- [ ] 保留历史 SCPI 查询命令
- [ ] 对已关闭能力返回稳定错误码，而不是静默忽略
