# 任务进度追踪与回溯

本文档用于记录 RP2350_TRIG 工程的正式任务进度。每完成一个正式任务后，都应追加一条记录，说明任务目标、完成内容、验证结果、剩余工作和下一步计划，便于后续回溯设计决策和工程状态。

## 记录规则

- 每个正式任务使用独立编号：`TASK-YYYYMMDD-NNN`。
- 每条记录必须写明任务目标、完成内容、验证结果、剩余工作。
- 只记录已经进入工程方案或代码实现的正式任务，不记录临时讨论。
- 如果任务只完成了阶段性目标，状态应为 `进行中`，不能写成 `完成`。
- 如果任务依赖后续硬件验证，需要在验证结果中明确写出“仅完成编译验证”或“未做硬件验证”。
- 任务记录追加在“任务记录”章节顶部，保持最新任务在最前面。

## 状态定义

| 状态 | 含义 |
|---|---|
| `完成` | 当前任务目标已经达成，并完成必要验证。 |
| `进行中` | 已完成阶段性工作，但任务闭环还未完成。 |
| `阻塞` | 当前无法继续，需要外部条件、硬件、资料或决策。 |
| `暂停` | 暂时不推进，但不是技术阻塞。 |

## 记录模板

```markdown
### TASK-YYYYMMDD-NNN - 任务标题

- 状态：进行中 / 完成 / 阻塞 / 暂停
- 日期：YYYY-MM-DD
- 任务目标：
  - ...
- 完成内容：
  - ...
- 验证结果：
  - ...
- 还需完成：
  - ...
- 关联文件：
  - `path/to/file`
- 下一步：
  - ...
```

## 任务记录

### TASK-20260621-016 - OTA BOOT 复位闭环实机验证

- 状态：进行中
- 日期：2026-06-21
- 任务目标：
  - 在 watchdog reboot 修正后，验证 `SYST:OTA:BOOT` 是否能真正触发复位，并让设备重新启动回 App。
- 完成内容：
  - 重新首烧最新 `build/RP2350_TRIG_FACTORY.uf2` 后，通过 `COM4` 确认设备在线。
  - 初始查询结果为 `*IDN? -> RP2350_TRIG,SYNC_TRIGGER,0,RP2350_TRIG`。
  - 初始 OTA 状态为 `"IDLE",2,"NONE",0`。
  - 执行 `python tools/ota_send/ota_send.py COM4 build/RP2350_TRIG.bin`。
  - OTA payload 大小为 `69448` 字节，CRC32 为 `0x261061CB`。
  - OTA 状态正常经过 `CHECK_PERMISSION`、`ERASE_SLOT`、`RECEIVING`，最终进入 `"READY_TO_REBOOT",2,"NONE",2`。
  - 发送 `SYST:OTA:BOOT` 后，USB CDC 立即断开，PC 端读串口返回设备错误，说明复位动作已经触发。
  - 等待重新枚举后，`COM4` 恢复响应。
- 验证结果：
  - 复位后 `*IDN?` 正常返回 `RP2350_TRIG,SYNC_TRIGGER,0,RP2350_TRIG`。
  - 复位后 `SYST:OTA:STAT?` 返回 `"IDLE",2,"NONE",0`。
  - 复位后 `SYST:OTA:RES?` 返回 `0,"NONE"`。
  - OTA 传输、pending 标记、BOOT 复位、重新枚举、App 重新运行已实机跑通。
  - 当前 `SYST:OTA:SLOT?` 仍是占位查询，只返回 AO 默认目标 slot，尚不能作为严格 active/pending/confirmed slot 审计依据。
- 还需完成：
  - 完善 metadata 查询接口，使 `SYST:OTA:SLOT?` 返回真实 active、pending、confirmed 状态。
  - 增加 App 自检后的 `COMMIT` 机制。
  - 增加 Bootloader copy 结果、失败原因和 rollback 结果的可查询记录。
  - 增加构建版本号或固件 build id，用于 OTA 前后确认确实切换到新镜像。
- 关联文件：
  - `drivers/mcu/watchdog/src/drv_watchdog.c`
  - `components/ota_manager/src/ota_fb.c`
  - `bootloader/src/bootloader_main.c`
  - `middleware/scpi_port/src/scpi_port.c`
  - `docs/TASK_PROGRESS.md`
