# RefMem 最小系统板 Bring-up 记录

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
Related: `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`, `docs/hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md`, `docs/sync/SYNC_IO_ARCHITECTURE.md`, `tools/two_board_io_validate/README.md`
Last updated: 2026-08-14

本文档记录当前两块最小系统板上的 RefMem / VDC / PIO transport bring-up 操作。它不是架构约束，也不是产品板 pin map；架构定义以 `REFMEM_DOMAIN_ARCHITECTURE.md` 和 `REFMEM_SYNC_ARCHITECTURE.md` 为准。

## 当前目标

当前 bring-up 目标是用最简单的 PIO SPI 风格 adapter，验证 RefMem Sync Protocol 的最小两板闭环：

```text
Board A publishes HELLO / EPOCH / DELTA
  -> PIO SPI adapter carries frame
  -> Board B validates and commits mirror
  -> Board B snapshot visible
  -> Board B returns ACK_NACK / QUALITY
```

这一步不要求 BISS-C 跑通，也不要求最终产品通讯链路定型。BISS-C、RJ45、UART、RS485 后续都应作为 adapter 迁移目标复用同一 RefMem Sync Protocol。

## 当前板卡与端口

当前双板调试端口：

| 板卡 | 端口 | 识别信息 |
|---|---|---|
| Board A / B0 | `COM3` | `GTS,DTC100,91274BA197662714,0.1.0` |
| Board B / B1 | `COM4` | `GTS,DTC100,73E940D75B406BCD,0.1.0` |

2026-08-14 当前 HIL 端口更新为：

| 板卡 | 端口 | 逻辑 slot |
|---|---|---:|
| Board A | `COM5` | `0` |
| Board B | `COM6` | `1` |

当前已验证固件：

| 板卡 | build id | package CRC | 说明 |
|---|---|---|---|
| COM3 / Board A | `20260814112552` | `0xD7B2581D` | 功能 AO 模板化与 `ModelTurntableAO` 可加载实例验证版。 |
| COM4 / Board B | `20260814104920` | `0x2DF62B6E` | GPIO4..7 overlay 方向 HIL 已验证参考版。 |
| COM5 / Board A | `20260814133439` | `0x1926CA52` | RefMem Sync HELLO/EPOCH SCPI 搬运闭环通过。 |
| COM6 / Board B | `20260814133439` | `0x1926CA52` | RefMem Sync HELLO/EPOCH SCPI 搬运闭环通过。 |
| COM5 / Board A | `20260814134858` | `0xA776513E` | RefMem Sync HELLO/EPOCH/DELTA/MIRROR SCPI 搬运闭环通过。 |
| COM6 / Board B | `20260814134858` | `0xA776513E` | RefMem Sync HELLO/EPOCH/DELTA/MIRROR SCPI 搬运闭环通过。 |
| COM5 / Board A | `20260814141201` | `0xA1E002E6` | RefMem Sync HELLO/EPOCH/DELTA/MIRROR/ACK_NACK SCPI 搬运闭环通过。 |
| COM6 / Board B | `20260814141201` | `0xA1E002E6` | RefMem Sync HELLO/EPOCH/DELTA/MIRROR/ACK_NACK SCPI 搬运闭环通过。 |
| COM5 / Board A | `20260814142748` | `0xE3F75DB7` | RefMem Sync HELLO/EPOCH/DELTA/MIRROR/ACK_NACK/FENCE SCPI 搬运闭环通过。 |
| COM6 / Board B | `20260814142748` | `0xE3F75DB7` | RefMem Sync HELLO/EPOCH/DELTA/MIRROR/ACK_NACK/FENCE SCPI 搬运闭环通过。 |
| COM5 / Board A | `20260814143942` | `0x4D1483AE` | RefMem Sync HELLO/EPOCH/DELTA/MIRROR/ACK_NACK/FENCE/QUALITY SCPI 搬运闭环通过。 |
| COM6 / Board B | `20260814143942` | `0x4D1483AE` | RefMem Sync HELLO/EPOCH/DELTA/MIRROR/ACK_NACK/FENCE/QUALITY SCPI 搬运闭环通过。 |

