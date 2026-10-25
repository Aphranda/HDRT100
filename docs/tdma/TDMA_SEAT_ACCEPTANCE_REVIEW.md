# 一等席验收矩阵（TDMA-SEAT-03 / 目标"一等席稳定性达到特等席水平"）

Status: Draft
Domain: TDMA
Canonical: `docs/tdma/TDMA_SEAT_ACCEPTANCE_REVIEW.md`
Related: `docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/tdma/TDMA_SEAT_TX_CHANNEL_REVIEW.md`
Last updated: 2026-10-25

## 文档接口与范围

本文是**验收索引**：把"一等席达到特等席水平"这一目标拆成可核查的条目，逐条给出**证据落点**（符号 / 命令 / 测试）与状态，供评审与提交前复核使用。

- 它**不定义语义**：语义以 [TDMA_RUNTIME_CONSTRAINTS.md](TDMA_RUNTIME_CONSTRAINTS.md)、[TDMA_DOMAIN_ARCHITECTURE.md](TDMA_DOMAIN_ARCHITECTURE.md) 与代码为准；本文不复制语义、不当第二事实源（AGENTS.md C12）。
- 它是评审快照，**不登记为契约**（硬约束 6），也不改变 [TODO](TDMA_DOMAIN_TODO.md) 状态。
- 复核命令见文末；证据分三类：**主机测试**（可复现）、**只读命令**（需真机）、**待做**。

## 一、验收矩阵