- 下一步：
  - 补齐 OTA 审计查询和 App commit/rollback 闭环，使 OTA 从“可升级”提升到“可审计、可回滚”的工业化状态。

### TASK-20260621-015 - OTA 传输成功与 BOOT 复位链路修正

- 状态：进行中
- 日期：2026-06-21
- 任务目标：
  - 在重新首烧 factory 镜像后，验证 OTA 传输和 `SYST:OTA:BOOT` 进入 Bootloader 的闭环。
- 完成内容：
  - 首烧后通过 `COM4` 确认 `*IDN?` 正常返回。
  - 确认 OTA 初始状态为 `"IDLE",2,"NONE",0`。
  - 执行 `python tools/ota_send/ota_send.py COM4 build/RP2350_TRIG.bin`。
  - OTA 镜像大小为 `69440` 字节，CRC32 为 `0x52A3DF79`。
  - 设备从 `CHECK_PERMISSION`、`ERASE_SLOT` 正常进入 `RECEIVING`。
  - 传输结束后状态进入 `"READY_TO_REBOOT",2,"NONE",2`。
  - 发送 `SYST:OTA:BOOT` 后，设备仍保持 `READY_TO_REBOOT`，未发生 USB 断开和复位。
  - 定位到 `drv_watchdog_reboot()` 调用 `watchdog_reboot()` 后立即返回，主循环继续喂狗，可能覆盖短延时复位窗口。
  - 修改 `drivers/mcu/watchdog/src/drv_watchdog.c`，在请求 watchdog reboot 后关闭中断并进入 `tight_loop_contents()` 等待复位。
- 验证结果：
  - OTA 传输、Flash 写入、CRC 校验、向量表校验和 pending metadata 写入均已实机通过。
  - `SYST:OTA:BOOT` 复位动作在当前板端旧固件中未生效。
  - `cmake --build --preset pico2-release` 编译通过，并已重新生成 `build/RP2350_TRIG_FACTORY.uf2`。
  - 新的 watchdog reboot 修正尚未完成板端首烧后的硬件验证。
- 还需完成：
  - 重新传统烧录最新 `build/RP2350_TRIG_FACTORY.uf2`。
  - 再次执行 OTA 发送。
  - 再发送 `SYST:OTA:BOOT`，确认 USB 重新枚举和 Bootloader copy-to-active。
  - 重启后查询 `*IDN?`、`SYST:OTA:STAT?`、`SYST:OTA:SLOT?`、`SYST:OTA:RES?`。
- 关联文件：
  - `drivers/mcu/watchdog/src/drv_watchdog.c`
  - `tools/ota_send/ota_send.py`
  - `components/ota_manager/src/ota_fb.c`
  - `docs/TASK_PROGRESS.md`
- 下一步：
  - 首烧最新 factory 镜像，复测 `SYST:OTA:BOOT` 是否能真正触发 Bootloader 升级。

### TASK-20260621-014 - OTA 擦除等待优化与实机状态恢复

- 状态：进行中
- 日期：2026-06-21
- 任务目标：
  - 解决 OTA `BEGIN` 后长期停留在 `ERASE_SLOT`，导致 PC 端发送脚本超时退出的问题。
- 完成内容：
  - 实机确认 LCD 界面已正常显示，说明 factory 镜像显示链路已可运行。
  - 使用 `COM4` 执行 `python tools/ota_send/ota_send.py COM4 build/RP2350_TRIG.bin`。
  - 发现板端长时间返回 `"ERASE_SLOT",2,"NONE",1`，脚本默认 15 s 超时退出。
  - 定位原因为 App 侧 OTA 每次擦除完整 Slot B，而不是按本次 `.bin` 镜像大小擦除。
  - 修改 `components/ota_manager/src/ota_fb.c`，将擦除范围改为 `BEGIN` 镜像大小按 4 KB sector 向上对齐。
  - 修改 `tools/ota_send/ota_send.py`，将默认 `BEGIN` 等待时间从 15 s 提高到 60 s。
  - 对当前板端发送 `SYST:OTA:ABOR`，将上一次未完成 OTA 从 `RECEIVING` 恢复到 `ABORTED`。
