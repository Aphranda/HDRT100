# REFMEM 任务进度归档 04

Status: Frozen
Domain: REFMEM
Canonical: `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_04.md`
Related: `docs/refmem/REFMEM_TASK_PROGRESS.md`
Last updated: 2026-10-25

## 归档说明

由 C14 轮转从 `docs/refmem/REFMEM_TASK_PROGRESS.md` 迁入，newest-first 保存
`REFMEM-TASK-20260815-006..REFMEM-TASK-20260814-050`；未删除、未改写。归档文件同样遵循 C15 倒排。

### REFMEM-TASK-20260815-006 - RMTP ApplicationMap 真实表镜像

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 按 P0 “逐表替换占位 payload”推进 `.rmtp` table 0 `ApplicationMap`。
  - 将 ApplicationMap 校验从 application model 内部静态 linter 提升为可被 SD/System Pack parser 复用的公共 table contract。
- 完成内容：
  - `tools/refmem/refmem_table_image/refmem_table_image.py` 新增 `build_application_map_payload()`，table 0 从 64 字节占位改为 6 个 u32 的真实 `ApplicationMap` payload。
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
  - `tools/refmem/refmem_table_image/refmem_table_image.py`
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
  - 消除 `tools/refmem/refmem_pack_build/refmem_pack_build.py` 与 `tools/storage/sd_fs_build/sd_fs_build.py` 中重复维护的 RMTP 表格式、表名、CRC、BoardCapability 和 GenericNode payload 生成逻辑。
  - 保证独立 RefMem package 工具和 SD System Pack staging 输出同一份 `.rmtp` 二进制表镜像，避免后续表格式演进时出现双源分叉。
- 完成内容：
  - 新增 `tools/refmem/refmem_table_image/refmem_table_image.py`，作为 canonical RMTP table image builder，统一 `RMTP` header、directory、package CRC、9 张表顺序、默认 BoardCapabilityTable 和 GenericNodeTable payload。
  - `tools/refmem/refmem_pack_build/refmem_pack_build.py` 改为复用共享 builder，只保留独立输出目录、manifest 和 idx 生成职责。
  - `tools/storage/sd_fs_build/sd_fs_build.py` 删除私有 `build_refmem_table_package()`、`build_refmem_board_capability_payload()`、`build_refmem_generic_node_payload()` 等重复实现，SD staging 直接调用共享 builder。
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
  - `tools/refmem/refmem_table_image/refmem_table_image.py`
  - `tools/refmem/refmem_table_image/__init__.py`
  - `tools/refmem/refmem_pack_build/refmem_pack_build.py`
  - `tools/storage/sd_fs_build/sd_fs_build.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`

### REFMEM-TASK-20260815-003 - RMTP Board/Generic owner validation

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 推进 P0 `.rmtp` 从纯 CRC parser 走向真实表镜像 owner validation。
  - 先闭环固定 u32 布局的 `BoardCapabilityTable` 和 `GenericNodeTable`，避免继续把 64 字节 placeholder 当成可验证表。
- 完成内容：
  - `tools/refmem/refmem_pack_build/refmem_pack_build.py` 和 `tools/storage/sd_fs_build/sd_fs_build.py` 生成真实 `BoardCapabilityTable` / `GenericNodeTable` payload；其他表暂时仍是 placeholder，后续逐表替换。
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
  - `tools/refmem/refmem_pack_build/refmem_pack_build.py`
  - `tools/storage/sd_fs_build/sd_fs_build.py`
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
  - `tools/rtos/multicore_board_validate.py` 不再把 A0/A2/A7 解释成固定功能位置，只验证 generic SlotClaimMap gate、required/optional 和 config RUN gate 一致性。
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
  - `tools/rtos/multicore_board_validate/multicore_board_validate.py`
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
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 增加 runtime quality 查询校验，确认远端 QUALITY frame 接收后进入派生质量视图。
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
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
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
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 新增 `--package-crc`、`--line-remap-a-to-b`、`--line-remap-b-to-a` 和 `--preflight-io`。
  - `--preflight-io` 会在主协议交换前调用 `tools/sync/two_board_io_validate/two_board_io_validate.py`，该工具完成双板 profile 查询、输出 release、逐线 remap 验证和退出释放。
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
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
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
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 扩展为 HELLO/EPOCH/DELTA/MIRROR/ACK_NACK/FENCE/QUALITY 全流程，记录 61 条 HIL 结果。
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
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`
- 下一步：
  - 回到 P4.5 未完成项：真实 PIO SPI physical adapter service、线序/生命周期阶段 0 固化，以及 quality 到 `DistributedConnectionQualityTable` 的正式映射。

