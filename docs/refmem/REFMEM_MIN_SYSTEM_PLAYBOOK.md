# RefMem 最小系统板 Bring-up 记录

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
Related: `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`, `docs/hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md`, `docs/sync/SYNC_IO_RESOURCE_PLAN.md`, `tools/two_board_io_validate/README.md`
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

当前已验证固件：

| 项目 | 值 |
|---|---|
| build id | `20260814083032` |
| package CRC | `0x2071E6F5` |
| SlotClaimMap CRC | `386979554` |

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
- 两板具备 `REFMEM + VDC` baseline。
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

## 执行日志

### 2026-08-14

- 两板 RefMem baseline 验证通过。
- 两板 IO line-order 验证通过。
- 当前 logical remap 记录为 B0->B1: `1,2,0,3`，B1->B0: `2,1,0,3`。
- 后续进入 P4.5 阶段 1：PIO SPI adapter skeleton。
- 新增 `GPIO4..7` 最小模型系统 overlay 规划：X 板承载 `A1/A2/A3`，Y 板承载 `A4/A5`；其中 Y 板槽位已整体向后挪，避免与 X 板 `A3` 链路控制冲突。

## 注意事项

- 当前最小系统 bring-up 不代表产品板隔离、电源、ESD、连接器和差分链路已验证。
- PIO SPI adapter 是最小验证载体，不是最终通讯绑定。
- 后续迁移 BISS-C、RJ45、UART 或 RS485 时，必须复用 `REFMEM_SYNC_ARCHITECTURE.md` 的协议语义和 quality/fence 解释。
- 串口脚本不得并行占用同一端口。
- board-facing 改动需要小步验证，避免问题积累。
