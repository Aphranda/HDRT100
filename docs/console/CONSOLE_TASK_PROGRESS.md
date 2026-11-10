# 运维与调试控制面（CONSOLE）任务进展

Status: Active
Domain: CONSOLE
Canonical: `docs/console/CONSOLE_TASK_PROGRESS.md`
Related: `docs/console/CONSOLE_ARCHITECTURE.md`, `docs/console/CONSOLE_TK_ARCHITECTURE.md`, `docs/console/CONSOLE_DOMAIN_TODO.md`, `docs/README.md`
Last updated: 2026-11-09

## 文档接口

本文记录 CONSOLE 域的真实执行、证据和失败；稳定语义见 [Architecture](CONSOLE_ARCHITECTURE.md)，任务状态见 [TODO](CONSOLE_DOMAIN_TODO.md)。条目标签使用 `CONSOLE-PROGRESS-YYYYMMDD-NNN`，最新在最上面。

## 当前 checkpoint

### CONSOLE-PROGRESS-20261109-022：demo 读面迁 OBS、开关旧写入口下架

- **写面**：按固件行为（`CONFigure:SWITch#` → `-200,"USE_CTRL_EXECUTE"`）在
  `catalog.Function.app_deprecated` 标记开关函数；界面不再提供“应用层 SCPI”单选，
  `build_app()` 对废弃入口 fail-closed。其余无 REFMEM 类型的应用层写保留。
- **读面**：`obs_read.py` 增加 key3/4/5/6/8 的结构化读取（`ControlPlane.read` 逐槽行），
  导出 `read_ring_status/read_boot_plan/read_guide_plan/read_trigger_state/read_node_health`
  与页面混入 `ObsReaderMixin.read_obs`；REFTMEM 读不再由页面直接拼 SCPI。
- **校准 workflow**：`precheck` 与报告 `ring_after` 改读 OBS key6（本地行
  `valid/enabled/up/down/adapter`），不再用 `RING:STATus?` 的 12 字段；
  `GUIDE:PLAN?` 的 27 字段判据保留为诊断例外。
- **校准页**：汇总的启环状态改 OBS key5（`action/fail_step/boot_done/half_ring/profile_source`），
  保留“详细 BOOT:PLAN?（本地 SCPI 例外）”按钮；环状态按钮改 OBS key6。
- **总览**：板卡在线改 OBS key8（worker + UiPump 异步刷新），卡片标注读源。
- **模拟器**：按 REFMEM-READ-01 行格式（`slot,valid,epoch,seq,age_ms,stale,<schema>`）
  应答 `OBS?`，`--demo` 全流程可演示。
- **验证**：`test_trigger_debug_demo/ui/obs` 全绿（含新增 OBS 解析、模拟器 OBS、开关废弃断言）；
  `py_compile` 全通过。证据路径 `out/refmem_read/` 的 HIL 报告仍属 REFMEM 域。
- **边界**：未改固件；`READ:*`、`GUIDE:PLAN?`/`BOOT:PLAN?` 全字段没有 OBS key，
  登记 `CONSOLE-046`，不作为跨板判据。

### CONSOLE-PROGRESS-20261109-021：单口引导全阶梯按钮与 P2 复环失败

- **页面**：汇总页新增 `运行全阶梯 P1→T3`；动作页统一 `action_inputs`，P2/P3 使用 level 7 + §4.6 规范载荷，T0–T3 按阶梯参数；运行前要求 `ladder_index=0`。
- **参考脚本**：`guide_cmdplane_probe.py ladder 1 6020 6120` 四板 P1 PASS；`p2 6021 6121` 执行 1 个 trial 后 `status=2/reason=14`（STOPPED + NOT_ONLINE_SAFE），不是 restore 的 `status=3/abort_site=26|27`。
- **修正**：P2 `status=2/reason=14` 的代码路径是 `APP_CTRQS_P2_CODED_STOP` 中 `calibration_manager_restore_training_stage()` 失败 → `hard_fail_reason=NOT_ONLINE_SAFE`，不是 Q/复环。已试 P1 level7（10k 环四板 GREEN）后接 P2，仍复现。
- **页面**：动作页 level 已改为可编辑，P1 可填 7；全阶梯按钮仍需先复位 ladder。
- **日志**：`logs/2026-11-08/10k-P1-235555.log` 示例；P2 参考脚本无归档日志。
- **验证**：host 29 passed；页面按钮/阶梯参数 smoke。

### CONSOLE-PROGRESS-20261108-020：校准运行日志按日期分目录

- **格式**：`out/calibration/guide-10k/logs/YYYY-MM-DD/<label>-<HHMMSS>.log`；动作页 label 为 `10k-P1` 等。
- **内容**：每步 CONFig/TRIGger、gen/epoch、Q 进度、报告/failure 路径；失败也落盘。
- **报告**：PASS/FAIL JSON 增加 `log_path`。
- **验证**：`2026-11-08/10k-235303.log` 已生成；P1 在该轮 `SUCCEEDED`。
- **边界**：日志只记录主机流程，不含固件内部 trace。

### CONSOLE-PROGRESS-20261108-019：P2/P3 独立页试跑，ladder 未推进

- **P2**：独立页 gen 6006/epoch 6106，CONFig/TRIGger `OK`；Q `status=1/reconcile=1/trials_done=4/passed=0xF/failed=0`，但 `ladder_index=1/ladder_step=0`，demo strict 以 `trials=1` 判 FAIL。
- **P3**：gen 6007/epoch 6107；Q `status=3/reason=13`、`trials_done=0`；未进入完成态。
- **更正**：`app.c:1722` 是旧注释；P2–T3 trial body 已存在（P2 校验 2334-2354、P3 2355-2386、T0–T3 2387-2482）。之前 demo 的 P2/P3 失败是**载荷形状/阶梯顺序**问题，不是“未实现”；动作页已按 §4.6 规范载荷修正并恢复运行。
- **证据**：`out/calibration/guide-10k/action-P2/.../20261108-234522-359551-failed.json`、`out/calibration/guide-10k/action-P3/.../20261108-234729-7b661c-failed.json`。
- **台架**：ring 仍 up，build `20261108142853`。

### CONSOLE-PROGRESS-20261108-018：校准页拆为汇总 + P0–T3 子 tab

- **结构**：`CalibrationPage` 变为二级 Notebook：汇总 + 线序校准、时钟标定、编码标定、时延标定、TRN-00 marker、TRN-01 SCK、TRN-02 DATA、TRN-03 staging。
- **独立页**：各页自带 action/generation/epoch/window/trials/candidates/thresholds、读 PLAN/BOOT；当前固件只接 P1，P1 运行可用；P2–T3 保留页面/参数/只读入口，运行按钮禁用（避免把 fail-closed 当可用）。
- **实板验证**：P1 独立页在 build `20261108142853` 用 gen 6004/epoch 6104 运行，`SUCCEEDED`，`status=1/reconcile=1/trials=4/passed=0xF/failed=0`；报告 `out/calibration/guide-10k/action-P1/calibration/guide-10k/20261108-233903-14fb2d.json`。
- **边界**：P2–T3 的 C 段默认值按 §4.6 规范；P2 实跑仍在 Q 段失败，见 021。
- **验证**：host 29 passed；9 个子 tab 实板截图。

### CONSOLE-PROGRESS-20261108-017：全页面实板 UI 验证

- **台架**：COM5/NO.1，build `20261108142853`，10 kHz 环在线。
- **范围**：14 个 tab 全部构建/切换/截图；安全读回：总览 refresh、状态回读查询、ACK 聚合、控制面 catalog、校准 `BOOT:PLAN?`、OTA progress、原始 `*IDN?`。
- **结果**：无 Tk 异常、无页面崩溃；实板返回 `*IDN?=NO.1,DHRT100,0010071E65B5CB38`；校准页 `BOOT:PLAN?` 为 `action=1, steps=8, fail=0, done=1, source=2(默认档)`，显示 READY。
- **修正**：校准页默认档改为 10 kHz；新增 BOOT 状态面板；验证脚本强制串口、按队列空闲逐页等待。
- **未做**：OTA 升级、TCP 服务启动、控制面/功能页写命令；仅验证构建与只读入口。
- **证据**：`out/calibration/guide-10k/ui-verify/`（14 张 PNG + report.json）。

