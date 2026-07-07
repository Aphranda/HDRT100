# SCPI 基础命令

当前 SCPI 服务通过 Pico SDK `stdio` 通道接入，默认使用 USB CDC。命令以 `\n` 或 `\r\n` 结束。Trigger 相关控制命令当前已经通过 `sync_trigger` 事件接口收口，SCPI 不再直接调用底层 `sync_io`。

## 标准命令

| 命令 | 说明 |
|---|---|
| `*IDN?` | 查询设备身份。 |
| `*RST` | 恢复 SCPI 当前基础配置，并停止采样和同步时钟。 |
| `*CLS` | 清除 SCPI 状态/错误。 |
| `*TST?` | 自检占位，当前返回 `0`。 |
| `SYST:ERR?` | 查询下一条 SCPI 错误。 |
| `SYST:ERR:COUN?` | 查询 SCPI 错误数量。 |
| `SYST:VERS?` | 查询 SCPI 标准版本。 |
| `SYST:FW:VERS?` | 查询固件语义版本，返回 `major,minor,patch`。 |
| `SYST:FW:BUILD?` | 查询固件 build id，由构建脚本生成 UTC 时间戳，每次构建刷新。 |
| `SYST:BOOT:VERS?` | 查询当前 App 声明的 Bootloader 兼容版本，返回 `major,minor,patch`。 |
| `SYST:BOOT:CAP?` | 查询当前 metadata 中记录的 Bootloader/OTA 能力位，`bit0=COPY_TO_ACTIVE`，`bit1=DIRECT_AB`。 |

## 触发输出

| 命令 | 说明 |
|---|---|
| `TRIG:WIDT <us>` | 设置 `GPIO20/TRIG_OUT` 脉宽，单位 us。 |
| `TRIG:WIDT?` | 查询 `TRIG_OUT` 脉宽。 |
| `TRIG:IMM` | 立即输出一次 `TRIG_OUT` 脉冲。 |

## 第二路脉冲输出

| 命令 | 说明 |
|---|---|
| `PULS:WIDT <us>` | 设置 `GPIO21/PULSE_OUT` 脉宽，单位 us。 |
| `PULS:WIDT?` | 查询 `PULSE_OUT` 脉宽。 |
| `PULS:IMM` | 立即输出一次 `PULSE_OUT` 脉冲。 |

## Marker 输出

| 命令 | 说明 |
|---|---|
| `MARK:WIDT <us>` | 设置 `GPIO23/MARKER_OUT` 脉宽，单位 us。 |
| `MARK:WIDT?` | 查询 `MARKER_OUT` 脉宽。 |
| `MARK:IMM` | 立即输出一次 `MARKER_OUT` 脉冲。 |

## 采样配置

| 命令 | 说明 |
|---|---|
| `SAMP:RATE <Hz>` | 设置输入采样率，并启动 `GPIO16..GPIO19` 采样。 |
| `SAMP:RATE?` | 查询输入采样率。 |
| `SAMP:STAT ON` | 启动输入采样。 |
| `SAMP:STAT OFF` | 停止输入采样。 |
| `SAMP:STAT?` | 查询采样状态。 |

## 同步时钟输出

| 命令 | 说明 |
|---|---|
| `OUTP:CLOC:FREQ <Hz>` | 设置 `SYNC_CLK_OUT` 输出频率。产品目标映射为 AUX2/GPIO28；当前固件仍在旧路径 GPIO22，需迁移。 |
| `OUTP:CLOC:FREQ?` | 查询同步时钟频率。 |
| `OUTP:CLOC:STAT ON` | 启动同步时钟输出。 |
| `OUTP:CLOC:STAT OFF` | 停止同步时钟输出。 |
| `OUTP:CLOC:STAT?` | 查询同步时钟输出状态。 |

## 应用层语义 IO 与资源互斥

SCPI 产品接口按语义通道描述触发 IO，不应要求用户理解或切换任意 GPIO。量产默认映射为：

