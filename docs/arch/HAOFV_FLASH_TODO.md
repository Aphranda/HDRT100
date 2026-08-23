# HAOFV 板载 Flash 域实施工作板

Status: Active
Domain: HAOFV / Flash / OTA / Storage
Canonical: `docs/arch/HAOFV_FLASH_TODO.md`
Related: `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`, `docs/ota/OTA_TODO.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/storage/SD_TODO.md`
Last updated: 2026-08-23

本文只跟踪 Flash v2 的实现、迁移和验证。架构语义以 `HAOFV_FLASH_ARCHITECTURE.md` 为准；
v1 OTA 已完成项和历史报告仍留在 `docs/ota/OTA_TODO.md`，不得在两个 TODO 中重复标记完成。

### 文档接口

- 架构语义、不变量和契约：只看 `HAOFV_FLASH_ARCHITECTURE.md` 与 `docs/check/DOCS_REGISTRY.md`。
- 里程碑、子项状态和退出门禁：只看本文；`[x]` 必须有可定位的进度证据。
- 构建号、提交号、板端日志、失败和阻塞：只看 `HAOFV_FLASH_TASK_PROGRESS.md`；本文只保留任务编号
  或报告路径引用，不复制快照正文。

## 一、工作板规则与当前状态

### 1.1 状态规则

- `[ ]`：未开始或只有方案，没有可复核产物。
- `[~]`：正在实现；必须写明当前分支/产物和未完成 gate。
- `[x]`：代码、测试、HIL、文档和回退证据均满足该项定义。
- `[!]`：阻塞；必须记录阻塞条件、可继续的旁路工作和解除条件。

“编译通过”“SCPI 有响应”“单次启动成功”都不能单独作为完成证据。契约从 `pending` 改为
`active` 需要实现、负向测试、相应 HIL 和独立 C11 交叉审核。

### 1.2 已有基线

| 基线 | 已完成部分 | 未完成部分 |
|---|---|---|---|
| 物理/构建/兼容布局事实已核对 | `[x]` `CMakeLists.txt`、`drv_flash.h`、`ota_partition.h`、`ARCH_FLASH_CROSS_REVIEW_01.md`；live consumer 同源于 v1 compatibility map。 | `[ ]` v2 factory 迁移。 |
| v2 map source/schema 与 geometry gate | `[x]` `config/flash_map_v2.json`、`config/flash_map_gen/`、factory-candidate Recovery/BCB/map baseline 和 region hash gate。 | `[ ]` 受控恢复、空片部署与高地址 HIL。 |
| Flash v2 owner/map/store/Boot/OTA 语义已形成 canonical | `[x]` `HAOFV_FLASH_ARCHITECTURE.md`。 | `[ ]` 7 条目标契约仍为 `pending`。 |
| Direct A/B 为发布默认 | `[x]` CMake preset、release check、当前 Bootloader。 | `[ ]` 从 v2 清除 `COPY_TO_ACTIVE` 兼容分支，并完成历史查询语义。 |
| core1 Flash park/ACK 基础存在 | `[x]` `drv_flash_lockout.*`、FlashTransaction HIL、OTA image/Product Config intent。 | `[ ]` Boot/metadata 迁移与跨 reset durable owner 闭环。 |
| USB CDC/USBTMC/SD OTA 基础存在 | `[x]` `ota_manager`、host tools、历史 OTA 验证。 | `[ ]` UART/RS485 adapter 接入统一 v2 stream/journal/Flash sink。 |
| TDMA reliable bulk 资源基础存在 | `[x]` `tdma_profile.h`、traffic scheduler。 | `[ ]` OTA wire/session/durable ACK。 |
| RefMem registry、VDC runtime、PIO persona 基础存在 | `[x]` 对应组件与域文档。 | `[ ]` 生产持久化与签名 PIO catalog。 |

这些 `[x]` 只表示“迁移输入可用”，不表示任一 v2 里程碑完成。

### 1.3 当前主线

```text
M0 契约/迁移输入
  -> M1 FlashMap + 唯一事务 owner
       -> M2 Stores + namespace + PIO catalog
       -> M3 Boot/镜像信任链
            -> M4 本地 OTA + DHRT100 样板迁移
                 -> M5 TDMA 流式 OTA
                      -> M6 量产加固与发布
```

M2 和 M3 可在 M1 稳定后并行，但 M4 必须同时依赖 M2/M3；M5 不得绕过本地 OTA 闭环直接做
多板分发。M0-01/M0-02 已完成，M1-01 已完成 geometry/range 子项；M1-02 已完成纯算法、
generated permission view、版本化 live consumer 和只读板端验证；M1-03 已建立 transaction owner，并已迁移 OTA image 与 Product Config 首个 App writer
并迁移 OTA image 首个生产 writer，同时保持 v2 只能由 factory full erase/reflash 部署。

