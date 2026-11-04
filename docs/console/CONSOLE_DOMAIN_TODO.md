# 运维与调试控制面（CONSOLE）待办

Status: Active
Domain: CONSOLE
Canonical: `docs/console/CONSOLE_DOMAIN_TODO.md`
Related: `docs/console/CONSOLE_ARCHITECTURE.md`, `docs/console/CONSOLE_TK_ARCHITECTURE.md`, `docs/console/CONSOLE_TASK_PROGRESS.md`, `docs/README.md`, `docs/interface/SCPI_COMMAND_PLAN.md`, `docs/trigger/t2/TRIGGER_T2_TODO.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/calibration/CALIBRATION_DOMAIN_TODO.md`
Last updated: 2026-11-04

## 当前 checkpoint

- 本域以**调试 Demo**为首个交付物；当前代码位于 `tools/domains/trigger/trigger_debug_demo/`。
- C0 立项已完成：三件套、域索引、`CONSOLE` 命名白名单。
- 下一 gate：C1 底座——CONSOLE-002/003/004 契约登记、CONSOLE-005 REFMEM 页、CONSOLE-006/019 WorkflowRunner、CONSOLE-028..030 Tk Shell/Store/UiPump、CONSOLE-007 安全门。
- **2026-11-04 当前焦点**：`CONSOLE-035` 准入/路由接口已落；`CONSOLE-017` 已完成 adapter 所有权、半双工 relay、HELLO/responder、core0 hook 与只读状态页，**carrier 仍未注册**（等命令 ACK 跟踪）。剩余：PIO 回收与两板 HIL；`CONSOLE-018` 交接未开始。设计边界与先例见架构 §6.1/§6.3。

## 任务表

