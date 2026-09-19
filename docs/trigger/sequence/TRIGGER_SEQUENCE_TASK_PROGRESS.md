# 节点序列预约触发任务进度

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/sequence/TRIGGER_SEQUENCE_TASK_PROGRESS.md`
Related: `docs/trigger/sequence/TRIGGER_SEQUENCE_ARCHITECTURE.md`, `docs/trigger/sequence/TRIGGER_SEQUENCE_TODO.md`, `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.html`, `docs/reports/distributed-trigger/相控阵测试系统RP分布式触发方案技术报告0804.html`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-09-19

## 文档接口

[Architecture](TRIGGER_SEQUENCE_ARCHITECTURE.md) 维护稳定设计，
[TODO](TRIGGER_SEQUENCE_TODO.md) 维护任务状态，本文件只追加执行与证据记录。
每条记录包含 progress ID、TODO ID、日期、变更/提交、验证、结果、证据及下一 gate。
本文件中的分支、提交、行号、测试结果与计数均为当次审计快照，非产品事实源。

### NSEQ-PROGRESS-20260916-016 - DUT/VNA 最小闭环角色与节点 SCPI

- TODO：`NSEQ-073`、`NSEQ-074`、`NSEQ-076`；日期：2026-09-16。
- 新增 `refmem_sequence_roles` 角色解析：DUT_LINK_CONTROL 接收单路推进脉冲并输出 SP8T 编码电平；VNA_GATEWAY 使用 INSTRUMENT_CONTROLLER 槽位输出触发脉冲并接收 READY；支持同板不同 RefMem 槽位、generation、角色重复和 IO 冲突校验。
- 新增 `CONF:SEQ:NODE:LOAD`、`CONF:SEQ:NODE:ACT`、`READ:SEQ:NODE:LOAD?`，复用现有 RefMem staging/activate/status 事务；序列运行期间返回 `SEQUENCE_RUNTIME_FROZEN`。
- 闭环接线定义：网分 READY 可通过线缆接入 DUT 选定 IN1–IN4，单个 READY 边沿只推进一个序列状态；DUT 不产生额外完成脉冲，DONE/READY 仅发布执行事实。
- 软件验证：Release 固件构建 `out/build/sequence-role-integration/` 完成，`docs_check --strict-names` 通过；尚未进行本次角色配置的板端线缆验收。

## 当前 Checkpoint

最新修复为进度044：独立外部PULSE模式先准备捕获，再发状态触发，反馈锁存至当前脉冲结束。
软件回归、独立复核和USBTMC固件包已完成；进度047补本地OUT4到IN1独立模式闭环及GUI观察实测。

`NSEQ-001/002` 已完成：实际链路和源码审计、标准三件套、自动门禁与独立复核通过。
用户已确认“一脉冲一序列状态”，对应链路切换及其他节点工作。
`NSEQ-010` 配置模型实现、行为测试、构建与独立软件复核通过。
早期范围为单节点调试、TDMA仅预留接口；当时已接通并在COM10验证
SCPI、编码输出、建立时间、完成脉冲和实际IO读取。SP8T全部地址及回绕通过，见进度008。
IN1已接外部信号并完成低频功能验证，见进度010；四板P3失败为历史事实，
不再作为本轮单节点开发前置条件，提交凭证也未伪造或绕过。
当前工作分支为 `feature/node-sequence-reservation-trigger`，创建基线为
`wip/tdma-real-flight-processing` 的 `7fa834df`（本次工作快照）。
配置、软件切步及IN1低频流程已通过；其余输入实际激励、独立波形和开关本体通路未验收，
不得据此宣称支持分布式节点预约。最新范围为同板 DUT/VNA 通过已接线 RJ45 物理回环协作，
保留独立 SP8T 和 DUT-only 模式；角色配置及同板 LINK 运行绑定已实现，见进度019/021。
已提交的单板验收检查点为进度043：独立SP8T、DUT/VNA和转台三槽位固定单板功能验收及暂停/恢复、
忙时保护、异常恢复通过，代码已提交为`a344e90a`，正式凭证绑定当前源码及真实VISA OTA。
Core1运行协调及动态PIO握手首切片已落地；NSEQ-101仍在迁移，运输优先路径尚未完成。
下一代码提交先关闭NSEQ-RISK-04角色激活诊断缺口；200ms位置周期、严格TDMA稳定性、
完整动态claim和多板验收尚未通过，不将功能检查点写成性能或P3通过。
物理回环基础验证及设备收尾状态见后续记录，不以早期端口和配置快照替代当前事实。

## 任务记录

### NSEQ-PROGRESS-20260915-001 - 指令表和现有序列路径基线审计

- TODO：`NSEQ-001`、`NSEQ-002`、`NSEQ-003`。
- 日期：2026-09-15。
- 变更/提交：新建 `docs/trigger/sequence/` 三件套并接入文档索引；此记录时未提交，未修改固件。
- 需求：按 SCPI 表先完成配置，再通过指令切换，最后接外部脉冲；用户补充输入须可选 IN1-IN4。
- 审计方式：主控与独立只读工作流 `sequence_command_audit`、`sequence_runtime_audit` 对照需求和代码；功能实施前先完成文档 gate。

审计证据（路径与位置为当次快照）：

| 位置 | 观察 | 影响 |
|---|---|---|
| 指定 HTML 的 P5/P6“序列配置指令/状态展开规则” | 状态参数自动展开，plan 保存 state_id 引用；角度脉冲执行整条 active sequence | 单步扩展不能静默改写角度语义，NSEQ-003 确定动作单位 |
| `middleware/scpi_port/inc/scpi_config_commands.h` 的 `SCPI_CONFIG_COMMANDS` | TRIG 参数、SEQuence 与 ACTive 写入挂接 `scpi_port_result_accepted` | 返回接受不代表已存储，需要 NSEQ-010/011 |
| `middleware/scpi_port/src/scpi_config_commands.c` | 参数、计划、MAP、CHECK、active 读回使用固定示例 | 配置验收必须验证真实读写和错误回滚 |
| `middleware/scpi_port/src/scpi_trigger_commands.c` | START/STOP/PAUSE/CONTINUE 的实现不足以驱动业务序列，状态回包仍有示例 | 必须接 Trigger runtime，NSEQ-020/021 |
| `middleware/scpi_port/src/scpi_realtime_sequence_commands.c` 的 `scpi_cmd_trigger_source` | source 使用历史 GPIO 范围校验 | 与当前产品输入符号不一致，NSEQ-030 改为逻辑映射 |
| `boards/rp2350_trig/inc/board_config.h` | 以 `BOARD_SYNC_TRIG_IN_PIN`、`BOARD_SYNC_ARM_IN_PIN`、`BOARD_SYNC_EXT_CLK_IN_PIN`、`BOARD_SYNC_GATE_IN_PIN` 映射逻辑 IN1-IN4 | 输入必须引用真实板级符号，不能猜测不存在的 IN1_PIN 宏或复制历史 pin 区间 |
| `components/sync_io/src/sync_io_mode_seq_step.c` 的 status 查询 | DMA 预填进度不等于物理输出完成步数 | NSEQ-020/021 必须建立真实 completion 与游标关联 |
| `components/sync_io/src/seq_step.pio` 与 gate 映射 | gate 与输入角色有潜在冲突，等待边沿前检测不等于命中瞬间仍有效 | NSEQ-003/030 明确冲突和门控采样语义 |

- 验证/结果：代码与 HTML 只读审计已得到上述缺口；文档检查、pytest、hook 和独立文档复核尚待执行。
- 证据位置：上述源文件与用户指定 HTML；后续命令结果统一写入 `out/node-sequence/`。
- 失败/风险：固定 SCPI 回包、GPIO 旧范围及 DMA 预填游标均不能充当功能通过证据。脉冲动作单位已向用户澄清，等待结果。
- 下一 gate：完成 NSEQ-001 文档检查与独立复核；NSEQ-002 审计收口后按 NSEQ-010 推进真实配置实现。动作单位未确定前不实现依赖的切步路径。

### NSEQ-PROGRESS-20260915-002 - 实际链路架构理解与标准文件修订

- TODO：`NSEQ-001`、`NSEQ-002`、`NSEQ-003`。
- 日期：2026-09-15。
- 变更/提交：按用户新提供的技术报告修订三件套；仍仅有文档改动，未提交、未操作板卡。
- 需求确认：用户明确“一个脉冲推进一个序列状态”“序列切换是链路切换，其他节点工作要进行”；后续要求先理解实际架构再完成标准文件。
- 理解结论：A3 接收主机配置/软件意图；A0 接收所选 IN1-IN4 脉冲并选下一状态；A1 切 DUT/链路，A2 切馈源/极化，A3 触发 VNA 并捕获 READY/MEAS_DONE。同状态采用共同基准、节点补偿和本地预约；DONE 不能串行充当下一板实时触发。
- 文档落实：增加角色与链路图、配置跨节点发布、接纳/执行/整步完成分离、捕获单 owner、资源共存门禁；TODO 增加 NSEQ-012/015，把节点协同闭环前置到软件切步阶段，外部输入只替换事件来源。

补充证据（当次源码/报告快照）：

| 来源 | 对照结果 |
|---|---|
| 技术报告 P7/P8/P10/P12/P17/P19 | A0-A3 分工、并行预约、DONE/MEAS_DONE、T2 有效性、A3 网关和硬件自治 |
| `components/distributed_config/src/distributed_config.c` 的 `s_role_map/s_loop_plan/s_action_map` | 存在静态角色/等待规则/动作模板；不等于可写计划库或实际设备动作已经接通 |
| `components/distributed_refmem/src/refmem_application_model.c` | FIRE_LOAD/DONE 模板及 link_sequence_state 只是基础结构，角色映射须与实际报告和部署配置核对 |
| `components/sync_io/src/sync_io_mode_seq_step.c` 与 `boards/rp2350_trig/inc/board_config.h` | legacy PIO 输出与当前 TDMA TX 资源重合，不能直接在 real-flight 运行中 ARM |
| `components/sync_io/src/sync_io_model_sched.c` | 有本地预约原语，但通道、observer 与共享 workspace 能力有限，需专门资源矩阵 |
| `components/vdc_dpll_manager/src/vdc_dpll_manager.c` 与 `components/sync_io/src/sync_io.c` | VDC 已消费 capture latch 队列；新增 Trigger 消费者会争抢，需要 owner 分发 |

- 验证/结果：主控已复核两条只读审计结论和真实 board 宏；文档门禁及独立审阅接下来执行。
- 失败/处置：初稿对输入宏使用了尚不存在的名称，现已纠正为真实 board 宏；初稿曾把节点预约留到外部输入之后，现改为软件切步阶段先闭合节点工作。前一条记录中的“动作单位待确认”已由本条解除。
- 证据位置：本条表列原始文件；后续自动检查写入 `out/node-sequence/`。
- 下一 gate：完成标准文件门禁与独立审核；随后按 TODO 执行配置闭环，不能提前宣称配置或链路切换已经可用。

### NSEQ-PROGRESS-20260915-003 - 文档门禁与独立复核

- TODO：`NSEQ-001`、`NSEQ-002`。
- 日期：2026-09-15。
- 变更/提交：标准三件套与索引已完成；仅文档工作，未提交、未构建、未操作设备。
- 独立审核：`sequence_command_audit` 只读对照技术报告、指令表和源码，确认角色分工、共同预约、DONE 非串行触发、游标分离、输入映射、capture owner、TODO 依赖和 C13 结构；首轮 FAIL 指出 legacy ARM 命令应为 `REALtime:ARM`。主控修正并补齐验证记录后，独立末次复核 PASS，无新增阻断项，结论仅覆盖标准文件交付。
- 测试首次失败：`python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider --basetemp out/node-sequence/pytest-20260915` 因父目录尚不存在而 setup 失败（当次快照：3 passed，15 errors）。建立输出目录后换用新 basetemp 重跑，未修改检查器/测试或覆盖已有目录。
- 实际命令环境：使用 Windows PowerShell 调用原生 Python 与 PATH 中 Git 的 `sh`；执行约束中的旧 Git Bash 绝对路径在此机器不可用，等效入口保留如下。

| 验证命令 | 结果（本次快照，非产品事实源） | 证据 |
|---|---|---|
| `python tools/docs_check/docs_check.py --strict-names` | PASS，140 文件，零命名/索引告警 | `out/node-sequence/pre-commit.log` 包含强制执行的同一检查 |
| `python tools/doc_regression_check.py` | PASS；保留既有 TDMA-FLIGHT-BITMAP-01 旧 ID 告警 | `out/node-sequence/pre-commit.log` |
| `python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider --basetemp out/node-sequence/pytest-20260915-r2` | PASS，18 passed | `out/node-sequence/doc-tests.log` |
| `$env:FORCE_DOC_GATES='1'; sh .githooks/pre-commit` | PASS，强制文档 gate；无固件范围变更，P3 run 未触发 | `out/node-sequence/pre-commit.log` |
| `python tools/doc_regression_check.py --log-check` | PASS with WARN：既有提交时间晚于旧 gate marker，可能存在历史绕过；本次不修改历史 | `out/node-sequence/log-check.log` |
| `python tools/hardware_acceptance/p3_hardware_acceptance.py check-staged` | PASS，无 staged code change；不是硬件验收通过凭证 | `out/node-sequence/check-staged.log` |
| `git diff --check` | PASS | 工作树差异检查；含新文件的结构另由 docs_check 覆盖 |

- 失败/处置：环境路径造成的测试 setup 失败已解决；历史 ID 与 escape-hatch 警告保留，不写成零告警。未注册新契约，未修改登记表状态。
- 下一 gate：NSEQ-001/002 已关闭；NSEQ-003 和配置实现仍按 TODO 待推进。

### NSEQ-PROGRESS-20260915-004 - 配置模型实施与设备接入核对

- TODO：`NSEQ-010`，尚未进入依赖它的 NSEQ-011。
- 日期：2026-09-15。
- 变更/提交：在 `feature/node-sequence-reservation-trigger` 增加
  `trigger_sequence_config.h/.c` 并加入 `CMakeLists.txt`；未提交、未合并。
- 模型：参数校验与状态展开、静态计划库、CHECK、active、逐项映射、冻结门禁和
  generation 失效；具体容量与 CRC 布局引用架构 NSEQ-A-03 和头文件符号。
  单 owner 模型不操作硬件，不增加 Trigger 状态或跨核发布；SCPI 固定响应本切片尚未替换。
- 构建：新建 `out/build/node-sequence-release`，避免旧工作区缓存路径触发清理。
  初始 configure/build 已生成 A/B 固件与 OTA 包；PowerShell 把正常 SDK stderr
  包装成 NativeCommandError，导致首次 shell 结果非零，完整日志保留。
  再次显式 `exit $LASTEXITCODE` 校验构建成功，Flash app A/B 与 boot 布局门禁通过。
  实际命令为 `python tools/cmake_build_auto/cmake_build_auto.py --preset pico2-release --build-dir out/build/node-sequence-release`；
  证据为 `out/node-sequence/nseq010-configure.log`、`nseq010-build.log` 和 `nseq010-build-confirm.log`。
- 设备核对：用户已表示硬件接上；本机串口及 PnP 实际只枚举 COM10，
  USB UID 为 `839E1AE79EA20F31`，按 `p3_bench.json` 对应观察板；主环 UID 未枚举。
  两次 `*IDN?`/`SYST:FW:BUILD?` 尝试都在打开 COM10 时返回 PermissionError，
  没有取得 SCPI 响应，也没有确认在线固件身份。不得把 USB 身份当作完整 SCPI 身份验收。
  已向用户反馈核对主环 USB/Hub 和端口占用，软件实现继续。
- 原始接入证据：`out/node-sequence/hardware-probe.log` 和 `hardware-ports.log`。
  未关闭用户进程、未修改串口驱动、未向观察板发送成功的配置或输出指令。
- 当前验证：文档结构检查与自回归 PASS，文档 pytest 当次快照为 18 passed，
  证据 `nseq010-docs-check.log`、`nseq010-doc-regression.log`、`nseq010-doc-tests.log`；
  `nseq010-log-check.log` 为 PASS。保留既有 TDMA-FLIGHT-BITMAP-01 ID 警告。
- 下一 gate：完成模型行为测试及独立复核，冻结本切片源码后执行 P3；
  硬件 gate 未通过前不关闭 NSEQ-010，也不把构建通过写成板端配置通过。

### NSEQ-PROGRESS-20260915-005 - 模型验证通过，P3 设备发现阻塞

- TODO：`NSEQ-010`，最终状态为 BLOCKED；NSEQ-011 仍 PENDING。
- 日期：2026-09-15。
- 变更/提交：配置模型、CMake 和测试已暂存，文档未暂存，尚未提交或合并。
  未修改现有 SCPI callback、Trigger runtime、PIO 或 TDMA 状态机。
- 软件验证：`python -m pytest tests/python/test_trigger_sequence_config.py -p no:cacheprovider --basetemp out/node-sequence/pytest-model-main`
  通过，当次快照 9 passed。测试实际编译并执行生产 C，检查失败原子性、容量/范围、
  最大名称长度、冻结、跨计划隔离、active 撤销与失效、generation 耗尽和损坏 CRC。
  Python itertools/struct/zlib 独立生成映射与 CRC 期望；证据 `out/node-sequence/nseq010-model-tests.log`。
- 独立复核：`sequence_command_audit` 只读审阅接口、实现、CMake 和测试，
  独立执行相同测试得到 PASS，无阻断发现；证据目录 `out/node-sequence/nseq010-model-review`。
  结论只覆盖软件模型，不代替 P3，也不声明业务 SCPI 已可用。
- P3 实际命令：`python tools/hardware_acceptance/p3_hardware_acceptance.py run --tdma-only --build-dir out/build/node-sequence-release --out-dir out/HardwareAcceptance/20260915/node-sequence-nseq010-p3-four`。
  构建成功，OTA 设备发现返回主环全部 missing_serial_numbers，P3 返回失败。
  当次快照 build 为 `20260915033639`，board_count/updated_count 均为零，
  未发送主环固件、未进入校准、TDMA 短帧或实物配置测试，无本次验收凭证。
- 原始失败证据：`out/node-sequence/nseq010-p3-run.log`、
  `out/HardwareAcceptance/20260915/node-sequence-nseq010-p3-four/ota.log`、
  `ota-four-board-tdma/summary.json`。注意该 OTA summary 在零板结果下仍写
  passed=true，这是现有工具的空结果汇总行为；必须结合非零退出码和缺失 UID
  判为失败，禁止据此声明 OTA/P3 成功。本切片未修改验收工具。
- 指纹与门禁：`nseq010-source-fingerprint.log` 记录 working/staged 一致；
  `python tools/hardware_acceptance/p3_hardware_acceptance.py check-staged` 返回失败，
  原因是当前 staged 指纹无匹配硬件验收。日志 `nseq010-check-staged.log`；
  不能复用历史 receipt、手写通过报告或绕过 hook 提交。
- 最终文档 gate：强制执行 `sh .githooks/pre-commit` 的 docs_check 和
  doc_regression 均通过，随后 P3 凭证 gate 按上述原因失败；日志
  `out/node-sequence/nseq010-pre-commit-final.log`。最终文档 pytest 当次快照
  18 passed，日志 `nseq010-doc-tests-final.log`，staged diff 空白检查通过。
- 文档末次独立复核发现首页 Checkpoint 遗留“配置实现尚未开始”，
  已纠正为模型软件验证完成、P3 阻塞；历史记录保留其当时状态。
- 后续动作：主环设备可见且串口可用后，先读身份，再对本切片重跑 P3；
  通过后关闭 NSEQ-010，进入 NSEQ-011 的真实 SCPI 解析、响应和配置边界。

### NSEQ-PROGRESS-20260915-006 - 单节点实现及首轮板端验证

- TODO：`NSEQ-003/011/015/020/021/030/050`。
- 日期：2026-09-15；代码及文档仍未提交、未合并。
- 范围：按用户后续明确的单节点调试实施，TDMA仅保留后续接口。
  原始多板P3失败不再阻塞本轮开发，但仍不能充当提交凭证。
- 实现：真实SCPI参数、计划、映射、激活和CHECK；软件STEP、输入选择、
  编码输出、建立定时、独立完成脉冲、暂停/恢复/停止和实际IN/OUT查询。
  Core0配置与Core1执行分离；GPIO/alarm/IRQ及SMA资源独占，OTA/Flash冲突拒绝。
- 主机验证：`single-node-host-final-r1.log`记录206项通过；
  覆盖生产C配置/服务、真实libscpi、GPIO/alarm模拟、资源包装和静态门禁。
  独立资源仲裁测试证据位于`resource-arbiter-review*`。
- 构建：`single-node-build-r2.log`记录Release及Flash布局通过，
  build快照为`20260915041512`。
- 单板OTA：限定COM10及UID `839E1AE79EA20F31`，期望一板，
  `ota-single-r1/summary.json`记录传输、重启、版本核验通过。
  此OTA的boot commit仅确认板上固件，不是Git提交。
- 首轮SCPI：`single-node-hil-r1.json`保留失败原始证据。
  真实参数/活动计划/MAP/CHECK/编码配置读回通过，非法计划写入保持原活动计划；
  START返回accepted后持续STARTING，未通过运行验收。
  `hil-r1-diagnostic.txt`记录STOPPING、占用掩码仍在，以及未接线IN/OUT读零。
- 独立复核：`sequence_runtime_audit`发现服务仅挂在可禁用/隔离的调度槽，
  导致START/STOP无法执行；另发现新ARM失败会混入上次backend计数与时间戳。
  两项均进入修复与回归，不能把首轮OTA成功写成序列执行成功。
- 既有回归暴露：`single-node-io-regression-r1.log`保留初次资源包装引起的
  源码提取测试失败；修复测试提取与静态检查，使其既检查原语义又检查租约包装。
- SCPI错误信息：生产`scpi_user_config.h`关闭device-dependent error text，
  `SYST:ERR?`对执行拒绝返回通用错误；具体行为用状态、计数和配置不变共同验证。
- 下一gate：修复调度及新run快照隔离，重建/重新单板OTA并重跑SCPI和IO验证。
  用户未接外部信号，外部脉冲及示波器波形仍未验收。

### NSEQ-PROGRESS-20260915-007 - 软件切步闭环及输入启动故障定位

- TODO：`NSEQ-015/020/021/030/050`；日期：2026-09-15；未提交。
- 修复：序列服务从可关闭负载迁入受计量的强制阶段，TDMA/analyzer先执行，
  离线提前返回也处理邮箱；新ARM失败清空本轮事实，避免沿用历史数据。
  中间复核指出周期计时之前调用会形成耗时盲区，最终改为阶段计量内部执行。
- 验证：`single-node-host-final-r2.log`记录207项通过；最终调度/backend/service
  复核为`single-node-dispatch-final-r3.log`的3项通过。
  `sequence_runtime_audit`独立审查及重跑通过；未将模拟时钟当作物理波形证据。
- 构建/OTA：`single-node-build-r4.log`重新配置生成新build，
  `single-node-build-r5.log`完成最终调度源码增量构建；
  `ota-single-r2/summary.json`核验COM10新build `20260915043510`。
- 板端结果：`single-node-hil-r2.json`记录真实软件STEP、首末步循环、忙时拒绝、
  配置冻结、就绪暂停、忙时暂停完成在途步骤、恢复不自动推进及STOP取消/输出清低通过。
  OUT编码和完成输出均读回高低变化；时间戳验证配置等待及脉宽的下界，非示波器精度验收。
- 输入故障：首次IN1/RISING的START超时，随后配置与run清空；
  `hil-r2-input-diagnostic.txt`、`hil-r2-reset-diagnostic.txt`及`hil-r2-watchdog.txt`
  确认同一build及WATCHDOG_TIMEOUT/CORE1_STALL。整轮测试仍记FAIL。
- 根因：SDK共享IRQ默认槽位由USB device/stdio、RS485 UART/DMA占满；
  输入raw IRQ申请额外槽位触发SDK hard_assert。链接map及SDK/驱动注册位置相互印证。
  应用目标补足序列GPIO槽位，并加入容量回归；不修改TDMA业务代码。
- 下一gate：新固件重新构建、OTA，重跑包含输入启动/停止与IO读回的完整测试；
  当前仍无外部输入脉冲或示波器接线，不能宣称外部触发验收通过。

### NSEQ-PROGRESS-20260915-008 - SP8T地址序列与SCPI/IO验收通过

- TODO：`NSEQ-003/011/015/020/021/030/050/051`；日期：2026-09-15；未提交、未合并。
- 用户最新指定先以SP8T的地址全组合验证。测试配置为OUT1最低位至OUT3最高位，
  OUT4独立完成；样例建立时间和完成脉宽分别为100000/200000微秒，仅为验证配置快照。
- IRQ修复：应用A/B目标的`PICO_MAX_SHARED_IRQ_HANDLERS`预算覆盖既有注册与序列输入。
  后端有编译期容量保护，负向用例验证旧容量编译拒绝；bootloader维持原默认配置。
  `sequence_runtime_audit`独立核对实际compile_commands中SDK C/ASM定义一致，并重跑后端测试通过。
- 软件/构建：`single-node-host-final-r3.log`记录207项通过；
  `single-node-build-r6.log`记录Release、A/B/boot Flash布局通过。
- 单板OTA：`ota-single-r3/summary.json`记录一板更新/重启/版本确认PASS，
  build为`20260915044506`，UID为`839E1AE79EA20F31`。
  包SHA256为`D60B6D2ED367FE79AEC41C586A2EAEAD4FB6F9088503AE44F75614B85A780B28`。
- SP8T实测：`sp8t-hil-r1.json`为PASS，204条断言（含轮询，快照）。
  逐次软件STEP验证000/001/010/011/100/101/110/111/000，接纳与完成均为9，
  每步完成索引、后继索引、地址位逐通道实际读取全部匹配；DONE后不自动推进。
  该测试验证控制板pad输出，不代表未连接的SP8T射频通路已切换。
- 完整单板回归：`single-node-hil-r3.json`为PASS，330条断言（含轮询，快照）。
  参数/计划/IO/CODE、失败原子性、配置冻结、忙时拒绝、首末步循环、暂停/恢复、
  停止取消、MODE0及*RST清低、重新START计数清零均通过。
  IN1-IN4各自RISING/FALLING的配置、ARM/STOP、源隔离和实际IO读取均通过，未再触发重启。
  外部输入脉冲与示波器测试标志明确为false，故不关闭外部验收待办。
- 输出事实：软件控制实测读到编码保持、完成OUT高/低及STOP后低；
  记录CPU观察时间及延迟，不宣称PIO定宽精度、外部边沿响应精度或全节点DONE。
- 交付状态：`sp8t_setup.scpi`保存可复用配置；`sp8t-ready-config.txt`记录最终重新下发并读回，
  active为SP8T、源BUS、设备IDLE、输出/资源占用为零。配置位于RAM，重启需重新配置。
- 文档与提交门禁：文档检查、文档回归及其pytest通过；既有契约ID告警保留。
  `single-node-pre-commit-r2.log`文档部分PASS，随后P3检查因working/staged源码不一致而FAIL；
  `single-node-check-staged-r1.log`保留同类拒绝。未修改暂存状态来伪造验收、未提交。
- 失败闭环：进度006调度挂起与007共享IRQ耗尽均由新build复测关闭；原始FAIL日志全部保留。
- 下一gate：接线后验证SP8T实体通路、IN1-IN4真实边沿、忙时输入和波形时序；
  相关门禁通过后再按代码/文档分离提交，不能把本单板证据充当完整P3凭证。

### NSEQ-PROGRESS-20260915-009 - 可复用SP8T验收工具

- TODO：`NSEQ-022/031/050`；日期：2026-09-15；本次新增工具和测试，不改固件。
- 变更：新增`tools/hardware_acceptance/sequence_trigger_acceptance.py`，使用现有串口生命周期。
  身份核对成功才写配置；BUS执行软件STEP；IN1-IN4模式仅观察独立源的有限脉冲序列，
  不发送软件STEP，也不生成模拟输入。每步校验run/generation、计数、地址、时间戳和真实OUT。
  丢失中间状态、采样期间状态变化、事件计数不符均失败，不把最终游标相同当全程正确。
- 外部模式：要求`--input-events`来自脉冲源独立计数，等于预期完成数与忙时拒绝数之和；
  脉冲间隔须留出建立/完成时间及SCPI采样窗口。启动后终端打印ARMED才开始发送，
  发送完停源，工具在观察窗口结束后核对总数并STOP。不能用本工具证明高频最大吞吐。
- 证据边界：报告始终明确`external_waveform_verified=false`、`rf_path_verified=false`、
  `p3_receipt=false`及`completion_high_observed=false`。工具在READY后读码，
  完成OUT高相由进度008专门测试读回；本工具不替代外部波形或射频通路测量。
- 清理与留证：失败和中断均尝试STOP并验证输出/资源低；主失败与清理失败分别保存，
  输出文件使用排他创建，拒绝覆盖历史成功或失败证据。
- 初轮验证：`acceptance-tool-tests-r2.log`记录47项通过；
  `acceptance-tool-bus-r1.json`为COM10 SP8T软件实测PASS；
  `acceptance-tool-no-input-r1.json`在未接外部源时按预期FAIL（step count mismatch），
  cleanup_failure为空、输出归零。这是工具拒绝空验收的证据，不是外部输入通过。
- 独立复核：`sequence_runtime_audit`只读审查判据、身份及清理并运行测试；
  指出完成高相证据边界，主控据此增加显式报告字段，不夸大PASS范围。
- 最终复测：`acceptance-tool-tests-r3.log`记录47项工具测试及18项文档测试通过；
  `acceptance-tool-bus-r2.json`在COM10使用最终报告格式通过，保留SP8T/BUS且STOP完成。
  `acceptance-tool-build.log`构建通过，固件源码未变；无需重复OTA。
  `acceptance-tool-docs-check.log`、`acceptance-tool-doc-regression.log`通过。
  `acceptance-tool-pre-commit.log`文档部分通过，P3部分及`acceptance-tool-check-staged.log`
  仍因working/staged源码不一致而拒绝；无本次P3凭证，未暂存、提交或合并。
- 下一gate：外部物理接线和脉冲源独立发送计数，随后用正式工具采样并另留原始波形；
  未接线时不能关闭NSEQ-031，本轮不重复已通过的软件验证代替外部验收。

#### 验收命令样例

以下build、UID、时间和脉冲数为进度009的历史示例快照，不是固定产品参数。
这些命令对应当时GPIO版工具；当前工具要求PIO0时间元数据，不能直接用于该旧build。
PIO版命令需使用其实际验收build和新的输出文件，待进度011补齐。

```powershell
python tools/hardware_acceptance/sequence_trigger_acceptance.py --port COM10 --serial-number 839E1AE79EA20F31 --build 20260915044506 --out out/node-sequence/sp8t-bus-new.json
```

外部模式仅在接入所选输入且脉冲源可发送指定数量后执行，例如：

```powershell
python tools/hardware_acceptance/sequence_trigger_acceptance.py --port COM10 --serial-number 839E1AE79EA20F31 --build 20260915044506 --source IN1 --edge RIS --steps 9 --input-events 9 --duration 30 --out out/node-sequence/sp8t-in1-new.json
```

工具输出ARMED后，在窗口内发送上述有限边沿并停源。若测试忙时拒绝，
用`--busy`声明预期拒绝数，`--input-events`必须包含这些边沿；计数不能从被测板结果反推。

### NSEQ-PROGRESS-20260915-010 - IN1低频外部触发流程及后续PIO约束

- TODO：`NSEQ-030/031/032/050/060/061/062`；日期：2026-09-15；本次未改固件、未OTA或提交。
- 用户已接IN1，报告方波3V，频率先描述1kHz后调整50Hz；随后明确“先用50跑完流程，后续提速”，
  并要求后续读取SYNC分配、考虑PIO热加载。以下频率/电平为用户设置值，不是独立仪器测量结果。
- 实时身份核对：`in1-connected-probe-r1.txt`验证COM10、UID及build与进度008一致；
  `in1-connected-levels-r1.txt`记录真实IN1高低变化。初次`in1-1khz-busy-r1.json`
  涉及用户调整频率的时段，仅保留为历史观察，不能据文件名宣称完成1kHz验收。
- SP8T快档测试配置快照：建立1000微秒、完成脉宽1000微秒，GPIO IRQ/alarm实现；
  不是硬件极限，也不是PIO后端。`in1-50hz-rising-r1.json`和`in1-50hz-falling-r1.json`
  均PASS，暂停前接纳/完成分别155/155及154/154，busy为零；恢复后的总接纳/完成分别178/178及177/177。
  无fault或取消，暂停期间接受/完成不增长、notready随真实输入增长，恢复保持run/generation并继续后继状态。
- 忙时测试配置快照：建立100000微秒、完成200000微秒，主动超过输入周期。
  `in1-50hz-busy-r1.json`为PASS，暂停前10完成与150 busy拒绝，恢复后12完成与180 busy拒绝；
  拒绝不排队，暂停完成在途步骤后计数稳定，STOP后不重放。
- 控制矩阵：`in1-50hz-controls-r1.json`为PASS。
  选IN2/IN3/IN4时，持续IN1输入均不推进；IN1模式拒绝软件STEP；在途STOP取消一个未完成步骤、
  输出/租约清零；重新START建立新run，从首状态执行，暂停后只完成已接纳的一步。
  测试结束恢复SP8T/IN1上升沿、1000/1000微秒配置，IDLE、所有输出低。
- 证据边界：上述脚本按SCPI间隔采样运行快照，允许采样之间多步执行，暂停后读取稳定编码。
  不能将这些结果写成每个输入边沿均有独立波形、每档输出逐拍采样或无丢脉冲认证。
  IN2-IN4仅验证“未选IN1隔离”，不等于其实际输入接线验收；独立波形、射频通路和PIO提速仍未验证。
- 独立复核：`sequence_runtime_audit`只读核验四份JSON及脚本，确认PASS、清理成功与上述计数；
  未发现阻断当前功能结论的问题，同时明确轮询不能替代逐边沿波形证据。
- SYNC只读核对：`docs/sync/SYNC_IO_ARCHITECTURE.md`及`sync_io_persona_manager.c`定义
  descriptor资源矩阵与quiesced切换/回滚；当前board快照SMA/SYNC为PIO0、TDMA RX为PIO2。
  用户PIO2意图与当前分配后续由NSEQ-060对齐，本轮未修改board或TDMA分区。
  NSEQ-061覆盖PIO装载/停止/卸载、失败回滚和旧事件隔离，NSEQ-062再做频率提升与波形验收。
- 下一gate：低频功能已闭环；补齐独立输入/输出波形和其他输入实际激励，再按用户后续安排推进PIO优化。

### NSEQ-PROGRESS-20260915-011 - PIO0执行迁移开始

- TODO：`NSEQ-060/061/062`；日期：2026-09-15。
- 用户明确不改PIO1/PIO2，使用PIO0快速序列触发，取代进度010中尚未确认的PIO2意图。
  board的`BOARD_TDMA_SMA_PIO_BLOCK_ID`与SYNC域分区一致，NSEQ-060关闭。
- NSEQ-061进入实施：SYNC_IO persona热加载，整计划提前准备，边沿准入、编码及完成输出由PIO执行，
  CPU汇总实际执行回执。保留既有捕获资源，申请不足明确拒绝并回滚。
- 本记录仅确认范围和实施启动；新PIO程序、时序、回执、热加载及板端流程尚待验证。
  进度010的GPIO后端低频证据不能当作PIO后端通过。
- 软件实现更新：PIO0热加载、整计划DMA、真实执行/完成回执、稳定拒绝结算及独立
  `READ:SEQ:TIMing?`/`READ:SEQ:REJections?`已接入；原运行状态字段顺序保持兼容。
  外部运行时拒绝计数pending，不以异步DMA相减虚报busy；暂停/停止排空后结算。
- 失败保留：`pio-build-r1.log`链接RAM超额，缩减私有回执环后`pio-build-r2.log`仍超额；
  合并重复冻结快照及收紧已验证范围的编码表后，`pio-build-r3.log`、`pio-build-r4.log`通过。
  未扩大linker或缩减其他域缓冲。最终环容量以`RECEIPT_WORDS`为准。
- 软件证据：`pio-combined-tests-r1.log`记录268项模型/SCPI/服务/工具/资源回归PASS；
  `pio-backend-final-tests-r1.log`记录13项PIO/生产函数测试PASS，含真实persona manager、
  claim/load/arm/start失败回滚、暂停PC切点与计数恢复。以上数量为本轮快照，非固定门槛。
- 独立复核：`sequence_runtime_audit`核对PIO0范围、DMA停止顺序、暂停与停止边界、
  双核单份冻结计划及新增失败注入，未剩已知代码阻断；不能替代板端或波形验收。
- 新包build快照`20260915060237`，SHA256为
  `F6844724742F8F6178D4FEA698DEE626551B4DC77595B0AB5EEB85CA36055022`。
  `pio-build-r4.log`记录Release与Flash布局PASS，单板OTA和PIO版流程正在验证。
- 用户最新验收指令：单板调试无需P3，只需验证单板功能；后续不以P3阻塞本轮验收，
  历史失败原样保留。本次尚未提交或合并。
- 首轮OTA：`pio-ota-r1/summary.json`PASS并核对新build；`pio-bus-r1.json`记录
  SP8T软件全状态及回绕的九步均执行完成，但STOP清理返回OVERFLOW，故整体验收FAIL。
  输出和租约已清零，`pio-bus-failure-idle-r1.txt`保留现场。正在修复DMA停止后的计数读取，
  未以正常切步覆盖清理失败，未开始更高频率验收。
- STOP修复后build快照`20260915062651`，`pio-build-r5.log`及`pio-ota-r2/summary.json`通过；
  `pio-bus-r2.json`九步SP8T及STOP完整PASS，`pio-hotload-r1.json`记录重复热加载PASS。
  `pio-in1-50hz-rising-r1.json`未收到脉冲，用户随后确认此前源输出未开启；
  开源后`pio-in1-50hz-rising-r2.json`已接纳并完成154步，但PAUSE返回OVERFLOW，仍待修复。
  因此不能将这轮外部PIO测试写成通过，当前设备STOP后输出及租约已释放。
- 用户当时新增并修正接口为`CONFigure:SEQuence:NEXT`、`READ:SEQuence:NEXT?`（历史命名，进度029已合并移除）。
  写入共用软件STEP，查询共用运行状态块且不推进；`next-tests-r1.log`记录真实SCPI
  解析84项PASS，`next-docs-check-r1.log`与`next-doc-regression-r1.log`通过。
  新命令已落源码，当前板端上述build尚未包含，待下一次构建/OTA验证。

### NSEQ-PROGRESS-20260915-012 - PIO0暂停修复及单板提速验证

- TODO：`NSEQ-061/062/050`；日期：2026-09-15；按用户要求仅进行单板功能验收，无P3前置。
- 保留失败：`pio-in1-50hz-rising-r3.json`在诊断版仍完成154步后PAUSE报回执溢出。
  最终定位为恢复DMA时写`CTRL_TRIG`使旧RELOAD提前启动；改写非触发`AL1_CTRL`，
  PAUSE完成排空后保持RX停止，CONT先恢复RX再开放输入。此前重复abort/resume路径不再执行。
  SDK依据为`hardware/dma.h`的`dma_channel_set_config`与`dma_channel_set_trans_count`。
- 最终build快照`20260915091316`；`pio-build-r8.log`与`pio-ota-r4/summary.json`PASS。
  包SHA256为`BC4D22FE8966D0B7689D276AB273E33EA2C009E85B1D48E6AF0D24FAAEE705E3`。
  `pause-r8-tests.log`记录97项PIO后端及SCPI测试PASS；故障来源细分以`sync_io_sequence_fault_t`为准。
- 独立复核`pio_pause_review`核对SDK触发副作用、暂停恢复顺序及生产测试模拟，未发现阻断项，
  单独执行后端13项PASS。以下板测均在上述最终build执行。
- 50Hz连续IN1（用户设置值，非独立测量）：`pio-in1-50hz-rising-r4.json`、
  `pio-in1-50hz-falling-r1.json`与`pio-in1-50hz-busy-r1.json`全部PASS，
  覆盖双沿、回绕、暂停/恢复、拒绝结算与STOP。源持续输出属于正常运行场景。
- 用户切至1kHz后：`pio-in1-1khz-rising-r1.json`与`pio-in1-1khz-falling-r1.json`PASS。
  配置快照为建立100us、完成脉宽100us；总接纳/完成分别5550/5550与5579/5579，
  稳定结算busy均为零，无fault/cancel。暂停只增长notready，恢复保持身份并继续后继状态。
- 忙时压力：`pio-in1-1khz-busy-r1.json`PASS，单步配置改为1000us+1000us，
  最终1174接纳/完成、2347 busy拒绝，STOP后IDLE；拒绝不重放。
  `pio-in1-1khz-controls-r1.json`PASS，覆盖未选输入隔离、外部模式拒绝软件STEP、
  在途STOP取消、再次START首状态以及暂停完成在途工作。
- `pio-hotload-r2.json`记录同build重复热加载、双沿换源及资源释放PASS；
  `next-bus-r2.json`使用当时的`CONF:SEQ:NEXT`/`READ:SEQ:NEXT?`完成SP8T九步与停止，PASS；该历史证据中的命令名不改写，当前接口见进度029。
  `1khz-final-idle-r1.txt`确认恢复IN1上升沿、100us+100us，IDLE、输出和租约为零。
- 证据边界：上述外部测试使用SCPI间隔观察及暂停后稳定pad读回，不是每边沿独立波形认证；
  运行时拒绝数待结算，只有暂停/停止后的最终结果用于判定。IN2-IN4实际激励尚未完成。
  用户计划后续5kHz和1MHz压力测试；后者先验证过载拒绝/恢复，不承诺当前实现每微秒完成一步。
- NSEQ-061按单板功能范围完成，NSEQ-062进入提速验证；尚未提交或合并。

### NSEQ-PROGRESS-20260915-013 - 5 kHz与1 MHz压力档及调试界面

- 频率调整通过外部信号源完成，序列固件无需重新编译；输入源、边沿、建立时间和完成脉宽均可在停止状态下通过SCPI重新配置。
- 5 kHz连续IN1在PIO0后端通过上升沿、下降沿及忙时测试：10/10微秒档上升沿完成27636步、下降沿完成27830步，均无fault；200/200微秒忙时档完成5885步并记录11768次busy拒绝。暂停、继续和停止后的计数稳定，输出与租约释放。
- 用户切至1 MHz后，使用10/10微秒档完成单板压力验证：测试期间完成115909步，无fault；暂停时接纳95171/完成95171，记录1998586次busy拒绝；继续后接纳115909/完成115909，最终停止回到IDLE并释放IO。证据文件SHA-256为`9390AC599DB749D11EA606E0473A81F834375DF29BD11539DABC1BEC0B2A92A0`（快照，非事实源）。该结果证明过载拒绝、暂停/恢复和停止恢复路径工作，不代表每个1 MHz边沿均被执行或完成波形认证。
- 实时读回确认当时的`READ:SEQ:NEXT?`与`READ:IO:STAT?`可用（前者已由进度029替换为`TRIG:SEQ:NEXT?`）；板卡随后恢复IN1上升沿、10/10微秒、IDLE配置。`tools/sequence_trigger_debug_ui/sequence_trigger_debug_ui.py`提供纯Tk调试界面，可自动发现串口并通过单一后台队列串行化操作，支持序列控制、当前位置与IO读回、分级日志及导出、设备身份/自定义SCPI查询，并复用`tools/ota_multi_update/ota_multi_update.py`执行单板OTA。
- 证据边界：频率为用户信号源设置值，未使用独立脉冲计数器或示波器；1 MHz结果用于压力与恢复验证，后续若继续提高频率应优先观察回执环溢出和拒绝计数。

### NSEQ-PROGRESS-20260915-014 - START首状态预置实现

- TODO：`NSEQ-063`；日期：2026-09-15；本次修改固件、验收工具、测试和域文档。
- 用户确认运行语义：`TRIG:START`完成后处于计划首状态，第一次真实BUS/IN触发切换到第二状态，后续按计划循环。
- 实现：PIO owner取得资源后直接驱动首状态；初始建立时间到期且executor READY前不启动外部输入counter/ingress，BUS单步也保持NOT_READY。DMA执行环按第二状态至末状态、再回首状态的顺序循环。
- 计数边界：START预置不增加accepted/completed、不生成执行/完成回执，也不拉高完成OUT；运行状态发布current=首状态、next=第二状态。STOP仍将本persona输出拉低并释放资源。
- 验收工具同步检查START后的首状态实际IO，并按新游标公式验证首次触发、逐步执行和回绕；旧build证据不用于证明新语义。
- host验证：针对service、真实SCPI解析、PIO模型/生产函数和验收工具的组合回归记录160项PASS；补充初始建立deadline准入门禁后，PIO专项13项复测PASS；全部sequence/resource专项共281项PASS（当次环境快照，非产品事实源）。输出位于`out/pytest/sequence-start-prime-r3/`、`out/pytest/sequence-prime-gate/`及对应完整专项运行记录。
- 首轮回归曾因旧游标/输出断言及host替身缺少启动计时接口出现19项失败，修正测试语义和替身后全绿；未通过删减产品检查规避失败。
- Release构建：`pico2-release`在`out/build/sequence-start-prime/`完成772个步骤，A/B应用、Bootloader、factory UF2、OTA包及三份flash-link契约检查通过（当次构建快照，非产品事实源）。
- 单板OTA：当前设备枚举为COM8，UID `839E1AE79EA20F31`；使用P3同源码构建产物更新至build `20260915144303`并提交成功，证据位于`out/node-sequence/sequence-start-prime-ota/`。
- BUS单板验收：`out/node-sequence/sequence-start-prime-bus.json`记录PASS。START后为`current=0,next=1,accepted=completed=0`且IO地址为0；第一次软件触发进入状态1；第8步回到状态0，第9步再次进入状态1；全程无拒绝、取消或fault，STOP后五路IO读回均为0。
- 证据边界：用户当前没有外部脉冲源，本轮不执行IN1-IN4验收；串口轮询未观察完成高电平，也不能证明START期间OUT4没有瞬态脉冲，该项仅由无回执实现路径和host测试覆盖，仍需示波器或逻辑分析仪形成外部波形证据。
- P3结果：同源码P3运行在五板OTA发现阶段FAIL，仅找到上述一板，缺少4个登记序列号；未生成P3凭证，不能用单板PASS替代。
- 下一gate：补齐五板后重跑P3，并在具备示波器或逻辑分析仪时验证START不产生OUT4完成脉冲。

### NSEQ-PROGRESS-20260915-015 - 调试界面与USB运行时切换固件

- 用户串口日志中`*IDN?`正常返回，但`SYST:USB:MODE USBTMC`、`SYST:USB:BOOT`和
  `SYST:USB:MODE?`均无响应。源码与当前Release配置核对确认：CDC链路正常，当前板上固件未启用
  `PROJECT_ENABLE_USB_RUNTIME_SWITCH`，因此没有注册上述命令；重复发送不能完成模式切换。
- 调试界面迁入`tools/sequence_trigger_debug_ui/`，提供浅色扁平布局、串口与VISA USB仪器自动发现、
  单一后台操作队列、当前位置/IO读回、分级日志与导出，以及Serial/USBTMC OTA入口。切换模式前先查询
  `SYST:USB:MODE?`；响应不是`CDC`或`USBTMC`时直接报告当前固件未启用运行时切换，不再继续静默发送
  MODE和BOOT。
- 新增`pico2-usb-runtime-switch`专用preset，默认以CDC启动并启用
  `PROJECT_ENABLE_USB_RUNTIME_SWITCH`。常规`pico2-release`能力不变，USB运行时切换仍是调试能力，
  不改变产品对外接口契约。
- 首次专用构建在链接阶段因Pico SDK libc heap保留导致RAM超出608 B（构建快照，非事实源）。
  `CMakeLists.txt`新增可选`PROJECT_PICO_HEAP_SIZE`覆盖项，专用preset按
  `CMakePresets.json`中的当前值覆盖SDK `PICO_HEAP_SIZE`；FreeRTOS任务内存继续以
  `configTOTAL_HEAP_SIZE`为事实源，两者没有混用。普通Release未设置覆盖项，继续使用SDK默认值。
- 旧build目录首次重跑仍失败，原因是自动构建脚本跳过preset配置、cache内新变量为空；显式执行
  `cmake --preset pico2-usb-runtime-switch -B out/build/sequence-usb-runtime-switch`后，编译数据库确认
  `crt0.S`收到覆盖定义。随后完整构建PASS，A/B应用、Bootloader、factory UF2、OTA包和三份
  flash-link契约检查均通过；产物位于`out/build/sequence-usb-runtime-switch/`。独立配置的
  `pico2-release`确认覆盖项为空、编译命令未定义`PICO_HEAP_SIZE`，完整Release构建及三份
  flash-link检查同样PASS。
- 单板OTA首次记录`out/node-sequence/sequence-usb-runtime-switch-ota/`为FAIL：数据发送完成且BOOT返回
  OK，但设备以runtime CDC描述符从COM8重新枚举为COM4，提交工具只重开旧端口而超时。新端口随后
  读回目标build及`SYST:USB:MODE? -> CDC`，证明这是post-reset端口变化，不是镜像启动失败；原失败未覆盖。
- CDC侧写入USBTMC并读回后执行BOOT，NI-VISA自动发现
  `USB0::0xCAFE::0x4030::839E1AE79EA20F31::INSTR`；TMC侧身份、build及模式查询通过。
  `out/node-sequence/sequence-usbtmc-bus.json`记录BUS单板验收PASS：START直接选择首位置并输出首编码，
  九次软件推进覆盖完整一轮及回绕后的下一位置，接纳/完成一致、无fault，STOP后IDLE且输出全部释放。
  最后通过TMC写回CDC并BOOT，COM4恢复枚举，身份、目标build和CDC模式再次读回通过。
- `open_serial_port()`清理改为与`SerialSession.close()`一致的best-effort语义：BOOT已返回后目标立即
  断开时，不再由退出阶段的flush/close异常覆盖成功响应；命令执行阶段异常仍正常上抛。正式序列验收工具
  增加`--visa-resource`，TMC与串口共用同一套状态、计数、回绕、实际IO和STOP判据。
- 产品级证据边界：最终工具源码对应的P3证据位于
  `out/HardwareAcceptance/20260915/p3-235133/`（运行快照，非事实源）：五板OTA发现仅识别到当前
  COM8设备，缺少其余四个登记序列号，流程FAIL且未签发P3凭证；没有用单板在线或构建PASS替代五板门禁。

### NSEQ-PROGRESS-20260916-017 - 固化反馈接线验证工具与单板复测

- TODO：`NSEQ-022`、`NSEQ-031`、`NSEQ-050`、`NSEQ-079`；日期：2026-09-16；本轮新增工具和测试，未 OTA、未提交。以下计数与参数均为本次验收快照，非产品事实源。
- 新增 `tools/hardware_acceptance/sequence_feedback_validate.py`，统一设备发现、UID/build 校验、Serial/USBTMC、配置读回、BUS 单步与持续 IN1 观察。先写入 tools，再通过命令行运行；不再用临时内联 Python 操作设备。
- BUS 阶段配置 OUT1–OUT3 为 SP8T 编码、OUT4 为可采样的长脉冲，逐步读取真实 pad 电平与 IN2 回接。持续输入阶段不假设独立脉冲数，允许 SCPI 跨多个步采样，暂停排空后核对运行标识、游标、计数，再验证恢复。报告显式保留 `independent_input_count_verified=false`、`vna_role_runtime_verified=false`、`external_waveform_verified=false`、`rf_path_verified=false`。
- 初次 `out/node-sequence/feedback-bench-r1.json`：BUS 回接通过，IN1 在观察窗口无事件而失败；失败记录和 STOP 清理结果保留。用户随后确认信号源已开启并设置为 50 Hz，`feedback-in1-50hz-r2.json` 持续输入通过。
- 独立只读复核 `/root/feedback_tool_review` 发现并修复工具边界：暂停期间 NOT_READY 拒绝数可以继续增加；CONT 接受后有界等待 owner 恢复；STOP 后必须核对错误、执行计数及故障。相应软件回归共 92 项通过（`test_sequence_feedback_validate.py` 与 `test_sequence_trigger_acceptance.py`），复审无剩余阻断项。
- 最终工具证据：`out/node-sequence/feedback-50hz-reviewed-r3.json`，设备 COM3、UID `839E1AE79EA20F31`、build `20260916045313`，报告带工具 SHA256。BUS 九次单步覆盖编码全组合及回绕，每步 OUT4 高/低均在 IN2 读回；START 预置首状态，反馈输入与 DONE 均未额外推进。
- 同一报告中，持续 IN1 输入暂停时 accepted/completed 为 257，恢复后累计 511，busy_rejected=0、faults=0。暂停期间执行计数冻结，NOT_READY 增长属于关闭准入后的输入拒绝。STOP 后 IO 为 `1,0,0,0,0`：IN1 外部源仍为高，输出、资源占用、armed 和 busy 已归零。50 Hz 为用户设定，SCPI 轮询不等同于独立频率/逐边沿测量。
- 更正此前诊断：`SYST:ERR?` 短写法可用；先前 `Undefined header` 来自错误的 `SYST:BUILD?` 探测，已撤回错误队列短写法的无必要修改。早先有限脉冲工具指定九个输入事件没有独立源计数依据，不能作为本次连续信号验收标准，原失败报告保留。
- 证据边界：当前旧固件的 OUT4 仍由序列 status pulse 路径驱动，本轮证明电气接线与既有序列基础功能，不代表新增 VNA 槽位运行时、READY 超时、RefMem 事实发布或双角色闭环已实现。进度 016 中的角色解析目前为独立模型，尚未接入 START/PIO；节点 SCPI 包装也没有提供独立 IO 角色绑定。下一 gate 是完善角色模型校验、接入 Core0 配置/Core1 owner 快照及 VNA 运行状态，再做新固件闭环验收；不能把端口枚举当作唯一剩余阻塞。
- 重复运行（每次使用新证据文件名，既有文件拒绝覆盖）：

```powershell
python tools/hardware_acceptance/sequence_feedback_validate.py --serial-number 839E1AE79EA20F31 --build 20260916045313 --mode all --source-hz 50 --out out/node-sequence/feedback-50hz-next.json
```

### NSEQ-PROGRESS-20260916-018 - DUT 无状态输出模式与新固件单板验收

- TODO：`NSEQ-050`、`NSEQ-070`、`NSEQ-071`、`NSEQ-073`、`NSEQ-078`、`NSEQ-079`；日期：2026-09-16；代码与文档尚未提交。以下 build、计数、参数和测试数量为当次快照，非产品事实源。
- 增加 `TRIGGER_SEQUENCE_STATUS_NONE` / `SYNC_IO_SEQUENCE_STATUS_NONE`，支持 `CONF:SEQ:OUTPUT 7,0,NONE,10,0` 及真实读回。NONE 要求状态掩码和脉宽为零，DUT 只驱动编码电平；非法组合不改变先前配置。PIO0 使用既有跳过状态脉冲路径，保留建立时间及执行回执，不修改 PIO1/PIO2。
- Tk 界面默认选择“无”状态输出，取消 OUT4 状态选中并禁用脉宽；保留显式 PULSE/LEVEL 模式，说明旧状态脉冲不等同于 VNA 网关配置。
- 修正 RefMem 纯角色解析模型：区分 claim_epoch 与 active_generation，按 instance ID 查找稀疏实例，核验实际启用的 LINK_SWITCHER / INSTRUMENT_CONTROLLER、槽位身份和 claim，拒绝角色重复及 IO 冲突，允许 DUT-only/BUS。失败不覆盖已有绑定。模型仍未接入生产 START，不能视为槽位资源租约已经生效。
- 软件回归：service、真实 SCPI 与 PIO 后端/指令模型共 115 项通过，输出 `out/pytest/sequence-dut-none-r1/`；角色容量组合与 GUI 共 33 项通过，输出 `out/pytest/sequence-roles-gui-r1/`；工具相关回归通过。独立只读复核 `/root/feedback_tool_review` 未发现新增阻断问题，明确指出整组 SMA 独占和运行时绑定尚未解决。
- 构建通过显式 `cmake --preset pico2-usb-runtime-switch -B out/build/sequence-dut-none` 配置后完成；日志 `out/node-sequence/dut-none-build-preset.log`，A/B/Boot flash-link 检查通过，目标 build `20260916063153`。早先自动配置未继承 preset、PowerShell stderr 包装报错的记录保留，不用其旧产物作为本轮验收依据。
- 固化 OTA 工具单板上传、复位和提交 PASS，证据 `out/node-sequence/dut-none-ota-r1/`；UID `839E1AE79EA20F31`，重新发现为 COM3，随后每次验收均核对目标 build。设备操作全部通过已保存的 tools 入口执行。
- `out/node-sequence/dut-none-bus-r1.json`：DUT-only BUS 验收 PASS；START 首状态、九次单步、全编码与回绕均符合预期；读回 owned=7，采样中 OUT4/IN2 保持低，STOP 释放输出。采样不能替代独立波形证明。
- `out/node-sequence/dut-none-in1-r1.json`：兼容 PULSE 的 OUT4→IN2 高低回接 PASS；随后以 NONE 模式接收用户确认的 50 Hz IN1 输入，暂停前 accepted=completed=255，恢复后累计 509，busy_rejected=0、faults=0。暂停期间执行计数冻结，持续输入增加 NOT_READY 属于预期拒绝；STOP 为 IDLE，IO 读回 `0,0,0,0,0`。频率为用户设置值，未作独立逐边沿计数。
- 收尾核验：文档命名/元数据检查、文档回归、文档测试（18 项）、逃生门审计和 diff 检查通过；登记表既有命名 WARN 保留。pre-commit 与 check-staged 因无暂存源码而跳过硬件凭证检查，这不代表 P3 通过。`/root/feedback_tool_review` 再次独立核对 OTA、BUS、IN1 原始报告与进度018/TODO，确认结论和未完成边界一致。
- 边界与下一 gate：本轮不证明 VNA 角色执行、READY 超时、独立波形或射频通路，报告相应标记均为 false。NONE 不占状态位不代表 OUT4 可交由另一 owner 独立租用，后端仍独占 SMA 组；双角色必须在统一 PIO0 owner 下组合。下一步接入 Core0 角色激活与 START 不可变快照，封闭原始 RefMem 别名的运行期修改路径，再实现 VNA READY/超时/事实发布。当前 PAUSE 保留租约与编码电平，与长期目标的暂停安全释放仍有差距。未生成或宣称 P3 凭证。

### NSEQ-PROGRESS-20260916-019 - 角色配置事务、槽位提案和独立固件验证

- TODO：`NSEQ-072`、`NSEQ-074`、`NSEQ-076`、`NSEQ-081`；日期：2026-09-16；未提交。以下 build、测试数量、槽位及栈用量为当次快照，非产品事实源。
- `CONF:SEQ:NODE:ROLE` 联合暂存 NODE_LOAD 与真实 FB 启用和资源声明，`READ:SEQ:NODE:ROLE?` 从真实 staging/active 镜像读回，ACT 复用既有 owner 完整包事务。使用 command slot 串行准入，不广播缺少 FB 声明的 NODE_LOAD-only delta。SD staging 替换时清除旧 inline 缓存，避免角色配置复活；独立复核发现的问题已修复并回归。
- 本次硬件配置为 DUT instance5/slot2 与 VNA instance7/slot3。仅启用真实 LINK_SWITCHER / INSTRUMENT_CONTROLLER，不用 MODEL_VNA 或旧 RJ45/UART 模板声明冒充 SMA 测量资源。角色启用不等于 runtime 已绑定或 IO 租约已取得。
- 原始 RefMem LOAD、NODE、BOARD、ACT、SYNC 和转台载入等 SCPI 别名增加序列非 IDLE 冻结；严格校验十进制参数、额外参数与溢出。自动 TDMA RX 的内部 NODE_LOAD 应用入口仍需 owner 层冻结，不能把 SCPI 修复声明为所有写入路径闭合。
- `refmem_slot_claim_derive_proposals()` 支持显式 slot→physical board 提案、非零 claim epoch、同板多槽 policy、真实 board 能力核验及冲突证据；纯模型回归通过，尚未替换生产活动 claim 派生或申请运行租约。不能通过伪造多行物理板身份实现同板多角色。
- 软件验证：角色 staging/真实 SCPI/冻结/提案/解析组合 108 项通过（`out/pytest/sequence-role-config-r1/`）；栈修复后的 SCPI 与冻结回归 100 项通过（`out/pytest/sequence-role-stack-r1/`）；固化角色配置工具 31 项通过（`out/pytest/sequence-role-tool-r1/`）。
- 失败证据保留：build `20260916065928` OTA 成功，但 `sequence-role-stage-hil-r1.json` 中 ROLE 写入超时、USB 断开，`sequence-role-stage-reset-r1/summary.json` 记录 WATCHDOG_TIMEOUT / CORE0_SUPERVISOR_STALL。生产反汇编显示 handler 的对齐 vector 副本占约 4 KiB 栈，加嵌套包校验超出 USB SCPI 任务预算；该证据不是异常 PC 定位。
- 修复为轻量 owner 空闲查询，消除 handler 大型 vector 局部副本，不增加超时或任务栈。显式重新配置独立 build `20260916071550`，日志 `sequence-role-stack-configure-r1.log` / `sequence-role-stack-build-r1.log`；A/B/Boot 构建检查通过，`sequence-role-stack-ota-r1/` OTA 提交 PASS。
- 固化 `tools/hardware_acceptance/sequence_feedback_validate.py --mode role-config`，对 UID `839E1AE79EA20F31` 核对 build 后验证 staged 未提前影响 active、ACT 成功、两角色实际声明读回和 STOP 释放输出。`out/node-sequence/sequence-role-stage-hil-r2.json` PASS，`vna_role_runtime_verified=false`。
- 后续诊断 `sequence-role-stage-postpass-r1/summary.json` 读回 usb_device 栈余量 216 words、复位类别 SOFTWARE_REBOOT；其中错误探测 `SYST:TDMA:RING?` 超时，不能把该工具总 passed 当作每条查询通过。后续物理回环 setup 的 `*CLS` 清理该已知探测错误；正式状态命令以现有 TDMA 工具为准。
- 用户最终选择 RJ45 发收物理回环，替代此前软件直达方案，为后续多板保留同一寻址/调度/接收校验路径；独立 `CONF:SWITCH# N` 和 DUT-only 不取消。下一 gate 为当前 build 物理回环、活动 claim/租约与不可变角色快照，再接入带 run/generation/step 身份的 TDMA 协作、VNA READY 与超时。PAUSE 安全释放、GUI 组合角色及实际协作验收仍未完成，未生成 P3 凭证。