当前实验资源策略：四板 COM3–COM6 优先用于 VDC、TDMA 和其他实时算法优化；四板 OTA 只保留
兼容路径回归，不作为当前 Flash v2 destructive HIL 或迁移完成证据。Flash 的 Scratch、factory
erase/reflash、回退和 v2 deployment 证据统一在 DHRT100 单板闭环后再执行。

### 1.4 下一步执行序（跨电脑交接清单）

以下顺序是当前分支的唯一推荐执行路径；每项完成前不得把对应里程碑标记为 `[x]`。

1. **M3-05 Recovery/factory 工件**：signed debug slot manifest、factory UF2 checksum、Recovery/
   map/BCB baseline、受控 full erase/load/verify 和 DHRT100 空片恢复 HIL 已完成；下一步是
   Recovery runtime factory package 验证、受控 USB/SD restore 和 C11 激活。在这些 gate 齐全前
   继续保持 `target_not_deployed` 标记。
2. **M4-02 durable resume 闭环**：真实 v2 `OTA_JOURNAL` backend、raw-image bounded resume core 和
   保留最新 checkpoint 的 sector rotation 已完成 host/build 接入；下一步补 package parser/image
   cursor、abort/restart policy、真实掉电/endurance 矩阵和 DHRT100 跨 reset HIL。不得用 raw host
   证据替代 package 或实板 resume 完成定义。
3. **M3-04 信任链**：portable verifier、RP2350 软件验签、角色化 key registry、离线签名请求、
   专用 debug profile、Boot slot-manifest 重验和 verified counter 到 BCB 的传递已完成；下一步
   补 OTP/key binding、产品 counter 来源与泄露处置。生产 profile 仍须独立 release key，debug
   key 不进入发布镜像；unsigned bypass 保持关闭。
4. **M0-05 实板回退（已完成）**：DHRT100 已取得 ROM BOOTSEL、full erase、factory UF2
   load/verify、应用复核、identity/build/slot/错误队列、artifact hash 与原始日志证据；保留该
   v1 工件作为后续 V2 调试阶段的恢复基线。
5. **M1 板端退出门禁**：使用可确认身份的 DHRT100 完成 CAL/training/thermal-critical/FAULT/
   unknown admission 负向 HIL、v2 Scratch restore 和 core1 alive 复核；只有 raw write delta、policy
   reason、温度和重启后身份/应用均有证据，才能关闭 M1。
6. **M4 样板迁移与本地入口**：Recovery、签名、Journal 和 v1 rollback drill 就绪后，才执行空片
   factory migration，再依次完成 USB CDC/USBTMC/UART/RS485/SD 的 A/B、resume、revert/Recovery。
7. **退出评审与提交**：代码验证先完成并单独提交/推送；随后更新本文件和
   `HAOFV_FLASH_TASK_PROGRESS.md`，运行文档门禁；`ARCH-FLASHMAP-01`、`ARCH-FLASHOWNER-01`
   只有在 host/build/HIL、回退和 C11 独立交叉审核齐全后才可从 `pending` 激活。

跨电脑开始工作时，先执行 `git pull --ff-only`、`git status --short --branch`，并先阅读本节及
`HAOFV_FLASH_TASK_PROGRESS.md` 顶部的最新任务记录；DHRT100 物理操作属于不可由主机测试替代的单独 gate。

## 二、里程碑总览

| 里程碑 | 目标 | 进入条件 | 退出证据 |
|---|---|---|---|
| M0 | 冻结机器可执行输入和迁移边界 | canonical 已评审 | inventory、schema、map source、迁移/回退包 |
| M1 | 建立 FlashMap 和 App 唯一写事务层 | M0 | host tests、link scan、高地址 Scratch HIL |
| M2 | 建立 NVS/Blob/FCB 和主域 namespace | M1 | torn/GC/wear tests、域重启负向 HIL |
| M3 | 建立 BCB、Direct A/B、签名和 Recovery | M1；M2 提供 BCB/store primitives | Boot fault matrix、factory artifact |
| M4 | 统一 USB CDC/USBTMC/UART/RS485/SD stream 并迁移 DHRT100 样板 | M2 + M3 | 本地 A/B/revert/resume/Recovery 闭环 |
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

#### 已完成部分

- [x] 冻结 BCB lane/commit/select/GC 和 Image Manifest TLV/hash/signature/security counter 规则。
- [x] 冻结 `OtaStreamSession` identity、generation、object/destination 和 durable offset 语义。
- [x] 冻结 TDMA OPEN/DATA/ACK/CLOSE/ABORT/STATUS、credit/resume token 和 reject reason。

#### 未完成部分

- [ ] 扩充 parser golden/fuzz corpus，并将真实 parser/fuzz 接入 M2/M3；当前已有 4 个 golden
  vector 和机器检查器，相关 registry 项保持 `pending`。

产物：`config/flash_wire_contracts.json`、`tools/flash_map/flash_wire_check.py`、
`tests/python/test_flash_wire_contracts.py`。