### CONSOLE-PROGRESS-20261108-016：校准页增加单口启环状态

- **页面**：`CalibrationPage` 新增 `BOOT:PLAN?` 状态面板，显示 action/steps/fail_step/boot_done/next_step/half_ring/have_profile 与 `profile_source`（1=持久档、2=默认档、0=无档）。
- **判定**：`boot_done=1 + fail_step=0 + half_ring=0 + action∈{1,2}` 显示 `READY`，否则 `NOT READY`。
- **按钮**：`读 BOOT:PLAN?`、`读 RING:STATus?`；与现有单口 C/T 流程共用 COM5。
- **边界**：仅读回/展示；不自动复位、不自动 provisioning、不启 10M 环。
- **验证**：UI smoke 覆盖 20 字段 BOOT 解析与“默认档/READY”；host 29 passed。

### CONSOLE-PROGRESS-20261108-015：10 kHz 双 PASS；10 MHz ARM 仍阻塞

- **10 kHz PASS ×2**：gen 6001/epoch 6101、gen 6002/epoch 6102；均 `status=1/reconcile=1/trials=4/passed=0xF/failed=0/abort=0`。
- **报告自包含**：`calibration_workflow.py` 的 PASS/FAIL 报告新增 `idn/build`；`20261108-164145-654461.json` 含 build `20261108055416`。
- **10M**：`tdma_start_ring --level 7 --skip-train --full` 在 ARM 后 `adapter_started=0` 超时，未进入 C/T。
- **当前台架**：10 kHz 环 up。
- **判定**：10k 收束完成；M4/M5 未完成，目标 blocked 于 10M ARM。

### CONSOLE-PROGRESS-20261108-014：新 build 10 kHz PASS，10M 仍失败

- **新 build**：四板 `20261108055416`；10 kHz 零口自组环 `enabled/up/down/adapter=1`。
- **10 kHz demo**：gen 6001 / epoch 6101；CONFig/TRIGger `OK`；Q `progress_status=1(COMPLETE)`、`reconcile=1`、`trials_done=4`、`passed_mask=0xF`、`failed_mask=0`、`abort_site=0`；Workflow `SUCCEEDED`。
- **10M 尝试**：`tdma_start_ring --level 7 --skip-train --full`；STOP/STAGE/APPLY/TOPO/ARM/START 返回 OK，但 step8 ARM timeout，`adapter_started=0`；未进入 demo C/T。
- **恢复**：四板 BOOT:RESet 回 10 kHz 自组环。
- **证据**：`out/calibration/guide-10k/bench/calibration/guide-10k/20261108-163824-447c21.json`、`out/calibration/guide-10k/bench/10m-skiptrain-20261108/summary.json`。

### CONSOLE-PROGRESS-20261108-013：目标转入外部阻塞

- **已收口**：10 kHz 基线、demo C/T 单口链路、strict verdict、失败报告、四板只读基线。
- **阻塞 1**：10 kHz Q 段 `status=3/reason=14`，`app.c` RESTORE_WAIT/REBUILD 路径需固件 owner 修复（`CONSOLE-042`）；主机侧不改固件。
- **阻塞 2**：10 MHz 建环 TRAIN 前置 `-200`、`adapter_started=0` 不可复现；且校准基线缺失（`CONSOLE-043/044`），需 10M setup/校准/P3 owner。
- **边界**：未改固件/接线；台架当前为 10 kHz 自组环。
- **判定**：目标不标完成；等待外部 owner。证据见 005/009/010/012。
- **后续**：014 在新 build `20261108055416` 上 10 kHz demo 已 PASS，Q 阻塞解除；10M ARM/TRAIN 仍 OPEN。

### CONSOLE-PROGRESS-20261108-012：10M adapter 窗口不存在，停止盲试

- **探针**：ARM 后 50 ms 间隔采样四板 `RING:STATus?` 5 s；四板始终 `adapter_started=0`、`cfgseq=6/applied=5`。
- **结论**：没有可用的“ARM 后 adapter 窗口”让 TRAIN 插入；此前一次 `adapter=1` 观察不可复现，不能再作为 10M 建环依据。
- **处置**：停止 10M 盲试；四板 BOOT:RESet 恢复 10 kHz 自组环（当前 `enabled/up/down/adapter=1`）。
- **证据**：本次会话控制台输出；探针在失败分支提前退出，未生成 JSON，不能作为独立产物。可复跑的 10M 产物仍只有 010 的 `10m-order-trace-20261108.json`。

### CONSOLE-PROGRESS-20261108-011：10M APPLY settle 反证；adapter 不稳定

- **尝试**：STOP→STAGE/APPLY→TOPO 后等待 `cfgseq==applied`，再 ARM，等 `adapter_started=1` 后 TRAIN。
- **结果**：COM5 的 `OPMode:APPLy` 返回 `-200,"TDMA_OPMODE_APPLY_STOP_REQUIRED"`（STOP 异步未完成）；ARM 后四板 10 s 内 `adapter_started=0`，无法进入 TRAIN。
- **结论**：单纯等 `cfgseq==applied` 不足；本次观测到 ARM 后 `adapter_started` 不能稳定保持 1，TRAIN 的前置窗口尚未定清。`CONSOLE-043` 仍 IN PROGRESS。
- **恢复**：四板 BOOT:RESet 回 10 kHz 自组环；当前 `enabled/up/down/adapter=1`。
- **证据**：本次会话控制台输出；探针在失败分支提前退出，未生成 JSON，不能作为独立产物。

### CONSOLE-PROGRESS-20261108-010：10M ARM/TRAIN 顺序 trace 反证裸 ARM

- **trace**：`bench_10m_order_trace.py` 记录 `STOP → STAGE/APPLY → TOPO → ARM → TRAIN → START` 全时间戳。
- **结果**：ARM 后四板 `enabled=1, adapter_started=0, cfgseq=6/applied=5`；随后 `TRAIN 4096` 与 `START` 全部 `-200`。
- **结论**：**裸 ARM 后 TRAIN 不可行**；之前 step6 后短暂看到 `adapter=1` 的机制尚未由 trace 证明，不能作为“ARM→TRAIN”顺序证据。`CONSOLE-043` 保持 IN PROGRESS。
- **恢复**：四板 BOOT:RESet 恢复 10 kHz 自组环（当前 `enabled/up/down/adapter=1`）。
- **证据**：`out/calibration/guide-10k/bench/10m-order-trace-20261108.json`。

### CONSOLE-PROGRESS-20261108-009：M4 10 MHz 建环成功，但 P1 C/T 失败

- **建环**：10 MHz 曾按 `STOP/STAGE/APPLY/TOPOLOGY → ARM → TRAIN → START` 建起四板环（`10m-start` summary 证明 up/down=1、node_count=4、rx_bad=0）；但 010 的 trace 反证“裸 ARM 后 TRAIN 可行”，顺序证据不成立；`10m-start` 的 `sequence` 仍是静态 TRAIN→ARM 文案。
- **demo 10 MHz C/T**：COM5 单口 `GUIDE:CONFig level 7` + `TRIGger` 均 `OK`；Q 回读 `status=3(FAILED), reason=14(NOT_ONLINE_SAFE), trials_done=4, passed_mask=0x0A, failed_mask=0x05, commit=2`；level 7 由该失败 JSON 的 config/plan 证明。
- **失败归因边界**：本次只做了 `OPMode:STAGE/APPLY level 7`，未恢复校准 stage/基线；整片擦除后校准数据被清，10 MHz P1 的 4-cycle trial 失败与此一致。M4 不能用当前台架状态判定为 demo 回归失败，也不能宣称 10 MHz 通过。
- **结论**：10 MHz demo 链路可执行，但本轮 P1 本身有失败 trial，且 Q 仍 `NOT_ONLINE_SAFE`；**不能宣称 10 MHz 回归通过**。
- **恢复**：四板 `SYST:BOOT:RESet` 恢复 10 kHz 自组环；当前 `enabled/up/down/adapter=1`。
- **证据**：`out/calibration/guide-10k/bench/10m-start-20261108/summary.json`、`out/calibration/guide-10k/bench/calibration/guide-10m/20261108-105627-461d6e-failed.json`。