### NSEQ-PROGRESS-20260916-020 - RJ45 物理回环与独立模式回归

- TODO：`NSEQ-079`、`NSEQ-081`；日期：2026-09-16；未提交。以下计数、频率、build 与工具输出为当次快照，非产品事实源。
- 用户已接 RJ45，并明确后续多板兼容方向。使用已有 `tools/tdma_ring_monitor/tdma_single_board_loopback.py` 在 COM3/build `20260916071550` 执行实际 STOP/STAGE/APPLY/TOPOLOGY/PROBE/ARM/TRAIN/START，不使用软件直达替代物理收发。
- `out/node-sequence/rj45-loopback-r1/summary.json` 基础电气/数据环路 PASS；工具加固后 `rj45-loopback-r2/summary.json` 再次 PASS，保留全部原始 SCPI 和采样。r2 收集 18 个样本，adapter TX/RX 分别增长 4726/3056，坏帧及 RX overrun 增量均零，STOP 后上下行状态读回停止。该 RX/TX 聚合计数差不能用作逐角色消息交付率。
- 两轮 `formal_feedback_evidence=true` 是工具读取本轮既有 ring 标志得出的结果，不证明角色 payload 已递交、逐步消息 ACK 或多板时间同步；没有把单板结果升级为角色协作或多板验收。
- 独立复核进一步发现旧判据只看末样本状态及首末计数，可能漏掉中途停止/错误/计数复位。已改为逐样本状态与错误检查、相邻计数不回退、坏帧/溢出计数不能增长或复位，formal 标记须全窗口满足；故障回归及共用工具测试57项通过（`out/pytest/rj45-tool-evidence-r5/`）。
- **严格复测失败，未闭环**：`out/node-sequence/rj45-loopback-r3/summary.json` 收集19个样本，第10号样本 `down_running=0`，随后恢复；adapter TX/RX 增长4992/3208，坏帧/溢出增量零。最终 `passed=false`、`formal_feedback_evidence=false`，STOP 后上下行停止。r1/r2 历史 PASS 不覆盖该失败，当前不能判定持续回环稳定。
- 生产 `tdma_pio_spi_ring_adapter_service_impl()` 的 down_running 由本轮接收或 `last_rx_service_ns` 新鲜度判断。只读写路径审计发现 capture_service_ns 来自 owner 的 last_service_ns，异步 accept 还校验当前时间不早于 capture；现有证据不支持“捕获时间比当前 service 更晚”的猜测。在当次配置下，更符合超过8ms未接纳新RX，但不是已证实物理线缆断流；相邻 PHYS 样本的软件 observation drops 为878→972→1070。需补 owner 边界的下降原因/age 及波形证据才能定因。保留失败，不放宽判断、扩大超时或用偶然通过覆盖。
- 独立模式回归：`rj45-dut-only-regression-r1.json` 验证 NONE/BUS START 首状态、九次单步及回绕；`rj45-standalone-sp8t-r1.json` 验证手动 `CONF:SWITCH1` 各位置与实际 OUT1–OUT3 编码，序列保持 IDLE；`rj45-in1-regression-r1.json` 验证 NONE/IN1，暂停前 accepted=completed=255，恢复后累计511，busy_rejected=0、faults=0。STOP 后输出、armed、busy 和租约均零，外部 IN1 仍可为高。50 Hz 来自用户设定，未独立测频或验收射频通路。
- 手动 SP8T 检查固化为 `sequence_feedback_validate.py --mode standalone-sp8t`，不临时拼接硬件脚本。独立复核指出 IDLE 的序列 STOP 不负责清理手动开关电平，已补专用 finally 归零及读回，并分别保存原始与清理异常；相关工具回归38项通过，见 `out/pytest/sequence-rj45-standalone-r2/`。
- 修复后 `rj45-standalone-sp8t-r2.json` 再验 PASS，保存专用手动归零的实际 IO 读回；上述独立模式验收不证明其与 TDMA 同时运行或双角色协作。
- `/root/rj45_sequence_integration_audit` 只读审计确认尚有物理回帧递交障碍：flight engine 拒绝本机 source/跳过本机 segment，adapter 的 expected owner mask 排除本机，RefMem consumer 再次跳过 local slot。不能只删除上层过滤；须由 TDMA owner 根据真实 RX 回程证据递交 self-return，并保持远端 WKC/freshness 语义。
- 同一审计确认 process-image 是可合并的 latest-value；逐步请求必须保持至 ACK 或进入有界队列。当前固定 physical topology mailbox 不等于 RefMem role slot；需显式 role→claimed board→topology 路由。纯角色 resolver 只做本板执行校验，不能冒充多板路由解析器。具体代码落点为 `tdma_flight_engine.c`、`tdma_pio_spi_ring_adapter.c` 与 `distributed_refmem.c`。
- 下一 gate 已分解至 TODO NSEQ-081：活动 claim/运行配置冻结、容量及协议、物理 self-return payload 验证、统一 PIO0 双角色执行。当前实际 VNA trigger/READY 协作仍未实现，PIO1/PIO2 未修改，未执行多板或生成 P3 凭证。
- 收尾复核：`/root/feedback_tool_review` 对两项工具修复复审通过，明确硬件r3仍未通过。文档元数据/命名及回归检查、18项文档测试、diff检查和逃生门审计通过；登记表既有命名WARN保留。约定Git Bash路径在本机不存在，改用PATH中的 `C:\Program Files\Git\bin\sh.exe` 执行 `sh .githooks/pre-commit`；hook与check-staged因无暂存源码跳过硬件凭证核验，不代表P3通过。未提交或推送。

