# RP1200波导天线测试系统分布式触发方案SCPI指令表

版本：0.5  
日期：2026-08-11  
产品：DTC100 Distributed Trigger  
接口：USBTMC / USB488  
文档状态：Markdown 源文档，后续 HTML/PDF 由本文同步更新

## 1. 文档约定

本文用于上位机前期开发、联调和后续产品化协议冻结。后续维护顺序固定为：

```text
先更新 Markdown -> 评审确认 -> 同步更新 HTML -> 按需导出 PDF
```

本文以 `docs/RP1200波导天线测试系统分布式触发方案SCPI指令表最初版.html` 作为格式和初始定义基线；后续增强只在该基线之上扩展，不破坏初版已经明确的业务语义。

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

### 1.3 设备信息

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
| `SYSTem:FW:BUILD?` |  | `build id` | 查询当前烧录固件构建标识 |
| `SYSTem:ERRor?` |  | `<code>,<text>` | 读取并弹出一条 SCPI 错误 |
| `SYSTem:ERRor:COUNt?` |  | `<count>` | 查询错误队列数量 |
| `SYSTem:RTOS:STATus?` |  | `task table` | 读取 heap、任务栈水位、任务状态和核心心跳 |
| `SYSTem:CORE?` |  | `core block` | 读取 core0/core1 心跳、循环计数和状态机摘要 |
| `SYSTem:LOG:LEVel` | `0..3` | `1` | 设置日志等级：0=DEBUG，1=INFO，2=WARN，3=ERROR |
| `SYSTem:LOG:LEVel?` |  | 等级值 | 查询日志等级 |
| `SYSTem:LOG:STATus?` |  | 日志统计 | 查询日志计数、队列、水位、丢弃和输出失败统计 |
| `SYSTem:TRACe:LAST?` |  | trace 摘要 | 查询最近 trace 摘要 |
| `SYSTem:SNAPshot:LAST?` |  | snapshot 摘要 | 查询最近一次配置、运行或故障快照 |
| `SYSTem:LOG:PAGE?` | `[page_id]` | `log page block` | 按页读取日志，用于上位机报告和故障追溯 |
| `SYSTem:TRACe:DATA?` | `[kind,page]` | `trace block` | 读取运行 trace、同步 trace 或故障 trace |
| `SYSTem:SNAPshot:DATA?` | `[kind]` | `snapshot block` | 读取配置、运行或故障快照 |
| `SYSTem:T2:DATA?` | `[count]` | `T2 block` | 读取 T2、e_act、late、CRC、seq 分布数据 |

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

## 3. 业务配置指令

### 3.1 触发参数与扫描角度

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CONFigure:TRIGger` | `<chan_cnt>,<pol>,<freq_cnt>,<wave_cnt>` | `1` | 配置角度点内序列展开参数；展开顺序为通道 -> 极化 -> 频点 -> 波位 |
| `READ:TRIGger:PARameter?` |  | `param block` | 读取通道数、极化、频点数、波位数、展开状态数和 CRC |
| `CONFigure:ANGLe:SWEEP` | `<start_deg>,<stop_deg>,<step_deg>` | `1` | 配置扫描角度范围和步长，用于输出脉冲与转台位置对应 |
| `READ:ANGLe:SWEEP?` |  | `angle block` | 读取扫描起始角、终止角、步长、角度点数和当前角度索引 |
| `READ:ANGLe:POSition?` |  | `position block` | 读取当前转台角度、角度时间戳、有效标志和位置源 |
| `CONFigure:ANGLe:BPOint` | `[angle_deg]` | `1` | 配置角度断点；给出参数时绑定指定扫描角度，运行中或暂停中省略参数时使用当前待输出角度游标 |
| `CONFigure:ANGLe:BPOint:CLEAr` |  | `1` | 清除角度断点和命中标志；若当前已因断点暂停，仅清除断点标志，不自动继续运行 |
| `READ:ANGLe:BPOint?` |  | `breakpoint block` | 读取断点角度、命中状态、暂停状态和断点游标 |

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

### 3.2 序列配置

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CONFigure:SEQuence` | `<plan_id>,<state_id_1>,<state_id_2>,...` | `1` | 一次性写入完整状态序列；`state_id` 必须引用当前 `CONFigure:TRIGger` 自动展开生成的状态表 |
| `READ:SEQuence?` | `[plan_id]` | `sequence block` | 读取当前或指定序列；返回顺序引用、记录数、CRC 和展开后的状态条目 |
| `READ:SEQuence:MAP?` | `[plan_id]` | `map block` | 读取状态映射表；返回 `state_id` 与 `switch1_ch`、`switch2_sel`、`pol`、`freq_idx`、`wave_idx` 的对应关系 |
| `CONFigure:SEQuence:AUTO` | `<plan_id>` | `1` | 按 `CONFigure:TRIGger` 自动生成默认序列 |
| `READ:SEQuence:CHECK?` | `[plan_id]` | `check block` | 维护接口：检查序列长度、状态范围、SWITCH1/SWITCH2、极化、频点、波位和 CRC |

