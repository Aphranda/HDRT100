# SD 卡 System Pack 规划与待办

Status: Active
Domain: SD
Canonical: `docs/SD_TODO.md`
Related: `docs/SD_TASK_PROGRESS.md`, `docs/SCPI_COMMANDS.md`, `docs/OTA_SYSTEM_DESIGN.md`
Last updated: 2026-07-07

本文档定义 RP2350_TRIG 的 SD 卡系统。SD 卡不是简单 OTA 介质，而是 App 侧 **System Pack 介质 + 持久化观测层**，用于任务配置、校准补偿、Pack/Ref 版本管理、Vector/反射内存快照、脉冲异常 trace、运行报告、产测结果和离线 OTA。

Bootloader 第一版不读取 SD/FatFs。SD 卡可插拔，文件系统和写入延迟都不适合进入最小启动链路，也不能进入 PIO/DMA/IRQ 硬实时触发闭环。

## 验收标准摘要

| 优先级 | 验收标准 |
|---|---|
| P0 | 新 FAT32 卡可自动 bootstrap 最小 System Pack；StorageAO job 覆盖文件查询、分页目录、manifest scan、snapshot、trace 和 fault evidence；所有 SD/FatFs 操作保持在管理面，硬实时路径不等待 SD。 |
| P1 | Pack/Ref、profile/cal、mission/recipe、离线 OTA 和 schema/capability 校验形成事务化流程；ARM 前能把 SD 配置转换为事件和摘要，armed 状态拒绝关键配置热加载。 |
| P2 | 报告、产测、工具、多卡兼容、热插拔、长稳和掉电恢复具备可重复验证矩阵；release 归档包含 SD image、manifest 和报告模板。 |

## 1. 核心定位

```text
RAM Vector / 反射内存 = 当前实时事实
RAM trace ring        = 最近事件与脉冲异常现场
SD System Pack        = 历史事实、任务包、校准包、证据包
```

原则：

- SD 卡可以在 `BOOT`、`IDLE`、ARM 前、`DISARM` 后、`FAULT` 后读取或写入。
- ARM 后硬实时阶段不得等待 SD/FatFs。
- 实时异常在硬实时热路径只做必要状态锁存；RAM trace ring 追加必须位于非 hot path 或已证明不影响实时性的管理面，之后由 `StorageAO` 分片落盘。
- PIO/DMA/IRQ 硬实时热路径不得新增 SD、FatFs、日志、trace 写入或非确定性操作；只能保留维持实时输出必需的寄存器操作、计数器和状态锁存。
- SCPI/UI/SD 文件输入都只能投递事件，不能直接修改 Trigger/Ota/Storage 域状态。
- Vector 不保存大块数据；Vector 只记录 id、hash、size、CRC、进度和错误摘要。
- 证据文件必须使用临时文件、索引和完整性标志，避免掉电后把半文件当成有效证据。
- SD 根目录是长期兼容接口，后续新增 TRIG 模式不得通过新增模式专属根目录扩展；模式差异必须收敛到 versioned pack/profile/mission/cal 文件中。
- 推荐介质：8GB 或 16GB FAT32 microSDHC。

## 2. HAOFV 分层

| 层级 | SD 相关职责 |
|---|---|
| `SCPI / UI / PC tool intent` | 只表达意图：查询、加载、checkout、导出、离线 OTA。 |
| `StorageAO` | SD 域主动对象，负责事件队列、执行预算、文件 job、跨域事件投递。 |
| `StorageFB` | 表驱动主状态机，调度 mount、scan、read/write、snapshot、trace、pack/ref。 |
| `PackFB` | StorageFB 内部子功能块，扫描、校验、checkout System Pack。 |
| `RefFB` | StorageFB 内部子功能块，解析和事务切换 `active/previous/factory/candidate`。 |
| `SnapshotFB` | StorageFB 内部子功能块，写 snapshot/report，并关联 active pack 摘要。 |
| `StorageVector` | 发布卡状态、job 摘要、pack/ref 摘要、最近 snapshot/trace/report 摘要。 |
| `Resource Arbiter` | 串行化 `SPI0 + SD`、`SPI0 + LCD`、`FLASH`。 |
| `FatFs Port / sd_card driver` | 只提供文件系统和 block 访问，不保存业务状态。 |

典型链路：

```text
SCPI/UI/PC tool intent
  -> storage_manager_post_event()
  -> StorageAO
  -> StorageFB
     -> RefFB / PackFB / SnapshotFB
  -> Resource Arbiter
  -> FatFs / sd_card
  -> StorageVector summary
  -> optional TriggerAO/OtaAO/DiagnosticsAO event
```

跨域动作：

```text
CHECKOUT_PACK success
  -> TriggerAO CONFIG_SET event
  -> TriggerAO CAL_SET event
  -> DiagnosticsAO PACK_CHECKOUT event

OFFLINE_OTA selected
  -> OtaAO PACKAGE_BEGIN/DATA/END event stream

pulse fault captured
  -> DiagnosticsAO FAULT_LATCHED
  -> StorageAO WRITE_SNAPSHOT + WRITE_TRACE
```

## 3. System Pack 与 Git-like 模型

Git 对本系统的意义是配置集合可追溯、可回滚、可审计，而不是在固件里实现 Git。

固件端采用轻量子集：

| Git 思想 | 固件端采用 | 说明 |
|---|---|---|
| commit | 是，称为 `pack` | 一组 profile/mission/cal/update 文件的不可变组合。 |
| ref | 是 | `active`、`previous`、`factory`、`candidate` 指向 pack。 |
| hash | 是 | 固件以 CRC32 起步，PC 工具可用 SHA256。 |
| checkout | 是，受限 | 只允许 ARM 前把 pack 加载到 RAM 配置快照。 |
| rollback | 是，显式 | 退回 previous/factory；第一版不自动回滚。 |
| diff | 仅 PC 工具 | 固件不做文本 diff。 |
| branch/merge | 否 | 不在固件端支持分支、合并、冲突解决。 |
| object database/packfile | 否 | 不实现 Git 对象库和压缩包。 |

System Pack 规则：

- `candidate` 只表示待验证 pack，不直接影响 Trigger。
- `candidate` 通过 manifest/schema/product/hardware/required/hash 校验后才允许 checkout。
- checkout 只在 `BOOT/IDLE/MAINTENANCE` 或 ARM 前执行。
- checkout 成功后，配置先进入 StorageAO 的已验证对象集合，再通过事件进入 TriggerVector 摘要。
- 触发运行中不得切换 ref。
- 脉冲异常不会自动回滚 pack，但 fault report 必须记录 active/previous pack 信息。
- 是否回滚由显式 SCPI/PC 工具/人工动作决定。
- 后续新增 TRIG mode 时，pack 是模式能力边界；旧固件遇到无法理解的新模式包必须拒绝 checkout，但仍允许目录查询、文件信息查询和证据导出。

## 4. SD 文件架构

根目录固定小写。P0 目标是先稳定 `/manifest.idx + /profile + /mission + /cal + /update + /snapshots + /traces + /reports`；P1 引入 `/packs + /refs` 后，根目录默认文件可作为 active pack 的兼容镜像。

