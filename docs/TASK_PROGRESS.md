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

### TASK-20260622-006 - OTA 未处理评审项归档

- 状态：完成
- 日期：2026-06-22
- 任务目标：
  - 将 OTA 代码评审中尚未处理的风险项写入待办，避免后续遗漏。
- 完成内容：
  - 在 `docs/OTA_TODO.md` 中补充 Bootloader copy transaction 状态设计待办。
  - 补充 copy-to-active 失败时不应在 Slot A 可能损坏时直接清 pending 的待办。
  - 补充从 copy-to-active 演进到真正 A/B 启动或 active 备份/scratch 恢复机制的评估项。
  - 补充 metadata v3 扩展字段独立 CRC 或扩展区版本/CRC 的待办。
  - 补充 metadata schema 迁移规则待办。
  - 补充 SCPI OTA 写命令返回语义、同步等待模式、错误时序测试和 release 注入命令不可用验证。
- 验证结果：
  - 文档更新完成，未改动代码，未执行编译。
- 还需完成：
  - 后续按 `docs/OTA_TODO.md` 优先级逐项实现和验证。
- 关联文件：
  - `docs/OTA_TODO.md`
  - `docs/TASK_PROGRESS.md`
- 下一步：
  - 优先设计 copy-to-active 掉电恢复策略，再推进 manifest/metadata 扩展 CRC 和 SCPI 交互语义完善。

### TASK-20260622-005 - OTA 评审后优先级优化

- 状态：完成
- 日期：2026-06-22
- 任务目标：
  - 根据 OTA 代码评审结果，优先修复低风险但影响工业可靠性的状态约束和启动校验问题。
- 完成内容：
  - `SYST:OTA:COMM` 增加前置条件保护：
    - metadata 必须可读取。
    - `pending_slot` 必须为 `OTA_SLOT_NONE`。
    - Bootloader 最近结果必须为 `OTA_BOOT_RESULT_APPLIED`。
    - 不满足条件时返回 `OTA_ERR_INVALID_STATE`，避免误清 pending 或误写 `APPLIED` 审计。
  - Bootloader 普通启动前增加 active App 完整性校验：
    - metadata 中 `active_slot == OTA_SLOT_A` 且 `slot_a_size != 0` 时，使用 Slot A size/CRC 校验。
    - metadata 不完整时，保留最小向量校验作为首烧/历史状态兼容路径。
  - `docs/OTA_TODO.md` 标记上述两项保护已完成，保留 copy-to-active 真实掉电恢复为 P0 待办。
- 验证结果：
  - `cmake --build --preset pico2-release` 编译通过，生成 `build/RP2350_TRIG_FACTORY.uf2`，factory blocks 为 `315`。
  - `cmake --build --preset pico2-validation` 编译通过，生成 `build-validation/RP2350_TRIG_FACTORY.uf2`，factory blocks 为 `324`。
  - 当前板端只读查询正常：
    - `SYST:FW:BUILD? -> "20260621163006"`
    - `SYST:OTA:STAT? -> "IDLE",1,"NONE",0`
    - `SYST:OTA:SLOT? -> 1,0,1,0,1`
    - `SYST:OTA:RES? -> 0,"NONE","APPLIED",1,74040,3505617050`
- 还需完成：
  - 本轮未重新烧录新构建产物，因此上述保护目前完成编译验证，尚未完成板端实机验证。
  - copy-to-active 掉电恢复策略仍需后续设计和台架验证。
- 关联文件：
  - `components/ota_manager/src/ota_fb.c`
  - `bootloader/src/bootloader_main.c`
  - `docs/OTA_TODO.md`
  - `docs/TASK_PROGRESS.md`
- 下一步：
  - 下次烧录 release factory 后，验证错误时序下的 `SYST:OTA:COMM` 不会清 pending，并验证 Bootloader active App CRC 校验路径。

### TASK-20260622-004 - OTA 异常注入工业化收口

- 状态：完成
- 日期：2026-06-22
- 任务目标：
  - 异常注入路径验证完成后，按工业产品交付要求收口构建配置，避免调试/破坏性命令进入量产固件。
  - 将真实掉电验证写入后续待办，后续具备台架条件后再验证。
