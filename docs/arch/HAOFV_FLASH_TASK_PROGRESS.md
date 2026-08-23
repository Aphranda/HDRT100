# HAOFV Flash 域任务进度

Status: Active
Domain: HAOFV / Flash / OTA / Storage
Canonical: `docs/arch/HAOFV_FLASH_TASK_PROGRESS.md`
Related: `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`, `docs/arch/HAOFV_FLASH_TODO.md`, `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`
Last updated: 2026-08-23

本文记录 Flash v2 迁移已经发生的实现、验证、提交和剩余 gate。架构语义以
`HAOFV_FLASH_ARCHITECTURE.md` 为准，未完成项和依赖关系以 `HAOFV_FLASH_TODO.md` 为准；
本文不冻结新契约，也不以单次构建结果替代 TODO 完成定义。

### 文档接口

本文是实施证据日志，不是架构事实源，也不是工作板。稳定语义回到架构文档，子项状态回到 TODO；
本文件只追加任务编号、代码提交、构建/HIL 原始报告、失败、跳过、回退和阻塞，并通过任务编号回链
到 TODO。不得在本文件自行把契约状态从 `pending` 改成 `active`。

### FLASH-TASK-20260823-052 - Remove OTA-owner watchdog reconfiguration

- 状态：代码与 host/build 回归通过；DHRT100 烧录、COM9 UART、`READY_TO_REBOOT → BOOT →
  COMM → COMMITTED` 和 power-cut HIL 仍待硬件窗口，不能作为 M1/M3/M4 退出证据。
- 修复：移除 `portable_ota_core_port.c` 和 `ota_fb.c` 中 OTA/metadata owner 对
  `drv_watchdog_enable(30s/60s)` 的临时维护窗口。硬件 watchdog 超时和喂狗现在只由
  `WatchdogSupervisorAO` 的健康门控制；END 的 manifest/BCB step 继续每次只推进一个有界
  FlashTransaction 子步骤，Supervisor stall 不再被 owner 路径重配置掩盖。
- 验证：V2 `cmake --build build-v2-debug-ninja3` 通过（含 FlashMap/schema/wire/inventory、
  App A/B、Boot/Recovery link gate）；`tools/run_portable_ota_tests.ps1` 的 stream、ingress、
  checkpoint、BCB 和 portable core 测试全部通过。构建后使用固化签名工具重新生成 key 7
  debug signature，candidate package 生成成功。
- 边界：当前没有可用的 DHRT100/COM9 枚举，未执行本轮烧录、UART 原始日志或闭环；不得把
  host/build 结果替代板端验证。恢复板卡后必须使用 `tools/picotool_flash/picotool_flash.py`
  和现有 OTA 工具，并保存 watchdog 状态、UART transcript 及最终状态链。

### FLASH-TASK-20260823-051 - Completion journal rotation policy

- 状态：portable rotation、v2 wiring、host/build 回归通过；真实 DHRT100 掉电/复位、长期
  endurance 和独立 C11 审核仍未完成，M1-05-G/H/I/L 及 M1 退出门禁保持未完成。
- 实现：`flash_transaction_journal_config_t` 增加可选 erase callback/geometry。journal 满槽时
  扫描最新有效 sequence，选择不包含最新记录的下一个 erase block；erase 成功后逐槽确认
  erased，再从该 block 的首槽追加。未提供 erase callback 的既有用户仍返回 FULL/fail closed。
  v2 `OTA_JOURNAL` completion region 绑定已有 FlashTransaction `OTA_JOURNAL` requester，
  不绕过唯一 Flash owner。
- Host：扩展 `test_flash_transaction_journal.c` 覆盖 rotation、最新记录保留和 sequence 连续性；
  `run_flash_transaction_tests.ps1`、`run_ota_journal_tests.ps1`、portable OTA runner 和定向
  Python policy/consumer/picotool 测试均通过。
- 构建：v1 `build` 与 v2 `build-v2-debug-ninja3` 通过 App A/B、Boot、Recovery、FlashMap/
  schema/wire/link gates；调试 key 7 的 v2 candidate transcript 因本轮固件变更重新签名，
  package 重新生成并由 public-only verifier 通过。
- 边界：rotation 只证明 host/backend 的确定性旧/新选择，尚未证明真实 power-cut、sector
  wear 分布或 DHRT100 跨 reset resume；板端不可见时不改变任何 HIL checkbox。

### FLASH-TASK-20260823-050 - M1-05-G live completion journal wiring

- 状态：v2 真实 producer wiring、host/build 回归通过；DHRT100 跨 reset/power-cut、journal
  rotation/endurance 和独立 C11 owner 审核仍未完成，M1-05-G、M1-05 和 M1 继续保持未完成。
- 实现：`components/ota_manager/src/ota_journal.c` 将 v2 `OTA_JOURNAL` partition 划分为
  completion journal 与 stream checkpoint 两个由 partition size/geometry 推导的 region，
  两者通过 region wrapper 共享同一 `ota_journal_platform_t`，禁止跨 region 读写。启动时
  初始化 `flash_transaction_journal_store_t`，构造 process-lifetime completion lease，并
  注册到 `FlashTransactionAO`；`portable_ota_port_durable_init()` 在 BCB metadata 读取前
  建立该 backend，因此空/损坏 BCB 也不会留下未配置的 completion owner。Product Config、
  OTA metadata 和 OTA image 的既有 completion lease 注入点因此获得真实持久化 backend。v1
  compatibility 继续不编译 v2 journal。
- Host：`test_ota_journal.c` 现在验证 lease 注册、completion append/recover 与 checkpoint
  互不覆盖；`run_ota_journal_tests.ps1` 固化链接 `flash_transaction_journal.c`；新增 v2
  durable-wiring 静态回归覆盖空 metadata 初始化顺序。上述测试与 FlashTransaction owner/
  journal reset matrix 均通过。
- 构建：`build` 和 `build-v2-debug-ninja3` 通过 App A/B、Boot、Recovery、FlashMap/schema/
  wire/link gates；v2 调试 key 7 的 signing transcript 因固件变更重新签名后，candidate
  package 重新生成并通过 public-only verifier。
- 边界：completion journal 当前是固定 region、满槽后 fail closed，尚未具备真实板端
  rotation/endurance；DHRT100 仍未重新枚举，不能把 host store recovery 当作板端掉电证据。

### FLASH-TASK-20260823-049 - M3-03 v2 Direct A/B 单主线收敛

- 状态：代码、host 静态回归和 v1/v2 build 通过；M3-03 要求的 DHRT100
  no-confirm/attempt-exhausted/revert HIL、回退证据和独立 C11 审核仍未完成，不能关闭
  M3-03 或 M3。
- 代码：`v2_candidate` CMake 配置现在强制
  `PROJECT_OTA_DEFAULT_BOOT_MODE=DIRECT_AB`；v2 portable OTA 不再从 metadata 选择
  `COPY_TO_ACTIVE`；v2 Bootloader 将 legacy boot mode metadata fail closed 并进入 Recovery，
  不编译固定地址 copy transaction/apply 路径。v1 compatibility 保留原兼容路径。
- SCPI：fault-injection 构建的 v2 不注册可写 `SYSTem:OTA:MODE`，只保留只读
  `SYSTem:OTA:MODE?` 与其他注入查询；v1 compatibility 继续保留 legacy mode 命令，便于
  回归与恢复验证。
- 验证：新增 `tests/python/test_v2_direct_ab_policy.py`（4 项通过）；`build` 和
  `build-v2-debug-ninja3` 均通过 App A/B、Boot、Recovery、FlashMap/schema/wire/link gates，
  v2 调试 key 7 的签名 package 重新生成并通过 public-only verifier。构建结果仅证明代码与
  工件一致，不替代板端 HIL。
- 板端阻塞：前一轮 DHRT100 烧录在 `build-v2-debug-ninja3/dhrt100_post_fix_flash_attempt.txt`
  的 load 中途出现 `picoboot::connection_error`，当前仍需重新插拔或按住 BOOTSEL 让 DHRT100
  重新枚举；恢复后使用固化 `tools/picotool_flash/picotool_flash.py` 重刷，并通过 COM9
  UART 保存 no-confirm、attempt-exhausted、revert 及
  `READY_TO_REBOOT → BOOT → COMM → COMMITTED` 原始 transcript。

### FLASH-TASK-20260823-048 - Supervisor feed ownership and BCB readback context fix

- 状态：代码与 host/build 回归通过；DHRT100 新固件烧录/OTA 闭环待本轮板端窗口完成，不能把本条
  作为 M1/M3/M4 退出证据。
- 修复：移除 `FlashTransactionAO` 同步执行器中的直接 `drv_watchdog_feed()`，改为只发布有界
  transaction progress telemetry；硬件 watchdog 继续只由独立 `WatchdogSupervisorAO` 的健康门喂狗，
  避免 END metadata 卡住时被 owner 路径掩盖。BCB transaction readback 显式使用事务保存的
  `platform.read_page(platform.context, ...)`；事务 API 现在接收可写 store owner，计数器更新不再
  通过 const store 强转，也拒绝任何将 transaction context 强转为 store 的实现。
- 同步清理：`drv_flash_erase_parked()/program_parked()` 不再在 sector/page 循环中直接
  `watchdog_update()`；FlashTransaction 的硬件 watchdog 喂狗责任保持在 Supervisor 健康门。
- 验证：`test_picotool_flash.py`、`test_flash_consumer_check.py` 共 9 项通过；portable BCB store
  async transaction、portable OTA core/session 可执行文件通过；`build-v2-debug-ninja3` 的
  DHRT100 App A/B、Boot、Recovery、FlashMap/schema/wire/link gates 通过。完整 package 重新用
  调试 key 7 签名并生成，记录见 `build-v2-debug-ninja3/DHRT100_V2_CANDIDATE_UPDATE.pkg`。
- 板端：当前 COM9 UART 监视窗口未收到日志，尚未宣称新固件在 DHRT100 上完成烧录/闭环；下一步
  使用固化 `picotool_flash.py` 和 `ota_send.py`，并保存 UART transcript 及
  `READY_TO_REBOOT → BOOT → COMM → COMMITTED` 查询证据。
- 本轮烧录尝试：`build-v2-debug-ninja3/dhrt100_post_fix_flash_attempt.txt`。DHRT100 在
  `load` 进度约 33% 时报告 `picoboot::connection_error`，重试时已不再枚举 BOOTSEL；这属于
  板端连接/供电恢复阻塞，不作为固件闭环失败归因。恢复前需重新插拔或按住 BOOTSEL 让板卡
  重新枚举，再用同一固化工具重刷并继续 UART/OTA 验证。

### FLASH-TASK-20260823-047 - END 可调度 FlashTransaction 子步骤

- 状态：代码、host 回归、DHRT100 烧录和 OTA 闭环均完成；本条不改变 M1/M3/M4 总体退出状态。
- 实现：`pota_end()` 现在只做接收长度/CRC/package cursor 检查并进入 `VERIFYING`；后续由
  `pota_service()` 每次推进一个有界步骤：向量校验、slot manifest append、BCB `mark_pending`，
  最后才发布 `READY_TO_REBOOT`。`POTA_OPERATION_SERVICE` 已允许 `VERIFYING/MARK_PENDING`，
  所有 Flash 写入仍经平台 owner/FlashTransaction callback。
- 调度贯通：stream 新增 `ENDING` 状态；CLOSE ACK 只表示 END 请求已接受，AO tick 继续 service，
  达到 `READY_TO_REBOOT` 后才释放 ingress lease。legacy OTA AO 同样在 VERIFYING/MARK_PENDING
  tick 中推进；`tools/ota_send/ota_send.py` 已等待明确 READY，而不是把 END 后一次回读/CDC
  断开当作成功。
- 验证：V2 debug 主工程 App A/B、Boot、Recovery、link contract 构建通过；portable OTA host
  runner（含 core/stream/ingress）通过；新 package 由 key 7 离线签名并通过 public-only verifier，
  package size/build 记录见 `build-v2-debug-ninja3/DHRT100_V2_CANDIDATE_UPDATE.pkg`。
- 验证：`build-v2-debug-ninja3` 主工程全量 Ninja、FlashMap/schema/wire/link gates 和
  `run_portable_ota_tests.ps1` 均通过。固化 `picotool_flash.py` 对 DHRT100（
  `GTS,DHRT100,839E1AE79EA20F31,0.1.0`）烧录/Flash verify/reboot 通过；随后
  `ota_send.py --boot-and-commit --expect-final-state COMMITTED` 返回 0，SCPI 读回
  `SYST:OTA:STAT? = "COMMITTED",1,"NONE",5`、`SYST:OTA:RES? =
  5,"NONE","APPLIED",2,474824,4110326733`、`SYST:OTA:TXN? = 0,0,0,0,0,0,0,0`。
  COM9 UART 记录到 software reboot、DHRT100 boot、application initialized 和 OTA AO
  initialized，未出现 `CORE0_SUPERVISOR_STALL`。

### FLASH-TASK-20260823-048 - Bootloader manifest lane geometry 修正

- 状态：完成；修正 Bootloader 只读 manifest validator 仍使用双 lane 总 footprint 的问题。
  `lane_size` 现在与 App writer 一致，使用单 lane `OTA_SLOT_MANIFEST_LANE_SIZE`；该错误曾在
  OTA 已完成接收并重启后触发 `STAGE_VALIDATE_FAILED`。
- 验证：重新构建并使用 `tools/picotool_flash/picotool_flash.py` 烧录 DHRT100，Flash verify
  全部通过；同一签名 package 再次 OTA 后达到 `COMMITTED/APPLIED`，证明
  `READY_TO_REBOOT → BOOT → COMM → COMMITTED` 闭环恢复。原始报告：
  `build-v2-debug-ninja3/picotool_flash_boot_manifest_fix.txt`、
  `build-v2-debug-ninja3/ota_boot_manifest_fix.txt`、
  `build-v2-debug-ninja3/after_boot_manifest_fix.json` 和 COM9 UART transcript。

### FLASH-TASK-20260823-046 - V2 debug OTA watchdog HIL

- 状态：进行中；factory 烧录闭环通过，signed package 数据接收达到完整长度，但 `END` 阶段
  仍触发硬件 watchdog 复位，未达到 `READY_TO_REBOOT`，因此不得宣称 OTA/BOOT/COMM 完成。
- 证据：V2 debug build `20260823033844` 由 `tools/picotool_flash/picotool_flash.py` 写入
  DHRT100，picotool Flash verify 全部通过；key 7 签名与 package 生成通过，package 大小为
  943232 B。`tools/ota_send/ota_send.py` 收包达到 `943232/943232` 后 CDC 重置；重枚举
  `SYSTem:WATCH:LOG?` 为 `WATCHDOG_TIMEOUT,CORE0_SUPERVISOR_STALL`，slot/BCB 未变化。
- 根因分析：FlashTransaction 合法停驻 core1 时，诊断 supervisor 需要识别受控 lockout；同时
  OTA END 的同步 metadata/manifest owner 路径仍可能让高优先级控制任务长时间占用。已加入
  lockout-aware heartbeat、AO 边界 feed、擦除/编程分段 feed 和 debug maintenance watchdog
  window，尚未完成板端回归闭环，不能作为生产方案。
- 原始报告：`build-v2-debug-ninja3/picotool_flash_final_60000.txt` 及本次终端 transcript；
  当前板卡保持可启动的 V2 debug factory，OTA 状态为 `IDLE`，未修改部署状态。
- 下一步：提高 END 路径为可调度的分步事务并在 DHRT100 上重新执行 signed OTA；确认无新
  watchdog、状态 `READY_TO_REBOOT → COMMITTED`、active slot 更新和 transaction/journal 清零后，
  再移除临时 debug watchdog window 并单独提交代码与文档。

### FLASH-TASK-20260823-045 - V2 slot manifest/hash verifier baseline

- 状态：完成 V2 slot-manifest durable owner/schema、Boot 切换前的 manifest/header/签名/计数器和
  A/B 镜像 SHA-256 重验的代码与 host/build 切片；M1、M3、M4 退出门禁继续保持未完成，V2
  `deployment_state=target_not_deployed` 未改变。
- 代码：`pota_slot_manifest` 提供双 lane body/commit、幂等 append、旧 lane 回退和损坏最新
  lane 选择；V2 Boot 只读加载活动 slot manifest，经 portable package parser、角色化公钥
  registry、BCB verified counter 和 Flash SHA-256 校验后才允许跳转；OTA adapter 在 pending
  前持久化同一 header。V1 编译路径不链接该验证状态。
- host/构建：slot manifest 单元测试覆盖空 store、首条 append、重复 header、双 lane sequence、
  torn commit、旧 lane rollback、损坏 newest lane 和非法 geometry；V1 主工程与 V2
  factory-candidate 的 App A/B、Boot、Recovery、link contract 均构建通过；portable OTA runner
  通过，全量 host runner 快照为 `31/31`，V1 `release_check=OK`。
- 协议向量：manifest extension 从 version 1 升为带 A/B SHA-256 的 version 2，旧 signing
  transcript hash/signature golden vector 已替换为新公开 key/signature；仓库只保留公开材料，
  不生成、不读取或提交私钥。
- V1 回归与 V2 闭环边界：DHRT100 当前仍运行旧 V1 build；已用板卡身份查询确认
  `GTS,DHRT100,839E1AE79EA20F31,0.1.0`，但 V1 烧录/OTA 仅可作为回归，不计入本任务 V2
  闭环。V2 candidate package 因生产 key registry 为空而按预期被 consumer gate 拒绝；未将
  `target_not_deployed` 改为 deployed，也未用 V1 工件冒充 V2 验证。当前尚未对 DHRT100 烧录
  V2 candidate；真实签名/部署批准、factory baseline 的可启动 manifest、full-erase/reflash、
  reset/power-cut、A/B/revert/Recovery HIL 仍是阻塞项。
- 失败/阻塞原始证据：直接运行 pytest 时系统临时目录权限导致 2 个 `tmp_path` setup error；
  改用仓库内独立 `--basetemp` 后 OTA packager/signature/FlashMap 定向集合 `28/28` 通过。
  V2 consumer 预期失败：`flash_consumer_check=FAILED detail=v2 candidate OTA package is not
  fully signed`。
- 提交与推送：本条目对应代码和测试修改尚未提交；文档门禁通过后按约定与文档分离提交并推送。

### FLASH-TASK-20260823-046 - DHRT100 debug V2 deployment and OTA failure evidence

- 调试 key：为本次本地 candidate 临时生成 P-256 factory key，公钥（key_id=7）为
  `043a3268ac707d46b33b933f950d5d79e7f6e80767880d0d5811d77af58571939d59d9a73add45f52f15230d32b2268739373f67f8e13f958b47c0eb5697222e00`；
  私钥仅留在工作区外的临时调试文件，不入库、不用于生产。当前 package transcript SHA-256
  为 `3dfb77affcde55b2cbc0d81385451fe554f584e3f7f77f1b9b09487cebfd1a0d`，对应 low-S 签名已由
  public-only verifier 复验。
- 烧录：使用 `DHRT100_V2_CANDIDATE_FACTORY.uf2` 对 DHRT100（board identity
  `GTS,DHRT100,839E1AE79EA20F31,0.1.0`）执行 BOOTSEL/reflash，picotool load/verify/reboot
  全部成功。V2 debug build `20260823020451` 启动，`DIRECT_AB`、BCB health 和 erased
  slot-manifest debug bootstrap 均可查询；COM9 仅是调试串口，未作为板卡身份。
- OTA HIL：对同一 signed package 执行 legacy `SYST:OTA:PBEGIN/DATA`，首次连续发送在
  header 后触发 `INVALID_STATE`/`QUEUE_FULL`；证实 OTA AO 擦除 service 被 DATA 事件饿死。
  host sender 已增加 package block pacing、等待 `RECEIVING` 和异步 BEGIN terminal snapshot
  过滤；重新烧录后可完成 inactive image 接收过程，但本次运行在 END/状态回读处未得到
  `READY_TO_REBOOT`，板上最终回到 `IDLE`，slot/BCB 未改变。stream ingress 也因上一失败会话
  留下 `SYST:OTA:STREAM:STAT? = 0,5,0,3137263498,5` 而未完成 CLOSE/BOOT。
- 结论：这是真实 V2 debug deployment 证据，不是 V1 替代，也不是 M4 闭环；发现的 legacy
  OTA AO fairness、event queue back-pressure、END 状态持久化问题进入下一待办。候选仍保持
  `target_not_deployed`，production key registry 仍为空，M1/M3/M4 退出门禁不变。回退路径为
  已验证的 V1 DHRT100 factory UF2；在修复并重新 build 后必须再次烧录 DHRT100 验证。

### FLASH-TASK-20260823-044 - Verified package object durable resume

- 状态：M4-01/M4-02 完成签名 package 的 inactive-object source 与 durable resume host/build
  切片；M4、M3 和 M1 的退出门禁继续保持未完成，v2 deployment state 保持
  `target_not_deployed`。
- receiver：checkpoint schema 保存 package cursor、package prefix CRC 和目标镜像前缀 CRC。
  package resume OPEN 后先要求 source 重传 manifest header，重新执行 product/hardware/map、目标
  slot/run offset、signature/key role 和 security counter 校验；通过后才按有界 service 扫描已写
  inactive image 前缀，CRC 不符时在续写前 fail closed，并从首个未确认 sector 擦除尾部后发布
  recovered cursor。
- source：CDC sender 从完整签名 package 只提取原始 manifest header 和当前 inactive slot image，
  OPEN 的 size/CRC/hash/checkpoint 全部绑定该紧凑对象；无关 slot image 和 package padding 不传输、
  不进入 OTA Stage。分块在 header 和 image 边界处分割，避免目标镜像首块跨越源 package 的非对齐
  object 边界。旧 PBEGIN 完整 package 兼容路径不变。
- 负向证据：resume 前续写、篡改签名、损坏目标镜像前缀、错误 map/token/object/cursor、非对齐
  package object 范围均拒绝；正向覆盖 manifest 重验、有界 readback、尾部擦除、cursor continuation、
  image/package CRC 和单次 pending publish。
- 验证：portable OTA、定向 Python 和全量 host runner 通过，后者为本次快照 `31/31`；当前构建
  package 的 sender 投影确认只形成 manifest + inactive image object。DHRT100 v1 release 与 v2
  factory-candidate 完整构建通过，App A/B、Boot、Recovery link contract 通过，v1
  `release_check=OK`。
- 提交与推送：`9ed91c7 feat(ota): resume verified package streams`、
  `be7ea0c feat(ota): stream only inactive package objects` 已推送
  `origin/feature/rtos-multicore-haofv`；Registry 状态未改变。
- 边界：本轮没有烧录或写入 DHRT100。真实 reset/power-cut、journal rotation 交叉点、长期
  endurance、USB CDC/USBTMC/UART/RS485/SD 五入口和 Recovery HIL 仍未完成；生产 key registry
  仍为空，v2 factory-candidate 继续禁止部署，不能据此关闭 M4-02 或 M4。

