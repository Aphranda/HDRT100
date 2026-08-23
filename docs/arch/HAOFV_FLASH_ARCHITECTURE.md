# HAOFV 板载 Flash 域架构

Status: Active
Domain: HAOFV / Flash / OTA / Storage
Canonical: `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/HAOFV_FLASH_TODO.md`, `docs/ota/OTA_SYSTEM_DESIGN.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/storage/SD_TODO.md`
Last updated: 2026-08-24

本文档是 RP2350_TRIG 板载 QSPI Flash 的跨域 canonical。它定义 FlashMap、App/Boot writer、
掉电事务、Boot/Direct A/B、OTA、关键配置、Calibration、VDC、System Pack、RefMem package、
故障事件和动态 PIO catalog 的边界。实施顺序、任务状态和完成证据统一在
`docs/arch/HAOFV_FLASH_TODO.md` 跟踪。

## 一、阅读边界与事实优先级

### 1.0 三份 Flash 文档的接口

- **架构文档（本文）**：只定义跨域稳定语义、owner 边界、状态机、不变量和契约落点；不记录
  单次构建号、板端日志或临时完成判断。
- **实施 TODO（`HAOFV_FLASH_TODO.md`）**：只定义里程碑、子项、`[ ]/[~]/[x]/[!]` 状态、进入/退出
  条件和证据索引；不复制实施快照，不替代架构契约。
- **任务进度（`HAOFV_FLASH_TASK_PROGRESS.md`）**：只记录已发生的代码提交、构建/HIL、报告路径、
  失败和阻塞；不得自行冻结契约或把一次性证据写成架构事实。

语义变更先改本文并按需要登记 `DOCS_REGISTRY.md`；状态变更改 TODO；实施证据只追加到任务进度。
三者通过任务编号和契约 ID 互相引用，不在多个文档复制同一快照。

### 1.1 本文回答什么

本文只回答四类跨域问题：

1. 哪些数据进入板载 Flash，哪些必须留在 RAM、SD 或分发源。
2. 谁能 erase/program，如何证明写入已经 durable。
3. Boot、App、factory tool、USB CDC、USBTMC、UART、RS485、SD 和 TDMA OTA 如何共享一个 FlashMap
   和镜像信任链。
4. 如何从当前兼容布局迁移到目标 v2，并验证掉电、回滚、寿命和实时性。

各业务域仍解释自己的数据语义。Calibration 决定 delay/bias 是否 accepted，VDC 决定 DPLL
profile 是否可用，RefMem 决定 deployment package 是否可激活，TDMA 决定实时窗口是否准入；
Flash Domain 只提供存储、事务、权限和 completion，不成为第二个业务 owner。

### 1.2 事实优先级

| 层级 | 事实源 | 本文使用方式 |
|---|---|---|
| 物理器件 | 产品网表 `U17/W25Q128JVSIQ` | 确认产品器件容量和硬件兼容性。 |
| 当前构建 | `CMakeLists.txt::PICO_FLASH_SIZE_BYTES` | 确认 SDK/XIP 构建声明。 |
| 当前实现 | `drv_flash.h`、`ota_partition.h`、Bootloader、linker 与工具 | 描述 as-is，不用目标文档覆盖现实。 |
| 目标契约 | 本文 + `docs/check/DOCS_REGISTRY.md` | 定义 v2 owner、map、store 和生命周期。 |
| 目标数值 | `config/flash_map_v2.json` 及生成的 `FLASH_MAP_*` 符号 | 作为 v2 目标 map 的数值事实源；生成物新鲜度由构建和 release gate 校验。 |

本文表格中的 v2 offset/size 是 `config/flash_map_v2.json` 的评审快照，不是当前已部署布局。
目标 source/header/manifest 已建立，但 Bootloader、live linker、App 和 factory artifact 仍使用 v1；
在这些消费者同时迁移并完成 factory/HIL 前，不得把目标布局报告为已部署或已烧录。

### 1.3 当前与目标一页对照

