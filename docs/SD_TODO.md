# SD 卡 System Pack 规划与待办

本文档定义 RP2350_TRIG 的 SD 卡系统。SD 卡不是简单 OTA 介质，而是 App 侧 **System Pack 介质 + 持久化观测层**，用于任务配置、校准补偿、Pack/Ref 版本管理、Vector/反射内存快照、脉冲异常 trace、运行报告、产测结果和离线 OTA。

Bootloader 第一版不读取 SD/FatFs。SD 卡可插拔，文件系统和写入延迟都不适合进入最小启动链路，也不能进入 PIO/DMA/IRQ 硬实时触发闭环。

## 1. 核心定位

```text
RAM Vector / 反射内存 = 当前实时事实
RAM trace ring        = 最近事件与脉冲异常现场
SD System Pack        = 历史事实、任务包、校准包、证据包
```

原则：

- SD 卡可以在 `BOOT`、`IDLE`、ARM 前、`DISARM` 后、`FAULT` 后读取或写入。
- ARM 后硬实时阶段不得等待 SD/FatFs。
- 实时异常只写 RAM trace ring，之后由 `StorageAO` 分片落盘。
- SCPI/UI/SD 文件输入都只能投递事件，不能直接修改 Trigger/Ota/Storage 域状态。
- Vector 不保存大块数据；Vector 只记录 id、hash、size、CRC、进度和错误摘要。
- 证据文件必须使用临时文件、索引和完整性标志，避免掉电后把半文件当成有效证据。
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

## 4. SD 文件架构

根目录固定小写。P0 可以使用根目录默认文件；P1 引入 `/packs + /refs` 后，根目录默认文件可作为 active pack 的兼容镜像。

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

旧规划中的 `/config` 映射为 `/profile + /mission`，`/capture` 映射为 `/traces + /reports`，后续不再新增独立根目录。

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
required=/profile/active.json,type=profile,crc32=00000000
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
- 复杂 hash、签名、版本矩阵由 PC 工具校验。

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

1024 条事件约 16 KB，适合 RAM ring 和 SD 落盘。

## 7. 数据分级与对象模型

| 等级 | 数据类型 | 示例 | 存储策略 | 实时约束 |
|---|---|---|---|---|
| L0 当前事实 | RAM Vector / 反射内存 | TriggerVector、OtaVector、StorageVector | RAM 摘要 | 实时读，不写 SD |
| L1 异常现场 | RAM trace ring | 脉冲异常、DMA overflow、READY timeout | RAM ring | ISR/实时侧只追加轻量事件 |
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
- `WRITE_TRACE/WRITE_REPORT` 可以在 `DISARM/FAULT` 后执行，ARM 后只写 RAM trace ring。
- 所有 `READING/WRITING/SYNCING` 都必须分片执行，单次 service 超预算后让出主循环。

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

Ref 切换事务：

```text
write candidate.ref.tmp
flush/sync/close
rename candidate.ref.tmp -> candidate.ref
validate candidate pack
rename active.ref -> previous.ref.tmp
rename candidate.ref -> active.ref
rename previous.ref.tmp -> previous.ref
```

恢复规则：

