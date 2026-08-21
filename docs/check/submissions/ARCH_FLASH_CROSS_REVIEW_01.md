# 核验提交单：Flash v2 → HAOFV 顶层

Status: Active
Domain: HAOFV / Flash / Documentation Governance
Canonical: `docs/check/submissions/ARCH_FLASH_CROSS_REVIEW_01.md`
Related: `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-08-21

## 提交内容

本次核验把硬件容量、构建配置、当前 Flash/OTA 实现和各 HAOFV 主域文档分层交叉，不以
目标架构反推当前代码已实现。核验覆盖 FlashMap、写 owner、Boot Control、统一 OTA stream、
RefMem/VDC 持久化和 PIO program catalog。

| 契约 | 符合性 | 层间证据 |
|---|---|---|
| `ARCH-FLASHMAP-01` | 偏差 | 产品器件与 `CMakeLists.txt::PICO_FLASH_SIZE_BYTES` 支持 16 MiB；`drv_flash.h` 与 `ota_partition.h` 仍保留低容量/v1 私有布局，尚无同源生成 FlashMap。 |
| `ARCH-FLASHOWNER-01` | 偏差 | `drv_flash_lockout.h` 已有 core1 park/ACK 基础；App 仍未形成唯一 `FlashTransactionAO`，Boot/App raw API 可见性也未收敛。 |
| `ARCH-BOOTCTRL-01` | 偏差 | `ota_metadata.h` 与现有 Bootloader 提供 Direct A/B/commit 基础；v2 双 lane append/commit、签名、security counter 和 Recovery 尚未闭环。 |
| `ARCH-OTASTREAM-01` | 偏差 | `ota_package.h` 与 USB/SD 工具已有 package/传输基础；TDMA durable ACK、credit、resume journal 和统一 session 尚未实现。 |
| `REFMEM-PERSIST-01` | 偏差 | `refmem_table_registry.h` 已有 staging/active registry；尚无 BlobStore package/ref、掉电回退和新 epoch 启动闭环。 |
| `VDC-PERSIST-01` | 偏差 | `vdc_domain.h` 已定义 VDC runtime；尚无独立 VDC NVS，且必须验证重启不恢复 LOCK/offset/rate。 |
| `ARCH-PIOCAT-01` | 偏差 | `tdma_pio_spi_phys.h` 已有 persona 与动态程序切换；尚无签名镜像 catalog、System Pack resource validation 和 executable blob 拒绝门禁。 |

## 主域持久化交叉核验

| 层 | 核验结论 |
|---|---|
| System/Trigger/Loop/SYNC_IO | 低频 identity、recipe、profile 和 catalog selection 可进入 NVS/System Pack；运行 mode、cursor、SM/DMA 状态必须保持易失。 |
| Calibration/TDMA/RefMem/VDC | accepted calibration、部署 package 和低频 discipline profile 可持久化；训练/runtime/epoch/lock 均不得直接恢复。 |
| Communication/Measure | 静态 adapter/channel profile 可持久化；FIFO、在线状态、raw capture 和波形进入 RAM/SD。 |
| OTA/Boot/Storage | BCB、inactive image、journal 与 active capsule 进入 Flash；完整统一 package/history 留在 source 或 SD。 |
| Diagnostics/UI | 低频关键故障和必要偏好可持久化；高频 trace、传感器流和页面瞬态不进板载 Flash。 |

## 偏差声明

- 以上 7 条契约均保持 `pending`。目标文档已冻结语义边界，但当前 v1 分区、driver 上限和
  OTA metadata 不能作为 v2 完成证据。
- 原 `TDMA-FLIGHT-BITMAP-01` 的多段主题不被现行 registry checker 识别；保留该历史行并
  标记 `superseded`，由语义相同的 `TDMA-FLIGHTBITMAP-01` 接替，契约内容和 pending 状态不变。
- v2 不做在线原地迁移。实现需先完成同源 FlashMap、factory/recovery 路径和高地址非破坏
  验证，再对样板执行 factory erase/reflash。
- `FUTURE_POOL` 维持未分配，主域需求优先复用已有 Store API；不得以本次矩阵为由立即切分
  新分区或扩大 OTA cache。

## Alternatives considered

- 为每个 HAOFV 主域建立私有 Flash 分区（拒绝：扩大 owner、GC、寿命和迁移耦合）。
- 在每个目标板缓存完整 A+B package（拒绝：重复占用容量，TDMA receiver 只需 inactive image object）。
- System Pack 携带任意 PIO 指令或 native plugin（拒绝：缺少签名模块 ABI、sandbox 和 rollback）。
- 统一 FlashMap + Store namespace + Deployment Capsule（接受：保持 HAOFV 唯一 writer 和语义分层）。

## 核验结论

- 结论: ACCEPT_WITH_DEVIATION
- 核验人: HAOFV architecture owner

## 交叉审核记录（C11，必填）

- 审核方: 产品网表/构建配置/Flash 驱动与 Boot-OTA-TDMA-RefMem-VDC 域 canonical 的层间交叉证据
- 审核方式: 文档交叉 + 层间交叉
- 审核结论: PASS_WITH_NOTE（目标语义一致；实现仍为 v1，7 条契约保持 pending）
- 审核日期: 2026-08-21
