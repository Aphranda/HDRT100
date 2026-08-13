# RP2350B 四板分布式触发方案

Status: Draft
Domain: TRIGGER / Distributed Sync
Target: RP2350_TRIG RP2350B QFN-80 四板系统
Canonical: `docs/trigger/RP2350B_FOUR_BOARD_DISTRIBUTED_TRIGGER_SCHEME.md`
Last updated: 2026-08-04
Related:

- `docs/reports/distributed-trigger/RTOS_DISTRIBUTED_TRIGGER_0614_REPORT.html`
- `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
- `docs/hardware/RP2350B_QFN80_IO_CONSTRAINTS.md`
- `docs/sync/SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md`
- `docs/sync/SYNC_IO_RESOURCE_PLAN.md`
- `docs/hardware/RP2350B_NETLIST_REVIEW_2026-08-04.md`

本文档把原 Zynq/EtherCAT/DC 分布式触发技术报告，改写为当前 RP2350B QFN-80
硬件可以落地的四板分布式触发方案。新版方案以 `RP2350B_QFN80_IO_CONSTRAINTS`
为唯一 IO 依据；早期文档中 `GPIO26..29` 作为 AUX0..AUX3 校准环路的口径已经不再适用。

## 1. 设计结论

四块 RP2350B 板卡采用同一硬件，通过角色配置形成 A0..A3 四个业务节点：

- A0：扫描/时间主控板，接收转台位置脉冲或编码器参考，运行扫描引擎和 DPLL，
  生成未来触发基准 `T_fire_base`。
- A1：DUT/链路板，按预约时刻切换 DUT 侧开关、门控或链路动作。
- A2：馈源/极化板，按预约时刻切换馈源极化或相关动作。
- A3：上位机接入/网分/VNA 板。上位机物理连接 A3，A3 转发配置、START/STOP
  和状态数据；在动作窗口内触发网分并捕获 READY/REDY 回读。

核心原则保持原报告思想，但实现载体改为 RP2350：

- 不依赖通信包到达时刻直接产生业务触发边沿。
- A3 将上位机配置和控制命令转入同步环，A0 生成未来触发时刻，各板提前装载
  本地 PIO 倒计时，到点由本板输出。
- RJ45 触发差分链路用于四板同步帧、预约帧、状态回读和校准，不作为串行实时转发链。
- SMA 输出负责设备近端触发，SMA 输入负责转台位置脉冲、设备 READY/T2 回读或示波器回环。
- RP2350 原型不承诺 Zynq/FPGA 方案中的 1 ns DC 绝对比较、亚 ns TDC 或 200 ps RMS 输出抖动；首版目标定位为百 ns 级分布式动作一致性原型。

## 2. 与原技术报告的映射

| 原报告概念 | Zynq/EtherCAT 方案 | RP2350B 四板映射 |
|---|---|---|
| DC 时间轴 | EtherCAT DC + FPGA 64-bit counter | 每板本地 tick + `offset/rate` 软件虚拟 DC |
| `T_fire_base` | 主站 DPLL 生成未来 DC 绝对时间 | A0 下位机 DPLL 生成未来虚拟 DC tick，上位机只配置参数 |
| `T_fire_i` | `T_fire_base + Δt_i` | 各板换算成本地 `delta_ticks` 后装载 PIO |
| PDO 预约队列 | EtherCAT PDO 周期下发 | RJ45_SYNC_RING / USB / 本地控制链路提前下发 |
| FPGA 本地比较 | PL 内绝对时间比较 | PIO 短窗口倒计时输出，CPU 维护 64-bit 时间 |
| TDC/T2 回读 | FPGA TDC 捕获设备完成脉冲 | PIO 捕获窗口 + CPU 扩展时间戳；必要时外部示波器闭合 |
| `e_dc` | EtherCAT DC/SYNC0 实测 | 虚拟 DC 同步残差 `e_vdc`，由同步帧边沿统计 |
| `e_act` | `T2_i - T_fire_base - Δt_i` | 保留同一验收口径，但统计分辨率受 RP2350/PIO 限制 |
| DPLL | 主站预测转台角度到时间 | A0 下位机运行并生成预约队列，各节点 PIO 输出已预约边沿 |

## 3. RP2350B IO 约束下的硬件分配

本方案严格采用 2026-08-04 RP2350B QFN-80 IO 约束。

### 3.1 主触发 SMA

| GPIO | 方向 | 物理信号 | 方案用途 |
|---:|---|---|---|
| 16 | 输出 | `SMA_OUT1` | 设备触发 / 开关锁存 / VNA TRIG / 测试脉冲 |
| 17 | 输出 | `SMA_OUT2` | 设备触发 / 辅助动作 |
| 18 | 输出 | `SMA_OUT3` | 设备触发 / 辅助动作 |
| 19 | 输出 | `SMA_OUT4` | 设备触发 / 辅助动作 |
| 20 | 输入 | `SMA_IN4` | READY/T2 回读 / 外部测试输入 |
| 21 | 输入 | `SMA_IN3` | READY/T2 回读 / 外部测试输入 |
| 22 | 输入 | `SMA_IN2` | READY/T2 回读 / 外部测试输入 |
| 23 | 输入 | `SMA_IN1` | A0 默认转台 Compare Out / 位置脉冲输入 |

注意：`GPIO20..23` 物理顺序为 `IN4, IN3, IN2, IN1`。固件用 PIO `IN PINS, 4`
采样后必须按新版 pin map 做通道解释或 bit reverse。

### 3.2 四板同步 RJ45 触发链路

| GPIO | 方向 | 物理信号 | 方案用途 |
|---:|---|---|---|
| 26 | 输出 | `RJ45_FWD_TRIG_OUT` | 同步/预约/状态帧转发输出 |
| 27 | 输入 | `RJ45_FWD_TRIG_IN` | 同步/预约/状态帧接收输入 |
| 32 | 输出 | `ISO1452_TRIG_DE` | TRIG 差分驱动使能，初始化完成后使能 |
| 41 | 输出 | `TRIG_RE` | TRIG 差分接收使能控制，默认接收使能 |

四板推荐组成单向环：

```text
A0 RJ45_FWD_TRIG_OUT -> A1 RJ45_FWD_TRIG_IN
A1 RJ45_FWD_TRIG_OUT -> A2 RJ45_FWD_TRIG_IN
A2 RJ45_FWD_TRIG_OUT -> A3 RJ45_FWD_TRIG_IN
A3 RJ45_FWD_TRIG_OUT -> A0 RJ45_FWD_TRIG_IN
```

每段均为点对点固定方向差分链路，不做多点总线仲裁，不在运行中切换方向。链路承担：

- 同步帧 `SYNC`
- 预约帧 `FIRE_LOAD`
- 状态帧 `DONE / READY / FAULT`
- 校准帧 `CAL_EDGE`

业务触发边沿仍由各板 SMA 输出本地产生。

### 3.3 BiSS / RS422 口边界

| GPIO | 方向 | 物理信号 | 本方案处理 |
|---:|---|---|---|
| 24 | 输入 | `BISS_DATA1_IN` | 保留给编码器/协议采样，不作为四板同步主链路 |
| 25 | 输出 | `BISS_CLK1_OUT` | 保留给 BiSS/SSI 时钟输出 |
| 28 | 输入 | `BISS_CLK0_IN` | 保留给上游时钟/协议 tap |
| 29 | 输出 | `BISS_DATA0_OUT` | 保留给协议辅助输出 |
| 30 | 输出 | `ISO1452_UP_BISS_DE` | 上游 BiSS 驱动使能 |
| 31 | 输出 | `ISO1452_DN_BISS_DE` | 下游 BiSS 驱动使能 |
| 40 | 输出 | `DN_BISS_RE` | 下游 BiSS 接收使能 |
| 42 | 输出 | `UP_BISS_RE` | 上游 BiSS 接收使能 |

如果转台或编码器后续提供 BiSS/SSI/RS422 位置接口，可由 A0 的 BiSS 口接入；
首版建议仍以 `SMA_IN1/GPIO23` 接收转台 Compare Out TTL/脉冲，先闭合触发链路。

## 4. 四板业务拓扑

```text
                 RJ45_SYNC_RING / 固定方向差分同步链路
       +------------------------------------------------------+
       |                                                      |
       v                                                      |
