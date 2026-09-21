# 工具域治理待办

Status: Active
Domain: Tools
Canonical: `docs/tools/DOCS_TOOLS_TODO.md`
Related: `docs/tools/DOCS_TOOLS_ARCHITECTURE.md`, `docs/tools/DOCS_TOOLS_TASK_PROGRESS.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-09-21

## 文档接口与状态规则

本表是任务状态唯一落点；治理语义见 [Architecture](DOCS_TOOLS_ARCHITECTURE.md)，事实及失败证据见 [Task Progress](DOCS_TOOLS_TASK_PROGRESS.md)。状态使用 PENDING、IN PROGRESS、BLOCKED、DONE；DONE 仅表示该任务的退出门禁完成，不表示整个治理体系完成。

## 目标与当前阶段

建立可发现、可复用、可复现、可交接的工具体系，控制重复增长和临时依赖。当前切片已用版本化布局基线冻结顶层和遗留目录增长，完成 SCPI query、SD trace decode、VDC sync metrology、VDC DPLL replay、DPLL observation decode、DPLL residual analyze 和共享 SCPI 组件的分类迁移；其余存量按批核清、迁移和验收。

## 未完成

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TOOLS-CHECK-001 | 工具治理静态检查 | IN PROGRESS | inventory/check/catalog 与布局基线已落地；新增根文件、未分类一级目录、遗留根新增文件及证明空文件均有正反测试。全量 Python 基线异常已清零，状态机资源检查器同步现行 owned/TIMER1 契约并加固 C11 预处理、多候选函数/persona/board 宏判定；继续存量准入、配置/输出接口版本与证明内容检查，不以文件存在代替资格审查。 |
| TOOLS-GATE-001 | 修复门禁触发范围与现行要求的差异 | PENDING | TOOLS-CHECK-001；独立核对工具/测试/hook staged 变更触发，正反测试及同指纹 P3 通过；不得缩减现行验收要求。 |
| TOOLS-CONSOLIDATE-001 | 分批复用、目录迁移与退役 | IN PROGRESS | 已迁移项见目标段；旧实现原字节归档，兼容入口、调用者、回归、独立复核和同指纹四板 P3 已闭合，见进度 023、024、027、028。继续按 TG-LAYOUT 核清下一批；未完成工具原位登记，不自动提升支持资格或删除旧文件。 |
| TOOLS-LEGACY-IO-001 | 清理导入即访问硬件的旧脚本 | PENDING | TOOLS-INVENTORY-001；优先处理 tdma_ring_monitor/sd_fault_read.py 的顶层串口访问、固定 COM 与旧绝对路径；改动须验证并保留旧用途，不在扫描中执行。 |
| TOOLS-RUNTIME-STATE-001 | 分离工具代码与主机运行状态 | PENDING | TOOLS-INVENTORY-001；codex_watchdog 内历史 state/heartbeat JSON 与个人路径纳入迁移，配置模板与 out 状态分离，恢复语义验证后交付，不自动删除旧文件。 |

## 已完成

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TOOLS-HANDOFF-001 | 标准结果与交接复现 | DONE | TOOLS-WORKFLOW-001；已完成另一 detached worktree 的离线复算、工具 catalog 校验、自建 package/四板 OTA、带 HDO 的 P3 run/resume 及直接 joint CLI；失败与恢复证据见 TOOLS-PROGRESS-20260921-022。 |
| TOOLS-WORKFLOW-001 | 四板 DPLL/VDC 工作流依赖闭合 | DONE | internal-seal 从 scope/outer 到 base 全链已迁入 tracked 目录；V14 固定完整 lineage/输入但 runtime entrypoint 为 null、out executable closure 为空。旧源码原位保留且不执行；characterization、动态闭包、独立复核及同指纹四板 DPLL P3 通过，见 TOOLS-PROGRESS-20260920-004..20260921-021。交接复现已由 TOOLS-HANDOFF-001 单独闭合。 |
| TOOLS-LAYOUT-001 | 目录规划与首批路径映射 | DONE | TG-LAYOUT 分层、稳定入口例外和首批目标已建立；scope 叶端逐文件来源/目标/调用者/兼容/验证/回滚映射完成并经独立复核，见 TOOLS-PROGRESS-20260920-003/004。后续批次仍须各自建映射。 |
| TOOLS-SCOPE-001 | 联合采样示波器叶端迁入标准目录 | DONE | 旧源码原位归档且 SHA 可核；新 reader/SparseScope 无 out 代码依赖，回归、真实 RAW 离线对照、独立复核、同指纹 P3 和 60 s 联合实测已通过，见 TOOLS-PROGRESS-20260920-004；不代表板卡工作流全部闭合。 |
| TOOLS-WORKFLOW-INPUTS-001 | internal-seal 工作流显式输入与准入 | DONE | tracked manifest 绑定完整历史可执行闭包、receipt/baseline/tap、板卡顺序及摘要；首次 import/硬件前 fail-closed，父子流程先落准入证据。回归、独立复核、同指纹 P3 和 scope-off 四板实测通过，见 TOOLS-PROGRESS-20260920-005。 |
| TOOLS-WORKFLOW-LIFECYCLE-001 | 联合采样 outer 串口与恢复生命周期迁移 | DONE | tracked lifecycle 接管四板动态端口、双 STOP、identity/build、RAM timing/delay、child、最终恢复和连接关闭；outer 不再 import 历史 adapter，报告兼容且失败不清 dirty。回归、独立复核、同指纹 P3 和 scope-off 实测通过，见 TOOLS-PROGRESS-20260920-006。 |
| TOOLS-WORKFLOW-SEAL-001 | child internal summary / quiet startup / sealed GUARD 三层迁移 | DONE | tracked factory 接管三层 Probe/parser/verdict；旧三份原件固定角色归档且不执行，runtime out closure 从 20 降到 17。characterization、四板 frozen golden、独立复核、同指纹 P3 及 scope-off/on 实测通过，见 TOOLS-PROGRESS-20260920-007。 |
| TOOLS-WORKFLOW-RUNTIME-001 | child monitor control / parallel START runtime 基座迁移 | DONE | tracked runtime base 直接加载剩余 RunProbe，接管 phase/RUN control 与 bounded parallel START；旧 monitor/startup/late-scope/Trial/HIL/reader 六份原件归档且不执行，runtime closure 从 17 降到 11。动态闭包、回归、独立复核、同指纹 P3 及 scope-off/on 实测通过，见 TOOLS-PROGRESS-20260920-008。 |
| TOOLS-WORKFLOW-RUN-001 | child RUN output 层迁移 | DONE | tracked RunProbe 接管 schema10、timing/RUN prepare、output STOP 与 STOP-only 校验；内部旧 scope owner 禁用，outer 独占恢复。旧 RunProbe 原件归档且不执行，runtime closure 从 11 降到 10；回归、golden、独立复核、P3 及 scope-off/on 通过，见 TOOLS-PROGRESS-20260920-009。 |
| TOOLS-WORKFLOW-PROBE-SUPPORT-001 | child probe support 叶端迁移 | DONE | tracked 纯 helper 接管 UID、字段解析、receipt 绑定和 native summary；旧 helper 固定为 migrated lineage 且不执行，runtime closure 从 10 降到 9。导入锁、真实 provenance、无环登记、回归、独立复核、四板 P3 及 scope-on 实测通过，见 TOOLS-PROGRESS-20260920-010。 |
| TOOLS-P3-DPLL-SHORT-001 | P3 四板 DPLL 短闭环与相差分级 | DONE | 默认 P3 不再要求 NO5；同一组四板完成 `dpll_short_seconds` 闭环，有 HDO 时联合外部采样，无 HDO 时 external=SKIPPED，并从三从原生 summary trace 的稳态 residual extrema 分级。完整窗口、bin、成功间隔、endpoint/末 bin、分页重组及源码/build/四板/session/generation 绑定均 fail-closed；WARN/ERROR 引用实现符号，内部包络不替代 GPIO 精度。见 TOOLS-PROGRESS-20260920-011、TOOLS-PROGRESS-20260921-026。 |
| TOOLS-WORKFLOW-PHASE-001 | child phase / native trace 层迁移 | DONE | tracked phase factory 接管 phase parser、phase/origin ARM、native trace collect、endpoint consistency 与 release；失败 trace 保留、已完成 trace 有界 release。旧 phase 固定为 migrated lineage 且不执行，runtime closure 从 9 降到 8；回归、动态闭包、独立复核和四板 DPLL P3 通过，见 TOOLS-PROGRESS-20260920-012。 |
| TOOLS-WORKFLOW-LONGWINDOW-001 | child long-window policy 层迁移 | DONE | tracked factory 接管 reverse START、temporary trial、finite wait、decision window oracle 与 final projection；长区间保持 observation-only。旧 long-window 固定为 migrated lineage且不执行，runtime closure 从 8 降到 7；冻结 golden、回归、动态闭包、独立复核和四板 DPLL P3 通过，见 TOOLS-PROGRESS-20260920-013。 |
| TOOLS-WORKFLOW-TRACE-001 | child native trace 层迁移 | DONE | tracked factory 接管 status/ACK polling、follower ARM、STOP/download/decode、coverage、last-decision consistency 与 release/retain。旧 trace 固定为 migrated lineage 且不执行，runtime closure 从 7 降到 6；故障矩阵、回归、动态闭包、独立复核和四板 DPLL P3 通过，见 TOOLS-PROGRESS-20260921-014。 |
| TOOLS-WORKFLOW-FOLLOW-001 | child local follow 层迁移 | DONE | tracked factory 接管 signed/wide parser、decision oracle、follower enable/readback、STOP snapshot 与 model/DCO endpoint；单 follower 错误隔离。旧 follow 固定为 migrated lineage 且不执行，runtime closure 从 6 降到 5；冻结 golden、回归、动态闭包、独立复核和四板 DPLL P3 通过，见 TOOLS-PROGRESS-20260921-015。 |
| TOOLS-WORKFLOW-OBSERVER-001 | child observer snapshot wrapper 迁移 | DONE | tracked wrapper 接管 all-board STOP gate 与四板 snapshot-before-parent 顺序；internal-seal 不调用该深层 collect，硬件证据不虚构 snapshot 查询。旧 observer 固定为 migrated lineage 且不执行，runtime closure 从 5 降到 4；回归、动态闭包、独立复核和四板 DPLL P3 集成通过，见 TOOLS-PROGRESS-20260921-016。 |
| TOOLS-WORKFLOW-MATCH-001 | child exact-match 层迁移 | DONE | tracked factory 接管 parser、有向 delay、TAP 安装/恢复、MATCH/session 启停读回、STOP-only evidence 与逐 follower assessment；parent transport 失败和单 follower 错误独立留证。旧 match 固定为 migrated lineage 且不执行，runtime closure 从 4 降到 3；冻结 golden、故障矩阵、动态闭包、独立复核和四板 DPLL P3 集成通过，见 TOOLS-PROGRESS-20260921-018。 |
| TOOLS-WORKFLOW-DIRECT-001 | child direct-RX 层迁移 | DONE | tracked factory 接管 direct/lane/timing/budget parser、baseline 差分、retained header/mailbox 与 direct/master/polled event 一致性；CRC/payload 故障按 follower 隔离。旧 direct 固定为 migrated lineage 且不执行，runtime closure 从 3 降到 2；冻结 golden、完整性负测、动态闭包、独立复核和四板 DPLL P3 集成通过，见 TOOLS-PROGRESS-20260921-019。 |
| TOOLS-WORKFLOW-TYPED-001 | child typed-TX 与 lifecycle 层迁移 | DONE | tracked factory 接管 mailbox codec、receipt/UID/build、配置/START/TRIAL、最终 STOP、逐项恢复/revoke、best-effort close/owner 恢复；实际 backend transport registry 保持唯一对象。旧 typed 固定为 migrated lineage 且不执行，runtime closure 从 2 降到 1；golden、完整 lifecycle fault injection、fresh Probe 构造、独立复核和四板 DPLL P3 通过，见 TOOLS-PROGRESS-20260921-020。 |
| TOOLS-WORKFLOW-BASE-001 | child base probe 层迁移 | DONE | tracked base 接管 bounded command/action、STOP、load、export、pipeline 与 standalone cleanup；import 无设备副作用，三个 owner 无条件恢复。旧 base 固定为 migrated lineage 且不执行，runtime closure 从 1 降到 0；方法/fault characterization、fresh closure、独立复核和四板 DPLL P3 通过，见 TOOLS-PROGRESS-20260921-021。 |
| TOOLS-DOCS-001 | 建立独立 tools 文档域和标准三件套 | DONE | 三文件职责分离、总索引及域路由完整，独立复核与文档门禁通过；见 TOOLS-PROGRESS-20260920-001。 |
| TOOLS-INVENTORY-001 | 全量文件归属与静态依赖候选盘点 | DONE | 已完成文件归属、静态候选及四板 out 链专项复核；不代表全部存量运行依赖已闭合。测试、独立审查及同指纹快速 P3 已完成，见 TOOLS-PROGRESS-20260920-002。 |
| TOOLS-REGISTRY-001 | 登记结构和唯一事实源 | DONE | registry/schema、首批登记及目录同步完成；UNASSESSED 与支持资格分离，正反测试、独立审查及同指纹快速 P3 通过并保留告警，见 TOOLS-PROGRESS-20260920-002。 |

## 接续顺序与交付规则

四板 internal-seal 从 scope/outer 到 base 的 executable out 依赖已经清零，完整历史 lineage 与输入仍固定留证。TOOLS-HANDOFF-001 已完成。顶层布局基线已阻止遗留目录继续增长，已分类项以目标段和 registry 为准；下一步选择调用关系已核清的下一批，同时继续 TOOLS-CHECK-001 的配置/输出接口与证明内容检查。当前内部精度改进验收必须联合已枚举示波器；scope-off 只保留诊断证据，不关闭外部对比 gate。OTA 实现保持现有边界。

文档和实现分离提交；实现切片按现行 P3 规则验收。特等席资源变更必须先由用户审核，工具治理本身不授予新增实时资源。

旧代码只能归档，不得删除；迁移映射必须带原实现的归档路径、来源 SHA 与替代入口，不能仅以 Git 历史代替。冻结的 out 原件保持原位，清单登记历史用途和未解除的调用关系。

## 里程碑、基线与统一完成定义

已有基线是根目录工具登记、生成导航和显式静态检查；支持资格与动态依赖仍待逐组核验。里程碑依次为文档建域、工具登记、四板工作流交接闭合、检查器接线、存量分批收敛。当前无已确认外部阻塞；未实施事项保持 PENDING。

任务转 DONE 必须达到本行门禁、关联验证和失败证据完整、独立复核完成；实现变更须有同指纹 P3。整体完成还要求全量归属可查、正式依赖闭合、受影响接口兼容，以及新工作区交接可复现，不能仅凭检查器通过宣布治理完成。