| 维度 | 当前实现 | v2 目标 |
|---|---|---|
| 物理容量声明 | 构建和生成式 `FlashGeometry` 声明 16 MiB，并由门禁互校。 | 保持统一 geometry 并增加器件/HIL 证据。 |
| 可访问/已分配范围 | Raw HAL 可做完整 16 MiB 边界检查；`ota_partition.h` 仍只分配下 4 MiB v1 兼容布局，上 12 MiB 未分配。 | 完整 16 MiB 由版本化 `FlashMap` 限权管理。 |
| 分区 owner | v2 source 已生成 header/manifest/CMake/linker 常量；live v1 linker、factory 和 OTA header 尚未切换。 | Boot/linker/App/factory/tool 全部消费同一 map artifact。 |
| App writer | OTA、metadata、Product Config 等路径直接或间接使用 raw Flash API。 | core0 `FlashTransactionAO` 是 App 唯一 erase/program owner。 |
| Boot writer | Bootloader 使用自己的最小写路径。 | 保留独立 `BootFlashService`，但与 App 共用 geometry/map/BCB 契约。 |
| Boot 模式 | v1 compatibility 保留 `COPY_TO_ACTIVE` 仅用于历史回归；V2 不编译该运行分支。 | 只保留 Direct A/B test/confirm/revert 主线；历史 mode 只读返回 `LEGACY_UNSUPPORTED` 或 `UNSUPPORTED`。 |
| 数据存储 | Product Config 固定 sector；Calibration/VDC/RefMem 无统一生产持久化。 | NVS/blob/FCB + versioned namespace + atomic ref。 |
| OTA | USB CDC/USBTMC/SD 已有入口；UART/RS485 有通信资源基础；TDMA 有 reliable bulk 资源基础但无
  stream session。 | 五类本地 ingress 共用 `OtaStreamSession` 和 durable Flash sink，TDMA 在 M5 接入。 |

## 二、顶层决策与契约索引

### 2.1 不变量

以下约束优先于局部组件便利性：

1. App erase/program 只能由 core0 `FlashTransactionAO` 发起；业务 AO 只提交 intent。
2. Bootloader 只能依赖 `BootFlashService + FlashMap + BCB + ImageVerifier + Raw HAL`，不链接
   RTOS、SCPI、TDMA、FatFs 或 littlefs。
3. 所有 offset/size 由一个机器可读 FlashMap 生成或校验，不能继续散落手写。
4. Active App 永不被 App 写；OTA 只安装 inactive slot，成功验证后才提交 pending BCB。
5. 关键 KV 用 append journal/NVS，大对象用 immutable blob + atomic ref，低频事件用 FCB。
6. “入队成功”不是“保存成功”；只有 program + readback verify + commit 完成才是 durable。
7. RUN、Calibration training、thermal critical 或 core1 park 未 ACK 时不开始普通写事务。
8. 完整 A+B package 留在 PC、SD 或 TDMA 分发源；接收板只接收目标 inactive image object。
9. PIO program 随签名 App image 发布；System Pack 只能选择 catalog ID，不能携带可执行字节码。
10. v1 -> v2 不做在线原地搬迁；样板和首批产品走可审计 factory erase/reflash。

### 2.2 已登记目标契约

状态以 `docs/check/DOCS_REGISTRY.md` 为唯一事实源。当前均为 `pending`，表示语义已经确定，
实现和 HIL 尚未完成。

| contract_id | 本文落点 | 核心语义 |
|---|---|---|
| `ARCH-FLASHMAP-01` | 第四章 | Boot/linker/App/factory/tool 共用唯一 FlashMap。 |
| `ARCH-FLASHOWNER-01` | 第三章 | App 只有 core0 FlashTransactionAO 可写；Boot 使用最小服务。 |
| `DOCS-FLASH-01` | 第一章 1.0 节 | 架构、TODO、任务进度三类文档各自的事实边界与变更接口。 |
| `ARCH-BOOTCTRL-01` | 第六章 | BCB 双 lane append/commit，Direct A/B test-confirm-revert。 |
| `ARCH-OTASTREAM-01` | 第七章 | USB CDC/USBTMC/UART/RS485/SD/TDMA 共用 session，ACK 只确认 durable offset。 |
| `REFMEM-PERSIST-01` | 第五章 | 只持久化部署 package/ref；上电建立新 epoch。 |
| `VDC-PERSIST-01` | 第五章 | 只持久化低频 profile；上电重新锁相。 |
| `ARCH-PIOCAT-01` | 第五章 | 只装载签名 App catalog program；System Pack 只选择 ID。 |

## 三、HAOFV 组件与事务模型

### 3.1 组件关系