当前 COM5/COM6 查询到的 SlotClaimMap CRC 为 `386979554`。

## 当前 IO Profile

当前 active IO profile：

| 方向 | GPIO 组 |
|---|---|
| 输入组 | `GPIO16..19` |
| 输出组 | `GPIO21..24` |

构建参数：

| 参数 | 当前默认值 |
|---|---:|
| `PROJECT_SYNC_IO_INPUT_BASE_PIN` | `16` |
| `PROJECT_SYNC_IO_OUTPUT_BASE_PIN` | `21` |

约束：

- 输入组和输出组必须是连续 4 个 GPIO。
- 输入组和输出组不得重叠。
- 调试线序属于最小系统 profile，不写入产品板约束。
- `GPIO12..15` 避开 SD/SPI 调试资源。
- `GPIO4..7` 可作为最小模型系统 debug overlay；该 overlay 独立于 `GPIO16..19` / `GPIO21..24` 的 RefMem/PIO adapter 主链路。
- 最小系统板 UART 不启用；`GPIO4/5` 虽然在 `board_config.h` 中保留 UART1 兼容定义，但默认构建不得初始化 UART1。
- 两块板必须共地。

## GPIO4..7 最小模型系统 Overlay

当前两块最小系统板新增 `GPIO4..7` 直连线，用于模拟一个小型分布式测试系统。该 overlay 的目标是先验证“业务节点 -> 硬实时事件 -> VDC/RefMem -> 虚拟仪表”的闭环，不替代后续 PIO SPI / BISS-C / RJ45 transport adapter。

槽位采用全系统唯一 A0-A7 逻辑编号。按“Y 板向后挪一个槽位”后的当前模型分配如下：

| 板卡 | 逻辑槽位 | 加载实例 | 主要职责 |
|---|---|---|---|
| X 板 | `A1` | 模拟转台节点 | 按扫描/断点角度向外输出位置脉冲。 |
| X 板 | `A2` | 模拟网分节点 | 接收网分 TRIG，输出虚拟 READY。 |
| X 板 | `A3` | 链路控制节点 | 基于同步时钟预约输出链路切换事件。 |
| Y 板 | `A4` | 脉冲分发节点 | 捕获转台脉冲，经总线/VDC 形成同步时间事实并发布给 X 板。 |
| Y 板 | `A5` | VNA 网关节点 | 捕获链路切换事件，触发虚拟网分，等待 READY。 |

`GPIO4..7` 当前建议线序如下：

| GPIO | 输出 owner | 输入 receiver | 模拟信号 | 说明 |
|---:|---|---|---|---|
| `GPIO4` | X 板 `A1` 模拟转台 | Y 板 `A4` 脉冲分发 | `TURN_POS_PULSE` | 位置/角度脉冲输入，是 Y 板建立事件时间和 VDC 发布的起点。 |
| `GPIO5` | X 板 `A2` 模拟网分 | Y 板 `A5` VNA 网关 | `VNA_READY` | 虚拟网分完成后返回 ready。 |
| `GPIO6` | Y 板 `A5` VNA 网关 | X 板 `A2` 模拟网分 | `VNA_TRIG` | VNA 网关向虚拟网分发起一次触发。 |
| `GPIO7` | X 板 `A3` 链路控制 | Y 板 `A5` VNA 网关 | `LINK_SWITCH` | 链路控制节点按 VDC 预约时刻发出开关切换事件。 |

约束：

- 每根线必须只有一个输出 owner；未获得 owner 的同名 GPIO 必须保持输入或高阻。
- `GPIO4..7` 在 RP2350 上可由 PIO 使用，但当前 overlay 仍需由资源仲裁器和 board profile 显式启用。
- `GPIO4/5` 与 UART1 TX/RX 兼容定义冲突；最小系统板默认 `PROJECT_ENABLE_UART_STDIO=OFF`，不得在该 overlay 运行时启用 UART1。
- 该 overlay 是业务模型验证线束，不代表产品板 pin map，也不改变 A0-A7 通用槽位的动态装载原则。
- 表中的 A1/A2/A3/A4/A5 是当前测试实例选择，不是默认固化绑定；功能节点应由 SCPI 或 SD System Pack staging 显式加载并通过 DeploymentGate 激活。