### CONSOLE-PROGRESS-20261108-008：M4 10 MHz 建环在 TRAIN 前置失败

- **尝试**：`tdma_start_ring.py --level 7 --full`（四板 UID，build `20261107175157`，未用 `--bringup`）。
- **成功**：discover、STOP all、STAGE/APPLY level 7、TOPOLOGY 均 OK。
- **失败**：clock training `TRAIN 4096` 在 NO.2 返回 `-200,"TDMA_RING_TRAIN"`，0/4096 cycles；未进入 ARM/START。
- **恢复**：四板 `SYST:BOOT:RESet` 后 boot profile 自组回 10 kHz；`enabled/up/down/adapter=1`，build 一致。
- **证据**：`out/calibration/guide-10k/bench/10m-ring-20261108/summary.json`。
- **边界**：未改固件/接线；10 MHz TRAIN 前置未解决，M4 不能宣称完成。
- **后续**：009/010 已反证“ARM→TRAIN 实际可行”；`10m-start` summary 的静态 sequence 仍是 TRAIN→ARM，043 不能标 DONE；010 的 trace 也只证明裸 ARM 后 TRAIN 失败。

### CONSOLE-PROGRESS-20261108-007：M3 四板只读基线（registered tool）

- **恢复**：四板 `SYST:BOOT:RESet` 后 boot profile 自组 10 kHz；`tools/calibration/guide_cmdplane_probe.py status` 只读确认 NO.1–4 全部 `enabled=1/up=1/down=1/adapter=1`，build 一致 `20261107175157`，PLAN 全零。
- **证据**：`out/calibration/guide-10k/bench/m3-four-board-status-20261108.txt`。
- **结论**：四板只读基线已可复跑；demo 默认仍只读协调板，M3 的“demo 内聚合”未做。
- **交叉复核（只读子代理）**：4 项 PASS；附注：状态文件为 UTF-16、无工具命令 provenance，NO.1 `trip_ns=400` 而其余为 0，语义未确认，不影响四板 ring 就绪判定。
- **验证**：host 29 passed；未发 GUIDE 命令。

### CONSOLE-PROGRESS-20261108-006：M3 主机 strict verdict 收口

- **判定对齐**：`calibration_workflow.py` 的 `PLAN_FIELDS` 增加 `abort_site`；`_report` 现在要求 `valid=1/progress_valid=1/progress_status=1/reconcile=1/trials_done=trials/passed_mask=(1<<trials)-1/failed_mask=0/abort_site=0`，不再只凭 status 判定。
- **失败分类**：新增 `failure_class`：`Q_RING_RESTORE`（status=3/reason=14）、`Q_FAILED`、`Q_NOT_REACHED`、`Q_NOT_RECONCILED`、`TRIAL_FAILED`、`ABORT_SITE`；失败报告带 `ring_after`。
- **页面**：`calibration_page` 的 PLAN 字段增加 `abort_site` 显示。
- **交叉复核（只读子代理，二次）**：M1 PASS（弱：m1 JSON 是摘要，非逐口原始响应）；M2 若把 gen 962 demo 与 gen 963 四板时序合并叙述则 **FAIL**，两者已拆开；M2 gen/final/ring 在各自 run 内成立，trigger 先后为运行日志推断；app.c 根因 **PASS（机制措辞已修正）**；strict gate PASS；`abort_site` 为固件第 25 字段（`scpi_tdma_snapshot_commands.c:845`），缺字段 fail-closed。新 `ring_after/failure_class` 尚未由 out/ 旧报告演示，等下一次台架 run 取证。
- **验证**：host 29 passed；无台架新命令。
- **边界**：四板一致性仍由外部只读工具/维护口确认；demo 默认仍只读协调板。

### CONSOLE-PROGRESS-20261108-005：M1 环恢复 + M2 四板时序取证，Q 段根因归固件

- **M1**：对四板执行 `SYSTem:BOOT:RESet`（build `20261107175157`，COM5/4/3/6），boot profile 自组 10 kHz 环；基线 `enabled/up/down/adapter=1`，`level=23`、`baud=10000`、`GUIDE:PLAN?` 清零。证据 `out/calibration/guide-10k/bench/m1-baseline-20261108-103157.json`。
- **M2 复跑（demo，独立一次）**：`GuideWorkflow(level=23)` 用 gen 962/epoch 1002，C/T 均 `OK`；Q 为 `status=3(FAILED), reason=14(NOT_ONLINE_SAFE), trials_done=4, passed_mask=15, failed_mask=0`，ring 落 0。证据 `out/calibration/guide-10k/bench/calibration/guide-10k/20261108-103210-7fe281-failed.json`。
- **四板时序（另一次 gen 963，不与上面混为同一 run）**：NO.2–4 到 gen 50.765/50.797/50.844 s，NO.1 60.0 s；trigger 在四板 gen 全到后发出；epoch 四板 0.079–0.219 s；四板最终 PLAN 均 status=3/reason=14。该证据支持“不是主机提前 trigger”，但 trigger 时刻未写入 JSON，属运行日志推断。
- **证据质量**：`m2-timing-20261108-103502.json` 是本次仪器化运行的手写摘要，producer 在 `out/` 未入库；四板最终 PLAN 原始值来自本次会话输出，不能仅凭该 JSON 独立复跑。
- **根因（修正）**：`app_guide_window_close()` 会重置 boot 标志并重新开窗；`app_diag_service()` 每 tick 调 `app_guide_boot_service()`，replay 会被重试。`APP_CTRQS_RESTORE_WAIT` 的 guard `s_boot_guide_done && s_boot_fail_step==0` 表示“replay 已成功”，不是“等待 replay”。真正缺陷是：boot plan 存在时 `!app_ctrqs_boot_plan_present()` 不成立，REBUILD 分支不可达；replay 失败或恢复后的 ring 身份/up/down 不匹配时，只能盲等 45 s 后置 `NOT_ONLINE_SAFE`。建议修复：处理 `s_boot_fail_step != 0`、boot-plan 不匹配时回退 REBUILD、发布可区分的 abort_site/reason。
- **边界**：未改固件；Q 段修复需固件 owner 授权。

### CONSOLE-PROGRESS-20261108-004：台架验证 demo 10 kHz 单口 C/T，Q 段失败

- **台架**：COM5 = NO.1，build `20261107175157`；起始 10 kHz ring `enabled/up/down/adapter=1`，`PLAN?` level 23、gen 960。
- **demo 执行**：`SYST:TDMA:GUIDE:CONFig 1,305398721,961,23,4000,4,3,0,4,16,1,64` → `OK`；59 s 后 gen=961；`GUIDE:TRIGger 1,1001` → `OK`；epoch=1001。
- **结果**：`PLAN?` = `valid=1, progress_valid=1, progress_status=3(FAILED), trials_done=4, deepest=3, reason=14(NOT_ONLINE_SAFE), reconcile=3, passed_mask=15, failed_mask=0`；随后 `RING:STATus?` 为 `enabled=0/up=0/down=0/adapter=0`。
- **证据**：`out/calibration/guide-10k/bench/calibration/guide-10k/20261108-094828-3fd2cd-failed.json`。
- **判定**：demo 的单口命令/等待/报告链正确；Q 段 ring restore 失败，属固件/环恢复问题，不是主机假失败。未继续发命令，未尝试自行恢复台架。

### CONSOLE-PROGRESS-20261107-003：10 kHz 启环确认后，单口流程进入可用态

- **输入**：用户确认 10 kHz 启环已可用（`RING:BRINGUP 1` 路径）；本会话未动台架、未复验。
- **影响**：`GuideWorkflow(level=23,label="10k")` 的 precheck 不再被 ring 缺失阻塞；10 kHz C/T/PLAN 单口流程可按页面直接运行。
- **边界**：流程仍只读协调板 `PLAN?`；四板 gen/epoch/progress 一致性与参考板 P1 TIMEOUT 不在主机侧解决，转为 `CONSOLE-041`。
- **证据**：主机流程测试 28 passed；无台架读数/凭证。

### CONSOLE-PROGRESS-20261107-002：10 kHz 单口流程复用 10 MHz 步骤