- 完成内容：
  - 将 `PROJECT_ENABLE_OTA_FAULT_INJECTION` 的 CMake 默认值改为 `OFF`。
  - `pico2-release` preset 显式关闭 `PROJECT_ENABLE_OTA_FAULT_INJECTION`。
  - 新增 `pico2-validation` preset，使用独立 `build-validation/` 目录并开启 `PROJECT_ENABLE_OTA_FAULT_INJECTION`，用于异常注入和台架验证。
  - 保留 `pico2-debug-uart` 开启异常注入，用于调试构建。
  - README 增加 validation 构建命令，并明确 release 构建不包含异常注入 SCPI。
  - SCPI 文档明确 `SYST:OTA:INJ:*` 仅存在于 validation/debug 构建，不开放给最终用户。
  - 新增 `docs/OTA_TODO.md`，记录发布构建隔离、掉电恢复验证、manifest/兼容性、发布验证报告和自动化工具待办。
- 验证结果：
  - `cmake --preset pico2-release` 配置通过。
  - `cmake --build --preset pico2-release` 编译通过，生成 `build/RP2350_TRIG_FACTORY.uf2`，factory blocks 为 `314`，符合 release 移除异常注入代码后的体积变化。
  - `cmake --preset pico2-validation` 配置通过。
  - `cmake --build --preset pico2-validation` 编译通过，生成 `build-validation/RP2350_TRIG_FACTORY.uf2`，factory blocks 为 `324`，保留异常注入验证代码。
- 还需完成：
  - 后续具备可控电源或继电器台架后，执行真实掉电恢复验证。
- 关联文件：
  - `CMakeLists.txt`
  - `CMakePresets.json`
  - `README.md`
  - `docs/SCPI_COMMANDS.md`
  - `docs/OTA_TODO.md`
  - `docs/TASK_PROGRESS.md`
- 下一步：
  - 使用 `pico2-release` 作为客户/量产固件构建入口；使用 `pico2-validation` 作为异常注入和掉电台架验证入口。

### TASK-20260622-003 - OTA 故障注入接口与剩余路径验证

- 状态：完成
- 日期：2026-06-22
- 任务目标：
  - 增加 OTA 故障注入接口，用于验证 metadata 双副本容错、Bootloader copy 失败路径和 OTA 审计恢复能力。
  - 继续验证 OTA 剩余失败路径，避免 pending、故障标志或 metadata 损坏污染后续升级。
- 完成内容：
  - 新增 `PROJECT_ENABLE_OTA_FAULT_INJECTION` CMake 选项，源码默认关闭，当前 `pico2-release`/`pico2-debug-uart` preset 显式开启用于台架验证。
  - OTA metadata 升级到 v3，增加 `fault_injection_flags`，并保持 v2 前缀 CRC 兼容，避免旧 Bootloader 因 metadata 扩展无法读取 pending。
  - 新增 v2 metadata 读取/升级路径，保留 active/pending/confirmed/rollback/boot result 等审计字段。
  - 新增 SCPI 调试命令：
    - `SYST:OTA:INJ:COPY`
    - `SYST:OTA:INJ:COPY?`
    - `SYST:OTA:INJ:CLEAR`
    - `SYST:OTA:INJ:MCOR <0|1>`
    - `SYST:OTA:INJ:MREP`
  - Bootloader 增加 copy 失败注入分支，命中后记录 `COPY_FAILED`、清除 pending、保留旧 App。
  - README 和 SCPI 文档补充故障注入命令、用途和量产关闭要求。
  - 执行 App OTA，将板端更新到带故障注入 SCPI 的版本，查询 `SYST:OTA:INJ:COPY? -> 0`。
  - 执行 metadata 单副本损坏测试：
    - `SYST:OTA:INJ:MCOR 0 -> "OK"`
    - `SYST:OTA:SLOT? -> 1,0,1,0,0`
    - `SYST:OTA:INJ:MREP -> "OK"`
    - `SYST:OTA:INJ:COPY? -> 0`
  - 尝试 copy 失败注入测试，确认当前板端 Bootloader 仍为旧版本，未执行注入分支。
  - 测试结束后清理状态：
    - `SYST:OTA:INJ:CLEAR -> "OK"`
    - `SYST:OTA:COMM -> "OK"`
    - `SYST:OTA:SLOT? -> 1,0,1,0,0`
    - `SYST:OTA:INJ:COPY? -> 0`
  - 传统烧录最新 `build/RP2350_TRIG_FACTORY.uf2` 后，确认新 Bootloader/App 已生效：
    - `SYST:FW:BUILD? -> "20260621162038"`
    - `SYST:OTA:STAT? -> "IDLE",1,"NONE",0`
    - `SYST:OTA:SLOT? -> 1,0,1,0,0`
    - `SYST:OTA:RES? -> 0,"NONE","NONE",0,0,0`
    - `SYST:OTA:INJ:COPY? -> 0`
  - 执行 copy 失败注入闭环：
    - `SYST:OTA:INJ:CLEAR -> "OK"`
    - `SYST:OTA:INJ:COPY -> "OK"`
    - `SYST:OTA:INJ:COPY? -> 1`
    - `python tools\ota_send\ota_send.py COM4 build\RP2350_TRIG.bin`
    - OTA payload 大小 `74040` 字节，CRC32 `0xD39E02C3`，进入 `"READY_TO_REBOOT",2,"NONE",2`
    - `SYST:OTA:BOOT` 后重启，查询 `SYST:OTA:RES? -> 0,"NONE","COPY_FAILED",2,74040,3550347971`
    - 查询 `SYST:OTA:SLOT? -> 1,0,1,0,1`，确认 pending 已清除，rollback 计数增加。
    - 查询 `SYST:FW:BUILD? -> "20260621162038"`，确认故障注入没有切换运行镜像。
    - 查询 `SYST:OTA:INJ:COPY? -> 0`，确认 Bootloader 已清除故障注入标志。
  - 执行失败后的正常 OTA 恢复：
    - 重新构建并发送 OTA，payload 大小 `74040` 字节，CRC32 `0xD0F3789A`。
    - `SYST:OTA:BOOT` 后查询 `SYST:FW:BUILD? -> "20260621162338"`。
    - 查询 `SYST:OTA:RES? -> 0,"NONE","APPLIED",2,74040,3505617050`。
    - 执行 `SYST:OTA:COMM -> "OK"`。
    - 最终状态 `SYST:OTA:STAT? -> "COMMITTED",1,"NONE",5`。