| # | 验收条目 | 实现落点 | 证据（只读/可复现） | 状态 |
|---|---|---|---|---|
| 1 | 一等席有独立服务事实（被拒/退休/等待） | `tdma_first_class_stage/retire`（`components/tdma/inc/tdma_first_class.h`） | `SYSTem:TDMA:SEAT:COUNters?` 的 `first_class_stage_refused_count`、`first_class_retired_waiting_count`；`tests/python/test_tdma_seat_rotation.py` | ✅ |
| 2 | 一等席有界背压（不丢、可计数） | 单深度席位 + 计数 | `first_class_stage_refused_count`；同上测试 | ✅ |
| 3 | 一等席完整性由通道统一校验后才投递 | `tdma_priority_rx_validate()`（header/identity/transport + mailbox CRC16） | `tests/python/test_tdma_priority_rx.py`、`test_t2_freeze_completion.py` | ✅ |
| 4 | 一等席接收有界保全（拒绝不丢） | 每源 FIFO 深度 `DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH`、只追加 SPSC | `POSition?` 的 `lane_hold/drain/drain_fail`；`test_t2_freeze_completion.py` | ✅ |
| 5 | 保全队列有界（满则丢弃并计数） | `lane_hold_dropped_count` | 同上 | ✅ |
| 6 | 单条重投有界（超预算退休并计数） | `DISTRIBUTED_REFMEM_POSITION_HOLD_MAX_ATTEMPTS`、`lane_hold_expired_count` | 同上 | ✅ |
| 7 | 一等席服务期限：等待有声明预算 | `TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES` | `SEAT:COUNters?` 的 `seat_first_class_wait_max`、`seat_first_class_wait_violation_count`；`test_tdma_seat_rotation.py` | ✅ |
| 8 | 发布侧推迟也进同一条预算（不无界等待） | `seat_first_class_deferred_count` | `SEAT:COUNters?`；`test_tdma_seat_rotation.py` | ✅ |
| 9 | 失败按 class 归因（不与他人混淆） | `tdma_priority_rx_reject_class()` 三桶 | `SYSTem:TDMA:FLIGHT:PRIORity?` 尾部四字段；`test_tdma_priority_rx.py` | ✅ |
| 10 | 切片不可解码 = 不可恢复损失，独立计数 | `lane_decode_reject_count` | `POSition?`、`SEAT:HEALth?` bit7；`test_t2_freeze_completion.py` | ✅ |
| 11 | 共享控制字节：一等优先 + 诊断准入 | `distributed_refmem_position_ack_control_available()` | `lane_diag_refused_count`、`lane_ack_override_count`；`test_t2_freeze_completion.py` | ✅ |
| 12 | 席位窗口饥饿显式（**非设计机制**） | `tdma_loop_plan_seat_service()`、`dropped_mask`、`tdma_pio_spi_ring_origin_seat_gate()`；owner 定案：**席位调度由 M:N 轮换实现**（见条目 19），loop-plan 窗口门控保持 `TDMA_LOOP_SCHEDULE_ENABLED == 0` | `seat_starved_count`、`seat_first_class_starved_count`；`test_tdma_seat_admission.py` + `test_tdma_seat_rotation.py` 的门控行为与构建守卫用例 | ✅ 逻辑与门控行为已主机验证；**非现行调度机制**（`TDMA-PROGRESS-20261011-034`） |
| 13 | 稳定性判定页（一次读回给结论与原因） | `tdma_seat_health_reason()`、`SYSTem:TDMA:SEAT:HEALth?` | 8 个 reason 位 + 22 字段证据；`tests/unit/test_tdma_pio_spi_ring_adapter.c`（13 组断言） | ✅ |
| 14 | **共用快速通道的分时复用**（owner 定案：无独立通道） | M:N 轮换 + 机会守恒 + 推迟/预算记账 + 共享控制字节准入 | 见条目 19/20 与 `test_tdma_seat_invariants.py`；`lane_diag_refused_count` / `lane_ack_override_count`；[方案评审](TDMA_SEAT_TX_CHANNEL_REVIEW.md)（其 A″/B 方案已作废） | ✅ 语义已由 owner 裁定（2026-10-11）：**不存在"通道独立"要求** |
| 15 | 真机证据（四板台架 / P3） | `tools/sync_trigger/seat_acceptance.py`（只读判定）+ P3 构建烧录 | 见四、执行剧本；期望每板 `ok=1, reason_mask=0` | ⏳ 剧本与工具就绪，待授权执行 |
| 16 | 席位工程健康度（本域测试与治理不红） | 见 `TDMA-PROGRESS-20261011-014/015/016/017/018` | 全量主机套件 `1 failed / 8070 passed`（唯一失败为 TOOLS 治理项） | ✅ |
| 17 | **设备构建与 SRAM/scratch 预算** | `tools/cmake_build_auto/cmake_build_auto.py`（preset `pico2-release`，与 P3 同路径）；秒级守卫 `tools/sync_trigger/seat_acceptance.py --device-syntax` | 两槽链接 + `flash_link_contract=OK`；`link_free_bytes=2108`、`.scratch_x`=1000 B（+3 KiB 栈 = 24 B 余量）；波形段 donor `352→334`（−1512 B，见 `TDMA-PROGRESS-20261011-025`）；守卫对 8 个席位相关 TU（含消费侧 	rigger_t2_runtime.c） 全 PASS，且已用"撤掉 `<stddef.h>`"负测证明能复现目标构建的报错 | ✅ 链接通过；波形段容量待评审确认 |
| 18 | **队列出口记账闭合**（随机化不变量） | `lane_hold_count == lane_drain_count + lane_hold_expired_count + lane_hold_delivered_count + 仍在队列` | `tests/python/test_tdma_seat_invariants.py`（24 种子 × 400 次随机投递，逐次投递后 + 每个序列结束时校验）、`test_t2_freeze_completion.py` 线上重复用例 | ✅ 补上原先未计数的出口后闭合（见 `TDMA-PROGRESS-20261011-027`） |
| 19 | **M:N 席位调度本身**（公平性 + 机会守恒）（随机化不变量） | `seat_opportunity_count == seat_priority_count + seat_first_class_count + seat_first_class_deferred_count + seat_first_class_reject_count`；一等席份额 − 理想值 ≤ 1 个槽位；`seat_first_class_wait ≤ TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES` | `test_tdma_seat_invariants.py`：6 组 M:N × 32 种子 × 400 次机会 + 16 种子故障恢复存活性 | ✅ 全绿 |
| 20 | **采集自解释（声明预算 + 门控开关可读）** | `SYSTem:TDMA:SEAT:ROTation?` 11 字段：配置/生效比例、`share_max`、`cycle_max`、等待预算、`hold_depth`、`hold_max_attempts`、`loop_schedule_enabled` | `scpi_system_snapshot_commands.c`（`scpi_cmd_system_tdma_seat_rotation_q`）；字段数由 `test_seat_acceptance_tool.py` **从固件源码反推**并与工具常量比对；`seat_acceptance.py` 用 `loop_schedule_enabled` 校验"饥饿计数必须有门控" | ✅（条目的动机：特等席侧有 `PRIORity:BUDget?`，一等席此前只有计数没有上限） |
| 21 | **端到端丢失可判定**（消费侧事实进入判定页） | bit8 `FIRST_CLASS_MISSED` = `freeze_abandoned_count` + `freeze_missing_local_count`（饱和）；两原始计数在页尾 | `tests/unit/test_tdma_pio_spi_ring_adapter.c`（bit8 单置位与全清）、`tests/python/test_seat_acceptance_tool.py`（位 8 的两个证据计数 + 字段数 24 由源码反推）、`seat_acceptance.py`（bit8 认领两个原始计数） | ✅ 本轮闭合。**仍缺**：G1/G2/G5/G8（迟到量级、从未到达的独立计数、放弃路径驱动、期限字段可读） |