序列采用“自动展开 + 顺序引用”模式。`CONFigure:TRIGger` 根据通道、极化、频点和波位自动生成状态表，展开顺序固定为：

```text
switch1_ch -> pol -> freq_idx -> wave_idx
```

`CONFigure:SEQuence` 只上传引用顺序，上位机无需逐字段描述每个状态。`state_id` 不是黑盒编号，必须能通过 `READ:SEQuence:MAP?` 回读到 SWITCH1/SWITCH2/pol/freq/wave 映射。

状态映射字段：

```text
state_id,
switch1_ch,
switch2_sel,
pol,
freq_idx,
wave_idx
```

### 3.3 开关独立控制

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CONFigure:SWITch#` | `<value>` | `1` | 独立切换开关；`#=1` 为 SWITCH1/SP8T，取 1..8；`#=2` 为 SWITCH2/SP2T，取 0/1 |
| `READ:SWITch#?` |  | `switch block` | 读取指定开关的目标值、实际值、busy、错误码和最近切换时间 |

运行中若开关已被序列引擎占用，独立切换命令返回 busy。

## 4. 运行控制指令

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `TRIGger:MODE` | `0|1|2|3|4` | `1` | 设置模式：0=IDLE，1=TRIG，2=CAL，3=SYNC，4=SIM |
| `TRIGger:MODE?` |  | `mode block` | 读取当前模式、运行态、允许动作和拒绝原因 |
| `TRIGger:STARt` | `[plan_id]` | `1` | 启动当前测试计划；必须通过配置门禁、同步门禁和序列校验 |
| `TRIGger:STOP` |  | `1` | 停止运行，清空未执行预约队列，保留配置、断点和日志 |
| `TRIGger:PAUSe` |  | `1` | 在下一个安全边界暂停；不截断正在执行的角度点内部动作 |
| `TRIGger:CONTinue` |  | `1` | 从暂停角度继续；若因角度断点暂停，对当前断点做一次性越过 |
| `READ:TRIGger:STATe?` |  | `state block` | 读取 `mode,run_state,current_angle,angle_index,seq_index,late_count,error_code,breakpoint_state` |
| `READ:TRIGger:PARameter?` |  | `param block` | 读取触发参数、序列 CRC、角度配置 CRC、同步门禁和校准 CRC |

典型流程：

```scpi
CONFigure:ANGLe:SWEEP -10,370,1
CONFigure:ANGLe:BPOint 0
CONFigure:TRIGger 8,2,5,1
CONFigure:SEQuence:AUTO PLAN_A
READ:SEQuence?
SYNC:CHECk
TRIGger:MODE 1
TRIGger:STARt PLAN_A
READ:TRIGger:STATe?
TRIGger:STOP
```

上位机不逐点驱动 RUN 态。启动后由 A0 扫描编排和各节点 PIO 本地执行完成角度点循环，USB/SCPI 抖动不能进入已装载边沿。

## 5. 状态与数据读取

### 5.1 业务读取

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `READ:RUN:SUMMary?` |  | `summary block` | 读取 run_id、计划 ID、角度进度、有效样本、失败样本和门禁摘要 |
| `READ:T2:COUNt?` |  | `<count>` | 读取 T2 FIFO 当前数据量 |
| `READ:T2:DATA?` | `[count]` | `T2 block` | 读取 T2 数据 |
| `READ:STATistics?` | `[kind]` | `statistics block` | 读取 `e_vdc/e_act/e_pll/late/crc/seq` 统计摘要 |

`READ:T2:DATA?` 字段：

```text
seq,node,channel,t2_tick,status,error_code,temperature
```

