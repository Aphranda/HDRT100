# 节点序列预约触发任务进度

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/sequence/TRIGGER_SEQUENCE_TASK_PROGRESS.md`
Related: `docs/trigger/sequence/TRIGGER_SEQUENCE_ARCHITECTURE.md`, `docs/trigger/sequence/TRIGGER_SEQUENCE_TODO.md`, `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.html`, `docs/reports/distributed-trigger/相控阵测试系统RP分布式触发方案技术报告0804.html`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-09-17

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
最新主线先完成功能再优化短暂接收掉落；严格稳定性失败不改写为通过。
独立有限轮次已完成新固件 IN1/BUS 验证；进度025修复回帧证据及probe启动问题后，
组合单轮/十轮/连续STOP、同固件独立与手动SP8T均通过。正在补暂停/恢复与异常场景；
完整动态 claim/多板验收尚未完成。
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

## 失败与回退

配置模型、SCPI与GPIO版板端验证已完成相应记录；当前迁移PIO0，后续结果按新增记录跟踪。
后续失败必须按 progress ID 追加原因、输出文件和回退状态，不覆盖旧失败记录。

## 下一 Gate

按最新用户优先级推进 NSEQ-081/082：保留短暂掉落的严格失败，将其优化延后，
真实 RJ45 角色递交、VNA 首次至末次测量及有限/持续协作已在进度025通过。
保留独立 SP8T、DUT-only BUS/IN 的回归；组合角色需在统一 PIO0 owner 下管理 SMA 租约。
当前源码已由受限门禁固定 profile 补齐 typed snapshot、PAUSE/CONT exchange 轮换、STOP 资源回收
和 GUI 命令构造路径单板证据；后续变更仍须重新生成匹配 staged 指纹的凭证，旧报告不能替代。
其他输入独立激励、外部波形、SP8T 射频通路和多板同步仍按未验收项记录，不从汇总计数推断通过。
用户单板功能验收口径保持；不宣称 P3 或全节点裁决通过，提交仍服从实际门禁，历史失败保留。
