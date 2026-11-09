# REFMEM 统一控制面：命令与代码指南

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_UNIFIED_CONTROL_COMMANDS.md`
Related: `docs/check/DOCS_REGISTRY.md`（`REFMEM-READ-01`）、`docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`、`docs/interface/SCPI_COMMANDS.md`、`config/refmem/command_catalog.yaml`、`config/refmem/read_catalog.yaml`
Last updated: 2026-11-09

## 1. 边界

- 唯一写入口是 `SYSTem:CTRL:EXECute`；开关的旧入口 `CONFigure:SWITch#` 已废弃，返回
  `-200,"USE_CTRL_EXECUTE"`。
- 远端（跨板）命令要求环的维护传输在线，且**只有控制 owner** 可发起；
  非 owner 返回 `-200,"NOT_CONTROL_OWNER"`，环不在线返回 `-200,"NOT_ONLINE_SAFE"`。
- 读面 `SYSTem:REFMEM:OBS?` 只读本地镜像/接收镜像，不做同步跨板读；对端无发布时
  `valid=0/age=0xFFFFFFFF/stale=1`。
- 本文的字段数、窗口值是**快照**；以代码/`config/refmem/*.yaml` 为准。

## 2. 命令速查

### 2.1 写（统一入口）

```text
SYSTem:CTRL:EXECute <name|id>,<target_mask>,<required_mask>,<params...>
```

- `name|id`：`config/refmem/command_catalog.yaml` 的 `name` 或数字 id（如 `TRIGGER_SWITCH` / `45`）。
- 约束：`target_mask != 0` 且 `required_mask ⊆ target_mask`，否则 `-200,"BAD_ARGUMENT"`。
- `params` 个数必须等于该命令的 `param_words`（如 `TRIGGER_SWITCH` 为 2：`switch_number,value`）。
- 应答：`QUEUED,<command_seq>`；执行错误为 `-200,"<原因>"`。

### 2.2 读（命令结果）

| 命令 | 字段（快照） |
|---|---|
| `SYSTem:CTRL:STATus?` | `seq,crc32,state,command_seq,target,required,ack,nack,busy,timeout,last_reason,last_reason_slot,last_completed_seq` |
| `SYSTem:COMMand:TARget? <slot>` | `slot,seq,state,type,target,required,ack,nack,last_reason` |

`state`：0 IDLE、1 POSTED、2 TAKEN、3 BUSY、4 ACKED、5 NACKED、6 TIMED_OUT。
`last_reason` 关键值：3 NODE_STALE、6 RESOURCE_BUSY、8 PAYLOAD_CRC、9 EPOCH_MISMATCH、
11 TIMEOUT、12 PERMISSION_DENIED、13 CONFIG_VALIDATION_FAILED（完整表见
`components/distributed_refmem/inc/refmem_command.h`）。

### 2.3 读（观测模型）

```text
SYSTem:REFMEM:OBS? <key>,<offset>,<count>
```

- `key` 1..8；`offset` 是槽位，必须 `< DISTRIBUTED_REFMEM_NODE_COUNT`；`count` 裁剪。
- 应答 = `key,offset,count` + `count` 行，每行
  `slot,valid,epoch,seq,age_ms,stale,<schema words...>`。
- `valid=0`：`age_ms=0xFFFFFFFF`、`stale=1`、值全 0。
- key/schema 见 `config/refmem/read_catalog.yaml`：`switch1/2`、`trigger_state`、`guide_plan`、
  `boot_plan`、`ring_status`、`cal_status`、`node_health`。
- 对端行由 10 B 紧凑帧的字段轮转发布组合；当前 `epoch` 为占位、`seq` 为帧序号、
  `age_ms` 为接收端本地戳，`stale` 窗口为常量（快照 10 s）。

其它只读命令：

| 命令 | 用途 |
|---|---|
| `SYSTem:REFMEM:CLAIM?` / `:CLAIM:EVIDence?` | SlotClaim 表/证据 |
| `SYSTem:REFMEM:NODES? [offset,count]` | 节点在线表 |
| `SYSTem:REFMEM:NODE:CONTrol? [slot]` | 节点命令记录（含 target/required/ack/nack/reason） |
| `SYSTem:REFMEM:CMD:STATus?` | 命令载入/发送计数 |
| `SYSTem:REFMEM:CMD:DISPatch?` | 目标 dispatch 诊断（calls/read_params_fail/broadcast_wait/last_skip） |
| `SYSTem:REFMEM:CTRL:OWNer?` | 派生的控制 owner |

### 2.4 部署（角色绑定）

```text
TRIGger:REServation:DEPloyment:STAGe <gen>,<crc>,<uid,slot,in,out,fb>×4
TRIGger:REServation:DEPloyment:ACTivate <gen>,<crc>
TRIGger:REServation:DEPloyment:CLEar
TRIGger:REServation:DEPloyment?
```

- STOP-only；环运行时 `DEPloyment?` 返回 `1,33`（NOT_IDLE），stage/activate 被拒。
- `crc` 是 `zlib.crc32` 对 LE u32 序列（generation + 每行 uid_lo/uid_hi/slot/in/out/fb）的结果。
- 同 generation 重复 stage 被拒（GENERATION）；换 generation 再 stage。

## 3. 传输与环

| 命令 | 说明 |
|---|---|
| `SYSTem:TDMA:RING:STARt` / `STOP` / `STATus?` | 起/停/读环 |
| `SYSTem:TDMA:OPMode:APPLy` | 应用档位（10 MHz=level 7，10 kHz=level 23） |
| `SYSTem:TDMA:SEAT:ROTation <p>,<f>,<s>` | 席位份额；四板命令分发要求 `s>0`，文档规则 `f>=3`（如 `1,3,1`） |
| `SYSTem:TDMA:LOAD:MASK <mask>` | 负载掩码（不替代维护窗口） |