- **结果**：`GuideWorkflow` 参数化 level；10 MHz=7、10 kHz=23；步骤仍为 precheck→CONFig→等 gen→TRIGger→等 epoch→等 COMPLETE→报告；10 kHz 默认 gen 120 s、epoch 180 s、watch 360 s；CONFig 对 `-200/NOT_ONLINE` 做 6 次有界重试（2 s 间隔）；页面默认 `CRC=0x12340000+generation`，多轮 gen 不再复用固定 CRC。
- **单口边界**：precheck 等待协调板 RING 30 s；10 kHz 的 `RING:BRINGUP 1`/四板 gen/epoch 一致性仍不在本流程内（`CONSOLE-040`）；不打开第二端口。
- **验证**：`GuideWorkflow(level=23)` 假页面用例生成 `...,1,23,...` 并产出 `out/calibration/guide-10k/<run>.json`；UI smoke 校验 gen=1/2 的 CRC 自动派生；demo+UI 28 passed。
- **未做**：台架 HIL；10 kHz 参考板 TIMEOUT 的固件根因不在主机侧修复。

### CONSOLE-PROGRESS-20261107-001：单口校准引导增加 10 kHz 档位

- **结果**：新增 `calibration_page.py` 与“校准引导”tab；同一 C/T/PLAN 流程提供 10 MHz（level 7）和 10 kHz（level 23）预设；下发 `SYST:TDMA:GUIDE:CONFig` / `TRIGger`，读回 `PLAN?`、`SCAN?`、`PAIR?`、`RING:STATus?`。
- **边界**：只做主机侧命令构造/读回，不访问台架；10 kHz 的 `RING:BRINGUP 1`/四板 gen/epoch 一致性仍需外部启环与读回，页面明确提示。
- **验证**：UI smoke 断言 10 kHz 命令为 `...,23,...`、10 MHz 为 `...,7,...`；demo+UI 27 passed；tab 数更新为 14。
- **文档**：demo README、tool registry 已加 `calibration_page.py`。

### CONSOLE-PROGRESS-20261104-029：HIL 物理链路最终判定（六对板零连通 + 单板回环无 RX）

- 追加“直连镜像”映射：NO.1 master `rx19,csn17,sck18,tx16` / NO.4 slave `rx16,csn17,sck18,tx19`，`BOOTstrap:STATus?` physical rx 仍 **0**。
- 单板回环：NO.1 `rx20,csn17,sck18,tx16`（假设 16→20 跳线），physical rx 仍 **0**。
- 六对板 `two_board_io_validate` 全部 FAIL、observed 映射为空；`SYST:REFMEM:SYNC:TDMA:STATus?` 四板均回 `1,1,...` 但无对端 ready 证据。
- 结论：当前台架没有可用的两板物理链路（IO 组与回环都不通），HIL 无法执行；代码侧（carrier/命令/reply/超时、RX DMA 888B）已完成并有 host/链接证据。

### CONSOLE-PROGRESS-20261104-028：RX DMA 单字节 lane 收缩（-888B，ON/OFF 双镜像）

- `s_refmem_spi_rx_dma_words` 由 `uint32_t[296]`（1184B）改为 `uint8_t[296]`（296B），两处 DMA 配置（`capture_words`、`receive_begin`）统一 `DMA_SIZE_8`，三处读取去掉 `& 0xFFu`。
- 收益：ON `.bss` 0x68a94→**0x6871c**，余量 364B→**1252B**；OFF `.bss` 0x68998→**0x68620**，余量 616B→**1504B**（与预测一致，各 −888B）。
- 新增 `tests/python/test_refmem_spi_rx_dma_width.py`（2 passed）：锁死 `uint8_t` 声明、恰好两处 `DMA_SIZE_8`、无 `DMA_SIZE_32/16`、DMA 读取行无掩码、4+36+256=296。ON/OFF 四板均链接 exit 0；refmem host 全通过。
- **未验证的硬件前提**：`DMA_SIZE_8` 读 PIO `rxf` 是否按“一次一个条目、取 bits[7:0]”工作，需两板 `SYNC:RAW:RX? 256,165` 要求 `mismatch_count=0`；当前台架 IO 组零连通，无法执行。

### CONSOLE-PROGRESS-20261104-027：HIL 物理链路判定——两板 IO 预检零连通

- 用仓库自带 `tools/two_board_io_validate/two_board_io_validate.py` 分别测 **NO.1↔NO.4** 与 **NO.1↔NO.2**：两边 IO profile 都是 `IN GPIO20..23 / OUT GPIO16..19`，但所有输出位在对面输入 mask 均为 `0x0`，observed 映射为空，结论 **FAIL（无任何连线）**。
- 随后用 IO profile 推导的引脚（master `rx20,csn17,sck18,tx16`；slave `rx20,csn21,sck22,tx16`）ARM，`BOOTstrap:STATus?` 仍 physical rx=0；此前 16..19 与 TDMA 24..29 映射同样 rx=0。
- 结论：**当前台架不存在两板 IO 组直连**；若“线序未变”指的是 TDMA/RJ45 链路，则低速 SPI 需要按该链路的电气方向重新映射（现有 SPI adapter 的 master/slave 方向与 BiSS 单链路方向不一致），不能直接套用 16..19。
- 台架已刷：NO.1/NO.2/NO.4 均为 `20261104055417`（ON 镜像），HIL 只差物理链路确认。

### CONSOLE-PROGRESS-20261104-026：bootstrap carrier 注册与 reply/超时闭环；台架 NO.1/NO.2 已刷 ON 镜像

- **代码**：`distributed_refmem_link.c` 新增 bootstrap binding——命令 envelope（kind/type/generation/target_mask）、`distributed_refmem_cmd_frame_receive`、reply envelope（source/seq16/flags/reason）→ `cmd_reply_store`、`cmd_reply_tx_get`→relay 回送、reply 超时→`node_control_update_status(TIMED_OUT/TIMEOUT)` fail-closed；`bootstrap_start` 注册 carrier，stop/handover 注销。ON 双板链接 exit 0，`.bss` 0x68a94，余量 **364B**；refmem host 全通过。
- **台架**：用 `tools/picotool_flash/picotool_flash.py` 按序列号把 ON factory UF2 刷入 **NO.1/NO.2**，两板 `SYST:FW:BUILD? = "20261104055417"`，`BOOTstrap:STATus?` 可查；ARM 返回 `"OK",1,...` / `"OK",2,...`（pin 16..19 与 TDMA 24/26/25/29、24/27/28/29 都试过）。
- **HIL 未通**：master relay/physical tx 增长、physical rx=**0**；slave physical rx=**0**、每 tick timeout。即两板间没有收到任何字节，carrier `ready` 始终为 0，命令准入不会放行。下一步用 `SYNC:SPI:RAW:TX/RX?` 或逻辑分析定位交叉线序/CSN（raw 与 relay 共用 adapter，需先解决互斥）。
- **未做**：分片命令（fragment）不在此 carrier 承载；HIL 未通过前不把“承载”写成完成。

### CONSOLE-PROGRESS-20261104-025：独立冗余审查结论与 HIL 线序纠正

- **独立审查**（只读子代理，map 13:39 快照）：全仓**没有**可安全合并的两个 live 实例；SCPI 诊断 context 与常驻 context 不能合并（`refmem_sync_init` 会 `memset` 掉 `command_handler`，且 `SYNC:RX` 注入口会升级为真实命令通道），只能开关化。可回收 top：`s_refmem_spi_rx_dma_words` 4× 容量（888B，需 HIL 改 DMA 宽度）、SCPI 桥开关化（2252B，产品面决策）、校准阶段 union（最多 ~7176B，owner 仲裁不对称，暂不合并）。三份 8KB 表镜像/`s_capture_payload_storage`（已是 union）/三个 `s_action` 均**不可合并**。CRC 去重只省 flash，不增主 RAM。
- **线序纠正**：两板最小系统沿用既有 TDMA/GPIO16..19 线序，**无需改线**；此前“GPIO16-19 未接线”的阻塞判断作废。HIL 唯一剩余动作是刷 `PROJECT_REFMEM_BOOTSTRAP_ENABLED=ON` 镜像并执行 ARM/STATus。
- **台架读数**：COM5=`NO.1`、COM6=`NO.4`，仍是旧 build；`SYSTem:REFMEM:BOOTstrap:STATus?` 无响应，确认需刷 ON 镜像。

