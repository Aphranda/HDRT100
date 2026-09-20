# 工具域治理任务进度

Status: Active
Domain: Tools
Canonical: `docs/tools/DOCS_TOOLS_TASK_PROGRESS.md`
Related: `docs/tools/DOCS_TOOLS_ARCHITECTURE.md`, `docs/tools/DOCS_TOOLS_TODO.md`, `docs/check/DOCS_REGRESSION_PLAN.md`
Last updated: 2026-09-21

## 文档接口

本文只记录实施与验证事实，任务状态以 TODO 为准，治理语义以 Architecture 为准。每条记录引用稳定任务 ID，使用唯一 `TOOLS-PROGRESS-YYYYMMDD-NNN`，最新记录在上；轮转遵循文档自回归 C14/C15，历史失败和原始证据不得丢失。

## 当前 checkpoint

### TOOLS-PROGRESS-20260921-022：标准结果与交接复现

- TODO task ID：TOOLS-HANDOFF-001。
- 实现提交 `4a02e887` 将 internal-seal 的二十份历史 Python lineage、两份运行输入按原字节纳入 `tools/archive/vdc/vdc_joint_capture/lineage_v1/`，manifest 的 `runtime_entrypoint` 为空且 executable closure 为零；提交 `f4040543` 再纳入两份只读硬件 golden，characterization 只执行 tracked parser/oracle，不再 import archive Python。两批均由工具登记唯一归属；独立复核先发现 README 三处旧 ancestry 描述、phase 全字段覆盖弱化及 golden provenance 缺口，逐项修正后最终 PASS。
- 隔离交接工作区为 `out/archive/pending-deletion/vdc-handoff-clean/`，detached HEAD 固定 `f4040543`，保留等待用户最终评审删除。初始 checkout 不含主工作区历史证据；`load_inputs()` 实测二十 migrated、零 runtime closure，governance/catalog 通过；完整 joint、probe-support 与 governance 回归为 `243 passed / 1 skipped`（快照），主工作区同集合为 `245 passed / 1 skipped`，唯一跳过是 Windows 无符号链接权限。首次聚焦命令暴露 tests 仍依赖历史 `out/` 及 `--basetemp` 父目录问题；失败原样保留，补 tracked golden、probe-support 冻结常量并移除 archive 执行后复验全绿。
- 主工作区同指纹验收保留 `p3-r3..r7` 原件；其中 `p3-r5` 因 NO1 并行 START `<timeout>` fail-closed，最终四板 timing/delay/identity 读回恢复，错误原件不改写；后继成功凭证指向 `p3-r7`。该轮使用当前源码重新构建并完成四板 OTA、P0T、校准、TRN-00..03、TDMA 和带 HDO 的 60 s DPLL，结果 `PASS_WITH_WARNINGS`（快照），errors / cleanup_errors / restore_needed 为空，`check-staged` 通过。
- 跨工作区硬件复现不复用主工作区 receipt：新增 probe-support 测试后，隔离 worktree 重新执行标准 `p3_hardware_acceptance.py run`，自产 build `20260920230455`、package、四板 OTA summary 与原始证据，`p3-070447` 为 `PASS_WITH_WARNINGS`；一次 `resume` 在 TRN-00 选定行不完整时 fail-closed 并保留 `handoff-clean-p3-r4`，随后新目录 `handoff-clean-p3-r5` 成功。两次成功 scope-on 均取得十二份 scope record / 四十八个 RAW block；`p3-070447` / `p3-r5` 外部最大绝对相差约 5.870 ns / 23.957 ns，三个内部 endpoint 最大绝对残差 93/86/89 ns、91/86/86 ns（快照，均为 INFO），DPLL errors 为空，outer errors / cleanup_errors / restore_needed 均为空。
- README 的直接命令随后在同一 worktree 输出 `handoff-clean-joint-r2`：scope-on 60 s 返回 passed，outer report complete，errors / cleanup_errors / restore_needed 均为空。旧 `out/` Python 未被导入或执行，历史路径只作 provenance locator；冻结 golden 只用于离线测试。TOOLS-HANDOFF-001 据此关闭，下一项为 TOOLS-CHECK-001。

### TOOLS-PROGRESS-20260921-021：child base probe 与零 runtime closure

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-BASE-001。
- 新增 `tools/workflows/vdc_joint_capture/base.py`，接管四板 allowlist、query barrier、work/file 上限、JSONL action、control/trace ACK、STOP barrier、record poll、TDMA/trace export、pipeline、load/configure/acquire/collect 和基础 standalone lifecycle。模块从 calibration owner 已加载的 command 解析唯一 backend，不修改 `sys.path`，import 不枚举或打开设备；standalone cleanup 只处理自身连接，三个全局 owner 无条件恢复。
- V14 manifest 将 archived `input_probe.py` 固定为 `base_probe` migrated role，runtime entrypoint 改为 null、runtime closure 为空。fresh-process actual=declared=0，missing / declared-not-loaded / migrated_loaded 全为空；P3 provenance 只有 tracked base/typed，20 份 archived lineage 均不执行，旧原件不删除。
- 软件与独立审查：base characterization 覆盖 command/control/trace、STOP 重试、poll、load、TDMA cancel→RAM→SAVE→SD、trace SAVE/count/decode、configure/ARM、acquire/START/TRIAL、collect 顺序、import 无设备副作用，以及 standalone 成功和 configure/collect/单板 restore/单板 close 故障。修复前宽组合 2086 passed / 1 skipped（快照），跳过项仅为 Windows 无符号链接权限；reviewer 随后发现 standalone run 的 close 可阻断三个 owner 恢复及方法覆盖不足，补齐后聚焦组合 191 passed。最终独立复核 PASS，无剩余 finding；工具治理/catalog、编译、source length 和 whitespace 检查通过。
- 同指纹四板 P3 `tools-base-r1/p3-r1` 使用 build `20260920171028`，结果 PASS_WITH_WARNINGS（快照：INFO 33 / WARN 20 / ERROR 0 / FATAL 0）。V14 报告记录 20 migrated / 0 runtime，tracked base/typed 在 provenance 中唯一；DPLL 60 s scope-on outer/inner/joint 通过，12 份 scope record、48 个 RAW block，外部最大绝对相差 77.958 ns，三个内部 endpoint 最大绝对残差 96 ns（快照，INFO）；errors / cleanup_errors / restore_needed 均为空，`check-staged` 通过。原件位于 `out/HardwareAcceptance/20260921/tools-base-r1/p3-r1/`。
- 实现提交 `a130215b`；TOOLS-WORKFLOW-BASE-001 与 TOOLS-WORKFLOW-001 关闭。下一项为 TOOLS-HANDOFF-001，在另一新工作区按受控输入复现离线与硬件流程；本 checkpoint 不把当前工作区通过替代跨工作区交接。