| ID | 任务 | 优先级 | 状态 | 出口/备注 |
|---|---|---|---|---|
| CONSOLE-001 | 域立项：README + 架构 + TODO + 进展，更新 `docs/README` 与命名白名单 | P0 | DONE | 文档门禁全绿 |
| CONSOLE-002 | 冻结 `CONSOLE-SESSION-01`：单端口/单会话/单写者；GUI/TCP 共享 Dispatcher | P0 | PENDING | 候选契约，登记前 C11 |
| CONSOLE-003 | 冻结 `CONSOLE-TCP-01`：本地行协议、超时、`BUSY OTA`、错误语义 | P0 | PENDING | 含外部客户端示例与负向用例 |
| CONSOLE-004 | 冻结 `CONSOLE-RECEIPT-01`：OTA/flow receipt schema、`receipt_sha256`、`.sha256` | P0 | PENDING | 先复用 OTA 凭证字段 |
| CONSOLE-005 | REFMEM 页：`LOAD:NODE/BOARD/SD`、`LOAD:ACTIVATE`、`LOAD:STATUS?`、`TABle?`、`QUALity?`、`CLAIM/LEASE` | P0 | PENDING | 多板配置的前置 |
| CONSOLE-017 | 引导期低速 SPI 承载：复用 REFMEM `refmem_spi_physical` adapter，由 REFMEM core0 bootstrap owner 驱动 command/ACK/fragment/table/status | P0 | IN PROGRESS | 先例：P4.5 阶段 2 两板 25 MHz 闭环；**不恢复** `SYNC:SPI:*` 帧级 SCPI 直发。代码已闭环（carrier 已注册、命令/reply/超时绑定完成）；ON 镜像余量 1252B（RX DMA −888B）；NO.1/NO.2/NO.4 已刷 ON 镜像并 ARM 成功，但 six-pair IO 预检零连通、physical rx=0，HIL 链路未通，默认开关仍 OFF |
| CONSOLE-018 | bootstrap→ring 交接与回滚：ring ready 自动 handover + DISarm，失败/超时留 evidence | P0 | IN PROGRESS | ring 在 pio1/pio2，与 REFMEM SPI（pio0）不共享指令空间；交接已自动执行（reason=RING_HANDOVER），ring stop 后需显式 re-arm。产品台的“控制对”线序仍未决 |
| CONSOLE-006 | WorkflowPolicy + WorkflowSpec/StepSpec：步骤、前置、期望回读、超时/重试、失败策略、receipt schema | P0 | IN PROGRESS | Spec/Step 已落；WorkflowPolicy 阈值与 receipt schema 未冻结 |
| CONSOLE-019 | WorkflowRunner：持久化 state、cursor、resume/cancel、单 runner、版本匹配 fail-closed | P0 | DONE | `ui/workflow.py`；版本/id 不匹配拒绝加载 |
| CONSOLE-020 | OTA 迁移为 workflow（首个样例）：snapshot→begin→data→end→wait→receipt | P0 | DONE | `ota_workflow.py`；begin/end/receipt 不可恢复，stream 按 chunk cursor |
| CONSOLE-021 | T2 workflow：停止态 1–13 + 运行 16–21 + teardown | P1 | PENDING | 与 CONSOLE-008/009 合并实现 |
| CONSOLE-022 | TDMA workflow：低速 bootstrap → OPMode/topology → TRAIN → ARM → START → 高速交接 | P1 | PENDING | 与 CONSOLE-010/017/018 合并实现 |
| CONSOLE-023 | DPLL/VDC workflow：role/tune → feedback → output → priority → lock/readiness | P1 | PENDING | 与 CONSOLE-011 合并实现 |
| CONSOLE-024 | 校准 workflow：BIAS/MARK/SCK/DATA/CLK/STAGE/ACTIVE | P2 | PENDING | 与 CONSOLE-012 合并实现 |
| CONSOLE-025 | Workflow 版本矩阵与证据：workflow id/version ↔ firmware build ↔ 命令目录 ↔ receipt | P1 | PENDING | 版本不匹配 fail-closed |
| CONSOLE-026 | 维护 §6.2 单端口登记与退出标准；禁止在 TODO/架构另存第二份事实 | P0 | PENDING | §6.2 是登记源；本行只跟踪执行状态 |
| CONSOLE-027 | 过渡期多端口退出：UI `过渡 · 多端口` 标识、ring 未 ready 时 `RING_NOT_READY` 白名单、wrapper 通过后退役维护路径 | P0 | PENDING | 不保留长期双路径；退役按归档规则 |
| CONSOLE-028 | Tk Shell + PageSpec/Router：root/theme/DPI/close guard、懒加载页面、`can_close` 导航守卫 | P0 | DONE | Router/LegacyPageAdapter 覆盖 13 页；DPI/close guard 已接 |
| CONSOLE-029 | ConsoleUiVector + Store + Intent/Presenter：把 UI 状态从 Tk 变量收口，页面只读/只发 Intent | P0 | IN PROGRESS | 五 slice Store 已接；13 页已 render；ACK 已 slice；其余页面字段仍本地 |
| CONSOLE-030 | UiPump + 线程契约：worker→主线程队列、可取消任务、关闭 cancel/join、`after` 可取消 | P0 | IN PROGRESS | results/tcp/ota 已走 pump；app.py 无裸 `self.after`；TCP 活跃 handler 未 join |
| CONSOLE-031 | DialogService + WidgetKit + Theme/DPI：确认/错误/进度、表单/表格/日志/错误条、主题 token | P1 | DONE | DialogPort/ErrorBanner/LogView/ThemeTokens/DPI scaling 已接；app.py 无 messagebox |
| CONSOLE-032 | Tk 测试与打包：FakeScheduler/FakeDialog/FakeControlPlane、Page 契约测试、`python -m` entrypoint | P1 | IN PROGRESS | Store/Router/Pump/Workflow/OTA/Settings/Error 测试已加；ControlPlane/打包未做 |
| CONSOLE-033 | TDMA 流式 OTA 集成：单端口 relay、durable wait、断点/失败恢复 | P2 | PENDING | 固件开发中；不提前宣称可用 |
| CONSOLE-034 | Tk 可访问性/i18n：键盘焦点、字符串表、错误文案分离、高对比度预留 | P2 | PENDING | 不阻塞首版；控件命名预留 label/description |
| CONSOLE-035 | 单端口远程 REFMEM 的 control-transport 准入：ring ready 或 bootstrap carrier（已注册且握手 ready），其余 fail-closed；路由 ring 优先 | P0 | IN PROGRESS | 接口已落：`refmem_control_transport_route/submit` + 组合 readiness + host 用例；carrier 本体未注册，默认行为仍为 ring-only。产品线序是否覆盖见架构 §6.1 |
| CONSOLE-036 | docs_check 裸文件名死链检查：对同目录 *_ARCHITECTURE.md / *_TODO.md / *_TASK_PROGRESS.md 解析存在性 | P2 | PENDING | 工具/门禁变更需 host 验收；先修已知死链 |
| CONSOLE-007 | 安全门：维护模式、RUN 态写拒绝、`STOP/BOOT/COMMIT/CLEAR` 二次确认、owner/lease | P0 | PENDING | 需要负向 host 用例 |
| CONSOLE-008 | T2 停止态 workflow：清表、GRID、DEPloyment、TEMPlate/ACTion、COUNter、T2:CONFigure/MOUNT | P1 | PENDING | 参考 `tools/sync_trigger/t2_workflow.py` 1–13 |
| CONSOLE-009 | T2 运行 workflow：VDC session、优先级、RING、PWM、采样、teardown | P1 | PENDING | 参考 t2_workflow 16–21 |
| CONSOLE-010 | TDMA 启环 workflow：STOP、OPMode、TOPOLOGY、TRAIN、ARM、START、wait、status/counter | P1 | PENDING | 参考 `tdma_start_ring.py`；注意单端口覆盖 |
| CONSOLE-011 | DPLL/VDC workflow：角色/调参/Provisional、feedback session、OUTPut、优先级、LOCK:READiness、质量 | P1 | PENDING | 流程尚未冻结，标实验性 |
| CONSOLE-012 | 校准训练 workflow：BIAS、MARK、SCK、DATA、CLK、STAGE、ACTIVE | P2 | PENDING | 参考 `calibration_*_train.py`；依赖台架 |
| CONSOLE-013 | 证据页：读取 OTA/T2/DPLL60/P3 receipt，显示 PASS/FAIL 与缺失项 | P2 | PENDING | 只读，不抢端口 |
| CONSOLE-014 | 存储/trace/flash/BISS/USB mode 诊断页 | P2 | PENDING | 出问题时的定位面 |
| CONSOLE-015 | 路径迁移：`tools/domains/trigger/trigger_debug_demo/` → `tools/domains/console/debug_console/` | P2 | PENDING | 只迁移不删除；更新 registry/tests/imports |
| CONSOLE-016 | 台架 smoke 与版本矩阵：console ↔ firmware build ↔ 命令表/receipt schema | P2 | PENDING | 台架占用，待空闲 |

