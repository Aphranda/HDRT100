# RP1200波导天线测试系统分布式触发方案SCPI指令表

Status: Active
Domain: SCPI
Canonical: `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md`
Related: `docs/reports/scpi/RP1200波导天线测试系统分布式触发方案SCPI指令表.html`, `docs/interface/SCPI_COMMANDS.md`, `docs/archive/TASK_PROGRESS.md`
Last updated: 2026-08-11

版本：0.11
日期：2026-08-13
产品：DTC100 Distributed Trigger
接口：USBTMC / USB488
文档状态：Markdown 源文档，HTML 已同步到 0.11，后续 PDF 由本文同步导出

## 1. 文档约定

本文用于上位机前期开发、联调和后续产品化协议冻结。后续维护顺序固定为：

```text
先更新 Markdown -> 评审确认 -> 同步更新 HTML -> 按需导出 PDF
```

本文以 `docs/legacy/rp1200/RP1200波导天线测试系统分布式触发方案SCPI指令表最初版.html` 作为格式和初始定义基线；后续增强只在该基线之上扩展，不破坏初版已经明确的业务语义。

### 1.1 指令命名

| 域 | 用途 | 示例 |
|---|---|---|
| `*` | IEEE 488.2 基础指令 | `*IDN?`, `*RST`, `*CLS` |
| `SYSTem` | 系统、版本、错误、日志、维护和门禁 | `SYSTem:ERRor?`, `SYSTem:LOG:PAGE?` |
| `CONFigure` | 业务配置，不直接表达运行动作 | `CONFigure:TRIGger`, `CONFigure:SEQuence` |
| `READ` | 业务读取和报告数据读取 | `READ:TRIGger:STATe?`, `READ:SYNC:HEALth?` |
| `TRIGger` | 触发模式、启动、停止、继续和运行控制 | `TRIGger:MODE`, `TRIGger:STARt` |
| `CALibration` | 校准保存、激活、回滚和快速测 delay | `CALibration:STARt`, `CALibration:ACTivate` |
| `SYNC` | 同步配置包、检查、启动、重锁和质量管理 | `SYNC:CHECk`, `SYNC:RELock` |

文档显示完整长指令，并按 SCPI 风格标出前四字符大写，例如 `CONFigure`、`PARameter`、`STATe`、`ACTivate`。固件可以支持标准短写，但对外文档以完整命令为准。

### 1.2 通信与响应

| 项目 | 规则 |
|---|---|
| 物理接口 | 成品固定为 USBTMC / USB488 |
| 开发维护接口 | CDC 仅用于 validation 固件，不作为成品业务接口 |
| 命令结束符 | `LF` 或 `CRLF` |
| 文本响应 | 逗号分隔字段，以换行结束 |
| 二进制数据 | IEEE 488.2 definite length block |
| 写命令响应 | `OK` 或 `1` 表示 accepted / complete；复杂动作必须有对应 `READ` 闭环 |
| 错误队列 | `SYSTem:ERRor?` 读取 SCPI 错误 |
| 故障锁存 | `SYSTem:FAULT:*` 管理硬件/运行故障 |

最初版文档中运行控制写命令多使用 `OK`；当前固件和后续产品化文档可以统一为 `1`，但上位机前期开发应兼容 `OK` / `1` 两种 accepted 响应。任何 accepted 响应都不等价于跨节点动作完成。

### 1.3 接口边界硬约束

本指令表是 DTC100 对外通讯接口，不是硬件直控接口。上位机通过 SCPI 发送配置、动作意图和查询请求；
固件 SCPI 层只负责解析、权限/状态/资源门禁，并通过 owner API 写入 command/config slot、投递 owner event 或读取快照。
SCPI callback 不直接操作 GPIO、PIO、DMA、ADC、UART、RS485、BiSS、SD、flash 或现场 IO。

内部执行由各域 owner 和子功能状态机自发完成：

```text
SCPI command
  -> SCPI/A3 gateway accepted
  -> command/config slot or owner event
  -> domain owner state machine
  -> hardware backend or core1 realtime payload
  -> reflected memory status/result/evidence slot
  -> READ:*? / SYSTem:*? query
```

反射内存保存多节点共同事实、配置快照、命令意图、ACK/NACK、状态摘要和质量证据。
它不承载精确触发边沿，也不传 OTA payload、日志全文、波形或 SD 文件内容。
SCPI 只允许写 command/config slot；state、summary、ACK/NACK、result、health 和 evidence slot 必须由对应 owner 写入。
写命令响应 `OK` 或 `1` 只表示接口层 accepted；动作完成、拒绝原因、质量和证据必须通过
`READ:*?`、`SYSTem:*?`、ACK/NACK 或日志/snapshot 闭环确认。

### 1.4 上位机能力分层

| 上位机 | RUN 前 | RUN 中 | RUN 后 |
|---|---|---|---|
| 测试上位机 / `TEST` 权限 | 配置转台连续触发、DTC 角度域、触发参数和 active sequence；完成门禁检查后 START | 只从网分取数据，不控制分布式触发系统，不作为实时闭环参与者；可保留安全停止和状态摘要读取 | 读取 `SYSTem:RUN:*`、故障和错误摘要，生成测试报告 |
| 调试上位机 | 完整配置 DTC、转台、网分、CAL/SYNC、资源门禁和诊断策略 | 面向当前系统任意状态的验证和控制；按权限配置决定查询、控制、排队或拒绝，支持转台控制、联动校准、同步重锁、HOLDOVER/STOP/FAULT 处理和 trace 采集 | 导出日志、trace、snapshot、RUN summary、CAL/SYNC 质量和分布式向量表证据 |

调试上位机本身是一套完整工具，按四级权限裁剪功能：`TEST`、`SERVICE`、`DEBUG`、`FACTORY`。
最低权限 `TEST` 就是测试上位机，覆盖现场测试完整业务闭环，但不开放维护、校准写入、
同步调参、存储写、权限修改和任意状态强控；`SERVICE` 面向现场维护；
`DEBUG` 高于 `SERVICE`，面向当前系统任意状态验证和控制；`FACTORY` 为工厂/工程最高权限。
权限为单调继承：高级权限拥有所有低级权限功能，再追加本级能力。
调试上位机仍属于 core0 控制面：
命令必须经 SCPI/A3 gateway、AO 事件、权限配置、资源仲裁、SystemModeTable、状态策略表和分布式 ACK
闭环进入系统。业务逻辑、转台联动、校准和同步调试不得直接调用 core1 实时入口，不得直接改写
`TriggerVector`/PIO owner，不得修改已装载的本地预约边沿；core1 只消费已验证的小载荷和实时命令。
因此“业务逻辑不影响实时核心”的产品含义是：调试端可以覆盖全部控制入口，但具体账号、角色或调试场景
只拿到对应权限；任意系统状态下的控制都必须由权限表和状态策略表共同给出
`ALLOW/QUERY_ONLY/SAFE_BOUNDARY/QUEUE/DENY` 决策。不能立即安全执行的控制必须被拒绝、排队到安全边界
或转入 HOLDOVER/FAULT。

### 1.5 设备信息

| 项目 | 默认值 | 说明 |
|---|---|---|
| 制造商 | `GTS` | `*IDN?` 字段 1 |
| 型号 | `DTC100` | `*IDN?` 字段 2 |
| 序列号 | 芯片唯一 ID | `*IDN?` 字段 3，用于区分 A0/A1/A2/A3 节点 |
| 固件版本 | `0.1.0` | `*IDN?` 字段 4，正式版本以构建系统输出为准 |
| 外部入口 | `A3` | 上位机只接入 A3，A0/A1/A2 不直接暴露外部控制接口 |

## 2. 通用与系统指令

### 2.1 IEEE 488.2 指令

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `*IDN?` |  | `GTS,DTC100,<serial>,<fw>` | 查询设备识别码 |
| `*RST` |  | `1` | 停止触发和同步，恢复运行态配置到安全默认值；不删除已保存配置包 |
| `*CLS` |  | `1` | 清除 SCPI 状态寄存器和错误队列；不清除锁存硬件故障 |
| `*TST?` |  | `0` 或错误码 | 执行基础自检并返回结果 |
| `*OPC?` |  | `1` | 查询前序命令是否完成 |
| `*OPC` |  | `1` | 设置操作完成标志 |
| `*WAI` |  | 无 | 等待前序命令完成 |
| `*STB?` |  | 状态字节 | 查询状态字节 |
| `*ESR?` |  | 事件字节 | 查询并清除事件状态寄存器 |
| `*ESE` / `*ESE?` | `<n>` | `1` / `n` | 设置或查询事件状态使能 |
| `*SRE` / `*SRE?` | `<n>` | `1` / `n` | 设置或查询服务请求使能 |

### 2.2 系统状态与日志

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `SYSTem:VERSion?` |  | `version block` | 读取 SCPI、固件、硬件 profile、构建时间和协议版本 |
| `SYSTem:FW:VERSion?` |  | `fw version` | 查询固件语义版本 |
| `SYSTem:FW:BUILD?` |  | `build id` | 查询当前烧录固件构建标识 |
| `SYSTem:BOOT:VERSion?` |  | `boot version` | 查询当前 App 声明的 Bootloader 兼容版本 |
| `SYSTem:BOOT:CAPability?` |  | `capability mask` | 查询 Bootloader/OTA 能力位 |
| `SYSTem:ERRor?` |  | `<code>,<text>` | 读取并弹出一条 SCPI 错误 |
| `SYSTem:ERRor:COUNt?` |  | `<count>` | 查询错误队列数量 |
| `SYSTem:RTOS:STATus?` |  | `task table` | 读取 heap、任务栈水位、任务状态和核心心跳 |
| `SYSTem:CORE?` |  | `core block` | 读取 core0/core1 心跳、循环计数和状态机摘要 |
| `SYSTem:LOG:LEVel` | `0..3` | `1` | 设置日志等级：0=DEBUG，1=INFO，2=WARN，3=ERROR |
| `SYSTem:LOG:LEVel?` |  | 等级值 | 查询日志等级 |
| `SYSTem:LOG:STATus?` |  | 日志统计 | 查询日志计数、队列、水位、丢弃和输出失败统计 |
| `SYSTem:RUN:LAST?` |  | `run id` | 查询最近一次 RUN 的 run_id、完成态和摘要是否可读 |
| `SYSTem:RUN:SUMMary?` | `[run_id]` | `run summary block` | 读取 RUN 后摘要：角度命中、序列执行、late、故障、CRC 和完成状态 |
| `SYSTem:RUN:LOG?` | `[run_id,page]` | `run log page block` | 按页读取指定 RUN 的短日志摘要，用于测试报告和异常追溯 |
| `SYSTem:TRACe:LAST?` |  | trace 摘要 | 查询最近 trace 摘要 |
| `SYSTem:SNAPshot:LAST?` |  | snapshot 摘要 | 查询最近一次配置、运行或故障快照 |
| `SYSTem:LOG:PAGE?` | `[page_id]` | `log page block` | 按页读取日志，用于上位机报告和故障追溯 |
| `SYSTem:TRACe:DATA?` | `[kind,page]` | `trace block` | 读取运行 trace、同步 trace 或故障 trace |
| `SYSTem:SNAPshot:DATA?` | `[kind]` | `snapshot block` | 读取配置、运行或故障快照 |
| `SYSTem:T2:COUNt?` |  | `<count>` | 读取 T2 FIFO 当前数据量 |
| `SYSTem:T2:DATA?` | `[count]` | `T2 block` | 读取 T2、e_act、late、CRC、seq 分布数据 |

`SYSTem:RUN:SUMMary?` 是 RUN 后复盘接口，不作为测试上位机 RUN 中实时控制依据。测试上位机
在 `TRIGger:STARt` 后只从网分取数据；分布式触发系统内部完成角度脉冲接收、序列执行、
预约分发、T2/READY 捕获和故障锁存。

### 2.3 故障闭环

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `SYSTem:FAULT:LAST?` |  | `fault block` | 查询最近一次锁存故障证据 |
| `SYSTem:FAULT:CLEAr` |  | `1` | 清除锁存故障；执行后必须重新做配置门禁、同步检查和触发启动 |

固定恢复流程：

```text
停止运行
-> 读取 SYSTem:FAULT:LAST?
-> 保存 trace / snapshot / log
-> SYSTem:FAULT:CLEAr
-> 重新 SYNC:CHECk
-> 重新 TRIGger:STARt
```