### FLASH-TASK-20260823-043 - OTA stream durable abort tombstone

- 状态：M4-02 完成 portable durable abort/restart policy 的 host/build 切片；M4-02、M4、M3 和
  M1 的退出门禁继续保持未完成，v2 deployment state 保持 `target_not_deployed`。
- 代码：checkpoint schema 增加 flags 并定义 durable abort tombstone。session 只有在底层 abort
  成功且 tombstone 经既有 checkpoint backend 提交后才报告 `ABORTED`；重建 store 后，同一
  session/generation 的 OPEN 被拒绝，source 必须提升 generation 并从 offset 0 重新开始。checkpoint
  replay 改为先选择同一 identity 的最新 sequence，再判断幂等、回退或 tombstone 冲突，避免旧记录
  遮蔽最新终止事实。
- 工具与诊断：`SYSTem:OTA:JOURnal?` 追加 flags 投影；CDC sender 的 `--resume` 只接受 flags 为零的
  checkpoint，abort tombstone 不会被当成可续传 cursor。未知 flags 在写入前 fail closed。
- 验证：portable OTA runner、定向 Python 和全量 host runner 通过，后者为本次快照 `31/31`；
  DHRT100 v1 release 与 v2 factory-candidate 完整构建通过，App A/B、Boot、Recovery link contract
  通过，v1 `release_check=OK`。
- 提交与推送：`93ff7fe feat(ota): persist stream abort tombstones` 已推送
  `origin/feature/rtos-multicore-haofv`；Registry 状态未改变。
- 边界：本轮没有烧录或写入 DHRT100。tombstone 尚无真实 reset/power-cut、journal sector rotation
  交叉点、跨 ingress retransmit 或 endurance HIL；package parser/image cursor resume 也未完成，
  不能据此关闭 M4-02 或 M4。v2 factory-candidate 仍禁止部署。

### FLASH-TASK-20260823-042 - OTA Journal sector rotation

- 状态：M4-02 完成 durable checkpoint journal 的 host/build rotation 切片；M4-02、M4、M3 和 M1
  的退出门禁继续保持未完成，v2 deployment state 保持 `target_not_deployed`。
- 代码：portable checkpoint config 增加可选 erase callback/geometry。store 全满时扫描最新有效
  sequence，只选择不含该最新记录的下一 erase block；erase 成功并逐槽验证为空后才写入新 body/
  commit。未配置 erase callback 的 portable 用户保持原有 `FULL` fail-closed 语义。DHRT100 v2
  adapter 将 sector erase 作为 `FLASH_TRANSACTION_REQUESTER_OTA_JOURNAL` intent 提交，不绕过 App
  唯一 Flash owner。
- 负向证据：配置只有一个 erase block 或缺少 erase callback 时拒绝初始化；erase 失败返回 IO，
  旧最新 checkpoint 仍可恢复；重试成功后 sequence 单调推进。torn body/marker、readback corruption、
  replay/conflict 和未启用 GC 的 full 行为继续通过既有测试。
- 验证：portable OTA、OTA journal adapter 和全量 host runner 通过，后者为本次快照 `31/31`；
  DHRT100 v1 release 与 v2 factory-candidate 完整构建通过，App A/B、Boot、Recovery link contract
  通过，v1 `release_check=OK`。
- 提交与推送：`5140dc0 feat(ota): rotate durable checkpoint journal` 已推送
  `origin/feature/rtos-multicore-haofv`；Registry 状态未改变。
- 边界：本轮没有烧录或写入 DHRT100。rotation 尚无真实 power-cut、长期 wear/endurance、跨 reset
  resume 或 retransmit HIL；package parser/image cursor 与 abort/restart policy 仍未完成，不能据此
  关闭 M4-02 或 M4。

### FLASH-TASK-20260823-041 - RP2350 manifest verifier 与 verified counter 闭环

- 状态：M3-04 完成 RP2350 软件验签、角色化公钥 registry、离线签名工件和 verified counter 到
  pending BCB 的 host/build 切片；M3-04、M3、M4 和 M1 的退出门禁继续保持未完成，v2 deployment
  state 保持 `target_not_deployed`。
- 代码：新增 Mbed TLS SHA-256 + ECDSA P-256 verifier，接受 uncompressed SEC1 公钥与 raw
  `r || s` 签名并强制 low-S；registry 按 dev/release/factory role 选择 key，未知、重复、撤销和
  role mismatch 均 fail closed。portable core 只在 package header 完成 identity/range/counter/
  signature 验证后保存 manifest counter，并在 mark-pending 时传入 BCB；普通 metadata 更新继承
  最新 counter，避免 confirm/copy/repair 写回占位值。启用 `require_signature` 后 raw begin、raw
  resume 和 stream raw OPEN 均在任何擦写前拒绝。
- 离线工件：v2 默认不生成空签名 package；只有显式 counter/key ID 且 key 已登记、未撤销、角色
  允许时才生成 canonical transcript 与 signing request，外部签名通过登记公钥复验后才生成
  package。仓库不生成、不读取或保存私钥；生产 registry 当前为空，因此 v2 update 默认拒绝。
- 验证：portable OTA runner 和全量 host runner 通过，后者为本次快照 `31/31`；DHRT100 v1 release
  与 v2 factory-candidate 完整构建通过，App A/B、Boot、Recovery link contract 通过，v1
  `release_check=OK`。v2 默认构建确认不产生 unsigned update package。固定 golden vector 仅含
  公钥/签名，不含私钥。
- 提交与推送：`355e3ee`、`7c2ac80`、`7c6386a` 和 `3750aeb` 已推送
  `origin/feature/rtos-multicore-haofv`；Registry 状态未改变。
- Boot 审计边界：Boot 当前只消费 slot vector 与 CRC；metadata 中预留的 slot SHA-256 尚无 staging
  writer，签名 manifest 也未作为 Boot 可重验对象持久化。因此本切片不能解释为 Boot image trust
  chain 完成。下一步先建立 slot manifest 的 durable owner/schema，再让 Boot 在切换 active slot 前
  重验 identity、run offset、image hash、signature、key role 和 BCB counter。
- HIL 边界：本轮没有烧录或写入 DHRT100，且禁止烧录
  `DHRT100_V2_CANDIDATE_FACTORY.uf2`；OTP/key binding、受控 Recovery、BOOTSEL rollback 和
  DHRT100 空片/A-B/revert HIL 齐全前不得改变该限制。

### FLASH-TASK-20260823-040 - Raw image durable resume core

- 状态：M4-02 完成 raw-image durable resume 的 host/build 切片；M4-02、M4、M3 和 M1 的退出门禁
  继续保持未完成，v2 deployment state 仍为 `target_not_deployed`。
- 代码：checkpoint schema 升级并保存 durable prefix CRC；恢复只接受 session/generation、固定 wire
  token、object、size、package CRC、活动 map、目标分区和 inactive slot 全匹配的 raw image。
  package-mode checkpoint 明确 fail closed。恢复校验不在 OPEN 中同步扫描镜像，而由 OTA AO 的
  bounded service cadence 每次推进不超过 `POTA_MAX_DATA_BLOCK_SIZE`；校验通过后按 Flash erase
  sector 清理 durable offset 后的未确认尾部，再发布 recovered cursor，避免重复 program 半提交页。
  非最终 checkpoint 只在活动 Flash geometry 的 erase-sector 边界产生。
- 工具与诊断：CDC sender 增加 `--resume` 及显式 session/generation 参数；先通过 `*IDN?` 识别
  DHRT100，再查询活动 map 与 `SYSTem:OTA:JOURnal?`，从 canonical FlashMap 推导 App partition ID，
  并在 OPEN 前校验 journal token/size/CRC。journal 诊断新增 durable prefix CRC 字段。
- host 证据：恢复矩阵覆盖 map/token mismatch、损坏 durable prefix、OPEN 零镜像扫描、分块 CRC
  校验、验证前 DATA 拒绝、durable 前缀保持、未确认尾部 sector 擦除、无重复 prefix program、
  continuation close 和 pending publish。全量 host runner 为 31/31，Flash/OTA 定向 Python 为
  52/52；这些计数是本次验证快照，非架构事实源。
- build/release 证据：`pico2-release` 和 `pico2-v2-factory-candidate` 完整构建通过；v1/v2 consumer、
  App/Boot/Recovery link contract、独立 Flash owner report 和 `release_check=OK`。v2 候选仍含空
  signature，只是布局/构建工件，不是可部署 release。
- 失败与 HIL 边界：一次非门禁全量 Python 探测因系统 pytest 临时目录权限产生 setup errors，且
  既有 TDMA reflection 报告缺失；改用仓库外独立 basetemp 后本任务定向集合全绿。只读串口扫描
  只发现 CH343 接口且 SCPI 查询超时，未获得 DHRT100 `*IDN?`，因此未烧录、未写 Flash、未执行
  reset/power-cut/retransmit HIL。签名、受控 Recovery、固定回退和 v2 部署 gate 齐全前仍禁止烧录
  `DHRT100_V2_CANDIDATE_FACTORY.uf2`。
- 提交与推送：`4a594ce feat(ota): resume raw streams from durable checkpoint` 已推送
  `origin/feature/rtos-multicore-haofv`；Registry 状态未改变。
- 剩余工作：package parser/image cursor resume、journal GC/sector rotation、abort/restart policy、
  真实掉电点矩阵和 DHRT100 跨 reset resume/revert HIL 均未完成，不能把本切片解释为 M4-02 或
  M4 已关闭。

### FLASH-TASK-20260823-039 - v2 OTA Journal durable store adapter

- 状态：M4-02 增加真实 v2 `OTA_JOURNAL` 持久 backend 与只读诊断切片；M4-02、M4、M3 和 M1
  退出门禁保持未完成，候选 deployment state 继续为 `target_not_deployed`。
- 代码：新增 `OtaJournal` adapter，将 portable checkpoint 的 body/commit marker 更新合并为完整
  program page，并经 `FLASH_TRANSACTION_REQUESTER_OTA_JOURNAL` 提交；owner 只接受生成 map 中的
  `OTA_JOURNAL` 分区、完整 program page 或 erase sector，跨分区、错长度、非对齐和 v1 高地址
  均 fail closed。该 requester 不继承 FlashTransaction completion journal lease，避免 journal
  自递归。
- 启动与诊断：v2 App 初始化 journal/session 失败时拒绝启动 OTA surface；新增只读
  `SYSTem:OTA:JOURnal?`，投影 valid/result/sequence、session/generation/token/object、durable/
  total offset 和 package/chunk CRC。持久化 schema 与 DHRT100 OTA 验证工具已切换到独立 journal
  查询，不改变既有 `SYSTem:OTA:TXN?` 响应。
- 验证：`run_ota_journal_tests.ps1`、FlashTransaction owner/journal tests、全量 host runner
  31/31 通过；Flash/OTA 定向 Python 50/50 通过。`pico2-release` 与
  `pico2-v2-factory-candidate` 完整生成 App A/App B/Boot 及候选 Recovery/factory/update 工件；
  v1/v2 consumer、App/Boot/Recovery link contract、独立 release report 和 `release_check=OK`
  均通过。上述计数均为本次验证快照，非架构事实源。
- 提交与推送：`7912378 feat(ota): add v2 durable checkpoint backend` 已推送
  `origin/feature/rtos-multicore-haofv`。
- 边界与回退：本切片只完成 durable store adapter。stream/session/package core 仍从 offset 0
  初始化，尚不能恢复 received/programmed cursor、运行中 package/image CRC 或 header/parser
  状态；journal 尚无 GC/sector rotation，写满后 fail closed。未识别、烧录或写入 DHRT100，未做
  reset/power-cut/retransmit HIL，不宣称跨 reset resume。代码可独立 revert `7912378`；Registry
  状态未变。
- 下一步：实现 identity-bound resume API，使 recovered checkpoint 只在 descriptor/map/
  generation/token/object/size/package CRC 全匹配时恢复 core 状态；补 running CRC state、parser
  state、重复 chunk 幂等和 mismatch/partial checkpoint 负向矩阵，再进入 DHRT100 烧录闭环。

### FLASH-TASK-20260823-038 - Read-only Recovery 与 v2 factory baseline

- 状态：M3-05 增加 build-only Recovery/factory baseline 切片；M3-05、M3、M4 和 M1 的退出门禁
  均保持未完成。候选 deployment state 继续为 `target_not_deployed`，普通 release 继续使用
  `v1_compat`。
- 代码：新增独立 `DHRT100_RECOVERY`，只提供 `*IDN?`、Recovery status/map/BCB health 和显式
  ROM BOOTSEL handoff；BCB store 增加 read-only init，Recovery 不链接 raw Flash writer、App、
  RTOS、SCPI、TDMA 或存储栈。v2 Boot 在无有效 BCB/可启动 App 时只尝试已验证 Recovery，v1
  保留原兼容 fallback。
- 工厂基线：候选 UF2 加入 Recovery、有效 lane0 BCB 和 canonical map manifest；baseline 生成器
  从 FlashMap、BCB 与 OTA metadata C 头读取 wire 常量并校验容量。region report 覆盖
  Bootloader、App A、Recovery、Boot Control、OTA Stage 全部已编程区域，consumer 重算 size/
  SHA-256，并要求 full erase 与 map identity 一致；其余 store 明确列为 erased baseline。
- 验证：定向 Flash/Recovery Python 测试 15/15、portable OTA、全量 host runner 30/30、
  `pico2-release`、`pico2-v2-factory-candidate`、v1/v2 consumer、v1 release check、Recovery/App/Boot
  link gate 和 v2 release report 全部通过；缺少 factory opt-in 的 v2 配置按预期失败。
- 提交与推送：`ab1aee3 feat(boot): add read-only v2 recovery candidate` 与
  `9ac6681 feat(flash): verify v2 factory region hashes` 已推送
  `origin/feature/rtos-multicore-haofv`。
- 边界与回退：本轮未识别、擦除、烧录或写入 DHRT100；候选 update package 的 signature 仍为空，
  Recovery 尚无 factory package verifier/SD restore，真实 `OTA_JOURNAL`、OTP/key binding、空片恢复、
  A/B/revert/Recovery、掉电和 v2 Scratch HIL 均未完成。两个提交可独立 revert，Registry 状态未变。

### FLASH-TASK-20260823-037 - Gated v2 factory-candidate build path

- 状态：M1-02/M3-05/M4-04 增加显式、不可误入普通发布的 v2 工厂候选构建通路；这些总项及
  M1/M3/M4 退出门禁保持未完成。普通 preset 继续固定 `PROJECT_FLASH_DEPLOYMENT_MAP=v1_compat`；
  只有同时选择 `v2_candidate` 和 `PROJECT_FACTORY_MIGRATION_BUILD=ON` 才能配置候选构建，缺少
  factory flag 的负向配置按预期失败。
- 代码：新增 `flash_deployment_map.h` 作为 C consumer 的构建期活动 map façade，链接脚本消费由
  CMake 配置的 `flash_map_active.ldinc`；BootFlashService、FlashTransaction、OTA、Product Config
  和 validation-only Scratch 不再直接绑定 v1 符号。候选工件使用独立名称，packager/consumer
  只有显式 opt-in 才接受 `target_not_deployed` manifest；标准 release 工件名和 v1 地址保持不变。
- 验证：`pico2-release` 与 `pico2-v2-factory-candidate` 均完成 App A/App B/Boot 链接，三类
  Flash link contract 通过；v1/v2 consumer check、`release_check`、定向 Python 29 项和全量 host
  runner 30/30 通过。候选生成 `DHRT100_V2_CANDIDATE_FACTORY.uf2` 与
  `DHRT100_V2_CANDIDATE_UPDATE.pkg`，其地址由 `flash_map_v2_manifest.json` 校验。
- 提交与推送：代码提交 `ed2de4b feat(flash): add gated v2 factory candidate build` 已推送
  `origin/feature/rtos-multicore-haofv`。
- 边界与回退：候选 factory 尚未包含 Recovery、map/BCB/空 store baseline，update package 的
  signature 仍为空，真实 `OTA_JOURNAL` backend、签名/OTP、空片恢复和 DHRT100 HIL 均未完成。
  本切片没有烧录、擦除或写板，不能视为 v2 deployment；可独立 revert `ed2de4b` 回到 v1-only
  consumer，Registry 状态未变。

### FLASH-TASK-20260823-036 - BCB persistent health projection

- 状态：M3-02 完成 BCB 盘上健康事实重建与 App 只读诊断切片；M3-02 总项保持 `[~]`。
  该快照从 lane seal 和有效 body/commit record 重新扫描，不依赖本次启动的 RAM 计数，因此
  store 重新初始化后仍能报告 valid lane/record、最新 lane generation、sequence、security
  counter 及其 lane/page。
- 代码：`pota_bcb_store_get_health_snapshot()` 和 façade wrapper 只使用既有 read callback，
  不执行 erase/program，不改变 BCB wire 格式；`ota_metadata_get_bcb_health()` 将 portable
  snapshot 投影到产品层；新增 `SYSTem:OTA:BCB:HEALth?` 返回七个无符号字段。host fixture
  覆盖三次 append、lane rotation、store 重建和最新 commit 损坏后的旧记录选择。
- 验证：portable OTA tests 和全量 host runner 30/30 通过；`pico2-release` 完整生成 DHRT100
  App A/App B/Boot/factory/update，FlashMap/inventory/wire/link gates 及 `release_check=OK`。
  本次构建快照 build id 为 `20260822205422`，App A/App B 大小分别为 438456/438464 bytes；
  这些数字只属于本次证据，不是架构事实。
- 提交与推送：代码提交 `68d1ef7 feat(boot): expose persistent bcb health` 已推送
  `origin/feature/rtos-multicore-haofv`。
- 边界与回退：lane generation 是已提交 lane 擦除的持久下界，不等同于 Flash 芯片完整寿命
  计数；尚无失败擦写持久统计、产品阈值、v2 migration/Recovery policy 或 DHRT100 板端查询
  证据。本切片未烧录、未写板、未改 deployed v1 map；可独立 revert `68d1ef7`。

### FLASH-TASK-20260823-027 - Product Config append-only record primitive

- 状态：M1-05-K 完成 Product Config 运行时 single-sector rewrite 的替换切片，M1-05-K
  总项保持 `[~]`。旧 v1 首页记录仍可读取；新写入按擦除页 append，记录含 sequence/CRC，
  同值更新不写；扫描只选择 CRC 有效且 sequence 最新的记录，遇到 partial/invalid record
  跳过。Product NVS 满槽时 fail closed，不执行隐式 GC 或 sector rotation。
- 代码：`components/product_config/src/product_config.c` 改为固定
  `DRV_FLASH_PAGE_SIZE` slot 的 append/read-latest/store-readback；
  `components/flash_transaction/src/flash_transaction_fb.c` 允许 Product Config 在
  Product NVS 内按 program-page 对齐的非零 relative offset 写入，同时继续把 erase 限制在
  首个 sector；`tests/unit/test_flash_transaction.c` 增加第二页 append 的 owner 负向边界。
- 验证：`tools/tests/run_host_unit_tests.ps1` 30/30；
  `cmake --build --preset pico2-release --parallel 4` 完整生成 DHRT100 App A/App B/Boot、
  factory UF2 和 update package；FlashMap/inventory/wire/link gates 及
  `python tools/release_check/release_check.py --root . --build-dir build` 均通过。
- 边界与回退：本切片未改变 deployed v1 map、未执行 DHRT100 烧录/掉电/回退 HIL；满槽时
  保留最新已提交记录并返回失败，后续 M2-02 负责 rotation/GC/recovery。M1-05-G/H/I/J/L、
  M3/M4 真实 journal/Recovery/ingress/HIL 仍未完成。

### FLASH-TASK-20260823-028 - Direct A/B decision façade

- 状态：M3-03 增加 portable、无 IO 的 Direct A/B 决策分类器；M3-03 总项保持 `[~]`，Boot
  真实状态机尚未切换到该 façade。
- 代码：新增 `third_party/portable_ota/include/pota_direct_ab.h` 与
  `src/pota_direct_ab.c`，`pota_direct_ab_decide()` 先验证 metadata/Direct A/B mode，
  再输出 `NO_PENDING`、`BOOT_PENDING` 或 `ROLLBACK`；attempt exhausted 只选择 confirmed/
  previous 中与失败 slot 不同的合法回退目标，不执行 Flash 或镜像校验。portable OTA 测试
  增加 pending、attempt exhausted 和 no-pending 三条断言；App/Boot 均链接同一 portable source。
- 验证：`run_portable_ota_tests.ps1 -BuildDir build-portable-ota-tests-m3-direct-ab` 通过；
  `cmake --build --preset pico2-release --parallel 4`、App A/App B/Boot link gate 和
  `release_check=OK` 通过。
- 边界与回退：未把分类器测试当作 Boot HIL；未执行 DHRT100 reset/no-confirm/attempt
  exhausted/revert、vector/hash/signature/Recovery 验证，M3-03/M3 退出门禁保持未完成。若
  Boot 接入失败可独立 revert portable façade，不影响现有 v1 状态机。

### FLASH-TASK-20260823-029 - DHRT100 hardware probe blocked

- 状态：硬件门禁未完成。系统只枚举到 USB-Enhanced-SERIAL CH343，序列号
  `5C93186767`；未获得可确认的 DHRT100 `*IDN?` 或固件 build 响应。
- 操作：只读启动 `tools/flash_map/flash_map_board_validate.py`，使用当前串口探测
  `*IDN?`、`SYST:FW:BUILD?` 和 FlashMap/传感器查询；在超时且无报告生成后中止。
  未执行 BOOTSEL、烧录、擦除、掉电、回退或任何写命令。
- 结论：该串口名称不作为板卡身份；DHRT100 M1/M3/M4 的物理 HIL、烧录和回退证据继续
  保持未完成，待固件重新枚举且 `*IDN?` 明确返回 DHRT100 后再重试。

### FLASH-TASK-20260823-030 - Product Config sector rotation

- 状态：M1-05-K/M2-02 增加了基本 sector rotation；M1-05-K 总项仍为 `[~]`。当所有
  program-page slot 已占用时，选择不包含最新有效记录的下一 sector，先通过
  FlashTransactionAO 擦除该 sector，再从其首个 page append 新记录；如果没有有效锚点、
  擦除失败或后续 program/readback 失败，保留旧记录并返回失败。
- 代码：`components/product_config/src/product_config.c` 增加 slot/sector 几何静态断言、
  最新记录所在 sector 选择和环形轮换；`flash_transaction_fb.c` 允许 Product NVS 内任意
  sector-aligned erase，但仍限制 requester、分区和整 sector 长度；
  `test_flash_transaction.c` 增加非首 sector erase owner 边界。
- 验证：`tools/tests/run_host_unit_tests.ps1` 30/30；`cmake --build --preset pico2-release
  --parallel 4` 通过并生成 DHRT100 App A/App B/Boot/factory/update 工件；FlashMap、inventory、
  link gate 和 `release_check=OK` 通过。