### NSEQ-PROGRESS-20260916-021 - 双角色主线、有限轮次与首轮硬件验证

- TODO：`NSEQ-015`、`NSEQ-072`、`NSEQ-075`、`NSEQ-076`、`NSEQ-078`、`NSEQ-079`、`NSEQ-081`、`NSEQ-082`；日期：2026-09-16；未提交。以下构建号、计数和测试数为当次快照，非产品事实源。
- 用户要求先完成主线再优化细节，允许把进度020的短暂 `down_running` 掉落留作后续优化。严格 TDMA 工具及其失败证据保留；组合功能工具明确声明 `tdma_stability_verified=false`，没有把有界功能继续升级为稳定性、多板或 P3 通过。
- 用户补充独立与组合两种模式都须可配置轮次，默认只执行一轮，零才显式持续；支持十轮、百轮、万轮等配置。新增 `CONF:SEQ:REP` / `READ:SEQ:REP?`，查询 configured、active、finished。合法总状态数以 START 的 `plan.count * repeat_count <= UINT32_MAX` 校验为准；SP8T 上限读回快照为536870911轮，不是实际执行了该轮次。
- 独立模式 START 预置首状态，有限执行接纳 `count * repeat - 1` 枚推进事件，最后状态的建立/状态动作完成后自动 IDLE、finished、输出低和租约释放。组合模式完成每个状态对应的测量，最后一次 READY 后停止，不请求越过末状态；START 本身仍不伪造一枚序列推进事件。
- PIO0 新增有限 ingress 硬件配额，用 Y 倒计数限制外部准入，最后一次接纳后永久 parked；不靠 CPU 轮询捕获停止时机，也不展开万轮计划缓存。executor 用 `IN Y,32` autopush 节省指令，保持写出/完成回执及建立、脉宽时序；有限 ingress、executor、counter 连同既有 capture 恰好占满当次 PIO0 指令容量。暂停在 request/quota 边界只扣一次配额，恢复保留最终 parked 状态。资源预算引用 `SYNC_IO_SEQUENCE_INSTRUCTION_WORDS`，PIO1/PIO2 未修改。
- 组合模式在统一 SMA/PIO0 owner 内以 gateway pulse 程序替代未使用的 BUS ingress；READY 经 counter/DMA 捕获，每次 fire 排空旧值并建立新捕获边界。READY 回执和脉冲已拉低的 PIO FIFO 凭证分开，READY 不直接发出序列 REQUEST。暂停取消测量等待/脉冲，协调器恢复当前测量；PAUSE 保留编码和租约的既有行为仍未改成安全释放/重取。
- 新增 `CONF:SEQ:LINK OFF` 与 `CONF:SEQ:LINK LOOPBACK,dut_slot,vna_slot,IN通道,OUT通道,pulse_us,timeout_ms,edge`。用户最新明确要求专门的回环模式，规范名称改为 LOOPBACK，RJ45 只作兼容别名，GUI 标为“RJ45物理回环”；早期本轮日志中的 RJ45 写法保留原文。Core0 `trigger_sequence_link` 绑定真实启用角色及 model epoch，经既有 CONTROL mailbox 分片交换 LINK_APPLIED/READY_NEXT；完整 run/generation/binding/step 身份抑制重复与旧事件，Core1 命令槽执行 fire/step/finish。`READ:SEQ:LINK?` 和 GUI 提供模式、角色、输入输出、轮次及过程读回。活动 proposals/claim resolver 与完整多板路由尚未全面接入，不能把专用同板回环称为远端多板模式或全局租约裁决通过。
- 软件验证：PIO生产逻辑/实际汇编仿真51项通过，`out/pytest/sequence-gateway-quota-r2/` 包含79999次推进对应万轮 SP8T、额外输入不推进、零推进与暂停各边界。service/真实SCPI联合回归105项通过，GUI相关47项通过；组合工具24项和独立轮次工具22项通过。主控另补非整除计划的 UINT32 总状态边界：三状态计划允许1431655765轮，增加一轮拒绝，后续最终回归记录由主控补齐。
- 固件 build `20260916100501` 编译完成，经持久化 OTA 工具烧录并确认提交，`out/node-sequence/sequence-cycle-ota-r1/summary.json` 为 PASS。设备 UID `839E1AE79EA20F31`、COM3；以下验收只对应该 build，后续修复构建不能复用其硬件结论。
- 独立有限单板 PASS：`sequence-repeat-in1-r1.json` 在持续 IN1 信号下单轮 accepted=completed=7、finished；`sequence-repeat-in1-100-r1.json` 百轮 accepted=completed=799、finished；`sequence-repeat-bus-r1.json` 软件 NEXT 单轮完成7步。上述文件均位于 `out/node-sequence/`，结束后 IDLE、输出与租约为零，信号源继续运行期间没有额外推进。50 Hz 是用户报告的源设置，没有独立逐脉冲计数或波形/RF测量。
- 显式持续单板 PASS：`out/node-sequence/sequence-repeat-continuous-r1.json` 使用 REP=0 达到观察步数后由工具明确 STOP，停止后计数稳定。上限配置 PASS：`sequence-repeat-max-config-r1.json` 只写入并读回536870911轮，`configuration_verified=true`、`functional_execution_verified=false`；未执行该超长运行，不混淆配置验证和硬件执行。
- **组合首轮未通过**：`out/node-sequence/sequence-cycle-hil-r1.json` 保留 WAIT_APPLIED 超时、未形成角色 RX 消息和零 VNA trigger/READY 的原始记录，`passed=false`、`functional_cycle_verified=false`；清理后序列和 TDMA 停止，输出低。审查定位真实单板 raw echo 的 hop 未被远端站点增加，且 RX 可能落后于最新 TX，原递交条件过窄。修复必须关联真实 TX 历史与 RX 邮箱字节，不能改写原始 hop、伪造远端 WKC 或以本地 TX 成功代替接收。修复、重建、OTA 和新组合 HIL 仍在进行，最终结果须追加，不能由独立模式通过代替。
- 固化工具：`tools/hardware_acceptance/sequence_tdma_cycle_validate.py` 负责组合流程；`tools/hardware_acceptance/sequence_repeat_validate.py` 负责独立 IN1/BUS 有限与显式持续流程。两者复用身份/构建校验、CDC/VISA 与原始命令记录，证据使用新文件拒绝覆盖；查询或清理失败保留，不把“配置万轮”写成“执行万轮”。未运行临时硬件片段。
- 后续失败与当前板端状态：build `20260916163601` OTA 成功，但 `out/node-sequence/sequence-loopback-hil-r1.json` 仍在首个 LINK_APPLIED 等待中超时，角色 RX 消息为零；不能宣称回环闭环通过。随后增加专用回环拒收原因读回并 OTA 到 `20260916164501`，证据 `sequence-loopback-diag-ota-r1/summary.json` 为 PASS；用户要求先拆分提交，此诊断 build 尚未继续硬件闭环测试。上述数字均为运行快照，诊断固件和相关改动仍在工作区。
- 下一 gate：完成真实 raw echo 递交修复与组合首末测量验收，复验有限/持续与独立模式，记录当前 build 的最终 IO 状态、错误和回退。动态 claim/多板、PAUSE 租约释放、其他输入独立激励与波形/RF仍未验收；未生成 P3 凭证，未提交或推送。
- 文档批次验证：`docs_check --strict-names`、`doc_regression_check.py`、文档检查器18项测试（`out/pytest/sequence-progress021-docs-r1/`）、`git diff --check` 和逃生门审计通过；登记表既有 ID 命名 WARN 保留。`sh .githooks/pre-commit` 与 `p3_hardware_acceptance.py check-staged` 因无暂存源码跳过硬件凭证检查，不表示本轮取得 P3 凭证。后续主控追加 HIL 后须再次运行相应文档门禁。

### NSEQ-PROGRESS-20260916-022 - 已验证模型的独立提交

- 用户要求先提交已验证部分，并明确单板调试只做功能确认、不执行 P3。本次仅以临时提交 hook 保留文档检查、按该授权豁免 P3；仓库常设 hook 不改，不生成或宣称 P3 凭证。
- 提交范围为 `refmem_sequence_roles`、`refmem_slot_claim_derive_proposals()`、对应测试及 CMake 的模型源项。角色解析检查真实实例 ID、claim epoch、物理板身份、能力及 IO 冲突；槽位提案引用真实能力表，支持按策略同板多槽，拒绝无效输入时不覆盖已有结果。
- 两个新 API 是纯模型基础，尚未接入生产运行态调用，不代表角色激活、资源租约、VNA 触发或 RJ45 闭环完成。SCPI、PIO、轮次、GUI 与回环的其他在研改动保留在工作区，不混入模型提交。
- 隔离验证使用 `out/node-sequence/verified-model-slice/`：以原 HEAD 加本批模型文件构成独立快照，使用原版 application model，证明不依赖其他未提交改动。主机测试覆盖不同容量、稀疏实例、失败原子性以及旧 claim/protocol；七项通过（本次运行快照，非事实源）。构建与测试原始记录保存在该目录的 `out/`。
- 独立只读复核确认失败路径不修改输出、实例按 ID 查找、claim epoch 独立、输入与输出分别判冲突；未发现阻塞项。代码和文档分离提交，未推送。
- 精确切片的 `pico2-release` 构建通过，A/B/BOOT 的 flash-link 检查通过；build `20260916085327`（构建快照，未烧录）。代码提交为 `b61c612e`，仅包含本批模型与测试。

### NSEQ-PROGRESS-20260916-023 - GUI 分页与当前状态整理

- TODO：`NSEQ-051`、`NSEQ-078`、`NSEQ-081`、`NSEQ-082`、`NSEQ-083`；日期：2026-09-16。仅整理 GUI、测试和状态文档，未修改固件、未操作硬件、未提交或推送；以下数量和尺寸为本次验证快照，非产品事实源。
- GUI 改为独立 SP8T 序列、RJ45 物理回环、手动 SP8T、设备维护四页。独立页分组为序列配置、推进事件和输出/状态反馈；回环页分组为序列配置及 VNA READY/触发参数，固定 DUT 编码和 VNA OUT 分工，无独立软件 NEXT 入口。共用连接栏、IO 读回及可拖动日志区，保留清空/导出日志、CDC/VISA、USB 切换和 OTA。
- 两种模式各自保存配置草稿，切页不发命令。START 只接受当前资源、当前草稿成功配置后的授权；修改参数、换资源、手工写命令、OTA 或 USB 切换后重新配置。配置完成事件带代次与资源身份，旧事件不能授权新设备或覆盖重置后的配置事实；只读查询保持授权。
- STOP/刷新根据设备配置事实处理，查看独立页也能停止先前组合模式的 TDMA；设备仍为组合模式时拒绝手动 NEXT。切换通信资源清除旧开关/IO/占用/轮次/LINK 显示，旧资源迟到响应保留日志但不覆盖当前设备读回；长响应不再挤占连接控件。
- 新增真实 Tk 回归 `tests/python/test_sequence_trigger_debug_ui_layout.py`，禁止打开串口/VISA、执行 OTA。覆盖模式控件、草稿隔离、切页无命令、隐藏配置不影响当前模式、START 授权、STOP/刷新、旧资源/旧配置回包和手工写命令失效。默认与最小窗口布局、长响应下端口栏均通过；截图位于 `out/pytest/sequence-gui-layout-r4/test_visible_controls_fit_wind0/` 和 `test_visible_controls_fit_wind1/`，尺寸分别为 1380×900、1120×820。
- 最终命令：`python -m pytest tests/python/test_sequence_trigger_debug_ui_layout.py tests/python/test_sequence_trigger_debug_ui.py tests/python/test_sequence_tdma_cycle_validate.py tests/python/test_sequence_repeat_validate.py -q -p no:cacheprovider --basetemp out/pytest/sequence-gui-tabs-final-r2`；130 项通过。早期布局运行遇到 Windows Tcl 初始化与 pytest 原生文件描述符捕获冲突，保留失败输出；fixture 仅在创建 Tk 时关闭捕获，最终正常捕获模式全过，无跳过掩盖失败。
- 独立只读复核 `/root/rj45_sequence_integration_audit` 检查 GUI 状态/资源边界与 TODO 原始证据，提出的模式混淆、维护失效、旧 IO 及接续顺序问题均已修复，最终无阻断项。
- TODO 新增当前交付快照与接续顺序：模型代码/文档已提交但未推送；独立功能按对应 build 标记；组合两次 HIL 超时和诊断 build 尚未 HIL 均保留。手动 SP8T 的 build `20260916100501` 证据为 `out/node-sequence/sequence-cycle-standalone-r1.json`，补明确文件名以便追溯。内部自动 RX NODE_LOAD 的 owner 冻结仍待补，不把 SCPI 冻结测试外推为该路径通过。
- 验证边界：本轮只证明 GUI 行为及命令兼容，不证明诊断 build `20260916164501` 的 RJ45/VNA 组合、波形、射频通路或多板通过。按用户单板功能确认要求不执行 P3；下一步仍是固化工具采集 `READ:SEQ:LINK:TRANSPORT?` 并跑通真实 RJ45 首末测量闭环。

### NSEQ-PROGRESS-20260916-024 - real-flight 对照与异步 RX 证据覆盖修复

- TODO：`NSEQ-081`、`NSEQ-084`；日期：2026-09-16。用户要求继续调试并参考 real-flight 最新通信提交；本轮未提交、未推送。以下 build、计数及路径为本次快照，非产品事实源。
- 首先用已固化 `sequence_tdma_cycle_validate.py` 指定 UID `839E1AE79EA20F31`、诊断 build `20260916164501`、单轮、`--gui-control` 尝试闭环。`out/node-sequence/sequence-loopback-diag-hil-r1.json` 和复查 `sequence-loopback-diag-hil-r2.json` 均为发现阶段失败：Serial/VISA 列表为空，未找到唯一目标设备；transcript 为空，未发控制或清理指令。已请用户确认板卡上电与 USB 连接。本轮没有板端诊断读回、OTA 或闭环执行结果。
- `git fetch origin wip/tdma-real-flight-processing` 后核对远端 `cc1d8aeb577c64d8f95f900e75a3a71cfddbcb51`。其中 `66919c27` 提供 Core0 FIFO 借用/STOP reset 保护，`385a9ff0` 提供 TX/事件耗时细分，`aa6c9880` 保留完整 CRC 并用 SRAM kernel 优化；它们未修改本分支专用 local-return 匹配。保留为后续通信整合参考，本轮未整体合并远端、未引入其 VDC 命令原型或硬件验收凭证。
- 独立审计发现本地回环在 Core1 接受 Core0 解析结果时重新查 `reference_tx_evidence`，即使 RX admission 已把当时的 TX 字节保存到 `job.expected`，仍可能因继续发送覆盖历史槽而误拒收。新增测试走真实生产 `enable_async_rx → physical RX → Core0 claim/build → Core1 accept`，捕获时保留的记录有效，解析期间再发送覆盖槽，接受仍在既有新鲜度内；修复前输出 `delivered=0 expected=1 reject=5`。可复现二进制位于 `out/pytest/sequence-async-proof-before-r2/`，补存输出 `out/node-sequence/sequence-rx-proof-reproduction-r1.log`。
- 修复仅在 `tdma_pio_spi_ring_adapter.c` 的 local-return 检查与 `tdma_pio_spi_ring_rx_prepare.inc` 捕获条件：异步路径优先使用捕获时已保存的 TX bytes；缺少捕获证据时不能回借后来的历史槽。LOOPBACK 功能交付不要求 TX 时间锁存有效，但证据依旧只产生于真实 `phys_tx` 成功之后。序号、identity、长度、不可变 header、本地 mailbox、CRC、epoch/map/profile 与 age 检查继续保留；无缓存扩容，无 PIO 程序变更，无虚构 hop/WKC。
- 测试 `python -m pytest tests/python/test_tdma_local_return.py -q -p no:cacheprovider --basetemp out/pytest/sequence-async-proof-final`：容量 2/6/8 三项通过，每个容量含原有 local-return 场景及新增异步覆盖、捕获前过期、内容/CRC/身份损坏、epoch、age、无锁存与重复投递场景；容量 8 同时跑原完整 adapter/resident 测试。直接 inject 仍按当前历史匹配，捕获前证据已过期仍安全拒收。
- 工具增加 `READ:SEQ:LINK:TRANSPORT?` 六字段解析及拒收名称，保留原始响应，未知原因码保留 UNKNOWN，解析失败不覆盖原文；不把最后一次拒收等同根因。诊断、TDMA adapter、LINK 协调器及协议联合回归 70 项通过：`python -m pytest tests/python/test_sequence_tdma_cycle_validate.py tests/python/test_tdma_diagnostic_burst.py tests/python/test_trigger_sequence_link.py tests/python/test_trigger_sequence_link_protocol.py -q -p no:cacheprovider --basetemp out/pytest/sequence-rx-proof-integration-r1`。
- 显式 `cmake --preset pico2-usb-runtime-switch -B out/build/sequence-rx-proof`，再 `cmake --build out/build/sequence-rx-proof`；Release A/B/BOOT 构建与 flash-link 检查通过。候选 build `20260916111629` 为该次生成器 UTC 标识，未烧录；日志 `sequence-rx-proof-configure-r1.log` / `sequence-rx-proof-build-r1.log` 及改动源码/测试/固件包 SHA256 `sequence-rx-proof-artifact-hashes-r1.json` 均在 `out/node-sequence/`。
- `/root/rj45_sequence_integration_audit` 独立只读复核通过：真实 TX 成功证据、跨核快照、新鲜度、CRC 与负向场景保留，无阻断项。证据边界：仅证明捕获后异步覆盖修复，尚不能认定它解释此前硬件 WAIT_APPLIED 超时；捕获前已滞后超过历史窗口仍拒收。设备恢复连接后，先采诊断 build，再 OTA 此候选固件，复测组合首末步/有限与持续，并回归独立 SP8T。按用户要求仅做单板功能确认，不执行 P3。
- 文档命名/元数据、回归及逃生门检查通过，检查器 18 项测试通过（`out/pytest/sequence-rx-proof-docs-r1/`）；仓库原有登记表命名 WARN 保留。按仓库换行配置执行 `git diff --check` 通过；pre-commit 因无暂存源码跳过硬件凭证检查，不表示 P3 通过。

