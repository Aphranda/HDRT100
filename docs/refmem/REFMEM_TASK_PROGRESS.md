# Distributed RefMem 内部主域任务进度

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_TASK_PROGRESS.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`
Last updated: 2026-08-13

本文档记录 Distributed Vector Blackboard / RefMem Sync Domain 的阶段性任务进度、验证结果和后续动作。待办事项放在 `REFMEM_DOMAIN_TODO.md`，本文只记录已经发生的工作和可回溯结果。

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

首阶段先完成文档主域和架构边界，不修改代码。

## 任务记录

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