- 边界与回退：当前仍无真实 Product NVS power-cut 注入、sector seal、wear health 或 DHRT100
  rotation HIL；轮换策略只在旧最新记录所在 sector 之外执行，故不宣称 M2-02/M1-05-K 完成。

### FLASH-TASK-20260823-031 - BootControlStore façade owner boundary

- 状态：M3-02 的独立 BootControlStore façade 子项完成，M3-02 总项保持 `[~]`。Boot/App 的
  `ota_metadata.c` 不再直接调用 `pota_bcb_store_*`，而通过 `pota_boot_control_facade` 进行
  init/select/append/wear 查询；façade 不暴露 Flash offset/lane 几何，只转发已验证的
  platform callback boundary。
- 代码：新增 `third_party/portable_ota/include/pota_boot_control_facade.h` 和
  `src/pota_boot_control_facade.c`，App/Boot CMake 均链接；`test_pota_boot_control_store.c`
  增加 façade 的未初始化拒绝、append/select 和 wear snapshot 测试。
- 验证：`run_portable_ota_tests.ps1 -BuildDir build-portable-ota-tests-m3-bcb-facade` 通过；
  `cmake --build --preset pico2-release --parallel 4` 生成 App A/App B/Boot/factory/update，
  FlashMap/inventory/link gate 和 `release_check=OK` 通过。
- 边界与回退：façade 不是 v2 deployment、持久 wear health、Recovery 或 DHRT100 BCB HIL；
  portable BCB 的 torn-write/GC host 证据保持有效，未执行板端写入。删除 façade 文件即可
  回退到同一 primitive API，不改变 Flash map。

### FLASH-TASK-20260823-032 - Direct A/B Boot 状态机接入 façade

- 状态：M3-03 完成 portable decision façade 到 Boot 实际 pending 状态机的接入切片；M3-03
  总项保持 `[~]`，DHRT100 reset/no-confirm/attempt-exhausted/revert HIL 仍未完成。
- 代码：`bootloader_apply_direct_ab_pending()` 通过
  `portable_ota_port_metadata_direct_ab_decide()` 获取纯策略分类结果；`NO_PENDING` 保持
  原状态，`BOOT_PENDING` 继续执行真实 slot/vector/hash 校验后再 apply，`ROLLBACK` 沿用
  既有 `MAX_ATTEMPTS` fail-closed 记录路径。portable port 只负责 metadata layout 到
  `pota_metadata_t` 的 const view，不拥有 Flash 或镜像 IO。
- 验证：`run_portable_ota_tests.ps1 -BuildDir build-portable-ota-tests-m3-direct-ab-boot`
  通过；`run_host_unit_tests.ps1` 通过；`cmake --build --preset pico2-release --parallel 4`
  和 `python tools/release_check/release_check.py --root . --build-dir build` 均通过；App A、
  App B、Boot link gate 通过。
- 提交与推送：代码提交 `33d441f feat(boot): route direct ab decision through portable facade`
  已推送 `origin/feature/rtos-multicore-haofv`；本文档只记录证据，不改变 registry status。
- 边界与回退：未执行 DHRT100 烧录、掉电、回退或任何写入；删除 wrapper 和调用点即可回退
  到原 Boot 状态机，Flash map 不变。M3-03 的 slot-specific image/vector/hash/signature
  校验和 M3 退出门禁仍未满足。

### FLASH-TASK-20260823-033 - Local stream ingress 接入 App owner

- 状态：M4-03 完成 USB CDC/USBTMC SCPI 控制面到 `pota_stream_ingress`、
  `pota_stream_session` 和实际 FlashTransaction owner 的接入切片；M4-03 总项保持 `[~]`，
  SD/UART/RS485 producer、v2 durable journal 和跨 reset resume 尚未完成。
- 代码：portable port 持有 target-slot platform callback 和 ingress/session 生命周期，OTA AO 在
  既有有界 service cadence 中只推进 OPEN/RECEIVING stream；legacy OTA 与 stream 在
  OPEN/RECEIVING/READY_TO_REBOOT 期间互斥。新增固定 little-endian OPEN decoder，字段长度和
  offset 由 `POTA_STREAM_OPEN_*` 符号定义，不暴露 C ABI padding 或 `bool` 表示。
- 接口：新增 `SYSTem:OTA:STReam:OPEN/DATA/CLOSe/ABORt/BOOT/STATus?`；USB source 必须与
  当前控制面模式匹配，DATA 每帧校验 CRC，状态只报告 session source/state/durable offset/
  token/result。Boot target 使用 stub，不获得 App writer 能力。
- 验证：portable OTA host tests、全量 host 30/30、wire contract 4/4、相关 Python 6/6、
  App A/App B/Boot release build、link gate 和 `release_check=OK` 均通过。代码提交
  `5b93726`、`9d72b81` 已推送。
- 边界与回退：当前 durable offset 仅表示底层 program/readback 已成功；尚未绑定真实 v2
  `OTA_JOURNAL`，所以不能宣称跨 reset durable resume。删除 stream SCPI/port 调用点即可回退，
  legacy OTA 路径和 deployed v1 map 不变。

### FLASH-TASK-20260823-034 - Local stream 独立发送工具

- 状态：新增 USB CDC stream sender，作为 M4-03 host/tool 证据；未替代五类 transport HIL。
- 工具：`tools/ota_stream_send/ota_stream_send.py` 先用 `*IDN?` 确认 DHRT100，再查询 inactive
  target，选择 slot-specific image，构造固定 little-endian OPEN 描述符，按 chunk CRC 发送并
  查询 durable offset；可执行 stream BOOT、等待 USB 重枚举后 COMMIT。串口名称只作为连接
  路径，不作为板卡身份。
- 验证：`tests/python/test_ota_stream_send.py` 覆盖 OPEN wire layout、hash/CRC、SCPI block 和
  status parser；release build 和 link gate 通过。代码提交 `9d72b81` 已推送。
- 边界与回退：工具当前仅支持 USB CDC；USBTMC、SD、UART、RS485 必须使用各自真实 transport
  adapter，不能通过伪造 source 枚举替代。

### FLASH-TASK-20260823-035 - DHRT100 stream HIL probe blocked

- 状态：物理 gate 未完成。系统仅枚举 USB-Enhanced-SERIAL CH343，设备序列号仍为
  `5C93186767`，本轮连接路径为新的串口枚举名；未获得 DHRT100 `*IDN?`、build、slot、target
  或 sensor 响应。
- 操作：执行只读 serial query 和 `flash_map_board_validate.py` 探测；后者无响应且未形成有效报告
  后中止。未执行 BOOTSEL、烧录、erase/program、掉电、回退或任何 stream DATA。
- 结论：M1/M3/M4 的 DHRT100 烧录、A/B、回退和跨 reset gate 保持未完成；待连接能由
  `*IDN?` 明确确认 DHRT100 后，先用 legacy OTA 部署 stream 固件，再用独立 sender 完成下一轮
  stream A/B/Boot/Commit 闭环。

### FLASH-TASK-20260823-025 - signed manifest extension 与 counter gate

- 状态：M3-04 增加固定 manifest extension 的 portable parser/packager boundary；这是信任链
  的准入切片，不宣称已选定 RP2350 密码算法或完成 OTP/key lifecycle。
- 代码提交：`a65e988 feat(boot): add signed manifest security gate`，已推送。512-byte package
  header 的保留区可携带 extension version、required flags、security counter、key ID 和外部
  signature bytes；parser 在 required signature、minimum counter、签名长度、verifier 缺失或
  verifier 拒绝时 fail closed，并映射 `SIGNATURE_INVALID`/`SECURITY_COUNTER_ROLLBACK`。platform
  info 已将 counter/required-signature/verifier 约束传入 package parser；offline packager 只
  接受外部 `--signature-hex`，不在仓库内伪造签名。
- 测试：`run_portable_ota_tests.ps1 -BuildDir build-portable-ota-tests-m3-manifest-failclosed`
  通过；`tests/python/test_ota_packager.py` 10/10 通过；主工程 App A/App B/Boot、FlashMap/
  inventory/link gate 和 `release_check=OK` 通过，最新 package 重新生成。
- 边界：当前 DHRT100 live platform 未提供 RP2350 verifier/key source，release 默认仍为
  unsigned compatibility package；未执行签名烧录、OTP、掉电安全 counter、Recovery 或
  DHRT100 HIL，M3-04/M3 退出门禁保持未完成。

### FLASH-TASK-20260823-026 - stream session checkpoint store wiring

- 状态：M4-02 的 portable session 已从仅有 checkpoint primitive 推进为可配置 store 调用；仍不
  宣称 v2 `OTA_JOURNAL` 已部署或真实 OTA producer 已迁移。
- 代码：`pota_stream_session_set_checkpoint_store()` 将 session 绑定到
  `pota_stream_checkpoint_store`；每次成功的底层 program/readback 后，按 interval/final policy
  生成包含 session/generation/token/object/offset/total/package/chunk CRC 的 checkpoint，再推进
  session 的 durable cursor。append 失败将 session 置为 FAILED 并返回 `CHECKPOINT`，不发布成功
  状态。代码已纳入 portable tests 和主工程。
- 验证：`tools/tests/run_portable_ota_tests.ps1 -BuildDir build-portable-ota-tests-m4-checkpoint-store`
  通过；新增 ingress 测试在固定 fake flash checkpoint store 上执行 append + recover，并校验
  token/object/offset；`build-flash-m1-05h-20260823-release` App A/App B/Boot、FlashMap/
  inventory/link gate 构建通过。
- 边界：fake store 只证明 session/store wiring；当前 live map 仍为 v1 compatibility，尚无真实
  v2 journal address、Flash backend 的 page/sector adapter、掉电注入、跨 reset resume 或 DHRT100
  HIL，因此 M1-05-G/H、M4-02 和 M4 退出门禁保持未完成。

### FLASH-TASK-20260823-024 - M3 BCB wear telemetry 与 M4 ingress adapter

- 状态：M3-02 增加 BCB 物理页编程/擦除计数快照 primitive；M4-03 增加五类本地入口共用的
  transport-neutral ingress adapter。两项均为可独立复核的代码切片，尚不等于产品级持久 wear
  store 或真实端口迁移完成。
- 代码提交：`27601ae feat(ota): unify local stream ingress and BCB wear telemetry`、
  `c251f7d feat(ota): expose ingress status projection`、`df61fe3 feat(ota): route ingress service
  through shared adapter`，均已推送。
- 代码：`pota_bcb_platform_t` 增加可选的 `on_program_page`/`on_erase_lane` 观测钩子，
  `pota_bcb_store_get_wear_snapshot()` 暴露本次运行的物理操作计数；钩子只观测，不得执行
  Flash IO。新增 `pota_stream_ingress`，统一 USB CDC、USBTMC、SD、UART、RS485 的 source
  admission、单一 active source、最大帧长和可选 CRC 校验，再调用同一个
  `pota_stream_session`，不复制 A/B package 或改变 durable offset 语义；新增
  `pota_stream_ingress_get_status()`，投影 source/state/durable offset/token/last result。
- 测试：`tools/tests/run_portable_ota_tests.ps1 -BuildDir build-portable-ota-tests-m4-ingress-wear`
  通过；新增 ingress 的 CRC/源切换/会话状态测试，并断言 BCB 首次 append、replay 的页编程和
  lane 擦除计数。主工程 `build-flash-m1-05h-20260823-release` 重新配置构建通过，App A/App B/
  Boot 的 `flash_link_contract=OK`。
- 边界：当前 adapter 尚未接入 USB CDC/USBTMC/SD/UART/RS485 的生产 producer；BCB wear 计数
  尚未写入持久 health namespace；v2 `OTA_JOURNAL`、跨 reset/power-cut、Recovery、签名/OTP
  anti-rollback 和 DHRT100 物理烧录闭环仍未完成，不更新 M1/M3/M4 退出门禁。

### FLASH-TASK-20260823-023 - M3-02 BCB security counter monotonic gate

- 状态：portable BCB primitive 新增 security counter 防回退约束；完整 M3-04 签名、OTP、
  key lifecycle、掉电安全计数和 DHRT100 Boot fault HIL 仍未完成。
- 代码提交：`5a98377 feat(boot): reject BCB security counter rollback`，已推送。
  当新 BCB update 的 `security_counter` 低于当前 newest valid record 时返回
  `POTA_BCB_RESULT_POLICY`，不擦除 lane、不写入新 record；相同或更高 counter 仍受 sequence/
  CRC/commit/seal 校验约束。新增 portable BCB host 负向测试。
- 验证：`run_portable_ota_tests.ps1 -BuildDir build-portable-ota-tests-m3-counter` 通过；
  主工程 App A/App B/Boot、FlashMap/inventory/link gate 和 `release_check=OK` 通过。
- 边界：当前 `ota_metadata.c` 仍写入占位 `security_counter=0`，所以本条不宣称 anti-rollback
  产品闭环；未执行 DHRT100 烧录、OTP/key、Recovery 或真实掉电验证。

### FLASH-TASK-20260823-022 - M4-02 checkpoint frequency policy primitive

- 状态：已提供可独立测试的 monotonic checkpoint frequency decision；实际 wear/retransmit
  profile 选择和 stream ingress 调用仍未完成。
- 代码提交：`cb73d76 feat(ota): add checkpoint frequency policy`，已推送。新增
  `pota_stream_checkpoint_policy_t`、policy validity 和 `should_append()`；只有 durable offset
  单调前进且达到 interval，或按策略到达 final offset 时才建议 append，拒绝 offset 回退、越界、
  zero interval 和 zero offset。该 primitive 不执行 Flash 写入，避免把每 chunk 当成持久化策略。
- 验证：`run_portable_ota_tests.ps1 -BuildDir build-portable-ota-tests-m4-02-policy` 通过；
  主工程 App A/App B/Boot、FlashMap/inventory/link gate 和 `release_check=OK` 通过，产物已更新
  但尚未成功烧录 DHRT100。
- 边界：仍未关闭 M1-05-G 的 live `OTA_JOURNAL`、真实掉电/Recovery、五类 ingress 或
  DHRT100 HIL；policy profile 需结合 endurance/retransmit 实测后冻结。

### FLASH-TASK-20260823-021 - M4-02 stable stream identity token

- 状态：stream token 已从 moving durable offset 解耦，适合作为 checkpoint identity；resume
  orchestration 和实际 journal backend 仍未接入。
- 代码提交：`fd585fc fix(ota): keep stream token stable across checkpoints`，已推送。
  `pota_stream_session_token()` 现在只对 OPEN descriptor（session/generation/capability/identity/
  package/map/destination/object）计算 CRC，不随 chunk offset 变化；host 测试断言首尾 token
  一致。offset 顺序、重复和冲突检查仍由 session cursor 单独执行。
- 验证：`run_portable_ota_tests.ps1 -BuildDir build-portable-ota-tests-m4-02-token` 通过；
  主工程 App A/App B/Boot、FlashMap/inventory/link gate 和 `release_check=OK` 通过，最新包
  仍未成功烧录 DHRT100。
- 边界：稳定 token 只解决 identity 语义，不等于跨 reset resume；v2 `OTA_JOURNAL`、五类
  ingress、Recovery/掉电 HIL 仍保持未完成。

### FLASH-TASK-20260823-020 - M4-02 OTA chunk readback boundary

- 状态：stream write 的 durable boundary 已收敛到 program 后 readback；真实 ingress、v2 journal
  和 DHRT100 跨 reset 证据仍待完成。
- 代码提交：`3b36dd5 feat(ota): verify streamed chunks by readback`，已推送。portable OTA core
  通过 `portable_core_flash_read()` 使用 `drv_flash_read` 回读每个 program buffer；缺少 read API
  或内容不一致时返回 `POTA_ERR_READBACK`，状态进入 FAILED，`programmed_size` 不推进。该 read
  caller 已加入 `config/flash_raw_call_allowlist.json`，owner 为 `OtaStreamReadView`，只读且仅
  面向 v1 inactive slot。
- 验证：`run_portable_ota_tests.ps1 -BuildDir build-portable-ota-tests-m4-02-readback` 通过；
  `run_host_unit_tests.ps1 -BuildDir build-host-m4-readback-2` 通过 30/30，新增 readback corruption
  负向用例。`build-flash-m1-05h-20260823-release` 主工程、Flash inventory/link gate 和
  `release_check=OK` 通过。
- 边界：当前仍是 v1 compatibility map；没有把 readback 单测解释为真实掉电、Recovery、
  BOOTSEL 或 DHRT100 烧录闭环。
- DHRT100 硬件尝试：`build/dhrt100_readback_ota_20260823/transcript.txt` 记录已发送
  `SYST:OTA:PBEGIN` 并进入 `RECEIVING`，随后首个 `SYST:OTA:PROG?` 写入时 USB 设备断开；
  板卡未重新枚举且 `picotool info -a` 未发现 BOOTSEL 设备。本次不计为成功烧录，需板卡重新
  枚举后再执行恢复/闭环。

### FLASH-TASK-20260823-019 - M4-02 durable stream checkpoint primitive

- 状态：M4-02 portable checkpoint 子项完成 host 可复核切片；真实 v2 `OTA_JOURNAL` producer、
  ingress 接入、checkpoint frequency policy 和 DHRT100 跨 reset HIL 仍未完成，故 M4-02 保持 `[~]`。
- 代码：新增 `third_party/portable_ota/include/pota_stream_checkpoint.h`、
  `third_party/portable_ota/src/pota_stream_checkpoint.c` 和
  `tests/unit/test_pota_stream_checkpoint.c`；固定 64-byte 槽记录，包含 magic/schema/sequence、
  session identity、durable offset、package/chunk CRC、record CRC 和 commit marker。append 只在
  program 后 readback 校验通过时推进 sequence；相同 offset/CRC 重放幂等，元数据冲突、旧 offset
  回放和越界均 fail closed。
- 测试：`tools/tests/run_portable_ota_tests.ps1 -BuildDir build-portable-ota-tests-m4-02` 通过，
  覆盖顺序推进、复位恢复、满槽、撕裂 body、撕裂 commit marker、旧 offset 和 CRC 损坏回读。
- 构建：`build-flash-m1-05h-20260823-release` App A/App B/Boot、FlashMap/inventory/link gate
  和 `tools/release_check/release_check.py` 均通过；产物为本地 release 快照，尚未烧录 DHRT100。
- 边界：当前固件仍部署 v1 compatibility map；该 portable primitive 不访问 v2 高地址
  `OTA_JOURNAL`，不得据此宣称 durable resume、Recovery 或 M4 退出门禁完成。

### FLASH-TASK-20260823-016 - M3-02 BCB adapter 接入 v1 metadata

- 状态：M3-02 进入真实 adapter 接入阶段；portable primitive 与 `ota_metadata.c` 已连接，
  但 DHRT100 BCB/Recovery、掉电和 C11 证据仍未完成。
- 代码提交：`1e4e27f feat(boot): connect metadata to dual-lane BCB`，已推送。
  `ota_metadata_load()` 先通过 `pota_bcb_store_select_newest()` 读取双 lane body/commit/seal，
  校验 payload 中的 metadata CRC 后返回最新事实；`ota_metadata_store()` 将完整 v1 metadata
  作为 BCB payload，通过 `pota_bcb_store_append()` 执行 verified page、commit marker、lane
  generation/GC。已有 v1 single-sector copy 只保留为 legacy read/migration fallback；首次从
  legacy/default 写入会建立 BCB lane。`ota_metadata_corrupt_copy()` 现在按 lane 破坏，供后续
  BCB fault matrix 使用。
- 边界：BCB lane 由生成的 `OTA_METADATA_OFFSET/SIZE` 和 `FLASH_COMPAT_MAP_*_VERSION` 派生，
  未访问 v2 target 高地址；Boot callback 继续落到 BootFlashService，App callback 继续落到
  FlashTransactionAO。`security_counter` 当前保持占位值，不能解释为 anti-rollback。
- 验证：`build-flash-m1-05h-20260823-release` App A/App B/Boot 完整构建和三 profile link gate
  通过；`run_portable_ota_tests.ps1 -BuildDir build-portable-ota-tests-m3-02` 通过；
  `release_check.py` 输出 `release_check=OK`。没有把这些 host/build 结果扩展为真实掉电、
  Recovery 或 BOOTSEL 证据。

### FLASH-TASK-20260823-017 - DHRT100 BCB-backed A/B OTA 闭环

- 状态：DHRT100 已用包含 BCB adapter 的新 release 包完成一次真实 inactive-slot staged OTA、
  reboot 和 explicit commit；本条不关闭 M3-02 的 fault/Recovery/power-cut gate。
- 工件：`build-flash-m1-05h-20260823-release/DHRT100_UPDATE.pkg`，构建标识
  `20260822175837`，package CRC32 `0x0965EE9D`；板端身份为 `GTS,DHRT100,839E1AE79EA20F31,0.1.0`。
- 闭环证据：`build/dhrt100_bcb_ota_20260823_verify/summary.json` 与
  `build/dhrt100_bcb_ota_20260823_recover/summary.json`；首次包发送记录
  `READY_TO_REBOOT`，随后重启后 `IDLE`，`COMMit` 后 `COMMITTED`，最终
  `SYST:ERRor?=0,\"No error\"`。工具第一次因 `--boot-and-commit` 仍期待中间态
  `READY_TO_REBOOT` 而返回非零，但 transcript 已记录最终 `COMMITTED`；这不是板端失败。
- 额外观察：重复验证流程在已完成确认后得到 `INVALID_STATE`，随后使用独立 boot/commit 工具
  恢复为无错误状态；该负向结果说明 confirm 入口不接受无 pending 状态。
- 主机回归：`tools/tests/run_host_unit_tests.ps1 -BuildDir build-host-after-bcb` 完成 30/30，
  其中 portable BCB、FlashTransaction journal、FlashMap、lockout 和 TDMA/RefMem 相关套件均通过。
- 边界：本次仍是 v1 compatibility map 的 A/B 部署，未写 v2 高地址；未执行双 lane 损坏、
  lane seal/commit/body 掉电注入、空 BCB Recovery、security counter/anti-rollback 或 BOOTSEL。

### FLASH-TASK-20260823-018 - M4-01 identity-bound stream session core

- 状态：M4-01 的 transport-neutral session 骨架和 host 负向语义已实现；没有把进程内 offset
  解释为 durable resume，也未改变现有 USB/SD/UART/RS485 ingress 的调用路径。
- 代码提交：`64110cd feat(ota): add identity-bound stream session core`，已推送。
  新增 `pota_stream_session.h/.c`：OPEN descriptor 绑定 session/generation/capability、identity、
  map version、App partition、destination slot、object、total size、package CRC/hash；要求
  inactive-write 与 durable-ACK capability。WRITE 只接受当前 durable-cursor 的顺序 chunk，
  同 offset 同 CRC 的重复 chunk 幂等，冲突/跳跃 offset fail closed；CLOSE 只在完整接收后进入
  `READY_TO_REBOOT`，底层仍经 `pota_session` 的现有 Flash sink。
