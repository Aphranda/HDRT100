# SCPI 基础命令

Status: Active
Domain: SCPI
Canonical: `docs/SCPI_COMMANDS.md`
Related: `docs/SYNC_IO_RESOURCE_PLAN.md`, `docs/SYNC_IO_REFACTOR_PLAN.md`, `docs/OTA_SYSTEM_DESIGN.md`, `docs/SD_TODO.md`, `docs/SCPI_USB_INTERFACE_DESIGN.md`
Last updated: 2026-07-22

成品默认 SCPI 服务通过 USBTMC/USB488 接入。命令以 `\n` 或 `\r\n` 结束。Trigger 相关控制命令当前已经通过 `sync_trigger` 事件接口收口，SCPI 不再直接调用底层 `sync_io`。

固件使用 `PROJECT_ENABLE_USBTMC` 构建启用 USBTMC/USB488 + SCPI 专业仪表接口。USBTMC 模式复用同一套 SCPI 命令表，当前 USB 描述符按 `bus-powered` 申明；后续成品如确认自供电，再切换为 `self-powered`。USB 描述符、VISA 枚举和供电属性记录见 `docs/SCPI_USB_INTERFACE_DESIGN.md`。
开发/validation 阶段如需同一份固件在 CDC / USBTMC 间切换，可启用 `PROJECT_ENABLE_USB_RUNTIME_SWITCH`，并通过 `SYSTem:USB:MODE` 写入 Product Config；该切换不作为 DTC100 成品对外接口。这里 `SYST` 是 `SYSTem` 的短写。这个切换不是 OTA A/B 机制，A/B 只保留给升级镜像和回滚。业务层按 `CONFigure` / `READ` / `TRIGger` / `CALibration` / `SYNC` 分域，`SYSTem` 保留系统与基础状态。

## 标准命令

