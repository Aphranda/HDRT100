# HAOFV 板载 Flash 域架构

Status: Active
Domain: HAOFV / Flash / OTA / Storage
Canonical: `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/ota/OTA_SYSTEM_DESIGN.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/storage/SD_TODO.md`
Last updated: 2026-08-21

本文档是 RP2350_TRIG 板载 QSPI Flash 的跨域 canonical。它统一 Bootloader、Direct A/B、
OTA、Product Config、Calibration、System Pack、故障事件和未来 TDMA 流式 OTA 的分区、
owner、事务、掉电恢复和验证边界。SD 文件系统仍归 Storage Domain；板载 Flash 只保存
启动链、关键 KV、低频关键事件和受控 blob，不替代 SD 的高频日志与完整报告。

当前代码仍运行低容量兼容布局。本文定义的 v2 是目标架构；迁移任务、依赖和完成证据见
`docs/arch/HAOFV_FLASH_TODO.md`。在 v2 的代码符号、Bootloader 和 factory image 同时落地
之前，下面的目标布局不得被误报为已部署事实。

## 一、设计结论

1. 物理容量以产品网表中的 `U17/W25Q128JVSIQ` 和构建符号
   `PICO_FLASH_SIZE_BYTES` 为硬件事实；驱动不得保留独立的小容量上限。
2. 建立唯一 `FlashMap`，Bootloader、linker、App、factory image、工具和文档全部从同一
   机器可读定义生成或校验，不再由 OTA 组件私有拥有整片 Flash。
3. App 运行期只有 core0 `FlashTransactionAO` 可以发起 erase/program；业务 AO 只提交
   intent。Bootloader 使用最小 `BootFlashService`，不依赖 RTOS、SCPI、FatFs 或 littlefs。
4. v2 只保留 Direct A/B 的 test/confirm/revert 主线；旧 `COPY_TO_ACTIVE` 只作为迁移期
   代码清理对象，不进入 v2 的长期运行模式。
5. 关键 KV 使用 journal/NVS 语义，故障事件使用 FCB 语义，大 blob 使用 blob store；
   不允许用“擦一个固定 sector 再覆盖”保存频繁变化的配置。
6. OTA 的 USB、SD、UART 和 TDMA 只是 ingress transport。它们共用 `OtaStreamSession`、
   镜像验证和 Flash sink，不复制 OTA 状态机，也不能直接调用 Flash driver。
7. TDMA OTA 只使用已存在的 `TDMA_PAYLOAD_CLASS_OTA_BULK` 和
   `TDMA_TRAFFIC_RELIABLE_BULK`，受 `maintenance_gate_open`、credit 和背压约束；
   VDC/RefMem 硬实时窗口永远优先。
8. 完整统一 package 留在 PC、SD 或 TDMA 分发源；接收板只流式安装目标 slot image，
   不在板载 Flash 再保存一份完整 A+B package。
9. PIO program 随签名 firmware 构建并进入只读 program catalog；运行时动态装载 PIO IMEM
   不需要独立 Flash 分区，System Pack 只能选择 catalog 中已验证的 persona/program ID。
10. 不执行在线 v1 -> v2 原地搬迁。开发样板与首批产品采用可审计的 factory erase/reflash；
   Bootloader 遇到未知 map version 时 fail closed，进入恢复入口而不是猜测旧地址。

## 二、当前功能整合

| 当前能力 | 当前实现事实 | v2 归属 | 保留/替换 |
|---|---|---|---|
| 物理 Flash | `CMakeLists.txt::PICO_FLASH_SIZE_BYTES` 已按产品器件配置，`drv_flash.h` 仍有较小独立上限。 | `FlashGeometry` | 保留 SDK/XIP 驱动，删除重复容量事实。 |
| Bootloader | 可校验 Slot、选择 Direct A/B、记录 boot result，也保留 copy-to-active 分支。 | `BootFlashService + BootControlStore` | 保留已验证校验/回滚算法，v2 删除长期双模式。 |
| OTA session | `portable_ota` 已有 begin/write/end、package 解析、CRC、metadata helper。 | `OtaAO/OtaFB + OtaStreamSession` | 保留平台无关核心，Flash 操作改投递 transaction intent。 |
| USB/VISA OTA | SCPI block 传输和 host 工具已闭环。 | `UsbOtaIngress` | 作为 transport adapter 保留。 |
| SD OTA | StorageAO 可读取统一 package。 | `SdOtaIngress` | 只读 package 并喂给统一 stream，不拥有安装逻辑。 |
| TDMA 能力 | profile 已声明 OTA bulk、reliable bulk、maintenance gate 和背压。 | `TdmaOtaIngress/Egress` | 补 session、chunk、ACK、credit、resume；不新建 TDMA runtime。 |
| Product Config | 当前固定 sector 擦写，保存 USB mode 和 board number。 | `ProductConfigNVS` | schema 保留，存储机制替换为 journal/NVS。 |
| Calibration | active/staging/rollback、generation/freshness 尚未形成正式持久化。 | `CalibrationNVS` | 只保存 Calibration Domain accepted package；Flash 不解释测量语义。 |
| System Pack | 主要由 SD `packs/refs` 管理。 | `SystemPackBlobStore` | 板载只缓存必要子集/恢复最小包，SD 仍是完整包和报告宿主。 |
| Fault/trace | 高频 trace 与日志走 SD。 | `FaultEventFCB` | 板载只留低频启动/掉电/关键故障摘要。 |
| 双核安全 | `drv_flash_lockout` 已有 core1 request/ACK/poll。 | `FlashTransactionAO` 的 quiesce 阶段 | 保留机制，禁止每个调用者自行拼接 lockout。 |
| 温度诊断 | Diagnostics 已发布板温、RP2350 温度和传感器健康。 | `FlashPolicyGate` 输入 | critical/invalid 时拒绝新写事务；阈值仍由 board policy 定义。 |

