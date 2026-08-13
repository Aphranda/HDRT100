# 触发系统工业级增强方案

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/TRIGGER_INDUSTRIAL_ENHANCEMENT_DESIGN.md`
Related: `docs/trigger/TRIGGER_SYNC_TODO.md`, `docs/sync/SYNC_IO_RESOURCE_PLAN.md`, `docs/trigger/TRIGGER_SEQ_STEP_DESIGN.md`
Last updated: 2026-07-07

本文档基于对开源触发/测试仪器项目的对标分析，梳理当前 SEQ_STEP 实现与工业级触发系统的差距，
并给出分阶段增强方案。

## 对标项目

| 项目 | 领域 | 关键参考点 |
|---|---|---|
| [ngscopeclient/scopehal](https://github.com/ngscopeclient/scopehal) | 示波器 HAL | 8 种触发类型（Edge/Pulse/Runt/Slew/Dropout/Window/UART/Glitch），触发偏移模型 |
| [gusmanb/logicanalyzer](https://github.com/gusmanb/logicanalyzer) | 逻辑分析仪 | 24ch/400MHz PIO 采样，运行时 PIO 程序生成，复杂多级触发 |
| [LAFT Logic Analyzer](https://ece4760.github.io/Projects/Fall2025/jk2582_cjm369_mlh348/index.html) | 逻辑分析仪 | 运行时生成 PIO 触发程序，4 级触发条件链，SUMP 协议兼容 |
| [temps_utile_](https://github.com/patrickdowling/temps_utile-) | 时钟/触发发生器 | 6 通道，burst 模式，逻辑门（AND/OR/XOR），Euclidean 发生器，<100μs 延迟 |
| [Sitka Gravity](https://www.synthtopia.com/content/2026/03/20/sitka-instruments-gravity-eurorack-clock-module-now-open-source/) | 触发音序器 | 16 步 xox 音序器，实时录制，随机跳过，2 bank × 8 slot 存储 |
| [HAGIWO SyncLFO](https://awonak.github.io/HagiwoModulove/) | 模块合成器 | Burst 发生器（周期/计数/形状），触发延迟（delay/gate/slew），包络发生器 |
| [terrygeng/ads124s0x-pio](https://github.com/terrygeng/ads124s0x-pio) | DMA 序列器 | 3 DMA 通道自重置链，双缓冲 PIO 输出 |

## 当前差距总览

### 触发源

| 特性 | 当前状态 | 对标项目 | 优先级 |
|---|---|---|---|
| 触发源 IO 选择 | ❌ 硬编码 GPIO16 | scopehal (多通道选择) | **P0** |
| 有效触发源 | GPIO16(TRIG_IN)/17(ARM_IN)/18(EXT_CLK)/19(GATE_IN)/26-29(AUX0-3) | — | P0 |
| 软件触发（SCPI 单步命令） | ❌ | 通用 | P0 |
| 内部定时器触发 | ❌ | temps_utile_ | P2 |

**设计要点**：PIO `wait pin` 指令使用的 pin index 是相对于 `sm_config_set_in_pins()` 设定的
`in_pin_base` 的偏移量。当前 PIO 程序用 `wait 0 pin, 0`，即 `in_pin_base + 0`。
因此在 ARM 前调用 `seq_step_program_init()` 时传入不同的 `trigger_in_pin` 即可动态切换触发源，
**无需修改 PIO 指令本身**。`pio_gpio_init()` 会将新引脚绑定到 PIO，旧引脚需释放回 SIO。

### 触发模式

| 特性 | 当前状态 | 对标项目 | 优先级 |
|---|---|---|---|
| 上升沿触发 | ✅ SEQ_STEP | scopehal Edge | — |
| 下降沿触发 | ❌ | scopehal Edge | P0 |
| 双边沿触发 | ❌ | scopehal Edge | P1 |
| 电平触发（高/低） | ❌ | scopehal Window | P1 |
| 脉冲宽度限定（min/max） | ❌ | scopehal Pulse Width | P0 |
| 毛刺抑制（<N ns 拒绝） | ❌ | scopehal Glitch | P0 |
| 多级触发链（ARM→QUALIFY→TRIGGER） | ❌ 仅 ARM_IN 预留 | LAFT (4-stage) | P1 |
| 外部门控（GATE_IN） | ❌ 仅 GPIO 预留 | 通用工业触发 | P0 |
| 内部定时器触发 | ❌ | temps_utile_ | P2 |

### 序列/输出控制

| 特性 | 当前状态 | 对标项目 | 优先级 |
|---|---|---|---|
| 编码序列步进 | ✅ SEQ_STEP | — | — |
| 单步模式（每触发一步，不自动连续） | ❌ | 通用 | P0 |
| Burst 模式（一次触发输出 N 脉冲） | ❌ | SyncLFO, temps_utile_ | P1 |
| 连续循环 | ✅ DMA ring-buffer | — | — |
| 乒乓往返（正向→反向→正向） | ❌ | AWG 通用 | P2 |
| 随机步进 | ❌ | Gravity random skip | P2 |
| 条件跳转（输出码决定下一跳） | ❌ | LAFT PIO jmp | P2 |
| 每步独立定时（hold time / dead time） | ❌ | 工业 AWG | P1 |
| 触发延迟（触发后 N ns/μs 再输出） | ❌ | SyncLFO Trigger Delay | P1 |
| 触发间隔最小值 | ❌ | scopehal Holdoff | P0 |

### 统计与诊断

| 特性 | 当前状态 | 对标项目 | 优先级 |
|---|---|---|---|
| 触发计数（总触发次数） | ❌ | 通用 | P0 |
| 输出计数（总输出步数） | ❌ | 通用 | P0 |
| 丢失触发计数（BUSY 期间到达的触发） | ❌ | 通用 | P0 |
| 门控拒绝计数（GATE_IN 禁止的触发） | ❌ | 通用 | P1 |
| 毛刺拒绝计数（脉冲宽度不足） | ❌ | 通用 | P1 |
| 触发间隔统计（min/max/avg） | ❌ | 工业仪器 | P2 |
| 运行时间累计 | ❌ | 通用 | P2 |
| 溢出/欠载计数 | ✅ dropped_capture_words | — | — |
| 故障锁存 + 时间戳 | ❌ | 工业安全系统 | P0 |
| 回绕计数 | ✅ rollover_count | — | — |
| 序列索引快照 | ✅ seq_index | — | — |
| 时间戳记录（每次触发的绝对时间） | ❌ | 工业测试 | P1 |

### 安全与保护

| 特性 | 当前状态 | 对标项目 | 优先级 |
|---|---|---|---|
| 输出安全态定义（全 0/全 1/保持/高阻） | ❌ 仅默认拉低 | 工业安全 | P0 |
| CPU 心跳看门狗（超时自动 DISARM） | ❌ | 工业安全 | P0 |
| 紧急停止输入 | ❌ | 工业安全 | P1 |
| 故障锁存（FAULT 需显式清除） | ❌ | 工业安全 | P0 |
| 配置 CRC 校验 | ❌ | 工业仪器 | P2 |

### 运行时控制

| 特性 | 当前状态 | 对标项目 | 优先级 |
|---|---|---|---|
| ARM / DISARM | ✅ | — | — |
| 单步命令（SCPI 触发一次步进） | ❌ | 通用 | P0 |
| 暂停/恢复 | ❌ | 通用 | P1 |
| 动态跳步（跳到指定索引） | ❌ | 工业测试 | P2 |
| 运行中修改序列表（live update） | ❌ | AWG | P2 |
| 条件停止（输出特定码时停止） | ❌ | 工业测试 | P2 |
| 触发源选择（TRIG_IN/软件/定时器） | ❌ | 通用 | P1 |

### 数据管理

| 特性 | 当前状态 | 对标项目 | 优先级 |
|---|---|---|---|
| 命名序列保存/调用 | ❌ | Gravity (2×8 slots) | P2 |
| 非易失存储（Flash save/load） | ❌ | 工业仪器 | P2 |
| 配置导入/导出（SCPI 二进制块） | ✅ SEQ:DATA / SEQ:DATA? | — | — |
| 多序列槽位 | ❌ | Gravity | P2 |
| 配置校验（值域检查） | ❌ | 工业仪器 | P0 |

## 分阶段增强路线

### Phase 1 — 工业可用性基线（P0）

目标：从"基础功能跑通"提升到"现场可安全使用"。

```
P1A: 触发边沿选择    — 上升沿/下降沿/双边沿 + 毛刺抑制
P1B: 外部门控          — GATE_IN 硬件接入 PIO
P1C: 软件触发          — SCPI 单步触发命令
P1D: 单步模式          — 每触发仅输出一步，需重新 ARM 或持续等待
P1E: 触发统计          — trigger_count, output_count, missed_count
P1F: 故障模型          — 故障锁存 + 错误码 + 时间戳
P1G: 安全输出态        — IDLE/FAULT 时输出可配置安全值
P1H: 看门狗            — 可选 CPU 心跳看门狗自动 DISARM
P1I: 配置校验          — 序列表值域检查 + 参数合法性
P1J: 触发间隔最小值    — 防止噪声/反弹导致的过高速触发
```

### Phase 2 — 工业灵活性（P1）

```
P2A: 脉冲宽度限定      — 仅接受 N ns < 脉宽 < M ns 的触发
P2B: 多级触发链        — ARM_IN → GATE_IN → QUALIFY → TRIGGER 四级
P2C: Burst 模式        — 一次触发自动输出 N 步
P2D: 每步定时          — 序列步间可编程延迟
P2E: 触发延迟          — 触发后延时 N ns/μs 再输出
P2F: 时间戳            — 每次触发记录 32-bit 绝对时间戳
P2G: 门控拒绝统计      — GATE_IN 关闭期间到达的触发计数
P2H: 毛刺拒绝统计      — 脉宽不足被拒绝的触发计数
P2I: 暂停/恢复         — 保持当前位置，暂停接受触发
P2J: 触发源选择        — TRIG_IN / 软件 / 内部定时器
```

### Phase 3 — 高端仪器能力（P2）

```
P3A: 乒乓序列           — 自动正向→反向→正向步进
P3B: 条件跳转           — 输出码决定序列分支（PIO 内实现）
P3C: 运行时改表         — 不 DISARM 即可修改序列表（双缓冲）
P3D: 命名存储           — Flash 保存/调用多组配置
P3E: 触发间隔统计       — min/max/avg/stddev
P3F: 随机步进           — 概率跳步
P3G: 内部定时器触发     — 可编程周期的自走触发
P3H: 条件停止           — 输出特定编码时自动 DISARM
```

## TriggerVector 扩展设计

按 Phase 1 增强后的 TriggerVector：

```c
#define TRIG_SEQ_TABLE_MAX   256u
#define TRIG_SEQ_WIDTH_MAX   8u
#define TRIG_TIMESTAMP_DEPTH 64u     /* 时间戳环形缓冲 */