```text
/
  manifest.json
  manifest.idx
  refs/
    active.ref
    previous.ref
    factory.ref
    candidate.ref
  packs/
    pack_000001/
      manifest.json
      manifest.idx
      profile/active.json
      mission/recipe.json
      mission/sequence.bin
      mission/node_map.json
      cal/board_cal.json
      cal/cable_delay.json
      cal/dpll_comp.bin
      update/RP2350_TRIG_UPDATE.pkg
  profile/
    active.json
    profiles/
  mission/
    recipe.json
    sequence.bin
    node_map.json
  cal/
    board_cal.json
    cable_delay.json
    dpll_comp.bin
    io_delay_table.bin
  snapshots/
    boot/
    arm/
    fault/
    run/
  traces/
    run/
    fault/
  reports/
    run/
    fault/
    acceptance/
  logs/
    boot.log
    fault.log
    storage.log
    ota.log
  update/
    RP2350_TRIG_UPDATE.pkg
  factory/
    test_plan.json
    result.json
```

目录职责：

| 路径 | 职责 | 读取者 |
|---|---|---|
| `/manifest.json` | 人类可读 SD 根索引。 | PC 工具 / SCPI / UI |
| `/manifest.idx` | 固件可解析的根索引。 | StorageAO |
| `/refs/*.ref` | active/previous/factory/candidate 指针。 | RefFB |
| `/packs/pack_xxxxxx/` | 不可变 System Pack。 | PackFB |
| `/profile` | P0 兼容 profile 入口。 | StorageAO -> TriggerAO |
| `/mission` | P0 兼容 mission 入口。 | StorageAO -> TriggerAO |
| `/cal` | P0 兼容 calibration 入口。 | StorageAO -> TriggerAO |
| `/snapshots` | Vector/反射内存快照。 | PC 工具 / SCPI |
| `/traces` | 二进制事件 trace 和索引。 | PC 工具 |
| `/reports` | 运行摘要、脉冲异常报告、验收报告。 | PC 工具 / 人工 |
| `/logs` | 文本日志。 | PC 工具 / 人工 |
| `/update` | P0 兼容 OTA package 入口。 | StorageAO -> OtaAO |
| `/factory` | 产测计划和结果。 | StorageAO / 产测工具 |

旧规划中的 `/config` 映射为 `/profile + /mission`，`/capture` 映射为 `/traces + /reports`。P0 工具和固件默认不再生成或依赖 `/config`、`/capture`、`/resource`；如需兼容旧卡，只允许 PC 工具或维护命令以只读方式识别旧目录并给出迁移提示。

### 4.1 TRIG 模式扩展兼容

SD 文件系统必须把“新增 TRIG 模式”视为 System Pack 内容演进，而不是根目录协议演进。新增模式例如 gate-level、arm-single、free-burst、encoder/PCNT 扩展、A0-A3 测试流程，都应复用 `/profile`、`/mission`、`/cal`、`/packs`、`/refs`：

- 不新增 `/seq_step`、`/enc_count`、`/free_burst` 等模式专属根目录；根目录只表达对象类型，不表达业务模式。
- P0 兼容入口固定为 `/profile/active.json`、`/mission/recipe.json`、`/mission/sequence.bin`、`/mission/node_map.json`、`/cal/*`。P1 后这些文件可由 active pack 镜像到根目录兼容入口。
- `/profile/active.json` 保存当前 active mode、通用触发参数、能力摘要和默认 mission/cal 引用；不保存大块序列表或模式私有二进制。
- `/mission/recipe.json` 保存模式 recipe 和流程步骤；`/mission/sequence.bin` 保存高频/大块序列；`/mission/node_map.json` 保存 A0-A3 角色、I/O 映射和测试流程部署。
- 模式私有的大块表，例如 burst pattern、DPLL/延迟补偿、复杂门控表，使用带 `magic/format_version/header_size/payload_size/crc32` 的二进制文件，放在 `/mission` 或 `/cal` 下，并由 manifest `required=` 引用。
- 每个模式必须声明 `mode_id`、`mode_name`、`mode_schema`、`min_firmware` 和可选 `capability_flags`。旧固件可以忽略未知可选字段，但不得 ARM 未知必需模式。
- schema 相同且只新增可选 key：旧固件可忽略未知 key 并继续应用已知字段。
- 新增必需文件、改变模式语义或改变二进制 payload 格式：必须提高对应 `mode_schema` 或 pack/schema，并通过 `min_firmware` 阻止旧固件 checkout。
- Trace/report 不按模式拆目录；新增模式只扩展 trace `domain/event_id`、decoder 事件名和 report JSON 字段，继续写入 `/traces/run|fault` 与 `/reports/run|fault`。
- SD 验证工具必须覆盖“旧兼容入口 + active pack 入口”两种路径，确认新模式文件可枚举、可校验、可拒绝不兼容 schema。

建议 profile 片段：

```json
{
  "magic": "RP2350_TRIG_PROFILE",
  "schema": 1,
  "active_mode": {
    "mode_id": 1,
    "mode_name": "SEQ_STEP",
    "mode_schema": 1,
    "min_firmware": "0.1.0",
    "capability_flags": ["sequence", "edge", "gate"]
  },
  "mission": "/mission/recipe.json",
  "calibration": "/cal/board_cal.json"
}
```

建议 mission recipe 片段：

```json
{
  "magic": "RP2350_TRIG_MISSION",
  "schema": 1,
  "mode": "SEQ_STEP",
  "mode_schema": 1,
  "sequence": {
    "path": "/mission/sequence.bin",
    "format": "RP2350_TRIG_SEQUENCE",
    "format_version": 1,
    "crc32": "00000000"
  },
  "nodes": "/mission/node_map.json"
}
```

## 5. Manifest 与 Ref

`manifest.json` 人类可读，`manifest.idx` 固件第一版优先读取。固件不解析复杂嵌套 JSON，避免脆弱字符串匹配。

`manifest.idx` 示例：

```text
magic=RP2350_TRIG_SD
schema=1
product_id=RP2350_TRIG
hardware_id=rp2350_trig
pack_version=0.1.0
min_firmware=0.1.0
default.profile=/profile/active.json
default.mission=/mission/recipe.json
default.calibration=/cal/board_cal.json
default.ota_package=/update/RP2350_TRIG_UPDATE.pkg
mode=SEQ_STEP
mode_schema=1
capability=sequence
capability=edge
capability=gate
required=/profile/active.json,type=profile,crc32=00000000
required=/mission/recipe.json,type=mission,crc32=00000000
required=/update/RP2350_TRIG_UPDATE.pkg,type=ota_package,crc32=00000000
```

`*.ref` 示例：

```text
magic=RP2350_TRIG_REF
schema=1
name=active
pack=/packs/pack_000001
pack_id=1
pack_crc32=12345678
manifest_crc32=00000000
```

解析约束：

- 固件只接受 ASCII key 和 UTF-8 value。
- 每行最大长度固定上限，例如 160 字节。
- 未识别 key 忽略。
- `schema/product_id/hardware_id` mismatch 必须拒绝应用。
- `required=` 行用于存在性和可选 CRC 检查。
- `mode/mode_schema/capability` 描述当前 pack 对 Trigger 的需求；未知必需模式或 schema 较新时不得 checkout。
- 未识别的可选 key 必须忽略；未识别但被 `required=` 引用的对象必须按 schema/min_firmware 规则决定是否拒绝。
- 复杂 hash、签名、版本矩阵由 PC 工具校验。

生成约束：

- `manifest.json` 和 `manifest.idx` 必须由同一次工具运行生成，二者的 product/hardware/schema/default/required 摘要必须一致。
- P0 release 验收以 `manifest.idx` 为固件入口；只有 `manifest.json` 不能视为可 checkout 的 System Pack。
- `sd_fs_build.py` 输出目录必须匹配第 4 节固定根目录；旧目录只能作为显式兼容选项输出。
- 新增 TRIG mode 时，PC 工具必须同时更新 profile、mission、manifest、ref 摘要，并生成兼容性报告：最低固件版本、必需能力、必需文件、CRC/hash。