| 命令 | 说明 |
|---|---|
| `*IDN?` | 查询设备身份。 |
| `*RST` | 恢复 SCPI 当前基础配置，并停止采样和同步时钟。 |
| `*CLS` | 清除 SCPI 状态/错误。 |
| `*TST?` | 自检占位，当前返回 `0`。 |
| `SYSTem:ERRor?` | 查询下一条 SCPI 错误。 |
| `SYSTem:ERRor:COUN?` | 查询 SCPI 错误数量。 |
| `SYSTem:VERS?` | 查询 SCPI 标准版本。 |
| `SYSTem:FW:VERS?` | 查询固件语义版本，返回 `major,minor,patch`。 |
| `SYSTem:FW:BUILD?` | 查询固件 build id，由构建脚本生成 UTC 时间戳，每次构建刷新。 |
| `SYSTem:BOOT:VERS?` | 查询当前 App 声明的 Bootloader 兼容版本，返回 `major,minor,patch`。 |
| `SYSTem:BOOT:CAP?` | 查询当前 metadata 中记录的 Bootloader/OTA 能力位，`bit0=COPY_TO_ACTIVE`，`bit1=DIRECT_AB`。 |
| `SYSTem:USB:MODE?` | 查询当前 USB mode；DTC100 成品固定返回 `"USBTMC"`。 |
| `SYSTem:USB:MODE <CDC|USBTMC>` | validation 固件维护命令：写入下次启动的 USB mode；DTC100 成品不作为对外配置项。 |
| `SYSTem:USB:BOOT` | 立即重启，重启后按 Product Config 选择 USB mode。 |
| `SYSTem:LOG:LEVel <0..3>` | 设置文本日志最小输出等级：`0=DEBUG`、`1=INFO`、`2=WARN`、`3=ERROR`。默认 `INFO`。 |
| `SYSTem:LOG:LEVel?` | 查询当前文本日志最小输出等级，返回名称和值。 |
| `SYSTem:LOG:STATus?` | 查询文本日志统计：当前等级、等级值、DEBUG/INFO/WARN/ERROR 发出计数、过滤丢弃计数、截断计数、输出失败计数，以及队列丢弃数、当前队列字节数、队列高水位。 |
| `SYSTem:CORE?` | 查询核心运行状态：core1 是否启用、core0/core1 循环计数、core0/core1 最近一次心跳毫秒时间戳。 |
| `SYSTem:RTOS:STATus?` | 查询 FreeRTOS heap 和任务栈水位。 |
| `LOOP:STAT?` / `STATus:LOOP?` | 查询 `task_loop_engine` 的只读空壳状态：是否 ready、service_count、first_service_ms、last_service_ms。 |
| `VDC:STAT?` / `STATus:VDC?` | 查询 `task_vdc_sync` 的只读空壳状态：是否 ready、lock_state、service_count、first_service_ms、last_service_ms、sync_seq。 |
| `DPLL:STAT?` / `STATus:DPLL?` | 查询 `task_dpll` 的只读空壳状态：是否 ready、state、service_count、first_service_ms、last_service_ms、update_seq。 |
| `SYSTem:CONFigure:STAT?` / `STATus:CFG?` | 查询配置门禁状态：build id、ready、gate_state、service_count、epoch、run_id、版本号、ACK/NACK/busy/timeout 位和 CRC 快照（build/hw/role/loop/action/calibration/config）。 |
| `SYSTem:CONFigure:ROLE? [node_id]` | 查询静态 `NodeRoleMap` 条目；省略 `node_id` 时查询 0。返回 `version,node_count,target_mask,input_base_pin,output_base_pin,aux_base_pin,node_id,role,persona,feature_mask`。 |
| `SYSTem:CONFigure:LOOP? [layer_id]` | 查询静态 `LoopPlan` 层条目；省略 `layer_id` 时查询 0。返回 `version,node_loop_count,array_loop_count,layer_count,default_wait_rule,layer_id,node_id,action_id,wait_rule`。 |
| `SYSTem:CONFigure:ACT? [action_id]` | 查询静态 `ActionMap` 条目；省略 `action_id` 时查询 0。返回 `version,action_count,action_id,node_id,sma_out_pin,sma_in_pin,edge,delay_us`。 |
| `SYSTem:CONFigure:CAL? [node_id]` | 查询静态 `Calibration` 节点补偿；省略 `node_id` 时查询 0。返回 `version,node_count,node_id,delta_ns,sma_hop_ns,rj45_hop_ns,device_delay_ns,tempco_ppb,valid_window_ns`。 |
| `SYSTem:CONFigure:ACK?` | 查询本地分布式命令 ACK 快照：`version,command_seq,target_mask,ack_flags,nack_flags,busy_flags,timeout_flags,last_nack_reason,last_nack_node,reason_count,reason_table_crc32,config_crc32`。当前为本地门禁骨架，尚未接真实 RJ45 ACK delta。 |
| `SYSTem:CONFigure:NACK? [reason_id]` | 查询 NACK reason 表条目；省略 `reason_id` 时查询 0。返回 `version,reason_count,reason_id,severity,retryable,blocking,detail_code,name`。首版 reason 覆盖 `NONE`、配置 CRC、硬件 profile、节点 stale/fault 和 flash lockout 未就绪。 |
| `SYSTem:SCPI:RUN:ALLOW? [index]` | 查询 RUN 态 SCPI 白名单策略表；省略 `index` 时查询 0。返回 `version,entry_count,enforced,policy_crc32,index,class_id,run_allowed,query_allowed,write_allowed,forbidden_error_code,pattern`。关键触发/采样/时钟/BiSS/Storage/OTA 写入口已按表拒绝，禁止码 `2401=RUN_STATE_DENIED`、`2402=RESOURCE_BUSY`。 |
| `SYSTem:REFM:STAT?` | 查询本地 DistributedVectorTable P0 快照：`table_size,layout_version,table_seq,local_node_id,node_count,local_heartbeat,service_count,flags`。 |
| `SYSTem:REFM:NODE? [node_id]` | 查询 NodeSlot P0 快照；省略 `node_id` 时查询本节点，当前预留 `0..7` 共 8 个节点，支持真实板卡和模型节点。返回 `node_id,state,heartbeat,slot_version,last_update_ms,stale_count,fault_code,flags,node_type`。 |
| `SYSTem:CORE:VECT?` | 查询 `CoreVectorOwnerTable` 快照：`version,table_seq,core_count,core0_vtor_owner,core1_vtor_owner,core0_irq_owner_mask,core1_irq_owner_mask,entry_table_owner,flags,guard_owner,guard_crc,guard_stale,guard_flags`。 |
| `SYSTem:PROT:STAT?` | 查询 `RuntimeProtectionTable` 快照：`version,table_seq,ram_resident_required,flash_lockout_supported,flash_lockout_online,flash_lockout_requested,flash_lockout_acknowledged,park_state,entry_table_owner,flags,guard_owner,guard_crc,guard_stale,guard_flags`。 |
| `SYSTem:MODE:TAB? [mode_id]` | 查询 `SystemModeTable` 条目；省略 `mode_id` 时查询 0。返回 `version,mode_count,current_mode,table_crc32,mode_id,run_allowed,ota_allowed,fault_allowed,name`。 |
| `SYSTem:RESource:TAB? [resource_id]` | 查询 `ResourceArbiterTable` 条目；省略 `resource_id` 时查询 0。返回 `version,resource_count,current_mode,active_resources,last_conflict_resources,table_crc32,resource_id,mask,owner_mode,active,name,owner_name`。 |
| `SYSTem:FAULT:TAB? [fault_id]` | 查询 `FaultCodeTable` 条目；省略 `fault_id` 时查询 0。返回 `version,fault_count,latched,table_crc32,fault_id,domain_id,severity,recoverable,sticky,name`。 |

