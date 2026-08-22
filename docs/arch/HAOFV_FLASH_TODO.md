# HAOFV 板载 Flash 域实施工作板

Status: Active
Domain: HAOFV / Flash / OTA / Storage
Canonical: `docs/arch/HAOFV_FLASH_TODO.md`
Related: `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`, `docs/ota/OTA_TODO.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/storage/SD_TODO.md`
Last updated: 2026-08-22

本文只跟踪 Flash v2 的实现、迁移和验证。架构语义以 `HAOFV_FLASH_ARCHITECTURE.md` 为准；
v1 OTA 已完成项和历史报告仍留在 `docs/ota/OTA_TODO.md`，不得在两个 TODO 中重复标记完成。

## 一、工作板规则与当前状态

### 1.1 状态规则

- `[ ]`：未开始或只有方案，没有可复核产物。
- `[~]`：正在实现；必须写明当前分支/产物和未完成 gate。
- `[x]`：代码、测试、HIL、文档和回退证据均满足该项定义。
- `[!]`：阻塞；必须记录阻塞条件、可继续的旁路工作和解除条件。

“编译通过”“SCPI 有响应”“单次启动成功”都不能单独作为完成证据。契约从 `pending` 改为
`active` 需要实现、负向测试、相应 HIL 和独立 C11 交叉审核。

### 1.2 已有基线

| 基线 | 状态 | 证据 | 仍缺什么 |
|---|---|---|---|
| 物理/构建/兼容布局事实已核对 | `[x]` | `CMakeLists.txt`、`drv_flash.h`、`ota_partition.h`、`ARCH_FLASH_CROSS_REVIEW_01.md` | live consumer 已同源于 v1 compatibility map；v2 仍需 factory 迁移。 |
| v2 map source/schema 与 geometry gate | `[~]` | `config/flash_map_v2.json`、`config/flash_map_v1_compat.json`、`config/flash_map_gen/`、commit `315dc6f`/`10fd545`/`a211188` | v2 factory 部署与高地址 HIL 尚未完成。 |
| Flash v2 owner/map/store/Boot/OTA 语义已形成 canonical | `[x]` | `HAOFV_FLASH_ARCHITECTURE.md` | 7 条目标契约仍为 `pending`。 |
| Direct A/B 为发布默认 | `[x]` | CMake preset/release check/当前 Bootloader | `COPY_TO_ACTIVE` 兼容分支尚未从 v2 清除。 |
| core1 Flash park/ACK 基础存在 | `[x]` | `drv_flash_lockout.*`、FlashTransaction HIL | OTA image 与 Product Config 已收敛到 FlashTransactionAO；Boot/metadata 仍待迁移。 |
| USB/SD OTA 基础存在 | `[x]` | `ota_manager`、host tools、历史 OTA 验证 | 尚未共用 v2 stream/journal/Flash sink。 |
| TDMA reliable bulk 资源基础存在 | `[x]` | `tdma_profile.h`、traffic scheduler | 尚无 OTA wire/session/durable ACK。 |
| RefMem registry、VDC runtime、PIO persona 基础存在 | `[x]` | 对应组件与域文档 | 尚无生产持久化与签名 PIO catalog。 |

这些 `[x]` 只表示“迁移输入可用”，不表示任一 v2 里程碑完成。

### 1.3 当前主线

```text
M0 契约/迁移输入
  -> M1 FlashMap + 唯一事务 owner
       -> M2 Stores + namespace + PIO catalog
       -> M3 Boot/镜像信任链
            -> M4 本地 OTA + factory/COM8 迁移
                 -> M5 TDMA 流式 OTA
                      -> M6 量产加固与发布
```

M2 和 M3 可在 M1 稳定后并行，但 M4 必须同时依赖 M2/M3；M5 不得绕过本地 OTA 闭环直接做
多板分发。M0-01/M0-02 已完成，M1-01 已完成 geometry/range 子项；M1-02 已完成纯算法、
generated permission view、版本化 live consumer 和只读板端验证；M1-03 已建立 transaction owner，并已迁移 OTA image 与 Product Config 首个 App writer
并迁移 OTA image 首个生产 writer，同时保持 v2 只能由 factory full erase/reflash 部署。

### 1.4 下一步执行序（跨电脑交接清单）

以下顺序是当前分支的唯一推荐执行路径；每项完成前不得把对应里程碑标记为 `[x]`。

1. **M1-04 统一准入 gate**：以 `resource_arbiter` 为唯一运行态事实源，补充
   Calibration training 和 TDMA clock-training 的 owner 发布 gate；保留 RUN 下 OTA 的
   “先检查、后取得 FLASH owner、再进入 OTA”语义，不得直接把 RUN 全部改成拒绝。FlashTransactionAO
   只消费 snapshot，拒绝 CAL/training/thermal-critical/FAULT/unknown 状态的新写，warning 仅按
   policy 降速或暂停。补 host negative fixture、validation-only COM8 负向 HIL，并记录零 raw
   erase/program delta。