## 三、借鉴成熟项目的取舍

| 来源 | 借鉴 | 本项目不照搬 |
|---|---|---|
| MCUboot | signed image/TLV、security counter、test/confirm/revert、镜像与传输解耦。 | 不直接引入其完整 trailer/swap 机制；RP2350 v2 使用 Direct A/B 和本项目 Bootloader。 |
| Zephyr flash map | fixed partition ID、area 边界、统一 open/read/write/erase 语义。 | 不引入 Zephyr device model；用轻量静态表和生成校验。 |
| Zephyr NVS | append-only KV、CRC、掉电恢复、sector rotation 和 wear leveling。 | 不把大 package 或日志塞进 KV。 |
| Zephyr FCB | 适合顺序追加、循环回收的低频事件记录。 | 高频 trace 继续写 SD，不用片上 Flash 承担采样流。 |
| littlefs | 掉电恢复、copy-on-write、适合小型 blob/目录。 | BCB、anti-rollback、校准 critical KV 不用普通文件覆盖；首阶段可先做定长 blob store。 |
| ESP-IDF OTA | 冗余 boot control、序号选择、新镜像确认前自动回滚。 | 不复制 ESP32 partition/API。 |
| OpenHarmony DSoftBus | capability、network identity、session、lane/QoS、统一 bytes/stream 传输语义。 | 不引入动态发现、动态路由、IPC 或链路无感切换；工业环网继续使用静态拓扑和确定性 TDMA。 |

## 四、HAOFV 分层与 owner

```text
SCPI / UI / SD / TDMA / factory tool
              |
              v intent only
OtaAO / ConfigAO / CalibrationAO / DiagnosticsAO
              |
              v bounded command + immutable data provider
FlashTransactionAO (core0, App 唯一 erase/program owner)
  FlashTransactionFB
    VALIDATE -> QUIESCE -> ACQUIRE -> PARK_CORE1
    -> ERASE/PROGRAM -> READBACK_VERIFY -> COMMIT
    -> RELEASE -> COMPLETE/FAILED
              |
              +--> BootControlStore
              +--> FlashNVS
              +--> FlashBlobStore
              +--> FlashFCB
              |
              v
FlashMap -> Raw Flash HAL -> RP2350 SDK / XIP

Boot context (无 RTOS):
Bootloader -> BootFlashService -> FlashMap + BCB + ImageVerifier -> Raw Flash HAL
```

### 4.1 唯一 writer

| 事实/动作 | writer | reader | 禁止 |
|---|---|---|---|
| App erase/program | core0 `FlashTransactionAO` | 各业务 AO 读 completion/vector | OTA、Config、Calibration、SCPI 直接调用 `drv_flash_erase/program`。 |
| Boot erase/program | `BootFlashService` | Bootloader state machine | Bootloader 链接 RTOS、SCPI、FatFs、TDMA 或 App AO。 |
| Flash map | 生成后的 `flash_map` 静态定义 | linker、Boot、App、tool、release gate | 在 CMake、linker、头文件和脚本重复手写 offset。 |
| BCB | Bootloader 与 App 通过 `BootControlStore` 的互斥状态转换写入 | Boot/OTA/Diagnostics | 原地覆盖最后有效副本。 |
| Product Config | `ProductConfigAO` 产生 record，`FlashNVS` 持久化 | System/USB/board identity | 固定 sector 每次整擦。 |
| Calibration | Calibration Domain 发布 accepted package，`FlashNVS` 持久化 | VDC/TDMA/Diagnostics | Flash 层生成 delay/bias 或接受 diagnostic-only evidence。 |
| OTA stream state | `OtaAO/OtaStreamSession` | transport adapters/Diagnostics | TDMA adapter 改 pending slot 或写 BCB。 |
| TDMA schedule/gate | `TdmaSchedulerAO`/core1 | OTA producer 读取 completion/credit | OtaAO 直接打开 maintenance gate 或占 PIO/DMA。 |

### 4.2 FlashTransactionVector

Vector 至少发布以下字段块；由 core0 owner 写，多字段快照使用 seqlock：

