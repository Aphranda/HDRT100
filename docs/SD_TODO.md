# SD 卡系统待办

本文档记录 RP2350_TRIG 的 SD 卡系统从当前最小可用版本推进到产品级维护介质所需的剩余工作。SD 卡定位为 App 侧维护介质，用于离线 OTA 包、日志、配置、资源、采样摘要和验证报告。Bootloader 第一版不读取 SD/FatFs，避免可插拔介质影响基本启动可靠性。

## 当前基线

- [x] SD 卡纳入 HAOFV 管理域：底层驱动只负责 SPI/block，FatFs port 只负责文件系统适配，`storage_manager` 发布状态快照。
- [x] 新增 SD SPI 模式底层驱动：`drivers/external/sd_card/`。
- [x] 新增 FatFs 适配层：`middleware/fatfs_port/`。
- [x] 新增存储管理组件：`components/storage_manager/`。
- [x] App 初始化和周期服务接入 `storage_manager_init()` / `storage_manager_service()`。
- [x] SCPI 查询接入：
  - `SYST:SD:STAT?`
  - `SYST:SD:INFO?`
  - `SYST:STOR:STAT?`
  - `MMEM:CAT?`
  - `MMEM:CAT? "/update"`
- [x] SD 卡文件系统 staging 工具接入：`tools/sd_fs_build/sd_fs_build.py`。
- [x] SD 独立 LCD 页面接入，UI 只读取 `storage_manager_vector_t` 快照。
- [x] LCD 顶部 tab 改为 3 槽滚动式 tabview，KEY2/GPIO2 可切到 SD 页面。

## 当前验证记录

- 日期：2026-07-02
- 构建：`pico2-release`
- build id：`20260702153535`
- OTA 包：`build/RP2350_TRIG_UPDATE.pkg`
- 验证目录：`build/ota_validation_tab_open_source_style_quick`
- 构建验证：
  - `cmake --build --preset pico2-release` 通过。
  - `python tools\release_check\release_check.py --preset pico2-release --build-dir build` 通过，`release_check=OK`。
- OTA 闭环：
  - `SYST:OTA:MODE? -> "DIRECT_AB",1`
  - `SYST:OTA:STAT? -> "COMMITTED",2,"NONE",5`
- SD 查询：
  - `SYST:SD:STAT? -> "CARD_READY",1,1,"OK",0`
  - `SYST:SD:INFO? -> "CARD_READY","SDHC_SDXC",1,61085696,30542848,1,1,1`
  - `SYST:STOR:STAT? -> "CARD_READY",1,1,"OK",0`
  - `MMEM:CAT? -> "OK","image.ub,4486744,FILE;BOOT.BIN,2230264,FILE;System Volume Information,0,DIR;"`
  - `MMEM:CAT? "/update" -> "NO_PATH","OPEN_FAILED:5"`，当前插入卡未创建 `/update`，不是挂载失败。

## P0 - SD 维护介质最小闭环

- [ ] 将 `tools/sd_fs_build/sd_fs_build.py` 生成的 `build/sdcard/` 实际写入 SD 卡。
- [ ] 确认 SD 卡根目录包含 `/update/RP2350_TRIG_UPDATE.pkg`。
- [ ] 复测 `MMEM:CAT? "/update"`，确认默认统一包可被枚举。
- [ ] 增加默认 OTA 包检查命令或状态字段，显示 `/update/RP2350_TRIG_UPDATE.pkg` 是否存在、文件大小和基础校验结果。
- [ ] 增强 `storage_manager` 路径处理：
  - 统一 `0:/`、`/`、相对路径和空路径。
  - 拒绝 `..` 路径穿越。
  - 限制路径长度和目录枚举输出长度。
- [ ] 增强错误码映射，把 FatFs `FRESULT` 和 SD driver status 压缩成稳定 SCPI/UI 文本。
- [ ] 增加无卡、空卡、无 `/update`、坏路径、重新插卡后的基础验证记录。

## P0 - SD 离线 OTA

- [ ] 增加 App 侧离线 OTA 命令，例如 `SYST:OTA:FILE "<path>"`。
- [ ] 离线 OTA 第一版只接受统一 `.pkg`，默认路径为 `/update/RP2350_TRIG_UPDATE.pkg`。
- [ ] raw `.bin` 只保留兼容/台架路径，不作为 SD release 默认格式。
- [ ] SD 文件读取必须转换为现有 OTA 事件流：
  - `BEGIN`
  - `DATA_BLOCK`
  - `END`
  - `BOOT`
  - `COMM`
- [ ] SCPI/UI/SD 入口只投递事件，不直接修改 `OtaAO/OtaFB/OtaVector` 状态。
- [ ] 读取 `.pkg` 前检查 package magic、header size、package size、image slot、run offset 和 CRC/header。
- [ ] 分块读取文件并限时让步，避免长时间持有 `SPI0 + SD` 阻塞 LCD 和 SCPI。
- [ ] 离线 OTA 期间资源申请规则：
  - 读 SD 文件时持有 `SPI0 + SD`。
  - 写 Flash 时持有 `FLASH`。
  - 不同时长时间持有 `SPI0 + SD + FLASH`。