- 验证结果：
  - `cmake --build --preset pico2-release` 编译通过，已生成新的 `build/RP2350_TRIG_FACTORY.uf2`。
  - App 侧故障注入 SCPI 命令实机可用。
  - metadata 单副本擦除后，另一副本可维持 OTA 状态读取，`MREP` 可恢复双副本。
  - 当前板端最终状态无 pending、无故障注入标志残留。
  - Bootloader copy 失败注入已完成实机闭环，验证了 `COPY_FAILED` 审计、pending 清除、故障标志清除、旧 App 保留和 rollback 计数增加。
  - copy 失败后再次执行正常 OTA 可成功恢复，Bootloader 记录 `APPLIED`，App 可 `COMMIT`。
- 还需完成：
  - 补充真实断电/掉电台架测试，覆盖 copy 过程中断电的物理故障。
- 关联文件：
  - `CMakeLists.txt`
  - `CMakePresets.json`
  - `config/project_config.h`
  - `components/ota_manager/inc/ota_metadata.h`
  - `components/ota_manager/src/ota_metadata.c`
  - `bootloader/src/bootloader_main.c`
  - `middleware/scpi_port/src/scpi_port.c`
  - `README.md`
  - `docs/SCPI_COMMANDS.md`
  - `docs/TASK_PROGRESS.md`
- 下一步：
  - 后续如具备可控电源台架，执行真实断电恢复测试；当前软件故障注入路径已闭环。

### TASK-20260622-002 - OTA 传输中止路径测试与恢复

- 状态：完成
- 日期：2026-06-22
- 任务目标：
  - 验证 OTA 传输过程中主动中止时，不会写入 pending metadata，也不会改变当前运行固件。
- 完成内容：
  - `tools/ota_send/ota_send.py` 新增 `--abort-after-blocks <N>`。
  - 工具在发送指定数量数据块后自动发送 `SYST:OTA:ABOR`，并查询最终状态。
  - README 增加传输中止测试命令示例。
  - 执行 `python tools\ota_send\ota_send.py COM4 build\RP2350_TRIG.bin --abort-after-blocks 8 --expect-final-state ABORTED`。
  - 半包中止后查询状态：
    - `SYST:OTA:STAT? -> "ABORTED",2,"ABORTED",3`
    - `SYST:OTA:SLOT? -> 1,0,1,0,0`
    - `SYST:OTA:RES? -> 3,"ABORTED","APPLIED",1,71200,993826160`
    - `SYST:FW:BUILD? -> "20260621155512"`
  - 随后执行一次正常 OTA 恢复到最新 payload。
  - 重启后查询 `SYST:FW:BUILD? -> "20260621155934"`。
  - Bootloader 审计为 `SYST:OTA:RES? -> 0,"NONE","APPLIED",2,71200,4087555981`。
  - 执行 `SYST:OTA:COMM` 后进入 `COMMITTED`。
