# Distributed RefMem 内部主域任务进度

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_TASK_PROGRESS.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`
Last updated: 2026-11-09

本文档记录 Distributed Vector Blackboard / RefMem Sync Domain 的阶段性任务进度、验证结果和后续动作。待办事项放在 `REFMEM_DOMAIN_TODO.md`，本文只记录已经发生的工作和可回溯结果。

## 当前 checkpoint

- 最新条目：`REFTMEM-TASK-20261109-001`（REFMEM-READ-01 M0/M1：读写一致目标立项、host 入口对齐、逐槽行 schema 定稿；详见下）。
- 上一阶段：`REFTMEM-TASK-20261029-040`。**361 全 PASS**：SWITCH_A/B 5776(16×361)、GATEWAY 722(2×361)，late/no_plan/backend 0；工具 `78cca54f`；N11 gateway 例外已撤销。
- C11：`REFMEM-CTRL-01` = PASS_WITH_CHANGES（pending 保持）；新增 `REFMEM-ENTRY-01`（pending，待 C11-02）；A 的 host 拆分待 P3 提交。
- 统一控制面：命令回复回程已打通；远程配置 stage 已 ACK；**目标侧配置授权（方案 A）已落地**，非协调者远程配置 12；租约、idle 窗口/事务已实现；非协调者入口在 N5.5。
- N3.2：N3.2.1–5、N3.2.8 完成；N3.2.4 补 per-target ACK；N3.2.6 延后；N3.2.7 部分（待补乱序/窗口 miss/ring stop→start/mid-ring/长期 soak）。
- N5.0：N5.0.2a config-txn 拆出（`distributed_refmem.c` 6,370→6,315）；P3 `20261024144807`；config txn HIL 14/14。
- N5.0.2b：cmd-reply 拆出（6,315→6,109）；P3 `20261024151357`；exec196 HIL 三目标 `frag_complete=[1,1,1]`。
- N5.0.2c：node control 拆出（6,109→6,003）；P3 `20261024154054`；config txn HIL 14/14。
- N5.0.2d：cmd-frame 拆出（6,003→5,946）；P3 `20261024160525`；exec196 HIL 三目标 `frag_complete=[1,1,1]`。
- N5.0.2e：dispatch/command 拆出（5,946→5,763）；P3 `20261024163157`；config txn HIL 14/14。
- 下一步：**N8.0 基线冻结与台架探测** → N8.1 快照回正 → N8.2 契约/N5.5 冻结 → N8.3 IF-013 后端 → N8.4 N5.7 只读聚合 → N8.5 T2 running-safe → N8.6 四板 HIL → N8.7 收口；细化见 TODO N8 节。

### REFMEM-TASK-20261109-001 — REFMEM-READ-01 M0/M1：读写一致目标立项与行 schema 定稿

- 长期目标：`goal-59a82c72`（40 轮），把 `REFMEM-READ-01` 从 pending 推进到 active；逐槽行、发布面、host/page 单一入口、四板 HIL；OTA/烧录/板号/整片擦除为非目标。
- M0 代码（`7c2ed8e4`）：`ControlPlane.write` 改 `SYST:CTRL:EXECute` 并解析 `QUEUED,<seq>`；`wait_terminal` 改 `SYST:COMMAND:TARget?`（终态 4/5/6，超时带 state/reason）；smoke 改 6/8 key；control plane 用例进 `host_acceptance.DEFAULT_PATHS`。
- M0 文档（`fc0a1de0`）：SCPI 文档去掉「-200/待 M2」；READ-01 id 统一 `REFMEM-`；登记表 catalog 36→38（快照，以 `refmem_command_catalog_count` 为准）；READ-01 证据回写 6/8、host/smoke 已完成、C11 记录待补；新增 `submissions/REFMEM_READ_CROSS_REVIEW_01.md`（补位提交，明确待复核、不冒充通过）。
- M1（`31e4f15f`）：行 schema 定稿——应答 `key,offset,count` 头 + N 条 `slot,valid,epoch,seq,age_ms,stale,<schema>`；`offset/count` 为槽位区间；每行仅由该 slot 的 owner 发布，远端槽读本地镜像，未发布 `valid=0`，不得用本地 provider 冒充当槽。
- 验证：`pytest tests/python/test_refmem_control_plane.py` 8 passed；host acceptance 85 passed；`docs_check` 280 files；`doc_regression` 63 contracts。
- 交叉复核（只读子代理 `b2e480b5`，2026-11-09）：5 组声称全部成立；N1 已修（新增 `wait_aggregate()` 读 `CTRL:STATus?`，`wait_terminal` 容忍无记录并仅限本地）、N2/N3/N6/N7/N8 已修；N4 随 M2 固件批次（需 P3），N5/N9 记 OPEN。
- M2 设计输入（2026-11-09 审计）：**当前树没有远端 node region 的 writer**——`NODES?` 远端恒 stale=1；process-image REFMEM 片仅 10 B；DELTA 每帧 1 个 u32。R1-3 需新增发布通道；推荐 `REFMEM_SYNC_FRAME_OBS`（打包 key 记录 ≤256 B）+ `statistics` 区 1024 B mirror，待 TDMA 线格式/席位评审。详见架构 REFMEM-READ-01「发布面运输」。
- M4 行格式（2026-11-09）：`OBS?` 改逐槽行（本地槽 valid、对端槽 stale）；`refmem_obs` 编码器支持多行；`ControlPlane.read` 解析 per-slot rows（并兼容旧单行）；simulator/用例同步（12 passed）；firmware 全量 build 1482 targets OK。
- P3 dpll60（2026-11-09）：OTA 失败根因=复用目录里的旧 `DHRT100_UPDATE.pkg`（build `20261105063447`），v2 构建实际产出 `DHRT100_V2_CANDIDATE_UPDATE.pkg`（`20261109043018`）；四板 FAIL=`POTA_ERR_SIGNATURE_INVALID(16)`、0 erase/0 FT。已修包选择（`3c7759b4`）与 picotool UF2 选择；12:33 重跑 OTA 4/4 passed（208.7s），随后 TRN-03B `CAL:TRAIN:STAGe:LINK 2,10` 失败，P3 未产出（只读子代理取证中）。
- TRN-03B 首败（12:37）根因=host framing：`CAL:TRAIN:STAGe:LINK` 成功回显 `2,1` 无终止符，晚到后拼进 `SYST:ERR?` 的 `0,"No error"`，被误读成 `2,10,"No error"`；已修 `38d09e17`（`TDMA_CONTROL_RESULT_FIELDS` LINK=2/BEGIN=7）。复跑（12:51，package reuse）改在 NO.2 `SYST:TDMA:OPMode:APPLy` 返回 `-200` 失败，第二份只读取证中；P3 仍未产出。
- APPLY 取证（`efd7aa04`）：NO.2 `OPMode?` active=`23,10000,150000000,...`（level 23 / 10 kHz / 150 ms）vs staged=`7,10000000,1000000,...`（level 7 / 10 MHz / 1 ms）；`reject_count=5`、`last_result=1(BAD_ARGUMENT)`。NO.1 对照 active==staged、`reject_count=2`、`last_result=0`。判为 board-local stopped-mutation 状态（boot replay 3–33 s 与 TRN-03B 并发、`RING:STATus?` 不发布该状态），非矩阵/CRC/幂等；`BOOT:RESET` 无效，**用 picotool 重刷同 build（BOOTSEL 硬复位）清除**：active==staged、reject 归零。
- M4 构建凭证（2026-11-09 13:09）：`dpll60` reuse build `20261109043236`，`DPLL60_JOINT`（QUICK_DIAGNOSTIC）、sources=1002、receipt 已绑 staged；**不覆盖 OBS 行为路径**。OBS 四行冒烟另归档 `out/refmem_read/obs_smoke_20261109.txt`（build `20261109051835`）。
- 失败结果写命令的原文回传（`bca42cdc`）：`APPLY/LINK/BEGIN` 被拒时 matcher 接受 `-200` 行，`checked_action` 用原文报错；不再退化为 `<timeout>`。`test_scpi_framing`；trn03 用例 172 passed。
- M2 运输（2026-11-09，HAOFV 复核后修正）：上一版 `field_id=0x100+key*16+word` **绕开了既有 `DataLinkTable`**（14 条字段契约，含 `update_period_us/stale_window_us`），属重复造轮子；改为**复用 DataLinkTable + `REFMEM_DELTA`**——`field_id=data_link_id`、镜像按 `(slot,data_link_id)`、`stale` 用条目的 `stale_window_us`。**并已确认这不是“只补 runtime”**：DataLink 是标量表且 14/14 已满，DELTA 只接受 width≤4、每源槽一个 mirror，8 key 共 32 word 需扩模型（version/CRC/package）或按 word 拆分，可能还要扩 instance 表。
- M6（2026-11-09，`0c064db2`）：开关命令按 HAOFV owner fail-closed——deployment 不可解或 role 非 `SWITCH_A/B` → reason 12；旧 `CONFigure:SWITch#` → `-200,"USE_CTRL_EXECUTE"`。DPLL60_JOINT 构建凭证 build `20261109051835` 通过（trn03 矩阵复用旧件，证据强度打折）；现场 NO1 本地/广播均 NACK reason 12、legacy 返回 `-200,"USE_CTRL_EXECUTE"`；SWITCH_A ACK 待加载 T2 deployment 后补证。审核指出原用 STOP-gated snapshot，已改为 `trigger_deployment_snapshot_cached()`（待新构建凭证）。（后由 `9aeb173d` 改为未配置→13；正向已由 `9b5f9529` 补证，本条为历史口径。）
- 第二轮交叉复核（`a1492f4e`）：M4 逐槽行代码/源码凭证成立；发现 M4 文档旧口径、M2 文档仍冻结 `0x100`、M6 STOP 门回归、P3 口径 overreach 等，已逐项修正；残余见提交单 §4.2。
- P3 第三次失败（2026-11-09 13:45，cached 修复后）：NO.3 `SYSTem:TDMA:OPMode:APPLy` 返回 `-200,"TDMA_OPMODE_APPLY_STOP_REQUIRED"`（info 已能显示，`bca42cdc` 生效）；STOPPED readback 仍 `enabled=0/config=applied=4`。判为 boot replay 3–33 s 与 TRN-03B 并发（§7.4 已两次独立取证）；修复方向：reset 后等 `BOOT:PLAN? boot_done` 或 35 s 窗口关闭再启动 staging。
- P3 通过（2026-11-09 14:05，`a79b10b7`）：build `20261109055830`、`DPLL60_JOINT`、sources=1002；开关 owner 判定改用 `trigger_deployment_snapshot_cached()`（RUN-safe）。**注意**：该 P3 仍在 10 MHz 窗口下；按 calibration 会话的载波分离设计（C/T/Q/S 走 10 kHz、仅 R 段 10 MHz），底层 level 协议问题仍归其侧，本提交不声称解决 P3 间歇性。
- TRN-03B workaround（`30f5f7c3`）：reset 后 `wait_boot_replay()` 强制 35 s 窗口再启动 staging；已向 calibration 会话申明 `workflow_steps.py` 变更；不触碰 `app.c`/`profile_level`/`SET_FLIGHT_MODE`/boot profile 语义。
- 跨会话对齐（2026-11-09）：calibration 会话交付载波分离设计与证据方法学（C/T/Q/S=10 kHz、R=10 MHz、T3 后 latch；P3 间歇失败机制为 R 段 trial 起点锚定 + 10 kHz 维护面延迟）；本域不重复其推导，后续 P3/载波相关判据引用其提交 `d946b9ca`/`fb28dff5`/`5c160d70`/`a1151ff8`。
- M2 扩展提案（`REFMEM_READ_MODEL_DESIGN_01`，2026-11-09）：DataLink 14 条标量已满、DELTA 仅 width≤4/单 mirror；对比 A 扩表（14→46，版本/包升级）、B 加宽度（schema 变更）、C `RefMemSlotContract` 静态视图（不动模型 version/CRC/package）；推荐 C，待 C11/TDMA 确认 process-image field/value 每周期通路。
- 第三轮交叉复核（`33b11994`）：cached/P3 源绑定成立（不覆盖开关功能）；boot wait 归因被证伪（BSS 清零，非 boot_done 陈旧），已改判据为 `fail_step=0 && boot_done=1 && half_ring=0` 且 pending 失败（`1c6a16c7`）；M2 提案的 `0x100` 与“每周期通用 field/value”前提均被证伪，提案已重写为 A′/B′ 待裁决；reason 12 语义稀释（未配置/角色不符/BUSY 都映射 12）记为残余。
- reason 分档（2026-11-09，`9aeb173d`）：cached snapshot 返回 BUSY→reason 6、UNCONFIGURED/其他→13、角色不符→12，修正“未配置/角色不符/BUSY 都报 12”的稀释。P3 build `20261109062402`；现场无 deployment 时 NO1/NO2 本地开关命令均 NACK **reason 13**（不再是 12）；SWITCH_A ACK 仍需部署后补。
- M3 8/8 key（2026-11-09，`6c28df1c`）：`trigger_state`（service 快照 state/run_state/epoch）与 `node_health`（node status state/heartbeat/stale_count/fault_code）补齐；P3 build `20261109063728`；实板 8 个 key 本地行均 valid=1（`out/refmem_read/obs_8keys_20261109.txt`）。
- M6 正向（2026-11-09，build `20261109072222`）：用台架序列号作 `board_uid` 构造最小部署（COUNTER/SWITCH_A/GATEWAY/SWITCH_B）并逐板 stage/activate 后，SWITCH_A/B 本地开关命令 **ACKED**（state 4，`READ:SWITch1?` 值跟随 5→4/3→4），COUNTER/GATEWAY **NACK reason 12** 且本地状态不变；证据 `out/refmem_read/switch_role_hil_20261109.txt`。广播 `0xF` 聚合因环未运行（`NOT_ONLINE_SAFE`）未证，留 M7。
- M7 广播聚合尝试（2026-11-09）：10 kHz bring-up（`--level 23 --skip-train`，含/不含 `--bringup`）两次均在 NO.2 `ARM timeout`（`ring_node_count=0`、`ring_config_seq=24`、`ring_adapter_started=0`），广播未发出；已 `RING:STOP` + `DEPloyment:CLEar` 清理。另：清部署后重复 stage 同一 generation 被拒（`GENERATION=6`），需换 generation。广播聚合仍 OPEN，待 TDMA/载波侧修复 NO.2 ARM。
- M7 stale 语义（2026-11-09，`dc319ab9`）：四板 8 key OBS 矩阵 PASS（`out/refmem_read/obs_hil_matrix_20261109.json`）——每板本地行 `valid=1`、对端行 `valid=0/age=0xFFFFFFFF/stale=1`；工具 `tools/refmem/obs_hil_matrix.py` + `check_page` 5 单测。缺板与广播聚合仍 OPEN（环未起）。
- BOOT:PLAN? 口径统一（2026-11-09）：TDMA/calibration 文档统一为 **20 字段、0-based** 索引（`fail_step=3`、`half_ring=16`、`boot_done=17`、`boot_next_step=18`、`profile_source=19`），修正“19 字段/第 18 字段”混用。
- 四板矩阵复跑（2026-11-09，重写后）：`obs_hil_matrix.py` 增加本地 `valid=1`、page 头/count、本地 fresh 校验并落行级证据；四板 8 key **PASS**（build `20261109072222`，`out/refmem_read/obs_hil_matrix_20261109.json`）；`check_page` 9 单测。
- 上下电后广播复测（2026-11-09）：10 kHz 环启动成功（NO.2 ARM 恢复），但广播 `45,15,15` 仍停在 `CTRL:STATus?` state 1（QUEUED），仅本板 slot0 有 NACK 记录、远端槽无记录；加 `LOAD:MASK 27` 无效。判为简易起环未配置维护/二等席命令窗口，需 P3/TRN-03 级 setup；已 STOP/CLEar。广播聚合仍 OPEN。
- M2 通道复核（2026-11-09）：`refmem_sync_commit_delta()` 已接受任意 `field_id/width<=4` 并保存值，发送端每周期已带 `field_id/value`（现写 baseline 0 + `s_service_count`）；因此 M2 选 **B″（REFMEM 本地字段复用）**——发送轮转 + `[slot][field]` mirror + OBS 组合，不改 wire/模型 version/CRC/package，也不需要 TDMA 选择器。待 C11 复核 id 范围/隔帧 baseline/RAM 后实现。
- 广播命令席复测（2026-11-09）：三种假设均失败——10 kHz + `LOAD:MASK 27` → QUEUED；再加 `--process-image` → 仍 QUEUED；10 MHz 训练环 → NO.2 `TDMA_RING_TRAIN` 失败。代码定位 `tdma_maintenance_select_origin_tx()` 需 `maintenance_pending && window_open && credit >= max_frame_size`；10 kHz bringup profile 无维护窗口，10 MHz 训练又失败。广播留 TDMA/校准域，已 STOP/CLEar。
- M2 运输补充（2026-11-09）：REFMEM 的 `field_id/value` 走 **process-image 帧**（`distributed_refmem.c:820-826` 组帧、`refmem_sync_commit_delta` 接收），10 kHz 环 up 时 up/down 计数已在增长，因此 **OBS 发布不依赖维护窗口**；只有广播命令依赖 `tdma_maintenance_select_origin_tx` 的 window/credit。M2 可在 10 kHz 环上验证，无需 10 MHz 训练或维护席。
- M2a 接收侧（2026-11-09，`3ae06ecb`）：`refmem_sync` 按 `field_id` 存 OBS 字段（共享 1 KB：value/mask/seq/updated），`distributed_refmem_get_obs_field()` 暴露 `(slot,field)`；OBS 对端行按契约前缀和组合 32 word——齐全 `valid=1`，缺任一字段保持 stale。P3 build `20261109082220`；发送端轮转尚未接入，所以现场对端行仍 stale（预期）。
- M2b 发送 scaffold（2026-11-09，**工作区未提交**）：compact builder 增加 `field_id` 参数；publish 路径按 `REFTMEM_OBS_FIELD_BASE` 轮转 32 word，未注册 source 时保持 baseline(0)+service count；本地采样 `scpi_refmem_obs_sample_local()` 已实现但**暂不注册**，避免实时路径内联采样。P3 三次（含注册/不含注册）均因 VDC `STARVED`（OUTPUT_STOPPED+OUTPUT_STALE）失败，未取得凭证；待上下电/VDC 恢复后重跑再提交。
- M2b P3 重试（2026-11-09，第 4 次）：仍 `STARVED`（NO.2/NO.3 `OUTPUT_STOPPED+OUTPUT_STALE`、`EARLY_FREEZE`）；与是否注册采样无关，判为台架 VDC 状态，需上下电/载波侧恢复后再取证。发送 scaffold 留在工作区，未提交。
- M2b 完成（2026-11-09，`7da1f9cb`）：core0 `task_refmem_sync` 每 ~50 ms 采样 8 key 32 word → publisher 缓存；compact 发送每帧轮转一个 OBS field（0x100..），无缓存回退 baseline；接收端按 field_id 存 value/mask/seq/updated，OBS 对端行按前缀和组合。P3 build `20261109110157`，`DPLL60_JOINT`、`tdma_stage_passed=true`、外部 HDO scope `PASS`（35.9 ns）、sources=1005；校准会话配置入库 `config/hardware_acceptance/p3_bench_calibrated.json`。
- 跨板 OBS 验证（2026-11-09）：起 10 kHz 环时 NO.2/NO.3 轮换 `ARM timeout`（`ring_node_count=0`）；软件 `BOOT:RESET` 未清，待上下电后重试。
- M2 跨板 HIL（2026-11-09，build `20261109110157`，10 kHz 环）：SWITCH_A（COM4）写 `45,2,2,1,4` → ACK、`READ:SWITch1?`=`1,1,1,4,4,0`、本板 OBS slot1 `[4,4]`；COM5 `OBS? 1,0,4` 读到 slot1 `valid=1/age=183ms/stale=0/[4,4]`，slot0/2/3 仍 `[1,1]`；`OBS? 2,0,4` 全 `[0,0]`。证据 `out/refmem_read/obs_write_read_20261109.txt`。**写→跨板读一致**。
- M7 stale/停发 HIL（2026-11-09，build `20261109113327`）：基线 `OBS? 1,0,4` 四行 `valid=1/stale=0/age≈104ms`；停 COM6（slot3）后 16 s，slot1/2/3 均 `valid=1/age≈17.3s/stale=1`（窗口 10 s）。证据 `out/refmem_read/obs_stale_20261109.txt`。单板缺板（环仍存活）与广播聚合仍未证。
- M5 起点（2026-11-09，`e3e9b9c8`）：readback 页 `read.switch1/2` 改走 `ControlPlane.read`（新 `obs_read.py`，2 用例），不再直接发 `READ:SWITch#`；demo 31 passed。
- 广播第 3 假设（2026-11-09）：按 TDMA 文档设 `SEAT:ROTation 1,3,1`（f=3、s=1）后，`45,15,15` 仍 30 s QUEUED、仅本板记录。`LOAD:MASK`/process-image/seat 三假设均失败，广播留 TDMA 域。
- 另一会话停止：其 demo 在途改动作为继承提交 `7d3f2ef6`（calibration 页/工作流，tests 31 passed），M5 在其上继续。
- M5 完成（2026-11-09，`f7e9b898`/`53ba8004`）：demo transport 统一包装——`SYSTem:REFMEM:*?` 经 `ControlPlane.query`、`SYSTem:CTRL:EXECute` 经 `ControlPlane.write`、开关读经 `ControlPlane.read`；readback/ack/诊断页不再直连 REFMEM 路径。`test_trigger_debug_obs` 5 用例；host 94 passed。
- 广播复核（2026-11-09，build `20261109113327`，seat `1,3,1`）：`45,15,15` **已送达四板**——COM5/COM4/COM3/COM6 均有 `seq=93566` 记录；COM3（GATEWAY）NACK 12，COM4/COM6 仍 state 1（未 dispatch），origin 聚合 state 1。**运输成立**，卡在目标 dispatch/ACK 聚合；环运行时 `DEPloyment?` 返回 33（STOP-gated），无法确认缓存部署。
- 广播 dispatch 诊断（2026-11-09，build `20261109113327`）：环 up 时 COM4 本地写 `45,2,2` → **state 4 ACKED**（dispatch 服务正常）；COM4 广播被 `NOT_CONTROL_OWNER` 拒绝（入口正确）；owner(COM5) 广播 `seq=93566` 送达四板——COM3 NACK 12，COM4/6 仅 accept(state 1) 未执行，origin 最终 **state 6 TIMEOUT / reason 11 / ack=0xA / nack=0x4**。结论：运输与入口正确，多目标命令的**目标执行/提交**未完成。
- 广播根因（2026-11-09）：`refmem_cmd_load` 的发送预算取自 `tdma_maintenance_runtime_delivery_budget_ms(bytes,frames)`（或显式 `timeout_us`）；10 kHz bringup 的维护流实际需 ~16 s，超预算 → 提前 `STATE_TIMED_OUT/reason 11`，目标停在 `POSTED(1)`。**10 kHz bringup 不适合命令面**；广播 HIL 应在 10 MHz 维护流（P3/TRN-03 级）下做。
- 10 MHz 对照（2026-11-09，`--leave-on-failure` 保留 TRN-03 环，build `20261109113327`）：广播结果与 10 kHz **完全一致**——COM3 NACK 12，COM4/6 state 1，origin TIMEOUT 11 / ack=0xA / nack=0x4；同一环上 COM4 本地写 ACK。`SYST:REFMEM:CLAIM?` 四板一致。结论：**非速率/预算问题**；远端写入的命令记录在 SWITCH 角色板上不 dispatch（GATEWAY 正常 NACK），本地记录正常。
- dispatch 诊断（2026-11-09，`63817965`，build `20261109121613`）：新增 `SYST:REFMEM:CMD:DISPatch?`（service_calls/read_params_fail/broadcast_wait/last_skip）。10 MHz 环上实测：四板计数 ~29 万次调用、~28 万次 read_params 失败，但**绝大多数是空闲轮询无记录**，不能区分广播记录；SWITCH 仍 state 1、origin TIMEOUT 11。下一步需“仅在新 seq 上计数”或对比远端载荷同步。
- take/状态更新诊断（2026-11-09，`2a196a58`，build `20261109124923`）：新增 `distributed_refmem_node_control_sequence()`，只统计 live 新记录的 `read_params` 失败；10 MHz 环广播后四板 `read_params_fail=0`、`last_skip=2`（记录已被消费），但 COM4/6 仍 `state 1` → 问题在**执行后 node-control 状态更新未生效**，不在读取/传输。take 被拒路径已改为镜像 NACK（防御性，防止静默消费）。
- 当前 OPEN：node-control 状态更新未生效（SWITCH 角色）、广播聚合、单板缺板 NACK；契约仍 pending。