- 验证结果：
  - `cmake --build --preset pico2-release` 编译通过。
  - 已重新生成 `build/RP2350_TRIG_FACTORY.uf2`。
  - 当前板端状态已恢复为 `"ABORTED",2,"ABORTED",3`。
  - 由于板端尚未重新烧录新 factory 镜像，本次快速擦除优化还未完成硬件闭环验证。
- 还需完成：
  - 重新传统烧录 `build/RP2350_TRIG_FACTORY.uf2`。
  - 再次运行 `python tools/ota_send/ota_send.py COM4 build/RP2350_TRIG.bin`。
  - 发送 `SYST:OTA:BOOT`，验证 USB 重新枚举、Bootloader 复制 Slot B 到 Slot A、App 正常重启。
- 关联文件：
  - `components/ota_manager/src/ota_fb.c`
  - `tools/ota_send/ota_send.py`
  - `docs/TASK_PROGRESS.md`
- 下一步：
  - 首烧更新后的 factory 镜像后，复测完整 OTA 升级闭环。

### TASK-20260621-013 - OTA READY_TO_REBOOT 实机验证

- 状态：进行中
- 日期：2026-06-21
- 任务目标：
  - 使用 `COM4` 验证 Slot A 链接后的 OTA 镜像能否完成传输、写入、校验并进入可重启状态。
- 完成内容：
  - 构建确认 `build/RP2350_TRIG.bin`。
  - 通过 `COM4` 查询板端在线，初始 OTA 状态为 `"IDLE",2,"NONE",0`。
  - 使用 `tools/ota_send/ota_send.py` 发送 `build/RP2350_TRIG.bin`。
  - OTA 传输完成，最终状态进入 `"READY_TO_REBOOT",2,"NONE",2`。
  - 向量表校验已通过，上一轮 `VECTOR` 失败问题已解决。
- 验证结果：
  - `SYST:OTA:PROG?` 传输过程达到完整镜像进度。
  - `SYST:OTA:STAT?` 最终返回 `READY_TO_REBOOT`。
  - 发送 `SYST:OTA:BOOT` 后状态仍为 `READY_TO_REBOOT`，说明当前运行固件尚未包含新增 BOOT 事件复位处理，或尚未使用 factory Bootloader 首烧。
- 还需完成：
  - 先传统烧录 `build/RP2350_TRIG_FACTORY.uf2`，使 Bootloader 和 Slot A App 同时生效。
  - 再次通过 OTA 发送 `build/RP2350_TRIG.bin`。
  - 再发送 `SYST:OTA:BOOT`，验证 Bootloader 从 Slot B 复制到 Slot A 并启动。
- 关联文件：
  - `tools/ota_send/ota_send.py`
  - `components/ota_manager/src/ota_fb.c`
  - `bootloader/src/bootloader_main.c`
  - `build/RP2350_TRIG_FACTORY.uf2`
  - `build/RP2350_TRIG.bin`
- 下一步：
  - 执行 factory 首烧后复测完整 OTA reboot/复制闭环。

### TASK-20260621-012 - README Python 构建脚本说明

- 状态：完成
- 日期：2026-06-21
- 任务目标：
  - 在 README 中说明当前工程使用 Python 脚本参与构建和 OTA 的正确操作方式。
- 完成内容：
  - 明确固件构建入口仍然是 CMake，Python 脚本不是替代构建入口。
  - 补充 CMake 自动调用 `tools/uf2_join/uf2_join.py` 生成 factory UF2 的过程。
  - 补充手动复现 `uf2_join.py` 的命令。
  - 补充 `ota_bin_info.py`、`ota_send.py`、`ota_packager.py` 的使用场景说明。
  - 明确首次烧录使用 `RP2350_TRIG_FACTORY.uf2`，后续 OTA 使用 `RP2350_TRIG.bin`。
- 验证结果：
  - 文档更新完成。
- 还需完成：
  - 后续 OTA 发送工具支持自动发送 `SYST:OTA:BOOT` 后，再同步更新 README。
- 关联文件：
  - `README.md`
  - `tools/uf2_join/uf2_join.py`
  - `tools/ota_bin_info/ota_bin_info.py`
  - `tools/ota_send/ota_send.py`
- 下一步：
  - 进行 factory 首烧和完整 OTA 实机验证。

### TASK-20260621-011 - OTA Bootloader 与 Slot A 启动布局

