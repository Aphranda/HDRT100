# Distributed RefMem 内部主域任务进度

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_TASK_PROGRESS.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`
Last updated: 2026-08-15

本文档记录 Distributed Vector Blackboard / RefMem Sync Domain 的阶段性任务进度、验证结果和后续动作。待办事项放在 `REFMEM_DOMAIN_TODO.md`，本文只记录已经发生的工作和可回溯结果。

### REFMEM-TASK-20260815-031 - Registry image lifecycle buffers

- 状态：完成 COM5/COM6 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `RefMemTableRegistry` 从 descriptor/CRC staging 推进到 registry 级真实 package image 生命周期。
  - 保持 HAOFV 边界：完整 `.rmtp` bytes 只进入 `DistributedRefMemAO` / `RefMemTableRegistry` 私有 buffer，向量表和 SCPI 查询只暴露 descriptor、CRC、state、seq 和 evidence 摘要。
- 完成内容：
  - `RefMemTableRegistry` 新增 active/staging/rollbackable 三组静态 image buffer 和 size，单包上限 `REFMEM_TABLE_IMAGE_BUFFER_SIZE=8192`。
  - 新增 `refmem_table_registry_stage_package_image()`，在 package/header/table CRC 与 owner validation summary 通过后复制完整 package bytes 到 staging image。
  - `refmem_table_registry_activate_staging()` 已能在 gate 通过且 staging bytes 存在时执行 registry 级切换：旧 active 进入 rollbackable，staging 进入 active，per-table active CRC 从 staging CRC 更新，staging descriptor/payload 清空。
  - metadata-only staging、失败 staging 或空 package staging 会清空 staging payload；activation 继续返回 `IMAGE_NOT_LOADED`，避免复用上一轮 package bytes 形成伪 active。
  - `SYSTem:REFMEM:LOAD:SD` 现在把 StorageAO 读取到的 package bytes 随 package validation summary 一起交给 `DistributedRefMemAO`，SCPI 仍不直接写 TableRegistry 或 active fact。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过；新增 stale payload 回归断言。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815115843`，package CRC `0x15FE50D0`。
  - COM5 OTA 到 build `20260815115843` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM6 OTA 到 build `20260815115843` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM5 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815115843_image_lifecycle` 通过。
  - COM6 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM6 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM6_20260815115843_image_lifecycle` 通过。
  - COM5/COM6 均执行 `tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py --skip-sd` 通过，确认 `LOAD:NODE`、`LOAD:BOARD` 和 command ACK/NACK 路径未被 staging payload 生命周期改动破坏。
- 结论：
  - P0 的 registry 级真实 active/staging/rollbackable image buffer 切换已经落地；向量表仍只承载摘要，符合 HAOFV。
  - activation 后 active package 到业务 stable table view 的解析、owner access/release、真实 owner validation callback 调度和跨节点 activation ACK/FENCE 仍是后续 P0/P2/P3 工作。

### REFMEM-TASK-20260815-030 - NodeLoadTable staging image

- 状态：完成 COM5 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `SYSTem:REFMEM:LOAD:NODE` 从单条候选状态快照推进为私有 staging `DistributedNodeLoadTable` image。
  - 保持 HAOFV 边界：SCPI 只 post `NODE_LOAD_STAGE` command，`DistributedRefMemAO` owner take 后更新 staging 表镜像、执行 validation、ACK/NACK，并只向 TableRegistry 发布摘要。
- 完成内容：
  - 新增 `s_staging_node_load_table` 和 `s_staging_node_load_valid`，`LOAD:NODE` 成功后在私有 staging 表上累积候选。
  - `LOAD:NODE` 现在按候选 `NodeLoadTable` 调用 `refmem_application_contract_validate_node_load_table()`，并计算整表 staging CRC。
  - 成功路径只更新 `RefMemTableRegistry` table 3 `NodeLoadTable` 的 staging CRC、`OWNER_OK` 状态和 flags；不再通过整包 staging refresh 把 9 张表都标成 staging。
  - 非法 node/instance 仍返回 `REJECTED` / command NACK，并把 table 3 标成 `FAILED`，不污染 active 表。
  - `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 同步断言：合法 `LOAD:NODE` 必须有非零 NodeLoadTable staging CRC，`SYSTem:REFMEM:TABle? 3` 的 staging mask 至少包含 bit3。
- 验证结果：
  - `python -m py_compile tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815114032`，package CRC `0xF7BB88ED`。
  - COM5 OTA 到 build `20260815114032` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM5 执行 `python -u tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM5 --skip-sd --timeout 10 --out-dir build-rtos-multicore-smoke\refmem_load_COM5_20260815114032_nodeload_staging` 通过；合法 `LOAD:NODE 5,9,32,32,1,0,0` 生成 table 3 staging CRC `3388599922`，`SYSTem:REFMEM:TABle? 3` 返回 `staging_table_mask=8`。
  - COM5 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815114032` 通过，确认 `LOAD:SD` 全 9 表 staging CRC 未被破坏。
- 结论：
  - `LOAD:NODE` 已进入 P0 表镜像主线，不再只是单字段 snapshot。
  - active/staging/rollbackable 全表 image buffer、activation gate、abort/rollback 仍是 P0 下一步。
- 后续动作：
  - 按 P0-P3 优先级推进：P0 真实 table image activation/rollback；P1 SlotClaimMap 协调和溢出验证；P2 RefMemSlotContract 字段级 owner/snapshot；P3 Command/ACK/NACK 与 activation gate 统一。

### REFMEM-TASK-20260815-029 - TableRegistry per-table staging CRC gate

- 状态：完成 COM5 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 将 RefMem 主线从 `LOAD:SD => STAGED` 字符串检查推进到 TableRegistry per-table 证据检查。
  - 验证 `.rmtp` package directory 中 9 张 canonical 表的 CRC 是否逐表进入 `SYSTem:REFMEM:TABle?` staging entry。
  - 保持 HAOFV 边界：SCPI 只执行 `LOAD:SD` 意图和读取 snapshot，不直接写 active fact 或 TableRegistry。
- 发现问题：
  - COM5 旧 build 执行 `SYSTem:REFMEM:LOAD:SD` 后返回 `STAGED`，但 `SYSTem:REFMEM:TABle? 0` 的 `staging_crc32` 仍为整包 CRC `0xB27CF840`，不是 `ApplicationMap` 单表 CRC `0x60E14FD0`。
  - 根因是 `DistributedRefMemAO` 将 package parser summary 交给 `RefMemTableRegistry` 时漏填 `validation.table_mask`，导致 `refmem_table_registry_stage_package_validation()` 拒绝 per-table validation summary，并退回旧的整包 CRC staging refresh。
- 完成内容：
  - `distributed_refmem_stage_sd_system_pack()` 补齐 `validation.table_mask = 0x1FF`，让 RefMemAO 向 TableRegistry 提交完整 9 表 package validation 事实。
  - 新增 `tools/refmem_table_registry_validate/refmem_table_registry_validate.py`，读取本地 RMTP header/directory，校验 package/payload/table CRC，然后通过单次串口生命周期查询：
    - `SYSTem:REFMEM:LOAD:SD`
    - `SYSTem:COMMand:ACK?`
    - `SYSTem:REFMEM:LOAD:STATus?`
    - `SYSTem:REFMEM:TABle? 0..8`
    - `SYSTem:ERRor?`
  - 脚本断言 `staging_table_mask=0x1FF`、每表 `staging_crc32` 等于 RMTP directory CRC、`validation_state=OWNER_OK`、`STAGING_PRESENT|CRC_OK|OWNER_OK` flags 齐全。
- 验证结果：
  - `python -m py_compile tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815113037`，package CRC `0xFF54EA0C`。
  - COM5 OTA 到 build `20260815113037` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM5 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815113037_r2` 通过。
  - 验证通过的 9 表 staging CRC：
    - ApplicationMap `0x60E14FD0`
    - BoardCapability `0xCEFC3615`
    - GenericNode `0xA1681310`
    - NodeLoad `0x7D7762D1`
    - FbInstance `0x1CC15A37`
    - EventLink `0x5A4E36C8`
    - DataLink `0xD7E24830`
    - DeploymentGate `0x31FDBA68`
    - ConnectionQuality `0xA67C64D1`
- 结论：
  - `LOAD:SD` 正向路径现在不只证明 package 能解析为 `STAGED`，还证明 TableRegistry 的 per-table staging 证据与 RMTP directory 一致。
  - 真实 active/staging/rollbackable image buffer 仍未落地，activation 仍应保持 `IMAGE_NOT_LOADED` 阻断。
- 后续动作：
  - 继续 RefMem 主线 P0：实现真实 active/staging/rollbackable table image 切换和 owner validation callback 调度。
  - 继续 P7：补无 SD、manifest 缺失、CRC 正确但 owner validation 失败、activation 失败回滚等验证。

### REFMEM-TASK-20260815-028 - Optimize LOAD:SD bounded read chunk

- 状态：完成 COM5 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 在不破坏 HAOFV 边界的前提下优化 `SYSTem:REFMEM:LOAD:SD` 耗时。
  - 保持 SCPI 只发起 load 意图和输出 snapshot，SD/FatFs 仍归 StorageAO，RefMem staging 仍归 DistributedRefMemAO。
- 完成内容：
  - `STORAGE_MANAGER_FILE_READ_MAX_BYTES` 从 128B 提高到 512B，扩大 StorageAO 内部 bounded read job 缓冲。
  - `SCPI_REFMEM_PACKAGE_READ_CHUNK` 从 128B 提高到 512B，让 `LOAD:SD` 读取 4800B RMTP 时从约 38 个 read job 降到约 10 个 read job。
  - 公开 `SYSTem:STORage:FILE:READ?` 的 `SCPI_STORAGE_MMEM_READ_BYTES_MAX` 仍保持 128B，避免普通 SCPI 文件读取响应突然变长。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815110412`，package CRC `0x81CE4C7E`。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - COM5 已通过 OTA/commit 升级到 build `20260815110412`。
  - COM5 写入当前 HIL manifest、profile、mission、cal 和 `/refmem/app_model.rmtp` 后，`SYSTem:REFMEM:LOAD:SD` 单次加载加后续状态查询总耗时约 `6.944 s`；返回 `STAGED`，Storage 最后一块 `FILE_READ` 为 192B，错误队列为 0。
  - COM5 执行 `python -u tools\refmem_pack_write\refmem_pack_write.py COM5 --timeout 10 --load-timeout 30 --chunk 256 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --out-dir build-rtos-multicore-smoke\refmem_pack_write_COM5_20260815110412_512b_load` 通过。
- 结论：
  - `LOAD:SD` 首轮优化有效，耗时从约 37 秒降到 7 秒量级。
  - 无参 `SYSTem:REFMEM:LOAD:SD` 在本轮干净串行测试中未复现 `Missing parameter`；此前污染来自命令行引号错误和同一 COM 口并发访问造成的响应串扰。
- 后续动作：
  - 当前 512B bounded read 路径作为主线可用基线保留；继续压缩到 1 秒级的 bounded stream read job 先转入支线优化，不阻塞后续 RefMem 主线闭环。

### REFMEM-TASK-20260815-027 - StorageAO/RefMem LOAD:SD HIL deep dive

- 状态：完成 COM6 板端闭环；COM5 已在后续任务中恢复并重刷
- 日期：2026-08-15
- 任务目标：
  - 深挖 `/refmem/app_model.rmtp` 通过 Storage SCPI 写入时反复撞到 `RUNNING`、最终 `RENAME_FAILED` 的原因。
  - 判断是否由最小系统板 RefMem PIO-SPI 与 SD 共享 SPI 引起，并给出结构性修复。
- 结论：
  - 当前板级配置中 SD 与 LCD 共用 `BOARD_SPI_PORT`，实际为 `spi1` 的 SCK/MOSI/MISO 总线；RefMem 最小 transport 使用 PIO-SPI GPIO16-19，不是 SD 使用的硬件 SPI 总线。
  - `RUNNING` 的主要原因不是 RefMem PIO-SPI 与 SD 物理共线，而是 StorageAO 服务优先级、boot snapshot 插队和 UI 刷屏共享 SPI 资源叠加。
  - `RENAME_FAILED` 发生在 CRC 已匹配后的临时文件替换阶段，说明 SCPI 分块上传和内存缓冲完整，失败集中在 FatFs rename/replace 语义或目标文件残留。
  - `SYSTem:REFMEM:LOAD:SD` 的挂死风险来自 SCPI 回调栈上 8 KB package buffer；该缓冲已迁移为模块静态加载缓冲，避免命令执行时压垮 SCPI task stack。
  - `SYSTem:REFMEM:LOAD:SD` 返回固定 IO_ERROR/REJECTED 的根因是 RefMem load 只等待 StorageAO job 约 200 ms；真实 manifest/package 分段读取会超过该窗口，导致 `RUNNING` 被误判为失败。等待窗口已与 Storage SCPI 统一到 10000 loops。
  - UI 不是旁路代码：`UiAO`/`LcdFb` 纳入 HAOFV 资源治理，UI 只读取 snapshot 并通过 ResourceArbiter 获取 `SPI0|LCD`，StorageAO 获取 `SPI0|SD`，板级 port 负责共享 SPI 模式和 CS 准备。
- 完成内容：
  - `storage_manager_service()` 改为显式 Storage job 优先于 boot snapshot/system pack 自动动作，避免 SCPI 已投递 job 后被启动快照插队。
  - FreeRTOS 任务优先级调整：`storage` 从 2 提到 3，`ui` 从 2 降到 1，避免 UI 与 StorageAO 在 `SPI0|LCD/SD` 资源上同级频繁碰撞。
  - `ui_manager_service()` 在 StorageAO job 处于 `QUEUED/RUNNING` 时跳过本轮刷新，让 SD 文件操作优先完成。
  - `StatusUI` 对 `SPI0|LCD` 使用 owner 标记 `StatusUI`，后续 ResourceArbiter 快照可直接看到冲突 holder。
  - `fatfs_port_write_text_file_atomic()` 与 `fatfs_port_write_binary_file_atomic()` 收敛到公共 bytes 写入 helper；临时文件 rename 失败后增加直写目标文件兜底，避免 FatFs 覆盖式 rename 或旧文件残留导致 StorageAO 卡死。
  - `SCPI_STORAGE_JOB_WAIT_LOOPS` 从 2000 增加到 10000；`tools/refmem_pack_write/refmem_pack_write.py` 默认 timeout 从 3 s 增加到 10 s，防止设备端仍在合法写入时 PC 侧先超时。
  - `SCPI_REFMEM_LOAD_JOB_WAIT_LOOPS` 从 200 增加到 10000；`tools/refmem_pack_write/refmem_pack_write.py` 增加 `--load-timeout`，默认 60 s，专门覆盖 `LOAD:SD` 的长耗时 staged 路径。
  - 新增 `tools/scpi_common/scpi_serial.py`，固化 USB CDC 串口打开、settle、输入/输出缓冲清理、idle-gap 读行和 finally close 生命周期管理；`scpi_query`、`storage_scpi_validate`、`refmem_pack_write` 已接入。
  - 新增 `tools/storage_file_upload/storage_file_upload.py`，作为通用 Storage SCPI 文件上传工具，支持多文件、自动建目录、分片写入、写后 `FILE:INFO?` 校验。
- 验证结果：
  - `python -m py_compile tools\scpi_common\scpi_serial.py tools\storage_file_upload\storage_file_upload.py tools\refmem_pack_write\refmem_pack_write.py tools\scpi_query\scpi_query.py tools\storage_scpi_validate\storage_scpi_validate.py` 通过。
  - `python -m pytest tests\python\test_refmem_pack_build.py -q` 通过，2 passed；pytest cache 写入 `.pytest_cache` 因权限被拒绝，只产生 warning。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，最终验证 build id `20260815104540`，package CRC `0x1995734A`。
  - OTA/commit COM6 到 build `20260815104540` 通过。
  - COM6 通用 Storage CRUD 使用唯一目录重跑通过；先前 COM5 directory rename 失败确认为历史目标目录残留，不是 `RUNNING`。
  - COM6 写入 HIL manifest（只要求 profile/mission/cal/refmem 四个小文件）后，`SYSTem:SD:MANifest?` 返回 `"OK",1,"RP2350_TRIG","rp2350_trig","20260815103459",4,0`。
  - COM6 执行 `python -u tools\refmem_pack_write\refmem_pack_write.py COM6 --timeout 10 --load-timeout 60 --chunk 256 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815103459\refmem\app_model.rmtp --out-dir build-rtos-multicore-smoke\refmem_pack_write_COM6_20260815104540_load_wait_fixed` 通过；`SYSTem:REFMEM:LOAD:SD` 返回 `STAGED`，staging package CRC `2994534464`，Storage job `DONE`，错误队列为 0。
  - 发现通用 Storage FILE 写事务仍是 RAM buffer 型，`STORAGE_MANAGER_WRITE_BUFFER_MAX_BYTES=8192`，不能用该接口直接上传 523 KB OTA package；大文件 OTA 仍应走 OTA AO/portable OTA 流式入口，Storage 文件接口后续若要支持大文件需设计流式后端。
- 后续动作：
  - 将 `LOAD:SD` 37 s 耗时继续优化：优先减少逐 128 B 文件读 job 的重复调度，或在 StorageAO 提供 bounded stream read job。
  - 继续确认无参 `SYSTem:REFMEM:LOAD:SD` 的错误队列行为，区分真实 parser 污染和 PC 侧命令串扰。
- 关联文件：
  - `application/src/app_tasks.c`
  - `components/storage_manager/src/storage_manager.c`
  - `components/ui_manager/src/ui_manager.c`
  - `components/ui_manager/src/status_ui.c`
  - `middleware/fatfs_port/src/fatfs_port.c`
  - `middleware/scpi_port/src/scpi_storage_commands.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/scpi_common/scpi_serial.py`
  - `tools/refmem_pack_write/refmem_pack_write.py`
  - `tools/storage_file_upload/storage_file_upload.py`

### REFMEM-TASK-20260815-026 - Complete RMTP canonical table payloads

- 状态：完成代码侧基础闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `.rmtp` 中剩余 5 张 canonical 表从 64 字节占位 payload 升级为真实固定 u32 wire payload。
  - 让 `RefMemTableRegistry` 对全 9 张表执行 owner validation，同时保持 active image 切换阻断，不伪激活。
- 完成内容：
  - `tools/refmem_table_image/refmem_table_image.py` 新增 `FbInstance`、`EventLink`、`DataLink`、`DeploymentGate`、`ConnectionQuality` payload 生成。
  - `FbInstance` 的 `instance_name` 和 `DataLink` 的 `slot_path` 不写入 C 指针，`.rmtp` 使用 `name_hash` / `slot_path_hash` 作为稳定 wire 字段；可读说明仍放在 idx/json。
  - `refmem_table_registry.c` 的 table image size 改为 wire size，避免把 C struct pointer size 当作 package layout。
  - `RefMemTableRegistry` 增加 4-8 号表 parser 和字段级 owner validation，成功 package 的 `owner_validated_table_mask` 覆盖 `REFMEM_APP_TABLE_MASK_ALL`。
  - 单元测试构造器从“前 4 张真实 + 后 5 张 placeholder”升级为全 9 张 contract package；staging descriptor 预期从 `CRC_OK` 更新为 `OWNER_OK`。
- 验证结果：
  - `python -m py_compile tools\refmem_table_image\refmem_table_image.py tools\refmem_pack_build\refmem_pack_build.py tools\sd_fs_build\sd_fs_build.py` 通过。
  - `python -m pytest tests\python\test_refmem_pack_build.py -q` 通过，2 passed；pytest cache 写入 `.pytest_cache` 因权限被拒绝，只产生 warning。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `python tools\refmem_pack_build\refmem_pack_build.py --output-dir build-rtos-multicore-smoke\refmem_pack_full_tables` 生成 9 表 package，size `4800`，CRC `0xEFAF178F`。
- 后续闭环：
  - 继续运行完整 host/unit/docs/build/HIL 验证；通过后再进入真实 staging/active/rollbackable image buffer。
- 关联文件：
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tools/refmem_table_image/refmem_table_image.py`
  - `tools/refmem_pack_build/refmem_pack_build.py`
  - `tests/unit/test_refmem_table_registry.c`
  - `tests/python/test_refmem_pack_build.py`

### REFMEM-TASK-20260815-025 - Block descriptor-only table activation

- 状态：完成基础纠偏
- 日期：2026-08-15
- 任务目标：
  - 消除 `RefMemTableRegistry` 只切 descriptor/CRC 就宣称 active image 已替换的架构风险。
  - 在真实 active/staging/rollbackable table buffer 未落地前，明确阻断 activation。
- 完成内容：
  - 新增 `REFMEM_TABLE_ACTIVATE_ERR_IMAGE_NOT_LOADED`。
  - `refmem_table_registry_activate_staging()` 在 validated staging 和 activation gate 都通过后，仍返回 false，保留 staging descriptor，并写入 `IMAGE_NOT_LOADED`；不修改 active descriptor、rollbackable descriptor 或 active entry CRC。
  - 单元测试从“成功切 descriptor”改为验证“真实 table image 未加载时拒绝伪激活”。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815085709`，package CRC `0x1C61EB50`。
  - COM5 执行 `python tools\ota_boot_commit\ota_boot_commit.py COM5 --expected-build 20260815085709 --out-dir build-rtos-multicore-smoke\ota_commit_COM5_20260815085709` 通过，`SYSTem:FW:BUILD?` 返回 `20260815085709`，`SYSTem:OTA:COMMit` 返回 `"OK"`，错误队列为 `0,"No error"`。
  - COM6 执行 `python tools\ota_boot_commit\ota_boot_commit.py COM6 --expected-build 20260815085709 --out-dir build-rtos-multicore-smoke\ota_commit_COM6_20260815085709` 通过，`SYSTem:FW:BUILD?` 返回 `20260815085709`，`SYSTem:OTA:COMMit` 返回 `"OK"`，错误队列为 `0,"No error"`。
- 后续闭环：
  - 后续必须引入真实 staging/active/rollbackable table image buffer 和业务表切换后，才能恢复 activation 成功路径。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_table_registry.h`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tests/unit/test_refmem_table_registry.c`

### REFMEM-TASK-20260815-024 - LOAD:SD via RefMem command slot

- 状态：完成首版闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `SYSTem:REFMEM:LOAD:SD` 的 RefMem staging 和 TableRegistry package validation 收敛到 `DistributedRefMemAO` command slot。
  - 保持 StorageAO/RefMemAO 职责边界：SD/FatFs/manifest/file read 属于 StorageAO，RefMem staging/ACK/NACK 属于 RefMemAO。
- 完成内容：
  - 新增 `REFMEM_COMMAND_TYPE_TABLE_PACKAGE_STAGE` 和 `distributed_refmem_stage_sd_system_pack()`。
  - `SYSTem:REFMEM:LOAD:SD` 改为先通过 StorageManager 获取 manifest/package validation 摘要，再调用 RefMem intent API；SCPI 不再直接调用 `refmem_application_model_stage_sd_system_pack()` 或 `refmem_table_registry_stage_package_validation()`。
  - Storage 前置 job busy/incomplete 也转换为 `REJECTED` load snapshot 和 command NACK，避免写命令无 completion。
  - `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 增加 `LOAD:SD` 后的 `SYSTem:COMMand:ACK?` 验证，确认 command type 为 `TABLE_PACKAGE_STAGE`。
- 验证结果：
  - `python -m py_compile tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py` 通过。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815084755`，package CRC `0xE5D9FCC9`。
  - COM5/COM6 OTA 写入、boot 和 commit 到 build `20260815084755` 通过，错误队列均为 `0,"No error"`。
  - COM5 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM5 --out-dir build-rtos-multicore-smoke\refmem_load_COM5_20260815084755_sd_command_slot` 通过；当前 SD package 未有效 staging，`LOAD:SD` 返回 `REJECTED`，`SYSTem:COMMand:ACK?` 返回 `TABLE_PACKAGE_STAGE` NACK。
  - COM6 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM6 --out-dir build-rtos-multicore-smoke\refmem_load_COM6_20260815084755_sd_command_slot` 通过；结果同 COM5。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
- 后续闭环：
  - 继续扩展 SD HIL：覆盖无 SD、manifest 缺失、manifest OK 且 package valid 三类路径，并校验 TableRegistry per-table staging CRC。
  - `LOAD:SD` 仍不执行 active image 切换；P0 仍需实现真实 active/staging/rollbackable table image。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_command.h`
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py`

### REFMEM-TASK-20260815-023 - LOAD:BOARD via RefMem command slot

- 状态：完成首版闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `SYSTem:REFMEM:LOAD:BOARD` 从 SCPI 直接调用 BoardCapability staging API 收敛到 `DistributedRefMemAO` command slot / staging 路径。
  - 验证 `board_id` 只作为板卡/profile 候选 payload，不被误用为 A0-A7 target slot。
- 完成内容：
  - 新增 `REFMEM_COMMAND_TYPE_BOARD_CAPABILITY_STAGE` 和 `distributed_refmem_stage_board_capability()`。
  - `SYSTem:REFMEM:LOAD:BOARD` 改为调用 RefMem intent API；`DistributedRefMemAO` 原子清理已完成 command、post `BOARD_CAPABILITY_STAGE`、由本地 RefMem owner take、调用 `refmem_application_model_stage_scpi_board_capability()`，随后 ACK/NACK。
  - `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 增加 BoardCapability 正向/负向验证：有效 board 候选应 ACK，非法 `board_id=16` 应 NACK。
  - 修正验证脚本参数格式：当前 SCPI 数字解析不接受 `0x...`，脚本改为十进制参数，避免把 parser `-101 Invalid character` 误判为 RefMem 卡死。
- 验证结果：
  - `python -m py_compile tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py` 通过。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815082900`，package CRC `0xF913F583`。
  - COM5/COM6 OTA 写入、boot 和 commit 到 build `20260815082900` 通过，错误队列均为 `0,"No error"`。
  - COM5 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM5 --skip-sd --out-dir build-rtos-multicore-smoke\refmem_load_COM5_20260815082900_board_command_slot_r3` 通过。
  - COM6 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM6 --skip-sd --out-dir build-rtos-multicore-smoke\refmem_load_COM6_20260815082900_board_command_slot_r3` 通过。
- 后续闭环：
  - `LOAD:BOARD` 仍是单条 staging snapshot，不是完整 BoardCapabilityTable active/staging/rollbackable image；P0 仍需升级为真实表镜像。
  - `SYSTem:REFMEM:LOAD:SD` 仍需接入 command slot，并保持 StorageAO/RefMemAO 职责分离。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_command.h`
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py`

### REFMEM-TASK-20260815-022 - LOAD:NODE via RefMem command slot

