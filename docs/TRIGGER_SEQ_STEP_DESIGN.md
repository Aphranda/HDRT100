# SEQ_STEP — 编码序列步进触发模式

Status: Active
Domain: TRIGGER
Canonical: `docs/TRIGGER_SEQ_STEP_DESIGN.md`
Related: `docs/SYNC_IO_REFACTOR_PLAN.md`, `docs/TRIGGER_SYNC_TODO.md`, `docs/SCPI_COMMANDS.md`
Last updated: 2026-07-07

本文档按 HAOFV 架构定义 RP2350_TRIG 的第一个产品触发模式：外部触发脉冲驱动的编码序列步进输出。管理层使用表驱动模式向量，实时面由 PIO 自主闭环，CPU 零介入。

## 模式命名

> **SEQ_STEP** — 编码序列步进模式

每个 `TRIG_IN` 上升沿，`OUT[3:0]` 按预置编码表步进一位。编码表由 SCPI/UI 写入，PIO 硬实时执行。

后续模式统一命名约定：`<触发源>_<行为>`。

## HAOFV 分层

```text
SCPI / UI
  ↓ 事件投递（TriggerEvent）
TriggerAO
  ↓ 事件队列 + 执行预算
TriggerFB
  ↓ 表驱动 ECC 状态转移
TriggerVector  ←── seq_table[], seq_len, seq_idx, seq_width, mode
  ↓ 配置下发（ARM/DISARM）
PIO + DMA
  ↓ 硬件自主闭环
GPIO[23:20] ←── 编码输出
```

关键原则：

- **管理面**（AO/FB/Vector）：只在配置、ARM、DISARM 时执行。不对实时信号路径增加任何延迟。
- **实时面**（PIO/DMA）：ARM 之后 CPU 永久退出。PIO 3 条指令死循环，输入边沿→输出编码全硬件闭环。

## TriggerVector 扩展

```c
typedef enum {
    TRIG_MODE_IDLE      = 0,
    TRIG_MODE_SEQ_STEP  = 1,   // 编码序列步进
    // 预留
    TRIG_MODE_GATE_LEVEL,       // 门控电平
    TRIG_MODE_ARM_SINGLE,       // 单次 ARM 触发
    TRIG_MODE_FREE_BURST,       // 自走 burst
    TRIG_MODE_COUNT,
} trig_mode_t;

#define TRIG_SEQ_TABLE_MAX  256u
#define TRIG_SEQ_WIDTH_MAX  8u

typedef struct {
    trig_mode_t   active_mode;
    uint32_t      supported_modes;           // bitmask, bit-N = mode N supported

    // SEQ_STEP 配置
    uint32_t      seq_table[TRIG_SEQ_TABLE_MAX];
    uint32_t      seq_length;                // 1..256
    uint32_t      seq_index;                 // PIO 维护，AO 只读快照
    uint32_t      seq_output_width;          // 1..8

    // 公共运行态
    uint32_t      state;                     // IDLE/ARMED/TRIGGERED/BUSY/FAULT
    uint32_t      trigger_count;
    uint32_t      rollover_count;
    uint32_t      error_code;
} trigger_vector_t;
```

写权限：

| 字段 | 写入者 |
|---|---|
| `active_mode` | 只能 `TriggerFB` 写 |
| `seq_table` / `seq_length` / `seq_output_width` | `TriggerFB` 在 IDLE 状态下接收事件写 |
| `seq_index` | PIO/DMA 硬件维护，AO 只快照 |
| `state` | 只能 `TriggerFB` ECC 写 |
| `trigger_count` / `rollover_count` | DMA 完成中断 + TriggerFB 更新 |

## 表驱动模式向量

每个模式的特征和约束存为静态表项，进入模式时只查表，不写 if/else：

```c
typedef struct {
    trig_mode_t mode;
    uint32_t    required_resources;    // PIO + DMA 资源位
    uint32_t    allowed_states;        // 允许从哪些 AO 状态进入
    uint32_t    config_mask;           // 需要的非空配置字段
} trig_mode_entry_t;

static const trig_mode_entry_t s_mode_table[] = {
    {
        .mode               = TRIG_MODE_SEQ_STEP,
        .required_resources = SYS_RESOURCE_PIO1 | SYS_RESOURCE_DMA,
        .allowed_states     = (1u << TRIG_STATE_IDLE) | (1u << TRIG_STATE_ARMED),
        .config_mask        = TRIG_CFG_SEQ_TABLE | TRIG_CFG_SEQ_LENGTH
                             | TRIG_CFG_SEQ_WIDTH,
    },
    // 后续模式追加一行即可
};
```

