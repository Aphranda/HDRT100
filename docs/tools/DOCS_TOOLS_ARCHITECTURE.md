# 工具域治理架构

Status: Draft
Domain: Tools
Canonical: `docs/tools/DOCS_TOOLS_ARCHITECTURE.md`
Related: `docs/tools/DOCS_TOOLS_TODO.md`, `docs/tools/DOCS_TOOLS_TASK_PROGRESS.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`, `tools/README.md`
Last updated: 2026-09-21

## 文档接口与范围

本文定义工具治理方案与责任边界；任务状态只在 [TODO](DOCS_TOOLS_TODO.md)，实施及验证证据只在 [Task Progress](DOCS_TOOLS_TASK_PROGRESS.md)。本版为治理草案，不登记新产品契约；已落地入口与后续设施分别列明，登记不代表支持资格或工作流迁移完成。

`docs/tools/` 是独立文档域，管理仓库工具的身份、入口、依赖、生命周期、复现与交接；根目录 `tools/` 继续存放实现。工具服务的业务语义、精度门限和设备 owner 仍归 VDC、TDMA、SYNC、OTA 等业务域，验证方法归 `docs/validation/`，通用文档门禁归 `docs/check/`。工具域不复制这些事实源。

## TG-IDENTITY：工具身份与事实源

现有入口见 `tools/README.md`；存量问题与盘点快照见进度记录，目标设施的实施状态以 TODO 为准。

目标是让接手者通过仓库、声明依赖及显式证据输入即可定位、运行和复核工作流，不依赖聊天记录、个人绝对路径或未登记脚本。先治理身份与依赖，再分批整理目录。

| 管理对象 | 唯一事实源与职责 |
|---|---|
| 工具登记 | 根目录 `tools/tool_registry.json`；稳定工具组 ID、入口、归属、状态、依赖及验证引用。组可包含多个明确列出的入口与公共模块；不把文件数、目录数或组数等同逻辑工具数。 |
| 登记结构 | 根目录 `tools/tool_registry.schema.json`；字段与版本检查。 |
| 使用导航 | 根目录 `tools/README.md`；目录部分从登记生成，操作说明留在工具自身 README，不人工维护重复状态。 |
| 工作流输入 | `config/tool_workflows/`；步骤、输入、资源、固定判据、检查点及失败恢复。internal-seal 联合采样已落地首份受版本控制的输入清单，其他场景仍须分别登记。 |
| 工作流生命周期 | `tools/workflows/<workflow>/` 中的 tracked owner；父流程负责设备会话、STOP barrier、子进程、恢复及结果汇总。VDC joint outer 生命周期已迁入，child Probe/adapter 仍按清单逐批收敛。 |
| 执行证据 | `out/` 下独立运行目录；原始数据、日志、实际配置、指纹和结果，不能反向成为正式工具的代码依赖。 |

## TG-LAYOUT：目录规划与增量迁移

目录按责任与依赖方向组织，工具身份由登记 ID 保持稳定。下面是目标布局；按需创建有实际内容的目录，不先建空骨架。现有路径继续以登记表为准。