## 一之二、消费侧缺口（T2 域，尚未关闭）

以下由独立审计（`TDMA-PROGRESS-20261011-028` 记录）确认，属 **T2 运行时域**的观测能力缺口，一等席的判定页因此可能报出**假健康**：

| 缺口 | 事实 | 证据 |
|---|---|---|
| G1 | ~~迟到/过期的位置片被并入 `descriptor_reject_count`~~ **已归因（2026-10-11）**：`late_arrival_count` 独立计数（聚合计数保留不变），目标溢出（无窗口）明确不计入 | `trigger_t2_runtime.c`（到达准入拆分）、`TRIGger:RESERVation:T2?` 末位 |
| G2 | ~~从未到达没有任何事实~~ **已由既有事实覆盖（2026-10-11 核对）**：消费侧"被要求冻结一个自己从未拥有的位置"即 `freeze_missing_local_count`，已在 `-029` 并入 bit8。审计曾把 `s_pending_mask` 当作"期待到达集合"，实为**待发送 FREEZE 队列**，故不再另设第二个语义重叠的计数 | `trigger_t2_runtime.c:185-207`、`TDMA-PROGRESS-20261011-030` |
| G4 | 冻结完成"放弃"此前只有按位置位掩码；本轮已补饱和计数 `freeze_abandoned_count`（`T2?` 末位） | `trigger_t2_runtime.c:153-157`、`trigger_t2_runtime.h` |
| G5 | ~~放弃路径仍未被驱动~~ **已关闭（2026-10-11，抽取式 harness）**：`tests/python/test_trigger_t2_freeze_abandon.py` 抽取 `resend_one_freeze()` 本体，用同名的自持 statics + 两个 extern 桩直接驱动该分支——放弃（掩码 + 计数 + 不再重发）、计数饱和、仍在窗口内则重发一次、节奏门（`T2_FREEZE_RESEND_SLICES`）、确认后完成、以及暂存被拒只记传输事实。**负测**：移除计数自增后该用例失败，证明断言有牙。此前三轮整机驱动的失败原因也记在同处（原点持续解算刷新窗口 / 冻结采样流触发无关服务错误）。（旧描述，保留作为经过）**三轮未攻克**：`-024` 记录、`-026`/`-027` 两次尝试均为失败——冻结采样流会触发服务错误、保持解算又不断延长窗口；现用例以 **origin 身份**（本轮修正了此前误配为 follower 的问题）跑 400 片，断言"计数 == 掩码 popcount"且无任何位置被报告完成 | `tests/python/test_trigger_t2_runtime.py` 的 `origin_no_ack` 用例 |
| G6 | ~~`SEAT:HEALth?` 的 8 个 reason 位无一由 T2 侧事实驱动~~ **已关闭（2026-10-11）**：新增 bit8 `FIRST_CLASS_MISSED`，由 `freeze_abandoned_count` + `freeze_missing_local_count` 驱动，页尾追加两个原始计数 | `tdma_pio_spi_ring_adapter.h`（`tdma_seat_health_reason`）、`scpi_system_snapshot_commands.c`（`scpi_cmd_system_tdma_seat_health_q`） |
| G8 | ~~没有页暴露承诺期限，迟到量级不可读~~ **已关闭（2026-10-11）**：`T2?` 末尾追加 `late_last_current_ms` 与 `late_last_expiry_sync`（二者之差即最近一次迟到量），随后**逐位置**追加承诺本身（`target_ms_index` 低字与 `expiry_sync`，3 位置共 6 字段） | `trigger_t2_runtime.c`（到达路径记录量级）、`trigger_t2_runtime.h`、`TRIGger:RESERVation:T2?`、`test_seat_acceptance_tool.py::test_t2_page_tail_is_append_only` |
| G7 | ~~`T2?` 页字段顺序未与实现绑定~~ **已关闭（2026-10-11）**：`test_seat_acceptance_tool.py::test_t2_page_tail_is_append_only` 断言新增字段必须**末位按序追加** | `middleware/scpi_port/src/scpi_trigger_t2_runtime_commands.c` |