- 验证结果：
  - 传输中止不会产生 pending。
  - 传输中止不会改变运行固件 build id。
  - 设备可从 `ABORTED` 状态重新执行正常 OTA 并成功恢复。
  - 正常 OTA 后 Bootloader copy-to-active 和 COMMIT 仍通过。
- 还需完成：
  - metadata 双副本损坏测试需要增加专用调试命令或离线 Flash 修改工具。
  - Bootloader copy 过程中断电需要硬件电源控制或人工断电台架。
  - copy 失败路径需要故障注入机制，例如调试版 Bootloader 强制失败开关。
- 关联文件：
  - `tools/ota_send/ota_send.py`
  - `README.md`
  - `docs/TASK_PROGRESS.md`
- 下一步：
  - 设计 OTA 故障注入接口，用于 metadata 损坏、Bootloader copy 失败和断电恢复测试。

### TASK-20260622-001 - OTA 向量表失败路径测试

- 状态：完成
- 日期：2026-06-22
- 任务目标：
  - 构造 CRC 正确但 App 向量表错误的 OTA payload，验证设备能在向量表校验阶段拒绝镜像。
- 完成内容：
  - `tools/ota_send/ota_send.py` 新增 `--corrupt-vector`。
  - `--corrupt-vector` 仅在内存中将 reset handler 向量清零，不修改磁盘上的 `.bin` 文件。
  - CRC 按破坏后的 payload 重新计算，因此设备端 CRC 校验可以通过，错误应落在 `VECTOR` 校验阶段。
  - dry-run 可显示 `corrupt_vector=reset_handler_zero`。
  - README 增加向量表失败路径测试命令示例。
- 验证结果：
  - 执行 `python tools\ota_send\ota_send.py COM4 build\RP2350_TRIG.bin --corrupt-vector --expect-final-state FAILED` 实机通过。
  - 最终状态返回 `"FAILED",2,"VECTOR",4`。
  - 查询 `SYST:OTA:SLOT? -> 1,0,1,0,0`，确认未写入 pending。
  - 查询 `SYST:OTA:RES? -> 4,"VECTOR","APPLIED",1,71200,993826160`。
  - 查询 `SYST:FW:BUILD? -> "20260621155512"`，确认运行固件未切换。
- 还需完成：
  - 增加传输中断/用户 abort 测试。
  - 增加 metadata 双副本损坏测试。
  - 增加 Bootloader copy 失败和断电恢复测试。
- 关联文件：
  - `tools/ota_send/ota_send.py`
  - `README.md`
  - `docs/TASK_PROGRESS.md`
- 下一步：
  - 实现传输中断和 abort 路径测试，确认未完成镜像不会进入 pending。

### TASK-20260621-024 - OTA CRC 失败路径测试

- 状态：完成
- 日期：2026-06-21
- 任务目标：
  - 增加 OTA 发送工具的失败路径测试能力，并验证 CRC 错误镜像不会进入 pending 或触发升级。
- 完成内容：
  - `tools/ota_send/ota_send.py` 新增 `--corrupt-crc`，可故意发送错误 CRC32。
  - 新增 `--expect-final-state`，用于断言最终 OTA 状态并返回正确退出码。
  - 新增最终状态解析，默认期望 `READY_TO_REBOOT`。
  - dry-run 支持显示真实 CRC 和故意发送的错误 CRC。
  - 当前板端从 `COMMITTED` 状态手动 `SYST:OTA:ABOR` 切到 `ABORTED` 后执行 CRC 失败测试。
  - 发送错误 CRC payload，最终返回 `"FAILED",2,"CRC",4`。
  - 查询 `SYST:OTA:SLOT? -> 1,0,1,0,0`，确认未写入 pending。
  - 查询 `SYST:OTA:RES? -> 4,"CRC","APPLIED",1,71200,993826160`。
  - 修改 OTA 状态机，允许从 `COMMITTED` 直接开始下一次 OTA，不再要求先手动 `ABOR`。
  - README 增加 CRC 失败路径测试命令示例。
- 验证结果：
  - `python tools\ota_send\ota_send.py COM4 build\RP2350_TRIG.bin --corrupt-crc --expect-final-state FAILED` 实机通过。
  - CRC 错误镜像未污染 pending metadata。
  - 运行固件 build id 保持 `"20260621155512"`，未发生升级切换。
  - `cmake --build --preset pico2-release` 编译通过。
- 还需完成：
  - 使用新固件验证 `COMMITTED` 状态可以直接进入下一次 OTA。
  - 增加向量表错误测试。
  - 增加 metadata 双副本损坏和 Bootloader copy 失败测试。