- Host 验证：`run_portable_ota_tests.ps1 -BuildDir build-portable-ota-tests-m4-01-final` 通过，
  新增测试覆盖 capability/identity/destination gate、service 前拒写、乱序、重复、冲突、token、
  close/abort 状态；portable BCB 与原有 OTA 测试均通过。
- 构建验证：`build-flash-m1-05h-20260823-release` App A/App B/Boot、FlashMap/inventory/link
  gate 和 `release_check=OK` 通过；M4-02 journal/resume、五类 ingress 接入、manifest parser/
  signature 和 DHRT100 session HIL 仍未完成。

### FLASH-TASK-20260823-015 - BootFlashService owner boundary

- 状态：M3-01 的 Boot 写入 owner 收敛子项完成；M3-01 总项、M1-05-J 和 M3-02 仍未完成。
- 代码提交：`0fe881c feat(boot): route writes through BootFlashService`，已推送。
  新增 `boot_flash_service.h/.c`；Bootloader 的镜像复制和 OTA metadata adapter 均通过
  `boot_flash_service_erase/program`，Bootloader 不再直接调用 raw erase/program。服务只接受
  生成 v1 compatibility map 中的 App A、App B 和 Boot Control 范围，并执行 sector/page 对齐、
  长度和分区边界检查；v2 target map 未部署地址仍被拒绝。
- Host/build 验证：`tests/python/test_flash_link_check.py` 8/8；
  `build-flash-m1-05h-20260823-release` 通过 FlashMap/inventory/wire/schema/migration、App A/B
  与 Boot link contract；`tools/release_check/release_check.py` 输出 `release_check=OK`。
- 证据边界：raw inventory 现在登记 `bootloader/src/boot_flash_service.c` 为唯一 Boot 写 owner，
  link gate 只允许 `boot_flash_service_erase/program` 作为 raw caller。该切片没有接入 portable
  BCB primitive、没有实现 wear counter/Recovery，也没有进行 BOOTSEL、掉电或 v2 高地址 HIL；
  因此不得把 M3-01/M3-02、M0-05 或 C11 审核标记为完成。

### FLASH-TASK-20260823-014 - M3-02 portable BCB dual-lane primitive

- 状态：M3-02 进入实现中；portable primitive 已具备，尚未接替现有 `ota_metadata.c` 的实际
  BCB 持久化，也未执行 DHRT100 BCB/Recovery HIL。
- 代码提交：`572dd65 feat(boot): add portable dual-lane BCB primitive`，已推送。
  `pota_boot_control_store` 使用 body page -> verified readback -> commit page，并为每个 lane
  保留 seal page；写满 lane 后擦除另一 lane、建立新 generation，再由 selector 只接受 schema/map、
  payload CRC、body CRC、commit marker 和 lane seal 均有效的记录。
- Host 验证：`tools/tests/run_portable_ota_tests.ps1 -BuildDir build-portable-ota-tests-m3-02`
  通过；新增测试覆盖首次 append/select、sequence replay、body/program/readback/commit/seal
  故障、lane GC 和旧 lane erase 失败。
- 构建验证：`build-flash-m1-05h-20260823-release` 重新配置并构建 App A/App B/Boot，
  `flash_link_contract=OK`；primitive 已纳入 App/Boot 编译目标但当前没有业务调用者。
- 边界：当前 v1 compatibility BCB 仍由 `ota_metadata.c` 双 copy single-sector rewrite 提供；
  在 payload 适配、BootFlashService 调用点、wear counter、空 BCB Recovery 和真实掉电证据完成前，
  不标记 M3-02 或 `ARCH-BOOTCTRL-01` 完成。

### FLASH-TASK-20260823-013 - M3-01 Bootloader partition size gate

- 状态：M3-01 的 size/map 子项完成；BootFlashService 依赖白名单和完整 M3-01 退出门禁仍未完成。
- 代码提交：`06c83af feat(boot): gate Bootloader size by FlashMap symbols`，已推送。
  `flash_link_check.py --profile boot` 现在同时校验 `FLASH` 区域与生成的
  `FLASH_COMPAT_MAP_BOOTLOADER_ORIGIN/LENGTH`，并拒绝缺失或越界的 `__flash_binary_end`。
- Host 验证：`tests/python/test_flash_link_check.py` 8/8 通过，新增 partition-size 正向和溢出
  负向 fixture；现有 Boot raw-caller/dependency 检查保持生效。
- 构建验证：`build-flash-m1-05h-20260823-release` 重新构建通过，输出
  `flash_link_contract=OK profile=boot`；`release_check=OK`。
- 边界：该门禁只证明链接产物不越过生成分区，不能替代空白 Flash recovery、BCB torn-write、
  signature/anti-rollback 或 DHRT100 Boot fault HIL。

### FLASH-TASK-20260823-012 - M1-06 Scratch validation-only 闭环

- 状态：M1-06 已完成受限 validation-only 流程；高地址 v2 deployment 和 JEDEC 读数仍明确阻塞，
  因而 TODO 保持 `[~]`，不宣称 M1 或 v2 迁移完成。
- 代码提交：`1914e42 feat(flash): add validation-only Scratch closed-loop path`，已推送。
  `PROJECT_ENABLE_FLASH_VALIDATION` 默认关闭；开启时命令只允许 Scratch 首个 sector/page，
  每次 AO 事务由 `job_id=0` 分配新 identity，恢复擦除和 `0xFF` 检查纳入结果字段。
- 主机验证：`tools/tests/run_flash_transaction_tests.ps1 -BuildDir
  build-flash-transaction-tests-m1-06` 通过 OFF/ON 两套 FlashTransactionFB 测试；新增
  `tests/python/test_flash_scratch_validate.py` 3/3 通过。
- 构建验证：validation build `build-flash-m1-06-validation-20260823` 通过 map/inventory/link
  gates，产物 build `20260822172110`；正常 release build
  `build-flash-m1-05h-20260823-release` 通过 `release_check=OK`，App/Boot 均无
  `SYSTem:DIAGnostic:FLASh:VALidate` 字符串。
- DHRT100 HIL：`build/dhrt100_flash_scratch_hil_20260823/flash_scratch_validation.json`
  记录 identity/build、target map symbol/geometry、lockout、温度/电流和前后 slot/error；
  validation 返回 `erase=1, program=1, hash_match=1, restore=1, erased=1`，
  `SYSTem:ERRor?` 为 `0,"No error"`。随后使用正常 release package 回滚，
  `build/dhrt100_flash_scratch_restore_20260823/` 记录 build `20260822170901`、
  confirmed slot 和无错误；回滚后重新发送 validation 命令得到 `-113,"Undefined header"`，
  证明 destructive command 未注册到正常运行镜像。
- 边界：当前 firmware 仍运行 v1 compatibility map；工具只读取 v2 map symbol 作为诊断，
  未向 v2 高地址写入；驱动没有 JEDEC ID API，因此 JEDEC 字段尚未闭合。

### FLASH-TASK-20260823-007 - M1-05-H host reset boundary matrix

- 状态：M1-05-H 继续进行；主机端已覆盖当前 journal 实现可注入的 body、commit marker、
  readback transport failure 和 readback corruption 四个边界；未执行 DHRT100 掉电注入。
- 代码变更：`tests/unit/test_flash_transaction_journal.c` 增加确定性 fault fixture，并在每个故障后
  重新初始化 store 模拟 reset，验证恢复结果只能是明确的旧 accepted 或新 committed：
  body=old、marker=old、readback failure/new、readback corruption/new。
- 验证：`tools/tests/run_flash_transaction_tests.ps1 -BuildDir build-flash-m1-05h-20260823`
  通过；输出包含 `journal reset boundary matrix passed`，transaction 与 journal runner 均成功。
- HAOFV 边界：本轮只加强 host recovery 证据，不配置 v1 固件访问未部署的 v2 `OTA_JOURNAL` 高地址，
  不把 host fault fixture 当作真实 power-cut/lane-seal HIL；M1-05-G/H/I、M1-06、M0-05 和 v2
  deployment 仍未完成。

### FLASH-TASK-20260823-008 - M1-05-I journal replay after store reset

- 状态：M1-05-I 继续进行；补齐 host 端 provider/store reset 后重复 terminal completion 的幂等
  证据，未执行 DHRT100 掉电或 v2 journal 写入。
- 代码提交：`f288860 test(flash): cover journal replay after reset`，已推送。
- 测试：先写入 accepted completion，再重新初始化 journal store 模拟 provider reset，重放完全
  相同的 completion；断言 append 成功但 `program` 调用数不增加、`next_sequence` 不推进，且
  latest recovery 仍返回原记录。`run_flash_transaction_tests.ps1 -BuildDir
  build-flash-m1-05i-20260823` 通过。
- 边界：该证据只覆盖同一 durable backend 内容可见时的幂等 replay；live producer 接入、跨真实
  reset 的 identity 持久化、power-cut/lane-seal HIL 和 C11 审核仍待完成。

### FLASH-TASK-20260823-009 - M1-05-J independent release owner report

- 状态：M1-05-J 继续进行；新增独立报告工具，未改变 App/Boot 写权限。
- 代码提交：`fd07202 feat(flash): emit independent release owner report`，已推送。
- 工具：`tools/flash_map/flash_release_report.py` 读取 DHRT100 App A/App B/Boot 的 map/dis，复用
  `flash_link_check.validate_link_contract()`，输出包含 git revision、map/dis SHA-256、profile、
  failure 列表和总结果的 JSON；缺失工件 fail closed。新增 Python 正/负测试 2 项。
- 验证：`test_flash_link_check.py` + `test_flash_release_report.py` 共 8/8；release gate `OK`。
  当前构建报告为 `build-flash-m1-05h-20260823-release/flash_release_report.json`，App A、App B、
  Boot 三项均无 failure。
- 边界：报告已独立落盘但尚未纳入发布流水线强制 gate，Boot 依赖审计的 C11 交叉审核仍待完成；
  不以该报告替代真实 DHRT100 v2 deployment、BOOTSEL 或 power-cut 证据。

### FLASH-TASK-20260823-010 - DHRT100 staged OTA retry blocked

- 状态：未完成；使用 `build-flash-m1-05h-20260823-release/DHRT100_UPDATE.pkg` 尝试对 DHRT100
  执行 inactive-slot staged OTA（BOOT/COMM），工具在超时窗口内没有得到任何串口 transcript，已
  中止进程。
- 结论：没有 `READY_TO_REBOOT`、重枚举、`IDLE` 或 `COMMITTED` 原始证据，本轮不烧录成功、不更新
  板端状态，也不把该尝试当作 HIL 失败矩阵。待 DHRT100 重新枚举且端口可复核后，重新执行并保存
  完整 transcript、identity、slot/result、sensor snapshot。
- 影响边界：本次只产生阻塞记录；host journal、release build/link 和文档门禁证据不受影响。M0-05
  BOOTSEL、M1-05-G/H/I durable/power-cut、v2 deployment 和 M1-05-J C11 审核仍未完成。

### FLASH-TASK-20260823-011 - M1-05-J release gate enforcement

- 状态：M1-05-J 继续进行；独立 App/Boot owner 报告已接入 `release_check` 强制门禁。
- 代码提交：`a648cbd feat(release): enforce Flash owner report gate`，已推送。
- 实现：release check 现在直接重新运行 `flash_release_report.collect_report()`；任一 App A、App B
  或 Boot map/dis 缺失、解析失败或 owner contract failure 都 fail closed。新增缺失工件负向测试。
- 验证：release gate 对 `build-flash-m1-05h-20260823-release` 输出
  `independent App/Boot Flash owner report is valid` 和 `release_check=OK`；相关 Python 回归
  13/13 通过。
- 边界：M1-05-J 的独立报告与强制 gate 已完成代码侧，Boot 依赖的独立 C11 交叉审核仍未完成；
  不改变 v1 compatibility map、DHRT100 实板状态或 v2 deployment 结论。

### FLASH-TASK-20260823-012 - DHRT100 staged OTA and FlashMap closure

- 状态：DHRT100 当前 v1 compatibility 固件完成真实 inactive-slot staged OTA；本条只证明现有
  release 路径和 FlashMap 只读诊断，不能关闭 v2 deployment、durable journal 或 BOOTSEL gate。
- 工件：`build-flash-m1-05h-20260823-release/DHRT100_UPDATE.pkg`，build `20260822162706`，
  package CRC32 `0x8DC82FF7`；完整原始 transcript：
  `build/dhrt100_ota_20260823_m1_05j_transcript.txt`。
- OTA 闭环：`READY_TO_REBOOT`, target 2 → USB CDC 重枚举窗口的 `ClearCommError` →
  `post_boot_status="IDLE",1,"NONE",0` → `committed_status="COMMITTED",1,"NONE",5`。
  最终 `SYST:FW:BUILD?="20260822162706"`、`SYST:OTA:SLOT?=2,0,2,0,0`、`SYST:ERR?=0,"No error"`。
- FlashMap/传感器只读验证：`flash_map_board_validate.py` 通过，`partitions=14/14`、
  `access_checks=260`；板温 `32.923 °C`、RP2350 内温 `38.276 °C`、current nominal `79 mA`、
  `current_calibrated=0`、front-end healthy。报告目录：
  `build/dhrt100_flash_gate_20260823_final/`。
- 多核回归：8/9 通过；identity、build、core1、loop、VDC、Calibration、config gate、error queue
  均通过，唯一失败为 `SYST:SYNC:VDC:DPLL:STAT? update_seq STALLED`，作为独立 DPLL 问题保留，
  不归因于 Flash 变更。原始报告：`build/dhrt100_m1_05j_20260823/`。
- HAOFV 边界：本次未写入 v2 高地址、未执行 BOOTSEL/full erase、未注入 power-cut；M1-05-G/H/I
  durable backend/replay、M0-05 和 C11 审核仍未完成。

### FLASH-TASK-20260823-006 - 最新 release package DHRT100 闭环

- 状态：最新 release 工件已完成 DHRT100 实板 A/B OTA；不改变 M1-05-G/H/I、M1-06、M0-05
  和 v2 deployment 的未完成状态。
- 使用工件：`build/DHRT100_UPDATE.pkg`，build `20260822161521`，package CRC32 `0x6E373F67`。
- 闭环结果：目标槽 2，`READY_TO_REBOOT` → `post_boot_status="IDLE",1,"NONE",0` →
  `committed_status="COMMITTED",1,"NONE",5`。USB reset 时的 ClearCommError 断开提示后成功
  重连，属于 CDC 重枚举窗口，不是 OTA 失败。
- 最终 DHRT100 查询：`*IDN? = GTS,DHRT100,839E1AE79EA20F31,0.1.0`；
  `SYST:FW:BUILD? = 20260822161521`；`SYST:OTA:STAT? = "COMMITTED",1,"NONE",5`；
  `SYST:OTA:SLOT? = 2,0,2,0,0`；`SYST:OTA:TXN? = 0,0,0,0,0,0,0,0`；
  `SYST:ERR? = 0,"No error"`。

### FLASH-TASK-20260823-005 - release artifact freshness gate 修复确认

- 状态：构建/release gate 已恢复全绿；本条不新增板端擦写。
- 现象：completion-lease 注入后首次 release check 发现 `DHRT100_FACTORY.uf2` 仍是旧工件，
  `flash_consumer_check` 报 factory target 缺失；未将 stale artifact 作为可发布结果。
- 处理：重新构建 `DHRT100_FACTORY`、`DHRT100_UPDATE` 和 link check；最终
  `release_check=OK`，factory/update/link 工件与当前 DHRT100 App A/B 构建一致。
- 新 package 快照：build `20260822161521`，package CRC32 `0x6E373F67`，payload SHA-256
  `e7e506b998521c7560d1269e291665a01cb5ea96d3f92779422997993218533e`。
- 边界：本次只刷新工件，没有重新烧录 DHRT100；上一条 `dfa1f02` 工件已完成板端闭环，后续
  若使用最新 package 必须重新执行 staged OTA 验证。

### FLASH-TASK-20260823-004 - Journal 最新损坏回退旧 completion

- 状态：M1-05-H 继续进行；补充 host recovery 证据，未执行 DHRT100 擦写。
- 测试提交：`1576665 test(flash): cover journal recovery fallback`，已推送。
- 新增夹具先写入 accepted 与 committed 两条有效记录，再模拟 reset 后最新槽 CRC/正文损坏；
  recovery 必须选择 sequence 较低但完整有效的 accepted 记录，不能返回损坏 committed 或
  悬挂状态。FlashTransaction host/journal runner 通过。
- 边界：当前仍是 host backend 证据；body/readback/commit marker/lane seal 的真实掉电注入、
  live OTA_JOURNAL producer 和 v2 map 部署尚未完成。

### FLASH-TASK-20260823-003 - live producer completion-lease 注入与 DHRT100 验证

- 状态：M1-05-G 继续进行；OTA image、Product Config、App OTA metadata producer 已统一从
  FlashTransactionAO 获取可选 completion lease，但当前 v1 compatibility 固件仍未配置
  OTA_JOURNAL durable backend。
- 代码提交：`dfa1f02 feat(flash): inject completion lease into live producers`，已推送。
- 实现边界：
  - FlashTransactionAO 新增 process-lifetime lease setter/getter；事务运行中禁止替换 lease，
    lease callback 不完整时 fail closed。
  - AO submit 在请求未显式提供 lease 时注入 owner 配置的 lease；三个实际 producer 均因此
    进入同一 completion journal 边界。
  - 默认 lease 为 NULL，保持当前 deployed v1 compatibility map 不访问未部署的 OTA_JOURNAL
    高地址；没有伪造 v2 live write 证据。
- DHRT100 实板验证：
  - 工件：`build/DHRT100_UPDATE.pkg`，build `20260822161114`，package CRC32 `0xEADA378E`。
  - 完整闭环：目标槽 1，`READY_TO_REBOOT` → `post_boot_status="IDLE",2,"NONE",0` →
    `committed_status="COMMITTED",2,"NONE",5`。
  - USB reset 窗口出现一次 Windows `ClearCommError` 断开提示，工具随后成功重连；最终：
    `*IDN? = GTS,DHRT100,839E1AE79EA20F31,0.1.0`，`SYST:FW:BUILD? = 20260822161114`，
    `SYST:OTA:STAT? = "COMMITTED",2,"NONE",5`，`SYST:OTA:SLOT? = 1,0,1,0,0`，
    `SYST:OTA:TXN? = 0,0,0,0,0,0,0,0`，`SYST:ERR? = 0,"No error"`。
- 仍未完成：实际 durable journal store/backend 配置、跨 reset/power-cut、provider replay、
  M1-06 Scratch、M0-05 BOOTSEL full erase/factory recovery、v2 map deployment 和 C11 审核。

### FLASH-TASK-20260823-002 - 终止 job replay 拒绝与 DHRT100 再次闭环

- 状态：M1-05-I 继续进行；FlashTransactionFB 现在拒绝已终止显式 job ID 的重复提交，
  但 provider reset 后重放、live producer 和掉电恢复仍未完成。
- 代码提交：`29585e9 fix(flash): reject terminal job replay`，已推送。
- 实现与测试：
  - FB 记录最近一次 terminal job ID；相同非零 job ID 再提交直接 fail closed，不执行 raw
    erase/program，也不推进 transaction generation。
  - 新增 host 负向夹具验证 terminal snapshot 稳定、raw erase 计数不增加；FlashTransaction
    host/journal runner 通过。
- DHRT100 实板验证：
  - 工件：`build/DHRT100_UPDATE.pkg`，build `20260822160424`，package CRC32 `0xE54787DF`。
  - 完整输出：目标槽 2，`READY_TO_REBOOT` → `post_boot_status="IDLE",1,"NONE",0` →
    `committed_status="COMMITTED",1,"NONE",5`。
  - 最终：`*IDN? = GTS,DHRT100,839E1AE79EA20F31,0.1.0`；`SYST:FW:BUILD? = 20260822160424`；
    `SYST:OTA:STAT? = "COMMITTED",1,"NONE",5`；`SYST:OTA:SLOT? = 2,0,2,0,0`；
    `SYST:OTA:TXN? = 0,0,0,0,0,0,0,0`；`SYST:ERR? = 0,"No error"`。
- 仍未完成：M1-05-G/H/I 的 durable/live producer 及掉电 replay 部分、M1-06 Scratch、M0-05
  BOOTSEL full erase/factory recovery、v2 map deployment 和 C11 交叉审核。

### FLASH-TASK-20260823-001 - DHRT100 OTA 两阶段确认工具与实板闭环

- 状态：M1-04/M1-05 继续进行；OTA transport 与 boot/confirm 现在可以由工具显式分阶段验证，
  仍未宣称 durable journal、v2 部署或整体 Flash 迁移完成。
- 代码提交：`5a71bdf feat(ota): add staged boot commit validation`，已推送。
- 工具变更：
  - `tools/ota_send/ota_send.py` 新增 `--boot-and-commit`；仅在传输状态为
    `READY_TO_REBOOT` 时执行 BOOT，等待 DHRT100 USB CDC 重枚举并确认 `IDLE`，再发送 COMM。
  - `--expect-final-state COMMITTED` 现在可验证完整 OTA 闭环，不再把 transport 阶段的
    `READY_TO_REBOOT` 误判为失败。
  - `tests/python/test_ota_send.py` 增加 quoted SCPI 状态解析回归；该测试 3/3 通过。
- DHRT100 实板证据：
  - 使用 DHRT100 package `build/DHRT100_UPDATE.pkg`，build `20260822155631`、package CRC32
    `0xCF96B57E`；第一次传输目标槽 1，第二次使用新工具传输目标槽 1 并完成确认。
  - 新工具完整输出：`READY_TO_REBOOT`, target 1 → `post_boot_status="IDLE",2,"NONE",0`
    → `committed_status="COMMITTED",2,"NONE",5`。
  - 最终查询：`*IDN? = GTS,DHRT100,839E1AE79EA20F31,0.1.0`；
    `SYST:FW:BUILD? = 20260822155631`；`SYST:OTA:STAT? = "COMMITTED",2,"NONE",5`；
    `SYST:OTA:SLOT? = 1,0,1,0,0`；`SYST:OTA:TXN? = 0,0,0,0,0,0,0,0`；`SYST:ERR? = 0,"No error"`。
  - `SYST:DIAG:SENS?` 返回板温 22.984 °C、芯片温 33.406 °C、电流 nominal estimate 79 mA，
    `current_calibrated=0`；这些是诊断快照，不是计量校准结果。
- 未完成：M1-05-G live OTA_JOURNAL producer、M1-05-H 掉电恢复、M1-05-I provider replay、
  M1-06 Scratch、M0-05 BOOTSEL full erase/factory recovery、v2 map deployment 和 C11 审核。

### FLASH-TASK-20260822-036 - DHRT100 journal 幂等修复 OTA 闭环

- 状态：M1-05-I 继续进行；journal backend 的重复 completion 语义已补齐并完成 DHRT100
  实际 OTA 验证，但 producer 接入和掉电 replay 仍未关闭。