方向安全预检工具：

```powershell
python tools\debug_model_overlay_validate\debug_model_overlay_validate.py --port-x COM3 --port-y COM4
```

该工具使用 `REALtime:IO:MODel:*` 维护接口，运行前后都会 release 双方 `GPIO4..7`，并逐线验证当前 owner 方向。

## 当前实测线序

当前 COM3/COM4 双板实测 logical remap：

| 方向 | 线序定义 |
|---|---|
| B0 -> B1 | `OUT0->IN1, OUT1->IN2, OUT2->IN0, OUT3->IN3` |
| B1 -> B0 | `OUT0->IN2, OUT1->IN1, OUT2->IN0, OUT3->IN3` |

如果后续重接为直通线序，可以用工具显式指定：

```powershell
python tools\two_board_io_validate\two_board_io_validate.py --port-a COM3 --port-b COM4 --expect-a-to-b 0,1,2,3 --expect-b-to-a 0,1,2,3
```

## 已有验证

### RefMem baseline

```powershell
python tools\refmem_network_validate\refmem_network_validate.py --port-a COM3 --port-b COM4 --out-dir build-rtos-multicore-smoke\refmem_network_COM3_COM4
```

已验证：

- 两板 `*IDN?` 可通信。
- 两板 build id 一致。
- 两板具备 `REFMEM + VDC + TDMA` baseline。
- SlotClaim gate ready。
- 默认 evidence 为空。
- SlotClaimMap CRC 一致。

### IO line order

```powershell
python tools\two_board_io_validate\two_board_io_validate.py --port-a COM3 --port-b COM4 --out-dir build-rtos-multicore-smoke\two_board_io_COM3_COM4_remap
```

已验证：

- 工具能管理两个串口生命周期。
- 输出逐 bit 驱动、对端输入 mask 读取正常。
- 当前 measured remap 可以稳定通过。

## PIO SPI Adapter Bring-up 待办入口

最小系统板 PIO SPI adapter 的阶段规划统一维护在 `REFMEM_DOMAIN_TODO.md` 的 `P4.5 - 最小系统板 PIO SPI Adapter Bring-up` 中。本文只记录当前实际端口、线序、已验证结果和后续执行日志，避免把一次性操作细节写进架构文档。

当前执行顺序以 TODO 为准：

| 阶段 | 目标 |
|---|---|
| 阶段 0 | 线序、串口生命周期和输出 release 稳定。 |
| 阶段 1 | adapter skeleton 和 caps/counter snapshot。 |
| 阶段 2 | `REFMEM_HELLO` / `REFMEM_EPOCH` 双向对齐。 |
| 阶段 3 | `REFMEM_DELTA` mirror、snapshot visible 和 `ACK_NACK`。 |
| 阶段 4 | `REFMEM_FENCE` 与 `QUALITY` 闭环。 |

## HELLO/EPOCH SCPI 搬运闭环

阶段 2 先通过 SCPI 维护面搬运 RefMem Sync frame，验证协议状态机和两板 peer/quality 对齐；这不是最终物理链路，也不绕过 HAOFV 修改 active fact。真实 PIO SPI adapter 接入后，应复用同一帧格式和接收状态机。

板端维护入口：