| 语义通道 | 产品物理通道 | GPIO | 说明 |
|---|---|---:|---|
| `TRIG_IN` | IN0 | 16 | 主触发、脉冲计数或编码器 A 相输入。 |
| `ARM_IN` | AUX0 | 26 | 外部 ARM 资格/请求；产品目标放在 AUX，避免与 `ENC_COUNT` B 相冲突。当前固件尚未接入 TriggerFB。 |
| `EXT_CLK_IN` | AUX1 | 27 | 外部参考/采样时钟预留；产品目标放在 AUX，避免污染主输入组。 |
| `GATE_IN` | IN3 | 19 | 外部门控/抑制；在 `ENC_COUNT` 中由 Z 相占用。 |
| `TRIG_OUT` | OUT0 | 20 | 主确定性触发输出。 |
| `PULSE_OUT` | OUT1 | 21 | 第二路脉冲或 `SEQ_STEP` bit1。 |
| `SYNC_CLK_OUT` | AUX2 | 28 | 同步时钟；产品目标放在 AUX，避免与 `SEQ_STEP` bit2 冲突。当前固件仍在旧路径 GPIO22。 |
| `MARKER_OUT` | AUX3 | 29 | Marker/debug/status；产品目标放在 AUX，避免与 `SEQ_STEP` bit3 冲突。当前固件仍在旧路径 GPIO23。 |

资源互斥规则：

| 状态/模式 | SCPI 约束 |
|---|---|
| `SEQ_STEP` armed | 主输出总线 OUT0..OUT3 被序列引擎独占；`TRIG:IMM`、`PULS:IMM` 这类主总线即时输出应返回 busy 或在 ARM 前关闭。产品迁移后 `SYNC_CLK_OUT`/`MARKER_OUT` 使用 AUX2/AUX3，可独立于序列总线。 |
| `ENC_COUNT` armed | IN0/IN1/IN3 被 A/B/Z 独占；AUX0=`ARM_IN` 可作为未来独立资格输入。IN3=`GATE_IN` 与 Z 相仍冲突。OUT0 被比较触发占用。 |
| `BISS_ARMED` | BiSS-C TAP 占用 `PIO2 + AUX0..AUX3`，`TRIG:ENC:APIN 26` 和 AUX framework 功能应返回 busy/执行错误。 |
| `IDLE` | 即时脉冲、同步时钟和 Marker 命令可以使用各自语义输出。 |

`TRIG:ENC:APIN <16|26>` 中的 `26` 组属于开发/诊断级复用，会占用 AUX0..AUX3；
产品默认仍应使用 `16` 组。BiSS 已配置或已 armed 时，`TRIG:ENC:APIN 26`
会被拒绝。后续新增 SCPI/UI 配置应优先使用 `TRIG_IN`、
`ARM_IN`、`SYNC_CLK_OUT` 等语义名，而不是直接公开任意 GPIO。

## SEQ_STEP 编码序列步进模式

触发输入每个上升沿使编码输出步进到下一序列值。详细设计见 `docs/TRIGGER_SEQ_STEP_MODE.md`。

产品硬件默认固定使用统一主触发 IO：输入 `GPIO16..GPIO19`，输出 `GPIO20..GPIO23`。
这些通道只承载模式相关的高速输入/输出；跨模式功能信号如 `ARM_IN`、`EXT_CLK_IN`、
`SYNC_CLK_OUT`、`MARKER_OUT` 约束在 AUX0..AUX3。

| 命令 | 说明 |
|---|---|
| `TRIG:MODE <0\|1>` | 设置触发模式，`0=IDLE`，`1=SEQ_STEP`。 |
| `TRIG:MODE?` | 查询当前触发模式和模式号。 |
| `TRIG:SEQ:LENG <1..256>` | 设置编码序列长度。 |
| `TRIG:SEQ:LENG?` | 查询序列长度。 |
| `TRIG:SEQ:WIDT <1..8>` | 设置编码输出位宽。 |
| `TRIG:SEQ:WIDT?` | 查询编码位宽。 |
| `TRIG:SEQ:INDE?` | 查询当前步进索引。 |
| `TRIG:SEQ:DATA <binary_block>` | 写入编码表（二进制块，长度=4×seq_length）。 |
| `TRIG:SEQ:DATA?` | 回读编码表。 |
| `TRIG:ARM` | 加载 PIO + DMA，进入 SEQ_ARMED。 |
| `TRIG:DISA` | 停止 PIO + DMA，回到 IDLE。 |
| `TRIG:FAULT` | 维护/验证命令：先强制触发 Trigger fault，再投递 StorageAO `FAULT_EVIDENCE` job 在 FAULT 后后台写入 snapshot/trace/report。 |
| `STAT:TRIG?` | 触发域摘要：模式、状态、seq_index、rollover_count、error_code。 |