2. **M1-05 owner/buffer 收敛**：实现 immutable provider/refcount 或等价 lease，覆盖 producer
   reset、duplicate completion、page/sector 执行中 abort 和 lease 释放；保持大于
   `FLASH_TRANSACTION_OWNED_PAYLOAD_SIZE` 的请求 fail-closed，补 host 与构建/inventory gate。
3. **M1-06 高地址 Scratch**：只增加 validation-only Scratch lease intent；流程固定为 target
   confirm → erase → pattern program → readback/hash → erase/restore。记录 geometry、map symbol、
   pattern hash、lockout、温度和恢复结果；release binary 必须不含 destructive validation 命令。
4. **M0-05 实板回退**：在 COM8 板上物理按住 BOOTSEL 后复位/重新上电，确认 ROM BOOTSEL 可见，
   再执行 full erase、factory UF2 load/verify 和应用复核；保留 identity、build、slot、错误队列、
   artifact hash 与原始日志。未获得 ROM BOOTSEL 证据前保持 `[!]`，不得以应用 USB 断开代替。
5. **退出评审与提交**：代码验证先完成并单独提交/推送；随后更新本文件和
   `HAOFV_FLASH_TASK_PROGRESS.md`，运行文档门禁；`ARCH-FLASHMAP-01`、`ARCH-FLASHOWNER-01`
   只有在 host/build/HIL、回退和 C11 独立交叉审核齐全后才可从 `pending` 激活。

跨电脑开始工作时，先执行 `git pull --ff-only`、`git status --short --branch`，并先阅读本节及
`HAOFV_FLASH_TASK_PROGRESS.md` 顶部的最新任务记录；COM8 物理操作属于不可由主机测试替代的单独 gate。

## 二、里程碑总览

| 里程碑 | 目标 | 进入条件 | 退出证据 |
|---|---|---|---|
| M0 | 冻结机器可执行输入和迁移边界 | canonical 已评审 | inventory、schema、map source、迁移/回退包 |
| M1 | 建立 FlashMap 和 App 唯一写事务层 | M0 | host tests、link scan、高地址 Scratch HIL |
| M2 | 建立 NVS/Blob/FCB 和主域 namespace | M1 | torn/GC/wear tests、域重启负向 HIL |
| M3 | 建立 BCB、Direct A/B、签名和 Recovery | M1；M2 提供 BCB/store primitives | Boot fault matrix、factory artifact |
| M4 | 统一 USB/SD/UART stream 并迁移 COM8 | M2 + M3 | 本地 A/B/revert/resume/Recovery 闭环 |
| M5 | 增加 TDMA stream 和多板滚动升级 | M4 + TDMA reliable bulk 稳定 | 两板、四板、实时性和 resume 报告 |
| M6 | 掉电、寿命、安全、跨平台和 release 收口 | M4/M5 | release gate、长期报告、技术债删除 |

## 三、M0 契约与迁移输入

目标：把文档决策转成可由代码、工具和 CI 消费的输入；本阶段不烧写 v2。

### M0-01 当前实现 inventory

- [x] 扫描所有 `drv_flash_read/erase/program/xip_ptr` 调用、offset/size、linker ORIGIN、factory
  address 和 host tool 假设，输出版本化机器可读清单。
- [x] 每个调用点记录 owner/core/mode/partition/write frequency/power-cut semantics/目标 API。
- [x] CI 保存基线并拒绝新增未登记 raw erase/program 调用。
- [x] 对照 `ota_partition.h` 明确下 4 MiB 兼容布局和上 12 MiB 未分配，禁止把物理容量声明
  当作 v2 已实现。

产物：`config/flash_raw_call_allowlist.json`、构建目录 `flash_inventory.json`、
`tools/flash_map/flash_inventory.py`；证据 commit `315dc6f`。

### M0-02 FlashMap source schema

- [x] 定义 geometry/map 机器格式：map version、partition ID、offset/size、alignment、Boot/App/
  factory permission、executable/store type、update policy 和 compatibility。
- [x] 生成或校验 C header、linker memory、factory address、OTA packager 和文档快照。v1
  compatibility 与 v2 target 分别生成 versioned header/manifest/CMake/linker artifact；live
  consumer 显式选择 `PROJECT_FLASH_DEPLOYMENT_MAP=v1_compat`，禁止在线误用 v2。
- [x] 静态断言对齐、无重叠、A/B 等长、bootable range、reserved gap policy 和 map tail。
- [x] 负向 fixture 覆盖 overlap、overflow、wrong geometry、bad executable permission、manifest
  state/partition、linker/source token、OTA run offset 和 release preset drift。

产物：`config/flash_map_v1_compat.json`、`config/flash_map_v2.json`、`config/flash_map.schema.json`、
`config/flash_map_gen/`、`tools/flash_map/flash_map.py`、`tools/flash_map/flash_consumer_check.py`；
证据 commit `315dc6f`、`a211188`。