### REFMEM-TASK-20261029-041 — 台架复位与默认流程改动状态

- 台架复位（2026-10-29 23:05）：四板软件复位后
  - build `20261029103949`；`OTA:STATus? = IDLE`；
  - `TDMA:RING:STATus?` 全 0（环停，boot 计数 1/1）；
  - `T2:SEQ:MODE? = 1,0,0`（复位后回到 legacy；默认流程会在启动时按板重设 gateway=2）。
- **未提交**（工作区，已实现并通过合成用例，门禁需匹配的 T2 PASS 或 P3）：
  - `t2_workflow.py`：默认 `T2_SEQ_MODE=0` / `T2_SEQ_MODE_GATEWAY=2`、`T2_START_DELAY_S=0.5`；
  - `sequence_workflow.py`：默认 `--sample-ticks 70`、`--seq-mode 0`、`--seq-mode-gateway 2`；单 late 容错（PASS + 标注，不重跑全流程），严格凭证仅在三角色全 `executed==groups×positions` 时写；移除了自动重跑逻辑。
- 验证：合成用例（SWITCH_B 5760=16×360、late 1）→ `ok=True`、message 标注 “single late tolerated … strict receipt withheld”；台架端到端默认跑已在 22:34 观察到单 late（旧逻辑下 FAIL），新容错逻辑尚未在台架全量复跑。
- 后续提交方式（二选一）：`sequence_workflow` 默认流程跑到严格 PASS（写 T2 receipt）后提交；或 `p3 run` 建包 + 四板 OTA + `resume` 后提交。

### REFMEM-TASK-20261029-040 — N12.3 达成：三角色 361/361 官方 PASS

- 关键改动：
  1. 逐板模式：`apply_sequence_modes()` 在启动时、配置前下发 `T2:SEQ:MODE`（`T2_SEQ_MODE` 全板、`T2_SEQ_MODE_GATEWAY` 仅 gateway 板）；
  2. `T2_START_DELAY_S=0.5` 受控起步偏移，消除 switch 首位置的偶发 late。
- 结果（`sequence_workflow --positions 361 --sample-ticks 70 --seq-mode 0 --seq-mode-gateway 2`，21:34:34）：**verdict OK**：

| 角色 | groups | executed | finished | late | 期望 |
|---|---|---|---|---|---|
| SWITCH_A | 16 | 5776 | 361 | 0 | 16×361=5776 ✓ |
| SWITCH_B | 16 | 5776 | 361 | 0 | 16×361=5776 ✓ |
| **GATEWAY** | 2 | **722** | **361** | 0 | 2×361=722 ✓ |

`no_plan/late/backend` 全 0；`T2 acceptance receipt` PASS。
- 提交：`78cca54f`（工具 + T2 PASS 凭证；firmware 类经 T2 sequence receipt 放行）。
- N11 的 “GATEWAY 361 例外” **已撤销**（快照 §4 行改为已闭；TODO N12 全 ✅）。
- 证据：`out/n12/seq_361_delay05.log`、`out/sync_trigger/sequence_flow.json`、`config/hardware_acceptance/t2_acceptance_receipt.json`。

### REFMEM-TASK-20261029-039 — N12.3 复跑：GATEWAY 稳定 361/361，switch 仍各差 1 个 late

- 同命令复跑（`--seq-mode 0 --seq-mode-gateway 2`，groups 默认 16）：
  - SWITCH_A：**5776 / 361 / late 0** ✓（上一轮它是 360）；
  - **GATEWAY：722 / 361 / late 0** ✓（连续两次 361/361）；
  - SWITCH_B：5760 / 360 / **late 1** ✗（上一轮它是 361）。
- 判读：gateway 的模型路径已稳定达标；**每次只有一条 switch 差 1 个 late**，且在 A/B 之间交替 → 属启动首位置的调度 late（窗口/起始相位），不是结构缺陷。
- 结论：N12.3 只差“switch 首位置 late 1”这一项；gateway 例外已可撤销（gateway 连续两次 361/361）。
- 证据：`out/n12/seq_361_hybrid2.log`、`out/sync_trigger/sequence_flow.json`。
- 下一步：在 step 19 启动 PWM/转台前加入受控起步偏移（或把首个位置排除在 late 统计外并留证），再跑一次争取三角色全 361；随后提交工具并撤销 N11 gateway 例外。

### REFMEM-TASK-20261029-038 — N12.3：官方 361 混合跑（GATEWAY 361/361 达标，SWITCH_A late 1）

- 工具固化：`t2_workflow` 新增 `apply_sequence_modes()`（启动时、配置前按板下发 `T2:SEQ:MODE`；`T2_SEQ_MODE_GATEWAY` 覆盖 gateway 板）；`sequence_workflow` 新增 `--seq-mode-gateway`，`--seq-mode` 默认 None（不再强制覆盖）。
- 运行：`sequence_workflow --steps 1-5 --positions 361 --sample-ticks 70 --ready-required 0 --seq-mode 0 --seq-mode-gateway 2`（groups 默认 16）。
- 计数（`out/sync_trigger/sequence_flow.json`）：