## ENC_COUNT 编码器计数触发模式

编码器 A 相上升沿计数，达到目标计数后在 `GPIO20/TRIG_OUT` 输出触发脉冲。详细设计见 `docs/TRIGGER_ENC_COUNT_MODE.md`。

| 命令 | 说明 |
|---|---|
| `TRIG:MODE 2` | 设置触发模式为 `ENC_COUNT`。 |
| `TRIG:ENC:TARG <N>` | 设置目标计数值，`N > 0`。 |
| `TRIG:ENC:TARG?` | 查询目标计数值。 |
| `TRIG:ENC:COUN?` | 查询当前计数快照。 |
| `TRIG:ENC:APIN <16\|26>` | 选择编码器输入组基脚。产品默认 `16` = A/B/Z `GPIO16/GPIO17/GPIO19`；`26` = A/B/Z `GPIO26/GPIO27/GPIO29` 仅作为开发诊断复用，会占用 AUX 功能接口。 |
| `TRIG:ENC:APIN?` | 查询当前 A/B/Z 实际 GPIO，返回 `A,B,Z`。 |
| `TRIG:ENC:REV?` | 查询 Z 脉冲累计圈数。 |

当前 `enc_count.pio` 使用 4-pin 连续输入组采样：A=base、B=base+1、base+2 保留、Z=base+3。因此暂不支持任意非连续 A/B/Z 引脚组合。为保持主输入输出纯粹并保留 AUX 功能接口，量产配置应使用 `TRIG:ENC:APIN 16`。

## BiSS-C TAP 协议触发模式

BiSS-C P0 阶段只实现 TAP monitor：监听 AUX0/AUX1 上的 CLK/DATA，按固定 profile 抽取 position，并在 crossing 命中后从 `TRIG_OUT` 输出触发。`TRIG:BISS:ROLE` 保留未来角色的数值兼容关系：`0=TAP`、`1=SLAVE`、`2=MASTER`、`3=BRIDGE`。当前只有 `0=TAP` 可配置和 ARM；非 TAP role 可写入用于兼容/显示，但 `TRIG:MODE 3` 或 `TRIG:ARM` 会返回执行错误，`STAT:BISS?` 中 role 状态返回 `NOT_IMPLEMENTED`。

P0 profile mutation 在 `BISS_ARMED` 状态下会返回错误；应先 `TRIG:DISA`，修改 profile 后再 `TRIG:MODE 3` 和 `TRIG:ARM`。