### M0-05 migration/rollback 包

状态：`[x]` 已完成。v1 回退工件、checksum 和至少一块 DHRT100 样板 BOOTSEL/full-erase
恢复 gate 均已闭环；后续 V2 迁移不得把本项重新标为进行中。

#### 已完成部分

- [x] 固定 v1 最后可恢复 commit、factory UF2、BOOTSEL 流程和工具版本。
- [x] 定义 identity/Product Config/OTA metadata/Calibration/report index 的备份、转换和丢弃策略。
- [x] 定义 blank/v1/unknown/v2 map 的 Boot 行为和用户可见恢复信号。
- [x] 明确样板只走 factory full erase/reflash，不实现 App 在线原地搬迁。
- [x] 通过固化 `tools/artifact_checksum/artifact_checksum.py` 生成并核对 v1 factory UF2 清单；
  `size=1051648`、SHA-256=`71aaba8d...5c4313`，完整值与报告见
  `out/flash_hil/dhrt100_v1_factory_checksum_20260823.json`。
- [x] DHRT100 已完成 BOOTSEL/full erase/factory UF2 load/verify；恢复后 `*IDN?`、build、
  OTA slot/result、BCB health、sensor snapshot 和 `SYST:ERR?` 均有原始记录，报告见
  `out/flash_hil/dhrt100_m005_bootsel_restore_20260823_final.txt`、
  `out/flash_hil/dhrt100_m005_post_restore_20260823.txt`。

#### 未完成部分

- 无；M0-05 的 v1 回退工件和至少一块 DHRT100 样板恢复 gate 已完成。

产物：`config/flash_migration_policy.json`、`tools/flash_map/flash_migration_check.py`、
`tests/python/test_flash_migration_policy.py`；板端原始恢复记录追加在
`HAOFV_FLASH_TASK_PROGRESS.md` 的 `FLASH-TASK-20260823-079`。

### M0 退出门禁

#### 已完成部分

- [x] inventory 与源码扫描一致；新增 raw caller 能使 CI 失败。

#### 未完成部分

- [ ] 补齐 map/schema/wire 的真实 parser/fuzz corpus（当前正向、边界和负向 fixture 已存在）。
- [x] v1 回退 artifact 可由 BOOTSEL 恢复至少一块样板（DHRT100，证据：
  `FLASH-TASK-20260823-079`）。
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

#### 已完成部分

- [x] 实现 `flash_map_find()`、partition-relative range validation 和 operation permission。
- [x] Boot/App/factory 使用不同的 generated permission view。
- [x] linker、factory image、packager、release size gate 消费显式选择的 map artifact；普通构建
  固定生成的 v1 compatibility map，v2 只能由 factory-candidate preset 与独立命名工件驱动，且
  manifest 保持 `target_not_deployed`。
- [x] 测试 active App write 拒绝、cross-partition 拒绝、Scratch lease 和所有 partition 首尾。

#### 未完成部分

- [ ] 在 DHRT100 上部署并验证 v2 分区，完成 C11 激活审核；当前板端仍保持 v1 compatibility map。
- [ ] 将 App OTA image、Product Config、App metadata writer 的 intent 证据与 Boot 独立 writer
  的依赖审计闭环归档。
证据：commit `10fd545`、`a211188`；`run_flash_map_tests.ps1` 纳入全量 host runner，release/Boot/
App A/App B 均编译链接同一 `flash_map.c`。DHRT100 通过只读 SCPI 对 generated v2 target map 和
permission view 做板端闭环；live linker/factory/packager 则由 generated v1 compatibility map
保持当前可启动地址。报告见 `HAOFV_FLASH_TASK_PROGRESS.md` 的 `FLASH-TASK-20260822-002`、
`FLASH-TASK-20260822-003` 和 `FLASH-TASK-20260822-004`。本项仍为进行中：App OTA image、
Product Config 与 App metadata writer 已走 intent，Boot metadata 保持独立 BootFlashService；
板上 v2 分区没有部署或写入，C11 激活审核未开始。受控候选构建证据见
`FLASH-TASK-20260823-037`。

### M1-03 FlashTransactionAO/FB/Vector

#### 已完成部分

- [x] 已固定首轮 one-deep queue、job/requester/operation/provider generation/completion/cancel；
  当前 OTA 两页载荷继续走 `FLASH_TRANSACTION_OWNED_PAYLOAD_SIZE` 固定池，超过该上限的请求可使用
  generation-bound immutable buffer lease，缺少合法 lease 时仍 fail closed。
- [x] 实现 `VALIDATE -> QUIESCE -> ACQUIRE -> PARK -> ERASE/PROGRAM -> VERIFY -> COMMIT ->
  RELEASE -> COMPLETE/FAILED`，每次 service 只推进一个有界步骤。
