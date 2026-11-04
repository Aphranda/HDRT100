# 运维与调试控制面（CONSOLE）任务进展

Status: Active
Domain: CONSOLE
Canonical: `docs/console/CONSOLE_TASK_PROGRESS.md`
Related: `docs/console/CONSOLE_ARCHITECTURE.md`, `docs/console/CONSOLE_TK_ARCHITECTURE.md`, `docs/console/CONSOLE_DOMAIN_TODO.md`, `docs/README.md`
Last updated: 2026-11-02

## 文档接口

本文记录 CONSOLE 域的真实执行、证据和失败；稳定语义见 [Architecture](CONSOLE_ARCHITECTURE.md)，任务状态见 [TODO](CONSOLE_DOMAIN_TODO.md)。条目标签使用 `CONSOLE-PROGRESS-YYYYMMDD-NNN`，最新在最上面。

## 当前 checkpoint

### CONSOLE-PROGRESS-20261102-015：M5 CONSOLE 范围收口

- **结果**：13 个页面全部通过 Router 注册并实现 `render(vector)`；`app.py` 无 `messagebox`/裸 `self.after`；OTA 走 `WorkflowSpec`；Store 含 Connection/Mask/Run/Ack/Ui 五 slice；Settings/ErrorBoundary/LogView/ThemeTokens/DPI 已接。
- **验证**：`test_trigger_debug_ui.py + test_trigger_debug_demo.py` 27 passed；UI smoke 断言 13 页均有 render；docs 250/0、38 doc tests。
- **残余**：全量仓库 `pytest tests/python` 首败为 `test_app_realtime_profile` 的 `profile.exe` exit 3221226505（非 CONSOLE 改动）；无 HIL/P3；TCP 活跃 handler 未 join；ACK 之外页面字段仍本地；CONSOLE 契约未登记；未提交。
- **判定**：CONSOLE 重构目标范围完成；全仓基线/HIL/契约作为后续硬化项，不写成本次已通过。

### CONSOLE-PROGRESS-20261102-014：M5 ACK slice 与 13 页 render 收口

- **结果**：`ConsoleUiVector` 新增 `AckSlice`；`AckPage.show_status/show_reply` 写 Store，`render` 从 Store 回填 seq/state/ACK/NACK/timeout/reason；UI smoke 断言 13 个页面均有 `render`，Control 通过 `control_holder.render` 代理。
- **验证**：targeted host 27 passed；`app.py` 无 `messagebox`/裸 `self.after`；docs 250/0、38 doc tests。
- **全量 host**：仍在 `test_app_realtime_profile` 的 `profile.exe` exit 3221226505 首败，未计为通过。
- **剩余**：全量 host 基线、HIL/P3、TCP 活跃 handler、其他页面字段 slice 化、契约登记。

### CONSOLE-PROGRESS-20261102-013：M5 页面全覆盖与 host 回归（部分）

- **结果**：剩余 9 个页面（Overview、5 个 FunctionPage、Control、Ota、Tcp）补齐 `render(vector)`；`FunctionPage` 负责 card 转发，`FunctionCard/Control/Ota/Tcp` 按连接态控制发送/读取按钮；13 个 tab 现在都通过 Router 订阅 Store。
- **生命周期**：`OtaPage` 的取消事件改名 `_cancel_event`，新增 `cancel/dispose`；`TcpPage` 新增 `render/cancel/dispose`；关闭时 Router 退订并调用页面生命周期。
- **验证**：相关 host 套件 `test_trigger_debug_ui.py + test_trigger_debug_demo.py` 27 passed；UI smoke 断言 13 个页面均有 `render`；`app.py` 无 `messagebox`/裸 `self.after`；docs 250/0、38 doc tests。
- **全量 host 未绿**：`pytest tests/python -q` 在 `test_app_realtime_profile.py` 因 `profile.exe` exit 3221226505 失败，与本次 CONSOLE 改动无关；未继续跑完，未把它计为通过。
- **剩余**：全量 host 基线、HIL/P3、契约登记、ACK 业务数据 slice 化。