- `state/job_id/requester/partition_id/operation`
- `requested_bytes/processed_bytes/verified_bytes`
- `map_version/store_generation/transaction_generation`
- `last_result/last_error/retry_count/abort_pending`
- `lockout_request_seq/lockout_ack_seq/lockout_timeout_count`
- `erase_count_delta/program_count_delta/verify_failure_count`
- `temperature_flags/policy_gate_reason`
- `started_timestamp_ms/completed_timestamp_ms`

查询命令只读该 Vector，不触发 XIP 扫描、CRC 全盘计算或 Flash 现场访问。

### 4.3 FlashTransactionFB 规则

- 每个 FB action 只推进一个有界步骤并返回；不得在事件 callback 内完成整槽 erase。
- erase/program 前依次检查 system mode、partition permission、range/alignment、温度策略、
  active image 保护、资源锁和 core1 park ACK。
- erase/program 的 RAM resident 临界区必须包含 SDK 调用所需代码、数据和 lockout loop；
  临界区禁止 USB、SD、LCD、格式化日志和任何 XIP 间接调用。
- `abort` 只设置 `abort_pending`；当前不可中断 page/sector 操作完成并验证后再退出。
- completion 必须包含 durable 边界。业务域只有收到 durable completion 才能发布“已保存”或
  向 TDMA sender 返回持久化 ACK。

## 五、FlashMap v2

### 5.1 分区原则

- 所有区域按 erase sector 对齐；可启动镜像还必须满足 linker/XIP 对齐和向量表约束。
- Bootloader、Boot Control、A、B、Recovery、OTA Cache、Resume Journal、关键 NVS、blob、
  FCB 和 scratch 分开，避免一个域的 GC 擦除另一个域的事实。
- A/B 等长，分别构建 slot-specific XIP 镜像；统一 package 可以携带两个目标镜像。
- Recovery 是独立签名镜像，不等同于 Slot A 的备份，也不被普通 OTA 自动覆盖。
- OTA Stage 只保存 manifest、小型 chunk/checkpoint spill 或未来受验证 delta，不缓存完整双镜像
  package。USB/SD/TDMA 从统一 package 中选择目标 slot object，直接事务化写 inactive slot。
- Scratch 仅由 FlashTransactionAO/GC/验证工具临时租用，不向普通业务域公开固定地址。

### 5.2 目标布局快照

下表是 `ARCH-FLASHMAP-01` 的 v2 目标快照，当前代码尚未实现，不能作为已烧录事实。
实现阶段必须把每行生成成 `FLASH_MAP_*` 符号，并由工具验证表尾等于
`PICO_FLASH_SIZE_BYTES`；从那一刻起代码符号取代本文数字成为唯一数值事实源。

| Partition ID | Offset snapshot | Size snapshot | 目标符号 | owner / purpose |
|---|---:|---:|---|---|
| `BOOTLOADER` | `0x000000` | 256 KiB | `FLASH_MAP_BOOTLOADER_*` | immutable Bootloader。 |
| `BOOT_CONTROL` | `0x040000` | 256 KiB | `FLASH_MAP_BOOT_CONTROL_*` | BCB 双 lane、map manifest、security counter。 |
| `APP_A` | `0x080000` | 2 MiB | `FLASH_MAP_APP_A_*` | Direct A/B slot A。 |
| `APP_B` | `0x280000` | 2 MiB | `FLASH_MAP_APP_B_*` | Direct A/B slot B。 |
| `RECOVERY` | `0x480000` | 1 MiB | `FLASH_MAP_RECOVERY_*` | 最小独立签名 recovery image，单独 size gate。 |
| `OTA_STAGE` | `0x580000` | 1 MiB | `FLASH_MAP_OTA_STAGE_*` | manifest/chunk spill/delta staging，不缓存完整 A+B package。 |
| `OTA_JOURNAL` | `0x680000` | 256 KiB | `FLASH_MAP_OTA_JOURNAL_*` | stream resume、transaction journal。 |
| `PRODUCT_NVS` | `0x6C0000` | 256 KiB | `FLASH_MAP_PRODUCT_NVS_*` | board identity、USB mode、产品 KV。 |
| `CALIBRATION_NVS` | `0x700000` | 1 MiB | `FLASH_MAP_CALIBRATION_NVS_*` | accepted/staging/rollback link/IO calibration records。 |
| `VDC_NVS` | `0x800000` | 256 KiB | `FLASH_MAP_VDC_NVS_*` | servo/holdover/discipline profile，禁止保存 runtime lock。 |
| `SYSTEM_PACK` | `0x840000` | 2 MiB | `FLASH_MAP_SYSTEM_PACK_*` | active Deployment Capsule、RefMem package、小型 blob。 |
| `FAULT_FCB` | `0xA40000` | 1 MiB | `FLASH_MAP_FAULT_FCB_*` | boot/power/critical fault 循环事件。 |
| `SCRATCH` | `0xB40000` | 1 MiB | `FLASH_MAP_SCRATCH_*` | GC、恢复和 validation lease。 |
| `FUTURE_POOL` | `0xC40000` | 3.75 MiB | `FLASH_MAP_FUTURE_POOL_*` | 未分配容量；只能通过新 map version 分配。 |