## 校准域

| 命令 | 说明 |
|---|---|
| `CONFigure:CAL <...>` | 配置校准参数、补偿表或校准目标。 |
| `READ:CAL?` | 查询校准状态、结果或补偿快照。 |

## 同步域

| 命令 | 说明 |
|---|---|
| `CONFigure:SYNC <...>` | 配置同步模式、锁定策略或环路参数。 |
| `READ:SYNC?` | 查询同步锁定状态、偏差或健康信息。 |

## 触发输出

| 命令 | 说明 |
|---|---|
| `TRIG:WIDT <us>` | 设置 `GPIO20/TRIG_OUT` 脉宽，单位 us。 |
| `TRIG:WIDT?` | 查询 `TRIG_OUT` 脉宽。 |
| `TRIG:IMM` | 立即输出一次 `TRIG_OUT` 脉冲。 |

## 第二路脉冲输出

| 命令 | 说明 |
|---|---|
| `PULSe:WIDT <us>` | 设置 `GPIO21/PULSE_OUT` 脉宽，单位 us。 |
| `PULSe:WIDT?` | 查询 `PULSE_OUT` 脉宽。 |
| `PULSe:IMM` | 立即输出一次 `PULSE_OUT` 脉冲。 |

## RJ45 触发输出

当前硬件定义以 `RJ45_TRIG_IN=GPIO19/IN3` 和 `RJ45_TRIG_OUT=GPIO23/OUT3`
为准，不再定义独立 `MARKER_OUT` 物理信号。新接口优先使用 `RJ45:TRIG:*`；
`MARKer:*` 是历史兼容命令，仍等价输出到 `RJ45_TRIG_OUT`。

| 命令 | 说明 |
|---|---|
| `RJ45:TRIG:WIDT <us>` | 设置 `GPIO23/RJ45_TRIG_OUT` 脉宽，单位 us。 |
| `RJ45:TRIG:WIDT?` | 查询 `RJ45_TRIG_OUT` 脉宽。 |
| `RJ45:TRIG:IMM` | 立即从 `GPIO23/RJ45_TRIG_OUT` 输出一次触发脉冲。 |
| `RJ45:TRIG:PINS?` | 查询 RJ45 触发硬件绑定，返回 `in_pin,out_pin`，当前为 `19,23`。 |
| `MARKer:WIDT <us>` | 设置兼容触发脉宽，单位 us；输出到 `GPIO23/RJ45_TRIG_OUT`。 |
| `MARKer:WIDT?` | 查询兼容触发脉宽。 |
| `MARKer:IMM` | 立即从 `GPIO23/RJ45_TRIG_OUT` 输出一次兼容触发脉冲。 |

固件快照和新配置结构使用 `rj45_trigger_width_us` 作为主字段；历史
`marker_width_us` 保留为同值镜像，仅用于旧 UI/脚本兼容。

## 采样配置

| 命令 | 说明 |
|---|---|
| `SAMPle:RATE <Hz>` | 设置输入采样率，并启动 `GPIO16..GPIO19` 采样。 |
| `SAMPle:RATE?` | 查询输入采样率。 |
| `SAMPle:STAT ON` | 启动输入采样。 |
| `SAMPle:STAT OFF` | 停止输入采样。 |
| `SAMPle:STAT?` | 查询采样状态。 |

## 同步时钟输出

| 命令 | 说明 |
|---|---|
| `OUTPut:CLOC:FREQ <Hz>` | 设置 `SYNC_CLK_OUT` 输出频率。当前固件运行在 AUX2/GPIO28；若 AUX persona/BiSS 占用 `PIO2 + AUX`，启动会返回执行错误并置资源冲突。 |
| `OUTPut:CLOC:FREQ?` | 查询同步时钟频率。 |
| `OUTPut:CLOC:STAT ON` | 启动同步时钟输出。 |
| `OUTPut:CLOC:STAT OFF` | 停止同步时钟输出。 |
| `OUTPut:CLOC:STAT?` | 查询同步时钟输出状态。 |

## 应用层语义 IO 与资源互斥

SCPI 产品接口按语义通道描述触发 IO，不应要求用户理解或切换任意 GPIO。量产默认映射为：