### CONSOLE-PROGRESS-20261104-024：低速 SPI 承载 REFMEM——relay 出站/入站与 reply 窗口

- **结果**：relay 增加 `refmem_bootstrap_relay_submit()`（同步 TX，无需新缓冲）、`WAIT_REPLY` 状态、`reply_timeout_ms`、`frame_handler` 入站回调；initiator 提交后切 SLAVE 等 reply，responder 从 LISTEN 直接回帧；坏帧/非 probe 帧交给 `frame_handler`（供 REFMEM binding 解析命令/回执）。
- **验证**：`run_refmem_bootstrap_relay_tests.ps1` 通过（未 ready 拒绝提交、ready 提交、reply handler 收到帧、reply 超时 reason、responder 提交）；ON 镜像 `DHRT100`/`DHRT100_B` 链接 exit 0，余量 **372B**。
- **未做**：固件 binding（envelope、`distributed_refmem_cmd_frame_receive`、reply 回送、超时 NACK）与 carrier 注册；未 HIL。
- **下一 gate**：binding + carrier 注册（带 timeout fail-closed），再做 ON 链接/文档。

### CONSOLE-PROGRESS-20261104-023：定向释放与 CRC 合并（ON 镜像恢复链接）

- **释放**：移除 SCPI `s_refmem_package_chunk[512]`；LOAD:SD 改为 `storage_manager_copy_file_read_job_result()` 直接写入目标 buffer，不再经 512B 静态中转。分块读取循环保留，行为等价；省 **512B 主 RAM**。
- **合并**：`tdma_origin_calibration_crc.c` 删除自带 16-entry residue 表与 kernel，改为调用 `tdma_transport_crc32_compute()`（同为 reflected CRC-32 / ISO-HDLC，transport kernel 仍是 SRAM 常驻）。省 64B `scratch_y` 与一套重复 kernel；数值等价由 `test_tdma_origin_calibration_crc.py`（3 passed）与 `run_tdma_transport_frame_tests.ps1` 证明。
- **构建**：`PROJECT_REFMEM_BOOTSTRAP_ENABLED=ON` + `pico2-v2-factory-candidate` → `DHRT100.elf` / `DHRT100_B.elf` 链接 exit 0；`.bss` 0x68a84，heap 结束 0x2007fe84，**余量 380B**。
- **未计入证据**：`test_tdma_origin_admission.py` 在编译期因既有 `ota_ao_tdma_maintenance_allowed` 隐式声明失败（与本合并无关），未用于本次证明；HIL/FULL 仍因两板 GPIO16-19 线序缺失未执行。
- **残余**：默认编译开关仍 OFF（功能未 HIL 验证）；SCPI SYNC 诊断桥（2252B）与 action probe（1432B）两个重复控制面未合并；RX DMA 1184B 的 4× 单位疑点未修。

### CONSOLE-PROGRESS-20261104-022：RAM 预算回退方案 B（编译开关默认 OFF）与镜像恢复

- **决策**：RAM 预算三选一未获 owner 决策，按上轮声明执行 **B**：新增 `PROJECT_REFMEM_BOOTSTRAP_ENABLED`（默认 OFF）。OFF 时三个 bootstrap 源文件不参与镜像、SCPI/核心接线编译为 fail-closed stub；ON 时恢复完整接线。
- **构建证据**：`pico2-v2-factory-candidate` + OFF → **DHRT100/DHRT100_B 链接成功**，产出 `DHRT100.elf`、`DHRT100_B.elf`、`DHRT100_V2_CANDIDATE_FACTORY.uf2`，`flash_link_contract=OK`，重复构建 exit 0。ON → 双板 6 个 bootstrap 对象编译通过，但 `DHRT100.elf` 仍 `overflowed by 132 bytes`——确认瓶颈是 RAM 预算，不是编译/逻辑。
- **验证**：`run_refmem_all_host_tests.ps1` 全 PASS；docs 250/0、38 doc tests。
- **残余**：ON 镜像仍差 132B（heap/.bss）；HIL 需刷机 + 两板 GPIO16-19 线序；carrier 未注册；无 FULL 凭证；未提交。

### CONSOLE-PROGRESS-20261104-021：M4 台架勘察与 RAM 预算阻塞

- **台架**：COM3=NO.3、COM4=NO.2、COM5=NO.1、COM6=NO.4，全部运行 build `20261103195548`；`SYSTem:REFMEM:SYNC:SPI:STATus?` 返回 `-113 Undefined header` → 运行固件不含本轮 bootstrap SCPI；当前是产品四板机架，P4.5 两板 GPIO16-19 SPI 线序未建立。故 HIL 前置 = 刷入新固件 + 恢复两板 SPI 线序，二者都不是软件可自证的。
- **构建**：对象编译双板通过；**整链失败**：`pico2-v2-factory-candidate` 溢出 **132 bytes**，`pico2-release`（v1_compat）溢出 4064 bytes。实测 v2 基线（03:55 成功构建）`.bss` 后仅剩 0 字节余量；本轮新对象 `.bss` = owner 149 + relay 88，SCPI 移除字段回收 116，净增 132 → 正好压垮。这不是逻辑错误，是**零余量的 RAM 预算墙**。
- **代码**：`refmem_cmd_load` 的两条 submit 路径改用 `refmem_control_transport_route()`（与单测同一决策函数），去掉重复的 ring/bootstrap 布尔组合。
- **待决**：RAM 预算需 owner 决策——(a) 在别处释放 ≥256B；(b) 给 bootstrap 加编译开关默认关，待预算批准再开；(c) 缩 heap（运行时风险）。
- **残余**：carrier 未注册；HIL 未做；无 P3/FULL；代码未提交。

### CONSOLE-PROGRESS-20261104-020：M3 失败 reason/交接与资源归属澄清

- **结果**：relay 增加 `last_reason`（BUILD_FAIL / TX_FAIL / ACK_TIMEOUT / INVALID_FRAME / PEER_TIMEOUT / RING_HANDOVER），并追加到 `BOOTstrap:STATus?`；core0 在 ring ready 时自动 `handover`（撤销 readiness + disarm adapter，计数与 reason 保留），ring stop 后不自动 re-arm；修正资源判断：REFTMEM SPI 在 `pio0`、TDMA ring 在 `pio1/pio2`，二者不共享 PIO 指令空间，真正冲突的是 `pio0` 上的 SYNC_IO 调试组。
- **验证**：relay host 用例新增 peer-timeout reason、handover reason、TX-fail reason；DHRT100/DHRT100_B 的 relay/link/scpi_system 对象编译 exit 0；`run_refmem_all_host_tests.ps1` 待本轮收口复跑。
- **残余**：carrier 未注册（命令 ACK 跟踪未做）；无两板 HIL/P3；产品台控制对未决；`pio0` 归还 SYNC_IO 的 program release 未做；代码未提交（firmware 需 FULL）。
- **下一 gate**：两板 HIL 验证 readiness/RX/TX 角色切换；或先做 carrier 的 ACK 跟踪。

### CONSOLE-PROGRESS-20261104-019：M2b HELLO/responder/core0 hook 与只读状态

- **结果**：新增 HELLO probe 绑定（`refmem_bootstrap_hello_*`，round-trip host 用例）；relay 增加 responder 模式（LISTEN → 收到对端 probe → guard → MASTER 回帧 → LISTEN）；`distributed_refmem_service()` 调用 relay；SCPI `SYNC:SPI:ARM` 成功后启动 relay、`DISarm` 停止；重新登记 `SYSTem:REFMEM:SYNC:SPI:ARM/DISarm/STATus?`，新增 `SYSTem:REFMEM:BOOTstrap:STATus?`（armed/ready/state/peer/tx/rx/timeout/retry/invalid/build_fail/tx_fail + owner 拒绝计数）。
- **验证**：`run_refmem_bootstrap_hello_tests.ps1`、`run_refmem_bootstrap_relay_tests.ps1`（含 responder/guard）、owner 与 035 policy 用例全绿；DHRT100/DHRT100_B 的 hello/relay/link/scpi_system/scpi_port 对象编译 exit 0。
- **残余**：carrier **未注册**（命令 ACK 跟踪未做，避免 POSTED 永久挂起）；无两板 HIL/P3；PIO 回收与 018 交接未做；代码未提交（firmware 需 FULL）。
- **下一 gate**：命令 ACK 跟踪 + carrier 注册，或先做两板 HIL 验证 readiness/TX/RX 角色切换。