## TriggerFB ECC 状态转移

### 状态定义

| 状态 | 含义 |
|---|---|
| `IDLE` | 无配置模式，或已完成 DISARM |
| `SEQ_CONFIGURED` | 已写编码表和模式，等待 ARM |
| `SEQ_ARMED` | PIO + DMA 已就绪，等待第一个触发边沿 |
| `SEQ_STEPPING` | 正在执行序列步进，PIO 硬件自主 |
| `FAULT` | 错误锁存，保留诊断快照 |

### ECC 状态转移表

| 当前状态 | 事件 | 条件 | 动作 | 下一状态 |
|---|---|---|---|---|
| `IDLE` | `CONFIG_MODE` | mode=SEQ_STEP, table+length+width 合法 | 查模式表，写 TriggerVector 配置 | `SEQ_CONFIGURED` |
| `SEQ_CONFIGURED` | `ARM` | PIO+DMA 资源可用，Trigger 未在 OTA/Flash 冲突 | 加载 PIO 程序，DMA 预填 seq_table[]，写 PIO SM 启用 | `SEQ_ARMED` |
| `SEQ_CONFIGURED` | `CONFIG_MODE` | 新 mode != SEQ_STEP | 切换模式配置 | `IDLE` |
| `SEQ_ARMED` | `TRIGGER_EDGE` | PIO 硬件判定 | PIO `out pins` 输出编码，seq_index+1 | `SEQ_ARMED` |
| `SEQ_ARMED` | `DISARM` | — | 停 PIO SM，停 DMA，发布最终 seq_index 快照 | `IDLE` |
| `SEQ_ARMED` | `ROLLOVER` | seq_index >= seq_length | 回绕策略（stop/loop），发布 rollover_count | `SEQ_ARMED` 或 `IDLE` |
| `SEQ_ARMED` | `FAULT` | 资源冲突、DMA 中断异常 | 停 PIO+DMA，锁存错误码 | `FAULT` |
| `FAULT` | `CLEAR` | 用户确认 | 清错误码，释放资源 | `IDLE` |

SEQ_STEPPING 不作为一个 AO 持续轮询态。ARM 之后 SEQ_ARMED 就是 steady state，PIO 在硬件层自主步进。AO 只在以下时机醒来：
1. 序列完成/回绕 → DMA 中断通知
2. 用户 DISARM → 事件触发
3. 资源冲突 → 资源仲裁通知

## PIO 程序

```asm
; seq_step.pio — 3 条指令，3 PIO 周期/步
; 输入: TRIG_IN (GPIO16)
; 输出: OUT[3:0] → GPIO[23:20]
; 配置: out_shift=right, autopull=ON, pull_threshold=seq_width

.program seq_step
.wrap_target
    wait 0 pin, TRIG_IN        ; ① 等待低电平（去抖依靠外部斯密特）
    wait 1 pin, TRIG_IN        ; ② 等待上升沿 → 触发
    out pins, 4                ; ③ 输出当前编码，auto-pull 自动补字
.wrap                            ; 回到 ①
```

- `wait 0/1` 保证对完整上升沿响应，不误触发毛刺后的残留高电平
- `autopull=ON, pull_threshold=seq_width`：每次步进输出后自动从 TX FIFO 拉下一个 32-bit 表项
- 当前实现为 1 个 32-bit 表项对应 1 次触发步进，表项低 `seq_width` bit 作为输出编码
- DMA 在 FIFO 快空时自动补填，连续模式由 DMA 重装载/回绕策略保证

## 时序

| 参数 | 值 | 公式 |
|---|---|---|
| 最小触发周期 | ~20 ns | 3 × PIO 周期 @150 MHz |
| 最高触发速率 | ~50 MHz | clk_sys / 3 |
| 输入→输出延迟 | ~20 ns | 3 PIO 周期 |
| 编码位宽 | 1–8 bit | `pull_threshold` 可配 |
| 序列步数 | 1–256 | `TRIG_SEQ_TABLE_MAX` |
| CPU 占用（ARM 后） | 零 | PIO 自主 |

## 资源占用

