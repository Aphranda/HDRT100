# ENC_COUNT — 编码器计数触发模式

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/TRIGGER_ENC_COUNT_DESIGN.md`
Related: `docs/sync/SYNC_IO_REFACTOR_PLAN.md`, `docs/trigger/TRIGGER_SYNC_TODO.md`, `docs/SCPI_COMMANDS.md`
Last updated: 2026-07-07

按 HAOFV 架构设计的第二种触发模式。接收电机编码器 A/B/Z 信号，硬件计数到目标值后输出触发脉冲。

## 信号模型

```
     ┌───┐   ┌───┐   ┌───┐   ┌───┐
A ───┘   └───┘   └───┘   └───┘   └──  ← 编码器 A 相
       ┌───┐   ┌───┐   ┌───┐   ┌───┐
B ─────┘   └───┘   └───┘   └───┘   └  ← 编码器 B 相 (滞后 A 90° = CW)
              ┌─┐
Z ────────────┘ └────────────────────  ← 索引脉冲 (每圈一次, 位置归零)

CW  (A 超前 B):  A↑时 B=0  →  计数 +1  →  X-- (趋近目标)
CCW (B 超前 A):  A↑时 B=1  →  忽略 (Phase 1)
```

## 模式定义

- **名称**: `ENC_COUNT` (mode = 2)
- **解码**: 1x (A 上升沿), 仅 CW 方向计数
- **Z 行为**: 高电平 → 重新从 TX FIFO 拉取目标值 (位置归零)
- **输出**: 计数器归零时 `TRIG_OUT` 输出固定宽度脉冲

## HAOFV 分层

```text
SCPI / UI
  ↓ TRIG_EVENT_CONFIGURE_ENC / TRIG_EVENT_ARM / TRIG_EVENT_DISARM
TriggerAO
  ↓ 事件队列 + 周期服务
TriggerFB
  ↓ 表驱动 ECC 状态转移
TriggerVector  ←── enc_target, enc_pins, enc_count, mode=ENC_COUNT
  ↓ ARM 时配置下发
PIO (enc_count) + DMA
  ↓ 硬件自主: A 边沿检测 → B 方向判断 → X-- → Z 复位 → 触发脉冲
GPIO[20]
```

管理面 (AO/FB/Vector) 只在 CONFIGURE / ARM / DISARM 时执行。
实时面 (PIO) ARM 后零 CPU 介入。

## TriggerVector 扩展

```c
typedef enum {
    TRIG_MODE_IDLE      = 0,
    TRIG_MODE_SEQ_STEP  = 1,
    TRIG_MODE_ENC_COUNT = 2,
    TRIG_MODE_COUNT,
} trig_mode_t;

typedef enum {
    TRIG_ENC_DECODE_1X = 0,   /* A 上升沿, Phase 1 */
    TRIG_ENC_DECODE_2X,       /* A 上升+下降 (预留) */
    TRIG_ENC_DECODE_4X,       /* A/B 全部边沿 (预留) */
} trig_enc_decode_t;

typedef struct {
    /* ... 公共字段 ... */

    /* ENC_COUNT 配置 */
    uint32_t       enc_target;         /* 目标计数值 */
    uint32_t       enc_count;          /* 当前计数值 (PIO 维护, AO 只读) */
    trig_enc_decode_t enc_decode;      /* 解码模式 */
    bool           enc_z_reset;        /* Z 脉冲复位使能 */

    /* 引脚配置 (默认: A=16, B=17, Z=18) */
    uint32_t       enc_a_pin;
    uint32_t       enc_b_pin;
    uint32_t       enc_z_pin;
} trigger_vector_t;
```

写权限:

| 字段 | 写入者 |
|---|---|
| `enc_target`, `enc_decode`, `enc_z_reset` | TriggerFB (IDLE/ENC_CONFIGURED) |
| `enc_a/b/z_pin` | TriggerFB (IDLE 状态) |
| `enc_count` | PIO 硬件更新, AO 周期快照 |
| `state` | TriggerFB ECC |

## ECC 状态

| 状态 | 含义 |
|---|---|
| `IDLE` | 默认 |
| `ENC_CONFIGURED` | 已配置编码器参数, 等待 ARM |
| `ENC_ARMED` | PIO 运行中, 硬件自主计数 |
| `FAULT` | 错误锁存 |

### ECC 转移表

| 当前 | 事件 | 条件 | 动作 | 下一状态 |
|---|---|---|---|---|
| IDLE | CONFIGURE_ENC | enc_target>0, pins 合法 | 写 TriggerVector | ENC_CONFIGURED |
| IDLE | CONFIGURE_ENC | 参数非法 | 设 error_code | IDLE (拒绝) |
| ENC_CONFIGURED | ARM | PIO+DMA 可用 | 加载 enc_count PIO, DMA 填目标值 → TX FIFO | ENC_ARMED |
| ENC_CONFIGURED | DISARM | — | 清配置, 释放资源 | IDLE |
| ENC_ARMED | DISARM | — | 停 PIO+DMA, 释放资源 | IDLE |
| ENC_ARMED | DMA_ROLLOVER | 触发计数已产生 | 更新 enc_count, trigger_count | ENC_ARMED |
| ENC_ARMED | FAULT | PIO 异常停止 | 停 PIO, 锁存错误 | FAULT |
| FAULT | CLEAR | — | 释放资源 | IDLE |

## PIO 程序

```asm
; enc_count.pio — 24 指令, 1 SM
; 每步 ~13 PIO 周期 ≈ 87 ns → 最高计数速率 ~11.5 MHz
;
; 寄存器:
;   X = 剩余计数 (递减, 归零 = 触发)
;   Y = 临时位提取
;   OSR/ISR = A/B/Z 采样与移位提取

