# 核验提交单：单口引导期校准流程（CALIBRATION）→ HAOFV / TDMA / REFMEM

Status: Active
Domain: Calibration
Canonical: `docs/check/submissions/CALIBRATION_GUIDE_CROSS_REVIEW_01.md`
Related: `docs/calibration/CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-11-05

## 提交内容

v1 方案（四段 C/T/R/Q：单口经 10 kHz 环写配置、触发、四板各自运行、最后查询）提交两份
独立只读审查：HAOFV owner/实时 与 控制面/契约。两份均判 `REJECT`。

| 父层条款 | v1 符合性 | 证据 |
|---|---|---|
| CP2 执行必在目标 | ❌ | T 的"广播方置 pending"由发起方写目标事实 |
| CP3 门禁不降级 / Flash lockout | ❌ | C 段边跑边写 flash，绕过 idle 窗口 |
| CP5 目标显式 | ❌ | "一条广播"为隐式广播，零掩码被拒 |
| CP7 至多一次 | ❌ | 无 `(boot_id, seq, target)` 去重 |
| CP8 接受 ≠ 完成 | ❌ | "接受即返回"后立刻停环，ACK 无机会聚合 |
| TDMA 低速档禁止 commit | ❌ | C 段每板持久化违反该条 |
| 训练 owner 分工 | ⚠️ | R 段的 Δ/inject 落在 core0，等于 host 注入实时时序 |

## 偏差声明

- `CAL-GUIDE`（候选，未登记）：v1 的"配置代际"取消，改为复用既有
  `calibration_generation/topology_generation/profile_crc32/schedule_crc32`；接受理由=避免第二套
  代际命名空间与第二 writer。
- 恢复语义：不把"末尾强制恢复 10 kHz"当命令，改为持久化开机引导档 + 定序自组；接受理由=
  `RING:TOPOLOGY` 等命令运行期 `NOT_ONLINE_SAFE`，环停后无通道。
- 单口产品化：以"单口必接协调者板、C4/C5 为前置"为前提；接受理由=非协调者远端入口仍被拒。

## Alternatives considered

- 方案A 运行期直接写 flash 持久化配置（v1）：拒绝，环停即断链，且违反低速禁止 commit。
- 方案B 用隐式广播触发（v1）：拒绝，违反 CP5/CP2/CP8/CP7。
- 方案C 停止后靠命令恢复 10 kHz（v1）：拒绝，恢复命令运行期不可用，失败不可恢复。
- 方案D（v2，接受）：C 只 RAM staging；T 为目录内命令 + 显式掩码 + 去重；R 由板端 core1
  定序；S 段做 staging/ARM gate；Q 读 typed mirror；持久化走 `REFTMEM-IDLE-01`。

## 核验结论

- 结论: **REJECT（v1）**；**REJECT（v2）**。v2 只完成了"设计层"修订，**未实现、未取证**；
  因此 v1 的 BLOCKER 不得记为闭环，登记与 C11 均未开始。
- 核验人: 主控会话（提交方）

## 发现与处置（合并两份审查，去重）

**处置列只表示"设计层改法"，不表示已闭环。** 实现状态见 playbook §5；R2-B3/B4/B5、R2-M1/M2
以及 B2/B7/B8/B9、N1–N3 仍是未实现前置，BLOCKER 状态保留。

| ID | 严重度 | 发现 | 设计层处置 | 实现状态 |
|---|---|---|---|---|
| R1-B1 | BLOCKER | C 段边跑边写 flash：写 flash 需 core1 park + lockout，环随之中断（自指死锁） | 拆为"环上 RAM staging + STOP 本地 commit"；低速不 commit（§2.1） | 设计改法；"STOP 窗口 commit 是否属低速会话"未定 |
| R1-B2 | BLOCKER | 恢复失败不可恢复；默认档是 10 MHz，无 stage 时 ARM 被拒 | 持久化开机 10 kHz 引导档 + 定序自组（§2.3） | **未实现**（无持久化档、无自动 ARM、无 quorum） |
| R1-B3 | BLOCKER | Δ ≥ 300 ms 无派生下界，也无"四板已 ARM"依据 | Δ 逐节点派生 + ARM 窗口（§2.4） | Δ 输入未实测；Q 缺 arm 时刻/超窗 |
| R2-B1 | BLOCKER | T 为隐式广播，违反 CP5 | 显式掩码 + SlotClaimMap 解析（§2.2） | 字段存在；解析/记录未实现 |
| R2-B2 | BLOCKER | T 写目标事实（CP2）；接受即返回后停环，ACK 无聚合（CP8） | 目标自置 pending；required 全 ACK 才停环（§2.2） | 无 handler；"全 ACK 才停环"无消费者 |
| R2-B3 | BLOCKER | 目录缺 RING/OPMode/CAL 全部条目 | 4 行 / 9 条待建目录（§3） | **零匹配，未建** |
| R2-B4 | BLOCKER | `param_words ≤ 16`、全 inline；CAL LINK 18+ 无法承载 | `ref` 载荷或板端本地生成（§3） | **未实现**；`TLV_REF` 无消费者 |
| R2-B5 | BLOCKER | 无 `(boot_id, seq, target)` 去重；`idempotent` 未消费 | 三元组去重（§2.2） | 无 `boot_id`；`idempotent` 未消费 |
| R2-B6 | BLOCKER | C 段持久化绕过 idle 窗口；代际当权威与 PERSIST-01 冲突 | 走 `REFTMEM-IDLE-01`；不新增代际（§2.1） | 设计；引导档落点需改 FlashMap |
| R1-M1 | MAJOR | 新造"配置代际"形成第二套命名空间 | 取消，复用既有身份链（§2.1） | 设计 |
| R1-M2 | MAJOR | Q 缺 §6 逐 trial 身份与 `age/stale` | 绑定身份 + 逐 trial 字段（§2.5） | 未实现 |
| R1-M3 | MAJOR | Δ 等待与 inject 若落 core0 = host 注入实时时序 | 改 core1 定时/状态机（§2.4） | core1 API 已有；未接线 |
| R1-M4 | MAJOR | `T_trial` 内落盘无上界；core1 不得写 SD | `T_trial` ≥ 锁存 + 落盘上界（§5） | 未实测 |
| R1-M5 | MAJOR | 56 B wire 不内联大数组，静态表无法整表下发 | 分段 + CRC + `ref`（§3） | 未实现；若走 wire 需改 §1 边界 |
| R1-M6 | MAJOR | 单口依赖"协调板发起、远端执行"，该形态未产品化 | 前置门禁：单口必接协调者板（§5） | C4/C5 未闭合 |
| R1-M7 | MAJOR | "不自动重试"与维护流 3 次重试冲突 | 重传回既有终态；训练重试新 epoch（§2.2） | 未实现 |
| R1-M8 | MAJOR | 缺把结果写成 per-link staging 并过 ARM 门禁的段 | 新增 S 段（§2.6） | 未实现 |
| R2-M1 | MAJOR | Q 无 typed mirror 行；CAL 区是不透明 buffer | typed 行为 Q 前置（§5） | 未实现 |
| R2-M2 | MAJOR | `allowed_states` 等三字段执行侧不消费 | 列为前置（§3） | 未实现 |
| R2-M3 | MAJOR | 域归属重叠；与"配置期间不得停环"冲突 | 编排归 CONSOLE、命令面归 REFMEM、环归 TDMA（§1、§2.0） | 维护窗口豁免无条款依据 |
| R2-M4 | MAJOR | 非协调者板无跨节点发号 | 单口必接协调者板（§5） |
| R2-M5 | MAJOR | 恢复 10 kHz 不是可发命令 | 改为本地自组（§2.3） |
| R1-m1 | MINOR | 10 kHz 打破"线时 ≤ 相位 WCET"断言，未实测 | 列入待补证据（§5） |
| R1-m2 | MINOR | 10 kHz 捕获窗只看得到电平，不能声称波形 | 10 kHz 只给计数/状态（§2.5） |

## 交叉审核记录（C11）

- 审核方: 两份独立只读 agent（HAOFV owner/实时；控制面/契约），均非本文作者
- 审核方式: agent 交叉，只读仓库，未修改任何文件
- 审核结论: `REJECT`（v1）；v2 复审仍 `REJECT`
- 审核日期: 2026-11-05
- 未取得证据的条目（如实保留）: 10 kHz 作为"引导期唯一命令载体"的 TDMA 条款原文；
  ring status 进入 REFMEM mirror 的读数面；`CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md` §8/§10
  的逐行门禁条文。上述三项在复审前不得当作已成立事实引用。

## v2 复审（同日，只读 agent）

总判 `REJECT`。B1–B9：CLOSED 3 项（B1/B4/B5，设计层）、PARTIAL 3 项（B3/B6/B9）、
OPEN 3 项（B2/B7/B8）。

| 项 | 判定 | 证据与缺口 |
|---|---|---|
| B1 | CLOSED（设计层） | 拆分与 `REFTMEM-IDLE-01` 一致；"STOP 窗口 commit 是否属低速会话"仍未定义 |
| B2 | **OPEN** | 只有 RAM 内 bring-up 标志，无持久化引导档、无上电自组、无自动 ARM；boot 按编译期档位，ARM 仍显式本地调用 |
| B3 | PARTIAL（round 5 进展） | core1 intent/arm/inject 已有；`Δ` 已按符号派生（§9.5：174 B/139.2 ms wire、533 µs guard、600 ms 圈、T 单 chunk、Δ 下限 2 s），但输入 WCET 未实测、`Δ_arm` 取值待实测；Q 字段已补 `delta_ms/arm_window_ms/arm_cycle/arm_latency_ms/over_window` |
| B4 | CLOSED（设计层） | 显式掩码；零掩码/非子集在 `refmem_control_submit.c` 已被拒 |
| B5 | CLOSED（设计层） | 目标自置 pending；required 全覆盖才终态（`distributed_refmem_cmd_reply.c`） |
| B6 | PARTIAL | 目录仍 37 行全 inline，无 RING/OPMode/CAL_GUIDE 行；三字段执行侧仍不消费；BRINGup/ARM 只是 SCPI/service 调用 |
| B7 | PARTIAL（round 3 修正） | 池已存在（`refmem_param_pool.h`：3×1024 B、REF+slot、seq/epoch/size/CRC 校验、host 用例）；但池在 `distributed_refmem_table.tlv`，而 `REFTMEM_VECTOR_REGION_TLV` 零消费者 → 池是板内的，跨板 REF 不成立；56 B wire 不带 payload |
| B8 | PARTIAL | 无 `boot_id`（零命中）；`command_seq` 由 `osal_tick_ms()` 播种，跨重启不唯一 |
| B9 | PARTIAL | "不改 FlashMap"不成立：前 4 扇区已被 cal store + seat mount 占满且有 static assert；compat map 仅 2 扇区 |

新增 BLOCKER：

| ID | 问题 | 证据 |
|---|---|---|
| N1 | 自组无可触发的 ARM/START；上电无自动 ARM、无持久化档；"半环拒绝"本地不可判定（无 quorum/超时规则） | `tdma_runtime_owner.c` bring-up ARM 需 flight map；`scpi_tdma_snapshot_commands.c` 显式调用 |
| N2 | S 段"只在 STOP 下执行"没有命令通道；与 §2.3 否掉"末尾发命令恢复"是同一条论证，plan 自相矛盾 | playbook §2.6 vs §2.3 |
| N3 | 10 kHz 载体被当作既有事实；TDMA 架构仍判"当前软件不可行"，L1–L4 是计划行不是验收项 | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md` 低速档节 |