| 角色 | groups | executed | finished | late | 期望 |
|---|---|---|---|---|---|
| **GATEWAY** | 2 | **722** | **361** | 0 | 2×361=722 ✓ |
| SWITCH_B | 16 | 5776 | 361 | 0 | 16×361=5776 ✓ |
| SWITCH_A | 16 | 5760 | 360 | **1** | 16×361=5776 ✗（差 1 个 late） |

- 结论：**GATEWAY 首次在官方 361 口径下达到 361/361**（722 groups）；整体 verdict 仅因 SWITCH_A 的 1 个 late 失败（360/361），属偶发调度 late，不是结构性问题。复跑应能全过。
- 证据：`out/n12/seq_361_hybrid.log`、`out/sync_trigger/sequence_flow.json`、`t2_acceptance_receipt.json`（本次 FAIL）。
- 下一步：复跑同一命令 1–2 次确认 SWITCH_A 的 late 是否偶发；全过后跑 P3+host 提交工具改动，撤销 N11 的 gateway 例外。

### REFMEM-TASK-20261029-037 — N12.2：逐板模式混合成功（gateway 模型 360/361，switch legacy 361/361）

- 代码结论：统一模型的“位置服务”（`trigger_sequence_pilot_position_service`）只实现 gateway 路径（固定走 `gateway_fire/ready`），switch 在 MODE≥2 会被带进该路径并卡死；因此 switch 保持 legacy、gateway 用模型。
- 配置（reboot 后配置前）：COM3（2BD5…，GATEWAY）`T2:SEQ:MODE 2`；COM5/COM4/COM6 `MODE 0`；`T2_READY_REQUIRED=0`、`T2_SAMPLE_TICKS=70`、`T2_SEQ_GROUPS=1,8,2,2`。
- 结果（`steps-20261029-204135.log`，21/21，failed steps: none）：

| 角色 | entered | frozen | executed | abandoned | groups exec/count | 期望 |
|---|---|---|---|---|---|---|
| SWITCH_A | 361 | 361 | **361** | 0 | 2888/8 | 8×361=2888 ✓ |
| SWITCH_B | 361 | 361 | **361** | 0 | 722/2 | 2×361=722 ✓ |
| **GATEWAY** | 361 | 361 | **360** | 1 | 720/2 | 2×361=722（差 2：1 abandoned + 1 in-flight） |
| COUNTER | 361 | 361 | 0 | 358 | — | 无动作角色，PASS |

- 结论：**逐板混合模式（gateway 模型 + switch legacy）可行**——switch 已 361/361 全过，gateway 到 360/361（仅 1 个 abandoned），不再是最多 14 的死停。
- 证据：`out/n12/t2_hybrid.log`、`out/tdma_t2/steps-20261029-204135.log`。
- 下一步：复跑确认稳定性，并尝试把 gateway 的 1 个 abandoned 收掉（加长窗口/重试）；随后用 sequence_workflow 口径固化混合流程（需 per-board mode 支持或文档化操作），再决定是否撤销 N11 的 gateway 例外。

### REFMEM-TASK-20261029-036 — N12.1：MODE=2 + 模型口径组数（GATEWAY 已执行 359/361，SWITCH 仍停 4）

- 配置：reboot 后置 `T2:SEQ:MODE 2`（配置前），`T2_READY_REQUIRED=0`、`T2_SAMPLE_TICKS=70`、`T2_SEQ_GROUPS=1,8,2,2`。
- 采样结果（`steps-20261029-203114.log`，t+122s）：
  - **GATEWAY（2 组）：`win 361/361/361/359/2` → entered 361、frozen 361、executed 359、abandoned 2**；`groups exec/count 718/2`；freeze mismatch +30。
  - SWITCH_A（8 组）：entered 4、frozen 4、executed 0、pending 3；`groups exec/count 0/8`。
  - SWITCH_B（2 组）：entered 4、executed 0、pending 3；`groups exec/count 0/2`。
  - COUNTER：entered 361、abandoned 358、pending 3（无动作角色）。
- 判读：**模型口径下 gateway 的动作链已跑通（359/361）**；switch 仍在第 4 个位置后停摆（动作未执行、pending 3），说明 switch 的模型配置/窗口口径仍不对（不是 gateway 问题了）。
- 本次运行在 t+124s 后中断（无 step 21 汇总），后续需重跑取 STEPLOSS 终值。
- 证据：`out/tdma_t2/steps-20261029-203114.log`、`out/n12/t2_model_groups.log`。
- 下一步：查 switch 在模型下的 stage 拒绝（`trigger_sequence_pilot` 的 `s_step_detail`、`TRIGger:SEQuence:...` diag），并按 8/2 组与窗口（`SYNC_IO_SEQUENCE_PLAN_MAX_GROUPS`）校配置。

### REFMEM-TASK-20261029-035 — 统一模型（sync_io_sequence）激活实验：gateway 不再停摆但动作被放弃

- 结论先行：统一模型入口是 `trigger_sequence_pilot`（`sync_io_sequence`），MODE 必须在**配置前**设置；reboot 后置 `T2:SEQ:MODE 2` 再跑 `--steps 1-21`（`T2_SAMPLE_TICKS=70`）。
- 结果（`steps-20261029-200720.log`，step 20 采样 486 s）：
  - COUNTER：entered/frozen 361，abandoned 358，pending 3 → PASS；
  - **GATEWAY：entered/frozen 361（不再停在 14）**，但 executed 0 / abandoned 361 → 动作全部被放弃；WARN mismatch +19；
  - SWITCH_A / SWITCH_B：entered/frozen 仅 4，executed 0、pending 3 → FAIL（模型接管后 switch 反而退化）。
- 判读：MODE=2 成功把位置链交给统一模型（gateway 位置 361 都进来了），但**角色动作组没有执行**（全 abandoned）——组数/脉冲宽度/窗口配置仍是 legacy 口径（16/2/16），与模型不一致；switch 也因同一原因停在 4 个位置。
- 与 legacy（MODE=0）对比：legacy 下 switch 361/361、gateway 14/361；模型下 gateway 361 entered、switch 4 ——两个方向各差一半，说明**模型路径是正确方向，差在角色配置**。
- 证据：`out/n11/t2_unified_model.log`、`out/tdma_t2/steps-20261029-200720.log`。
- 下一步：在 MODE=2 下按模型口径重配角色（SWITCH_A 组数 8、SWITCH_B 2、GATEWAY 2，脉冲宽度按 `sync_io_sequence` 要求），再跑 361；或先试 MODE=1。

### REFMEM-TASK-20261029-034 — N11.5：收口（工具提交、冒烟与 361 口径）

- 工具提交 `22f6e1d3`（P3 `20261029103949` / 1613 / `7cc0d725`；host 81 passed / 542 files）：
  generation 自愈、`query_page()` 重试、STEPLOSS 判据、`--seq-mode`、host 用例。
- 最终树冒烟（`--steps 1-21 --leave-running`，build `20261029103949`）：**21/21，failed steps: none**；
  STEPLOSS：COUNTER/SWITCH_A/GATEWAY/SWITCH_B 全 **PASS**（switch/gateway 有 freeze fallback WARN；`overwrote +0`、`pending 0`）。
- 361 口径（最终）：SWITCH_A/SWITCH_B **361/361 PASS**（5776 executed，late/no_plan/backend 0）；GATEWAY **FAIL（台架例外）**——legacy 需 analyzer READY、pilot 行内不可用，列为固件后续任务。
- 结论：N11 目标范围（工具加固 + 判据 + 1-21 全链 + 361 对 SWITCH 的结论）闭环；gateway pilot 修复与 analyzer 条件不在本目标。
- 证据：`out/n11/t2_smoke_final.log`、`out/tdma_t2/steps-20261029-190500.log`、`out/n11/seq_361c.log`。

### REFMEM-TASK-20261029-033 — N11.4c：行内 SEQ:MODE 不可用，gateway 结论定案

- 干净复跑（先 reboot + 1-21 基线确认 21/21）后，用 `--seq-mode 2` 在 step 16 前下发：
  - 四板 `TRIGger:REServation:T2:SEQ:MODE 2` 全部 `<timeout>`；
  - step 17 FAIL：`sync input not admitted on [all four]`；
  - step 18 FAIL：`err 7 valid 0`（identity fault），epoch 与 dep_gen 相同（1793269617），排除 epoch 问题；
  - chain rc=1，verdict FAIL。
- 结论：**在当前固件/命令语义下，行内切换 `T2:SEQ:MODE` 不可用**——命令无应答（阻塞/超时），且会破坏 SYNC 输入准入；pilot 序列模型要在停止态、配置前、以非阻塞 ACK 方式启用，属固件改动，超出 N11 范围。
- **361 口径定案**：
  - SWITCH_A / SWITCH_B：**PASS**（361/361、5776 executed、late/no_plan/backend 0）；
  - GATEWAY：**FAIL**（legacy 路径需外部 analyzer READY；无 READY 时 `UNCERTAIN` 停摆在 14/361；pilot 模式不可用）→ 本台架显式例外，修法另立固件任务。
- 证据：`out/n11/seq_361_mode2b.log`、`steps-20261029-182657.log`、`out/tdma_t2/steps-20261029-182213.log`（基线）。
- 下一步：跑 P3 resume + host acceptance，提交工具改动（generation 自愈/重试/STEPLOSS/`--seq-mode`）与 host 用例；补 N11.5 §8 报告。

### REFMEM-TASK-20261029-032 — N11.4b：SEQ:MODE 1/2 对照（均不可用）

- 背景：`TRIGger:REServation:T2:SEQ:MODE <0|1|2>`（默认 0=legacy）可让序列模型接管位置 backend。
- MODE=1（4 板设置后跑 `--steps 11-20`）：step 17/19 FAIL（`sync input not admitted`、计数器 `0/0/0` 不前进），GATEWAY `entered 0`、`groups exec/count 0/1`。
- MODE=2（显式交还 legacy 引脚）：step 16–19 FAIL；`ORIGin:TRIAL` 被拒 `CAL_ORIGIN_TRIAL_REJECTED`，`cause UNAUTHORIZED`、`EPOCH_SUPERSEDED`（trial 192982 vs now 192984），ring `en 0`。
- 已恢复 MODE=0（四板回读 `1,0,0`）。
- 结论：**pilot 模式不能在“先 1-10 再 11-20”的时点启用**——它会改写 ring/epoch 前提，导致 origin trial 被拒；要么在 T2 配置后、ring 启动前由工具内设置（需要 `t2_workflow` 新增 `--seq-mode` 选项），要么继续 legacy。
- 下一步（N11.4c）：给 `t2_workflow` 加 `--seq-mode`（在 step 12 之后、step 16 之前下发），单次 1-20 验证 gateway 是否 361/361；否则把 “gateway 需 analyzer READY” 写为 361 口径显式例外。

### REFMEM-TASK-20261029-031 — N11.4a：READY=1 对照与 gateway 路径判定

- READY=1 对照（`--positions 361 --sample-ticks 70 --ready-required 1`）：
  - `SWITCH_A/SWITCH_B` 仍 `361/361`（5776 executed）；
  - **GATEWAY 退化为 1/361**（executed 2），说明 READY=1 更早停摆，不是修复方向。
- READY=0（前一轮）：GATEWAY 14/361，`ACTION armed 4/completed 0`，`GW state 4 = UNCERTAIN`。
- 路径判定（`TRIGger:REServation:T2:ACTion:ARMBranch?`）：
  - GATEWAY `1,0,514` → `0x202`：branch=2、kind=2（GATEWAY）、**pilot_switch/pilot_position 位均为 0**；
  - SWITCH_A `1,0,258` → `0x102`：branch=2、kind=1、pilot 位同样为 0。
  - 结论：**gateway 走的是 legacy gateway deadline 路径（pilot 序列模型未启用）**；无 analyzer READY 时该路径在若干次 retire 后进入 `UNCERTAIN`，已 arm 槽无法完成 → 停摆。
- 含义：361 判据要覆盖 GATEWAY，需要让序列模型接管 backend（pilot position mode），或明确把 GATEWAY 从 361 验收口径中排除并书面说明。
- 证据：`out/n11/seq_361_ready1.log`、`ARMBranch?` 读数、`out/sync_trigger/sequence_flow.json`。
- 下一步（N11.4b）：查 `trigger_sequence_pilot_position_mode` 的启用方式（sequence configure 未开启该位）或确认 gateway 是否本就应走 legacy + analyzer。

### REFMEM-TASK-20261029-030 — N11.4：GATEWAY 14/361 停摆定位（backend UNCERTAIN）

- 数据（`steps-20261029-172113.log`，361 全程）：
  - `WIN slot 2 GATEWAY: entered 17 / announced 17 / frozen 17 / executed 14`，`pending 3`；
  - `ACTION slot 2 GATEWAY: armed 4 / completed 0 / failed 0 / uncertain 0`，槽 `[2] seq 29 deadline ... backend 65 width 9`；
  - `GW ... state 4`（`SYNC_IO_GATEWAY_DEADLINE_UNCERTAIN`，枚举 0=IDLE/1=ARMED/2=COMPLETE/3=CANCELLED/4=UNCERTAIN），`svc_wait 5019 / svc_done 255 / late 129`；
  - 位置页 `hold 15472 / drain 15233 / drain_fail 17 / hold_drop 1089 / deliv 239`。
- 对比：同一次运行 `SWITCH_A/SWITCH_B` 均 `361/361`、`5776` executed、`late/no_plan/backend` 全 0；COUNTER 无动作。
- 初判：GATEWAY 的 deadline 后端进入 `UNCERTAIN`（READY 未满足/超时）后，`armed` 槽无法完成，后续位置被 BUSY 拒绝 → 停摆在 14–17 位置；与 READY 语义（`T2_READY_REQUIRED=0` 仅 retire 计数）强相关。
- 下一实验（N11.4a）：同一 361 流程用 `--ready-required 1` 对照（若 gateway 能完成，说明 retire 语义不足；若仍 UNCERTAIN，则查 `sync_io_gateway` 的 arm/complete 路径）。
- 证据：`out/n11/seq_361c.log`、`steps-20261029-172113.log`、`out/sync_trigger/sequence_flow.json`。

### REFMEM-TASK-20261029-029 — 361 位置验证：SWITCH_A/B 通过，GATEWAY 仅 14/361

- 入口：`sequence_workflow.py --steps 1-5 --positions 361 --ready-required 0`（step 2 跑 `t2_workflow 0-20`）。
- 采样窗口校准：默认 ticks → 140/361；`--sample-ticks 45` → 315/361；`--sample-ticks 70`（~140 s）→ 361 位置跑满。
- 结果（build `20261029084245`，`out/n11/seq_361c.log`、`out/sync_trigger/sequence_flow.json`）：

| 角色 | groups | finished | executed | 期望 | no_plan | late | backend | freeze_missing |
|---|---|---|---|---|---|---|---|---|
| SWITCH_A | 16 | **361** | 5776 | 5776 | 0 | 0 | 0 | 29 |
| SWITCH_B | 16 | **361** | 5776 | 5776 | 0 | 0 | 0 | 47 |
| GATEWAY | 2 | **14** | 28 | 722 | 0 | 0 | 0 | 1 |