- [x] Vector 使用 seqlock；只读查询不触发 Flash IO，并记录 policy/lockout/progress/result/timing；
  thermal/health gate、policy reason 与 temperature flags 已接入。
- [x] completion 区分 accepted/programmed/verified/committed，OTA 兼容包装只消费 committed。

#### 未完成部分

- [~] 移除仍存在的同步兼容包装，完成 Boot writer、OTA_JOURNAL durable backend、运行时 abort/lease
  与跨 reset durable completion 的统一 owner 收敛：OTA_JOURNAL 默认 program/erase 已改为
  `FlashTransactionAO` owner-scoped backend，禁止直接调用通用同步 execute；remaining work 是
  Boot C11 交叉审核、真实板端掉电和 runtime producer 的全异步 checkpoint boundary。

首轮证据：commit `2a79643`、`bdc744b`、`accdfbc`、`f3d5a96`；OTA image erase/program 已从 portable callback 进入
`FlashTransactionAO/FB`，active slot 未知或写活动槽均 fail closed。host fault fixtures、release、
RTOS+双核构建和 DHRT100 双向 OTA/Vector HIL 均通过；HIL 工具在目标 image payload 首块和最终
metadata 提交后分别核对 Vector。该项保持进行中，因为当前仍有同步兼容包装，Boot writer、OTA_JOURNAL
durable backend、运行时 abort/lease 和跨 reset durable completion 尚未收敛；completion lease/journal
contract 已建立并有 host fail-closed 证据，但 live OTA/Product Config/App metadata producer
尚未接入 v2 durable completion backend。

### M1-04 Mode、温度与双核门禁

#### 已完成部分

- [x] FlashTransactionAO 已接入 Diagnostics fault、board/chip thermal critical、System/FAULT、
  Trigger activity、Calibration training 和 TDMA clock-training fail-closed gate；policy
  error/temperature flags 已进入 Vector，训练状态由 resource_arbiter snapshot 统一发布/消费，
  core0/core1 owner 发布 helper 已在 `808f825` 收敛。host 负向 fixture 已覆盖 CAL/training
  admission。
- [x] host negative fixture 覆盖 thermal critical/diagnostics fault、Calibration active 和
  TDMA training active admission，断言 raw erase/program 未执行。
- [x] Trigger capture/clock、FAULT mode、Flash resource conflict、Calibration training 和 TDMA
  clock-training 已细分为 policy reason；System owner 发布 gate 已接入。
- [x] App raw write session 的 core1 park request/ACK/release 只由 transaction owner 驱动；parked raw
  operation 在无活动 session 时 fail closed，Boot 同步 raw writer 保持独立会话边界。
- [x] 审计 RAM resident closure：代码、常量、jump table、IRQ path 不依赖 XIP。
- [x] HIL 注入 park timeout，证明 raw operation 未执行；release 后 core1 alive。

#### 未完成部分

- [ ] 补齐 RUN/CAL/thermal critical/unknown state 的板端拒绝入口；warning 只按 policy 暂停或降速，
  不能被误当成 critical fail-open。
- [ ] 修正 `CLKTRAIN state=FORWARDING` 与 arbiter snapshot 不一致，并完成 board/fault/thermal/
  CAL/training negative HIL。

### M1-05 Buffer 与 owner 收敛

#### 已完成部分

- [x] **M1-05-A 固定池 owner**：不超过 `FLASH_TRANSACTION_OWNED_PAYLOAD_SIZE` 的 payload 在
  submit 时复制入固定 pool；超过上限且没有合法 lease 的请求 fail closed，raw writer 不接收
  producer alias。
- [x] **M1-05-B immutable lease 合约**：大 payload 绑定 generation、长度、retain/release；OTA
  image、Product Config、App OTA metadata producer 已接入 generation-bound intent/lease 路径。
- [x] **M1-05-C admission 与 reset 负向**：queue full、provider generation reset（raw 前和 raw
  期间）、large-payload no-raw 均有 host fixture，验证拒绝时 raw erase/program 计数为零。
- [x] **M1-05-D 异步边界与 abort**：每次 service 只推进一个有界 step；page/sector erase/program 回调
  中触发 abort 后跳过 verify/commit，并释放 core1/Flash owner。
- [x] **M1-05-E completion lease**：accepted/programmed/verified/committed/failed 边界只发布一次；
  duplicate terminal/abort、append fail-closed 和 lease 释放均有 host 覆盖。
- [x] **M1-05-F durable journal backend 基础**：固定槽、CRC、commit marker、readback 与 reset
  recovery backend 已纳入 CMake 和 transaction host runner；inventory gate 证明 App raw
  erase/program caller 必须是 FlashTransactionAO，写 API 头文件已从通用 `drv_flash.h` 隐藏。

#### 未完成部分（关闭 M1-05 前必须完成）