### 5.2 配置门禁与反射内存

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `SYSTem:CONFigure:STATus?` |  | `gate block` | 读取配置门禁、ACK/NACK、busy、timeout 和 CRC 快照 |
| `SYSTem:CONFigure:ACK?` |  | `ack block` | 读取分布式配置 ACK 位图、NACK 位图和最近拒绝原因 |
| `SYSTem:CONFigure:NACK?` | `[reason_id]` | `reason block` | 读取 NACK reason 表，用于 UI 参数校验和故障提示 |
| `SYSTem:REFM:STAT?` |  | `refmem block` | 读取 64KB 分布式向量表表头、版本、table_seq、节点心跳和 stale 状态 |
| `SYSTem:REFM:NODE?` | `[node_id]` | `node block` | 读取指定节点镜像；省略时读取本节点 |
| `SYSTem:CONFigure:ROLE?` | `[node_id]` | `role block` | 查询 NodeRoleMap |
| `SYSTem:CONFigure:LOOP?` | `[layer_id]` | `loop block` | 查询 LoopPlan 层级和数组循环编排 |
| `SYSTem:CONFigure:ACT?` | `[action_id]` | `action block` | 查询 SP8T、SP2T、READY 等动作映射 |
| `SYSTem:CONFigure:CAL?` | `[node_id]` | `cal block` | 查询链路、端口和设备动作补偿摘要 |
| `SYSTem:SCPI:RUN:ALLOW?` | `[index]` | `policy block` | 查询运行态 SCPI 白名单策略 |

反射内存用于维护多节点共同事实和摘要，不承载精确触发边沿，也不传大文件、波形、OTA payload 或 SD 内容。

## 6. 校准指令

### 6.1 链路表维护

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CONFigure:CALibration:LINK:ADD` | `<type>,<src_node>,<src_port>,<dst_node>,<dst_port>,<direction>,<enable>,<required>` | `1` | 新增可校准链路；链路 key 已存在时返回 duplicate |
| `CONFigure:CALibration:LINK:SET` | 同 ADD | `1` | 修改链路属性；不修改已经保存的 delay 数据 |
| `CONFigure:CALibration:LINK:DELete` | `<type>,<src_node>,<src_port>,<dst_node>,<dst_port>[,DEL]` | `1` | 删除链路；带 `DEL` 时同时删除该链路校准数据 |
| `READ:CALibration:LINK?` | `[type,src_node,src_port,dst_node,dst_port]` | `link table` | 读取链路清单、方向、使能、必需标志和当前 delay 是否有效 |

### 6.2 校准动作

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CALibration:STARt` | `<type>,<src_node>,<src_port>,<dst_node>,<dst_port>` | `result block` | 快速测量指定输入/输出段，例如 `CALibration:STARt SMA,A0,OUT1,A1,IN1` |
| `READ:CALibration:STATe?` | `[type,src_node,src_port,dst_node,dst_port]` | `state block` | 读取最近一次或指定链路校准状态；快速事务通常返回 DONE/FAIL |
| `READ:CALibration:RESult?` | `[type,src_node,src_port,dst_node,dst_port]` | `result block` | 读取最近一次校准结果、delay、jitter、样本数和失败原因 |

### 6.3 执行约束

| 约束 | 规则 | 上位机处理 |
|---|---|---|
| 运行状态 | 只允许在 `IDLE`、`STOPPED` 或 `CAL` 模式执行 | 运行中拒绝时先 `TRIGger:STOP`，再重新校准 |
| 链路存在 | `CALibration:STARt` 的端口对必须已在 LINK 表中登记 | 缺失时先执行 `CONFigure:CALibration:LINK:ADD` |
| 失败保护 | 失败不覆盖旧 staging delay，只记录失败原因和原始计数 | 读取 `READ:CALibration:RESult?` 后决定是否重测 |
| 同步影响 | 校准写入 staging；只有 `CALibration:ACTivate` 后才影响 SYNC | 激活后重新执行 `SYNC:CHECk` |

### 6.4 拒绝原因

| `reject_reason` | 含义 | 处理 |
|---|---|---|
| `RUNNING` | 系统处于 TRIG RUN 或正在输出预约边沿 | 停止触发后重试 |
| `LINK_NOT_FOUND` | 指定端口对未登记到 LINK 表 | 先新增或修正 LINK |
| `PORT_INVALID` | 节点、端口名或对象类型非法 | 按校准参数页检查端口枚举 |
| `CAL_BUSY` | 已有校准短事务正在执行 | 稍后重试 |
| `SIGNAL_TIMEOUT` | 指定输入端未捕获到回传边沿 | 检查线缆、方向和端口映射 |
| `QUALITY_FAIL` | 抖动、样本数或 delay 超出门限 | 读取结果并决定重测或手动写入 |

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
| `NODE` | `NODE,A0,RJ45,A1,RJ45,delay_ns` | RJ45 触发回传链路 delay；用于同步环 `A0->A1->A2->A3->A0` |
| `DEVICE` | `DEVICE,A1,SP8T,READY,delay_ns` | 稳态 DC 锁定后测得的设备动作/T2 delay，例如 SP8T、SP2T、VNA TRIG/READY |

### 7.2 标准链路清单