```text
SCPI / UI / SD / TDMA / factory tool
              |
              v intent / immutable provider
OtaAO / ProductConfigAO / CalibrationAO / VdcAO / StorageAO / DiagnosticsAO
              |
              v bounded command
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

### 3.2 Owner 矩阵

| 事实/动作 | 唯一 writer | reader | 禁止 |
|---|---|---|---|
| App erase/program | core0 `FlashTransactionAO` | 业务 AO 读 completion/vector | OTA、Config、Calibration、SCPI 直接调用 raw erase/program。 |
| Boot erase/program | `BootFlashService` | Bootloader state machine | Bootloader 依赖 App AO 或文件系统。 |
| Flash map | 生成后的静态 map | Boot/linker/App/factory/tool/release gate | 在多处重复手写 offset。 |
| BCB | `BootControlStore` 的受控状态转换 | Boot/OtaAO/Diagnostics | 原地覆盖最后有效记录。 |
| Product Config | ProductConfigAO 产生 record，FlashNVS 持久化 | System/USB/identity | 每次更新整擦一个固定 sector。 |
| Calibration | Calibration Domain 发布 accepted package | VDC/TDMA/Diagnostics | Flash 层计算或接受 diagnostic-only 校准。 |
| VDC profile | VDC owner 发布 accepted low-frequency profile | VDC boot/checking path | 恢复 runtime lock/offset/rate。 |
| OTA session | OtaAO/OtaStreamSession | ingress/Diagnostics | TDMA adapter 写 BCB 或 active slot。 |
| TDMA schedule/gate | TdmaSchedulerAO/core1 | OTA producer 读 gate/credit | OtaAO 自行打开 maintenance gate。 |

### 3.3 FlashTransactionFB

- FB action 每次只推进一个有界步骤，不在 callback 内完成整槽擦除或等待传输。
- VALIDATE 检查 mode、partition permission、range/alignment、active image、温度和 buffer lease。
- QUIESCE/ACQUIRE/PARK 固定按 system mode token -> partition lease -> Flash resource -> core1 ACK。
- RAM resident 临界区覆盖 SDK 调用、数据和 lockout loop；禁止 USB、SD、LCD、格式化日志和
  任何 XIP 间接访问。
- `abort` 只设置请求；当前 page/sector 原子操作和读回校验完成后才能退出。
- COMPLETE 明确区分 accepted、programmed、verified 和 committed；业务域只能把 committed
  completion 投影为“已保存”。

### 3.4 Vector 与 buffer 生命周期

`FlashTransactionVector` 由 core0 owner 写，多字段快照使用 seqlock，至少包含：

- job：`state/job_id/requester/partition_id/operation`；
- progress：`requested_bytes/processed_bytes/verified_bytes`；
- version：`map_version/store_generation/transaction_generation`；
- result：`last_result/last_error/retry_count/abort_pending`；
- lockout：`lockout_request_seq/lockout_ack_seq/lockout_timeout_count`；
- health：`erase_count_delta/program_count_delta/verify_failure_count`；
- policy：`temperature_flags/policy_gate_reason`；
- timing：`started_timestamp_ms/completed_timestamp_ms`。

查询命令只读 Vector，不现场扫描 Flash。小 payload 可复制进固定队列；大 payload 使用 immutable
provider 或固定 pool，并绑定 generation/refcount。completion 前 producer 不能复用 buffer。

## 四、FlashMap v2

### 4.1 分区原则

- 所有区域按 erase sector 对齐；bootable image 还满足 linker/XIP/vector 约束。
- A/B 等长并分别构建 slot-specific XIP image。
- Recovery 是独立签名镜像，不是 Slot A 的普通备份，也不由普通 OTA 自动覆盖。
- OTA Stage 只容纳 manifest、受控 chunk spill 或未来 delta，不缓存完整 A+B package。
- NVS/blob/FCB 分区按事务语义隔离，避免一个 store 的 GC 擦除另一个域事实。
- Scratch 只允许 FlashTransactionAO、GC、Recovery 或 validation lease 使用。
- Future Pool 保持未分配；只有新 map version、兼容性评审和迁移路径完成后才能分配。

### 4.2 目标布局快照

下表是 `ARCH-FLASHMAP-01` 的目标评审快照；数值事实源为 `config/flash_map_v2.json`。生成器已
输出 `FLASH_MAP_*` header、manifest、CMake/linker 常量，并验证对齐、重叠、权限、A/B 等长和
map tail。由于 live linker/factory/OTA 尚未切换，这仍不是当前烧录布局。

| Partition ID | Offset snapshot | Size snapshot | 目标符号 | owner / purpose |
|---|---:|---:|---|---|
| `BOOTLOADER` | `0x000000` | 256 KiB | `FLASH_MAP_BOOTLOADER_*` | immutable Bootloader。 |
| `BOOT_CONTROL` | `0x040000` | 256 KiB | `FLASH_MAP_BOOT_CONTROL_*` | BCB lanes、map manifest、security counter。 |
| `APP_A` | `0x080000` | 2 MiB | `FLASH_MAP_APP_A_*` | Direct A/B slot A。 |
| `APP_B` | `0x280000` | 2 MiB | `FLASH_MAP_APP_B_*` | Direct A/B slot B。 |
| `RECOVERY` | `0x480000` | 1 MiB | `FLASH_MAP_RECOVERY_*` | 独立签名 Recovery。 |
| `OTA_STAGE` | `0x580000` | 1 MiB | `FLASH_MAP_OTA_STAGE_*` | manifest/chunk spill/delta staging。 |
| `OTA_JOURNAL` | `0x680000` | 256 KiB | `FLASH_MAP_OTA_JOURNAL_*` | stream resume/transaction journal。 |
| `PRODUCT_NVS` | `0x6C0000` | 256 KiB | `FLASH_MAP_PRODUCT_NVS_*` | identity、USB mode、产品 KV。 |
| `CALIBRATION_NVS` | `0x700000` | 1 MiB | `FLASH_MAP_CALIBRATION_NVS_*` | accepted/staging/previous calibration。 |
| `VDC_NVS` | `0x800000` | 256 KiB | `FLASH_MAP_VDC_NVS_*` | servo/holdover/discipline profile。 |
| `SYSTEM_PACK` | `0x840000` | 2 MiB | `FLASH_MAP_SYSTEM_PACK_*` | Deployment Capsule、RefMem package、小型 blob。 |
| `FAULT_FCB` | `0xA40000` | 1 MiB | `FLASH_MAP_FAULT_FCB_*` | boot/power/critical fault events。 |
| `SCRATCH` | `0xB40000` | 1 MiB | `FLASH_MAP_SCRATCH_*` | GC/recovery/validation lease。 |
| `FUTURE_POOL` | `0xC40000` | 3.75 MiB | `FLASH_MAP_FUTURE_POOL_*` | 未分配。 |

A/B、Bootloader、BCB 和 Recovery 是启动安全链，不应全部计作“OTA 缓存”。真正用于临时 OTA
数据的只有 Stage/Journal，而且 target 不缓存完整 package。App 容量是否足够由 release size
gate 根据链接产物和分区符号判断，不在文档固定百分比。

### 4.3 权限视图

| Partition | Boot | App read | App write | factory |
|---|---|---|---|---|
| Bootloader | verify/read | metadata only | never | signed factory flow only |
| Boot Control | constrained read/write | snapshot | store intent only | initialize/recover |
| Active App | verify/boot | XIP | never | program/verify |
| Inactive App | verify | bounded verify | OTA intent only | program/verify |
| Recovery | verify/boot | version/hash | privileged release intent | program/verify |
| OTA Stage/Journal | resume read | OTA owner | OTA intent only | initialize |
| NVS/Blob/FCB | minimal recovery read | Store API | namespace intent only | initialize/migrate |
| Scratch | recovery lease | no raw address | transaction lease only | validation/recovery |

Map manifest 必须携带 map version、geometry profile 和 compatibility。未知 map 不尝试猜测旧地址；
Boot fail closed 进入 Recovery/ROM，App 不在线重分区。

## 五、持久化数据模型

### 5.1 数据分类规则

一个对象只有同时满足以下条件才可进入板载 Flash：跨重启仍成立、有明确 owner 和 schema、
可独立校验、写频率可预算、掉电后能判定旧/新事实、目标分区权限允许。否则使用 RAM、SD
或外部分发源。

| Store | 适用对象 | 事务模型 | 不适用对象 |
|---|---|---|---|
| `BootControlStore` | BCB、security counter、image state | redundant lane + append + commit | 普通 KV、日志。 |
| `FlashNVS` | 小型 versioned KV/record | append + CRC + rotation | package、波形、实时状态。 |
| `FlashBlobStore` | immutable package/capsule/blob | object hash + manifest + atomic ref | 任意可执行代码、高频追加。 |
| `FlashFCB` | 低频顺序事件 | append ring + sector GC | 高频 trace、周期传感器流。 |

所有 record/object 都必须有 schema、length、generation、CRC/hash 和 commit 状态。reader 跳过
torn、unknown-required-schema 和未 commit 对象。GC 只能作为 FlashTransactionAO job 执行。

### 5.2 主域 namespace 矩阵

| 主域 | 持久化事实 | Store | 不持久化/外部存储 |
|---|---|---|---|
| System/Product | board identity、USB mode、capability、permission、active capsule ref | Product NVS/System Pack | 当前 mode、resource lock、command queue |
| Trigger/Loop | named sequence/recipe/mission、safe limit、active profile ref | System Pack | ARM/RUN/PAUSE、cursor、live queue、deadline；历史进 SD |
| SYNC_IO/PIO/DMA | board IO profile、catalog ID、resource claim | signed App catalog + System Pack selection | IMEM/SM/FIFO/DMA/IRQ/live persona |
| Calibration | accepted/staging/previous package、generation、topology/profile binding、delay/bias/residence | Calibration NVS | diagnostic-only raw capture；完整 evidence 进 SD |
| TDMA | foundation/operating/process-image profile、payload whitelist/budget、adapter claim | System Pack | ring state、window cursor、counter、FIFO、in-flight、live gate |
| RefMem | `.rmtp`、ApplicationMap、Capability/NodeLoad/FB/Event/DataLink tables、active/previous ref | System Pack/BlobStore | live vector、dirty、command/ACK、heartbeat、stale、peer online、epoch |
| VDC/DPLL | servo/holdover/reference/timestamp dictionary、accepted aging/temperature/wander profile、Calibration ref | VDC NVS/System Pack baseline | offset/rate/phase/DCO、LOCK/HOLDOVER、map generation、sample ring |
| Communication | adapter/address/role profile、accepted calibration ref | System Pack/Calibration NVS | RX/TX FIFO/counter、transaction、retry、online state |
| Measure/Capture | channel/range/unit/trigger/compression profile、calibration ref | System Pack | raw sample/waveform/capture ring/report 进 RAM/SD |
| OTA/Boot | BCB、manifest/hash/signature/counter、slot state、durable journal | Boot Control/App/Recovery/OTA Journal | transport queue、未 checkpoint offset、完整 package |
| Storage/Deployment | active/previous capsule、最小离线对象和 refs | System Pack/BlobStore | 完整 pack/history/user files 进 SD |
| Diagnostics/UI | critical event、Flash health、必要低频偏好 | Fault FCB/Product NVS | 高频 trace、周期传感器流、当前页面/编辑瞬态 |

该矩阵不为每个域建立私有分区。只有 endurance、权限、掉电原子性或 Recovery 依赖确实不同，
才允许提出新 map version。业务域申请 namespace/object，不申请裸 offset。

### 5.3 RefMem

`REFMEM-PERSIST-01` 只允许持久化 deployment input：`.rmtp`、静态表、package hash/schema/
generation 和 active/previous atomic ref。上电先读入 staging，通过 CRC/schema/owner/resource/
DeploymentGate 后由 `DistributedRefMemAO` 激活，并建立新 epoch。

live 64 KiB vector、dirty mask、slot sequence、command、ACK/NACK、heartbeat、stale、peer online、
transport in-flight 和 RUN completion 不能恢复为有效事实。所有 peer mirror 在重新 HELLO/EPOCH/
full-or-delta sync 前保持 stale。

### 5.4 VDC/DPLL

`VDC-PERSIST-01` 将 Calibration source facts 与 VDC discipline profile 分开：Calibration NVS 保存
accepted link delay/bias；VDC NVS 保存 servo、holdover、reference priority、timestamp dictionary
和经维护态 accept 的 aging/temperature/wander model。

`offset/rate/phase_error/DCO/lock_state/HOLDOVER age/map generation/sample ring` 掉电即失去
freshness。每次启动固定从 `OFF/CHECKING` 开始，绑定 active Calibration 后，用新硬件观测
重新锁相；不能因 profile 有效而直接发布 `LOCKED`。

### 5.5 PIO Program Catalog

`ARCH-PIOCAT-01` 的目标链为：

```text
signed App image
  -> PioProgramCatalog(program_id/version/hash/instruction_count/resource_claim)
  -> System Pack selects allowed persona/program_id
  -> PIO owner stops SM/DMA, validates claim, loads IMEM, publishes snapshot