## 6. 文件格式

| 数据 | 推荐格式 | 原因 |
|---|---|---|
| manifest/profile/mission/report | JSON | 易读、便于上位机生成。 |
| manifest/ref 固件索引 | line-oriented `.idx/.ref` | 固件解析简单可靠。 |
| calibration 小表 | JSON | 易读，适合少量 offset、版本、hash。 |
| calibration 大表 / DPLL 补偿 | binary + header + CRC | 体积小，读取快，避免解析负担。 |
| trace | binary ring dump + `.idx` | 事件数量可能较大，二进制更稳定。 |
| CSV report | CSV | 方便 Excel/脚本快速分析。 |
| OTA | unified `.pkg` | 默认 release 格式，raw `.bin` 只保留兼容路径。 |

所有二进制文件头建议包含：

```text
magic
format_version
header_size
payload_size
build_id/hash
sequence
crc32
```

二进制通用约束：

- 多字节整数统一 little-endian。
- `crc32` 覆盖范围必须逐格式写清；默认覆盖 header 中 `crc32` 字段之后的有效 payload。
- `header_size` 必须允许后续追加字段；未知 header 扩展字段跳过但不得影响 payload offset。

Snapshot JSON 第一版保存摘要，不转储大块数组：

```json
{
  "magic": "RP2350_TRIG_SNAPSHOT",
  "schema": 1,
  "kind": "fault",
  "sequence": 1,
  "build_id": "20260703163458",
  "uptime_ms": 123456,
  "active_pack_id": 1,
  "active_pack_crc32": "12345678",
  "system": {
    "mode": 1,
    "fault_summary": 0,
    "resource_locks": 0
  },
  "trigger": {
    "mode": "SEQ_STEP",
    "state": "ARMED",
    "count": 0,
    "error": 0
  },
  "storage": {
    "state": "CARD_READY",
    "error": 0
  },
  "ota": {
    "mode": "DIRECT_AB",
    "state": "COMMITTED"
  },
  "trace": {
    "id": 1,
    "path": "/traces/fault/fault_000001.bin",
    "events": 0
  }
}
```

Trace binary 第一版头：

```text
magic:       "RTRC"
schema:      uint16
header_len:  uint16
sequence:    uint32
event_count: uint32
start_ms:    uint32
end_ms:      uint32
tick_hz:     uint32
flags:       uint32
crc32:       uint32
```

Trace 事件记录固定 16 字节：

```text
timestamp_us_or_tick uint32
event_id             uint16
domain               uint8
severity             uint8
arg0                 uint32
arg1                 uint32
```

1024 条事件约 16 KB，适合后续 RAM ring 和 SD 落盘目标；P0 固件先使用 64 条 ring，优先验证格式、CRC、索引和 fault 落盘闭环。

Trace 固定约束：

- 第一版 `timestamp_us_or_tick` 使用单调 32-bit tick；`tick_hz` 写入 trace header。
- timestamp wrap 由解码工具按事件顺序展开；固件报告中记录是否发生 wrap。
- `crc32` 覆盖全部事件记录，不覆盖 `.idx`；`.idx` 记录 `.bin` 的 size、event_count、crc32 和完整性标志。

P0 trace 已实现事件来源：

- Storage domain：boot snapshot due。
- Trigger domain：SCPI ARM/FAULT 维护事件、TriggerAO queue post/full/null、TriggerFB execute、state change、error change、DMA rollover 进展、ENC Z pulse、资源申请快照、无状态变化的 ignored event。
- SyncIO domain：init、capture start/stop/drop/fail、pulse FIFO full/invalid、clock start/stop/fail、SEQ/ENC arm/disarm/fail、PIO instruction space failure、SEQ gate invalid、SEQ/ENC ARM 成功后的管理面 runtime 采样。
- P0 解码工具：`tools/sd_trace_decode/sd_trace_decode.py`，可校验 `.bin` 头、事件区 CRC 和可选 `.idx`，输出 JSON/CSV。

Runtime 观测约束：

- `sync_io.seq_runtime`、`sync_io.enc_runtime` 只允许在 ARM 成功后的管理面路径或 DISARM/FAULT 后读回路径记录。
- runtime flags 可包含 `running`、`pio_enabled`、`dma_busy`、`dma_irq_enabled`、`tx_fifo_empty`、`tx_fifo_full` 和低 16 位 transfer count。
- 不得在 DMA IRQ handler、PIO IRQ handler、PIO feeding loop 或固定周期硬实时路径中调用 `storage_manager_trace_event()`、LOG、FatFs 或 SD driver。
- 如果后续需要 trigger edge/missed edge、DMA restart/overflow、PIO state、A0-A3 timeout、READY/REDY、resource timeout 等观测，优先选择硬件计数器/状态锁存，随后由管理面或 FAULT/DISARM 后处理读出并落盘。

## 7. 数据分级与对象模型

| 等级 | 数据类型 | 示例 | 存储策略 | 实时约束 |
|---|---|---|---|---|
| L0 当前事实 | RAM Vector / 反射内存 | TriggerVector、OtaVector、StorageVector | RAM 摘要 | 实时读，不写 SD |
| L1 异常现场 | RAM trace ring | 脉冲异常、DMA overflow、READY timeout | RAM ring | 硬实时侧只做必要状态锁存；trace 事件写入不得进入 PIO/DMA/IRQ hot path |
| L2 证据文件 | snapshot/trace/report | fault snapshot、run trace、pulse report | SD | DISARM/FAULT 后写入 |
| L3 系统包 | pack/profile/mission/cal/update | active pack、recipe、cal、OTA pkg | SD | ARM 前读取 |
| L4 工具产物 | manifest/ref/factory/logs | manifest、ref、产测计划、日志 | SD | 可延迟、可失败 |

建议对象类型：

```text
STORAGE_OBJECT_MANIFEST
STORAGE_OBJECT_REF
STORAGE_OBJECT_PACK
STORAGE_OBJECT_PROFILE
STORAGE_OBJECT_MISSION
STORAGE_OBJECT_SEQUENCE
STORAGE_OBJECT_CALIBRATION
STORAGE_OBJECT_SNAPSHOT
STORAGE_OBJECT_TRACE
STORAGE_OBJECT_REPORT
STORAGE_OBJECT_LOG
STORAGE_OBJECT_OTA_PACKAGE
STORAGE_OBJECT_FACTORY_PLAN
STORAGE_OBJECT_FACTORY_RESULT
```

文件生命周期：

| 生命周期 | 适用目录 | 规则 |
|---|---|---|
| immutable | `/packs/pack_xxxxxx` | PC 工具生成，固件默认不修改。 |
| ref | `/refs/*.ref` | 可事务切换，指向 immutable pack。 |
| static | `/profile`、`/mission`、`/cal`、`/update`、`/factory/test_plan.json` | P0 兼容入口或 PC 工具写入。 |
| append-only | `/logs/*.log` | 低频追加，达到大小上限后轮转。 |
| generated | `/snapshots`、`/traces`、`/reports` | 固件生成，单调序号命名，不覆盖旧证据。 |
| volatile-temp | `*.tmp` | 写入中间态，启动扫描时清理或忽略。 |
| index | `*.idx`、`manifest.idx`、后续 `index.json` | 描述文件集合和完整性，先验证再使用。 |

## 8. StorageVector 摘要

后续扩展方向：