| 类型 | 标准链路 | 说明 |
|---|---|---|
| `NODE` | `A0,RJ45 -> A1,RJ45 -> A2,RJ45 -> A3,RJ45 -> A0,RJ45` | RJ45_SYNC_RING 完整环路，内部包含 4 个 required hop |
| `SMA` | `<src_node>,OUT# -> <dst_node>,IN#` | SMA 触发链路按现场接线任意配置，不限定 A0 作为输出源 |
| `DEVICE` | `A1,SP8T,READY` / `A2,SP2T,READY` / `A3,VNA,READY` | 在虚拟 DC `LOCKED` 后标定设备 READY/T2，形成预测分发使用的动作补偿 |

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
| `link table` | `type,src_node,src_port,dst_node,dst_port,direction,enable,required,has_delay` | 链路定义表，用于软件维护拓扑和校准入口 |
| `delay table` | `type,src_node,src_port,dst_node,dst_port,delay_ns,jitter_ns,count,crc,timestamp,valid` | 校准 delay 数据表，供 SYNC 和触发门禁引用 |
| `result block` | `state,type,src,dst,delay_ns,jitter_ns,count,error_code,reject_reason` | 最近一次测量结果；失败时 `delay_ns` 无效 |

## 8. 校准版本与质量

### 8.1 保存、激活和回滚

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CALibration:SAVE` | `<cal_id>[,scope]` | `1` | 保存 staging 校准表并生成 CRC；scope=ALL/SMA/NODE/DEVICE |
| `CALibration:LOAD` | `<cal_id>` | `1` | 把指定校准表载入 staging，不立即影响 active 表 |
| `CALibration:ACTivate` | `<cal_id>` | `1` | 激活指定校准表；只允许在 IDLE 或 STOPPED 状态执行 |
| `CALibration:ROLLback` |  | `1` | 恢复到上一次 active 校准表 |
| `READ:CALibration:LIST?` |  | `cal list` | 读取已保存校准表的 `cal_id,crc,timestamp,scope,valid` |
| `READ:CALibration:ACTive?` |  | `active block` | 读取 active / staging 校准表标识、CRC、保存状态和修改标志 |

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
| SAVE | 只持久化 staging，不自动切换 active | 上位机必须显式 `CALibration:ACTivate` |
| ACTivate | 切换 active 后清除最近一次 SYNC 检查结论 | 上位机必须重新 `SYNC:CHECk` |
| ROLLback | 恢复上一次 active 校准表 | 同样要求重新同步检查 |
| health fail | required 链路缺失、过期或超 jitter | 拒绝 `SYNC:STARt` 或拒绝 TRIG RUN |

```scpi
CONFigure:CALibration:META FIELD_20260811,OP01,FIX_A,CABLE_SET_A,28.5,FIELD_CHECK
CALibration:SAVE FIELD_20260811
CALibration:ACTivate FIELD_20260811
READ:CALibration:HEALth? NODE
```

## 9. 同步指令

### 9.1 同步边界

同步阶段的目标是让 DPLL 在 RJ45_SYNC_RING 上建立稳态虚拟 DC 时钟。active 校准表中的 NODE 链路 delay 只用于固定 hop 补偿和拓扑门禁；`SYNC:STARt` 不测 T2。只有虚拟 DC 进入 `LOCKED` 后，DEVICE/T2 校准才具备统一时间基准，后续预测分发使用稳态 DC + T2/动作补偿执行。

### 9.2 动作指令

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `SYNC:CHECk` | `[ACTive|STAGing]` | `check block` | 校验校准表、NODE 链路、CRC、时效、方向和拓扑；省略为 ACTive |
| `SYNC:STARt` |  | `1` | 启动同步环、虚拟 DC 维护和运行门禁服务 |
| `SYNC:STOP` |  | `1` | 停止同步服务，退出锁定态 |
| `SYNC:RELock` |  | `1` | 清除当前 offset/rate 估计并重新锁定；不清除校准表和同步配置 |
| `SYNC:HOLDover` | `0|1` | `1` | 进入或退出保持态；保持态只允许已装载队列完成 |

### 9.3 读取指令

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `READ:SYNC:STATe?` |  | `state block` | 读取同步状态、锁定态、origin、seq、holdover 和 fault |
| `READ:SYNC:PARameter?` |  | `param block` | 读取绑定校准表、环路、DPLL 和门禁参数 |
| `READ:SYNC:HEALth?` |  | `health block` | 读取 CRC、seq、drop、relock、e_vdc、late 和链路时效统计 |
| `READ:SYNC:NODE?` | `[node]` | `node block` | 读取单节点 offset、rate、last_seq、age 和链路健康度 |
| `READ:SYNC:LINK?` | `[src_node,dst_node]` | `link table` | 读取 NODE delay、hop 方向、required/enabled 状态和校验结果 |
| `READ:SYNC:CHECk?` |  | `check block` | 读取最近一次同步检查结果和拒绝原因 |

### 9.4 同步状态机

| 状态 | 含义 | 允许动作 |
|---|---|---|
| `IDLE` | 未启动同步或已停止 | 允许配置、加载、检查和启动 |
| `CHECKED` | active 配置和拓扑检查通过 | 允许 `SYNC:STARt` |
| `LOCKING` | 正在估计 offset/rate | 允许读取状态、健康度和维护 DPLL 调试 |
| `LOCKED` | 虚拟 DC 已锁定，满足运行门禁 | 允许 TRIG RUN |
| `HOLDOVER` | 短时失去同步帧，按保持策略运行 | 不允许新增预约，只允许已装载队列完成 |
| `FAULT` | CRC、seq、拓扑或门禁故障锁存 | 读取证据后停止并清故障 |

### 9.5 故障与恢复

| 场景 | 状态变化 | 恢复动作 |
|---|---|---|
| NODE 链路缺失或方向错误 | `SYNC:CHECk` 返回失败，不进入 `CHECKED` | 读取 `READ:SYNC:LINK?`，修正校准表或 ring 顺序 |
| CRC / seq 连续超限 | `LOCKED -> FAULT` | 停止触发，读取 `READ:SYNC:HEALth?` 和故障证据 |
| 短时丢帧 | `LOCKED -> HOLDOVER` | 等待自动重锁或执行 `SYNC:RELock` |
| e_vdc 超门限 | `LOCKED -> LOCKING/FAULT` | 读取质量数据，必要时调整 profile 或排查链路 |
| 故障锁存 | `FAULT` | 读取日志、trace、snapshot 后执行 `SYSTem:FAULT:CLEAr` |

## 10. 同步参数与流程

### 10.1 同步配置参数

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CONFigure:SYNC:CALibration` | `<cal_id>,<cal_crc>,<max_age_s>` | `1` | 绑定同步使用的校准表到 staging 配置 |
| `CONFigure:SYNC:RING` | `<origin>,<node_order>,<period_us>,<bitrate>,<timeout_ms>,<crc_limit>` | `1` | 配置 RJ45_SYNC_RING；`node_order` 每一跳必须匹配同方向 NODE 校准链路 |
| `CONFigure:SYNC:DPLL` | `<lock_window_ns>,<lock_count>,<holdover_ms>,<relock_ms>,<profile>` | `1` | 配置虚拟 DC 时钟同步 DPLL 的锁定、保持和重锁判据 |
| `CONFigure:SYNC:GATE` | `<required_lock>,<max_age_ms>,<max_evdc_p99_ns>,<allow_holdover>` | `1` | 配置触发运行门禁 |

