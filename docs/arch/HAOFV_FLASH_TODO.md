# HAOFV 板载 Flash 域实施待办

Status: Active
Domain: HAOFV / Flash / OTA / Storage
Canonical: `docs/arch/HAOFV_FLASH_TODO.md`
Related: `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`, `docs/ota/OTA_TODO.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/storage/SD_TODO.md`
Last updated: 2026-08-21

本文把 `HAOFV_FLASH_ARCHITECTURE.md` 拆成可提交、可回退、可板测的任务。状态约定：
`[ ]` 未开始，`[~]` 进行中，`[x]` 有证据完成，`[!]` 阻塞。仅编译通过不能标记 HIL 完成。

## 一、阶段总览

| 阶段 | 目标 | 进入条件 | 退出证据 |
|---|---|---|---|
| F0 | 冻结事实与迁移策略 | 架构评审通过 | map/owner/BCB/stream 契约和风险表 |
| F1 | Geometry/FlashMap 单一来源 | F0 完成 | host 静态检查、生成 linker/tool 地址 |
| F2 | FlashTransactionAO | F1 完成 | 唯一 writer、core1 lockout、job 单测/HIL |
| F3 | NVS/blob/FCB stores | F2 完成 | torn write/GC/wear 单测与板测 |
| F4 | Boot v2 + Direct A/B + Recovery | F1-F3 基础可用 | A/B/revert/recovery/BCB 掉电闭环 |
| F5 | 本地 OTA 与 factory 迁移 | F4 完成 | USB/SD OTA、factory erase/reflash、高地址验证 |
| F6 | TDMA 流式 OTA | TDMA reliable bulk 稳定，F5 完成 | 两板再四板 stream/resume/滚动升级闭环 |
| F7 | 安全、寿命与发布 | F4-F6 完成 | 签名、anti-rollback、wear、release report |

## 二、F0 架构冻结与基线

### F0-01 现状清单

- [ ] 输出机器可读清单：所有 `drv_flash_read/erase/program/xip_ptr` 调用点、所有 offset/size
  定义、linker ORIGIN、factory image 地址和 host tool 假设。
- [ ] 为每个调用点标记 owner、core、system mode、partition、写频率、掉电语义和替代 API。
- [ ] 验收：CI 中保存扫描结果；新增裸写调用会失败。

### F0-02 迁移策略

- [ ] 决策记录明确：开发板采用 factory full erase/reflash，不做在线 v1 -> v2 搬迁。
- [ ] 定义旧 map/未知 map/空白 Flash 的 Bootloader 行为和用户可见恢复信号。
- [ ] 备份现有 COM8 的 identity、Product Config、OTA metadata、校准/报告索引；标注哪些可恢复、
  哪些只是诊断快照。
- [ ] 回退：保留当前 HEAD 对应 factory UF2 和验证记录，可通过 BOOTSEL 恢复 v1。

### F0-03 分区契约 source of truth

- [ ] 建立 `flash_geometry` 和 `flash_map` 的机器可读定义；数字只在该定义出现一次。
- [ ] 为每个 partition 定义 ID、offset、size、erase/program alignment、read/write owner、Boot/App/
  factory permission、executable、store type 和 update policy。
- [ ] 生成或校验 C header、linker memory、factory image address、package tool 和文档表。
- [ ] 静态断言：对齐、无重叠、A/B 等长、bootable region 合法、表尾匹配
  `PICO_FLASH_SIZE_BYTES`。
- [ ] 验收：故意制造 overlap、gap policy violation、错误容量和 linker 偏移时构建失败。

### F0-04 BCB/Image Manifest 契约

- [ ] 冻结 BCB v1 record、lane 选择、commit marker、sequence 比较、GC 和 torn-write 规则。
- [ ] 冻结 Image Manifest v2/TLV、hash/signature/key ID/security counter、slot/link address 和
  compatibility 规则。
- [ ] 形成 golden vectors：合法、未知可选 TLV、未知必需 TLV、截断、长度溢出、CRC 错、
  hash 错、签名错、rollback counter 低。

### F0-05 TDMA stream 契约

- [ ] 冻结 capability、session identity、wire version、消息类型、sequence/offset、CRC、durable
  cumulative ACK、credit、resume token 和 reject reason。