### NSEQ-PROGRESS-20260916-025 - 板卡恢复连接与真实 RJ45 组合主线通过

- TODO：`NSEQ-079`、`NSEQ-081`、`NSEQ-082`、`NSEQ-084`；日期：2026-09-16。用户重新接入板卡，按单板功能确认继续调试，不执行 P3；未提交或推送。以下 build、次数、计数和路径均为本次快照，非产品事实源。
- 设备 UID `839E1AE79EA20F31`、CDC `COM3`；原诊断固件的独立 IN1 单轮先通过 `sequence-diag-in1-baseline-r1.json`。信号源频率按用户申报的 50 Hz 记录，没有独立测频。现有接线为 IN1 接信号源、OUT4 接 IN2、RJ45 输出接输入。
- 失败保留：`sequence-loopback-diag-hil-r3.json` 在 TDMA 状态查询超时；`sequence-loopback-diag-hil-r4.json` 到达 WAIT_APPLIED 后失败，TRANSPORT 拒收为 LAYOUT；`sequence-loopback-layout-hil-r1.json` 保留 START 超时，清理前 PROCESS 实际为 configured=1、active=0。原因是工具/GUI 使用 topology probe，而 adapter.start 无条件禁用 process engine，导致回包无法取得冻结布局。工具清理诊断增加 PROCESS 原始读回。
- 第一版修复保留 explicit local-return 的 engine；host 动态双槽异步回包通过，构建并 OTA `20260916120013`。新板测 `sequence-loopback-layout-new-hil-r1.json` 又在 ARM 失败：runtime ADAPTER_START_FAILED、adapter FLIGHT_MAP_REJECT。单板 probe 没有已准入远端拓扑，原 receive-health 配置要求远端 expected mask，不能直接套用。第一版测试曾提供有效拓扑，未覆盖这个真实启动条件。
- 最终修复：probe 保持 receive-health 未配置，仅显式 local-return 保留真实 process map；RX 先清除所有远端分类 mask，再仅合入真实 TX 字节证明的 local mask。没有本地证明就不发布 FIFO，远端 RX commit/WKC 不执行；非 probe 的拓扑准入规则不变。异步 RX 捕获证据、CRC、identity、epoch/map/profile、新鲜度、mailbox seq16 去重继续保留，捕获前证据过期仍拒收；未扩大缓存/超时，未修改 PIO1/PIO2 程序。
- `out/pytest/sequence-probe-no-topology-final/` 容量 2/6/8 测试通过，覆盖无拓扑 probe 成功、同条件非 probe ARM 拒绝、合法 local+remote 仅递交本地、remote-only 不发布、health/WKC 不增加；`out/pytest/sequence-board-return-integration-r1/` 工具、GUI 命令、LINK 及协议共 120 项通过。独立 `/root/rj45_sequence_integration_audit` 复核无阻断项，检查了备用 tx_forward 路径不能旁路远端门禁。
- 重新配置/构建 `out/build/sequence-rx-proof`，build `20260916121654`；A/B/BOOT flash-link 检查通过。日志为 `sequence-loopback-probe-configure-r2.log`、`sequence-loopback-probe-build-r1.log`，SHA256 为 `sequence-loopback-probe-artifact-hashes-r1.json`。首次 PowerShell 重定向把 CMake stderr 提示映射为失败退出码，原日志 r1 保留；改用 `cmd /c` 原生命令重定向后 configure 返回零，未忽略失败继续构建。
- OTA 使用固化 `ota_multi_update.py`，绑定 COM3、UID、唯一板卡及生成包。默认 picotool 预复位在 `sequence-rx-proof-ota-r1/summary.json` 失败，send/commit 未发生；运行态无可用 picotool reset 接口。后续显式 `--skip-pre-reboot` 跳过可选预复位，仍执行 SCPI OTA、重枚举、boot commit 与身份确认。第一版 OTA 为 `sequence-loopback-layout-ota-r1/summary.json`，最终固件为 `sequence-loopback-probe-ota-r1/summary.json`，均通过。
- 最终组合 HIL 全部使用 `sequence_tdma_cycle_validate.py --serial-number 839E1AE79EA20F31 --build 20260916121654 --source-hz 50 --gui-control`：单轮 `sequence-loopback-probe-hil-r1.json` 最终 triggers=ready=8、completed=7、rxmessages=16；十轮 `sequence-loopback-probe-ten-r1.json` 最终 triggers=ready=80、completed=79、rxmessages=160，轮次读回 `10,10,1`；连续 `sequence-loopback-probe-continuous-r1.json` 完成跨轮推进后由工具 STOP。以上三份报告均 PASS；有限轮次在末次 READY 实际回收后才进入 IDLE，连续模式由显式 STOP 结束。清理输出/租约归零，静默期角色计数不变。单轮物理 TX/RX 均增长，TRANSPORT matches/published 均非零；不是软件直达或仅 TX 成功。
- 同一固件的独立回归：`sequence-probe-independent-bus-r1.json` BUS 单轮、`sequence-probe-independent-in1-r1.json` IN1 十轮、`sequence-probe-manual-sp8t-r1.json` 独立开关位置/OUT 对应及安全复位均 PASS。以上报告、日志和 OTA 目录均位于 `out/node-sequence/`。
- 独立 reviewer 核验首轮完整报告、身份前后、实际 GUI builder/executor、首末采样、物理计数与清理，确认单板主线证据有效。`--gui-control` 表示真实 GUI 命令构造器和批处理执行器直接驱动板卡，不表示人工点击 Tk 或示波器观察通过。波形、射频、多板、独立输入计数、严格 TDMA 稳定性与 P3 均未声明通过。
- 暂停工具固化：`sequence_tdma_cycle_validate.py --repeat 0 --pause-resume` 使用已注册的 `TRIG:PAUS` / `TRIG:CONT`，PAUSED 与 LINK paused 均读回后建立基线，静默采样确认角色计数、SP8T 编码保持及 OUT4 低电平，恢复按新测量增量核验，允许取消暂停时的测量。cleanup 补 TDMA STOP 后真实 enabled/adapter_started 状态核验。`out/pytest/sequence-cycle-pause-tool-r1/` 71 项通过；不把离散 IO 采样当作无毛刺波形验收。
- 新失败保留：`sequence-loopback-probe-pause-r1.json` 在 TRIG:START 超时，尚未进入暂停；同配置未扩大超时重跑 `sequence-loopback-probe-pause-r2.json` PASS，暂停静默期与 CONT 后继续推进均通过。之后再次手动开关 `sequence-probe-manual-sp8t-r2.json` PASS，再切组合 `sequence-loopback-probe-mode-transition-r1.json` 实际单轮完成，但清理前 `SYST:REFMEM:SYNC:TDMA:STAT?` 超时，残留的 SCPI Execution error 在后续 STOP 错误队列检查中读出，整份报告仍 FAIL，不以 functional_cycle_verified 覆盖失败。
- 停止状态独立复查 `sequence-probe-final-stopped-r1.txt`：同一 UID/build，序列 IDLE、OUT/owned/armed/busy 全零、TDMA enabled/adapter_started 为零、错误队列为空。启动间歇拒绝与诊断快照失败仍需分别定位，不能仅凭一次相邻手动操作推断模式切换为根因。
- 下一 gate：定位间歇 START 拒绝，补异常恢复，继续保留动态 claim/租约及内部 owner 冻结缺口；主线有限轮次和组合暂停/继续已有板端通过证据，短暂 down_running 按用户要求后续优化。

### NSEQ-PROGRESS-20260916-026 - START 已发布配置视图与最终单板复测

- TODO：`NSEQ-079`、`NSEQ-081`、`NSEQ-082`；日期：2026-09-16。未提交/推送，按用户要求不执行 P3。以下测试数、构建身份和路径为本次快照，非产品事实源。
- 进度025的 START 失败发生在同步前置检查：旧 run/计数完全未重置，未进入 COMMAND_START；SCPI 失败只入错误队列而无正常响应，通用 -200 不能区分具体前置原因。本次确认并复现其中一个缺陷：LINK 后台 service 持 `s_guard` 时被 START 只读检查打断，会因单次 take 失败误报配置无效。旧实现真实 service 持锁重入测试失败，证据 `out/pytest/sequence-link-start-view-before/`；不能以此断言唯一解释此前硬件间歇失败。
- 修复限于 `trigger_sequence_link.c`：START 从短临界区中读取已发布的 config/model_epoch/binding_epoch，不再争运行态锁；configure 仍持唯一写者锁，用独立 atomic 标志拒绝配置中读旧视图，按发布快照、清配置标志、释放写锁的顺序完成。保留角色/FB、模型代次、binding 复核及 TDMA enabled/started/data/up 门禁。USB 与 RS485 的 SCPI 可来自不同 Core0 任务，因此不宣称 guard 检查至 START 入队全事务原子化，既有 owner 冻结缺口仍保留。
- 新增测试覆盖运行服务持锁下读取不误拒绝、配置中拒绝旧快照、读取 TDMA 时 binding/model 变化及全部原 ring/role gate。`out/pytest/sequence-link-start-view-final-r2/` 14 项通过；独立 `/root/rj45_sequence_integration_audit` 复核无阻断项。工具与 Tk GUI 联合回归 139 项通过，证据 `out/pytest/sequence-pause-gui-regression-r1/`。
- 构建 `out/build/sequence-rx-proof`，build `20260916124155`，A/B/BOOT flash-link 通过；日志 `sequence-start-view-configure-r1.log` / `sequence-start-view-build-r1.log`，源码/测试/包 SHA256 `sequence-start-view-artifact-hashes-r1.json`。固化 OTA 工具绑定原 UID/COM3、`--skip-pre-reboot`，`sequence-start-view-ota-r1/summary.json` PASS。
- 新固件实际复测：`sequence-start-view-manual-r1.json` 手动 SP8T、`sequence-start-view-ten-r1.json` GUI 路径组合十轮、`sequence-start-view-bus-r1.json` 独立 BUS 单轮、`sequence-start-view-in1-r1.json` IN1 十轮均 PASS。`sequence-start-view-pause-r1.json` 实际完成连续运行、暂停/CONT 和恢复推进，但清理阶段 `READ:SEQ:LINK:TRANSPORT?` 超时，后续 STOP 读出残留 -200，整份报告 FAIL 并保留。未扩大超时或自动重发 START；独立重新执行 `sequence-start-view-pause-r2.json` 完整 PASS，包括连续、暂停/继续及 STOP 的真实 TDMA 停止读回。
- `sequence-start-view-final-stopped-r1.txt` 再次独立确认同一 UID、build：序列 IDLE，OUT/owned/armed/busy 全零，TDMA enabled/adapter_started 全零，错误队列为空。上述报告和日志均位于 `out/node-sequence/`；配置仍仅在 RAM，重启需重配。
- 当前结论：单板两种模式主线、有限轮次、连续 STOP 及暂停/CONT 已有当前固件通过证据。诊断快照在 TDMA 活动期间仍可能取不到而返回 SCPI 错误，原失败不被后续 PASS 覆盖；下一步聚焦有界快照读回与错误归因、异常恢复，保留动态 claim/租约及内部 owner 冻结工作。不宣称 TDMA 稳定性、波形、射频、多板或 P3 完成。

### NSEQ-PROGRESS-20260916-027 - 换机受限提交与 risk 交接

- TODO：`NSEQ-051`、`NSEQ-085`；日期：2026-09-16。用户明确授权提交未完成验证的部分、标记risk、由下一代码提交解决，并说明需换机拉取代码。当前交付为功能分支检查点，不是合并或正式验收。
- 代码提交 `ba5d2fb8`：`feat(sequence): checkpoint single-board SP8T and RJ45 VNA cycle [risk]`。保留相互依赖的固件、SCPI、PIO0、TDMA local-return、GUI及工具/测试，共61文件（提交快照，非事实源）；独立 reviewer 确认均属序列主线依赖，没有引入无关改动或修改PIO1/PIO2。此前模型代码/文档提交一并在该分支历史中保留。
- 提交前重新执行全部相关 sequence、RefMem角色/冻结及TDMA回环/非阻塞测试：`out/pytest/sequence-risk-commit-r1/`，700 passed。暂存内容空白检查、文档命名及回归检查通过；`sequence-start-view-artifact-hashes-r1.json` 的源码/测试/固件包指纹逐项匹配当前文件。Release构建、OTA和真实板测证据引用进度026，不用新报告覆盖旧失败。
- gate例外审计：用户已明确单板功能确认、不执行P3；本次按其受限提交授权及治理C3使用显式 `git commit --no-verify`，commit正文记录原因、通过范围与风险。没有修改hook，没有生成或借用P3凭证，不宣称P3门禁通过。文档单独提交并正常执行文档hook；单板例外不自动成为未来提交的常设豁免。
- 未完成项以 `NSEQ-RISK-01/02/03` 登记在TODO：诊断快照失败/SCPI错误归因、配置至START及内部owner冻结、异常与恢复验证。**下一代码提交必须优先解决当前risk并补证据；配套文档提交不算关闭risk。** 动态多板分配、波形、RF和严格TDMA稳定性继续不在已验收范围。
- 可移植证据保存在 [本次单板报告目录](evidence/20260916-single-board/)：当前固件的OTA摘要、手动/BUS/IN1/组合十轮、PAUSE成功与失败、旧START失败与清理失败、最终停止读回及指纹。均由原始报告逐字节复制，保留旧路径作为来源标识；不是重新执行或P3凭证。其余`out/`日志、构建缓存、包和pytest目录不会随Git拉取，不能在另一台电脑假设它们存在。
- 换机接续：拉取 `origin/feature/node-sequence-reservation-trigger`，先读TODO的risk表；配置 `git config core.hooksPath .githooks`。GUI入口为 `python tools/sequence_trigger_debug_ui/sequence_trigger_debug_ui.py`，需要Python/Tk、pyserial，USBTMC另需pyvisa和可用VISA后端。固件构建使用 `pico2-usb-runtime-switch` preset，需要Pico SDK与ARM工具链；本机路径不能照搬。
- 当前板端为进度026的build `20260916124155`、UID `839E1AE79EA20F31`，序列/TDMA已停止、输出零、错误队列空。换机端口号可能变化，应重新枚举并按UID选设备；配置保存在RAM，重启后重新下发。已固化的 `sequence_tdma_cycle_validate.py` 支持 `--gui-control --repeat 10` 或 `--repeat 0 --pause-resume`，必须显式给实际 `--build`、`--serial-number` 与新的 `--out` 路径；接线/源频率仍需与当前实物一致。

### NSEQ-PROGRESS-20260916-028 - risk 软件闭合与单板凭证门禁

- TODO：`NSEQ-085`；日期：2026-09-16。按用户最新口径只做单板功能验收，不执行 P3。以下测试数和构建结果均为本次工作区快照，非产品事实源；当前源码尚未 OTA 或执行新单板 HIL，不写成硬件通过。
- `NSEQ-RISK-01`：TDMA local-return 增加 `tdma_local_return_snapshot_quality_t` 和独立一致缓存；SCPI TRANSPORT 查询始终返回 typed quality。单板工具在每项清理诊断前后读取错误队列，`UNAVAILABLE`、解析失败、遗留错误或本查询产生错误均令报告失败。
- `NSEQ-RISK-02`：sequence service 增加共享配置事务门，START 从校验、LINK guard、运行快照/store 冻结到命令入队保持同一门；参数、IO/code、轮次、LINK、RefMem自动RX/角色/通用装载/激活及模型 staging 使用同一门，LINK 内部锁顺序固定。
- `NSEQ-RISK-03`：LINK wire 以 `TRIGGER_SEQUENCE_LINK_WIRE_SIZE` 为准加入 `exchange_id`；每个 LINK_APPLIED 分配非零新 identity，READY_NEXT 必须回显。暂停恢复、超时停止后重启和旧 exchange 注入测试确认旧消息不产生额外 fire/step。
- 相关 sequence、RefMem 和 TDMA 软件回归共 564 passed；Release `pico2-release` 在 `out/build-risk-closure/` 构建通过，A/B application、bootloader、UF2、OTA package 和 flash link contract 均完成。`git diff --check` 通过。以上只表示软件与构建闭合。
- 在用户收窄范围前曾启动 P3，因配置中的全部板卡均未枚举而在硬件测试前失败；该失败仅作为台架不可用记录，不作为本次单板门禁阻塞，也不重跑或写成 P3 通过。
- 新增 `sequence_single_board_gate.py`：仅接受 `SOURCE_ALLOWLIST` 中逐文件列出的 sequence 风险源码/测试/工具，固定执行 `FINITE_REPEAT` 有限 profile 和连续 PAUSE/CONT profile，绑定 staged 源码指纹、固件包、单板 OTA 摘要、validator 及两份原始报告摘要。任一白名单外源码回到 P3；`check-staged` 不访问硬件且不能生成凭证。
- 门禁自回归首轮 136 passed，覆盖匹配凭证、陈旧源码、证据篡改、白名单外固件、snapshot unavailable、未轮换 exchange、OTA 包不匹配及 pre-commit 分流；合并后的 sequence/RefMem/TDMA/门禁相关套件最终 612 passed。当前源码的单板 `run` 尚未执行，因此未生成 `sequence_single_board_receipt.json`；NSEQ-RISK-01/02/03 为软件修复完成、板端验证待完成，`NSEQ-085` 保持 IN PROGRESS。

### NSEQ-PROGRESS-20260916-029 - SCPI NEXT 合并与无 IN1 单板验收

- TODO：`NSEQ-079/081/085`；日期：2026-09-16。按用户要求使用软件触发模拟外部 READY，保持 RJ45 发收物理回环，不执行 P3。以下 build、计数和摘要均为本次验收快照，非产品事实源。
- SCPI 软件推进合并为 `TRIGger:SEQuence:NEXT` 与 `TRIGger:SEQuence:NEXT?`；旧 `TRIGger:SEQuence:STEP`、`CONFigure:SEQuence:NEXT`、`READ:SEQuence:NEXT?` 从注册表移除。独立 BUS 模式仍直接请求一步；LOOPBACK 模式只向 Core1 owner 提交当前 gateway READY，随后由 READY_NEXT 经真实 RJ45 发送、接收和完整 identity 校验后请求 DUT 切步，不允许 SCPI 直达 DUT cycle step。
- host 回归 `617 passed`；新的 `pico2-release` 完整构建位于 `out/build-sequence-scpi-next/`，build `20260916155106`，A/B application、bootloader、factory UF2、OTA package 和 flash link contract 均通过。
- 严格单板 OTA 证据为 `out/HardwareAcceptance/20260916/sequence-scpi-next-ota/summary.json`：限定 UID `839E1AE79EA20F31` 和单板数量，目标 build 读回一致，OTA 提交后错误队列为空。应用态串口复位产生的 reset 关闭提示保留在原始输出，重枚举、build、slot、result 和 commit 检查均通过，未使用 diagnostic continue。
- 有限 profile `sequence-scpi-next-single-board/finite.json` PASS：十轮 SP8T 共记录 80 个唯一 `(run,generation,step,exchange_id)` SCPI NEXT，最终 trigger/READY 为 80、completed 为 79、phase DONE，轮次读回 `10,10,1`；TDMA adapter TX/RX 计数分别增长 14832/6759。
- 连续 profile `sequence-scpi-next-single-board/pause-resume.json` PASS：记录 20 个唯一 SCPI NEXT，PAUSE 静默检查通过，CONT 后 exchange identity 轮换并恢复推进至 completed 19；STOP 后序列、输出、租约与 TDMA 均清理，`cleanup_failures` 为空。
- `config/hardware_acceptance/sequence_single_board_receipt.json` 已生成并经 `check-staged` 通过，绑定 staged 源码指纹、当前 package、OTA 摘要、两份原始报告、UID 和 build。该结果只关闭单板 RJ45 功能 risk，不证明独立 IN1 边沿、外部波形、RF、多板、P3 或严格 TDMA 稳定性。

### NSEQ-PROGRESS-20260917-030 - MANUAL 来源统一与 RJ45 手动 READY

- TODO：`NSEQ-086`；日期：2026-09-17。用户确认序列对外来源统一改为 `MANUAL`，不保留 `BUS` SCPI 兼容别名；本次继续使用单板功能门禁，不执行 P3。
- 独立模式的数值来源零改名为 `MANUAL`，仍由 `TRIGger:SEQuence:NEXT` 直接请求一步。LOOPBACK 的 READY 来源扩展为 `MANUAL` 或 IN1-IN4；只有 MANUAL 可以接受 SCPI NEXT，外部输入模式拒绝软件 READY。
- sync_io gateway 使用显式 enable 与输入通道分离。MANUAL 仍由 PIO 输出 VNA trigger pulse，但不启用 READY counter、输入 SM 或边沿 DMA；软件 READY 只结束当前 gateway 等待。READY_NEXT 仍须经过真实 RJ45 发送、接收及 run/generation/binding/step/exchange identity 校验后，Core1 才请求 DUT 切步。
- MANUAL 的 `LINK_WAIT_READY` 不应用外部 READY 等待超时；WAIT_APPLIED、WAIT_RETURN、WAIT_STEP 等其他阶段保持有界超时。GUI 在发送 NEXT 前读取 23 字段 LINK 状态，来源、phase 或 error 不满足时不发送 setter，避免把固件拒绝误报为普通串口超时。
- host 全量 sequence/role/P3 gate 相关回归 `620 passed`；`pico2-release` 完整构建位于 `out/build-sequence-manual/`，build `20260916164200`，A/B application、bootloader、factory UF2、OTA package 和 flash link contract 均通过。以上计数和 build 均为本次验收快照，非产品事实源。
- 严格单板 OTA 证据为 `out/HardwareAcceptance/20260917/sequence-manual-ota/summary.json`：限定 UID `839E1AE79EA20F31` 和单板数量，目标 build 读回一致，send、boot、slot apply、commit 及错误队列检查通过，未使用 diagnostic continue。
- 有限 profile `sequence-manual-single-board/finite.json` PASS：十轮读回 `10,10,1`，记录 80 个具有唯一 exchange identity 的 SCPI NEXT；连续 profile `pause-resume.json` PASS：记录 20 个 SCPI NEXT，PAUSE 静默及 CONT 后 exchange identity 轮换通过。两组清理后 IO 输出、租约、armed 和 busy 均为零，`cleanup_failures` 为空。
- `config/hardware_acceptance/sequence_single_board_receipt.json` 已生成并经 `check-staged` 通过，绑定 staged 源码指纹、build、package、OTA 摘要及两份原始报告。`NSEQ-086` 关闭；历史报告和命令行中的 BUS 仅代表当时接口名称，不改写原始证据。本次不执行 P3，结果不证明多板、波形、RF、独立输入边沿或严格 TDMA 稳定性。

### NSEQ-PROGRESS-20260917-031 - RJ45 显式 OUT 属性与紧凑布局

- TODO：`NSEQ-087`；日期：2026-09-17。用户要求 RJ45 物理回环与独立 SP8T 一样显式配置 OUT 属性，并缩短输出/状态属性下拉框，使四路保持在边框内同一行。
- RJ45 页面新增独立草稿的 OUT1-OUT4 属性行，默认快照仍为三路编码和一路触发，但命令构造不再写死 mask 或 OUT4。配置时从勾选和角色生成编码 mask 与实际 VNA `OUTx`；必须恰好一路启用为 VNA 触发，且不得与编码输出重叠。独立页保持编码/状态语义，两页下拉显示缩短为“编码/状态/触发”。
- host 定向回归 `150 passed`，随后 sequence/role/gate 全量相关回归 `626 passed`；包含将 VNA 触发改到 OUT3、编码改用 OUT1/OUT2/OUT4 的命令验证，以及非法 OUT、重叠、多个或缺少触发输出的拒绝。真实 Tk 在两种窗口尺寸下验证四路选择器位于边框内且同排；以上数量为本次测试快照，非产品事实源。
- 当前 build `20260916164200` 未改变固件。复用其严格 OTA 摘要后执行 `out/HardwareAcceptance/20260917/rj45-explicit-out-single-board/`：有限十轮与连续 PAUSE/CONT 均 PASS，分别记录 80/20 次 SCPI NEXT，exchange 轮换通过，清理后 IO 和租约全零；新 receipt 经 `check-staged` 通过。本次硬件使用默认物理接线，只证明新 GUI 构造器的显式默认分配可运行，不证明非默认 OUT 接线的外部波形或 RF 通路。

### NSEQ-PROGRESS-20260917-032 - 单板默认USB切换构建与实测

- TODO：`NSEQ-088`；日期：2026-09-17。用户要求编译USB运行时切换，后续单板调试默认启用。使用已有`pico2-usb-runtime-switch` preset，不改普通Release默认值；TODO增加统一构建与OTA入口。以下build与次数为本次验证快照，非产品事实源。
- 新构建目录`out/build/sequence-usb-runtime-20260917`，build `20260917012959`；cache确认`PROJECT_ENABLE_USB_RUNTIME_SWITCH=ON`、`PROJECT_USB_DEFAULT_MODE=CDC`。A/B/BOOT flash-link检查通过；日志`out/node-sequence/sequence-usb-runtime-configure-20260917-r1.log`和`sequence-usb-runtime-build-20260917-r1.log`。
- OTA通过固化工具限定UID `839E1AE79EA20F31`与单板数量，使用`--skip-pre-reboot`；`out/node-sequence/sequence-usb-runtime-ota-20260917-r1/summary.json`为PASS。由旧CDC固件COM10重新枚举为COM3，新build及`SYST:USB:MODE?`读回CDC，错误队列为空。
- 新增固化`tools/usb_runtime_switch/usb_runtime_switch.py`，先核验UID/build，再停止序列/TDMA、读取IO与owner停止状态，设置MODE并读回后BOOT；重枚举必须再次核验目标传输、UID、build和mode，无盲重发。配套mock回归12项通过；该维护工具不生成验收凭证，也没有修改单板gate白名单。
- 真实CDC→USBTMC→CDC→USBTMC全部PASS，报告为`out/node-sequence/sequence-usb-to-tmc-20260917-r1.json`、`sequence-usb-back-cdc-20260917-r1.json`、`sequence-usb-final-tmc-20260917-r1.json`。最终设备保留USBTMC，VISA资源`USB0::0xCAFE::0x4030::839E1AE79EA20F31::INSTR`；默认构建启用能力与当前设备选择的模式分别记录。
- VISA下外部IN1组合十轮PASS，报告`out/node-sequence/sequence-usb-tmc-external-ten-20260917-r1.json`；没有注入软件READY。信号源沿用先前50Hz参数作报告元数据，本轮未独立测频。PyVISA曾提示响应没有终止字符，原输出保留，未以该提示替代响应与状态核验。
- 失败保留：`out/node-sequence/sequence-usb-cdc-ten-20260917-r1.json`的软件NEXT组合回归在第13次测量等待时返回超时及SCPI -200，未完成十轮；停止清理已执行。仅确认USB能力与VISA外部输入通过，不能宣称新构建的软件NEXT固定门禁通过。下一步定位该执行拒绝，不靠自动重发或扩大timeout掩盖。
- 本轮未修改固件源代码、未生成新staged硬件凭证、未提交；新增维护工具不在当前单板gate白名单，后续提交必须按当时实际范围执行适用门禁，不能以本次切换报告替代。

## NSEQ-PROGRESS-20260917-033：START首项响应延时与状态输出

