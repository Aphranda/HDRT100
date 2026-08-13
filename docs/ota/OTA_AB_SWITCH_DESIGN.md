# OTA A/B 直接切换设计

Status: Active
Domain: OTA
Canonical: `docs/ota/OTA_AB_SWITCH_DESIGN.md`
Related: `docs/ota/OTA_SYSTEM_DESIGN.md`, `docs/ota/OTA_COPY_TRANSACTION_DESIGN.md`, `docs/ota/OTA_TODO.md`
Last updated: 2026-07-07

本文档定义 RP2350_TRIG 从当前 `copy-to-active` OTA 逐步演进到真正 A/B 直接启动的路线。目标是在不破坏当前可用 OTA 的前提下，先建立双 slot 镜像构建能力，再逐步切换 Bootloader、metadata、SCPI 和上位机工具。

## 当前状态

当前 OTA 流程：

```text
App 运行在 Slot A
OTA payload 写入 Slot B
Bootloader 校验 Slot B
Bootloader 将 Slot B 复制覆盖 Slot A
从 Slot A 启动
```

该方案已经通过 copy transaction 增强了掉电恢复，但升级时仍需要覆盖当前 active Slot A。真正 A/B 直接切换的目标是：新固件写入 inactive slot 后，Bootloader 直接从该 slot 启动，不再复制覆盖当前 active slot。

## 目标流程

```text
active = Slot A
OTA target = Slot B
写入并校验 Slot B
metadata.pending_slot = Slot B
Bootloader 校验 Slot B
metadata.active_slot = Slot B
跳转 Slot B
App 自检通过后 COMM
metadata.confirmed_slot = Slot B
```

下一次 OTA 反向进行：

```text
active = Slot B
OTA target = Slot A
写入并校验 Slot A
切换 active = Slot A
```

## 分阶段实施

### 阶段 1：双 slot 构建能力

- 新增 `linker/rp2350_app_slot_b.ld`。
- 构建 Slot A 镜像：`RP2350_TRIG_A.bin`，链接地址 `0x10040000`。
- 构建 Slot B 镜像：`RP2350_TRIG_B.bin`，链接地址 `0x101C0000`。
- 当前 `RP2350_TRIG.bin` 继续作为 Slot A 默认镜像保留，factory 行为不变。

验证标准：

- release/validation 均能构建 A/B 两个 App bin。
- Slot A factory 烧录后仍正常启动。
- 离线检查 Slot B bin 向量表 reset handler 位于 `0x101C0000..0x1033FFFF`。

### 阶段 2：metadata 增加 A/B 模式字段

已新增 A/B 专用扩展字段：

```c
uint32_t boot_mode;          // COPY_TO_ACTIVE or DIRECT_AB
uint32_t previous_slot;      // rollback source
uint32_t boot_generation;    // switch generation
uint32_t boot_capabilities;  // bootloader capabilities
uint32_t metadata_ab_crc32;  // A/B extension crc
```

这些字段追加在旧 copy transaction 扩展 CRC 之后，并使用独立 `metadata_ab_crc32` 校验，避免移动旧字段导致迁移期 Bootloader/App 对 metadata 的解释不一致。

默认值：

- `boot_mode = COPY_TO_ACTIVE`
- `previous_slot = NONE`
- `boot_generation = 0`
- `boot_capabilities = COPY_TO_ACTIVE`

该阶段只增加字段、默认值、校验和查询接口，不改变启动策略。

### 阶段 3：Bootloader 支持按 active_slot 启动

Bootloader 从固定 Slot A 跳转，扩展为受 `boot_mode` 控制的双路径：

- `COPY_TO_ACTIVE`：继续执行已验证的 Slot B -> Slot A copy-to-active 流程。
- `DIRECT_AB`：校验 `pending_slot` 对应运行地址，更新 `previous_slot/active_slot/boot_generation`，然后按 `active_slot` 直接跳转。

Direct A/B 分支逻辑：

```text
if pending_slot != NONE:
    validate pending_slot
    previous_slot = active_slot
    active_slot = pending_slot
    boot_attempts++
    clear pending
    jump active_slot
else:
    validate active_slot
    jump active_slot
```

如果 active slot 校验失败，则尝试 rollback 到 `confirmed_slot` 或 `previous_slot`。