### 2.4 系统门禁与分布式表

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `SYSTem:MODE:TABle?` | `[mode_id]` | `mode table row` | 查询 SystemModeTable：当前模式、允许能力和表 CRC |
| `SYSTem:RESource?` |  | `resource block` | 读取资源仲裁摘要：active、conflict、request owner 和 holder owner |
| `SYSTem:RESource:TABle?` | `[resource_id]` | `resource table row` | 查询 ResourceArbiterTable：资源 owner、活跃位和冲突位 |
| `SYSTem:FAULT:TABle?` | `[fault_id]` | `fault table row` | 查询 FaultCodeTable：故障域、等级、sticky 和 recoverable |
| `SYSTem:REFMEM:STATus?` |  | `refmem block` | 查询本地 DistributedVectorTable 表头、layout version、table_seq、节点数和本节点 heartbeat |
| `SYSTem:REFMEM:NODE?` | `[node_id]` | `node block` | 查询 NodeSlot 快照；省略时读取本节点 |
| `SYSTem:CORE:VECTOR?` |  | `core vector block` | 读取 core0/core1 VTOR owner、IRQ owner、entry table owner 和 guard 状态 |
| `SYSTem:PROTection:STATus?` |  | `runtime protection block` | 读取 RAM-resident、flash lockout/park、entry owner 和保护状态 |
| `SYSTem:CONFigure:STAT?` |  | `gate block` | 读取配置门禁、ACK/NACK、busy、timeout 和 CRC 快照 |
| `SYSTem:CONFigure:ROLE?` | `[node_id]` | `role block` | 查询 NodeRoleMap 条目 |
| `SYSTem:CONFigure:LOOP?` | `[layer_id]` | `loop block` | 查询 LoopPlan 层级和数组循环编排 |
| `SYSTem:CONFigure:ACT?` | `[action_id]` | `action block` | 查询 SP8T、SP2T、READY 等动作映射 |
| `SYSTem:CONFigure:CAL?` | `[node_id]` | `cal block` | 查询链路、端口和设备动作补偿摘要 |
| `SYSTem:CONFigure:ACK?` |  | `ack block` | 查询分布式命令 ACK/NACK/busy/timeout 快照 |
| `SYSTem:CONFigure:NACK?` | `[reason_id]` | `reason block` | 查询 NACK reason 表，用于 UI 参数校验和故障提示 |
| `SYSTem:SCPI:RUN:ALLOW?` | `[index]` | `policy block` | 查询运行态 SCPI 策略表；命名保留 ALLOW，但语义为权限 profile 在 RUN 状态下的执行结果 |
| `SYSTem:LOOP:STATus?` |  | `loop block` | 系统维护查询：loop_engine ready、service_count 和 service 时间 |
| `SYSTem:SYNC:VDC:STATus?` |  | `vdc block` | 同步域维护查询：虚拟 DC 服务 ready、lock_state、service_count 和 sync_seq |
| `SYSTem:SYNC:VDC:DPLL:STATus?` |  | `dpll block` | 同步域维护查询：VDC DPLL ready、state、service_count 和 update_seq |
| `SYSTem:SYNC:VDC:LOCK:READiness?` |  | `vdc lock readiness block` | 同步域维护查询：读取 VDC/DPLL 最小实例锁定输入条件、阻塞原因、timestamp eligibility、observer 计数和 dictionary/profile CRC；只读，不写 lock/offset/rate |
| `SYSTem:SYNC:VDC:OBServer:TDMA:SELFtest` | `[role],[output_index],[observed_mask],[initial_sample_mask],[sample_period_ns],[pulse_period_ns],[pulse_high_ns],[pulse_count],[frame_crc32],[start_delay_ns]` | `1` | 同步域维护动作：TX 提交公共 TDMA `VDC_SYNC_SAMPLE` 诊断 frame，RX 启动 observation/capture；当前 evidence 为 `SOFTWARE_US/1000ns/DIAGNOSTIC_ONLY`，不写 DPLL |
| `SYSTem:SYNC:VDC:OBServer:TDMA:SELFtest?` |  | `vdc selftest block` | 同步域维护查询：读取 self-test 配置、CRC、错误和 first window start |
| `SYSTem:SYNC:VDC:OBServer:TDMA` | `[enabled],[initial_sample_mask],[sample_period_ns],[frame_crc32]` | `1` | 同步域维护配置：按 active `VDC_OBSERVATION_WINDOW` 配置 observer expected/base 时间；不启动 capture，不提升 timestamp flags |
| `SYSTem:SYNC:VDC:OBServer` | `[enabled],...` | `1` | 同步域维护配置：无参数或 `0` 关闭 raw capture observer；`1` 时显式配置 batch、event、mask、tick、window 和 frame CRC，不启动 capture |
| `SYSTem:SYNC:VDC:OBServer?` |  | `vdc observer block` | 同步域维护查询：读取 VDC manager 消费 SYNC_IO raw capture word 的 observer 计数和最近 gate 证据；只读，不启动 capture |

HTML 分页将系统域拆成两页：系统状态与日志、系统门禁与分布式表。

## 3. 业务配置指令

权限归属：P5-P7 为现场测试业务域，默认归 `TEST+`。现场测试程序只拿 `TEST` 权限时，
仍应能完成业务配置、序列准备、运行门禁、启动/停止、暂停/继续、断点续测、状态读取和结束复盘。
`DEBUG+` 增加的是任意状态强控、外设联动调试、异常注入和越过常规现场流程的验证能力。

### 3.1 触发参数与扫描角度

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CONFigure:TRIGger` | `<chan_cnt>,<pol>,<freq_cnt>,<wave_cnt>` | `1` | 配置角度点内序列展开参数；展开顺序为通道 -> 极化 -> 频点 -> 波位 |
| `READ:TRIGger:PARameter?` |  | `param block` | 读取通道数、极化、频点数、波位数、展开状态数和 CRC |
| `CONFigure:ANGLe:SWEEP` | `<start_deg>,<stop_deg>,<step_deg>` | `1` | 配置扫描角度范围和步长，用于输出脉冲与转台位置对应 |
| `READ:ANGLe:SWEEP?` |  | `angle block` | 读取扫描起始角、终止角、步长、角度点数和当前角度索引 |
| `CONFigure:ANGLe:PULSe` | `<RISING|FALLING>,<pulse_width_us>,<timeout_ms>` | `1` | 配置 A0 接收转台角度触发脉冲的边沿、脉宽和超时；归属角度域，不做编码器 PCNT 换算 |
| `READ:ANGLe:PULSe?` |  | `angle pulse block` | 读取角度脉冲输入配置、有效计数、漏脉冲计数、超时状态和最近边沿时间 |
| `READ:ANGLe:POSition?` |  | `position block` | 读取 DTC 侧由扫描配置和角度脉冲推导出的角度状态；不代表运动控制器反馈的真实转台位置 |
| `CONFigure:ANGLe:BREAkpoint` | `[angle_deg]` | `1` | 配置角度断点；给出参数时绑定指定扫描角度，运行中或暂停中省略参数时使用当前待输出角度游标 |
| `CONFigure:ANGLe:BREAkpoint:CLEAr` |  | `1` | 清除角度断点和命中标志；若当前已因断点暂停，仅清除断点标志，不自动继续运行 |
| `READ:ANGLe:BREAkpoint?` |  | `breakpoint block` | 读取断点角度、命中状态、暂停状态和断点游标 |

极化编码：

| 值 | 含义 |
|---:|---|
| `0` | H |
| `1` | V |
| `2` | BOTH，展开为 H/V 两态 |

例如：5 个频点、8 个通道、1 个波位、双极化：

```text
5 x 8 x 1 x 2 = 80 states
```

角度域分两层：`CONFigure:ANGLe:SWEEP` 定义本次测试的角度点集合；`CONFigure:ANGLe:PULSe`
定义 A0 如何接收转台在目标角度点输出的触发脉冲。转台自身的
`ConfigureTrigger(dimension,start,stop,step,pulseWidth,isRaisingEdge,timeout)` 由测试上位机
调用，DTC 不再把连续编码器脉冲换算为角度。
`READ:ANGLe:POSition?` 返回的是 A0 当前扫描游标和最近角度脉冲状态，例如
`angle_index/current_angle_by_sweep/last_pulse_time`；真实转台位置、速度和伺服状态由调试上位机
通过运动控制器 API 获取。

### 3.2 序列配置

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CONFigure:SEQuence` | `<plan_id>,<state_id_1>,<state_id_2>,...` | `1` | 一次性写入完整状态序列；`state_id` 必须引用当前 `CONFigure:TRIGger` 自动展开生成的状态表 |
| `READ:SEQuence?` | `[plan_id[,index]]` | `sequence block` | 查询配置的序列库；省略参数读取当前配置序列摘要，给出 `plan_id` 读取指定序列，给出 `index` 读取该序列任意一条状态引用 |
| `READ:SEQuence:MAP?` | `[plan_id]` | `map block` | 读取状态映射表；返回 `state_id` 与 `switch1_ch`、`switch2_sel`、`pol`、`freq_idx`、`wave_idx` 的对应关系 |
| `READ:SEQuence:CHECK?` | `[plan_id]` | `sequence check block` | `TEST+` 可选详细预检查询；不改变 active sequence，返回 state_id 范围、映射、开关、极化、频点、波位和 CRC 的逐项校验结果 |
| `CONFigure:SEQuence:ACTive` | `<plan_id>` | `1` | accepted：将已配置且校验通过的序列设为活动序列；RUN 中禁止切换，完成态通过 ACK/状态查询确认 |
| `READ:SEQuence:ACTive?` |  | `sequence active block` | 查询当前活动序列；测试上位机 START 前确认 active plan_id、CRC、状态数、valid、最近检查状态和拒绝原因 |

序列采用“自动展开 + 顺序引用”模式。`CONFigure:TRIGger` 根据通道、极化、频点和波位自动生成状态表，展开顺序固定为：

```text
switch1_ch -> pol -> freq_idx -> wave_idx
```

`CONFigure:SEQuence` 只上传引用顺序，上位机无需逐字段描述每个状态。`state_id` 不是黑盒编号，必须能通过 `READ:SEQuence:MAP?` 回读到 SWITCH1/SWITCH2/pol/freq/wave 映射。

RUN 态主循环是“角度脉冲外层 + 点内序列内层”：A0 每收到一个有效角度脉冲，推进一个
`angle_index`，并完整执行一次 active sequence。序列执行完成后等待下一角度脉冲；不存在由
测试上位机发命令推进点内序列，也不引入多轮 repeat 概念。

状态映射字段：

```text
state_id,
switch1_ch,
switch2_sel,
pol,
freq_idx,
wave_idx
```

### 3.3 运行门禁

| 场景 | 门禁 | 拒绝处理 |
|---|---|---|
| `TRIGger:STARt` | 配置 CRC、active 校准表、active 同步配置、active 序列 CRC、序列校验和运行态策略必须通过 | 读取 `READ:SEQuence:ACTive?`、`SYSTem:CONFigure:STAT?` 和 `SYSTem:CONFigure:NACK?` |
| 同步要求 | `required_lock=1` 时 SYNC 必须为 `LOCKED`，且 e_vdc、age、CRC 低于门限 | 读取 `READ:SYNC:STATe?`、`READ:SYNC:HEALth?` |
| 角度脉冲 | 角度脉冲边沿、脉宽、超时和 `angle_count` 必须与转台配置和扫描角度一致 | 读取 `READ:ANGLe:PULSe?`、`READ:ANGLe:SWEEP?` |
| 活动序列切换 | 只允许在 IDLE/CONFIG/调试安全边界切换 active sequence；RUN 中不能替换本轮已冻结的 active 序列 | 读取 `READ:SEQuence:ACTive?` 和 `SYSTem:CONFigure:ACK?` |
| `TRIG RUN` | 只允许状态、日志、T2、fault 和 STOP 查询/控制；禁止配置、校准、同步和 DPLL 调试写入 | 返回运行态拒绝或 resource busy |

### 3.4 开关独立控制

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CONFigure:SWITch#` | `<value>` | `1` | 独立切换开关；`#=1` 为 SWITCH1/SP8T，取 1..8；`#=2` 为 SWITCH2/SP2T，取 0/1 |
| `READ:SWITch#?` |  | `switch block` | 读取指定开关的目标值、实际值、busy、错误码和最近切换时间 |

运行中若开关已被序列引擎占用，独立切换命令返回 busy。

## 4. 运行控制指令

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `TRIGger:MODE` | `0|1|2|3|4` | `1` | 设置模式：0=IDLE，1=TRIG，2=CAL，3=SYNC，4=SIM；按参数值授权，`TEST` 只允许 `0=IDLE` 和 `1=TRIG` |
| `TRIGger:MODE?` |  | `mode block` | 读取当前模式、运行态、允许动作和拒绝原因 |
| `TRIGger:STARt` | `[plan_id]` | `1` | accepted：无参数时启动当前 active sequence；带 `plan_id` 时作为受限便捷事务，请求激活该序列并启动一次扫描任务 |
| `TRIGger:STOP` |  | `1` | accepted：中止当前扫描任务，清空未执行预约队列，保留配置、断点和日志；正常流程由扫描完成进入 COMPLETE |
| `TRIGger:PAUSe` |  | `1` | 在下一个安全边界暂停；不截断正在执行的角度点内部动作 |
| `TRIGger:CONTinue` |  | `1` | 从暂停角度继续；若因角度断点暂停，对当前断点做一次性越过 |
| `READ:TRIGger:STATe?` |  | `state block` | 读取 `mode,run_state,run_id,plan_id,current_angle,angle_index,angle_count,seq_index,seq_count,angle_pulse_count,missed_angle_pulse_count,late_count,error_code,reject_reason` |
| `READ:TRIGger:PARameter?` |  | `param block` | 读取触发参数、序列 CRC、角度配置 CRC、同步门禁和校准 CRC |