- 状态：进行中
- 日期：2026-06-21
- 任务目标：
  - 解决 OTA `VECTOR` 校验失败，使固件具备真正升级启动路径。
- 完成内容：
  - 新增 Bootloader 目标 `RP2350_TRIG_BOOT`。
  - 新增 App Slot A 链接脚本，将主 App 链接到 `0x10040000`。
  - 新增 Bootloader 链接脚本，将 Bootloader 固定在 `0x10000000..0x1003FFFF`。
  - Bootloader 支持读取 metadata、校验 Slot B、复制 Slot B 到 Slot A、清 pending、跳转 Slot A。
  - 新增 factory 首烧包 `build/RP2350_TRIG_FACTORY.uf2`，包含 Bootloader 和 Slot A App。
  - OTA App 继续输出标准 raw `.bin`，用于 SCPI OTA 发送。
  - `SYST:OTA:BOOT` 现在会触发 watchdog reboot，进入 Bootloader 执行 pending 升级。
  - 新增 `tools/uf2_join/uf2_join.py` 生成多地址 UF2 factory image。
- 验证结果：
  - `cmake --build --preset pico2-release` 编译通过。
  - `build/RP2350_TRIG.elf.map` 确认 App `__vectors = 0x10040000`。
  - `build/RP2350_TRIG.bin` 前 16 字节确认 reset handler 指向 Slot A 地址。
  - 已生成 `build/RP2350_TRIG_FACTORY.uf2`。
  - 当前尚未完成板端 factory 首烧和完整 OTA reboot 实测。
- 还需完成：
  - 烧录 `RP2350_TRIG_FACTORY.uf2`。
  - 通过 `COM4` 发送 `RP2350_TRIG.bin`，确认 OTA END 后进入 `READY_TO_REBOOT`。
  - 发送 `SYST:OTA:BOOT`，确认 Bootloader 复制并启动新 App。
  - 后续增加 App 自检确认、失败回滚、签名校验和掉电恢复。
- 关联文件：
  - `bootloader/`
  - `linker/rp2350_bootloader.ld`
  - `linker/rp2350_app_slot_a.ld`
  - `tools/uf2_join/uf2_join.py`
  - `CMakeLists.txt`
  - `components/ota_manager/src/ota_fb.c`
  - `drivers/mcu/watchdog/`
- 下一步：
  - 执行 factory 首烧和完整 OTA 实机验证。

### TASK-20260621-010 - OTA USB CDC 实机传输验证

- 状态：进行中
- 日期：2026-06-21
- 任务目标：
  - 使用 `COM4` 验证当前 OTA USB CDC/SCPI 传输、分块写入、进度查询和结束校验链路。
- 完成内容：
  - 修正 `tools/ota_send/ota_send.py`，将 `SYST:OTA:BEGIN` 的 CRC 参数改为十进制，避免当前 SCPI 参数解析拒绝 `0x...`。
  - 增加 BEGIN 后等待 `RECEIVING` 状态的逻辑，避免目标槽擦除期间提前发送数据。
  - 增加日志行过滤，降低健康日志与 SCPI 响应共用 USB CDC 时的干扰。
  - 同步修正 `tools/ota_bin_info/ota_bin_info.py` 输出的 `scpi_begin` 示例。
  - 通过 `COM4` 实测发送 `build/RP2350_TRIG.bin`，大小 `69400` 字节，CRC32 `0x77CEA7A5`。
- 验证结果：
  - 板端进入 `RECEIVING` 状态。
  - `SYST:OTA:PROG?` 最终返回 `69400,69400,1000`，证明整包接收/写入流程完成。
  - `SYST:OTA:STAT?` 最终返回 `"FAILED",2,"VECTOR",4`。
  - 当前失败点为 OTA END 后的 App 向量表校验，不是 USB CDC 传输或分块写入失败。
- 还需完成：
  - 增加真正的 Bootloader。
  - 将 App 链接到 OTA Slot 运行地址，或明确实现 Bootloader copy-to-active 方案。
  - 完成 pending/boot/commit/rollback 闭环后再验证 `READY_TO_REBOOT`。