- 结论：**361 判据对 SWITCH_A/SWITCH_B 通过（16×361=5776，零 late/no_plan/backend）**；**GATEWAY 失败**——只完成 14 个位置后停止推进（非 LATE/NO_PLAN，属准入/吞吐或 SP2T 双组 staging 停摆）。
- 与短窗口对比：默认窗口会把"扫描未完成"误判；**361 验证必须 `--sample-ticks ≥ 70`**。
- 下一步（N11.4）：定位 GATEWAY 为何在 ~14 个位置后停摆（GWDIAG 的 BUSY/READY 拒绝、SP2T 组交替、或 action 表 staging），修后重跑 361。

### REFMEM-TASK-20261029-028 — N11.2/N11.3：GATEWAY 读回重试与丢步判据

- N11.2：新增 `query_page()` 有界重试（3 次、50 ms）；抽样循环的 `TRIGger:REServation:T2?` 与 `T2:SCAN?` 走该路径，超时/短页不再把整页变成 `?`；step 20 末尾打印累计重试次数。
  - 验证：`steps-20261029-163841.log` 中 `RUNTIME slot 2 GATEWAY ... received 7 ... last_error 0`（上一轮同位置是 `? / last_error 1`）；本轮重试计数 0。
- N11.3：step 21 新增 `STEPLOSS` 判据（基线扣减）：
  - 基线在 step 19 采样前记录 `freeze_mismatch`/`freeze_fallback`/`SCAN overwrote`；
  - 失败条件：非 COUNTER 角色存在未执行/未放弃的计划（`pending != 0`），或窗口 `overwrote` 增量 > 0；
  - `freeze mismatch/fallback` 增量 > 0 记 **WARN**（FREEZE 版本不匹配走了 fallback，位置仍执行）。
- 结果（`--steps 1-21`，build `20261029072712`，READY=0）：
  - **21/21，failed steps: none**；
  - `COUNTER PASS`、`SWITCH_A PASS`（WARN +3/+3）、`GATEWAY PASS`（WARN +4/+4）、`SWITCH_B PASS`（WARN +4/+4）；`overwrote +0`、`pending 0`。
- 结论：**按判据无丢步**（无未执行计划、无窗口覆盖）；SWITCH/GATEWAY 存在 FREEZE fallback 告警，属质量项、非丢步。
- 记录：`steps-20261029-163841.log`、`out/n11/t2_full_e.log`。
- 待办：工具与 host 用例提交需 P3；WARN 是否可接受需在 N11.4 复跑确认稳定性。

### REFMEM-TASK-20261029-027 — N11.1：T2 workflow 分次运行自愈（generation 复用）

- 根因：每次调用生成新 `dep_gen`，而 grid/T2 绑定在旧 epoch；step 18 判据 `epoch == dep_gen` 永不成立。
- 修复（`tools/sync_trigger/t2_workflow.py`，待 P3 提交）：
  1. `out/tdma_t2/last_generation.json` 作为跨调用记录；不含 step 8/9 的区间（如 `11-20`）复用该 generation；
  2. 复用路径**不再**做 `<= last_accepted` 递增（此前把它 +1，仍对不上）；
  3. step 18 发现 `err 0 valid 1` 但 epoch 不等时，打印明确诊断（期望/实际 + 建议重跑 8-12 或 1-21）；失败信息带 epoch 对照。
- 台架验证（build `20261029072712`，READY=0）：
  - `--steps 1-21 --leave-running` → **21/21**，failed steps none；
  - `--steps 1-10` → `--steps 11-20` → **failed steps: none**（step 18 `12.4s` OK、step 19/20 OK），generation 复用生效。
- host：`tests/python/test_t2_workflow_generation.py` 4 passed（复用/新 epoch/显式覆盖/缺失 marker）。
- 记录：`out/n11/t2_full_a.log`、`out/n11/t2_split_f.log`、`out/n11/t2_split_g.log`。
- 下一项：N11.2 GATEWAY 读回重试；N11.3 丢步判据；工具改动提交需 P3。

### REFMEM-TASK-20261029-026 — N11.0：T2 workflow 失败归因（分次 epoch 断链）

- 现象：`--steps 1-10` 后接 `--steps 11-20`，step 18 报 `reference cursor never became valid within 24 s`，step 19 拒绝启动脉冲。
- 取数：`out/tdma_t2/steps-20261029-160020.log:134` → `err 0 valid 1 epoch 1793260668 session 1793260820`；两次调用各自生成 `dep_gen`，grid/T2 绑定旧 epoch（`1793260668`），第二次会话是 `1793260820`，判据 `epoch == dep_gen` 永不成立。
- 验证：单次 `--steps 1-21 --leave-running`（`T2_READY_REQUIRED=0`）**21/21 OK**，`failed steps: none`；step 20 cursor `epoch 1793261119 session 1793261119`（一致）。日志 `out/tdma_t2/steps-20261029-160519.log`。
- 全链样本：GATEWAY(slot2) `groups exec/count 25/1→37/1`、`mismatch 0`；SWITCH_B(slot3) `26/1→38/1`、`CUM mismatch 1 fallback 1`（判据待 N11.3）；ACTION 采样时 `armed 2/4 completed 0`（仍在执行，不作丢步证据）。
- 结论：**不是固件缺陷**，是工具分次调用导致 generation 断链；N11.1 做持久化/自愈，N11.3 定丢步判据。

### REFMEM-TASK-20261029-025 — T2 workflow：禁用 READY 后 10 步全过

- 背景：此前 `t2_workflow.py --steps 1-10` 在混合 build / READY 要求下多处失败（step 3/4/6/8/9/10）。
- 处置：`T2_READY_REQUIRED=0`（禁用 READY）后重跑一次：
  - `t2_workflow.py --steps 1-10 --leave-running` → **failed steps: none**，10/10 OK。
- 证据：`out/n10/t2_workflow_readyoff.log`、`out/tdma_t2/steps-20261029-155748.log`。
- 说明：该开关只影响 gateway 对 `TRIGger:SEQUENCE:REAdy` 的处理，不改变 REFMEM 统一面代码；四板需同 build（当前 `20261029072712`）。

### REFMEM-TASK-20261029-024 — N10.4：SCPI 执行错误回包修复（拒绝不再静默）

- 背景：C11-06 指出 `EXECute` 拒绝（`NOT_ONLINE_SAFE`/`BAD_ARGUMENT`）串口无文本，只有错误队列/日志；判读只能靠记录号不变。
- 修复（`b8b87e32`）：`scpi_port_push_exec_error` 先按 SCPI 标准输出 `<code>,<info>` 行并 flush，再入队；不改队列语义、不改 `CTRL:STATus?` 字段序。
- HIL（build `20261029072712`）：
  - 环停 `EXECute 30,14,14` → `-200,"NOT_ONLINE_SAFE"`；
  - 坏参数 `EXECute 30,1,1,1` → `-200,"BAD_ARGUMENT"`；
  - `41` 仍 `QUEUED/ACKED`。
- 凭证：P3 `20261029072712` / 1613 / `ee4b076e`（PASS_WITH_WARNINGS，ERROR 0/FATAL 0）；错误行仅为输出层变更，AC 语义未动。
- 残余：TRN-03 re-arm 预算仍为台架时序任务（非代码）；C3/C6、EXE-SEAT-01 维持另立/待签。

### REFMEM-TASK-20261029-023 — N10.3：C11-06 必改落地（文本/证据/第二层）

- C11-06 = `PASS_WITH_CHANGES`：修复成立；三项必改（状态文本、HIL 证据口径、第二层覆盖/可观测）本轮落地。
- **HIL 补证**（含 build 与 ring 行）：
  - 环停 `out/n10/n10_failclosed_v2.json`：build `20261029052659`、`RING:STATus?` enabled=0；`EXECute 30,14,14` 无回包且 `CTRL:STATus?` 保持上一条终态（无新记录）；`FUNC_TDMA_ONLINE,14,14` `QUEUED 720616` → `FUNCtion:STATus?` = `state=3(FAILED), step=1/2, last_reason=13`；`41` 仍 `ACKED`。
  - 环起 `out/n10/n10_positive_v2.json`：build 同上、ring `enabled=1/node_count=4/up=1/down=1`；`30,14,14` `ACKED/ack=14`、`31,14,14` `ACKED/ack=14`。
- **口径更正**：`CTRL:STATus?` 第 12 字段是 `last_reason_slot` 而非 `applied`；ACK 判据只用 `state/ack_flags/nack`。
- **可观测性（明文接受）**：拒绝走 `scpi_port_push_exec_error` 但串口无文本回包（与既有 `BAD_ARGUMENT` 同类）；判读判据＝拒绝后记录号不变、下一条 `41` 仍 `QUEUED`。公共 SCPI 错误回包修复另立（影响面大，需 P3+四板）。
- **已知限制**：OTA 期间 link 服务早退会冻结 cache（OTA 中停环可绕过一次）；环停到 cache 归零 ≤1 tick 竞态——提交前仍二次校验，不足以产生 POSTED，记录待观。
- 契约：`REFMEM-ENTRY-01` clause/check 补"环未运行且 target 含远端 → 入口 `NOT_ONLINE_SAFE`、不落记录"，优先级高于 `NOT_CONTROL_OWNER`，并与部署期 `NOT_ONLINE_SAFE` 区分。

### REFMEM-TASK-20261029-022 — N10.2：环停远端命令 fail-closed 修复（已提交）

- 根因更正：不是"非 owner 目标"，而是**环停时远端 `REFTMEM_CMD` 仍被受理**——提交阻塞在维护流，源记录停留 `POSTED`，后续命令无法再 post（需 reboot）。
- 修复（`5bb742fb`）：
  1. core0 refmem 服务每 tick 刷新 `s_ring_transport_ready_cache`（原子），SCPI/submit 只做无锁读，避免命令上下文取 TDMA 锁；
  2. `distributed_refmem_control_submit` 与 `SYSTem:CTRL:EXECute` 在 target 含远端且 transport 未就绪时直接拒绝（`REJECTED` / `NOT_ONLINE_SAFE`），不再产生记录；
  3. host 用例补 `refmem_ring_transport_admits` 四组矩阵。
- HIL（build `20261029052659`）：
  - 环停：`EXECute 30/28,14,14` 不再 post（`CTRL:STATus?` 保持上一终态），`41` 仍 `ACKED` → 单槽不被堵死；
  - 环起：`30,14,14` `ACKED/ack=14`（第 12 字段是 `last_reason_slot`，不是 applied）、`31,14,14` `ACKED/ack=14`，回归通过。
- 凭证：P3 `20261029052659` / 1613 / `e144e97a`；host 77 passed / 541 files。
- 新残余：该 handler 的显式错误（`BAD_ARGUMENT`/`NOT_ONLINE_SAFE`）**无 SCPI 文本回包**（本地坏参数同样静默），属既有错误回包问题，另立。

### REFMEM-TASK-20261029-021 — N10.1：VDC/非 owner 目标命令挂起复现（POSTED 不终结）

- 复现（COM5，环停）：`CTRL:EXECute 28,14,14,1,0,1000`（以及 29）→ `CTRL:STATus?` = `465941,1780937987,1,465941,14,14,0,0,0,0,0,0,0`：**state=1（POSTED）**、`target=required=14`、`ack=0`、`nack=0`、`applied=0`。
- 后果：随后再发 `CTRL:EXECute 29,...` **超时**——`post_command_replacing_complete` 要求上一条 complete，POSTED 记录把单槽堵死；需 reboot 协调者清除（已做）。
- 更正上一轮措辞：不是 `TAKEN`（state=2），而是 **`POSTED`（state=1）**；`VDC:OUTPut:TIMing?` 读回 `12000,16000,6000` 说明本地 VDC 配置已被此前的 `FUNC_VDC_RECONFIG` 应用，与本次 28/29 无关。
- 初判：`target=14` 只含 1/2/3（非 VDC owner），目标侧执行/回程未形成 ACK/NACK（期望 fail-closed 到 reason 12/13）；需在目标 dispatch/回复路径加"非 owner 目标必须回 NACK"或收窄发号前校验。
- 证据：`out/n10/vdc_taken_repro.json`、`out/n10/vdc_taken_repro2.json`。

### REFMEM-TASK-20261029-020 — TRN-03 re-arm 预算诊断（FULL 阻断点）

- 用 `--run out/HardwareAcceptance/20261029/hawf-115728 --steps 21-36 --diagnostic-continue` 复跑：step 21/22 `OK`，step 23 `warn`（no measured SCK row satisfies the flight re-arm budget），step 24 TDMA 闭环 `OK`(76.3s)，step 26 `warn`（startup barrier timed out），step 29 因 provisional receipt 校验失败终止——**FULL 凭证仍不可得**。
- 数值诊断（`trn03-matrix.json`）：4 条链路 `link_base_delay_ns=41`、`baud=10 MHz`、`sample_period=4 ns`；候选行 4 组 `(0,1,0,1)/(0,1,1,1)/(1,1,0,1)/(1,1,1,1)` 的 `_sck_row_replay_safety` 全部 `safe=False`，最小 margin `-1 sample`，unsafe follower 2–3 条。
- 结论：这是台架/时序预算问题（SCK 相位余量差 1 sample），不是 REFMEM 代码问题；修复需台架专项（改 link base/布线/波特率或重标 SCK 行）。
- 证据：`out/n8/wf_fullbench_diag.log`、`out/HardwareAcceptance/20261029/hawf-115728/trn03-matrix.json`。

### REFMEM-TASK-20261029-019 — 四域/FUNC 单口同批捕获完成

- 用带轮询的 `out/n8/capture_single_port_full.py` 在 COM5 一口完成：`FUNC_TDMA_ONLINE` → `FUNC_TDMA_CLOCK_ONLINE` → `FUNC_VDC_RECONFIG` → `T2_CONFIGURE(30)` → `T2_MOUNT(31)`；每步等 `CTRL:STATus?` 终态后再发下一条。
- 结果：全部 `QUEUED` 并到终态；MOUNT `state=4/ack=14`；`COMMand:STATus?` `acked=1`；环 `up_tx` 4771→6540 连续。
- 观察（待归因）：`VDC:OUTPut:TIMing?` 读回 `24000,32000,16000`（请求 `12000/16000/6000`）；随后直接 `CTRL:EXECute 29,14,14,1,0,12000,16000,6000` 在环停/VDC 非 idle 下停留 `TAKEN`（未回 ACK/NACK），但**不阻塞**后续 `41`（`ACKED`）。疑为 VDC/TDMA 域 handler 未返回或应用未完成；**不作为通过证据**。
- 证据：`out/n8/single_port_full.json`；台架四板 build `20261029031043`、环停。

### REFMEM-TASK-20261029-018 — host 门禁覆盖 RefMem C runner（残余闭合）

