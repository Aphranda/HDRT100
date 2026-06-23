# OTA 产品化待办

本文档记录 OTA 从当前验证版推进到工业产品发布版所需的剩余工作。

## P0 - 发布构建隔离

- [x] `pico2-release` 关闭 `PROJECT_ENABLE_OTA_FAULT_INJECTION`。
- [x] 新增 `pico2-validation`，专门用于异常注入、台架验证和产测调试。
- [x] 发布前检查 release 固件中 `SYST:OTA:INJ:*` 命令不可用。
- [x] 发布前检查周期调试日志默认关闭。
- [x] 明确 factory UF2、OTA BIN、validation 固件、release 固件的命名和归档规则。

## P0 - 掉电恢复验证

- [x] `SYST:OTA:COMM` 增加前置条件：仅在无 pending 且 Bootloader 最近结果为 `APPLIED` 时允许确认。
- [x] Bootloader 普通启动优先使用 metadata 中的 Slot A size/CRC 验证 active App，metadata 不完整时才退回最小向量校验。
- [x] 设计 Bootloader copy transaction 状态，区分 `COPY_STARTED`、`COPY_ERASED_ACTIVE`、`COPY_PROGRAMMING`、`COPY_VERIFYING`、`COPY_DONE`，避免掉电后错误清 pending。
- [x] 实现 metadata copy transaction 字段/API，并增加扩展区 CRC，支持后续 Bootloader 按阶段持久化 copy-to-active 状态。
- [x] copy-to-active 失败时保留可恢复信息，不应在 Slot A 可能损坏时直接清 pending。
- [x] 正常 OTA 闭环验证：Slot B staging、Bootloader copy-to-active、`APPLIED`、`COMM`、transaction 清零。
- [ ] 评估从 copy-to-active 演进到真正 A/B 启动，或增加 active 备份/scratch 恢复机制。
- [x] OTA 接收过程中复位/掉电，重启后应保留旧 App，metadata 不进入 pending。
- [ ] OTA metadata 写入过程中掉电，重启后应从双副本中选择有效副本。
- [x] Bootloader Slot B -> Slot A copy 过程中掉电，重启后应可恢复到可启动状态。
- [x] `SYST:OTA:COMM` 前掉电，重启后应保留可审计状态，允许重新确认或按策略处理。
- [ ] 使用可控电源或继电器台架执行重复掉电测试，并记录循环次数和失败率。

## P0 - A/B 直接切换演进

- [x] 输出 A/B 直接切换设计文档，明确 copy-to-active 到 direct A/B 的迁移路线。
- [x] 增加 Slot B App 链接脚本和 `RP2350_TRIG_B.bin` 构建产物。
- [x] metadata 增加 `boot_mode`、`previous_slot`、`boot_generation`、`boot_capabilities` 等 A/B 扩展字段。
- [x] Bootloader 支持按 `active_slot` 直接跳转 Slot A 或 Slot B。
- [x] OTA 接收目标从固定 Slot B 改为 inactive slot。
- [x] 增加 `SYST:OTA:MODE?`、`SYST:OTA:TARG?`、`SYST:OTA:CAP?`。
- [x] 更新 `ota_send.py`，根据目标 slot 自动选择 A/B 镜像。
- [x] 完成 direct A/B 正常双向升级验证。
- [x] 完成 direct A/B 未确认回滚验证。
- [x] 完成 direct A/B 断电恢复验证。
- [x] 实现统一 OTA package：一个文件包含 Slot A/Slot B 镜像，由下位机根据当前模式和目标 slot 选择写入镜像。
- [x] 验证统一 OTA package 在 `DIRECT_AB` 模式下可 A->B、B->A 双向升级并确认。
- [x] 验证统一 OTA package 在 release 默认 `COPY_TO_ACTIVE` 模式下可选择 Slot A 链接镜像并完成 copy-to-active。
- [ ] 评估 release 默认启用 `DIRECT_AB` 的出厂条件和迁移策略。

## P1 - 镜像完整性与兼容性