```text
state
card_present
fs_mounted
fatfs_available
card_type
capacity_kib
free_kib
current_job
job_state
job_object_class
job_path_hash
job_total_bytes
job_done_bytes
job_crc32
active_pack_id
active_pack_crc32
candidate_pack_id
candidate_pack_crc32
previous_pack_id
factory_pack_id
pack_state
pack_error
last_snapshot_id
last_snapshot_path_hash
last_trace_id
last_trace_path_hash
last_report_id
last_report_path_hash
last_error_domain
last_error_code
last_fresult
last_event_sequence
```

StorageVector 不保存完整长路径、JSON 文本、trace buffer 或文件内容。完整路径由 StorageAO 内部固定缓冲或最近路径表保存；SCPI/UI 需要完整路径时通过 StorageAO 查询接口返回裁剪后的字符串。

## 9. StorageFB 状态机

建议第一版状态：

```text
UNINITIALIZED
IDLE
CARD_UNKNOWN
NO_CARD
CARD_DETECTED
PROBING
MOUNTING
MOUNTED
PACK_SCANNED
PACK_VALIDATING
PACK_CHECKOUT
PACK_ROLLBACK
SCANNING
READING
WRITING
SYNCING
JOB_DONE
JOB_FAILED
REMOVED
FAULT
```

关键规则：

- `PROBING/MOUNTED` 只更新 StorageVector，不触发跨域配置。
- `PACK_VALIDATING` 只校验 candidate，不影响 active。
- `PACK_CHECKOUT` 只能在 `BOOT/IDLE/MAINTENANCE` 或 ARM 前执行。
- `PACK_ROLLBACK` 只响应显式请求，第一版不自动触发。
- `WRITE_TRACE/WRITE_REPORT` 可以在 `DISARM/FAULT` 后执行；ARM 后禁止 SD 写入，硬实时面只锁存必要状态，RAM trace ring 追加也必须服从 PIO/DMA/IRQ hot path 约束。
- 所有 `READING/WRITING/SYNCING` 都必须分片执行，单次 service 超预算后让出主循环。
- P0 优先把 `PROBE/CATALOG/FILE_INFO/SCAN_MANIFEST` 收敛为 StorageAO job，SCPI/UI 只读取最近结果或投递请求；不得在 SCPI 回调内长时间同步 mount、枚举目录或读大文件。
- StorageFB/StorageAO 不得被 PIO/DMA/IRQ handler 直接调用；硬实时面只发布最小状态，Storage 域在非实时上下文读取快照并生成证据。

## 10. Storage 事件

第一阶段建议事件：

```text
STORAGE_EVENT_PROBE
STORAGE_EVENT_CATALOG
STORAGE_EVENT_FILE_INFO
STORAGE_EVENT_SCAN_SYSTEM_PACK
STORAGE_EVENT_SCAN_PACK_REFS
STORAGE_EVENT_VALIDATE_CANDIDATE_PACK
STORAGE_EVENT_CHECKOUT_PACK
STORAGE_EVENT_ROLLBACK_PACK
STORAGE_EVENT_LOAD_PROFILE
STORAGE_EVENT_LOAD_CALIBRATION
STORAGE_EVENT_WRITE_SNAPSHOT
STORAGE_EVENT_WRITE_TRACE
STORAGE_EVENT_WRITE_REPORT
STORAGE_EVENT_OFFLINE_OTA
STORAGE_EVENT_ABORT_JOB
```

## 11. 命名、序号与事务

自动生成文件使用单调序号：

```text
/snapshots/boot/boot_000001.json
/snapshots/arm/arm_000001.json
/snapshots/fault/fault_000001.json
/traces/run/run_000001.bin
/traces/run/run_000001.idx
/reports/fault/pulse_fault_000001.json
```

序号来源：

- 第一阶段启动时扫描目标子目录最大序号，下一次写入使用 `max + 1`。
- 后续引入 `/snapshots/index.json`、`/traces/index.json`、`/reports/index.json` 后，优先使用 index 中的 `next_sequence`，并与目录扫描结果取较大值。
- 如果扫描失败，StorageAO 不得覆盖已有文件；应进入 `STORAGE_ERR_SEQUENCE_ALLOC_FAILED` 或退化为只读。
- UTC 时间只能作为文件内容字段，不作为唯一文件名依据。

单文件事务：

```text
allocate sequence
write payload to .tmp
flush/sync
close
rename .tmp -> final
publish StorageVector result
```

Ref 切换事务优化目标：

```text
write candidate.ref.tmp
flush/sync/close
rename candidate.ref.tmp -> candidate.ref
validate candidate pack
read current active.ref as previous payload
write previous.ref.tmp
write active.ref.tmp with candidate payload
flush/sync/close both tmp files
rename previous.ref -> previous.ref.bak if previous.ref exists
rename previous.ref.tmp -> previous.ref
rename active.ref -> active.ref.bak
rename active.ref.tmp -> active.ref
publish checkout done after active.ref re-read and validation succeeds
```

事务约束：

- 切换前必须完成 candidate pack 校验，校验失败不得改动 `active.ref`。
- 不依赖覆盖式 rename；目标 final 已存在时先改名为 `.bak`，再 rename `.tmp`。
- `active.ref` 被改名后到新 `active.ref` 成功落盘之间是唯一高风险窗口，启动恢复必须优先用 `active.ref.bak` 恢复旧 active，而不是自动提交 `active.ref.tmp`。
- checkout 成功的判据是新 `active.ref` 重新读取并校验通过；只有此后才允许向 TriggerAO 投递配置更新事件。

恢复规则：

- `.tmp` 文件不得被视为有效证据。
- 自动生成文件默认不覆盖已有 final 文件；如果 final 已存在，必须重新分配 sequence。
- trace `.bin` 先写，trace `.idx.tmp` 后写，`.idx` rename 成功后 trace 才视为完整。
- `active.ref` 有效则继续使用 active。
- `active.ref` 缺失或无效但 `active.ref.bak` 有效时，先恢复 `active.ref.bak` 为 active；本次 checkout 视为未完成。
- `active.ref` 和 `active.ref.bak` 都无效但 `previous.ref` 有效则退回 previous。
- previous 也无效则尝试 factory。
- factory 无效则 System Pack 不可用，但基本触发功能仍应可运行。
- 启动清理 `.tmp/.bak` 前必须先完成 ref 解析；未被选中的 `*.ref.tmp` 只能删除或保留诊断，不得自动提升为 active。

可重复更新的索引文件不直接覆盖：

```text
write index.tmp
flush/sync/close
rename old index.json -> index.bak
rename index.tmp -> index.json
```

启动恢复时 `index.json` 失败则尝试 `index.bak`，再失败才扫描目录重建最近摘要。

## 12. 路径、安全、完整性

所有来自 SCPI、UI、manifest/ref 的路径都必须规范化：

- 统一 `/path/file`，内部转换为 FatFs 所需路径。
- 拒绝空路径之外的相对路径。
- 拒绝 `..`。
- 拒绝控制字符。
- 限制最大路径长度。
- 限制单次目录枚举输出长度。
- 固件默认只允许访问白名单根目录。

白名单：

```text
/manifest.json
/manifest.idx
/refs/
/packs/
/profile/
/mission/
/cal/
/snapshots/
/traces/
/reports/
/logs/
/update/
/factory/
```

兼容策略：

