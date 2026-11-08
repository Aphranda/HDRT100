# 校准域任务记录

Status: Active
Domain: CALIBRATION
Canonical: `docs/calibration/CALIBRATION_TASK_PROGRESS.md`
Related: `docs/calibration/CALIBRATION_DOMAIN_TODO.md`, `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`, `docs/calibration/CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/vdc/VDC_TASK_PROGRESS.md`
Last updated: 2026-11-09

本文档记录校准域从方案、粗捕获到双向测距和 VDC/DPLL 接入的实际进展。记录中的 HIL
结果必须绑定 build、拓扑、profile、接线和证据目录；未绑定这些上下文的数字只能作为
诊断快照，不能作为 active calibration 或产品精度承诺。

## CAL-TASK-20261109-001 - 修 P2 CODED_STOP 的 training-stage 恢复（CONSOLE-045）

- **症状**：P2 进入 trial body、1 个 trial 通过后，`APP_CTRQS_P2_CODED_STOP` 里
  `calibration_manager_restore_training_stage()` 返回 false → `hard_fail_reason=NOT_ONLINE_SAFE`
  → `status=2 (STOPPED)`，阶梯停在 P2。
- **根因**：该函数在**没有持久化 stage** 时直接返回 false。台架是产品默认档 + 无 TRN-03 记录，
  P2_PREP 清掉的是 RAM 里的 stage（持久记录本来就不存在），所以"恢复"没有对象却被当成失败。
- **修复**（`components/calibration_manager/src/calibration_manager.c`）：先读 store status，
  `persisted_valid==0` 视为"没有可恢复内容"直接返回 true；只有"记录存在但无效/恢复失败"才返回 false。
  同步更新 `application/src/app.c` P2_CODED_STOP 的注释。
- **实板验证**（build `20261108164418`，四板）：P1 `ladder 1 8000 8100` → COMPLETE（ladder_idx=1）；
  随后 `p2 8001 8101` → 四板 `status=1, trials_done=1, passed=1, failed=0, abort=0, ladder_idx=2`；
  `verify 2 8101 8001 1 1` → **PASS**，证据
  `out/calibration/guide-10k/evidence/20261109-010527-verify-2/`。
- **残余**：P3 的 CONFig 目前被 `-200,"TDMA_GUIDE_CONFIG"` 拒绝（命令面 submit 不受理），阶梯仍停在
  P2 之后；属另一个症状，待查 `SYSTem:REFMEM:CMD:STATus?`。修复未提交（固件 lane，等 P3 凭证）。

## CAL-TASK-20261108-050 - M5：10 MHz 训练敏感性取证（OPEN，可复跑）

- **10 MHz 环可形成（无 TRAIN）**：`tdma_start_ring.py --level 7 --skip-train --bringup` →
  `out/calibration/guide-10k/bench/10m-ring-m5/summary.json` **passed=true**，四板
  `enabled/up/down/adapter=1`、`baud=10000000, period=1000000`。
- **10 MHz 的 CTRQS P1 在窗口 ARM 阶段就失败**：build `20261108041830` 上 `ga 1 3000 3100 7 ...`，
  状态序列 1→4(ARM)→26(RESTORE_WAIT)→27(REBUILD)→3(FAILED)，`trials=0/4`、`reason=14`，窗口后四板
  `enabled/adapter/baud=0`。证据：`out/calibration/guide-10k/evidence/20261108-124246-action1-gen3000/`。
  说明：10k 的 REBUILD 修复只覆盖"replay 未成环"，不覆盖 10M 的 **ARM 本身失败**。
- **TRAIN 证据缺口**：**本 build 没有尝试过 10M TRAIN**（`10m-ring-m5` 显式 `--skip-train`，
  `10m-prep-m5` 只跑 steps 0–4）。仅有的两次 10M TRAIN 都在旧 build `20261107175157` 上失败：
  `10m-order-trace-20261108.json` 的 TRAIN 在 ARM 后立即 `-200`（当时 `adapter_started=0`、
  `last_reason=9 ADAPTER_START_FAILED`）；`10m-ring-20261108/summary.json` 的 TRAIN 在 ARM 前也
  `-200`（`enabled/adapter` 前置不满足）。因此 **cycles=4/16 的 result/reject 仍 UNKNOWN**，
  `failed_mask=0x05` 的"4-cycle 失败"（旧 build 的 demo）无法在本轮归因。
- **下一步（10M owner 或下轮）**：先拿到"ARM 后 `adapter_started=1` 且 `data_enabled=0`"的稳定顺序
  （`--steps 0-6` 后立刻抓 `RING:STATus?`），再在该状态发 `RING:TRAIN 4/16`，抓
  `RING:TRAIN:STATus?` 的 state/result/reject 与 `RING:TRAIN:STATus?` 的 `request_seq` 变化。
- **台架已恢复**：四板 reboot 后 10 kHz 自组环（`.../20261108-124404-boot-m5-restore/`，boot=PASS）。

## CAL-TASK-20261108-049 - CONSOLE-042：Q 段恢复在 replay 未成环时回退 REBUILD（校准侧关闭，台账待 console owner 合并）

- **取证（只读，先做）**：复跑 10k P1 并每 2 s 抓四板 `BOOT:PLAN?`/`RING:STATus?`/`GUIDE:PLAN?`：
  `out/calibration/guide-10k/bench/restore-trace-20261108-114735.json`。关键事实：replay 全程
  `fail_step=0, boot_done=1, next_step=8`（自认成功）；t=33.8–52.1 s 四板是**半环**
  `enabled=1/up=1/down=0`、`applied==config=9`；t≈54.4 的 20 s 完整度看门狗把半环停掉
  （`cfg 9→10`），此后 `enabled=0`；45 s 后四板 `progress_status=3 / reason=14 / abort_site=0`。
- **根因**：boot replay 只报“接受”不保证成环——它的 START 成功了，但只形成 `up=1/down=0` 的半环；
  20 s 完整度看门狗（`app_guide_ring_watch_service`）把半环停掉后，旧代码以
  `app_ctrqs_boot_plan_present()` 为 REBUILD 门槛，有 boot plan 时无回退 → 盲等 45 s 报
  `NOT_ONLINE_SAFE`。（此前写的“applied 停在 STOP”与 trace 不符，已按实测更正。）
- **修复**（`application/src/app.c`）：`APP_CTRQS_RESTORE_WAIT` 不再以 boot plan 为门槛；replay 定局
  （`boot_done`）后若 `restore_formed()` 仍假，抑制 replay（`s_boot_replay_suppressed`）并回退到显式
  REBUILD（环已静默则立即，否则等 45 s 截止，避免拆掉正在形成的环）；失败路径 `abort_site=26/27`，
  成功不置 `abort_site`（strict verdict 以 `abort_site!=0` 判失败）。26 路径保留 finalize 的 action
  verdict；27 路径保留已发布的非零 reason、只在没 reason 时填 `NOT_ONLINE_SAFE`（不再吞掉 ARM 类
  原始失败码）。**同轮修掉状态泄漏**：`s_boot_half_ring` 原只置不清，会让后续 `BOOT:PLAN?` 恒为
  `half_ring=1`、`BOOT:TOPO? accepted=0`、`boot_verdict` 误判——现在在 `app_guide_window_close()`、
  恢复成形、rebuild 成形三处清零。
- **实板验证（P3 最终 build `20261108055416`）**：
  `out/calibration/guide-10k/bench/restore-trace-20261108-140801.json`：t≈64 s 四板 `prog=1/1`
  （COMPLETE）、`reason=0`、`abort=0`、环 `1/1/1/1`；赛后四板 `BOOT:PLAN?` `half_ring=0, boot_done=1,
  fail_step=0`。同结论在 build `20261108050455`（`...-131349`）与 `20261108040639`（`...-121518`）复现三次。
