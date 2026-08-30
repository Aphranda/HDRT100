# 状态机与底层实时资源域架构

Status: Active
Domain: STATE_MACHINE
Canonical: `docs/state_machine/STATE_MACHINE_DOMAIN_ARCHITECTURE.md`
Related: `docs/state_machine/STATE_MACHINE_DOMAIN_TODO.md`, `docs/state_machine/STATE_MACHINE_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/sync/SYNC_IO_ARCHITECTURE.md`, `docs/arch/HAOFV_ARCHITECTURE.md`
Last updated: 2026-08-29

本文档定义 HAOFV 中状态机与底层实时状态机资源的稳定边界。这里的“状态机”包括
PIO state machine、DMA/FIFO 驱动的硬件执行状态，以及由 core1 owner 管理的有界运行
状态；它不是某个业务域的第二个 owner，也不替代 TDMA、VDC、Calibration 或 Trigger
的 canonical 文档。

## 域定位与边界

```text
System Pack / SCPI / domain intent
          -> AO/FB owner and resource contract
          -> STATE_MACHINE resource/persona manager
          -> PIO SM + DMA + FIFO + IRQ
          -> deterministic wire or edge
```

状态机域负责：

- 声明、仲裁、装载和释放 PIO、SM、DMA、DREQ、FIFO、GPIO function 与 IRQ 资源；
- 为每个实时 persona 固定输入/输出方向、SM 角色、FIFO owner 和停止安全态；
- 检查 persona、资源代际和 profile identity，冲突时保持 `STOPPED` 并发布既有 fault；
- 为上层 owner 提供 bounded arm/disarm、运行快照和硬件 evidence 接口。

状态机域不负责业务 payload 解释、DPLL servo、RefMem commit、SD/FatFs、SVG 分析或
通过软件时间戳决定物理边沿。业务状态和跨节点事实仍由对应 AO/FB/Vector owner 管理。

## PIO 资源分区契约

当前迁移目标把三个 PIO block 按物理职责隔离：

| 资源域 | 固定职责 | 方向约束 | owner |
|---|---|---|---|
| SMA PIO | `SMA_IN/OUT`、appointment marker、SFCW/FMCW 捕获 | 输入采样和输出波形由独立 SM 承担 | Calibration / `sma_cable_delay` |
| TDMA TX PIO | TX 逻辑端：`CLK`、`SYNC/CS` 输出，以及反向 `DATA` 输入 | TX SM 同时配置 IN/OUT，但控制输出与 DATA 输入使用固定 pin/FIFO 语义 | TDMA core1 / PIO adapter |
| TDMA RX PIO | RX 逻辑端：`CLK`、`SYNC/CS` 输入，以及反向 `DATA` 输出 | RX SM 同时配置 IN/OUT，但控制输入与 DATA 输出使用固定 pin/FIFO 语义 | TDMA core1 / PIO adapter |

PIO 实例、GPIO 和 SM 编号不得在本文件复制为第二事实源；它们必须由 board profile、
`tdma_foundation_profile_t` 和 resource arbiter 的符号派生。上述表描述稳定职责，
不是当前源码已经完成迁移的声明。

## 上行/下行状态机模型

### 三类控制信号的方向定义

| 信号/控制 | TX 端 | RX 端 | 稳定语义 |
|---|---|---|---|
| `CLK` 控制 | `clk_out` | `clk_in` | TX 端产生位时序；RX 端接收并据此限定 DATA 采样/发送窗口。 |
| `SYNC`/`CS` 控制 | `sync_out` | `sync_in` | TX 端产生帧边界/启动标记；RX 端只用它建立帧门控，不把它当 payload。 |
| `DATA` 控制 | `data_in` | `data_out` | RX 端输出 payload/recovery；TX 端接收并进入 capture/forward 路径。 |

因此单条 link 的方向关系为：

```text
TX 端 clk_out + sync_out (CLK/CS)
      -> physical link -> RX 端 clk_in + sync_in

RX 端 data_out (DATA)
      -> return/data link -> TX 端 data_in

core0 inactive image -> core1 fixed phase
      -> RX 端 data_out FIFO/DMA
      -> PIO wire launch
      -> TX 端 data_in FIFO/DMA
          ├─ capture DMA -> RX ring -> core0 parser/diagnostic
          └─ forward/replace 由已发布 buffer 决定
```