- 关联文件：
  - `tools/ota_send/ota_send.py`
  - `tools/ota_bin_info/ota_bin_info.py`
  - `components/ota_manager/src/ota_fb.c`
  - `components/ota_manager/src/ota_image.c`
  - `components/ota_manager/inc/ota_partition.h`
- 下一步：
  - 实现 Bootloader 与 OTA App 链接布局，解决 `VECTOR` 校验失败。

### TASK-20260621-009 - LCD 麻点显示问题修正

- 状态：进行中
- 日期：2026-06-21
- 任务目标：
  - 修正 UF2 烧录后 LCD 显示随机麻点的问题，恢复同步触发配置页的正常显示。
- 完成内容：
  - 修正 U8G2 单色帧缓存读取方式，从错误的横向 bit-packed 索引改为 U8G2 垂直页索引。
  - 增加 `BOARD_I2C_ENABLED` 开关，默认关闭 I2C 初始化，避免 GPIO8/9 与 LCD DC/CS 冲突。
  - 调整 ST7789 初始化状态标志，确保清屏阶段走正常 LCD 写入路径。
- 验证结果：
  - `cmake --build --preset pico2-release` 编译通过。
  - 当前仅完成编译验证，尚未完成重新烧录后的板端显示验证。
- 还需完成：
  - 重新烧录 `build/RP2350_TRIG.uf2`，确认 LCD 是否恢复正常。
  - 若仍有麻点，继续增加 LCD 纯色测试页，用于区分 SPI/LCD 初始化问题和 UI 渲染问题。
- 关联文件：
  - `components/sync_config_ui/src/sync_config_ui.c`
  - `boards/rp2350_trig/inc/board_config.h`
  - `boards/rp2350_trig/src/board.c`
  - `drivers/external/lcd/src/lcd_st7789.c`
- 下一步：
  - 进行板端复测，根据显示结果决定是否加入启动自检色条。

### TASK-20260621-008 - OTA 标准 bin 发送工具

- 状态：进行中
- 日期：2026-06-21
- 任务目标：
  - 增加 PC 端 OTA 发送工具，直接通过 SCPI USB CDC 发送标准 raw firmware `.bin`。
- 完成内容：
  - 新增 `tools/ota_send/ota_send.py`。
  - 工具自动计算 `.bin` size 和 CRC32。
  - 自动发送 `SYST:OTA:BEGIN`、`SYST:OTA:DATA #<block>`、`SYST:OTA:END`。
  - 支持 `--block-size`，当前限制为 256 或 512。
  - 支持 `--dry-run`，无硬件时可验证发送计划。
  - OTA 文档增加在线发送示例。
- 验证结果：
  - `python tools/ota_send/ota_send.py COM_TEST build/RP2350_TRIG.bin --dry-run` 执行通过。
  - `cmake --build --preset pico2-release` 编译通过。
  - 当前仍未做板端 USB CDC 实传验证。
- 还需完成：
  - 使用真实 USB CDC 端口做板端 OTA 实传。
  - 根据实测处理 SCPI 响应、超时、错误恢复和进度显示。
  - 增加发送完成后的状态判定和失败退出码。
- 关联文件：
  - `tools/ota_send/ota_send.py`
  - `docs/OTA方案.md`
- 下一步：
  - 执行 dry-run，并在硬件连接后做实传验证。

### TASK-20260621-007 - OTA 传输格式改为标准 raw bin

- 状态：完成
- 日期：2026-06-21
- 任务目标：
  - 取消自定义 `.ota` 后缀和自定义 OTA header，OTA 传输统一采用标准 raw firmware `.bin`。
- 完成内容：
  - 设备端 `OtaFB END` 阶段改为校验 `.bin` 整包 CRC、App 向量表和 pending metadata。
  - `BEGIN <size>,<crc32>` 的参数改为对应 `.bin` 文件本身。
  - 简化 `ota_image` 模块，仅保留 App 向量表校验。
  - 删除 `tools/ota_packager/ota_packager.py`。
  - 新增 `tools/ota_bin_info/ota_bin_info.py`，输出 `.bin` 的 size、CRC32 和 `SYST:OTA:BEGIN` 参数。
  - 同步更新 OTA、SCPI、架构文档中的 `.ota/header` 表述。
- 验证结果：
  - `cmake --build --preset pico2-release` 编译通过。
  - `python tools/ota_bin_info/ota_bin_info.py build/RP2350_TRIG.bin` 执行通过。
  - 当前未做板端 SCPI 实传验证。