### CONSOLE-PROGRESS-20261102-012：M4 Dialog/Settings/ErrorBoundary/WidgetKit 收口

- **结果**：`app.py` 的 17 处直接 `messagebox` 全部改为 `DialogService`（error/warning/info/confirm）；`ui/settings.py` 提供 schema 白名单、原子写、损坏备份 `.bad`；`ui/errors.py` 提供 ErrorBoundary + `report_callback_exception` 限流；`ui/widgets.py` 提供 ErrorBanner/LogView；`ui/theme.py` 提供主题 token 与 `apply_tk_scaling`；DemoApp 接入 ErrorBanner、LogView、Settings。
- **验证**：`app.py` 已无 `messagebox`/裸 `self.after`；settings/error 测试新增；demo+UI 27 passed。
- **剩余**：全量 HIL/P3、TCP 活跃 client handler join、契约登记；M5 处理。

### CONSOLE-PROGRESS-20261102-011：M3 ACK/回读/诊断/原始命令页面迁移

- **结果**：`PageRouter.register_legacy` 对实现 `render(vector)` 的页面订阅 `ConsoleStore`，页面关闭时退订；`AckPage`、`QueryPanel`、`DiagnosticsPage`、`RawPage` 增加 `render/cancel/dispose`，按 `ConnectionSlice.connected` 控制读取/发送按钮。
- **去裸 after**：`AckPage._auto_tick` 改用 `TkScheduler` 并保存 token；`OtaPage` 的 receipt 定时也改用 `app.scheduler`。当前 `app.py` 已无 `self.after(` 调用。
- **Dialog**：`QueryPanel` 参数错误改走 `app.dialogs.error`；`AckPage`/诊断/原始命令无 `messagebox`。
- **测试**：新增 Router→Store→render 用例；UI smoke 断言连接后 ACK/raw 按钮可用；demo+UI 25 passed。
- **剩余**：`FunctionCard`/`ControlPage`/`OtaPage`/`TcpPage` 仍有 17 处直接 `messagebox`，留给 M4 WidgetKit/DialogService 收口。

### CONSOLE-PROGRESS-20261102-010：M2b OTA 迁移为可恢复 WorkflowSpec

- **结果**：新增 `ui/workflow.py`（WorkflowSpec/StepSpec/WorkflowState/WorkflowRunner）与 `ota_workflow.py`（snapshot→begin→stream→end→wait→receipt 六步）。`OtaPage.start` 不再启动 ad-hoc `_run` 线程，改为 `OtaWorkflow.start`；`_run` 仅保留兼容 wrapper；`abort` 同时 cancel workflow；新增“恢复上次”按钮，按 step cursor 恢复；`stream` 用 `chunk_index` checkpoint，并在恢复前用 `PROGress?` 校验偏移。
- **安全边界**：`begin`/`end`/`receipt` 标记 `resume_safe=False`；`stream` 可恢复但设备偏移不一致直接 fail-closed；state 写 `out/workflows/ota.conventional-<run_id>.json`，版本/id 不匹配拒绝加载。
- **测试**：`tests/python/test_trigger_debug_ui.py` 增加 WorkflowRunner 持久化/恢复/拒绝 unsafe resume/cancel；`test_trigger_debug_demo.py` 增加 OTA workflow 六步/凭证/2 块集成用例与按 chunk cursor 恢复用例；当前 demo+UI 24 passed。
- **未做**：M3 页面迁移、M4 WidgetKit、M5 HIL/收口；OTA 的 `begin`/`end` 失败仍需从头重跑（无 durable offset）。

### CONSOLE-PROGRESS-20261102-009：M2a TCP 停止移出 UI 线程

- **结果**：`device.TcpBridge` 增加 `stop_async()` / `wait_stopped()` / `_stop_server()`：先 detach server 拒绝新客户端，再在后台线程执行 `shutdown/server_close`；`TcpPage.stop()` 改为异步 + `TkScheduler` 轮询完成，UI 不再直接 `shutdown()`；`disconnect()` 用 `stop(wait=True)` 在断开 transport 前等待桥接退出。
- **测试**：`tests/python/test_trigger_debug_ui.py` + `test_trigger_debug_demo.py` → 19 passed；`py_compile app.py/device.py` OK。
- **剩余**：`TcpBridge` 的活跃 client handler 仍是 daemon 线程，未追踪 join；OTA 仍是 ad-hoc 线程（M2b）。