运行控制同属现场测试业务域，默认 `TEST+` 可用。写入类动作必须经过状态策略表：
`TRIGger:MODE` 对 `TEST` 只开放 `0=IDLE` 和 `1=TRIG`，`2=CAL`、`3=SYNC` 需要 `SERVICE+`，
`4=SIM` 需要 `DEBUG+`；`START` 只允许在 IDLE/CONFIG/ARM 前安全边界进入运行，
`STOP` 可在任意状态安全执行，`PAUSe/CONTinue` 和断点续测由现场测试 profile 决定是否显示，
但不需要 `DEBUG` 权限。

典型流程：

```scpi
CONFigure:ANGLe:SWEEP -10,370,1
CONFigure:ANGLe:PULSe RISING,10,30000
CONFigure:ANGLe:BREAkpoint 0
CONFigure:TRIGger 8,2,5,1
CONFigure:SEQuence PLAN_A,0,1,2,3,4,5
CONFigure:SEQuence:ACTive PLAN_A
READ:SEQuence?
READ:SEQuence:ACTive?
READ:ANGLe:PULSe?
SYNC:CHECk
TRIGger:MODE 1
TRIGger:STARt PLAN_A
```

`TRIGger:STARt [plan_id]` 保留输入 ID 的便捷能力，但不能绕过 `CONFigure:SEQuence:ACTive` 的
语义和门禁。无参数 `START` 只启动当前 active sequence；带 `plan_id` 的 `START` 等价于：

```text
检查 plan_id 存在
-> 检查该序列已通过 CHECK
-> 请求切换 active sequence
-> 确认 active CRC
-> 通过配置/同步/角度脉冲门禁
-> 启动扫描任务
```

该便捷事务只允许在 `IDLE/CONFIG/ARM` 前使用，RUN 中禁止用 `plan_id` 替换本轮已冻结序列。
如果 active 切换或任一门禁未完成，`START` 应返回 busy/NACK 或保持 accepted 后由
`SYSTem:CONFigure:ACK?`、`READ:SEQuence:ACTive?` 和 `READ:TRIGger:STATe?` 闭环确认失败原因。

上位机不逐点驱动 RUN 态。启动后由 A0 接收转台角度触发脉冲，按扫描角度推进
`angle_index`，每个角度点完整执行一次 active sequence，并由分布式系统内部完成预约分发
和 T2/READY 闭环。测试上位机 RUN 中只从网分取数据，不再控制分布式触发系统；RUN 后通过
`SYSTem:RUN:SUMMary?`、`SYSTem:FAULT:LAST?` 和 trace/snapshot 复盘。

运行状态建议：

| 状态 | 含义 |
|---|---|
| `IDLE` | 未运行或已安全停止 |
| `ARMED` | 资源、同步、角度脉冲和序列门禁已通过 |
| `WAIT_ANGLE_PULSE` | 等待转台输出下一个目标角度脉冲 |
| `ANGLE_PULSE_HIT` | 已收到有效角度脉冲，准备执行该角度点序列 |
| `SEQ_RUNNING` | 正在执行当前角度点内完整序列 |
| `WAIT_NEXT_ANGLE` | 当前角度点序列完成，等待下一角度点 |
| `COMPLETE` | 扫描角度范围完成，RUN 摘要已锁存或可读取 |
| `HOLDOVER` | 同步或节点新鲜度短时异常，停止新增预约 |
| `FAULT` | 故障锁存，要求读取证据并清除 |

## 5. 状态与数据读取

### 5.1 业务读取

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `READ:RUN:SUMMary?` |  | `summary block` | 兼容读取最近 RUN 摘要；产品上位机优先使用 `SYSTem:RUN:SUMMary?` |
| `READ:STATistics?` | `[kind]` | `statistics block` | 读取 `e_vdc/e_act/e_pll/late/crc/seq` 统计摘要 |

`SYSTem:T2:DATA?` 字段：

```text
seq,node,channel,t2_tick,status,error_code,temperature
```

### 5.2 配置门禁与反射内存

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `SYSTem:CONFigure:STAT?` |  | `gate block` | 读取配置门禁、ACK/NACK、busy、timeout 和 CRC 快照 |
| `SYSTem:CONFigure:ACK?` |  | `ack block` | 读取分布式配置 ACK 位图、NACK 位图和最近拒绝原因 |
| `SYSTem:CONFigure:NACK?` | `[reason_id]` | `reason block` | 读取 NACK reason 表，用于 UI 参数校验和故障提示 |
| `SYSTem:REFMEM:STATus?` |  | `refmem block` | 读取 64KB 分布式向量表表头、版本、table_seq、节点心跳和 stale 状态 |
| `SYSTem:REFMEM:NODE?` | `[node_id]` | `node block` | 读取指定节点镜像；省略时读取本节点 |
| `SYSTem:CORE:VECTOR?` |  | `core vector block` | 读取 core0/core1 VTOR owner、IRQ owner、entry table owner 和 guard 状态 |
| `SYSTem:PROTection:STATus?` |  | `runtime protection block` | 读取 RAM-resident、flash lockout/park、entry owner 和保护状态 |
| `SYSTem:SYNC:VDC:STATus?` |  | `vdc block` | 同步域维护查询：读取虚拟 DC 服务 ready、lock_state、service_count 和 sync_seq |
| `SYSTem:SYNC:VDC:DPLL:STATus?` |  | `dpll block` | 同步域维护查询：读取 VDC DPLL ready、state、service_count 和 update_seq |
| `SYSTem:SYNC:VDC:LOCK:READiness?` |  | `vdc lock readiness block` | 同步域维护查询：读取锁定输入条件和 DPLL 锁定状态；用于区分 observer/dictionary/timestamp/gate/servo 阻塞点 |
| `SYSTem:SYNC:VDC:OBServer:TDMA:SELFtest` | `[role],[output_index],[observed_mask],[initial_sample_mask],[sample_period_ns],[pulse_period_ns],[pulse_high_ns],[pulse_count],[frame_crc32],[start_delay_ns]` | `1` | 同步域维护动作：TX 提交公共 TDMA `VDC_SYNC_SAMPLE` 诊断 frame，RX 启动 observation/capture；当前 evidence 为 `SOFTWARE_US/1000ns/DIAGNOSTIC_ONLY`，不写 DPLL |
| `SYSTem:SYNC:VDC:OBServer:TDMA:SELFtest?` |  | `vdc selftest block` | 同步域维护查询：读取 self-test 配置、CRC、错误和 first window start |
| `SYSTem:SYNC:VDC:OBServer:TDMA` | `[enabled],[initial_sample_mask],[sample_period_ns],[frame_crc32]` | `1` | 同步域维护配置：按 active TDMA observation window 建立 observer 最小实例，不写 DPLL |
| `SYSTem:SYNC:VDC:OBServer` | `[enabled],...` | `1` | 同步域维护配置：启停 VDC manager raw capture observer；不启动 SYNC_IO capture，不改变 DPLL lock |
| `SYSTem:SYNC:VDC:OBServer?` |  | `vdc observer block` | 同步域维护查询：读取 raw capture observer 状态、计数和最近样本证据 |
| `SYSTem:CONFigure:ROLE?` | `[node_id]` | `role block` | 查询 NodeRoleMap |
| `SYSTem:CONFigure:LOOP?` | `[layer_id]` | `loop block` | 查询 LoopPlan 层级和数组循环编排 |
| `SYSTem:CONFigure:ACT?` | `[action_id]` | `action block` | 查询 SP8T、SP2T、READY 等动作映射 |
| `SYSTem:CONFigure:CAL?` | `[node_id]` | `cal block` | 查询链路、端口和设备动作补偿摘要 |
| `SYSTem:SCPI:RUN:ALLOW?` | `[index]` | `policy block` | 查询运行态 SCPI 策略表；命名保留 ALLOW，但语义为权限 profile 在 RUN 状态下的执行结果 |

反射内存用于维护多节点共同事实和摘要，不承载精确触发边沿，也不传大文件、波形、OTA payload 或 SD 内容。

## 6. 校准指令

### 6.1 链路表维护

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CONFigure:CALibration:LINK:ADD` | `<type>,<src_node>,<src_port>,<dst_node>,<dst_port>,<direction>,<enable>,<required>` | `1` | 新增可校准链路；链路 key 已存在时返回 duplicate |
| `CONFigure:CALibration:LINK:UPDate` | 同 ADD | `1` | 修改链路属性；不修改已经保存的 delay 数据 |
| `CONFigure:CALibration:LINK:CLEAr` |  | `1` | 清空 staging 链路表；active 校准表不受影响 |
| `CONFigure:CALibration:LINK:DELete` | `<type>,<src_node>,<src_port>,<dst_node>,<dst_port>[,DEL]` | `1` | 删除链路；带 `DEL` 时同时删除该链路校准数据 |
| `READ:CALibration:LINK?` | `[type,src_node,src_port,dst_node,dst_port]` | `link table` | 读取链路清单、方向、使能、必需标志和当前 delay 是否有效 |

### 6.2 校准动作

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CALibration:STARt` | `<type>,<src_node>,<src_port>,<dst_node>,<dst_port>` | `result block` | 快速测量指定输入/输出段，例如 `CALibration:STARt SMA,A0,OUT1,A1,IN1` |
| `CALibration:STOP` |  | `1` | 停止当前校准事务；若事务已完成则保持最近结果不变 |
| `CALibration:CLEAr` |  | `1` | 清除 staging 校准结果和最近一次校准事务状态，不影响 active 表 |
| `READ:CALibration:STATe?` | `[type,src_node,src_port,dst_node,dst_port]` | `state block` | 读取最近一次或指定链路校准状态；快速事务通常返回 DONE/FAIL |
| `READ:CALibration:RESult?` | `[type,src_node,src_port,dst_node,dst_port]` | `result block` | 读取最近一次校准结果、delay、jitter、样本数和失败原因 |

### 6.3 执行约束

| 约束 | 规则 | 上位机处理 |
|---|---|---|
| 系统状态轴 | 基础链路校准要求 `SystemMode=IDLE/CAL` 且 `TriggerState!=RUN` | 运行中拒绝时先 `TRIGger:STOP`，系统回到 `IDLE` 后再重新校准 |
| 链路存在 | `CALibration:STARt` 的端口对必须已在 LINK 表中登记 | 缺失时先执行 `CONFigure:CALibration:LINK:ADD` |
| 失败保护 | 失败不覆盖旧 staging delay，只记录失败原因和原始计数 | 读取 `READ:CALibration:RESult?` 后决定是否重测 |
| 同步影响 | 校准写入 staging；只有 `CALibration:ACTivate` 后才影响 SYNC | 激活后重新执行 `SYNC:CHECk` |
| 持久化 | `CALibration:SAVE` 涉及 flash/storage 时必须通过资源仲裁；core1 需已 park/lockout 或后端不触发 flash erase/program | 失败时读取 `SYSTem:PROTection:STATus?`、`SYSTem:SD:STATus?` 和 `SYSTem:CONFigure:NACK?` |

### 6.4 校准门禁

| 场景 | 允许状态 | 说明 |
|---|---|---|
| 基础链路校准 | `SystemMode=IDLE/CAL` | `CALibration:STARt` 只写 staging，失败不覆盖旧 delay |
| DEVICE/T2 测量 | `SystemMode=CAL` 且 SYNC `LOCKED` | 使用虚拟 DC 统一时间基准测动作补偿；结果仍写 staging；TRIG RUN 中禁止 |
| 校准保存 | `SystemMode=IDLE/MAINT` | 持久化前必须通过资源仲裁和 flash/storage 门禁 |
| 校准激活 | `SystemMode=IDLE` | `CALibration:ACTivate` 后清除最近一次 SYNC 检查结论，必须重新 `SYNC:CHECk` |
| 分布式确认 | command ACK 完成 | `SAVE/ACTivate/ROLLback` 写命令只表示 accepted，完成态通过 ACK/状态查询确认 |
| TRIG RUN | 禁止 | 禁止 LINK/PARameter/META/SAVE/LOAD/ACTivate/ROLLback，避免运行中改变时间基准 |

### 6.5 拒绝原因

| `reject_reason` | 含义 | 处理 |
|---|---|---|
| `RUNNING` | 系统处于 TRIG RUN 或正在输出预约边沿 | 停止触发后重试 |
| `LINK_NOT_FOUND` | 指定端口对未登记到 LINK 表 | 先新增或修正 LINK |
| `PORT_INVALID` | 节点、端口名或对象类型非法 | 按校准参数页检查端口枚举 |
| `CAL_BUSY` | 已有校准短事务正在执行 | 稍后重试 |
| `SIGNAL_TIMEOUT` | 指定输入端未捕获到回传边沿 | 检查线缆、方向和端口映射 |
| `QUALITY_FAIL` | 抖动、样本数或 delay 超出门限 | 读取结果并决定重测或手动写入 |
| `SYNC_NOT_LOCKED` | DEVICE/T2 测量时虚拟 DC 未锁定 | 先执行 `SYNC:CHECk` / `SYNC:STARt` 并等待 `LOCKED` |
| `PERSIST_DENIED` | 保存路径未通过资源仲裁、flash lockout 或 storage 门禁 | 读取保护与存储状态，进入 `IDLE/MAINT` 后重试 |
| `ACK_TIMEOUT` | 分布式保存、激活或回滚未收到目标节点确认 | 读取 `SYSTem:CONFigure:ACK?` 和 NACK 详情 |

