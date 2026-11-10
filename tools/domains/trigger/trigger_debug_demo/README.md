# 触发调试 Demo（单端口 / REFMEM 统一控制面）

一个面向 node-sequence 应用面的本地调试 Demo。它只打开**一个**协调板端口
（串口或 USB TMC），通过 REFMEM 统一控制面控制全部板卡；图形界面和本地 TCP
透传共用同一个会话与同一个调度器。

> 目标态是**所有流程单端口**。尚未 REFMEM 化的 bring-up 步骤允许过渡期多端口，
> 但必须登记迁移路径和退出标准；多端口不是长期产品模式。见 CONSOLE 架构 §6。

```
┌──────────────┐        ┌──────────────────────┐
│ Tk GUI       │──┐     │  Dispatcher（单线程） │
│ 功能模块 tab │  ├────▶│  唯一 transport 所有者│──▶ COMx / USB TMC
│ TCP :5025    │──┘     └──────────────────────┘
└──────────────┘                  │
                                  ▼
                     SYSTem:CTRL:EXECute / REFMEM
                                  │
                    ┌────────┬────────┬────────┬────────┐
                    │ slot 0 │ slot 1 │ slot 2 │ slot 3 │
                    │  A0    │  A1    │  A2    │  A3    │
                    └────────┴────────┴────────┴────────┘
```

## 快速开始

```powershell
# 图形界面 + 模拟四板（无台架也能演示全部功能）
python tools/domains/trigger/trigger_debug_demo/trigger_debug_demo.py --demo

# 图形界面 + 串口
python tools/domains/trigger/trigger_debug_demo/trigger_debug_demo.py --serial COM5

# 图形界面 + USB TMC（PyVISA）
python tools/domains/trigger/trigger_debug_demo/trigger_debug_demo.py --visa USB0::0x2E8A::...::INSTR

# 无界面：只提供 TCP 透传（默认 127.0.0.1:5025）
python tools/domains/trigger/trigger_debug_demo/trigger_debug_demo.py --headless --serial COM5 --tcp-port 5025
```

依赖：`pyserial`（串口）与 `pyvisa` + 本机 VISA runtime（USB TMC）。演示模式不需要硬件。

## 功能模块 tab

| Tab | 内容 |
|---|---|
| 总览 | 设备身份、REFMEM 控制权、触发状态、节点在线、最近命令 |
| 运行控制 | `TRIGger:MODE/STARt/STOP/PAUSe/CONTinue/ABORt` |
| 触发配置 | `CONFigure:TRIGger`（通道/极化/频点/波位） |
| 角度扫描 | `ANGLe:SWEEp/PULSe/BREAkpoint/CLEar/SPEEd` |
| 开关 | `SWITch1`（SP8T 1..8）与 `SWITch2`（SP2T 0/1） |
| 序列计划 | `CONFigure:SEQuence` plan 表、`SEQuence:ACTive`、`REPeat` |
| 状态回读 | `READ:TRIGger/SEQuence/ANGLe/SWITch` 查询 |
| 板卡与 ACK | 目标/必需掩码、聚合 ACK、逐板 `COMMand:TARget?` |
| 统一控制面 | 通用 `CTRL:EXECute`、目录、OWNer、56 B 原始命令帧 |
| 校准引导 | 汇总 + P0–T3 八个动作页；`GUIDE` C/T、`PLAN?`、启环状态（OBS key5/6） |
| OTA | 常规单板 OTA（BEGIN/DATA/END/BOOT/COMMIT）、自动签发凭证、TDMA 流状态（开发中） |
| TCP 透传 | 本地 TCP 服务启停、客户端计数、透传日志 |
| 诊断 | 错误队列、故障、环状态、命令帧/装载统计 |
| 原始命令 | 任意 SCPI 命令行 |

写入以**统一控制面**为唯一入口：