### M0-03 持久化 schema registry

- [x] 为 BCB、Image Manifest、Product NVS、Calibration NVS、VDC NVS、Blob/Deployment Capsule、
  Fault FCB、OTA Journal 和 PIO Catalog 分配 schema/object type。
- [x] 每个对象登记 writer、reader、compatibility、frequency/endurance、atomicity、rollback、
  factory default、diagnostic projection 和 SD evidence 去向。
- [x] 建立 negative inventory：Domain Vector、ECC state、queue/FIFO、lock/counter/cursor、VDC
  lock、RefMem epoch/ACK、PIO/DMA runtime 均不得作为启动事实恢复。
- [x] 未知 required field fail closed；未知 optional field 可跳过但保留长度/完整性检查。

产物：`config/persistence_schema_registry.json`、`tools/flash_map/persistence_schema_check.py`、
`tests/python/test_persistence_schema.py`；golden records 留待 M2 Store core。

### M0-04 Boot/OTA wire 契约输入

- [x] 冻结 BCB lane/commit/select/GC 和 Image Manifest TLV/hash/signature/security counter 规则。
- [x] 冻结 `OtaStreamSession` identity、generation、object/destination 和 durable offset 语义。
- [x] 冻结 TDMA OPEN/DATA/ACK/CLOSE/ABORT/STATUS、credit/resume token 和 reject reason。
- [~] 形成 parser golden/fuzz corpus；当前已有 4 个 golden vector 和机器检查器，真实 parser/fuzz
  corpus 尚待 M2/M3 实现接入；相关 registry 项保持 `pending`。

产物：`config/flash_wire_contracts.json`、`tools/flash_map/flash_wire_check.py`、
`tests/python/test_flash_wire_contracts.py`。

### M0-05 migration/rollback 包

- [~] 固定 v1 最后可恢复 commit、factory UF2、BOOTSEL 流程和工具版本；artifact checksum 与实际
  BOOTSEL 样板恢复仍待完成。
- [x] 定义 identity/Product Config/OTA metadata/Calibration/report index 的备份、转换和丢弃策略。
- [x] 定义 blank/v1/unknown/v2 map 的 Boot 行为和用户可见恢复信号。
- [x] 明确样板只走 factory full erase/reflash，不实现 App 在线原地搬迁。

产物：`config/flash_migration_policy.json`、`tools/flash_map/flash_migration_check.py`、
`tests/python/test_flash_migration_policy.py`；v1 回退 artifact/runbook 的板端恢复证据仍待完成。

### M0 退出门禁

- [x] inventory 与源码扫描一致；新增 raw caller 能使 CI 失败。
- [~] map/schema/wire 具有正向、边界和负向 fixture；真实 parser/fuzz corpus 仍待 M2/M3。
- [ ] v1 回退 artifact 可由 BOOTSEL 恢复至少一块样板。
- [ ] 无目标 offset 被写入 linker/driver/tool 之外的第二事实源。

## 四、M1 FlashMap 与唯一事务 owner

目标：先获得可验证、可限权、双核安全的基础写层，再接任何业务 store。

### M1-01 Geometry 与 Raw HAL

- [x] `drv_flash` 总容量只引用 geometry；删除独立 4 MiB limit。
- [x] `read/xip_ptr/erase/program` 使用 overflow-safe range check 和 alignment check。
- [x] raw write header `drv_flash_write.h` 只由 BootFlashService、FlashTransaction target 和
  geometry fixture 显式 include；通用 `drv_flash.h` 不再声明 erase/program。
- [x] host tests 覆盖 zero length、last byte、one-byte overflow、integer wrap、unaligned/null/high range。

证据：commit `315dc6f`、`9892768`、`cc9bd80`；`run_drv_flash_geometry_tests.ps1`、release 与
RTOS+双核 smoke 构建通过。`flash_link_check.py` 对 App A/B 的 RAM closure、IRQ 屏蔽/恢复、
parked raw caller 和同步 raw write link ownership 执行构建期门禁，M1-01 已完成。

### M1-02 FlashMap 与 permission view

- [x] 实现 `flash_map_find()`、partition-relative range validation 和 operation permission。
- [x] Boot/App/factory 使用不同的 generated permission view。
- [x] linker、factory image、packager、release size gate 消费显式选择的 deployed map artifact；当前
  为生成的 v1 compatibility map，v2 target map 不得驱动 live consumer。
- [x] 测试 active App write 拒绝、cross-partition 拒绝、Scratch lease 和所有 partition 首尾。

证据：commit `10fd545`、`a211188`；`run_flash_map_tests.ps1` 纳入全量 host runner，release/Boot/
App A/App B 均编译链接同一 `flash_map.c`。COM8 通过只读 SCPI 对 generated v2 target map 和
permission view 做板端闭环；live linker/factory/packager 则由 generated v1 compatibility map
保持当前可启动地址。报告见 `HAOFV_FLASH_TASK_PROGRESS.md` 的 `FLASH-TASK-20260822-002`、
`FLASH-TASK-20260822-003` 和 `FLASH-TASK-20260822-004`。本项仍为进行中：App OTA image、
Product Config 与 App metadata writer 已走 intent，Boot metadata 保持独立 BootFlashService；
板上 v2 分区没有部署或写入，C11 激活审核未开始。