typedef enum {
    TRIG_EDGE_RISING  = 0,
    TRIG_EDGE_FALLING = 1,
    TRIG_EDGE_BOTH    = 2,
} trig_edge_t;

typedef enum {
    TRIG_SAFE_ZERO    = 0,    /* 输出全 0 */
    TRIG_SAFE_ONE     = 1,    /* 输出全 1 */
    TRIG_SAFE_HOLD    = 2,    /* 保持最后值 */
    TRIG_SAFE_HIZ     = 3,    /* 高阻（输入模式）*/
} trig_safe_state_t;

typedef struct {
    /* 模式 */
    trig_mode_t    active_mode;
    uint32_t       supported_modes;

    /* 触发条件 */
    trig_edge_t    edge;               /* 边沿选择 */
    uint32_t       trigger_source_pin; /* 触发源 GPIO (16-19, 26-29) */
    uint32_t       gate_source_pin;    /* 门控源 GPIO (0=禁用) */
    uint32_t       min_pulse_ns;       /* 最小脉宽（ns），0=不检查 */
    uint32_t       min_interval_ns;    /* 最小触发间隔（ns），0=不限制 */
    bool           gate_enabled;       /* GATE_IN 使能 */
    bool           single_step_mode;   /* 单步模式 */

    /* SEQ_STEP 配置 */
    uint32_t       seq_table[TRIG_SEQ_TABLE_MAX];
    uint32_t       seq_length;
    uint32_t       seq_index;
    uint32_t       seq_output_width;
    trig_safe_state_t safe_state;      /* IDLE/FAULT 时的输出安全态 */

    /* 运行态 */
    trig_state_t   state;
    uint32_t       error_code;
    uint32_t       fault_timestamp_ms;

    /* 统计 */
    uint32_t       trigger_count;      /* 接受的有效触发 */
    uint32_t       output_count;       /* 总输出步数 */
    uint32_t       missed_count;       /* BUSY 期间丢失的触发 */
    uint32_t       gate_reject_count;  /* GATE_IN 关闭拒绝的触发 */
    uint32_t       glitch_reject_count;/* 脉宽不足拒绝的触发 */
    uint32_t       rollover_count;

    /* 时间戳 */
    uint32_t       timestamps[TRIG_TIMESTAMP_DEPTH];
    uint32_t       timestamp_head;
    uint32_t       timestamp_count;

    /* 兼容旧字段 */
    uint32_t       trigger_width_us;
    uint32_t       pulse_width_us;
    uint32_t       marker_width_us;
    uint32_t       capture_sample_hz;
    uint32_t       sync_clock_hz;
    bool           sync_clock_enabled;
    bool           initialized;
    bool           io_initialized;
    bool           capture_running;
    bool           sync_clock_running;
    uint32_t       dropped_capture_words;
} trigger_vector_t;
```

## PIO 程序增强（Phase 1）

Phase 1 需要的 PIO 增强：

```asm
; seq_step_v2.pio — 增强版编码序列步进
; 新增: 下降沿触发、毛刺抑制、GATE_IN 门控、单步模式