## 二、待决事项（阻塞 14/15 与 12 的运行时意义）

**定案（2026-10-11，owner 裁定）**：**M:N 轮换（TDMA-SEAT-01）就是席位调度**，`TDMA_LOOP_SCHEDULE_ENABLED` 的 loop-plan 窗口门控**不是本设计的调度机制**，保持 0；相关饥饿计数与门控代码保留为休眠代码（有构建守卫与行为用例，避免腐烂），不再作为待启用项。
2. ~~**A' 方案取舍**~~ **已定案（owner，2026-10-11）**：快速通道只有一条，特等与一等分时复用，A″/B 作废；`TDMA_SEAT_TX_CHANNEL_REVIEW.md` 保留为决策过程记录。
3. **台架 / P3 授权**：条目 15 的全部证据需要构建+烧录。
4. **治理项**：`test_tool_governance.py::test_actual_registry_and_catalog_are_in_sync` 需 TOOLS owner 处理 103 个未评估工具族与 `invalid allowed tool root file` 条目。
5. **timeliness 是否要有自己的判定位**：`late_arrival_count` 目前只作为 `TRIGger:RESERVation:T2?` 的证据（与特等席的可见性做法一致：特等席也没有"迟到"判定位，只有 VDC 侧的 age/interval 预算）；若希望"迟到但未丢失"也让 `SEAT:HEALth?` 报非零，需要新增 bit9 `FIRST_CLASS_LATE`——这属语义范围变更，需你确认。
6. **波形段容量**：为恢复 SRAM 余量，`VDC_DPLL_MANAGER_WAVEFORM_SEGMENT_MAX_RECORDS` 由 352 缩为 334（−1512 B，属产品可见参数，缩容由目标授权）；需评审确认该容量可接受，或改为缩小席位侧保存队列（`DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH`，当前 4）。

## 三、复核命令（主机 + 设备构建，可复现）

> 凡改动固件（含头文件），**设备构建属于验证范围**：`TDMA-PROGRESS-20261011-025` 记录了两处只有发布构建才能发现的缺陷（缺 `NULL` 声明、SCPI 处理器读不存在的结构成员）与一次 SRAM 溢出。

