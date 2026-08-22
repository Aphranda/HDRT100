# HAOFV Flash 域任务进度

Status: Active
Domain: HAOFV / Flash / OTA / Storage
Canonical: `docs/arch/HAOFV_FLASH_TASK_PROGRESS.md`
Related: `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`, `docs/arch/HAOFV_FLASH_TODO.md`, `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`
Last updated: 2026-08-22

本文记录 Flash v2 迁移已经发生的实现、验证、提交和剩余 gate。架构语义以
`HAOFV_FLASH_ARCHITECTURE.md` 为准，未完成项和依赖关系以 `HAOFV_FLASH_TODO.md` 为准；
本文不冻结新契约，也不以单次构建结果替代 TODO 完成定义。

## 记录规则

- 任务编号使用 `FLASH-TASK-YYYYMMDD-NNN`，最新记录放在顶部。
- 每条记录必须区分 source 已建立、live consumer 已迁移和板端已部署三种状态。
- 涉及 erase/program、分区切换或 factory artifact 时，必须记录目标板、map version、回退路径和
  HIL 原始报告；未烧录必须明确写出。
- 契约状态只以 `docs/check/DOCS_REGISTRY.md` 为准，进度记录不得自行把 `pending` 改写为
  `active`。
- 代码与文档提交分开记录；失败、跳过和环境依赖与通过项同等保留。

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