- 还需完成：
  - 增加 `ota_send.py`，直接发送标准 `.bin`。
  - Bootloader 读取 metadata 后按标准 raw bin 校验启动。
  - SD 卡离线 OTA 使用 `/update/*.bin`。
- 关联文件：
  - `components/ota_manager/inc/ota_image.h`
  - `components/ota_manager/src/ota_image.c`
  - `components/ota_manager/src/ota_fb.c`
  - `tools/ota_bin_info/ota_bin_info.py`
  - `docs/OTA方案.md`
  - `docs/SCPI_COMMANDS.md`
  - `docs/HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE.md`
- 下一步：
  - 实现 `ota_send.py` 或 Bootloader 最小启动链路。

### TASK-20260621-006 - OTA raw bin 校验、Metadata 和工具

- 状态：进行中
- 日期：2026-06-21
- 任务目标：
  - 在 `OtaAO/OtaFB/OtaVector` 基础上，采用标准 raw firmware `.bin` 作为 OTA 传输格式，补齐 CRC 校验、metadata pending 标记和 PC 端信息工具。
- 完成内容：
  - OTA 输入格式改为标准 raw firmware `.bin`，不再使用自定义 `.ota` 后缀和自定义 OTA header。
  - 新增 raw `.bin` CRC 校验。
  - 新增 App 向量表校验，检查初始 SP、Reset Handler、Thumb bit 和运行地址范围。
  - 新增 `ota_metadata`，实现 metadata 双副本加载、sequence 选择、CRC 校验和 pending 写入。
  - `OtaFB END` 阶段增加 raw `.bin` CRC 校验、向量表校验和 pending metadata 写入。
  - 新增 `tools/ota_bin_info/ota_bin_info.py`，可输出 `.bin` 的 size、CRC32 和 `SYST:OTA:BEGIN` 参数。
- 验证结果：
  - `cmake --build --preset pico2-release` 编译通过。
  - `python tools/ota_bin_info/ota_bin_info.py build/RP2350_TRIG.bin` 可输出 OTA 传输参数。
  - 当前仅完成编译和工具信息验证，未做板端 SCPI OTA 实传验证。
- 还需完成：
  - 拆出 Bootloader 工程。
  - Bootloader 读取 metadata，校验 pending slot，跳转或回滚。
  - 实现 App commit。
  - 增加 `ota_send.py`，完成 USB CDC SCPI 发送闭环。
  - 增加掉电恢复测试和硬件验证。
- 关联文件：
  - `components/ota_manager/inc/ota_image.h`
  - `components/ota_manager/inc/ota_metadata.h`
  - `components/ota_manager/src/ota_image.c`
  - `components/ota_manager/src/ota_metadata.c`
  - `components/ota_manager/src/ota_fb.c`
  - `tools/ota_bin_info/ota_bin_info.py`
  - `docs/OTA方案.md`
- 下一步：
  - 实现 Bootloader 最小启动链路和 metadata 回滚策略。

### TASK-20260621-005 - 工程头文件目录统一为 inc

- 状态：完成
- 日期：2026-06-21
- 任务目标：
  - 将工程自有模块的头文件目录从 `include/` 统一改为 `inc/`，与 `src/` 并列。
- 完成内容：
  - 迁移 `application/`、`boards/`、`components/`、`drivers/`、`middleware/`、`osal/` 下的自有 `include/` 目录。
  - 保留第三方库目录结构，例如 `third_party/scpi-parser/libscpi/inc`。
  - 更新 `CMakeLists.txt`、`README.md` 和相关设计文档中的路径。
- 验证结果：
  - `cmake --build --preset pico2-release` 编译通过。
  - 工程自有目录下已无 `include/` 目录残留。
- 还需完成：
  - 根目录空 `inc/` 因系统权限未能删除，但不参与构建。
- 关联文件：
  - `CMakeLists.txt`
  - `README.md`
  - `docs/HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE.md`
  - `docs/OTA方案.md`
- 下一步：
  - 后续新增模块默认使用 `inc/` 和 `src/`。

### TASK-20260621-004 - OTA 第一阶段 App 侧骨架