```powershell
$env:HOST_CC='D:\Microsoft\mingw64\bin\gcc.exe'
# 席位/位置/诊断/运输验收集（条目 1–13）
python -m pytest tests/python/test_tdma_seat_admission.py tests/python/test_tdma_seat_admission_owner.py `
  tests/python/test_tdma_seat_rotation.py tests/python/test_t2_freeze_completion.py `
  tests/python/test_t2_position_flight.py tests/python/test_tdma_priority_rx.py `
  tests/python/test_tdma_priority_rx_scpi.py tests/python/test_vdc_feedback_transport.py `
  tests/python/test_vdc_flight_rx.py -q -p no:cacheprovider
# 判定页与适配器 host 单测（条目 13）
powershell -NoProfile -ExecutionPolicy Bypass -File tools/tests/run_tdma_pio_spi_ring_adapter_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/tests/run_tdma_service_scheduler_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/tests/run_tdma_ring_runtime_tests.ps1
# 设备构建（条目 17；与 P3 同一 preset，只构建不烧录）
python tools/cmake_build_auto/cmake_build_auto.py --root . --build-dir out/build/seat-ram-measure
python tools/project/ram_budget_check/ram_budget_check.py out/build/seat-ram-measure/DHRT100.elf.map --profile release --top 6
# 秒级设备语法守卫（复用上面构建的 compile_commands.json；不链接、不烧录）
python tools/sync_trigger/seat_acceptance.py --device-syntax
# 文档门禁
python tools/docs_check/docs_check.py --strict-names
python tools/doc_regression_check.py
```

真机（需授权，条目 15）：见下节剧本。

## 四、真机执行剧本（只读优先，需授权）

**步骤 1：构建与烧录**（唯一会改变设备状态的步骤，走既有 P3 流程，不另立一套）：

```powershell
python tools/hardware_acceptance/p3_hardware_acceptance.py run
```

P3 通过后即可用其凭证提交；本验收只负责**读回判定**，不重复构建/烧录逻辑。

**步骤 2：只读采集与本机判定**（不改变设备状态）：

```powershell
python tools/sync_trigger/seat_acceptance.py --port COM3 --port COM4 --port COM5 --port COM6
```

- 该工具**只发查询**（`SYSTem:TDMA:SEAT:HEALth?`、`…:SEAT:COUNters?`、`SYSTem:REFMEM:SYNC:FLIGHT:POSition?`、`SYSTem:TDMA:SCHEDule?`），**从不**发 STOP/START/配置；三种模式（`--port` / `--from-json` / `--self-test`）互斥且必填，因此不存在"参数写错就连上端口"的退化路径。
- 期望：每板 `PASS`（`ok=1`、`reason_mask=0`）；任何非零 reason 位都会被翻译成中文可读原因并指向同一页内的对应计数。
- 证据落在 `out/HardwareAcceptance/<date>/seat-acceptance-<time>/`（`summary.json` + `report.md`）。

**步骤 3：离线复算**（把采集交给评审者，无需设备）：

```powershell
python tools/sync_trigger/seat_acceptance.py --from-json <capture.json>
```

**工具自身的可信度**：`--self-test` 覆盖健康板、8 个 reason 位各自置位、计数与判定位不一致、页长度不符（拒绝解释而不是猜）、饥饿证据；`tests/python/test_seat_acceptance_tool.py` 另外**从固件 SCPI 处理器源码反推字段数**并与工具常量比对（22/12/11/31），因此页长变更不会静默错位。

**边界**：本工具验证的是**共用快速通道的分时复用**（M:N 机会守恒、推迟/预算、迟到与丢失归因、共享控制字节准入），不验证"通道独立"——owner 已定案不存在该要求；也不替代 P3 的构建/烧录验收。