### TOOLS-PROGRESS-20260921-020：child typed-TX 与 lifecycle 层迁移

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-TYPED-001。
- 新增 `tools/workflows/vdc_joint_capture/typed.py`，从 manifest 指定的 base 入口构造 tracked TypedProbe，接管 fields/CRC16/typed mailbox、receipt/source/UID/build 准入、四板连接、stage/load/FIFO/TX prime、SYNC/ARM、START/TRIAL、STOP collect、逐板 disable/restore、revoke、逐连接 best-effort close 和两个 board-command owner 恢复。transport registry 从 admitted base 的实际 `SerialSession` module 解析并 fail-closed，移除 archived `sys.path` 隐式导入。
- V13 manifest 将 archived `capture_typed_tx.py` 固定为 `typed_tx` migrated role，runtime entrypoint 下沉到 `input_probe.py`；动态 out closure 快照从 2 降到 1。fresh-process actual=declared=1，missing / declared-not-loaded / migrated_loaded 全为空；实际 provenance 中 archived typed 为零、tracked typed 唯一，旧原件不删除。
- 软件与独立审查：冻结 `native-r1/input-probe.json` 的 mailbox/CRC/event 对 archived/tracked 全等，8 类 mailbox 负测同拒；characterization 覆盖 configure/acquire/collect、check-only、非 check-only 完整 lifecycle，以及 configure/collect/单板 restore/单板 close 故障。宽组合 2074 passed / 1 skipped（快照），跳过项仅为 Windows 无符号链接权限；最终 Probe fresh-process 构造及 transport registry identity 聚焦回归通过。独立复核先发现 close 异常可阻断 owner 恢复及 lifecycle fault 覆盖不足，修正后又由 P3 暴露重复 transport module，改为实际 backend module identity 后最终 PASS，无剩余 finding。
- 同指纹四板 P3 失败证据完整保留：`p3-r1` 因 typed namespace 缺 `scpi_serial` 在 Probe 构造期拒绝；`p3-r2`、`p3-r4`、`p3-r6` 在 P0T 链路准入失败；`p3-r3`、`p3-r5` 因 transport registry 写入第二模块实例，FOLLOW 查询稳定返回 timeout，tracked cleanup 均完成逐板恢复、revoke 和连接关闭。最终 `p3-r7` 使用 build `20260920171028`，结果 PASS_WITH_WARNINGS（快照：INFO 33 / WARN 18 / ERROR 0 / FATAL 0）；V13 报告记录 19 migrated / 1 runtime，tracked typed/direct 在实际 provenance 中唯一。DPLL 60 s scope-on outer/inner/joint 通过，12 份 scope record、48 个 RAW block，外部最大绝对相差 2.000 ns，三个内部 endpoint 最大绝对残差 94 ns（快照，INFO）；errors / cleanup_errors / restore_needed 均为空，`check-staged` 通过。原件位于 `out/HardwareAcceptance/20260921/tools-typed-r1/p3-r1..r7/`。
- 实现提交 `535ce95c`；TOOLS-WORKFLOW-TYPED-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。下一批迁移唯一剩余 base 祖先，继续保留 archived 原件和失败证据。

### TOOLS-PROGRESS-20260921-019：child direct-RX 层迁移

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-DIRECT-001。
- 新增 `tools/workflows/vdc_joint_capture/direct.py`，从 manifest 指定的 typed-TX 入口构造 tracked DirectProbe，接管 direct/lane/timing/budget parser、冻结 baseline 差分、retained header/mailbox CRC 与 identity/payload 核验，以及 direct、master 和 polled consumer 的 event 比较。STOP 后先逐项保存四板原始 snapshot，再隔离执行三个 follower assessment；同 identity 的 payload mismatch 必须拒绝，不同 identity 的 retained event 仅记录 disposition。
- V12 manifest 将 archived `capture_direct_rx.py` 固定为 `direct_rx` migrated role，runtime entrypoint 下沉到 `capture_typed_tx.py`；动态 out closure 快照从 3 降到 2。fresh-process actual=declared=2，missing / declared-not-loaded / migrated_loaded 全为空；实际 provenance 中 archived direct 为零、tracked direct 唯一，旧原件不删除。
- 软件与独立审查：冻结 `native-r1/input-probe.json` 的四板 direct/lane/timing/budget、retained header 与 event comparison 对 archived/tracked 逐字段全等；characterization 覆盖 STOP gate、baseline、retained/polled 成功路径、sequence/transport CRC/packet CRC/schedule/profile/mailbox CRC、same-identity payload mismatch、different identity 和单 follower 隔离。宽组合 2045 passed / 1 skipped（快照），跳过项仅为 Windows 无符号链接权限；补测后聚焦回归 54 passed，工具治理/catalog、编译、source length 和 whitespace 检查通过。独立复核先给出 CRC/event 负测覆盖备注，补齐后最终 PASS，无剩余 finding。
- 同指纹四板 P3 首次 `tools-direct-r1/p3-r1` 在 P0T 邻接准备时出现 STOP ACK 未确认及 `rx_frames=0` / `magic_fail=106`，未进入本批 child，失败原件保留。新目录重跑 `p3-r2` 使用 build `20260920171028`，结果 PASS_WITH_WARNINGS（快照：INFO 32 / WARN 22 / ERROR 0 / FATAL 0）；V12 报告记录 18 migrated / 2 runtime，tracked direct/match 在实际 provenance 中唯一。DPLL 60 s scope-on outer/inner/joint 通过，12 份 scope record、48 个 RAW block，外部最大绝对相差 47.959 ns；三个内部 endpoint 最大绝对残差 102 ns，按实现门限为 WARN、未达 ERROR（快照）。errors / cleanup_errors / restore_needed 均为空，`check-staged` 通过。internal-seal 不调用深层 DirectProbe.collect，standalone 语义由直接测试证明。原件位于 `out/HardwareAcceptance/20260921/tools-direct-r1/p3-r1..r2/`。
- 实现提交 `6b883073`；TOOLS-WORKFLOW-DIRECT-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。下一批迁移 typed/base 剩余祖先，继续保留 archived 原件和失败证据。