| 语义通道 | 产品物理通道 | GPIO | 说明 |
|---|---|---:|---|
| `TRIG_IN` | IN0 | 16 | 主触发、脉冲计数或编码器 A 相输入。 |
| `RJ45_TRIG_IN` | IN3 | 19 | RJ45 差分触发硬件输入；模式内也可解释为 gate/inhibit。 |
| `ARM_IN` | AUX0 | 26 | 外部 ARM 资格/请求；产品目标放在 AUX，避免与 `ENC_COUNT` B 相冲突。当前固件尚未接入 TriggerFB。 |
| `EXT_CLK_IN` | AUX1 | 27 | 外部参考/采样时钟预留；产品目标放在 AUX，避免污染主输入组。 |
| `GATE_IN` | IN3 | 19 | 模式层门控/抑制解释；底层硬件通道是 `RJ45_TRIG_IN`。 |
| `RJ45_TRIG_OUT` | OUT3 | 23 | RJ45 差分触发硬件输出；当前 BiSS crossing 使用该硬件语义。 |
| `TRIG_OUT` | OUT0 | 20 | 主确定性触发输出。 |
| `PULSE_OUT` | OUT1 | 21 | 第二路脉冲或序列位 1。 |
| `SYNC_CLK_OUT` | AUX2 | 28 | 同步时钟；当前固件已放在 AUX，避免与主序列位 2 冲突，并通过资源仲裁与 BiSS/AUX persona 互斥。 |

资源互斥规则：

| 状态/模式 | SCPI 约束 |
|---|---|
| `TRIG` armed | 主输出总线 OUT0..OUT3 被触发引擎独占；`TRIG:IMM`、`PULSe:IMM`、`RJ45:TRIG:IMM`、`MARKer:IMM` 这类主总线即时输出应返回 busy 或在运行前关闭。 |
| `ENC_COUNT` armed | IN0/IN1/IN2 被 A/B/Z 独占；AUX0=`ARM_IN` 可作为未来独立资格输入。IN3=`RJ45_TRIG_IN/GATE_IN` 不被 ENC 软件定义占用。OUT0 被比较触发占用。 |
| `BISS_ARMED` | BiSS-C TAP 占用 `PIO2 + AUX0..AUX3`，AUX framework 功能应返回 busy/执行错误。 |
| `IDLE` | 即时脉冲、同步时钟和 RJ45 trigger 兼容命令可以使用各自语义输出。 |

硬件已经定型后，`TRIG:ENC:APIN` 只接受 `16`。历史开发诊断入口
`TRIG:ENC:APIN 26` 已关闭，因为 AUX0/AUX1 是固定差分输入，AUX2/AUX3 是固定
差分输出，不能作为连续 4-pin 编码器输入组。后续新增 SCPI/UI 配置应优先使用
`TRIG_IN`、`ARM_IN`、`SYNC_CLK_OUT` 等语义名，而不是直接公开任意 GPIO。

## 触发模式

`TRIG` 运行域用于序列触发、校准预约和同步运行。当前对外只保留四个模式值：

| 模式 | 含义 | 说明 |
|---|---|---|
| `0` | `IDLE` | 空闲态，仅允许查询和配置。 |
| `1` | `TRIG` | 触发态，执行序列展开、步进和运行控制。 |
| `2` | `CAL` | 校准态，使用 `CONFigure:CAL` / `READ:CAL?`。 |
| `3` | `SYNC` | 同步态，使用 `CONFigure:SYNC` / `READ:SYNC?`。 |
| `4` | `SIM` | 模拟态，仅做序列回放和状态机验证，不驱动真实输出。 |

| 命令 | 说明 |
|---|---|
| `TRIGger:MODE <0..4>` | 设置当前运行模式。 |
| `TRIGger:MODE?` | 查询当前运行模式和模式号。 |
| `TRIGger:SEQuence:LENG <1..256>` | 设置编码序列长度，仅在 `TRIG` 模式下有效。 |
| `TRIGger:SEQuence:LENG?` | 查询序列长度。 |
| `TRIGger:SEQuence:WIDT <1..8>` | 设置编码输出位宽。 |
| `TRIGger:SEQuence:WIDT?` | 查询编码位宽。 |
| `TRIGger:SEQuence:INDE?` | 查询当前步进索引。 |
| `TRIGger:SEQuence:DATA <binary_block>` | 写入编码表（二进制块，长度=4×seq_length）。 |
| `TRIGger:SEQuence:DATA?` | 回读编码表。 |
| `TRIGger:STARt` | 启动当前模式。 |
| `TRIGger:STOP` | 停止当前模式并回到 `IDLE`。 |
| `TRIGger:FAULt` | 维护/验证命令：先强制触发 Trigger fault，再投递 StorageAO `FAULT_EVIDENCE` job 在 FAULT 后后台写入 snapshot/trace/report。 |
| `READ:TRIGger:PARameter?` | 查询触发参数，返回 `chan_cnt,pol,freq_cnt,wave_cnt,total_state_count`。 |
| `READ:TRIGger:STATe?` | 查询触发运行状态，返回 `mode,run_state,current_angle,angle_index,seq_index,late_count,error_code,breakpoint_enabled,breakpoint_angle`。 |
| `STATus:TRIG?` | 触发域摘要：模式、状态、seq_index、rollover_count、error_code。 |