```scpi
CONFigure:CALibration:LINK:ADD SMA,A0,OUT1,A1,IN1,BIDIR,1,1
CALibration:STARt SMA,A0,OUT1,A1,IN1
READ:CALibration:RESult? SMA,A0,OUT1,A1,IN1
```

## 7. 校准参数与流程

### 7.1 校准对象

| 对象 | 端口格式 | 说明 |
|---|---|---|
| `SMA` | `SMA,A0,OUT1,A1,IN1,delay_ns` | A0..A4 节点之间 SMA 输入/输出触发链路 delay |
| `NODE` | `NODE,A0,RJ45,A1,RJ45,delay_ns` | RJ45 触发回传链路 delay；产品同步环默认 `A0->A1->A2->A3->A0` |
| `DEVICE` | `DEVICE,A1,SP8T,READY,delay_ns` | 稳态 DC 锁定后测得的设备动作/T2 delay，例如 SP8T、SP2T、VNA TRIG/READY |

### 7.2 标准链路清单

| 类型 | 标准链路 | 说明 |
|---|---|---|
| `NODE` | `A0,RJ45 -> A1,RJ45 -> A2,RJ45 -> A3,RJ45 -> A0,RJ45` | RJ45_SYNC_RING 完整环路，内部包含 4 个 required hop |
| `SMA` | `<src_node>,OUT# -> <dst_node>,IN#` | SMA 触发链路按现场接线任意配置，不限定 A0 作为输出源 |
| `DEVICE` | `A1,SP8T,READY` / `A2,SP2T,READY` / `A3,VNA,READY` | 在虚拟 DC `LOCKED` 后标定设备 READY/T2，形成预测分发使用的动作补偿 |

A4 仅作为早期调试和 HIL 回环节点使用，可模拟转台和网分；当启用 A4 调试拓扑时，
`NODE` 环可扩展为 `A0->A1->A2->A3->A4->A0`，required hop 为 5。
产品运行拓扑仍以 A0..A3 四板为默认，不要求现场保留 A4。

### 7.3 delay 参数表

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CONFigure:CALibration:PARameter:ADD` | `<type>,<src_node>,<src_port>,<dst_node>,<dst_port>,<delay_ns>,<jitter_ns>,<count>` | `1` | 新增 staging delay 数据；目标已存在时返回 duplicate |
| `CONFigure:CALibration:PARameter:SET` | `<type>,<src_node>,<src_port>,<dst_node>,<dst_port>,<delay_ns>,<jitter_ns>,<count>` | `1` | 手动写入或覆盖 staging delay 数据 |
| `CONFigure:CALibration:PARameter:DELete` | `<type>,<src_node>,<src_port>,<dst_node>,<dst_port>` | `1` | 删除指定链路 delay；保留链路定义 |
| `READ:CALibration:PARameter?` | `[type,src_node,src_port,dst_node,dst_port]` | `delay table` | 输出端口表；表中 delay 为 ns，包含 jitter、样本数、CRC 和时间戳 |

### 7.4 字段说明

| 字段 | 取值 | 说明 |
|---|---|---|
| `type` | `SMA` / `NODE` / `DEVICE` | 校准对象类型 |
| `direction` | `FWD` / `REV` / `BIDIR` / `RING` | 链路方向；SYNC 使用的 NODE 链路必须与环路方向一致 |
| `required` | `0` / `1` | 标记该链路是否为运行门禁必需项 |
| `delay_ns` | 浮点 ns | 链路固定传播/动作 delay；SYNC 和触发预约计算使用该值 |

### 7.5 返回字段

| block | 字段 | 说明 |
|---|---|---|
| `link table` | `table_seq,slot_seq,owner,crc,stale,flags,type,src_node,src_port,dst_node,dst_port,direction,enable,required,has_delay` | 链路定义表，用于软件维护拓扑和校准入口；表头字段用于验证 owner、CRC 和快照新鲜度 |
| `delay table` | `table_seq,slot_seq,owner,crc,stale,flags,type,src_node,src_port,dst_node,dst_port,delay_ns,jitter_ns,count,timestamp,valid` | 校准 delay 数据表，供 SYNC 和触发门禁引用；大块历史不进 Vector，只回读当前摘要 |
| `result block` | `state,type,src,dst,delay_ns,jitter_ns,count,error_code,reject_reason,epoch,run_id,cal_id,cal_crc` | 最近一次测量结果；失败时 `delay_ns` 无效 |

## 8. 校准版本与质量

### 8.1 保存、激活和回滚

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CALibration:SAVE` | `<cal_id>[,scope]` | `1` | 保存 staging 校准表并生成 CRC；scope=ALL/SMA/NODE/DEVICE；涉及 flash/storage 时必须通过资源仲裁和 core1 park/lockout 门禁 |
| `CALibration:LOAD` | `<cal_id>` | `1` | 把指定校准表载入 staging，不立即影响 active 表 |
| `CALibration:ACTivate` | `<cal_id>` | `1` | accepted：激活指定校准表；只允许在 IDLE 状态执行，完成态通过 ACK/状态查询确认 |
| `CALibration:ROLLback` |  | `1` | accepted：恢复到上一次 active 校准表，完成态通过 ACK/状态查询确认 |
| `READ:CALibration:LIST?` |  | `cal list` | 读取已保存校准表的 `cal_id,crc,timestamp,scope,valid` |
| `READ:CALibration:ACTive?` |  | `active block` | 读取 active / staging 校准表标识、CRC、保存状态、修改标志和 ACK 摘要 |

### 8.2 追溯与质量

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CONFigure:CALibration:META` | `<cal_id>,<operator>,<fixture_id>,<cable_id>,<temperature_c>,<note>` | `1` | 写入 staging 校准表追溯信息 |
| `READ:CALibration:META?` | `[cal_id]` | `meta block` | 读取校准表追溯信息 |
| `CONFigure:CALibration:LIMit` | `<profile>` | `1` | 选择校准质量门限档位；完整门限由固件 profile 展开 |
| `READ:CALibration:HEALth?` | `[type]` | `health table` | 读取缺失、过期、jitter 超限和可用于 SYNC 的 NODE 链路数量 |
| `SYSTem:CALibration:LIMit:OVERRide` | `<type>,<key=value>[,...]` | `1` | 调试接口：按需覆盖指定类型的校准质量门限；未写字段保持 profile 展开值 |
| `SYSTem:CALibration:LIMit:OVERRide?` | `[type]` | `limit block` | 调试接口：读取 profile 展开后的完整校准门限和覆盖状态 |
| `SYSTem:CALibration:LIMit:DEFAult` | `[type]` | `1` | 清除调试覆盖，恢复 profile 门限 |

### 8.3 生命周期与质量字段

| 项目 | 字段/状态 | 说明 |
|---|---|---|
| 配置状态 | `staging,active,dirty,saved` | 所有 CONFigure 写入 staging；SAVE 后持久化；ACTivate 后成为 SYNC 可用 active |
| 质量状态 | `OK,MISSING,EXPIRED,JITTER_HIGH,COUNT_LOW` | health table 对每条 required 链路给出门禁结论 |
| 版本字段 | `cal_id,crc,timestamp,scope,operator,fixture_id,cable_id,temperature_c` | 用于报告追溯和现场复现 |
| SYNC 依赖 | `valid_node_link_count,missing_node_link` | 同步环只接受 active 且方向匹配的 NODE delay |

### 8.4 门禁规则

| 场景 | 规则 | 影响 |
|---|---|---|
| TRIG RUN | 禁止 LINK/PARameter/META/SAVE/LOAD/ACTivate/ROLLback | 返回状态拒绝，避免运行中改变时间基准 |
| SAVE | 只持久化 staging，不自动切换 active；涉及 flash/storage 时必须通过资源仲裁和 core1 park/lockout 门禁 | 上位机必须显式 `CALibration:ACTivate` |
| ACTivate | 切换 active 后清除最近一次 SYNC 检查结论 | 上位机必须重新 `SYNC:CHECk` |
| ROLLback | 恢复上一次 active 校准表 | 同样要求重新同步检查 |
| health fail | required 链路缺失、过期或超 jitter | 拒绝 `SYNC:STARt` 或拒绝 TRIG RUN |
| 分布式 ACK | `SAVE/ACTivate/ROLLback` 写命令只表示 accepted，目标节点完成态由 ACK 位图确认 | `ACK_TIMEOUT/BUSY/NACK` 必须进入 NACK reason |

```scpi
CONFigure:CALibration:META FIELD_20260811,OP01,FIX_A,CABLE_SET_A,28.5,FIELD_CHECK
CALibration:SAVE FIELD_20260811
CALibration:ACTivate FIELD_20260811
READ:CALibration:HEALth? NODE
```

## 9. 同步指令

### 9.1 同步边界

同步阶段的目标是让 DPLL 在 BiSS-C/RJ45_SYNC_RING 观测上建立稳态虚拟 DC。时序上，本地晶振和 `local_tick` 先存在；同步帧只提供跨节点边沿观测；DPLL 根据本地时间戳、seq、CRC、链路 delay 和节点 age 估计 `offset/rate`，进入 `LOCKED` 后才形成可用于正式运行的共同虚拟 DC。active 校准表中的 NODE 链路 delay 只用于固定 hop 补偿和拓扑门禁；`SYNC:STARt` 不测 T2。只有虚拟 DC 进入 `LOCKED` 后，DEVICE/T2 校准才具备统一时间基准，后续预测分发使用稳态 DC + T2/动作补偿执行。

### 9.2 指令分层与收敛

SYNC 域按产品框架收敛为四层，避免同一功能在多个前缀下重复暴露：

| 层级 | 入口 | 保留功能 | 不承担的功能 |
|---|---|---|---|
| 业务配置 | `CONFigure:SYNC:*` | 写 staging 同步配置：绑定校准表、ring、VDC DPLL profile、运行门禁和质量门限 | 不启动同步、不保存 active、不返回运行状态 |
| 同步动作 | `SYNC:*` | 执行检查、启动、停止、重锁、保持、保存、加载、激活和回滚 | 不直接返回完整状态；写命令只表示 accepted 或 check 结果 |
| 业务读取 | `READ:SYNC:*?` | 上位机主视图：同步状态、参数、健康度、节点、链路、检查结果、质量和版本 | 不暴露 RTOS 任务水位、不暴露调试覆盖写入口 |
| 维护诊断 | `SYSTem:SYNC:VDC:*` | VDC 服务状态和 VDC DPLL 调试覆盖，用于 SERVICE/DEBUG 联调 | 不作为产品报告主视图，不替代 `READ:SYNC:*?` |

收敛原则：

- `READ:SYNC:STATe?` 是同步状态主入口；`SYSTem:SYNC:VDC:STATus?` 只看内部服务活性。
- `READ:SYNC:QUALity?` 是同步质量主入口；全局 `READ:STATistics?` 属于报告统计域，不挂在 SYNC 模块下。
- `CONFigure:SYNC:VDC:DPLL` 只选择和保存 VDC DPLL profile；`SYSTem:SYNC:VDC:DPLL:*` 只做易失调试覆盖。
- `SYNC:CHECk` 执行一次检查并返回结果；`READ:SYNC:CHECk?` 读取最近一次检查快照，两者允许响应字段相同但动作语义不同。
- 不再新增裸顶层 `VDC:*`、`DPLL:*` 或 `STATus:VDC/DPLL?`；历史验证记录只作为历史事实保留。

### 9.3 主线挂载关系

SYNC 是 DTC100 主控制面上的一个业务域，不是独立通信主线。挂载关系如下：

```text
USBTMC / USB488
  -> task_usb_device
  -> task_scpi
  -> libscpi command table
  -> SCPI_SYNC_COMMANDS
  -> task_vdc_sync / VdcSlot / ACK