| 目标位置 | 放置内容与边界 |
|---|---|
| `tools/README.md`、`tool_registry.json`、`tool_registry.schema.json` | 根导航、工具身份与登记结构。根目录不再新增独立业务脚本；现有根入口作为迁移兼容路径单独登记。 |
| `tools/domains/<domain>/<tool>/` | 单域工具及私有模块；domain 沿用责任域，如 tdma、vdc、sync、trigger、refmem、calibration、communication、interface、storage、flash、rtos、watchdog、ota。DPLL 仍归 VDC，不另造平行责任域。 |
| `tools/common/<capability>/` | 已有多个调用者的公共协议、解码、设备传输或证据处理能力；纯离线模块与设备操作模块分开，导入不访问设备。不设无归属的 utils 杂物目录。 |
| `tools/workflows/<workflow>/` | 跨工具/跨域编排，管理设备占用、启动、采集、STOP、恢复与证据汇总；业务算法仍由域工具负责。 |
| `tools/build/<tool>/` | 构建驱动、构建信息、资源预算和发布检查；固件包格式与 OTA 逻辑仍归对应责任域，不因命令用于发布而混放。 |
| `tools/host/<tool>/` | 主机辅助和文档转换等非板端工具；主机状态与个人配置另存运行目录。 |
| `tools/archive/<domain>/<tool>/<revision>/`（按需建立） | 已迁移旧实现的只读历史归档；记录来源版本/哈希、替代入口、依赖和适用证据。登记为 ARCHIVED 条目并关联原工具 ID，不作为正式运行依赖；旧路径需要兼容时只保留转发入口。 |
| `tools/hardware_acceptance/`、`docs_check/`、`tool_governance/`、`checks/`、`tests/`、`bench/` | 保留现有稳定入口：硬件验收、文档检查、工具治理、代码检查、测试启动器、交互台架。`tools/doc_regression_check.py` 同样保留；不为目录美观改动门禁和既有调用契约。 |
| `tests/python/` 等现有测试树 | 工具测试与小型冻结 fixture；`tools/tests/` 只承担已有测试启动器职责，不复制测试套件。 |
| `config/tool_workflows/` | 可复用工作流参数和台架配置；现有 `config/hardware_acceptance/` 保留。配置不存凭据，不用个人绝对路径，不包含可执行 Python；首批 internal-seal 清单绑定完整历史可执行闭包、输入摘要与板卡顺序。 |
| `out/<task>/<run>/`；硬件证据沿用 `out/HardwareAcceptance/<date>/<run>/` | 日志、波形、结果、缓存、运行状态和一次性探索产物。正式工作流不执行其中代码；历史原件不因目录治理迁走。 |

归类时先判断是否为跨域编排，再判断是否为已有调用者共享的能力，其余归业务 owner；名称含 capture、decode、validate 不构成新的一级分类。一个工具可有多个明确入口，不按每个脚本机械拆目录；跨域共享实现只有一个落点，通过登记依赖复用。

已落地 `tools/tool_layout.json` 作为版本化布局基线。治理检查允许标准根与稳定基础设施根在完成登记后增长；根文件必须在白名单中，其他一级目录及其当前文件路径作为遗留存量冻结。新增根文件、新增未分类一级目录，或在遗留根中新增文件，即使已经登记归属也会失败。基线可随迁移缩减；扩大遗留例外必须显式修改该文件接受评审，不能借登记绕过分类。

单工具目录只保留实现、README、必要的依赖声明和随版本发布的资源。README 说明输入输出、运行前提、资源、副作用、命令和恢复方法；架构与任务状态回链 docs/tools，避免重复三份治理文档。目录名使用小写下划线，按稳定能力命名，不使用日期、个人名、final、new 或 v2 表示迭代；接口/证据版本显式管理。EXPERIMENTAL 是登记状态，具备长期保留价值的实验代码也归对应工具，不新建无限增长的 temp/sandbox 工具池。

依赖方向为工作流调用域工具与公共组件、域工具调用公共组件，公共组件不反向依赖业务工作流；模块 import 不启动设备。迁移时显式确定包/CLI 入口与仓库定位方法，避免依赖当前工作目录或新增 sys.path/全局 monkey patch；标准库同名目录不能靠修改搜索路径解决。包装入口保持参数、退出码和异常清理语义，不能复制第二套实现。

### 首批路径决策与当前落地