## 阶段映射

仅用于按 C0–C4 追溯，不替代任务表；同一任务可跨阶段，以任务表状态为准。

- **C0 立项**：CONSOLE-001。
- **C1 底座**：CONSOLE-002..007、017..020、025..032、035；出口必须含契约登记、bootstrap 准入、WorkflowRunner 可恢复、Tk Shell/Store/UiPump。
- **C2 运行面**：CONSOLE-008..011、013、015、021..023、027；出口含 T2/TDMA/DPLL 运行、单端口收敛、导航守卫。
- **C3 测量面**：CONSOLE-012、014、024、033、034；出口含校准/流式 OTA/诊断/可访问性。
- **C4 收口**：CONSOLE-016、036；台架 smoke、版本矩阵、文档门禁工具收口。

## 阻塞与依赖

- **单端口尚未收敛**：命令面要按架构 §6.3 的三层口径看（本地 SCPI / REFMEM 类型+目录 / 目标 handler）。`LOAD:BOARD/NODE`、`LEASE`、`DPLL ROLE/TUNE/OVERRIDE`、`VDC OUTPUT`、`T2 CONFIGURE/MOUNT` 三层齐；`LOAD:SD` 有类型无目标 handler；`LOAD:ACTIVATE`、`RING:ARM/TRAIN/START`、`OPMode`、`VDC FEEDBACK/PRIORITY/LOCK`、T2 表安装、CAL 只有本地 SCPI。ring 未 ready 时远程 REFMEM 会被 `refmem_control_submit.c` 拒绝，先按 §6.2 走过渡多端口并登记 `CONSOLE-035`。
- **低速承载本体未落地**：`refmem_spi_physical` adapter 目前由 SCPI 持有（`s_refmem_sync.spi_adapter`），只有 arm/disarm，没有 PIO 指令空间回收；最小系统 profile 与 SYNC_IO 调试组重叠，产品 profile 的线序又与 TDMA persona 不同。所有权迁移、relay、握手 readiness 与 unload 未做前，不得宣称可热加载或“单端口可建环”。
- **校准训练依赖低速承载**：校准要内化到 REFMEM 统一控制面，但建环前没有高速数据面，也没有 CAL 类型/目标 handler；只能先用 §6.1 的低速承载，否则退回多端口本地 SCPI。CAL 类型与 stage 语义仍是阻塞项（§6.3）。
- **DPLL 流程未冻结**：`docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md` §11 仍把四板 VDC 建立流程列为待办，先按 T2 workflow 6/13/15/17/18 作为实验性参考。
- **台架占用**：所有硬件证据延期；host/simulator 先闭环。
- **路径迁移**：在 C1 稳定前不迁移，避免 import/registry/test 同时变动。

## 验收口径

- 每个 P0 任务有 host 用例与可复跑命令。
- 每个流程步骤有前置条件、期望回读、失败 reason 和 receipt 字段。
- 长期流程必须以 `WorkflowSpec` 交付，状态持久化、可恢复、可取消；不得用 GUI-only 线程状态。
- 单端口是最终目标；多端口只能作为已登记的过渡维护项，必须在 UI 显示 `过渡 · 多端口`、端口占用和退出标准，不得偷偷切换或成为长期双路径。
- 文档变更按 `AGENTS.md` §5 跑全部门禁。