- 关联NSEQ-090。按最新要求，独立START从无序列进入首状态也执行状态输出：编码稳定后等待配置的`settle_us`响应延时，再执行PULSE或LEVEL；脉宽单独使用`pulse_us`，NONE不输出状态。此行为替代进度014/015历史版本的“START不发完成脉冲”，历史证据不改写。
- `sync_io_sequence.c`让首项复用PIO executor的writing/settling/status路径；校验并消耗首项两条真实回执，不增加accepted/written/completed；首项状态动作结束前关闭输入准入。有限单项零后续步仍输出首项状态，再发布finished；启动中STOP不伪造推进，暂停不提前开输入。PIO指令和PIO1/2不变，RJ45的NONE路径仍由真实LINK_APPLIED回环请求网关触发。
- GUI统一显示“响应延时”，提示启动和后续切步顺序。固化`tools/hardware_acceptance/sequence_start_validate.py`，覆盖PULSE/LEVEL/NONE、单项有限、热加载、启动高电平期间STOP及重启；OUT4→IN2采用延长建立/脉宽后的pad采样，不声明精确延时、独立脉冲计数或波形验收。
- 软件验证：backend/service/SCPI/link共205项通过，`out/pytest/sequence-start-backend-20260917-r1/`；GUI布局/命令与已有USB工具共89项通过，`out/pytest/sequence-start-ui-20260917-r1/`；新增启动验证工具与repeat共42项通过，`out/pytest/sequence-start-tool-r3/`。PIO回归包含真实生产STOP的初始零/一/两条回执边界，验证零推进、输出归零及资源释放。
- 构建快照（非事实源）：`pico2-usb-runtime-switch`生成build `20260917035128`，A/B/BOOT flash-link检查通过；日志`out/node-sequence/sequence-start-status-configure-20260917-r1.log`、`sequence-start-status-build-20260917-r1.log`，包和固件源码SHA256见`sequence-start-status-hashes-20260917-r1.json`。本轮在Windows原生PowerShell/CMD调用现有CMake工具链。
- 硬件阻塞：`sequence-start-status-to-cdc-20260917-r1.json`报告找不到目标UID；`sequence-start-status-discover-20260917-r1.json`及r2均为串口/VISA列表为空。未执行新固件OTA、OUT实测或RJ45回归，不能以进度032旧build结果替代；等待板卡重新枚举。
- 未暂存、未提交、未生成硬件凭证。新增工具和测试不在当前单板gate逐文件白名单，后续提交需按实际staged范围处理适用门禁，不修改白名单或借旧凭证放行。软件NEXT历史失败NSEQ-089保持独立待办。
- 独立只读复审通过；文档双检查、文档测试和`git diff --check`通过。配置文档指定的旧Git Bash路径不存在，改用当前`C:/Program Files/Git/bin/sh.exe`运行pre-commit；因无staged源码，hook及P3检查跳过硬件验证，单板check-staged明确返回“no staged source change”，均不代表硬件验收通过。

## NSEQ-PROGRESS-20260917-034：START输出OTA与软件触发单板验证

- 用户确认板卡重新接入，无信号源，要求用软件指令模拟触发；随后补接OUT4→IN2。本轮仅使用MANUAL与`TRIG:SEQ:NEXT`，没有把软件推进声明为外部输入边沿验收。
- 固件包及`sync_io_sequence.c/.pio`的SHA256与进度033编译记录一致；UID `839E1AE79EA20F31`在COM3枚举，`tools/ota_multi_update/ota_multi_update.py`限定单板UID、COM3及板数，OTA与boot/commit通过，板端build为`20260917035128`。报告`out/node-sequence/sequence-start-status-ota-20260917-r1/summary.json`；USB运行时切换仍启用，本轮保持CDC。
- 首轮`sequence-start-status-hil-20260917-r1.json`失败原样保留：实际OUT4已拉高但IN2未跟随。依照用户软件模拟范围，为固化工具增加显式`--output-only`，仍检查全部输出与序列计数，报告`io_loopback_verified=false`；22项工具回归通过，`out/pytest/sequence-start-output-only-r1/`。无回接的r2全部阶段通过，未覆盖或改写r1。
- 用户接好OUT4→IN2后，以默认严格回接模式运行`sequence-start-status-hil-20260917-r3.json`，全部阶段PASS：编码先输出、响应延时期间状态低、随后状态高、脉冲结束后低；PULSE/LEVEL/NONE、PIO热加载、单项单轮、启动中STOP和重启均通过。START推进计数保持零，NEXT只推进一项，静置不自动推进；清理后IDLE、输出归零、资源释放、无cleanup失败。延长的delay/pulse用于SCPI采样，仅为电平与执行顺序确认，不声明精确波形时序。
- `sequence-start-status-manual-ten-20260917-r1.json`独立软件十轮PASS：START首项加后续79条NEXT，最终finished置位并自动停止；无外部信号源，未验收PIO输入准入。
- 组合回归失败保留：`sequence-start-status-rj45-manual-ten-20260917-r1.json`使用GUI配置构造器及软件READY，真实RJ45回环运行到triggers=16、ready/completed=15、exchange=16时，`TRIG:SEQ:NEXT`超时，清理读取保留SCPI -200。复现NSEQ-089同类超时/-200现象，根因待定位；不能宣称组合十轮通过。之后严格回接工具再次完成停止和IO资源清理。
- 本轮没有更改固件源代码、没有提交或生成staged凭证；启动验证工具的新增软件测试和实际硬件运行绑定上述报告，后续提交仍须执行适用门禁。NSEQ-090独立启动输出已完成功能确认，RJ45完整回归仍受NSEQ-089阻塞。

## NSEQ-PROGRESS-20260917-035：接回IN1后的外部触发确认

- 用户接回信号源并确认IN1、50Hz；电压未重新确认，频率为用户声明而非本轮独立测量。设备已切至USBTMC，发现报告`out/node-sequence/sequence-start-external-discover-20260917-r1.json`，所有运行均核验UID `839E1AE79EA20F31`及build `20260917035128`。本轮没有重新编译或OTA。
- `sequence-start-external-ten-20260917-r1.json`：独立SP8T十轮PASS，外部IN1驱动79次后继推进，START首项包含在完整轮次中；配置/活动轮次均为十，finished置位，自动IDLE并释放输出，软件NEXT发送数为零。
- `sequence-start-external-rj45-ten-20260917-r1.json`：GUI命令构造器配置真实RJ45回环，IN1作为VNA READY，十轮PASS；触发/READY各80，DUT完成79次后继推进，真实RX消息160，最后一步采样完成后结束，没有额外推进或软件READY注入。此为固件计数及单板闭环证据，不是独立脉冲计数或严格TDMA稳定性证据。
- `sequence-start-external-rj45-pause-20260917-r1.json`：显式持续模式PAUSE/CONT/STOP通过，暂停静置不推进，恢复后继续执行，最终停止及资源清理通过。三份报告cleanup_failures均为空；设备保留USBTMC并停止。PyVISA的响应终止符提示保留，实际返回及状态检查均通过。
- NSEQ-090的独立首项输出、响应延时、热加载/停止及外部驱动、组合首次至末次测量回归已获当前build功能证据，标记DONE；NSEQ-089软件READY的NEXT拒绝仍待定位，不能用外部输入通过替代。OUT4→IN2电平证据沿用同build进度034；未声明精确波形、RF、多板或P3验收通过。

## 验证与证据索引

| 关联任务 | 证据 | 状态 |
|---|---|---|
| NSEQ-001 | NSEQ-PROGRESS-20260915-003 与 `out/node-sequence/` 下文档 gate 输出 | 自动门禁与独立复核 PASS |
| NSEQ-002 | NSEQ-PROGRESS-20260915-001/002/003 的源码、指令表和技术报告对照 | 只读审计与独立复核 PASS |
| NSEQ-010 | NSEQ-PROGRESS-20260915-004/005 与 `out/node-sequence/nseq010-*` | 模型测试/构建/软件复核 PASS；P3 设备发现及 staged 凭证 gate FAIL |
| NSEQ-011/020/021 | NSEQ-PROGRESS-20260915-008、`sp8t-hil-r1.json`、`single-node-hil-r3.json` | 新固件配置/软件切步/SP8T回绕/IO读回PASS |
| NSEQ-022 | NSEQ-PROGRESS-20260915-009、`acceptance-tool-*` | 正式工具、BUS实测及无输入负向检查；不替代外部波形验收 |
| NSEQ-015/030 | NSEQ-PROGRESS-20260915-008/010与完整单板回归 | GPIO版各源双沿ARM/STOP和IO读回PASS，IN1已接线验证；其他输入激励与PIO版待验收 |
| NSEQ-031/032 | NSEQ-PROGRESS-20260915-010、`in1-50hz-*.json` | IN1低频功能PASS；其他输入激励、独立波形/RF仍待验收 |
| NSEQ-060/061/062 | NSEQ-A-06及进度011/012 | PIO0热加载与50Hz/1kHz功能通过；后续压力档待测 |
| NSEQ-063 | NSEQ-PROGRESS-20260915-014/015、`out/node-sequence/sequence-start-prime-bus.json`与`out/node-sequence/sequence-usbtmc-bus.json` | START首状态、首次触发及回绕的CDC/USBTMC BUS单板验收PASS；P3与OUT4外部波形待完成 |
| NSEQ-040 | 分布式预约和全节点裁决 | 按最新范围后续实施，本轮不执行 |
| NSEQ-072/074 | NSEQ-PROGRESS-20260916-019、`sequence-role-stage-hil-r2.json` | 真实角色表配置/激活通过；运行绑定未完成 |
| NSEQ-079/081 | NSEQ-PROGRESS-20260916-020、`out/node-sequence/rj45-*` | 独立模式回归通过；严格物理回环r3存在中途down_running异常，角色payload通路未完成 |
| NSEQ-072/075/076/078/081/082 | NSEQ-PROGRESS-20260916-021、`sequence-cycle-ota-r1/summary.json`、`sequence-cycle-hil-r1.json` | 双角色与轮次实现及 OTA 已完成；组合首轮超时失败，修复后新 HIL 待补 |
| NSEQ-079/082 | NSEQ-PROGRESS-20260916-021、`out/node-sequence/sequence-repeat-*.json` | 新固件独立 IN1有限/BUS有限/显式持续通过；超长轮次仅配置验证通过 |
| NSEQ-078/083 | NSEQ-PROGRESS-20260916-023、`out/pytest/sequence-gui-tabs-final-r2/` | GUI 分页、Tk 布局与命令/工具回归通过；当前固件 GUI 硬件联调待完成 |
| NSEQ-081/084 | NSEQ-PROGRESS-20260916-024、`sequence-loopback-diag-hil-r1/r2.json`、`out/pytest/sequence-async-proof-final/`、`sequence-rx-proof-build-r1.log` | 异步证据覆盖复现/修复及构建通过；设备未枚举，候选固件未 OTA，硬件闭环未验证 |
| NSEQ-079/081/082/084 | NSEQ-PROGRESS-20260916-025、`sequence-loopback-probe-ota-r1/summary.json`、`sequence-loopback-probe-*.json`、`sequence-probe-independent-*.json`、`sequence-probe-manual-sp8t-r1.json` | 新固件单轮/十轮/连续STOP真实RJ45组合通过；同固件独立BUS/IN1与手动SP8T通过，历史失败保留 |
| NSEQ-079/081/082 | NSEQ-PROGRESS-20260916-026、`sequence-start-view-ota-r1/summary.json`、`sequence-start-view-*.json`、`sequence-start-view-final-stopped-r1.txt` | START读视图修复及当前固件组合十轮、连续PAUSE/CONT/STOP、独立BUS/IN1/手动SP8T通过；诊断超时失败仍保留 |
| NSEQ-085 | NSEQ-PROGRESS-20260916-028、`out/pytest/sequence-single-board-gate-r1/`、`out/build-risk-closure/` | risk软件修复、Release与受限单板门禁完成；当前源码单板OTA/HIL及staged凭证待执行，不代表P3通过 |
| NSEQ-079/081/085 | NSEQ-PROGRESS-20260916-029、`sequence-scpi-next-ota/summary.json`、`sequence-scpi-next-single-board/finite.json`、`pause-resume.json`、`config/hardware_acceptance/sequence_single_board_receipt.json` | SCPI NEXT 软件 READY、真实 RJ45 回环有限/连续单板验收及 staged 凭证通过；不代表 P3、多板、波形、RF、独立输入或严格 TDMA 稳定性 |
| NSEQ-086 | NSEQ-PROGRESS-20260917-030、`out/build-sequence-manual/`、`sequence-manual-ota/summary.json`、`sequence-manual-single-board/finite.json`、`pause-resume.json`、`config/hardware_acceptance/sequence_single_board_receipt.json` | MANUAL 统一、gateway 输入分流、host/Release、严格单板 OTA、有限十轮及连续 PAUSE/CONT gate 通过；不代表 P3、多板、波形、RF、独立输入或严格 TDMA 稳定性 |
| NSEQ-087 | NSEQ-PROGRESS-20260917-031、`out/pytest/rj45-out-final-r2/`、`rj45-explicit-out-single-board/finite.json`、`pause-resume.json`、`config/hardware_acceptance/sequence_single_board_receipt.json` | RJ45 显式 OUT 属性、紧凑同排布局、命令负向回归及默认接线单板 gate 通过；非默认 OUT 仅验证命令构造，不代表外部波形或 RF 验收 |

### NSEQ-PROGRESS-20260917-036 - 第三模式软件实现与RAM集成前检查

- TODO：`NSEQ-091/092/093/094/095`；日期：2026-09-17。先写入用户确认规则，再实现COUNTER/DUT/VNA三槽位、POSITION SCPI、PIO0独立累计计数与RJ45整计划采样状态机；START预置首项但等阈值后才采样。采样期允许原始脉冲，下一完整位置阈值忙时故障；位置结束保留余数。GUI新增独立第三页和按需末条历史查询。
- 软件回归：SCPI/owner运行时172项通过（`out/pytest/position-owner-r3`）；链路、协议及新固化工具56项通过（`out/pytest/position-compact-link-r1`）。worker另完成GUI/Tk95项、驱动资源与计数回归；独立复核发现并修复恢复边界、重装与暂停交错、非法阈值准入、停止末次计数读回问题。计数快照不等于物理锁存时间戳。
- 首轮USB切换preset配置成功，链接失败：RAM超限1896字节。保留`out/node-sequence/position-configure-20260917-r1.log`、`position-build-20260917-r1.log`；第三模式尚未OTA或硬件验证。已实施本地紧凑历史与编码缓存候选，后续实际布局和余量以新构建map为准。
- 已固化`tools/hardware_acceptance/sequence_position_validate.py`：校验身份/固件、真实GUI配置和读回、IN1计数及OUT4→IN2夹具READY、真实RJ45两位置整轮及历史，然后故意不回应MANUAL READY以验证下一位置忙故障。工具host测试通过，尚未运行设备。
- 用户指示先参考real-flight RAM压缩。已fetch `origin/wip/tdma-real-flight-processing`，当次HEAD为`6ba57876`；识别`e6ff06ae`及配套文档`e2d7cda6`：反射内存静态表由65536缩为18432字节，保留节点数量、区域ID与owner，节点步长缩小、layout升v2，产包工具和旧包拒绝校验同步。早期共享工作区压缩`c8b5f4ac`已包含在当前分支，不能重复计算收益。此段数值为上游代码对比快照，非本分支事实源。
- 下一gate：评估并移植RefMem布局压缩必要切片，保持序列角色事务和owner边界；重新运行布局/产包/序列回归、构建、单板OTA及三模式回归。未把上游P3凭证用作本分支验收，未提交，硬件仍为进度035固件。

### NSEQ-PROGRESS-20260917-037 - RAM切片、VISA OTA与第三模式单板实测

- TODO：`NSEQ-089/091/092/093/094/095`；日期：2026-09-17。本节build、次数和内存数值均为当次快照，非产品常量事实源。
- 移植real-flight `e6ff06ae`的RefMem布局/产包/测试必要切片，保留当前序列角色入口，不引入优先接收及VDC调度改动。静态表节省47104字节；layout为新版本、封装格式独立，旧CRC正确部署包仍由owner拒绝。C11审核见`docs/check/submissions/REFMEM_CROSS_REVIEW_01.md`，契约维持pending。
- `pico2-usb-runtime-switch`构建`20260917090914`成功，A/B/BOOT flash-link通过；日志`position-configure-20260917-r2.log`、`position-build-20260917-r2.log`。A槽map显示预留heap后到RAM结束剩47500字节，不视为运行时栈余量测量；本地编码缓存和历史也采用紧凑存储，历史窗口引用代码容量符号。
- 先核验UID `839E1AE79EA20F31`及旧build，用固化`visa_ota_send.py`通过USBTMC发送并BOOT，再用`ota_boot_commit.py --skip-boot`自动重连USBTMC核验新build并确认槽位。日志`position-visa-ota-20260917-r1.log`、`position-visa-commit-20260917-r1/summary.json`；commit PASS，错误队列为空。未复制上游验收凭证。
- N为100、声明信号源50Hz、IN1计数、OUT4→IN2模拟READY、真实RJ45回环：`position-hil-20260917-r4.json` PASS。首阈值前无采样，两个位置共16次触发/READY、15次后继切步，16条历史请求/完成/采样标志齐全；原始脉冲在测量期间增长。故意不回应MANUAL READY时，在第二阈值由Core1报告COUNTER_BUSY并停止。
- 旧模式同build回归：`position-regression-independent-20260917-r1.json`独立IN1十轮PASS；`position-regression-rj45-manual-20260917-r1.json`真实RJ45软件READY十轮PASS。NEXT改用发布快照避免writer guard争用；确定性抢占测试通过，不盲重发失败指令。
- START回归`position-regression-start-status-20260917-r1.json`失败保留：OUT4已高但该条先采的IN2仍低。SCPI两次GPIO读跨边沿是实现层的可能原因，单条失配不证明接线损坏。工具保留失配并要求有界窗口内匹配，持续失配仍失败；修后r2七阶段PASS。该证据不证明独立波形脉宽或边沿时延。
- POSITION首轮`position-hil-20260917-r1.json`正常流程通过但故障归因失败：工具错误匹配BACKEND，生产返回BACKEND_FAULT；已按实际SCPI修正并补回归。权限切换中r3发生VISA超时，报告保留；r4是新独立运行，未覆盖旧失败。
- 最终软件回归：`out/pytest/final-position-r1`的布局、产包、SCPI、GUI及工具202项PASS；`out/pytest/position-final-runtime-r1`的驱动、服务、SCPI运行时、LINK/协议223项PASS。工具审核另外补运行身份不漂移、终态错误、慢查询越过观察窗口、每项历史和错误状态检测，`out/pytest/position-tool-review-r2`57项PASS。
- N为1000的`position-n1000-lifecycle-20260917-r1.json`通过完整流程及PAUSE/CONT/STOP/restart；独立审核后加强工具身份、截止时间和连续首位置历史校验，r2复验PASS。随后补异步START旧IDLE窗口的有界等待，最终工具`position-final-lifecycle-20260917-r1.json`在N100复验PASS；有限正常、预期故障、故障后重配置恢复、连续模式首位置、暂停计数/恢复和STOP重启均验证。最终工具/文档77项回归PASS，见`out/pytest/position-final-tools-docs-r1`。
- 最终START工具七阶段`position-final-start-status-20260917-r1.json` PASS；独立审核方`/root/rj45_tool_evidence`只读复审确认工具身份、截止时间、历史与异步START问题均关闭。
- 验收结束序列及TDMA均停止，编码/触发输出和资源所有权归零，设备保持USBTMC。所有单板报告仅证明输入夹具、编码与角色流程，不代表真实VNA、SP8T射频、独立脉冲计量、多板或P3。未暂存或提交；源码范围包含RefMem布局等受限白名单外变更，单板功能报告不替代提交门禁凭证。

### NSEQ-PROGRESS-20260917-038 - 1kHz、每度一次位置提速失败

- TODO：`NSEQ-096`；日期：2026-09-17。用户当前信号源已设1kHz；目标为模拟5°/s、每1°触发一次，故本档N200，位置间隔200ms。后续5k/10k/100k分别用N1000/2000/20000；均为本次测试计划快照，取消1MHz档。
- 使用已固化`sequence_position_validate.py --threshold 200 --source-hz 1000 --duration 15`，同UID和build `20260917090914`、USBTMC、真实RJ45、IN1计数和OUT4→IN2夹具READY，报告`out/node-sequence/position-1khz-n200-20260917-r1.json`为FAIL。
- 阈值前观察到累计195且未触发；后续接纳第一位置并产生一次VNA触发和一次READY。累计367的读回仍是LINK_WAIT_RETURN，切步completed为零；累计401时Core1报告`SYNC_IO_SEQUENCE_FAULT_COUNTER_BUSY`（SCPI backend_fault14），LINK error5。下一个位置已到而首轮未完成，保护按规则停止，没有放宽阈值或排队补采。
- 结论：当前配置未满足每200ms完成整轮SP8T采样。故障前停在READY_NEXT的RJ45回程等待，仍需分解消息分片、收发与owner调度延迟；此观察不足以独立证明具体根因，也不把低速通过外推为本档通过。
- 工具清理确认IDLE，输出、owned、armed、busy均归零，TDMA已停；完整失败及清理证据保留。此测试未改固件、不需要重新编译，不能声明独立边沿计量通过。

### NSEQ-PROGRESS-20260917-039 - 回退100Hz、N20仍未满足位置周期

- TODO：`NSEQ-096`；日期：2026-09-17。用户改测100Hz与500Hz，并确认当前源为100Hz；保持5°/s、每1°一次位置，本档N20，下一档N100。这里频率、计数和周期为当次测试快照。
- 固化工具使用`--threshold 20 --source-hz 100 --duration 15`，身份/build及夹具接线与进度038相同。报告`out/node-sequence/position-100hz-n20-20260917-r1.json`为FAIL，原始数据保留。
- 第一位置已采首项且收到READY，累计33时在WAIT_RETURN；随后切到第二项，累计40即到下一位置边界，首轮尚未完成。终态trigger/ready各一、accepted/completed各一，Core1 backend_fault14、LINK error5，符合忙时位置保护。
- 降低原始脉冲频率并等比例减小N没有改变200ms位置间隔；本次再次证明当前闭环未达到目标，不能把它归因为单纯1kHz输入过快。尚需定位RJ45通知运输和协调调度耗时，不改变整轮或忙时拒绝语义。
- 清理完成：序列IDLE，输出/owned/armed/busy均零、TDMA已停；500Hz尚未测试。

### NSEQ-PROGRESS-20260917-040 - 环节耗时评估与IO特等席范围

- TODO：`NSEQ-096/097/098/099`；日期：2026-09-17。用户要求分解每段耗时，参考real-flight TDMA特等席，并将部分IO控制提升到特等席。本轮为只读源码/已有报告评估和待办细化，尚未修改调度/IO固件、编译或OTA；设备沿用进度039停止状态。下列时间、次数和上游版本均为证据快照，非产品时限或物理波形事实源。
- 复核上游`origin/wip/tdma-real-flight-processing`、HEAD `6ba57876`的`tdma_priority_rx.h`、`tdma_priority_rx.c`、`tdma_pio_spi_phys_priority.inc`及`application/src/app.c`：有界接收保留、IRQ配额/截止窗口、关闭裕量、epoch撤销、阶段计时可借鉴；sink回调仅借用已验证记录，不授权一般状态机或硬件生命周期执行。RAM移植未带入这一接收路径；不把上游能力记作当前分支已具备。
- 当前`app_realtime_tdma_phase()`顺序为TDMA、analyzer、sequence，且整个phase可能因准入不足被跳过。序列PIO编码及延时已自主执行；优先提升的是CPU识别回执、递交后继意图与发布事实的等待，而不是用CPU重新产生波形。
- `sync_io_sequence_gateway_fire()`含SDK `dma_channel_abort()`等待；`drain_receipts()`上限为`RECEIPT_WORDS`，故障路径还会停止DMA。完整`trigger_sequence_service_service()`包含START装载/STOP回收，不能原样当作短步或搬入IRQ。独立只读审核`/root/sequence_priority_audit`确认先拆短步、固定回执预算、STOP抑制、异常锁存/安全输出/后续清理，再引入有预算的Core1窗口；具体实施和回归点写入TODO。
- 当前传输由`trigger_sequence_link_protocol.h`的逻辑消息拆成多个mailbox fragment；本轮快照每消息六片，一个位置为COUNTER_NEXT加各状态LINK_APPLIED/READY_NEXT，八项合计十七条完整消息。`distributed_refmem_tdma_flight_sync_publish()`受配置间隔和`feedback_timeout_ns`节流、TX双缓冲可用性影响，Core0接收还需完整重组。IO窗口本身不能消除这些等待，尚不能归因到单一根因。

| 环节 | 现有时间依据（本轮快照） | 能支持的结论 |
|---|---|---|
| 位置间隔目标 | 用户模拟5°/s、每1°一次，为200ms；八项摊分为25ms/项且须包含位置管理开销 | 为验收预算，不是现有测量结果；100Hz/N20仍失败 |
| 编码稳定延时 | 报告配置`settle_us=10`，每次实际切码由PIO执行 | 配置为10µs；尚无独立边沿时延证明，START已预置首项，不能机械按八次增加本位置耗时 |
| 网关触发脉冲 | 报告配置`gateway_pulse_us=1000`，每项一次 | 配置1ms/项；八项配置脉宽合计8ms，不代表实际VNA采样时间 |
| RJ45裸数据移位 | 报告`baud_hz=10000000`、`last_rx_size=100`；按字节数乘位数除速率 | 80µs仅为该字节数的理论移位时间；未含其他封装、间隔、回程及收发软件，不能当成RTT |
| 阈值到首项请求 | 50Hz/N1000成功报告首项计数1005/2002，相对阈值1000/2000 | 按声明源频率估算100ms/40ms；计数快照不是阈值或输出边沿锁存 |
| 相邻状态请求 | 同报告两位置的历史计数差，详见下段 | 请求到下一请求估算100–220ms，包含通信、调度、IO、夹具READY；各段尚不能独立拆出 |
| 位置完成观察 | 首位置第一次观察WAIT_COUNT为1062，第二位置终态为2066 | 阈值到软件完成观察约1240ms/1320ms；首项含主机轮询滞后，末项停止计数，非物理完成精度 |
| 真实VNA采样 | 当前OUT4→IN2是反馈夹具 | 未测量；后续接真实VNA还需加上其采样及反馈延时 |

- 时间来源为`out/node-sequence/position-n1000-lifecycle-20260917-r2.json`的`settings`、`two_positions.history/samples/ring_after`。第一位置请求计数为1005、1010、1017、1023、1029、1038、1046、1052；相邻请求估算100、140、120、120、180、160、120ms。第二位置为2002、2009、2017、2028、2037、2045、2052、2060；相邻估算140、160、220、180、160、140、160ms。频率50Hz为当时用户声明、不是当前100Hz或独立计量结果；量化粒度20ms，不能据此分解微秒级动作，也不能用各段最大值之和冒充WCET。
- 当前只能判断端到端等待远大于配置脉宽及裸数据移位时间，CPU调度/运输/重组链路是优先调查对象；不能声称已经测出每个软件模块的准确用时。下一gate为NSEQ-097板端同钟分段计时，再分切片接入IO与TDMA优先路径，使用新build重跑既定100Hz/N20及旧模式；频率未切换前不运行500Hz档，不提高N掩盖200ms目标。
- 验证：文档命名/内容检查、自回归检查及18项对应pytest通过（`out/pytest/sequence-priority-plan-20260917-r1`）；保留既有`TDMA-FLIGHT-BITMAP-01`命名WARN。项目约定的Git Bash绝对路径在本机不存在，改用PATH中的`C:/Program Files/Git/bin/sh.exe`执行pre-commit成功。未暂存源码，single-board `check-staged`明确返回无staged源码，P3 check与hook跳过硬件检查；不将此写为新固件硬件通过。独立审核再次核对原始时间记录，建议的STOP复查点和第三模式验收范围措辞已修正。本轮仅细化计划，不变更契约登记状态。

### NSEQ-PROGRESS-20260917-041 - Core1运行状态机迁移及动态PIO下一切片

