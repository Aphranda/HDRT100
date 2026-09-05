# 项目执行约束与证据门禁

Status: Draft
Domain: Documentation Governance
Canonical: `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Related: `AGENTS.md`, `README.md`, `docs/check/DOCS_REGRESSION_PLAN.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TODO.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`
Last updated: 2026-09-05

> 本文是跨 worker/agent 的长期执行流程入口。它描述如何工作、如何留证和何时停止；不替代产品架构、域内运行时契约或单次验收报告。

## 1. 适用范围与优先级

本文适用于主控、worker、自动化工具和参与 HDRT100 变更的其他协作者。

约束优先级如下：

1. 系统安全要求和用户当前明确指令优先。
2. 仓库 `AGENTS.md` 与本文必须保持一致；`AGENTS.md` 只保留 onboarding、最短门禁和本文入口。
3. 域架构、TODO、Task Progress 和验收报告分别承担稳定语义、计划状态、执行记录和原始证据，不得相互替代。
4. 与历史快照、旧任务消息或已废止流程冲突时，以当前 canonical 文档和最新用户指令为准，并保留冲突证据。

## 2. 工具、编码与工作区

### EXE-TOOL-01：统一可复核的项目命令

- 项目命令优先使用工作区约定的 Git Bash：`D:\Aphranda\Git\bin\bash.exe`。
- 如果某个工具只能在 PowerShell、CMD 或 Python 原生环境运行，可以使用等效入口，但必须在报告中说明原因和实际命令。
- Markdown、Python、配置和源文件按 UTF-8 读取；修改使用 `apply_patch`，不得覆盖或回滚未授权的用户改动。
- 构建、测试、验收、波形、时序和审计中间产物统一写入 `out/` 的任务子目录。临时 pytest basetemp 也必须位于 `out/`，根目录不得留下 `.tmp-*` 或同类临时目录。

### EXE-TOOL-02：事实源与快照边界

- 硬数字、状态结论和能力声明必须回到代码符号、登记表或原始验收证据。
- 无法绑定事实源的数字必须标注“快照，非事实源”，不能在本文固化为产品事实。
- 当前 OTA 块大小、某次 build/HIL 编号、debug 配额、worker 数量和单轮完成结论属于任务快照，不应直接迁入本文。

## 3. 变更与证据闭环

### EXE-CHANGE-01：独立改动的最小交付单元

每个独立代码、PIO、构建、工具或测试改动必须按以下顺序完成：

1. 明确状态机节点、变更边界、风险和回滚方式。
2. 运行相关软件测试并记录完整命令和结果。
3. 编译受影响配置，保留 build 输出和源码指纹。
4. 对固件/PIO/构建/工具/测试改动执行 P3 硬件验收；验收凭证必须绑定当前 staged 源码指纹。
5. 将成功、失败、拒绝原因、资源冲突、状态快照和原始数据写入 `out/`。
6. 主控复核证据后，代码与文档分离提交；提交后按任务授权选择性 push。

失败不能被摘要覆盖。`forced_continue` 只能表示调试流程在记录拒绝后继续，不得写成严格门禁通过。

### EXE-STATE-01：状态机迁移与 TDMA 短帧闭环

- 每次状态机状态或状态边界发生改动后，必须先完成 TDMA 短帧闭环，再进入下一项迁移。
- 闭环失败时，使用 SD 波形/逻辑分析工具定位，保留原始波形、解析结果和修复前后对比；未修复不得推进。
- 旧 build、不同源码指纹或仅有 host 测试的闭环证据不能证明当前切片已验收。
- 短帧闭环至少应能回溯目标 build、板卡/链路、周期或帧计数、`passed`、`closed_loop_passed`、实时门禁结果、诊断结果、强制继续标记和原始证据目录。

### EXE-OTA-01：OTA 时序失败处理

- OTA 传输完成、设备状态发布、端口关闭、复位、USB 重枚举和提交确认必须分别记录时间和原始响应。
- `IDLE`、`PermissionError`、重枚举失败或 post-reset race 必须保留为失败证据；不得只增大 timeout 或改变最终状态判断来掩盖时序问题。
- OTA 工具改动必须先有 host/仿真回归，再进行新固件硬件验收；硬件仍运行旧 build 时，不得用旧证据替代新 build 验收。

## 4. Debug 安全门禁

### EXE-SAFE-01：可恢复拒绝的记录与有界继续

Debug profile 下，可恢复的门禁拒绝、资源冲突、状态不稳定、校准失败或诊断失败必须记录：

- 拒绝原因和原始返回值；
- 资源冲突和参与者；
- 状态快照、计数器和时间戳；
- 原始串口、波形、日志或存储数据；
- 有界强制继续次数、后继状态和终止条件。

记录后可沿有界强制继续路径推进，直到下一状态、超时或本轮结束。强制继续不改变失败事实，也不提升为产品模式行为。

### EXE-SAFE-02：不可恢复风险硬停

仅下列不可恢复安全风险允许硬停：越界 DMA、非法内存或 Flash 操作、失控 GPIO，以及经代码/硬件证据确认等价的不可逆风险。产品 profile 继续保持严格拒绝。

## 5. Worker 与主控职责

### EXE-ROLE-01：worker 边界

- 文档 worker 先只读审计，按 A/B/C/D 输出逐文件问题、证据、建议动作、批次和契约登记影响；未经确认不得修改文档、检查器或 `.agents/`。
- 状态机 worker 聚焦一个明确状态或切片；不得跨越 TDMA 短帧闭环、编译、软件/硬件验收和证据复核直接进入下一状态。
- worker 不得把旧约束、旧设备映射、旧 build 或旧验收报告当作当前事实；发现时效性冲突必须上报。

### EXE-ROLE-02：主控复核

主控负责核对源码差异、测试覆盖、build 指纹、TDMA/SD 原始证据、硬件验收凭证和远端状态。只有证据闭合后才选择性 commit/push；遇到外部阻塞必须保留原始失败并明确下一可执行动作。

## 6. 文档自回归与契约边界

- 新增或修改文档必须满足 5 字段元数据、命名规则、`docs/README.md` 索引和 `Last updated` 要求。
- 文档门禁使用 `tools/docs_check/docs_check.py --strict-names`、`tools/doc_regression_check.py`、对应 pytest 和 pre-commit；所有临时输出放入 `out/doc-audit/<run-id>/`。
- 本文是流程规范草案，不新增 `DOCS_REGISTRY` 契约。若将其中某条冻结为产品或跨域运行时契约，必须下沉到对应域 canonical，登记唯一 `contract_id`，并按 C11 交叉审核和顶层刷新规则处理。
- 契约登记行不可物理删除，只能转为 `superseded`；评审快照和草稿不得登记为契约。

## 7. 当前版本状态

本文首版为 `Draft`，用于收敛分散在对话、`AGENTS.md`、工具说明和验收流程中的长期执行约束。后续应通过文档审计、worker 交叉复核和门禁结果决定是否转为 `Active`；转为 `Active` 不代表其中的单次 build、板端状态或历史验收结果成为永久事实。