- 状态：完成首版闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `SYSTem:REFMEM:LOAD:NODE` 从 SCPI 直接调用 application staging API 收敛到 `DistributedRefMemAO` command slot / staging 路径。
  - 验证 `SYSTem:COMMand:ACK?` 能直接观察 `LOAD:NODE` 的 `NODE_LOAD_STAGE` completion。
- 完成内容：
  - 新增 `distributed_refmem_stage_node_load()`，由 RefMemAO 原子清理已完成 command、post `NODE_LOAD_STAGE`、take target slot、调用 `refmem_application_model_stage_scpi_node_config()`，随后 ACK/NACK。
  - 修复 command slot 抢占竞态：`clear completed command -> allocate seq -> post new command` 现在在 RefMemAO 内部同一个 critical section 完成，避免 `system_manager` 在间隙重新发布 `CONFIG_ACTIVATE`。
  - `SYSTem:REFMEM:LOAD:NODE` 改为调用 RefMem intent API；返回字段保持原 `STAGED/REJECTED + LOAD:STATus` 格式。
  - `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 增加 `SYSTem:COMMand:ACK?` 校验，并把 table count / active mask 更新为当前 9 张表 / `0x1FF`。
- 验证结果：
  - `python -m py_compile tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py` 通过。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815081416`，package CRC `0x5D6D4C16`。
  - COM5/COM6 OTA 写入、boot 和 commit 到 build `20260815081416` 通过，错误队列均为 `0,"No error"`。
  - COM5 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM5 --skip-sd --out-dir build-rtos-multicore-smoke\refmem_load_node_COM5_20260815081416_command_slot` 通过。
  - COM6 执行 `python tools\refmem_scpi_load_validate\refmem_scpi_load_validate.py COM6 --skip-sd --out-dir build-rtos-multicore-smoke\refmem_load_node_COM6_20260815081416_command_slot` 通过。
  - COM5/COM6 再次执行 `tools\model_turntable_load_validate\model_turntable_load_validate.py` 通过，确认共享 post helper 未破坏 `CONFigure:MODEl:TURNtable:LOAD`。
- 后续闭环：
  - `LOAD:NODE` 仍是单条 staging snapshot，不是完整 NodeLoadTable active/staging/rollbackable image；P0 仍需升级为真实表镜像。
  - `SYSTem:REFMEM:LOAD:SD` 和 `SYSTem:REFMEM:LOAD:BOARD` 仍需接入 command slot。
- 关联文件：
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py`

### REFMEM-TASK-20260815-021 - Generic command ACK/NACK SCPI view

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 增加通用 command slot 维护视图，避免继续把 `SYSTem:CONFigure:ACK?` 当成所有 command 的查询入口。
  - 保持单一 ACK/NACK 事实源：新接口直接读取 `DistributedRefMemAO` 的 `AckCommandSlot` snapshot，不另建状态。
- 完成内容：
  - 新增 `SYSTem:COMMand:ACK?`，返回 schema version、state、command seq、source/target、command type/class、payload 摘要、taken/ACK/NACK/busy/timeout flags、last reason、evidence、clear seq 和 last completed seq。
  - 新增 `SYSTem:COMMand:NACK? [reason_id]`，默认读取当前 command snapshot 的 `last_reason`，也可按 reason id 查询 RefMem command reason 表。
  - `SYSTem:CONFigure:ACK? / NACK?` 继续只作为 `CONFIG_ACTIVATE` 的配置门禁兼容视图。
  - `tools/model_turntable_load_validate/model_turntable_load_validate.py` 增加 `SYSTem:COMMand:ACK?` 和 `SYSTem:COMMand:NACK?` 检查，验证 `NODE_LOAD_STAGE` command 的 ACK 事实。
- 验证结果：
  - `python -m py_compile tools\model_turntable_load_validate\model_turntable_load_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `python tools\docs_check\docs_check.py` 通过。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815080110`，package CRC `0x093AC676`。
  - COM5/COM6 OTA 写入、boot 和 commit 到 build `20260815080110` 通过，错误队列均为 `0,"No error"`。
  - COM5 执行 `python tools\model_turntable_load_validate\model_turntable_load_validate.py COM5 --expected-build 20260815080110 --slot 1 --output 0 --out-dir build-rtos-multicore-smoke\model_turntable_load_COM5_20260815080110_command_ack` 通过。
  - COM6 执行 `python tools\model_turntable_load_validate\model_turntable_load_validate.py COM6 --expected-build 20260815080110 --slot 1 --output 0 --out-dir build-rtos-multicore-smoke\model_turntable_load_COM6_20260815080110_command_ack` 通过。
- 后续闭环：
  - 后续 START/STOP、activation、resource job 接入 command slot 后，统一用 `SYSTem:COMMand:*` 验证 command completion；配置上位机仍可读取 `SYSTem:CONFigure:*` 兼容视图。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/model_turntable_load_validate/model_turntable_load_validate.py`

### REFMEM-TASK-20260815-020 - ModelTurntable LOAD via RefMem command slot

- 状态：完成首版闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `CONFigure:MODEl:TURNtable:LOAD <slot_id>,<output_index>` 从 SCPI 直接业务调用收敛到 RefMem command slot / NodeLoad staging 路径。
  - 保持 HAOFV 边界：SCPI 只提交 intent；`DistributedRefMemAO` 负责 command、staging 和 ACK/NACK；实际加载由 `ModelTurntableAO` registered owner 执行。
- 完成内容：
  - 新增 `distributed_refmem_register_node_load_owner()`，按 `instance_id` 注册 AO/FB owner 回调；`distributed_refmem.c` 不再直接 include `model_turntable.h`。
  - `ModelTurntableAO` 在初始化时注册 `Template.ModelTurntableAO` 的 NodeLoad owner，owner 回调内部执行 `model_turntable_load()`。
  - `CONFigure:MODEl:TURNtable:LOAD` 改为调用 `distributed_refmem_stage_model_turntable_load()`；该入口 post `NODE_LOAD_STAGE` command、写入 NodeLoad staging snapshot、由 owner 执行加载，并 ACK/NACK command slot。
  - `system_manager` 的 `SYSTem:CONFigure:ACK?` 兼容视图只读取 `CONFIG_ACTIVATE` command，避免模型 LOAD command 污染配置门禁 ACK。
  - 新增 `tools/model_turntable_load_validate/model_turntable_load_validate.py`，固化 build id、默认未加载、LOAD、staging snapshot、配置 ACK 兼容视图和错误队列检查。
- 验证结果：
  - `python -m py_compile tools\model_turntable_load_validate\model_turntable_load_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815075135`，package CRC `0x3F00DD7D`。
  - COM5/COM6 OTA 写入、boot 和 commit 到 build `20260815075135` 通过，错误队列均为 `0,"No error"`。
  - COM5 执行 `python tools\model_turntable_load_validate\model_turntable_load_validate.py COM5 --expected-build 20260815075135 --slot 1 --output 0 --out-dir build-rtos-multicore-smoke\model_turntable_load_COM5_20260815075135` 通过。
  - COM6 执行 `python tools\model_turntable_load_validate\model_turntable_load_validate.py COM6 --expected-build 20260815075135 --slot 1 --output 0 --out-dir build-rtos-multicore-smoke\model_turntable_load_COM6_20260815075135` 通过。
- 后续闭环：
  - 当前仍是首版 staging snapshot，不是完整 NodeLoadTable active/staging/rollbackable image；P0 仍需把 `CONFigure:MODEl:TURNtable:LOAD` 升级为真实 table image、owner validation、SlotClaimMap、RealtimeCapabilityContract、DeploymentGate 和 rollback。
  - 将 `ModelVnaAO`、`LinkSwitcherAO` 等其余可加载实例接入同一 NodeLoad owner 注册和 command slot 路径。
- 关联文件：
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/model_turntable/src/model_turntable.c`
  - `components/system_manager/src/system_manager.c`
  - `middleware/scpi_port/src/scpi_model_commands.c`
  - `tools/model_turntable_load_validate/model_turntable_load_validate.py`

### REFMEM-TASK-20260815-019 - SystemManager config ACK maps to RefMem command slot

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 消除 `SYSTem:CONFigure:ACK?` 由 SystemManager 本地字段单独拼装造成的 ACK/NACK 事实分裂。
  - 保持 SCPI 兼容查询不变，同时让底层 ACK/NACK 来自 RefMem `AckCommandSlot` snapshot。
- 完成内容：
  - `DistributedRefMemAO` 增加 command slot owner API：reason table CRC、post、ACK、NACK、timeout、clear 和 snapshot。
  - `system_manager` 在配置 RUN gate 初始化和 service 中发布 `CONFIG_ACTIVATE` command snapshot，并按 gate 结果写入 ACK 或 NACK。
  - 修正 quality gate 变坏时 ACK/NACK flags 可能仍停留在旧全 ACK 的问题；现在 reject 时 `SYSTem:CONFigure:ACK?` 返回 `ack=0,nack=target`。
  - `tools/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py` 增加 config ACK 正向、负向和恢复后的全 ACK 检查。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过 host GCC 断言执行。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815061144`，package CRC `0xAB77E7E3`。
  - COM5/COM6 OTA 到 build `20260815061144` 并 commit 通过。
  - COM5/COM6 执行 `multicore_board_validate.py --tests build_id config_gate_status ack_reason_and_run_policy` 均通过。
  - COM5 执行 `refmem_quality_gate_hil_validate.py` 通过，确认 TDMA timeout 后 config gate reject、config ACK reject，并通过 OTA 恢复 ready / full ACK。
  - COM6 在 COM5 负向验证后再次执行配置 ACK 相关 HIL 查询通过。
- 后续闭环：
  - 将 `CONFigure:MODEl:*:LOAD` 接入 command slot，让 SCPI 只 post `NODE_LOAD_STAGE` 或 `CONFIG_STAGE`。
  - 评估是否新增 `SYSTem:COMMand:ACK? / NACK?` 作为通用 command slot 维护视图。
- 关联文件：
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/distributed_refmem/inc/refmem_command.h`
  - `components/distributed_refmem/src/refmem_command.c`
  - `components/system_manager/src/system_manager.c`
  - `tools/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py`

### REFMEM-TASK-20260815-018 - Command slot foundation

- 状态：完成基础件
- 日期：2026-08-15
- 任务目标：
  - 将 `AckCommandSlot` 从文档契约落到可执行 C 基础件。
  - 为后续 `CONFigure:MODEl:*:LOAD`、NodeLoad staging、配置 activation 和跨节点 ACK/NACK 收敛提供统一数据面。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_command.h` 和 `components/distributed_refmem/src/refmem_command.c`。
  - 实现 `try_post`、`try_take`、`ack`、`nack`、`mark_timeout`、`clear` 和 seqlock snapshot。
  - 字段与 `REFMEM_COMMAND` / `REFMEM_ACK_NACK` payload 对齐，提供 `refmem_command_to_sync_command_payload()` 与 `refmem_command_to_sync_ack_payload()`。
  - TAKE 只做目标、epoch/run_id 和 payload CRC 检查；耗时动作仍必须由 AO/FB service tick 执行，避免违反 HAOFV owner 边界。
  - 新增 `tests/unit/test_refmem_command.c` 和 `tools/tests/run_refmem_command_tests.ps1`，并纳入 `tools/tests/run_host_unit_tests.ps1`。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过 host GCC 断言执行。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815060551`，package CRC `0x06B0C911`。
- 后续闭环：
  - 将模型加载动作接入 command slot：SCPI accepted 后只 post `NODE_LOAD_STAGE` 或 `CONFIG_STAGE`，由 RefMem owner ACK/NACK。
  - 将现有 `system_manager` 配置 ACK 映射到同一 command snapshot，避免 ACK/NACK 事实分裂。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_command.h`
  - `components/distributed_refmem/src/refmem_command.c`
  - `tests/unit/test_refmem_command.c`
  - `tools/tests/run_refmem_command_tests.ps1`

### REFMEM-TASK-20260815-017 - Quality gate negative HIL script

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 将 `TDMA timeout -> RefMem quality gate reject -> SystemManager config gate reject -> OTA restore` 固化成可重复 HIL 脚本。
  - 避免后续用手工 SCPI 命令制造 TDMA timeout 后忘记恢复板卡运行态。
- 完成内容：
  - 新增 `tools/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py`。
  - 脚本读取初始 `SYSTem:CONFigure:STAT?` 和 `SYSTem:REFMEM:SYNC:TDMA:STATus?`，确认初始 config gate ready。
  - 脚本发送短窗口 `SYSTem:REFMEM:SYNC:TDMA:RX` 且不发 TX，轮询 TDMA timeout，并确认 `SYSTem:CONFigure:STAT?` 进入 `ready=0, gate_state=2`。
  - 支持 `--restore-package` 和 `--expected-build`，在负向验证后调用 OTA send + boot/commit 恢复板卡，并再次确认 config gate ready。
  - 修正 `SYSTem:CONFigure:STAT?` 解析：首字段 build id 是带引号数字字符串，不能被当成 ready 字段。
- 验证结果：
  - `python -m py_compile tools\refmem_quality_gate_hil_validate\refmem_quality_gate_hil_validate.py` 通过。
  - COM5 执行 `python tools\refmem_quality_gate_hil_validate\refmem_quality_gate_hil_validate.py COM5 --expected-build 20260815053147 --restore-package build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --out-dir build-rtos-multicore-smoke\refmem_quality_gate_COM5_20260815053147_scripted_r3` 通过。
  - 报告路径：`build-rtos-multicore-smoke\refmem_quality_gate_COM5_20260815053147_scripted_r3\summary.json`。
- 关联文件：
  - `tools/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py`

### REFMEM-TASK-20260815-016 - Quality gate RUN gate integration

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 将 `refmem_quality_evaluate_deployment_gate()` 接入产品 config RUN gate。
  - 保持 `SystemManager` 只消费 `DistributedRefMemAO` 的 quality gate 结果，不直接读取 TDMA mailbox 或 physical adapter counter。
- 完成内容：
  - `DistributedRefMemAO` 增加 `distributed_refmem_quality_gate_ready()`，内部从 core1 TDMA snapshot 派生 runtime quality entry，并调用 quality gate evaluator。
  - `system_manager` 的 config gate 同时检查 distributed config、SlotClaim gate 和 RefMem quality gate；任一失败时 `ready=0`、`gate_state=2`、ACK 清空/NACK target mask。
  - 修复 `distributed_refmem.h` 与 `refmem_quality.h` 的 include 循环，公开 API 只暴露布尔 quality gate，不泄漏 quality table 内部类型。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815053147`，package CRC `0x190D53A9`。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过 host GCC 断言执行。
  - COM5/COM6 OTA 提交通过，均返回 `SYSTem:FW:BUILD? => "20260815053147"`。
  - 正向：COM5/COM6 查询 `SYSTem:CONFigure:STAT?` 均返回 `ready=1, gate_state=1, target_mask=15, ack_flags=15, nack_flags=0`。
  - 负向：COM5 执行 `SYSTem:REFMEM:SYNC:TDMA:RX 1000,25000000,16,17,18,23` 且不发 TX 后，`TDMA:STATus?` 显示 `state=5, timeout_count=1, last_result=3, last_error=3`，`SYSTem:CONFigure:STAT?` 变为 `ready=0, gate_state=2`。
  - 恢复：COM5 重新 OTA 同一 package 后恢复 `ready=1, gate_state=1`；COM6 保持 `ready=1, gate_state=1`。
- 后续闭环：
  - quality gate 负向流程已在 `REFMEM-TASK-20260815-017` 固化为 HIL 脚本。
- 关联文件：
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/system_manager/src/system_manager.c`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`

### REFMEM-TASK-20260815-015 - Quality gate evaluator

- 状态：完成基础件，SystemManager 接入待继续
- 日期：2026-08-15
- 任务目标：
  - 在 TDMA runtime quality entry 已进入 `DistributedConnectionQualityTable` 派生视图后，提供表驱动的 `DeploymentGate.QUALITY` 消费入口。
  - 避免 RUN gate 直接遍历 adapter、core1 mailbox 或 SCPI 查询结果。
- 完成内容：
  - `refmem_quality.h/.c` 增加 `refmem_quality_gate_threshold_t`、`refmem_quality_gate_reason_t` 和 `refmem_quality_evaluate_deployment_gate()`。
  - evaluator 输入 `refmem_quality_runtime_table_t` 和阈值，输出 `refmem_deployment_gate_entry_t`，包含 `check_id=REFMEM_APP_GATE_QUALITY`、pass/reject 状态、reject reason、node/slot 和 evidence index。
  - 当前 reason 覆盖 bad argument、empty runtime table、CRC、stale、late、drop、timeout 和 last_error。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 明确 `DeploymentGate.QUALITY` 只能消费 quality evaluator 输出，不能直接读取底层 adapter 或 core1 mailbox。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过 host GCC 断言执行。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815052521`，package CRC `0x346FB00F`。
- 后续闭环：
  - `system_manager` config RUN gate 接入和 TDMA timeout 板端负向验证已在 `REFMEM-TASK-20260815-016` 完成。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_quality.h`
  - `components/distributed_refmem/src/refmem_quality.c`
  - `tests/unit/test_refmem_quality.c`

### REFMEM-TASK-20260815-014 - TDMA quality runtime mapping

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 将 core1 realtime TDMA service 的 window timeout、DMA/physical overrun、missed window 和 physical adapter error 纳入 `DistributedConnectionQualityTable` 派生运行态视图。
  - 保持 active static `ConnectionQualityTable` 只作为契约/CRC 来源，不被 SCPI 或维护 bridge 热写。
- 完成内容：
  - `refmem_quality.h/.c` 增加 TDMA service runtime entry，`quality_id=0x54444D41`，scope 为 `REFMEM_APP_QUALITY_TRANSPORT_ADAPTER`。
  - 映射规则：TDMA `timeout_count -> timeout_count`，`reject_count -> late_count`，`overrun_count -> drop_count`，`last_error -> last_error`，`intent_seq/completed_seq -> seq_expected/seq_last`。
  - `SYSTem:REFMEM:QUALity?` 查询时读取 `distributed_refmem_get_realtime_tdma()` snapshot，并把 TDMA entry 放在 index 1；index 0 仍为本地 PIO SPI adapter 诊断 entry，后续 remote QUALITY entry 依次追加。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 补齐 `TRANSPORT_ADAPTER` scope 和 TDMA 派生映射规则。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过 host GCC 断言执行。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过 host GCC 断言执行。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815051252`，package CRC `0x5B12BF64`。
  - COM5/COM6 OTA 提交通过，均返回 `SYSTem:FW:BUILD? => "20260815051252"`。
  - COM5/COM6 查询 `SYSTem:REFMEM:QUALity? 1` 可见 TDMA runtime entry；HIL 前 entry_count 为 2，HIL 后 entry_count 为 3（追加 remote QUALITY entry）。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --transport tdma --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815051252_tdma_quality_mapping` 通过。
- 还需完成：
  - 将 TDMA quality entry 接入 DeploymentGate/RUN gate evidence 消费规则。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_quality.h`
  - `components/distributed_refmem/src/refmem_quality.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_quality.c`

### REFMEM-TASK-20260815-013 - P4.5 SPI frame legacy removal

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 在 TDMA HIL 通过后删除 `SYSTem:REFMEM:SYNC:SPI:*` 的帧级阻塞命令，避免后续继续通过 core0/SCPI 直驱 RefMem frame transport。
  - 保留 `SPI:ARM/DISarm/STATus?/LINE:*/RAW:*` 作为 bring-up 诊断入口。
- 完成内容：
  - 从 SCPI 命令表删除 `SYSTem:REFMEM:SYNC:SPI:RX?`、`SPI:HELLo`、`SPI:EPOCh`、`SPI:DELTa`、`SPI:ACK`、`SPI:FENCe`、`SPI:QUALity:FRAMe`。
  - 删除上述命令实现和 `scpi_refmem_sync_spi_send_result()`，消除 core0 直发 RefMem frame 的维护路径。
  - `tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 改为 TDMA-only frame transport；RAW 仍作为线序/物理链路诊断。
  - HIL frame plan 改为直接使用 `SYSTem:REFMEM:SYNC:HELLo?/EPOCh?/DELTa?/ACK?/FENCe?/QUALity:FRAMe?` 生成 frame，再通过 `TDMA:TX/RX/FRAMe?` 完成两板交换。
- 验证结果：
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815045059`，package CRC `0x732BD11F`。
  - COM5/COM6 OTA 提交通过，均返回 `SYSTem:FW:BUILD? => "20260815045059"`。
  - Negative check：`SYSTem:REFMEM:SYNC:SPI:HELLo` 和 `SYSTem:REFMEM:SYNC:SPI:RX?` 已返回 `Undefined header`，确认旧 SPI frame 入口不再注册。
  - Positive check：`SYSTem:REFMEM:SYNC:SPI:ARM`、`SYSTem:REFMEM:SYNC:SPI:RAW:TX` 和 `SYSTem:REFMEM:SYNC:TDMA:STATus?` 仍可用。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --transport tdma --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815045059_tdma_spi_frame_removed` 通过。
  - HIL 报告路径：`build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815045059_tdma_spi_frame_removed\refmem_spi_hil_report.json`。
- 后续闭环：
  - TDMA window timeout、DMA overrun、missed window 和 physical adapter error 到 `DistributedConnectionQualityTable` 的正式映射已在 `REFMEM-TASK-20260815-014` 完成。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py`

### REFMEM-TASK-20260815-012 - P4.5 COM5/COM6 core1 TDMA HIL 闭环

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 将 RefMem Sync 的两板物理验证从 SCPI 阻塞帧级命令收敛到 core1 realtime TDMA service。
  - 证明 PC 只投递 RX/TX intent 和读取 snapshot，`HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 帧通过真实 PIO+DMA 物理链路完成。
- 完成内容：
  - 修正 `SYSTem:REFMEM:SYNC:TDMA:TX`：SCPI 层只解码 hex 并提交 TDMA intent，不再在 core0 直接调用 physical adapter 发送帧。
  - 固化 HIL 脚本的 TDMA hex 字符串参数，避免未加引号的 A-F hex 被 SCPI lexer 当作数字/指数解析。
  - 将 `refmem_spi_physical_adapter_receive()` 拆成 begin/poll 异步 RX 基础能力；blocking receive 保持兼容并复用同一解析逻辑。
  - 将 TDMA RX 从长时间阻塞 core1 改为每轮 core1 loop poll 一次，`PENDING` 时保持 ARMED/ACCEPTED 快照，不提前写 completed_seq。
  - 将 `REFMEM_REALTIME_TDMA_FRAME_MAX` 对齐到 RefMem Sync 协议 MTU：`REFMEM_SYNC_FRAME_HEADER_SIZE + REFMEM_SYNC_FRAME_PAYLOAD_MAX`。
  - HIL 脚本在 TDMA exchange 前清理两端上一次 intent，并用 `TDMA:STATus?` 做 receiver arm gate。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815043100`，package CRC `0x49E7D77C`。
  - COM5/COM6 OTA 提交通过，均返回 `SYSTem:FW:BUILD? => "20260815043100"`。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --transport tdma --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815043100_tdma_mtu` 通过。
  - HIL 报告路径：`build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815043100_tdma_mtu\refmem_spi_hil_report.json`。
  - 报告中 `A_RAW_B`、`B_RAW_A`、双向 `HELLO/EPOCH/DELTA/ACK/FENCE/QUALITY` 全部 passed；最终 TDMA snapshot 显示 A/B `ready_count=12`、`timeout_count=0`、`overrun_count=0`、`reject_count=0`。
- 还需完成：
  - 将 `SYSTem:REFMEM:SYNC:SPI:*` 帧级阻塞命令降级为 legacy/diagnostic 或删除，仅保留 line/raw bring-up 必要入口。
  - 将 TDMA window timeout、DMA overrun、missed window 和 physical adapter error 正式映射到 `DistributedConnectionQualityTable`。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_realtime_tdma.h`
  - `components/distributed_refmem/inc/refmem_spi_physical_adapter.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/distributed_refmem/src/refmem_realtime_tdma.c`
  - `components/distributed_refmem/src/refmem_spi_physical_adapter.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py`
- 下一步：
  - 收敛 `SYSTem:REFMEM:SYNC:SPI:*` 帧级阻塞命令，并把 TDMA 质量计数并入 RefMem quality/evidence。

### REFMEM-TASK-20260815-011 - P4.5 core1 TDMA service contract 收敛

- 状态：完成 service contract、physical ops 绑定与构建闭环；SCPI 维护路径迁移和 HIL 待继续
- 日期：2026-08-15
- 任务目标：
  - 将当前 PIO+DMA physical adapter 从“只有 SCPI 维护命令能表达状态”向 HAOFV 的 RefMemAO/core1 realtime TDMA service 边界收敛。
  - 先建立 core0 intent 与 core1 result 的字段归属，避免跨核共享状态双 writer。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_realtime_tdma.h` 和 `components/distributed_refmem/src/refmem_realtime_tdma.c`。
  - service 使用双 guard：core0 writer 只更新 intent mailbox、frame staging 和 reject counter；core1 writer 只更新 runtime/result snapshot、completed seq、ready/timeout/overrun/error counter。
  - TDMA service 增加 physical ops 边界，避免把具体总线实现写死在基础状态机中；host 单测使用 fake ops，固件使用 PIO+DMA physical adapter wrapper。
  - `DistributedRefMemAO` 绑定 `refmem_spi_physical_adapter_transmit/receive`，core1 service 可按 intent role、baud 和 deadline 调用真实 PIO+DMA TX/RX。
  - TDMA service 增加 RX result frame 缓存和只读 API；收到的 frame 不写入向量表大数据，后续由 RefMem Sync owner 读取并进入协议 decode/quality。
  - `distributed_refmem_init()` 初始化 TDMA service，`app_realtime_run_once()` 通过 `distributed_refmem_realtime_run_once()` 在 core1 循环中推进。
  - 新增维护查询 `SYSTem:REFMEM:SYNC:TDMA:STATus?`，只读取 snapshot，不直接驱动硬件动作。
  - 新增维护入口 `SYSTem:REFMEM:SYNC:TDMA:TX/RX/FRAMe?/ABORt`：`TX/RX` 只 post intent，`FRAMe?` 读取 core1 result frame 并交给 RefMem Sync receive state machine。
  - `tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 增加 `--transport tdma`，在现有线序 remap 和 frame plan 基础上改走 TDMA intent 路径；RAW 仍保留为 SPI line diagnostic。
  - 新增 `tests/unit/test_refmem_realtime_tdma.c` 和 `tools/tests/run_refmem_realtime_tdma_tests.ps1`。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过 host gcc 断言执行。
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815040106`，package CRC `0xE67D0C6D`。
- 后续：
  - 用 COM5/COM6 执行 `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --transport tdma --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_tdma`。
  - TDMA HIL 通过后再收敛或删除 `SYSTem:REFMEM:SYNC:SPI:*` 的帧级阻塞命令。