```

| 层级 | 归属 | 职责 |
|---|---|---|
| 物理入口 | USBTMC / USB488 | 承载全部 DTC100 SCPI 指令，不为 SYNC 单独开端口 |
| 解析入口 | `task_scpi` | 解析 `CONFigure:SYNC:*`、`SYNC:*`、`READ:SYNC:*?`、`SYSTem:SYNC:*`，返回 accepted 或读取快照 |
| 命令挂载 | `scpi_sync_commands.c/.h` | 在主命令表中声明 SYNC 域命令；不持有实时状态事实源 |
| 执行 owner | `task_vdc_sync` | 消费同步配置/动作事件，维护 VDC lock、offset/rate、holdover、quality |
| 事实快照 | `VdcSlot`、`NodeSlot`、`StatisticsSlot` | 给 `READ:SYNC:*?` 和运行门禁提供一致快照 |
| 运行引用 | `task_loop_engine` / `TRIGger:STARt` | 只读取 VDC 锁定和质量快照做 START/RUN 门禁，不直接调用 SYNC 命令 |

SYNC 域挂到主线后的行为规则：

- `CONFigure:SYNC:*` 写 staging 配置，完成态通过 ACK 和 `READ:SYNC:PARameter?` 读取。
- `SYNC:CHECk/STARt/STOP/RELock/HOLDover` 是动作入口，返回值只表示 accepted 或本次检查结果；跨节点完成态通过 ACK 和 `READ:SYNC:*?` 闭环。
- `READ:SYNC:*?` 是产品主视图，测试上位机和报告系统应优先使用。
- `SYSTem:SYNC:VDC:*` 是维护诊断视图，只暴露 VDC 服务和 VDC DPLL 调试，不参与现场测试主流程。
- `task_vdc_sync` 不直接产生 TRIG 边沿；它只建立共同虚拟 DC 和运行门禁事实，实时边沿仍由 `task_loop_engine` 装载、core1 执行。

### 9.4 动作指令

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `SYNC:CHECk` | `[ACTive|STAGing]` | `check block` | 校验校准表、NODE 链路、CRC、时效、方向和拓扑；省略为 ACTive |
| `SYNC:STARt` |  | `1` | accepted：启动同步环、边沿观测、DPLL 收敛和运行门禁服务；完成态通过 `READ:SYNC:STATe?` / ACK 查询 |
| `SYNC:STOP` |  | `1` | accepted：停止同步服务，退出锁定态；STOP ACK 完成前仍按当前安全策略处理已装载队列 |
| `SYNC:RELock` |  | `1` | accepted：清除当前 offset/rate 估计并重新锁定；不清除校准表和同步配置，也不自动恢复 RUN |
| `SYNC:HOLDover` | `0|1` | `1` | accepted：`1` 请求进入保持态；`0` 只请求退出保持态，必须重新 `LOCKED` 并通过运行门禁后才可恢复 |

### 9.5 读取指令

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `READ:SYNC:STATe?` |  | `sync state block` | 读取同步状态、origin、seq、本地 tick、offset/rate、holdover、节点新鲜度和 fault |
| `READ:SYNC:PARameter?` |  | `param block` | 读取绑定校准表、环路、DPLL 和门禁参数 |
| `READ:SYNC:HEALth?` |  | `health block` | 读取 CRC、seq、drop、relock、e_vdc、late 和链路时效统计 |
| `READ:SYNC:NODE?` | `[node]` | `sync node block` | 读取单节点 local_tick、offset/rate、last_seq、age 和链路健康度 |
| `READ:SYNC:LINK?` | `[src_node,dst_node]` | `link table` | 读取 NODE delay、hop 方向、required/enabled 状态和校验结果 |
| `READ:SYNC:CHECk?` |  | `check block` | 读取最近一次同步检查结果和拒绝原因 |

### 9.6 同步状态机

| 状态 | 含义 | 允许动作 |
|---|---|---|
| `IDLE` | 未启动同步或已停止 | 允许配置、加载、检查和启动 |
| `CHECKED` | active 配置和拓扑检查通过 | 允许 `SYNC:STARt` |
| `FREE_RUN` | 只有本地晶振和 `local_tick`，尚未收到有效组网观测 | 只允许维护查询和重新检查 |
| `OBSERVED` | 已收到有效 BiSS-C/RJ45 同步观测，但 offset/rate 尚未稳定 | 自动进入 `LOCKING` 或按故障策略退出 |
| `LOCKING` | DPLL 正在估计 offset/rate 并统计 `e_vdc` | 允许读取状态、健康度和维护 DPLL 调试 |
| `LOCKED` | 虚拟 DC 已锁定，满足运行门禁 | 允许 TRIG RUN |
| `HOLDOVER` | 短时失去同步帧或节点 stale，停止生成新的 FIRE_LOAD | 不允许新增预约；已装载队列按安全策略完成或撤销；重锁后也不自动恢复 RUN |
| `FAULT` | CRC、seq、拓扑或门禁故障锁存 | 读取证据后停止并清故障 |

### 9.7 故障与恢复

| 场景 | 状态变化 | 恢复动作 |
|---|---|---|
| NODE 链路缺失或方向错误 | `SYNC:CHECk` 返回失败，不进入 `CHECKED` | 读取 `READ:SYNC:LINK?`，修正校准表或 ring 顺序 |
| CRC / seq 连续超限 | `LOCKED -> FAULT` | 停止触发，读取 `READ:SYNC:HEALth?` 和故障证据 |
| 短时丢帧 | `LOCKED -> HOLDOVER` | 停止新增预约；等待重锁或执行 `SYNC:RELock`，重锁后重新 ARM/START |
| 节点 stale/missing/invalid/fault | `LOCKED/RUN -> HOLDOVER/FAULT` | 读取 `READ:SYNC:NODE?` 和 `SYSTem:REFMEM:NODE?`；不补发已过期触发 |
| e_vdc 超门限 | `LOCKED -> LOCKING/FAULT` | 读取质量数据，必要时调整 profile 或排查链路 |
| 故障锁存 | `FAULT` | 读取日志、trace、snapshot 后执行 `SYSTem:FAULT:CLEAr` |

### 9.8 同步门禁

| 场景 | 允许状态 | 说明 |
|---|---|---|
| 配置/激活 | `IDLE` | `SYNC:SAVE/LOAD/ACTivate/ROLLback` 不允许在 RUN 或锁定运行中修改 active 基准 |
| 保存同步配置 | `SystemMode=IDLE/MAINT` | 涉及 flash/storage 时必须通过资源仲裁和 core1 park/lockout 门禁 |
| 启动同步 | `CHECKED` | 最近一次 active `SYNC:CHECk` 必须通过，NODE 链路方向、CRC、版本和节点新鲜度必须匹配 |
| 分布式确认 | command ACK 完成 | `START/STOP/RELock/HOLDover/ACTivate/ROLLback` 写命令只表示 accepted，完成态通过 ACK/状态查询确认 |
| 调试 DPLL | `IDLE` / `LOCKING` | 正式 `TRIG RUN` 和 `LOCKED` 稳态运行禁止修改 DPLL 调试覆盖 |
| 触发运行 | `LOCKED` 且节点 `OK` | `FREE_RUN/OBSERVED/LOCKING/HOLDOVER/FAULT` 不能作为正式触发时间基准；任一目标节点 `STALE/MISSING/INVALID/FAULT` 时拒绝 RUN |

### 9.9 流程门禁

| 步骤 | 门禁条件 | 失败读取 |
|---|---|---|
| 配置检查 | cal_id 存在、CRC 匹配、未过期、NODE 链路方向匹配 | `READ:SYNC:LINK?` |
| 启动同步 | 最近一次 active `SYNC:CHECk` 通过，目标节点版本和新鲜度有效 | `READ:SYNC:CHECk?` |
| 进入 OBSERVED | 至少收到一轮有效同步帧，seq/CRC 正常 | `READ:SYNC:STATe?` |
| 进入 LOCKED | 连续 `lock_count` 次 e_vdc 落入当前 `lock_acceptance_threshold_ns`；初步 bring-up 可选 10 us / 1 us / 100 ns 三档 | `READ:SYNC:HEALth?`、`READ:SYNC:QUALity?` |
| 触发运行 | required_lock=1 时必须 LOCKED，且 `lock_quality_tier=FINE_100NS`、e_vdc、age、CRC、seq、节点新鲜度低于门限 | `READ:SYNC:STATe?` |
| HOLDOVER 恢复 | 重锁后只恢复同步锁定，不自动恢复 TRIG RUN | `READ:SYNC:STATe?`、`SYSTem:CONFigure:ACK?` |

### 9.10 DPLL 调试范围

| 参数 | 范围 | 说明 |
|---|---|---|
| `profile` | `DEFAULT` / `STRICT` / `RELAXED` / `FAST_LOCK` / `LOW_JITTER` | 业务配置只选择固件内置 profile |
| `cal_limit key` | `max_delay_ns` / `max_jitter_ns` / `min_count` / `max_age_s` | 校准调试覆盖字段，可只写需要临时调整的字段 |
| `sync_limit key` | `max_crc` / `max_seq` / `max_drop` / `max_relock` / `max_evdc_p99_ns` / `max_evdc_p999_ns` / `min_lock_count` | 同步调试覆盖字段，可只写需要临时调整的字段 |
| `bandwidth_hz` | `0.01 .. 20` | 维护调试用等效环路带宽 |
| `damping` | `0.3 .. 2.0` | 维护调试用阻尼系数 |
| `max_slew_ppm` | `1 .. 200` | offset/rate 修正限幅 |
| `coef_source` | `DEFAULT` / `PROFILE` / `OVERRIDE` | 当前系数来源 |

## 10. 同步参数与流程

### 10.1 同步配置参数

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CONFigure:SYNC:CALibration` | `<cal_id>,<cal_crc>,<max_age_s>` | `1` | 绑定同步使用的校准表到 staging 配置 |
| `CONFigure:SYNC:RING` | `<origin>,<node_order>,<period_us>,<bitrate>,<timeout_ms>,<crc_limit>` | `1` | 配置 RJ45_SYNC_RING；`node_order` 每一跳必须匹配同方向 NODE 校准链路 |
| `CONFigure:SYNC:VDC:DPLL` | `<lock_acceptance_threshold_ns>,<lock_count>,<holdover_ms>,<relock_ms>,<profile>` | `1` | 配置虚拟 DC 时钟同步 DPLL 的锁定、保持和重锁判据；维护 bring-up 可用 10000/1000/100 ns 三档，正式运行仍要求 100 ns quality tier |
| `CONFigure:SYNC:GATE` | `<required_lock>,<max_age_ms>,<max_evdc_p99_ns>,<allow_holdover>` | `1` | 配置触发运行门禁 |

### 10.2 同步字段

| 字段组 | 字段 | 说明 |
|---|---|---|
| `param` | `table_seq, sync_id, cal_id, cal_crc`<br>`epoch, run_id, max_age_s, origin`<br>`node_order, period_us, bitrate, lock_acceptance_threshold_ns`<br>`holdover_ms, relock_ms, dpll_profile, gate_profile` | 同步使用的校准表、环路配置、DPLL profile、版本上下文和锁定判据 |
| `health` | `slot_seq, owner, crc, stale`<br>`crc_count, seq_error, drop_count, relock_count`<br>`evdc_p99_ns, evdc_p999_ns, late_count, stale_count`<br>`holdover_count, fault_count, gate_state, reject_reason` | 同步环健康度、虚拟 DC 残差、节点新鲜度和运行门禁依据 |
| `node` | `node, role, freshness, stale`<br>`local_tick, dc_tick, offset_tick, rate_q32`<br>`last_seq, age_ms, link_delay_ns, lock_state`<br>`crc_count, seq_error, fault_code, flags` | 单节点本地 tick、DPLL 估计结果、校准链路 delay 使用值和 `OK/STALE/MISSING/INVALID/FAULT` 新鲜度 |
| `check` | `check_state, target_config, cal_id, cal_crc`<br>`sync_id, sync_crc, node_order, topology_ok`<br>`missing_link, expired_link, direction_mismatch, node_freshness`<br>`missing_node, stale_node, invalid_node, reject_reason` | 启动前定位缺失链路、过期校准表、方向错误、节点新鲜度和门禁拒绝原因 |

### 10.3 拓扑检查字段

| 字段 | 含义 | 规则 |
|---|---|---|
| `node_order` | 同步环顺序 | 产品默认 `A0>A1>A2>A3>A0`；调试 HIL 可为 `A0>A1>A2>A3>A4>A0` |
| `required_link_count` | 必需 NODE 链路数量 | 产品四板环路为 4；A4 调试拓扑为 5 |
| `valid_link_count` | active 校准表中有效同向链路数量 | 必须等于 required_link_count |
| `missing_link` | 缺失链路列表 | 为空才允许启动 |
| `direction_mismatch` | 方向错误列表 | A0->A1 与 A1->A0 不能混用 |
| `expired_link` | 超过 max_age_s 的链路 | required 链路过期时拒绝启动 |
| `node_freshness` | 目标节点新鲜度摘要 | 必须全部为 `OK` 才允许进入 RUN |
| `missing_node` | 启动后从未见过的节点 | 非空时禁止 ARM/START |
| `stale_node` | 超过 stale window 未更新的节点 | RUN 中进入 HOLDOVER 或 FAULT，禁止补发过期触发 |
| `invalid_node` | CRC、版本、role 或 frame mask 不匹配的节点 | 禁止 ARM/RUN |

```scpi
CONFigure:SYNC:CALibration FIELD_20260811,3A91C027,86400
CONFigure:SYNC:RING A0,A0>A1>A2>A3>A0,1000,12500000,20,0
CONFigure:SYNC:VDC:DPLL 300,100,200,1000,LOW_JITTER
CONFigure:SYNC:GATE 1,50,100,0
SYNC:CHECk STAGing
SYNC:SAVE FIELD_SYNC_20260811
SYNC:ACTivate FIELD_SYNC_20260811
SYNC:CHECk
SYNC:STARt
READ:SYNC:STATe?
```

## 11. 同步版本与质量

### 11.1 版本管理

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `SYNC:SAVE` | `<sync_id>[,scope]` | `1` | 保存 staging 同步配置并生成 CRC；scope=ALL/CAL/RING/DPLL/GATE/LIMIT；涉及 flash/storage 时必须通过资源仲裁和 core1 park/lockout 门禁 |
| `SYNC:LOAD` | `<sync_id>` | `1` | 载入同步配置包到 staging，不立即影响 active |
| `SYNC:ACTivate` | `<sync_id>` | `1` | accepted：激活同步配置包；只允许在 IDLE 状态执行，完成态通过 ACK/状态查询确认 |
| `SYNC:ROLLback` |  | `1` | accepted：恢复到上一次 active 同步配置包，完成态通过 ACK/状态查询确认 |
| `READ:SYNC:LIST?` |  | `sync list` | 读取已保存配置包的 id、CRC、时间戳、scope 和 valid 标志 |
| `READ:SYNC:ACTive?` |  | `active block` | 读取 active/staging 配置、绑定校准表、CRC、修改标志、ACK 摘要和最近检查状态 |