- `.tmp` 文件不得被视为有效证据。
- 自动生成文件默认不覆盖已有 final 文件；如果 final 已存在，必须重新分配 sequence。
- trace `.bin` 先写，trace `.idx.tmp` 后写，`.idx` rename 成功后 trace 才视为完整。
- `active.ref` 有效则继续使用 active。
- `active.ref` 无效但 `previous.ref` 有效则退回 previous。
- previous 也无效则尝试 factory。
- factory 无效则 System Pack 不可用，但基本触发功能仍应可运行。

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
- 脉冲异常实时采集只写 RAM trace ring；SD 落盘属于后处理。

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
tools/rp2350_tk_toolbox.py
```

## 19. 实现路线

### P0A - 固定目录与 Manifest

- [ ] `sd_fs_build.py` 生成固定目录、`manifest.json`、`manifest.idx`。
- [ ] 板端 StorageAO 扫描 `manifest.idx`。
- [ ] 检查 required 文件存在性和可选 CRC。
- [ ] 实现路径规范化、目录白名单和文件信息查询。
- [ ] `MMEM:CAT?` 增加分页或输出长度限制。
- [ ] SD UI 显示 manifest/pack 状态、默认 OTA 包存在性和 SD 错误摘要。

### P0B - Vector 快照

- [ ] 定义 snapshot JSON 固件结构体和序列分配。
- [ ] 写入 boot snapshot：`/snapshots/boot/boot_XXXXXX.json`。
- [ ] 写入 ARM 前 snapshot：`/snapshots/arm/arm_XXXXXX.json`。
- [ ] 写入 fault snapshot：`/snapshots/fault/fault_XXXXXX.json`。
- [ ] 增加最近 snapshot 查询：`SYST:SNAP:LAST?`。
- [ ] UI 显示最近 fault snapshot id/hash。

### P0C - 脉冲异常追溯

- [ ] 增加 RAM trace ring，固定事件记录 16 字节。
- [ ] 记录 trigger edge/missed edge、gate、DMA restart/overflow、PIO state、TriggerFB ECC、A0-A3 timeout、READY/REDY、resource timeout。
- [ ] DISARM/FAULT 后写 `/traces/*/*.bin` 和 `.idx`。
- [ ] 生成 `/reports/fault/pulse_fault_XXXXXX.json`。
- [ ] 增加查询：`SYST:TRAC:LAST?`、`SYST:FAULT:LAST?`。

### P1A - Pack/Ref

- [ ] 增加 `/packs/pack_xxxxxx/` 和 `/refs/*.ref` 目录支持。
- [ ] 支持 `active/previous/factory/candidate` ref 查询。
- [ ] candidate pack 校验通过后才允许 checkout。
- [ ] checkout 成功后通过事件更新 TriggerAO 配置和校准。
- [ ] snapshot/report 记录 active pack id/hash。
- [ ] PC 工具生成 pack、ref、pack diff 和一致性检查。

### P1B - Profile/Calibration

- [ ] 加载 active pack 或 P0 兼容路径中的 profile/cal。
- [ ] 校验 schema/product/hardware/hash。
- [ ] ARM 前把配置转换为 TriggerAO/TriggerVector 事件和摘要。
- [ ] 拒绝 armed 状态下从 SD 重新加载关键配置。

### P1C - Mission/Recipe

- [ ] 加载 active pack 或 P0 兼容路径中的 mission。
- [ ] 加载 `/mission/sequence.bin`。
- [ ] 加载 `/mission/node_map.json`，支持 A0-A3 角色与测试流程部署。
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
- [x] LCD 顶部 tab 改为 3 槽滚动式 tabview，KEY2/GPIO2 可切到 SD 页面。

## 21. 当前验证记录

- 日期：2026-07-02
- 构建：`pico2-release`
- build id：`20260702153535`
- OTA 包：`build/RP2350_TRIG_UPDATE.pkg`
- 验证目录：`build/ota_validation_tab_open_source_style_quick`
- 构建验证：
  - `cmake --build --preset pico2-release` 通过。
  - `python tools\release_check\release_check.py --preset pico2-release --build-dir build` 通过，`release_check=OK`。
- OTA 闭环：
  - `SYST:OTA:MODE? -> "DIRECT_AB",1`
  - `SYST:OTA:STAT? -> "COMMITTED",2,"NONE",5`
- SD 查询：
  - `SYST:SD:STAT? -> "CARD_READY",1,1,"OK",0`
  - `SYST:SD:INFO? -> "CARD_READY","SDHC_SDXC",1,61085696,30542848,1,1,1`
  - `SYST:STOR:STAT? -> "CARD_READY",1,1,"OK",0`
  - `MMEM:CAT? -> "OK","image.ub,4486744,FILE;BOOT.BIN,2230264,FILE;System Volume Information,0,DIR;"`
  - `MMEM:CAT? "/update" -> "NO_PATH","OPEN_FAILED:5"`，当前插入卡未创建 `/update`，不是挂载失败。

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
- `tools/rp2350_tk_toolbox.py`
- `docs/OTA方案.md`
- `docs/HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE.md`
- `docs/TASK_PROGRESS.md`