- schema 相同：直接支持。
- schema 较旧：兼容读取，必要时提示 upgrade。
- schema 较新：拒绝应用到 Trigger/Ota，只允许目录查询。
- mode_schema 相同：直接支持该模式的已知字段。
- mode_schema 较旧：兼容读取，并可由 PC 工具升级 recipe/profile。
- mode_schema 较新或 mode 未知：拒绝 ARM/checkout，只允许目录查询、`MMEM:INFO?`、`MMEM:READ?` 和 PC 工具导出。
- optional capability 未知：忽略并记录 warning；required capability 未知：拒绝 checkout。
- product_id/hardware_id 不匹配：拒绝加载。
- `/update/*.pkg` 以 OTA package header 为准；manifest 只作为发现和预检查入口。

## 13. 错误模型

```text
STORAGE_ERR_NONE
STORAGE_ERR_NO_CARD
STORAGE_ERR_MOUNT_FAILED
STORAGE_ERR_PATH_DENIED
STORAGE_ERR_PATH_NOT_FOUND
STORAGE_ERR_SCHEMA_UNSUPPORTED
STORAGE_ERR_PRODUCT_MISMATCH
STORAGE_ERR_CRC_MISMATCH
STORAGE_ERR_LOW_SPACE
STORAGE_ERR_IO_TIMEOUT
STORAGE_ERR_WRITE_FAILED
STORAGE_ERR_RENAME_FAILED
STORAGE_ERR_INCOMPLETE_TRACE
STORAGE_ERR_SEQUENCE_ALLOC_FAILED
STORAGE_ERR_PACK_NOT_FOUND
STORAGE_ERR_PACK_INVALID
STORAGE_ERR_REF_INVALID
STORAGE_ERR_CHECKOUT_DENIED
STORAGE_ERR_BUSY_ARMED
```

FatFs `FRESULT` 和 SD driver status 不应直接泄漏到上层语义。StorageAO 映射为稳定错误码，同时在 snapshot/report 中保留底层 raw code 方便调试。

## 14. 容量、配额、保留

| 目录 | 初始建议配额 | 保留策略 |
|---|---|---|
| `/packs` | 512 MB | 保留 factory、active、previous、candidate 和最近 N 个历史 pack。 |
| `/snapshots` | 16 MB | 每类保留最近 128 个。 |
| `/traces` | 256 MB | 保留最近 256 个 run/fault trace，或按剩余空间裁剪。 |
| `/reports` | 64 MB | 保留最近 512 个报告。 |
| `/logs` | 16 MB | 单文件 1 MB 轮转，保留 4 份。 |
| `/update` | 64 MB | P0 默认包 + 少量历史包。 |
| `/factory` | 32 MB | 产测结果按批次归档。 |

低空间策略：

- 禁止生成新的大 trace，只写小型 fault snapshot。
- StorageVector 发布 `LOW_SPACE`。
- UI 显示 SD 低空间状态。
- SCPI 返回可读错误，而不是在写文件中途失败。

## 15. 热插拔与失效处理

```text
CARD_UNKNOWN
NO_CARD
CARD_DETECTED
MOUNTING
MOUNTED
PACK_SCANNED
REMOVED
FAILED
```

规则：

- ARM 中拔卡：不影响实时触发；只锁存 `SD_REMOVED_DURING_RUN`。
- 写 trace 中拔卡：job failed，RAM trace ring 尽量保留。
- checkout 中拔卡：不提交 TriggerVector 配置，active ref 不应被破坏。
- 读 profile/cal 中拔卡：加载失败，不提交 TriggerVector 配置。
- 离线 OTA 读包中拔卡：中止 Storage job，投递 OtaAO abort/error。
- 无卡启动：系统仍可基本运行，只是 System Pack 功能不可用。

## 16. SD 离线 OTA 约束

离线 OTA 是 SD 的子功能，不是 SD 的唯一目标。

- 默认只接受统一 `.pkg`，默认路径 `/update/RP2350_TRIG_UPDATE.pkg` 或 active pack 内的 OTA package。
- raw `.bin` 只保留兼容/台架路径，不作为 SD release 默认格式。
- 读取 `.pkg` 前检查 package magic、header size、package size、image slot、run offset、CRC/header。
- 离线 OTA 必须先确认 Trigger 未 armed、未 capture、未输出同步时钟。
- SD 文件读取转换为现有 OtaAO 事件流：`BEGIN/DATA_BLOCK/END/BOOT/COMM`。
- SD->OTA 数据块必须使用 fixed bounce buffer，并由 event_bus/OtaAO 复制 payload；不得把即将复用的 SD 读缓冲指针投递给 OtaAO。
- 读 SD 文件持有 `SPI0 + SD`，写 Flash 持有 `FLASH`，不得长时间同时持有 `SPI0 + SD + FLASH`。

推荐流水：

```text
read SD small block into bounce buffer
release SPI0 + SD
post/copy OTA data event
OtaAO writes/verifies flash slice
repeat
```

## 17. 调度与资源预算

- `StorageAO` 默认按 background/low priority 运行，单次 service 预算建议 500-2000 us。
- SD block 读写以 512 B 或小批量 sector 为单位分片。
- LCD 刷新持有 `SPI0 + LCD`，SD 读写持有 `SPI0 + SD`，二者互斥。
- LCD 需要刷新但 SD 正在读写时，LCD 跳过本轮并保持 dirty。
- 脉冲异常实时采集只做必要状态锁存；RAM trace ring 追加和 SD 落盘都属于非 hot path 后处理。
- PIO/DMA/IRQ 的实时输出优先级高于所有 SD/trace/report 工作；任何新增观测点必须先证明不会拉长 IRQ、阻塞 DMA feeding 或改变 PIO 时序。

## 18. PC 工具链

PC 工具负责重活：

- 生成完整目录、manifest、默认 profile/cal/mission/update。
- 生成 `/packs/pack_xxxxxx` 和 `/refs/*.ref`。
- 计算 SHA256/CRC32。
- 比较两个 pack 的差异。
- 从工程配置导出 profile/mission/cal。
- 检查 manifest/ref 一致性。
- 清理过旧 pack。
- 解析 `.bin + .idx` trace。

建议工具：

```text
tools/sd_fs_build/
tools/sd_pack_build/
tools/sd_pack_check/
tools/sd_pack_diff/
tools/sd_trace_decode/
tools/bench/rp2350_tk_toolbox.py
```

## 19. 实现路线

### P0A - 固定目录与 Manifest