- [x] OTA payload 增加基础 manifest/package header，包含包大小、镜像 slot、偏移、大小、CRC32 和运行地址。
- [x] OTA package manifest 扩展产品型号、硬件版本、App 版本、build id、payload SHA-256 和 `min_bootloader_version`。
- [x] 验证统一 OTA package 负向路径：整包 CRC 错误、镜像 CRC 错误、App 向量错误、包头 magic/version/size 错误、slot/run_offset 不匹配。
- [x] 增加 `min_bootloader_version`，App 在 OTA package 首块解析时检查 Bootloader 能力。
- [x] 增加 `SYST:BOOT:VERS?` 或等效命令，查询 Bootloader 版本。
- [x] 增加 `SYST:OTA:CAP?`，查询当前设备支持的 OTA 能力。
- [ ] 后续需要安全升级时，增加签名校验。
- [x] metadata v3 扩展字段增加独立 CRC 或扩展区版本/CRC，避免尾部字段不受保护。
- [ ] 建立 metadata schema 迁移规则，后续新增字段必须保持旧 Bootloader/App 可判定兼容性。

## P1 - SCPI OTA 交互语义

- [ ] 明确 `BEGIN/DATA/END/BOOT/COMM` 的返回语义：`"OK"` 仅表示命令接受，最终结果必须查询 `SYST:OTA:STAT?` 或 `SYST:OTA:RES?`。
- [ ] 对 `END/BOOT/COMM` 增加可选同步等待模式或专用查询，减少上位机误把入队成功当成执行成功。
- [ ] 增加错误时序测试：`READY_TO_REBOOT` 前发送 `COMM`、无 pending 发送 `BOOT`、接收过程中重复 `BEGIN`。
- [ ] release 固件中验证 `SYST:OTA:INJ:*` 返回 SCPI 错误或不可识别。

## 统一 OTA package 负向验证记录

验证环境：

- 日期：2026-06-23
- 固件：`build-codex-release\RP2350_TRIG_FACTORY.uf2`
- package：`build-codex-release\RP2350_TRIG_UPDATE.pkg`
- 端口：`COM4`
- 模式：release 默认 `COPY_TO_ACTIVE`
- 初始状态：`SYST:OTA:SLOT? -> 1,0,1,0,0`

验证命令和结果：

| 场景 | 命令参数 | 期望 | 实测 |
|---|---|---|---|
| 整包 CRC 错误 | `--corrupt-crc` | `FAILED/CRC` | `"FAILED",2,"CRC",4` |
| 镜像 CRC 错误 | `--package-negative image-crc` | `FAILED/CRC` | `"FAILED",2,"CRC",4` |
| App 向量错误 | `--package-negative image-vector` | `FAILED/VECTOR` | `"FAILED",2,"VECTOR",4` |
| 包头 magic 错误 | `--package-negative header-magic` | `FAILED/BAD_HEADER` | `"FAILED",2,"BAD_HEADER",4` |
| 包头 version 错误 | `--package-negative header-version` | `FAILED/BAD_HEADER` | `"FAILED",2,"BAD_HEADER",4` |
| 包头 package size 错误 | `--package-negative header-size` | `FAILED/BAD_HEADER` | `"FAILED",2,"BAD_HEADER",4` |
| image slot 错误 | `--package-negative slot` | `FAILED/BAD_HEADER` | `"FAILED",2,"BAD_HEADER",4` |
| image run_offset 不匹配 | `--package-negative run-offset` | `FAILED/IMAGE_TOO_LARGE` | `"FAILED",2,"IMAGE_TOO_LARGE",4` |

验证后状态：

- `SYST:OTA:SLOT? -> 1,0,1,0,0`
- `SYST:OTA:RES? -> 4,"IMAGE_TOO_LARGE","APPLIED",1,74056,3774081352`
- 无 pending，旧 confirmed Slot A 保持运行。

## P1 - 发布验证报告

- [ ] 新增 OTA validation report 模板，记录正常升级、CRC 错误、向量错误、中止、metadata 单副本损坏、copy 失败和掉电测试结果。
- [ ] 在 OTA validation report 中增加统一 package 专项：正常升级、A/B 自动选择、COPY_TO_ACTIVE 兼容、负向包验证、失败后旧固件保持运行。
- [ ] 将每次 release 的 `.uf2`、`.bin`、map 文件、build id、CRC/SHA 和验证报告一起归档。
- [ ] 明确异常注入固件不能作为客户交付物。

## P2 - 工具与自动化