### TOOLS-PROGRESS-20260921-018：child exact-match 层迁移

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-MATCH-001。
- 新增 `tools/workflows/vdc_joint_capture/match.py`，从 manifest 指定的 direct-RX 入口构造 tracked MatchProbe，接管 match parser、有向 path delay 重建、可选 TAP 安装/恢复、三个 follower 的 MATCH/session 启用读回、STOP-only origin/match/model/event/TAP/recovery 采集，以及 identity/path/interval 独立核验。parent transport assessment 失败仍继续 follower assessment，单 follower 错误不阻断其他板。
- V11 manifest 将 archived `capture_match.py` 固定为 `exact_match` migrated role，runtime entrypoint 下沉到 `capture_direct_rx.py`；动态 out closure 快照从 4 降到 3。fresh-process actual=declared=3，missing / declared-not-loaded / migrated_loaded 全为空；实际 provenance 中 archived match 为零、tracked match 唯一，旧原件不删除。
- 软件与独立审查：冻结 `native-r1/input-probe.json` 的三个 follower `match_raw`、parsed match 与独立 delay 对 archived/tracked 逐字段全等；characterization 覆盖 directed path 负测、TAP/MATCH 启停和恢复读回、STOP gate、采集顺序、parent failure 后继续 assessment 与单 follower 隔离。宽组合 2033 passed / 1 skipped（快照），跳过项仅为 Windows 无符号链接权限；工具治理/catalog、编译、source length 和 whitespace 检查通过。独立复核先发现一处重复登记仍写四文件 ancestry，修正后最终 PASS，无剩余 finding。
- 同指纹四板 P3 使用 build `20260920171028`，`tools-match-r1/p3-r1` 为 PASS_WITH_WARNINGS（快照：INFO 33 / WARN 18 / ERROR 0 / FATAL 0）。V11 报告记录 17 migrated / 3 runtime，tracked match/observer 在实际 provenance 中唯一；DPLL 60 s scope-on outer/inner/joint 通过，12 份 scope record、48 个 RAW block，外部最大绝对相差 50.208 ns，三个内部 endpoint 最大绝对残差 99 ns（快照，INFO）；errors / cleanup_errors / restore_needed 均为空，`check-staged` 通过。internal-seal 不调用深层 MatchProbe.collect，本次 P3 不虚构 match 查询；standalone 语义由直接测试证明。原件位于 `out/HardwareAcceptance/20260921/tools-match-r1/p3-r1/`。
- 实现提交 `bd495a05`；TOOLS-WORKFLOW-MATCH-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。下一批迁移 direct/typed/base 剩余祖先，继续保留 archived 原件和失败证据。

### TOOLS-PROGRESS-20260921-017：Core0 Tick 升频顺序纠错与四板复验

- TODO task ID：TOOLS-P3-DPLL-SHORT-001；本记录纠正 TOOLS-PROGRESS-20260920-011 中“board 在 scheduler 前升频”的误判，不改变 TOOLS TODO 状态，也不新增跨域契约。
- 根因核验：RP2350 FreeRTOS port 的 `vPortSetupTimerInterrupt()` 在 scheduler 启动时使用运行态 `clock_get_hz(clk_sys)` 装载 Core0 SysTick；此前 `task_system()` 启动后才经 `app_runtime_bringup()` 调用 `board_init()` 将系统时钟切到 `BOARD_SYS_CLOCK_HZ`。因此 reload 仍按启动时默认时钟装载，升频后由 `configTICK_RATE_HZ` 表示的 1 ms Tick 实际约为 0.6 ms（推导快照，非事实源）。`configCPU_CLOCK_HZ` 已等于 `BOARD_SYS_CLOCK_HZ`，但非 tickless port 的 reload 路径不使用该宏，单改宏不能修复启动顺序。
- 修复提交 `6c8b0b41` 新增幂等 `board_clock_init()`，在 `osal_kernel_init()`、任务创建和 scheduler 启动前建立板级时钟；后续 `board_init()` 再次核对同一 owner。新增源码顺序/时钟契约回归与既有 Core1 release 回归共 10 passed（快照），`pico2-release` 构建及 Flash link 检查通过。
- 影响边界：Core1 TDMA 使用独立 core-local SysTick，并且只在 `board_init()` 后启动，故其线速周期没有直接按 150 MHz 运行；受影响的是 Core0 上所有 RTOS delay/timeout，以及每 Tick 调用 `tdma_runtime_owner_core0_prepare_service()` 的准备/控制面，历史执行频率曾被间接加速，不能再表述为“对 TDMA 无影响”。
- 同指纹四板 P3 原件位于 `out/HardwareAcceptance/20260921/core0-tick-r1..r3/`。`r1` 因未暂存 match 迁移被 child loader 拒绝，`r2` 因隔离 worktree 缺冻结 bench adapter 被准入拒绝，两次均在采集前失败并保留；补齐冻结输入后的 `r3` 为 PASS_WITH_WARNINGS（快照：INFO 33 / WARN 19 / ERROR 0 / FATAL 0），四节点基础收发均通过且 transport bad 为零。HDO 联合 DPLL 60 s 外部最大绝对相差 35.972 ns，三个内部 endpoint 最大绝对残差 95 ns（快照）；errors / cleanup_errors / restore_needed 为空，`check-staged` 与 pre-commit 通过。
- `r3` 仍报告 VDC/RefMem deadline 和 VDC quarantine 等 TDMA schedule-quality WARN，因此本次结论只证明 Core0 Tick 修复未破坏四节点基础运输与 60 s DPLL 相差门限，不把快速诊断结果提升为严格实时调度通过。隔离 worktree 与临时 HEAD 版 observer 已留在 `out/archive/pending-deletion/`，等待用户最终评审删除。