## 开关控制

| 命令 | 说明 |
|---|---|
| `CONFigure:SWITch# <value>` | 独立切换开关；`#=1` 表示 SWITCH1（原 SP8T），`#=2` 表示 SWITCH2（原 SP2T）。SWITCH1 取 `1..8`，SWITCH2 取 `0/1`。运行中或开关已被序列引擎占用时返回 busy。 |
| `READ:SWITch#?` | 查询开关当前目标和状态；`#=1` 或 `#=2`。 |

## 角度配置

| 命令 | 说明 |
|---|---|
| `CONFigure:ANGLe:SWEEp <start_deg>,<stop_deg>,<step_deg>` | 设置扫描起始角度、终止角度和扫描角度步长，用于将输出脉冲对应到扫描位置。 |
| `READ:ANGLe:SWEEp?` | 查询当前扫描角度范围和步长。 |

## 序列维护

| 命令 | 说明 |
|---|---|
| `READ:SEQuence:CHECK?` | 维护/验证命令：检查序列长度、状态范围、SWITCH1/SWITCH2 组合和 CRC。 |

## 断点与分段运行

| 命令 | 说明 |
|---|---|
| `CONFigure:ANGLe:BP [angle_deg]` | 设置角度断点。给出参数时断点绑定到指定扫描角度；运行中或暂停中省略参数时，使用当前待输出的角度游标。 |
| `READ:ANGLe:BP?` | 查询当前角度断点配置、命中状态和断点游标。 |
| `CONFigure:ANGLe:BP:CLEAr` | 清除断点配置；若当前已因断点暂停，仅清除断点标志，不自动继续运行。 |

断点只作用于扫描角度，不作用于角度点内部的测试序列。断点在每个角度点开始前判断；若当前扫描角度命中断点，状态机进入 `PAUSED`，该角度点对应的测试序列尚未输出。后续 `TRIGger:CONTinue` 从该角度继续执行，并完整跑完该角度点内的测试序列。为避免原地再次命中，`TRIGger:CONTinue` 对当前断点位置做一次性越过；扫描循环后再次到达同一断点仍会暂停。`TRIGger:STOP` 停止运行但不改变断点配置，`*RST` 清除临时断点和运行游标。

## 序列展开

业务运行模型为 `扫描角度外层循环 -> 角度点内测试序列内层循环`。每个扫描角度点执行一组完整测试序列，序列展开顺序按 `通道 -> 极化 -> 频点 -> 波位` 进行，`CONFigure:TRIGger` 的参数顺序也与此一致，写作 `chan_cnt,pol,freq_cnt,wave_cnt`。其中 `pol=0` 表示 H，`pol=1` 表示 V，`pol=2` 表示 BOTH，且 `2` 会展开成 H/V 两态。

## PCNT 参数接口

以下命令当前写入 TriggerVector/PCNT 配置快照，其中部分能力仍是后续 PIO 增强预留项。

| 命令 | 说明 |
|---|---|
| `TRIGger:PCNT:DEC <0..3>` | 设置解码模式：`0=SINGLE`，`1=QUAD1X`，`2=QUAD2X`，`3=UPDOWN`。 |
| `TRIGger:PCNT:DEC?` | 查询解码模式。 |
| `TRIGger:PCNT:DIR <0..2>` | 设置方向：`0=CW`，`1=CCW`，`2=BOTH`。 |
| `TRIGger:PCNT:DIR?` | 查询方向配置。 |
| `TRIGger:PCNT:FILT <ns>` | 设置滤波窗口配置值。 |
| `TRIGger:PCNT:FILT?` | 查询滤波窗口配置值。 |
| `TRIGger:PCNT:GATE <ON\|OFF>` | 设置 PCNT 门控配置位。 |
| `TRIGger:PCNT:GATE?` | 查询 PCNT 门控配置位。 |
| `TRIGger:PCNT:CMP <ns>` | 设置比较器触发脉冲宽度配置值。 |
| `TRIGger:PCNT:CMP?` | 查询比较器触发脉冲宽度配置值。 |
| `TRIGger:PCNT:PRES <value>` | 设置预设计数值。 |
| `TRIGger:PCNT:PRES?` | 查询预设计数值。 |
| `TRIGger:PCNT:CLE` | 清零当前 PCNT 计数，并先累计到 `enc_total`。 |
| `TRIGger:PCNT:TOT?` | 查询累计计数。 |
| `TRIGger:PCNT:FREQ?` | 查询频率快照字段。 |

## 触发测量

| 命令 | 说明 |
|---|---|
| `MEASure:FREQ? <gate_ms>` | 使用 MCU 内部门控读取序列硬件计数，返回频率 Hz。`gate_ms` 建议 10..60000。 |
| `MEASure:REP?` | 查询最近一次非阻塞测量报告；当前主要供内部工具使用。 |