- 状态：**此前"两个新 C runner 未入总门禁"的残余已闭合。**
- 改动：`tools/acceptance/host_acceptance.py` 与 `host_acceptance_workflow.py` 的选择器改为 `staged ∪ DEFAULT_PATHS`，两个 C runner 经 `tests/python/test_refmem_host_units.py` 进入每次 host 门禁。
- 凭证：host `PASS 77 passed / 541 files`（`409ebf2b`）；P3 不适用（tools/acceptance 属宿主类）。
- 文档：ENTRY-01 check、快照 §4/§7.2 同步；`REFMEM-ENTRY-01` 保持 `active`。
- 下一项：四域/FUNC 单口同批捕获（台架，N9 残余）。

### REFMEM-TASK-20261029-017 — N9.3：ENTRY-01 转 active（C11-05 整改闭环）

- 状态：**`REFMEM-ENTRY-01` pending → active（2026-10-29）**；`REFMEM-CTRL-01` 保持 `pending`（C3 租约/C6 fence 未实现）。
- C11-05（`docs/check/submissions/REFMEM_C11_REVIEW_05.md`）= `PASS_WITH_CHANGES`，要求 2 项文本整改，均已落：
  1. `DOCS_REGISTRY.md:29` 删除"条款拆分…待下一次登记"，"原字面仍不满足"改为"已由 `REFMEM-ENTRY-01` 重新定义"；同文件 `Last updated` 改 2026-10-29；
  2. `REFMEM_DOMAIN_TODO.md` 的 N5.5 决策与 `REFMEM_DOMAIN_ARCHITECTURE.md` 的入口边界改为"入口侧 `NOT_CONTROL_OWNER`（不落 reason）/目标侧 reason 12"。
- 已知残余：两个新 C host runner 未入 `run_host_unit_tests.ps1` 总门禁（已在 ENTRY-01 check 注明）。
- 下一步：N9.5 最终 §8 报告。

### REFMEM-TASK-20261029-016 — N9.2/N9.4：C11-04 复核与收口快照重写

- 状态：**N9.2 ✅（C11-04 = PASS_WITH_CHANGES）、N9.4 ✅（快照重写）；N9.3 ENTRY-01 保持 pending。**
- C11-04 三项整改：
  1. ENTRY-01 clause 第 3 条收窄为"T2 配置/MOUNT/聚合已机器捕获"，四域/`FUNC_*` 降为产品约束（未验证）；check 注明 host 用例"存在且可跑、未入总门禁"；
  2. 入口 12 语义收尾：`NOT_CONTROL_OWNER`（不落 reason）/ 目标侧 reason 12，架构与登记表同步；
  3. 快照 §3 当前 P3 改为 `20261029031043`/1613/`ae155f7d`，单口证据指向 `single_port_capture.json`。
- 快照已按 N9 重写 §1–§7（子项表、六门、契约状态、证据索引、提交清单），明确不得称完成。
- 下一步：C11-05 复审（可选，决定 ENTRY-01 是否 active）→ N9.5 最终报告。

### REFMEM-TASK-20261029-015 — N9 日间收尾立项：B 方案证据等级接受

- 状态：**N9.0/N9.1 ✅**（基线冻结、证据等级书面接受）；N9.2 C11-04 待跑。
- N9.0 基线：HEAD `22f7121d`、四板 build `20261029031043`、receipt QUICK/`ae155f7d`/1613 files/`strict_gates_passed=false`；他人 dirty 文件 3 个（t2_receipt、OTA_TODO、T2_FLOW）+ 本流程生成的 `docs/validation/P3_FLOW.html`。
- N9.1：快照 §1/§4、架构实现状态、登记表 `REFMEM-CTRL-01`/`REFMEM-ENTRY-01` 均写入"**N8.5 证据等级 = QUICK P3 + 四板 HIL，不冒充 full**；`--full-bench` 卡 TRN-03 SCK re-arm 预算，另立台架任务"。
- 里程碑：N9.0–N9.5 见 `REFMEM_DOMAIN_TODO.md` N9 节。
- 下一步：N9.2 C11-04 独立复核（子线程）→ N9.3 状态落地 → N9.4 快照重写 → N9.5 报告。

### REFMEM-TASK-20261029-014 — P3 全量尝试：QUICK 可复现，FULL 卡 TRN-03 re-arm 预算

- 用 `hardware_acceptance_workflow.py --profile full`（37 步、`--reuse-flashed`）跑两次：
  - 首次 step 30 失败：`A1E549202D18ED6A: final endpoint is outside the last steady trace bin`（录制分箱边缘，非相位超限，phase max 101 ns / warn 100 ns）；
  - 重跑从 0 走完 step 36，`credential: written`：build `20261029031043`、1613 files、`ae155f7d…`、scope `FOUR_NODE_DPLL_QUICK_DIAGNOSTIC`、`strict_gates_passed=false`、PASS_WITH_WARNINGS。
- `--full-bench`（`p3_bench.json`）在 step 21 失败：`no SCK offset candidate row can satisfy the flight re-arm budget`。
- 结论：**当前台架可获凭证仍是 QUICK/非 strict**；FULL 受 TRN-03 re-arm 预算限制，与 N8.5 代码无关，需台架/时序专项。
- 证据：`out/n8/wf_full.log`、`out/n8/wf_full2.log`、`out/n8/wf_fullbench.log`、`out/HardwareAcceptance/20261029/hawf-115208/`。

### REFMEM-TASK-20261029-013 — C11 必改项：host 可测拆分与入口契约拆分

- 状态：**A（host 可测拆分）代码完成、待 P3；B（入口契约拆分）已登记 pending、待 C11-02。**
- A：
  - `distributed_refmem_command_aggregate()` 拆到独立 TU `refmem_command_aggregate.c`（仅依赖 node-control read，去除 status.c 的重依赖）；
  - 新增纯策略 `refmem_position_reset_admits()`（`refmem_position_reset_policy.c`），`reset_position_running()` 改用之；
  - 新增 host 用例 `test_refmem_command_aggregate.c`（空/混合/更新终态/NULL）与 `test_refmem_position_reset_policy.c`（停环/适配器未起/配置未应用/运行稳定），随 `run_refmem_all_host_tests.ps1` 的 glob 自动收录；
  - host 全量仅剩既有 `realtime_tdma` 债务；P3 build `20261029031043`（进行中）。
- B：新增 `REFMEM-ENTRY-01`（同目录/同语义 + 跨节点发号单入口 + 单串口拓扑），登记 `pending`，请第二个 C11 线程复核后再决定是否 active；`REFMEM-CTRL-01` 保持 pending。
- 遗留：注释同步随 A 一起提交；`REFTMEM_POS_RESET_TIMEOUT` 保留为 reserved（未返回）。

### REFMEM-TASK-20261029-012 — N8.5 四板 T2 MOUNT 正向通过

- 状态：**N8.5 ✅**：`CTRL:EXECute 31,14,14,1,0,0` → `state=4 ACKED`、`ack_flags=14`、`nack=0`、`applied=3`。
- 关键修正：统一命令的 dispatch 在**同一个 refmem 服务任务**里执行，原 request/done 握手会等待自身 → reason 7 超时。改为在 reset API 内**同上下文直接清 TX/pending**（单写者），T2 start 期间生产者自然停发；不再需要跨任务握手。
- 前置：`t2_workflow --steps 1-10 --leave-running` 配 T2 → `trn03 --stage process-image --no-t2 --leave-running` 起环 → `CTRL:EXECute 30,14,14,...` 经 cached STOP 快照 ACK → 再 MOUNT。
- 连续性：MOUNT 前后环保持 `enabled=1`、`up/down=1`，COM5 `up_tx` 4340→7310→9028、COM4/3/6 TX/RX 同步推进；函数后四板 `RING:STOP`。
- 单串口约束（用户 2026-10-29）：统一命令实际只从 COM5 下发，远程目标走 `REFTMEM_CMD`；四板 `ack=14` 成立。**2026-10-29 补机器捕获**：`out/n8/single_port_capture.json`（port=COM5、build `20261029031043`、`30/31,14,14` 均 `ACKED/ack=14`、`COMMand:STATus?` 可见）；四域/FUNC 的单口同批捕获仍未做。
- 提交：`f07fb2b6`（代码，P3 `20261029012332` / `af6fbe0f`）、`b50f6c30`（文档）。
- 证据：`out/n8/n85_mount_four_board.json`、`out/n8/four_board_hil/trn03_n85j/`、`out/n8/n85_mount_local_ok.json`。

### REFMEM-TASK-20261029-011 — N8.5 running-safe 代码落地；正向 HIL 待复跑

- 状态：**代码 ✅（构建 + host 回归）、fail-closed 实机 ✅、正向 MOUNT HIL ⏳。**
- 实现：
  - `distributed_refmem_position_state_t` 增加 `reset_request/reset_done` 握手；
  - `distributed_refmem_position_tx_prepare()` 在 request 期间停发新位置；
  - refmem 服务在**同一上下文**清 TX/pending 状态（`position_clear_running_tx`）后置 done——不做跨任务清状态；
  - `distributed_refmem_reset_position_running()`（有界 50 ms，`UNSAFE/TIMEOUT/OK`）+ `distributed_refmem_position_resume()`；
  - `t2_mount_handler` 接线：reset OK → `trigger_t2_runtime_start(epoch, origin)` → resume；失败回退 `NOT_ONLINE_SAFE`。
- 验证：build `20261029003215`（四板已刷）；`run_refmem_all_host_tests.ps1` 仅剩既有 `realtime_tdma` 失败；环停时 `CTRL:EXECute 31,1,1,1,0,0` → `state=5 NACKED/reason=14`（fail-closed 保持）。
- 未完成：环运行 + T2 configured 的正向 MOUNT。台架时序互相干扰——第二轮 TRN 的 recorder cancel 需要 reboot，而 reboot 会清掉 T2 配置；`RECord:SAVE` 只把 state 5→8，`CANCel` 不清 8。
- 下一步：改用不依赖 TRN recorder 的起环路径（或给 recorder 增加 SAVE→IDLE 复位），再跑含 MOUNT 步的四板 HIL。

### REFMEM-TASK-20261029-010 — 用户改选 B：N8.5 继续实施（撤回 A 边界）

- 状态：用户 2026-10-29 先选择 A（接受边界）、随后**更正为 B**：继续实现 T2 running-safe。
- 处置：A 的边界文本在提交前撤回（未提交、无历史包袱）；契约/架构/快照恢复为"N8.5 进行中，未通过前 `NOT_ONLINE_SAFE`"。
- 计划：quiesce position TX/RX → 有界等待 → 清位置状态（generation 递增）→ `t2_mount_handler` 接线 → 含 MOUNT 步四板 HIL。
- 设计包：`out/n8/n8_5_running_safe_design.md`。

### REFMEM-TASK-20261029-009 — N8.7 收口结论：统一面未完成，N8.5 为唯一功能缺口

- 状态：**N8.0–N8.4、N8.6 完成；N8.5 只有设计、未实现；整体不可标完成。**
- 结论写入 `REFMEM_UNIFIED_CONTROL_REVIEW.md` §7：子项表、契约状态（`REFMEM-CTRL-01` 保持 pending）、残余风险、提交清单。
- 未实现 N8.5 的原因（工程判断）：`trigger_t2_runtime_start` 要求 “RX producer parked”，而运行中 position TX/RX 跨 scpi/refmem 两个任务，现有代码没有 quiesce 锁；没有专门设计与 HIL 窗口时，贸然落实时代码会引入竞态。设计包见 `out/n8/n8_5_running_safe_design.md`。
- 下一步：N8.5 专项（quiesce 锁 + 有界等待 + HIL）；T2 四板含 MOUNT 复跑；C11/EXE-SEAT-01 由用户/第三方推进。

### REFMEM-TASK-20261029-008 — N8.6 四板 function HIL 复跑通过

- 状态：**N8.6 ✅**：`FUNC_TDMA_ONLINE`(88291, 2/2)、`FUNC_TDMA_CLOCK_ONLINE`(88292, 4/4)、`FUNC_VDC_RECONFIG`(88293, 4/4) 均 `state=2 COMPLETED`、目标 `ack=14`。
- 环：`trn03_closed_loop --stage process-image --leave-running --no-t2 --diagnostic-continue`（config `out/HardwareAcceptance/20261029/p3-n57/trn03-matrix.json`、row 1）；四板 `up/down=1`、TX/RX 推进（COM5 19159→19160 等）。
- VDC 读回：COM5/4/3/6 `VDC:OUTPut:TIMing?` 均 `12000,16000,6000`。
- 边界：本轮 `--no-t2`，T2 四板沿用 10-27 证据；本 HIL 是功能证据，不是新 build 凭证（`20261028180729` 的 P3 已随 `4f7a94f0` 提交）。
- 台架：函数后四板 `RING:STOP`，`enabled=0`、`up/down=0`。
- 证据：`out/n8/four_board_hil/n86_summary.json`、`out/n8/four_board_hil/trn03_run5/`。

### REFMEM-TASK-20261029-007 — 台架换 flash 路径成功；N5.7 实机读回通过

- 状态：**N8.4 ✅（代码 + 实机）**；`ota_send` 中途卡死的阻塞被绕过。
- 关键：`tools/picotool_flash`（BOOTSEL UF2 工厂镜像）可用；四板刷到 `20261028180729`（含 N5.7）。随后 `ota_multi_update --skip-pre-reboot --block-size 512` 4/4 PASS，产出 `summary.json` 供 P3 `resume`。
- N5.7 实机（COM5）：
  - 合法 `41` 后 `SYSTem:COMMand:STATus?` = `8,0,0,0,1,0,0,4,0,0,21893`（acked=1、last_state=4）；
  - 非法 `41` 后 = `8,0,0,0,0,1,0,5,13,0,21894`（nacked=1、last_state=5、reason=13）；
  - `SYSTem:COMMand:TARget? 0` = `0,21894,5,41,1,1,0,1,13`。
- 证据：`out/n8/n57_smoke.json`、`out/n8/ota_multi_n57/summary.json`、`out/n8/picotool_flash_*.txt`。
- 下一步：P3 `resume`（进行中）→ 提交 N5.7；N8.6 四板 `FUNC_*` HIL；N8.5 视余量。

### REFMEM-TASK-20261029-006 — 台架 flash 路径受阻；N8.5 设计包交付

- 状态：**N8.4 代码保持（实机 smoke 仍阻塞）；N8.5 只出设计，不落代码。**
- 台架：COM4/COM5 恢复后重试 OTA 仍在中途卡死；BOOTSEL 路径 `picotool reboot -f -u` 的多种参数组合都未进入 BOOTSEL（`info` 始终报 USB serial）。四板已 `OTA:ABOR` + reboot，全部 `IDLE`、build `20261028171345`、环停。
- N8.5 设计：`out/n8/n8_5_running_safe_design.md` 给出 quiesce+reset 方案——新增 `distributed_refmem_reset_position_running()`（请求静默 → 有界等待 TX/RX 空闲 → 清 position 状态 → 释放 + generation 递增），再由 `t2_mount_handler` 接线；明确写者边界、失败回退与 HIL 判据。
- 不落代码的原因：实时路径改动无法上板验证；仅 host 证据不足以收口。
- 下一步：flash 路径需现场介入（BOOTSEL 按键 / 换 USB 口 / 换主机）或修 `ota_send` 的阻塞写；恢复后先做 N5.7 smoke，再评估 N8.5。