+-------------+      +-------------+      +-------------+      +-------------+
| A0 扫描主控 | ---> | A1 DUT/链路 | ---> | A2 馈源极化 | ---> | A3 PC/VNA网关 |
| DPLL / POS  |      | SP8T/门控   |      | POL/开关    |      | TRIG/READY  |
+-------------+      +-------------+      +-------------+      +-------------+
       ^                                                      |
       +-------------------- DONE / READY / FAULT ------------+

A0 SMA_IN1:  转台 Compare Out / 位置脉冲
A1 SMA_OUTx: DUT 侧开关、链路门控或锁存触发
A2 SMA_OUTx: 馈源极化、开关或锁存触发
A3 SMA_OUTx: VNA 外触发
A1/A2/A3 SMA_INx: 设备 READY / 完成脉冲 / 回环测量
```

如果 A1 和 A2 必须严格同一角度点同时动作，A0 必须提前下发共同的
`T_fire_base`；A1/A2 不能等各自收到前一块板的 DONE 后再实时动作。DONE 只用于
闭环确认、异常剔除和推进下一轮测试。

## 5. 时间模型

每块板维护本地单调 tick：

```text
local_tick = clk_sys tick
1 tick = 4 ns   when clk_sys = 250 MHz
```

CPU 维护虚拟 DC 映射：

```text
dc_tick = local_tick * rate_q32 + offset_tick
```

其中：

- `offset_tick`：本板相对 A0 虚拟 DC 的相位偏移。
- `rate_q32`：本板时钟相对 A0 的频率修正。
- `Δt_i`：通道、线缆、驱动器、接收阈值、设备动作延迟和期望相对 delay 的补偿量。
- `T_fire_i = T_fire_base + Δt_i`。

PIO 不维护完整 64-bit DC 时间。CPU 在安全提前量内把未来 `T_fire_i` 转成本地
`delta_ticks`，装载给 PIO；PIO 只执行短窗口倒计时和输出。

## 6. 帧格式建议

RJ45 同步环路首版使用“边沿时间戳 + 固定位宽 payload”的简单帧。第一个有效边沿
用于时间戳，后续 payload 只用于语义和 CRC。

```c
typedef enum {
    RING_SYNC      = 0x01,
    RING_FIRE_LOAD = 0x02,
    RING_A1_DONE   = 0x11,
    RING_A2_DONE   = 0x12,
    RING_MEAS_DONE = 0x13,
    RING_FAULT     = 0x7F,
} ring_frame_type_t;