### TOOLS-PROGRESS-20260921-016：child observer snapshot wrapper 迁移

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-OBSERVER-001。
- 新增 `tools/workflows/vdc_joint_capture/observer.py`，从 manifest 指定的 match 入口构造 tracked ObserverProbe，接管 all-board STOP gate，以及按板序执行 event snapshot、每板立即 save、全部完成后才调用 parent collect 的 standalone 语义。internal-seal SummaryProbe 自己接管 summary collect，不调用该深层 collect；本 checkpoint 不声称 P3 发送了 event snapshot 查询。
- V10 manifest 将 archived `capture_observer_dma_match.py` 固定为 `observer_snapshot` migrated role，runtime entrypoint 下沉到 `capture_match.py`；动态 out closure 快照从 5 降到 4。fresh-process actual=declared=4，missing / declared-not-loaded / migrated_loaded 全为空；实际 provenance 中 archived observer 为零、tracked observer 唯一，旧原件不删除。
- 软件与独立审查：characterization 覆盖 barrier 缺失和 queries-blocked 在任何设备操作前拒绝、四板 snapshot 按序、逐板 save、parent collect 最后执行；实际 FollowProbe/TraceProbe 的 observer 指针均指向 tracked owner。最终宽组合 489 passed / 1 skipped（快照），跳过项仅为 Windows 无符号链接权限；工具治理/catalog、编译和 whitespace 检查通过。独立复核 PASS，无剩余 finding。
- 同指纹四板 P3 使用 build `20260920141158`，`tools-observer-r1/p3-r1` 为 PASS_WITH_WARNINGS（快照：INFO 33 / WARN 20 / ERROR 0 / FATAL 0）。tracked observer 在实际 MRO/provenance 中唯一，DPLL 60 s scope-on outer/inner/joint 通过，12 份 scope record、48 个 RAW block、四板 trace release 完整，外部最大绝对相差 5.669 ns（快照，INFO）；errors / cleanup_errors / restore_needed 均为空，`check-staged` 通过。原件位于 `out/HardwareAcceptance/20260921/tools-observer-r1/p3-r1/`。
- 实现提交 `9a86145e`；TOOLS-WORKFLOW-OBSERVER-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。下一批迁移 match/direct/typed/base 剩余祖先，继续保留 archived 原件和失败证据。

### TOOLS-PROGRESS-20260921-015：child local follow 层迁移

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-FOLLOW-001。
- 新增 `tools/workflows/vdc_joint_capture/follow.py`，从 manifest 指定的 observer 入口构造 tracked FollowProbe，接管 signed/wide follower status parser、独立 frequency decision 检查、follower enable/disable readback、STOP-only status snapshot 和 model/DCO endpoint 一致性；reference 不启用 follower mode，三个 follower assessment 相互隔离。
- V9 manifest 将 archived `capture_follow.py` 固定为 `local_follow` migrated role，runtime entrypoint 下沉到 `capture_observer_dma_match.py`；动态 out closure 快照从 6 降到 5。fresh-process actual=declared=5，missing / declared-not-loaded / migrated_loaded 全为空；实际 provenance 中 archived follow 为零、tracked follow 唯一，旧原件不删除。
- 软件与独立审查：冻结 `native-r1/input-probe.json` 的三个 follower `follow_raw`、parsed status、decision_check 与 model endpoint 对 tracked/archived 逐字段全等；prepared/interval/error/step/sequence 负测同拒。characterization 覆盖 SYNC disable、三个 follower enable/readback、四板 STOP status、parent model collect 和单 follower model mismatch 隔离。最终宽组合 485 passed / 1 skipped（快照），跳过项仅为 Windows 无符号链接权限；工具治理/catalog、编译和 whitespace 检查通过。独立复核 PASS，无剩余 finding。
- 同指纹四板 P3 使用 build `20260920141158`，`tools-follow-r1/p3-r1` 为 PASS_WITH_WARNINGS（快照：INFO 33 / WARN 20 / ERROR 0 / FATAL 0）。DPLL 60 s scope-on outer/inner/joint 均通过，12 份 scope record、48 个 RAW block、四板 trace release 完整，外部最大绝对相差 18.964 ns（快照，INFO）；errors / cleanup_errors / restore_needed 均为空，`check-staged` 通过。原件位于 `out/HardwareAcceptance/20260921/tools-follow-r1/p3-r1/`。
- 实现提交 `e9e9d920`；TOOLS-WORKFLOW-FOLLOW-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。下一批迁移 observer/match/direct/typed/base 剩余祖先，继续保留 archived 原件和失败证据。

### TOOLS-PROGRESS-20260921-014：child native trace 层迁移

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-TRACE-001。
- 新增 `tools/workflows/vdc_joint_capture/trace.py`，从 manifest 指定的 follow 入口构造 tracked TraceProbe，接管 trace status/ACK bounded polling、三个 follower ARM、STOP/download/decode、match/decision coverage、跨度和 last-decision endpoint 一致性，以及 release/retain 恢复；reference 不走 follower ARM。
- V8 manifest 将 archived `capture_trace.py` 固定为 `native_trace` migrated role，runtime entrypoint 下沉到 `capture_follow.py`；动态 out closure 快照从 7 降到 6。fresh-process actual=declared=6，missing / declared-not-loaded / migrated_loaded 全为空；实际 provenance 中 archived trace 为零、tracked trace 唯一，旧原件不删除。
- 软件与独立审查：characterization 覆盖 command/query 注册、ACK 成功/timeout 全 samples、follower-only ARM、成功 STOP/download/decode/coverage/span/last-decision/release，以及 download failure retain/no-release、endpoint mismatch 后 release、release failure 继续其他板。最终宽组合 476 passed / 1 skipped（快照），跳过项仅为 Windows 无符号链接权限；工具治理/catalog、编译和 whitespace 检查通过。独立复核 PASS，无剩余 finding。
- 同指纹四板 P3 使用 build `20260920141158`，`tools-trace-r1/p3-r1` 为 PASS_WITH_WARNINGS（快照：INFO 33 / WARN 19 / ERROR 0 / FATAL 0）。DPLL 60 s scope-on outer/inner/joint 均通过，12 份 scope record、48 个 RAW block、四板 trace release 完整，外部最大绝对相差 33.211 ns（快照，INFO）；errors / cleanup_errors / restore_needed 均为空，`check-staged` 通过。原件位于 `out/HardwareAcceptance/20260921/tools-trace-r1/p3-r1/`。
- 实现提交 `80f4475c`；TOOLS-WORKFLOW-TRACE-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。下一批迁移 follow/match 剩余祖先，继续保留 archived 原件和失败证据。

