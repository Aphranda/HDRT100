# 接口域 HAOFV 架构符合性评审（2026-10-20）

Status: Active
Domain: INTERFACE
Canonical: `docs/interface/SCPI_HAOFV_REVIEW.md`
Related: `docs/interface/SCPI_COMMAND_PLAN.md`, `docs/interface/SCPI_COMMANDS.md`, `docs/interface/SCPI_HOST_ARCHITECTURE.md`, `docs/interface/SCPI_HOST_TODO.md`, `docs/interface/SCPI_HOST_TASK_PROGRESS.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-25

本文档记录对接口域（`docs/interface/` 与 `middleware/scpi_port/`）的一次 HAOFV 架构符合性评审，
用于回答“当前对外接口是否存在不符合 HAOFV 分层、owner、非阻塞与契约模型的地方”。
它是**评审记录，不是契约**：不登记 `docs/check/DOCS_REGISTRY.md`，不冻结新条款；若要把下列任何
结论升级为跨域契约，必须单独登记并走 C11 交叉审核。所有文件:行号以评审当日工作树为准。

## 1. 范围与方法

- 文档面：`docs/interface/` 下全部 `.md`（README、命令计划、命令清单、USB 设计、host 三件套、
  TASK_PROGRESS）。
- 实现面：`middleware/scpi_port`（命令表装配、分域命令模块、每个命令族的 `.c`/`.h` 运行态入口）、
  `middleware/usbtmc_scpi_port`、以及被 SCPI 直接调用的 `components/sync_io`、`drv_flash`、
  `drv_rs485`、`storage_manager` 边界。
- 方法：按 HAOFV 的“SCPI 只表达意图/配置/查询；只写 command/config slot 或读 snapshot；
  完成态由 ACK/NACK 或回读闭环；FB/AO 非阻塞”逐条对照代码；同时核对接口域文档与命令表的
  覆盖关系。
- 基线：`python tools/docs_check/docs_check.py --strict-names`（评审时 213 文件、回写后 214 文件，
  0 警告）与 `python tools/doc_regression_check.py`（43 契约、id 唯一）在评审时与回写后均为
  通过。因此下列问题
  **不是门禁失败**，而是门禁覆盖不到的语义偏差与契约漂移。

## 2. 结论

接口域主干方向符合 HAOFV：OTA 走 `ota_ao_post_event`，Trigger 走 `sync_trigger_post_event`，
校准/同步走 owner request + snapshot，TDMA/RefMem 实时路径走 intent mailbox，逻辑分析仪走
`ARCH-IOANALYZER-02` 单槽邮箱。**不符合项集中在维护/HIL 路径**：把“调用 owner API”退化成
“在 SCPI 回调里执行 owner 工作、直接碰硬件或阻塞等待”。

**强制口径**：本域不承认“维护接口可以例外”。维护命令与产品命令同受 HAOFV 约束——只能表达
意图/配置/查询，只能写 command/config slot 或读 snapshot，硬件动作由 owner 执行，完成态由
ACK/NACK 或回读闭环。`HAOFV-MAINT-20260813-008` 中“维护接口可以保留强操作”的旧表述不再
适用于接口域，以本条为准；该口径待按 C11 登记为契约。

按严重度汇总：

| 级别 | 数量 | 主题 |
|---|---|---|
| 高 | 3 | 回调内阻塞等待并驱动 StorageAO；接口层阻塞重试遮症状；维护/查询回调直接操作硬件 |
| 中 | 4 | Flash 擦写进入回调；未注册的 raw SPI 直驱残留；`FAULT:CLEar` 空实现；拒绝不可观测 |
| 文档/契约 | 4 | 命令树与文档脱节；顶层契约摘要与登记表不一致；接口无错误码区间；域文档结构与标签不一致 |
| 低 | 1 | 复位命令绕过 SystemAO |

## 3. 实现面不符合项

### 3.1 H1：SCPI 回调内同步等待 StorageAO，并直接驱动其 service

- 现象：`MMEMory:*`、`SYSTem:STORage*`、`SYSTem:REFMEM:LOAD:SD` 等命令在回调内循环等待
  storage job；上限为 10000 次 × 1 ms，即单条命令最多阻塞约 10 s。裸机路径还直接调用
  `storage_manager_service(250u)`，等于由 SCPI 任务推进 StorageAO。
- 证据：[scpi_storage_commands.c](middleware/scpi_port/src/scpi_storage_commands.c#L685-L700)
  （`SCPI_STORAGE_JOB_WAIT_LOOPS` 定义于同文件 L21）与
  [scpi_system_snapshot_commands.c](middleware/scpi_port/src/scpi_system_snapshot_commands.c#L4384-L4405)
  （调用点 L424 / L4419 / L4446）。
- 违反：接口域 canonical 规则“SCPI callback 只能解析参数、做门禁，并通过 owner API 写
  command/config slot、投递 event 或读 snapshot”
  （[SCPI_COMMAND_PLAN.md](docs/interface/SCPI_COMMAND_PLAN.md#L63-L71)）；HAOFV 的 AO 执行预算与
  “查询读快照、不触发现场硬件/长事务”模型
  （[HAOFV_ARCHITECTURE.md](docs/arch/HAOFV_ARCHITECTURE.md#L1125-L1144)）。该风险已在
  `HAOFV-MAINT-20260813-008` 记录为进行中，但当前实现仍保留同步等待。
- 影响：单条 SCPI 命令可占满接口任务约 10 s，主机看到超时、看门狗任务心跳证据变旧；同一
  StorageAO 同时被 `task_storage` 和 SCPI 任务推进，形成第二个 service 入口与重入风险。
- 建议：命令只提交 job 并立即返回 `ACCEPTED` + job id；完成态改由 `SYSTem:STORage:JOB?`、
  `MMEMory:*?` 或 ACK/NACK 闭环。禁止 SCPI 调用任何 `*_service()`。

### 3.2 H2：`SYSTem:TDMA:RING:TOPology` 在回调内阻塞重试

- 现象：设置失败时在**同一条命令内**重试 50 次、每次 `sleep_ms(20u)`（约 1 s），用于掩盖
  RING:STOP 后 owner 状态尚未收敛的瞬时拒绝。
- 证据：[scpi_system_snapshot_commands.c](middleware/scpi_port/src/scpi_system_snapshot_commands.c#L3881-L3891)。
- 违反：写命令“只表示接口层 accepted，完成/拒绝由 ACK/NACK 或回读闭环”
  （[SCPI_COMMAND_PLAN.md](docs/interface/SCPI_COMMAND_PLAN.md#L70)）与 FB/AO 非阻塞规则
  （[HAOFV_ARCHITECTURE.md](docs/arch/HAOFV_ARCHITECTURE.md#L1144)）。
- 已知状态：该处置在 [SCPI_HOST_TASK_PROGRESS.md](docs/interface/SCPI_HOST_TASK_PROGRESS.md#L46-L50)
  中已明确记为“落在接口域之外、仅遮症状”的临时处置，根因归 `IF-004`，仍未冻结。
- 建议：命令返回 `ACCEPTED`/`BUSY` 与 request sequence，由主机查询
  `SYSTem:TDMA:RING:STATus?`/ACK；禁止在接口层做等待重试。若 owner 需要 settle 窗口，
  应由 owner 发布明确状态（如 `PENDING_CONFIG`），而不是让 SCPI 退化为轮询器。

### 3.3 H3：维护/查询回调直接操作现场硬件

三类实例都属于“在回调里完成硬件动作”，而不是“提交意图后返回”：

1. `REALtime:IO:SMA:FREQuency:RX?`：查询回调内 `save_and_disable_interrupts()` +
   `busy_wait_us_32(gate_us)` + `gpio_set_function()`，结束后把引脚固定恢复为
   `GPIO_FUNC_PIO0`，而不是恢复原 owner 的 function。
   证据：[scpi_realtime_io_commands.c](middleware/scpi_port/src/scpi_realtime_io_commands.c#L282-L287)、
   [sync_io.c](components/sync_io/src/sync_io.c#L1701-L1710)。
2. `REALtime:IO:OUTPut:IMMediate`、`REALtime:IO:MODel:*`：直接置输出 mask，不经过单槽 intent
   或 owner 状态机。
   证据：[scpi_realtime_io_commands.c](middleware/scpi_port/src/scpi_realtime_io_commands.c#L212)、
   [scpi_realtime_io_commands.c](middleware/scpi_port/src/scpi_realtime_io_commands.c#L347)。
3. `COMMunication:SERial:UART#:TX:TEST`：直接调用 `drv_rs485_write_test()` 写 RS485，无 owner
   事件、无 RUN 态门禁。
   证据：[scpi_communication_uart_commands.c](middleware/scpi_port/src/scpi_communication_uart_commands.c#L147-L161)。
- 违反：接口域“SCPI callback 不得直接操作 GPIO、PIO、DMA、ADC、UART、RS485、BiSS、SD、flash
  或任何现场硬件”（[SCPI_COMMAND_PLAN.md](docs/interface/SCPI_COMMAND_PLAN.md#L67)）；HAOFV
  组件约束“SCPI/UI/System Pack 只能表达意图、配置和查询，不能直接驱动硬实时边沿”
  （[HAOFV_ARCHITECTURE.md](docs/arch/HAOFV_ARCHITECTURE.md#L120)）。
- 同域反例：逻辑分析仪 ARM/STOP 已按 `ARCH-IOANALYZER-02` 走单槽 mailbox
  （[scpi_realtime_io_commands.c](middleware/scpi_port/src/scpi_realtime_io_commands.c#L549)、
  [SYNC_IO_ARCHITECTURE.md](docs/sync/SYNC_IO_ARCHITECTURE.md#L256-L264)），说明同域存在两套边界。
- 影响：SMA 测量会改写引脚 function，绕开 Resource Arbiter/persona 所有权；关中断忙等与
  host 可控的 `gate_us` 组合会把接口任务的阻塞时间交给上位机决定；RS485 测试可在非维护态
  直接占用通信资源。
- 建议：按 analyzer 模式把这些动作收敛为“单槽 intent + Core1/owner 执行 + snapshot/ACK”。
  接口域**不设“维护强操作”例外**：维护命令与产品命令同受 HAOFV 约束，必须由 owner 执行、
  有界耗时并提供 ACK/回读；不能维持“文档全禁、代码全做”。

### 3.4 M1：Flash 擦写进入 SCPI 回调

- 现象：`SYSTem:DIAGnostic:FLASh:VALidate` 在回调内同步执行
  erase → program → 读回 → erase，单次擦除 60–300 ms；`FLASh:JEDEC?` 查询直接读驱动。
- 证据：[scpi_system_diagnostics_commands.c](middleware/scpi_port/src/scpi_system_diagnostics_commands.c#L230-L347)、
  [scpi_system_diagnostics_commands.h](middleware/scpi_port/inc/scpi_system_diagnostics_commands.h#L20-L26)。
  `PROJECT_ENABLE_FLASH_VALIDATION` 默认 OFF（[CMakeLists.txt](CMakeLists.txt#L38)），仅在
  validation 构建暴露。
- 违反：Flash 唯一 writer/异步 job 约束（`App erase/program` 仅 core0 `FlashTransactionAO`，
  必须分片、可 abort）与“禁止 SCPI 直接调用 Flash 擦写”
  （[HAOFV_ARCHITECTURE.md](docs/arch/HAOFV_ARCHITECTURE.md#L1380)）。
- 建议：保留为工厂/validation 专用也必须由 owner 异步 job 执行；build gate 只限制命令可见性，
  不能作为绕过 HAOFV 的理由；产品构建不应包含该命令。

### 3.5 M2：未注册的 raw SPI 直驱 handler 仍在接口源码中

- 现象：`SYSTem:REFMEM:SYNC:SPI:ARM/DISarm/LINE:*/RAW:TX/RAW:RX` 的 handler 仍存在（含
  physical line drive、raw transmit、默认 1000 ms 超时的 raw receive），但当前命令表中
  **没有注册**；同时 `docs/refmem` 仍记录 `SPI:ARM`/`SPI:RAW:TX` “仍可用”。
- 证据：[scpi_system_snapshot_commands.c](middleware/scpi_port/src/scpi_system_snapshot_commands.c#L1437-L1656)、
  [REFMEM_TASK_PROGRESS.md](docs/refmem/REFMEM_TASK_PROGRESS.md#L1062-L1074)。
- 影响：一是死代码/契约漂移（命令不可用但文档称可用）；二是若后续重新注册，立即构成 H3
  级违规（物理引脚直驱 + 阻塞接收）。
- 建议：删除，或改走 `SYSTem:REFMEM:SYNC:TDMA:*` 的 intent mailbox；同步修正域文档的
  “仍可用”表述。

### 3.6 M3：`SYSTem:FAULT:CLEar` 是空实现

- 现象：命令表把 `SYSTem:FAULT:CLEar` 映射到 `scpi_port_result_accepted`，只回 `1`，不投递
  任何 owner 事件；全仓没有该命令的 `FAULT_CLEAR` 生产者（`TRIG_EVENT_CLEAR_FAULT` 只有 FB
  消费端，`REFMEM_COMMAND_TYPE_FAULT_CLEAR` 无使用点）。
- 证据：[scpi_system_diagnostics_commands.h](middleware/scpi_port/inc/scpi_system_diagnostics_commands.h#L45)。
- 违反：HAOFV F1 故障必须“显式 CLEAR 事件”恢复
  （[HAOFV_ARCHITECTURE.md](docs/arch/HAOFV_ARCHITECTURE.md#L1208-L1210)），以及“accepted 后由
  ACK/NACK/回读闭环”的接口模型。
- 建议：要么接 owner 事件（并返回 accepted + 后续状态回读），要么从命令表删除，避免主机
  误判“已清除”。

### 3.7 M4：命令拒绝在主机侧不可观测（IF-004）

- 现象：回调返回 `SCPI_RES_ERR` 时发送端读不到任何应答；`scpi_port_push_exec_error` 压入的
  名称不进入错误队列，`SYSTem:ERR?` 只回通用文本。主机无法区分“处理器拒绝”和“传输超时”，
  每次拒绝付出一个完整 `serial_timeout_s`。
- 证据：[SCPI_HOST_TASK_PROGRESS.md](docs/interface/SCPI_HOST_TASK_PROGRESS.md#L34-L45)、
  [SCPI_HOST_TODO.md](docs/interface/SCPI_HOST_TODO.md#L13-L16)（IF-004，PENDING）。
- 违反：接口域“写命令返回 OK/1 只表示 accepted；动作完成、拒绝原因和质量证据必须通过
  ACK/NACK、`READ:*?` 或 `SYSTem:*?` 回读”
  （[SCPI_COMMAND_PLAN.md](docs/interface/SCPI_COMMAND_PLAN.md#L70)）。
- 建议：按 IF-004 的 (a)/(b) 选项冻结拒绝应答或队列可读名称；在新契约登记前不要依赖超时
  作为拒绝判据。

## 4. 契约与文档面问题

### 4.1 M5：命令树与接口文档脱节

- 现象（快照，非事实源）：命令表宏展开后约 826 个 pattern（2026-10-20）；按去掉 `[]`、忽略
  大小写归一化后，仅约 199 条能在 [SCPI_COMMANDS.md](docs/interface/SCPI_COMMANDS.md) 中命中、
  约 117 条能在 [SCPI_COMMAND_PLAN.md](docs/interface/SCPI_COMMAND_PLAN.md#L460-L741) 的 §10
  指令树中命中，约 556 条两份都未出现。`TRIGger:REServation:*`、`SYSTem:TDMA:*`、
  `SYSTem:VDC:*`、`SYSTem:REFMEM:SYNC:*`、`SYSTem:DIAGnostic:*` 等维护树均未进入接口域
  指令树；计划 §6.2 只登记 `SYSTem:SYNC:VDC:*`，代码另有 `SYSTem:VDC:*` 第二套维护树。
- 违反：HAOFV 把“SCPI 命令表”定义为规则表，接口域负责对外契约
  （[HAOFV_ARCHITECTURE.md](docs/arch/HAOFV_ARCHITECTURE.md#L120)、§“表驱动使用范围”）；
  当前接口域 canonical 无法覆盖实际命令面，等于契约未被接口域持有。
- 建议：由 `s_scpi_commands[]` 的宏装配生成/校验 §10 指令树；至少补齐上述命名空间与
  `SYSTem:VDC:*` 的归属决定，并新增机器检查防止再次漂移。见 `IF-009`。

### 4.2 M6：顶层契约摘要与登记表不一致

- 现象：顶层 [HAOFV_ARCHITECTURE.md](docs/arch/HAOFV_ARCHITECTURE.md#L178) 仍写
  `INTERFACE-T2ACTION-01` 为 12 项（窗口累计 7 + 动作失败 5），而
  [DOCS_REGISTRY.md](docs/check/DOCS_REGISTRY.md#L68) 的 v4 已冻结为 22 项（增加组序列 5、
  FREEZE 提前量 5、宣告提前量 2）。同时登记表没有任何 `domain=interface` 行；两条
  `INTERFACE-*` 契约的 `domain=trigger`、`clause_loc` 指向 trigger 文档。
- 影响：顶层可见性与事实源不一致；接口域无法在自己的 canonical 中持有对外 wire 契约。
- 建议：修正顶层摘要为单一事实源引用（或直接引用登记行）；明确 `INTERFACE-*` 的归属域与
  clause 落点；该动作涉及契约文本，按 C11 由非本人复核。见 `IF-009`。

### 4.3 L1：接口域没有错误码区间

- 现象：HAOFV 错误码空间只定义到 600–699
  （[HAOFV_ARCHITECTURE.md](docs/arch/HAOFV_ARCHITECTURE.md#L1040-L1049)），而 SCPI 拒绝码
  `2401/2402` 落在未冻结区间
  （[distributed_config.h](components/distributed_config/inc/distributed_config.h#L56-L57)）；
  `scpi_port_push_exec_error` 的名称又不进入错误队列。
- 影响：主机拿不到稳定、可解析的接口错误码；与“错误码和诊断事件表”原则不符。
- 建议：为接口域分配独立错误码区间（或在现有域码上做显式映射），并登记为契约。见 `IF-010`。

### 4.4 L2：域文档结构与域标签不一致

- 现象：接口域 canonical 是 [SCPI_COMMAND_PLAN.md](docs/interface/SCPI_COMMAND_PLAN.md#L1-L7)
  （`PLAN` 后缀、Domain: SCPI），另有 Draft 的
  [SCPI_HOST_ARCHITECTURE.md](docs/interface/SCPI_HOST_ARCHITECTURE.md#L1-L7)（Domain: INTERFACE）
  与 [SCPI_USB_INTERFACE_DESIGN.md](docs/interface/SCPI_USB_INTERFACE_DESIGN.md#L1-L7)
  （Domain: USB）；没有 `INTERFACE_DOMAIN_ARCHITECTURE`/`INTERFACE_TODO` 三件套；域 README
  的 canonical 表漏列 host 三件套且 `Last updated` 停在 2026-08-13。
- 影响：域 owner 不唯一，读者无法判断哪份文档定义接口域架构；契约落点分散。
- 建议：收敛为单一 canonical 架构 + TODO + Task Progress，更新 README 索引；在
  `DOCS-TRIPLETFORMAT-01` 落地时对齐。见 `IF-009`。

### 4.5 L3：复位命令绕过 SystemAO

- 现象：`SYSTem:BOOT:RESet` 与 `SYSTem:OTA:BOOT` 直接调用 `drv_watchdog_reboot()`。
- 证据：[scpi_port.c](middleware/scpi_port/src/scpi_port.c#L305-L310)、
  [scpi_ota_commands.c](middleware/scpi_port/src/scpi_ota_commands.c#L592)。
- 影响：复位原因/阶段证据由驱动直接产生，绕过 SystemAO 的复位意图与证据登记。
- 建议：改为 SystemAO 复位 intent，并保留复位原因/阶段证据；不得以“维护命令”为由绕过
  owner，也不登记为显式例外。

## 5. 符合 HAOFV 的参照实现

下列路径可作为后续修正的模板：

- OTA：全部走 `ota_ao_post_event`，不直接碰 Flash
  （[scpi_ota_commands.c](middleware/scpi_port/src/scpi_ota_commands.c#L79)）。
- Trigger：`sync_trigger_post_event` / owner snapshot，不跨核写 runtime state
  （[scpi_port.c](middleware/scpi_port/src/scpi_port.c#L229-L237)）。
- TDMA/RefMem 实时：`distributed_refmem_submit_realtime_tdma_tx/rx` intent mailbox + owner
  snapshot（[scpi_system_snapshot_commands.c](middleware/scpi_port/src/scpi_system_snapshot_commands.c#L2058-L2071)）。
- 逻辑分析仪：单槽 ARM/STOP mailbox + Core1 消费（`ARCH-IOANALYZER-02`）。
- TDMA operating profile：`SYSTem:TDMA:PERiod` 返回 `PENDING` + generation，由 owner 在
  STOP 后应用（[scpi_system_snapshot_commands.c](middleware/scpi_port/src/scpi_system_snapshot_commands.c#L3431-L3443)）。

## 6. 处置顺序与验收判据

| 顺序 | 动作 | 验收判据 |
|---|---|---|
| 1 | 修 H1/H2：移除 SCPI 内 job 等待与 `*_service()`；RING 拓扑改为 accepted/BUSY + 回读 | 静态扫描 SCPI 源码无 `*_service()`/`sleep_ms`/`busy_wait`；10 s/1 s 阻塞用例消失 |
| 2 | H3：维护强操作同样收敛为 intent mailbox + owner 执行（**无例外**） | 每个直接硬件命令都有 owner/资源 claim、有界耗时和回读证据；SCPI 源码无直接硬件调用 |
| 3 | 清理 M1–M3：Flash validation 移入 owner job；删除 raw SPI 残留；实现或删除 `FAULT:CLEar` | 命令表与源码一致；无“accepted 但无 owner 动作”的写命令 |
| 4 | 修 M4/M5/M6/L1/L2：拒绝可观测、命令树再生成、契约摘要/归属、错误码区间、域文档收敛 | 主机可在不依赖超时的前提下归因；接口域文档覆盖全部命名空间并登记契约 |

## 7. 评审基线与边界声明

- 评审日：2026-10-20。
- 检查器基线：`docs_check` 通过（回写后 214 文件、0 警告）；`doc_regression_check` 通过
  （43 契约）。
- 命令表事实源：`middleware/scpi_port/src/scpi_port.c` 的 `s_scpi_commands[]` 宏装配；
  本文出现的命令数量均为评审日快照，不作为长期事实。
- 本文不修改任何契约状态；`IF-005..IF-010` 已在
  [SCPI_HOST_TODO.md](docs/interface/SCPI_HOST_TODO.md) 建立，冻结前仍按 PENDING 处理。

## 8. 文档回写记录

| 文档 | 变更 |
|---|---|
| `docs/interface/SCPI_COMMAND_PLAN.md` | 增 §13 结论表与 §10 事实边界；明确维护接口无强操作例外 |
| `docs/interface/SCPI_COMMANDS.md` | 增“文档边界”，声明本文非全量命令表 |
| `docs/interface/SCPI_HOST_ARCHITECTURE.md` | 增评审指针与“维护强操作不设例外”口径 |
| `docs/interface/SCPI_HOST_TODO.md` | 新增 `IF-005..IF-010`，`IF-007` 标注强制收敛 |
| `docs/interface/SCPI_HOST_TASK_PROGRESS.md` | 增 `SCPI-PROGRESS-20261020-001` 与强制口径 |
| `docs/interface/README.md`、`docs/README.md` | 增评审索引、host 文档索引与边界说明 |
| `docs/arch/HAOFV_MAINTENANCE_TODO.md` | 撤销“维护接口可以保留强操作”的旧口径；维护动作同样受 HAOFV 约束 |