### 11.2 质量判据和 DPLL

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CONFigure:SYNC:LIMit` | `<profile>[,<key=value>[,...]]` | `1` | 选择同步质量门限档位；调试时可追加字段覆盖，未写字段保持 profile 展开值 |
| `READ:SYNC:QUALity?` | `[sync_id]` | `quality block` | 读取质量结论、e_vdc 分布、错误计数、链路年龄、门禁拒绝原因和 100 ns / 1 us / 10 us 锁定质量等级 |
| `READ:SYNC:VERSion?` |  | `version block` | 读取同步配置版本、绑定校准版本、固件版本、硬件 profile 和最近激活时间 |
| `SYSTem:SYNC:VDC:STATus?` |  | `vdc block` | 读取同步域虚拟 DC 服务内部状态；产品报告优先用 `READ:SYNC:STATe?` |
| `SYSTem:SYNC:VDC:DPLL:STATus?` |  | `dpll block` | 读取 VDC DPLL 内部状态；用于维护和闭环调试 |
| `SYSTem:SYNC:VDC:LOCK:READiness?` |  | `vdc lock readiness block` | 读取最小 VDC 实例锁定就绪证据；`input_ready` 与 `locked` 分离，当前诊断 latch 应停在 timestamp not eligible |
| `SYSTem:SYNC:VDC:OBServer:TDMA:SELFtest` | `[role],[output_index],[observed_mask],[initial_sample_mask],[sample_period_ns],[pulse_period_ns],[pulse_high_ns],[pulse_count],[frame_crc32],[start_delay_ns]` | `1` | 维护态 VDC/TDMA bring-up：TX 提交公共 TDMA `VDC_SYNC_SAMPLE` 诊断 frame，RX 侧周期性 observation window + capture；不写 DPLL lock/offset/rate |
| `SYSTem:SYNC:VDC:OBServer:TDMA:SELFtest?` |  | `vdc selftest block` | 读取 self-test 最近一次配置、schedule CRC、错误码、start delay 和 first window start |
| `SYSTem:SYNC:VDC:OBServer:TDMA` | `[enabled],[initial_sample_mask],[sample_period_ns],[frame_crc32]` | `1` | 从 active `VDC_OBSERVATION_WINDOW` 自动配置 observer expected/base 时间；用于最小实例 bring-up，不启动 capture，不改变 DPLL |
| `SYSTem:SYNC:VDC:OBServer` | `[enabled],...` | `1` | 配置 SYNC_IO raw capture 到 VDC compact observation 的 observer；无参数或 `0` 关闭，启用态必须提供完整时间戳和 gate 参数 |
| `SYSTem:SYNC:VDC:OBServer?` |  | `vdc observer block` | 读取 SYNC_IO raw capture 到 VDC compact observation 的 observer 状态；只读维护证据，不改变 DPLL |
| `SYSTem:SYNC:VDC:DPLL:TUNE` | `<bandwidth_hz>,<damping>,<max_slew_ppm>` | `1` | 按等效传递函数参数覆盖 VDC 虚拟环路滤波器，仅用于调试 |
| `SYSTem:SYNC:VDC:DPLL:COEFficient` | `<kp_q31>,<ki_q31>,<max_slew_ppm>` | `1` | 直接覆盖离散 PI 环路系数 |
| `SYSTem:SYNC:VDC:DPLL:OVERRide?` |  | `override block` | 读取调试覆盖是否生效、来源、允许状态、最近写入时间和清除原因 |
| `SYSTem:SYNC:VDC:DPLL:COEFficient?` |  | `coef block` | 读取当前环路滤波器系数、来源、限幅和生效状态 |
| `SYSTem:SYNC:VDC:DPLL:DEFAult` |  | `1` | 清除调试覆盖，恢复内置 profile |

### 11.3 质量字段

| 字段 | 来源 | 说明 |
|---|---|---|
| `e_vdc_p99_ns` | 虚拟 DC 环路 | 同步残差 P99，用于判断多节点同步是否稳定 |
| `crc_count / seq_error` | RJ45_SYNC_RING | 同步帧通信质量；超过门限进入 FAULT 或拒绝 TRIG RUN |
| `relock_count` | DPLL 状态机 | 重锁次数过多说明链路噪声、丢帧或环路参数不合适 |
| `node_freshness` | DistributedVectorTable / NodeSlot | `OK/STALE/MISSING/INVALID/FAULT`；非 OK 时拒绝 ARM/RUN 或进入 HOLDOVER/FAULT |
| `ack_flags / nack_flags / busy_flags / timeout_flags` | AckCommandSlot | 区分命令 accepted 和分布式完成态 |
| `epoch / run_id` | SystemSlot / REFMEM_EPOCH | 把同步、T2、故障和报告绑定到同一轮运行上下文 |
| `gate_state` | 运行门禁 | READY/BLOCKED；BLOCKED 时给出 reject_reason |

校准质量门限和 DPLL 的调试覆盖均为易失态，不写入出厂默认 profile。同步质量门限统一由 `CONFigure:SYNC:LIMit` 管理；执行不带 `key=value` 的 profile 配置时清除同步门限临时覆盖。`READ:CALibration:HEALth?`、`READ:SYNC:PARameter?`、`READ:SYNC:QUALity?` 应返回 profile 展开值和 override 标志。

DPLL 是实现稳态 DC 时钟的基础环路：它不产生物理时钟，而是把各节点本地晶振产生的 `local_tick` 映射到共同虚拟 DC。BiSS-C/RJ45 组网提供同步帧和边沿观测，DPLL 持续估计各节点相对 DC 的 offset/rate，并输出 `FREE_RUN`、`OBSERVED`、`LOCKING`、`LOCKED`、`HOLDOVER` 等同步状态。T2/DEVICE 校准应在 DC `LOCKED` 后执行，用统一时间基准测得动作补偿；预测分发再使用稳态 DC + T2/动作补偿生成本地预约。调试覆盖只允许在 `IDLE` 或 `LOCKING` 使用；正式 `TRIG RUN` 禁止修改。同步 DPLL 不等同于扫描角度预测 DPLL。

## 12. 调试功能

调试功能面向完整调试上位机，不是测试上位机最小流程的一部分。调试上位机的目标是作为
当前系统任意状态的验证和控制入口：DTC、转台、网分联动、CAL/SYNC、资源门禁、故障恢复、
trace 和维护诊断都应可达。控制范围不再按固定小集合理解，而是按可配置权限模型管理；
同一套调试上位机可加载四级权限 profile：`TEST/SERVICE/DEBUG/FACTORY`。所有动作都必须通过
core0 控制面、权限表、资源仲裁、SystemModeTable、分布式 ACK 和安全边界闭环，不能直接影响
core1 已装载边沿和实时快路径。

### 12.1 四级权限模型

| 权限 | 对应上位机/角色 | 主要调试功能 | 典型限制 |
|---|---|---|---|
| `TEST` | 现场测试程序 / 操作员 | 覆盖 P5-P7 现场测试业务指令：读取设备信息和配置摘要，装载测试 recipe，配置触发参数、扫描角度、角度脉冲、断点/续测和 active sequence，执行 `SYNC:CHECk`，启动/暂停/继续/停止测试，读取 RUN summary、序列状态、同步状态和故障摘要 | 业务写入只允许在 IDLE/CONFIG/ARM 前或安全暂停边界；RUN 中只从网分拿数据，并保留安全停止和只读摘要；不做分布式维护、校准写入、同步调参、存储写、权限修改和任意状态强控 |
| `SERVICE` | 维护上位机 / 现场服务 | 继承 `TEST`；增加校准/同步维护：CAL/SYNC SAVE/LOAD/ACTivate/ROLLback、DPLL 调试覆盖、资源诊断、存储导出、长稳验证 | 需要 IDLE/MAINT、资源仲裁、core1 park/lockout 和审计记录 |
| `DEBUG` | 调试上位机 / 开发调试 | 继承 `SERVICE`；增加当前系统任意状态验证：业务配置、序列检查、转台/网分联动、状态机推进、同步重锁、HOLDOVER/STOP/FAULT 处理、trace/log/snapshot 采集 | RUN 中改 active 配置、已冻结补偿或实时边沿必须排队/拒绝；破坏性维护仍需 FACTORY 授权 |
| `FACTORY` | 工厂/工程授权 | 继承 `DEBUG`；增加出厂与工程功能：权限 profile 配置、USB/boot/OTA、硬件 profile、flash/storage 维护、保护策略验证 | 必须有本地授权或物理维护条件；禁止在正式 RUN 中执行破坏性操作 |

### 12.2 TEST 最小业务闭环权限

现场测试程序只分配 `TEST` 权限，因此 `TEST` 必须覆盖 P5-P7 的现场测试业务指令，以及
“转台输出角度脉冲 -> A0 计数 -> A0
分发时钟/角度事件 -> 各板卡计数预约触发 -> 网分采集数据”的完整测试链路。`TEST`
不是调试权限，但它需要具备正常业务写入能力；限制点在于只能配置本次测试 recipe，不能在
RUN 中替换已冻结配置，也不能进入校准、同步调参、存储维护或权限维护域。

| 阶段 | `TEST` 必须可用的代表指令 | 状态边界 | 说明 |
|---|---|---|---|
| 启动识别 | `*IDN?`、`SYSTem:VERSion?`、`SYSTem:CORE?`、`SYSTem:RTOS:STATus?`、`SYSTem:SCPI:ROLE?` | 任意状态只读 | 确认连接对象、固件版本、核心/节点状态和当前权限为 `TEST` |
| 测试 recipe 装载 | P5 业务配置：`CONFigure:TRIGger`、`CONFigure:ANGLe:SWEEP`、`CONFigure:ANGLe:PULSe`、`READ:TRIGger:PARameter?`、`READ:ANGLe:*?` | IDLE/CONFIG/ARM 前写入，任意状态只读 | 设置一次现场测试所需的触发参数、扫描角度集合和转台脉冲输入阈值 |
| 序列准备 | `CONFigure:SEQuence`、`CONFigure:SEQuence:ACTive`、`READ:SEQuence:ACTive?` | IDLE/CONFIG/ARM 前 | 写入或选择测试序列；`CONFigure:SEQuence:ACTive` 内部完成序列校验并冻结 active sequence，失败原因通过 ACK/NACK 和 `READ:SEQuence:ACTive?` 查询；`TRIGger:STARt [plan_id]` 可作为激活并启动的受限便捷事务 |
| 同步门禁 | `SYNC:CHECk`、`READ:SYNC:STATe?`、`READ:SYNC:HEALth?`、`READ:SYNC:LINK?`、`READ:SYNC:CHECk?` | START 前；RUN 中只读 | 检查 active CAL/SYNC、NODE 链路和 DPLL 锁定质量；不允许 `TEST` 修改 DPLL 或同步 profile |
| 启动运行 | P7 运行控制：`TRIGger:MODE 0|1`、`TRIGger:STARt [plan_id]`、`READ:TRIGger:STATe?`、`READ:TRIGger:PARameter?` | IDLE/CONFIG/ARM 前启动；RUN 中只读 | `TEST` 只允许设置 IDLE/TRIG 两种模式；`START` 只负责进入运行，不再承担任意配置；带 `plan_id` 时仍必须通过 active sequence 校验和门禁 |
| 断点续测 | `CONFigure:ANGLe:BREAkpoint`、`CONFigure:ANGLe:BREAkpoint:CLEAr`、`READ:ANGLe:BREAkpoint?`、`TRIGger:PAUSe`、`TRIGger:CONTinue` | IDLE/CONFIG 或安全暂停边界 | 归属现场测试业务，用于断点续测和停点恢复；是否在 UI 显示由 `TEST` profile 开关决定，不需要 `DEBUG` |
| RUN 态安全动作 | `TRIGger:STOP`、`READ:ANGLe:POSition?`、`READ:ANGLe:PULSe?`、`READ:TRIGger:STATe?`、`READ:SYNC:STATe?` | RUN 中 `STOP` 安全执行，其余只读 | 测试程序 RUN 中主要从网分取数据；DTC 只提供安全停止、运行状态和只读证明 |
| 结束复盘 | `SYSTem:RUN:LAST?`、`SYSTem:RUN:SUMMary?`、`SYSTem:RUN:LOG?`、`SYSTem:FAULT:LAST?`、`SYSTem:ERRor?`、`SYSTem:LOG:PAGE?` | STOP/IDLE/FAULT 后只读 | 生成现场测试报告，记录触发计数、漏脉冲、同步质量、错误、日志和故障摘要 |

### 12.3 全状态权限策略矩阵

| 类别 | 代表指令 | 权限/状态策略 | 说明 |
|---|---|---|---|
| 只读观察 | `READ:*`、`SYSTem:*?`、trace/log/snapshot 查询 | `TEST+` 通常允许 | 只读快照，不触发现场 IO，不阻塞实时路径；stale 时返回 stale 标志 |
| P5-P7 业务页 | `CONFigure:TRIGger`、`CONFigure:ANGLe:*`、`CONFigure:SEQuence:*`、`READ:SEQuence:*?`、`TRIGger:MODE 0|1`、`TRIGger:STARt/STOP/PAUSe/CONTinue`、`READ:TRIGger:*?`、`CONFigure:SWITch#`、`READ:SWITch#?` | `TEST+`，按 IDLE/CONFIG/ARM/PAUSE/RUN 安全边界执行 | 现场测试程序必须能完成业务闭环；`TEST` 不允许进入 CAL/SYNC/SIM 模式；RUN 中禁止替换 active/冻结配置，独立开关仅在序列引擎未占用时允许 |
| 同步门禁查询 | `SYNC:CHECk`、`READ:SYNC:*` | `TEST+`；`SYNC:CHECk` 仅 START 前，RUN 中只读 | 用于测试启动门禁和报告证明；不允许修改同步 profile、DPLL 覆盖或 HOLDOVER 策略 |
| 任意状态调试控制 | 强制切换、任意业务重配置、状态机推进、异常注入、越过门禁的验证动作 | `DEBUG+`，RUN 中通常 `QUEUE/DENY` | 面向调试上位机验证当前系统任意状态；不能把 `TEST` 的正常业务能力误划到这里 |
| 转台/网分联动 | 运动控制器 API、网分采集/状态 API | `DEBUG+`，由外设 owner 决定 | 调试上位机可直接控制外设；DTC 只接收角度脉冲并返回推导状态 |
| 同步维护 | `SYNC:RELock`、`SYNC:HOLDover`、`SYNC:SAVE/LOAD/ACTivate/ROLLback` | `SERVICE+`，按状态策略执行 | 重锁不自动恢复 TRIG RUN；RUN 中控制需进入安全边界 |
| 联动校准 | `CALibration:*`、DEVICE/T2 测量 | `SERVICE+`，IDLE/CAL 优先 | 不能改变本轮已冻结补偿；DEVICE/T2 校准应使用锁定 DC 基准 |
| 模式扩展 | `TRIGger:MODE 2|3`、`TRIGger:MODE 4` | `2=CAL`、`3=SYNC` 需要 `SERVICE+`；`4=SIM` 需要 `DEBUG+` | 模式值按权限单独判定，不能因 `TRIGger:MODE` 命令本身在 P7 而自动开放给 `TEST` |
| 深度调试 | 状态机推进、DPLL 覆盖、异常注入、trace 强制采集 | `DEBUG+` | 用于开发验证当前系统任意状态；破坏性动作仍受 FACTORY/资源门禁限制 |
| 维护/存储/升级 | `CALibration:SAVE`、`SYNC:SAVE`、`SYSTem:OTA:*`、flash/storage | `SERVICE+`；破坏性项需 `FACTORY` | 必须满足资源仲裁、core1 park/lockout 和持久化门禁 |
| 权限配置 | `SYSTem:SCPI:PERMission*`、`SYSTem:SCPI:ROLE*` | `FACTORY` | 修改调试上位机自身权限 profile；变更必须可审计并可回读 |