### TOOLS-PROGRESS-20260920-013：child long-window policy 层迁移

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-LONGWINDOW-001。
- 新增 `tools/workflows/vdc_joint_capture/longwindow.py`，从 manifest 指定的 native trace 入口构造 tracked LongWindowProbe，接管四板 reverse START、temporary origin trial、有限 quiet wait、rational secant 向外取整、同 baseline/model threshold progression 和最终 correlated projection。长区间继续标为 observation-only，不授予 lock/精度资格；单 follower oracle 失败独立进入 errors。
- V7 manifest 将 archived `capture_longwindow.py` 固定为 `longwindow` migrated role，runtime entrypoint 下沉到 `capture_trace.py`；动态 out closure 快照从 8 降到 7。fresh-process actual=declared=7，missing / declared-not-loaded / migrated_loaded 全为空；实际 provenance 中 archived long-window 为零、tracked long-window 唯一，旧原件不删除。
- 软件与独立审查：冻结 `native-r1/input-probe.json` 的三个 follower 各 8 个 decision，tracked `decision_oracle` / `check_windows` / `check_last_projection` 与 archived 和冻结结论逐字段全等；负测同拒。characterization 覆盖 NO4→NO1 START、trial 参数、11 s wait、非法 grant 和逐 follower collect 隔离。最终宽组合 470 passed / 1 skipped（快照），跳过项仅为 Windows 无符号链接权限；工具治理/catalog、编译和 whitespace 检查通过。独立复核 PASS，无剩余 finding。
- 同指纹四板 P3 使用 build `20260920141158`，`tools-longwindow-r1/p3-r1` 为 PASS_WITH_WARNINGS（快照：INFO 31 / WARN 28 / ERROR 0 / FATAL 0）。DPLL 60 s scope-on outer/inner/joint 均通过，12 份 scope record、48 个 RAW block、四板 trace release 完整，外部最大绝对相差 17.702 ns（快照，INFO）；errors / cleanup_errors / restore_needed 均为空，`check-staged` 通过。原件位于 `out/HardwareAcceptance/20260920/tools-longwindow-r1/p3-r1/`。
- 实现提交 `394ff57c`；TOOLS-WORKFLOW-LONGWINDOW-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。下一批迁移 native trace/follow/match 剩余祖先，继续保留 archived 原件和失败证据。

### TOOLS-PROGRESS-20260920-012：child phase / native trace 层迁移

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-PHASE-001。
- 新增 `tools/workflows/vdc_joint_capture/phase.py`，从 manifest 指定的 long-window 入口构造 tracked PhaseProbe，接管 52-word phase status parser、phase/origin trace ARM、STOP 后 native SRAM 下载/解码、phase/frequency endpoint 一致性和 trace release。只有完整读取的 trace 才 release；失败 trace 保留用于恢复，endpoint/release 错误进入 errors 并压低 passed。
- V6 manifest 将 archived `capture_phase_timer1.py` 固定为 `phase_timer1` migrated role，runtime entrypoint 下沉到 `capture_longwindow.py`；动态 out closure 快照从 9 降到 8。fresh-process actual=declared=8，missing / declared-not-loaded / migrated-loaded 全为空；运行 provenance 只记录 tracked phase 和下一层 long-window，archived phase 不执行，旧原件不删除。
- 软件与独立审查：真实 r4 follower endpoint 与 archived/tracked parser/constants 全等；characterization 覆盖 plan/provenance、phase/origin ARM、四板成功 collect/release、单板 download 失败 retain/no-release、endpoint 失败仍释放全部完整 trace、release 失败记错并继续其他板。最终宽组合 460 passed / 1 skipped（快照），跳过项仅为 Windows 无符号链接权限；工具治理/catalog、编译、source length 报告和 whitespace 检查通过。独立复核最终 PASS，无剩余 finding。
- 同指纹四板 P3 使用 build `20260920141158`，`tools-phase-r1/p3-r1` 为 PASS_WITH_WARNINGS（快照：INFO 33 / WARN 18 / ERROR 0 / FATAL 0）。DPLL 60 s scope-on outer/inner/joint 均通过，12 份 scope record、48 个 RAW block、四板 trace release 完整，外部最大绝对相差 10.185 ns（快照，INFO）；errors / cleanup_errors / restore_needed 均为空，`check-staged` 通过。原件位于 `out/HardwareAcceptance/20260920/tools-phase-r1/p3-r1/`。
- 实现提交 `0e72f1fe`；TOOLS-WORKFLOW-PHASE-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。下一批从 long-window/trace/follow/match 剩余祖先选择独立切片，继续保留 archived 原件和失败证据。

### TOOLS-PROGRESS-20260920-011：P3 四板 DPLL 60 s 短闭环门禁

- TODO task ID：TOOLS-P3-DPLL-SHORT-001；与 TOOLS-GATE-001、TOOLS-WORKFLOW-001 联动，但不据此关闭后二者。
- P3 bench 的 OTA、TDMA 和 DPLL 均固定为同一组四板，不再枚举、OTA、连接或要求 NO5。默认 quick 在 TDMA STOP handoff 后运行 `dpll_short_seconds`；配置 HDO 被 VISA 枚举时 scope-on，确认缺席时 scope-off 且 external=SKIPPED，VISA discovery 失败阻断。
- 相差策略由 `p3_dpll_short.py` 的 `PHASE_WARN_NS` / `PHASE_ERROR_NS` 唯一定义：外部路径只用 scope RAW 离线复算的 `relative_ns`，内部路径只用三个 follower 最终 residual interval，不跨测量域混算。缺样本、解析、内部健康、STOP/恢复失败记录 evidence ERROR；provisional receipt 只供同轮 joint，复核 package/build、OTA、calibration generation、TRN-03/TDMA readback 和原始 STOP records，且 `check-staged` 明确拒绝 provisional schema。
- 软件与独立审查：最终组合回归 449 passed / 1 skipped，跳过项仅为 Windows 无符号链接权限；工具治理/catalog、文档门禁、编译和 whitespace 检查通过。独立审查先发现 evidence error 误标相差超限、provisional 准入过弱、owner 依赖环、scope discovery 未交叉绑定和 short schema 虚假 lock WARN；逐项修复后最终 PASS，无剩余 finding。
- 失败证据保留：`p3-dpll-short-r1` 在 archived phase 链再次使用旧 quick validator 时 child 拒绝；`p3-dpll-short-r2` 在 provisional 缺 calibration generation 时配置拒绝。两次 outer 均完成四板 STOP、RAM timing/delay 恢复和最终 readback，cleanup_errors / restore_needed 为空；失败不改写为通过。
- 功能通过 `p3-dpll-short-r3` 后，registry/catalog owner 修正改变源码指纹，因此不作为最终凭证。最终同指纹 `p3-dpll-short-r4` 使用 build `20260920141158`，结果 PASS_WITH_WARNINGS（快照：INFO 33 / WARN 20 / ERROR 0 / FATAL 0）；DPLL 为 60 s scope-on，12 份 scope record、48 个 RAW block、三个内部 endpoint、errors 为空，外部最大绝对相差 8.301 ns（快照，INFO），RAW 离线复算和 `check-staged` 通过。原件位于 `out/HardwareAcceptance/20260920/p3-dpll-short-r1..r4/`。
- 对用户提出的 150→250 MHz Core0 tick 假设做了代码核验：当前分支与 `origin/feature/node-sequence-reservation-trigger` 的 FreeRTOS/board 时钟配置无差异；`configCPU_CLOCK_HZ` 与 `BOARD_SYS_CLOCK_HZ` 均为 250 MHz 对应符号值，board 在 scheduler 前升频，RP2350 FreeRTOS port 从运行时 `clock_get_hz(clk_sys)` 装载 Core0 SysTick。Core1 TDMA 使用独立 core-local SysTick。因此未找到 150 MHz 残留，不能把本轮失败归因于 Core0 tick，也未据此修改固件。
- 实现提交 `181bc3a4`。后续继续 TOOLS-WORKFLOW-001 的 phase/follow/match 祖先迁移；P3 新门禁已作为每个实现批次的默认验收路径。

