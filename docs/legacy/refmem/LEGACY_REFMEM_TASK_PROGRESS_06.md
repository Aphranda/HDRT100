# Distributed RefMem 内部主域任务进度（归档 06）

Status: Frozen
Domain: REFMEM
Canonical: `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_06.md`
Related: `docs/refmem/REFMEM_TASK_PROGRESS.md`
Last updated: 2026-10-27

本文件接收 `REFMEM_TASK_PROGRESS.md` 轮转出的最旧条目（C14）；条目保持原文与倒排顺序。

### REFMEM-TASK-20260815-036 - Cross-core seqlock guard release fence

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 修复风险评审 §3.1：跨核 seqlock/guard 写侧普通自增缺少 release fence，读侧 acquire 无法保证读取到完整可见的数据快照。
  - 保持 HAOFV 边界：`refmem_realtime_tdma` 仍由 core0 发布 intent、core1 发布 result；`refmem_command` 仍由 command slot owner 处理 ACK/NACK，不引入跨层业务逻辑。
- 完成内容：
  - `refmem_realtime_tdma.c` 新增 guard release increment helper，intent/result begin/end write 均通过 `__atomic_add_fetch(..., __ATOMIC_RELEASE)` 发布 guard。
  - `refmem_command.c` 新增 command guard release increment helper，command slot begin/end write 均通过 release 语义发布 guard。
  - 扫描 `components/distributed_refmem`，确认无 `guard++` 残留。
- 验证结果：
  - `rg -n "guard\+\+|\+\+.*guard" components\distributed_refmem -S` 无残留匹配。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_command_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_contract_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815140755`，package CRC `0x8FC40ACD`。

### REFMEM-TASK-20260815-035 - Staging table view pre-parse activation gate

- 状态：完成 COM5/COM6 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 收掉 active image 已切换但 application runtime snapshot 解析失败的理论窗口。
  - 保持 HAOFV 边界：SCPI 只提交 activation intent；`DistributedRefMemAO` 调度，`RefMemTableRegistry` 管 image，`refmem_application_model` 作为 owner 预解析 staging stable view。
- 完成内容：
  - 新增 `refmem_application_model_prepare_staging_table_views()`，从 staging image 借出 9 张 canonical 表并解析到 pending runtime snapshot。
  - 新增 `refmem_application_model_commit_prepared_table_views()` / `discard_prepared_table_views()`，registry activation 成功后才提交 pending，失败时丢弃。
  - `distributed_refmem_activate_staging()` 在调用 `refmem_table_registry_activate_staging()` 前先执行 staging view 预解析；预解析失败时记录 `STAGING_VIEW_INVALID`，并通过 command slot NACK。
  - 新增 `REFMEM_TABLE_ACTIVATE_ERR_STAGING_VIEW_INVALID` 和 `REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED`，并同步 `SYSTem:COMMand:NACK?` reason table。
  - `test_refmem_table_registry.c` 增加 prepare/commit 断言：prepare 不改变 active getter，commit 后才切到 package runtime snapshot，pending 只能提交一次。
  - `tools/refmem/refmem_table_registry_validate/refmem_table_registry_validate.py` 增加 preflight error queue drain，避免 Storage/OTA 历史错误污染本轮验证；summary 只将 `FAIL` 计入失败。
  - `tools/storage/storage_file_upload/storage_file_upload.py` 增加 StorageAO job idle 等待和 active write transaction abort preflight，避免目录/删除/写入 job 交错。
  - `tools/interface/scpi_query/scpi_query.py` 修正 `INFO? "path"` 这类带参数查询识别，并新增 `tools/interface/scpi_query/refmem_storage_info_probe.scpi` 作为 RefMem HIL SD 依赖探测命令集。
  - `BoardCapabilityTable` 当前产品 wire contract 固定为 8 条 A0-A7 能力记录；`CLAIM_CANDIDATE_MAX = 16` 只保留给 SlotClaim proposal / overflow evidence，不复用为 BoardCapability 表容量。
  - `scpi_refmem_read_package()` 先通过 StorageAO file info 获取真实文件大小，再按剩余长度分块读取，避免最后一块 512 字节越过文件尾导致 `LOAD:SD` 读包失败。
  - `tools/storage/sd_fs_build/sd_fs_build.py` 固化生成 `manifest_refmem_hil.idx`，用于 RefMem HIL，只要求 profile、mission、cal 和 RefMem `.rmtp` 四个小文件。
  - 新增 `tools/interface/scpi_query/refmem_file_read_probe.scpi`，用于通过 Storage `FILE:READ?` 探测 `/refmem/app_model.rmtp` 头尾读取。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python -m py_compile tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `python -m py_compile tools\storage_file_upload\storage_file_upload.py` 通过。
  - `python -m py_compile tools\scpi_query\scpi_query.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `python -m pytest tests\python\test_refmem_pack_build.py -q` 通过，2 passed；pytest cache 写入 `.pytest_cache` 因权限被拒绝产生 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，最终生成 build id `20260815135119`，package CRC `0x1801DEA8`。
  - `python tools\sd_fs_build\sd_fs_build.py --build-dir build-rtos-multicore-smoke --output-dir build-rtos-multicore-smoke\sdcard_8slot_20260815135119 --clean --no-zip --no-reports` 通过，生成 8 槽位 `.rmtp` size `4512`，package CRC `0x31062D02`，BoardCapability table size `296`。
  - COM5 OTA 到 build `20260815135119` 并 boot commit 成功；上传 8 槽位 HIL 文件集合成功。
  - COM5 执行 `refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_8slot_20260815135119\refmem\app_model.rmtp --load-sd --activate` 通过：`LOAD:SD` 返回 `STAGED`，`LOAD:ACTivate` 返回 `ACTIVE`，active image table mask `511`，BoardCapability table size `296`，最终 `SYSTem:ERRor?` 为 `0,"No error"`。
  - COM6 OTA 到 build `20260815135119` 并 boot commit 成功；上传 8 槽位 HIL 文件集合成功。
  - COM6 首次 `LOAD:SD` 在读包阶段返回 `REJECTED`，随后 `refmem_file_read_probe.scpi` 证明 `/refmem/app_model.rmtp` 头尾 Storage read 正常；复跑 `refmem_table_registry_validate.py COM6 --load-sd --activate` 通过，active image table mask `511`，BoardCapability table size `296`，最终 `SYSTem:ERRor?` 为 `0,"No error"`。