| 当前入口或来源 | 目标决策 | 迁移约束 |
|---|---|---|
| `tools/vdc_priority_trace/` 的联合采集入口及 internal-seal 基础 out 链 | 编排归 `tools/workflows/vdc_joint_capture/`；域内解码/探针归 `tools/domains/vdc/priority_trace/` | 先消除 out 可执行依赖；原入口兼容，参考中断注入单独显式选项。 |
| `tools/scpi_query/` | `tools/domains/interface/scpi_query/` | 已迁移实现与命令资源；旧 Python 入口兼容转发，旧资源暂留，迁移前实现按原字节归档。 |
| `tools/sd_trace_decode/` | `tools/domains/storage/sd_trace_decode/` | 已迁移唯一离线实现并更新两个调用者；旧入口兼容转发，迁移前实现按原字节归档。 |
| `tools/vdc_sync_metrology/` | `tools/domains/vdc/sync_metrology/` | 已迁移唯一离线实现并更新 receipt provenance；旧入口兼容转发，保持输入 schema，不混入设备采集。 |
| `tools/vdc_dpll_replay/` | `tools/domains/vdc/dpll_replay/` | 已迁移离线 Python 实现与同目录 C runner；旧 Python 入口兼容转发，迁移前 Python/C 原字节归档。仅允许隔离主机进程内 diagnostic replay，不授予 calibration、firmware lock 或硬件验收资格。 |
| `tools/scope_dpll_capture/` | DPLL 分析归 `tools/domains/vdc/scope_capture/`；确认复用的 RAW/SCPI 设备接口才抽入 common | 不把现有 NORM 采集冒充联合采集的冻结 RAW 实现。 |
| `tools/tdma_ring_monitor/`、`tools/calibration_ring_validate/` | 分别归 domains/tdma 和 domains/calibration 的工具目录 | 先复核动态加载、相对路径和 P3 调用；不整组盲搬。 |
| `tools/scpi_common/` | `tools/common/scpi/` | 已迁移 serial lifecycle、board identity 与 OTA timing 唯一实现；全部仓内调用者改用 canonical import，旧 qualified/bare 包名保持 package/child 对象身份，迁移前实现按原字节归档。 |
| `tools/cmake_build_auto/`、`tools/codex_watchdog/` | 分别归 build 和 host | 构建定位、运行状态与恢复方式须回归。 |
| OTA、Flash 工具及全部稳定验收/门禁入口 | 仅登记分类，当前原位保留 | OTA 本轮不改；路径调整不得借机改变刷写行为。 |

完整旧路径→目标路径→调用者→兼容方式→验证→回滚映射由逐批迁移任务维护；上表不是已执行搬迁清单。迁移优先级为主线工作流依赖闭合、公共叶模块、对应域入口，最后评估低收益的纯路径整理。单批同时更新登记、生成导航、调用方与测试，执行同指纹 P3；通过前不推进下一批。旧入口保持可用，退役仅作归档，不因标记 ARCHIVED 自动搬动历史文件。

用户要求（2026-09-20）：迁移完成后旧代码不得删除，只能归档。覆盖旧实现为兼容包装前，必须保留原字节归档并核验来源 SHA；禁止用 Git 历史仍可找回代替归档交付。out 中已冻结的历史脚本及证据原位保留，通过迁移来源清单登记原位归档，不改写历史文件。归档不代表可以删除原始证据，也不让归档代码成为正式工具的反向依赖；如仍有旧调用者，先保留并登记过渡关系，闭合后再完成退役。

待删除候选不得直接删除：只可移动到归档区内明确的待删除目录并维护清单，最终删除决定由用户评审。

### 按完成度逐批入位

按用户要求，确认完成的工具逐步迁入标准规划目录。分开判断“源位置的工具已具备迁移条件”和“目标位置已验证交付”：前者要求用途/owner、实际调用者与运行依赖、配置/输出、设备副作用/恢复已核清，相关回归和适用验收已有证据；仅有文件登记、一次运行成功或目录名合规不算确认完成。当前 UNASSESSED 存量逐组核验，不能整组默认授予迁移资格。

每批依次执行：确认迁移条件和映射，迁入目标位置并保留兼容入口，同步登记/导航/交接说明并核对新旧命令及依赖，运行相关回归和同指纹 P3，独立复核后提交。目标路径交付完成才关闭本批迁移；SUPPORTED 仍须另满足完整交接准入，目录入位不会自动提升生命周期。未完成工具原位登记待办；out 中需长期复用的实验链先完成依赖和行为收敛，再作为已登记工具迁入，不长期依靠临时代码维持正式工作流。