### CONSOLE-PROGRESS-20261104-018：M2 relay 状态机（半双工 TX/RX 角色交替）

- **结果**：新增 `refmem_bootstrap_relay`：TX(MASTER)→RX(SLAVE) 交替、ack 超时/重试预算、peer 心跳超时撤销 readiness、坏帧/TX 失败计数；readiness 只由收到对端帧置位。owner 增加 `transmit/receive_begin/receive_poll` 包装（每次交换重新 arm 角色）。架构 §6.1 记录半双工约束：`transmit` 要求 MASTER、`receive_*` 要求 SLAVE，角色只在 arm 固定，因此握手是角色切换而非常驻双向连接。
- **验证**：`run_refmem_bootstrap_relay_tests.ps1` 通过（未 armed/成功/peer 超时/3 次超时+2 次重试后 backoff/坏帧/TX 失败预算）；`run_refmem_bootstrap_transport_tests.ps1` 通过；DHRT100 与 DHRT100_B 的 relay/owner 对象编译 exit 0。
- **残余**：relay 未接 core0（无 `distributed_refmem_service()` hook）、无 HELLO 绑定/对端 responder/guard、carrier 未注册、只读状态未暴露；无 HIL/P3；代码未提交（firmware 需 FULL）。
- **下一 gate**：M2b 集成——HELLO 绑定 + responder + core0 hook + carrier 注册 + 只读计数。

### CONSOLE-PROGRESS-20261104-017：M1 adapter 所有权迁移到 REFMEM bootstrap owner

- **结果**：新增 `refmem_bootstrap_transport` owner（arm/disarm/snapshot/armed/adapter 诊断入口 + 拒绝计数）；`refmem_spi_physical_adapter_t` 从 SCPI `scpi_refmem_sync_state_t` 移除，SCPI `SYNC:SPI:ARM/DISarm/STATus?`、raw/line 全部改走 owner；arm 现在带前置门：model/realtime idle 且 ring 未 ready，否则拒绝并给 `REFMEM_BOOTSTRAP_NOT_STOPPED` / `REFMEM_BOOTSTRAP_RING_READY`。
- **验证**：`tools/tests/run_refmem_bootstrap_transport_tests.ps1` 通过（NULL/未 STOP/ring ready/adapter 失败/成功/Snapshot/disarm/转发参数）；`ninja -C build` 编译 DHRT100 与 DHRT100_B 的 owner、`scpi_system_snapshot_commands.c`、`scpi_platform_snapshot_commands.c`、`scpi_node_snapshot_commands.c` 通过。
- **残余**：carrier 未注册、relay/逐 target 握手 readiness 未做；PIO 指令空间回收未做；无 HIL/P3；代码未提交（firmware 需 FULL）。
- **下一 gate**：M2 relay + readiness（注册 carrier、超时/重试、reason/计数）。

### CONSOLE-PROGRESS-20261104-016：建环前低速承载的设计冻结与准入层

- **结果**：架构 §6.1 改为复用 REFMEM `refmem_spi_physical` adapter（不新增 TDMA persona program），并写清 profile 边界：P4.5 最小系统/调试 profile 与产品 BiSS/RJ45 线序不是同一条，产品侧建环前控制面仍需 owner 决策。§6.3 新增命令面三层口径（本地 SCPI / REFMEM 类型+目录 / 目标 handler），修正 §6.2 把“本地已有”写成“缺失”的表述；TODO 017/018/035 与阻塞项同步；REFMEM TODO 新增 P4.7（建环前低速控制面）任务清单。
- **代码**：新增 control-transport 准入/路由（`refmem_control_transport_route/submit/admits` + carrier 注册/注销）；`distributed_refmem_ring_transport_ready()` 语义扩展为 ring 或 carrier，`distributed_refmem_ring_link_ready()` 保留 ring-only 供路由判优先级；`refmem_cmd_load` 的单帧与分片两条 submit 路径接入 carrier；carrier 未注册时行为与原 ring-only 一致。
- **验证**：`tools/tests/run_refmem_position_reset_policy_tests.ps1` 通过（ring/双无/carrier 未就绪/就绪/ring 优先/注销/回调参数）；`ninja -C build` 编译 DHRT100 与 DHRT100_B 的 `refmem_cmd_load.c`、`refmem_position_reset_policy.c`、`distributed_refmem_plane.c` 通过。
- **未做/残余**：carrier 本体（adapter 所有权迁移、逐 target 握手 readiness、core0 relay、PIO 指令空间回收、两板 HIL）未做；无 P3/台架证据；产品线序是否提供控制对未决；代码未提交（firmware 改动需覆盖当前指纹的 FULL 凭证）。
- **下一 gate**：CONSOLE-017 的 adapter 所有权迁移 + relay；CONSOLE-018 的 bootstrap→ring 交接顺序。

### CONSOLE-PROGRESS-20261102-015：M5 CONSOLE 范围收口

- **结果**：13 个页面全部通过 Router 注册并实现 `render(vector)`；`app.py` 无 `messagebox`/裸 `self.after`；OTA 走 `WorkflowSpec`；Store 含 Connection/Mask/Run/Ack/Ui 五 slice；Settings/ErrorBoundary/LogView/ThemeTokens/DPI 已接。
- **验证**：`test_trigger_debug_ui.py + test_trigger_debug_demo.py` 27 passed；UI smoke 断言 13 页均有 render；docs 250/0、38 doc tests。
- **残余**：全量仓库 `pytest tests/python` 首败为 `test_app_realtime_profile` 的 `profile.exe` exit 3221226505（非 CONSOLE 改动）；无 HIL/P3；TCP 活跃 handler 未 join；ACK 之外页面字段仍本地；CONSOLE 契约未登记；未提交。
- **判定**：CONSOLE 重构目标范围完成；全仓基线/HIL/契约作为后续硬化项，不写成本次已通过。

### CONSOLE-PROGRESS-20261102-014：M5 ACK slice 与 13 页 render 收口

- **结果**：`ConsoleUiVector` 新增 `AckSlice`；`AckPage.show_status/show_reply` 写 Store，`render` 从 Store 回填 seq/state/ACK/NACK/timeout/reason；UI smoke 断言 13 个页面均有 `render`，Control 通过 `control_holder.render` 代理。
- **验证**：targeted host 27 passed；`app.py` 无 `messagebox`/裸 `self.after`；docs 250/0、38 doc tests。
- **全量 host**：仍在 `test_app_realtime_profile` 的 `profile.exe` exit 3221226505 首败，未计为通过。
- **剩余**：全量 host 基线、HIL/P3、TCP 活跃 handler、其他页面字段 slice 化、契约登记。

### CONSOLE-PROGRESS-20261102-013：M5 页面全覆盖与 host 回归（部分）

- **结果**：剩余 9 个页面（Overview、5 个 FunctionPage、Control、Ota、Tcp）补齐 `render(vector)`；`FunctionPage` 负责 card 转发，`FunctionCard/Control/Ota/Tcp` 按连接态控制发送/读取按钮；13 个 tab 现在都通过 Router 订阅 Store。
- **生命周期**：`OtaPage` 的取消事件改名 `_cancel_event`，新增 `cancel/dispose`；`TcpPage` 新增 `render/cancel/dispose`；关闭时 Router 退订并调用页面生命周期。
- **验证**：相关 host 套件 `test_trigger_debug_ui.py + test_trigger_debug_demo.py` 27 passed；UI smoke 断言 13 个页面均有 `render`；`app.py` 无 `messagebox`/裸 `self.after`；docs 250/0、38 doc tests。
- **全量 host 未绿**：`pytest tests/python -q` 在 `test_app_realtime_profile.py` 因 `profile.exe` exit 3221226505 失败，与本次 CONSOLE 改动无关；未继续跑完，未把它计为通过。
- **剩余**：全量 host 基线、HIL/P3、契约登记、ACK 业务数据 slice 化。