- [ ] 明确 DSoftBus 理念映射只包括 capability/session/lane/QoS/stream，不包含动态发现和路由。
- [ ] 明确 `TdmaSchedulerAO`、`OtaAO/OtaDistributionFB`、`FlashTransactionAO` 三个 owner 的
  command/vector/completion 边界。
- [ ] 形成 wire golden vectors 和 parser fuzz corpus；未完成前 registry 保持 `pending`。

### F0-06 各主域持久化需求冻结

- [ ] 逐域确认 System/Product、Trigger/Loop、SYNC_IO/PIO、Calibration、TDMA、RefMem、VDC、
  Communication、Measure、OTA/Boot、Storage、Diagnostics 和 UI 的 persistent/live 边界。
- [ ] 每个待持久化对象登记 owner、schema/version、compatibility、frequency/endurance、权限、
  atomicity、rollback、default/factory policy、diagnostic projection 和 SD evidence 去向。
- [ ] 建立 namespace registry；业务域只能申请 `ProductNVS/CalibrationNVS/VdcNVS/SystemPack/
  FaultFCB` 中的对象，不得直接申请裸 offset 或建立私有 erase/program 服务。
- [ ] 对所有 Domain Vector、ECC state、queue/FIFO、lock/counter/cursor 做 negative inventory，
  证明重启不会把上次 live snapshot 恢复成当前事实。
- [ ] 把新增容量需求先映射到 store object budget；只有 store 语义无法满足时，才提出新 map
  version 与 partition 申请，并附 endurance、recovery 和 migration 证据。

## 三、F1 Geometry 与 FlashMap

### F1-01 Raw Flash HAL

- [ ] 将 driver 总容量改为只引用 geometry；删除独立的小容量常量。
- [ ] `read/xip_ptr/erase/program` 使用溢出安全 range 检查，覆盖 `offset + length` 边界。
- [ ] 区分 raw HAL 和 policy API；raw erase/program 头文件只对 Boot/FlashTransaction target 可见。
- [ ] host 单测：零长度、末字节、越界一字节、整数溢出、未对齐、空指针和高地址范围。

### F1-02 Partition API

- [ ] 实现 `flash_map_find(id)`、`flash_map_validate_range(id, offset, length, operation)` 和
  permission 查询，不向业务域暴露可随意组合的裸地址。
- [ ] 为 Boot/App/factory build 生成不同 permission view。
- [ ] 单测覆盖所有 partition 的首尾、cross-partition、active-slot write 拒绝和 scratch lease。

### F1-03 Linker/factory/tool 同源

- [ ] A/B linker ORIGIN/LENGTH 从 map 生成或由 map checker 比对。
- [ ] factory UF2 输入地址和 metadata/BCB 初始化镜像从 map 生成。
- [ ] OTA packager 读取 map manifest，校验 A/B 镜像的 vector/reset handler 位于目标 slot。
- [ ] release gate 比对 ELF map、bin size、package manifest、factory UF2 block address。

### F1-04 高地址非破坏验证工具

- [ ] 增加 validation-only SCPI intent 和 host 工具，只租用 scratch test sector。
- [ ] 流程固定为备份/确认空闲 -> erase -> pattern program -> readback/hash -> erase restore。
- [ ] 命令需 validation build flag；release 二进制字符串扫描确认不存在。
- [ ] COM8 验收报告记录 JEDEC/geometry、测试 offset symbol、pattern hash、lockout 和恢复状态。

## 四、F2 FlashTransactionAO

### F2-01 AO/FB/Vector 骨架

- [ ] 定义 bounded queue、job ID、requester、partition lease、operation、immutable data provider、
  completion event 和 cancel token。
- [ ] 实现 ECC：`IDLE/VALIDATE/QUIESCE/ACQUIRE/PARK/ERASE/PROGRAM/VERIFY/COMMIT/RELEASE/
  COMPLETE/FAILED`。
- [ ] `FlashTransactionVector` 使用 seqlock；查询不触发 Flash IO。
- [ ] FB action 每次只做一个受控步骤，budget overrun 进入诊断而非循环追赶。

### F2-02 mode/policy gate

- [ ] 接入 System mode、Trigger activity、Calibration training、TDMA maintenance gate 和
  Diagnostics thermal flags。
- [ ] RUN/CAL/new thermal critical 下拒绝新写；warning 策略可暂停或降低 job service rate。
- [ ] 定义 policy reason 枚举和 SCPI 文本映射；未知状态默认拒绝。

### F2-03 core1 lockout