## 状态查询

| 命令 | 说明 |
|---|---|
| `STATus:SYNC?` | 返回同步 IO 状态：初始化状态、采样状态、时钟状态、采样率、时钟频率、采样溢出计数。 |
| `STATus:TRIG?` | 返回触发域状态：模式、状态、源引脚、seq_index、enc_target、enc_count、trigger_count、rollover_count、error_code。`error_code` 当前稳定值：`0=NONE`、`1=INVALID_SEQ_CONFIG`、`2=RESOURCE_CONFLICT`、`3=IO_ARM_FAILED`、`4=IO_LOST`、`10=INVALID_ENC_TARGET`、`11=INVALID_ENC_PINS`、`20=INVALID_BISS_CONFIG`、`100=FORCED_FAULT`。 |
| `SYSTem:RES?` | 返回资源仲裁摘要：`active_resources,last_conflict_resources,request_owner,holder_owner`。用于调试触发模式、AUX persona、PIO/DMA/SD/OTA 等资源冲突。 |

## SD / System Pack 维护

SD 命令遵循 `docs/SD_TODO.md` 中的 `StorageAO + StorageFB + StorageVector` 设计。SCPI 只负责表达查询或维护意图，后续应逐步收敛到 StorageAO job；当前 P0A/P0B/P0C 已完成 `FILE_INFO`、`FILE_READ`、`CATALOG_PAGE`、`MANIFEST_SCAN`、`SYSTEM_INIT`、`SNAPSHOT_WRITE` 和 `FAULT_EVIDENCE` job 闭环。

| 命令 | 说明 |
|---|---|
| `SYSTem:SD:STAT?` | 查询 SD 状态摘要：状态、card_present、fs_mounted、底层卡状态、storage_error。 |
| `SYSTem:SD:INFO?` | 查询 SD 卡信息：状态、卡类型、high_capacity、block_count、capacity_kib、fatfs_available、fs_mounted、probe_count。 |
| `SYSTem:SD:RAW:CLEAR <sectors>,"ERASE"` | 破坏性维护命令：绕过 FatFs 写零 SD 卡前 `sectors` 个 512B 扇区，当前固件最大 64；用于卡分区/FAT 元数据导致主机格式化卡死时清前缀。返回 `status,requested,cleared,raw_status,storage_error`。会破坏分区表/FAT，不用于正常 release 流程；Trigger armed 时拒绝执行。 |
| `SYSTem:SD:RAW:READ? <sector>` | 维护诊断命令：绕过 FatFs 读取单个 512B 扇区，并返回前 64 字节十六进制，用于确认 Pico 侧格式化或 raw 写入是否真实落卡。返回 `status,sector,raw_status,storage_error,hex64`；Trigger armed 时拒绝执行。 |
| `SYSTem:SD:MKFS "ERASE"` | 破坏性维护命令：由 Pico 通过 FatFs `f_mkfs` 在 SD 卡上创建 FAT/FAT32 文件系统，必须带确认字符串 `"ERASE"`。返回 `status,fatfs_status,storage_state,storage_error,block_count,capacity_kib,mkfs_result,mount_result`。只作用于 Pico 上的 SD 卡，不访问主机盘符；Trigger armed 时拒绝执行。 |
| `SYSTem:SD:INIT` | 非破坏性初始化命令：在已挂载 FAT32 卡上创建最小 System Pack 目录和默认文件；若 `/manifest.idx` 已存在则不覆盖。返回 `status,manifest_status,schema,build_id,required_count,missing_count,error`。不会格式化 SD 卡。 |
| `SYSTem:SD:MAN?` | 投递 StorageAO `MANIFEST_SCAN` job 扫描 `/manifest.idx`；若 FAT32 卡可挂载但 `/manifest.idx` 缺失，会先执行同一套非破坏性 System Pack 初始化再重新扫描。兼容返回 manifest 状态、schema、product_id、hardware_id、build_id、required_count、missing_count；Trigger armed 时拒绝执行。 |
| `SYSTem:STOR:STAT?` | `SYSTem:SD:STAT?` 的 Storage 域别名。 |
| `SYSTem:STOR:JOB:INFO "<path>"` | 投递 StorageAO `FILE_INFO` job，返回 `"OK",job_id`；实际 FatFs 查询在 `storage_manager_service()` 中执行。 |
| `SYSTem:STOR:JOB?` | 查询最近 Storage job：`state,id,type,path,size,kind,path_hash,error`；当前 type 包含 `FILE_INFO`、`FILE_READ`、`CATALOG_PAGE`、`MANIFEST_SCAN`、`SYSTEM_INIT`、`SNAPSHOT_WRITE`、`FAULT_EVIDENCE`，manifest/system-init job 的 kind 为 `MANIFEST` 且 size 为 required_count，file read job 的 kind 为 `READ` 且 size 为本次返回字节数，catalog page job 的 kind 为 `CATALOG` 且 size 为本页返回条目数，fault evidence job 的 path 指向最新 fault report。 |
| `MMEM:CAT? ["<path>"]` | 兼容诊断目录枚举；内部投递 `CATALOG_PAGE` job 包装第 0 页，最多 16 项。长目录可能不完整，可靠枚举必须使用 `MMEM:CAT:PAGE?`。非法路径如 `/../` 应返回 `PATH_DENIED`。 |
| `MMEM:CAT:PAGE? "<path>",<offset>,<limit>` | 投递 StorageAO `CATALOG_PAGE` job 分页枚举白名单路径目录，返回 `status,path,offset,returned,next_offset,complete,truncated,entries`；`limit` 固件端最大限制为 16；Trigger armed 时拒绝执行。 |
| `MMEM:INFO? "<path>"` | 投递 StorageAO `FILE_INFO` job 查询白名单路径中的单个文件或目录信息，兼容返回 `status,path,size,kind,path_hash,error`；用于长目录截断时稳定确认文件存在；Trigger armed 时拒绝执行。 |
| `MMEM:READ? "<path>",<offset>,<length>` | 投递 StorageAO `FILE_READ` job，受限读取白名单路径中的文件片段，兼容返回 `status,path,offset,requested,returned,eof,path_hash,error,hex`；`length` 固件端最大限制为 128 字节，当前用于 SD 验证工具读回 trace `.bin/.idx`；Trigger armed 时拒绝执行。 |
| `SYSTem:SNAPshot:WRIT ["boot"\|"arm"\|"fault"\|"run"]` | 投递 StorageAO `SNAPSHOT_WRITE` job 写入 snapshot JSON；省略 kind 时默认 `boot`。兼容层等待 job 完成后返回 `"OK"`。`TRIGger:STARt` 内部同样通过 `SNAPSHOT_WRITE("arm")` 生成启动前 snapshot；手动命令在 Trigger armed 时拒绝执行。 |
| `SYSTem:SNAPshot:LAST?` | 查询最近 snapshot 摘要：状态、kind、sequence、path、path_hash、error。 |
| `SYSTem:TRACe:LAST?` | 查询最近 trace 摘要：状态、kind、sequence、path、path_hash、event_count、error。 |
| `SYSTem:FAULT:LAST?` | 查询最近 fault report 摘要：状态、report_id、report_path、path_hash、snapshot_id、trace_id、error。 |