- 代码提交：`195b85a fix(flash): make completion journal replay idempotent`，已推送。
- 代码与主机证据：
  - 相同 `job_id/transaction_generation/provider_generation/store_generation/event` 且记录内容
    完全一致时，journal append 返回成功但不重复占用槽位或推进 sequence。
  - 同一事务/事件身份但 payload 不一致时 fail closed；新增测试覆盖 program call 数、sequence
    和 latest recovery。
  - FlashTransaction host/journal runner、DHRT100 A/B/Boot 构建、FlashMap/inventory/schema/
    migration/wire/link gate 均通过；release package 已重新生成。
- DHRT100 实际验证：
  - 目标身份：`GTS,DHRT100,839E1AE79EA20F31,0.1.0`。
  - 使用 `build/DHRT100_UPDATE.pkg`（build `20260822155631`，package CRC32 `0xCF96B57E`）
    写入非活动槽；传输完成状态为 `READY_TO_REBOOT`，随后执行 `SYST:OTA:BOOT` 和重连后的
    `SYST:OTA:COMM`。
  - 最终：`SYST:FW:BUILD? = 20260822155631`，`SYST:OTA:STAT? = "COMMITTED",1,"NONE",5`，
    `SYST:OTA:SLOT? = 2,0,2,0,0`，`SYST:OTA:TXN? = 0,0,0,0,0,0,0,0`，`SYST:ERR? = 0,"No error"`。
  - `SYST:DIAG:SENS?` 返回有效板温/芯片温/电流诊断快照；当前电流仍是 nominal-only，不能
    当作计量校准值。
- 工具边界：`ota_send.py --expect-final-state COMMITTED` 对 READY_TO_REBOOT 返回了预期值不匹配，
  不是擦写失败；人工 BOOT/COMM 后闭环通过。后续工具应把“传输完成”和“重启确认”拆成两个明确
  阶段，避免把 transport 成功误判为失败。
- 仍未完成：OTA_JOURNAL live producer 接入、跨 reset/power-cut、provider replay 去重、v2 map
  部署、M1-06 Scratch 和 BOOTSEL full erase/factory recovery。

### FLASH-TASK-20260822-035 - DHRT100 release/link 回归确认

- 状态：M1-05-J 继续进行；本条只记录构建与主机证据，不涉及串口或板端擦写。
- 验证结果：
  - `tools/release_check/release_check.py --preset pico2-release --build-dir build` 返回
    `release_check=OK`，DHRT100 factory/update/Boot 工件齐全，release binary 未包含 validation
    destructive command 字符串。
  - FlashTransaction host/journal runner 通过；Flash 定向 Python 回归 31/31 通过。
  - 默认临时目录受 Windows 权限限制导致一次 pytest setup error，改用隔离的
    `D:/Temp/dhrt100-flash-tests-20260822-1` 重跑后全绿；源码和工作树未受测试临时文件影响。
- 边界：仍未烧录 DHRT100，未部署 v2 map，未完成 OTA_JOURNAL live producer、掉电恢复、replay
  和 C11 交叉审核；不能将本条当作 M1-05 退出证据。

### FLASH-TASK-20260822-033 - App 链接级 raw writer 调用边界收紧

- 状态：M1-05-J 继续进行；本轮只增强静态链接证据，不改变 deployed v1 compatibility map，
  不执行板端擦写，也不宣称 M1-05 或 v2 部署完成。
- 代码提交：`47b15a3 fix(flash): enforce linked raw write owner edges`，已推送。
- 完成内容：
  - `tools/flash_map/flash_link_check.py` 除了检查同步 raw erase/program 符号是否出现在 App
    符号表，还解析反汇编调用边；任何 App wrapper 直接调用 `drv_flash_erase` 或
    `drv_flash_program` 都 fail closed。
  - 新增 Python 负向夹具，覆盖“符号存在但实际调用边越权”的情况，避免仅依赖源码 inventory
    或 dead-code/linker 保留状态判断 owner。
- 验证结果：
  - `test_flash_link_check.py`：5/5 通过；FlashTransaction host/journal runner 通过。
  - `DHRT100`、`DHRT100_B`、`DHRT100_BOOT` 构建和 `project_flash_link_check` 通过，三类产物均
    通过现有 RAM closure、parked owner 与 raw caller 约束。
  - 本轮没有使用串口、没有烧录 DHRT100，也没有改变串口设备或 v2 map deployment state；
    M1-05-J 仍缺 Boot/release 独立 raw symbol visibility 证据。
- 下一步：继续完成 OTA_JOURNAL live producer 接入和跨 reset/power-cut/replay 夹具；板端验证统一
  使用 DHRT100 型号记录，待实际连接和新工件确认后再执行。

### FLASH-TASK-20260822-034 - Boot raw writer 调用集合固定

- 状态：M1-05-J 继续进行；在不改动 Boot writer 实现的前提下补充调用集合约束。
- 代码提交：`7183e46 fix(flash): pin boot raw writer caller set`，已推送；与文档提交保持分离。
- `--profile boot` 现在要求 `drv_flash_erase` 仅由 `main`/`ota_metadata_flash_erase` 调用，
  `drv_flash_program` 仅由 `main`/`ota_metadata_flash_program` 调用；新增越权 caller 负向夹具。
- `test_flash_link_check.py` 6/6 通过；DHRT100 App A/B/Boot 的 `project_flash_link_check` 通过。
- 仍未执行 DHRT100 板端擦写；M1-05-J 的 release 独立报告和 C11 交叉审核仍待完成。

## 记录规则

- 任务编号使用 `FLASH-TASK-YYYYMMDD-NNN`，最新记录放在顶部。
- 每条记录必须区分 source 已建立、live consumer 已迁移和板端已部署三种状态。
- 涉及 erase/program、分区切换或 factory artifact 时，必须记录目标板、map version、回退路径和
  HIL 原始报告；未烧录必须明确写出。
- 契约状态只以 `docs/check/DOCS_REGISTRY.md` 为准，进度记录不得自行把 `pending` 改写为
  `active`。
- 代码与文档提交分开记录；失败、跳过和环境依赖与通过项同等保留。

### FLASH-TASK-20260822-032 - 训练 gate owner 修复与 DHRT100 构建闭环

- 状态：M1-04 继续进行；训练 gate 的代码 owner/时序缺陷已修复，板端负向 HIL 尚未重跑，
  因此不关闭 M1-04，也不宣称 COM8 迁移完成。
- 代码提交：`a785607 fix(flash): bind training gates to domain owners`，已推送。
- 修复内容：
  - `resource_arbiter` 提供独立 Calibration 与 TDMA clock-training 发布接口，避免 Calibration
    helper 用旧快照覆盖 TDMA owner 的 gate。
  - Calibration start intent 在 core0 发布时立即置 gate；core1 完成 PIO/SM/DMA 状态消费后再由
    Calibration owner 清除，覆盖命令发布与实时执行之间的窗口。
  - TDMA owner 在训练请求接受时立即置 gate，并在 core1 服务后依据真实 CLKTRAIN snapshot
    清除；`distributed_refmem_tdma_ring_train()` 不再绕过 TDMA owner 直接提交 service intent。
  - 新增 `resource_arbiter` host fixture，验证两个 owner 独立更新时 gate 不互相清除。
- 验证结果：
  - `run_resource_arbiter_tests.ps1`、FlashTransaction host tests、TDMA ring runtime/journal
    tests 通过。
  - DHRT100、DHRT100_B、DHRT100_BOOT 构建通过；`DHRT100_FACTORY.uf2` 与 `DHRT100_UPDATE.pkg`
    已生成。release_check、A/B/Boot `flash_link_check`、FlashMap/inventory/schema/migration/wire
    gates 全部通过；release binary 未包含 validation Scratch 命令。
  - 本轮未烧录 COM8；四板既有报告中的 `CLKTRAIN=FORWARDING`/arbiter `0,0` 差异需用新固件
    重跑，且仍需记录 raw erase/program 零增量、拒绝原因和恢复后的安全态。
- 未完成：
  - M1-04 板端 Calibration/TDMA/thermal/fault negative HIL 与 warning policy；
  - M1-05 OTA_JOURNAL live producer、跨 reset/power-cut recovery、replay/idempotence；
  - M1-06 Scratch 高地址 HIL；M0-05 BOOTSEL full erase/factory recovery；v2 map deployment。

## 当前检查点

### FLASH-TASK-20260822-031 - M1-05 子项拆分与当前边界

- 状态：文档整理完成；M1-05 保持 `[~]`。本轮不宣称新的 Flash 迁移完成，也未部署 v2 map。
- 已独立列为完成的子项：固定池 owner、generation-bound immutable lease、queue/provider-reset
  负向、异步 bounded-step/abort、completion lease 边界、durable journal backend 基础，以及
  App raw caller inventory gate。对应代码验证已由 `run_flash_transaction_tests.ps1` 通过，异步
  provider reset fixture 已在代码提交 `d173d6d` 推送。
- 仍未完成的子项：OTA_JOURNAL live producer 接入、跨 reset/power-cut/torn recovery、completion
  replay 去重、App/Boot/release link-level raw symbol visibility、M2/M3 atomic store primitive
  收敛和 M1-05 独立 C11 退出评审。
- 证据边界：现有 `build-flash-m1-05-*` 和 NO.1–NO.4 并发 OTA 只证明当前 v1 compatibility 路径
  的 host/build/HIL 基础，不证明 durable reset recovery、v2 高地址部署、BOOTSEL full erase 或
  Bootloader 重刷。

### FLASH-TASK-20260822-030 - 训练态发布修复与四板并发 OTA 回归

- 状态：M1-04/M1-05 继续进行；本轮将训练活动发布 helper 复用到 core0 与 core1 owner
  service，并保留 `SYSTem:RESource:TRAINing?` 只读诊断命令。代码已推送，v2 map 仍未部署。
- 代码与构建：
  - 代码提交：`808f825 fix(flash): publish training gate from realtime owner`，已推送
    `origin/feature/rtos-multicore-haofv`。
  - 构建目录：`build-flash-m1-04-hil-20260822/`；firmware build id：`20260822071237`。
  - OTA package SHA-256：`CB24BAA5AD38123AEA1E747F40010F5870585E2ADE045711574F3D69B44D44DE`。
  - `pico2-release`、release_check、FlashMap/inventory/persistence/migration/wire/link gate、
    FlashTransaction/portable OTA/FlashMap host tests 均通过；release binary 未包含 Scratch
    validation 命令。
- 板端 HIL：
  - NO.1 / COM3 先行 OTA、boot、commit；随后 NO.2 / COM5、NO.3 / COM6、NO.4 / COM4
    并发 OTA、boot、commit。四板最终均为 build `20260822071237`、`COMMITTED`、
    `SYST:OTA:TXN? = 0,0,0,0,0,0,0,0`、`SYST:ERROR? = 0,"No error"`，训练安全态为 `0,0`。
  - 训练负向 HIL 未通过：四板 topology/ARM 后，NO.1 的
    `SYST:TDMA:RING:TRAIN:STATus?` 可见 `state=FORWARDING`，但
    `SYST:RESOURCE:TRAINing?` 仍为 `0,0`；随后训练命令返回 timeout/error。该差异作为下一项
    调试入口保留，未记录为 admission rejection，也未宣称 raw erase/program delta 为零。
  - 原始报告：`build/flash_m1_04_com3_status_20260822.txt`、
    `build/flash_m1_04_com3_boot_20260822/`、`build/flash_m1_04_COM4_boot_20260822/`、
    `build/flash_m1_04_COM5_boot_20260822/`、`build/flash_m1_04_COM6_boot_20260822/` 和
    `build/flash_m1_04_COM[3-6]_final_20260822.txt`。
- 仍缺：
  - M1-04 板端 CAL/training/thermal/fault 拒绝 HIL 与 warning policy；当前首要问题是解释
    `CLKTRAIN` snapshot 与 arbiter training snapshot 的不一致。
  - M1-05 durable journal live producer、跨 reset/power-cut recovery 和 v2 deployment；未执行
    Scratch/高地址任意 offset、BOOTSEL full erase 或 Bootloader 重刷。

### FLASH-TASK-20260822-029 - M1-04 准入负向补齐与四板并发回归

- 状态：M1-04/M1-05 继续进行；本轮补齐 Calibration training 与 TDMA clock-training 的
  host fail-closed fixture，并修正统一 OTA 负向工具按当前目标槽选择镜像表项；未部署 v2
  map、未接入 durable journal live producer，也未关闭 M1-04/M1-05。
- 日期：2026-08-22
- 代码与工具：
  - `tests/unit/test_flash_transaction.c` 新增
    `FLASH_TRANSACTION_ERROR_CALIBRATION_ACTIVE` 和
    `FLASH_TRANSACTION_ERROR_TDMA_TRAINING_ACTIVE` 负向断言，确认 raw erase/program 计数为零。
  - `tools/ota_send/ota_send.py` 在 `--package-negative` 前查询 `SYST:OTA:TARG?`，只修改
    当前目标槽的 package image entry；修复活动槽轮换后 image-crc 负向用例误改 Slot A 的问题。
  - 代码提交：`0af810d feat(flash): close admission gate host fixtures`，已推送
    `origin/feature/rtos-multicore-haofv`。
- 构建与 host gate：
  - build 目录：`build-flash-m1-04-gate-20260822/`；firmware build id：`20260822061912`。
  - `pico2-release`、release_check、FlashMap/inventory/persistence/migration/wire/link gate、
    `run_flash_transaction_tests.ps1`、Python 编译和 diff check 均通过。
- 板端 HIL：
  - NO.1 / COM3：`build/flash_burn_m1_04_gate_NO1_20260822/`；NO.2 / COM5：
    `build/flash_burn_m1_04_gate_NO2_20260822/`；NO.3 / COM6：
    `build/flash_burn_m1_04_gate_NO3_20260822/`；NO.4 / COM4：
    `build/flash_burn_m1_04_gate_NO4_20260822/`。
  - NO.2–NO.4 并发执行；三块板均通过 baseline、positive OTA、boot commit、transport/image/
    header/slot/run-offset 全部负向项和 final safe state。负向日志记录目标槽为 Slot B，最终
    `SYST:OTA:TXN?` 为零活动事务。
  - 本轮只验证当前 v1 compatibility Direct A/B 路径；未执行 Scratch/高地址任意 offset、
    BOOTSEL full erase 或 Bootloader 重刷，v2 map 仍为 `target_not_deployed`。
- 还需完成：
  - M1-04 仍缺板端 CAL/training/thermal/fault 拒绝 HIL 和 warning policy 证据；
    M1-05 仍缺 durable journal 接入 live OTA/Product Config/App metadata producer、跨 reset/
    power-cut recovery 和异步 provider/step hook。

### FLASH-TASK-20260822-028 - durable journal backend 与四板并发 OTA 回归

- 状态：M1-05 继续进行；本轮完成 durable transaction journal source/backend 的 host/build
  接入和四板硬件回归，但尚未部署 v2 OTA_JOURNAL，也未把 live producer 接入该 backend。
- 日期：2026-08-22
- 完成内容：
  - 新增 `flash_transaction_journal.h/.c`：固定槽记录、record CRC、commit marker、写后
    readback、最新有效记录恢复；torn body/commit、CRC 损坏和 journal full 均 fail closed。
  - 提供 `flash_transaction_journal_make_completion_lease()` adapter；CMake 与既有
    `run_flash_transaction_tests.ps1` 纳入 journal host fixture。
  - `test_flash_transaction_journal.c` 覆盖 append/recover、单点损坏、半写、容量耗尽和
    completion lease 适配；transaction 与 journal host runner 均通过。
- 构建与工件：
  - build 目录：`build-flash-m1-05-journal-20260822/`
  - firmware build id：`20260822053750`
  - OTA package SHA-256：`A0EA4E14E50400225DD2C6D0748A9CE20FA949F28376CDA5944E1F9F833DD7A4`
  - `release_check.py`、FlashMap/inventory/persistence/migration/wire/link gates 均通过。
- 板端 HIL：
  - NO.1（COM3）先行通过；NO.2（COM5）、NO.3（COM6）、NO.4（COM4）按 USB serial
    定向 factory load 后并发执行 Direct A/B OTA。
  - 四板最终均通过 `baseline_query`、`positive_ota`、`boot_commit`、transport/image/header/
    slot/run-offset 全部负向项和 `final_safe_state`；原始报告目录为
    `build/flash_burn_journal_backend_NO1_20260822/` 至
    `build/flash_burn_journal_backend_NO4_20260822/`。
  - 中途发现 `image-crc` 负向工具固定修改 Slot A 表项；活动槽已轮换到 Slot B 时该用例
    会误命中未选中的镜像。统一活动槽为 Slot A 后重跑，四板全部通过；这不是 firmware CRC
    回归，且未修改工具行为。
  - 最终四板均回到无 pending、安全状态；未执行 Scratch/高地址任意 offset、BOOTSEL full
    erase 或 Bootloader 重刷，v2 map 仍为 `target_not_deployed`。
- 还需完成：
  - 将 durable journal backend 接入 live OTA/Product Config/App metadata producer，并补
    async provider/step hook 的跨 reset、power-cut/torn journal 证据。
  - 完成代码提交后再登记本条文档证据；M1-05 不得标记完成。

### FLASH-TASK-20260822-027 - completion lease/journal 边界与四板回归

- 状态：M1-05 继续进行；本轮建立 completion lease/journal 合约和 transaction 边界 fail-closed
  语义，但尚未部署 v2 OTA_JOURNAL，也未把 live producer 接入 durable completion backend。
- 日期：2026-08-22
- 完成内容：
  - `flash_transaction_completion_lease_t` 绑定 retain/release/append 生命周期；事务在
    accepted、programmed、verified 和最终 terminal 边界发布带 job/transaction/provider/store
    generation 的 journal record。
  - journal append 失败立即阻止后续 verify/commit；completion lease 在终态释放一次，重复 service
    不重复发布 terminal record。COMMITTED 只在 core1/resource release 成功后发布，release failure
    不会被 durable record 伪装成 committed。
  - host fixture 覆盖四个边界的 journal failure、release failure、lease retain/release 和
    duplicate terminal service；已验证物理 raw 已发生时仍保持 fail-closed，不伪造回滚。
- 验证结果（以下为本次构建/HIL 快照，非长期事实源）：
  - `tools/tests/run_flash_transaction_tests.ps1` 和全量
    `tools/tests/run_host_unit_tests.ps1` 均通过，host runner 为 30/30。
  - `build-flash-m1-05-completion-20260822/` 的 `pico2-release` 构建、FlashMap/inventory/
    persistence/migration/wire/link gate 和 `release_check.py` 均通过；build id 为
    `20260822052715`，package payload SHA-256 为
    `e9fb9ef5ed911f9c49709f786bbf9270da74f0c4e02ea4c1f639d1655521ab0d`。
  - NO.1 / COM3：`build/flash_burn_completionlease_release_NO1_20260822/`；NO.2 / COM5：
    `build/flash_burn_completionlease_release_NO2_20260822/`；NO.3 / COM6：
    `build/flash_burn_completionlease_release_NO3_20260822/`；NO.4 / COM4：
    `build/flash_burn_completionlease_release_NO4_20260822/`。四块板均通过
    `baseline_query`、`positive_ota`、`boot_commit`、`final_safe_state`；NO.2–NO.4 为并发执行。
  - 本轮 live OTA 仍使用 v1 compatibility Direct A/B，completion lease 未接入 v2 durable store；
    v2 map 保持 `target_not_deployed`，未执行 Scratch、高地址任意 offset、BOOTSEL full erase
    或 Bootloader 重刷。
- 提交与推送：
  - 代码提交 `6fc17cd feat(flash): add completion journal lease boundaries` 已推送
    `origin/feature/rtos-multicore-haofv`；文档使用独立提交。
- 还需完成：
  - 实现 OTA_JOURNAL 的 durable backend、reset recovery/torn-record 选择和 live OTA/Product/
    metadata producer wiring；补 power-cut 与跨 reset HIL，再复核 M1-05/M1-03 退出门禁。

### FLASH-TASK-20260822-026 - live producer lease 与四板并发 OTA 证据

- 状态：M1-05 继续进行；OTA、Product Config、App OTA metadata producer 已接入
  generation-bound immutable buffer lease，并完成当前 v1 Direct A/B 的四板并发 OTA 闭环。
  completion lease、durable reset journal、power-cut 和跨 reset duplicate completion 仍未完成，
  因此 M1-05/M1-03 不关闭。
- 日期：2026-08-22
- 完成内容：
  - live producer 在 `flash_transaction_ao_execute()` 生命周期内创建并传递
    `flash_transaction_buffer_lease_t`；provider reset 使用 acquire/release 原子 pending 语义。
  - 仍保持大于 `FLASH_TRANSACTION_OWNED_PAYLOAD_SIZE` 且无合法 lease 时 fail-closed；当前
    refcount 是同步轻量生命周期，不宣称跨 reset durable lease。
- 验证结果（以下为本次构建/HIL 快照，非长期事实源）：
  - `build-flash-m1-05-20260822/` 的 `pico2-release` 工件、host/build/link/inventory、
    FlashMap/persistence/migration/wire gate 和 `release_check.py` 均通过；build id 为
    `20260822045432`。
  - NO.1 `0010071E65B5CB38` / COM3：`build/flash_burn_livelease_NO1_20260822/`；
    NO.2 / COM5：`build/flash_burn_livelease_NO2_20260822/`；NO.3 / COM6：
    `build/flash_burn_livelease_NO3_20260822/`；NO.4 / COM4：
    `build/flash_burn_livelease_NO4_20260822/`。四块板均通过 `baseline_query`、
    `positive_ota`、`boot_commit`、`final_safe_state`；NO.2–NO.4 为并发执行。
  - 本轮仍只验证已部署的 v1 compatibility Direct A/B 路径；v2 map 保持
    `target_not_deployed`，未执行 Scratch、高地址任意 offset、BOOTSEL full erase 或 Bootloader 重刷。
- 还需完成：
  - completion lease/durable reset journal，以及 provider power-cut、duplicate completion 和
    跨 reset 证据；完成后再复核 M1-05/M1-03 退出门禁。
  - 不改变 `docs/check/DOCS_REGISTRY.md` 中契约的 `pending` 状态，C11 激活审核尚未触发。

### FLASH-TASK-20260822-024 - 长期迁移检查点与 M1-05 下一 gate

- 状态：HAOFV Flash 迁移继续按 M0/M1 工作包逐项闭环；当前唯一进行中的实现切片是 M1-05
  buffer/owner convergence。M1-02、M1-03、M1-04、M0-05 和 M1-06 均保持各自未完成状态，
  不因一次构建或一次四板 OTA 而提前关闭。
- 当前已闭环证据：fixed owned payload、large-payload fail-closed、queue/duplicate terminal、
  raw-step abort、provider generation reset fail-closed；对应 host、release/build/link/inventory
  和四板 Direct A/B OTA 报告均已记录在本文件后续条目。