typedef struct {
    uint8_t  frame_type;
    uint8_t  node_id;
    uint16_t flags;
    uint32_t sequence_id;
    uint64_t t_fire_base;
    int32_t  delta_ticks_or_value;
    uint8_t  crc8;
} ring_frame_t;
```

推荐从 12.5 Mbps 固定位宽 NRZ 开始验证；稳定后再评估 20.833 Mbps。

| 目标速率 | PIO cycles/bit @250 MHz | 用途 |
|---:|---:|---|
| 12.5 Mbps | 20 | 首版推荐，示波器和 CRC 验证裕量大 |
| 16.667 Mbps | 15 | 中间档 |
| 20.833 Mbps | 12 | 高速档，便于整周期 PIO 时序 |
| 25 Mbps | 10 | 对收发器、隔离器和布线要求更高 |

## 7. 业务时序

一轮测试建议按以下流程执行：

1. A0 通过 `SMA_IN1/GPIO23` 捕获转台 Compare Out 或位置脉冲。
2. A0 扩展时间戳，更新 `position_count` 和 `sequence_id`。
3. A0 的 DPLL 根据目标角度、当前速度估计和补偿表生成未来 `T_fire_base`。
4. A0 通过 RJ45_SYNC_RING 下发 `RING_FIRE_LOAD(seq, T_fire_base, link_index, pol_index)`。
5. A1/A2/A3 收到预约帧后换算本地 `delta_ticks`，装载 PIO `local_fire`。
6. 到点后：
   - A1 输出 DUT/链路动作触发；
   - A2 输出馈源/极化动作触发；
   - A3 按相对 delay 输出 VNA 触发。
7. 各板用 SMA 输入捕获设备完成/READY/T2，生成 `T2_i`。
8. A1/A2/A3 通过环路返回 `DONE / MEAS_DONE / FAULT`。
9. A0 计算 `e_i[k]`、`e_c[k]`、`e_act`，异常样本剔除后更新 DPLL 和慢速 `Δt_i`。

过期帧处理规则：

- 若 `T_fire_i - now < t_margin_min`，本板标记 `late`，禁止临界路径补救触发。
- CRC 错误、序号乱序、READY 超窗、链路超时均不进入 DPLL 环路滤波。
- 正式采集期间只允许平滑微调补偿，禁止突变已经生效的触发计划。

## 8. PIO 职责划分

### 8.1 `ring_rx_tx`

用于 `GPIO27/RJ45_FWD_TRIG_IN` 到 `GPIO26/RJ45_FWD_TRIG_OUT` 的同步环路收发：

```text
wait edge on RJ45_FWD_TRIG_IN
capture relative counter
decode fixed bit-time payload
optionally forward after fixed calibrated delay
push timestamp/status to CPU
```

### 8.2 `local_fire`

用于 `GPIO16..19/SMA_OUT1..4` 的预约触发输出：

```text
pull delta_ticks, mask, pulse_width_ticks, polarity
count down delta_ticks
set selected SMA_OUT high/low
hold pulse_width_ticks
return safe level
```

### 8.3 `capture_window`

用于 `GPIO20..23/SMA_IN4..1` 的转台位置脉冲和设备完成脉冲捕获：

```text
CPU arms capture window
PIO samples SMA_IN group
on selected edge: push channel bitmap + window counter
CPU expands to virtual DC timestamp
```

实现时必须显式记录 PIO 检测循环的 `loop_cycles`。捕获分辨率取决于循环周期，
不应写成等同于 1 个系统 tick，必须用示波器实测标定。

## 9. 指标与验收口径

RP2350B 四板方案采用分层验收，继承原报告的命名，但指标按 RP2350 能力重新约束。

| 验收项 | 符号 | RP2350B 首版目标 | 说明 |
|---|---|---:|---|
| 虚拟 DC 同步残差 | `e_vdc` | Demo P99 ≤ 100 ns；P99.9 ≤ 300 ns，实测冻结 | 由 RJ45_SYNC_RING 同步帧边沿统计；不从芯片典型值外推 |
| 本地触发输出粒度 | `e_q` | 4 ns @250 MHz | PIO 倒计时粒度；实际输出含收发器、隔离器、阈值和线缆 |
| 通道校准残差 | `e_io` | 首版 ≤ 10 ns；优化 ≤ 2.5 ns | SMA 输出、输入回读、线缆和设备阈值标定后的剩余残差 |
| 同板近端响应 | `t_near` | 典型 50-300 ns；验收 ≤ 1 µs | SMA 输入到 SMA 输出快路径，不代表跨板同步 |
| 设备动作残差 | `e_act` | 首版 P99.9 ≤ 300 ns；优化目标 ≤ 150 ns | `T2_i - T_fire_base - Δt_i`，由实测闭合 |
| DPLL 角度时间残差 | `e_pll` | 工程 P99.9 ≤ 50 µs；优化 ≤ 30 µs | 评价角度到时间映射，不与 ns 级动作同步混用 |
| late 预约 | `late_count` | 正式采集为 0 | 通信抖动只影响是否 late，不改变已装载边沿 |

如果最终要求跨板动作残差稳定小于 10 ns，或要求亚 ns 绝对 TDC 校准，应升级到
FPGA/TDC、硬件时间戳以太网或 EtherCAT DC 方案；RP2350B 四板原型不宜承诺该等级指标。

## 10. 状态机

| 状态 | 所属板 | 入口条件 | 退出条件 |
|---|---|---|---|
| `IDLE` | 全部 | 上电、未校准或未 ARM | 完成自检和链路检测 |
| `SYNCING` | 全部 | RJ45 环路已连接 | `e_vdc` 收敛并锁定 |
| `ARMED` | 全部 | 同步锁定、互锁闭合、输出安全 | 收到未来预约帧 |
| `QUEUED` | A1/A2/A3 | PIO 已装载 `delta_ticks` | 到点输出或 late |
| `FIRING` | A1/A2/A3 | 倒计时到点 | 输出脉宽结束并回到安全电平 |
| `WAIT_T2` | A1/A2/A3 | 已输出动作触发 | 捕获 READY/T2 或超时 |
| `ROUND_DONE` | A0 | 收到有效 DONE/MEAS_DONE | 更新残差和下一轮枚举 |
| `HOLDOVER` | 全部 | 同步暂时丢失 | 恢复锁定或超时进入 FAULT |
| `FAULT` | 全部 | CRC、late、超窗、互锁或硬件故障 | 人工或上位机清除后重新校准 |

## 11. 校准流程

### 11.1 静态 IO 校准

- 测量 `SMA_OUTx -> SMA_INx` 回环延迟。
- 测量 `RJ45_FWD_TRIG_OUT -> 下一板 RJ45_FWD_TRIG_IN` 单段延迟。
- 记录线缆长度、端接、收发器 propagation delay、温度和供电条件。
- 建立 `Δt_channel`、`Δt_cable`、`Δt_device` 初值表。

### 11.2 环路同步校准

- A0 周期发 `RING_SYNC(seq)`。
- A1/A2/A3 捕获同步边沿，并按固定转发延迟继续转发。
- A0 收回环路帧，统计总环路延迟。
- 通过多轮测量估计每板 `offset_tick` 和慢速 `rate_q32`。

### 11.3 业务动作校准

- A1/A2/A3 分别对 DUT、馈源和 VNA 做动作触发与 READY 回读。
- 计算 `T2_i - T_fire_base` 分布。
- 固定项写入 `Δt_i`，慢变项进入低带宽温漂/通道补偿。
- READY 缺失、超窗、乱序样本只记录诊断，不进入校准平均。

## 12. 分阶段实施计划

### Phase 0：单板 IO 验证

- 验证 GPIO16..19 四路 SMA 输出。
- 验证 GPIO20..23 四路 SMA 输入，并确认 `IN4..IN1` 反序解释正确。
- 验证 GPIO26/27 RJ45 触发收发方向。
- 验证 `ISO1452_TRIG_DE` 和 `TRIG_RE` 默认态，确保启动阶段不误驱动。

验收：

- 每个物理端口到软件通道映射正确。
- 输出安全态、极性、脉宽和最小间隔可配置。
- RJ45 单段回环连续 1e6 帧 CRC 错误为 0。

### Phase 1：四板 RJ45_SYNC_RING

- A0->A1->A2->A3->A0 组成固定方向环。
- 实现 `RING_SYNC`、`RING_FAULT`、基础 `sequence_id`。
- 每板记录 `rx_tick/tx_tick/status/crc`。

验收：

- 四板 1e6 轮无自激、无丢帧。
- 环路总延迟分布稳定，重启后可复现。
- 断线、CRC 错误、重复帧、乱序帧均能进入 FAULT 或 HOLDOVER。

### Phase 2：虚拟 DC 与预约触发

- A0 周期发同步帧，A1/A2/A3 估计 `offset/rate`。
- A0 发送未来 `FIRE_LOAD`。
- A1/A2/A3 提前装载 PIO `local_fire`。

验收：

- `late_count=0` 时，通信抖动不改变 SMA 输出边沿。
- A1/A2/A3 同一 `T_fire_base` 输出残差进入首版目标窗口。
- 可查询 `dc_locked/offset_tick/rate_ppb/late_count/last_seq`。

### Phase 3：A3 网关与 A0 扫描引擎

- 上位机通过网分近端 A3 的 CDC/USBTMC 下载程序包和发送 `START/STOP`。
- A3 完成传输层校验并通过 RJ45_SYNC_RING 把配置/控制转发给 A0。
- A0 从本地 TF/SD 装载程序，执行节点循环、数组循环和多层硬件联动。
- 大序列采用索引和双缓冲窗口读取；PIO/IRQ 不直接访问 TF/SD。
- START 后断开上位机，扫描仍按内部程序运行；异常保存循环断点和状态快照。

验收：

- 节点循环、数组循环、嵌套层级和多节点动作顺序与参考模型一致。
- 上位机不发送逐点推进命令，USB 断开不改变扫描节拍。
- 掉卡、文件 CRC 错误、缓冲欠载和断点恢复具有明确状态与故障行为。

### Phase 4：接入转台与设备

- A0 接入转台 Compare Out 或 BiSS/SSI 位置源。
- A1 接入 DUT/链路动作。
- A2 接入馈源/极化动作。
- A3 接入 VNA TRIG、READY/REDY 和上位机数据采样链路。

验收：

- `e_act` 由 T2/READY 或统一示波器实测闭合。
- A1/A2/A3 超窗不触发有效射频采样。
- 日志包含 seq、T_fire、T2、late、CRC、fault、温度和电源状态。

### Phase 5：DPLL 与整机自循环

- A0 下位机运行角度到时间 DPLL；上位机只配置参数、启动/停止和读取结果。
- -5°~-3° 完成加速和匀速确认。
- -3°~0° 完成预测收敛和队列装载。
- 0°~360° 正式采集期间滚动生成 `T_fire_base[k]`。

验收：

- `e_pll` 与 `e_act` 分开统计。
- `e_pll` P99.9 ≤ 50 µs 作为工程首版目标。
- `e_act` 按 A1/A2/A3 设备动作实测分布冻结。
- 正式测试期间上位机数据采样抖动不影响节点循环、动作节拍和触发边沿。

## 13. 软件/SCPI 接口建议

### 13.1 软件硬件分离原则

当前方案继承原 STM32 通用采样控制器的节点循环、数组循环和多层硬件联动
能力，但扫描编排必须运行在 A0 下位机。测试进入 `RUN` 后，四板硬件内部
自循环，不依赖上位机逐点推进。

职责边界如下：

- 上位机：通过网分近端 A3 编辑和下载配置、选择程序、启动、停止、暂停/恢复、
  仪表数据采样、状态读取和报告生成。
- A3：上位机通信和网分数据网关，负责配置包转发、命令应答、数据缓存与 VNA
  近端触发；不承担 RUN 态扫描节拍决策。
- A0：从 TF/SD 装载扫描程序，执行节点循环、数组循环、多层硬件联动和 DPLL，
  滚动生成预约触发队列，汇总四板状态并处理故障。
- A1/A2/A3：接收本节点动作窗口，执行 PIO 本地触发、READY/T2 捕获和状态回传。
- TF/SD：保存程序包、编排序列、校准表、断点、运行日志和数据索引；运行时采用
  预读和窗口化缓存，任何文件访问不得阻塞 PIO/IRQ 实时路径。

`RUN` 态下即使 USB/上位机暂时断开，已启动扫描仍按已装载程序继续运行；
只有硬件 `STOP`、有效的远程 `RUN:STOP`、互锁或 `FAULT` 才结束扫描。若安全
策略要求通信丢失停机，应作为可配置互锁项，而不是默认调度机制。

### 13.2 下位机扫描对象

| 对象 | 关键字段 | 当前硬件映射 |
|---|---|---|
| `HostLink` | `transport/session_id/fw_crc` | 上位机通过 CDC/USBTMC 连接网分近端 A3 |
| `NodeRoleMap` | `A0/A1/A2/A3/hw_profile` | A0=扫描/时间主站，A1=DUT/链路，A2=馈源/极化，A3=上位机网关/VNA |
| `ScanProgram` | `program_id/version/crc/entry` | A0 从 TF/SD 装载并校验，可保存多个测试程序 |
| `LoopEngine` | `level/node_loop/array_loop` | A0 支持节点循环、数组循环及机械外层/电子内层嵌套 |
| `NodeLoop` | `node_id/child/next/condition` | 支持不规则树形状态路径和按条件跳转 |
| `ArrayLoop` | `state_base/count/stride/repeat` | 支持规则状态数组和重复循环 |
| `LayerAction` | `layer_id/action_list/wait_rule` | 每层联动零个或多个 A1/A2/A3 硬件动作 |
| `ActionMap` | `node/sma_out/sma_in/edge/delay_us` | 绑定到 `GPIO16..19` SMA 输出和反序的 `GPIO20..23` SMA 输入 |
| `DelayCal` | `hop/sma/device_ready/valid_window` | RJ45 hop、SMA 口、设备 READY/T2、有效采样窗口补偿 |

大程序不应一次性全部复制到 RAM。A0 采用“TF/SD 程序文件 + RAM 索引 +
当前/下一窗口双缓冲”的方式运行；切换窗口在普通任务完成，PIO 只消费已经
准备好的动作描述符。

### 13.3 上位机接口三平面

接口分为配置面、控制面和数据面：

| 平面 | 允许动作 | 状态限制 |
|---|---|---|
| 配置面 | 下载程序包、角色、循环层级、动作映射、校准参数 | 只允许 `IDLE/CONFIG` 修改 |
| 控制面 | `ARM/START/STOP/PAUSE/RESUME` | `RUN` 后不接受逐点推进命令 |
| 数据面 | 仪表数据采样、进度、时间戳、日志、错误读取 | 走缓存或快照，不阻塞扫描引擎 |

项目化 SCPI/二进制块接口建议：

```text
*IDN?
SYST:ROLE?
NODE:MAP?