- [x] **M1-05-G live producer 接入（基础）**：将 `OTA_JOURNAL` durable backend 接入实际 OTA image、Product
  Config 和 App OTA metadata producer，不能只由 host fixture 驱动。三个 producer 已接入
  FlashTransactionAO completion-lease 注入点（`dfa1f02`）；v2 启动路径现在建立真实 completion
  journal lease，并与 stream checkpoint 使用同一 partition 的互不重叠 region；completion
  journal 已支持保留最新 block 的受控 rotation，未配置 erase callback 的 portable store 仍
  fail closed。
- [x] **M1-05-G owner backend 收敛**：completion nested journal 与 stream checkpoint journal 均通过
  `flash_transaction_ao_journal_program/erase()` 进入唯一 FlashTransactionAO；生产 `ota_journal.c`
  不再调用通用 `flash_transaction_ao_execute()`。
- [ ] **M1-05-G 剩余**：完成真实 DHRT100 跨 reset/power-cut 证据、rotation/endurance HIL 和
  completion store 长期容量预算。
- [ ] **M1-05-H 跨 reset/power-cut 闭环**：覆盖 body/readback/commit marker/lane seal 各断电点，复位
  后只能得到确定的旧/新 completion，禁止悬挂 accepted 或伪造 committed。Host reset boundary
  matrix 已覆盖 body/marker/readback transport failure/readback corruption，并证明旧/新选择确定
  （`FLASH-TASK-20260823-007`）；真实掉电注入、lane seal backend 和 DHRT100 证据仍待完成。
- [ ] **M1-05-I replay/idempotence**：duplicate completion、重复提交和 provider reset 后重放必须按
  generation/job/object 去重，不能重复 program 或重复发布 terminal event。Journal backend 已
  对相同 completion 实现幂等，对冲突 payload fail closed（`195b85a`）；provider reset、live
  producer 和板端掉电 replay 仍待完成。Host 已补 store reset 后重复 completion 不占新槽的矩阵
  （`FLASH-TASK-20260823-008`）。FlashTransactionFB 现在拒绝最近 terminal 显式 job ID 的
  重复提交（`29585e9`），跨 reset 的 durable identity 仍待完成。
- [x] **M1-05-I host identity bridge**：非零 request fingerprint 现在优先于 RAM-local job/
  transaction/provider lease generation，用于 reset 后 completion find/append 幂等；durable store
  generation 仍必须匹配，冲突 payload 仍 fail closed，并由新增 host fixture 覆盖。
- [ ] **M1-05-J link-level visibility**：在 App、Boot、release 三类链接产物上证明 raw erase/program
  符号只对允许的 owner 可见，不能仅依赖源码 inventory 扫描。App 侧现已增加反汇编调用边
  fail-closed 检查（`47b15a3`），Boot 侧现已固定允许 caller 集合；独立 JSON 报告工具、
  三 profile 产物快照和 `release_check` 强制 gate 已完成（`FLASH-TASK-20260823-009`、
  `FLASH-TASK-20260823-011`、`FLASH-TASK-20260823-015`），Boot 依赖的 C11 交叉审核仍待补齐。
- [x] **M1-05-K atomic store 依赖收敛（基础）**：Product Config 已迁移到固定页 append-only record
  primitive，兼容读取旧首记录；每次更新只 program 下一个擦除页，同值不写，满槽时
  先擦除不包含最新记录的下一 sector 再追加，擦除/写入失败均 fail closed，不执行首扇区
  rewrite。OTA metadata 已由 BCB adapter 和 `pota_boot_control_facade` 承载，证据见
  `FLASH-TASK-20260823-016`、`FLASH-TASK-20260823-031`。
- [ ] **M1-05-K 剩余**：完成 M2-02 的完整 GC/wear health、跨 reset recovery 和独立 C11 raw
  visibility 审核，之后才能关闭本项。
- [ ] **M1-05-L 退出评审**：host/build/link/HIL、回退路径和独立 C11 交叉审核齐全后，才允许把
  M1-05 从 `[~]` 改为 `[x]`，并同步更新 `ARCH-FLASHOWNER-01` 登记状态。

实施快照、提交号、构建号、板端报告和未完成 gate 统一记录在
`HAOFV_FLASH_TASK_PROGRESS.md` 的 `FLASH-TASK-20260822-026`、`027`、`028`、`029`、`031`、`032`；本
TODO 只保留可独立验收的状态项和证据索引。

### M1-06 高地址 Scratch 验证

#### 已完成部分

- [x] validation-only SCPI 只提交 Scratch lease intent；FlashTransactionFB 拒绝其它分区、非零 offset、跨多 sector/page 请求。
- [x] 流程固定为 confirm token -> erase -> pattern program -> readback/hash -> erase/restore；哈希匹配和恢复擦除均进入返回证据。
- [x] DHRT100 报告已记录 identity/build、v2 map symbol/geometry、lockout、温度/电流和恢复结果；
  当前部署仍是 v1 compatibility map，v2 高地址写入保持禁止。
- [x] release binary string scan 证明 destructive validation command 不在正常 release App/Boot 工件中。