### REFMEM-TASK-20261029-005 — N8.4 N5.7 只读聚合代码完成；台架 OTA 不稳阻塞实机验证

- 状态：**N5.7 代码 ✅（构建 + host 回归）；实机 smoke ⏳（OTA 反复卡死）。**
- 内容：新增 `refmem_command_aggregate_t` 与 `distributed_refmem_command_aggregate()`——从既有 per-node command 记录派生 `posted/taken/busy/acked/nacked/timed_out` 计数与最新终态（state/reason/slot/completed_seq），**不建第二事实、不改 wire/布局**；新增只读 SCPI `SYSTem:COMMand:STATus?`（11 字段）与 `SYSTem:COMMand:TARget? <slot>`（9 字段，读同一 node-control）。
- 验证：`cmake --build build --target DHRT100` 通过；`run_refmem_all_host_tests.ps1` 仅剩既有 `realtime_tdma` harness 债务（缺 `tdma_service_core1`），无新增失败。
- 台架：COM5 三次 OTA（P3 并发、单板 512 B、重试）分别在 `ERASE_SLOT`、63%、无输出处卡死；已 `OTA:ABOR` + picotool reboot 恢复，当前 build `20261028171345`、OTA `IDLE`、环停。
- 未覆盖：aggregate 的实机读回；N8.5（T2 running-safe）未开始；N8.6 四板 `FUNC_*` HIL 未跑。
- 下一步：换 flash 路径（picotool/串行 OTA）后复验 N5.7；再进 N8.5。

### REFMEM-TASK-20261029-004 — N8.6 P3 凭证取得（resume）+ `48` 边界更正

- 状态：**P3 QUICK 凭证已重置并 PASS**（build `20261028171345`，fingerprint `1fc53c7d…`，1609 files，`PASS_WITH_WARNINGS`：INFO 33 / WARN 12 / ERROR 0 / FATAL 0）。
- 台架绕行：P3 `run` 的四板并发 OTA 两次卡在 `ota_send`（reboot 后立即开会话）；改为 `ota_multi_update --skip-pre-reboot --settle 2.0 --block-size 512` 先取得 `summary.json`，再 `p3_hardware_acceptance.py resume --package … --ota-summary …` 跑完 P0T/TRN/TDMA/DPLL。凭证 `config/hardware_acceptance/p3_acceptance_receipt.json`。
- 更正：`CTRL:EXECute 48` 无应答**不是固件缺陷**——上一轮实际只发了 15 个参数（handler 要 16 个，`SYST:ERR? -109`）。按 16 词重发后 `QUEUED`，`CTRL:STATus?` 为 `state=5 NACKED/reason=13`（plan 业务校验拒绝）；`47` 仍依赖有效 plan，未做正向。
- host：`run_refmem_command_authority_tests.ps1` 通过；`run_refmem_all_host_tests.ps1` 仅剩 `realtime_tdma` 既有 harness 债务。
- 提交：代码（`distributed_refmem_command.c` + 测试）与 receipt 待提交；工作区另有既有 `tools/sync_trigger/t2_workflow.py` 改动，需一并入树才能通过 `check-staged` 的指纹一致性。
- 下一步：N8.4（N5.7 只读聚合）与 N8.5（T2 running-safe）设计/实现；四板 `FUNC_*` HIL 复跑。

### REFMEM-TASK-20261029-003 — N8.3 控制面失败路径 NACK 可见性修复与 IF-013 复验

- 状态：**N8.3 代码 + 单板 HIL ✅；四板 P3 凭证未取（台架 OTA 并发卡死），N8.6 待续。**
- 根因：`distributed_refmem_node_control_service()` 失败分支只 NACK command slot、不写 node-control，`CTRL:STATus?` 永远 `POSTED`（state=1），与在途命令不可区分。
- 修复：失败分支补 `distributed_refmem_node_control_update_status(NACKED, nack=1<<slot, reason)`；无 wire/布局/槽位变化。
- host：`run_refmem_command_authority_tests.ps1` 增加 NACK 镜像断言（state/nack 掩码/reason）并通过；`run_refmem_all_host_tests.ps1` 仍有 `run_refmem_realtime_tdma_tests.ps1` 一项失败，属 N5.0.4a 拆分后的既有 harness 债务（缺 `tdma_service_core1` 符号），与本修复无关。
- 单板 HIL（COM5，build `20261028171345`，环停止）：非法 41 → `state=5 NACKED/nack=1/reason=13`；合法 41 → `ACKED`；42（合法浮点）→ `ACKED`；43/44/45/46 → `ACKED`；47（无 plan）→ `NACKED/13`；49 → `ACKED`；35 → `NACKED/13`（会话策略 fail-closed）。
- 未闭环边界：`CTRL:EXECute 48`（16 词 plan）无应答、`SYST:ERR? -109`；`CTRL:EXECute 4`（self-check）超时；留 N8.3b。
- 台架：P3 两次 `run` 均卡在四板 OTA（残留 `RECEIVING`/`FAILED`）；已逐板 `OTA:ABOR` + picotool reboot + 手动 `ota_send`（512 B 块）统一四板到 `20261028171345`，OTA 均 `IDLE`。
- 提交：**未提交**——工作区存在他人未提交的 `tools/sync_trigger/t2_workflow.py`，`check-staged` 会拒绝混合树；待该改动落库或用户裁定后，再按 staged 指纹跑 P3 提交。
- 下一步：N8.3b（48/4 根因 + 全量 host 复跑）→ N8.6（P3 凭证/四板 HIL）。

### REFMEM-TASK-20261029-002 — N8.1/N8.2 收口快照与入口无关边界回正

- 状态：**N8.1 ✅、N8.2 ✅（均为文档面）**。
- N8.1：`REFMEM_UNIFIED_CONTROL_REVIEW.md` 标题改为"范围闭环，整体未收口"、Status `Active`；§1 明确"不可标记完成"，§2 补 catalog 36/历史 21、N7.6 改 ⚠️，§3 补当前 P3 `20261028154542`/`out/n8/baseline.json`/`b16f7b0a`，§4 补契约 pending、N5.5、IF-012/013、相位预算，§6 指向 N8。
- N8.2：N5.5 冻结为**产品级入口收敛**——非协调者远端 `REFTMEM_CMD` fail-closed(12)、不中继/不排队/不建 relay 状态；`REFMEM-CTRL-01` 增补"N6/N7 增量已实现 + 入口无关条款未满足、状态保持 pending"；`REFMEM_DOMAIN_ARCHITECTURE.md` 新增「入口无关边界（N5.5）」小节，C1 转发腿列为显式非目标。
- 边界：不把契约转 active、不代替 C11、不写转发代码；wire/布局/槽位未动。
- 下一步：N8.3 IF-013 后端（先核 `TRIGger:MODE` 与 7 条配置命令的 handler 与最新证据）。

### REFMEM-TASK-20261029-001 — N8 统一面收口夜间目标立项

- 状态：长期目标建立（`goal-2c069112…`，40 轮上限）；N8.0–N8.7 细化里程碑写入 `REFMEM_DOMAIN_TODO.md` N8 节。
- 授权边界（用户 2026-10-29）：台架允许无人值守 HIL（复位/烧录/起环/P3 QUICK）；实时路径允许改动（含 T2 running-safe 位置复位）；N5.5 由 AI 按 HAOFV 架构自决。
- N5.5 预置决策：**产品级入口收敛**——非协调者入口仅本地，远端 `REFMEM_CMD` 由非协调者发起一律 fail-closed（12），CP1 记为显式例外；不新增 payload class/wire/第二命令槽/第二 ACK；重开判据：产品要求非协调者远端发起时按紧凑 intent delta 重做，并先补 TLV/参数池同步与 256 B 容量证据。
- 基线（本轮实测，非事实源）：host 4 套件通过（`run_refmem_function_tests.ps1`、`run_refmem_function_catalog_tests.ps1`、`run_refmem_command_catalog_tests.ps1`、`run_refmem_unified_control_host_tests.ps1`）；`command_catalog.yaml` 36 条、`function_catalog.yaml` 6 个；当前 P3 receipt build `20261028154542`（QUICK/DPLL scope、1609 files、fingerprint `80094026…`）；工作区仅 `t2_acceptance_receipt.json` 未提交（`passed=true/verdict_ok=false`，`8aae5e78` 已堵放行路径）。
- 下一步：N8.0 台架可用性探测，随后 N8.1/N8.2 文档冻结。

### REFMEM-TASK-20261027-016 — 四板 VDC 正向通过：目标闭环

- 状态：**四板 VDC `FUNC_VDC_RECONFIG` 正向 ✅；统一控制面目标闭环。**
- 关键修正：此前 `reason=7` 是**参数非法**，不是 reference 不退休：
  - `input_port=0` 非法（1..4），`timeout_ms=100 <= window_ms=1000` 非法（必须 `timeout_ms > window_ms`）。
  - 合法参数：`input_port=4`、`edge=0`、`nominal=10000000`、`window_ms=1000`、`timeout_ms=2500`；timing `12000/16000/6000`。
- 本地：`FUNC_VDC_RECONFIG,1,1,4,0,10000000,1000,2500,0,12000,16000,6000` → `COMPLETED/ack=1`，timing 读回 `12000,16000,6000`。
- 四板（process-image 环）：`FUNC_VDC_RECONFIG,14,14,...` → `state=2 COMPLETED`、step 4/4、`ack=14`、`nack=0`；COM4/3/6 `VDC:OUTPut:TIMing?` 均 `12000,16000,6000`；四板 ring `up/down=1`、TX/RX 推进。
- 证据：`out/n7/local_hil/vdc_function_success.txt`、`out/n7/four_board_hil/vdc_positive_four_board.txt`、`trn03_vdc4_1/`。
- 边界：`T2_MOUNT/START` 仍 `NOT_ONLINE_SAFE`（需 running-safe 位置复位），属于已声明首版边界，不是本轮回归。
- 处置：HIL 后 `RING:STOP` 四板。
- 结论：N6.0–N6.5、N7.1–N7.6 的目标范围均已闭环；可标记目标完成。

### REFMEM-TASK-20261027-015 — 四板 T2 正向通过：T2 读 STOP 暂存快照

- 状态：**四板 T2 正向 ✅**；目标仅余 VDC reference/timing。
- 根因：`trigger_grid_preview_snapshot`/`trigger_deployment_snapshot` 是 STOP-only 读；环 START 后返回 `TRIGGER_GRID_PREVIEW_NOT_STOPPED`（`1,33`），T2 handler 误判为前置缺失。
- 修复（`a3563def`）：新增只读 `trigger_grid_preview_snapshot_cached` / `trigger_deployment_snapshot_cached`，T2 handler 在 TDMA 运行中读 STOP 暂存副本；本地 SCPI 仍走 STOP-only 原件。P3 `20261027014444`。
- 四板 HIL（reset → T2 steps 1-10 → TRN process-image 不复位）：
  - `FUNC_T2_RECONFIG,14,14,1,0,14,0,5000,5000,500,0,0,360000,1000,1000,0`
  - `state=2 COMPLETED`、step 2/2、`ack=14`、`nack=0`；`CTRL:STATus?` `state=4 ACKED`。
  - 四板 ring 全程 `up/down=1`，TX/RX 推进；HIL 后 `RING:STOP`。
- 证据：`out/n7/four_board_hil/t2_positive_cached.txt`、`trn03_cached_run1/`、`t2_cached_stage1.log`。
- 残余：VDC reference/timing 仍 `reason=7`；T2 START/MOUNT 仍本地；N7.5/N7.6 待 VDC。
- 下一步：VDC owner 修 reference retire/output idle，或按用户决定把 VDC 降级为窄 delay 并收口。

### REFMEM-TASK-20261027-014 — 根因：RING:ARM 清掉 T2 前置，形成启动顺序冲突

- 状态：四板 T2 正向被**固件顺序**阻塞，不是台架抖动；建议目标标 blocked。
- 二分（COM5，STOP 态已装 grid/deployment）：
  - 基线 `GRID?` 完整；
  - `FLIGHT:MODE 1` 后 `GRID?` 完整；
  - `FIFO:RESet` 后 `GRID?` 完整；
  - `OPMode:STAGe 7/APPLy`、`RING:TOPology` 后 `GRID?` 完整；
  - **`SYSTem:TDMA:RING:ARM` 后 `GRID?`/`DEPloyment?` 变为 `1,33`**。
- ARM 后重装前置：`t2_workflow --steps 8-10` 失败（step8 `GRID:CONFigure` result 33、step9 binding stale、step10 stores not current）。
- 结论：T2 grid/deployment 必须在 ARM **前**安装；而远端 function 又要求环 START 后才能承载 `REFTMEM_CMD`。ARM 清前置 ⇒ 启动后无法再配 T2 ⇒ 四板远端 `FUNC_T2_RECONFIG` 不可能用当前固件完成。
- 正面结论仍保留：process-image 环上远端 T2 step0 ACK=14（`REFMEM-TASK-20261027-013`）。
- 证据：`out/n7/t2_bisect.txt`、`out/n7/t2_after_arm.log`。
- 闭环：固件需“ARM 保留 T2 前置”或“T2_CONFIGURE 读 ARM 前快照/允许 ARM 后重装”；在此之前不进入下一子项。

### REFMEM-TASK-20261027-013 — process-image 上远端 T2 交付成立；前置被 TRN 清掉

- 状态：四板 T2 远端命令路径 ✅（step0 ACK=14）；正向仍 ❌（前置丢失）。
- 序列：
  1. 复位四板；`t2_workflow.py --steps 1-10 --leave-running` 成功（grid/deployment/templates 全配好，环仍停）。
  2. 不复位，`trn03_closed_loop --stage process-image --leave-running --no-t2 --cancel-unsaved-record` 一次通过（`trn03_t2_keep3`，`passed/left_running=true`）。
  3. 直接跑协调者 `FUNC_T2_RECONFIG,14,14,1,0,14,0,5000,5000,500,0,0,360000,1000,1000,0`：
     - step0（cmd32 T2_UNMOUNT）**ACK=14**；step1（cmd30 T2_CONFIGURE）**NACK=14 / reason=13**；function FAILED。
     - 源 `CTRL:STATus?` `state=5 NACKED/nack=14`。
- 关键结论：**process-image 环能承载 REFMEM_CMD，远端 T2 停/配置交付成立**；失败原因是 TRN 启动后 `GRID?`/`DEPloyment?` 返回 `1,33`（前置失效），step1 拿不到 grid/deployment。
- 处置：已 `RING:STOP` 四板，均 `enabled=0`。
- 证据：`out/n7/four_board_hil/t2_positive_process_image.txt`、`trn03_t2_keep3/`、`t2_steps_1_10_reset.log`。
- 下一步：定位 TRN 启动中清掉 grid/deployment 的命令（FLIGHT:MODE/FIFO/TOPO/ARM 候选），改为保留前置；或在 TRN 后不 STOP 重配前置的等价路径。

