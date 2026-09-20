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
| TOOLS-WORKFLOW-001 | 四板 DPLL/VDC 工作流依赖闭合 | PENDING | 前两项；按已复核的 internal-seal 基础链和 vector 专项分开迁移，不能把参考中断注入变成默认采样。移除日期目录代码依赖，历史基线/设备参数显式输入，保留 STOP/恢复和证据语义。 |
| TOOLS-HANDOFF-001 | 标准结果与交接复现 | PENDING | TOOLS-WORKFLOW-001；声明命令、环境、设备、版本、原始证据及失败恢复；另一工作区离线复算，对应硬件配置重跑留证。 |
| TOOLS-CHECK-001 | 工具治理静态检查 | IN PROGRESS | inventory/check/catalog 入口已落地；登记结构、路径、角色和目录漂移有正反测试。继续存量准入、配置/输出接口版本与证明内容检查，不以文件存在代替资格审查；检查过程无设备副作用。 |
| TOOLS-GATE-001 | 修复门禁触发范围与现行要求的差异 | PENDING | TOOLS-CHECK-001；独立核对工具/测试/hook staged 变更触发，正反测试及同指纹 P3 通过；不得缩减现行验收要求。 |
| TOOLS-CONSOLIDATE-001 | 分批复用、迁移与退役 | PENDING | 登记及相应工作流验证；列调用者与替代路径，每批回归、保留兼容和失败证据，不自动删除旧文件。 |
| TOOLS-LEGACY-IO-001 | 清理导入即访问硬件的旧脚本 | PENDING | TOOLS-INVENTORY-001；优先处理 tdma_ring_monitor/sd_fault_read.py 的顶层串口访问、固定 COM 与旧绝对路径；改动须验证并保留旧用途，不在扫描中执行。 |
| TOOLS-RUNTIME-STATE-001 | 分离工具代码与主机运行状态 | PENDING | TOOLS-INVENTORY-001；codex_watchdog 内历史 state/heartbeat JSON 与个人路径纳入迁移，配置模板与 out 状态分离，恢复语义验证后交付，不自动删除旧文件。 |

## 已完成

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TOOLS-DOCS-001 | 建立独立 tools 文档域和标准三件套 | DONE | 三文件职责分离、总索引及域路由完整，独立复核与文档门禁通过；见 TOOLS-PROGRESS-20260920-001。 |
| TOOLS-INVENTORY-001 | 全量文件归属与静态依赖候选盘点 | DONE | 已完成文件归属、静态候选及四板 out 链专项复核；不代表全部存量运行依赖已闭合。测试、独立审查及同指纹快速 P3 已完成，见 TOOLS-PROGRESS-20260920-002。 |
| TOOLS-REGISTRY-001 | 登记结构和唯一事实源 | DONE | registry/schema、首批登记及目录同步完成；UNASSESSED 与支持资格分离，正反测试、独立审查及同指纹快速 P3 通过并保留告警，见 TOOLS-PROGRESS-20260920-002。 |

## 接续顺序与交付规则

文档建域、文件盘点和首批登记已完成，下一步推进 TOOLS-WORKFLOW-001；先迁移叶端解码与采集接口，再收敛 Probe/Trial 和基础 adapter，专项注入保持显式配置。四板主线优先，OTA 实现保持现有边界。任务受阻时记录具体证据和解除条件，不用扩展 P3 范围替代工具治理。

文档和实现分离提交；实现切片按现行 P3 规则验收。特等席资源变更必须先由用户审核，工具治理本身不授予新增实时资源。

## 里程碑、基线与统一完成定义

已有基线是根目录工具登记、生成导航和显式静态检查；支持资格与动态依赖仍待逐组核验。里程碑依次为文档建域、工具登记、四板工作流交接闭合、检查器接线、存量分批收敛。当前无已确认外部阻塞；未实施事项保持 PENDING。

任务转 DONE 必须达到本行门禁、关联验证和失败证据完整、独立复核完成；实现变更须有同指纹 P3。整体完成还要求全量归属可查、正式依赖闭合、受影响接口兼容，以及新工作区交接可复现，不能仅凭检查器通过宣布治理完成。