- TODO：`NSEQ-100/101/104`；日期：2026-09-17。本节build、测试数量、频率和预算均为本次快照，非产品常量事实源。Core1唯一执行LINK状态机及历史写入，Core0保留分片桥；完整接收经固定容量SPSC队列、发送经不可变offer交接。STOP待办立即抑制重试/递交，BUSY保留原消息，旧run消息及溢出按身份隔离；完整角色表在Core0 START guard校验，运行时只核对原子model epoch。
- 软件：运行时/SCPI/RefMem/PIO/dispatch共298项通过，`out/node-sequence/core1-link-runtime-20260917-r1.log`；最终LINK与文档59项通过，`out/pytest/core1-link-final-tests-20260917-r1`。只读审核`/root/sequence_priority_audit`为PASS_WITH_NOTES，修复溢出快照acquire屏障；原有owner锁和生命周期服务仍在，不能称完整非阻塞。
- 构建`out/build/sequence-core1-link-20260917`、preset `pico2-usb-runtime-switch`、build `20260917123949`成功，A/B/BOOT flash-link通过。指纹`out/node-sequence/core1-link-artifact-hash-20260917-r1.json`；VISA OTA及槽位确认`core1-link-visa-ota-20260917-r1.log`、`core1-link-visa-commit-20260917-r1/summary.json`通过。map的Core1栈保留量不是动态水位测量。
- 当前build、UID `839E1AE79EA20F31`、USBTMC、真实RJ45、IN1声明100Hz、OUT4→IN2：`core1-link-position-baseline-20260917-r2.json`通过N1000两完整位置、忙时阈值故障、故障后恢复、连续首位置、暂停/恢复及STOP/restart；独立IN1十轮`core1-link-independent-20260917-r1.json`及GUI双角色软件READY十轮`core1-link-rj45-manual-20260917-r1.json`均通过。N1000仅为功能迁移基线，不代替200ms目标。
- 三槽位r1正常两位置及忙时保护通过，但生命周期工具将跨查询的LINK WAIT_COUNT与COUNTER WAIT_COUNTER_RETURN误拼成已完成位置而失败；保留`core1-link-position-baseline-20260917-r1.json`。固化工具改为同时要求两个phase均WAIT_COUNT，保留完整采样/历史检查；工具40项通过`out/pytest/core1-link-tool-20260917-r2`，r2重新运行通过。
- 同工具新增可选`--schedule-evidence`，保存`SYST:TDMA:SCHED?`原文、错误队列及所有phase计数，不提升严格时序结论。r2清理前TDMA phase预算850µs、最大观测约2.402ms且有超限，`strict_realtime_verified=false`；未与同等观测的旧build对比，不把所有超限归因于本次迁移，动态栈水位也未验证。
- 固定目标复测`core1-link-position-target-20260917-r1.json`在运输配置期间VISA timeout，并记录Execution error，完整失败及清理保留；独立重跑r2进入真实序列，累计40即第二位置边界时仍在首轮，COUNTER_BUSY保护停止，目标仍FAIL。原始证据`core1-link-position-target-20260917-r2.json`；未增加N掩盖目标，不改变忙时拒绝。
- 双角色连续模式软件READY的PAUSE/CONT独立补测`core1-link-rj45-pause-20260917-r1.json`通过；外部IN1 READY、声明100Hz十轮`core1-link-rj45-in1-20260917-r1.json`也通过。审核方只读复核身份、完整位置历史、完成谓词及调度原文，未发现功能闭环阻断；工具旧fallback标签“Core0 link guard”同步更正为Core1协调器，不改变判定。
- 上述单板报告均在`out/node-sequence/`，清理后序列/TDMA停止并释放输出。功能迁移切片闭合，严格TDMA稳定性、200ms目标、波形/真实VNA/射频及多板仍未验收。接着按TODO推进动态装载的PIO grant/ACK切片；不改PIO1/2，不将完整软件FSM放入IRQ。未暂存或提交，现有RefMem等白名单外改动不能由手工单板报告替代提交门禁。

### NSEQ-PROGRESS-20260917-042 - 动态PIO握手、常驻READY DMA与单板复验

- TODO：`NSEQ-101/105`；日期：2026-09-17。仅组合模式加载精简NONE executor及grant门控READY；编码、settle、脉宽和每grant一次边沿由PIO完成，FIRE只递交FIFO，不再abort DMA/重启SM。PAUSE/STOP仍在生命周期退役DMA并清旧grant/累计baseline；独立SP8T程序保留。指令数以实际汇编产物为准：本轮快照双角色含capture为26条、三槽位31条，均未使用PIO1/2。
- 软件94项PIO/驱动测试通过`out/pytest/n101-pio-r4`，包含双极性、初始有效电平、实际指令握手顺序、FIFO背压、每FIRE零abort、迟到DMA取消及热加载回滚；149项owner/LINK/SCPI/dispatch通过`out/node-sequence/pio-gateway-owner-tests-20260917-r1.log`。独立审核发现compact STOP在settle前一tick可能误记完成，已改用completed偏移并加取消/完成及精确settle逐周期测试，最终`PASS_WITH_NOTES`，软件时钟模型不替代波形。
- 构建`out/build/sequence-pio-gateway-20260917`，build `20260917130449`，USB运行时切换开启，A/B/BOOT链接检查通过。源码补丁/哈希、产物哈希和构建日志分别为`pio-gateway-worktree-20260917-r1.patch`、`pio-gateway-source-hashes-20260917-r1.json`、`pio-gateway-artifact-hash-20260917-r1.json`、`pio-gateway-build-20260917-r1.log`。VISA发送及槽位B确认通过`pio-gateway-visa-ota-20260917-r1.log`、`pio-gateway-visa-commit-20260917-r1/summary.json`。
- 用户本轮重新确认IN1为50Hz，OUT4→IN2、真实RJ45；`pio-gateway-position-baseline-20260917-r2.json`通过N1000两位置、16次触发/READY、15次切步、完整历史、忙边界、恢复、PAUSE/CONT及STOP/restart。双角色软件READY十轮`pio-gateway-rj45-manual-20260917-r2.json`、连续暂停恢复`pio-gateway-rj45-pause-20260917-r2.json`、独立IN1十轮`pio-gateway-independent-20260917-r1.json`和START七阶段`pio-gateway-start-status-20260917-r1.json`均通过。
- 失败原件保留：position r1仍按旧100Hz设35秒窗口，实际只累计1753且第一位置8次采样已完成，未到第二位置阈值而timeout；确认50Hz后按真实位置等待时间重跑，不把延长功能观察窗口作为200ms性能通过。manual r1因主控UID参数拼写错误，被身份预检拒绝，未写设备；pause r1在ROLE ACT被拒绝，独立重跑r2通过，拒绝原因继续核查，不能隐去。
- 所有上述报告/日志均在`out/node-sequence/`。TDMA调度仍有预算超限，位置闭环约秒级，严格实时与200ms目标未通过；下一迁移仍为运输/有界服务，当前先按用户最新要求完成三模式正式单板凭证和代码检查点。普通回执扫描及生命周期清理尚未全部拆成非阻塞短步，NSEQ-101保持IN PROGRESS。
- 补测外部IN1 READY十轮`pio-gateway-rj45-in1-20260917-r1.json`通过；布局、产包、角色SCPI、GUI、USB工具及NO5隔离192项回归通过`pio-gateway-integration-tests-20260917-r1.log`。角色ACT拒绝由独立审核定位为`REFMEM_TABLE_ACTIVATE_ERR_GATE`，staging为OWNER_OK且CRC完整，排除将其误记为PIO运行故障；报告缺少gate位图，快照暂不可用仅为候选，记`NSEQ-RISK-04`留下一代码提交关闭。
- 同build的固定200ms位置间隔复测（用户确认50Hz，N10）`pio-gateway-position-target-20260917-r1.json`仍以忙时位置故障失败，原件保留；PIO握手切片不被写成运输加速或性能目标完成。

### NSEQ-PROGRESS-20260917-043 - 三模式功能检查点及可复验提交门禁

- TODO：`NSEQ-105`；日期：2026-09-17。用户明确要求单板验收后提交一部分已实现代码，沿用仅单板功能、不做P3的任务口径。当前转台/PIO/Core1功能依赖RefMem布局及角色实现，不能拆成缺少依赖的固件提交；代码检查点集中保存这些必要实现及工具，文档另行提交，性能迁移尚未完成。
- 新增固化`tools/visa_ota_update/visa_ota_update.py`，依次执行包校验、实时UID/build预检、实际VISA发送并BOOT、按UID重连提交，记录真实子进程返回值/原始stdout和stderr/起止时间、包与工具SHA256及子摘要。不自动重试失败的OTA事务，不手工拼成功凭证；USB重枚举/槽位确认采用有界轮询。包头保留字段不称CRC，提交槽位必须为A/B。28项相关测试通过`out/pytest/visa-ota-update-r3`，独立审核通过。
- 已真实运行该流水线，以同一build `20260917130449`重刷当前包，`out/node-sequence/pio-gateway-visa-pipeline-20260917-r1/summary.json` PASS；这证明发送/BOOT/提交及最终build，不单靠同build文本宣称物理槽位翻转。设备仍为USBTMC。另同build两方向USB切换`pio-gateway-usb-to-cdc-20260917-r1.json`及`pio-gateway-usb-to-tmc-20260917-r1.json`通过，UID/build保持。
- 正式单板门禁升级为三模式固定profile并精确列入新增必要文件；不新增目录通配豁免，不改P3 scope。继续要求worktree与index全源码指纹一致，旧凭证不能放行；OTA包内容、子日志和各profile/验证器摘要均核对，RefMem布局/产包/SCPI回归保存JUnit且不准skip。位置profile使用显式源频率推算等待窗口，READY超时保持原配置，不借此扩大性能声明。`NSEQ-RISK-04`尚未关闭，下一代码提交优先补激活gate诊断并收敛偶发拒绝。
- 正式`sequence_single_board_gate.py run`通过：build `20260917130449`、UID `839E1AE79EA20F31`、USBTMC、IN1由用户确认50Hz、OUT4→IN2及真实RJ45。固定host回归183项全通过；双角色有限/暂停恢复、独立IN1十轮、START七阶段、N1000位置计数与忙时/生命周期五组profile全部PASS。完整命令及日志见`out/node-sequence/sequence-three-mode-gate-20260917-r1.log`，各JSON、host日志与JUnit在`out/HardwareAcceptance/20260917/sequence-three-mode-r1/`；以上数字为本次验收快照。
- 凭证`config/hardware_acceptance/sequence_single_board_receipt.json`使用`HAOFV_SEQUENCE_SINGLE_BOARD_RECEIPT_V2`，绑定源码指纹、包及真实OTA摘要。暂存凭证后`check-staged`、手动及提交时pre-commit、`git diff --cached --check`均通过；代码提交`a344e90a`（`feat(sequence): checkpoint three-mode single-board control [risk]`），本次文档另行提交。NSEQ-105完成，NSEQ-101继续IN PROGRESS、NSEQ-102至104仍待完成。
- 最终position报告`cleanup_failures`为空，序列IDLE、outputs/owned/armed/busy归零、TDMA停止且probe关闭，保留USBTMC便于继续调试。凭证仅为三模式单板功能，不证明真实VNA、RF、独立边沿计量、波形、多板、P3或严格TDMA稳定性；N1000功能等待不替代失败的200ms位置目标。
- 独立只读审核`/root/sequence_priority_audit`结论`PASS_WITH_NOTES`、无阻断：保留RISK-04、PIO未完成范围及性能失败，RefMem契约维持pending。审核建议的下一Gate旧措辞与OTA事务重试/USB有界轮询边界已修正。
- 文档检查`docs_check --strict-names`、`doc_regression_check`及`--log-check`通过；对应18项pytest通过，目录`out/pytest/sequence-checkpoint-docs-final-20260917-r1`。保留既有`TDMA-FLIGHT-BITMAP-01`命名WARN；本机约定Git Bash路径不存在，手动hook使用PATH中的`C:/Program Files/Git/bin/sh.exe`。

### NSEQ-PROGRESS-20260918-044 - 独立SP8T反馈捕获窗口及远程USBTMC包

- TODO：`NSEQ-106`；日期：2026-09-18。用户确认OUT4接网分触发输入、Trigger Out B接IN1，B为Sweep/End正脉冲；手动切步可见反馈，自动只见一次。当前未接板，源码确认存在启动捕获晚于首状态脉冲、脉冲期间拒绝反馈的窗口；尚未实测证明这就是现场唯一根因。
- 独立external+PULSE在START同步启动PIO输入捕获和DMA；复用executor两处指令，在编码建立完成后、状态OUT前开放一次接纳，IRQ请求锁存至当前脉冲结束再执行下一项。首项OSR同步预置，priming以真实首对回执退出，不再依赖可能已被反馈消耗的READY位。未增加PIO指令或改PIO1/2，MANUAL/LEVEL/NONE/组合网关及无推进配额保持原路径。
- 软件accepted仍表示执行准入，未消费反馈仅为候选；PAUSE排空已锁存的一步、阻止新接纳，STOP撤销未消费候选记notready。独立复核发现的accepted与notready重复记账风险已通过`pending_request()`分流修复并增加实际C边界测试，不把反馈伪造成已完成切步。
- 验证快照：最终PIO/驱动127项通过，含首次及连续早反馈、最短脉冲、双极性、重复沿、建立期噪声、有限配额、初始有效电平、迟反馈、暂停/停止和迟CPU回执；日志`out/node-sequence/feedback-window-final-tests-20260918-r1.log`，目录`out/pytest/feedback-window-final-20260918-r1`。上层owner/SCPI/LINK/GUI234项通过，日志`out/node-sequence/feedback-upper-tests-20260918-r1.log`。独立审核`/root/independent_feedback_review`为PASS_WITH_NOTES，无阻断，额外初始电平/迟反馈测试由主控补齐；软件模型不等于物理边沿验证。
- 以当前HEAD `efe5ca78`加工作区修复编译，preset `pico2-usb-runtime-switch`并覆盖`PROJECT_USB_DEFAULT_MODE=USBTMC`。build `20260918074511`，A/B/BOOT flash-link通过；日志`out/node-sequence/feedback-usbtmc-configure-20260918-r1.log`、`feedback-usbtmc-build-20260918-r1.log`及最终增量确认r2。构建目录`out/build/sequence-feedback-usbtmc-20260918/`，包`DHRT100_UPDATE.pkg`；已保存的板端CDC选择仍优先于固件默认值。
- 远程交付目录`out/remote/sequence-feedback-usbtmc-20260918/`包含带版本名的OTA包与README。包大小1554708字节，SHA256 `1f2405bf472256062775cb74fbe12a5f8beadecb901887f785976f3157f938c9`，均为本次产物快照。审核C/PIO的git对象指纹分别为`55a8141eb8db8de4558ca3e8d20e82b637cb0f19`、`b52507d38e3a9d1559d310cc3d6ea2598c30217e`。
- 未本地OTA、未生成新硬件凭证、未提交。下一gate是远程核对build/USB枚举，并同时观察OUT4和IN1，验证首反馈、连续各项、有限轮次、PAUSE/CONT、STOP及重启。网分反馈须重新回到非有效电平再产生下一边沿；B脉宽若使相邻完成信号合并，仍需在仪表侧校准。独立有限末项仍按既有输出动作完成后结束，不新增等待最后网分READY的语义。
- 文档检查和自回归通过，对应18项pytest通过（`out/pytest/feedback-docs-20260918-r1`）；保留既有登记命名WARN。本次`--log-check`还提示最近HEAD晚于已有hook记录，仅记录该时间差，不据此判定历史提交绕过门禁。本次源码未暂存，手动pre-commit跳过硬件分支，不作为新硬件通过证据。交付压缩包为`out/remote/DHRT100_20260918074511_USBTMC_SP8T_FEEDBACK.zip`，内含上述包和远程操作说明。

### NSEQ-PROGRESS-20260918-045 - 调试 GUI Windows 便携包

- TODO：`NSEQ-107`；日期：2026-09-18。使用 `D:\Microsoft\Miniconda\envs\py2exe\python.exe`（Python 3.11.15）构建 PyInstaller one-directory 包；新增 `frozen_entry.py` 作为 GUI/控制台助手入口，允许列表方式调度 VISA/Serial OTA 工具，避免冻结后把 GUI 自身重复启动。`sequence_debug.spec` 收集 Tk、PyVISA、PyVISA-py、PySerial、PyUSB、libusb 及 Conda Tcl/Tk/ffi 压缩库依赖，并内置 `5711_-_Sync_Event.png` 图标。
- 构建工具 `tools/sequence_trigger_debug_ui/build_windows.py` 已固化构建、自检、README 和 ZIP 生成流程，并可从源码目录外调用。最终包目录为 `out/package/sequence-gui-20260918-r5/dist/DHRT100_Sequence_Debug/`，压缩包为 `out/package/sequence-gui-20260918-r5/DHRT100_Sequence_Debug.zip`；分发必须保留 `_internal`、两个 exe 和 README。自检在无硬件环境创建真实 Tk 窗口并验证 5 个分页、Tk 8.6、串口/VISA/libusb 版本，结果见 `self-test.json`。
- 无硬件软件验证：8 个冻结 OTA 工具的 `--help` 均返回 0；VISA OTA 使用现有 `.pkg` 的 `--dry-run` 返回 0 并输出包大小/CRC；Serial dry-run 在虚构 COM 端口下按预期报告设备数不匹配。GUI 源码回归 `102 passed`；未执行本地 OTA、USBTMC 枚举、真实设备通信或硬件验收，不将便携包自检记为 NSEQ-106/NSEQ-107 的设备验收。

### NSEQ-PROGRESS-20260918-046 - 转台脉冲计数模式现场循环采样确认

- 日期：2026-09-18；本节数值均为现场读回或仿真快照，非产品常量。用户授权“可以依据我的证词进行提交”，并最终澄清“当时验证的是转台脉冲计数模式”。因此“网分也可以跑了”归属COUNTER/DUT/VNA三槽位模式，不归属独立SP8T反馈。现场对话：麻彦广18:03:29“在循环采样”；董力18:03:41“可以可以”、18:03:55“说明之前采样太快了，看不到”、18:04:11“现在降低速度，就能看到了”、18:04:45“终于啊，单板功能差不多了”。这是用户提供的现场证词，不是工具报告。
- 用户提供读回原文：17:28:14 `SYST:FW:BUILD?` 返回 `"20260918074511"`；17:28:25 `TRIG:SEQ:NEXT?` 返回 `"IDLE",15,15,8,7,7,0,7,7,7,7,9,79,79,1,0,0,0,"NONE",0,0,0,0,0`；17:28:35 `READ:IO:STAT?` 返回 `0,0,0,0,0`。可确认79次推进完成、busy_rejected为1、故障为0；这些字段没有LINK模式及COUNTER历史，不能据此识别独立模式、位置数或独立物理脉冲计数。此前按独立模式解释为八项十轮的推断不作为模式验收依据。
- 降速后转台脉冲计数模式循环采样由用户确认有效；实际最终阈值N、源频率、延时、脉宽、型号和波形未提供。此前建议100000us建立延时、10000us脉宽仅为独立模式排查建议，不冒充转台实际配置。“网分未重新武装”仍是未证实假设，不作为根因结论。
- 增加真实PIO指令模型直接OUT4→IN1十轮及10/100us反馈时序回归。软件模型中10us建立延时/10us输出脉宽的80脉冲约1.66ms结束；模型不代表独立波形测量。保留进度044固件C/PIO指纹和构建包不变，不加入试验性诊断SCPI。
- 本次受限代码提交仅包含反馈固件和相关测试；GUI打包代码仍在工作区，进度045记录的是工作区产物。代码与本文分开提交，提交消息标注`[risk]`。依据用户当前明确指令，一次性覆盖本次Git硬件hook要求；仓库`.githooks`及持久`core.hooksPath`保持原样，未改验收器、未制作或替换硬件凭证。自动门禁拒绝原文与测试日志保存在`out/node-sequence/feedback-testimony-20260918/`；不能记为自动验收通过。
- NSEQ-106保持IN PROGRESS，转台证词不能替代独立反馈测试；NSEQ-RISK-05保留独立反馈自动验收、OTA摘要、现场参数与生命周期补证，要求下一代码提交解决。既有NSEQ-RISK-04保持未解决，本次反馈修复按用户既有允许未验证功能标risk的受限提交授权保存，不冒充已由现场验证覆盖。转台位置阈值、每位置完整采样记录和200ms目标尚未由本次证词单独验证；未扩大到P3、多板、RF或速度上限验收。
- 提交前软件快照：PIO/驱动、SCPI与LINK共287项通过，文档检查器18项测试通过；`docs_check --strict-names`、`doc_regression_check`通过，保留既有登记命名WARN。C/PIO源码指纹仍与进度044一致。单板`check-staged`明确拒绝“working source differs from the staged commit”，原因包括未纳入本次提交的GUI工作区改动；既有凭证也不作为本次通过证据。用户现场授权的受限提交不消除此拒绝事实。
- 代码检查点：`c862bf92 fix(sequence): arm feedback before status output [risk]`，仅包含固件C/PIO与两份测试；文档另行正常经过hook提交。未推送远端。

### NSEQ-PROGRESS-20260919-047 - 三模式回环工具与独立SP8T启动观察

- TODO：`NSEQ-106`、`NSEQ-108`；日期：2026-09-19。本节数字是本次实测或工具默认值快照，非产品事实源。GUI三模式各增加回环工具窗口，提供填入预设、配置、启动并观察和停止；预设填入不下发，观察不发送NEXT，不修改既有参数。沿用现有SCPI和owner边界，无固件或PIO修改。
- 预设接线：独立模式OUT4到IN1；双槽位RJ45物理回环加OUT4到READY IN1；转台模式外部计数源到IN1，OUT4到READY IN2，RJ45物理回环保留。接真实网分时OUT4接网分触发输入，网分反馈OUT接READY输入；计数源独立提供。转台预设保留用户阈值和槽位分配，达到一个位置后执行整个SP8T序列。
- GUI默认预设为八项、十轮或十位置、建立延时10000us、脉宽100us；以`SequenceUi.apply_loopback_preset`为代码事实源。观察时长可调，转台默认延长以覆盖低频累计阈值；观察结果包含计数、轮次、错误和忙拒绝，转台另读累计脉冲/位置/阈值。停止绑定测试启动时设备，每次启动阶段交换前检查取消。日志写入`out/sequence-loopback/`，观察结束不额外STOP，运行中明确提示设备仍运行。
- 初次设备枚举为空；重新连接后USBTMC身份为`839E1AE79EA20F31`，build为`20260918074511`。第一次读取为MANUAL、输出配置未有效，查询不存在的SP8T计划超时，未发送START；失败原文保留在`out/node-sequence/independent-loopback-20260919/start-observe-r1.json`，工具随后增加有效外部反馈配置前置检查。
- 再次检查时设备已有有效配置（本工具未配置）：`IN1,RISING`、输出`7,8,PULSE,10,10,4,1`、重复`100,100,1`、LINK disabled。`sequence_start_observe.py`仅发一次START，run由14进入15，八项百轮完成799次推进，最终IDLE，accepted=completed=799，cycles=99，busy_rejected=1，faults/backend_fault=0，error=NONE，IO全部归零，SCPI无错误。START首项不计入推进，因此799与八项百轮一致；末项回环沿未被接纳，不据此声明物理边沿零丢失。原始记录`out/node-sequence/independent-loopback-20260919/start-observe-r2.json`。
- 使用工具`--gui-observer`调用GUI同一`observe_loopback`函数，第二次独立启动得到相同完成计数及IDLE，证据为`out/node-sequence/independent-loopback-20260919/gui-observer-r3.json`。两次均出现PyVISA结束字符提示，实际SCPI数据完整且解析成功，保留提示，不以其替代通信稳定性验收。只证明本板独立线缆反馈启动闭环与有限轮次停止；未覆盖真实网分、暂停恢复、其他输入及新GUI双槽位/转台实测，也未生成P3或当前staged指纹验收凭证。
- 软件回归和真实Tk布局测试覆盖模式隔离、无反馈、故障、取消及切换资源后停止；独立复核指出的取消时序和停止资源问题已修复，普通停止与回环窗口专用停止分别绑定当前设备与测试设备，同板不同模式的停止也取消当前观察。最终GUI与文档回归共133项通过，目录`out/pytest/gui-loopback-20260919-r6`；便携GUI为`out/package/sequence-gui-20260919-loopback-r3/DHRT100_Sequence_Debug.zip`，两个EXE自检通过。本次未执行OTA，打包后的硬件通信仍待验证；源码GUI观察函数实测不等同于EXE实测。GUI打包旧改动与本次新改动均仍待独立提交，未扩大现场证词范围。
- 文档检查通过，保留既有登记命名WARN；本机约定的D盘Git Bash路径不存在，使用PATH中的`C:\Program Files\Git\bin\sh.exe`执行pre-commit。hook及P3 `check-staged`拒绝当前暂存状态：GUI与布局测试工作树不同于旧暂存版本，且打包源码超出单板白名单；未绕过门禁，未生成或复用凭证，本次未提交推送。

### NSEQ-PROGRESS-20260919-048 - 第二模式RJ45双槽位物理反馈验证

- TODO：`NSEQ-108`；日期：2026-09-19。使用已有`tools/hardware_acceptance/sequence_tdma_cycle_validate.py --gui-control`调用实际GUI配置/启动命令构建器及执行器；USBTMC设备`839E1AE79EA20F31`、build `20260918074511`。本节数字均为测试快照，非产品事实源；本次未修改固件或验收工具。
- 接线为RJ45物理返回及OUT4到IN1，后者模拟VNA触发后的READY；DUT/VNA槽位为2/3，OUT1到OUT3输出SP8T编码，独立状态输出为NONE。配置八项序列、建立延时10us、网关脉宽1000us、READY超时5000ms；输入为IN1上升沿，未启用`--scpi-next`，无软件READY或NEXT注入。
- 首次一轮功能完成：trigger=ready=8、completed=7，序列IDLE、LINK phase=8、无序列或LINK故障，IO归零。然而后续`SYSTem:REFMEM:SYNC:TDMA:STATus?`诊断读取超时，之后RING STOP返回OK、错误队列返回`-200,Execution error`，报告整体FAIL。诊断查询与STOP之间没有读取错误队列，因此不能把该错误确定归因于STOP；源码中TDMA快照不可得时查询可返回SCPI_RES_ERR，实际原因仍未闭合。保留`out/node-sequence/rj45-loopback-20260919/gui-one-round-r1.json`，不将其改写为通过。
- 随后十轮运行PASS：run=18、generation=6，trigger=ready=80，accepted=completed=79，busy_rejected=0、faults/backend_fault=0，LINK rejected/error=0，重复读回`10,10,1`。全部状态完成后序列IDLE、LINK完成；STOP后静默期计数稳定、IO归零，TDMA STOP返回OK且错误队列为0，readback确认owner停止。该次清理无失败，证据`out/node-sequence/rj45-loopback-20260919/gui-ten-rounds-r2.json`。
- 物理TDMA发送/接收和LINK返回消息有记录，双槽位主线及有限轮次退出已确认；本次未接真实网分，不代表采样质量、RF、多板或TDMA稳定性验收。GUI命令构建/执行路径已实测，新回环窗口的观察函数及打包EXE仍需独立验证；首轮诊断快照失败不由重复通过关闭。未提交推送，既有staged凭证缺口仍保留。

### NSEQ-PROGRESS-20260919-049 - 第三模式外部计数与每秒位置验证

- TODO：`NSEQ-108`；日期：2026-09-19。用户确认50Hz脉冲源到IN1、OUT4到IN2，并指定后续提高频率仍保持约每秒一个位置。按`N = 输入频率 × 位置周期`通过SCPI配置阈值；本次N=50、两个位置，每位置完整八项SP8T序列，RJ45物理返回保持。网关脉宽1000us，编码建立10us，READY超时10000ms，设备和build同进度048；数字为本次快照。
- `sequence_position_validate.py`新增`--external-input`，复用真实GUI POSITION配置/启动构建器，禁止与注入式lifecycle组合。无IN1、READY或NEXT软件注入；检查阈值前无采样、两位置完整结果、历史恰好16条、各记录阈值/观测计数/状态编码/触发READY序号/完成标志、运行标识及RJ45收发增长，最后检查owner完成和输出释放。独立只读审核补充的TDMA增长和额外历史记录拒绝已落实；工具53项测试通过，目录`out/pytest/position-external-20260919-r4`。
- 原N=1000测试按用户要求中止，进程exit=1，`out/node-sequence/position-external-20260919/two-positions-r1.json`为空，不能作为任何通过证据；下一轮工具先STOP并重新配置。N=50两次运行通过，最终证据`out/node-sequence/position-external-20260919/two-positions-n50-r3.json`：两个位置、16次触发、16次READY、15次推进、16条历史；阈值分别为50和100，采样期间计数继续，最终计数104。序列/LINK/COUNTER无错误，清理无失败，序列及TDMA停止，IO归零；频率为用户声明，未做独立源边沿计量。
- 主机SCPI时间快照：首次位置由4921.156秒的45脉冲/0位置变到4921.265秒的50脉冲/1位置；第二次由4922.234秒的98脉冲/1位置变到4922.328秒的103脉冲/2位置，与约1秒一个位置相符。固件历史`position_admitted_tick_ms`却相差1669，明显与主机尺度不同；`cycle_elapsed_ms`为138/137仅能作为固件软件时间快照，不能解释为真实毫秒。此次`--position-cycle-target-ms 1000`是功能测试上限，不证明实际1秒预算或原200ms性能目标；时间尺度偏差需后续独立校准。
- 结论为第三模式外部脉冲计数、整序列采样回环及有限位置停止通过；不宣称真实网分测量、多板、RF、严格TDMA稳定性或打包EXE验收。没有编译/OTA/固件改动，没有提交推送或新硬件门禁凭证。