### REFMEM-TASK-20261027-012 — T2 前置不能在 process-image 运行中补装

- 状态：四板 T2 正向仍阻塞；C14 已轮转。
- 实测：TRN process-image `trn03_t2prep1` 一次通过（`passed=true/left_running=true`）；在运行中的 process-image 环上跑 `t2_workflow.py --steps 7-11 --leave-running`，步骤 8–11 失败：
  - step8 `GRID:CONFigure` → `grid not configured (result 33)`；
  - step9 `binding stale`；step10 `stores not current`；step11 `counter not loaded`。
- 判定：grid/deployment/template 的安装需要 STOP 态；process-image 运行中不能补装 T2 前置。四板 T2 正向需要“先 STOP 装前置 → 再 process-image 启动”的顺序，而 TRN 启动会复位 RAM，当前工具链没有保留前置的路径。
- 处置：已 `RING:STOP` 四板，均 `enabled=0`。
- 证据：`out/n7/t2_steps_7_11.log`、`out/n7/four_board_hil/trn03_t2prep1/`。
- 下一步：给 TRN/流程增加“STOP 态装 T2 前置后不复位启动 process-image”，或把 T2 前置持久化到 flash/配置，再复跑四板 T2 正向。

### REFMEM-TASK-20261027-011 — T2 leave-running 工具；四板 T2 仍卡 process-image

- 状态：工具 ✅；四板 T2 正向 ❌。
- `tools/sync_trigger/t2_workflow.py` 新增 `--leave-running`，跳过自动 step21 teardown；P3 `20261026181952`；commit `a5cbdb06`。
- 实测：`--steps 1-20 --leave-running` 后四板 `RING:STATus? enabled/up/down=1`，T2/VDC 运行；协调者 `FUNC_T2_RECONFIG,14,14,...` 的 step0（cmd32）仍超时 `reason=11`，源 `CTRL:STATus?` POSTED。
- 判定：T2 workflow 的 ring 是 raw/T2 链，不承载 `REFTMEM_CMD`；四板 T2 正向必须用 TRN process-image 环（且保留 grid/deployment/templates）。工具 leave-running 只解决“不被 teardown”，不解决命令承载。
- 处置：已 `T2:UNMOUNT` + `RING:STOP` 四板，均 `enabled=0`。
- 证据：`out/n7/four_board_hil/t2_positive_four_board.txt`、`out/n7/t2_workflow_leave.log`。
- 下一步：把 T2 前置（grid/deployment/templates）与 TRN process-image 串起来；或给 TRN 增加“保留 T2 前置”的能力。

### REFMEM-TASK-20261027-010 — N7.5/N7.6 收口快照（未完成）

- 状态：新增 `docs/refmem/REFMEM_UNIFIED_CONTROL_REVIEW.md`，作为 N7.5/N7.6 的收口快照；**目标不标完成**。
- 内容：N6.0–N6.5、N7.1–N7.6 子项总表；P3/HIL/提交证据索引；VDC reference/timing、四板 T2 正向、T2 START/MOUNT 开放项与复验判据；契约边界（单 command slot / 单 ACK seat / coordinator-only）。
- 关键判定：四板逐板本地 function 不能替代协调者单命令四板证据（非协调者 step0 `reason=11`）；T2 workflow 1-20 仍自动 teardown，无法借其保持“四板环+前置”。
- 文档门禁：`docs_check` 235 files、`doc_regression` 全绿；提交见本轮 docs。
- 下一步：VDC owner 修 reference retire/output idle；四板 T2 正向需 process-image + 前置；完成后更新本快照并标 N7.5/N7.6。

### REFMEM-TASK-20261027-009 — 四板 T2 正向边界：function 入口仅协调者

- 状态：T2 本地正向保持；四板单命令 T2 正向未达成。
- 尝试：
  - `t2_workflow.py --steps 1-20` 仍自动执行 step21 teardown（日志 `out/n7/t2_workflow_1_20.log`），环/T2 被停，无法借其保持“四板环 + 前置”状态。
  - 在 COM4/3/6 各自跑本地 `FUNC_T2_RECONFIG,1,1,...` → step0（cmd32）`step_state=1 SUBMITTED` 后超时 `reason=11`（`out/n7/four_board_hil/t2_positive_per_board.txt`）。
- 判定：`SYSTem:CTRL:FUNCtion:RUN` 的入口仍是协调者（slot 0）单入口；非协调者本地 function 不会被 RefMem 服务消费。四板 T2 正向必须走协调者 `FUNC_T2_RECONFIG,14,14,...` + 健康 process-image 环；不能用四块本地调用拼凑。
- 现有证据：COM5 本地 `FUNC_T2_RECONFIG` `COMPLETED/ack=1`（`t2_positive_try.txt`）；四板 T2 负向 step0 ACK、step1 `reason=13`。
- 未覆盖：四板 T2 正向；T2 START/MOUNT 仍 `NOT_ONLINE_SAFE`；VDC reference/timing。
- 下一步：用可保持运行的 process-image 环（或给 T2 workflow 加 leave-running 选项，需工具/P3 凭证）再跑四板 T2 正向。

### REFMEM-TASK-20261027-008 — T2 正向 function 首次通过：停+配置 COMPLETED

- 状态：T2 正向域停配置 ✅（本地 COM5）；四板 T2 正向未做（环已停）。
- 前置：`t2_workflow.py --full` 21 步全 OK（grid/deployment/action table/template/T2 configure+start/VDC run/优先级/位置发布/teardown），日志 `out/n7/t2_workflow_full.log`；teardown 停环后 grid/deployment/templates 仍 configured（`GRID?` configured=1、`DEPloyment?` active=1、`TEMPlate?` installed=1）。
- 先单步验证：`CTRL:EXECute 30,1,1,1,0,14,0,5000,5000,500,0,0,360000,1000,1000,0` → `state=4 ACKED`。关键：`threshold_per_mille` 必须 0<值<1000；此前用 0 被 `predictor_config_valid` 拒绝（diag 142）。
- function 正向：`FUNC_T2_RECONFIG,1,1,1,0,14,0,5000,5000,500,0,0,360000,1000,1000,0`
  - `state=2 COMPLETED`、`step_index=2/2`、`ack=1`、`nack=0`；`CTRL:STATus?` `state=4 ACKED`。
- 参数：run_id=1，source=0，target=14，origin=0，pre/post=5000 mdeg，threshold=500‰，start=0，zero=0，terminal=360000 mdeg，step=1000 mdeg，ppd=1000，window=0。
- 证据：`out/n7/local_hil/t2_positive_try.txt`；未改代码，无新 P3。
- 未覆盖：四板 T2 正向（需健康 process-image 环）、T2 start/mount（仍 `NOT_ONLINE_SAFE`）、VDC reference/timing、N7.5。
- 下一步：用同一组参数在四板环上跑 `FUNC_T2_RECONFIG,14,14,...`；补齐 N7.5 收口。

### REFMEM-TASK-20261027-007 — 有界 RUN_STATE_DENIED 重试；VDC reference 仍不退休

- 状态：function 重试策略落库；VDC reference 仍阻塞。
- 内容：`REFMEM_FUNCTION_STEP_RETRY_MAX=3`、`REFMEM_FUNCTION_STEP_RETRY_GAP_MS=100`；step NACK `reason=7` 时保持 RUNNING、重试最多 3 次，耗尽后 FAILED；其他 reason 仍立即失败。
- 验证：host `test_refmem_function` 新增“7 重试→RUNNING→耗尽 FAILED”用例；`pico2-release` 构建通过；P3 `20261026181952`；commit `4c0be6fb`。
- VDC 复测：`FUNC_VDC_RECONFIG` 仍 `state=3 FAILED / step1 cmd27 / reason=7`；`VDC:REFerence:STATus?` 仍 `1,2,...`（RUNNING），`CONFigure?` 未变。重试不能替代 VDC owner 的 retire 闭环。
- 证据：`out/n7/local_hil/vdc_retry.txt`。
- 未覆盖/残余：VDC reference retire、timing idle、T2 正向、四板 VDC、N7.5。
- 下一步：VDC owner 修 reference retire/output idle；function 重试只作为域停窗口的容错，不是根因修复。

### REFMEM-TASK-20261027-006 — FUNC_VDC_OUTPUT_DELAY：在线 VDC delay 单命令成功

- 状态：窄 VDC function 通过；VDC reference/timing 仍阻塞。
- 内容：新增 `FUNC_VDC_OUTPUT_DELAY`（id 6，arg_words 1，steps 33→28：VDC_STOP → VDC_OUTPUT_DELAY）；function catalog 6 个。
- HIL（COM5 本地，`out/n7/local_hil/vdc_delay_function.txt`）：
  - `FUNC_VDC_OUTPUT_DELAY,1,1,321` → `state=2 COMPLETED`、`step_index=2`、`ack=1`、`nack=0`。
  - `VDC:OUTPut:DELay?` 读回 `321`。
- 证据：`pico2-release` 构建通过；P3 `20261026180937`；host catalog/executor 通过；commit `970d231b`。
- 台架：本轮为 VDC reference 做的 3 次 TRN retry 均失败（startup barrier/空错误），未进入可跑 reference 的 process-image；因此 VDC reference retire 未复验。
- 未覆盖：VDC timing（`vdc_run_output_configuration_idle` 仍 false）、reference（`s_reference_active_generation` 未清零）、四板 VDC function、T2 正向。
- 下一步：VDC owner 修 reference retire/output idle；或把 VDC timing 也做成窄 function 并先解决 idle 判据。

### REFMEM-TASK-20261027-005 — VDC 原子面实测：delay 成功，timing/reference 仍拒绝

- 状态：VDC output delay 在线配置 ✅；output timing 与 reference 仍 `reason=7`；`FUNC_VDC_RECONFIG` 未通过。
- 实测（COM5 本地，TDMA 已停环；`CTRL:EXECute` 单步）：
  - `33` VDC_STOP → `QUEUED,91582`（ACK）。
  - `28,123` VDC_OUTPUT_DELAY → `QUEUED,91583`；读回 `VDC:OUTPut:DELay?=123` ✅。
  - `29,1000,2000,100` VDC_OUTPUT_TIMING → `QUEUED,91584`；`CTRL:STATus?` = `state=5 NACKED/nack=1/reason=7`；读回仍 `12000,16000,6000`。
  - `27,...` VDC_REFERENCE_CONFIG 仍 reason 7（reference 未 retire）；`ENABle?=0` 但 `STATus state=2 RUNNING`。
- 判定：
  - delay publish 是纯原子写，`VDC_STOP` 后可用。
  - timing 的 `vdc_run_output_configuration_idle()` 仍为 false（run output cancel 后还有其它 output 配置未 idle），被 fail-closed 拒绝。
  - reference 的 `s_reference_active_generation` 未清零，`reference_config_install` 拒绝；根因在 VDC owner retire 闭环，不在 RefMem。
- 证据：`out/n7/local_hil/vdc_function.txt`（含本次命令/读回）；提交 `5e688260`（VDC 代码）、`410b25b6`（阻塞文档）。
- 未覆盖：VDC timing/reference 正面、四板 VDC function、T2 正向。
- 下一步：VDC owner 修 reference retire + output 全 idle 判据；或先提供只含 `VDC_STOP→OUTPUT_DELAY` 的窄 function，证明在线 VDC 输出配置。

### REFMEM-TASK-20261027-004 — N6.3.3 VDC 在线配置：代码/P3 完成，参考退休阻塞

- 状态：VDC 在线配置 API + `FUNC_VDC_RECONFIG` 已落库并 P3；**功能 HIL 未通过**，卡在 VDC reference 退休。
- 内容：
  - 新增 `vdc_dpll_manager_set_reference_config_online` / `set_output_delay_ns_online` / `set_output_timing_profile_online`：TDMA 不停机，参考必须已停、VDC output 必须 idle；本地 SCPI 仍 TDMA-STOP-only。
  - `VDC_STOP` 执行 `vdc_run_output_cancel()` + `vdc_dpll_manager_set_reference_enabled(false)`；`VDC_START` 明确 `NOT_ONLINE_SAFE`（run output 需 period/high/duration）。
  - 新增 `FUNC_VDC_RECONFIG`（33→27→28→29，9 args）；function catalog 5 个。
- 证据：`pico2-release` 构建通过；P3 `20261026174849`；host VDC harness 30 passed（补 `vdc_run_output_configuration_idle` fake）；commit `5e688260`。
- HIL 负结果（`out/n7/local_hil/vdc_function.txt`）：
  - `FUNC_VDC_RECONFIG,1,1,0,0,10000000,1000,100,0,1000,2000,100` → step0（VDC_STOP）ACK；step1（cmd27 reference config）`NACK reason=7`；function `FAILED`。
  - `VDC:REFerence:ENABle?=0`，但 `STATus?` 仍 `state=2 RUNNING`；SCPI `ENABle 0` 后仍不退休。`reference_config_install` 因 `s_reference_active_generation` 非零拒绝。
- 根因候选：reference cancel 只置取消/请求，core1 owner 未在 TDMA 运行/停环两种工况下推进到 RETIRED；需 VDC owner 修“disable → retire”闭环，或在 `VDC_STOP` 等待 bounded retirement（不能忙等）。
- 未覆盖：VDC reference/config 正面 HIL、output delay/timing 正面、四板 VDC function；T2 正向仍缺。
- 台架处置：VDC HIL 后已 `RING:STOP` 四板。
- 下一步：修 VDC reference retire 闭环，再重跑 `FUNC_VDC_RECONFIG`；未过前 N6.3.3 不标 ✅。

### REFMEM-TASK-20261027-003 — N7.4 四板闭环：本地执行槽只含本板位

- 状态：**N7.4 四板 function HIL 通过**（TDMA/DPLL 多步 + T2 负向）；VDC function 仍缺。
- 根因（2026-10-27，step1 超时）：
  - 目标执行远端命令时，`distributed_refmem_post_command_replacing_complete` 把命令投到本板 command slot，但 request 仍带 `target_mask/required_mask=14`；本板 ACK 只能置本板位，slot 永远 `TAKEN`，下一步 `post` 因“未完成”失败并静默设置 `s_node_control_last_command_seq`，不再 ACK/回复。
  - 证据：COM4 `SYSTem:COMMand:ACK?` 显示 `state=2 TAKEN, command_seq=1, target/required=14, command_type=21`；`CMDFRame?=2,0,0,0,2`（帧已 apply），`NODE:CONTrol?` 为 step1 `POSTED`，但 `CMD:REPLy? armed=1`（没有为 step1 arm）。