- 下一项且仅下一项：将 generation-bound immutable buffer lease 接入 live producer，并补齐 completion
  lease/durable reset 语义；在此之前不进入 v2 Scratch 写入。
- 退出条件：provider 生命周期正向/负向 host fixture、producer reset/duplicate completion 语义、
  release 与 inventory/link gate、必要的板端报告、代码/文档分离提交和文档四项门禁全部具备；
  之后才评估 M1-05/M1-03 退出和 M1-06 validation-only Scratch lease。
- 约束复核：v2 target map 仍为 `target_not_deployed`；未执行任意 offset 命令、BOOTSEL full erase
  或高地址 Scratch 破坏性验证；契约登记表未发生状态变更，不需要 C11 激活审核。

### FLASH-TASK-20260822-025 - M1-05 immutable buffer lease 生命周期

- 状态：M1-05 继续进行；generation-bound immutable buffer lease 已在 FlashTransactionFB/AO
  建立最小生命周期实现，live producer 仍默认使用固定 pool，completion lease/durable reset
  journal 尚未完成。
- 日期：2026-08-22
- 完成内容：
  - 新增 `flash_transaction_buffer_lease_t`，大于 `FLASH_TRANSACTION_OWNED_PAYLOAD_SIZE` 的
    program request 必须提供匹配 provider generation、长度、immutable data、retain/release
    回调；request 在 VALIDATE 后取得 lease，raw/verify 使用 lease data，终态 RELEASE 释放一次。
  - 无 lease、长度不足、generation mismatch 或 retain 失败均在 raw writer 前返回 `PROVIDER`；
    小 payload 仍保持 submit-time fixed-pool snapshot，未改变当前 OTA producer 默认路径。
  - host fixture 覆盖大 payload lease 正向 retain/program/verify/release、generation mismatch、
    retain failure、已有 large-payload no-raw、producer reset 和 raw-step abort 语义。
- 验证结果：
  - FlashTransaction 专项测试和全量 30 个 host test scripts 通过。
  - `pico2-release` 构建、FlashMap/inventory/persistence/migration/wire/link gate 与
    `release_check.py` 通过；工件目录 `build-flash-m1-05-20260822/`，本轮 build id 为
    `20260822044323`。
  - NO.1 `0010071E65B5CB38` / COM3 先完成 factory 烧录和 OTA；NO.2/NO.3/NO.4（COM5/COM6/COM4）
    随后并发 OTA，四块板 baseline、positive OTA、boot/commit、final safe state 全部 PASS，原始
    报告位于 `build/flash_burn_provider_NO1_20260822/` 至 `build/flash_burn_provider_NO4_20260822/`。
- 提交与推送：
  - 代码提交 `852fd48 feat(flash): add immutable buffer lease provider` 已推送
    `origin/feature/rtos-multicore-haofv`。
- 还需完成：
  - live OTA/Product/metadata producer 接入 lease、completion lease/durable reset journal、
    provider power-cut/duplicate completion 证据；完成后再评估 M1-05/M1-03 退出。

### FLASH-TASK-20260822-020 - NO.1 至 NO.4 工厂烧录与 OTA 闭环

- 状态：M0-05/M1 实板验证继续进行；四块板均完成 `build-product-release` factory UF2 烧录，
  随后完成 Direct A/B 正向 OTA、Boot、commit 和最终安全态检查。
- 日期：2026-08-22
- 板卡与报告：
  - NO.1 `0010071E65B5CB38` / COM3：`build/flash_burn_NO1_20260822_positive2/`，PASS。
  - NO.2 `FB276192BEF9CCE1` / COM5：`build/flash_burn_NO2_20260822/`，PASS。
  - NO.3 `2BD5090FE009FA2A` / COM6：`build/flash_burn_NO3_20260822/`，PASS。
  - NO.4 `A1E549202D18ED6A` / COM4：`build/flash_burn_NO4_20260822/`，PASS。
- 每块板的 `baseline_query`、`positive_ota`、`boot_commit`、`final_safe_state` 均通过；
  `SYST:OTA:TXN?` 最终为零活动事务。该证据验证当前 v1 compatibility factory/OTA 路径，
  不等同于 v2 高地址 Scratch 或 BOOTSEL 物理回退门禁。

| 工作包 | 状态 | 已有证据 | 下一 gate |
|---|---|---|---|
| M0-01 implementation inventory | 完成 | raw caller allowlist、旧地址依赖、构建/release scan gate | 后续新增 caller 必须先登记。 |
| M0-02 FlashMap source/schema | 完成 | v1 compatibility/v2 target 双版本 source、生成物、live consumer、artifact/drift gate | 后续 map 变更必须同时通过 freshness 与 consumer gate。 |
| M1-01 Geometry/Raw HAL | 完成 | 16 MiB geometry、overflow-safe range、host boundary tests、A/B RAM closure/link ownership gate、COM8 v1 OTA/lockout HIL | 后续新增 raw/link caller 必须先通过 inventory 与 link gate。 |
| M1-02 permission view | 进行中 | generated X-macro、纯算法服务、版本化 live consumer、host 边界测试、COM8 OTA/只读权限闭环 | 真实 writer 接入、v2 factory 部署与 C11 激活审核。 |
| M1-03 FlashTransactionAO | 进行中 | one-deep queue/FB/Vector、OTA image/Product Config/App metadata writer、owned two-page snapshot、transaction-owned core1 park、COM8 双向 OTA 闭环 | Boot writer、异步 completion、immutable provider/refcount、运行时 abort 与 durable reset 语义。 |
| M0-04 wire/parser corpus | 代码验证完成，文档待收口 | golden/truncation/bit-mutation corpus 已绑定 `pota_*` 与 TDMA frame parser；release gate、host runner `30/30` 通过 | 更新本文件与 TODO 的状态描述，完成独立审查后再关闭 M0-04。 |
| M0-05 migration/rollback | 阻塞 | validation recovery 入口和回退日志已保留；应用态 reboot 未使 RP2350 ROM BOOTSEL 保持可见 | 物理 BOOTSEL full erase、factory UF2 verify、COM8 恢复报告。 |
| M1-04 mode/thermal/dual-core gate | 进行中 | thermal/diagnostics/trigger/FAULT/resource reason 与 park-timeout HIL 已通过 | Calibration/TDMA owner gate、RUN/CAL/training negative HIL、warning policy 和 COM8 负向证据。 |
| M1-05 buffer/owner convergence | 进行中 | fixed owned payload、large-payload fail-closed、generation-bound immutable lease、queue/duplicate terminal、raw-step abort、provider-reset host fixture | live producer lease 接入、completion lease/durable 证据。 |
| M1-06 high-address Scratch | 未开始 | 仅有 v2 target map/permission 输入，未对板写入 | validation-only Scratch intent、COM8 高地址闭环、恢复与 release string scan。 |

### FLASH-TASK-20260822-021 - M1-05 原始步骤 abort 负向闭环

- 状态：M1-05 继续进行；固定 owned payload 与大 payload fail-closed 保持不变，本切片补齐同步
  raw erase/program 步骤的 abort 语义，未宣称 provider/refcount、producer reset 或 durable completion
  已完成。
- 日期：2026-08-22
- 完成内容：
  - `FlashTransactionFB` 在 raw erase/program 回调返回成功后重新检查 `abort_pending`；若回调期间
    收到 abort，记录已处理字节和对应 erase/program delta，直接进入 RELEASE 并发布 ABORTED 结果，
    不执行 VERIFY 或 COMMIT。物理 raw 操作已经发生这一事实不被回滚或伪造为未写入。
  - host fixture 在 fake erase 与 fake program 回调内部通过 `flash_transaction_fb_request_abort()`
    注入 abort，断言事务最终为 `ABORTED`、completion level 停留在 `PROGRAMMED`、`verified_bytes=0`，
    且两类 verify 回调均未被调用；release/unpark 仍各执行一次。
- 验证结果：
  - `tools/tests/run_flash_transaction_tests.ps1` 通过。
  - 全量 `tools/tests/run_host_unit_tests.ps1` 的 30 个脚本通过；执行器分段输出曾在嵌套 PowerShell
    会话中提前回收，剩余 4 个脚本随后单独复跑并全部通过。
- 提交与推送：
  - 代码提交 `df05507 fix(flash): abort transaction after raw step` 已推送
    `origin/feature/rtos-multicore-haofv`。
- 还需完成：
  - immutable provider/refcount、producer reset、completion lease/durable 语义；之后再评估 M1-03/
    M1-05 退出和 M1-06 Scratch 进入条件。

### FLASH-TASK-20260822-022 - M1-05 新工件四板烧录与双板并发 OTA

- 状态：M1-05 host/build/HIL 继续进行；本轮使用包含 raw-step abort 修复的新 release 工件完成
  四块板 factory 烧录与 Direct A/B OTA 闭环，未改变 v2 target map 的 deployed 状态。
- 日期：2026-08-22
- 工件与构建：
  - 构建目录：`build-flash-m1-05-20260822/`；`pico2-release` 配置/构建和
    `tools/release_check/release_check.py` 均通过。
  - 工件：`RP2350_TRIG_FACTORY.uf2`、`RP2350_TRIG_UPDATE.pkg`；构建脚本通过 inventory、map、
    persistence、migration、wire 和 RAM/link closure gates。
- 板卡与报告：
  - NO.1 `0010071E65B5CB38` / COM3：`build/flash_burn_M1_05_NO1_20260822/`，PASS。
  - NO.2 `FB276192BEF9CCE1` / COM5：`build/flash_burn_M1_05_NO2_20260822/`，PASS。
  - NO.3 `2BD5090FE009FA2A` / COM6：`build/flash_burn_M1_05_NO3_20260822/`，PASS。
  - NO.4 `A1E549202D18ED6A` / COM4：`build/flash_burn_M1_05_NO4_20260822/`，PASS。
  - 每块板的 `baseline_query`、`positive_ota`、`boot_commit`、`final_safe_state` 均通过，最终
    `SYST:OTA:TXN?` 为零活动事务；NO.3/NO.4 的 OTA 流程按要求并发执行并各自完成 commit。
- 说明：多板烧录期间 USB 地址会在每次重启后重新编号，实际操作以 picotool 输出的 serial 为准；
  报告中的 COM 与 serial 映射保持不变。该闭环验证当前 v1 compatibility factory/OTA 路径，
  不等同于 v2 高地址 Scratch 或 BOOTSEL full-erase 回退门禁。
- 提交与推送：
  - 代码：`df05507 fix(flash): abort transaction after raw step`。
  - 工具：`44a007f fix(tools): ignore temporary non-utf8 flash sources`。
- 还需完成：
  - immutable provider/refcount、producer reset、completion lease/durable 语义，以及 M0-05
    BOOTSEL full erase/reflash 和 M1-06 Scratch HIL。

### FLASH-TASK-20260822-023 - M1-05 provider generation reset fail-closed

- 状态：M1-05 继续进行；producer reset 已有显式 AO/FB 通知入口和 host 负向证据，但这不等同于
  immutable 大 payload provider/refcount 或跨 reset durable completion 已完成。
- 日期：2026-08-22
- 完成内容：
  - 新增 `flash_transaction_fb_notify_provider_reset()` 与
    `flash_transaction_ao_notify_provider_reset()`；通知必须匹配当前 program request 的
    `provider_generation`，错误 generation 或终态事务拒绝。
  - reset 在 raw 步骤前到达时，事务以 `PROVIDER` 失败且不调用 raw writer；reset 在 raw 回调期间
    到达时保留已处理字节/PROGRAMMED completion，直接 RELEASE/FAILED，跳过 VERIFY/COMMIT，避免把
    已发生的物理写入伪装成可提交结果。
  - host fixture 覆盖 generation mismatch、raw 前 reset、raw 期间 reset、无 verify、owner release
    和终态重复通知拒绝。
- 验证结果：
  - `tools/tests/run_flash_transaction_tests.ps1` 通过；全量 30 个 host test scripts 通过。
  - `pico2-release` 构建、FlashMap/inventory/persistence/migration/wire/link gate 和
    `release_check.py` 通过；新工件 build id `20260822035034`，位于
    `build-flash-m1-05-20260822/`。
  - NO.1 `/` COM3 先烧 factory 后通过 OTA；NO.2/NO.3/NO.4（COM5/COM6/COM4）随后并发 OTA，四块板
    的 baseline、positive OTA、boot/commit、final safe state 均 PASS，报告分别位于
    `build/flash_burn_next_NO1_20260822/` 至 `build/flash_burn_next_NO4_20260822/`。
- 提交与推送：
  - 代码提交 `85ae5f9 feat(flash): fail closed on provider reset` 已推送
    `origin/feature/rtos-multicore-haofv`。
  - 后续提交 `ff15761 fix(flash): publish provider reset atomically` 将 reset pending 标志改为
    acquire/release 原子发布，覆盖 producer/FlashTransaction 跨执行上下文的可见性；已推送。
- 还需完成：
  - immutable provider/refcount、completion lease/durable reset journal；之后再评估 M1-03/M1-05
    退出和 M1-06 Scratch 进入条件。

### FLASH-TASK-20260822-019 - Calibration/TDMA training gate 接入 resource_arbiter

- 状态：M1-04 继续进行；Calibration loopback/CLOCK_CODED/P3 与 TDMA clock-training 的运行态
  由 owner snapshot 发布到 `resource_arbiter`，FlashTransactionAO 在 QUIESCE 阶段统一消费并
  以独立 policy reason 拒绝新写；未改变 RUN 下“先检查、再取得 FLASH owner”的语义。
- 日期：2026-08-22
- 完成内容：
  - `resource_arbiter_snapshot_t` 增加 calibration-training 和 TDMA clock-training 活动事实，
    由 `calibration_manager_service()` 读取已有 Calibration/TDMA owner snapshot 后发布。
  - FlashTransaction 新增 `CALIBRATION_ACTIVE`、`TDMA_TRAINING_ACTIVE` reason；状态进入
    Vector 的 `policy_gate_reason/last_error`，不会执行 raw erase/program。
  - host FlashTransaction runner `30/30` 通过；release 构建尝试被既有
    `flash_inventory.py` UTF-8 扫描问题阻断，未伪造构建通过证据。
- 仍需完成：
  - 增加针对真实 owner gate 的 host fixture/板端 validation-only negative HIL，补 warning
    pause/de-rate policy；随后再收敛 M1-05 provider/abort 和 M1-06 Scratch。

### FLASH-TASK-20260822-018 - 跨电脑交接与 M0-M1 下一步冻结

- 状态：M0-04 的实现和主机验证已完成，但本进度文档尚未把它收口为完成；M0-05 仍受物理
  BOOTSEL 阻塞；M1-04/M1-05/M1-06 继续进行或未开始。
- 日期：2026-08-22
- 已确认事实：
  - 当前分支为 `feature/rtos-multicore-haofv`，最近代码和文档提交均已推送；工作树交接前应
    重新执行 `git status --short --branch`，不可依赖旧终端状态。
  - M1-04 不得通过“RUN 全拒绝 OTA”解决。RUN 是普通运行态，OTA 必须在 trigger、Calibration、
    TDMA training、thermal critical、FAULT 和资源冲突均无阻断时由 transaction owner 取得 FLASH
    后进入 OTA；Calibration/TDMA 只能发布 gate，不能直接操作 Flash。
  - COM8 板卡 `839E1AE79EA20F31` 的应用 OTA、park-timeout 负向和 lockout 正向证据已保留在
    `build/`；这些证据不替代 ROM BOOTSEL full erase/reflash。
- 下一台电脑的执行顺序：
  1. 先实现并测试 `resource_arbiter` 的 Calibration/TDMA training gate 及统一 snapshot 消费，
     再做 validation-only COM8 负向 HIL；断言 raw erase/program 计数为零、policy reason 可追溯。
  2. 再完成 immutable provider/refcount、producer reset、duplicate completion 和 raw-step abort，
     重新跑 host runner、release/RTOS+双核构建及 inventory/link gate。
  3. 只通过 Scratch lease intent 做 M1-06 高地址验证，完成擦除恢复和 release command string scan。
  4. 有条件时按住 BOOTSEL 完成 M0-05；保存 full erase、UF2 hash、identity/build/slot/error queue
     和恢复后的 COM8 原始报告。
  5. 代码与文档分离提交并推送；文档提交前运行 docs_check、doc_regression、相关 pytest 和
     `.githooks/pre-commit`。登记表 status 变更必须补 C11 独立交叉审核，不得自审自批。
- 未完成项不应标记为 `[x]`：M0-05 BOOTSEL、M1-04 owner gate/HIL、M1-05 provider/abort、M1-06
  Scratch HIL，以及 M1 退出契约的 C11 激活。

### FLASH-TASK-20260822-017 - RAM closure 与 park-timeout 负向闭环

- 状态：M1-01 完成；M1-04 继续进行。App A/B 构建已强制检查 Flash critical RAM closure，
  core1 no-ACK 注入已证明 raw erase/program 不会执行且清除后 core1 恢复运行。
- 日期：2026-08-22
- 完成内容：
  - `flash_link_check.py` 检查 A/B RAM-resident symbol closure、XIP 引用、IRQ disable/restore 指令、
    parked raw caller ownership，并拒绝同步 raw erase/program 链入 App；检查已进入普通构建和 release gate。
  - validation-only SCPI 增加已有 lockout fault flag 的设置/查询；release 构建继续由 string gate 证明
    不包含 `SYSTem:OTA:INJect` 命令。
  - 新增 no-ACK HIL：核对 FlashTransaction requester/operation/error、零 processed/verified、零 raw
    erase/program delta、timeout 单次增长、注入读回清零和 core1 heartbeat 恢复。
- COM8 闭环（以下数字为本轮快照，非长期事实源）：
  - 目标板 `839E1AE79EA20F31` 从 release build `20260822003017` 正向 OTA 到 validation build
    `20260822004135`；安装报告位于 `build/flash_park_timeout_COM8/install_validation/`。
  - 首次负向运行的核心断言全部成立，但 USB CDC 丢失 clear 命令 ACK，旧工具误判失败；原始证据保留
    在 `build/flash_park_timeout_COM8/no_ack_negative/`。修正为以立即读回为权威后，复跑报告
    `no_ack_negative_rerun/` 通过：timeout `2->3`、transaction error `PARK(18)`、raw erase/program
    delta 均为零、core1 heartbeat `94018->95028`。
  - 最终用 release package 做 A/B OTA/Boot/commit，build `20260822004128`、活动槽 A、identity 不变；
    通过报告位于 `build/flash_park_timeout_COM8/release_closed_loop_rerun/`，request/ACK/release 均为
    `2->938`，镜像事务为 512-byte、metadata 事务为 256-byte，均 verified/committed。
- 验证与提交：
  - release 与 validation A/B 构建、link gate、release string scan、host runner `30/30`、定向 Python
    均通过；代码提交 `8ae4296`、`1efc77b`、`b193d43` 已推送。
- 还需完成：
  - M0-04 parser/fuzz、M0-05 BOOTSEL；M1-04 mode/Calibration/TDMA/thermal 负向 HIL；M1-05
    immutable provider/abort；M1-06 v2 Scratch HIL。

### FLASH-TASK-20260822-016 - transaction-owned core1 park 与 512-byte OTA 双向闭环

- 状态：M1-03/M1-04/M1-05 继续进行；App raw write 的 core1 park 会话已收敛到
  FlashTransaction owner，当前 OTA producer 使用的两页 payload 已由 transaction 固定池持有。
- 日期：2026-08-22
- 完成内容：
  - Raw HAL 拆分 session begin/end 与 parked erase/program；只有 `flash_transaction_ao.c` 可调用
    parked write，Boot 同步 raw writer 继续使用独立 session，inventory 拒绝其他 owner。
  - FlashTransaction 在 acquire Flash resource 后请求 park，在释放 Flash resource 前释放 core1；
    park/release 失败分别进入明确错误，release 失败可覆盖原成功终态。
  - owned payload pool 扩展到 `FLASH_TRANSACTION_OWNED_PAYLOAD_SIZE`，覆盖当前 OTA producer 的
    两页块；更大 payload 继续以 `PROVIDER` fail closed。
  - OTA HIL 工具从统一包 image table 计算目标槽首个 payload block，在传输中采样 image Vector，
    并在结束后独立核对 metadata Vector，避免后写 metadata 覆盖 image 快照造成误判。
- COM8 闭环（以下数字为本轮快照，非长期事实源）：
  - 目标板 `839E1AE79EA20F31` 原运行 build `20260821234514`。默认统一包和 raw 512-byte 发送在旧
    256-byte pool 上分别暴露 `INVALID_STATE/PROVIDER`；后续失败与尝试均保留在
    `build/flash_park_owner_COM8/`，不计为通过证据。
  - `picotool reboot -f -u` 未让设备保持可访问 BOOTSEL，直接 factory load 后 build 也未改变；
    因此 M0-05 BOOTSEL 样板恢复仍未完成。恢复实际通过 256-byte raw inactive-slot OTA、Boot/commit
    完成，串口确认新 build `20260821234933`。
  - 新 build 使用默认 512-byte unified package 完成 B->A 与 A->B 两次 OTA/Boot/commit；两次 image
    Vector 分别指向非活动 partition 1/2，均为 512/512/512 programmed/verified/committed；最终
    metadata Vector 均为 requester 2、partition 3、256/256/256 committed。
  - 两个方向的 core1 request/ACK/release 均从 2 增长到 938，timeout/release timeout 为 0；最终
    build 保持 `20260821234933`、identity 不变、错误队列为空。通过报告位于
    `build/flash_park_owner_COM8/default512_dynamic_probe_closed_loop/` 与
    `build/flash_park_owner_COM8/default512_dynamic_probe_reverse/`。
  - 最后传感器快照板温约 31.069 degC、RP2350 内温约 35.934 degC；current frontend healthy、
    nominal-only，电流估算尚未校准。
- 验证与提交：
  - FlashTransaction/Raw HAL fixture 覆盖 park/release failure、无 session parked write、重复
    begin/end 和 parked caller ownership；全量 host runner `30/30`，HIL parser pytest `4/4`。
  - 全量 Python 为 `123/124`；唯一失败是缺少既有 TDMA 反射台架报告
    `build-product-release/tdma_pio_timing_check_reflection_20260821.json`，未伪造该证据。
  - 代码提交 `bdc744b`、`accdfbc`、`f3d5a96`、`3e48dab` 已推送；factory UF2 SHA-256 为
    `9DC685494D620D8B9B148F175881F0ED7D19B7B65FF797C416985D5610FD863B`，统一包 SHA-256 为
    `4DF7A734477A9B0E744322EC35E501A687C8686631B84C6068557CF8AC297A7D`。
- 还需完成：
  - M0-05 真实 BOOTSEL full erase/reflash；M1-04 RAM/XIP closure、park-timeout 与 mode 拒绝 HIL；
    M1-05 immutable provider/refcount、producer reset/raw-operation abort；M1-06 受限 Scratch HIL。

