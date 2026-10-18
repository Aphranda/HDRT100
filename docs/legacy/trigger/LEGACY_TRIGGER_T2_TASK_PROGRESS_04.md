# TRIGGER T2 任务进展归档 04

Status: Frozen
Domain: TRIGGER
Canonical: `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_04.md`
Related: `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/check/DOCS_REGRESSION_PLAN.md`
Last updated: 2026-10-18

> 本文件是 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md` 按 C14（环5）轮转迁出的历史证据段，
> 覆盖 `T2-PROGRESS-20261010-023` .. `T2-PROGRESS-20261009-021`（按新鲜度倒排，最新在前），共 3 条。
> 条目正文逐字保留、未作编辑；规范文件的 `## 归档索引` 是唯一索引入口。
> 轮转规则见 `docs/check/DOCS_REGRESSION_PLAN.md` §0 C14。

### T2-PROGRESS-20261010-023：从板首次完成 T2 动作闭环，剩余缺失定位为一等席传输丢帧

- TODO task ID：`T2-EXEC-001`、`T2-TRANSPORT-002`；提交 `914a854d`。
- **源起动时序（A-13）**：探针在 T2 START 前即启动模拟源时，NO1 在任何 PREVIEW 保存前越过全部阈值（`announced_mask=7`），`stage_one_freeze()` 遇首个无预览位置即返回，后续位置不再检查，FREEZE 永不发出。以既有 `--late-source` 绕过后 NO1 冻结三个位置；固件侧"无预览位置判失败并继续"属 T2 语义变更，待单独评审，未改。
- **从板隔离**：动作武装后从板 SYNC_TRIGGER 升至 27–36k 拍（预算 26800，快照）被隔离，原因是已武装 deadline 的逐片轮询（`service_deadlines`、sync_io switch/gateway `realtime_service_core1`/`realtime_status`）从 XIP 执行；放入 RAM 后四板零隔离。
- **FREEZE 丢失（接收侧）**：FREEZE 每位置只发一次，而从板 RX 邮箱单深度，前一 PREVIEW 未被取走时 FREEZE 以 FRAGMENT 拒收（NO2 达 77 次，CRC 拒收 0）。按用户选定方案 a 增加接收侧按位置 FREEZE 锁存，取用时 FREEZE 优先；拒收降至 3–7 次。
- **分段计时**：T2 Core1 服务新增 cursor/receive/deadlines 三段峰值，附于 `TRIGger:REServation:T2?` 末尾。从板峰值约 11.4k/17.7k/10.1k 拍（快照，`t2-bench-probe-194914`）。
- **实测（四板，`t2-bench-probe-194914`，快照）**：NO2 三个位置 received/frozen/armed/done 全为 7，为首次从板端到端动作闭环；NO3 frozen=2、NO4 frozen=6，已冻结者全部完成。P3 `p3-195851` PASS_WITH_WARNINGS（DPLL PASS；新增 T1/T3 SCK 训练诊断告警，TDMA deadline miss 告警与前次相同）。
- **未闭合**：NO1 一等席记录 137 次上车，三从板 `rx_complete_count` 均为 43，同帧共同丢失、无 CRC 错误，指向 origin 或从板对背靠背快速路径帧的公共处理。用户判断为特等与一等发射间隔过近、从板尚未处理完特等帧；先以 TX/RX 时间戳精确预算，再决定是否在 TDMA-SEAT-01 中引入最小间隔（底层语义变更，须先评审）。




### T2-PROGRESS-20261010-022：紧缩单帧记录经一等席快速路径跨环送达，动作执行未闭合

- TODO task ID：`T2-TRANSPORT-002`、`T2-TURNTABLE-003`、`T2-EXEC-001`；提交 `19bb91dd`（紧缩单帧）、`71ed3e5f`（NO1 去隔离、台架链）、`8bd547b2`（TDMA-SEAT-01）。
- **台架链更正**：`tools/sync_trigger/t2_enable.py` 的 COUNTER 行输出掩码改为 0（`trigger_deployment_validate()` 要求 COUNTER 无输出，原 0x8 使整表被拒、`T2:CONFigure` 返回 92）；部署 generation 每次单调递增（`DEPloyment:CLEar` 保留 `last_generation`）；仅 NO1 为 origin、全板 source_slot=0。`T2-PROGRESS-20261009-021` 的“唯一阻塞”与“NO1 不配置符合预期”两项结论据此更正，原条目保留。已跑通的 09-27 台架链固化为 `tools/sync_trigger/t2_bench_probe.py`。
- **NO1 SYNC_TRIGGER 去隔离**：origin 热路径（`service_origin`、`preview_copy`、`publish_begin/end`、COUNTER 读取）放入 SRAM，运行时最大 27460→约 9600 拍（预算 26800，快照）。
- **载荷**：位置记录改为 10 B 紧缩单帧（tag、version16、target_ms_index32、fine24），恰占 mailbox RefMem 区，一帧发完；线上不携带运行身份，从板以启动期配置补全后校验。
- **运输**：TDMA-SEAT-01 后 T2 记录写入一等邮箱，经 origin 快速路径与特等按发布机会 1:1 轮换上车；不再改写已排队 VDC image。
- **实测（四板，单轮 12 s，快照，`t2-bench-probe-183247`）**：NO1 记录上车；三从板 `rx_complete_count` 6/1/1、`received_mask` 含位置 3、SYNC_TRIGGER 零隔离；DPLL60 PASS（`dpll60-102638`）。
- **未闭合**：三从板 `T2:ACTion?` 全零，尚无冻结与执行；需逐环检查 PREVIEW 版本推进、阈值 FREEZE 送达、从板本地 deadline 解算与 PIO 装填。NO3/NO4 各 5 次 RX 拒绝（FRAGMENT），待查。




