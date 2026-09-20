# 工具域治理任务进度

Status: Active
Domain: Tools
Canonical: `docs/tools/DOCS_TOOLS_TASK_PROGRESS.md`
Related: `docs/tools/DOCS_TOOLS_ARCHITECTURE.md`, `docs/tools/DOCS_TOOLS_TODO.md`, `docs/check/DOCS_REGRESSION_PLAN.md`
Last updated: 2026-09-20

## 文档接口

本文只记录实施与验证事实，任务状态以 TODO 为准，治理语义以 Architecture 为准。每条记录引用稳定任务 ID，使用唯一 `TOOLS-PROGRESS-YYYYMMDD-NNN`，最新记录在上；轮转遵循文档自回归 C14/C15，历史失败和原始证据不得丢失。

## 当前 checkpoint

### TOOLS-PROGRESS-20260920-004：示波器叶端首批迁入标准目录

- TODO task ID：TOOLS-LAYOUT-001、TOOLS-WORKFLOW-001、TOOLS-SCOPE-001。
- 范围：联合采样当前 CLI 保持原路径；显式导入 `tools/common/hdo_scope/reader.py` 和 `tools/domains/vdc/scope_capture/sparse.py`，替换示波器运行路径的外置类继承与 monkey patch。板卡 adapter/Probe/Trial、START/STOP/恢复逻辑不迁移，本批不宣称 out 执行链全部解除。
- 用户追加要求：旧代码只归档、不删除。四份历史源文件原位保留，来源 SHA、迁移符号、目标与回滚方式登记在 `tools/domains/vdc/scope_capture/migration.json`；仍被旧板卡链导入的历史脚本不标为完全退役。

| 旧来源（均在 out/HardwareAcceptance/ 下原位保留） | 迁移符号 | 目标 | 调用/兼容和回滚 |
|---|---|---|---|
| 20260916/dpll-scope-hold-r1/capture_scope_r3.py | Scope / decode_block | tools/common/hdo_scope/reader.py | joint 改用显式 reader；旧源码及旧调用方保留，回滚本批 caller/registry 需重新验收。 |
| 20260918/dpll-phase-center-r1/capture_late_scope_schema10.py | ConfigurableScope | tools/domains/vdc/scope_capture/sparse.py | 保留 PRE 范围归一和内存配置行为；原 ResumeTrial 不迁移。 |
| 20260918/dpll-extended-window-r1/capture_startup_one_second.py | LongScope.write | 同上 | 保留 MAIN/OPC 顺序，取消模块 monkey patch；parallel_start 仍由旧板卡链承担。 |
| 20260919/dpll-checkpoint-r2/monitor_external.py | SparseScope | 同上 | 保留 RAW 缓存重置和 runtime WAV setter 等待；joint 的负阈值 fresh WAIT/STOP 和异步落盘保持原语义。 |

