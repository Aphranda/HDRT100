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
| `OUTP:CLOC:FREQ <Hz>` | 设置 `GPIO22/SYNC_CLK_OUT` 输出频率。 |
| `OUTP:CLOC:FREQ?` | 查询同步时钟频率。 |
| `OUTP:CLOC:STAT ON` | 启动同步时钟输出。 |
| `OUTP:CLOC:STAT OFF` | 停止同步时钟输出。 |
| `OUTP:CLOC:STAT?` | 查询同步时钟输出状态。 |

## SEQ_STEP 编码序列步进模式

触发输入每个上升沿使编码输出步进到下一序列值。详细设计见 `docs/TRIGGER_SEQ_STEP_MODE.md`。

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
| `STAT:TRIG?` | 触发域摘要：模式、状态、seq_index、rollover_count、error_code。 |

## 状态查询

| 命令 | 说明 |
|---|---|
| `STAT:SYNC?` | 返回同步 IO 状态：初始化状态、采样状态、时钟状态、采样率、时钟频率、采样溢出计数。 |

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