当前 App 镜像约数百 KiB 是 2026-08-21 的构建快照，不是容量事实源。v2 单槽增大主要为
RTOS、协议、诊断、签名库和后续功能留余量；release gate 仍应以链接产物和符号阈值计算，
而不是在文档写死百分比。

该平衡版快照中，Boot/BCB/A/B/Recovery/OTA Stage/Journal 合计约占物理容量四成，且其中
A/B 是产品必需的可启动冗余，不等同于“OTA 缓存”。`FUTURE_POOL` 不允许被普通 store 当作
自动扩容空间；只有新 map version、兼容性评审和 factory/recovery 迁移完成后才能分配。

### 5.3 权限矩阵

| Partition | Boot read | Boot write | App read | App write | factory write |
|---|---|---|---|---|---|
| Bootloader | yes | no | metadata only | no | signed factory flow only |
| Boot Control | yes | constrained | snapshot | via store intent | yes |
| Active App | yes | no | XIP | no | yes |
| Inactive App | yes | no | verify | OTA intent only | yes |
| Recovery | yes | recovery policy only | version/hash query | release service only | yes |
| OTA Stage/Journal | manifest/resume read | no | OTA owner | OTA owner intent | yes |
| NVS/blob/FCB | minimal recovery read | no | store API | partition-specific intent | yes |
| Scratch | recovery only | recovery only | no direct address | transaction lease only | yes |

## 六、Boot Control 与镜像链

### 6.1 BCB

Boot Control Block 使用至少两个独立 erase lane。record 采用 append/commit 语义，包含：

- `magic/schema/map_version/record_length`
- `sequence/boot_generation/security_counter`
- `active_slot/pending_slot/confirmed_slot/previous_slot`
- `attempt_count/rollback_count/last_boot_result/reset_reason`
- 每个 bootable image 的 `manifest_hash/image_hash/image_size/version/state`
- `record_crc/commit_marker`

写入顺序固定为 body -> readback verify -> commit marker。选择规则为 schema/map 合法、CRC 和
commit marker 有效、sequence 最新；不得仅凭 sequence 选择结构损坏记录。GC 必须先在另一
lane 写入并 seal 新基线，再回收旧 lane。

### 6.2 Image Manifest v2

镜像 manifest 至少覆盖 product/hardware compatibility、slot/link address、image size、
entry/vector 范围、semantic version、build identity、hash、signature、key ID、security
counter 和 extensible TLV。CRC 只用于传输/介质误码，不能替代签名和 anti-rollback。

### 6.3 启动状态机

```text
load geometry/map -> select valid BCB -> verify pending/confirmed image
  pending valid and attempts available -> TEST_BOOT(pending)
  App self-test + explicit confirm       -> CONFIRMED
  reset/no confirm/verification failure  -> REVERT(previous confirmed)
  A and B unusable                       -> verify and boot RECOVERY
  map/BCB/recovery all unusable          -> ROM BOOTSEL/factory recovery indication
```

Bootloader 不扫描普通 NVS、littlefs 或 SD，不依赖 TDMA。Recovery image 负责较复杂的 USB/
SD 恢复 UI；其权限仍受签名和 anti-rollback 约束。

## 七、存储抽象

| Store | 数据类型 | 事务模型 | 典型消费者 |
|---|---|---|---|
| `BootControlStore` | 小、极关键、低频 | redundant append + commit marker | Bootloader/OtaAO |
| `FlashNVS` | 小 KV、版本化 record | append + CRC + sector rotation | ProductConfig/Calibration |
| `FlashBlobStore` | package、System Pack、Recovery blob | immutable object + manifest + atomic ref | OTA/Storage |
| `FlashFCB` | 低频顺序事件 | append ring + sector GC | Diagnostics/System |

所有 record 必须有 schema、length、generation、CRC 和 commit 状态。reader 必须跳过 torn、
unknown-required-schema 和未 commit record。GC 是 FlashTransactionAO 的 job，不在业务 AO
callback 内运行。

Calibration NVS 的 key 使用 unique board ID、directed link endpoints、topology CRC、profile
CRC 和 calibration generation；NO/USB 枚举顺序不能成为主键。只有 Calibration Domain 的
accepted package 可变为 active，diagnostic-only evidence 只能进 SD report 或 debug blob。

### 7.1 HAOFV 各主域持久化矩阵

Flash 只承接跨重启仍成立、能够独立验证、写频率有界的事实。Domain Vector 的 live snapshot、
AO/FB ECC 状态、命令队列和硬实时数据面均不得因为“方便恢复”而持久化。各域也不拥有私有
erase/program 入口；它们只向对应 store 提交 versioned record/blob intent，并在 durable
completion 后更新自己的“已保存”摘要。