### M1-03 FlashTransactionAO/FB/Vector

- [~] 已固定首轮 one-deep queue、job/requester/operation/provider generation/completion/cancel；
  当前 OTA 两页载荷继续走 `FLASH_TRANSACTION_OWNED_PAYLOAD_SIZE` 固定池，超过该上限的请求可使用
  generation-bound immutable buffer lease，缺少合法 lease 时仍 fail closed。
- [~] 实现 `VALIDATE -> QUIESCE -> ACQUIRE -> PARK -> ERASE/PROGRAM -> VERIFY -> COMMIT ->
  RELEASE -> COMPLETE/FAILED`，每次 service 只推进一个有界步骤。
- [x] Vector 使用 seqlock；只读查询不触发 Flash IO，并记录 policy/lockout/progress/result/timing；
  thermal/health gate、policy reason 与 temperature flags 已接入。
- [x] completion 区分 accepted/programmed/verified/committed，OTA 兼容包装只消费 committed。

首轮证据：commit `2a79643`、`bdc744b`、`accdfbc`、`f3d5a96`；OTA image erase/program 已从 portable callback 进入
`FlashTransactionAO/FB`，active slot 未知或写活动槽均 fail closed。host fault fixtures、release、
RTOS+双核构建和 COM8 双向 OTA/Vector HIL 均通过；HIL 工具在目标 image payload 首块和最终
metadata 提交后分别核对 Vector。该项保持进行中，因为当前仍有同步兼容包装，Boot writer、
immutable provider/refcount、运行时 abort/lease 和跨 reset durable completion 尚未收敛。

### M1-04 Mode、温度与双核门禁

- [~] FlashTransactionAO 已接入 Diagnostics fault、board/chip thermal critical、System/FAULT、
  Trigger activity、Calibration training 和 TDMA clock-training fail-closed gate；policy
  error/temperature flags 已进入 Vector，且训练状态由 resource_arbiter snapshot 统一发布/消费。
  warning policy 与板端 fault/thermal/training negative HIL 仍待补齐。
- [x] host negative fixture 覆盖 thermal critical/diagnostics fault admission，断言 raw erase/program
  未执行；板端 fault/thermal 注入 HIL 仍待安全入口。
- [~] Trigger capture/clock、FAULT mode、Flash resource conflict、Calibration training 和 TDMA
  clock-training 已细分为 policy reason；System owner 发布 gate 已接入，板端拒绝 HIL 和
  warning policy 仍待完成。
- [ ] RUN/CAL/thermal critical/unknown state 拒绝新写；warning 只按 policy 暂停或降速。
- [x] App raw write session 的 core1 park request/ACK/release 只由 transaction owner 驱动；parked raw
  operation 在无活动 session 时 fail closed，Boot 同步 raw writer 保持独立会话边界。
- [x] 审计 RAM resident closure：代码、常量、jump table、IRQ path 不依赖 XIP。
- [x] HIL 注入 park timeout，证明 raw operation 未执行；release 后 core1 alive。

### M1-05 Buffer 与 owner 收敛

- [~] 不超过 `FLASH_TRANSACTION_OWNED_PAYLOAD_SIZE` 的 payload 在 submit 时复制入固定 pool；更大
  payload 必须绑定 generation、长度、retain/release 的 immutable buffer lease，缺少合法 lease 时
  fail closed，禁止把 producer alias 交给 raw writer；live producer 迁移和跨 reset durable lease
  语义仍待完成。
- [~] queue full、producer reset、duplicate completion、abort during page/sector 均有单测；当前已覆盖
  queue full、duplicate terminal/abort、large payload no-raw，以及 raw erase/program 回调期间触发
  abort 后跳过 verify/commit，以及 provider generation reset 在 raw 前/期间 fail-closed 的 host fixture；
  completion lease/durable 语义仍待异步 provider/step hook。
- [~] OTA image、Product Config 与 App OTA metadata 已迁移到 intent API；Boot metadata 通过独立
  BootFlashService adapter 保持 raw owner，M3 BootControlStore 与 M2-02 Product NVS store 仍待完成。
  当前 Product/OTA metadata 仍是 single-sector rewrite，不得视为 atomic NVS/BCB。
- [x] inventory gate 证明 App raw erase/program caller 必须是 FlashTransactionAO；写 API 头文件
  进一步从通用 `drv_flash.h` 隐藏。仍待 link-level symbol visibility 与运行时 abort/lease 语义。

### M1-06 高地址 Scratch 验证