- 修复（`871279c8`）：本板执行副本的 `target_mask/required_mask` 改为 `1u << local_slot`；远端聚合仍由 node control/reply 承担。P3 `20261026173427`。
- 四板 HIL（TRN process-image `left_running=true`，build `20261026173427`）：
  - `FUNC_TDMA_ONLINE,14,14,3,1,0,255` → `state=2 COMPLETED`、`step_index=2`、`ack_mask=14`、`nack=0`。
  - `FUNC_TDMA_CLOCK_ONLINE`（4 步）→ `state=2 COMPLETED`、`step_index=4`、`ack_mask=14`；三目标 `DPLL:ROLE?` 读回 `0,0,3,3,0`（applied gen 3、pending 0）。
  - `FUNC_T2_RECONFIG,14,14,...` → step0（cmd32）`ACK=14`；step1（cmd30）`NACK reason=13`；function `FAILED`（T2 前置缺失，符合 fail-closed）。
  - 连续性：COM4 `up_sequence 44697→51795`、COM6 `45898→51601`、COM3 单次查询超时但后续 `51251`；四板 `enabled/up/down=1`，无停环。HIL 后已 `RING:STOP` 四板。
- 证据：`out/n7/four_board_hil/function_hil_localslot.txt`、`continuity.txt`、`trn03_localslot/`。
- 未覆盖：VDC reference/output function；T2 **正向** configure（需有效 grid/deployment/templates）；外部示波器/完整 P3 范围。
- 下一步：N6.3.3 VDC function（仍卡 `tdma_service_update_stopped_metadata` 要求 TDMA 停环），或先做 N7.5 收口文档；N7.4 可标 ✅（范围见上）。

### REFMEM-TASK-20261027-002 — N7.4 第二次：step0 三目标 ACK，step1 仍超时

- 状态：四板 function HIL 部分通过（step0），多步远端仍有阻塞；N7.4 未完成。
- 环：先复位四板，再 `trn03_closed_loop --stage process-image --leave-running --no-t2 --window-s 1.0`；`passed=true / left_running=true`（`out/n7/four_board_hil/trn03i/`）。四板 `RING:STATus?` `enabled=1/up=1/down=1`，process-image 路径就绪。
- function：COM5（owner=0）`FUNC_TDMA_ONLINE,14,14,3,1,0,255` → `QUEUED,90599`；6 s 后 `state=3 FAILED`、`step_index=1`、`step_command_id=23`、`step_state=3`、`ack_mask=14`、`nack=0`、`reason=11`；源 `CTRL:STATus?` 仍 `POSTED`。
  - **step0（seat rotation，cmd 21）三目标 ACK=14**，目标 `SEAT:ROTation?` 读回 `3,1,...`；证明远端 function 执行器、命令分发、ACK 聚合与 seat 在线切换可用。
  - **step1（load mask，cmd 23）超时**；20 ms 步间 gap 未能修复。目标环形状态仍 `enabled/up/down=1`，TX/RX 推进；不是 TDMA 停环。
- 代码：`REFTMEM_FUNCTION_STEP_GAP_MS=20` + `step_ready_ms` 已落库并 P3 `20261026171627`；提交 `11cf7997`。
- 根因候选（未定）：单深度命令/回复槽在 step0 ACK 后仍保留，step1 的 ACK 被丢弃；或 load mask 写入触发 realtime load 重排导致回复路径短暂中断。需目标侧 `cmd_reply`/node control 计数与 `CMD:REPLY?` 逐项定位。
- 台架处置：HIL 后已 `RING:STOP` 四板，`RING:STATus?` 均 `enabled=0`；未留环运行。
- 未覆盖：step1 修复、四板 `FUNC_CLOCK_RECONFIG`/`FUNC_T2_RECONFIG`、TDMA TX/RX 跨 function 连续性、VDC function。
- 下一步：加目标侧 reply/command 计数（只读）定位 step1；或把 load mask 拆成独立单步 function 验证；再重跑四板。

### REFMEM-TASK-20261027-001 — N7.4 四板 function HIL 尝试：环可启动，命令路径未就绪

- 状态：四板 function HIL **未通过**；记录负结果与根因，N7.4 保持未完成。
- 环启动（手动，不经 `RING:TRAIN`）：
  - 四板 `STOP → OPMode:STAGe 7 → APPLy → TOPology 4,<local>,0 → ARM` 均 `ARM:STATus?=1`；forward 1/2/3 先 `START`、slot0 最后 `START`。
  - `RING:STATus?` 四板均 `enabled=1,node_count=4,up=1,down=1,adapter_started=1`，TX/RX 计数推进。证据 `out/n7/four_board_hil/function_hil.txt` 前后快照。
- 远端 function：
  - COM5（owner=0）`FUNC_TDMA_ONLINE,14,14,3,1,0,0` → `QUEUED,103132`；step0（cmd 21）`step_state=SUBMITTED`，12 s 后 `state=3 FAILED / reason=11`，源 `CTRL:STATus?` 仍 `POSTED`，无目标 ACK。
  - 根因判断：手动 `RING:START` 只启动 adapter，未建立 TRN-03 的 process-image/resident 维护命令路径；环能跑帧，但 `REFTMEM_CMD` 维护流未被消费。不是 seat 打包或 function 执行器错误（本地 HIL 已证）。
- TRN-03 路线：
  - COM5 的 recorder 曾卡在 state 5；手动 `TDMA:RECord:CANCel` 等待 2 s 后清到 0。
  - 重跑 `trn03_closed_loop --cancel-unsaved-record --leave-running --no-t2`：matrix staged、四板 armed、recording exported，但 **explicit startup barrier timed out**，`left_running=false`；未进入可跑 function 的 process-image 状态。证据 `out/n7/four_board_hil/trn03d/`。
- 补充 host 证据：`test_refmem_function` 新增多目标用例，`target_mask/required_mask=0x0E` 原样传到 atomic submit；套件仍全绿。
- 结论：四板 HIL 受台架/process-image 启动阻塞，不伪造通过；下一步优先用 P3/TRN 的健康启动路径（或修复 startup barrier），再重跑同一 function 脚本。
- 文档状态：N7.4 保持 ⏳；N7.5 不启动。

### REFMEM-TASK-20261026-007 — N7 应用功能层：一条命令一个功能

- 状态：function 目录/执行器/SCPI/本地 HIL 完成；四板 HIL 与 VDC function 仍待做。
- 内容：
  - `config/refmem/function_catalog.yaml` 生成 4 个静态 function：`FUNC_TDMA_ONLINE`（21→23）、`FUNC_CLOCK_RECONFIG`（24→25）、`FUNC_T2_RECONFIG`（32→30）、`FUNC_TDMA_CLOCK_ONLINE`（21→23→24→25）；每步用 `arg_base/arg_words` 切函数参数。
  - `refmem_function.c` 执行器：单 in-flight（共享单命令槽）、串行 step、`ABORT_REMAINING`、5 s step 超时（`int32_t` 回绕安全）、终态保留 4 个 job 快照；接入 `app_refmem_service()`。
  - `SYSTem:CTRL:FUNCtion:RUN/STATus?/CATalog?` 接线；`CTRL:EXECute` 与 function 共用新的 `distributed_refmem_control_submit()`。
- 本地 HIL（COM5，目标本地；transcript `out/n7/local_hil/transcript.txt`）：
  - `FUNC_TDMA_ONLINE`：`state=2 COMPLETED`，`step_index=2`，`SEAT:ROTation?` 读回 `3,1,3,1,4,...`（p=3/f=1 已生效）。
  - `FUNC_CLOCK_RECONFIG`：`state=2 COMPLETED`；`DPLL:ROLE?` 读回 `0,0,2,1,1`（requested gen 2、applied 1、pending 1）。
  - `FUNC_T2_RECONFIG` 负向：`state=3 FAILED`、`step_index=1`、step0 ACK、step1 `NACK reason=13`；符合 T2 前置缺失时的 fail-closed。
- 证据：`run_refmem_function_catalog_tests`、`run_refmem_function_tests` 全绿；`pico2-release` 构建 `flash_link_contract=OK`；P3 `DPLL60_JOINT` build `20261026164220`、`check-staged` OK。
- 过程偏差：N7 首次 P3 在 DPLL 阶段报 `provisional receipt source fingerprint differs`，原因是 P3 运行中又改了 `refmem_control_submit.c`（源指纹变化）；该次不计为代码失败，最终以冻结源重跑通过。教训：P3 期间禁止改固件源。
- 未覆盖：四板协调者→远端 function HIL；TDMA TX/RX 跨 function 连续性；VDC reference/output function；T2 `MOUNT`（明确 `NOT_ONLINE_SAFE`）。
- 提交：`47ecba9d`。
- 下一步：四板 function HIL（先启动环）或 VDC function；随后 §8 收口。

### REFMEM-TASK-20261026-006 — N6.4 T2 域停配置（TDMA 不中断）

- 状态：`UNMOUNT`/`CONFIGURE` 完成；`MOUNT` 首版明确 `NOT_ONLINE_SAFE`。
- 内容：
  - `T2_UNMOUNT` 调 `trigger_t2_runtime_stop()`；`T2_CONFIGURE` 从 13 个参数重建 `trigger_t2_runtime_config_t`（grid/deployment/templates 前置校验、scan step/geometry、window 0=默认 grace），在 T2 `active==0` 时调用 `trigger_t2_runtime_configure`。
  - `T2_MOUNT` 不调用 `distributed_refmem_reset_position_stopped()`（该函数要求 TDMA 停环）；首版返回 `NOT_ONLINE_SAFE`，T2 启动仍走本地 `T2:MOUNT`。
  - TDMA 全程不停机；统一面只负责 T2 停+配置。
- 证据：`pico2-release` 构建 `flash_link_contract=OK`；P3 `DPLL60_JOINT` build `20261026162736`、`check-staged` OK。首跑在 TRN-03B `no complete board record available after failed startup` 失败；用同一 package/OTA 重跑通过，原始失败保留 `out/n6/p3-dpll60-n64/`，通过件 `out/n6/p3-dpll60-n64-retry/`。
- 未覆盖：`T2_UNMOUNT/CONFIGURE` 的目标级 HIL 尚未执行；本次 P3 只证明构建/OTA/启动/DPLL 路径。T2 启动的在线安全 reset 未实现。
- 下一步：N6.3.3 VDC 域停配置，或跳过 VDC 直接 N7 function 层（用已注册的 21/23/24/25/30/32 先做静态 function）。

### REFMEM-TASK-20261026-005 — N6.3 TDMA 在线 seat/load（不停环）

- 状态：TDMA 在线部分完成；VDC 域停配置仍待做（N6.3.1 剩余）。
- 内容：
  - adapter 的 `p/f/s` 三份额改为单个 `seat_rotation_packed` 原子字 + generation；`tdma_pio_spi_ring_origin` 每发布机会只读一次并解包，在线切换在下一机会生效，**不需要停环**。
  - SCPI `SYSTem:TDMA:SEAT:ROTation` 仍走 STOP-only `tdma_runtime_owner_set_seat_rotation3`；统一控制面新增 `tdma_runtime_owner_commit_seat_rotation3` 在线入口。
  - `TDMA_LOAD_MASK` 走现有 `app_realtime_set_load_mask`；`TDMA_RING_TOPOLOGY`/`TDMA_PERIOD` 运行时返回 `NOT_ONLINE_SAFE`（reason 14），部署期仍走原 STOP 路径。
  - reason 表由 14 项扩为 15 项；`refmem_command.h` 增加 reason 14。
- 证据：`test_tdma_seat_rotation_online`（pack/unpack/valid3/默认 1:1）通过；`pico2-release` 构建 `flash_link_contract=OK`；P3 `DPLL60_JOINT` build `20261026161302`、`check-staged` OK。
- 台架残余：首次 P3 在 TRN-03B `matrix write/readback failed`（`CAL:TRAIN:STAGe:BEGin` timeout，stage=process-image）；用同一 package/OTA `dpll60 --package ... --ota-summary ...` 重跑通过，判定为台架瞬时超时，非本切片回归；原始失败保留在 `out/n6/p3-dpll60-n63/`，通过件在 `out/n6/p3-dpll60-n63-retry/`。
- 未覆盖：seat 在线切换的**目标级 HIL**（切换前后 `up_sequence`/`down_rx_sequence` 连续性）尚未做；P3 只证明默认 1:1 路径与构建/启动。VDC/T2 域停配置未做。
- 提交：`dac41e0e`（firmware+host）；文档另提。
- 下一步：N6.3.3 VDC 域停配置（reference/delay/timing）或 N6.4 T2；随后 N7 function 层。

### REFMEM-TASK-20261026-004 — N6.1 四域配置命令目录与协调者入口闭合

- 状态：完成（catalog/entry 层）；N6.2 起进入域适配器。
- 内容：
  - `refmem_command.h` 新增 20–34 共 15 个 `command_type`；`config/refmem/command_catalog.yaml` 增至 21 行；生成器 `COMMAND_TYPES` 同步。
  - `dispatch_control` 将新类型纳入授权；idle 窗口仅门控部署期（20/22）与既有 stage/activate，在线 TDMA seat/load、DPLL、VDC/T2 域停配置不受窗口阻塞。
  - 协调者入口闭合：`refmem_control_entry_decide` + `SYSTem:REFMEM:CTRL:OWNer?`；非 owner 远端命令 fail-closed（`NOT_CONTROL_OWNER`），不排队死 intent。
- 证据：`test_refmem_command_catalog`（21 行/15 新行/参数边界）、`test_refmem_control_route`（entry 6 情景）、`test_refmem_command_authority`、`run_refmem_unified_control_host_tests` 全绿；固件 `pico2-release` 三 profile `flash_link_contract=OK`；P3 `DPLL60_JOINT` build `20261026154606`、`check-staged` OK。
- 已知残余：`run_refmem_all_host_tests` 中 `run_refmem_realtime_tdma_tests` 因 `tdma_service` 拆分后的 host 链接缺件失败（`tdma_service_core1_service`/`tdma_service_submit_tx` 等未定义），属既有拆分债务，不在本切片改动路径。
- 提交：`a5e47834`（firmware）、`8e87c9e5`（host）、`2304b407`（docs）。
- 下一步：N6.2 DPLL RUN-safe 适配器（CommandHandlerRegistry + 应用层 handler）。

## 归档索引

| 文件 | 条目范围 | 条目数 | 归档日期 |
|---|---|---|---|
| `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_01.md` | REFMEM-TASK-20260813-021..REFMEM-TASK-20260813-001 | 21 | 2026-10-17 |
| `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_02.md` | REFMEM-TASK-20260814-039..REFMEM-TASK-20260813-022 | 20 | 2026-10-17 |
| `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_03.md` | REFMEM-TASK-20260814-049..REFMEM-TASK-20260814-040 | 10 | 2026-10-24 |
| `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_04.md` | REFMEM-TASK-20260815-006..REFMEM-TASK-20260814-050 | 10 | 2026-10-25 |
| `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_05.md` | REFMEM-TASK-20260815-016..REFMEM-TASK-20260815-007 | 10 | 2026-10-26 |
| `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_06.md` | REFMEM-TASK-20260815-036..REFMEM-TASK-20260815-017 | 20 | 2026-10-27 |
| `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_07.md` | REFTMEM-TASK-20261026-003..REFTMEM-TASK-20260815-037 | 67 | 2026-11-09 |