| 命令 | 说明 |
|---|---|
| `TRIG:MODE 3` | 使用当前 BiSS profile 配置协议触发模式。P0 仅接受 `ROLE=0`。 |
| `TRIG:BISS:ROLE <0..3>` | 设置 BiSS role：`0=TAP`、`1=SLAVE`、`2=MASTER`、`3=BRIDGE`。P0 仅实现 TAP。 |
| `TRIG:BISS:ROLE?` | 查询 role 名称和数值。 |
| `TRIG:BISS:DEV <id>` | 设置 profile/device id，供上位机区分固定 profile。 |
| `TRIG:BISS:DEV?` | 查询 profile/device id。 |
| `TRIG:BISS:CLOC <Hz>` | 设置 BiSS clock Hz，用于计算 PIO sample delay 合法范围。 |
| `TRIG:BISS:CLOC?` | 查询 BiSS clock Hz。 |
| `TRIG:BISS:FBIT <1..64>` | 设置固定帧位宽。 |
| `TRIG:BISS:FBIT?` | 查询固定帧位宽。 |
| `TRIG:BISS:POFF <0..63>` | 设置 position 起始 bit offset，MSB-first。 |
| `TRIG:BISS:POFF?` | 查询 position 起始 bit offset。 |
| `TRIG:BISS:PBIT <1..32>` | 设置 position 位宽。 |
| `TRIG:BISS:PBIT?` | 查询 position 位宽。 |
| `TRIG:BISS:PMOD <N>` | 设置 position crossing modulo，`N > 0`。 |
| `TRIG:BISS:PMOD?` | 查询 position crossing modulo。 |
| `TRIG:BISS:TARG <value>` | 设置 position crossing 触发阈值；`0` 表示软件路径不触发 crossing。 |
| `TRIG:BISS:TARG?` | 查询 position crossing 触发阈值。 |
| `TRIG:BISS:SAMP:EDGE <0\|1>` | 设置采样边沿：`0=RISING`，`1=FALLING`。 |
| `TRIG:BISS:SAMP:EDGE?` | 查询采样边沿。 |
| `TRIG:BISS:SAMP:DEL <cycles>` | 设置 PIO sample delay cycles，必须小于每 bit 的 PIO cycle 数。 |
| `TRIG:BISS:SAMP:DEL?` | 查询 PIO sample delay cycles。 |
| `TRIG:BISS:SAMP:SCAN <0\|1>` | 启用或关闭 timeout 后 sample delay scan。 |
| `TRIG:BISS:SAMP:SCAN?` | 查询 sample delay scan 使能状态。 |
| `TRIG:BISS:SAMP:SCAN:STAR <cycles>` | 设置 sample delay scan 起始 cycles。 |
| `TRIG:BISS:SAMP:SCAN:STAR?` | 查询 sample delay scan 起始 cycles。 |
| `TRIG:BISS:SAMP:SCAN:END <cycles>` | 设置 sample delay scan 结束 cycles。 |
| `TRIG:BISS:SAMP:SCAN:END?` | 查询 sample delay scan 结束 cycles。 |
| `TRIG:BISS:SAMP:SCAN:STEP <cycles>` | 设置 sample delay scan 步进 cycles，必须大于 0。 |
| `TRIG:BISS:SAMP:SCAN:STEP?` | 查询 sample delay scan 步进 cycles。 |
| `TRIG:BISS:TIME <us>` | 设置帧 timeout，单位 us，必须大于 0。 |
| `TRIG:BISS:TIME?` | 查询帧 timeout。 |
| `TRIG:BISS:ANCH:OFFS <0..63>` | 设置 anchor 起始 bit offset。 |
| `TRIG:BISS:ANCH:OFFS?` | 查询 anchor 起始 bit offset。 |
| `TRIG:BISS:ANCH:BITS <0..64>` | 设置 anchor bit width；`0` 表示关闭 anchor check。 |
| `TRIG:BISS:ANCH:BITS?` | 查询 anchor bit width。 |
| `TRIG:BISS:ANCH:MASK <value>` | 设置 anchor compare mask，当前 SCPI 写入低 32 bit。 |
| `TRIG:BISS:ANCH:MASK?` | 查询 anchor compare mask。 |
| `TRIG:BISS:ANCH:VAL <value>` | 设置 anchor expected value，当前 SCPI 写入低 32 bit。 |
| `TRIG:BISS:ANCH:VAL?` | 查询 anchor expected value。 |
| `TRIG:BISS:ERR:BIT <offset>` | 设置 ERR bit offset；`4294967295` 表示禁用。 |
| `TRIG:BISS:ERR:BIT?` | 查询 ERR bit offset。 |
| `TRIG:BISS:WARN:BIT <offset>` | 设置 WRN bit offset；`4294967295` 表示禁用。 |
| `TRIG:BISS:WARN:BIT?` | 查询 WRN bit offset。 |
| `TRIG:BISS:STAT:GATE <0..2>` | 设置状态位 gate：`0=IGNORE`，`1=COUNT_ONLY`，`2=BLOCK_TRIGGER`。 |
| `TRIG:BISS:STAT:GATE?` | 查询状态位 gate。 |
| `TRIG:BISS:CRC:OFFS <0..63>` | 设置 CRC 字段起始 bit offset。 |
| `TRIG:BISS:CRC:OFFS?` | 查询 CRC 字段起始 bit offset。 |
| `TRIG:BISS:CRC:BITS <0..16>` | 设置 CRC bit width；`0` 表示关闭 CRC check。 |
| `TRIG:BISS:CRC:BITS?` | 查询 CRC bit width。 |
| `TRIG:BISS:CRC:COV:OFFS <0..63>` | 设置 CRC 覆盖区域起始 bit offset。 |
| `TRIG:BISS:CRC:COV:OFFS?` | 查询 CRC 覆盖区域起始 bit offset。 |
| `TRIG:BISS:CRC:COV:BITS <0..64>` | 设置 CRC 覆盖区域 bit width。 |
| `TRIG:BISS:CRC:COV:BITS?` | 查询 CRC 覆盖区域 bit width。 |
| `TRIG:BISS:CRC:POLY <value>` | 设置 CRC polynomial，当前接受低 16 bit。 |
| `TRIG:BISS:CRC:POLY?` | 查询 CRC polynomial。 |
| `TRIG:BISS:CRC:INIT <value>` | 设置 CRC init，当前接受低 16 bit。 |
| `TRIG:BISS:CRC:INIT?` | 查询 CRC init。 |
| `TRIG:BISS:CRC:XOR <value>` | 设置 CRC final xor，当前接受低 16 bit。 |
| `TRIG:BISS:CRC:XOR?` | 查询 CRC final xor。 |
| `TRIG:BISS:CRC:INV <0\|1>` | 设置线上的 CRC 字段是否 invert。 |
| `TRIG:BISS:CRC:INV?` | 查询 CRC invert 配置。 |
| `TRIG:BISS:CRC:GATE <0\|1>` | 设置 CRC gate：`0=LATE_COUNT`，`1=BLOCK_TRIGGER`。 |
| `TRIG:BISS:CRC:GATE?` | 查询 CRC gate。 |
| `TRIG:BISS:LAT:OFFS <ns>` | 设置已测得的固定 latency offset，单位 ns。 |
| `TRIG:BISS:LAT:OFFS?` | 查询固定 latency offset。 |
| `TRIG:BISS:PINS?` | 查询 BiSS 语义引脚：`clk_in,data_in,clk_out,data_out,pulse_in,pulse_out`。 |
| `TRIG:BISS:PULS [delta]` | 软件注入本地 pulse-in 事件，默认 delta=1，用于管理面测试。 |
| `TRIG:BISS:FRAM <position>` | 软件注入收到的 position/event_count，用于管理面 crossing 测试。 |
| `TRIG:BISS:CRC:ERR` | 软件注入 CRC error 计数。 |
| `TRIG:BISS:TIME:INJ` | 软件注入 timeout 计数。 |
| `STAT:BISS?` | 查询 BiSS 摘要：role 名称、role 数值、role 状态、trigger 状态、device、clock、frame bits、position offset/bits/modulo、target、last position、last seq、frame error、status block、CRC error、FIFO overflow、timeout、trigger、pulse-in、tx frame、rx frame、pulse-out、active sample edge、active sample delay、scan index、scan wrap。 |

