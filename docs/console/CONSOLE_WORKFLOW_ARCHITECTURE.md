# 运维与调试控制面（CONSOLE）工作流定义（WorkflowSpec）

Status: Active
Domain: CONSOLE
Canonical: `docs/console/CONSOLE_WORKFLOW_ARCHITECTURE.md`
Related: `docs/console/CONSOLE_ARCHITECTURE.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`, `tools/tool_workflow_triage.json`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-11-04

> 本文是 **workflow 的唯一定义**，从 `CONSOLE_ARCHITECTURE.md` §7 原样摘出。所有把脚本判为
> `WORKFLOW` 的登记（`tools/tool_workflow_triage.json`）都要按本文的 WorkflowSpec / 步骤模型 /
> 证据与版本约束执行；判定阈值由 Console 的 `WorkflowPolicy` 统一给出，不在文档里写死。

**长期流程不得以“按钮 + 线程 + 内存状态”实现。** 满足以下任一条即必须定义为 workflow：

- 多步，或需要等待设备/重启/人工确认；
- 需要中断、恢复、重试或跳过；
- 跨多个板卡/角色；
- 写 active 配置，或产生凭证；
- 依赖前置状态（STOP、cursor、calibration、ring、lease）。

判定阈值由 Console 的 `WorkflowPolicy` 统一定义（步骤数、时长、可恢复性、跨板数），不在文档里写死；UI 不得各自解释。

## 1 WorkflowSpec

每个 workflow 至少声明：

| 字段 | 含义 |
|---|---|
| `id` / `version` | 稳定标识与版本；与固件 build、命令目录版本做兼容矩阵 |
| `inputs` | 操作员输入、镜像、板卡目标、transport 模式 |
| `preconditions` | STOP/RUN、ring、cursor、calibration、lease、build 匹配 |
| `steps` | 有序步骤；每步有 id、标题、动作、期望回读、超时/重试、失败策略 |
| `transport` | `single_port_refmem` / `maintenance_multiport` / `offline`；必须显式 |
| `receipt_schema` | 每步证据与最终凭证的 schema |

每个 Step 至少声明：`id`、`precondition`、`action`、`expect`、`timeout/retry`、`evidence`、`on_failure`（abort / rollback / skip / manual）。

## 2 形式与定义

**Workflow 是可版本化、可恢复、可取证的多步设备操作状态机。** 它不是脚本、宏或“大按钮”；它是控制台执行长期流程的唯一合法形式。

```text
WorkflowSpec（声明式定义）
        ↓
Planner（前置检查 / 展开步骤）
        ↓
WorkflowRunner（单 writer 执行）
        ↓
State Store（out/workflows/*.json）
        ↓
Evidence / Receipt（每步 + 最终凭证）
        ↓
GUI / CLI / headless / TCP（只是视图）
```

## 3 步骤模型

每个 Step 至少声明：`id`、`precondition`、`action`、`expect`、`timeout/retry`、`evidence`、`on_failure`、`idempotent`。

| 步骤类型 | 用途 |
|---|---|
| `command` | 单条 SCPI / `CTRL:EXECute` 写操作 |
| `query` | 只读查询与字段断言 |
| `wait` | 轮询状态直到谓词成立或超时 |
| `manual` | 等待操作员确认（接线、换板、按 BOOT） |
| `subworkflow` | 复用子流程（如 TDMA bootstrap 子流程） |
| `foreach` | 对已声明、有上限的集合迭代（OTA 分块、逐板命令） |
| `parallel` | 仅允许独立只读/无写冲突的并行组，必须带 barrier |
| `evidence` | 写 receipt / 摘要 / 日志哈希 |
| `compensate` | 失败/取消时的安全收尾（STOP/ABORT/释放 lease） |

`foreach` 不是通用循环：必须有上限和可恢复 cursor；禁止在 workflow 中执行任意代码。

状态机：

```text
CREATED → PRECHECK → RUNNING
                         ↔ WAITING（设备/人工）
                         ↔ PAUSED
                       → ABORTING
                       → SUCCEEDED / FAILED / CANCELLED
```

## 4 能力范围

**能做：**