## TG-OWNER：职责与 HAOFV 边界

| 角色 | 职责 |
|---|---|
| 工具维护角色 | 入口、依赖、兼容性、测试及操作说明；责任按域/角色登记，不绑定临时 agent。 |
| 业务域 owner | 被测行为、配置语义、精度与产品资格判据。 |
| 工作流维护角色 | 工具组合、板卡身份、资源占用、STOP/RUN 顺序、证据闭合及恢复。 |
| 独立复核者 | 检查声明、实际依赖及验证范围；主控核对后交付。 |

工具经既有 SCPI/维护接口工作，不绕过固件 owner。明确区分离线处理、设备查询、配置、输出与刷写；记录每类操作的运行前提。设备按 UID 识别，COM 口是运行时映射。共享串口、示波器等资源须声明占用和释放，不允许工作流并发争用。

治理不新增 Core1 发布工作、实时观测或特等席。健康诊断镜像遵循既有 Core0 异步边界；席位增设/扩容须用户审核，见 `EXE-SEAT-01`。

## TG-INTERFACE：登记与接口

每个工具登记稳定 ID、用途、所属域、维护角色、生命周期、推荐入口及兼容别名。按类别补充输入/输出结构版本、配置与环境变量、运行依赖、操作影响、STOP/RUN 条件、资源、退出码、测试和适用验证证据。

新增前检索已有能力，说明新增理由；相同用途优先复用或配置化，确需并存时说明差异。公共组件显式登记依赖；测试、包装入口、资源文件归属对应工具或组件，不能遗漏，也不强行视作独立工具。

正式支持的入口不得执行或导入 `out/` 中的代码。历史证据可作为显式输入，须声明来源、哈希、获取方式及缺失时行为。实验适配器成为正式工作流依赖前，应纳入版本管理并验证。输入证据与可执行插件必须分开处理。

迁移期间仍需执行历史 adapter 时，入口必须在首次动态 import 和硬件访问前核验该场景的完整可执行闭包，而不是只核验最外层脚本；受 Git 跟踪的依赖继续由同指纹 P3 覆盖。baseline 等数据输入同时校验摘要和实际消费字段，receipt 同时校验 schema、证据引用、源码指纹和设备顺序。父子流程在 `run()` 前分别持久化本次清单与摘要，使启动、配置或清理早期失败仍可回溯准入输入。

outer 生命周期不得通过 adapter 的多层属性链取得串口 backend 或恢复 runner。它使用实例级响应匹配器，先向所有可达板发送 output STOP 和 TDMA STOP，再进行任何查询；临时 RAM timing/delay 的 dirty/uncertain 状态只在最终逐板 identity、invariant、读回与错误队列全部通过后清除。报告保留兼容 schema 并另记 lifecycle 版本和源摘要。此流程不写 Flash、不 reboot、不因 child 成功而省略 STOP/恢复。

child adapter 的逐层迁移使用固定角色 lineage：兼容 selector 只可指向 archived internal-seal 原件；已迁移 source 继续按角色、路径和 SHA 保留，但不得留在 runtime closure。tracked adapter 从 manifest 指定的下一未迁移入口构造，不能回退执行 compatibility selector。每批用 frozen native bytes 对 archived/tracked parser 与 verdict 做 golden，并对 configure、命令改写、quiet RUN、STOP collect gate 和 cleanup 后重算做直接 characterization。

runtime control/START 基座同样归 tracked workflow：只加载 manifest 指定的剩余 RunProbe 入口，四板 allowlist 来自已准入清单；parallel START 只使用已验证且互异的 persistent session，全部 worker join 后统一落 JSONL，任一非 OK 只能在全部 join 后失败并交给外层 STOP。已由 tracked outer/scope 替代的 monitor、startup、late-scope、Trial/HIL 和 reader 原件转为命名归档来源，不得继续作为 import 副作用加载。