round 2 复核（主控，读代码 + 证据目录，见 playbook §9.3）：N3 的"文档判不可行"属实，但那是
L1–L4 实现前的结论——四处硬门槛与免标定 ARM 均已在代码落地，四板 10K HIL
（`out/tdma_lowrate/wf-10k-run3`、`ring-10k-run7`、`wf-10k-pi2`，均 `passed=true`）已通过。
故 N3 由 "OPEN" 降为 **MITIGATED**：引导期功能可用；残留是 600 s soak 与 TDMA 文档待刷新，
不再阻塞 M0。

处置：N1–N3 与 B2/B7/B9 必须先在 playbook 修正并取得证据，才允许进入 M0 实现；
v2 不登记任何条款，目标 `M0` 记为**被阻塞**，不是"待做"。

## v2 控制面/契约复核（同日，第二份只读 agent）

总判 `REJECT`（作为可交 C11 的提交包）。已修：登记事实、处置过度声称、CP5 解析、CP1 引用、
域归属、维护窗口依据、ref 与 wire 边界。

| ID | 严重度 | 问题 | 处置 |
|---|---|---|---|
| B-1 | BLOCKER | 称 `CONSOLE-FLOW-01` "已在 CONSOLE 架构登记"，实际是未登记候选（`CONSOLE_ARCHITECTURE.md`"以下契约尚未登记"；registry 无 `CONSOLE-*` 行） | 已改为"候选、未登记、依赖 C11" |
| B-2 | BLOCKER | 提交单称"v2 已按全部 BLOCKER 修订"，处置列全标闭环 | 已加"实现状态"列；BLOCKER 不记闭环 |
| M-1 | MAJOR | 目录行/handler 不存在；"7 类"与 §3 的 4 行不符 | 已改"4 行 / 9 条待建"，并注零匹配 |
| M-2 | MAJOR | CP5 只满足字段，未满足 `SlotClaimMap` 解析并随命令记录 | 已补 §2.2 |
| M-3 | MAJOR | CP1 例外未引用 | 已补 §1（非协调者不中继） |
| M-4 | MAJOR | 段顺序与 `CONSOLE-FLOW-01` 双源 | 已注"步骤 ID，编排归候选契约" |
| M-5 | MAJOR | 维护窗口豁免无条款依据 | 已注"需在扩展节补豁免或引低速档条文" |
| M-6 | MAJOR | `ref`/`TLV_REF` 与"不新增 payload class"可能冲突 | 已注"若走 wire 分段即新 wire/布局，先改 §1 或走 ARCH 增量" |

CP 逐条：CP2/CP7 未找到实现证据（无目录/handler、无 `boot_id`）；CP5 字段层满足、解析层未实现；
CP8 聚合有通用实现但"全 ACK 才停环"无消费者；CP3/CP4/CP6/CP9 未见新违反。
Draft 不作 clause_loc、C11 前不登记、复用 `REFMEM-CTRL-01` 不新开 id 三项合规。