当前状态：Bootloader 已具备 `DIRECT_AB` 分支和按 `active_slot` 跳转能力。新 release/factory 构建对空白 metadata 默认启用 `DIRECT_AB`；已有 metadata 保持原 boot mode，不通过普通 OTA 静默迁移。

### 阶段 4：OTA target 动态选择

App OTA 接收目标由固定 Slot B 扩展为按 `boot_mode` 选择：

- `COPY_TO_ACTIVE`：目标继续固定为 Slot B，镜像向量表按 Slot A 运行地址校验。
- `DIRECT_AB`：目标选择 inactive slot，镜像向量表按目标 slot 运行地址校验。

```text
active = Slot A -> target = Slot B
active = Slot B -> target = Slot A
```

使用双链接镜像时，上位机必须发送与目标 slot 匹配的 bin。

当前状态：App 侧已实现动态 target 选择；`DIRECT_AB` 作为新 factory 默认路径验证通过，`COPY_TO_ACTIVE` 兼容路径保留。

### 阶段 5：confirm / rollback

App 启动后如果 `active_slot != confirmed_slot`，说明处于待确认状态。自检通过后执行：

```text
SYST:OTA:COMM
confirmed_slot = active_slot
boot_attempts = 0
```

如果连续多次未确认，Bootloader 回滚到 confirmed slot。

当前实现策略：

- `pending_slot` 应用成功后，`active_slot = pending_slot`，`confirmed_slot` 暂不改变，`boot_attempts = 1`。
- 后续每次在 `active_slot != confirmed_slot` 的状态下复位启动，Bootloader 会增加 `boot_attempts`。
- 当 `boot_attempts >= BOOTLOADER_MAX_BOOT_ATTEMPTS` 时，Bootloader 回滚到 `confirmed_slot`，清零 `boot_attempts`，增加 `rollback_count`，并记录 `MAX_ATTEMPTS`。
- 若 factory 首烧后的 Slot A 尚无 `slot_a_size/slot_a_crc32`，回滚目标选择允许退化到向量表校验；已有 size/CRC 的 slot 仍执行 CRC 强校验。

实机验证结果：

- validation 固件完成 Slot A -> Slot B 未确认试运行，连续复位后回滚到 confirmed Slot A，随后可再次执行 direct A/B 升级并 `COMM`。
- 真实断电验证完成：
  - `READY_TO_REBOOT` 后断电再上电，Bootloader 正确应用 pending Slot B。
  - 未确认 Slot B 状态下连续真实断电，`boot_attempts` 按 1 -> 2 -> 3 增长。
  - 再次真实断电后回滚到 confirmed Slot A，`rollback_count` 增加，`last_boot_result = MAX_ATTEMPTS`。
  - 回滚后可继续执行 direct A/B OTA 并 `COMM`。

## SCPI 扩展建议

- `SYST:OTA:MODE?`：查询 `COPY_TO_ACTIVE` 或 `DIRECT_AB`。
- `SYST:OTA:TARG?`：查询下一次 OTA 应写入的目标 slot。
- `SYST:OTA:CAP?`：查询 Bootloader/OTA 能力。
- `SYST:BOOT:VERS?`：查询 Bootloader 版本。

## 工具链演进

上位机 OTA 工具应先查询目标 slot：

```text
SYST:OTA:TARG? -> A or B
```

然后选择对应镜像：

```text
target A -> RP2350_TRIG_A.bin
target B -> RP2350_TRIG_B.bin
```

当前 `tools/ota_send/ota_send.py` 已支持：

```powershell
python tools/ota_send/ota_send.py COM4 --auto-target `
  --image-a build-validation/RP2350_TRIG.bin `
  --image-b build-validation/RP2350_TRIG_B.bin
```

validation 固件支持 `SYST:OTA:MODE <0|1>` 切换 `COPY_TO_ACTIVE` / `DIRECT_AB`。release 固件仅保留查询接口，不提供模式写接口。

## 迁移策略

1. 新出厂 release/factory 默认启用 `DIRECT_AB`。
2. `COPY_TO_ACTIVE` 分支继续保留，用于已有 metadata 仍处于旧模式的设备。
3. validation 固件继续支持 `SYST:OTA:MODE <0|1>` 做 A/B 与 copy-to-active 台架验证。
4. 旧设备通过 factory 刷新或维护流程迁移，不通过普通 OTA 强制切换 boot mode。