| HAOFV 主域 | 应进入板载 Flash 的事实 | namespace / store | 不得恢复为 live fact 或应留在 SD/RAM |
|---|---|---|---|
| System / Product | board identity、USB mode、hardware capability、权限与安全 policy、active Deployment Capsule ref。 | `PRODUCT_NVS` + `SYSTEM_PACK` | 当前 mode、resource lock、command slot、任务/队列状态。 |
| Trigger / Loop | named sequence/recipe/mission、safe limit、active profile/reference；参数作为 Deployment Capsule 的受版本对象。 | `SYSTEM_PACK`，少量产品偏好进 `PRODUCT_NVS` | ARM/RUN/PAUSE 状态、sequence cursor、live queue、deadline、breakpoint；任务历史和完整报告进 SD。 |
| SYNC_IO / PIO / DMA | board IO profile、允许的 persona/program catalog ID、resource claim 和 compatibility。 | 签名 App 中只读 `PioProgramCatalog` + `SYSTEM_PACK` 选择项 | PIO IMEM 当前内容、SM PC/FIFO、DMA descriptor/remaining count、IRQ/live persona 状态。 |
| Calibration | accepted/staging/previous package、active generation/ref、board/link/topology/profile binding、delay/bias/residence 与质量摘要。 | `CALIBRATION_NVS` | diagnostic-only raw capture 不得激活；完整校准 evidence/report 进 SD。 |
| TDMA Foundation | foundation/operating/process-image profile、payload whitelist、traffic budget、adapter/resource claim；作为部署配置。 | `SYSTEM_PACK` | ring running/armed、window cursor、sequence/counter、FIFO、in-flight frame、maintenance gate live state。 |
| Distributed RefMem | `.rmtp`、ApplicationMap、BoardCapability、NodeLoad、FB/Event/DataLink tables、active/previous package ref。 | `SYSTEM_PACK` / `FlashBlobStore` | 64 KiB live vector、dirty、command/ACK、heartbeat、stale、peer online、transport in-flight、epoch。 |
| VDC / DPLL | servo、holdover、reference priority、timestamp dictionary、accepted aging/temperature/wander profile、Calibration ref。 | `VDC_NVS` + `SYSTEM_PACK` baseline | offset/rate/phase error/DCO、LOCK/HOLDOVER、map generation、sample ring；每次上电重新锁相。 |
| Communication adapters | BiSS/UART/RS485/USB/TDMA adapter profile、addressing/role、accepted static latency/calibration ref。 | `SYSTEM_PACK`；实测 offset 进 `CALIBRATION_NVS` | RX/TX FIFO、counter、active transaction、retry/backoff、last frame 和连接在线状态。 |
| Measure / Capture | channel/transducer profile、单位/量程、trigger/compression policy、calibration ref。 | `SYSTEM_PACK` + `CALIBRATION_NVS` 引用 | 原始 ADC/edge/waveform 流、capture ring、批量结果；完整数据与报告进 SD。 |
| OTA / Boot | BCB、image manifest/hash/signature/security counter、pending/confirmed/previous slot、durable resume journal。 | `BOOT_CONTROL` + inactive App/Recovery + `OTA_JOURNAL` | transport RX queue 和未经 durable checkpoint 的 offset；完整 A+B package 留在 source/SD。 |
| Storage / Deployment | active/previous Deployment Capsule、最小离线运行对象和 atomic refs。 | `SYSTEM_PACK` / `FlashBlobStore` | SD 完整 pack/history、用户文件、波形、截图、报表和高频日志不得复制进板载 Flash。 |
| Diagnostics / Fault | boot/reset/power/critical fault、Flash health、关键传感器超限摘要和关联 generation。 | `FAULT_FCB` | 周期温度/电流采样、TDMA/Trigger 高频 trace、完整 crash/evidence；持续流写 SD。 |
| UI / Local preference | 产品确需跨重启的亮度、语言、显示策略等低频偏好，且必须有 schema/default。 | `PRODUCT_NVS` | 当前页面、光标、按键、动画、临时提示和未确认编辑值。 |

这个矩阵不为每个域新建分区。System Pack 是静态部署与参数对象的共同容器，Calibration/VDC
使用独立 NVS 是因为 acceptance、freshness 和 rollback 语义不同；Boot/OTA、产品身份与故障
事件则分别使用 BCB、Product NVS 和 FCB。只有 endurance、权限、掉电原子性或 recovery
依赖确实不同，才允许在下一版 map 中申请新 partition。

### 7.2 RefMem 持久化边界

RefMem 的 64 KB `DistributedVectorTable` 是运行期共同事实，不是 Flash 镜像。Flash 只保存
部署输入和引用：

| 可持久化 | Store | 上电动作 |
|---|---|---|
| `.rmtp` package、ApplicationMap、NodeLoad、FB/Event/DataLink、BoardCapability 等静态表 | `SYSTEM_PACK` blob | 读入 staging，完成 CRC/schema/owner/resource/DeploymentGate 校验后激活。 |
| active/previous deployment ref、package hash/CRC、schema 和 generation | Blob atomic ref / NVS summary | 只选择候选 package，不直接恢复 live table。 |
| board unique identity 与基础 capability | `PRODUCT_NVS` | 作为 claim/compatibility 输入，不等于 active slot。 |
| load/activation/rollback 失败摘要 | `FAULT_FCB` + SD report | 供诊断，不驱动 active fact。 |

