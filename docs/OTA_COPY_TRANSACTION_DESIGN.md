# OTA Copy Transaction 设计

Status: Active
Domain: OTA
Canonical: `docs/OTA_COPY_TRANSACTION_DESIGN.md`
Related: `docs/OTA_SYSTEM_DESIGN.md`, `docs/OTA_AB_SWITCH_DESIGN.md`, `docs/OTA_TODO.md`
Last updated: 2026-07-07

本文档定义当前 copy-to-active OTA 方案的掉电恢复增强设计。目标是在不改变现有 4 MB Flash 分区的前提下，降低 Bootloader 从 Slot B 复制到 Slot A 过程中掉电或写入失败导致设备不可启动的风险。

## 当前约束

- Flash：W25Q32，4 MB。
- Bootloader：`0x000000..0x03FFFF`，256 KB。
- Slot A：`0x040000..0x1BFFFF`，1.5 MB，当前运行 App。
- Slot B：`0x1C0000..0x33FFFF`，1.5 MB，OTA staging App。
- Metadata：`0x340000..0x34FFFF`，64 KB，双副本。
- Product config：`0x350000..0x35FFFF`，64 KB。
- Scratch：`0x360000..0x3FFFFF`，640 KB。

当前方案不是完整 A/B 启动，而是 Bootloader 校验 Slot B 后复制到 Slot A，再从 Slot A 启动。它的主要风险是：Slot A 被擦除或写入一半时掉电，下一次启动必须能继续完成复制，不能错误清除 pending。

## 设计目标

- 保持当前分区不变。
- 不在 copy 过程中清除 pending。
- copy 失败或掉电后，Bootloader 能根据 transaction 状态继续复制或重新开始复制。
- 只有 Slot A 校验通过后，才允许清除 pending 并记录 `APPLIED`。
- 不把软件故障注入等验证行为带入 release 构建。

## 非目标

- 本阶段不实现真正双 bank XIP 启动。
- 本阶段不做 Bootloader OTA。
- 本阶段不把 640 KB scratch 扩展成完整 1.5 MB active 备份，因为空间不足。
- 本阶段不引入签名校验；manifest/SHA/signature 单独设计。

## Copy Transaction 状态

建议在 OTA metadata 中增加 copy transaction 字段：

```c
typedef enum {
    OTA_COPY_TXN_NONE = 0,
    OTA_COPY_TXN_STARTED,
    OTA_COPY_TXN_ERASED_ACTIVE,
    OTA_COPY_TXN_PROGRAMMING,
    OTA_COPY_TXN_VERIFYING,
    OTA_COPY_TXN_DONE,
    OTA_COPY_TXN_FAILED,
} ota_copy_txn_state_t;
```

建议记录字段：

```c
uint32_t copy_txn_state;
uint32_t copy_source_slot;
uint32_t copy_destination_slot;
uint32_t copy_size;
uint32_t copy_crc32;
uint32_t copy_written;
uint32_t copy_attempts;
uint32_t copy_last_error;
uint32_t metadata_ext_crc32;
```

`metadata_ext_crc32` 用于保护 v3 之后的扩展字段。现有 `metadata_crc32` 保持 v2 前缀兼容，避免旧 Bootloader/App 无法识别基础 metadata。

## 状态转移

```text
pending Slot B valid
  -> COPY_STARTED
  -> COPY_ERASED_ACTIVE
  -> COPY_PROGRAMMING
  -> COPY_VERIFYING
  -> COPY_DONE
  -> APPLIED, clear pending
```

失败转移：

```text
COPY_STARTED / COPY_ERASED_ACTIVE / COPY_PROGRAMMING / COPY_VERIFYING
  -> reboot
  -> reload metadata
  -> if pending Slot B still valid: restart copy from erase-active
  -> if pending Slot B invalid: keep pending, record failure, do not claim APPLIED
```

只有以下条件同时满足时才能清除 pending：

- Slot A vector 校验通过。
- Slot A CRC 与 pending Slot B CRC 一致。
- Slot A size 与 pending image size 一致。
- transaction 到达 `COPY_DONE`。

## Bootloader 恢复规则

### 无 pending

- 校验 active Slot A。
- 如果 Slot A 有 `slot_a_size/slot_a_crc32`，优先使用完整 CRC 校验。
- 否则只做兼容性最小向量校验。
- 校验失败则停在 Bootloader，不跳转无效 App。

### 有 pending，transaction 为 NONE

- 校验 Slot B。
- 写入 `COPY_STARTED`。
- 从 Slot B 复制到 Slot A。

### 有 pending，transaction 为 STARTED/ERASED_ACTIVE/PROGRAMMING/VERIFYING

- 认为上次 copy 未完成。
- 重新校验 Slot B。
- Slot B 有效时，重新擦除 Slot A 并完整复制。
- 不依赖 `copy_written` 做断点续写，避免 page/sector 边界和部分写入校验复杂化。

### 有 pending，transaction 为 DONE

- 校验 Slot A。
- 校验通过后记录 `APPLIED`，更新 `slot_a_size/slot_a_crc32`，清 pending。
- 校验失败时回到重新复制流程，不清 pending。

### Slot B 无效

- 记录 `STAGE_VALIDATE_FAILED`。
- 如果 Slot A 仍有效，启动 Slot A。
- 不应记录 `APPLIED`。
- 是否清 pending 需要谨慎：release 建议保留 pending 和失败结果，等待上位机重新下发 OTA 或用户维护；validation 可提供清理命令。

## 对现有代码的改动状态

1. 已完成：`ota_metadata_t` 增加扩展 copy transaction 字段和扩展 CRC。
2. 已完成：新增 metadata API：
   - `ota_metadata_begin_copy_transaction()`
   - `ota_metadata_update_copy_transaction()`
   - `ota_metadata_finish_copy_transaction()`
   - `ota_metadata_fail_copy_transaction()`
   - `ota_metadata_clear_copy_transaction()`
3. 待完成：Bootloader `bootloader_apply_pending_image()` 不再直接在 copy 失败时清 pending。
4. 待完成：Bootloader 每个关键阶段写入 metadata，保证掉电后可判断上次处于哪个阶段。
5. 待完成：App `SYST:OTA:SLOT?` 或 `SYST:OTA:RES?` 后续可扩展查询 copy transaction 状态。

## 验证计划

- 正常 OTA：`NONE -> STARTED -> ERASED_ACTIVE -> PROGRAMMING -> VERIFYING -> DONE -> APPLIED`。
- copy 前掉电：重启后重新开始 copy。
- active 擦除后掉电：重启后重新擦除并复制 Slot B。
- programming 中掉电：重启后重新擦除并复制 Slot B。
- verifying 中掉电：重启后校验 Slot A，不通过则重新复制。
- Slot B 被破坏：不跳转坏 Slot A，不误报 `APPLIED`。
- 多次掉电：不超过设定尝试次数时持续可恢复；超过尝试次数后进入维护状态。

## 后续演进

长期更优方案是真正 A/B 启动：Bootloader 能直接启动 Slot A 或 Slot B，App 链接和中断向量支持 slot-independent 或双镜像链接。这样升级时不需要覆盖当前 active slot，掉电恢复能力更强。当前 copy transaction 是在现有分区和链接模型下的折中增强。
