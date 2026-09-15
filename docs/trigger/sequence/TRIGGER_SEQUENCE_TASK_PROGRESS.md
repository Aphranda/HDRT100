# 节点序列预约触发任务进度

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/sequence/TRIGGER_SEQUENCE_TASK_PROGRESS.md`
Related: `docs/trigger/sequence/TRIGGER_SEQUENCE_ARCHITECTURE.md`, `docs/trigger/sequence/TRIGGER_SEQUENCE_TODO.md`, `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.html`, `docs/reports/distributed-trigger/相控阵测试系统RP分布式触发方案技术报告0804.html`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-09-15

## 文档接口

[Architecture](TRIGGER_SEQUENCE_ARCHITECTURE.md) 维护稳定设计，
[TODO](TRIGGER_SEQUENCE_TODO.md) 维护任务状态，本文件只追加执行与证据记录。
每条记录包含 progress ID、TODO ID、日期、变更/提交、验证、结果、证据及下一 gate。
本文件中的分支、提交、行号、测试结果与计数均为当次审计快照，非产品事实源。

## 当前 Checkpoint

`NSEQ-001/002` 已完成：实际链路和源码审计、标准三件套、自动门禁与独立复核通过。
用户已确认“一脉冲一序列状态”，对应链路切换及其他节点工作。
`NSEQ-010` 配置模型实现、行为测试、构建与独立软件复核通过。
用户后续明确本轮仅单节点调试、TDMA仅预留接口；已接通并在COM10验证
SCPI、编码输出、建立时间、完成脉冲和实际IO读取。SP8T全部地址及回绕通过，见进度008。
IN1已接外部信号并完成低频功能验证，见进度010；四板P3失败为历史事实，
不再作为本轮单节点开发前置条件，提交凭证也未伪造或绕过。
当前工作分支为 `feature/node-sequence-reservation-trigger`，创建基线为
`wip/tdma-real-flight-processing` 的 `7fa834df`（本次工作快照）。
配置、软件切步及IN1低频流程已通过；其余输入实际激励、独立波形和开关本体通路未验收，
不得据此宣称支持分布式节点预约。设备保留SP8T/IN1上升沿配置，IDLE且输出低。

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
- 用户新增并修正接口为`CONFigure:SEQuence:NEXT`、`READ:SEQuence:NEXT?`。
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
  `next-bus-r2.json`使用新`CONF:SEQ:NEXT`/`READ:SEQ:NEXT?`完成SP8T九步与停止，PASS。
  `1khz-final-idle-r1.txt`确认恢复IN1上升沿、100us+100us，IDLE、输出和租约为零。
- 证据边界：上述外部测试使用SCPI间隔观察及暂停后稳定pad读回，不是每边沿独立波形认证；
  运行时拒绝数待结算，只有暂停/停止后的最终结果用于判定。IN2-IN4实际激励尚未完成。
  用户计划后续5kHz和1MHz压力测试；后者先验证过载拒绝/恢复，不承诺当前实现每微秒完成一步。
- NSEQ-061按单板功能范围完成，NSEQ-062进入提速验证；尚未提交或合并。

### NSEQ-PROGRESS-20260915-013 - 5 kHz与1 MHz压力档及调试界面

- 频率调整通过外部信号源完成，序列固件无需重新编译；输入源、边沿、建立时间和完成脉宽均可在停止状态下通过SCPI重新配置。
- 5 kHz连续IN1在PIO0后端通过上升沿、下降沿及忙时测试：10/10微秒档上升沿完成27636步、下降沿完成27830步，均无fault；200/200微秒忙时档完成5885步并记录11768次busy拒绝。暂停、继续和停止后的计数稳定，输出与租约释放。
- 用户切至1 MHz后，使用10/10微秒档完成单板压力验证：测试期间完成115909步，无fault；暂停时接纳95171/完成95171，记录1998586次busy拒绝；继续后接纳115909/完成115909，最终停止回到IDLE并释放IO。证据文件SHA-256为`9390AC599DB749D11EA606E0473A81F834375DF29BD11539DABC1BEC0B2A92A0`（快照，非事实源）。该结果证明过载拒绝、暂停/恢复和停止恢复路径工作，不代表每个1 MHz边沿均被执行或完成波形认证。
- 实时读回确认`READ:SEQ:NEXT?`与`READ:IO:STAT?`可用；板卡随后恢复IN1上升沿、10/10微秒、IDLE配置。`tools/sequence_trigger_debug_ui/sequence_trigger_debug_ui.py`提供纯Tk调试界面，可自动发现串口并通过单一后台队列串行化操作，支持序列控制、当前位置与IO读回、分级日志及导出、设备身份/自定义SCPI查询，并复用`tools/ota_multi_update/ota_multi_update.py`执行单板OTA。
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

## 失败与回退

配置模型、SCPI与GPIO版板端验证已完成相应记录；当前迁移PIO0，后续结果按新增记录跟踪。
后续失败必须按 progress ID 追加原因、输出文件和回退状态，不覆盖旧失败记录。

## 下一 Gate

真实SCPI控制、SP8T编码全组合及回绕、busy拒绝、暂停/停止和实际IO读回已通过。
IN1低频双沿、暂停/恢复/停止和忙时拒绝已通过。补齐其余输入激励、独立波形及SP8T实体通路；
PIO提速先核对SYNC资源分配，再实现并验证persona热加载，未验证项不得宣称通过。
START首状态预置还需当前源码P3凭证和OUT4外部波形闭环，完成前NSEQ-063保持IN PROGRESS。
本轮不实施TDMA通信或全节点裁决；按用户确认的单板调试口径，单板验证通过后允许提交，原始P3失败保留。