- 新增 `--scope-resource` / `--scope-serial`，传递到子进程并记录生效配置；默认仍为既有台架。新模块独立登记 EXPERIMENTAL，完整工作流保持 UNASSESSED，不自动授予跨设备/跨机支持。Scope 的 Python assert 改为显式错误，失败命令/坏块保留，关闭资源用 finally；这些差异由回归覆盖。
- 软件验证快照，非事实源：最终组合回归 242 passed / 1 skipped，跳过项为当前 Windows 账户无符号链接权限；真实离线对照 12 窗口 / 48 RAW 块、31,200,576 bytes，结果与原 phase.json 精确一致。原件来自 `out/HardwareAcceptance/20260920/joint-60s-r3/capture/`，新解码器不导入原脚本；新模块在无 out 的隔离目录可导入且不打开设备。文档回归 38 passed；四份旧源 SHA 全部一致。
- 审查失败与修复：初审发现 Scope 打开 VISA 后设置 timeout/chunk_size 失败会泄漏句柄；已将打开/设置纳入同一异常清理边界，分别关闭已得句柄，清理失败不替换原始异常。后续独立复核发现序列号曾在整个 IDN 字段集合中匹配，型号也可误通过；已改为精确比较 `*IDN?` 第三字段，并补厂商/型号/固件字段负测。修复后交回复核；两轮 CHANGES_REQUESTED 与最终结论均保留，不更改采样时序。
- 硬件验收快照，非事实源：最终源码指纹见 `config/hardware_acceptance/p3_acceptance_receipt.json`；build `20260920072724` 的快速四板 P3 为 PASS_WITH_WARNINGS（INFO 24 / WARN 21 / ERROR 0 / FATAL 0），P0/P3/T3 通过，TDMA 带告警通过，DPLL 按四板范围 SKIPPED。原件在 `out/HardwareAcceptance/20260920/tools-scope-leaf-r1/p3-r3/`，`check-staged` 已核对同指纹。
- 真实联合采样快照，非事实源：`joint-60s-r3` 外层及 joint verdict 均通过，12 个新鲜窗口和 48 个 RAW 块完整，11 个稳态窗口与首个收敛窗口均在 50 ns 带内；最大绝对相差为 37.596 ns。RUN 中板端查询为零，四板内部健康、STOP/恢复、异步落盘和示波器关闭完成，errors / cleanup_errors / restore_needed 均为空；不授予未采区间连续锁相或精确事件对应资格。原件在 `out/HardwareAcceptance/20260920/tools-scope-leaf-r1/joint-60s-r3/`。
- 失败证据：`joint-60s-r1` 因 HDO 未枚举而在 Scope 构造时拒绝，板卡完成 STOP/恢复，原目录保留；序列号修复后首次重跑因源码与旧 P3 凭证不匹配，在外层 preflight、输出目录创建和硬件运行前 fail-closed，因此没有 `joint-60s-r2` 硬件原件，本 checkpoint 只保留拒绝事实，不声称存在原始目录；错误选择链式复用摘要的 P3 尝试在硬件操作前拒绝，结构化拒绝原件保留在 `p3-r2/`。这些失败均未改写为通过。
- 工具治理证据目录 `out/tools-governance/20260920/scope-leaf-r1/` 包括软件回归、离线 golden、来源 SHA 和独立审查；TOOLS-LAYOUT-001 与 TOOLS-SCOPE-001 据此关闭。下一步仍是 TOOLS-WORKFLOW-001 的板卡生命周期与显式历史输入收敛，不能仅移动最外 adapter 来宣布全链迁移完成。

### TOOLS-PROGRESS-20260920-003：目录分层与迁移边界规划

- TODO task ID：TOOLS-LAYOUT-001；与 WORKFLOW、CONSOLIDATE 联动。
- 根据当前登记入口补充 TG-LAYOUT：业务域、公共能力、跨域工作流、构建、主机辅助分层；稳定验收/门禁/测试启动器原位保留，配置、测试和运行产物分别路由。DPLL 继续归 VDC，工具 ID 不随路径变化。
- 用户进一步要求“逐步将确认完成的工具移动至标准规划目录”；已补源位置准入、迁移、目标位置验证的顺序。未完成工具原位登记，已登记不等于已完成，目录迁入不自动授予 SUPPORTED。
- 给出联合采集、计量、示波器、TDMA、校准、SCPI、构建与主机辅助的首批目标方向；强调逐文件调用者映射、兼容包装、显式包入口、依赖方向与同指纹验收。OTA 保持当前边界，历史证据不搬动，未执行目录迁移或新建空目录。
- LAYOUT 保持 IN PROGRESS：总体规划已写入架构，首批逐文件旧/新路径、调用者、验证和回滚映射仍待实施前完成；不阻塞无关主线，不将所有存量搬迁作为前置条件。
- 本轮仅修改 tools 域三份文档，不改工具实现/登记/固件或特等席；文档门禁与独立复核结果见 `out/doc-audit/tools-layout-20260920/`。不产生新的硬件验收声明。
- 验证：独立审查 PASS；严格命名、文档自回归、检查器回归（38 passed，快照）、手工 hook、log-check、check-staged 通过。按审查建议明确登记/导航须在同指纹验收前同步。