### NSEQ-PROGRESS-20260919-050 - 转台输入指令复核与GUI频率周期换算

- TODO：`NSEQ-109`；日期：2026-09-19。用户要求GUI填写脉冲频率或周期，同时保持每秒一个位置。转台页增加“频率/周期…”窗口，接受Hz或ms，通过`calculate_position_threshold`换算N；固定目标一秒，整数四舍五入并显示实际预计间隔。仅填入阈值草稿，须重新配置后启动，不控制信号源，不自动检测实际频率，不改变固件/PIO。
- 回顾SCPI原指令表P5：`CONFigure:ANGLe:SWEEp start,stop,step`描述扫描角度范围，`CONFigure:ANGLe:PULSe edge,pulse_width_us,timeout_ms`描述目标角度触发输入；脉宽不等于周期。原业务是一角度事件执行完整序列，目前原始脉冲模式是累计N个脉冲形成一个位置事件。源码`scpi_config_commands.h`将ANGLE写入映射到accepted占位回调，`scpi_config_commands.c`对应查询仍返回固定值，不能称为真实配置或实际角度读取。
- 真实已实测的输入配置为`CONF:SEQ:LINK POSITION,counter_slot,dut_slot,vna_slot,counter_input,N,ready_input,trigger_output,pulse_us,timeout_ms,edge`；后两个时间参数属于网关输出与READY等待，不能当作转台输入周期。HTML指令表新增实现状态说明、POSITION、COUNTER和完整HISTORY接口描述；不修改旧ANGLE语法、不伪称已实现。后续ANGLE接入需统一原始计数到业务角度事件的映射和owner，另列NSEQ-109跟踪。
- GUI回归快照130项通过，目录`out/pytest/gui-rate-20260919-r1`；涵盖50Hz/20ms、较高频率、非法值、取整提示以及填入后必须重新配置。便携包`out/package/sequence-gui-20260919-rate-r1/DHRT100_Sequence_Debug.zip`，两个EXE自检通过；未对新频率窗口执行硬件通信或OTA，既有第三模式N=50实测证据仍见进度049。

### 051：ANGLE四参数、真实绑定与单板闭环

- TODO：`NSEQ-109`；日期：2026-09-19。以下build、测试数量和运行计数均为本次快照，非事实源；未提交。
- 主配置为`CONF:ANGLE:SWEEP start,stop,step,speed`，速度单位°/s；`CONF:ANGLE:INPUT INx,pulses_per_degree`提供输入标定，SPEED允许停止态单独更新声明速度。SWEEP/INPUT/POSITION读取真实值；未实现的PULSE及BREAKPOINT明确报错。同步HTML、Markdown指令表与SCPI基础命令文档，字段事实源为`scpi_config_angle_*`回调。
- 配置阶段将角度网格导出为整数N和有限位置数，通过既有service配置锁和link锁原子生效；Core1运行FSM、PIO0及RJ45流程不变，无实时浮点计算。校验反向网格、整数脉冲、累计计数和状态总量溢出，节点模型或LINK绑定变化使angle bound失效。首个N脉冲对应开始角度，每位置整轮采样；速度不驱动机械运动。
- GUI第三模式新增可选角度扫描，四项主参数和独立输入标定，实时显示位置数、N、输入/位置频率及周期；编辑使旧配置失效。下发必须核验ANGLE ACK和SWEEP/INPUT读回，拒绝旧占位值、bound为假及参数不一致；仍保留原始计数模式。
- 独立审核发现并修复两项：ANGLE引用缓存model epoch可能虚报绑定；GUI原先未验证ANGLE ACK/读回。固件回归255通过（`out/pytest/angle-firmware-r6`），GUI/回环178通过（`out/pytest/angle-gui-binding-final`）；后续JSON功能另补测试。
- USBTMC及运行时USB切换构建通过，目录`out/build/sequence-angle-usbtmc-20260919`，最终增量日志`out/node-sequence/angle-usbtmc-build-20260919-r3.log`，A/B/BOOT flash-link检查通过。build `20260919033909`通过VISA实际OTA、重枚举及COMMIT，摘要`out/ota/sequence-angle-20260919-r1/summary.json`。
- `sequence_position_validate.py --external-input --angle-scan --threshold 50 --source-hz 50`在UID `839E1AE79EA20F31`上通过。IN1外部源、OUT4→IN2、RJ45物理回环；角度0至1、步长1、速度1、标定50，N=50且位置数2。读回均bound有效；首位置前current_valid为假，最终角度1、current有效/next无效，16次采样/READY、15次推进、16条历史、累计104脉冲。证据`out/node-sequence/angle-external-20260919/two-positions-r1.json`，无软件脉冲注入，结束释放IO及停止环路。
- 同新build双角色RJ45十轮回归通过：`out/node-sequence/angle-external-20260919/rj45-regression-r1.json`。仅功能验证；声明源频率不等于独立边沿测量，历史软件时间尺度偏差仍保留，不从软件毫秒推断波形性能。
- 文档检查及18项自回归通过。提交门禁仍因先前打包文件超出单板白名单、index/worktree源码不同而失败，未生成或声称新staged硬件凭证，未绕过门禁。

### 052：GUI本地JSON参数恢复

- TODO：`NSEQ-109`；日期：2026-09-19。使用`tools/sequence_trigger_debug_ui/settings.py`保存版本化JSON，路径为`%LOCALAPPDATA%/DHRT100/sequence_trigger_debug_ui.json`；无该环境变量时使用用户目录`.config/DHRT100`。
- 启动恢复三模式参数草稿、角度/速度及输入标定、OUT分配、连接选择、分页和观察时长，关闭前写入临时文件并原子替换。仅恢复白名单字段，不恢复设备运行态、配置成功标记、IO读数或待发指令；恢复不下发设备命令。文件损坏/版本不支持时提示并使用默认值，无效字段忽略；保存失败弹窗提示，旧有效文件保留。
- 实际Tk窗口关闭/重开、错误文件/字段、保存失败、OTA阻止退出、历史三模式及ANGLE行为共194项通过：`out/pytest/gui-settings-full-r1`。打包自测显式禁用配置持久化，避免测试覆盖用户配置。新增保存失败弹窗后Tk回归41项通过：`out/pytest/gui-settings-close-20260919-r1`。
- 便携包`out/package/sequence-gui-20260919-angle-settings-r1/DHRT100_Sequence_Debug.zip`构建通过，GUI与工具EXE自检均通过（`gui-self-test.json`、`self-test.json`）。包内README注明本地JSON路径及草稿恢复行为。新EXE未执行实板OTA，固件及源码GUI命令路径实测证据仍见进度051。
- 独立只读复核`/root/angle_review`通过：缓存模型绑定和GUI读回校验两项问题均关闭，JSON草稿白名单、无自动下发及原子保存边界确认，无新增阻塞发现。复核采用主控的测试/硬件证据，未重复实测。

### 053：500Hz第三模式六十位置验证

- TODO：`NSEQ-109/108`；日期：2026-09-19。以下配置和计数均为本次单板快照，非事实源；未改固件、未重新OTA、未提交。
- 用户将外部源改为500Hz，保持IN1输入、OUT4→IN2、RJ45物理回环。两位置首次运行完成16次触发/READY及全部历史，但结束诊断`SYSTem:REFMEM:SYNC:TDMA:STATus?`超时；随后STOP ACK为OK而错误队列读到-200，不能归因于STOP。失败原件`out/node-sequence/angle-external-20260919/third-mode-500hz-r1.json`保留。相同参数r2通过，不能据重跑关闭诊断快照偶发失败。
- 按用户要求扩展既有`sequence_position_validate.py --positions`，有限位置数量同步到SWEEP及repeat。运行中持续拉取已完成历史，在板端环形记录覆盖前保存；检查序号/运行身份/完成标志，丢失时明确失败。容量引用`TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY`的工具快照，不增固件RAM。相关回归67项通过：`out/pytest/position-sixty-20260919-r3`，含超过环形容量、记录覆盖拒绝、未完成记录等待、整数溢出。
- 实测命令参数：`--external-input --angle-scan --positions 60 --threshold 500 --source-hz 500 --duration 80 --poll 0.05 --position-cycle-target-ms 1000`；实际UID `839E1AE79EA20F31`、build `20260919033909`。角度0至59、步长1、速度1°/s，模拟标定500脉冲/度；声明500Hz对应每秒一个位置，不代表真实转台标定。
- 六十位置通过：480次触发/READY、479次后继推进、60整轮、480条完整历史全部校验。首阈值500，末阈值30000，最终累计30043脉冲（末轮采样期间继续计数）；末角度59且next无效。故障/拒绝均为零，末项自动IDLE，清理后OUT和占用均为零，物理TDMA收发计数增长、清理无异常。证据`out/node-sequence/angle-external-20260919/third-mode-500hz-sixty-r1.json`。
- START至末角度读回主机时间约60.391秒，仅为本次主机观察；固件软件tick比例偏差仍未关闭，不作为准确硬件时序、独立边沿计数、真实网分/RF或P3验收。

### 054：1kHz第三模式六十位置验证

- TODO：`NSEQ-109/108`；日期：2026-09-19。以下数字为本次单板快照，非事实源。用户将输入源调整为1kHz；沿用IN1、OUT4→IN2和RJ45物理回环，仅SCPI改为N=1000，保持每秒一个位置、六十位置。未改代码/固件、未重新编译或OTA。
- 既有工具参数`--external-input --angle-scan --positions 60 --threshold 1000 --source-hz 1000 --duration 80 --poll 0.05 --position-cycle-target-ms 1000`；UID `839E1AE79EA20F31`、build `20260919033909`。角度0至59、步长1、速度1°/s，本次输入标定1000脉冲/度；由外部信号源模拟转台脉冲。
- 首次运行通过：60个位置各8状态，共480次触发、480次READY、479次后继推进；480条历史完整收集校验，终点角度59、next无效。末位置阈值60000，最终累计60081脉冲，余量来自末轮采样期间继续计数；故障/拒绝均零。自然完成到IDLE，清理OUT/占用为零，TDMA诊断及停止无异常。证据`out/node-sequence/angle-external-20260919/third-mode-1khz-sixty-r1.json`。
- 主机START至末角度读回约60.391秒；仅功能及主机观察，不是精确硬件时序或独立边沿计数验收。既有500Hz诊断快照偶发超时及固件软件tick尺度问题未因本次通过而关闭。

### 055：1kHz第三模式三百六十位置完整运行

- TODO：`NSEQ-109/108`；日期：2026-09-19。以下为本次单板实测快照，非事实源。用户要求完整运行360位置；沿用IN1外部1kHz源、OUT4→IN2模拟网分READY、RJ45物理回环，N=1000，每秒一个位置。角度0至359、步长1、速度1°/s、标定1000脉冲/度；只通过SCPI配置，未改固件或重新OTA。
- 既有工具参数`--external-input --angle-scan --positions 360 --threshold 1000 --source-hz 1000 --duration 400 --poll 0.05 --position-cycle-target-ms 1000`。设备UID `839E1AE79EA20F31`、build `20260919033909`，首次运行通过；证据`out/node-sequence/angle-external-20260919/third-mode-1khz-360-r1.json`。
- 全部360个位置各执行SP8T整轮8状态：2880次触发、2880次READY、2879次后继推进；持续拉取保存的2880条历史全部通过身份、位置/状态/编码、阈值/请求累计脉冲、采样完成和连续序号核验。板端保留末32条，主机无历史覆盖丢失。首阈值1000，末阈值360000；最终累计360085脉冲，余量为末轮采样期间计数。
- 终点角度359、next无效；末项自然进入IDLE，故障/忙拒绝/未就绪拒绝均零。真实TDMA收发增长；结束诊断及停止清理通过，OUT、占用、armed、busy均零。START至末角度读回主机观察约360.344秒。
- 本次是单板模拟网分反馈的完整有限扫描，不扩展为真实网分/RF、多板、精确硬件时序或独立边沿计数通过；既有诊断快照偶发超时和固件tick尺度风险继续保留。暂停、忙时故障和异常恢复未在本次正常扫描中注入，不能据本次结果新增这些生命周期验收结论。

### 056：360位置分段运行时间分析

- TODO：`NSEQ-097/101/103/109`；日期：2026-09-19。以下统计来自进度055原始报告，是离线估算快照，不是物理时序事实源。分析工具为`tools/hardware_acceptance/sequence_timing_analyze.py`，报告`out/node-sequence/angle-external-20260919/timing-1khz-360-r1.json`，源报告SHA-256已写入分析结果。
- 360个位置、2880条历史的阈值间隔原始软件tick均值1666.657，按声明输入频率1000Hz和N=1000校准后位置周期均值1000.000ms，p95约1001.406ms；主机START至末角度读回约360.344s，轮廓回归得到输入计数约1000.035Hz。实际输入频率未用独立计数器测量。
- 从位置阈值接纳到该位置最后状态完成，原始`cycle_elapsed_ms`均值137.894，p50=139，p95=145；按上述位置周期比例估算约82.737ms（p50=83.400，p95=87.000）。因此每个位置约917.266ms（p50=916.805，p95=922.205）处于等待下一位置；等待不表示状态机阻塞，原始脉冲仍继续累计。
- 状态分段估算（状态0至7）均值约10.512、10.580、10.582、10.473、10.358、10.175、10.007、10.050ms；各段p95约11.4–12.0ms。状态0包含位置接纳/首项路径，不能直接与后续七段等同；历史时间是接纳到READY完成的累计值，相邻状态相减才得到这些段间隔。
- 脉冲计数显示每位置首项相对阈值的接纳延迟均值约0.561ms（脉冲粒度估算），末状态请求相对阈值约73.333ms，说明采样期间脉冲继续累计；这不是额外等待，也不是位置提前触发。最后位置余量仍在采样期间累计。
- 读取到的最近消息分段样本（非2880条全量，受状态查询采样限制）按同一倍率估算：offer均值1.815ms/p95=2.400，return均值3.568ms/p95=4.800，inbox均值0.435ms/p95=0.600，message_total均值5.818ms/p95=7.200；四项相加无不一致样本。return包含TDMA调度、分片、回环和重组，不能命名为RJ45线缆传播时间。
- 已定位软件tick偏快约5/3的源码原因：FreeRTOS RP2350端口在`vPortSetupTimerInterrupt`按启动时`clk_sys`配置Core0 SysTick；调度器启动后`board_init()`才把150MHz提高到`BOARD_SYS_CLOCK_HZ`的250MHz，未重装SysTick。`osal_tick_ms()`随后直接使用`xTaskGetTickCount()*portTICK_PERIOD_MS`。因此原始“ms”乘约0.6才是当前源码倍率下的估算，不代表已修复；PIO/真实硬件时钟不应盲目套用该倍率。该结论由独立只读时钟审计确认，未修改固件。
- 当前记录没有独立的编码写出、settle完成、OUT边沿、网分触发边沿或READY边沿硬件时间戳；配置的settle和网关脉宽不能当作实测段时长。跨Core1的LINK字段均在同一Core1服务路径使用共享RTOS tick，但仍受上述SysTick倍率问题影响。
- 分析工具回归5项通过：`out/pytest/timing-analysis-20260919-r1`。该统计不关闭NSEQ-097/101/103的时钟修复、PIO预算、物理波形和严格TDMA门禁。

### 057：提交范围与剩余门禁

- 日期：2026-09-19。用户要求先提交推送。本次文档保存进度047至056的工作区实现、实测与分析记录；ANGLE、GUI配置保存、打包及验证工具的代码尚未提交，不能将本文的功能记录当作远端已有实现。
- 新增SCPI头文件改动、GUI配置模块、打包文件及测试等超出当前`sequence_single_board_gate.py`逐文件白名单；按仓库现行规则需要P3凭证。现有单板接线不满足四板P3，已完成的单板报告不能直接替代staged源码指纹凭证。代码提交等待明确验收范围和对应真实门禁结果，不修改或复用旧凭证。
- 保留TDMA诊断查询偶发超时、SysTick时间尺度偏差及打包EXE硬件验证缺口。消息分段统计为665个去重快照（本次分析快照，非事实源）；历史采样完成边界是网关READY且owner退出busy，早于后继READY_NEXT推进，不把返回消息耗时归入该完成边界。

### 058：TIMER1硬件时基迁移与第三模式重验准备

- TODO：`NSEQ-110/097`；日期：2026-09-19。用户要求禁止序列运行使用软件tick，对照`origin/wip/tdma-real-flight-processing`的`48ac0c69`及`750ff810`，仅迁移本次序列所需的硬件周期释放与checked TIMER1读取，不合并整条分支。
- Core0初始化TIMER1后以release/acquire发布就绪；checked接口检查初始化、频率、source、pause及high/low/high一致性，失败保留输出，不惰性初始化、不回退到软件tick。Core1继续使用硬件cycle计数控制phase/WCET，周期入口改为硬件绝对时间`busy_wait_until()`。
- LINK运行超时、位置接纳/完成、消息offer/return/inbox改为64位TIMER1原始计数，先相减再换算毫秒；SCPI字段顺序保持，绝对毫秒字段按原无符号宽度回绕，时差字段饱和。时钟不可用或计数回退进入`LINK_CLOCK`，STOP优先于时钟检查，恢复后允许新run。历史字段代表Core1观察边界，不是物理IO边沿锁存。
- RefMem flight发布间隔也改用同一TIMER1，避免实际序列发车仍受RTOS tick控制；保留FIFO背压、单owner和已有发布间隔配置。时钟不可用时拒绝发布并报告该路径的错误，纳秒反馈间隔向上换算先提升宽度以避免溢出。全局Core0 RTOS tick初始化偏差未在本次修改，不能声明全系统时钟均已修复。
- 软件验证：LINK/SCPI/ANGLE/位置工具/TIMER1/Core1释放组合回归302项通过，14项为device/host互斥用例跳过；新增flight发布回归8项通过。后续工具、LINK、flight及既有时钟回归147项通过，目录`out/pytest/sequence-timer1-final-r1`。覆盖精确超时边界、低位回绕、时钟无效/回退、启动拒绝、故障STOP/新run、发布背压及取整界限。
- USBTMC默认且运行时切换启用，build `20260919052648`；A/B/BOOT链接检查通过，日志`out/node-sequence/timer1-usbtmc-build-20260919-r2.log`。VISA OTA、重枚举、build核对及COMMIT通过，原件`out/ota/sequence-timer1-20260919-r1/summary.json`。源码工作区差异留于`out/node-sequence/timer1-firmware-20260919.patch`，不是staged提交凭证。
- 用户重新确认1kHz。IN1外部源、OUT4到IN2、RJ45物理回环，两位置预检通过，记录`out/node-sequence/timer1-external-20260919/third-mode-1khz-smoke-r1.json`。16次触发/READY、完整历史、自然完成及清理通过。PyVISA终止符提示保留；PowerShell合并stderr时给出外层退出1，原工具JSON为passed=true，不能将两者混写为无告警执行。
- 分析工具必须显式指定`--clock-source timer1 --timer1-build 20260919052648`并与报告build一致；直接统计硬件毫秒，不按声明输入频率归一化。两位置整轮81/82ms，间隔1001ms为预检快照，非事实源；完整扫描与最终统计见后续记录。

### 059：TIMER1版第三模式1kHz完整360位置通过

- TODO：`NSEQ-110/097/109`；日期：2026-09-19。以下数字为本次硬件/分析快照，非产品事实源。UID `839E1AE79EA20F31`、build `20260919052648`，沿用进度058的OTA包；用户确认IN1为1kHz外部源，OUT4到IN2模拟READY，RJ45物理回环。阈值1000脉冲、360位置、每位置八状态，ANGLE范围0至359、步长1、速度1、标定1000。未注入计数、READY或NEXT。
- 原始报告`out/node-sequence/timer1-external-20260919/third-mode-1khz-360-r1.json`：passed=true，主机观察约360.344s；360位置、2880触发/READY、2879后继推进及2880条连续完整历史均通过核验。末位置阈值360000，最终累计360089脉冲，末角度359有效且next无效。额外89脉冲来自末轮采样期间继续计数。
- LINK/COUNTER/序列故障和拒绝均零；末项自然IDLE，清理后OUT/owned/armed/busy均零；TDMA发送接收增长，环路STOP与读回通过，cleanup_failures为空。此次TDMA诊断查询无超时，但不据单次通过关闭既有偶发快照风险。完整运行使用CMD直接保留stdout/stderr并退出0，日志`out/node-sequence/timer1-full-20260919-r1.log`仍记录PyVISA终止符提示。
- 分析原件`out/node-sequence/timer1-external-20260919/timing-1khz-360-r1.json`绑定源报告SHA-256及明确的TIMER1 build；比例固定1，不使用输入频率修正时间。位置间隔均值999.992ms，P95=1001ms，范围998至1002ms；每位置整轮均值85.375ms，P95=90ms，最大96ms；相邻位置之间采样完成后的等待均值约914.624ms。
- 状态0至7的间隔均值分别为10.333、10.839、10.836、10.750、10.911、10.592、10.542、10.564ms，P95为12至13ms。首状态为位置接纳至首项READY完成，其余为相邻完成记录之差；不是独立测得的SP8T编码或网分采样脉宽。
- 最近消息查询得到680个去重快照，非完整消息轨迹。offer均值2.188ms、return均值3.004ms、inbox读回均为0ms、total均值6ms；inbox的0仅表示取整后不足1ms，不表示零耗时。各段单独向下取整，段和与整段差0至2ms均符合量化界限。return包含TDMA调度/运输/重组，不是线缆传播时延。
- 完成本次第三模式主线复验，不宣称真实网分/RF、独立边沿计数、硬件波形、其他模式、严格TDMA预算或P3通过。PIO定时未改，全局Core0 RTOS时间偏差仍保留；毫秒SCPI字段不能证明微秒级IO时序。软件/文档检查通过，代码尚未提交，进度057的白名单及staged凭证缺口未因此消除。

### 060：统一多板 TDMA/VDC 模型审计与长期任务启动

- TODO：`NSEQ-111/112`；日期：2026-09-19。用户确认每条环路唯一VDC参考发布、每板本地模型、同板三角色共享，并要求TDMA也按多板架构应用于单板。已更新架构草案和长期迁移待办；本条为审计/规划快照，不新增冻结契约或宣称迁移完成。
- 已fetch并核对`origin/wip/tdma-real-flight-processing`的`30e073e0`。上游typed TX只在reference物理槽位发布参考事件，从板exact-sequence MATCH后校正本板committed DCO；旧文档的FOLLOWER直接镜像命令不能代替最新typed路径。clock本地epoch/run与共同来源身份分层，不能要求各板计数恰好相等。
- 当前分支LINK仅接收`physical_source == ring.local_slot_id`的回程，单板工具采用逻辑双节点topology；三个业务槽位仍集中在本板，完整跨板部署路由未接入。START未冻结transport配置身份，优先补该边界，再接后续部署/模型。
- 时间迁移需同时处理原点与单位：当前manager仍用TIMER0 uptime纳秒，旧servo锚点来自逻辑observation，不能把TIMER1 raw或只乘周期直接输入旧模型。上游TIMER1重锚与committed DCO/publication revision为后续复用依据。
- 上游`vdc_priority_tx.inc`与`tdma_pio_spi_ring_origin_publish_priority()`会接管参考节点完整邮箱，当前序列也使用本地邮箱；接入前须定义共存运输分配、期限和背压，不能直接启用两个producer。上游特等席的物理周期准入本身不证明序列端到端响应达标。
- 已有短脉宽基线原件`out/node-sequence/sequence-fast-20260919/baseline-10us-20positions-r1.json`及`baseline-10us-timing-r1.json`：旧build `20260919052648`、外部1kHz、20位置/160采样通过；八状态整轮均值84.35ms，后继采样间隔均值10.636ms。与进度059长脉宽基线相比，调度/运输仍是主要待优化环节；当前无低于1ms或物理波形达标证据。
- 本轮保留全部既有未提交变更，设备尚未因架构审计重新配置或OTA。后续每个切片记录源码、构建、真实OTA、短帧和功能报告；进度057的提交门禁范围缺口继续保留。

### 061：TDMA运行绑定及动作提交首切片三十位置通过

- TODO：`NSEQ-112`；日期：2026-09-19。以下构建、参数、计数和计时为本次验收快照，非产品常量。用户要求快速迭代时每个代码切片只跑30位置；该规则已写入TODO，最终完整扫描仍按NSEQ-116执行。
- START冻结已应用transport配置及adapter启动身份；LINK服务、发送、接收均核对，快照暂不可读时推迟，连续不可读使用独立计时，换代或停止撤销旧运行。实际STEP/FIRE/COUNTER_REARM提交通过TDMA owner的`ring_control_guard`执行身份核对及单次有界回调，Core0 STOP不能在核对后、提交前穿入。prepared IO入口不执行完整service、等待或DMA清理；已递交PIO前缀允许完成。
- 软件验证原件保留：r1模拟START未经过真实guard导致失败，修正fixture后LINK r2通过；r4提取式IO测试缺少新prepared函数，补齐提取列表后r5的LINK/service/IO/TDMA组合212项全过，目录`out/pytest/sequence-binding-20260919-r5`。此前SCPI运行态用例已通过。独立只读审查`/root/sequence_fast_audit`确认三个提交点修复无阻断发现；START加载和初始编码仍走生命周期路径，不据此宣称全部初始化可同步撤销或即时硬件静默。
- 构建目录`out/build/sequence-binding-usbtmc-20260919`，build `20260919061724`；USB运行时切换启用，默认USBTMC，A/B/boot链接检查通过，日志`out/node-sequence/binding-build-20260919-r2.log`。真实VISA OTA报告`out/ota/sequence-binding-20260919-r1/summary.json`通过发送、BOOT、重枚举和commit确认；设备UID `839E1AE79EA20F31`。
- 使用固化工具`sequence_position_validate.py --external-input --angle-scan --positions 30 --threshold 1000 --source-hz 1000 --duration 50 --poll 0.05 --gateway-pulse-us 10 --position-cycle-target-ms 1000 --schedule-evidence`。IN1沿用用户确认的外部1kHz，OUT4到IN2，RJ45物理回环；未注入计数、READY或NEXT。原始报告`out/node-sequence/sequence-binding-20260919/third-mode-1khz-30-r1.json`通过：30位置、240次触发/READY、239次后继推进及240条完整历史；最终累计30090脉冲，末轮采样期间继续计数，故障/拒绝均零。
- 有限轮次自然完成，清理后OUT/owned/armed/busy均零，cleanup_failures为空；TDMA物理TX从39增至14935、RX从38增至14935，坏帧及overrun无增长。SCPI完整原始交互随报告保存，运行时仍有PyVISA终止符提示。调度诊断`strict_realtime_verified=false`且有超预算/错过阶段记录，单板功能通过不代表严格TDMA性能门禁通过。
- TIMER1离线分析`out/node-sequence/sequence-binding-20260919/timing-1khz-30-r1.json`绑定源报告哈希和build，比例固定1：位置周期均值999.966ms；八状态整轮均值89.8ms、P95=93ms、最大96ms；后继状态完成间隔均值11.329ms。现有毫秒记录不能证明READY到OUT边沿低于1ms，性能目标继续待办。
- 首切片完成，NSEQ-112整体仍进行中；完整多板业务路由、共享VDC、运输共存及有界快速调度尚待迁移。源码未提交，进度057的提交凭证范围缺口未关闭；本报告不作为P3、多板、独立边沿或真实网分/RF验收。

### 062：PIO基础拍与逐状态计时、短脉宽闭环验证