- 过程记录：
  - 全量 System Pack manifest 要求 `/update/RP2350_TRIG_UPDATE.pkg`，通用 Storage write API 当前单事务上限 8192 bytes，不能用它上传 523 KB OTA 包；本轮 RefMem HIL 改用 `manifest_refmem_hil.idx`，只要求 profile、mission、cal 和 RefMem `.rmtp` 四个小文件，不绕过 StorageAO manifest gate。
  - COM5 首次覆盖 manifest 后出现一次 `manifest_status=8 / missing_count=1`，单独 `SYSTem:SD:MANifest?` 随后返回 OK；该现象计入后续 StorageAO manifest scan/job 时序观察，不阻塞本轮 RefMem activation gate 结论。
  - COM6 最终 build 首次 `LOAD:SD` 后、Storage read probe 后复跑成功，继续作为 StorageAO job/manifest 首轮时序支线观察项。
- 后续动作：
  - 继续接入真实 owner validation callback 调度和跨节点 ACK/FENCE。

### REFMEM-TASK-20260815-034 - Active table views to application runtime snapshot

- 状态：完成 COM5/COM6 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 让 registry 级 active image activation 后，业务读取路径不再继续固定读编译内置表。
  - 保持 HAOFV 边界：`RefMemTableRegistry` 只管理 active/staging/rollbackable image 和 const table view，`refmem_application_model` 负责把 active view 解析成私有 runtime snapshot。
- 完成内容：
  - 新增 `refmem_application_model_apply_active_table_views()`，从 active image 借出 9 张 canonical 表的 stable view，解析到 application model 私有 runtime tables。
  - 外部 getter 切到 runtime snapshot：ApplicationMap、BoardCapability、GenericNode、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate、ConnectionQuality。
  - `DistributedRefMemAO` 在 `TABLE_PACKAGE_ACTIVATE` 成功后触发 application model apply，并更新 `active_package_crc32` 与 per-table CRC snapshot。
  - `tools\refmem_table_registry_validate.py` 增加 activation 后 `SYSTem:REFMEM:LOAD:STATus?` 检查，确认 application model active package CRC 已跟随 active image。
  - `tools\tests\run_refmem_table_registry_tests.ps1` 链接 application model 源，新增 host 断言覆盖 getter 从 active image runtime snapshot 取 BoardCapability/FBInstance。