### REFMEM-TASK-20260815-010 - P4.5 PIO+DMA physical adapter 25 MHz 闭环

- 状态：完成当前 HIL；core1 TDMA service 化待继续
- 日期：2026-08-15
- 任务目标：
  - 按 VDC/TDMA 架构纠偏 P4.5 physical adapter：不得使用 RP2350 内置 SPI，也不得把 SIO bitbang 当作闭环结论。
  - 使用真实线序检测结果驱动 PIO SPI pin plan，在 COM5/COM6 两块最小系统板之间完成真实物理链路 RefMem Sync 帧闭环。
  - 目标速率提升到 25 MHz，避免继续以低速 PC 搬运或逐字节轮询掩盖架构问题。
- 完成内容：
  - 新增 `components/distributed_refmem/src/refmem_spi_physical.pio`，提供 PIO TX byte 和 PIO RX byte state machine。
  - `refmem_spi_physical_adapter.c` 从内置 `spi0`/SIO bitbang 纠偏为 PIO TX、PIO RX 和 DMA RX 缓冲；CPU 不再逐 bit 或逐字节搬运 RX FIFO。
  - `board_config.h` 增加 RefMem PIO transport 的 PIO/SM 资源定义，默认 baud 改为 25 MHz。
  - `tools/two_board_io_validate/two_board_io_validate.py` 支持 auto/detect 模式，并把 observed remap 写入报告。
  - `tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 默认使用 auto remap 和 25 MHz，根据实测 OUT->IN map 推导双向 PIO SPI pin plan。
  - 新增 `tools/refmem_spi_line_activity/refmem_spi_line_activity.py`，用于慢速长 RAW:TX 时观察对端输入 mask，证明 PIO TX 已在真实线束上活动。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，build `20260815031915`，package CRC `0xD5A25528`。
  - COM5/COM6 均 OTA 写入、boot 并 commit 到 build `20260815031915`。
  - `python tools\refmem_spi_line_activity\refmem_spi_line_activity.py --sender COM5 --receiver COM6 --sender-to-receiver 1,2,0,3 --baud 1000 --count 256 --seed 255` 显示 B 侧 IN0/IN2 有高低变化，证明 PIO TX 的 RX/SCK 已过线。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --baud 1000000 --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815031915_pio_dma_1m` 通过。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815031915_pio_dma_25m` 通过。
  - 25 MHz 报告记录：线序 preflight PASS，A->B remap `[1,2,0,3]`，B->A remap `[2,1,0,3]`；`RAW/HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 14 个 exchange 全部 PASS；`failures=[]`。
- 还需完成：
  - 当前 HIL 仍由 SCPI 在帧级触发 TX/RX。产品化下一步必须把 PIO+DMA physical adapter 接入 core1 realtime TDMA service，core0 只配置窗口和帧意图，core1 发布 frame-ready/timeout/overrun snapshot。
  - PIO/SM/DMA 资源需要接入 ResourceArbiter 和 RealtimeCapabilityContract，避免维护 HIL 直接抢占现有 `sync_io` 资源。
- 关联文件：
  - `components/distributed_refmem/src/refmem_spi_physical.pio`
  - `components/distributed_refmem/src/refmem_spi_physical_adapter.c`
  - `components/distributed_refmem/inc/refmem_spi_physical_adapter.h`
  - `boards/rp2350_trig/inc/board_config.h`
  - `tools/two_board_io_validate/two_board_io_validate.py`
  - `tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py`
  - `tools/refmem_spi_line_activity/refmem_spi_line_activity.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`

### REFMEM-TASK-20260815-009 - P4.5 SPI physical adapter 首次接入与线级阻塞定位

- 状态：部分完成，HIL 阻塞在物理线级 preflight
- 日期：2026-08-15
- 任务目标：
  - 按 P4.5 要求，把 RefMem Sync 从 PC/SCPI hex 搬运推进到真实两板 SPI 物理链路。
  - 保持 HAOFV 边界：SCPI 只作为维护触发和诊断入口，RefMem frame 仍由 RefMem Sync validate/receive 处理，不让 Vector 承载数据。
  - 在 COM5/COM6 两块最小系统板上验证 `HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 真实过线。
- 完成内容：
  - 新增 `refmem_spi_physical_adapter.h/.c`，使用 `spi0` 的 debug bring-up profile：RX16、CS17、SCK18、TX19，默认 500 kHz。
  - `SYSTem:REFMEM:SYNC:SPI:*` 增加 ARM/DISarm/STATus、frame TX/RX、raw byte TX/RX 和 line release/drive/status 维护入口。
  - `tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 固化三层验证：先扫线，再 raw byte，再 RefMem frame；PC 只触发 TX/RX，不搬运 frame hex。
  - build `20260815022459` 已通过 OTA 写入并在 COM5/COM6 上 commit，双板 `SYSTem:FW:BUILD?` 均返回该 build。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，package CRC `0xAE0FFEB2`。
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - OTA COM5/COM6 正常进入 `READY_TO_REBOOT` 并完成 `SYSTem:OTA:COMMit`。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815022459_linefull` 失败在 line preflight。
  - 六条线级检查均读 0：A_CS_B、A_SCK_B、A_TX_B_RX、B_CS_A、B_SCK_A、B_TX_A_RX。因此当前不是 RefMem frame/CRC/target 语义问题，也不是 raw SPI 负载问题，而是 GPIO16-19 互联未成立或 COM5/COM6 对应板未按该线序连接。
- 下一步：
  - 按 playbook 接线后重跑同一 HIL：`SCK18-SCK18`、`CS17-CS17`、`A_TX19 -> B_RX16`、`B_TX19 -> A_RX16`、`GND-GND`。
  - 线级 preflight 通过后，再进入 raw byte 和 `HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` frame 层闭环。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_spi_physical_adapter.h`
  - `components/distributed_refmem/src/refmem_spi_physical_adapter.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`

### REFMEM-TASK-20260815-008 - Host GCC 单元测试断言门禁

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 使用 `D:\Embedded\GCC\mingw64\bin\gcc.exe` 让现有 C 单元测试在 host 上真实执行断言。
  - 纠正此前多处测试脚本在无 host C 编译器时只做 ARM GCC compile-only 的验证口径。
  - 将该门禁作为继续推进 RefMem 表模型、S0 flash/core1、真实 transport 和 PIO 预约输出前的基础验证。
- 完成内容：
  - 新增 `tools/tests/run_host_unit_tests.ps1`，默认将 `D:\Embedded\GCC\mingw64\bin` 加入 `PATH`，顺序运行 BiSS、portable LOG/OTA 和 RefMem 相关测试脚本。
  - 修正 `run_refmem_application_contract_tests.ps1` 的链接源，补入 `refmem_slot_claim.c`。
  - 修复 `refmem_claim_propose_frame_init()` 在设置 `payload_count` 前计算 payload CRC 的协议 bug。
  - 扩大 `test_refmem_table_registry.c` 的测试构包容量，避免当前 `.rmtp` P0-P3 表 payload 超过旧 `1536` 字节缓冲导致 host 栈破坏。
  - 修正 `test_portable_ota_core.c` 的 package-mode 测试夹具，使 header package CRC 字段保持与当前 packager 一致的 `0` 语义。
  - 修正 `test_biss_protocol.c` 中 CRC6 golden vector 的参与位拼接，改为明确的 18-bit payload/status 和 24-bit payload 向量。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1` 通过。
  - S0 补入 Flash/Core1 lockout 断言后，覆盖 13 个测试脚本、18 个 C 单元测试文件，期望输出 `host unit test scripts passed: 13/13`。
  - Host GCC：`D:\Embedded\GCC\mingw64\bin\gcc.exe`。
- 还需完成：
  - 下一轮优先进入 S0：Flash erase/program 前 core1 park/lockout/RAM-resident 入口完整握手和故障注入。
  - 然后接入真实最小 physical transport，让 `DELTA/ACK/FENCE` 在两板真实链路上跑通。
  - `ModelTurntableAO` 需要补真实 PIO scheduled fire 输出路径，验证“到点出边沿”。
- 关联文件：
  - `tools/tests/run_host_unit_tests.ps1`
  - `tools/tests/run_refmem_application_contract_tests.ps1`
  - `components/distributed_refmem/src/refmem_claim_protocol.c`
  - `tests/unit/test_refmem_table_registry.c`
  - `tests/unit/test_portable_ota_core.c`
  - `tests/unit/test_biss_protocol.c`
  - `docs/validation/README.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 不继续扩大 RefMem 静态表模型，先按 `REFMEM_DOMAIN_TODO.md` 的近期主线完成 S0、真实 transport 和 PIO 预约输出闭环。

### REFMEM-TASK-20260815-007 - RMTP NodeLoadTable 真实表镜像

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 继续 P0 逐表真实化，将 `.rmtp` table 3 `NodeLoadTable` 从占位 payload 升级为固定 u32 表镜像。
  - 让 NodeLoad 的基础字段合法性由公共 application contract 校验，为后续 `LOAD:NODE` staging image 和动态实例装载做准备。
- 完成内容：
  - `tools/refmem_table_image/refmem_table_image.py` 新增 `build_node_load_payload()`，按当前 11 条默认 NodeLoad 模板输出固定 u32 payload。
  - `refmem_application_contract.h/.c` 新增 `refmem_application_contract_validate_node_load_table()`，校验 version、load count、load id、application/profile、node 范围、instance 范围、fail policy、enabled/required 和 target node mask。
  - `refmem_application_model.c` 的静态 NodeLoad linter 复用公共 contract，并保留对当前内置 FB instance 表的实例存在性、重复 enabled instance 和 required enabled instance 覆盖检查。
  - `refmem_table_registry.c` 新增 NodeLoad parser，并在 `.rmtp` package owner validation 中把 table 3 纳入 owner-validated mask。
  - `tests/python/test_refmem_pack_build.py` 增加 table 3 size/count 断言；`tests/unit/test_refmem_table_registry.c` 增加 NodeLoad package 构造和 owner mask/staging 状态断言。
- 验证结果：
  - `python -m pytest tests\python\test_refmem_pack_build.py` 通过，2 passed。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\refmem_pack_build\refmem_pack_build.py --output-dir build-rtos-multicore-smoke\refmem_pack_nodeload` 通过，生成 `.rmtp` size `1924`、CRC `3A20B868`，table 3 CRC `7D7762D1`。
  - `python tools\sd_fs_build\sd_fs_build.py --build-dir build-rtos-multicore-smoke --output-dir build-rtos-multicore-smoke\sdcard_nodeload --clean` 通过。
  - 独立打包输出与 SD staging 输出的 `/refmem/app_model.rmtp` SHA256 均为 `C2EC768B52E284403D655BF60309E5AE102AE974909763A3B078B5FA79CE75CD`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814172116`，package CRC `0x5B9C1C34`。
- 还需完成：
  - 继续将 FbInstance、EventLink、DataLink、DeploymentGate 和 ConnectionQuality 五张表升级为真实表镜像。
  - `SYSTem:REFMEM:LOAD:NODE` 仍需从单条候选 snapshot 升级为真正的 NodeLoad staging image；本轮只是让 RMTP package 中的完整 NodeLoad 表具备 canonical payload 和 contract。
- 关联文件：
  - `tools/refmem_table_image/refmem_table_image.py`
  - `tools/refmem_pack_build/refmem_pack_build.py`
  - `components/distributed_refmem/inc/refmem_application_contract.h`
  - `components/distributed_refmem/src/refmem_application_contract.c`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tests/python/test_refmem_pack_build.py`
  - `tests/unit/test_refmem_table_registry.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`

### REFMEM-TASK-20260815-006 - RMTP ApplicationMap 真实表镜像

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 按 P0 “逐表替换占位 payload”推进 `.rmtp` table 0 `ApplicationMap`。
  - 将 ApplicationMap 校验从 application model 内部静态 linter 提升为可被 SD/System Pack parser 复用的公共 table contract。
- 完成内容：
  - `tools/refmem_table_image/refmem_table_image.py` 新增 `build_application_map_payload()`，table 0 从 64 字节占位改为 6 个 u32 的真实 `ApplicationMap` payload。
  - `refmem_application_contract.h/.c` 新增 `refmem_application_contract_validate_application_map()`，校验 version、application/profile id、layout version 和 target node mask。
  - `refmem_application_model.c` 的静态 linter 复用公共 ApplicationMap contract，避免内置模型与 `.rmtp` parser 分叉。
  - `refmem_table_registry.c` 新增 ApplicationMap parser，并在 package owner validation 中把 table 0 纳入 owner-validated mask。
  - `tests/python/test_refmem_pack_build.py` 更新 table 0 size/字段断言；`tests/unit/test_refmem_table_registry.c` 更新 placeholder 拒绝点和 owner mask 预期。
- 验证结果：
  - `python -m pytest tests\python\test_refmem_pack_build.py` 通过，2 passed。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\refmem_pack_build\refmem_pack_build.py --output-dir build-rtos-multicore-smoke\refmem_pack_appmap` 通过，生成 `.rmtp` size `1496`、CRC `2F945F7D`，table 0 CRC `60E14FD0`。
  - `python tools\sd_fs_build\sd_fs_build.py --build-dir build-rtos-multicore-smoke --output-dir build-rtos-multicore-smoke\sdcard_appmap --clean` 通过。
  - 独立打包输出与 SD staging 输出的 `/refmem/app_model.rmtp` SHA256 均为 `CAD97D3A4CF3D205AFFEB5375DDE3E49A8A3BD55D4A4140C95AF4F9A9E55A379`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814171242`，package CRC `0x2A60F721`。
- 还需完成：
  - 继续将 NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate 和 ConnectionQuality 六张表升级为真实表镜像。
  - 对包含字符串语义的 FB/Data 表先确定 binary canonical 表达方式，例如固定 hash/id 字段，而不是把 C 指针或可变文本写入 `.rmtp`。
- 关联文件：
  - `tools/refmem_table_image/refmem_table_image.py`
  - `components/distributed_refmem/inc/refmem_application_contract.h`
  - `components/distributed_refmem/src/refmem_application_contract.c`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tests/python/test_refmem_pack_build.py`
  - `tests/unit/test_refmem_table_registry.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`

### REFMEM-TASK-20260815-005 - TableRegistry package staging 表级 CRC 纠偏

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 修复 `.rmtp` `LOAD:SD` staging 中 `RefMemTableRegistry` 把同一个 package CRC 写入所有 table entry `staging_crc32` 的结构性偏离。
  - 将 package descriptor 与 table entry 的职责分开：descriptor 记录包级 CRC/mask/path，entry 记录对应表自己的 CRC、CRC_OK/OWNER_OK 状态和 owner validation 覆盖情况。
- 完成内容：
  - `refmem_table_package_validation_t` 增加 `table_mask`、`table_crc32[9]` 和 `owner_validated_table_mask`。
  - `.rmtp` parser 在 directory 校验通过时保存每张表的 CRC；BoardCapability/GenericNode owner contract 通过后只把 table 1/2 标记进 `owner_validated_table_mask`。
  - 新增 `refmem_table_registry_stage_package_validation()`，用 package validation summary 更新 per-table staging entry：所有表进入 `CRC_OK`，当前已 owner validation 的 BoardCapability/GenericNode 进入 `OWNER_OK`。
  - staging descriptor 的 `package_crc32` 保持包级 CRC；由于其余七张表仍未 owner validation，descriptor state 保持 `CRC_OK`，不能被 `activate_staging()` 当作完整 `OWNER_OK` active image 激活。
  - `SYSTem:REFMEM:LOAD:SD` 在 package parser 通过后调用新的 package staging API，`SYSTem:REFMEM:TABle? <id>` 可观察真实表级 staging CRC。
  - 单元测试补充 per-table CRC、partial owner state 和 partial owner staging 不可激活的约束。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814170816`，package CRC `0x9B75313C`。
- 还需完成：
  - 将其余七张 `.rmtp` 表升级为真实表镜像并接入 owner validation，使完整 package staging descriptor 能从 `CRC_OK` 进入 `OWNER_OK`。
  - 将 owner validation reason/evidence 进一步落入 table entry 的 `last_result/evidence_index`，而不是只保留 validation summary。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_table_registry.h`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_table_registry.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`

### REFMEM-TASK-20260815-004 - RMTP table image 生成器共享基础件

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 消除 `tools/refmem_pack_build/refmem_pack_build.py` 与 `tools/sd_fs_build/sd_fs_build.py` 中重复维护的 RMTP 表格式、表名、CRC、BoardCapability 和 GenericNode payload 生成逻辑。
  - 保证独立 RefMem package 工具和 SD System Pack staging 输出同一份 `.rmtp` 二进制表镜像，避免后续表格式演进时出现双源分叉。
- 完成内容：
  - 新增 `tools/refmem_table_image/refmem_table_image.py`，作为 canonical RMTP table image builder，统一 `RMTP` header、directory、package CRC、9 张表顺序、默认 BoardCapabilityTable 和 GenericNodeTable payload。
  - `tools/refmem_pack_build/refmem_pack_build.py` 改为复用共享 builder，只保留独立输出目录、manifest 和 idx 生成职责。
  - `tools/sd_fs_build/sd_fs_build.py` 删除私有 `build_refmem_table_package()`、`build_refmem_board_capability_payload()`、`build_refmem_generic_node_payload()` 等重复实现，SD staging 直接调用共享 builder。
  - 两个脚本都补齐直接脚本运行时的 repo root import path，避免 `python tools\...\*.py` 因 `tools` 包不可见失败。
  - `REFMEM_DOMAIN_TODO.md` 与架构文档同步当前状态：BoardCapability/GenericNode 已是 `.rmtp` 真实表镜像，其余表仍按 P0 逐表替换。
- 验证结果：
  - `python -m py_compile tools\sd_fs_build\sd_fs_build.py tools\refmem_pack_build\refmem_pack_build.py tools\refmem_table_image\refmem_table_image.py` 通过。
  - `python -m pytest tests\python\test_refmem_pack_build.py` 通过，2 passed。
  - `python tools\refmem_pack_build\refmem_pack_build.py --output-dir build-rtos-multicore-smoke\refmem_pack_shared` 通过，生成 `.rmtp` size `1536`、CRC `E24B033A`。
  - `python tools\sd_fs_build\sd_fs_build.py --build-dir build-rtos-multicore-smoke --output-dir build-rtos-multicore-smoke\sdcard_shared --clean` 通过。
  - 独立打包输出与 SD staging 输出的 `/refmem/app_model.rmtp` SHA256 均为 `6D3A788DB55DC43BF63434C38C5E00E9741C631B9382710F9768317928401188`。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814170001`，package CRC `0xBD91DC17`。
- 还需完成：
  - 将其余七张 `.rmtp` 表升级为真实表镜像，并继续接入各自 owner validation。
  - 后续可把 manifest/idx 的公共描述逻辑继续收敛，但二进制 table image 已经只有一个生成源。
- 关联文件：
  - `tools/refmem_table_image/refmem_table_image.py`
  - `tools/refmem_table_image/__init__.py`
  - `tools/refmem_pack_build/refmem_pack_build.py`
  - `tools/sd_fs_build/sd_fs_build.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`

### REFMEM-TASK-20260815-003 - RMTP Board/Generic owner validation

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 推进 P0 `.rmtp` 从纯 CRC parser 走向真实表镜像 owner validation。
  - 先闭环固定 u32 布局的 `BoardCapabilityTable` 和 `GenericNodeTable`，避免继续把 64 字节 placeholder 当成可验证表。
- 完成内容：
  - `tools/refmem_pack_build/refmem_pack_build.py` 和 `tools/sd_fs_build/sd_fs_build.py` 生成真实 `BoardCapabilityTable` / `GenericNodeTable` payload；其他表暂时仍是 placeholder，后续逐表替换。
  - `RefMemTableRegistry` 在 `.rmtp` header、directory、payload、package 和单表 CRC 通过后，解析 table 1/2，并调用 `refmem_application_contract_validate_slot_substrate()` 做 owner validation。
  - `refmem_table_package_error_t` 增加 `REFMEM_TABLE_PACKAGE_ERR_OWNER_VALIDATION`，旧 placeholder package 会被拒绝，`first_bad_table` 指向 table 1。
  - `tests/unit/test_refmem_table_registry.c` 增加旧 placeholder package 拒绝和 contract table package 接受两条回归。
  - `tests/python/test_refmem_pack_build.py` 更新表 1/2 的期望尺寸，确认 payload header 为 `version,node_count`。
  - `REFMEM_DOMAIN_TODO.md` 标记 BoardCapabilityTable 的 `LOAD:SD` owner validation 完成，并新增其余表真实镜像待办。
- 验证结果：
  - `python -m pytest tests\python\test_refmem_pack_build.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python -m py_compile tools\sd_fs_build\sd_fs_build.py tools\refmem_pack_build\refmem_pack_build.py` 通过。
  - `python tools\refmem_pack_build\refmem_pack_build.py --output-dir build-rtos-multicore-smoke\refmem_pack_owner_contract` 通过。
  - `python tools\sd_fs_build\sd_fs_build.py --build-dir build-rtos-multicore-smoke --output-dir build-rtos-multicore-smoke\sdcard_owner_contract --clean` 通过。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `git diff --check` 通过；仅报告工作区 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814164837`，package CRC `0xC2461ED3`。
- 还需完成：
  - 将其余七张 `.rmtp` 表升级为真实表镜像，并按 ApplicationMap/NodeLoad/FB/Event/Data/Gate/Quality owner contract 逐表校验。
  - 将 package owner validation 结果进一步写入 TableRegistry evidence，而不是只返回 package validation error。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_table_registry.h`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tools/refmem_pack_build/refmem_pack_build.py`
  - `tools/sd_fs_build/sd_fs_build.py`
  - `tests/unit/test_refmem_table_registry.c`
  - `tests/python/test_refmem_pack_build.py`

### REFMEM-TASK-20260815-002 - Realtime contract 入口收敛到 SlotClaimMap

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 消除旧 `refmem_realtime_contract_derive()` 通过 `active_default_slot` 反查 BoardCapability 的 legacy 路径。
  - 保证 RealtimeCapabilityContract 只从 SlotClaimMap resolved assignment 派生，不再把 BoardCapability default slot 当作生产绑定事实。
- 完成内容：
  - 从 `refmem_realtime_contract.h/.c` 删除 `refmem_realtime_contract_derive()` 和内部 `active_default_slot` 查找 helper。
  - `derive_from_claim_map()` 改用公共 `REFMEM_APP_CAP_BASELINE`，与 application contract baseline 语义一致。
  - `tests/unit/test_refmem_realtime_contract.c` 改为构造 `refmem_slot_claim_map_t` 中的 claimed assignment，并只调用 `refmem_realtime_contract_derive_from_claim_map()`。
  - `REFMEM_DOMAIN_TODO.md` 将 P2 legacy realtime contract 偏离项标记完成，并把 P2 当前基线改为 SlotClaimMap resolved assignment。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `git diff --check` 通过；仅报告工作区 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814164049`，package CRC `0x69842137`。
- 还需完成：
  - 继续 P0：`.rmtp` 真实表镜像生成、表级 owner validation、active/staging/rollbackable image buffer。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_realtime_contract.h`
  - `components/distributed_refmem/src/refmem_realtime_contract.c`
  - `tests/unit/test_refmem_realtime_contract.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`

### REFMEM-TASK-20260815-001 - GenericNode slot substrate linter 纠偏

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 审查当前 REFMEM 代码中偏离 HAOFV 的表职责边界，并先闭环修复已开始的 P1 `GenericNodeTable` linter 问题。
  - 保证 A0-A7 仍是通用逻辑插槽，不被 `BoardCapabilityTable[i]` 的 UUID、persona、hw profile 或 default slot 固定绑定。
- 完成内容：
  - `REFMEM_DOMAIN_TODO.md` 增加本轮架构偏离审查待办，覆盖 TableRegistry active image、`.rmtp` owner validation、NodeLoad staging、GenericNode linter、legacy realtime contract 和 vector mutable accessor。
  - 新增 `refmem_application_contract.h/.c`，作为 GenericNode slot substrate 与 BoardCapability 候选能力表的表契约校验基础件。
  - 将 GenericNode contract 与 BoardCapability contract 拆成独立 validator：GenericNode 只校验 node id、baseline、claim policy、online/fail policy；BoardCapability 只校验 board id、baseline、default slot 范围、online flag 和 IO/IP capability 覆盖；组合校验不引入新的 A/B 固定绑定。
  - 将 `REFMEM_APP_CAP_BASELINE` 上移为公共应用模型常量，默认表、SCPI staging 和 application contract 使用同一份 `BOARD + REFMEM + VDC` baseline 定义。
  - 移除 linter 中 `BoardCapabilityTable[i]` 与 `GenericNodeTable[i]` 的 slot、UUID、persona、hw profile 一一相等要求。
  - 新增 `tests/unit/test_refmem_application_contract.c` 和 `tools/tests/run_refmem_application_contract_tests.ps1`，覆盖错位 default slot、不同 UUID/persona/hw profile 仍可通过 substrate contract，以及 baseline/default slot/capability coverage 负例。
  - `REFMEM_DOMAIN_TODO.md` 将对应 P1 偏离项标记完成。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `git diff --check` 通过；仅报告工作区 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814162658`，package CRC `0x6F2E6B9C`。
- 还需完成：
  - 继续处理 P0：真实 active/staging/rollbackable table image、`.rmtp` 表级 owner validation、NodeLoadTable staging image。
  - 后续处理 P2：删除或降级 legacy `refmem_realtime_contract_derive()` 的 default-slot fallback。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_contract.h`
  - `components/distributed_refmem/src/refmem_application_contract.c`
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `tests/unit/test_refmem_application_contract.c`
  - `tools/tests/run_refmem_application_contract_tests.ps1`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`

### REFMEM-TASK-20260814-053 - SlotClaim gate integrity checks

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 优先完成 P1 SlotClaimMap 的关键门禁，避免后续动态节点装载和物理同步链路绕过 A0-A7 通用插槽约束。
  - 补齐 stale claim、claim CRC 和 map CRC 检查，同时纠正“UUID 硬绑定 A slot”的错误方向。