RUN output 层由 tracked workflow 唯一管理 timing snapshot/apply、RUN prepare、output STOP 和 STOP 后 schema 校验；外层 lifecycle 是 RAM timing/delay 恢复的唯一 owner。历史 RunProbe 内部示波器 owner 在本工作流中 fail-closed 禁用，物理采样只走独立 tracked sparse-scope 组件，防止双 owner 或旧 reader 重新进入 child import 链。

phase 层由 tracked workflow 管理 phase status 解码、phase/origin trace ARM、STOP 后 native trace 下载/解码、phase/frequency endpoint 一致性和 trace release。只有完整读取的 trace 才允许 release；下载/解码失败的 trace 必须保留用于恢复，endpoint 或 release 错误必须进入最终 errors 并压低 passed。历史 phase 原件只保留为 migrated lineage，下一层 long-window ancestry 仍由 manifest 固定路径和 SHA 准入。

long-window 层由 tracked workflow 管理反向四板 START、临时 origin trial、有限静默窗口、rational secant 向外取整、同 baseline/model 的 threshold progression 和最终 correlated projection。长区间只是观测，不自动成为 lock 或精度资格；单 follower oracle 失败独立记错并压低 passed。历史 long-window 原件只保留为 migrated lineage，下一层 native trace ancestry 仍由 manifest 固定路径和 SHA 准入。

native trace 层由 tracked workflow 管理 status/ACK bounded polling、三个 follower ARM、STOP/download/decode、match/decision coverage、跨度和 last-decision endpoint 一致性，以及 release/retain 恢复。reference 不走 follower ARM；未完整读取的 trace 不 release，已完整读取的 trace 即使后续 endpoint 失败也必须尝试 release。历史 trace 原件只保留为 migrated lineage，下一层 follow ancestry 仍由 manifest 固定路径和 SHA 准入。

local follow 层由 tracked workflow 管理 signed/wide status 解码、独立 frequency decision 检查、follower enable/disable readback、STOP-only status snapshot 和 model/DCO endpoint 一致性。reference 不启用 follower mode；三个 follower 的 assessment 相互隔离，任何单板 parser/decision/model 错误都进入 errors 并压低 passed。历史 follow 原件只保留为 migrated lineage；observer 与 exact-match 已由 tracked workflow 接管，剩余 direct-RX ancestry 仍由 manifest 固定路径和 SHA 准入。

observer snapshot 层由 tracked workflow 提供 all-board STOP 后的四板 TDMA event snapshot-before-parent-collect 语义。internal-seal 的 summary collect 由更上层 tracked owner 接管，不调用此深层 collect；因此 P3 只证明 tracked observer 进入实际 MRO/provenance 且联合流程无回归，standalone snapshot 顺序由直接 characterization 证明，不把未发送的 snapshot 查询写成硬件证据。历史 observer wrapper 只保留为 migrated lineage，并委托 tracked exact-match 层。

exact-match 层由 tracked workflow 管理 match status 解码、按已装载 path matrix 独立重建有向 delay、可选 TAP 安装/恢复、三个 follower 的 MATCH/session 启用读回，以及 STOP 后 origin、match、model、event、TAP 和 recovery 证据采集。parent transport assessment 失败仍须记录并继续逐 follower 核验；单 follower 的 identity/path/interval 错误不得阻断其他 follower。internal-seal 不调用此深层 collect，因此 P3 证明 tracked match 进入实际 MRO/provenance 和联合流程无回归，standalone TAP/MATCH/collect 语义由直接 characterization 与冻结 golden 证明。历史 match 原件只保留为 migrated lineage，并委托 tracked direct-RX 层。

direct-RX 层由 tracked workflow 管理 direct/lane/timing/budget 解码、冻结 timing baseline 的独立差分、retained transport header 与 mailbox 的 CRC/identity/payload 核验，以及 direct、master 和较慢 polled consumer 之间的 event 比较。四板 STOP 后先持久化所有原始 snapshot，再逐 follower assessment；单 follower 的 parser、计数、header、mailbox 或 payload 错误不得阻断其他 follower。不同 identity 的最终 retained event 不视为损坏，同 identity 的 payload 不一致必须拒绝。历史 direct-RX 原件只保留为 migrated lineage，并委托 tracked typed-TX 层。