禁止持久化后直接恢复为有效运行事实的字段包括 live vector payload、dirty mask、slot/field
sequence、command slot、ACK/NACK、heartbeat、stale、peer online、transport in-flight、epoch
和 RUN completion。每次上电/部署激活都建立新 epoch；peer mirror 在重新 HELLO/EPOCH/full or
delta sync 之前保持 stale。Flash/Storage 不能绕过 `DistributedRefMemAO` 直接写 active image。

### 7.3 VDC/DPLL 持久化边界

VDC 的 Flash namespace 独立于 Calibration record。Calibration NVS 保存链路 delay/bias 的
accepted source facts；VDC NVS 保存如何使用观测形成共同时间的低频 profile：

| 可持久化 | 条件 | 上电动作 |
|---|---|---|
| `VdcServoProfile`、`VdcHoldoverPolicy`、reference priority | signed System Pack 或 accepted NVS profile | schema/sanity/profile CRC 校验后作为 CHECKING 输入。 |
| `VdcTimestampDictionary` | 与 board/topology/profile compatibility 绑定 | 校验 source/resolution 映射，不声明 sample eligible。 |
| aging/temperature compensation、wander/error-bound model | 长窗口统计形成 candidate，经维护态显式 accept | 装入 staging/active discipline profile，重新锁相验证。 |
| active calibration reference/CRC | 指向 Calibration NVS accepted generation | 只建立 binding；calibration 缺失/过期则拒绝锁相。 |
| servo/holdover/relock 质量报告 | `FAULT_FCB` 摘要 + SD 完整报告 | 仅诊断/验收。 |

严禁把 `offset/rate/phase_error/DCO control/lock_state/HOLDOVER age/map generation/sample ring`
保存后在下次启动直接恢复为 `LOCKED`。这些字段绑定上一次供电、温度、reference、topology、
calibration 和实时观测，掉电即失去 freshness。启动固定从 `OFF/CHECKING` 开始，经 active
calibration、TDMA schedule、timestamp dictionary、initial sync 和 DPLL quality gate 后才能
重新发布共同时间。

## 八、统一 OTA stream 与 TDMA 扩展

### 8.1 transport-neutral session

```text
UsbOtaIngress / SdOtaIngress / UartOtaIngress / TdmaOtaIngress
                         |
                         v
OtaStreamSession
  OPEN -> RECEIVE -> DURABLE_ACK -> VERIFY_PACKAGE -> INSTALL_INACTIVE
  -> MARK_PENDING -> READY_TO_REBOOT
                         |
                         v
FlashTransactionAO / BootControlStore
```

session 绑定 source identity、target identity/capability、package identity、manifest hash、
session generation、total size 和 destination policy。不同 transport 可以在新 session 中续传，
但同一 session 不允许无证据地切 lane 或改变 package identity。

### 8.2 对 DSoftBus 理念的受控映射

| DSoftBus 理念 | 本项目映射 | 确定性约束 |
|---|---|---|
| Bus Center / node identity | Calibration accepted topology + board unique ID + capability snapshot | 不做动态发现；身份变化使 session freshness 失效。 |
| capability publish/discover | `OTA_RECEIVER_V2`、map/image/security capability | 由 RefMem/TDMA config control 发布，只读静态 capability。 |
| Session/Socket | `OtaStreamSession` | 有界静态 session 数；显式 open/close/abort。 |
| Lane/QoS | transport 在 session open 前选择；TDMA 映射 reliable bulk | session 中不动态换路；VDC/RefMem 窗口优先。 |
| bytes/stream/file | 统一 chunk + package manifest | 不传裸任意文件路径；目标是受验证 package/object。 |

### 8.3 TDMA wire 语义

目标控制消息为 `OPEN/OPEN_ACK/RESUME_QUERY/DATA/ACK/CLOSE/CLOSE_ACK/ABORT/STATUS`。
每帧至少携带 wire version、session ID、generation、source/target identity、sequence、offset、
payload length、flags 和 CRC。OPEN 绑定 package manifest hash；DATA 不重复解释 OTA manifest。

ACK 是 durable cumulative ACK：对应范围已经由 receiver 写入目标 store 并 readback 验证。
receiver 同时返回 credit；sender 的 outstanding 数据不得超过 credit。RAM queue 接收成功不等于
durable ACK。重复 DATA 按 session/offset/hash 幂等处理；冲突数据必须 fail closed 并终止 session。

resume journal 只按受控 checkpoint 频率持久化，不能每个 TDMA frame 擦写。复位后 receiver
从 journal 和 Flash readback 重建 durable offset，再返回 resume token。token 必须绑定 package
hash、map version、target partition 和 session generation。