| 命令 | 作用 |
|---|---|
| `SYSTem:REFMEM:SYNC:INITialize <local_slot>,<epoch>,<run>` | 初始化本板 sync context 和 PIO SPI adapter skeleton。 |
| `SYSTem:REFMEM:SYNC:HELLo? <source_slot>,<target_mask>,<seq>` | 生成 HELLO frame，返回 frame size、header 摘要和 hex frame。 |
| `SYSTem:REFMEM:SYNC:EPOCh? <source_slot>,<target_mask>,<seq>` | 生成 EPOCH frame，返回 frame size、header 摘要和 hex frame。 |
| `SYSTem:REFMEM:SYNC:DELTa? <source_slot>,<target_mask>,<seq>,<slot_id>,<slot_seq>,<field_id>,<value>,<dirty_mask>` | 生成最小 u32 DELTA test field frame。 |
| `SYSTem:REFMEM:SYNC:ACK? <source_slot>,<target_mask>,<seq>` | 基于本板最近一次 RX snapshot 生成 ACK_NACK frame，返回 frame 摘要、被确认 seq、RX result 和 hex frame。 |
| `SYSTem:REFMEM:SYNC:FENCe? <source_slot>,<target_mask>,<seq>,<fence_seq>,<scope>,<required_mask>,<min_table_seq>,<deadline_us>` | 生成最小 FENCE frame，用于验证 required 本地 slot 对指定 source mirror 的 visible/min seq 判定。 |
| `SYSTem:REFMEM:SYNC:QUALity:FRAMe? <source_slot>,<target_mask>,<seq>,<quality_id>,<scope>,<target_slot>` | 生成 QUALITY frame，把本地 receive quality counter 和指定 peer seq 摘要发布给对端。 |
| `SYSTem:REFMEM:SYNC:RX "<hex>"` | 将 hex frame 注入 adapter RX staging，poll 后送入 `refmem_sync_receive_frame()`。 |
| `SYSTem:REFMEM:SYNC:MIRRor? <source_slot>` | 查询指定来源 slot 的最新 sync mirror snapshot。 |
| `SYSTem:REFMEM:SYNC:ACK:STATus? <source_slot>` | 查询指定来源 slot 最近一次 ACK_NACK snapshot。 |
| `SYSTem:REFMEM:SYNC:FENCe:STATus? <source_slot>` | 查询指定来源 slot 最近一次 FENCE snapshot。 |
| `SYSTem:REFMEM:SYNC:QUALity:STATus? <source_slot>` | 查询指定来源 slot 最近一次 remote QUALITY snapshot。 |
| `SYSTem:REFMEM:SYNC:PEER? <source_slot>` | 查询指定对端的 seen、HELLO、EPOCH、seq、drop/stale 状态。 |
| `SYSTem:REFMEM:SYNC:QUALity?` | 查询本板 sync 接收质量计数。 |
| `SYSTem:REFMEM:SYNC:ADAPter?` | 查询 adapter caps/counter snapshot。 |

两板脚本：

```powershell
python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM3 --port-b COM4 --slot-a 0 --slot-b 1 --epoch 1 --run 1
```

带线序预检和报告元数据：

```powershell
python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814143942 --package-crc 0x4D1483AE --line-remap-a-to-b 1,2,0,3 --line-remap-b-to-a 2,1,0,3 --preflight-io
```

如果固件切到 USBTMC，可改用 VISA resource：

```powershell
python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --visa-a USB::... --visa-b USB::... --slot-a 0 --slot-b 1 --epoch 1 --run 1
```

## Physical PIO SPI Adapter HIL

真实 PIO/SPI 物理链路验证使用 `tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py`。该脚本不再由 PC 搬运 RefMem frame hex；PC 只负责在接收侧启动 RX，在发送侧触发 TX，frame 必须经过板间 PIO+DMA 物理链路。

当前 debug bring-up profile 使用四根输出线到对端四根输入线，线序必须由 `two_board_io_validate.py` 实测，不假设固定直通：

| 方向 | 当前实测 remap |
|---|---|
| COM5 A -> COM6 B | `OUT0->IN1, OUT1->IN2, OUT2->IN0, OUT3->IN3` |
| COM6 B -> COM5 A | `OUT0->IN2, OUT1->IN1, OUT2->IN0, OUT3->IN3` |

由当前 remap 推导的 PIO SPI pin plan：

| 方向 | Master CS | Master SCK | Master TX | Slave RX | Slave CS | Slave SCK |
|---|---:|---:|---:|---:|---:|---:|
| A -> B | GPIO21 | GPIO22 | GPIO23 | GPIO16 | GPIO17 | GPIO18 |
| B -> A | GPIO22 | GPIO21 | GPIO23 | GPIO16 | GPIO17 | GPIO18 |

推荐执行：

```powershell
python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6
```

脚本分三层验证：

```text
line preflight:
  REALtime:IO:PROFile?
  two_board_io_validate auto remap
  derive PIO SPI pin plan from observed OUT->IN map
raw byte:
  A RAW:TX -> B RAW:RX?
  B RAW:TX -> A RAW:RX?
RefMem frame:
  HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY
```