| 资源 | 占用 | 说明 |
|---|---|---|
| PIO | pio1/sm0 | GPIO20-23 编码输出，3 条指令 |
| DMA | 1 通道 | SRAM seq_table[] → PIO TX FIFO |
| IRQ | PIO1 IRQ0 | 回绕/完成通知（可选，走 DMA 中断亦可） |
| GPIO | GPIO16(IN) + GPIO20-23(OUT) | 4-bit 编码输出 |
| CPU | **零** | ARM 后不需要 CPU 介入 |

## SCPI 命令

```text
TRIG:MODE SEQ_STEP              # 设置模式
TRIG:MODE?                      # 查询当前模式 "SEQ_STEP",1

TRIG:SEQ:TABL <w0>,...,<wN>     # 写入编码表（十进制，空格/逗号分隔）
TRIG:SEQ:TABL?                  # 回读编码表
TRIG:SEQ:LENG <1..256>          # 序列长度
TRIG:SEQ:LENG?                  # 查询序列长度
TRIG:SEQ:WIDT <1..8>            # 编码位宽
TRIG:SEQ:WIDT?                  # 查询位宽
TRIG:SEQ:INDE?                  # 查询当前步进索引（PIO 快照）

TRIG:ARM                        # 加载 PIO + DMA，进入 ARMED
TRIG:DISA                        # 停 PIO/DMA，回 IDLE

STAT:TRIG?                      # 触发域摘要快照
```

规则：

- `TRIG:SEQ:TABL` 只在 IDLE/SEQ_CONFIGURED 状态接受；ARM 后拒绝。
- 查询类命令读 TriggerVector 快照，不触碰 PIO。
- `TRIG:ARM` 投递事件 → TriggerAO → TriggerFB → 查模式表 → 配置 PIO + DMA。

## 实现步骤

### Step 1 — PIO 程序 + sync_io SEQ_STEP 原语

- 新增 `components/sync_io/src/seq_step.pio`
- `sync_io` 增加 `sync_io_seq_step_arm(seq_table, seq_length, seq_width)` 和 `sync_io_seq_step_disarm()`
- PIO 加载 + DMA 配置 + SM 启用封装在一处

### Step 2 — TriggerVector + 模式表

- 新增 `components/sync_trigger/inc/trigger_vector.h`
- 定义 `trigger_vector_t`、`trig_mode_t`、`trig_mode_entry_t`
- 静态模式表 `s_mode_table[]`

### Step 3 — TriggerFB ECC 状态转移表

- 新增 `components/sync_trigger/src/trigger_fb.c`
- 实现 `trigger_fb_execute(context, event)` — 查 ECC 表
- `CONFIG_MODE` / `ARM` / `DISARM` / `TRIGGER_EDGE` / `ROLLOVER` / `FAULT` / `CLEAR`

### Step 4 — TriggerAO

- 改造 `sync_trigger` 为 TriggerAO
- `trigger_ao_init()` / `trigger_ao_post_event()` / `trigger_ao_service()`
- 内部持有 `trigger_vector_t` 快照和事件队列

### Step 5 — DMA 回绕/完成中断

- 序列结束时 DMA 中断通知 TriggerAO
- TriggerAO 投递 `ROLLOVER` 事件给 TriggerFB

### Step 6 — SCPI 命令接入

- `TRIG:MODE` / `TRIG:SEQ:*` / `TRIG:ARM` / `TRIG:DISA` / `STAT:TRIG?`
- 直接写 TriggerVector 命令槽或投递事件

### Step 7 — 兼容性

- 保留现有 `TRIG:IMM` / `TRIG:WIDT` 等即时脉冲命令
- 即时脉冲命令在 SEQ_ARMED 状态下返回 BUSY
- baremetal 和 RTOS 双路径均走同一 TriggerAO service

## 与现有 sync_io 的关系

`sync_io` 保持底层 I/O 原语定位。`sync_trigger`（业务层）不直接操作 PIO 寄存器：

```text
TriggerFB
  ↓
sync_trigger (TriggerAO 内部)
  ↓
sync_io_seq_step_arm() / sync_io_seq_step_disarm()
  ↓
PIO / DMA / GPIO
```

`sync_io` 新增的 `seq_step` 函数是**无状态的原语**——传参数、配置硬件、返回结果。状态、仲裁、模式切换由高层负责。