- TODO：`NSEQ-115/097`；日期：2026-09-19。以下构建、计数及耗时为本次实测快照，非产品常量。PIO0定时统一使用`SYNC_IO_SEQUENCE_TICK_NS`及派生换算，当前基础拍为4ns；PIO1/2不变。整数微秒参数上限由`SYNC_IO_SEQUENCE_TIME_MAX_US`派生，同步工具及GUI校验；这不代表物理波形精度或VDC同步误差已验收。
- 新增`READ:SEQ:HIST:TIM? <ordinal>`，保留既有`READ:SEQ:TIM?`。每条历史携带版本、时钟、运行/绑定/交换身份、位置/状态、有效标志及六个64位TIMER1阶段偏移；REQUEST/APPLIED/FIRE_QUEUED/DONE分别即时读取硬件计数，OFFERED/RETURNED复用运输记录。零偏移可有效，缺失标记不得冒充零时长。边界均为Core1观察或递交，不是物理编码/OUT/READY边沿。
- `sequence_position_validate.py --timing-evidence`持续保存全部历史及时间身份，`sequence_timing_analyze.py`输出逐位置/逐状态JSON、CSV及Markdown，按原始计数相减，不使用软件tick或按声明输入频率校准。各段之和精确等于该位置总时间；跨位置等待仍使用粗粒度毫秒，最终位置等待留空。
- 最终构建`20260919070600`，USBTMC及运行时USB切换启用，A/B/BOOT链接检查通过；包`out/build/sequence-4ns-timing-usbtmc-20260919-r2/DHRT100_UPDATE.pkg`，SHA-256 `a5ac2f3e1607539a103bac73e99f5206858dfc9542aa0fd97b3a3e40fd14c58e`。真实OTA报告`out/ota/sequence-4ns-timing-20260919-r2/summary.json`通过。初版重复SCPI符号构建失败已修正；中间build的共享服务时间戳数据保留，不作为最终逐段依据。
- 软件回归覆盖LINK、SCPI、IO、工具、GUI、64位回绕、每阶段独立读取、缺失标记、时间加和及导出；最终受影响LINK/分析器组合94项通过，`out/pytest/sequence-4ns-timing-20260919-r6`。此前组合及GUI布局回归结果分别保留于同名前缀r1至r5和`sequence-4ns-gui-20260919-r1`，不将重复执行数量累加为独立用例数。
- 用户确认接线保持IN1外部1kHz、N=1000、OUT4到IN2、RJ45物理回环；每轮30位置、每位置八状态。最终build的10us和1000us脉宽报告分别为`out/node-sequence/sequence-4ns-timing-20260919/third-mode-1khz-30-r2.json`、`third-mode-1khz-30-1000us-r1.json`，均passed=true：240次触发/READY、239次后继推进、240条完整计时历史，自然结束和停止清理通过。
- 用户追加短脉宽测试后，1us首轮`third-mode-1khz-30-1us-r1.json`因启动后的`SYST:REFMEM:SYNC:TDMA:STAT?`超时而提前停止，并读取到SCPI执行错误；输出和资源已释放。原件保留，不能当作脉冲捕获失败或完整通过。相同参数重跑`third-mode-1khz-30-1us-r2.json`通过全部30位置/240次采样及历史，cleanup_failures为空；重跑通过不关闭诊断查询风险。
- 分析报告与原件同目录：`timing-1khz-30-r2.{json,csv,md}`、`timing-1khz-30-1000us-r1.{json,csv,md}`、`timing-1khz-30-1us-r2.{json,csv,md}`。10us与1000us的整位置均值分别89.702ms、89.496ms；1us仍约89ms。1us每状态平均：请求至应用观察2.124ms、应用至运输递交2.587ms、递交至回程3.800ms、回程至FIRE排队0.479ms、排队至完成观察2.105ms、请求前空隙0.046ms。运输回程包含调度/分片/重组，不等于线缆传播；应用观察也不等于PIO编码实际耗时。
- 1us下前项完成观察至后项FIRE排队均值9.018ms、P95为10.372ms、最大11.382ms，仅为软件边界代理值，不能称作物理READY至OUT响应。当前样本说明脉宽不是主导等待，下一切片仍需减少状态机/运输等待；低于1ms目标未通过。100ns虽对应当前25个基础拍，但SCPI仍只接受整数微秒，本轮未扩展接口或冒充100ns已验证。独立波形、±50ns VDC及严格实时预算均未验收。
- 另外回归独立SP8T外部1kHz十轮通过，原件`independent-1khz-ten-r1.json`；双角色`dual-1khz-ten-r1.json`完成80次触发/READY及79次推进，但整体passed=false：清理前TDMA诊断查询超时，随后STOP返回OK而错误队列含执行错误，清理检查失败。现有交互不能把该错误唯一归因于STOP；停止后输出/资源为零。保留NSEQ-RISK-04，不以业务计数通过替代完整验收。
- 本切片完成计时和短脉宽功能确认，NSEQ-115整体保持进行中；完整部署、共享VDC和快速流水线继续待办。源码未提交，进度057的staged凭证范围缺口仍在。

### 063：诊断不可用显式回复与主动探针边界

- 日期：2026-09-19。以下为本切片快照，非产品常量。已追踪`SYST:REFMEM:SYNC:TDMA:STAT?`：底层组合快照任一guard或scheduler try-lock忙时返回false，旧handler直接返回SCPI错误且无查询结果，真实解析器不发送结果终止符并压入执行错误，因此形成主机超时和下一条STOP读到遗留错误的链路。旧报告不能区分当时具体哪个guard竞争；不能把STOP的OK回复说成执行失败。
- Core0诊断handler按`SCPI_TDMA_SNAPSHOT_ATTEMPTS`有限重读，退避引用`SCPI_TDMA_SNAPSHOT_BACKOFF_US`；耗尽返回明确BUSY且不遗留执行错误，正常字段不变，不输出失败拷贝留下的部分数据。底层Core1接口未增加等待。SDK短退避不保证让出RTOS任务，仍可能BUSY；这是诊断可用性边界，不据此宣称根除所有竞争。
- 真实SCPI解析器与真实底层快照组合测试11项通过，目录`out/pytest/tdma-snapshot-scpi-20260919-r3`。早期r1暴露ERR返回不发换行，r2测试输出CRLF归一化导致两项fixture失败，修正后r3通过，原件保留。工具支持显式`--diagnostic-stress`并保存每次诊断前后错误；双角色清理失败时立即记录诊断错误队列，再执行STOP，避免误归因。工具/分析器组合189项通过，`out/pytest/sequence-snapshot-tools-20260919-r1`。独立审查确认修复范围及非物理时序边界。
- build `20260919073022`，包`out/build/sequence-snapshot-usbtmc-20260919/DHRT100_UPDATE.pkg`，USBTMC及运行时USB切换启用，A/B/BOOT链接通过；真实OTA `out/ota/sequence-snapshot-20260919-r1/summary.json`通过。包SHA-256为`487bfd1689174f969191383e2a209904ea56e4d3a79c40e754340f5b6eaaba93`。
- 主动诊断压力原件`out/node-sequence/sequence-snapshot-20260919/third-mode-1khz-30-stress-r1.json`失败：连续TDMA/PHY查询期间收到BUSY；错误队列为空，序列及TDMA停止清理正常。用户指出该方式是干扰探针，确认不作为正常性能验收。原件保留，不以增加重试或盲重跑将其改称通过。
- 关闭额外诊断压力后的`third-mode-1khz-30-r1.json`通过外部1kHz、N=1000、1us触发、OUT4到IN2及真实RJ45三十位置闭环：240次采样及完整计时历史，自然完成/清理通过。该轮仍有常规SCPI历史轮询，不能称作静默无查询。双角色`dual-1khz-ten-r1.json`在同build下十轮完整通过，诊断及停止清理均无失败。
- 用户随后确认使用HAOFV向量隔离：Trigger本地诊断向量由Core1单写，Core0只读；保留完整短轮次记录，START后主机静默，完成后读回，不占用TDMA/VDC同步载荷。下一切片按此迁移；RAM发布仍有成本，不宣称绝对零干扰。本次不关闭NSEQ-RISK-04的ROLE ACT拒绝分项诊断缺口，源码未提交。

### 064：HAOFV本地诊断向量隔离与静默三十位置通过

- TODO：`NSEQ-115/097`；日期：2026-09-19。以下为本次验证快照，非产品常量。用户确认Trigger域本地诊断向量、Core1单写、Core0只读，不占用TDMA/VDC同步载荷；同意运行期间不使用GUI查询。实现复用紧凑历史存储，容量由`TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY`定义，当前为256条，每条96字节，无第二份完整历史缓冲。
- 向量以atomic32 payload及有界sequence快照发布；历史读者不取得实时临界区锁、不访问运行态owner，失败不改调用方输出。Core1新run重置可见窗口，STOP/故障保留已有和部分记录；配置后旧记录保留原身份，不能映射成新配置。新`READ:SEQ:HIST:STAT?`报告版本、时钟、容量、运行/绑定身份、阈值、total/retained/overwritten；旧HIST/TIM字段排列保持。
- 工具`sequence_position_validate.py --quiet-capture --external-input`先读回配置、准备真实环路、核对向量容量，最后单次START；等待声明位置总时间加末轮预算及余量，其间无LINK/COUNTER/TDMA/PHY/历史查询。结束后先确认自然终态，再读取完整历史；前后运行必须换代，向量身份/容量/计数须保持，读回前后元数据一致且零覆盖。默认不隐式重试BUSY。停止清理由既有finally执行，容量不足、错误终态、丢历史、插入命令均失败。
- 静默报告不再声称在线观察过首阈值前状态；只以完成历史证明阈值及请求累计计数关系。`quiet_capture`保存START和首查询序号/时间及静默命令数；只能证明本工具未查询，不证明USB无后台事务或其他未知客户端静默。分析器分别标记静默向量、在线历史轮询、主动诊断压力；没有中途COUNTER样本时host频率回归为null，不制造数据。计数读取/RAM发布仍有开销，绝对零干扰与物理边沿精度均未证明。
- 软件证据：LINK和分析器102项通过，`out/pytest/sequence-vector-core-20260919-r1`；真实SCPI角色/历史接口69项通过，`sequence-history-vector-scpi-20260919-r1`；工具及分析器208项通过，`sequence-vector-tools-20260919-r1`。向量专项覆盖240条完整保留、环形覆盖、旧ordinal拒绝、序号回绕、BUSY输出不变、十万次并发发布、停止/重配/新run隔离及故障/总数溢出，不把主机并发测试等同硬件WCET。
- 新build `20260919074927`，USBTMC及运行时切换启用；A/B/BOOT链接通过，包`out/build/sequence-vector-usbtmc-20260919/DHRT100_UPDATE.pkg`，SHA-256 `9c8cb27422c4dce2dbf4f4b86b2efae1db0bdda99f45aa7ff7deedbbe61bc4fe`。链接map的BSS末地址为`0x2007b21c`，至RAM上界尚余19940字节（链接快照，不等于运行期堆余量）。真实VISA OTA `out/ota/sequence-vector-20260919-r1/summary.json`完成升级、重枚举、build核对与commit。
- 硬件原件`out/node-sequence/sequence-vector-20260919/third-mode-1khz-30-quiet-r1.json`通过；接线仍为IN1外部1kHz、N=1000、OUT4到IN2及RJ45物理回环，网关脉宽1us。START为传输记录第105条，静默约32.015秒后第106条才开始查询，commands_during_wait=0。30位置、240次触发/READY、239次后继推进、240条完整计时记录；向量overwritten=0，读回前后元数据一致。末计数30095，额外95脉冲发生于末位置采样期；故障/拒绝零，自然完成后owner为IDLE，清理后OUT/owned/armed/busy均零。
- 同目录`timing-1khz-30-quiet-r1.{json,csv,md}`提供逐位置、逐状态硬件拍分析。整位置均值95.273616ms，最小92.096348ms、最大97.032620ms；各状态阶段均值：请求至应用观察2.321ms、应用至递交2.119ms、递交至回程4.338ms、回程至FIRE排队0.590ms、FIRE排队至完成观察2.483ms、请求前空隙0.058ms。前项完成观察至后项排队均值9.436ms、P95为9.834ms、最大9.979ms，仅为观察边界代理值。
- 与旧build在线轮询的时间不能直接作因果比较；本次先建立向量隔离后的静默基线，没有实现低于1ms，也没有实际READY/OUT边沿锁存证据。有限向量容量不支持完整长扫描一次性静默保留，超容量会拒绝；后续长扫描须另行规划证据运输。共享VDC、完整物理部署路由和快速流水线仍未完成，代码尚未提交。

### 065：单板验收范围授权与提交检查点

- 日期：2026-09-19；以下为执行快照，非产品常量。用户要求先提交推送再继续，并明确本批TDMA参考多板架构的共享实现只需通过单板闭环验收。单板工具逐文件扩展实时入口、TDMA运行绑定、TIMER1、SCPI诊断及本批测试/GUI依赖；仍拒绝未列举文件，保留暂存与工作树一致性、完整源码指纹、真实包摘要及OTA原件校验，不修改pre-commit分流或伪造P3结果。
- 软件检查`out/pytest/sequence-checkpoint-20260919-r1.xml`为889项通过、14项按另一时钟实现选择跳过；门禁范围测试54项通过。向量及门禁扩展经独立只读审查无阻断问题。固件未因本次提交检查变更，沿用进度064实烧的build及包；本段工具改动不作为另一个固件构建。
- 固定单板验收首次原件`out/HardwareAcceptance/20260919/sequence-vector-checkpoint-r1`中双角色有限/暂停通过；独立MANUAL十轮完成79次推进、输出释放，但STOP读回收到明确BUSY后被旧解析器按格式错误拒绝，整轮失败。修复将该协议结果分类为`SnapshotBusy`，停止等待在原deadline内记录原文和错误队列，最终仍需真实停止快照；不自动重发修改命令。
- r2双角色暂停恢复功能完成，结束后汇总查询同样遇到BUSY；r3在配置阶段`CONF:SEQ:LINK OFF`产生执行错误而无结果，主机超时，原件及清理错误归因保留。后者不是诊断BUSY，尚不能区分configuration guard、LINK writer guard及配置内部拒绝，不能以重跑通过关闭。功能验收的TDMA汇总读回显式处理短暂BUSY并逐次留证；主动压力探针及静默历史读取仍直接拒绝不可用结果。
- 工具相关回归最终341项通过，`out/pytest/sequence-checkpoint-stop-busy-20260919-r3.xml`；r1的旧异常类型断言失败已保留，随后按新的明确BUSY异常更新断言。新增用例覆盖恢复、持续BUSY、SCPI错误、畸形报文及失败原文保留。等待期限不重置；该期限沿用主机工具语义，不承诺单次通信超时也包含在严格墙钟上界内。
- r4固定验收的双角色有限/暂停、独立MANUAL十轮和全部START输出模式通过；位置注入profile拒绝外部IN1计数与软件注入混合，整轮失败。工具新增`--counter-input`选择未接脉冲源的输入；固定注入profile使用`POSITION_COUNTER_INPUT`（本次为IN3），保持OUT4到IN2真实READY和RJ45回程。判据仍逐项核对实际输入绑定、注入命令及累计数，不放宽为允许未知额外脉冲；外部静默profile继续强制IN1。相关回归351项通过，`out/pytest/sequence-checkpoint-input-20260919-r1.xml`。
- r5有限双角色通过，随后暂停profile的角色激活返回`REJECTED`及gate错误，保留`pause-resume.json`；与既有NSEQ-RISK-04同类，尚无分项拒绝位诊断，不能把它归因于已分类的TDMA查询BUSY。本次检查点保留risk标记，下一切片优先收敛配置准入诊断及并发拒绝；不以任何后续单板PASS消除该风险。
- r6的双角色、独立模式、START及IN3注入两位置通过；随后忙阈值profile开始时的TDMA汇总读取BUSY导致整体失败。转台工具运行前后及清理汇总现复用`ring_snapshot`有界读取，BUSY原文和错误队列写入报告；运行期静默窗口不变，HIST及主动诊断探针仍不隐式重试。相关247项回归通过，`out/pytest/sequence-checkpoint-position-busy-20260919-r1.xml`。每次汇总读取有自己的期限，嵌套在清理循环时总墙钟可能超过外层期限，不能声称严格共用一个deadline。
- 最终固定验收`out/HardwareAcceptance/20260919/sequence-vector-checkpoint-r7`全部通过：双角色有限及暂停恢复、独立MANUAL十轮、START的PULSE/LEVEL/NONE/单项/停止重启/热加载、IN3注入两位置及忙阈值故障/暂停累计/停止重启，全部清理通过。固定主机回归195项通过。真实生成并暂存`config/hardware_acceptance/sequence_single_board_receipt.json`，build `20260919074927`、UID `839E1AE79EA20F31`、源码指纹`b59c3c89b4e643e501fa220c7117f8ac245e656a0ffdba316210a818ae50b2a9`；`check-staged`与pre-commit实跑通过，进度057的本批白名单/暂存凭证缺口关闭。
- 提交前另跑`out/node-sequence/sequence-vector-20260919/third-mode-1khz-30-checkpoint-quiet-r1.json`，外部IN1 1kHz、N=1000、OUT4到IN2、1us脉宽和真实RJ45三十位置静默通过：240次采样及完整计时记录，零覆盖；START记录105至首查询106之间等待32秒、零命令，向量读回前后身份/计数一致，自然结束及资源释放通过。离线逐位置/状态拆解为同目录`timing-1khz-30-checkpoint-quiet-r1.{json,csv,md}`，源报告SHA-256 `284ccd651b6c5eca4a1985d95a87ef2ff01e82569d9857ebf835ce8b1a3ea0c1`。前项完成观察至后项FIRE排队均值约9.493ms，仍非物理边沿延迟，低于1ms目标未通过。
- 代码与单板凭证已提交为`ae156377`，带`risk`标记。文档单独提交；NSEQ-RISK-04/06继续OPEN，失败原件未删除。此次只确认单板功能，不声明P3、多板同步、独立边沿计数、波形/RF或严格TDMA稳定性通过；部署映射、共享VDC和快速流水线继续按原长期任务推进，先处理配置拒绝诊断。

### 066：配置拒绝可归因诊断与单板复现

- TODO：`NSEQ-117`、`NSEQ-RISK-04/06`；日期：2026-09-19。以下构建、计数和测试数量为本次执行快照，非产品常量。检查点代码`ae156377`及文档`54387901`已推送，远端核对为`543879012862691c93e7404d8d5c9e84bb7a4f16`；本条后续改动尚未提交。
- TDMA stopped-config 增加typed拒绝原因，保持原检查顺序、锁与回调边界；LINK checked配置返回本次阶段和TDMA/gateway/回退结果。合法但未接纳的`CONF:SEQ:LINK`返回`REJECTED`及原因，同时保留执行错误，不再无回复超时；成功仍返回原accepted值。不重试配置、不放宽运行冻结或停止代次检查。
- ACT checked结果包含本次attempt、registry错误、evaluated/failed/unavailable位、staging身份和质量状态。`SYST:REFMEM:LOAD:ACT:STAT?`提供读回；成功ACT原字段保持，失败追加诊断，纯快照不可用返回BUSY。独立只读审查发现NODE ACT别名提前退出且测试未经过实际wrapper，已统一委托处理器并补真实别名测试，运行期拒绝仍保持。参数解析失败不算owner激活尝试。沿用完整TDMA快照，本切片仅分类，没有消除其无关锁依赖；gateway回退及registry激活后的apply失败仍有既有事务边界，不能宣称所有失败完全无副作用。
- 软件证据：LINK/TDMA/角色SCPI组合157项通过，`out/pytest/sequence-config-diagnostic-r3.xml`；补充真实writer guard及epoch耗尽后LINK74项通过，`sequence-config-guards-r1.xml`；周边SCPI/TDMA快照和文档186项通过，`sequence-config-surrounding-r1.xml`；最终ACT别名、冻结和配置工具105项通过，`sequence-config-alias-r1.xml`。r1缺少新测试桩声明、r2返回值补丁落错函数导致失败均保留，已修正后再验。新增固化工具`sequence_config_validate.py`逐次保存配置回复、错误队列、当前attempt和角色读回，首次拒绝即停止，不重放修改命令。
- 初版build `20260919085259`的真实VISA OTA通过，`out/ota/sequence-config-diagnostic-20260919-r1/summary.json`。初版外部静默报告`out/node-sequence/sequence-config-diagnostic-20260919/third-mode-1khz-30-quiet-r1.json`失败：START后静默约32秒，零运行期命令；COUNTER events/positions/partial均为零，LINK等待首位置且error为零，未发生采样。清理后输出和owner释放，不能判定信号源关闭或固件捕获故障；已请用户确认IN1外部1kHz，确认前不冒充外部闭环通过。
- 别名修正最终build `20260919090217`，目录`out/build/sequence-config-diagnostic-usbtmc-20260919-r2`，USBTMC默认及USB运行时切换启用，A/B/BOOT链接检查通过。包SHA-256 `4b6f5a884bfddb7c4e3a6c94454d797f0fc9a6e0c46123ee15d13fb828939d7f`，真实OTA `out/ota/sequence-config-diagnostic-20260919-r2/summary.json`完成升级、重枚举及commit。PowerShell重配置日志将SDK正常stderr包装为NativeCommandError并返回非零，原件保留；最终使用CMD直接保存日志的独立目录构建退出零。
- 最终固件配置工具`config-ten-r2.json`前九次通过，第十次ACT返回BUSY而非超时，当前诊断原文`10,7,3,191,0,64,2178798616,10,1,0,0,0,0,0`，证明此次不可用来自质量快照，真实失败位为空。后四项零值在quality unavailable时不是有效质量证据，不据此宣称计数为零。初版`config-ten-r1.json`十次通过仍保留，不能抵消最终固件复现。后续应提供窄质量快照，避免无关scheduler/ring/registry依赖，仍须保留相关发布竞争和真实质量错误拒绝。
- 最终固件独立MANUAL十轮`independent-manual-ten-r2.json`通过；三槽位`position-injected-r2.json`通过IN3软件注入两位置、忙阈值故障及暂停/恢复/停止重启，OUT4到IN2与RJ45仍为真实物理回环。双角色普通工具路径`dual-manual-ten-r2.json`在`*CLS`无回复命令上等待结果超时，未开始业务运行，清理通过；使用既有GUI命令路径`dual-gui-manual-ten-r2.json`软件READY十轮通过，不把它称作修复了普通工具路径。
- 按用户单板口径补充本切片接口头及测试/配置工具逐文件白名单；未改门禁判据。固定验收`out/HardwareAcceptance/20260919/sequence-config-diagnostic-r1`的双角色有限、暂停恢复、独立十轮和全部START profile通过；位置profile的角色激活再次返回BUSY，diagnostic attempt为18、failed_mask为空、unavailable_mask为质量位，整体验收失败，未生成新凭证。原receipt仍只覆盖检查点源码，禁止用于本次提交。
- 本轮保留配置诊断切片待后续修复和外部脉冲复验，不推进部署/VDC下一迁移，不关闭NSEQ-RISK-04/06，不将注入profile替代外部静默30位置。当前设备已停止序列和TDMA，后续先修窄质量读取及普通工具无回复命令处理，再真实构建/OTA、固定单板门禁和外部短轮次；通过后才能提交本切片。

### 067：窄质量快照修复及当前源码单板验收

- TODO：`NSEQ-117`、`NSEQ-RISK-04/06`；日期：2026-09-19。以下为本轮执行快照，非产品常量。用户再次确认TDMA参考多板模型、单板跑通即可功能验收。本轮保留受限提交口径，不以单板凭证宣称多板或物理时序通过。
- 新增`tdma_service_get_quality_snapshot`及RefMem包装，只读取质量准入需要的intent/result发布，避开无关ring/registry/scheduler锁。保留全局reject/overrun/timeout与原traffic-class错误映射；有界重读耗尽仍拒绝且不改输出，不清零错误、不重放ACT。scheduler绑定及初始化仍要求静止生命周期。普通VISA工具的标准无回复`*CLS`改为只写，随后独立查错误队列，不伪造ACK。
- 软件组合160项通过，`out/pytest/sequence-quality-combined-r1.xml`；真实RefMem完整/窄快照映射测试通过，`out/pytest/sequence-quality-mapping-r1`；文档测试18项通过，`sequence-quality-docs-r1.xml`。独立只读审查未发现阻断问题，指出尚无读取途中configured变化的独立注入测试，代码已有复查；不据此声明任意并发重绑安全。
- build `20260919092554`，USBTMC默认及USB运行时切换启用，A/B/BOOT链接通过；包`out/build/sequence-quality-usbtmc-20260919/DHRT100_UPDATE.pkg`，SHA-256 `571f8d3eb3aafa7c472629062176d21c399aee8ff12141866a19055d9a729515`。真实OTA `out/ota/sequence-quality-20260919-r1/summary.json`通过升级、重枚举、build核对及commit。
- 配置工具连续30次独立事务全部通过，`out/node-sequence/sequence-quality-20260919/config-thirty-r1.json`；普通双角色工具软件READY十轮通过，`dual-manual-ten-r1.json`，覆盖此前`*CLS`失败路径。相关发布竞争负测仍拒绝，成功重跑不证明所有间歇配置问题根除。
- 固定单板门禁`out/HardwareAcceptance/20260919/sequence-quality-r1`全部通过：双角色有限轮次及暂停恢复、独立重复、START全部profile、三槽位注入及忙阈值/生命周期。生成`config/hardware_acceptance/sequence_single_board_receipt.json`，绑定staged源码指纹`54bb4f69528f8a4eebcbcc8d705c3f84964ef1000fc93fa4b422b1dd42dacecd`。三槽位为IN3软件注入，OUT4到IN2及RJ45为真实物理回环；不替代外部转台计数验证。
- 同build外部复验`out/node-sequence/sequence-quality-20260919/third-mode-1khz-30-quiet-r1.json`失败：声明IN1为1kHz、N=1000、30位置、1us OUT4到IN2；START之后32秒静默且零运行期命令，counter events/positions/partial均零，phase为WAIT_POSITION、error零，未产生采样历史。停止后OUT/owned/armed/busy归零，错误队列为空。此结果不能区分外部信号缺失与输入捕获故障；等待已发出的信号源确认，不生成无样本的计时结论。
- 按既有受限提交授权保存已通过单板门禁的修复，继续保留risk：质量快照无关锁依赖已修复，NSEQ-RISK-04仍待外部短轮次闭合；NSEQ-RISK-06原间歇OFF拒绝尚无新现场诊断，不能称根因消除。下一步恢复并验证外部输入，再跑静默30位置和逐段计时；该切片闭合前不推进部署/VDC下一迁移。
- 代码提交`177a1fe9`带`risk`，提交钩子已核验当前单板凭证，文档分离提交。文档全量检查及回归通过；仅保留既有`TDMA-FLIGHT-BITMAP-01`格式警告。约定的`D:/Aphranda/Git/bin/bash.exe`在本机不存在，手动门禁使用已安装的`C:/Program Files/Git/bin/sh.exe`通过；构建和工具继续使用本机PowerShell/CMD及Python入口。

## 失败与回退

配置模型、SCPI与GPIO版板端验证已完成相应记录；当前迁移PIO0，后续结果按新增记录跟踪。
后续失败必须按 progress ID 追加原因、输出文件和回退状态，不覆盖旧失败记录。

## 下一 Gate

最新配置修复及单板凭证以进度067为准；当前外部IN1计数为零，需先确认并恢复外部输入，再完成本切片静默短轮次。以下早期验收和性能记录不替代该待办。

三模式均有本地物理反馈功能证据，转台已补外部50Hz、N=50两位置GUI命令路径验证。NSEQ-106仍待独立真实网分及生命周期补证，NSEQ-RISK-05自动验收与OTA摘要仍未闭合；NSEQ-108待回环窗口及打包EXE完整验证，保留TDMA诊断快照偶发失败与固件时间尺度偏差。
历史TIMER1版1kHz完整360位置证据见进度058/059，旧RTOS tick分析仅保留历史；当前build三模式固定回归及提交凭证以进度065为准。全局Core0时钟及迁移后的最终完整扫描仍需分别闭合。
最新诊断及计时切片以进度063/064为准：向量版新固件外部1kHz三十位置静默闭环通过，主机零运行期命令，完整历史零覆盖。双角色诊断/清理在进度063对应build已通过；主动诊断压力仍会BUSY，不把它作为性能基线。后续每切片保持静默30位置、完整历史及停止清理核验，再推进统一部署、共享VDC与运输共存。最终完整扫描保留360位置，当前毫秒级性能和调度超预算未关闭。
NSEQ-105三模式单板凭证及代码检查点已完成；既有下一代码提交约束仍优先关闭
NSEQ-RISK-04激活gate与NSEQ-RISK-06配置拒绝诊断缺口，不以重跑通过代替根因闭环，再继续NSEQ-101至104迁移。
保留独立SP8T的MANUAL/IN回归；组合角色使用统一PIO0 owner及真实RJ45运输，不软件直达。
PIO握手首切片和Core1运行状态机功能已通过，固定200ms位置周期与严格TDMA稳定性仍未通过；
后续改动逐片构建、OTA和单板闭环，重新生成匹配staged指纹的凭证，旧报告不能替代。
其他输入独立激励、外部波形、SP8T 射频通路和多板同步仍按未验收项记录，不从汇总计数推断通过。
用户单板功能验收口径保持；不宣称 P3 或全节点裁决通过，提交仍服从实际门禁，历史失败保留。