### 12.4 调试上位机闭环

```text
任意状态调试命令
-> SCPI/A3 gateway
-> PermissionProfile + SystemModeTable + StatePolicyTable
-> ResourceArbiter
-> AO event / command_seq
-> Distributed ACK / state snapshot
-> READ/SYSTem 查询完成态
```

调试上位机可以开放全量按钮和页面，但 UI 应显示每条命令在当前模式下的策略：
`ALLOW`、`QUERY_ONLY`、`SAFE_BOUNDARY`、`QUEUE`、`DENY`。任何 `DENY/QUEUE/BUSY/NACK`
都必须能通过 `SYSTem:SCPI:PERMission?`、`SYSTem:SCPI:RUN:ALLOW?`、`SYSTem:CONFigure:ACK?`、`SYSTem:CONFigure:NACK?`
和相关 `READ:*` 查到原因。
权限后缀 `+` 按 `TEST < SERVICE < DEBUG < FACTORY` 展开。

### 12.5 调试功能指令入口

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `SYSTem:SCPI:PERMission?` | `[role|command]` | `permission block` | 查询当前权限 profile、命令域授权、允许状态和审计标志 |
| `SYSTem:SCPI:PERMission` | `<role>,<command>,<policy>[,<states>]` | `1` | 设置调试权限条目；仅 `FACTORY` 可写，完成态通过 ACK/日志确认 |
| `SYSTem:SCPI:ROLE?` |  | `role block` | 查询当前连接/会话角色：`TEST/SERVICE/DEBUG/FACTORY` |
| `SYSTem:SCPI:ROLE` | `<role>[,<token>]` | `1` | 切换调试权限 profile；实现可绑定物理授权、token 或本地维护模式 |
| `SYSTem:SCPI:RUN:ALLOW?` | `[index]` | `policy block` | 查询 RUN 态策略表；它是权限表在 RUN 状态下的结果，不是完整控制能力边界 |
| `SYSTem:RESource?` |  | `resource block` | 查询资源占用、冲突、request owner 和 holder owner |
| `SYSTem:CONFigure:ACK?` |  | `ack block` | 查询调试命令 accepted 后的分布式完成态 |
| `SYSTem:CONFigure:NACK?` | `[reason_id]` | `reason block` | 查询拒绝原因表 |
| `SYSTem:CORE:VECTOR?` |  | `core vector block` | 查询 core0/core1 VTOR、IRQ owner、entry owner 和 guard |
| `SYSTem:PROTection:STATus?` |  | `runtime protection block` | 查询 RAM-resident、flash lockout/park 和入口归属 |
| `SYSTem:RUN:LOG?` | `[run_id,page]` | `run log page block` | 调试导出 RUN 短日志 |

## 13. 维护指令

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `SYSTem:USB:MODE?` |  | `USBTMC` | 查询当前 USB 模式；成品固定为 USBTMC |
| `SYSTem:USB:MODE` | `CDC|USBTMC` | `1` | validation 固件维护命令：写入下次启动 USB 模式 |
| `SYSTem:USB:BOOT` |  | `1` | 重启并应用 USB 模式 |
| `SYSTem:OTA:STATus?` |  | `OTA block` | 查询升级流程状态 |
| `SYSTem:SD:STATus?` |  | `storage block` | 查询 SD、日志、snapshot 和报告写入状态 |

### 13.1 通信维护指令

通信维护域用于 UART、RS485、BiSS-C 等外部或板间接口的配置、状态和验证。USB 仍归
`SYSTem:USB:*`，反射内存仍归 `SYSTem:REFMEM:*`。UART 首版先冻结 SCPI 入口，后端驱动和
owner task 后续接入。

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `COMMunication:SERial:UART#:BAUD` | `<baud>` | `1` | 设置 UART# 波特率 |
| `COMMunication:SERial:UART#:BAUD?` |  | `<uart>,<baud>` | 查询 UART# 波特率 |
| `COMMunication:SERial:UART#:FORMat` | `<data_bits>,<parity>,<stop_bits>` | `1` | 设置 UART# 帧格式，`parity=NONE/EVEN/ODD` |
| `COMMunication:SERial:UART#:FORMat?` |  | `<uart>,<data_bits>,<parity>,<stop_bits>` | 查询 UART# 帧格式 |
| `COMMunication:SERial:UART#:STATe` | `<0|1>` | `1` | 关闭或使能 UART# 维护端口 |
| `COMMunication:SERial:UART#:STATe?` |  | `<uart>,<state>,<enable>` | 查询 UART# 使能状态 |
| `COMMunication:SERial:UART#:STATus?` |  | `uart status block` | 查询 UART# ready、配置、收发计数和后端接入状态 |
| `COMMunication:SERial:UART#:TX:TEST` | `<count>[,<pattern>]` | `1` | 发送 UART# 测试帧 |
| `COMMunication:SERial:UART#:TX:TEST?` |  | `<uart>,<count>,<state>` | 查询最近一次 UART# 发送测试摘要 |
| `COMMunication:SERial:UART#:RX:COUNt?` |  | `<uart>,<byte_count>,<frame_count>` | 查询 UART# 接收计数 |
| `COMMunication:SERial:UART#:ERRor?` |  | `<uart>,<error_count>,<last_error>` | 查询 UART# 错误计数和最近错误 |

### 13.2 错误处理速查

| 场景 | 上位机处理 |
|---|---|
| 配置校验失败 | 读取 `SYSTem:CONFigure:NACK?`、`SYSTem:CONFigure:ACK?` 和 `READ:SEQuence:ACTive?` |
| 运行 late 或 timeout | 停止后读取 `READ:TRIGger:STATe?`、`SYSTem:FAULT:LAST?` 和 trace/snapshot |
| 同步失锁 | 读取 `READ:SYNC:STATe?`、`READ:SYNC:HEALth?` 和 `READ:SYNC:LINK?` |
| 校准失败 | 读取 `READ:CALibration:STATe?` 和 `READ:CALibration:RESult?`；失败不覆盖旧 staging 数据 |
| 报告导出 | 按页读取 `SYSTem:LOG:PAGE?`、`SYSTem:TRACe:DATA?`、`SYSTem:SNAPshot:DATA?` 和 `SYSTem:T2:DATA?` |

## 14. 响应数据格式

### 14.1 文本 block