- [ ] 将 lockout request/ACK/timeout/release 只放入 transaction owner，不让业务 adapter 重复调用。
- [ ] 审计 RAM resident closure：调用图、常量、跳转表、IRQ handler 均不依赖 XIP。
- [ ] HIL：core1 online 正常写、ACK timeout 注入拒绝写、release 后 core1 alive、TDMA/Trigger
  counter 连续性符合维护模式预期。

### F2-04 owner 收敛

- [ ] 迁移 `portable_ota_port`、`ota_metadata`、`product_config` 的 erase/program 到 intent API。
- [ ] Bootloader 仅保留 `BootFlashService` raw access；App target 的链接/visibility gate 禁止其他调用。
- [ ] CI 扫描和 link map 证明 App 裸写符号只被 FlashTransaction 模块引用。

### F2-05 buffer 生命周期

- [ ] 禁止事件总线跨 tick 保存临时 stack/SCPI buffer 指针。
- [ ] 小 payload 入队复制；大 payload 使用带 generation/refcount 的 immutable provider 或固定 pool。
- [ ] completion 前 producer 不复用 buffer；abort/reboot 路径释放 lease。
- [ ] 压力单测：queue full、producer reset、duplicate completion、abort during page/sector。

## 五、F3 Stores

### F3-01 BootControlStore

- [ ] 实现双 lane append、record validate/select、commit marker、lane GC 和 wear counter。
- [ ] 每个写入边界做 reset/torn-write 注入，始终保留至少一个有效 record。
- [ ] 无有效 BCB 不创建“默认可启动”事实，交给 Boot recovery policy。

### F3-02 ProductConfigNVS

- [ ] 为现有 USB mode/board number 定义 versioned keys 和默认值策略。
- [ ] 迁移单 sector overwrite 为 append journal/sector rotation；同值写不产生新 record。
- [ ] 导入 v1 配置只允许 factory tool 显式执行；App 不在线猜测旧布局。
- [ ] HIL：循环写、复位注入、GC、最新有效值、erase 分布和寿命计数。

### F3-03 CalibrationNVS

- [ ] 定义 candidate/accepted/active/previous record 和 atomic active ref。
- [ ] key 绑定 unique board/link endpoints、topology/profile/schedule CRC、generation/freshness。
- [ ] `CALibration:SAVE` 只提交 accepted package；diagnostic-only、过期或 CRC 不匹配拒绝。
- [ ] VDC 只读取 active accepted snapshot；加载失败进入未校准/relock gate，不沿用未知旧值。

### F3-04 VdcNVS

- [ ] 定义 servo/holdover/reference/timestamp dictionary/discipline profile namespace，和
  Calibration NVS 的 link delay/bias source facts 分离。
- [ ] aging/temperature/wander 只能先写 candidate，经长窗口证据、profile CRC、维护态显式
  accept 后切 active；RUN 中不得持久化。
- [ ] boot loader 不读取 VDC NVS；App 启动校验 profile 后仍从 `OFF/CHECKING` 重新锁相。
- [ ] 单测/HIL 证明保存的 `offset/rate/DCO/LOCK/HOLDOVER/map generation` 不会被恢复为 live fact。

### F3-05 BlobStore

- [ ] 定义 immutable object header、chunk bitmap、object hash、manifest 和 atomic ref。
- [ ] OTA cache、System Pack 子集和 Recovery 更新采用不同 namespace/permission。
- [ ] 先实现定长 extent + manifest；只有确有目录/小文件需求时再评估 upstream littlefs。
- [ ] power-cut 单测覆盖 body 完整但 ref 未 commit、ref torn、旧/新 object 同时存在和 GC。

### F3-06 RefMem deployment persistence

- [ ] `SYSTEM_PACK` blob 保存完整 `.rmtp` package 与 active/previous atomic ref；不保存 live
  64 KB vector snapshot 作为可直接运行镜像。
- [ ] 上电只把选中 package 送入 RefMem staging；CRC/schema/owner/resource/DeploymentGate
  通过后由 `DistributedRefMemAO` 激活，并建立新 epoch。
- [ ] 明确禁止持久化恢复 dirty、command、ACK/NACK、heartbeat、stale、peer online、in-flight
  和 RUN completion；peer 在重新同步前必须 stale。
- [ ] HIL：active package 正常加载、package torn 回退 previous、两份都坏进入 factory profile、
  重启后旧 ACK/epoch 不能误清新 dirty。