### TOOLS-PROGRESS-20260920-010：child probe support 纯叶端迁移

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-PROBE-SUPPORT-001。
- 新增 `tools/domains/vdc/priority_trace/probe_support.py`，迁入 UID 顺序、SCPI 字段解析、receipt provenance、observer delta 和 frozen native summary。V5 manifest 将 archived helper 固定为 `probe_support` migrated role；runtime closure 从 10 降到 9，旧源码原位归档且不执行。
- tracked RUN loader 仅在持有 Python 全局 import lock 时为历史 phase 链提供兼容模块名，成功、异常和并发路径均恢复原 `sys.modules` 状态；继承报告移除未执行 archived helper 的来源行，改记实际 tracked 路径/SHA。helper 独立登记为无下游依赖 leaf，workflow 与 priority trace 单向依赖，登记图无环。
- 软件与审查证据：聚焦回归 109 passed，最终宽回归 300 passed / 1 skipped，跳过项为 Windows 无符号链接权限；工具治理/catalog、编译和 whitespace 检查通过。独立复核先发现 alias 并发、provenance 和登记依赖三项问题，修复后 PASS，无剩余 finding。
- 同指纹硬件验收快照，非事实源：首次默认五板尝试 `p3-r1` 因旧配置要求未枚举 NO5，在 OTA 准入阶段拒绝并保留原件；随后 build `20260920130659` 的四板 `p3-r2` 为 PASS_WITH_WARNINGS（INFO 33 / WARN 22 / ERROR 0 / FATAL 0），凭证与实现指纹一致。该旧 P3 仍将 DPLL 标为 SKIPPED，后续按用户要求改为四板 60 s DPLL 闭环，不再要求 NO5。
- scope-on `joint-scope-on-r1` outer/inner/joint PASS：12 个新鲜窗口、48 个 RAW block，最大绝对相差 41.643 ns（快照）；11 个 migrated source / 9 个 runtime closure，报告只记录实际 tracked helper，RUN 内板端查询为零，四板 timing/delay 恢复，errors / cleanup_errors / restore_needed 均为空。原件位于 `out/HardwareAcceptance/20260920/tools-probe-support-r1/`。
- 实现提交 `3b3d8d02`；TOOLS-WORKFLOW-PROBE-SUPPORT-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。下一批迁移 phase/follow/match 深层祖先；旧原件仍只归档，不删除，任何待删除候选移入归档区后由用户最终评审。

### TOOLS-PROGRESS-20260920-009：child RUN output 层迁移

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-RUN-001。
- 新增 `tools/workflows/vdc_joint_capture/run_output.py`，接管 119 字段 schema10 parser、timing snapshot/apply、四板 RUN prepare/parser 注册、output STOP-before-parent-barrier 和 STOP 后 RUN activity/retirement/timing/anchor 校验。外层 tracked lifecycle 继续作为 timing/delay 唯一恢复 owner；历史 RunProbe 内部 scope owner显式拒绝，联合 scope 只走已迁移的外部 sparse-scope。
- V4 manifest 将 archived `capture_run_output_schema10.py` 纳入固定 `run_output` migrated role，runtime entrypoint 下沉到 phase 层，动态 out closure 从 11 降到 10。默认 manifest/loader schema 有直接自回归；fresh closure actual=declared=10，missing / declared-not-loaded / migrated-loaded 为空。
- 软件与审查证据：tracked characterization 覆盖 schema10 合法行和八类 closed-set 拒绝、timing 读写、RUN prepare、parser 表、output STOP 顺序、STOP-only 四板校验和内部 scope 拒绝；最终宽组合 336 passed / 1 skipped，跳过项为 Windows 无符号链接权限。archived/tracked golden 的 FIELDS/WIDE 完全一致，119 字段合法行 parse dict 全等，八类负测两边同拒；四板 internal-seal golden 继续全等。独立复核在当前 tracked SHA 上 PASS；登记/catalog、编译、source length 与 whitespace 检查通过。
- 同指纹 P3 快照，非事实源：build `20260920121536` 的四板 quick P3 为 PASS_WITH_WARNINGS（INFO 23 / WARN 31 / ERROR 0 / FATAL 0），DPLL 按 tdma-only 范围 SKIPPED；原件在 `out/HardwareAcceptance/20260920/tools-run-output-r1/p3-r1/`，staged receipt 同指纹。
- scope-off `joint-scope-off-r1` outer/inner PASS：V4 报告记录 10 migrated / 10 runtime 与 run-output SHA，internal health 通过、external SKIPPED、RUN 内板端查询为零，四板 timing/delay 恢复，errors / cleanup_errors / restore_needed 均为空。
- scope-on `joint-scope-on-r1` outer/inner PASS：12 个新鲜窗口、48 RAW 块、steady qualification 通过，最大绝对相差 32.804 ns（快照）；RUN 内板端查询为零，四板 timing/delay 恢复且无清理错误。硬件原件位于同任务目录，scope 结果不授予未采区间连续锁相或精确事件对应资格。
- 实现提交 `95f2504d`；TOOLS-WORKFLOW-RUN-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。下一批迁移剩余 10-file phase/follow/match 深层祖先；十份 migrated source 原位归档，不删除。