- 当前验证：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python -m py_compile tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815124740`，package CRC `0x1E1584A1`。
  - `python -u tools\ota_send\ota_send.py COM5 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python -u tools\ota_boot_commit\ota_boot_commit.py COM5 --expected-build 20260815124740 --out-dir build-rtos-multicore-smoke\ota_commit_COM5_20260815124740_refmem_app_view` 通过。
  - `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815124740_app_view` 通过。
  - `python -u tools\ota_send\ota_send.py COM6 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python -u tools\ota_boot_commit\ota_boot_commit.py COM6 --expected-build 20260815124740 --out-dir build-rtos-multicore-smoke\ota_commit_COM6_20260815124740_refmem_app_view` 通过。
  - `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM6 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM6_20260815124740_app_view` 通过。
- 后续动作：
  - 把 activation 前 staging stable table view 预解析纳入 activation transaction，减少“registry 已切换但 application apply 失败”的理论窗口。
  - 接入真实 owner validation callback 调度和跨节点 ACK/FENCE。

### REFMEM-TASK-20260815-033 - Stable table view access/release

- 状态：完成 COM5/COM6 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 在 registry 级 image activation 后，提供稳定的单表只读 view，避免后续业务 AO/FB 直接拼 RMTP offset 或读取裸 buffer。
  - 保持 HAOFV 边界：TableRegistry 借出 const payload view，向量表和 SCPI 仍只输出摘要，不承载完整 table 数据。
- 完成内容：
  - 新增 `refmem_table_view_t`，包含 role、table id、table seq、package CRC、table CRC、image offset、image size 和 const payload pointer。
  - 新增 `refmem_table_registry_access_table(role, table_id, view)` / `refmem_table_registry_release_table(view)`，通过 RMTP directory 和 table CRC 定位 payload。
  - 增加 reader guard：active/staging/rollbackable 任一 image 存在未 release view 时，activation 返回 `IMAGE_BUSY`，避免表指针悬挂。
  - 新增 `SYSTem:REFMEM:TABle:VIEW? [role],[table_id]`，通过 access/release 读取 view 摘要：`version,role,table_id,table_seq,package_crc32,table_crc32,image_offset,image_size,first_u32`。
  - `tools/refmem/refmem_table_registry_validate.py --activate` 增加 active table 0 和 table 3 view 摘要断言。
- 当前验证：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过，覆盖 active view access/release、double release reject、unreleased view 阻断 activation。
  - `python -m py_compile tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815122901`，package CRC `0xDDB83D74`。
  - `python -u tools\ota_send\ota_send.py COM5 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python -u tools\ota_boot_commit\ota_boot_commit.py COM5 --expected-build 20260815122901 --out-dir build-rtos-multicore-smoke\ota_commit_COM5_20260815122901_refmem_view` 通过。
  - `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815122901_view` 通过。
  - `python -u tools\ota_send\ota_send.py COM6 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python -u tools\ota_boot_commit\ota_boot_commit.py COM6 --expected-build 20260815122901 --out-dir build-rtos-multicore-smoke\ota_commit_COM6_20260815122901_refmem_view` 通过。
  - `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM6 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM6_20260815122901_view` 通过。
  - COM5/COM6 均确认 `LOAD:SD`、`LOAD:ACTivate`、`TABle:IMAGe?`、`TABle:VIEW? 0,0`、`TABle:VIEW? 0,3`、9 张 `TABle?` 和 `SYSTem:ERRor?` 闭环通过。
- 后续动作：
  - 下一步把 active package view 解析为业务结构表 snapshot，并逐步替换继续读取编译内置表的路径。

### REFMEM-TASK-20260815-032 - Table package activation command

- 状态：完成 COM5/COM6 板端闭环
- 日期：2026-08-15
- 任务目标：
  - 让 P0 table image activation 不再只停留在 host API；补齐板端维护入口和固化验证脚本。
  - 保持 HAOFV 边界：SCPI 只提交 activation intent 和读取 descriptor，真正 active/staging/rollbackable 切换由 `DistributedRefMemAO` / `RefMemTableRegistry` owner 完成。
- 完成内容：
  - 新增 `REFMEM_COMMAND_TYPE_TABLE_PACKAGE_ACTIVATE`，activation 入口也进入 command slot。
  - 新增 `distributed_refmem_activate_staging(realtime_idle)`：本地 owner take 后组装 activation gate，检查 RefMem idle、realtime idle、runtime protection、CRC/owner、SlotClaim、quality/DeploymentGate 和 local command take，再调用 `refmem_table_registry_activate_staging()`。
  - 新增 `SYSTem:REFMEM:LOAD:ACTivate`，返回 `ACTIVE/REJECTED`、registry snapshot，以及 active/staging/rollbackable 三个 image descriptor。
  - 新增 `SYSTem:REFMEM:TABle:IMAGe? [role]`，读取 active/staging/rollbackable descriptor：`version,role,state,table_mask,package_crc32,table_seq,path_hash,last_result,evidence_index`。
  - `tools/refmem/refmem_table_registry_validate/refmem_table_registry_validate.py` 增加 `--activate`，固化 `LOAD:SD -> LOAD:ACTivate -> command ACK -> image descriptor -> TABle? 0..8` 验证。
- 验证结果：
  - `python -m py_compile tools\refmem_table_registry_validate\refmem_table_registry_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815121205`，package CRC `0xFE13F612`。
  - COM5 OTA 到 build `20260815121205` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM6 OTA 到 build `20260815121205` 并 `SYSTem:OTA:COMMit` 通过，错误队列为 0。
  - COM5 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM5 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM5_20260815121205_activate` 通过。
  - COM6 执行 `python -u tools\refmem_table_registry_validate\refmem_table_registry_validate.py COM6 --package build-rtos-multicore-smoke\sdcard_full_tables_20260815110412\refmem\app_model.rmtp --load-sd --activate --timeout 10 --load-timeout 30 --out-dir build-rtos-multicore-smoke\refmem_table_registry_COM6_20260815121205_activate` 通过。