### 10.2 同步字段

| 字段组 | 字段 | 说明 |
|---|---|---|
| `param` | `cal_id, cal_crc`<br>`max_age_s, origin`<br>`node_order, period_us`<br>`bitrate, lock_window_ns`<br>`holdover_ms, dpll_profile` | 同步使用的校准表、环路配置、DPLL profile 和锁定判据 |
| `health` | `crc_count, seq_error`<br>`drop_count, relock_count`<br>`evdc_p99_ns, evdc_p999_ns`<br>`late_count` | 同步环健康度、虚拟 DC 残差和运行门禁依据 |
| `node` | `node, offset_tick`<br>`rate_q32, last_seq`<br>`age_ms, link_delay_ns` | 单节点虚拟 DC 估计结果和校准链路 delay 使用值 |
| `check` | `check_state, cal_id`<br>`cal_crc, node_order`<br>`missing_link, expired_link`<br>`direction_mismatch, reject_reason` | 启动前定位缺失链路、过期校准表、方向错误和门禁拒绝原因 |

### 10.3 拓扑检查字段

| 字段 | 含义 | 规则 |
|---|---|---|
| `node_order` | 同步环顺序 | 首版固定 `A0>A1>A2>A3>A0` |
| `required_link_count` | 必需 NODE 链路数量 | 四板环路应为 4 |
| `valid_link_count` | active 校准表中有效同向链路数量 | 必须等于 required_link_count |
| `missing_link` | 缺失链路列表 | 为空才允许启动 |
| `direction_mismatch` | 方向错误列表 | A0->A1 与 A1->A0 不能混用 |
| `expired_link` | 超过 max_age_s 的链路 | required 链路过期时拒绝启动 |

### 10.4 流程门禁

| 步骤 | 门禁条件 | 失败读取 |
|---|---|---|
| 配置检查 | cal_id 存在、CRC 匹配、未过期、NODE 链路方向匹配 | `READ:SYNC:LINK?` |
| 启动同步 | 最近一次 active `SYNC:CHECk` 通过 | `READ:SYNC:CHECk?` |
| 进入 LOCKED | 连续 `lock_count` 次 e_vdc 落入 `lock_window_ns` | `READ:SYNC:HEALth?` |
| 触发运行 | required_lock=1 时必须 LOCKED，且 e_vdc、age、CRC 低于门限 | `READ:SYNC:STATe?` |