### TOOLS-PROGRESS-20260920-008：child runtime control / parallel START 基座迁移

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-RUNTIME-001。
- 新增 `tools/workflows/vdc_joint_capture/runtime_base.py`，直接加载剩余 `capture_run_output_schema10.py` RunProbe 入口，接管历史 monitor 的 deadline/report、phase/RUN control 和 bounded parallel START。parallel START 从 V3 manifest 接受四板 allowlist，只使用已验证 persistent session 和互异 serial transport，保留 action budget、quiet barrier、四 worker join、排序/JSONL 及 fail-after-join 语义。
- V3 manifest 将 `external_monitor / parallel_startup / late_scope_adapter / outer_trial / outer_hil / legacy_scope_reader` 六份旧实现加入固定角色 migrated lineage；它们继续按路径/SHA 原位归档，但不再执行。runtime entrypoint 下沉到 RunProbe，out 动态闭包由 17 降到 11。fresh-process 动态检查遍历 factory 对象图和 `sys.modules`，actual=declared，missing / declared-not-loaded / migrated-loaded 全为空；原件在 `out/tools-governance/20260920/runtime-base-r1/closure.json`。
- 软件/审查证据：runtime fake characterization 覆盖 master/follower phase 与 RUN prepare 顺序、UID/port/allowlist、persistent session、distinct transport、deadline/action budget、quiet barrier、并发 join、单板 timeout 后全 join 和日志；宽组合回归 331 passed / 1 skipped，跳过项为 Windows 无符号链接权限。V3 frozen golden 为 9 migrated / 11 runtime，四板 archived/tracked parser/evaluation 逐字段全相等。独立复核 PASS，无剩余 finding；登记/catalog、编译、source length 与 whitespace 检查通过。
- 同指纹 P3 快照，非事实源：build `20260920113143` 的四板 quick P3 为 PASS_WITH_WARNINGS（INFO 25 / WARN 19 / ERROR 0 / FATAL 0），DPLL 按 tdma-only 范围 SKIPPED；原件在 `out/HardwareAcceptance/20260920/tools-runtime-base-r1/p3-r1/`，staged receipt 同指纹。
- scope-off `joint-scope-off-r1` outer/inner PASS：V3 报告记录 9 migrated / 11 runtime 与 runtime-base SHA，internal health 通过、external SKIPPED、RUN 内板端查询为零，四板 timing/delay 恢复，errors / cleanup_errors / restore_needed 均为空。
- scope-on `joint-scope-on-r1` outer/inner PASS：12 个新鲜窗口、48 RAW 块、steady qualification 通过，最大绝对相差 35.638 ns（快照）；RUN 内板端查询为零，四板 timing/delay 恢复且无清理错误。scope 结果不授予未采区间连续锁相或精确事件对应资格。硬件原件位于同任务目录。
- 实现提交 `b6fe5cf8`；TOOLS-WORKFLOW-RUNTIME-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。下一批迁移剩余 11-file RunProbe/phase/follow/match 深层祖先；九份 migrated source 原位归档，不删除。

### TOOLS-PROGRESS-20260920-007：child internal summary / startup / seal 三层迁移

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-SEAL-001。
- 新增 `tools/workflows/vdc_joint_capture/internal_seal.py`，在 tracked factory 中重建 internal summary trace、可选 pre-START MODEL 省略和 sealed GUARD/parser/verdict 三层；联合 child 只从 manifest 的 `runtime_entrypoint` 加载下一层，不再 exec archived compatibility selector。V2 manifest 用固定角色 `internal_seal / quiet_startup / internal_summary` 保存三份原件路径/SHA，compatibility selector 必须绑定 internal_seal；三份 migrated source 与 runtime closure 不得重叠，out 动态执行闭包由 20 降至 17。
- 软件与等价性证据：tracked fake-runtime characterization 实际构造 SealProbe，覆盖四板 summary arm/parser 注册、optional MODEL 不发送、SUMMARY→GUARD 与 expiry 命令改写、quiet parallel START、STOP collect gate、cleanup 后资格重算；最终组合回归 321 passed / 1 skipped，跳过项为 Windows 无符号链接权限。frozen golden 使用上一批通过的四板 native SRAM 和 RUN raw，对 archived/tracked parser 及 `evaluate_board` 逐字段比较，四板全相等且 PASS；原件在 `out/tools-governance/20260920/internal-seal-r1/`。
- 独立审查先后发现并推动修复：golden 路径核对、固定 lineage 角色、compatibility selector 与 internal_seal 强绑定、真实 tracked 类 characterization、GUARD parser 表直接断言及 registry 债务措辞。最终复核 PASS，无剩余 finding；工具登记/catalog、编译、source length 和 whitespace 检查通过。
- P3 失败与复验，快照非事实源：修复前指纹 `p3-r1` 在 TRN-00 MARK 未形成完整 selected offset row 时拒绝，原件保留且不改写；最终 build `20260920105121` 的 `p3-r2` 为 PASS_WITH_WARNINGS（INFO 25 / WARN 21 / ERROR 0 / FATAL 0），DPLL 按 tdma-only 范围 SKIPPED，staged receipt 同指纹。
- 最终 scope-off 实测 `joint-scope-off-r1` outer/inner PASS：V2 manifest 在 child 报告中记录固定三角色和 17-file runtime closure，tracked adapter SHA 与源码一致，internal health 通过、external SKIPPED、RUN 内板端查询为零，未创建 scope/sample 目录；四板 timing/delay 恢复，errors / cleanup_errors / restore_needed 均为空。
- 示波器恢复后补 scope-on 组合实测 `joint-scope-on-r1` outer/inner PASS：12 个新鲜窗口、48 RAW 块、steady qualification 通过，最大绝对相差 47.929 ns（快照）；RUN 内板端查询为零，四板 timing/delay 恢复，errors / cleanup_errors / restore_needed 均为空。scope 证据不授予未采区间连续锁相或精确事件对应资格。硬件原件均在 `out/HardwareAcceptance/20260920/tools-internal-seal-r1/`。
- 实现提交 `8a53402c`；TOOLS-WORKFLOW-SEAL-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。下一批迁移 `monitor_external`、startup 和 RunProbe 的更深基础链；17 份 runtime 原件继续按 SHA 准入，旧三份 migrated source 原位归档，不删除。