统一 package v2 应把 manifest 与 slot-specific image object 分开索引。分发源保留完整包，
根据 receiver 当前 active slot 和 capability 只发送对应 inactive slot object。receiver 在 OPEN
阶段先验证 manifest/object identity，再直接写 inactive slot；无关 slot object 不经过 TDMA，
也不落入板载 OTA Stage。

### 8.4 多板滚动 OTA

- 分发协调属于 OTA Domain 的 `OtaDistributionFB`，TDMA 只传输和报告 completion。
- 同一 package 可以按 target bitmap/cohort 分发，每个节点独立维护 durable offset、验证和
  pending 状态；不能用一个节点 ACK 代表整组成功。
- 先 stage/verify 所有目标，再执行显式 commit policy。对 reference 节点升级前必须完成角色
  迁移和 topology/profile generation 更新；不得在 reference reboot 时假装 ring 仍锁定。
- 任一节点 map/security/capability 不兼容时从 cohort 排除并报告原因，不允许为追求“全成功”
  降低签名、anti-rollback 或 Flash 门禁。
- RUN/CAL 中 gate 关闭时 sender 保持 session 并接受零 credit；不得挤占 VDC、RefMem、T2 或
  Calibration 窗口，也不得通过提高动态优先级绕过 scheduler。

### 8.5 动态 PIO program

PIO instruction memory 的运行时装载不等于 Flash 动态模块加载。当前目标模型为：

```text
signed App image
  -> PioProgramCatalog(program_id/version/hash/instruction_count/resource_claim)
  -> System Pack selects allowed persona/program_id
  -> PIO owner stops SM/DMA, validates claim, loads program, publishes snapshot
```

- `.pio` 源码生成的程序属于 A/B firmware image，受同一 image hash/signature/rollback 保护。
- catalog 声明 PIO block、instruction count、side-set/pin/DMA/resource compatibility；
  DeploymentGate 在激活 System Pack 前验证，不能等到 RUN 中才发现 instruction memory 冲突。
- System Pack/RefMem 只传播 program/persona ID、version、resource claim 和 active evidence，
  不传播任意 PIO 指令字。
- persona 切换仍由对应 PIO owner 在 SM/DMA 停止和安全 IO 状态下执行；FlashTransactionAO
  不参与每次切换，因为程序已经是只读 firmware 的一部分。
- 若未来确需独立更新 PIO bytecode，必须新增 signed executable-object manifest、ABI、静态
  verifier、资源/IO sandbox 和 rollback 契约；在此之前 BlobStore 拒绝 executable object。

## 九、模式、温度与资源门禁

| 条件 | read | new erase/program | 当前不可中断操作 |
|---|---|---|---|
| BOOT | map/BCB/image verify | BootFlashService constrained | 完成当前 Boot record 原子步骤 |
| MAINTENANCE/OTA | yes | policy 允许 | 分片完成后可 abort |
| RUN | bounded XIP/read | no | 完成当前 page/sector 后关闭 gate |
| CALIBRATION training | evidence read only | no | 完成当前 page/sector 后报告 policy violation |
| thermal warning | yes | policy 可降速/暂停 | 完成原子步骤 |
| thermal critical/sensor invalid | yes | no | 完成原子步骤并 fail closed |
| FAULT | diagnostics read | 仅明确 recovery policy | 不开始普通 GC/OTA/config write |

资源申请顺序固定，避免锁顺序反转：system mode token -> partition lease -> Flash resource ->
core1 park -> raw operation。TDMA/SD/USB transport 资源在向 FlashTransactionAO 提交 immutable
buffer 后应尽快释放，不能在等待 sector erase 时长期持有传输资源。

## 十、诊断与 SCPI 投影

建议维护命令：

```text
SYSTem:FLASH:INFO?             # geometry/map/version/capability snapshot
SYSTem:FLASH:MAP? [partition]  # partition permission and bounds snapshot
SYSTem:FLASH:JOB?              # FlashTransactionVector
SYSTem:FLASH:STORE?            # NVS/blob/FCB generation and GC state
SYSTem:FLASH:WEAR?             # erase high-watermark and bad/torn counters
SYSTem:OTA:STREAM:STATus?      # session/durable offset/credit/transport
SYSTem:OTA:STREAM:RESume?      # read-only resume token summary
```

release 固件不提供任意 offset erase/program 命令。validation 固件的 destructive HIL 命令必须
编译隔离、限制在 scratch/test lease，并通过 release gate 确认命令字符串不存在。

## 十一、验证门禁

