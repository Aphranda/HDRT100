# 工具域治理待办

Status: Active
Domain: Tools
Canonical: `docs/tools/DOCS_TOOLS_TODO.md`
Related: `docs/tools/DOCS_TOOLS_ARCHITECTURE.md`, `docs/tools/DOCS_TOOLS_TASK_PROGRESS.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-09-20

## 文档接口与状态规则

本表是任务状态唯一落点；治理语义见 [Architecture](DOCS_TOOLS_ARCHITECTURE.md)，事实及失败证据见 [Task Progress](DOCS_TOOLS_TASK_PROGRESS.md)。状态使用 PENDING、IN PROGRESS、BLOCKED、DONE；DONE 仅表示该任务的退出门禁完成，不表示整个治理体系完成。

## 目标与当前阶段

建立可发现、可复用、可复现、可交接的工具体系，控制重复增长和临时依赖。当前切片完成文件归属盘点、首批登记及显式静态检查；存量运行依赖复核、工作流迁移、跨机交接和 hook 接线分别验收。

## 未完成

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TOOLS-WORKFLOW-001 | 四板 DPLL/VDC 工作流依赖闭合 | IN PROGRESS | scope、输入、outer、seal、runtime control/START 与 RUN output 已完成；剩余 phase/follow/match 深层祖先仍待迁入。internal-seal 与 vector 专项分开迁移，不能把参考中断注入变成默认采样。最终移除日期目录代码依赖，保留 STOP/恢复和证据语义。 |
| TOOLS-HANDOFF-001 | 标准结果与交接复现 | PENDING | TOOLS-WORKFLOW-001；声明命令、环境、设备、版本、原始证据及失败恢复；另一工作区离线复算，对应硬件配置重跑留证。 |
| TOOLS-CHECK-001 | 工具治理静态检查 | IN PROGRESS | inventory/check/catalog 入口已落地；登记结构、路径、角色和目录漂移有正反测试。继续存量准入、配置/输出接口版本与证明内容检查，不以文件存在代替资格审查；检查过程无设备副作用。 |
| TOOLS-GATE-001 | 修复门禁触发范围与现行要求的差异 | PENDING | TOOLS-CHECK-001；独立核对工具/测试/hook staged 变更触发，正反测试及同指纹 P3 通过；不得缩减现行验收要求。 |
| TOOLS-CONSOLIDATE-001 | 分批复用、目录迁移与退役 | PENDING | 按 TG-LAYOUT，确认完成一批再迁入标准目录一批；先核清用途、依赖、调用者、恢复及源位置验证。目标位置保持稳定 ID，同步 registry、目录和调用者，旧入口兼容，相关回归、同指纹 P3 和独立复核后交付；未完成工具原位登记，不自动提升支持资格或删除旧文件。 |
| TOOLS-LEGACY-IO-001 | 清理导入即访问硬件的旧脚本 | PENDING | TOOLS-INVENTORY-001；优先处理 tdma_ring_monitor/sd_fault_read.py 的顶层串口访问、固定 COM 与旧绝对路径；改动须验证并保留旧用途，不在扫描中执行。 |
| TOOLS-RUNTIME-STATE-001 | 分离工具代码与主机运行状态 | PENDING | TOOLS-INVENTORY-001；codex_watchdog 内历史 state/heartbeat JSON 与个人路径纳入迁移，配置模板与 out 状态分离，恢复语义验证后交付，不自动删除旧文件。 |

## 已完成

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TOOLS-LAYOUT-001 | 目录规划与首批路径映射 | DONE | TG-LAYOUT 分层、稳定入口例外和首批目标已建立；scope 叶端逐文件来源/目标/调用者/兼容/验证/回滚映射完成并经独立复核，见 TOOLS-PROGRESS-20260920-003/004。后续批次仍须各自建映射。 |
| TOOLS-SCOPE-001 | 联合采样示波器叶端迁入标准目录 | DONE | 旧源码原位归档且 SHA 可核；新 reader/SparseScope 无 out 代码依赖，回归、真实 RAW 离线对照、独立复核、同指纹 P3 和 60 s 联合实测已通过，见 TOOLS-PROGRESS-20260920-004；不代表板卡工作流全部闭合。 |
| TOOLS-WORKFLOW-INPUTS-001 | internal-seal 工作流显式输入与准入 | DONE | tracked manifest 绑定完整历史可执行闭包、receipt/baseline/tap、板卡顺序及摘要；首次 import/硬件前 fail-closed，父子流程先落准入证据。回归、独立复核、同指纹 P3 和 scope-off 四板实测通过，见 TOOLS-PROGRESS-20260920-005。 |
| TOOLS-WORKFLOW-LIFECYCLE-001 | 联合采样 outer 串口与恢复生命周期迁移 | DONE | tracked lifecycle 接管四板动态端口、双 STOP、identity/build、RAM timing/delay、child、最终恢复和连接关闭；outer 不再 import 历史 adapter，报告兼容且失败不清 dirty。回归、独立复核、同指纹 P3 和 scope-off 实测通过，见 TOOLS-PROGRESS-20260920-006。 |
| TOOLS-WORKFLOW-SEAL-001 | child internal summary / quiet startup / sealed GUARD 三层迁移 | DONE | tracked factory 接管三层 Probe/parser/verdict；旧三份原件固定角色归档且不执行，runtime out closure 从 20 降到 17。characterization、四板 frozen golden、独立复核、同指纹 P3 及 scope-off/on 实测通过，见 TOOLS-PROGRESS-20260920-007。 |
| TOOLS-WORKFLOW-RUNTIME-001 | child monitor control / parallel START runtime 基座迁移 | DONE | tracked runtime base 直接加载剩余 RunProbe，接管 phase/RUN control 与 bounded parallel START；旧 monitor/startup/late-scope/Trial/HIL/reader 六份原件归档且不执行，runtime closure 从 17 降到 11。动态闭包、回归、独立复核、同指纹 P3 及 scope-off/on 实测通过，见 TOOLS-PROGRESS-20260920-008。 |
| TOOLS-WORKFLOW-RUN-001 | child RUN output 层迁移 | DONE | tracked RunProbe 接管 schema10、timing/RUN prepare、output STOP 与 STOP-only 校验；内部旧 scope owner 禁用，outer 独占恢复。旧 RunProbe 原件归档且不执行，runtime closure 从 11 降到 10；回归、golden、独立复核、P3 及 scope-off/on 通过，见 TOOLS-PROGRESS-20260920-009。 |
| TOOLS-WORKFLOW-PROBE-SUPPORT-001 | child probe support 叶端迁移 | DONE | tracked 纯 helper 接管 UID、字段解析、receipt 绑定和 native summary；旧 helper 固定为 migrated lineage 且不执行，runtime closure 从 10 降到 9。导入锁、真实 provenance、无环登记、回归、独立复核、四板 P3 及 scope-on 实测通过，见 TOOLS-PROGRESS-20260920-010。 |
| TOOLS-DOCS-001 | 建立独立 tools 文档域和标准三件套 | DONE | 三文件职责分离、总索引及域路由完整，独立复核与文档门禁通过；见 TOOLS-PROGRESS-20260920-001。 |
| TOOLS-INVENTORY-001 | 全量文件归属与静态依赖候选盘点 | DONE | 已完成文件归属、静态候选及四板 out 链专项复核；不代表全部存量运行依赖已闭合。测试、独立审查及同指纹快速 P3 已完成，见 TOOLS-PROGRESS-20260920-002。 |
| TOOLS-REGISTRY-001 | 登记结构和唯一事实源 | DONE | registry/schema、首批登记及目录同步完成；UNASSESSED 与支持资格分离，正反测试、独立审查及同指纹快速 P3 通过并保留告警，见 TOOLS-PROGRESS-20260920-002。 |

## 接续顺序与交付规则

文档建域、文件盘点、首批登记、scope、输入、outer、seal、runtime control/START、RUN output 和 probe support 已完成；TOOLS-WORKFLOW-001 下一步迁移剩余 phase/follow/match 深层祖先，专项注入保持显式配置。不要求搬完全部存量才能推进主线。示波器可用时可补 scope-on 组合验收，但无示波器切片仍优先使用 scope-off，不把设备状态改写成产品结论。四板主线优先，OTA 实现保持现有边界。任务受阻时记录具体证据和解除条件，不用扩展 P3 范围替代工具治理。

文档和实现分离提交；实现切片按现行 P3 规则验收。特等席资源变更必须先由用户审核，工具治理本身不授予新增实时资源。

旧代码只能归档，不得删除；迁移映射必须带原实现的归档路径、来源 SHA 与替代入口，不能仅以 Git 历史代替。冻结的 out 原件保持原位，清单登记历史用途和未解除的调用关系。

## 里程碑、基线与统一完成定义

已有基线是根目录工具登记、生成导航和显式静态检查；支持资格与动态依赖仍待逐组核验。里程碑依次为文档建域、工具登记、四板工作流交接闭合、检查器接线、存量分批收敛。当前无已确认外部阻塞；未实施事项保持 PENDING。

任务转 DONE 必须达到本行门禁、关联验证和失败证据完整、独立复核完成；实现变更须有同指纹 P3。整体完成还要求全量归属可查、正式依赖闭合、受影响接口兼容，以及新工作区交接可复现，不能仅凭检查器通过宣布治理完成。