### FLASH-TASK-20260822-012 - M0 persistence registry 与 migration policy 输入

- 状态：M0-03 完成；M0-05 进行中。新增输入只描述 namespace、兼容和回退边界，不改变当前 v1
  live map，也不触发 v2 烧录。
- 日期：2026-08-22
- 完成内容：
  - `config/persistence_schema_registry.json` 为 BCB、Image Manifest、Product NVS、Calibration
    NVS、VDC NVS、Deployment Capsule、Fault FCB、OTA Journal 和 PIO Catalog 分配唯一 type_id，
    并登记 writer/reader、兼容、寿命、atomicity、rollback、factory default、诊断投影和 SD evidence。
  - registry 明确 required unknown field fail-closed、optional field skip-but-preserve-integrity，
    同时列出 Domain Vector/ECC/queue/lock/VDC lock/RefMem ACK/PIO-DMA runtime 的 negative inventory。
  - `config/flash_migration_policy.json` 固定 v1_compat -> v2 只能走 BOOTSEL full erase/reflash，
    明确 identity/Product/OTA/Calibration/report 的备份转换策略和 blank/unknown/v2 Boot 行为；在线
    relocation、destructive SCPI 与 Bootloader 在线更新均禁止。
  - 新增 `persistence_schema_check.py`、`flash_migration_check.py` 及 Python 正/负向 fixture。
- 验证结果：
  - schema/migration checker 与 5 个定向 pytest 通过；文档门禁将在本切片单独提交前运行。
- 还需完成：
  - M0-04 wire corpus；M0-05 v1 factory artifact checksum、BOOTSEL 实板恢复和独立回退报告。

### FLASH-TASK-20260822-013 - M1-05 大 payload fail-closed 与 completion 负向语义

- 状态：M1-05 进行中；固定 program-page owned payload 已保留，大 payload immutable provider 尚未
  实现，因此超出固定 pool 的 App program intent 明确返回 `PROVIDER`，不调用 raw operation。
- 日期：2026-08-22
- 完成内容：
  - `FlashTransactionFB` 在 requester/partition policy 通过后拒绝超出 owned payload pool 的 program
    请求，避免 producer 可变 buffer 在排队或 service 间被别的任务修改。
  - host fixture 覆盖 large payload no-raw、queue full、duplicate terminal completion 和 terminal
    状态下 abort 拒绝；已有 page snapshot fixture 继续证明小 payload submit 时复制。
- 验证结果：
  - FlashTransaction host tests、release 构建通过；代码提交 `3b349a2 test(flash): fail closed on
    aliased large payloads` 已推送。
- 还需完成：
  - generation/refcount immutable provider、producer reset/duplicate completion 持久化语义，以及
    page/sector raw step 可注入的 abort fixture；完成后再评估 M1-03/M1-05 退出。

### FLASH-TASK-20260822-014 - M0-04 Boot/OTA/TDMA wire 输入与 golden vectors

- 状态：M0-04 进行中；字段、提交顺序、durable offset、reject reason 已冻结为机器输入，真实 parser
  与 fuzz corpus 尚未实现，因此相关契约仍保持 pending。
- 日期：2026-08-22
- 完成内容：
  - `config/flash_wire_contracts.json` 固定 BCB_RECORD、IMAGE_MANIFEST_TLV、OTA_STREAM_SESSION
    和 TDMA_OTA_CONTROL 四类输入；明确 identity/generation/destination、hash/signature/security
    counter、credit/resume token 与 OPEN/DATA/ACK/CLOSE/ABORT/STATUS 生命周期。
  - 注册 bad magic/version/length/CRC、unknown required、identity mismatch、generation replay、
    destination forbidden、non-durable offset、signature invalid、security counter rollback 等拒绝原因。
  - 加入 open、generation replay、unknown required、bad signature 四个 golden vector 及 checker/负向
    pytest；不改变当前 v1 OTA live path。
- 验证结果：
  - wire checker 与定向 pytest 通过；文档门禁将在本切片单独提交前运行。
- 还需完成：
  - parser/golden corpus 与 fuzz harness 接入具体 BCB/OTA/TDMA 实现，并完成 M0-04 独立审查。

### FLASH-TASK-20260822-015 - M0 输入 gate 接入构建与 M1 基线复核

- 状态：M0-03 完成；M0-04 进行中；M0-05 进行中；M1-01/M1-02 代码基线可复核，M1 退出仍受
  BOOTSEL 回退实板证据、link-level visibility、core1 park owner 和 Scratch HIL 阻塞。
- 日期：2026-08-22
- 完成内容：
  - `project_flash_contract_check` 已在 CMake 中强制运行 persistence schema、migration policy 和
    wire contract checker；任一输入缺失/漂移会阻断 release 与 RTOS+双核构建。
  - M0-03/M0-04 的 checker 正向/负向 pytest 与全量 host runner `30/30` 通过；release build
    `pico2-release` 重新配置并通过，构建输出同时报告 `persistence_schema=OK`、
    `flash_migration=OK`、`flash_wire=OK`。
  - 保持 HAOFV 边界：v1 compatibility 仍是唯一 live map，v2 target_not_deployed；没有在线搬迁、
    高地址写入、Bootloader 重刷或 destructive SCPI。
- 还需完成：
  - M0-05 生成并校验 factory UF2/BOOTSEL 实板回退报告；M0-04 parser/fuzz corpus；M1-01 link
    visibility、M1-04 core1 park/mode owner、M1-06 受限 Scratch intent 与 HIL。

## 任务记录

### FLASH-TASK-20260822-010 - Policy gate reason/temperature Vector 与 COM8 闭环

- 状态：M1-04 进行中；critical thermal/diagnostics fault 已在 admission 层细分并 fail closed，System/
  Calibration/TDMA mode policy 与实际 fault injection HIL 仍未完成。
- 日期：2026-08-22
- 完成内容：
  - `FlashTransactionFB` 增加可选 policy-check hook；兼容旧 bool policy，同时把 policy error 和
    temperature flags 写入 seqlock Vector，避免把 thermal、latched diagnostics fault 混成普通 raw failure。
  - AO policy 顺序固定为 thermal critical -> diagnostics fault -> requester/resource policy；warning 和
    current nominal-only 不阻断写入。
  - host fixture 分别注入 thermal critical 与 diagnostics fault，断言 erase/program/release 计数均为零，
    且终态 error、policy_gate_reason、temperature_flags 一致。
- 验证结果：
  - FlashTransaction host tests、全量 host runner `30/30`、release 与 RTOS+双核构建通过；代码提交
    `f0efc77 feat(flash): expose thermal policy gate reasons` 已推送。
  - COM8 `839E1AE79EA20F31` 使用 build `20260821174820` 完成统一 package 与 raw inactive-slot
    OTA、Boot/commit；`SYST:OTA:STAT?` 为 `COMMITTED`，`SYST:ERRor?` 为 `0,"No error"`。
    最后传感器快照板温 `31.633°C`、RP2350 内温 `36.403°C`、current frontend healthy、nominal
    `89 mA`、未校准；最后可读 transaction Vector 为 metadata `requester=2, partition=3,
    256/256 verified/committed, lockout=2/2`。
  - 原始闭环记录：`build/flash_policy_COM8/`、`build/flash_policy_raw_COM8/`；一次针对 metadata
    transaction 的旧 HIL 断言因 requester/partition 预期不匹配而失败，已保留在
    `build/flash_policy_lockout_COM8/`，不作为通过证据。
- 还需完成：
  - 增加可控板端 fault/thermal 注入或安全模拟入口后再做 negative HIL；接入 System/Trigger/
    Calibration/TDMA gate、core1 park owner 上移，再评估 M1-04 退出。

### FLASH-TASK-20260822-011 - Trigger/FAULT/资源 gate 原因细分与 COM8 烧录

- 状态：M1-04 进行中；FlashTransactionAO 已把 trigger activity、FAULT mode 和 Flash resource
  conflict 映射为独立 policy reason，Calibration/TDMA training 与真正 mode owner 仍待接入。
- 日期：2026-08-22
- 完成内容：
  - admission policy 在 thermal critical、diagnostics fault、非法 requester 之后读取
    `resource_arbiter_snapshot_t`，分别拒绝 FAULT mode、trigger capture/clock 活动和已有 Flash
    owner，避免把系统互斥误报为 raw erase/program 失败。
  - Vector 的 `policy_gate_reason` 与 `last_error` 保持同值；旧 bool policy callback 仍兼容，host
    fixture 覆盖 trigger/mode reason 且断言 erase 未调用。
- 验证结果：
  - FlashTransaction host fixture、release 与 RTOS+双核构建通过；代码提交
    `094d2cb feat(flash): classify trigger and mode gates` 已推送。
  - COM8 `839E1AE79EA20F31` 使用 build `20260821175703` 完成 unified package OTA、Boot/commit；
    最终 `SYST:OTA:STAT?="COMMITTED",1,"NONE",5`、active slot `2`、错误队列为空。最后 Vector
    为 metadata requester `2`/partition `3`、`256/256` verified/committed、lockout `2/2`；传感器
    快照板温 `31.069°C`、RP2350 内温 `35.934°C`、current frontend healthy、nominal `69 mA`、
    未校准。
- 还需完成：
  - 由 System/Calibration/TDMA 的真实 owner 发布 mode/training gate；补安全的板端拒绝 HIL，随后
    才能把 M1-04 从进行中推进到退出评审。

当前 live Bootloader、App linker、factory UF2、OTA partition 和 packager 均从 generated
`v1_compat` artifact 取得既有低 4 MiB 兼容布局，不再各自手写地址；
`config/flash_map_v2.json` 的目标分区仍未烧录或部署。App 已通过 OTA 部署 consumer gate 与只读
permission diagnostic；Boot 构建目标已链接同一服务，但板上 Bootloader 本轮没有重刷。

### FLASH-TASK-20260822-006 - OTA metadata App/Boot writer 边界与 COM8 transaction 闭环

- 状态：M1-03 进行中；App metadata erase/program 已接入 FlashTransactionAO，Boot metadata 保留
  独立 BootFlashService adapter；M3 BootControlStore 和 M2 durable store 仍未完成。
- 日期：2026-08-22
- 任务目标：
  - 拆开 `ota_metadata.c` 的共享语义与物理写后端，避免把 App AO/RTOS 依赖带入 Boot target。
  - 验证 OTA 的 mark-pending/confirm-active 元数据写入不会被旧 resource lock 自锁，且最终由
    transaction owner 完成 readback/commit。
- 完成内容：
  - 新增 `ota_metadata_flash.h` 边界及 App/Boot/read 三个 adapter；App adapter 将 metadata
    sector/page intent 交给 `FlashTransactionAO`，Boot adapter 仅调用 `drv_flash`，read adapter
    保持只读。
  - `ota_metadata.c` 不再直接调用 raw erase/program；raw inventory 将 Boot write 与 metadata
    read 分开登记，App raw write 只剩 FlashTransactionAO。
  - 移除 portable OTA `mark_pending/confirm_active` 外层 Flash resource lock，避免 transaction
    policy 在 owner 尚未 acquire 前被旧包装占用而自拒绝。
  - transaction policy 增加 `OTA_METADATA` requester，仅允许 generated Boot Control partition；
    host fixture 覆盖 metadata sector erase、错误 Product NVS partition 拒绝。
- HAOFV 边界：
  - 这是 writer owner/backend split，不是 M3 BootControlStore 完成；metadata 仍是现有双 copy
    sector rewrite，未引入 append/GC/power-cut matrix 或 durable completion journal。
  - Boot target 未链接 App `FlashTransactionAO`、resource arbiter 或 RTOS；未重刷 Bootloader，
    v2 map 仍为 `target_not_deployed`，没有高地址写入或任意地址 SCPI。
  - registry 中 Flash owner/BootControl 契约继续 `pending`，未进行 C11 状态激活。
- 验证结果（以下均为本次构建/HIL 快照，非长期事实源）：
  - 全量 host runner `30/30`、FlashTransaction metadata fixture、raw inventory `6` callers、
    release check/consumer gate、release 与 RTOS+双核构建通过。
  - 代码提交 `47c0a8b feat(flash): split app and boot metadata writers` 已推送；package build
    id `20260821172739`。
  - COM8 `839E1AE79EA20F31` OTA/Boot/commit 后，transaction Vector 为 requester `2`、partition
    `3`、operation `2`、`256/256` processed/verified、completion `4` committed、lockout
    `2/2`、last_error `0`；`SYSTem:OTA:STAT?` 为 `COMMITTED`，`SYSTem:ERRor?` 为 `0,"No error"`。
  - COM8 multicore smoke 为 `16/17`；core1/VDC/calibration/config/refmem/protection/error queue
    均通过，唯一失败为既有无合格 timestamp evidence 导致 DPLL `update_seq` stalled，未宣称
    DPLL 算法闭环。
- 板端证据与回退：
  - 原始报告位于 `build/ota_metadata_transaction_hil_COM8/`、
    `build/ota_metadata_transaction_vector_COM8.txt` 和 `build/ota_metadata_multicore_hil_COM8/`。
  - 板上保持 v1 Direct A/B、active slot 1、错误队列为空；另一个已验证槽与 BOOTSEL factory
    recovery 保留。M0-05 固定回退 runbook 仍未完成。
- 还需完成：
  - M3 BootControlStore：Boot-only durable backend、BCB lane/GC、fault matrix 和 reset journal。
  - M1-04 thermal/mode gate、owner 驱动 park handshake；M1-05 大 payload provider/refcount、
    duplicate completion/abort during raw operation；M2-02 Product NVS atomic store。
- 下一步：
  - 先补 App metadata transaction 的 negative/power-cut host fixtures，再进入 M2 Store core；
    继续禁止 v2 高地址在线写入。

### FLASH-TASK-20260822-007 - App raw write owner inventory gate

- 状态：M1-05 进行中；App raw erase/program 归属检查已加入 inventory，未宣称所有 buffer/lease
  与异步语义完成。
- 日期：2026-08-22
- 完成内容：
  - `tools/flash_map/flash_inventory.py` 现在对任何 `contexts` 含 `app` 且包含 raw erase/program
    的 caller 强制要求 `owner=FlashTransactionAO` 与 `target_api=FlashTransactionAO`。
  - 新增负向 Python fixture，证明 ProductConfig/FlashNVS 等业务 owner 的 App raw write 会被
    gate 拒绝；Boot context 的 `BootFlashService` 仍允许作为独立边界。
- 验证结果：
  - inventory 当前报告 `6` 个登记 caller；新增 owner gate fixture 与全量 host runner `30/30`
    通过，代码提交 `55140eb test(flash): gate app raw writes to transaction owner` 已推送。
  - 该 gate 与 release/consumer scan 一起执行，但仍需补 link-level symbol visibility、producer
    reset、duplicate completion 和 abort-during-raw-operation fixtures。
- 下一步：
  - 为 metadata/Product Config 补 power-cut/duplicate completion host model，再进入 M1-04 thermal
    gate 与 M2 Store core；继续禁止 v2 高地址在线写入。

### FLASH-TASK-20260822-008 - Raw write header visibility split

- 状态：M1-01/M1-05 进行中；raw write API 的 include 边界已收紧，运行时 owner/lease gate 仍继续。
- 日期：2026-08-22
- 完成内容：
  - 新增 `drivers/mcu/flash/inc/drv_flash_write.h`，将 `drv_flash_erase/program` 从通用
    `drv_flash.h` 移出；仅 driver implementation、BootFlashService、FlashTransactionAO 和 geometry
    fixture 显式 include 写头。
  - App/Boot release 构建继续通过，编译层面不再让只读业务通过通用 HAL 声明直接拿到 raw write。
- 验证结果：
  - geometry host tests、FlashTransaction host tests、release 与 RTOS+双核构建、Flash inventory
    `6` callers、consumer/release gate 均通过；代码提交 `9892768 refactor(flash): hide raw write API behind owner header` 已推送。
  - COM8 `839E1AE79EA20F31` 使用新 package build `20260821173547` 完成 OTA/Boot/commit；最终
    active slot 1、`BOARD:NO?=0`、错误队列为空。metadata transaction Vector 仍为 requester `2`、
    partition `3`、program、`256/256` verified/committed、lockout `2/2`。
  - 同次板端诊断快照为板温 `31.633°C`、RP2350 内温 `36.403°C`、current frontend healthy、
    nominal current `89 mA`、`current_calibrated=0`；仅作为诊断快照。
- 还需完成：
  - link-level symbol visibility、App metadata/Product Config power-cut fixtures、lease/refcount、
    duplicate completion、abort during raw page/sector 和 M1-04 thermal/mode gate。

### FLASH-TASK-20260822-009 - Transaction thermal critical gate 与 COM8 闭环

- 状态：M1-04 进行中；FlashTransactionAO 已接入 diagnostics fault/thermal critical fail-closed
  gate，尚未接入完整 System/Calibration/TDMA mode policy。
- 日期：2026-08-22
- 完成内容：
  - transaction policy 在新 intent admission 时读取 diagnostics seqlock sensor snapshot；board/chip
    critical thermal flags 或 latched diagnostics fault 拒绝新写，warning 不阻断。
  - 不改变 BootFlashService 边界、v1 compatibility map 或 SCPI 地址权限；现有 thermal flags 仍由
    `SYSTem:DIAGnostic:SENSors?` 只读暴露。
- 验证结果：
  - host runner `30/30`、FlashTransaction/geometry tests、release 与 RTOS+双核构建、inventory/
    consumer/release gates 通过；代码提交 `c0d32ec feat(flash): gate transactions on thermal faults` 已推送。
  - COM8 `839E1AE79EA20F31` 使用 build `20260821173948` OTA/Boot/commit 成功；最终 sensor flags
    无 thermal critical，板温 `31.391°C`、RP2350 内温 `36.403°C`、current frontend healthy，
    nominal current `69 mA`、未校准；transaction Vector 为 metadata requester `2`、partition `3`、
    `256/256` verified/committed、lockout `2/2`，错误队列为空。
- 还需完成：
  - negative HIL 注入 thermal critical/diagnostics fault，证明新 transaction 不执行 raw operation；
    接入 mode/trigger/calibration/TDMA gate 后再评估 M1-04 退出。

### FLASH-TASK-20260822-005 - Product Config intent 迁移与 COM8 持久化闭环

- 状态：M1-03 进行中；Product Config App writer 已迁移，OTA metadata、Boot writer 和 M2-02
  双副本/NVS 语义仍未完成。
- 日期：2026-08-22
- 任务目标：
  - 在保持 deployed `v1_compat` 与单板回退路径不变的前提下，把 Product Config 的 App
    erase/program 收敛到 `FlashTransactionAO`，不让业务域直接调用 raw write。
  - 固定一页小载荷的 provider 生命周期，并用 COM8 的 SCPI 写入、重启和回读证明 Product NVS
    intent 的 committed completion。
- 完成内容：
  - `FlashTransactionFB` 增加固定 program-page owned payload；submit 时复制调用方缓冲区，后续
    provider 修改不会改变实际写入内容。长度、分区、alignment 和 requester policy 继续 fail closed。
  - 新增 `PRODUCT_CONFIG` requester policy：只允许 generated
    `FLASH_COMPAT_MAP_PRODUCT_NVS_ID` 的 sector erase 与 page program；不依赖 active App slot。
  - `ProductConfigAO` 的 sector erase、page program 均通过 transaction execute API；readback 继续
    使用 raw read view。raw inventory 已将 Product Config 归类为 read-only reader，写 owner 收敛到
    `FlashTransactionAO`。
  - 逻辑板号 `0` 与代码已有的“未分配”默认语义对称：允许 Product Config 和 runtime identity
    清除板号，便于多板拓扑解绑和验证后恢复用户状态。
- HAOFV 边界：
  - 本切片仍是同步迁移桥，不宣称 M2-02 Product NVS 的 append/rotation、双副本、GC、wear 或
    power-cut atomicity；现有 single-sector rewrite 技术债保留并登记在 TODO。
  - 没有迁移 OTA metadata；App/Boot 共享 metadata raw writer 仍待独立 App transaction 与
    BootFlashService 边界，未向 Boot target 引入 App AO/RTOS 依赖。
  - v2 map 仍为 `target_not_deployed`，未写高地址、未重刷 Bootloader、未新增任意地址 SCPI。
- 验证结果（以下均为本次构建/HIL 快照，非长期事实源）：
  - FlashTransaction host tests、全量 host runner `30/30`、Flash inventory、release consumer、
    release check、RTOS+双核构建和文档门禁通过；定向文档/Flash Python 为 `18/18`。
  - 代码提交 `6252049 feat(flash): route product config through transaction owner` 和
    `6878ca9 fix(product): allow clearing logical board number` 已推送；release package build
    id 为 `20260821171708`。
  - COM8 `839E1AE79EA20F31` 先由 build `20260821171038` 写入 Product Config `BOARD:NO 7`，
    transaction Vector 快照为 requester `PRODUCT_CONFIG`、Product NVS partition、program、
    `256/256` processed/verified、completion committed、error `0`；重启后板号仍为 `7`。
  - 随后由 build `20260821171708` 完成 OTA/Boot/commit，再写入 `BOARD:NO 0`；transaction
    Vector 快照仍为 Product Config/Product NVS、`256/256` processed/verified、committed；再次
    软件重启后 `BOARD:NO?` 返回 `0`，`SYSTem:ERRor?` 清空为 `0,"No error"`。
  - 最终板端传感器快照：板温 `31.472°C`、RP2350 内温 `36.403°C`、current frontend healthy，
    nominal current `89 mA`，`current_calibrated=0`；这些是诊断快照，不是校准计量结论。
- 板端证据与回退：
  - 原始 transcript/report 位于 `build/product_config_transaction_boot_COM8/`、
    `build/product_config_transaction_pre_reboot_COM8.txt`、`build/product_config_transaction_post_write_COM8.txt`、
    `build/product_config_transaction_after_reboot_COM8.txt`、`build/product_config_clear_boot_COM8/`、
    `build/product_config_clear_write_COM8.txt` 和 `build/product_config_clear_after_reboot_COM8.txt`。
  - 板上最终保持原先未分配板号 `0`、active slot 1、build `20260821171708`；v1 Direct A/B 与
    BOOTSEL factory recovery 仍可回退。M0-05 固定回退 runbook 尚未完成。
- 还需完成：
  - Product NVS M2-02：versioned key、同值不写、append/rotation、GC、wear/power-cut fixtures
    和重启 HIL；本切片不把 sector rewrite 标记完成。
  - OTA metadata App/Boot backend split、BootFlashService、raw write header 可见性 gate；随后
    才能迁移 metadata writer。
  - M1-04 thermal/mode gate、真正 owner 驱动的 core1 park、异步 completion、large immutable
    provider/refcount 与跨 reset durable completion。
- 下一步：
  - 先建立 App metadata transaction backend 与 BootFlashService 的最小边界，保持 Boot target
    不依赖 App FlashTransactionAO；继续禁止 v2 高地址在线写入。

