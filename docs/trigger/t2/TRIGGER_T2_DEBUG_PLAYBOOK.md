# TRIGGER T2 调试手册

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_DEBUG_PLAYBOOK.md`
Related: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-10-25

> 本手册只记录**可复用的调试方法**与**踩过的坑**（症状 → 根因 → 修法 → 证据）。
> 具体读数与轮次见 `TRIGGER_T2_TASK_PROGRESS.md`；时序/契约见 `TRIGGER_T2_ARCHITECTURE.md`。

## 1. 测量纪律（先保证"看到的是真的"）

1. **别相信缓存产物**：`flash_link_contract=OK` 可能来自复用的旧 ELF。
   任何"绿"都要**先删 ELF 强制重链接**再确认（本夜两次因此误判）。
2. **采样必须在 teardown 之前**：链路结束会自动 teardown，之后再读板子全是 0。
   工作流在采样循环末尾把页面落盘到 `out/sync_trigger/sequence_snapshot.json`。
3. **每个页面单独读、单独判成败**：一次 `try` 里连发五页，会把超时存成"读数"。
   快照现在**逐页读取 + 一次重试 + 记录失败页与字段数**。
4. **页面字段按发射顺序建索引表**，不要记"尾 N 字段"。
   本夜两处登记（`T2-DEPLOY-003` 等）就是因为"尾 7 字段"而失效——
   `T2:ACTion?` 的窗口七项在**预测器 9 项之后**（`event_count=9` 时索引 45..51）。
5. **判据用不变量，不用魔数**：`executed = 组数 × finish`；位置数用 `executed / 组数` 反推。
   `finish` 会跨圈累计/重置，**不能单独当位置数**。

## 2. 失败定位的固定顺序（从外到内）

```
① 位置层：entered → announced → frozen → executed 是否逐级相等（不等就查窗口/预测）
② 后端层：identity / terminal / arm 三类失败各多少（运行期判的）
③ 执行器层：stage 的 8 个拒绝桶 / arm 的 4 个拒绝桶 / svc_wait / complete / plan_next
④ 硬件层：SM 使能、PC、TX FIFO 电平、pending
```

每一步都先加**计数**再动手改代码；没有计数就不猜。

## 3. 踩过的坑（症状 → 根因 → 修法）

| 症状 | 根因 | 修法 |
|---|---|---|
| 窗口最后一组完成"看不见"，位置卡在两发 | `poll()` 换 head 时先置 COMPLETE、补装后**不重指** status | 补装后重指（ARMED + 新 head 的 deadline/sequence、`receipt_mask=0`） |
| 相邻窗口序号重叠 | `stage_sequence` 固定 +2，语义应是"最后发出的序号" | `stage_sequence = base + count − 1` |
| 同一 COMPLETE 被消费两次 | 先用"sequence 前进"再落 `else if (complete)` | 只按 `completed_sequence` 前进一步，且该分支 `return` |
| 位置在窗口末尾判 LATE | 窗口内只推进 `group`，不推进 `action_q`/`deadline_tick` | 窗口内同步累加 delta 与 tick |
| 第三发之后再也不动 | 网关 `service` 完成一组才装下一组；`prepare_realtime` 每次**清 FIFO/重启 SM** | 见第 5 节的预装设计 |
| 网关每位置一次 BACKEND | `GATEWAY_PLAN_MAX`/`SYNC_IO_SEQUENCE_PLAN_MAX_GROUPS` 停在 16，与 18 的窗口不一致 | **四个上限常量必须同改** |
| 网关长跑零星 BACKEND | BUSY 被当成位置失败 | `plan_pending()` + 运行期跳过本轮（延后，不失败） |
| `w_executed` 比真实位置数大 | 槽位被接管时"FROZEN 就算 EXECUTED" | 只有 `emitted_position_mask` 置位才记 EXECUTED，否则 ABANDONED |
| 迟到组直接失败 | `start_deadline` 把"已过期/进 guard"当拒绝 | 先钳位到 `now+guard` 再判范围；计 `late` |
| 2 组窗口计数只有 1/3 | `completed_sequence` 是**电平**（最近完成组），被当**事件**用 | 以 `sequence > expected + 回执` 为事件；电平分支只作最后一组的兜底 |
| 加一行代码就 `RAM overflowed` | 余量只有几十字节 | 先量 `nm --print-size --size-sort`；从 `configTOTAL_HEAP_SIZE` 等整块里腾 |

## 4. 决策规则（避免"改一天又改回去"）

1. **一次只改一项**，每项都上台架；**验证失败立即回退**，并在进展日志里写清"为什么不成立"。
2. **改常量类需求（每位置动作数、窗口尺寸）必须一次找齐所有上限**：
   表容量 / 运行期窗口 / 网关 plan / 计划构建。
3. **回执与身份**：执行器只报告"推进"，身份由动作侧（组号）决定；两者混用必出双计数或漏计数。
4. **判据要容忍采样边界**：`|executed − 组数 × 位置数| ≤ 组数`（圈起点不由采样点决定）。
5. **不要用断言把契约写给实现看**：页面断言按"实际布局"重建，并在提交信息里写明契约变更。

## 5. 工具与配方

| 目的 | 命令 |
|---|---|
| 序列步进（0 环境 / 1 配置 / 2 连续链 / 3 读数 / 4 判据 / 5 HTML） | `python tools/sync_trigger/sequence_workflow.py --step N` |
| 分圈判定 | `sequence_workflow.py --steps 3-5 --positions 361 [--groups 16]` |
| 单步台架 | `t2_workflow.py --steps 0-20`（连续性必须保持，中途不改源码） |
| P3 | 源码冻结后 `hardware_acceptance_workflow.py --profile full`；或 `--run <dir> --steps a-b` 续跑 |
| 主机回归 | `pytest -k "trigger or sync_io or t2_"`；网关连打三发：`test_sync_io_gateway.py -k triple` |
| 页面索引 | 读 `scpi_trigger_t2_runtime_commands.c` 的 `SCPI_Result*` 顺序建表 |

## 6. 预装两组（网关下一步的精确设计）

`prepare_realtime()` 每次都会关 SM、**清 FIFO**、重启，并把脉宽写进 Y、把 `trigger_count+1` 写进 ISR——
这就是"FIFO 里永远只有一发"的直接原因。要两发在飞，最小改法是：

```text
pull block        ; 字 A = 延时
out x, 32
pull block        ; 字 B = 本组序号（身份）
out isr, 32
delay: jmp x-- delay
irq 6
set pins, 1
high:  jmp y-- high     ; 脉宽仍由 Y 每位置预置一次
set pins, 0
push block              ; 推 ISR = 本组序号
```

- 每触发两个字，FIFO 深度 4 ⇒ 两发在飞；第二组延时 = `(d2 − d1) − 脉宽 − 4`；
- `push` 出来的序号自带身份，不需要软件环；
- 验收：整圈位置数 ≥ 300、`late` → 0、`executed = 组数 × finish` 保持、三类失败 0。