TX 与 RX PIO block 均必须同时包含一个输入方向和一个输出方向；TX/RX 两个逻辑 SM
也都同时配置 IN/OUT，这是交叉收发的必要条件。`CLK` 和 `SYNC/CS` 从 TX→RX，
`DATA` 从 RX→TX；三类控制必须使用固定的 pin、FIFO、DREQ 和 DMA 语义。禁止的是
旧的 `master/slave` 复合协议语义（把未声明的 MOSI/MISO/CLK 交换混在一起），而不是
合法的交叉 IN/OUT 指令本身。

这种拆分带来的直接收益是：

- TX/RX 两个逻辑 SM 各自拥有固定的 `CLK/SYNC` 与 `DATA` IN/OUT 组合，避免旧复合
  persona 在不同角色间临时交换 MOSI/MISO/CLK；
- 每一类信号可以独立装载训练得到的 offset、独立采集 hardware evidence，并由
  独立 DMA endpoint 消费，便于定位单段 link 或单个 Node 的延迟；
- TX/RX 两个 PIO block 可以并行运行输入和输出路径，Core1 只需在 phase boundary
  选择已发布 buffer，减少实时路径上的软件参与和竞争。

### 独立飞行控制：RX 卸载与 TX 加载

飞行控制在逻辑上进一步分为两个相互独立的方向控制器，方向与物理含义保持一致：

| 控制器 | 方向 | 责任 | 允许访问的共享对象 |
|---|---|---|---|
| `flight_rx_unload` | 上行（输入） | 在 RX 帧边界读取已到达的 process image，完成 mailbox 识别，生成 `present/new/expected` 位图；RX descriptor 成功入队后才提交 sequence。 | RX PIO/DMA、`TDMA_RX_FRAME_FIFO`；不得写 TX image |
| `flight_tx_load` | 下行（输出） | 在 TX 帧边界选择 Core0 已发布的 TX generation，把本节点拥有的可写 segment 覆盖到 wire image，并把结果交给 TX PIO/DMA。 | TX PIO/DMA、`TDMA_TX_IMAGE_FIFO`；不得改变 RX 去重状态 |

`tdma_flight_engine_unload_rx()` 与 `tdma_flight_engine_load_tx()` 是这两个方向的
明确软件边界。二者可以在同一个 Core1 service 周期内并行推进，但不存在“先完成 RX
才允许 TX”或“TX 失败阻塞 RX”的隐式依赖。旧的 `tdma_flight_engine_apply*()` 仅作为
兼容封装，内部语义仍必须等价于一次独立 RX 卸载加一次独立 TX 加载。

这里的“上行/下行”沿用 TDMA 控制腿的命名，不等同于 DATA 电气线的箭头。当前点对点
接线中 `CLK/SYNC` 是 TX 端输出、RX 端输入；`DATA` 则由 RX 端输出、TX 端输入。因此
RX unload 是对到达本节点的 DATA/过程镜像做卸载，TX load 是把本节点 shadow image
装载到即将沿 DATA 反向返回的发送镜像；两者在同一节点上分别落到不同 PIO block，
不能仅凭 `TX`/`RX` 名称推断三根线的物理方向。

该边界保证“卸载数据”和“加载数据”在 ownership、失败计数和时序上可分别观测：RX
FIFO 满时只丢弃给 Core0 的解析镜像，TX 没有新 generation 时复用上一版；任一方向的
异常都不能把另一方向变成阻塞式路径。

限制也必须明确：PIO 指令存储仍按 PIO block 共享，增加 SM 不会增加该 block 的
 instruction RAM；每个 persona 仍需通过程序长度、GPIO function、FIFO 深度和 DREQ
 静态检查。若任一组合无法在预算内装载，必须拒绝 ARM，而不是退回未声明的复合 SM。