- [x] `sd_fs_build.py` 迁移到第 4 节固定目录，默认生成 `/profile`、`/mission`、`/cal`、`/snapshots`、`/traces`、`/reports`、`/logs`、`/update`、`/factory`。
- [x] `sd_fs_build.py` 生成同源 `manifest.json`、`manifest.idx`；旧 `/config`、`/capture`、`/resource` 只允许显式兼容选项。
- [x] 板端 StorageAO 扫描 `manifest.idx`，结果发布到 StorageVector 摘要。
- [x] 检查 required 文件存在性和可选 CRC。
- [x] 实现路径规范化、目录白名单和文件信息查询；SCPI/UI/manifest/ref 路径共用同一入口。
- [x] 增加 `MMEM:INFO? "<path>"`，用于 evidence 目录增长导致 `MMEM:CAT?` 截断时稳定确认单个文件存在、大小、类型和路径 hash。
- [x] 增加 `MMEM:CAT:PAGE? "<path>",offset,limit`，返回 `next_offset/complete/truncated`，避免长目录一次性输出截断。
- [x] 增加 `MMEM:READ? "<path>",offset,length` 受限文件片段读取，复用白名单路径策略，用于板端验证读回 trace `.bin/.idx`。
- [x] 增加 StorageAO `FILE_INFO` job 最小闭环：`SYST:STOR:JOB:INFO "<path>"` 投递，`SYST:STOR:JOB?` 查询结果，FatFs 查询在 `storage_manager_service()` 中执行。
- [x] 将 `MMEM:READ?` 迁移为 StorageAO `FILE_READ` job，旧返回字段保持兼容，`SYST:STOR:JOB?` 可验证 `DONE/FILE_READ/READ/error=0`。
- [x] 将 `SYST:SD:MAN?` 迁移为 StorageAO `MANIFEST_SCAN` job，旧 manifest 摘要字段保持兼容，`SYST:STOR:JOB?` 可验证 `DONE/MANIFEST_SCAN/MANIFEST/error=0`。
- [x] 将 `MMEM:CAT?` 旧接口迁移为分页包装兼容诊断接口：内部只返回 `MMEM:CAT:PAGE?` 第 0 页最多 16 项；可靠长目录枚举必须使用分页命令。
- [x] 将 `MMEM:CAT:PAGE?` 和 `MMEM:CAT?` 迁移为 StorageAO `CATALOG_PAGE` job，旧返回字段保持兼容，`SYST:STOR:JOB?` 可验证 `DONE/CATALOG_PAGE/CATALOG/error=0`。
- [x] 增加 Pico 侧维护格式化入口 `SYST:SD:MKFS "ERASE"` 和 raw sector 读回诊断 `SYST:SD:RAW:READ?`，用于原始卡/主机不识别卡绕过 PC 侧格式化。
- [x] 增加 Pico 侧非破坏性 System Pack 初始化入口 `SYST:SD:INIT`；已挂载 FAT32 卡缺 `/manifest.idx` 时自动创建最小目录、默认 profile/mission/cal、占位 `/update/RP2350_TRIG_UPDATE.pkg`、`manifest.json` 和 `manifest.idx`，`SYST:SD:MAN?` 会在 `NOT_FOUND` 时自动执行同一初始化后重扫。
- [x] `MMEM:INFO?` 兼容查询迁移为 StorageAO `FILE_INFO` job；`MMEM:CAT*`、`MMEM:READ?`、`MMEM:INFO?`、`SYST:SD:MAN?`、`SYST:SD:INIT`、`SYST:SD:MKFS`、`SYST:SD:RAW:*` 和手动 `SYST:SNAP:WRITe` 在 Trigger armed 时拒绝执行，避免 ARM 后等待 SD/FatFs。
- [x] `MANIFEST_SCAN`、`SYSTEM_INIT`、`FAULT_EVIDENCE` 与 boot snapshot 自动初始化路径拆成多轮 Storage service 推进；单个 FatFs 调用仍为同步边界，但不再在同一 service 周期连续执行多个 SD/FatFs 动作。
- [ ] 使用已知可写 SD 卡复验 Pico 侧 MKFS；当前异常卡 `f_mkfs` 返回成功但 sector 0 读回仍全 0，判断为介质写入不落盘，不能作为 SD System Pack 闭环介质。
- [x] 在有效 FAT32 SD 卡上完成 `FILE_READ + CATALOG_PAGE` 板端闭环复验：build id `20260706135037`，验证目录 `build-sd-goodcard\sd_validation_file_read_catalog_goodcard`，`READ:SYST:STOR:JOB?` 与 `PAGE:SYST:STOR:JOB?` 均为 `DONE/.../error=0`。
- [x] 在新 FAT32 SD 卡上完成 Pico 侧自动构建最小 System Pack 闭环：build id `20260706152725`，验证目录 `build-sd-goodcard\sd_validation_auto_bootstrap_goodcard`，`SYST:SD:INIT -> "OK","OK",1,"20260706152725",4,0,0`，根目录自动生成 `/profile`、`/mission`、`/cal`、`/logs`、`/update`、`/factory`、`manifest.idx` 和 `manifest.json`。
- [x] SD UI 显示 manifest/pack 状态、默认 OTA 包存在性和 SD 错误摘要。

### P0B - Vector 快照

- [x] 定义 snapshot JSON 固件结构体和序列分配。
- [x] 写入 boot snapshot：`/snapshots/boot/boot_XXXXXX.json`。
- [x] 写入 ARM 前 snapshot：`/snapshots/arm/arm_XXXXXX.json`。
- [x] 写入 fault snapshot：`/snapshots/fault/fault_XXXXXX.json`。
- [x] 增加最近 snapshot 查询：`SYST:SNAP:LAST?`。
- [x] 手动 `SYST:SNAP:WRITe` 迁移为 StorageAO `SNAPSHOT_WRITE` job，旧命令返回语义保持兼容。
- [x] `TRIG:ARM` 的 arm snapshot 迁移为 StorageAO `SNAPSHOT_WRITE` job，SCPI 回调不直接执行 FatFs 写入。
- [x] `TRIG:FAULT` 改为先投递 Trigger fault，再投递 `FAULT_EVIDENCE` job；fault snapshot/trace/report 在 FAULT 后由 StorageAO 分轮后台生成，SCPI 回调不等待 SD。
- [x] UI 显示最近 fault snapshot id/hash。

### P0C - 脉冲异常追溯

- [x] 增加 RAM trace ring，固定事件记录 16 字节。
- [x] 记录 TriggerAO queue post/full/null、TriggerFB execute、state change/error change、DMA rollover 进展、ENC Z pulse。
- [x] 记录 Trigger 配置变更：source、edge、gate、safe，并在解码器中输出 before/after 细节。
- [x] 增加 Trigger 资源/底层 I/O 失败 trace 事件名：resource busy、I/O arm failed、I/O lost。
- [x] 记录 SyncIO 管理面事件：init、capture drop、pulse FIFO full、clock、SEQ/ENC arm/disarm/fail。
- [x] 记录 SyncIO 管理面 runtime 采样：SEQ/ENC ARM 成功后输出 PIO enabled、DMA busy、DMA IRQ enabled、TX FIFO 和 transfer count 摘要。
- [x] 按 HAOFV 约束补齐 TriggerAO 管理面 `runtime_sample`：ARM 成功时记录 state/edge/gate 与进度摘要，后续只在进度、rollover、ENC Z 或错误变化时记录；不进入 PIO/DMA/IRQ hot path。
- [x] 按 HAOFV 约束补齐 SEQ PIO state 与 DMA restart 管理面观测：`sync_io.seq_pio_state`、`sync_io.seq_dma_restart` 已进入 fault trace 解码和板端验证，不进入 PIO/DMA/IRQ hot path。
- [x] 按 HAOFV 约束补齐 resource timeout/资源申请失败第一阶段观测：`trigger.resource_snapshot` 在 ARM 成功和资源申请失败时记录 requested resources、active resources 和 arbiter mode；不等待资源、不改变非阻塞 acquire 语义、不进入 PIO/DMA/IRQ hot path。
- [x] 按 HAOFV 约束补齐 DMA overflow 第一阶段管理面观测：`sync_io.seq_dma_overflow`、`sync_io.enc_dma_overflow` 由 runtime sample 对 restart/rollover 增量进行 baseline/异常锁存，不在 DMA IRQ hot path 写 trace/log/SD。
- [x] 按 HAOFV 约束补齐 A0-A3 timeout、READY/REDY 第一阶段管理面观测：`sync_io.aux_snapshot`、`sync_io.ready_redy`、`sync_io.aux_timeout` 已进入 fault trace 解码和板端验证；当前只提供 AUX0..AUX3/READY/REDY 采样与 timeout latch 基线，不在 PIO/DMA/IRQ hot path 写 trace/log/SD。
- [x] 将 expected READY mask 从 SyncIO 编译期常量收口为管理面 runtime 状态：默认 mask 仍为 `0`，后续 `/mission/node_map.json` loader 可通过 setter 接入，不需要在 SyncIO 底层硬编码 A0-A3 业务策略。
- [ ] 后续加载 `/mission/node_map.json` 后，把 A0-A3 角色、期望 READY mask 和业务 timeout 策略接入运行时对象；不得在 SyncIO 底层硬编码业务角色策略。
- [x] DISARM/FAULT 后写 `/traces/*/*.bin` 和 `.idx`。
- [x] 生成 `/reports/fault/pulse_fault_XXXXXX.json`。
- [x] 增加查询：`SYST:TRAC:LAST?`、`SYST:FAULT:LAST?`。
- [x] 增加离线 trace 解码工具：`tools/sd_trace_decode/sd_trace_decode.py`。
- [x] SD 板端验证工具通过 `MMEM:READ?` 读回最新 fault trace `.bin/.idx`，并调用解码器校验 header、CRC、idx 和事件数量。
- [x] `TRIG:FAULT` 的 fault snapshot/trace/report 后处理迁移为 StorageAO `FAULT_EVIDENCE` job，SCPI 回调不直接执行 FatFs 写入。