- 结论：
  - P0 已具备板端可触发、可查询、可脚本验证的 registry 级 table package activation 闭环。
  - 本轮是首轮 activation，旧 active 来自编译内置表、没有 package bytes，因此 rollbackable descriptor 为空；后续需增加二次 activation 回滚验证、active package 到业务 stable table view 的解析、owner access/release，以及跨节点 ACK/FENCE。

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
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 同步断言：合法 `LOAD:NODE` 必须有非零 NodeLoadTable staging CRC，`SYSTem:REFMEM:TABle? 3` 的 staging mask 至少包含 bit3。
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
  - 新增 `tools/refmem/refmem_table_registry_validate/refmem_table_registry_validate.py`，读取本地 RMTP header/directory，校验 package/payload/table CRC，然后通过单次串口生命周期查询：
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
  - `SCPI_STORAGE_JOB_WAIT_LOOPS` 从 2000 增加到 10000；`tools/refmem/refmem_pack_write/refmem_pack_write.py` 默认 timeout 从 3 s 增加到 10 s，防止设备端仍在合法写入时 PC 侧先超时。
  - `SCPI_REFMEM_LOAD_JOB_WAIT_LOOPS` 从 200 增加到 10000；`tools/refmem/refmem_pack_write/refmem_pack_write.py` 增加 `--load-timeout`，默认 60 s，专门覆盖 `LOAD:SD` 的长耗时 staged 路径。
  - 新增 `tools/interface/scpi_common/scpi_serial.py`，固化 USB CDC 串口打开、settle、输入/输出缓冲清理、idle-gap 读行和 finally close 生命周期管理；`scpi_query`、`storage_scpi_validate`、`refmem_pack_write` 已接入。
  - 新增 `tools/storage/storage_file_upload/storage_file_upload.py`，作为通用 Storage SCPI 文件上传工具，支持多文件、自动建目录、分片写入、写后 `FILE:INFO?` 校验。
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
  - `tools/interface/scpi_common/scpi_serial.py`
  - `tools/refmem/refmem_pack_write/refmem_pack_write.py`
  - `tools/storage/storage_file_upload/storage_file_upload.py`

### REFMEM-TASK-20260815-026 - Complete RMTP canonical table payloads

- 状态：完成代码侧基础闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `.rmtp` 中剩余 5 张 canonical 表从 64 字节占位 payload 升级为真实固定 u32 wire payload。
  - 让 `RefMemTableRegistry` 对全 9 张表执行 owner validation，同时保持 active image 切换阻断，不伪激活。