- [ ] `tools/ota_send/ota_send.py` 增加可选自动 boot/commit 流程。
- [x] 增加 release 检查脚本，自动确认 release preset 中故障注入关闭。
- [x] release 检查脚本纳入 Slot B 镜像和统一 OTA package 产物。
- [ ] 增加串口回归脚本，批量执行 `SYST:FW:*`、`SYST:OTA:*` 基础查询。
- [ ] 增加掉电台架控制脚本接口，支持随机或指定阶段断电。

## Portable OTA 迁移验证记录

- [x] Step 2A：产品侧 package parser 委托到 `third_party/portable_ota`。
- [x] Step 2A 构建闭环：`build-portable-migration` 构建通过，`release_check=OK`。
- [x] Step 2A 板端闭环：COM4 上完成 factory 烧录、统一 package 正常 OTA、
  Bootloader apply、App commit 和 package 负向矩阵验证。
- [x] Step 2A 最终安全状态：`SYST:OTA:SLOT? -> 1,0,1,0,0`，无 pending，
  confirmed Slot A 保持运行。
- [x] Step 2B：产品侧 CRC32 委托到 `middleware/portable_ota_port`，
  App 和 Bootloader 共用 `third_party/portable_ota/src/pota_crc32.c`。
- [x] Step 2B 构建闭环：`build-portable-boot-crc` 构建通过，
  portable OTA ARM GCC compile/object-build gate 通过，`release_check=OK`。
- [x] Step 2B Bootloader 影响验证：用 `picotool` 烧录 factory，
  COM4 上完成统一 package 正常 OTA、`SYST:OTA:BOOT`、Bootloader
  `APPLIED`、App `COMM`。
- [x] Step 2B 负向矩阵复测：整包 CRC、镜像 CRC、App 向量、包头
  magic/version/size、slot、run_offset 均按预期失败。
- [x] Step 2B 最终安全状态：`SYST:OTA:SLOT? -> 1,0,1,0,0`，无 pending；
  最后一次审计保留 `FAILED/IMAGE_TOO_LARGE`，Bootloader 最近成功结果为
  `APPLIED`。
- [x] Step 2C：产品侧 App 向量校验委托到 `middleware/portable_ota_port`，
  App 和 Bootloader 共用 `third_party/portable_ota/src/pota_image.c`。
- [x] Step 2C 构建闭环：新增 portable image vector 单测，
  `build-portable-image` 构建通过，`release_check=OK`。
- [x] Step 2C Bootloader 影响验证：用 `picotool` 烧录 factory，
  COM4 上完成统一 package 正常 OTA、`SYST:OTA:BOOT`、Bootloader
  `APPLIED`、App `COMM`。
- [x] Step 2C 负向矩阵复测：`image-vector` 精准返回 `FAILED/VECTOR`，
  其余整包 CRC、镜像 CRC、包头、slot、run_offset 均按预期失败。
- [x] Step 2C 最终安全状态：`SYST:OTA:SLOT? -> 1,0,1,0,0`，无 pending；
  最后一次审计保留 `FAILED/IMAGE_TOO_LARGE`。
- [x] Step 2D：补齐 `pota_core` 产品语义差异：package header CRC 早期拒绝、
  `pota_service()` 分段擦除、最终块页对齐 padding、非最终非页对齐块拒绝。
- [x] Step 2D 闭环：portable OTA ARM GCC compile/object-build gate 通过；
  本步未切换产品固件路径，因此不重复板端烧录。
- [x] Step 2E：增加 middleware core adapter，映射 `pota_status_t`/error/result
  到现有 `ota_vector_t`/SCPI 语义。
- [x] Step 2F：将产品 `ota_fb` BEGIN/TICK/DATA/END/ABORT 迁移到
  `pota_core`，并执行完整烧录、正向 OTA、负向矩阵闭环。
- [x] Step 2F 最终安全状态：`SYST:OTA:SLOT? -> 1,0,1,0,0`，无 pending；
  状态/错误映射保持原有 SCPI 期望。
- [x] Step 3A：新增 `include/pota.h` 一站式头文件和 `pota_session`
  会话门面，产品 middleware core adapter 改为调用 `pota_session_*`。