- [ ] validation-only SCPI 只提交 Scratch lease intent，不暴露任意 offset 命令。
- [ ] 流程为 target confirm -> erase -> pattern program -> readback/hash -> erase/restore。
- [ ] COM8 报告记录 JEDEC/geometry、map symbol、pattern hash、lockout、temperature 和恢复结果。
- [ ] release binary string scan 证明 destructive command 不存在。

### M1 退出门禁

- [ ] `ARCH-FLASHMAP-01` 和 `ARCH-FLASHOWNER-01` 的 host/build/HIL 证据齐全。
- [ ] 所有 App writer 已走 intent；Boot writer 依赖白名单不受 App AO 污染。
- [ ] 高地址验证只触碰 Scratch，重启后 Boot/identity/当前 App 不变。

## 五、M2 Stores、namespace 与部署对象

目标：所有业务域复用少量 store primitive，不再创建私有 sector/offset 服务。

### M2-01 Store core

- [ ] 实现 common record envelope、CRC/hash、generation、commit marker 和 version compatibility。
- [ ] 实现 NVS append/sector rotation、Blob immutable object/atomic ref、FCB append ring/GC。
- [ ] GC 只能提交 FlashTransaction job；空间不足时返回明确 backpressure/reason。
- [ ] power-cut fixtures 覆盖 body/commit/ref/GC 各边界，始终选出确定的旧或新事实。

### M2-02 Product NVS

- [~] USB mode/board number 已有 Product Config intent 与默认策略；versioned key/namespace、identity/
  capability/permission 记录仍待 Store core。
- [ ] 同值写不产生 record；删除固定 sector overwrite。
- [ ] v1 导入只能由 factory tool 显式执行，App 不猜测旧布局。
- [ ] 循环写/复位/GC/erase distribution/wear HIL 通过。

### M2-03 Calibration NVS

- [ ] 定义 candidate/accepted/active/previous 与 atomic active ref。
- [ ] key 绑定 unique board/link endpoints、topology/profile/schedule、generation/freshness。
- [ ] `CALibration:SAVE` 只接受 Calibration Domain accepted package；diagnostic-only/expired/bad CRC 拒绝。
- [ ] VDC 只读取 active accepted snapshot；缺失或过期进入 uncalibrated/relock gate。

### M2-04 VDC NVS

- [ ] 定义 servo/holdover/reference/timestamp dictionary/discipline profile namespace。
- [ ] aging/temperature/wander 先进入 candidate，经长窗口 evidence 和维护态 accept 后切 active。
- [ ] boot 不读取 VDC NVS；App 每次从 `OFF/CHECKING` 重锁。
- [ ] negative HIL 证明 offset/rate/DCO/LOCK/HOLDOVER/map generation 不恢复为 live fact。

### M2-05 System Pack、Blob 与 RefMem

- [ ] 定义 Deployment Capsule manifest、immutable extent/chunk bitmap/object hash 和 active/previous ref。
- [ ] 保存 `.rmtp` 与 ApplicationMap/Capability/NodeLoad/FB/Event/DataLink tables，不保存 live vector。
- [ ] 上电只送 staging；DeploymentGate 通过后激活并建立新 epoch，peer 初始 stale。
- [ ] torn active ref 回退 previous；两份损坏进入 factory profile，不伪造默认 deployment。

### M2-06 Fault FCB 与 wear health

- [ ] 定义 boot/reset/power/Flash/OTA/critical sensor 低频事件；重复事件限速/合并。
- [ ] 高频 TDMA/Trigger/temperature/current trace 保持 SD，不进入 FCB。
- [ ] 每个 store 发布 erase estimate/high-watermark/GC/torn/CRC/bad-region health。
- [ ] 按产品写频率形成 endurance budget，不用短台架结果替代寿命分析。

### M2-07 PIO Program Catalog 与域接入

- [ ] 清点全部 `.pio`/persona，生成 ID/version/hash/instruction count/PIO/SM/pin/DMA claim catalog。
- [ ] catalog 纳入签名 App manifest；System Pack 只选择 ID，DeploymentGate 校验 compatibility/resource。
- [ ] persona HIL 验证 stop SM/DMA -> safe IO -> load -> start -> snapshot，全程无 Flash write。
- [ ] Blob object type gate 拒绝 executable PIO/native code。
- [ ] Trigger/Loop、TDMA、Communication、Measure、Diagnostics/UI 配置按架构矩阵归并 namespace。

### M2 退出门禁

- [ ] `REFMEM-PERSIST-01`、`VDC-PERSIST-01`、`ARCH-PIOCAT-01` 具备重启负向证据。
- [ ] 所有 store 的 torn/GC/unknown-schema/rollback tests 通过。
- [ ] 没有业务域拥有 raw offset、私有 GC 或从 Flash 恢复 runtime snapshot。

## 六、M3 Boot 与镜像信任链

目标：先让单板能够确定地 test/confirm/revert/recover，再允许迁移和网络 OTA。

### M3-01 BootFlashService 与依赖白名单