- 完成内容：
  - `refmem_slot_claim_reason_t` 增加 `STALE`、`CLAIM_CRC` 和 `MAP_CRC` reason，并移除硬绑定 mismatch reason，避免把 A0-A7 误当固定物理板或固定功能。
  - `refmem_slot_claim_derive_map()` 不再用 UUID 反查固定 slot；`STRICT_UUID` 只要求候选板具备稳定 UUID。
  - SlotClaim resolved assignment 的 runtime capability 采用 BoardCapability 中的实际能力，slot 本身只承担通用地址、claim policy、priority 和 required 语义。
  - `refmem_slot_claim_gate_evaluate()` 重新计算 map CRC 和每个 assignment CRC，发现篡改或不一致时拒绝 gate。
  - `refmem_slot_claim_gate_evaluate()` 将显式 `STALE` 状态或 assignment epoch 与 map epoch 不一致视为 stale claim，拒绝进入 ready。
  - `tests/unit/test_refmem_slot_claim.c` 覆盖 duplicate、缺失 UUID、任意 slot claim、candidate overflow、stale、claim CRC 和 map CRC。
  - `tools/multicore_board_validate.py` 不再把 A0/A2/A7 解释成固定功能位置，只验证 generic SlotClaimMap gate、required/optional 和 config RUN gate 一致性。
  - `REFMEM_DOMAIN_TODO.md` 将 P1 SlotClaim 完整性检查项标记完成。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python -m py_compile tools\multicore_board_validate\multicore_board_validate.py` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814155637`，package CRC `0xA9699AF4`。
  - COM5 OTA/commit 通过，`SYSTem:FW:BUILD?` 返回 `20260814155637`，`SYSTem:ERRor?` 返回 `0,"No error"`。
  - COM6 OTA/commit 通过，`SYSTem:FW:BUILD?` 返回 `20260814155637`，`SYSTem:ERRor?` 返回 `0,"No error"`。
  - 初次双板 `multicore_board_validate.py` 只有旧脚本的固定 slot 功能期望失败；修正为 generic slot gate 并烧录最终 build 后，COM5/COM6 均 17/17 passed。
  - 板端 SlotClaim 查询：`SYSTem:REFMEM:CLAIM? 0` 在 COM5/COM6 均返回 gate ready、map CRC `386979554`、无 conflict/overflow/mismatch；`SYSTem:REFMEM:CLAIM:EVIDence? 0` 返回空 evidence；`SYSTem:CONFigure:STAT?` ready；`SYSTem:ERRor?` 为 no error。
- 还需完成：
  - 后续继续 P1：BoardCapabilityTable 纳入 `.rmtp` 真实表镜像、单板 16 候选反向验证和动态装载验证。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_slot_claim.h`
  - `components/distributed_refmem/src/refmem_slot_claim.c`
  - `tests/unit/test_refmem_slot_claim.c`
  - `tools/multicore_board_validate/multicore_board_validate.py`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 继续 P1：BoardCapabilityTable `.rmtp` 镜像、动态装载和 16 候选溢出 HIL。

### REFMEM-TASK-20260814-052 - RefMem Quality runtime snapshot

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将本地 PIO SPI adapter 计数和 remote `REFMEM_QUALITY` snapshot 规范映射为 `DistributedConnectionQualityTable` entry 形状。
  - 保持 active static `ConnectionQualityTable` 只作为契约/CRC 来源，不在运行中被维护 bridge 热写。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_quality.h` 和 `src/refmem_quality.c`。
  - 新增 `refmem_quality_runtime_table_t`，包含 active quality table CRC、local slot、epoch/run、overflow count 和 runtime quality entries。
  - 本地 adapter 条目映射 sync CRC/stale/drop 计数和 adapter bad/drop/timeout/last error/latency class。
  - remote 条目映射 `REFMEM_QUALITY` 的 source/target、seq、CRC/stale/drop/late/timeout、last error、p99/p999 和 evidence。
  - 新增维护查询 `SYSTem:REFMEM:QUALity? [index]`，读取 runtime derived quality snapshot，不修改 active RefMem fact。
  - `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 增加 runtime quality 查询校验，确认远端 QUALITY frame 接收后进入派生质量视图。
  - `REFMEM_DOMAIN_TODO.md` 将 P4.5 quality 映射项和 P5 `refmem_quality.h/.c` 标记完成。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python -m py_compile tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814151311`，package CRC `0x97D14458`。
  - COM5/COM6 均通过 OTA 更新并 commit 到 build `20260814151311`。
  - `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814151311 --package-crc 0x97D14458 --line-remap-a-to-b 1,2,0,3 --line-remap-b-to-a 2,1,0,3 --preflight-io --out-dir build-rtos-multicore-smoke\refmem_sync_quality_runtime_hil_COM5_COM6_20260814151311_r2` 通过。
  - HIL 关键结果：IO preflight PASS；B0->B1 remap `[1,2,0,3]`，B1->B0 remap `[2,1,0,3]`；RefMem Sync 63 条记录全部 PASS；两端 runtime quality index `1` 均正确反映对端 QUALITY frame，A 侧记录 B->A `crc_error_count=1,drop_count=2,last_error=9`，B 侧记录 A->B 无错误。
- 还需完成：
  - 真实 PIO SPI physical adapter service 接入，替换当前 PC/SCPI frame 搬运。
  - 后续把 quality snapshot 接入 DeploymentGate/RUN gate evidence。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_quality.h`
  - `components/distributed_refmem/src/refmem_quality.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_quality.c`
  - `tools/tests/run_refmem_quality_tests.ps1`
  - `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`
- 下一步：
  - 编译、烧录 COM5/COM6 并执行带 IO preflight 的 RefMem Sync HIL，完成本轮闭环后提交推送。

### REFMEM-TASK-20260814-051 - RefMem Sync HIL 报告与 IO 预检固化

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 补齐 P4.5 阶段 0 的线序与串口生命周期检查，把 `REALtime:IO:PROFile?`、输出 release、逐线 remap 和退出清理纳入 RefMem Sync HIL 前置流程。
  - 扩展 RefMem Sync HIL 报告，记录 package CRC、双向线序 remap 和 IO preflight 结果，避免后续依赖手写日志。
- 完成内容：
  - `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 新增 `--package-crc`、`--line-remap-a-to-b`、`--line-remap-b-to-a` 和 `--preflight-io`。
  - `--preflight-io` 会在主协议交换前调用 `tools/two_board_io_validate/two_board_io_validate.py`，该工具完成双板 profile 查询、输出 release、逐线 remap 验证和退出释放。
  - `summary.json` 与 `transcript.txt` 写入 package CRC、line remap 和 `io_preflight` 摘要；preflight 结果保存到同一报告目录下的 `io_preflight/`。
  - `REFMEM_DOMAIN_TODO.md` 将阶段 0 与 HIL 报告扩展项标记完成。
- 验证结果：
  - `python -m py_compile tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py` 通过。
  - `python tools\two_board_io_validate\two_board_io_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\two_board_io_COM5_COM6_preflight_20260814143942` 通过。
  - `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814143942 --package-crc 0x4D1483AE --line-remap-a-to-b 1,2,0,3 --line-remap-b-to-a 2,1,0,3 --preflight-io --out-dir build-rtos-multicore-smoke\refmem_sync_report_hil_COM5_COM6_20260814143942` 通过。
  - HIL 关键结果：IO preflight PASS；B0->B1 remap `[1,2,0,3]`，B1->B0 remap `[2,1,0,3]`；完整 RefMem Sync HIL 61 条记录全部 PASS；报告记录 package CRC `0x4D1483AE`。
- 还需完成：
  - 真实 PIO SPI physical adapter service 接入，替换当前 PC/SCPI frame 搬运。
  - 将 remote quality snapshot 映射到 `DistributedConnectionQualityTable`。
- 关联文件：
  - `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
  - `tools/README.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
- 下一步：
  - 进入真实 PIO SPI physical adapter service 的实现拆解；先定义 service 状态机、TX/RX ownership、线序依赖和与当前 SCPI bridge 的并存边界。

### REFMEM-TASK-20260814-050 - RefMem Sync QUALITY frame 最小闭环

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 在 FENCE 已通过的基础上，增加 `REFMEM_QUALITY` frame，把本地 receive quality counter 和 peer seq 摘要同步给对端。
  - 保持 QUALITY frame 只写 remote quality snapshot，不直接写 active `DistributedConnectionQualityTable`。
- 完成内容：
  - `refmem_sync_context_t` 增加按 source slot 索引的 `refmem_sync_remote_quality_snapshot_t`。
  - `refmem_sync_receive_frame()` 接收 `REFMEM_QUALITY` 后提交 remote quality snapshot，记录 quality id、scope、target slot、seq expected/last、CRC/stale/drop/late/timeout、last error 和 evidence。
  - 新增 `refmem_sync_get_remote_quality()`。
  - 新增 `SYSTem:REFMEM:SYNC:QUALity:FRAMe?`，从本地 receive quality counter 和指定 peer seq 摘要生成 QUALITY frame。
  - 新增 `SYSTem:REFMEM:SYNC:QUALity:STATus?`，查询指定 source slot 最近一次 remote QUALITY snapshot。
  - `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 扩展为 HELLO/EPOCH/DELTA/MIRROR/ACK_NACK/FENCE/QUALITY 全流程，记录 61 条 HIL 结果。
- 验证结果：
  - `python -m py_compile tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814143942`，package CRC `0x4D1483AE`。
  - COM5/COM6 均 OTA 并 commit 到 build `20260814143942`。
  - `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814143942 --out-dir build-rtos-multicore-smoke\refmem_sync_quality_hil_COM5_COM6_20260814143942` 通过，61 条记录全部 PASS。
  - HIL 关键结果：两板 build id 均为 `20260814143942`；SlotClaimMap CRC 均为 `386979554`；A->B QUALITY snapshot 记录 `seq_expected=9,seq_last=8` 且错误计数为 0；B->A QUALITY snapshot 记录 `seq_expected=10,seq_last=9,crc_error_count=1,drop_count=2,last_error=9`。
- 还需完成：
  - 将 remote QUALITY snapshot 映射到 `DistributedConnectionQualityTable`，并明确 active table owner validation 与 evidence index。
  - 真实 PIO SPI physical adapter service 接入，替换当前 PC/SCPI frame 搬运。
  - 增加 quality 与 DeploymentGate/RUN gate 的消费规则。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_sync.h`
  - `components/distributed_refmem/src/refmem_sync.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_sync.c`
  - `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`
- 下一步：
  - 回到 P4.5 未完成项：真实 PIO SPI physical adapter service、线序/生命周期阶段 0 固化，以及 quality 到 `DistributedConnectionQualityTable` 的正式映射。

### REFMEM-TASK-20260814-049 - RefMem Sync FENCE 最小闭环

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 在 DELTA mirror 和 ACK/NACK 已通过的基础上，增加最小 `REFMEM_FENCE`，让接收板能基于 source mirror visible 与最小 seq 形成 pass/fail/timeout snapshot。
  - 保持 FENCE 仍为 RefMem Sync 维护闭环，不直接触发产品 RUN gate、DistributedDeploymentGate 或 fault latch。
- 完成内容：
  - `refmem_sync_context_t` 增加按 source slot 索引的 `refmem_sync_fence_snapshot_t`。
  - `refmem_sync_receive_frame()` 接收 `REFMEM_FENCE` 后解析 `refmem_sync_fence_payload_t`，检查 local slot 是否在 required mask 中、source mirror 是否 visible、mirror frame seq 是否满足 `min_table_seq`。
  - 新增 `refmem_sync_get_fence()`。
  - 新增 `SYSTem:REFMEM:SYNC:FENCe?`，生成最小 FENCE frame，携带 fence seq、scope、required mask、min table seq、CRC bundle 和 deadline。
  - 新增 `SYSTem:REFMEM:SYNC:FENCe:STATus?`，查询指定 source slot 最近一次 FENCE snapshot。
  - `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 扩展为 HELLO/EPOCH/DELTA/MIRROR/ACK_NACK/FENCE 全流程，增加双向 FENCE pass 和 min seq 不满足的 fail/timeout 场景。
- 验证结果：
  - `python -m py_compile tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814142748`，package CRC `0xE3F75DB7`。
  - COM5/COM6 均 OTA 并 commit 到 build `20260814142748`。
  - `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814142748 --out-dir build-rtos-multicore-smoke\refmem_sync_fence_hil_COM5_COM6_20260814142748` 通过，55 条记录全部 PASS。
  - HIL 关键结果：两板 build id 均为 `20260814142748`；SlotClaimMap CRC 均为 `386979554`；A->B 与 B->A FENCE 在对端 mirror visible 且 `min_table_seq=3` 时 passed；A->B `min_table_seq=99,deadline_us=0` 时 failed，`missing_mask=2,timed_out=1,last_reason=3`。
- 还需完成：
  - 将 FENCE snapshot 接入正式 `refmem_command.h/.c` completion、DeploymentGate 和 `DistributedConnectionQualityTable`。
  - 实现 `REFMEM_QUALITY` frame，把 adapter CRC/drop/late/timeout 计数作为总线无关质量事实发布。
  - 真实 PIO SPI physical adapter service 接入，替换当前 PC/SCPI frame 搬运。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_sync.h`
  - `components/distributed_refmem/src/refmem_sync.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_sync.c`
  - `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`
- 下一步：
  - 进入 P4.5 阶段 4 的 `REFMEM_QUALITY` frame，把现有 adapter 和 receive quality counter 从本地查询扩展为可同步质量事实。

### REFMEM-TASK-20260814-048 - RefMem Sync ACK/NACK 最小闭环

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 在 DELTA mirror 已通过的基础上，增加 `REFMEM_ACK_NACK` 回传，让两块板能对已接收 DELTA 和异常 RX 结果形成可查询确认。
  - 覆盖正向 ACK、duplicate seq、target mismatch 和 payload CRC mismatch，不直接写 active ApplicationModel、SlotClaimMap 或业务 active fact。
- 完成内容：
  - `refmem_sync_context_t` 增加按 source slot 索引的 `refmem_sync_ack_snapshot_t`。
  - `refmem_sync_receive_frame()` 接收 `REFMEM_ACK_NACK` 后提交 ACK/NACK snapshot，记录 command/delta seq、taken/ack/nack/busy/timeout 位图、reason、evidence、frame seq 和 received count。
  - payload CRC 错误时尽量保留可解码 header 到 RX snapshot，使维护 bridge 能为坏帧生成 NACK 证据。
  - 新增 `refmem_sync_get_ack()`。
  - 新增 `SYSTem:REFMEM:SYNC:ACK?`，基于本板最近一次 RX snapshot 生成 ACK_NACK frame。
  - 新增 `SYSTem:REFMEM:SYNC:ACK:STATus?`，查询指定 source slot 最近一次 ACK/NACK snapshot。
  - `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 扩展为 HELLO/EPOCH/DELTA/MIRROR/ACK_NACK 全流程，正向 ACK 双向生成后再互相注入，避免 ACK frame 覆盖 DELTA `last_rx`。
- 验证结果：
  - `python -m py_compile tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814141201`，package CRC `0xA1E002E6`。
  - COM5/COM6 均 OTA 并 commit 到 build `20260814141201`。
  - `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814141201 --out-dir build-rtos-multicore-smoke\refmem_sync_ack_hil_COM5_COM6_20260814141201_r3` 通过，46 条记录全部 PASS。
  - HIL 关键结果：两板 build id 均为 `20260814141201`；SlotClaimMap CRC 均为 `386979554`；A->B 与 B->A DELTA seq `3` 均收到 ACK；duplicate seq NACK reason `6`、target mismatch NACK reason `4`、payload CRC mismatch NACK reason `9` 均由对端接收并可查询。
- 还需完成：
  - 将 ACK/NACK 从维护 bridge 的 `last_rx` 生成模式收敛到正式 `refmem_command.h/.c` command slot 完成语义。
  - 增加 `REFMEM_FENCE` 可见性门禁，把 mirror visible、ACK/NACK 和 RUN/SYNC gate 串起来。
  - 真实 PIO SPI physical adapter service 接入，替换当前 PC/SCPI frame 搬运。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_sync.h`
  - `components/distributed_refmem/src/refmem_sync.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_sync.c`
  - `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`
- 下一步：
  - 进入 P4.5 阶段 4 的 FENCE/QUALITY 闭环，并开始把 ACK/NACK 与正式 command slot completion 语义对齐。

### REFMEM-TASK-20260814-047 - RefMem Sync DELTA mirror 最小闭环

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 在 HELLO/EPOCH 已通过的基础上，增加最小 `REFMEM_DELTA` test field，让两块板能通过受控协议帧同步一个 u32 事实到对端 mirror snapshot。
  - 保持 DELTA mirror 仍属于 RefMem Sync 维护闭环，不直接写 active ApplicationModel、SlotClaimMap 或业务 active fact。
- 完成内容：
  - `refmem_sync_context_t` 增加按 source slot 索引的 `refmem_sync_mirror_snapshot_t`。
  - `refmem_sync_receive_frame()` 接收 `REFMEM_DELTA` 后解析 `refmem_sync_delta_header_t + u32 value`，更新 mirror visible、slot、field、slot_seq、value、payload CRC、frame seq、committed count 和 visible count。
  - 新增 `refmem_sync_get_mirror()`。
  - `SYSTem:REFMEM:SYNC:DELTa?` 可生成带 ACK request flag 的最小 u32 DELTA frame。
  - `SYSTem:REFMEM:SYNC:MIRRor?` 可查询指定 source slot 的 mirror snapshot。
  - `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 从 HELLO/EPOCH 扩展为 HELLO/EPOCH/DELTA/MIRROR，并增加 build id、SlotClaimMap CRC 和 adapter snapshot 预检。
- 验证结果：
  - `python -m py_compile tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814134858`，package CRC `0xA776513E`。
  - COM5/COM6 均 OTA 并 commit 到 build `20260814134858`。
  - `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --expected-build 20260814134858 --out-dir build-rtos-multicore-smoke\refmem_sync_delta_hil_COM5_COM6_20260814134858_report` 通过，26 条记录全部 PASS。
  - HIL 关键结果：两板 build id 均为 `20260814134858`；SlotClaimMap CRC 均为 `386979554`；adapter id 均为 `1`；A->B DELTA value `2768240641`、B->A DELTA value `3053453314` 均在对端 mirror 可见；两板 quality `accepted_count=3` 且 frame、CRC、target、epoch 错误计数为 0。
- 还需完成：
  - `REFMEM_ACK_NACK` 回传，把 DELTA accepted、duplicate seq、target mismatch 和 payload CRC mismatch 转成可查询 ACK/NACK 结果。
  - `REFMEM_FENCE` 可见性门禁，把 mirror visible 与 RUN/SYNC gate 串起来。
  - 真实 PIO SPI physical adapter service 接入，替换当前 PC/SCPI frame 搬运。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_sync.h`
  - `components/distributed_refmem/src/refmem_sync.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_sync.c`
  - `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
- 下一步：
  - 进入 P4.5 阶段 3 的 ACK_NACK 闭环，优先让接收侧根据最近一次 RX snapshot 生成 ACK_NACK frame 并由对端接收/记录。

### REFMEM-TASK-20260814-046 - RefMem Sync HELLO/EPOCH SCPI 搬运闭环入口

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 在真实 PIO SPI 物理 adapter 完成前，先通过系统维护面把 RefMem Sync frame 在两块最小系统板之间搬运起来。
  - 验证 HELLO/EPOCH 的 frame encode、adapter RX staging、receive state machine、peer snapshot 和 quality counter，不直接修改 active RefMem fact。
- 完成内容：
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c` 增加 `SYSTem:REFMEM:SYNC:*` 维护入口：`INITialize`、`HELLo?`、`EPOCh?`、`RX`、`PEER?`、`QUALity?`、`ADAPter?`。
  - `HELLo?` 从 BoardCapabilityTable、ApplicationModel snapshot、adapter caps 和 build id 摘要生成 HELLO frame。
  - `EPOCh?` 从 TableRegistry/ApplicationModel snapshot 生成首版 EPOCH frame。
  - `RX` 执行 `hex -> PIO SPI adapter RX staging -> poll -> refmem_sync_receive_frame()`，只更新 sync context 的 peer/quality 状态。
  - 新增 `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py`，固化两板 HELLO/EPOCH 搬运验证，支持 COM 口和 USBTMC VISA resource。
  - `REFMEM_SYNC_ARCHITECTURE.md` 增加维护面 SCPI bridge 边界，明确该入口不是裸 RefMem 域，也不是 active fact 写入口。
  - `REFMEM_MIN_SYSTEM_PLAYBOOK.md` 增加两板 HELLO/EPOCH 验证命令、脚本和通过条件。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814133439`，package CRC `0x1926CA52`。
  - `python tools\ota_send\ota_send.py COM5 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python tools\ota_boot_commit\ota_boot_commit.py COM5 --expected-build 20260814133439 --out-dir build-rtos-multicore-smoke\ota_commit_COM5_20260814133439` 通过，`SYSTem:FW:BUILD?` 返回 `20260814133439`，`SYSTem:ERRor?` 返回 `0,"No error"`。
  - `python tools\ota_send\ota_send.py COM6 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python tools\ota_boot_commit\ota_boot_commit.py COM6 --expected-build 20260814133439 --out-dir build-rtos-multicore-smoke\ota_commit_COM6_20260814133439` 通过，`SYSTem:FW:BUILD?` 返回 `20260814133439`，`SYSTem:ERRor?` 返回 `0,"No error"`。
  - `python tools\refmem_sync_hil_validate\refmem_sync_hil_validate.py --port-a COM5 --port-b COM6 --slot-a 0 --slot-b 1 --epoch 1 --run 1 --out-dir build-rtos-multicore-smoke\refmem_sync_hil_COM5_COM6_20260814133439` 通过，14 条记录全部 PASS。
  - HIL 关键结果：A/B 双向 HELLO 与 EPOCH 的 `RX` 均 `ACCEPTED`；A peer slot 1 与 B peer slot 0 均 `hello_seen=1, epoch_seen=1, frame_count=2`；A/B quality 均 `accepted_count=2` 且 frame、CRC、target、epoch 错误计数为 0。
- 还需完成：
  - 后续把同一帧通路接到真实 PIO SPI 物理 adapter service，再推进 DELTA、ACK_NACK、FENCE 和 QUALITY。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
  - `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 烧录两块板并执行 HELLO/EPOCH HIL；若通过，关闭 P4.5 阶段 2 的 SCPI 搬运闭环，再开始真实 PIO SPI adapter service。

### REFMEM-TASK-20260814-045 - TableRegistry image descriptor 与 activation 骨架

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将上一轮文档中的 active/staging/rollbackable table image contract 落到 `RefMemTableRegistry` 的首版代码形态。
  - 先建立 registry 级 descriptor、activation gate 和失败不污染 active 的验证，不提前改动真实业务表 buffer。
- 完成内容：
  - `refmem_table_registry.h/.c` 增加 `refmem_table_image_descriptor_t`，覆盖 role、state、table mask、package CRC、table seq、path hash、last result 和 evidence index。
  - 增加 active/staging/rollbackable 三类 descriptor，`refresh_active()` 初始化 active descriptor，`refresh_staging()` 和 `stage_table()` 更新 staging descriptor。
  - 增加 `refmem_table_activation_gate_t` 和 `refmem_table_registry_activate_staging()`：必须显式传入 RefMem idle、realtime idle、flash safe、CRC OK、owner OK、SlotClaim OK、DeploymentGate OK 和 command ACK OK；gate 失败时只记录结果，不改 active。
  - activation 成功时旧 active descriptor 进入 rollbackable，新 active descriptor 从 validated staging 派生，registry entry 的 active CRC 和状态同步更新，staging descriptor 清空。
  - 新增 `tests/unit/test_refmem_table_registry.c` 和 `tools/tests/run_refmem_table_registry_tests.ps1`，固化 active descriptor、gate 失败、成功 activation、rollbackable descriptor 和无有效 staging 拒绝验证。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814131250`，package CRC `0xA9FBFD22`。
- 还需完成：
  - 将 activation gate 的各个输入接到真实 RefMem mode、产品实时 idle/park、flash lockout/RAM-resident 状态、SlotClaimMap、DeploymentGate 和 command ACK。
  - 实现真实 active/staging/rollbackable table buffer 切换，而不是只切 registry descriptor 和 CRC。
  - 把 image descriptor 暴露到维护查询或专用验证脚本，形成板端 activation 正反向闭环。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_table_registry.h`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tests/unit/test_refmem_table_registry.c`
  - `tools/tests/run_refmem_table_registry_tests.ps1`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 继续 P0，将 owner validation callback 调度接入 TableRegistry，并把 validation result 写入 table id、owner id、validator id、reason 和 evidence。

### REFMEM-TASK-20260814-044 - RefMem table image activation 主线收敛

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 通读 `docs/refmem` 当前主线，承接 StorageAO 通用文件管理和 `.rmtp` staging load 的最新闭环。
  - 将下一轮 RefMem 工作从“能加载 staging snapshot”收敛到“可验证、可激活、可回滚的 active/staging/rollbackable table image”。
  - 明确 RefMem 向量表不承载 table image 或文件数据，只承载状态、CRC、path hash、version、seq、quality 和 evidence 摘要。
- 完成内容：
  - `README.md` 更新到 2026-08-14，补充 StorageAO 文件/目录 CRUD、`app_model.rmtp` 上传/读回、`SYSTem:REFMEM:LOAD:SD` staging 正向闭环，以及下一轮 activation 主线。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `Table Image Activation Contract`，定义 `active_image`、`staging_image`、`rollbackable_image` 三类镜像、activation 状态链、activation gate、descriptor 切换规则和失败回滚证据。
  - `REFMEM_DOMAIN_TODO.md` 将 Storage 文件上传和通用 CRUD 板端验证标记完成，并新增 table image descriptor、activation gate、owner validation evidence、activation 正向和失败回滚验证待办。
- 验证结果：
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
- 还需完成：
  - 实现真实 table image descriptor 和 active/staging/rollbackable 切换。
  - 实现 owner validation callback 调度，并将 table id、owner id、validator id、reason 和 evidence 写入 TableRegistry。
  - 增加 activation 正向和失败回滚脚本，覆盖 CRC 正确但 owner validation/SlotClaim/DeploymentGate/ACK 失败不得污染旧 active image。
- 关联文件：
  - `docs/refmem/README.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 进入 P0 table image activation 代码闭环，优先落地 descriptor、activation gate 和 owner validation callback。

### REFMEM-TASK-20260814-043 - 功能 AO 模板化与 ModelTurntableAO 首个可加载实例

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 取消 `TriggerMasterAO`、`TriggerAO`、`LinkSwitcherAO`、`PulseCounterAO`、`InstrumentControllerAO`、`ModelVnaAO` 和 `ModelTurntableAO` 的默认固定运行语义。
  - 保持 A0-A7 为通用逻辑槽位，功能节点最终由 SCPI 或 SD System Pack staging 确认装载，不在默认表中写死。
  - 先落地一个可验证的模拟转台实例，作为后续模型节点实例化的首个样例。