- [x] Step 3B 闭环：`build-portable-session` 构建通过，
  portable OTA ARM GCC compile/object-build gate 通过，`release_check=OK`；
  COM4 上完成 factory 烧录、正向 OTA、Bootloader `APPLIED`、App `COMM`
  和完整负向矩阵。
- [x] Step 3B 最终安全状态：`SYST:FW:BUILD? -> "20260623101235"`，
  `SYST:OTA:SLOT? -> 1,0,1,0,0`，`SYST:OTA:RES? ->
  4,"IMAGE_TOO_LARGE","APPLIED",1,75024,895520178`。
- [x] Step 3C：新增 `pota_strings`，将 OTA state/error/result 和
  Bootloader result 文本 helper 下沉到 `third_party/portable_ota`；
  产品侧 `ota_state_to_string()`、`ota_error_to_string()` 和
  `ota_metadata_boot_result_to_string()` 改为 middleware 包装。
- [x] Step 3C 闭环：`build-portable-strings` 构建通过，
  portable strings 单测纳入 ARM GCC compile/object-build gate，
  `release_check=OK`；COM4 上完成 factory 烧录、正向 OTA、Bootloader
  `APPLIED`、App `COMM`。
- [x] Step 3C 负向矩阵复测：整包 CRC、镜像 CRC、App 向量、包头
  magic/version/size、slot、run_offset 均按预期输出原有 SCPI 文本。
- [x] Step 3C 最终安全状态：`SYST:FW:BUILD? -> "20260623102556"`，
  `SYST:OTA:SLOT? -> 1,0,1,0,0`，`SYST:OTA:RES? ->
  4,"IMAGE_TOO_LARGE","APPLIED",1,75224,2315388268`。
- [x] Step 3D：将 metadata v3 schema、基础 CRC、扩展区 CRC、A/B CRC、
  默认值初始化、copy transaction 字段清理、状态/slot 合法性判断和
  newest-copy 选择逻辑下沉到 `third_party/portable_ota`。
- [x] Step 3D 产品接入：新增 `portable_ota_metadata_port.c`，通过字段布局
  static assert 保证 `ota_metadata_t` 与 `pota_metadata_t` 兼容；产品侧
  flash 双副本读写、v2 旧格式迁移和 RP2350 offset 策略仍保留在
  `components/ota_manager/src/ota_metadata.c`。
- [x] Step 3D 闭环：`build-portable-metadata` 构建通过，
  portable metadata v3 单测纳入 ARM GCC compile/object-build gate，
  `release_check=OK`；COM4 上完成 factory 烧录、正向 OTA、Bootloader
  `APPLIED`、App `COMM`。
- [x] Step 3D 负向矩阵复测：整包 CRC、镜像 CRC、App 向量、包头
  magic/version/size、slot、run_offset 均按预期失败，且未留下 pending。
- [x] Step 3D 最终安全状态：`SYST:FW:BUILD? -> "20260623104609"`，
  `SYST:OTA:SLOT? -> 1,0,1,0,0`，`SYST:OTA:RES? ->
  4,"IMAGE_TOO_LARGE","APPLIED",1,75240,3624267205`。
- [x] Step 3E：将 `mark_pending`、`confirm_active`、`set_boot_mode`、
  `set_fault_injection` 和 copy transaction 状态更新等内存 metadata
  mutation helper 下沉到 `third_party/portable_ota`。
- [x] Step 3E 产品接入：产品 `ota_metadata_*` 公开 API 保持不变，
  内部收敛为 `load -> portable mutation -> store`；RP2350 flash 双副本
  存储策略仍留在产品层。
- [x] Step 3E 闭环：`build-portable-metadata-mutation` 构建通过，
  portable metadata mutation 单测纳入 ARM GCC compile/object-build gate，
  `release_check=OK`；COM4 上完成 factory 烧录、正向 OTA、Bootloader
  `APPLIED`、App `COMM`。
- [x] Step 3E 负向矩阵复测：整包 CRC、镜像 CRC、App 向量、包头
  magic/version/size、slot、run_offset 均按预期失败，且未留下 pending。
- [x] Step 3E 最终安全状态：`SYST:FW:BUILD? -> "20260623112832"`，
  `SYST:OTA:SLOT? -> 1,0,1,0,0`，`SYST:OTA:RES? ->
  4,"IMAGE_TOO_LARGE","APPLIED",1,75160,3242593473`。