- 多步编排：命令、查询、等待、人工确认、子流程、有界迭代。
- 前置门禁：STOP/RUN、ring、cursor、calibration、lease、build/命令目录版本。
- 恢复：从最后成功步骤继续；恢复前重新校验前置/版本。
- 取消/中止：在步骤边界安全退出，执行 compensate。
- 重试：仅幂等步骤；非幂等必须人工决策。
- 证据：每步结果、原始响应、reason、hash；最终 receipt。
- 多 transport：目标态是单端口 REFMEM；维护多端口仅作为过渡路径；workflow 显式声明 `target_transport` 与可选的 `transitional_transport`。
- 多入口：GUI、CLI、headless；TCP 默认只读状态，写操作需显式开启。
- 版本矩阵：workflow version ↔ firmware build ↔ command catalog ↔ receipt schema。

**不能做：**

- 不能绕过固件 owner/lease/RUN 门禁。
- 不能定义新的 SCPI 语义；命令必须来自命令目录/契约。
- 不能替代 TDMA/VDC 的实时调度。
- 不能替代 P3/T2 验收判定；receipt 是证据，不是资格。
- 不能隐藏多端口；多端口必须显式声明并显示。
- 不能执行任意 Python/YAML 代码；只允许注册过的 action 类型。
- 不能并行写同一个 owner/同一板；并行只允许无冲突只读。
- 不能把 GUI 内存状态当唯一状态；页面关闭后 workflow 仍可恢复。

与其他形式的关系：

| 形式 | 特点 | 适用 |
|---|---|---|
| 命令卡 | 单条命令/查询 | 调试、探索 |
| 宏/Macro | 固定命令序列，无状态/证据 | 临时重复操作 |
| 脚本 | 命令式，难恢复/难审计 | 一次性维护 |
| Workflow | 声明式、可恢复、有门禁/证据 | 长期流程、生产运维 |
| 验收工具 | 独立判定/凭证 | P3/T2/DPLL 验收，不塞进 workflow |

## 5 流程能力矩阵

目标态均为单端口；括号内是当前过渡能力，必须按 §6.2 的迁移登记收敛。

| 流程 | 典型步骤 | transport | 恢复单位 | 人工门 | 证据 |
|---|---|---|---|---|---|
| 常规 OTA | snapshot→begin→data→end→wait→receipt→boot/commit | 单端口顺序 | chunk / END | BOOT、COMMIT | OTA receipt |
| TDMA 流式 OTA | open→data→durable wait→close→boot | 单端口 relay | durable offset | BOOT | stream receipt |
| T2 停止态 | STOP→OPMode→topology→clear→grid→deploy→template/action→counter→T2 config/mount | 单端口（当前过渡多端口） | step / table | MOUNT、START | T2 receipt |
| T2 运行 | VDC→priority→ring→cursor→PWM→sample→teardown | 单端口（当前过渡多端口） | step | START、STOP | 运行证据 |
| TDMA 启环 | bootstrap→OPMode→topology→TRAIN→ARM→START→status | 单端口（当前过渡多端口） | step / board | ARM、START | ring evidence |
| DPLL/VDC | role→tune→provisional→feedback→output→priority→lock | 单端口（当前过渡多端口） | step | RUN、RELOCK | lock/quality evidence |
| 校准训练 | BIAS→MARK→SCK→DATA→CLK→STAGE→ACTIVE | 单端口（当前过渡多端口） | stage / link | SAVE、COMMIT | 矩阵/trial evidence |
| REFMEM 装载 | stage→activate→table→quality | 单端口（当前过渡多端口） | table | ACTIVATE | table/quality evidence |

## 6 成熟度与第一版边界

| 级别 | 能力 | 当前 |
|---|---|---|
| L0 | 命令卡/宏，无状态 | 当前大部分页面 |
| L1 | 线性 workflow，step 恢复，receipt | 目标第一批（OTA），当前未达 |
| L2 | 多板/parallel/barrier，transport 切换 | T2/TDMA/DPLL |
| L3 | 版本矩阵、HIL 证据、SUPPORTED | 收口后 |

**v1 只做线性 workflow：**

- 顺序步骤、step cursor、JSON 状态、resume/cancel、receipt。
- 只允许 `command/query/wait/manual/foreach/evidence/compensate`。
- 不做 parallel，不做超过 1 层的嵌套。
- 目标态单端口 REFMEM；ring 未 ready 时 `maintenance_multiport` 是唯一合法路径，必须带 `reason=RING_NOT_READY` 并在 UI 标记过渡。
- 第一个迁移对象：常规单板 OTA；第二个：T2 停止态 + 运行。