| block | 字段 | 用途 |
|---|---|---|
| `state block` | `state,sub_state,epoch,run_id,seq,progress,error_code,reject_reason` | 运行态、同步态、校准态通用状态读取 |
| `link table` | `table_seq,slot_seq,owner,crc,stale,flags,type,src_node,src_port,dst_node,dst_port,direction,enable,required,has_delay` | 校准链路表固定字段，支持 owner、CRC 和 stale 检查 |
| `delay table` | `table_seq,slot_seq,owner,crc,stale,flags,type,src_node,src_port,dst_node,dst_port,delay_ns,jitter_ns,count,timestamp,valid` | 校准 delay 摘要固定字段，完整历史仍在 storage |
| `sync state block` | `table_seq,slot_seq,owner,crc,stale,flags,state,sync_id,sync_crc,cal_id,cal_crc,epoch,run_id,node_order,origin,local_tick,dc_tick,offset_tick,rate_q32,last_seq,age_ms,holdover_ms,e_vdc_p99_ns,node_freshness,fault_code,reject_reason` | `READ:SYNC:STATe?` 固定字段；`local_tick` 是本地晶振 tick，`dc_tick` 是 DPLL 映射后的虚拟 DC |
| `sync node block` | `table_seq,slot_seq,owner,crc,stale,flags,node,role,freshness,local_tick,dc_tick,offset_tick,rate_q32,last_seq,age_ms,link_delay_ns,lock_state,crc_count,seq_error,fault_code` | `READ:SYNC:NODE?` 固定字段；用于定位单节点观测、DPLL 估计和链路健康 |
| `param block` | `id,crc,version,modified,epoch,run_id,field...` | 配置参数读取 |
| `permission block` | `role,command,domain,policy,states,audit_required,source,version,modified,deny_reason` | 权限 profile 查询；用于调试上位机显示每级权限可用功能 |
| `role block` | `current_role,role_order,available_roles,auth_state,expires_ms,source,last_change,audit_seq` | 当前调试会话角色、四级权限顺序和授权状态 |
| `sequence block` | `query_plan_id,query_index,sequence_crc,state_count,index_valid,state_id...` | 查询配置序列库；支持读取指定序列摘要或任意一条状态引用 |
| `sequence check block` | `plan_id,state_count,sequence_crc,state_range_ok,map_crc_ok,switch_range_ok,pol_range_ok,freq_range_ok,wave_range_ok,duplicate_state,missing_state,reject_reason` | 详细预检结果；`TEST+` 可选读取，调试上位机可显示逐项诊断，功能上不替代 active sequence 查询 |
| `sequence active block` | `active_plan_id,active_crc,state_count,valid,last_check_state,reject_reason` | 查询当前活动序列；测试上位机 START 前确认将要执行的 active sequence |
| `quality block` | `state,last_offset_ns,rms_offset_ns,max_abs_offset_ns,freq_offset_ppb,jitter_pk_ns,last_sample_age_1e3ns,last_reject_code,accepted_sample_count,rejected_sample_count,last_timestamp_resolution_ns,health_state,lock_quality_tier,fine_lock_threshold_ns,debug_lock_threshold_ns,coarse_lock_threshold_ns,lock_acceptance_threshold_ns` | VDC 同步质量统计；`lock_quality_tier=1/2/3` 对应 10 us / 1 us / 100 ns，只有 100 ns fine tier 可作为正式 RUN 质量依据 |
| `ack block` | `command_seq,target_mask,ack_flags,nack_flags,busy_flags,timeout_flags,nack_reason[node]` | 分布式命令完成态；写命令返回 accepted 后由该 block 闭环 |
| `fault block` | `fault_code,source_node,first_ts,last_ts,evidence_seq,sticky,recoverable,text` | 故障锁存证据 |
| `run summary block` | `run_id,plan_id,start_time,stop_time,complete_state,angle_start,angle_stop,angle_step,angle_count,angle_hit_count,missed_angle_pulse_count,seq_count,total_trigger_count,late_count,fault_count,sync_state,config_crc,sequence_crc,cal_crc,reject_reason` | RUN 后摘要；测试上位机用于报告和复盘，不作为 RUN 中控制节拍 |
| `angle pulse block` | `edge,pulse_width_us,timeout_ms,angle_pulse_count,missed_angle_pulse_count,last_edge_time,timeout_state,valid,fault_code` | A0 接收转台目标角度触发脉冲的角度域配置和统计 |
| `position block` | `source,angle_index,angle_count,current_angle_by_sweep,next_angle_by_sweep,angle_pulse_count,last_pulse_time,valid,stale,fault_code` | DTC 侧扫描游标和角度脉冲推导状态；真实转台位置由运动控制器 API 提供 |
| `core vector block` | `version,table_seq,core_count,core0_vtor_owner,core1_vtor_owner,core0_irq_owner_mask,core1_irq_owner_mask,entry_table_owner,flags,guard_owner,guard_crc,guard_stale,guard_flags` | core0/core1 入口表、IRQ owner 和 guard 观测 |
| `runtime protection block` | `version,table_seq,ram_resident_required,flash_lockout_supported,flash_lockout_online,flash_lockout_requested,flash_lockout_acknowledged,park_state,last_result,last_elapsed_us,request_seq,ack_seq,release_seq,timeout_count,release_timeout_count,entry_table_owner,flags,guard_owner,guard_crc,guard_stale,guard_flags` | RAM-resident、flash lockout/park、入口归属和 S0 lockout 证据观测 |
| `vdc block` | `ready,lock_state,service_count,first_service_ms,last_service_ms,sync_seq` | 同步域维护字段；产品上位机优先使用 `READ:SYNC:STATe?` |
| `dpll block` | `ready,state,service_count,first_service_ms,last_service_ms,update_seq` | 开发验证兼容字段；用于扫描/转台 DPLL 任务观测 |
| `vdc lock readiness block` | `input_ready,locked,reason,lock_state,health_state,accepted_sample_count,rejected_sample_count,last_reject_code,observer_enabled,observer_submitted,observer_accepted,observer_rejected,observer_last_gate,last_timestamp_source,last_timestamp_resolution_ns,last_timestamp_flags,timestamp_dpll_eligible,dictionary_entry_count,dictionary_crc32,dictionary_profile_crc32,schedule_crc32,last_payload_class,last_source_slot_id,last_reference_slot_id` | `SYSTem:SYNC:VDC:LOCK:READiness?` 固定字段；用于确认 VDC 最小实例卡在 observer、dictionary、timestamp eligibility、gate 还是 DPLL servo，不改变状态 |
| `vdc selftest block` | `active,role,output_index,observed_mask,initial_sample_mask,sample_period_ns,pulse_period_ns,pulse_high_ns,pulse_count,frame_crc32,schedule_crc32,last_error,started_ms,start_delay_ns,first_window_start_lo,first_window_start_hi` | `SYSTem:SYNC:VDC:OBServer:TDMA:SELFtest?` 固定字段；用于记录 VDC/TDMA selftest 的 RX/TX 参数、active schedule 摘要和诊断 gate 证据 |
| `vdc observer block` | `enabled,max_words_per_service,service_count,raw_word_count,no_edge_count,ambiguous_edge_count,bad_argument_count,submitted_count,accepted_count,rejected_count,last_capture_result,last_raw_word,last_sample_seq,last_event_id,last_tick_l32,last_gate_reject_code,previous_sample_mask,next_base_time_l32_ns,rising_event_id,falling_event_id,observed_mask,initial_sample_mask,sample_period_ns,expected_window_start_lo,expected_window_start_hi,frame_crc32,max_backward_ticks,quality_flags,sample0_lsb,schedule_crc32,dictionary_crc32,dictionary_entry_count,dictionary_profile_crc32,last_edge_index,last_timestamp_source,last_timestamp_resolution_ns,last_timestamp_flags,last_source_slot_id,last_reference_slot_id,last_payload_class` | `SYSTem:SYNC:VDC:OBServer?` 固定字段；前 18 字段保留原 observer 计数，后续字段用于 HIL 记录配置、CRC、edge index 和 timestamp dictionary 展开证据 |
| `vdc observer config` | `enabled,max_words_per_service,rising_event_id,falling_event_id,observed_mask,initial_sample_mask,next_base_time_l32_ns,sample_period_ns,expected_window_start_lo,expected_window_start_hi,frame_crc32,max_backward_ticks,quality_flags,sample0_lsb` | `SYSTem:SYNC:VDC:OBServer` 的启用态参数；无参数或 `enabled=0` 表示关闭并清零 observer，配置不启动 capture；`quality_flags bit31` 表示 expected/base 时间来自 active TDMA observation window |

### 14.2 二进制 block

| 数据 | 格式 | 说明 |
|---|---|---|
| `CONFigure:SEQuence` | IEEE488.2 definite length block | 记录数组，每条记录固定 8 个字段 |
| `SYSTem:T2:DATA?` | block | 输出 `seq,node,channel,t2_tick,status,error_code,temperature` |
| `SYSTem:TRACe:DATA?` | block | trace 分页输出，包含页号、总页数、run_id、CRC |
| `SYSTem:SNAPshot:DATA?` | block | 配置、运行、故障快照，带 layout_version 和 CRC |

示例：

```text
#42048<2048 bytes payload>

LOCKED,FIELD_SYNC_20260811,3A91C027,A0>A1>A2>A3>A0,A0,126400000,126398720,1280,4294967296,42,18,0,73,0,0
```

所有分页数据必须包含：

```text
run_id,
page_index,
page_count,
crc
```

## 15. 附录

### 15.1 产品化边界

| 边界 | 规则 |
|---|---|
| 校准 | NODE/SMA 基础链路校准提供固定 delay；DEVICE/T2 校准应在 DC `LOCKED` 后执行，形成预测分发用动作补偿 |
| 同步 | 本地晶振先提供 `local_tick`，BiSS-C/RJ45 提供跨节点观测，同步 DPLL 收敛出稳态虚拟 DC；NODE 环路方向必须与 `node_order` 完全一致 |
| DPLL | `SYSTem:SYNC:VDC:DPLL:*` 是实现虚拟 DC 的 offset/rate 收敛环路调试入口，不替代物理晶振；预测分发使用稳态 DC + T2/动作补偿，角度预测 DPLL 属于触发/扫描域 |
| HOLDOVER | 首版为保守策略：只允许已装载队列完成，不接收新增预约 |
| 测试上位机 | RUN 前配置转台和 DTC，RUN 中只从网分取数据，RUN 后读取 `SYSTem:RUN:*` 摘要和故障证据 |
| 调试上位机 | 面向当前系统任意状态的验证和控制；通过 `TEST/SERVICE/DEBUG/FACTORY` 四级权限 profile 控制可见按钮、可执行命令和状态策略，其中 `DEBUG` 高于现场 `SERVICE`。全部动作必须通过 core0 控制面、权限表、资源仲裁和 ACK 闭环，不直接影响 core1 已装载边沿 |
| 反射内存 | 用于多节点共同事实和摘要，不承载精确触发边沿 |
| A4 调试节点 | 只用于早期软件调试和 HIL 回环，模拟网分与转台；产品运行不依赖 A4 |

### 15.2 保留项

| 项目 | 说明 |
|---|---|
| 模型节点 | 分布式向量表按 8 节点预留，后续可加入模拟网分、模拟转台、模型 DUT 或产测代理 |
| 外部位置源 | 外部 BiSS-C/SSI/RS422 编码器仅作为调试期通讯和位置源预留；当前产品同步不需要外部 BiSS-C 编码器 |
| 高级同步 | 外部 TDC、硬件参考时钟、光隔离同步链路可作为后续增强 |
| 报告系统 | run_id、配置 CRC、校准 CRC、同步 CRC、T2/e_act/e_vdc/e_pll 统计需要进入批次报告 |

### 15.3 修订记录

| 版本 | 日期 | 说明 |
|---|---|---|
| `0.11` | 2026-08-11 | 根据测试/调试上位机边界复审业务配置、序列状态和运行控制：角度触发脉冲归入角度域，明确转台在目标角度点输出脉冲、A0 每个角度完整执行 active sequence；测试上位机 RUN 中只从网分取数据，调试上位机面向当前系统任意状态验证和控制，并通过 `TEST/SERVICE/DEBUG/FACTORY` 四级权限 profile、状态策略表、资源仲裁和 ACK 闭环执行、排队或拒绝；新增专门调试功能页；RUN 后摘要归入 `SYSTem:RUN:*` 日志/复盘接口。 |
| `0.10` | 2026-08-11 | 按产品化页面阅读方式重新分页：系统 2 页、校准 4 页、同步 4 页、维护 2 页；系统域补齐模式表、资源表、故障表、反射内存、配置 ACK、core/protection 和兼容状态查询；DPLL 调试范围移入同步状态与门禁页。 |
| `0.9` | 2026-08-11 | 结合 HAOFV 和 RTOS 分区架构复审 CAL/SYNC：补分布式 ACK 闭环、节点新鲜度门禁、HOLDOVER/RELOCK 保守策略、flash/storage 持久化门禁、epoch/run_id 追溯和表驱动 owner/seq/CRC/stale 字段。 |
| `0.8` | 2026-08-11 | 根据独立评审细化指令表：明确 A4 调试节点边界、同步响应字段、统一 IDLE 状态、将门禁分散到序列/CAL/SYNC/TRIG 业务端、加入 core/vector/protection 诊断查询，并澄清外部 BiSS-C 仅为调试保留 |
| `0.7` | 2026-08-11 | 根据对话决策完整回顾细化校准和同步：补拒绝原因、标准链路清单、校准门禁、同步故障恢复、拓扑检查字段、DPLL 调试范围和虚拟 DC 收敛顺序 |
| `0.6` | 2026-08-11 | 按最初版页面架构扩展为校准三页和同步三页：校准指令、校准参数与流程、校准版本与质量、同步指令、同步参数与流程、同步版本与质量 |
| `0.5` | 2026-08-11 | 按 `RP1200波导天线测试系统分布式触发方案SCPI指令表最初版.html` 复核格式和初始定义；回补设备信息、IEEE 488.2 完整基础指令、系统摘要查询、`SYSTem:REFMEM:*`、`READ:SEQuence:MAP?`，并将序列主模型恢复为自动展开状态表 + state_id 顺序引用 |
| `0.4` | 2026-08-11 | 形成 Markdown 源文档；补齐业务配置、运行控制、校准短事务、校准追溯、SYNC staging/active、NODE 拓扑强校验、版本质量和 DPLL 调试边界 |
| `0.3` | 2026-08-11 | 补齐校准和同步版本管理、质量判据、运行态保护、SYNC 检查字段、HOLDOVER 保守策略和 DPLL 调试边界 |

## 17. 最初版复核记录

复核基线：

- `docs/legacy/rp1200/RP1200波导天线测试系统分布式触发方案SCPI指令表最初版.html`

已继承的格式和定义：

- 页面结构保留“设备信息、系统指令、业务配置、序列状态定义、运行控制、校准、同步、维护附录”的逻辑分组。
- 设备信息保留 `GTS,DTC100,<uid>,0.1.0` 的 `*IDN?` 口径。
- 通信约定保留 USBTMC/USB488 成品接口、CDC validation 维护接口、调试权限 profile 和状态策略约束。
- IEEE 488.2 指令补齐 `*OPC`、`*STB?`、`*ESR?`、`*ESE/*ESE?`、`*SRE/*SRE?`。
- 系统指令补齐 `SYSTem:FW:BUILD?`、`SYSTem:LOG:STATus?`、`SYSTem:TRACe:LAST?`、`SYSTem:SNAPshot:LAST?`。
- 反射内存命令收敛到 `SYSTem:REFMEM:STATus?`、`SYSTem:REFMEM:NODE?` 命名，旧 `SYSTem:REFM:*` 不再保留。
- 序列主模型恢复为 `CONFigure:TRIGger` 自动展开状态表，`CONFigure:SEQuence` 上传 state_id 顺序引用。
- 增加并保留 `READ:SEQuence:MAP?`，用于显示 state_id 到 SWITCH1/SWITCH2/pol/freq/wave 的映射。
- 角度断点命名恢复为完整 `CONFigure:ANGLe:BREAkpoint`、`READ:ANGLe:BREAkpoint?`。

在最初版基础上继续保留的产品化增强：

- 校准域从早期抽象 `CONFigure:CAL <domain,node,target,mode,value...>` 拆成链路表、delay 表、结果读取、追溯信息、版本和质量管理。
- `CALibration:STARt` 明确为快速短事务，只计算线缆/链路固定 delay，失败不覆盖旧 staging 数据。
- 同步域从早期抽象 `CONFigure:SYNC <mode,target,param...>` 拆成 CAL/RING/DPLL/GATE、CHECK/START/STOP/RELOCK/HOLDOVER、版本和质量管理。
- 同步明确 staging/active 两级配置，`SYNC:STARt` 只使用 active。
- RJ45_SYNC_RING 拓扑强校验要求 `node_order` 每一跳匹配同方向 active NODE 校准链路。
- DPLL 调试接口保留在 `SYSTem:SYNC:VDC:DPLL:*` 维护域，正式 TRIG RUN 禁止修改。
