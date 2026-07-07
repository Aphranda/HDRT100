# 工业级脉冲计数 — 对标分析与增强方案

Status: Active
Domain: TRIGGER
Canonical: `docs/TRIGGER_PULSE_COUNT_ANALYSIS.md`
Related: `docs/TRIGGER_ENC_COUNT_MODE.md`, `docs/SYNC_TRIGGER_TODO.md`, `docs/PIO_RESOURCE_PLAN.md`
Last updated: 2026-07-07

对标 7 个开源脉冲计数实现，分析当前 ENC_COUNT 与工业级系统的差距，给出 HAOFV 对齐的增强路线。

## 对标项目

| 项目 | 平台 | 关键特性 | 参考价值 |
|---|---|---|---|
| [ESP32 PCNT](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/pcnt.html) | ESP32 | 硬件 40MHz, 16bit×8 通道, 正交解码, 5 阈值比较, 滤波器 | 工业级功能基准 |
| [madhephaestus/ESP32Encoder](https://github.com/madhephaestus/ESP32Encoder) | ESP32 | 半/全正交封装, 8 编码器并行, 16→64bit 溢出扩展 | 软件架构参考 |
| [ornotermes/rpFreqCounter](https://github.com/ornotermes/rpFreqCounter) | RP2040 | 双 SM 频闪/计数, SSD1306 显示, MicroPython | 频率测量模式 |
| [Cirromulus/pulsecounter](https://github.com/Cirromulus/tuning-fork-clock) | RP2040 | read-request 机制, GPSDO 校准, 50MHz 上限 | DMA 优化 |
| [bikeNomad/smartStepper](https://github.com/bikeNomad/micropython-rp2-smartStepper) | RP2040/2350 | SM 输出 + SM 计数同步, 位置闭环 | 编码器闭环参考 |
| Reciprocal Counter (社区) | RP2040 | 3 SM: 门控 + 时钟计数 + 脉冲计数, 互为倒数 | **最精密频率测量** |
| [ufnalski/encoder_timing](https://github.com/ufnalski/incremental_encoder_pulse_timing_l432kc) | STM32 | 脉冲间隔计时, 速度计算, HAL 封装 | 脉冲间隔测量 |

## 当前 ENC_COUNT 差距

### 解码模式

| 特性 | ESP32 PCNT | ENC_COUNT 当前 | 差距 |
|---|---|---|---|
| 1x 解码 (A↑ CW) | ✅ | ✅ | — |
| 1x 解码 (CCW) | ✅ | ❌ | P0 |
| 2x 解码 (A↑↓) | ✅ | ❌ | P1 |
| 4x 解码 (A↑↓ + B↑↓) | ✅ | ❌ | P1 |
| 单脉冲模式 (非正交) | ✅ CH0 only | ❌ | P0 |
| 上下计数 (独立 U/D 引脚) | ✅ `pcnt_set_mode` | ❌ | P1 |

### 计数控制

| 特性 | ESP32 PCNT | ENC_COUNT | 差距 |
|---|---|---|---|
| 计数方向选择 | CH0/CH1 信号自动 | CW only | P0 |
| 外部使能/门控 | `ctrl_gpio` | ❌ | P0 |
| 软复位 | `pcnt_counter_clear` | ❌ | P0 |
| 暂停/恢复 | `pcnt_counter_pause/resume` | ❌ | P1 |
| 软件递增/递减 | ✅ | ❌ | P2 |
| Z 信号圈数累计 | ✅ 中断 | ✅ PIO IRQ→CPU | — |

### 阈值与事件

| 特性 | ESP32 PCNT | ENC_COUNT | 差距 |
|---|---|---|---|
| 目标值比较 | 5 个独立阈值 | 1 个 (target) | P0 |
| 比较动作 (清零/停止/中断) | 每个阈值可独立配置 | 仅触发脉冲 | P0 |
| 上溢/下溢检测 | ✅ 16bit | ✅ 32bit (天然) | — |
| 阈值 ± 滞回 | ❌ | ❌ | P2 |
| 频率/速度测量 | ❌ (需软件) | ❌ | P0 |

### 信号质量

| 特性 | ESP32 PCNT | ENC_COUNT | 差距 |
|---|---|---|---|
| 数字滤波器 | ✅ 0-1023×12.5ns 窗口 | ❌ | P0 |
| 毛刺抑制 | 滤波器实现 | ❌ (仅 wait 0→1) | P0 |
| 信号极性反转 | ❌ | ❌ | P2 |
| 超时检测 (无脉冲) | ❌ | ❌ | P1 |

### 统计与诊断

| 特性 | smartStepper | ENC_COUNT | 差距 |
|---|---|---|---|
| 脉冲计数 | ✅ | ✅ enc_count | — |
| 频率 (Hz) | ✅ 速度计算 | ❌ | P0 |
| 圈数/累计 | ✅ | ✅ enc_rev_count | — |
| 时间戳记录 | ❌ | ❌ | P1 |
| DMA 溢出计数 | ✅ | ❌ | P1 |
| 每脉冲间隔 (µs) | ufnalski 项目 | ❌ | P2 |

## 工业脉冲计数器功能模型

参照 PLC 高速计数器 (HSC) 和伺服驱动器功能：

```
┌──────────────────────────────────────────────────────┐
│                    脉冲计数器                          │
├──────────────┬───────────────────────────────────────┤
│ 输入信号     │ A/B 正交, U/D 方向, P/D 脉冲+方向       │
│ 解码模式     │ 1x/2x/4x 正交, 单脉冲, 上下计数          │
│ 滤波器       │ 可编程数字滤波 (ns 级窗口)              │
│ 门控         │ 外部 HW GATE / 软件 SW GATE / 电平/LATCH│
│ 计数控制     │ 方向 / 复位 / 暂停 / 预设值加载          │
├──────────────┼───────────────────────────────────────┤
│ 比较器 0     │ 阈值0: CV==TV → 动作(脉冲/停止/中断)    │
│ 比较器 1     │ 阈值1: CV>=TV → 动作                    │
│ 上溢/下溢    │ 计数器绕回事件                           │
│ 零位/圈数    │ Z 标记 + rev_counter                    │
├──────────────┼───────────────────────────────────────┤
│ 频率测量     │ 门控时间内的脉冲数 / 每脉冲间隔法         │
│ 速度计算     │ 频率 / PPR = RPM                        │
│ 总累计       │ 上电以来总脉冲数 (独立于 CV)             │
├──────────────┼───────────────────────────────────────┤
│ 时间戳       │ 每脉冲到达时刻, 环形缓冲                 │
│ 间隔分析     │ min/max/avg/stddev 脉冲间隔              │
│ 缺失检测     │ 超过 T 无脉冲 → timeout event           │
├──────────────┼───────────────────────────────────────┤
│ 故障锁存     │ 滤波器溢出, 频率超限, 方向反转           │
│ 状态快照     │ cv, rev, freq, total, error, flags     │
└──────────────┴───────────────────────────────────────┘
```

## HAOFV 架构对齐

```text
管理面 (SCPI/UI → TriggerAO → TriggerFB → TriggerVector)
  配置:   模式/解码/滤波/门控/比较器/方向/通道
  统计:   频率/累计/间隔/超时/故障
  控制:   软复位/暂停/预设加载/阈值写入

实时面 (PIO/DMA)
  PIO SM0: 编码器 A/B/Z 解码 + 滤波 + 方向判定 + 计数
  PIO SM1: 阈值比较 + 脉冲输出 + 时间戳触发
  DMA:     时间戳环形缓冲 → SRAM
  定时器:   频率测量门控周期
```

### TriggerVector 扩展

```c
typedef struct {
    /* ── 输入配置 ── */
    uint32_t   ch_a_pin;          /* A / UP 引脚 (默认 16) */
    uint32_t   ch_b_pin;          /* B / DOWN 引脚 (默认 17) */
    uint32_t   ch_z_pin;          /* Z 引脚 (默认 19, 0=禁用) */
    uint32_t   ch_gate_pin;       /* 门控引脚 (0=禁用) */
    trig_pcnt_decode_t decode;    /* 解码: SINGLE/QUAD_1X/2X/4X/UP_DOWN/PULSE_DIR */
    uint32_t   filter_ns;         /* 数字滤波窗口 (ns), 0=禁用 */
    trig_edge_t edge;             /* 单脉冲模式边沿选择 */

    /* ── 计数控制 ── */
    trig_pcnt_mode_t mode;        /* 模式: CONTINUOUS/SINGLE_CYCLE/RANGE/BATCH */
    trig_pcnt_dir_t  dir;         /* 方向: CW/CCW/BOTH/EXTERNAL */
    uint32_t   preset_value;      /* 预设值 (复位后加载) */
    bool       reset_on_z;        /* Z 脉冲复位? */
    bool       gate_invert;       /* 门控反相 */

    /* ── 比较器 ── */
    uint32_t   cmp0_value;        /* 比较器0 阈值 */
    trig_action_t cmp0_action;    /* PULSE/STOP/IRQ/CLEAR */
    uint32_t   cmp0_pulse_ns;     /* cmp0 触发脉冲宽度 */
    uint32_t   cmp1_value;        /* 比较器1 阈值 */
    trig_action_t cmp1_action;

    /* ── 运行态 (PIO 维护, AO 快照) ── */
    int32_t    counter_value;     /* 当前计数值 */
    uint32_t   rev_count;         /* Z 脉冲圈数 */
    uint32_t   total_count;       /* 上电累计 (永不复位) */
    uint32_t   frequency_hz;      /* 当前频率 (管理面更新) */
    uint32_t   cmp0_fire_count;   /* 比较器0 触发次数 */
    uint32_t   cmp1_fire_count;

    /* ── 诊断 ── */
    uint32_t   filter_reject_count;  /* 滤波器拒绝脉冲数 */
    uint32_t   overflow_count;       /* 上溢次数 */
    uint32_t   timeout_event_count;  /* 无脉冲超时次数 */
    uint32_t   max_interval_ns;      /* 最大脉冲间隔 */
    uint32_t   last_error;

    /* ... 保留公共字段 ... */
} trigger_vector_t;
```

### PIO 双 SM 架构

```
SM0 (编码器解码 + 滤波 + 计数):
  ┌─────────────────────────────────────┐
  │ 滤波器: in pins → shift → reject    │
  │ 解码器: 1x/2x/4x/UP_DOWN/PULSE_DIR │
  │ 方向: CW/CCW/BOTH/EXT              │
  │ 计数器: X (32bit, 递减)            │
  │ 门控: wait GATE pin                │
  │ Z 检测: irq 0 → SM1               │
  │ 比较: X==cmp? → irq 1 → SM1      │
  └─────────────────────────────────────┘

SM1 (阈值比较 + 输出 + 时间戳):
  ┌─────────────────────────────────────┐
  │ cmp0 到达 → set pins (脉冲输出)     │
  │ cmp1 到达 → irq → CPU (中断通知)    │
  │ 时间戳: timer latch → push → DMA    │
  │ Z 脉冲: rev_count++ (Y 递减)        │
  └─────────────────────────────────────┘
```

### PIO 滤波器实现

```asm
; 数字滤波器: N 个连续一致采样才通过
; ISR = 移位寄存器 (历史采样)
; OSR = 比较掩码
; 
; 例如 4 采样滤波 (N=4): ISR 最低 4 位全 0 或全 1 才通过

    in pins, 1             ; 新采样 → ISR[0]
    mov x, isr
    and x, filter_mask     ; 掩码取最后 N 位
    jmp x--, check_all_one
    ; 全零检查
    jmp x!=y, reject
passed:
    ; 信号稳定 → 继续正常流程
```

## 分阶段实施

### Phase 1 — 计数基础 (对标 ESP32 PCNT 核心)

| 项目 | 说明 |
|---|---|
| P1A: 解码模式 | SINGLE / QUAD_1X / QUAD_2X / UP_DOWN |
| P1B: 双向计数 | CW+CCW 双方向, PIO `jmp y--` 代理 |
| P1C: 比较器扩展 | 2 个独立阈值 + 动作 (脉冲/停止/IRQ) |
| P1D: 数字滤波器 | 可编程 N 采样一致窗口 |
| P1E: 频率测量 | 门控法 (100ms/1s/10s 窗口) |
| P1F: 外部门控 | GATE_IN 硬件门控 |
| P1G: 软复位/预设 | SCPI 命令清除/加载计数值 |
| P1H: 比较器脉冲宽度 | 可配置触发输出脉宽 |

### Phase 2 — 工业诊断

| 项目 | 说明 |
|---|---|
| P2A: 脉冲间隔统计 | min/max/avg 间隔, DMA 环形缓冲 |
| P2B: 超时检测 | N ms 无脉冲 → timeout event |
| P2C: 滤波器统计 | filter_reject_count |
| P2D: 频率变化率 | Δf/Δt 检测突变 |
| P2E: 方向反转检测 | 工业安全: 意外反转告警 |

### Phase 3 — 高级功能

| 项目 | 说明 |
|---|---|
| P3A: 4x 解码 | 全正交, 最高分辨率 |
| P3B: 脉冲+方向模式 | PULSE + DIR pin, 工业伺服标准 |
| P3C: 循环预设 | A→B 范围循环计数 |
| P3D: 批量预设 | 多个预设计数值, 顺序触发 |
| P3E: Flash 存储 | 配置非易失保存 |

## SCPI 命令扩展

```text
; 解码与配置
TRIGger:PCNT:DECode <SINGLE|QUAD1X|QUAD2X|QUAD4X|UPDOWN|PULSEDIR>
TRIGger:PCNT:DIRection <CW|CCW|BOTH|EXT>
TRIGger:PCNT:FILTer <ns>        ; 滤波窗口
TRIGger:PCNT:FILTer?
TRIGger:PCNT:GATE <ON|OFF|pin>  ; 门控
TRIGger:PCNT:GATE?

; 比较器
TRIGger:PCNT:CMP0 <value>,<action>,<pulse_ns>
TRIGger:PCNT:CMP0?
TRIGger:PCNT:CMP1 <value>,<action>,<pulse_ns>
TRIGger:PCNT:CMP1?

; 计数控制
TRIGger:PCNT:PRESet <value>     ; 预设值
TRIGger:PCNT:CLEar              ; 清零
TRIGger:PCNT:PAUSe
TRIGger:PCNT:RESume

; 统计查询
TRIGger:PCNT:FREQuency?
TRIGger:PCNT:TOTal?
TRIGger:PCNT:INTerval? <index>  ; 脉冲间隔
TRIGger:PCNT:DIAG?              ; 诊断摘要
```

## 参考时序

| 参数 | ENC_COUNT 当前 | Phase 1 目标 | ESP32 PCNT |
|---|---|---|---|
| 最大输入频率 | 10 MHz | 10 MHz (1x) / 5 MHz (4x) | **40 MHz** |
| 计数器位宽 | **32 bit** | 32 bit | 16 bit |
| 滤波器 | 仅 wait 0→1 | 可编程 0-255 clk_sys 窗口 | 0-1023 APB 窗口 |
| 比较器 | 1 (target) | 2 (cmp0, cmp1) | **5** |
| 解码模式 | 1x CW | 1x/2x + 双向 | 1x/2x/4x + 双向 |
| 频率测量 | ❌ | 门控法 | 需软件 |
| Z 行为 | rev_count++ | rev_count + 可复位 | 中断 |

## 结论

> ENC_COUNT 的 PIO 基础 (32bit × 10MHz) 已优于 ESP32 PCNT (16bit × 40MHz) 在**工业长计数**场景。差距在功能完备性：解码模式、多比较器、滤波器、频率测量。Phase 1 补齐后达到工业 PLC HSC 核心能力。