### CONSOLE-PROGRESS-20261102-012：M4 Dialog/Settings/ErrorBoundary/WidgetKit 收口

- **结果**：`app.py` 的 17 处直接 `messagebox` 全部改为 `DialogService`（error/warning/info/confirm）；`ui/settings.py` 提供 schema 白名单、原子写、损坏备份 `.bad`；`ui/errors.py` 提供 ErrorBoundary + `report_callback_exception` 限流；`ui/widgets.py` 提供 ErrorBanner/LogView；`ui/theme.py` 提供主题 token 与 `apply_tk_scaling`；DemoApp 接入 ErrorBanner、LogView、Settings。
- **验证**：`app.py` 已无 `messagebox`/裸 `self.after`；settings/error 测试新增；demo+UI 27 passed。
- **剩余**：全量 HIL/P3、TCP 活跃 client handler join、契约登记；M5 处理。

### CONSOLE-PROGRESS-20261102-011：M3 ACK/回读/诊断/原始命令页面迁移

- **结果**：`PageRouter.register_legacy` 对实现 `render(vector)` 的页面订阅 `ConsoleStore`，页面关闭时退订；`AckPage`、`QueryPanel`、`DiagnosticsPage`、`RawPage` 增加 `render/cancel/dispose`，按 `ConnectionSlice.connected` 控制读取/发送按钮。
- **去裸 after**：`AckPage._auto_tick` 改用 `TkScheduler` 并保存 token；`OtaPage` 的 receipt 定时也改用 `app.scheduler`。当前 `app.py` 已无 `self.after(` 调用。
- **Dialog**：`QueryPanel` 参数错误改走 `app.dialogs.error`；`AckPage`/诊断/原始命令无 `messagebox`。
- **测试**：新增 Router→Store→render 用例；UI smoke 断言连接后 ACK/raw 按钮可用；demo+UI 25 passed。
- **剩余**：`FunctionCard`/`ControlPage`/`OtaPage`/`TcpPage` 仍有 17 处直接 `messagebox`，留给 M4 WidgetKit/DialogService 收口。

### CONSOLE-PROGRESS-20261102-010：M2b OTA 迁移为可恢复 WorkflowSpec

- **结果**：新增 `ui/workflow.py`（WorkflowSpec/StepSpec/WorkflowState/WorkflowRunner）与 `ota_workflow.py`（snapshot→begin→stream→end→wait→receipt 六步）。`OtaPage.start` 不再启动 ad-hoc `_run` 线程，改为 `OtaWorkflow.start`；`_run` 仅保留兼容 wrapper；`abort` 同时 cancel workflow；新增“恢复上次”按钮，按 step cursor 恢复；`stream` 用 `chunk_index` checkpoint，并在恢复前用 `PROGress?` 校验偏移。
- **安全边界**：`begin`/`end`/`receipt` 标记 `resume_safe=False`；`stream` 可恢复但设备偏移不一致直接 fail-closed；state 写 `out/workflows/ota.conventional-<run_id>.json`，版本/id 不匹配拒绝加载。
- **测试**：`tests/python/test_trigger_debug_ui.py` 增加 WorkflowRunner 持久化/恢复/拒绝 unsafe resume/cancel；`test_trigger_debug_demo.py` 增加 OTA workflow 六步/凭证/2 块集成用例与按 chunk cursor 恢复用例；当前 demo+UI 24 passed。
- **未做**：M3 页面迁移、M4 WidgetKit、M5 HIL/收口；OTA 的 `begin`/`end` 失败仍需从头重跑（无 durable offset）。

### CONSOLE-PROGRESS-20261102-009：M2a TCP 停止移出 UI 线程

- **结果**：`device.TcpBridge` 增加 `stop_async()` / `wait_stopped()` / `_stop_server()`：先 detach server 拒绝新客户端，再在后台线程执行 `shutdown/server_close`；`TcpPage.stop()` 改为异步 + `TkScheduler` 轮询完成，UI 不再直接 `shutdown()`；`disconnect()` 用 `stop(wait=True)` 在断开 transport 前等待桥接退出。
- **测试**：`tests/python/test_trigger_debug_ui.py` + `test_trigger_debug_demo.py` → 19 passed；`py_compile app.py/device.py` OK。
- **剩余**：`TcpBridge` 的活跃 client handler 仍是 daemon 线程，未追踪 join；OTA 仍是 ad-hoc 线程（M2b）。

### CONSOLE-PROGRESS-20261102-008：Demo 按 Tk 架构完成 strangler 底座接入

- **结果**：新增 `tools/domains/trigger/trigger_debug_demo/ui/`：`state.py`（四 slice Store）、`scheduler.py`（Tk/Fake）、`dialogs.py`（DialogPort）、`page.py`（Page/NavigationDecision/LegacyPageAdapter）、`router.py`（Notebook Router + guard）、`pump.py`（有界 UiPump/PumpSink）。
- **接入**：`DemoApp` 使用 Store/Router/TkScheduler/TkDialogService/UiPump；13 个现有 tab 通过 `LegacyPageAdapter` 注册；`results/tcp/ota` 事件改为 pump；`Dispatcher` 结果由 `_on_result` 投递；`disconnect` 增加 `join(timeout)`；OTA 独占期非 OTA 命令改为静默丢弃 + status，不再弹模态。
- **未做**：没有重写 13 个 legacy Page 内部逻辑；`OtaPage` 仍是 ad-hoc 线程；`TcpBridge.shutdown` 仍在 UI 线程（legacy）；没有 implement WorkflowRunner/REFMEM/T2/TDMA/DPLL/校准。
- **测试**：新增 `tests/python/test_trigger_debug_ui.py`，覆盖 Store 四 slice/revision/退订、FakeScheduler、UiPump 有界/terminal 保留、PumpSink、Router guard、懒加载 Page；demo+UI 共 19 passed。
- **门禁**：`python -m pytest tests/python/test_trigger_debug_ui.py tests/python/test_trigger_debug_demo.py -q` → 19 passed；`python -m py_compile` app+ui → OK；`docs_check --strict-names` → passed（files=250 warnings=0）；`doc_regression_check` → passed。

### CONSOLE-PROGRESS-20261102-007：三次独立评审后的架构收敛

- **评审 1（架构一致性）**：指出单端口远程 REFMEM 与 `refmem_control_submit.c` 的 ring-ready gate 矛盾；REFMEM 目录已覆盖 `LOAD`/`LEASE`/`DPLL`/`VDC OUTPut`/`T2`，文档“未覆盖”表述过宽；`MODULES` 不是页面事实源；Workflow 与 OTA ad-hoc 缺现行例外；候选契约被提前当验收条款。
- **评审 2（Tk 工程）**：指出关闭/取消链、Page/Store 契约、OTA 单端口独占期三个 BLOCKER；`UiPump` 无界、after 无所有权、Fake 无注入点、`can_close` bool/reason 冲突；建议首版不引入 ttkbootstrap、删除 snapshot/restore/图片等过度设计。
- **评审 3（文档治理）**：指出主架构曾引用不存在的 CONSOLE_TODO.md（已改为 `docs/console/CONSOLE_DOMAIN_TODO.md`）、`docs_check` 裸文件名盲区、README 任务范围过期、Related 缺 TK、目标树缺 `console/`、P0 与 C0–C4 缺映射、进展缺门禁凭证。
- **已修**：§6 增加“单端口可用前提”并明确 ring 未 ready 只能本地/维护多端口；§6.2 增加退出标准并按 catalog 行号纠正阻塞；§6.1 补 `STOPPED` 与“新增低速 persona program”；§7.6 增加现行例外表；§7.8/7.9/§10 标注候选契约；Tk 文档补 Page `render/cancel/dispose`、四 slice、Scheduler/ControlPlane/Dialog 注入、关闭时序、OTA 独占期、UiPolicy、设置原子写；TODO 新增 033–036，README/索引/阶段映射对齐。
- **未修**：`docs_check` 裸文件名死链检查（CONSOLE-036，需工具改动 + host 验收）；CONSOLE-002/003/004/035 仍未登记/未落地；代码仍为 legacy，未改 Tk 实现。
- **门禁**：`python tools/docs_check/docs_check.py --strict-names` → passed（files=250 warnings=0）；`python tools/doc_regression_check.py` → passed；`python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -q` → 38 passed。

