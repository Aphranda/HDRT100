# 硬件验收操作手册（P3 37 步）

Status: Active
Domain: VALIDATION
Canonical: `docs/validation/VALIDATION_HARDWARE_ACCEPTANCE_PLAYBOOK.md`
Related: `AGENTS.md`, `docs/validation/README.md`, `docs/validation/P3_FLOW.html`, `config/hardware_acceptance/p3_bench.json`
Last updated: 2026-11-05

本手册给**其他会话/操作者**使用，目标是避免“命令跑错、台架被并发占用、把 QUICK 当 FULL、把瞬态当硬件故障”导致验收反复失败。参数与路径是 2026-11-05 的**运行快照**，不是事实源；以 `config/hardware_acceptance/p3_bench.json`、`CMakePresets.json` 和 `check-staged` 的当前输出为准。

DPLL 证据选择：默认 `--monitor auto`。检测到示波器先做外部；外部通过就**不再做内部**；外部失败（含 VISA/枚举失败）自动切内部；二者任一通过都签发凭证，但 receipt 用 `dpll_qualification` 区分 `SCOPE_PASS` / `INTERNAL_FALLBACK` / `INTERNAL_ONLY`，不得把 fallback 说成 scope-qualified。

## 0. 先判定：这一轮到底该不该跑

| 改动范围 | 需要什么 | 入口 |
|---|---|---|
| 固件/构建/`config/`/`drivers/`/`middleware/`/`tools/`（`tools/acceptance/` 除外） | FULL P3 凭证 | `hardware_acceptance_workflow.py --profile full` |
| 只改 `tests/`、`.githooks/`、`tools/acceptance/` | host 凭证，不上台架 | `python tools/acceptance/host_acceptance.py run` |
| 只改 `docs/`、`*.md` | 文档门禁 | `docs_check` + `doc_regression_check` |
| 没有源码改动，只想复核已有凭证 | 不要重跑 | `workflow_steps.py check-staged` |

**不要**为了“确认一下”重复跑整轮 FULL；先看 `out/HardwareAcceptance/<date>/<run>/workflow-state.json` 和已有 receipt 是否覆盖当前指纹。同一工作树、同一 build、同一指纹，已有 FULL 凭证时优先 `check-staged`，不是重跑。

## 1. 台架前置检查（只读，按顺序）

1. **单会话占用**：同一台架同时只允许一个验收/OTA/环控制进程。检查 `out/HardwareAcceptance/.bench.lock`；若 PID 仍存活，等它结束或由持有者停止；若 PID 已死，当前 workflow 会自动清 stale lock。**不要**并行起第二个 `hardware_acceptance_workflow.py`，也不要在验收中手动跑 OTA/环命令。
2. **四板同一 build**：只读查询四板 `SYSTem:FW:BUILD?`，必须完全一致；不一致先补齐 OTA/刷写，不要带病验收。
3. **BOARD:NO 唯一连续**：只读 `SYSTem:BOARD:NO?`，必须是 1/2/3/4（顺序按物理链路）；重复或 0 先修正。工厂重刷后常见 `NO.0`，需 `SYSTem:BOARD:NO 4` 这类写回。
4. **环已停止**：`SYSTem:TDMA:RING:STATus?` 的 `ring_enabled/up_running/down_running` 应为 0；step 7 会复位，但验收前不要人为起环。
5. **角色是 MASTER 也可**：step 7 复位会清掉 RAM-only 角色；step 27 会自动把 follower 设为 `FOLLOWER(1,0)` 并回读。**不要**在验收前手动写 ROLE（除非你在单独调试，且明确知道会改变台架状态）。
6. **示波器自动选择**：`--monitor auto`（默认）先看 `discover_optional_scope`；`present=true` 才走外部，`present=false` 直接内部，`present=null`（VISA/枚举失败）记录 UNKNOWN 后切内部。HDO 资源/serial 与 `p3_bench.json` 一致，CHAN1 接参考输出；只有显式 `--monitor internal` 才强制内部。
7. **构建 preset 正确**：当前树必须用 `pico2-v2-factory-candidate`（V2 分区/97 KiB heap）。默认 `pico2-release` 会 `RAM overflowed`；这是配置错误，不是板子故障。**不要**为默认 preset 调 heap。
8. **源码指纹/积压**：`git status --porcelain` 待提交条目 ≤40；>40 按 AGENTS §2.11 先冻结。`working_source_fingerprint` 与最近 build record 一致时才可 `--reuse-flashed`。