### P1A - Pack/Ref

- [ ] 增加 `/packs/pack_xxxxxx/` 和 `/refs/*.ref` 目录支持。
- [ ] 支持 `active/previous/factory/candidate` ref 查询。
- [ ] candidate pack 校验通过后才允许 checkout。
- [ ] Pack manifest 增加 `mode/mode_schema/capability/min_firmware` 兼容性校验，未知必需模式不得 checkout。
- [ ] 实现 Ref 切换事务和启动恢复：`active.ref.bak` 优先恢复旧 active，`*.ref.tmp` 不自动提升为 active。
- [ ] checkout 成功后通过事件更新 TriggerAO 配置和校准。
- [ ] snapshot/report 记录 active pack id/hash。
- [ ] PC 工具生成 pack、ref、pack diff 和一致性检查。
- [ ] PC 工具输出 pack 兼容性报告：固件最低版本、必需能力、必需文件、CRC/hash、是否可由 P0 兼容入口镜像。

### P1B - Profile/Calibration

- [ ] 加载 active pack 或 P0 兼容路径中的 profile/cal。
- [ ] 校验 schema/product/hardware/hash。
- [ ] 解析 `active_mode.mode_id/mode_name/mode_schema/capability_flags`，未知必需 mode/capability 拒绝 ARM。
- [ ] ARM 前把配置转换为 TriggerAO/TriggerVector 事件和摘要。
- [ ] 拒绝 armed 状态下从 SD 重新加载关键配置。

### P1C - Mission/Recipe

- [ ] 加载 active pack 或 P0 兼容路径中的 mission。
- [ ] 定义 mode-neutral recipe 装载入口：`mode/mode_schema/sequence/nodes/calibration`，禁止新增模式专属根目录。
- [ ] 加载 `/mission/sequence.bin`。
- [ ] 加载 `/mission/node_map.json`，支持 A0-A3 角色与测试流程部署。
- [ ] 针对新增 TRIG mode 增加 recipe 负向验证：未知 mode、mode_schema 较新、缺 required capability、CRC 错误。
- [ ] 生成 run report。

### P1D - 离线 OTA

- [ ] 增加 `SYST:OTA:FILE "<path>"` 或等价 Storage 事件。
- [ ] 支持 active pack 内 OTA package。
- [ ] SD package -> inactive slot -> `READY_TO_REBOOT` -> `BOOT` -> `COMM` 正向闭环。
- [ ] 负向验证：坏 magic、坏 package CRC、坏 image CRC、坏 vector、读文件中拔卡、空间/路径错误。

### P2A - 报告、产测、工具

- [ ] `/reports/run/` 写运行摘要和 CSV。
- [ ] `/factory/test_plan.json` 驱动产测计划。
- [ ] `/factory/result.json` 写回验收结果。
- [ ] `sd_pack_check`、`sd_pack_diff`、`sd_trace_decode`。
- [ ] release 归档保存 `RP2350_TRIG_SDCARD.zip`、manifest、报告模板。

### P2B - 长稳与掉电恢复

- [ ] 多卡兼容：FAT32 小容量卡、SDHC、SDXC、空卡、已有系统目录卡。
- [ ] 热插拔：无卡启动后插卡、挂载后拔卡、checkout 中拔卡、OTA 读文件中拔卡。
- [ ] 长时间运行：UI 刷新 + SD 探测、SCPI 高频查询 + 目录枚举、Trigger 运行时 SD 查询不影响硬实时路径。
- [ ] 掉电：写文件中断电、trace `.bin` 已写但 `.idx` 未写、ref 切换中断电、离线 OTA 读包中断电、`COMM` 前后断电。

## 20. 当前基线

- [x] SD 卡纳入 HAOFV 管理域：底层驱动只负责 SPI/block，FatFs port 只负责文件系统适配，`storage_manager` 发布状态快照。
- [x] 新增 SD SPI 模式底层驱动：`drivers/external/sd_card/`。
- [x] 新增 FatFs 适配层：`middleware/fatfs_port/`。
- [x] 新增存储管理组件：`components/storage_manager/`。
- [x] App 初始化和周期服务接入 `storage_manager_init()` / `storage_manager_service()`。
- [x] SCPI 查询接入：`SYST:SD:STAT?`、`SYST:SD:INFO?`、`SYST:STOR:STAT?`、`MMEM:CAT?`、`MMEM:CAT? "/update"`。
- [x] SD 卡文件系统 staging 工具接入：`tools/sd_fs_build/sd_fs_build.py`。
- [x] SD 独立 LCD 页面接入，UI 只读取 `storage_manager_vector_t` 快照。
- [x] SD UI 显示 manifest/System Pack、默认 OTA、StorageAO job、snapshot/fault evidence 和错误摘要。
- [x] LCD 顶部 tab 改为 3 槽滚动式 tabview，KEY2/GPIO2 可切到 SD 页面。

## 21. 当前验证记录

本节只保存最新一次 SD 闭环验证摘要；历史验证记录写入 `docs/SD_TASK_PROGRESS.md`。

- 日期：2026-07-06
- 任务记录：`SD-TASK-20260706-031`
- 构建目录：`build-sd-ready-mask`
- build id：`20260706162327`
- 烧录固件：`build-sd-ready-mask\RP2350_TRIG_FACTORY.uf2`
- 验证目录：`build-sd-ready-mask\sd_validation_ready_mask`
- 验证结果：`PASS`
- 构建与 release gate：
  - `python -m py_compile tools\sd_trace_decode\sd_trace_decode.py tools\sd_board_validate\sd_board_validate.py` 通过。
  - `python tools\cmake_build_auto\cmake_build_auto.py --preset pico2-release --build-dir build-sd-ready-mask` 通过。
  - `python tools\release_check\release_check.py --preset pico2-release --build-dir build-sd-ready-mask` 通过，`release_check=OK`。
  - 已使用 picotool 烧录 `build-sd-ready-mask\RP2350_TRIG_FACTORY.uf2`，Flash verify 三段均 OK；`sd_board_validate.py` 只做 SCPI 板端验证，不负责烧录。
- SD 基础查询：
  - `SYST:SD:STAT? -> "CARD_READY",1,1,"OK",0`
  - `SYST:SD:INFO? -> "CARD_READY","SDHC_SDXC",1,61067264,30533632,1,1,1`
  - `SYST:SD:INIT -> "OK","OK",1,"20260706152725",4,0,0`
  - `SYST:SD:MAN? -> "OK",1,"RP2350_TRIG","rp2350_trig","20260706152725",4,0`
  - `SYST:STOR:STAT? -> "CARD_READY",1,1,"OK",0`