- 完成内容：
  - `refmem_application_model.c` 中功能 AO 名称改为 `Template.*` 语义；默认 active 基线只保留基础 RefMem/Loop/System/Calibration 类 owner，功能 AO 作为待加载模板。
  - `refmem_application_model.h` 增加模型信号 IO claim：`MODEL_TURNTABLE_PULSE`、`MODEL_VNA_READY`、`MODEL_VNA_TRIGGER` 和 `LINK_SWITCH_EVENT`。
  - `refmem_realtime_contract.c` 将模型信号 IO claim 映射为 `PIO + DMA + CORE1_RT` 实时能力需求。
  - 新增 `components/model_turntable`，实现模拟转台脉冲发生器：支持扫描起止/步长、脉宽、边沿、超时、速度和加速度配置；脉冲间隔按加速、匀速、减速形成疏密变化。
  - 新增 `SCPI_MODEL_COMMANDS`，提供 `CONFigure:MODEl:TURNtable:*`、`READ:MODEl:TURNtable:*` 和 `MODEl:TURNtable:STARt/STOP`。
  - 当前 debug 输出经 `sync_io_debug_model_write_pin()` 驱动 GPIO4..7 overlay；后续应迁移到 PIO/DMA/core1 预约输出。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814112552`，package CRC `0xD7B2581D`。
  - `python tools\ota_send\ota_send.py COM3 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python tools\ota_boot_commit\ota_boot_commit.py COM3 --expected-build 20260814112552 --out-dir build-rtos-multicore-smoke\ota_boot_commit_model_template_COM3` 通过，COM3 commit 成功。
  - COM3 查询 `READ:MODEl:TURNtable:LOAD?` 返回 `0,4294967295,0`，确认默认未加载。
  - 未加载时执行 `MODEl:TURNtable:STARt` 后，`SYSTem:ERRor?` 返回 `-200,"Execution error"`，符合必须先 LOAD 的约束。
  - 执行 `CONFigure:MODEl:TURNtable:LOAD 1,0` 后，`READ:MODEl:TURNtable:LOAD?` 返回 `1,1,0`。
  - 执行 `CONFigure:MODEl:TURNtable:TRIGger 0,0,10,1,2000,1,-1` 和 `CONFigure:MODEl:TURNtable:MOTion 10,20` 后，`MODEl:TURNtable:STARt/STOP` 均返回 `"OK"`，最终 `SYSTem:ERRor?` 为 `0,"No error"`。
- 还需完成：
  - 将 `ModelTurntableAO` 的 LOAD 结果接入 RefMem NodeLoad staging / activation，而不是只停留在本地运行时状态。
  - 将 debug GPIO 输出迁移为 PIO/DMA/core1 预约输出，并把脉冲计数、last tick、profile phase 写入 RefMem snapshot。
  - 按同一模式补 `ModelVnaAO`、`LinkSwitcherAO`、`PulseDistributorAO` 和 `VnaGatewayAO` 的可加载实例。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/refmem_realtime_contract.c`
  - `components/model_turntable/inc/model_turntable.h`
  - `components/model_turntable/src/model_turntable.c`
  - `middleware/scpi_port/inc/scpi_model_commands.h`
  - `middleware/scpi_port/src/scpi_model_commands.c`
  - `components/sync_io/inc/sync_io.h`
  - `components/sync_io/src/sync_io.c`
- 下一步：
  - 继续推进 P4.6 最小业务 HIL，先让模拟转台脉冲由可加载实例驱动，再由对端捕获并形成 VDC/RefMem 时间事实。

### REFMEM-TASK-20260814-042 - GPIO4..7 overlay 维护接口与预检工具

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 为 GPIO4..7 最小模型 overlay 增加方向安全维护接口和双板预检脚本。
  - 先确保 UART1 不占用 GPIO4/5，且脚本能以串口生命周期管理方式 release、驱动、读取和退出清理。
- 完成内容：
  - `sync_io.h/.c` 增加 `sync_io_debug_model_*` 维护函数，覆盖 GPIO4..7 output enable/value、release 和 input level snapshot。
  - `scpi_realtime_io_commands.c/h` 增加 `REALtime:IO:MODel:PROFile?`、`REALtime:IO:MODel:INPut:LEVel?`、`REALtime:IO:MODel:OUTPut:MASK`、`REALtime:IO:MODel:OUTPut:MASK?` 和 `REALtime:IO:MODel:OUTPut:RELease`。
  - `REALtime:IO:MODel:PROFile?` 返回 `base_pin,pin_count,uart_conflict_mask,uart_enabled`，当前期望为 `4,4,3,0`。
  - `REALtime:IO:MODel:OUTPut:MASK <enable_mask>,<value_mask>` 显式区分输出 owner 和输出电平；未 enable 的线恢复输入下拉。
  - 新增 `tools/debug_model_overlay_validate/debug_model_overlay_validate.py`，默认验证 X->Y GPIO4/GPIO5/GPIO7 和 Y->X GPIO6，运行前后 release 双方 GPIO4..7。
  - `tools/README.md`、`REFMEM_MIN_SYSTEM_PLAYBOOK.md` 和 `REFMEM_DOMAIN_TODO.md` 同步新增工具入口和待办状态。
- 验证结果：
  - `python -m py_compile tools\debug_model_overlay_validate\debug_model_overlay_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC 为 `0x2DF62B6E`，build id 为 `20260814104920`。
  - `python tools\ota_send\ota_send.py COM3 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，COM3 OTA 进入 `READY_TO_REBOOT`。
  - `python tools\ota_boot_commit\ota_boot_commit.py COM3 --expected-build 20260814104920 --out-dir build-rtos-multicore-smoke\ota_boot_commit_debug_model_COM3` 通过，COM3 启动并 commit 到 build `20260814104920`。
  - COM3 顺序查询 `REALtime:IO:MODel:PROFile?` 返回 `4,4,3,0`，确认 GPIO4..7 overlay profile 生效且 UART stdio 关闭。
  - COM3 顺序查询 `REALtime:IO:MODel:INPut:LEVel?` 返回 `4,4,0`。
  - COM4 首次 OTA 未完成：`ota_send.py COM4 ...` 打开端口失败，错误为 `PermissionError(13, '拒绝访问。', None, 5)`，确认为串口软件占用；关闭占用工具后重试通过。
  - `python tools\ota_send\ota_send.py COM4 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，COM4 OTA 进入 `READY_TO_REBOOT`。
  - `python tools\ota_boot_commit\ota_boot_commit.py COM4 --expected-build 20260814104920 --out-dir build-rtos-multicore-smoke\ota_boot_commit_debug_model_COM4` 通过，COM4 启动并 commit 到 build `20260814104920`。
  - COM4 顺序查询 `REALtime:IO:MODel:PROFile?` 返回 `4,4,3,0`。
  - `python tools\debug_model_overlay_validate\debug_model_overlay_validate.py --port-x COM3 --port-y COM4 --out-dir build-rtos-multicore-smoke\debug_model_overlay_COM3_COM4` 通过。
  - HIL 方向结果：`TURN_POS_PULSE` X.GPIO4 -> Y.GPIO4、`VNA_READY` X.GPIO5 -> Y.GPIO5、`VNA_TRIG` Y.GPIO6 -> X.GPIO6、`LINK_SWITCH` X.GPIO7 -> Y.GPIO7 均 PASS。
- 还需完成：
  - 进入最小业务 HIL：A1 位置脉冲、A4 捕获并更新时间事实、A3 预约链路切换、A5 触发虚拟网分、A2 返回 READY。
- 关联文件：
  - `components/sync_io/inc/sync_io.h`
  - `components/sync_io/src/sync_io.c`
  - `middleware/scpi_port/inc/scpi_realtime_io_commands.h`
  - `middleware/scpi_port/src/scpi_realtime_io_commands.c`
  - `tools/debug_model_overlay_validate/debug_model_overlay_validate.py`
  - `tools/README.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 将 overlay 节点写入 NodeLoad/Capability staging，并补齐最小业务 HIL 的 RefMem snapshot / quality / evidence 读取闭环。

### REFMEM-TASK-20260814-041 - GPIO4..7 最小模型 overlay 规划

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将用户当前已连接的 `GPIO4..7` 记录为最小系统业务模型 overlay。
  - 明确 Y 板模型实例整体向后挪一个槽位，避免和 X 板 `A3` 链路控制冲突。
  - 消除最小系统 UART1 对 `GPIO4/5` 的默认占用风险。
- 完成内容：
  - `REFMEM_MIN_SYSTEM_PLAYBOOK.md` 增加 `GPIO4..7` 最小模型 overlay，定义 X 板 `A1/A2/A3` 和 Y 板 `A4/A5`。
  - `HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md` 增加 overlay 硬件约束，明确每根线的唯一输出 owner、输入 receiver 和模拟信号。
  - `REFMEM_DOMAIN_TODO.md` 增加 `P4.6 - 最小模型系统 GPIO4..7 Overlay`，把 profile、node load、RealtimeCapabilityContract、方向安全脚本和业务 HIL 拆成后续待办。
  - `board_config.h` 注明 `GPIO4/5` 的 UART1 兼容定义只在 UART stdio 启用时有效，最小模型 overlay 可复用。
  - `board.c` 将 `drv_uart.h` include 和 `board_init_uart()` 内部初始化改为受 `PROJECT_ENABLE_UART_STDIO` 控制；默认最小系统 build 不初始化 UART1。
- 当前 overlay 线束：
  - `GPIO4`: X `A1` 模拟转台输出 `TURN_POS_PULSE` -> Y `A4` 脉冲分发输入。
  - `GPIO5`: X `A2` 模拟网分输出 `VNA_READY` -> Y `A5` VNA 网关输入。
  - `GPIO6`: Y `A5` VNA 网关输出 `VNA_TRIG` -> X `A2` 模拟网分输入。
  - `GPIO7`: X `A3` 链路控制输出 `LINK_SWITCH` -> Y `A5` VNA 网关输入。
- 还需完成：
  - 增加 debug model board profile 或等价配置表，显式声明 `GPIO4..7` overlay 与 UART1 互斥。
  - 增加方向安全检测脚本，确保运行前双方非 owner 引脚 release。
  - 增加最小业务 HIL，验证位置脉冲、VDC 发布时间、预约链路切换、虚拟网分触发和 READY 回传。
- 关联文件：
  - `boards/rp2350_trig/inc/board_config.h`
  - `boards/rp2350_trig/src/board.c`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 先做 GPIO4..7 方向安全脚本和 profile 声明，再把 overlay 节点写入 RefMem node load / realtime capability contract。

### REFMEM-TASK-20260814-040 - REFMEM_HELLO bundle helper

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 为 P4.5 阶段 2 的 `REFMEM_HELLO` 双向交换建立可复用 payload/frame 生成入口。
  - 将 board capability、adapter caps、layout/application/config CRC 和 build id CRC 收敛为标准 HELLO，而不是在后续 HIL 或 SCPI 中临时拼字段。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_transport_adapter.h`，抽出通用 adapter id、capability、state、error 和 `refmem_transport_caps_t`。
  - `refmem_pio_spi_adapter.h` 改为引用公共 transport adapter 头，避免后续 BISS-C/RJ45/UART/RS485 adapter 重复定义。
  - 新增 `components/distributed_refmem/inc/refmem_sync_hello.h`。
  - 新增 `components/distributed_refmem/src/refmem_sync_hello.c`。
  - 实现 `refmem_sync_hello_payload_from_board()`：从 board capability、adapter caps 和版本 CRC 生成 `refmem_sync_hello_payload_t`。
  - 实现 `refmem_sync_hello_encode_frame()`：把 HELLO payload 编码为标准 RefMem Sync frame。
  - 新增 `tests/unit/test_refmem_sync_hello.c` 和 `tools/tests/run_refmem_sync_hello_tests.ps1`，覆盖 payload 字段、frame validate、adapter inject/poll 和 payload byte match。
  - 将 `refmem_sync_hello.c` 加入根 `CMakeLists.txt` 固件源列表。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_hello_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_pio_spi_adapter_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_frame_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC 为 `0x36759853`，build id 为 `20260814103620`。
  - 本轮未烧录板端，未运行 COM3/COM4 HIL；真实两板 `REFMEM_HELLO` 双向交换仍未完成。
- 还需完成：
  - 定义 `task_refmem_sync` 内的 adapter service 调用点，接入 HELLO TX/RX 周期。
  - 增加两板 HIL 工具步骤：双方生成 HELLO、发送、接收、校验 build/layout/application/config/capability/adapter bundle。
  - 将 HELLO 结果写入 peer state 和 connection quality snapshot。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_transport_adapter.h`
  - `components/distributed_refmem/inc/refmem_sync_hello.h`
  - `components/distributed_refmem/src/refmem_sync_hello.c`
  - `components/distributed_refmem/inc/refmem_pio_spi_adapter.h`
  - `tests/unit/test_refmem_sync_hello.c`
  - `tools/tests/run_refmem_sync_hello_tests.ps1`
  - `CMakeLists.txt`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 设计 `RefMemSyncService` 的 TX/RX service 边界，并把 HELLO frame 进入 `refmem_sync_receive_frame()` 的路径固定下来。

### REFMEM-TASK-20260814-039 - PIO SPI adapter RX staging 前置闭环

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 为 P4.5 阶段 2 的 `REFMEM_HELLO` 双向交换建立 adapter-level RX staging 基础。
  - 先验证完整 RefMem Sync frame 可以在 adapter 层完成接收注入、缓存、poll、计数和错误归因，不接真实 PIO FIFO/DMA。
- 完成内容：
  - `refmem_pio_spi_adapter_t` 增加单帧 RX staging buffer、`rx_frame_size` 和 snapshot `rx_pending`。
  - 新增 `refmem_pio_spi_adapter_inject_rx_frame()`，作为后续 PIO/DMA RX ISR 或 HIL loopback 的受控入口。
  - `send` 和 `inject_rx_frame` 均改为调用 `refmem_sync_frame_validate()`，在 transport 边界校验 payload CRC。
  - `poll` 支持取出 pending RX frame，更新 `rx_count`、`last_rx_size`、`rx_pending` 和 `last_error`。
  - 单元测试增加 HELLO frame 注入/轮询、timestamp 保存、rx pending 清除和坏 payload CRC 拒绝路径。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_pio_spi_adapter_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_frame_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC 为 `0x39B8BCC8`，build id 为 `20260814103131`。
  - 本轮未烧录板端，未运行 COM3/COM4 HIL；真实两板 `HELLO` 双向交换仍未完成。
- 还需完成：
  - 定义 adapter TX/RX service 的 ownership，明确 `task_refmem_sync`、core1 realtime path 和后续 PIO/DMA ISR 的边界。
  - 增加 `REFMEM_HELLO` build/layout/app/config/capability bundle 生成 helper。
  - 将 HELLO send/poll 接到 RefMem Sync 状态机，并进入两板 HIL 验证。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_pio_spi_adapter.h`
  - `components/distributed_refmem/src/refmem_pio_spi_adapter.c`
  - `tests/unit/test_refmem_pio_spi_adapter.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 进入 `REFMEM_HELLO` bundle 生成和 adapter-level send/poll 接入，再规划两板 HIL。

### REFMEM-TASK-20260814-038 - PIO SPI adapter 能力契约映射

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将最小系统板 PIO SPI transport adapter 从“代码占位”升级为可被 `BoardCapabilityTable` 和 `RealtimeCapabilityContract` 表达的能力。
  - 保持默认业务 profile 不强制切换到 PIO SPI；PIO SPI 作为 bring-up adapter，可通过后续 board capability / instance 配置加载。
- 完成内容：
  - `refmem_application_model.h` 增加 `REFMEM_APP_IO_PIO_SPI_SYNC`，用于表达 PIO SPI 同步 adapter 的 IO 约束。
  - `refmem_application_model.h` 增加 `REFMEM_APP_IP_PIO_SPI_SYNC_DELTA`，用于表达 PIO SPI 承载 RefMem Sync delta 的类 IP 核能力。
  - `refmem_application_model.h` 增加 `REFMEM_APP_TRANSPORT_PIO_SPI`，用于后续 EventLink/adapter 选择。
  - `refmem_realtime_contract.c` 将 `PIO_SPI_SYNC` / `PIO_SPI_SYNC_DELTA` 映射到 `PIO + DMA + CORE1_RT` 能力。
  - 新增 `refmem_realtime_contract_transport_resource_claim()`、`refmem_realtime_contract_transport_io_claim()` 和 `refmem_realtime_contract_transport_ip_core_claim()`，统一由 transport 生成 resource/io/ip_core claim。
  - `refmem_application_model.c` 更新 transport linter 范围，并把 `PIO_SPI_SYNC` 纳入互斥 IO claim 检查。
  - 新增 `tests/unit/test_refmem_realtime_contract.c` 和 `tools/tests/run_refmem_realtime_contract_tests.ps1`。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_pio_spi_adapter_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC 为 `0x8FC6456B`，build id 为 `20260814102626`。
  - 本轮未烧录板端，未运行 COM3/COM4 HIL；变更仍属于能力契约和编译验证。
- 还需完成：
  - 将 `REFMEM_HELLO` 通过 PIO SPI adapter 完成双向交换。
  - 后续在 BoardCapability staging 或 System Pack 中增加可切换的 min-system PIO SPI profile。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/inc/refmem_realtime_contract.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/refmem_realtime_contract.c`
  - `tests/unit/test_refmem_realtime_contract.c`
  - `tools/tests/run_refmem_realtime_contract_tests.ps1`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 进入 P4.5 阶段 2：在不绑定真实 PIO 时序的前提下，先建立 adapter-level `REFMEM_HELLO` loopback/queue 骨架。

### REFMEM-TASK-20260814-037 - PIO SPI transport adapter skeleton

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 为两块最小系统板 RefMem Sync bring-up 建立首版 PIO SPI 风格 transport adapter skeleton。
  - 保持 adapter 层只承载总线收发、MTU、能力位、计数和错误快照，不绑定 RefMem active table，也不计算 VDC/DPLL。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_pio_spi_adapter.h`。
  - 新增 `components/distributed_refmem/src/refmem_pio_spi_adapter.c`。
  - 定义 adapter id、capability mask、max payload、preferred MTU、latency class、state 和 last error。
  - 定义 counters snapshot：tx/rx、tx reject、rx empty、bad frame、drop、timeout、last tx/rx size 和 optional RX timestamp。
  - 实现 `init`、`reset_counters`、`get_caps`、`get_snapshot`、`send` 和 `poll` 首版接口。
  - `send` 当前只验证 RefMem Sync frame header 和 payload size，并更新 tx/reject/drop/bad frame 计数。
  - `poll` 当前为空接收占位，返回 no frame 并更新 rx empty/last error；真实 PIO FIFO/DMA 接入留到后续阶段。
  - 新增 `tests/unit/test_refmem_pio_spi_adapter.c` 和 `tools/tests/run_refmem_pio_spi_adapter_tests.ps1`。
  - 将 `refmem_pio_spi_adapter.c` 加入根 `CMakeLists.txt` 固件源列表。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_pio_spi_adapter_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_frame_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC 为 `0x6267900E`，build id 为 `20260814101953`。
  - 本轮未烧录板端，未运行 COM3/COM4 HIL；新增代码仍属于 transport adapter skeleton。
- 还需完成：
  - 定义 PIO SPI adapter caps 与 `BoardCapabilityTable` / `RealtimeCapabilityContract` 的映射关系。
  - 实现真实 PIO SPI 帧定界、TX/RX FIFO、DMA 或 IRQ service，以及可选 RX timestamp 采样。
  - 接入 `REFMEM_HELLO` 双向发送与接收，进入两板最小闭环。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_pio_spi_adapter.h`
  - `components/distributed_refmem/src/refmem_pio_spi_adapter.c`
  - `tests/unit/test_refmem_pio_spi_adapter.c`
  - `tools/tests/run_refmem_pio_spi_adapter_tests.ps1`
  - `CMakeLists.txt`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 先补 PIO SPI adapter caps 到 BoardCapabilityTable / RealtimeCapabilityContract 的映射约束，再开始 `REFMEM_HELLO` 的 adapter-level loopback。

### REFMEM-TASK-20260814-006 - 总线无关 RefMem Sync frame 基础件

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将 RefMem Sync Protocol 的首版固定帧头和基础 payload 落成总线无关代码基础件。
  - 保持 RefMem 协议层不绑定 BISS-C、PIO SPI、RJ45、UART 或 RS485，后续 adapter 只承载完整协议帧。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_sync_frame.h`。
  - 新增 `components/distributed_refmem/src/refmem_sync_frame.c`。
  - 定义固定线格式帧头：magic、version、frame_type、flags、payload_size、source_slot、target_mask、epoch、run、seq、ack_seq、compact_time、header CRC 和 payload CRC。
  - 定义首版 frame type：`HELLO/EPOCH/DELTA/COMMAND/ACK_NACK/FENCE/QUALITY`。
  - 定义 `HELLO`、`EPOCH`、`DELTA`、`COMMAND`、`ACK_NACK`、`FENCE` 和 `QUALITY` 的基础 payload 结构。
  - 使用显式 little-endian encode/decode，避免直接发送 C struct padding。
  - 将 `refmem_sync_frame.c` 加入根 `CMakeLists.txt` 固件源列表。
  - 新增 `tests/unit/test_refmem_sync_frame.c` 和 `tools/tests/run_refmem_sync_frame_tests.ps1`。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_frame_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC 为 `0x434F3DD1`，build id 为 `20260814100501`。
  - 本轮未烧录板端，未运行串口 HIL；新增代码目前只作为总线无关协议帧基础件接入构建。
- 还需完成：
  - 建立 `refmem_sync.h/.c`，承载接收侧 validate/commit/visible 状态机。
  - 建立 PIO SPI transport adapter skeleton，并接入最小系统两板 HIL。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_sync_frame.h`
  - `components/distributed_refmem/src/refmem_sync_frame.c`
  - `tests/unit/test_refmem_sync_frame.c`
  - `tools/tests/run_refmem_sync_frame_tests.ps1`
  - `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 补齐 `COMMAND/ACK_NACK/FENCE/QUALITY` payload，随后进入 PIO SPI adapter skeleton。

### REFMEM-TASK-20260814-007 - RefMem Sync 接收状态机骨架

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 在总线无关 frame 基础上新增 RefMem Sync 接收侧状态机骨架。
  - 保持接收侧只做 validate、quality 和进入 mirror/commit 前置判断，不直接写 64 KB active fact。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_sync.h`。
  - 新增 `components/distributed_refmem/src/refmem_sync.c`。
  - 定义 `refmem_sync_context_t`，保存本地 slot、active epoch/run、peer state 和 quality counters。
  - 定义接收结果：accepted、bad argument、frame invalid、source invalid、target mismatch、epoch mismatch、duplicate seq、stale seq。
  - 实现 `refmem_sync_receive_frame()`：调用 `refmem_sync_frame_validate()`，检查 source slot、target mask、epoch/run、duplicate/stale/gap，并更新 peer/quality counter。
  - 新增 `tests/unit/test_refmem_sync.c` 和 `tools/tests/run_refmem_sync_tests.ps1`。
  - 将 `refmem_sync.c` 加入根 `CMakeLists.txt` 固件源列表。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_frame_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC 为 `0xFDD8D052`，build id 为 `20260814101333`。
  - 本轮未烧录板端，未运行串口 HIL；新增代码目前只作为总线无关接收状态机骨架接入构建。
- 还需完成：
  - 将 validated frame 映射到 RefMem mirror/staging view。
  - 接入 `RefMemSlotContract`，对 delta field writer、宽度、值域和生命周期做真实校验。
  - 建立 PIO SPI transport adapter skeleton，并把 adapter quality 映射到 `DistributedConnectionQualityTable`。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_sync.h`
  - `components/distributed_refmem/src/refmem_sync.c`
  - `tests/unit/test_refmem_sync.c`
  - `tools/tests/run_refmem_sync_tests.ps1`
  - `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 进入 P4.5 阶段 1，建立 PIO SPI adapter skeleton 的 caps/counter snapshot。

## 记录规则

每条任务记录使用以下格式：

```text
### REFMEM-TASK-YYYYMMDD-NNN - 标题

- 状态：
- 日期：
- 任务目标：
- 完成内容：
- 验证结果：
- 还需完成：
- 关联文件：
- 下一步：
```

## 当前目标

RefMem Domain 当前目标是从 `components/distributed_refmem/` 的本地 64 KB 表骨架，升级为 HAOFV 内部基础主域：

```text
DistributedRefMemAO
+ RefMemSyncFB
+ DistributedVectorTable
+ 静态分布式应用模型
+ command / ACK-NACK
+ deployment gate
+ connection quality
```

当前实现已经落地 `refmem_vector_table.h/.c`、`refmem_application_model.h/.c`、静态模型 linter、package CRC 和 `SYSTem:REFMEM:LOAD:*` staging 骨架。下一阶段主线按以下顺序推进：

```text
RefMemTableRegistry
-> staging/active/rollbackable table image
-> SlotClaimMap + 16 candidate overflow evidence
-> RefMemSlotContract internal validation
-> command ACK/NACK
-> REFMEM_DELTA / REFMEM_EPOCH sync protocol
```

## 任务记录

### REFMEM-TASK-20260814-036 - 双板 PIO 可配置调试接线 profile

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 为两块最小系统板组网验证建立方便接线且可自定义的 PIO 调试 profile。
  - 保持该接线只属于最小系统调试约束，不上升为产品板 pin map。
- 完成内容：
  - 新增 `PROJECT_SYNC_IO_INPUT_BASE_PIN` 和 `PROJECT_SYNC_IO_OUTPUT_BASE_PIN` 构建参数。
  - 当前默认 profile 为 `GPIO16..19` 输入、`GPIO21..24` 输出；后续可通过 CMake 切换整组 base pin。
  - `board_config.h` 从 active profile 派生 `TRIG_IN`、`TRIG_OUT`、`PULSE_OUT`、`RJ45_TRIG_IN/OUT` 等语义脚。
  - `sync_io_hw_profile` 的编译期断言改为检查连续 4 位、范围合法和输入/输出不重叠。
  - `trigger_fb` 默认触发源改为从 active profile 读取，不再硬编码 `GPIO16`。
  - `HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md` 记录双板交叉接线表和 `GPIO12..15` 避让规则。
  - 增加 `REALtime:IO:PROFile?`、`REALtime:IO:INPut:LEVel?`、`REALtime:IO:OUTPut:MASK`、`REALtime:IO:OUTPut:MASK?` 和 `REALtime:IO:OUTPut:RELease` 维护接口。
  - 新增 `tools/two_board_io_validate/two_board_io_validate.py`，逐位静态驱动两块板的 active output group 并读取对端 input mask，自动识别漏接、错位和短接。
  - `REFMEM_DOMAIN_TODO.md` 将双板 PIO 预检待办标记完成，后续真实两板 HIL 验证仍保留。
- 验证结果：
  - `python -m py_compile tools\two_board_io_validate\two_board_io_validate.py tools\realtime_scpi_validate\realtime_scpi_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814083032`，package CRC `0x2071E6F5`。
  - `COM3` 和 `COM4` OTA 升级并 commit 到 build `20260814083032`。
  - `python tools\refmem_network_validate\refmem_network_validate.py --port-a COM3 --port-b COM4` 通过，双方 SlotClaimMap CRC 均为 `386979554`。
  - `python tools\two_board_io_validate\two_board_io_validate.py --port-a COM3 --port-b COM4` 首次按直通期望执行，测得方向性线序：A->B 为 `OUT0->IN1, OUT1->IN2, OUT2->IN0, OUT3->IN3`；B->A 为 `OUT0->IN2, OUT1->IN1, OUT2->IN0, OUT3->IN3`。
  - 自动线序检测工具默认改为按上述实测 logical remap 验收，保留 `--expect-a-to-b` / `--expect-b-to-a` 参数支持后续改线。
  - `python tools\two_board_io_validate\two_board_io_validate.py --port-a COM3 --port-b COM4 --out-dir build-rtos-multicore-smoke\two_board_io_COM3_COM4_remap` 通过，确认当前线束按 logical remap 可用。
- 还需完成：
  - 评估后续是否需要将 logical line remap 从工具参数升级到 RefMem/IO profile 表。
  - 后续产品板 profile 需要按产品板硬件约束设置独立构建参数，不沿用调试默认接线。
- 关联文件：
  - `boards/rp2350_trig/inc/board_config.h`
  - `components/sync_io/inc/sync_io_hw_profile.h`
  - `docs/hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md`
  - `docs/sync/SYNC_IO_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 构建并烧录最小系统 profile 后，用 `SYSTem:REFMEM:*` 和后续 PIO 预检确认双板基础链路。