typed-TX 层由 tracked workflow 管理 typed mailbox CRC/target/generation/interval 解码，以及 child 的 receipt/source/UID/build 准入、四板会话、stage 复用、load/FIFO/TX prime、SYNC、反向 ARM/START、TRIAL、STOP collect、disable/restore/revoke/close 和最终 verdict。transport query registry 必须从 tracked base 的实际 backend module 解析并保持对象 identity，禁止以第二份包导入或 `sys.path` 隐式配置注册到错误实例。cleanup 对 final STOP、collect、逐板各恢复项、revoke 和逐连接 close 分别隔离；任何单项失败均须留证，连接 map 清空及两个 board-command owner 恢复必须位于无条件 finally。历史 typed-TX 原件只保留为 migrated lineage，并委托 tracked base。

base 层由 tracked workflow 管理 allowlist、query barrier、总工作量/文件读取上限、JSONL action 持久化、control/trace ACK、全板 STOP、load mask、TDMA/trace export、pipeline snapshot 和基础 lifecycle。它从已加载的 calibration owner 解析唯一 TDMA backend，不修改 `sys.path`，import 不枚举或打开设备。standalone run 只关闭自身打开的连接；final STOP、collect、逐板恢复、revoke 和 close 分别隔离，三个全局 owner 必须无条件恢复。历史 base 原件只保留为 migrated lineage；internal-seal manifest 的 runtime entrypoint 为 null、runtime closure 为空，正式运行不再执行任何 `out/` Python。

## TG-LIFECYCLE：生命周期与迁移

| 状态 | 准入或退出条件 |
|---|---|
| UNASSESSED | 存量接管的登记状态：已分配归属但未完成运行条件、依赖和交接复核；不等于已支持、实验失败或废弃，不自动迁移到其他状态。 |
| EXPERIMENTAL | 有用途、责任和转正/退出条件，明确未支持范围；不能隐性成为正式依赖。 |
| SUPPORTED | 依赖闭合、操作说明与接口明确、适用测试通过、交接复现完成；不代表产品验收通过。 |
| DEPRECATED | 有替代入口、调用者清单和迁移方式，兼容期显式管理。 |
| ARCHIVED | 保留历史复现用途与获取入口，不作为新工作流默认依赖。 |

状态迁移记录理由、验证及替代关系；废弃不自动删除文件。存量先登记债务和退出条件，按工作流小批迁移，不一次搬动全部目录。配置/输出接口变更需声明兼容性，避免仅路径迁移却破坏调用方。

## TG-HANDOFF：结果、失败恢复与交接

每次运行记录工具及依赖版本、源码指纹、固件 build（适用时）、完整命令、生效配置、设备身份、接线、阶段日志、原始数据及哈希。必须记录失败、设备末态、清理/恢复结果和下一可执行动作；重跑使用新目录，不能覆盖旧失败。

结果分开表达执行成败、证据完整性、被测能力判定与清理结果。沿用 `INFO/WARN/ERROR/FATAL` 分级，判据在运行前固定；采集成功不能代替锁相通过，离线分析不能代替硬件验收。