.program enc_count
    pull                   ; 首字 = 目标值
    mov x, osr

.wrap_target
    wait 0 pin, 0         
    wait 1 pin, 0          ; A 上升沿

    in pins, 3             ; ISR = Z,B,A
    mov osr, isr

    out null, 1            ; 移出 A
    out y, 1               ; y = B
    jmp y--, no_count      ; B=1 → CCW, 跳过

    jmp x--, check_z       ; CW: X--, 非零继续
    jmp fire               ; X==0 → 触发!

no_count:
    jmp check_z_entry

check_z:
    mov osr, isr
check_z_entry:
    out null, 2            ; 移出 A,B
    out y, 1               ; y = Z
    jmp y--, wrap          ; Z=0 → 继续
    pull noblock           ; Z=1 → 重载目标值
    mov x, osr
.wrap

fire:
    set pins, 1            ; TRIG_OUT = 1
    set y, 9               ; 10 周期 ≈ 67ns 脉冲
fire_wait:
    jmp y--, fire_wait
    set pins, 0            ; TRIG_OUT = 0
    pull                   ; 下一目标值
    mov x, osr
%}
```

## 时序

| 参数 | 值 |
|---|---|
| 最高编码器计数速率 | ~11.5 MHz (> 任何工业编码器) |
| 每步延迟 | ~87 ns |
| A 上升沿 → TRIG_OUT | ~107 ns (无 Z 检查) |
| 输出脉冲宽度 | ~67 ns (10 PIO 周期, 固定) |

## 资源占用

| 资源 | 占用 |
|---|---|
| PIO | pio1/sm0 (可与 SEQ_STEP 共享, 互斥使用) |
| DMA | 1 通道: SRAM[target_count] → PIO TX FIFO |
| GPIO | A(16), B(17), Z(18), TRIG_OUT(20) |
| CPU | **零** (ARM 后) |

## SCPI 命令

```text
TRIGger:MODE ENC_COUNT          ; mode=2
TRIGger:ENC:TARGet <N>          ; 目标计数值
TRIGger:ENC:TARGet?
TRIGger:ENC:COUNt?              ; 当前计数值 (只读快照)
TRIGger:ENC:APIN <16>           ; A 相 GPIO，固定派生 A/B/Z=16/17/18
TRIGger:ENC:BPIN?               ; B 相 GPIO，当前固定 17
TRIGger:ENC:ZPIN?               ; Z 相 GPIO，当前固定 18
TRIGger:ENC:ZPIN?
TRIGger:ENC:ZRESet <ON|OFF>     ; Z 复位使能
TRIGger:ENC:ZRESet?
TRIGger:ARM
TRIGger:DISarm
STATus:TRIGger?                 ; 含 enc_target, enc_count 字段
```

## 实现步骤

1. PIO + sync_io 原语 (`enc_count.pio`, `sync_io_enc_count_arm/disarm`)
2. TriggerVector 扩展 + TriggerFB ECC 表新增 ENC_COUNT 行
3. SCPI 命令
4. 板端验证 (需要信号发生器模拟 A/B/Z)
