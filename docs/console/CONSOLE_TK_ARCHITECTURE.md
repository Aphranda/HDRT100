# 运维与调试控制面（CONSOLE）Tk 应用框架架构

Status: Active
Domain: CONSOLE
Canonical: `docs/console/CONSOLE_TK_ARCHITECTURE.md`
Related: `docs/console/CONSOLE_ARCHITECTURE.md`, `docs/console/CONSOLE_DOMAIN_TODO.md`, `docs/console/CONSOLE_TASK_PROGRESS.md`, `docs/README.md`, `docs/arch/HAOFV_ARCHITECTURE.md`
Last updated: 2026-11-02

## 1. 定位

本文定义 CONSOLE 的 **Tk 应用框架层**：页面如何组织、状态放在哪里、线程如何回到主线程、对话框如何可测试、主题与 DPI 如何统一。它不定义设备语义，也不替代 `CONSOLE_ARCHITECTURE.md` 的 workflow/单端口/证据约束。

目标：

- 单 UI 线程：只有 Tk 主线程能碰 widget。
- 单向数据流：Page → Intent → Presenter/Workflow → Store → Page。
- 页面可测试：Page 是 Passive View，不依赖真实设备。
- 页面懒加载、可恢复、可守卫：workflow 运行中禁止误切页。
- 统一 Dialog/WidgetKit/Theme/DPI，不让每页各写一套。
- 有错误边界、日志上限、关闭安全、设置持久化。

非目标：

- 不做通用 GUI 框架，不替换 ttk。
- 不做完整 MVVM 双向绑定，不做完整 Redux/Event Sourcing。
- 不把 Temporal/Prefect/Airflow 运行时引入桌面工具。
- 不替各域定义 SCPI/工作流语义。

## 2. 设计来源与取舍