1. **统一控制面**（默认）：`SYSTem:CTRL:EXECute <目录名>,<target_mask>,<required_mask>,<params...>`
   —— 一次提交，目标掩码决定哪些板执行，必需掩码决定聚合完成条件；回读
   `SYSTem:CTRL:STATus?` / `SYSTem:COMMand:TARget?` / `SYSTem:REFMEM:CMD:REPLy?`。
2. **应用层 SCPI**：只保留没有 REFMEM 类型的写（如 `CONFigure:SEQuence:REPeat`）。
   开关的旧入口 `CONFigure:SWITch#` 固件已废弃（返回 `-200,"USE_CTRL_EXECUTE"`），
   界面不再把它当作可选项。

读面分三层：

- **OBS 观测模型**（有 key 的状态）：`SYSTem:REFMEM:OBS? <key>,<offset>,<count>`，
  经 `ControlPlane.read` 返回逐槽行；`switch1/2`、`trigger_state`、`guide_plan`、
  `boot_plan`、`ring_status`、`cal_status`、`node_health` 均走这里。
- **命令结果**：`SYSTem:CTRL:STATus?`（origin 聚合记录）与
  `SYSTem:COMMand:TARget? <slot>`（本地命令记录）。
- **诊断例外**：`GUIDE:PLAN?`、`BOOT:PLAN?`、`READ:*`、`RING:STATus?` 的富字段没有
  OBS key，仍是本地 SCPI；界面按钮标注“本地 / 诊断例外”，不得用于跨板判据。

## TCP 透传协议

- 地址：`127.0.0.1:5025`（可在界面或 `--tcp-port` 修改；传 `0` 用临时端口）。
- 一行一条 SCPI 命令，UTF-8/ASCII，以 `\n` 结束。
- 服务返回一行响应；超时或设备错误返回错误文本。
- `QUIT` / `EXIT` 关闭当前客户端。
- 可多个客户端，但命令经同一 Dispatcher 串行化；**GUI 与 TCP 共用同一个串口或 USB TMC 会话，不会出现两个写者**。
  TCP 客户端提交的统一控制面命令也会在 GUI 的「板卡与 ACK」页同步显示聚合/逐板 ACK。

示例：

```python
import socket
with socket.create_connection(("127.0.0.1", 5025)) as s:
    f = s.makefile("rwb", buffering=0)
    f.write(b"*IDN?\n");            print(f.readline().decode().strip())
    f.write(b"SYSTem:CTRL:EXECute TRIGGER_MODE,15,15,1\n")
    print(f.readline().decode().strip())       # QUEUED,<seq>
    f.write(b"SYSTem:CTRL:STATus?\n"); print(f.readline().decode().strip())
```

## 目录命令与参数编码

`SYSTem:CTRL:CATalog?` 返回 `id,name,type,owner,param_words,param_kind,realtime`。
node-sequence 第一批命令：

| REFMEM 名 | id | words | 参数 |
|---|---:|---:|---|
| `TRIGGER_MODE` | 35 | 1 | mode |
| `TRIGGER_START` | 36 | 4 | plan_id 4×u32（每 word 4 ASCII，零填充） |
| `TRIGGER_STOP` | 37 | 0 | — |
| `TRIGGER_PAUSE` | 38 | 0 | — |
| `TRIGGER_CONTINUE` | 39 | 0 | — |
| `TRIGGER_ABORT` | 40 | 0 | — |
| `TRIGGER_CONFIGURE` | 41 | 4 | chan, pol, freq, wave |
| `TRIGGER_ANGLE_SWEEP` | 42 | 3 | start/stop/step 的 IEEE-754 f32 位模式 |
| `TRIGGER_ANGLE_PULSE` | 43 | 3 | edge, width_us, timeout_ms |
| `TRIGGER_ANGLE_BREAKPOINT` | 44 | 2 | present, angle f32 位模式 |
| `TRIGGER_SWITCH` | 45 | 2 | switch_number, value |
| `TRIGGER_ANGLE_BREAKPOINT_CLEAR` | 46 | 0 | — |
| `TRIGGER_SEQUENCE_ACTIVE` | 47 | 4 | plan_id 4×u32 |
| `TRIGGER_SEQUENCE_PLAN` | 48 | 16 | plan_id 4 + count + 11 个 state id（补零） |
| `TRIGGER_ANGLE_SPEED` | 49 | 1 | speed deg/s 的 f32 位模式 |