## OTA 维护

OTA 命令遵循 `docs/OTA_SYSTEM_DESIGN.md` 中的 `OtaAO + OtaFB + OtaVector` 设计。SCPI 只负责解析命令、投递 OTA 事件和读取状态快照，不直接擦写 Flash，不直接修改 OTA 状态。

| 命令 | 说明 |
|---|---|
| `SYSTem:OTA:STAT?` | 查询 OTA 状态摘要：状态、目标 slot、错误码、最近结果。 |
| `SYSTem:OTA:PROG?` | 查询 OTA 进度：已接收字节、期望字节、千分比进度。 |
| `SYSTem:OTA:BEGIN <size>,<crc32>` | 开始 OTA 传输，`size/crc32` 对应标准 raw firmware `.bin`，接受后返回 `"OK"`。 |
| `SYSTem:OTA:PBEGIN <size>,<crc32>` | 开始统一 OTA package 传输，包内包含 Slot A/Slot B 两个 App 镜像；下位机根据当前 OTA 模式和 target slot 自行选择写入镜像。 |
| `SYSTem:OTA:DATA #<block>` | 发送 `.bin` 二进制块，投递 `OTA_EVENT_DATA_BLOCK`，为保证吞吐当前不逐块返回 ACK。 |
| `SYSTem:OTA:END` | 结束传输并请求校验，投递 `OTA_EVENT_END`，接受后返回 `"OK"`。 |
| `SYSTem:OTA:ABOR` | 中止当前 OTA，投递 `OTA_EVENT_ABORT`，接受后返回 `"OK"`。 |
| `SYSTem:OTA:BOOT` | 镜像 ready 后请求重启进入 pending slot，接受后返回 `"OK"` 并触发复位。 |
| `SYSTem:OTA:COMM` | App 自检通过后确认当前固件，写入 confirmed metadata；该命令会触发 flash 写入，执行时必须先由 core1 进入 lockout/poll，接受后返回 `"OK"`。 |
| `SYSTem:OTA:SLOT?` | 查询 `active,pending,confirmed,boot_attempts,rollback_count`。 |
| `SYSTem:OTA:RES?` | 查询 `app_result,app_error,boot_result,boot_source_slot,boot_size,boot_crc32`。 |
| `SYSTem:OTA:TXN?` | 查询 Bootloader copy transaction：`state,source,destination,size,crc32,written,attempts,last_error`。 |
| `SYSTem:OTA:MODE?` | 查询当前 OTA 启动模式：`"COPY_TO_ACTIVE",0` 或 `"DIRECT_AB",1`。 |
| `SYSTem:OTA:TARG?` | 查询下一次 OTA 写入目标 slot，当前 copy-to-active 默认返回 `2`。 |
| `SYSTem:OTA:CAP?` | 查询当前固件声明的 OTA 能力位，`bit0=COPY_TO_ACTIVE`，`bit1=DIRECT_AB`。 |

