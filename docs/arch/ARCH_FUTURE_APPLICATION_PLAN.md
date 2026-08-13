# Distributed Hard Real-Time Trigger System 未来应用路线图

Status: Draft
Domain: ARCH
Canonical: `docs/arch/ARCH_FUTURE_APPLICATION_PLAN.md`
Related: `docs/arch/ARCH_PRODUCT_ARCHITECTURE.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
Last updated: 2026-08-13

本文档记录 Distributed Hard Real-Time Trigger System 在当前产品完成后的平台化应用规划。它不是当前固件必须立即实现的功能清单，也不替代 `ARCH_PRODUCT_ARCHITECTURE.md`、`RTOS_HAOFV_TODO.md` 或各功能域待办。当前近期目标仍然是完成 DTC100 / RP2350 平台上的分布式触发产品闭环；本文面向后续产品线、产业应用和开源生态扩展。

## 核心判断

当前架构已经从单一“相控阵触发控制器”演进为通用的分布式硬实时系统平台基础：

```text
HAOFV
+ RTOS + dual-core AMP
+ Distributed Vector Blackboard / RefMem Sync Domain
+ 64 KB DistributedVectorTable
+ VDC / DPLL virtual DC time
+ PIO / DMA / IRQ hard real-time trigger chain
+ multi-node AO/FB role/persona loading
```

因此，系统名 `Distributed Hard Real-Time Trigger System` 不只覆盖当前相控阵/波导天线测试应用，也能承接后续分布式仪表、工业同步、运动控制、数据采集和开源硬实时平台方向。

## 近期主场景

近期主场景仍然是当前产品化闭环已经覆盖或正在覆盖的场景。

| 场景 | 价值 | 现有架构支撑 |
|---|---|---|
| 相控阵 / 多通道同步触发 | 多节点同步发射、采样、切换、极化、扫描和校准。 | LoopEngine、TriggerAO、T2/READY、VDC/DPLL、RefMem。 |
| 分布式仪表同步 | VNA/网分、示波器、AWG 等多仪表同步触发、同步采样和同步扫描。 | A3 gateway、SCPI、System Pack、AckCommandSlot、GatewaySlot。 |
| 多板协同测试系统 | A0/A1/A2/A3 角色协同，节点共同认知和 delta 同步。 | DistributedApplicationMap、NodeSlot[8]、RJ45_SYNC_RING、RefMem Sync。 |

这些场景是当前产品的第一优先级。后续平台化规划不能削弱当前产品的构建、烧录、板端验证和报告闭环。

## 横向扩展场景

这些场景不要求改 HAOFV 主架构，只需要新增或替换 AO/FB、role/persona、IO owner 和 System Pack 配置。

| 场景 | 可复用能力 | 需要新增的主要域能力 |
|---|---|---|
| 分布式运动控制 | 共同时间、分布式状态机、反射内存、动作链路、校准链路、故障证据。 | 运动轨迹 AO、轴同步 FB、位置反馈 owner、运动安全门禁。 |
| 分布式电机控制 | 多驱动同步 PWM、同步采样、同步换相、同步故障处理。 | PWM/ADC 硬实时 owner、电机控制 FB、驱动保护和功率级状态。 |
| 分布式测量系统 | T2/READY 捕获、时间基准、delay 校准、统计 slot、fault evidence。 | TDC/TOF/LiDAR/光纤传感测量 AO、timestamp 压缩和报告模板。 |
| 分布式 DAQ | 多节点同步采样、同步触发、同步事件捕获和数据面桥接。 | ADC/采样 buffer owner、采集任务、数据压缩和流式导出。 |
| 分布式 ATE / 产测 | System Pack、OTA、Diagnostics、Snapshot、Trace、Report 和 ACK/NACK。 | 工站 profile、产测流程 FB、夹具 IO、批次报告和权限矩阵。 |

这些扩展都应通过静态分布式应用模型加载到 A0-A7 通用节点，不应新增固定“第 9 类节点”。例如模型网分、模拟转台、脉冲分发、链路切换、仪表控制、运动轴控制器和测试代理都只是 role/persona/instance。

## 高价值商业方向

这些方向适合作为后续产品线或商业模块，而不是当前产品第一阶段目标。

| 方向 | 商业价值 | 平台化关键点 |
|---|---|---|
| 分布式射频系统 | MIMO、Massive MIMO、雷达阵列、射频扫描和多通道校准。 | 相位/时间校准、阵列状态表、仪表网关、质量报告。 |
| 分布式测试仪器 | 开源或低成本 VNA/AWG/示波器/多通道触发器。 | SCPI/USBTMC、仪器状态模型、采样/输出后端、校准证书。 |
| 分布式同步控制器 | EtherCAT DC、PXI Trigger、LXI Trigger 的轻量替代或补充。 | VDC/DPLL 质量、同步协议、节点一致性门禁、互操作接口。 |
| 分布式边缘控制 | 多节点协同采样、决策、动作和诊断。 | AO/FB 插件化、DistributedApplicationMap、连接质量和部署门禁。 |

这里的目标不是复制 EtherCAT、PXI 或 LXI 的全部生态，而是提供更轻量、可裁剪、可验证的硬实时同步控制平台。

## 开源生态方向

如果后续准备开源或半开源，建议优先开放不会暴露具体产品机密、但能形成生态吸引力的工具和基础件：

| 生态组件 | 作用 |
|---|---|
| RefMem simulator | 模拟 64 KB DistributedVectorTable、slot owner、ACK/NACK、stale 和 CRC。 |
| Distributed state visualizer | 可视化 AO/FB 实例、事件连接、数据连接和 DeploymentGate。 |
| DPLL visualizer | 可视化 VDC offset/rate、环路误差、LOCK/HOLDOVER/RELOCK 过程。 |
| Trigger chain analyzer | 分析 FIRE_LOAD、local_fire、T2/READY、late 和 fault evidence。 |
| Consistency validator | 验证 ApplicationMap、FbInstanceTable、EventLinkTable、DataLinkTable 和 System Pack CRC。 |
| Instrument control examples | 给 VNA/AWG/示波器/DAQ 的示例 owner、SCPI 和报告模板。 |
| Motion/robotics examples | 展示分布式同步动作和安全门禁，但不承诺替代完整工业运动控制标准。 |

开源生态的核心卖点不是“能控制某个 GPIO”，而是“用小 MCU 构建可观察、可校准、可恢复的分布式硬实时系统”。

## 跨平台可移植性

HAOFV 的核心思想与具体 MCU 无强绑定。RP2350 是当前最低成本、最容易推广的参考实现，不是架构边界。真正需要跨平台保持不变的是逻辑层：

```text
Active Object
Function Block
Vector / DistributedVectorTable
owner-based write contract
AckCommandSlot
VDC / DPLL virtual DC time
T2 / READY capture contract
System Pack / Diagnostics / Evidence
```

需要替换的是平台层：

```text
hard real-time backend
shared memory / cache / barrier
RTOS or Linux service model
OTA / boot chain
storage filesystem
network / sync transport
board profile
```

### 平台映射

| 平台 | 控制面 | 硬实时路径 | RefMem 承载 | 适合方向 |
|---|---|---|---|---|
| RP2350 / RP2350B | core0 RTOS | core1 + PIO + DMA + IRQ | SRAM 64 KB 表 | 低成本开源版、教学版、基础仪器控制。 |
| AM3352 | Linux / RTOS control | PRU + DMA / Ethernet | DDR、OCMC RAM 或 PRU shared RAM | 工业版、Linux 网关、PRU 实时同步。 |
| i.MX RT1170 | M7 control | M4、FlexIO、GPT、DMA | TCM / OCRAM | 高性能 MCU 版、复杂 UI + 实时双核。 |
| STM32H7 | M7 或 M7/M4 | TIM、HRTIM、DMA、EXTI、ITCM/DTCM | SRAM / DTCM | 工业 MCU、运动控制、DAQ、HRTIM 应用。 |
| ESP32-S3 | 双核 Xtensa + FreeRTOS | RMT、I2S、DMA、GPIO matrix | SRAM | 低成本无线/边缘版本，适合非极限实时场景。 |
| TI C2000 | DSP/control core | ePWM、CLA、ADC trigger、DMA | RAM | 电机控制、电源控制、多驱动同步。 |
| FPGA | softcore 或外部 CPU | 硬件 pipeline、counter、TDC、SERDES | BRAM / URAM | 极限硬实时、亚 ns TDC、大规模阵列。 |
| Zynq | ARM A9/A53 | PL FPGA fabric | DDR + BRAM | 商业高性能版、仪器/PXI/LXI/雷达阵列。 |

### 抽象层边界

跨平台时建议形成四层 portable 边界：

| 抽象层 | 保持稳定的接口 | 平台适配内容 |
|---|---|---|
| HAOFV core | AO/FB event、owner、state、result、budget。 | RTOS queue、thread、timer、critical section。 |
| RefMem core | slot layout、guard、snapshot、ACK/NACK、delta。 | memory placement、cache coherency、barrier、endianness。 |
| Time core | timestamp sample、DPLL update、VDC state、holdover。 | hardware timestamp、timer frequency、clock discipline。 |
| Realtime backend | ARM/FIRE_LOAD、capture、local_fire、T2/READY。 | PIO/PRU/TIM/RMT/FPGA pipeline implementation。 |

跨平台原则：

- AO/FB 不直接依赖 PIO、PRU、TIM、RMT 或 FPGA 寄存器。
- TriggerAO/TriggerFB 的输入输出契约保持不变；硬实时 backend 只实现 `arm/load/capture/status`。
- RefMem 64 KB 首版布局保持同构；更大平台可以增加 extension，不直接破坏基础 slot。
- DPLL 算法保持平台无关；平台只提供 timestamp 观测、tick 频率和时钟质量。
- Linux 平台可以把 SCPI/UI/System Pack 放在用户态，但实时 backend 和 RefMem 同步必须有明确 owner、权限和 cache/barrier 策略。

### 产品版本分层

| 版本 | 定位 | 典型平台 | 目标 |
|---|---|---|---|
| Open Reference | 低成本开源参考实现。 | RP2350 / RP2350B | 证明 HAOFV + RefMem + DPLL + Trigger chain 的最小闭环。 |
| Industrial MCU | 工业 MCU 产品化版本。 | STM32H7、i.MX RT | 更强算力、更强外设、更完整安全和工业 IO。 |
| Linux Gateway | 工业网关/仪表网关版本。 | AM3352、i.MX Linux SoC | Linux 上位服务 + PRU/实时核硬实时后端。 |
| Extreme Realtime | 极限时间精度版本。 | FPGA、Zynq | 硬件 pipeline、TDC、SERDES、阵列级同步。 |

## 移植路线图

后续跨平台不建议直接“大移植”，而应先抽象 portable contract：

| 阶段 | 目标 | 交付物 |
|---|---|---|
| P0 | 冻结 portable boundary。 | `portable_haofv`、`portable_refmem`、`portable_realtime_backend` 接口草案。 |
| P1 | RP2350 反向整理为 reference port。 | RP2350 port 层、PIO backend、SDK/FreeRTOS 适配清单。 |
| P2 | 主机仿真。 | RefMem simulator、DPLL unit test、command transaction test。 |
| P3 | 第二 MCU 试移植。 | STM32H7 或 i.MX RT 的 timer/DMA backend demo。 |
| P4 | Linux + realtime coprocessor 试移植。 | AM3352 PRU 或 Zynq PL backend demo。 |
| P5 | 生态发布。 | 平台兼容矩阵、移植指南、参考硬件、示例 persona。 |

## 平台化分层

后续产品化建议按三层推进：

| 层级 | 目标 | 交付物 |
|---|---|---|
| 产品层 | 完成当前 DTC100 / RP2350 分布式触发系统闭环。 | 固件、硬件约束、SCPI 指令表、System Pack、报告和验证工具。 |
| 平台层 | 把 HAOFV、RefMem、VDC/DPLL、AckCommandSlot 和 System Pack 抽象为可复用基础。 | portable refmem、portable command transaction、DPLL toolkit、配置/验证工具。 |
| 生态层 | 支持仪表、DAQ、运动控制、ATE、RF 和边缘控制示例。 | 示例 persona、仿真器、可视化工具、参考硬件和应用指南。 |

## 推进原则

- 当前产品闭环优先，不为远期生态牺牲近期构建、烧录、板端验证和报告闭环。
- 所有扩展应用都必须服从 HAOFV：owner、AO/FB、Vector、Resource Arbiter、Hardware Service 和硬实时边界。
- 未来应用通过 role/persona/instance 加载，不破坏 A0-A7 通用节点模型。
- RefMem 不变成任意共享内存；它仍然只保存共同事实、命令意图、ACK/NACK、版本、质量和证据。
- VDC/DPLL 是硬实时预测分发的基础，不和业务域混用。
- 开源组件优先选择模拟器、验证器、可视化工具和 portable 基础件，避免过早暴露未冻结的产品细节。

## 未来阶段建议

| 阶段 | 目标 | 判断标准 |
|---|---|---|
| F0 | 当前分布式触发产品闭环。 | DTC100 / RP2350 板端可稳定完成配置、同步、校准、触发、T2、报告和恢复。 |
| F1 | 平台基础件抽象。 | RefMem、AckCommandSlot、VDC/DPLL、System Pack 和工具链可在非当前产品中复用。 |
| F2 | 仪表/DAQ/ATE 示例。 | 至少 2 个非相控阵 demo persona 可通过同一 ApplicationMap 加载运行。 |
| F3 | 工业同步/运动控制探索。 | 能给出多轴/多执行器同步 demo、错误边界和安全限制。 |
| F4 | 开源生态包。 | 提供 simulator、visualizer、validator、reference examples 和清晰的兼容策略。 |