### CONSOLE-PROGRESS-20261102-006：Tk 应用框架写入架构，接上主架构/索引/TODO

- **结果**：新增 `docs/console/CONSOLE_TK_ARCHITECTURE.md`，定义 Shell/Router/Page/Store/Intent/UiPump/Dialog/WidgetKit/ErrorBoundary 分层与契约；`CONSOLE_ARCHITECTURE.md` 新增 §11“Tk 应用框架”摘要并链接详情。
- **索引**：`docs/console/README.md`、`docs/README.md`、`docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md` 已加入该 canonical；TODO 新增 `CONSOLE-028..032`（Shell/Store/UiPump/Dialog/测试打包）。
- **口径**：Tk 层采用 MVP/Passive View + Redux-lite Store/Intent + Hexagonal ControlPlane + Statechart/Saga workflow；不引入完整 MVVM/Redux/Temporal 运行时。
- **现状**：当前 Demo 仍是 legacy（Page 直接继承 `ttk.Frame`、状态散在 Tk 变量、直接 `messagebox`、OTA ad-hoc 线程）；本次只冻结框架定义，未改代码。
- **门禁**：`python tools/docs_check/docs_check.py --strict-names` → passed（files=250 warnings=0）；`python tools/doc_regression_check.py` → passed；`python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -q` → 38 passed。
- **下一步**：先落 `CONSOLE-028/029`（Page/Router + Store/Intent），再 `CONSOLE-030`（UiPump），最后迁移 OTA/Workflow 页面。

### CONSOLE-PROGRESS-20261102-005：单端口改为最终目标，多端口降级为过渡路径

- **结果**：`CONSOLE_ARCHITECTURE.md` §6 改为“单端口目标与过渡期路径”，新增 §6.2 单端口路径优化登记（REFMEM/T2/TDMA/DPLL/校准/OTA 的当前→目标 transport、阻塞、迁移任务）；§7.4/7.5/§9/§10 统一改为“目标单端口，过渡期多端口”。
- **TODO**：新增 `CONSOLE-026`（单端口路径登记）与 `CONSOLE-027`（过渡多端口退出/UI 标识）；原有阻塞项改为“单端口尚未收敛”。
- **口径**：多端口不是产品模式；默认 workflow 的 `target_transport` 必须是 `single_port_refmem`，多端口仅允许显式维护模式并在 wrapper 通过后退役。
- **现状**：常规 OTA 已是单端口顺序；T2/TDMA/DPLL/校准/REFMEM 装载仍是过渡多端口（或混合），尚未收敛。
- **下一步**：按 §6.2 逐项补 typed REFMEM command/table package，先把 REFMEM 装载与 T2 表安装收敛到单端口。

### CONSOLE-PROGRESS-20261102-004：Workflow 定义、步骤模型与能力范围写入架构

- **结果**：`CONSOLE_ARCHITECTURE.md` §7 扩充为完整定义：§7.2 形式与定义（Spec→Planner→Runner→State→Evidence→视图）、§7.3 步骤模型/状态机、§7.4 能做/不能做/与命令卡/宏/脚本/验收工具的边界、§7.5 各流程能力矩阵、§7.6 L0–L3 成熟度与 v1 线性边界、§7.7 待定设计选择。
- **明确约束**：workflow 必须声明 transport（单端口 REFMEM / 维护多端口 / 离线）；v1 不做 parallel、不做超过 1 层嵌套；OTA 是第一迁移对象，T2 第二。
- **未实现**：当前 OTA 仍是 ad-hoc 线程，T2/TDMA/DPLL/校准无 workflow；本次仅冻结定义与边界。
- **下一步**：按 `CONSOLE-006/019` 落 WorkflowPolicy/Spec/Runner，再迁移 OTA。

### CONSOLE-PROGRESS-20261102-003：控制台约束——长期流程必须 workflow 化

- **结果**：`CONSOLE_ARCHITECTURE.md` 新增 §7“工作流约束（Workflow-first）”，定义 WorkflowSpec/Step/运行语义/证据/UI/禁止项；明确 OTA、T2、TDMA 启环、DPLL/VDC、校准训练、REFMEM 装载必须走 workflow，不得以“按钮 + 线程 + 内存状态”实现。
- **新增任务**：`CONSOLE-019` WorkflowRunner + 持久化/恢复、`CONSOLE-020` OTA 迁移为首个 workflow、`CONSOLE-021..024` T2/TDMA/DPLL/校准 workflow、`CONSOLE-025` 版本矩阵。
- **现状差距**：当前 OTA 仍是 `OtaPage` 内的 ad-hoc 线程，T2/TDMA/DPLL/校准没有 workflow；因此本约束是**目标态**，不是当前已满足项。
- **下一步**：先落 WorkflowPolicy/WorkflowSpec 静态定义与 WorkflowRunner，再把 OTA 迁移为第一个可恢复 workflow，最后复制到 T2/TDMA/DPLL/校准。

### CONSOLE-PROGRESS-20261102-002：架构决定——校准/TDMA 前用低速 SPI PIO 承载 REFMEM 控制面

- **结果**：在 `CONSOLE_ARCHITECTURE.md` §6.1 增加“引导期低速 SPI PIO（bootstrap persona）”链路：STOP 态热加载低速 SPI persona，先用它承载 REFMEM command/ACK/table/status，完成校准与拓扑/时钟准备后，再通过 persona FSM（QUIESCING → UNLOADING → LOADING → ACTIVE）切到高速 TDMA persona；失败走 ROLLING_BACK 回低速恢复。
- **边界**：仍是同一组 SPI 引脚；不是第二总线/第二命令槽/第二 ACK；REFMEM 的 CRC/seq/fence/quality/ACK 语义不变；只在 STOP/维护边界热加载，RUN 中禁止。
- **未实现**：低速速率、PIO 指令字数、DMA/SM 预算和切换窗口尚未核算；已登记 `CONSOLE-017`（bootstrap persona）与 `CONSOLE-018`（高速交接/回滚），当前只是架构决定，不是已验证事实。
- **下一步**：先做 persona 状态/资源核算与 host 模型，再进台架验证切换与回滚。

### CONSOLE-PROGRESS-20261102-001：CONSOLE 主域立项，调试 Demo 成为首个交付物

- **结果**：新增 `docs/console/` 主域三件套与 README；`CONSOLE` 加入文档命名白名单；`docs/README.md` 增加域入口与 canonical 行。调试 Demo 定位为本域首个交付物，当前仍在原路径，迁移列为 `CONSOLE-015`。
- **交付物**：`tools/domains/trigger/trigger_debug_demo/`
  - 单端口 GUI + 本地 TCP 共用同一 Dispatcher；
  - 串口 / USB TMC（PyVISA）/ 模拟器三种 transport；
  - REFMEM 统一控制面与应用层 SCPI 两条通道；
  - 常规单板 OTA（BEGIN/PBEGIN/DATA/END/BOOT/COMMIT）与自动凭证；
  - 板卡 ACK、诊断、原始命令页。
- **证据**：
  - host：`python -m pytest tests/python/test_trigger_debug_demo.py -q -p no:cacheprovider` 全绿（快照，非事实源）；
  - 模拟 OTA：`out/ota_receipts/` 自动生成 JSON + `.sha256`；
  - 截图：`out/trigger_debug_demo.png`、`out/trigger_debug_demo_ota.png`、`out/trigger_debug_demo_tcp.png`；
  - 注册：`TOOL-CONSOLE-DEBUG-DEMO`（已从 `TOOL-TRIGGER-DEBUG-DEMO` 改名；路径迁移见 CONSOLE-015）。
- **未覆盖**：REFMEM 装载/表/质量页、T2/TDMA/DPLL/校准四条流程、安全门、HIL/台架证据；见 TODO 的 CONSOLE-005..016。
- **下一 gate**：C1 底座——先做 REFMEM 页 + FlowRunner + receipt 规范 + 安全门；在此之前不迁移路径、不宣称主域完成。