## PCNT 参数接口

以下命令当前写入 TriggerVector/PCNT 配置快照，其中部分能力仍是后续 PIO 增强预留项。

| 命令 | 说明 |
|---|---|
| `TRIG:PCNT:DEC <0..3>` | 设置解码模式：`0=SINGLE`，`1=QUAD1X`，`2=QUAD2X`，`3=UPDOWN`。 |
| `TRIG:PCNT:DEC?` | 查询解码模式。 |
| `TRIG:PCNT:DIR <0..2>` | 设置方向：`0=CW`，`1=CCW`，`2=BOTH`。 |
| `TRIG:PCNT:DIR?` | 查询方向配置。 |
| `TRIG:PCNT:FILT <ns>` | 设置滤波窗口配置值。 |
| `TRIG:PCNT:FILT?` | 查询滤波窗口配置值。 |
| `TRIG:PCNT:GATE <ON\|OFF>` | 设置 PCNT 门控配置位。 |
| `TRIG:PCNT:GATE?` | 查询 PCNT 门控配置位。 |
| `TRIG:PCNT:CMP <ns>` | 设置比较器触发脉冲宽度配置值。 |
| `TRIG:PCNT:CMP?` | 查询比较器触发脉冲宽度配置值。 |
| `TRIG:PCNT:PRES <value>` | 设置预设计数值。 |
| `TRIG:PCNT:PRES?` | 查询预设计数值。 |
| `TRIG:PCNT:CLE` | 清零当前 PCNT 计数，并先累计到 `enc_total`。 |
| `TRIG:PCNT:TOT?` | 查询累计计数。 |
| `TRIG:PCNT:FREQ?` | 查询频率快照字段。 |