### REFMEM-TASK-20260814-035 - CLAIM_CONFLICT / RELEASE / RESOLVE 帧

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 补齐 SlotClaim 自组网协调消息族的静态帧定义。
  - 保持当前阶段仍为协议基础件，不接运行时 RJ45 收发。
- 完成内容：
  - `refmem_claim_protocol.h/.c` 增加 `refmem_claim_resolution_entry_t` 与 `refmem_claim_resolution_frame_t`。
  - `CLAIM_CONFLICT` 和 `CLAIM_RESOLVE` 共享 resolution entry 数组，覆盖 candidate、slot、board、assigned slot、claim_state、reason、evidence_id 和 claim_crc。
  - 增加 `refmem_claim_release_payload_t` 与 `refmem_claim_release_frame_t`，覆盖 slot、board、release_seq 和 claim_crc。
  - 增加 conflict/release/resolve 的 init/validate API。
  - SlotClaim 单元测试覆盖 payload CRC mutation、wrong frame type 和 bad payload count。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过；当前机器无 host C 编译器，结果为 ARM GCC 编译检查通过，未执行主机 exe。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped；HIL 串口测试未启用。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0x9623ECA9`。
- 还需完成：
  - 接入 RJ45_SYNC_RING 收发、epoch stale 检查、SlotClaimMap 聚合与 commit。
  - 将两板 baseline 工具升级为真实 `CLAIM_*` 组网验证。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_claim_protocol.h`
  - `components/distributed_refmem/src/refmem_claim_protocol.c`
  - `tests/unit/test_refmem_slot_claim.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 定义 RefMem claim RX/TX staging queue 或 adapter，使协议帧能进入 RefMemAO 聚合，但仍不直接修改 active fact。

### REFMEM-TASK-20260814-034 - CLAIM_HELLO 与 CLAIM_COMMIT 帧

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 在 `CLAIM_PROPOSE` 之外补齐两板发现和提交所需的最小控制帧。
  - 继续保持协议基础件只做 init/validate，不接运行时收发。
- 完成内容：
  - `refmem_claim_protocol.h/.c` 增加 `refmem_claim_hello_frame_t` 和 `refmem_claim_commit_frame_t`。
  - `CLAIM_HELLO` payload 覆盖 board id、uuid、capability、IO/IP core、hw profile、active slot、loaded instance、baseline/VDC ready 和 claim CRC。
  - `CLAIM_COMMIT` payload 覆盖 SlotClaimMap CRC、slot/assigned/conflict/overflow/evidence 计数、committed node mask 和 gate_ready。
  - 协议实现抽出 header 初始化、header 校验和 raw payload CRC helper，保持 `CLAIM_PROPOSE` 外部 API 不变。
  - SlotClaim 单元测试增加 HELLO/COMMIT payload CRC 和 frame type 错误检测。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过；当前机器无 host C 编译器，结果为 ARM GCC 编译检查通过，未执行主机 exe。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped；HIL 串口测试未启用。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0x36309B39`。
- 还需完成：
  - 继续补 `CLAIM_CONFLICT/RELEASE/RESOLVE` 帧。
  - 接入 RJ45_SYNC_RING 收发、epoch stale 检查和 SlotClaimMap commit。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_claim_protocol.h`
  - `components/distributed_refmem/src/refmem_claim_protocol.c`
  - `tests/unit/test_refmem_slot_claim.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 增加 conflict/release/resolve 帧，完成 SlotClaim 协调消息族的静态协议定义。

### REFMEM-TASK-20260814-033 - CLAIM_PROPOSE 帧协议基础

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 为后续 RJ45 `CLAIM_*` 自组网协调提供可校验的帧格式基础。
  - 当前只定义和验证 `CLAIM_PROPOSE`，不接运行时发送/接收。
- 完成内容：
  - 新增 `refmem_claim_protocol.h/.c`。
  - 定义 claim frame header：magic、version、frame_type、claim_epoch、claim_seq、source board、payload_count、payload_crc32 和 header_crc32。
  - 定义 `refmem_claim_propose_frame_t`，最多携带 16 条 `SlotClaimProposal`。
  - 增加 `refmem_claim_propose_frame_init()` 和 `refmem_claim_propose_frame_validate()`。
  - SlotClaim 单元测试扩展 payload CRC、header CRC 和超 16 candidate 拒绝检查。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过；当前机器无 host C 编译器，结果为 ARM GCC 编译检查通过，未执行主机 exe。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped；HIL 串口测试未启用。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0xEE4E9E15`。
- 还需完成：
  - 定义并实现 `CLAIM_HELLO/CONFLICT/RELEASE/RESOLVE/COMMIT` 帧。
  - 接入 RJ45_SYNC_RING 收发、epoch stale 检查和 SlotClaimMap commit。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_claim_protocol.h`
  - `components/distributed_refmem/src/refmem_claim_protocol.c`
  - `components/distributed_refmem/inc/refmem_slot_claim.h`
  - `tests/unit/test_refmem_slot_claim.c`
  - `tools/tests/run_refmem_slot_claim_tests.ps1`
  - `CMakeLists.txt`
- 下一步：
  - 将 claim frame 与 RefMem Sync frame/RMA delta 分层对齐，避免后续帧类型互相挤占。

### REFMEM-TASK-20260814-032 - 两板 RefMem baseline 验证工具骨架

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 为后续两块最小系统板组网验证建立专用工具入口。
  - 当前只验证基础 SCPI snapshot，不假设 RJ45 `CLAIM_*` 协调协议已完成。
- 完成内容：
  - 新增 `tools/refmem_network_validate/refmem_network_validate.py`。
  - 工具显式管理两个串口生命周期：分别 open、settle、清输入/输出、查询、flush、close。
  - 单板 baseline 覆盖 `*IDN?`、`SYST:FW:BUILD?`、`SYST:CORE?`、`SYST:SYNC:VDC:STAT?`、`SYST:SYNC:VDC:DPLL:STAT?`、`SYST:CONFigure:STAT?`、`SYST:REFMEM:CLAIM? 0` 和 `SYST:REFMEM:CLAIM:EVIDence? 0`。
  - 双板比较默认要求 build id 和 SlotClaimMap CRC 一致，可通过 `--allow-build-mismatch` 或 `--allow-map-mismatch` 放宽。
  - 验证结果写入 `summary.json` 和 `summary.txt`。
- 验证结果：
  - `python -m py_compile tools\refmem_network_validate\refmem_network_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped；两板工具未实际打开硬件串口。
- 还需完成：
  - 接入真实 RJ45 `CLAIM_*` 协调后，扩展重复 slot claim、缺失 UUID、stale、overflow、owner validation 拒绝和 commit 后 CRC 一致性验证。
- 关联文件：
  - `tools/refmem_network_validate/refmem_network_validate.py`
  - `tools/README.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 增加 SlotClaim stale/CRC 字段，再规划 RJ45 claim frame。

### REFMEM-TASK-20260814-031 - SlotClaimEvidence 诊断视图

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将 SlotClaim 负向结果从计数升级为可查询 evidence，支撑两板组网冲突诊断。
- 完成内容：
  - `refmem_slot_claim_map_t` 增加 `evidence_count` 和最多 16 条 `refmem_slot_claim_evidence_t`。
  - duplicate claim、disabled slot、缺失稳定 UUID、invalid slot 和 active slot 容量 overflow 会记录 evidence；UUID/profile 不再作为 A slot 硬绑定依据。
  - 新增 `refmem_slot_claim_find_evidence()`。
  - 新增维护查询 `SYSTem:REFMEM:CLAIM:EVIDence? [evidence_id]`，不改变既有 `SYSTem:REFMEM:CLAIM?` 字段顺序。
  - SlotClaim 单元测试增加 evidence_count、reason、board_id 和 candidate_id 断言。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过；当前机器无 host C 编译器，结果为 ARM GCC 编译检查通过，未执行主机 exe。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped；HIL 串口测试未启用。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0x117B81F7`。
- 还需完成：
  - stale claim、claim CRC 和跨板 `CLAIM_*` 协调 evidence 仍待接入。
  - 两块最小系统板组网 HIL 需要验证双方 `SlotClaimMap CRC` 一致和 evidence 能闭环定位冲突来源。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_slot_claim.h`
  - `components/distributed_refmem/src/refmem_slot_claim.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_slot_claim.c`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 增加两板组网验证工具骨架，串口生命周期必须由统一 helper 管理。

### REFMEM-TASK-20260814-030 - SlotClaim 负向单元测试基础

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 先用纯 C 单元测试固定 SlotClaimMap / gate 的负向基础行为。
  - 为后续两块最小系统板组网验证建立可复用的算法判定地基。
- 完成内容：
  - `BoardCapabilityTable` 容量从 8 个 active slot 扩为 16 个候选容量，默认 active profile 的 `board_count` 仍保持 8，不改变当前板端默认行为。
  - `refmem_slot_claim_derive_map()` 增加 active slot 容量边界：candidate 数量超过 A0-A7 slot 数量后，后续候选进入 overflow，而不是被误判为普通重复 claim。
  - 新增 `tests/unit/test_refmem_slot_claim.c`，覆盖 nominal assignment、loaded instance mask、duplicate claim、缺失 UUID 和第 9 个候选 overflow。
  - 新增 `tools/tests/run_refmem_slot_claim_tests.ps1`，沿用现有纯 C 测试风格：有 host gcc/clang 时运行 exe，否则退化为 ARM GCC 编译检查。
  - `tools/README.md`、`tests/README.md` 和 `REFMEM_DOMAIN_TODO.md` 增加测试入口和两板组网验证待办。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过；当前机器无 host C 编译器，结果为 ARM GCC 编译检查通过，未执行主机 exe。
- 还需完成：
  - 增加 stale claim、claim CRC、任意 slot claim、9-16 全矩阵和超过 16 candidate rejected 的测试。
  - 后续在两块最小系统板上验证 `CLAIM_*` 协调消息、slot 冲突处理、RefMem snapshot 一致性和 VDC baseline。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/refmem_slot_claim.c`
  - `tests/unit/test_refmem_slot_claim.c`
  - `tools/tests/run_refmem_slot_claim_tests.ps1`
  - `tools/README.md`
  - `tests/README.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 补齐 SlotClaimEvidence 诊断视图，并把两板最小系统组网验证拆成 HIL 工具任务。

### REFMEM-TASK-20260814-029 - SlotClaim gate HIL 验证入口固化

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 把 SlotClaim 本地 RUN gate 的正向板端验证固化到标准工具和 pytest HIL。
  - 继续保持串口生命周期集中管理，pytest 默认不打开串口。
- 完成内容：
  - `tools/multicore_board_validate/multicore_board_validate.py` 增加 `test_refmem_slot_claim_gate()`。
  - 验证内容覆盖 `SYSTem:CONFigure:STAT?` ready/ACK/NACK、`SYSTem:REFMEM:CLAIM? 0/2/7` 的 map header、gate_ready、error counters、assignment、claim_state、online_required 和 claim_crc。
  - `ALL_TESTS` 加入 `refmem_slot_claim_gate`，CLI 全量验证会自动覆盖该项。
  - `tests/hil/test_multicore_board_validate.py` 将 `refmem_slot_claim_gate` 加入共享 `hil_serial` fixture 的只读 smoke；默认 pytest 仍跳过 HIL，不会自行打开串口。
  - `tools/README.md` 和 `REFMEM_DOMAIN_TODO.md` 同步记录验证入口。
- 验证结果：
  - `python -m py_compile tools\multicore_board_validate\multicore_board_validate.py tests\hil\test_multicore_board_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped；HIL 串口测试未启用。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0xB77FAB04`。
- 还需完成：
  - 板端实际运行 `python -m pytest -m hil --run-hil --hil-port COMx` 或 `python tools/multicore_board_validate/multicore_board_validate.py COMx --tests refmem_slot_claim_gate`。
  - 增加负向 SlotClaim 矩阵：重复 claim、缺失 UUID、stale、9-16 候选 overflow、超过 16 候选 rejected。
- 关联文件：
  - `tools/multicore_board_validate/multicore_board_validate.py`
  - `tests/hil/test_multicore_board_validate.py`
  - `tools/README.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 增加 SlotClaim 负向测试所需的 staging/fault injection 入口，避免直接修改 active profile。

### REFMEM-TASK-20260814-028 - SlotClaim 本地 RUN gate 接入

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将 SlotClaimMap 首版结果接入 RefMem DeploymentGate 和系统 config RUN gate。
  - 让 claim 冲突、缺失 UUID、overflow、stale 和 required slot 缺失不只可查询，还能实际阻止 ready/RUN。
- 完成内容：
  - `refmem_slot_claim_assignment_t` 增加 `online_required`，用于区分 required slot 与 spare/dynamic slot。
  - 新增 `refmem_slot_claim_gate_status_t` 和 `refmem_slot_claim_gate_evaluate()`，输出 gate ready、first_bad_slot、first_reason、conflict/overflow/required_missing/mismatch 统计和 map CRC。
  - RefMem application model 的 `DeploymentGate` 静态验证接入 claim gate；本地 claim gate fail 时 model validation fail。
  - `system_manager` config gate 接入 claim gate；分布式配置 CRC 合法但 claim gate fail 时，`SYSTem:CONFigure:STAT?` 的 `ready=0`、`gate_state=2`、`nack_flags=target_mask`。
  - `SYSTem:REFMEM:CLAIM? [slot_id]` 返回值扩展 gate 字段和 `online_required` 字段，便于解释 RUN gate 拒绝原因。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0xCF7DDAB2`。
- 还需完成：
  - 板端/HIL 验证 `SYSTem:REFMEM:CLAIM?` 与 `SYSTem:CONFigure:STAT?` 的 gate 行为一致。
  - 增加第 9 到第 16 个候选的 overflow evidence，并接入 DeploymentGate evidence。
  - 接入 RJ45 `CLAIM_*` 协调消息后的跨板 claim gate。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_slot_claim.h`
  - `components/distributed_refmem/src/refmem_slot_claim.c`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/system_manager/src/system_manager.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 增加 SlotClaim gate 的 Python/板端验证脚本，然后推进 overflow evidence 和 RJ45 claim 协调。

### REFMEM-TASK-20260814-027 - SlotClaimMap 首版本地派生

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 建立 SlotClaimMap 首版代码组件，把 B0-Bx board/profile 节点映射到 A0-A7 resolved assignment。
  - 让 RealtimeCapabilityContract 使用 SlotClaimMap resolved assignment，不再依赖 `active_default_slot` 直接查 BoardCapability。
- 完成内容：
  - 新增 `refmem_slot_claim.h/.c`，定义 `SlotClaimProposal`、`SlotClaimAssignment` 和 `SlotClaimMap` 首版结构。
  - `refmem_slot_claim_derive_map()` 从 GenericNode、BoardCapability、NodeLoad 和 FB instance 派生本地 claim map，记录 candidate_count、assigned_count、conflict_count、overflow_count、disabled_count、loaded_instance_mask、claim state、reason 和 CRC。
  - `refmem_realtime_contract_derive_from_claim_map()` 接入 SlotClaimMap resolved assignment，application model linter 已改用该入口验证资源、IO 和类 IP 核能力。
  - 增加 `SYSTem:REFMEM:CLAIM? [slot_id]` 维护查询，返回 map 摘要和指定 A slot assignment。
  - 文档同步 SlotClaimMap 首版能力边界：当前从 active default profile 本地派生，`claim_epoch=1`；RJ45 `CLAIM_*` 自组网消息、overflow evidence 和 DeploymentGate 接入仍是后续项。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0xAFB84E5B`。
- 还需完成：
  - 接入 RJ45 `CLAIM_HELLO/PROPOSE/CONFLICT/RESOLVE/COMMIT` 协调消息。
  - 增加第 9 到第 16 个未分配候选的 overflow evidence，并将未解决冲突接入 DeploymentGate node_check。
  - 增加 HIL 验证：`SYSTem:REFMEM:CLAIM?` 与 BoardCapability、NodeLoad 和 realtime contract 结果一致。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_slot_claim.h`
  - `components/distributed_refmem/src/refmem_slot_claim.c`
  - `components/distributed_refmem/inc/refmem_realtime_contract.h`
  - `components/distributed_refmem/src/refmem_realtime_contract.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 将 SlotClaimMap 接入 DeploymentGate/RUN gate，先让本地未解决 conflict/mismatch 可以拒绝 RUN，再推进 RJ45 自组网协调。

### REFMEM-TASK-20260814-026 - RealtimeCapabilityContract 首版组件

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 把“加载节点实例时必须同时加载实时能力”的规则从文档和 linter 私有函数中抽成 RefMem 内部基础组件。
  - 为后续 DeploymentGate、SlotClaimMap 和板端 HIL 验证提供统一的资源/IO/类 IP 核 contract 派生入口。
- 完成内容：
  - 新增 `refmem_realtime_contract.h/.c`，定义 `refmem_realtime_contract_t` 和 `refmem_realtime_contract_derive()`。
  - 首版 contract 从 NodeLoad、FB instance、GenericNode 和 BoardCapability 派生 `resource_claim`、`io_claim`、`ip_core_claim`、目标 capability、目标 IO constraint、目标 IP core mask、缺失掩码和结果码。
  - application model linter 改为调用 `refmem_realtime_contract_derive()`，能力校验错误归入 `REFMEM_APP_LINT_BAD_REALTIME_CONTRACT`。
  - 当前没有 SlotClaimMap resolved assignment，因此临时通过 `BoardCapabilityTable.active_default_slot == node_id` 关联 B 节点和 A slot；文档中已标记后续替换点。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped；HIL 串口测试未启用。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0xCD54C6D1`。
- 还需完成：
  - 将 contract 输入从 `active_default_slot` 替换为 SlotClaimMap resolved assignment。
  - 接入 DeploymentGate/RUN gate，并增加 time budget、IP core version、PIO program id、DMA channel policy、IRQ source 和 fallback policy 校验。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_realtime_contract.h`
  - `components/distributed_refmem/src/refmem_realtime_contract.c`
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 推进 SlotClaimMap 首版，让板卡能力、逻辑 slot 和实时 contract 的关联不再依赖 default slot。

### REFMEM-TASK-20260814-025 - BoardCapability SCPI staging 闭环

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 让物理板能力可以通过 SCPI 受控提交到 RefMem staging，支撑 SD System Pack 之外的调试加载和能力实例化验证。
  - 保持 A0-A7 逻辑 slot 与 B0-Bx 物理/profile 节点解耦，SCPI 不直接修改 active BoardCapabilityTable。
- 完成内容：
  - 增加 `SYSTem:REFMEM:LOAD:BOARD <board_id>,<board_uuid_crc32>,<capability_mask>,<io_constraint_mask>,<ip_core_mask>,<default_persona_mask>,<hw_profile_crc32>,<active_default_slot>,<online_required>`。
  - 增加 `SYSTem:REFMEM:LOAD:BOARD:STATus?`，返回 board capability staging snapshot，覆盖 load_seq、mode、active/staging CRC、lint/error 和当前候选字段。
  - `refmem_application_model_stage_scpi_board_capability()` 校验 board 范围、`REFMEM+VDC` baseline、默认 slot 范围和基础字段后，只写 staging snapshot 与 TableRegistry staging 状态。
  - `SCPI_COMMANDS.md` 和 `REFMEM_DOMAIN_ARCHITECTURE.md` 同步说明：板卡能力可由 SD System Pack 或 SCPI staging 提交，但 active profile 仍必须等待后续 CRC、owner validation、IO/IP 核检查、DeploymentGate 和 activation。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped；HIL 串口测试未启用。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0x8AB7A16E`。
- 还需完成：
  - 将 BoardCapabilityTable 接入真实多条 staging image、owner validation callback、active/rollbackable 切换和 RUN gate。
  - 增加 HIL 验证：加载 link-control/BISS-C 候选后，确认对应 PIO/DMA/core1_rt 类 IP 核能力可以由 DeploymentGate 检查并通过 RefMem snapshot 闭环。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 推进 `RealtimeCapabilityContract` 派生和 DeploymentGate 接入，让 board capability 不只可加载，还能约束实际 RUN。

### REFMEM-TASK-20260814-024 - NodeLoad 实时能力契约补齐

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 补齐“RefMem 加载节点实例时必须同时加载 core1 实时能力、IO 约束和类 IP 核能力”的架构与代码表达。
  - 将 A0-A7 统一收敛为 RefMem 逻辑槽位，把当前项目物理/实例标签改为 B0-B4，避免 slot 与板卡定位混淆。
- 完成内容：
  - `refmem_fb_instance_entry_t` 增加 `ip_core_claim` 字段，首版覆盖 `PULSE_CAPTURE`、`PULSE_FIRE`、`LINK_SEQUENCE`、`BISS_C_CODEC`、`RJ45_SYNC_DELTA` 和 `VDC_DPLL`。
  - 静态模型 linter 将 `ip_core_claim` 映射为 capability gate，确保链路控制、BISS-C 编解码等类 IP 核不会被当作普通 GPIO。
  - 默认 profile 中 `B2.LinkSwitcherAO` 明确声明 `CORE1_RT + PIO + DMA + LINK_CONTROL`，并补齐 FIRE_LOAD、DONE、FAULT、link timestamp、link sequence state 等事件/数据连接。
  - BISS-C 模型节点声明为 `BISS_C_CODEC` 类 IP 核，要求 PIO、DMA、core1_rt 和 BISS-C IO。
  - 增加 `REFMEM_APP_CAP_REFMEM` 和 `REFMEM_APP_CAP_VDC`，当前静态表所有 A0-A7 slot 候选都具备 `REFMEM + VDC` baseline，linter 对 baseline 做硬检查。
  - 明确 `VDC` 是每个物理节点参与虚拟 DC 时间语义的基础能力，`VDC_DPLL` 才是运行 DPLL owner 的类 IP 核能力。
  - 增加 `BoardCapabilityTable` 首版代码结构，描述 B0-Bx 物理/模型节点能力、IO 约束、类 IP 核、默认 persona 和默认 slot，并参与 package CRC 与 linter。
  - 将 `BoardCapabilityTable` 升级为 TableRegistry 正式表，table id 为 1；`.rmtp` / SD System Pack table count 从 8 增加到 9，表顺序为 ApplicationMap、BoardCapability、GenericNode、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate、ConnectionQuality。
  - 增加 `SYSTem:REFMEM:BOARD? [board_id]`，可读取 active BoardCapabilityTable 的 B 节点能力、IO 约束、类 IP 核、默认 slot 和 CRC。
  - 文档明确板卡能力必须支持 SD System Pack 和受控 SCPI staging 加载；固件内置表只作为 default/factory profile，active 表必须由 CRC、owner validation、IO 约束、类 IP 核和 DeploymentGate 验证后激活。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `RealtimeCapabilityContract`，明确 RefMem 只验证和发布实时能力事实，实际执行仍由 core1/PIO/DMA/域状态机 owner 完成。
  - `REFMEM_DOMAIN_TODO.md` 增加 `BoardCapabilityTable`、动态 SlotClaim、realtime contract 派生和 HIL 验证待办。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0xD66CCA10`。
- 还需完成：
  - 将 `BoardCapabilityTable` 接入真实 staging/active table image 切换和受控 SCPI 写入 staging。
  - 实现 `RealtimeCapabilityContract` 派生组件，并接入 DeploymentGate 和 RUN gate。
  - 做板端/HIL 验证：加载 link-control 候选后确认 FIRE_LOAD、脉冲捕获、链路序列状态与 RefMem snapshot 闭环。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 进入 `BoardCapabilityTable` / `SlotClaimProposal` 细化，让 B0-B4 物理能力与 A0-A7 slot assignment 完全解耦。

### REFMEM-TASK-20260814-023 - StorageAO 通用文件管理基础件

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将 `app_model.rmtp` 写入 SD 的能力从 RefMem 专用 package 命令纠偏为 HAOFV StorageAO 通用文件管理基础件。
  - 支持文件和目录增删改查，并保持 SCPI 不直接调用 FatFs、RefMem 向量表不承载文件数据。
- 完成内容：
  - `SYSTem:STORage:FILE:*` 增加通用文件事务写入、info、read、delete、rename。
  - `SYSTem:STORage:DIRectory:*` 增加 create、delete、rename、catalog。
  - StorageManager 增加 `FILE_DELETE`、`FILE_RENAME`、`DIRECTORY_CREATE`、`DIRECTORY_DELETE`、`DIRECTORY_RENAME` job，并通过 StorageAO service 和资源仲裁访问 SD。
  - FatFs port 增加 delete 和 rename 封装；文件写入仍使用 tmp + sync + rename 原子替换。
  - `tools/refmem_pack_write/refmem_pack_write.py` 改为通过 `SYSTem:STORage:FILE:WRITe:*` 写入 `/refmem/app_model.rmtp`，再执行 `SYSTem:REFMEM:LOAD:SD`。
  - 新增 `tools/storage_scpi_validate/storage_scpi_validate.py`，固化通用 Storage 文件/目录 CRUD 验证流程。
  - `SCPI_COMMANDS.md` 和 RefMem 架构/TODO 同步为通用 Storage 文件管理接口，删除文档中的 RefMem package 专用入口。