界面会自动把角度/速度转成 f32 位模式、把 plan ID 转成 4 个 word；预览框显示实际
发送的命令。应用层命令在“应用层 SCPI”通道下使用十进制/字符串原值。

## OTA

OTA 页提供两种模式：

- **常规 OTA（当前单板，一台一台烧录）**：已实现。选择 `.bin` / `.pkg`，按
  `SYSTem:OTA:BEGin`（包用 `PBEGIN`）→ 分块 `SYST:OTA:DATA` → `END` 传输；到达
  `READY_TO_REBOOT` 后由操作员显式点「重启到新镜像」，新镜像自检通过后再点
  「确认 COMMIT」。不会自动重启。
- **TDMA 流式 OTA（单端口多板）**：固件仍在开发中。本页只开放
  `STReam:STATus?` / `STReam:CAPability?` / `TDMA:DIAG?` 读取，不发起跨板烧录，
  避免在协议未闭环时误刷。

常规 OTA 使用同一个 Dispatcher / 同一个串口或 VISA 会话；OTA 期间 GUI 普通命令和
TCP 透传都会返回 `BUSY OTA`，防止与 `DATA` 块交错。

### 自动签发凭证

勾选「完成后自动签发凭证」（默认开启）后，`END` 完成（成功或失败）会自动在
`out/ota_receipts/` 写入：

- `ota_receipt_<UTC时间>_<board_id>.json`
- 同名 `.json.sha256` 旁路摘要

凭证包含：schema、工具版本、模式、backend/resource、板号/板 ID、镜像 size/CRC32/
SHA-256、传输块大小/块数、before/after 的 build/slot/status/result、阶段、PASS/FAIL、
OTA 日志 SHA-256、源码版本（`source_revision`/`source_tree`/`source_dirty`），以及
`receipt_sha256`。`receipt_sha256` 是对除自身以外全部字段的
规范化 JSON 摘要，便于事后校验；它不是私钥签名，真正的发布签名仍由发布流程完成。
点击「补发凭证」可在 BOOT/COMMIT 后按当前状态再签一份（阶段为 `APPLIED`）。

## 文件

| 文件 | 作用 |
|---|---|
| `trigger_debug_demo.py` / `__main__.py` | 入口 |
| `app.py` | Tk 页面、OTA 流程、TCP 页 |
| `catalog.py` | node-sequence 功能目录与命令/参数编码 |
| `device.py` | 串口/VISA/模拟器、Dispatcher、TCP 服务 |
| `ota.py` | 镜像哈希、SCPI arbitrary block、凭证签发 |
| `ota_workflow.py` | 常规 OTA 的 WorkflowSpec：snapshot→begin→stream→end→wait→receipt |
| `calibration_page.py` | 单口校准引导：汇总页 + P0–T3 动作页壳 |
| `calibration_action.py` | P0–T3 独立动作页与参数表（`ACTION_SPECS`） |
| `calibration_workflow.py` | 单口 GuideWorkflow：precheck(OBS key6)→CONFig→…→report |
| `obs_read.py` | OBS key 读取（`ControlPlane.read`）与 `ControlPlaneTransport` 包装 |
| `ui/workflow.py` | WorkflowSpec/StepSpec/WorkflowRunner：持久化 cursor/step_data、resume/cancel |
| `ui/settings.py` | schema 白名单、原子写、损坏 `.bad` 备份 |
| `ui/errors.py` | ErrorBoundary + Tk 回调异常限流 |
| `ui/widgets.py` | ErrorBanner/LogView |
| `ui/theme.py` | ThemeTokens + DPI scaling |
| `ui/state.py` | `ConsoleUiVector` 四 slice + `ConsoleStore` |
| `ui/scheduler.py` | Tk/Fake SchedulerPort |
| `ui/page.py` | Page/PageSpec/NavigationDecision/LegacyPageAdapter |
| `ui/router.py` | Notebook 路由、懒加载、导航守卫 |
| `ui/pump.py` | 有界 UiPump + PumpSink |
| `ui/dialogs.py` | DialogPort/TkDialogService |