- 完成内容：
  - `tools/refmem/refmem_table_image/refmem_table_image.py` 新增 `FbInstance`、`EventLink`、`DataLink`、`DeploymentGate`、`ConnectionQuality` payload 生成。
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
  - `tools/refmem/refmem_table_image/refmem_table_image.py`
  - `tools/refmem/refmem_pack_build/refmem_pack_build.py`
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
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 增加 `LOAD:SD` 后的 `SYSTem:COMMand:ACK?` 验证，确认 command type 为 `TABLE_PACKAGE_STAGE`。
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
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py`

### REFMEM-TASK-20260815-023 - LOAD:BOARD via RefMem command slot

- 状态：完成首版闭环
- 日期：2026-08-15
- 任务目标：
  - 将 `SYSTem:REFMEM:LOAD:BOARD` 从 SCPI 直接调用 BoardCapability staging API 收敛到 `DistributedRefMemAO` command slot / staging 路径。
  - 验证 `board_id` 只作为板卡/profile 候选 payload，不被误用为 A0-A7 target slot。
- 完成内容：
  - 新增 `REFMEM_COMMAND_TYPE_BOARD_CAPABILITY_STAGE` 和 `distributed_refmem_stage_board_capability()`。
  - `SYSTem:REFMEM:LOAD:BOARD` 改为调用 RefMem intent API；`DistributedRefMemAO` 原子清理已完成 command、post `BOARD_CAPABILITY_STAGE`、由本地 RefMem owner take、调用 `refmem_application_model_stage_scpi_board_capability()`，随后 ACK/NACK。
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 增加 BoardCapability 正向/负向验证：有效 board 候选应 ACK，非法 `board_id=16` 应 NACK。
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
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py`

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
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 增加 `SYSTem:COMMand:ACK?` 校验，并把 table count / active mask 更新为当前 9 张表 / `0x1FF`。
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
  - `tools/refmem/refmem_scpi_load_validate/refmem_scpi_load_validate.py`

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
  - `tools/trigger/model_turntable_load_validate/model_turntable_load_validate.py` 增加 `SYSTem:COMMand:ACK?` 和 `SYSTem:COMMand:NACK?` 检查，验证 `NODE_LOAD_STAGE` command 的 ACK 事实。
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
  - `tools/trigger/model_turntable_load_validate/model_turntable_load_validate.py`

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
  - 新增 `tools/trigger/model_turntable_load_validate/model_turntable_load_validate.py`，固化 build id、默认未加载、LOAD、staging snapshot、配置 ACK 兼容视图和错误队列检查。
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
  - `tools/trigger/model_turntable_load_validate/model_turntable_load_validate.py`

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
  - `tools/refmem/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py` 增加 config ACK 正向、负向和恢复后的全 ACK 检查。
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
  - `tools/refmem/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py`

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
  - 新增 `tools/refmem/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py`。
  - 脚本读取初始 `SYSTem:CONFigure:STAT?` 和 `SYSTem:REFMEM:SYNC:TDMA:STATus?`，确认初始 config gate ready。
  - 脚本发送短窗口 `SYSTem:REFMEM:SYNC:TDMA:RX` 且不发 TX，轮询 TDMA timeout，并确认 `SYSTem:CONFigure:STAT?` 进入 `ready=0, gate_state=2`。
  - 支持 `--restore-package` 和 `--expected-build`，在负向验证后调用 OTA send + boot/commit 恢复板卡，并再次确认 config gate ready。
  - 修正 `SYSTem:CONFigure:STAT?` 解析：首字段 build id 是带引号数字字符串，不能被当成 ready 字段。
- 验证结果：
  - `python -m py_compile tools\refmem_quality_gate_hil_validate\refmem_quality_gate_hil_validate.py` 通过。
  - COM5 执行 `python tools\refmem_quality_gate_hil_validate\refmem_quality_gate_hil_validate.py COM5 --expected-build 20260815053147 --restore-package build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --out-dir build-rtos-multicore-smoke\refmem_quality_gate_COM5_20260815053147_scripted_r3` 通过。
  - 报告路径：`build-rtos-multicore-smoke\refmem_quality_gate_COM5_20260815053147_scripted_r3\summary.json`。
- 关联文件：
  - `tools/refmem/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py`