.program seq_step_v2
.wrap_target
    ; ── GATE_IN 检查 ──
    wait 1 pin, GATE_IN       ; 等待门控使能

    ; ── 边沿检测 + 毛刺抑制 ──
    wait 0 pin, TRIG_IN       ; 等低
    wait 1 pin, TRIG_IN       ; 等上升沿（下降沿模式用相反的极性）
    
    ; ── 输出编码 ──
    out pins, 4               ; auto-pull 自动拉下一字
    
    ; ── 单步模式下发射 IRQ 通知 CPU ──
    irq 0                     ; 通知 TriggerAO: 一次触发已完成
.wrap
```

毛刺抑制通过 `wait 0` + `wait 1` 自然实现（至少持续 2 PIO 周期 ≈ 13ns）。可配置 `min_pulse_ns` 在 CPU 端做慢速二次校验，
或 PIO 内插入 NOP 循环。

## SCPI 命令扩展

```text
; 触发源
TRIGger:SOURce <pin>            ; 设置触发源 GPIO (16-19, 26-29)
TRIGger:SOURce?
TRIGger:GATE:SOURce <pin>       ; 设置门控源 GPIO (16-19, 26-29, 0=禁用)
TRIGger:GATE:SOURce?

; 触发条件
TRIGger:EDGE <0|1|2>            ; 0=RISING, 1=FALLING, 2=BOTH
TRIGger:EDGE?
TRIGger:MINPulse <ns>
TRIGger:MINPulse?
TRIGger:MINInterval <ns>
TRIGger:MINInterval?
TRIGger:GATE <ON|OFF>
TRIGger:GATE?