- 关联文件：
  - `tools/ota_send/ota_send.py`
  - `components/ota_manager/src/ota_fb.c`
  - `README.md`
  - `docs/TASK_PROGRESS.md`
- 下一步：
  - 实现向量表错误 payload 测试，验证 `OTA_ERR_VECTOR` 路径。

### TASK-20260621-023 - Build ID OTA 前后变化实机验证

- 状态：完成
- 日期：2026-06-21
- 任务目标：
  - 验证每次构建刷新的 build id 是否真正进入 OTA payload，并能在 OTA 后通过 `SYST:FW:BUILD?` 识别新镜像。
- 完成内容：
  - 首烧后查询板端 `SYST:FW:BUILD? -> "dev"`，发现 factory 固件没有包含生成的 build info。
  - 定位原因为 App include path 缺少 `build/generated`，`project_config.h` 未找到 `project_build_info.h`，走了 fallback `"dev"`。
  - 修正 `CMakeLists.txt`，将 `${CMAKE_CURRENT_BINARY_DIR}/generated` 加入 App include path。
  - 重新构建后确认 `build/generated/project_build_info.c` 中 build id 为 `20260621155512`。
  - 通过 OTA 发送新 `build/RP2350_TRIG.bin`，payload 大小 `71200` 字节，CRC32 `0x3B3C9570`。
  - 发送 `SYST:OTA:BOOT` 后 USB CDC 断开并重新枚举。
  - 重启后查询 `SYST:FW:BUILD? -> "20260621155512"`。
  - 查询 `SYST:OTA:RES? -> 0,"NONE","APPLIED",2,71200,993826160`。
  - 执行 `SYST:OTA:COMM`，返回 `"OK"`，状态进入 `COMMITTED`。
- 验证结果：
  - build id 已确认随 OTA payload 进入运行固件。
  - OTA 前后可通过 `SYST:FW:BUILD?` 判断运行镜像是否已切换。
  - 当前 App build id 路径、OTA 传输、BOOT、APPLIED 审计、COMMIT 均实机通过。
- 还需完成：
  - 最新 factory 已修正 build id include path，后续新板首烧应直接返回真实 build id。
  - 发布流程后续可扩展 git commit hash、dirty 标志、构建机/渠道信息。
  - 进入失败路径测试：CRC 错误、向量表错误、metadata 双副本损坏、copy 过程中断电。
- 关联文件：
  - `CMakeLists.txt`
  - `tools/build_info/gen_build_info.py`
  - `config/project_config.h`
  - `middleware/scpi_port/src/scpi_port.c`
  - `docs/TASK_PROGRESS.md`
- 下一步：
  - 开始 OTA 失败路径和掉电恢复测试。

### TASK-20260621-022 - Build ID 每次构建刷新

- 状态：完成
- 日期：2026-06-21
- 任务目标：
  - 解决 `SYST:FW:BUILD?` 仅在 CMake configure 阶段刷新的问题，使每次生成 OTA payload 时都有新的 build id。
- 完成内容：
  - 新增 `tools/build_info/gen_build_info.py`。
  - 构建脚本每次生成 `build/generated/project_build_info.h` 和 `build/generated/project_build_info.c`。
  - `project_build_info.c` 定义 `g_project_build_id`，由 App 编译链接进固件。
  - `SYST:FW:BUILD?` 改为返回 `g_project_build_id`。
  - CMake 增加 `project_build_info` target，并将生成的 `.c` 加入 App 目标。
  - 移除 configure 阶段 `PROJECT_BUILD_ID` 编译宏。
  - 更新 README 和 SCPI 文档。
- 验证结果：
  - 第一次 `cmake --build --preset pico2-release` 生成 build info 并重新编译 App。
  - 第二次连续构建仍会重新运行 build info 生成脚本、重新编译 `project_build_info.c`、重新链接 App、重新生成 factory UF2。
  - `build/generated/project_build_info.c` 中 build id 已更新为新的 UTC 时间戳。
- 还需完成：
  - 首烧最新 factory 后，实机查询 `SYST:FW:BUILD?`。
  - 再跑一次 OTA，确认升级前后 build id 可以作为镜像变化依据。
  - 后续发布流程可增加 git commit hash、dirty 标志和 release channel 字段。
- 关联文件：
  - `tools/build_info/gen_build_info.py`
  - `CMakeLists.txt`
  - `config/project_config.h`
  - `middleware/scpi_port/src/scpi_port.c`
  - `docs/SCPI_COMMANDS.md`
  - `README.md`