```scpi
CONFigure:SYNC:CALibration FIELD_20260811,3A91C027,86400
CONFigure:SYNC:RING A0,A0>A1>A2>A3>A0,1000,12500000,20,0
CONFigure:SYNC:DPLL 300,100,200,1000,LOW_JITTER
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
| `SYNC:SAVE` | `<sync_id>[,scope]` | `1` | 保存 staging 同步配置并生成 CRC；scope=ALL/CAL/RING/DPLL/GATE/LIMIT |
| `SYNC:LOAD` | `<sync_id>` | `1` | 载入同步配置包到 staging，不立即影响 active |
| `SYNC:ACTivate` | `<sync_id>` | `1` | 激活同步配置包；只允许在 IDLE 或 STOPPED 状态执行 |
| `SYNC:ROLLback` |  | `1` | 恢复到上一次 active 同步配置包 |
| `READ:SYNC:LIST?` |  | `sync list` | 读取已保存配置包的 id、CRC、时间戳、scope 和 valid 标志 |
| `READ:SYNC:ACTive?` |  | `active block` | 读取 active/staging 配置、绑定校准表、CRC、修改标志和最近检查状态 |

### 11.2 质量判据和 DPLL

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `CONFigure:SYNC:LIMit` | `<profile>[,<key=value>[,...]]` | `1` | 选择同步质量门限档位；调试时可追加字段覆盖，未写字段保持 profile 展开值 |
| `READ:SYNC:QUALity?` | `[sync_id]` | `quality block` | 读取质量结论、e_vdc 分布、错误计数、重锁计数、链路年龄和门禁拒绝原因 |
| `READ:SYNC:VERSion?` |  | `version block` | 读取同步配置版本、绑定校准版本、固件版本、硬件 profile 和最近激活时间 |
| `SYSTem:SYNC:DPLL:TUNE` | `<bandwidth_hz>,<damping>,<max_slew_ppm>` | `1` | 按等效传递函数参数覆盖虚拟环路滤波器，仅用于调试 |
| `SYSTem:SYNC:DPLL:COEFficient` | `<kp_q31>,<ki_q31>,<max_slew_ppm>` | `1` | 直接覆盖离散 PI 环路系数 |
| `SYSTem:SYNC:DPLL:OVERRide?` |  | `override block` | 读取调试覆盖是否生效、来源、允许状态、最近写入时间和清除原因 |
| `SYSTem:SYNC:DPLL:COEFficient?` |  | `coef block` | 读取当前环路滤波器系数、来源、限幅和生效状态 |
| `SYSTem:SYNC:DPLL:DEFAult` |  | `1` | 清除调试覆盖，恢复内置 profile |

### 11.3 质量字段

| 字段 | 来源 | 说明 |
|---|---|---|
| `e_vdc_p99_ns` | 虚拟 DC 环路 | 同步残差 P99，用于判断多节点同步是否稳定 |
| `crc_count / seq_error` | RJ45_SYNC_RING | 同步帧通信质量；超过门限进入 FAULT 或拒绝 TRIG RUN |
| `relock_count` | DPLL 状态机 | 重锁次数过多说明链路噪声、丢帧或环路参数不合适 |
| `gate_state` | 运行门禁 | READY/BLOCKED；BLOCKED 时给出 reject_reason |

### 11.4 DPLL 调试范围

| 参数 | 范围 | 说明 |
|---|---|---|
| `profile` | `DEFAULT` / `STRICT` / `RELAXED` / `FAST_LOCK` / `LOW_JITTER` | 业务配置只选择固件内置 profile |
| `cal_limit key` | `max_delay_ns` / `max_jitter_ns` / `min_count` / `max_age_s` | 校准调试覆盖字段，可只写需要临时调整的字段 |
| `sync_limit key` | `max_crc` / `max_seq` / `max_drop` / `max_relock` / `max_evdc_p99_ns` / `max_evdc_p999_ns` / `min_lock_count` | 同步调试覆盖字段，可只写需要临时调整的字段 |
| `bandwidth_hz` | `0.01 .. 20` | 维护调试用等效环路带宽 |
| `damping` | `0.3 .. 2.0` | 维护调试用阻尼系数 |
| `max_slew_ppm` | `1 .. 200` | offset/rate 修正限幅 |
| `coef_source` | `DEFAULT` / `PROFILE` / `OVERRIDE` | 当前系数来源 |

校准质量门限和 DPLL 的调试覆盖均为易失态，不写入出厂默认 profile。同步质量门限统一由 `CONFigure:SYNC:LIMit` 管理；执行不带 `key=value` 的 profile 配置时清除同步门限临时覆盖。`READ:CALibration:HEALth?`、`READ:SYNC:PARameter?`、`READ:SYNC:QUALity?` 应返回 profile 展开值和 override 标志。

DPLL 是实现稳态 DC 时钟的基础环路：它持续估计各节点相对 DC 的 offset/rate，并输出 `LOCKING`、`LOCKED`、`HOLDOVER` 等同步状态。T2/DEVICE 校准应在 DC `LOCKED` 后执行，用统一时间基准测得动作补偿；预测分发再使用稳态 DC + T2/动作补偿生成本地预约。调试覆盖只允许在 `IDLE`、`STOPPED` 或 `LOCKING` 使用；正式 `TRIG RUN` 禁止修改。同步 DPLL 不等同于扫描角度预测 DPLL。

## 12. 维护指令

| 指令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `SYSTem:USB:MODE?` |  | `USBTMC` | 查询当前 USB 模式；成品固定为 USBTMC |
| `SYSTem:USB:MODE` | `CDC|USBTMC` | `1` | validation 固件维护命令：写入下次启动 USB 模式 |
| `SYSTem:USB:BOOT` |  | `1` | 重启并应用 USB 模式 |
| `SYSTem:OTA:STATus?` |  | `OTA block` | 查询升级流程状态 |
| `SYSTem:SD:STATus?` |  | `storage block` | 查询 SD、日志、snapshot 和报告写入状态 |

### 12.1 错误处理速查

| 场景 | 上位机处理 |
|---|---|
| 配置校验失败 | 读取 `SYSTem:CONFigure:NACK?` 和 `READ:SEQuence:CHECK?` |
| 运行 late 或 timeout | 停止后读取 `READ:TRIGger:STATe?`、`SYSTem:FAULT:LAST?` 和 trace/snapshot |
| 同步失锁 | 读取 `READ:SYNC:STATe?`、`READ:SYNC:HEALth?` 和 `READ:SYNC:LINK?` |
| 校准失败 | 读取 `READ:CALibration:STATe?` 和 `READ:CALibration:RESult?`；失败不覆盖旧 staging 数据 |
| 报告导出 | 按页读取 `SYSTem:LOG:PAGE?`、`SYSTem:TRACe:DATA?`、`SYSTem:SNAPshot:DATA?` 和 `SYSTem:T2:DATA?` |

## 13. 响应数据格式

### 13.1 文本 block

| block | 字段 | 用途 |
|---|---|---|
| `state block` | `state,sub_state,run_id,seq,progress,error_code,reject_reason` | 运行态、同步态、校准态通用状态读取 |
| `param block` | `id,crc,version,modified,field...` | 配置参数读取 |
| `check block` | `check_state,target_config,crc_ok,topology_ok,gate_ok,reject_reason` | 启动前门禁和离线预检 |
| `quality block` | `result,p99,p999,count,crc_error,seq_error,late_count,relock_count` | 同步和测试质量统计 |
| `fault block` | `fault_code,source_node,first_ts,last_ts,evidence_seq,sticky,recoverable,text` | 故障锁存证据 |

### 13.2 二进制 block

| 数据 | 格式 | 说明 |
|---|---|---|
| `CONFigure:SEQuence` | IEEE488.2 definite length block | 记录数组，每条记录固定 8 个字段 |
| `READ:T2:DATA?` | block | 输出 `seq,node,channel,t2_tick,status,error_code,temperature` |
| `SYSTem:TRACe:DATA?` | block | trace 分页输出，包含页号、总页数、run_id、CRC |
| `SYSTem:SNAPshot:DATA?` | block | 配置、运行、故障快照，带 layout_version 和 CRC |

示例：

```text
#42048<2048 bytes payload>