统一 OTA package 由 `tools/ota_packager/ota_packager.py` 生成，`tools/ota_send/ota_send.py` 会自动识别包头并发送 `SYSTem:OTA:PBEGIN`。package 首部固定 512 B，包含产品型号、硬件版本、App 版本、build id、payload SHA-256、最小 Bootloader 版本、每个镜像的 slot/offset/size/CRC32/run offset。payload 中 Slot A/Slot B 镜像按 512 B 对齐，保证流式写入时满足 Flash page 编程约束。设备在擦除目标 slot 前会拒绝产品型号、硬件版本和最小 Bootloader 版本不匹配的 package。

第一阶段建议 `SYSTem:OTA:DATA` 单块 256 B 或 512 B。OTA 期间应暂停周期日志，避免日志与 SCPI binary block 混用同一 USBTMC 通道。

`tools/ota_send/ota_send.py` 支持统一 package 负向验证参数：

| 参数 | 说明 | 期望错误 |
|---|---|---|
| `--corrupt-crc` | 故意发送错误的 `PBEGIN` 整包 CRC。 | `CRC` |
| `--package-negative image-crc` | 修改被选中镜像的 header CRC。 | `CRC` |
| `--package-negative image-vector` | 破坏被选中镜像 reset vector，并同步更新镜像 CRC。 | `VECTOR` |
| `--package-negative header-magic` | 破坏 package magic。 | `BAD_HEADER` |
| `--package-negative header-version` | 破坏 package version。 | `BAD_HEADER` |
| `--package-negative header-size` | 破坏 package size。 | `BAD_HEADER` |
| `--package-negative slot` | 破坏被选中镜像 slot 字段。 | `BAD_HEADER` |
| `--package-negative run-offset` | 破坏被选中镜像 run offset。 | `IMAGE_TOO_LARGE` |

## OTA 故障注入

以下命令仅在 CMake 选项 `PROJECT_ENABLE_OTA_FAULT_INJECTION=ON` 时编译，用于研发验证和产测调试，量产固件应关闭。命令会擦写 OTA metadata 或强制 Bootloader 失败，不应开放给最终用户。当前工程使用 `pico2-validation` 构建开启这些命令，`pico2-release` 构建关闭这些命令。

| 命令 | 说明 |
|---|---|
| `SYSTem:OTA:INJ:COPY` | 设置下一次 Bootloader Slot B -> Slot A 复制失败注入标志。需要已烧入支持该功能的 Bootloader。 |
| `SYSTem:OTA:INJ:COPY?` | 查询当前 OTA 故障注入标志，`0` 表示未开启。 |
| `SYSTem:OTA:INJ:CLEAR` | 清除 OTA 故障注入标志。 |
| `SYSTem:OTA:INJ:MCOR <0|1>` | 擦除指定 metadata 副本，用于验证双副本容错。 |
| `SYSTem:OTA:INJ:MREP` | 从当前有效 metadata 重新写入双副本，用于恢复 metadata 冗余。 |
| `SYSTem:OTA:MODE <0|1>` | 切换 OTA 启动模式，`0=COPY_TO_ACTIVE`，`1=DIRECT_AB`。仅用于 direct A/B 台架验证。 |
| `SYSTem:BOOT:RES` | 通过 watchdog 触发系统复位。仅用于 validation 固件验证 Bootloader 回滚路径。 |

复制失败注入的期望结果：OTA payload 已进入 `READY_TO_REBOOT` 后发送 `SYSTem:OTA:BOOT`，Bootloader 应记录 `COPY_FAILED`，清除 pending，保留旧 App 运行，`rollback_count` 增加。

## 当前限制

- 日志和 SCPI 响应目前共用 stdio 通道，后续产品化应拆分控制通道和日志通道，或在 SCPI 会话期间关闭周期日志。
- `SAMPle:RATE` 当前会直接启动采样，但尚未接入 DMA 环形缓冲。
- OTA 命令已接入 `OtaAO/OtaFB/OtaVector`，SCPI 只投递事件和读取快照，不直接调用 Flash 擦写 API。