### F3-07 FaultEventFCB

- [ ] 定义 boot/reset/power/Flash/OTA/critical sensor 低频事件 schema。
- [ ] 环形追加、sector rotation、CRC/torn skip、overflow/high-watermark 统计。
- [ ] 高频 TDMA/Trigger trace 保持 SD；FCB producer 限速、合并重复事件。

### F3-08 wear/health

- [ ] 每个 store 发布 erase estimate、高水位、GC count、torn record、CRC failure 和 bad region。
- [ ] 建立 endurance budget：按产品预期写频率计算，不用短台架结果代替寿命分析。
- [ ] `SYSTem:FLASH:WEAR?` 只读快照；导出到 SD acceptance report。

### F3-09 主域 namespace 收敛

- [ ] System/Product：迁移 identity、USB mode、capability、permission；验证 live mode/resource
  lock/command slot 不被恢复。
- [ ] Trigger/Loop：把 named sequence/recipe/mission 与 safe limit 打包进 Deployment Capsule；
  禁止持久化 ARM/RUN/cursor/live queue/deadline，任务历史写 SD。
- [ ] SYNC_IO/PIO：生成只读 `PioProgramCatalog`，System Pack 只选择 ID/resource claim；验证
  boot 不恢复 SM/FIFO/DMA/IRQ/persona runtime。
- [ ] TDMA：把 foundation/operating/process-image/payload budget 归并到 System Pack schema；
  ring runtime、counter、in-flight 和 maintenance gate 始终从 STOPPED/closed 重建。
- [ ] Communication：adapter/address/role 配置归 System Pack，accepted physical latency 归
  Calibration NVS；RX/TX live state 与在线状态不得落盘。
- [ ] Measure：通道/量程/单位/trigger/compression profile 保存为部署对象，raw capture、波形、
  完整 evidence/report 经 StorageAO 写 SD，不允许采样 callback 触发 Flash write。
- [ ] Diagnostics/UI：关键低频事件写 FCB、产品必要偏好写 Product NVS；周期传感器流、高频
  trace、当前页面/光标/未确认编辑值保持 RAM 或 SD。
- [ ] 为每个 namespace 增加 schema compatibility、unknown-required-field、factory default、
  downgrade/rollback 和 torn-write 测试，并在 SCPI 仅投影 owner Vector/completion。

## 六、F4 Boot v2

### F4-01 最小 Bootloader 依赖

- [ ] Boot target 只链接 geometry/map、BCB、image verifier、raw HAL、watchdog/LED/ROM recovery。
- [ ] 生成依赖白名单；禁止 RTOS、SCPI、TDMA、FatFs、littlefs 和业务组件进入 link map。
- [ ] Bootloader size gate 使用分区符号计算。

### F4-02 Direct A/B 单主线

- [ ] 删除 v2 factory default 的 copy-to-active 选择和运行分支。
- [ ] pending -> test boot -> explicit confirm；未确认 reset/attempt exhausted -> previous confirmed。
- [ ] A/B slot-specific 镜像、package selector、vector range 和 hash/signature 验证单测。
- [ ] 工具与 SCPI 不再把旧 mode 切换当成 v2 capability；历史命令返回明确 unsupported/schema。

### F4-03 Recovery

- [ ] 构建独立 recovery image，功能最小化：诊断 map/BCB、验证 factory package、USB/SD 恢复。
- [ ] A/B 都无效时验证 Recovery；Recovery 无效时进入 ROM BOOTSEL 可识别状态。
- [ ] Recovery 普通 OTA 更新需要更高权限/签名策略，不能由任意 TDMA sender 覆盖。

### F4-04 signature/anti-rollback

- [ ] 选定签名算法和库，评估 RP2350 ROM/OTP/key storage 能力及 STM32 可移植层。
- [ ] 定义 dev/release/factory key 分离、key ID、rotation/revocation 和泄露处置。
- [ ] security counter 更新必须掉电安全；低 counter 镜像即使 CRC 正确也拒绝。
- [ ] release 工具在离线环境生成签名并输出可审计 manifest/SBOM。

### F4-05 Boot HIL

- [ ] Factory boot A。
- [ ] A -> B 与 B -> A。
- [ ] pending 未确认回滚。
- [ ] pending hash/signature/vector 错误回滚。
- [ ] BCB 单 lane、双 lane 损坏。
- [ ] A/B 损坏进入 Recovery。
- [ ] Recovery 损坏进入 ROM/factory 恢复指示。
- [ ] 每个结果通过 SCPI/SD report/LED evidence 可审计。