### TOOLS-PROGRESS-20260920-002：全量登记与静态检查首批落地

- TODO task ID：TOOLS-INVENTORY-001、TOOLS-REGISTRY-001；TOOLS-CHECK-001 继续分阶段实施。
- 已建立根目录 `tools/tool_registry.json`、配套 schema 和 `tool_governance` 入口；文件归属精确到路径，工具组 ID 与显式 entrypoints 分离。根目录兼容包装归目标组，未移动原入口。`tools/README.md` 增加生成目录和治理导航。
- 盘点快照，非事实源：原有 263 个工具文件，含本轮设施后 268 个，登记为 98 个工具组；角色为 163 个入口、29 个模块、5 个兼容包装、53 个测试运行器、14 个资源、4 个说明文件。97 组为 UNASSESSED，新检查器为 EXPERIMENTAL；未把已登记升级为 SUPPORTED。静态检查的 194 条告警表示存量资格和依赖待审，不是硬件错误。
- inventory 用 Git 文件集合、字节哈希、Python AST 和位置线索盘点，不 import 被审工具；候选依赖不是完整执行图，out 输出路径也不自动算违规。check 校验 schema、文件归属、路径与入口、声明依赖、别名及证明引用；catalog 与 README 生成块一致性可显式检查，尚未接入 hook。
- 独立四板工作流盘点：最新实测基础 adapter 是 internal-seal，基础链依赖 20 个 out Python 文件，vector 专项额外 10 个；保守依赖图 83 节点/126 关系。vector 含 B 臂外参及参考中断注入，不能直接作为普通采样默认项。历史基线属于显式证据输入，源码、设备配置与证据须分别迁移，见 `workflow-audit.json` / `review-notes.txt`。
- 重复项复核：scope_dpll_capture、vdc_long_monitor、vdc_sync_metrology 分别是示波器快照、主动双板自检和离线计量，不能凭名称合并。未发现相同字节文件组；兼容包装有已确认转发关系，不当作重复实现删除。sd_fault_read 顶层串口访问/固定路径及 codex_watchdog 历史运行状态分别纳入专门待办。
- 失败与修复：初审复现文本末尾换行破坏目录、错误 schema 导致 KeyError、别名自环漏检；均修复，保留初审及最终 PASS 于 `registry-review.txt`。同时修正相对导入候选和二进制资源处理。Windows 首次依赖安装的 CLI launcher 写入失败，包导入可用；随后 `pip install --user -r ...` 返回成功，见 `dependencies-install.log`。首次 inventory 管道遇默认 GBK 输出，CLI 已显式输出 UTF-8。
- 软件验证快照，非事实源：静态登记和目录一致性通过；功能回归 95 passed / 1 skipped，跳过项为 Windows 无符号链接创建权限；文档回归 38 passed。日志见 `out/tools-governance/20260920/inventory-r1/tests-r4.log`、`docs-tests.log`，独立审查最终 PASS。
- P3 首轮失败：`out/HardwareAcceptance/20260920/tools-registry-r1/p3-r1/` 完成构建和四板 OTA，但 NO1 START 超时、记录样本为零，从板 START 成功；汇总未形成完整节点记录。结果 BLOCKED（INFO 8 / WARN 3 / ERROR 7 / FATAL 0，快照），四板清理后 STOP。保留原始记录及 `inventory-r1/p3-r1-failure-summary.json`，不宣称根因已修复。
- 有界复验：不改源码、不重刷，用 resume 复用本次 package/OTA，软件复位并复核 UID/build，沿用已知线序和原固定范围。`out/HardwareAcceptance/20260920/tools-registry-r1/p3-r2/alarms.json` 为 PASS_WITH_WARNINGS（INFO 25 / WARN 19 / ERROR 0 / FATAL 0，快照），四板 STOP 交接有效。原始 TDMA passed / realtime_gate_passed / closed_loop_passed 仍为 false，diagnostic_passed 为 true；DPLL 未执行。本结果仅支持固定范围快速验收，不授予严格实时或锁相资格。
- 凭证快照，非事实源：build `20260920021717`；源码 SHA256 `0dc1f0bfa9c1458e42dc2cbfd5a0777f045699c50ea39c87c0087e4a8601f548`，1333 个文件。凭证为 `config/hardware_acceptance/p3_acceptance_receipt.json`，软件与审计证据位于 `out/tools-governance/20260920/inventory-r1/`；源码及域文档分离交付。
- 提交与收口：实现和对应凭证已提交 `f9993de4`；tools 内用法说明/生成目录属于同一工具指纹，随实现交付，docs 域文件另交付。严格命名、文档自回归、`FORCE_DOC_GATES=1` 手工 hook、log-check、check-staged 与 staged whitespace 均通过，日志见同目录 `*-closeout.log` / `log-audit.log`。仅关闭文件盘点和初始登记，静态检查完整目标仍在推进。
- 下一 gate：本切片验收后推进 TOOLS-WORKFLOW-001，先消除基础采样链的临时可执行依赖，再处理带注入的专项模式；完整运行依赖、跨机交接与门禁接线仍未完成。

