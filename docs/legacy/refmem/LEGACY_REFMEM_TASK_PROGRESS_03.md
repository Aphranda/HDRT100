# REFMEM 任务进度归档 03

Status: Frozen
Domain: REFMEM
Canonical: `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_03.md`
Related: `docs/refmem/REFMEM_TASK_PROGRESS.md`
Last updated: 2026-10-24

## 归档说明

由 C14 轮转从 `docs/refmem/REFMEM_TASK_PROGRESS.md` 迁入，newest-first 保存
`20260814-049..20260814-040`；未删除、未改写。归档文件同样遵循 C15 倒排。

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
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 扩展为 HELLO/EPOCH/DELTA/MIRROR/ACK_NACK/FENCE 全流程，增加双向 FENCE pass 和 min seq 不满足的 fail/timeout 场景。
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
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
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
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 扩展为 HELLO/EPOCH/DELTA/MIRROR/ACK_NACK 全流程，正向 ACK 双向生成后再互相注入，避免 ACK frame 覆盖 DELTA `last_rx`。
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
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
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
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py` 从 HELLO/EPOCH 扩展为 HELLO/EPOCH/DELTA/MIRROR，并增加 build id、SlotClaimMap CRC 和 adapter snapshot 预检。
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
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
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
  - 新增 `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py`，固化两板 HELLO/EPOCH 搬运验证，支持 COM 口和 USBTMC VISA resource。
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
  - `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py`
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
  - 新增 `tools/sync/debug_model_overlay_validate/debug_model_overlay_validate.py`，默认验证 X->Y GPIO4/GPIO5/GPIO7 和 Y->X GPIO6，运行前后 release 双方 GPIO4..7。
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
  - `tools/sync/debug_model_overlay_validate/debug_model_overlay_validate.py`
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