```

`.pio` program 受 App image 的 hash/signature/rollback 保护，不需要独立 Flash 分区。BlobStore
拒绝 executable PIO/native object。若未来要求独立 bytecode 更新，必须另立 signed module
manifest、ABI、静态 verifier、IO/resource sandbox 和 rollback 契约。

## 六、Boot 与镜像信任链

### 6.1 Boot Control Block

BCB 使用至少两个独立 erase lane。record 包含 schema/map、sequence/boot generation/security
counter、active/pending/confirmed/previous slot、attempt/rollback/result/reset reason、各镜像
manifest/hash/size/version/state 以及 CRC/commit marker。

写入顺序固定为 body -> readback verify -> commit marker。选择最新记录前先验证 schema/map、
length、CRC 和 commit；GC 必须先在另一 lane seal 新基线，再回收旧 lane。

### 6.2 Image Manifest

manifest 至少覆盖 product/hardware compatibility、slot/link address、image size、entry/vector
range、semantic version、build identity、hash、signature、key ID、security counter 和 extensible
TLV。CRC 只证明介质/传输完整性，不替代签名和 anti-rollback。

### 6.3 启动状态机

```text
load geometry/map -> select valid BCB -> verify pending/confirmed image
  pending valid and attempts available -> TEST_BOOT(pending)
  App self-test + explicit confirm       -> CONFIRMED
  reset/no confirm/verification failure  -> REVERT(previous confirmed)
  A and B unusable                       -> verify and boot RECOVERY
  map/BCB/recovery all unusable          -> ROM BOOTSEL/factory indication