- [ ] 增加离线 OTA 正向验证：SD package -> inactive slot -> `READY_TO_REBOOT` -> `BOOT` -> `COMM`。
- [ ] 增加离线 OTA 负向验证：坏 magic、坏 package CRC、坏 image CRC、坏 vector、读文件中拔卡、空间/路径错误。

## P1 - SD UI 与状态可视化

- [x] 增加独立 SD 页面，显示 card/media/filesystem 基础状态。
- [x] 顶部 tab 支持滚动显示，SD 页面可通过 KEY2 进入。
- [ ] SD 页面第二阶段显示：
  - `/update` 是否存在。
  - 默认 package 是否存在。
  - package size。
  - package build id 或版本。
  - 最近一次 SD/FatFs 错误。
  - 离线 OTA 进度。
- [ ] SD 页面需要区分 `NO_CARD`、`NO_FILESYSTEM`、`CARD_READY`、`PATH_ERROR`、`FAILED`。
- [ ] 检查 SD 页面长字符串裁剪，避免 `NO_FILESYSTEM`、`MOUNT_FAILED`、长文件名等压到边框。
- [ ] 增加 SD 页面手动刷新/重新探测入口，先可通过 SCPI，后续再考虑本地按键长按。

## P1 - 文件系统能力

- [ ] 增加文件信息查询能力：文件大小、属性、修改时间。
- [ ] 增加目录枚举分页或限制，避免单次 SCPI 响应过长。
- [ ] 支持 `/logs` 写入：
  - 固件 build id。
  - OTA 结果。
  - SD/FatFs 错误。
  - 关键诊断 fault。
- [ ] 支持 `/reports` 写入验证报告摘要。
- [ ] 支持 `/config` 读取和写入设备配置。
- [ ] 支持 `/capture` 保存采样摘要或触发统计。
- [ ] 写文件使用临时文件 + flush/sync + rename，降低掉电后半文件风险。
- [ ] 增加容量不足、只读卡、目录不存在、文件已存在的处理策略。

## P1 - 工具与产测

- [x] `tools/sd_fs_build/sd_fs_build.py` 生成 SD 根目录 staging 和 zip 包。
- [ ] `tools/rp2350_tk_toolbox.py` 增加独立 SD 操作区：
  - 构建 SD 文件系统。
  - 打开 staging 目录。
  - 查询 SD 状态。
  - 列出根目录和 `/update`。
  - 触发离线 OTA。
- [ ] 增加命令行 SD smoke 工具，批量执行 `SYST:SD:*` 和 `MMEM:CAT?`。
- [ ] SD staging manifest 记录：
  - package 文件名。
  - package size。
  - CRC32/SHA256。
  - build id。
  - 生成时间。
- [ ] release 归档中同时保存 `RP2350_TRIG_SDCARD.zip` 和 SD manifest。

## P2 - 稳定性与兼容性

- [ ] 多卡兼容验证：
  - FAT32 小容量卡。
  - SDHC。
  - SDXC。
  - 空卡。
  - 已有系统目录的卡。
- [ ] 热插拔验证：
  - 开机无卡后插卡。
  - 已挂载后拔卡。
  - OTA 读文件中拔卡。
  - 失败后重新插卡恢复。
- [ ] 长时间运行验证：
  - UI 周期刷新 + SD 周期探测。
  - SCPI 高频查询 + SD 目录枚举。
  - Trigger 运行时 SD 查询不影响硬实时路径。
- [ ] 掉电验证：
  - 文件写入中断电。
  - 离线 OTA 接收中断电。
  - package 读取完成但 pending 未写入时断电。
  - `COMM` 前后断电。
- [ ] 后续如需提升吞吐，再评估 SDIO；当前 release 继续以 SPI 模式作为稳定基线。

## 架构边界

- SD 卡只作为 App 侧维护介质，不参与 Bootloader 最小启动链路。
- Bootloader 不集成 SD/FatFs/UI/SCPI。
- SD 文件输入、SCPI 输入、UI 输入都只能投递事件，不能直接修改 OTA 或 Trigger 域状态。
- `storage_manager` 只发布存储域快照，不保存大文件缓存到 Vector。
- 大文件数据块不进入 Vector，只记录路径、大小、CRC、进度和错误摘要。
- LCD 与 SD 共享 SPI0，所有访问必须通过 Resource Arbiter 串行化。

## 关联文件

- `tools/sd_fs_build/sd_fs_build.py`
- `drivers/external/sd_card/inc/sd_card.h`
- `drivers/external/sd_card/src/sd_card.c`
- `middleware/fatfs_port/inc/fatfs_port.h`
- `middleware/fatfs_port/src/fatfs_port.c`
- `middleware/fatfs_port/src/fatfs_diskio.c`
- `components/storage_manager/inc/storage_manager.h`
- `components/storage_manager/src/storage_manager.c`
- `components/sync_config_ui/src/sync_config_ui.c`
- `middleware/scpi_port/src/scpi_port.c`
- `tools/rp2350_tk_toolbox.py`
- `docs/OTA方案.md`
- `docs/HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE.md`
- `docs/TASK_PROGRESS.md`
