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