### F4-06 PIO Program Catalog

- [ ] 清点所有 `.pio` program/persona，生成 program ID、version/hash、instruction count、PIO/SM/
  side-set/pin/DMA/resource claim catalog，并纳入签名 App manifest。
- [ ] System Pack 只选择 catalog ID；DeploymentGate 拒绝未知 ID、版本不兼容、instruction
  memory 超额、IO/DMA/SM 冲突和 owner mismatch。
- [ ] persona 切换 HIL 验证 SM/DMA stop -> safe IO -> load -> start -> snapshot；不产生 Flash write。
- [ ] BlobStore object type gate 拒绝 executable PIO/native object；未来独立 bytecode 更新另立契约。

## 七、F5 本地 OTA 与 factory 迁移

### F5-01 OtaStreamSession

- [ ] 从 transport callback 中拆出统一 open/write/close/abort/resume API。
- [ ] session 绑定 package hash、identity、map version、generation 和 destination policy。
- [ ] USB、VISA、SD adapter 回归使用同一核心，状态/错误/progress 文本保持兼容映射。

### F5-02 stage/install policy

- [ ] package manifest 与 slot-specific image object 分离；source 根据 receiver active slot/capability
  只发送目标 inactive slot object。
- [ ] USB/SD/TDMA 默认直接事务化写 inactive slot；`OTA_STAGE` 只用于 manifest、受控 chunk
  spill 或未来 delta，不保存完整 A+B package。
- [ ] direct sink 支持 journal/readback resume；不能因没有完整 cache 返回虚假 durable offset。
- [ ] host/SD source 保留完整统一 package；目标板不接收、不落盘无关 slot object。

### F5-03 factory image

- [ ] factory artifact 包含 Bootloader、Slot A、Recovery、初始化 map manifest/BCB 和空 store 基线。
- [ ] factory 工具执行目标确认、备份可迁移 identity、full erase、program、readback verify。
- [ ] 禁止把旧 metadata 留在新 BCB 地址；factory report 记录每个 region hash。

### F5-04 COM8 迁移闭环

- [ ] 记录迁移前 `*IDN?`、build、OTA slot/result、board number 和传感器 snapshot。
- [ ] BOOTSEL/factory 重刷 v2，确认 USB 重新枚举和 identity 恢复策略。
- [ ] 执行高地址 scratch HIL、ProductConfigNVS 写读、CalibrationNVS 空/默认门禁。
- [ ] 执行 USB A->B、B->A、未确认回滚和 Recovery。
- [ ] 恢复 TDMA/Calibration 单板 persona，确认 Flash 迁移未破坏 PIO/DMA/IO owner。

## 八、F6 TDMA 流式 OTA

### F6-01 wire/parser

- [ ] 实现 OPEN/ACK/RESUME/DATA/CLOSE/ABORT/STATUS 编解码和 golden vectors。
- [ ] 所有 length/offset/sequence 运算做溢出检查；未知 required flags fail closed。
- [ ] fuzz：截断、超长、重复、乱序、交叉 session、identity/hash/generation mismatch。

### F6-02 capability/session/lane

- [ ] capability 包含 receiver version、map version、max chunk/credit、security、cache 和 Recovery 状态。
- [ ] board unique ID + accepted topology 形成 network identity；NO 只作显示/slot，不作安全 identity。
- [ ] session open 前选择 `TDMA_TRAFFIC_RELIABLE_BULK`；运行中不动态换 lane。

### F6-03 credit/backpressure/durable ACK

- [ ] credit 由固定 RX pool、Flash job depth、journal checkpoint 和 maintenance gate 联合计算。
- [ ] durable ACK 只在 program/readback 完成后推进；queue accept 不推进 durable offset。
- [ ] credit=0 可长期暂停而不丢 session；sender 不 busy-loop、不挤占 config/realtime traffic。

### F6-04 resume journal

- [ ] checkpoint 粒度按 wear budget 和重传成本选择，由代码 profile 定义。
- [ ] token 绑定 package hash/map/partition/generation；不匹配时 restart/abort，不拼接旧数据。
- [ ] 复位后用 journal + readback 重建；journal torn 时退回最近可信 durable checkpoint。

### F6-05 两板 HIL