flight follower 的 RX PIO DATA SM 同时承担确定性的 wire forward 和 RX 卸载端点，
通过 `push noblock` 将字节送入该 SM 的 RX FIFO，再由 DMA 写入 RX ring；运行态不再
初始化第二个 DATA sampler，也不让两个 DMA 竞争同一 FIFO。SD/波形采集属于独立的
diagnostic persona，只能在停止或明确的 capture 窗口启用，不能改变 forward/unload
时序。`forward DMA` 与本地 payload/recovery TX DMA 由静态 profile 声明并按 persona
互斥，不能运行中临时借道或覆盖发送中的 buffer。

## SM 角色与所有权

目标角色集合如下，具体编号由 profile 定义：

| 角色 | PIO 域 | FIFO/DMA 关系 | 允许的实时操作 |
|---|---|---|---|
| `tx_sm` | TDMA TX | TX FIFO（CLK/SYNC 控制）；RX FIFO（DATA 输入/证据） | 输出 CLK/SYNC，同时接收 DATA |
| `rx_sm` | TDMA RX | RX FIFO（CLK/SYNC 边界）；TX FIFO（DATA 输出） | 接收 CLK/SYNC，同时输出 DATA |
| `tx_evidence_sm` / `rx_evidence_sm` | TDMA TX/RX | 独立硬件 tick/IRQ evidence FIFO | 记录两端边沿关联，不消费业务 FIFO |
| `tx_aux_sm` / `rx_aux_sm` | TDMA TX/RX | persona 专用 FIFO（静态声明） | 仅在 profile 明确时启用 |
| `sma_out_sm` / `sma_in_sm` | SMA PIO | SMA 专用 FIFO/DMA | 输出或采样 SMA 波形 |

core1 只在固定 phase 内选择已发布 buffer、装载 FIFO 和启动已验证 persona；PIO/DMA
执行实际字节和边沿；core0 负责解析、诊断、SD 和离线分析。任何状态机不得在实时
路径调用 FatFs、USB、DPLL 算法或动态内存。

## 生命周期与失败恢复

资源生命周期固定为：

```text
STOPPED -> validate profile -> abort/drain old DMA -> release old claims
         -> claim PIO/SM/DMA/GPIO -> load signed persona -> clear FIFO
         -> ARM -> RUNNING -> bounded disarm -> STOPPED
```

资源冲突、方向不符、DREQ/FIFO 不一致、程序装载失败、DMA stall 或 evidence 无法关联
时，状态机域必须 fail-closed：停止相关 persona、保留故障计数和 snapshot，不降级为
旧复合 TX/RX 路径。旧 persona 与新方向 persona 不得在同一个 RUN epoch 混用。

## 跨域接口与验证映射

| 接口 | 事实源/调用者 | 状态机域保证 |
|---|---|---|
| TDMA TX/RX resource contract | `tdma_foundation_profile_t` / TDMA owner | PIO、SM、DMA、GPIO 方向和互斥关系一致 |
| SMA maintenance persona | `sma_cable_delay` / Calibration owner | 不占用 TDMA TX/RX 资源 |
| Hardware timestamp evidence | TDMA PIO/DMA -> VDC gate | 只接受硬件 tick、sequence 和 active matrix 一致的记录 |
| Runtime snapshot | core1 owner -> core0/SCPI | 多字段使用既有 guarded/seqlock 发布约束 |

验证必须同时覆盖静态资源冲突、PIO 指令方向、DMA/DREQ 归属、STOP/ARM 生命周期、
follower forward/capture 不共享 FIFO，以及四节点 TDMA 与 NO5 只读观测。host/build
通过不能替代 OTA、HIL 和原始波形证据。

maintenance/calibration persona 仍保留旧 `BOARD_TDMA_SPI_PIO` 及
`MASTER_SM/SLAVE_SM` 复合实现；flight persona 已按本契约开始使用 TX/RX 两个 PIO
block。两者不得在同一个 RUN epoch 混用。只有全部 flight persona、资源仲裁、双路径
FIFO/DMA 和 OTA/HIL 通过 `STATE_MACHINE_DOMAIN_TODO.md` 退出门禁后，才可将运行时迁移
标记为已实现。