P3 默认绑定同一组四板完成 OTA、TDMA 与 `p3_bench.json` 的 `dpll_short_seconds` 短闭环，不再要求 NO5。配置的 HDO 可枚举时联合稀疏物理采样；确认未枚举时 external 明确为 SKIPPED，并以 NO2--NO4 原生 summary trace 的稳态 residual extrema 包络分级，不再只看最终 endpoint。内部路径校验完整请求窗、终止/覆盖、每 bin 成功、`INTERNAL_MAX_SUCCESS_GAP_S`、最后稳态 bin 与 endpoint 一致性；`INTERNAL_SETTLING_BINS` 对应启动 bin 保留在证据中但不进入稳态精度分级。分页响应必须按 canonical 读序重组，并与 `summary.ram.bin`、原生解码结果一致；summary 还绑定源码指纹、build、四板 UID、session、generation、slot 与 node count。外部 `scope_samples[].phase.relative_ns` 与内部 owner residual 是不同测量域，不混合取值；内部包络不授予连续 GPIO 波形精度。相差分级引用 `p3_dpll_short.py` 的 `PHASE_WARN_NS` 与 `PHASE_ERROR_NS`；证据缺失、解析、内部健康、STOP 或恢复失败使用独立 evidence ERROR，不伪装成相差超限。最终 receipt 哈希绑定 summary、joint reports、内部原生 trace/分页记录，以及 scope-on 时的 scope records 和 RAW block，`check-staged` 离线复算后放行。

交接验收要求：新工作区可按声明环境和输入完成离线复算；具备对应硬件时可重跑工作流并安全恢复。证据包不得包含密钥或本机凭据。最近成功记录必须注明适用配置和版本，不能以日期新鲜替代兼容性证明。

## TG-VERIFY：自回归与验证映射

入口为 `tools/tool_governance/tool_governance.py`，提供 inventory、check、catalog；当前为显式调用，不自动接入 hook。检查器只做静态检查，不执行登记入口、不导入被登记模块、不打开设备；JSON Schema 外部引用不从网络获取。`--help`/模拟测试仅在明确安全的测试环境运行。

inventory 枚举 Git 跟踪及非忽略的新文件，记录字节哈希、角色、静态导入候选和待审位置。Python 用 AST，文本按 UTF-8 读取，不透明资源只记哈希；动态加载、shell/subprocess 调用须人工复核，`out/` 引用可能只是合法输出位置，不能一律判成代码依赖。已声明 depends_on 是确认的部分关系，dependency_review 为 PENDING 时，空列表不代表独立。

check 使用随仓库登记的 schema 校验字段、唯一身份、精确文件归属、入口角色、显式依赖、兼容别名、非空证明引用及 `tool_layout.json` 布局基线；不授予证明内容充分性。`--check-catalog` 复核根目录 `tools/README.md` 的生成块，catalog 只输出文本。所有命令只写 stdout，调用者将结果存入 out；其结果不是 staged 指纹或硬件凭证。新增非忽略文件未登记即报告错误，未分类顶层增长和遗留根文件增长独立报错，存量未评估状态保留告警。

下表是完整目标及落地顺序；当前已执行的结构检查以上述 check 入口为准。动态运行依赖、接口证明充分性和 hook 准入仍为后续工作，不能据此表宣称已经自动强制。

| 检查 | 目标判定与落地顺序 |
|---|---|
| 登记覆盖 | ID 唯一、入口存在、文件归属、状态及引用有效；先报告存量，约束新增/实质修改项。 |
| 依赖闭合 | 缺失依赖、个人路径、正式代码依赖 `out/`、未登记插件；明确错误阻断，启发式发现告警。 |
| 接口与交接 | 配置/输出版本、说明、测试和恢复步骤一致；SUPPORTED 必须有复现证据。 |
| 生命周期 | 正式工作流引用废弃入口、替代关系缺失形成告警与迁移任务。 |
| 门禁接线 | 检查器自身有正反测试；分阶段接入，不能把报告模式冒充已生效门禁。 |

现行工具/测试/hook 实现修改仍执行 `AGENTS.md` 与 `EXE-CHANGE-01` 的同指纹 P3；治理不自行豁免离线工具，也不扩大固定 P3 范围。仅文档变更执行文档门禁。工具规则、登记结构和跨域接口将来需要冻结时，再分别复核并登记适用契约。

## 跨域契约接口

上述 TG 标识仅是草案条款锚点，不是 DOCS_REGISTRY 契约 ID。本文遵循既有文档三件套、执行和特等席审核约束；工具登记不授予产品运行资格，也不改变任何域的 wire、SCPI 或时间预算契约。