- [ ] 在 accepted topology/profile 上，USB 向源板送 package，源板经 TDMA stage 到目标板。
- [ ] 注入丢帧、重复、CRC 错、credit=0、目标复位、源复位和 session timeout。
- [ ] OTA 期间 VDC/RefMem traffic quality 不出现新增 deadline miss/window overrun。
- [ ] target reboot/confirm/revert 后重新加入 topology，generation/freshness 正确更新。

### F6-06 四板滚动升级

- [ ] OtaDistributionFB 建立 per-node vector：capability、durable offset、verify、pending、result。
- [ ] 定义 cohort/stage-all/commit policy；单节点 ACK 不代表全组完成。
- [ ] 先升级非 reference，验证重入；迁移 reference role 后再升级原 reference。
- [ ] 失败节点隔离并保留旧固件；环网降级/恢复有显式 topology generation 和报告。
- [ ] 最终报告包含每节点 image hash、security counter、session/retry、boot result 和 timing quality。

## 九、F7 安全、寿命与发布

### F7-01 destructive fault matrix

- [ ] 自动化复位/掉电注入覆盖 BCB、NVS、blob ref、FCB、OTA cache/journal 和 image install。
- [ ] 每个注入点定义期望的旧/新事实，不接受“能启动但状态未知”。
- [ ] 使用可控电源/继电器台架，记录循环数、失败率、Flash health 和原始日志。

### F7-02 long-run/wear

- [ ] 配置、校准保存、故障事件和 OTA resume 分别做加速寿命测试。
- [ ] 验证 sector rotation 均匀度、GC 最坏耗时、温度 gate 和看门狗行为。
- [ ] 根据实测更新写频率 policy，不在 RUN/CAL 放开普通持久化。

### F7-03 release gates

- [ ] docs/check、host unit、fuzz corpus、main build、size/map、link dependency、release string scan 全绿。
- [ ] factory/OTA/recovery artifacts 具有 manifest、hash、signature、build ID、map version 和 SBOM。
- [ ] private key 不进入仓库/build log/report；dev key 产物不得通过 release gate。
- [ ] 归档 COM8、两板、四板 HIL 报告并链接 release checklist。

### F7-04 删除技术债

- [ ] 删除旧 4 MiB driver 上限、重复 offset、W25Q32 当前事实、copy-to-active runtime 分支。
- [ ] 删除业务组件裸 Flash 写和 Product Config 单 sector overwrite。
- [ ] 删除 OTA 对整片 Flash 的隐式所有权以及旧 mode 可写 SCPI。
- [ ] 旧文档转 Deprecated/legacy 并保留历史，不物理删除契约登记。

### F7-05 未来产品与跨平台准备

- [ ] 定义 `DeploymentCapsule` manifest：ApplicationMap、role/persona、profile、mission、
  calibration ref、capability requirements、schema 和 firmware compatibility。
- [ ] 明确当前 capsule 只选择静态编译的 AO/FB/persona；验证未知 role、未知 required schema、
  超容量和资源 claim 冲突均 fail closed。
- [ ] 为分布式仪表、运动控制、DAQ、ATE 各建立一个 storage usage model，逐项区分
  critical NVS、active blob、FCB、RAM stream 和 SD evidence。
- [ ] 建立 geometry capability profile，使 RP2350 reference map 与 STM32H7/i.MX RT 等
  port 共用 Partition ID/Store API，而不共用硬编码 offset。
- [ ] host 仿真验证同一 Deployment Capsule 在不同 geometry capability 上的 accept/reject。
- [ ] 若提出动态原生插件需求，另立 signed module/ABI/MPU/resource claim 架构评审；在该
  契约完成前，BlobStore 明确拒绝 executable object type。

## 十、每个任务的统一完成定义

任务只有同时满足以下条件才可 `[x]`：

1. 代码/文档 source of truth 明确，没有新增重复数字或 owner。
2. host 正向、边界、负向、掉电/torn（适用时）测试通过。
3. 主工程 release 与 RTOS+双核 validation 构建通过，warnings-as-errors。
4. 涉及硬件的任务有 COM8/两板/四板对应原始记录，不能用模拟代替。
5. HAOFV Vector、error reason、generation/freshness、completion evidence 可查询。
6. 文档四项门禁全绿；跨域契约变更完成 C11 交叉审核。
7. 代码与文档分离 commit，并推送当前私有分支。
8. 提供回退 artifact 或明确不可逆步骤及恢复路径。