| 来源 | 借鉴点 | 取舍 |
|---|---|---|
| [Fowler Passive View](https://martinfowler.com/eaaDev/PassiveScreen.html) / [Humble Dialog](https://www.martinfowler.com/articles/humble-dialog-box.html) | Page 只做展示；Presenter 处理逻辑；对话框可测试 | ✅ 采用为页面基座 |
| [Redux 三原则](https://redux.js.org/understanding/thinking-in-redux/three-principles) / Elm | 单一状态、只读快照、Intent→更新→View | ✅ 采用 Redux-lite，不做全局大 store |
| Presentation Model / MVVM | ViewModel 承载展示状态 | ✅ 采用 `ConsoleUiVector`；❌ 不做双向绑定 |
| [Hexagonal / Ports & Adapters](https://docs.aws.amazon.com/prescriptive-guidance/latest/cloud-design-patterns/hexagonal-architecture.html) | Transport/ControlPlane 是 port，serial/VISA/TCP 是 adapter | ✅ 采用 |
| [CQRS](https://martinfowler.com/bliki/CQRS.html) | 命令与查询分离 | ✅ 采用 Intent 分类，不引入独立总线 |
| [Saga / 补偿](https://microservices.io/patterns/data/saga.html) | 长流程失败后的补偿/回滚 | ⚠️ 仅用于可逆步骤；OTA post-commit 不可补偿，必须标记不可逆 |
| [Temporal durable execution](https://docs.temporal.io/evaluate/understanding-temporal) | workflow state、activity、retry、idempotency、resume | ✅ 借概念；❌ 不引入服务端 |
| [XState / Statecharts](https://stately.ai/docs/xstate) | 显式状态与迁移 | ✅ 落成表驱动 ECC，不强依赖库 |
| [Qt Model/View](https://doc.qt.io/qt-6.8/model-view-programming.html) | Model 与 View 分离 | ✅ 用于表格/日志/命令列表 |
| [ttkbootstrap](https://www.ttkbootstrap.org/en/latest/user-guide/foundations/the-widget-model.html) | 主题/DPI/控件样式 | ❌ 首版不引入；用现有 clam + theme token。若后续引入，必须 pin 版本、做许可证/回退评估，禁止主题双写 |
| [VMx](https://github.com/thekaveh/VMx) / [Tkinter-MVC-Example](https://github.com/unodan/Tkinter-MVC-Example) | Tk 的 MVVM/MVC 示例 | ⚠️ 只作参考，不作依赖 |
| [pygubu](https://github.com/python-gui-application/pygubu) | 声明式 UI/XML | ⚠️ 复杂静态表单可评估；动态 workflow 页不适用 |

## 3. 分层

```text
Tk Shell（root / mainloop / theme / DPI / close guard）
  ├─ Router（PageSpec / Page 生命周期 / 导航守卫）
  ├─ Pages（Passive View，只读 Store、只发 Intent）
  ├─ Store（ConsoleUiVector + selectors，只读快照）
  ├─ Intent Bus（Page → Presenter/Controller）
  ├─ Presenters / Controllers（参数校验、权限、前置条件）
  ├─ Workflow AO（WorkflowRunner + Step ECC；补偿仅用于可补偿步骤）
  ├─ UiPump（worker/queue → Tk 主线程）
  └─ Services（Transport / Dialog / Settings / Log / Theme）
```

数据流：

```text
Page → Intent → Presenter → Workflow/ControlPlane
                               ↓
Page ← 订阅/选择器 ← UiPump ← Store（新 revision）
```

Page 不直接调用 Dispatcher/Transport，不直接改业务状态，不直接 `messagebox`。

**端口与组合根**（测试注入点）：

| Port | 作用 | 生产实现 | 测试实现 |
|---|---|---|---|
| `SchedulerPort` | `after/after_cancel`、timer 所有权 | TkScheduler | FakeScheduler |
| `ControlPlanePort` | `submit/read/wait_acked` | Refmem/MultiPort | FakeControlPlane |
| `DialogPort` | `confirm/error/progress` | TkDialogService | FakeDialogService |
| `StorePort` | `snapshot/subscribe` | ConsoleStore | InMemoryStore |

`console.app.build_app()` 是唯一组合根：由它 new Store/Dispatcher/WorkflowAO/Dialog/Scheduler，再注入 `PageHost`。Page 不自己 new 服务，也不使用全局单例。

## 4. Tk Shell

Shell 是唯一持有 root 的组件：

- 创建 `tk.Tk`、`ttk.Style`、菜单/状态栏/主题/DPI。
- 安装 `report_callback_exception`，把未捕获异常送到 ErrorBoundary/日志，而不是弹默认栈。
- `WM_DELETE_WINDOW` 固定时序：
  1. 置 `closing`，拒绝新 Intent/导航；
  2. 对所有 Page/Workflow 调 `cancel()`；
  3. 停 TCP bridge（worker + 轮询完成，**不得在 UI 线程 `shutdown()`**）；
  4. transport owner `join(timeout)`；只有 join 成功才 `close()` transport，超时记录未退出任务；
  5. 取消所有 `after` id（per-page registry）；
  6. destroy root。
- `can_close` 必须快速、无阻塞、不弹窗、不做 I/O；需要确认的操作先由 Presenter/DialogService 完成，再进入关闭流程。
- 负责全局快捷键、窗口几何、最小尺寸、主题切换。
- 不承载业务逻辑，不直接发 SCPI。

## 5. Router 与 Page 契约

`PageHost` 只向页面暴露 `intents`、`store`、`dialogs`、`router` 和只读 `context`；**不暴露 transport/ControlPlane**。页面拿不到串口/VISA 句柄，从结构上阻止绕过 Presenter。

```python
@dataclass(frozen=True)
class NavigationDecision:
    allowed: bool
    reason: str = ""

@dataclass(frozen=True)
class PageSpec:
    id: str
    title: str
    factory: Callable[["PageHost"], "Page"]
    capabilities: frozenset[str] = frozenset()  # connection / workflow / ota / maintenance
    lazy: bool = True
    shortcut: str | None = None

class Page(Protocol):
    def build(self, parent) -> None: ...
    def render(self, vector: "ConsoleUiVector") -> None: ...
    def on_show(self, ctx) -> None: ...
    def on_hide(self) -> None: ...
    def can_close(self) -> NavigationDecision: ...
    def cancel(self) -> None: ...
    def dispose(self) -> None: ...
```

- 页面**懒创建**：第一次 `show` 才 `build`，避免启动时构建所有 tab。
- 每个 Page 一个实例；`on_hide` 停 timer/订阅，`dispose` 释放资源，`cancel` 取消本页发起的任务。
- `can_close` 是**纯函数**：只读当前快照，不弹窗、不做 I/O、不等待；需要确认时由 Shell 先做一次性导航确认，再调 `can_close`。
- 不再提供 `snapshot/restore`；首版只持久化“最近页面”，不放宽到跨会话业务状态。
- Router 负责 `show(page_id, params)`、`back`、`guard`；所有导航返回 `NavigationDecision`。
- 导航守卫：workflow 运行中、表单 dirty、OTA 独占期、维护模式切换时返回 `allowed=False, reason=...`，不静默切页。

## 6. Store 与 ConsoleUiVector

```python
@dataclass(frozen=True)
class ConnectionSlice:
    backend: str
    port: str
    connected: bool
    tcp_listening: bool
    transport_mode: str          # single_port_refmem / maintenance_multiport / offline
    occupied_ports: tuple[str, ...]
    exit_criteria_visible: bool

@dataclass(frozen=True)
class MaskSlice:
    target_mask: int
    required_mask: int

@dataclass(frozen=True)
class RunSlice:
    busy: bool
    workflow_id: str | None
    workflow_step: str | None
    workflow_state: str | None
    error_code: int

@dataclass(frozen=True)
class AckSlice:
    seq: str
    state: str
    ack: str
    nack: str
    timeout: str
    reason: str

@dataclass(frozen=True)
class UiSlice:
    revision: int
    status_text: str
    selected_item: str | None
    dirty: bool

@dataclass(frozen=True)
class ConsoleUiVector:
    connection: ConnectionSlice
    mask: MaskSlice
    run: RunSlice
    ack: AckSlice
    ui: UiSlice
```

路由状态（`page_id`）单独放 `RouteState`，不塞进业务 Vector。

规则：

- Store 只允许 Presenter/WorkflowAO 更新；Page 只读。
- `workflow_id/step/state` 在 Store 中只是**投影**；权威事实是 workflow state 文件/WorkflowAO。Store 不得成为第二 workflow owner。
- Page 通过 `selectors()` + `render(vector)` 订阅所需 slice；`revision` 变化时按 slice 比较，不做全页重绘。
- Tk `StringVar/BooleanVar` 只是**只读适配器**：Store→var 只在值变化时写；写入期设 suppress 标志；trace 回调只发 Intent，不反向改 var。
- Page `dispose` 必须取消订阅；worker 不直接写 Store。
- 需要跨页共享的只放摘要；大块数据（日志、镜像、矩阵）不进入 Store。
- `ConnectionSlice` 与 `MaskSlice` 必须覆盖连接后端/端口/连接态、target/required mask；这些是跨页核心事实。

## 7. Intent 与 Presenter

Intent 是 UI 的唯一出口：

```text
Connect / Disconnect
Query
RunWorkflow / PauseWorkflow / ResumeWorkflow / CancelWorkflow
OpenPage / Confirm / Dismiss
```

- Page 只构造 Intent，不拼 SCPI、不碰线程、不判断权限。
- Presenter 做参数校验、前置条件、权限、维护模式、审计。
- 命令类 Intent 与查询类 Intent 分开（CQRS-lite）；查询不应触发设备写。
- 结果通过 Store 和 UiEvent 回页面，不通过 `messagebox` 返回值。
- Presenter 可无 Tk 单测；这是 Passive View 的核心收益。

## 8. UiPump 与线程模型

| 线程 | 所有权 | 禁止 |
|---|---|---|
| Tk 主线程 | 所有 widget、Store 订阅、Router | 阻塞 I/O、`sleep`、长计算 |
| Dispatcher 线程 | 唯一 transport 会话 | 碰 widget、直接改 Tk 变量 |
| Workflow 线程 | 单条 workflow 执行 | 碰 widget、并发第二条 workflow |
| Worker/池 | 解析、文件、导出等短任务 | 直接 `after` 或写 Store |

规则：

- Worker 只 `post(UiEvent)`；UiPump 在主线程 `after(interval, drain)` 有界消费。
- `UiEvent` 类型：`progress / result / error / log / state_changed`。
- 任务带 `cancel_token`；关闭窗口先 cancel，再 `join(timeout)`。
- `after` 回调必须可取消；页面销毁时 `after_cancel`。
- 不对 UI 线程做 `join`，不在 UI 线程等网络/串口。

`UiPolicy` 统一定义（数值由配置/代码符号给出，不手写）：

- 每 tick 最大消费事件数、队列高水位、单 tick 最大耗时。
- 日志上限与丢弃策略：可丢最旧 `log`/`progress`；**不得丢** `error`/`result`/`state_changed`。
- 队列高水位时反压：暂停 worker 投递或合并进度，不阻塞 Tk 主线程。
- 关闭时等待 timeout；超时记录未退出任务，不无限阻塞。

## 9. DialogService 与安全门

```python
class DialogService(Protocol):
    def confirm(self, title: str, message: str, danger: bool = False) -> bool: ...
    def error(self, title: str, message: str, reason: str | None = None) -> None: ...
    def progress(self, title: str, cancellable: bool = True) -> "ProgressHandle": ...
```

- Page 禁止直接 `messagebox`；只有 Tk 实现层可以调用。
- 长操作使用非阻塞 `Toplevel`/进度面板，不用 `wait_window` 卡死主循环。
- 危险操作（STOP/BOOT/COMMIT/CLEAR/OTA/CAL:SAVE）由 Presenter 调 `confirm`，并记录操作者/模式。
- 测试注入 `FakeDialogService`，可断言“是否确认过”，不弹真窗口。

### 9.1 单端口独占期

单端口只有一个 command 队列，必须显式仲裁：

```python
class UiResourcePolicy:
    modal_owner: str | None      # 同时只允许一个模态对话框
    exclusive_job: str | None    # OTA / boot / calibration save 等长任务
```

- `exclusive_job` 持有者拥有 command 队列；OTA 是最常见持有者。
- 独占期内，GUI/TCP 的非独占命令**静默丢弃并写 `status_text`**，不弹模态；自动刷新（如 ACK 页）暂停。
- 优先级：安全 STOP/ABORT > `exclusive_job` > 手动 workflow > 自动轮询。
- `exclusive_job` 必须在完成/失败/取消时释放；`can_close` 只检查并返回 reason，不弹窗。
- 不允许用 `ota_busy` 布尔 + 每个页面各自 `messagebox` 的方式代替该策略。

## 10. WidgetKit、主题与 DPI

WidgetKit 统一以下控件：

- `FormField`：标签、校验、错误文案、单位。
- `CommandTable`：命令目录、目标 mask、参数表单。
- `LogView`：分级、过滤、行数上限、复制/导出。
- `StatusBar`：连接、模式、workflow step、busy、错误。
- `BusyOverlay` / `ProgressPanel`：可取消进度。
- `ErrorBanner`：页面级错误，不弹模态。
- `ConfirmDialog`：危险操作确认。

约束：

- 主题用 token（颜色/间距/字号），不硬编码像素；`tk scaling` 统一 DPI。
- 表格/日志用 model 驱动（Qt Model/View 思路），不把数据塞进 widget。
- 图片保留引用，避免 GC；日志有上限；长文本不无限追加。
- ttkbootstrap 只作为主题/控件底座候选；引入前做依赖/许可证评估，不让页面直接依赖它。

### 10.1 可访问性与 i18n

- 所有可交互控件有键盘焦点顺序、Enter/Esc 语义和可见焦点环。
- 状态不能只靠颜色表达；必须同时有文字/图标/`status_text`。
- 用户可见字符串进字符串表，不散落硬编码；错误 reason 与展示文案分离。
- 字体/间距跟随 DPI 与主题 token；不做固定像素布局。
- 破坏性确认框必须可键盘操作，默认焦点在“取消”。
- 屏幕阅读器/全量 i18n 不是首版目标；首版只做键盘可达性、字符串收口和错误文案分离，屏幕阅读器/高对比度作为后续增强，控件命名预留 `label/description`。

## 11. 错误边界、日志与设置

- 每个 Page 安装错误边界：捕获异常 → `ErrorBanner` + 结构化日志 + 可选 receipt，不让整个 App 崩溃；同一 key 限流，避免错误风暴。
- `report_callback_exception` 由 Shell 安装；错误处理自身再抛异常时只写日志，不再弹窗。
- 日志分级、带 page/workflow/run_id；敏感信息（密钥、凭证）不入日志。
- 设置保存窗口几何、主题、后端、最近端口、最近页面；不保存密码/私钥。
- 设置文件必须有 schema 白名单；读取到数组/未知字段/类型错误时回退默认，不启动即崩。
- 设置写入用“临时文件 + 原子 replace”；损坏文件备份为 `.bad`，不静默覆盖。
- 崩溃日志写到可定位目录，附版本、OS、Tk 版本、最近事件。

## 12. 测试与打包

- `FakeScheduler`：同步执行 `after`，测试不等待真实时间。
- `FakeDialogService`：记录确认/错误调用。
- `FakeControlPlane`：模拟单端口/多端口/失败/超时。
- Page 契约测试：`build/render/on_show/on_hide/can_close/cancel/dispose`。
- Presenter 测试：参数校验、权限、导航守卫、错误映射。
- Tk smoke：注入 `FakeScheduler` + 手动 drain，只断言 widget 存在、`render` 不抛；不替代 Presenter 单测。
- Workflow headless：无 GUI 也能跑，GUI 只是视图。
- 打包：`python -m` entrypoint、版本号、依赖检查、启动失败可诊断。
- 测试禁止 `sleep` 等真实时间；用 fake scheduler/clock。

## 13. 与 HAOFV 的映射

**可照搬的语义**：

| HAOFV | CONSOLE-TK |
|---|---|
| `UiVector` | `ConsoleUiVector` + `RouteState` |
| Function Block / ECC | `PageFB` + `Workflow Step ECC` |
| 表驱动 | `PageSpec`、快捷键表、错误码/文案表、命令目录 |
| 单写者 | Presenter 写 Store；WorkflowAO 写 workflow state；Shell 只写 layout/theme/geometry，不写 Store |
| fail-closed + reason | ErrorBanner + reason code + 日志/receipt |
| Service Layer | `WidgetKit` / `DialogService` / `ThemeService` / `Transport` |

**仅类比，不能照搬**：

| HAOFV | 差异 |
|---|---|
| `UiAO`（固件 AO） | Console 的 `UiAO` 对应 `WorkflowAO + Dispatcher`，不是 Tk Shell+Router；Shell 无 ECC/事件队列 |
| `Resource Arbiter`（PIO/DMA/Flash） | Console 只有 `UiResourcePolicy`：`modal_owner` + `exclusive_job`，不仲裁固件资源 |
| 硬实时预算 | Console 是软实时；只定义队列/UI 超时，不承诺 core1 deadline |

## 14. Tk 专属约束与反模式

| 约束 | 说明 |
|---|---|
| 单 UI 线程 | Tk 不是线程安全的；worker 只能 queue |
| `after` 有界 | 不每 tick 全量刷新；按 revision 增量 |
| 页面懒加载 | 不在 `_build` 一次性创建所有页 |
| 对话框非阻塞 | 长操作不要 `wait_window` |
| 日志有上限 | `Text` 限制行数/字节 |
| 关闭安全 | `WM_DELETE_WINDOW` → guard → cancel/join |
| 异常边界 | 每页 try/except + 全局回调异常处理 |
| DPI/焦点 | `tk scaling`、Tab 顺序、快捷键统一由 Shell 管 |

图片引用规则在引入图片资源时再补；当前 Demo 不使用 `PhotoImage`，不提前设计。

反模式：

- Page 直接 `messagebox`、直接 `Dispatcher.request`、直接 `StringVar` 当业务状态。
- Page 裸调 `self.after`（必须经 SchedulerPort/timer registry）。
- Worker 调 `after` 或改 widget。
- 在 UI 线程 `sleep`/等串口。
- 所有 tab 启动时全建。
- 一个页面同时做 UI、流程、证据。
- 用全局可变单例代替 Store。
- 引入完整 MVVM/Redux 框架却没有对应测试。

## 15. 迁移顺序

当前 Demo 是 legacy：Page 直接继承 `ttk.Frame`，状态散在 Tk 变量，对话框直接 `messagebox`，OTA 是 ad-hoc 线程。迁移采用 strangler，不做大爆炸重写：

0. `LegacyPageAdapter` 包住现有 tab，选 ACK 页做试点（它的自动刷新/OTA 冲突最典型），先验证 Page/Store/UiPump 契约。
1. `ConsoleUiVector` + Store：先收口 UI 状态（连接/掩码/运行/ACK/UI 五个 slice）。
2. `PageSpec/Router/Page`：懒加载 + `NavigationDecision` + 导航守卫。
3. `UiPump`：统一 worker → 主线程，替换各页裸 `after`；加 timer registry。
4. `DialogService`：收口 `messagebox`，支持测试注入；OTA 独占期不弹模态。
5. `WidgetKit` + theme/DPI：统一表单/表格/日志/错误条。
6. `ErrorBoundary` + `SettingsService` + `LogService`。
7. 迁移 OTA/Workflow 页面，移除/归档页面内重复的线程/对话框逻辑。

## 16. 验收

- 文档：`docs_check --strict-names`、`doc_regression_check` 全绿。
- 页面：Page 契约测试全绿；页面不直接 `messagebox`/`Dispatcher`；无裸 `self.after`。
- 状态：Store 单写者；Page 只读；revision 增量刷新；Tk 变量有 suppress 防回写环。
- 线程：worker 不碰 Tk；关闭窗口 cancel/join；`after` 可取消；TcpBridge shutdown 不在 UI 线程。
- 导航：workflow 运行/表单 dirty 时 `can_close` 返回 `NavigationDecision(allowed=False, reason=...)`。
- 独占：OTA 期间非 OTA 命令静默丢弃并写 status，不弹模态；多端口模式在 UI 可见。
- 测试：FakeScheduler/FakeDialog/FakeControlPlane；无 `sleep`。
- 设置：schema 校验 + 原子写；损坏文件回退并备份 `.bad`。
- 打包：`python -m` 可启动；版本/依赖/崩溃日志可诊断。