### CONSOLE-PROGRESS-20261102-008：Demo 按 Tk 架构完成 strangler 底座接入

- **结果**：新增 `tools/domains/trigger/trigger_debug_demo/ui/`：`state.py`（四 slice Store）、`scheduler.py`（Tk/Fake）、`dialogs.py`（DialogPort）、`page.py`（Page/NavigationDecision/LegacyPageAdapter）、`router.py`（Notebook Router + guard）、`pump.py`（有界 UiPump/PumpSink）。
- **接入**：`DemoApp` 使用 Store/Router/TkScheduler/TkDialogService/UiPump；13 个现有 tab 通过 `LegacyPageAdapter` 注册；`results/tcp/ota` 事件改为 pump；`Dispatcher` 结果由 `_on_result` 投递；`disconnect` 增加 `join(timeout)`；OTA 独占期非 OTA 命令改为静默丢弃 + status，不再弹模态。
- **未做**：没有重写 13 个 legacy Page 内部逻辑；`OtaPage` 仍是 ad-hoc 线程；`TcpBridge.shutdown` 仍在 UI 线程（legacy）；没有 implement WorkflowRunner/REFMEM/T2/TDMA/DPLL/校准。
- **测试**：新增 `tests/python/test_trigger_debug_ui.py`，覆盖 Store 四 slice/revision/退订、FakeScheduler、UiPump 有界/terminal 保留、PumpSink、Router guard、懒加载 Page；demo+UI 共 19 passed。
- **门禁**：`python -m pytest tests/python/test_trigger_debug_ui.py tests/python/test_trigger_debug_demo.py -q` → 19 passed；`python -m py_compile` app+ui → OK；`docs_check --strict-names` → passed（files=250 warnings=0）；`doc_regression_check` → passed。

### CONSOLE-PROGRESS-20261102-007：三次独立评审后的架构收敛

- **评审 1（架构一致性）**：指出单端口远程 REFMEM 与 `refmem_control_submit.c` 的 ring-ready gate 矛盾；REFMEM 目录已覆盖 `LOAD`/`LEASE`/`DPLL`/`VDC OUTPut`/`T2`，文档“未覆盖”表述过宽；`MODULES` 不是页面事实源；Workflow 与 OTA ad-hoc 缺现行例外；候选契约被提前当验收条款。
- **评审 2（Tk 工程）**：指出关闭/取消链、Page/Store 契约、OTA 单端口独占期三个 BLOCKER；`UiPump` 无界、after 无所有权、Fake 无注入点、`can_close` bool/reason 冲突；建议首版不引入 ttkbootstrap、删除 snapshot/restore/图片等过度设计。
- **评审 3（文档治理）**：指出主架构曾引用不存在的 CONSOLE_TODO.md（已改为 `docs/console/CONSOLE_DOMAIN_TODO.md`）、`docs_check` 裸文件名盲区、README 任务范围过期、Related 缺 TK、目标树缺 `console/`、P0 与 C0–C4 缺映射、进展缺门禁凭证。
- **已修**：§6 增加“单端口可用前提”并明确 ring 未 ready 只能本地/维护多端口；§6.2 增加退出标准并按 catalog 行号纠正阻塞；§6.1 补 `STOPPED` 与“新增低速 persona program”；§7.6 增加现行例外表；§7.8/7.9/§10 标注候选契约；Tk 文档补 Page `render/cancel/dispose`、四 slice、Scheduler/ControlPlane/Dialog 注入、关闭时序、OTA 独占期、UiPolicy、设置原子写；TODO 新增 033–036，README/索引/阶段映射对齐。
- **未修**：`docs_check` 裸文件名死链检查（CONSOLE-036，需工具改动 + host 验收）；CONSOLE-002/003/004/035 仍未登记/未落地；代码仍为 legacy，未改 Tk 实现。
- **门禁**：`python tools/docs_check/docs_check.py --strict-names` → passed（files=250 warnings=0）；`python tools/doc_regression_check.py` → passed；`python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -q` → 38 passed。