- 下一步：
  - 用最新 factory 和 OTA payload 验证 build id 前后变化。

### TASK-20260621-021 - SCPI 降噪与 OTA ACK 稳定性实机验证

- 状态：完成
- 日期：2026-06-21
- 任务目标：
  - 验证关闭周期 health 日志后 SCPI 通道是否稳定，并确认 OTA ACK 粘连修正后的完整升级流程。
- 完成内容：
  - 重新首烧最新 `build/RP2350_TRIG_FACTORY.uf2`。
  - 查询 `*IDN?`、`SYST:FW:VERS?`、`SYST:FW:BUILD?`、`SYST:OTA:STAT?`、`SYST:OTA:SLOT?`、`SYST:OTA:RES?` 均正常。
  - 观察 3 秒串口输入，`observe_logs_count=0`，确认周期 health 日志已默认关闭。
  - 执行 `python tools\ota_send\ota_send.py COM4 build\RP2350_TRIG.bin`，脚本输出不再出现 `"OK"` 粘连。
  - OTA payload 大小为 `71200` 字节，CRC32 为 `0x7E0384DD`。
  - OTA 完成后状态进入 `"READY_TO_REBOOT",2,"NONE",2`。
  - 重启前审计查询：
    - `SYST:OTA:SLOT? -> 1,2,1,0,0`
    - `SYST:OTA:RES? -> 2,"NONE","NONE",2,71200,2114159837`
  - 发送 `SYST:OTA:BOOT` 后 USB CDC 断开并重新枚举。
  - 重启后查询：
    - `SYST:OTA:STAT? -> "IDLE",1,"NONE",0`
    - `SYST:OTA:SLOT? -> 1,0,1,0,0`
    - `SYST:OTA:RES? -> 0,"NONE","APPLIED",2,71200,2114159837`
  - 执行 `SYST:OTA:COMM`，命令明确返回 `"OK"`。
- 验证结果：
  - SCPI 通道不再被周期 health 日志污染。
  - OTA 发送脚本已兼容 `"OK"` ACK，不再打印粘连响应。
  - `SYST:OTA:COMM` ACK 和 confirmed metadata 写入均实机通过。
  - 完整 OTA、BOOT、审计、COMMIT 流程稳定通过。
- 还需完成：
  - build id 当前仍在 CMake configure 阶段生成，后续应改为每次构建或发布脚本显式生成。
  - 增加 OTA 失败路径测试：CRC 错误、向量表错误、metadata 写入中断、copy 过程中断电。
  - 后续设计真正 rollback 策略，目前仍是 copy-to-active 模式。
- 关联文件：
  - `config/project_config.h`
  - `components/diagnostics/src/diagnostics.c`
  - `tools/ota_send/ota_send.py`
  - `docs/TASK_PROGRESS.md`
- 下一步：
  - 实现 build id 每次构建刷新，并开始 OTA 失败路径和掉电恢复测试。

### TASK-20260621-020 - OTA ACK 实机验证与 SCPI 通道降噪

- 状态：进行中
- 日期：2026-06-21
- 任务目标：
  - 验证固件版本/build id 查询和 OTA 写命令 ACK，并处理 ACK 粘连和 health 日志干扰 SCPI 的问题。
- 完成内容：
  - 首烧后查询 `SYST:FW:VERS? -> 0,1,0`。
  - 查询 `SYST:FW:BUILD? -> "20260621153554"`。
  - 执行 `python tools\ota_send\ota_send.py COM4 build\RP2350_TRIG.bin`，OTA 传输成功。
  - 发现 `BEGIN/END` 的 `"OK"` ACK 可能与紧随其后的查询响应粘连为同一行，例如 `"OK""CHECK_PERMISSION"...`。
  - 修改 `tools/ota_send/ota_send.py`，在读取响应时剥离行首连续 `"OK"` ACK。
  - `SYST:OTA:BOOT` 实机触发 USB CDC 断开并重新枚举。
  - 重启后查询 `SYST:OTA:RES? -> 0,"NONE","APPLIED",2,71312,1983630478`。
  - 执行 `SYST:OTA:COMM` 后状态进入 `"COMMITTED",1,"NONE",5`。
  - 发现周期 health 日志仍可能抢占手动 SCPI 读取。
  - 新增 `PROJECT_ENABLE_HEALTH_LOG`，默认关闭周期 health 日志，降低 SCPI/OTA 控制通道污染。