#### 未完成部分

- [ ] 增加底层 JEDEC ID 的驱动/SCPI 来源，并在允许的 v2 物理验证窗口完成高地址 Scratch 证据。

### M1 退出门禁

- [ ] `ARCH-FLASHMAP-01` 和 `ARCH-FLASHOWNER-01` 的 host/build/HIL 证据齐全。
- [ ] 所有 App writer 已走 intent；Boot writer 依赖白名单不受 App AO 污染。
- [ ] 高地址验证只触碰 Scratch，重启后 Boot/identity/当前 App 不变。

## 五、M2 Stores、namespace 与部署对象

目标：所有业务域复用少量 store primitive，不再创建私有 sector/offset 服务。

### M2-01 Store core

#### 已完成部分

- [x] common record envelope、CRC/hash、generation、commit marker 和 version compatibility 已落地。
- [x] NVS append/scan planner 已落地，负责 aligned span、最新记录选择和 torn tail 识别；不执行
  Flash IO，实际写入仍由 FlashTransactionAO owner 负责。

#### 未完成部分

- [ ] 实现 NVS sector rotation、Blob immutable object/atomic ref、FCB append ring/GC。
- [ ] GC 只能提交 FlashTransaction job；空间不足时返回明确 backpressure/reason。
- [ ] power-cut fixtures 覆盖 body/commit/ref/GC 各边界，始终选出确定的旧或新事实。

### M2-02 Product NVS

#### 已完成部分

- [x] USB mode/board number 已有 Product Config intent 与默认策略。

#### 未完成部分

- [ ] 将 versioned key/namespace、identity/capability/permission 记录接入 Store core。
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

#### 已完成部分

- [x] Boot target 已独立链接 geometry/map、BCB/metadata、image/vector validator、Raw HAL 和 ROM
  recovery 相关路径；`BootFlashService` 已成为活动 map 的 App A/App B/Boot Control erase/program
  唯一 raw owner（证据：`FLASH-TASK-20260823-015`、`FLASH-TASK-20260823-037`）。
- [x] `boot_flash_service_erase/program` 对构建期活动 generated map 的可写分区执行
  sector/page 对齐、长度和分区边界检查；Bootloader 镜像复制与 metadata adapter 均经由该 API，
  raw inventory/link gate 拒绝其它 Boot caller。
- [x] link gate 已接入 Boot map/dis，拒绝 RTOS、SCPI、TDMA、FatFs、littlefs、FlashTransactionAO、
  resource arbiter、storage manager 和 App OTA AO 符号；当前 build 通过。
- [x] Bootloader size 使用生成的活动 partition symbol gate，不使用文档硬编码阈值；
  `__flash_binary_end` 必须落在 `FLASH_ACTIVE_MAP_BOOTLOADER_ORIGIN/LENGTH` 界内。

#### 未完成部分

- [ ] 补齐 BCB payload、wear counter、Recovery 和 v2 map deployment 的最终依赖收敛。

### M3-02 BootControlStore

- [x] portable primitive 已实现双 lane append/select/commit/GC 和 lane generation；v1
  `ota_metadata.c` 已接入实际 BCB payload 和 BootFlashService/FlashTransactionAO adapter，
  现在再经独立 `pota_boot_control_facade` owner boundary（`FLASH-TASK-20260823-031`）。盘上
  seal/record 可重建 valid lane/record、最新 lane generation/sequence/security counter，并通过
  只读 SCPI 投影（`FLASH-TASK-20260823-036`）。
- [x] primitive 在无有效 lane 时返回 `NO_VALID`，不创建默认记录；DHRT100 已完成
  BCB-backed A/B OTA/reboot/confirm 闭环（`FLASH-TASK-20260823-017`）。
- [x] host fault fixture 覆盖 body/readback/commit/lane seal/旧 lane erase，均 fail closed 并保留旧记录。

#### 未完成部分

- [ ] 完成产品寿命阈值/累计失败统计、v2 schema migration 和 Boot Recovery policy 接入。

### M3-03 Direct A/B 单主线

#### 已完成部分

- [x] 新增纯策略 `pota_direct_ab_decide()` façade，明确 no-pending、pending test boot 和
  attempt-exhausted rollback 的输入/输出，并已接入 Boot 实际 pending 状态机（证据：
  `FLASH-TASK-20260823-028`、`FLASH-TASK-20260823-032`）；DHRT100 已完成 no-confirm、
  attempt increment 和 attempt-exhausted rollback HIL（`FLASH-TASK-20260823-057`）。

#### 未完成部分

- [ ] 完成 A/B slot-specific image、vector/reset handler、hash/signature/compatibility 全 fault matrix，
  并接入 Recovery 与独立 C11 审核。
- [ ] v2 删除 `COPY_TO_ACTIVE` 运行分支和可写 mode 命令；历史查询返回明确 legacy/unsupported。