## 触发测量

| 命令 | 说明 |
|---|---|
| `MEAS:FREQ? <gate_ms>` | 使用 MCU 内部门控读取 SEQ_STEP 硬件计数，返回频率 Hz。`gate_ms` 建议 10..60000。 |
| `MEAS:REP?` | 查询最近一次非阻塞测量报告；当前主要供内部工具使用。 |

## 状态查询

| 命令 | 说明 |
|---|---|
| `STAT:SYNC?` | 返回同步 IO 状态：初始化状态、采样状态、时钟状态、采样率、时钟频率、采样溢出计数。 |
| `STAT:TRIG?` | 返回触发域状态：模式、状态、源引脚、seq_index、enc_target、enc_count、trigger_count、rollover_count、error_code。 |

## SD / System Pack 维护

SD 命令遵循 `docs/SD_TODO.md` 中的 `StorageAO + StorageFB + StorageVector` 设计。SCPI 只负责表达查询或维护意图，后续应逐步收敛到 StorageAO job；当前 P0A/P0B/P0C 已完成 `FILE_INFO`、`FILE_READ`、`CATALOG_PAGE`、`MANIFEST_SCAN`、`SYSTEM_INIT`、`SNAPSHOT_WRITE` 和 `FAULT_EVIDENCE` job 闭环。