- 状态：进行中
- 日期：2026-06-21
- 任务目标：
  - 按 HAOFV 架构实现 OTA App 侧基础骨架，使系统具备 SCPI 入口、状态查询、Slot B 写入和 CRC 校验基础。
- 完成内容：
  - 新增 `drivers/mcu/flash/`，封装 `drv_flash_read/erase/program/is_erased`。
  - 新增 `components/ota_manager/`，实现 `OtaVector/OtaAO/OtaFB`。
  - 实现 OTA 事件：`BEGIN`、`DATA_BLOCK`、`END`、`ABORT`、`TICK`。
  - 接入 `app_run_once()` 周期调用 `ota_ao_service()`。
  - 增加 SCPI `SYST:OTA:*` 基础命令。
- 验证结果：
  - `cmake --build --preset pico2-release` 编译通过。
  - 当前未做板端 Flash 写入实测。
- 还需完成：
  - 完整资源仲裁。
  - metadata 双副本。
  - Bootloader 交接。
  - 上位机发送工具。
- 关联文件：
  - `drivers/mcu/flash/`
  - `components/ota_manager/`
  - `middleware/scpi_port/src/scpi_port.c`
  - `application/src/app.c`
- 下一步：
  - 补齐 raw `.bin` 校验、metadata 和 PC 端工具。

### TASK-20260621-003 - OTA 方案细化到 HAOFV 架构

- 状态：完成
- 日期：2026-06-21
- 任务目标：
  - 按融合型主动对象功能块向量架构细化 OTA 方案。
- 完成内容：
  - 将 OTA 域拆分为 `OtaAO/OtaFB/OtaVector`。
  - 增加 `FlashJobFB/MetadataFB/ImageVerifyFB/BootHandoffFB` 设计。
  - 定义 OtaVector、OTA 事件模型、ECC 状态转移表、资源仲裁规则和 SCPI 映射。
  - 补充 Bootloader pending/commit/rollback 交接设计和错误码表。
- 验证结果：
  - 文档完成，未涉及编译。
- 还需完成：
  - 按方案逐步实现代码。
- 关联文件：
  - `docs/OTA方案.md`
  - `docs/SCPI_COMMANDS.md`
- 下一步：
  - 实现 OTA App 侧骨架。

### TASK-20260621-002 - 融合型主动对象功能块向量架构方案

- 状态：完成
- 日期：2026-06-21
- 任务目标：
  - 基于现有 PinProbe A1 方案和 RP2350_TRIG 需求，形成可扩展的中型嵌入式软件架构。
- 完成内容：
  - 定义 `Hybrid Active Object Function Block Vector Architecture`。
  - 明确上层 Active Object、中层轻量 IEC 61499 Function Block、底层 Time-Synchronized Vector Blackboard。
  - 保留 PIO/DMA/IRQ 作为硬实时旁路。
  - 明确 Resource Arbiter、Service Layer、表驱动状态机的职责。
- 验证结果：
  - 文档完成，未涉及编译。
- 还需完成：
  - 后续新增模块按该架构逐步落地。
- 关联文件：
  - `docs/HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE.md`
  - `README.md`
- 下一步：
  - 将 OTA 作为第一个 HAOFV 架构落地模块。

### TASK-20260621-001 - 同步触发系统当前基线

- 状态：进行中
- 日期：2026-06-21
- 任务目标：
  - 建立 RP2350 同步触发系统的 PIO IO 基线，并记录后续工业化待办。
- 完成内容：
  - 完成 `components/sync_io/` 的 PIO 输入采样、主触发输出、第二路脉冲输出、同步时钟、Marker 和 AUX IO。
  - 输出 `docs/PIO_RESOURCE_PLAN.md`。
  - 输出 `docs/SYNC_TRIGGER_TODO.md`。
- 验证结果：
  - 工程编译通过。
  - 未完成示波器和信号源台架验证。
- 还需完成：
  - 上层 `sync_trigger` 状态机。
  - DMA 环形缓冲。
  - 触发延时、burst、统计、错误码和硬件验证。
- 关联文件：
  - `components/sync_io/`
  - `docs/PIO_RESOURCE_PLAN.md`
  - `docs/SYNC_TRIGGER_TODO.md`
- 下一步：
  - 在 OTA 基础稳定后，回到 `sync_trigger` 上层状态机实现。