**现行例外登记**（C1 未完成前不得把 §10 的全量 workflow 断言当已满足）：

| 流程 | 当前形态 | 不满足条款 | 迁移任务 | 失效时点 |
|---|---|---|---|---|
| 常规 OTA | `OtaPage` ad-hoc 线程 + `after` 收尾 | 无 cursor/resume；§7.12 禁止 ad-hoc 长任务 | CONSOLE-020 | OTA workflow 通过验收 |
| T2/TDMA/DPLL/校准 | 未实现 workflow | 无 step state/证据/恢复 | CONSOLE-021..024 | 各自 workflow 通过验收 |
| 多端口 bring-up | 维护脚本/显式多口 | 非单端口目标 | CONSOLE-026/027/035 | ring ready 或 bootstrap 准入 |

## 7 待定设计选择

1. 定义格式：YAML（易读、可 diff）还是 Python dataclass（类型强）？建议 YAML + 注册 action 类型，禁止 eval。
2. TCP 权限：只允许 `status`，还是允许 `start/pause/resume/cancel`？建议默认只读。
3. parallel 范围：v1 是否只允许只读并行？建议是。
4. OTA 恢复：依赖固件 durable offset，还是每次从头传？建议先从头传，固件有 journal 再续传。
5. 人工门在 headless：是否允许 `--yes` 自动通过？建议只允许非破坏性步骤，BOOT/COMMIT 必须显式 flag。
6. workflow 与多端口：transport 不匹配直接 fail-closed，不做自动降级。

## 8 运行语义

- **状态唯一事实源是 workflow state**，不是 Tk 变量、线程局部变量或日志文本。
- 状态持久化到 `out/workflows/<workflow_id>.json`：`workflow_id`、`version`、`cursor`、`inputs`、`started_at`、`last_error`、`step_results`、`evidence_hashes`。该布局是候选契约 `CONSOLE-FLOW-01`，登记前不作为外部承诺。
- 支持从最后一个成功步骤恢复；恢复前重新校验前置条件和版本，不满足则 fail-closed，不得静默从第 0 步重跑。
- 步骤必须尽量幂等；非幂等步骤（BOOT/COMMIT/CAL:SAVE）必须有明确的“已执行”证据和二次确认。
- 取消/中止必须回到安全态（STOP/ABORT/释放 lease），并记录取消点。
- 同一 workflow 只允许一个 runner；GUI、CLI、TCP 不能同时驱动同一条 workflow。
- Workflow 必须可 headless 运行；GUI 只是视图，不是执行器。

## 9 证据与版本

- 每步输出结构化结果；最终输出 receipt（复用 `CONSOLE-RECEIPT-01` 候选；登记前不作为验收判据）。
- 凭证包含 workflow id/version、固件 build、命令目录版本、板卡 ID、步骤结果、失败 reason、日志/镜像 hash。
- workflow 版本与固件 build/命令目录不匹配时 fail-closed，不允许“试一下”。

## 10 UI 约束

- 长期流程页面只显示：步骤列表、当前步骤、阻塞原因、下一步、最近一次证据。
- 不允许在页面里再写一套“开始/停止/恢复”的业务逻辑；按钮只调用 workflow runner。
- 一次性查询/单条写命令可以保留命令卡；一旦需要多步/恢复/证据，必须升级为 workflow。

## 11 必须工作流化的流程

- OTA（常规单板、TDMA 流式）
- T2 停止态配置 + 运行 + teardown
- TDMA 启环（含低速 SPI bootstrap → 高速 TDMA 交接）
- DPLL/VDC 启动、锁定、采集
- 校准训练（BIAS/MARK/SCK/DATA/CLK/STAGE）
- REFMEM 装载/激活/表/质量（多板）
- 多板验收/证据收集

## 12 禁止

- GUI-only workflow state；
- ad-hoc 线程 + 无 checkpoint 的“长任务”；
- 每个 tab/CLI 各写一份步骤定义；
- 用通用 raw SCPI 转发冒充 workflow engine；
- 失败后静默从第 0 步重跑；
- 无版本匹配检查的 resume。