- 验证结果：
  - `python -m py_compile tools/refmem_pack_write/refmem_pack_write.py tools/storage_scpi_validate/storage_scpi_validate.py` 通过。
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，最终生成 build id `20260813163405`，package CRC `0x22C703CC`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813163405 --out-dir build-rtos-multicore-smoke/ota_boot_commit_storage_rename_retry` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813163405`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/storage_scpi_validate/storage_scpi_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_storage_file_mgmt_tmp_path` 通过，覆盖目录 create/catalog/rename/delete 和文件 write/info/read/rename/delete。
  - 初次写 `/refmem/app_model.rmtp` 失败定位为 FatFs 原子 rename 阶段错误；修复后 `python tools/refmem_pack_write/refmem_pack_write.py COM6 --package build-rtos-multicore-smoke/sdcard_refmem_parser/refmem/app_model.rmtp --timeout 8 --out-dir build-rtos-multicore-smoke/validation_refmem_pack_write_storage_file_rename_retry` 通过，`FILE:INFO?` 返回 704 字节，`FILE:READ?` 读回 `RMTP` header，`SYSTem:REFMEM:LOAD:SD` 返回 `STAGED`。
- 还需完成：
  - 将 StorageAO 写事务从当前 4096 字节 RAM buffer 升级为分片落盘或后端流式事务，用于更大的 System Pack/RefMem table image。
  - 继续实现 staging/active/rollbackable table image 切换与 owner validation callback。
- 关联文件：
  - `components/storage_manager/inc/storage_manager.h`
  - `components/storage_manager/src/storage_manager.c`
  - `middleware/fatfs_port/inc/fatfs_port.h`
  - `middleware/fatfs_port/src/fatfs_port.c`
  - `middleware/scpi_port/inc/scpi_storage_commands.h`
  - `middleware/scpi_port/src/scpi_storage_commands.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem_pack_write/refmem_pack_write.py`
  - `tools/storage_scpi_validate/storage_scpi_validate.py`
- 下一步：
  - 完成板端 CRUD 和 RefMem load 正向闭环后，继续实现 staging/active/rollbackable table image 切换与 owner validation callback。

### REFMEM-TASK-20260813-022 - StorageAO RefMem package 对象事务

- 状态：已被 20260814-023 纠偏为通用 Storage 文件管理基础件
- 日期：2026-08-13
- 任务目标：
  - 允许通过 SCPI 把 `app_model.rmtp` 写入 SD，同时保持 HAOFV 边界：SCPI 不直接写 FatFs，RefMem 向量表不承载文件数据。
  - 把写入能力做成 StorageAO object transaction 基础件，后续可扩展到其他存储对象和后端。
- 完成内容：
  - StorageManager 增加 object contract 和写事务 API：`begin_object_write`、`write_object_chunk`、`commit_object_write`、`abort_object_write`、`get_write_snapshot`。
  - 首个对象为 `REFMEM_PACKAGE`，固定映射 `/refmem/app_model.rmtp`，支持 create/update/read/delete/info；该专用入口已在 20260814-023 中迁移为通用 `SYSTem:STORage:FILE:*` 路径接口。
  - FatFs port 增加 `fatfs_port_delete()`；原子写入支持替换已有目标文件。
  - 原 `SYSTem:REFMEM:PACKage:*` 接入 StorageAO 对象事务：`BEGIN/DATA/END/ABORt/STATus?/INFO?/READ?/DELete`；该命令树已删除，不再作为正式接口。
  - 新增 `tools/refmem_pack_write/refmem_pack_write.py`，固化分块上传、读回和 `LOAD:SD` 验证流程。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 已通过，生成 build id `20260813154434`，package CRC `0x8E5DC49A`。
- 还需完成：
  - 运行 Python 脚本静态检查、文档检查。
  - OTA 烧录到 COM6 后，用 `refmem_pack_write.py` 完成板端上传、读回和 `LOAD:SD` 正向闭环。
- 关联文件：
  - `components/storage_manager/inc/storage_manager.h`
  - `components/storage_manager/src/storage_manager.c`
  - `middleware/fatfs_port/inc/fatfs_port.h`
  - `middleware/fatfs_port/src/fatfs_port.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem_pack_write/refmem_pack_write.py`
- 下一步：
  - 完成板端闭环后，将该对象事务抽象继续推广到 profile/calibration 等受控存储对象。

### REFMEM-TASK-20260813-021 - LOAD:SD 接入 RMTP parser 首版

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 让 `SYSTem:REFMEM:LOAD:SD` 不再只依赖 manifest 摘要，而是读取并校验 RefMem table image。
  - 首版只校验 `.rmtp` 格式并写 staging snapshot，不替换 active image。
- 完成内容：
  - `refmem_table_registry.h/.c` 增加 `.rmtp` package validation API，校验 magic、format version、header size、total size、table count、payload CRC、package CRC 和每表 CRC。
  - `refmem_application_model_stage_sd_system_pack()` 增加 package CRC、package valid 和 package error 入参。
  - `SYSTem:REFMEM:LOAD:SD [path]` 默认读取 `/refmem/app_model.rmtp`，可用可选 path 覆盖；读取仍通过 StorageAO `FILE_READ` job，不直接调用 FatFs。
  - `tools/refmem_scpi_load_validate.py` 允许旧 SD 卡上缺少 `/refmem/app_model.rmtp` 时返回 `REJECTED`，并在 STAGED 时检查 package CRC 和路径。
  - `SCPI_COMMANDS.md`、`REFMEM_DOMAIN_ARCHITECTURE.md` 和 `REFMEM_DOMAIN_TODO.md` 同步 LOAD:SD parser 状态。
- 验证结果：
  - `python -m py_compile tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 通过。
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813152145`，package CRC `0xD5C9018D`。
  - `python tools/sd_fs_build/sd_fs_build.py --build-dir build-rtos-multicore-smoke --output-dir build-rtos-multicore-smoke/sdcard_refmem_parser --clean --no-zip --no-reports` 通过，生成新版 SD staging。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813152145 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_rmtp_parser` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813152145`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_rmtp_parser` 通过；当前板上 SD 卡仍是旧 System Pack，`LOAD:SD` 返回 `REJECTED`、last_error `7`、path `/refmem/app_model.rmtp`，符合缺少/无效 table image 的预期拒绝路径。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_rmtp_parser_multicore` 通过，16/16 passed。
- 还需完成：
  - 将新版 SD staging 写入实际 SD 卡后，复测 `LOAD:SD` 的 STAGED 正向路径。
  - parser 通过后仍需实现真实 table image staging buffer、owner validation callback 和 activation/rollback。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_table_registry.h`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 优先实现板端 parser 通过后的 staging table image 存储和 owner validation callback，而不是直接 active 替换。

### REFMEM-TASK-20260813-020 - SD System Pack 集成 RefMem package

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 让 `tools/sd_fs_build/sd_fs_build.py` 默认生成 RefMem table image 文件。
  - 在根 `/manifest.idx` 中引用 RefMem table image，为后续 `LOAD:SD` parser 提供稳定输入。
- 完成内容：
  - `sd_fs_build.py` 固化 `/refmem` 目录，生成 `/refmem/app_model.rmtp`、`/refmem/app_model.idx` 和 `/refmem/app_model.json`。
  - 根 `manifest.idx` 增加 `default.refmem=/refmem/app_model.rmtp`。
  - 根 `manifest.idx` 的 required 列表增加 `type=refmem_table_image`。
  - `SD_TODO.md` 同步 System Pack 示例、文件格式表和固定目录说明。
  - `REFMEM_DOMAIN_TODO.md` 标记 SD 工具集成完成。
- 验证结果：
  - `python -m py_compile tools/sd_fs_build/sd_fs_build.py tools/refmem_pack_build/refmem_pack_build.py` 通过。
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `python tools/sd_fs_build/sd_fs_build.py --build-dir build-rtos-multicore-smoke --output-dir build-rtos-multicore-smoke/sdcard_refmem_pack --clean --no-zip --no-reports` 通过。
  - 生成的根 `manifest.idx` 包含 `default.refmem=/refmem/app_model.rmtp` 和 `required=/refmem/app_model.rmtp,type=refmem_table_image,size=704,crc32=9474FC98`。
  - 生成的 `/refmem/app_model.idx` 包含 8 张表的 offset/size/crc32。
- 还需完成：
  - 板端 `LOAD:SD` 仍未解析 `.rmtp`，当前只利用 StorageAO manifest scan 结果写 staging snapshot。
- 关联文件：
  - `tools/sd_fs_build/sd_fs_build.py`
  - `docs/storage/SD_TODO.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_TASK_PROGRESS.md`
- 下一步：
  - 提交推送本轮 SD System Pack 集成；随后进入板端 `.rmtp` parser 设计和实现。

### REFMEM-TASK-20260813-019 - RefMem table image 格式固化

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 定义 RefMem 自己的 table image 文件格式，避免把根 `/manifest.idx` 和二进制表镜像混在一起。
  - 固化最小 PC 侧生成脚本，为后续 `LOAD:SD` 真实 parser 做输入准备。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `RefMem Table Image 格式`，定义 `/refmem/app_model.rmtp`、`app_model.idx`、`app_model.json` 三个文件的职责。
  - 定义 `.rmtp` header、table directory、payload CRC、package CRC 和 parser/owner validation 约束。
  - 新增 `tools/refmem_pack_build/refmem_pack_build.py`，生成最小 8 表 placeholder package、索引和 JSON 说明。
  - `tools/README.md` 增加新工具说明。
  - `REFMEM_DOMAIN_TODO.md` 标记格式定义完成；真实 `LOAD:SD` parser 仍保留待办。
- 验证结果：
  - `python -m py_compile tools/refmem_pack_build/refmem_pack_build.py` 通过。
  - `python tools/refmem_pack_build/refmem_pack_build.py --output-dir build-rtos-multicore-smoke/refmem_pack_format` 通过，生成 `refmem/app_model.rmtp`、`refmem/app_model.idx` 和 `refmem/app_model.json`。
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
- 还需完成：
  - 将 `sd_fs_build.py` 集成 RefMem package。
  - 板端实现 `.rmtp` parser 并接入 `SYSTem:REFMEM:LOAD:SD`。
- 关联文件：
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_TASK_PROGRESS.md`
  - `tools/refmem_pack_build/refmem_pack_build.py`
  - `tools/README.md`
- 下一步：
  - 提交推送本轮格式固化；随后评估是否把 `sd_fs_build.py` 集成 RefMem package。

### REFMEM-TASK-20260813-018 - TableRegistry 生命周期字段与 owner validation contract

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 在 TableRegistry 首版查询基础上补齐 table image 生命周期的可观测字段。
  - 增加 owner validation contract 首版入口，但不提前实现 active 替换。
- 完成内容：
  - `refmem_table_registry_entry_t` 增加 `image_offset` 和 `image_size`，为后续 System Pack/TLV table image 导入提供稳定位置语义。
  - 增加 registry flags：`ACTIVE_PRESENT`、`STAGING_PRESENT`、`CRC_OK`、`OWNER_OK`。
  - `refmem_table_registry_refresh_snapshot()` 改为 active/staging mask 分离，staging 状态不再覆盖 active 表存在性。
  - 新增 `refmem_table_registry_validate_staging()`，首版基于 staging snapshot 的 package CRC、lint error 和 last error 设置 owner-ok 可观测状态。
  - `SYSTem:REFMEM:TABle?` 返回字段增加 `image_offset,image_size`。
  - `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 同步 18 字段返回格式。
- 验证结果：
  - `python -m py_compile tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 通过。
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813150257`，package CRC `0x5248DF3C`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813150257 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_table_lifecycle` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813150257`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_table_lifecycle` 通过，覆盖 `SYSTem:REFMEM:TABle?` 18 字段、active/staging mask 分离和 staging owner-ok flags。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_table_lifecycle_multicore` 通过，16/16 passed。
- 还需完成：
  - 接入真实 TLV/System Pack table image。
  - 实现 owner validation callback table 和 active/rollbackable 切换。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_table_registry.h`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 继续 P0：定义 TLV/System Pack table image 格式，随后让 `LOAD:SD` 进入真实 parser。

### REFMEM-TASK-20260813-017 - RefMem TableRegistry 首版

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 按新 P0 待办落地 `RefMemTableRegistry` 首版。
  - 先建立可编译、可查询的 registry 基础件，用于观察 active/staging CRC、validation state 和 evidence。
- 完成内容：
  - 新增 `refmem_table_registry.h/.c`，覆盖 8 张静态应用模型表的 registry entry。
  - registry 首版从 `refmem_application_model_snapshot_t` 刷新 active CRC，从 `refmem_application_model_load_snapshot_t` 刷新 staging CRC 和 validation state。
  - 增加 `SYSTem:REFMEM:TABle? [table_id]` 查询，返回 registry snapshot 加指定表 entry。
  - 根 `CMakeLists.txt` 纳入 `refmem_table_registry.c`。
  - `SCPI_COMMANDS.md` 和 `REFMEM_DOMAIN_TODO.md` 同步新命令与完成状态。
- 验证结果：
  - `python -m py_compile tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 通过。
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813145717`，package CRC `0xFF8303A3`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813145717 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_table_registry_fix` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813145717`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_table_registry_fix` 通过，覆盖 `SYSTem:REFMEM:TABle?` 初始 active mask、NodeLoad staging 后 active/staging mask 共存、非法 NodeLoad rejected 和 SD staging。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_table_registry_multicore` 通过，16/16 passed。
- 还需完成：
  - `RefMemTableRegistry` 后续接入真实 active/staging table image、owner validation callback 和 rollbackable 状态。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_table_registry.h`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 继续 P0：定义真实 active/staging table image 生命周期和 owner validation callback。

### REFMEM-TASK-20260813-016 - RefMem 文档主线重排

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 在 Distributed RefMem 发生较大架构变动后，重新审查 `docs/refmem` 内容。
  - 把待办从早期 P0-P8 历史阶段重排为当前可执行优先级。
  - 修正 README 和架构文档中过时的实现状态。
- 完成内容：
  - `README.md` 从 Draft 更新为 Active，并补充 RefMemAO、A0-A7 通用逻辑插槽、NodeLoad、SlotClaim、表镜像加载等当前主线。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加“当前 Canonical Model”，明确 RefMemAO owner、A0-A7 通用插槽、GenericNode/NodeLoad 分层、SlotClaimMap、RefMemSlotContract 和 load staging 边界。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 的“当前实现现状”更新为当前代码真实状态，列出已实现组件和未完成模块。
  - `REFMEM_DOMAIN_TODO.md` 整体重构为当前执行队列：P0 表镜像与加载闭环、P1 SlotClaimMap 与自组网协调、P2 SlotContract 与 AO/FB owner API、P3 Command/ACK/NACK、P4 Sync/RMA、P5 组件化、P6 接口接入、P7 验证。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
- 还需完成：
  - 按新 P0 从 `RefMemTableRegistry` 和 table image 生命周期开始实现。
- 关联文件：
  - `docs/refmem/README.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_TASK_PROGRESS.md`
- 下一步：
  - 提交推送本轮文档重排；随后进入 P0-TableRegistry 首个实现闭环。

### REFMEM-TASK-20260813-015 - RefMem SCPI staging load 首版

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 在 RefMem 自身状态机 `mode=IDLE` 时支持 SCPI 发起 SD/System Pack 加载。
  - 支持通过 SCPI 直接提交节点装载配置候选到 RefMem staging，用于节点实例化和后续自组网协调验证。
  - 加载只进入 staging snapshot，不直接覆盖 active NodeLoadTable 或 live NodeSlot fact。
- 完成内容：
  - `refmem_application_model.h/.c` 增加 RefMem load 状态机枚举：`IDLE / LOAD_TO_STAGING / VALIDATING / ACTIVATING / FAULT`。
  - 增加 staging 状态枚举：`EMPTY / STAGED / VALIDATED / FAILED`，并建立 `refmem_application_model_load_snapshot_t`。
  - 增加 `refmem_application_model_stage_sd_system_pack()`：接收 Storage manifest 结果，当前用已编译静态应用模型 package CRC 写入 staging snapshot，占位等待真实 TLV/System Pack parser。
  - 增加 `refmem_application_model_stage_scpi_node_config()`：通过 SCPI inline 参数提交一条 NodeLoad 候选，校验 A0-A7 node 范围、instance 范围和基础 enable/required 位。
  - `SYSTem:REFMEM:LOAD:SD [path]` 接入 StorageAO `MANIFEST_SCAN` job，只在 RefMem load mode 为 `IDLE` 且底层实时触发状态 `TRIG_STATE_IDLE` 时允许。
  - `SYSTem:REFMEM:LOAD:NODE <node_id>,<instance_id>,<role_mask>,<persona_mask>[,<enabled>,<required>,<load_order>]` 接入 SCPI 节点候选 staging。
  - `SYSTem:REFMEM:LOAD:STATus?` 固定返回 load snapshot，覆盖 load_seq、source、mode、staging_state、manifest、active/staging CRC、lint/error 和候选节点字段。
  - 新增 `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py`，固化 CDC/USBTMC RefMem load 命令验证。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 和 `SCPI_COMMANDS.md` 同步 RefMem load 状态机与 SCPI 命令说明。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813143521`，package CRC `0xE86659C5`。
  - `python tools/product_scpi_validate/product_scpi_validate.py --dry-run --skip-mode` 通过，生成 119 条产品 SCPI 固定响应用例；该脚本不覆盖 `scpi_system_snapshot_commands.h`，RefMem load 使用新增专用脚本验证。
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `python -m py_compile tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py tools/product_scpi_validate/product_scpi_validate.py` 通过。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，OTA 进入 `READY_TO_REBOOT`。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813143521 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_scpi_load` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813143521`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_scpi_load` 通过：`LOAD:STATus?` 初始 `mode=0`；合法 `LOAD:NODE 5,9,32,32,1,0,0` 返回 `STAGED`；非法 `LOAD:NODE 8,9,32,32,1,0,0` 返回 `REJECTED` 且 `last_error=4`；`LOAD:SD` 返回 `STAGED`，manifest build id `20260812074528`。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_scpi_load_multicore` 通过，16/16 passed。
- 还需完成：
  - 将 SD manifest 占位导入升级为真实 TLV/System Pack parser。
  - 将 SCPI inline 单条 NodeLoad 候选升级为 staging NodeLoadTable image，支持多条候选、CRC、owner validation 和 activation。
  - 增加类似 OTA 的 `BEGIN/DATA/END/ABORT` 分块传输完整 RefMem application/node package 到 staging。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/interface/SCPI_COMMANDS.md`
- 下一步：
  - OTA 烧录本轮固件，使用 CDC/USBTMC 查询 `SYSTem:REFMEM:LOAD:STATus?`、`LOAD:NODE` 和 `LOAD:SD`，确认 staging 快照闭环。

### REFMEM-TASK-20260813-014 - 全局逻辑插槽 claim 与自组网协调

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 明确 A0-A7 通用插槽在 active profile / epoch 内是全环唯一逻辑地址。
  - 支持一块物理板同时承载多个不同逻辑插槽，同时禁止多个物理板提交同一个 active slot owner。
  - 增加自组网协调机制：重复 claim 优先尝试迁移到空闲通用插槽，只有插槽满、实例化溢出或硬绑定 required slot 不匹配时才失败。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加全局逻辑插槽 `SlotClaim`、`SlotClaimMap` 和自组网协调状态机。
  - 协调状态覆盖 `DISCOVER -> CLAIM_PROPOSE -> CLAIM_COLLECT -> CONFLICT_DETECTED -> RESOLVE_PLAN -> RESOLVE_COMMIT -> CLAIM_ACTIVE`，失败进入 `CLAIM_STALE / CLAIM_FAULT`。
  - 明确协调消息：`CLAIM_HELLO`、`CLAIM_PROPOSE`、`CLAIM_CONFLICT`、`CLAIM_RELEASE`、`CLAIM_RESOLVE`、`CLAIM_COMMIT`。
  - 明确候选节点实例与 active slot assignment 分层：一块物理板最多可上报 16 个候选节点实例用于自组网协调和反向验证，但 active assignment 只能映射到 A0-A7，第 9 到第 16 个未分配候选必须进入 `OVERFLOW` evidence。
  - `refmem_application_model.h` 增加 `REFMEM_APP_MODEL_CLAIM_CANDIDATE_MAX = 16`，作为后续 `SlotClaimProposal` 运行态上限。
  - `HAOFV_ARCHITECTURE.md` 和 `RTOS_HAOFV_ARCHITECTURE.md` 同步全环 slot claim 唯一性与 DeploymentGate 拒绝规则。
  - `refmem_application_model.h/.c` 增加 `REFMEM_APP_CLAIM_*` 策略、`claim_policy` 和 `claim_priority` 字段。
  - 静态模型 linter 增加 slot claim 策略检查：required slot 不允许 disabled/dynamic claim，dynamic spare 必须 report-only 且非 required。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813141942`，package CRC `0x0978A193`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，OTA 进入 `READY_TO_REBOOT`。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813141942 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_slot_claim16` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813141942`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_slot_claim16` 通过，16/16 passed。
  - 板端维护查询通过：`SYSTem:REFMEM:STATus? => 65536,1,107645,0,8,107644,107644,7`，其中 flags `7` 表示 directory valid、directory CRC valid 和 application model valid 均置位；`SYSTem:REFMEM:NODE? => 0,1,107650,107651,112568,0,0,0,0`；`SYSTem:CORE:VECTOR? => 1,107656,2,0,1,15,3840,2,7,2,24714159,0,7`；`SYSTem:PROTection:STATus? => 1,107661,1,1,1,0,0,0,2,11,2,177242018,0,7`。
- 还需完成：
  - 在 `DistributedRefMemAO` 中实现运行态 `SlotClaimMap` 聚合、重复 claim 检测、迁移计划和 claim epoch commit。
  - 增加单板 16 候选节点反向验证，确认不会生成第 9 个隐式插槽、不会覆盖已有 active slot，超过 16 个 proposal 会被拒绝。
  - 将 `SlotClaimMap` 暴露到 DeploymentGate evidence 和后续维护查询。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/arch/HAOFV_ARCHITECTURE.md`
  - `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
- 下一步：
  - 进入 `DistributedRefMemAO` 运行态 `SlotClaimMap` 聚合、协调消息和 16 候选反向验证实现。

### REFMEM-TASK-20260813-013 - GenericNode capability 与应用 role 分离

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 继续纠偏 RefMem 静态模型，避免 GenericNodeTable 从当前节点应用范围反推出通用节点属性。
  - 将通用节点能力定义为硬件/基础能力上限，应用 role/persona/instance 只通过 NodeLoadTable 装载。
  - 固化“RefMem A0-A7 是 8 个通用插槽，node_id 是同步协议中的 slot id”这一模型。
  - 综合用户提出的 RM Slot 能力模型，评估如何在不破坏 HAOFV 与既有静态模型表的前提下形成通用 RefMem 基础件。
- 完成内容：
  - `refmem_application_map_t` 删除 `node_count` 和 `node[]`，只保留 application/profile/layout/target mask 元数据。
  - 新增 `refmem_generic_node_table_t`，独立维护 A0-A7 通用插槽基座。
  - `refmem_application_model_snapshot_t` 增加 `generic_node_crc32`，package CRC 和 table mask 纳入 GenericNodeTable。
  - 增加独立 `REFMEM_APP_CAP_*` 能力位，GenericNodeTable 不再复用 `REFMEM_APP_ROLE_*`。
  - 增加 capability gate：enabled load 的实例 resource/IO claim 必须被目标 GenericNode 的 `capability_mask` 覆盖。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 明确 GenericNode capability 不能从当前装载实例反推，必须来自 board profile、硬件约束或 System Pack 的硬件 profile。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 和 `RTOS_HAOFV_ARCHITECTURE.md` 将 A0-A7 明确描述为通用插槽，实例化节点/逻辑功能通过 NodeLoadTable 装入插槽。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加通用 RefMemAO 基础件模型：现有静态模型表、Header/Directory、SlotGuard、DeploymentGate 和 QualityTable 共同生成 `DistributedRefMemAO` 内部 `RefMemSlotContract` 契约视图。
  - 明确 `RefMemSlotContract` 不是绕过 AO/FB 或 RefMemAO 的第二套业务 API，而是 `DistributedRefMemAO` 接收、校验、发布和订阅分发反射内存事实时使用的内部契约。
  - `HAOFV_ARCHITECTURE.md` 与 `RTOS_HAOFV_ARCHITECTURE.md` 同步表读写规范：业务行为入口仍归 AO/FB owner、ConfigGate、CommandSlot owner 和 RefMem Sync owner；裸 RefMem 字段不得被业务代码直接写入。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813140220`，package CRC `0xC6404998`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，OTA 进入 `READY_TO_REBOOT`。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813140220 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_slot_contract` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813140220`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_slot_contract` 通过，16/16 passed。
  - 板端维护查询通过：`SYSTem:REFMEM:STATus? => 65536,1,89430,0,8,89429,89429,7`，其中 flags `7` 表示 directory valid、directory CRC valid 和 application model valid 均置位；`SYSTem:REFMEM:NODE? => 0,1,89434,89435,94354,0,0,0,0`；`SYSTem:CORE:VECTOR? => 1,89440,2,0,1,15,3840,2,7,2,4138206416,0,7`；`SYSTem:PROTection:STATus? => 1,89445,1,1,1,0,0,0,2,11,2,2207849873,0,7`。