| 命令 | 说明 |
|---|---|
| `SYST:SD:STAT?` | 查询 SD 状态摘要：状态、card_present、fs_mounted、底层卡状态、storage_error。 |
| `SYST:SD:INFO?` | 查询 SD 卡信息：状态、卡类型、high_capacity、block_count、capacity_kib、fatfs_available、fs_mounted、probe_count。 |
| `SYST:SD:RAW:CLEAR <sectors>,"ERASE"` | 破坏性维护命令：绕过 FatFs 写零 SD 卡前 `sectors` 个 512B 扇区，当前固件最大 64；用于卡分区/FAT 元数据导致主机格式化卡死时清前缀。返回 `status,requested,cleared,raw_status,storage_error`。会破坏分区表/FAT，不用于正常 release 流程；Trigger armed 时拒绝执行。 |
| `SYST:SD:RAW:READ? <sector>` | 维护诊断命令：绕过 FatFs 读取单个 512B 扇区，并返回前 64 字节十六进制，用于确认 Pico 侧格式化或 raw 写入是否真实落卡。返回 `status,sector,raw_status,storage_error,hex64`；Trigger armed 时拒绝执行。 |
| `SYST:SD:MKFS "ERASE"` | 破坏性维护命令：由 Pico 通过 FatFs `f_mkfs` 在 SD 卡上创建 FAT/FAT32 文件系统，必须带确认字符串 `"ERASE"`。返回 `status,fatfs_status,storage_state,storage_error,block_count,capacity_kib,mkfs_result,mount_result`。只作用于 Pico 上的 SD 卡，不访问主机盘符；Trigger armed 时拒绝执行。 |
| `SYST:SD:INIT` | 非破坏性初始化命令：在已挂载 FAT32 卡上创建最小 System Pack 目录和默认文件；若 `/manifest.idx` 已存在则不覆盖。返回 `status,manifest_status,schema,build_id,required_count,missing_count,error`。不会格式化 SD 卡。 |
| `SYST:SD:MAN?` | 投递 StorageAO `MANIFEST_SCAN` job 扫描 `/manifest.idx`；若 FAT32 卡可挂载但 `/manifest.idx` 缺失，会先执行同一套非破坏性 System Pack 初始化再重新扫描。兼容返回 manifest 状态、schema、product_id、hardware_id、build_id、required_count、missing_count；Trigger armed 时拒绝执行。 |
| `SYST:STOR:STAT?` | `SYST:SD:STAT?` 的 Storage 域别名。 |
| `SYST:STOR:JOB:INFO "<path>"` | 投递 StorageAO `FILE_INFO` job，返回 `"OK",job_id`；实际 FatFs 查询在 `storage_manager_service()` 中执行。 |
| `SYST:STOR:JOB?` | 查询最近 Storage job：`state,id,type,path,size,kind,path_hash,error`；当前 type 包含 `FILE_INFO`、`FILE_READ`、`CATALOG_PAGE`、`MANIFEST_SCAN`、`SYSTEM_INIT`、`SNAPSHOT_WRITE`、`FAULT_EVIDENCE`，manifest/system-init job 的 kind 为 `MANIFEST` 且 size 为 required_count，file read job 的 kind 为 `READ` 且 size 为本次返回字节数，catalog page job 的 kind 为 `CATALOG` 且 size 为本页返回条目数，fault evidence job 的 path 指向最新 fault report。 |
| `MMEM:CAT? ["<path>"]` | 兼容诊断目录枚举；内部投递 `CATALOG_PAGE` job 包装第 0 页，最多 16 项。长目录可能不完整，可靠枚举必须使用 `MMEM:CAT:PAGE?`。非法路径如 `/../` 应返回 `PATH_DENIED`。 |
| `MMEM:CAT:PAGE? "<path>",<offset>,<limit>` | 投递 StorageAO `CATALOG_PAGE` job 分页枚举白名单路径目录，返回 `status,path,offset,returned,next_offset,complete,truncated,entries`；`limit` 固件端最大限制为 16；Trigger armed 时拒绝执行。 |
| `MMEM:INFO? "<path>"` | 投递 StorageAO `FILE_INFO` job 查询白名单路径中的单个文件或目录信息，兼容返回 `status,path,size,kind,path_hash,error`；用于长目录截断时稳定确认文件存在；Trigger armed 时拒绝执行。 |
| `MMEM:READ? "<path>",<offset>,<length>` | 投递 StorageAO `FILE_READ` job，受限读取白名单路径中的文件片段，兼容返回 `status,path,offset,requested,returned,eof,path_hash,error,hex`；`length` 固件端最大限制为 128 字节，当前用于 SD 验证工具读回 trace `.bin/.idx`；Trigger armed 时拒绝执行。 |
| `SYST:SNAP:WRIT ["boot"\|"arm"\|"fault"\|"run"]` | 投递 StorageAO `SNAPSHOT_WRITE` job 写入 snapshot JSON；省略 kind 时默认 `boot`。兼容层等待 job 完成后返回 `"OK"`。`TRIG:ARM` 内部同样通过 `SNAPSHOT_WRITE("arm")` 生成 ARM 前 snapshot；手动命令在 Trigger armed 时拒绝执行。 |
| `SYST:SNAP:LAST?` | 查询最近 snapshot 摘要：状态、kind、sequence、path、path_hash、error。 |
| `SYST:TRAC:LAST?` | 查询最近 trace 摘要：状态、kind、sequence、path、path_hash、event_count、error。 |
| `SYST:FAULT:LAST?` | 查询最近 fault report 摘要：状态、report_id、report_path、path_hash、snapshot_id、trace_id、error。 |

## OTA 维护

OTA 命令遵循 `docs/OTA方案.md` 中的 `OtaAO + OtaFB + OtaVector` 设计。SCPI 只负责解析命令、投递 OTA 事件和读取状态快照，不直接擦写 Flash，不直接修改 OTA 状态。