## 2. 标准命令

### A. 全新 FULL（构建 + OTA + 校准 + TDMA + DPLL + 凭证）

```powershell
python tools/hardware_acceptance/hardware_acceptance_workflow.py `
  --profile full `
  --preset pico2-v2-factory-candidate `
  --monitor auto `
  --time-budget 1500,1800
```

- `--profile full` 与 `--full` 等价；**续跑也必须带**，否则 step0 冻结的 scope/凭证档位可能变成 QUICK。
- `--monitor auto` 会从 `p3_bench.json` 派生 `out/HardwareAcceptance/p3_bench_scope.json`（`force_internal=false`）；**不要**手改这个派生文件。
- 自动流程：`present=true` 先外部；外部通过就**不跑内部**；外部失败（scope/窗口/证据/VISA）切内部；`present=false` 直接内部；`present=null` 记 UNKNOWN 后切内部。receipt 的 `dpll_qualification` 必须是 `SCOPE_PASS`/`INTERNAL_FALLBACK`/`INTERNAL_ONLY` 之一。
- `--time-budget warning,error` 按整轮累计耗时给足；当前全流程曾用 `1500,1800` 通过，但续跑前先看已耗时，必要时提高。不要用默认 60/100 跑 FULL。

### B. 四板已刷同一 build，复用 package（跳过构建/OTA）

```powershell
python tools/hardware_acceptance/hardware_acceptance_workflow.py `
  --profile full --reuse-flashed --monitor auto --time-budget 1500,1800
```

只有同时满足才用：四板 `SYSTem:FW:BUILD?` 相同；`out/build/<build>/p3-build-record.json` 的 `source_fingerprint` 等于当前工作树指纹；package 存在。否则去掉 `--reuse-flashed`，正常构建/刷写。

### C. 续跑已有 run（失败后最常用）

```powershell
python tools/hardware_acceptance/hardware_acceptance_workflow.py `
  --run out/HardwareAcceptance/<date>/<hawf-...> `
  --steps 8-36 `
  --profile full --monitor auto --time-budget 1500,1800
```

- 只重跑失败步骤及其下游；不要每次从 0 开始，除非要重新构建/刷写/冻结新 scope。
- **step 29 单独重试后，必须再跑 31-36** 让 step35 重签 receipt；只跑 29-30 不会产生完整凭证。
- 同一个 run 的 step0 已冻结 acceptance scope；**不能**用 `--run` 把 QUICK 改成 FULL。要换 FULL，开新 run（或 B 的 `--reuse-flashed`）。

## 3. 运行中看什么

- 运行目录：`out/HardwareAcceptance/<date>/hawf-<stamp>/`；命令日志、`workflow-state.json`、`build.log`、`ota.log`、`topology.log`、`dpll-short-summary.json`、`alarms.json` 都在这里。
- 关键判据：step 29/30 同轮 OK；step 33 预算 OK；step 34 分级 PASS（ERROR/FATAL 为 0）；step 35 `credential: written`；step 36 OK。
- step 29 证据：`dpll_qualification=SCOPE_PASS` 时要求 `external_status=PASS`、`scope_enabled=true`、窗口不是 12/12 missed；`INTERNAL_FALLBACK` 允许 `external_status=SKIPPED/FAIL`，但必须有 `dpll_external_attempt` 失败/UNKNOWN 记录和 internal health；`INTERNAL_ONLY` 只允许 scope 未检测到或显式 `--monitor internal`。
- **不要**在 step 29 运行中手动查询板子/起环/动 OTA；会污染 60 s 窗口。

## 4. 失败处置表（先按表，不要自由发挥）