- **P3**：最终 `out/HardwareAcceptance/20261108/p3-restore4`（build `20261108055416`）与
  `p3-restore2`（`041830`）均为 **PASS_WITH_WARNINGS**（ERROR 0）；quick-diagnostic 诊断档
  （`strict_gates_passed=false`，含 450 s 预算超出与 `DEBUG_BOUNDED_FORCE_CONTINUE`），凭证结构按
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC` 接受。固件提交 `04040e59`（`722c3b29` 为首版）。
  期间一次 P3 因四板 OTA `durable 512` 退化失败（`OTA:FT?=10,2,17,17,0,256,0,1`），已用整片擦除
  （`--flash-size 0x1000000`）+ 重刷 + 重写板号恢复，恢复 runbook 见单口指南 §6。
- **残余**：`CONSOLE-042` 台账行仍 PENDING（console 两文件是另一会话未提交改动，本会话不改）；
  10 MHz 4-cycle 训练敏感性仍 OPEN（见 CAL-TASK-20261108-050）。

## CAL-TASK-20261108-048 - C11 复审闭环：单口启环契约转 active（build `20261107175157`）

- **二次 C11 只读复核**（子代理 `a61aafc3`）：APPROVE_WITH_NOTES；转 active 前须关 M1–M3
  （clear 物理回读、C/T 证据链、rotate 擦除 nonce）与 M4/M5 口径。结论与逐条处置见
  `docs/check/submissions/TDMA_BOOTPROFILE_CROSS_REVIEW_01.md`。
- **M1**：`tdma_boot_profile_store_erase()` 改为两扇区 `drv_flash_is_erased()` 全 0xFF 才算 CLEARED；
  非空或不可读 → `VERIFY`；host 用例补"垃圾扇区必须被物理擦除"。
- **M2**：探针新增 `ct <gen> [tag]`，只开 COM5 发 `GUIDE:CONFig`，归档 `command_port`/`response`/
  host fingerprint，四板读回。
- **M3**：rotate 目标擦除折入 nonce（与 program/clear 一致），避免撕裂重试命中日志去重。
- **复验**（build `20261107175157`）：P3 **PASS_WITH_WARNINGS**（INFO 35 / WARN 2 / ERROR 0；
  `out/HardwareAcceptance/20261108/p3-final3/`）、零口自组 **PASS**
  （`.../20261108-020250-boot-final4/`：`steps=8, fail_step=0, boot_done=1, up/down/adapter=1/1/1`）、
  只开 COM5 的单口 C/T **PASS**（`.../20261108-020353-ct-final4/`：四板 `gen=960`，59.5 s）。
- **提交**：`7a357d68`（固件 M1/M3 + P3 凭证）、`33e573c0`（探针 `ct` + store 用例）。
- **收口**：契约 `TDMA-BOOTPROFILE-01` 由 `pending` 转 **active**（复审结论与 M1–M3 关闭证据已在提交单）。

## CAL-TASK-20261108-047 - M5：R26 首写闭环（新代 TRN-03 stage 首次落 flash）

- **做法**：在 build `20261107162842` 的四板上跑
  `trn03_stage.py --board-id <4> --config out/HardwareAcceptance/20261108/p3-final2/trn03-matrix.json
  --expected-build 20261107162842 --commit`（`out/calibration/trn03-r26/summary.json`）。
- **结果**：`persistence_after_commit` 四板一致：`persisted_valid=1, reject=0,
  record_generation=1794072390, record_sequence=1`；直接 SCPI 复核
  `READ:CALibration:TRAINing:STAGe:PERSistence?` =
  `"TRN03NVS",1,1,0,0,1794072390,1,1487355668,1794072390,3671493230,...`。
  `record_sequence=1` 即新 generation 的第一次写，R26 的"从未写 flash"不再成立。
- **处置**：`CALIBRATION_CTRQS_CHECKLIST.md` 的 R26 行由 OPEN 改为 CLOSED（附上证据路径）。
- **注**：commit 的 SCPI 响应在工具侧记为 `<timeout>`，但 `errors_drained_before/after` 均为
  `0,"No error"` 且持久化读回一致；判定以读回为准（与既有"提交读回"口径一致）。

## CAL-TASK-20261107-046 - 单口启环收口：同一 P3 镜像 + 运行时 boot profile provisioning

**章程**（用户 2026-11-07 裁决）：严格单口启环（只用协调口 COM5/NO.1），且**不接受第二份镜像**——
P3 验收镜像就是台架镜像，台架必须跑最新代码的 P3 构建。

**先查后做（推翻了交接 §3.1 的旧假设）**：

- REFMEM 远程命令面**不能自举环**：远程提交要求 ring transport 已就绪（`refmem_control_submit.c`），
  命令目录没有 OPMode/BRINGUP/ARM/START 条目，`TDMA_RING_TOPOLOGY` 远程 handler 恒拒（id 20）。
  环停时"只开 COM5 发命令起环"不可能，不是补几行目录能解决的。
- 可行路线是**开机重建**：v2 地图的 boot profile store（CALIBRATION_NVS 双扇区）+ 运行时
  provisioning，上电后四板无任何主机命令自组 10 kHz 环，之后只用 COM5。台架编译开关
  `PROJECT_TDMA_GUIDE_AUTOFORM` 只保留为历史证据。

**实现**（契约 `TDMA-BOOTPROFILE-01`，pending）：

- `SYSTem:TDMA:BOOT:PROFile <level>,<nodes>,<slot>,<flags>,<topology_crc32>`（STOP-only **fail-closed**，
  环快照读不出也拒绝）、`...:PROFile?`（含扫描诊断 4 字段）、`...:PROFile:CLEar`；generation 由
  store 重扫决定（严格大于最新可读记录），flags 必须含 `BRINGUP_NO_CAL`，node_count 受
  `PROJECT_NODE_CAPACITY` 限制，错误码分层（ARGUMENT/RING_RUNNING/RANGE/FLAG/UNSUPPORTED/
  GENERATION/FLASH/VERIFY/RECORD）。
- `tdma_boot_profile_store_next_generation/erase`；无法解析的扇区在 commit 前擦除；
  `BOOT:PLAN?` 追加 `boot_done`/`boot_next_step`（17→19 字段），给 P3 重跑提供"boot 服务已结束"屏障。
- RAM：provisioning 让原本被 GC 的 commit 路径参与链接，v2 RAM 首次溢出 128 B；把 `s_program`
  从整擦除扇区（4 KiB）改为按记录跨度计算（256 B）。
- 测试：新增 `run_tdma_boot_profile_store_tests.ps1`（v2 地图、真实 commit/rotate/erase/next_generation）
  与 `run_tdma_boot_plan_tests.ps1`（profile/plan/sequence/stage/region），均注册进
  `run_host_unit_tests.ps1`；`tests/python/test_guide_boot_plan.py` 覆盖探针判据；修正
  `test_tdma_boot_sequence.c` 与现行序列（TOPOLOGY 在 STOP 前）不一致、`run_flash_transaction_tests.ps1`
  缺 `components/algo/inc`。

**实板发现的五个真 bug**（都不是理论问题）：

1. flash 事务策略：`FLASH_TRANSACTION_REQUESTER_CALIBRATION` 只允许 calibration 子区，boot profile
   在 CALIBRATION_NVS 的 4·ERASE 偏移 → 写入被 PERMISSION 拒绝（`TDMA_BOOT_PROFILE_FLASH`）。
2. store 的 XIP 绝对地址漏了子区相对偏移 → `scan_sector` 一直读 calibration store 的 TRN-03 记录
   （object 不匹配），provisioning 恒 `RECORD`。host 单测的 stub 把绝对/相对坐标映射到同一窗口，
   所以之前测不出来。
3. 复用 build 目录导致 `PROJECT_BUILD_ID` 不更新：OTA 以 build id 判等，**同 id 时不换镜像**，
   `*BUILD?` 因此可能"看起来没变"。修法：最终验收用新 build 目录。
4. `CLEar` 谎报：flash 完成日志按请求指纹去重（指纹含 `store_generation`），clear 固定用 1 →
   第二次 clear 是 COMPLETED 重放、无物理擦除，`CLEARED` 后记录仍在。修法：擦除后回读两扇区，
   非空即 `VERIFY` 拒绝。
5. 同参数重写被去重：commit 的身份只有 profile generation，clear 后重写同一 profile（同 gen/同内容）
   被当成重放 → `VERIFY`。修法：commit/erase 增加一次性 `nonce`（SCPI 传 boot uptime）折入指纹。

**证据（最终镜像 build `20261107162842`，源码含 F1–F16 全部修复）**：

- P3：`out/HardwareAcceptance/20261108/p3-final2` **PASS**（INFO 34 / WARN 0 / ERROR 0 / FATAL 0；
  `--preset pico2-v2-factory-candidate --tdma-only --build-dir out/build/p3-acceptance-20261108-002825
  --reuse-topology out/m1-scan/p0t-night3/summary.json`），P0T 为同 build 独立重测（全环
  NO.1→2→3→4）。
- 零口自组：四板 provisioning（level 23 / nodes 4 / slot=NO−1 / flags=1 / crc=0x642F84A7）后按
  从板先、协调板最后重启，无任何起环 SCPI；`guide_cmdplane_probe.py boot final3` **PASS**：
  `out/calibration/guide-10k/evidence/20261108-013330-boot-final3/readback.json`（四板
  `action=START_REF/LISTEN`、`steps=8`、`fail_step=0`、`boot_done=1`、`up/down/adapter=1/1/1`，
  帧总量 68→72）。
- 单口 C/T：只开 COM5 发 `GUIDE:CONFig 950` 返回 `"OK"`，58.8 s 后四板 `PLAN? gen=950`（`valid=1`），
  证据 `.../20261108-013432-ct-final3/readback.json`。`SET_FLIGHT_MODE` 修复生效。

**OTA 阻塞归因（已解除）**：最终 P3 的 OTA 在 durable=512 处失败（`FT?` 为 MODE/17），换旧镜像
（build `20261107135640`）同样复现 → 非固件问题；根因是台架连续 factory 刷写后 flash/OTA 元数据退化。
恢复：`picotool_flash.py --full-erase` + 重刷 factory + `SYSTem:BOARD:NO <n>` 逐板写回；干净 flash 上
OTA 单板 PASS，随后四板 P3 一次通过。**代价**：整片擦除清掉校准/板号，需重写板号（本次已恢复 NO.1–4）。
- **C/T 单口下发：已修复并在最终镜像复验**。原始症状：自组环上只开 COM5 发 `GUIDE:CONFig`/`TRIGger`
  返回 `"OK"`，但 `REFMEM:CMD:STATus?` 停在 `q_depth=1, enq=1, deq=0, tx_ok=0`；60 s 内四板
  `PLAN?` 不变。只读子代理 `792dc544` 追到注入点：参考板 beacon 只在"无飞行 TX 排队"时才带一个维护
  chunk（`tdma_maintenance_runtime.c:135-195`），而 RefMem 飞行同步每个环回填 FIFO（10 kHz/level 23
  每 600 ms），于是命令永远发不出去。**修复**：boot 序列新增 `SET_FLIGHT_MODE`（STOP 后、ARM 前设置
  resident process image），使开机自组环天然具备命令面窗口；序列由 7 步变 8 步，
  `test_tdma_boot_sequence` 同步。**最终镜像复验**：单口 CONFig 58.8 s 四板 `gen=950`（见上方证据）。

**下一步（按 ROI）**：① 契约 `TDMA-BOOTPROFILE-01` 从 pending → active：F1–F16 修完后提交 C11 复审，
把复审结论与 build `20261107162842` 的三份证据（P3 / 零口 / 单口 C/T）写进提交单；② 固件按 lane 提交
（`check-staged` 绑定 P3 指纹）；③ M5 残余：R39（同一 ARM 会话 ≥2 样本帧活性，可用本次 C/T 归档的
`total_up_tx/down_rx` 单调性做起点）、R26（新代 stage 首写 0/1/1）、M6.8（先 stage 7 再 P1 的全 10 MHz
回归）；④ 收口时把 `CALIBRATION-SGATE-01` 的 pending 状态与本次证据对齐。

**当前台架**：四板 build `20261107162842`（P3 PASS 镜像），boot profile 已 provisioning（level 23 /
nodes 4 / crc 0x642F84A7），上电零口自组成 10 kHz 环；固件已具备提交条件（待 `check-staged`）。

## CAL-TASK-20261105-044 - M1 板端对表扫描：执行器跑通、检测未收敛

**本轮新增**（bench 开关 `PROJECT_TDMA_GUIDE_SCAN`，默认 OFF；产品镜像行为不变）：

- 纯逻辑 `components/tdma/{inc,src}/tdma_guide_scan.*`：12 对表（`N×(N−1)`）、按本地 slot
  派生 `DRIVER/RECEIVER/IDLE`、活动位图、单闭环聚合（`NO_ACTIVITY/OPEN_CHAIN/BRANCH/
  DUPLICATE_EDGE/MULTI_LOOP`）；host 用例 `tests/unit/test_tdma_guide_scan.c` +
  `tests/python/test_tdma_guide_scan.py`（7 组，覆盖反向环与 anchor 旋转）。
- boot 执行器第二段 `app_guide_scan_service()`：自组完成后进暗窗，12 个 5 s 时隙，每时隙
  `STOP → 2 节点拓扑 + bring-up 拓扑 → 等 applied==config → PROBE 10 → ARM(带 stopped
  屏障) → TRAIN 512 → START → 接收窗轮询 → STOP`；收尾清 probe 并重放冻结 boot 步骤恢复
  4 节点环。
- 只读出口 `SYSTem:TDMA:GUIDE:SCAN?`（22 字段：state/pair/role/位图/observed/skipped/
  failures/fail_phase/arm_*/trials_*/max_late_ms/stall_ms/帧字增量）。

**实板结果**（build `out/build/m1-scan`；日志 `out/m1-scan/run-20261105-13*.txt`）：

- 执行器四板一致：`trials_started=12`、`trials_armed=6`、`skipped=0`、`failures=0`、
  `max_late_ms=0`、`stall_ms ≤ 23`；收尾后 `RING:STATus?` `enabled=1 node_count=4`、
  `BOOT:PLAN?` `half_ring=0`（环恢复）。
- 检测**未收敛**：严格阈值（`Δframes≥10 或 Δwords≥8`，与宿主 P0T 同量级）下只有 NO.3 报
  1 条（bit 11，`frames_delta=0`、`words_delta=15`）；放宽到"任一计数变化"时曾出现 2/4 条，
  且两轮命中的 pair 不同（`(1,2)` vs `(3,2)`）→ 不得写成 M1 通过。

**三个实测根因（已修，写进 playbook §9.19）**：

1. 相位推进条件为"相位号 ≤ FINISH"，而 SETTLE 编号 10 大于当时 FINISH 8 → 状态机在 SETTLE
   永久停住，`trials_armed=0`、`skipped=6`、一个 ARM 都没发出；
2. `trial_start_ms` 只在 resync 分支前移、FINISH 不前移 → 第 0 对之后所有相位同时过期，
   一次服务调用连续跑完 STOP/TOPOLOGY/ARM，等于没有 settle/arm 预算；
3. 2 节点拓扑只下 refmem 拓扑、没下 `tdma_runtime_owner_set_bringup_topology(2,0)` →
   up/down group 与 flight map 缺失，暗窗后环恢复不了（`node_count=0`、`half_ring=1`）。

**未完成**：接收侧检测的可重复性；跨板位图聚合（单口 Q 段）未实现；join-TX 抑制门仍缺。
**下一步**：先用宿主 `--adjacency-only` 在同一 build 上取一次 12 对基线，确认板端本地驱动的
探针是否真产生 ≥10 帧，再决定延长 burst（probe phase/START 提前）还是改判据；不要继续盲调时隙。

**提交状态（本条目写入时）**：代码已 `git add` 且 `check-staged` 通过（receipt build
`20261105055647`、scope `DPLL60_JOINT`、sources=983），但整仓 pre-commit 被并行 OTA 会话的
未跟踪文件 `tools/ota_keys/ensure_debug_key.py`（工具治理判“unregistered file”）阻断，因此本批
代码与文档**尚未提交**。解除方式：该会话提交/登记该工具，或用户裁定归属后按其分类补登
`tools/tool_workflow_triage.json`；不要用 `--no-verify` 绕过。

**台架状态（本条目写入时）**：四板已从 bench 镜像（v2 + AUTOFORM + SCAN）刷回标准 v1 构建
（`out/build/dpll60-latest/DHRT100_FACTORY.uf2`），板号已重写 NO.1..4，dpll60 凭证通过
（`out/m1-scan/dpll60-r2`）。要复跑 M1，需按 §9.18 重刷 bench 构建并重写板号。

**续（同会话第 2 轮，2026-11-05 下午）——检测根因再收窄**：
- **宿主基线**：`calibration_ring_topology.py --adjacency-only --no-assign`（v1 标准镜像、同四板）
  12 对里 4 对检出，每对 `rx_words 9844–11235`、`rx_frames=0`；环序 NO.1→NO.2→NO.3→NO.4
  （`out/m1-scan/p0t-host-baseline/summary.json`）。**真检出是上万字**，板端 0–15 字是噪声。
- **probe persona 门**：`tdma_pio_spi_ring_adapter_tx.c:351-356` 要求
  `topology_probe_mode && !(flags & BRINGUP_NO_CAL)`；引导档恒带该 flag，暗窗内必须先清。
  `tdma_service_set_bringup_no_cal`（`tdma_service.c:519-540`）要求 `config_seq==applied`，
  只能在 SETTLE 后调用；放在拓扑写入后实测四板 `fail_phase=1`、`trials_armed=0`。
- **仍未解决**：清标志后四板仍是 0–9 字，burst 未出现；两板在 900 ms 的拓扑写入处抢跑
  （`fail_phase=1`）。下一步按宿主次序复刻"一次 STAGe/APPLy/PROBe 前导 + 每对只改拓扑"，
  再判断 operating profile 是否被每对的 foundation profile 重写。
- **凭证**：本轮最后一次源码改动后已重跑 dpll60（`out/m1-scan/dpll60-r4`，build
  `20261105063447`，`check-staged` OK，sources=983）；提交仍被并行 OTA 会话的
  `tools/ota_keys/ensure_debug_key.py` 阻断（该路径属冻结 legacy 根，登记也不能清，
  须由该会话把它移到 `tools/ota/ota_keys/` 并登记）。

**凭证路径的坑（本轮新增）**：`workflow_steps.py dpll60` 默认用 `pico2-release` 预设（**v1_compat**），
而 bench 镜像是 **v2_candidate**；v1 包的 run offset 与 v2 分区不符，OTA 在第二个 `STREAM:DATA`
处静默失败（裸 `SCPI_RES_ERR` 不写信道 → 300 s 超时，**不是** `erase failed: state=9`）。处置：
先把四板刷回同一地图的镜像再跑；`--flash picotool` 不产生可提交凭证。

**续（同会话第 3 轮，2026-11-05 15:00）——决定性读数是 TX**：

- 按宿主前导补了"暗窗开头一次性 `stage/apply level 7`"（宿主 `--level` 默认 7 = 10 MHz/1 ms，
  不是引导档 23/10 kHz），并把 prelude 改成带 settle 屏障的状态机（`fail_phase=102` 是屏障超时，
  实测 NO.4 在 `OPMode?` 上停在 staged=7/active=23）。
- 新增两个只读字段 `tx_delta`、`sample_started` 后，四板读数：接收侧
  `sample_started=1`、`frames_delta=0`、`words_delta=0..8`（噪声），而 **`tx_delta=0`**。
- 即：ARM/START 都被接受、接收侧在跑，但**驱动板一个帧都没发**；宿主同一四板每对是
  `rx_words≈1e4`。所以问题不再是"阈值"或"对齐"，而是 probe 模式下 TX 根本没启动；
  下一步按 TX 路径的每一个门（probe 标志、BRINGUP_NO_CAL、data_enabled、flight engine、
  PIO/DMA 使能、driver/receiver 槽位）逐项对照宿主次序。
- 凭证：源码在本轮又改动，`dpll60-r4` 已过期；提交被同一外部文件阻断，故未重跑。

**续（同会话第 3 轮末，2026-11-05 15:20）——去掉 TRAIN 后有了真 burst，但邻接仍不对**：

- **TRAIN 是有害的**：`RING:TRAIN` 把 PIO persona 换成 `CLOCK_COARSE`、卸掉 `FLIGHT_ORIGIN`，
  只有"训练后强制 STOP + 重新 ARM"才恢复（`tdma_pio_spi_phys_arm.c:828-943`、
  `tdma_ring_runtime.c:770-778`）；宿主 `--adjacency-only` 正是**不训练**的
  （`calibration_ring_topology.py:309-313`）。删掉训练调用后，驱动窗内
  `tx_delta=330–406` 帧、接收侧出现 `words_delta=314`（此前只有 0–9 的噪声）。
- **新增逐对证据出口** `SYST:TDMA:GUIDE:PAIR? <i>`（role/frames/words/tx），一轮即得 12 对全表。
  实测（`out/m1-scan/run-20261105-151902.txt`）：
  - 驱动侧 12/12 正常（330–406 帧）；
  - 期望的 4 条环边**全部为 0**：NO.1 收 NO.4 = 0、NO.4 收 NO.3/NO.2/NO.1 = 0、NO.3 收 NO.2 = 0；
  - 唯一非零接收是 NO.3 收 NO.4（对 11，314 字，**非宿主测出的环边**），且 NO.4 在自己
    驱动的对 11 也收到 370 字（自环/对端转发）。
- 结论收敛到：**2 节点配对下两板的 RX/TX 方向与物理环不一致**（或两板对同一对的时隙没对齐），
  不是阈值、不是 TX、不是判据。下一步：在逐对记录里加"本对窗口相对 epoch 的起止时间戳"，
  先证明四板确实在同一对同一时刻，再查 2 节点拓扑的 slot0/slot1 与物理 IN/OUT 的对应。

**续（第 4 轮，2026-11-05 15:35）——对齐与配置已证清白，故障收窄到"只有第一对能收"**：

- `SYST:TDMA:GUIDE:PAIR? <i>` 现在返回 `role,frames,words,tx,packed_state,start_offset_ms`；
  `packed_state` = `node_count | local<<8 | ref<<16 | enabled<<24 | started<<25 | gap<<26`。
- **对齐**：四板每对的 `start_offset_ms` 都是 `0,5000,…,55000`，逐对一致 → 时隙对齐成立。
- **配置**：驱动板 `state=50331650`（node=2/local=0/ref=0/enabled=1/started=1），
  接收板 `50331906`（local=1）→ 2 节点拓扑、角色、START 全部正确。
- **故障**（`out/m1-scan/run-20261105-153408.txt`）：
  - 物理环 1→2→3→4→1 的 4 条边，只有 **pair 0（1→2）** 收到（`words=5671`）；
  - pair 4（2→3）、pair 8（3→4）、pair 9（4→1）接收侧全 0，尽管驱动侧 tx=332–403；
  - 非边 pair 11（4→3）两侧各有 217/354 字 → 假阳性。
- 也就是说：**驱动 12/12 都在 10 MHz 发帧，接收侧只有整轮第一次 ARM 的那一对真正武装了 RX**。
  首选假设：后续每对的 ARM 复用了上一对遗留的 RX 武装状态（未强制重装 RX 几何）；
  下一轮实验：SETTLE 里除 `applied==config` 外再要求 `adapter_started==0`（证明上一对的
  adapter 已完全退休）后才重新 ARM。

**续（第 5 轮，2026-11-05 15:42）——adapter 退休屏障排除，RX 武装"只生效一次"成立**：

- SETTLE 加 `adapter_started==0` 后结果不变（说明上一对确实退休了）：pair 0 的接收侧
  `words=32742`，pair 4/8/9 仍全 0；非边 pair 11 仍是 217/225/352 量级假阳性。
- 结论升级为：**整轮只有第一次 ARM 真正武装了接收侧**；同一块板（NO.2）第 0 对收 32742 字、
  第 7 对（同为 slot 1 接收）收 0，排除了"配置/角色/对齐"。
- 代码线索：`tdma_rx_prepare_request()` 只在 `TDMA_RX_PREPARE_IDLE` 时接受
  （`tdma_rx_prepare.c:12-19`），READY 未被消费时后续请求被拒；`tdma_pio_spi_ring_adapter_tx.c:574`
  只在 stop 路径 `cancel` 它。下一轮先验证"第 2 对起 `rx_preparation` 是否停在 READY"，
  再决定是消费/复位该 job 还是每对强制 cancel+request。

**续（第 5 轮，2026-11-05 15:50）——宿主仍是 4/4，板端次序改动后结果反而变得不确定**：

- **台架健康复核**：把 bench 镜像刷回四板后，宿主 `--adjacency-only` 仍 4/4 检出
  （`words 10914/12840/13696/12412`，环序 1→2→3→4，`out/m1-scan/p0t-host-recheck`）。
  也就是说**同一固件、同一台架，宿主次序稳定成功**，差异只在编排。
- 按宿主"先接收侧、后驱动侧"把驱动 START 延后到 3700 ms 后：NO.1 在 WAIT_RING 超时
  （`fail_phase=100`），NO.2 反而报 3 条（bits 0/7/10，其中 7/10 不是环边），且三次接收窗的
  `words` 都是**同一个 346**（与本板发送计数同量级）→ 更像本板自环/固定伪影，不是对端突发。
- 三次运行（pair0=32742 / pair0=5671 / 本次 346×3）结果互不相同 ⇒ **固定相位表 + 无握手**
  的编排在 ARM/START 竞态下不确定。宿主的做法是**每步确认**（ARM 接收→查 started→ARM 驱动→
  查 started→基线快照→START 接收→START 驱动）。
- 下一轮改法：把"每对执行"改成**带确认的串行状态机**（每步 poll 到目标快照再进下一步），
  而不是同一张毫秒相位表；必要时用"驱动侧先等接收侧 beacon/RX 活动或超时"做自同步。

**续（第 6 轮，2026-11-05 16:05）——`PAIR?` 增加 service_delta / adapter_last_error，出现两个强信号**：

- `adapter_last_error=3`（`TX_FAILED`）只出现在**每块板的第一个驱动对**（NO.1 对 0、NO.2 对 3、
  NO.3 对 6、NO.4 对 9），此后都是 0；接收对恒为 0。
- 唯一成功收到 burst 的那一对（NO.2 对 0，`words=8560`）`service_delta=1`；所有失败对
  `service_delta≈500–700`。也就是说：**adapter service 一旦在窗口内高频运行，原始 RX 累积就没了**；
  service 几乎不跑时，ARM 留下的原始捕获反而能收到 burst。
- 解释方向：ARM 走 probe 分支后 PIO 处于"原始探针捕获"，而 ring service 一旦开始跑，
  会用正常 flight 路径重配/消费 RX DMA（`tx_count` 仍增长，说明 service 在发但不收）。
- 下一轮最小实验：**只 START 驱动侧，接收侧保持 `data_enabled=0`**（service 不跑，保留 ARM
  的原始捕获），看是否稳定出现上万字；若成立，再决定接收侧是否需要在采样后再 START 一次。

**续（第 7 轮，2026-11-05 16:10）——"不 START 接收侧"被证伪**：

- 实测（`out/m1-scan/run-20261105-161103.txt`）：接收侧不 START 时 `service_delta=0`
  （service 确实没跑），但**`words` 全为 0**——ARM 只开硬件抓取，不 START 就连原始 DMA 也不会累积。
  因此上一轮"service 运行吃掉 burst"的相关性是巧合，`START` 是接收侧的必要条件。
- 已把接收侧 START 恢复（代码回到上一形态并加注释说明反证）。
- 结论：M1 的失败仍只在"START 之后 RX 是否真的解码/累积"这一层，且随运行次序不确定；
  继续盲调时隙的收益已很低。② 的正测试需要一个真实 TRN-03 matrix——本仓库当前 `out/` 里
  **没有任何 `trn03-matrix.json`**，只能用完整 TRN-02 链现生成（约 30–40 min）。

**续（第 8 轮，2026-11-05 16:30）——用户提示"有内部逻辑分析仪"，已定位其 SCPI 面**：

- 面：`REALtime:IO:ANALyzer:STATe?`（20 字段，当前全 0=idle）、`:ARM <mask>,<period_ns>,<max>,<timeout_us>,<overwrite>`、
  `:EDGE:ARM`、`:STOP`、`:BURSt:ARM/STATe?/SAVE/EXPort?`（`middleware/scpi_port/src/scpi_realtime_io_commands.c:500-620`）。
- **`ARM` 在引导环运行期间一律被拒**（`-200 Execution error`，四板、两种镜像都如此）：它过
  `scpi_port_reject_if_run_forbidden(TRIGGER_CONFIG)`，与运行中的 TDMA/VDC 资源冲突。
  结论：LA 必须在**暗窗内由固件本地 arm**（`sync_io_logic_analyzer_request_arm()`），不能靠
  主机在开机后 arm。
- 待确认：LA persona 的 `gpio_read_mask` 是否覆盖 TDMA 环的 TX/RX 线；以及原始采样记录的读回路径
  （`STATe?` 只给计数/速率，记录需要 burst/export 或本地拷贝）。
- 台架已恢复：四板刷回 bench 镜像 `20261105081130`、板号 NO.1..4、`BOOT:PLAN?` 正常
  （NO.1 START_REF、其余 LISTEN）。

**续（第 9 轮，2026-11-05 16:45）——内部 LA 可用，之前 `-200` 是参数超限**：

- 根因：`SYNC_IO_LOGIC_ANALYZER_MAX_RECORDS=256`（`sync_io_logic_analyzer.h:128`），我先前传
  `max_records=8192` 被 `config_valid` 直接拒绝。改成 `ARM 0,100,256,5000000,1` 后**立即被接受**，
  且**引导环运行时也能 arm**（不需要暗窗）。
- `STATe?` 20 字段（`scpi_realtime_io_commands.c` 的 `scpi_cmd_analyzer_state_q`）：
  initialized/active/state/mode/end_reason/`capture_sequence`/`produced_records`/`consumed_records`/
  `dropped_records`/`overrun_count`/`data_crc32`/manager_* 等；实测 raw 采样（100 ns、256 记录）
  时 `produced_records=1136`、`dropped=323499`（10 kHz 环下丢样正常）。
- LA persona 的读掩码包含 TDMA pad：`gpio_read_mask = MAIN_INPUT | MAIN_OUTPUT | TDMA_PAD`
  （`sync_io_persona_resources.c:127-129`），所以它**能看见环线电平**。
- 限制：每次最多 256 条记录（100 ns 下 25.6 µs），`STATe?` 只给计数，**没有记录读回路径**；
  要用它判定"burst 是否到接收侧引脚"，需要 (a) 触发式捕获 + (b) 记录导出（`BURSt:EXPort?` 或
  固件本地拷贝）。这是下一轮的具体工作。

**续（第 10 轮，2026-11-05 16:50）——LA 计数/CRC 不能当作判据**：

- 在 pair 4 的"安静段 vs 突发段"各 arm 一次 raw 捕获（256×100 ns），读 `STATe?`：
  NO.3 的 `data_crc32` 三次分别是 `87381027 / 213403322 / 1660605473`——**安静段与突发段都不同**，
  因为捕获是自由运行且含全部 pad 位，CRC 不构成判据。
- NO.4 第二次 arm 后 `state=3`、`end_reason=3`，再下一次整组归零（LA 停止/复位），说明
  反复 arm 会改变 LA 生命周期；要做判定必须**一次成功的触发式捕获 + 记录导出**，
  不能靠计数/CRC 近似。
- 结论：M1 的"接收侧引脚是否看到 burst"仍未判定；LA 是可用工具，但需要
  (a) 明确 TDMA pad 的位号，(b) 触发式捕获，(c) 记录读回，三者都在下一阶段补齐。

**续（第 11 轮，2026-11-05 17:00）——TX_FAILED 的代码定位与工作保全**：

- `TX_FAILED` 是 **phys 层拒绝一次 TX 提交**的结果（`tdma_pio_spi_ring_adapter_tx.c:745-762`；
  `phys_tx_retryable` 为假才置错）。它只在每板第一个驱动对出现，之后 adapter 接受提交
  （`tx_count` 增长、error=0），说明**驱动侧后续确实在发**；失败在**接收侧的捕获路径**。
- 工作保全：`git stash create` + `refs/wip/m1-scan-20261105` = `ce22a6ce`（不改工作区、不丢改动），
  供下次会话在解除提交阻断后对比/提交。

**续（第 12 轮，2026-11-05 17:20）——M1 接受路径改为 10 kHz 自组（用户裁定）**：

- 新出口 `SYSTem:TDMA:BOOT:TOPology?` → `accepted,reason`（0/1 无快照或环未启用、2 半环、
  4 收不到前驱、6 引导步骤未完成）；判据**不含** join 标志（实测从板 `join_timeout=1` 仍成环）。
- 实板（bench `out/build/m1-self`，`GUIDE_SCAN=OFF`）：
  - 正：四板同启 → 四板 `1,0`，`enabled=1 node_count=4 up=1 down=1`、tx/rx 增长；
  - 负：停 NO.2、同步重启其余三块 → 三块 `0,2` 且 `half_ring=1`，被停板 `0,3`；
  - 恢复：四板重启 → 四板 `1,0`。
- 邻接矩阵（§9.19/9.20 的对表扫描）降级为 **provisioning 专用**；`CAL-GUIDE-11` 随之改口径。
- 台架：四板运行 `out/build/m1-self` 镜像（bench），NO.1..4，环已自组。

**续（第 13 轮，2026-11-05 18:30）——② S 段正测试通过：有 TRN-03 stage 时开机直接起 10 MHz**：

- 用 V2 预设跑完整 P3 得到真实 matrix（`out/m1-scan/full-v2d/trn03-matrix.json`，
  该轮 `PASS`：INFO 42 / WARN 0 / ERROR 0 / FATAL 0）；`trn03_stage.py --config ... --commit`
  四板 `passed=true`，`PERSistence?` = `persisted_valid=1, loaded=1, reject=0`。
- 刷 bench 镜像变体（`PROJECT_TDMA_GUIDE_AUTOFORM=ON`、`GUIDE_SCAN=OFF`、`GUIDE_STAGE_BOOT=ON`）
  并同步重启 → 四板 `SYST:TDMA:OPMode?` 活动档 **7（10 MHz / 1 ms）**，
  `BOOT:TOPology? = 1,0`，`RING:STATus?` `enabled=1 node_count=4 up=1 down=1`、
  tx/rx ≈ 9.9k–10.4k。**全程无主机起环命令**。
- 负向观察：`STAGe:CLEar` 只清 RAM stage、不清 NVS 记录；此时记录被上下文判 `reject=7`，
  但个别板仍可能因 restore 时序先加载 → 出现"三块 23、一块 7"的混档（环起不来）。
  因此开机覆盖只以 `persisted_valid` 为准（`loaded` 是 restore 之后才置位的，不能当前置条件）；
  跨拓扑/跨 profile 的陈旧记录目前靠 ARM 门与 restore 上下文拒绝，**开机前 CRC 预校验是待办**。
- 台架现状：四板 bench 镜像 `m1-stage`（`20261105100647`）、NO.1..4、以 10 MHz 运行在
  persisted stage 上（`BOOT:TOPology?=1,0`）。

**续（第 14 轮，2026-11-05 18:55）——③-b 逐槽 RX 计数落地，语义是"谁发起的"不是"谁是我的前驱"**：

- 新增 `adapter.rx_slot_mask/rx_slot_count[8]`（`tdma_pio_spi_ring_adapter.h`，运行时与快照两个结构体都加），
  在 `tdma_pio_spi_ring_adapter_rx.c` 接受帧处按 `view.origin_slot_id` 计数，只读出口
  `SYSTem:TDMA:RING:RXSLOT?`（mask + 8 槽计数），`tdma_ring_adapter_service.c` 负责拷贝进快照。
- 四板实测（10 MHz 环、`BOOT:TOPology?=1,0`、up/down=1）：`mask=1`、`count[0]≈5.3–5.5k`、其余槽为 0。
- **语义修正**：cut-through 转发保留 `origin_slot_id`，所以每板看到的都是"参考板（slot 0）的流"，
  而不是它的直接前驱。要得到"前驱"需要帧头携带**上一跳 slot**（当前没有）。因此本项交付
  "每板确实在收谁的流"，不能直接当环序矩阵用；缺槽定位仍走 `down_running`/半环。
- 台架过程还暴露一个运维事实：NO.4 掉过板号（`BOOT:PLAN?=0,1` 无档），重刷镜像并重写
  `SYST:BOARD:NO 4` 后恢复；说明批量刷写后必须逐板复核 NO 与 `BOOT:PLAN?`。

**续（第 15 轮，2026-11-05 19:40）——③-b 代码待凭证；台架 P0T 连续失败**：

- ③-b 代码已改完并在 10 MHz 环上验证读数（`RXSLOT?` 四板 `mask=1`、`count[0]≈5.4k`），但
  **尚未提交**：连续 3 次 V2 P3（`full-v2f/g/i`）都在 P0T 的某个 pair 上 `START <timeout>` 失败
  （判据与之前偶发同样式），拿不到与当前源码匹配的凭证。
- 侧记：`--full-erase` 刷写会因 180 s 超时中断（四板各约 95%），必须用
  `--command-timeout 600` 重刷恢复；整片擦除同时清掉板号与持久 stage，恢复后要逐板重写 NO。
- 处置：源码改动保留在工作区并用 `refs/wip/m1-scan-20261105` 兜底；凭证与提交等台架 P0T 稳定后
  一次完成（`workflow_steps.py run --preset pico2-v2-factory-candidate --tdma-only`）。

**续（第 16 轮，2026-11-05 19:50）——P0T 失败定位为工具传输问题，③-b 已入库**：

- **A/B 证据（同台架同线序）**：workflow 内 P0T 连续 3 次 `RING:START <timeout>`（`error_after`
  仍 `0,"No error"`）；独立 `calibration_ring_topology.py --adjacency-only --short-open --gap 0.2`
  连续 2 次 **4/4 通过**（环序 1→2→3→4）。差异只在 persistent CDC + 100 ms action-timeout，
  因此是**软件问题**，不是线序/硬件。
- 绕过：standalone P0T（**必须做 NO 分配**，`--no-assign` 会被 `--reuse-topology` 判 node map 不一致）
  + `workflow_steps.py run --reuse-topology <summary>` → 本次 V2 P3 `PASS_WITH_WARNINGS`
  （build `20261105114026`，INFO 33 / WARN 2 / ERROR 0 / FATAL 0）。
- **③-b 已入库**（`aabcbd34`）：adapter 逐槽 RX 计数 + `SYSTem:TDMA:RING:RXSLOT?`；
  四板实测 `mask=1`、`count[0]≈5.3–5.5k`（cut-through 保留发起者，语义见 §9.22）。
- 待办：P0T 的 START 超时后应像 ARM 一样用 `RING:STATus?` 复核意图，而不是直接失败。

**续（第 17 轮，2026-11-05 20:05）——"放宽 START 判据"这条路被契约测试否掉**：

- 曾按"超时就查 `RING:STATus?` 复核"改 P0T，`tests/python/test_calibration_topology_start.py`
  立刻 **8 条失败**：该文件把"未知 ACK 一律按失败、要求 STOP 后重 ARM 并重取基线"冻结为契约。
  改动已完全撤回（工作区干净），结论改写为：**正确修法在传输层**（workflow 的 P0T 用
  `--short-open`，或 START 前重连），不是在 ACK 判据上放宽。
- 另：`test_calibration_topology_start.py` 里有 2 条 cleanup 用例当前失败（`main()` 返回 0 而非 1），
  与本轮改动无关（该文件最后一次改动是 `579b1fdb`），记为既有问题。
- 本轮 P0T 又以 persistent CDC 跑了一次：4/4 通过（未触发新路径），说明失败是间歇性的传输问题。

## CAL-TASK-20261105-043 - 交接：日间 M0 收口，新会话入口

**已完成并入库**（提交 `4458cff3`、`0777e5b6`、`44631607`、`5d4105dd`）：

- WP-1/B7：引导档记录/存储（v2 CALIBRATION_NVS 子区）/决策/7 步序列 + `calibration_stage_derive`；
- boot 执行器：`app_guide_boot_service()`（可续跑，3 s 预热 + 30 s 窗口），
  `BOOT:PLAN?` 17 字段只读出口（2026-11-07 追加 `boot_done`/`boot_next_step` 后为 19 字段）；
- M0 实测：四板无主机命令上电自组 10 kHz（up/down=1、tx/rx 增长、`fail_step=0`），
  缺一块板时 `half_ring=1` 并 STOP，四板齐全不误杀；
- P3 凭证：build `20261105035001` `PASS_WITH_WARNINGS`（sources=981，trials=8），host 77 passed；
- 复跑 runbook：playbook §9.18；实测表：§9.17。

**未完成（不得写成已完成）**：

- ① 单口全链 P0T→TRN-00→01→02→03 自动运行；
- ② TRN-03 per-link staging 过 ARM 门禁拉起 10 MHz 环；
- 无 join-TX 抑制门（从板 START 即发送）；从板 `reason=5 PAYLOAD_STARVATION` 语义待说明。

**台架当前状态**：四板运行 P3 构建（**不含** `PROJECT_TDMA_GUIDE_AUTOFORM`，`BOOT:PLAN?` 为 NONE）；
要复现自组按 §9.18 重刷 `out/build/v2-guide/DHRT100_V2_CANDIDATE_FACTORY.uf2`。
板号已写回 NO.1..NO.4（整片擦除曾抹成 NO.0）。

**踩过的坑（新会话必读）**：

1. LISTEN 必须**立即 START**：ARM 只开硬件抓取，`data_enabled`（START）才让 adapter service 跑；
   "听到再 START"会死锁。
2. ARM 必须走 `distributed_refmem_tdma_ring_arm()`：它先配默认 flight map；直连
   `tdma_service_ring_arm` 会被物理层以 `OWNER_FLIGHT_MAP(17)` 拒绝。
3. v2 factory UP2 声明 `full_erase_required: true`；`picotool load` 不擦 OTA_JOURNAL，
   残留 journal 跨启动重放 → 串口 OTA 第一步 `erase failed: state=9`。整片擦除可清，但会抹板号。
4. `run` 的 step 计划必须含 step 6（在线 build 回读），否则 step 24 会因 OTA 前快照误判 build mismatch。
5. 工作区仍有并行会话的 12 条 OTA 文档/工具改动未提交（`tools/ota_keys/*`、`docs/ota/*`、
   `tools/ota/OTA_workflow.py`、`tools/picotool_flash/picotool_flash.py`），不要误并入本域提交。

**下一步顺序**：① 把 C/T/R 段（§2 的 C/T/R）接进 boot 执行器 → 四板单口全链；
② TRN-03 staging + ARM 门禁（M5）→ 10 MHz 环；③ 补 join-TX 抑制门与缺槽 bitmap 语义。

## CAL-TASK-20261105-042 - M0 上电自组成功（四板 10 kHz，无主机命令）

- **build**：`20261105031331`（v2 候选 + `PROJECT_TDMA_GUIDE_AUTOFORM=ON`，slot=NO-1、nodes=4、level=23）。
- **做法**：四板 picotool 刷同一 factory 镜像 → 各自上电启动，无任何起环 SCPI。
- **结果**（`SYST:TDMA:RING:STATus?`）：

| 板 | slot | up | down | adapter_started | service | tx | rx |
|---|---|---|---|---|---|---|---|
| NO.1 | 0 | 1 | 1 | 1 | 97952 | 315 | 18 |
| NO.2 | 1 | 1 | 1 | 1 | 6878 | 20 | 20 |
| NO.3 | 2 | 1 | 1 | 1 | 69740 | 22 | 22 |
| NO.4 | 3 | 1 | 1 | 1 | 42753 | 23 | 23 |

  `BOOT:PLAN?`：NO.1 = START_REF，其余 = LISTEN；四板 `fail_step=0`、`profile=1`；
  NO.2 `join_frame_seen=1`（听到协调板帧后入环）。
- **修复的两个真因**：
  1. `LISTEN` 自锁：ARM 只开硬件抓取，软件数据面由 `data_enabled`（START）门控 →
     改为 LISTEN 立即 START，5 s 窗口只写 `join_frame_seen/join_timeout` 证据；
  2. `ARM` 缺 flight map：直连 `tdma_service_ring_arm` 会跳过默认 map，物理层报
     `OWNER_FLIGHT_MAP(17)` → 改用 SCPI 同款 `distributed_refmem_tdma_ring_arm()`。
- **仍未闭环（不得写成已完成）**：
  - ~~半环/缺槽未判~~ → **本轮已补**（见下）；
  - **无 join-TX 抑制门**：从板 START 即参与发送；
  - 从板 `reason=5 PAYLOAD_STARVATION` 在空载低速率下出现，需文档说明；
  - 串口 OTA 在 factory 迁移后 `erase failed: state=9`，凭证路径未通（picotool 可用）。
- **半环拒绝（本轮实测，build `20261105032420`）**：boot 完成后启动 20 s 完整性窗口，
  `down_running` 为 0 即判半环：置 `half_ring=1` 并 `RING:STOP`。
  - 四板齐全：四板 `up=1 down=1 tx/rx>0`，`half_ring=0`（无误杀）；
  - 停掉 NO.2 后重启其余三块：三块均 `half_ring=1`、`up=0/down=0/started=0`（拒绝成功）；
  - 恢复四板：回到 `up=1 down=1`、`half_ring=0`。
- **门禁**：docs_check 256 OK、doc_regression OK、pytest 80 passed；固件待 P3/凭证。

## CAL-TASK-20261105-041 - M0-③ LISTEN/RX-join v1（ARM 收、首个帧后启 TX）

- **实现**：`app_guide_run_step(RING_LISTEN)` 仍先 ARM（此时 adapter 接收、`data_enabled=0` 不发布 TX），
  记录 `adapter_rx_count` 基线并开 5 s join 窗口；`app_diag_service` 每轮调用
  `app_guide_listen_service()`：一旦 `adapter_rx_count` 变化或 `up_running` 置位，就
  `tdma_service_ring_start()` 启 TX 并清 pending；窗口超时置 `app_guide_boot_join_timeout()`，
  保持 ARM、**不假装已加入**。
- **语义边界**：这是"ARM 收 → 首帧 → START"的 v1 近似，不新增 adapter 状态机；缺槽仍由协调板
  bitmap 判定（N1=B 的后续项）。v1 地图无引导档 → 整段不执行。
- **构建**：v1 增量 161/161、v2 增量 164/164，`flash_link_contract=OK`（app A/B/boot/recovery）。
- **未上板**：LISTEN 路径需要 v2 地图 + 引导档 provisioning 才能实跑；P3 进行中。
- **门禁**：docs_check 256 OK、doc_regression OK、pytest 80 passed。

## CAL-TASK-20261105-040 - WP-1 START_REF 执行接线（日间目标 M0-②）

- **顺序修正**：`tdma_boot_sequence` 的 `START_REF`/`LISTEN` 改为
  `STAGE_PROFILE → APPLY_PROFILE → SET_BRINGUP_TOPOLOGY → SET_BRINGUP_NO_CAL → RING_ARM →
  (START|LISTEN)`，与已验证的 SCPI 起环顺序一致（profile 先于 topology，BRINGup 在 topology
  之后、ARM 之前）；host 用例同步。
- **执行器**：`application/src/app.c` 新增 `app_guide_run_step()` / `app_guide_boot_service()`，
  由 `app_diag_service()` 首轮执行一次；步骤映射 `stage/apply_operating_profile`、
  `distributed_refmem_set_tdma_ring_topology + set_bringup_topology`、`set_bringup_no_cal`、
  `tdma_service_ring_arm/start/stop`。失败即停在首个失败步（1-based `app_guide_boot_fail_step()`）。
- **LISTEN 现状**：`RING_LISTEN` 只做 ARM、置 `app_guide_boot_listen_pending()`，**不启 TX**；
  RX-join 未实现，属显式缺口，不当作已加入。
- **构建**：v1 `pico2-lowrate4` 159/159、v2 候选 `v2-boot-candidate` 166/166，均
  `flash_link_contract=OK`（app A/B/boot/recovery）。
- **运行时语义**：v1 地图无引导档 → `NONE` → 执行器空转（不改变现网起环方式）；v2 地图载入
  档案后按序执行。未上板验证执行路径。
- **门禁**：docs_check 256 OK、doc_regression OK、pytest 80 passed；固件待新一轮 P3。

## CAL-TASK-20261105-039 - P3 验收：run 的 stale-build 误判、resume 通过、工具修正

- **首次 `run`**（build `20261105002955`）：OTA 四板 PASS、P0T/CLK/marker/TRN-00/01/02 全 OK，
  但在 step 24（TDMA 过程镜像）第一句失败：`board build mismatch: expected 20261105002955;
  四板=20261104164938`。
- **根因（主会话 + 只读子代理独立取证一致）**：`validate_recorded_builds` 校验的是 **OTA summary**
  的 `boards[].build_id`，而该值是 OTA **前**探测快照（`ota_multi_update.py:294/607/623` 从不回填）；
  live 豁免依赖 `online_builds`，但 `acceptance_step_plan`（`workflow_steps.py:2807`）的 `run` 计划
  是 `[2,3,4,5]`、**不含 step 6**，`phase_online_builds` 永不执行 → 豁免恒空 → 确定性误判。
  四板实时 `SYSTem:FW:BUILD?` 全为 `20261105002955`，属"记录证据过期"，不是真回退。
- **恢复**：`resume --package …/DHRT100_UPDATE.pkg --ota-summary …/ota-four-board/summary.json
  --out-dir …/p3-082941-resume` → `reuse live build=20261105002955`，全套台架重跑，
  **P3 graded PASS**（INFO 42 / WARN 0 / ERROR 0 / FATAL 0），receipt
  `config/hardware_acceptance/p3_acceptance_receipt.json`，证据
  `out/HardwareAcceptance/20261105/p3-082941-resume/`。
- **工具修正**：`workflow_steps.py:2807` 的 `run` 计划补 step 6（`[2,3,4,5,6]`），避免下次 `run`
  再因"记录证据过期"而在 step 24 误判；属 host 作用域，需 host 凭证。
- **未覆盖**：本次 P3 为 quick diagnostic profile（`strict_gates_passed` 以 receipt 为准）；
  未做 FULL bench、未跑 600 s soak。
- **门禁**：docs_check 256 OK、doc_regression OK、pytest 80 passed。

## CAL-TASK-20261105-038 - WP-1 boot 载入接线（app_init + 双构建）

- **改动**：`application/src/app.c` 在 `tdma_runtime_owner_init()` 之后调用
  `tdma_boot_profile_store_init()`，有档则 `tdma_boot_plan_decide()`+`tdma_boot_sequence_build()`，
  无档则显式 `NONE`；`application/inc/app.h` 新增只读访问器
  `app_guide_boot_plan_get()` / `app_guide_boot_steps_get()`（执行序列是后续独立阶段，本次不 ARM/START）。
- **构建验证**：v1 `pico2-lowrate4` 增量 **157/157**、`flash_link_contract=OK`；v2 候选
  `v2-boot-candidate` 增量 **164/164**、四份 map 契约 OK。
- **运行时语义**：v1 地图 store 返回 `UNSUPPORTED` → 计划 `NONE`（保持多口 bring-up）；
  v2 地图 boot 时载入 profile 并冻结计划/步骤，但尚未执行 ARM/START（`LISTEN` 仍待 RX-join）。
- **边界**：未上板；`app_guide_boot_*` 暂无 SCPI 出口，属代码级接线。
- **门禁**：docs_check 256 OK、doc_regression OK、pytest 80 passed。

## CAL-TASK-20261105-037 - v2 候选固件构建通过（真实引导档 store 分支编入）

- **配置**：`-DPROJECT_FLASH_DEPLOYMENT_MAP=v2_candidate -DPROJECT_FACTORY_MIGRATION_BUILD=ON
  -DPROJECT_NODE_CAPACITY=4 -DPROJECT_OTA_SECURITY_COUNTER=1 -DPROJECT_OTA_SIGNING_KEY_ID=3506438145
  -DPROJECT_OTA_KEY_PROFILE=v2_debug`（debug 私钥在 `out/keys/`，不入库）；目录
  `out/build/v2-boot-candidate`。
- **结果**：`cmake --build … -j 8` 完成 **1472/1472**，产出
  `DHRT100_V2_CANDIDATE_FACTORY.uf2`（10028 blocks）；`flash_link_contract=OK` 覆盖 app A/B、
  boot、recovery 四份 map；`flash_inventory`/`factory_region_report` 生成成功。
- **意义**：`FLASH_DEPLOYMENT_MAP_HAS_TDMA_BOOT_PROFILE=1`，因此
  `tdma_boot_profile_store.c` 的真实实现（双扇区、v2 子区 offset 4×erase）首次进入固件构建，
  不再只有 host 分支。
- **边界**：这是 **debug 签名**的 factory-migration 候选（非 release）；未上板、未跑 HIL；
  `app.c` 尚未调用 store。
- **门禁**：docs_check 256 OK、doc_regression OK、pytest 80 passed。

## CAL-TASK-20261105-036 - WP-1/B7 新模块进固件构建（CMake + flash 白名单）

- **构建接线**：`CMakeLists.txt` 的 TDMA 源清单加入 `tdma_boot_profile.c`/`tdma_boot_profile_store.c`/
  `tdma_boot_plan.c`/`tdma_boot_sequence.c`，calibration_manager 源清单加入 `calibration_stage_derive.c`。
- **flash 白名单**：`config/flash_raw_call_allowlist.json` 新增
  `components/tdma/src/tdma_boot_profile_store.c`（`xip_ptr`、`v1_product_nvs_calibration_compat`/
  `v2_calibration_nvs`），与 calibration/seat-mount store 同形态；`flash_inventory` 的 raw-caller
  drift 消除。
- **构建证据**：`cmake --build out/build/pico2-lowrate4 -j 8` 完成 **214/214**，`build_id=20261105001036`，
  `image_a_size=1111736`/`image_b_size=1111752`，`flash_link_contract=OK`（app A/B + boot 三份 map）。
- **覆盖边界**：该构建是默认 **v1 compat** 地图，所以只编到 store 的 `UNSUPPORTED` 分支；v2 子区
  分支（offset 4×erase 的真实 store）仍只有 host gcc 测试，未跑 v2 固件构建。
- **门禁**：docs_check 256 OK、doc_regression OK、pytest 80 passed。

## CAL-TASK-20261105-035 - WP-1 自组步骤序列落地（纯函数 + host 测试）

- **新增**：`components/tdma/inc/tdma_boot_sequence.h` 与 `src/tdma_boot_sequence.c`——把
  `tdma_boot_plan_decide()` 的动作翻成有序本地步骤：
  - `START_REF`：`SET_BRINGUP_NO_CAL → SET_BRINGUP_TOPOLOGY → STAGE_PROFILE → APPLY_PROFILE →
    RING_ARM → RING_START`；
  - `LISTEN`：同前五步，最后 `RING_LISTEN`（不启 TX）；
  - `REJECT`：`STOP`；`NONE`：空。
- **fail-closed**：容量不足时返回 0（不静默截断成"半启动"），空指针返回 0。
- **验证**：`gcc -std=c11 -Wall -Wextra -Werror` 编译通过，`test_tdma_boot_sequence.c` 输出
  `tdma boot sequence host tests passed`（6 步序、LISTEN 无 TX、REJECT/ NONE、容量与空指针负测）。
- **未闭环**：序列尚未在 `app.c` boot 调用点执行；`RING_LISTEN` 需要 adapter 的 RX-join 支持
  （N1=B 的实现项，未做）。
- **门禁**：docs_check 256 OK、doc_regression OK、pytest 80 passed。

## CAL-TASK-20261105-034 - WP-1 引导档双扇区 store 落地（host 测试）

- **新增**：`components/tdma/inc/tdma_boot_profile_store.h` 与 `src/tdma_boot_profile_store.c`——
  v2 子区上的双扇区 NVS（scan/append/rotate/commit marker/写后重扫校验），记录用
  `tdma_boot_profile`（32 B + CRC32）；v1 compat 编译分支显式返回 `UNSUPPORTED`。
- **验证**：host 侧以 RAM 扇区 stub（`drv_flash_xip_ptr`/`drv_flash_is_erased`/
  `flash_transaction_ao_execute`）+ 真实 `flash_store_nvs`/`flash_store_record`/`pota_crc32`
  编译运行：v2 输出 `tdma boot profile store host tests passed`（空→commit→get、同代 append、
  高代覆盖、旧代拒绝、20 次提交触发轮换后仍可读、re-init 回读）；v1 输出
  `v1 compat: boot profile store unsupported`。两个 TU 均 `-Wall -Wextra -Werror`。
- **已知取舍**：store 复用 `FLASH_TRANSACTION_REQUESTER_CALIBRATION`（同一 `CALIBRATION_NVS`
  分区），专用 requester 与 flash 策略白名单待 flash 域评审。
- **未闭环**：boot 载入与定序自组未接调用点；runner 未登记；未跑固件 CMake 构建。
- **门禁**：docs_check 256 OK、doc_regression OK、pytest 80 passed。

## CAL-TASK-20261105-033 - WP-1 自组决策模块落地（N1=B 纯函数 + host 测试）

- **新增**：`components/tdma/inc/tdma_boot_plan.h` 与 `src/tdma_boot_plan.c`——
  `tdma_boot_plan_decide(has_profile, profile, expected_nodes, local_slot, out)`，输出四选一动作
  （`NONE/START_REF/LISTEN/REJECT`）与原因码（`NO_PROFILE/BAD_PROFILE/NODE_MISMATCH/SLOT_MISMATCH`）。
- **语义**（N1=B）：无档→不动（v1 compat 保持多口 bring-up）；coordinator（slot 0）arm+start；
  其余 slot 先 RX 监听、等协调板帧；profile 与 expected_nodes/local_slot 不一致→REJECT，
  不形成半环；非法记录→REJECT。
- **验证**：`gcc -std=c11 -Wall -Wextra -Werror` 编译通过，`test_tdma_boot_plan.c` 输出
  `tdma boot plan host tests passed`（覆盖 7 类输入 + 空输出指针）。
- **未闭环**：决策尚未接 boot 调用点（`app.c` / TDMA owner）；store 与 flash 读写未接线；runner 未登记。
- **门禁**：docs_check 256 OK、doc_regression OK、pytest 80 passed。

## CAL-TASK-20261105-032 - WP-1 v2 引导档子区定义与双图验证

- **改动**：`config/flash_deployment_map.h` 在 v2 分支新增
  `FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_*`（`CALIBRATION_NVS` 内、relative offset
  4×erase、2 扇区）并加 static assert（不越过分区、不与 seat mount 重叠）；v1 compat 分支显式
  定义 `HAS=0`、offset/size 全 0，表示"无引导档、保持多口 bring-up"。
- **验证**：`tests/unit/test_flash_tdma_boot_profile_region.c` 分别在
  `-DPROJECT_FLASH_DEPLOYMENT_V2=1` 与默认（v1 compat）下编译运行：v2 输出
  `v2 boot profile region ok`（offset/size/分区/不重叠四条断言），v1 输出
  `v1 compat: no boot profile region`。两个 TU 均以 `-Wall -Wextra -Werror` 通过。
- **未闭环**：子区只定义了落点，双扇区 NVS store、boot 载入、定序自组尚未接线；未跑固件 CMake 构建。
- **门禁**：docs_check 256 OK、doc_regression OK、pytest 80 passed。

## CAL-TASK-20261105-031 - WP-1 引导档记录模块落地（C 纯函数 + host 测试）

- **新增**：`components/tdma/inc/tdma_boot_profile.h` 与 `src/tdma_boot_profile.c`——固定 32 B 记录
  （version/profile_level/node_count/local_slot/flags/topology_crc32/generation + CRC32），
  `encode/decode/valid` 三个入口；字段校验覆盖 level≠0、slot<node_count、node 2..8、flag 子集、
  generation/topology 非零。
- **验证**：`gcc -std=c11 -Wall -Wextra -Werror` 编译通过，`test_tdma_boot_profile.c` 运行输出
  `tdma boot profile host tests passed`；含 roundtrip、单字节翻转拒收、七个字段负测、尺寸/空指针负测。
- **未闭环**：双扇区 NVS store、v2 子区落点（`CALIBRATION_NVS` offset 4×erase）、boot 载入与定序自组
  均未接线；与派生模块一样，正式 runner（`tools/tests/run_*.ps1` + registry/triage）仍待补。
- **门禁**：docs_check 256 OK、doc_regression OK、pytest 80 passed。

## CAL-TASK-20261105-030 - R2b 派生模块落地（C 纯函数 + host 测试）

- **新增**：`components/calibration_manager/inc/calibration_stage_derive.h` 与 `src/calibration_stage_derive.c`
  ——`calibration_stage_derive_pio`（persona/clkdiv_q16/clk_sys/instruction_period/bit_cycles）、
  `calibration_stage_link_base_delay_ns`、`calibration_stage_phase_delay_samples` 三个纯函数，
  非法输入返回 false，不做静默截断。
- **对齐 oracle**：数值逐项等于 `tests/python/test_cal_guide_derive_golden.py` 冻结的 golden 向量
  （10/25/30 MHz 的 `273152/17`、`109312/7`、`91136/6`；base 160/162/164→80/81/82；
  phase 80/4/0→20、+1→21、−1→19、81→20）。
- **验证**：`gcc -std=c11 -Wall -Wextra -Werror` 编译通过，`test_calibration_stage_derive.c` 运行输出
  `calibration stage derive host tests passed`。
- **未闭环**：`tests/unit/test_calibration_stage_derive.c` 还没有正式的 runner（`tools/tests/run_*.ps1`），
  需在 `tool_registry`/`tool_workflow_triage` 登记后才进 host 聚合；本轮是直接 gcc 编译验证。
- **门禁**：docs_check 256 OK、doc_regression OK、pytest 80 passed。

## CAL-TASK-20261105-029 - 单口引导期三项裁定（B/1/R2b）与 R2b golden 向量

- **裁定**：用户 2026-11-05 确认"可以，继续吧"，按建议组合冻结 **N1=B**（主从成环：协调板先发、
  从板 RX-join、缺槽由 bitmap 判定）、**B9=1**（v2 地图 + factory migration；代码侧先做 v2 分支
  引导档子区）、**B7=R2b**（紧凑 stage 104 B + 派生公式从 Python 移植到 C）。
- **落点**：playbook §9.15 裁定记录、§9.1（B 规格 + 编队模型）、§9.2（B9=1）、§9.4/§9.12–§9.14
  （R2b）、TODO `CAL-GUIDE-01/04` 转 `IN PROGRESS`。
- **新增可执行守卫**：`tests/python/test_cal_guide_derive_golden.py`（3 用例）把 `pio_facts`
  （level 7/8/9/20/21/22 的 persona/clkdiv/q16/instruction_period/bit_cycles）与 `link_base_delay_ns`、
  `phase_delay_samples` 的期望值冻结为 golden 向量，作为 C 侧 `calibration_stage_derive` 移植的
  对齐 oracle（C 实现必须复现同一组数值）。
- **状态**：本轮只冻结决策与 golden 向量，未改固件；WP-1（引导档/自组）与 B7（派生移植）可开工。
- **门禁**：docs_check 256 OK、doc_regression OK、含新向量在内 80 passed。

## CAL-TASK-20261105-028 - 单口引导期流程（10 kHz 载体）规划与 HAOFV 双审

- **范围**：最终形态只接协调板单口；10 kHz 4 节点低速 TDMA 作为引导期唯一跨板命令载体，
  承担校准训练配置/触发/查询，并把 10 MHz 环所需 per-link staging 交给 `S` 段。流程文档
  `docs/calibration/CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md`（Draft，未登记），任务行
  `CAL-GUIDE-01..10` 见 [校准域待办](CALIBRATION_DOMAIN_TODO.md) §2.6。
- **审查**：两份独立只读审查（HAOFV owner/实时；控制面/契约）均判 `REJECT`。合并后 9 项
  `BLOCKER`、13 项 `MAJOR`：边跑边写 flash 的自指死锁；恢复失败不可恢复（默认档 10 MHz、
  无 stage 时 ARM 被拒）；Δ 无派生下界且无 ARM 依据；T 为隐式广播（CP5）且发起方写目标事实
  （CP2）；接受即返回后停环、ACK 无聚合（CP8）；目录缺 RING/OPMode/CAL 全部条目；
  `param_words ≤ 16` 且全 inline 装不下 CAL LINK；无去重（CP7）；持久化绕过 idle 窗口且与
  `REFTMEM-PERSIST-01` 冲突。逐条处置与证据见
  [核验提交单](../check/submissions/CALIBRATION_GUIDE_CROSS_REVIEW_01.md)。
- **v2 修订**：C 拆为"环上 RAM staging + STOP 本地 commit"；T 改为目录内命令 + 显式掩码 +
  三元组去重 + required 全 ACK 才停环；恢复改为"持久化开机 10 kHz 引导档 + 定序自组"
  （`BRINGUP_NO_CAL`），Q 开机可答；Δ 逐节点派生 + ARM 有界窗口（明确不是握手证明）；
  inject 归 core1 定时；Q 记录绑定 build/topology/profile/schedule/calibration 与逐 trial
  字段并带 age/stale；新增 `S` 段做 staging/ARM gate；不新增代际、不新开契约 id（复用
  `CONSOLE-FLOW-01`、`REFMEM-CTRL-01` 增量、`CALIBRATION-PHASE-01`）。
- **未闭环（复审前不得当事实引用）**：10 kHz 作为"引导期唯一载体"的 TDMA 条款原文；ring status
  进 REFMEM mirror 的读数面；`CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md` §8/§10 逐行条文；
  10 kHz 相位 WCET/迟到/IRQ 与 SD 写时延均无实测；C4/C5 未闭合，单口必接协调者板。
- **提交**：无代码改动；本批仅文档（playbook + 核验单 + TODO §2.6 + 本条）。下一 gate 是
  v2 独立复审（C11），通过后才允许登记任何条款。

## CAL-TASK-20261026-027 - 校准 SCPI 命令拆分（N5.0.4c）

- 变更：`middleware/scpi_port/src/scpi_calibration_commands.c` 2,131 → 1,022 行；新增
  `scpi_calibration_capture_commands.c` 1,122 行（DATA/SCK/RING/TRAIN/PATH/BIAS/SAVE/CLK/
  P3/LINK 与元信息），两单元均 ≤1,300 的 SCPI 预算。拆分点在第 1021/1022 行（括号深度 0）。
- 私有面：仅 `scpi_calibration_result_u64` 由 `static` 改外部符号，声明进
  `inc/scpi_calibration_commands_internal.h`；`SCPI_CALIBRATION_COMMANDS` 注册宏未改动，
  回调名与相对顺序不变。
- 等价性：函数定义集合前后一致（87/87，无增删）；产品 SCPI 校验源清单补入新单元；
  `test_trn03_candidate` 改读两文件。
- 验证：host 定向 46 passed（`test_tdma_origin_admission[stale]` 为既有失败，归属 N5.0.4b）；
  P3 `FOUR_NODE_DPLL_QUICK_DIAGNOSTIC` = `PASS_WITH_WARNINGS`（INFO 31 / WARN 14 / ERROR 0 /
  FATAL 0）。未覆盖 full bench 与严格质量门。
- 归档：`middleware/scpi_port/archive/scpi_calibration_split_v1/`（含 SHA256）。
- 提交：`cf472872`。

## CAL-TASK-20260913-026 - 当前测量基线上的独立 RX 诊断窗口

- 对应 TODO：`TRN-ORIGIN-RX-01`、`TDMA-FLIGHT-002F`，保持 IN PROGRESS。
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-origin-rx-window/`，完整
  实验记录见 `TDMA-PROGRESS-20260913-032`。
- 保持上一切片固件；当前 P3 测量提供 link base、拓扑、profile 与 generation。
  独立 RX 搜索和 NO2 DATA 压力点是显式未接受的诊断扩展，矩阵结构与所选行通过
  host 编码/re-arm 校验不等于有效窗口测量通过。只在停止态 staging 后由 TDMA owner
  应用，各试验核对实际 TX/RX phase 并保留严格启动失败。
- 观察相邻 RX 选行和回切压力控制，目标是选取后续验证的内部采样点；不冻结
  采样常数、模拟 setup/hold 余量或 active calibration。ARM 拒绝、停止态现场、
  有界重试和当前矩阵实际恢复分别留证，不能用重试成功提升原始失败。
- 本轮普通模式已取得多个零错误选行及晚相位回切错误，RX12 保留为后续内部
  候选。最终当前矩阵已核对 staging、实际运行参数和 STOP；严格启动失败、
  最坏接收余量、同帧绑定及自主/逐圈证据仍开放。

## CAL-TASK-20260913-025 - origin 独立 RX 采样参数与存储兼容验证

- 对应 TODO：`TRN-ORIGIN-RX-01`、`TDMA-FLIGHT-002F`。本项仍 IN PROGRESS；证据根为
  `out/HardwareAcceptance/20260913/tdma-flight-origin-rx-phase/`。
- 独立 origin capture offset/phase 经 Calibration stage 与同一 base/offset 校验传给
  TDMA owner；只在停止态配置，普通/自主 origin 的接收端使用独立 phase，DATA TX 与
  follower 重定时使用原 DATA 参数。候选矩阵增加完整独立搜索维度，旧 LINK 命令与
  旧存储恢复原共用行为；新 payload 保存独立参数并核对版本和长度。
- 软件与 build 数值为快照，非事实源：相关 Python 用例及 runtime/service/存储 C
  测试通过。首次容量测试仍断言旧 payload 大小，失败日志保留，区分新旧格式后重跑
  通过。Release A/B/Boot build `20260912164001`、源码指纹
  `a34652c8546de0f77a1f0dd409953ad7e0209c2be6a37a8b74fd08dd36c894d6`，静态占用
  增加 264 B，两份 App 链接剩余均 4904 B，正式 RAM 门禁仍失败。
- 当前源码真实 P3 已完成，四板 OTA 与物理训练流程通过，凭证
  `p3-receipt-r1.json` 为 `passed=true/strict_gates_passed=false`。严格实时与完整
  process-image 接受结论不因诊断凭证而改变。固定 TX 的 RX 单变量对照已取得有限
  普通模式证据，支持独立接收相位方向；首轮回切 ARM 拒绝保留，不能据此宣布根因
  修复或接收窗口验收。停止态参数负测及原 staging 恢复已通过。完整实验结果、失败
  及最终恢复统一记录在 `TDMA-PROGRESS-20260913-031`，不在本表固化
  采样常数或 active calibration；实际 Flash commit/reboot 保全尚需专门硬件证据。
- 临时许可证下自主 origin 已使用独立 RX 参数完成有限计时观察，实际 phase 读回
  与普通模式一致；窗口内 transport CRC 无增长，完整 WCET 仍超限。原始波形、
  命令拒绝和恢复失败均在同根保留；这不替代接收窗口搜索、最坏余量与逐圈证据验收。

## CAL-TASK-20260831-024 - PHY timing 模块拆分后的快速 10 MHz 验收

- 对应 TODO：`P3-HIL-CODE-GATE`、`TRN-03D-PHY-01`、`TRN-03D-PHY-03`。
- 变更：将无状态 PHY 时序辅助函数固化为
  `components/tdma/inc/tdma_pio_spi_phys_timing.h` 与
  `components/tdma/src/tdma_pio_spi_phys_timing.c`，主 PHY 文件保留实时 owner
  和既有约 2000 行迁移边界；本次不把 SD、SVG 或诊断逻辑放入 Core1/PIO 热路径。
- 构建：`out/build/phy-timing-module-20260831/`，`build_id=20260831042513`；
  715/715 目标、PIO 头、App/A/B、Boot、Flash link contract 和统一 OTA package
  均通过。
- OTA：执行 `tools/ota/ota_multi_update/ota_multi_update.py`，五板均已枚举并完成逐板
  OTA。NO1--NO4 的物理环路板按唯一地址复查；NO5 为
  `839E1AE79EA20F31`，当前 USB CDC 端口为 `COM10`。五块板的逐板
  `ota_boot_commit/summary.json` 均为 `passed=true` 且读回目标 build。验收逻辑仍按
  唯一地址动态发现端口，不把 `COM10` 固化为永久端口号；工具在 verbose 汇总打印阶段
  触发 Windows GBK `UnicodeEncodeError`，不影响设备端提交，后续需修复主机输出编码。
- 快速 P3：四板物理环序执行 `--diagnostic-frequency-only --frequency-mhz 10`
  和 `CLK_DATA`/`CS_DATA` 两组信号，共 24/24 trial 通过；RTT 约 176--184 ns，
  delay estimate 80--82 ns，频率误差 0%，占空比 50--54%，DMA overrun 和 PIO
  stall 均为零。证据：
  `out/hardware_acceptance/p3-basic-10mhz-phy-timing-20260831/summary.json`。
- 波形：使用 `tools/calibration_ring_validate/calibration_p3_waveform.py`
  的 `--include-all --window-ns 1000` 从硬件时间戳生成 24 份 1 us SVG，位于
  `out/analysis/p3-basic-10mhz-phy-timing-20260831-all/`。当前 P3 SCPI 仍未导出
  原始 256-word SD capture，默认模式因此不伪造波形并返回 `rendered_count=0`；
  本次 SVG 明确标记为硬件时间戳诊断证据。
- Host 验证：`tests/python/test_trn03_closed_loop.py`、
  `test_calibration_link_p3.py`、`test_calibration_p3_waveform.py` 共 107 passed，
  输出在 `out/pytest/phy-timing-module-20260831/`。
- 下一步：先用 NO1--NO4 完成四板 TDMA gate；NO5/COM10 仅用于随后独立的 DPLL
  观测 gate。修复 OTA 工具 UTF-8 输出后再归档顶层 summary，然后继续下一个 PHY
  `.inc` 功能块迁移并重复本验收。

## CAL-TASK-20260831-023 - 快速硬件验收与 P3 时间戳波形诊断

- 对应 TODO：`P3-HIL-CODE-GATE`、`TRN-03D-HEALTH-04`。
- 状态：完成快速验收；正式全量校准门禁仍按既有流程执行。
- 运行边界：为加快 PHY 拆分迭代，使用 `tools/calibration_ring_validate/calibration_link_p3.py`
  的显式 `--diagnostic-frequency-only --frequency-mhz 10` 子集；该模式只验证基础 10 MHz，
  不能替代包含 10/25/30 MHz、训练、TDMA 和 NO5 DPLL 的发布验收。
- 实测结果：四板环序
  `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A -> A1E549202D18ED6A`，
  `CLK_DATA`/`CS_DATA` 两组、24 个 trial 全部通过；RTT 为 176--184 ns，delay estimate
  为 78--82 ns，10 MHz 频率误差为 0%，占空比为 52/48%，DMA/PIO overrun/stall 为零。
- 运行态恢复：此前 Node2 SD `FILE_WRITE` 和 Node3 `capture_sample_count=0` 均只使用应用级
  软件重启恢复；更换 SD 卡后未再复现。未因运行态问题执行 OTA；本次 host 工具改动也未触发 OTA。
- 诊断工具：新增 `tools/calibration_ring_validate/calibration_p3_waveform.py`，可从既有
  P3 `summary.json` 生成 1 us SVG，区分理想、initiator、responder 的硬件边沿时间戳；当前
  P3 SCPI 尚未导出原始 256-word PIO 捕获到 SD，工具会明确标记该限制，不把时间戳重建冒充
  SD 原始波形。
- 验证：`tests/python/test_calibration_link_p3.py` 与
  `tests/python/test_calibration_p3_waveform.py` 共 17 passed；快速验收证据位于
  `out/hardware_acceptance/p3-basic-10mhz-20260831/summary.json`，失败 P3 时间戳 SVG
  位于 `out/analysis/p3-resume4-failures/`。
- 本轮 PHY 拆分迭代：构建产物位于
  `out/build/phy-split-p3-10mhz-20260831/`，`build_id=20260831040257`；五板异步 OTA
  后通过唯一地址复查，四板均运行该 build。基础 10 MHz P3 证据位于
  `out/hardware_acceptance/p3-basic-10mhz-phy-split-20260831/summary.json`，四条 link、
  两组信号和 24/24 trial 通过，delay estimate 为 80--82 ns；对应 1 us SVG 位于
  `out/analysis/p3-basic-10mhz-phy-split-20260831/`。OTA 工具在 verbose 输出阶段遇到
  Windows GBK 编码异常，但 OTA 后五板 build 复查均为目标 build；该主机输出问题不影响
  设备端提交结果，后续应在无 verbose 或 UTF-8 环境下补齐可归档 OTA summary。
- 下一步：继续 PHY/状态机迁移；每个设备端代码切片仍须 build、同包 OTA 和正式硬件验收，
  host-only 分析工具复用现有 build，不得用 OTA 处理运行态复位。

## CAL-TASK-20260831-020 - 运行态复位与 OTA 边界固化

- 对应 TODO：`P3-HIL-CODE-GATE`、`TRN-03D-HEALTH-04`。
- 状态：完成（流程约束）。
- 规则：运行态卡死、quarantine、ARM/状态机未清理等问题只允许使用
  `tools/ota/picotool_reboot/picotool_reboot.py` 做应用软件重启；重启后必须重新通过
  topology/profile/calibration identity、矩阵读回和 ARM gate。不得通过再次 OTA 来“解决”
  运行态复位问题。
- OTA 触发条件：仅当设备端固件源码、PIO 原语、构建输入或设备端配置发生变化并重新构建时，
  才进行多板异步 OTA。host 分析、SVG/SD 解码或验收编排工具变化复用既有 build，跳过 build/OTA，
  并在 `firmware-reuse.json` 中记录 `firmware_changed=false`、`build_skipped=true`、
  `ota_skipped=true`。
- 本次变更只更新流程文档，未修改设备端固件，因此不触发 build 或 OTA；后续运行态恢复应保留
  软件重启记录，并重新执行受影响的校准/训练和 HIL 门禁。

## CAL-TASK-20260831-021 - 运行态复位后 level8 粗 CLK 复测

- 对应 TODO：`P1`、`P3-HIL-CODE-GATE`。
- 状态：阻塞（运行态配置门禁），未触发 OTA。
- 处置：按复位边界仅使用 `tools/ota/picotool_reboot/picotool_reboot.py` 对 NO1–NO5
  做应用软件重启；复用固件 build `20260830193828`，没有重新构建或刷写。
- 结果：重启后 NO1/NO2 的 level8 粗 CLK 可取得 bracket；随后 NO3/NO4 在不同轮次
  出现 `ARM rejected with result=8`
  （`DISTRIBUTED_REFMEM_TDMA_ARM_RUNTIME_CONFIG_REJECTED`）。该结果属于运行态
  topology/profile/config apply 闭环未满足，不能解释为链路或固件已修复。
- 证据：`out/hardware_acceptance/phy-split-step3-20260831-reboot2/`，其中包含五板
  软件重启 transcript、level8 summary 和失败日志；复测过程中未生成 OTA summary。
- 下一步：继续用软件重启后的同一 build 检查 STOP→topology/profile apply→ARM 的
  config generation/readback；只有确认设备端固件或 PIO 代码需要改变时才重新 build/OTA。

## CAL-TASK-20260831-022 - MARK 环回采样抖动与 offset 候选复测

- 对应 TODO：`TRN-01`、`TRN-03D-PHY-01`、`TRN-03D-PHY-03`。
- 状态：进行中（已定位为采样量化问题，尚未修改设备端实现）。
- 固件边界：全部复测复用 build `20260830193828`；运行态只执行 STOP/ARM 和运行时
  offset 矩阵加载，没有重新 build 或 OTA。任何卡死/状态残留仍按 `CAL-TASK-20260831-020`
  的软件重启边界处理。
- SD/SVG 证据：四节点 MARK 原始捕获已从 SD 下载到
  `out/analysis/marker_node[0..3]_*.json`，逐节点 1 us SVG 和离线相关结果位于同目录；
  Node0 的环回捕获 `g1788128339` 在当前 `+1` offset 下固件距离为 `450`，把离线
  offset 模拟为 `+2` 可降至 `44`，但这不是跨重复的稳定值。Node0/Node3 的原始边沿
  出现 `32..44 ns` 和 `76..84 ns` 桶，Node1/Node2 主要为 `40/80 ns` 桶。
- 内容完整性：对四个捕获按 Manchester 半码元多数值离线解码，四节点均为 `321/321`
  个逻辑位正确，CRC/header/EOF 标志均有效；因此当前失败不是极性、线序或 MARK 内容
  损坏，而是 PIO 逐边沿重定时后的 4 ns 采样量化抖动被原始 sample-distance 放大。
- 矩阵复测：固定 `[0,-1,0,+1]` 做 8 次重复时 Node0/Node3 接受数分别为 `5/8`、
  `6/8`，最坏距离 `1085/969`；固定 `[+2,-1,0,+1]` 时分别为 `5/8`、`6/8`，
  最坏距离 `1155/884`。两组均不能作为稳定 active offset；失败 trial 和 SD 路径保留在
  `out/hardware_acceptance/marker-node0-offset0-repeat8-20260831/` 与
  `out/hardware_acceptance/marker-node0-offset2-repeat8-20260831/`。
- 下一步：在不改变 TDMA 实时路径的前提下，为 MARK 相关器增加“边沿量化容差与原始
  distance 分离”的设计评审；先补 C/host 回归和离线证据，再决定是否修改设备端相关器。
  在该实现确认前，不得用提高 offset 或重复 OTA 掩盖此采样门禁问题。

## CAL-TASK-20260830-019 - P3 代码变更硬件验收强约束

- 对应 TODO：`P3-HIL-CODE-GATE`。
- 状态：完成。
- 目标：任何固件、PIO、构建、工具、测试或 hook 实现改变后，旧 P3 HIL 证据自动失效；只有
  完成统一硬件验收并生成与 Git index 同源的凭证，pre-commit 才允许提交。
- 实现：新增 `tools/hardware_acceptance/p3_hardware_acceptance.py`，统一编排 release build、
  多板异步 OTA、完整 P3 matrix 和 TDMA 调度隔离核验；台架事实集中在
  `config/hardware_acceptance/p3_bench.json`；tracked receipt 绑定源码树、固件包、OTA/P3
  summary digest 和关键判据。pre-commit 只校验 staged 指纹及本地证据，不在提交时临时占用
  硬件；实现工作区有未暂存差异时 fail closed。
- 软件验证：OTA 编排、验收工具、P3 工具和 TRN-03 定向 Python 回归共 `117 passed`
  （快照，非事实源），证据为 `out/pytest/p3-hardware-acceptance-tool-20260830/`。
- 硬件闭环：最终 build 为 `20260830133250`（快照，非事实源）；五板异步 OTA 全通过，
  证据为 `out/hardware_acceptance/p3-mandatory-gate-r3-20260830/ota-five-board/summary.json`；
  四板物理环序的 CLK_DATA/CS_DATA、完整频率阶梯和重复矩阵共 `72/72` 通过（快照，非
  事实源），delay 为 `80..82 ns`，DMA overrun/PIO stall 均为零，25 MHz 为最高稳定档。
  P3 前后 realtime load mask 不变且 calibration 未 quarantine。完整机检判据与 digest 见
  `config/hardware_acceptance/p3_acceptance_receipt.json`，硬数字事实源为工具与台架配置符号。
- 失败闭环：首次运行暴露 OTA child 输出使用系统 GBK 解码及 `stdout=None` 落盘缺陷，已固定
  UTF-8/replace 和 None-safe 汇总；第二次运行暴露刚 OTA 后异步 START snapshot 仍为旧 IDLE，
  已改为同 epoch 有界幂等重发并以状态回读为唯一 ACK。两次失败均未签发 PASS 凭证，原始
  evidence 保留在 `out/hardware_acceptance/p3-mandatory-gate-20260830/` 和
  `out/hardware_acceptance/p3-mandatory-gate-r2-20260830/`。
- 语义边界：该流程是代码回归基线，不清除 P3 endpoint bias、active candidate、持久化和
  P4-REL 产品门禁，也不把 P3 加入 TDMA realtime load mask 或短帧预算。


## CAL-TASK-20260828-017 - TRN-03B 诊断捕获预算收敛与待办补齐

- 对应 TODO：`TRN-03B`、`TRN-03D-PHY-01..04`、`TRN-03D-SOAK-01`。
- 代码修正：原始环路捕获改为持久化私有工作快照；PENDING 拍只推进 SCK/RX copy stage，
  仅 READY 拍发布完整物理快照，避免每个 16 字节搬运拍重复复制大结构；RX 搬运块由 16 字节
  收紧到 4 字节，并在工具异常中保留完整 TDMA schedule 前后快照和 delta，便于定位
  `last_runtime/max_runtime/WCET` 耗时来源。
- 验证：`tests/python/test_trn03_closed_loop.py` 与 `test_trn03_waveform.py` 共 91 项通过，
  TDMA receive-health、flight-overlay、PIO-SPI ring-adapter 三组固化 C host 测试通过；增量
  构建目录为 `out/build/trn03-todo-closure-20260828/`，A/B、OTA 包和 flash-link 检查通过。
- OTA：四板异步 OTA 全部通过，证据目录为 `out/ota/trn03-copy4-20260828/`，四个板卡均为
  `PASS`，设备 build 为 `20260828072954`。
- HIL：`out/training/trn03b_pending_publish_20260828/` 仍未通过 schedule gate，说明捕获
  仍可能使 calibration phase 超过固定 WCET 并进入 quarantine；随后诊断复测
  `out/training/trn03b_copy4_diag_20260828/` 在 Node1 停止恢复阶段因 `config_seq` 尚未应用
  而 ARM 超时。短帧启动/稳态本身未被证明失败，但本轮不能宣称 TRN-03B 完成。
- 结论：中间 PENDING 快照重复发布已固化回归约束；下一步必须先完成停止后四板 config-seq
  应用确认，再逐拍取得 schedule 快照，继续拆分或优化超 WCET 的捕获 stage。未完成前不得
  将诊断 SD/SVG 证据或旧四板短帧证据升级为新的 TRN-03B/TRN-03D 完成结论。

补充：工具提交 `f279637` 已将 `PROFILE_APPLY/TOPOLOGY` 等 STOPPED owner mutation
的成功条件扩展为等待 `ring_config_seq == ring_applied_config_seq`，并把该确认保存在动作证据中；
91 项 TRN-03 Python 回归通过。四板 HIL 需在下一轮 OTA 后重新执行，不能把此 host-side 修复
等同于实板通过。

## CAL-TASK-20260828-018 - 四板短帧稳态与 SD 捕获侵入预算复测

- 对应 TODO：`TRN-03B`、`TRN-03D-PHY-01..03`、`TRN-03D-SOAK-01`。
- 环境：四板重新异步 OTA/重启后运行 build `20260828072954`，使用冻结 calibration
  generation/profile/matrix 和 process-image stage；OTA 证据为
  `out/ota/trn03-reset-before-hil_20260828/`。
- 短帧结果：`out/training/trn03b_clean_hil_20260828/summary.json` 显示四 Node
  sequence、TX/RX FIFO、process-image map apply/accepted 和 bitmap 持续增长；各 Node
  原始 `rx_bad/partial/stall`、adapter bad、sequence gap 和 CRC mismatch 均为 0。
  这确认当前短帧数据路径本身没有观察到错误帧消费或 FIFO 停滞。
- SD/调度结果：Node0 的原始 SCK/RX 采集仍使 calibration phase `last_runtime_cycles=43853`，
  超过固定 `wcet_cycles=34000`，并产生 start miss、deadline miss、overrun 和 quarantine；
  工具保存了捕获前后完整 schedule 与增量。由于 schedule gate 失败，本轮没有生成可接受的
  四 Node SVG/SD 分析包，不能将短帧或 TRN-03B 标记 DONE。
- 结论：短帧稳态与捕获诊断必须分离验收；下一步继续优化捕获的 PIO/SD 原始证据搬运，或将
  采集安排到不占用 TDMA calibration phase 的独立预算窗口。禁止通过放宽 `34000` WCET、
  关闭 quarantine 或忽略 schedule delta 来宣称完成；DPLL 闭环仍等待 TRN-03 短帧和捕获无扰
  门禁全部通过。

## CAL-TASK-20260828-016 - TDMA 基础负载前置与训练接口基线

- 对应 TODO：训练子域 `TRN-03` 的 TDMA 周期预算前置，以及 `P4-DBG` 的 transport 依赖。
- 状态：TDMA 基础负载闭环完成；校准域仍保持 staging/diagnostic-only，不改变 active
  calibration gate。
- 日期：2026-08-28。
- 结果：五板统一 OTA 后，TDMA 固定周期先装入 VDC/DPLL、SYNC_CAPTURE、REFMEM 和
  SYNC_TRIGGER 基础负载；CALIBRATION、MODEL、TRIGGER_MEASURE 未因运行时余量而临时加入。
  固定 Node mailbox 的 mandatory 区域、CRC 和可选诊断容量由同一 layout/预算工具生成，校准
  训练继续只消费 TDMA 发布的 raw evidence、窗口和质量快照。
- 验证：release A/B 构建、定向 pytest、五板 OTA/HIL 均通过；基础 schedule 两轮快照无
  schedule miss，证据为诊断快照而非 DPLL 正式锁相结论。
- 证据位置：`out/tdma_process_image_budget/current.md`、
  `out/tdma_cycle_schedule/foundation_sync_trigger21.md`、
  `out/ota/tdma_foundation_sync21_final_20260828/summary.json`。
- 下一步：保持基础 schedule 不变，补齐 SD 原始波形的频率/占空比长期证据；随后逐 phase
  评估 TRN-03C/D 所需的校准或恢复负载，只有静态 WCET/wire/guard 预算明确允许时才启用。

## CAL-TASK-20260827-015 - DPLL/VDC 快速调试路径与原始链路错误压降边界

- 对应 TODO：`P4-DBG-01..03`、`P4-LIVE-01..02`、`P4-REL-01`、
  `TRN-03D-PHY`、`TRN-03D-RXGATE`、`TRN-03D-RETRY` 和 `TRN-03D-HEALTH`。
- TDMA 当前状态足以支撑 DPLL/VDC 的快速接口和算法调试：四板短帧
  `process-image/FIFO` 闭环、sequence/CRC、逐 Node 质量汇总、SD 原始波形/SVG、
  `VdcSyncAO/SyncDpllFB` snapshot 和 `DIAGNOSTIC_ONLY` sample admission 均已有可复核路径。
  因此可以并行开发 trace replay、offset/rate/lock 曲线、参数扫描和 reject 分类。
- 当前状态仍不足以宣称真实产品级 DPLL 锁定：TDMA 文档基线仍要求真实 PIO 边沿
  hardware latch、`HARDWARE_TICK`、分辨率门限、非 `DIAGNOSTIC_ONLY` 和
  `simultaneous_feedback_loop_evidence`；软件 drain 时间戳只能用于诊断。endpoint bias、
  fresh P3 active calibration、RX health/retry/watchdog 和发布级长稳也未全部完成。
- 验证阶段的最小硬门槛已明确：sample identity/sequence/source/resolution/CRC 可追溯，坏样本
  不进入 DPLL，offset/rate 只有 VDC owner 写，debug/provisional 结果不得进入正式 RUN。
  完整 EtherCAT 风格 RX gate、stale image、有限重发、降级/STOP、持久化和故障矩阵属于
  `P4-REL`，不阻塞 `P4-DBG`；但原始 FER 必须先独立收敛，不能用重试掩盖物理错误。
- 最新四板证据（诊断快照，非产品精度承诺）：
  `out/training/trn03d_node0_data_offset6_refinement32_g210_20260827/summary.json`
  中 Node0 DATA `+6` 候选 32/32 次 accepted，残差为 `+4` 9 次、`+5` 23 次、`+6` 0 次；
  `out/training/trn03d_node0_data_offset6_periodic_soak60_rerun_build041331_20260827/summary.json`
  为四板 61 点、总接收机会 64444、各 Node 原始 bad=0，最差 Node 零错误 FER 的 95% 上界约
  `1.86e-4`（约 186 ppm）。该样本只能说明本轮未观察到坏帧，不能替代更长时间的产品级
  BER/FER 目标；Node0..3 的 SD 原始流量和 1 us SVG 已生成。
- 文档结论：先以冻结矩阵和 `+6` 候选继续扩大 raw FER 样本、再接入硬件 latch 的
  `P4-LIVE`；调试路径与产品安全路径分离，避免前期因完整恢复闭环未完成而停止算法推进。
- 验证：文档门禁 `docs_check --strict-names`、`doc_regression_check`、文档 pytest 18 项和
  pre-commit 均通过；登记表既有 `TDMA-FLIGHT-BITMAP-01` 格式警告保持未混入本任务。
- 下一步：固化 P4-DBG replay/servo evidence 工具；完成最小 TDMA RX verdict 与 accepted/stale
  image owner；随后执行真实 PIO latch HIL，再补齐 P4-REL 的 retry/health/fault/soak 门禁。

## CAL-TASK-20260827-014 - TRN-03D 周期长稳与原始误帧压降基线

- 对应 TODO：`TRN-03D-PHY`、`TRN-03D-RXGATE`、`TRN-03D-RETRY`、
  `TRN-03D-HEALTH`、`TRN-03D-SOAK` 和 `VAL-04`。
- 状态：最新 release build 的四板 process-image 在短闭环后继续执行长稳；本轮证明坏帧能够被
  transport CRC 拦截且不会计入 accepted RX，但最差 Node 原始误帧尚未收敛，因此长稳不通过，
  不得用重试或四 Node 平均值掩盖该物理/采样层问题。
- 工具进展：`tools/calibration_ring_validate/trn03_closed_loop.py` 已加入 anchored 周期快照，逐间隔
  保存 up/down、adapter/FIFO/process-image/physical counter、瞬时错误、恢复次数和 counter
  单调性；冻结 matrix 的 profile level 与命令行冲突时在触碰硬件前拒绝。sticky 的
  `tdma_ring_runtime_reason_t` 只作为最近启动 reason 保留，不再误判为当前 down；在线健康仍由
  up/down、adapter/physical 状态和错误 counter 增量决定。
- 回归：周期采样、单 Node 查询失败、down/recovery、sticky reason 和原有短帧/SVG 用例已纳入
  `tests/python/test_trn03_closed_loop.py`；JUnit 位于
  `out/pytest/trn03_periodic_soak_sticky_reason.xml`。
- 首轮长稳（诊断快照，非事实源）：证据位于
  `out/training/trn03d_process_image_soak60_build041331_level7_20260827/`。四 Node sequence、FIFO、
  process-image 和 bitmap 持续增长，SD 原始流量及逐 Node SVG 均生成；SCK 波形分析全部通过。
  最差 Node0 在本轮接收机会中检出两个 `TDMA_TRANSPORT_CRC_MISMATCH`，其余 Node 未增长。
  最近错误的冻结 header 只在 transport CRC 字段出现一个 bit 差异；坏帧没有发布为有效镜像。
- 周期复测（诊断快照，非事实源）：证据位于
  `out/training/trn03d_process_image_periodic_soak60_build041331_20260827/`。周期工具保存首尾在内的
  完整 timeline；Node0 的一个 transport CRC 错误落在首个采样间隔，其余间隔和其余 Node 未见
  adapter bad counter 增长。四 Node SD/SVG 的频率和占空比门禁继续通过。
- 判定修正：本轮 HIL summary 仍由修正前工具把 sticky startup reason 计为 unhealthy；原始
  timeline、逐间隔 CRC 增量和最终 Node gate 均保留，修正后的分类器已有回归，下一轮 HIL 必须
  重新生成不含该误报的 summary，不能手工改写旧证据。
- 接收策略：先通过相邻 DATA offset、首帧启动和 CS/SCK/DATA 相位复测降低原始 FER；其后按
  EtherCAT 的确定性验收原则实现 CRC/sequence/identity/Node bitmap/WKC/deadline 全门禁、上一
  accepted image 的显式 stale、下一周期自然刷新、预算内有界重发、滑动窗口降级和 watchdog
  STOP。阈值必须进入 profile/candidate staging 并绑定 generation/CRC，不能成为 host 硬编码。
- 下一 gate：使用同一矩阵分别长跑 Node0 DATA offset 相邻候选，比较最差 Node 原始误帧与错误
  bit 分布；随后完成 RX gate/retry/health owner 设计、单测、release build、四板异步 OTA 和
  故障 HIL。

## CAL-TASK-20260827-013 - TRN-03B 复核与 TRN-03C path gate 证据更新

- 状态：TRN-03A 和 TRN-03B 保持完成；本轮在最新 path gate 固件上重复验证
  process-image、SD raw capture 和逐 Node SVG，不再将 endpoint-bias/P3-3 缺失计入
  TRN-03B。TRN-03C 已进入进行中，但尚未产生可激活的硬件 candidate。
- 代码进展：host 工具 `trn03_candidate_lifecycle.py` 已固化 candidate stage/activate/
  rollback JSON 生命周期；提交 `b5fd5fd` 使 `calibration_path_activation_gate_t` 必须
  匹配 candidate `calibration_generation`，并使 `calibration_path_snapshot_build()` 拒绝重复或
  缺失 source link。manager 激活/回滚成功后清除旧 candidate，防止重放。
- 验证：定向 Python/JUnit 报告为 `out/pytest/trn03c-path-gate.xml`，C host 双向
  校准回归由仓库固化脚本执行通过。本轮代码提交为 `9052d09` 和
  `b5fd5fd`，均已推送。
- 构建与 OTA（本轮诊断快照，非事实源）：构建产物位于
  `out/build/trn03-sck-safe-20260827/`。首次四板 OTA 在尾部传输阶段停留于
  `RECEIVING`，现场保存在 `out/ota/ota-stuck-20260827-0940/`；执行受控
  `SYST:OTA:ABOR` 后重新异步 OTA，恢复证据为
  `out/ota/trn03-sck-safe-recovery-20260827/`。随后 path-gate 固件的四板结果为
  `out/ota/trn03-path-gate-b5fd5fd-20260827/summary.json`，所有板卡按 `*IDN?`
  身份核对后完成更新。
- 四板闭环（本轮诊断快照，非事实源）：使用
  `out/training/trn03a_g210_matrix_clk_sys_phase_safe_20260827.json` 的完整矩阵 row，
  两次独立 process-image 闭环分别位于
  `out/training/trn03c_path_gate_process_b5fd5fd_retry1_20260827/` 和
  `out/training/trn03c_path_gate_process_b5fd5fd_repeat2_retry1_20260827/`。两次均完成四个
  Node 门禁、SD 原始环路流量下载、waveform replay 和逐 Node `1 us` SVG。
- 保留失败证据：第二次首次尝试位于
  `out/training/trn03c_path_gate_process_b5fd5fd_repeat2_20260827/`，Node0 在 stopped
  状态下幂等重配置已有 flight map 时遇到瞬时 map 锁竞争，`arm_result`
  返回 `DISTRIBUTED_REFMEM_TDMA_ARM_FLIGHT_MAP_REJECTED`。失败后四板均恢复
  STOPPED，同条件全新会话重试通过；该样本不计入 candidate repeat，并保留为
  并发回归输入。
- TRN-03C 当前拒绝原因：现有 P3 burst 包含 data burst/width 失败 trial，现有
  bias set 均为 `valid=0` 且无 accepted sample。原始 reference snapshot 只有本地
  TX 边沿，与四板环路接线不是本板 TX->RX 三线短接一致。下一步必须先
  切换本板 `CLK/SYNC/DATA` reference loopback 接线，生成有效 bias generation，
  再重测 fresh P3 和 candidate；不得放宽门禁或用 TRN-03B 短帧波形代替 bias。
- 未完成边界：evidence package 到 manager 的受控分段导入、四板真实
  stage/activate/VDC readback/rollback、掉电持久化，以及 TRN-03D 的 DMA overrun、
  PIO stall、Node dropout、stale identity 和长稳仍待完成。

## CAL-TASK-20260827-012 - TRN-03C candidate lifecycle owner 接入

- 状态：TRN-03C 生命周期边界已接入代码，但完整四板 candidate evidence、现场激活和回滚
  HIL 仍未完成，因此继续禁止发布 active calibration。
- host 工具：`tools/calibration_ring_validate/trn03_candidate.py` 新增 candidate CRC 校验、
  `activate_candidate()` 和 `rollback_candidate()`；generation 重放、坏 CRC、无效 active/
  rollbackable 包均拒绝。纯 Python 生命周期用例并入 `test_trn03_candidate.py`。
- 固件 owner：`CalibrationManager` 现在持有 candidate、active、rollback 三份 path snapshot；
  candidate staging 不改变 active，激活/回滚使用 `calibration_path_snapshot` 的 generation、
  topology/profile/schedule/freshness 门禁，并在成功激活后向 VDC 发布 accepted snapshot。
  `CALibration:ACTivate`、`CALibration:ROLLback` 不再使用无条件 accepted stub，失败进入 SCPI
  error queue；`READ:CALibration:ACTive?` 读取 manager-owned active 状态。
- 验证：构建目录为 `out/build/trn03-sck-safe-20260827/`，package 由该构建重新生成；定向
  Python 回归报告为 `out/pytest/trn03c-path-lifecycle.xml`（101 passed）；双向 C host unit
  为 `tools/tests/run_calibration_bidirectional_tests.ps1`（passed）。本轮四板异步 OTA 因
  当前 USB 端口会话未完成，未形成新的 HIL 证据，不能将此提交标记为 OTA 通过。
- 未完成边界：还需实现从 TRN-03C evidence package 到 manager candidate 的受控导入、真实
  四板 activate/rollback、掉电持久化和 VDC/DPLL 消费验证；当前代码提交为 `a59eeb3`。

## CAL-TASK-20260827-011 - TRN-03D DATA 线端与 MARK timeout 故障证据

- 状态：TRN-03D 已进入进行中；DATA 的旧 epoch、header CRC 错误、无故障正向控制和 MARK
  无下降沿 timeout 已完成各自 release build 的四板异步 OTA 与 HIL。DMA overrun、PIO stall、
  Node dropout、stale identity/rollback 和长稳仍待完成，因此不发布 active calibration。
- 代码闭环：marker 公共相关原语返回实际解码的 header、header inverse 和 header CRC8；DATA
  evaluator 从有效线端 header 重建语义 codeword 后独立计算 `observed_crc32`，不再用请求中的
  expected CRC 回填 observed evidence。每次 ARM 的 responder 故障配置随 request 固化，状态和
  SD capture 同时保留门限、故障参数、observed header/CRC 及 correlation reject reason。
- 拒绝优先级：generation identity 先于物理执行结果；随后按 PIO stall、DMA、timeout 保留首个
  根因，避免缺失的 CRC/correlation evidence 覆盖物理失败，且 PIO stall 同时引出的 DMA overrun
  仍归因于 PIO。没有物理错误时，DATA 的 header CRC 失败映射到
  `CALIBRATION_TRAINING_DATA_REJECT_CRC`，合法但旧 epoch 的 header mismatch 映射到
  `CALIBRATION_TRAINING_DATA_REJECT_EPOCH`，其余相关错误保持 correlation/distance/margin 拒绝。
  离线 SVG replay 执行相同的 inverse/CRC/epoch gate，不能仅凭 Hamming distance 标记 accepted。
- 构建与 OTA（本轮诊断快照，非事实源）：release build 目录为
  `out/build/trn03d-wire-evidence/`，build ID 为 `20260826212611`；四板异步 OTA 证据为
  `out/ota/trn03d-wire-evidence_20260827/summary.json`，四个按 `*IDN?` 识别的 Node 均更新成功。
- HIL（本轮诊断快照，非事实源）：旧 epoch 试验位于
  `out/training/trn03d_data_epoch_link0_g214_e224_20260827/`，线端 header CRC 有效且 observed epoch
  为请求前一 epoch，最终按 EPOCH 拒绝；单独 CRC8 扰动位于
  `out/training/trn03d_data_crc_link0_g215_e225_20260827/`，inverse 有效且按 CRC 拒绝；正向控制位于
  `out/training/trn03d_data_positive_link0_g216_e226_20260827/`，线端 `data_crc32` 与
  `observed_crc32` 相同并 accepted。正向控制的 configured offset 为 `+4` 拍、residual 为 `+1`
  拍，最终 Node offset 为 `+5` 拍；物理 base 仍来自该 link 的 `measured_link_delay / 2`，没有
  为 DATA 另造 base。
- 原始证据：三组试验均保存板端 `/cal/data_node0_link0_*.json`、host 下载的
  `node0_link0_capture.json`、`1 us` SVG 和 waveform analysis JSON；它们分别位于对应 HIL 目录，
  可从 observed header、CRC 和 raw waveform 重放拒绝结论。
- MARK timeout（本轮诊断快照，非事实源）：release build 目录为
  `out/build/trn03d-marker-timeout/`，build ID 为 `20260826221138`；四板异步 OTA 证据位于
  `out/ota/trn03d-marker-timeout_20260827/`。故障 trial 位于
  `out/training/trn03d_marker_timeout_g217_e227_20260827/`：Node0 发送 idle-high、没有下降沿，
  最终按 `CALIBRATION_TRAINING_MARKER_REJECT_TIMEOUT` 拒绝且 timeout counter 增长；其余 Node
  保持 PREPARED，统一 STOP 后四板恢复 IDLE，active record 前后一致。
- 无边沿证据边界：originator 在首个 PIO 输入边沿前进入 watchdog timeout，没有 raw DMA sample，
  因此本 trial 明确保存状态、counter、fault readback、STOP 和 active-before/after，且
  `capture_files` 为空；不生成或伪造 SD raw waveform/SVG。
- 回归与交付：DATA 线端闭环的 C/Python 证据和提交 `4e6911d` 保持不变；MARK timeout 闭环使用
  仓库固化 C 测试脚本，MARK/SCK/DATA/码字四组均通过，定向 Python 回归报告位于
  `out/pytest/trn03d-marker-timeout/python-results.xml`。代码提交 `b24e10e` 已推送到
  `feature/rtos-multicore-haofv`。
- 保护边界：本轮 `active_unchanged` 只证明现有 active raw record/CRC 未变化；manager 尚未接入
  完整 calibration candidate/active/previous generation，因此不能据此宣称 TRN-03C rollback 或
  active-generation 保护已经完成。

## CAL-TASK-20260826-010 - TRN-03A/B 四板短帧与 process-image 闭环

- 状态：TRN-03A staging/ARM gate、TRN-03B raw-flight 和 process-image/FIFO 四板闭环已通过；
  本任务仍保持 `DIAGNOSTIC_ONLY`，下一阶段进入 TRN-03C active candidate gate。
- 代码闭环：process follower 的 END 路径在 PIO IRQ 后显式回到 command pull，避免落入 PASS
  多采一个 raw byte；RX DMA absolute producer 使用 fixed-frame boundary 重建并保持单调；单帧
  解析失败排入 PASS 维持环路，迟到结果通过 `overlay_late_coalesce_count` 与真实
  `overlay_prepare_fail_count` 分离。
- staging/拒绝证据：TRN-03A 写后读回、ARM 和负门禁目录为
  `out/training/trn03a_g210_stage_after_data_fix_20260826/` 与
  `out/training/trn03a_negative_gates_final_20260825/`；训练输入统一来自
  `out/training/trn03a_g210_matrix_clk_sys_phase_20260826.json` 的 row0。
- raw-flight 证据：四板产品 flight persona 的最近复验证据位于
  `out/training/trn03b_g210_raw_flight_after_data_fix_retry2_20260826/`，与 process-image 使用同一
  calibration/topology/profile/schedule identity。
- 最终 process-image HIL（诊断快照，非事实源）：release build 为 `20260826055402`，四板异步
  OTA 目录为 `out/ota/trn03-process-late-coalesce-v9-20260826/`；STOP -> ARM 重复通过证据为
  `out/training/trn03b_process_late_coalesce_v9_row0_retry1_20260826/`。四节点 up/down、sequence、
  transport CRC、TX/RX FIFO、map apply 和 bitmap 均增长，follower replacement 增长，adapter
  bad 与 overlay prepare failure 均不增长，finally STOPPED 通过。
- PIO 计数证据（同一诊断快照）：follower 的 absolute producer 与
  `overlay_frame_boundary_count * overlay_physical_byte_count` 保持同一 fixed-frame 坐标，
  alignment byte/bit 在观测窗口前后不漂移。Node2 的迟到解析被计入 late coalesce，不再阻塞
  core1 或污染物理失败计数。
- SD raw SCK 证据（同一诊断快照）：四节点 `ring_capture_analysis.json` 均通过 frequency gate，
  period/high/low/duty 由 PIO raw sample 独立派生；逐节点 `1 us` SVG 位于最终证据目录的
  `analysis/`，不依赖 transport frame 是否能在局部窗口完整解码。
- 回归与交付：定向 Python 回归结果为
  `out/pytest/trn03-process-late-coalesce-results.xml`；release 构建目录为
  `out/build/trn03-process-late-coalesce-v9/`。代码提交 `c81defc` 已推送到
  `feature/rtos-multicore-haofv`。
- 下一步：TRN-03C 汇总 per-link path/residence/loop delay、PIO cycle budget 与 residual，执行
  bias、hardware latch、freshness、CRC、重复性和 rollback 门禁；通过前不得发布 active
  calibration。

## CAL-TASK-20260826-009 - MARK/SCK/DATA 统一相位训练路径

- 状态：已完成代码与 host 工具收敛，并完成本 build 的四板 OTA、SCK 零 offset 基线及
  推荐矩阵动态加载复验；MARK/DATA 的统一路径复验和 TRN-03 staging 尚未完成，因此保持
  `DIAGNOSTIC_ONLY`，不发布 active calibration。
- 统一模型：代码事实源为 `calibration_training_phase.h` 与 `calibration_phase.py`；MARK、
  SCK、DATA 均按 `link_base_delay = measured_link_delay / 2`、`effective_phase =
  round(link_base_delay / sample_period) + node_offset` 计算。codebook half-chip 只参与波形
  编解码，不再作为物理链路 base。
- 统一阶段：三种信号均输出 `HAOFV_UNIFIED_PHASE_TRAINING_V1`，按 PIO 起始边沿、raw capture、
  SD 保存、离线相关/SVG、零 offset 基线、全量 Node offset 矩阵、动态加载和 residual repeat
  gate 执行；电气引脚和 PIO persona 只作为 adapter 差异。
- 统一矩阵：`build_offset_rows()` 是全量 Node 笛卡尔积唯一生成器；MARK 调用该生成器，SCK
  和 DATA 通过 `build_observed_offset_matrix()` 生成相同通用 row，并保留信号专用兼容字段。
  offset 范围与 Node 容量引用 `CALIBRATION_TRAINING_PHASE_*`，不在各训练项目重复定义。
- 固件构建：`out/build/sck-independent/DHRT100_UPDATE.pkg` 已成功生成；本次诊断快照的
  `payload_sha256` 为 `b3006a96e61e0495174421b3c5dd6ee4ff721c4cd8dd28c5609f35542c4e57ad`。
- 四板 OTA（诊断快照）：`out/ota/unified-phase-20260826/summary.json` 记录全部目标 Node 更新
  成功并读回 package build；板卡身份、端口、旧/新 build 和逐板日志均保存在该目录。
- SCK 零 offset 基线（诊断快照）：证据目录
  `out/training/sck_unified_baseline_0000_20260826/`。全环独立 repeat 全部 accepted；各 link
  的最终 offset 众数均为 `+1` sample，生成的推荐矩阵为 `[+1,+1,+1,+1]`。全部 SD raw
  capture 均生成对应的 `node*_link*_sck_capture_replay_1us.svg`。
- SCK 动态加载复验（诊断快照）：证据目录
  `out/training/sck_unified_offset_p1p1p1p1_20260826/`。推荐矩阵加载后全环 repeat 全部
  accepted 且无 gate failure；residual 以零拍为众数，少量 trial 落在相邻的负一拍，最终
  推荐矩阵保持 `[+1,+1,+1,+1]`。这属于原始采样网格的量化分布，不再回写第二套算法。
- 回归：公共路径及 MARK/DATA/SCK/TRN-03 定向 Python 测试通过；MARK、DATA、SCK C host
  单测通过。测试数量属于本次运行快照，事实源为 `out/pytest/` 和构建日志。
- 下一步：用同一 package 和统一工具复验 MARK、DATA 的零 offset 基线、逐 Node SVG、矩阵
  动态加载及 residual repeat；三类矩阵均 accepted 后复验 TRN-03 staging 写后读回与 raw-flight。

## CAL-TASK-20260825-008 - MARK 扩样检查点与 SCK 独立训练决策

- 状态：已完成 MARK 矩阵工具归因修复、固定 identity 的零 offset 扩样和离线 SVG 对齐；
  已确认 SCK 必须使用自身 capture origin 独立完成两级训练。SCK 解耦尚未实现，本检查点
  不继续刷板、不发布 active calibration，TRN-03A 恢复为进行中。
- 代码检查点：当前分支与远端均为提交 `b69f1a8`；板端仍运行 build `20260825083548`。
  本轮没有固件修改，因此没有重新构建或 OTA。
- MARK 工具修复：
  - `calibration_marker_train.py` 与 `calibration_marker_offsets.py` 将 offset 归因从错误的
    `source_node` 改为实际加载 capture offset 的 `destination_node`；matrix 默认固定
    epoch/codeword，并让每个 row 进行多次独立 `STOP -> ARM -> inject`。
  - 默认 repeat 数、逐 node gate、distance 统计和推荐 row 已固化在工具中；本地
    `topology_generation` 不再错误要求跨板相同，topology/profile/schedule CRC 仍必须一致。
  - 定向回归为本轮诊断快照：`43 passed`，pytest 目录为
    `out/pytest/mark_matrix_repeat_fix/`。
- MARK 零 offset 扩样（诊断快照，非事实源）：固定 codebook `1`、epoch `91` 和
  `[0,0,0,0]`，共执行 `32` 次独立 trial。证据目录为
  `out/training/mark_baseline_0000_repeat8_cb1_e91_g136_20260825/` 与
  `out/training/mark_baseline_0000_repeat24_cb1_e91_g144_20260825/`。

| Node | accepted | best lag 分布 | distance median | distance max |
|---|---:|---|---:|---:|
| node0 | 25/32 | `0:29, 1:3` | 441.5 | 1202 |
| node1 | 32/32 | `0:15, 1:17` | 1 | 228 |
| node2 | 32/32 | `0:25, 1:7` | 37.5 | 480 |
| node3 | 29/32 | `0:27, 1:5` | 129.5 | 954 |

- 解释：独立 ARM 后环路会落入相邻的 raw sample 相位状态，一拍量化变化属于正常观测；
  静态 offset 必须依据更大样本的拍差直方图、众数/中位数和拒绝比例选择，不能要求每次
  distance 完全相同，也不能凭单张 SVG 决定最终矩阵。node0 返回和 node3 的拒绝必须保留
  为有效失败 evidence。
- SVG 对齐：g136、g137、g138 的候选 delay 分别为 `[-4,0,0,-4] ns`、
  `[+4,+4,+4,+4] ns` 和 `[-4,0,-4,-4] ns`；换算关系为
  `offset_delta_samples = -candidate_delay_ns / sample_period_ns`，本轮 sample period 快照为
  `4 ns`。JSON/SVG 位于基线目录的 `baseline_analysis/g136/`、`g137/`、`g138/`。
- 失败搜索证据：`[+3,-2,0,+2]` 与 `[+2,-2,0,+2]` 两个候选 row 均各执行独立 repeat，
  因 node0 返回重复门禁失败；证据分别位于
  `out/training/mark_matrix_repeat8_p3m2p0p2_cb1_e91_g120_20260825/` 和
  `out/training/mark_matrix_repeat8_p2m2p0p2_cb1_e91_g128_20260825/`。失败不从训练集删除。
- SCK 架构审计：当前 `calibration_training_sck.c` 和 `calibration_sck_train.py` 的公式/请求
  仍包含 `source_marker_offset_sample_count`、`destination_marker_offset_sample_count` 和
  `marker_to_sck_samples`，会把 MARK 的一拍量化误差叠加到 SCK。正确路径是 SCK 使用 PIO
  内部启动、已知 SCK burst 和返回 raw capture，独立形成 SCK-TRN-01/02 offset matrix；
  `mark_sck_skew` 只作为最终 guard/window 验收，不回写任一信号的物理 offset。
- 下一步：先移除 SCK 相位公式对 MARK offset 的依赖并补 host 回归；随后用固定 identity、
  独立 repeat 和全量 node matrix 运行四板 SCK 基线，保存 SD raw capture、逐 node `1 us` SVG、
  拍差分布和失败比例；完成后重新生成并验证 TRN-03A staging，再进入 raw-flight。

## CAL-TASK-20260825-007 - TRN-03B 原始短帧飞行 persona 接通

- 状态：完成 raw byte-level flight 的代码、工具分级、host 单测和固件构建；尚未刷入四板，
  `raw-flight` HIL 未通过前不得进入 `process-image`，TRN-03B 保持进行中。
- 完成内容：
  - reference 在 TDMA ARM 时选择 `TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN`，follower 选择
    `TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER`；reference 预装 DATA DMA 并产生有界 CS/SCK
    burst，返回流由独立 RX capture 接收。
  - follower PIO 在同一 wire pass 中再生 SCK、反向流水 DATA 并捕获 RX；修复 `push` 清空 ISR
    导致转发字节为零的问题。ring adapter 的 `PHYSICAL_FLIGHT` 模式不再执行完整 RX 后的
    第二次 software TX，产品 runtime 固定选择该模式，host fake phys 保留 store-forward 测试模式。
  - RX scanner 支持 bit-shift 恢复 packet magic；历史 `copy_normal_capture` API 兼容 NORMAL 与
    两种 flight persona，保留既有 SCPI、SD capture 和 SVG 工具链。
  - `trn03_closed_loop.py` 增加 `raw-flight` 与 `process-image` 两级门禁；前者验证 persona、
    sequence、CRC 和物理计数，后者才要求 TX/RX FIFO 与 map apply 增长。
- 验证结果：
  - `tools/tests/run_tdma_pio_spi_ring_adapter_tests.ps1` 通过，构建目录为
    `out/pytest/build-tdma-flight-ring-adapter`。
  - TRN-03 定向 Python 回归通过，临时目录为 `out/pytest/trn03-flight-runtime-temp`。
  - `tools/cmake_build_auto/cmake_build_auto.py --preset pico2-release` 构建通过，产物目录为
    `out/build/trn03-flight-runtime`；具体 package size/CRC 属本轮构建快照，以目录内产物为准。
- 未完成边界：当前 follower 是透明 byte pipeline，尚未在本 node 的固定 segment 到达时从
  active TX image 替换内容；飞行修改后的 WKC、尾部 transport CRC、segment 完整性以及四板
  SD capture/SVG 证据均未完成，不能宣称 EtherCAT-style process-image flight 已闭环。
- 下一步：使用现有多板异步 OTA 工具刷入本轮 package，只执行
  `trn03_closed_loop.py --stage raw-flight`。若失败，优先核对 persona、bit-shift magic、DMA
  produced words、timeout/stall、训练 offset 和 tail budget，并保存全部 node 的 SD capture/SVG。

## CAL-TASK-20260825-006 - TRN-03B NORMAL 波形闭环与捕获消费恢复

- 状态：TRN-03B 的板端 NORMAL RX/TX 捕获、Core1 latch、SD 保存、四板并行下载和逐 node
  SVG 已形成可重复诊断闭环；捕获请求不再因一次未消费而永久停在 `PENDING`。TDMA 短帧环路
  本身仍未通过，因此 TRN-03B 保持进行中，不发布 active calibration。
- 已确认原因：旧请求入口把 `PENDING` 当作不可覆盖状态。若某次 core0 发布恰好未被 Core1
  及时消费，所有后续 latch 都被拒绝，偶发 park/stall 或观察窗口错过会被放大为永久不消费。
  修复后 mailbox 采用 latest-wins，新 sequence 可覆盖旧 pending；generation、capture epoch
  和 guarded snapshot 仍阻止保存旧证据。host 默认进行有界重试，并记录 `latch_attempts`。
- 诊断边界：旧固件首次停顿发生时尚无阶段计数，因此不能从既有证据严格区分 Core1 瞬时
  park/stall 与 intent 未观察。新固件增加 `core1_service_count`、
  `intent_read_fail_count`、`last_seen_sequence`、`copy_attempt_count`、
  `copy_fail_count` 和 `consumed_sequence`。本轮最终四板 HIL 中，每个 node 首次 latch 即被
  消费，intent 读取与物理快照复制均无失败；这证明恢复路径和复制路径有效，但不倒推旧瞬态
  的唯一成因。
- 波形语义修正：RX 保存 SCK 上升沿原始采样流；TX 保存最近一个完整短帧，而不是连续历史
  尾部。捕获容量和版本引用 `TDMA_PIO_SPI_NORMAL_CAPTURE_BYTES` 与
  `TDMA_PIO_SPI_NORMAL_CAPTURE_VERSION`。旧数据长度可由完整帧长度整除，历史工具截取的
  尾部恰为零填充，因此旧 SVG 的全零 TX 不是内存破坏。V2 分析器找不到完整 RX 帧头时发布
  `N/A` 和原因，不再误报零 shift。
- 固化工具：`tools/calibration_ring_validate/trn03_waveform.py` 负责 V1/V2 读取、完整帧校验、
  方向矩阵对比和 SVG；`trn03_closed_loop.py` 负责 Core1 latch、SD save、四板并行下载、分析
  及 finally STOP；`trn03_matrix.py` 把 capture 元数据纳入 TRN-03 配置。训练层仅使用
  node/link/loop 术语。
- 构建与 HIL（本轮 bench 快照，非事实源）：Release 产物位于
  `out/build/trn03b_capture_retry`，build `20260825051259`；四板异步 OTA 证据为
  `out/ota/trn03b_capture_retry_20260825/summary.json`。最终运行证据位于
  `out/training/trn03b_capture_retry_final_20260825/summary.json`，各 node 的 SD 保存、下载、
  消费诊断和 STOPPED 回退均成功。
- SVG 证据：`out/training/trn03b_capture_retry_final_20260825/analysis/` 下的
  `node0_ring_capture_1us.svg` 至 `node3_ring_capture_1us.svg`。本轮波形显示 node0 发送完整帧，
  node1 在 node0 marker/SCK 下只捕获到 idle-low DATA，node2/node3 未形成后续 TX；这与
  “完整帧 RX 后才 TX”的环形等待一致。下一步必须实现 PIO cyclic/cut-through 数据转发，
  不能靠继续调整 offset 解除该等待。
- 回归证据：定向 Python 测试结果为
  `out/pytest/trn03b_capture_retry_20260825.xml`；本条中的通过数、build 和计数均仅代表本轮
  evidence snapshot，不是接口常量。

## CAL-TASK-20260825-005 - TRN-02 固定阶梯退出与 TRN-03A 四板 ARM

- 状态：`TRN-02B/D` 已完成固定 operating-profile 阶梯的四 link 多次重复门禁，
  `TRN-03A` 已完成完整 replay matrix 的自动生成、四板 staging、ARM 状态读回和
  STOPPED 回退。`TRN-03B/C/D` 尚未完成，当前结果不发布为 active calibration，
  也不向 VDC/DPLL 提交。
- 固件与拓扑（bench 诊断快照，非事实源）：四板 build 为 `20260825023511`，物理 node
  顺序为 `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A ->
  A1E549202D18ED6A -> 0010071E65B5CB38`。MARK 使用 codebook 1 和 node offset
  `[+1,-1,0,+1]` 拍；DATA 使用 codebook 0、reverse 方向和 configured offset
  `[+5,+5,+5,+5]` 拍；以上均为本轮训练配置，不是固件写死值。
- TRN-02 正向证据（本轮快照）：level 7/8/9 分别绑定 calibration generation
  `101/102/103`。每档 residence 的四条 link 均为三次 `1` 拍且 span 为 `0`；每档 DATA
  均为 `12/12` accepted，四 link repeat span 不超过工具参数
  `max_offset_span_sample`。三档 profile gate 的 identity、完整 link 集、DMA/PIO/timeout
  和 residence 配对检查全部通过。
- 固化工具：`trn02_profile_gate.py` 改为按 level 接受 DATA/residence 成对 summary，拒绝
  缺档或混合 identity；新增 `trn03_matrix.py`，从 paired evidence 自动派生 NORMAL PIO
  周期、DATA window、codeword、forward residence、loop delay 和 link budget。生成器通过
  源码锚点测试约束 persona、系统时钟、bit cycles 与 operating-profile 表，并先按 RP2350
  PIO 16.8 分频器量化 `clkdiv` 后再派生各 cycle，禁止人工填写 residence。
  `trn03_stage.py` 增加 stage/link 全字段写后比对、逐 node ARM 状态读回和
  STOPPED 回退读回；训练报告不复用 `slot` 名称。
- TRN-03A HIL（本轮快照）：level 9 generation 103 的完整四 link matrix 在四板均读回
  `complete=1`、valid bitmap `15`；ARM 后四板均为 ring enabled、adapter started 且 local
  node 为 `0..3`，随后 STOP 后均恢复 disabled/stopped。此前空矩阵、缺 link、
  diagnostic-only 和预算过期的四板负向门禁继续作为拒绝证据。
- 证据索引：profile gate 为
  `out/training/trn02_profile_gate_g101_g103_20260825.json`；三档 matrix 为
  `out/training/trn03_matrix_level7_g101_20260825.json`、
  `out/training/trn03_matrix_level8_g102_20260825.json`、
  `out/training/trn03_matrix_level9_g103_20260825.json`；正式 ARM 为
  `out/training/trn03a_level9_g103_arm_quantized_20260825/summary.json`；负向门禁为
  `out/training/trn03a_negative_gates_final_20260825/summary.json`；Python 固化回归为
  `out/pytest/calibration_trn03_final_20260825.xml`。
- 下一步：进入 `TRN-03B`，按 ring role 装载产品 flight persona 并先执行四板
  `raw-flight`；通过后再要求 TX/RX FIFO、segment replacement 和 map apply 同时增长。
  失败必须保持当前 STOPPED 回退语义。

## CAL-TASK-20260824-004 - TRN-02 profile gate、offset 故障注入与 TRN-03A staging

- 状态：TRN-02 的三组 profile 证据已由固化工具聚合；offset 故障注入已增加离线回归，
  但 TRN-02B/D 仍不能宣称完成，因为 forward residence 尚未与每个 profile 的同一
  calibration generation 绑定。无 residence 或 generation/profile 不匹配时，gate 明确失败。
- 固化工具：`tools/calibration_ring_validate/trn02_profile_gate.py` 汇总 level 7/8/9
  的 12/12 link trials、profile identity 和 residence 兼容性；
  `tools/calibration_ring_validate/trn02_offset_fault.py` 生成偏移故障的预期失败类别。
  当 offset 将窗口推出边界时，预期为 `TIMEOUT_EXPECTED_WINDOW_MISSED`；结果禁止
  active candidate 和 TRN-03 staging。
- TRN-03A 代码入口已增加 2..8 节点的 per-link calibration stage、generation/topology/
  profile/schedule identity、PIO 时序字段和 link budget 检查；缺链路、混合身份或
  不可重放预算在 ARM 前拒绝，并保持 STOPPED。当前仍需补齐板端 stage/query 编排和
  四板 HIL，因此 TRN-03A/B/C/D 不改变为完成。
- 验证：`out/training/trn02_profile_gate_20260824.json` 明确记录当前 gate failure；
  Python calibration tests 10/10、TRN-02 offset fault manifest、C training-data 和
  TDMA ring-runtime host tests 均通过。

## CAL-TASK-20260824-003 - TRN-02 四链路单 profile 收敛与 TRN-03 入口审计

- 状态：`TRN-02A` 和 `TRN-02C` 已完成；`TRN-02B` 和 `TRN-02D` 仍为进行中。当前
  四链路单 profile 矩阵通过，但尚未满足多次独立 repeat 和固定频率阶梯全 profile
  验证，因此不得进入四板 TRN-03 ARM/START。
- 固件与拓扑（bench 诊断快照，非事实源）：四板均运行 build `20260824125459`；
  accepted physical order 为 `0010071E65B5CB38 -> FB276192BEF9CCE1 ->
  2BD5090FE009FA2A -> A1E549202D18ED6A -> 0010071E65B5CB38`。
- 运行时训练参数（本轮快照）：MARK 方向为 forward，DATA 方向为 reverse；
  MARK per-Node offset 为 `[+1,-1,0,+1]` 拍，DATA per-Node configured offset 为
  `[+5,+5,+5,+5]` 拍；采样周期为 `4 ns`，逻辑基准 delay 为 `40 ns`。方向和 offset
  均由 host 按现场拓扑动态提交，未写死到 PIO 程序。
- HIL 结果（本轮快照）：calibration generation `87`，epoch `126..129`，四条
  directed link 全部 accepted；topology/profile/schedule CRC 和 sample period 在矩阵内一致，
  DMA overrun、PIO stall 和 timeout 均未增长。残差为 `[-1,-1,0,0]` 拍，故校准后
  逻辑 delay 为 `[36,36,40,40] ns`，均在 `40 ns ± 1` 拍内。
- delay 语义：校准后逻辑 delay 按 `base_delay + residual` 计算；物理捕获中心按
  `base_delay + configured_offset + residual` 计算。DATA configured offset 只调整 initiator
  capture phase，不并入同时参与长等待的 `base_delay_ns`，避免重复补偿。
- 证据目录：`out/training/trn02d_data_offsets_5555_e126_g87_20260824/`；根
  `summary.json` 记录 `TRN-02D_REPEAT_MATRIX`、`repeats=1`、四链路各自判定和统一
  identity bundle；每条链路目录保存 SD capture 下载、离线 replay JSON 和 `1 us` SVG。
- 构建与部署证据（本轮快照）：固件位于 `out/build/trn02-configured-data-offset/`；
  四板异步 OTA 证据位于 `out/ota/trn02-configured-data-offset_20260824/`；配置 offset 与
  delay 标签回归报告位于 `out/pytest/calibration_configured_offset.xml` 和
  `out/pytest/calibration_delay_label.xml`。
- TRN-03 入口审计：现有矩阵只含一个 profile CRC，且每条链路只有一次最终
  repeat；`TRN-02B` 要求的 forward residence 仍需与同一 generation 证据绑定。下一步
  使用已固化的 `tools/calibration_ring_validate/calibration_data_train.py` 跑完 operating
  profile 固定阶梯的四链路多次独立 repeat；所有 profile 通过后才关闭
  `TRN-02B/D` 并开始 `TRN-03A`。
- 回退点：任一 profile/link/repeat 失败均保留 raw evidence，整环保持 STOPPED，
  不修改 active calibration generation，不向 VDC/DPLL 发布本轮 diagnostic window。

## CAL-TASK-20260824-002 - TRN-01 退出与 TRN-02 入口准备

- 状态：`TRN-01A..D` 已按当前 build/拓扑的 diagnostic 门禁收口；`TRN-02A` 进入实现期，
  尚未开始独立 DATA 码元训练，不得把 CS marker offset 提升为 DATA window 或 active calibration。
- 固件与拓扑（bench 诊断快照，非事实源）：四板均运行 build `20260824090429`；accepted
  physical order 为 `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A ->
  A1E549202D18ED6A -> 0010071E65B5CB38`。板卡身份只使用 `*IDN?` 唯一地址，COM 号仅为
  本轮临时传输端点。
- TRN-01 退出证据：
  - 零 offset 基线使用 epoch `90`、calibration generation `60`，四节点全部 accepted；ARM
    阶段四节点均为 PREPARED 且 `dma_capture_count=0`，证明 INJECT 前不存在假 marker 边沿。
  - 依据离线全局重叠方向动态加载 `[+1,-1,0,+1]` 个 sample 的四节点 offset；一拍为
    当前 capture 的 `4 ns` 采样周期。本轮 epoch `91`、calibration generation `61`，四节点
    仍全部 accepted，firmware distance 为 `9/7/8/9`，离线最佳残差均为 `0 ns`。
  - 两轮均满足同一 epoch/sequence/marker CRC、normal polarity、完整 marker flags、node0
    返回 marker、DMA overrun/stall/timeout 为零；训练结束恢复 NORMAL persona。
  - 一拍结果只说明当前 CS marker capture window 在该 build、topology、codebook 和节点状态下
    收敛；它不是独立 DATA offset，也不清除 `DIAGNOSTIC_ONLY`。
- 证据目录：
  - 零 offset：`out/training/trn01_arm_inject_marker_latch_baseline_e90_0000_20260824/`；
  - 一拍复核：`out/training/trn01_arm_inject_one_sample_p1m1p0p1_e91_g61_20260824/`；
  - 两个目录均包含 `summary.json`；一拍目录额外保存四节点 SD capture、离线 replay JSON 和
    参考/原始/最佳对齐三行的 `1 us` SVG。
- TRN-02 入口审计：
  - 当前 marker persona 在 CS 输入/输出线上生成、转发和捕获 coded marker；现有相关器、
    golden vector、CRC/epoch/polarity/best/second peak/margin 基础逻辑可以复用。
  - 当前尚无独立的 marker 后 DATA TX/RX PIO 路径，也没有 `marker_data_skew`、DATA
    best/second peak、training window/guard staging。因此 marker HIL 不能替代 `TRN-02A/B`。
  - 下一硬件入口固定为 `node0 -> node1` 单链路：先由 PIO 在 marker 后发送已知 DATA
    codeword，再以 P3 per-link diagnostic candidate 形成有界搜索窗口；只有单链路连续 trial
    accepted 后才轮换其余 directed link。
- TRN-02A 软件门禁基座：
  - 新增 `calibration_training_data` guarded request/evidence/snapshot，独立保存 source/destination、
    epoch/sequence、codebook/CRC、generation/CRC bundle、sample period、marker-to-DATA 间隔、
    base delay、搜索区间、guard、best/second peak、margin、polarity、window 和 skew。
  - evaluator 覆盖 generation、旧 epoch、sequence、CRC、evidence flags、相关器拒绝、反相、
    capture truncation、DMA/stall/timeout、distance、margin、search range 和 edge order；接受结果
    始终保留 `DIAGNOSTIC_ONLY`。
  - 固化 `tools/tests/run_calibration_training_data_tests.ps1`；host C 测试覆盖正常、单拍错位、
    反相、截断、重复峰、旧 epoch、越界搜索、DMA fault 和非法窗口。
  - 固件 A/B/Boot 在 `out/build/calibration-marker-arm-inject-marker-latch/` 完成构建，Flash map、
    persistence、wire、SCPI namespace 和三镜像 link gate 通过；本轮未 OTA，因为独立 DATA PIO
    和板端 SCPI 尚未接入。
- 回退点：TRN-02 任一失败只停止 training persona、保留本任务的 TRN-01 diagnostic evidence
  和旧 active generation；不得 ARM 四板 TDMA，不得向 VDC/DPLL 发布结果。

## CAL-TASK-20260824-001 - TRN-01 字段基座与四板 marker 基线

- 状态：`TRN-01A` 进行中；C snapshot、seqlock store、拒绝矩阵和 host evidence parser
  已建立，尚未接入 core1 marker persona 或板端 SCPI，因此不得声称 `TRN-01` 环路通过。
- 四板基线（本轮诊断快照，非事实源）：使用现有固化工具
  `tools/calibration_ring_validate/calibration_clk_coded.py`，按 accepted physical order 在统一
  旧固件上执行四主 coded-marker trial，四个 master 均 accepted，marker 完整性、DMA 和
  PIO fault 门禁通过。证据位于 `out/training/p2_marker_baseline_20260824/`；该结果仍是 P2
  coded RTT，不包含每个 follower 的 marker capture/forward tick，不能替代 `TRN-01D`。
- 实现：新增 `calibration_training_marker` request/evidence/snapshot 模型，覆盖 identity、node、
  epoch/sequence、marker/codebook/CRC/polarity、generation/CRC bundle、硬件 capture/forward tick、
  residence、DMA/PIO/timeout 和 `DIAGNOSTIC_ONLY`；新增
  `tools/calibration_ring_validate/calibration_marker_train.py`，只校验固件导出的硬件证据，
  不允许 host 生成或修正边沿时间戳。
- 验证（本轮构建/测试快照，非事实源）：C marker 单测通过；校准相关 Python 回归通过；
  Release 固件在 `out/build/trn01-marker/` 完成编译和 Flash link gate，构建产物尚未部署到
  四板。pytest 和 C host-test 临时产物均位于 `out/pytest/`。
- 下一步：实现 `TRN-01B/C` 的 core1 PIO marker capture/cut-through persona 与 bounded
  intent/prepare-ack，增加板端 `MARKERTRN` 状态查询；只有四板同 epoch 的 capture/forward
  evidence 经 host 工具验证后，才把 `TRN-01A` 和 `TRN-01D` 关闭。

## CAL-TASK-20260822-007 - P2 分辨率输入下的四板 TDMA 环路运行

- 状态：诊断运行完成但环路未闭合；校准数据仍为 `DIAGNOSTIC_ONLY`，不得进入 active
  calibration 或 VDC/DPLL。
- 目标：在 P2 编码 marker 已将 raw 采样细化到 4 ns 的前提下，使用校准结果为 TDMA
  四板环路提供初始 loop-delay 窗口，并记录运行态证据。
- 固件与身份：四块板均为 DHRT100 build `20260822111137`；身份只取 `*IDN?` 唯一地址，
  COM3/COM5/COM6/COM4 仅为本轮临时传输端点。accepted physical order 为
  `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A -> A1E549202D18ED6A ->
  0010071E65B5CB38`。
- P2 输入：四主节点 level 8 重复结果为 `10/10` accepted，best lag 为 `100/101`
  个 raw sample，即 `400/404 ns`；单次硬件分辨率按 `sample_period_ns=4 ns` 记录。
  本轮只将 `402 ns` 和 `8 ns` 容差作为候选窗口，不能视为已经写入板端的正式校准值。
- TDMA 运行：使用既有
  `tools/tdma_ring_monitor/tdma_start_ring.py`，按四个唯一地址配置 node count 4、
  reference node 0，level 8，训练 `4096` cycles，然后 ARM/START。四板 topology、node
  map 和训练命令均完成；证据目录为
  `build-product-release/tdma_ring_p2_runtime_20260822`。
- 运行结果：启动快照中四板均为 `ring_node_count=4`、`ring_adapter_started=1`、
  `ring_up_running=1`，但 `ring_down_running=0`；参考板 TX 增长而 RX 未增长，四板
  `ring_adapter_rx_bad_count` 未见增长。因此本轮结论为 `UP_ONLY / LOOP_NOT_CLOSED`，
  不是稳定四板环路通过。
- 配置边界：对四板查询 `SYSTem:TDMA:RING:LOOP:DELay?` 均未得到响应，不能据此声称
  `loop_delay_ns=402,tolerance=8` 已生效。需先确认板端 build 已包含该 SCPI/配置路径，
  在 STOPPED 状态写入并读回，再重新 ARM/START。
- 只读监控：运行期间使用
  `tools/tdma_ring_monitor/tdma_ring_monitor.py` 对 NO.1/NO.2 的持久串口端点执行
  90 秒只读采样；监控证据目录为
  `build-product-release/tdma_ring_p2_runtime_monitor_20260822`。监控不得替代四板全量
  运行态证据，结束后必须对全部节点执行 STOP 并关闭串口。
- 下一步：确认并 OTA 含 loop-delay 配置的统一固件；停止当前环路后四板读回 loop-delay、
  feedback timeout、up/down 状态，再以 10 MHz/25 MHz 逐级复测。只有四板 `up_running=1`、
  `down_running=1`、参考节点反馈证据增长、bad/overrun 不增长且重复统计通过，才可进入
  P3/DPLL 前置门禁。

## CAL-TASK-20260822-006 - P3 双向测距线角色动态映射

- 状态：代码完成，待四板硬件复测；P3 仍为 `DIAGNOSTIC_ONLY`。
- 固件新增 `CLK_DATA` 与 `CS_DATA` 两组请求。两组分别选择独立的 PIO catalog persona，
  core1 在每次请求完成后停止 SM/DMA 并卸载，不能在同一已装载 persona 内切换信号组。
- P3 以 `forward_line`、`return_line`、`sync_line` 表达角色，不把 CLK/CS/DATA
  物理名称当成功能。`CLK_DATA` 为 `forward=CLK, return=DATA, sync=CS`；
  `CS_DATA` 为 `forward=CS, return=DATA, sync=CLK`。同步线只打开捕获窗口，
  不进入 `t1/t2/t3/t4` 或 path-sum。
- 两组共用逻辑 `t1/t2/t3/t4` 掩码和 path-sum 方程。当前网线链路按正反向对称处理，
  `delay_estimate = corrected_path_sum / 2`；`asymmetry_ns` 和对应 gate 保留，
  后续非对称介质仍可拒绝或降级 active eligibility。
- SCPI `CALibration:P3:STARt` 保留原 5 参数，第 6 参数 `signal_group` 可选，`0=CLK_DATA`、
  `1=CS_DATA`；验证工具 `--signal-group BOTH` 会逐组执行并分别记录证据。
- 本轮验证：主机 P3 测试 8/8、P3 plan 测试 10/10，DHRT100 双核固件和 Flash link gate 通过；
  尚未在四板上 OTA/复测两组信号，不能据此宣称三线硬件校准完成。

## CAL-TASK-20260822-003 - Accepted calibration evidence SD source

- 状态：SD source evidence 已接入；Flash Calibration NVS 仍未实现，不能恢复为 active。
- `calibration_bias_snapshot_validate()` 通过后，`CALibration:SAVE` 才会提交有界文件写入
  intent；Storage manager 以临时文件 + 原子 rename 写入 `/cal/accepted_<unique-id>_g<generation>.json`。
- 文件固定携带 `unique-id`、build、generation、sample/accepted/rejected count、persona/profile/
  topology generation、mean bias、spread 和 snapshot CRC；`active=false` 明确表示它是后续
  Flash M2-03 的输入证据，而不是 VDC/DPLL live fact。
- 当前四块板的 reference-loopback 复测仍为 `edge_mask=0x5`、`result_valid=0`、四边沿时间戳为
  零，因此没有执行成功的 SAVE；该失败证据保存在
  `build-dhrt100-p3-dpll-20260822/calibration_bias_reference/COM3..COM6`。
- 固件 build `20260822095115` 的双镜像、OTA package 和 Flash link gate 通过；四板物理
  P3 仍保持 `DIAGNOSTIC_ONLY`，不能绕过 bias/freshness gate 写入 accepted source。

## CAL-TASK-20260822-001 - P3 path snapshot admission and VDC bridge

- 状态：代码门禁和四板诊断闭环均完成；当前不清除 `DIAGNOSTIC_ONLY`。
- 路径快照增加独立 `max_asymmetry_ns` 门限；active 快照必须具备硬件锁存、bias/topology
  freshness、重复统计和 asymmetry 有效标志，并通过快照 CRC。
- VDC/DPLL manager 新增 Calibration snapshot 发布入口，逐链路复制 delay/jitter、generation
  和 freshness，构造 accepted VDC path-delay table；默认零延迟 staging 表仍被拒绝。
- `calibration_bidirectional` host tests、DHRT100 双核固件构建和 Flash link gate 已通过。
- 四板已恢复枚举；已先用 `*IDN?` 唯一地址确认环序，再执行完整 10/25/30 MHz 频率阶梯。

## CAL-TASK-20260822-004 - P3 当前固件四板复测

- 状态：当前 DHRT100 build 的四板逐链路 P3 复测完成；所有诊断 trial 通过，但结果仍为
  `DIAGNOSTIC_ONLY`，未生成 active path-delay，也未提交 VDC/DPLL。
- 固件与拓扑（bench 诊断快照，非校准事实源）：
  - 四块板均为 DHRT100 build `20260822095115`；板卡身份由 `*IDN?` 返回的唯一地址确定，
    COM3/COM5/COM6/COM4 仅作为临时传输端点。
  - accepted physical order 为
    `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A -> A1E549202D18ED6A ->
    0010071E65B5CB38`。
- HIL 参数与结果：
  - 使用 `tools/calibration_ring_validate/calibration_link_p3.py`，每条相邻链路执行
    10/25/30 MHz、每档 3 次、32 脉冲、256 capture words；证据目录为
    `build-dhrt100-p3-dpll-20260822/p3_recheck_20260822`。
  - 36/36 trial accepted；每条链路和每档频率均为 3/3，initiator/responder edge mask
    为 `9/6`，DMA overrun 和 PIO stall 均为 0。
  - 10 MHz 实测频率 10 MHz、占空比约 52%；25 MHz 实测频率 25 MHz、占空比约 50%；
    30 MHz 实测约 30.303 MHz、占空比约 48.485%，保持 `LIMITED_RX`，稳定档最高为 25 MHz。
  - delay estimate 范围为 80..82 ns；四条链路均值为 80..81.333 ns，重复标准差为
    0..0.943 ns；responder residence 保持 20 ns。以上均为当前 build/拓扑/线缆/收发器的
    诊断观测，不是 endpoint bias 扣除后的单向事实。
- active gate 复核：
  - `calibration_path_delay_probe.py` 对四块板均返回 `MISSING`，`timestamp_resolution_ns`
    和 `timestamp_flags` 仍为 0；因此本轮不能发布 active path-delay。
  - 下一步仍是接入同一 PIO persona 的板内三线 reference loopback，生成有效 bias
  generation，再补 freshness、asymmetry 和故障注入后运行 active snapshot admission。

## CAL-TASK-20260822-005 - P3-3 板内 reference loopback 复测

- 状态：四块板均完成当前固件的短 reference-loopback 检查；四块板均未形成有效
  Calibration bias snapshot，P3-3 仍被物理三线回环连接阻塞。
- 固件与身份：四块板均返回 DHRT100 build `20260822095115`；`*IDN?` 返回的唯一地址为
  `0010071E65B5CB38`、`FB276192BEF9CCE1`、`2BD5090FE009FA2A`、`A1E549202D18ED6A`。
  COM3/COM5/COM6/COM4 仅是本轮临时传输端点。
- 验证命令与证据：使用既有
  `tools/calibration/calibration_loopback_validate/calibration_loopback_validate.py`，每板 64 words、
  1 次短窗口；证据目录为
  `build-dhrt100-p3-dpll-20260822/reference_loopback_recheck/<unique-id>`。
- 结果：四板均为 `runs_passed=0`；固件快照没有有效四边沿，`result_valid=0`，residence、
  raw path-sum 和 delay estimate 均为 0；`SYST:ERR?` 均为 `0,"No error"`。因此这是
  reference-loopback 缺失/未闭合的有效拒绝，不是 SCPI、USB 或 PIO 错误。
- HAOFV 结论：不得用本轮零值生成 bias generation，也不得执行 `CALibration:SAVE`；接好每块
  板同一 PIO persona 的 `CLK_TX->CLK_RX`、`DATA_TX->DATA_RX`、`SYNC_TX->SYNC_RX` 后，
  必须重新运行多轮 bias 采样并通过 generation、CRC、freshness 和 accepted-count 门禁。

## CAL-TASK-20260822-002 - P3 四板逐链路三档复测

- 状态：四条相邻链路的 P3 诊断 HIL 完成；结果仍为 `DIAGNOSTIC_ONLY`，未生成 active
  per-link calibration，未提交给 VDC/DPLL。
- 日期：2026-08-22。
- 固件与拓扑（bench 诊断快照，非校准事实源）：
  - 四板均运行 DHRT100 build `20260822085100`；板卡身份只取 `*IDN?` 唯一地址，临时
    COM 端点为 COM3/COM5/COM6/COM4。
  - accepted physical order 为
    `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A -> A1E549202D18ED6A ->
    0010071E65B5CB38`；拓扑证据目录为
    `build-dhrt100-p3-dpll-20260822/calibration_topology_20260822`。
- HIL 参数与结果：
  - 使用 `tools/calibration_ring_validate/calibration_link_p3.py`，每条链路执行
    10/25/30 MHz、每档 3 次、每次 32 脉冲和 256 capture words；完整证据目录为
    `build-dhrt100-p3-dpll-20260822/calibration_p3_20260822`。
  - 36/36 trial accepted；四条链路、三档频率均为 3/3，initiator/responder edge mask
    分别为 `9/6`，合并四边沿完整，DMA overrun 和 PIO stall 均为 0。
  - 10 MHz 实际频率为 10.000 MHz、占空比约 52%；25 MHz 为 25.000 MHz、占空比约
    50%；30 MHz 为约 30.303 MHz、占空比约 48.48%，按策略归类为 `LIMITED_RX`，不进入
    稳定 profile。
  - 4 ns PIO/DMA 采样量化下，observed delay estimate 范围为 78..82 ns，单链路重复
    jitter 为 0..2 ns；responder residence 为 20 ns。该值是当前 build/拓扑/线缆/收发器
    的对称假设观测值，不是 endpoint bias 扣除后的单向事实。
- HAOFV 边界与结论：
  - SCPI/core0 只提交有界 intent；PIO、SM、DMA、方向控制和边沿收割均由 TDMA core1
    owner 持有，Calibration 只配对同 epoch raw evidence 并执行质量门禁。
  - 本轮证明四板逐链路 P3 传输、频率/占空比门禁和三档策略可重复通过；不证明 endpoint
    bias、长时间温漂、四段 cumulative/residual 或 VDC/DPLL active 准入。
- 下一步：完成 P3-3 板内 endpoint bias/reference loopback，补齐缺边沿/乱序/重复、
  SYNC/CRC/epoch、频率偏差、asymmetry、DMA/stall 和 topology/profile freshness 故障注入，
  再以新的 generation 运行 active snapshot admission。

## 当前任务状态

| 任务 | 状态 | 结论 |
|---|---|---|
| 校准域职责与 TDMA/VDC 边界 | `[x]` | 校准拥有测量与接受门禁，TDMA 负责传输与编排，VDC/DPLL 消费 accepted snapshot |
| 线序与环路顺序测量 | `[~]` | host 隔离探测、闭环判定和 NO 提交已迁入 calibration 命名空间；板内 generation/freshness 待实现 |
| 第一阶段 CLK RTT 粗捕获 | `[x]` | 已完成板内最小实现和四板 HIL，仍为 diagnostic-only |
| 第二阶段编码 marker/相关测距 | `[~]` | 动态 persona、固定双 DMA、板端相关和四主最小 HIL 已完成；重复门限及板内多板协调待完成 |
| 第三阶段双向同时对比法 | `[~]` | 四板四链路 10/25/30 MHz 三档诊断 HIL 已完成 36/36；endpoint bias、故障注入、freshness/active gate 待完成 |
| VDC/DPLL active calibration gate | `[ ]` | 依赖正式 hardware latch、bias、generation/freshness 和 P3 结果 |

## CAL-TASK-20260821-010 - COM8 当前固件重写与单板回环复核

- 状态：当前 HEAD 已通过 OTA 重写并提交到 slot 2；短接线接好后单板双向回环 10/10
  accepted。结果继续保持 `REFERENCE_LOOPBACK + DIAGNOSTIC_ONLY`，active calibration 仍为
  `FIELD_DEFAULT`。
- 日期：2026-08-21。
- 固件与板卡（bench 诊断快照，非校准事实源）：
  - 源码 commit 为 `f1e942ee0b4b8057ab7077e17be2755035421d35`，build ID 为
    `20260821130800`；OTA package 由 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`
    提供。
  - COM8 当次对应唯一地址 `839E1AE79EA20F31`；OTA boot/commit 后 slot 2 为 committed，
    `SYSTem:ERRor?` 返回 `0,"No error"`。证据目录为
    `build-rtos-multicore-smoke/ota_boot_commit_calibration_current`。
  - 验证保持 12 V 断开，仅使用 USB 供电，避免已知高侧电流采集前端共模拓扑再次发热。
- 单板回环（bench 诊断快照，非校准事实源）：
  - 第一次 10 轮为 0/10，固定只有本地 TX 边沿，随后确认原因是回环线未接；这不是 GPIO、
    PIO persona 或固件回归证据。
  - 接好 `CLK_TX -> CLK_RX`、`SYNC_TX -> SYNC_RX`、`DATA_TX -> DATA_RX` 短接线后，同一
    build 重跑 10 个独立 epoch，10/10 accepted；每轮 `edge_mask=0xF`、latch flags `0x7`、
    reject reason 为 0，residence 为 960..980 ns、raw path-sum 为 100..120 ns、对称假设下的
    observed delay estimate 为 50..60 ns。
  - 证据目录为
    `build-rtos-multicore-smoke/calibration_loopback_com8_build_20260821130800_rewired`；其中
    `summary.json` 绑定 build、板卡地址、epoch、四边沿、公式结果和最终错误队列。
- HAOFV 边界与恢复：
  - 回环仍由 TDMA core1 owner 持有 PIO/SM/DMA、驱动器方向和四边沿收割；core0/SCPI 只
    提交 bounded intent 并读取 guarded snapshot，host 未注入实时边沿时间戳。
  - STOP 后常驻 TDMA service count 持续递增，维护 persona 已退出；单板未形成 accepted
    topology，因此保持 `ring_enabled=0`、adapter stopped 和 adapter error=0，不强行 START。
  - `READ:CALibration:CLOCk:CODEd?` 与 `READ:CALibration:P3?` 当前无新运行结果；endpoint
    bias/reference generation、topology/profile freshness、active/staging CRC 和 VDC/DPLL
    consumer gate 仍未完成，禁止清除 `DIAGNOSTIC_ONLY`。
- 传感器（USB-only bench 诊断快照，非器件精度事实源）：
  - `SYSTem:DIAGnostic:SENSors?` 返回板载温度约 34.0 C、RP2350 内部温度约 39.2 C；电流
    前端输出约 1.4465 V，frontend plausibility 为 true，未校准 nominal estimate 约 79 mA。
  - 该结果只说明 12 V 断开时前端处于合理零点附近；在高侧共模拓扑修复前不得恢复 12 V。

## CAL-TASK-20260821-009 - P3 四板逐链路双向测距

- 状态：P3-1/P3-2 和四板逐链路诊断 HIL 完成；结果继续保持 `DIAGNOSTIC_ONLY`，未生成
  active per-link calibration，未提交给 VDC/DPLL。
- 日期：2026-08-21。
- HAOFV 实现边界：
  - TDMA core1 owner 动态装载 initiator/responder PIO persona，持有 SM、DMA、驱动器方向和
    4 ns GPIO24..31 采样；core0/SCPI 只发布 guarded intent 和读取 snapshot。
  - initiator 记录本地 `t1=CLK_TX`、`t4=DATA_RX`，responder 记录本地
    `t2=CLK_RX`、`t3=DATA_TX`；host 只配对同 epoch evidence 并计算
    `path_sum=(t4-t1)-(t3-t2)`。
  - 新增 `CALibration:P3:STARt/STOP`、`READ:CALibration:P3?` 和
    `tools/calibration_ring_validate/calibration_link_p3.py`；板卡身份只使用 `*IDN?`
    唯一地址，COM 仅是当次端点。
- OTA 与 HIL（bench 诊断快照，非校准事实源）：
  - 四板升级到 build `20260821100236`，证据目录
    `build-product-release/calibration_p3_ota_20260821`。
  - 四板物理顺序为 `0010071E65B5CB38 -> FB276192BEF9CCE1 ->
    2BD5090FE009FA2A -> A1E549202D18ED6A -> 0010071E65B5CB38`。
  - 先做 10 MHz 四段单轮 smoke，4/4 accepted；完整阶梯再对四段分别执行
    `10/25/30 MHz`、每级 3 个独立 epoch。基础四边沿、CLK 频率/占空比、DMA/stall 门禁
    为 36/36 accepted，证据目录为 `build-product-release/calibration_p3_full_20260821`。
  - 各 link 的单程对称估计均落在 `80..82 ns`；该 2 ns 台阶来自 4 ns RTT 采样量化后
    除以二。每轮 responder residence 均为 20 ns，DMA overrun 和 PIO stall 均为零。
  - 采样得到 10 MHz 为 10.000 MHz、source duty 52%；25 MHz 为 25.000 MHz、source
    duty 50%；30 MHz 多数为约 30.303 MHz、source duty 48.48%。最后一段 responder
    30 MHz duty 最低为 42.42%，当前在 4 ns 采样诊断容差内，但须由示波器复核电气波形。
  - 发现基础门禁遗漏返回 DATA pulse width 后，工具已增加 `data_high_ok`（理想半周期
    量化值允许一个 sample period 误差）。30 MHz 四段各补跑 10 轮，共 39/40 accepted；
    NO.1→NO.2 第 6 轮的 DATA_RX 高电平只有 8 ns，被 `initiator_data_width` 正确拒绝。
    证据目录为 `build-product-release/calibration_p3_30m_data_gate_20260821`。因此 25 MHz
    是当前四段保守稳定档；30 MHz 改为 `LIMITED_RX` 有界诊断接收档，不能进入 active
    profile。后续每次 P3 验证固定执行
    `calibration_link_frequency_policy.REQUIRED_FREQUENCY_LADDER_MHZ` 完整阶梯，即使稳定档
    失败也不跳过 `LIMITED_RX_FREQUENCY_MHZ`；该档任一拒绝记录 `FALLBACK_25MHZ` 并按
    `LIMITED_RX_FALLBACK_MHZ` 回退，但不使已通过的稳定档总判定失败。
- 剩余门禁：
  - 同 persona endpoint bias/reference generation 尚未完成，因此当前 `delay_estimate` 只是
    对称假设下的 observed value，不是产品单向延迟事实。
  - 尚未完成缺边沿/乱序/epoch/frequency/asymmetry 故障注入、topology/profile freshness、
    四段 cumulative 与整圈 residual 对比及长时间温漂统计。
  - P3 snapshot 尚未形成 active/staging CRC、generation 和 VDC/DPLL consumer gate；不得
    清除 `TDMA_PIO_SPI_P3_FLAG_DIAGNOSTIC_ONLY`。
- 验证：
  - `cmake --build build-product-release -j 4` 通过并生成 A/B 固件、UF2 和 OTA package。
  - `python -m pytest tests/python/test_calibration_link_p3.py
    tests/python/test_calibration_link_plan.py -p no:cacheprovider`：10 项通过。
  - P3 基础 HIL 的 12 个 link/frequency level 全部 3/3 通过；增加 DATA width 门禁后的
    30 MHz 加严复测为 39/40，详细四时间戳、频率、占空比、脉宽、residence、path-sum
    和失败门禁见两份 evidence `summary.json`。
  - `calibration_link_p3.py` 与 `calibration_link_plan.py` 已拒绝不完整频率列表，保证每次
    验证包含 `LIMITED_RX_FREQUENCY_MHZ`；summary 分离 `STABLE_REQUIRED` 与 `LIMITED_RX`，
    并按 `LIMITED_RX_FALLBACK_MHZ` 发布 fallback 状态。该策略只改变诊断评分，不放宽
    DATA/CLK 频率、占空比或脉宽门限。

## CAL-TASK-20260821-006 - P2 重复统计与 TDMA PIO 时序校正

- 状态：P2 重复诊断完成；普通 TDMA TX PIO 时序校正完成；P3 正式准入仍未满足。
- 日期：2026-08-21。
- P2 重复 HIL（build `20260821044448`，accepted physical order）使用
  `tools/calibration_ring_validate/calibration_clk_coded.py`，每个 master 独立执行 10
  轮，结果目录：
  - `build-product-release/calibration_clk_coded_p2_four_repeat10_level7`
  - `build-product-release/calibration_clk_coded_p2_four_repeat10_level8`
  - `build-product-release/calibration_clk_coded_p2_four_repeat10_level9`
- 结果摘要：
  - 10 MHz：40/40 accepted，NO.1/2/4 的 lag span 为 1，NO.3 为 2；各主节点最小
    margin 为 `22/79/25/57`。
  - 25 MHz：40/40 accepted，四主 lag span 均为 1；各主节点最小 margin 为 `4/14/26/36`。
  - 30 MHz：40/40 accepted，NO.1 lag span 为 2，其余为 1；各主节点最小 margin 为
    `14/42/47/14`。
  - 三档均无 DMA overrun、PIO stall、marker flag 缺失或 mixed peak；结果仍为
    `DIAGNOSTIC_ONLY`，不能据此缩短 40 ns robust marker，也不能生成 active delay。
- 发现并修正普通 TDMA TX PIO 时序偏差：原实现实际为 7 cycles/bit、2:5 高低比，
  与分频函数声明的 6 cycles/bit 不一致；修正为 6 cycles/bit、3:3 高低比。静态工具
  `tools/tdma_ring_monitor/tdma_pio_timing_check.py` 及测试
  `tests/python/test_tdma_pio_timing_check.py` 已通过；报告为
  `build-product-release/tdma_pio_timing_check_10_25_30_fixed.json`。
- OTA：四板均使用唯一地址白名单升级到最终 build `20260821061831`；证据目录
  `build-product-release/ota_final_pio_fix_20260821`。中间 build `20260821061232` 曾试验
  5 ms command-mailbox commit guard，但四板 coded START 仍由 guarded snapshot 确认、CDC
  payload 超时，且一次后续 trial 遇到端点重枚举；该无收益 guard 已从最终源码和固件移除。
- 最终 build 使用 NO.1 做一次四板 coded smoke 通过：lag=100、distance=89、margin=351，
  marker flags 完整且 DMA/stall 为零；证据目录
  `build-product-release/calibration_clk_coded_final_smoke`。该数字仍是 bench 诊断快照。
- HAOFV 边界：PIO waveform 属于 TDMA transport/persona 实现；Calibration 只消费
  profile/raw evidence 和 coded correlation 结果，不把静态 timing check 当作硬件 latch
  或 VDC/DPLL calibration evidence。示波器仍需确认 rise/fall、线缆和收发器影响。
- 下一步：在修正 waveform 的 build 上重新跑 10/25/30 MHz 的多板 coded HIL；只有在
  40 ns fallback 的重复统计满足统一 margin/lag gate 后，才逐级评估更短 marker；随后再
  补 `TRAIN_PREPARE/ACK/commit`、endpoint bias 和真实 `t1..t4` hardware latch，决定是否
  进入 P3 双板 diagnostic 阶段。

## CAL-TASK-20260821-007 - 码元档位筛选与回环反射时序复核

- 状态：P2 candidate 筛选完成一轮；32 ns 通过，24 ns 退化，20 ns 因板卡暂时未全部
  重新枚举而未作无效判定；P3 正式准入仍未满足。
- 日期：2026-08-21。
- OTA：四板按 `*IDN?` 唯一地址白名单升级到 build `20260821062825`，证据目录为
  `build-product-release/ota_coded_marker_20260821`；四板 post-commit build 查询均一致。
- coded marker 单轮筛选使用 `tools/calibration_ring_validate/calibration_clk_coded.py`：
  `codebook=3`（32 ns）为 4/4 accepted，lag histogram 为 `100/100/101/100`，无
  DMA overrun、PIO stall 或 mixed peak，最小 margin=133；证据目录
  `build-product-release/calibration_clk_codebook3_screen`。这些数字是绑定 build、拓扑
  和接线的 bench/诊断快照，不是产品精度事实源。
- `codebook=2`（24 ns）仅 NO.1 accepted，NO.2--NO.4 为
  `correlation_manchester` reject（证据目录 `build-product-release/calibration_clk_codebook2_screen`），
  因此不能缩短 robust marker；在四板重新枚举后再决定是否补测 20 ns，不能把缺板状态
  当作 20 ns 结果。
- 回环反射校准静态复核已扩展 `tools/tdma_ring_monitor/tdma_pio_timing_check.py` 与
  `tests/python/test_tdma_pio_timing_check.py`：`tdma_pio_spi_clk_burst` 在 10/25/30 MHz
  的理论占空比均为 50%，频率误差在静态门限内；`tdma_pio_spi_clk_forward` 不使用本地
  baud divider，频率由上游 RX CLK 决定。报告为
  `build-product-release/tdma_pio_timing_check_reflection_20260821.json`；电气
  rise/fall、ISO1452、连接器和线缆影响必须以示波器实测。
- HAOFV 边界：Calibration 只接受 coded snapshot/raw evidence；PIO waveform、burst、
  forward 和 DMA 资源仍归 TDMA/core1 owner，core0/SCPI 不进入边沿热路径。

## CAL-TASK-20260821-008 - P3 逐链路低频推进基线

- 状态：P3 尚未开始正式测量；已完成低频优先策略、在线板卡确认和 active path-delay
  基线读取，当前 firmware 不具备板间四边沿 persona，故不能生成逐链路结果。
- 日期：2026-08-21。
- 四板按 `*IDN?` 唯一地址重新枚举成功，physical order 保持
  `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A -> A1E549202D18ED6A`，
  build 均为 `20260821062825`；证据目录为
  `build-product-release/inventory_p3_frequency_baseline`。
- 已用 `tools/calibration_ring_validate/calibration_path_delay_probe.py` 读取四板当前
  path-delay 与 TDMA 状态；四板均返回 `MISSING`，`ring_enabled=0`，
  `ring_timestamp_resolution_ns=0`，没有 active per-link calibration；证据目录为
  `build-product-release/calibration_p3_baseline_path_delay`。
- 频率/占空比门禁沿用 `10 -> 25 -> 30 MHz`。静态报告
  `build-product-release/tdma_pio_timing_check_reflection_20260821.json` 已证明 burst
  在三档为 50% duty，forward 为上游边沿再生；示波器仍需确认电气波形。
- 下一步必须先完成 P3-1/P3-2：板间 `CLK/DATA/SYNC` 同 epoch marker、四边沿 hardware
  latch、residence/path-sum snapshot 和逐 link SCPI/工具入口；完成后才能从 NO.1→NO.2
  的 10 MHz 第一段开始，再依次推进 NO.2→NO.3、NO.3→NO.4、NO.4→NO.1，并逐级提升
  到 25/30 MHz。
- 物理方向已复核：同一 BiSS 段为 A.CLK_TX `GPIO25` -> B.CLK_RX `GPIO28`，同时
  B.DATA_TX `GPIO29` -> A.DATA_RX `GPIO24`，SYNC 使用 `GPIO26/27` 关联 epoch；ISO1452
  固定方向与 P3 的 `t1/t2/t3/t4` 双向方程一致。

## CAL-TASK-20260821-005 - P2 coded marker 四板最小闭环

- 状态：SCPI/工具最小闭环和四主单轮 HIL 完成；结果保持 diagnostic-only，产品级协调与
  重复门限未完成。
- 日期：2026-08-21。
- 完成内容：
  - 新增 `CALibration:CLOCk:CODEd:STARt/STOP` 和
    `READ:CALibration:CLOCk:CODEd?`；core0 只提交 guarded intent，板卡唯一地址、build、node、
    topology/profile/schedule CRC、baud 和 generation 由固件自动绑定。
  - 修复 STOPPED 维护态的 topology metadata 生命周期：Calibration 只读 TDMA service 的
    last accepted `ring_staged_config`，不改变 `RING:STOP` 清空 live runtime 的既有语义。
  - 新增 `tools/calibration_ring_validate/calibration_clk_coded.py`，按 `*IDN?` 唯一地址发现、
    follower 先于 master、四主轮换、guarded snapshot 判定、STOP/IDLE 收尾并输出 UTF-8
    JSON/CSV；COM 号仅为当次传输端点。
  - `M255_MANCHESTER_20` 主峰可见但严格字段门禁拒绝；回退
    `M255_MANCHESTER_40` 后，accepted physical order 的四个 master 单轮全部通过。
- HIL 快照（build `20260821044448`，10 MHz，非阈值事实源）：
  - accepted order 为 `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A ->
    A1E549202D18ED6A`；四主 best lag 为 `100/100/101/101` raw samples。
  - best distance 为 `23/152/0/154`，margin 为 `442/184/485/179`；四主 marker flags 全通过，
    DMA overrun 和 PIO stall 均为零。
  - 单轮结果仍使用显式宽松 diagnostic gate，不能据此冻结 distance/margin threshold，不能
    清除 `CALIBRATION_CLK_CODED_FLAG_DIAGNOSTIC_ONLY`。
- OTA 与证据：
  - 四板 OTA 完整通过，四个唯一地址均运行 build `20260821044448`；证据目录
    `build-product-release/ota_coded_p2_20260821_final`。
  - 四主 HIL 证据目录
    `build-product-release/calibration_clk_coded_p2_four_accepted_order`。
  - NO.1 调试 UART COM7 读取 15 秒无 fault/watchdog 日志；UART 不是板卡身份。
- 遗留：
  - coded START/STOP 的 CDC action response 在 persona 切换时可能超时；工具仅在 snapshot
    sequence 和 request metadata 完全匹配时确认接受，产品闭环仍需消除此响应时序问题。
  - 实现 TRAIN_PREPARE/ACK/commit sequence，并完成四主多次重复、10/25/30 MHz 统计和
    profile/topology change、掉线、commit miss 故障注入。

## CAL-TASK-20260821-004 - P2 动态 PIO persona 与 coded raw transport

- 状态：固件算法基座、动态 persona、固定双 DMA 和 Calibration core1 最小闭环已完成；
  板端触发接口、多板协调、硬件 capture-origin HIL 和正式阈值尚未完成。
- 日期：2026-08-21。
- HAOFV 边界：
  - Calibration 生成和解释 marker，执行有界相关、generation 门禁及 accepted/rejected 发布。
  - TDMA core1 是 PIO/SM/DMA persona 唯一 owner，只搬运 packed TX sample 和 bounded raw capture。
  - core0 只能向 Calibration guarded command mailbox 发布 request/gate；SCPI 不直接操作 PIO/DMA。
- 完成内容：
  - 实现 candidate M255 Manchester marker 的 C/Python golden vector、header/反码/CRC、正反相
    相关和旧 epoch、缺失/重复、截断、低 margin 故障注入。
  - PIO2 改为 `tdma_pio_spi_program_persona_t` 动态装载：`NORMAL`、`CLOCK_COARSE`、
    `CAL_LOOPBACK`、`CLOCK_CODED` 互斥存在，切换前检查两个 SM 和 TX/RX DMA 均停止；失败
    尝试恢复上一 persona，完成后恢复普通 persona。
  - `CLOCK_CODED` master 使用 `TDMA_PROFILE_DEFAULT_TX_DMA_CHANNEL_ID` 和
    `TDMA_PROFILE_DEFAULT_RX_DMA_CHANNEL_ID` 驱动 packed TX 与 oversampling RX 固定窗口；
    follower 只启用 CLK forwarding，不解析 marker。
  - DMA resource claim 已从 `TdmaFoundationProfile` 传播到 ring runtime、service 和物理层，
    profile 声明与硬件实现不一致时拒绝 arm。
  - Calibration core1 构建 marker、提交 raw request、收割 guarded capture，并调用
    `calibration_clk_coded_process_core1()`；成功结果继续保留 `DIAGNOSTIC_ONLY`。
- 验证结果：
  - `run_calibration_clk_marker_tests.ps1`、TDMA profile/runtime/adapter/service 与 RefMem-TDMA
    相关主机单测全部通过；Python codebook evaluator 为 6/6 通过。
  - `cmake --build build-product-release` 完成 A/B 编译和统一包生成；build
    `20260821041729`，该编号仅为本轮构建快照，非接口事实源。
  - 当前没有板端 coded HIL，不能把 software `capture_origin_tick` 当作正式 hardware latch，
    不能清除 `DIAGNOSTIC_ONLY`，也不登记 candidate marker 为冻结跨域契约。
- 下一步：
  - 增加最小 SCPI/工具触发闭环并实测 candidate 半码元档位；随后实现
    `TRAIN_PREPARE/ACK/commit sequence` 和四主轮换。
  - 以 PIO/DMA hardware capture origin、重复统计、profile/topology freshness 和 persona
    恢复作为 HIL 放行门禁。

## CAL-TASK-20260821-003 - 线序与环路顺序测量迁入校准域

- 状态：host 工具、命名和文档所有权迁移完成；板内 topology generation/freshness 尚待实现。
- 日期：2026-08-21。
- 任务目标：
  - 将有向线序/邻接矩阵、单闭环判定、anchor 旋转、node map 和 NO 提交归入校准域。
  - 校准测量工具统一使用 `calibration_*` 名称；TDMA 只提供隔离 probe transport 和计数证据。
  - 禁止 TDMA START 根据调用参数隐式写入 NO，避免未校准顺序覆盖已接受 topology。
- 完成内容：
  - `tdma_ring_autodetect.py` 迁移为
    `tools/calibration_ring_validate/calibration_ring_topology.py`，默认报告目录改为
    `calibration_ring_topology_*`。
  - 输出增加 `measurement_domain=calibration` 和 measurement phase；保留 directed pair
    evidence、adjacency、ring order、node map 和完整 snapshot。
  - `--anchor-id` 决定 accepted ring 的 NO.1；`--assign-no` 只在单闭环判定通过后提交映射，
    支持写后读回及 `--reboot-verify-no` 持久化复核。
  - `tdma_start_ring.py` 删除 NO 分配和启动时隐式 NO 写入；它只消费调用者提供的 accepted
    calibration order 显式映射到 TDMA/RefMem 边界的 local/reference slot ID。
  - 第一阶段和码本工具同步迁移为 `calibration_clk_train.py`、
    `calibration_clk_codebook_eval.py`，相应 Python 测试也改用 calibration 命名。
- 当前边界：
  - 工具已能生成 host 诊断 topology，但固件尚未发布带 CRC、generation、freshness 和
    accepted/rejected reason 的 `CalibrationTopologySnapshot`。
  - 本次不新增 wire layout 或阈值冻结契约；阈值仍来自显式工具参数/profile。
- 四板实测（build `20260821021250`，bench 诊断快照）：
  - 使用 `calibration_ring_topology.py --level 7 --adjacency-only` 完成全部有向板对隔离扫描，
    accepted ring 为 `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A ->
    A1E549202D18ED6A -> 0010071E65B5CB38`；本轮未写 NO。
  - 首轮发现 RX counter 普通回退被误算为 32-bit wrap，产生一个无 raw-word/edge 的假分支；
    工具已限定 wrap 只在 counter 高端到低端时成立，普通回退发布 regression 并按零增量处理。
  - 修正后 topology PASS；随后 `calibration_clk_train.py` 沿 accepted order 执行 10 MHz
    四主轮换，四个 master 均得到 `[400,500) ns`，无 mixed point，结果保持 diagnostic-only。
- 证据目录：
  - `build-product-release/calibration_ring_topology_four_10m_final`
  - `build-product-release/calibration_clk_train_four_10m_final`

## CAL-TASK-20260821-002 - 第一阶段归属迁移与供电入口 A/B 复测

- 状态：第一阶段文档归属迁移完成；四板粗捕获复测通过，结果保持 diagnostic-only。
- 日期：2026-08-21。
- 任务目标：
  - 将 CLK RTT 第一阶段流程、bracket 解释、四主结果和质量结论统一迁入校准域。
  - TDMA 只保留 PIO/SM/DMA、core1 command mailbox、persona、窗口和 raw evidence transport。
  - 将默认训练阶梯收敛到 operating profile level 7/8/9 对应的 `10 -> 25 -> 30 MHz`。
  - 通过把供电入口从 NO.2 移到 NO.1 的 A/B 复测，判断相邻码元桶差异是否跟随供电入口。
- 固定上下文：
  - build：`20260821021250`；物理环序：
    `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A -> A1E549202D18ED6A -> NO.1`。
  - 板卡身份来自 `*IDN?` 唯一地址；本轮 COM3/COM5/COM6/COM4 仅为临时传输端点。
- 验证结果（bench 诊断快照，非通用时序事实源）：
  - 供电入口 NO.2：10 MHz 四主均为 `[400,500) ns`；25 MHz 仅 NO.2 落入
    `[440,480) ns`；30 MHz 的 NO.2/NO.3 落入 `[434,467) ns`，其余为相邻低桶。
  - 供电入口 NO.1：10 MHz 四主仍为 `[400,500) ns`；25 MHz 改为 NO.3 落入
    `[440,480) ns`；30 MHz 仅 NO.2 落入 `[434,467) ns`。
  - 全部 trial 通过且没有 mixed point；相邻桶没有跟随供电入口移动。当前现象按码元边界
    量化解释，不把 40 ns/约 33.3 ns 的桶差声明为真实供电传播延时。
- 证据目录：
  - `build-product-release/tdma_clk_train_four_10m`
  - `build-product-release/tdma_clk_train_four_25m`
  - `build-product-release/tdma_clk_train_four_30m`
  - `build-product-release/tdma_clk_train_four_power_no1_10m`
  - `build-product-release/tdma_clk_train_four_power_no1_25m`
  - `build-product-release/tdma_clk_train_four_power_no1_30m`
- 边界：
  - 第一阶段 bracket 不生成 active path-delay、endpoint bias、feedback timeout 或 DPLL 样本。
  - 后续由 P2 编码 marker/过采样在 coarse bracket 内缩窗；TDMA 不复制测量公式和评分。

## CAL-TASK-20260821-001 - 单板回环双向测距预研

- 状态：本轮单板前期算法和纯 PIO/DMA 闭环目标完成；结果保持 reference-only / diagnostic-only，
  endpoint bias generation 和双板 P3 仍待完成。
- 日期：2026-08-21。
- 任务目标：
  - 使用单板 TX/RX 三线回环先验证双向同时对比法的公式和 reject gate。
  - 保持单板结果为 `REFERENCE_LOOPBACK`，不进入 active calibration 或 VDC/DPLL。
  - 核对小板现有 resident TDMA loopback 的实际物理状态，为后续 PIO edge-latch 接入准备证据。
- 完成内容：
  - 新增 `calibration_bidirectional.h/.c`，实现 `t1..t4` 本地时钟域检查、
    `residence_B = t3 - t2`、`path_sum = (t4 - t1) - residence_B`、endpoint bias 扣除、
    clock-rate bound、DMA/epoch/persona/topology/bias gate 和 `active_eligible` 判定。
  - 双板语义不比较 `t1` 与 `t2` 的绝对先后，只分别检查 A 域的 `t4>=t1` 与 B 域的 `t3>=t2`。
  - 新增 `test_calibration_bidirectional.c` 和 `run_calibration_bidirectional_tests.ps1`；
    覆盖有效回环样本、坏顺序、缺边沿、DMA 错误、persona/topology/bias/clock-rate/reference
    policy、negative path 以及 hardware-latched 非回环样本。
  - 将模块加入顶层 CMake，主固件 A/B 均完成重新编译和打包。
  - PIO 回环已收敛到 `TdmaRuntimeOwner`：停止态复用 TDMA 已声明的 PIO、TX/RX SM 和
    `TDMA_PIO_SPI_RX_DMA_CHANNEL`；`CALibration:LOOPback:STARt <words>` 只提交有界 intent，
    `READ:CALibration:LOOPback?` 只读取 guarded raw/result snapshot。
  - 固件端执行 `t1..t4` edge decode、SYNC/edge-mask/顺序门禁和现有
    `calibration_bidirectional_evaluate()`；单板结果始终带 `REFERENCE_LOOPBACK` /
    `DIAGNOSTIC_ONLY`，不会进入 active calibration 或 VDC。
- 单板实测结果（诊断快照，非校准事实源）：
  - 当前 USB CDC 枚举为 `COM8`，`COM7` 本轮未枚举；板卡地址为
    `839E1AE79EA20F31`，运行 build 为 `20260820082350`。
  - `tdma_single_board_loopback.py` 15 秒窗口完成 `STOP -> LOCAL -> ARM -> TRAIN -> START`；
    TX/RX sequence 和 adapter TX/RX 均增长，adapter/phys bad、magic fail、ring overrun
    均无增量。
  - `TDMA:FLIGHT:TX` 本次返回超时，RX 仍为空；因此 flight FIFO 镜像未通过，不能把本次运行
    记为飞行处理闭环。
  - `simultaneous_feedback_loop_evidence=0`、`ring_last_error=TIMESTAMP_MISSING`，符合
    当前没有 PIO/DMA 四边沿 latch 的预期；没有生成 `t1..t4`，没有生成 active delay。
  - OTA 后 COM8 最终运行 build `20260820172113`，活动槽 A 已提交；板卡地址仍为
    `839E1AE79EA20F31`，SCPI 错误队列为空。
  - resident TDMA IO 对照确认 TX=`GPIO26/25/29`、RX=`GPIO27/28/24`；同一短回环线下
    5 秒窗口 TX/RX 均前进 `1195` 帧且物理坏帧无增长，排除 IO 映射和接线错误。
  - 纯 PIO 回环先后修正 capture SM 覆盖 TX pin direction、返回输入未显式 mux 到 PIO2，
    并把 250 MHz 下过窄的 marker 扩展到可穿越 ISO1452 的有界窗口。
  - 最终 DMA 完成 `128` words，sample period 为 `20 ns`；snapshot 为
    `edge_mask=0xF`、`flags=0x7`、`reject_reason=NONE`、epoch `2`，四时间戳依次为
    `1060/1120/2080/2140 ns`，SYNC 匹配且 `result_valid=1`。
  - 固件端计算得到 residence `960 ns`、raw path-sum `120 ns`、reference delay estimate
    `60 ns`；`active_eligible=0`，未写入 VDC/DPLL active fact。上述数值仅是该 build、
    当前短线与收发器的诊断快照，不是 endpoint bias 或产品精度事实源。
  - 连续 10 个独立 epoch 全部满足四边沿、PIO/DMA、SYNC、公式和 reference-only 门禁，
    epoch 从 `3` 递增到 `12`，SCPI 错误队列为空；firmware-reported residence 范围为
    `960..980 ns`、raw path-sum 为 `100..120 ns`、delay estimate 为 `50..60 ns`。
  - 加入 core0 低频传感器 diagnostics 后，OTA build `20260820174134` 再完成 10 个独立
    epoch；四边沿、PIO/DMA、SYNC、公式和 reference-only 门禁仍为 10/10 通过，
    residence、raw path-sum 和 delay estimate 保持在前述诊断快照范围，证明 ADC service
    未进入 core1 实时边沿路径。证据仍为 `REFERENCE_LOOPBACK + DIAGNOSTIC_ONLY`。
  - 同一 build 上 `SYSTem:DIAGnostic:SENSors?` 连续读取确认板载温度与 RP2350 ADC8
    内部温度有效且无热告警；电流前端输出近低轨并伴随 U24 明显发热，已判为硬件前端
    故障并断电。电流值不得用于本任务的算法或 active calibration 结论。
  - 10 轮结束并 STOP 后重新进入 resident TDMA persona，5 秒只读窗口 TX/RX 各增长
    `1203` 帧，adapter/phys bad、magic fail、ring overrun 均无增长，证明 PIO2 SM/DMA
    维护 persona 能退出并恢复 TDMA owner 运行态。
- 验证命令与证据：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools/tests/run_calibration_bidirectional_tests.ps1`
    通过，输出 `calibration_bidirectional tests passed`。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成新的 A/B 固件和 update package。
  - `python tools/tdma_ring_monitor/tdma_single_board_loopback.py COM8 --duration-s 15
    --poll-interval-s 0.5 --out-dir build-rtos-multicore-smoke/tdma_single_loopback_current`
    完成物理回环预检；原始摘要见该目录 `summary.json`。
  - `python tools/calibration/calibration_loopback_validate/calibration_loopback_validate.py COM8
    --out-dir build-rtos-multicore-smoke/calibration_loopback_com8_final` 通过；raw SCPI 和
    snapshot 见该目录 `summary.json`。
  - `python tools/calibration/calibration_loopback_validate/calibration_loopback_validate.py COM8 --runs 10
    --out-dir build-rtos-multicore-smoke/calibration_loopback_com8_repeat10` 通过 10/10 轮；
    每轮 snapshot、epoch 和诊断范围见该目录 `summary.json`。
  - 传感器集成后的同 build 复验目录为
    `build-rtos-multicore-smoke/calibration_loopback_com8_sensors_adc8/summary.json`，通过
    10/10；随后因 U24 硬件过热主动断电，未把 TDMA persona 恢复工具的端口打开失败计为通过。
  - 校准 STOP 后重新 ARM/START resident TDMA，再执行
    `tdma_single_board_loopback.py --skip-ring-setup` 通过；persona 恢复证据见
    `build-rtos-multicore-smoke/tdma_restore_after_calibration/summary.json`。
- 还需完成：
  - 将当前 10 轮短窗口扩展为长时间重复性/温漂统计，再补 bias generation/reference
    profile；本次不清除 diagnostic-only。
  - 先完成 bias/reference loopback，再执行双板 P3 HIL；当前结果不能清除 diagnostic-only。
- 关联文件：
  - `components/calibration_manager/inc/calibration_bidirectional.h`
  - `components/calibration_manager/src/calibration_bidirectional.c`
  - `tests/unit/test_calibration_bidirectional.c`
  - `tools/tests/run_calibration_bidirectional_tests.ps1`
  - `tools/tdma_ring_monitor/tdma_single_board_loopback.py`
  - `tools/calibration/calibration_loopback_validate/calibration_loopback_validate.py`
  - `build-rtos-multicore-smoke/tdma_single_loopback_current/summary.json`
  - `build-rtos-multicore-smoke/calibration_loopback_com8_final/summary.json`
  - `build-rtos-multicore-smoke/calibration_loopback_com8_repeat10/summary.json`
  - `build-rtos-multicore-smoke/calibration_loopback_com8_sensors_adc8/summary.json`
  - `build-rtos-multicore-smoke/tdma_restore_after_calibration/summary.json`

## CAL-TASK-20260820-002 - 校准域待办与任务记录建立

- 状态：完成文档拆分；代码和第三阶段板端验证未完成。
- 日期：2026-08-20。
- 任务目标：
  - 依据 TDMA CLK 分级训练方案建立校准域可执行 TODO。
  - 把 P0/P1/P2/P3/P4 的 owner、交付物、门禁和阻塞项单独记录。
  - 为后续双向同时对比、endpoint bias、四板 residual 和八节点扩展保留证据入口。
- 完成内容：
  - 新增 `CALIBRATION_DOMAIN_TODO.md`，明确校准域与 TDMA、VDC/DPLL、SYNC_IO 的边界。
  - 新增本任务记录，区分已完成的方案/粗捕获与尚未实现的正式校准流程。
  - 将第一阶段结果标记为 build/topology/profile 绑定的 diagnostic snapshot。
  - 明确正式 active per-link delay 必须经过四时间戳 hardware latch、bias generation、
    重复统计、topology freshness 和 VDC/DPLL gate。
- 验证结果：
  - 文档回归命令待本次文档修改完成后统一执行。
- 关联文件：
  - `docs/calibration/CALIBRATION_DOMAIN_TODO.md`
  - `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`
  - `docs/calibration/README.md`
  - `docs/README.md`
- 下一步：
  - 先完成 P0 hardware latch/evidence 字段与 P2 marker golden vector，再进入双板 P3 HIL。

## CAL-TASK-20260820-001 - TDMA CLK 训练方案与双向测距补充

- 状态：完成方案更新；实现和板端验证待完成。
- 日期：2026-08-20。
- 任务目标：
  - 将 TDMA 训练物理测量从整圈平均分摊推进到逐链路双向时间传递。
  - 明确 `CLK/DATA/SYNC` 同 epoch 边沿关联、residence 扣除、path-sum 和 asymmetry gate。
  - 保持 EtherCAT DC 风格的训练状态、质量和接受门禁。
- 完成内容：
  - 方案加入 P3 `t1/t2/t3/t4` 定义和双向同时对比方程。
  - 明确等长差分线缆是对称性工程证据，不替代 endpoint bias/reference loopback。
  - 明确四板回环是逐链路结果的系统级 residual/HIL 门禁，不是单链路可观测性的替代品。
  - 明确只有正式 hardware latch、bias generation、重复统计和 topology freshness 通过后，
    才能生成 active per-link delay。
- 验证结果：
  - 本任务为方案和架构记录，未声称完成双板或四板 P3 实测。
- 关联文件：
  - `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`
  - `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`
- 下一步：
  - 实现 P0/P2 基础件；完成同 persona 板内 bias reference loopback 后再执行双板 HIL。

## CAL-TASK-20260820-000 - 第一阶段 CLK RTT 粗捕获基线

- 状态：完成诊断基线；不能用于正式 VDC/DPLL 校准。
- 日期：2026-08-20。
- 完成内容：
  - 板内完成 CLK forwarding、master burst/capture、PIO IRQ、service 和 guarded snapshot。
  - 四板 HIL 完成四主轮换和多个 SPI profile 的粗区间捕获。
  - 训练结果保留 overlap、mixed、超时、错误增量、master、唯一板卡地址、profile 和
    `DIAGNOSTIC_ONLY` 标志。
- 结果边界：
  - 结果只对当次 build、当前拓扑、接线、收发器和 profile 有效，详情以训练方案中的
    HIL evidence directory 为准。
  - 粗 RTT 包含线缆、收发器和 follower residence，不能直接当作单 link delay，也不能
    作为完整帧 feedback timeout。
  - 当前 latch/时间戳尚未满足正式 DPLL gate，不能清除 diagnostic-only。
- 关联实现：
  - `components/tdma/src/tdma_pio_spi_phys.c`
  - `components/tdma/inc/tdma_pio_spi_phys.h`
  - `components/tdma/src/tdma_runtime_owner.c`
  - `tools/calibration_ring_validate/calibration_clk_train.py`
  - `tools/calibration_ring_validate/calibration_clk_codebook_eval.py`
- 下一步：
  - 将粗 bracket 作为编码 marker 的有界搜索输入，禁止扩大其含义为 active calibration。

## 证据记录规则

后续每次代码或板端验证追加一条任务记录，至少包含：

- 任务编号、日期、状态、目标和实际完成内容；
- build ID、固件/工具版本、板卡唯一地址、local node、拓扑、线缆/收发器和 profile CRC；
- 训练 epoch/sequence、accepted/rejected、硬件 latch source/resolution/flags、DMA overrun/
  stall、margin、residence、path-sum、bias generation、calibration generation 和 freshness；
- 执行的 host unit、HIL、烧录、SCPI smoke 或长稳命令，以及证据目录；
- 失败原因、影响范围、回滚/恢复动作和下一步。

当前未完成项目不能通过“代码已经存在”“工具能查询”或“软件 timer 有数值”标记为完成。