### M3-04 Manifest、signature 与 anti-rollback

- [x] 已选定 Mbed TLS SHA-256 + ECDSA P-256，portable verifier 通过 crypto callback 与平台解耦。
- [x] key registry 已分离 dev/release/factory role，并拒绝未知、重复、撤销和角色不允许的 key；
  生产 key 表保持空表时 fail closed。
- [x] 固定 manifest extension parser/packager boundary：支持 security counter、key ID、required
  signature 和外部 verifier callback；RP2350 已接入 low-S P-256 verifier，缺少 verifier、签名或
  counter 回退时 fail closed。验签后的 counter 已传入 pending BCB；v2 签名策略拒绝 raw begin/
  resume。
- [x] portable BCB primitive 已拒绝低于当前有效记录的 `security_counter`（证据：
  `FLASH-TASK-20260823-023`）；metadata pending 写入已保存 verified counter，confirm/copy/repair 继承
  最新 counter，证据见 `FLASH-TASK-20260823-041`。
- [x] 离线工具可输出 canonical signing transcript/request，并只接受外部 raw P-256 签名；私钥不
  生成、不读取、不入库，未配置 counter/key 时不生成 v2 package。

#### 未完成部分

- [ ] 完成 RP2350 OTP binding、STM32 实际 crypto port、产品 counter source、OTP 灌装、rotation
  ceremony、泄露处置 runbook、SBOM、release/factory signing ceremony。
- [ ] Boot 重新验证 slot manifest、OTP/key binding 和产品 counter 来源，并补齐掉电矩阵。

### M3-05 Recovery 与 factory artifact

#### 已完成部分

- [x] Recovery 已具备只读 map/BCB health 诊断和显式 ROM BOOTSEL handoff；新增
  `tools/factory_package/factory_package.py` 对 signed factory baseline 做确定性 region/hash/
  full-erase/key-profile fail-closed 验证，并由 `tools/factory_restore/factory_restore.py` 在
  显式执行前校验 UF2 地址集合，证据见 `FLASH-TASK-20260823-058`、`059`；只读 Recovery
  边界证据见 `FLASH-TASK-20260823-038`。
- [x] Recovery 分区在 v2 map 中保持 factory-only 写权限，Recovery 镜像本身不链接 raw Flash writer；
  Recovery 镜像本身不链接 raw Flash writer。
- [x] 独立命名且受 factory flag 保护的 v2 candidate artifact 已包含 Bootloader、Slot A、Recovery、
  有效 lane0 BCB、canonical map manifest，并声明其余 store 必须在 full erase 后保持 erased；证据见
  `FLASH-TASK-20260823-037`、`FLASH-TASK-20260823-038`。
- [x] factory report 已覆盖并由 consumer 重算全部已编程 region 的 size/SHA-256，同时要求
  `full_erase_required=true`。

#### 未完成部分

- [ ] 实现 Recovery 运行时 factory package 验证、受控 USB/SD restore、更新授权/签名验证和实际
  覆盖负向 HIL。
- [ ] 执行空片恢复，取得 full erase 后无旧 metadata 残留的 DHRT100 证据。

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

## 七、M4 本地 OTA 与 DHRT100 样板迁移

目标：USB CDC、USBTMC、UART、RS485、SD 共用一个 transport-neutral session，并在 DHRT100 单板
样板（当前物理验证板为 DHRT100）完成 v2 迁移闭环。

### M4-01 OtaStreamSession core

#### 已完成部分

- [x] 已新增 transport-neutral `pota_stream_session` 的 open/write/close/abort 状态骨架，顺序、
  重复和冲突 chunk 有 host 负向证据；raw-image resume 与 durable abort tombstone 已接入 M4-02
  journal，签名 package parser/image cursor resume 已完成 host/build 接入，证据见
  `FLASH-TASK-20260823-044`。
- [x] session descriptor 已绑定 identity/capability/package hash/object/map/partition/generation/
  destination，并校验 inactive-write/durable-ACK capability。
- [x] descriptor 已区分 object/destination 并拒绝错误 App partition；CDC source 已从完整签名
  package 只发送原始 manifest header + inactive image object，无关 slot image 不传输，证据见
  `FLASH-TASK-20260823-044`。
#### 未完成部分

- [ ] 统一跨 ingress 接入，并让 sink 只向 FlashTransactionAO 提交 intent，verified object 后才写
  pending BCB。

### M4-02 Journal 与 durable resume

#### 已完成部分

- [x] `pota_stream_session` 已可配置 `pota_stream_checkpoint_store`，在底层 program/readback
  成功后按 interval/final policy append checkpoint，并在 append/recover 失败时 fail closed；真实
  v2 `OTA_JOURNAL` backend 已通过 FlashTransaction owner 写入并在固件启动时 fail closed 接入，
  raw-image core 已按 descriptor/map/partition/token/CRC 恢复，并由 bounded service 校验 durable
  prefix、清理未确认尾部后发布 cursor；package core 会在恢复前重验 manifest/signature/counter，
  再校验 inactive image 前缀并恢复紧凑对象 cursor，证据见 `FLASH-TASK-20260823-039`、`040`、
  `044`。