| 现象 | 最可能原因 | 安全动作 | 禁止 |
|---|---|---|---|
| build 链接 `.bss will not fit` / `RAM overflowed` | 用了默认 `pico2-release`，不是 V2 preset | 加 `--preset pico2-v2-factory-candidate` 重跑 | 改 heap / 换回旧 build 硬刷 |
| step 8 `START <timeout>` 或 `-200 TDMA_RING_START` | 复位后 START 准入瞬态（50 ms 窗口） | 先重跑 `--steps 8-36`；现 P0T gap 已强制 ≥100 ms | 立刻全板下电/清环/重写 BOARD:NO；把 timeout 当成功 |
| step 29 NO.3/4 无脉冲、`STARVED`、12/12 scope missed | 可能是示波器/触发/窗口问题，不一定是板子 | 新流程会自动切内部；若内部也失败，再查 roles/`OUTPut:RUN?` 后重跑 `--steps 29-36` | 直接 factory reflash；把 fallback 当 SCOPE_PASS |
| OTA 某板 `0,6,512,5002,5` / `FLASH:JOB?` 超时 | 该板 flash journal/事务卡死 | 读 `STREAM:INIT?/STAT?/JOUR?/TXN?/BCB:HEALTH?`；保留证据，评估 factory reflash | 反复重试 OTA；未获用户同意就全擦 |
| receipt 是 `QUICK_DIAGNOSTIC` | 续跑没带 `--profile full`，step0 冻了 QUICK | 开新 FULL run（或用 `--reuse-flashed`） | 手改 receipt 的 profile/scope |
| `check-staged` 报 bench config hash 失配 | 派生 `p3_bench_scope.json` 被覆盖/改短 | 重新生成 scope config，重跑 35-36 重签 | 手改 receipt 的 sha |
| `bench busy` / lock 残留 | 另一个验收/OTA 进程在跑 | 等持有者结束；死 PID 由 workflow 清 | 并行起第二个验收；删 `out/` 证据 |
| scope `present=false` | 示波器未连/被占用 | 自动走内部，receipt 记 `INTERNAL_ONLY`；要外部证据就先修 VISA 后重跑 | 把 internal 当 scope-qualified FULL |

## 5. 成功后收口

1. 读 `config/hardware_acceptance/p3_acceptance_receipt.json`：`acceptance_profile=FULL`、`acceptance_scope=FULL`、`passed=true`、`build_id`、`source_tree_sha256`、`source_file_count`、`dpll_qualification`。`SCOPE_PASS` 要求 `dpll_summary.external_status=PASS`、`scope_enabled=true`、`dpll_scope_discovery.present=true`；`INTERNAL_FALLBACK`/`INTERNAL_ONLY` 要求 `scope_enabled=false`、internal health 通过，且 fallback 有 `dpll_external_attempt` 的 FAIL/UNKNOWN 记录。
2. 跑 `python tools/hardware_acceptance/workflow_steps.py check-staged`；必须显示 `sources=<n> build=<id> scope=FULL`。
3. 若本批还改了 `tests/`/`tools/acceptance/`：先 `python tools/acceptance/host_acceptance.py run`，再提交。
4. 代码/工具 + receipt 作为**一个**代码批次提交（`check-staged` 绑定 staged 指纹）；文档单独提交，不混批。
5. 交接记录：run 目录、命令、step 状态、receipt sha、build/fingerprint/files、external 状态、提交号、残余。

## 6. 绝对不要做

- 不要并行跑两个验收/OTA/环控制会话。
- 不要用默认 `pico2-release` 跑当前树，然后怀疑板子。
- 不要用 `--run` 改 acceptance profile/scope；要换就开新 run。
- 不要把 `--monitor internal` 的 `external_status=SKIPPED` 当 scope-qualified FULL。
- 不要在外部 scope 已通过后还跑内部采集；新流程会直接签发 `SCOPE_PASS`。
- 不要把 `INTERNAL_FALLBACK`/`INTERNAL_ONLY` 写成 `SCOPE_PASS`；receipt 的 `dpll_qualification` 必须如实。
- 不要在 step 29 运行中查板/起环/动 OTA。
- 不要因为 step 8/29 的瞬态失败立刻 factory reflash；先按表中“先重跑”处理。
- 不要手改 receipt、scope config、`workflow-state.json` 来“修”问题。
- 不要把 `out/` 下的失败证据删掉/覆盖后再重跑；重试用 `--out-dir` 或保留 run 目录。