- 验证结果：
  - 固件版本和 build id 查询实机可用。
  - 带 ACK 的 OTA 流程实机可用。
  - `cmake --build --preset pico2-release` 编译通过。
  - `python tools\ota_send\ota_send.py COM4 build\RP2350_TRIG.bin --dry-run` 执行通过。
  - 关闭 health 日志后的固件尚未重新首烧验证。
- 还需完成：
  - 重新首烧最新 `build/RP2350_TRIG_FACTORY.uf2`，确认 health 日志不再周期输出。
  - 再跑一次 OTA，确认脚本不再打印粘连 ACK。
  - 后续让 build id 在每次 build 时刷新，避免只有 CMake configure 时刷新。
- 关联文件：
  - `tools/ota_send/ota_send.py`
  - `config/project_config.h`
  - `components/diagnostics/src/diagnostics.c`
  - `docs/TASK_PROGRESS.md`
- 下一步：
  - 首烧最新 factory，验证 SCPI 通道降噪和 OTA ACK 解析稳定性。

### TASK-20260621-019 - OTA 版本可识别与写命令 ACK

- 状态：进行中
- 日期：2026-06-21
- 任务目标：
  - 增加固件版本/build id 查询，使 OTA 前后可确认运行镜像；统一 OTA 写命令的确认响应。
- 完成内容：
  - 新增 `PROJECT_BUILD_ID`，默认值为 `dev`。
  - CMake 在 configure 阶段生成 UTC 时间戳 build id，并通过编译宏注入 App 和 Bootloader。
  - 新增 SCPI `SYST:FW:VERS?`，返回 `major,minor,patch`。
  - 新增 SCPI `SYST:FW:BUILD?`，返回当前固件 build id。
  - `SYST:OTA:BEGIN`、`END`、`ABOR`、`BOOT`、`COMM` 接受事件后返回 `"OK"`。
  - `SYST:OTA:DATA` 保持不逐块 ACK，避免降低 OTA 传输效率和干扰 binary block。
  - 更新 `tools/ota_send/ota_send.py`，查询响应过滤 `"OK"` ACK，兼容新的写命令确认。
  - 更新 README 和 SCPI 文档。
- 验证结果：
  - 首次编译发现旧的无 ACK helper 已无调用，在 `-Werror` 下触发 unused-function，已删除。
  - `cmake --build --preset pico2-release` 编译通过。
  - `python tools\ota_send\ota_send.py COM4 build\RP2350_TRIG.bin --dry-run` 执行通过。
  - 已生成 `build/RP2350_TRIG_FACTORY.uf2` 和 `build/RP2350_TRIG.bin`。
- 还需完成：
  - 重新首烧最新 factory 镜像。
  - 实机查询 `SYST:FW:VERS?` 和 `SYST:FW:BUILD?`。
  - 执行 OTA，确认带 ACK 的 `BEGIN/END/BOOT/COMM` 不影响脚本和串口手动操作。
  - 后续可考虑 build id 每次 build 自动刷新，而不是仅 CMake configure 阶段刷新。
- 关联文件：
  - `config/project_config.h`
  - `CMakeLists.txt`
  - `middleware/scpi_port/src/scpi_port.c`
  - `tools/ota_send/ota_send.py`
  - `docs/SCPI_COMMANDS.md`
  - `README.md`
  - `docs/TASK_PROGRESS.md`
- 下一步：
  - 首烧最新 factory 后，验证版本查询和带 ACK 的 OTA 完整流程。

### TASK-20260621-018 - OTA 审计与 Commit 实机验证

- 状态：完成
- 日期：2026-06-21
- 任务目标：
  - 验证 OTA metadata 审计查询、Bootloader copy 结果记录和 App `COMMIT` 确认路径是否在硬件上可用。
- 完成内容：
  - 重新首烧最新 `build/RP2350_TRIG_FACTORY.uf2` 后，确认设备通过 `COM4` 在线。
  - 初始状态查询：
    - `SYST:OTA:STAT? -> "IDLE",1,"NONE",0`
    - `SYST:OTA:SLOT? -> 1,0,1,0,0`
    - `SYST:OTA:RES? -> 0,"NONE","NONE",0,0,0`
  - 执行 `python tools/ota_send/ota_send.py COM4 build/RP2350_TRIG.bin`。
  - OTA payload 大小为 `71112` 字节，CRC32 为 `0x1A029087`。
  - OTA 完成后进入 `"READY_TO_REBOOT",2,"NONE",2`。
  - 重启前审计查询：
    - `SYST:OTA:SLOT? -> 1,2,1,0,0`
    - `SYST:OTA:RES? -> 2,"NONE","NONE",2,71112,436375687`
  - 发送 `SYST:OTA:BOOT` 后 USB CDC 断开并重新枚举。
  - 重启后 Bootloader 审计查询：
    - `SYST:OTA:STAT? -> "IDLE",1,"NONE",0`
    - `SYST:OTA:SLOT? -> 1,0,1,0,0`
    - `SYST:OTA:RES? -> 0,"NONE","APPLIED",2,71112,436375687`
  - 执行 `SYST:OTA:COMM` 后，App 状态进入 `COMMITTED`。