- OBS 走 process-image，10 kHz bringup 即可验证。
- **广播/远端命令需要维护流**：10 kHz bringup 的维护队列可能停在 `served=2` 后超时；
  广播 HIL 使用 10 MHz 训练环（TRN-03/P3 级）。
- 诊断起环：`trn03_closed_loop.py ... --leave-running --leave-on-failure`
  （`--leave-on-failure` 是诊断开关，默认关闭；失败也保留环）。

## 4. Host 代码指南

### 4.1 `ControlPlane`（`tools/refmem/control_plane.py`）

| 方法 | 说明 |
|---|---|
| `read(key, offset=0, count=0)` | 解析 OBS 逐槽行，返回 `key/offset/count/rows` |
| `write(command_type, params=None, targets=0xF, required=0xF)` | `CTRL:EXECute`，返回 `command_seq` |
| `wait_aggregate(required, timeout_s=60)` | 轮询 `CTRL:STATus?` 直到 required 覆盖；返回 `state/ack/nack/reason` |
| `wait_terminal(slots, timeout_s=60)` | 轮询 `COMMAND:TARget?`（仅本地记录） |
| `query(command)` | 只读查询统一入口；`-200/ERR` 抛 `RejectedError` |

`transport` 只需实现 `execute(command) -> str`。

```python
from control_plane import ControlPlane
from scpi_serial import SerialSession

with SerialSession("COM5", 115200, 1.5, 0.05) as session:
    plane = ControlPlane(session)
    seq = plane.write(45, [1, 4], targets=0xA, required=0xA)  # SWITCH_A/B
    print(plane.wait_aggregate(required=0xA, timeout_s=10))
    print(plane.read(1, 0, 4)["rows"])
```

### 4.2 页面适配（`tools/domains/trigger/trigger_debug_demo/obs_read.py`）

`ControlPlaneTransport` 包装 demo 的 transport：

- `SYSTem:REFMEM:*?` → `ControlPlane.query`；
- `SYSTem:CTRL:EXECute` → 解析后 `ControlPlane.write`，回 `QUEUED,<seq>`；
- 其余命令透传。

页面因此不再直连 REFMEM 路径。

### 4.3 四板 HIL

```text
python tools/refmem/obs_hil_matrix.py --port COM5 --port COM4 --port COM3 --port COM6
```

校验每板本地 `valid=1/stale=0`、对端 stale、schema 宽度，输出 JSON 到 `out/refmem_read/`。

## 5. 固件代码地图

| 文件 | 职责 |
|---|---|
| `components/distributed_refmem/inc/refmem_command.h` | 命令类型/状态/原因/SlotClaim 常量 |
| `middleware/scpi_port/src/scpi_refmem_snapshot_commands.c` | `CTRL:EXECute`、`CTRL:STATus?`、`TARget?`、`OBS?`、诊断命令 |
| `components/distributed_refmem/src/distributed_refmem_command.c` | 目标 dispatch、take/ACK/NACK、node-control 镜像 |
| `components/distributed_refmem/src/refmem_cmd_load.c` | 命令分片发送、重放、超时 |
| `components/distributed_refmem/src/refmem_sync.c` | OBS 字段接收镜像（`(slot,field)`，共享 1 KB）与 stale 时间戳 |
| `components/distributed_refmem/src/distributed_refmem_plane.c` | compact 帧组包、OBS 字段轮转 |
| `application/src/app_tasks.c` | core0 每 ~50 ms 采样 OBS 32 word |
| `components/distributed_refmem/inc/refmem_obs.h` | 行编码/解码 |

## 6. 常见坑

| 现象 | 原因/处置 |
|---|---|
| `BAD_ARGUMENT` | `required ⊄ target`、`target=0` 或 params 个数不符 |
| `NOT_CONTROL_OWNER` | 广播/远端命令不是控制 owner 发起；从 owner 板发 |
| `NOT_ONLINE_SAFE` | 环未起或维护传输未就绪；先起环 |
| `DEPloyment?` 返回 `1,33` | 环运行中；STOP 后再读写部署 |
| 广播后 origin `state 5 / reason 12` | 正常：非开关板 NACK；全 ACK 需把 target/required 指向开关槽（如 `0xA`） |
| 缺板 origin `state 6 / timeout≠0` | fail-closed 的 TIMEOUT；目前不是 NACKED |
| 对端 OBS 恒 stale | 发布器未上线/环未起；确认 `OBS?` 本地行与环状态 |
| 10 kHz 广播停在 QUEUED | 维护队列速率不足；用 10 MHz 训练环（TRN-03/P3） |
| 旧 `CONFigure:SWITch#` 失败 | 已废弃；改用 `CTRL:EXECute` |

## 7. 验收

```text
python tools/hardware_acceptance/workflow_steps.py dpll60 \
    --config config/hardware_acceptance/p3_bench_calibrated.json --scope internal
python tools/hardware_acceptance/workflow_steps.py check-staged
python tools/acceptance/host_acceptance.py run
```

证据目录：`out/refmem_read/`（OBS 矩阵、跨板写读、stale、广播、缺板）。

## 8. 当前状态

`REFMEM-READ-01` 已 **active**。已证：逐槽 OBS、跨板写读、stale、广播 ACK/NACK、缺板
fail-closed、host/页面统一 `ControlPlane`。残余：缺板为 TIMEOUT 而非 NACKED；`seq/age`
仍是发布面占位语义。
