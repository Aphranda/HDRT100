# HAOFV_FLASH 任务进度归档

Status: Frozen
Domain: arch
Canonical: `docs/legacy/arch/LEGACY_HAOFV_FLASH_TASK_PROGRESS_01.md`
Related: `docs/arch/HAOFV_FLASH_TASK_PROGRESS.md`
Last updated: 2026-10-17

## 归档说明

由 C14 轮转从 `docs/arch/HAOFV_FLASH_TASK_PROGRESS.md` 迁入，按时间正序保存最旧条目；未删除、未改写。

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

### 文档接口

本文是实施证据日志，不是架构事实源，也不是工作板。稳定语义回到架构文档，子项状态回到 TODO；
本文件只追加任务编号、代码提交、构建/HIL 原始报告、失败、跳过、回退和阻塞，并通过任务编号回链
到 TODO。不得在本文件自行把契约状态从 `pending` 改成 `active`。