LOCKED,FIELD_SYNC_20260811,3A91C027,A0>A1>A2>A3>A0,0,0,42,18.6,73.2,0
```

所有分页数据必须包含：

```text
run_id,
page_index,
page_count,
crc
```

## 14. 产品化边界

| 边界 | 规则 |
|---|---|
| 校准 | NODE/SMA 基础链路校准提供固定 delay；DEVICE/T2 校准应在 DC `LOCKED` 后执行，形成预测分发用动作补偿 |
| 同步 | 同步用 DPLL 建立稳态虚拟 DC 时钟；NODE 环路方向必须与 `node_order` 完全一致 |
| DPLL | `SYNC:DPLL` 是实现稳态 DC 时钟的虚拟 offset/rate 环路；预测分发使用稳态 DC + T2/动作补偿，角度预测 DPLL 属于触发/扫描域 |
| HOLDOVER | 首版为保守策略：只允许已装载队列完成，不接收新增预约 |
| 上位机 | 上位机负责配置、启动、停止、读取状态和导出报告，不参与 RUN 态逐点实时决策 |
| 反射内存 | 用于多节点共同事实和摘要，不承载精确触发边沿 |

## 15. 保留项

| 项目 | 说明 |
|---|---|
| 模型节点 | 分布式向量表按 8 节点预留，后续可加入模拟网分、模拟转台、模型 DUT 或产测代理 |
| 外部位置源 | BiSS/SSI/RS422 位置源保留，不作为四板同步主环 |
| 高级同步 | 外部 TDC、硬件参考时钟、光隔离同步链路可作为后续增强 |
| 报告系统 | run_id、配置 CRC、校准 CRC、同步 CRC、T2/e_act/e_vdc/e_pll 统计需要进入批次报告 |

## 16. 修订记录

| 版本 | 日期 | 说明 |
|---|---|---|
| `0.7` | 2026-08-11 | 根据对话决策完整回顾细化校准和同步：补拒绝原因、标准链路清单、校准门禁、同步故障恢复、拓扑检查字段和 DPLL 调试范围 |
| `0.6` | 2026-08-11 | 按最初版页面架构扩展为校准三页和同步三页：校准指令、校准参数与流程、校准版本与质量、同步指令、同步参数与流程、同步版本与质量 |
| `0.5` | 2026-08-11 | 按 `RP1200波导天线测试系统分布式触发方案SCPI指令表最初版.html` 复核格式和初始定义；回补设备信息、IEEE 488.2 完整基础指令、系统摘要查询、`SYSTem:REFM:*`、`READ:SEQuence:MAP?`，并将序列主模型恢复为自动展开状态表 + state_id 顺序引用 |
| `0.4` | 2026-08-11 | 形成 Markdown 源文档；补齐业务配置、运行控制、校准短事务、校准追溯、SYNC staging/active、NODE 拓扑强校验、版本质量和 DPLL 调试边界 |
| `0.3` | 2026-08-11 | 补齐校准和同步版本管理、质量判据、运行态保护、SYNC 检查字段、HOLDOVER 保守策略和 DPLL 调试边界 |

## 17. 最初版复核记录

复核基线：

- `docs/RP1200波导天线测试系统分布式触发方案SCPI指令表最初版.html`

已继承的格式和定义：

- 页面结构保留“设备信息、系统指令、业务配置、序列状态定义、运行控制、校准、同步、维护附录”的逻辑分组。
- 设备信息保留 `GTS,DTC100,<uid>,0.1.0` 的 `*IDN?` 口径。
- 通信约定保留 USBTMC/USB488 成品接口、CDC validation 维护接口、RUN 态白名单约束。
- IEEE 488.2 指令补齐 `*OPC`、`*STB?`、`*ESR?`、`*ESE/*ESE?`、`*SRE/*SRE?`。
- 系统指令补齐 `SYSTem:FW:BUILD?`、`SYSTem:LOG:STATus?`、`SYSTem:TRACe:LAST?`、`SYSTem:SNAPshot:LAST?`。
- 反射内存命令恢复最初版 `SYSTem:REFM:STAT?`、`SYSTem:REFM:NODE?` 命名。
- 序列主模型恢复为 `CONFigure:TRIGger` 自动展开状态表，`CONFigure:SEQuence` 上传 state_id 顺序引用。
- 增加并保留 `READ:SEQuence:MAP?`，用于显示 state_id 到 SWITCH1/SWITCH2/pol/freq/wave 的映射。
- 角度断点命名恢复为完整 `CONFigure:ANGLe:BPOint`、`READ:ANGLe:BPOint?`。

在最初版基础上继续保留的产品化增强：

- 校准域从早期抽象 `CONFigure:CAL <domain,node,target,mode,value...>` 拆成链路表、delay 表、结果读取、追溯信息、版本和质量管理。
- `CALibration:STARt` 明确为快速短事务，只计算线缆/链路固定 delay，失败不覆盖旧 staging 数据。
- 同步域从早期抽象 `CONFigure:SYNC <mode,target,param...>` 拆成 CAL/RING/DPLL/GATE、CHECK/START/STOP/RELOCK/HOLDOVER、版本和质量管理。
- 同步明确 staging/active 两级配置，`SYNC:STARt` 只使用 active。
- RJ45_SYNC_RING 拓扑强校验要求 `node_order` 每一跳匹配同方向 active NODE 校准链路。
- DPLL 调试接口保留在 `SYSTem:SYNC:DPLL:*` 维护域，正式 TRIG RUN 禁止修改。