- 负向路径验证：
  - `MMEM:CAT? "/../" -> "PATH_DENIED","PATH_DENIED"`
  - `MMEM:INFO? "/../" -> "PATH_DENIED","/../",0,"UNKNOWN",0,5`
  - `MMEM:CAT:PAGE? "/../",0,4 -> "PATH_DENIED","/../",0,0,0,0,0,"PATH_DENIED"`
  - `MMEM:READ? "/../",0,16 -> "PATH_DENIED","/../",0,16,0,0,0,5,""`
- StorageAO job 验证：
  - `INIT:SYST:STOR:JOB? -> "DONE",1,"SYSTEM_INIT","/manifest.idx",4,"MANIFEST",3822083274,0`
  - `MAN:SYST:STOR:JOB? -> "DONE",2,"MANIFEST_SCAN","/manifest.idx",4,"MANIFEST",3822083274,0`
  - `SYST:STOR:JOB:INFO "/manifest.idx" -> "OK"`
  - `SYST:STOR:JOB? -> "DONE","FILE_INFO","/manifest.idx",...,"FILE",...,0`
  - `SYST:SNAP:WRIT "boot" -> "OK"`
  - `SNAP:SYST:STOR:JOB? -> "DONE",12,"SNAPSHOT_WRITE","/snapshots/boot/boot_000058.json",58,"SNAPSHOT",2528692015,0`
  - `ARM:SYST:STOR:JOB? -> "DONE",14,"SNAPSHOT_WRITE","/snapshots/arm/arm_000029.json",29,"SNAPSHOT",3137737441,0`
  - `FAULT:SYST:STOR:JOB? -> "DONE",17,"FAULT_EVIDENCE","/reports/fault/pulse_fault_000026.json",26,"FAULT_EVIDENCE",1745620374,0`
  - `PAGE:SYST:STOR:JOB? -> "DONE",30,"CATALOG_PAGE","/traces/fault",2,"CATALOG",1962968327,0`
  - `READ:SYST:STOR:JOB? -> "DONE",38,"FILE_READ","/traces/fault/fault_000027.idx",17,"READ",1583248467,0`
- MMEM 目录兼容验证：
  - 根目录由 Pico 侧非破坏性初始化生成：`profile`、`mission`、`cal`、`logs`、`update`、`factory`、`manifest.idx`、`manifest.json`。
  - `MMEM:CAT? "/update" -> "OK","compat,0,DIR;RP2350_TRIG_UPDATE.pkg,88,FILE;"`
  - `MMEM:CAT? "/profile" -> "OK","profiles,0,DIR;active.json,247,FILE;"`
  - `MMEM:CAT? "/mission" -> "OK","recipe.json,215,FILE;node_map.json,273,FILE;"`
  - `MMEM:CAT? "/cal" -> "OK","board_cal.json,230,FILE;"`
- Snapshot / trace / report：
  - `ARM:SYST:STOR:JOB? -> "DONE",17,"SNAPSHOT_WRITE","/snapshots/arm/arm_000009.json",9,"SNAPSHOT",3863456945,0`
  - `FAULT:SYST:STOR:JOB? -> "DONE",20,"FAULT_EVIDENCE","/reports/fault/pulse_fault_000009.json",9,"FAULT_EVIDENCE",775850829,0`
  - `FAULT:SYST:TRAC:LAST? -> "OK","fault",9,"/traces/fault/fault_000009.bin",636190657,53,0`
  - `MMEM:CAT? "/snapshots/fault" -> "OK","fault_000001.json,427,FILE;fault_000002.json,456,FILE;fault_000003.json,456,FILE;fault_000004.json,456,FILE;"`
  - `MMEM:CAT? "/traces/fault" -> "OK","fault_000001.bin,740,FILE;fault_000001.idx,144,FILE;fault_000002.bin,740,FILE;fault_000002.idx,144,FILE;fault_000003.bin,756,FILE;fault_000003.idx,144,FILE;fault_000004.bin,772,FILE;fault_000004.idx,144,FILE;"`
  - `MMEM:CAT? "/reports/fault" -> "OK","pulse_fault_000001.json,529,FILE;pulse_fault_000002.json,535,FILE;pulse_fault_000003.json,535,FILE;pulse_fault_000004.json,536,FILE;"`
- Trace 读回与解码：
  - `.bin` 通过 `MMEM:READ?` 读回 `884/884` 字节。
  - `trace_readback\decoded_fault_trace.json`：`magic_ok=true`、`schema_ok=true`、`size_ok=true`、`crc_ok=true`、`idx_ok=true`。
  - 解码事件数 `53` 与 `SYST:TRAC:LAST?` 一致，包含 `sync_io.seq_runtime`、`sync_io.seq_pio_state`、`sync_io.seq_dma_restart`、`sync_io.seq_dma_overflow`、`sync_io.aux_snapshot`、`sync_io.ready_redy`、`sync_io.aux_timeout`、`trigger.runtime_sample` 和 `trigger.resource_snapshot`。
  - `sync_io.aux_timeout` 当前为 baseline/latch 基础设施；真实期望 READY mask 仍等待 `/mission/node_map.json` runtime 提供，不在 SyncIO 底层硬编码 A0-A3 业务策略。
- HAOFV 实时性确认：
  - 新增 System Pack 初始化只在 Storage 管理面执行，不进入 PIO/DMA/IRQ hot path。
  - SD UI 只读取 `StorageVector` 摘要，未在 UI 路径新增 SD/FatFs/trace/log 调用。
  - runtime trace、DMA overflow 和 AUX/READY/REDY baseline/异常锁存只在 ARM 成功后的管理面路径或后处理采样记录；DMA IRQ handler 仍只更新已有计数和 DMA 寄存器。
  - PIO/DMA/IRQ hot path 未加入 SD、FatFs、日志或 trace 写入。

## 22. 架构边界

- SD 卡作为 App 侧 System Pack/持久化观测介质，不参与 Bootloader 最小启动链路。
- Bootloader 不集成 SD/FatFs/UI/SCPI。
- SD 文件输入、SCPI 输入、UI 输入都只能投递事件，不能直接修改 OTA 或 Trigger 域状态。
- `storage_manager` 只发布存储域快照，不保存大文件缓存到 Vector。
- 大文件数据块不进入 Vector，只记录 id、path hash、大小、CRC、进度和错误摘要。
- LCD 与 SD 共享 SPI0，所有访问必须通过 Resource Arbiter 串行化。

## 23. 关联文件

- `tools/sd_fs_build/sd_fs_build.py`
- `drivers/external/sd_card/inc/sd_card.h`
- `drivers/external/sd_card/src/sd_card.c`
- `middleware/fatfs_port/inc/fatfs_port.h`
- `middleware/fatfs_port/src/fatfs_port.c`
- `middleware/fatfs_port/src/fatfs_diskio.c`
- `components/storage_manager/inc/storage_manager.h`
- `components/storage_manager/src/storage_manager.c`
- `components/sync_config_ui/src/sync_config_ui.c`
- `middleware/scpi_port/src/scpi_port.c`
- `tools/bench/rp2350_tk_toolbox.py`
- `docs/OTA_SYSTEM_DESIGN.md`
- `docs/HAOFV_ARCHITECTURE.md`
- `docs/TASK_PROGRESS.md`
