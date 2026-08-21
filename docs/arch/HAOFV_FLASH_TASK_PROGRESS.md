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

| 工作包 | 状态 | 已有证据 | 下一 gate |
|---|---|---|---|
| M0-01 implementation inventory | 完成 | raw caller allowlist、旧地址依赖、构建/release scan gate | 后续新增 caller 必须先登记。 |
| M0-02 FlashMap source/schema | 完成 | v1 compatibility/v2 target 双版本 source、生成物、live consumer、artifact/drift gate | 后续 map 变更必须同时通过 freshness 与 consumer gate。 |
| M1-01 Geometry/Raw HAL | 进行中 | 16 MiB geometry、overflow-safe range、host boundary tests、COM8 v1 OTA/lockout HIL | raw write header 只对 BootFlashService/FlashTransaction target 可见。 |
| M1-02 permission view | 进行中 | generated X-macro、纯算法服务、版本化 live consumer、host 边界测试、COM8 OTA/只读权限闭环 | 真实 writer 接入、v2 factory 部署与 C11 激活审核。 |
| M1-03 FlashTransactionAO | 进行中 | one-deep queue/FB/Vector、OTA image 唯一 transaction owner、host fault tests、COM8 双次 OTA HIL | metadata/Product Config/Boot writer、异步 completion、lease/buffer、thermal gate 与 durable reset 语义。 |

当前 live Bootloader、App linker、factory UF2、OTA partition 和 packager 均从 generated
`v1_compat` artifact 取得既有低 4 MiB 兼容布局，不再各自手写地址；
`config/flash_map_v2.json` 的目标分区仍未烧录或部署。App 已通过 OTA 部署 consumer gate 与只读
permission diagnostic；Boot 构建目标已链接同一服务，但板上 Bootloader 本轮没有重刷。

## 任务记录

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