### T2-PROGRESS-20261009-021：使能链逐环打通至 `T2:CONFigure` 成功（发布仍受飞行 FIFO 静止条件阻塞）

- TODO task ID：T2-DEPLOY-002、T2-EXEC-001、T2-SLOT-GATEWAY-001、T2-ACCEPT-001。
- 本轮定位并逐环打通了"从加载到 T2 运行"的完整前置链，全部以硬件读回为证据（不再依赖推断）：

**1. 网格**：`GRID:CONFigure` 被拒的根因是 `subdivisions` 必须 ≥ 2
（`trigger_time_grid_valid()` 要求 `epoch && generation && subdivisions >= 2 && period_ns >= subdivisions`）。
修正后 `GRID?` 显示 configured=1、period=1 ms、subdivisions=2。

**2. 部署**：`DEPloyment:STAGe`/`:ACTivate` 的 CRC 由 `trigger_deployment_crc()` 生成，主机实现与设备一致
（设备直接接受并在 `DEPloyment?` 回显该 CRC）⇒ 部署 staged+active 成立。

**3. 动作表**：安装校验包含两道独立闸门，二者都需满足
（`flash_transaction` 层与动作层）：`trigger_action_table_validate()` 要求
**动作行 `role` 等于本板 `binding.local_role`**，因此必须**逐板、按本板角色**构造动作表；
且 COUNTER 板（NO1）无对应 `kind`（`SWITCH_CODE`/`GATEWAY_PULSE`），其动作安装被 ROLE 拒绝，符合"COUNTER 只观察"。

**4. 模板库（此前完全缺失的一环）**：`T2:CONFigure` 的前置不是动作运行时，而是**独立的动作模板库**
`trigger_action_template_store`（命令族 `TRIGger:REServation:TEMPLate:{INSTall,CLEar,ACTivate,RELease,?}`）。
`TEMPLate:INSTall` 的 CRC 由 `trigger_action_template_table_crc()` 生成（header 两字 + 每行
`template_id,generation,role,kind,argument,span_slices,delta_q` 两个 32 位字），主机实现经设备验证为 OK；
随后 `TEMPLate:ACTivate` 置 `resources_loaded`/`binding_current`。

**5. 停机维护窗口**：`T2:CONFigure` 经 `tdma_service_run_stopped_maintenance()` 落地；
该函数要求"环未使能、适配器未启动、配置已应用、无挂起停机元数据更新/环控制、且
`ring_staged_config.geometry_generation == 0`"。注意与第 3/4 步**存在时序张力**：
绑定成立需要 staged 环配置，而维护窗口要求 staged 几何已消费 ⇒ 需在同一停机窗口内按序完成。

**6. 预测器取值规则**：`trigger_turntable_predictor_config_valid()` 要求
`pulses_per_degree`、**`pre_roll_mdeg` 与 `post_roll_mdeg` 均非零**、
`start_angle_mdeg <= zero_angle_mdeg <= terminal_angle_mdeg`、`0 < threshold_per_mille < 1000`。
此前 `T2:CONFigure` 一直失败的直接原因即 roll 为 0。

**7. 结果**：修正预测器参数后，三块执行板（NO2 SWITCH_A / NO3 GATEWAY / NO4 SWITCH_B）
`T2:CONFigure` **成功**，`T2?` 的 `configured = 1`（NO1 因 COUNTER 无模板未配置，符合预期）。

**8. 当前唯一阻塞**：`T2:STARt` 的准入第二条 `distributed_refmem_reset_position_stopped()` 返回 false
（`scpi_t2_update_stopped()` operation=1 路径）——该函数要求环未使能/适配器未启动/配置已应用、
飞行 FIFO `tx_ready_count == 0 && tx_active_generation == 0 && rx_parse_count == rx_queued_count`、
且本地与各节点位置邮箱不处于 WRITING/READING。实测在"复位后未启环"的静止态下仍不满足
⇒ **位置帧发布（`SYSTem:REFMEM:SYNC:FLIGHT:POSition?` 的计数推进）尚未观测到**，为下一步唯一待办。

- 证据与可复现手段：本轮为使能链每一环建立了"诊断通道"（复用席位挂载状态查询
  `TRIGger:REServation:SEAT:MOUNT?` 的 last_reject 字段，采用"基线 + 首个触发者优先"语义），
  可一次读出拒绝原因；相关临时插桩需在目标达成后清理或规范化为正式诊断。
- 台架：四板在线；硬件验收跑（含 1 ms DPLL 60 s）多次 PASS，构建 `flash_link_contract=OK`。