1. 静态 map：所有分区对齐、不重叠、表尾匹配物理容量；linker/factory/tool 地址由同源生成。
2. owner：App 中除 FlashTransactionAO/Boot adapter 外，扫描不到裸 erase/program 调用。
3. 双核：每次 write 都有 core1 park ACK；timeout 不执行 raw operation；恢复后 core1 alive。
4. 掉电：BCB、NVS、blob ref、FCB、OTA resume 的每个 commit 边界均做断电/复位注入。
5. 启动：A->B、B->A、未确认回滚、A/B 损坏进 Recovery、BCB 双 lane 损坏 fail closed。
6. 高地址：在 scratch/test lease 验证超过旧兼容边界的 erase/program/readback，不碰 boot/image/NVS。
7. stream：乱序、重复、丢帧、CRC 错、credit=0、断点续传、identity/generation/hash mismatch。
8. TDMA：OTA bulk 不能造成 VDC/RefMem deadline miss、window overrun 或 calibration evidence 污染。
9. wear：循环配置/校准/事件写入并验证 sector rotation、GC、寿命计数和最后有效记录保留。
10. release：签名、anti-rollback、factory image、SBOM/key policy、map manifest 和报告归档齐全。

## 十二、未来产品应用映射

`docs/arch/ARCH_FUTURE_APPLICATION_PLAN.md` 中的应用不会共享一套任意读写目录，而是复用
稳定的 Store API 和 manifest/schema。RP2350 的 16 MiB v2 只是 reference map；STM32H7、
i.MX RT、Linux/PRU、FPGA/Zynq 等平台可以使用不同 geometry 和 offset，但必须保持语义
Partition ID、事务 completion、image trust 和 store record 契约。

| 未来场景 | 板载 Flash 保存 | RAM/SD/外部数据面保存 | 关键门禁 |
|---|---|---|---|
| 分布式仪表 | 仪器 identity、能力、校准证书摘要、active deployment ref。 | 完整校准报告、波形、截图、长日志。 | 校准证书签名/schema 与硬件 identity 绑定。 |
| 运动控制 | 轴 identity、安全限制、零点/传感器校准、已验证 active trajectory ref。 | 实时轨迹 buffer、历史曲线、完整任务库。 | Flash 不进入 servo loop；安全限制更新需维护模式和权限。 |
| 电机控制 | 功率级 profile、传感器/电机参数、保护阈值、固件/算法版本。 | 高频 ADC/PWM trace、示波记录。 | 参数必须有 hardware compatibility 和 rollback。 |
| 分布式测量/DAQ | 通道 profile、timestamp dictionary schema、压缩/触发配置、故障摘要。 | 原始采样流、批量数据、完整 evidence/report。 | 数据流背压不能诱发实时 Flash 写。 |
| ATE/产测 | 工站 identity、fixture calibration、权限/许可、active recipe ref、任务断点摘要。 | 批次报告、产品序列明细、附件和审计日志。 | 断点与 recipe/package generation 绑定，禁止跨批次误续。 |
| 分布式 RF | 阵列/通道校准摘要、active beam/scan profile ref、security counter。 | 大型阵列表、扫描数据、完整质量报告。 | active ref 原子切换，旧固件拒绝未知 required schema。 |
| 开源 reference | 示例 System Pack、capability manifest、兼容性 golden vectors。 | simulator/visualizer 数据集和构建产物。 | 开发 key 与 release key/产品机密完全分离。 |

### 12.1 Deployment Capsule

板载 `SYSTEM_PACK` 保存“当前节点启动/离线运行所需的最小已验证子集”，称为 Deployment
Capsule。它可以包含 ApplicationMap、role/persona 选择、profile、mission 索引、calibration
引用、capability requirements 和 schema，但不保存完整历史库。

当前安全模型下，role/persona/AO/FB 的原生代码静态编译进签名 firmware；Deployment
Capsule 只选择允许的实现和参数。不得从普通 System Pack blob 动态加载任意机器码。若未来
需要可执行插件，必须另立 signed module ABI、MPU/权限、资源 claim、回滚和安全评审契约，
不能把数据 blob loader 直接扩成代码 loader。

### 12.2 容量与平台 profile

- reference map 的 System Pack 区只承载 active capsule；完整 pack/history 继续使用 SD。
- 大型轨迹、波形、阵列表或模型超出 active capsule 预算时，必须走 SD/外部存储或下一版
  geometry profile，不能挤占 BCB、Calibration、Fault 或 OTA 安全边界。
- map manifest 声明 geometry profile 和 store capability。System Pack 在 checkout 前验证
  required capacity/capability；不足时 fail closed，不做运行时隐式重分区。
- 跨平台 portable 层保持 `FlashTransactionPort/BootControlStore/FlashNVS/BlobStore/FCB`
  契约，平台 port 提供 alignment、erase/program、cache/XIP/coherency 和 protection 能力。

## 十三、明确不做

- 不在本轮引入完整 OpenHarmony DSoftBus、网络发现、IPC 或动态路由。
- 不让 littlefs/NVS/FCB 成为 Bootloader 依赖。
- 不把高频采样、完整 trace、文本日志或用户任意文件长期写入片上 Flash。
- 不让 OTA/Calibration/Config 为方便而绕过 FlashTransactionAO。
- 不兼容运行时自动识别并迁移旧分区；迁移由 factory 工具和可恢复步骤完成。
- 不在没有签名/anti-rollback 设计闭环前把 TDMA 多板 OTA称为量产安全升级。
- 不让未来应用把 System Pack 数据 blob 当作未经隔离的动态原生插件执行。