### TOOLS-PROGRESS-20260920-006：联合采样 outer 生命周期迁入 tracked workflow

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-LIFECYCLE-001。
- 新增 `tools/workflows/vdc_joint_capture/lifecycle.py`，接管父进程四板 UID→动态 COM 映射、串口 session、output/TDMA 双 STOP、identity/build/board order、RAM timing/delay 临时应用、child 日志与 timeout、最终恢复/读回和逐板关闭。联合入口 outer 分支不再 import adapter 或经 `base.external.base.trial.hil` 取得历史 runner；adapter 仅在已准入的 child 分支导入。
- 生命周期只迁入本工作流实际调用面，不带入历史 Flash STORE/reboot HIL。`scpi_serial.SerialSession` 新增向后兼容的实例级 response matcher，识别 timing 三元组及 signed delay，不再靠历史模块全局 monkey patch。报告保留 `DPLL_RAM_TIMING_TRIAL_V1`、`basis_script_sha256` 和原结果字段，新增 lifecycle version/source 及 restore detail。
- 安全语义：所有可达板收到两类 STOP 后才允许首个 query；RAM timing 与 delay 分别保持 dirty/uncertain，只有最终逐板 STOP、identity、PI/role/baseline、timing/delay 和 error queue 全过才清除。device-free 用例覆盖 child/restore/final drift/open/close/真实 subprocess timeout 等失败路径；独立复核在报告兼容、登记副作用和 restore false-negative 修复后最终 PASS。
- 软件验证快照，非事实源：最终组合回归 312 passed / 1 skipped，跳过项为 Windows 无符号链接权限；lifecycle/joint/TDMA 定向 146 passed。共享 SCPI 宽回归 521 / 522 passed，唯一失败是未修改的 `test_trn03_closed_loop.py` 对未修改 `application/src/app.c` 的既有文本断言，本批不把它改写为通过。工具登记/catalog、编译、source length 与 whitespace 检查通过。
- 硬件验收快照，非事实源：build `20260920094117` 的 scope-off 四板 P3 为 PASS_WITH_WARNINGS（INFO 23 / WARN 22 / ERROR 0 / FATAL 0），DPLL 按 tdma-only 范围 SKIPPED；原件在 `out/HardwareAcceptance/20260920/tools-workflow-lifecycle-r1/p3-r1/`，staged receipt 与实现指纹一致。
- 首次实测 `joint-scope-off-r1` 保留 FAIL：NO1 并行 START 在原 action timeout 内返回 `<timeout>`，其余三板 OK；child 按既有规则 STOP，NO1 留下 `-200`，outer 记录 child 和 restore error，随后 final readback 证明四板 timing/delay 已恢复且 `restore_needed=[]`，失败未被覆盖。
- 同源码/凭证有界复验 `joint-scope-off-r2` PASS：九个 outer stage 全过，父报告 lifecycle SHA 与源码一致，四板 initial/final timing 和 delay 精确相同；child internal health 与 joint verdict 通过，RUN 内板端查询为零，external 为 SKIPPED，未创建 scope/sample 目录，errors / cleanup_errors / restore_needed 均为空。原件位于同任务目录。
- 实现提交 `f37c5fa0`；TOOLS-WORKFLOW-LIFECYCLE-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。下一步迁移不依赖示波器的 child Probe/基础 adapter；历史 outer 文件因 child 祖先仍 import 而暂留 manifest closure，不删除原件或提前宣称全链解除。

### TOOLS-PROGRESS-20260920-005：internal-seal 工作流输入显式化

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-INPUTS-001。
- 新增 `config/tool_workflows/vdc_joint_capture_internal_seal.json` 与 `tools/workflows/vdc_joint_capture/inputs.py`，将基础场景的 adapter entrypoint、完整历史可执行闭包、P3 receipt、stopped baseline、tap baseline 和四板 UID 顺序变为单一 tracked 输入清单。现有 CLI 路径与 `--bench-adapter` 保留，所选 adapter 必须与清单一致；vector/reference/gap 场景未混入。
- 准入顺序：解析 UTF-8 JSON、拒绝路径逃逸/重复、逐文件比对 SHA、校验 baseline 每板实际消费字段和唯一命令集合、校验 tap 主从请求、限定 quick-graded receipt schema 并验证完整证据/当前源码指纹/板卡顺序，全部通过后才动态 import adapter。父进程和子进程均在 `run()` 前写入 manifest、完整 closure 与摘要。
- 独立审查先后发现并推动修复：只 pin 最外 adapter、receipt schema 未显式限制、baseline/tap 深层字段浅校验、准入证据落盘过晚。最终复核 PASS，无剩余 finding；声明 closure 与既有基础链审计一致，当前 SHA 全部匹配，Git tracked 依赖由 P3 源码指纹覆盖。
- 软件验证快照，非事实源：最终组合回归 254 passed / 1 skipped，跳过项为当前 Windows 账户无符号链接权限；聚焦联合采样/输入/scope 组 159 passed。工具登记/catalog、JSON、编译和 whitespace 检查通过；旧 receipt 对新源码在 adapter import 和硬件前按预期拒绝。
- 硬件验收快照，非事实源：build `20260920085328` 的 scope-off 四板 P3 为 PASS_WITH_WARNINGS（INFO 25 / WARN 19 / ERROR 0 / FATAL 0），DPLL 按 tdma-only 范围 SKIPPED；原件在 `out/HardwareAcceptance/20260920/tools-workflow-inputs-r1/p3-r1/`，staged receipt 与当前实现指纹一致。
- scope-off 基础工作流实测快照，非事实源：`joint-scope-off-r1` outer/inner 均 PASS，运行 60 s，manifest closure 在父子报告中均完整，RUN 内板端查询为零，external status 为 SKIPPED，未创建 scope/sample 目录；errors / cleanup_errors / restore_needed 均为空。原件在 `out/HardwareAcceptance/20260920/tools-workflow-inputs-r1/joint-scope-off-r1/`。
- 实现提交 `3f13f8d4`；TOOLS-WORKFLOW-INPUTS-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。当前示波器已移出台架，下一批只推进不需要示波器的 Probe/Trial、基础 adapter、静态检查或离线交接工作，不重跑 scope 硬件。

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
