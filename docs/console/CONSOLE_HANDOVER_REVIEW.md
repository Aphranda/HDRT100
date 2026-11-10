# CONSOLE 会话交接（2026-11-09）

Status: Active
Domain: CONSOLE
Canonical: `docs/console/CONSOLE_HANDOVER_REVIEW.md`
Related: `docs/console/CONSOLE_ARCHITECTURE.md`, `docs/console/CONSOLE_TK_ARCHITECTURE.md`, `docs/console/CONSOLE_DOMAIN_TODO.md`, `docs/console/CONSOLE_TASK_PROGRESS.md`, `docs/calibration/CALIBRATION_SINGLE_PORT_BRINGUP_COMMANDS.md`
Last updated: 2026-11-09

> 本文是交接快照，非事实源；build/run/证据路径以实际报告为准。

## 1. 当前台架（交接时快照）

- 串口：`COM5` / NO.1，四板同一 build `20261108142853`。
- 10 kHz 默认档自组环在线：`enabled=1, nodes=4, up=1, down=1, adapter_started=1`。
- 协调板 `BOOT:PLAN?`：`action=1, steps=8, fail_step=0, boot_done=1, half_ring=0, profile_source=2（默认档）`。
- 当前 `GUIDE:PLAN?` 的 ladder 状态以现场为准；跑阶梯前先确认 `ladder_index=0`。

## 2. 已完成

### 2.1 页面结构

`校准引导` 已拆成二级 Notebook：

```text
汇总
P0 线序校准
P1 时钟标定
P2 编码标定
P3 时延标定
T0 TRN-00 marker
T1 TRN-01 SCK 偏移
T2 TRN-02 DATA 重复
T3 TRN-03 staging
```

- 汇总页：`BOOT:PLAN?` 状态、单口 C/T、`运行全阶梯 P1→T3`、参数汇总。
- 独立动作页：动作参数、`运行当前动作`、`读 PLAN?`、`读 BOOT:PLAN?`、独立日志。
- P0 只读；T3 运行前确认。
- 动作页标注 `驱动方`/`状态`，明确是板端执行，不是 host 编排。

### 2.2 动作载荷

按 `CALIBRATION_SINGLE_PORT_BRINGUP_COMMANDS.md` §4.6：

- P1：level 23，`0,4,16`，trials 4，阈值 1/64。
- P2：level 7，`0,1,80,120,0`，trials 1，阈值 10/512。
- P3：level 7，`0,0,32,256,3`，trials 1，阈值 10000000/50000；expected trials = nodes×repeats。
- T0–T3：页面保留各自 candidates 模板，按阶梯执行。
- 动作顺序必须 P1→P2→P3→T0→T1→T2→T3；跨阶会被固件拒绝。
- `app.c:1722` 的 “only P1 wired” 是旧注释；P2–T3 trial body 已存在，`reason=13` 常见原因是载荷形状/跨阶，不是未实现。

### 2.3 日志

运行日志按日期分目录：

```text
out/calibration/guide-10k/logs/YYYY-MM-DD/
  10k-P1-<HHMMSS>.log
  10k-P2-<HHMMSS>.log
  ...
```

报告 JSON 增加 `log_path`；PASS/FAIL 均落盘。

### 2.4 已验证

- P1 单口 C/T：多次 PASS（当前 build `20261108142853`；报告示例
  `out/calibration/guide-10k/action-P1/calibration/guide-10k/20261109-001454-4d3156.json`）。
- 参考脚本 P1 四板 PASS：`guide_cmdplane_probe.py ladder 1 6020 6120`。
- 全页面（14 tab）实板构建/截图/安全读回：`out/calibration/guide-10k/ui-verify/`。
- host：29 passed；docs：278/0；doc regression passed。

## 3. 当前阻塞

### P2

规范载荷执行后，P2 实际进入 trial body（1 trial），最终：

```text
status=2 (STOPPED)
reason=14 (NOT_ONLINE_SAFE)
ladder_index=1
```

这是 `APP_CTRQS_P2_CODED_STOP` 里 `calibration_manager_restore_training_stage()` 失败后设 `hard_fail_reason=NOT_ONLINE_SAFE`，不是 Q/复环 restore（后者应为 `status=3/abort_site=26|27`）。已试 P1 level 7（10k 环上四板 GREEN）后接 P2，仍复现；下一步查 P2 coded stop 的 training-stage 恢复，而不是继续调 P1 档位。

### P3–T3

P2 未通过，ladder 不前进，P3–T3 不能继续；之前 P3 `reason=13` 是错误载荷/跨阶导致，不要复用。

### 读路径

- **已统一（OBS）**：开关、`trigger_state`、`guide_plan`、`boot_plan`、`ring_status`、
  `cal_status`、`node_health` 走 `SYSTem:REFMEM:OBS?`（`ControlPlane.read` 逐槽行）。
  校准 `precheck`/报告用 key6，汇总启环状态用 key5，总览板卡在线用 key8。
- **命令结果**：`SYST:CTRL:STATus?` / `SYST:COMMAND:TARget?`（统一命令记录）。
- **诊断例外**：`GUIDE:PLAN?`（27 字段）、`BOOT:PLAN?`（20 字段）、`READ:*` 没有 OBS key，
  仍是本地 SCPI；UI 按钮标注“本地 / 诊断例外”，登记 `CONSOLE-046`。

## 4. 下一步

1. 复位阶梯；四板 `OPMode:STAGe 7 + APPLy`；P1 改为 level 7 重跑，再 P2。
2. 若 P2 仍 STOPPED，抓 `PLAN?` 第 24 字段 `abort_site` 与第 23 字段 `failed_mask` 定位 abort 点。
3. P2 通过后，用汇总页 `运行全阶梯 P1→T3` 一次跑完；逐页先单独验证。
4. ~~实现 REFMEM read 模型~~ 已完成可迁部分（OBS key3/4/5/6/8）；富字段读见 `CONSOLE-046`，
   扩 key 需修订 `REFMEM-READ-01` 并过 P3。
5. 提交前：`host_acceptance.py run`、`check-staged`；文档改动跑 §5。
6. `app.c:1722` 旧注释应由固件 owner 更新，避免下一会话误判。

## 5. 关键文件

- 页面：`tools/domains/trigger/trigger_debug_demo/calibration_page.py`
- 动作页：`tools/domains/trigger/trigger_debug_demo/calibration_action.py`
- Workflow/日志：`tools/domains/trigger/trigger_debug_demo/calibration_workflow.py`、`ui/runlog.py`
- 参考脚本：`tools/calibration/guide_cmdplane_probe.py`
- 指南：`docs/calibration/CALIBRATION_SINGLE_PORT_BRINGUP_COMMANDS.md` §4.6
- 进度：`docs/console/CONSOLE_TASK_PROGRESS.md` 20261109-021