PROG:LOAD #<package>
PROG:STORE "SD:/plans/<name>.bin"
PROG:SELECT "<name>"
PROG:STAT?

LOOP:CONF <node_loop>,<array_loop>,<layer_count>
LOOP:STAT?
LAYER:ACT <layer>,<action_list>
ACT:MAP <node>,<function>,<out>,<in>,<edge>,<delay_us>,<wait_rule>
ACT:MAP?

RING:STAT?
RING:COUN?
RING:LAST?
RING:SYNC:RATE <Hz>

DC:STAT?
DC:OFFS?
DC:RATE?

RUN:ARM
RUN:START
RUN:STOP
RUN:PAUSE
RUN:RESUME
RUN:STAT?

CAL:IO:RUN
CAL:IO:TABLE?
CAL:RING:RUN
CAL:RING:TABLE?

DPLL:STAT?
DPLL:HOLD
DPLL:RELOCK

DATA:ACQ?
STAT:POS?
STAT:SAMP?
STAT:LOOP?
TRACE:TS?
SYST:ERR:LAST?
```

### 13.4 运行流程

1. 上位机生成扫描程序包，包含节点循环、数组循环、层动作表、设备映射、
   校准引用和数据采样配置。
2. 通过 `PROG:LOAD` 下载到 A3，A3 完成传输层 CRC 后转发给 A0；A0 校验程序
   版本、IO 能力和四板在线状态，然后把权威运行副本写入本地 TF/SD。
3. A0 选择程序并执行 dry-run/估时，检查最大循环深度、动作窗口、READY 超时、
   RAM 缓冲和触发队列裕量。
4. 上位机发送 `RUN:ARM`、`RUN:START` 后退出扫描调度路径。
5. A0 从 TF/SD 窗口化装载编排序列，内部推进节点循环、数组循环和层级状态机，
   并向 A1/A2/A3 分发未来动作。
6. 上位机并行完成 VNA 等仪表数据采样，通过 `DATA:*`、`STAT:*`、`TRACE:*`
   读取缓存结果；读取延迟不影响硬件扫描节拍。
7. 扫描完成、`RUN:STOP`、互锁或 `FAULT` 后，A0 保存最终断点、统计和日志。
8. 异常信息包含 FSM 状态、循环层、位置、节点/数组索引、节点号、端口号、
   `late/CRC/ready_timeout` 等上下文。

开发期允许追加 GPIO 级诊断命令，但产品 UI/SCPI 应优先使用语义通道：
`SMA_OUT1..4`、`SMA_IN1..4`、`RJ45_SYNC_IN/OUT`、`DPLL_STATE`、`FIRE_QUEUE`。

## 14. 风险与边界

- 新版硬件只有一组 RJ45 触发输入和一组输出，天然适合链式/环式同步，不是三路并行星形广播。
- 环路同步可以标定固定 hop delay，但无法仅靠单向环唯一分解每段传播延迟；需要线缆交叉测试、反向测试或外部示波器辅助。
- RP2350 PIO 不能直接读取系统 timer。所有时间戳都必须通过窗口计数、采样索引或 CPU 扩展实现。
- `GPIO20..23` 输入物理顺序与通道编号相反，是固件和测试最容易踩坑的位置。
- BiSS/RS422 口在本方案中不承担四板同步主链路，避免与编码器/协议功能互相污染。
- 若现场 EMI 强，RJ45 差分链路、SMA 线缆、GND/FGND 单点策略、屏蔽层连接和 ISO1452 默认态必须按 PCB 评审清单闭环。

## 15. 当前推荐下一步

1. 固件 pin map 先同步到 `RP2350B_QFN80_IO_CONSTRAINTS`，特别是：
   - `GPIO16..19 = SMA_OUT1..4`
   - `GPIO20..23 = SMA_IN4..1`
   - `GPIO26/27 = RJ45_FWD_TRIG_OUT/IN`
2. 做单板 GPIO/PIO 通道映射测试，生成物理端口到软件通道的测试记录。
3. 用两板先验证 RJ45 同步单段链路，再扩展到四板环。
4. 四板环路延迟分布稳定后，再接入预约触发和 DPLL。
5. 所有对外指标以实测 P99/P99.9 分布冻结，不把 RP2350 原型写成 EtherCAT/Zynq 等级承诺。
