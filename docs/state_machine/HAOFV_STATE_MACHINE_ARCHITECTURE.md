# 状态机与底层实时资源域架构

Status: Active
Domain: STATE_MACHINE
Canonical: `docs/state_machine/HAOFV_STATE_MACHINE_ARCHITECTURE.md`
Related: `docs/state_machine/HAOFV_STATE_MACHINE_TODO.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/sync/SYNC_IO_ARCHITECTURE.md`, `docs/arch/HAOFV_ARCHITECTURE.md`
Last updated: 2026-09-03

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

## TDMA-RESIDENT-01：常驻过程映像生命周期

状态机域对 TDMA 的最终抽象不是“完成一帧后重新 ARM”，而是管理一个启动一次、持续
运行的 resident process image。下面是架构级生命周期；它描述稳定语义，不要求与某个
过渡期 C enum 逐项同名。

```text
STOPPED
   -> STAGED
   -> ARMED
   -> RESIDENT_INIT       一次注入初始 process image
   -> RUNNING
        -> CYCLE_BOUNDARY
        -> LOCAL_UNLOAD
        -> LOCAL_LOAD
        -> FORWARD
        -> CYCLE_BOUNDARY  循环

RUNNING --STOP/RESET/FAULT/RECONFIGURE--> STOPPED 或 STAGED
```

`RESIDENT_INIT` 只执行一次：验证 active profile、选择初始 active/shadow image、清理
FIFO/DMA 并启动已验证的 flight persona。进入 `RUNNING` 后，物理 frame 的完成只是
`CYCLE_BOUNDARY` 事件，不是生命周期终点；状态机不得因一个 frame 返回 origin 就转入
终止性 `FRAME_COMPLETE`。

每个 cycle 的固定职责如下：

| 执行阶段 | owner | 语义 |
|---|---|---|
| `CYCLE_BOUNDARY` | Core1 TDMA owner | 选择已经发布的 generation，建立本周期的 active image 视图。 |
| `LOCAL_UNLOAD` | RX flight path | 从到达的 resident image 取出本节点拥有的输入 segment，并发布受保护的 descriptor/evidence。 |
| `LOCAL_LOAD` | TX flight path | 将本节点新的 segment generation 写入自己的固定位置；没有更新则复用上一版。 |
| `FORWARD` | PIO/DMA | 继续发送同一物理 frame instance；不等待 Core0，不为其他 Node 新建独立 frame。 |

因此多个 Node 可以在同一轮传播中完成各自的卸载/装载。`cycle sequence` 表示循环
周期，`segment generation` 表示具体 Node 字段更新代次；状态机只负责按 owner 和
boundary 调度它们，不混用两个序号。

## ARCH-PIOPARTITION-01：PIO 资源分区契约

三个 PIO block 按唯一运行时 owner 隔离，实例映射以 `BOARD_TDMA_SMA_PIO_BLOCK_ID`、
`BOARD_TDMA_TX_PIO_BLOCK_ID` 和 `BOARD_TDMA_RX_PIO_BLOCK_ID` 为代码事实源：

| 资源域 | 固定职责 | persona 方向约束 | 唯一运行时 owner |
|---|---|---|---|
| Realtime Observation / SYNC_IO/SMA PIO | `SYNC_IO` 语义输入输出、SMA、预约触发、校准捕获和逻辑分析仪 persona | persona 显式声明 pin 指令方向、FIFO、DMA/DREQ；互斥切换，不要求所有能力并发常驻 | `SYNC_IO realtime owner`；Calibration、Trigger 和诊断工具只是 persona 请求方 |
| TDMA TX PIO | combined `CLK+SYNC/CS` control、origin 返回 DATA capture、RTT/clock-latch evidence | control、capture、evidence 分属独立 SM/FIFO；一个 control SM 同时产生 CLK 和 SYNC | TDMA Foundation/Core1 owner |
| TDMA RX PIO | origin DATA output，或 follower DATA flight/本地过程映像 overlay，以及 follower clock-latch evidence | follower DATA SM 允许在同一 bit loop 执行 `in pins` 与 `out pins`；业务 RX FIFO/DMA 消费者仍唯一 | TDMA Foundation/Core1 owner |

PIO 实例、GPIO、SM 和 DMA 编号不得在本文复制为第二事实源；board profile 由
`tdma_state_machine_resource_contract()` 投影到运行时，resource arbiter 负责整块 PIO、
DMA、GPIO、IRQ 和 DREQ 的原子 claim/release。TDMA flight 运行期间独占 TDMA TX/RX
两个 PIO block，maintenance/calibration persona 不得与 flight 混用。

`SYNC_IO` 输出与预约触发已收敛到 Realtime Observation / SYNC_IO/SMA PIO 的 PIO0 persona
manager；TDMA flight 不再启停或临时接管 SYNC_IO 的输出资源。只读 `LOGIC_ANALYZER`
persona 可以在该 PIO 上旁路采样 TDMA GPIO pad，但不得改变
目标 GPIO function、方向或 pull，也不得读取 TDMA 业务 FIFO；详细契约见
`SYNC_IO_ARCHITECTURE.md:ARCH-IOANALYZER-01`。

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