2026-08-15 记录：

- build `20260815022459` 使用固定硬件 SPI/直通线序假设，在线级 preflight 失败；该结果证明不能假设 GPIO16-19 直连。
- build `20260815025153` 短暂改成 SIO bitbang 诊断后被纠偏：该路径不符合 VDC TDMA/PIO 架构，不作为闭环结论。
- build `20260815031915` 使用 PIO TX、PIO RX 和 DMA 接收缓冲，COM5/COM6 均 OTA commit。
- `build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815031915_pio_dma_1m` 通过，证明 PIO+DMA 帧链路稳定。
- `build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815031915_pio_dma_25m` 通过，25 MHz 下完成 `RAW/HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 双向闭环。

当前仍未完成的产品化项：把维护命令触发式 TX/RX 升级为 core1 realtime TDMA service。最终运行态应由 core0 配置窗口和帧意图，core1/PIO/DMA 在 TDMA window 内自循环，SCPI 只读取 snapshot 和 quality evidence。

脚本执行顺序：

```text
optional IO preflight:
  REALtime:IO:PROFile?
  REALtime:IO:OUTPut:MASK 0
  REALtime:IO:OUTPut:RELease
  two_board_io_validate line remap check
A INIT(slot 0, epoch 1, run 1)
B INIT(slot 1, epoch 1, run 1)
A HELLO? -> B RX
B HELLO? -> A RX
A EPOCH? -> B RX
B EPOCH? -> A RX
A DELTA? -> B RX -> B MIRROR?
B DELTA? -> A RX -> A MIRROR?
A ACK? for B DELTA
B ACK? for A DELTA
B duplicate A DELTA -> B ACK? NACK duplicate -> A RX -> A ACK:STATus?
B RX A ACK -> B ACK:STATus?
A target-mismatch DELTA -> B RX rejected -> B ACK? NACK target -> A RX -> A ACK:STATus?
A payload-CRC-bad DELTA -> B RX rejected -> B ACK? NACK CRC -> A RX -> A ACK:STATus?
A FENCE? -> B RX -> B FENCE:STATus?
B FENCE? -> A RX -> A FENCE:STATus?
A FENCE? min seq too high -> B RX -> B FENCE:STATus? failed/timeout
A QUALITY:FRAME? -> B RX -> B QUALITY:STATus?
B QUALITY:FRAME? -> A RX -> A QUALITY:STATus?
A PEER?(B), B PEER?(A), A/B QUALITY?
```

通过条件：

- 两板 `RX` 都返回 `ACCEPTED`。
- 两板 peer 均 `hello_seen=1` 且 `epoch_seen=1`。
- 两板 mirror 均 `visible=1`，`value_u32` 等于对端发送值。
- 两板 quality 中 `accepted_count>=3`，`bad_frame_count/crc_error_count/target_mismatch_count/epoch_mismatch_count=0`。
- target mask 必须包含接收板 local slot，否则应被 `TARGET_MISMATCH` 拒绝。
- EPOCH 必须匹配接收板 active epoch/run，否则应被 `EPOCH_MISMATCH` 拒绝。
- ACK/NACK 正向 ACK 的 `delta_seq32` 必须指向被确认的 DELTA seq；duplicate、target mismatch、payload CRC mismatch 必须分别回传 NACK reason。
- `ACK?` 是维护 bridge 调试命令，基于本板最近一次 RX snapshot 生成确认帧；双向 ACK 验证时必须先生成双方 ACK frame，再互相注入，避免 ACK 帧覆盖 DELTA 的 `last_rx`。
- FENCE pass 要求接收板 local slot 包含在 `required_mask` 中，且对应 source mirror 已 visible，`last_frame_seq32 >= min_table_seq`。
- FENCE fail/timeout 当前只写入 fence snapshot，不触发产品 RUN gate 或 distributed fault latch；真实门禁接入后再由 DeploymentGate/QualityTable 消费。
- QUALITY frame 当前同步的是本地 receive quality counter 和 peer seq 摘要，接收端只写 remote quality snapshot；写入 `DistributedConnectionQualityTable` 仍是后续步骤。
- 带 `--preflight-io` 时，脚本会先运行 `two_board_io_validate.py`，完成 `REALtime:IO:PROFile?` 查询、双板输出 release、逐线 remap 检查和退出清理，再重新打开串口进入 RefMem Sync 协议验证。

## 执行日志

### 2026-08-14

- 两板 RefMem baseline 验证通过。
- 两板 IO line-order 验证通过。
- 当前 logical remap 记录为 B0->B1: `1,2,0,3`，B1->B0: `2,1,0,3`。
- 后续进入 P4.5 阶段 1：PIO SPI adapter skeleton。
- 新增 `GPIO4..7` 最小模型系统 overlay 规划：X 板承载 `A1/A2/A3`，Y 板承载 `A4/A5`；其中 Y 板槽位已整体向后挪，避免与 X 板 `A3` 链路控制冲突。
- 新增 `REALtime:IO:MODel:*` 维护接口和 `tools/debug_model_overlay_validate` 方向安全预检工具。
- COM3/COM4 均更新到 build `20260814104920`，package CRC `0x2DF62B6E`。
- `python tools\debug_model_overlay_validate\debug_model_overlay_validate.py --port-x COM3 --port-y COM4 --out-dir build-rtos-multicore-smoke\debug_model_overlay_COM3_COM4` 通过。
- overlay HIL 已验证：`GPIO4` X->Y 位置脉冲、`GPIO5` X->Y READY、`GPIO6` Y->X TRIG、`GPIO7` X->Y LINK_SWITCH。
- COM3 更新到 build `20260814112552`，package CRC `0xD7B2581D`，用于验证功能 AO 模板化和 `ModelTurntableAO` 可加载实例。
- COM3 验证默认未加载：`READ:MODEl:TURNtable:LOAD?` 返回 `0,4294967295,0`。
- COM3 验证未加载启动被拒绝：直接 `MODEl:TURNtable:STARt` 后，`SYSTem:ERRor?` 返回 `-200,"Execution error"`。
- COM3 验证临时加载到 slot 1/output 0：`CONFigure:MODEl:TURNtable:LOAD 1,0` 后 `READ:MODEl:TURNtable:LOAD?` 返回 `1,1,0`，配置触发和运动参数后 `STARt/STOP` 均返回 `"OK"`。
- 新增 `SYSTem:REFMEM:SYNC:*` 维护入口，可在两板之间通过 SCPI 搬运 HELLO/EPOCH hex frame；该入口只操作 sync context、adapter skeleton 和 peer/quality snapshot，不直接改 active RefMem 表。
- 新增 `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py`，固化两板 HELLO/EPOCH 搬运验证流程，支持 USB CDC COM 口和 USBTMC VISA resource。
- COM5/COM6 均 OTA 并 commit 到 build `20260814133439`，package CRC `0x1926CA52`。
- `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --out-dir build-rtos-multicore-smoke\refmem_sync_hil_COM5_COM6_20260814133439` 通过。
- HIL 结果：两板 HELLO/EPOCH `RX` 均返回 `ACCEPTED`；A peer slot 1 与 B peer slot 0 均 `hello_seen=1, epoch_seen=1, frame_count=2`；两板 quality `accepted_count=2`，`bad_frame_count/crc_error_count/target_mismatch_count/epoch_mismatch_count=0`。
- 新增最小 DELTA mirror：`refmem_sync` 接收 `REFMEM_DELTA` 后提交到按 source slot 索引的 sync mirror snapshot，记录 slot、field、slot_seq、u32 value、payload CRC、frame seq、committed/visible count；不写 active ApplicationModel 或 SlotClaimMap。
- 新增 `SYSTem:REFMEM:SYNC:DELTa?` 和 `SYSTem:REFMEM:SYNC:MIRRor?`，HIL 脚本扩展为 HELLO/EPOCH/DELTA/MIRROR 全流程，并补充 build、SlotClaimMap CRC 与 adapter snapshot 预检。
- COM5/COM6 均 OTA 并 commit 到 build `20260814134858`，package CRC `0xA776513E`。
- `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814134858 --out-dir build-rtos-multicore-smoke\refmem_sync_delta_hil_COM5_COM6_20260814134858_report` 通过。
- HIL 结果：26 条记录全部 PASS；两板 build id 均为 `20260814134858`；SlotClaimMap CRC 均为 `386979554`；adapter id 均为 `1`；A->B DELTA value `2768240641`、B->A DELTA value `3053453314` 均在对端 `MIRRor?` 可见；两板 quality `accepted_count=3` 且 frame/CRC/target/epoch 错误计数为 0。
- 新增 `REFMEM_ACK_NACK` receive snapshot 和 `SYSTem:REFMEM:SYNC:ACK?` / `ACK:STATus?` 维护入口；`RX` 对 payload CRC 错误会保留可解码 header，用于生成 NACK 证据。
- COM5/COM6 均 OTA 并 commit 到 build `20260814141201`，package CRC `0xA1E002E6`。
- `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814141201 --out-dir build-rtos-multicore-smoke\refmem_sync_ack_hil_COM5_COM6_20260814141201_r3` 通过。
- HIL 结果：46 条记录全部 PASS；两板 build id 均为 `20260814141201`；SlotClaimMap CRC 均为 `386979554`；正向 ACK 覆盖 A->B 与 B->A DELTA seq `3`；NACK 覆盖 duplicate seq reason `6`、target mismatch reason `4`、payload CRC mismatch reason `9`；ACK/NACK frame 均被对端接收并可由 `ACK:STATus?` 查询。
- 新增 `REFMEM_FENCE` receive snapshot 和 `SYSTem:REFMEM:SYNC:FENCe?` / `FENCe:STATus?` 维护入口；接收侧根据 source mirror visible 和 `min_table_seq` 生成 pass/fail/timeout snapshot。
- COM5/COM6 均 OTA 并 commit 到 build `20260814142748`，package CRC `0xE3F75DB7`。
- `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814142748 --out-dir build-rtos-multicore-smoke\refmem_sync_fence_hil_COM5_COM6_20260814142748` 通过。
- HIL 结果：55 条记录全部 PASS；A->B 和 B->A FENCE 在对端 mirror visible 且 `min_table_seq=3` 时 passed；A->B `min_table_seq=99,deadline_us=0` 时 failed，`missing_mask=2,timed_out=1,last_reason=3`。
- 新增 `REFMEM_QUALITY` remote snapshot 和 `SYSTem:REFMEM:SYNC:QUALity:FRAMe?` / `QUALity:STATus?` 维护入口；接收侧按 source slot 记录 quality id、scope、peer seq、CRC/stale/drop/timeout 和 last error。
- COM5/COM6 均 OTA 并 commit 到 build `20260814143942`，package CRC `0x4D1483AE`。
- `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814143942 --out-dir build-rtos-multicore-smoke\refmem_sync_quality_hil_COM5_COM6_20260814143942` 通过。
- HIL 结果：61 条记录全部 PASS；A->B QUALITY snapshot 记录 `seq_expected=9,seq_last=8` 且错误计数为 0；B->A QUALITY snapshot 记录 `seq_expected=10,seq_last=9,crc_error_count=1,drop_count=2,last_error=9`。
- `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814143942 --package-crc 0x4D1483AE --line-remap-a-to-b 1,2,0,3 --line-remap-b-to-a 2,1,0,3 --preflight-io --out-dir build-rtos-multicore-smoke\refmem_sync_report_hil_COM5_COM6_20260814143942` 通过。
- 带预检报告结果：`io_preflight` PASS；B0->B1 remap `[1,2,0,3]`，B1->B0 remap `[2,1,0,3]`；完整 RefMem Sync HIL 61 条记录全部 PASS，报告记录 package CRC `0x4D1483AE`。

## 注意事项

- 当前最小系统 bring-up 不代表产品板隔离、电源、ESD、连接器和差分链路已验证。
- PIO SPI adapter 是最小验证载体，不是最终通讯绑定。
- 后续迁移 BISS-C、RJ45、UART 或 RS485 时，必须复用 `REFMEM_SYNC_ARCHITECTURE.md` 的协议语义和 quality/fence 解释。
- 串口脚本不得并行占用同一端口。
- board-facing 改动需要小步验证，避免问题积累。