## 工作流约束

按 CONSOLE 主域约束，长期流程（OTA、T2、TDMA 启环、DPLL/VDC、校准训练）必须以
`WorkflowSpec` 形式交付：持久化 step/cursor、可恢复/取消、每步证据、版本匹配
fail-closed；GUI 只是视图，不得持有唯一执行状态。

当前 OTA 仍是 `OtaPage` 内的 legacy 线程实现，迁移见 `CONSOLE-020`；T2/TDMA/DPLL/校准
尚未 workflow 化。详细约束见
[CONSOLE_ARCHITECTURE.md](../../../../../docs/console/CONSOLE_ARCHITECTURE.md) §7。

## Tk 架构接入（strangler）

`app.py` 已接入 `ui/` 基础件：Store 四 slice、PageRouter + LegacyPageAdapter、TkScheduler、
DialogPort、UiPump/PumpSink；现有 tab 先通过 legacy adapter 注册，ACK/OTA/TCP 的事件
走同一条主线程泵。页面内部仍是 legacy 代码（直接 Tk 变量、OTA ad-hoc 线程），迁移顺序见
[CONSOLE_TK_ARCHITECTURE.md](../../../../../docs/console/CONSOLE_TK_ARCHITECTURE.md) §15。

## 测试

```powershell
python -m pytest tests/python/test_trigger_debug_demo.py -q -p no:cacheprovider
```

覆盖：目录 ID/word 数、f32 与 plan 编码、命令渲染、响应字段、模拟四板 ACK、
Dispatcher 同步请求、TCP 往返、GUI/TCP 共用一个 transport、Tk 界面冒烟、
OBS 逐槽行解析（key3/4/5/6/8）、模拟器 OBS 应答、开关应用层写废弃。

## 已知限制

- 本工具尚未在台架实测（台架占用）；模拟器与 host 测试通过，真机 ACK/时序待台架空闲后验证。
- 常规 OTA 的 `BEGIN/DATA/END/BOOT/COMMIT` 路径未在台架实测；TDMA 流式 OTA 仍由固件开发中，
  本页只读状态，不执行跨板烧录。
- 自动凭证是自查摘要（`receipt_sha256` + `.sha256` 旁路），不是私钥签名；发布级签名仍走发布流程。
- 当前固件 `TRIGGER_MODE` 的 REFMEM 域配置 handler 仍是 fail-closed（IF-013 未接完），真机上该命令会
  得到 NACK；这是当前固件行为，不是 Demo 的编码问题。
- 应用层写只保留没有 REFMEM 类型的命令；开关的 `CONFigure:SWITch#` 固件已废弃，
  Demo 只提供统一控制面入口。
- OBS key 是窄 schema（见 `config/refmem/read_catalog.yaml`）：`GUIDE:PLAN?`/`BOOT:PLAN?`
  的全字段、`READ:*` 富查询仍走本地 SCPI，界面标注“诊断例外”，不得用于跨板判据。
- 模拟器按 REFMEM-READ-01 行格式应答 OBS；真机 OBS 的逐槽/stale 证据由 REFMEM 域
  `out/refmem_read/` 的 HIL 报告提供。
- `TRIGGER_SEQUENCE_PLAN` 的 count 上限为 11，plan ID 最多 16 个 ASCII 字符。
- TCP 为本地明文 `127.0.0.1`，不做鉴权；不要绑定到非回环地址。