```

Recovery image 负责验证 factory package 和受控 USB/SD 恢复。Recovery 更新使用更高权限策略，
不能由任意 TDMA sender 覆盖。Bootloader 不扫描普通 NVS、littlefs 或 SD 文件系统。

## 七、统一 OTA 与 TDMA 分发

### 7.1 Package 与 receiver 存储策略

统一 package 把 manifest 与 slot-specific image object 分开索引。PC、SD 或 TDMA source 保存
完整 package；receiver 根据 active slot/capability 只请求 inactive object，并直接事务化写入
inactive App。无关 slot object 不传输、不进入 OTA Stage。

### 7.2 OtaStreamSession

```text
UsbOtaIngress / SdOtaIngress / UartOtaIngress / TdmaOtaIngress
                         |
                         v
OtaStreamSession
  OPEN -> RECEIVE -> DURABLE_ACK -> VERIFY_OBJECT
  -> INSTALL_INACTIVE -> MARK_PENDING -> READY_TO_REBOOT
                         |
                         v
FlashTransactionAO / BootControlStore
```

session 绑定 source/target identity、capability、package/manifest hash、map version、target
partition、generation、total size 和 destination policy。transport adapter 只收发 bytes/chunks，
不能复制 OTA 状态机、写 BCB 或调用 raw Flash。

### 7.3 Durable ACK、credit 与 resume

- ACK 是 cumulative durable offset，仅在 program + readback verify 后推进。
- receiver credit 由固定 RX pool、Flash job depth、checkpoint budget 和 maintenance gate 决定。
- credit 为零时 sender 保持 session，不 busy-loop，也不挤占 VDC/RefMem 实时流。
- 重复 DATA 按 session/offset/hash 幂等；同 offset 冲突数据 fail closed。
- resume checkpoint 不按每帧擦写；token 绑定 package hash、map、partition、identity 和 generation。
- reset 后用 journal + Flash readback 重建 durable offset，torn journal 回退最近可信 checkpoint。

### 7.4 TDMA 边界

TDMA OTA 只使用已有 `TDMA_PAYLOAD_CLASS_OTA_BULK` 和 `TDMA_TRAFFIC_RELIABLE_BULK`。
控制语义为 `OPEN/OPEN_ACK/RESUME_QUERY/DATA/ACK/CLOSE/CLOSE_ACK/ABORT/STATUS`；wire
contract 和 parser 在实现前保持 registry `pending`。

`TdmaSchedulerAO` 独占 maintenance gate、window 和 adapter；OtaAO 独占 package/session/
distribution；FlashTransactionAO 独占 durable write。RUN/CAL 或 gate closed 时允许 session
暂停，禁止以动态优先级绕过 VDC、RefMem、T2 或 Calibration 窗口。

多板分发由 `OtaDistributionFB` 维护 per-node capability、durable offset、verify、pending 和 boot
result。每个节点独立 ACK；先 stage/verify，再按 cohort policy commit。reference 节点升级前先
迁移角色和 topology/profile generation，不能在 reboot 期间继续宣称 ring/VDC 有效。

### 7.5 对 DSoftBus 理念的受控借鉴

| 理念 | 本项目映射 | 不采用 |
|---|---|---|
| identity/capability | board unique ID + accepted topology + receiver capability | 动态发现成为安全身份 |
| session/socket | 有界 `OtaStreamSession` | 无界动态会话 |
| lane/QoS | open 前选择 transport；TDMA 映射 reliable bulk | session 中无证据切路 |
| bytes/stream | manifest 约束的 chunk/object | 任意远端文件路径 |
| distributed bus | 静态 topology + RefMem/TDMA facts | OpenHarmony IPC、动态路由和无感切换 |

## 八、运行策略、诊断与验证

### 8.1 Mode、温度和资源门禁

| 条件 | read | 新 erase/program | 原子操作中的处理 |
|---|---|---|---|
| BOOT | map/BCB/image verify | BootFlashService constrained | 完成当前 record 原子步骤 |
| MAINTENANCE/OTA | yes | policy 允许 | 分片完成后可 abort |
| RUN | bounded read/XIP | no | 完成 page/sector 后关闭 gate |
| Calibration training | evidence read only | no | 完成原子步骤并报告 violation |
| thermal warning | yes | 降速或暂停 | 完成原子步骤 |
| thermal critical/sensor invalid | yes | no | 完成原子步骤后 fail closed |
| FAULT | diagnostics | 仅明确 Recovery policy | 不开始普通 GC/OTA/config write |

资源申请顺序固定为 system mode token -> partition lease -> Flash resource -> core1 park -> raw
operation。transport 提交 immutable buffer 后应尽快释放 USB/SD/TDMA 资源，不能持锁等待 erase。

### 8.2 SCPI 投影

维护面只读 owner Vector：

```text
SYSTem:FLASH:INFO?
SYSTem:FLASH:MAP? [partition]
SYSTem:FLASH:JOB?
SYSTem:FLASH:STORE?
SYSTem:FLASH:WEAR?
SYSTem:OTA:STREAM:STATus?
SYSTem:OTA:STREAM:RESume?
```

release 固件不提供任意 offset erase/program。validation destructive command 必须编译隔离、
限制在 Scratch lease，并由 release string scan 证明不存在。

### 8.3 验证矩阵

| Gate | 必须证明 |
|---|---|
| Map | 对齐、无重叠、表尾匹配 geometry；linker/factory/tool 同源。 |
| Owner | App 除 FlashTransactionAO/Boot adapter 外无裸 erase/program 调用。 |
| Dual-core | 每次写有 park ACK；timeout 不写；release 后 core1 alive。 |
| Power-cut | BCB/NVS/blob ref/FCB/journal 每个 commit 边界都有旧/新确定结果。 |
| Boot | A->B、B->A、未确认回滚、A/B 损坏进 Recovery、BCB 损坏 fail closed。 |
| High address | validation lease 验证超过旧兼容边界的 erase/program/readback，不碰产品区域。 |
| Stream | 乱序、重复、丢帧、CRC、zero credit、resume、identity/hash/generation mismatch。 |
| Realtime | OTA bulk 不新增 VDC/RefMem deadline miss、window overrun 或 Calibration 污染。 |
| Wear | sector rotation、GC、write frequency、温度 policy 和最后有效记录。 |
| Release | image signature、anti-rollback、factory/recovery artifact、SBOM/key policy 和报告齐全。 |

只有 host test、build/link gate 和相应 DHRT100/两板/四板 HIL 都具备证据，registry 契约才可从
`pending` 进入 `active`。状态变化必须另做 C11 交叉审核。M4 的系统对象名为 `DHRT100`；`COM8`
仅表示当前物理验证端口，不能作为产品或系统名称；历史 `RP2350_TRIG_*` 构建产物名保留到回退
迁移完成。

## 九、迁移与发布边界

### 9.1 v1 -> v2

1. 保存当前发布 artifact 和 v1 BOOTSEL 回退路径。
2. 读取并归档 identity、Product Config、OTA metadata、传感器和校准/报告索引。
3. 用 factory tool 明确选择目标板，执行 full erase、program、readback verify。
4. 初始化 map manifest、BCB、Slot A、Recovery 和空 store baseline。
5. 运行高地址 Scratch、Product NVS、Calibration empty/default、A/B/revert/Recovery 验证。
6. 恢复 TDMA/Calibration persona，证明新 Flash 路径未破坏 PIO/DMA/IO owner。

App 不自动识别并搬迁旧 offset；未知 map 进入 Recovery。迁移报告记录每个 region hash、旧/新
identity、Boot result 和可恢复 artifact。

### 9.2 发布完成条件

- FlashMap 数字只有一个机器可读来源。
- App/Boot writer 和依赖边界可由 link/scan gate 证明。
- Direct A/B、Recovery、signature、anti-rollback 和 BCB 掉电闭环完成。
- Product/Calibration/VDC/RefMem/Deployment/Fault store 有 torn/GC/wear 证据。
- USB CDC、USBTMC、UART、RS485、SD 本地 OTA 与 TDMA 两板/四板分发均使用同一 stream 核心。
- release 固件不含 destructive command、dev key 或 v1 隐式 offset 假设。

## 十、未来产品与平台扩展

### 10.1 Deployment Capsule

`SYSTEM_PACK` 只保存节点启动和离线运行所需的最小 verified subset：ApplicationMap、role/
persona、profile、mission index、Calibration ref、capability requirements 和 schema。完整 pack、
历史、波形和报告留在 SD/外部系统。

AO/FB/persona 原生代码静态编译进签名 firmware；Capsule 只选择实现和参数。动态原生插件必须
另立 signed module ABI、MPU/permission、resource claim 和 rollback 架构。

### 10.2 应用数据放置

| 场景 | 板载 Flash | RAM/SD/外部数据面 |
|---|---|---|
| 分布式仪表 | identity、capability、calibration summary、active deployment ref | 完整校准报告、波形、截图、长日志 |
| 运动/电机控制 | 安全限制、零点/传感器/功率级 profile、active mission ref | 实时轨迹、ADC/PWM trace、任务库 |
| DAQ/分布式测量 | channel/timestamp/compression/trigger profile | raw sample、批量数据、完整 evidence |
| ATE/产测 | station/fixture identity、permission、recipe ref、受控 checkpoint | 批次明细、附件、审计报告 |
| 分布式 RF | calibration summary、beam/scan ref、security counter | 大型阵列表、扫描数据、质量报告 |
| 开源 reference | 示例 capsule、capability manifest、golden vectors | simulator dataset 和构建产物 |

### 10.3 跨平台语义

RP2350 16 MiB map 是 reference geometry。STM32H7、i.MX RT、Linux/PRU、FPGA/Zynq 可使用
不同 offset 和介质，但保持 Partition ID、transaction completion、image trust、Store API 和
record schema 语义。容量不足时 fail closed 或选择另一 geometry profile，不运行时隐式重分区。

## 十一、参考取舍与明确不做

| 来源 | 借鉴 | 不照搬 |
|---|---|---|
| MCUboot | signed image/TLV、security counter、test/confirm/revert | 完整 trailer/swap 机制 |
| Zephyr flash map/NVS/FCB | 单一分区词汇、append/rotation/torn recovery、循环事件 | Zephyr device model |
| ESP-IDF OTA | 冗余 boot control、confirm/revert | ESP32 partition/API |
| littlefs | 仅在确有目录型 blob 需求时评估 | BCB、critical KV、Calibration acceptance |
| OpenHarmony DSoftBus | capability/session/lane/QoS/stream 理念 | 动态发现、路由、IPC、无感切换 |

明确不做：

- 不让业务域、SCPI 或 transport 绕过 FlashTransactionAO。
- 不让 littlefs/NVS/FCB 成为 Bootloader 依赖。
- 不把高频采样、完整 trace、文本日志或用户任意文件写入板载 Flash。
- 不缓存完整 A+B package 到每个 target。
- 不在线猜测或迁移未知旧 map。
- 不在签名、anti-rollback、resume 和多板 HIL 完成前宣称 TDMA OTA 可量产。
- 不把 System Pack data blob 当作可执行插件。
