# BiSS 组网 HIL 交接记录

Status: Active
Domain: Verification
Canonical: `docs/BISSC_NETWORK_LOOPBACK_PLAYBOOK.md`
Related: `docs/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md`, `docs/TASK_PROGRESS.md`, `docs/README.md`
Last updated: 2026-08-11

本文档记录 2026-08-11 这轮 BiSS 组网处理流程，便于在其他电脑继续工作。

## 当前 Git 状态

- 当前分支：`main`
- 本轮新增提交：`8dbc8f0 feat: add five-board loopback preflight`
- 该提交创建后，`git push origin main` 曾被工具审批层拒绝；拒绝原因是审批流断开，不是远端 Git 错误。
- 这轮已经把 HIL 口径收回到 A3 单外部 COM + 内部 BiSSC 组网。
- 继续前应先执行：

```powershell
git status --branch --short
git log -3 --oneline
git push origin main
```

如果远端还没有 `8dbc8f0`，应先推送；如果远端已有该提交，则继续下一轮待办。

## 本轮目标

用户最新明确的台架约束是：

- 5 块板参与验证，但只有 A3 具有外部 COM 口。
- 其余板通过 BiSSC 内部组网通讯，不再按“每块板一个 COM”处理。
- A3 是控制面入口，内部网络中的其他节点按逻辑角色声明。
- 模拟转台 + 网分的那块板属于内部节点，默认按 `A4` 处理。

注意：当前仓库固件仍未实现真实 BiSSC 节点帧级闭环，所以本轮只完成“拓扑 + A3 外部入口 + SCPI 预检”的脚本收口，不宣称已经完成全部内部帧通信。

## 已完成改动

新增工具：

- `tools/distributed_loopback_validate/__init__.py`
- `tools/distributed_loopback_validate/distributed_loopback_validate.py`

文档更新：

- `docs/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md`
  - 在 P6 记录 A3 单外部 COM + BiSSC 内部组网的 HIL 入口。
  - 明确模拟板同时承担 turntable + VNA，位于内部节点侧。
- `docs/README.md`
  - 在验证工具入口中加入 BiSS 组网 preflight 脚本。
- `docs/TASK_PROGRESS.md`
  - 追加 `TASK-20260811-001 - BiSS 组网 HIL preflight 脚本`。

## 脚本能力

脚本路径：

```powershell
python tools/distributed_loopback_validate/distributed_loopback_validate.py --help
```

当前能力：

- 只要求 1 个外部入口：`--a3-port COMx`。
- 其余 4 块板通过 `--peer` 声明逻辑角色，走内部 BiSSC 组网口径。
- 必须有且仅有 1 个 `--sim-role`，默认模拟板是 `A4`。
- 逻辑角色支持 `A0`、`A1`、`A2`、`A3`、`A4`，其中 `A3` 固定为控制面入口。
- 非 dry-run 时只会打开 A3 的串口并执行 SCPI preflight：
  - `*IDN?`
  - `SYST:FW:BUILD?`
  - `SYST:CORE?`
  - `SYST:CFG:STAT?`
  - `SYST:CFG:ACK?`
  - `SYST:CFG:ROLE? 0..4`
  - `SYST:MODE:TAB? 1`
  - `SYST:RESource:TAB? 0`
  - `SYST:FAULT:TAB? 0`
- 输出：
  - `summary.json`
  - `scpi_log.txt`

## 已验证命令

语法检查：

```powershell
python -m py_compile tools\distributed_loopback_validate\distributed_loopback_validate.py
```

文档检查：

```powershell
python tools\docs_check\docs_check.py
```

结果：通过，仍有 6 个历史命名 warning。

`A3-only` 口径 dry-run：

```powershell
python tools\distributed_loopback_validate\distributed_loopback_validate.py `
  --a3-port COM5 `
  --peer A0 --peer A1 --peer A2 --peer A4 `
  --sim-role A4 --dry-run --out-dir build-biss-network\dryrun_a3_only
```

`A4` 口径 dry-run：

```powershell
python tools\distributed_loopback_validate\distributed_loopback_validate.py `
  --a3-port COM5 `
  --peer A0 --peer A1 --peer A2 --peer A4 `
  --sim-role A4 --dry-run --out-dir build-biss-network\dryrun_final
```

结果：A3-only 拓扑 dry-run 通过。

## 真实 HIL preflight 用法

在现场确认 A3 外部 COM 口后执行，例如：

```powershell
python tools\distributed_loopback_validate\distributed_loopback_validate.py `
  --a3-port COM5 `
  --peer A0 --peer A1 --peer A2 --peer A4 `
  --sim-role A4 `
  --timeout 8 --settle 2 `
  --out-dir build-biss-network\preflight_a3_only
```

验收口径：

- A3 能返回有效 `*IDN?` 和 build id。
- `SYST:CORE?` 中 core1 enabled。
- `SYST:CFG:STAT?` ready/gate_state 正常。
- `SYST:CFG:ACK?` 中 target mask 和 reason CRC 非零。
- `SYST:MODE:TAB?`、`SYST:RESource:TAB?`、`SYST:FAULT:TAB?` 可查询。
- `SYST:CFG:ROLE? 0..4` 返回完整逻辑拓扑快照。

## 不能混淆的边界

本轮完成的是 BiSS 组网 preflight，不是完整内部闭环：

- 未实现真实内部 BiSSC 节点帧收发闭环。
- 未实现跨板 ACK delta 同步。
- 未实现 `FIRE_LOAD` 装载和 T2 采样闭环。
- 未实现转台 Compare Out 和 VNA READY/REDY 真实 IO 模拟。
- 未执行 5 串口预检，因为当前口径不是 5 串口。

## 下一步建议

优先级建议：

1. 在另一台电脑先确认 `8dbc8f0` 是否已推送到 `origin/main`。
2. 用 A3 外部 COM 跑一次 `--dry-run` 和一次真实 SCPI preflight。
3. 若 preflight 通过，继续 `RTOS_DISTRIBUTED_TRIGGER_PARTITION.md` 的 P4：
   - 定义 RJ45 `SYNC/FIRE_LOAD/DONE/MEAS_DONE/FAULT` 帧格式和 CRC。
   - 定义 `REFMEM_DELTA` / `REFMEM_EPOCH` 帧格式。
   - 实现 ACK/NACK/busy_flags 位图同步。
4. 组网脚本后续扩展为真实内部闭环：
   - 发送/观测 BiSSC 节点帧。
   - 校验 `seq/run_id/epoch`。
   - 统计 CRC、late、latency。
   - 输出长稳报告。