- 验证结果：
  - metadata 的 active/pending/confirmed 查询已实机可用。
  - Bootloader 已能记录并上报 `APPLIED` 结果，说明 Slot B copy-to-active 成功。
  - `SYST:OTA:COMM` 已能写入 confirmed metadata，查询后状态为 `"COMMITTED",1,"NONE",5`。
  - `SYST:OTA:RES?` 在 commit 后返回 `5,"NONE","APPLIED",1,71112,436375687`。
- 还需完成：
  - `SYST:OTA:COMM` 当前作为事件命令没有显式返回 `OK`，后续应统一 SCPI 写命令确认响应。
  - 增加固件 build id/version 查询，便于 OTA 前后确认实际运行镜像版本差异。
  - 增加掉电恢复和失败路径测试，包括 pending 写入后断电、copy 中断、CRC 故障镜像。
  - 后续如要真正 rollback，需要从 copy-to-active 演进到可回滚的 A/B 启动策略，或保留完整 active 备份。
- 关联文件：
  - `components/ota_manager/src/ota_metadata.c`
  - `bootloader/src/bootloader_main.c`
  - `middleware/scpi_port/src/scpi_port.c`
  - `docs/TASK_PROGRESS.md`
- 下一步：
  - 统一 SCPI 写命令响应，并增加固件 build id 查询。

### TASK-20260621-017 - OTA 审计查询与 Commit 闭环

- 状态：进行中
- 日期：2026-06-21
- 任务目标：
  - 在 OTA 已能传输和重启的基础上，补齐 metadata 审计查询、Bootloader 结果记录和 App 侧确认当前固件的 `COMMIT` 路径。
- 完成内容：
  - 将 OTA metadata 版本升级到 v2。
  - 新增 Bootloader 结果枚举：`APPLIED`、`MAX_ATTEMPTS`、`STAGE_VALIDATE_FAILED`、`COPY_FAILED`、`ACTIVE_VALIDATE_FAILED` 等。
  - 公开 `ota_metadata_store()`，让 App 和 Bootloader 共用同一套双副本写入逻辑。
  - 新增 `ota_metadata_confirm_active()`，用于 App 自检通过后确认当前 active slot。
  - Bootloader 在处理 pending 镜像时记录最近一次来源 slot、镜像大小、CRC 和处理结果。
  - `SYST:OTA:SLOT?` 改为读取真实 metadata，返回 `active,pending,confirmed,boot_attempts,rollback_count`。
  - `SYST:OTA:RES?` 改为返回 `app_result,app_error,boot_result,boot_source_slot,boot_size,boot_crc32`。
  - `SYST:OTA:COMM` 接入 `OTA_EVENT_COMMIT`，写入 confirmed metadata。
  - 更新 README 和 SCPI 命令文档中的 OTA 审计查询说明。
- 验证结果：
  - `cmake --build --preset pico2-release` 编译通过。
  - App 和 Bootloader 目标均编译通过，并重新生成 factory UF2。
  - 当前尚未重新首烧并进行板端 OTA 审计查询实测。
- 还需完成：
  - 重新传统烧录最新 `build/RP2350_TRIG_FACTORY.uf2`。
  - 再执行一次 OTA，确认重启后 `SYST:OTA:SLOT?` 和 `SYST:OTA:RES?` 返回真实 metadata。
  - 执行 `SYST:OTA:COMM`，确认状态进入 `COMMITTED`，metadata 的 confirmed slot 正确。
  - 后续增加固件 build id，便于 OTA 前后确认实际运行镜像版本。
- 关联文件：
  - `components/ota_manager/inc/ota_metadata.h`
  - `components/ota_manager/src/ota_metadata.c`
  - `bootloader/src/bootloader_main.c`
  - `components/ota_manager/src/ota_fb.c`
  - `middleware/scpi_port/src/scpi_port.c`
  - `docs/SCPI_COMMANDS.md`
  - `README.md`
- 下一步：
  - 进行最新 factory 首烧和 OTA 审计查询实机验证。

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