- [ ] Boot target 只链接 geometry/map、BCB、ImageVerifier、Raw HAL、watchdog/LED/ROM recovery。
- [ ] link gate 拒绝 RTOS、SCPI、TDMA、FatFs、littlefs 和业务组件。
- [ ] Bootloader size 使用 partition symbol gate，不使用文档硬编码阈值。

### M3-02 BootControlStore

- [ ] 实现双 lane append/select/commit/GC/wear counter。
- [ ] 无有效 BCB 不创建默认可启动事实，进入 Recovery policy。
- [ ] torn-write 注入覆盖 body/readback/commit/lane seal/old lane erase。

### M3-03 Direct A/B 单主线

- [ ] pending -> test boot -> explicit confirm；reset/no-confirm/attempt exhausted -> previous confirmed。
- [ ] A/B slot-specific image、vector/reset handler、hash/signature/compatibility 校验。
- [ ] v2 删除 `COPY_TO_ACTIVE` 运行分支和可写 mode 命令；历史查询返回明确 legacy/unsupported。

### M3-04 Manifest、signature 与 anti-rollback

- [ ] 选定算法/库和 RP2350 OTP/key capability；定义 STM32 portable boundary。
- [ ] dev/release/factory key 分离，定义 key ID、rotation/revocation 和泄露处置。
- [ ] security counter 掉电安全；低 counter 即使 CRC 正确也拒绝。
- [ ] 离线 release tool 输出 manifest/hash/signature/build ID/SBOM，不泄露 private key。

### M3-05 Recovery 与 factory artifact

- [ ] Recovery 最小能力：诊断 map/BCB、验证 factory package、受控 USB/SD 恢复。
- [ ] Recovery 更新使用更高权限，普通 TDMA OTA 无权覆盖。
- [ ] factory artifact 包含 Bootloader、Slot A、Recovery、map manifest/BCB 和空 store baseline。
- [ ] factory report 记录每个 region hash，禁止遗留旧 metadata 到新 BCB 地址。

### M3-06 Boot fault matrix

- [ ] Factory boot A；A->B；B->A；pending 未确认回滚。
- [ ] pending hash/signature/vector/compatibility/security counter 错误回滚。
- [ ] BCB 单 lane 损坏可恢复，双 lane 无效进入 Recovery。
- [ ] A/B 损坏进入 Recovery；Recovery 损坏进入 ROM/factory indication。
- [ ] 每个结果有 SCPI/SD report/LED evidence 和可重复脚本。

### M3 退出门禁

- [ ] `ARCH-BOOTCTRL-01` 的所有 Boot/torn/security case 有确定结果。
- [ ] Boot dependency/size/map gate 在 CI 中生效。
- [ ] factory/recovery artifact 可从空白 Flash 恢复样板。

## 七、M4 本地 OTA 与 COM8 迁移

目标：USB/SD/UART 共用一个 transport-neutral session，并在单板完成 v2 迁移闭环。

### M4-01 OtaStreamSession core

- [ ] 从 ingress callback 拆出 open/write/close/abort/resume 状态机。
- [ ] session 绑定 identity/capability/package/object/map/partition/generation/destination。
- [ ] package manifest 与 slot-specific object 分离；source 只发送 inactive object。
- [ ] sink 只向 FlashTransactionAO 提交 intent，verified object 后才写 pending BCB。

### M4-02 Journal 与 durable resume

- [ ] durable offset 只在 program/readback completion 后推进。
- [ ] checkpoint frequency 由 wear/retransmit profile 定义，不按每 chunk 擦写。
- [ ] token 绑定 package hash/map/partition/identity/generation；mismatch restart/abort。
- [ ] reset 后 journal + readback 重建；torn journal 回退最近可信 checkpoint。

### M4-03 Local ingress regression

- [ ] USB/VISA、SD、UART adapter 使用同一 core，兼容状态/error/progress projection。
- [ ] Stage 只保存 manifest/chunk spill/delta，不缓存完整 A+B package。
- [ ] 乱序、重复、CRC、truncate、overflow、abort、zero storage、wrong slot/package 全部 fail closed。
- [ ] USB/SD A->B、B->A、resume、revert、Recovery 回归通过。

### M4-04 COM8 factory migration

- [ ] 迁移前记录 `*IDN?`、build、slot/result、board identity、Product Config、sensor snapshot。
- [ ] BOOTSEL/factory full erase/reflash v2，确认 USB 重新枚举和 identity 转换策略。
- [ ] 运行 M1 Scratch、Product NVS、Calibration empty/default、Boot fault subset。
- [ ] 恢复 TDMA/Calibration 单板 persona，确认 PIO/DMA/IO owner 未被 Flash 重构破坏。
- [ ] 执行 v1 rollback drill，证明 artifact/runbook 可恢复。

### M4 退出门禁