| 命令 | 说明 |
|---|---|
| `SYST:OTA:STAT?` | 查询 OTA 状态摘要：状态、目标 slot、错误码、最近结果。 |
| `SYST:OTA:PROG?` | 查询 OTA 进度：已接收字节、期望字节、千分比进度。 |
| `SYST:OTA:BEGIN <size>,<crc32>` | 开始 OTA 传输，`size/crc32` 对应标准 raw firmware `.bin`，接受后返回 `"OK"`。 |
| `SYST:OTA:PBEGIN <size>,<crc32>` | 开始统一 OTA package 传输，包内包含 Slot A/Slot B 两个 App 镜像；下位机根据当前 OTA 模式和 target slot 自行选择写入镜像。 |
| `SYST:OTA:DATA #<block>` | 发送 `.bin` 二进制块，投递 `OTA_EVENT_DATA_BLOCK`，为保证吞吐当前不逐块返回 ACK。 |
| `SYST:OTA:END` | 结束传输并请求校验，投递 `OTA_EVENT_END`，接受后返回 `"OK"`。 |
| `SYST:OTA:ABOR` | 中止当前 OTA，投递 `OTA_EVENT_ABORT`，接受后返回 `"OK"`。 |
| `SYST:OTA:BOOT` | 镜像 ready 后请求重启进入 pending slot，接受后返回 `"OK"` 并触发复位。 |
| `SYST:OTA:COMM` | App 自检通过后确认当前固件，写入 confirmed metadata，接受后返回 `"OK"`。 |
| `SYST:OTA:SLOT?` | 查询 `active,pending,confirmed,boot_attempts,rollback_count`。 |
| `SYST:OTA:RES?` | 查询 `app_result,app_error,boot_result,boot_source_slot,boot_size,boot_crc32`。 |
| `SYST:OTA:TXN?` | 查询 Bootloader copy transaction：`state,source,destination,size,crc32,written,attempts,last_error`。 |
| `SYST:OTA:MODE?` | 查询当前 OTA 启动模式：`"COPY_TO_ACTIVE",0` 或 `"DIRECT_AB",1`。 |
| `SYST:OTA:TARG?` | 查询下一次 OTA 写入目标 slot，当前 copy-to-active 默认返回 `2`。 |
| `SYST:OTA:CAP?` | 查询当前固件声明的 OTA 能力位，`bit0=COPY_TO_ACTIVE`，`bit1=DIRECT_AB`。 |

统一 OTA package 由 `tools/ota_packager/ota_packager.py` 生成，`tools/ota_send/ota_send.py` 会自动识别包头并发送 `SYST:OTA:PBEGIN`。package 首部固定 512 B，包含产品型号、硬件版本、App 版本、build id、payload SHA-256、最小 Bootloader 版本、每个镜像的 slot/offset/size/CRC32/run offset。payload 中 Slot A/Slot B 镜像按 512 B 对齐，保证流式写入时满足 Flash page 编程约束。设备在擦除目标 slot 前会拒绝产品型号、硬件版本和最小 Bootloader 版本不匹配的 package。

第一阶段建议 `SYST:OTA:DATA` 单块 256 B 或 512 B。OTA 期间应暂停周期日志，避免日志与 SCPI binary block 混用同一 USB CDC 通道。

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
| `SYST:OTA:INJ:COPY` | 设置下一次 Bootloader Slot B -> Slot A 复制失败注入标志。需要已烧入支持该功能的 Bootloader。 |
| `SYST:OTA:INJ:COPY?` | 查询当前 OTA 故障注入标志，`0` 表示未开启。 |
| `SYST:OTA:INJ:CLEAR` | 清除 OTA 故障注入标志。 |
| `SYST:OTA:INJ:MCOR <0|1>` | 擦除指定 metadata 副本，用于验证双副本容错。 |
| `SYST:OTA:INJ:MREP` | 从当前有效 metadata 重新写入双副本，用于恢复 metadata 冗余。 |
| `SYST:OTA:MODE <0|1>` | 切换 OTA 启动模式，`0=COPY_TO_ACTIVE`，`1=DIRECT_AB`。仅用于 direct A/B 台架验证。 |
| `SYST:BOOT:RES` | 通过 watchdog 触发系统复位。仅用于 validation 固件验证 Bootloader 回滚路径。 |

复制失败注入的期望结果：OTA payload 已进入 `READY_TO_REBOOT` 后发送 `SYST:OTA:BOOT`，Bootloader 应记录 `COPY_FAILED`，清除 pending，保留旧 App 运行，`rollback_count` 增加。

## 当前限制

- 日志和 SCPI 响应目前共用 stdio 通道，后续产品化应拆分控制通道和日志通道，或在 SCPI 会话期间关闭周期日志。
- `SAMP:RATE` 当前会直接启动采样，但尚未接入 DMA 环形缓冲。
- OTA 命令已接入 `OtaAO/OtaFB/OtaVector`，SCPI 只投递事件和读取快照，不直接调用 Flash 擦写 API。