### TOOLS-PROGRESS-20260920-001：建立工具域治理三件套

- TODO task ID：TOOLS-DOCS-001。
- 用户要求：新增独立 tools 文档域，按 Architecture、TODO、Task Progress 分离治理语义、任务状态与实施证据。
- 已建立 `docs/tools/` 三份标准文件，并补总索引、canonical、进度路由及文档域结构规划。采用现行命名白名单支持的 `DOCS_TOOLS_*` 前缀，不为建域修改检查器。
- 盘点快照，非事实源：本轮前置读取时 `git ls-files tools` 返回 263 个文件、95 个含跟踪文件的子目录。`tools/README.md` 已存在使用说明；尚未发现统一工具登记。联合采集入口依赖外置 bench adapter，并硬编码历史 out 基线；存量完整分类与依赖审计尚未执行。
- 门禁差异：`.githooks/pre-commit` 当前 P3 触发 case 未覆盖全部 tools/tests/hook 实现变更，而 AGENTS 与 EXE-CHANGE-01 要求此类改动执行 P3；纳入 TOOLS-GATE-001，未在本轮修改规则或实现。
- 范围：仅文档；登记表、schema、检查器和工作流迁移仍为拟议。无新产品契约、registry 状态变更、特等席或 Core1 工作；不改 OTA。
- 验证与证据：独立复核无结构性阻断，按建议移除架构中的单次范围语句，并给 validation 旧入口补治理边界。docs_check 严格命名通过，doc_regression 通过且仅保留既有告警；检查器回归 38 项通过（本轮快照）。手工 hook 使用 `FORCE_DOC_GATES=1`，双文档门禁通过；log-check、check-staged 通过，后者明确无 staged 实现变更。本轮仅文档，不运行硬件 P3，不产生实现验收声明。输出目录 `out/doc-audit/tools-domain-20260920/`，含 `review.txt` 与门禁日志；本轮未提交。
- 失败/回退：首轮 doc_regression 拒绝空的归档索引标题（`gate-1.log`）；改为无归档说明，首次轮转再建立真实索引，复验通过。不移动或删除现有工具与证据。
- 下一 gate：TOOLS-INVENTORY-001 做全量归属和依赖盘点；建域完成不代表登记、检查器或迁移完成。

尚无轮转归档；首次轮转时建立归档索引表并登记真实文件。