; 运行控制
TRIGger:STEP                  ; 单步触发（软件触发源）
TRIGger:SINGle <ON|OFF>       ; 单步模式开关
TRIGger:SINGle?
TRIGger:PAUSe                 ; 暂停触发接受
TRIGger:RESume                ; 恢复触发接受

; 安全
TRIGger:SAFE <ZERO|ONE|HOLD>
TRIGger:SAFE?

; 统计
TRIGger:COUNt?
TRIGger:COUNt:CLEar
TRIGger:TSTamp? <index>       ; 查询指定时间戳

; 诊断
STATus:TRIGger?               ; 完整摘要（已实现，需扩展字段）
```

## 实现顺序建议

```
Phase 1A  (触发边沿选择)     — 12 条指令，PIO 程序扩展 + SCPI
Phase 1B  (外部门控)          — PIO wait 指令 + pin 配置
Phase 1C  (软件触发)          — SCPI 命令 → 事件 → 消费
Phase 1D  (单步模式)          — PIO IRQ + 状态机新增 SINGLE_STEP 状态
Phase 1E  (触发统计)          — struct 字段 + ISR 更新 + SCPI 查询
Phase 1F  (故障模型)          — ECC 表新增 FAULT 路径 + 时间戳
Phase 1G  (安全输出态)        — DISARM 时按配置设置 GPIO
Phase 1H  (看门狗)            — OSAL timer 周期性检查 ARM 态 CPU 心跳
Phase 1I  (配置校验)          — fb_idle_configure_seq 强化检查
Phase 1J  (触发间隔最小值)    — PIO 内插入间隔计数 + CPU 端可配置阈值
```

## 参考资源

- scopehal Trigger Types: `ngscopeclient/scopehal` — `section-triggers.tex`
- LAFT 运行时 PIO 生成: `ece4760.github.io/Projects/Fall2025/jk2582_cjm369_mlh348`
- gusmanb/logicanalyzer RP2350 port: `github.com/gusmanb/logicanalyzer`
- temps_utile_ 多模式触发发生器: `github.com/patrickdowling/temps_utile-`
- HAGIWO Burst/ADSR/Delay 发生器: `awonak.github.io/HagiwoModulove`