- [ ] `ARCH-OTASTREAM-01` 的本地 transport、durable offset 和 resume 证据齐全。
- [ ] COM8 在 v2 上完成 A/B/revert/Recovery 和关键 store 重启验证。
- [ ] 迁移/回退报告归档，未留下依赖 v1 offset 的隐式工具路径。

## 八、M5 TDMA 流式 OTA

目标：在不影响 VDC/RefMem 硬实时预算的前提下增加可靠批量分发。

### M5-01 Wire/parser

- [ ] 实现 OPEN/ACK/RESUME/DATA/CLOSE/ABORT/STATUS 编解码和 golden vectors。
- [ ] length/offset/sequence 做 overflow check，未知 required flags fail closed。
- [ ] fuzz 覆盖 truncate/oversize/duplicate/out-of-order/cross-session/identity/hash/generation mismatch。

### M5-02 Capability、identity 与 lane

- [ ] capability 包含 receiver/map/security/max chunk/credit/cache/Recovery state。
- [ ] board unique ID + accepted topology 形成 network identity；NO 只作显示/slot。
- [ ] session open 前选择 reliable bulk；运行中不动态换 lane。

### M5-03 Credit、durable ACK 与 resume

- [ ] credit 联合固定 RX pool、Flash queue、checkpoint budget 和 maintenance gate。
- [ ] queue accept 只消耗 credit，不推进 ACK；program/readback completion 才推进 cumulative offset。
- [ ] gate closed/credit zero 可长期暂停，不 busy-loop，不提升优先级。
- [ ] source/target reset、torn journal 和重复 DATA 可恢复或明确 abort。

### M5-04 DistributionFB

- [ ] per-node vector 发布 capability/durable offset/verify/pending/boot result/error。
- [ ] cohort/stage-all/commit policy 明确；单节点 ACK 不代表整组完成。
- [ ] 先升级非 reference；迁移 reference role/topology generation 后升级原 reference。
- [ ] 不兼容/失败节点隔离并保留旧固件，禁止降低签名或 anti-rollback 求全成功。

### M5-05 两板 HIL

- [ ] source 从 USB/SD 读取 package，经 TDMA 向 target inactive slot 流式安装。
- [ ] 注入 drop/duplicate/CRC/zero credit/source reset/target reset/session timeout。
- [ ] target boot/confirm/revert 后重新加入 topology，generation/freshness 正确。
- [ ] OTA 期间 VDC/RefMem 无新增 deadline miss/window overrun。

### M5-06 四板滚动升级

- [ ] stage/verify/commit 每节点可观测；reference role 迁移有明确 fence。
- [ ] 单节点失败、掉线、回滚和重新加入均有 topology/ring/VDC 降级事实。
- [ ] 报告包含每节点 image hash/security counter/session/retry/boot result/timing quality。
- [ ] 完成后所有节点恢复一致 map/capability/deployment generation。

### M5 退出门禁

- [ ] TDMA wire/session/parser contract 通过 host/fuzz/two-board/four-board gates。
- [ ] durable ACK 与 journal 在 reset/power-cut 下不越过真实 Flash 内容。
- [ ] hard realtime traffic 质量没有被 bulk backlog 污染。

## 九、M6 量产加固与发布

目标：把能工作的闭环提升为可长期运行、可审计、可跨平台维护的产品能力。

### M6-01 Power-cut 与 destructive fault matrix

- [ ] 自动化覆盖 BCB、NVS、Blob ref、FCB、Journal 和 inactive image install 每个 commit 边界。
- [ ] 每个 injection point 定义旧/新期望，不接受“能启动但状态未知”。
- [ ] 可控电源台架记录 iteration/failure/Flash health/raw log 和最小复现条件。

### M6-02 Wear、温度与长稳

- [ ] Product/Calibration/VDC/FCB/Journal 分别做加速写入和 sector distribution 测试。
- [ ] 验证 GC worst-case、temperature pause/resume、watchdog 和最后有效记录。
- [ ] 根据实测确定 write frequency policy；RUN/CAL 不开放普通持久化。

### M6-03 Release/security gates

- [ ] docs、host unit、fuzz、main build、size/map、link dependency、release string scan 全绿。
- [ ] artifact 含 manifest/hash/signature/build ID/map version/SBOM；private/dev key 不进 release。
- [ ] COM8、两板、四板 HIL 报告关联具体 commit/artifact/tool version。
- [ ] registry 逐条从 `pending` 转 `active` 时分别完成 C11 review，不批量无证据改状态。

### M6-04 技术债清除

- [ ] 删除 driver 4 MiB limit、重复 offset、W25Q32 当前事实和 v2 copy-to-active branch。
- [ ] 删除业务组件 raw Flash write、Product Config fixed-sector overwrite 和 OTA 整片 owner 假设。
- [ ] Deprecated/legacy 文档保留历史但明确不再是 source of truth，不删除已登记契约。

### M6-05 跨平台与未来产品