- [x] portable policy primitive 已按 monotonic byte interval/final offset 决定 checkpoint，
  raw-image 非最终 checkpoint 进一步约束到活动 Flash erase-sector 边界，不按每 chunk 擦写
  （证据：`FLASH-TASK-20260823-022`、`040`）。
- [x] checkpoint identity 绑定 session/generation/token/object/total/package CRC，并拒绝元数据
  或 token 冲突；token 已固定为 little-endian OPEN wire CRC，活动 map 与 App partition ID 来自
  generated deployment map；abort 会持久化 tombstone，同 session/generation 重启被拒绝且提升
  generation 后只能从 offset 0 开始，证据见 `FLASH-TASK-20260823-043`。
- [x] host primitive 与真实 backend adapter 已覆盖重建、torn body/commit、CRC/readback corruption、
  page/sector 对齐、分区边界、requester 权限、durable prefix 损坏、bounded scan 和尾部清理；journal
  写满后只擦除不含最新有效 checkpoint 的下一 erase block，erase/verify 失败时保留旧最新记录并
  fail closed，证据见 `FLASH-TASK-20260823-042`。

#### 未完成部分

- [ ] 完成板端跨 reset/power-cut durable resume、跨 ingress producer 接入、wear/retransmit profile、
  journal rotation 和 endurance HIL。

### M4-03 Local ingress regression

#### 已完成部分

- [x] `pota_stream_ingress` 已通过 portable port 接入 App OTA AO 和实际 FlashTransaction owner；
  USB CDC/USBTMC SCPI 控制面使用固定 little-endian OPEN、每帧 CRC、source admission 和
  status/BOOT 投影，独立 CDC sender 已支持从 journal 恢复 raw image 和签名 inactive package
  object（证据：`FLASH-TASK-20260823-033`、`034`、`040`、`044`）。
- [x] Session 已在 OPEN 阶段拒绝未绑定 checkpoint storage 的 `DURABLE_ACK`，并在 header
  acceptance 阶段拒绝完整 A+B package，host 断言 erase/program 计数不增加；当前实现直接将
  manifest header + inactive image object 交给 FlashTransaction sink，未建立独立完整 package
  cache。证据：`FLASH-TASK-20260823-068`。

#### 未完成部分

- [ ] 接入 SD/UART/RS485 真实 producer、USBTMC/VISA sender，并完成五类板端回归。
- [ ] 完成乱序、重复、CRC、truncate、overflow、abort、zero storage、wrong slot/package 的全量
  fail-closed matrix。
- [ ] USB CDC、USBTMC、UART、RS485、SD 的 A->B、B->A、resume、revert、Recovery 回归通过。

### M4-04 DHRT100 样板 factory migration（物理 gate）

#### 已完成部分

- [x] v2 candidate 已能在隔离 preset 下生成并通过布局/链接检查；factory artifact 保持
  `target_not_deployed`。

#### 未完成部分

- [ ] DHRT100 尚未取得 v2 空片、烧录和启动证据；物理 gate 必须按迁移前快照、BOOTSEL/full erase、
  启动复核和回退顺序执行。
- [ ] 迁移前记录 `*IDN?`、build、slot/result、board identity、Product Config、sensor snapshot。
- [ ] BOOTSEL/factory full erase/reflash v2，确认 USB 重新枚举和 identity 转换策略。
- [ ] 运行 M1 Scratch、Product NVS、Calibration empty/default、Boot fault subset。
- [ ] 恢复 TDMA/Calibration 单板 persona，确认 PIO/DMA/IO owner 未被 Flash 重构破坏。
- [ ] 执行 v1 rollback drill，证明 artifact/runbook 可恢复。

### M4 退出门禁

- [ ] `ARCH-OTASTREAM-01` 的五类本地 transport、durable offset 和 resume 证据齐全。
- [ ] DHRT100 在 v2 上完成 A/B/revert/Recovery 和关键 store 重启验证。
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
- [ ] DHRT100、两板、四板 HIL 报告关联具体 commit/artifact/tool version。
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
| DHRT100 单板样板（物理 gate） | Scratch、store reboot、A/B/revert/Recovery/local resume | M1/M2/M3/M4 | M6-03 |
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
4. 涉及硬件的任务有对应 DHRT100/两板/四板原始记录，不能用模拟代替。
5. HAOFV Vector、reason、generation/freshness 和 durable completion 可查询。
6. 回退 artifact/runbook 已实测；不可逆步骤有 factory/Recovery 路径。
7. 文档四项门禁全绿；契约状态变化完成独立 C11 交叉审核。
8. 代码与文档分离 commit，并推送当前私有分支。