- 还需完成：
  - 把 GenericNode capability 暴露到后续 table registry / System Pack / DeploymentGate evidence。
  - 将 resource/IO 到 capability 的映射从当前代码 helper 升级为表驱动资源能力矩阵。
  - 后续实现 `DistributedRefMemAO` 内部 `RefMemSlotContract` 派生与 linter，不建立对外业务读写 API。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_TASK_PROGRESS.md`
- 下一步：
  - 进入 `DistributedRefMemAO` 内部 `RefMemSlotContract` 派生规则和 linter 实现，或继续 table registry / System Pack / DeploymentGate evidence。

### REFMEM-TASK-20260813-012 - 通用节点与实例加载架构纠偏

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将 RefMem 静态模型从“节点直接绑定产品实例”纠偏为“通用节点基座 + 应用实例加载表”。
  - 支持同一块板卡同时加载多个逻辑实例，例如调试阶段一块板同时模拟转台和网分。
  - 避免 `ApplicationMap.node[]` 通过 `instance_first/count` 绑定连续实例范围，阻碍后续多节点、多 profile 和多实例加载。
- 完成内容：
  - `REFMEM_DOMAIN_TODO.md` 增加架构纠偏待办：A0-A7 通用节点基座必须与应用实例装载拆开。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 将 `DistributedApplicationMap` 改为应用/profile 元数据和 CRC bundle，不再作为节点目录。
  - 新增 `DistributedGenericNodeTable` 和 `DistributedNodeLoadTable` 架构说明。
  - `HAOFV_ARCHITECTURE.md` 和 `RTOS_HAOFV_ARCHITECTURE.md` 同步 GenericNode / NodeLoad / FbInstance 三层边界。
  - `refmem_application_model.h/.c` 增加 `refmem_node_load_table_t`，把实例实际装载关系从节点表移出。
  - `refmem_app_node_entry_t` 改为通用节点基座字段：`capability_mask`、`default_persona_mask`、`hw_profile_crc32`、`online_required`、`fail_policy`。
  - `refmem_fb_instance_entry_t.node_id` 改为 `default_node_id`，实际 active 节点以 NodeLoadTable 为准。
  - linter 改为校验 NodeLoadTable 的 `node_id -> instance_id` 装载关系，资源/IO 冲突按加载到同一节点的 enabled 实例组合检查。
  - package CRC 和 `table_mask` 纳入 NodeLoadTable CRC。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813133647`，package CRC `0xD148A39A`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，OTA 进入 `READY_TO_REBOOT`。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813133647 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_node_load` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813133647`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_node_load` 通过，16/16 passed。
  - 板端维护查询通过：`SYSTem:REFMEM:STATus? => 65536,1,128516,0,8,128515,128515,7`，其中 flags `7` 表示 directory valid、directory CRC valid 和 application model valid 均置位；`SYSTem:REFMEM:NODE? => 0,1,128521,128522,133442,0,0,0,0`；`SYSTem:CORE:VECTOR? => 1,128527,2,0,1,15,3840,2,7,2,405916318,0,7`；`SYSTem:PROTection:STATus? => 1,128532,1,1,1,0,0,0,2,11,2,2731132301,0,7`。
- 还需完成：
  - 定义 binary/TLV 存储格式、版本兼容和 System Pack 导入策略。
  - 把 package CRC、lint 结果和 table mask 暴露到维护查询或 DeploymentGate evidence。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 继续 P2 的 binary/TLV 存储格式和 System Pack 导入策略。

### REFMEM-TASK-20260813-011 - 静态模型 linter 首版

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将 P2 静态应用模型从基础字段合法性检查升级为关系一致性 linter。
  - 为后续 RUN gate、owner 写权限和 System Pack 导入提供统一的模型验证结果。
- 完成内容：
  - `refmem_application_model_snapshot_t` 增加 `lint_error_count` 和 `first_lint_error`。
  - 增加 `refmem_app_lint_error_t`，首版覆盖表版本、节点范围、实例范围、实例引用、资源冲突、IO 冲突、重复 writer、事件链路、数据链路、gate/quality 错误。
  - linter 检查节点声明的 instance range 是否确实归属该节点。
  - linter 检查同节点启用实例的独占资源冲突；首版将 Flash、SD、USB、LCD 作为硬独占资源，RJ45/PIO/DMA/core1 保留给后续按实例细化。
  - linter 检查同节点启用实例的独占 IO 冲突；首版将 link-control、BiSS-C、UART/RS485 作为硬独占 IO，SMA/RJ45_SYNC 暂按分布式链路能力处理。
  - linter 检查 `slot_path` 的 writer 唯一性，禁止同一字段被不同实例声明为 writer。
  - linter 检查 START、STOP、FIRE_LOAD、DONE、FAULT 必需事件链路存在。
  - linter 检查关键 slot 的 data link 覆盖：System、Role、VDC、Loop、DPLL、Node、Trigger、IO、Calibration、AckCommand、Gateway。
  - `refmem_application_model_validate()` 改为使用 linter 汇总结果；RefMem status 的 `APP_MODEL_VALID` 位继续由 snapshot valid 派生。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813130200`，package CRC `0xA3A4BDE6`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，OTA 进入 `READY_TO_REBOOT`。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813130200 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_app_linter` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813130200`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_app_linter` 通过，16/16 passed。
  - 板端维护查询通过：`SYSTem:REFMEM:STATus? => 65536,1,96185,0,8,96184,96184,7`，其中 flags `7` 表示 directory valid、directory CRC valid 和 application model valid 均置位；`SYSTem:REFMEM:NODE? => 0,1,96189,96190,101105,0,0,0,0`；`SYSTem:CORE:VECTOR? => 1,96196,2,0,1,15,3840,2,7,2,2463022334,0,7`；`SYSTem:PROTection:STATus? => 1,96202,1,1,1,0,0,0,2,11,2,737992584,0,7`。
- 还需完成：
  - 把 `lint_error_count/first_lint_error` 暴露到后续维护查询或 RUN gate evidence。
  - 将 PIO/DMA/core1/RJ45 的共享/独占策略从硬编码升级为 resource class 表。
  - 增加 System Pack 导入前的离线 linter 和故障注入测试。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 继续 P2 的 binary/TLV 存储格式和 System Pack 导入策略，或将 linter 结果接入 DeploymentGate / RUN gate。

### REFMEM-TASK-20260813-010 - P2 六张静态应用模型表落代码

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将 P2 文档定义的六张静态分布式应用模型表落到 `refmem_application_model.h/.c`。
  - 首版建立 static const 表、CRC bundle、getter 和 validate，暂不做动态加载、不新增 SCPI 命令面。
- 完成内容：
  - 新增 `refmem_application_model.h/.c`，包含 `DistributedApplicationMap`、`DistributedFbInstanceTable`、`DistributedEventLinkTable`、`DistributedDataLinkTable`、`DistributedDeploymentGate` 和 `DistributedConnectionQualityTable` 的首版结构与静态实例。
  - `DistributedApplicationMap` 覆盖 A0-A7 八个通用节点；A0-A3 为产品链路节点，A4 为调试期 model_vna/model_turntable/test_agent 组合节点，A5-A7 保留为 spare board。
  - 表内实例覆盖 SystemAO、RefMemSyncFB、LoopEngineAO、TriggerAO、LinkSwitcherAO、GatewayAO、CalibrationAO、ModelVnaAO 和 ModelTurntableAO。
  - 增加每张表的 CRC，以及汇总 `package_crc32`；对含字符串指针的表按字段和字符串内容逐项计算 CRC，避免指针地址污染。
  - 增加 `refmem_application_model_validate()`，首版检查 node id、instance id、event/data link 引用、slot ref、target mask、gate check 和 quality scope 的基本一致性。
  - `distributed_refmem_init()` 接入 `refmem_application_model_init()`，并通过 `DISTRIBUTED_REFMEM_FLAG_APP_MODEL_VALID` 把模型有效状态并入 `SYSTem:REFMEM:STATus?` flags。
  - 根 `CMakeLists.txt` 纳入 `refmem_application_model.c`。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813125333`，package CRC `0x7E02FB60`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，OTA 进入 `READY_TO_REBOOT`。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813125333 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_app_model` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813125333`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_app_model` 通过，16/16 passed。
  - 板端维护查询通过：`SYSTem:REFMEM:STATus? => 65536,1,99552,0,8,99551,99551,7`，其中 flags `7` 表示 directory valid、directory CRC valid 和 application model valid 均置位；`SYSTem:REFMEM:NODE? => 0,1,99557,99558,104473,0,0,0,0`；`SYSTem:CORE:VECTOR? => 1,99562,2,0,1,15,3840,2,7,2,3508053401,0,7`；`SYSTem:PROTection:STATus? => 1,99567,1,1,1,0,0,0,2,11,2,1780001644,0,7`。
- 还需完成：
  - 定义静态模型表 binary/TLV 存储格式、CRC、版本兼容和 System Pack 导入策略。
  - 增加更完整的静态模型 linter，检查资源/IO claim 冲突、writer 唯一性和 event/data link 完整性。
  - 将 DeploymentGate 输出映射到 RUN gate、诊断 evidence 和更细的维护查询。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `CMakeLists.txt`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 在 P2 继续补静态模型 linter 与 System Pack/TLV 存储格式，或回到 P3 用模型表支撑 slot guard/owner 检查。

### REFMEM-TASK-20260813-009 - Directory CRC 与 slot map 校验

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 为 `DistributedVectorTable` 增加 directory CRC 和 slot directory 自检，防止 slot map 半更新或 layout 边界错误进入运行事实。
  - 保持本轮为 RefMem 内部实现，不新增顶级 SCPI 域，不改变既有 `SYSTem:REFMEM:*` 查询格式。
- 完成内容：
  - `refmem_vector_header_slot_t` 增加 `directory_crc32` 字段，header size 继续由 static assert 冻结为 1 KB。
  - `refmem_vector_table.c` 增加 `refmem_vector_directory_crc()` 和 `refmem_vector_table_validate_directory()`。
  - directory 校验覆盖 slot_count、offset 顺序、非零 size、64 KB 边界和表尾精确覆盖。
  - `distributed_refmem.c` 初始化 directory 后固化 CRC，并在 runtime publish 时刷新 directory valid / CRC valid flags。
  - `distributed_refmem.h` 增加 `DISTRIBUTED_REFMEM_FLAG_DIRECTORY_VALID` 和 `DISTRIBUTED_REFMEM_FLAG_DIRECTORY_CRC_VALID`，由 `SYSTem:REFMEM:STATus?` 的 flags 字段暴露维护状态。
  - `refmem_vector_header_crc()` 改为排除 `header_crc32` 字段自身的分段 CRC，避免把该字段值或占位零错误纳入 header CRC。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813123640`，package CRC `0xC037DE57`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，OTA 进入 `READY_TO_REBOOT`。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813123640 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_directory_crc` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813123640`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_directory_crc` 通过，16/16 passed。
  - 板端维护查询通过：`SYSTem:REFMEM:STATus? => 65536,1,123715,0,8,123714,123714,3`，其中 flags `3` 表示 directory valid 与 directory CRC valid 均置位；`SYSTem:REFMEM:NODE? => 0,1,123720,123721,128635,0,0,0,0`；`SYSTem:CORE:VECTOR? => 1,123726,2,0,1,15,3840,2,3,2,2158064260,0,3`；`SYSTem:PROTection:STATus? => 1,123732,1,1,1,0,0,0,2,11,2,1346783678,0,3`。
- 还需完成：
  - 为全部 slot 增加统一 guard 或等价兼容结构。
  - 实现 slot owner 写权限检查和 seqlock/双缓冲。
- 关联文件：
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/inc/refmem_vector_table.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/distributed_refmem/src/refmem_vector_table.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 进入 P3 下一项，设计并落地 slot 统一 guard。

### REFMEM-TASK-20260813-008 - Vector Table layout 拆分

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将 `distributed_refmem.c` 内部的 64 KB `DistributedVectorTable` layout、slot enum、header/node slot、directory 初始化和 header CRC 拆出为独立 `refmem_vector_table.h/.c`。
  - 让 `distributed_refmem.c` 只保留 RefMem runtime 发布、节点状态和维护 snapshot 逻辑。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_vector_table.h`，集中定义 `REFMEM_VECTOR_MAGIC`、slot id、directory、header slot、node slot 和 64 KB table layout。
  - 新增 `components/distributed_refmem/src/refmem_vector_table.c`，集中实现 table clear、header/node accessor、slot directory 初始化和 header CRC。
  - 将 table/header/node 结构体 size static assert 移入 `refmem_vector_table.c`，继续冻结 1 KB header、512 B node slot 和 64 KB table。
  - 修改 `distributed_refmem.c`，通过 `refmem_vector_table_*` API 访问向量表，移除本文件内的 layout 私有定义。
  - 修改根 `CMakeLists.txt`，把 `refmem_vector_table.c` 纳入当前构建。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - 旧布局内部符号清理检查通过：`DISTRIBUTED_REFMEM_MAGIC`、`DISTRIBUTED_REFMEM_SLOT_COUNT`、`distributed_vector_table_t`、`distributed_refmem_*_slot_t`、`distributed_refmem_fast_crc32` 在 `components/distributed_refmem/` 中无残留。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813122753`，package CRC `0x614E2152`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，OTA 进入 `READY_TO_REBOOT`。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813122753 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_vector_split` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813122753`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_vector_split` 通过，16/16 passed。
  - 板端维护查询通过：`SYSTem:REFMEM:STATus? => 65536,1,116625,0,8,116624,116624,0`；`SYSTem:REFMEM:NODE? => 0,1,116629,116630,121542,0,0,0,0`；`SYSTem:CORE:VECTOR? => 1,116634,2,0,1,15,3840,2,0,2,619701535,0,0`；`SYSTem:PROTection:STATus? => 1,116639,1,1,1,0,0,0,2,11,2,3143599354,0,0`。
  - 默认 `build` 目录不适用于当前分支：该分支要求 `PROJECT_USE_FREERTOS=ON` 和 `PROJECT_USE_MULTICORE=ON`。
- 还需完成：
  - 为 DistributedVectorTable 实现 directory CRC、slot directory 校验、统一 guard、owner 写权限和 seqlock/双缓冲。
  - 继续将 `distributed_refmem` 拆成 RefMem Domain 子模块。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_vector_table.h`
  - `components/distributed_refmem/src/refmem_vector_table.c`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `CMakeLists.txt`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 进入 P3 下一项，实现 directory CRC 和 slot directory 校验。

### REFMEM-TASK-20260813-007 - 虚拟反射内存参考框架补足

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 基于 NASA cFS Table Services、OpenSHMEM / MPI RMA、MPI RMA 和 IEC 61499 的一手机制，补足 RefMem 主域框架。
  - 将参考项目落到可实现的表生命周期、受控 RMA window、completion、fence/quiet 和静态应用模型检查。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加虚拟反射内存参考机制矩阵。
  - 增加 `RefMemTableRegistry` 框架，覆盖 table id、owner、offset/size、layout version、active/staging CRC、validation state、validator id、last result 和 evidence。
  - 增加 staging/active 表生命周期：`LOAD_TO_STAGING -> CRC_CHECK -> OWNER_VALIDATE -> ACTIVATE -> ACTIVE -> ROLLBACKABLE/FAILED`。
  - 增加 `RefMemRmaWindow` 受控子集，限制为 slot delta、command flag、dirty bitmap、heartbeat/seq、quality counter 等白名单字段。
  - 增加 RMA completion 语义：`origin_encoded -> ring_sent -> target_received -> target_crc_ok -> target_owner_validated -> target_committed -> visible_in_snapshot`。
  - `REFMEM_DOMAIN_TODO.md` 补充 TableRegistry、staging/active/rollback、owner validation、RMA window 和 completion 实现项。
- 验证结果：
  - 本任务为文档框架补足，尚未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 将 `RefMemTableRegistry` 和 `RefMemRmaWindow` 落到代码组件。
  - 补充 staging/active load/dump 的 System Pack 存储格式。
- 关联文件：
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 进入 RefMem P3/P5 代码前，冻结 registry table id 和 RMA atomic 白名单。

### REFMEM-TASK-20260813-006 - 外部参考机制收敛到 RefMem 待办

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将 NASA cFS Table Services、OpenSHMEM / MPI RMA、IEC 61499 的可借鉴机制收敛到 RefMem Domain 待办。
  - 明确 RefMem 只吸收表驱动、CRC、owner validation、RMA completion、atomic/fence 和静态 FB 图，不引入完整外部协议栈或动态分布式运行时。
- 完成内容：
  - `REFMEM_DOMAIN_TODO.md` 增加“参考项目收敛原则”矩阵，定义每个外部参考项目的借鉴机制和本项目落地边界。
  - 新增 P1.5 外部参考机制工程化收敛章节，列出 cFS、RMA、IEC 61499 到文档和实现的映射任务。
  - P2 增加静态模型 linter、package CRC 和 FB 图版本门禁待办。
  - P3 增加 RefMem Table Registry、active/inactive image 生命周期、owner validation callback 和 dump/load 镜像规则待办。
  - P4 增加 command slot atomic API、completion 语义和 memory order / fence 规则待办。
  - P5 增加 RefMem RMA Window、delta completion、远端原子更新白名单、RMA-style fence 和 compact timestamp / delta frame 分层待办。
  - P8 增加 cFS-style 和 RMA-style 故障注入验证项；VDC/DPLL 类参考拆分到 VDC Domain 待办维护。
- 验证结果：
  - 本任务为文档待办推进，未修改代码，未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 在 `REFMEM_DOMAIN_ARCHITECTURE.md` 增加虚拟反射内存参考机制矩阵。
  - 在 VDC Domain 中补齐 offset/rate/quality 与 initial sync/drift compensation/holdover 的字段映射。
  - 后续按 P3/P4/P5/P8 把参考机制转成代码和验证闭环。
- 关联文件：
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_TASK_PROGRESS.md`
- 下一步：
  - 继续完善 `REFMEM_DOMAIN_ARCHITECTURE.md` 的外部参考机制章节，避免 TODO 和架构正文脱节。

### REFMEM-TASK-20260813-005 - Command / ACK / NACK 契约定义

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 进入 P4，定义 RefMem `AckCommandSlot` 的命令意图、ACK/NACK、busy、timeout、reason 和 evidence 契约。
  - 将现有 `SYSTem:CONFigure:ACK? / NACK?` 收敛为底层 command slot 的配置门禁视图。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 Command / ACK / NACK 契约，明确写命令返回 accepted 不代表动作完成。
  - 定义 `AckCommandSlot` 字段：`command_seq`、source、target、required mask、command type/class、payload ref/CRC、epoch/run_id、timeout、taken/ack/nack/busy/timeout 位图、reason、evidence 和 clear_seq。
  - 定义命令类型首版集合：`CONFIG_STAGE`、`CONFIG_ACTIVATE`、`START/STOP`、`ARM`、`FIRE_LOAD`、`CAL_START`、`SYNC_START_STOP`、`FAULT_CLEAR`、`RESOURCE_JOB` 等。
  - 定义命令状态机：`IDLE -> POSTED -> TAKEN -> EXECUTING/BUSY -> ACKED/NACKED/TIMED_OUT/FAULTED -> CLEAR_PENDING -> IDLE`。
  - 定义重复 `command_seq`、payload CRC mismatch、epoch mismatch、timeout、clear_seq 和 stale 策略。
  - 定义产品化 NACK reason 扩展列表。
  - `REFMEM_DOMAIN_TODO.md` 与 `RTOS_HAOFV_TODO.md` 将 P4 文档定义项标记完成，并拆出 `refmem_command.h/.c`、system_manager 映射和通用 `SYSTem:COMMand:*` 评估待办。
  - `SCPI_COMMAND_PLAN.md` 和 `SCPI_COMMANDS.md` 同步 ACK/NACK 单事实源规则。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，保留 7 个既有文件命名 warning。
  - 本任务为文档契约定义，未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 将 AckCommandSlot 字段落到 `refmem_command.h/.c`。
  - 将现有 `system_manager` 配置 ACK 迁移或映射到 RefMem AckCommandSlot snapshot。
  - 扩展 NACK reason 表并评估通用 `SYSTem:COMMand:ACK? / NACK?`。
- 关联文件：
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/arch/RTOS_HAOFV_TODO.md`
  - `docs/interface/SCPI_COMMAND_PLAN.md`
  - `docs/interface/SCPI_COMMANDS.md`
- 下一步：
  - 进入 P5，定义 `REFMEM_DELTA` / `REFMEM_EPOCH` 帧格式、slot delta CRC、seq、timestamp、RJ45_SYNC_RING stale 和重放策略。

### REFMEM-TASK-20260813-004 - DistributedVectorTable 契约冻结

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 进入 P3，冻结 64 KB `DistributedVectorTable` 的 slot offset、slot size、layout version、slot owner、snapshot 和时间字段契约。
  - 以当前 `components/distributed_refmem/` P0 实现为基线，明确文档冻结内容和后续代码实现项。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 将 64 KB 表格从“建议大小”升级为固定 offset/size，表尾固定 `0x10000`。
  - 增加 Header/Directory 契约，定义 `magic/end_magic`、`layout_version`、`table_seq`、`epoch_id`、`run_id`、slot directory、directory CRC 和兼容版本。
  - 增加 slot guard 契约，定义 `slot_seq`、owner、writer、crc、stale、flags、write_epoch、write_tick32。
  - 增加 owner 与写权限表，明确各 slot 的唯一 writer 和禁止事项。
  - 增加 snapshot 与并发契约，定义 `DIRECT_ATOMIC`、`SEQLOCK`、`DOUBLE_BUFFER`、`EVIDENCE_REF` 四类策略。
  - 增加 Version Bundle，统一 layout、application、config、calibration、sync、loop、action、permission、storage、build 和 hw profile 版本。
  - 增加时间字段与回绕规则，区分 `tick32`、`epoch_id + tick32` 和 `dc_time64_ns`。
  - `RTOS_HAOFV_ARCHITECTURE.md` 同步固定 offset/size 表格，并把详细契约指向 RefMem canonical。
  - `REFMEM_DOMAIN_TODO.md` 与 `RTOS_HAOFV_TODO.md` 将 P3 文档冻结项标记完成，并拆出代码实现项。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，保留 7 个既有文件命名 warning。
  - 本任务为文档契约冻结，未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 将 `distributed_refmem.h/.c` 拆出 `refmem_vector_table.h/.c`。
  - 实现 directory CRC、slot directory 校验、统一 guard、owner 写权限、seqlock/双缓冲和运行上下文字段。
- 关联文件：
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
  - `docs/arch/RTOS_HAOFV_TODO.md`
- 下一步：
  - 进入 P4，定义 Command / ACK / NACK 槽原子 Take/Clear、command_seq、target mask、busy/timeout/reason 和 SCPI ACK/NACK 对齐。

### REFMEM-TASK-20260813-003 - 静态分布式应用模型细化

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 按 A0-A7 通用节点模型细化 RefMem 的静态分布式应用模型。
  - 明确脉冲分发、链路切换、仪表控制、模型网分、模拟转台、网关和测试代理都是加载到通用节点上的 role/persona/instance。
  - 为后续 `refmem_application_model.h/.c` 和 RUN gate 实现提供字段契约。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `DistributedApplicationMap` 字段、规则和实例类型约束。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `DistributedFbInstanceTable`，覆盖 instance、domain、版本、资源/IO claim、预算、状态 slot 和冲突分类。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `DistributedEventLinkTable`，覆盖 START/STOP/FIRE_LOAD/DONE/FAULT/ACK/NACK 的静态事件路径。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `DistributedDataLinkTable`，定义 slot writer/reader、类型、单位、值域、生命周期、snapshot 和 stale 策略。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `DistributedDeploymentGate`，把 layout、node、instance、resource、IO、writer、event、data、config、cal/sync quality 纳入 RUN 门禁。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `DistributedConnectionQualityTable`，覆盖 seq、CRC、stale、late、drop、timeout、p99/p999 和 evidence。
  - `RTOS_HAOFV_ARCHITECTURE.md` 同步实例类型，避免 RTOS 文档仍只描述模型节点和网关。
  - `REFMEM_DOMAIN_TODO.md` 与 `RTOS_HAOFV_TODO.md` 将 P2 文档定义标记为完成，并拆出代码落地、TLV/CRC/System Pack 和 RUN gate 接入待办。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，保留 7 个既有文件命名 warning。
  - 本任务为文档模型细化，未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 将六张静态模型表落到 `refmem_application_model.h/.c`。
  - 定义静态模型表的 binary/TLV 存储格式、CRC、版本兼容和 System Pack 导入策略。
  - 将 DeploymentGate 输出映射到 `SYSTem:REFMEM:STATus?`、诊断 evidence 和 RUN gate。
- 关联文件：
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
  - `docs/arch/RTOS_HAOFV_TODO.md`
- 下一步：
  - 进入 P3，冻结 `DistributedVectorTable` 64 KB layout、slot directory、slot owner、snapshot 和回绕安全时间字段契约。

### REFMEM-TASK-20260813-002 - RefMem 内部主域 P0/P1 同步

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 继续进行 RefMem 主域升级，把 RefMem 从文档目录和架构 layer 明确同步为 HAOFV 内部基础主域。
  - 明确 A0-A7 是八个通用节点，模型网分、模拟转台、网关和测试代理只是加载到通用节点上的实例。
  - 明确不冲突时同一通用节点支持同时载入多个逻辑实例。
- 完成内容：
  - `HAOFV_ARCHITECTURE.md` 将 `Distributed Vector Blackboard / RefMem Sync` 表述为内部主域。
  - `RTOS_HAOFV_ARCHITECTURE.md` 将 `task_refmem_sync` 描述为当前任务壳承载 `DistributedRefMemAO / RefMemSyncFB`。
  - `SCPI_COMMAND_PLAN.md` 和 `SCPI_COMMANDS.md` 明确 `SYSTem:REFMEM:*` 是 RefMem 内部主域的系统维护入口，不建立裸顶级 `REFMEM` SCPI 域。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 补充节点模型硬规则：RefMem 底座只固定 A0-A7 八个通用节点。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 补充多实例共存规则：在资源、IO、时序、owner、slot writer、事件连接和数据连接不冲突时，同一通用节点允许同时载入多个逻辑实例。
  - `arch/README.md` 和 `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md` 将 RefMem 主域加入阅读顺序和内部架构域目录规划。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，保留 7 个既有文件命名 warning。
  - 本任务为文档主域同步，未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 将 `RTOS_HAOFV_TODO.md` 的 P1 从 RTOS 总待办进一步收敛到 RefMem Domain 子待办。
  - 建立 ApplicationMap / FbInstanceTable / EventLinkTable / DataLinkTable / DeploymentGate / ConnectionQualityTable 的详细设计。
  - 后续进入代码组件化前，先冻结 `DistributedVectorTable` slot 字段契约。
- 关联文件：
  - `docs/arch/HAOFV_ARCHITECTURE.md`
  - `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
  - `docs/arch/README.md`
  - `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
  - `docs/interface/SCPI_COMMAND_PLAN.md`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/arch/HAOFV_MAINTENANCE_TODO.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 按 `REFMEM_DOMAIN_TODO.md` 的 P2/P3，先定义静态分布式应用模型和 VectorTable slot 契约。

### REFMEM-TASK-20260813-001 - RefMem 三份标准文档建立

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 在 `docs/refmem/` 中建立 RefMem 内部主域的三份标准文件。
  - 将“反射内存向量表升格为 Distributed Vector Blackboard / RefMem Sync 内部主域”的影响面写入文档。
- 完成内容：
  - 新增 `REFMEM_DOMAIN_ARCHITECTURE.md`，定义 RefMem Domain 的定位、职责边界、HAOFV 层级、静态分布式模型、核心数据面、SCPI 边界、当前实现现状和目标代码形态。
  - 新增 `REFMEM_DOMAIN_TODO.md`，把需要修改的现有文件、建议新增的文档、建议新增的代码文件和实施阶段拆成 P0-P8 待办。
  - 新增 `REFMEM_TASK_PROGRESS.md`，作为 RefMem 主域独立任务进度入口。
- 验证结果：
  - 本任务为文档生成，尚未执行 docs check、构建、烧录或板端 SCPI。
- 还需完成：
  - 更新 `docs/refmem/README.md`，加入三份标准文档入口。
  - 更新 `docs/arch/README.md` 和 `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`，把 RefMem 明确为内部主域。
  - 将 HAOFV/RTOS/SCPI 文档中 RefMem 的定位同步为内部主域。
- 关联文件：
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_TASK_PROGRESS.md`
- 下一步：
  - 按 `REFMEM_DOMAIN_TODO.md` 的 P0/P1 更新索引和架构入口。