- [ ] geometry profile 允许 RP2350、STM32H7、i.MX RT 等共享 Partition ID/Store API 而非 offset。
- [ ] 为仪表、运动/电机、DAQ、ATE、RF 建 storage usage model 和 capacity admission fixture。
- [ ] host 验证同一 Deployment Capsule 在不同 geometry/capability 上 accept/reject。
- [ ] 动态原生插件需求另立 signed module/ABI/MPU/resource/rollback 评审；此前 BlobStore 拒绝执行。

### M6 退出门禁

- [ ] destructive/wear/thermal/security/release 报告齐全并可重放。
- [ ] 7 条 Flash 目标契约均有独立状态审核，不再存在未解释 v1 地址依赖。
- [ ] factory、OTA、Recovery 和 rollback artifact 可由另一台环境复现。

## 十、跨域接入索引

本表只指向唯一工作包，不复制任务内容。

| 主域 | 持久化对象 | 禁止恢复 | 工作包 |
|---|---|---|---|
| System/Product | identity/config/capability/permission | mode/lock/queue | M2-02 |
| Trigger/Loop | recipe/mission/safe limit/profile ref | ARM/cursor/live queue | M2-07 |
| SYNC_IO/PIO | catalog/IO profile/resource claim | IMEM/SM/FIFO/DMA runtime | M2-07 |
| Calibration | accepted package/generation | diagnostic raw capture | M2-03 |
| TDMA | profile/payload budget/resource claim | ring/counter/FIFO/gate | M2-07、M5 |
| RefMem | `.rmtp`/tables/active ref | vector/dirty/ACK/epoch | M2-05 |
| VDC/DPLL | discipline profile/Calibration ref | lock/offset/rate/DCO | M2-04 |
| Communication | adapter/address/calibration ref | FIFO/transaction/online | M2-07 |
| Measure | channel/trigger/compression profile | raw stream/waveform | M2-07 |
| OTA/Boot | BCB/image/journal | transport queue/uncommitted offset | M3、M4、M5 |
| Storage | Deployment Capsule/minimal offline object | complete history/user files | M2-05 |
| Diagnostics/UI | critical event/necessary preference | high-rate trace/page state | M2-06、M2-07 |

## 十一、验证证据矩阵

| 层级 | 必跑证据 | 首次 gate | 发布 gate |
|---|---|---|---|
| 文档 | docs_check、doc_regression、pytest、pre-commit | 每个文档 commit | M6-03 |
| Host unit | map/range/store/BCB/session/parser/compatibility | M0-M5 各包 | 全量 |
| Static/link | map/linker/tool 同源、raw caller、Boot deps、size/string/key scan | M1/M3 | M6-03 |
| Main build | release + RTOS/双核 validation，warnings-as-errors | M1 | 每个 artifact |
| 单板 COM8 | Scratch、store reboot、A/B/revert/Recovery/local resume | M1/M2/M3/M4 | M6-03 |
| 两板 | TDMA drop/reset/resume + realtime quality | M5-05 | M6-03 |
| 四板 | cohort/reference migration/failure/rejoin | M5-06 | M6-03 |
| Long-run | power-cut/wear/thermal/watchdog | M6-01/M6-02 | release 必需 |

所有报告必须包含 commit、artifact hash、map version、tool version、board identity、接线/profile、
起止时间、原始日志路径和结论。仅粘贴终端最后一行不构成 HIL 证据。

## 十二、回退与风险控制

| 变更面 | 首选回退 | 禁止 |
|---|---|---|
| Map/driver | BOOTSEL + v1 factory artifact | App 猜测旧 offset 在线搬迁 |
| Store schema | 保留 previous ref/old reader，未知 required fail closed | 原地覆盖唯一有效记录 |
| Boot/BCB | previous confirmed + Recovery | 无有效 BCB 时创建默认成功状态 |
| OTA session | abort/restart from可信 durable checkpoint | 用 RAM accepted offset 冒充 durable |
| TDMA cohort | 隔离失败节点、保留旧固件、恢复 topology generation | 单节点 ACK 代表整组成功 |
| Security counter | 拒绝低版本并进入受控 Recovery | 为恢复方便降低 counter/signature policy |
| PIO catalog | 保持 STOPPED 和旧 verified persona | 从普通 BlobStore 加载替代字节码 |

## 十三、统一完成定义

一个工作包只有同时满足以下条件才可标 `[x]`：

1. source of truth、owner、schema 和生命周期明确，没有新增重复数字或私有 writer。
2. 正向、边界、负向、torn/power-cut 测试按适用范围通过。
3. release 与 RTOS/双核 validation 构建通过，静态/link gate 生效。
4. 涉及硬件的任务有对应 COM8/两板/四板原始记录，不能用模拟代替。
5. HAOFV Vector、reason、generation/freshness 和 durable completion 可查询。
6. 回退 artifact/runbook 已实测；不可逆步骤有 factory/Recovery 路径。
7. 文档四项门禁全绿；契约状态变化完成独立 C11 交叉审核。
8. 代码与文档分离 commit，并推送当前私有分支。