### CONSOLE-PROGRESS-20261102-006：Tk 应用框架写入架构，接上主架构/索引/TODO

- **结果**：新增 `docs/console/CONSOLE_TK_ARCHITECTURE.md`，定义 Shell/Router/Page/Store/Intent/UiPump/Dialog/WidgetKit/ErrorBoundary 分层与契约；`CONSOLE_ARCHITECTURE.md` 新增 §11“Tk 应用框架”摘要并链接详情。
- **索引**：`docs/console/README.md`、`docs/README.md`、`docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md` 已加入该 canonical；TODO 新增 `CONSOLE-028..032`（Shell/Store/UiPump/Dialog/测试打包）。
- **口径**：Tk 层采用 MVP/Passive View + Redux-lite Store/Intent + Hexagonal ControlPlane + Statechart/Saga workflow；不引入完整 MVVM/Redux/Temporal 运行时。
- **现状**：当前 Demo 仍是 legacy（Page 直接继承 `ttk.Frame`、状态散在 Tk 变量、直接 `messagebox`、OTA ad-hoc 线程）；本次只冻结框架定义，未改代码。
- **门禁**：`python tools/docs_check/docs_check.py --strict-names` → passed（files=250 warnings=0）；`python tools/doc_regression_check.py` → passed；`python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -q` → 38 passed。
- **下一步**：先落 `CONSOLE-028/029`（Page/Router + Store/Intent），再 `CONSOLE-030`（UiPump），最后迁移 OTA/Workflow 页面。

### CONSOLE-PROGRESS-20261102-005：单端口改为最终目标，多端口降级为过渡路径

- **结果**：`CONSOLE_ARCHITECTURE.md` §6 改为“单端口目标与过渡期路径”，新增 §6.2 单端口路径优化登记（REFMEM/T2/TDMA/DPLL/校准/OTA 的当前→目标 transport、阻塞、迁移任务）；§7.4/7.5/§9/§10 统一改为“目标单端口，过渡期多端口”。
- **TODO**：新增 `CONSOLE-026`（单端口路径登记）与 `CONSOLE-027`（过渡多端口退出/UI 标识）；原有阻塞项改为“单端口尚未收敛”。
- **口径**：多端口不是产品模式；默认 workflow 的 `target_transport` 必须是 `single_port_refmem`，多端口仅允许显式维护模式并在 wrapper 通过后退役。
- **现状**：常规 OTA 已是单端口顺序；T2/TDMA/DPLL/校准/REFMEM 装载仍是过渡多端口（或混合），尚未收敛。
- **下一步**：按 §6.2 逐项补 typed REFMEM command/table package，先把 REFMEM 装载与 T2 表安装收敛到单端口。

### CONSOLE-PROGRESS-20261102-004：Workflow 定义、步骤模型与能力范围写入架构

- **结果**：`CONSOLE_ARCHITECTURE.md` §7 扩充为完整定义：§7.2 形式与定义（Spec→Planner→Runner→State→Evidence→视图）、§7.3 步骤模型/状态机、§7.4 能做/不能做/与命令卡/宏/脚本/验收工具的边界、§7.5 各流程能力矩阵、§7.6 L0–L3 成熟度与 v1 线性边界、§7.7 待定设计选择。
- **明确约束**：workflow 必须声明 transport（单端口 REFMEM / 维护多端口 / 离线）；v1 不做 parallel、不做超过 1 层嵌套；OTA 是第一迁移对象，T2 第二。
- **未实现**：当前 OTA 仍是 ad-hoc 线程，T2/TDMA/DPLL/校准无 workflow；本次仅冻结定义与边界。
- **下一步**：按 `CONSOLE-006/019` 落 WorkflowPolicy/Spec/Runner，再迁移 OTA。

### CONSOLE-PROGRESS-20261102-003：控制台约束——长期流程必须 workflow 化