### FLASH-TASK-20260822-004 - FlashTransactionAO 首轮 OTA writer 与 COM8 双次闭环

- 状态：M1-03 进行中；完成 OTA image erase/program 首个生产 writer 迁移，未完成整个 App/Boot
  raw writer 收敛。
- 日期：2026-08-22
- 任务目标：
  - 在不改变 deployed `v1_compat` 地址、不写 v2 高地址的前提下，建立 HAOFV
    `FlashTransactionAO/FB/Vector`，让 OTA image 不再直接调用 Raw HAL。
  - 以 host fault fixtures 和 COM8 双次 OTA 证明 active-slot fail-closed、program/readback completion
    与 core1 lockout 闭环。
- 完成内容：
  - 新增 one-deep transaction queue、job/requester/operation/provider generation、abort、分级
    completion 和 seqlock Vector；FB 每次 service 只推进一个状态，终态统一 release owner。
  - policy 仅接受 OTA image 写 generated `FLASH_COMPAT_MAP_APP_A_ID/APP_B_ID` 中的非活动槽；
    active slot 从已校验 metadata 注入，未知状态、活动槽、跨分区、越界、未对齐和 provider 失效均
    fail closed。
  - `FlashTransactionAO` 以 owner name 申请 Flash resource，唯一执行 App OTA raw erase/program 和
    XIP readback verify；raw inventory 已把 portable OTA caller 替换为 transaction owner。
  - portable OTA 保留同步兼容包装，但只有 committed completion 才向上返回成功；metadata
    mark-pending/confirm 本轮仍使用既有 raw owner，Boot target 不链接 App transaction 组件。
  - 新增 `SYSTem:DIAGnostic:FLASh:TRANsaction?` 只读 Vector 查询、host fault runner，并扩展
    Flash lockout HIL 在 boot 前核对分区、进度、verify、completion、generation 和 lockout snapshot。
- HAOFV 边界：
  - 本轮 transaction policy 只消费 generated v1 compatibility partition；v2 继续
    `target_not_deployed`，没有在线重定位、Bootloader 重刷或高地址写入。
  - `PARK_CORE1` 是首轮 transaction 可观测状态，实际 park/ACK 仍由已审计 Raw HAL lockout
    closure 执行；将 park handshake 完全上移和 thermal/mode gate 属于 M1-04。
  - 当前 queue 为同步迁移桥，尚无 lease、大 payload immutable provider/refcount、跨 reset durable
    completion；因此 M1-03、M1-04 和 M1-05 均不能标记完成。
  - Flash registry 契约继续保持 `pending`，未进行 C11 status 变更。
- 验证结果（以下数字均为本次构建/HIL 快照，非长期事实源）：
  - FlashTransaction host fault fixtures 通过，覆盖 inactive-slot erase/program、active/unknown active、
    permission/range/alignment/provider、policy/resource/raw/verify failure、queue busy、abort 和 Vector
    generation；全量 host runner 为 30/30。
  - release 与 RTOS+双核构建、generated map freshness、raw inventory、Flash consumer/release gate、
    SCPI product list 和文档门禁通过。
  - 全量 Python 为 110/111；唯一失败仍是既有 reflection report 测试缺少
    `build-product-release/tdma_pio_timing_check_reflection_20260821.json`，未伪造台架产物。
  - 代码提交 `2a7964352d60b8c3a32bbb9dd16b2b090a55b482` 已推送；release package build id 为
    `20260821165051`，package SHA-256 为
    `854F162C761E4C73AC4B2511628D1DCE0DF9FFA5EEDF37D80D77800A435CFE06`。
  - COM8 `839E1AE79EA20F31` 先由旧 build bootstrap 到新 transaction 固件并 commit 至 slot 2；
    第二次由新固件向 inactive partition 1 执行 OTA，随后 boot/commit 成功。
  - 第二次写入期间 lockout request/ACK/release 从 2 同步增长到 937，timeout/release timeout 保持
    0；boot 前最后一个 transaction 为 OTA requester、APP_A program，256/256 processed/verified，
    completion=committed、error=none、transaction generation=933。
  - 板端 target map 为 14/14、permission access 260/260；Flash 定向双核/保护 smoke 为 5/5，
    core1 采样窗口增长 2011，最终错误队列为空。
  - 校准 PIO reference loopback 为 3/3，诊断快照 residence/raw path/delay estimate 为
    980/100/50 ns；该结果边界仍是 `REFERENCE_LOOPBACK + DIAGNOSTIC_ONLY`，不是 active calibration。
  - 通用 multicore 全项为 16/17；DPLL service 正常但无合格时间戳输入时 `update_seq` 保持 1。
    额外 VDC observer TX+RX 自检因 no-edge 返回 `last_error=4`，未宣称 DPLL 算法闭环成功；测试后
    已关闭 observer 并确认错误队列为空。
  - 板端 FlashMap 验证时板温/RP2350 内温为 31.875/37.339 degC；电流前端
    `healthy=1`、输出 1446520 uV、nominal estimate 79 mA，但 `current_calibrated=0`，不能作为计量值。
- 板端证据与回退：
  - 原始报告位于 `build/flash_transaction_bootstrap_COM8/`、
    `build/flash_transaction_hil_COM8_20260822/`、`build/flash_map_board_COM8_20260822/`、
    `build/multicore_flash_smoke_COM8_20260822/` 和 `build/calibration_loopback_COM8_20260822/`。
  - 代码可 revert `2a79643`；板端仍使用 v1 Direct A/B，另一个已验证镜像槽与既有 BOOTSEL
    factory 恢复路径保留。M0-05 固定回退 artifact/runbook 仍未完成。
- 提交与推送：
  - `2a79643 feat(flash): route OTA writes through transaction owner`
  - 代码提交已推送 `origin/feature/rtos-multicore-haofv`；本文档使用独立提交。
- 还需完成：
  - 把 metadata/Product Config App writer 迁移到 intent API，并为 Boot metadata 建立独立
    BootFlashService 边界；收敛 raw write header 可见性。
  - 将同步迁移桥改为 AO 异步 completion，补 lease/buffer/refcount、abort during raw page/sector、
    duplicate completion 和跨 reset durable completion。
  - 进入 M1-04，接入 System/Trigger/Calibration/TDMA/thermal gate，并把 park handshake owner
    边界完全上移；在接线/profile 匹配后单独完成 VDC/DPLL observer 算法 HIL。
- 下一步：
  - 优先拆分 App/Boot metadata writer 边界，再迁移 Product Config；继续禁止 v2 高地址写入。

### FLASH-TASK-20260822-003 - v1 compatibility live consumer 同源与 COM8 闭环

- 状态：完成 M0-02 和 M1-02 的 live consumer 子项；M1-02 总项继续进行中。
- 日期：2026-08-22
- 任务目标：
  - 消除 OTA header、三个 linker、factory UF2 和 OTA packager 中的 v1 地址副本，同时保持当前
    板卡可启动布局不变。
  - 明确隔离 deployed v1 compatibility map 与 target-not-deployed v2 map，禁止 App 在线搬迁。
- 完成内容：
  - 新增 `config/flash_map_v1_compat.json` 及 namespace 为 `FLASH_COMPAT_MAP_*` 的 generated
    header/manifest/CMake/linker artifact；生成器支持独立 symbol prefix/header guard。
  - CMake/preset 显式选择 `PROJECT_FLASH_DEPLOYMENT_MAP=v1_compat`，配置阶段拒绝其他 live map；
    OTA partition alias、三个 linker、factory UF2 address 和 Boot Control fill size 均消费生成符号。
  - OTA packager 强制读取 `deployed_compatibility` manifest，从 APP_A/APP_B partition 派生 run
    offset/capacity，拒绝 map state、partition shape 和 image overflow 异常。
  - 新增 `flash_consumer_check.py` 并接入 release gate，核对 source token、三份 ELF map、BIN
    capacity、OTA descriptor 和 factory UF2 block target；raw inventory 改为登记生成符号依赖。
- HAOFV 边界：
  - v1 compatibility 只描述当前部署事实，不允许新增 v1 分区功能；v2 仍保持
    `target_not_deployed`，迁移路径仍是 audited factory full erase/reflash。
  - FlashMap permission view 继续投影 v2 target policy，live boot/write consumer 使用 v1
    compatibility artifact；两者以 deployment state 和 symbol namespace 隔离，不能隐式互换。
  - 本轮真实写入只走既有 OTA inactive-slot owner 和 core1 lockout；未访问旧兼容边界以上区域，
    未重刷 Bootloader，未新增任意地址 destructive SCPI。
  - Flash registry 契约继续保持 `pending`；v2 factory migration、唯一 transaction owner、高地址
    Scratch HIL 和 C11 独立审核仍未满足。
- 验证结果（以下数值均为本次构建/HIL 快照，非长期事实源）：
  - generated v1/v2 freshness、raw inventory、`flash_consumer_check.py` 和 release gate 通过；release
    与 RTOS+双核构建均通过。
  - 三份 ELF map 的 FLASH origin/length 与 generated manifest 一致：Boot、App A、App B 仍为既有
    兼容布局；具体数值由 `FLASH_COMPAT_MAP_*_ORIGIN/LENGTH` 和构建 map 文件提供。
  - host C runner 为 29/29；本轮 Flash/OTA/release 定向 Python 为 29/29；全量 Python 为
    110/111。唯一失败仍是既有 `test_reflection_report_has_balanced_ladder`，因为本机缺少
    `build-product-release/tdma_pio_timing_check_reflection_20260821.json`，未伪造报告。
  - 代码提交 `a2111886f4feb51a8edcf787ce0c961b87feac4f` 已推送；release package build id 为
    `20260821162722`，package SHA-256 为
    `5E413A3A8B339C91A0955979D6423E29291A9D11163F402A4C7AC7EB19FC4233`。
  - COM8 `839E1AE79EA20F31` 从 active slot 2 向 inactive slot 1 完成 OTA、Boot 和 commit；最终
    slot snapshot 为 active/confirmed 1，错误队列为空。
  - 写入期间 lockout request/ACK/release 从 2 增长到 929，timeout/release timeout 保持 0，
    `last_result=1`，写入后临界区快照为 1007 us。
  - 板端 target map snapshot 为 14/14、permission access 为 260/260；定向 multicore smoke 为
    5/5，core1 采样窗口增长 2009。
  - OTA 前板温/RP2350 内温为 31.875/36.871 degC；闭环后为 31.633/36.871 degC。电流前端
    `healthy=1`，输出 1446520 uV，nominal estimate 为 79 mA，但 `current_calibrated=0`，该估算
    不能作为计量值。
- 板端证据与回退：
  - 原始报告位于 `build/flash_v1_compat_preflight_COM8_20260822.txt`、
    `build/flash_v1_compat_hil_COM8_20260822/`、`build/flash_v1_compat_map_COM8_20260822/` 和
    `build/flash_v1_compat_multicore_COM8_20260822/`。
  - 代码可 revert `a211188`；板端仍使用 v1 Direct A/B 和既有 BOOTSEL factory 恢复路径。
    M0-05 固定回退 artifact/runbook 未完成，不以本轮 OTA 成功替代该 gate。
- 提交与推送：
  - `a211188 feat(flash): derive live consumers from compatibility map`
  - 代码提交已推送 `origin/feature/rtos-multicore-haofv`；本文档使用独立提交。
- 还需完成：
  - 进入 M1-03，建立 FlashTransactionAO/FB/Vector 和唯一 App writer；生产 permission view 从可信
    active-slot provider 取上下文。
  - 完成 raw read/write header 可见性收敛、M1-04 mode/thermal gate 和 M1-06 high-address Scratch
    validation intent。
  - M0-05 固定 v1 factory 回退 artifact/runbook 后，才能开始 M4 的 v2 factory migration。
- 下一步：
  - 实现 M1-03 的 transaction 数据模型、状态机和 host fault fixtures；继续禁止 v2 在线部署。

### FLASH-TASK-20260822-002 - FlashMap permission view 与 COM8 只读闭环

- 状态：进行中。M1-02 的纯算法、generated context view 和测试子项已完成；live consumer 子项
  未完成。
- 日期：2026-08-22
- 任务目标：
  - 以 `config/flash_map_v2.json` 为唯一分区输入，实现 Boot/App/factory 的静态权限视图和 App
    active-slot/Scratch lease 动态规则。
  - 在不写 v2 高地址、不新增任意地址 Flash 命令的前提下，把算法接入固件并由 COM8 闭环验证。
- 完成内容：
  - 生成器新增 deployment state、executable flag 和 `FLASH_MAP_PARTITION_TABLE` X-macro；
    `flash_map.c` 不重复手写分区数字。
  - 新增 `flash_map_find()`、partition-relative range、operation permission 和 context view；零长度、
    越界、跨分区、非法 context/operation 均 fail closed。
  - App 动态规则拒绝活动 App 槽写入，只允许非活动槽写入，只允许活动槽执行；active slot 未知时
    写/执行均拒绝。Scratch 写入要求 lease，Future Pool 不授予任何权限。
  - App A/App B 与 Boot 构建目标链接同一 portable service；增加 `SYSTem:DIAGnostic:FLASh:MAP?`
    和 `SYSTem:DIAGnostic:FLASh:ACCEss?` 两个只读查询。命令不调用 Raw HAL，不执行 read/erase/
    program。
  - 新增 host C 测试、runner 和 `flash_map_board_validate.py`；板端工具从同一 JSON 推导期望值，
    同时保存原始 transcript、温度、电流前端、core1 与错误队列。
- HAOFV 边界：
  - FlashMap 仍是 Raw HAL 上方的纯策略服务，不拥有 Flash transaction，不成为业务 AO；实际 writer
    必须等待 M1-03 的唯一 FlashTransactionAO。
  - SCPI 的 active partition/context 参数只用于诊断算法矩阵，不是实际写权限来源；后续 writer 必须
    从可信 Boot/OTA 状态构造 access view。
  - map state 继续是 `target_not_deployed`；本轮 App OTA 只使用既有 v1 非活动槽，没有访问 v2
    Scratch/Future Pool，没有重刷 Bootloader。
  - `ARCH-FLASHMAP-01` 等 Flash 契约继续保持 `pending`，没有触发 C11 status 变更。
- 验证结果（以下数值均为本次构建/HIL 快照，非长期事实源）：
  - generated artifact `--check`、Flash inventory、SCPI namespace 和 release gate 通过；release 与
    RTOS+双核构建均完成 App A/App B/Boot 链接。
  - `run_host_unit_tests.ps1` 为 29/29；Flash/OTA/release 定向 Python 回归为 29/29。
  - 全量 Python 回归为 101/102；唯一失败仍是既有
    `test_reflection_report_has_balanced_ladder`，原因是本机缺少
    `build-product-release/tdma_pio_timing_check_reflection_20260821.json`，没有为通过测试伪造该
    TDMA 台架报告。
  - 代码提交 `10fd545c9d8654f21f7a6a58b6dd7162e9450764` 已推送；release package build id 为
    `20260821160431`，package SHA-256 为
    `0B7D94304E658643D2917B5DBBC13D550F0EE3E10E05D98ACCFA79593A57F1CB`。
  - COM8 `839E1AE79EA20F31` 完成 inactive-slot OTA、Boot 和 commit；active slot 从 1 切换到 2，
    最终 OTA 状态为 committed，transaction 全零。
  - 写入期间 lockout request/ACK/release 从 2 增长到 929，timeout/release timeout 均为 0，
    `last_result=1`，写入后的临界区快照为 1044 us。
  - 板端 map snapshot 为 14/14，permission access 为 260/260；专项验证覆盖活动/非活动 App、
    execute、Scratch lease、Future Pool、cross-partition、zero-length 和 unknown-active 拒绝。
  - 定向 multicore smoke 为 5/5，core1 采样窗口增长 2008，最终错误队列为空。
  - OTA 前板温/RP2350 内温为 31.553/36.403 degC；算法验证时为 31.633/36.871 degC。
    sensor flags 只有 current nominal-only；current front-end healthy，current estimate 未校准。
- 板端证据与回退：
  - 原始报告位于 `build/flash_map_com8_ota_20260822/`、
    `build/flash_map_com8_algorithm_20260822/` 和 `build/flash_map_com8_smoke_20260822/`。
  - 本轮变更为只读策略/诊断接入；代码可 revert `10fd545`，板端仍保留 v1 Direct A/B 与既有
    BOOTSEL factory 恢复路径。M0-05 要求的固定回退 artifact/runbook 仍未完成，不以本次 OTA
    成功替代该 gate。
- 提交与推送：
  - `10fd545 feat(flash): add generated permission view`
  - 代码提交已推送 `origin/feature/rtos-multicore-haofv`；本文档证据使用独立提交。
- 还需完成：
  - 让 live linker、factory builder、OTA packager 和 release size gate 消费 map artifact，并补独立
    linker drift fixture。
  - 为生产 writer 接入可信 active-slot provider；拆分 Raw read/write header，再建立 M1-03
    FlashTransactionAO/FB/Vector。
  - M1-03/M1-04 完成后才能增加受限 Scratch intent 和执行 M1-06 高地址破坏性 HIL。
- 下一步：
  - 先收口 M0-02/M1-02 live consumer 和 size gate，保持 v1 可回退构建；随后进入 M1-03 owner。

### FLASH-TASK-20260821-001 - FlashMap 输入、inventory 与 16 MiB Raw HAL 首轮迁移

- 状态：进行中。M0-01 已完成；M0-02 和 M1-01 仅完成首轮子项。
- 日期：2026-08-21
- 任务目标：
  - 按 Flash TODO 启动 `M0-01 -> M0-02 -> M1-01`，先建立机器可执行输入和边界门禁。
  - 保持 HAOFV owner 边界，不在 FlashTransactionAO/permission view 建立前切换在线分区或新增
    任意地址写接口。
- 完成内容：
  - 新增 `config/flash_raw_call_allowlist.json` 与 `flash_inventory.py`，登记 5 个生产 raw caller 的
    owner、context/core、mode、partition、频率、掉电语义和目标 API。
  - inventory 同时固定 v1 低 4 MiB 兼容边界、上 12 MiB 未分配状态，以及 OTA header、三个
    linker/factory 地址依赖；构建和 release gate 拒绝未登记 caller 或地址 token 漂移。
  - 新增 `config/flash_map_v2.json`、JSON schema 和生成器，生成 C header、规范化 manifest、
    CMake 地址常量和 linker 常量；source 明确标记 `target_not_deployed`。
  - map validator 检查 uint32/XIP overflow、erase/program geometry、对齐、隐式 gap、重叠、尾部、
    A/B 等长、execute/store/permission 和保留区权限。
  - `drv_flash.h` 删除独立 4 MiB 限制，total/sector/page/XIP 只引用生成式 geometry；当前
    `ota_partition.h`、live linker 和 factory 地址未改变。
  - 新增 Raw HAL host stub 和边界测试，并纳入全量 host unit test runner。
- HAOFV 边界：
  - 本轮只改变 Raw HAL 的物理范围认知，没有赋予业务 AO 新的分区权限。
  - App raw writer 仍是已登记技术债；后续由 `FlashTransactionAO` 收敛，不能把 allowlist 当成长期
    写权限。
  - `ARCH-FLASHMAP-01`、`ARCH-FLASHOWNER-01` 及其余 Flash 目标契约继续保持 `pending`。
- 验证结果：
  - FlashMap generated artifact check 通过：map version 2、state `target_not_deployed`、14 个分区、
    geometry 16 MiB。
  - raw inventory check 通过：5 个 caller、5 个 legacy address dependency、active map version 1。
  - FlashMap/inventory/release policy Python 定向测试 13/13 通过。
  - `run_drv_flash_geometry_tests.ps1` 通过；覆盖 zero length、last byte、one-byte overflow、
    `SIZE_MAX` wrap、unaligned、null 和 4 MiB 以上合法范围。
  - `run_host_unit_tests.ps1` 通过，28/28 个 host test script 完成。
  - `pico2-release` 构建、`release_check.py` 和 `pico2-rtos-multicore-smoke` 构建通过；两类构建均
    实际执行 FlashMap freshness 与 inventory gate。
  - COM8 通过 `flash_lockout_hil_validate.py` 完成 inactive-slot OTA、Boot/commit 和真实写入验证；
    build 从 `20260821130800` 升级为 `20260821154202`，active slot 从 2 切换并 committed 到 1。
  - Flash 写前后 `request_seq/ack_seq/release_seq` 均从 2 增长到 927，timeout/release timeout 为 0，
    `last_result=1`，最后一次写临界区耗时 1574 us。
  - COM8 重启后定向 smoke 5/5 通过：identity、build id、core1 heartbeat、runtime protection 和
    error queue；core1 loop 在采样窗口增加 2008。
  - OTA package SHA-256 为
    `64A1D8DC28AF415126001AEB9966F682BF9D0199F4B70EB6082B8511F1F935F9`。
  - 文档严格命名、doc regression、14 项文档单测和 pre-commit 全部通过。
  - 全量 Python 回归 101 项中 100 项通过；既有
    `test_reflection_report_has_balanced_ladder` 因本地缺少
    `build-product-release/tdma_pio_timing_check_reflection_20260821.json` 失败，与本轮 Flash 改动无关。
- 板端状态：
  - 已在 COM8 `839E1AE79EA20F31` 上通过现有 v1 OTA 路径执行真实 erase/program、Boot 和 commit；
    最终 `SYST:OTA:STAT?` 为 `"COMMITTED",2,"NONE",5`，transaction 全零，错误队列为空。
  - 烧录前板温/RP2350 内温约为 32.842/38.276 degC，烧录重启后约为
    32.923/37.808 degC；flags 只有 current nominal-only，无 thermal warning/critical。
  - 本轮未加载 factory UF2，未触碰 v2 高地址，也未把目标 map 标记为 deployed。
  - 原始证据位于 `build/flash_migration_com8_lockout_20260821/`、
    `build/flash_migration_com8_smoke_20260821/`、`build/flash_migration_com8_preflight.txt` 和
    `build/flash_migration_com8_postflight.txt`。
  - M1-06 高地址 Scratch HIL 必须等待 M1-02 permission view、M1-03 transaction owner 和受限
    validation intent 完成后执行。
- 提交与推送：
  - `315dc6f feat(flash): add map source and geometry gates`
  - `6e6fc5d docs(flash): record initial map migration evidence`
  - 两个提交均已推送 `origin/feature/rtos-multicore-haofv`。
- 还需完成：
  - 让 live linker、factory builder、OTA packager 和 release size gate 消费 map artifact。
  - 实现 M1-02 partition-relative permission view，并补 active App/cross-partition/Scratch lease 测试。
  - 拆分 raw read/write header，随后建立 M1-03 FlashTransactionAO/FB/Vector 和唯一 App writer。
  - 完成 M0-03 schema registry、M0-05 v1 回退 artifact/runbook，之后才具备 factory/HIL 迁移条件。
- 下一步：
  - 优先实现 M1-02 的只读 map table、context permission 和 host tests；不修改 live linker 地址。
