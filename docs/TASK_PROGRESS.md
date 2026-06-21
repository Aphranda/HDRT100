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