- **结果**：`CONSOLE_ARCHITECTURE.md` 新增 §7“工作流约束（Workflow-first）”，定义 WorkflowSpec/Step/运行语义/证据/UI/禁止项；明确 OTA、T2、TDMA 启环、DPLL/VDC、校准训练、REFMEM 装载必须走 workflow，不得以“按钮 + 线程 + 内存状态”实现。
- **新增任务**：`CONSOLE-019` WorkflowRunner + 持久化/恢复、`CONSOLE-020` OTA 迁移为首个 workflow、`CONSOLE-021..024` T2/TDMA/DPLL/校准 workflow、`CONSOLE-025` 版本矩阵。
- **现状差距**：当前 OTA 仍是 `OtaPage` 内的 ad-hoc 线程，T2/TDMA/DPLL/校准没有 workflow；因此本约束是**目标态**，不是当前已满足项。
- **下一步**：先落 WorkflowPolicy/WorkflowSpec 静态定义与 WorkflowRunner，再把 OTA 迁移为第一个可恢复 workflow，最后复制到 T2/TDMA/DPLL/校准。

### CONSOLE-PROGRESS-20261102-002：架构决定——校准/TDMA 前用低速 SPI PIO 承载 REFMEM 控制面

- **结果**：在 `CONSOLE_ARCHITECTURE.md` §6.1 增加“引导期低速 SPI PIO（bootstrap persona）”链路：STOP 态热加载低速 SPI persona，先用它承载 REFMEM command/ACK/table/status，完成校准与拓扑/时钟准备后，再通过 persona FSM（QUIESCING → UNLOADING → LOADING → ACTIVE）切到高速 TDMA persona；失败走 ROLLING_BACK 回低速恢复。
- **边界**：仍是同一组 SPI 引脚；不是第二总线/第二命令槽/第二 ACK；REFMEM 的 CRC/seq/fence/quality/ACK 语义不变；只在 STOP/维护边界热加载，RUN 中禁止。
- **未实现**：低速速率、PIO 指令字数、DMA/SM 预算和切换窗口尚未核算；已登记 `CONSOLE-017`（bootstrap persona）与 `CONSOLE-018`（高速交接/回滚），当前只是架构决定，不是已验证事实。
- **下一步**：先做 persona 状态/资源核算与 host 模型，再进台架验证切换与回滚。

### CONSOLE-PROGRESS-20261102-001：CONSOLE 主域立项，调试 Demo 成为首个交付物

- **结果**：新增 `docs/console/` 主域三件套与 README；`CONSOLE` 加入文档命名白名单；`docs/README.md` 增加域入口与 canonical 行。调试 Demo 定位为本域首个交付物，当前仍在原路径，迁移列为 `CONSOLE-015`。
- **交付物**：`tools/domains/trigger/trigger_debug_demo/`
  - 单端口 GUI + 本地 TCP 共用同一 Dispatcher；
  - 串口 / USB TMC（PyVISA）/ 模拟器三种 transport；
  - REFMEM 统一控制面与应用层 SCPI 两条通道；
  - 常规单板 OTA（BEGIN/PBEGIN/DATA/END/BOOT/COMMIT）与自动凭证；
  - 板卡 ACK、诊断、原始命令页。
- **证据**：
  - host：`python -m pytest tests/python/test_trigger_debug_demo.py -q -p no:cacheprovider` 全绿（快照，非事实源）；
  - 模拟 OTA：`out/ota_receipts/` 自动生成 JSON + `.sha256`；
  - 截图：`out/trigger_debug_demo.png`、`out/trigger_debug_demo_ota.png`、`out/trigger_debug_demo_tcp.png`；
  - 注册：`TOOL-CONSOLE-DEBUG-DEMO`（已从 `TOOL-TRIGGER-DEBUG-DEMO` 改名；路径迁移见 CONSOLE-015）。
- **未覆盖**：REFMEM 装载/表/质量页、T2/TDMA/DPLL/校准四条流程、安全门、HIL/台架证据；见 TODO 的 CONSOLE-005..016。
- **下一 gate**：C1 底座——先做 REFMEM 页 + FlowRunner + receipt 规范 + 安全门；在此之前不迁移路径、不宣称主域完成。