TX 与 RX PIO block 在 block 层可以包含输入和输出，但不能由此推导“每个 SM
单方向”或“每个 SM 都同时配置 IN/OUT”。`CLK` 和 `SYNC/CS` 从 TX→RX，`DATA` 从
RX→TX；每个 persona role 必须单独声明 pin 指令方向、FIFO owner 和 DMA/DREQ owner。
control/capture/evidence SM 可以保持单方向；follower DATA SM 则必须在同一硬件循环中
采样并转发 DATA。禁止的是未声明的 `master/slave` 复合协议语义和 FIFO 双消费者，
不是合法且已声明的 `in pins`/`out pins` 组合。

这种拆分带来的直接收益是：

- combined control、DATA flight/capture 和 evidence 角色使用固定槽位，避免旧复合
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

`flight_rx_unload` 与 `flight_tx_load` 是目标架构中的逻辑控制边界，不是对当前 C API
名称的声明。当前实现仍以 `tdma_flight_engine_inspect_input()`、
`tdma_flight_engine_commit_input()` 和 `tdma_flight_engine_apply*()` 完成输入识别、提交和
局部 overlay。后续可以拆出方向化接口，但无论接口是否物理拆分，两个方向都必须能在
同一个 Core1 cycle 内推进，不存在“先完成 RX 才允许 TX”或“TX 失败阻塞 RX”的隐式
依赖。

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

flight 角色名称和槽位以 `board_config.h` 的 `BOARD_TDMA_*` persona role 符号为事实源：

| 角色符号 | PIO 域 | FIFO/DMA 关系 | 允许的实时操作 |
|---|---|---|---|
| `BOARD_TDMA_TX_CONTROL_OUT_SM` | TDMA TX | TX control word；不消费业务 RX FIFO | origin 产生或 follower 转发同一组 CLK+SYNC 波形 |
| `BOARD_TDMA_TX_RTT_EVIDENCE_SM` | TDMA TX | 独立 seed/evidence FIFO | origin 关联本地与返回 CS 边沿，不参与 flight admission |
| `BOARD_TDMA_TX_CLOCK_LATCH_SM` | TDMA TX | 独立 hardware tick RX FIFO | origin 捕获 control 边沿，不消费 DATA |
| `BOARD_TDMA_TX_DATA_CAPTURE_SM` | TDMA TX | 唯一返回 DATA capture RX FIFO/DMA | origin 连续采样返回 DATA |
| `BOARD_TDMA_RX_DATA_FLIGHT_SM` | TDMA RX | origin 使用 TX FIFO/DMA；follower 使用同一 SM 的 command TX 与业务 RX FIFO/DMA | origin 输出 DATA；follower 同拍采样、延迟转发并发布本地卸载字节 |
| `BOARD_TDMA_RX_CLOCK_LATCH_SM` | TDMA RX | 独立 hardware tick RX FIFO | follower 捕获输入 control 边沿，不消费 DATA |
| SYNC_IO/SMA persona role | Realtime Observation / SYNC_IO/SMA PIO | persona 私有 FIFO/DMA | 输入捕获、只读逻辑分析或输出波形；只运行已获 owner 且资源兼容的 persona |

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

上面的资源生命周期与 TDMA resident cycle 生命周期是两层不同的状态：资源只在
`ARM/DISARM` 边界申请和释放，而 `RUNNING` 内的 cycle boundary 周而复始。显式重新
配置必须先让 resident loop 进入 quiesce/STOPPED，再重新经过 `STAGED -> ARMED ->
RESIDENT_INIT`，不能在运行中替换 active process image 或临时改变资源归属。

资源冲突、方向不符、DREQ/FIFO 不一致、程序装载失败、DMA stall 或 evidence 无法关联
时，状态机域必须 fail-closed：停止相关 persona、保留故障计数和 snapshot，不降级为
旧复合 TX/RX 路径。旧 persona 与新方向 persona 不得在同一个 RUN epoch 混用。

## 跨域接口与验证映射

| 接口 | 事实源/调用者 | 状态机域保证 |
|---|---|---|
| TDMA TX/RX resource contract | `tdma_foundation_profile_t` / TDMA owner | PIO、SM、DMA、GPIO 方向和互斥关系一致 |
| SYNC_IO/SMA persona | `sync_io` / Calibration/Trigger/diagnostic requester | 不占用 TDMA TX/RX PIO；逻辑分析仪只读取 TDMA GPIO pad |
| Hardware timestamp evidence | TDMA PIO/DMA -> VDC gate | 只接受硬件 tick、sequence 和 active matrix 一致的记录 |
| Runtime snapshot | core1 owner -> core0/SCPI | 多字段使用既有 guarded/seqlock 发布约束 |

验证必须同时覆盖静态资源冲突、PIO 指令方向、DMA/DREQ 归属、STOP/ARM 生命周期、
follower forward/capture 不共享 FIFO，以及四节点 TDMA 与 NO5 只读观测。host/build
通过不能替代 OTA、HIL 和原始波形证据。还必须验证一次 `RESIDENT_INIT` 后持续 cycle、
同一轮多 Node 的 `LOCAL_UNLOAD/LOCAL_LOAD`、无更新透传，以及 `FRAME_COMPLETE` 不终止
`RUNNING`。

maintenance/calibration persona 仍保留旧 `BOARD_TDMA_SPI_PIO` 及
`MASTER_SM/SLAVE_SM` 复合实现；flight persona 已按本契约开始使用 TX/RX 两个 PIO
block。两者不得在同一个 RUN epoch 混用。只有全部 flight persona、资源仲裁、双路径
FIFO/DMA 和 OTA/HIL 通过 `HAOFV_STATE_MACHINE_TODO.md` 退出门禁后，才可将运行时迁移
标记为已实现。
